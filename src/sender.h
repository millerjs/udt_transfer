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

#ifndef SENDER_H
#define SENDER_H

// initializes everything needed for sender
void init_sender();

// cleanup everything for sender
void cleanup_sender();

int get_parent_dir(char parent_dir[MAX_PATH_LEN], char path[MAX_PATH_LEN]);

// sends a file to out fd by creating an appropriate header and
// sending any data

int send_file(file_object_t *file);

// sends a file list across the wire

int send_filelist(file_LL* fileList, int totalSize);

// main loop for send mode, takes a linked list of files and streams
// them

int handle_files(file_LL* fileList, file_LL* remote_fileList);

// sends a list of files to the receiver and waits for a response

file_LL* send_and_wait_for_filelist(file_LL* fileList);

// send header specifying that the sending stream is complete

int complete_xfer();

// send header & wait for ack

void send_and_wait_for_ack_of_complete();

// write header data to out fd

int write_header(header_t header);


int fill_data(void* data, size_t len);

// write data block to out fd

off_t write_block(header_t header, int len);


#endif
