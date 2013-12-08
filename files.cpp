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

#define _FILE_OFFSET_BITS 64

#include "ucp.h"

// step backwards down a given directory path

int get_parent_dir(char parent_dir[MAX_PATH_LEN], char path[MAX_PATH_LEN]){
    
    bzero(parent_dir, MAX_PATH_LEN);
    char*cursor = path+strlen(path);

    while (cursor > path && *cursor != '/')
	cursor--;
		    
    if (cursor <= path){
	error("Unable to recognize parent directory: %s", path);

    }
		    
    memcpy(parent_dir, path, cursor-path);
}



int print_file_LL(file_LL *list){
    while (list){
	fprintf(stderr, "%s, ", list->curr->path);
	list = list->next;
    }
}

file_object_t* new_file_object(char*path){
    file_object_t *file = (file_object_t*) malloc(sizeof(file_object_t));
    file->path = strdup(path);
    if (stat(path, &file->stats) == -1){
	perror("ERROR: Unable to stat file");
	exit(EXIT_FAILURE);
    }

    switch (file->stats.st_mode & S_IFMT) {
    case S_IFBLK:  file->mode = S_IFBLK;     
	file->filetype = (char*)  "block device";
	break;
    case S_IFCHR:  file->mode = S_IFCHR;     
	file->filetype = (char*) "character device";
	break;
    case S_IFDIR:  file->mode = S_IFDIR;     
	file->filetype = (char*) "directory";
	break;
    case S_IFIFO:  file->mode = S_IFIFO;     
	file->filetype = (char*) "named pipe";
	break;
    case S_IFLNK:  file->mode = S_IFLNK;     
	file->filetype = (char*) "symlink";
	break;
    case S_IFREG:  file->mode = S_IFREG;     
	file->filetype = (char*) "regular file";
	break;
    case S_IFSOCK: file->mode = S_IFSOCK;    
	file->filetype = (char*) "socket";
	break;
    default:       
	fprintf(stderr, "Filetype uknown: %s", file->path);
    }


    return file;
}

file_LL* add_file_to_list(file_LL *fileList, char*path){

    // make a file object out of the path
    file_object_t* new_file = new_file_object(path);
    file_LL *cursor = fileList;

    // create a new file list
    file_LL * new_list = (file_LL*) malloc(sizeof(file_LL));
    new_list->curr = new_file;
    new_list->next = NULL;
    
    if (!fileList){
	return new_list;
    } else {
	// move to end of file list
	while (cursor->next)
	    cursor = cursor->next;
	cursor->next = new_list;
    }
    return fileList;

}

file_LL* build_filelist(int n, char *paths[]){

    file_LL *fileList = NULL;

    for (int i = 0; i < n ; i++){
	fileList = add_file_to_list(fileList, paths[i]);
    }
    
    return fileList;
    

}



file_LL* lsdir(file_object_t *file){
    
    // Verify that we were actually passed a directory file
    if ( !(file->mode == S_IFDIR) ){
	fprintf(stderr, "File is not a directory");
	return NULL;
    }
    
    DIR *dirp = opendir(file->path);
    struct dirent * entry;
    file_LL* ls_fileList = NULL;

    // Iterate through each file in the directory
    while ((entry = readdir(dirp)) != NULL){

	// If given, ignore the current and parent directories 
	if ( strcmp(entry->d_name, ".") && strcmp(entry->d_name,"..") ){
	    
	    char path[MAX_PATH_LEN];
	    sprintf(path, "%s/%s", file->path, entry->d_name);
	    ls_fileList = add_file_to_list(ls_fileList, path);
	    
	}
	
    }


    closedir(dirp);

    return ls_fileList;


}


// make a new directory, but recurse through dir tree until this is possible

int mkdir_parent(char* path){

    // default permissions for creating new directories
    int ret, err;
    int mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;

    ret = mkdir(path, mode);

    if ( ret ){
	
	// Hold onto last error

	err = errno;
	
	// If the parents in the path name do not exist, then make them

	if (err == ENOENT){
	    
	    if (opt_verbosity > VERB_2)
		fprintf(stderr, "Again find parent directory and make it\n");

	    char parent_dir[MAX_PATH_LEN];
	    get_parent_dir(parent_dir, path);
	    
	    if (opt_verbosity > VERB_2)
		fprintf(stderr, "Attempting to make %s\n", parent_dir);

	    mkdir_parent(parent_dir);

	}

	// The directory already exists
	else if (err = EEXIST){
	    // Continue
	}

	// Otherwise, mkdir failed
	else {
	    fprintf(stderr, "ERROR: Unable to create directory [%s]: %s\n", 
		    path, strerror(err));
	    clean_exit(EXIT_FAILURE);

	}

    } else {
	
	if (opt_verbosity > VERB_2)
	    fprintf(stderr, "Built directory %s\n", path);

    }


    return ret;

}

// Get the size of a file, should handle large files as well

off_t fsize(int fd) {

    off_t size;
    size = lseek64(fd, 0L, SEEK_END);
    lseek64(fd, 0L, SEEK_SET);

    return size;
}


int generate_base_path(char* prelim, char *data_path){

    // generate a base path for all destination files    

    int bl = strlen(prelim);

    if (prelim[bl-1] != '/') bl++;

    sprintf(data_path, "%s/", prelim);

    return bl;

}
