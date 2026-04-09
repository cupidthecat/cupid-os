#ifndef HOMEFS_H
#define HOMEFS_H

#include "types.h"
#include "vfs.h"

/* Native persistent filesystem for /home.
 * Backed by a serialized container file stored on the FAT16 partition. */

vfs_fs_ops_t *homefs_get_ops(void);

/* Flush the mounted /home filesystem to its FAT16-backed container file. */
int homefs_sync(void);

#endif
