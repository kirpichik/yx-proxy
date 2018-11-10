
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

header_entry_t* create_header_entry(const char* key, size_t key_len) {
  header_entry_t* entry = (header_entry_t*)malloc(sizeof(header_entry_t));
  if (entry == NULL) {
    perror("Cannot create header entry");
    return NULL;
  }

  entry->next = NULL;
  pstring_init(&entry->key);
  pstring_init(&entry->value);
  if (!pstring_append(&entry->key, key, key_len)) {
    perror("Cannot store header key");
    free(entry);
    return NULL;
  }

  return entry;
}

void free_header_entry_list(header_entry_t* entry) {
  header_entry_t* temp;

  while (entry) {
    pstring_free(&entry->key);
    pstring_free(&entry->value);

    temp = entry->next;
    free(entry);
    entry = temp;
  }
}

void proxy_accept_client(int socket) {
  client_state_t* state = (client_state_t*)malloc(sizeof(client_state_t));
  if (state == NULL) {
    perror("Cannot allocate state for connection");
    close(socket);
    return;
  }
  http_parser_init(&state->parser, HTTP_REQUEST);

  state->socket = socket;
  state->headers = NULL;
  state->target = NULL;
  pstring_init(&state->outbuff);
  pstring_init(&state->url);

  state->parser.data = state;

  if (!sockets_add_socket(socket)) {
    fprintf(stderr, "Too many file descriptors.\n");
    free(state);
    close(socket);
    return;
  }

  sockets_set_hup_handler(socket, &client_hup_handler, state);
  sockets_set_in_handler(socket, &client_input_handler, &state->parser);
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
    port = atoi(split_pos + (-strlen(host)));
    hostname = (char*)malloc((size_t)(split_pos - host + 1));
    memcpy(hostname, host, (size_t)(split_pos - host));
    hostname[split_pos - host] = '\0';
  }

  state->target = (target_state_t*)malloc(sizeof(target_state_t));
  if (state->target == NULL) {
    perror("Cannot allocate target state");
    return false;
  }

  state->target->client = state;
  pstring_init(&state->target->outbuff);
  state->target->headers = NULL;
  state->target->proxy_finished = false;

  state->target->socket = socket(AF_INET, SOCK_STREAM, 0);
  if (state->target->socket < 0) {
    perror("Cannot create target socket");
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
    free(state->target);
    state->target = NULL;
    return false;
  }

#ifdef _PROXY_DEBUG
  fprintf(stderr, "Connection established with %s\n", hostname);
#endif

  http_parser_init(&state->target->parser, HTTP_RESPONSE);
  state->target->parser.data = state->target;

  if (!sockets_add_socket(state->target->socket)) {
    fprintf(stderr, "Too many file descriptors\n");
    close(state->target->socket);
    free(state->target);
    state->target = NULL;
    return false;
  }

  sockets_set_hup_handler(state->target->socket, &target_hup_handler,
                          state->target);
  sockets_set_in_handler(state->target->socket, &target_input_handler,
                         &state->target->parser);

  return true;
}

bool send_pstring(int socket, pstring_t* buff) {
  size_t offset = 0;
  ssize_t result;

  while ((result = send(socket, buff->str + offset, buff->len - offset, 0)) !=
         buff->len - offset) {
    if (result == -1) {
      if (errno != EWOULDBLOCK)
        perror("Cannot send data to socket");
      pstring_substring(buff, offset);
      return false;
    }

    offset += result;
  }

  free(buff->str);
  pstring_init(buff);

  return true;
}

char* build_header_string(header_entry_t* entry, size_t* result_len) {
  size_t len = entry->key.len + entry->value.len + 4;  // 4 is ": " and "\r\n"
  char* output = (char*)malloc(len);
  if (output == NULL)
    return NULL;

  memcpy(output, entry->key.str, entry->key.len);
  output[entry->key.len] = ':';
  output[entry->key.len + 1] = ' ';
  memcpy(output + entry->key.len + 2, entry->value.str, entry->value.len);
  output[len - 2] = '\r';
  output[len - 1] = '\n';

  (*result_len) = len;
  return output;
}
