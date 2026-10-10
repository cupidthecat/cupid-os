#ifndef CUPID_TOOLCHAIN_DISK_IMAGE_H
#define CUPID_TOOLCHAIN_DISK_IMAGE_H

#include "fat16_stage.h"

typedef struct {
  void *context;
  ctool_u64 size;
  ctool_status_t (*read_exact)(void *context, ctool_u64 offset,
                                ctool_u8 *bytes, ctool_u32 size);
} ctool_disk_source_t;

typedef struct {
  ctool_disk_source_t bootloader;
  ctool_disk_source_t kernel;
  ctool_disk_source_t checked_template;
  ctool_disk_source_t previous;
  ctool_bool previous_present;
  ctool_bool force_format;
  ctool_u32 image_sectors;
  ctool_u32 fat_start_lba;
  ctool_fat16_store_t candidate;
} ctool_disk_image_request_t;

typedef struct {
  ctool_bool ready;
  ctool_bool reused;
  ctool_bool attempted_write;
} ctool_disk_image_result_t;

/* Compose a private candidate using bounded sector buffers. Sources are stable
 * captured views for this entire call; callbacks transfer exactly the requested
 * bytes synchronously and never retain a view. The candidate is disjoint from
 * every source and has exactly image_sectors sectors.
 *
 * Validate every checked-template byte against an independent layout renderer
 * before writing. A valid previous image retains every byte from the FAT start
 * onward. A missing, invalid or force-formatted image receives the complete
 * pristine template and a zero data area. The reuse predicate preserves the
 * existing hosted writer's MBR/BPB geometry rules.
 *
 * A failed operation with attempted_write set requires discarding the candidate.
 * Only ready permits subsequent file staging. Capture, name projection, flushing,
 * final validation, source rechecks and guarded publication belong to the caller.
 * The result and request must be disjoint. Failure returns diagnostic flags,
 * never a partially ready image. No allocator or complete-image buffer is used. */
ctool_status_t ctool_disk_image_compose(const ctool_disk_image_request_t *request,
                                        ctool_disk_image_result_t *result_out);

#endif
