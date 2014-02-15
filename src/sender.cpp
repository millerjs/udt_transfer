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

ucp_block block;

// int allocate_block
// - allocates the block that encapsulates the header and data buffer
// - note:
//   Format of buffer:
//     [header --> sizeof(header_t)] [data --> BUFFER_LEN]
// - returns: RET_SUCCESS on success, RET_FAILURE on failure
int allocate_block(ucp_block *block){

    // Calculate length of block based on optimal buffer size and
    // header length
    int alloc_len = BUFFER_LEN + sizeof(header_t);

    block->buffer = (char*) malloc(alloc_len*sizeof(char));

    if (!block->buffer)
	error("unable to allocate data");

    // record parameters in block

    block->dlen = BUFFER_LEN;
    block->data = block->buffer + sizeof(header_t);

    return RET_SUCCESS;
}

// int fill_data
// - copy a small amount of data into the buffer, this is not used
//   for data blocks
int fill_data(void* data, size_t len){
    
    return (!!memcpy(block.data, data, len));
    
}

// write header data to out fd
int write_header(header_t header){

    // should you be using write block?

    // int ret = write(fileno(stdout), &header, sizeof(header_t));
    int ret = write(opts.send_pipe[1], &header, sizeof(header_t));

    return ret;

}

// write data block to out fd
off_t write_block(header_t header, int len){

    memcpy(block.buffer, &header, sizeof(header_t));

    if (len > BUFFER_LEN)
	error("data out of bounds");

    int send_len = len + sizeof(header_t);

    // int ret = write(fileno(stdout), block.buffer, send_len);
    int ret = write(opts.send_pipe[1], block.buffer, send_len);

    fprintf(stderr, "Wrote %d bytes\n", ret);

    if (ret < 0){
	error("unable to write to send_pipe");
    }

    TOTAL_XFER += ret;

    return ret; 

}

// Notify the destination that the transfer is complete
int complete_xfer(){
    
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

    if (!file) return -1;

    while (!opts.socket_ready)
        usleep(10000);

    verb(VERB_2, " --- sending [%s] %s", file->filetype, file->path);

    header_t header;

    if (file->mode == S_IFDIR){
	
	// create a header to specify that the subsequent data is a
	// directory name and send
	header = nheader(XFER_DIRNAME, strlen(file->path)+1);
	memcpy(block.data, file->path, header.data_len);
	write_block(header, header.data_len);

    }

    else {

	int fd;
	off_t f_size;
	int o_mode = O_LARGEFILE | O_RDONLY;
	// int o_mode = O_RDONLY;

	// create header to specify that subsequent data is a regular
	// filename and send

	header = nheader(XFER_FILENAME, strlen(file->path)+1);

	// remove the root directory from the destination path

	char destination[MAX_PATH_LEN];
	int root_len = strlen(file->root);
	
	if (opts.full_root || !root_len || strncmp(file->path, file->root, root_len)){

	    sprintf(destination, "%s", file->path);

	} else {
	    memcpy(destination, file->path+root_len+1, 
		   strlen(file->path)-root_len);
	}


	fill_data(destination, header.data_len);

	write_block(header, header.data_len);	
	
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
	    fprintf(stderr, "Unable to determine size of file");
	}
	
	// Send length of file
	
	header = nheader(XFER_F_SIZE, sizeof(off_t));
	fill_data(&f_size, header.data_len);
	write_block(header, header.data_len);

	// buffer and send file
	
	int rs;
	off_t sent = 0;
	

	while ((rs = read(fd, block.data, BUFFER_LEN))){

	    verb(VERB_3, "Read in %d bytes", rs);

	    // Check for file read error

	    if (rs < 0)
		error("Error reading from file");

	    // create header to specify that we are also sending file data

	    header = nheader(XFER_DATA, rs);
	    sent += write_block(header, rs);

	    // Print progress
	    
	    if (opts.progress)
		print_progress(file->path, sent, f_size);

	}

	// Carriage return for  progress printing

	if (opts.progress) 
	    fprintf(stderr, "\n");

	// Done with fd

	close(fd);

    }

    return RET_SUCCESS;
    
}


// main loop for send mode, takes a linked list of files and streams
// them

int handle_files(file_LL* fileList){

    allocate_block(&block);
        
    // Send each file or directory
    while (fileList){

	file_object_t *file = fileList->curr;

	// While there is a directory, opts.recurse?
	if (file->mode == S_IFDIR){


	    // Tell desination to create a directory 

	    if (opts.full_root)
		send_file(file);

	    // Recursively enter directory
	    if (opts.recurse){

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

	    if (opts.regular_files){

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

	    if (opts.verbosity > VERB_0){
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
