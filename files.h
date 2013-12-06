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
#include <sys/types.h>
#include <dirent.h>
#include <sys/ioctl.h>

#define MAX_PATH_LEN 1024

typedef struct file_object_t{
    struct stat stats;
    int mode;
    char *filetype;
    char * path;
    int length;
} file_object_t;


typedef struct file_LL file_LL;

struct file_LL{
    file_object_t *curr;
    file_LL *next;
};

/* Creates a new file_object_t given a path and stores the file
   stats */

file_object_t* new_file_object(char*path);

/* Adds a file_object_t to the fileList linked list of file_object_t
   based on path */

file_LL* add_file_to_list(file_LL *fileList, char*path);

/* Buils a linked list of file_object_t given path array of length n */

file_LL* build_filelist(int n, char* paths[]);

/* Buils a linked list of file_object_t given directory file object */

file_LL* lsdir(file_object_t *file);

    
