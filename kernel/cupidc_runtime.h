/**
 * cupidc_runtime.h - Dynamic data structures for CupidC programs
 *
 * Provides Array and HashTable implementations that CupidC programs
 * can use via kernel API bindings.  These are essential for the
 * CupidC shell implementation.
 */

#ifndef CUPIDC_RUNTIME_H
#define CUPIDC_RUNTIME_H

#include "types.h"

/* Dynamic Array */

typedef struct {
  void **data;
  int count;
  int capacity;
} cc_array_t;

cc_array_t *cc_array_new(void);
void cc_array_push(cc_array_t *arr, void *item);
void *cc_array_get(cc_array_t *arr, int idx);
void cc_array_set(cc_array_t *arr, int idx, void *item);
int cc_array_count(cc_array_t *arr);
void cc_array_free(cc_array_t *arr);
void cc_array_clear(cc_array_t *arr);

/* Hash Table (string -> string) */

typedef struct cc_hash_entry {
  char *key;
  char *value;
  struct cc_hash_entry *next;
} cc_hash_entry_t;

typedef struct {
  cc_hash_entry_t **buckets;
  int bucket_count;
  int item_count;
} cc_hash_t;

cc_hash_t *cc_hash_new(int size);
void cc_hash_set(cc_hash_t *ht, const char *key, const char *value);
const char *cc_hash_get(cc_hash_t *ht, const char *key);
void cc_hash_delete(cc_hash_t *ht, const char *key);
int cc_hash_exists(cc_hash_t *ht, const char *key);
int cc_hash_count(cc_hash_t *ht);
void cc_hash_free(cc_hash_t *ht);

/**
 * Iterate over all entries.  Calls fn(key, value) for each entry.
 * Note: CupidC doesn't support function pointer args directly yet,
 * so this is primarily for kernel-side use.
 */
void cc_hash_foreach(cc_hash_t *ht, void (*fn)(const char *, const char *));

/**
 * Get all keys as a freshly allocated array of strings.
 * Caller must kfree each string and the array itself.
 * Returns count via out parameter.
 */
char **cc_hash_keys(cc_hash_t *ht, int *out_count);

#endif /* CUPIDC_RUNTIME_H */
