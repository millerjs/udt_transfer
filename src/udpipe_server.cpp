/*****************************************************************************
Copyright 2013 Laboratory for Advanced Computing at the University of Chicago

This file is part of udpipe

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
#include "udpipe.h"

#include "../udt/src/udt.h"
#include "parcel.h"
#include "udpipe_threads.h"

#include <arpa/inet.h>

using std::cerr;
using std::endl;
using std::string;

void* recvdata(void*);
void* senddata(void*);

int buffer_size;

void *run_server(void *_args_) 
{
    thread_args * args = (thread_args*) _args_;

    verb(VERB_2, "[server] Running server...");

    // initial setup
    char *port = args->port;
    int blast = args->blast;
    int udt_buff = args->udt_buff;
    int udp_buff = args->udp_buff; // 67108864;
    int mss = args->mss;

    verb(VERB_2, "[server] Starting UDT...");

    // start UDT
    UDT::startup();

    addrinfo hints;
    addrinfo* res;
    struct sockaddr_in my_addr;

    // switch to turn on ip specification or not
    int specify_ip = !(args->listen_ip == NULL);

    verb(VERB_2, "Listening on specific ip: %s", args->listen_ip);
    
    // char* ip;

    // if (specify_ip)
    // 	ip = strdup(args->listen_ip);

    if (specify_ip) {
        my_addr.sin_family = AF_INET;     
        my_addr.sin_port = htons(atoi(port)); 
        my_addr.sin_addr.s_addr = inet_addr(args->listen_ip);

        bzero(&(my_addr.sin_zero), 8);    
    } else {
        memset(&hints, 0, sizeof(struct addrinfo));
    
        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        string service(port);

        if (0 != getaddrinfo(NULL, service.c_str(), &hints, &res)) {
            cerr << "illegal port number or port is busy.\n" << endl;
            return NULL;
        }
    }

    buffer_size = udt_buff;

    verb(VERB_2, "[server] Creating socket...");

    UDTSOCKET serv;
    if (specify_ip){
        serv = UDT::socket(AF_INET, SOCK_STREAM, 0);
    } else { 
        serv = UDT::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    }

    // UDT Options
    if (blast) {
        UDT::setsockopt(serv, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
    }
    
    UDT::setsockopt(serv, 0, UDT_MSS, &mss, sizeof(int));
    UDT::setsockopt(serv, 0, UDT_RCVBUF, &udt_buff, sizeof(int));
    UDT::setsockopt(serv, 0, UDP_RCVBUF, &udp_buff, sizeof(int));

    // printf("Binding to %s\n", inet_ntoa(sin.sin_addr));
    
    verb(VERB_2, "[server] Binding socket...");
    
    int r;

    if (specify_ip) { 
        r = UDT::bind(serv, (struct sockaddr *)&my_addr, sizeof(struct sockaddr));
    } else {
        r = UDT::bind(serv, res->ai_addr, res->ai_addrlen);
    }

    if (UDT::ERROR == r) {
    	cerr << "bind: " << UDT::getlasterror().getErrorMessage() << endl;
    	return NULL;
    }


    if (UDT::ERROR == UDT::listen(serv, 10)) {
        cerr << "listen: " << UDT::getlasterror().getErrorMessage() << endl;
        return NULL;
    }

    sockaddr_storage clientaddr;
    int addrlen = sizeof(clientaddr);
    
    UDTSOCKET recver;
    pthread_t rcvthread, sndthread;

    verb(VERB_2, "[server] Listening for client...");

    if (UDT::INVALID_SOCK == (recver = UDT::accept(serv,
						   (sockaddr*)&clientaddr, &addrlen))) {

        cerr << "accept: " << UDT::getlasterror().getErrorMessage() << endl;
        return NULL;
    }

    verb(VERB_2, "[server] New client connection...");

    char clienthost[NI_MAXHOST];
    char clientservice[NI_MAXSERV];
    getnameinfo((sockaddr *)&clientaddr, addrlen, clienthost,
		sizeof(clienthost), clientservice, sizeof(clientservice),
		NI_NUMERICHOST|NI_NUMERICSERV);


    verb(VERB_2, "[server] Creating receive thread...");

    rs_args rcvargs;
    rcvargs.usocket = new UDTSOCKET(recver);
    rcvargs.use_crypto = args->use_crypto;
    rcvargs.verbose = args->verbose;
    rcvargs.n_crypto_threads = args->n_crypto_threads;
    rcvargs.c = args->dec;
    rcvargs.timeout = args->timeout;

    // Set sender file descriptors
    if (args->send_pipe && args->recv_pipe){
        rcvargs.send_pipe = args->send_pipe;
        rcvargs.recv_pipe = args->recv_pipe;
    } else {
        fprintf(stderr, "[udpipe_server] server pipes uninitialized\n");
        exit(1);
    }
    
    pthread_create(&rcvthread, NULL, recvdata, &rcvargs);
    pthread_detach(rcvthread);
    RegisterThread(rcvthread, "recvdata");
    
    verb(VERB_2, "[server] Receive thread created: %lu", rcvthread);

    verb(VERB_2, "[server] Creating send thread");

    rs_args send_args;
    send_args.usocket = new UDTSOCKET(recver);
    send_args.use_crypto = args->use_crypto;
    send_args.verbose = args->verbose;

    send_args.n_crypto_threads = args->n_crypto_threads;
    send_args.c = args->enc;
    send_args.timeout = args->timeout;

    if (args->send_pipe && args->recv_pipe) {
        
        send_args.send_pipe = args->send_pipe;
        send_args.recv_pipe = args->recv_pipe;

        if (args->print_speed){
            pthread_t mon_thread;
            pthread_create(&mon_thread, NULL, monitor, &recver);
            RegisterThread(mon_thread, "monitor");
        }

        pthread_create(&sndthread, NULL, senddata, &send_args);
        RegisterThread(sndthread, "senddata");
        
        verb(VERB_2, "[server] Waiting for send thread to complete");
        pthread_join(sndthread, NULL);
        
    } else { 
        fprintf(stderr, "[udpipe_server] send or receive pipe uninitialized\n");
    }
    
    verb(VERB_2, "[server] Exiting and cleaning up");
    UDT::close(*rcvargs.usocket);
    UDT::close(*send_args.usocket);
    
    UDT::cleanup();

    free(args->ip);
    ExitThread(GetMyThreadId());
    return NULL;
}

