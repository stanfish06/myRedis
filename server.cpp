#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <string.h>
#include <cassert>
#include <cerrno>
#include "utils.h"


static void login(int connfd) {
	char rbuf[64] = {};
	ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
	if (n < 0) {
		return;
	}
	printf("client says %s\n", rbuf);
	char wbuf[] = "world";
	write(connfd, wbuf, strlen(wbuf));
}


const size_t k_max_msg = 4096;
static int32_t one_request(int connfd) {
	char rbuf[4 + k_max_msg];
	errno = 0;
	// here you are reading the first 4 bytes in little endian
	// this just checks the length of the msg
	int32_t err = read_full(connfd, rbuf, 4);
	if (err) {
		printf(errno == 0 ? "EOF" : "read() error");
		return err;
	}
	uint32_t len = 0;
	memcpy(&len, rbuf, 4);
	if (len > k_max_msg) {
		return -1;
	}
	err = read_full(connfd, &rbuf[4], len);
	if (err) {
		printf("read() error");
		return err;
	}
	printf("client says: %.*s\n", len, &rbuf[4]);

	const char reply[] = "world";
	char wbuf[4 + sizeof(reply)];
	len = (uint32_t)strlen(reply);
	memcpy(wbuf, &len, 4);
	memcpy(&wbuf[4], reply, len);
	return write_all(connfd, wbuf, 4+len);
}

int main() {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	int val = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(1234);
	addr.sin_addr.s_addr = htonl(0);
	// this (const struct sockaddr *) is doing typecasting, which means typecast the address of sockaddr_in to the pointer of type sockaddr
	int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
	rv = listen(fd, SOMAXCONN);
	while (true) {
		struct sockaddr_in client_addr = {};
		socklen_t addrlen = sizeof(client_addr);
		int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);

		if (connfd < 0) {
			continue;
		}
		while (true) {
			int32_t err = one_request(connfd);
			if (err) {
				break;
			}
		}
		close(connfd);
	}
}

