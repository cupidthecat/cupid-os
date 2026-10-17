#ifndef CUPID_TOOLCHAIN_FAT16_STAGE_H
#define CUPID_TOOLCHAIN_FAT16_STAGE_H

#include "ctool.h"

typedef struct {
  void *context;
  ctool_u32 sector_count;
  ctool_status_t (*read_sector)(void *context, ctool_u32 sector,
                                 ctool_u8 *bytes);
  ctool_status_t (*write_sector)(void *context, ctool_u32 sector,
                                  const ctool_u8 *bytes);
} ctool_fat16_store_t;

typedef struct {
  ctool_u8 bytes[11];
} ctool_fat16_name_t;

typedef struct {
  void *context;
  ctool_u32 size;
  ctool_status_t (*read_exact)(void *context, ctool_u32 offset,
                                ctool_u8 *bytes, ctool_u32 size);
} ctool_fat16_payload_t;

typedef struct ctool_fat16_volume ctool_fat16_volume_t;

/* Sector callbacks transfer exactly 512 bytes synchronously. The store and
 * payload contexts remain caller-owned; callbacks must not retain the views.
 * Open validates FAT16 geometry inside the supplied store. Close releases only
 * the volume handle. No complete image or payload is retained in memory.
 *
 * Stage accepts already projected, padded 8.3 components in the guest namespace.
 * Host paths, Unicode projection and source capture belong to its caller.
 * It creates parents and replaces a file, preserving lowest-free allocation,
 * zero padding and every FAT copy. As with the existing hosted stage writer,
 * directories use the root or first directory cluster and do not grow.
 *
 * The store must be a private disposable candidate with exclusive ownership.
 * Stage is not a publication transaction. A failure after any attempted write
 * poisons this handle; discard that candidate rather than retrying it. The
 * caller owns flushing, independent validation and final guarded publication.
 * Calls on a handle are neither concurrent nor reentrant. */
ctool_status_t ctool_fat16_open(ctool_allocator_t allocator,
                                 ctool_fat16_store_t store,
                                 ctool_u32 partition_sector,
                                 ctool_fat16_volume_t **volume_out);
void ctool_fat16_close(ctool_fat16_volume_t *volume);
ctool_status_t ctool_fat16_stage(ctool_fat16_volume_t *volume,
                                  const ctool_fat16_name_t *components,
                                  ctool_u32 component_count,
                                  ctool_fat16_payload_t payload);

#endif
