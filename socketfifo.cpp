#include "socketfifo.h"

#include <string.h>

SocketFifo::SocketFifo() {
	_len = 0;
}

unsigned int SocketFifo::free() {
	return sizeof(_data) - _len;
}

void SocketFifo::inc(int len) {
	_len += len;
}

void SocketFifo::skip(int len) {
	if (!len)
		return;
	_len -= len;
	if (_len && _len != len)
		memmove(_data, _data+len, _len);
}

unsigned char* SocketFifo::get_in() {
	return _data+_len;
}

unsigned char* SocketFifo::get_out() {
	return _data;
}

int SocketFifo::len() {
	return _len;
}

int SocketFifo::out(char* data, int len) {
	if (len > _len)
		len = _len;

	memcpy(data, _data, len);
	skip(len);
	return _len;
}

int SocketFifo::in(const unsigned char* data, int len) {
	memcpy(_data+_len, data, len);
	_len += len;
	return len;
}