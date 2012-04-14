#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "messages.h"

#include <iostream>
#include "connlist.h"

int main() {

	int server_sockfd = create_server_socket("12345");
	new ServerDaemonSocket(server_sockfd);

	eventloop();
}
