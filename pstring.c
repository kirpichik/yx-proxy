
#include <stdlib.h>
#include <string.h>

#include "pstring.h"

void pstring_init(pstring_t* str) {
  if (str == NULL)
    return;

  str->len = 0;
  str->str = NULL;
}

bool pstring_append(pstring_t* str, const char* buff, size_t len) {
  if (str == NULL || buff == NULL)
    return false;

  size_t size = len + (str->str ? 0 : 1);
  str->str = (char*) realloc(str->str, str->len + size);

  if (str->str == NULL)
    return false;

  memcpy(str->str + str->len, buff, len);
  str->len += size;

  return true;
}

bool pstring_replace(pstring_t* str, const char* buff, size_t len) {
  if (str == NULL || buff == NULL)
    return false;

  if (str->str != NULL)
    free(str->str);

  pstring_init(str);
  return pstring_append(str, buff, len);
}

void pstring_finalize(pstring_t* str) {
  if (str == NULL || str->str == NULL)
    return;
  str->str[str->len] = '\0';
}

