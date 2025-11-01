#include <cerrno>
#include <cstring>
#include <cstdio>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <string>
#include "utils.h"

const size_t k_max_msg = 4096;
static int32_t send_query(int fd, const uint8_t *text, size_t len) {
	if (len > k_max_msg) {
		return -1;
	}
	// here use 4 because uint32 occupies 4 bytes
	char wbuf[4 + k_max_msg];
	memcpy(wbuf, &len, 4);
	memcpy(&wbuf[4], text, len);
	if (int32_t err = write_all(fd, wbuf, 4 + len)) {
		return err;
	}
	return 0;
}

// TODO: add functio to process response from server
// fd refers to file discriptor
// static int32_t query(int fd, const char *text) {
// 	char rbuf[4 + k_max_msg + 1];
// 	errno = 0;
// 	int32_t err = read_full(fd, rbuf, 4);
// 	if (err) {
// 		printf(errno == 0 ? "EOF" : "read() error");
// 		return err;
// 	}
// 	// this gets the length of the message
// 	memcpy(&len, rbuf, 4);
// 	if (len > k_max_msg) {
// 		printf("msg too long");
// 		return -1;
// 	}
// 	err = read_full(fd, &rbuf[4], len);
// 	if (err) {
// 		printf("read() error");
// 		return err;
// 	}
// 
// 	printf("server says: %.*s\n", len, &rbuf[4]);
// 	return 0;
// }

int main() {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = ntohs(1234);
	addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);  // 127.0.0.1
	int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));

        std::vector<std::string> msg_list = {
          "fizz",
          "buzz",
          "foo",
          "bar"
	};

        for (const std::string &s : msg_list) {
	  int32_t err = send_query(fd, (uint8_t *)s.data(), s.size());
	  sleep(1);
	}                
	return 0;
}
