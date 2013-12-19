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

#include <cstdarg>
#include <iostream>
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

/* The buffer len is calculated as the optimal udt block - block
   header len = 67108864 - 16 */

#define BUFFER_LEN 67108848

#define MAX_ARGS 128

#define END_LATENCY 2

#define RET_FAILURE -1
#define RET_SUCCESS 0
#define EXIT_SUCCESS 0

#define SIZE_B  8e0
#define SIZE_KB 1024.
#define SIZE_MB 1.0e6
#define SIZE_GB 1.0e9
#define SIZE_TB 1.0e12
#define SIZE_PB 1.0e15

#define LABEL_B  "B"
#define LABEL_KB "KB"
#define LABEL_MB "MB"
#define LABEL_GB "GB"
#define LABEL_TB "TB"
#define LABEL_PB "PB"


// Global variables

extern int timer;
extern off_t TOTAL_XFER;
extern int opt_verbosity;

typedef enum{
    XFER_DATA,
    XFER_FILENAME,
    XFER_DIRNAME,
    XFER_F_SIZE,
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

typedef struct ucp_block{
    char*buffer;
    char*data;
    int dlen;
} ucp_block;


typedef struct ucp_opt_t{
    int verbosity;
    int recurse;
    int mode;
    int progress;
    int regular_files;
    int default_udpipe;
    int remote;
    int delay;
    int log;
    int restart;
    int mmap;
    int full_root;

    char restart_path[MAX_PATH_LEN];

} ucp_opts_t;

typedef struct remote_arg_t{

    int pipe_pid;
    int ssh_pid;
    int remote_pid;

    /* Global path variables */
    char remote_dest[MAX_PATH_LEN];
    char pipe_port[MAX_PATH_LEN];
    char pipe_host[MAX_PATH_LEN];
    char udpipe_location[MAX_PATH_LEN];

    char pipe_cmd[MAX_PATH_LEN];
    char xfer_cmd[MAX_PATH_LEN];



} remote_arg_t;

extern remote_arg_t remote_args;
extern ucp_opt_t opts;


void usage(int EXIT_STAT);

void prii(char* str, int i);

void msg(char* str);

void warn(char* fmt, ...);

void verb(int verbosity, char* fmt, ...);

void error(char* fmt, ...);

void print_bytes(const void *object, size_t size);

// Make sure that we have killed any zombie or orphaned children

int kill_children(int verbosity);

double get_scale(off_t size, char*label);

// Print time and average trasnfer rate

void print_xfer_stats();

// Wrapper for exit() with call to kill_children()

void clean_exit(int status);

// Handle SIGINT by exiting cleanly

void sig_handler(int signal);

// display the transfer progress of the current file

int print_progress(char* descrip, off_t read, off_t total);

header_t nheader(xfer_t type, off_t size);

// wrapper for read

off_t read_data(void* b, int len);

int read_header(header_t *header);

// step backwards down a given directory path

int get_parent_dir(char parent_dir[MAX_PATH_LEN], char path[MAX_PATH_LEN]);

// create the command used to execvp a pipe i.e. udpipe

int generate_pipe_cmd(char*pipe_cmd, int pipe_mode);

// execute a pipe process i.e. udpipe and redirect ucp output

int run_pipe(char* pipe_cmd);

// run the ssh command that will create a remote ucp process

int run_ssh_command(char *remote_dest);


#endif
