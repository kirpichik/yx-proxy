
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

#include "pstring.h"

#ifndef _CACHE_H
#define _CACHE_H

struct cache_entry;

typedef struct cache_entry_reader {
  void (*callback)(struct cache_entry*, void*);
  void* arg;
  struct cache_entry_reader* next;
} cache_entry_reader_t;

typedef struct cache_entry {
  char* url;
  volatile bool finished;
  volatile bool invalid;
  pstring_t data;
  cache_entry_reader_t* readers;
  pthread_rwlock_t lock;
  struct cache_entry* next;
} cache_entry_t;

typedef struct cache {
  pthread_mutex_t global_lock;
  cache_entry_t* list;
} cache_t;

/**
 * Init cache.
 *
 * @return {@code true} if success.
 */
bool cache_init(void);

/**
 * Finds stored cache entry or creates new, if not exists.
 *
 * @param url Entry name.
 * @param entry Returning entry.
 *
 * @return {@code 0} if entry found, {@code 1} if not found and
 * created and {@code -1} if error occured.
 */
int cache_find_or_create(char* url, cache_entry_t** entry);

/**
 * Create reader for entry.
 *
 * @param entry Target entry.
 * @param callback Callback for cache updates.
 * @param arg Argument for callback.
 *
 * @return Created reader or {@code NULL}.
 */
cache_entry_reader_t* cache_entry_subscribe(cache_entry_t* entry,
                                            void (*callback)(cache_entry_t*,
                                                             void*),
                                            void* arg);

/**
 * Unsubscribes reader associated with cache entry.
 *
 * @param entry Target entry.
 * @param reader Target reader.
 *
 * @return {@code true} if success.
 */
bool cache_entry_unsubscribe(cache_entry_t* entry,
                             cache_entry_reader_t* reader);

/**
 * Extracts data part from cache entry.
 *
 * @param entry Target entry.
 * @param offset Offset from entry data beginning.
 * @param buffer Storage buffer.
 * @param len Storage buffer length.
 *
 * @return Amount of bytes stored to buffer or {@code -1} if error occured.
 */
ssize_t cache_entry_extract(cache_entry_t* entry,
                            size_t offset,
                            char* buffer,
                            size_t len);

/**
 * Appends new data to cache entry and notify all subscribers.
 *
 * @param entry Target entry.
 * @param data Appending data.
 * @param len Length of appending data.
 *
 * @return {@code true} if success.
 */
bool cache_entry_append(cache_entry_t* entry, const char* data, size_t len);

/**
 * Marks cache entry as successfully finished and notify all subscribers.
 *
 * @param entry Target entry.
 */
void cache_entry_mark_finished(cache_entry_t* entry);

/**
 * Marks cache entry as invalid.
 * If entry marked as invalid, new readers will not connect to this entry.
 *
 * @param entry Target entry.
 */
void cache_entry_mark_invalid(cache_entry_t* entry);

/**
 * Marks cache entry as finished and invalid and notify all subscibers.
 * If entry marked as invalid, new readers will not connect to this entry.
 *
 * @param entry Target entry.
 */
void cache_entry_mark_invalid_and_finished(cache_entry_t* entry);

/**
 * Cleanup all cache entries.
 */
void cache_free(void);

#endif
