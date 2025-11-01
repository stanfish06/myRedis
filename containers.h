#include <cstdint>
#ifndef CONTAINERS_H
#define CONTAINERS_H

#include <vector>
// the state of a connection will be updated across multiple iterations, so state must be stored
typedef struct Conn {
	int fd = -1;
	bool want_read       = false;
	bool want_write      = false;
	bool want_close      = false;
	std::vector<uint8_t> incoming;
	std::vector<uint8_t> outgoing;
} Conn;

#endif
