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
#include "files.h"

// send header specifying that the sending stream is complete

int complete_xfer(){
    
    // Notify the destination that the transfer is complete
    verb(VERB_2, "Signalling end of transfer.");

    // Send completition header

    header_t header;
    header.type = XFER_COMPLTE;
    header.data_len = 0;
    write_header(header);
    
    return RET_SUCCESS;

}

// sends a file to out fd by creating an appropriate header and
// sending any data

int send_file(file_object_t *file){

    char* data = _data + HEADER_LEN;

    if (!file) return -1;

    verb(VERB_2, " --- sending [%s] %s", file->filetype, file->path);

    header_t header;

    if (file->mode == S_IFDIR){

	// create a header to specify that the subsequent data is a
	// directory name and send
	header = nheader(XFER_DIRNAME, strlen(file->path)+1);
	memcpy(data, file->path, header.data_len);
	write_data(header, data, header.data_len);

    }

    else {

	int fd;
	off_t f_size;
	int o_mode = O_LARGEFILE | O_RDONLY;

	// create header to specify that subsequent data is a regular
	// filename and send

	header = nheader(XFER_FILENAME, strlen(file->path)+1);
	memcpy(data, file->path, header.data_len);
	write_data(header, data, header.data_len);	
	// write_data(header, file->path, header.data_len);

	// open file to send data blocks

	if (!( fd = open(file->path, o_mode))){
	    perror("ERROR: unable to open file");
	    clean_exit(EXIT_FAILURE);
	}

	// Attempt to advise system of our intentions

	if (posix_fadvise64(fd, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE) < 0){
	    verb(VERB_3, "Unable to advise file read");
	}
 
	// Get the length of the file in advance
	if ((f_size = fsize(fd)) < 0){
	    error("Unable to determine size of file");
	}

	// Send length of file

	header = nheader(XFER_F_SIZE, sizeof(f_size));
	write_data(header, &f_size, header.data_len);

	// buffer and send file
	
	int rs;
	off_t sent = 0;
	
	while ((rs = read(fd, data, BUFFER_LEN))){
	// while ((rs = read(fd, data, BUFFER_LEN))){

	    verb(VERB_3, "Read in %d bytes", rs);

	    // Check for file read error

	    if (rs < 0)
		error("Error reading from file");

	    // create header to specify that we are also sending file data

	    header = nheader(XFER_DATA, rs);
	    sent += write_data(header, data, rs);

	    // Print progress
	    
	    if (opt_progress)
		print_progress(file->path, sent, f_size);

	}

	// Carriage return for  progress printing

	if (opt_progress) 
	    fprintf(stderr, "\n");

	// Done with fd

	close(fd);

    }
    
}


int dump_fileList(int fd, file_LL* fileList){

    char path[MAX_PATH_LEN];
    while (fileList){

	file_object_t *file = fileList->curr;

	sprintf(path, "%s\n", file->path);
	write(fd, path, strlen(path));
	
	// While there is a directory, opt_recurse?

	if (file->mode == S_IFDIR){
	    
	    // Recursively enter directory
	    if (opt_recurse){

		// Get a linked list of all the files in the directory 
		file_LL* internal_fileList = lsdir(file);

		// if directory is non-empty then recurse 
		if (internal_fileList){
		    dump_fileList(fd, internal_fileList);
		}

	    }

	} 

	fileList = fileList->next;

    }

    return RET_SUCCESS;


}


// main loop for send mode, takes a linked list of files and streams
// them

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

		verb(VERB_2, "> Entering [%s]  %s", file->filetype, file->path);

		// Get a linked list of all the files in the directory 
		file_LL* internal_fileList = lsdir(file);

		// if directory is non-empty then recurse 
		if (internal_fileList){
		    handle_files(internal_fileList);
		}
	    }

	    // If user chose not to recurse into directories
	    else {
		verb(VERB_1, " --- SKIPPING [%s]  %s", file->filetype, file->path);
	    }

	} 

	// if it is a regular file, then send it
	else if (file->mode == S_IFREG){

	    if (is_in_checkpoint(file)){
		char*status = "completed";
		verb(VERB_1, "Logged: %s [%s]", file->path, status);
	    } else {

		send_file(file);

	    }


	} 

	// If the file is a character device or a named pipe, warn user
	else if (file->mode == S_IFCHR || file->mode == S_IFIFO){

	    if (opt_regular_files){

		warn("Skipping [%s] %s.\n%s.", file->filetype, file->path, 
		     "To enable sending character devices, use --all-files");

	    } else {

		warn("sending %s [%s].\nTo prevent sending %ss, remove the -all-files flag.",
		     file->path, file->filetype, file->filetype);

		
		send_file(file);

	    }
	    
	}

	// if it's neither a regular file nor a directory, leave it
	// for now, maybe send in later version
	else {

	    verb(VERB_2, "   > SKIPPING [%s] %s", file->filetype, file->path);

	    if (opt_verbosity > VERB_0){
		warn("File %s is a %s", file->path, file->filetype);
		error("This filetype is not currently supported.");
	    }

	}

	log_completed_file(file);

	fileList = fileList->next;

    }

    close_log_file();

    return RET_SUCCESS;

}
