
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "http-handler.h"

#define CHECK_ERROR(value, msg, ret) if (value) { perror(msg); return ret; }
#define CHECK_ERROR_INT(value, msg) CHECK_ERROR(value, msg, -1)
#define CHECK_ERROR_PTR(value, msg) CHECK_ERROR(value, msg, NULL)
#define CHECK_ERROR_BOOL(value, msg) CHECK_ERROR(value, msg, false)

#define CHECK_NULL(target, msg, ret) CHECK_ERROR(target == NULL, msg, ret)
#define CHECK_NULL_INT(target, msg) CHECK_NULL(target, msg, -1)
#define CHECK_NULL_PTR(target, msg) CHECK_NULL(target, msg, NULL)
#define CHECK_NULL_BOOL(target, msg) CHECK_NULL(target, msg, false)

#define CHECK_POSITIVE(target, msg, ret) CHECK_ERROR(target < 0, msg, ret)
#define CHECK_POSITIVE_INT(target, msg) CHECK_POSITIVE(target, msg, -1)
#define CHECK_POSITIVE_PTR(target, msg) CHECK_POSITIVE(target, msg, NULL)
#define CHECK_POSITIVE_BOOL(target, msg) CHECK_POSITIVE(target, msg, false)

#define LINE_DELIMITER "\r\n"
#define LINE_DELIM_LEN (sizeof(LINE_DELIMITER) - 1)
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
  return send_data(socket, LINE_DELIMITER, LINE_DELIM_LEN);
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

static ssize_t strstrn(char* haystack, size_t haystack_len, char* needle, size_t needle_len) {
  if (haystack_len < needle_len || haystack == NULL || needle == NULL)
    return -1;

  for (size_t i = 0; i < haystack_len - needle_len + 1; i++) {
    bool found = true;
    for (size_t j = 0; j < needle_len; j++)
      if (haystack[i] != needle[j]) {
        found = false;
        break;
      }

    if (found)
      return i;
  }

  return -1;
}

static char* recv_line(http_handler_t* handler, size_t* result_len) {
  ssize_t recv_len;
  size_t offset = 0;
  size_t len = 0;
  char* result = NULL;
  // TODO - search in previous stored buffer

  while ((recv_len = recv(handler->socket, handler->buffer, HTTP_BUFFER_SIZE, 0)) > 0) {
    size_t size = offset + recv_len;
    if (size > len) {
      result = (char*) realloc(result, len += HTTP_BUFFER_SIZE);
      CHECK_NULL_PTR(result, "Cannot receive HTTP line");
    }
    memcpy(result + offset, handler->buffer, recv_len);

    size_t backward = LINE_DELIM_LEN <= offset ? offset - LINE_DELIM_LEN : 0;
    size_t scan_len = backward + recv_len;

    ssize_t pos = strstrn(result + offset - backward, scan_len, LINE_DELIMITER, LINE_DELIM_LEN);
    if (pos != -1) {
      size_t end_pos = pos + offset - backward;
      if (end_pos != len) {
        result = (char*) realloc(result, end_pos);
        CHECK_NULL_PTR(result, "Cannot receive HTTP line");
      }

      // TODO - store handler buffer offsets

      (*result_len) = end_pos;
      return result;
    }

    offset += recv_len;
  }

  if (result != null)
    free(result);
  return NULL;
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

