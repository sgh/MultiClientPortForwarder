#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "messages.h"
#include "connlist.h"

unsigned int id_sequence = 1;

void connection_accept(struct ConnectedSocket* con) {
	int res;
	struct ConnectedSocket* new_connection = (struct ConnectedSocket*)malloc(sizeof(struct ConnectedSocket));
	memset(new_connection, 0, sizeof(struct ConnectedSocket));
	new_connection->fd = accept(con->fd, NULL, NULL);
	if (con->type == CONN_DAEMON_LISTEN) {
		printf("CONN_DAEMON_LISTEN\n");
		new_connection->type = CONN_DAEMON;
	}
	if (con->type == CONN_FORWARD_LISTEN) {
		printf("Accept CONN_FORWARD_LISTEN\n");
		new_connection->type = CONN_FORWARD;
		new_connection->clientserver_connection = con->clientserver_connection;
		new_connection->id = id_sequence;
		struct CMD_ConnectPort cmd;
		cmd.type = CMD_CONNECT_PORT;
		cmd.port = con->port;
		cmd.id = new_connection->id;
		res = send(con->clientserver_connection->fd, &cmd, sizeof(cmd), 0);
		assert(res == sizeof(cmd));
		id_sequence++;
		printf("CMD_CONNECT_PORT id:%d\n", cmd.id);
	}
	connlist_add(new_connection);
}

void connection_handle(struct ConnectedSocket* con) {
	if (conn_receive(con) == -1)
		return;

	int res;
	struct MSG_AckConnectPort* ack = (struct MSG_AckConnectPort*)con->rxbuffer;

	if (con->type == CONN_DAEMON) {
		int consumed;
		struct MSG_IdentifyConnection* identify = (struct MSG_IdentifyConnection*)con->rxbuffer;
		do {
			consumed = 0;
			switch (con->rxbuffer[0]) {
				case MSG_ACK_CONNECT_PORT:
					printf("Ack connect port: %d\n", ack->id);
					consumed = sizeof(struct MSG_AckConnectPort);
					break;
				case MSG_SOCKET_DATA:
					consumed = conn_socket_data(con);
					break;
				case MSG_IDENTIFY_CONNECTION: {
					int bufsize = identify->len - sizeof(struct MSG_IdentifyConnection) + 1;
                                        if (con->rxlen < identify->len)
                                                break;
					printf("bufsize:%d\n", bufsize);
					con->name = malloc(bufsize);
					memset(con->name, 0, bufsize);
					memcpy(con->name, (char*)identify + sizeof(struct MSG_IdentifyConnection), bufsize-1);
					printf("len:%d \"%s\"\n", identify->len, con->name);
					consumed = identify->len;

					int server_sockfd;
					struct ConnectedSocket* server_socket ;

					if (strcmp(con->name, "CLIENT1") == 0) {
						server_sockfd = create_server_socket("8080");
						server_socket = (struct ConnectedSocket*)malloc(sizeof(struct ConnectedSocket));
						memset(server_socket, 0, sizeof(struct ConnectedSocket));
						server_socket->fd = server_sockfd;
						server_socket->port = 80;
						server_socket->type = CONN_FORWARD_LISTEN;
						server_socket->clientserver_connection = con;
						connlist_add(server_socket);
					}

					if (strcmp(con->name, "CLIENT2") == 0) {
						server_sockfd = create_server_socket("2222");
						server_socket = (struct ConnectedSocket*)malloc(sizeof(struct ConnectedSocket));
						memset(server_socket, 0, sizeof(struct ConnectedSocket));
						server_socket->fd = server_sockfd;
						server_socket->port = 22;
						server_socket->type = CONN_FORWARD_LISTEN;
						server_socket->clientserver_connection = con;
						connlist_add(server_socket);
					}


					break;
				}
				default:
					printf("Invalid package\n");
					break;
			}
			if (consumed) {
                                assert( consumed <= con->rxlen );
                                memmove(con->rxbuffer, con->rxbuffer + consumed, con->rxlen - consumed);
				con->rxlen -= consumed;
// 				printf("Consumed %d\n", consumed);
			}
		} while (consumed && con->rxlen);
// 		if (it->rxlen)
// 			printf("Left with %d pending bytes\n", it->rxlen);
	}

	/* Forward local socket data over the channel to the client */
	conn_forward(con);
	
	if (con->fd == -1 && con->type == CONN_FORWARD) {
		struct CMD_ClosePort cmd;
		cmd.type = CMD_CLOSE_PORT;
		cmd.id = con->id;
		printf("Send closeport %d\n", cmd.id);
		res = send(con->clientserver_connection->fd, &cmd, sizeof(cmd), 0);
		assert(res == -1 || res == sizeof(cmd));
	}
}

int main() {

	int server_sockfd = create_server_socket("12345");
	struct ConnectedSocket* server_socket = (struct ConnectedSocket*)malloc(sizeof(struct ConnectedSocket));
	memset(server_socket, 0, sizeof(struct ConnectedSocket));
	server_socket->fd = server_sockfd;
	server_socket->type = CONN_DAEMON_LISTEN;
	connlist_add(server_socket);
		
	fd_set rfd;
	struct timeval tv;

	while (1) {
		int maxfd = 0;

		FD_ZERO(&rfd);
		struct ConnectedSocket* con = connected_sockets;
		do {
			if (!con) break;
			if (sizeof(con->rxbuffer) <= con->rxlen)
				continue;
			FD_SET(con->fd, &rfd);
			if (maxfd < con->fd)
				maxfd = con->fd;
			con = con->next;
		} while (con != connected_sockets);

		tv.tv_usec = 0;
		tv.tv_sec = 1;
		if (select(maxfd+1, &rfd, NULL, NULL, &tv) > 0) {

			con = connected_sockets;
			do {
				if (!con)
					break;
				if (FD_ISSET(con->fd, &rfd)) {
					switch (con->type) {
						case CONN_DAEMON_LISTEN:
						case CONN_FORWARD_LISTEN:
							connection_accept(con);
							break;
						case CONN_DAEMON:
						case CONN_FORWARD:
							connection_handle(con);
							break;
					}
							
				}

				if (con->fd == -1) {
					struct ConnectedSocket* tmp = con;
					con = con->prev;
					connlist_delete(tmp);
				}
				con = con->next;
			} while (con != connected_sockets);

		} //else
// 			printf("tick\n");
	}
}
