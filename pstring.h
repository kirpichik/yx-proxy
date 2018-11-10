
#include <stdbool.h>
#include <stdint.h>

#ifndef _PSTRING_H
#define _PSTRING_H

typedef struct pstring {
  size_t len;
  char* str;
} pstring_t;

/**
 * Initializes required string.
 *
 * @param str Required string.
 */
void pstring_init(pstring_t* str);

/**
 * Adds additional data to the end of string.
 *
 * @param str Required string.
 * @param buff Source buffer.
 * @param len Count of bytes required to copy.
 *
 * @return {@code false} if not enougth memory.
 */
bool pstring_append(pstring_t* str, const char* buff, size_t len);

/**
 * Replace string data to the new data from buffer.
 *
 * @param str Required string.
 * @param buff Source buffer.
 * @param len Count of bytes required to copy.
 *
 * @return {@code false} if not enougth memory.
 */
bool pstring_replace(pstring_t* str, const char* buff, size_t len);

/**
 * Extract substring of required string.
 *
 * @param str Required string.
 * @param begin Begin position for substring.
 *
 * @return {@code false} if not enougth memory.
 */
bool pstring_substring(pstring_t* str, size_t begin);

/**
 * Inserts zero-ending byte to the end of string.
 *
 * @param str Required string.
 */
void pstring_finalize(pstring_t* str);

/**
 * Free memory for string if it required.
 *
 * @param str Required string.
 */
void pstring_free(pstring_t* str);

#endif
