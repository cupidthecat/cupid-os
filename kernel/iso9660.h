/* kernel/iso9660.h — ECMA-119 + Rock Ridge (SUSP/RRIP) parser.
 * Pure functions over block_device_t. No VFS coupling.
 * All strings are null-terminated ASCII. Rock Ridge NM records
 * may contain arbitrary bytes; non-ASCII is preserved but lookups
 * use ASCII case-fold. */
#ifndef ISO9660_H
#define ISO9660_H

#include "types.h"
#include "blockdev.h"

#define ISO9660_LOGICAL_BLOCK_SIZE 2048u
#define ISO9660_PVD_LBA            16u
#define ISO9660_MAX_NAME_LEN       255  /* Rock Ridge can go big; we cap here */

/* Runtime mount state populated by iso9660_mount_parse. */
typedef struct {
    block_device_t *bdev;
    uint32_t        root_extent_lba;
    uint32_t        root_extent_size;
    bool            has_rockridge;
} iso9660_mount_t;

/* Per-file state. */
typedef struct {
    uint32_t extent_lba;
    uint32_t file_size;
    uint32_t pos;
    bool     is_dir;
    /* For directory iteration (readdir): */
    uint32_t dir_walk_offset;  /* byte offset within the directory's extent */
    void    *owner;   /* opaque — iso9660_vfs stores slot* here */
} iso9660_file_t;

/* Parse the PVD at LBA 16 and populate `m`. Returns 0 on success,
 * negative errno on failure (VFS_EINVAL / VFS_EIO from vfs.h). */
int iso9660_mount_parse(block_device_t *bdev, iso9660_mount_t *m);

/* Look up `path` (starting with '/' or relative to root, either accepted)
 * in the ISO. On success, fills `out` with extent_lba, file_size, is_dir.
 * Returns 0 or negative errno. */
int iso9660_lookup(const iso9660_mount_t *m, const char *path,
                   iso9660_file_t *out);

/* Directory iterator: reads the next entry from the directory whose state
 * is stored in `f` (extent_lba, file_size, dir_walk_offset).
 *
 * On success fills out_name (up to out_name_cap bytes, null-terminated),
 * *out_is_dir, *out_size, and advances f->dir_walk_offset.
 * Returns 1 if a record was emitted, 0 on end-of-directory, -errno on error.
 * Skips "." and ".." records. */
int iso9660_readdir_step(const iso9660_mount_t *m, iso9660_file_t *f,
                         char *out_name, uint32_t out_name_cap,
                         bool *out_is_dir, uint32_t *out_size);

#endif
