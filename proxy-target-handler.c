
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <unistd.h>

#include "proxy-handler.h"
#include "sockets-handler.h"

#include "proxy-target-handler.h"

#define BUFFER_SIZE 1024
#define HTTP_VERSION_PREFIX_LEN (sizeof("HTTP/1.0 ") - 1)

static bool parse_response_code(target_state_t* state, char* buff, size_t len) {
  // If code already parsed
  if (state->code >= 100)
    return true;
  
  state->code = 200;
  
  // TODO - parse code and mark cache invalid if not 200
  
  return true;
}

static bool target_input_handler(target_state_t* state) {
  char buff[BUFFER_SIZE];
  ssize_t result;

  while (1) {
    result = recv(state->socket, buff, BUFFER_SIZE, 0);

    if (result == -1) {
      if (errno != EAGAIN) {
        perror("Cannot recv data from target");
        return false;
      }
      return true;
    } else if (result == 0)
      return true;

    if (!parse_response_code(state, buff, result)) {
      fprintf(stderr, "Cannot parse response code\n");
      return false;
    }
    
    if (!cache_entry_append(state->cache, buff, result)) {
      fprintf(stderr, "Cannot store target data to cache\n");
      return false;
    }
  }
}

static void target_cleanup(target_state_t* state) {
  sockets_remove_socket(state->socket);
  pstring_free(&state->outbuff);
  free(state);
}

void target_handler(int socket, int events, void* arg) {
  target_state_t* state = (target_state_t*)arg;

  // Handle output
  if (events & POLLOUT) {
    if (send_pstring(socket, &state->outbuff))
      sockets_cancel_out_handle(state->socket);
  }

  // Handle input
  if (events & (POLLIN | POLLPRI)) {
    if (!target_input_handler(state)) {
      // If parse/receive error, mark invalid and finish
      cache_entry_mark_invalid_and_finished(state->cache);
      target_cleanup(state);
      return;
    }
  }

  // Handle hup
  if (events & POLLHUP) {
    // If not OK code, mark entry as invalid
    if (state->code != 200)
      cache_entry_mark_invalid_and_finished(state->cache);
    else
      cache_entry_mark_finished(state->cache);
    target_cleanup(state);
  }
}
