
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "sockets-handler.h"

#include "proxy-handler.h"

#define BUFFER_SIZE 1024

static void input_handler(int socket, void* arg) {
  char buff[BUFFER_SIZE];
  int result;

  while (1) {
    result = recv(socket, buff, BUFFER_SIZE, 0);

    if (result == -1) {
      if (errno != EAGAIN)
        perror("Cannot recv data");
      return;
    }

    write(STDOUT_FILENO, buff, result);
  }
}

void proxy_accept_client(int socket) {
  sockets_add_socket(socket, &input_handler, NULL);
}
