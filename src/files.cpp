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

#define _FILE_OFFSET_BITS		64
#define FILE_TIME_SLICE_SIZE	50000
#define MAX_PIPE_FIFO_SIZE		64
#define MAX_PIPE_FIFO_LOOKUP	4

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

#include "util.h"
#include "parcel.h"

int flogfd = 0;
char *f_map = NULL;
char g_log_path[MAX_PATH_LEN];

int g_socket_ready = 0;
int g_encrypt_verified = 0;
int g_signed_auth = 0;
int g_authed_peer = 0;

off_t g_pipe_fifo[NUM_FIFOS][MAX_PIPE_FIFO_SIZE];
int g_pipe_fifo_idx[NUM_FIFOS];
int g_pipe_fifo_high_water[NUM_FIFOS];

copy_chunk_t g_time_slices[FILE_TIME_SLICE_SIZE];
int g_time_slice_idx = 0;

pthread_mutex_t g_pipe_mutex;

// Initialize

file_LL *checkpoint = NULL;

void init_pipe_fifo()
{
	memset(g_pipe_fifo, 0, (sizeof(off_t) * MAX_PIPE_FIFO_SIZE) * NUM_FIFOS);
	memset(g_pipe_fifo_idx, 0, sizeof(int) * NUM_FIFOS);
	memset(g_pipe_fifo_high_water, 0, sizeof(int) * NUM_FIFOS);
}

void print_pipe_fifo(fifo_t fifo_pipe)
{
	if ( fifo_pipe < NUM_FIFOS ) {
		if ( g_pipe_fifo_idx[fifo_pipe] ) {
			if ( fifo_pipe ) {
				verb(VERB_2, "[%s] WRITE pipe", __func__);
			} else {
				verb(VERB_2, "[%s] READ pipe", __func__);
			}
			for ( int i = 0; i < g_pipe_fifo_idx[fifo_pipe]; i++ ) {
				verb(VERB_2, "[%s] %d - %lu", __func__, i, g_pipe_fifo[fifo_pipe][i]);
			}
		}
	}
}

int push_pipe_fifo(fifo_t fifo_pipe, off_t write_size)
{
	if ( (fifo_pipe < NUM_FIFOS) && write_size ) {
		if ( g_pipe_fifo_idx[fifo_pipe] < MAX_PIPE_FIFO_SIZE ) {
			g_pipe_fifo[fifo_pipe][g_pipe_fifo_idx[fifo_pipe]++] = write_size;
			if ( g_pipe_fifo_idx[fifo_pipe] > g_pipe_fifo_high_water[fifo_pipe] ) {
				g_pipe_fifo_high_water[fifo_pipe] = g_pipe_fifo_idx[fifo_pipe];
			}
		}
		if ( fifo_pipe ) {
			verb(VERB_2, "[%s] %d pushed, WRITE pipe now %d", __func__, write_size, g_pipe_fifo_idx[fifo_pipe]);
		} else {
			verb(VERB_2, "[%s] %d pushed, READ pipe now %d", __func__, write_size, g_pipe_fifo_idx[fifo_pipe]);
		}
		print_pipe_fifo(fifo_pipe);
	}
	return g_pipe_fifo_idx[fifo_pipe];
}

int pop_pipe_fifo(fifo_t fifo_pipe, off_t read_size)
{
	if ( (fifo_pipe < NUM_FIFOS) && read_size ) {
		if ( g_pipe_fifo_idx[fifo_pipe] > 0 ) {
			if ( g_pipe_fifo[fifo_pipe][g_pipe_fifo_idx[fifo_pipe] - 1] != read_size ) {
				verb(VERB_2, "[%s] fifo pop not equal to size (%lu != %lu)", __func__, read_size, g_pipe_fifo[fifo_pipe][g_pipe_fifo_idx[fifo_pipe]]);
			}
			g_pipe_fifo[fifo_pipe][--g_pipe_fifo_idx[fifo_pipe]] = 0;
		}
		if ( fifo_pipe ) {
			verb(VERB_2, "[%s] %d popped, WRITE pipe now %d", __func__, read_size, g_pipe_fifo_idx[fifo_pipe]);
		} else {
			verb(VERB_2, "[%s] %d popped, READ pipe now %d", __func__, read_size, g_pipe_fifo_idx[fifo_pipe]);
		}
		print_pipe_fifo(fifo_pipe);
	}
	return g_pipe_fifo_idx[fifo_pipe];
}

int get_fifo_size(fifo_t fifo_pipe) {
	int ret_val = -1;
	if ( fifo_pipe < NUM_FIFOS ) {
		ret_val = g_pipe_fifo_idx[fifo_pipe];
	}
	return ret_val;
}

int get_fifo_high_water_mark(fifo_t fifo_pipe) {
	int ret_val = -1;
	if ( fifo_pipe < NUM_FIFOS ) {
		ret_val = g_pipe_fifo_high_water[fifo_pipe];
	}
	return ret_val;
}

void init_time_array()
{
	memset(g_time_slices, 0, sizeof(copy_chunk_t) * FILE_TIME_SLICE_SIZE);
	g_time_slice_idx = 0;
}

void add_time_slice(chunk_t type, double slice, long data_size)
{
	if ( get_file_logging() ) {
		if ( g_time_slice_idx < FILE_TIME_SLICE_SIZE ) {
			if ( type < NUM_CHUNK_TYPES ) {
				g_time_slices[g_time_slice_idx].type = type;
			}
			g_time_slices[g_time_slice_idx].time_slice = slice;
			g_time_slices[g_time_slice_idx++].data_size = data_size;
		} else {
//			verb(VERB_1, "[%s] Unable to add, array full", __func__);
		}
	}
}

void print_time_slices()
{
	for ( int i = 0; i < g_time_slice_idx; i++ ) {
		verb(VERB_2, "%d: %d %.04f %lu", i, g_time_slices[i].type, g_time_slices[i].time_slice, g_time_slices[i].data_size);
//		fprintf(stderr, "%d: %.04f %lu\n", i, g_time_slices[i].time_slice, g_time_slices[i].data_size);
	}
}


void set_socket_ready(int state)
{
	if ( state != 0 ) {
		g_socket_ready = 1;
	} else {
		g_socket_ready = 0;
	}
}

int get_socket_ready()
{
	return g_socket_ready;
}

void set_auth_signed()
{
	g_signed_auth = 1;
}

void set_peer_authed()
{
	g_authed_peer = 1;
}

int get_auth_signed()
{
	return g_signed_auth;
}

int get_peer_authed()
{
	return g_authed_peer;
}

void set_encrypt_ready(int state)
{
	if ( state != 0 ) {
		g_encrypt_verified = 1;
	} else {
		g_encrypt_verified = 0;
	}
}

int get_encrypt_ready()
{
	int ready = 0;

	if (g_opts.encryption) {
		ready = (g_signed_auth & g_authed_peer);
	} else {
		ready = 1;
	}
	return ready;
}


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
	if (!g_opts.restart || !file)
		return 0;

	struct timespec mtime1 = {0, 0};
	struct timespec mtime2 = {0, 0};

	file_object_t *match = find_last_instance(checkpoint, file);

	if (match) {

		if (g_opts.ignore_modification) {
			return 1;
		}

		mtime1.tv_sec = file->stats.st_mtime;
		mtime2.tv_sec = match->stats.st_mtime;

		if (mtime2.tv_sec - mtime1.tv_sec) {
			verb(VERB_1, "[%s] Resending [%s]. File has been modified since checkpoint.", __func__, file->path);
			return 0;
		}

		return 1;

	}

	return 0;
}

int read_checkpoint(char *path)
{
//	char c;
	FILE* restart_f;
	char linebuf[MAX_PATH_LEN];

	if(!(restart_f = fopen(path, "r"))) {
		ERR("Unable to open restart file [%s]", path);
	}

	struct timespec mtime = {0, 0};
	while (fscanf(restart_f, "%s %li", linebuf, &mtime.tv_sec) == 2) {
		checkpoint = add_file_to_list(checkpoint, linebuf, NULL);
//		file_object_t *last = get_file_LL_tail(checkpoint);
		file_object_t *last = checkpoint->tail->curr;
		if (last) {
			last->stats.st_mtime = mtime.tv_sec;
			verb(VERB_2, "[%s] Checkpoint completed and unmodified: %s [%li]", __func__,
				 linebuf, mtime.tv_sec);
		}
	}

	return RET_SUCCESS;
}


int open_log_file()
{
	if (!g_opts.log) {
		return RET_FAILURE;
	}

	int f_mode = O_CREAT | O_WRONLY | O_APPEND;
	int f_perm = 0666;

	if((flogfd = open(g_log_path, f_mode, f_perm)) < 0) {
		ERR("Unable to open log file [%s]", g_log_path);
	}

	return RET_SUCCESS;
}

int close_log_file()
{
	if (!g_opts.log) {
		return RET_SUCCESS;
	}

	if(close(flogfd)) {
		verb(VERB_3, "[%s] Unable to close log file [%s].", __func__, g_log_path);
	}

	return RET_SUCCESS;
}

int log_completed_file(file_object_t *file)
{
	if (!g_opts.log) {
		return RET_SUCCESS;
	}

	char path[MAX_PATH_LEN];

	struct timespec mtime = {0, 0};
	mtime.tv_sec = file->stats.st_mtime;
	snprintf(path, MAX_PATH_LEN - 1, "%s %li\n", file->path, mtime.tv_sec);

	if (!write(flogfd, path, strlen(path))) {
		perror("WARNING: [log_completed_file] unable to log file completion");
	}

	return RET_SUCCESS;
}

// step backwards up a given directory path
int get_parent_dir(char parent_dir[MAX_PATH_LEN], char path[MAX_PATH_LEN])
{
	memset(parent_dir, 0, sizeof(char) * MAX_PATH_LEN);
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
		verb(VERB_2, "%s, ", cursor->curr->path);
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
	file->root = strdup(root);

	if (stat(path, &file->stats) == -1) {
		ERR("unable to stat file [%s]", file->path);
	}

	switch (file->stats.st_mode & S_IFMT) {
		case S_IFBLK:
			file->mode = S_IFBLK;
			file->filetype = strdup((char*) "block device");
			break;
		case S_IFCHR:
			file->mode = S_IFCHR;
			file->filetype = strdup((char*) "character device");
			break;
		case S_IFDIR:
			file->mode = S_IFDIR;
			file->filetype = strdup((char*) "directory");
			file->mtime_sec = file->stats.st_mtime;
			file->mtime_nsec = file->stats.st_mtim.tv_nsec;
			break;
		case S_IFIFO:
			file->mode = S_IFIFO;
			file->filetype = strdup((char*) "named pipe");
			break;
		case S_IFLNK:
			file->mode = S_IFLNK;
			file->filetype = strdup((char*) "symlink");
			break;
		case S_IFREG:
			file->mode = S_IFREG;
			file->filetype = strdup((char*) "regular file");
			file->length = file->stats.st_size;
			file->mtime_sec = file->stats.st_mtime;
			file->mtime_nsec = file->stats.st_mtim.tv_nsec;
			break;
		case S_IFSOCK:
			file->mode = S_IFSOCK;
			file->filetype = strdup((char*) "socket");
			break;
		default:
			verb(VERB_2, "Filetype uknown: %s", file->path);
			break;
	}

	return file;
}

file_LL* add_file_to_list(file_LL *fileList, char*path, char*root)
{
	verb(VERB_3, "[%s] path = %s, root = %s", __func__, path, root);

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

	verb(VERB_2, "[%s] %d paths", __func__, n);

	for (int i = 0; i < n ; i++) {

		if (paths[i]) {
			verb(VERB_2, "[%s] trying %s", __func__, paths[i]);

			if (stat(paths[i], &stats) == -1) {
				ERR("unable to stat file [%s], error = %d", paths[i], errno);
			}

			if ((stats.st_mode & S_IFMT) == S_IFDIR) {
				verb(VERB_2, "[%s] dir found, traversing %s", __func__ , paths[i]);
				fileList = add_file_to_list(fileList, paths[i], paths[i]);
				char parent_dir[MAX_PATH_LEN];
				get_parent_dir(parent_dir, paths[i]);
				lsdir_to_list(fileList, paths[i], paths[i]);
			} else {
				char parent_dir[MAX_PATH_LEN];
				get_parent_dir(parent_dir, paths[i]);

				fileList = add_file_to_list(fileList, paths[i], parent_dir);
			}
		}

		// fileList = add_file_to_list(fileList, paths[i]);
	}

	verb(VERB_2, "[%s] complete, %d items in list", __func__, fileList->count);
	return fileList;
}


file_LL* build_filelist(int n, char *paths[])
{
	file_LL *fileList = NULL;
	struct stat stats;

	verb(VERB_3, "[%s] %d paths", __func__, n);

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

	verb(VERB_3, "[%s] complete", __func__);
	return fileList;
}

void lsdir_to_list(file_LL* ls_fileList, char* dir, char* root)
{
	// Verify that we were actually passed a directory file
/*    if ( !(file->mode == S_IFDIR) ){
        warn("attemped to enter a non-directory file");
        return NULL;
    } */
	verb(VERB_3, "[%s]: %s %s", __func__, dir, root);

	DIR *dirp = opendir(dir);
	struct dirent * entry;
	if ( dirp != NULL ) {
		struct stat tmpStats;

		// Iterate through each file in the directory
		while ((entry = readdir(dirp)) != NULL) {

			// If given, ignore the current and parent directories
			if ( strcmp(entry->d_name, ".") && strcmp(entry->d_name,"..") ) {
				char path[MAX_PATH_LEN];
				snprintf(path, MAX_PATH_LEN - 1, "%s/%s", dir, entry->d_name);
//				ls_fileList = add_file_to_list(ls_fileList, path, root);
				add_file_to_list(ls_fileList, path, root);

				if (stat(path, &tmpStats) == -1) {
					ERR("unable to stat file [%s]", path);
				}

				// if it's a directory, recurse
				if ( (tmpStats.st_mode & S_IFMT) == S_IFDIR ) {
					lsdir_to_list(ls_fileList, path, root);
				}
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
			snprintf(path, MAX_PATH_LEN - 1, "%s/%s", file->path, entry->d_name);
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
			verb(VERB_2, "[%s] Again find parent directory and make it", __func__);

			char parent_dir[MAX_PATH_LEN];
			get_parent_dir(parent_dir, path);

			verb(VERB_2, "[%s] Building directory path [%s]", __func__, parent_dir);

			mkdir_parent(parent_dir);
			ret = mkdir_parent(path);

		// The directory already exists
		} else if (err == EEXIST) {

			// Continue

		// Otherwise, mkdir failed
		} else {
			fprintf(stderr, "ERROR: Unable to create directory [%s]: %s", path, strerror(err));
			clean_exit(EXIT_FAILURE);
		}

	} else {
		verb(VERB_2, "[%s] Built directory %s", __func__, path);
	}

	return ret;
}

// Get the size of a file, should handle large files as well
off_t fsize(int fd)
{
	struct stat tmp_stat;
	fstat(fd, &tmp_stat);
//	size = lseek64(fd, 0L, SEEK_END);
//	lseek64(fd, 0L, SEEK_SET);
	return tmp_stat.st_size;
}

int generate_base_path(char* prelim, char *data_path, int data_path_size)
{
	// generate a base path for all destination files
	int bl = strlen(prelim);
	if (bl == 0) {
		snprintf(data_path, data_path_size - 1, "%s/", prelim);
	} else {
		if (prelim[bl-1] != '/') {
			bl++;
		}
		snprintf(data_path, data_path_size - 1, "%s/", prelim);
	}

	return bl;
}



void init_pipe_mutex(void)
{
	pthread_mutex_init(&g_pipe_mutex, NULL);
}


#define PIPE_WRITE_TIMEOUT_MS		100
//
// pipe_write
//
// writes to a given file descriptor/pipe
// is a dummy wrapper around write to better track how/when access is being done
//
ssize_t pipe_write(int fd, const void *buf, size_t count)
{
	ssize_t written_bytes = 0;
	pollfd		poll_data;

	poll_data.fd = fd;
	poll_data.events = POLLOUT | POLLRDHUP | POLLERR | POLLHUP | POLLNVAL;
	poll_data.revents = 0;

	if ( poll(&poll_data, (nfds_t)1, PIPE_WRITE_TIMEOUT_MS) > -1 ) {
		if ( poll_data.revents & POLLOUT ) {
			written_bytes = write(fd, buf, count);
/*			if ( fd == g_opts.send_pipe[1] ) {
				push_pipe_fifo(FIFO_WRITE, written_bytes);
			} else {
				push_pipe_fifo(FIFO_READ, written_bytes);
			} */
		} else {
//			verb(VERB_2, "[%s] revents: %0X", __func__, poll_data.revents);
		}
	} else {
		verb(VERB_2, "[%s] errno: %0X", __func__, errno);
	}

//	verb(VERB_2, "[%s] Written %lu bytes (%d requested) to fd %d", __func__, written_bytes, count, fd);

	return written_bytes;
}

#define PIPE_READ_TIMEOUT_MS		100
//
// pipe_read
//
// reads from a given file descriptor/pipe
// is a dummy wrapper around read to better track how/when access is being done
//
ssize_t pipe_read(int fd, void *buf, size_t count)
{
	ssize_t		read_bytes = 0;
	pollfd		poll_data;

	poll_data.fd = fd;
	poll_data.events = POLLIN | POLLPRI | POLLRDHUP | POLLERR | POLLHUP;
	poll_data.revents = 0;

//	verb(VERB_2, "[%s] polling data from pipe %d", __func__, fd);
	if ( poll(&poll_data, (nfds_t)1, PIPE_READ_TIMEOUT_MS) > -1 ) {
		if ( poll_data.revents & POLLIN ) {
//			verb(VERB_2, "[%s] requesting %d bytes", __func__, count);
			read_bytes = read(fd, buf, count);
			if ( read_bytes < 0 ) {
				fprintf(stderr, "[%s] ERROR - %s\n", __func__, strerror(errno));
				verb(VERB_2, "[%s] ERROR - %s", __func__, strerror(errno));
			}
/*			if ( fd == g_opts.send_pipe[0] ) {
				pop_pipe_fifo(FIFO_WRITE, read_bytes);
			} else {
				pop_pipe_fifo(FIFO_READ, read_bytes);
			} */
		} else {
//			verb(VERB_2, "[%s] revents: %0X", __func__, poll_data.revents);
		}
	} else {
		verb(VERB_2, "[%s] errno: %0X", __func__, errno);
	}

//	verb(VERB_2, "[%s] Read %lu bytes (%d requested) from fd %d", __func__, read_bytes, count, fd);

	return read_bytes;
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
	timespec		times[2];
	int				retVal = 0;

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
		fprintf(stderr, "ERROR: Unable to set timestamp on %s, error code %d\n", filename, retVal);
	} else {
		verb(VERB_3, "[%s] setting - mtime: %d, mtime_nsec: %ld", __func__, mtime, mtime_nsec);
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
			verb(VERB_3, "[%s] stat for file: %s, mtime: %d, mtime_nsec: %ld", __func__, filename, tmpStat.st_mtime, tmpStat.st_mtim.tv_nsec);
			*mtime = tmpStat.st_mtime;
			*mtime_nsec = tmpStat.st_mtim.tv_nsec;
			verb(VERB_3, "[%s] file: %s, mtime: %d, mtime_nsec: %ld", __func__, filename, *mtime, *mtime_nsec);
		} else {
			fprintf(stderr, "ERROR: bad pointer passed to get_mod_time\n");
		}
	}

	return retVal;

}

//
// get_filelist_size
//
// Returns the size of a file list (total, in bytes)
//
int get_filelist_size(file_LL *fileList)
{
	int total_size = 0;

	if ( fileList != NULL ) {
		file_node_t* cursor = fileList->head;
		int static_file_size = (sizeof(int) * 3) + sizeof(long int) + sizeof(struct stat);

		while ( cursor != NULL ) {
			total_size += (static_file_size + strlen(cursor->curr->filetype) + strlen(cursor->curr->path) + strlen(cursor->curr->root) + 3);  // 3 is for 3 null terminators of strings
			cursor = cursor->next;
		}
	}
	return total_size;
}


//
// pack_filelist
//
// packs a file list into a byte buffer for sending along
//
char* pack_filelist(file_LL* fileList, int total_size)
{
	verb(VERB_3, "[%s] total_size = %d", __func__, total_size);

	// malloc the space to make everything continuous
	char* packed_data = (char*)malloc(sizeof(char) * total_size);
	char* packed_data_ptr = packed_data;

	if ( fileList != NULL ) {
		file_node_t* cursor = fileList->head;
//		int static_file_size = (sizeof(int) * 3) + sizeof(long int) + sizeof(struct stat);
		while ( cursor != NULL ) {
			// copy over the static data
			memcpy(packed_data_ptr, &(cursor->curr->stats), sizeof(int));
			packed_data_ptr += sizeof(struct stat);

			memcpy(packed_data_ptr, &(cursor->curr->mode), sizeof(int));
			packed_data_ptr += sizeof(int);

			memcpy(packed_data_ptr, &(cursor->curr->length), sizeof(int));
			packed_data_ptr += sizeof(int);

			memcpy(packed_data_ptr, &(cursor->curr->mtime_sec), sizeof(int));
			packed_data_ptr += sizeof(int);

			memcpy(packed_data_ptr, &(cursor->curr->mtime_nsec), sizeof(long int));
			packed_data_ptr += sizeof(long int);

			// copy strings (remember that every C string func handles null terminators differently, kids!)
			strcpy(packed_data_ptr, cursor->curr->filetype);
			packed_data_ptr += strlen(cursor->curr->filetype) + 1;

			strcpy(packed_data_ptr, cursor->curr->path);
			packed_data_ptr += strlen(cursor->curr->path) + 1;

			strcpy(packed_data_ptr, cursor->curr->root);
			packed_data_ptr += strlen(cursor->curr->root) + 1;

			// next!
			cursor = cursor->next;
		}
	}

	return packed_data;
}


//
// unpack_filelist
//
// unpacks a file list into from byte buffer we got from another
// machine
// NOTE: we may have some 32/64 bit issues here, so they might
// need to be addressed at some point
//
file_LL* unpack_filelist(char* fileList_data, int data_length)
{
//	verb(VERB_3, "[%s] walking, data length = %d", __func__, data_length);

	file_LL* file_list = (file_LL*)malloc(sizeof(file_LL));
	file_list->head = NULL;
	file_list->tail = NULL;
	file_list->count = 0;
	int tmp_len;

	// unpack each entry
	while ( data_length > 0 ) {
		file_node_t *file_node = (file_node_t*)malloc(sizeof(file_node_t));
		file_object_t *file = (file_object_t*) malloc(sizeof(file_object_t));

		file_node->curr = file;
		file_node->next = NULL;

		memset(file, 0, sizeof(file_object_t));
		file->filetype = (char*)NULL;

		// copy the stat struct
		memcpy(&(file->stats), fileList_data, sizeof(struct stat));
		fileList_data += sizeof(struct stat);
		data_length -= sizeof(struct stat);

		// copy the mode
		memcpy(&(file->mode), fileList_data, sizeof(int));
		fileList_data += sizeof(int);
		data_length -= sizeof(int);

		// copy the length
		memcpy(&(file->length), fileList_data, sizeof(int));
		fileList_data += sizeof(int);
		data_length -= sizeof(int);

		// copy the mtime in seconds
		memcpy(&(file->mtime_sec), fileList_data, sizeof(int));
		fileList_data += sizeof(int);
		data_length -= sizeof(int);

		// copy the mtime in seconds
		memcpy(&(file->mtime_nsec), fileList_data, sizeof(long int));
		fileList_data += sizeof(long int);
		data_length -= sizeof(long int);

		// copy the strings (strdup mallocs, so these will need to be freed someday)
		file->filetype = strdup(fileList_data);
		tmp_len = strlen(file->filetype) + 1;
		fileList_data += tmp_len;
		data_length -= tmp_len;

		file->path = strdup(fileList_data);
		tmp_len = strlen(file->path) + 1;
		fileList_data += tmp_len;
		data_length -= tmp_len;

		file->root = strdup(fileList_data);
		tmp_len = strlen(file->root) + 1;
		fileList_data += tmp_len;
		data_length -= tmp_len;

		// add data to list
//		verb(VERB_2, "[%s] file = %s, mtime_sec = %d", __func__, file->path, file->mtime_sec);

		// if we're first, just make it head & tail
		if ( file_list->head == NULL ) {
			file_list->head = file_node;
			file_list->tail = file_node;
		// otherwise, tack us on the end
		} else {
			file_list->tail->next = file_node;
			file_list->tail = file_node;
		}
		file_list->count++;
	}

	return file_list;

}

//
// free_file_object
//
// frees a file object
//
void free_file_object(file_object_t* file)
{
	if ( file ) {
		// free all the malloc'ed strings
		if ( file->filetype ) {
			free(file->filetype);
		}
		if ( file->path ) {
			free(file->path);
		}
		if ( file->root ) {
			free(file->root);
		}
		// now free the file (everything else is good)
		free(file);
	}
}


//
// free_file_list
//
// frees a file list and all data along on it
//
void free_file_list(file_LL* fileList)
{
	if ( fileList ) {
		file_node_t* cursor = fileList->head;
		file_node_t* tmp_cursor = fileList->head;
		while ( cursor != NULL ) {
			// free what it's pointing at
			free_file_object(cursor->curr);
			cursor = cursor->next;

			// now free the node
			free(tmp_cursor);
			tmp_cursor = cursor;
		}
		// finally, free the list struct itself
		free(fileList);
	}
}


//
// compare_timestamps
//
// compares the timestamps of two files, returning 0 if the same
// or non-zero if not (working on how to know which is newer)
int compare_timestamps(file_object_t* file1, file_object_t* file2)
{
//    verb(VERB_2, "compare_timestamps: file1 = %s, mtime_sec/nsec = %d/%lu",
//        file1->path, file1->mtime_sec, file1->mtime_nsec);
//    verb(VERB_2, "compare_timestamps: file2 = %s, mtime_sec/nsec = %d/%lu",
//        file2->path, file2->mtime_sec, file2->mtime_nsec);
    return ( (file1->mtime_sec - file2->mtime_sec) + (file1->mtime_nsec - file2->mtime_nsec) );
}


