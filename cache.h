
#include <stdbool.h>
#include <stddef.h>

#include "pstring.h"

#ifndef _CACHE_H
#define _CACHE_H

struct cache_entry;

typedef struct cache_entry_reader {
  void (*callback)(struct cache_entry*, size_t, void*);
  void* arg;
  size_t offset; // TODO - useless?
  struct cache_entry_reader* next;
} cache_entry_reader_t;

typedef struct cache_entry {
  char* url;
  bool finished;
  bool invalid;
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
                                                             size_t,
                                                             void*),
                                            void* arg);

bool cache_entry_unsubscribe(cache_entry_t* entry,
                             cache_entry_reader_t* reader);

ssize_t cache_entry_extract(cache_entry_t* entry, size_t offset, char* buffer, size_t len);

bool cache_entry_append(cache_entry_t* entry, const char* data, size_t len);

void cache_entry_mark_finished(cache_entry_t* entry);

void cache_entry_mark_invalid(cache_entry_t* entry);

void cache_entry_mark_invalid_and_finished(cache_entry_t* entry);

void cache_entry_drop(cache_entry_t* entry);

void cache_free(void);

#endif
