/*
 * cupidscript_arrays.h - Array and associative array support for CupidScript
 */
#ifndef CUPIDSCRIPT_ARRAYS_H
#define CUPIDSCRIPT_ARRAYS_H

#include "types.h"

/* Forward declarations */
typedef struct script_context script_context_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Limits
 * ══════════════════════════════════════════════════════════════════════ */
#define MAX_ARRAY_SIZE     32
#define MAX_ARRAYS          8
#define MAX_ASSOC_SIZE     32
#define MAX_ASSOC_ARRAYS    4

#ifndef MAX_VAR_NAME
#define MAX_VAR_NAME 64
#endif

#ifndef MAX_VAR_VALUE
#define MAX_VAR_VALUE 256
#endif

/* ══════════════════════════════════════════════════════════════════════
 *  Regular array
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
    char name[MAX_VAR_NAME];
    char elements[MAX_ARRAY_SIZE][MAX_VAR_VALUE];
    int  length;
    bool used;
} cs_array_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Associative array entry
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
    char key[MAX_VAR_NAME];
    char value[MAX_VAR_VALUE];
    bool used;
} cs_assoc_entry_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Associative array
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
    char name[MAX_VAR_NAME];
    cs_assoc_entry_t entries[MAX_ASSOC_SIZE];
    int  count;
    bool used;
} cs_assoc_array_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Regular array API
 * ══════════════════════════════════════════════════════════════════════ */

/* Create array from list of values */
void cs_array_create(cs_array_t *arrays, int *array_count,
                     const char *name, const char values[][MAX_VAR_VALUE],
                     int count);

/* Set element at index */
void cs_array_set(cs_array_t *arrays, int array_count,
                  const char *name, int index, const char *value);

/* Get element at index (returns NULL if not found) */
const char *cs_array_get(cs_array_t *arrays, int array_count,
                         const char *name, int index);

/* Get length of array */
int cs_array_length(cs_array_t *arrays, int array_count, const char *name);

/* Append elements */
void cs_array_append(cs_array_t *arrays, int array_count,
                     const char *name, const char *value);

/* Find array by name (returns NULL if not found) */
cs_array_t *cs_array_find(cs_array_t *arrays, int array_count,
                           const char *name);

/* ══════════════════════════════════════════════════════════════════════
 *  Associative array API
 * ══════════════════════════════════════════════════════════════════════ */

/* Create associative array */
void cs_assoc_create(cs_assoc_array_t *assocs, int *assoc_count,
                     const char *name);

/* Set key-value pair */
void cs_assoc_set(cs_assoc_array_t *assocs, int assoc_count,
                  const char *name, const char *key, const char *value);

/* Get value by key */
const char *cs_assoc_get(cs_assoc_array_t *assocs, int assoc_count,
                         const char *name, const char *key);

/* Check if key exists */
bool cs_assoc_has_key(cs_assoc_array_t *assocs, int assoc_count,
                      const char *name, const char *key);

/* Delete key */
void cs_assoc_delete(cs_assoc_array_t *assocs, int assoc_count,
                     const char *name, const char *key);

/* Find assoc array by name (returns NULL if not found) */
cs_assoc_array_t *cs_assoc_find(cs_assoc_array_t *assocs, int assoc_count,
                                 const char *name);

#endif /* CUPIDSCRIPT_ARRAYS_H */
