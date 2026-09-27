#ifndef CUPID_BUILD_ARTIFACTS_H
#define CUPID_BUILD_ARTIFACTS_H
#include "artifact_size_policy.h"

#define CUPIDBUILD_ARTIFACT_ERROR_BYTES 262144u

typedef struct {
  const char *repository_root;
  const char *policy_path;
  const char *linux_manifest_path;
} cupidbuild_artifact_request_t;

/* Read-only verification against the reviewed release record beneath root.
 * Paths are UTF-8; policy and manifest paths are repository-relative with slash
 * separators. This call
 * owns retained observations through final checks and closes them before return.
 * It writes no files and launches no tools. Repository review supplies release
 * authority; this is not a signature check. Drift checks are sequential.
 * Returns one on success. Failure clears result and terminates a nonempty error
 * buffer. A zero error capacity permits NULL. Inputs and outputs must not alias. */
int cupidbuild_verify_artifact_sizes(const cupidbuild_artifact_request_t *request,
                                    artifact_size_policy_result_t *result,
                                    char *error, size_t error_capacity);
#endif
