udpipe
======

udpipe is a UDT data transfer device based off of the functionality of netcat.

CONTENT
-------
./src:     udpipe source code

./udt:	      UDT source code, documentation and license


TO MAKE
------- 
    make -e os=XXX arch=YYY 

XXX: [LINUX(default), BSD, OSX]   
YYY: [IA32(default), POWERPC, IA64, AMD64]  

### Dependencies:
OpenSSL (libssl and libcrypto)  

udpipe has only been tested for Linux.


USAGE
------

udpipe follows the same model as netcat.  The server side establishes a listener, and awaits an incoming connection.  The client side connects to an established server or times out.  Encryption is off by default. The encrypted option uses a multithreaded version of OpenSSL with aes-128.

### Basic usage:

Server side:
       up [udtcat options] -l port

Client side:
       up [udtcat options] host port

#### udpipe Options:

     -l							start a server
     -n n_crypto_threads 		set number of encryption threads per send/recv thread to n_crypto_threads
     -p key				    	turn on encryption and specify key in-line
     -f path			        turn on encryption, path=path to key file
     -v							verbose
     -t timeout					force udpipe to timeout if no data transfered
     -i local_ip                server only: specify which local ip to bind the server to 

### Basic exmple (unencrypted)

Client side:

       up localhost 9000 < source/file

Server side:

       up -l 9000 > output/file

### Basic exmple (encrypted)

Client side:

       up -n 4 -p key localhost 9000 < source/file

Server side:

       up -n 4 -f file/contains/key -l 9000 > output/file

This examples creates a connection to trasfer "source/file" to "output/file" over an encrypted stream on port 9000 which uses 4 threads to encrypt/decrypt each block with a specified key.


