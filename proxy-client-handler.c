
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cache.h"
#include "proxy-handler.h"
#include "sockets-handler.h"

#include "proxy-client-handler.h"

#define BUFFER_SIZE 4096

// Strings for HTTP protocol
#define HEADER_CONNECTION "Connection"
#define HEADER_HOST "Host"
#define HEADER_CONNECTION_CLOSE "close"
#define PROTOCOL_VERSION_STR "HTTP/1.0"
#define LINE_DELIM "\r\n"

#define DEF_LEN(str) (sizeof(str) - 1)

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

  // Only store if no connection
  if (state->target == NULL)
    return pstring_append(&state->target_outbuff, buff, len);

  if (!pstring_append(&state->target->outbuff, buff, len))
    return false;

  // TODO - optimisation: try to send, before this
  sockets_enable_out_handle(state->target->socket);
  sockets_cancel_in_handle(state->socket);

  return true;
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
 * Extracts path from URL.
 *
 * http://example.com/path/to/target -> /path/to/target
 *
 * @return Extracted path or {@code NULL}.
 */
static char* extract_path_from_url(char* url, size_t len) {
  size_t slash_count = 0;
  char* res = url;

  for (size_t i = 0; i < len; i++) {
    if (*(++res) == '/')
      slash_count++;

    if (slash_count == 3)
      return res;
  }

  return NULL;
}

/**
 * Dumps initial request line from client to proxying target.
 *
 * @param state Current state.
 *
 * @return {@code true} if initial line successfully sent.
 */
static bool dump_initial_line(client_state_t* state) {
  char* method = get_method_by_id(state->parser.method);
  if (method == NULL)
    return false;

  char* path = extract_path_from_url(state->url.str, state->url.len);
  if (path == NULL)
    return false;

  size_t prefix_len = strlen(method) + 1;                 // "<method> "
  size_t suffix_len = DEF_LEN(PROTOCOL_VERSION_STR) + 3;  // " <version>\r\n"
  size_t url_len = state->url.len - (path - state->url.str);
  size_t len = prefix_len + url_len + suffix_len;

  // TODO - allocate on stack
  char* output = (char*)malloc(len + 1);
  if (output == NULL)
    return false;

  memcpy(output, method, prefix_len - 1);
  output[prefix_len - 1] = ' ';
  memcpy(output + prefix_len, path, url_len);
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
 * Handles target cache entry updates.
 */
static void accept_cache_updates(cache_entry_t* entry, void* arg) {
  client_state_t* state = (client_state_t*)arg;
  if (entry->data.str != NULL)
    sockets_enable_out_handle(state->socket);
}

/**
 * Searches for cache entry with required URL.
 * If cache entry found, use it.
 * If cache entry not found, creates connection and use it.
 */
static bool establish_cached_connection(client_state_t* state, char* host) {
  // FIXME - form url from state->url and host
  int result = cache_find_or_create(state->url.str, &state->cache);
  if (result == -1)
    return false;

  state->reader =
      cache_entry_subscribe(state->cache, &accept_cache_updates, state);
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
    state->parse_error = true;
    return 1;
  }

  return 0;
}

/**
 * Dumps client header to target connection.
 */
static bool dump_buffered_header(client_state_t* state) {
  size_t len;
  char* line =
      build_header_string(&state->header_key, &state->header_value, &len);

  if (line == NULL) {
    perror("Cannot allocate client output header line");
    return false;
  }

  if (!send_to_target(state, line, len)) {
    fprintf(stderr, "Cannot send header to target\n");
    free(line);
    return false;
  }

  pstring_free(&state->header_key);
  pstring_free(&state->header_value);
  free(line);
  return true;
}

/**
 * Finalize request header.
 */
static bool handle_finished_header(client_state_t* state) {
  if (state->header_key.str == NULL)
    return true;

  // Connection: close
  if (!strncmp(state->header_key.str, HEADER_CONNECTION,
               DEF_LEN(HEADER_CONNECTION))) {
    pstring_replace(&state->header_value, HEADER_CONNECTION_CLOSE,
                    DEF_LEN(HEADER_CONNECTION_CLOSE));
    pstring_finalize(&state->header_value);
    return dump_buffered_header(state);
  }

  pstring_finalize(&state->header_value);

  // Host: <host>
  if (!strncmp(state->header_key.str, HEADER_HOST, DEF_LEN(HEADER_HOST))) {
    return establish_cached_connection(state, state->header_value.str) &&
           dump_buffered_header(state);
  }

  return dump_buffered_header(state);
}

/**
 * Handles request header field input data.
 */
static int handle_request_header_field(http_parser* parser,
                                       const char* at,
                                       size_t len) {
  client_state_t* state = (client_state_t*)parser->data;

  if (!state->url_dumped) {
    pstring_finalize(&state->url);
    dump_initial_line(state);
    state->url_dumped = true;
  }

  // Handle previous header
  if (!handle_finished_header(state)) {
    state->parse_error = true;
    return 1;
  }

  // Append to current header key
  if (!pstring_append(&state->header_key, at, len)) {
    perror("Cannot append header key");
    state->parse_error = true;
    return 1;
  }

  return 0;
}

/**
 * Handles request header value input data.
 */
static int handle_request_header_value(http_parser* parser,
                                       const char* at,
                                       size_t len) {
  client_state_t* state = (client_state_t*)parser->data;

  pstring_finalize(&state->header_key);

  if (!pstring_append(&state->header_value, at, len)) {
    perror("Cannot store client header value");
    state->parse_error = true;
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
  if (!handle_finished_header(state)) {
    state->parse_error = true;
    return 1;
  }
  send_to_target(state, LINE_DELIM, DEF_LEN(LINE_DELIM));

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
    state->parse_error = true;
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

/**
 * Handles client input data.
 */
static bool client_input_handler(client_state_t* state) {
  char buff[BUFFER_SIZE];
  ssize_t result;
  size_t nparsed;

  result = recv(state->socket, buff, BUFFER_SIZE, 0);

  if (result == -1) {
    if (errno != EAGAIN) {
      perror("Cannot recv data from client");
      return false;
    }
    return true;
  } else if (result == 0)
    return true;

  nparsed = http_parser_execute(&state->parser, &http_request_callbacks, buff,
                                result);
  if (nparsed != result || state->parse_error) {
    fprintf(stderr, "Cannot parse http input from client socket\n");
    return false;
  }

  return true;
}

/**
 * Handles client output data.
 */
static bool client_output_handler(client_state_t* state) {
  char buff[BUFFER_SIZE];
  ssize_t len =
      cache_entry_extract(state->cache, state->cache_offset, buff, BUFFER_SIZE);
  if (len == -1)
    return false;

  if (len == 0) {
    if (state->cache->finished)
      return false;
    sockets_cancel_out_handle(state->socket);
    return true;
  }

  state->cache_offset += len;
  if (!pstring_append(&state->client_outbuff, buff, len))
    return false;

  return send_pstring(state->socket, &state->client_outbuff) != -1;
}

/**
 * Cleanup all client data.
 */
static void client_cleanup(client_state_t* state) {
  sockets_remove_socket(state->socket);
  cache_entry_unsubscribe(state->cache, state->reader);
  pstring_free(&state->client_outbuff);
  pstring_free(&state->target_outbuff);
  pstring_free(&state->url);
  pstring_free(&state->header_key);
  pstring_free(&state->header_value);
  free(state);
}

void client_handler(int socket, int events, void* arg) {
  client_state_t* state = (client_state_t*)arg;

  // Handle output
  if (events & POLLOUT) {
    if (!client_output_handler(state)) {
      client_cleanup(state);
      return;
    }
  }

  // Handle input
  if (events & (POLLIN | POLLPRI)) {
    if (!client_input_handler(state)) {
      client_cleanup(state);
      return;
    }
  }

  // Handle hup
  if (events & POLLHUP) {
    client_cleanup(state);
  }
}
