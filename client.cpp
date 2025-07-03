#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include "utils.h"

const size_t k_max_msg = 4096;
static int32_t query(int fd, const char *text) {
	uint32_t len = (uint32_t)strlen(text);
	if (len > k_max_msg) {
		return -1;
	}
	char wbuf[4 + k_max_msg];
	memcpy(wbuf, &len, 4);
	memcpy(&wbuf[4], text, len);
	if (int32_t err = write_all(fd, wbuf, 4 + len)) {
		return err;
	}
	char rbuf[4 + k_max_msg + 1];
	errno = 0;
	int32_t err = read_full(fd, rbuf, 4);
	if (err) {
		printf(errno == 0 ? "EOF" : "read() error");
		return err;
	}
	memcpy(&len, rbuf, 4);
	if (len > k_max_msg) {
		printf("msg too long");
		return -1;
	}
	err = read_full(fd, &rbuf[4], len);
	if (err) {
		printf("read() error");
		return err;
	}

	printf("server says: %.*s\n", len, &rbuf[4]);
	return 0;
}

int main() {
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = ntohs(1234);
	addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);  // 127.0.0.1
	int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));

	int32_t err = query(fd, "hello");
	if (err) {
		goto L_DONE; 
	}
	
	err = query(fd, "hello again");
	if (err) {
		goto L_DONE; 
	}
L_DONE:
	close(fd);
	return 0;
}
