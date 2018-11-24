
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

void proxy_accept_client(int socket) {
  client_state_t* state = (client_state_t*)malloc(sizeof(client_state_t));
  if (state == NULL) {
    perror("Cannot allocate state for connection");
    close(socket);
    return;
  }
  memset(state, 0, sizeof(client_state_t));

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

/**
 * Resolves hostname to IP-address.
 *
 * @param hostname Required hostname.
 *
 * @return IP-address struct.
 */
static struct in_addr* resolve_hostname(char* hostname) {
  struct hostent* entry = gethostbyname(hostname);
  if (entry == NULL)
    return NULL;

  return (struct in_addr*)entry->h_addr_list[0];
}

bool proxy_establish_connection(client_state_t* state, char* host) {
  struct sockaddr_in addr;
  struct in_addr* resolved;
  char* hostname = host;
  int port = DEFAULT_HTTP_PORT;
  char* split_pos = strrchr(host, ':');

  if (split_pos != NULL) {
    port = atoi(host + (split_pos - host) + 1);
    hostname = (char*)malloc((size_t)(split_pos - host + 1));
    memcpy(hostname, host, (size_t)(split_pos - host));
    hostname[split_pos - host] = '\0';
  }

  state->target = (target_state_t*)malloc(sizeof(target_state_t));
  if (state->target == NULL) {
    perror("Cannot allocate target state");
    return false;
  }

  memset(state->target, 0, sizeof(target_state_t));
  pstring_init(&state->target->outbuff);
  pstring_replace(&state->target->outbuff, state->target_outbuff.str,
                  state->target_outbuff.len);
  state->target->cache = state->cache;
  http_parser_init(&state->target->parser, HTTP_RESPONSE);
  state->target->parser.data = state->target;

  state->target->socket = socket(AF_INET, SOCK_STREAM, 0);
  if (state->target->socket < 0) {
    perror("Cannot create target socket");
    pstring_free(&state->target->outbuff);
    free(state->target);
    state->target = NULL;
    return false;
  }

#ifdef _PROXY_DEBUG
  fprintf(stderr, "Resolving %s...\n", hostname);
#endif

  if ((resolved = resolve_hostname(hostname)) == NULL) {
    fprintf(stderr, "Cannot resolve hostname: %s\n", hostname);
    close(state->target->socket);
    pstring_free(&state->target->outbuff);
    free(state->target);
    state->target = NULL;
    if (hostname != host)
      free(hostname);
    return false;
  }

#ifdef _PROXY_DEBUG
  fprintf(stderr, "Hostname %s resolved as %s\n", hostname,
          inet_ntoa(*((struct in_addr*)resolved)));
#endif

  addr.sin_addr.s_addr = *((in_addr_t*)resolved);
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (hostname != host)
    free(hostname);

  if (connect(state->target->socket, (struct sockaddr*)&addr, sizeof(addr)) ==
      -1) {
    perror("Cannot connect to target");
    close(state->target->socket);
    pstring_free(&state->target->outbuff);
    free(state->target);
    state->target = NULL;
    return false;
  }

#ifdef _PROXY_DEBUG
  fprintf(stderr, "Connection established with %s\n", hostname);
#endif

  if (!sockets_add_socket(state->target->socket, &target_handler,
                          state->target)) {
    fprintf(stderr, "Too many file descriptors\n");
    close(state->target->socket);
    pstring_free(&state->target->outbuff);
    free(state->target);
    state->target = NULL;
    return false;
  }

  sockets_enable_in_handle(state->target->socket);
  sockets_enable_out_handle(state->target->socket);
  return true;
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
