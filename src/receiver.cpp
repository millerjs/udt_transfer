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

#include "util.h"
#include "parcel.h"
#include "files.h"
#include "receiver.h"

// main loop for receiving mode, listens for headers and sorts out
// stream into files

int fout;
off_t total, rs, ds, f_size = 0;
int complete = 0;
int expecting_data = 0;
int read_new_header = 1;
char data_path[MAX_PATH_LEN];
int mtime_sec;
long int mtime_nsec;


int read_header(header_t *header) 
{

    // return read(fileno(stdin), header, sizeof(header_t));
    return read(opts.recv_pipe[0], header, sizeof(header_t));

}

// wrapper for read
off_t read_data(void* b, int len) 
{

    off_t rs, total = 0;
    char* buffer = (char*)b;
    
    while (total < len) {
        // rs = read(fileno(stdin), buffer+total, len - total);
        rs = read(opts.recv_pipe[0], buffer+total, len - total);
        total += rs;
        TOTAL_XFER += rs;
    }

    verb(VERB_4, "Read %d bytes from stream", total);

    return total;

}


int receive_files(char*base_path) 
{

    header_t header;

    while (!opts.socket_ready) {
        usleep(10000);
    }

    int alloc_len = BUFFER_LEN - sizeof(header_t);
    char* data = (char*) malloc( alloc_len * sizeof(char));

    // generate a base path for all destination files and get the
    // length
    int bl = generate_base_path(base_path, data_path);
    
    // Read in headers and data until signalled completion
    while (!complete) {

        if (read_new_header) {
            if ((rs = read_header(&header)) <= 0) {
                ERR("Bad header read");
            }
        }
        
        if (rs) {
            // We are receiving a directory name
            if (header.type == XFER_DIRNAME) {

                verb(VERB_4, "Received directory header");
                
                // Read directory name from stream
                read_data(data_path+bl, header.data_len);

                if (opts.verbosity > VERB_1) {
                    fprintf(stderr, "making directory: %s\n", data_path);
                }
                
                // make directory, if any parent in directory path
                // doesnt exist, make that as well
                mkdir_parent(data_path);

                // safety reset, data block after this will fault, expect a header
                expecting_data = 0;
                read_new_header = 1;

            // We are receiving a file name
            } else if (header.type == XFER_FILENAME) {

                verb(VERB_4, "Received file header");
                
                // int f_mode = O_CREAT| O_WRONLY;
                int f_mode = O_CREAT| O_RDWR;
                int f_perm = 0666;

                // hang on to mtime data until we're done
                mtime_sec = header.mtime_sec;
                mtime_nsec = header.mtime_nsec;
                verb(VERB_2, "Header mtime: %d, mtime_nsec: %ld\n", mtime_sec, mtime_nsec);
                
                // Read filename from stream
                read_data(data_path+bl, header.data_len);

                verb(VERB_2, "Initializing file receive: %s\n", data_path+bl);


                fout = open(data_path, f_mode, f_perm);

                if (fout < 0) {

                    // If we can't open the file, try building a
                    // directory tree to it
                    
                    // Try and get a parent directory from file
                    char parent_dir[MAX_PATH_LEN];
                    get_parent_dir(parent_dir, data_path);
                    
                    if (opts.verbosity > VERB_2) {
                        fprintf(stderr, "Using %s as parent directory.\n", parent_dir);
                    }
                    
                    // Build parent directory recursively
                    if (mkdir_parent(parent_dir) < 0) {
                        perror("ERROR: recursive directory build failed");
                    }

                }

                // If we had to build the directory path then retry file open
                if (fout < 0) {
                    fout = open(data_path, f_mode, 0666);
                }

                if (fout < 0) {
                    fprintf(stderr, "ERROR: %s ", data_path);
                    perror("file open");
                    clean_exit(EXIT_FAILURE);
                }

                // Attempt to optimize simple sequential write
                if (posix_fadvise64(fout, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE)) {
                    if (opts.verbosity > VERB_3) {
                        perror("WARNING: Unable to advise file write");
                    }
                }		

                read_new_header = 1;
                expecting_data = 1;
                total = 0;

            // We are receiving the size of the next file
            } else if (header.type == XFER_F_SIZE) {
            
                // read in the size of the file
                read_data(&f_size, header.data_len);

                // Memory map attempt
                if (opts.mmap) {
                    map_fd(fout, f_size);
                }
            
            // We are receiving a data chunk
            } else if (header.type == XFER_DATA) {

                off_t rs, len;
                
                if (!expecting_data) {
                    fprintf(stderr, "ERROR: Out of order data block.\n");
                    clean_exit(EXIT_FAILURE);
                }

                while (header.type == XFER_DATA) {

                    // Either look to receive a whole buffer of
                    // however much remains in the data block
                    len = (BUFFER_LEN < f_size-total) ? BUFFER_LEN : f_size-total;

                    // read data buffer from stdin
                    // use the memory map
                    if (opts.mmap) {
                        if ((rs = read_data(f_map+total, len)) < 0) {
                            ERR("Unable to read stdin");
                        }

                    } else {
                        if ((rs = read_data(data, len)) < 0) {
                            ERR("Unable to read stdin");
                        }

                        // Write to file
                        if ((write(fout, data, rs) < 0)) {
                            perror("ERROR: unable to write to file");
                            clean_exit(EXIT_FAILURE);
                        }
                    }

                    total += rs;

                    read_header(&header);

                    // Update user on progress if opts.progress set to true		    
                    if (opts.progress) {
                        print_progress(data_path, total, f_size);
                    }

                }

                // Formatting
                if (opts.progress) {
                    fprintf(stderr, "\n");
                }

                // Check to see if we received full file
                if (f_size) {
                    if (total == f_size) {
                        verb(VERB_3, "Received full file [%li B]", total);
                    } else {
                        warn("Did not receive full file: %s", data_path);
                    }

                } else {
                    warn("Completed stream of known size");
                }

                if (ftruncate64(fout, f_size)) {
                    ERR("unable to truncate file to correct size");
                }

                // On the next loop, use the header that was just read in
                read_new_header = 0;
                expecting_data = 0;
                f_size = 0;

                // Truncate the file in case it already exists and remove extra data
                if (opts.mmap) {
                    unmap_fd(fout, f_size);
                }

                close(fout);
                
                // fly - now is the time when we set the timestamps
                set_mod_time(data_path, mtime_nsec, mtime_sec);

            // Or maybe the transfer is complete
            } else if (header.type == XFER_COMPLETE) {
                if (opts.verbosity > VERB_1) {
                    fprintf(stderr, "Receive completed.\n");
                }
                
                complete = 1;
                // return RET_SUCCESS;
            }

            // Catch corrupted headers
            else {
                fprintf(stderr, "ERROR: Corrupt header %d.\n", header.type);
                clean_exit(EXIT_FAILURE);
            }

        }	
    }
    
    return RET_SUCCESS;

}
