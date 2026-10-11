#include <cassert>
#include <cstdint>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#ifndef UTILS_H
#define UTILS_H

// max frame body size, shared by client and server
const size_t k_max_msg = 4096;

// response status codes
enum {
	RES_OK  = 0,
	RES_ERR = 1,  // unknown command or bad args, data holds the message
	RES_NX  = 2,  // key not found
};

// this is primarily make sure exactly n bytes are read,
// which is not always true. For example, if you only read
// a bytes (a < n) in the first msg, you need read anothe r msg (or maybe more)
inline static int32_t read_full(int fd, char *buf, size_t n) {
	while(n > 0) {
		ssize_t rv = read(fd, buf, n);
		if (rv <= 0) {
			return -1;
		}
		assert((size_t)rv <= n);
		n -= (size_t)rv;
		buf += rv;
	}
	return 0;
}

// size_t is a type definition of unsigned integer
inline static int32_t write_all(int fd, const char *buf, size_t n) {
	while (n > 0) {
		ssize_t rv = write(fd, buf, n);
		if (rv <= 0) {
			return -1;
		}
		assert((size_t)rv <= n);
		n -= (size_t)rv;
		buf += rv;
	}
	return 0;
}


#endif
