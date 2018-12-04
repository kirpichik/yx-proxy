
#include <stdarg.h>
#include <string.h>

#include "proxy-utils.h"

#define BUFFER_SIZE 4096

static void vproxy_error(int err, const char* format, va_list args) {
  char buffer[BUFFER_SIZE + 1];
  int size = vsnprintf(buffer, BUFFER_SIZE, format, args);
  buffer[size] = '\0';
  if (err) {
    fprintf(stderr, "%s: %s\n", buffer, strerror(err));
  } else {
    fprintf(stderr, "%s\n", buffer);
  }
}

void proxy_error(int err, const char* format, ...) {
  va_list args;
  va_start(args, format);
  vproxy_error(err, format, args);
  va_end(args);
}

void proxy_log(const char* format, ...) {
#ifdef _PROXY_DEBUG
  va_list args;
  va_start(args, format);
  vproxy_error(0, format, args);
  va_end(args);
#endif
}
