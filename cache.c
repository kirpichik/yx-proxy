
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"

static cache_t cache;

bool cache_init(void) {
  // TODO - init for multithreading
  cache.list = NULL;
  return true;
}

int cache_find_or_create(char* url, cache_entry_t** result) {
  if (url == NULL)
    return -1;
  cache_entry_t* entry = cache.list;
  cache_entry_t* prev = NULL;

  while (entry) {
    // Found invalid cache entry
    if (entry->invalid) {
      // If no readers, delete it
      if (entry->readers == NULL && entry->finished) {
        free(entry->url);
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
      return 0;
    }

    prev = entry;
    entry = entry->next;
  }

  entry = (cache_entry_t*)malloc(sizeof(cache_entry_t));
  if (entry == NULL) {
    perror("Cannot create cache entry");
    return -1;
  }

  memset(entry, 0, sizeof(cache_entry_t));
  pstring_init(&entry->data);
  entry->url = strdup(url);
  if (entry->url == NULL) {
    free(entry);
    return false;
  }
  entry->next = cache.list;
  cache.list = entry;
  (*result) = entry;

  return 1;
}

cache_entry_reader_t* cache_entry_subscribe(cache_entry_t* entry,
                                            void (*callback)(cache_entry_t*,
                                                             void*),
                                            void* arg) {
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
  reader->next = entry->readers;
  entry->readers = reader;

  callback(entry, arg);

  return reader;
}

bool cache_entry_unsubscribe(cache_entry_t* entry,
                             cache_entry_reader_t* reader) {
  cache_entry_reader_t* curr;
  if (entry == NULL || reader == NULL)
    return false;

  curr = entry->readers;
  if (entry->readers == reader) {
    entry->readers = entry->readers->next;
    free(curr);
    return true;
  }

  while (curr) {
    if (curr->next == reader) {
      curr->next = reader->next;
      free(reader);
      return true;
    }

    curr = curr->next;
  }

  return false;
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

ssize_t cache_entry_extract(cache_entry_t* entry,
                            size_t offset,
                            char* buffer,
                            size_t len) {
  if (entry == NULL || buffer == NULL)
    return -1;

  if (entry->data.len < offset)
    return -1;

  size_t result_len = entry->data.len - offset;
  if (result_len > len)
    result_len = len;

  memcpy(buffer, entry->data.str + offset, result_len);
  return result_len;
}

bool cache_entry_append(cache_entry_t* entry, const char* data, size_t len) {
  if (entry == NULL || data == NULL)
    return false;

  if (!pstring_append(&entry->data, data, len)) {
    perror("Cannot cache entry data");
    return false;
  }

  readers_foreach(entry, len);

  return true;
}

void cache_entry_mark_finished(cache_entry_t* entry) {
  if (entry != NULL) {
    entry->finished = true;
    readers_foreach(entry, 0);
  }
}

void cache_entry_mark_invalid(cache_entry_t* entry) {
  if (entry != NULL)
    entry->invalid = true;
}

void cache_entry_mark_invalid_and_finished(cache_entry_t* entry) {
  if (entry != NULL) {
    entry->finished = true;
    entry->invalid = true;
    readers_foreach(entry, 0);
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
    readers_free(curr->readers);
    free(curr->url);
    pstring_free(&curr->data);

    cache.list = curr->next;
    free(curr);
    curr = cache.list;
  }
}
