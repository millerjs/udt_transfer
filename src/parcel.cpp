/*****************************************************************************
Copyright 2013 Laboratory for Advanced Computing at the University of Chicago

              This file is part of parcel by Joshua Miller

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions
and limitations under the License.
*****************************************************************************/

#include "parcel.h"
#include "timer.h"
#include "sender.h"
#include "receiver.h"
#include "files.h"
#include "postmaster.h"
#include "thread_manager.h"

#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <execinfo.h>

int g_opt_verbosity = 1;
int g_opt_debug_file_logging = 0;
char g_base_path[MAX_PATH_LEN];

char g_logfilename[] = "debug.log";

// Statistics globals
int g_timer = 0;
off_t G_TOTAL_XFER = 0;

#include "util.h"

#include "udpipe.h"
#include "udpipe_threads.h"
#include "udpipe_server.h"
#include "udpipe_client.h"

typedef enum: uint8_t {
    PARCEL_STATE_MASTER,
    PARCEL_STATE_SLAVE,
    NUM_PARCEL_STATES
} parcel_states;


typedef enum: uint8_t {
    PARCEL_SUBSTATE_SENDING,
    PARCEL_SUBSTATE_RECEIVING,
    NUM_PARCEL_SUBSTATES
} parcel_substates;


typedef struct parcel_state_t {
    parcel_states state;
    parcel_substates substate;
} parcel_state_t;


remote_arg_t g_remote_args;
parcel_opt_t g_opts;

typedef enum : uint8_t {
    UDPIPE_SERVER,
    UDPIPE_CLIENT,
    NUM_UDPIPE_TYPES
} udpipe_t;

using std::cerr;
using std::endl;

void cleanup_pipes();


/* 
 * void print_bytes:
 * - print the bytes at pointer object for length size
 */
void print_bytes(const void *object, size_t size) 
{
    size_t i;
    
    fprintf(stderr, "[ ");
    for(i = 0; i < size; i++){
        fprintf(stderr, "%02x ", ((const unsigned char *) object)[i] & 0xff);
    }
    fprintf(stderr, "]\n");
}


/* 
 * header_t nheader
 * - creates and initializes a new header with type [type] and length [size]
 * - returns: new header
 */
header_t nheader(xfer_t type, off_t size)
{
    header_t header;
    header.type = type;
    header.data_len = size;
    header.mtime_sec = 88;
    header.mtime_nsec = 88;
    return header;
}

/* 
 * void usage
 * - print the usage information
 */
void usage(int EXIT_STAT)
{

    char* options[] = {
        "--help \t\t\t print this message",
        "--remote (-r) host:dest \t immitate scp with parcel and udpipe",
        "--checkpoint (-k) log_file \t log transfer to file log_file.  If log_file exists",
        "\t\t\t\t from previous transfer, resume transfer at last completed file.",
        "--verbose \t\t\t verbose, notify of files being sent. Same as -v2",
        "--quiet \t\t\t silence all warnings. Same as -v0",
        "--no-mmap \t\t\t do not memory map the file (involves extra memory copy)",
        "--full-root \t\t\t do not trim file path but reconstruct full source path",
        "--pipe pipe_cmd \t\t attempt to connect to specified pipe executable",
        "--log (-g) log_file \t\t log transfer to file log_file but do not restart",
        "--restart log_file \t\t restart transfer from file log_file but do not log",
        "",

        "-l [dest_dir] \t\t listen for file transfer and write to dest_dir [default ./]",
//        "-v verbosity level \t\t set the level of verbosity",
        "-v enables verbose output",
        "",

        "Remote transfers: --remote (-r) host:dest",
        "\tThis option requires udpipe [up] to be in your path. parcel",
        "\twill attempt to execute udpipe on the specified host over",
        "\tssh.  If successful, it will transfer file list to",
        "\tdirectory dest.",
        
        "\nLevels of Verbosity:",
        "\t0: Withhold WARNING messages",
        "\t1: Update user on file transfers [DEFAULT]",
        "\t2: Update user on underlying processes, i.e. directory creation ",
        "\t3: Print information on optimizations ",
        "\t4: Print any information generated",
        NULL
    };
    
    fprintf(stderr, "Basic usage: \n\tparcel source_dir | parcel -l dest_dir\n");
    fprintf(stderr, "Options:\n");
    
    for (int i = 0; options[i]; i++)
        fprintf(stderr, "   %s\n", options[i]);
    
    exit(EXIT_STAT);

}


/* 
 * void verb
 * - print message to stdout depending on user's selection of verbosity
 */
void verb(int verbosity, char* fmt, ... )
{
    if (g_opt_verbosity >= verbosity) {
        va_list args; va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        va_end(args);
        if ( g_opt_debug_file_logging ) {
            FILE* debug_file = fopen(g_logfilename, "a+");
            va_list args; va_start(args, fmt);
            vfprintf(debug_file, fmt, args);
            fprintf(debug_file, "\n");
            fflush(debug_file);
            fclose(debug_file);
            va_end(args);
        }
    }
}


/* 
 * void warn
 * - warn the user unless verbosity set to VERB_0 (0)
 */
void warn(char* fmt, ... )
{
    if (g_opt_verbosity > VERB_0){
        va_list args; va_start(args, fmt);
        fprintf(stderr, "%s: warning:", __func__);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        va_end(args);
    }
}


// /* 
//  * void error
//  * - print error to stdout and quit
//  * - note: error() takes a variable number of arguments with 
//  *   format string (like printf)
//  */
// void error(char* fmt, ... )
// {
//     va_list args; va_start(args, fmt);
//     fprintf(stderr, "%s: error:", __func__);
//     vfprintf(stderr, fmt, args);
//     if (errno) perror(" ");
//     fprintf(stderr, "\n");
//     va_end(args);
//     clean_exit(EXIT_FAILURE);
// }


/* 
 * int kill_children
 * - Make sure that we have killed any zombie or orphaned children
 * - returns: returns 0 on success, does not return on failure
 */
int kill_children(int verbosity)
{

    // Clean up the ssh process
    if (g_remote_args.ssh_pid && g_remote_args.remote_pid){

        if (g_opt_verbosity >= verbosity) 
            fprintf(stderr, "[%s] Killing child ssh process... ", __func__);

        if (kill(g_remote_args.ssh_pid, SIGINT)){
            if (g_opt_verbosity >= verbosity) 
                perror("FAILURE");
        } else {
            verb(verbosity, "success.");
        }

        int ssh_kill_pid;
        ssh_kill_pid = fork();

        // CHILD
        if (ssh_kill_pid == 0) {

            if (g_opt_verbosity >= verbosity) 
                fprintf(stderr, "[%s] Killing remote parcel process... ", __func__);

            char kill_cmd[MAX_PATH_LEN];
            sprintf(kill_cmd, "kill -s SIGINT %d 2> /dev/null", g_remote_args.remote_pid);
            char *args[] = {"ssh", "-A", g_remote_args.pipe_host, kill_cmd, NULL};

            // Execute the pipe process
            if (execvp(args[0], args)){
                fprintf(stderr, "[%s] WARNING: unable to kill remote process.", __func__);
                exit(EXIT_FAILURE);
            }

        } 

        // PARENT
        else {
            int stat;
            waitpid(ssh_kill_pid, &stat, 0);
            if (!stat){
                verb(verbosity, "success.");
            }
        }
    }

    return RET_SUCCESS;
}


/* 
 * double get_scale
 * - takes the transfer size and an allocated label string
 * - returns: the ratio to scale the transfer size to be human readable
 * - state  : writes the label for the scale to char*label
 */
double get_scale(off_t size, char*label)
{
    char    tmpLabel[8];
    double  tmpSize = 1.0;
    
    if (size < SIZE_KB){
        sprintf(tmpLabel, "B");
        tmpSize = SIZE_B;
    } else if (size < SIZE_MB){
        sprintf(tmpLabel, "KB");
        tmpSize = SIZE_KB;
    } else if (size < SIZE_GB){
        sprintf(tmpLabel, "MB");
        tmpSize = SIZE_MB;
    } else if (size < SIZE_TB){
        sprintf(tmpLabel, "GB");
        tmpSize = SIZE_GB;
    } else if (size < SIZE_PB){
        sprintf(tmpLabel, "TB");
        tmpSize = SIZE_TB;
    } else {
        sprintf(tmpLabel, "PB");
        tmpSize = SIZE_PB;
    } 

    sprintf(label, tmpLabel);
//    fprintf(stderr, "get_scale: size = %ld, label = %s, newSize = %f\n", size, label, tmpSize);
//    label = "[?]";
    return tmpSize;
}


/* 
 * void print_xfer_stats
 * - prints the average speed, total data transfered, and time of transfer to terminal
 */
void print_xfer_stats()
{
    char label[8];
    if (g_opts.verbosity >= VERB_2 || g_opts.progress){
        stop_timer(g_timer);
        double elapsed = timer_elapsed(g_timer);

        double scale = get_scale(G_TOTAL_XFER, label);

        fprintf(stderr, "\t\tSTAT: %.2f %s transfered in %.2fs [ %.2f Gbps ] \n", 
                G_TOTAL_XFER/scale, label, elapsed, 
                G_TOTAL_XFER/(elapsed*SIZE_GB));
    }
}


#define MAX_OUTPUT_COUNT    10000
/* 
 * void clean_exit
 * - a wrapper for exit 
 * - note: this is important or subsequent transfers will encounter zombie children
 */
void clean_exit(int status)
{
    verb(VERB_2, "[%s] Start", __func__);
    close_log_file();
    print_xfer_stats();
    cleanup_pipes();
    SetExit();

    int counter = 0;
    verb(VERB_2, "\n");
    while ( GetThreadCount() > 0 ) {
        if ( counter == 0 ) {
            verb(VERB_2, "[%s] Waiting on %d threads to exit", __func__, GetThreadCount());
            PrintThreads();
            counter = MAX_OUTPUT_COUNT;
        } else {
            counter--;
        }
        usleep(100);
    }

    exit(status);
}


/* 
 * void sig_handler
 * called when signals are passed to parcel
 * - note: set by set_handlers
 */
void sig_handler(int signal)
{

    if (signal == SIGINT){
        verb(VERB_0, "\nERROR: [%d] received SIGINT, cleaning up and exiting...", getpid());
    }

    if (signal == SIGSEGV){
        verb(VERB_0, "\nERROR: [%d] received SIGSEV, caught SEGFAULT cleaning up and exiting...", getpid());
#ifdef DEBUG_BACKTRACE
        fprintf( stderr, "\n********* SEGMENTATION FAULT *********\n\n" );

        void *trace[32];
        size_t size, i;
        char **strings;

        size    = backtrace( trace, 32 );
        strings = backtrace_symbols( trace, size );

        fprintf( stderr, "\nBACKTRACE:\n\n" );

        for( i = 0; i < size; i++ ){
            fprintf( stderr, "  %s\n", strings[i] );
        }

        fprintf( stderr, "\n***************************************\n" );
#endif
    }

    // Kill children and let user know
    kill_children(VERB_2);
    clean_exit(EXIT_FAILURE);
}


/* 
 * int print_progress
 * - prints the fraction of data transfered for the current file with a carriage return
 * - returns: RET_SUCCESS on success, RET_FAILURE on failure
 */
int print_progress(char* descrip, off_t read, off_t total)
{
    char label[8];
    char fmt[1024];
    int path_width = 60;

    // Get the width of the terminal
    struct winsize term;
    if (!ioctl(fileno(stdout), TIOCGWINSZ, &term)){
        int progress_width = 35;
        path_width = term.ws_col - progress_width;
    }

    // Scale the amount read and generate label
    off_t ref = (total > read) ? total : read;
    double scale = get_scale(ref, label);

    // if we know the file size, print percentage of completion
    if (total) {
        double percent = total ? read*100./total : 0.0;
        sprintf(fmt, "\r +++ %%-%ds %%0.2f/%%0.2f %%s [ %%.2f %%%% ]", path_width);
        verb(VERB_2, fmt, descrip, read/scale, total/scale, label, percent);

    } else {
        sprintf(fmt, "\r +++ %%-%ds %%0.2f/? %%s [ ? %%%% ]", path_width);
        verb(VERB_2, fmt, descrip, read/scale, label);
    }

    return RET_SUCCESS;
}


/* 
 * int run_ssh_command
 * - run the ssh command that will create a remote parcel process
 * - returns: RET_SUCCESS on success, RET_FAILURE on failure
 */
//int run_ssh_command(char *remote_path)
int run_ssh_command()
{
    parse_destination(g_remote_args.xfer_cmd);

    if ( !strlen(g_remote_args.remote_path) ) {
        warn("remote destination was not set");
        return RET_FAILURE;
    } 
    
    verb(VERB_2, "[%s %d] Attempting to run remote command to %s:%s", __func__, getpid(),
         g_remote_args.pipe_host, g_remote_args.pipe_port);

    // Create pipe and fork
    g_remote_args.ssh_pid = fork();

    // CHILD
    if (g_remote_args.ssh_pid == 0) {

        char remote_pipe_cmd[MAX_PATH_LEN];
        char cmd_options[MAX_PATH_LEN];

        memset(remote_pipe_cmd, 0, sizeof(char) * MAX_PATH_LEN);
        memset(cmd_options, 0, sizeof(char) * MAX_PATH_LEN);

        // Redirect output from ssh process to ssh_fd
        char *args[] = {
            "ssh",
            "-A", 
            g_remote_args.pipe_host, 
            remote_pipe_cmd, 
            NULL
        };

        sprintf(remote_pipe_cmd, "%s ", g_remote_args.udpipe_location);

        if (g_opts.encryption){
            strcat(remote_pipe_cmd, " -n ");
            char n_crypto_threads[MAX_PATH_LEN];     
            sprintf(n_crypto_threads, "--crypto-threads %d ", g_opts.n_crypto_threads);
            strcat(remote_pipe_cmd, n_crypto_threads);
        }

        if (g_opts.mode == MODE_SEND) {

            ERR_IF(g_opts.remote_to_local, "Attempting to create ssh session for remote-to-local transfer in mode MODE_SEND\n");

            if (g_remote_args.remote_ip){
                sprintf(cmd_options, "--interface %s -xt -p %s %s ", 
                        g_remote_args.remote_ip,
                        g_remote_args.pipe_port, 
                        g_remote_args.remote_path);
            } else {
                sprintf(cmd_options, "-xt -p %s %s ", 
                        g_remote_args.pipe_port, 
                        g_remote_args.remote_path);
            }

        } else if (g_opts.mode == MODE_RCV) {

            ERR_IF(!g_opts.remote_to_local, "Attempting to create ssh session for local-to-remote transfer in mode MODE_RCV\n");

            sprintf(cmd_options, "-x -q %s -p %s %s ", 
                    g_remote_args.pipe_host,
                    g_remote_args.pipe_port,
                    g_remote_args.remote_path);
        }

        strcat(remote_pipe_cmd, cmd_options);

        verb(VERB_2, "[%s %d] ssh command: ", __func__, getpid());
        for (int i = 0; args[i]; i++) {
            verb(VERB_2, "args[%d]: %s", i, args[i]);
        }

        ERR_IF(execvp(args[0], args), "unable to execute ssh process");
        ERR("premature ssh process exit");
    }

    verb(VERB_2, "[%s %d] exit", __func__, getpid());
    return RET_SUCCESS;
}


/* 
 * int get_remote_pid
 * - Attempts to read the process id from the remote process
 * - returns: nothing
 */
int get_remote_pid()
{
    // Try and get the pid of the remote process from the ssh pipe input
    if (read(g_opts.recv_pipe[0], &g_remote_args.remote_pid, sizeof(pid_t)) < 0) {
        perror("WARNING: Unable to read pid from remote process");
    } 

    // Read something from the pipe, proceed
    else {
        verb(VERB_2, "[%s] Remote process pid: %d\n", __func__, g_remote_args.remote_pid);
    }

    return 0;
}


/* 
 * void initialize_udpipe_args
 * - 
 * - returns: nothing
 */
void initialize_udpipe_args(thread_args *args)
{
    args->ip               = NULL;
    args->listen_ip        = NULL;
    args->port             = NULL;

    args->enc              = NULL;
    args->dec              = NULL;

    args->udt_buff         = BUFF_SIZE;
    args->udp_buff         = BUFF_SIZE;

    args->blast            = 0;
    args->blast_rate       = 1000;
    args->mss              = 8400;
    args->n_crypto_threads = 1;
    args->print_speed      = 0;
    args->timeout          = 0;
    args->use_crypto       = 0;
    args->verbose          = 0;
}


/* 
 * int parse_destination
 * - parse argument xfer_cmd for host:destination
 * - returns: RET_SUCCESS on success, RET_FAILURE on failure
 */
int parse_destination(char *xfer_cmd)
{
    verb(VERB_2, "[%s] xfer_cmd = %s, len %d", __func__, xfer_cmd, strlen(xfer_cmd));
    int hostlen = -1;
    int cmd_len = strlen(xfer_cmd);

    // we've already set the pipe_host, don't overwrite
    if (strlen(g_remote_args.pipe_host)) {
        return RET_FAILURE;
    }

    // the string appears not to contain a remote destination
    hostlen = strchr(xfer_cmd, ':') - xfer_cmd + 1;
    ERR_IF(!hostlen, "Please specify host [host:destination_file]: %s", xfer_cmd);

    // the string appears to contain a remote destination
//    strcpy(g_remote_args.pipe_host, "flynn@localhost");
    snprintf(g_remote_args.pipe_host, hostlen, "%s", xfer_cmd);
//    strcpy(g_remote_args.remote_path, "out2");
    snprintf(g_remote_args.remote_path, cmd_len-hostlen+1, "%s", xfer_cmd+hostlen);	
    verb(VERB_2, "[%s] exit ok", __func__);

    return RET_SUCCESS;
}


/* 
 * int get_remote_host
 * - given a list of arguments, check to see if user passed a remote specification
 * - returns: RET_SUCCESS on success, RET_FAILURE on failure
 */
int get_remote_host(int argc, char** argv)
{

    // Look for a specified remote destination
    for (int i = 0; i < argc; i++) {
        if (strchr(argv[i], ':')) {
            verb(VERB_2, "[%s] Found remote host [%s]", __func__, argv[i]);

            sprintf(g_remote_args.xfer_cmd, "%s", argv[i]);
            g_opts.remote = 1;

            // Set this argument to NULL because it is not a file to send
            argv[i] = NULL;

            // switch to MODE_RECV if it's not the last item on the list
            if (i < argc - 1) {
                NOTE(g_opts.remote_to_local = 1);
                g_opts.mode = MODE_RCV;
            }

            return RET_SUCCESS;

        }
    }

    return RET_FAILURE;
}


int get_base_path(int argc, char** argv, int optind)
{
    verb(VERB_2, "[%s] enter", __func__);
    int i;
    for ( i = 0; i < argc; i++ ) {
        verb(VERB_2, "[%s] %d - %s", __func__, i, argv[i]);
    }

    if (g_opts.mode & MODE_RCV) { 
        verb(VERB_2, "[%s] MODE_RCV detected", __func__);
        // Destination directory was passed
        if (optind < argc) {
            // Generate a base path for file locations
            verb(VERB_2, "[%s] argv[optind] = %s", __func__, argv[optind]);
            sprintf(g_base_path, "%s", argv[optind++]);

            // Are there any remaining command line args? Warn user
            verb(VERB_2, "[%s] Unused command line args:", __func__);
            for (; optind < argc-1; optind++) {
                verb(VERB_2, "Unused %s\n", argv[optind]);
            }
        }
        verb(VERB_2, "[%s] g_base_path = %s", __func__, g_base_path);
    } else {
        verb(VERB_2, "[%s] MODE_SND, g_base_path left empty", __func__);
    }

    return RET_SUCCESS;
}


/* 
 * int set_defaults
 * - sets the defaults for the two global option structs, 
 *    [parcel_opt_t g_opts] and [remote_arg_t g_remote_args]
 * - returns: RET_SUCCESS on success, RET_FAILURE on failure
 */
int set_defaults()
{
    memset(g_remote_args.pipe_cmd,    0, sizeof(char) * MAX_PATH_LEN);
    memset(g_remote_args.xfer_cmd,    0, sizeof(char) * MAX_PATH_LEN);
    memset(g_remote_args.pipe_host,   0, sizeof(char) * MAX_PATH_LEN);
    memset(g_remote_args.pipe_port,   0, sizeof(char) * MAX_PATH_LEN);
    memset(g_remote_args.remote_path, 0, sizeof(char) * MAX_PATH_LEN);

    memset(g_base_path, 0, sizeof(char) * MAX_PATH_LEN);

    sprintf(g_remote_args.udpipe_location, "parcel");
    sprintf(g_remote_args.pipe_port,       "9000");

    g_opts.mode                   = MODE_SEND;
    g_opts.verbosity              = VERB_1;
    g_opts.timeout                = 0;
    g_opts.recurse                = 1;
    g_opts.regular_files          = 1;
    g_opts.progress               = 1;
    g_opts.default_udpipe         = 0;
    g_opts.remote                 = 0;
    g_opts.delay                  = 0;
    g_opts.log                    = 0;
    g_opts.restart                = 0;
    g_opts.mmap                   = 0;
    g_opts.full_root              = 0;

    g_opts.remote_to_local        = 0;
    g_opts.ignore_modification    = 0;

    g_opts.socket_ready           = 0;
    g_opts.encryption             = 0;
    g_opts.n_crypto_threads       = 1;
    g_opts.enc = NULL;
    g_opts.dec = NULL;

    g_opts.send_pipe              = NULL;
    g_opts.recv_pipe              = NULL;
    g_remote_args.local_ip        = NULL;
    g_remote_args.remote_ip       = NULL;

    return RET_SUCCESS;
}


/* 
 * int get_options
 * - parse the command line arguments
 * - returns: optind, the index of the last used argument
 */
int get_options(int argc, char *argv[])
{

    int opt;
    
    // Read in options

    static struct option long_options[] =
    {
        {"verbosity"            , no_argument           , &g_opts.verbosity           , VERB_2},
        {"quiet"                , no_argument           , &g_opts.verbosity           , VERB_0},
        {"debug"                , no_argument           , NULL                      , 'b'},
        {"no-mmap"              , no_argument           , &g_opts.mmap                , 1},
        {"full-root"            , no_argument           , &g_opts.full_root           , 1},
        {"ignore-modification"  , no_argument           , &g_opts.ignore_modification , 1},
        {"all-files"            , no_argument           , &g_opts.regular_files       , 0},
        {"remote-to-local"      , no_argument           , &g_opts.remote_to_local     , 1},
        {"sender"               , no_argument           , NULL                      , 'q'},
        {"help"                 , no_argument           , NULL                      , 'h'},
        {"log"                  , required_argument     , NULL                      , 'l'},
        {"timeout"              , required_argument     , NULL                      , '5'},
        {"verbosity"            , required_argument     , NULL                      , '6'},
        {"interface"            , required_argument     , NULL                      , '7'},
        {"remote-interface"     , required_argument     , NULL                      , '8'},
        {"crypto-threads"       , required_argument     , NULL                      , '2'},
        {"restart"              , required_argument     , NULL                      , 'r'},
        {"checkpoint"           , required_argument     , NULL                      , 'k'},
        {0, 0, 0, 0}
    };

    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "i:xl:thvc:k:r:nd:5:p:q:b7:8:2:6:", 
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'k':
                // restart from and log to file argument
                g_opts.restart = 1;
                g_opts.log = 1;
                sprintf(log_path, "%s", optarg);
                sprintf(g_opts.restart_path, "%s", optarg);
                break;

            case 'n':
                g_opts.encryption = 1;
                break;

            case '2':
                ERR_IF(sscanf(optarg, "%d", &g_opts.n_crypto_threads) != 1, "unable to parse encryption threads from -n flag");
                fprintf(stderr, "n_crypto_threads: %d\n", g_opts.n_crypto_threads);
                break;

            case 'q':
                sprintf(g_remote_args.pipe_host, "%s", optarg);
                NOTE(g_opts.remote_to_local = 1);
                g_opts.mode |= MODE_SEND;
                break;

            case '5':
                ERR_IF(sscanf(optarg, "%d", &g_opts.timeout) != 1, "unable to parse timeout --timeout flag");
                break;

            case 'r':
                // restart but do not log to file
                g_opts.restart = 1;
                sprintf(g_opts.restart_path, "%s", optarg);
                break;

            case 'l':
                // log to file but do not restart from file
                g_opts.log = 1;
                sprintf(log_path, "%s", optarg);
                break;

            case 'c':
                // specify location of remote binary
                sprintf(g_remote_args.udpipe_location, "%s", optarg);
                break;

            case 'p':
                // specify port
                sprintf(g_remote_args.pipe_port, "%s", optarg);
                break;

            case 'x':
                // disable printing progress
                g_opts.progress = 0;
                break;

            case 't':
                // specify receive mode
                g_opts.mode |= MODE_RCV;
                g_opts.mode ^= MODE_SEND;
                break;

            case 'i':
                // specify the host, [i]p address
                sprintf(g_remote_args.pipe_host, "%s", optarg);
                break;

            case 'd':
                // in the case of slow ssh initiation, delay binding to
                // remote by optarg seconds
                g_opts.delay = atoi(optarg);
                break;

            case 'v':
                // default verbose
                g_opts.verbosity = 2;
                break;

            case '6':
                // verbosity
                ERR_IF(sscanf(optarg, "%d", &g_opts.verbosity) != 1, "unable to parse verbosity level");
                break;

            case '7':
                ERR_IF(!(g_remote_args.local_ip = strdup(optarg)), "unable to parse local ip");
                break;

            case '8':
                ERR_IF(!(g_remote_args.remote_ip = strdup(optarg)), "unable to parse remote ip");
                break;

            case 'h':
                // help
                usage(0);
                break;

            case '\0':
                break;

            case 'b':
                g_opt_debug_file_logging = 1;
                g_opts.verbosity = 2;
                break;

            default:
                fprintf(stderr, "Unknown command line option: [%c].\n", opt);
                usage(EXIT_FAILURE);
                break;
        }
    }

    g_opt_verbosity = g_opts.verbosity;
    if (g_opt_verbosity < VERB_1) {
        g_opts.progress = 0;
    }

    return optind;
}


/* 
 * int set_handlers
 * - sets the signal handler to sig_handler()
 * - returns: RET_SUCCESS on success, RET_FAILURE on failure
 */
int set_handlers()
{
    // Catch interupt signals

    if (signal(SIGINT, sig_handler) == SIG_ERR) {
        warn("[set_handlers] unable to set SIGINT handler");
        return RET_FAILURE;
    }

    if (signal(SIGSEGV, sig_handler) == SIG_ERR) {
        warn("[set_handlers] unable to set SIGSEGV handler\n");
        return RET_FAILURE;
    }

    return RET_SUCCESS;
}


int initialize_pipes()
{
    g_opts.send_pipe = (int*) malloc(2*sizeof(int));
    g_opts.recv_pipe = (int*) malloc(2*sizeof(int));

    ERR_IF(pipe(g_opts.send_pipe), "unable to create server's send pipe");
    ERR_IF(pipe(g_opts.recv_pipe), "unable to create server's receiver pipe");

    return RET_SUCCESS;
}


void cleanup_pipes()
{
    // fly - closing these means the reads will die in the threads properly, so we should be good
    close(g_opts.send_pipe[0]);
    close(g_opts.send_pipe[1]);
    close(g_opts.recv_pipe[0]);
    close(g_opts.recv_pipe[1]);

    if ( g_opts.send_pipe != NULL ) {
        free(g_opts.send_pipe);
        g_opts.send_pipe = NULL;
    }

    if ( g_opts.recv_pipe != NULL ) {
        free(g_opts.recv_pipe);
        g_opts.recv_pipe = NULL;
    }

}


pthread_t start_udpipe_thread(remote_arg_t *remote_args, udpipe_t udpipe_server_type)
{
    thread_args *args = (thread_args*) malloc(sizeof(thread_args));
    initialize_udpipe_args(args);

    char *host = (char*)malloc(1028*sizeof(char));
    char *at_ptr = NULL;
    if ((at_ptr = strrchr(remote_args->pipe_host, '@'))) {
        sprintf(host, "%s", at_ptr+1);
    } else {
        sprintf(host, "%s", remote_args->pipe_host);
    }

    args->ip               = host;
    args->n_crypto_threads = 1;
    args->port             = strdup(remote_args->pipe_port);
    args->recv_pipe        = g_opts.recv_pipe;
    args->send_pipe        = g_opts.send_pipe;
    args->timeout          = g_opts.timeout;
    args->verbose          = (g_opts.verbosity > VERB_1);
    args->listen_ip        = remote_args->local_ip;

    pthread_t udpipe_thread;
    if ( udpipe_server_type == UDPIPE_SERVER ) {
        pthread_create(&udpipe_thread, NULL, &run_server, args);
        RegisterThread(udpipe_thread, "run_server");
    } else {
        pthread_create(&udpipe_thread, NULL, &run_client, args);
        RegisterThread(udpipe_thread, "run_client");
    }

    return udpipe_thread;
}


int start_transfer(int argc, char*argv[], int optind)
{
    file_LL *fileList = NULL;

    // if logging is enabled, open the log/checkpoint file
    open_log_file();

    // if user selected to restart a previous transfer
    if (g_opts.restart) {
        verb(VERB_2, "[%s] Loading restart checkpoint [%s].", __func__, g_opts.restart_path);
        read_checkpoint(g_opts.restart_path);
    }

    if (g_opts.mode & MODE_RCV) {

        if ( g_opts.remote_to_local ) {
            verb(VERB_2, "[%s] Starting local_to_remote receiver\n", __func__);
            // spawn process on remote host and let it create the server
            verb(VERB_2, "[%s] Running ssh to remote path %s\n", __func__, g_remote_args.remote_path);
//            run_ssh_command(g_remote_args.remote_path);
            run_ssh_command();
        } else {
            verb(VERB_2, "[%s] Starting remote_to_local receiver\n", __func__);
        }

        verb(VERB_2, "[%s] Done running ssh\n", __func__);
        pid_t pid = getpid();
        write(g_opts.send_pipe[1], &pid, sizeof(pid_t));
        g_opts.socket_ready = 1;

        verb(VERB_2, "[%s] Running with file destination mode", __func__);
        start_udpipe_thread(&g_remote_args, UDPIPE_SERVER);

        if ( g_opts.remote_to_local ) {
            g_timer = start_timer("receive_timer");
        }

        // Listen to sender for files and data, see receiver.cpp
        receive_files(g_base_path);

        header_t h = nheader(XFER_COMPLETE, 0);
        write(g_opts.send_pipe[1], &h, sizeof(header_t));

    } else if (g_opts.mode & MODE_SEND) {

        if ( !g_opts.remote_to_local ) {
            verb(VERB_2, "[%s] Starting remote_to_local sender\n", __func__);
            run_ssh_command();
        } else {
            verb(VERB_2, "[%s] Starting local_to_remote sender\n", __func__);
        }

        verb(VERB_2, "[%s] Done running ssh\n", __func__);

        // delay proceeding for slow ssh connection
        if (g_opts.delay) {
            verb(VERB_1, "[%s] Delaying %ds for slow connection", __func__, g_opts.delay);
            sleep(g_opts.delay);
        }

        verb(VERB_2, "[%s] Starting udpipe thread\n", __func__);

        // connect to receiving server
        start_udpipe_thread(&g_remote_args, UDPIPE_CLIENT);

        verb(VERB_2, "[%s] Starting getting remote pid\n", __func__);
        // get the pid of the remote process in case we need to kill it
        get_remote_pid();

        ERR_IF(optind >= argc, "Please specify files to send");

        verb(VERB_2, "[%s] Running with file source mode", __func__);

        int n_files = argc-optind;
        char **path_list = argv+optind;

        verb(VERB_2, "[%s] building filelist of %d items from %s", __func__, n_files, path_list[0]);
        // Generate a linked list of file objects from path list
        ERR_IF(!(fileList = build_full_filelist(n_files, path_list)), "Filelist empty. Please specify files to send.\n");

        // send the file list, requesting version from dest
        file_LL* remote_fileList = send_and_wait_for_filelist(fileList);

        // Visit all directories and send all files
        // This is where we pass the remainder of the work to the
        // file handler in sender.cpp
        handle_files(fileList, remote_fileList);
        
        // signal the end of the transfer
        send_and_wait_for_ack_of_complete();

        // free the remote list
        free_file_list(remote_fileList);
        
    }

    return RET_SUCCESS;
}


void init_parcel(int argc, char *argv[])
{
    // set structs g_opts and g_remote_args to their default values
    set_defaults();

    // parse user command line input and get the remaining argument index
    int optind = get_options(argc, argv);
    verb(VERB_2, "[%s] optind = %d, argc = %d", __func__, optind, argc);
    
    if (g_opts.mode & MODE_RCV) { 
        verb(VERB_2, "[%s] set to MODE_RCV", __func__);
    }
    if (g_opts.mode & MODE_SEND) { 
        verb(VERB_2, "[%s] set to MODE_SEND", __func__);
    }

    if (g_opts.encryption){
        char* cipher = (char*) "aes-128";
        // fly - here is where we use the key instead of the password
        crypto enc(EVP_ENCRYPT, PASSPHRASE_SIZE, (unsigned char*)"password", cipher, g_opts.n_crypto_threads);
        crypto dec(EVP_DECRYPT, PASSPHRASE_SIZE, (unsigned char*)"password", cipher, g_opts.n_crypto_threads);
        g_opts.enc = &enc;
        g_opts.dec = &dec;
    }

    // specify how to catch signals
    set_handlers();

    get_remote_host(argc, argv);
    if ( g_opts.remote_to_local ) { 
        optind++;
    }
    get_base_path(argc, argv, optind);

    if ( g_opt_debug_file_logging ) {
        verb(VERB_2, "[%s] opening log file %s", __func__, g_logfilename);
        FILE* debug_file = fopen(g_logfilename, "a");
        if ( !debug_file ) {
            g_opt_debug_file_logging = 0;
            verb(VERB_2, "[%s] unable to open log file, error %d", __func__, g_logfilename, errno);
        }
        fclose(debug_file);
        verb(VERB_2, "********", __func__, g_logfilename);
        verb(VERB_2, "********", __func__, g_logfilename);
        verb(VERB_2, "[%s] log file opened as %s", __func__, g_logfilename);
    }


    verb(VERB_2, "[%s] parcel started as id %d", __func__, getpid());

    initialize_pipes();
    init_sender();
    init_receiver();
}


void cleanup_parcel()
{
    verb(VERB_2, "[%s] Cleaning up", __func__);

    cleanup_receiver();
    cleanup_sender();
    clean_exit(EXIT_SUCCESS);
}


int main(int argc, char *argv[])
{

    init_parcel(argc, argv);

    g_timer = start_timer("send_timer");
    start_transfer(argc, argv, optind);

    cleanup_parcel();

    return RET_SUCCESS;
}
