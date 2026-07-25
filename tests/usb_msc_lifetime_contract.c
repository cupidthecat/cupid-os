#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define TYPES_H

#include "../kernel/fs/blockdev.c"
#include "../kernel/usb/usb_msc.c"

static void *released_state;
static int release_count;

void print(const char *text) {
    (void)text;
}

void print_int(uint32_t value) {
    (void)value;
}

void klog(log_level_t level, const char *format, ...) {
    (void)level;
    (void)format;
}

void *kmalloc_debug(size_t size, const char *file, uint32_t line) {
    (void)file;
    (void)line;
    return malloc(size);
}

void kfree(void *ptr) {
    if (ptr == released_state) release_count++;
    free(ptr);
}

int usb_control(
    usb_device_t *dev,
    uint8_t bmRequestType,
    uint8_t bRequest,
    uint16_t wValue,
    uint16_t wIndex,
    void *data,
    uint16_t wLength
) {
    (void)dev;
    (void)bmRequestType;
    (void)bRequest;
    (void)wValue;
    (void)wIndex;
    (void)data;
    (void)wLength;
    return -1;
}

int usb_control_retry(
    usb_device_t *dev,
    uint8_t bmRequestType,
    uint8_t bRequest,
    uint16_t wValue,
    uint16_t wIndex,
    void *data,
    uint16_t wLength
) {
    return usb_control(
        dev,
        bmRequestType,
        bRequest,
        wValue,
        wIndex,
        data,
        wLength
    );
}

int usb_register_driver(usb_driver_t *driver) {
    (void)driver;
    return 0;
}

static msc_state_t *make_state(usb_device_t *dev) {
    msc_state_t *st = malloc(sizeof(msc_state_t));
    if (!st) return NULL;
    st->dev = dev;
    st->command_lock = 0u;
    st->online = true;
    st->blk.name = "usb-test";
    st->blk.sector_count = 16u;
    st->blk.sector_size = 512u;
    st->blk.driver_data = st;
    st->blk.read = blk_read;
    st->blk.write = blk_write;
    st->blk.release = msc_block_release;
    st->blk.registry_ref_count = 0u;
    st->blk.registry_registered = false;
    dev->driver_data = st;
    return st;
}

int main(void) {
    usb_device_t dev = {0};
    uint8_t byte = 0u;
    if (!msc_capacity_supported(31u, 512u)) return 16;
    if (!msc_capacity_supported(31u, 2048u)) return 17;
    if (msc_capacity_supported(31u, 511u)) return 18;
    if (msc_capacity_supported(0xFFFFFFFFu, 512u)) return 19;
    blkdev_init();

    msc_state_t *first = make_state(&dev);
    if (!first) return 1;
    released_state = first;
    if (blkdev_register(&first->blk) != 0) return 2;
    block_device_t *held = blkdev_get(0);
    if (held != &first->blk) return 3;
    if (msc_disconnect(&dev) != 0) return 4;
    if (release_count != 0) return 5;
    if (blkdev_count() != 0 || blkdev_index_limit() != 0) return 6;
    if (blkdev_read(held, 0u, 1u, &byte) != -1) return 7;
    if (blkdev_write(held, 0u, 1u, &byte) != -1) return 8;
    if (blkdev_put(held) != 0) return 9;
    if (release_count != 1) return 10;

    msc_state_t *second = make_state(&dev);
    if (!second) return 11;
    released_state = second;
    if (blkdev_register(&second->blk) != 0) return 12;
    if (msc_disconnect(&dev) != 0) return 13;
    if (release_count != 2) return 14;
    if (blkdev_count() != 0 || blkdev_index_limit() != 0) return 15;

    msc_state_t *third = make_state(&dev);
    if (!third) return 20;
    released_state = third;
    if (msc_disconnect(&dev) != -1) return 22;
    if (
        dev.driver_data != third
        || third->dev != &dev
        || !third->online
    ) {
        return 23;
    }
    if (release_count != 2) return 24;
    if (blkdev_count() != 0 || blkdev_index_limit() != 0) return 25;
    if (blkdev_register(&third->blk) != 0) return 21;
    if (msc_disconnect(&dev) != 0) return 26;
    if (release_count != 3) return 27;
    if (blkdev_count() != 0 || blkdev_index_limit() != 0) return 28;
    return 0;
}
