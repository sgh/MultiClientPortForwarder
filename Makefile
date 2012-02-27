
CFLAGS=-Wall -Wextra -O0 -g
CXXFLAGS=-Wall -Wextra -O0 -g

all: server client

server: connlist.cpp connlist.h server.cpp messages.h

client: connlist.cpp connlist.h client.cpp messages.h

clean:
	rm -f client
	rm -f server
