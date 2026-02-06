/**
 * vfs.c — Virtual File System for CupidOS
 *
 * Provides a unified file API across multiple filesystem types
 * (RamFS, DevFS, FAT16) with hierarchical mount points and
 * Linux-style path resolution.
 */

#include "vfs.h"
#include "string.h"
#include "memory.h"
#include "../drivers/serial.h"

/* ── Registered filesystem types ──────────────────────────────────── */
#define VFS_MAX_FS_TYPES 8
static vfs_fs_ops_t *fs_types[VFS_MAX_FS_TYPES];
static int fs_type_count = 0;

/* ── Mount table ──────────────────────────────────────────────────── */
static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static int mount_count = 0;

/* ── File descriptor table ────────────────────────────────────────── */
static vfs_file_t fd_table[VFS_MAX_OPEN_FILES];

/* ══════════════════════════════════════════════════════════════════════
 *  Internal helpers
 * ══════════════════════════════════════════════════════════════════════ */

static size_t vfs_strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static void vfs_strcpy(char *dst, const char *src, size_t max) {
    size_t i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/**
 * Find the mount point with the longest matching prefix for `path`.
 * Returns NULL if no mount matches.
 */
static vfs_mount_t *find_mount(const char *path, const char **rel_path) {
    vfs_mount_t *best = NULL;
    size_t best_len = 0;

    for (int i = 0; i < mount_count; i++) {
        if (!mounts[i].mounted) continue;

        size_t mlen = vfs_strlen(mounts[i].path);

        /* Root "/" matches everything */
        if (mlen == 1 && mounts[i].path[0] == '/') {
            if (!best || best_len < 1) {
                best = &mounts[i];
                best_len = 1;
            }
            continue;
        }

        /* Check prefix match */
        if (strncmp(path, mounts[i].path, mlen) == 0) {
            /* Must be exact or followed by '/' */
            if (path[mlen] == '\0' || path[mlen] == '/') {
                if (mlen > best_len) {
                    best = &mounts[i];
                    best_len = mlen;
                }
            }
        }
    }

    if (best && rel_path) {
        if (best_len == 1 && best->path[0] == '/') {
            /* Root mount — relative path is everything after "/" */
            *rel_path = path + 1;
        } else {
            /* Skip mount prefix and any trailing '/' */
            const char *rp = path + best_len;
            if (*rp == '/') rp++;
            *rel_path = rp;
        }
    }

    return best;
}

/**
 * Find a filesystem type by name.
 */
static vfs_fs_ops_t *find_fs_type(const char *name) {
    for (int i = 0; i < fs_type_count; i++) {
        if (strcmp(fs_types[i]->name, name) == 0) {
            return fs_types[i];
        }
    }
    return NULL;
}

/**
 * Allocate a file descriptor. Returns index or -1.
 */
static int alloc_fd(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!fd_table[i].in_use) {
            memset(&fd_table[i], 0, sizeof(vfs_file_t));
            fd_table[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════════ */

int vfs_init(void) {
    memset(mounts, 0, sizeof(mounts));
    memset(fd_table, 0, sizeof(fd_table));
    memset(fs_types, 0, sizeof(fs_types));
    mount_count = 0;
    fs_type_count = 0;
    KINFO("VFS initialized");
    return VFS_OK;
}

int vfs_register_fs(vfs_fs_ops_t *ops) {
    if (!ops || !ops->name) return VFS_EINVAL;
    if (fs_type_count >= VFS_MAX_FS_TYPES) return VFS_ENOSPC;

    fs_types[fs_type_count++] = ops;
    KINFO("VFS: registered filesystem '%s'", ops->name);
    return VFS_OK;
}

int vfs_mount(const char *source, const char *target,
              const char *fs_type) {
    if (!target || !fs_type) return VFS_EINVAL;
    if (mount_count >= VFS_MAX_MOUNTS) return VFS_ENOSPC;

    vfs_fs_ops_t *ops = find_fs_type(fs_type);
    if (!ops) {
        KERROR("VFS: unknown filesystem type '%s'", fs_type);
        return VFS_EINVAL;
    }

    vfs_mount_t *m = &mounts[mount_count];
    vfs_strcpy(m->path, target, VFS_MAX_PATH);
    m->ops = ops;
    m->fs_private = NULL;

    /* Call filesystem mount */
    if (ops->mount) {
        int rc = ops->mount(source, &m->fs_private);
        if (rc < 0) {
            KERROR("VFS: mount '%s' at '%s' failed (%d)",
                   fs_type, target, rc);
            return rc;
        }
    }

    m->mounted = 1;
    mount_count++;

    KINFO("VFS: mounted '%s' at '%s'", fs_type, target);
    return VFS_OK;
}

/* ── File operations ──────────────────────────────────────────────── */

int vfs_open(const char *path, uint32_t flags) {
    if (!path || path[0] != '/') return VFS_EINVAL;

    const char *rel_path = NULL;
    vfs_mount_t *m = find_mount(path, &rel_path);
    if (!m) return VFS_ENOENT;
    if (!m->ops->open) return VFS_ENOSYS;

    int fd = alloc_fd();
    if (fd < 0) return VFS_EMFILE;

    void *handle = NULL;
    int rc = m->ops->open(m->fs_private, rel_path, flags, &handle);
    if (rc < 0) {
        fd_table[fd].in_use = 0;
        return rc;
    }

    fd_table[fd].flags = flags;
    fd_table[fd].position = 0;
    fd_table[fd].fs_data = handle;
    fd_table[fd].mount = m;

    return fd;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES) return VFS_EINVAL;
    if (!fd_table[fd].in_use) return VFS_EINVAL;

    int rc = VFS_OK;
    if (fd_table[fd].mount && fd_table[fd].mount->ops->close) {
        rc = fd_table[fd].mount->ops->close(fd_table[fd].fs_data);
    }

    fd_table[fd].in_use = 0;
    fd_table[fd].fs_data = NULL;
    fd_table[fd].mount = NULL;
    return rc;
}

int vfs_read(int fd, void *buffer, uint32_t count) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES) return VFS_EINVAL;
    if (!fd_table[fd].in_use) return VFS_EINVAL;
    if (!buffer) return VFS_EINVAL;

    vfs_file_t *f = &fd_table[fd];
    if (!f->mount || !f->mount->ops->read) return VFS_ENOSYS;

    int rc = f->mount->ops->read(f->fs_data, buffer, count);
    if (rc > 0) {
        f->position += (uint32_t)rc;
    }
    return rc;
}

int vfs_write(int fd, const void *buffer, uint32_t count) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES) return VFS_EINVAL;
    if (!fd_table[fd].in_use) return VFS_EINVAL;
    if (!buffer) return VFS_EINVAL;

    vfs_file_t *f = &fd_table[fd];
    if (!f->mount || !f->mount->ops->write) return VFS_ENOSYS;

    int rc = f->mount->ops->write(f->fs_data, buffer, count);
    if (rc > 0) {
        f->position += (uint32_t)rc;
    }
    return rc;
}

int vfs_seek(int fd, int32_t offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES) return VFS_EINVAL;
    if (!fd_table[fd].in_use) return VFS_EINVAL;

    vfs_file_t *f = &fd_table[fd];
    if (!f->mount || !f->mount->ops->seek) return VFS_ENOSYS;

    return f->mount->ops->seek(f->fs_data, offset, whence);
}

int vfs_stat(const char *path, vfs_stat_t *st) {
    if (!path || path[0] != '/' || !st) return VFS_EINVAL;

    const char *rel_path = NULL;
    vfs_mount_t *m = find_mount(path, &rel_path);
    if (!m) return VFS_ENOENT;
    if (!m->ops->stat) return VFS_ENOSYS;

    return m->ops->stat(m->fs_private, rel_path, st);
}

int vfs_readdir(int fd, vfs_dirent_t *dirent) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES) return VFS_EINVAL;
    if (!fd_table[fd].in_use) return VFS_EINVAL;
    if (!dirent) return VFS_EINVAL;

    vfs_file_t *f = &fd_table[fd];
    if (!f->mount || !f->mount->ops->readdir) return VFS_ENOSYS;

    return f->mount->ops->readdir(f->fs_data, dirent);
}

int vfs_mkdir(const char *path) {
    if (!path || path[0] != '/') return VFS_EINVAL;

    const char *rel_path = NULL;
    vfs_mount_t *m = find_mount(path, &rel_path);
    if (!m) return VFS_ENOENT;
    if (!m->ops->mkdir) return VFS_ENOSYS;

    return m->ops->mkdir(m->fs_private, rel_path);
}

int vfs_unlink(const char *path) {
    if (!path || path[0] != '/') return VFS_EINVAL;

    const char *rel_path = NULL;
    vfs_mount_t *m = find_mount(path, &rel_path);
    if (!m) return VFS_ENOENT;
    if (!m->ops->unlink) return VFS_ENOSYS;

    return m->ops->unlink(m->fs_private, rel_path);
}

/* ── Query ────────────────────────────────────────────────────────── */

int vfs_mount_count(void) {
    return mount_count;
}

const vfs_mount_t *vfs_get_mount(int index) {
    if (index < 0 || index >= mount_count) return NULL;
    return &mounts[index];
}
