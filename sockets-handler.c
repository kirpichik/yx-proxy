
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "proxy-handler.h"

#include "sockets-handler.h"

#define POLL_PRE_SIZE 50
#define POLL_GROW_SPEED 2

typedef struct callback {
  void (*callback)(int, int, void*);
  void* arg;
} callback_t;

typedef struct sockets_state {
  size_t polls_count;
  size_t size;
  struct pollfd* polls;
  callback_t* callbacks;
} sockets_state_t;

/**
 * Removes socket at required position.
 */
static void remove_socket_at(size_t);

/**
 * Global sockets state.
 */
static sockets_state_t state;

void sockets_destroy() {
  close(state.polls[0].fd);

  while (state.polls_count-- > 1) {
    callback_t* cb = &state.callbacks[1];
    if (cb->callback == NULL)  // For server socket
      close(state.polls[1].fd);
    else
      cb->callback(state.polls[1].fd, POLLHUP, cb->arg);
  }

  free(state.polls);
  free(state.callbacks);
}

/**
 * Initializes socket processing.
 *
 * @param server_socket Socket for receiving new clients.
 */
static void init_sockets_state(int server_socket) {
  state.size = POLL_PRE_SIZE;

  state.polls = (struct pollfd*)malloc(sizeof(struct pollfd) * state.size);
  state.callbacks = (callback_t*)malloc(sizeof(callback_t) * state.size);
  memset(state.polls, 0, sizeof(struct pollfd) * state.size);
  memset(state.callbacks, 0, sizeof(callback_t) * state.size);

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
  listen(server_socket, POLL_PRE_SIZE);

  while (1) {
    if ((count = poll(state.polls, (nfds_t)state.polls_count, -1)) == -1) {
      if (errno == EINTR)
        continue;
      break;
    }
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
#ifdef _PROXY_DEBUG
          fprintf(stderr, "Accept new client socket.\n");
#endif
          proxy_accept_client(socket);
        } else {
          fprintf(stderr, "Cannot accept new clients\n");
          close(server_socket);
          return 1;
        }
        continue;
      }

      callback_t* cb = &state.callbacks[i];
      cb->callback(state.polls[i].fd, revents, cb->arg);
    }
  }

  perror("Cannot use poll");
  return -1;
}

bool sockets_add_socket(int socket,
                        void (*callback)(int, int, void*),
                        void* arg) {
  if (state.polls_count + 1 >= state.size) {
    struct pollfd* temp_polls =
        (struct pollfd*)realloc(state.polls, size * sizeof(struct pollfd));
    if (temp_polls == NULL) {
      perror("Cannot increase sockets poll size");
      return false;
    }
    state.polls = temp_polls;

    callback_t* temp_callbacks =
        (callback_t*)realloc(state.callbacks, size * sizeof(callback_t));
    if (temp_callbacks == NULL) {
      perror("Cannot increase sockets callbacks size");
      return false;
    }
    state.callbacks = temp_callbacks;

    memset(state.polls + state.size, 0, size - state.size);
    memset(state.callbacks + state.size, 0, size - state.size);
    state.size = size;
  }

  state.polls[state.polls_count].fd = socket;
  state.polls[state.polls_count].events = 0;
  state.callbacks[state.polls_count].callback = callback;
  state.callbacks[state.polls_count].arg = arg;
  state.polls_count++;

  return true;
}

/**
 * Looking for a socket in the registered list.
 *
 * @param socket Required socket.
 *
 * @return Required socket position or {@code -1}.
 */
static ssize_t find_socket(int socket) {
  for (size_t i = 0; i < state.polls_count; i++)
    if (socket == state.polls[i].fd)
      return i;
  return -1;
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

  state.polls[pos].events &= ~POLLOUT;

  return true;
}

static void remove_socket_at(size_t pos) {
  state.polls_count--;

  memcpy(&state.polls[pos], &state.polls[state.polls_count],
         sizeof(struct pollfd));
  memset(&state.polls[state.polls_count], 0, sizeof(struct pollfd));

  memcpy(&state.callbacks[pos], &state.callbacks[state.polls_count],
         sizeof(callback_t));
  memset(&state.callbacks[state.polls_count], 0, sizeof(callback_t));
}

bool sockets_remove_socket(int socket) {
  ssize_t pos = find_socket(socket);
  if (pos == -1)
    return false;

  remove_socket_at(pos);
  close(socket);

  return true;
}
