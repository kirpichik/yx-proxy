
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "proxy-handler.h"
#include "sockets-handler.h"

#include "proxy-target-handler.h"

#define BUFFER_SIZE 1024

static void client_output_handler(int socket, void* arg) {
  handler_state_t* state = (handler_state_t*)arg;

  if (send_pstring(socket, &state->client_outbuff))
    sockets_enable_in_handle(state->target_socket);
}

static bool send_to_client(handler_state_t* state,
                           const char* buff,
                           size_t len) {
  write(STDOUT_FILENO, buff, len);
  if (!pstring_append(&state->client_outbuff, buff, len))
    return false;
  // TODO - optimisation: try to send, before this
  sockets_set_out_handler(state->client_socket, &client_output_handler, state);
  sockets_cancel_in_handle(state->target_socket);
  return true;
}

void target_input_handler(int socket, void* arg) {
  handler_state_t* state = (handler_state_t*)arg;
  char buff[BUFFER_SIZE];
  ssize_t result;

  while (1) {
    result = recv(socket, buff, BUFFER_SIZE, 0);

    if (result == -1) {
      if (errno != EAGAIN) {
        perror("Cannot recv data from target");
        sockets_remove_socket(socket);
        close(socket);
      }
      return;
    } else if (result == 0)
      return;

    write(STDOUT_FILENO, buff, result);
    if (!send_to_client(state, buff, result)) {
      fprintf(stderr, "Cannot send data to client");
      return;
    }
  }
}
