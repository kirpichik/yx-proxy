
#include <sys/poll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

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
  callback_t output_callbacks[POLL_SIZE];
} sockets_state_t;

static void remove_socket_at(size_t);

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

      // Handle server socket
      if (i == 0) {
        if (revents & POLLPRI || revents & POLLIN) {
          socket = accept(server_socket, NULL, NULL);
          fcntl(socket, F_SETFL, O_NONBLOCK);
          proxy_accept_client(socket);
        } else {
          fprintf(stderr, "Cannot accept new clients\n");
          close(server_socket);
          return 1;
        }
        continue;
      }

      // Handle other sockets

      if (revents & POLLPRI || revents & POLLIN) {
        callback_t* cb = &state.input_callbacks[i];
        cb->callback(state.polls[i].fd, cb->arg);
      }

      if (revents & POLLOUT) {
        callback_t* cb = &state.output_callbacks[i];
        cb->callback(state.polls[i].fd, cb->arg);
      }

      if (revents & POLLHUP || revents & POLLERR || revents & POLLNVAL) {
        fprintf(stderr, "Socket %d hup\n", state.polls[i].fd);
        remove_socket_at(i);
      }
    }
  }

  return -1;
}

bool sockets_add_socket(int socket) {
  if (state.polls_count >= POLL_SIZE)
    return false;

  state.polls[state.polls_count].fd = socket;
  state.polls[state.polls_count].events = 0;
  state.polls_count++;

  return true;
}

static ssize_t find_socket(int socket) {
  for (size_t i = 0; i < state.polls_count; i++)
    if (socket == state.polls[i].fd)
      return i;
  return -1;
}

bool sockets_set_in_handler(int socket, void (*callback)(int, void*), void* arg) {
  ssize_t pos = find_socket(socket);
  if (pos == -1)
    return false;

  state.input_callbacks[pos].callback = callback;
  state.input_callbacks[pos].arg = arg;
  state.polls[pos].events |= POLLIN | POLLPRI;

  return true;
}

bool sockets_set_out_handler(int socket, void (*callback)(int, void*), void* arg) {
  ssize_t pos = find_socket(socket);
  if (pos == -1)
    return false;

  state.output_callbacks[pos].callback = callback;
  state.output_callbacks[pos].arg = arg;
  state.polls[pos].events |= POLLOUT;

  return true;
}

bool sockets_enable_in_handle(int socket) {
  ssize_t pos = find_socket(socket);
  if (pos == -1)
    return false;

  state.polls[pos].events |= POLLIN | POLLPRI;

  return true;
}

bool sockets_enable_out_handle(int socket) {
  ssize_t pos = find_socket(socket);
  if (pos == -1)
    return false;

  state.polls[pos].events |= POLLOUT;

  return true;
}

bool sockets_cancel_in_handle(int socket) {
  ssize_t pos = find_socket(socket);
  if (pos == -1)
    return false;

  state.polls[pos].events &= ~(POLLIN | POLLPRI);

  return true;
}

bool sockets_cancel_out_handle(int socket) {
  ssize_t pos = find_socket(socket);
  if (pos == -1)
    return false;

  state.polls[pos].events &= ~(POLLIN | POLLPRI);

  return true;
}

static void remove_socket_at(size_t pos) {
  state.polls_count--;
  memcpy(&state.polls[pos], &state.polls[state.polls_count], sizeof(struct pollfd));
  memcpy(&state.input_callbacks[pos], &state.input_callbacks[state.polls_count], sizeof(callback_t));
  memcpy(&state.output_callbacks[pos], &state.output_callbacks[state.polls_count], sizeof(callback_t));
}

bool sockets_remove_socket(int socket) {
  ssize_t pos = find_socket(socket);
  if (pos == -1)
    return false;

  remove_socket_at(pos);

  return true;
}
