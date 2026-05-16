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
} block_device_t;

void blkdev_init(void);
int blkdev_register(block_device_t* dev);
block_device_t* blkdev_get(int index);
int blkdev_count(void);
int blkdev_read(block_device_t* dev, uint32_t lba, uint32_t count, void* buffer);
int blkdev_write(block_device_t* dev, uint32_t lba, uint32_t count, const void* buffer);

#endif
