
#include <stdint.h>
#include <stdbool.h>

#ifndef _PSTRING_H
#define _PSTRING_H

typedef struct pstring {
  size_t len;
  char* str;
} pstring_t;

void pstring_init(pstring_t* str);

bool pstring_append(pstring_t* str, const char* buff, size_t len);

bool pstring_replace(pstring_t* str, const char* buff, size_t len);

bool pstring_substring(pstring_t* str, size_t begin);

void pstring_finalize(pstring_t* str);

#endif
