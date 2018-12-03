
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "proxy-client-handler.h"
#include "proxy-target-handler.h"
#include "sockets-handler.h"

#include "proxy-handler.h"

#define DEFAULT_HTTP_PORT 80
#define BLOCKED_TLS_PORT "443"

void proxy_accept_client(int socket) {
  client_state_t* state = (client_state_t*)calloc(1, sizeof(client_state_t));
  if (state == NULL) {
    perror("Cannot allocate state for connection");
    close(socket);
    return;
  }

  http_parser_init(&state->parser, HTTP_REQUEST);

  state->socket = socket;
  pstring_init(&state->client_outbuff);
  pstring_init(&state->target_outbuff);
  pstring_init(&state->header_key);
  pstring_init(&state->header_value);
  pstring_init(&state->url);

  state->parser.data = state;

  if (!sockets_add_socket(socket, &client_handler, state)) {
    fprintf(stderr, "Too many file descriptors.\n");
    free(state);
    close(socket);
    return;
  }

  sockets_enable_in_handle(state->socket);
}

static int connect_target(char* host) {
  struct addrinfo hints, *result;
  char* split_pos = strrchr(host, ':');
  int sock = -1;
  char* hostname = host;
  char* port = "http";
  if (split_pos != NULL) {
    hostname = (char*)malloc((size_t)(split_pos - host + 1));
    memcpy(hostname, host, (size_t)(split_pos - host));
    hostname[split_pos - host] = '\0';
    port = host + (split_pos - host) + 1;
  }
  
  if (!strncmp(port, BLOCKED_TLS_PORT, sizeof(BLOCKED_TLS_PORT) - 1)) {
#ifdef _PROXY_DEBUG
    fprintf(stderr, "Ignore TLS connection to %s\n", host);
#endif
    if (hostname != host)
      free(hostname);
    return -1;
  }
  
#ifdef _PROXY_DEBUG
  fprintf(stderr, "Connecting to %s...\n", host);
#endif
  
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  int error = getaddrinfo(hostname, port, &hints, &result);
  if (error) {
    fprintf(stderr, "Cannot resolve %s: %s\n", host, gai_strerror(error));
    if (hostname != host)
      free(hostname);
    return -1;
  }
  
  sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (sock < 0) {
    perror("Cannot create target socket");
    goto cleanup;
  }
  
  if (connect(sock, result->ai_addr, result->ai_addrlen) < 0) {
    perror("Cannot connect to target");
    close(sock);
    sock = -1;
    goto cleanup;
  }
  
#ifdef _PROXY_DEBUG
  fprintf(stderr, "Connected to %s\n", host);
#endif
  
cleanup:
  if (hostname != host)
    free(hostname);
  freeaddrinfo(result);
  return sock;
}

bool proxy_establish_connection(client_state_t* state, char* host) {
  state->target = (target_state_t*)calloc(1, sizeof(target_state_t));
  if (state->target == NULL) {
    perror("Cannot allocate target state");
    return false;
  }
  
  http_parser_init(&state->target->parser, HTTP_RESPONSE);
  state->target->parser.data = state->target;
  state->target->cache = state->cache;
  pstring_init(&state->target->outbuff);
  pstring_replace(&state->target->outbuff, state->target_outbuff.str,
                  state->target_outbuff.len);
  
  if ((state->target->socket = connect_target(host)) < 0)
    goto error;
  
  if (!sockets_add_socket(state->target->socket, &target_handler,
                          state->target)) {
    fprintf(stderr, "Too many file descriptors\n");
    close(state->target->socket);
    goto error;
  }
  
  sockets_enable_in_handle(state->target->socket);
  sockets_enable_out_handle(state->target->socket);
  return true;
  
error:
  pstring_free(&state->target->outbuff);
  free(state->target);
  state->target = NULL;
  
  return false;
}

int send_pstring(int socket, pstring_t* buff) {
  size_t offset = 0;
  ssize_t result;

  while ((result = send(socket, buff->str + offset, buff->len - offset, 0)) !=
         buff->len - offset) {
    if (result == -1) {
      if (errno != EWOULDBLOCK) {
        perror("Cannot send data to socket");
        return -1;
      }
      pstring_substring(buff, offset);
      return 1;
    }

    offset += result;
  }

  free(buff->str);
  pstring_init(buff);

  return 0;
}

char* build_header_string(pstring_t* key,
                          pstring_t* value,
                          size_t* result_len) {
  size_t len = key->len + value->len + 4;  // 4 is ": " and "\r\n"
  char* output = (char*)malloc(len);
  if (output == NULL)
    return NULL;

  memcpy(output, key->str, key->len);
  output[key->len] = ':';
  output[key->len + 1] = ' ';
  memcpy(output + key->len + 2, value->str, value->len);
  output[len - 2] = '\r';
  output[len - 1] = '\n';

  (*result_len) = len;
  return output;
}
