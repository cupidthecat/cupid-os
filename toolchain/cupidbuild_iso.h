#ifndef CUPIDBUILD_ISO_H
#define CUPIDBUILD_ISO_H

#include "cupidobj.h"

typedef struct {
  ctool_u32 directories;
  ctool_u32 files;
  ctool_u32 directory_depth;
  ctool_u64 file_bytes;
} cupidbuild_iso_inventory_report_t;

/* Validate borrowed immutable manifest/inventory views before native ISO
 * publication. Uses the existing CupidObj request layout, without calling its
 * producer. Entry order and manifest order are independent. LF, CRLF and a
 * missing final newline are accepted; every declared path must occur exactly
 * once, with exact case and all directory parents represented.
 *
 * Limits retain CupidObj's 512 entries, 127 bytes per portable ASCII component,
 * and eight directory levels including the implicit root. directories includes
 * that root; file_bytes is a 64-bit sum of the observed file sizes. File payloads
 * are not read. The caller owns their capture and lifetime, filesystem type and
 * identity checks, and subsequent image/extent validation.
 *
 * No allocation, filesystem access, process launch or global state. Returns one
 * on success and clears the diagnostic. Failure clears the report and writes a
 * terminated diagnostic when capacity is nonzero. Zero capacity permits NULL;
 * otherwise error and report must be writable and disjoint from borrowed input.
 */
int cupidbuild_iso_inventory_validate(
    const ctool_source_t *manifest,
    const ctool_obj_iso_fixture_request_t *inventory,
    cupidbuild_iso_inventory_report_t *report,
    char *error, ctool_u32 error_capacity);

#endif
