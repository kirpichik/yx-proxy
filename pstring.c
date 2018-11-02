
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
  str->str = (char*)realloc(str->str, str->len + size);

  if (str->str == NULL)
    return false;

  memcpy(str->str + str->len, buff, len);
  str->len += len;

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

bool pstring_substring(pstring_t* str, size_t begin) {
  if (str == NULL || str->str == NULL || str->len <= begin)
    return false;

  char* buff = (char*)malloc(str->len - begin);
  if (buff == NULL)
    return false;

  memcpy(buff, str->str + begin, str->len - begin);
  free(str->str);
  str->str = buff;
  str->len -= begin;

  return true;
}

void pstring_finalize(pstring_t* str) {
  if (str == NULL || str->str == NULL)
    return;
  str->str[str->len] = '\0';
}
