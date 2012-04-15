#ifndef CONNLIST_H
#define CONNLIST_H

#include "socketfifo.h"
#include "messages.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <assert.h>

#include <vector>
#include <string>
#include <string.h>
#include <stdio.h>
#include <iostream>

#define CONN_DAEMON_NONE    0
#define CONN_DAEMON_LISTEN  1
#define CONN_DAEMON         2
#define CONN_FORWARD_LISTEN 3
#define CONN_FORWARD        4

extern unsigned int id_sequence;

class ConnectedSocket;

extern std::vector<ConnectedSocket*> connected_sockets;

ConnectedSocket& conn_from_id(unsigned short id);
std::vector<ConnectedSocket*>::iterator connlist_begin();

int create_server_socket(const char* port);
int create_client_socket(const char* ip, const char* port);

void connlist_add(ConnectedSocket* new_conn);
void conn_socket_data(ConnectedSocket& con);

void eventloop();


class ConnectedSocket {
protected:
	int    _fd;
	char   _type;

	std::string  _name;

	/* FORWARD sockets */
	unsigned short _port;
	ConnectedSocket* _parent;
	SocketFifo _rxfifo;
	SocketFifo _txfifo;

public:
	bool _pending_delete;
	unsigned short _id;

	void setup();
	ConnectedSocket();
	ConnectedSocket(int type);
	int conn_receive();
	void conn_close();
	void connlist_delete();
	int txfifo_in(const unsigned char* data, int len);
	int tx_len();
	int rx_free();
	int rx_len();
	int get_fd();
	int get_port();
	void transmit();
	void conn_socket_data(ConnectedSocket& con);
	virtual void connection_handle() = 0;
	~ConnectedSocket();
};


/**
 * An established forwarded connection
 */
class ForwardSocket : public ConnectedSocket {
	void forward_data();
public:
	ForwardSocket(int fd, ConnectedSocket* parent, unsigned int id);
	virtual void connection_handle();
};


/**
 * Used by the server for waiting for connections on sockets that should be
 * forwarded to a port on the client
 */
class ForwardListenSocket : public ConnectedSocket {
public:
	ForwardListenSocket(int fd, int port, ConnectedSocket* parent);
	virtual void connection_handle();
};


/**
 * Class used by the server for each client connection 
 */
class ClientConnectionSocket : public ConnectedSocket {
public:
	ClientConnectionSocket(int fd);
	virtual void connection_handle();
};


/**
 * Class used my the central server daemon
 */
class ServerDaemonSocket : public ConnectedSocket {
public:
	ServerDaemonSocket(int fd);
	virtual void connection_handle();
};

class ClientSocket : public ConnectedSocket {
public:
	ClientSocket(int fd);
	void connection_handle();

};

#endif