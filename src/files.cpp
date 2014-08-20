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

#define _FILE_OFFSET_BITS 64

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

#include "util.h"
#include "parcel.h"

int flogfd = 0;
char *f_map = NULL;
char log_path[MAX_PATH_LEN];

// Initialize 

file_LL *checkpoint = NULL;

// map the file pointed to by a file descriptor to memory

int map_fd(int fd, off_t size)
{
    // file protections and advice
    int prot	= PROT_READ | PROT_WRITE;
    int advice	= POSIX_MADV_SEQUENTIAL;

    // bad file descriptor?
    if (fd < 0) {
        close(fd);
        ERR("bad file descriptor");
    }

    // seek to end of file
    if (lseek(fd, size-1, SEEK_SET) < 0) {
        close(fd);
        ERR("Error setting file length");
    }

     // and write bit to actually set file size
    if (write(fd, "", 1) < 0) {
        close(fd);
        ERR("Error verifying file length");
    }

    // Map the file to memory
    f_map = (char*) mmap64(0, size, prot, MAP_SHARED, fd, 0);

    if (f_map == MAP_FAILED) {
        close(fd);
        ERR("unable to map file");
    }

    // Tell the system our intent with the file, no error check, if it
    // doesn't work, it doesn't work
    madvise(f_map, size, advice);

    return RET_SUCCESS;

}


int unmap_fd(int fd, off_t size)
{
    if (munmap(f_map, size) < 0) {
	// ERR("unable to un-mmap the file");
    }
    return RET_SUCCESS;
}

int mwrite(char* buff, off_t pos, int len)
{
    memcpy(f_map+pos, buff, len);
    return RET_SUCCESS;
}

// NOTE: this walks the list, which shouldn't need to be done as the list
// now holds/updates its tail. Left here for a sanity walk just in case
// it's needed later.

file_object_t *get_file_LL_tail(file_LL *main_list)
{
    file_node_t* list = main_list->head;
    
    if (!list) {
        return NULL;
    }
    
    while (list->next){
        list = list->next;
    }
    return list->curr;
}

file_object_t *find_last_instance(file_LL *list, file_object_t *file)
{
    file_object_t *last = NULL;
    file_node_t *cursor = NULL;
    
    if ( list != NULL ) {
        cursor = list->head;
    }
    
    while (list) {
        if (!strcmp(cursor->curr->path, file->path)) {
            last = cursor->curr;
        }
        cursor = cursor->next;
    }   
    return last;
}


int is_in_checkpoint(file_object_t *file)
{
    if (!opts.restart || !file)
        return 0;

    struct timespec mtime1 = {0, 0};
    struct timespec mtime2 = {0, 0};

    file_object_t *match = find_last_instance(checkpoint, file);

    if (match) {

        if (opts.ignore_modification) {
            return 1;
        }

        mtime1.tv_sec = file->stats.st_mtime;
        mtime2.tv_sec = match->stats.st_mtime;

        if (mtime2.tv_sec - mtime1.tv_sec) {
            verb(VERB_1, "Resending [%s]. File has been modified since checkpoint.", file->path);
            return 0;
        }

        return 1;

    }

    return 0;
}

int read_checkpoint(char *path)
{
//    char c;
    FILE* restart_f;
    char linebuf[MAX_PATH_LEN];

    if(!(restart_f = fopen(path, "r"))) {
        ERR("Unable to open restart file [%s]", path);
    }

    struct timespec mtime = {0, 0};
    while (fscanf(restart_f, "%s %li", linebuf, &mtime.tv_sec) == 2) {
        checkpoint = add_file_to_list(checkpoint, linebuf, NULL);
//        file_object_t *last = get_file_LL_tail(checkpoint);
        file_object_t *last = checkpoint->tail->curr;
        if (last) {
            last->stats.st_mtime = mtime.tv_sec;
            verb(VERB_2, "Checkpoint completed and unmodified: %s [%li]", 
                 linebuf, mtime.tv_sec);
        }
    }

    return RET_SUCCESS;
}    


int open_log_file()
{
    if (!opts.log) {
        return RET_FAILURE;
    }

    int f_mode = O_CREAT | O_WRONLY | O_APPEND;
    int f_perm = 0666;

    if((flogfd = open(log_path, f_mode, f_perm)) < 0) {
        ERR("Unable to open log file [%s]", log_path);
    }
    
    return RET_SUCCESS;
}

int close_log_file()
{
    if (!opts.log) {
        return RET_SUCCESS;
    }
    
    if(close(flogfd)) {
        verb(VERB_3, "Unable to close log file [%s].", log_path);
    }

    return RET_SUCCESS;
}

int log_completed_file(file_object_t *file)
{
    if (!opts.log) {
        return RET_SUCCESS;
    }

    char path[MAX_PATH_LEN];

    struct timespec mtime = {0, 0};
    mtime.tv_sec = file->stats.st_mtime;
    sprintf(path, "%s %li\n", file->path, mtime.tv_sec);

    if (!write(flogfd, path, strlen(path))) {
        perror("WARNING: [log_completed_file] unable to log file completion");
    }

    return RET_SUCCESS;
}

// step backwards up a given directory path
int get_parent_dir(char parent_dir[MAX_PATH_LEN], char path[MAX_PATH_LEN])
{
    bzero(parent_dir, MAX_PATH_LEN);
    char*cursor = path+strlen(path);

    while (cursor > path && *cursor != '/') {
        cursor--;
    }
		    
    if (cursor <= path) {
        parent_dir[0] = '\0';
    } else {
        memcpy(parent_dir, path, cursor-path);
    }

    return RET_SUCCESS;
}


int print_file_LL(file_LL *list)
{
    file_node_t* cursor = NULL;
    
    if ( list != NULL ) {
        cursor = list->head;
    }
    
    while (cursor) {
        fprintf(stderr, "%s, ", cursor->curr->path);
        cursor = cursor->next;
    }

    return RET_SUCCESS;
}

file_object_t* new_file_object(char*path, char*root)
{
    file_object_t *file = (file_object_t*) malloc(sizeof(file_object_t));

    // fly - zeroing in case we ever check the wrong one, but not sure how 
    // best to handle with pointers...calloc isn't proper for that
    memset(file, 0, sizeof(file_object_t));
    file->filetype = (char*)NULL;
    
    file->path = strdup(path);
    file->root = root;

    if (stat(path, &file->stats) == -1) {
        ERR("unable to stat file [%s]", file->path);
    }

    switch (file->stats.st_mode & S_IFMT) {
        case S_IFBLK:  
            file->mode = S_IFBLK;     
            file->filetype = (char*)  "block device";
            break;
        case S_IFCHR:  
            file->mode = S_IFCHR;     
            file->filetype = (char*) "character device";
            break;
        case S_IFDIR:  
            file->mode = S_IFDIR;     
            file->filetype = (char*) "directory";
            file->mtime_sec = file->stats.st_mtime;
            file->mtime_nsec = file->stats.st_mtim.tv_nsec;
            break;
        case S_IFIFO:  
            file->mode = S_IFIFO;     
            file->filetype = (char*) "named pipe";
            break;
        case S_IFLNK:  
            file->mode = S_IFLNK;     
            file->filetype = (char*) "symlink";
            break;
        case S_IFREG:  
            file->mode = S_IFREG;     
            file->filetype = (char*) "regular file";
            file->length = file->stats.st_size;
            file->mtime_sec = file->stats.st_mtime;
            file->mtime_nsec = file->stats.st_mtim.tv_nsec;
            break;
        case S_IFSOCK: 
            file->mode = S_IFSOCK;    
            file->filetype = (char*) "socket";
            break;
        default:       
            fprintf(stderr, "Filetype uknown: %s", file->path);
            break;
    }

    return file;
}

file_LL* add_file_to_list(file_LL *fileList, char*path, char*root)
{
    verb(VERB_2, "add_file_to_list: path = %s, root = %s\n", path, root);

    // make a file object out of the path
    file_object_t* new_file = new_file_object(path, root);

    // create a new node
    file_node_t* new_node = (file_node_t*)malloc(sizeof(file_node_t));
    new_node->curr = new_file;
    new_node->next = NULL;
    
    if (!fileList) {
        // create a new list if we don't have one
        file_LL * new_list = (file_LL*) malloc(sizeof(file_LL));
        new_list->head = new_node;
        new_list->tail = new_node;
        new_list->count = 1;
        return new_list;
    } else {
        // add to end of file list
        fileList->tail->next = new_node;
        fileList->tail = new_node;
        fileList->count++;
    }

    return fileList;
}

// part the first: get all elements in the top level path
// part the second: 


file_LL* build_full_filelist(int n, char *paths[])
{
    file_LL *fileList = NULL;
    struct stat stats;

    verb(VERB_2, "build_full_filelist: %d paths\n", n);

    for (int i = 0; i < n ; i++) {

        if (paths[i]) {
            verb(VERB_2, "build_full_filelist: trying %s\n", paths[i]);

            if (stat(paths[i], &stats) == -1) {
                ERR("unable to stat file [%s], error = %d", paths[i], errno);
            }

            if ((stats.st_mode & S_IFMT) == S_IFDIR) {
                verb(VERB_2, "build_full_filelist: dir found, traversing %s\n", paths[i]);
                fileList = add_file_to_list(fileList, paths[i], paths[i]);
                char parent_dir[MAX_PATH_LEN];
                get_parent_dir(parent_dir, paths[i]);
                lsdir_to_list(fileList, paths[i], parent_dir);
            } else {
                char parent_dir[MAX_PATH_LEN];
                get_parent_dir(parent_dir, paths[i]);
                
                fileList = add_file_to_list(fileList, paths[i], parent_dir);
            }
        }

        // fileList = add_file_to_list(fileList, paths[i]);
    }

    verb(VERB_2, "build_full_filelist: complete\n");
    return fileList;
    
    
}


file_LL* build_filelist(int n, char *paths[])
{
    file_LL *fileList = NULL;
    struct stat stats;

    verb(VERB_2, "build_filelist: %d paths\n", n);

    for (int i = 0; i < n ; i++) {

        if (paths[i]) {

            if (stat(paths[i], &stats) == -1) {
                ERR("unable to stat file [%s], error = %d", paths[i], errno);
            }

            if ((stats.st_mode & S_IFMT) == S_IFDIR) {
                fileList = add_file_to_list(fileList, paths[i], paths[i]);
            } else {
                char parent_dir[MAX_PATH_LEN];
                get_parent_dir(parent_dir, paths[i]);
                
                fileList = add_file_to_list(fileList, paths[i], parent_dir);
            }
        }

        // fileList = add_file_to_list(fileList, paths[i]);
    }

    verb(VERB_2, "build_filelist: complete\n");
    return fileList;
}

void lsdir_to_list(file_LL* ls_fileList, char* dir, char* root)
{
    // Verify that we were actually passed a directory file
/*    if ( !(file->mode == S_IFDIR) ){
        warn("attemped to enter a non-directory file");
        return NULL;
    } */
    verb(VERB_2, "lsdir_to_list: %s %s\n", dir, root);

    DIR *dirp = opendir(dir);
    struct dirent * entry;
    if ( dirp != NULL ) {

        // Iterate through each file in the directory
        while ((entry = readdir(dirp)) != NULL) {

            // If given, ignore the current and parent directories 
            if ( strcmp(entry->d_name, ".") && strcmp(entry->d_name,"..") ) {
                char path[MAX_PATH_LEN];
                sprintf(path, "%s/%s", dir, entry->d_name);
//                ls_fileList = add_file_to_list(ls_fileList, path, root);
                add_file_to_list(ls_fileList, path, root);
            }
        }

        closedir(dirp);
    } else {
        warn("attemped to enter a non-directory file");
    }
}


file_LL* lsdir(file_object_t *file)
{
    // Verify that we were actually passed a directory file
    if ( !(file->mode == S_IFDIR) ){
        warn("attemped to enter a non-directory file");
        return NULL;
    }

    DIR *dirp = opendir(file->path);
    struct dirent * entry;
    file_LL* ls_fileList = NULL;

    // Iterate through each file in the directory
    while ((entry = readdir(dirp)) != NULL) {

        // If given, ignore the current and parent directories 
        if ( strcmp(entry->d_name, ".") && strcmp(entry->d_name,"..") ) {
            char path[MAX_PATH_LEN];
            sprintf(path, "%s/%s", file->path, entry->d_name);
            ls_fileList = add_file_to_list(ls_fileList, path, file->root);
        }
    }

    closedir(dirp);

    return ls_fileList;
}


// make a new directory, but recurse through dir tree until this is possible
int mkdir_parent(char* path)
{
    // default permissions for creating new directories
    int ret, err;
    int mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;

    ret = mkdir(path, mode);

    if ( ret ) {
        // Hold onto last error
        err = errno;
        
        // If the parents in the path name do not exist, then make them
        if (err == ENOENT) {
            verb(VERB_2, "Again find parent directory and make it\n");

            char parent_dir[MAX_PATH_LEN];
            get_parent_dir(parent_dir, path);

            verb(VERB_2, "Building directory path [%s]\n", parent_dir);

            mkdir_parent(parent_dir);
            ret = mkdir_parent(path);
            
        // The directory already exists
        } else if (err == EEXIST) {

            // Continue
            
        // Otherwise, mkdir failed
        } else {
            fprintf(stderr, "ERROR: Unable to create directory [%s]: %s\n", path, strerror(err));
            clean_exit(EXIT_FAILURE);
        }

    } else {
        verb(VERB_2, "Built directory %s\n", path);
    }

    return ret;
}

// Get the size of a file, should handle large files as well
off_t fsize(int fd) 
{
    off_t size;
    size = lseek64(fd, 0L, SEEK_END);
    lseek64(fd, 0L, SEEK_SET);
    return size;
}



int generate_base_path(char* prelim, char *data_path)
{
    // generate a base path for all destination files    
    int bl = strlen(prelim);
    if (bl == 0) {
        sprintf(data_path, "%s/", prelim);
    } else { 
        if (prelim[bl-1] != '/') {
            bl++;
        }
        sprintf(data_path, "%s/", prelim);
    }

    return bl;
}

//
// set_mod_time 
//
// sets the modification time given a file name
// returns 0 on success, or non-zero error code in errno.h on fail
// note: it also sets the access time to now
//
int set_mod_time(char* filename, long int mtime_nsec, int mtime)
{
    timespec        times[2];
    int             retVal = 0;
    
    // fly - Ok, a bit obtuse, but utimensat (and utimes, actually) expect that times[0]
    // refers to atime and times[1] refers to mtime.  Also, in addition to UTIME_NOW, 
    // UTIME_OMIT can be used, so we could have it not set the atime.
    // Also, it could be problematic if the system doesn't use nanosecond resolution, and
    // we could fall back to second or microsecond, depending on the system.
    
    // atime set to now
    times[0].tv_sec = UTIME_NOW;
    times[0].tv_nsec = UTIME_NOW;
    
    // mtime set as file given
    times[1].tv_sec = mtime;
    times[1].tv_nsec = mtime_nsec;

    if ( utimensat(AT_FDCWD, filename, times, 0) < 0 ) {
        retVal = errno;
        fprintf(stdout, "ERROR: Unable to set timestamp on %s, error code %d\n", filename, retVal);
    } else {
        verb(VERB_2, "setting - mtime: %d, mtime_nsec: %ld\n", mtime, mtime_nsec);        
    }
    
    return retVal;
  
}

//
// get_mod_time 
//
// gets the modification time given a file name
// returns 0 on success, or non-zero error code in errno.h on fail
//
int get_mod_time(char* filename, long int* mtime_nsec, int* mtime)
{
    struct stat tmpStat;
    int         retVal = 0;
    
    if ( stat(filename, &tmpStat) < 0 ) {
        retVal = errno;
        fprintf(stderr, "ERROR: Unable to stat %s, error code %d\n", filename, retVal);
    } else {
        if ( (mtime != NULL) && (mtime_nsec != NULL) ) {
            verb(VERB_2, "stat for file: %s, mtime: %d, mtime_nsec: %ld\n", filename, tmpStat.st_mtime, tmpStat.st_mtim.tv_nsec);
            *mtime = tmpStat.st_mtime;
            *mtime_nsec = tmpStat.st_mtim.tv_nsec;
            verb(VERB_2, "file: %s, mtime: %d, mtime_nsec: %ld\n", filename, *mtime, *mtime_nsec);
        } else {
            fprintf(stderr, "ERROR: bad pointer passed to get_mod_time\n");
        }
    }
    
    return retVal;

    
}


// Returns the size of a file list (total, in bytes)
int get_filelist_size(file_LL *fileList)
{
    int total_size = 0;
    
    if ( fileList != NULL ) {
        file_node_t* cursor = fileList->head;
        int static_file_size = (sizeof(int) * 3) + sizeof(long int) + sizeof(struct stat);
        
        while ( cursor != NULL ) {
            total_size += (static_file_size + strlen(cursor->curr->filetype) + strlen(cursor->curr->path) + strlen(cursor->curr->root));
            cursor = cursor->next;
        }
    }
    
    return total_size;
    
}



