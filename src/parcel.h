/*****************************************************************************
Copyright 2013 Laboratory for Advanced Computing at the University of Chicago

This file is part of parcel

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

#ifndef PARCEL_H
#define PARCEL_H

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
#include <inttypes.h>

#include "files.h"
#include "crypto.h"
#include "debug_output.h"

/* The buffer len is calculated as the optimal udt block - block
   header len = 67108864 - 16 */

#define BUFFER_LEN 67108848

#define MAX_ARGS 128

#define END_LATENCY 2000

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

// Undefine this to have a backtrace spit out on segfault
#define DEBUG_BACKTRACE     1

// Global variables

extern int g_timer;
extern off_t G_TOTAL_XFER;
extern int g_opt_verbosity;

typedef enum : uint8_t {
	XFER_DATA,				// 0
	XFER_FILENAME,			// 1
	XFER_FIFO,				// 2
	XFER_DIRNAME,			// 3
	XFER_F_SIZE,			// 4
	XFER_COMPLETE,			// 5
	XFER_WAIT,				// 6
	XFER_DATA_COMPLETE,		// 7
	XFER_FILELIST,			// 8
	XFER_CONTROL,			// 9
	NUM_XFER_CMDS
} xfer_t;


typedef enum : uint8_t {
	CTRL_ACK,				// 0
	CTRL_RECV_READY,		// 1
	CTRL_RECEIVED,			// 2
	NUM_CTRL_MSGS
} ctrl_t;

#define MODE_SEND               1<<0
#define MODE_RCV                1<<1
#define MODE_CLIENT             1<<2
#define MODE_SERVER             1<<3

#define HEADER_TYPE_LEN         4
#define HEADER_DATA_LEN_LEN     4
#define HEADER_TYPE_MTIME_SEC   4
#define HEADER_TYPE_MTIME_NSEC  4

typedef struct header{
	ctrl_t      ctrl_msg;
	uint64_t    data_len;
	uint32_t    mtime_sec;
	uint64_t    mtime_nsec;
	xfer_t      type;
} header_t;

typedef struct parcel_block{
	char *buffer;
	char *data;
	uint64_t dlen;
} parcel_block;

typedef struct parcel_opt_t{
	int timeout;

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
	int mss;
	int fifo_test;
	off_t fifo_test_size;
	int full_root;
	int socket_ready;
	int ignore_modification;

	int *send_pipe;
	int *recv_pipe;

	int remote_to_local;
	int encryption;
	int n_crypto_threads;

	Crypto *enc;
	Crypto *dec;

	char restart_path[MAX_PATH_LEN];

} parcel_opts_t;

typedef struct remote_arg_t{

	int pipe_pid;
	int ssh_pid;
	int remote_pid;

	char *local_ip;
	char *remote_ip;

	char remote_path[MAX_PATH_LEN];
	char pipe_port[MAX_PATH_LEN];
	char pipe_host[MAX_PATH_LEN];
	char udpipe_location[MAX_PATH_LEN];

	char pipe_cmd[MAX_PATH_LEN];
	char xfer_cmd[MAX_PATH_LEN];

} remote_arg_t;

extern remote_arg_t g_remote_args;
extern parcel_opt_t g_opts;

int parse_destination(char *xfer_cmd);

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

header_t* nheader(xfer_t type, off_t size);

// wrapper for read

off_t read_data(void* b, int len);

int read_header(header_t *header);

// step backwards down a given directory path

int get_parent_dir(char parent_dir[MAX_PATH_LEN], char path[MAX_PATH_LEN]);

// create the command used to execvp a pipe i.e. udpipe

int generate_pipe_cmd(char*pipe_cmd, int pipe_mode);

// execute a pipe process i.e. udpipe and redirect parcel output

int run_pipe(char* pipe_cmd);

// run the ssh command that will create a remote parcel process

//int run_ssh_command(char *remote_dest);
int run_ssh_command();


#endif
