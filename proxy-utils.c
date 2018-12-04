
#include <stdarg.h>
#include <string.h>

#include "proxy-utils.h"

void proxy_error(int err, const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  if (err) {
    fprintf(stderr, ": %s\n", strerror(err));
  } else {
    fprintf(stderr, "\n");
  }
}

void proxy_log(const char* format, ...) {
  va_list args;
  va_start(args, format);
#ifdef _PROXY_DEBUG
  vfprintf(stderr, format, args);
  va_end(args);
  fprintf(stderr, "\n");
#else
  va_end(args);
#endif
}
