#include <cstdint>
#ifndef CONTAINERS_H
#define CONTAINERS_H

#include <vector>
typedef struct Conn {
	int fd = -1;
	bool want_read       = false;
	bool want_write      = false;
	bool want_close      = false;
	std::vector<uint8_t> incomming;
	std::vector<uint8_t> outgoing;
} Conn;

#endif
