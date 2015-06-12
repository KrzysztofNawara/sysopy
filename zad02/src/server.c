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
#include <fcntl.h>

#include "message.h"
#include "sockaddr_cmp.h"

#define IP_ADDR htonl(INADDR_ANY)
#define PORT 2507

#define SS_BACKLOG 16
#define UNIX_ADDR "./unix_socket"
#define INIT_DESC 4 /* must be > 2 */

bool loop = true;

void sigint_handler(int signo) {
    char msg[] = "\nSIGINT received...\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    loop = false;
}

int clientCapacity = 2;
struct pollfd *ufds = NULL;
int clientIterator = 2;

void addClient(int desc) {
    if(clientIterator >= clientCapacity) {
        clientCapacity = (clientCapacity > 0) ? 2*clientCapacity : INIT_DESC;
        ufds = realloc(ufds, sizeof(struct pollfd)*clientCapacity);
    }

    ufds[clientIterator].fd = desc;
    ufds[clientIterator].events = POLLIN;
    ufds[clientIterator].revents = 0;
    clientIterator++;
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

    memset(&act, 0, sizeof(act));
    act.sa_handler = sigint_handler;
    sigaction(SIGINT, &act, NULL);

    ufds = calloc(sizeof(struct pollfd), 2);

    /* create UNIX and INET listen sockets */
    int inet_listen = socket(AF_INET, SOCK_STREAM, 0);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = IP_ADDR;

    if (bind(inet_listen, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        perror("bind(...) failed");
        exit(1);
    }

    int unix_listen = socket(AF_UNIX, SOCK_STREAM, 0);

    memset(&uaddr, 0, sizeof(uaddr));
    uaddr.sun_family = AF_UNIX;
    strcpy(uaddr.sun_path, UNIX_ADDR);

    optval = 1;
    unlink(uaddr.sun_path);
    if (setsockopt(unix_listen, SOL_SOCKET, SO_PASSCRED, &optval, sizeof(optval)) == -1) {
        perror("setsockopt(..., SO_PASSCRED, ...) failed");
        exit(1);
    }

    if (bind(unix_listen, (struct sockaddr *) &uaddr, sizeof(uaddr)) == -1) {
        perror("bind2(...) failed");
        exit(1);
    }

    /* set sockets state to non-blocking - this will prevent accept() from blocking */
    fcntl(inet_listen, F_SETFL, O_NONBLOCK);
    fcntl(unix_listen, F_SETFL, O_NONBLOCK);

    /* mark sockets using listen */
    listen(inet_listen, SS_BACKLOG);
    listen(unix_listen, SS_BACKLOG);

    /* add sockets to polling queue */
    ufds[0].fd = inet_listen;
    ufds[0].events = POLLIN;
    ufds[0].revents = 0;

    ufds[1].fd = unix_listen;
    ufds[1].events = POLLIN;
    ufds[1].revents = 0;

    printf("Waiting for connections at %s:[%d]...\n", inet_ntoa(addr.sin_addr), PORT);

    while (loop) {
        if ((events = poll(ufds, clientIterator, 2500)) == 0) {
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
            /* first, check listening ports if somebody does not want to connect */
            int res = -1;
            i = 0;
            for(; i < 2 && events > 0; i++) {
                if(ufds[i].revents & POLLIN) {
                    res = accept(ufds[i].fd, NULL, NULL);
                    if (res > 0) {
                        /* new client connected! */
                        addClient(res);
                    } else if (res == -1 && errno != EWOULDBLOCK && errno != EAGAIN) {
                        perror("accept(...) failed");
                        exit(1);
                    }

                    events--;
                }
            }

            /* now check the rest for ordinary transmission requests */

            for (; i < clientIterator && events > 0; i++) {
                if(ufds[i].revents & POLLHUP) {
                    printf("Client disconnected\n");
                    ufds[i].fd *= -1;
                } else if (ufds[i].revents & POLLIN) {
                    if ((recv_len = recv(ufds[i].fd, &buf, sizeof(buf), 0)) == -1) {
                        if (errno == EINTR) {
                            continue;
                        }
                        perror("recvfrom(...) failed");
                        exit(1);
                    }

                    printf("Received: %s from: %s\n", buf.msg, buf.from);
                    //      ELSE SEND TO ALL1

                    for (int j = 2; j < clientIterator; j++) {
                        if(ufds[j].fd >= 0) {
                            if (send(ufds[j].fd, &buf, recv_len, 0) == -1) {
                                perror("sendto(...) failed");
                                exit(1);
                            }
                        }
                    }
                    events--;
                }
            }
        }
    }

    printf("Shutting down...\n");

    for (i = 0; i < clientIterator; i++) {
        if (close(ufds[i].fd) == -1) {
            perror("close(...) failed");
            exit(1);
        }
    }

    return 0;
}