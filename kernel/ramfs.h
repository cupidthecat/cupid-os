#ifndef RAMFS_H
#define RAMFS_H

#include "types.h"
#include "vfs.h"

/* ══════════════════════════════════════════════════════════════════════
 *  ramfs.h — In-memory filesystem for CupidOS
 *
 *  Provides a simple RAM-based filesystem with directory tree support.
 *  Used for /, /bin, /tmp mount points.
 * ══════════════════════════════════════════════════════════════════════ */

#define RAMFS_MAX_FILES   128
#define RAMFS_MAX_DATA    (64 * 1024)  /* 64KB max per file */

/* Get the VFS operations struct for ramfs */
vfs_fs_ops_t *ramfs_get_ops(void);

/* Add a pre-populated file to a mounted ramfs instance.
 * `fs_private` is the ramfs instance from mount.
 * Data is copied into the ramfs, caller retains ownership of original. */
int ramfs_add_file(void *fs_private, const char *path,
                   const void *data, uint32_t size);

#endif /* RAMFS_H */
