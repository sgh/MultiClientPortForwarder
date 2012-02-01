
CFLAGS=-Wall -Wextra -O0 -g

all: server client

server: connlist.c connlist.h server.c messages.h

client: connlist.c connlist.h client.c messages.h

clean:
	rm -f client
	rm -f server
