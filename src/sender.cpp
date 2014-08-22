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
#include "files.h"
#include "util.h"
#include "postmaster.h"
#include "sender.h"

// send header specifying that the sending stream is complete

parcel_block    sender_block;

postmaster_t*    send_postmaster;
global_data_t    global_send_data;

// int allocate_block
// - allocates the block that encapsulates the header and data buffer
// - note:
//   Format of buffer:
//     [header --> sizeof(header_t)] [data --> BUFFER_LEN]
// - returns: RET_SUCCESS on success, RET_FAILURE on failure
int allocate_block(parcel_block *block) 
{

    // Calculate length of block based on optimal buffer size and
    // header length
    int alloc_len = BUFFER_LEN + sizeof(header_t);

    if ( block->buffer == NULL ) {
        block->buffer = (char*) malloc(alloc_len*sizeof(char));
    }
    
    if (!block->buffer) {
        ERR("unable to allocate data");
    }

    // record parameters in block

    block->dlen = BUFFER_LEN;
    block->data = block->buffer + sizeof(header_t);

    return RET_SUCCESS;
}


void free_block(parcel_block *block)
{
    if ( block != NULL ) {
        if ( block->buffer != NULL ) {
            free(block->buffer);
            block->buffer = NULL;
        }
        block = NULL;
    }
    
    
}

// int fill_data
// - copy a small amount of data into the buffer, this is not used
//   for data blocks
int fill_data(void* data, size_t len) 
{
    return (!!memcpy(sender_block.data, data, len));
}

// write header data to out fd
int write_header(header_t header) 
{

    // should you be using write block?

    // int ret = write(fileno(stdout), &header, sizeof(header_t));
    int ret = write(opts.send_pipe[1], &header, sizeof(header_t));

    return ret;

}

// write data block to out fd
off_t write_block(header_t header, int len) 
{

    memcpy(sender_block.buffer, &header, sizeof(header_t));

    if (len > BUFFER_LEN)
	ERR("data out of bounds");

    int send_len = len + sizeof(header_t);

    // int ret = write(fileno(stdout), block.buffer, send_len);
    int ret = write(opts.send_pipe[1], sender_block.buffer, send_len);

    if (ret < 0) {
        ERR("unable to write to send_pipe");
    }

    TOTAL_XFER += ret;

    return ret; 

}

// Notify the destination that the transfer is complete
int complete_xfer() 
{
    
    verb(VERB_2, "Signalling end of transfer.");

    // Send completition header

    header_t header;
    header.type = XFER_COMPLETE;
    header.data_len = 0;
    write_header(header);
    
    return RET_SUCCESS;

}


// sends a file to out fd by creating an appropriate header and
// sending any data
int send_file(file_object_t *file) 
{

    if (!file) {
        return -1;
    }
    
    while (!opts.socket_ready) {
        usleep(10000);
    }

    verb(VERB_2, " --- sending [%s] %s", file->filetype, file->path);

    header_t header;

    if (file->mode == S_IFDIR) {
	
        // create a header to specify that the subsequent data is a
        // directory name and send
        header = nheader(XFER_DIRNAME, strlen(file->path)+1);
        memcpy(sender_block.data, file->path, header.data_len);
        write_block(header, header.data_len);

    } else {

        int fd;
        off_t f_size;
        int o_mode = O_LARGEFILE | O_RDONLY;

        // create header to specify that subsequent data is a regular
        // filename and send
        header = nheader(XFER_FILENAME, strlen(file->path)+1);
        
        // get the mod times and set them in the header
        int tmp_mtime;
        long int tmp_mtime_nsec;
        get_mod_time(file->path, &tmp_mtime_nsec, &tmp_mtime);

        header.mtime_sec = tmp_mtime;
        header.mtime_nsec = tmp_mtime_nsec;
        
        // remove the root directory from the destination path
        char destination[MAX_PATH_LEN];
        int root_len = strlen(file->root);
        
        if (opts.full_root || !root_len || strncmp(file->path, file->root, root_len)) {
            sprintf(destination, "%s", file->path);

        } else {
            memcpy(destination, file->path+root_len+1, strlen(file->path)-root_len);
        }

        fill_data(destination, header.data_len);

        write_block(header, header.data_len);	
        
        // open file to send data blocks
        if (!( fd = open(file->path, o_mode))) {
            perror("ERROR: unable to open file");
            clean_exit(EXIT_FAILURE);
        }

        // Attempt to advise system of our intentions
        if (posix_fadvise64(fd, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE) < 0) {
            verb(VERB_3, "Unable to advise file read");
        }
     
        // Get the length of the file in advance
        if ((f_size = fsize(fd)) < 0) {
            fprintf(stderr, "Unable to determine size of file");
        }
        
        // Send length of file
        header = nheader(XFER_F_SIZE, sizeof(off_t));
        fill_data(&f_size, header.data_len);
        write_block(header, header.data_len);

        // buffer and send file
        int rs;
        off_t sent = 0;

        while ((rs = read(fd, sender_block.data, BUFFER_LEN))) {

            verb(VERB_3, "Read in %d bytes", rs);

            // Check for file read error
            if (rs < 0) {
                ERR("Error reading from file");
            }

            // create header to specify that we are also sending file data
            header = nheader(XFER_DATA, rs);
            sent += write_block(header, rs);

            // Print progress
            if (opts.progress) {
                print_progress(file->path, sent, f_size);
            }
        }

        // Carriage return for  progress printing
        if (opts.progress) {
            fprintf(stderr, "\n");
        }

        // Done with fd
        close(fd);
        
        // fly - tell the other side we're done with the file
        header_t header;
        header.type = XFER_DATA_COMPLETE;
        header.data_len = 0;
        write_header(header);
    }

    return RET_SUCCESS;
    
}

int send_filelist(file_LL* fileList, int totalSize)
{
    header_t header;
    verb(VERB_2, "send_filelist: sending file list of size %d", totalSize);
//    totalSize = get_filelist_size(fileList);
    header = nheader(XFER_FILELIST, totalSize);
    
    if ( sender_block.data != NULL ) {
        char* tmp_file_list = pack_filelist(fileList, header.data_len);
        // fly - this is stupid, I should do this in one step
        memcpy(sender_block.data, tmp_file_list, header.data_len);
        free(tmp_file_list);
        write_block(header, header.data_len);
    } else {
        ERR("send_filelist: unable to copy to sender_block.data, value NULL");        
    }
    
    return RET_SUCCESS;
}


file_LL* send_and_wait_for_filelist(file_LL* fileList)
{
    header_t header;
    int total_size = get_filelist_size(fileList);

    while (!opts.socket_ready) {
        usleep(10000);
    }

    int alloc_len = BUFFER_LEN - sizeof(header_t);
    global_send_data.data = (char*) malloc( alloc_len * sizeof(char));
    
    send_filelist(fileList, total_size);
    
    // Read in headers and data until signalled completion
    while ( !global_send_data.complete ) {

        // pop the file list into the data packet so the callback can
        // adjust it based on the list returned
        global_send_data.user_data = (void*)fileList;
        if (global_send_data.read_new_header) {
            if ((global_send_data.rs = read_header(&header)) <= 0) {
                ERR("Bad header read");
            }
        }
        
        if (global_send_data.rs) {
            verb(VERB_3, "Dispatching message to sender: %d", header.type);
            dispatch_message(send_postmaster, header, &global_send_data);
        }
        
    }

    // free up the memory on the way out
    free(global_send_data.data);

    return ((file_LL*)global_send_data.user_data);
}


// main loop for send mode, takes a linked list of files and streams
// them

int handle_files(file_LL* fileList, file_LL* remote_fileList)
{
    
    if ( ((fileList != NULL) && (remote_fileList != NULL)) && (fileList->count == remote_fileList->count) ) {
    //    allocate_block(&block);
        file_node_t* cursor = fileList->head;
        file_node_t* remote_cursor = remote_fileList->head;
        
        
        // Send each file or directory
        while (cursor) {

            file_object_t *file = cursor->curr;
            file_object_t *remote_file = remote_cursor->curr;

            // While there is a directory, opts.recurse?
            if (file->mode == S_IFDIR) {

                // Tell desination to create a directory 
                if (opts.full_root) {
                    send_file(file);
                }
            } 

            // if it is a regular file, then send it
            else if (file->mode == S_IFREG) {

                if (is_in_checkpoint(file)) {
                    char*status = "completed";
                    verb(VERB_1, "Logged: %s [%s]", file->path, status);
                } else {
                    if ( compare_timestamps(file, remote_file) ) {
                        send_file(file);
                    }
                }

            } 

            // If the file is a character device or a named pipe, warn user
            else if (file->mode == S_IFCHR || file->mode == S_IFIFO) {

                if (opts.regular_files) {
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

                if (opts.verbosity > VERB_0) {
                    warn("File %s is a %s", file->path, file->filetype);
                    ERR("This filetype is not currently supported.");
                }

            }

            log_completed_file(file);
            cursor = cursor->next;
            remote_cursor = remote_cursor->next;
        }

        close_log_file();
    } else {
        
        if ( (fileList == NULL) || (remote_fileList == NULL) ) {
            ERR("Bad file list pointers passed");
        } else {
            ERR("Unequal file list counts: local = %d, remote = %d", fileList->count, remote_fileList->count);
        }
    }
    
    return RET_SUCCESS;
}

//
// pst_snd_callback_filelist
//
// routine to handle XFER_FILELIST message
//
int pst_snd_callback_filelist(header_t header, global_data_t* global_data)
{
    
    // all we need to do is unpack data and stuff into global_data
    char* tmp_file_list = (char*)malloc(sizeof(char) * header.data_len);
    
    read_data(tmp_file_list, header.data_len);
    file_LL* fileList = unpack_filelist(tmp_file_list, header.data_len);
    free(tmp_file_list);
    
    global_data->user_data = (void*)fileList;
    global_data->complete = 1;
    
    return 0;
}

void init_sender()
{
    
    allocate_block(&sender_block);

    // initialize the data
    global_send_data.f_size = 0;
    global_send_data.complete = 0;
    global_send_data.expecting_data = 0;
    global_send_data.read_new_header = 1;
    
    // create the postmaster
    send_postmaster = create_postmaster();

    // register the callbacks
    register_callback(send_postmaster, XFER_FILELIST, pst_snd_callback_filelist);
    
}


void cleanup_sender()
{
    if ( send_postmaster != NULL ) {
        free(send_postmaster);
    }
    free_block(&sender_block);
}

