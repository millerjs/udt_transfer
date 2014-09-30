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
/* #define _LARGE_FILES */
/* #define _FILE_OFFSET_BITS  64 */

#ifndef FILES_H
#define FILES_H

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <getopt.h>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define MAX_PATH_LEN 1024

typedef struct file_object_t{
    struct stat stats;
    int         mode;
    int         length;
    int         mtime_sec;
    long int    mtime_nsec;
    char        *filetype;
    char        *path;
    char        *root;
} file_object_t;

typedef struct file_LL file_LL;

typedef struct file_node_t {
    file_object_t *curr;
    struct file_node_t* next;
} file_node_t;

struct file_LL {
    file_node_t*    head;
    file_node_t*    tail;
    unsigned int    count;
//    file_object_t *curr;
//    file_LL *next;
};


extern char *f_map;
extern int flogfd;
extern char log_path[MAX_PATH_LEN];

void set_socket_ready(int state);

int get_socket_ready();

void set_encrypt_ready(int state);

int get_encrypt_ready();

void set_auth_signed();

void set_peer_authed();

int get_auth_signed();

int get_peer_authed();

int print_file_LL(file_LL *list);

int is_in_checkpoint(file_object_t *file);

int read_checkpoint(char *path);

int open_log_file();

int log_completed_file(file_object_t *file);

int close_log_file();

/* Creates a new file_object_t given a path and stores the file
   stats */

file_object_t* new_file_object(char*path);

/* Adds a file_object_t to the fileList linked list of file_object_t
   based on path */

file_LL* add_file_to_list(file_LL *fileList, char*path, char*root);

file_LL* init_filelist(int n, char *paths[]);

/* Builds a linked list of file_object_t given path array of length n, recursing in all directories */

file_LL* build_full_filelist(int n, char *paths[]);

/* Builds a linked list of file_object_t given path array of length n */

file_LL* build_filelist(int n, char* paths[]);

/* Builds a linked list of file_object_t given directory file object */

file_LL* lsdir(file_object_t *file);

/* Builds a linked list of file_object_t given directory file object, adding it to given file_LL */

void lsdir_to_list(file_LL* ls_fileList, char* dir, char* root);

/* make a new directory, but recurse through dir tree until this is possible */

int mkdir_parent(char* path);

int get_parent_dir(char parent_dir[MAX_PATH_LEN], char path[MAX_PATH_LEN]);

off_t fsize(int fd);

int generate_base_path(char *perlim_path, char *data_path);


int map_fd(int fd, off_t size);

int unmap_fd(int fd, off_t size);

int mwrite(char* buff, off_t pos, int len);

// wrapper around read to control & check
ssize_t pipe_read(int fd, void *buf, size_t count);

// wrapper around write to control & check
ssize_t pipe_write(int fd, const void *buf, size_t count);

// Set the mtime for a given file
int set_mod_time(char* filename, long int mtime_nsec, int mtime);

// Get the mtime for a given file
int get_mod_time(char* filename, long int* mtime_nsec, int* mtime);

// Gets the size of a file list (total, in bytes) in list_size, returns the count of files in list
int get_filelist_size(file_LL *fileList);

// Pack a file list into a byte array to send across
char* pack_filelist(file_LL* fileList, int total_size);

// Unpack sent file list byte array back into a file list struct
file_LL* unpack_filelist(char* fileList_data, int data_length);

// Free a given file object
void free_file_object(file_object_t* file);

// Free a given file list
void free_file_list(file_LL* fileList);

// Compare two file timestamps, return zero if the same, non-zero if not
int compare_timestamps(file_object_t* file1, file_object_t* file2);

#endif
