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

using std::cerr;
using std::endl;

// Global settings
int opt_verbosity = VERB_1;
int opt_recurse = 1;
int opt_mode = MODE_SEND;
int opt_progress = 0;
int opt_regular_files = 1;

// The pid of the pipe process if forked/executed
int pipe_pid = 0;

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

void sig_handler(int signal){

    // We want to make sure that a forked pipe process isn't left
    // hanging so we will kill it if it exists

    if (signal == SIGINT){

	fprintf(stderr, "\nSIGINT: ucp killed by SIGINT\n");

	// Caught the SIGINT, now kill child process

	if (pipe_pid){
	    fprintf(stderr, "Killing child pipe process... ");
	    if (kill(pipe_pid, SIGKILL)){
		fprintf(stderr, "[FAILURE]\n");
	    } else {
		fprintf(stderr, "[SUCCESS]\n");
	    }
	}
	
	// ragequit

	exit(EXIT_FAILURE);

    }

}

int write_header(header_t header){
    return write(fileno(stdout), &header, sizeof(header_t));
}

int write_data(char*data, int len){
    return write(fileno(stdout), data, len);
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
	    exit(EXIT_FAILURE);
	}

	int adv = posix_fadvise(fileno(fp), 0, 0, 
				POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE);

	if (adv){
	    if (opt_verbosity > VERB_3)
		fprintf(stderr, "WARNING: Unable to advise file read\n");
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

	int rs;
	while (rs = fread(data, sizeof(char), BUFFER_LEN, fp)){

	    if (rs < 0){
		perror("ERROR: error reading from file");
		exit(EXIT_FAILURE);
	    }
	    
	    write_data(data, rs);

	}

    }
    
}


int read_data(void* b, int len){

    int rs, total = 0;
    char* buffer = (char*)b;
    
    while (total < len){
	rs = read(fileno(stdin), buffer+total, len - total);
	total += rs;
    }

    return total;

}

int print_progress(char* descrip, int read, int total){

    // Get the width of the terminal

    struct winsize term;
    ioctl(fileno(stdout), TIOCGWINSZ, &term);

    int progress_width = 40;
    int path_width = term.ws_col - progress_width;
    
    char fmt[1024];
    sprintf(fmt, "\r +++ %%-%ds %%0.2f/%%0.2f MB [ %%.2f %%%% ]", path_width);
    fprintf(stderr, fmt,
	    descrip,
	    read/1000000.,
	    total/1000000., 
	    read/(float)total*100.);

}

int get_parent_dir(char parent_dir[MAX_PATH_LEN], char path[MAX_PATH_LEN]){
    
    bzero(parent_dir, MAX_PATH_LEN);
    char*cursor = path+strlen(path);

    while (cursor > path && *cursor != '/')
	cursor--;
		    
    if (cursor <= path){
	fprintf(stderr, "ERROR: Unable to recognize parent directory.\n");
	exit(EXIT_FAILURE);
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
	    exit(EXIT_FAILURE);

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
		    exit(EXIT_FAILURE);
		}

		int rs, len, total = 0;

		int out = open(data_path, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
		
		if (out < 0) {

		    // If we can't open the file, try building a
		    // directory tree to it

		    fprintf(stderr, "WARNING: %s: %s. %s.\n",
			    "Unable to open file for writing",
			    "Building directory tree",
			    data_path);
		    
		    // Try and get a parent directory from file

		    char parent_dir[MAX_PATH_LEN];
		    get_parent_dir(parent_dir, data_path);
		    
		    if (opt_verbosity > VERB_2)
			fprintf(stderr, "Using %s as parent directory.\n", parent_dir);
		    
		    // Build parent directory recursively

		    int ret = mkdir_parent(parent_dir);

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
			exit(EXIT_FAILURE);
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
		return 0;
	    }

	    // Catch corrupted headers
	    else {
		fprintf(stderr, "ERROR: Corrupt header.\n");
		exit(EXIT_FAILURE);
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

    return 0;

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
    
    return 0;

}

int run_pipe(char* pipe_cmd){
    
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

    // Create pipe and fork
    pipe(pipefd);
    pipe_pid = fork();

    // CHILD
    if (pipe_pid == 0) {

	// Redirect stdout to pipe's stdout
	if (opt_mode == MODE_SEND){
	    dup2(pipefd[0], 0);
	} 

	// Redirect stdout to pipe's stdout
	else {
	    dup2(pipefd[1], 1);
	}

	// Execute the pipe process
	if (execvp(args[0], args)){
	    fprintf(stderr, "ERROR: unable to execupte pipe process\n");
	    exit(EXIT_FAILURE);
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


    }


}

int main(int argc, char *argv[]){
    
    if (signal(SIGINT, sig_handler) == SIG_ERR){
	fprintf(stderr, "ERROR: unable to set SIGINT handler\n");
    }

    int opt;
    file_LL *fileList = NULL;
    char pipe_cmd[MAX_PATH_LEN];
    bzero(pipe_cmd, MAX_PATH_LEN);

    static struct option long_options[] =
	{
	    {"verbose", no_argument, &opt_verbosity, VERB_2},
	    {"--regular-files", no_argument, &opt_regular_files, 1},
	    {"quiet",   no_argument, &opt_verbosity, VERB_0},
	    {"help",    no_argument, 0, 'h'},
	    {"pipe",    required_argument, 0, 'u'},
	    {0, 0, 0, 0}
	};

    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "lhv:npu:", long_options, &option_index)) != -1){
	switch (opt){

	case 'p':
	    opt_progress = 1;
	    break;

	case 'l':
	    opt_mode = MODE_RCV;
	    break;

	case 'u':
	    strcpy(pipe_cmd, optarg);
	    break;
	    
	case 'n':
	    opt_recurse = 0;
	    break;

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

    // If in sender mode, test the files for typing, and build a file
    // list

    if (*pipe_cmd){
	run_pipe(pipe_cmd);
    }

    if (opt_mode == MODE_SEND){

	// Did the user pass any files?
	if (optind < argc){

	    if (opt_verbosity)
		fprintf(stderr, "Running with file source mode.\n");

	    // Clarify what we are passing as a file list
	
	    int n_files = argc-optind;
	    char **path_list = argv+optind;

	    // Generate a linked list of file objects from path list
	
	    fileList = build_filelist(n_files, path_list);
	
	    // Visit all directories and send all files

	    handle_files(fileList);

	    complete_xfer();

	} 

	// if No files were passed by user
	else {
	    fprintf(stderr, "No files specified. Did you mean to receive? [-l]\n");
	    usage(EXIT_FAILURE);
	}

    } 

    // Otherwise, switch to receiving mode

    else {

	if (opt_verbosity > VERB_1)
	    fprintf(stderr, "Running with file destination mode\n");

	// Destination directory was passed

	if (optind < argc) {

	    char* base_path = strdup(argv[optind]);
	    receive_files(base_path);

	}
 
	// No destination directory was passed, default to current dir

	else {

	    receive_files((char*)"");

	}

    }


    if (pipe_pid){

	if (opt_verbosity > VERB_1)
	    fprintf(stderr, "Killing child pipe process... ");

	sleep(END_LATENCY);

	if (kill(pipe_pid, SIGKILL)){
	    if (opt_verbosity > VERB_1) fprintf(stderr, "[FAILURE]\n");
	} else {
	    if (opt_verbosity > VERB_1) fprintf(stderr, "[SUCCESS]\n");
	}
    }
	
    return 0;
  
}
