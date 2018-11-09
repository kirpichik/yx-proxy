
#include <stdbool.h>

#include "pstring.h"

#ifndef _PROXY_HANDLER_H
#define _PROXY_HANDLER_H

typedef struct header_entry {
  pstring_t key;
  pstring_t value;
  struct header_entry* next;
} header_entry_t;

typedef struct handler_state {
  int client_socket;
  int target_socket;
  pstring_t target_outbuff;
  pstring_t client_outbuff;
  pstring_t url;
  header_entry_t* buffered_headers;
  bool proxy_finished;
} handler_state_t;

/**
 * Allocates new header entry.
 *
 * @param key Source buffer for key.
 * @param key_len Count of bytes required to copy.
 *
 * @return New header entry of {@code NULL} if not enougth memory.
 */
header_entry_t* create_header_entry(const char* key, size_t key_len);

/**
 * Accepts new client at required socket.
 *
 * @param socket Socket.
 */
void proxy_accept_client(int socket);

/**
 * Sends string to the socket.
 *
 * @param socket Target socket.
 * @param buff Sending string.
 *
 * @return {@code true} if all string was sent, or store left string as substring of buffer.
 */
bool send_pstring(int socket, pstring_t* buff);

/**
 * Dumps all buffered headers using sender function.
 *
 * @param state Current handler state.
 * @param sender Sender function.
 *
 * @return {@code true} if all headers successfully dumped using sender function.
 */
bool dump_buffered_headers(handler_state_t* state,
                           bool (*sender)(handler_state_t*,
                                          const char*,
                                          size_t));

/**
 * Free memory allocated for handler state.
 *
 * @param state State pointer.
 */
void handler_state_free(handler_state_t* state);

/**
 * Callback handler for sockets hup.
 */
void socket_hup_handler(int socket, void* arg);

/**
 * Cleanup memory for closed socket and its handlers.
 */
void interrupt_socket_handling(void* arg_input, void* arg_output, void* arg_hup);

#endif
