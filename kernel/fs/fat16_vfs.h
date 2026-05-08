/**
 * fat16_vfs.h - FAT16 VFS wrapper for CupidOS
 *
 * Thin wrapper exposing the existing FAT16 driver through
 * the VFS filesystem operations interface.
 */

#ifndef FAT16_VFS_H
#define FAT16_VFS_H

#include "vfs.h"

/**
 * Return the VFS operations struct for the fat16 filesystem type.
 */
vfs_fs_ops_t *fat16_vfs_get_ops(void);

#endif /* FAT16_VFS_H */
