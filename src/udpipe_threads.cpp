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

#include <udt.h>

#include "udpipe.h"
#include "udpipe_threads.h"

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

int timeout_sem;
void *monitor_timeout(void* arg) {

    int timeout = *(int*) arg;

    while (1){

	sleep(timeout);
	if (timeout_sem == 0){
	    fprintf(stderr, "Exiting on timeout.\n");
	    exit(0);

	} else {
	    // continue on as normal
	}

	// If timeout_sem == 2, the connection has not been made -> no timeout next round
	if (timeout_sem != 2)
	    timeout_sem = 0;

    }
}



void send_full(UDTSOCKET sock, char* buffer, int len){  
    
    int sent = 0;
    int rs = 0;
    while (sent < len){
	rs = UDT::send(sock, buffer+sent, len-sent, 0);
	if (UDT::ERROR == rs) {
	    if (UDT::getlasterror().getErrorCode() != ECONNLOST)
		cerr << "recv:" << UDT::getlasterror().getErrorMessage() << 
		    "send_full: Unable to send data." << endl;
	    exit(1);    
	}
	sent += rs;
    }
    
    
}

void recv_full(UDTSOCKET sock, char* buffer, int len)
{
    
    int recvd = 0;
    int rs = 0;
    while (recvd < len){
	rs = UDT::recv(sock, buffer+recvd, len-recvd, 0);
	if (UDT::ERROR == rs) {
	    if (UDT::getlasterror().getErrorCode() != ECONNLOST)
		cerr << "recv:" << UDT::getlasterror().getErrorMessage() << 
		    "send_full: Unable to send data." << endl;
	    exit(1);    
	}
	timeout_sem = 1;
	recvd += rs;
    }
    
    
}

const int KEY_LEN = 1026;
int signed_auth;


void auth_peer(rs_args* args)
{

    char key[KEY_LEN];
    char signed_key[KEY_LEN];

    RAND_bytes((unsigned char*)key, KEY_LEN);

    signed_auth = 0;

    send_full(*args->usocket, key, KEY_LEN);

    while (!signed_auth);

    recv_full(*args->usocket, signed_key, KEY_LEN);

    int crypt_len = KEY_LEN/args->n_crypto_threads;
    for (int i = 0; i < args->n_crypto_threads; i ++)
	pass_to_enc_thread(signed_key+i*crypt_len, signed_key+i*crypt_len, 
			   crypt_len, args->c);

    join_all_encryption_threads(args->c);


    if (memcmp(key, signed_key, KEY_LEN)){
	fprintf(stderr, "Authorization failed\n");
	exit(1);

    }

}


void sign_auth(rs_args* args)
{
    
    char key[KEY_LEN];

    recv_full(*args->usocket, key, KEY_LEN);

    int crypt_len = KEY_LEN/args->n_crypto_threads;
    for (int i = 0; i < args->n_crypto_threads; i ++)
	pass_to_enc_thread(key+i*crypt_len, key+i*crypt_len, 
			   crypt_len, args->c);

    join_all_encryption_threads(args->c);

    send_full(*args->usocket, key, KEY_LEN);

    signed_auth = 1;

}


void* recvdata(void * _args)
{

    rs_args * args = (rs_args*)_args;
    
    if (args->verbose)
	fprintf(stderr, "[recv thread] Initializing receive thread...\n");

    if (args->verbose && args->use_crypto)
	fprintf(stderr, "[recv thread] Receive encryption is on.\n");

    UDTSOCKET recver = *args->usocket;

    int crypto_buff_len = BUFF_SIZE / args->n_crypto_threads;
    int buffer_cursor;

    char* indata = (char*) malloc(BUFF_SIZE*sizeof(char));
    if (!indata){
	fprintf(stderr, "Unable to allocate decryption buffer");
	exit(EXIT_FAILURE);
    }

    if (args->use_crypto)
	auth_peer(args);

    timeout_sem = 2;

    // Create a monitor thread to watch for timeouts
    if (args->timeout > 0){
	pthread_t monitor_thread;
	pthread_create(&monitor_thread, NULL, &monitor_timeout, &args->timeout);

    }


    READ_IN = 1;

    int new_block = 1;
    int block_size = 0;
    int offset = sizeof(int)/sizeof(char);
    int crypto_cursor;

    if (args->verbose)
	fprintf(stderr, "[recv thread] Listening on receive thread.\n");

    // Set monitor thread to expect a timeout
    if (args->timeout)
	timeout_sem = 1;

    if(args->use_crypto) {
	while(true) {
	    int rs;

	    if (new_block){

		block_size = 0;
		rs = UDT::recv(recver, (char*)&block_size, offset, 0);

		if (UDT::ERROR == rs) {
		    if (UDT::getlasterror().getErrorCode() != ECONNLOST){
			cerr << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
			exit(1);			
		    }
		    exit(0);
		}

		new_block = 0;
		buffer_cursor = 0;
		crypto_cursor = 0;

	    }	
	    
	    rs = UDT::recv(recver, indata+buffer_cursor, 
			   block_size-buffer_cursor, 0);


	    if (UDT::ERROR == rs) {
		if (UDT::getlasterror().getErrorCode() != ECONNLOST){
		    cerr << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
		    exit(1);			
		}
		exit(0);
	    }

	    // Cancel timeout for another args->timeout seconds
	    if (args->timeout)
		timeout_sem = 1;

	    buffer_cursor += rs;

	    // Decrypt any full encryption buffer sectors
	    while (crypto_cursor + crypto_buff_len < buffer_cursor){

		pass_to_enc_thread(indata+crypto_cursor, indata+crypto_cursor, 
				   crypto_buff_len, args->c);
		crypto_cursor += crypto_buff_len;

	    }

	    // If we received the whole block
	    if (buffer_cursor == block_size){
		
		int size = buffer_cursor - crypto_cursor;
		pass_to_enc_thread(indata+crypto_cursor, indata+crypto_cursor, 
				   size, args->c);
		crypto_cursor += size;

		join_all_encryption_threads(args->c);

		write(args->recv_pipe[1], indata, block_size);

		buffer_cursor = 0;
		crypto_cursor = 0;
		new_block = 1;
	    
	    } 
	}

    } else { 

	int rs;
	while (1){

	    rs = UDT::recv(recver, indata, BUFF_SIZE, 0);

	    if (UDT::ERROR == rs) {
		if (UDT::getlasterror().getErrorCode() != ECONNLOST){
		    cerr << "recv:" << UDT::getlasterror().getErrorMessage() << endl;
		    exit(1);			
		}
		exit(0);
	    }

	    timeout_sem = 1;	
	    write(args->recv_pipe[1], indata, rs);	

	}
	

    }

    UDT::close(recver);
    return NULL;

}


void* senddata(void* _args)
{
    rs_args * args = (rs_args*) _args;
    
    if (args->verbose)
	fprintf(stderr, "[send thread] Initializing send thread...\n");

    UDTSOCKET client = *(UDTSOCKET*)args->usocket;

    if (args->verbose && args->use_crypto)
	fprintf(stderr, "[send thread] Send encryption is on.\n");

    char* outdata = (char*)malloc(BUFF_SIZE*sizeof(char));

    int crypto_buff_len = BUFF_SIZE / args->n_crypto_threads;
    
    int	offset = sizeof(int)/sizeof(char);
    int bytes_read;

    if (args->verbose)
	fprintf(stderr, "[send thread] Sending encryption status...\n");

    if (args->use_crypto)
	sign_auth(args);

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
	
    if (args->verbose)
	fprintf(stderr, "[send thread] Send thread listening on stdin.\n");

    if (args->use_crypto){

	while(true) {
	    int ss;

	    bytes_read = read(args->send_pipe[0], outdata+offset, BUFF_SIZE);
	
	    if(bytes_read < 0){
		cerr << "send:" << UDT::getlasterror().getErrorMessage() << endl;
		UDT::close(client);
		exit(1);
	    }

	    if(bytes_read == 0) {
		sleep(1);
		UDT::close(client);
		return NULL;
	    }
	
	    if(args->use_crypto){

		*((int*)outdata) = bytes_read;
		int crypto_cursor = 0;

		while (crypto_cursor < bytes_read){
		    int size = min(crypto_buff_len, bytes_read-crypto_cursor);
		    pass_to_enc_thread(outdata+crypto_cursor+offset, 
				       outdata+crypto_cursor+offset, 
				       size, args->c);
		
		    crypto_cursor += size;
		
		}
	    
		join_all_encryption_threads(args->c);

		bytes_read += offset;

	    }

	    int ssize = 0;
	    while(ssize < bytes_read) {
	    
		if (UDT::ERROR == (ss = UDT::send(client, outdata + ssize, 
						  bytes_read - ssize, 0))) {

		    cerr << "send:" << UDT::getlasterror().getErrorMessage() << endl;
		    return NULL;
		}

		ssize += ss;

	    }


	}

    } else {
	
	while (1) {

	    bytes_read = read(args->send_pipe[0], outdata, BUFF_SIZE);
	    int ssize = 0;
	    int ss;

	    if(bytes_read == 0) {

	      // fprintf(stderr, "Transfer complete\n");
	      
	      // sleep (1);

	      //UDT::close(client);
	      //UDT::close(*args->usocket);

	      return NULL;

	      //exit(0);
	    }


	    while(ssize < bytes_read) {

		if (UDT::ERROR == (ss = UDT::send(client, outdata + ssize, 
						  bytes_read - ssize, 0))) {
		    cerr << "send:" << UDT::getlasterror().getErrorMessage() << endl;
		    return NULL;
		}

		ssize += ss;

	    }

	}	
    }

    sleep(1);

    return NULL;
}


void* monitor(void* s)
{
    UDTSOCKET u = *(UDTSOCKET*)s;

    UDT::TRACEINFO perf;

    cerr << "Snd(Mb/s)\tRcv(Mb/s)\tRTT(ms)\tLoss\tPktSndPeriod(us)\tRecvACK\tRecvNAK" << endl;

    while (true) {
	sleep(1);

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

    return NULL;
 
}

