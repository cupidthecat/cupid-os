#ifndef VFS_H
#define VFS_H

#include "types.h"

/* ══════════════════════════════════════════════════════════════════════
 *  vfs.h — Virtual File System for CupidOS
 *
 *  Linux-style VFS providing unified file API across multiple
 *  filesystem types with hierarchical mount points.
 * ══════════════════════════════════════════════════════════════════════ */

/* ── Error codes (negative) ───────────────────────────────────────── */
#define VFS_OK       0
#define VFS_ENOENT  -2    /* No such file or directory */
#define VFS_EACCES  -13   /* Permission denied         */
#define VFS_EEXIST  -17   /* File exists               */
#define VFS_ENOTDIR -20   /* Not a directory           */
#define VFS_EISDIR  -21   /* Is a directory            */
#define VFS_EINVAL  -22   /* Invalid argument          */
#define VFS_EMFILE  -24   /* Too many open files       */
#define VFS_ENOSPC  -28   /* No space left on device   */
#define VFS_EIO     -5    /* I/O error                 */
#define VFS_ENOSYS  -38   /* Function not implemented  */

/* ── Open flags ───────────────────────────────────────────────────── */
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0100
#define O_TRUNC    0x0200
#define O_APPEND   0x0400

/* ── Seek whence ──────────────────────────────────────────────────── */
#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2

/* ── File types ───────────────────────────────────────────────────── */
#define VFS_TYPE_FILE   0
#define VFS_TYPE_DIR    1
#define VFS_TYPE_DEV    2

/* ── Limits ───────────────────────────────────────────────────────── */
#define VFS_MAX_OPEN_FILES 64
#define VFS_MAX_MOUNTS     16
#define VFS_MAX_PATH       128
#define VFS_MAX_NAME       64

/* ── Forward declarations ─────────────────────────────────────────── */
struct vfs_mount;

/* ── Directory entry ──────────────────────────────────────────────── */
typedef struct {
    char     name[VFS_MAX_NAME];
    uint32_t size;
    uint8_t  type;    /* VFS_TYPE_FILE, VFS_TYPE_DIR, VFS_TYPE_DEV */
} vfs_dirent_t;

/* ── File statistics ──────────────────────────────────────────────── */
typedef struct {
    uint32_t size;
    uint8_t  type;
} vfs_stat_t;

/* ── Filesystem operations interface ──────────────────────────────── */
typedef struct {
    const char *name;  /* "ramfs", "devfs", "fat16" */

    int (*mount)(const char *source, void **fs_private);
    int (*unmount)(void *fs_private);

    int (*open)(void *fs_private, const char *path, uint32_t flags,
                void **file_handle);
    int (*close)(void *file_handle);
    int (*read)(void *file_handle, void *buffer, uint32_t count);
    int (*write)(void *file_handle, const void *buffer, uint32_t count);
    int (*seek)(void *file_handle, int32_t offset, int whence);

    int (*stat)(void *fs_private, const char *path, vfs_stat_t *st);
    int (*readdir)(void *file_handle, vfs_dirent_t *dirent);
    int (*mkdir)(void *fs_private, const char *path);
    int (*unlink)(void *fs_private, const char *path);
} vfs_fs_ops_t;

/* ── Mount point entry ────────────────────────────────────────────── */
typedef struct vfs_mount {
    char           path[VFS_MAX_PATH]; /* mount path, e.g. "/home" */
    vfs_fs_ops_t  *ops;
    void          *fs_private;
    uint8_t        mounted;
} vfs_mount_t;

/* ── VFS file handle ──────────────────────────────────────────────── */
typedef struct {
    uint32_t       flags;
    uint32_t       position;
    void          *fs_data;       /* fs-specific file handle */
    vfs_mount_t   *mount;
    uint8_t        in_use;
} vfs_file_t;

/* ── Public API ───────────────────────────────────────────────────── */

/* Initialize VFS subsystem */
int vfs_init(void);

/* Register a filesystem type */
int vfs_register_fs(vfs_fs_ops_t *ops);

/* Mount a filesystem */
int vfs_mount(const char *source, const char *target,
              const char *fs_type);

/* File operations — return fd (>= 0) or negative error */
int vfs_open(const char *path, uint32_t flags);
int vfs_close(int fd);
int vfs_read(int fd, void *buffer, uint32_t count);
int vfs_write(int fd, const void *buffer, uint32_t count);
int vfs_seek(int fd, int32_t offset, int whence);

/* Metadata */
int vfs_stat(const char *path, vfs_stat_t *st);

/* Directory operations */
int vfs_readdir(int fd, vfs_dirent_t *dirent);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rename(const char *old_path, const char *new_path);

/* Query */
int vfs_mount_count(void);
const vfs_mount_t *vfs_get_mount(int index);

#endif /* VFS_H */
