
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "http-parser.h"
#include "proxy-client-handler.h"
#include "sockets-handler.h"

#include "proxy-handler.h"

header_entry_t* create_header_entry(const char* key, size_t key_len) {
  header_entry_t* entry = (header_entry_t*)malloc(sizeof(header_entry_t));
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
  http_parser* parser = (http_parser*)malloc(sizeof(http_parser));
  if (parser == NULL) {
    perror("Cannot allocate parser for client connection");
    close(socket);
    return;
  }
  http_parser_init(parser, HTTP_REQUEST);

  handler_state_t* state = (handler_state_t*)malloc(sizeof(handler_state_t));
  if (state == NULL) {
    perror("Cannot allocate state for connection");
    close(socket);
    return;
  }
  state->proxy_finished = false;
  state->client_socket = socket;
  state->target_socket = -1;
  state->buffered_headers = NULL;
  pstring_init(&state->target_outbuff);
  pstring_init(&state->client_outbuff);
  pstring_init(&state->url);

  parser->data = state;

  if (!sockets_add_socket(socket)) {
    fprintf(stderr, "Too many file descriptors.\n");
    close(socket);
    return;
  }
  sockets_set_hup_handler(socket, &socket_hup_handler, parser);
  sockets_set_in_handler(socket, &client_input_handler, parser);
}

bool send_pstring(int socket, pstring_t* buff) {
  size_t offset = 0;
  ssize_t result;

  while ((result = send(socket, buff->str + offset, buff->len - offset, 0)) !=
         buff->len - offset) {
    if (result == -1) {
      if (errno != EWOULDBLOCK)
        perror("Cannot send data to socket");
      pstring_substring(buff, offset);
      return false;
    }

    offset += result;
  }

  free(buff->str);
  pstring_init(buff);

  return true;
}

bool dump_buffered_headers(handler_state_t* state,
                           bool (*sender)(handler_state_t*,
                                          const char*,
                                          size_t)) {
  header_entry_t* entry = state->buffered_headers;
  char* output;
  size_t len;

  while (entry) {
    len = entry->key.len + entry->value.len + 4;  // 4 is ": " and "\r\n"
    output = (char*)malloc(len);

    memcpy(output, entry->key.str, entry->key.len);
    output[entry->key.len] = ':';
    output[entry->key.len + 1] = ' ';
    memcpy(output + entry->key.len + 2, entry->value.str, entry->value.len);
    output[len - 2] = '\r';
    output[len - 1] = '\n';

    if (!sender(state, output, len)) {
      free(output);
      return false;
    }

    free(output);
    entry = state->buffered_headers->next;
    free(state->buffered_headers);
    state->buffered_headers = entry;
  }

  state->buffered_headers = NULL;

  return true;
}

void handler_state_free(handler_state_t* state) {
  if (state->target_outbuff.str != NULL)
    free(state->target_outbuff.str);
  if (state->client_outbuff.str != NULL)
    free(state->client_outbuff.str);
  header_entry_t* entry = state->buffered_headers;
  while (entry) {
    if (entry->key.str != NULL)
      free(entry->key.str);
    if (entry->value.str != NULL)
      free(entry->value.str);
    state->buffered_headers = entry->next;
    free(entry);
    entry = state->buffered_headers;
  }
}

void socket_hup_handler(int socket, void* arg) {
  http_parser* parser = (http_parser*)arg;
  handler_state_t* state = (handler_state_t*)parser->data;

  if (state->client_socket == socket) {
    if (state->target_socket != -1) {
      sockets_remove_socket(state->target_socket);
      close(state->target_socket);
    }
  } else if (state->target_socket == socket) {
    sockets_remove_socket(state->client_socket);
    close(state->client_socket);
  }

  handler_state_free(state);
  free(parser);
}
