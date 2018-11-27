
#include <stdbool.h>

#ifndef _SOCKETS_HANDLER_H
#define _SOCKETS_HANDLER_H

/**
 * Main loop of clients handling.
 *
 * @param server_socket Socket for reciveing new clients.
 */
int sockets_poll_loop(int server_socket);

/**
 * Destroy sockets loop.
 */
void sockets_destroy(void);

/**
 * Adds new socket for processing.
 *
 * @param socket New socket.
 * @param callback Socket handler function.
 * @param arg Argument for socket handler.
 *
 * @return {@code false} if to many sockets already registered.
 */
bool sockets_add_socket(int socket,
                        void (*callback)(int, int, void*),
                        void* arg);

/**
 * Enable input handling for added socket.
 *
 * @param socket Required socket.
 *
 * @return {@code true} if handle enabled.
 */
bool sockets_enable_in_handle(int socket);

/**
 * Enable output handling for added socket.
 *
 * @param socket Required socket.
 *
 * @return {@code true} if handle enabled.
 */
bool sockets_enable_out_handle(int socket);

/**
 * Enable input/output handling for added socket.
 *
 * @param socket Required socket.
 *
 * @return {@code true} if handle enabled.
 */
bool sockets_enable_io_handle(int socket);

/**
 * Disable input handling for added socket.
 *
 * @param socket Required socket.
 *
 * @return {@code true} if handle disabled.
 */
bool sockets_cancel_in_handle(int socket);

/**
 * Disable output handling for added socket.
 *
 * @param socket Required socket.
 *
 * @return {@code true} if handle disabled.
 */
bool sockets_cancel_out_handle(int socket);

/**
 * Disable input/output handling for added socket.
 *
 * @param socket Required socket.
 *
 * @return {@code true} if handle disabled.
 */
bool sockets_cancel_io_handle(int socket);

/**
 * Removes socket from processing list.
 *
 * @param socket Required socket.
 *
 * @return {@code true} if socket removed.
 */
bool sockets_remove_socket(int socket);

#endif
