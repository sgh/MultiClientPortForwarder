#ifndef SOCKETFIFO_H
#define SOCKETFIFO_H

class SocketFifo {
	unsigned char _data[102400];
	int _len;

public:
	SocketFifo();
	unsigned int free();
	void inc(int len);
	void skip(int len);
	unsigned char* get_in();
	unsigned char* get_out();
	int len();
	int out(char* data, int len);
	int in(const unsigned char* data, int len);
};

#endif