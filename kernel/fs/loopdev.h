/* kernel/loopdev.h -- File-backed block device for mounting disk images
 * (e.g. .iso files) via the VFS layer. */
#ifndef LOOPDEV_H
#define LOOPDEV_H

#include "blockdev.h"

/* Open `vfs_path` read-only, wrap as a block_device_t whose sector size
 * is 2048 bytes. Returns NULL on failure (open failed, malloc failed,
 * or file smaller than one sector). Caller owns the returned pointer
 * and must free with loopdev_destroy.
 *
 * The returned device is NOT automatically registered via blkdev_register.
 * Callers (e.g. iso9660_vfs mount) pass it directly to block-level APIs. */
block_device_t *loopdev_create(const char *vfs_path);

/* vfs_close the backing fd, free driver_data and device struct. */
void loopdev_destroy(block_device_t *dev);

#endif
