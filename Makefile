C++ = g++

DIR = $(shell pwd)

CCFLAGS = -Wno-write-strings -g -O3 -Wall
CCLFAGS = $(CCFLAGS) -fsel-sched-pipelining -fselective-scheduling
LDFLAGS = -lrt -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE 

APP = ucp
APPOUT = ucp

all: $(APP) 

%.o: %.cpp 
	$(C++) $(CCFLAGS) $< -c

ucp: ucp.o sender.o receiver.o files.o timer.o
	$(C++) $^ -o $(APPOUT) $(LDFLAGS)

clean:
	rm -f *.o $(APPOUT) 

install:
	export PATH=$(DIR):$$PATH
