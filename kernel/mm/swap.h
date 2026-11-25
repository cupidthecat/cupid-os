/** @file swap.h
 * @brief Pin, release, and inspect opt-in disk-backed allocations. */
#ifndef SWAP_H
#define SWAP_H

#include "types.h"

typedef uint32_t swap_handle_t;
#define SWAP_INVALID 0u

#define SWAP_MAX_HANDLES 1024u

int           swap_init(const char *backing_path, uint32_t pool_bytes);
swap_handle_t swap_kmalloc(uint32_t size);
void         *swap_pin(swap_handle_t h);
void          swap_unpin(swap_handle_t h);
void          swap_free(swap_handle_t h);

typedef struct {
    uint32_t handles_in_use;
    uint32_t handles_total;
    uint32_t evictions;
    uint32_t disk_alloc[4];
    uint32_t disk_cap[4];
    uint32_t ram_alloc[4];
    uint32_t ram_cap[4];
    uint32_t pinned_count;
} swap_stats_t;

void swap_stats(swap_stats_t *out);

#endif
