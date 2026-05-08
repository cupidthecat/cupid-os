/**
 * Block Device Layer
 *
 * Provides a generic abstraction for block-based storage devices.
 * Allows different device drivers (ATA, floppy, etc.) to register
 * themselves and be accessed through a uniform interface.
 */

#include "blockdev.h"
#include "kernel.h"

static block_device_t* devices[MAX_BLOCK_DEVICES];
static int device_count = 0;

/**
 * blkdev_init - Initialize block device layer
 */
void blkdev_init(void) {
    device_count = 0;
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        devices[i] = NULL;
    }
    print("Block device layer initialized\n");
}

/**
 * blkdev_register - Register a block device
 *
 * @param dev: Pointer to block device structure
 * @return 0 on success, -1 on failure
 */
int blkdev_register(block_device_t* dev) {
    if (!dev || device_count >= MAX_BLOCK_DEVICES) {
        return -1;
    }

    devices[device_count++] = dev;

    print("Block device registered: ");
    print(dev->name);
    print(" (");
    print_int(dev->sector_count);
    print(" sectors, ");
    print_int(dev->sector_size);
    print(" bytes/sector)\n");

    return 0;
}

/**
 * blkdev_get - Get block device by index
 *
 * @param index: Device index (0-based)
 * @return Pointer to block device, or NULL if invalid
 */
block_device_t* blkdev_get(int index) {
    if (index < 0 || index >= device_count) {
        return NULL;
    }
    return devices[index];
}

/**
 * blkdev_count - Get number of registered block devices
 *
 * @return Number of devices
 */
int blkdev_count(void) {
    return device_count;
}

/**
 * blkdev_read - Read sectors from block device
 *
 * @param dev: Block device
 * @param lba: Logical block address
 * @param count: Number of sectors to read
 * @param buffer: Buffer to read into
 * @return 0 on success, -1 on error
 */
int blkdev_read(block_device_t* dev, uint32_t lba, uint32_t count, void* buffer) {
    if (!dev || !dev->read) {
        return -1;
    }
    return dev->read(dev->driver_data, lba, count, buffer);
}

/**
 * blkdev_write - Write sectors to block device
 *
 * @param dev: Block device
 * @param lba: Logical block address
 * @param count: Number of sectors to write
 * @param buffer: Buffer containing data to write
 * @return 0 on success, -1 on error
 */
int blkdev_write(block_device_t* dev, uint32_t lba, uint32_t count, const void* buffer) {
    if (!dev || !dev->write) {
        return -1;
    }
    return dev->write(dev->driver_data, lba, count, buffer);
}
