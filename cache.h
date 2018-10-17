
#include <stdbool.h>

#ifndef _CACHE_H
#define _CACHE_H

typedef struct cache_entry_struct {
  struct cache_entry_struct* next;
  char* path;
  char* mime_type;
  char* data;
} cache_entry_t;

typedef struct cache_struct {

} cache_t;

bool cache_init(cache_t* cache);

bool cache_insert_entry(cache_t* cache, cache_entry_t* entry);

void cache_free(cache_t* cache);

bool cache_entry_init(cache_entry_t* entry, char* path, char* mime_type);

void cache_entry_free(cache_entry_t* entry);

#endif

