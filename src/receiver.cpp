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
#include "postmaster.h"
#include "sender.h"

// main loop for receiving mode, listens for headers and sorts out
// stream into files

postmaster_t*    receive_postmaster;
global_data_t    global_receive_data;

int validate_header(header_t header)
{
	int headerOk = 1;

	if ( header.type >= NUM_XFER_CMDS ) {
		headerOk = 0;
	}

	return headerOk;
}


int read_header(header_t *header)
{
	// return read(fileno(stdin), header, sizeof(header_t));
	return read(g_opts.recv_pipe[0], header, sizeof(header_t));
}

// wrapper for read
off_t read_data(void* b, int len)
{

	off_t rs, total = 0;
	char* buffer = (char*)b;

	while (total < len) {
		// rs = read(fileno(stdin), buffer+total, len - total);
		rs = read(g_opts.recv_pipe[0], buffer+total, len - total);
		total += rs;
		G_TOTAL_XFER += rs;
	}

	verb(VERB_4, "[%s] Read %d bytes from stream", __func__, total);

	return total;

}

int notify_system_ready()
{
	verb(VERB_2, "[%s] Notifying that we're up & ready", __func__);

	// Send that system is ready to receive

	header_t* header = nheader(XFER_CONTROL, 0);
	header->ctrl_msg = CTRL_RECV_READY;
	write_header(header);
	free(header);

	return RET_SUCCESS;
}


// Notify the destination that the transfer is complete
int acknowlege_complete_xfer()
{

	verb(VERB_2, "[%s] Acknowledging end of transfer", __func__);

	// Send completition header

	header_t* header = nheader(XFER_CONTROL, 0);
	header->ctrl_msg = CTRL_ACK;
	write_header(header);
	free(header);

	return RET_SUCCESS;

}


int receive_files(char*base_path)
{
	header_t header;

	while (!g_opts.socket_ready) {
		usleep(10000);
	}

	int alloc_len = BUFFER_LEN - sizeof(header_t);
	global_receive_data.data = (char*) malloc( alloc_len * sizeof(char));

	// generate a base path for all destination files and get the
	// length
	global_receive_data.bl = generate_base_path(base_path, global_receive_data.data_path);

//	notify_system_ready();

	// Read in headers and data until signalled completion
	while ( !global_receive_data.complete ) {
		if (global_receive_data.read_new_header) {
			if ((global_receive_data.rs = read_header(&header)) <= 0) {
				ERR("[%s] Bad header read, errno: %d", __func__, errno);
			}
		}

		if (global_receive_data.rs) {
			verb(VERB_2, "[%s] Dispatching message: %d", __func__, header.type);
			int postMasterStatus = dispatch_message(receive_postmaster, header, &global_receive_data);
			if ( postMasterStatus != POSTMASTER_OK ) {
				verb(VERB_1, "[%s] bad message dispatch call: %d", __func__, postMasterStatus);
			}
		}
		usleep(100);
	}

	// free up the memory on the way out
	free(global_receive_data.data);

	return 0;
}

// ###########################################################
//
// header callbacks and postmaster init below
//
// ###########################################################

//
// pst_callback_dirname
//
// routine to handle XFER_DIRNAME message

int pst_rec_callback_dirname(header_t header, global_data_t* global_data)
{

	verb(VERB_2, "[%s] Received directory header", __func__);

	// Read directory name from stream
	read_data(global_data->data_path + global_data->bl, header.data_len);

	verb(VERB_2, "[%s] Making directory: %s", __func__, global_data->data_path);

	// make directory, if any parent in directory path
	// doesnt exist, make that as well
	mkdir_parent(global_data->data_path);

	// safety reset, data block after this will fault, expect a header
	global_data->expecting_data = 0;
	global_data->read_new_header = 1;

	return 0;
}

//
// pst_callback_filename
//
// routine to handle XFER_FILENAME message

int pst_rec_callback_filename(header_t header, global_data_t* global_data)
{

	verb(VERB_2, "[%s] Received file header", __func__);

	// int f_mode = O_CREAT| O_WRONLY;
	int f_mode = O_CREAT| O_RDWR;
	int f_perm = 0666;

	// hang on to mtime data until we're done
	global_data->mtime_sec = header.mtime_sec;
	global_data->mtime_nsec = header.mtime_nsec;
	verb(VERB_3, "[%s] Header mtime: %d, mtime_nsec: %ld", __func__, global_data->mtime_sec, global_data->mtime_nsec);

	// Read filename from stream
	read_data(global_data->data_path + global_data->bl, header.data_len);

	verb(VERB_3, "[%s] Initializing file receive: %s", __func__, global_data->data_path + global_data->bl);


	global_data->fout = open(global_data->data_path, f_mode, f_perm);

	if (global_data->fout < 0) {

		// If we can't open the file, try building a
		// directory tree to it

		// Try and get a parent directory from file
		char parent_dir[MAX_PATH_LEN];
		get_parent_dir(parent_dir, global_data->data_path);

		verb(VERB_3, "[%s] Using %s as parent directory.", __func__, parent_dir);

		// Build parent directory recursively
		if (mkdir_parent(parent_dir) < 0) {
			perror("ERROR: recursive directory build failed");
		}

	}

	// If we had to build the directory path then retry file open
	if (global_data->fout < 0) {
		global_data->fout = open(global_data->data_path, f_mode, 0666);
	}

	if (global_data->fout < 0) {
		fprintf(stderr, "ERROR: %s ", global_data->data_path);
		perror("file open");
		clean_exit(EXIT_FAILURE);
	}

	// Attempt to optimize simple sequential write
	if (posix_fadvise64(global_data->fout, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE)) {
		if (g_opts.verbosity > VERB_3) {
			perror("WARNING: Unable to advise file write");
		}
	}

	global_data->read_new_header = 1;
	global_data->expecting_data = 1;
	global_data->total = 0;

	return 0;

}

//
// pst_callback_f_size
//
// routine to handle XFER_F_SIZE message

int pst_rec_callback_f_size(header_t header, global_data_t* global_data)
{

	verb(VERB_2, "[%s] Received file header", __func__);

	// read in the size of the file
	read_data(&(global_data->f_size), header.data_len);

	// Memory map attempt
	if (g_opts.mmap) {
		map_fd(global_data->fout, global_data->f_size);
	}

	return 0;

}


//
// pst_callback_complete
//
// routine to handle XFER_COMPLETE message

int pst_rec_callback_complete(header_t header, global_data_t* global_data)
{
	verb(VERB_2, "[%s] Receive completed", __func__);

	global_data->complete = 1;

	// acknowledge the complete
	acknowlege_complete_xfer();

	return 0;
}


//
// pst_callback_data
//
// routine to handle XFER_DATA message

int pst_rec_callback_data(header_t header, global_data_t* global_data)
{
	off_t rs, len;

	verb(VERB_2, "[%s] Received file header", __func__);

	if (!global_data->expecting_data) {
		fprintf(stderr, "[%s] ERROR: Out of order data block, of size %lu\n", __func__, header.data_len);
		clean_exit(EXIT_FAILURE);
	}

	// Either look to receive a whole buffer of
	// however much remains in the data block
	len = (BUFFER_LEN < (global_data->f_size - global_data->total)) ? BUFFER_LEN : (global_data->f_size - global_data->total);

	// read data buffer from stdin
	// use the memory map
	if (g_opts.mmap) {
		if ((rs = read_data(global_data->f_map + global_data->total, len)) < 0) {
			ERR("Unable to read stdin");
		}

	} else {
		if ((rs = read_data(global_data->data, len)) < 0) {
			ERR("Unable to read stdin");
		}

		// Write to file
		if ((write(global_data->fout, global_data->data, rs) < 0)) {
			perror("ERROR: unable to write to file");
			clean_exit(EXIT_FAILURE);
		}
	}

	global_data->total += rs;

//	read_header(&header);

	// Update user on progress if g_opts.progress set to true
	if (g_opts.progress) {
		print_progress(global_data->data_path, global_data->total, global_data->f_size);
	}


	return 0;

}

//
// pst_callback_data_complete
//
// routine to handle XFER_DATA_COMPLETE message

int pst_rec_callback_data_complete(header_t header, global_data_t* global_data)
{
	verb(VERB_2, "[%s] Received file header", __func__);
	// On the next loop, use the header that was just read in

	// Formatting
	if (g_opts.progress) {
		verb(VERB_2, "");
	}

	// Check to see if we received full file
	if (global_data->f_size) {
		if (global_data->total == global_data->f_size) {
//			verb(VERB_2, "[%s] Received full file %s [%li B]", __func__, global_data->data_path, global_data->total);
		} else {
			warn("Did not receive full file: %s", global_data->data_path);
		}

	} else {
		warn("Completed stream of known size");
	}

	if (ftruncate64(global_data->fout, global_data->f_size)) {
		ERR("unable to truncate file to correct size");
	}

//	global_data->read_new_header = 0;
	global_data->expecting_data = 0;
	global_data->f_size = 0;

	// Truncate the file in case it already exists and remove extra data
	if (g_opts.mmap) {
		unmap_fd(global_data->fout, global_data->f_size);
	}

	close(global_data->fout);

	// fly - now is the time when we set the timestamps
	set_mod_time(global_data->data_path, global_data->mtime_nsec, global_data->mtime_sec);

	return 0;
	}

//
// pst_rec_callback_filelist
//
// routine to handle XFER_FILELIST message
//
// fly - Ok, a few possible ways to handle this. One is dumb: walk the list and only
// change the file timestamps if we have them. That makes for a double send and a longer
// walk/compare on the other end.  Second is to create a new list that only has the
// files that are already present, but that presents an issue with needing a case for
// an empty list. For now, trying dumb, but if we have really long lists, the second
// case may be a necessity.

int pst_rec_callback_filelist(header_t header, global_data_t* global_data)
{
	file_LL*        fileList;
	struct stat     temp_stat_buffer;
	char*           cur_directory = NULL;

	memset(&temp_stat_buffer, 0, sizeof(struct stat));

	verb(VERB_3, "[%s] Received list data of size %d", __func__, header.data_len);

	char* tmp_file_list = (char*)malloc(sizeof(char) * header.data_len);

	read_data(tmp_file_list, header.data_len);
	fileList = unpack_filelist(tmp_file_list, header.data_len);
	free(tmp_file_list);

	// repopulate the list with our timestamps, if any

	verb(VERB_3, "[%s] %d elements, need to check %s for these", __func__, fileList->count, global_data->data_path);

	// if the directory exists, change to it
	// if it doesn't, the stat checks below will fail, and we'll get all zeroes
	if ( !stat(global_data->data_path, &temp_stat_buffer) ) {
		verb(VERB_3, "[%s] chdir to %s", __func__, global_data->data_path);
		cur_directory = get_current_dir_name();
		chdir(global_data->data_path);
	}

	// now, walk the list
	file_node_t* cursor = fileList->head;
	while ( cursor != NULL ) {
//		verb(VERB_3, "[%s] checking %s", __func__, cursor->curr->path);
		// remove the root directory from the destination path
		char destination[MAX_PATH_LEN];
		int root_len = strlen(cursor->curr->root);
		memset(destination, 0, MAX_PATH_LEN);

		if (!root_len || strncmp(cursor->curr->path, cursor->curr->root, root_len)) {
			sprintf(destination, "%s", cursor->curr->path);

		} else {
			memcpy(destination, cursor->curr->path + root_len + 1, strlen(cursor->curr->path) - root_len);
		}

		// check if file exists
		if ( !stat(destination, &temp_stat_buffer) ) {
			verb(VERB_3, "[%s] File %s present", __func__, destination);
			// if it's there, change the timestamp
			verb(VERB_3, "[%s] mtime = %d, mtime_nsec = %lu", __func__, temp_stat_buffer.st_mtime, temp_stat_buffer.st_mtim.tv_nsec);
			cursor->curr->mtime_sec = temp_stat_buffer.st_mtime;
			cursor->curr->mtime_nsec = temp_stat_buffer.st_mtim.tv_nsec;
		} else {
			verb(VERB_3, "[%s] File %s not found", __func__, cursor->curr->path);
			// if not, zero it out
			cursor->curr->mtime_sec = 0;
			cursor->curr->mtime_nsec = 0;
		}
		cursor = cursor->next;
	}
	verb(VERB_3, "[%s] Done walking", __func__);

	// return the list
	// get size of list and such
	int totalSize = get_filelist_size(fileList);

	while (!g_opts.socket_ready) {
		verb(VERB_3, "[%s] Socket not ready, waiting", __func__);
		usleep(10000);
	}

	verb(VERB_3, "[%s] Sending back", __func__);
	send_filelist(fileList, totalSize);

	// change directory back
	if ( cur_directory ) {
		chdir(cur_directory);
	}

	// free the file list
	free_file_list(fileList);

	return 0;
}

void init_receiver()
{
	verb(VERB_3, "[%s] Initializing receiver", __func__);

	// initialize the data
	global_receive_data.f_size = 0;
	global_receive_data.complete = 0;
	global_receive_data.expecting_data = 0;
	global_receive_data.read_new_header = 1;

	// create the postmaster
	receive_postmaster = create_postmaster();

	// register the callbacks
	register_callback(receive_postmaster, XFER_DIRNAME, pst_rec_callback_dirname);
	register_callback(receive_postmaster, XFER_FILENAME, pst_rec_callback_filename);
	register_callback(receive_postmaster, XFER_F_SIZE, pst_rec_callback_f_size);
	register_callback(receive_postmaster, XFER_COMPLETE, pst_rec_callback_complete);
	register_callback(receive_postmaster, XFER_DATA, pst_rec_callback_data);
	register_callback(receive_postmaster, XFER_DATA_COMPLETE, pst_rec_callback_data_complete);
	register_callback(receive_postmaster, XFER_FILELIST, pst_rec_callback_filelist);

	verb(VERB_3, "[%s] Done initializing receiver", __func__);

}

void cleanup_receiver()
{
	if ( receive_postmaster != NULL ) {
		free(receive_postmaster);
	}

}

