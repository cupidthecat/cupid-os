/*
 * cupidscript_arrays.c - Array and associative array implementation
 *
 * Provides bash-like arrays: arr=(one two three), ${arr[0]}, ${arr[@]}
 * and associative arrays: declare -A map; map[key]=value
 */

#include "cupidscript_arrays.h"
#include "string.h"
#include "memory.h"
#include "../drivers/serial.h"

/* Regular array operations */

cs_array_t *cs_array_find(cs_array_t *arrays, int array_count,
                           const char *name) {
    for (int i = 0; i < array_count; i++) {
        if (arrays[i].used && strcmp(arrays[i].name, name) == 0) {
            return &arrays[i];
        }
    }
    return NULL;
}

void cs_array_create(cs_array_t *arrays, int *array_count,
                     const char *name, const char values[][MAX_VAR_VALUE],
                     int count) {
    /* Find existing or allocate new slot */
    cs_array_t *arr = cs_array_find(arrays, *array_count, name);

    if (!arr) {
        if (*array_count >= MAX_ARRAYS) {
            KERROR("CupidScript: too many arrays (max %d)", MAX_ARRAYS);
            return;
        }
        arr = &arrays[*array_count];
        (*array_count)++;
        arr->used = true;
        /* Copy name */
        int i = 0;
        while (name[i] && i < MAX_VAR_NAME - 1) {
            arr->name[i] = name[i];
            i++;
        }
        arr->name[i] = '\0';
    }

    /* Set elements */
    arr->length = 0;
    for (int i = 0; i < count && i < MAX_ARRAY_SIZE; i++) {
        int j = 0;
        while (values[i][j] && j < MAX_VAR_VALUE - 1) {
            arr->elements[i][j] = values[i][j];
            j++;
        }
        arr->elements[i][j] = '\0';
        arr->length++;
    }

    KDEBUG("CupidScript: created array '%s' with %d elements", name, count);
}

void cs_array_set(cs_array_t *arrays, int array_count,
                  const char *name, int index, const char *value) {
    cs_array_t *arr = cs_array_find(arrays, array_count, name);
    if (!arr) return;

    if (index < 0 || index >= MAX_ARRAY_SIZE) return;

    int i = 0;
    while (value[i] && i < MAX_VAR_VALUE - 1) {
        arr->elements[index][i] = value[i];
        i++;
    }
    arr->elements[index][i] = '\0';

    /* Extend length if needed */
    if (index >= arr->length) {
        arr->length = index + 1;
    }
}

const char *cs_array_get(cs_array_t *arrays, int array_count,
                         const char *name, int index) {
    cs_array_t *arr = cs_array_find(arrays, array_count, name);
    if (!arr) return NULL;
    if (index < 0 || index >= arr->length) return NULL;
    return arr->elements[index];
}

int cs_array_length(cs_array_t *arrays, int array_count, const char *name) {
    cs_array_t *arr = cs_array_find(arrays, array_count, name);
    if (!arr) return 0;
    return arr->length;
}

void cs_array_append(cs_array_t *arrays, int array_count,
                     const char *name, const char *value) {
    cs_array_t *arr = cs_array_find(arrays, array_count, name);
    if (!arr) return;
    if (arr->length >= MAX_ARRAY_SIZE) return;

    int i = 0;
    while (value[i] && i < MAX_VAR_VALUE - 1) {
        arr->elements[arr->length][i] = value[i];
        i++;
    }
    arr->elements[arr->length][i] = '\0';
    arr->length++;
}

/* Associative array operations */

cs_assoc_array_t *cs_assoc_find(cs_assoc_array_t *assocs, int assoc_count,
                                 const char *name) {
    for (int i = 0; i < assoc_count; i++) {
        if (assocs[i].used && strcmp(assocs[i].name, name) == 0) {
            return &assocs[i];
        }
    }
    return NULL;
}

void cs_assoc_create(cs_assoc_array_t *assocs, int *assoc_count,
                     const char *name) {
    /* Check if already exists */
    cs_assoc_array_t *a = cs_assoc_find(assocs, *assoc_count, name);
    if (a) return;  /* Already exists */

    if (*assoc_count >= MAX_ASSOC_ARRAYS) {
        KERROR("CupidScript: too many assoc arrays (max %d)",
               MAX_ASSOC_ARRAYS);
        return;
    }

    a = &assocs[*assoc_count];
    (*assoc_count)++;
    a->used = true;
    a->count = 0;

    int i = 0;
    while (name[i] && i < MAX_VAR_NAME - 1) {
        a->name[i] = name[i];
        i++;
    }
    a->name[i] = '\0';

    /* Clear all entries */
    for (int j = 0; j < MAX_ASSOC_SIZE; j++) {
        a->entries[j].used = false;
    }

    KDEBUG("CupidScript: created assoc array '%s'", name);
}

void cs_assoc_set(cs_assoc_array_t *assocs, int assoc_count,
                  const char *name, const char *key, const char *value) {
    cs_assoc_array_t *a = cs_assoc_find(assocs, assoc_count, name);
    if (!a) return;

    /* Check if key already exists */
    for (int i = 0; i < MAX_ASSOC_SIZE; i++) {
        if (a->entries[i].used && strcmp(a->entries[i].key, key) == 0) {
            /* Update existing */
            int j = 0;
            while (value[j] && j < MAX_VAR_VALUE - 1) {
                a->entries[i].value[j] = value[j];
                j++;
            }
            a->entries[i].value[j] = '\0';
            return;
        }
    }

    /* Find empty slot */
    for (int i = 0; i < MAX_ASSOC_SIZE; i++) {
        if (!a->entries[i].used) {
            a->entries[i].used = true;
            int j = 0;
            while (key[j] && j < MAX_VAR_NAME - 1) {
                a->entries[i].key[j] = key[j];
                j++;
            }
            a->entries[i].key[j] = '\0';

            j = 0;
            while (value[j] && j < MAX_VAR_VALUE - 1) {
                a->entries[i].value[j] = value[j];
                j++;
            }
            a->entries[i].value[j] = '\0';

            a->count++;
            return;
        }
    }

    KERROR("CupidScript: assoc array '%s' full", name);
}

const char *cs_assoc_get(cs_assoc_array_t *assocs, int assoc_count,
                         const char *name, const char *key) {
    cs_assoc_array_t *a = cs_assoc_find(assocs, assoc_count, name);
    if (!a) return NULL;

    for (int i = 0; i < MAX_ASSOC_SIZE; i++) {
        if (a->entries[i].used && strcmp(a->entries[i].key, key) == 0) {
            return a->entries[i].value;
        }
    }
    return NULL;
}

bool cs_assoc_has_key(cs_assoc_array_t *assocs, int assoc_count,
                      const char *name, const char *key) {
    return cs_assoc_get(assocs, assoc_count, name, key) != NULL;
}

void cs_assoc_delete(cs_assoc_array_t *assocs, int assoc_count,
                     const char *name, const char *key) {
    cs_assoc_array_t *a = cs_assoc_find(assocs, assoc_count, name);
    if (!a) return;

    for (int i = 0; i < MAX_ASSOC_SIZE; i++) {
        if (a->entries[i].used && strcmp(a->entries[i].key, key) == 0) {
            a->entries[i].used = false;
            a->count--;
            return;
        }
    }
}
