/**
 * cupidc_runtime.c — Dynamic data structures for CupidC programs
 *
 * Implements Array and HashTable for CupidC shell and other programs.
 */

#include "cupidc_runtime.h"
#include "memory.h"
#include "string.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Dynamic Array
 * ══════════════════════════════════════════════════════════════════════ */

#define ARRAY_INITIAL_CAP 8

cc_array_t *cc_array_new(void) {
  cc_array_t *arr = kmalloc(sizeof(cc_array_t));
  if (!arr)
    return (cc_array_t *)0;

  arr->data = kmalloc(ARRAY_INITIAL_CAP * sizeof(void *));
  if (!arr->data) {
    kfree(arr);
    return (cc_array_t *)0;
  }

  arr->count = 0;
  arr->capacity = ARRAY_INITIAL_CAP;
  return arr;
}

void cc_array_push(cc_array_t *arr, void *item) {
  if (!arr)
    return;

  /* Grow if needed */
  if (arr->count >= arr->capacity) {
    int new_cap = arr->capacity * 2;
    void **new_data = kmalloc((uint32_t)new_cap * sizeof(void *));
    if (!new_data)
      return;
    memcpy(new_data, arr->data, (uint32_t)arr->count * sizeof(void *));
    kfree(arr->data);
    arr->data = new_data;
    arr->capacity = new_cap;
  }

  arr->data[arr->count] = item;
  arr->count++;
}

void *cc_array_get(cc_array_t *arr, int idx) {
  if (!arr || idx < 0 || idx >= arr->count)
    return (void *)0;
  return arr->data[idx];
}

void cc_array_set(cc_array_t *arr, int idx, void *item) {
  if (!arr || idx < 0 || idx >= arr->count)
    return;
  arr->data[idx] = item;
}

int cc_array_count(cc_array_t *arr) {
  if (!arr)
    return 0;
  return arr->count;
}

void cc_array_clear(cc_array_t *arr) {
  if (!arr)
    return;
  arr->count = 0;
}

void cc_array_free(cc_array_t *arr) {
  if (!arr)
    return;
  if (arr->data)
    kfree(arr->data);
  kfree(arr);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Hash Table (string -> string)
 * ══════════════════════════════════════════════════════════════════════ */

/* DJB2 hash function */
static uint32_t hash_djb2(const char *key, int bucket_count) {
  uint32_t hash = 5381;
  while (*key) {
    hash = ((hash << 5) + hash) + (uint32_t)(uint8_t)*key;
    key++;
  }
  return hash % (uint32_t)bucket_count;
}

cc_hash_t *cc_hash_new(int size) {
  if (size <= 0)
    size = 32;

  cc_hash_t *ht = kmalloc(sizeof(cc_hash_t));
  if (!ht)
    return (cc_hash_t *)0;

  ht->buckets = kmalloc((uint32_t)size * sizeof(cc_hash_entry_t *));
  if (!ht->buckets) {
    kfree(ht);
    return (cc_hash_t *)0;
  }

  memset(ht->buckets, 0, (uint32_t)size * sizeof(cc_hash_entry_t *));
  ht->bucket_count = size;
  ht->item_count = 0;
  return ht;
}

void cc_hash_set(cc_hash_t *ht, const char *key, const char *value) {
  if (!ht || !key)
    return;

  uint32_t idx = hash_djb2(key, ht->bucket_count);

  /* Check if key already exists */
  cc_hash_entry_t *entry = ht->buckets[idx];
  while (entry) {
    if (strcmp(entry->key, key) == 0) {
      /* Update existing value */
      kfree(entry->value);
      size_t vlen = strlen(value);
      entry->value = kmalloc(vlen + 1);
      if (entry->value)
        memcpy(entry->value, value, vlen + 1);
      return;
    }
    entry = entry->next;
  }

  /* Add new entry */
  cc_hash_entry_t *new_entry = kmalloc(sizeof(cc_hash_entry_t));
  if (!new_entry)
    return;

  size_t klen = strlen(key);
  size_t vlen = value ? strlen(value) : 0;

  new_entry->key = kmalloc(klen + 1);
  new_entry->value = kmalloc(vlen + 1);

  if (!new_entry->key || !new_entry->value) {
    if (new_entry->key)
      kfree(new_entry->key);
    if (new_entry->value)
      kfree(new_entry->value);
    kfree(new_entry);
    return;
  }

  memcpy(new_entry->key, key, klen + 1);
  if (value)
    memcpy(new_entry->value, value, vlen + 1);
  else
    new_entry->value[0] = '\0';

  new_entry->next = ht->buckets[idx];
  ht->buckets[idx] = new_entry;
  ht->item_count++;
}

const char *cc_hash_get(cc_hash_t *ht, const char *key) {
  if (!ht || !key)
    return (const char *)0;

  uint32_t idx = hash_djb2(key, ht->bucket_count);
  cc_hash_entry_t *entry = ht->buckets[idx];

  while (entry) {
    if (strcmp(entry->key, key) == 0)
      return entry->value;
    entry = entry->next;
  }
  return (const char *)0;
}

void cc_hash_delete(cc_hash_t *ht, const char *key) {
  if (!ht || !key)
    return;

  uint32_t idx = hash_djb2(key, ht->bucket_count);
  cc_hash_entry_t *entry = ht->buckets[idx];
  cc_hash_entry_t *prev = (cc_hash_entry_t *)0;

  while (entry) {
    if (strcmp(entry->key, key) == 0) {
      if (prev)
        prev->next = entry->next;
      else
        ht->buckets[idx] = entry->next;

      kfree(entry->key);
      kfree(entry->value);
      kfree(entry);
      ht->item_count--;
      return;
    }
    prev = entry;
    entry = entry->next;
  }
}

int cc_hash_exists(cc_hash_t *ht, const char *key) {
  return cc_hash_get(ht, key) != (const char *)0 ? 1 : 0;
}

int cc_hash_count(cc_hash_t *ht) {
  if (!ht)
    return 0;
  return ht->item_count;
}

void cc_hash_foreach(cc_hash_t *ht,
                     void (*fn)(const char *, const char *)) {
  if (!ht || !fn)
    return;

  for (int i = 0; i < ht->bucket_count; i++) {
    cc_hash_entry_t *entry = ht->buckets[i];
    while (entry) {
      fn(entry->key, entry->value);
      entry = entry->next;
    }
  }
}

char **cc_hash_keys(cc_hash_t *ht, int *out_count) {
  if (!ht || !out_count) {
    if (out_count)
      *out_count = 0;
    return (char **)0;
  }

  if (ht->item_count == 0) {
    *out_count = 0;
    return (char **)0;
  }

  char **keys = kmalloc((uint32_t)ht->item_count * sizeof(char *));
  if (!keys) {
    *out_count = 0;
    return (char **)0;
  }

  int k = 0;
  for (int i = 0; i < ht->bucket_count; i++) {
    cc_hash_entry_t *entry = ht->buckets[i];
    while (entry && k < ht->item_count) {
      size_t len = strlen(entry->key);
      keys[k] = kmalloc(len + 1);
      if (keys[k])
        memcpy(keys[k], entry->key, len + 1);
      k++;
      entry = entry->next;
    }
  }

  *out_count = k;
  return keys;
}

void cc_hash_free(cc_hash_t *ht) {
  if (!ht)
    return;

  for (int i = 0; i < ht->bucket_count; i++) {
    cc_hash_entry_t *entry = ht->buckets[i];
    while (entry) {
      cc_hash_entry_t *next = entry->next;
      kfree(entry->key);
      kfree(entry->value);
      kfree(entry);
      entry = next;
    }
  }

  kfree(ht->buckets);
  kfree(ht);
}
