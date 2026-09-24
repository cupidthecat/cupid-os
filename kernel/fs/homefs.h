#ifndef HOMEFS_H
#define HOMEFS_H

#include "types.h"
#include "vfs.h"

/* Native persistent filesystem for /home.
 * Backed by a serialized container file stored on the FAT16 partition.*/

vfs_fs_ops_t *homefs_get_ops(void);

/* Flush the mounted /home filesystem to its FAT16-backed container file. */
int homefs_sync(void);

/* Group related mutations behind one final container write. Batches may nest;
 * the outermost end reports the durable publication result. */
int homefs_batch_begin(void);
int homefs_batch_end(void);

/* Suppress persistence while generated boot assets seed /home. */
void homefs_seed_begin(void);
void homefs_seed_end(void);

#endif
