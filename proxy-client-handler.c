
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "proxy-handler.h"
#include "sockets-handler.h"
#include "cache.h"

#include "proxy-client-handler.h"

#define BUFFER_SIZE 1024

// Strings for HTTP protocol
#define HEADER_CONNECTION "Connection"
#define HEADER_HOST "Host"
#define HEADER_CONNECTION_CLOSE "close"
#define PROTOCOL_VERSION_STR "HTTP/1.0"

#define DEF_LEN(str) (sizeof(str) - 1)

/**
 * Callback that, when notified of the possibility of writing to the socket,
 * sends new data to the socket.
 */
static void target_output_handler(int socket, void* arg) {
  client_state_t* state = (client_state_t*)arg;

  if (send_pstring(socket, &state->outbuff)) {
    sockets_cancel_out_handle(state->target->socket);
    sockets_enable_in_handle(state->socket);
  }
}

/**
 * Saves the data that required to send to proxying target.
 *
 * @param state Current state.
 * @param buff Source buffer.
 * @param len Count of bytes from buffer.
 *
 * @return {@code true} if data saved.
 */
static bool send_to_target(client_state_t* state,
                           const char* buff,
                           size_t len) {
  // Drop output if cache used
  if (state->use_cache)
    return true;
  
  if (!pstring_append(&state->outbuff, buff, len))
    return false;
  // TODO - optimisation: try to send, before this
  sockets_set_out_handler(state->target->socket, &target_output_handler, state);
  sockets_cancel_in_handle(state->socket);
  return true;
}

/**
 * Callback that, when notified of the possibility of writing to the socket,
 * sends new data to the socket.
 */
static void client_output_handler(int socket, void* arg) {
  client_state_t* state = (client_state_t*)arg;
  
  if (send_pstring(socket, &state->client_outbuff))
    sockets_cancel_out_handle(state->socket);
}

/**
 * @return Method string name from http-parser code id.
 */
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

/**
 * Dumps initial request line from client to proxying target.
 *
 * @param state Current state.
 * @param method String name of request method.
 *
 * @return {@code true} if initial line successfully sent.
 */
static bool dump_initial_line(client_state_t* state, char* method) {
  if (method == NULL)
    return false;

  size_t prefix_len = strlen(method) + 1;                 // "<method> "
  size_t suffix_len = DEF_LEN(PROTOCOL_VERSION_STR) + 3;  // " <version>\r\n"
  size_t url_len = strlen(state->url.str);
  size_t len = prefix_len + url_len + suffix_len;

  // TODO - allocate at stack
  char* output = (char*)malloc(len + 1);
  if (output == NULL)
    return false;

  memcpy(output, method, prefix_len - 1);
  output[prefix_len - 1] = ' ';
  memcpy(output + prefix_len, state->url.str, url_len);
  output[prefix_len + url_len] = ' ';
  memcpy(output + prefix_len + url_len + 1, PROTOCOL_VERSION_STR,
         suffix_len - 1);
  output[len - 2] = '\r';
  output[len - 1] = '\n';

#ifdef _PROXY_DEBUG
  output[len] = '\0';
  fprintf(stderr, "Dump initial line: \"%s\"\n", output);
#endif

  bool result = send_to_target(state, output, len);
  free(output);

  return result;
}

/**
 * Dumps all buffered headers to target.
 *
 * @param state Current handler state.
 *
 * @return {@code true} if all headers successfully dumped.
 */
static bool dump_buffered_headers(client_state_t* state) {
  header_entry_t* entry = state->headers;
  char* output;
  size_t len;

  while (entry) {
    output = build_header_string(entry, &len);
    if (output == NULL)
      return false;

    if (!send_to_target(state, output, len)) {
      free(output);
      return false;
    }

    free(output);
    state->headers = entry->next;
    pstring_free(&entry->key);
    pstring_free(&entry->value);
    free(entry);
    entry = state->headers;
  }

  return true;
}

static void accept_cache_updates(cache_entry_t* entry, void* arg) {
  client_state_t* state = (client_state_t*) arg;
  if (entry->data.str == NULL)
    return;
  
  if (!pstring_append(&state->client_outbuff, entry->data.str + entry->offset, entry->data.len - entry->offset))
    return;
  // TODO - optimisation: try to send, before this
  sockets_set_out_handler(state->socket, &client_output_handler, state);
  return;
}

static bool establish_cached_connection(client_state_t* state, char* host) {
  // FIXME - form url from state->url and host
  int result = cache_find_or_create(state->url.str, &state->cache);
  if (result == -1)
    return false;
  
  state->reader = cache_entry_subscribe(state->cache, &accept_cache_updates, state);
  state->use_cache = true;

  if (result == 1) {
    state->use_cache = false;
    return proxy_establish_connection(state, host);
  }
  
  return true;
}

/**
 * Handles request URL input data.
 */
static int handle_request_url(http_parser* parser, const char* at, size_t len) {
  client_state_t* state = (client_state_t*)parser->data;

  if (!pstring_append(&state->url, at, len)) {
    perror("Cannot store client url");
    return 1;
  }

  return 0;
}

/**
 * Finalize request header.
 */
static bool handle_finished_header(http_parser* parser) {
  client_state_t* state = (client_state_t*)parser->data;
  header_entry_t* header = state->headers;
  if (header == NULL)
    return true;

  if (!strncmp(header->key.str, HEADER_CONNECTION,
               DEF_LEN(HEADER_CONNECTION))) {
    pstring_replace(&header->value, HEADER_CONNECTION_CLOSE,
                    DEF_LEN(HEADER_CONNECTION_CLOSE));
    pstring_finalize(&header->value);
    return true;
  }

  pstring_finalize(&header->value);

  // Already connected or cache active
  if (state->use_cache || state->target != NULL)
    return dump_buffered_headers(state);

  if (!strncmp(header->key.str, HEADER_HOST, DEF_LEN(HEADER_HOST))) {
    if (!establish_cached_connection(state, header->value.str))
      return false;
    if (!dump_initial_line(state, get_method_by_id(parser->method)))
      return false;
    return dump_buffered_headers(state);
  }

  return true;
}

/**
 * Handles request header field input data.
 */
static int handle_request_header_field(http_parser* parser,
                                       const char* at,
                                       size_t len) {
  client_state_t* state = (client_state_t*)parser->data;

  pstring_finalize(&state->url);

  if (state->headers) {               // Previous header exists
    if (state->headers->value.str) {  // Value stored, creating new
      if (!handle_finished_header(parser))
        return 1;
      header_entry_t* entry = create_header_entry(at, len);
      if (entry == NULL)
        return 1;
      entry->next = state->headers;
      state->headers = entry;
    } else {  // Key append
      if (!pstring_append(&state->headers->key, at, len)) {
        perror("Cannot append header key");
        return 1;
      }
    }
  } else
    state->headers = create_header_entry(at, len);

  return 0;
}

/**
 * Handles request header value input data.
 */
static int handle_request_header_value(http_parser* parser,
                                       const char* at,
                                       size_t len) {
  client_state_t* state = (client_state_t*)parser->data;

  pstring_finalize(&state->headers->key);

  if (!pstring_append(&state->headers->value, at, len)) {
    perror("Cannot store client header value");
    return 1;
  }

  return 0;
}

/**
 * Handles request headers complete part.
 */
static int handle_request_headers_complete(http_parser* parser) {
  client_state_t* state = (client_state_t*)parser->data;

  pstring_finalize(&state->url);
  if (!handle_finished_header(parser))
    return 1;
  send_to_target(state, "\r\n", 2);

#ifdef _PROXY_DEBUG
  fprintf(stderr, "Request headers complete.\n");
#endif

  return 0;
}

/**
 * Handles request body.
 */
static int handle_request_body(http_parser* parser,
                               const char* at,
                               size_t len) {
  client_state_t* state = (client_state_t*)parser->data;

  if (!send_to_target(state, at, len)) {
    fprintf(stderr, "Cannot proxy client data body to target socket\n");
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
    NULL  /* on_chunk_complete */
};

void client_input_handler(int socket, void* arg) {
  http_parser* parser = (http_parser*)arg;

  char buff[BUFFER_SIZE];
  ssize_t result;
  size_t nparsed;

  while (1) {
    result = recv(socket, buff, BUFFER_SIZE, 0);

    if (result == -1) {
      if (errno != EAGAIN) {
        perror("Cannot recv data from client");
        free(parser);
        sockets_remove_socket(socket);
        close(socket);
      }
      return;
    } else if (result == 0) {
      if (errno != EAGAIN)
        client_hup_handler(socket, (client_state_t*)parser->data);
      return;
    }

    nparsed =
        http_parser_execute(parser, &http_request_callbacks, buff, result);
    if (nparsed != result) {
      fprintf(stderr, "Cannot parse http input from client socket\n");
      client_hup_handler(socket, (client_state_t*)parser->data);
      return;
    }
  }
}

void client_hup_handler(int socket, void* arg) {
  client_state_t* state = (client_state_t*)arg;
  sockets_remove_socket(socket);

#ifdef _PROXY_DEBUG
  fprintf(stderr, "Client socket hup.\n");
#endif

  if (state->target != NULL) {
    close(state->target->socket);
    sockets_remove_socket(state->target->socket);
    pstring_free(&state->client_outbuff);
    free_header_entry_list(state->target->headers);
    free(state->target);
  }

  cache_entry_unsubscribe(state->cache, state->reader);
  close(state->socket);
  pstring_free(&state->outbuff);
  pstring_free(&state->url);
  free_header_entry_list(state->headers);
  free(state);
}
