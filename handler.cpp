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
int opt_verbosity = 0;
int opt_recurse = 1;
int opt_mode = MODE_SEND;

char data[BUFFER_LEN];
char path_buff[BUFFER_LEN];

void usage(){
    exit(1);
}

int write_header(header_t header){
    return write(fileno(stdout), &header, sizeof(header_t));
}

int write_data(char*data, int len){
    return write(fileno(stdout), data, len);
}

int send_file(file_object_t *file){

    if (!file) return -1;

    if (opt_verbosity)
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
	    
	    fprintf(stderr, "\n\nwriting data\n");
	    write_data(data, rs);

	}

    }
    
}


int read_data(void* b, int len){

    int rs = 0;
    char* buffer = (char*)b;
    
    while (rs < len){
	rs = read(fileno(stdin), buffer+rs, len - rs);
    }

    return rs;

}

int receive_files(char*base_path){

    int complete = 0;
    int rs, ds;
    
    char data_path[MAX_PATH_LEN];
    int expecting_data = 0;

    // default permissions for creating new directories

    int mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;

    // generate a base path for all destination files    

    int bl = strlen(base_path);
    if (base_path[bl] == '/') bl++;
    sprintf(data_path, "%s/", base_path);
    
    // Read in headers and data until signalled completion

    while (!complete){
	
	header_t header, data_header;
	rs = read_data(&header, sizeof(header_t));
	
	fprintf(stderr, "type: %d\n", header.type);

	if (rs){

	    // We are receiving a directory name

	    if (header.type == XFER_DIRNAME){

		read_data(data_path+bl, header.data_len);
		
		if (opt_verbosity)
		    fprintf(stderr, "making directory: %s\n", data_path);
	    
		// safety reset, data block after this will fault
		expecting_data = 0;

	    } 

	    // We are receiving a file name

	    else if (header.type == XFER_FILENAME) {
		
		read_data(data_path+bl, header.data_len);

		if (opt_verbosity)
		    fprintf(stderr, "Initializing file receive: %s\n", data_path);

		// the block following is expected to be data
		expecting_data = 1;

	    }

	    // We are receiving a data chunk

	    else if (header.type == XFER_DATA) {

		if (data_path[0] == '\0'){
		    fprintf(stderr, "ERROR: Out of order data block.\n");
		    exit(EXIT_FAILURE);
		}

		int rs, len, total = 0;
		
		while (total < header.data_len){

		    len = (BUFFER_LEN < header.data_len) ? BUFFER_LEN : header.data_len;
		    rs = read_data(data, len);
		    
		    if (rs < 0){
			perror("ERROR: Unable to read file");
			exit(EXIT_FAILURE);
		    }

		    fprintf(stderr, "writing data: %s\n", data_path);
		    fprintf(stderr, "        data: %s\n", data);

		    total += rs;

		}


		// another data block is not expected
		expecting_data = 0;

	    }

	    // Or maybe the transfer is complete

	    else if (header.type == XFER_COMPLTE){
		if (opt_verbosity)
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

		if (opt_verbosity)
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
		if (opt_verbosity)
		    fprintf(stderr, "> SKIPPING [%s]  %s\n", 
			    file->filetype, file->path);
	    }

	} 

	// if it is a regular file, then send it
	else if (file->mode == S_IFREG){
	    send_file(file);
	} 

	// if it's neither a regular file nor a directory, leave it
	// for now, maybe send in later version
	else {

	    if (opt_verbosity)
		fprintf(stderr, "   > SKIPPING [%s]  \%s\n", 
			file->filetype, file->path);

	    fprintf(stderr, "WARNING: file %s is a %s, ", 
		    file->path, file->filetype);
	    fprintf(stderr, "ucp currently supports only regular files and directories.\n");

	}

	fileList = fileList->next;

    }

    return 0;

}

int complete_xfer(){
    
    // Notify the destination that the transfer is complete
    header_t header;
    header.type = XFER_COMPLTE;
    header.data_len = 0;
    write_header(header);
    
    return 0;

}


int main(int argc, char *argv[]){
    
    int opt;
    file_LL *fileList = NULL;

    while ((opt = getopt (argc, argv, "lhvn")) != -1){
	switch (opt){

	case 'l':
	    opt_mode = MODE_RCV;
	    break;
	    
	case 'n':
	    opt_recurse = 0;
	    break;

	case 'v':
	    opt_verbosity = 1;
	    break;
	    
	case 'h':
	    usage();
	    break;

	default:
	    fprintf(stderr, "Unknown command line option.\n");
	    usage();
	    exit(1);

	}
    }

    // If in sender mode, test the files for typing, and build a file
    // list

    if (opt_mode == MODE_SEND){

	// Did the user pass any files?
	if (optind < argc){

	    if (opt_verbosity)
		fprintf(stderr, "Running as file source\n");

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
	    usage();
	}

    } 

    // Otherwise, switch to receiving mode

    else {

	if (opt_verbosity)
	    fprintf(stderr, "Running as file destination\n");

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


    return 0;
  
}
