
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "proxy-handler.h"
#include "sockets-handler.h"

#include "proxy-target-handler.h"

#define BUFFER_SIZE 4096

/**
 * Handles target response initial line.
 */
static int handle_response_message_complete(http_parser* parser) {
  target_state_t* state = (target_state_t*)parser->data;
  state->message_complete = true;
  return 0;
}

static http_parser_settings http_response_callbacks = {
    NULL, /* on_message_begin */
    NULL, /* on_url */
    NULL, /* on_response_status */
    NULL, /* on_header_field */
    NULL, /* on_header_value */
    NULL, /* on_headers_complete */
    NULL, /* on_response_body */
    handle_response_message_complete,
    NULL, /* on_chunk_header */
    NULL  /* on_chunk_complete */
};

/**
 * Handles target input data.
 */
static int target_input_handler(target_state_t* state) {
  char buff[BUFFER_SIZE];
  ssize_t result;
  size_t nparsed;

  result = recv(state->socket, buff, BUFFER_SIZE, 0);

  if (result == -1) {
    if (errno != EAGAIN) {
      perror("Cannot recv data from target");
      return -1;
    }
    return 0;
  } else if (result == 0)
    return 1;

  if (state->parser.status_code == 0) {
    nparsed = http_parser_execute(&state->parser, &http_response_callbacks,
                                  buff, result);
    if (nparsed != result) {
      fprintf(stderr, "Cannot parse http input from target socket\n");
      return -1;
    }
  }

  if (!cache_entry_append(state->cache, buff, result)) {
    fprintf(stderr, "Cannot store target data to cache\n");
    return -1;
  }

  return 0;
}

/**
 * Cleanup all target data.
 */
static void target_cleanup(target_state_t* state) {
  pthread_mutex_unlock(&state->lock);
  sockets_remove_socket(state->socket);
  pstring_free(&state->outbuff);
  pthread_cond_destroy(&state->notifier);
  pthread_mutex_destroy(&state->lock);
  free(state);
}

static bool target_init(target_state_t* state) {
  // Block SIGUSR2
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGUSR2);
  pthread_sigmask(SIG_BLOCK, &signals, NULL);

  if (!sockets_add_socket(state->socket, &target_handler, state)) {
    target_cleanup(state);
    return false;
  }
  sockets_enable_io_handle(state->socket);

  if ((errno = pthread_mutex_lock(&state->lock)) != 0) {
    perror("Cannot lock target lock");
    target_cleanup(state);
    return false;
  }

  return true;
}

void* target_thread(void* arg) {
  target_state_t* state = (target_state_t*)arg;
  int result;
  int events;

  if (!target_init(state))
    return NULL;

  while (1) {
    if ((errno = pthread_cond_wait(&state->notifier, &state->lock))) {
      perror("Cannot wait target condition");
      target_cleanup(state);
      return NULL;
    }
    events = state->revents;

    // Handle output
    if (events & POLLOUT) {
      result = send_pstring(state->socket, &state->outbuff);
      if (result == -1) {
        target_cleanup(state);
        return NULL;
      } else if (result == 0)
        sockets_cancel_out_handle(state->socket);
    }

    // Handle input
    if (events & (POLLIN | POLLPRI)) {
      result = target_input_handler(state);
      if (result == -1) {
        // If parse/receive error, mark invalid and finish
        cache_entry_mark_invalid_and_finished(state->cache);
        target_cleanup(state);
        return NULL;
      } else if (result == 1 && !state->message_complete) {
        state->message_complete = true;
        continue;
      }
    }

    // Handle ending
    if (state->message_complete) {
      // If not OK code, mark entry as invalid
      if (state->parser.status_code != 200)
        cache_entry_mark_invalid_and_finished(state->cache);
      else
        cache_entry_mark_finished(state->cache);
      target_cleanup(state);
      return NULL;
    }
  }
}

void target_handler(int socket, int events, void* arg) {
  target_state_t* state = (target_state_t*)arg;

  state->revents = events;
  pthread_cond_signal(&state->notifier);
}
