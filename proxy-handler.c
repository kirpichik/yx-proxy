
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "http-parser.h"
#include "sockets-handler.h"

#include "proxy-handler.h"

#define BUFFER_SIZE 1024

static int handle_message_begin(http_parser* parser) {
  printf("MESSAGE BEGIN\n");
  return 0;
}

static int handle_url(http_parser* parser, const char* at, size_t length) {
  printf("URL\n");
  return 0;
}

static int handle_status(http_parser* parser, const char* at, size_t length) {
  printf("STATUS\n");
  return 0;
}

static int handle_header_field(http_parser* parser, const char* at, size_t length) {
  printf("HEADER FIELD\n");
  return 0;
}

static int handle_header_value(http_parser* parser, const char* at, size_t length) {
  printf("HEADER VALUE\n");
  return 0;
}

static int handle_headers_complete(http_parser* parser) {
  printf("HEADERS COMPLETE\n");
  return 0;
}

static int handle_body(http_parser* parser, const char* at, size_t length) {
  printf("BODY\n");
  return 0;
}

static int handle_message_complete(http_parser* parser) {
  printf("MESSAGE COMPLETE\n");
  return 0;
}

static http_parser_settings http_callbacks = {
  handle_message_begin,
  handle_url,
  handle_status,
  handle_header_field,
  handle_header_value,
  handle_headers_complete,
  handle_body,
  handle_message_complete,
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
    nparsed = http_parser_execute(parser, &http_callbacks, buff, result);
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
  http_parser_init(parser, HTTP_BOTH);

  sockets_add_socket(socket, &input_handler, parser);
}

