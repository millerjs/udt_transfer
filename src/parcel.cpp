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
#include "debug_output.h"

#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stddef.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>

#define PARCEL_FLAG_MASTER      0x01
#define PARCEL_FLAG_MINION      0x02

#define PARCEL_MAX_TEMP_KEY_LENGTH	4096
#define PARCEL_CRYPTO_KEY_LENGTH	16

//#define DONT_CHECK_FILELIST         0

char g_base_path[MAX_PATH_LEN];

char* g_session_key = (char*)NULL;

char g_flags = 0;

FILE* g_ssh_file_handle = NULL;

// Statistics globals
int g_timer = 0;
off_t G_TOTAL_XFER = 0;

#include "util.h"

#include "udpipe.h"
#include "udpipe_threads.h"
#include "udpipe_server.h"
#include "udpipe_client.h"

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
 * header_t nheader
 * - creates and initializes a new header with type [type] and length [size]
 * - returns: new header
 */
header_t* nheader(xfer_t type, off_t size)
{
	header_t* header;
	header = (header_t*)malloc(sizeof(header_t));
	memset(header, 0, sizeof(header_t));
	header->data_len = size;
	header->mtime_sec = 88;
	header->mtime_nsec = 88;
	header->type = type;
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
//		"-v verbosity level \t\t set the level of verbosity",
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
 * int kill_children
 * - Make sure that we have killed any zombie or orphaned children
 * - returns: returns 0 on success, does not return on failure
 */
int kill_children()
{

	// Clean up the ssh process
	if (g_remote_args.ssh_pid && g_remote_args.remote_pid){

		verb(VERB_2, "[%d %s] Killing child ssh process... ", g_flags, __func__);

		if (kill(g_remote_args.ssh_pid, SIGINT)){
			ERR("FAILURE");
		} else {
			verb(VERB_2, "success.");
		}

		int ssh_kill_pid;
		ssh_kill_pid = fork();

		// CHILD
		if (ssh_kill_pid == 0) {

			verb(VERB_2, "[%d %s] Killing remote parcel process... ", g_flags, __func__);

			char kill_cmd[MAX_PATH_LEN];
			sprintf(kill_cmd, "kill -s SIGINT %d 2> /dev/null", g_remote_args.remote_pid);
			char *args[] = {"ssh", "-A", g_remote_args.pipe_host, kill_cmd, NULL};

			// Execute the pipe process
			if (execvp(args[0], args)){
				verb(VERB_2, "[%d %s] WARNING: unable to kill remote process.", g_flags, __func__);
				exit(EXIT_FAILURE);
			}
		// PARENT
		} else {
			int stat;
			waitpid(ssh_kill_pid, &stat, 0);
			if (!stat){
				verb(VERB_2, "success.");
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
//	fprintf(stderr, "get_scale: size = %ld, label = %s, newSize = %f\n", size, label, tmpSize);
//	label = "[?]";
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

		fprintf(stderr, "\n\tSTAT: %.2f %s transfered in %.2fs [ %.2f Gbps ] \n",
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
	verb(VERB_2, "[%d %s] Start", g_flags, __func__);
	close_log_file();
	print_xfer_stats();
	verb(VERB_2, "[%s] cleaning up pipes", __func__);
	cleanup_pipes();
	set_thread_exit();

	int counter = 0;
	verb(VERB_2, "\n");
	while ( (get_thread_count(THREAD_TYPE_ALL) > 0) && (status != EXIT_FAILURE) ) {
		if ( counter == 0 ) {
			verb(VERB_2, "[%d %s] Waiting on %d threads to exit", g_flags, __func__, get_thread_count(THREAD_TYPE_ALL));
			print_threads();
			counter = MAX_OUTPUT_COUNT;
		} else {
			counter--;
		}
		usleep(100);
	}

	verb(VERB_2, "[%s] cleaning up sender/receiver", __func__);
	cleanup_receiver();
	cleanup_sender();

	verb(VERB_2, "[%s] deleting crypto structs", __func__);
	delete(g_opts.enc);
	delete(g_opts.dec);

	if ( g_ssh_file_handle ) {
		verb(VERB_2, "[%s] pclosing file handle", __func__);
		pclose(g_ssh_file_handle);
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
        print_backtrace();
#endif
        abort();
    }

    // Kill children and let user know
    kill_children();
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
        fprintf(stderr, fmt, descrip, read/scale, total/scale, label, percent);

    } else {
        sprintf(fmt, "\r +++ %%-%ds %%0.2f/? %%s [ ? %%%% ]", path_width);
        verb(VERB_2, fmt, descrip, read/scale, label);
        fprintf(stderr, fmt, descrip, read/scale, label);
    }

    return RET_SUCCESS;
}


char* get_local_ip_address()
{
	int fd;
	struct ifreq ifr;

	fd = socket(AF_INET, SOCK_DGRAM, 0);

	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
	ioctl(fd, SIOCGIFADDR, &ifr);
	close(fd);

	return inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
}

/*
 * int run_ssh_command
 * - run the ssh command that will create a remote parcel process
 * - returns: RET_SUCCESS on success, RET_FAILURE on failure
 */
int run_ssh_command()
{
	parse_destination(g_remote_args.xfer_cmd);

	if ( !strlen(g_remote_args.remote_path) ) {
		warn("remote destination was not set");
		return RET_FAILURE;
	}

	verb(VERB_2, "[%d %s %d] Attempting to run remote command to %s:%s", g_flags, __func__, getpid(),
		 g_remote_args.pipe_host, g_remote_args.pipe_port);


	char remote_pipe_cmd[MAX_PATH_LEN];
	char cmd_options[MAX_PATH_LEN];

	memset(remote_pipe_cmd, 0, sizeof(char) * MAX_PATH_LEN);
	memset(cmd_options, 0, sizeof(char) * MAX_PATH_LEN);

	// Redirect output from ssh process to ssh_fd
	char *args[] = {
		"ssh",
		"-A -q",
		g_remote_args.pipe_host,
		remote_pipe_cmd,
		NULL
	};

	sprintf(remote_pipe_cmd, "%s ", g_remote_args.udpipe_location);

	if (g_opts.encryption) {
		strcat(remote_pipe_cmd, " -n ");
		char n_crypto_threads[MAX_PATH_LEN];
		sprintf(n_crypto_threads, "--crypto-threads %d ", g_opts.n_crypto_threads);
		strcat(remote_pipe_cmd, n_crypto_threads);
	}

	if ( get_file_logging() ) {
		strcat(remote_pipe_cmd, " -b ");
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

		sprintf(cmd_options, "-x -q %s@%s -p %s %s ",
//                g_remote_args.pipe_host,
				getlogin(), get_local_ip_address(),
				g_remote_args.pipe_port,
				g_remote_args.remote_path);
	}

	strcat(remote_pipe_cmd, cmd_options);

	verb(VERB_2, "[%d %s] ssh command: ", g_flags, __func__);
	for (int i = 0; args[i]; i++) {
		verb(VERB_2, "args[%d]: %s", i, args[i]);
	}

	char temp_command[MAX_PATH_LEN];
	memset(temp_command, 0, sizeof(char) * MAX_PATH_LEN);
	snprintf(temp_command, MAX_PATH_LEN, "%s %s %s %s", args[0], args[1], args[2], args[3]);
	verb(VERB_2, "[%d %s] Calling popen with %s", g_flags, __func__, temp_command);
	g_ssh_file_handle = popen(temp_command, "r");
	if ( g_ssh_file_handle == NULL ) {
		ERR("unable to execute ssh process");
	}

	verb(VERB_2, "[%d %s] exit", g_flags, __func__);
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
/*	if (pipe_read(g_opts.recv_pipe[0], &g_remote_args.remote_pid, sizeof(pid_t)) < 0) {
		perror("WARNING: Unable to read pid from remote process");
	}

	// Read something from the pipe, proceed
	else {
		verb(VERB_2, "[%d %s] Remote process pid: %d", g_flags, __func__, g_remote_args.remote_pid);
	} */
	return 0;
}


int get_shared_key()
{
//	char temp_key_buffer[4096];
//	if (pipe_read(g_opts.recv_pipe[0], &temp_key_buffer, sizeof(pid_t)) < 0) {
//	}

	return 0;
}


/*
 * void initialize_udpipe_args
 * -
 * - returns: nothing
 */
void initialize_udpipe_args(thread_args *args)
{
	args->ip				= NULL;
	args->listen_ip			= NULL;
	args->port				= NULL;

	args->enc				= NULL;
	args->dec				= NULL;

	args->udt_buff			= BUFF_SIZE;
	args->udp_buff			= BUFF_SIZE;

	args->blast				= 0;
	args->blast_rate		= 1000;
	args->mss				= 8400;
	args->n_crypto_threads	= 1;
	args->print_speed		= 0;
	args->timeout			= 0;
	args->use_crypto		= 0;
	args->verbose			= 0;
	args->master			= 0;
}


/*
 * int parse_destination
 * - parse argument xfer_cmd for host:destination
 * - returns: RET_SUCCESS on success, RET_FAILURE on failure
 */
int parse_destination(char *xfer_cmd)
{
	verb(VERB_2, "[%d %s] xfer_cmd = %s, len %d", g_flags, __func__, xfer_cmd, strlen(xfer_cmd));
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
	snprintf(g_remote_args.pipe_host, hostlen, "%s", xfer_cmd);
	snprintf(g_remote_args.remote_path, cmd_len-hostlen+1, "%s", xfer_cmd+hostlen);
	verb(VERB_2, "[%d %s] exit ok", g_flags, __func__);

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
			verb(VERB_2, "[%d %s] Found remote host [%s]", g_flags, __func__, argv[i]);

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
//	verb(VERB_2, "[%d %s] enter", g_flags, __func__);
//	int i;
//	for ( i = 0; i < argc; i++ ) {
//		verb(VERB_2, "[%d %s] %d - %s", g_flags, __func__, i, argv[i]);
//	}

	if (g_opts.mode & MODE_RCV) {
//		verb(VERB_2, "[%d %s] MODE_RCV detected (%0x)", g_flags, __func__, g_opts.mode);
		// Destination directory was passed
		if (optind < argc) {
			// Generate a base path for file locations
//			verb(VERB_2, "[%d %s] argv[optind] = %s", g_flags, __func__, argv[optind]);
			sprintf(g_base_path, "%s", argv[optind++]);

			// Are there any remaining command line args? Warn user
//			verb(VERB_2, "[%d %s] Unused command line args:", g_flags, __func__);
			for (; optind < argc-1; optind++) {
				verb(VERB_2, "Unused %s", argv[optind]);
			}
		}
//		verb(VERB_2, "[%d %s] g_base_path = %s", g_flags, __func__, g_base_path);
//	} else {
//		verb(VERB_2, "[%d %s] MODE_SND, g_base_path left empty", g_flags, __func__);
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

	set_socket_ready(0);

	return RET_SUCCESS;
}


/*
 * int get_options
 * - parse the command line arguments
 * - returns: optind, the index of the last used argument
 */
int get_options(int argc, char *argv[])
{

	if ( argc > 1 ) {
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

	/*    for ( int i = 0; i < argc; i++ ) {
			fprintf(stderr, "argv[%d] = %s\n", i, argv[i]);
		} */

		while ((opt = getopt_long(argc, argv, "i:xl:thvc:k:r:nd:5:p:q:b7:8:2:6:",
								  long_options, &option_index)) != -1) {
	//		fprintf(stderr, "opt = %c\n", opt);
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
	//				fprintf(stderr, "n_crypto_threads: %d\n", g_opts.n_crypto_threads);
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
					set_verbosity_level(VERB_2);
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
					set_file_logging(1);
	//				g_opts.verbosity = 2;
					break;

				default:
					fprintf(stderr, "Unknown command line option: [%c].\n", opt);
					usage(EXIT_FAILURE);
					break;
			}
		}

	//	g_opt_verbosity = g_opts.verbosity;
		if (get_verbosity_level() < VERB_1) {
			g_opts.progress = 0;
		}
	} else {
		usage(0);
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

/*
 * int initialize_pipes
 * - initializes the file descriptor pipes to be used for UDT
 * - returns: RET_SUCCESS
 */
int initialize_pipes()
{
	g_opts.send_pipe = (int*) malloc(2*sizeof(int));
	g_opts.recv_pipe = (int*) malloc(2*sizeof(int));

	ERR_IF(pipe(g_opts.send_pipe), "unable to create server's send pipe");
	ERR_IF(pipe(g_opts.recv_pipe), "unable to create server's receiver pipe");

	struct stat tmp_stat;
	fstat(g_opts.send_pipe[0], &tmp_stat);
	verb(VERB_2, "[%s] %lu bytes in send_pipe[0]", __func__, tmp_stat.st_size);

	fstat(g_opts.send_pipe[1], &tmp_stat);
	verb(VERB_2, "[%s] %lu bytes in send_pipe[1]", __func__, tmp_stat.st_size);

	fstat(g_opts.recv_pipe[0], &tmp_stat);
	verb(VERB_2, "[%s] %lu bytes in recv_pipe[0]", __func__, tmp_stat.st_size);

	fstat(g_opts.recv_pipe[1], &tmp_stat);
	verb(VERB_2, "[%s] %lu bytes in recv_pipe[1]", __func__, tmp_stat.st_size);

	verb(VERB_2, "[%d %s] send_pipe[0] = %d, send_pipe[1] = %d", g_flags, __func__, g_opts.send_pipe[0], g_opts.send_pipe[1]);
	verb(VERB_2, "[%d %s] recv_pipe[0] = %d, recv_pipe[1] = %d", g_flags, __func__, g_opts.recv_pipe[0], g_opts.recv_pipe[1]);

	return RET_SUCCESS;
}

/*
 * void cleanup_pipes
 * - closes & frees the pipes for tidy exit
 * - returns: nothing
 */
void cleanup_pipes()
{
	// fly - closing these means the reads will die in the threads properly, so we should be good
//	if ( g_flags & PARCEL_FLAG_MASTER ) {
		verb(VERB_2, "[%d %s] we're master, cleaning up pipes", g_flags, __func__);
		if ( g_opts.send_pipe != NULL ) {
			close(g_opts.send_pipe[0]);
			close(g_opts.send_pipe[1]);
		}
		if ( g_opts.recv_pipe != NULL ) {
			close(g_opts.recv_pipe[0]);
			close(g_opts.recv_pipe[1]);
		}
//	} else {
//		verb(VERB_2, "[%d %s] we're minion, let the other side do that", g_flags, __func__);
//	}

	if ( g_opts.send_pipe != NULL ) {
		free(g_opts.send_pipe);
		g_opts.send_pipe = NULL;
	}

	if ( g_opts.recv_pipe != NULL ) {
		free(g_opts.recv_pipe);
		g_opts.recv_pipe = NULL;
	}

}


/*
 * pthread_t start_udpipe_thread
 * - starts the correct type of udpipe thread
 * - returns: pthread_t - pointer to thread started
 */
pthread_t start_udpipe_thread(remote_arg_t *remote_args, udpipe_t udpipe_server_type)
{
	thread_args *args = (thread_args*) malloc(sizeof(thread_args));
	initialize_udpipe_args(args);
	verb(VERB_3, "[%d %s] args addy = %0x", g_flags, __func__, args);

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

	args->use_crypto       = g_opts.encryption;
	args->n_crypto_threads = g_opts.n_crypto_threads;
	args->enc              = g_opts.enc;
	args->dec              = g_opts.dec;

	if ( g_flags & PARCEL_FLAG_MASTER ) {
		args->master	= 1;
	}
	verb(VERB_2, "[%d %s] g_opts->enc = %0x", g_flags, __func__, g_opts.enc);
	verb(VERB_2, "[%d %s] g_opts->dec = %0x", g_flags, __func__, g_opts.dec);

	pthread_t udpipe_thread;
	if ( udpipe_server_type == UDPIPE_SERVER ) {
		create_thread(&udpipe_thread, NULL, &run_server, args, "crypto_update_thread", THREAD_TYPE_1);
    } else {
		create_thread(&udpipe_thread, NULL, &run_client, args, "crypto_update_thread", THREAD_TYPE_1);
    }

    return udpipe_thread;
}

/*
 * int master_transfer_setup
 * - sets up shop for the master's transfer
 * - returns: RET_SUCCESS
 */
int master_transfer_setup()
{
	char    tmpBuf[PARCEL_MAX_TEMP_KEY_LENGTH];

	// spawn process on remote host and let it create the server
	verb(VERB_2, "[%d %s] Running ssh to remote path %s", g_flags, __func__, g_remote_args.remote_path);

	run_ssh_command();

	verb(VERB_2, "[%d %s] Done running ssh", g_flags, __func__);

	// fly - ok, we have to get the key now that the ssh has started
	// read the key in from the handle
	int key_len = 0;
	memset(tmpBuf, 0, sizeof(char) * PARCEL_MAX_TEMP_KEY_LENGTH);
	if ( g_ssh_file_handle && g_opts.encryption ) {
		verb(VERB_2, "[%d %s] Looking for key...", g_flags, __func__);
//		fread(&tmpBuf[key_len], 1, 1, g_ssh_file_handle);
		while ( key_len < PARCEL_CRYPTO_KEY_LENGTH ) {
//		while ( (tmpBuf[key_len] != '\0') && (key_len < PARCEL_MAX_TEMP_KEY_LENGTH) ) {
			fread(&tmpBuf[key_len], 1, 1, g_ssh_file_handle);
//			verb(VERB_3, "[%d %s] %d: Read %c (%02X)", g_flags, __func__, key_len, tmpBuf[key_len], tmpBuf[key_len]);
			key_len++;
		}
		if ( key_len >= PARCEL_MAX_TEMP_KEY_LENGTH ) {
			ERR("[%d %s] received key of improper length", g_flags, __func__);
		}
		verb(VERB_3, "[%d %s] Got key back of len %d:", g_flags, __func__, key_len);
//		print_bytes(tmpBuf, PARCEL_CRYPTO_KEY_LENGTH, 16);
//		verb(VERB_3, "%s", tmpBuf);

		// copy it to our key
		g_session_key = (char*)malloc(sizeof(char) * key_len);
		memset(g_session_key, 0, sizeof(char) * key_len);
		strncpy(g_session_key, tmpBuf, sizeof(char) * key_len);
//		verb(VERB_3, "[%d %s] g_session_key:", g_flags, __func__);
//		verb(VERB_3, "%s", g_session_key);
	}

	if (g_opts.encryption) {
		char* cipher = (char*) "aes-128";
		// fly - here is where we use the key instead of the password
		// if we don't have a key, use a password (for now, we'll have to bail if no key)
		if ( !g_session_key ) {
			verb(VERB_2, "[%d %s] No session key found, populating with default", g_flags, __func__);
			g_session_key = (char*)malloc(sizeof(char) * strlen(tmpBuf) + 1);
			memset(g_session_key, 0, sizeof(char) * strlen(tmpBuf) + 1);
			strncpy(g_session_key, "password", sizeof(char) * strlen("password") + 1);
			verb(VERB_3, "[%d %s] g_session_key:", g_flags, __func__);
			verb(VERB_3, "%s", g_session_key);
			key_len = strlen("password");
		}
		g_opts.enc = new Crypto(EVP_ENCRYPT, key_len, (unsigned char*)g_session_key, cipher, g_opts.n_crypto_threads);
		g_opts.dec = new Crypto(EVP_DECRYPT, key_len, (unsigned char*)g_session_key, cipher, g_opts.n_crypto_threads);
//		verb(VERB_3, "[%d %s] enc thread_id = %d", g_flags, __func__, enc.get_thread_id());
//		verb(VERB_3, "[%d %s] dec thread_id = %d", g_flags, __func__, enc.get_thread_id());
	}

	return RET_SUCCESS;
}

/*
 * int minion_transfer_setup
 * - sets up shop for the minion's transfer
 * - not a lot here now, but centralized in case
 * - returns: RET_SUCCESS
 */
int minion_transfer_setup()
{
	char    tmpBuf[PARCEL_MAX_TEMP_KEY_LENGTH];

	if (g_opts.encryption) {
		int key_len = PARCEL_CRYPTO_KEY_LENGTH;
		char* cipher = (char*) "aes-128";
		// fly - here is where we use the key instead of the password
		if ( !g_session_key ) {
			verb(VERB_2, "[%d %s] No session key found, populating with default", g_flags, __func__);
			g_session_key = (char*)malloc(sizeof(char) * strlen(tmpBuf) + 1);
			memset(g_session_key, 0, sizeof(char) * strlen(tmpBuf) + 1);
			strncpy(g_session_key, "password", sizeof(char) * strlen("password") + 1);
			verb(VERB_3, "[%d %s] g_session_key:", g_flags, __func__);
			verb(VERB_3, "%s", g_session_key);
			key_len = strlen("password");
		}
		g_opts.enc = new Crypto(EVP_ENCRYPT, key_len, (unsigned char*)g_session_key, cipher, g_opts.n_crypto_threads);
		g_opts.dec = new Crypto(EVP_DECRYPT, key_len, (unsigned char*)g_session_key, cipher, g_opts.n_crypto_threads);
	}
	return RET_SUCCESS;
}


/*
 * int start_transfer
 * - starts a transfer for either side
 * - returns: RET_SUCCESS
 */
int start_transfer(int argc, char*argv[], int optind)
{
	file_LL *fileList = NULL;

	// if logging is enabled, open the log/checkpoint file
	open_log_file();

	// if user selected to restart a previous transfer
	if (g_opts.restart) {
		verb(VERB_2, "[%d %s] Loading restart checkpoint [%d %s].", g_flags, __func__, g_opts.restart_path);
		read_checkpoint(g_opts.restart_path);
	}

	if ( g_opts.mode & MODE_RCV ){
		if ( g_opts.remote_to_local ) {
		   verb(VERB_2, "[%d %s] Starting remote_to_local receiver", g_flags, __func__);
		} else {
		   verb(VERB_2, "[%d %s] Starting local_to_remote receiver", g_flags, __func__);
		}
	} else {
		if ( !g_opts.remote_to_local ) {
		   verb(VERB_2, "[%d %s] Starting remote_to_local sender", g_flags, __func__);
		} else {
		   verb(VERB_2, "[%d %s] Starting local_to_remote sender", g_flags, __func__);
		}
	}

	if ( g_flags & PARCEL_FLAG_MASTER ) {
		master_transfer_setup();
	} else {
		minion_transfer_setup();
	}
	verb(VERB_2, "[%d %s] g_opts->enc = %0x", g_flags, __func__, g_opts.enc);
	verb(VERB_2, "[%d %s] g_opts->dec = %0x", g_flags, __func__, g_opts.dec);

	if (g_opts.mode & MODE_RCV) {

		// sending pid if other side needs to kill?
/*        pid_t pid = getpid();
        pipe_write(g_opts.send_pipe[1], &pid, sizeof(pid_t)); */

		verb(VERB_2, "[%d %s] Running with file destination mode",g_flags, __func__);
		start_udpipe_thread(&g_remote_args, UDPIPE_SERVER);

		if ( g_opts.remote_to_local ) {
			g_timer = start_timer("receive_timer");
		}

//        verb(VERB_3, "[%d %s RECV] enc thread_id = %d", g_flags, __func__, g_opts.enc->get_thread_id());
//        verb(VERB_3, "[%d %s RECV] dec thread_id = %d", g_flags, __func__, g_opts.enc->get_thread_id());

//		g_opts.socket_ready = 1;
		while ( !get_encrypt_ready() );

		// Listen to sender for files and data, see receiver.cpp
		receive_files(g_base_path);

//		header_t* h = nheader(XFER_COMPLETE, 0);
//		pipe_write(g_opts.send_pipe[1], h, sizeof(header_t));
//		free(h);

	} else if (g_opts.mode & MODE_SEND) {

		// delay proceeding for slow ssh connection
		if (g_opts.delay) {
			verb(VERB_1, "[%d %s] Delaying %ds for slow connection", g_flags, __func__, g_opts.delay);
			sleep(g_opts.delay);
		}

		verb(VERB_2, "[%d %s] Running with file source mode", g_flags, __func__);
		// connect to receiving server
		start_udpipe_thread(&g_remote_args, UDPIPE_CLIENT);

		// get the pid of the remote process in case we need to kill it
//		get_remote_pid();

		ERR_IF(optind >= argc, "Please specify files to send");


		int n_files = argc-optind;
		char **path_list = argv+optind;

		verb(VERB_2, "[%d %s] building filelist of %d items from %s", g_flags, __func__, n_files, path_list[0]);
		// Generate a linked list of file objects from path list
		ERR_IF(!(fileList = build_full_filelist(n_files, path_list)), "Filelist empty. Please specify files to send.\n");

		verb(VERB_2, "[%d %s] Waiting for encryption to be ready", g_flags, __func__);
		while ( !get_encrypt_ready() );
		verb(VERB_2, "[%d %s] Encryption verified, proceeding", g_flags, __func__);

#ifdef DONT_CHECK_FILELIST
		send_files(fileList, fileList);
#else
		// send the file list, requesting version from dest
		file_LL* remote_fileList = send_and_wait_for_filelist(fileList);

		// Visit all directories and send all files
		// This is where we pass the remainder of the work to the
		// file handler in sender.cpp
		send_files(fileList, remote_fileList);
#endif
		// signal the end of the transfer
		send_and_wait_for_ack_of_complete();

#ifndef DONT_CHECK_FILELIST
		// free the remote list
		free_file_list(remote_fileList);
		free_file_list(fileList);
#endif
	}

	return RET_SUCCESS;
}


/*
 * void init_parcel
 * - main init routine for parcel
 * - returns: nothing
 */
void init_parcel(int argc, char *argv[])
{
	// set structs g_opts and g_remote_args to their default values
	set_defaults();

	// parse user command line input and get the remaining argument index
	int optind = get_options(argc, argv);

	// fly- we have to do this before the master/minion is set, because g_opts.mode
	// is changed in here depending
	get_remote_host(argc, argv);

	if (g_opts.mode & MODE_RCV) {
		if ( g_opts.remote_to_local ) {
			g_flags |= PARCEL_FLAG_MASTER;
		} else {
			g_flags |= PARCEL_FLAG_MINION;
		}
	} else {
		if ( !g_opts.remote_to_local ) {
			g_flags |= PARCEL_FLAG_MASTER;
		} else {
			g_flags |= PARCEL_FLAG_MINION;
		}
	}

	init_debug_output_file(g_flags & PARCEL_FLAG_MASTER);

	if (g_opts.mode & MODE_RCV) {
		verb(VERB_2, "[%d %s] set to MODE_RCV (%0x)", g_flags, __func__, g_opts.mode);
	} else {
		verb(VERB_2, "[%d %s] set to MODE_SEND (%0x)", g_flags, __func__, g_opts.mode);
	}

	if ( g_flags & PARCEL_FLAG_MINION ) {
		if ( g_opts.encryption ) {
			verb(VERB_2, "[%d %s] we are the minion", g_flags, __func__);
		   // send the key
			g_session_key = generate_session_key();
			fwrite(g_session_key, PARCEL_CRYPTO_KEY_LENGTH, 1, stdout);
			fflush(stdout);
		}
	} else {
		verb(VERB_2, "[%d %s] we are the master", g_flags, __func__);
	}

	verb(VERB_2, "[%d %s] optind = %d, argc = %d", g_flags, __func__, optind, argc);

	// specify how to catch signals
	set_handlers();

	if ( g_opts.remote_to_local ) {
		optind++;
	}
	get_base_path(argc, argv, optind);

	verb(VERB_2, "[%d %s] parcel started as id %d", g_flags, __func__, getpid());

	if ( !g_opts.encryption ) {
		// fly - set this as ready so it passes all checks on non-encrypted
		// it's only used to say it's initialized
		set_encrypt_ready(1);
	}

	initialize_pipes();
	init_sender();
	init_receiver();
}


void cleanup_parcel()
{
	verb(VERB_2, "[%d %s] Cleaning up", g_flags, __func__);

	clean_exit(EXIT_SUCCESS);
}

int main_loop()
{
	int running = 0;

	while ( running ) {
		// check for exit

		// call current state routine

		// process I/O


	}


	return 0;
}



int main(int argc, char *argv[])
{

	init_parcel(argc, argv);

	g_timer = start_timer("send_timer");
	start_transfer(argc, argv, optind);

	cleanup_parcel();
	verb(VERB_2, "[%s] exiting", __func__);

	return RET_SUCCESS;
}
