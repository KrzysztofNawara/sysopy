//
// Created by blueeyedhush on 6/12/15.
//

#ifndef MAKEFILE_MESSAGE_H
#define MAKEFILE_MESSAGE_H

#include "config.h"

typedef struct {
    char from[USERNAME_MAX+1];
    char msg[MSG_LEN_MAX+1];
} message;

#endif //MAKEFILE_MESSAGE_H
