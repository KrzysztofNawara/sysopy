//
// Created by blueeyedhush on 6/12/15.
//

#ifndef MAKEFILE_CONFIG_H
#define MAKEFILE_CONFIG_H

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE

#define MODE_LOCAL 'l'
#define MODE_REMOTE 'r'
#define UNIX_SOCKET_PATH_MAX 108
#define USERNAME_MAX 16
#define MSG_LEN_MAX 128
#define MIN_PORT 1024
#define MAX_PORT 65535
#define MSG_QUEUES_CAPACITY 64

/* Interface */
#define USR_CMD_EXIT "e\n"
#define USR_CMD_TYPE "t\n"


#endif //MAKEFILE_CONFIG_H
