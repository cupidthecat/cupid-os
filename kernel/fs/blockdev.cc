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
static int registered_device_count = 0;
static int device_index_limit = 0;
static volatile uint32_t device_registry_lock = 0;

static void registry_lock(void) {
    while (
        __atomic_exchange_n(
            &device_registry_lock,
            1u,
            __ATOMIC_ACQUIRE
        ) != 0u
    ) {
    }
}

static void registry_unlock(void) {
    __atomic_store_n(&device_registry_lock, 0u, __ATOMIC_RELEASE);
}

/**
 * blkdev_init - Initialize block device layer
*/
void blkdev_init(void) {
    device_registry_lock = 0;
    registered_device_count = 0;
    device_index_limit = 0;
    for (int i = 0; i < MAX_BLOCK_DEVICES; i++) {
        devices[i] = NULL;
    }
    print("Block device layer initialized\n");
}

/**
 * blkdev_register - Register a block device
 *
 * @param dev: Pointer to block device structure
 * @return 0 on success, -1 on invalid, duplicate, or full registration
*/
int blkdev_register(block_device_t* dev) {
    if (!dev) {
        return -1;
    }

    registry_lock();

    int vacant = -1;
    for (int i = 0; i < device_index_limit; i++) {
        if (devices[i] == dev) {
            registry_unlock();
            return -1;
        }
        if (vacant < 0 && devices[i] == NULL) {
            vacant = i;
        }
    }

    if (dev->registry_registered || dev->registry_ref_count != 0u) {
        registry_unlock();
        return -1;
    }

    if (vacant < 0) {
        if (device_index_limit >= MAX_BLOCK_DEVICES) {
            registry_unlock();
            return -1;
        }
        vacant = device_index_limit;
        device_index_limit++;
    }

    /*
     * Keep one temporary reference across the unlocked registration log.
     * Another CPU may unregister the device as soon as it becomes visible.
     */
    dev->registry_ref_count = 2u;
    dev->registry_registered = true;
    devices[vacant] = dev;
    registered_device_count++;
    registry_unlock();

    print("Block device registered: ");
    print(dev->name);
    print(" (");
    print_int(dev->sector_count);
    print(" sectors, ");
    print_int(dev->sector_size);
    print(" bytes/sector)\n");

    (void)blkdev_put(dev);
    return 0;
}

/**
 * Remove a block device from the public registry.
 *
 * This ends the numeric index's lifetime but does not modify the device
 * object. A later registration may reuse the vacant index. The owning driver
 * remains responsible for any pointers cached before this call.
 */
int blkdev_unregister(block_device_t* dev) {
    if (!dev) {
        return -1;
    }

    registry_lock();

    int index = -1;
    for (int i = 0; i < device_index_limit; i++) {
        if (devices[i] == dev) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        registry_unlock();
        return -1;
    }

    devices[index] = NULL;
    registered_device_count--;
    while (
        device_index_limit > 0
        && devices[device_index_limit - 1] == NULL
    ) {
        device_index_limit--;
    }
    dev->registry_registered = false;
    dev->registry_ref_count--;
    bool release = dev->registry_ref_count == 0u;
    void (*release_callback)(void*) = dev->release;
    void *driver_data = dev->driver_data;
    registry_unlock();
    if (release && release_callback) release_callback(driver_data);
    return 0;
}

/**
 * blkdev_get - Get block device by index
 *
 * @param index: Device index (0-based)
 * @return Pointer to block device, or NULL if invalid or references saturated
*/
block_device_t* blkdev_get(int index) {
    if (index < 0) {
        return NULL;
    }

    registry_lock();
    block_device_t* dev = NULL;
    if (index < device_index_limit) {
        dev = devices[index];
        if (dev) {
            if (dev->registry_ref_count == 0xFFFFFFFFu) {
                dev = NULL;
            } else {
                dev->registry_ref_count++;
            }
        }
    }
    registry_unlock();
    return dev;
}

/**
 * Release a reference acquired by blkdev_get.
 */
int blkdev_put(block_device_t* dev) {
    if (!dev) return -1;

    registry_lock();
    if (
        dev->registry_ref_count == 0u
        || (
            dev->registry_registered
            && dev->registry_ref_count == 1u
        )
    ) {
        registry_unlock();
        return -1;
    }

    dev->registry_ref_count--;
    bool release = dev->registry_ref_count == 0u;
    void (*release_callback)(void*) = dev->release;
    void *driver_data = dev->driver_data;
    registry_unlock();
    if (release && release_callback) release_callback(driver_data);
    return 0;
}

/**
 * blkdev_count - Get number of registered block devices
 *
 * @return Number of live registry entries
 */
int blkdev_count(void) {
    registry_lock();
    int count = registered_device_count;
    registry_unlock();
    return count;
}

/**
 * blkdev_index_limit - Get the exclusive upper bound for sparse index scans
 */
int blkdev_index_limit(void) {
    registry_lock();
    int limit = device_index_limit;
    registry_unlock();
    return limit;
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
