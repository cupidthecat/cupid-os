#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include "types.h"

#define MAX_BLOCK_DEVICES 4

typedef struct {
    const char* name;
    uint32_t sector_count;
    uint32_t sector_size;
    void* driver_data;
    int (*read)(void* driver_data, uint32_t lba, uint32_t count, void* buffer);
    int (*write)(void* driver_data, uint32_t lba, uint32_t count, const void* buffer);
    void (*release)(void* driver_data);
    uint32_t registry_ref_count;
    bool registry_registered;
} block_device_t;

/*
 * A numeric block-device index is valid from registration until the matching
 * unregister call succeeds. The registry may reuse that index immediately.
 * blkdev_get() acquires a reference that keeps the exact device object alive
 * after unregistering it. Every successful get must have one blkdev_put().
 * A driver with dynamic state supplies release and initializes both registry
 * fields to zero before its first registration.
 */
void blkdev_init(void);
int blkdev_register(block_device_t* dev);
int blkdev_unregister(block_device_t* dev);
block_device_t* blkdev_get(int index);
int blkdev_put(block_device_t* dev);
/* Number of devices that are currently registered. */
int blkdev_count(void);
/* Exclusive upper bound for sparse index scans. Vacant indices return NULL. */
int blkdev_index_limit(void);
int blkdev_read(block_device_t* dev, uint32_t lba, uint32_t count, void* buffer);
int blkdev_write(block_device_t* dev, uint32_t lba, uint32_t count, const void* buffer);

#endif
