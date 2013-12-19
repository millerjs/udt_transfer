/*****************************************************************************
Copyright 2013 Laboratory for Advanced Computing at the University of Chicago

	      This file is part of ucp by Joshua Miller

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

#include "ucp.h"
#include "timer.h"
#include "sender.h"
#include "receiver.h"
#include "files.h"

int opt_verbosity = 1;

// Statistics globals
int timer = 0;
off_t TOTAL_XFER = 0;

remote_arg_t remote_args;
ucp_opt_t opts;

using std::cerr;
using std::endl;

// print the usage

void print_bytes(const void *object, size_t size) 
{
    size_t i;
    
    fprintf(stderr, "[ ");
    for(i = 0; i < size; i++){
	fprintf(stderr, "%02x ", ((const unsigned char *) object)[i] & 0xff);
    }
    fprintf(stderr, "]\n");
}

void usage(int EXIT_STAT){

    int opt_lines = 13;
    char* options[] = {
	"--help \t\t print this message",
	"--verbose \t\t verbose, notify of files being sent. Same as -v2",
	"--quiet \t\t silence all warnings. Same as -v0",
	"",
	"-p \t\t print the transfer progress of each file",
	"-l [dest_dir] \t listen for file transfer and write to dest_dir [default ./]",
	"-v verbosity level \t set the level of verbosity",
	"\nLevels of Verbosity:",
	"\t0: Withhold WARNING messages ",
	"\t1: Update user on file transfers [DEFAULT]",
	"\t2: Update user on underlying processes, i.e. directory creation ",
	"\t3: Print information on optimizations ",
	"\t4: Unassigned ",
    };
    
    fprintf(stderr, "Basic usage: \n\tucp source_dir | ucp -l dest_dir\n");
    fprintf(stderr, "Options:\n");
    
    for (int i = 0; i < opt_lines; i++)
	fprintf(stderr, "   %s\n", options[i]);
    
    exit(EXIT_STAT);

}


// Print message to stdout depending on user's selection of verbosity

void verb(int verbosity, char* fmt, ... ){
    if (opt_verbosity >= verbosity){
	va_list args; va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
    }
}

// Warn the user unless verbosity set to 0

void warn(char* fmt, ... ){
    if (opt_verbosity > VERB_0){
	va_list args; va_start(args, fmt);
	fprintf(stderr, "WARNING: ");
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
	va_end(args);
    }
}

// Print error to stdout and quit

void error(char* fmt, ... ){
    va_list args; va_start(args, fmt);
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, fmt, args);
    if (errno) perror(" ");
    fprintf(stderr, "\n");
    va_end(args);
    clean_exit(EXIT_FAILURE);
}



// Make sure that we have killed any zombie or orphaned children

int kill_children(int verbosity){

    // Caught the SIGINT, now kill child process

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
		fprintf(stderr, "Killing remote ucp process... ");

	    char kill_cmd[MAX_PATH_LEN];
	    sprintf(kill_cmd, "kill -s SIGINT %d 2> /dev/null", remote_args.remote_pid);
	    char *args[] = {"ssh", "-A", remote_args.pipe_host, kill_cmd, NULL};

	    // Execute the pipe process
	    if (execvp(args[0], args)){
		fprintf(stderr, "WARNING: unable to kill remote process.");
		exit(EXIT_FAILURE);
	    }

	} else {

	    int stat;
	    waitpid(ssh_kill_pid, &stat, 0);
	    if (!stat) 
		verb(verbosity, "success.");
	    
	}
	
    }

    // Kill the pipe process

    if (remote_args.pipe_pid){

	if (opt_verbosity >= verbosity)
	    fprintf(stderr, "Killing child pipe process... ");

	if (kill(remote_args.pipe_pid, SIGINT)){
	    if (opt_verbosity >= verbosity) perror("FAILURE");
	} else {
	    verb(verbosity, "success.");
	}

	if (opt_verbosity >= verbosity)
	    fprintf(stderr, "Reaping child pipe process... ");	

	// Reap the child pipe process

	int status;
	if ((wait(&status)) == -1){
	    if (opt_verbosity >= verbosity) 
		perror("FAILURE");

	} else { 
	    verb(verbosity, "success.");
	}


    }

    return RET_SUCCESS;

}


double get_scale(off_t size, char*label){
    
     if (size < SIZE_KB){
	sprintf(label, "B");
	return SIZE_B;
    } else if (size < SIZE_MB){
	sprintf(label, "KB");
	return SIZE_KB;
    } else if (size < SIZE_GB){
	sprintf(label, "MB");
	return SIZE_MB;
    } else if (size < SIZE_TB){
	sprintf(label, "GB");
	return SIZE_GB;
    } else if (size < SIZE_PB){
	sprintf(label, "TB");
	return SIZE_TB;
    } else {
	sprintf(label, "PB");
	return SIZE_PB;
    } 

    label = "[?]";
    return 1.0;

}

// Print time and average trasnfer rate

void print_xfer_stats(){

    char label[8];
    
    if (opts.verbosity >= VERB_2 || opts.progress){
	stop_timer(timer);
	double elapsed = timer_elapsed(timer);

	double scale = get_scale(TOTAL_XFER, label);

	fprintf(stderr, "\t\t\tSTAT: %.2f %s transfered in %.2f s [ %.2f Gb/s ] \n", 
		TOTAL_XFER/scale, label, elapsed, 
		TOTAL_XFER/elapsed/scale*SIZE_B);
    }
    
}

// Wrapper for exit() with call to kill_children()

void clean_exit(int status){

    close_log_file();
    print_xfer_stats();
    kill_children(VERB_2);
    exit(status);
}

// Handle SIGINT by exiting cleanly

void sig_handler(int signal){

    // We want to make sure that a forked pipe process isn't left
    // hanging so we will kill it if it exists

    if (signal == SIGINT){
	verb(VERB_0, "\nERROR: [%d] received SIGINT, cleaning up and exiting...", getpid());
    
    }

    if (signal == SIGSEGV){
	verb(VERB_0, "\nERROR: [%d] received SIGSEV, cleaning up and exiting...", getpid());
	perror("Catching SEGFAULT");
    }


    // Kill chiildren and let user know
    
    kill_children(VERB_2);

    // ragequit

    clean_exit(EXIT_FAILURE);

}


// display the transfer progress of the current file

int print_progress(char* descrip, off_t read, off_t total){

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

    if (total){
    
	double percent = total ? read/(double)total*100 : 0;

	sprintf(fmt, "\r +++ %%-%ds %%0.2f/%%0.2f %%s [ %%.2f %%%% ]", path_width);
	fprintf(stderr, fmt, descrip, read/scale, total/scale, label, percent);

    } else {

	sprintf(fmt, "\r +++ %%-%ds %%0.2f/? %%s [ ? %%%% ]", path_width);
	fprintf(stderr, fmt, descrip, read/scale,label);

    }


}


header_t nheader(xfer_t type, off_t size){
    header_t header;
    header.type = type;
    header.data_len = size;
    return header;
}

// create the command used to execvp a pipe i.e. udpipe

int generate_pipe_cmd(char*pipe_cmd, int pipe_mode){
    
    if (*pipe_cmd){

	// Run user specified pipe process
	return RET_SUCCESS;

    } else if (opts.default_udpipe){

	// Make sure the user supplied a host ip 

	if (!*remote_args.pipe_host){
	    fprintf(stderr, "Please specify ip: [-i host]\n");
	    clean_exit(EXIT_FAILURE);
	}

	// Assume udpipe binary is in PATH and run

	char *default_args = "";

	if (pipe_mode == MODE_SEND){
	    sprintf(pipe_cmd, "up %s %s %s", default_args, 
		    remote_args.pipe_host, remote_args.pipe_port);

	} else {
	    sprintf(pipe_cmd, "up %s -l %s", default_args, 
		    remote_args.pipe_port);
	}

    } else {
	return RET_FAILURE;
    }

    return RET_SUCCESS;

}

// execute a pipe process i.e. udpipe and redirect ucp output

int run_pipe(char* pipe_cmd){

    // Create/verfiy the command that will be used to execute the pipe

    if (generate_pipe_cmd(pipe_cmd, opts.mode))
	return RET_FAILURE;

    verb(VERB_2, "Attempting to use pipe: [%s]\n", pipe_cmd);

    char *args[MAX_ARGS];
    bzero(args, MAX_ARGS);
    
    // tokenize the pipe command into argument array

    int arg_idx = 0;
    char* token = strtok(pipe_cmd, " ");
    while (token) {
	args[arg_idx] = strdup(token);
	arg_idx++;
	token = strtok(NULL, " ");
    }

    int pipefd[2];
    int empty_fd[2];
    int st = 0;

    // Create pipe and fork
    pipe(pipefd);
    pipe(empty_fd);
 
    pid_t parent_pid = getpid();

    remote_args.pipe_pid = fork();

    // CHILD
    if (remote_args.pipe_pid == 0) {

	// Redirect stdout to pipe's stdout
	if (opts.mode == MODE_SEND){
	    dup2(pipefd[0], 0);
	    dup2(pipefd[1], 1);
	} 

	// Redirect stdout to pipe's stdout

	else {  //  MODE_RCV
	    dup2(empty_fd[0], 0);
	    dup2(pipefd[1], 1);
	}

	// Execute the pipe process
	if (execvp(args[0], args)){
	    verb(VERB_0, "ERROR: unable to execute pipe process");
	    perror(args[0]);

	    if (opt_verbosity >= VERB_2) 
		fprintf(stderr, "Killing parent process... ");

	    if (kill(parent_pid, SIGINT)){
		if (opt_verbosity >= VERB_2) 
		    perror("FAILURE");
	    } else {
		verb(VERB_2, "success.");
	    }
	    clean_exit(EXIT_FAILURE);
	    
	}
	
    }

    // PARENT
    else {

	// Redirect the input from the pipe to stdin
	if (opts.mode == MODE_SEND){
	    dup2(pipefd[1], 1);
	} 

	// Redirect the output to the pipe's stdout
	else {
	    dup2(pipefd[0], 0);
	}
	
    }

}

// run the ssh command that will create a remote ucp process

int run_ssh_command(char *remote_dest){

    if (!remote_dest) 
	return RET_FAILURE;
    
    verb(VERB_2, "Attempting to run remote command to %s:%s\n", 
	 remote_args.pipe_host, remote_args.pipe_port);

    // Create pipe and fork

    int ssh_fd[2];
    pipe(ssh_fd);

    remote_args.ssh_pid = fork();

    // CHILD
    if (remote_args.ssh_pid == 0) {

	char remote_pipe_cmd[MAX_PATH_LEN];	
	bzero(remote_pipe_cmd, MAX_PATH_LEN);

	if (opts.mode == MODE_SEND){

	    // Generate remote ucp command to RECEIVE DATA
	    // generate_pipe_cmd(remote_pipe_cmd, MODE_RCV);
	    
	    // sprintf(remote_pipe_cmd, "%s -x -l -v4 --udpipe -l %s 2> /dev/null & %s", 
	    sprintf(remote_pipe_cmd, "%s -x -l -v4 --udpipe -l %s 2>~/ucp.log & %s", 
		    remote_args.udpipe_location, remote_args.remote_dest, "echo $!");
	    
	    // Redirect output from ssh process to ssh_fd

	    char *args[] = {"ssh", "-A", remote_args.pipe_host, 
			    remote_pipe_cmd, NULL};

	    if (opt_verbosity >= VERB_3){
		fprintf(stderr, "ssh command: ");
		for (int i = 0; args[i]; i++)
		    fprintf(stderr, "\"%s\" ", args[i]);
		fprintf(stderr, "\n");
	    }

	    dup2(ssh_fd[2], 2);
	    dup2(ssh_fd[1], 1);

	    if (execvp(args[0], args)){
	    	fprintf(stderr, "ERROR: unable to execute ssh process\n");
	    	clean_exit(EXIT_FAILURE);
	    } else {
	    	fprintf(stderr, "ERROR: premature ssh process exit\n");
	    	clean_exit(EXIT_FAILURE);
	    }

	} else if (opts.mode == MODE_RCV){
	    
	    // TODO

	    error("Remote -> Local transfers not currently supported, remove -l?\n");

	}

    }

    // PARENT
    else {
	
	// Try and get the pid of the remote process from the ssh pipe input

	char ssh_pid_str[MAX_PATH_LEN];
	if (read(ssh_fd[0], ssh_pid_str, MAX_PATH_LEN) < 0){

	    perror("WARNING: Unable to read pid from remote process");

	} else {

	    // Read something from the pipe, proceed

	    remote_args.remote_pid = atoi(ssh_pid_str);

	    verb(VERB_2, "Remote process pid: %d\n", remote_args.remote_pid);

	}

	// Do your thing, ucp, moving on.
	
    }

}

int parse_xfer_cmd(char*xfer_cmd){

    int hostlen = -1;
    int cmd_len = strlen(xfer_cmd);

    for (int i = 0; i < cmd_len; i++){
	if (xfer_cmd[i] == ':'){
	    hostlen = i+1;
	    break;
	}
    }

    if (hostlen < 0)
	error("Please specify transfer command [host:destination_file]");
    
    snprintf(remote_args.pipe_host, hostlen, "%s", xfer_cmd);
    snprintf(remote_args.remote_dest, cmd_len-hostlen+1,"%s", xfer_cmd+hostlen);

}

int set_defaults(){
    
    bzero(remote_args.pipe_cmd, MAX_PATH_LEN);
    bzero(remote_args.xfer_cmd, MAX_PATH_LEN);
    bzero(remote_args.pipe_host, MAX_PATH_LEN);
    bzero(remote_args.pipe_port, MAX_PATH_LEN);
    bzero(remote_args.remote_dest, MAX_PATH_LEN);
    
    sprintf(remote_args.udpipe_location, "ucp");
    sprintf(remote_args.pipe_port, "9000");
    
    opts.mode		= MODE_SEND;
    opts.verbosity	= VERB_1;
    opts.recurse	= 1;
    opts.regular_files	= 1;
    opts.progress	= 1;
    opts.default_udpipe	= 0;
    opts.remote		= 0;
    opts.delay		= 0;
    opts.log		= 0;
    opts.restart	= 0;
    opts.mmap		= 0;
    opts.full_root	= 0;

    return RET_SUCCESS;

}


int get_options(int argc, char *argv[]){

    int opt;
    
    // Read in options

    static struct option long_options[] =
	{
	    {"verbose"		, no_argument	, &opts.verbosity	, VERB_2}	,
	    {"v"		, no_argument	, &opts.verbosity	, VERB_2}	,
	    {"quiet"		, no_argument	, &opts.verbosity	, VERB_0}	,
	    {"no-mmap"		, no_argument	, &opts.mmap		, 1}		,
	    {"full-root"	, no_argument	, &opts.full_root	, 1}		,
	    {"all-files"	, no_argument	, &opts.regular_files	, 0}		,
	    {"help"		, no_argument		, NULL	, 'h'},
	    {"udpipe"		, required_argument	, NULL	, 'u'},
	    {"pipe"		, required_argument	, NULL	, 'n'},
	    {"remote"		, required_argument	, NULL	, 'o'},
	    {"log"		, required_argument	, NULL	, 'g'},
	    {"restart"		, required_argument	, NULL	, 'r'},
	    {"checkpoint"	, required_argument	, NULL	, 'k'},
	    {0, 0, 0, 0}
        };

    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "n:a:i:u:txlhv:np:d:c:o:g:k:r:", 
			      long_options, &option_index)) != -1){
	switch (opt){

	case 'k':
	    opts.restart = 1;
	    opts.log = 1;
	    sprintf(log_path, "%s", optarg);
	    sprintf(opts.restart_path, "%s", optarg);
	    break;

	case 'r':
	    opts.restart = 1;
	    sprintf(opts.restart_path, "%s", optarg);
	    break;

	case 'g':
	    opts.log = 1;
	    sprintf(log_path, "%s", optarg);
	    break;
	    
	case 'o':
	    opts.remote = 1;
	    sprintf(remote_args.xfer_cmd, "%s", optarg);
	    break;

	case 'c':
	    sprintf(remote_args.udpipe_location, "%s", optarg);
	    break;
	    
	case 'x':
	    opts.progress = 1;
	    break;

	case 'l':
	    opts.mode = MODE_RCV;
	    break;

	case 'p':
	    sprintf(remote_args.pipe_port, "%s", optarg);
	    break;

	case 'i':
	    sprintf(remote_args.pipe_host, "%s", optarg);
	    break;

	case 'd':
	    opts.delay = atoi(optarg);
	    break;

	case 'u':
	    if (*remote_args.pipe_cmd){
		fprintf(stderr, "ERROR: --udpipe [-u] and --pipe [-c] exclusive.\n");
		clean_exit(EXIT_FAILURE);
	    }
	    sprintf(remote_args.pipe_host, "%s", optarg);
	    opts.default_udpipe = 1;
	    break;
		
	case 'n':
	    if (opts.default_udpipe){
		fprintf(stderr, "ERROR: --udpipe [-u] and --pipe [-c] exclusive.\n");
		clean_exit(EXIT_FAILURE);
	    }
	    strcpy(remote_args.pipe_cmd, optarg);
	    break;
	    
	case 'v':
	    opts.verbosity = atoi(optarg);
	    break;
	    
	case 'h':
	    usage(0);
	    break;
	    
	case '\0':
	    break;
	    
	default:
	    fprintf(stderr, "Unknown command line option: [%c].\n", opt);
	    usage(EXIT_FAILURE);
	    
	}
    }

    opt_verbosity = opts.verbosity;
    if (opt_verbosity < VERB_1)
	opts.progress = 0;

    return optind;

}


int set_handlers(){

    // Catch interupt signals

    if (signal(SIGINT, sig_handler) == SIG_ERR){
	fprintf(stderr, "ERROR: unable to set SIGINT handler\n");
    }

    // if (signal(SIGSEGV, sig_handler) == SIG_ERR){
    // 	fprintf(stderr, "ERROR: unable to set SIGSEGV handler\n");
    // }

    return RET_SUCCESS;

}


int main(int argc, char *argv[]){

    int optind;
    file_LL *fileList = NULL;
    
    set_handlers();

    set_defaults();

    optind = get_options(argc, argv);

    open_log_file();

    if (opts.restart){
	verb(VERB_2, "Loading restart checkpoint [%s].", opts.restart_path);
	read_checkpoint(opts.restart_path);

    }


    if (opts.remote){

	parse_xfer_cmd(remote_args.xfer_cmd);

	if (!*remote_args.pipe_host){
	    fprintf(stderr, "ERROR: --remote requires a host [-i ip]\n");
	    clean_exit(EXIT_FAILURE);
	}

	if (!*remote_args.pipe_cmd) 
	    opts.default_udpipe = 1;

	run_ssh_command(remote_args.remote_dest);
	
	if (opts.delay)
	    verb(VERB_1, "Delaying %ds for slow connection\n", opts.delay);
	
	sleep(opts.delay);

    }

    // If in sender mode, test the files for typing, and build a file list
    
    if (opts.mode == MODE_SEND){



	run_pipe(remote_args.pipe_cmd);
	
	// Did the user pass any files?
	if (optind < argc){
	    
	    verb(VERB_2, "Running with file source mode.\n");
	    
	    // Clarify what we are passing as a file list
	    
	    int n_files = argc-optind;
	    char **path_list = argv+optind;

	    // Generate a linked list of file objects from path list
	    
	    fileList = build_filelist(n_files, path_list);
	    
	    // Visit all directories and send all files
	    
	    timer = start_timer("send_timer");

	    handle_files(fileList);
	    
	    complete_xfer();
	    
	} 
	
	// if No files were passed by user
	else {
	    fprintf(stderr, "No files specified. Did you mean to receive? [-l]\n");
	    clean_exit(EXIT_FAILURE);
	}
	
    } 
    
    // Otherwise, switch to receiving mode
    
    else if (opts.mode == MODE_RCV) {

	verb(VERB_2, "Running with file destination mode\n");
	
	// Check to see if user specified a pipe, if so, run it

	run_pipe(remote_args.pipe_cmd);

	// Destination directory was passed
	
	if (optind < argc) {
	    
	    // Generate a base path for file locations
	    
	    char base_path[MAX_PATH_LEN];
	    bzero(base_path, MAX_PATH_LEN);
	    sprintf(base_path, "%s", argv[optind++]);
	    
	    // Are there any remaining command line args? Warn user
	    
	    if (optind < argc){
		if (opts.verbosity > VERB_0){
		    fprintf(stderr, "WARNING: Unused command line args: [");
		    for (optind; optind < argc-1; optind++)
			fprintf(stderr, "%s, ", argv[optind]);
		    fprintf(stderr, "%s]\n", argv[optind]);
		}
	    }
	    
	    timer = start_timer("receive_timer");
	    // Listen to sender for files and data
	    receive_files(base_path);
	    
	}
	
	// No destination directory was passed, default to current dir
	
	else {
	    
	    timer = start_timer("receive_timer");
	    receive_files("");
	    
	}
	
    }
    
    print_xfer_stats();    

    sleep(END_LATENCY);

    kill_children(VERB_2);
    
    return RET_SUCCESS;
  
}
