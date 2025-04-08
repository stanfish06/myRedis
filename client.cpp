#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

int main() {
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = ntohs(1234);
	addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);  // 127.0.0.1
	int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));

	char msg[] = "hello";
	write(fd, msg, strlen(msg));

	char rbuf[64] = {};
	ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
	printf("server says: %s\n", rbuf);
	close(fd);
}
