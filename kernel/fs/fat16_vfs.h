/**
 * @file fat16_vfs.h
 * @brief Expose FAT16 through the virtual filesystem.
 */

#ifndef FAT16_VFS_H
#define FAT16_VFS_H

#include "vfs.h"

/**
 * Return the VFS operations struct for the fat16 filesystem type.
*/
vfs_fs_ops_t *fat16_vfs_get_ops(void);

#endif /* FAT16_VFS_H */
