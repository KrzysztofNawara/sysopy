#define _BSD_SOURCE
#define _XOPEN_SOURCE 500
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
       
bool loop = true;

int main(int argc, char* argv[]) {
	int sock_fd, recv_len;
	struct sockaddr_un addr;
	char buf[HOST_NAME_MAX + 1];

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <path>\n", argv[0]);
		fprintf(stderr, "For example: %s ./server_socket\n", argv[0]);
		exit(1);
	}
	
	if (strlen(argv[1]) >= sizeof(addr.sun_path)) {
		errno = ENAMETOOLONG;
		perror("addr.sun_path error");
		exit(1);
	}

	memset(&addr, 0, sizeof(addr));

	if ((sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
		perror("socket(...) failed");
		exit(1);
	}

	addr.sun_family = AF_UNIX;
	strcpy(addr.sun_path, argv[1]);

	
	// if (connect(sock_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
	// 	perror("connect(...) failed");
	// 	exit(1);
	// }

	// if (gethostname(buf, sizeof(buf)) == -1) {
	// 	perror("gethostname(...) failed");
	// 	exit(1);
	// }

	if (send(sock_fd, buf, strlen(buf), 0) == -1) {
		perror("send(...) failed");
		exit(1);
	}

	if ((recv_len = recv(sock_fd, buf, strlen(buf), 0)) == -1) {
		perror("recv(...) failed");
		exit(1);
	}

	buf[recv_len] = '\0';

	printf("Received reply: %s\n", buf);

	if (close(sock_fd) == -1) {
		perror("close(sock_fd) failed");
		exit(1);
	}

	return 0;
}