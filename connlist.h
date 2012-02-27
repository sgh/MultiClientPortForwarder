#ifndef CONNLIST_H
#define CONNLIST_H

#include "messages.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
// 	struct ConnectedSocket* next;
// 	struct ConnectedSocket* prev;

#include <vector>
#include <string>

#define CONN_DAEMON_NONE    0
#define CONN_DAEMON_LISTEN  1
#define CONN_DAEMON         2
#define CONN_FORWARD_LISTEN 3
#define CONN_FORWARD        4


struct ConnectedSocket;
struct ConnectedSocket {
	ConnectedSocket() {
		fd = -1;
		type = CONN_DAEMON_NONE;
		rxlen = 0;
		id = 0;
		port = 0;
		client_fd = 0;
		pending_delete = false;
	}
	int    fd;
	char   type;

	char           rxbuffer[10240];
	unsigned short rxlen;
	
	unsigned short id;
	std::string  name;
	
	/* FORWARD sockets */
	unsigned short port;
	int client_fd;
	bool pending_delete;
	
};

extern std::vector<ConnectedSocket> connected_sockets;

std::vector<ConnectedSocket>::iterator connlist_begin();
void connlist_delete(ConnectedSocket& con);
void connlist_add(ConnectedSocket& new_conn);

int create_server_socket(const char* port);
int create_client_socket(const char* ip, const char* port);
void conn_forward(ConnectedSocket& con);
int conn_receive(ConnectedSocket& con);
void conn_close(ConnectedSocket& con);
int conn_socket_data(ConnectedSocket& con);

ConnectedSocket& conn_from_id(unsigned short id);
#endif