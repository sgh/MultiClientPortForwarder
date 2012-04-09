#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <iostream>
#include <algorithm>

#include "connlist.h"

std::vector<ConnectedSocket*> connected_sockets;
std::vector<ConnectedSocket*> connected_sockets_to_be_added;
unsigned int id_sequence = 1;


void connlist_add(ConnectedSocket* new_conn) {
	connected_sockets_to_be_added.push_back(new_conn);
	std::cout << "Added connection - fd:" << new_conn->get_fd() << ". New size is " << connected_sockets_to_be_added.size() << std::endl;
}


std::vector<ConnectedSocket*>::iterator connlist_begin() {

	std::vector<ConnectedSocket*>::iterator it = connected_sockets.begin();
	while (it != connected_sockets.end()) {
		if ((*it)->pending_delete) {
			std::vector<ConnectedSocket*>::iterator tmp = it;
			delete (*it);
			it--;
			connected_sockets.erase(tmp);
		}

		it++;
	}

	if (connected_sockets_to_be_added.size() > 0) {
		connected_sockets.insert(connected_sockets.end(), connected_sockets_to_be_added.begin(), connected_sockets_to_be_added.end());
		std::cout << "Realy added connection. New size is " << connected_sockets.size() << std::endl;
		connected_sockets_to_be_added.clear();
	}

	return connected_sockets.begin();
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


ConnectedSocket& conn_from_id(unsigned short id) {
	std::vector<ConnectedSocket*>::iterator it = connected_sockets.begin();
	while (it != connected_sockets.end()) {
		
		if ((*it)->id == id)
			return *(*it);
		it++;
	}
	printf("Failed to lookup id %d\n", id);
	assert( 0 );
}


void eventloop() {
	fd_set rfd;
	fd_set wfd;
	struct timeval tv;

	while (1) {
		int maxfd = 0;

		FD_ZERO(&rfd);
		FD_ZERO(&wfd);
		std::vector<ConnectedSocket*>::iterator it = connlist_begin();
		while (it != connected_sockets.end()) {
			ConnectedSocket& con = **it;

			// Read from sockets only if rxfifo is empty
			if (con.rx_free()) {
				FD_SET(con.get_fd(), &rfd);
				if (maxfd < con.get_fd())
					maxfd = con.get_fd();
			}

			// Write only to sockets when write-data is available
			if (con.tx_len()) {
				FD_SET(con.get_fd(), &wfd);
				if (maxfd < con.get_fd())
					maxfd = con.get_fd();
			}

			it++;
		}

		tv.tv_usec = 0;
		tv.tv_sec = 1;
		if (select(maxfd+1, &rfd, &wfd, NULL, &tv) > 0) {

			std::vector<ConnectedSocket*>::iterator it = connlist_begin();
			std::cout << "Traversion " << connected_sockets.size() << " connections." << std::endl;
			while (it != connected_sockets.end()) {
				ConnectedSocket& con = **it;
				std::cout << "Checking fd:" << con.get_fd() << std::endl;
				if (FD_ISSET(con.get_fd(), &rfd))
					con.connection_handle();

				if (FD_ISSET(con.get_fd(), &wfd)) {
					con.transmit();
				}

				it++;
			}

		} else
			printf("tick\n");

	}
}



void ConnectedSocket::setup() {
	fd = -1;
	id = 0;
	port = 0;
	parent = NULL;
	pending_delete = false;
	connlist_add(this);
}


ConnectedSocket::ConnectedSocket() {
	setup();
}

ConnectedSocket::ConnectedSocket(int type) {
	setup();
	this->type = type;
}

int ConnectedSocket::conn_receive() {
	assert( rxfifo.free() );
	int len = recv(fd, rxfifo.get_in(), rxfifo.free(), MSG_DONTWAIT);
	rxfifo.inc(len);
	if (len == 0 || len == -1) {
		printf("Socket disconnected fd:%d\n", fd);
		conn_close();
		return -1;
	}
// 	printf("Received %d bytes on fd:%d type:%d id:%d\n", len, it->fd, it->type, it->id);
	return 0;
}

void ConnectedSocket::conn_close() {
	printf("Closing id:%d fd:%d\n", id, fd);
	close (fd);
	fd = -1;
	connlist_delete();
}

void ConnectedSocket::connlist_delete() {
	pending_delete = true;
}

int ConnectedSocket::txfifo_in(const unsigned char* data, int len) {
	return txfifo.in(data,len);
}

int ConnectedSocket::tx_len() {
	return txfifo.len();
}

int ConnectedSocket::rx_free() {
	return rxfifo.free();
}

int ConnectedSocket::rx_len() {
	return rxfifo.len();
}

int ConnectedSocket::get_fd() {
	return fd;
}

int ConnectedSocket::get_port() {
	return port;
}

void ConnectedSocket::transmit() {
	printf("TX: fd:%d len:%d\n", fd, txfifo.len());
	int res = ::send(fd, txfifo.get_out(), txfifo.len(), 0);
	if (res == -1) {
		conn_close();
	} else
		txfifo.skip(res);
}

void ConnectedSocket::conn_forward(ConnectedSocket& con) {
	const unsigned char* ptr = con.rxfifo.get_out();
	struct MSG_SocketData data;
	data.type = MSG_SOCKET_DATA;
	data.id = con.id;
	while (1) {
		data.len = con.rx_len();
		if (data.len > 1024)
			data.len = 1024;
		if (data.len == 0)
			break;
		con.parent->txfifo_in( (unsigned char*)&data, sizeof(data) );
		con.parent->txfifo_in( ptr, data.len );
		if (data.len < 500)
			printf("SOCKET->MUX: id:%d %d bytes payload\n", data.id, data.len);
		con.rxfifo.skip(data.len);
		ptr += data.len;
	}
	assert(con.rxfifo.len() == 0);
}

void ConnectedSocket::conn_socket_data(ConnectedSocket& con) {
	int res;
	struct MSG_SocketData* msg_socketdata = (struct MSG_SocketData*)con.rxfifo.get_out();
	int total_len = msg_socketdata->len + sizeof(struct MSG_SocketData);
	if (con.rxfifo.len() < total_len)
		return;
	if (msg_socketdata->len < 500)
		printf("MUX->SOCKET: data %d bytes (%d bytes payload)\n", con.rxfifo.len(), msg_socketdata->len);
	ConnectedSocket& connection = conn_from_id(msg_socketdata->id);
	res = connection.txfifo_in( con.rxfifo.get_out()+sizeof(struct MSG_SocketData), msg_socketdata->len);
	assert(res == -1 || res == msg_socketdata->len);
	con.rxfifo.skip(total_len);
}


ForwardSocket::ForwardSocket(int fd, ConnectedSocket* parent, unsigned int id) {
	this->fd = fd;
	this->type = CONN_FORWARD;
	this->parent = parent;
	this->id = id;
}


void ForwardSocket::connection_handle() {
	if (conn_receive() == -1)
		return;

	/* Forward local socket data over the channel to the client */
	conn_forward(*this);
	if (fd == -1) {
		struct CMD_ClosePort cmd;
		cmd.type = CMD_CLOSE_PORT;
		cmd.id = id;
		printf("Send closeport %d\n", cmd.id);
		parent->txfifo_in( (unsigned char*)&cmd, sizeof(cmd) );
	}
}

ForwardListenSocket::ForwardListenSocket(int fd, int port, ConnectedSocket* parent) {
	this->fd = fd;
	this->type = CONN_FORWARD_LISTEN;
	this->parent = parent;
	this->port = port;
}

void ForwardListenSocket::connection_handle() {
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

DaemonSocket::DaemonSocket(int fd) {
	this->fd = fd;
	this->type = CONN_DAEMON;
}


void DaemonSocket::connection_handle() {
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

DaemonListenSocket::DaemonListenSocket(int fd) {
	this->fd = fd;
	this->type = CONN_DAEMON_LISTEN;
}

void DaemonListenSocket::connection_handle() {
	int acceptedfd;
	acceptedfd = accept(fd, NULL, NULL);
	if (type == CONN_DAEMON_LISTEN) {
		printf("CONN_DAEMON_LISTEN\n");
		new DaemonSocket(acceptedfd);
	}

}

ClientSocket::ClientSocket(int fd) {
	this->fd = fd;
	this->type = CONN_DAEMON;
}

void ClientSocket::connection_handle() {
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