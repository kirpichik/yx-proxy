#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "cache.h"
#include "proxy-utils.h"
#include "sockets-handler.h"

static void interrupt_handler(int signal) {
  sockets_destroy();
  cache_free();
  printf("Server closed.\n");
  exit(0);
}

int main(int argc, char* argv[]) {
  struct sockaddr_in addr;
  int server_socket;
  int result;
  int port;

  if (argc != 2) {
    fprintf(stderr, "Usage: %s <listen-port>\n", argv[0]);
    return -1;
  }

  port = atoi(argv[1]);
  fprintf(stderr, "Binding server socket listener to %d...\n", port);

  server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket == -1) {
    perror("Cannot create socket");
    return -1;
  }

  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr))) {
    perror("Cannot bind server socket");
    return -1;
  }

  fprintf(stderr, "Server socket bound.\n");

  result = cache_init();
  if (result) {
    proxy_error(result, "Cannot init cache");
    return -1;
  }

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, &interrupt_handler);

  return sockets_poll_loop(server_socket);
}
