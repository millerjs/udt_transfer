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

#include "util.h"
#include "udpipe.h"
#include "udpipe_client.h"
#include "parcel.h"
#include "udpipe_threads.h"

#define prii(x) fprintf(stderr,"debug:%d\n",x)
#define pris(x) fprintf(stderr,"debug: %s\n",x)
#define uc_err(x) {fprintf(stderr,"error:%s\n",x);exit(1);}

#define ENOSERVER 1001

using std::cerr;
using std::endl;

void *run_client(void *_args_)
{

    thread_args *args = (thread_args*) _args_;

    verb(VERB_2, "[%s] Running client...", __func__);

    // initial setup
    char *ip = args->ip; 
    char *port = args->port;
    int blast = args->blast;
    int blast_rate = args->blast_rate;
    int udt_buff = args->udt_buff;
    int udp_buff = args->udp_buff; // 67108864;
    int mss = args->mss;

    verb(VERB_2, "[%s] Starting UDT...", __func__);

    // start UDT
    UDT::startup();

    // get the connection info for the requested port
    struct addrinfo hints, *local, *peer;

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    verb(VERB_2, "[%s] Calling getaddrinfo for port %s", __func__, port);
    if (0 != getaddrinfo(NULL, port, &hints, &local)) {
        cerr << "incorrect network address.\n" << endl;
        return NULL;
    }
    

    verb(VERB_2, "[%s] Creating socket...", __func__);
    
    // create the UDT socket
    UDTSOCKET client;
    bool NOT_CONNECTED = true;
    int connectionAttempts = 0;
    const int MAX_CONNECTION_ATTEMPTS = 25;

    while (NOT_CONNECTED && connectionAttempts < MAX_CONNECTION_ATTEMPTS) {

        client = UDT::socket(local->ai_family, local->ai_socktype, local->ai_protocol);

        if ( UDT::INVALID_SOCK == client ) {
            verb(VERB_1, "[%s] UDTError socket %s", __func__, UDT::getlasterror().getErrorMessage());
            return NULL;
        }

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

        verb(VERB_2, "[%s] Connecting to server...", __func__);
        
        if (UDT::ERROR == UDT::connect(client, peer->ai_addr, peer->ai_addrlen)) {
            verb(VERB_1, "[%s] UDTError %s", __func__, UDT::getlasterror().getErrorMessage());
        
            // cerr << "connect: " << UDT::getlasterror().getErrorCode() << endl;
            if (args->verbose) {
                cerr << "connect: " << UDT::getlasterror().getErrorMessage() << endl;
            }

            if (UDT::getlasterror().getErrorCode() != ENOSERVER) {
                return NULL;
            } else {
                connectionAttempts ++;
            }
            
        } else {
            NOT_CONNECTED = false;
        }

    }

    verb(VERB_2, "[%s] Creating receive thread...", __func__);

    if ( args->dec == NULL && args->use_crypto ) {
        verb(VERB_2, "[%s] dec crypto isn't initialized!!", __func__);
        exit(0);
    }

    // set the socket up and send to the receive thread
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
        fprintf(stderr, "[%s] send pipe uninitialized\n", __func__);
        exit(1);
    }

    pthread_create(&rcvthread, NULL, recvdata, &rcvargs);
    pthread_detach(rcvthread);
    RegisterThread(rcvthread, "recvdata", THREAD_TYPE_2);
    
    verb(VERB_2, "[%s] Receive thread created: %lu", __func__, rcvthread);

    verb(VERB_2, "[%s] Creating send thread...", __func__);

    if ( args->enc == NULL && args->use_crypto ) {
        verb(VERB_2, "[%s] enc crypto isn't initialized!!", __func__);
        exit(0);
    }

    // same thing, but with the send thread
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
        fprintf(stderr, "[%s] send pipe uninitialized\n", __func__);
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

    if (args->print_speed) {
        pthread_t mon_thread;
        pthread_create(&mon_thread, NULL, monitor, &client);
        RegisterThread(mon_thread, "monitor", THREAD_TYPE_2);
    }

    pthread_create(&sndthread, NULL, senddata, &send_args);
    RegisterThread(sndthread, "senddata", THREAD_TYPE_2);
    
    g_opts.socket_ready = 1;


    void * retval;
    pthread_join(sndthread, &retval);

    verb(VERB_2, "[%s] Exiting and cleaning up...", __func__);
    // Partial cause of segfault issue commented out for now
    // UDT::cleanup();
    UDT::close(*rcvargs.usocket);
    UDT::close(*send_args.usocket);
    UDT::cleanup();
    free(ip);
    ExitThread(GetMyThreadId());
    return NULL;
}

