
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "http-parser.h"
#include "sockets-handler.h"
#include "proxy-client-handler.h"

#include "proxy-handler.h"

header_entry_t* create_header_entry(const char* key, size_t key_len) {
  header_entry_t* entry = (header_entry_t*) malloc(sizeof(header_entry_t));
  if (entry == NULL) {
    perror("Cannot create header entry");
    return NULL;
  }

  entry->next = NULL;
  pstring_init(&entry->key);
  pstring_init(&entry->value);
  if (!pstring_append(&entry->key, key, key_len)) {
    perror("Cannot store header key");
    free(entry);
    return NULL;
  }

  return entry;
}

void proxy_accept_client(int socket) {
  http_parser* parser = (http_parser*) malloc(sizeof(http_parser));
  http_parser_init(parser, HTTP_REQUEST);

  handler_state_t* state = malloc(sizeof(handler_state_t));
  state->client_socket = socket;
  state->target_socket = -1;
  pstring_init(&state->target_outbuff);
  pstring_init(&state->client_outbuff);
  pstring_init(&state->url);

  parser->data = state;

  if (!sockets_add_socket(socket)) {
    fprintf(stderr, "Too many file descriptors.\n");
    close(socket);
    return;
  }
  sockets_set_in_handler(socket, &client_input_handler, parser);
}

bool send_pstring(int socket, pstring_t* buff) {
  size_t offset = 0;
  ssize_t result;

  while ((result = send(socket, buff->str + offset, buff->len - offset, 0)) != buff->len - offset) {
    if (result == -1) {
      if (errno != EWOULDBLOCK)
        perror("Cannot send data to target");
      pstring_substring(buff, offset);
      return false;
    }

    offset += result;
  }

  return true;
}
