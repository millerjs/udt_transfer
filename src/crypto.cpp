#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>

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
#include "debug_output.h"

#define pris(x)            if (DEBUG)fprintf(stderr,"[%s] %s\n",__func__,x)

#define MUTEX_TYPE          pthread_mutex_t
#define MUTEX_SETUP(x)      pthread_mutex_init(&(x), NULL)
#define MUTEX_CLEANUP(x)    pthread_mutex_destroy(&x) 
#define MUTEX_LOCK(x)       pthread_mutex_lock(&x)
#define MUTEX_UNLOCK(x)     pthread_mutex_unlock(&x)
#define THREAD_ID           pthread_self()

#define AES_BLOCK_SIZE      8

static MUTEX_TYPE *mutex_buf = NULL;
static void locking_function(int mode, int n, const char*file, int line);


Crypto::Crypto(int direc, int len, unsigned char* password, char *encryption_type, int n_threads)
{
    verb(VERB_2, "[%s]: New crypto object, direc = %d, len = %d, keyLen = %lu, type = %s, threads = %d", 
        __func__, direc, len, strlen((const char*)password), encryption_type, n_threads);

    N_CRYPTO_THREADS = n_threads;

    THREAD_setup();
    //free_key( password ); can't free here because is reused by threads
    const EVP_CIPHER *cipher = figure_encryption_type(encryption_type);

    if ( !cipher ) {
        exit(EXIT_FAILURE);
    }

    //aes-128|aes-256|bf|des-ede3
    //log_set_maximum_verbosity(LOG_DEBUG);
    //log_print(LOG_DEBUG, "encryption type %s\n", encryption_type);

    direction = direc;

    // EVP stuff
    for (int i = 0; i < N_CRYPTO_THREADS; i++) {

        memset(ivec, 0, 1024);

        EVP_CIPHER_CTX_init(&ctx[i]);
//        verb(VERB_2, "[%s]: Max key length = %d", __func__, EVP_MAX_KEY_LENGTH);
        if ( (len > EVP_MAX_KEY_LENGTH) || (strlen((const char*)password) > EVP_MAX_KEY_LENGTH) ) {
            verb(VERB_2, "[%s]: Key too long, defaulting to crappy one", __func__);
            if (!EVP_CipherInit_ex(&ctx[i], cipher, NULL, (const unsigned char*)"password", ivec, direc)) {
                verb(VERB_2, "[%s]: Error setting encryption scheme", __func__);
                exit(EXIT_FAILURE);
            }
            
        } else {
            if (!EVP_CipherInit_ex(&ctx[i], cipher, NULL, password, ivec, direc)) {
                verb(VERB_2, "[%s]: Error setting encryption scheme", __func__);
                exit(EXIT_FAILURE);
            }
        }
    }

    verb(VERB_2, "[%s]: id_lock before: %0x", __func__, &id_lock);
    if ( pthread_mutex_init(&id_lock, NULL) ) {
        verb(VERB_2, "[%s]: unable to init mutex id_lock", __func__);
    }
    verb(VERB_2, "[%s]: id_lock after: %0x", __func__, &id_lock);

    for (int i = 0; i < N_CRYPTO_THREADS; i++) {
        pthread_mutex_init(&c_lock[i], NULL);
        pthread_mutex_init(&thread_ready[i], NULL);
        pthread_mutex_lock(&thread_ready[i]);
    }

    // ----------- [ Initialize and set thread detached attribute
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    thread_id = 0;

    for (int i = 0; i < N_CRYPTO_THREADS; i++) {

        e_args[i].thread_id = i;
        e_args[i].ctx = &ctx[i];
        e_args[i].c = this;

        verb(VERB_2, "[%s]: Creating thread, id = %d", __func__, e_args[i].thread_id);
        int ret = pthread_create(&threads[i],
                 &attr, &crypto_update_thread, 
                 &e_args[i]);
        RegisterThread(threads[i], "crypto_update_thread");
    
        if (ret) {
            verb(VERB_2, "Unable to create thread: %d", ret);
        }
    }
}

int Crypto::get_num_crypto_threads() 
{
    return N_CRYPTO_THREADS;
}

int Crypto::get_thread_id()
{
//    verb(VERB_2, "[%s]: Checking id_lock at %0x (%d)", __func__, id_lock, sizeof(pthread_mutex_t));
    if ( pthread_mutex_lock(&id_lock) ) {
        verb(VERB_2, "[%s]: unable to lock mutex id_lock", __func__);
    }
    int id = this->thread_id;
    pthread_mutex_unlock(&id_lock);
    return id;
}

int Crypto::increment_thread_id()
{
    pthread_mutex_lock(&id_lock);
    thread_id++;
    if (thread_id >= N_CRYPTO_THREADS) {
        thread_id = 0;
    }
    pthread_mutex_unlock(&id_lock);
    return 1;
}

int Crypto::set_thread_ready(int thread_id)
{
    return pthread_mutex_unlock(&thread_ready[thread_id]);
}

int Crypto::wait_thread_ready(int thread_id)
{
    return pthread_mutex_lock(&thread_ready[thread_id]);
}

int Crypto::lock_data(int thread_id)
{
    return pthread_mutex_lock(&c_lock[thread_id]);
}

int Crypto::unlock_data(int thread_id)
{
    return pthread_mutex_unlock(&c_lock[thread_id]);
}


//    Crypto::~Crypto()
//    {
//        // i guess thread issues break this but it needs to be done
//        //TODO: find out why this is bad and breaks things
//        EVP_CIPHER_CTX_cleanup(&ctx);
//    }


// Returns how much has been encrypted and will call encrypt final when
// given len of 0
int Crypto::encrypt(char *in, char *out, int len)
{
    int evp_outlen;

    if (len == 0) {
        if (!EVP_CipherFinal_ex(&ctx[0], (unsigned char *)out, &evp_outlen)) {
            verb(VERB_2, "encryption error");
            exit(EXIT_FAILURE);
        }
        return evp_outlen;
    }

    if(!EVP_CipherUpdate(&ctx[0], (unsigned char *)out, &evp_outlen, (unsigned char *)in, len))
    {
        verb(VERB_2, "encryption error");
        exit(EXIT_FAILURE);
    }
    return evp_outlen;
}


const int max_block_size = 64*1024;

// Function for OpenSSL to lock mutex
static void locking_function(int mode, int n, const char*file, int line)
{
    pris("LOCKING FUNCTION CALLED");
    if (mode & CRYPTO_LOCK) {
        MUTEX_LOCK(mutex_buf[n]);
    } else { 
        MUTEX_UNLOCK(mutex_buf[n]);
    }
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
  
    if ( mutex_buf ) {

        int i;
        for (i = 0; i < CRYPTO_num_locks(); i++)
        MUTEX_SETUP(mutex_buf[i]);

        // CRYPTO_set_id_callback(threadid_func);
        CRYPTO_THREADID_set_callback(threadid_func);
        CRYPTO_set_locking_callback(locking_function);

        pris("Locking and callback functions set");
    }
    
    return 0;
}

// Cleans up the mutex buffer for openSSL
int THREAD_cleanup(void)
{
    pris("Cleaning up threads");
    if ( mutex_buf ) { 

        /* CRYPTO_set_id_callback(NULL); */
        CRYPTO_THREADID_set_callback(NULL);
        CRYPTO_set_locking_callback(NULL);

        int i;
        for (i = 0; i < CRYPTO_num_locks(); i ++)
        MUTEX_CLEANUP(mutex_buf[i]);
    }
    
    return 0;

}


int crypto_update(char* in, char* out, int len, Crypto *c)
{

    int evp_outlen = 0;
    int i = c->get_thread_id();
    c->increment_thread_id();
    c->lock_data(i);

    if (len == 0) {

        // FINALIZE CIPHER
        if (!EVP_CipherFinal_ex(&c->ctx[i], (uchar*)in, &evp_outlen)) {
            verb(VERB_2, "encryption error");
            exit(EXIT_FAILURE);
        }

    } else {

        // [EN][DE]CRYPT
        if(!EVP_CipherUpdate(&c->ctx[i], (uchar*)in, &evp_outlen, (uchar*)in, len)){
            verb(VERB_2, "encryption error");
            exit(EXIT_FAILURE);
        }

        // DOUBLE CHECK
        if (evp_outlen-len){
            verb(VERB_2, "Did not encrypt full length of data [%d-%d]", 
                evp_outlen, len);
            exit(EXIT_FAILURE);
        }

    }

    c->unlock_data(i);

    return evp_outlen;

}


void *crypto_update_thread(void* _args)
{

    int evp_outlen = 0, error = 0;

    if (!_args){
        verb(VERB_2,  "Null argument passed to crypto_update_thread");
    } else {

        e_thread_args* args = (e_thread_args*)_args;
        Crypto *c = (Crypto*)args->c;
//        verb(VERB_2, "[%s %lu] Enter with thread_id: %d", __func__, pthread_self(), args->thread_id);

        while (1) {
//            verb(VERB_2, "[%s %lu] Enter loop with thread id: %d", __func__, pthread_self(), args->thread_id);
            if ( CheckForExit() ) {
                break;
            }

//            verb(VERB_2, "[%s %lu] Checking thread id: %d", __func__, pthread_self(), args->thread_id);
            if ( args->thread_id > MAX_CRYPTO_THREADS ) {
                verb(VERB_2, "[%s %lu] Whoops, thread_id %d out of range!", __func__, pthread_self(), args->thread_id);
                exit(0);
            }
            c->wait_thread_ready(args->thread_id);

            int len = args->len;

            int total = 0;
            while (total < args->len) {

                if(!EVP_CipherUpdate(&c->ctx[args->thread_id], 
                         args->in+total, &evp_outlen, 
                         args->out+total, args->len-total)) {
                    verb(VERB_2, "encryption error");
                    error = 1;
                    break;
                }
                // fly - kind of a crappy way to exit the outer while here, but
                // I'm trying to centralize things on exit
                if ( error > 0 ) {
                    break;
                }
                total += evp_outlen;

            }

            if (len != args->len){
                verb(VERB_2, "error: The length changed during encryption.\n\n");
                break;
            }

            if (total != args->len){
                verb(VERB_2, "error: Did not encrypt full length of data %d [%d-%d]", 
                    args->thread_id, total, args->len);
                break;
            }

//            verb(VERB_2, "[%s %lu] Done with thread %d\n", __func__, pthread_self(), args->thread_id);
            c->unlock_data(args->thread_id);
        
        }
    }
    
    ExitThread(GetMyThreadId());
    return NULL;
    
}

int join_all_encryption_threads(Crypto *c)
{

    if (!c) {
        verb(VERB_2, "error: join_all_encryption_threads passed null pointer\n");
        return 0;
    }
    
    for (int i = 0; i < c->get_num_crypto_threads(); i++) {
        c->lock_data(i);
        c->unlock_data(i);
    }
    
    return 0;

}

int pass_to_enc_thread(char* in, char*out, int len, Crypto*c)
{

    if (len > 0) {

        int thread_id = c->get_thread_id();
        c->lock_data(thread_id);

        c->increment_thread_id();

        // fprintf(stderr, "[%d] Waiting on data %d in pass\n", getpid(), thread_id);

        c->e_args[thread_id].in = (uchar*) in;
        c->e_args[thread_id].out = (uchar*) out;
        c->e_args[thread_id].len = len;

        // fprintf(stderr, "[%d] posting thread %d in pass\n", getpid(), thread_id);
        c->set_thread_ready(thread_id);

        // fflush(stderr);
    }

    return 0;
}

const EVP_CIPHER* figure_encryption_type(char* encrypt_str)
{
    const EVP_CIPHER *cipher = (EVP_CIPHER*)NULL;

    if (strncmp("aes-128", encrypt_str, 8) == 0) {
#ifdef OPENSSL_HAS_CTR
        if (CTR_MODE)
            cipher = EVP_aes_128_ctr();
        else
#endif
            cipher = EVP_aes_128_cfb();
    }
    else if (strncmp("aes-192", encrypt_str, 8) == 0) {
#ifdef OPENSSL_HAS_CTR
        if (CTR_MODE)
            cipher = EVP_aes_192_ctr();
        else
#endif
            cipher = EVP_aes_192_cfb();
    }
    else if (strncmp("aes-256", encrypt_str, 8) == 0) {
#ifdef OPENSSL_HAS_CTR
        if (CTR_MODE)
            cipher = EVP_aes_256_ctr();
        else
#endif
            cipher = EVP_aes_256_cfb();
    }
    else if (strncmp("des-ede3", encrypt_str, 9) == 0) {
        // apparently there is no 3des nor bf ctr
        cipher = EVP_des_ede3_cfb();
    }
    else if (strncmp("bf", encrypt_str, 3) == 0) {
        cipher = EVP_bf_cfb();
    }
    else {
        verb(VERB_2, "error unsupported encryption type %s\n", encrypt_str);
    }
    
    return cipher;
    
}


//
// cull_rsa_key
//
// culls the header and footer off of a key
//
int cull_rsa_key(char* key)
{
    // fly - ok, so assumption here...the basic base64 encoding
    // does NOT use the '-' character, so the assumption is if
    // we run into one, we know we're looking at the beginning 
    // or ending line.  Looking into possibilities, there *are*
    // base64 web encodings that will replace the '+' and '=' with 
    // '-' and '_', so if that ever changes, this routine will break
    
    unsigned int i = 0, j = 0;
    int retVal = 0, skipLine = 0, delimiterCount = 0, key_len;
    char* tmp_key = NULL;

    key_len = strlen(key) + 1;
    verb(VERB_2, "[%s]: creating temp of size %d\n", __func__, key_len);

    // make some temp space for a smaller copy
    tmp_key = (char*)malloc(sizeof(char) * key_len);
    memset(tmp_key, 0, key_len);
    while ( i < strlen(key) ) {
        if ( skipLine ) {
            if ( key[i] == '\n' ) {
                skipLine = 0;
            }
            i++;
        } else {
            if ( (key[i] != '\n') && (key[i] != '-') ) {
                tmp_key[j++] = key[i++];
            } else {
                if ( key[i] == '-' ) {
                    if ( delimiterCount == 1 ) {
                        break;
                    }
                    delimiterCount++;
                    skipLine = 1;
                }
                i++;
            }
        }
    }

    // ok, we're done, let's zero out the original and copy over
    memset(key, 0, key_len);
    strncpy(key, tmp_key, strlen(tmp_key));
    free(tmp_key);

    return retVal;
}


//
// generate_session_key
//
// generates an RSA key, needs to be freed when done
//
char* generate_session_key(void)
{

    // fly - NIST says 65537, so there, no longer magic
    // http://en.wikipedia.org/wiki/RSA_%28cryptosystem%29#Faulty_key_generation
    const int kExp = 65537;
    
    // fly - less than 1024 is considered insecure, so I'm using double that for
    // now
    const int kBits = 2048;

    int key_len;
    char *pem_key;

    // fly - Hey kids, look at how many steps it takes OpenSSL to just
    // generate a string of an RSA key!  Can you add some routines that 
    // always have to be bundled together to make this more convoluted?
    // I bet you can if you try!

    if ( 1 ) {
        RSA *rsa = RSA_generate_key(kBits, kExp, 0, 0);

        BIO *bio = BIO_new(BIO_s_mem());
        PEM_write_bio_RSAPrivateKey(bio, rsa, NULL, NULL, 0, NULL, NULL);

        key_len = BIO_pending(bio);
        verb(VERB_2, "[%s]: creating pem_key of size %d\n", __func__, key_len);
        pem_key = (char*)malloc(sizeof(char) * key_len + 1);
        memset(pem_key, 0, (sizeof(char) * key_len) + 1);

        BIO_read(bio, pem_key, key_len);

        cull_rsa_key(pem_key);

        BIO_free_all(bio);
        RSA_free(rsa);
    } else {
        key_len = strlen("password   ");
        verb(VERB_2, "[%s]: creating pem_key of size %d\n", __func__, key_len);
        pem_key = (char*)malloc(sizeof(char) * key_len + 1);
        memset(pem_key, 0, (sizeof(char) * key_len) + 1);
        strncpy(pem_key, "password", strlen("password"));
    }


    return(pem_key);
}


