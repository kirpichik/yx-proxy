
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "proxy-handler.h"
#include "proxy-utils.h"
#include "sockets-handler.h"

#define POLL_PRE_SIZE 50
#define POLL_GROW_SPEED 2
#define BUFFER_SIZE 128

typedef struct callback {
  void (*callback)(int, int, void*);
  void* arg;
} callback_t;

typedef struct sockets_state {
  int signal_pipe;
  pthread_mutex_t lock;
  size_t polls_count;
  size_t size;
  struct pollfd* polls;
  callback_t* callbacks;
  bool changed;
  size_t _polls_count_copy;
  size_t _size_copy;
  struct pollfd* _polls_copy;
  callback_t* _callbacks_copy;
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
  while (state.polls_count-- > 1) {
    callback_t* cb = &state.callbacks[state.polls_count];
    if (cb->callback == NULL)  // For server socket or signal pipe
      close(state.polls[state.polls_count].fd);
    else
      cb->callback(state.polls[state.polls_count].fd, POLLHUP, cb->arg);
  }

  while (state._polls_count_copy-- > 1) {
    callback_t* cb = &state._callbacks_copy[state._polls_count_copy];
    if (cb->callback == NULL)  // For server socket or signal pipe
      close(state._polls_copy[state._polls_count_copy].fd);
    else
      cb->callback(state._polls_copy[state._polls_count_copy].fd, POLLHUP,
                   cb->arg);
  }

  close(state.signal_pipe);
  free(state.polls);
  free(state._polls_copy);
  free(state.callbacks);
  free(state._callbacks_copy);
}

/**
 * Initializes socket processing.
 *
 * @param server_socket Socket for receiving new clients.
 */
static int init_sockets_state(int server_socket) {
  int error;
  int pipes[2];

  if (pipe(pipes)) {
    perror("Cannot create signal pipe");
    return errno;
  }
  state.signal_pipe = pipes[1];

  state.size = state._size_copy = POLL_PRE_SIZE;
  state.changed = false;

  state.polls = (struct pollfd*)malloc(sizeof(struct pollfd) * state.size);
  if (state.polls == NULL)
    return errno;
  state.callbacks = (callback_t*)malloc(sizeof(callback_t) * state.size);
  if (state.callbacks == NULL) {
    error = errno;
    free(state.polls);
    return error;
  }
  memset(state.polls, 0, sizeof(struct pollfd) * state.size);
  memset(state.callbacks, 0, sizeof(callback_t) * state.size);

  state._polls_copy =
      (struct pollfd*)malloc(sizeof(struct pollfd) * state.size);
  if (state._polls_copy == NULL) {
    error = errno;
    free(state.callbacks);
    free(state.polls);
    return error;
  }
  state._callbacks_copy = (callback_t*)malloc(sizeof(callback_t) * state.size);
  if (state._callbacks_copy == NULL) {
    error = errno;
    free(state._polls_copy);
    free(state.callbacks);
    free(state.polls);
    return error;
  }
  memset(state._polls_copy, 0, sizeof(struct pollfd) * state.size);
  memset(state._callbacks_copy, 0, sizeof(callback_t) * state.size);

  if ((error = pthread_mutex_init(&state.lock, NULL)) != 0) {
    free(state._callbacks_copy);
    free(state._polls_copy);
    free(state.callbacks);
    free(state.polls);
    return error;
  }

  state.polls_count = state._polls_count_copy = 2;
  state.polls[0].fd = state._polls_copy[0].fd = server_socket;
  state.polls[0].events = state._polls_copy[0].events = POLLIN | POLLPRI;
  state.polls[1].fd = state._polls_copy[1].fd = pipes[0];
  state.polls[1].events = state._polls_copy[1].events = POLLIN | POLLPRI;

  return 0;
}

/**
 * Prepares new dataset for poll execution.
 * Copy new sockets, flags and callbacks from polls and callbacks
 * state fields to _polls_copy and _callbacks_copy fields.
 *
 * @return {@code true} if success, or {@code false} and sets errno value.
 */
static bool copy_state() {
  if (!state.changed) {
    pthread_mutex_unlock(&state.lock);
    return true;
  }

  if (state._size_copy < state.size) {
    state._polls_copy = (struct pollfd*)realloc(
        state._polls_copy, sizeof(struct pollfd) * state.size);
    if (state._polls_copy == NULL)
      return false;
    state._callbacks_copy = (callback_t*)realloc(
        state._callbacks_copy, sizeof(callback_t) * state.size);
    if (state._callbacks_copy == NULL)
      return false;

    state._size_copy = state.size;
  }

  // Check if new data contains less sockets than old
  if (state._polls_count_copy > state.polls_count) {
    size_t diff = state._polls_count_copy - state.polls_count;
    memset(state._polls_copy + state.polls_count, 0,
           sizeof(struct pollfd) * diff);
    memset(state._callbacks_copy + state.polls_count, 0,
           sizeof(callback_t) * diff);
  }

  memcpy(state._polls_copy, state.polls,
         sizeof(struct pollfd) * state.polls_count);
  memcpy(state._callbacks_copy, state.callbacks,
         sizeof(callback_t) * state.polls_count);
  state._polls_count_copy = state.polls_count;

  state.changed = true;

  return true;
}

/**
 * Handles poll syscall result.
 *
 * @param count Amount of updated sockets.
 *
 * @return {@code true} if success.
 */
static bool handle_polls_update(size_t count) {
  int revents, socket;
  char buffer[BUFFER_SIZE];
  ssize_t result;

  for (size_t i = 0; i < state._polls_count_copy; i++) {
    if (count == 0)
      break;

    revents = state._polls_copy[i].revents;
    state._polls_copy[i].revents = 0;

    // Skip sockets unchanged
    if (revents == 0)
      continue;
    count--;

    // Handle server socket
    if (i == 0) {
      if (revents & POLLPRI || revents & POLLIN) {
        socket = accept(state._polls_copy[0].fd, NULL, NULL);
        fcntl(socket, F_SETFL, O_NONBLOCK);
        proxy_log("Accept new client socket: %d", socket);
        proxy_accept_client(socket);
      } else {
        fprintf(stderr, "Cannot accept new clients\n");
        close(state._polls_copy[0].fd);
        return false;
      }
      continue;
    }

    // Handle signal pipe
    if (i == 1) {
      if (revents & POLLPRI || revents & POLLIN) {
        result = read(state._polls_copy[1].fd, buffer, BUFFER_SIZE);
        if (result < 0) {
          perror("Cannot handle signal pipe");
          close(state._polls_copy[1].fd);
          return false;
        }
      } else {
        fprintf(stderr, "Cannot handle signal pipe\n");
        close(state._polls_copy[1].fd);
        return false;
      }
      continue;
    }

    callback_t* cb = &state._callbacks_copy[i];
    if (cb->arg == state.callbacks[i].arg)  // Skip moved connections
      cb->callback(state._polls_copy[i].fd, revents, cb->arg);
  }

  return true;
}

int sockets_poll_loop(int server_socket) {
  int count, error;

  error = init_sockets_state(server_socket);
  if (error) {
    proxy_error(error, "Cannot init sockets state");
    return -1;
  }

  fcntl(server_socket, F_SETFL, O_NONBLOCK);
  listen(server_socket, POLL_PRE_SIZE);

  while (1) {
    if ((count = poll(state._polls_copy, (nfds_t)state._polls_count_copy,
                      -1)) == -1) {
      if (errno == EINTR && copy_state())
        continue;
      break;
    }

    error = pthread_mutex_lock(&state.lock);
    if (error)
      break;

    if (!handle_polls_update(count)) {
      pthread_mutex_unlock(&state.lock);
      return 1;
    }

    if (!copy_state()) {
      pthread_mutex_unlock(&state.lock);
      break;
    }

    error = pthread_mutex_unlock(&state.lock);
    if (error)
      break;
  }

  proxy_error(error, "Cannot handle sockets");
  return -1;
}

#define LOCK_POLLS()                                   \
  {                                                    \
    int error = pthread_mutex_lock(&state.lock);       \
    if (error) {                                       \
      proxy_error(error, "Cannot lock sockets state"); \
      return false;                                    \
    }                                                  \
  }

#define UNLOCK_POLLS() pthread_mutex_unlock(&state.lock);

#define NOTIFY_HANDLER()                       \
  {                                            \
    if (write(state.signal_pipe, "", 1) < 0) { \
      perror("Cannot send signal to pipe");    \
      return false;                            \
    }                                          \
  }

bool sockets_add_socket(int socket,
                        void (*callback)(int, int, void*),
                        void* arg) {
  LOCK_POLLS();

  if (state.polls_count + 1 >= state.size) {
    size_t size = state.size * POLL_GROW_SPEED;
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

  state.changed = true;

  UNLOCK_POLLS();

  // Do not notify about new socket, because no events set.
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
  LOCK_POLLS();

  ssize_t pos = find_socket(socket);
  if (pos == -1) {
    UNLOCK_POLLS();
    return false;
  }

  state.polls[pos].events |= POLLIN | POLLPRI;

  state.changed = true;

  UNLOCK_POLLS();

  NOTIFY_HANDLER();
  return true;
}

bool sockets_enable_out_handle(int socket) {
  LOCK_POLLS();
  ssize_t pos = find_socket(socket);
  if (pos == -1) {
    UNLOCK_POLLS();
    return false;
  }

  state.polls[pos].events |= POLLOUT;

  state.changed = true;

  UNLOCK_POLLS();

  NOTIFY_HANDLER();
  return true;
}

bool sockets_enable_io_handle(int socket) {
  LOCK_POLLS();
  ssize_t pos = find_socket(socket);
  if (pos == -1) {
    UNLOCK_POLLS();
    return false;
  }

  state.polls[pos].events |= POLLOUT | POLLIN | POLLPRI;

  state.changed = true;

  UNLOCK_POLLS();

  NOTIFY_HANDLER();
  return true;
}

bool sockets_cancel_in_handle(int socket) {
  LOCK_POLLS();
  ssize_t pos = find_socket(socket);
  if (pos == -1) {
    UNLOCK_POLLS();
    return false;
  }

  state.polls[pos].events &= ~(POLLIN | POLLPRI);

  state.changed = true;

  UNLOCK_POLLS();

  NOTIFY_HANDLER();
  return true;
}

bool sockets_cancel_out_handle(int socket) {
  LOCK_POLLS();
  ssize_t pos = find_socket(socket);
  if (pos == -1) {
    UNLOCK_POLLS();
    return false;
  }

  state.polls[pos].events &= ~POLLOUT;

  state.changed = true;

  UNLOCK_POLLS();

  NOTIFY_HANDLER();
  return true;
}

bool sockets_cancel_io_handle(int socket) {
  LOCK_POLLS();
  ssize_t pos = find_socket(socket);
  if (pos == -1) {
    return false;
  }

  state.polls[pos].events &= ~(POLLOUT | POLLIN | POLLPRI);

  state.changed = true;

  UNLOCK_POLLS();

  NOTIFY_HANDLER();
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
  LOCK_POLLS();

  // Pre notify for closing sockets
  NOTIFY_HANDLER();

  ssize_t pos = find_socket(socket);
  if (pos == -1) {
    UNLOCK_POLLS();
    return false;
  }

  remove_socket_at(pos);

  state.changed = true;

  UNLOCK_POLLS();

  close(socket);

  return true;
}

#undef NOTIFY_HANDLER
#undef UNLOCK_POLLS
#undef LOCK_POLLS
