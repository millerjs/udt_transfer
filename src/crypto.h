/*****************************************************************************
Copyright 2012 Laboratory for Advanced Computing at the University of Chicago

This file is part of UDR.

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
#ifndef CRYPTO_H
#define CRYPTO_H

#define MAX_CRYPTO_THREADS 32
#define USE_CRYPTO 1

#define PASSPHRASE_SIZE 32
#define HEX_PASSPHRASE_SIZE 64
#define EVP_ENCRYPT 1
#define EVP_DECRYPT 0
#define CTR_MODE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <limits.h>
#include <iostream>
#include <unistd.h>
#include <semaphore.h>

#include "thread_manager.h"

#define MUTEX_TYPE          pthread_mutex_t
#define MUTEX_SETUP(x)      pthread_mutex_init(&(x), NULL)
#define MUTEX_CLEANUP(x)    pthread_mutex_destroy(&x) 
#define MUTEX_LOCK(x)       pthread_mutex_lock(&x)
#define MUTEX_UNLOCK(x)	    pthread_mutex_unlock(&x)
#define THREAD_ID           pthread_self()

int THREAD_setup(void);
int THREAD_cleanup(void);
void *enrypt_threaded(void* _args);

using namespace std;

typedef unsigned char uchar;

typedef struct e_thread_args
{
    uchar *in;
    uchar *out;
    int len;
    EVP_CIPHER_CTX *ctx;
    int idle;
    void* c;
    int thread_id;
} e_thread_args;

void *crypto_update_thread(void* _args);
const EVP_CIPHER* figure_encryption_type(char* encrypt_str);

class Crypto
{
 private:
    //BF_KEY key;
    unsigned char ivec[ 1024 ];
    int direction;
    pthread_mutex_t c_lock[MAX_CRYPTO_THREADS];
    pthread_mutex_t thread_ready[MAX_CRYPTO_THREADS];

    pthread_mutex_t id_lock = PTHREAD_MUTEX_INITIALIZER;

    int passphrase_size;
    int hex_passphrase_size;

    int N_CRYPTO_THREADS;

    int thread_id;

 public:
    // EVP stuff
    EVP_CIPHER_CTX  ctx[MAX_CRYPTO_THREADS];
    e_thread_args   e_args[MAX_CRYPTO_THREADS];
    pthread_t       threads[MAX_CRYPTO_THREADS];

    // member function declarations
    Crypto(int direc, int len, unsigned char* password, char *encryption_type, int n_threads);
    int get_num_crypto_threads();
    int get_thread_id();
    int increment_thread_id();
    int set_thread_ready(int thread_id);
    int wait_thread_ready(int thread_id);
    int lock_data(int thread_id);
    int unlock_data(int thread_id);
//    ~Crypto();
    int encrypt(char *in, char *out, int len);

};

int crypto_update(char* in, char* data, int len, Crypto *c);
int join_all_encryption_threads(Crypto *c);
int pass_to_enc_thread(char* in, char* out, int len, Crypto*c);

// generates an RSA key, needs to be freed when done
char* generate_session_key(void);

#endif

