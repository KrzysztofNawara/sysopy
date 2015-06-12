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
#include <sys/time.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/un.h>

#include "message.h"
#include "sockaddr_cmp.h"

//#define IP_ADDR htonl(INADDR_ANY)
//#define PORT 2507

/*#define SS_BACKLOG 16*/
/*#define UNIX_ADDR "./unix_socket"*/
#define INIT_CLIENTS 2

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

/*
 * Make it a struct!
 */
int clientCapacity = 0;
struct sockaddr **clientTab = NULL;
socklen_t *clientSizes = NULL;
int *clientDesc = NULL;
long *clientLastHeardOf = NULL;
int clientIterator = 0;

long curr_time() {
    struct timeval tm;
    gettimeofday(&tm, NULL);

    return tm.tv_sec;
}

void addClient(struct sockaddr *cli_addr, socklen_t size, int desc) {
    if(clientIterator >= clientCapacity) {
        clientCapacity = (clientCapacity > 0) ? 2*clientCapacity : INIT_CLIENTS;
        clientTab = realloc(clientTab, sizeof(struct sockaddr*)*clientCapacity);
        clientSizes = realloc(clientSizes, sizeof(socklen_t)*clientCapacity);
        clientDesc = realloc(clientDesc, sizeof(int)*clientCapacity);
        clientLastHeardOf = realloc(clientLastHeardOf, sizeof(long)*clientCapacity);
    }

    clientTab[clientIterator] = cli_addr;
    clientSizes[clientIterator] = size;
    clientDesc[clientIterator] = desc;
    clientLastHeardOf[clientIterator] = curr_time();

    clientIterator++;
}

int clientPresent(struct sockaddr *cli_addr) {
    for(int i = 0; i < clientIterator; i++) {
        if(clientTab[i] != NULL && sockaddr_cmp(cli_addr, clientTab[i]) == 0) {
            return i;
        }
    }

    return -1;
}

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

    message buf;
    struct pollfd ufds[2];
    memset(&ufds, 0, sizeof(ufds));

    /* create UNIX and INET sockets */
    int inet_socket;
    int unix_socket;

    if ((inet_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror("socket(...) failed");
        exit(1);
    }

    if (bind(inet_socket, (struct sockaddr *) &(prog_args.inet_socket_addr), sizeof(prog_args.inet_socket_addr)) == -1) {
        perror("bind(...) failed");
        exit(1);
    }

    if ((unix_socket = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
        perror("socket(...) failed");
        exit(1);
    }

    unlink(prog_args.unix_socket_addr.sun_path);

    optval = 1;
    if (setsockopt(unix_socket, SOL_SOCKET, SO_PASSCRED, &optval, sizeof(optval)) == -1) {
        perror("setsockopt(..., SO_PASSCRED, ...) failed");
        exit(1);
    }

    if (bind(unix_socket, (struct sockaddr *) &(prog_args.unix_socket_addr), sizeof(prog_args.inet_socket_addr)) == -1) {
        perror("bind2(...) failed");
        exit(1);
    }

    /* add sockets to polling queue */
    ufds[0].fd = inet_socket;
    ufds[0].events = POLLIN;
    ufds[0].revents = 0;

    ufds[1].fd = unix_socket;
    ufds[1].events = POLLIN;
    ufds[1].revents = 0;

    printf("Waiting for connections at %s:[%s] and %s\n", prog_args.hr_ip, prog_args.hr_p, prog_args.hr_up);

    struct sockaddr *cli_addr = NULL;
    while (loop) {
        if ((events = poll(ufds, 2, 2500)) == 0) {
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
            for (i = 0; events > 0 && i < 2; i++) {
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
                    int cid = clientPresent(cli_addr);
                    if(cid == -1) {
                        addClient(cli_addr, actual_length, ufds[i].fd);
                    } else {
                        /* update timestamp */
                        clientLastHeardOf[cid] = curr_time();
                    }

                    /* now we have to check whether it was just a keepalive or a legitimate message, which should be forwarded */
                    if(recv_len == sizeof(message)) {
                        /* this is legit message! */
                        printf("Received: %s from: %s\n", buf.msg, buf.from);
                        int j;

                        long reference_time = curr_time();
                        for (j = 0; j < clientIterator; j++) {
                            cli_addr = clientTab[j];
                            actual_length = clientSizes[j];

                            if(reference_time - clientLastHeardOf[j] > TIMEOUT_SEC) {
                                /* kick this guy out */
                                free(clientTab[j]);
                                clientTab[j] = NULL;
                                printf("Client timed out\n");
                            } else {
                                if (sendto(clientDesc[j], &buf, recv_len, 0, cli_addr, actual_length) == -1) {
                                    perror("sendto(...) failed");
                                    exit(1);
                                }
                            }
                        }
                    } else {
                        printf("Heartbeat!\n");
                    }

                    events--;
                }
            }
        }
    }

    printf("Shutting down...\n");

    /* two sockets to close - 0 and 1 */
    for (int i = 1; i >= 0; i--) {
        if (close(ufds[i].fd) == -1) {
            perror("close(...) failed");
            exit(1);
        }
    }

    return 0;
}