/* kernel/loopdev.c -- file-backed block_device_t using the VFS. */

#include "loopdev.h"
#include "vfs.h"
#include "memory.h"
#include "string.h"
#include "../drivers/serial.h"

#define LOOPDEV_SECTOR_SIZE 2048u

typedef struct {
    int      fd;
    uint32_t file_size;
    char     name_buf[VFS_MAX_PATH + 8]; /* "loop:" prefix */
} loopdev_ctx_t;

static int loopdev_read(void *driver_data, uint32_t lba,
                        uint32_t count, void *buffer);
static int loopdev_write(void *driver_data, uint32_t lba,
                         uint32_t count, const void *buffer);

static int loopdev_read(void *driver_data, uint32_t lba,
                        uint32_t count, void *buffer) {
    loopdev_ctx_t *ctx = (loopdev_ctx_t *)driver_data;
    uint8_t       *out = (uint8_t *)buffer;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t off = (lba + i) * LOOPDEV_SECTOR_SIZE;
        if (off + LOOPDEV_SECTOR_SIZE > ctx->file_size) return -1;
        if (vfs_seek(ctx->fd, (int32_t)off, SEEK_SET) < 0) return -1;
        int n = vfs_read(ctx->fd, out + i * LOOPDEV_SECTOR_SIZE,
                         LOOPDEV_SECTOR_SIZE);
        if (n != (int)LOOPDEV_SECTOR_SIZE) return -1;
    }
    return 0;
}

static int loopdev_write(void *driver_data, uint32_t lba,
                         uint32_t count, const void *buffer) {
    (void)driver_data; (void)lba; (void)count; (void)buffer;
    return -1;  /* read-only */
}

block_device_t *loopdev_create(const char *vfs_path) {
    if (!vfs_path) return NULL;

    int fd = vfs_open(vfs_path, O_RDONLY);
    if (fd < 0) {
        KERROR("loopdev: vfs_open('%s') failed: %d", vfs_path, fd);
        return NULL;
    }

    vfs_stat_t st;
    if (vfs_stat(vfs_path, &st) < 0 || st.size < LOOPDEV_SECTOR_SIZE) {
        vfs_close(fd);
        return NULL;
    }

    loopdev_ctx_t *ctx = (loopdev_ctx_t *)kmalloc(sizeof(loopdev_ctx_t));
    block_device_t *dev = (block_device_t *)kmalloc(sizeof(block_device_t));
    if (!ctx || !dev) {
        if (ctx) kfree(ctx);
        if (dev) kfree(dev);
        vfs_close(fd);
        return NULL;
    }

    ctx->fd = fd;
    ctx->file_size = st.size;
    /* Build a display name: "loop:<path-last-32>" */
    ctx->name_buf[0] = 'l'; ctx->name_buf[1] = 'o'; ctx->name_buf[2] = 'o';
    ctx->name_buf[3] = 'p'; ctx->name_buf[4] = ':';
    size_t plen = strlen(vfs_path);
    const char *tail = plen > 32 ? vfs_path + plen - 32 : vfs_path;
    size_t ti = 0;
    for (size_t i = 5; i < sizeof(ctx->name_buf) - 1 && tail[ti]; i++, ti++) {
        ctx->name_buf[i] = tail[ti];
    }
    ctx->name_buf[5 + ti] = '\0';

    dev->name         = ctx->name_buf;
    dev->sector_count = ctx->file_size / LOOPDEV_SECTOR_SIZE;
    dev->sector_size  = LOOPDEV_SECTOR_SIZE;
    dev->driver_data  = ctx;
    dev->read         = loopdev_read;
    dev->write        = loopdev_write;
    return dev;
}

void loopdev_destroy(block_device_t *dev) {
    if (!dev) return;
    loopdev_ctx_t *ctx = (loopdev_ctx_t *)dev->driver_data;
    if (ctx) {
        if (ctx->fd >= 0) vfs_close(ctx->fd);
        kfree(ctx);
    }
    kfree(dev);
}
