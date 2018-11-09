
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
 * Adds new socket for processing.
 *
 * @param socket New socket.
 *
 * @return {@code false} if to many sockets already registered.
 */
bool sockets_add_socket(int socket);

/**
 * Sets new input handler for added socket and enable input handling.
 *
 * @param socket Required socket.
 * @param callback Input handler function.
 * @param arg Argument for input handler.
 *
 * @return {@code true} if handler set.
 */
bool sockets_set_in_handler(int socket,
                            void (*callback)(int, void*),
                            void* arg);

/**
 * Sets new output handler for added socket and enable output handling.
 *
 * @param socket Required socket.
 * @param callback Output handler function.
 * @param arg Argument for output handler.
 *
 * @return {@code true} if handler set.
 */
bool sockets_set_out_handler(int socket,
                             void (*callback)(int, void*),
                             void* arg);

/**
 * Sets new hup handler for added socket and enable hup handling.
 *
 * @param socket Required socket.
 * @param callback Hup handler function.
 * @param arg Argument for hup handler.
 *
 * @return {@code true} if handler set.
 */
bool sockets_set_hup_handler(int socket,
                             void (*callback)(int, void*),
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
 * Removes socket from processing list.
 *
 * @param socket Required socket.
 *
 * @return {@code true} if socket removed.
 */
bool sockets_remove_socket(int socket);

#endif
