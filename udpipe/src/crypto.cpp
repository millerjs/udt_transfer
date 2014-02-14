#include <openssl/evp.h>
#include <openssl/crypto.h>


#include <time.h>

#include <limits.h>
#include <unistd.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdlib.h>
#define DEBUG 0

#include "crypto.h"

#define pris(x)            if (DEBUG)fprintf(stderr,"[crypto] %s\n",x)   

#define MUTEX_TYPE	   pthread_mutex_t
#define MUTEX_SETUP(x)	   pthread_mutex_init(&(x), NULL)
#define MUTEX_CLEANUP(x)   pthread_mutex_destroy(&x) 
#define MUTEX_LOCK(x)	   pthread_mutex_lock(&x)
#define MUTEX_UNLOCK(x)	   pthread_mutex_unlock(&x)
#define THREAD_ID	   pthread_self()

#define AES_BLOCK_SIZE 8

static MUTEX_TYPE *mutex_buf = NULL;
static void locking_function(int mode, int n, const char*file, int line);

void pric(uchar* s, int len)
{
    int i;
    fprintf(stderr, "data: ");
    for (i = 0; i < len/4; i ++){
	fprintf(stderr, "%x ",  s[i]);
    }
    fprintf(stderr, "\n");
}

void prii(int i)
{
    if (DEBUG)
	fprintf(stderr, "             -> %d\n", i);
}

const int max_block_size = 64*1024;

// Function for OpenSSL to lock mutex
static void locking_function(int mode, int n, const char*file, int line)
{
    pris("LOCKING FUNCTION CALLED");
    if (mode & CRYPTO_LOCK)
	MUTEX_LOCK(mutex_buf[n]);
    else
	MUTEX_UNLOCK(mutex_buf[n]);
}

// Returns the thread ID
static void threadid_func(CRYPTO_THREADID * id)
{
    // fprintf(stderr, "[debug] %s\n", "Passing thread ID");
    CRYPTO_THREADID_set_numeric(id, THREAD_ID);
}


int THREAD_setup(void)
{
    
    pris("Setting up threads");
    mutex_buf = (MUTEX_TYPE*)malloc(CRYPTO_num_locks()*sizeof(MUTEX_TYPE));
  
    if (!mutex_buf)
	return 0;

    int i;
    for (i = 0; i < CRYPTO_num_locks(); i++)
	MUTEX_SETUP(mutex_buf[i]);

    // CRYPTO_set_id_callback(threadid_func);
    CRYPTO_THREADID_set_callback(threadid_func);
    CRYPTO_set_locking_callback(locking_function);

    pris("Locking and callback functions set");

    return 0;
}

// Cleans up the mutex buffer for openSSL
int THREAD_cleanup(void)
{
    pris("Cleaning up threads");
    if (!mutex_buf)
	return 0;

    /* CRYPTO_set_id_callback(NULL); */
    CRYPTO_THREADID_set_callback(NULL);
    CRYPTO_set_locking_callback(NULL);

    int i;
    for (i = 0; i < CRYPTO_num_locks(); i ++)
	MUTEX_CLEANUP(mutex_buf[i]);

    return 0;

}


int crypto_update(char* in, char* out, int len, crypto *c)
{

    int evp_outlen = 0;
    int i = c->get_thread_id();
    c->increment_thread_id();
    c->lock_data(i);

    if (len == 0) {
	
	// FINALIZE CIPHER
	if (!EVP_CipherFinal_ex(&c->ctx[i], (uchar*)in, &evp_outlen)) {
	    	fprintf(stderr, "encryption error\n");
	    	exit(EXIT_FAILURE);
	}

    } else {

    	// [EN][DE]CRYPT
    	if(!EVP_CipherUpdate(&c->ctx[i], (uchar*)in, &evp_outlen, (uchar*)in, len)){
    	    fprintf(stderr, "encryption error\n");
    	    exit(EXIT_FAILURE);
    	}

    	// DOUBLE CHECK
    	if (evp_outlen-len){
    	    fprintf(stderr, "Did not encrypt full length of data [%d-%d]", 
    		    evp_outlen, len);
    	    exit(EXIT_FAILURE);
    	}

    }

    c->unlock_data(i);

    return evp_outlen;

}


void *crypto_update_thread(void* _args)
{

    int evp_outlen = 0;

    if (!_args){
	fprintf(stderr, "Null argument passed to crypto_update_thread\n");
	exit(1);
    }

    e_thread_args* args = (e_thread_args*)_args;
    crypto *c = (crypto*)args->c;
    
    while (1){

	// fprintf(stderr, "[%d] Waiting for thread_ready %d\n", pthread_self(), args->thread_id);
	c->wait_thread_ready(args->thread_id);

	int len = args->len;

	int total = 0;
	while (total < args->len){

	    if(!EVP_CipherUpdate(&c->ctx[args->thread_id], 
				 args->in+total, &evp_outlen, 
				 args->out+total, args->len-total)){
		fprintf(stderr, "encryption error\n");
		exit(EXIT_FAILURE);
	    }
	    total += evp_outlen;

	}

	if (len != args->len){
	    fprintf(stderr, "error: The length changed during encryption.\n\n");
	    exit(1);
	}

	if (total != args->len){
	    fprintf(stderr, "error: Did not encrypt full length of data %d [%d-%d]", 
		    args->thread_id, total, args->len);
 	    exit(1);
	}
	
	// fprintf(stderr, "[%d] Done with thread %d\n", pthread_self(), args->thread_id);	
	c->unlock_data(args->thread_id);
	
    }

    return NULL;
    
}

int join_all_encryption_threads(crypto *c){

    if (!c){
	fprintf(stderr, "error: join_all_encryption_threads passed null pointer\n");
	return 0;
    }
    
    for (int i = 0; i < c->get_num_crypto_threads(); i++){
	c->lock_data(i);
	c->unlock_data(i);
    }
    
    return 0;

}

int pass_to_enc_thread(char* in, char*out, int len, crypto*c){

    if (len == 0)
	return 0; 

    int thread_id = c->get_thread_id();
    c->lock_data(thread_id);

    c->increment_thread_id();

    // fprintf(stderr, "[%d] Waiting on data %d in pass\n", getpid(), thread_id);

    c->e_args[thread_id].in = (uchar*) in;
    c->e_args[thread_id].out = (uchar*) out;
    c->e_args[thread_id].len = len;

    // fprintf(stderr, "[%d] posting thread %d in pass\n", getpid(), thread_id);
    c->set_thread_ready(thread_id);

    fflush(stderr);

    return 0;
}

