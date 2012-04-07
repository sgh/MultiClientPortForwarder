#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <iostream>
#include <algorithm>

#include "connlist.h"

std::vector<ConnectedSocket*> connected_sockets;

std::vector<ConnectedSocket*> connected_sockets_to_be_added;


unsigned int id_sequence = 1;

// void connection_accept(ConnectedSocket* pcon) {
// 	ConnectedSocket& con = *pcon;
// 	int acceptedfd;
// 	acceptedfd = accept(con.get_fd(), NULL, NULL);
// 	if (con.type == CONN_DAEMON_LISTEN) {
// 		printf("CONN_DAEMON_LISTEN\n");
// 		new DaemonSocket(acceptedfd);
// 	}
// 	if (con.type == CONN_FORWARD_LISTEN) {
// 		printf("Accept CONN_FORWARD_LISTEN\n");
// 		ForwardSocket* new_forwardconnection;
// 		new_forwardconnection = new ForwardSocket(acceptedfd);
// 		new_forwardconnection->parent = con.parent;
// 		new_forwardconnection->id = id_sequence;
// 		struct CMD_ConnectPort cmd;
// 		cmd.type = CMD_CONNECT_PORT;
// 		cmd.port = con.get_port();
// 		cmd.id = new_forwardconnection->id;
// 		con.parent->txfifo_in( (unsigned char*)&cmd, sizeof(cmd) );
// 		id_sequence++;
// 		printf("CMD_CONNECT_PORT id:%d\n", cmd.id);
// 	}
// }

void connlist_add(ConnectedSocket* new_conn) {
	connected_sockets_to_be_added.push_back(new_conn);
	std::cout << "Added connection - fd:" << new_conn->get_fd() << ". New size is " << connected_sockets_to_be_added.size() << std::endl;
}

std::vector<ConnectedSocket*>::iterator connlist_begin() {

	std::vector<ConnectedSocket*>::iterator it = connected_sockets.begin();
	while (it != connected_sockets.end()) {
		if ((*it)->pending_delete) {
			std::vector<ConnectedSocket*>::iterator tmp = it;
			delete (*it);
			it--;
			connected_sockets.erase(tmp);
		}

		it++;
	}

	if (connected_sockets_to_be_added.size() > 0) {
		connected_sockets.insert(connected_sockets.end(), connected_sockets_to_be_added.begin(), connected_sockets_to_be_added.end());
		std::cout << "Realy added connection. New size is " << connected_sockets.size() << std::endl;
		connected_sockets_to_be_added.clear();
	}

	return connected_sockets.begin();
}


int create_server_socket(const char* port) {
	struct addrinfo hints, *servinfo, *rp;
	int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_sockfd < 0)
		perror("socket failed\n");
	
	const int yes = 1;
	setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	getaddrinfo(NULL, port, &hints, &servinfo);

	for (rp=servinfo; rp!=NULL; rp=rp->ai_next) {
		printf("A\n");
		if (bind(server_sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
			printf("bind suceed\n");
			break;
		}
	}
	
	if (rp == NULL) {
		perror("failed to bind");
		exit(0);
	}

	freeaddrinfo(servinfo);    

	if (listen(server_sockfd, 1))
		perror("listen\n");
	return server_sockfd;
}

int create_client_socket(const char* ip, const char* port) {
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int sfd=-1, s;

	/* Obtain address(es) matching host/port */
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;    /* Allow IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
	hints.ai_flags = 0;
	hints.ai_protocol = 0;          /* Any protocol */

	s = getaddrinfo(ip, port, &hints, &result);
	if (s != 0) {
	fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
	exit(EXIT_FAILURE);
	}

	/* getaddrinfo() returns a list of address structures.
	Try each address until we successfully connect(2).
	If socket(2) (or connect(2)) fails, we (close the socket
	and) try the next address. */
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,
				rp->ai_protocol);
		if (sfd == -1)
			continue;

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;                  /* Success */

		close(sfd);
	}

	if (rp == NULL) {               /* No address succeeded */
	fprintf(stderr, "Could not connect\n");
	exit(EXIT_FAILURE);
	}

	freeaddrinfo(result);           /* No longer needed */
	printf("Create client socket fd:%d\n", sfd);
	return sfd;
}


ConnectedSocket& conn_from_id(unsigned short id) {
	std::vector<ConnectedSocket*>::iterator it = connected_sockets.begin();
	while (it != connected_sockets.end()) {
		
		if ((*it)->id == id)
			return *(*it);
		it++;
	}
	printf("Failed to lookup id %d\n", id);
	assert( 0 );
}

void eventloop() {
	fd_set rfd;
	fd_set wfd;
	struct timeval tv;

	while (1) {
		int maxfd = 0;

		FD_ZERO(&rfd);
		FD_ZERO(&wfd);
		std::vector<ConnectedSocket*>::iterator it = connlist_begin();
		while (it != connected_sockets.end()) {
			ConnectedSocket& con = **it;

			// Read from sockets only if rxfifo is empty
			if (con.rx_free()) {
				FD_SET(con.get_fd(), &rfd);
				if (maxfd < con.get_fd())
					maxfd = con.get_fd();
			}

			// Write only to sockets when write-data is available
			if (con.tx_len()) {
				FD_SET(con.get_fd(), &wfd);
				if (maxfd < con.get_fd())
					maxfd = con.get_fd();
			}

			it++;
		}




		tv.tv_usec = 0;
		tv.tv_sec = 1;
		if (select(maxfd+1, &rfd, &wfd, NULL, &tv) > 0) {

			std::vector<ConnectedSocket*>::iterator it = connlist_begin();
			std::cout << "Traversion " << connected_sockets.size() << " connections." << std::endl;
			while (it != connected_sockets.end()) {
				ConnectedSocket& con = **it;
				std::cout << "Checking fd:" << con.get_fd() << std::endl;
				if (FD_ISSET(con.get_fd(), &rfd))
					con.connection_handle();

				if (FD_ISSET(con.get_fd(), &wfd)) {
					con.transmit();
				}

				it++;
			}

		} else
			printf("tick\n");

	}
}