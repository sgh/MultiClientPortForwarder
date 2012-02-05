#ifndef MESSAGES_H
#define MESSAGES_H


/* Instruct client to connect to a remote port */
#define CMD_CONNECT_PORT           0x10
struct CMD_ConnectPort {
	char type;
	unsigned short port;
	unsigned short id;
};

/* Instruct client to connect to a specific connection */
#define CMD_CLOSE_PORT           0x11
struct CMD_ClosePort {
	char type;
	unsigned short id;
};


/* Reply from the client */
#define MSG_ACK_CONNECT_PORT  0x12
struct MSG_AckConnectPort {
	char type;
	unsigned int id;
};

/* Raw data that must be relayed to a socket */
#define MSG_SOCKET_DATA            0x13
struct MSG_SocketData {
	char type;
	unsigned int id;
	unsigned short len;
	/* data */
};

/* Indentify connection name */
#define MSG_INDENTIFY_CONNECTION   0x14
struct MSG_IdentifyConnection {
	char type;
	unsigned int id;
	unsigned short len;
	/* data, string */
};

#endif