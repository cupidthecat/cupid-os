/** @file swap_disk.h
 * @brief Describe the backing file used by the opt-in swap manager.
 * The 16 MiB file has four 4 MiB regions in size-class order.
 * The first two regions hold 1 KiB and 4 KiB slots.
 * The last two hold 16 KiB and 64 KiB slots.
 * The region offsets are exported in swap_disk_region_offset.
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
 * swap system (one global fd). swap_disk_close releases it.*/
int swap_disk_open(const char *vfs_path);
void swap_disk_close(void);

/* Read/write `swap_disk_slot_bytes[class_idx]` bytes to/from `buf`.
 * Returns 0 on success, negative errno on I/O failure.*/
int swap_disk_read(uint8_t class_idx, uint32_t slot, void *buf);
int swap_disk_write(uint8_t class_idx, uint32_t slot, const void *buf);

#endif
