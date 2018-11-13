
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "proxy-handler.h"
#include "sockets-handler.h"

#include "proxy-target-handler.h"

#define BUFFER_SIZE 1024
#define PROTOCOL_VERSION_STR "HTTP/1.0"
#define DEF_LEN(str) (sizeof(str) - 1)

/**
 * Callback that, when notified of the possibility of writing to the socket,
 * sends new data to the socket.
 */
static void client_output_handler(int socket, void* arg) {
  target_state_t* state = (target_state_t*)arg;

  if (send_pstring(socket, &state->outbuff)) {
    if (state->proxy_finished) {
      target_hup_handler(state->socket, state);
      return;
    }
    sockets_cancel_out_handle(state->client->socket);
    sockets_enable_in_handle(state->socket);
  }
}

/**
 * Saves the data that required to send to client.
 *
 * @param state Current state.
 * @param buff Source buffer.
 * @param len Count of bytes from buffer.
 *
 * @return {@code true} if data saved.
 */
static bool send_to_client(target_state_t* state,
                           const char* buff,
                           size_t len) {
  if (!pstring_append(&state->outbuff, buff, len))
    return false;
  // TODO - optimisation: try to send, before this
  sockets_set_out_handler(state->client->socket, &client_output_handler, state);
  sockets_cancel_in_handle(state->socket);
  return true;
}

/**
 * @param code HTTP code num.
 * @param res Return arg, that stores HTTP code string representation.
 */
static void http_code_to_str(int code, char res[3]) {
  res[2] = '0' + code % 10;
  res[1] = '0' + code % 100 / 10;
  res[0] = '0' + code / 100;
}

/**
 * Handles responce HTTP status from proxying target.
 */
static int handle_response_status(http_parser* parser,
                                  const char* at,
                                  size_t len) {
  // FIXME - repeatable function call

  target_state_t* state = (target_state_t*)parser->data;
  char code[3];
  http_code_to_str(parser->status_code, code);

  // "<version> <code> <desc>\r\n"
  size_t size = DEF_LEN(PROTOCOL_VERSION_STR) + sizeof(code) + len + 4;
  char* output = (char*)malloc(size + 1);

  memcpy(output, PROTOCOL_VERSION_STR, DEF_LEN(PROTOCOL_VERSION_STR));
  output[DEF_LEN(PROTOCOL_VERSION_STR)] = ' ';
  memcpy(output + DEF_LEN(PROTOCOL_VERSION_STR) + 1, code, sizeof(code));
  output[DEF_LEN(PROTOCOL_VERSION_STR) + sizeof(code) + 1] = ' ';
  memcpy(output + DEF_LEN(PROTOCOL_VERSION_STR) + sizeof(code) + 2, at, len);
  output[size - 2] = '\r';
  output[size - 1] = '\n';

#ifdef _PROXY_DEBUG
  output[size] = '\0';
  fprintf(stderr, "Responce status %d sent.\n", parser->status_code);
#endif

  bool result = send_to_client(state, output, size);
  free(output);

  return result ? 0 : 1;
}

/**
 * Dumps all buffered headers to client.
 *
 * @param state Current handler state.
 *
 * @return {@code true} if all headers successfully dumped.
 */
static bool dump_buffered_headers(target_state_t* state) {
  header_entry_t* entry = state->headers;
  char* output;
  size_t len;

  while (entry) {
    output = build_header_string(entry, &len);
    if (output == NULL)
      return false;

    if (!send_to_client(state, output, len)) {
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

/**
 * Handles responce header field from proxying target.
 */
static int handle_response_header_field(http_parser* parser,
                                        const char* at,
                                        size_t len) {
  target_state_t* state = (target_state_t*)parser->data;

  if (state->headers) {               // Previous header exists
    if (state->headers->value.str) {  // Value stored, creating new
      if (state->headers != NULL) {
        pstring_finalize(&state->headers->value);
        dump_buffered_headers(state);
      }
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
 * Handles responce header value from proxying target.
 */
static int handle_response_header_value(http_parser* parser,
                                        const char* at,
                                        size_t len) {
  target_state_t* state = (target_state_t*)parser->data;

  pstring_finalize(&state->headers->key);

  if (!pstring_append(&state->headers->value, at, len)) {
    perror("Cannot store client header value");
    return 1;
  }

  return 0;
}

/**
 * Handles responce headers complete part from proxying target.
 */
static int handle_response_headers_complete(http_parser* parser) {
  target_state_t* state = (target_state_t*)parser->data;

  if (state->headers != NULL) {
    pstring_finalize(&state->headers->value);
    dump_buffered_headers(state);
  }
  send_to_client(state, "\r\n", 2);

#ifdef _PROXY_DEBUG
  fprintf(stderr, "Responce headers complete.\n");
#endif

  return 0;
}

/**
 * Handles responce body.
 */
static int handle_response_body(http_parser* parser,
                                const char* at,
                                size_t len) {
  target_state_t* state = (target_state_t*)parser->data;

  if (!send_to_client(state, at, len)) {
    fprintf(stderr, "Cannot proxy target data body to client socket\n");
    return 1;
  }

  return 0;
}

/**
 * Handles responce message complete and close proxying target socket.
 */
static int handle_response_message_complete(http_parser* parser) {
  target_state_t* state = (target_state_t*)parser->data;

  state->proxy_finished = true;

#ifdef _PROXY_DEBUG
  fprintf(stderr, "Responce message complete.\n");
#endif

  return 0;
}

static http_parser_settings http_response_callbacks = {
    NULL, /* on_message_begin */
    NULL, /* on_url */
    handle_response_status,
    handle_response_header_field,
    handle_response_header_value,
    handle_response_headers_complete,
    handle_response_body,
    handle_response_message_complete,
    NULL, /* on_chunk_header */
    NULL  /* on_chunk_complete */
};

void target_input_handler(int socket, void* arg) {
  http_parser* parser = (http_parser*)arg;
  target_state_t* state = (target_state_t*)parser->data;
  char buff[BUFFER_SIZE];
  ssize_t result;
  size_t nparsed;

  while (1) {
    result = recv(socket, buff, BUFFER_SIZE, 0);

    if (result == -1) {
      if (errno != EAGAIN) {
        perror("Cannot recv data from target");
        free(parser);
        sockets_remove_socket(socket);
        close(socket);
      }
      return;
    } else if (result == 0) {
      if (errno != EAGAIN)
        target_hup_handler(socket, (target_state_t*)parser->data);
      return;
    }

    nparsed =
        http_parser_execute(parser, &http_response_callbacks, buff, result);
    if (nparsed != result) {
      fprintf(stderr, "Cannot parse http input from target socket\n");
      target_hup_handler(socket, (target_state_t*)parser->data);
      return;
    }

    if (state->proxy_finished)
      return;
  }
}

void target_hup_handler(int socket, void* arg) {
  target_state_t* state = (target_state_t*)arg;
  sockets_remove_socket(socket);

#ifdef _PROXY_DEBUG
  if (!state->proxy_finished)
    fprintf(stderr, "Target socket hup.\n");
#endif

  if (state->client != NULL) {
    close(state->client->socket);
    sockets_remove_socket(state->client->socket);
    pstring_free(&state->client->outbuff);
    free_header_entry_list(state->client->headers);
    pstring_free(&state->client->url);
    free(state->client);
  }

  close(state->socket);
  pstring_free(&state->outbuff);
  free_header_entry_list(state->headers);
  free(state);
}
