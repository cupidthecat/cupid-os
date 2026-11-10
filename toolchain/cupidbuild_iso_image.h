#ifndef CUPIDBUILD_ISO_IMAGE_H
#define CUPIDBUILD_ISO_IMAGE_H

#include "cupidbuild_iso.h"

typedef struct {
  cupidbuild_iso_inventory_report_t inventory;
  ctool_u32 image_bytes;
  ctool_u32 blocks;
  ctool_u32 path_table_bytes;
  ctool_u32 continuation_block;
} cupidbuild_iso_image_report_t;

/* Independently compare every deterministic ECMA-119/RRIP_1991A image byte with
 * a captured manifest and typed inventory. Never calls the CupidObj producer,
 * reads a filesystem, launches a tool or authors an output image. Includes
 * descriptors, both path tables, records, continuation, payloads and padding.
 *
 * Uses an exclusive arena for bounded temporary layout storage and rewinds it
 * on every exit. Borrowed views must stay immutable and precede that arena mark.
 * Output/diagnostic storage must be writable and disjoint from inputs/scratch.
 * Returns one on success. Failure clears the report and terminates a diagnostic
 * when capacity is nonzero; zero capacity permits NULL. Candidate and expected
 * complete image sizes must fit the existing 32-bit source/buffer view.
 */
int cupidbuild_iso_image_validate(
    ctool_arena_t *arena, const ctool_source_t *manifest,
    const ctool_obj_iso_fixture_request_t *inventory, const ctool_source_t *image,
    cupidbuild_iso_image_report_t *report, char *error, ctool_u32 error_capacity);

#endif
