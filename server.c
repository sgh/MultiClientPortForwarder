#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>

#include "messages.h"
#include "connlist.h"

unsigned int id_sequence = 1;

void connection_accept(struct ConnectedSocket* it) {
	int res;
	struct ConnectedSocket* new_socket = (struct ConnectedSocket*)malloc(sizeof(struct ConnectedSocket));
	memset(new_socket, 0, sizeof(struct ConnectedSocket));
	new_socket->fd = accept(it->fd, NULL, NULL);
	if (it->type == CONN_DAEMON_LISTEN) {
		printf("CONN_DAEMON_LISTEN\n");
		new_socket->type = CONN_DAEMON;
		
		int server_sockfd;
		struct ConnectedSocket* server_socket ;
		server_sockfd = create_server_socket("8080");
		server_socket = (struct ConnectedSocket*)malloc(sizeof(struct ConnectedSocket));
		memset(server_socket, 0, sizeof(struct ConnectedSocket));
		server_socket->fd = server_sockfd;
		server_socket->type = CONN_FORWARD_LISTEN;
		server_socket->clientsock = new_socket;
		connlist_add(server_socket);
	}
	if (it->type == CONN_FORWARD_LISTEN) {
		printf("Accept CONN_FORWARD_LISTEN\n");
		new_socket->type = CONN_FORWARD;
		new_socket->clientsock = it->clientsock;
		new_socket->id = id_sequence;
		struct CMD_ConnectPort cmd;
		cmd.type = CMD_CONNECT_PORT;
		cmd.port = 80;
		cmd.id = new_socket->id;
		id_sequence++;
		res = send(it->clientsock->fd, &cmd, sizeof(cmd), 0);
		assert(res == sizeof(cmd));
	}
	connlist_add(new_socket);
}

void connection_handle(struct ConnectedSocket* it) {
	conn_receive(it);

	int res;
	struct MSG_AckConnectPort* ack = it->rxbuffer;
	struct MSG_SocketData* msg_socketdata = it->rxbuffer;

	if (it->type == CONN_DAEMON) {
		int consumed;
		do {
			consumed = 0;
			switch (it->rxbuffer[0]) {
				case MSG_ACK_CONNECT_PORT:
					printf("Ack connect port: %d\n", ack->id);
					consumed = sizeof(struct MSG_AckConnectPort);
					break;
				case MSG_SOCKET_DATA:
// 					printf("Socket data %d bytes (%d bytes payload)\n", it->rxlen, msg_socketdata->len);
					if (it->rxlen < msg_socketdata->len + sizeof(struct MSG_SocketData))
						break;
					struct ConnectedSocket* connection = conn_from_id(msg_socketdata->id);
					if (connection) {
						res = send(connection->fd, it->rxbuffer+sizeof(struct MSG_SocketData), msg_socketdata->len, 0);
						if (res == -1) {
							conn_close(it);
							break;
						}
						assert(res == -1 || res == msg_socketdata->len);
					}
					consumed = msg_socketdata->len + sizeof(struct MSG_SocketData);
					break;
				default:
					printf("Invalid package\n");
					break;
			}
			if (consumed) {
				memmove(it->rxbuffer, it->rxbuffer + consumed, it->rxlen - consumed);
				it->rxlen -= consumed;
// 				printf("Consumed %d\n", consumed);
			}
		} while (consumed && it->rxlen);
// 		if (it->rxlen)
// 			printf("Left with %d pending bytes\n", it->rxlen);
	}

	/* Forward local socket data over the channel to the client */
	conn_forward(it, it->rxbuffer, it->rxlen);
	
	if (it->fd == -1 && it->type == CONN_FORWARD) {
		struct CMD_ClosePort cmd;
		cmd.type = CMD_CLOSE_PORT;
		cmd.id = it->id;
		res = send(it->clientsock->fd, &cmd, sizeof(cmd), 0);
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
		struct ConnectedSocket* it = connected_sockets;
		do {
			if (!it) break;
			if (sizeof(it->rxbuffer) <= it->rxlen)
				continue;
			FD_SET(it->fd, &rfd);
			if (maxfd < it->fd)
				maxfd = it->fd;
			it = it->next;
		} while (it != connected_sockets);

		tv.tv_usec = 0;
		tv.tv_sec = 1;
		if (select(maxfd+1, &rfd, NULL, NULL, &tv) > 0) {

			it = connected_sockets;
			do {
				if (!it)
					break;
				if (FD_ISSET(it->fd, &rfd)) {
					switch (it->type) {
						case CONN_DAEMON_LISTEN:
						case CONN_FORWARD_LISTEN:
							connection_accept(it);
							break;
						case CONN_DAEMON:
						case CONN_FORWARD:
							connection_handle(it);
							break;
					}
							
				}

				if (it->fd == -1) {
					struct ConnectedSocket* tmp = it;
					it = it->prev;
					connlist_delete(tmp);
				}
				it = it->next;
			} while (it != connected_sockets);

		} else
			printf("tick\n");
	}
}
