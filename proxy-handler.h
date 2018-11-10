
#include <stdbool.h>

#include "http-parser.h"
#include "pstring.h"

#ifndef _PROXY_HANDLER_H
#define _PROXY_HANDLER_H

typedef struct header_entry {
  pstring_t key;
  pstring_t value;
  struct header_entry* next;
} header_entry_t;

struct target_state;

typedef struct client_state {
  int socket;
  http_parser parser;
  pstring_t outbuff;
  header_entry_t* headers;
  struct target_state* target;
  pstring_t url;
} client_state_t;

typedef struct target_state {
  int socket;
  http_parser parser;
  pstring_t outbuff;
  header_entry_t* headers;
  struct client_state* client;
  bool proxy_finished;
} target_state_t;

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
 * Free all memory allocated to headers list.
 *
 * @param entry List head.
 */
void free_header_entry_list(header_entry_t* entry);

/**
 * Accepts new client at required socket.
 *
 * @param socket Socket.
 */
void proxy_accept_client(int socket);

/**
 * Establish connection to proxying target at required hostname and port.
 * Also creates target input handler and parser.
 *
 * @param state Current state.
 * @param host hostname[:port]
 *
 * @return {@code true} if successfully established.
 */
bool proxy_establish_connection(client_state_t* state, char* host);

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
 * Allocates and build header string from header entry.
 *
 * @param entry Header entry.
 * @param result_len Result string length.
 *
 * @return New header string allocated on heap or {@code NULL} if not enougth memory.
 */
char* build_header_string(header_entry_t* entry, size_t* result_len);

#endif
