/**
 * Block Cache
 *
 * Implements an LRU (Least Recently Used) cache for disk sectors with
 * write-back policy. Provides significant performance improvement by
 * reducing disk I/O operations.
 *
 * Features:
 * - 64-entry cache (32KB total)
 * - LRU eviction policy
 * - Write-back with periodic flush (every 5 seconds)
 * - Cache statistics tracking
 */

#include "blockcache.h"
#include "kernel.h"
#include "memory.h"
#include "string.h"
#include "debug.h"

static block_cache_t cache;
static uint32_t access_counter = 0;

/* Output function pointers (can be overridden for GUI mode) */
static void (*cache_print)(const char*) = print;
static void (*cache_print_int)(uint32_t) = print_int;

void blockcache_set_output(void (*print_fn)(const char*), void (*print_int_fn)(uint32_t)) {
    if (print_fn) cache_print = print_fn;
    if (print_int_fn) cache_print_int = print_int_fn;
}

/**
 * blockcache_init - Initialize block cache
 *
 * @param device: Block device to cache
 * @return 0 on success, -1 on failure
 */
int blockcache_init(block_device_t* device) {
    if (!device) {
        return -1;
    }

    // Allocate cache entries
    cache.entries = (cache_entry_t*)kmalloc(CACHE_SIZE * sizeof(cache_entry_t));
    if (!cache.entries) {
        print("Block cache: kmalloc failed\n");
        return -1;
    }

    cache.device = device;
    cache.hits = 0;
    cache.misses = 0;
    cache.evictions = 0;
    cache.writebacks = 0;

    // Mark all entries as invalid
    for (int i = 0; i < CACHE_SIZE; i++) {
        cache.entries[i].valid = 0;
        cache.entries[i].dirty = 0;
        cache.entries[i].lba = 0;
        cache.entries[i].last_access = 0;
    }

    print("Block cache initialized (");
    print_int(CACHE_SIZE);
    print(" entries, ");
    print_int((CACHE_SIZE * SECTOR_SIZE) / 1024);
    print(" KB)\n");

    return 0;
}

/**
 * find_cache_entry - Find cache entry for given LBA
 *
 * @param lba: Logical block address
 * @return Pointer to cache entry, or NULL if not found
 */
static cache_entry_t* find_cache_entry(uint32_t lba) {
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache.entries[i].valid && cache.entries[i].lba == lba) {
            return &cache.entries[i];
        }
    }
    return NULL;
}

/**
 * find_lru_entry - Find least recently used cache entry
 *
 * Returns first invalid entry if available, otherwise returns
 * entry with oldest last_access time.
 *
 * @return Pointer to LRU cache entry
 */
static cache_entry_t* find_lru_entry(void) {
    uint32_t oldest = 0xFFFFFFFF;
    int lru_idx = 0;

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (!cache.entries[i].valid) {
            return &cache.entries[i];
        }
        if (cache.entries[i].last_access < oldest) {
            oldest = cache.entries[i].last_access;
            lru_idx = i;
        }
    }

    return &cache.entries[lru_idx];
}

/**
 * blockcache_read - Read sector via cache
 *
 * @param lba: Logical block address
 * @param buffer: Buffer to read into
 * @return 0 on success, -1 on error
 */
int blockcache_read(uint32_t lba, void* buffer) {
    // Search for cached entry
    cache_entry_t* entry = find_cache_entry(lba);

    if (entry) {
        // Cache hit
        cache.hits++;
        memcpy(buffer, entry->data, SECTOR_SIZE);
        entry->last_access = ++access_counter;
        return 0;
    }

    // Cache miss
    cache.misses++;

    // Find LRU entry to evict
    entry = find_lru_entry();

    // Write back if dirty
    if (entry->valid && entry->dirty) {
        cache.writebacks++;
        if (blkdev_write(cache.device, entry->lba, 1, entry->data) != 0) {
            print("Block cache: writeback failed at LBA ");
            print_int(entry->lba);
            print("\n");
            return -1;
        }
    }

    // Load new sector from disk
    if (blkdev_read(cache.device, lba, 1, entry->data) != 0) {
        print("Block cache: disk read failed at LBA ");
        print_int(lba);
        print("\n");
        return -1;
    }

    // Update entry
    entry->lba = lba;
    entry->valid = 1;
    entry->dirty = 0;
    entry->last_access = ++access_counter;
    if (cache.evictions < 0xFFFFFFFF) {
        cache.evictions++;
    }

    // Copy to output buffer
    memcpy(buffer, entry->data, SECTOR_SIZE);
    return 0;
}

/**
 * blockcache_write - Write sector via cache
 *
 * @param lba: Logical block address
 * @param buffer: Buffer containing data to write
 * @return 0 on success, -1 on error
 */
int blockcache_write(uint32_t lba, const void* buffer) {
    cache_entry_t* entry = find_cache_entry(lba);

    if (entry) {
        // Cache hit - update in place
        memcpy(entry->data, buffer, SECTOR_SIZE);
        entry->dirty = 1;
        entry->last_access = ++access_counter;
        return 0;
    }

    // Cache miss - allocate entry
    cache.misses++;
    entry = find_lru_entry();

    // Write back if dirty
    if (entry->valid && entry->dirty) {
        cache.writebacks++;
        if (blkdev_write(cache.device, entry->lba, 1, entry->data) != 0) {
            print("Block cache: writeback failed at LBA ");
            print_int(entry->lba);
            print("\n");
            return -1;
        }
    }

    // For write-allocate: read sector first (to allow partial writes later)
    if (blkdev_read(cache.device, lba, 1, entry->data) != 0) {
        print("Block cache: disk read failed at LBA ");
        print_int(lba);
        print("\n");
        return -1;
    }

    // Now update with new data
    memcpy(entry->data, buffer, SECTOR_SIZE);
    entry->lba = lba;
    entry->valid = 1;
    entry->dirty = 1;
    entry->last_access = ++access_counter;
    if (cache.evictions < 0xFFFFFFFF) {
        cache.evictions++;
    }

    return 0;
}

/**
 * blockcache_flush_all - Flush all dirty cache entries to disk
 */
void blockcache_flush_all(void) {
    uint32_t flushed = 0;

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache.entries[i].valid && cache.entries[i].dirty) {
            if (blkdev_write(cache.device, cache.entries[i].lba, 1, cache.entries[i].data) != 0) {
                print("Block cache: flush failed at LBA ");
                print_int(cache.entries[i].lba);
                print("\n");
                continue;
            }
            cache.entries[i].dirty = 0;
            flushed++;
        }
    }

    if (flushed > 0) {
        print("Block cache: flushed ");
        print_int(flushed);
        print(" dirty block");
        if (flushed != 1) {
            print("s");
        }
        print("\n");
    }
}

/**
 * blockcache_periodic_flush - Timer callback for periodic cache flush
 *
 * Called every 5 seconds by timer interrupt to ensure data persistence.
 *
 * @param r: Interrupt registers (unused)
 * @param channel: Timer channel (unused)
 */
void blockcache_periodic_flush(struct registers* r, uint32_t channel) {
    (void)r;
    (void)channel;
    blockcache_flush_all();
}

/**
 * blockcache_sync - Manual cache flush (sync command)
 */
void blockcache_sync(void) {
    blockcache_flush_all();
}

/**
 * blockcache_stats - Print cache statistics
 */
void blockcache_stats(void) {
    cache_print("Cache statistics:\n");
    cache_print("  Hits: ");
    cache_print_int(cache.hits);
    cache_print("\n  Misses: ");
    cache_print_int(cache.misses);
    cache_print("\n  Evictions: ");
    cache_print_int(cache.evictions);
    cache_print("\n  Writebacks: ");
    cache_print_int(cache.writebacks);
    cache_print("\n");

    if (cache.hits + cache.misses > 0) {
        uint32_t total = cache.hits + cache.misses;
        uint32_t hit_percent = (cache.hits * 100) / total;
        cache_print("  Hit rate: ");
        cache_print_int(hit_percent);
        cache_print("%\n");
    }
}
