#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "utils.h"
#include "containers.h"

const size_t k_max_args = 1024;

// key-value store shared by all connections
static std::map<std::string, std::string> g_data;
// returned by whoami, in k8s this is the pod name
static char g_hostname[256] = "unknown";
// set by SIGTERM/SIGINT, checked by the event loop
static volatile sig_atomic_t g_stop = 0;

static void on_signal(int) {
	g_stop = 1;
}

static void die(const char *msg) {
	fprintf(stderr, "%s: %s\n", msg, strerror(errno));
	exit(1);
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
	uint32_t ip = client_addr.sin_addr.s_addr;
	fprintf(stderr, "new client from %u.%u.%u.%u:%u\n",
	    ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
	    ntohs(client_addr.sin_port)
	);

	fd_set_nb(connfd);
	Conn *conn      = new Conn();
	conn->fd        = connfd;
	conn->want_read = true;
	return conn;
}

static void buf_append(std::vector<uint8_t> &buf, const uint8_t *data, size_t len) {
	buf.insert(buf.end(), data, data + len);
}

static void buf_consume(std::vector<uint8_t> &buf, size_t n) {
	buf.erase(buf.begin(), buf.begin() + n);
}

// read a u32 at cur and advance it, false if not enough bytes left
static bool read_u32(const uint8_t *&cur, const uint8_t *end, uint32_t &out) {
	if ((size_t)(end - cur) < 4) {
		return false;
	}
	memcpy(&out, cur, 4);
	cur += 4;
	return true;
}

static bool read_str(const uint8_t *&cur, const uint8_t *end, size_t n, std::string &out) {
	if ((size_t)(end - cur) < n) {
		return false;
	}
	out.assign(cur, cur + n);
	cur += n;
	return true;
}

// request body: | nstr | len | str1 | len | str2 | ...
static int32_t parse_req(const uint8_t *data, size_t size, std::vector<std::string> &out) {
	const uint8_t *end = data + size;
	uint32_t nstr = 0;
	if (!read_u32(data, end, nstr) || nstr > k_max_args) {
		return -1;
	}
	while (out.size() < nstr) {
		uint32_t len = 0;
		if (!read_u32(data, end, len)) {
			return -1;
		}
		out.push_back(std::string());
		if (!read_str(data, end, len, out.back())) {
			return -1;
		}
	}
	// trailing bytes mean the frame is malformed
	return data == end ? 0 : -1;
}

struct Response {
	uint32_t status = RES_OK;
	std::string data;
};

static void do_request(const std::vector<std::string> &cmd, Response &out) {
	if (cmd.size() == 2 && cmd[0] == "get") {
		auto it = g_data.find(cmd[1]);
		if (it == g_data.end()) {
			out.status = RES_NX;
			return;
		}
		out.data = it->second;
	} else if (cmd.size() == 3 && cmd[0] == "set") {
		g_data[cmd[1]] = cmd[2];
	} else if (cmd.size() == 2 && cmd[0] == "del") {
		g_data.erase(cmd[1]);
	} else if (cmd.size() == 1 && cmd[0] == "whoami") {
		out.data = g_hostname;
	} else {
		out.status = RES_ERR;
		out.data = "unknown command or wrong number of args";
	}
}

// response frame: | len | status | data |, len covers status + data
static void make_response(const Response &resp, std::vector<uint8_t> &out) {
	uint32_t len = 4 + (uint32_t)resp.data.size();
	buf_append(out, (const uint8_t *)&len, 4);
	buf_append(out, (const uint8_t *)&resp.status, 4);
	buf_append(out, (const uint8_t *)resp.data.data(), resp.data.size());
}

static bool try_one_request(Conn *conn) {
	if (conn->incoming.size() < 4) {
		return false;
	}
	uint32_t len = 0;
	memcpy(&len, conn->incoming.data(), 4);
	// message too long
	if (len > k_max_msg) {
		conn->want_close = true;
		return false;
	}
	// frame not fully received yet, wait for the next read
	if (4 + len > conn->incoming.size()) {
		return false;
	}
	const uint8_t *request = &conn->incoming[4];

	std::vector<std::string> cmd;
	if (parse_req(request, len, cmd) < 0) {
		printf("fd %d: bad request, closing\n", conn->fd);
		conn->want_close = true;
		return false;
	}
	Response resp;
	do_request(cmd, resp);
	printf("fd %d: %s -> status %u\n", conn->fd, cmd.empty() ? "(empty)" : cmd[0].c_str(), resp.status);

	make_response(resp, conn->outgoing);
	buf_consume(conn->incoming, 4 + len);
	return true;
}

static void handle_read(Conn *conn) {
	uint8_t buf[64 * 1024];
	ssize_t rv = read(conn->fd, buf, sizeof(buf));
	// not actually ready, retry on the next poll
	if (rv < 0 && errno == EAGAIN) {
		return;
	}
	// 0 means the client closed the connection
	if (rv <= 0) {
		conn->want_close = true;
		return;
	}
	buf_append(conn->incoming, buf, (size_t)rv);
	// one read can hold several pipelined requests
	while (try_one_request(conn)) {}

	if (conn->outgoing.size() > 0) {
		conn->want_read = false;
		conn->want_write = true;
	}
}

static void handle_write(Conn *conn) {
	assert(conn->outgoing.size() > 0);
	ssize_t rv = write(conn->fd, conn->outgoing.data(), conn->outgoing.size());
	if (rv < 0 && errno == EAGAIN) {
		return;
	}
	if (rv < 0) {
		conn->want_close = true;
		return;
	}
	buf_consume(conn->outgoing, (size_t)rv);
	if (conn->outgoing.size() == 0) {
		conn->want_read = true;
		conn->want_write = false;
	}
}

int main() {
	// line-buffer stdout so logs appear immediately when it is a pipe (docker, kubectl logs)
	setvbuf(stdout, NULL, _IOLBF, 0);
	// writing to a client that already disconnected would otherwise kill the server
	signal(SIGPIPE, SIG_IGN);
	// no SA_RESTART so poll() returns EINTR and the loop sees g_stop
	struct sigaction sa = {};
	sa.sa_handler = on_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	gethostname(g_hostname, sizeof(g_hostname) - 1);
	int port = 1234;
	if (const char *env = getenv("PORT")) {
		port = atoi(env);
		if (port <= 0 || port > 65535) {
			fprintf(stderr, "invalid PORT: %s\n", env);
			return 1;
		}
	}

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		die("socket()");
	}
	int val = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(0);
	// this (const struct sockaddr *) is doing typecasting, which means typecast the address of sockaddr_in to the pointer of type sockaddr
	if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
		die("bind()");
	}
	if (listen(fd, SOMAXCONN) < 0) {
		die("listen()");
	}
	printf("listening on port %d as %s\n", port, g_hostname);

	std::vector<Conn *> fd2conn;
	std::vector<struct pollfd> poll_args;
	while (!g_stop) {
		poll_args.clear();
		struct pollfd pfd = {fd, POLLIN, 0};
		poll_args.push_back(pfd);
		for (Conn *conn : fd2conn) {
			if(!conn) {
				continue;
			}
			struct pollfd pfd = {conn->fd, POLLERR, 0};
			if (conn->want_read) {
				pfd.events |= POLLIN;
			}
			if (conn->want_write) {
				pfd.events |= POLLOUT;
			}
			poll_args.push_back(pfd);
		}

		int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), -1);
		// interrupted by a signal, the loop condition checks g_stop
		if (rv < 0 && errno == EINTR) {
			continue;
		}
		if (rv < 0) {
			die("poll()");
		}
		// accept new connection
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

	// graceful shutdown: k8s sends SIGTERM, then SIGKILL after the grace period
	size_t nconn = 0;
	for (Conn *conn : fd2conn) {
		if (conn) {
			(void)close(conn->fd);
			delete conn;
			++nconn;
		}
	}
	(void)close(fd);
	printf("shutting down, closed %zu connections\n", nconn);
	return 0;
}
