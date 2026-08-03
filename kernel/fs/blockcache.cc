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
#include "homefs.h"
#include "kernel.h"
#include "memory.h"
#include "bkl.h"
#include "string.h"
#include "debug.h"

static block_cache_t cache;
static uint32_t access_counter = 0;

/* The cache metadata and the legacy PIO ATA command ports are both shared
 * machine state.  Timer callbacks can reach this module from any LAPIC, so a
 * local interrupt disable alone is insufficient on SMP.  The BKL also saves
 * and clears IF, preventing a same-CPU timer callback from re-entering a PIO
 * transaction while a sector operation is in flight. */
static bool blockcache_guard_enter(void) {
    bool locked = bkl_is_initialized();
    if (locked) bkl_lock();
    return locked;
}

static void blockcache_guard_leave(bool locked) {
    if (locked) bkl_unlock();
}

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
static int blockcache_read_unlocked(uint32_t lba, void* buffer) {
    uint8_t loaded[SECTOR_SIZE];
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
        /* The old bytes remain valid after writeback, but no longer dirty. */
        entry->dirty = 0;
    }

    /* A failed device read may still modify its destination buffer.  Stage
     * into scratch space so the old cache identity and bytes stay paired. */
    if (blkdev_read(cache.device, lba, 1, loaded) != 0) {
        print("Block cache: disk read failed at LBA ");
        print_int(lba);
        print("\n");
        return -1;
    }

    // Update entry
    memcpy(entry->data, loaded, SECTOR_SIZE);
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

int blockcache_read(uint32_t lba, void* buffer) {
    bool locked = blockcache_guard_enter();
    int rc = blockcache_read_unlocked(lba, buffer);
    blockcache_guard_leave(locked);
    return rc;
}

/**
 * blockcache_write - Write sector via cache
 *
 * @param lba: Logical block address
 * @param buffer: Buffer containing data to write
 * @return 0 on success, -1 on error
*/
static int blockcache_write_unlocked(uint32_t lba, const void* buffer) {
    uint8_t loaded[SECTOR_SIZE];
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
        entry->dirty = 0;
    }

    // For write-allocate: read sector first (to allow partial writes later)
    if (blkdev_read(cache.device, lba, 1, loaded) != 0) {
        print("Block cache: disk read failed at LBA ");
        print_int(lba);
        print("\n");
        return -1;
    }

    // Now update with new data
    memcpy(entry->data, loaded, SECTOR_SIZE);
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

int blockcache_write(uint32_t lba, const void* buffer) {
    bool locked = blockcache_guard_enter();
    int rc = blockcache_write_unlocked(lba, buffer);
    blockcache_guard_leave(locked);
    return rc;
}

/**
 * blockcache_flush_all - Flush all dirty cache entries to disk
*/
static int blockcache_flush_all_unlocked(void) {
    uint32_t flushed = 0;
    int status = 0;

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache.entries[i].valid && cache.entries[i].dirty) {
            if (blkdev_write(cache.device, cache.entries[i].lba, 1, cache.entries[i].data) != 0) {
                print("Block cache: flush failed at LBA ");
                print_int(cache.entries[i].lba);
                print("\n");
                status = -1;
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
    return status;
}

int blockcache_flush_all(void) {
    bool locked = blockcache_guard_enter();
    int status = blockcache_flush_all_unlocked();
    blockcache_guard_leave(locked);
    return status;
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
int blockcache_sync(void) {
    bool locked = blockcache_guard_enter();
    int home_status = homefs_sync();
    int cache_status = blockcache_flush_all_unlocked();
    blockcache_guard_leave(locked);
    return home_status < 0 ? home_status : cache_status;
}

typedef struct {
    uint32_t expected_lba;
    uint32_t reads;
    uint32_t writes;
    int bad_write;
} blockcache_failure_test_t;

static int blockcache_failure_test_read(void *driver_data, uint32_t lba,
                                        uint32_t count, void *buffer) {
    blockcache_failure_test_t *test =
        (blockcache_failure_test_t *)driver_data;
    (void)lba;
    (void)count;
    test->reads++;
    memset(buffer, 0x5a, SECTOR_SIZE);
    return -1;
}

static int blockcache_failure_test_write(void *driver_data, uint32_t lba,
                                         uint32_t count,
                                         const void *buffer) {
    blockcache_failure_test_t *test =
        (blockcache_failure_test_t *)driver_data;
    const uint8_t *bytes = (const uint8_t *)buffer;
    test->writes++;
    if (lba != test->expected_lba || count != 1u) test->bad_write = 1;
    for (uint32_t i = 0; i < SECTOR_SIZE; i++) {
        if (bytes[i] != 0xa5u) {
            test->bad_write = 1;
            break;
        }
    }
    return 0;
}

static int blockcache_failure_victim_is_safe(cache_entry_t *entry,
                                             blockcache_failure_test_t *test) {
    if (!entry->valid || entry->dirty ||
        entry->lba != test->expected_lba ||
        test->reads != 1u || test->writes != 1u || test->bad_write) {
        return 0;
    }
    for (uint32_t i = 0; i < SECTOR_SIZE; i++) {
        if (entry->data[i] != 0xa5u) return 0;
    }
    return 1;
}

int blockcache_failure_selftest(void) {
    cache_entry_t *entries =
        (cache_entry_t *)kmalloc(CACHE_SIZE * sizeof(cache_entry_t));
    if (!entries) return -1;

    block_device_t device;
    blockcache_failure_test_t test;
    uint8_t io_buffer[SECTOR_SIZE];
    memset(&device, 0, sizeof(device));
    memset(&test, 0, sizeof(test));
    memset(io_buffer, 0x33, sizeof(io_buffer));
    for (int i = 0; i < CACHE_SIZE; i++) {
        entries[i].lba = 100u + (uint32_t)i;
        entries[i].valid = 1;
        entries[i].dirty = 0;
        entries[i].last_access = 2u + (uint32_t)i;
        memset(entries[i].data, (int)(uint8_t)i, SECTOR_SIZE);
    }
    entries[0].lba = 7u;
    entries[0].dirty = 1;
    entries[0].last_access = 1u;
    memset(entries[0].data, 0xa5, SECTOR_SIZE);

    test.expected_lba = entries[0].lba;
    device.driver_data = &test;
    device.read = blockcache_failure_test_read;
    device.write = blockcache_failure_test_write;

    bool locked = blockcache_guard_enter();
    block_cache_t saved_cache = cache;
    uint32_t saved_access_counter = access_counter;
    memset(&cache, 0, sizeof(cache));
    cache.entries = entries;
    cache.device = &device;

    int okay = blockcache_read_unlocked(999u, io_buffer) < 0 &&
               blockcache_failure_victim_is_safe(&entries[0], &test);

    memset(&test, 0, sizeof(test));
    test.expected_lba = entries[0].lba;
    entries[0].dirty = 1;
    if (blockcache_write_unlocked(1000u, io_buffer) >= 0 ||
        !blockcache_failure_victim_is_safe(&entries[0], &test)) {
        okay = 0;
    }

    cache = saved_cache;
    access_counter = saved_access_counter;
    blockcache_guard_leave(locked);
    kfree(entries);
    return okay ? 0 : -1;
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
