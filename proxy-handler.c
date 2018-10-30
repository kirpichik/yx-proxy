
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>

#include "http-parser.h"
#include "sockets-handler.h"

#include "proxy-handler.h"

#define BUFFER_SIZE 1024

typedef struct header_entry {
  char* key;
  char* value;
  struct header_entry* next;
} header_entry_t;

typedef struct handler_state {
  int client_socket;
  int target_socket;
  char* url;
  header_entry_t* buffered_headers;
} handler_state_t;

static bool establish_target_connection(handler_state_t*, char*);
static header_entry_t* create_header_entry(char*);
static bool dump_headers_buffer(header_entry_t*, int);

static int handle_request_url(http_parser* parser, const char* at, size_t len) {
  char* url = (char*) malloc(len + 1);
  memcpy(url, at, len);
  url[len] = '\0';
  ((handler_state_t*) parser->data)->url = url;

  return 0;
}

static int handle_request_header_field(http_parser* parser, const char* at, size_t len) {
  handler_state_t* state = (handler_state_t*) parser->data;
  char* field = (char*) malloc(len + 1);
  memcpy(field, at, len);
  field[len] = '\0';

  header_entry_t* entry = create_header_entry(field);
  if (state->buffered_headers)
    entry->next = state->buffered_headers;
  state->buffered_headers = entry;

  return 0;
}

static int handle_request_header_value(http_parser* parser, const char* at, size_t len) {
  handler_state_t* state = (handler_state_t*) parser->data;
  char* value;
  char* key = state->buffered_headers->key;

  if (strncmp(key, "Connection", sizeof("Connection") - 1)) {
    // Catch Connection: keep-alive header
    value = strdup("close");
  } else {
    value = (char*) malloc(len + 1);
    memcpy(value, at, len);
    value[len] = '\0';

    if (strncmp(key, "Host", sizeof("Host") - 1)) {
      // Catch Host: <target> header
      return establish_target_connection(state, value) ? 0 : 1;
    }
  }

  state->buffered_headers->value = value;

  return 0;
}

static int handle_request_body(http_parser* parser, const char* at, size_t len) {
  handler_state_t* state = (handler_state_t*) parser->data;

  // TODO - change when multithreading
  // Unset nonblock
  int oldfl = fcntl(state->target_socket, F_GETFL);
  if (oldfl == -1) {
    perror("Cannot get socket flags");
    return 1;
  }
  fcntl(state->target_socket, F_SETFL, oldfl & ~O_NONBLOCK);

  if (send(state->target_socket, at, len, 0) == -1) {
    perror("Cannot proxy client data body to target socket");
    return 1;
  }

  fcntl(state->target_socket, F_SETFL, oldfl);
  return 0;
}

static int handle_request_message_complete(http_parser* parser) {
  handler_state_t* state = (handler_state_t*) parser->data;

  sockets_remove_socket(state->client_socket);
  close(state->client_socket);

  if (state->target_socket != -1) {
    sockets_remove_socket(state->target_socket);
    close(state->target_socket);
  }

  free(state);
  free(parser);

  return 0;
}

static http_parser_settings http_request_callbacks = {
  NULL, /* on_message_begin */
  handle_request_url,
  NULL, /* on_status */
  handle_request_header_field,
  handle_request_header_value,
  NULL, /* on_headers_complete */
  handle_request_body,
  handle_request_message_complete,
  NULL,
  NULL
};

static void input_handler(int socket, void* arg) {
  http_parser* parser = (http_parser*) arg;
  char buff[BUFFER_SIZE];
  int result, nparsed;

  while (1) {
    result = recv(socket, buff, BUFFER_SIZE, 0);

    if (result == -1) {
      if (errno != EAGAIN) {
        perror("Cannot recv data");
        sockets_remove_socket(socket);
        close(socket);
      }
      return;
    }

    write(STDOUT_FILENO, buff, result);
    nparsed = http_parser_execute(parser, &http_request_callbacks, buff, result);
    if (nparsed != result) {
      fprintf(stderr, "Cannot parse http input from socket");
      sockets_remove_socket(socket);
      close(socket);
      return;
    }
  }
}

void proxy_accept_client(int socket) {
  http_parser* parser = (http_parser*) malloc(sizeof(http_parser));
  http_parser_init(parser, HTTP_REQUEST);

  handler_state_t* state = malloc(sizeof(handler_state_t));
  state->client_socket = socket;
  state->target_socket = -1;

  parser->data = state;

  sockets_add_socket(socket, &input_handler, parser);
}

static bool establish_target_connection(handler_state_t* state, char* host) {
  // TODO - resolve hostname, connect to target, dump url, dump headers
  return true;
}

static header_entry_t* create_header_entry(char* key) {
  header_entry_t* entry = (header_entry_t*) malloc(sizeof(header_entry_t));
  entry->key = key;
  entry->value = NULL;
  entry->next = NULL;
  return entry;
}

static bool dump_headers_buffer(header_entry_t* list, int socket) {
  header_entry_t* temp;
  char* output = NULL;
  size_t key_len, value_len;

  // TODO - change when multithreading
  // Unset nonblock
  int oldfl = fcntl(socket, F_GETFL);
  if (oldfl == -1) {
    perror("Cannot get socket flags");
    return false;
  }
  fcntl(socket, F_SETFL, oldfl & ~O_NONBLOCK);

  while(list) {
    if (!list->value) {
      free(list->key);
      temp = list;
      list = list->next;
      free(temp);
      continue;
    }

    key_len = strlen(list->key);
    value_len = strlen(list->value);
    output = realloc(output, key_len + value_len + 4); // 4 is ": ", "\n\0"

    memcpy(output, list->key, key_len);

    output[key_len] = ':';
    output[key_len + 1] = ' ';

    memcpy(output + key_len + 2, list->value, value_len);

    output[key_len + 2 + value_len] = '\n';
    output[key_len + 2 + value_len + 1] = '\0';

    if (send(socket, output, key_len + value_len + 4, 0) == -1) {
      perror("Cannot send header to socket");
      return false;
    }
  }

  // Return socket state
  fcntl(socket, F_SETFL, oldfl);

  return true;
}

