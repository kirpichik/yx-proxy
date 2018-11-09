
#include <stdbool.h>
#include <stddef.h>

#ifndef _CACHE_H
#define _CACHE_H

typedef struct cache_entry_reader {
  void (*callback)(char* data, size_t len);
  void* data;
  struct cache_entry_reader* next;
} cache_entry_reader_t;

typedef struct cache_entry {
  char* url;
  bool finished;
  size_t len;
  char* data;
  cache_entry_reader_t* readers;
  struct cache_entry* next;
} cache_entry_t;

typedef struct cache {
  cache_entry_t* list;
} cache_t;

bool cache_init(cache_t* cache);

int cache_find_or_create(cache_t* cache, char* url, cache_entry_t** entry);

void cache_free(cache_t* cache);

#endif
