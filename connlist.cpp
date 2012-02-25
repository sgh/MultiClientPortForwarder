#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "connlist.h"

struct ConnectedSocket* connected_sockets = NULL;

void connlist_add(struct ConnectedSocket* new_conn) {
	if (!connected_sockets) {
		new_conn->next = new_conn;
		new_conn->prev = new_conn;
		connected_sockets = new_conn;
	} else {
		connected_sockets->prev->next = new_conn;
		new_conn->prev = connected_sockets->prev;
		new_conn->next = connected_sockets;
		connected_sockets->prev = new_conn;
	}
}

void connlist_delete(struct ConnectedSocket* con) {
	printf("Connlist_delete\n");
	con->prev->next = con->next;
	con->next->prev = con->prev;
	free(con);
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


void conn_close(struct ConnectedSocket* con) {
	printf("Closing id:%d fd:%d\n", con->id, con->fd);
	close (con->fd);
	con->fd = -1;
}


void conn_forward(struct ConnectedSocket* con) {
	int res;
	if (con->type == CONN_FORWARD) {
		const char* ptr = con->rxbuffer;
		struct MSG_SocketData data;
		data.type = MSG_SOCKET_DATA;
		data.id = con->id;
		while (1) {
			data.len = con->rxlen<1024?con->rxlen:1024;
			if (data.len == 0) break;
			res = send(con->clientserver_connection->fd, &data, sizeof(data), 0);
			if (res == -1) {
				conn_close(con);
				return;
			}
			assert(res == sizeof(data));
			res = send(con->clientserver_connection->fd, ptr, data.len, 0);
			if (res == -1) {
				conn_close(con);
				return;
			}
			if (data.len < 500)
				printf("Forward data id:%d %d bytes (%d bytes payload)\n", data.id, res, data.len);
			assert(res == data.len);
			con->rxlen -= data.len;
			ptr += data.len;
		}
		assert(con->rxlen == 0);
	}
}

int conn_receive(struct ConnectedSocket* con) {
	assert( sizeof(con->rxbuffer) > con->rxlen );
	int len = recv(con->fd, con->rxbuffer+con->rxlen, sizeof(con->rxbuffer) - con->rxlen, MSG_DONTWAIT);
	if (len == 0 || len == -1) {
		printf("Socket disconnected fd:%d\n", con->fd);
		close(con->fd);
		con->fd = -1;
		return -1;
	}
	con->rxlen += len;
// 	printf("Received %d bytes on fd:%d type:%d id:%d\n", len, it->fd, it->type, it->id);
	return 0;
}



struct ConnectedSocket* conn_from_id(unsigned short id) {
	struct ConnectedSocket* con = connected_sockets;
	do {
		if (!con) break;
		
		if (con->id == id)
			return con;
		con = con->next;
	} while (con != connected_sockets);
	printf("Failed to lookup id %d\n", id);
	return NULL;
}


int conn_socket_data(struct ConnectedSocket* con) {
	int res;
	struct MSG_SocketData* msg_socketdata = (struct MSG_SocketData*)con->rxbuffer;
	if (msg_socketdata->len < 500)
		printf("Socket data %d bytes (%d bytes payload)\n", con->rxlen, msg_socketdata->len);
	if (con->rxlen < msg_socketdata->len + sizeof(struct MSG_SocketData))
		return 0;
	struct ConnectedSocket* connection = conn_from_id(msg_socketdata->id);
	if (connection) {
		res = send(connection->fd, con->rxbuffer+sizeof(struct MSG_SocketData), msg_socketdata->len, 0);
		assert(res == -1 || res == msg_socketdata->len);
	}
	return msg_socketdata->len + sizeof(struct MSG_SocketData);
}