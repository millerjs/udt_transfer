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

#ifndef UCP_H
#define UCP_H

/* #define _LARGE_FILES */
#define _FILE_OFFSET_BITS  64

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <getopt.h>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>

#include "files.h"

#define BUFFER_LEN 67108864
#define MAX_ARGS 128

#define END_LATENCY 2

#define RET_FAILURE -1
#define RET_SUCCESS 0
#define EXIT_SUCCESS 0

#define SIZE_B  8LL
#define SIZE_KB 1024LL
#define SIZE_MB 1048576LL
#define SIZE_GB 1073741824LL
#define SIZE_TB 1099511627776LL

#define LABEL_B  "B"
#define LABEL_KB "KB"
#define LABEL_MB "MB"
#define LABEL_GB "GB"
#define LABEL_TB "TB"

typedef enum{
    XFER_DATA,
    XFER_FILENAME,
    XFER_DIRNAME,
    XFER_COMPLTE
} xfer_t;

/* 
Levels of Verbosity:
 VERB_0: Withhold WARNING messages 
 VERB_1: Update user on file transfers
 VERB_2: Update user on underlying processes, i.e. directory creation 
 VERB_3: Unassigned 
 VERB_4: Unassigned 
*/

typedef enum{
    VERB_0,
    VERB_1,
    VERB_2,
    VERB_3,
    VERB_4
} verb_t;


typedef enum{
    MODE_SEND,
    MODE_RCV
} xfer_mode_t;

typedef struct header{
    xfer_t type;
    off_t data_len;
} header_t;


void usage(int EXIT_STAT);

// Make sure that we have killed any zombie or orphaned children

int kill_children(int verbosity);

off_t get_scale(off_t size, char*label);

// Print time and average trasnfer rate

void print_xfer_stats();

// Wrapper for exit() with call to kill_children()

void clean_exit(int status);

// Handle SIGINT by exiting cleanly

void sig_handler(int signal);

// write header data to out fd

int write_header(header_t header);

// write data block to out fd

off_t write_data(header_t header, char*data, off_t len);

// display the transfer progress of the current file

int print_progress(char* descrip, off_t read, off_t total);


off_t fsize(int fd);

header_t nheader(xfer_t type, off_t size);

// sends a file to out fd by creating an appropriate header and
// sending any data

int send_file(file_object_t *file);

// wrapper for read

off_t read_data(void* b, int len);

int read_header(header_t *header);

// step backwards down a given directory path

int get_parent_dir(char parent_dir[MAX_PATH_LEN], char path[MAX_PATH_LEN]);


// main loop for receiving mode, listens for headers and sorts out
// stream into files

int receive_files(char*base_path);

// main loop for send mode, takes a linked list of files and streams
// them

int handle_files(file_LL* fileList);

// send header specifying that the sending stream is complete

int complete_xfer();

// create the command used to execvp a pipe i.e. udpipe

int generate_pipe_cmd(char*pipe_cmd, int pipe_mode);

// execute a pipe process i.e. udpipe and redirect ucp output

int run_pipe(char* pipe_cmd);

// run the ssh command that will create a remote ucp process

int run_ssh_command(char *remote_dest);


// Global settings
extern int opt_verbosity;
extern int opt_recurse;
extern int opt_mode;
extern int opt_progress;
extern int opt_regular_files;
extern int opt_default_udpipe;
extern int opt_auto;
extern int opt_delay;

// The global variables for remote connection
extern int pipe_pid;
extern int ssh_pid;
extern int remote_pid;
extern char remote_dest[MAX_PATH_LEN];
extern char pipe_port[MAX_PATH_LEN];
extern char pipe_host[MAX_PATH_LEN];
extern char udpipe_location[MAX_PATH_LEN];

// Statistics globals
extern int timer;
extern off_t TOTAL_XFER;

// Buffers
extern char data[BUFFER_LEN];
extern char path_buff[BUFFER_LEN];

#endif
