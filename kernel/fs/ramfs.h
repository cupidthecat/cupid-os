#ifndef RAMFS_H
#define RAMFS_H

#include "types.h"
#include "vfs.h"

/**
 * @file ramfs.h
 * @brief In-memory filesystem for boot files and temporary storage.
 */

/** Maximum file size for writes and seeks. */
#define RAMFS_MAX_DATA (1024u * 1024u)

/* Get the VFS operations struct for ramfs */
vfs_fs_ops_t *ramfs_get_ops(void);

/* Add a pre-populated file to a mounted ramfs instance.
 * `fs_private` is the ramfs instance from mount.
 * Data is copied into the ramfs, caller retains ownership of original.*/
int ramfs_add_file(void *fs_private, const char *path,
                   const void *data, uint32_t size);

#endif /* RAMFS_H */
