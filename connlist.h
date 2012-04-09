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

void connlist_add(ConnectedSocket* new_conn);
ConnectedSocket& conn_from_id(unsigned short id);

class ConnectedSocket {
protected:
	int    fd;
	char   type;

	std::string  name;

	/* FORWARD sockets */
	unsigned short port;
	ConnectedSocket* parent;
	SocketFifo rxfifo;
	SocketFifo txfifo;

public:
	bool pending_delete;
	unsigned short id;

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
	void conn_forward(ConnectedSocket& con);
	void conn_socket_data(ConnectedSocket& con);
	virtual void connection_handle() = 0;
};


int create_server_socket(const char* port);
int create_client_socket(const char* ip, const char* port);
void conn_forward(ConnectedSocket& con);
void conn_socket_data(ConnectedSocket& con);


struct ForwardSocket : ConnectedSocket {
	ForwardSocket(int fd, ConnectedSocket* parent, unsigned int id) {
		this->fd = fd;
		this->type = CONN_FORWARD;
		this->parent = parent;
		this->id = id;
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
			parent->txfifo_in( (unsigned char*)&cmd, sizeof(cmd) );
		}
	}
};

struct ForwardListenSocket : ConnectedSocket {
	ForwardListenSocket(int fd, int port, ConnectedSocket* parent) {
		this->fd = fd;
		this->type = CONN_FORWARD_LISTEN;
		this->parent = parent;
		this->port = port;
	}

	virtual void connection_handle() {
		int acceptedfd;
		acceptedfd = accept(fd, NULL, NULL);
		printf("Accept CONN_FORWARD_LISTEN\n");
		new ForwardSocket(acceptedfd, parent, id_sequence);
		struct CMD_ConnectPort cmd;
		cmd.type = CMD_CONNECT_PORT;
		cmd.port = port;
		cmd.id = id_sequence;
		parent->txfifo_in( (unsigned char*)&cmd, sizeof(cmd) );
		id_sequence++;
		printf("CMD_CONNECT_PORT id:%d\n", cmd.id);

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

		struct MSG_AckConnectPort* ack = (struct MSG_AckConnectPort*)rxfifo.get_out();

		int last_len = 0;
		struct MSG_IdentifyConnection* identify = (struct MSG_IdentifyConnection*)rxfifo.get_out();
		do {
			last_len = rxfifo.len();
			switch (rxfifo.get_out()[0]) {
				case MSG_ACK_CONNECT_PORT:
					printf("Ack connect port: %d\n", ack->id);
					rxfifo.skip( sizeof(struct MSG_AckConnectPort) );
					break;
				case MSG_SOCKET_DATA:
					conn_socket_data(*this);
					break;
				case MSG_IDENTIFY_CONNECTION: {
					int bufsize = identify->len - sizeof(struct MSG_IdentifyConnection) + 1;
					if (rxfifo.len() < identify->len)
						break;
					printf("bufsize:%d\n", bufsize);
					name.assign((char*)identify + sizeof(struct MSG_IdentifyConnection), bufsize-1);
					printf("len:%d \"%s\"\n", identify->len, name.c_str());
					rxfifo.skip(identify->len);

					if (	name == "CLIENT1") {
						int server_sockfd = create_server_socket("8080");
						new ForwardListenSocket(server_sockfd, 80, this);
					}

					if (name == "CLIENT2") {
						int server_sockfd = create_server_socket("2222");
						new ForwardListenSocket(server_sockfd, 22, parent);
					}
					break;
				}
				default:
					printf("Invalid package\n");
					break;
			}
		} while (last_len != rxfifo.len() && rxfifo.len());

	}
};


struct DaemonListenSocket : ConnectedSocket {
	DaemonListenSocket(int fd) {
		this->fd = fd;
		this->type = CONN_DAEMON_LISTEN;
	}

	virtual void connection_handle() {
		int acceptedfd;
		acceptedfd = accept(fd, NULL, NULL);
		if (type == CONN_DAEMON_LISTEN) {
			printf("CONN_DAEMON_LISTEN\n");
			new DaemonSocket(acceptedfd);
		}

	}
};

struct ClientSocket : ConnectedSocket {
	ClientSocket(int fd) {
		this->fd = fd;
		this->type = CONN_DAEMON;
	}

	virtual void connection_handle() {
		if (conn_receive() == -1)
			return;

		int last_len = 0;
		struct CMD_ConnectPort* cmd_connectport = (struct CMD_ConnectPort*)rxfifo.get_out();
		struct CMD_ClosePort*   cmd_closeport   = (struct CMD_ClosePort*)rxfifo.get_out();

		do {
			last_len = rxfifo.len();
			struct ConnectedSocket* connection;
			struct MSG_AckConnectPort ack;
			int fd;
			switch (*rxfifo.get_out()) {
				case CMD_CONNECT_PORT:
					printf("Connect id:%d to port: %d\n", cmd_connectport->id, cmd_connectport->port);
					ack.type = MSG_ACK_CONNECT_PORT;
					ack.id = cmd_connectport->id;
					char portbuf[8];
					sprintf(portbuf, "%d", cmd_connectport->port);

					fd = create_client_socket("127.0.0.1", portbuf);
					new ForwardSocket(fd, this, cmd_connectport->id);

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
		} while (last_len != rxfifo.len() && rxfifo.len());

		/* Forward local socket data to the server */
		conn_forward(*this);
	}

};



extern std::vector<ConnectedSocket*> connected_sockets;

std::vector<ConnectedSocket*>::iterator connlist_begin();

void eventloop();

#endif