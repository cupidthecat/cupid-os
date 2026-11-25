#ifndef CUPID_ISO_FIXTURE_BUNDLE_H
#define CUPID_ISO_FIXTURE_BUNDLE_H

#include "cupidobj.h"

#define CTOOL_ISO_BUNDLE_ENTRIES 512u
#define CTOOL_ISO_BUNDLE_PATH_BYTES 1023u
#define CTOOL_ISO_BUNDLE_MANIFEST_BYTES ((CTOOL_ISO_BUNDLE_PATH_BYTES + 2u) * CTOOL_ISO_BUNDLE_ENTRIES)
#define CTOOL_ISO_BUNDLE_METADATA_BYTES (16u + CTOOL_ISO_BUNDLE_MANIFEST_BYTES + (16u + CTOOL_ISO_BUNDLE_PATH_BYTES) * CTOOL_ISO_BUNDLE_ENTRIES)

typedef struct {
  ctool_source_t manifest;
  ctool_obj_iso_fixture_request_t inventory;
} ctool_iso_fixture_bundle_request_t;

/* CUPISO1 transports one complete request without native paths or filesystem
 * fallback. Header: eight bytes "CUPISO1\0", LE32 manifest length, LE32 count,
 * then the manifest bytes. Each entry is LE32 kind, name length, payload length,
 * zero reserved word, then name and payload bytes without alignment padding.
 * Directories have zero payload and NULL source; files retain empty payloads.
 * Request order and manifest bytes are preserved exactly.
 *
 * These functions validate framing, bounds, kinds and source views. CupidObj
 * still owns portable names, membership, parent/depth policy and image layout.
 * No filesystem access, publication, mutable global state or producer invocation
 * occurs here. A nonzero diagnostic capacity requires writable error storage.
 * Results and diagnostics must be disjoint from all immutable inputs, which
 * must precede the arena mark. Failure clears the result and rewinds allocations.
 * Successful arena allocations remain live until caller rewind or close. */
int ctool_iso_fixture_bundle_encode(
    ctool_arena_t *arena, const ctool_source_t *manifest,
    const ctool_obj_iso_fixture_request_t *inventory, ctool_bytes_t *bundle_out,
    char *error, ctool_u32 error_capacity);

/* The decoded arrays belong to the arena; names, manifest and payloads borrow
 * the immutable bundle bytes. The bundle must outlive their use. The manifest
 * uses the bundle source's logical path for diagnostics; each file source uses
 * its ISO name. No name is assumed NUL-terminated. */
int ctool_iso_fixture_bundle_decode(
    ctool_arena_t *arena, const ctool_source_t *bundle,
    ctool_iso_fixture_bundle_request_t *request_out,
    char *error, ctool_u32 error_capacity);

#endif
