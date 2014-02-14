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
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <getopt.h>
#include <iostream>

#include "udpipe.h"
#include "udpipe_server.h"
#include "udpipe_client.h"

using std::cerr;
using std::endl;

void usage(){
    fprintf(stderr, "usage: udpipe [udpipe options] host port\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "\t-l \t\t\tlisten for connection\n");
    fprintf(stderr, "\t-n n_crypto_threads \tset number of encryption threads per send/recv thread to n_crypto_threads\n");
    fprintf(stderr, "\t-p key \t\t\tturn on encryption and specify key in-line\n");
    fprintf(stderr, "\t-f path \t\tturn on encryption, path=path to key file\n");
    fprintf(stderr, "\t-v verbose\n");
    fprintf(stderr, "\t-t timeout\t\tforce udpipe to timeout if no data transfered\n");
    exit(1);
}

void initialize_thread_args(thread_args *args){
    args->listen_ip = NULL;
    args->ip = NULL;
    args->port = NULL;
    args->blast = 0;
    args->blast_rate = 1000;
    args->udt_buff = BUFF_SIZE;
    args->udp_buff = BUFF_SIZE;
    args->mss = 8400;
    args->use_crypto = 0;
    args->verbose = 0;
    args->n_crypto_threads = 1;
    args->print_speed = 0;
    args->timeout = 0;
}

int main(int argc, char *argv[]){

    int opt;
    enum {NONE, SERVER, CLIENT};
    int operation = CLIENT;

    thread_args args;
    initialize_thread_args(&args);
    int use_crypto = 0;
    char* path_to_key = NULL;
    char* key = NULL;
    int n_crypto_threads = 1;

    // ----------- [ Read in options
    while ((opt = getopt (argc, argv, "i:t:hvsn:lp:f:")) != -1){
	switch (opt){

	case 'i':
	    args.listen_ip = optarg;
	    break; 	    

	case 's':
	    args.print_speed = 1;
	    break; 

	case 't':
	    args.timeout = atoi(optarg);
	    break; 

	case 'l':
	    operation = SERVER;
	    break;

	case 'v':
	    args.verbose = 1;
	    break;

	case 'n':
	    args.use_crypto = 1;
	    use_crypto  = 1; 
	    n_crypto_threads = atoi(optarg);
	    break;

	case 'p':
	    args.use_crypto = 1;
	    use_crypto = 1;
	    key = strdup(optarg);
	    break;

	case 'f':
	    args.use_crypto = 1;
	    use_crypto = 1;
	    path_to_key = strdup(optarg);
	    break;

	case 'h':
	    usage();
	    break;

	default:
	    fprintf(stderr, "Unknown command line arg. -h for help.\n");
	    usage();
	    exit(1);

	}
    }

    if (use_crypto && (path_to_key && key)){
	fprintf(stderr, "error: Please specify either key or key file, not both.\n");
	exit(1);
    }

    if (path_to_key){
	FILE*key_file = fopen(path_to_key, "r");
	if (!key_file){
	    fprintf(stderr, "key file: %s.\n", strerror(errno));
	    exit(1);
	}

	fseek(key_file, 0, SEEK_END); 
	long size = ftell(key_file);
	fseek(key_file, 0, SEEK_SET); 
	key = (char*)malloc(size);
	fread(key, 1, size, key_file);
	
    }

    // if (!use_crypto && key){
    // 	fprintf(stderr, "warning: You've specified a key, but you don't have encryption turned on.\nProceeding without encryption.\n");
    // }    

    if (use_crypto && !key){
	fprintf(stderr, "Please either: \n (1) %s\n (2) %s\n (3) %s\n",
		"include key in cli [-p key]",
		"read on in from file [-f /path/to/key/file]",
		"choose not to use encryption, remove [-n]");
	exit(1);
    }

    // Setup host
    if (operation == CLIENT){
	if (optind < argc){
	    args.ip = strdup(argv[optind++]);
	} else {
	    cerr << "error: Please specify server host." << endl;
	    exit(1);
	}
    if (args.verbose)
	fprintf(stderr, "Attempting connection to %s\n", args.ip);

    }

    if (args.verbose)
	fprintf(stderr, "Timeout set to %d seconds\n", args.timeout);
    

    // Check port input
    if (optind < argc){
	args.port = strdup(argv[optind++]);
    } else {
	cerr << "error: Please specify port num." << endl;
	exit(1);
    }

    // Initialize crypto
    if (use_crypto){

	char* cipher = (char*) "aes-128";
	crypto enc(EVP_ENCRYPT, PASSPHRASE_SIZE, (unsigned char*)key, cipher, n_crypto_threads);
	crypto dec(EVP_DECRYPT, PASSPHRASE_SIZE, (unsigned char*)key, cipher, n_crypto_threads);
	args.enc = &enc;
	args.dec = &dec;

	args.n_crypto_threads = n_crypto_threads;

    } else {

	args.enc = NULL;
	args.dec = NULL;

    }

    if (key)
	memset(key, 0, strlen(key));

    // Spawn correct process
    if (operation == SERVER){
	run_server(&args);

    } else if (operation == CLIENT){
	run_client(&args);

    } else {
	cerr << "Operation type not known" << endl;
    
    }

    
  
}
