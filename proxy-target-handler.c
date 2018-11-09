
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "http-parser.h"
#include "proxy-handler.h"
#include "sockets-handler.h"

#include "proxy-target-handler.h"

#define BUFFER_SIZE 1024
#define PROTOCOL_VERSION_STR "HTTP/1.0"
#define DEF_LEN(str) sizeof(str) - 1

/**
 * Callback that, when notified of the possibility of writing to the socket,
 * sends new data to the socket.
 */
static void client_output_handler(int socket, void* arg) {
  handler_state_t* state = (handler_state_t*)arg;

  if (send_pstring(socket, &state->client_outbuff)) {
    if (state->proxy_finished) {
      sockets_remove_socket(state->client_socket);
      close(state->client_socket);
      handler_state_free(state);
      return;
    }
    sockets_cancel_out_handle(state->client_socket);
    sockets_enable_in_handle(state->target_socket);
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
static bool send_to_client(handler_state_t* state,
                           const char* buff,
                           size_t len) {
  if (!pstring_append(&state->client_outbuff, buff, len))
    return false;
  // TODO - optimisation: try to send, before this
  sockets_set_out_handler(state->client_socket, &client_output_handler, state);
  sockets_cancel_in_handle(state->target_socket);
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
  
  handler_state_t* state = (handler_state_t*)parser->data;
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
 * Handles responce header field from proxying target.
 */
static int handle_response_header_field(http_parser* parser,
                                        const char* at,
                                        size_t len) {
  handler_state_t* state = (handler_state_t*)parser->data;

  pstring_finalize(&state->url);

  if (state->buffered_headers) {               // Previous header exists
    if (state->buffered_headers->value.str) {  // Value stored, creating new
      if (state->buffered_headers != NULL) {
        pstring_finalize(&state->buffered_headers->value);
        dump_buffered_headers(state, &send_to_client);
      }
      header_entry_t* entry = create_header_entry(at, len);
      if (entry == NULL)
        return 1;
      entry->next = state->buffered_headers;
      state->buffered_headers = entry;
    } else {  // Key append
      if (!pstring_append(&state->buffered_headers->key, at, len)) {
        perror("Cannot append header key");
        return 1;
      }
    }
  } else
    state->buffered_headers = create_header_entry(at, len);

  return 0;
}

/**
 * Handles responce header value from proxying target.
 */
static int handle_response_header_value(http_parser* parser,
                                        const char* at,
                                        size_t len) {
  handler_state_t* state = (handler_state_t*)parser->data;

  pstring_finalize(&state->buffered_headers->key);

  if (!pstring_append(&state->buffered_headers->value, at, len)) {
    perror("Cannot store client header value");
    return 1;
  }

  return 0;
}

/**
 * Handles responce headers complete part from proxying target.
 */
static int handle_response_headers_complete(http_parser* parser) {
  handler_state_t* state = (handler_state_t*)parser->data;

  pstring_finalize(&state->url);
  if (state->buffered_headers != NULL) {
    pstring_finalize(&state->buffered_headers->value);
    dump_buffered_headers(state, &send_to_client);
  }
  send_to_client(state, "\r\n\r\n", 4);
  
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
  handler_state_t* state = (handler_state_t*)parser->data;

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
  handler_state_t* state = (handler_state_t*)parser->data;
  state->proxy_finished = true;
  sockets_remove_socket(state->target_socket);
  close(state->target_socket);
  state->target_socket = -1;
  
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
  handler_state_t* state = (handler_state_t*)parser->data;
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
      if (state->proxy_finished)
        free(parser);
      return;
    }

    nparsed =
        http_parser_execute(parser, &http_response_callbacks, buff, result);
    if (nparsed != result) {
      fprintf(stderr, "Cannot parse http input from target socket\n");
      free(parser);
      sockets_remove_socket(socket);
      close(socket);
      return;
    }

    if (state->proxy_finished) {
      free(parser);
      return;
    }
  }
}
