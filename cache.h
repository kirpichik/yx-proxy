
#include <stdbool.h>
#include <stddef.h>

#include "pstring.h"

#ifndef _CACHE_H
#define _CACHE_H

struct cache_entry;

typedef struct cache_entry_reader {
  void (*callback)(struct cache_entry*, void* state);
  void* arg;
  struct cache_entry_reader* next;
} cache_entry_reader_t;

typedef struct cache_entry {
  char* url;
  bool finished;
  size_t offset;
  pstring_t data;
  cache_entry_reader_t* readers;
  struct cache_entry* next;
} cache_entry_t;

typedef struct cache {
  cache_entry_t* list;
} cache_t;

bool cache_init(void);

int cache_find_or_create(char* url, cache_entry_t** entry);

cache_entry_reader_t* cache_entry_subscribe(cache_entry_t* entry,
                                            void (*callback)(cache_entry_t*,
                                                             void*),
                                            void* arg);

bool cache_entry_unsubscribe(cache_entry_t* entry,
                             cache_entry_reader_t* reader);

bool cache_entry_append(cache_entry_t* entry, char* data, size_t len);

void cache_entry_mark_finished(cache_entry_t* entry);

void cache_entry_drop(cache_entry_t* entry);

void cache_free(void);

#endif
