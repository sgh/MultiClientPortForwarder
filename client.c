#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "connlist.h"
#include "messages.h"

void connection_handle(struct ConnectedSocket* con) {
	int res;
	if (conn_receive(con))
		return;

	if (con->type == CONN_DAEMON) {
		size_t consumed;
		struct CMD_ConnectPort* cmd_connectport = (struct CMD_ConnectPort*)con->rxbuffer;
		struct CMD_ClosePort*   cmd_closeport   = (struct CMD_ClosePort*)con->rxbuffer;
		
		do {
			consumed = 0;
			struct ConnectedSocket* connection;
			struct MSG_AckConnectPort ack;
			switch (con->rxbuffer[0]) {
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
					client_socket->clientserver_connection = con;
					client_socket->id = cmd_connectport->id;
					connlist_add(client_socket);

					res = send(client_socket->clientserver_connection->fd, &ack, sizeof(ack), 0);
					assert(res == sizeof(ack));
					consumed = sizeof(struct CMD_ConnectPort);
					break;
				case CMD_CLOSE_PORT:
					printf("Close port: %d\n", cmd_closeport->id);
					connection = conn_from_id(cmd_closeport->id);
					if (connection)
						conn_close(connection);
					consumed = sizeof(struct CMD_ClosePort);
					break;
				case MSG_SOCKET_DATA:
					consumed = conn_socket_data(con);
					break;
				default:
					printf("Invalid package\n");
					break;
			}
			if (consumed) {
				memmove(con->rxbuffer, con->rxbuffer + consumed, con->rxlen - consumed);
				con->rxlen -= consumed;
			}
		} while (consumed && con->rxlen);
	}
	
	/* Forward local socket data to the server */
	conn_forward(con);
}

int main(int argc, char *argv[]) {
	int sfd;
	fd_set rfd;
	struct timeval tv;

	sfd = create_client_socket("127.0.0.1", "12345");

	struct ConnectedSocket* new_socket = (struct ConnectedSocket*)malloc(sizeof(struct ConnectedSocket));
	memset(new_socket, 0, sizeof(struct ConnectedSocket));
	new_socket->fd = sfd;
	new_socket->type = CONN_DAEMON;
	connlist_add(new_socket);

	if (argc > 1) {
		struct MSG_IdentifyConnection identify;
		identify.type = MSG_IDENTIFY_CONNECTION;
		identify.len = strlen(argv[1]) + sizeof(identify);
		send(sfd, &identify, sizeof(identify), 0);
		send(sfd, argv[1], strlen(argv[1]), 0);
	}

	while (1) {
		int maxfd = -1;

		FD_ZERO(&rfd);
// 		printf("Listen: ");
		struct ConnectedSocket* it = connected_sockets;
		do {
			if (!it) break;
			if (sizeof(it->rxbuffer) <= it->rxlen)
				continue;
// 			printf(" %d", it->fd);
			FD_SET(it->fd, &rfd);
			if (maxfd < it->fd)
				maxfd = it->fd;
			it = it->next;
		} while (it != connected_sockets);
// 		printf("\n");

		if (maxfd == -1)
			break;

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

		} //else
// 			printf("client tick\n");
	}
	
	exit(EXIT_SUCCESS);
}
