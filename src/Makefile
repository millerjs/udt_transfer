C++ = g++
APP = parcel
APPOUT = ../parcel
DIR = $(shell pwd)

ifndef os
   os = LINUX
endif

ifndef arch
   arch = IA32
endif

CCFLAGS = -Wall -D$(os) -I../udt/src -finline-functions -g -lcrypto -D_LARGE_FILE_SOURCE=1 -Wno-write-strings -std=c++0x

ifeq ($(arch), IA32)
   CCFLAGS += -DIA32 #-mcpu=pentiumpro -march=pentiumpro -mmmx -msse
endif

ifeq ($(arch), POWERPC)
   CCFLAGS += -mcpu=powerpc
endif

ifeq ($(arch), IA64)
   CCFLAGS += -DIA64
endif

ifeq ($(arch), SPARC)
   CCFLAGS += -DSPARC
endif

LDFLAGS = -L../src ../udt/src/libudt.a -lstdc++ -lpthread -lm -lssl -lcrypto -lrt -Wl,-Map=$(APP).map,--cref

ifeq ($(os), UNIX)
   LDFLAGS += -lsocket
endif

ifeq ($(os), SUNOS)
   LDFLAGS += -lrt -lsocket
endif

all: $(APP) 

%.o: %.cpp 
	$(C++) $(CCFLAGS) $< -c

parcel: parcel.o sender.o receiver.o timer.o files.o udpipe_threads.o udpipe_server.o udpipe_client.o crypto.o postmaster.o thread_manager.o util.h debug_output.o
	$(C++) $^ -o $(APPOUT) $(LDFLAGS)

clean:
	rm -f *.o $(APPOUT) 

install:
	install $(APPOUT) $(INSTALL_PREFIX)/bin	
