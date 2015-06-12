#include "config.h"

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
#include <sys/un.h>

#include "message.h"
#include "sockaddr_cmp.h"

#define IP_ADDR htonl(INADDR_ANY)
#define UDP_PORT_BASE 2507
#define UDP_PORT_COUNT 2

#define UNIX_ADDR "./unix_socket"
#define INIT_CLIENTS 2

bool loop = true;

void sigint_handler(int signo) {
    char msg[] = "\nSIGINT received...\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    loop = false;
}

int clientCapacity = 0;
struct sockaddr **clientTab = NULL;
socklen_t *clientSizes = NULL;
int *clientDesc = NULL;
int clientIterator = 0;

void addClient(struct sockaddr *cli_addr, socklen_t size, int desc) {
    if(clientIterator >= clientCapacity) {
        clientCapacity = (clientCapacity > 0) ? 2*clientCapacity : INIT_CLIENTS;
        clientTab = realloc(clientTab, sizeof(struct sockaddr*)*clientCapacity);
        clientSizes = realloc(clientSizes, sizeof(socklen_t)*clientCapacity);
        clientDesc = realloc(clientDesc, sizeof(int)*clientCapacity);
    }

    clientTab[clientIterator] = cli_addr;
    clientSizes[clientIterator] = size;
    clientDesc[clientIterator] = desc;
    clientIterator++;
}

short clientPresent(struct sockaddr *cli_addr) {
    for(int i = 0; i < clientIterator; i++) {
        if(sockaddr_cmp(cli_addr, clientTab[i]) == 0) {
            return 1;
        }
    }

    return 0;
}

int main() {
    int recv_len, i, events, optval;

    socklen_t how_much_for_address = 0;
    {
        socklen_t inet_sizeof = sizeof(struct sockaddr_in);
        socklen_t un_sizeof = sizeof(struct sockaddr_un);
        if (inet_sizeof > un_sizeof) {
            how_much_for_address = inet_sizeof;
        } else {
            how_much_for_address = un_sizeof;
        }
    }

    struct sockaddr *cli_addr = NULL;
    struct sockaddr_in addr;
    struct sockaddr_un uaddr;
    socklen_t addr_len;
    struct sigaction act;
    message buf;
    struct pollfd ufds[UDP_PORT_COUNT];

    memset(&act, 0, sizeof(act));
    act.sa_handler = sigint_handler;
    sigaction(SIGINT, &act, NULL);

    memset(&ufds, 0, sizeof(ufds));

    for (i = 0; i < 1; i++) {
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
    {
        if ((ufds[1].fd = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
            perror("socket(...) failed");
            exit(1);
        }
        memset(&uaddr, 0, sizeof(uaddr));
        optval = 1;
        uaddr.sun_family = AF_UNIX;
        strcpy(uaddr.sun_path, UNIX_ADDR);
        unlink(uaddr.sun_path);
        if (setsockopt(ufds[1].fd, SOL_SOCKET, SO_PASSCRED, &optval, sizeof(optval)) == -1) {
            perror("setsockopt(..., SO_PASSCRED, ...) failed");
            exit(1);
        }
        if (bind(ufds[1].fd, (struct sockaddr *) &uaddr, sizeof(uaddr)) == -1) {
            perror("bind2(...) failed");
            exit(1);
        }
        ufds[i].events = POLLIN;
    }


    printf("Waiting for connections at %s:[from %d to %d]...\n", inet_ntoa(addr.sin_addr), UDP_PORT_BASE,
           UDP_PORT_BASE + UDP_PORT_COUNT - 1);

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
                    cli_addr = calloc(how_much_for_address, 1);
                    socklen_t actual_length = how_much_for_address;
                    if ((recv_len = recvfrom(ufds[i].fd, &buf, sizeof(buf), 0, cli_addr, &actual_length)) == -1) {
                        if (errno == EINTR) {
                            continue;
                        }
                        perror("recvfrom(...) failed");
                        exit(1);
                    }
                    //      IF MESSAGE IS CLIENT REGISTERING, THEN
                    if(clientPresent(cli_addr) == 0) {
                        addClient(cli_addr, actual_length, ufds[i].fd);
                    }
                    //printf("Got connection from %s:%d to port %d, received: %s from: %s\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port), UDP_PORT_BASE + i, buf.msg ,buf.from);
                    printf("Received: %s from: %s\n", buf.msg, buf.from);
                    //      ELSE SEND TO ALL1
                    int j;
                    for (j = 0; j < clientIterator; j++) {
                        // printf("Sending to %s port %i\n", inet_ntoa(clientTab[j].sin_addr), ntohs(clientTab[j].sin_port));
                        cli_addr = clientTab[j];
                        actual_length = clientSizes[j];
                        if (sendto(clientDesc[j], &buf, recv_len, 0, cli_addr, actual_length) == -1) {
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