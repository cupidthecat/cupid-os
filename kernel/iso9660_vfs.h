/* kernel/iso9660_vfs.h — VFS adapter for ISO9660. */
#ifndef ISO9660_VFS_H
#define ISO9660_VFS_H

#include "vfs.h"

/* Returns the fs_ops struct to pass to vfs_register_fs. */
vfs_fs_ops_t *iso9660_vfs_get_ops(void);

#endif
