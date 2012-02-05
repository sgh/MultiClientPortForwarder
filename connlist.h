#ifndef CONNLIST_H
#define CONNLIST_H

#include "messages.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>


#define CONN_DAEMON_LISTEN  1
#define CONN_DAEMON         2
#define CONN_FORWARD_LISTEN 3
#define CONN_FORWARD        4


struct ConnectedSocket;
struct ConnectedSocket {
	struct ConnectedSocket* next;
	struct ConnectedSocket* prev;
	int    fd;
	char   type;

	char           rxbuffer[10240];
	unsigned short rxlen;
	
	unsigned short id;
	char*  name;
	
	/* FORWARD sockets */
	unsigned short port;
	struct ConnectedSocket* clientserver_connection;
	
};

extern struct ConnectedSocket* connected_sockets;

void connlist_delete(struct ConnectedSocket* con);
void connlist_add(struct ConnectedSocket* new_conn);

int create_server_socket(const char* port);
int create_client_socket(const char* ip, const char* port);
void conn_forward(struct ConnectedSocket* con);
int conn_receive(struct ConnectedSocket* con);
void conn_close(struct ConnectedSocket* con);
int conn_socket_data(struct ConnectedSocket* con);

struct ConnectedSocket* conn_from_id(unsigned short id);
#endif