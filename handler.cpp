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

char data[BUFFER_LEN];

void usage(){
    exit(1);
}

int write_header(header_t header){
    write(fileno(stdout), &header, sizeof(header_t));
}

int write_data(char*data, int len){
    write(fileno(stdout), data, len);
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
	int rs = fread(data, sizeof(char), BUFFER_LEN, fp);
	if (rs < 0){
	    perror("ERROR: error reading from file");
	    exit(EXIT_FAILURE);
	}
	write_data(data, rs);

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

}

int complete_xfer(){
    
    // Notify the destination that the transfer is complete
    header_t header;
    header.type = XFER_COMPLTE;
    header.data_len = 0;
    write_header(header);

}


int main(int argc, char *argv[]){
    
    int opt;
    file_LL *fileList = NULL;

    while ((opt = getopt (argc, argv, "lhvn")) != -1){
	switch (opt){

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

    // If we were passed more options, switch to sender mode, test the
    // files for typing, and build a file list

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

    } // Otherwise, switch to receiving mode
    else {

	if (opt_verbosity)
	    fprintf(stderr, "Running as file  destination\n");

	exit(1);

    }


  
}
