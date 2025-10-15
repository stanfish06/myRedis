#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <poll.h>

#include "utils.h"
#include "containers.h"

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

// make socket handle non-blocking
static void fd_set_nb(int fd) {
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

static Conn *handle_accept(int fd) {
	struct sockaddr_in client_addr = {};
	socklen_t addrlen = sizeof(client_addr);
	int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
	if (connfd < 0) {
		return NULL;
	}

	fd_set_nb(connfd);
	Conn *conn      = new Conn();
	conn->fd        = connfd;
	conn->want_read = true;
	return conn;
}

static Conn *handle_read(Conn *conn) {
	return conn;
}

static Conn *handle_write(Conn *conn) {
	return conn;
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

	std::vector<Conn *> fd2conn;
	std::vector<struct pollfd> poll_args;
	while (true) {
		poll_args.clear();
		struct pollfd pfd = {fd, POLLIN, 0};
		poll_args.push_back(pfd);
		for (Conn *conn : fd2conn) {
			if(!conn) {
				continue;
			}
			struct pollfd pfd = {fd, POLLERR, 0};
			if (conn->want_read) {
				pfd.events |= POLLIN;
			}
			if (conn->want_write) {
				pfd.events |= POLLOUT;
			}
			poll_args.push_back(pfd);
		}

		int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
		if (rv < 0 && errno == EINTR) {
			continue;
		}
		if (rv < 0) {
			return -1;
		}
		if (poll_args[0].revents) {
			if (Conn *conn = handle_accept(fd)) {
				if (fd2conn.size() <= (size_t)conn->fd) {
					fd2conn.resize(conn->fd + 1);
				}
				fd2conn[conn->fd] = conn;
			}
		}
		for (size_t i = 1; i < poll_args.size(); ++i) {
			uint32_t ready = poll_args[i].revents;
			Conn *conn = fd2conn[poll_args[i].fd];
			if (ready & POLLIN) {
				handle_read(conn);
			}
			if (ready & POLLOUT) {
				handle_write(conn);
			}

			if ((ready & POLLERR) || conn->want_close) {
				(void)close(conn->fd);
				fd2conn[conn->fd] = NULL;
				delete conn;
			}
		}
	}

	// while (true) {
	// 	struct sockaddr_in client_addr = {};
	// 	socklen_t addrlen = sizeof(client_addr);
	// 	int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
	//
	// 	if (connfd < 0) {
	// 		continue;
	// 	}
	// 	while (true) {
	// 		int32_t err = one_request(connfd);
	// 		if (err) {
	// 			break;
	// 		}
	// 	}
	// 	close(connfd);
	// }
}

