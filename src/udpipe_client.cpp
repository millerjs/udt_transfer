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
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#include <udt.h>

#include "udpipe.h"
#include "udpipe_client.h"

#include "parcel.h"

#define prii(x) fprintf(stderr,"debug:%d\n",x)
#define pris(x) fprintf(stderr,"debug: %s\n",x)
#define uc_err(x) {fprintf(stderr,"error:%s\n",x);exit(1);}

#define ENOSERVER 1001

using std::cerr;
using std::endl;

void *run_client(void *_args_)
{

    thread_args *args = (thread_args*) _args_;

    if (args->verbose)
	fprintf(stderr, "[client] Running client...\n");

    char *ip = args->ip; 
    char *port = args->port;
    int blast = args->blast;
    int blast_rate = args->blast_rate;
    int udt_buff = args->udt_buff;
    int udp_buff = args->udp_buff; // 67108864;
    int mss = args->mss;

    if (args->verbose)
	fprintf(stderr, "Starting UDT...\n");

    UDT::startup();

    struct addrinfo hints, *local, *peer;

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (0 != getaddrinfo(NULL, port, &hints, &local)){
	cerr << "incorrect network address.\n" << endl;
	return NULL;
    }
    

    if (args->verbose)
	fprintf(stderr, "[client] Creating socket...\n");

    
    UDTSOCKET client;
    bool NOT_CONNECTED = true;
    int connectionAttempts = 0;
    const int MAX_CONNECTION_ATTEMPTS = 25;

    while (NOT_CONNECTED && connectionAttempts < MAX_CONNECTION_ATTEMPTS){

	client = UDT::socket(local->ai_family, local->ai_socktype, local->ai_protocol);

	// UDT Options
	if (blast)
	    UDT::setsockopt(client, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
	
	UDT::setsockopt(client, 0, UDT_MSS, &mss, sizeof(int));
	UDT::setsockopt(client, 0, UDT_SNDBUF, &udt_buff, sizeof(int));
	UDT::setsockopt(client, 0, UDP_SNDBUF, &udp_buff, sizeof(int));

	// freeaddrinfo(local);

	if (0 != getaddrinfo(ip, port, &hints, &peer)) {
	    cerr << "incorrect server/peer address. " << ip << ":" << port << endl;
	    return NULL;
	}

	if (args->verbose)
	    fprintf(stderr, "[client] Connecting to server...\n");
    
	if (UDT::ERROR == UDT::connect(client, peer->ai_addr, peer->ai_addrlen)) {
	
	    // cerr << "connect: " << UDT::getlasterror().getErrorCode() << endl;
	    cerr << "connect: " << UDT::getlasterror().getErrorMessage() << endl;

	    if (UDT::getlasterror().getErrorCode() != ENOSERVER)
		return NULL;
	    else
		connectionAttempts ++;
	    
	} else {
	    NOT_CONNECTED = false;
	}

    }

    if (args->verbose)
	fprintf(stderr, "[client] Creating receive thread...\n");

    pthread_t rcvthread, sndthread;
    rs_args rcvargs;
    rcvargs.usocket = new UDTSOCKET(client);
    rcvargs.use_crypto = args->use_crypto;
    rcvargs.verbose = args->verbose;
    rcvargs.n_crypto_threads = args->n_crypto_threads;
    rcvargs.c = args->dec;
    rcvargs.timeout = args->timeout;

    if (args->send_pipe && args->recv_pipe){
        rcvargs.recv_pipe = args->recv_pipe;
        rcvargs.send_pipe = args->send_pipe;
    } else {
        fprintf(stderr, "[udpipe_server] send pipe uninitialized\n");
        exit(1);
    }

    pthread_create(&rcvthread, NULL, recvdata, &rcvargs);
    pthread_detach(rcvthread);

    if (args->verbose)
	fprintf(stderr, "[client] Creating send thread...\n");

    rs_args send_args;
    send_args.usocket = new UDTSOCKET(client);
    send_args.use_crypto = args->use_crypto;
    send_args.verbose = args->verbose;
    send_args.n_crypto_threads = args->n_crypto_threads;
    send_args.c = args->enc;
    send_args.timeout = args->timeout;

    if (args->send_pipe && args->recv_pipe){
        send_args.send_pipe = args->send_pipe;
        send_args.recv_pipe = args->recv_pipe;
    } else {
        fprintf(stderr, "[udpipe_server] send pipe uninitialized\n");
        exit(1);
    }


    // freeaddrinfo(peer);

    if (blast) {
	CUDPBlast* cchandle = NULL;
	int temp;
	UDT::getsockopt(client, 0, UDT_CC, &cchandle, &temp);
	if (NULL != cchandle)
	    cchandle->setRate(blast_rate);
    }

    if (args->print_speed){
	pthread_t mon_thread;
	pthread_create(&mon_thread, NULL, monitor, &client);
	
    }

    pthread_create(&sndthread, NULL, senddata, &send_args);

    opts.socket_ready = 1;

    void * retval;
    pthread_join(sndthread, &retval);

    // Partial cause of segfault issue commented out for now
    // UDT::cleanup();

    return NULL;
}

