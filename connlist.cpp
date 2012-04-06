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

void connection_accept(ConnectedSocket* pcon) {
	ConnectedSocket& con = *pcon;
// 	int res;
	int acceptedfd;
	acceptedfd = accept(con.fd, NULL, NULL);
	if (con.type == CONN_DAEMON_LISTEN) {
		printf("CONN_DAEMON_LISTEN\n");
		new DaemonSocket(acceptedfd);
	}
	if (con.type == CONN_FORWARD_LISTEN) {
		printf("Accept CONN_FORWARD_LISTEN\n");
		ForwardSocket* new_forwardconnection;
		new_forwardconnection = new ForwardSocket(acceptedfd);
// 		new_forwardconnection->client_fd = con.client_fd;
		new_forwardconnection->parent = con.parent;
		new_forwardconnection->id = id_sequence;
		struct CMD_ConnectPort cmd;
		cmd.type = CMD_CONNECT_PORT;
		cmd.port = con.port;
		cmd.id = new_forwardconnection->id;
// 		new_forwardconnection->client_fd = con.client_fd;
// 		assert(res == sizeof(cmd));
		con.parent->txfifo.in( (unsigned char*)&cmd, sizeof(cmd) );
		id_sequence++;
		printf("CMD_CONNECT_PORT id:%d\n", cmd.id);
	}
}

void connlist_add(ConnectedSocket* new_conn) {
	connected_sockets_to_be_added.push_back(new_conn);
	std::cout << "Added connection - fd:" << new_conn->fd << ". New size is " << connected_sockets_to_be_added.size() << std::endl;
}

std::vector<ConnectedSocket*>::iterator connlist_begin() {

	std::vector<ConnectedSocket*>::iterator it = connected_sockets.begin();
	while (it != connected_sockets.end()) {
		if ((*it)->pending_delete) {
			std::vector<ConnectedSocket*>::iterator tmp = it;
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




void conn_forward(ConnectedSocket& con) {
// 	int res;

	const unsigned char* ptr = con.rxfifo._data;
	struct MSG_SocketData data;
	data.type = MSG_SOCKET_DATA;
	data.id = con.id;
	while (1) {
		data.len = con.rxfifo._len;
		if (data.len > 1024)
			data.len = 1024;
		if (data.len == 0)
			break;
// 		printf("send to fd:%d\n", con.client_fd);
// 		res = send(con.client_fd, &data, sizeof(data), 0);
		con.parent->txfifo.in( (unsigned char*)&data, sizeof(data) );
// 		if (res == -1) {
// 			con.conn_close();
// 			return;
// 		}
// 		assert(res == sizeof(data));
// 		res = send(con.client_fd, ptr, data.len, 0);
		con.parent->txfifo.in( ptr, data.len );
// 		if (res == -1) {
// 			con.conn_close();
// 			return;
// 		}
		if (data.len < 500)
			printf("SOCKET->MUX: id:%d %d bytes payload\n", data.id, data.len);
// 		assert(res == data.len);
		con.rxfifo.skip(data.len);
		ptr += data.len;
	}
	assert(con.rxfifo._len == 0);
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


void conn_socket_data(ConnectedSocket& con) {
	int res;
	struct MSG_SocketData* msg_socketdata = (struct MSG_SocketData*)con.rxfifo._data;
	int total_len = msg_socketdata->len + sizeof(struct MSG_SocketData);
	if (con.rxfifo._len < total_len)
		return;
	if (msg_socketdata->len < 500)
		printf("MUX->SOCKET: data %d bytes (%d bytes payload)\n", con.rxfifo._len, msg_socketdata->len);
	ConnectedSocket& connection = conn_from_id(msg_socketdata->id);
	res = connection.txfifo.in( con.rxfifo._data+sizeof(struct MSG_SocketData), msg_socketdata->len);
	assert(res == -1 || res == msg_socketdata->len);
	con.rxfifo.skip(total_len);
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
			if (con.rxfifo.free()) {
				FD_SET(con.fd, &rfd);
				if (maxfd < con.fd)
					maxfd = con.fd;
			}
			if (con.txfifo._len) {
				FD_SET(con.fd, &wfd);
				if (maxfd < con.fd)
					maxfd = con.fd;
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
				std::cout << "Checking fd:" << con.fd << std::endl;
				if (FD_ISSET(con.fd, &rfd))
					con.connection_handle();

				if (FD_ISSET(con.fd, &wfd)) {
					printf("TX: fd:%d len:%d\n", con.fd, con.txfifo._len);
					int res = send(con.fd, con.txfifo._data, con.txfifo._len, 0);
					if (res == -1) {
						con.conn_close();
					} else
						con.txfifo.skip(res);
				}

				it++;
			}

		} else
			printf("tick\n");

	}
}