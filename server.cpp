#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>


static void do_something(int connfd) {
	char rbuf[64] = {};
	ssize_t n = read(connfd, rbuf, sizeof(rbuf) - 1);
	if (n < 0) {
		return;
	}
	printf("client says %s\n", rbuf);

	char wbuf[] = "world";
	write(connfd, wbuf, strlen(wbuf));
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
		do_something(connfd);
		close(connfd);
	}
}

