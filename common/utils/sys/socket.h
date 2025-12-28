#pragma once

#include <unistd.h>
#include <utils/defs.h>


void set_socket_blocking(socket_t fd, bool blocking = true);
void set_socket_timeout(socket_t fd, float timeout_sec);
