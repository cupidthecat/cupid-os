/**
 * fat16_vfs.c — FAT16 VFS wrapper for CupidOS
 *
 * Wraps the existing FAT16 driver (root-directory-only flat namespace)
 * into the VFS filesystem operations interface.
 *
 * Limitations inherited from fat16.c:
 *  - Root directory only (no subdirectories)
 *  - 8.3 filenames
 *  - First partition only
 */

#include "fat16_vfs.h"
#include "fat16.h"
#include "vfs.h"
#include "string.h"
#include "memory.h"
#include "../drivers/serial.h"

/* ── FAT16 VFS file handle ────────────────────────────────────────── */

typedef struct {
    fat16_file_t *fat_file;     /* Underlying FAT16 file handle       */
    uint8_t       is_dir;       /* 1 if opened as root directory      */
    int           enum_done;    /* For readdir: 1 if enumeration done */
    /* Write buffering (FAT16 can only replace whole files) */
    char          filename[64]; /* 8.3 filename for write-back        */
    uint8_t      *write_buf;    /* Heap-allocated write buffer         */
    uint32_t      write_len;    /* Bytes written so far                */
    uint32_t      write_cap;    /* Allocated capacity                  */
    bool          dirty;        /* True if writes were made            */
} fat16_vfs_handle_t;

/* ── Readdir callback context ─────────────────────────────────────── */

#define FAT16_VFS_MAX_ENTRIES 128

typedef struct {
    vfs_dirent_t entries[FAT16_VFS_MAX_ENTRIES];
    int          count;
} fat16_vfs_dir_ctx_t;

/* Readdir state stored per-open directory */
typedef struct {
    fat16_vfs_dir_ctx_t ctx;
    int                 index;
} fat16_vfs_dir_handle_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Internal helpers
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Callback for fat16_enumerate_root — collects entries into context.
 */
static int fat16_vfs_enum_cb(const char *name, uint32_t size,
                             uint8_t attr, void *ctx) {
    fat16_vfs_dir_ctx_t *d = (fat16_vfs_dir_ctx_t *)ctx;
    if (d->count >= FAT16_VFS_MAX_ENTRIES) return 1; /* Stop */

    vfs_dirent_t *ent = &d->entries[d->count];
    size_t i = 0;
    while (name[i] && i < VFS_MAX_NAME - 1) {
        ent->name[i] = name[i];
        i++;
    }
    ent->name[i] = '\0';
    ent->size = size;
    ent->type = (attr & FAT_ATTR_DIRECTORY) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    d->count++;
    return 0; /* Continue */
}

/**
 * Strip leading slashes from a path.
 */
static const char *fat16_vfs_strip(const char *path) {
    while (*path == '/') path++;
    return path;
}

/* ══════════════════════════════════════════════════════════════════════
 *  VFS operations implementation
 * ══════════════════════════════════════════════════════════════════════ */

static int fat16_vfs_mount(const char *source, void **fs_private) {
    (void)source;
    /* fat16_init() must have succeeded before mounting. */
    if (!fat16_is_initialized()) {
        return VFS_EIO;
    }
    *fs_private = (void *)1;
    return VFS_OK;
}

static int fat16_vfs_unmount(void *fs_private) {
    (void)fs_private;
    return VFS_OK;
}

static int fat16_vfs_open(void *fs_private, const char *path,
                          uint32_t flags, void **file_handle) {
    (void)fs_private;
    const char *name = fat16_vfs_strip(path);

    /* Empty path or "." => root directory */
    if (name[0] == '\0' || (name[0] == '.' && name[1] == '\0')) {
        fat16_vfs_dir_handle_t *dh = kmalloc(sizeof(fat16_vfs_dir_handle_t));
        if (!dh) return VFS_EIO;
        memset(dh, 0, sizeof(fat16_vfs_dir_handle_t));

        /* Enumerate all entries now */
        fat16_enumerate_root(fat16_vfs_enum_cb, &dh->ctx);
        dh->index = 0;

        fat16_vfs_handle_t *h = kmalloc(sizeof(fat16_vfs_handle_t));
        if (!h) { kfree(dh); return VFS_EIO; }
        memset(h, 0, sizeof(fat16_vfs_handle_t));
        h->fat_file = (fat16_file_t *)dh;  /* Abuse pointer for dir handle */
        h->is_dir = 1;
        h->enum_done = 0;

        *file_handle = h;
        return VFS_OK;
    }

    /* Opening a subdirectory? */
    if (fat16_is_dir(name)) {
        fat16_vfs_dir_handle_t *dh = kmalloc(sizeof(fat16_vfs_dir_handle_t));
        if (!dh) return VFS_EIO;
        memset(dh, 0, sizeof(fat16_vfs_dir_handle_t));

        fat16_enumerate_subdir(name, fat16_vfs_enum_cb, &dh->ctx);
        dh->index = 0;

        fat16_vfs_handle_t *h = kmalloc(sizeof(fat16_vfs_handle_t));
        if (!h) { kfree(dh); return VFS_EIO; }
        memset(h, 0, sizeof(fat16_vfs_handle_t));
        h->fat_file = (fat16_file_t *)dh;
        h->is_dir = 1;

        *file_handle = h;
        return VFS_OK;
    }

    /* Create a new file? */
    if (flags & O_CREAT) {
        /* For create, attempt open first — if fails, write empty file */
        fat16_file_t *f = fat16_open(name);
        if (!f) {
            uint8_t empty = 0;
            if (fat16_write_file(name, &empty, 0) != 0) {
                return VFS_EIO;
            }
            f = fat16_open(name);
            if (!f) return VFS_EIO;
        }

        if (flags & O_TRUNC) {
            /* Truncate: delete and recreate */
            fat16_close(f);
            fat16_delete_file(name);
            uint8_t empty = 0;
            if (fat16_write_file(name, &empty, 0) != 0) {
                return VFS_EIO;
            }
            f = fat16_open(name);
            if (!f) return VFS_EIO;
        }

        fat16_vfs_handle_t *h = kmalloc(sizeof(fat16_vfs_handle_t));
        if (!h) { fat16_close(f); return VFS_EIO; }
        memset(h, 0, sizeof(fat16_vfs_handle_t));
        h->fat_file = f;
        h->is_dir = 0;
        /* Store filename for write-back */
        {
            int k = 0;
            while (name[k] && k < 63) { h->filename[k] = name[k]; k++; }
            h->filename[k] = '\0';
        }
        *file_handle = h;
        return VFS_OK;
    }

    /* Regular open */
    fat16_file_t *f = fat16_open(name);
    if (!f) return VFS_ENOENT;

    fat16_vfs_handle_t *h = kmalloc(sizeof(fat16_vfs_handle_t));
    if (!h) { fat16_close(f); return VFS_EIO; }
    memset(h, 0, sizeof(fat16_vfs_handle_t));
    h->fat_file = f;
    h->is_dir = 0;
    /* Store filename for potential write-back */
    {
        int k = 0;
        while (name[k] && k < 63) { h->filename[k] = name[k]; k++; }
        h->filename[k] = '\0';
    }

    *file_handle = h;
    return VFS_OK;
}

static int fat16_vfs_close(void *file_handle) {
    fat16_vfs_handle_t *h = (fat16_vfs_handle_t *)file_handle;
    if (!h) return VFS_OK;

    if (h->is_dir) {
        /* Free the directory handle */
        kfree(h->fat_file);  /* This is actually fat16_vfs_dir_handle_t* */
    } else {
        /* Flush any buffered writes */
        if (h->dirty && h->write_buf && h->filename[0]) {
            serial_printf("[fat16_vfs_close] flushing '%s', %u bytes\n",
                         h->filename, h->write_len);

            /* Save original file content for rollback */
            fat16_file_t *orig = h->fat_file;
            uint32_t orig_size = orig ? orig->file_size : 0;
            uint8_t *backup = NULL;

            if (orig && orig_size > 0) {
                backup = kmalloc(orig_size);
                if (backup) {
                    /* Rewind and read original content */
                    orig->position = 0;
                    fat16_read(orig, backup, orig_size);
                }
            }

            fat16_close(h->fat_file);
            h->fat_file = NULL;
            int del_rc = fat16_delete_file(h->filename);
            serial_printf("[fat16_vfs_close] delete returned %d\n", del_rc);

            int wr_rc = fat16_write_file(h->filename, h->write_buf, h->write_len);
            serial_printf("[fat16_vfs_close] write returned %d\n", wr_rc);

            if (wr_rc < 0 || (uint32_t)wr_rc != h->write_len) {
                serial_printf("[fat16_vfs_close] ERROR: write failed! Attempting rollback...\n");
                /* Try to restore original file */
                if (backup && orig_size > 0) {
                    int restore_rc = fat16_write_file(h->filename, backup, orig_size);
                    if (restore_rc == 0) {
                        serial_printf("[fat16_vfs_close] Rollback successful\n");
                    } else {
                        serial_printf("[fat16_vfs_close] CRITICAL: Rollback failed! File lost!\n");
                    }
                } else {
                    serial_printf("[fat16_vfs_close] CRITICAL: No backup available! File lost!\n");
                }
            }

            if (backup) kfree(backup);
        }
        if (h->fat_file) fat16_close(h->fat_file);
        if (h->write_buf) kfree(h->write_buf);
    }
    kfree(h);
    return VFS_OK;
}

static int fat16_vfs_read(void *file_handle, void *buffer,
                          uint32_t count) {
    fat16_vfs_handle_t *h = (fat16_vfs_handle_t *)file_handle;
    if (!h || !h->fat_file) return VFS_EINVAL;
    if (h->is_dir) return VFS_EISDIR;

    int result = fat16_read(h->fat_file, buffer, count);
    return result;
}

static int fat16_vfs_write(void *file_handle, const void *buffer,
                           uint32_t count) {
    fat16_vfs_handle_t *h = (fat16_vfs_handle_t *)file_handle;
    if (!h || h->is_dir) return VFS_EINVAL;
    if (count == 0) return 0;

    /* Allocate or grow write buffer as needed */
    uint32_t needed = h->write_len + count;
    if (!h->write_buf) {
        /* Initial allocation — round up to 512 boundary */
        h->write_cap = (needed + 511) & ~(uint32_t)511;
        if (h->write_cap < 1024) h->write_cap = 1024;
        h->write_buf = kmalloc(h->write_cap);
        if (!h->write_buf) return VFS_EIO;
        h->write_len = 0;
    } else if (needed > h->write_cap) {
        /* Grow: double or fit, whichever is larger */
        uint32_t new_cap = h->write_cap * 2;
        if (new_cap < needed) new_cap = (needed + 511) & ~(uint32_t)511;
        uint8_t *nb = kmalloc(new_cap);
        if (!nb) return VFS_EIO;
        memcpy(nb, h->write_buf, h->write_len);
        kfree(h->write_buf);
        h->write_buf = nb;
        h->write_cap = new_cap;
    }

    memcpy(h->write_buf + h->write_len, buffer, count);
    h->write_len += count;
    h->dirty = true;

    return (int)count;
}

static int fat16_vfs_seek(void *file_handle, int32_t offset, int whence) {
    fat16_vfs_handle_t *h = (fat16_vfs_handle_t *)file_handle;
    if (!h || !h->fat_file || h->is_dir) return VFS_EINVAL;

    /* FAT16 driver doesn't have seek — manually adjust position */
    fat16_file_t *f = h->fat_file;
    int32_t new_pos;
    switch (whence) {
        case SEEK_SET: new_pos = offset; break;
        case SEEK_CUR: new_pos = (int32_t)f->position + offset; break;
        case SEEK_END: new_pos = (int32_t)f->file_size + offset; break;
        default: return VFS_EINVAL;
    }
    if (new_pos < 0) new_pos = 0;
    if ((uint32_t)new_pos > f->file_size) new_pos = (int32_t)f->file_size;
    f->position = (uint32_t)new_pos;
    return (int)f->position;
}

static int fat16_vfs_stat(void *fs_private, const char *path,
                          vfs_stat_t *st) {
    (void)fs_private;
    const char *name = fat16_vfs_strip(path);

    /* Root directory */
    if (name[0] == '\0' || (name[0] == '.' && name[1] == '\0')) {
        st->type = VFS_TYPE_DIR;
        st->size = 0;
        return VFS_OK;
    }

    /* Check if it's a directory by scanning the root dir directly */
    {
        int found_as_dir = fat16_is_dir(name);
        if (found_as_dir > 0) {
            st->type = VFS_TYPE_DIR;
            st->size = 0;
            return VFS_OK;
        }
    }

    /* Open file to get size, then close */
    fat16_file_t *f = fat16_open(name);
    if (!f) return VFS_ENOENT;

    st->type = VFS_TYPE_FILE;
    st->size = f->file_size;
    fat16_close(f);
    return VFS_OK;
}

static int fat16_vfs_readdir(void *file_handle, vfs_dirent_t *dirent) {
    fat16_vfs_handle_t *h = (fat16_vfs_handle_t *)file_handle;
    if (!h) return VFS_EINVAL;
    if (!h->is_dir) return VFS_ENOTDIR;

    fat16_vfs_dir_handle_t *dh = (fat16_vfs_dir_handle_t *)h->fat_file;
    if (dh->index >= dh->ctx.count) return 0; /* No more entries */

    /* Copy entry */
    vfs_dirent_t *src = &dh->ctx.entries[dh->index];
    size_t i = 0;
    while (src->name[i] && i < VFS_MAX_NAME - 1) {
        dirent->name[i] = src->name[i];
        i++;
    }
    dirent->name[i] = '\0';
    dirent->size = src->size;
    dirent->type = src->type;
    dh->index++;
    return 1;
}

static int fat16_vfs_mkdir(void *fs_private, const char *path) {
    (void)fs_private;
    const char *name = fat16_vfs_strip(path);
    if (name[0] == '\0') return VFS_EINVAL;
    int rc = fat16_mkdir(name);
    return (rc == 0) ? VFS_OK : VFS_EIO;
}

static int fat16_vfs_unlink(void *fs_private, const char *path) {
    (void)fs_private;
    const char *name = fat16_vfs_strip(path);
    if (name[0] == '\0') return VFS_EINVAL;

    int result = fat16_delete_file(name);
    return (result == 0) ? VFS_OK : VFS_EIO;
}

/* ══════════════════════════════════════════════════════════════════════
 *  VFS operations struct
 * ══════════════════════════════════════════════════════════════════════ */

static vfs_fs_ops_t fat16_vfs_ops = {
    .name     = "fat16",
    .mount    = fat16_vfs_mount,
    .unmount  = fat16_vfs_unmount,
    .open     = fat16_vfs_open,
    .close    = fat16_vfs_close,
    .read     = fat16_vfs_read,
    .write    = fat16_vfs_write,
    .seek     = fat16_vfs_seek,
    .stat     = fat16_vfs_stat,
    .readdir  = fat16_vfs_readdir,
    .mkdir    = fat16_vfs_mkdir,
    .unlink   = fat16_vfs_unlink
};

vfs_fs_ops_t *fat16_vfs_get_ops(void) {
    return &fat16_vfs_ops;
}
