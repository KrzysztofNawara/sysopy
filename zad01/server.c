#define _XOPEN_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <limits.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#define IP_ADDR htonl(INADDR_ANY)
#define UDP_PORT_BASE 2507
#define UDP_PORT_COUNT 1

bool loop = true;

void sigint_handler(int signo) {
  char msg[] = "\nSIGINT received...\n";
  write(STDOUT_FILENO, msg, strlen(msg));
  loop = false;
}

struct sockaddr_in clientTab[10];
int clientIterator = 0;

void addClient(struct sockaddr_in cli_addr){
  clientTab[clientIterator] = cli_addr;
  clientIterator++;
}

int main() {
  int recv_len, i, j, events;
  struct sockaddr_in addr, cli_addr;
  socklen_t addr_len;
  struct sigaction act;
  char buf[HOST_NAME_MAX + 1];
  struct pollfd ufds[UDP_PORT_COUNT];

  memset(&act, 0, sizeof(act));
  act.sa_handler = sigint_handler;
  sigaction(SIGINT, &act, NULL);

  memset(&ufds, 0, sizeof(ufds));

  for (i = 0; i < UDP_PORT_COUNT; i++) {
    if ((ufds[i].fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
      perror("socket(...) failed");
      exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_PORT_BASE + i);
    addr.sin_addr.s_addr = IP_ADDR;

    if (bind(ufds[i].fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
      perror("bind(...) failed");
      exit(1);
    }
    
    ufds[i].events = POLLIN;
  }

  printf("Waiting for connections at %s:[from %d to %d]...\n", inet_ntoa(addr.sin_addr), UDP_PORT_BASE, UDP_PORT_BASE + UDP_PORT_COUNT - 1);

  while (loop) {
    if ((events = poll(ufds, UDP_PORT_COUNT, 2500)) == 0) {
      printf("Timeout, but no events!\n");
      continue;
    }
    else if (events == -1) {
      if (errno == EINTR) {
        continue;
      }
      perror("poll(...) failed");
      exit(1);
    }
    else {
      for (i = 0; events > 0 && i < UDP_PORT_COUNT; i++) {
        if (ufds[i].revents & POLLIN) {
          addr_len = sizeof(cli_addr);
          if ((recv_len = recvfrom(ufds[i].fd, buf, sizeof(buf), 0, (struct sockaddr *) &cli_addr, &addr_len)) == -1) {
            if (errno == EINTR) {
              continue;
            }
            perror("recvfrom(...) failed");
            exit(1);
          }
          //      IF MESSAGE IS CLIENT REGISTERING, THEN 
          addClient(cli_addr);
          printf("Got connection from %s:%d to port %d, received: %s\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port), UDP_PORT_BASE + i, buf);
          //      ELSE SEND TO ALL1
          int j;
          for(j = 0; j < clientIterator; j++){
            printf("Sending to %s port %i\n", inet_ntoa(clientTab[j].sin_addr), ntohs(clientTab[j].sin_port));
            cli_addr = clientTab[j];
            if (sendto(ufds[i].fd, buf, recv_len, 0, (struct sockaddr *) &cli_addr, addr_len) == -1) {
              perror("sendto(...) failed");
              exit(1);
            }
          }
          events--;
        }
      }
    }
  }

  printf("Shutting down...\n");

  for (i = 0; i < UDP_PORT_COUNT; i++) {
    if (close(ufds[i].fd) == -1) {
      perror("close(...) failed");
      exit(1);
    }
  }

  return 0;
}