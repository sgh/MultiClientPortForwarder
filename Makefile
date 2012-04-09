CFLAGS=-Wall -Wextra -O0 -g -MD
CXXFLAGS=-Wall -Wextra -O0 -g -MD
CC=g++

COMMONFILES = connlist.o socketfifo.o

all: server client

server: server.o ${COMMONFILES}

client: client.o ${COMMONFILES}

clean:
	rm -f client server *.d *.o

-include *.d
