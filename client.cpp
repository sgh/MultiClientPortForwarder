#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <iostream>
#include "connlist.h"
#include "messages.h"

// void connection_handle(struct ConnectedSocket& con) {

// }

int main(int argc, char *argv[]) {
	int sfd;

	sfd = create_client_socket("127.0.0.1", "12345");

	ClientSocket new_socket(sfd);

	if (argc > 1) {
		struct MSG_IdentifyConnection identify;
		identify.type = MSG_IDENTIFY_CONNECTION;
		identify.len = strlen(argv[1]) + sizeof(identify);
		new_socket.txfifo.in( (unsigned char*)	&identify, sizeof(identify));
		new_socket.txfifo.in( (unsigned char*)argv[1],   strlen(argv[1]));
	}

	eventloop();
	
	exit(EXIT_SUCCESS);
}
