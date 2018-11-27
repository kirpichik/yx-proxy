
#include <pthread.h>
#include <stdbool.h>

#include "cache.h"
#include "http-parser.h"
#include "pstring.h"

#ifndef _PROXY_HANDLER_H
#define _PROXY_HANDLER_H

#define PROXY_HTTP_RESPONSE_VALID_LINE_LEN sizeof("HTTP/1.0 200") - 1

struct target_state;

typedef struct client_state {
  int socket;
  volatile int revents;
  http_parser parser;
  bool parse_error;
  pthread_mutex_t lock;
  pthread_cond_t notifier;
  pthread_t thread;
  pstring_t url;
  bool url_dumped;
  pstring_t client_outbuff;
  pstring_t target_outbuff;
  pstring_t header_key;
  pstring_t header_value;
  struct target_state* target;
  cache_entry_reader_t* reader;
  cache_entry_t* cache;
  size_t cache_offset;
  bool use_cache;
} client_state_t;

typedef struct target_state {
  int socket;
  volatile int revents;
  http_parser parser;
  pthread_mutex_t lock;
  pthread_cond_t notifier;
  pthread_t thread;
  pstring_t outbuff;
  cache_entry_t* cache;
  bool message_complete;
} target_state_t;

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
 * @return Zero if all string was sent, or 1 if some output data stored
 * as substring of buffer, or -1 if error occured.
 */
int send_pstring(int socket, pstring_t* buff);

/**
 * Allocates and build header string from header entry.
 *
 * @param key Header key.
 * @param value Header value.
 * @param result_len Result string length.
 *
 * @return New header string allocated on heap or {@code NULL} if not enougth
 * memory.
 */
char* build_header_string(pstring_t* key, pstring_t* value, size_t* result_len);

#endif
