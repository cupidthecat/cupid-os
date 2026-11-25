/**
 * @file iso9660_vfs.h
 * @brief Mount read-only ISO9660 images through the virtual filesystem.
 */

#ifndef ISO9660_VFS_H
#define ISO9660_VFS_H

#include "vfs.h"

/* Returns the fs_ops struct to pass to vfs_register_fs. */
vfs_fs_ops_t *iso9660_vfs_get_ops(void);

#endif
