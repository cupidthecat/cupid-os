#ifndef CUPIDBUILD_ISO_PUBLICATION_H
#define CUPIDBUILD_ISO_PUBLICATION_H

#include "cupidbuild_iso_image.h"

typedef struct {
  const char *repository_root;
  const char *manifest_path;
  const char *fixtures_path;
  const char *output_path;
  const char *linux_manifest_path;
  const char *windows_manifest_path;
  const char *seed_release_path;
} cupidbuild_iso_publication_request_t;

typedef struct {
  cupidbuild_iso_image_report_t image;
  int changed;
  /* Remains set if cleanup fails after a successful publication. Equal-output
   * reuse succeeds without committing a replacement. */
  int committed;
} cupidbuild_iso_publication_result_t;

/* Publish a complete deterministic ISO through retained, frozen inputs and the
 * explicitly selected paired release. The root is absolute; other paths use
 * normalized repository-relative UTF-8. Outputs inside the fixture tree or
 * either seed directory fail. Both complete six-tool cohorts are validated;
 * only the host's frozen CupidObj is executed, with the existing 60s deadline.
 * All 512 entries, maximum portable names/depth and empty members are retained.
 * The candidate must pass independent complete image validation before rename.
 * Equal bytes preserve the output timestamp. Failed ordinary publication rolls
 * back to verified prior bytes or absence under the host transaction contract.
 * Created output directories persist. The release's authority belongs to the
 * caller; this operation checks its claims and lifetime, never authenticates it.
 *
 * Returns one on success. Failure clears the result except committed, which
 * reports an actual replacement even if a later close fails. Zero diagnostic
 * capacity permits NULL; otherwise error is writable. Request, strings, result
 * and diagnostic storage are disjoint and stay valid throughout the call.
 * No mutable global state is used. Installed seeds and recipes are unchanged. */
int cupidbuild_iso_publish(
    const cupidbuild_iso_publication_request_t *request,
    cupidbuild_iso_publication_result_t *result,
    char *error, ctool_u32 error_capacity);

#endif
