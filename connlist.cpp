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
		if ((*it)->_pending_delete) {
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
		
		if ((*it)->_id == id)
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
	_fd = -1;
	_id = 0;
	_parent = NULL;
	_pending_delete = false;
	connlist_add(this);
}


ConnectedSocket::ConnectedSocket() {
	setup();
}

ConnectedSocket::ConnectedSocket(int type) {
	setup();
}

ConnectedSocket::~ConnectedSocket() {
	conn_close();
}

int ConnectedSocket::conn_receive() {
	assert( _rxfifo.free() );
	int len = recv(_fd, _rxfifo.get_in(), _rxfifo.free(), MSG_DONTWAIT);
	_rxfifo.inc(len);
	if (len == 0 || len == -1) {
		printf("Socket disconnected fd:%d\n", _fd);
		conn_close();
		return -1;
	}
// 	printf("Received %d bytes on fd:%d type:%d id:%d\n", len, it->fd, it->type, it->id);
	return 0;
}

void ConnectedSocket::conn_close() {
	printf("Closing id:%d fd:%d\n", _id, _fd);
	close (_fd);
	_fd = -1;
	connlist_delete();
}

void ConnectedSocket::connlist_delete() {
	_pending_delete = true;
}

int ConnectedSocket::txfifo_in(const unsigned char* data, int len) {
	return _txfifo.in(data,len);
}

int ConnectedSocket::tx_len() {
	return _txfifo.len();
}

int ConnectedSocket::rx_free() {
	return _rxfifo.free();
}

int ConnectedSocket::rx_len() {
	return _rxfifo.len();
}

int ConnectedSocket::get_fd() {
	return _fd;
}

void ConnectedSocket::transmit() {
	printf("TX: fd:%d len:%d\n", _fd, _txfifo.len());
	int res = ::send(_fd, _txfifo.get_out(), _txfifo.len(), 0);
	if (res == -1) {
		conn_close();
	} else
		_txfifo.skip(res);
}


void ConnectedSocket::conn_socket_data(ConnectedSocket& con) {
	int res;
	struct MSG_SocketData* msg_socketdata = (struct MSG_SocketData*)con._rxfifo.get_out();
	int total_len = msg_socketdata->len + sizeof(struct MSG_SocketData);
	if (con._rxfifo.len() < total_len)
		return;
	if (msg_socketdata->len < 500)
		printf("MUX->SOCKET: data %d bytes (%d bytes payload)\n", con._rxfifo.len(), msg_socketdata->len);
	ConnectedSocket& connection = conn_from_id(msg_socketdata->id);
	res = connection.txfifo_in( con._rxfifo.get_out()+sizeof(struct MSG_SocketData), msg_socketdata->len);
	assert(res == -1 || res == msg_socketdata->len);
	con._rxfifo.skip(total_len);
}


ForwardSocket::ForwardSocket(int fd, ConnectedSocket* parent, unsigned int id) {
	this->_fd = fd;
	this->_parent = parent;
	this->_id = id;
	printf("Created ForwardSocket\n");
}


void ForwardSocket::forward_data() {
	const unsigned char* ptr = _rxfifo.get_out();
	struct MSG_SocketData data;
	data.type = MSG_SOCKET_DATA;
	data.id = _id;
	while (1) {
		data.len = rx_len();
		if (data.len > 1024)
			data.len = 1024;
		if (data.len == 0)
			break;
		_parent->txfifo_in( (unsigned char*)&data, sizeof(data) );
		_parent->txfifo_in( ptr, data.len );
		if (data.len < 500)
			printf("SOCKET->MUX: id:%d %d bytes payload\n", data.id, data.len);
		_rxfifo.skip(data.len);
		ptr += data.len;
	}
	assert(_rxfifo.len() == 0);
}


void ForwardSocket::connection_handle() {
	conn_receive();

	/* Forward local socket data over the channel to the client */
	forward_data();
	if (_fd == -1) {
		struct CMD_ClosePort cmd;
		cmd.type = CMD_CLOSE_PORT;
		cmd.id = _id;
		printf("Send closeport %d\n", cmd.id);
		_parent->txfifo_in( (unsigned char*)&cmd, sizeof(cmd) );
	}
}

ForwardListenSocket::ForwardListenSocket(int fd, int port, ConnectedSocket* parent) {
	this->_fd = fd;
	this->_parent = parent;
	this->_port = port;
}

void ForwardListenSocket::connection_handle() {
	int acceptedfd;
	acceptedfd = accept(_fd, NULL, NULL);
	printf("Accept CONN_FORWARD_LISTEN\n");
	new ForwardSocket(acceptedfd, _parent, id_sequence);
	struct CMD_ConnectPort cmd;
	cmd.type = CMD_CONNECT_PORT;
	cmd.port = _port;
	cmd.id = id_sequence;
	_parent->txfifo_in( (unsigned char*)&cmd, sizeof(cmd) );
	id_sequence++;
	printf("CMD_CONNECT_PORT id:%d\n", cmd.id);

}

ClientConnectionSocket::ClientConnectionSocket(int fd) {
	this->_fd = fd;
}


void ClientConnectionSocket::connection_handle() {
	if (conn_receive() == -1)
		return;

	struct MSG_AckConnectPort* ack = (struct MSG_AckConnectPort*)_rxfifo.get_out();

	int last_len = 0;
	struct MSG_IdentifyConnection* identify = (struct MSG_IdentifyConnection*)_rxfifo.get_out();
	struct CMD_ClosePort * closeport = (struct CMD_ClosePort*)_rxfifo.get_out();
	do {
		last_len = _rxfifo.len();
		switch (_rxfifo.get_out()[0]) {
			case MSG_ACK_CONNECT_PORT:
				printf("Ack connect port: %d\n", ack->id);
				_rxfifo.skip( sizeof(struct MSG_AckConnectPort) );
				break;
			case MSG_SOCKET_DATA:
				conn_socket_data(*this);
				break;
			case MSG_IDENTIFY_CONNECTION: {
				int bufsize = identify->len - sizeof(struct MSG_IdentifyConnection) + 1;
				if (_rxfifo.len() < identify->len)
					break;
				printf("bufsize:%d\n", bufsize);
				_name.assign((char*)identify + sizeof(struct MSG_IdentifyConnection), bufsize-1);
				printf("len:%d \"%s\"\n", identify->len, _name.c_str());
				_rxfifo.skip(identify->len);

				if (	_name == "CLIENT1") {
					int server_sockfd = create_server_socket("8080");
					new ForwardListenSocket(server_sockfd, 80, this);
				}

				if (_name == "CLIENT2") {
					int server_sockfd = create_server_socket("2222");
					new ForwardListenSocket(server_sockfd, 22, _parent);
				}
				break;
			}
			case CMD_CLOSE_PORT: {
				printf("CMD_CLOSE_PORT id:%d\n", closeport->id);
				conn_from_id(closeport->id).connlist_delete();
				_rxfifo.skip( sizeof(*closeport) );
				}
				break;
			default:
				printf("Invalid package\n");
				break;
		}
	} while (last_len != _rxfifo.len() && _rxfifo.len());

}

ServerDaemonSocket::ServerDaemonSocket(int fd) {
	this->_fd = fd;
}

void ServerDaemonSocket::connection_handle() {
	int acceptedfd;
	acceptedfd = accept(_fd, NULL, NULL);
	printf("CONN_DAEMON_LISTEN\n");
	new ClientConnectionSocket(acceptedfd);
}

ClientSocket::ClientSocket(int fd) {
	this->_fd = fd;
}

void ClientSocket::connection_handle() {
	if (conn_receive() == -1)
		return;

	int last_len = 0;
	struct CMD_ConnectPort* cmd_connectport = (struct CMD_ConnectPort*)_rxfifo.get_out();
	struct CMD_ClosePort*   cmd_closeport   = (struct CMD_ClosePort*)_rxfifo.get_out();

	do {
		last_len = _rxfifo.len();
		struct ConnectedSocket* connection;
		struct MSG_AckConnectPort ack;
		int fd;
		switch (*_rxfifo.get_out()) {
			case CMD_CONNECT_PORT:
				printf("Connect id:%d to port: %d\n", cmd_connectport->id, cmd_connectport->port);
				ack.type = MSG_ACK_CONNECT_PORT;
				ack.id = cmd_connectport->id;
				char portbuf[8];
				sprintf(portbuf, "%d", cmd_connectport->port);

				fd = create_client_socket("127.0.0.1", portbuf);
				new ForwardSocket(fd, this, cmd_connectport->id);

				std::cout << "Sending ack" << std::endl;
				_txfifo.in( (unsigned char*)&ack, sizeof(ack));
				_rxfifo.skip( sizeof(struct CMD_ConnectPort) );
				break;
			case CMD_CLOSE_PORT:
				printf("Close port: %d\n", cmd_closeport->id);
				conn_from_id(cmd_closeport->id).conn_close();
				_rxfifo.skip( sizeof(struct CMD_ClosePort) );
				break;
			case MSG_SOCKET_DATA:
				conn_socket_data(*this);
				break;
			default:
				printf("Invalid package\n");
				break;
		}
	} while (last_len != _rxfifo.len() && _rxfifo.len());
}