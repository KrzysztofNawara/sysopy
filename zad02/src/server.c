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

//#define IP_ADDR htonl(INADDR_ANY)
//#define PORT 2507

#define SS_BACKLOG 16
#define UNIX_ADDR "./unix_socket"
#define INIT_DESC 4 /* must be > 2 */

typedef struct {
    struct sockaddr_un unix_socket_addr;
    struct sockaddr_in inet_socket_addr;
    char *hr_up;
    char *hr_ip;
    char *hr_p;
} application_arguments;

application_arguments prog_args;

/*
 * Order of arguments:
 * - unix port name
 * - ip
 * - port
 */
void process_application_arguments(int argc, char **argv, application_arguments *args) {
    if(argc < 3) {
        printf("Too few argument, 3 required: <unix_socket_path> <ip> <port>\n");
        exit(1);
    }

    /* store them for later (in case I neeed them in debug messages) */
    args->hr_up = argv[1];
    args->hr_ip = argv[2];
    args->hr_p = argv[3];

    /* UNIX domain socket */
    char *unix_socket_path = argv[1];
    int path_len = strlen(unix_socket_path);

    if(path_len > UNIX_SOCKET_PATH_MAX) {
        printf("Socket path too long\n");
        exit(1);
    }

    args->unix_socket_addr.sun_family = AF_UNIX;
    strcpy(args->unix_socket_addr.sun_path, unix_socket_path);

    /* internet domain socket */
    args->inet_socket_addr.sin_family = AF_INET;
    int ret = inet_pton(AF_INET, argv[2], &(args->inet_socket_addr.sin_addr));
    if(ret != 1) {
        printf("Wrong IP format\n");
        exit(1);
    }

    long unvalidated_port = strtol(argv[3], NULL, 10);
    if(unvalidated_port < MIN_PORT || unvalidated_port > MAX_PORT) {
        printf("Wrong port\n");
        exit(1);
    }
    args->inet_socket_addr.sin_port = htons((in_port_t)unvalidated_port);
}

/* -------------------------------------- */

volatile bool loop = true;

void sigint_handler(int signo) {
    char msg[] = "\nSIGINT received...\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    loop = false;
}

/* -------------------------------------- */

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

/* -------------------------------------- */


int main(int argc, char **argv) {
    process_application_arguments(argc, argv, &prog_args);

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


    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = sigint_handler;
    sigaction(SIGINT, &act, NULL);

    ufds = calloc(sizeof(struct pollfd), 2);

    /* create UNIX and INET listen sockets */
    int inet_listen = socket(AF_INET, SOCK_STREAM, 0);

    if (bind(inet_listen, (struct sockaddr *) &(prog_args.inet_socket_addr), sizeof(prog_args.inet_socket_addr)) == -1) {
        perror("bind(...) failed");
        exit(1);
    }

    int unix_listen = socket(AF_UNIX, SOCK_STREAM, 0);

    unlink(prog_args.unix_socket_addr.sun_path);

    optval = 1;
    if (setsockopt(unix_listen, SOL_SOCKET, SO_PASSCRED, &optval, sizeof(optval)) == -1) {
        perror("setsockopt(..., SO_PASSCRED, ...) failed");
        exit(1);
    }

    if (bind(unix_listen, (struct sockaddr *) &(prog_args.unix_socket_addr), sizeof(prog_args.inet_socket_addr)) == -1) {
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

    printf("Waiting for connections at %s:[%s] and %s\n", prog_args.hr_ip, prog_args.hr_p, prog_args.hr_up);

    struct sockaddr *cli_addr = NULL;
    message buf;
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

                    if(recv_len == 0) {
                        /* socket was ready and yet no data read - it has be closed remotely */
                        printf("Client disconnected\n");
                        ufds[i].fd *= -1;
                    } else {
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
                    }

                    events--;
                }
            }
        }
    }

    printf("Shutting down...\n");

    for (int i = clientIterator - 1; i >= 0; i--) {
        if (close(ufds[i].fd) == -1) {
            perror("close(...) failed");
            exit(1);
        }
    }

    return 0;
}