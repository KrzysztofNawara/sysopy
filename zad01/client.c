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
#include <netinet/in.h>
#include <arpa/inet.h>
       
bool loop = true;

int main(int argc, char* argv[]) {
   int sock_fd, recv_len, port;
   struct sockaddr_in addr;
   char buf[HOST_NAME_MAX + 1], *endptr;

   if (argc != 3) {
      fprintf(stderr, "Usage: %s <ip> <port>\n", argv[0]);
      fprintf(stderr, "For example: %s 127.0.0.1 2507\n", argv[0]);
      exit(1);
   }

   memset(&addr, 0, sizeof(addr));

   if (inet_aton(argv[1], &(addr.sin_addr)) == 0) {
      fprintf(stderr, "inet_aton(...) failed\n");
      exit(1);
   }

   errno = 0;
   port = strtol(argv[2], &endptr, 10);
        if (errno == 0 && endptr != NULL && (*endptr != '\0' || port < 0 || port > 65535)) {
                errno = EINVAL;
        }
        if (errno != 0) {
                perror("port = strtol(...) failed");
                exit(1);
        }

   if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
      perror("socket(...) failed");
      exit(1);
   }

   addr.sin_family = AF_INET;
   addr.sin_port = htons(port);

   if (connect(sock_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
      perror("connect(...) failed");
      exit(1);
   }

   if (gethostname(buf, sizeof(buf)+1) == -1) {
      perror("gethostname(...) failed");
      exit(1);
   }
   strcpy(buf, "witaj!\0");
   if (send(sock_fd, buf, strlen(buf), 0) == -1) {
      perror("sendto(...) failed");
      exit(1);
   }
   while(1){
      if ((recv_len = recv(sock_fd, buf, strlen(buf), 0)) == -1) {
         perror("recvfrom(...) failed");
         exit(1);
      }

      buf[recv_len] = '\0';

      printf("Received reply: %s\n", buf);      
   }

   if (close(sock_fd) == -1) {
      perror("close(sock_fd) failed");
      exit(1);
   }

   return 0;
}