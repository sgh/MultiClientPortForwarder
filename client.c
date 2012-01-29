#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "connlist.h"
#include "messages.h"

#define BUF_SIZE 500

void connection_handle(struct ConnectedSocket* it) {
	int res;
	conn_receive(it);

	if (it->type == CONN_DAEMON) {
		int consumed;
		struct CMD_ConnectPort* cmd_connectport = it->rxbuffer;
		struct CMD_ClosePort*   cmd_closeport   = it->rxbuffer;
		struct MSG_SocketData*  msg_socketdata  = it->rxbuffer;
		
		do {
			consumed = 0;
			struct ConnectedSocket* connection;
			struct MSG_AckConnectPort ack;
			switch (it->rxbuffer[0]) {
				case CMD_CONNECT_PORT:
					printf("Connect id:%d to port: %d\n", cmd_connectport->id, cmd_connectport->port);
					ack.type = MSG_ACK_CONNECT_PORT;
					ack.id = cmd_connectport->id;
					char portbuf[8];
					sprintf(portbuf, "%d", cmd_connectport->port);
					int sockfd = create_client_socket("127.0.0.1", portbuf);

					struct ConnectedSocket* client_socket ;
					client_socket = (struct ConnectedSocket*)malloc(sizeof(struct ConnectedSocket));
					memset(client_socket, 0, sizeof(struct ConnectedSocket));
					client_socket->fd = sockfd;
					client_socket->type = CONN_FORWARD;
					client_socket->clientsock = it;
					client_socket->id = cmd_connectport->id;
					connlist_add(client_socket);

					res = send(client_socket->clientsock->fd, &ack, sizeof(ack), 0);
					assert(res == sizeof(ack));
					consumed = sizeof(struct CMD_ConnectPort);
					break;
				case CMD_CLOSE_PORT:
					printf("Close port: %d\n", cmd_closeport->id);
					connection = conn_from_id(cmd_closeport->id);
					close(connection->fd);
					connection->fd = -1;
					consumed = sizeof(struct CMD_ClosePort);
					break;
				case MSG_SOCKET_DATA:
// 					printf("Socket data %d bytes (%d bytes payload)\n", it->rxlen, msg_socketdata->len);
					if (it->rxlen < msg_socketdata->len + sizeof(struct MSG_SocketData))
						break;
					res = send(conn_from_id(msg_socketdata->id)->fd, it->rxbuffer+sizeof(struct MSG_SocketData), msg_socketdata->len, 0);
					assert(res == msg_socketdata->len);
					consumed = msg_socketdata->len + sizeof(struct MSG_SocketData);
					break;
				default:
					printf("Invalid package\n");
					break;
			}
			if (consumed) {
				memmove(it->rxbuffer, it->rxbuffer + consumed, it->rxlen - consumed);
				it->rxlen -= consumed;
			}
		} while (consumed && it->rxlen);
	}
	
	/* Forward local socket data to the server */
	conn_forward(it, it->rxbuffer, it->rxlen);
}

int main(/*int argc, char *argv[]*/) {	
	char buf[BUF_SIZE];
	int sfd, j;
	int len;
	int nread;
	fd_set rfd;
	struct timeval tv;

	sfd = create_client_socket("127.0.0.1", "12345");
	/* Send remaining command-line arguments as separate
	datagrams, and read responses from server */
	
	struct ConnectedSocket* new_socket = (struct ConnectedSocket*)malloc(sizeof(struct ConnectedSocket));
	memset(new_socket, 0, sizeof(struct ConnectedSocket));
	new_socket->fd = sfd;
	new_socket->type = CONN_DAEMON;
	connlist_add(new_socket);
	
	while (1) {
		int maxfd = 0;

		FD_ZERO(&rfd);
		struct ConnectedSocket* it = connected_sockets;
		do {
			if (!it) break;
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
			printf("client tick\n");
	}
	
	exit(EXIT_SUCCESS);
}
