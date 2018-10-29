
#include <sys/poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "proxy-handler.h"

#include "sockets-handler.h"

#define POLL_SIZE 50

typedef struct callback {
  void (*callback) (int, void*);
  void* arg;
} callback_t;

typedef struct sockets_state {
  size_t polls_count;
  struct pollfd polls[POLL_SIZE];
  callback_t input_callbacks[POLL_SIZE];
} sockets_state_t;

static sockets_state_t state;

static void init_sockets_state(int server_socket) {
  // Not required if state is global, but still do it for the future.
  memset(state.polls, 0, sizeof(state.polls));
  memset(state.input_callbacks, 0, sizeof(state.input_callbacks));

  state.polls_count = 1;
  state.polls[0].fd = server_socket;
  state.polls[0].events = POLLIN | POLLPRI;
}

int sockets_poll_loop(int server_socket) {
  int count;
  int revents;
  int socket;
  init_sockets_state(server_socket);

  fcntl(server_socket, F_SETFL, O_NONBLOCK);
  listen(server_socket, POLL_SIZE);

  while ((count = poll(state.polls, state.polls_count, -1)) != -1) {
    for (size_t i = 0; i < state.polls_count; i++) {
      if (count == 0)
        break;

      revents = state.polls[i].revents;
      state.polls[i].revents = 0;

      // Skip sockets unchanged
      if (revents == 0)
        continue;
      count--;

      // Check socket state
      if (revents & ~POLLPRI && revents & ~POLLIN) {
        sockets_remove_socket(state.polls[i].fd);
        close(state.polls[i].fd);
        continue;
      }

      if (i == 0) { // New client
        socket = accept(server_socket, NULL, NULL);
        fcntl(socket, F_SETFL, O_NONBLOCK);
        proxy_accept_client(socket);
      } else { // Handle client
        callback_t* cb = &state.input_callbacks[i];
        cb->callback(state.polls[i].fd, cb->arg);
      }
    }
  }

  return -1;
}

bool sockets_add_socket(int socket, void (*callback)(int, void*), void* arg) {
  if (state.polls_count >= POLL_SIZE)
    return false;

  state.polls[state.polls_count].fd = socket;
  state.polls[state.polls_count].events = POLLIN | POLLPRI;
  state.input_callbacks[state.polls_count].callback = callback;
  state.input_callbacks[state.polls_count].arg = arg;
  state.polls_count++;

  return true;
}

bool sockets_remove_socket(int socket) {
  for (size_t i = 0; i < state.polls_count; i++) {
    if (state.polls[i].fd == socket) {
      state.polls_count--;

      memcpy(&state.polls[i], &state.polls[state.polls_count], sizeof(struct pollfd));
      memset(&state.polls[state.polls_count], 0, sizeof(struct pollfd));

      memcpy(&state.input_callbacks[i], &state.input_callbacks[state.polls_count], sizeof(callback_t));
      memset(&state.input_callbacks[state.polls_count], 0, sizeof(callback_t));

      return true;
    }
  }

  return false;
}
