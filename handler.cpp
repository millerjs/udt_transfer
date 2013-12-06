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

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <getopt.h>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>

using std::cerr;
using std::endl;

#include "handler.h"
#include "files.h"

// Global settings
int opt_verbosity = 0;
int opt_recurse = 1;

void usage(){
    exit(1);
}

static enum{
    SEND_DATA,
    SEND_FILENAME,
    SEND_DIRNAME
} handler_commands;


int handle_files(file_LL* fileList){

    // Send each file or directory
    while (fileList){

	file_object_t *file = fileList->curr;
	fprintf(stderr, "Sending file %-30s[%s]\n", file->path, file->filetype);
	
	// While there is a directory, opt_recurse?
	if (file->mode == S_IFDIR){
	    
	    // Recursively enter directory
	    if (opt_recurse){

		if (opt_verbosity > 0)
		    fprintf(stderr, "Entering directory %s\n", file->path);
		
		file_LL* internal_fileList = lsdir(file);

		// if directory is non-empty then recurse 
		if (internal_fileList){
		    fprintf(stderr, "recursing...\n");
		    handle_files(internal_fileList);
		    
		}

	    }


	}


	fileList = fileList->next;

    }

}


int main(int argc, char *argv[]){
    
    int opt;
    file_LL *fileList = NULL;

    while ((opt = getopt (argc, argv, "lhv")) != -1){
	switch (opt){

	case 'r':
	    opt_recurse = 1;
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

	fprintf(stderr, "Running as file source\n");

	// Clarify what we are passing as a file list
	int n_files = argc-optind;
	char **path_list = argv+optind;

	// Generate a linked list of file objects from path list
	fileList = build_filelist(n_files, path_list);

    } // Otherwise, switch to receiving mode
    else {

	fprintf(stderr, "Running as file  destination\n");
	exit(1);

    }

    handle_files(fileList);

  
}
