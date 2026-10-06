#ifndef CUPIDBUILD_ISO_CAPTURE_H
#define CUPIDBUILD_ISO_CAPTURE_H

#include "cupidbuild_iso.h"
#include "cupidbuild_host.h"

typedef struct cupidbuild_iso_capture cupidbuild_iso_capture_t;

/* Capture the complete manifest and fixture tree through a caller's retained
 * observer. Logical paths are repository-relative UTF-8; an empty fixture path
 * selects that observer's root. Manifest names keep the ISO portable alphabet,
 * exact case, 512-entry, 127-component-byte and eight-level contracts.
 *
 * Owns copied names, manifest and payload bytes; borrows the observer, which must
 * outlive the capture and retain every observation through final publication.
 * Captures real kinds, regular-file metadata/payloads and exact membership of
 * every directory, including the fixture root and empty directories. Performs
 * an unchanged check before success. Creates no files, locks or child processes.
 * The existing observer limits apply, including 64 MiB per payload observation.
 * A failed capture returns NULL and frees its private storage. Observer failure
 * remains poisoned; pure manifest/inventory failure still forbids publication.
 * Diagnostic storage and result pointer must be disjoint from input strings.
 */
int cupidbuild_iso_capture_open(cupidbuild_host_observer_t *observer,
                                const char *manifest_logical,
                                const char *fixtures_logical,
                                cupidbuild_iso_capture_t **capture_out,
                                char *error, ctool_u32 error_capacity);

/* Borrowed immutable views remain valid until capture close. NULL returns NULL.
 * File source paths identify their original repository-relative host inputs;
 * entry paths identify their separate ISO namespace. */
const ctool_source_t *cupidbuild_iso_capture_manifest(const cupidbuild_iso_capture_t *capture);
const ctool_obj_iso_fixture_request_t *cupidbuild_iso_capture_inventory(const cupidbuild_iso_capture_t *capture);
const cupidbuild_iso_inventory_report_t *cupidbuild_iso_capture_report(const cupidbuild_iso_capture_t *capture);
int cupidbuild_iso_capture_require_unchanged(cupidbuild_iso_capture_t *capture);
/* Frees capture storage; never closes the borrowed observer. Accepts NULL. */
void cupidbuild_iso_capture_close(cupidbuild_iso_capture_t *capture);

#endif
