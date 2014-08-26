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

int opt_verbosity = 1;

// Statistics globals
int timer = 0;
off_t TOTAL_XFER = 0;

#include "util.h"

#include "udpipe.h"
#include "udpipe_threads.h"
#include "udpipe_server.h"
#include "udpipe_client.h"

remote_arg_t remote_args;
parcel_opt_t opts;

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
        "-v verbosity level \t\t set the level of verbosity",
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
    if (opt_verbosity >= verbosity){
        va_list args; va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        fprintf(stderr, "\n");
        va_end(args);
    }
}


/* 
 * void warn
 * - warn the user unless verbosity set to VERB_0 (0)
 */
void warn(char* fmt, ... )
{
    if (opt_verbosity > VERB_0){
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
    if (remote_args.ssh_pid && remote_args.remote_pid){

        if (opt_verbosity >= verbosity) 
            fprintf(stderr, "Killing child ssh process... ");

        if (kill(remote_args.ssh_pid, SIGINT)){
            if (opt_verbosity >= verbosity) 
                perror("FAILURE");
        } else {
            verb(verbosity, "success.");
        }

        int ssh_kill_pid;
        ssh_kill_pid = fork();

        // CHILD
        if (ssh_kill_pid == 0) {

            if (opt_verbosity >= verbosity) 
                fprintf(stderr, "Killing remote parcel process... ");

            char kill_cmd[MAX_PATH_LEN];
            sprintf(kill_cmd, "kill -s SIGINT %d 2> /dev/null", remote_args.remote_pid);
            char *args[] = {"ssh", "-A", remote_args.pipe_host, kill_cmd, NULL};

            // Execute the pipe process
            if (execvp(args[0], args)){
                fprintf(stderr, "WARNING: unable to kill remote process.");
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
    if (opts.verbosity >= VERB_2 || opts.progress){
        stop_timer(timer);
        double elapsed = timer_elapsed(timer);

        double scale = get_scale(TOTAL_XFER, label);

        fprintf(stderr, "\t\tSTAT: %.2f %s transfered in %.2fs [ %.2f Gbps ] \n", 
                TOTAL_XFER/scale, label, elapsed, 
                TOTAL_XFER/(elapsed*SIZE_GB));
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
    verb(VERB_2, "Clean exit");
    close_log_file();
    print_xfer_stats();
//    kill_children(VERB_2);
    SetExit();
    
    int counter = 0;
    verb(VERB_2, "\n");
    while ( GetThreadCount() > 0 ) {
        if ( counter == 0 ) {
            verb(VERB_2, "Waiting on %d threads to exit", GetThreadCount());
            PrintThreads();
            counter = MAX_OUTPUT_COUNT;
        } else {
            counter--;
        }
        usleep(100);
    }

    // fly - HAVE to wait for all the threads to be done to clean this up
    cleanup_pipes();
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
    if (total){
        double percent = total ? read*100./total : 0.0;
        sprintf(fmt, "\r +++ %%-%ds %%0.2f/%%0.2f %%s [ %%.2f %%%% ]", path_width);
        fprintf(stderr, fmt, descrip, read/scale, total/scale, label, percent);

    } else {
        sprintf(fmt, "\r +++ %%-%ds %%0.2f/? %%s [ ? %%%% ]", path_width);
        fprintf(stderr, fmt, descrip, read/scale,label);
    }

    return RET_SUCCESS;
}

/* 
 * int run_ssh_command
 * - run the ssh command that will create a remote parcel process
 * - returns: RET_SUCCESS on success, RET_FAILURE on failure
 */
int run_ssh_command(char *remote_path)
{

    parse_destination(remote_args.xfer_cmd);

    if (!remote_path){
        warn("remote destination was not set");
        return RET_FAILURE;
    }
    
    verb(VERB_2, "Attempting to run remote command to %s:%s", 
         remote_args.pipe_host, remote_args.pipe_port);

    // Create pipe and fork
    remote_args.ssh_pid = fork();

    // CHILD
    if (remote_args.ssh_pid == 0) {

        char remote_pipe_cmd[MAX_PATH_LEN];     
        bzero(remote_pipe_cmd, MAX_PATH_LEN);

	// Redirect output from ssh process to ssh_fd
	char *args[] = {
	    "ssh",
	    "-A", 
	    remote_args.pipe_host, 
	    remote_pipe_cmd, 
	    NULL
	};

        if (opts.mode == MODE_SEND){

	    ERR_IF(opts.remote_to_local, "Attempting to create ssh session for remote-to-local transfer in mode MODE_SEND\n");

            sprintf(remote_pipe_cmd, "%s -xt -p %s %s", 
                    remote_args.udpipe_location,
                    remote_args.pipe_port, 
                    remote_args.remote_path);


        } else if (opts.mode == MODE_RCV){

	    ERR_IF(!opts.remote_to_local, "Attempting to create ssh session for local-to-remote transfer in mode MODE_RCV\n");

            sprintf(remote_pipe_cmd, "%s -x -q %s -p %s %s", 
                    remote_args.udpipe_location,
                    remote_args.pipe_host,
                    remote_args.pipe_port, 
                    remote_args.remote_path);

        }

	verb(VERB_2, "ssh command: ");
	for (int i = 0; args[i]; i++)
	    verb(VERB_2, "args[%d]: %s", i, args[i]);

	ERR_IF(execvp(args[0], args), "unable to execute ssh process");
	ERR("premature ssh process exit");

    }

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
    if (read(opts.recv_pipe[0], &remote_args.remote_pid, sizeof(pid_t)) < 0){
        perror("WARNING: Unable to read pid from remote process");
    } 

    // Read something from the pipe, proceed
    else {
        verb(VERB_2, "Remote process pid: %d\n", remote_args.remote_pid);
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
    int hostlen = -1;
    int cmd_len = strlen(xfer_cmd);

    // we've already set the pipe_host, don't overwrite
    if (strlen(remote_args.pipe_host))
	return RET_FAILURE;

    // the string appears not to contain a remote destination
    hostlen = strchr(xfer_cmd, ':') - xfer_cmd + 1;
    ERR_IF(!hostlen, "Please specify host [host:destination_file]: %s", xfer_cmd);

    // the string appeasrs to contain a remote destination
    snprintf(remote_args.pipe_host, hostlen, "%s", xfer_cmd);
    snprintf(remote_args.remote_path, cmd_len-hostlen+1,"%s", xfer_cmd+hostlen);	

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
    for (int i = 0; i < argc; i++){

        if (strchr(argv[i], ':')){

            verb(VERB_2, "Found remote host [%s]", argv[i]);

            sprintf(remote_args.xfer_cmd, "%s", argv[i]);
            opts.remote = 1;

            // Set this argument to NULL because it is not a file to send
            argv[i] = NULL;

            if (i < argc - 1){
                NOTE(opts.remote_to_local = 1);
		opts.mode = MODE_RCV;
	    }

            return RET_SUCCESS;

        }
    }

    return RET_FAILURE;
}

/* 
 * int set_defaults
 * - sets the defaults for the two global option structs, 
 *    [parcel_opt_t opts] and [remote_arg_t remote_args]
 * - returns: RET_SUCCESS on success, RET_FAILURE on failure
 */
int set_defaults()
{
    bzero(remote_args.pipe_cmd,    MAX_PATH_LEN);
    bzero(remote_args.xfer_cmd,    MAX_PATH_LEN);
    bzero(remote_args.pipe_host,   MAX_PATH_LEN);
    bzero(remote_args.pipe_port,   MAX_PATH_LEN);
    bzero(remote_args.remote_path, MAX_PATH_LEN);
    
    sprintf(remote_args.udpipe_location, "parcel");
    sprintf(remote_args.pipe_port,       "9000");
    
    opts.mode                   = MODE_SEND;
    opts.verbosity              = VERB_1;
    opts.timeout                = 0;
    opts.recurse                = 1;
    opts.regular_files          = 1;
    opts.progress               = 1;
    opts.default_udpipe         = 0;
    opts.remote                 = 0;
    opts.delay                  = 0;
    opts.log                    = 0;
    opts.restart                = 0;
    opts.mmap                   = 0;
    opts.full_root              = 0;

    opts.remote_to_local        = 0;
    opts.ignore_modification    = 0;

    opts.socket_ready           = 0;
    opts.encryption             = 0;

    opts.send_pipe              = NULL;
    opts.recv_pipe              = NULL;

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
        {"verbosity"            , no_argument           , &opts.verbosity           , VERB_2},
        {"quiet"                , no_argument           , &opts.verbosity           , VERB_0},
        {"no-mmap"              , no_argument           , &opts.mmap                , 1},
        {"full-root"            , no_argument           , &opts.full_root           , 1},
        {"ignore-modification"  , no_argument           , &opts.ignore_modification , 1},
        {"all-files"            , no_argument           , &opts.regular_files       , 0},
        {"remote-to-local"      , no_argument           , &opts.remote_to_local     , 1},
        {"sender"               , no_argument           , NULL                      , 'q'},
        {"help"                 , no_argument           , NULL                      , 'h'},
        {"log"                  , required_argument     , NULL                      , 'l'},
        {"timeout"              , required_argument     , NULL                      , '5'},
        {"verbosity"            , required_argument     , NULL                      , '6'},
        {"restart"              , required_argument     , NULL                      , 'r'},
        {"checkpoint"           , required_argument     , NULL                      , 'k'},
        {0, 0, 0, 0}
    };

    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "n:i:xl:thv:c:k:r:n0d:5:p:q:", 
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'k':
                // restart from and log to file argument
                opts.restart = 1;
                opts.log = 1;
                sprintf(log_path, "%s", optarg);
                sprintf(opts.restart_path, "%s", optarg);
                break;

            case 'n':
                ERR_IF(sscanf(optarg, "%d", &opts.encryption) != 1, "unable to parse encryption threads from -n flag");
                break;

            case 'q':
                sprintf(remote_args.pipe_host, "%s", optarg);
                NOTE(opts.remote_to_local = 1);
                opts.mode |= MODE_SEND;
                break;

            case '5':
                ERR_IF(sscanf(optarg, "%d", &opts.timeout) != 1, "unable to parse timeout --timeout flag");
                break;

            case 'r':
                // restart but do not log to file
                opts.restart = 1;
                sprintf(opts.restart_path, "%s", optarg);
                break;

            case 'l':
                // log to file but do not restart from file
                opts.log = 1;
                sprintf(log_path, "%s", optarg);
                break;

            case 'c':
                // specify location of remote binary
                sprintf(remote_args.udpipe_location, "%s", optarg);
                break;

            case 'p':
                // specify port
                sprintf(remote_args.pipe_port, "%s", optarg);
                break;
                
            case 'x':
                // disable printing progress
                opts.progress = 0;
                break;

            case 't':
                // specify receive mode
                opts.mode |= MODE_RCV;
                opts.mode ^= MODE_SEND;
                break;

            case 'i':
                // specify the host, [i]p address
                sprintf(remote_args.pipe_host, "%s", optarg);
                break;

            case 'd':
                // in the case of slow ssh initiation, delay binding to
                // remote by optarg seconds
                opts.delay = atoi(optarg);
                break;

            case '0':
                
                break;

            case 'v':
                // default verbose
                opts.verbosity = 2;
                break;

            case '6':
                // verbosity
                ERR_IF(sscanf(optarg, "%d", &opts.verbosity) != 1, "unable to parse verbosity level");
                break;
                
            case 'h':
                // help
                usage(0);
                break;
                
            case '\0':
                break;
                
            default:
                fprintf(stderr, "Unknown command line option: [%c].\n", opt);
                usage(EXIT_FAILURE);
                break;
            
        }
    }

    opt_verbosity = opts.verbosity;
    if (opt_verbosity < VERB_1) {
        opts.progress = 0;
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
    opts.send_pipe = (int*) malloc(2*sizeof(int));
    opts.recv_pipe = (int*) malloc(2*sizeof(int));

    ERR_IF(pipe(opts.send_pipe), "unable to create server's send pipe");
    ERR_IF(pipe(opts.recv_pipe), "unable to create server's receiver pipe");

    return RET_SUCCESS;
}


void cleanup_pipes()
{
    if ( opts.send_pipe != NULL ) {
        free(opts.send_pipe);
        opts.send_pipe = NULL;
    }
    
    if ( opts.recv_pipe != NULL ) {
        free(opts.recv_pipe);
        opts.recv_pipe = NULL;
    }
    
}


pthread_t *start_udpipe_thread(remote_arg_t *remote_args, udpipe_t udpipe_server_type)
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
    args->recv_pipe        = opts.recv_pipe;
    args->send_pipe        = opts.send_pipe;
    args->timeout          = opts.timeout;
    args->verbose          = (opts.verbosity > VERB_1);

    pthread_t *udpipe_thread = (pthread_t*) malloc(sizeof(pthread_t));
    if ( udpipe_server_type == UDPIPE_SERVER ) {
        pthread_create(udpipe_thread, NULL, &run_server, args);
        RegisterThread(*udpipe_thread, "run_server");
    } else {
        pthread_create(udpipe_thread, NULL, &run_client, args);
        RegisterThread(*udpipe_thread, "run_client");
    }
    
    return udpipe_thread;    
}


int remote_to_local(int argc, char*argv[], int optind)
{
    file_LL *fileList = NULL;

    if (opts.mode & MODE_RCV) {

        char *base_path = NULL;

        verb(VERB_3, "Starting remote_to_local receiver\n");

        // spawn process on remote host and let it create the server
        run_ssh_command(remote_args.remote_path);

        pid_t pid = getpid();
        write(opts.send_pipe[1], &pid, sizeof(pid_t));
        opts.socket_ready = 1;

        verb(VERB_3, "Running with file destination mode");
//        pthread_t *server_thread = start_udpipe_server(&remote_args);
        pthread_t *server_thread = start_udpipe_thread(&remote_args, UDPIPE_SERVER);

        // Find the output path
        ERR_IF(optind >= argc, "I don't know where to put the files");
        for (; optind < argc; optind++) {
            // Go through the remaining arguments looking for an output directory path
            if (argv[optind]) {
                base_path = strdup(argv[optind]);
                break;
            }
        }
	
        ERR_IF(!base_path, "I thought I knew where to put the files, but I really didn't.");
        verb(VERB_3, "Writing files to: %s", base_path);

        // Listen to sender for files and data, see receiver.cpp
        receive_files(base_path);


	// Wait here for completion
        header_t h = nheader(XFER_COMPLETE, 0);
        write(opts.send_pipe[1], &h, sizeof(header_t));

	// This causes hanging, commented for now?
        verb(VERB_3, "Killing server thread");
        pthread_kill(*server_thread, 0);


    } else if (opts.mode & MODE_SEND) {

        verb(VERB_3, "FLAG: Starting remote_to_local sender\n");

        // delay proceeding for slow ssh connection
        if (opts.delay) {
            verb(VERB_1, "Delaying %ds for slow connection\n", opts.delay);
            sleep(opts.delay);
        }

        // connect to receiving server
//        start_udpipe_client(&remote_args);
        pthread_t *server_thread = start_udpipe_thread(&remote_args, UDPIPE_CLIENT);


        // get the pid of the remote process in case we need to kill it
        get_remote_pid();


        ERR_IF(optind >= argc, "Please specify files to send");

        verb(VERB_3, "Running with file source mode.\n");
                
        int n_files = argc-optind;
        char **path_list = argv+optind;

        // Generate a linked list of file objects from path list
//        ERR_IF(!(fileList = build_filelist(n_files, path_list)), "Filelist empty. Please specify files to send.\n");
        ERR_IF(!(fileList = build_full_filelist(n_files, path_list)), "Filelist empty. Please specify files to send.\n");

        // send the file list, requesting version from dest
        file_LL* remote_fileList = send_and_wait_for_filelist(fileList);
        
        // This is where we pass the remainder of the work to the
        // file handler in sender.cpp
        handle_files(fileList, remote_fileList);
        
        // we're done, free the remote file list
        free_file_list(remote_fileList);

        // signal the end of the transfer
        complete_xfer();
        
        header_t h = nheader(XFER_WAIT, 0);
        while (read(opts.recv_pipe[0], &h, sizeof(header_t))) {
            if (h.type == XFER_COMPLETE) {
                break;
            }
            else {
                fprintf(stderr, "received non-end-of-transfer message\n");
            }
        }
        verb(VERB_3, "Received acknowledgement of completion");

        free(server_thread);
    }

    // free up the thread handle
    return RET_SUCCESS;
}


int local_to_remote(int argc, char*argv[], int optind)
{
    file_LL *fileList = NULL;

    // if logging is enabled, open the log/checkpoint file
    open_log_file();

    // if user selected to restart a previous transfer
    if (opts.restart) {
        verb(VERB_3, "Loading restart checkpoint [%s].", opts.restart_path);
        read_checkpoint(opts.restart_path);
    }


    if (opts.mode & MODE_SEND) {

        verb(VERB_3, "Starting local_to_remote sender\n");
        
        // spawn process on remote host and let it create the server
        run_ssh_command(remote_args.remote_path);

        // delay proceeding for slow ssh connection
        if (opts.delay) {
            verb(VERB_1, "Delaying %ds for slow connection\n", opts.delay);
            sleep(opts.delay);
        }

        // connect to receiving server
//        start_udpipe_client(&remote_args);
        start_udpipe_thread(&remote_args, UDPIPE_CLIENT);
        
        // get the pid of the remote process in case we need to kill it
        get_remote_pid();

        ERR_IF(optind >= argc, "Please specify files to send");
                
        verb(VERB_3, "Running with file source mode.\n");
                
        int n_files = argc-optind;
        char **path_list = argv+optind;

        // Generate a linked list of file objects from path list
//        ERR_IF(!(fileList = build_filelist(n_files, path_list)), "Filelist empty. Please specify files to send.\n");
        ERR_IF(!(fileList = build_full_filelist(n_files, path_list)), "Filelist empty. Please specify files to send.\n");

        file_LL* remote_fileList = send_and_wait_for_filelist(fileList);
        
        // Visit all directories and send all files
        // This is where we pass the remainder of the work to the
        // file handler in sender.cpp
        handle_files(fileList, remote_fileList);
        // signal the end of the transfer
        complete_xfer();

        // free the remote list
        free_file_list(remote_fileList);

    // Otherwise, switch to receiving mode
    } else if (opts.mode & MODE_RCV) {

        verb(VERB_3, "Starting local_to_remote receiver\n");
        
        pid_t pid = getpid();
        write(opts.send_pipe[1], &pid, sizeof(pid_t));

        opts.socket_ready = 1;

        verb(VERB_3, "Running with file destination mode\n");

//        pthread_t *server_thread = start_udpipe_server(&remote_args);
        pthread_t *server_thread = start_udpipe_thread(&remote_args, UDPIPE_SERVER);

        // Check to see if user specified a pipe, if so, run it
        // run_pipe(remote_args.pipe_cmd);

        // Destination directory was passed
        if (optind < argc) {
            
            // Generate a base path for file locations
            char base_path[MAX_PATH_LEN];
            bzero(base_path, MAX_PATH_LEN);
            sprintf(base_path, "%s", argv[optind++]);
            
            // Are there any remaining command line args? Warn user
            verb(VERB_2, "Unused command line args:");
            for (; optind < argc-1; optind++) {
                verb(VERB_2, "Unused %s\n", argv[optind]);
            }
            
            timer = start_timer("receive_timer");

            // Listen to sender for files and data, see receiver.cpp
            receive_files(base_path);
            
        // If no destination directory was passed, default to current dir
        } else {
            timer = start_timer("receive_timer");
            receive_files("");
        }

        header_t h = nheader(XFER_COMPLETE, 0);
        write(opts.send_pipe[1], &h, sizeof(header_t));

        pthread_join(*server_thread, NULL);
        free(server_thread);
        
    }

    verb(VERB_3, "Waiting for acknowledgement of complete.\n");
    header_t h = nheader(XFER_WAIT, 0);
    while (read(opts.recv_pipe[0], &h, sizeof(header_t))) {
        if (h.type == XFER_COMPLETE) {
            break;
        } else {
            fprintf(stderr, "received non-end-of-transfer message\n");
        }
    }
    verb(VERB_3, "Received acknowledgement of completion");

    return RET_SUCCESS;
}

void init_parcel(int argc, char *argv[])
{
    // specify how to catch signals
    set_handlers();
    // set structs opts and remote_args to their default values
    set_defaults();
    // parse user command line input and get the remaining argument index
    optind = get_options(argc, argv);

    get_remote_host(argc, argv);
    
    initialize_pipes();
    init_sender();
    init_receiver();
    
}

void cleanup_parcel()
{
    
    verb(VERB_2, "Cleaning up parcel");
    
//    kill_children(VERB_2);
    
    cleanup_receiver();    
    cleanup_sender();
    clean_exit(EXIT_SUCCESS);
    
}


int main(int argc, char *argv[])
{

    init_parcel(argc, argv);
    
    timer = start_timer("send_timer");
    
    if (opts.remote_to_local) {
        remote_to_local(argc, argv, optind);
    } else {
        local_to_remote(argc, argv, optind);
    }

//    print_xfer_stats();    
    
    cleanup_parcel();
    
    return RET_SUCCESS;
}
