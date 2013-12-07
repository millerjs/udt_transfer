C++ = g++

DIR = $(shell pwd)

CCFLAGS = -Wno-write-strings -g
LDFLAGS = -lrt -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE 

APP = ucp
APPOUT = ucp

all: $(APP) 

%.o: %.cpp 
	$(C++) $(CCFLAGS) $< -c

ucp: handler.o files.o 
	$(C++) $^ -o $(APPOUT) $(LDFLAGS)

clean:
	rm -f *.o $(APPOUT) 

install:
	export PATH=$(DIR):$$PATH
