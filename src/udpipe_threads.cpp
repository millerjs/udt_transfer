/*****************************************************************************
Copyright 2013 Laboratory for Advanced Computing at the University of Chicago

This file is part of udpipe.

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
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <iostream>
#include <pthread.h>
#include <sys/types.h>

#include <udt.h>

#include "udpipe.h"
#include "udpipe_threads.h"
#include "thread_manager.h"
#include "parcel.h"

#define DEBUG 0
#define EXIT_FAILURE 1

#define prii(x) fprintf(stderr,"debug:%d\n",x)
#define pris(x) fprintf(stderr,"debug: %s\n",x)
#define prisi(x,y) fprintf(stderr,"%s: %d\n",x,y)
#define uc_err(x) {fprintf(stderr,"error:%s\n",x);exit(EXIT_FAILURE);}

const int ECONNLOST = 2001;

using std::cerr;
using std::endl;

int READ_IN = 0;

int g_timeout_sem;
int g_timeout_len;



void kick_monitor(void)
{
//	verb(VERB_2, "[%s] Monitor kicked", __func__);
	g_timeout_sem = time(NULL) + g_timeout_len;
}

int check_monitor_timeout(void)
{
	return (g_timeout_sem - time(NULL));
}

void *monitor_timeout(void* arg) {

//	int timeout = *(int*) arg;

	while (1) {
//		sleep(timeout);
//		sleep(1);
		usleep(100);
		if (check_monitor_timeout() <= 0){
			verb(VERB_2, "[%s] Timeout triggered, causing exit", __func__);
//			fprintf(stderr, "Exiting on timeout.\n");
			unregister_thread(get_my_thread_id());
			set_thread_exit();
//			exit(0);
			break;

		} else {
			// continue on as normal
		}

		// If g_timeout_sem == 2, the connection has not been made -> no timeout next round
/*		if (g_timeout_sem != 2) {
			g_timeout_sem = 0;
		} */

		if ( check_for_exit(THREAD_TYPE_2) ) {
			verb(VERB_2, "[%s] Got exit signal, exiting", __func__);
			unregister_thread(get_my_thread_id());
			break;
		}
	}
	return 0;
}

void init_monitor(time_t timeout_len)
{
	g_timeout_len = timeout_len;
	kick_monitor();

}



void send_full(UDTSOCKET sock, char* buffer, int len)
{
	int sent = 0;
	int rs = 0;
	verb(VERB_2, "[%s] Attempting to send %d bytes", __func__, len);
	while (sent < len) {
		rs = UDT::send(sock, buffer+sent, len-sent, 0);
//		verb(VERB_2, "[%s] sent %d bytes", __func__, rs);
		if (UDT::ERROR == rs) {
			if (UDT::getlasterror().getErrorCode() != ECONNLOST) {
				cerr << "recv:" << UDT::getlasterror().getErrorMessage() <<
					"send_full: Unable to send data." << endl;
				exit(1);
			}
		}
		sent += rs;
	}
}

void recv_full(UDTSOCKET sock, char* buffer, int len)
{
	int recvd = 0;
	int rs = 0;
	verb(VERB_2, "[%s] Attempting to receive %d bytes", __func__, len);
	while (recvd < len) {
		rs = UDT::recv(sock, buffer+recvd, len-recvd, 0);
//		rs = UDT::recv(sock, buffer+recvd, 1, 0);
//		verb(VERB_2, "[%s] received %d bytes", __func__, rs);
//		print_bytes(buffer, recvd + rs, 16);
		if (UDT::ERROR == rs) {
			if (UDT::getlasterror().getErrorCode() != ECONNLOST) {
				cerr << "recv:" << UDT::getlasterror().getErrorMessage() <<
					"send_full: Unable to send data." << endl;
				exit(1);
			}
		}
		kick_monitor();
//		g_timeout_sem = 1;
		recvd += rs;
	}
}

const int KEY_LEN = 1026;
//const int KEY_LEN = 64;
//int g_signed_auth = 0;
//int g_authed_peer = 0;


void auth_peer(rs_args* args)
{
	char auth_peer_key[KEY_LEN];
	char signed_key[KEY_LEN];
//	char decoded_signed_key[KEY_LEN];

	RAND_bytes((unsigned char*)auth_peer_key, KEY_LEN);
	verb(VERB_2, "[%s] Generated key:", __func__);
//	print_bytes(auth_peer_key, KEY_LEN, 16);

//	g_authed_peer = 0;

	verb(VERB_2, "[%s] Sending data", __func__);
	send_full(*args->usocket, auth_peer_key, KEY_LEN);

//	while (!g_signed_auth);

	verb(VERB_2, "[%s] Receiving data", __func__);
	recv_full(*args->usocket, signed_key, KEY_LEN);

	verb(VERB_2, "[%s] Received signed key:", __func__);
//	print_bytes(signed_key, KEY_LEN, 16);

	int crypt_len = KEY_LEN/args->n_crypto_threads;
	for (int i = 0; i < args->n_crypto_threads; i ++) {
		verb(VERB_2, "[%s] Passing data to encode/decode thread: %x in, %x out, i = %d, crypt_len = %d", __func__,
			signed_key+(i*crypt_len), signed_key+(i*crypt_len), i, crypt_len);
		pass_to_enc_thread(signed_key+(i*crypt_len), signed_key+(i*crypt_len),
				crypt_len, args->c);
	}

	join_all_encryption_threads(args->c);

	if (memcmp(auth_peer_key, signed_key, KEY_LEN)) {
		verb(VERB_1, "[%s] Authorization failed", __func__);
/*		verb(VERB_2, "key (%x):", auth_peer_key);
		print_bytes(auth_peer_key, KEY_LEN, 16);
		verb(VERB_2, "signed_key (%x):", signed_key);
		print_bytes(signed_key, KEY_LEN, 16); */
//		set_thread_exit();
		exit(1);
	} else {
		verb(VERB_2, "[%s] Key signed OK", __func__);
//		set_encrypt_ready(1);
		set_peer_authed();
//		g_authed_peer = 1;
	}
}


void sign_auth(rs_args* args)
{
	char sign_auth_key[KEY_LEN];
//	char encoded_sign_auth_key[KEY_LEN];

//	g_signed_auth = 0;
	verb(VERB_2, "[%s] Receiving data", __func__);
	// appears to try and receive a key
	recv_full(*args->usocket, sign_auth_key, KEY_LEN);

	verb(VERB_2, "[%s] received key:", __func__);
//	print_bytes(sign_auth_key, KEY_LEN, 16);

	// pass the key to the encode thread
	int crypt_len = KEY_LEN/args->n_crypto_threads;
	for (int i = 0; i < args->n_crypto_threads; i ++) {
		pass_to_enc_thread(sign_auth_key+(i*crypt_len), sign_auth_key+(i*crypt_len),
				crypt_len, args->c);
	}

	join_all_encryption_threads(args->c);

	verb(VERB_2, "[%s] signed key:", __func__);
//	print_bytes(sign_auth_key, KEY_LEN, 16);

	verb(VERB_2, "[%s] Sending data back", __func__);
	// send the key back
	send_full(*args->usocket, sign_auth_key, KEY_LEN);

	// set the g_signed_auth to true
//	g_signed_auth = 1;
	set_auth_signed();

}


void* recvdata(void * _args)
{
	int running = 1;
	pthread_t   tid;
	tid = pthread_self();

	rs_args * args = (rs_args*)_args;

	verb(VERB_2, "[%s %lu] Initializing receive thread, args->c = %0x", __func__, tid, args->c);

	if (args->use_crypto) {
		verb(VERB_2, "[%s %lu] Receive encryption is on.", __func__, tid);
		if ( args->c == NULL ) {
			fprintf(stderr, "[%s %lu] crypto is NULL on enter, exiting!", __func__, tid);
			exit(0);
		}
	}

	UDTSOCKET recver = *args->usocket;

	int crypto_buff_len = BUFF_SIZE / args->n_crypto_threads;
	int buffer_cursor;

	char* indata = (char*) malloc(BUFF_SIZE*sizeof(char));
	if (!indata) {
		fprintf(stderr, "Unable to allocate decryption buffer");
		exit(EXIT_FAILURE);
	}

	if ( (args->use_crypto) ) {
		if ( args->master ) {
			verb(VERB_2, "[%s %lu] Authorizing peer with key (%x)", __func__, tid, args->master);
			auth_peer(args);
		} else {
			verb(VERB_2, "[%s %lu] Waiting for authed to be signed (%x)...", __func__, tid, args->master);
			while (!get_auth_signed());
			verb(VERB_2, "[%s %lu] Authorizing peer with key (%x)", __func__, tid, args->master);
			auth_peer(args);
		}

	}

	// wait until we're actually done signing
	while ( !get_encrypt_ready() );

//	g_timeout_sem = 2;

	// Create a monitor thread to watch for timeouts
	if (args->timeout > 0) {
		pthread_t monitor_thread;
		init_monitor(args->timeout);
		create_thread(&monitor_thread, NULL, &monitor_timeout, &args->timeout, "monitor_timeout", THREAD_TYPE_2);
	}

	READ_IN = 1;

	int new_block = 1;
	int block_size = 0;
	int offset = sizeof(int)/sizeof(char);
	int crypto_cursor;
	pthread_mutex_t recv_thread_mutex;

	pthread_mutex_init(&recv_thread_mutex, NULL);

	verb(VERB_2, "[%s %lu] Listening on receive thread, args->c = %0x", __func__, tid, args->c);

	// Set monitor thread to expect a timeout
	kick_monitor();

	if(args->use_crypto) {
		verb(VERB_2, "[%s %lu] Entering crypto loop...", __func__, tid);
		if ( args->c != NULL ) {
			while(running) {
				pthread_mutex_lock(&recv_thread_mutex);
				int rs;
				if (new_block) {
					block_size = 0;
					rs = UDT::recv(recver, (char*)&block_size, offset, 0);
					if (UDT::ERROR == rs) {
						if (UDT::getlasterror().getErrorCode() != ECONNLOST) {
							cerr << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
						}
						running = 0;
					}
					if ( (rs > 0) && block_size ) {
						verb(VERB_2, "[%s %lu] new block, expecting size = %d", __func__, tid, block_size);
						new_block = 0;
						buffer_cursor = 0;
						crypto_cursor = 0;
					}
				}

				rs = UDT::recv(recver, indata+buffer_cursor,
						block_size-buffer_cursor, 0);
				if ( rs > 0 ) {
					verb(VERB_2, "[%s %lu] received %d bytes", __func__, tid, rs);
				}

				if (UDT::ERROR == rs) {
					if (UDT::getlasterror().getErrorCode() != ECONNLOST) {
						cerr << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
					}
					running = 0;
				}

				// Cancel timeout for another args->timeout seconds
				kick_monitor();
				if ( rs > 0 ) {
					buffer_cursor += rs;
				}
				
				// Decrypt any full encryption buffer sectors
				while (crypto_cursor + crypto_buff_len < buffer_cursor) {
					verb(VERB_2, "[%s %lu] decrypting full encryption buffer sectors", __func__, tid);
					pass_to_enc_thread(indata+crypto_cursor, indata+crypto_cursor,
							   crypto_buff_len, args->c);
					crypto_cursor += crypto_buff_len;
				}

				// If we received the whole block
				if (buffer_cursor == block_size) {
					if ( block_size ) {
						int size = buffer_cursor - crypto_cursor;
						if ( args->c != NULL ) {
							verb(VERB_2, "[%s %lu] block complete, decrypting size %d", __func__, tid, size);
							pass_to_enc_thread(indata+crypto_cursor, indata+crypto_cursor,
									   size, args->c);
							crypto_cursor += size;
							join_all_encryption_threads(args->c);
							pipe_write(args->recv_pipe[1], indata, block_size);
							buffer_cursor = 0;
							crypto_cursor = 0;
							new_block = 1;
							verb(VERB_2, "[%s %lu] setting new block flag and looping", __func__, tid);
						} else {
							fprintf(stderr, "[%s %lu] crypto class is NULL before thread, exiting!\n", __func__, tid);
							running = 0;
						}
					} else {
							buffer_cursor = 0;
							crypto_cursor = 0;
							new_block = 1;
					}
				}
				// fly - checking new_block to make sure last block is finished before we exit
				if ( check_for_exit(THREAD_TYPE_2) && new_block ) {
					verb(VERB_2, "[%s %lu] Got exit signal, exiting", __func__, tid);
					running = 0;
				}
				pthread_mutex_unlock(&recv_thread_mutex);
			}
		}  else {
			fprintf(stderr, "crypto class is NULL, exiting!\n");
		}
	} else {
		tid = pthread_self();
		verb(VERB_2, "[%s %lu] Entering non-crypto loop...", __func__, tid);
		int rs;
		while (running) {
			pthread_mutex_lock(&recv_thread_mutex);
			rs = UDT::recv(recver, indata, BUFF_SIZE, 0);
			if (UDT::ERROR == rs) {
				if (UDT::getlasterror().getErrorCode() != ECONNLOST) {
					cerr << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
					verb(VERB_2, "[%s %lu] Exiting on error 1...", __func__, tid);
					running = 0;
				}
				verb(VERB_2, "[%s %lu] Connection lost, exiting", __func__, tid);
				running = 0;
			}

			if ( check_for_exit(THREAD_TYPE_2) ) {
				verb(VERB_2, "[%s %lu] Got exit signal, exiting", __func__, tid);
				running = 0;
			}

			kick_monitor();
			if ( rs > 0 ) {
				verb(VERB_2, "[%s %lu] Writing %d bytes to pipe %d", __func__, tid, rs, args->recv_pipe[1]);
				pipe_write(args->recv_pipe[1], indata, rs);
			}
			pthread_mutex_unlock(&recv_thread_mutex);
		}
	}

	verb(VERB_2, "[%s %lu] Closing up and heading out...", __func__, tid);
//	UDT::close(recver);

	free(indata);
	unregister_thread(get_my_thread_id());
	set_thread_exit();
	pthread_mutex_destroy(&recv_thread_mutex);
	return NULL;
}

void senddata_cleanup_handler(void *arg)
{
	verb(VERB_2, "[senddata_cleanup_handler] Cleaning up on way out");

}

#define DEBUG_

void* senddata(void* _args)
{
	rs_args * args = (rs_args*) _args;
	pthread_t   tid;
//	int error = 0;
	int running = 1;
	pthread_mutex_t send_thread_mutex;

	pthread_cleanup_push(senddata_cleanup_handler, NULL);

	tid = pthread_self();
	verb(VERB_2, "[%s %lu] Initializing send thread...", __func__, tid);

	UDTSOCKET client = *(UDTSOCKET*)args->usocket;

	if (args->use_crypto) {
		verb(VERB_2, "[%s %lu] Send encryption is on.", __func__, tid);
	}

	char* outdata = (char*)malloc(BUFF_SIZE*sizeof(char));

	int crypto_buff_len = BUFF_SIZE / args->n_crypto_threads;

	int offset = sizeof(int)/sizeof(char);
	int bytes_read;

	// verifies that we can encrypt/decrypt
	if ( args->use_crypto ) {
		if ( !args->master ) {
			verb(VERB_2, "[%s %lu] Sending encryption status (%x)...", __func__, tid, args->master);
			sign_auth(args);
		} else {
			verb(VERB_2, "[%s %lu] Waiting for peer to be authed (%x)...", __func__, tid, args->master);
			while (!get_peer_authed());
			verb(VERB_2, "[%s %lu] Sending encryption status (%x)...", __func__, tid, args->master);
			sign_auth(args);
		}
	}

	// wait until we're actually done signing
	while ( !get_encrypt_ready() );

	// long local_openssl_version;
	// if (args->use_crypto)
	// 	local_openssl_version = OPENSSL_VERSION_NUMBER;
	// else
	// 	local_openssl_version = 0;


	// if (UDT::send(client, (char*)&local_openssl_version, sizeof(long), 0) < 0){
	// 	    // cerr << "send:" << UDT::getlasterror().getErrorMessage() << endl;
	// 	    // UDT::close(client);
	// 	    // exit(1);
	// }

	while (!READ_IN);

	verb(VERB_2, "[%s %lu] Send thread listening on stdin.", __func__, tid);

	pthread_mutex_init(&send_thread_mutex, NULL);


	if (args->use_crypto) {
		verb(VERB_2, "[%s %lu] Entering crypto loop", __func__, tid);
		while(running) {
			pthread_mutex_lock(&send_thread_mutex);
			int ss;
			bytes_read = pipe_read(args->send_pipe[0], outdata+offset, (BUFF_SIZE - offset));

			if(bytes_read < 0) {
				if ( errno != EBADF ) {
					verb(VERB_1, "[%s %lu] Error on read: %d",  __func__, tid, errno);
				}
				running = 0;
			}

			if(bytes_read == 0) {
				if ( check_for_exit(THREAD_TYPE_2) ) {
					verb(VERB_2, "[%s %lu] Got exit signal, exiting", __func__, tid);
					running = 0;
				} else {
				}
				usleep(100);
				pthread_mutex_unlock(&send_thread_mutex);
				continue;
			}

			*((int*)outdata) = bytes_read;
			int crypto_cursor = 0;

			while (crypto_cursor < bytes_read) {
				int size = min(crypto_buff_len, bytes_read-crypto_cursor);
				verb(VERB_2, "[%s %lu] Passing %d data to encode thread", __func__, tid, size);
				pass_to_enc_thread(outdata+crypto_cursor+offset,
						   outdata+crypto_cursor+offset,
						   size, args->c);

				crypto_cursor += size;
			}

			join_all_encryption_threads(args->c);
			bytes_read += offset;

			int ssize = 0;
			while(ssize < bytes_read) {
				if (UDT::ERROR == (ss = UDT::send(client, outdata + ssize,
								  bytes_read - ssize, 0))) {
					verb(VERB_1, "[%s %lu] Error on send: (%d) %s", __func__, tid, errno, strerror(errno));
					running = 0;
					break;
				}
				ssize += ss;
			}

			kick_monitor();

			if ( check_for_exit(THREAD_TYPE_2) ) {
				verb(VERB_2, "[%s %lu] Got exit signal, exiting", __func__, tid);
				running = 0;
			}
			pthread_mutex_unlock(&send_thread_mutex);
		}

	} else {
		verb(VERB_2, "[%s %lu] Entering non-crypto loop", __func__, tid);
		while (running) {
			pthread_mutex_lock(&send_thread_mutex);
			kick_monitor();

			bytes_read = pipe_read(args->send_pipe[0], outdata, BUFF_SIZE);
			int ssize = 0;
			int ss;

			while(ssize < bytes_read) {
				if (UDT::ERROR == (ss = UDT::send(client, outdata + ssize,
								  bytes_read - ssize, 0))) {
					verb(VERB_1, "[%s %lu] Error on send: (%d) %s", __func__, tid, errno, strerror(errno));
					running = 0;
					break;
				}
				ssize += ss;
			}
			if ( check_for_exit(THREAD_TYPE_2) ) {
				verb(VERB_2, "[%s %lu] Got exit signal, exiting", __func__, tid);
				running = 0;
			}
			pthread_mutex_unlock(&send_thread_mutex);
		}
	}

	sleep(1);
	verb(VERB_2, "[%s %lu] Freeing data & exiting", __func__, tid);
	free(outdata);
//	close(args->send_pipe[0]);
	unregister_thread(get_my_thread_id());
	pthread_cleanup_pop(0);
	pthread_mutex_destroy(&send_thread_mutex);
	return NULL;
}


void* monitor(void* s)
{
	UDTSOCKET u = *(UDTSOCKET*)s;

	UDT::TRACEINFO perf;

	cerr << "Snd(Mb/s)\tRcv(Mb/s)\tRTT(ms)\tLoss\tPktSndPeriod(us)\tRecvACK\tRecvNAK" << endl;

	while (true) {
		usleep(100);
//		sleep(1);

		if (UDT::ERROR == UDT::perfmon(u, &perf)) {
			cout << "perfmon: " << UDT::getlasterror().getErrorMessage() << endl;
			break;
		}

		cerr << perf.mbpsSendRate << "\t\t"
			 << perf.mbpsRecvRate << "\t\t"
			 << perf.msRTT << "\t"
			 << perf.pktRcvLoss << "\t"
			 << perf.pktRecv << "\t\t\t"
			 << perf.pktRecvACK << "\t"
			 << perf.pktRecvNAK << endl;
	}
	unregister_thread(get_my_thread_id());
	return NULL;

}
