##### Compiler

CC = g++

##### FLAGS

CFLAGS = -I../../include -Wno-write-strings -g -fpic -std=gnu99 -MMD -MP -DDEBUG -O3 
LDLIBS = -lm -lrt -pthread 

##### Objects

BIN = ../ucp
DEPS = $(OBJS:.o=.d)
OBJS = ucp.o sender.o receiver.o files.o timer.o

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS)  $(LDLIBS) -o $(BIN)

%.d: %.c

check-syntax:
	$(CC) -o nul -S ${CHK_SOURCES}

clean:
	rm -f $(OBJS) $(BIN) *.d
