/**
 * devfs.c — Device filesystem for CupidOS
 *
 * Provides /dev with pseudo-devices like null, zero, random, serial.
 * Each device is registered with read/write callbacks.
 */

#include "devfs.h"
#include "vfs.h"
#include "string.h"
#include "memory.h"
#include "../drivers/serial.h"

/* ── Device registry entry ────────────────────────────────────────── */

typedef struct {
    char    name[VFS_MAX_NAME];
    int   (*read)(void *buf, uint32_t count);
    int   (*write)(const void *buf, uint32_t count);
    int     in_use;
} devfs_device_t;

/* ── DevFS instance ───────────────────────────────────────────────── */

typedef struct {
    devfs_device_t devices[DEVFS_MAX_DEVICES];
    int            device_count;
} devfs_t;

/* ── DevFS file handle ────────────────────────────────────────────── */

typedef struct {
    devfs_device_t *device;
    int             readdir_index;   /* For directory enumeration */
} devfs_handle_t;

/* Singleton instance pointer for device registration before mount */
static devfs_t *g_devfs = NULL;

/* ══════════════════════════════════════════════════════════════════════
 *  Built-in devices
 * ══════════════════════════════════════════════════════════════════════ */

static int dev_null_read(void *buf, uint32_t count) {
    (void)buf; (void)count;
    return 0;  /* EOF always */
}

static int dev_null_write(const void *buf, uint32_t count) {
    (void)buf;
    return (int)count;  /* Discard, report success */
}

static int dev_zero_read(void *buf, uint32_t count) {
    memset(buf, 0, count);
    return (int)count;
}

static int dev_zero_write(const void *buf, uint32_t count) {
    (void)buf;
    return (int)count;  /* Discard */
}

/* Simple pseudo-random based on linear congruential generator */
static uint32_t random_seed = 12345;

static int dev_random_read(void *buf, uint32_t count) {
    uint8_t *p = (uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) {
        random_seed = random_seed * 1103515245u + 12345u;
        p[i] = (uint8_t)((random_seed >> 16) & 0xFF);
    }
    return (int)count;
}

static int dev_random_write(const void *buf, uint32_t count) {
    /* Seed the generator from written bytes */
    const uint8_t *p = (const uint8_t *)buf;
    for (uint32_t i = 0; i < count; i++) {
        random_seed ^= ((uint32_t)p[i] << (uint32_t)((i % 4) * 8));
    }
    return (int)count;
}

static int dev_serial_read(void *buf, uint32_t count) {
    (void)buf; (void)count;
    return 0;  /* Serial input not yet supported */
}

static int dev_serial_write(const void *buf, uint32_t count) {
    const char *p = (const char *)buf;
    for (uint32_t i = 0; i < count; i++) {
        serial_write_char(p[i]);
    }
    return (int)count;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Internal helpers
 * ══════════════════════════════════════════════════════════════════════ */

static devfs_device_t *devfs_find_device(devfs_t *fs, const char *name) {
    for (int i = 0; i < fs->device_count; i++) {
        if (fs->devices[i].in_use && strcmp(fs->devices[i].name, name) == 0) {
            return &fs->devices[i];
        }
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 *  VFS operations implementation
 * ══════════════════════════════════════════════════════════════════════ */

static int devfs_mount(const char *source, void **fs_private) {
    (void)source;

    devfs_t *fs;
    if (g_devfs) {
        /* Reuse existing instance (devices already registered) */
        fs = g_devfs;
    } else {
        fs = kmalloc(sizeof(devfs_t));
        if (!fs) return VFS_EIO;
        memset(fs, 0, sizeof(devfs_t));
        g_devfs = fs;
    }

    *fs_private = fs;
    return VFS_OK;
}

static int devfs_unmount(void *fs_private) {
    (void)fs_private;
    return VFS_OK;
}

static int devfs_open(void *fs_private, const char *path,
                      uint32_t flags, void **file_handle) {
    devfs_t *fs = (devfs_t *)fs_private;
    (void)flags;

    /* Strip leading slashes */
    while (*path == '/') path++;

    /* Empty path => open the /dev directory itself */
    if (path[0] == '\0') {
        devfs_handle_t *h = kmalloc(sizeof(devfs_handle_t));
        if (!h) return VFS_EIO;
        memset(h, 0, sizeof(devfs_handle_t));
        h->device = NULL;  /* Indicates directory */
        h->readdir_index = 0;
        *file_handle = h;
        return VFS_OK;
    }

    devfs_device_t *dev = devfs_find_device(fs, path);
    if (!dev) return VFS_ENOENT;

    devfs_handle_t *h = kmalloc(sizeof(devfs_handle_t));
    if (!h) return VFS_EIO;
    memset(h, 0, sizeof(devfs_handle_t));
    h->device = dev;
    h->readdir_index = 0;

    *file_handle = h;
    return VFS_OK;
}

static int devfs_close(void *file_handle) {
    if (file_handle) kfree(file_handle);
    return VFS_OK;
}

static int devfs_read(void *file_handle, void *buffer, uint32_t count) {
    devfs_handle_t *h = (devfs_handle_t *)file_handle;
    if (!h || !h->device) return VFS_EINVAL;
    if (!h->device->read) return VFS_ENOSYS;
    return h->device->read(buffer, count);
}

static int devfs_write(void *file_handle, const void *buffer,
                       uint32_t count) {
    devfs_handle_t *h = (devfs_handle_t *)file_handle;
    if (!h || !h->device) return VFS_EINVAL;
    if (!h->device->write) return VFS_ENOSYS;
    return h->device->write(buffer, count);
}

static int devfs_seek(void *file_handle, int32_t offset, int whence) {
    (void)file_handle; (void)offset; (void)whence;
    return VFS_ENOSYS;  /* Devices are not seekable */
}

static int devfs_stat(void *fs_private, const char *path,
                      vfs_stat_t *st) {
    devfs_t *fs = (devfs_t *)fs_private;

    /* Strip leading slashes */
    while (*path == '/') path++;

    if (path[0] == '\0') {
        st->type = VFS_TYPE_DIR;
        st->size = 0;
        return VFS_OK;
    }

    devfs_device_t *dev = devfs_find_device(fs, path);
    if (!dev) return VFS_ENOENT;

    st->type = VFS_TYPE_DEV;
    st->size = 0;
    return VFS_OK;
}

static int devfs_readdir(void *file_handle, vfs_dirent_t *dirent) {
    devfs_handle_t *h = (devfs_handle_t *)file_handle;
    if (!h) return VFS_EINVAL;
    if (h->device != NULL) return VFS_ENOTDIR; /* Not a directory */

    /* Walk through devices */
    while (h->readdir_index < DEVFS_MAX_DEVICES) {
        int idx = h->readdir_index;
        h->readdir_index++;

        /* Need g_devfs to iterate devices */
        if (!g_devfs) return 0;
        if (!g_devfs->devices[idx].in_use) continue;

        size_t i = 0;
        const char *name = g_devfs->devices[idx].name;
        while (name[i] && i < VFS_MAX_NAME - 1) {
            dirent->name[i] = name[i];
            i++;
        }
        dirent->name[i] = '\0';
        dirent->type = VFS_TYPE_DEV;
        dirent->size = 0;
        return 1;
    }

    return 0; /* No more entries */
}

static int devfs_mkdir_op(void *fs_private, const char *path) {
    (void)fs_private; (void)path;
    return VFS_ENOSYS;  /* Cannot create directories in devfs */
}

static int devfs_unlink_op(void *fs_private, const char *path) {
    (void)fs_private; (void)path;
    return VFS_ENOSYS;  /* Cannot unlink devices */
}

/* ══════════════════════════════════════════════════════════════════════
 *  VFS operations struct
 * ══════════════════════════════════════════════════════════════════════ */

static vfs_fs_ops_t devfs_ops = {
    .name     = "devfs",
    .mount    = devfs_mount,
    .unmount  = devfs_unmount,
    .open     = devfs_open,
    .close    = devfs_close,
    .read     = devfs_read,
    .write    = devfs_write,
    .seek     = devfs_seek,
    .stat     = devfs_stat,
    .readdir  = devfs_readdir,
    .mkdir    = devfs_mkdir_op,
    .unlink   = devfs_unlink_op
};

vfs_fs_ops_t *devfs_get_ops(void) {
    return &devfs_ops;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Device registration
 * ══════════════════════════════════════════════════════════════════════ */

int devfs_register_device(const char *name,
                          int (*read)(void *buf, uint32_t count),
                          int (*write)(const void *buf, uint32_t count)) {
    /* Create instance if needed */
    if (!g_devfs) {
        g_devfs = kmalloc(sizeof(devfs_t));
        if (!g_devfs) return VFS_EIO;
        memset(g_devfs, 0, sizeof(devfs_t));
    }

    if (g_devfs->device_count >= DEVFS_MAX_DEVICES) return VFS_ENOSPC;

    devfs_device_t *dev = &g_devfs->devices[g_devfs->device_count];
    size_t i = 0;
    while (name[i] && i < VFS_MAX_NAME - 1) {
        dev->name[i] = name[i];
        i++;
    }
    dev->name[i] = '\0';
    dev->read = read;
    dev->write = write;
    dev->in_use = 1;
    g_devfs->device_count++;

    return VFS_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Register built-in devices (call during init)
 * ══════════════════════════════════════════════════════════════════════ */

void devfs_register_builtins(void) {
    devfs_register_device("null",   dev_null_read,   dev_null_write);
    devfs_register_device("zero",   dev_zero_read,   dev_zero_write);
    devfs_register_device("random", dev_random_read,  dev_random_write);
    devfs_register_device("serial", dev_serial_read,  dev_serial_write);
}
