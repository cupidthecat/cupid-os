/* kernel/swap_disk.h - Backing-file I/O for the opt-in swap manager.
 * Fixed 16 MB layout: 4 MB per size class.
 *   Class 0 (1K slots): offset 0x000000
 *   Class 1 (4K slots): offset 0x400000
 *   Class 2 (16K slots): offset 0x800000
 *   Class 3 (64K slots): offset 0xC00000
 */
#ifndef SWAP_DISK_H
#define SWAP_DISK_H

#include "types.h"

#define SWAP_DISK_FILE_SIZE  (16u * 1024u * 1024u)
#define SWAP_DISK_NUM_CLASSES 4

extern const uint32_t swap_disk_slot_bytes[SWAP_DISK_NUM_CLASSES];
/* 1024, 4096, 16384, 65536 */

extern const uint32_t swap_disk_slot_count[SWAP_DISK_NUM_CLASSES];
/* 4096, 1024, 256, 64 */

extern const uint32_t swap_disk_region_offset[SWAP_DISK_NUM_CLASSES];
/* 0x000000, 0x400000, 0x800000, 0xC00000 */

/* Open (or create + zero-extend) the swap file. Returns 0 on success,
 * negative errno on failure. Leaves the fd open for the lifetime of the
 * swap system (one global fd). swap_disk_close releases it. */
int swap_disk_open(const char *vfs_path);
void swap_disk_close(void);

/* Read/write `swap_disk_slot_bytes[class_idx]` bytes to/from `buf`.
 * Returns 0 on success, negative errno on I/O failure. */
int swap_disk_read(uint8_t class_idx, uint32_t slot, void *buf);
int swap_disk_write(uint8_t class_idx, uint32_t slot, const void *buf);

#endif
