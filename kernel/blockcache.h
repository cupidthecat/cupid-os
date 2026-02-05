#ifndef BLOCKCACHE_H
#define BLOCKCACHE_H

#include "types.h"
#include "blockdev.h"
#include "isr.h"

#define CACHE_SIZE 64
#define SECTOR_SIZE 512

typedef struct {
    uint32_t lba;
    uint8_t valid;
    uint8_t dirty;
    uint32_t last_access;
    uint8_t data[SECTOR_SIZE];
} cache_entry_t;

typedef struct {
    cache_entry_t* entries;
    block_device_t* device;
    uint32_t hits;
    uint32_t misses;
    uint32_t evictions;
    uint32_t writebacks;
} block_cache_t;

int blockcache_init(block_device_t* device);
int blockcache_read(uint32_t lba, void* buffer);
int blockcache_write(uint32_t lba, const void* buffer);
void blockcache_flush_all(void);
void blockcache_periodic_flush(struct registers* r, uint32_t channel);
void blockcache_sync(void);
void blockcache_stats(void);

#endif
