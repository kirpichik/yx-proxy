
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "http-handler.h"

#define LINE_DELIMITER "\r\n"
#define HEADER_DELIMITER ":"

static bool send_data(int socket, char* data, size_t data_len) {
  ssize_t len;
  size_t offset = 0;

  while ((len = send(socket, data + offset, data_len - offset, 0)) != -1) {
    if (len + offset == data_len)
      return true;
    offset += len;
  }

  return false;
}

static bool send_newline(int socket) {
  return send_data(socket, LINE_DELIMITER, sizeof(LINE_DELIMITER) - 1);
}

static bool send_space(int socket) {
  return send_data(socket, " ", 1);
}

static char* get_protocol_name(int protocol) {
  switch (protocol) {
    case HTTP_VERSION_1_0:
      return "HTTP/1.0";
    case HTTP_VERSION_1_1:
      return "HTTP/1.1";
  }
  return "";
}

static char* get_method_name(int method) {
  switch (method) {
    case HTTP_METHOD_GET:
      return "GET";
  }
  return "";
}

void http_init_handler(int socket, http_handler_t* result) {
  result->socket = socket;
  result->buff_offset = result->buff_len = 0;
}

bool http_init_client_initial_line(char method, char* uri, client_initial_line_t* result) {
  if ((result->uri = strdup(uri)) == NULL) {
    perror("Cannot dup URI string");
    return false;
  }

  result->method = method;
  result->http_version = HTTP_VERSION_DEFAULT;
  return true;
}

void http_init_server_initial_line(char status, server_initial_line_t* result) {
  result->http_version = HTTP_VERSION_DEFAULT;
  result->status = status;
}

bool http_init_headler_line(char* key, char* value, header_line_t* result) {
  if ((result->key = strdup(key)) == NULL) {
    perror("Cannot dup header key string");
    return false;
  }

  if ((result->value = strdup(value)) == NULL) {
    perror("Cannot dup header value string");
    free(result->key);
    return false;
  }

  return true;
}

bool http_read_client_initial_line(http_handler_t* handler, client_initial_line_t* result) {
  return false;
}

bool http_read_server_initial_line(http_handler_t* handler, server_initial_line_t* result) {
  return false;
}

int http_read_header_line(http_handler_t* handler, header_line_t* result) {
  return -1;
}

bool http_write_client_initial_line(http_handler_t* handler, client_initial_line_t* output) {
  char* method = get_method_name(output->method);
  if (!send_data(handler->socket, method, strlen(method))) {
    perror("Cannot write client initial line");
    return false;
  }

  if (!send_space(handler->socket)) {
    perror("Cannot write client initial line");
    return false;
  }

  if (!send_data(handler->socket, output->uri, strlen(output->uri))) {
    perror("Cannot write client initial line");
    return false;
  }

  char* version = get_protocol_name(output->http_version);
  if (!send_data(handler->socket, version, strlen(version))) {
    perror("Cannot write server initial line");
    return false;
  }

  if (!send_newline(handler->socket)) {
    perror("Cannot write header line");
    return false;
  }

  return true;
}

bool http_write_server_initial_line(http_handler_t* handler, server_initial_line_t* output) {
  char* version = get_protocol_name(output->http_version);
  if (!send_data(handler->socket, version, strlen(version))) {
    perror("Cannot write server initial line");
    return false;
  }

  if (!send_space(handler->socket)) {
    perror("Cannot write header line");
    return false;
  }

  char* code = NULL; // TODO - itoa
  if (!send_data(handler->socket, code, strlen(code))) {
    perror("Cannot write server initial line");
    return false;
  }

  // TODO - write code description

  if (!send_newline(handler->socket)) {
    perror("Cannot write header line");
    return false;
  }

  return true;
}

bool http_write_header_line(http_handler_t* handler, header_line_t* output) {
  if (!send_data(handler->socket, output->key, strlen(output->key))) {
    perror("Cannot write header line");
    return false;
  }

  if (!send_data(handler->socket, HEADER_DELIMITER, strlen(HEADER_DELIMITER))) {
    perror("Cannot write header line");
    return false;
  }

  if (!send_space(handler->socket)) {
    perror("Cannot write header line");
    return false;
  }

  if (!send_data(handler->socket, output->value, strlen(output->value))) {
    perror("Cannot write header line");
    return false;
  }

  if (!send_newline(handler->socket)) {
    perror("Cannot write header line");
    return false;
  }

  return true;
}

bool http_write_body_begin(http_handler_t* handler) {
  return send_newline(handler->socket);
}

void http_free_header_line(header_line_t* data) {
  free(data->key);
  free(data->value);
}

void http_free_client_initial_line(client_initial_line_t* data) {
  free(data->uri);
}

