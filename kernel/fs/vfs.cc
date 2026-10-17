/**
 * vfs.cc - Virtual File System for Cupid OS
 *
 * Provides a unified file API across multiple filesystem types
 * (RamFS, DevFS, FAT16) with hierarchical mount points and
 * Linux-style path resolution.
*/

#include "vfs.h"
#include "string.h"
#include "memory.h"
#include "serial.h"

/* Registered filesystem types */
#define VFS_MAX_FS_TYPES 8
static vfs_fs_ops_t *fs_types[VFS_MAX_FS_TYPES];
static int fs_type_count = 0;

/* Mount table */
static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static int mount_count = 0;

/* File descriptor table */
static vfs_file_t fd_table[VFS_MAX_OPEN_FILES];

/*
 *  Internal helpers
*/

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
            /* Root mount - relative path is everything after "/" */
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

/*
 *  Public API
*/

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

int vfs_umount(const char *target) {
    if (!target) return VFS_EINVAL;

    /* Find a mount whose path exactly matches target. */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        vfs_mount_t *m = &mounts[i];
        if (!m->mounted) continue;
        if (strcmp(m->path, target) != 0) continue;

        /* Close any open files rooted at this mount. */
        int close_status = VFS_OK;
        for (int fd = 0; fd < VFS_MAX_OPEN_FILES; fd++) {
            if (fd_table[fd].in_use && fd_table[fd].mount == m) {
                int fd_status = vfs_close(fd);
                if (fd_status < 0 && close_status == VFS_OK) {
                    close_status = fd_status;
                }
            }
        }
        if (close_status < 0) {
            KERROR("VFS: close failed while unmounting '%s' (%d)",
                   target, close_status);
            return close_status;
        }

        int rc = 0;
        if (m->ops && m->ops->unmount) {
            rc = m->ops->unmount(m->fs_private);
        }
        if (rc < 0) {
            KERROR("VFS: unmount '%s' failed (%d)", target, rc);
            return rc;
        }
        m->mounted    = 0;
        m->fs_private = NULL;
        m->path[0]    = '\0';
        m->ops        = NULL;

        KINFO("VFS: unmounted '%s'", target);
        return rc;
    }
    return VFS_ENOENT;
}

/* File operations */

int vfs_open(const char *path, uint32_t flags) {
    KDEBUG("vfs_open path='%s' flags=0x%x", path ? path : "(null)", flags);

    if (!path || path[0] != '/') {
        KDEBUG("vfs_open EINVAL: bad path");
        return VFS_EINVAL;
    }
    uint32_t access = flags & (O_WRONLY | O_RDWR);
    uint32_t known = O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND;
    if ((flags & ~known) != 0u || access == (O_WRONLY | O_RDWR) ||
        ((flags & (O_TRUNC | O_APPEND)) != 0u && access == O_RDONLY)) {
        KDEBUG("vfs_open EINVAL: invalid flags=0x%x", flags);
        return VFS_EINVAL;
    }

    const char *rel_path = NULL;
    vfs_mount_t *m = find_mount(path, &rel_path);
    if (!m) {
        KDEBUG("vfs_open ENOENT: no mount for '%s'", path);
        return VFS_ENOENT;
    }
    if (!m->ops->open) {
        KDEBUG("vfs_open ENOSYS: no open op");
        return VFS_ENOSYS;
    }

    int fd = alloc_fd();
    if (fd < 0) {
        KDEBUG("vfs_open EMFILE: no free fd");
        return VFS_EMFILE;
    }

    void *handle = NULL;
    int rc = m->ops->open(m->fs_private, rel_path, flags, &handle);
    if (rc < 0) {
        KDEBUG("vfs_open open failed: rc=%d", rc);
        fd_table[fd].in_use = 0;
        return rc;
    }

    fd_table[fd].flags = flags;
    fd_table[fd].position = 0;
    fd_table[fd].fs_data = handle;
    fd_table[fd].mount = m;

    KDEBUG("vfs_open success: fd=%d", fd);
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
    if ((f->flags & (O_WRONLY | O_RDWR)) == O_WRONLY) return VFS_EACCES;
    if (!f->mount || !f->mount->ops->read) return VFS_ENOSYS;

    int rc = f->mount->ops->read(f->fs_data, buffer, count);
    if (rc > 0) {
        f->position += (uint32_t)rc;
    }
    return rc;
}

int vfs_write(int fd, const void *buffer, uint32_t count) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES) {
        KDEBUG("vfs_write EINVAL: bad fd=%d", fd);
        return VFS_EINVAL;
    }
    if (!fd_table[fd].in_use) {
        KDEBUG("vfs_write EINVAL: fd=%d not in use", fd);
        return VFS_EINVAL;
    }
    if (!buffer) {
        KDEBUG("vfs_write EINVAL: null buffer");
        return VFS_EINVAL;
    }

    KDEBUG("vfs_write fd=%d count=%u buffer=%p", fd, count, buffer);

    vfs_file_t *f = &fd_table[fd];
    if ((f->flags & (O_WRONLY | O_RDWR)) == O_RDONLY) return VFS_EACCES;
    if (!f->mount || !f->mount->ops->write) {
        KDEBUG("vfs_write ENOSYS: no write op");
        return VFS_ENOSYS;
    }
    if ((f->flags & O_APPEND) != 0u) {
        if (!f->mount->ops->seek) return VFS_ENOSYS;
        int append_pos = f->mount->ops->seek(f->fs_data, 0, SEEK_END);
        if (append_pos < 0) return append_pos;
        f->position = (uint32_t)append_pos;
    }

    int rc = f->mount->ops->write(f->fs_data, buffer, count);
    KDEBUG("vfs_write returned rc=%d", rc);
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

/* Rename / Move */

int vfs_rename(const char *old_path, const char *new_path) {
    if (!old_path || old_path[0] != '/' ||
        !new_path || new_path[0] != '/') return VFS_EINVAL;

    /* Stat the source to confirm it exists and is a file */
    vfs_stat_t st;
    int rc = vfs_stat(old_path, &st);
    if (rc < 0) return rc;
    if (st.type == VFS_TYPE_DIR) return VFS_EISDIR; /* dirs not yet supported */
    if (strcmp(old_path, new_path) == 0) return VFS_OK;

    const char *old_relative = NULL;
    const char *new_relative = NULL;
    vfs_mount_t *old_mount = find_mount(old_path, &old_relative);
    vfs_mount_t *new_mount = find_mount(new_path, &new_relative);
    if (!old_mount || !new_mount) return VFS_ENOENT;
    if (old_mount == new_mount && old_mount->ops->rename) {
        return old_mount->ops->rename(old_mount->fs_private,
                                      old_relative, new_relative);
    }
    return old_mount == new_mount ? VFS_ENOSYS : VFS_EXDEV;
}

/* Query */

int vfs_mount_count(void) {
    return mount_count;
}

const vfs_mount_t *vfs_get_mount(int index) {
    if (index < 0 || index >= mount_count) return NULL;
    return &mounts[index];
}
