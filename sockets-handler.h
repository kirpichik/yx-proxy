
#include <stdbool.h>

#ifndef _SOCKETS_HANDLER_H
#define _SOCKETS_HANDLER_H

int sockets_poll_loop(int server_socket);

bool sockets_add_socket(int socket, void (*callback)(int, void*), void* arg);

bool sockets_remove_socket(int socket);

#endif
