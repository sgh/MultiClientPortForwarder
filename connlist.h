#ifndef CONNLIST_H
#define CONNLIST_H

#include "messages.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <assert.h>
// 	struct ConnectedSocket* next;
// 	struct ConnectedSocket* prev;

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

struct SocketFifo {
	SocketFifo() {
		_len = 0;
	}

	unsigned int free() {
		return sizeof(_data) - _len;
	}

	void inc(int len) {
		_len += len;
	}

	void skip(int len) {
		if (!len)
			return;
		_len -= len;
		if (_len && _len != len)
			memmove(_data, _data+len, _len);
	}

	unsigned char* getput() {
		return _data+_len;
	}

	int out(char* data, int len) {
		if (len > _len)
			len = _len;

		memcpy(data, _data, len);
		skip(len);
		return _len;
	}

	int in(const unsigned char* data, int len) {
		memcpy(_data+_len, data, len);
		_len += len;
		return len;
	}

	unsigned char _data[102400];
	int _len;
};

struct ConnectedSocket;

void connlist_add(ConnectedSocket* new_conn);

struct ConnectedSocket {
	void setup() {
		fd = -1;
		id = 0;
		port = 0;
// 		client_fd = 0;
		pending_delete = false;
		connlist_add(this);
	}
	ConnectedSocket() {
		setup();
	}

	ConnectedSocket(int type) {
		setup();
		this->type = type;
	}

	int conn_receive() {
		assert( rxfifo.free() );
		int len = recv(fd, rxfifo.getput(), rxfifo.free(), MSG_DONTWAIT);
		rxfifo.inc(len);
		if (len == 0 || len == -1) {
			printf("Socket disconnected fd:%d\n", fd);
			conn_close();
			return -1;
		}
	// 	printf("Received %d bytes on fd:%d type:%d id:%d\n", len, it->fd, it->type, it->id);
		return 0;
	}

	void conn_close() {
		printf("Closing id:%d fd:%d\n", id, fd);
		close (fd);
		fd = -1;
		connlist_delete();
	}

	void connlist_delete() {
		pending_delete = true;
	}

	int    fd;
	char   type;

	virtual void connection_handle() = 0;

	SocketFifo rxfifo;
	
	unsigned short id;
	std::string  name;

	/* FORWARD sockets */
	unsigned short port;
// 	int client_fd;
	ConnectedSocket* parent;
	bool pending_delete;
	SocketFifo txfifo;
};

void connection_accept(ConnectedSocket* pcon);

int create_server_socket(const char* port);
int create_client_socket(const char* ip, const char* port);
void conn_forward(ConnectedSocket& con);
void conn_socket_data(ConnectedSocket& con);


struct DaemonListenSocket : ConnectedSocket {
	DaemonListenSocket(int fd) {
		this->fd = fd;
		this->type = CONN_DAEMON_LISTEN;
	}

	virtual void connection_handle() {
		connection_accept(this);
	}
};


struct ForwardListenSocket : ConnectedSocket {
	ForwardListenSocket(int fd) {
		this->fd = fd;
		this->type = CONN_FORWARD_LISTEN;
	}

	virtual void connection_handle() {
		connection_accept(this);
	}
};


struct DaemonSocket : ConnectedSocket {
	DaemonSocket(int fd) {
		this->fd = fd;
		this->type = CONN_DAEMON;
	}

	virtual void connection_handle() {
		if (conn_receive() == -1)
			return;

		struct MSG_AckConnectPort* ack = (struct MSG_AckConnectPort*)rxfifo._data;

		int last_len = 0;
		struct MSG_IdentifyConnection* identify = (struct MSG_IdentifyConnection*)rxfifo._data;
		do {
			last_len = rxfifo._len;
			switch (rxfifo._data[0]) {
				case MSG_ACK_CONNECT_PORT:
					printf("Ack connect port: %d\n", ack->id);
					rxfifo.skip( sizeof(struct MSG_AckConnectPort) );
					break;
				case MSG_SOCKET_DATA:
					conn_socket_data(*this);
					break;
				case MSG_IDENTIFY_CONNECTION: {
					int bufsize = identify->len - sizeof(struct MSG_IdentifyConnection) + 1;
					if (rxfifo._len < identify->len)
						break;
					printf("bufsize:%d\n", bufsize);
					name.assign((char*)identify + sizeof(struct MSG_IdentifyConnection), bufsize-1);
					printf("len:%d \"%s\"\n", identify->len, name.c_str());
					rxfifo.skip(identify->len);

					int server_sockfd;

					if (	name == "CLIENT1") {
						server_sockfd = create_server_socket("8080");
						ForwardListenSocket* server_socket = new ForwardListenSocket(server_sockfd);
						server_socket->port = 80;
// 						server_socket->client_fd	 = fd;
						server_socket->parent = this;
					}

					if (name == "CLIENT2") {
						server_sockfd = create_server_socket("2222");
						ForwardListenSocket* server_socket = new ForwardListenSocket(server_sockfd);
						server_socket->port = 22;
// 						server_socket->client_fd = 	fd;
						server_socket->parent = this;
					}
					break;
				}
				default:
					printf("Invalid package\n");
					break;
			}
		} while (last_len != rxfifo._len && rxfifo._len);

	}
};


struct ForwardSocket : ConnectedSocket {
	ForwardSocket(int fd) {
		this->fd = fd;
		this->type = CONN_FORWARD;
	}

	virtual void connection_handle() {
		if (conn_receive() == -1)
			return;

// 		int res;

		/* Forward local socket data over the channel to the client */
		conn_forward(*this);
		if (fd == -1) {
			struct CMD_ClosePort cmd;
			cmd.type = CMD_CLOSE_PORT;
			cmd.id = id;
			printf("Send closeport %d\n", cmd.id);
// 			res = send(client_fd, &cmd, sizeof(cmd), 0);
// 			assert(res == -1 || res == sizeof(cmd));
			parent->txfifo.in( (unsigned char*)&cmd, sizeof(cmd) );
		}
	}
};

ConnectedSocket& conn_from_id(unsigned short id);

struct ClientSocket : ConnectedSocket {
	ClientSocket(int fd) {
		this->fd = fd;
		this->type = CONN_DAEMON;
	}

	virtual void connection_handle() {
		if (conn_receive() == -1)
			return;

		int last_len = 0;
		struct CMD_ConnectPort* cmd_connectport = (struct CMD_ConnectPort*)rxfifo._data;
		struct CMD_ClosePort*   cmd_closeport   = (struct CMD_ClosePort*)rxfifo._data;

		do {
			last_len = rxfifo._len;
			struct ConnectedSocket* connection;
			struct MSG_AckConnectPort ack;
			ForwardSocket* client_socket = NULL;
			int fd;
			switch (rxfifo._data[0]) {
				case CMD_CONNECT_PORT:
					printf("Connect id:%d to port: %d\n", cmd_connectport->id, cmd_connectport->port);
					ack.type = MSG_ACK_CONNECT_PORT;
					ack.id = cmd_connectport->id;
					char portbuf[8];
					sprintf(portbuf, "%d", cmd_connectport->port);

					fd = create_client_socket("127.0.0.1", portbuf);
					client_socket = new ForwardSocket(fd);
// 					client_socket->client_fd = this->fd;
					client_socket->parent = this;
					client_socket->id = cmd_connectport->id;

					std::cout << "Sending ack" << std::endl;
					txfifo.in( (unsigned char*)&ack, sizeof(ack));
					rxfifo.skip( sizeof(struct CMD_ConnectPort) );
					break;
				case CMD_CLOSE_PORT:
					printf("Close port: %d\n", cmd_closeport->id);
					connection = &conn_from_id(cmd_closeport->id);
					if (connection)
						(*connection).conn_close();
					rxfifo.skip( sizeof(struct CMD_ClosePort) );
					break;
				case MSG_SOCKET_DATA:
					conn_socket_data(*this);
					break;
				default:
					printf("Invalid package\n");
					break;
			}
		} while (last_len != rxfifo._len && rxfifo._len);

		/* Forward local socket data to the server */
		conn_forward(*this);
	}

};



extern unsigned int id_sequence;

extern std::vector<ConnectedSocket*> connected_sockets;

std::vector<ConnectedSocket*>::iterator connlist_begin();

void eventloop();

#endif