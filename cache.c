
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proxy-utils.h"

#include "cache.h"

static cache_t cache;

int cache_init(void) {
  cache.list = NULL;
  return pthread_mutex_init(&cache.global_lock, NULL);
}

int cache_find_or_create(char* url, cache_entry_t** result) {
  int error;

  if (url == NULL)
    return -1;

  error = pthread_mutex_lock(&cache.global_lock);
  if (error) {
    proxy_error(error, "Cannot lock global cache");
    return error;
  }

  cache_entry_t* entry = cache.list;
  cache_entry_t* prev = NULL;

  while (entry) {
    // Found invalid cache entry
    if (entry->invalid) {
      // If no readers, delete it
      if (entry->readers == NULL && entry->finished) {
        free(entry->url);
        pthread_rwlock_destroy(&entry->lock);
        pstring_free(&entry->data);
        if (prev == NULL)
          cache.list = entry->next;
        else
          prev->next = entry->next;
        free(entry);
        entry = prev == NULL ? cache.list : prev->next;
      } else {
        prev = entry;
        entry = entry->next;
      }
      continue;
    }

    if (!strcmp(url, entry->url)) {
      (*result) = entry;
      pthread_mutex_unlock(&cache.global_lock);
      return 0;
    }

    prev = entry;
    entry = entry->next;
  }

  entry = (cache_entry_t*)malloc(sizeof(cache_entry_t));
  if (entry == NULL) {
    perror("Cannot create cache entry");
    pthread_mutex_unlock(&cache.global_lock);
    return -1;
  }

  memset(entry, 0, sizeof(cache_entry_t));
  error = pthread_rwlock_init(&entry->lock, NULL);
  if (error) {
    proxy_error(error, "Cannot init rw lock for cache entry");
    free(entry);
    pthread_mutex_unlock(&cache.global_lock);
    return -1;
  }

  pstring_init(&entry->data);
  entry->url = strdup(url);
  if (entry->url == NULL) {
    perror("Cannot duplicate URL string for cache entry");
    pthread_rwlock_destroy(&entry->lock);
    free(entry);
    pthread_mutex_unlock(&cache.global_lock);
    return -1;
  }
  entry->next = cache.list;
  cache.list = entry;
  (*result) = entry;

  pthread_mutex_unlock(&cache.global_lock);
  return 1;
}

cache_entry_reader_t* cache_entry_subscribe(cache_entry_t* entry,
                                            void (*callback)(cache_entry_t*,
                                                             void*),
                                            void* arg) {
  int error;

  if (entry == NULL || callback == NULL)
    return NULL;

  cache_entry_reader_t* reader =
      (cache_entry_reader_t*)malloc(sizeof(cache_entry_reader_t));
  if (reader == NULL) {
    perror("Cannot subscribe cache entry reader");
    return NULL;
  }
  reader->callback = callback;
  reader->arg = arg;

  error = pthread_rwlock_wrlock(&entry->lock);
  if (error) {
    proxy_error(error, "Cannot subscribe cache entry reader");
    free(reader);
    return NULL;
  }

  reader->next = entry->readers;
  entry->readers = reader;

  pthread_rwlock_unlock(&entry->lock);

  return reader;
}

bool cache_entry_unsubscribe(cache_entry_t* entry,
                             cache_entry_reader_t* reader) {
  cache_entry_reader_t* curr;
  int error;

  if (entry == NULL || reader == NULL)
    return false;

  error = pthread_rwlock_wrlock(&entry->lock);
  if (error) {
    proxy_error(error, "Cannot unsubscribe cache entry reader");
    return false;
  }

  curr = entry->readers;
  if (entry->readers == reader) {
    entry->readers = entry->readers->next;
    free(curr);
    pthread_rwlock_unlock(&entry->lock);
    return true;
  }

  while (curr) {
    if (curr->next == reader) {
      curr->next = reader->next;
      free(reader);
      pthread_rwlock_unlock(&entry->lock);
      return true;
    }

    curr = curr->next;
  }

  pthread_rwlock_unlock(&entry->lock);
  return false;
}

ssize_t cache_entry_extract(cache_entry_t* entry,
                            size_t offset,
                            char* buffer,
                            size_t len) {
  int error;

  if (entry == NULL || buffer == NULL)
    return -1;

  error = pthread_rwlock_rdlock(&entry->lock);
  if (error) {
    proxy_error(error, "Cannot use read lock for cache entry");
    return -1;
  }

  if (entry->data.len < offset) {
    pthread_rwlock_unlock(&entry->lock);
    return -1;
  }

  size_t result_len = entry->data.len - offset;
  if (result_len > len)
    result_len = len;

  memcpy(buffer, entry->data.str + offset, result_len);

  pthread_rwlock_unlock(&entry->lock);
  return result_len;
}

static void readers_foreach(cache_entry_t* entry, size_t len) {
  cache_entry_reader_t* reader = entry->readers;
  cache_entry_reader_t* curr;
  while (reader) {
    // This order is because that in
    // the callback you can delete the reader
    curr = reader;
    reader = reader->next;
    curr->callback(entry, curr->arg);
  }
}

bool cache_entry_append(cache_entry_t* entry, const char* data, size_t len) {
  int error;

  if (entry == NULL || data == NULL)
    return false;

  error = pthread_rwlock_wrlock(&entry->lock);
  if (error) {
    proxy_error(error, "Cannot lock cache entry in entry append");
    return false;
  }

  if (!pstring_append(&entry->data, data, len)) {
    perror("Cannot cache entry data");
    pthread_rwlock_unlock(&entry->lock);
    return false;
  }

  readers_foreach(entry, len);

  pthread_rwlock_unlock(&entry->lock);

  return true;
}

void cache_entry_mark_finished(cache_entry_t* entry) {
  int error;

  if (entry != NULL) {
    entry->finished = true;

    error = pthread_rwlock_rdlock(&entry->lock);
    if (error) {
      proxy_error(error, "Cannot lock cache entry in entry mark finished");
      return;
    }

    readers_foreach(entry, 0);

    pthread_rwlock_unlock(&entry->lock);
  }
}

void cache_entry_mark_invalid(cache_entry_t* entry) {
  if (entry != NULL)
    entry->invalid = true;
}

void cache_entry_mark_invalid_and_finished(cache_entry_t* entry) {
  int error;

  if (entry != NULL) {
    entry->invalid = true;
    entry->finished = true;

    error = pthread_rwlock_rdlock(&entry->lock);
    if (error) {
      proxy_error(error, "Cannot lock cache entry in entry mark finished");
      return;
    }

    readers_foreach(entry, 0);

    pthread_rwlock_unlock(&entry->lock);
  }
}

static void readers_free(cache_entry_reader_t* readers) {
  cache_entry_reader_t* curr = readers;
  while (curr) {
    readers = curr->next;
    free(curr);
    curr = readers;
  }
}

void cache_free(void) {
  cache_entry_t* curr = cache.list;
  while (curr) {
    pthread_rwlock_destroy(&curr->lock);
    readers_free(curr->readers);
    free(curr->url);
    pstring_free(&curr->data);

    cache.list = curr->next;
    free(curr);
    curr = cache.list;
  }

  pthread_mutex_destroy(&cache.global_lock);
}
