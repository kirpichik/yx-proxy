
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#include "http-parser.h"
#include "sockets-handler.h"
#include "proxy-handler.h"
#include "proxy-target-handler.h"

#include "proxy-client-handler.h"

#define BUFFER_SIZE 1024
#define DEFAULT_HTTP_PORT 80
#define HEADER_CONNECTION "Connection"
#define HEADER_HOST "Host"
#define HEADER_CONNECTION_CLOSE "close"
#define PROTOCOL_VERSION_STR "HTTP/1.1"

#define DEF_LEN(str) sizeof(str) - 1

static void target_output_handler(int socket, void* arg) {
  // TODO
}

static bool send_to_target(handler_state_t* state, const char* buff, size_t len) {
  if (!pstring_append(&state->target_outbuff, buff, len))
    return false;
  // TODO - optimisation: try to send, before this
  sockets_set_out_handler(state->target_socket, &target_output_handler, state);
  sockets_cancel_in_handle(state->client_socket);
  return true;
}

static char* get_method_by_id(int id) {
  switch (id) {
    case 0:
      return "DELETE";
    case 1:
      return "GET";
    case 2:
      return "HEAD";
    case 3:
      return "POST";
    case 4:
      return "PUT";
    default:
      return NULL;
  }
}

static bool dump_initial_line(handler_state_t* state, char* method) {
  if (method == NULL)
    return false;

  size_t prefix_len = strlen(method) + 1; // "<method> "
  size_t suffix_len = DEF_LEN(PROTOCOL_VERSION_STR) + 2; // " <version>\n"
  size_t url_len = strlen(state->url.str);
  size_t len = prefix_len + url_len +  suffix_len;

  char* output = (char*) malloc(len);
  if (output == NULL)
    return false;

  memcpy(output, method, prefix_len - 1);
  output[prefix_len - 1] = ' ';
  memcpy(output + prefix_len, state->url.str, url_len);
  output[prefix_len + url_len] = ' ';
  memcpy(output + prefix_len + url_len, PROTOCOL_VERSION_STR, suffix_len - 1);
  output[len - 1] = '\n';

  bool result = send_to_target(state, output, len);
  free(output);

  return result;
}

static struct in_addr* resolve_hostname(char* hostname) {
  struct hostent* entry = gethostbyname(hostname);
  if (entry == NULL)
    return NULL;

  return (struct in_addr*) entry->h_addr_list[0];
}

static bool establish_target_connection(handler_state_t* state, char* host) {
  struct sockaddr_in addr;
  struct in_addr* resolved;
  char* hostname = host;
  int port = DEFAULT_HTTP_PORT;
  char* split_pos = strrchr(host, ':');

  if (split_pos != NULL) {
    port = atoi(split_pos + (-strlen(host)));
    hostname = (char*) malloc((size_t) (split_pos - host + 1));
    memcpy(hostname, host, (size_t) (split_pos - host));
    hostname[split_pos - host] = '\0';
  }

  state->target_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (state->target_socket < 0) {
    perror("Cannot create target socket");
    return false;
  }

  if ((resolved = resolve_hostname(hostname)) == NULL) {
    fprintf(stderr, "Cannot resolve hostname: %s\n", hostname);
    close(state->target_socket);
    if (hostname != host)
      free(hostname);
    return false;
  }

  addr.sin_addr.s_addr = *((in_addr_t*) resolved);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (hostname != host)
    free(hostname);

  if (connect(state->target_socket, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
    perror("Cannot connect to target");
    close(state->target_socket);
    return false;
  }

  sockets_add_socket(state->target_socket);
  sockets_set_in_handler(state->target_socket, &target_input_handler, state);

  return true;
}

static int handle_request_url(http_parser* parser, const char* at, size_t len) {
  handler_state_t* state = (handler_state_t*) parser->data;

  if (!pstring_append(&state->url, at, len)) {
    perror("Cannot store client url");
    return 1;
  }

  return 0;
}

static bool handle_finished_header(http_parser* parser) {
  handler_state_t* state = (handler_state_t*) parser->data;
  header_entry_t* header = state->buffered_headers;
  if (header == NULL)
    return true;

  if (!strncmp(header->key.str, HEADER_CONNECTION, DEF_LEN(HEADER_CONNECTION))) {
    pstring_replace(&header->value, HEADER_CONNECTION, DEF_LEN(HEADER_CONNECTION));
    pstring_finalize(&header->value);
    return true;
  }

  pstring_finalize(&header->value);

  // Already connected
  if (state->target_socket != -1) {
    // TODO - dump buffered headers
    return true;
  }

  if (!strncmp(header->key.str, HEADER_HOST, DEF_LEN(HEADER_HOST))) {
    if (!establish_target_connection(state, header->value.str))
      return false;
    dump_initial_line(state, get_method_by_id(parser->method));
  }

  return true;
}

static int handle_request_header_field(http_parser* parser, const char* at, size_t len) {
  handler_state_t* state = (handler_state_t*) parser->data;

  pstring_finalize(&state->url);

  if (state->buffered_headers) { // Previous header exists
    if (state->buffered_headers->value.str) { // Value stored, creating new
      // TODO - handle previous header
      header_entry_t* entry = create_header_entry(at, len);
      if (entry == NULL)
        return 1;
      entry->next = state->buffered_headers;
      state->buffered_headers = entry;
    } else { // Key append
      if (!pstring_append(&state->buffered_headers->key, at, len)) {
        perror("Cannot append header key");
        return 1;
      }
    }
  } else
    state->buffered_headers = create_header_entry(at, len);

  return 0;
}

static int handle_request_header_value(http_parser* parser, const char* at, size_t len) {
  handler_state_t* state = (handler_state_t*) parser->data;

  pstring_finalize(&state->buffered_headers->key);

  if (!pstring_append(&state->buffered_headers->value, at, len)) {
    perror("Cannot store client header value");
    return 1;
  }

  return 0;
}

static int handle_request_headers_complete(http_parser* parser) {
  handler_state_t* state = (handler_state_t*) parser->data;

  pstring_finalize(&state->url);
  handle_finished_header(parser);
  send_to_target(state, "\r\n\r\n", 4);

  return 0;
}

static int handle_request_body(http_parser* parser, const char* at, size_t len) {
  handler_state_t* state = (handler_state_t*) parser->data;

  if (send_to_target(state, at, len)) {
    perror("Cannot proxy client data body to target socket");
    return 1;
  }

  return 0;
}

static http_parser_settings http_request_callbacks = {
  NULL, /* on_message_begin */
  handle_request_url,
  NULL, /* on_status */
  handle_request_header_field,
  handle_request_header_value,
  handle_request_headers_complete,
  handle_request_body,
  NULL, /* on_message_complete */
  NULL, /* on_chunk_header */
  NULL /* on_chunk_complete */
};

void client_input_handler(int socket, void* arg) {
  http_parser* parser = (http_parser*) arg;
  char buff[BUFFER_SIZE];
  int result, nparsed;

  while (1) {
    result = recv(socket, buff, BUFFER_SIZE, 0);

    if (result == -1) {
      if (errno != EAGAIN) {
        perror("Cannot recv data from client");
        sockets_remove_socket(socket);
        close(socket);
      }
      return;
    } else if (result == 0)
      return;

    nparsed = http_parser_execute(parser, &http_request_callbacks, buff, result);
    if (nparsed != result) {
      fprintf(stderr, "Cannot parse http input from client socket");
      sockets_remove_socket(socket);
      close(socket);
      return;
    }
  }
}
