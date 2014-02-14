/*****************************************************************************
Copyright 2013 Laboratory for Advanced Computing at the University of Chicago

This file is part of udr/udt

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
#include <stdlib.h>
#include <stdio.h>

#include "../udt/src/udt.h"

#include "cc.h"
#include "udpipe_threads.h"
#include "crypto.h"

/* #define BUFF_SIZE 327680 */
#define BUFF_SIZE 67108864

typedef struct rs_args{
    UDTSOCKET*usocket;
    crypto *c;
    int use_crypto;
    int verbose;
    int n_crypto_threads; 
    int timeout;
} rs_args;

typedef struct thread_args{
    crypto *enc;
    crypto *dec;
    char *listen_ip;
    char *ip;
    char *port;
    int blast;
    int blast_rate;
    size_t udt_buff;
    size_t udp_buff;
    int mss;
    int use_crypto;
    int verbose;
    int n_crypto_threads;
    int print_speed; 
    int timeout;
} thread_args;

void* send_buf_threaded(void*_args);
