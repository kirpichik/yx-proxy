
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
#include "proxy-utils.h"
#include "sockets-handler.h"

#include "proxy-handler.h"

void proxy_accept_client(int socket) {
  pthread_attr_t attr;
  int error;

  client_state_t* state = (client_state_t*)calloc(1, sizeof(client_state_t));
  if (state == NULL) {
    perror("Cannot allocate state for connection");
    goto error_malloc;
  }

  error = pthread_mutex_init(&state->lock, NULL);
  if (error) {
    proxy_error(error, "Cannot create mutex for client");
    goto error_mutex;
  }

  error = pthread_cond_init(&state->notifier, NULL);
  if (error) {
    proxy_error(error, "Cannot create condition for client");
    goto error_cond;
  }

  error = pthread_attr_init(&attr);
  if (error) {
    proxy_error(error, "Cannot create client thread attrs");
    goto error_attr;
  }
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  state->socket = socket;
  http_parser_init(&state->parser, HTTP_REQUEST);
  state->parser.data = state;

  error = pthread_create(&state->thread, &attr, &client_thread, state);
  if (error) {
    proxy_error(error, "Cannot create client thread");
    goto error_thread;
  }

  return;

error_thread:
  pthread_attr_destroy(&attr);
error_attr:
  pthread_cond_destroy(&state->notifier);
error_cond:
  pthread_mutex_destroy(&state->lock);
error_mutex:
  free(state);
error_malloc:
  close(socket);
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

  proxy_log("Connecting to %s...", host);

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = PF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  int error = getaddrinfo(hostname, port, &hints, &result);
  if (error) {
    fprintf(stderr, "Cannot resolve %s: %s", host, gai_strerror(error));
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

  proxy_log("Connected to %s with socket %d", host, sock);

cleanup:
  if (hostname != host)
    free(hostname);
  freeaddrinfo(result);
  return sock;
}

bool proxy_establish_connection(client_state_t* state, char* host) {
  pthread_attr_t attr;
  int error;

  state->target = (target_state_t*)calloc(1, sizeof(target_state_t));
  if (state->target == NULL) {
    perror("Cannot allocate target state");
    return false;
  }

  error = pthread_mutex_init(&state->target->lock, NULL);
  if (error) {
    proxy_error(error, "Cannot create mutex for target");
    goto error_mutex;
  }

  error = pthread_cond_init(&state->target->notifier, NULL);
  if (error) {
    proxy_error(error, "Cannot create condition for target");
    goto error_cond;
  }

  error = pthread_attr_init(&attr);
  if (error) {
    proxy_error(error, "Cannot create target thread attrs");
    goto error_attr;
  }
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  http_parser_init(&state->target->parser, HTTP_RESPONSE);
  state->target->parser.data = state->target;
  state->target->cache = state->cache;
  pstring_init(&state->target->outbuff);
  pstring_replace(&state->target->outbuff, state->target_outbuff.str,
                  state->target_outbuff.len);

  if ((state->target->socket = connect_target(host)) < 0)
    goto error_socket;

  error = pthread_create(&state->target->thread, &attr, &target_thread,
                         state->target);
  if (error) {
    proxy_error(error, "Cannot create target thread");
    goto error_socket;
  }

  return true;

error_socket:
  pstring_free(&state->target->outbuff);
  pthread_attr_destroy(&attr);
error_attr:
  pthread_cond_destroy(&state->target->notifier);
error_cond:
  pthread_mutex_destroy(&state->target->lock);
error_mutex:
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

  pstring_free(buff);

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
