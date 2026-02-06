/**
 * devfs.h — Device filesystem interface for CupidOS
 *
 * Exposes kernel devices as files under /dev.
 * Built-in devices: null, zero, random, serial.
 */

#ifndef DEVFS_H
#define DEVFS_H

#include "vfs.h"

#define DEVFS_MAX_DEVICES 16

/**
 * Return the VFS operations struct for the devfs filesystem type.
 */
vfs_fs_ops_t *devfs_get_ops(void);

/**
 * Register a new device in devfs.
 *
 * @param name   Device name (e.g. "null", "zero").
 * @param read   Read handler: int read(void *buf, uint32_t count)
 * @param write  Write handler: int write(const void *buf, uint32_t count)
 */
int devfs_register_device(const char *name,
                          int (*read)(void *buf, uint32_t count),
                          int (*write)(const void *buf, uint32_t count));

/**
 * Register all built-in devices (null, zero, random, serial).
 * Call before mounting devfs.
 */
void devfs_register_builtins(void);

#endif /* DEVFS_H */
