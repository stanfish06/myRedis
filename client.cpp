#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <string>
#include "utils.h"

// request: | len | nstr | len | str1 | len | str2 | ...
static int32_t send_req(int fd, const std::vector<std::string> &cmd) {
	size_t len = 4;
	for (const std::string &s : cmd) {
		len += 4 + s.size();
	}
	if (len > k_max_msg) {
		fprintf(stderr, "request too long: %zu > %zu bytes\n", len, k_max_msg);
		return -1;
	}
	// here use 4 because uint32 occupies 4 bytes
	char wbuf[4 + k_max_msg];
	uint32_t u32 = (uint32_t)len;
	memcpy(&wbuf[0], &u32, 4);
	u32 = (uint32_t)cmd.size();
	memcpy(&wbuf[4], &u32, 4);
	size_t cur = 8;
	for (const std::string &s : cmd) {
		u32 = (uint32_t)s.size();
		memcpy(&wbuf[cur], &u32, 4);
		memcpy(&wbuf[cur + 4], s.data(), s.size());
		cur += 4 + s.size();
	}
	return write_all(fd, wbuf, 4 + len);
}

// response: | len | status | data |, len covers status + data
static int32_t read_res(int fd, uint32_t &status, std::string &data) {
	char rbuf[4 + k_max_msg];
	errno = 0;
	if (read_full(fd, rbuf, 4)) {
		fprintf(stderr, errno == 0 ? "EOF\n" : "read() error\n");
		return -1;
	}
	// this gets the length of the message
	uint32_t len = 0;
	memcpy(&len, rbuf, 4);
	if (len < 4 || len > k_max_msg) {
		fprintf(stderr, "bad response length %u\n", len);
		return -1;
	}
	if (read_full(fd, &rbuf[4], len)) {
		fprintf(stderr, "read() error\n");
		return -1;
	}
	memcpy(&status, &rbuf[4], 4);
	data.assign(&rbuf[8], len - 4);
	return 0;
}

static void usage() {
	fprintf(stderr,
	    "usage: client [-h host] [-p port] <cmd> [args...]\n"
	    "  cmds: get k | set k v | del k | whoami\n"
	    "  e.g.  client -h localhost -p 1234 set k v\n");
	exit(2);
}

int main(int argc, char **argv) {
	const char *host = "127.0.0.1";
	const char *port = "1234";
	int opt;
	// '+' stops option parsing at the command, so args like "-1" stay args
	while ((opt = getopt(argc, argv, "+h:p:")) != -1) {
		switch (opt) {
		case 'h': host = optarg; break;
		case 'p': port = optarg; break;
		default: usage();
		}
	}
	if (optind >= argc) {
		usage();
	}
	std::vector<std::string> cmd(argv + optind, argv + argc);

	// resolve so DNS names (k8s service names) work, IPv4 only since the server binds IPv4
	struct addrinfo hints = {};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo *res = NULL;
	if (int rv = getaddrinfo(host, port, &hints, &res)) {
		fprintf(stderr, "resolve %s:%s: %s\n", host, port, gai_strerror(rv));
		return 1;
	}
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
		fprintf(stderr, "connect %s:%s: %s\n", host, port, strerror(errno));
		return 1;
	}
	freeaddrinfo(res);

	uint32_t status = 0;
	std::string data;
	if (send_req(fd, cmd) || read_res(fd, status, data)) {
		return 1;
	}
	close(fd);

	switch (status) {
	case RES_OK:
		printf("%s\n", data.empty() ? "OK" : data.c_str());
		return 0;
	case RES_NX:
		printf("(nil)\n");
		return 0;
	default:
		printf("(err) %s\n", data.c_str());
		return 1;
	}
}
