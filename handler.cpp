/*****************************************************************************
Copyright 2013 Laboratory for Advanced Computing at the University of Chicago

This file is part of ucp

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

#include "handler.h"
#include "timer.h"

using std::cerr;
using std::endl;

// Global settings
int opt_verbosity = VERB_1;
int opt_recurse = 1;
int opt_mode = MODE_SEND;
int opt_progress = 0;
int opt_regular_files = 1;
int opt_default_udpipe = 0;
int opt_auto = 0;
int opt_delay = 0;

// The global variables for remote connection
int pipe_pid = 0;
int ssh_pid = 0;
int remote_pid = 0;
char remote_dest[MAX_PATH_LEN];
char pipe_port[MAX_PATH_LEN];
char pipe_host[MAX_PATH_LEN];
char udpipe_location[MAX_PATH_LEN];

// Statistics globals
int timer = 0;
long int TOTAL_XFER = 0;


// Buffers
char data[BUFFER_LEN];
char path_buff[BUFFER_LEN];


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

int kill_children(int verbosity){

    // Caught the SIGINT, now kill child process

    // Clean up the ssh process

    if (ssh_pid && remote_pid){

	if (opt_verbosity > verbosity) 
	    fprintf(stderr, "Killing child ssh process... ");

	if (kill(ssh_pid, SIGINT)){
	    if (opt_verbosity > verbosity) perror("failure");
	} else {
	    if (opt_verbosity > verbosity) fprintf(stderr, "success.\n");
	}

	int ssh_kill_pid;
	ssh_kill_pid = fork();

	// CHILD
	if (ssh_kill_pid == 0) {

	    if (opt_verbosity > verbosity) 
		fprintf(stderr, "Killing remote ucp process... ");

	    char kill_cmd[MAX_PATH_LEN];
	    sprintf(kill_cmd, "kill -s SIGINT %d 2>/dev/null", remote_pid);
	    char *args[] = {"ssh", "-A", pipe_host, kill_cmd, NULL};

	    // Execute the pipe process
	    if (execvp(args[0], args)){
		fprintf(stderr, "WARNING: unable to kill remote process.\n");
		exit(EXIT_FAILURE);
	    }

	} else {

	    int stat;
	    waitpid(ssh_kill_pid, &stat, 0);
	    if (!stat) fprintf(stderr, "success.\n");
	    
	}

    }

    // Kill the pipe process

    if (pipe_pid){

	if (opt_verbosity > verbosity) fprintf(stderr, "Killing child pipe process... ");

	if (kill(pipe_pid, SIGINT)){
	    if (opt_verbosity > verbosity) perror("failure");
	} else {
	    if (opt_verbosity > verbosity) fprintf(stderr, "success.\n");
	}

	if (opt_verbosity > verbosity) 
	    fprintf(stderr, "Reaping child pipe process... ");	

	// Reap the child pipe process

	int status;
	if ((wait(&status)) == -1){
	    if (opt_verbosity > verbosity) 
		perror("failure");

	} else { 

	    if (opt_verbosity > verbosity) 
		fprintf(stderr, "success.\n");

	}


    }


    return RET_SUCCESS;

}

void print_xfer_stats(){
    
    if (opt_verbosity > VERB_1 || opt_progress){
	stop_timer(timer);
	double elapsed = timer_elapsed(timer);

	char fmt[1024];
	fprintf(stderr, "\t\t\tSTAT: %.2f GB transfered in %.2f s [ %.2f Gb/s ] \n", 
		TOTAL_XFER/1.e9, elapsed, TOTAL_XFER/elapsed/1e9*8.0);
    }
    
}

int clean_exit(int status){

    // print_xfer_stats();

    kill_children(VERB_2);
    exit(status);

}

void sig_handler(int signal){

    // We want to make sure that a forked pipe process isn't left
    // hanging so we will kill it if it exists

    if (signal == SIGINT){

	fprintf(stderr, "\nSIGINT: Handling signal.\n");

    }

    // Kill chiildren and let user know
    
    kill_children(VERB_0);

    // ragequit

    clean_exit(EXIT_FAILURE);

}

int write_header(header_t header){
    return write(fileno(stdout), &header, sizeof(header_t));
}

int write_data(char*data, int len){
    return write(fileno(stdout), data, len);
    TOTAL_XFER += len;
}

int print_progress(char* descrip, int read, int total){

    // Get the width of the terminal

    struct winsize term;
    int path_width;

    if (ioctl(fileno(stdout), TIOCGWINSZ, &term)){
	path_width = 60;
    } else {
	int progress_width = 40;
	path_width = term.ws_col - progress_width;
    }
    
    // Make transfer progress pretty

    char fmt[1024];
    sprintf(fmt, "\r +++ %%-%ds %%0.2f/%%0.2f MB [ %%.2f %%%% ]", path_width);
    fprintf(stderr, fmt,
	    descrip,
	    read/1000000.,
	    total/1000000., 
	    read/(float)total*100.);

}


int send_file(file_object_t *file){

    if (!file) return -1;

    if (opt_verbosity > VERB_1)
	fprintf(stderr, "   > Sending [%s]  %s\n", file->filetype, file->path);

    header_t header;

    if (file->mode == S_IFDIR){

	// create a header to specify that the subsequent data is a
	// directory name

	header.type = XFER_DIRNAME;
	header.data_len = strlen(file->path)+1;

	// send header and directory name

	write_header(header);
	write_data(file->path, header.data_len);

    }

    else {

	// create header to specify that subsequent data is a regular
	// filename

	header.type = XFER_FILENAME;
	header.data_len = strlen(file->path)+1;

	// send header and file path

	write_header(header);
	write_data(file->path, header.data_len);	
	
	// open file to cat data

	FILE *fp = fopen(file->path, "r");
	if (!fp){
	    perror("ERROR: unable to open file");
	    clean_exit(EXIT_FAILURE);
	}

	int adv = posix_fadvise64(fileno(fp), 0, 0, 
				POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE);

	if (adv < 0){
	    if (opt_verbosity > VERB_3)
		perror("WARNING: Unable to advise file read");
	}
 
	// Get the length of the file in advance
	fseek(fp, 0L, SEEK_END);
	int file_size = ftell(fp);
	fseek(fp, 0L, SEEK_SET);

	// create header to specify that we are also sending file data

	header_t data_header;
	data_header.type = XFER_DATA;
	data_header.data_len = file_size;

	// send data header

	write_header(data_header);

	// buffer and send file

	int rs, sent = 0;
	while (rs = fread(data, sizeof(char), BUFFER_LEN, fp)){

	    if (rs < 0){
		perror("ERROR: error reading from file");
		clean_exit(EXIT_FAILURE);
	    }
	    
	    write_data(data, rs);
	    sent += rs;
	    
	    if (opt_progress)
		print_progress(file->path, sent, data_header.data_len);

	}
	if (opt_progress) fprintf(stderr, "\n");

    }
    
}

int read_data(void* b, int len){

    int rs, total = 0;
    char* buffer = (char*)b;
    
    while (total < len){
	rs = read(fileno(stdin), buffer+total, len - total);
	total += rs;
	TOTAL_XFER += rs;
    }

    return total;

}


int get_parent_dir(char parent_dir[MAX_PATH_LEN], char path[MAX_PATH_LEN]){
    
    bzero(parent_dir, MAX_PATH_LEN);
    char*cursor = path+strlen(path);

    while (cursor > path && *cursor != '/')
	cursor--;
		    
    if (cursor <= path){
	fprintf(stderr, "ERROR: Unable to recognize parent directory.\n");
	clean_exit(EXIT_FAILURE);
    }
		    
    memcpy(parent_dir, path, cursor-path);
}

int mkdir_parent(char* path){

    // default permissions for creating new directories
    int ret, err;
    int mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;

    ret = mkdir(path, mode);

    if ( ret ){
	
	// Hold onto last error

	err = errno;
	
	// If the parents in the path name do not exist, then make them

	if (err == ENOENT){
	    
	    if (opt_verbosity > VERB_2)
		fprintf(stderr, "Again find parent directory and make it\n");

	    char parent_dir[MAX_PATH_LEN];
	    get_parent_dir(parent_dir, path);
	    

	    if (opt_verbosity > VERB_2)
		fprintf(stderr, "Attempting to make %s\n", parent_dir);

	    mkdir_parent(parent_dir);

	}

	// The directory already exists
	else if (err = EEXIST){
	    // Continue
	}

	// Otherwise, mkdir failed
	else {
	    fprintf(stderr, "ERROR: Unable to create directory [%s]: %s\n", 
		    path, strerror(err));
	    clean_exit(EXIT_FAILURE);

	}

    } else {
	
	if (opt_verbosity > VERB_2)
	    fprintf(stderr, "Built directory %s\n", path);

    }


    return ret;

}


int receive_files(char*base_path){

    int complete = 0;
    int rs, ds;
    
    char data_path[MAX_PATH_LEN];
    int expecting_data = 0;


    // generate a base path for all destination files    

    int bl = strlen(base_path);
    if (base_path[bl] == '/') bl++;
    sprintf(data_path, "%s/", base_path);
    
    // Read in headers and data until signalled completion

    while (!complete){
	
	header_t header;
	rs = read_data(&header, sizeof(header_t));

	if (rs){

	    // We are receiving a directory name

	    if (header.type == XFER_DIRNAME){

		read_data(data_path+bl, header.data_len);
		
		if (opt_verbosity > VERB_1)
		    fprintf(stderr, "making directory: %s\n", data_path);
	    
		// make directory, if any parent in directory path
		// doesnt exist, make that as well

		int r = mkdir_parent(data_path);

		// safety reset, data block after this will fault
		expecting_data = 0;

	    } 

	    // We are receiving a file name

	    else if (header.type == XFER_FILENAME) {
		
		read_data(data_path+bl, header.data_len);

		if (opt_verbosity > VERB_1)
		    fprintf(stderr, "Initializing file receive: %s\n", 
			    data_path+bl);

		// the block following is expected to be data
		expecting_data = 1;

	    }

	    // We are receiving a data chunk

	    else if (header.type == XFER_DATA) {
		
		if (!expecting_data){
		    fprintf(stderr, "ERROR: Out of order data block.\n");
		    clean_exit(EXIT_FAILURE);
		}

		int rs, len, total = 0;

		int out = open(data_path, O_CREAT, S_IRUSR | S_IWUSR);

		if (out < 0) {

		    // If we can't open the file, try building a
		    // directory tree to it

		    if (opt_verbosity > VERB_1){
			perror("WARNING: Unable to open file for writing");
			fprintf(stderr, "WARNING: Building directory tree: %s.\n", 
				data_path);
		    }
		    
		    // Try and get a parent directory from file

		    char parent_dir[MAX_PATH_LEN];
		    get_parent_dir(parent_dir, data_path);
		    
		    if (opt_verbosity > VERB_2)
			fprintf(stderr, "Using %s as parent directory.\n", parent_dir);
		    
		    // Build parent directory recursively

		    int ret = mkdir_parent(parent_dir);

		}
		

		// Attempt to optimize simple sequential read

		int adv = posix_fadvise64(out, 0, 0, 
					POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE);

		if (adv < 0){
		    if (opt_verbosity > VERB_3)
			perror("WARNING: Unable to advise file write");
		}
 
		while (total < header.data_len){

		    // Either look to receive a whole buffer of
		    // however much remains in the data block

		    len = (BUFFER_LEN < header.data_len - total) ? 
			BUFFER_LEN : header.data_len - total;

		    // read data buffer from stdin

		    rs = read_data(data, len);

		    if (rs < 0){
			perror("ERROR: Unable to read file");
			clean_exit(EXIT_FAILURE);
		    }

		    // Write to file
		    
		    write(out, data, rs);
		    total += rs;

		    // Update user on progress if opt_progress set to true		    

		    if (opt_progress)
			print_progress(data_path, total, header.data_len);

		}
		
		if (opt_progress) fprintf(stderr, "\n");

		close(out);

		// another data block is not expected
		expecting_data = 0;

	    }

	    // Or maybe the transfer is complete

	    else if (header.type == XFER_COMPLTE){
		if (opt_verbosity > VERB_1)
		    fprintf(stderr, "Receive completed.\n");
		return RET_SUCCESS;
	    }

	    // Catch corrupted headers
	    else {
		fprintf(stderr, "ERROR: Corrupt header.\n");
		clean_exit(EXIT_FAILURE);
	    }



	}
	
    }
    
}


int handle_files(file_LL* fileList){

    // Send each file or directory
    while (fileList){

	file_object_t *file = fileList->curr;
	
	// While there is a directory, opt_recurse?
	if (file->mode == S_IFDIR){
	    
	    // Tell desination to create a directory 
	    send_file(file);

	    // Recursively enter directory
	    if (opt_recurse){

		if (opt_verbosity > VERB_1)
		    fprintf(stderr, "> Entering [%s]  %s\n", 
			    file->filetype, file->path);

		// Get a linked list of all the files in the directory 
		file_LL* internal_fileList = lsdir(file);

		// if directory is non-empty then recurse 
		if (internal_fileList){
		    handle_files(internal_fileList);
		}
	    }
	    // If User chose not to recurse into directories
	    else {
		if (opt_verbosity > VERB_1)
		    fprintf(stderr, "> SKIPPING [%s]  %s\n", 
			    file->filetype, file->path);
	    }

	} 

	// if it is a regular file, then send it
	else if (file->mode == S_IFREG){
	    send_file(file);
	} 

	// If the file is a character device or a named pipe, warn user
	else if (file->mode == S_IFCHR || file->mode == S_IFIFO){

	    if (opt_regular_files){

		if (opt_verbosity > VERB_0)
		    fprintf(stderr, "WARNING: Skipping %s [%s].  %s.\n",
			    file->path, file->filetype,
			    // "To enable sending character devices, remove");
			    "Sending non-regular files disabled in this version.\n");

	    } else {

		if (opt_verbosity > VERB_0)
		    fprintf(stderr, "WARNING: Sending %s [%s].  %s %s.\n",
			    file->path, file->filetype,
			    "To prevent sending character devices, specify",
			    "--regular-files");
		
		send_file(file);

	    }

	    
	}


	// if it's neither a regular file nor a directory, leave it
	// for now, maybe send in later version
	else {

	    if (opt_verbosity > VERB_1)
		fprintf(stderr, "   > SKIPPING [%s]  \%s\n", 
			file->filetype, file->path);

	    if (opt_verbosity > VERB_0){
		fprintf(stderr,"ERROR: file %s is a %s, ", 
			file->path, file->filetype);
		fprintf(stderr,"ucp does not currently support this filetype.\n");
	    }

	}

	fileList = fileList->next;

    }

    return RET_SUCCESS;

}

int complete_xfer(){
    
    // Notify the destination that the transfer is complete
    if (opt_verbosity > VERB_1)
	fprintf(stderr, "Signalling end of transfer.\n");

    // Send completition header

    header_t header;
    header.type = XFER_COMPLTE;
    header.data_len = 0;
    write_header(header);
    
    return RET_SUCCESS;

}

int generate_pipe_cmd(char*pipe_cmd, int pipe_mode){
    
    if (*pipe_cmd){

	// Run user specified pipe process
	return RET_SUCCESS;

    } else if (opt_default_udpipe){

	// Make sure the user supplied a host ip 

	if (!*pipe_host){
	    fprintf(stderr, "Please specify ip: [-i host]\n");
	    clean_exit(EXIT_FAILURE);
	}

	// Assume udpipe binary is in PATH and run

	char *default_args = "";

	if (pipe_mode == MODE_SEND){
	    sprintf(pipe_cmd, "up %s %s %s -t 15", default_args, pipe_host, pipe_port);

	} else {
	    sprintf(pipe_cmd, "up %s -l %s -t 15", default_args, pipe_port);
	}

    } else {
	return RET_FAILURE;
    }

    return RET_SUCCESS;

}

int run_pipe(char* pipe_cmd){

    // Create/verfiy the command that will be used to execute the pipe

    if (generate_pipe_cmd(pipe_cmd, opt_mode))
	return RET_FAILURE;

    if (opt_verbosity > VERB_1)
	fprintf(stderr, "Attempting to use pipe: [%s]\n", pipe_cmd);

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
    pipe_pid = fork();

    // CHILD
    if (pipe_pid == 0) {

	// Redirect stdout to pipe's stdout
	if (opt_mode == MODE_SEND){
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
	    fprintf(stderr, "ERROR: unable to execute pipe process\n");
	    clean_exit(EXIT_FAILURE);
	}
    }

    // PARENT
    else {

	// Redirect the input from the pipe to stdin
	if (opt_mode == MODE_SEND){
	    dup2(pipefd[1], 1);
	} 

	// Redirect the output to the pipe's stdout
	else {
	    dup2(pipefd[0], 0);
	}

	// st = 0;
	// waitpid(-1, &st, 0);
	// fprintf(stderr, "Status: %d\n", st);
	
    }


}

int run_ssh_command(char *remote_dest){

    if (!remote_dest) 
	return RET_FAILURE;
    
    if (opt_verbosity > VERB_1)
	fprintf(stderr, "Attempting to run remote command to %s:%s\n", 
		pipe_host, pipe_port);


    // Create pipe and fork

    int ssh_fd[2];
    pipe(ssh_fd);

    ssh_pid = fork();

    // CHILD
    if (ssh_pid == 0) {

	char remote_pipe_cmd[MAX_PATH_LEN];	
	bzero(remote_pipe_cmd, MAX_PATH_LEN);

	if (opt_mode == MODE_SEND){

	    // Generate remote ucp command to RECEIVE DATA
	    // generate_pipe_cmd(remote_pipe_cmd, MODE_RCV);
	    
	    sprintf(remote_pipe_cmd, "%s -l -v4 --udpipe -l %s 2>/dev/null & %s", 
		    udpipe_location, remote_dest, "echo $!");
	    
	    // Redirect output from ssh process to ssh_fd

	    char *args[] = {"ssh", "-A", pipe_host, remote_pipe_cmd, NULL};

	    if (opt_verbosity > VERB_1){
		fprintf(stderr, "ssh command: ");
		for (int i = 0; args[i]; i++)
		    fprintf(stderr, "\"%s\" ", args[i]);
		fprintf(stderr, "\n\n");
	    }

	    // dup2(ssh_fd[0], 0);
	    dup2(ssh_fd[1], 1);

	    if (execvp(args[0], args)){
	    	fprintf(stderr, "ERROR: unable to execupte ssh process\n");
	    	clean_exit(EXIT_FAILURE);
	    } else {
	    	fprintf(stderr, "ERROR: premature ssh process exit\n");
	    	clean_exit(EXIT_FAILURE);
	    }

	} else if (opt_mode == MODE_RCV){
	    
	    // TODO

	    fprintf(stderr, "ERROR: Remote -> Local transfers not currently supported\n");
	    clean_exit(EXIT_FAILURE);

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

	    remote_pid = atoi(ssh_pid_str);
	    if (opt_verbosity > VERB_1)
		fprintf(stderr, "Remote process pid: %d\n", remote_pid);

	}


	// Do your thing, ucp, moving on.
	
    }

}

int parse_auto_optins(char*options){
    
    char*cursor = options;
    // char*
    while (*cursor){

	if (*cursor == ':')
	
	cursor++;
    }
    
    
}


int main(int argc, char *argv[]){
    
    if (signal(SIGINT, sig_handler) == SIG_ERR){
	fprintf(stderr, "ERROR: unable to set SIGINT handler\n");
    }

    // Set defaults

    int opt;
    file_LL *fileList = NULL;
    char pipe_cmd[MAX_PATH_LEN];

    bzero(pipe_cmd, MAX_PATH_LEN);
    bzero(pipe_host, MAX_PATH_LEN);
    bzero(pipe_port, MAX_PATH_LEN);
    bzero(remote_dest, MAX_PATH_LEN);

    sprintf(udpipe_location, "ucp");

    sprintf(pipe_port, "9000");

    // Read in options

    static struct option long_options[] =
	{
	    {"verbose", no_argument, &opt_verbosity, VERB_2},
	    {"regular-files", no_argument, &opt_regular_files, 1},
	    {"quiet",   no_argument, &opt_verbosity, VERB_0},
	    {"progress", no_argument, 0, 'x'},
	    {"help",    no_argument, 0, 'h'},
	    {"udpipe",    required_argument, 0, 'u'},
	    {"pipe",    required_argument, 0, 'n'},
	    {"remote",    required_argument, 0, 'r'},
	    {0, 0, 0, 0}
	};

    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "n:a:i:u:txlhv:np:d:", 
			      long_options, &option_index)) != -1){
	switch (opt){

	case 'r':
	    opt_auto = 1;
	    sprintf(remote_dest, optarg);
	    break;

	case 'c':
	    sprintf(udpipe_location, "%s", optarg);
	    break;
	    
	case 'x':
	    opt_progress = 1;
	    break;

	case 'l':
	    opt_mode = MODE_RCV;
	    break;

	case 'p':
	    sprintf(pipe_port, "%s", optarg);
	    break;

	case 'i':
	    sprintf(pipe_host, "%s", optarg);
	    break;

	case 'd':
	    opt_delay = atoi(optarg);
	    break;

	case 'u':
	    if (*pipe_cmd){
		fprintf(stderr, "ERROR: --udpipe [-u] and --pipe [-c] exclusive.\n");
		clean_exit(EXIT_FAILURE);
	    }
	    sprintf(pipe_host, "%s", optarg);
	    opt_default_udpipe = 1;
	    break;
		
	case 'n':
	    if (opt_default_udpipe){
		fprintf(stderr, "ERROR: --udpipe [-u] and --pipe [-c] exclusive.\n");
		clean_exit(EXIT_FAILURE);
	    }
	    strcpy(pipe_cmd, optarg);
	    break;
	    
	// case 'n':
	//     opt_recurse = 0;
	//     break;
	    
	case 'v':
	    opt_verbosity = atoi(optarg);
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
    
    if (opt_auto){

	if (!*pipe_host){
	    fprintf(stderr, "ERROR: --remote requires a host [-i ip]\n");
	    clean_exit(EXIT_FAILURE);
	}

	if (!*pipe_cmd) 
	    opt_default_udpipe = 1;

	run_ssh_command(remote_dest);
	
	if (opt_delay && (opt_verbosity > VERB_0))
	    fprintf(stderr, "Delaying %ds for slow connection\n", opt_delay);
	
	sleep(opt_delay);

    }

    // If in sender mode, test the files for typing, and build a file list
    
    if (opt_mode == MODE_SEND){

	run_pipe(pipe_cmd);
	
	// Did the user pass any files?
	if (optind < argc){
	    
	    if (opt_verbosity > VERB_1)
		fprintf(stderr, "Running with file source mode.\n");
	    
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
    
    else if (opt_mode == MODE_RCV) {

	if (opt_verbosity > VERB_1)
	    fprintf(stderr, "Running with file destination mode\n");
	
	// Check to see if user specified a pipe, if so, run it

	run_pipe(pipe_cmd);

	// Destination directory was passed
	
	if (optind < argc) {
	    
	    // Generate a base path for file locations
	    
	    char base_path[MAX_PATH_LEN];
	    bzero(base_path, MAX_PATH_LEN);
	    strcat(base_path, argv[optind++]);
	    strcat(base_path, "/");
	    
	    // Are there any remaining command line args? Warn user
	    
	    if (optind < argc){
		if (opt_verbosity > VERB_0){
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

    sleep(END_LATENCY);

    clean_exit(EXIT_SUCCESS);
    
    return RET_SUCCESS;
  
}
