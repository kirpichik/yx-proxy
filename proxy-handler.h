
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

header_entry_t* create_header_entry(const char* key, size_t key_len);

void proxy_accept_client(int socket);

bool send_pstring(int socket, pstring_t* buff);

bool dump_buffered_headers(handler_state_t* state,
                           bool (*sender)(handler_state_t*,
                                          const char*,
                                          size_t));

void handler_state_free(handler_state_t* state);

void socket_hup_handler(int socket, void* arg);

#endif
