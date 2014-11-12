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

	verb(VERB_2, "[%s] Running server...", __func__);

	// initial setup
	char *port = args->port;
	int blast = args->blast;
	int udt_buff = args->udt_buff;
	int udp_buff = args->udp_buff; // 67108864;
	int mss = args->mss;

	verb(VERB_2, "[%s] Starting UDT...", __func__);

	// start UDT
	UDT::startup();

	addrinfo hints;
	addrinfo* res;
	struct sockaddr_in my_addr;

	// switch to turn on ip specification or not
	int specify_ip = !(args->listen_ip == NULL);

	if (specify_ip) {
		verb(VERB_2, "[%s] Listening on specific ip: %s", __func__ , args->listen_ip);
	}

	// char* ip;

	// if (specify_ip)
	// 	ip = strdup(args->listen_ip);

	if (specify_ip) {
		my_addr.sin_family = AF_INET;
		my_addr.sin_port = htons(atoi(port));
//		my_addr.sin_addr.s_addr = inet_addr(args->listen_ip);
		inet_pton(my_addr.sin_family, args->listen_ip, &(my_addr.sin_addr.s_addr));

//		bzero(&(my_addr.sin_zero), 8);
		memset(&(my_addr.sin_zero), 0, sizeof(my_addr.sin_zero));
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

	verb(VERB_2, "[%s] Creating socket...", __func__);

	UDTSOCKET serv;
	if (specify_ip) {
		serv = UDT::socket(AF_INET, SOCK_STREAM, 0);
	} else {
		serv = UDT::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	}

	if ( UDT::INVALID_SOCK == serv ) {
		verb(VERB_1, "[%s] UDTError socket (%d) %s", __func__, UDT::getlasterror().getErrorCode(), UDT::getlasterror().getErrorMessage());
		return NULL;
	}

	// UDT Options
	if (blast) {
		UDT::setsockopt(serv, 0, UDT_CC, new CCCFactory<CUDPBlast>, sizeof(CCCFactory<CUDPBlast>));
	}

	UDT::setsockopt(serv, 0, UDT_MSS, &mss, sizeof(int));
	UDT::setsockopt(serv, 0, UDT_RCVBUF, &udt_buff, sizeof(int));
	UDT::setsockopt(serv, 0, UDP_RCVBUF, &udp_buff, sizeof(int));

	// printf("Binding to %s\n", inet_ntoa(sin.sin_addr));

	verb(VERB_2, "[%s] Binding socket...", __func__);

	int r;

	if (specify_ip) {
		r = UDT::bind(serv, (struct sockaddr *)&my_addr, sizeof(struct sockaddr));
	} else {
		r = UDT::bind(serv, res->ai_addr, res->ai_addrlen);
	}

	if (UDT::ERROR == r) {
		verb(VERB_1, "[%s] UDTError bind (%d) %s", __func__, UDT::getlasterror().getErrorCode(), UDT::getlasterror().getErrorMessage());
		return NULL;
	}


	if (UDT::ERROR == UDT::listen(serv, 10)) {
		verb(VERB_1, "[%s] UDTError listen (%d) %s", __func__, UDT::getlasterror().getErrorCode(), UDT::getlasterror().getErrorMessage());
		return NULL;
	}

	sockaddr_storage clientaddr;
	int addrlen = sizeof(clientaddr);

	UDTSOCKET recver;
	pthread_t rcvthread, sndthread;

	verb(VERB_2, "[%s] Listening for client...", __func__);

	if (UDT::INVALID_SOCK == (recver = UDT::accept(serv,
							(sockaddr*)&clientaddr, &addrlen))) {

		cerr << "accept: " << UDT::getlasterror().getErrorMessage() << endl;
		return NULL;
	}

	verb(VERB_2, "[%s] New client connection...", __func__);

	char clienthost[NI_MAXHOST];
	char clientservice[NI_MAXSERV];
	getnameinfo((sockaddr *)&clientaddr, addrlen, clienthost,
		sizeof(clienthost), clientservice, sizeof(clientservice),
		NI_NUMERICHOST|NI_NUMERICSERV);


	verb(VERB_2, "[%s] Creating receive thread...", __func__);

	rs_args recv_args;
	recv_args.usocket = new UDTSOCKET(recver);
	recv_args.use_crypto = args->use_crypto;
	recv_args.verbose = args->verbose;
	recv_args.n_crypto_threads = args->n_crypto_threads;
	recv_args.master = args->master;
	if ( (args->dec == NULL) && (args->use_crypto) ) {
		fprintf(stderr, "[%s] crypto class 'dec' uninitialized\n", __func__ );
		exit(1);
	}
	recv_args.c = args->dec;
	recv_args.timeout = args->timeout;

	// Set sender file descriptors
	if (args->send_pipe && args->recv_pipe){
		recv_args.send_pipe = args->send_pipe;
		recv_args.recv_pipe = args->recv_pipe;
	} else {
		fprintf(stderr, "[%s] server pipes uninitialized\n", __func__ );
		exit(1);
	}

//		pthread_create(&rcvthread, NULL, recvdata, &recv_args);
//		RegisterThread(rcvthread, "recvdata", THREAD_TYPE_2);
	create_thread(&rcvthread, NULL, recvdata, &recv_args, "recvdata", THREAD_TYPE_2);
	pthread_detach(rcvthread);

	verb(VERB_2, "[%s] Receive thread created: %lu", __func__ , rcvthread);

	verb(VERB_2, "[%s] Creating send thread", __func__);

	rs_args send_args;
	send_args.usocket = new UDTSOCKET(recver);
	send_args.use_crypto = args->use_crypto;
	send_args.verbose = args->verbose;

	send_args.n_crypto_threads = args->n_crypto_threads;
	send_args.c = args->enc;
	send_args.timeout = args->timeout;
	send_args.master = args->master;

	if ( (args->enc == NULL) && (args->use_crypto) ) {
		fprintf(stderr, "[%s] crypto class 'enc' uninitialized\n", __func__ );
		exit(1);
	}

	if (args->send_pipe && args->recv_pipe) {

		send_args.send_pipe = args->send_pipe;
		send_args.recv_pipe = args->recv_pipe;

		if (args->print_speed){
			pthread_t mon_thread;
			create_thread(&mon_thread, NULL, monitor, &recver, "monitor", THREAD_TYPE_2);
//			pthread_create(&mon_thread, NULL, monitor, &recver);
//			RegisterThread(mon_thread, "monitor", THREAD_TYPE_2);
		}

		create_thread(&sndthread, NULL, senddata, &send_args, "senddata", THREAD_TYPE_2);
//		pthread_create(&sndthread, NULL, senddata, &send_args);
//		RegisterThread(sndthread, "senddata", THREAD_TYPE_2);

		set_socket_ready(1);
		verb(VERB_2, "[%s] Waiting for send thread to complete", __func__);
		pthread_join(sndthread, NULL);

	} else {
		fprintf(stderr, "[%s] send or receive pipe uninitialized\n", __func__);
	}

	verb(VERB_2, "[%s] Exiting and cleaning up", __func__);
	UDT::close(*recv_args.usocket);
	UDT::close(*send_args.usocket);
	delete(send_args.usocket);
	delete(recv_args.usocket);
	UDT::cleanup();
	freeaddrinfo(res);
	free(args->ip);
	free(args->port);
	free(args);
	unregister_thread(get_my_thread_id());
	return NULL;
}

