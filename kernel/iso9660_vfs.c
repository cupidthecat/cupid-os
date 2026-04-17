/* kernel/iso9660_vfs.c — VFS adapter for ISO9660. */

#include "iso9660_vfs.h"
#include "iso9660.h"
#include "loopdev.h"
#include "vfs.h"
#include "memory.h"
#include "string.h"
#include "../drivers/serial.h"

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFu
#endif

#define ISO9660_MAX_MOUNTS 4

typedef struct {
    bool            in_use;
    block_device_t *bdev;
    iso9660_mount_t parse;
} iso9660_slot_t;

static iso9660_slot_t g_slots[ISO9660_MAX_MOUNTS];

static int iso_vfs_mount(const char *source, void **fs_private) {
    if (!source || !fs_private) return VFS_EINVAL;

    /* Find free slot. */
    iso9660_slot_t *slot = NULL;
    for (int i = 0; i < ISO9660_MAX_MOUNTS; i++) {
        if (!g_slots[i].in_use) { slot = &g_slots[i]; break; }
    }
    if (!slot) return VFS_EMFILE;

    block_device_t *bdev = loopdev_create(source);
    if (!bdev) return VFS_ENOENT;

    int rc = iso9660_mount_parse(bdev, &slot->parse);
    if (rc < 0) {
        loopdev_destroy(bdev);
        return rc;
    }

    slot->bdev   = bdev;
    slot->in_use = true;
    *fs_private  = slot;
    KINFO("iso9660: mounted '%s' (root lba=%u size=%u rr=%d)",
          source, slot->parse.root_extent_lba, slot->parse.root_extent_size,
          slot->parse.has_rockridge);
    return 0;
}

static int iso_vfs_unmount(void *fs_private) {
    if (!fs_private) return VFS_EINVAL;
    iso9660_slot_t *slot = (iso9660_slot_t *)fs_private;
    if (!slot->in_use) return VFS_EINVAL;
    loopdev_destroy(slot->bdev);
    slot->bdev   = NULL;
    slot->in_use = false;
    return 0;
}

/* Tasks 11-12 fill in read/seek/readdir: */
static int iso_vfs_open(void *fs_private, const char *path, uint32_t flags,
                        void **file_handle) {
    if (!fs_private || !path || !file_handle) return VFS_EINVAL;
    /* Reject write modes */
    if (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) {
        return VFS_EACCES;
    }
    iso9660_slot_t *slot = (iso9660_slot_t *)fs_private;

    iso9660_file_t *fh = (iso9660_file_t *)kmalloc(sizeof(iso9660_file_t));
    if (!fh) return VFS_EIO;

    int rc = iso9660_lookup(&slot->parse, path, fh);
    if (rc < 0) {
        kfree(fh);
        return rc;
    }
    fh->owner = slot;
    *file_handle = fh;
    return 0;
}

static int iso_vfs_close(void *file_handle) {
    if (!file_handle) return VFS_EINVAL;
    kfree(file_handle);
    return 0;
}

static int iso_vfs_read(void *file_handle, void *buffer, uint32_t count) {
    if (!file_handle || !buffer) return VFS_EINVAL;
    iso9660_file_t *fh = (iso9660_file_t *)file_handle;
    if (fh->is_dir) return VFS_EISDIR;
    if (fh->pos >= fh->file_size) return 0;

    iso9660_slot_t *slot = (iso9660_slot_t *)fh->owner;
    if (!slot || !slot->in_use) return VFS_EIO;

    /* Clamp count to file_size - pos. */
    uint32_t remaining = fh->file_size - fh->pos;
    if (count > remaining) count = remaining;

    uint8_t  sec[ISO9660_LOGICAL_BLOCK_SIZE];
    uint32_t cur_lba = UINT32_MAX;
    uint32_t total = 0;
    uint8_t *out   = (uint8_t *)buffer;

    while (total < count) {
        uint32_t file_off = fh->pos + total;
        uint32_t lba      = fh->extent_lba + (file_off / ISO9660_LOGICAL_BLOCK_SIZE);
        uint32_t in_sec   = file_off % ISO9660_LOGICAL_BLOCK_SIZE;

        if (cur_lba != lba) {
            int rc = slot->bdev->read(slot->bdev->driver_data, lba, 1, sec);
            if (rc < 0) return VFS_EIO;
            cur_lba = lba;
        }
        uint32_t avail = ISO9660_LOGICAL_BLOCK_SIZE - in_sec;
        uint32_t to_copy = count - total;
        if (to_copy > avail) to_copy = avail;
        for (uint32_t i = 0; i < to_copy; i++) out[total + i] = sec[in_sec + i];
        total += to_copy;
    }

    fh->pos += total;
    return (int)total;
}
static int iso_vfs_write(void *file_handle, const void *buffer, uint32_t count) {
    (void)file_handle; (void)buffer; (void)count; return VFS_EACCES;  /* readonly */
}
static int iso_vfs_seek(void *file_handle, int32_t offset, int whence) {
    if (!file_handle) return VFS_EINVAL;
    iso9660_file_t *fh = (iso9660_file_t *)file_handle;

    int64_t new_pos;
    switch (whence) {
        case SEEK_SET: new_pos = offset; break;
        case SEEK_CUR: new_pos = (int64_t)fh->pos + offset; break;
        case SEEK_END: new_pos = (int64_t)fh->file_size + offset; break;
        default:       return VFS_EINVAL;
    }
    if (new_pos < 0) new_pos = 0;
    if (new_pos > (int64_t)fh->file_size) new_pos = (int64_t)fh->file_size;
    fh->pos = (uint32_t)new_pos;
    /* Reset directory walker state if the caller rewinds a readdir. */
    if (fh->is_dir && new_pos == 0) fh->dir_walk_offset = 0;
    return (int)fh->pos;
}
static int iso_vfs_stat(void *fs_private, const char *path, vfs_stat_t *st) {
    if (!fs_private || !path || !st) return VFS_EINVAL;
    iso9660_slot_t *slot = (iso9660_slot_t *)fs_private;
    iso9660_file_t tmp;
    int rc = iso9660_lookup(&slot->parse, path, &tmp);
    if (rc < 0) return rc;
    st->size = tmp.file_size;
    st->type = tmp.is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    return 0;
}
static int iso_vfs_readdir(void *file_handle, vfs_dirent_t *dirent) {
    if (!file_handle || !dirent) return VFS_EINVAL;
    iso9660_file_t *fh = (iso9660_file_t *)file_handle;
    if (!fh->is_dir) return VFS_ENOTDIR;

    iso9660_slot_t *slot = (iso9660_slot_t *)fh->owner;
    if (!slot || !slot->in_use) return VFS_EIO;

    char     name[VFS_MAX_NAME];
    bool     is_dir = false;
    uint32_t size   = 0;

    int rc = iso9660_readdir_step(&slot->parse, fh,
                                  name, VFS_MAX_NAME,
                                  &is_dir, &size);
    if (rc < 0) return rc;
    if (rc == 0) return 0;   /* end of directory */

    /* Fill vfs_dirent_t */
    for (uint32_t i = 0; i < VFS_MAX_NAME; i++) {
        dirent->name[i] = name[i];
        if (!name[i]) break;
    }
    dirent->name[VFS_MAX_NAME - 1] = '\0';
    dirent->size = size;
    dirent->type = is_dir ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    return 1;
}
static int iso_vfs_mkdir(void *fs_private, const char *path) {
    (void)fs_private; (void)path; return VFS_EACCES;
}
static int iso_vfs_unlink(void *fs_private, const char *path) {
    (void)fs_private; (void)path; return VFS_EACCES;
}

static vfs_fs_ops_t iso_vfs_ops = {
    .name     = "iso9660",
    .mount    = iso_vfs_mount,
    .unmount  = iso_vfs_unmount,
    .open     = iso_vfs_open,
    .close    = iso_vfs_close,
    .read     = iso_vfs_read,
    .write    = iso_vfs_write,
    .seek     = iso_vfs_seek,
    .stat     = iso_vfs_stat,
    .readdir  = iso_vfs_readdir,
    .mkdir    = iso_vfs_mkdir,
    .unlink   = iso_vfs_unlink,
};

vfs_fs_ops_t *iso9660_vfs_get_ops(void) { return &iso_vfs_ops; }
