/**
 * fat16_vfs.cc - FAT16 VFS wrapper for Cupid OS
 *
 * Wraps the existing FAT16 driver (root-directory-only flat namespace)
 * into the VFS filesystem operations interface.
 *
 * Limitations inherited from fat16.cc:
 *  - Root directory only (no subdirectories)
 *  - 8.3 filenames
 *  - First partition only
*/

#include "fat16_vfs.h"
#include "fat16.h"
#include "fat16_control.h"
#include "vfs.h"
#include "string.h"
#include "memory.h"
#include "serial.h"

typedef struct {
    fat16_file_t *fat_file;     /* Underlying FAT16 file handle */
    uint8_t       is_dir;       /* 1 if opened as root directory */
    int           enum_done;    /* For readdir: 1 if enumeration done */
    char          filename[64]; /* 8.3 filename for write-back */
    uint8_t      *write_buf;    /* Heap-allocated write buffer */
    uint32_t      write_len;    /* Bytes written so far */
    uint32_t      write_cap;    /* Allocated capacity */
    uint32_t      cursor;       /* Logical file position */
    bool          dirty;        /* True if writes were made */
    bool          writable;     /* Opened with write access */
} fat16_vfs_handle_t;

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

/* Internal helpers */

/**
 * Callback for fat16_enumerate_root - collects entries into context.
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

static int fat16_vfs_name_char(char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9')) return 1;
    return c == '_' || c == '-' || c == '~' || c == '$' || c == '!' ||
           c == '#' || c == '%' || c == '&' || c == '@' || c == '^' ||
           c == '`' || c == '{' || c == '}' || c == '(' || c == ')';
}

static int fat16_vfs_canonical_component(const char **cursor, char *output,
                                         uint32_t *output_length) {
    const char *p = *cursor;
    uint32_t base_length = 0;
    uint32_t extension_length = 0;
    int in_extension = 0;

    while (*p && *p != '/') {
        char c = *p++;
        if (c == '.') {
            if (in_extension || base_length == 0) return VFS_EINVAL;
            in_extension = 1;
            output[(*output_length)++] = '.';
            continue;
        }
        if (!fat16_vfs_name_char(c)) return VFS_EINVAL;
        if (!in_extension) {
            if (base_length >= 8u) return VFS_EINVAL;
            base_length++;
        } else {
            if (extension_length >= 3u) return VFS_EINVAL;
            extension_length++;
        }
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        output[(*output_length)++] = c;
    }

    if (base_length == 0 || (in_extension && extension_length == 0)) {
        return VFS_EINVAL;
    }
    *cursor = p;
    return VFS_OK;
}

static int fat16_vfs_canonical_path(const char *path, char output[64]) {
    const char *cursor = path;
    uint32_t output_length = 0;
    if (!path || !path[0]) return VFS_EINVAL;

    int rc = fat16_vfs_canonical_component(
        &cursor, output, &output_length);
    if (rc < 0) return rc;
    if (*cursor == '/') {
        cursor++;
        if (!*cursor) return VFS_EINVAL;
        output[output_length++] = '/';
        rc = fat16_vfs_canonical_component(
            &cursor, output, &output_length);
        if (rc < 0) return rc;
    }
    if (*cursor) return VFS_EINVAL;
    output[output_length] = '\0';
    return VFS_OK;
}

static int fat16_vfs_has_subdirectory(const char *path) {
    while (*path) {
        if (*path++ == '/') return 1;
    }
    return 0;
}

static int fat16_vfs_map_open_status(int status) {
    if (status == FAT16_OPEN_NOT_FOUND) return VFS_ENOENT;
    if (status == FAT16_OPEN_NO_HANDLES) return VFS_EMFILE;
    if (status == FAT16_OPEN_INVALID) return VFS_EINVAL;
    if (status == FAT16_OPEN_BUSY) return VFS_EBUSY;
    return VFS_EIO;
}

static uint32_t fat16_vfs_round_capacity(uint32_t need) {
    if (need > 0xfffffdffu) return 0;
    uint32_t cap = (need + 511u) & ~(uint32_t)511u;
    if (cap < 1024u) {
        cap = 1024u;
    }
    return cap;
}

static int fat16_vfs_ensure_capacity(fat16_vfs_handle_t *h, uint32_t need) {
    if (!h) return VFS_EINVAL;
    if (need <= h->write_cap) return VFS_OK;

    uint32_t new_cap = h->write_cap ? h->write_cap : 1024u;
    while (new_cap < need) {
        if (new_cap > 0x7fffffffu) {
            new_cap = need;
            break;
        }
        new_cap *= 2u;
    }

    uint8_t *nb = kmalloc(new_cap);
    if (!nb) return VFS_EIO;

    if (h->write_buf && h->write_len > 0) {
        memcpy(nb, h->write_buf, h->write_len);
    }
    if (new_cap > h->write_len) {
        memset(nb + h->write_len, 0, new_cap - h->write_len);
    }

    if (h->write_buf) {
        kfree(h->write_buf);
    }
    h->write_buf = nb;
    h->write_cap = new_cap;
    return VFS_OK;
}

static int fat16_vfs_prepare_write_buffer(fat16_vfs_handle_t *h,
                                          uint32_t flags) {
    if (!h || h->is_dir) return VFS_EINVAL;
    if (h->write_buf) return VFS_OK;

    uint32_t initial_len = 0;
    if (!(flags & O_TRUNC) && h->fat_file) {
        initial_len = h->fat_file->file_size;
    }

    h->write_cap = fat16_vfs_round_capacity(initial_len);
    if (h->write_cap == 0) return VFS_ENOSPC;
    h->write_buf = kmalloc(h->write_cap);
    if (!h->write_buf) return VFS_EIO;
    memset(h->write_buf, 0, h->write_cap);

    if (initial_len > 0) {
        uint32_t saved_pos = h->fat_file->position;
        h->fat_file->position = 0;
        int rc = fat16_read(h->fat_file, h->write_buf, initial_len);
        h->fat_file->position = saved_pos;
        if (rc < 0 || (uint32_t)rc != initial_len) {
            kfree(h->write_buf);
            h->write_buf = NULL;
            h->write_cap = 0;
            return VFS_EIO;
        }
    }

    h->write_len = initial_len;
    h->cursor = (flags & O_APPEND) ? initial_len : 0;
    h->dirty = (flags & O_TRUNC) != 0;
    return VFS_OK;
}

/* VFS operations implementation */

static int fat16_vfs_mount(const char *source, void **fs_private) {
    (void)source;
    if (!fat16_is_initialized()) {
        if (fat16_init() != 0) {
            return VFS_EIO;
        }
    }

    /* We don't need per-mount state; use a sentinel pointer. */
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
    if (!path || !file_handle) return VFS_EINVAL;
    const char *raw_name = fat16_vfs_strip(path);

    /* Empty path or "." => root directory */
    if (raw_name[0] == '\0' ||
        (raw_name[0] == '.' && raw_name[1] == '\0')) {
        if (flags & (O_WRONLY | O_RDWR | O_APPEND | O_TRUNC)) {
            return VFS_EISDIR;
        }
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

    char name[64];
    if (fat16_vfs_canonical_path(raw_name, name) < 0) return VFS_EINVAL;
    if (fat16_file_is_reserved(name) &&
        (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND))) {
        return VFS_EBUSY;
    }

    /* Opening a subdirectory? */
    if (fat16_is_dir(name)) {
        if (flags & (O_WRONLY | O_RDWR | O_APPEND | O_TRUNC)) {
            return VFS_EISDIR;
        }
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
        fat16_file_t *f = NULL;
        int open_status = fat16_open_checked(name, &f);
        if (open_status == FAT16_OPEN_NOT_FOUND) {
            uint8_t empty = 0;
            if (fat16_write_file(name, &empty, 0) != 0) {
                return VFS_EIO;
            }
            open_status = fat16_open_checked(name, &f);
        }
        if (open_status != FAT16_OPEN_OK)
            return fat16_vfs_map_open_status(open_status);

        fat16_vfs_handle_t *h = kmalloc(sizeof(fat16_vfs_handle_t));
        if (!h) { fat16_close(f); return VFS_EIO; }
        memset(h, 0, sizeof(fat16_vfs_handle_t));
        h->fat_file = f;
        h->is_dir = 0;
        h->writable = (flags & (O_WRONLY | O_RDWR | O_APPEND)) != 0;
        strcpy(h->filename, name);
        if (h->writable && fat16_vfs_prepare_write_buffer(h, flags) < 0) {
            fat16_close(f);
            kfree(h);
            return VFS_EIO;
        }
        *file_handle = h;
        return VFS_OK;
    }

    /* Regular open */
    fat16_file_t *f = NULL;
    int open_status = fat16_open_checked(name, &f);
    if (open_status != FAT16_OPEN_OK)
        return fat16_vfs_map_open_status(open_status);

    fat16_vfs_handle_t *h = kmalloc(sizeof(fat16_vfs_handle_t));
    if (!h) { fat16_close(f); return VFS_EIO; }
    memset(h, 0, sizeof(fat16_vfs_handle_t));
    h->fat_file = f;
    h->is_dir = 0;
    h->writable = (flags & (O_WRONLY | O_RDWR | O_APPEND)) != 0;
    strcpy(h->filename, name);
    if (h->writable && fat16_vfs_prepare_write_buffer(h, flags) < 0) {
        fat16_close(f);
        kfree(h);
        return VFS_EIO;
    }

    *file_handle = h;
    return VFS_OK;
}

static int fat16_vfs_close(void *file_handle) {
    fat16_vfs_handle_t *h = (fat16_vfs_handle_t *)file_handle;
    int status = VFS_OK;
    if (!h) return VFS_OK;

    if (h->is_dir) {
        /* Free the directory handle */
        kfree(h->fat_file);  /* This is actually fat16_vfs_dir_handle_t* */
    } else {
        /* Flush any buffered writes */
        if (h->dirty && h->write_buf && h->filename[0]) {
            serial_printf("[fat16_vfs_close] flushing '%s', %u bytes\n",
                         h->filename, h->write_len);
            fat16_close(h->fat_file);
            h->fat_file = NULL;

            {
                uint8_t empty = 0;
                const void *src = h->write_len > 0 ? (const void *)h->write_buf
                                                   : (const void *)&empty;
                int wr_rc = fat16_write_file(h->filename, src, h->write_len);
                serial_printf("[fat16_vfs_close] write returned %d\n", wr_rc);

                if (wr_rc == FAT16_BUSY) {
                    serial_printf("[fat16_vfs_close] write blocked by a live reader\n");
                    status = VFS_EBUSY;
                } else if (wr_rc < 0 || (uint32_t)wr_rc != h->write_len) {
                    serial_printf("[fat16_vfs_close] write failed; previous entry retained\n");
                    status = VFS_EIO;
                }
            }
        }
        if (h->fat_file) fat16_close(h->fat_file);
        if (h->write_buf) kfree(h->write_buf);
    }
    kfree(h);
    return status;
}

static int fat16_vfs_read(void *file_handle, void *buffer,
                          uint32_t count) {
    fat16_vfs_handle_t *h = (fat16_vfs_handle_t *)file_handle;
    if (!h) return VFS_EINVAL;
    if (h->is_dir) return VFS_EISDIR;

    if (h->write_buf) {
        if (h->cursor >= h->write_len) return 0;
        if (count > h->write_len - h->cursor) {
            count = h->write_len - h->cursor;
        }
        memcpy(buffer, h->write_buf + h->cursor, count);
        h->cursor += count;
        return (int)count;
    }
    if (!h->fat_file) return VFS_EINVAL;

    int result = fat16_read(h->fat_file, buffer, count);
    return result;
}

static int fat16_vfs_write(void *file_handle, const void *buffer,
                           uint32_t count) {
    fat16_vfs_handle_t *h = (fat16_vfs_handle_t *)file_handle;
    if (!h || h->is_dir) return VFS_EINVAL;
    if (count == 0) return 0;
    if (!h->writable) return VFS_EACCES;

    if (!h->write_buf) {
        int prep_rc = fat16_vfs_prepare_write_buffer(h, 0);
        if (prep_rc < 0) return prep_rc;
    }

    if (count > 0xffffffffu - h->cursor) return VFS_ENOSPC;
    uint32_t end = h->cursor + count;
    if (end > 0x7fffffffu) return VFS_ENOSPC;
    if (end > h->write_cap) {
        int grow_rc = fat16_vfs_ensure_capacity(h, end);
        if (grow_rc < 0) return grow_rc;
    }

    if (h->cursor > h->write_len) {
        memset(h->write_buf + h->write_len, 0, h->cursor - h->write_len);
    }

    memcpy(h->write_buf + h->cursor, buffer, count);
    h->cursor = end;
    if (h->cursor > h->write_len) {
        h->write_len = h->cursor;
    }
    h->dirty = true;

    return (int)count;
}

static int fat16_vfs_seek(void *file_handle, int32_t offset, int whence) {
    fat16_vfs_handle_t *h = (fat16_vfs_handle_t *)file_handle;
    if (!h || h->is_dir) return VFS_EINVAL;

    if (h->write_buf) {
        uint32_t base = 0;
        uint32_t next;
        switch (whence) {
            case SEEK_SET: base = 0; break;
            case SEEK_CUR: base = h->cursor; break;
            case SEEK_END: base = h->write_len; break;
            default: return VFS_EINVAL;
        }
        if (offset < 0) {
            uint32_t magnitude = (uint32_t)(-(offset + 1)) + 1u;
            next = magnitude > base ? 0 : base - magnitude;
        } else {
            if ((uint32_t)offset > 0x7fffffffu - base) return VFS_EINVAL;
            next = base + (uint32_t)offset;
        }
        h->cursor = next;
        return (int)h->cursor;
    }
    if (!h->fat_file) return VFS_EINVAL;

    /* FAT16 driver doesn't have seek - manually adjust position */
    fat16_file_t *f = h->fat_file;
    uint32_t base;
    uint32_t new_pos;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = f->position; break;
        case SEEK_END: base = f->file_size; break;
        default: return VFS_EINVAL;
    }
    if (offset < 0) {
        uint32_t magnitude = (uint32_t)(-(offset + 1)) + 1u;
        new_pos = magnitude > base ? 0 : base - magnitude;
    } else {
        if ((uint32_t)offset > 0x7fffffffu - base) return VFS_EINVAL;
        new_pos = base + (uint32_t)offset;
    }
    if (new_pos > f->file_size) new_pos = f->file_size;
    f->position = new_pos;
    return (int)f->position;
}

static int fat16_vfs_stat(void *fs_private, const char *path,
                          vfs_stat_t *st) {
    (void)fs_private;
    if (!path || !st) return VFS_EINVAL;
    const char *raw_name = fat16_vfs_strip(path);

    /* Root directory */
    if (raw_name[0] == '\0' ||
        (raw_name[0] == '.' && raw_name[1] == '\0')) {
        st->type = VFS_TYPE_DIR;
        st->size = 0;
        return VFS_OK;
    }

    char name[64];
    if (fat16_vfs_canonical_path(raw_name, name) < 0) return VFS_EINVAL;

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
    fat16_file_t *f = NULL;
    int open_status = fat16_open_checked(name, &f);
    if (open_status != FAT16_OPEN_OK)
        return fat16_vfs_map_open_status(open_status);

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
    if (!path) return VFS_EINVAL;
    const char *raw_name = fat16_vfs_strip(path);
    char name[64];
    if (fat16_vfs_canonical_path(raw_name, name) < 0 ||
        fat16_vfs_has_subdirectory(name)) return VFS_EINVAL;
    int rc = fat16_mkdir(name);
    return (rc == 0) ? VFS_OK : VFS_EIO;
}

static int fat16_vfs_unlink(void *fs_private, const char *path) {
    (void)fs_private;
    if (!path) return VFS_EINVAL;
    const char *raw_name = fat16_vfs_strip(path);
    char name[64];
    if (fat16_vfs_canonical_path(raw_name, name) < 0) return VFS_EINVAL;

    int result = fat16_delete_file(name);
    if (result == FAT16_BUSY) return VFS_EBUSY;
    return (result == 0) ? VFS_OK : VFS_EIO;
}

/* VFS operations struct */

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
