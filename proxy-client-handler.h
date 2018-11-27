
#ifndef _PROXY_CLIENT_HANDLER_H
#define _PROXY_CLIENT_HANDLER_H

/**
 * Callback for input data from client.
 */
void client_handler(int socket, int events, void* arg);

/**
 * Client thread routine.
 */
void* client_thread(void* arg);

#endif
