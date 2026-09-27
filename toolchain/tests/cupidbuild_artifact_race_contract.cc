#include "cupidbuild_host.h"
#include "seed_manifest.h"
#include "seed_release.h"
#include <stdio.h>
#include <string.h>

static const char *race_phase;
static int race_pair(const unsigned char *, size_t, const unsigned char *, size_t,
                     const unsigned char *, size_t, char *, size_t);
static int race_unchanged(cupidbuild_host_observer_t *);
#define cupid_seed_pair_validate race_pair
#define cupidbuild_host_observer_require_unchanged race_unchanged
#include "cupidbuild_artifacts.cc"
#undef cupid_seed_pair_validate
#undef cupidbuild_host_observer_require_unchanged

static int race_pause(const char *phase) {
  unsigned char resume;
  if (strcmp(race_phase, phase) != 0) return 1;
  (void)fprintf(stderr, "ready %s\n", phase);
  if (fflush(stderr) != 0) return 0;
  return fread(&resume, 1u, 1u, stdin) == 1u && resume == 'x';
}
static int race_pair(const unsigned char *release, size_t release_size,
                     const unsigned char *linux_bytes, size_t linux_size,
                     const unsigned char *windows, size_t windows_size,
                     char *error, size_t capacity) {
  if (!race_pause("validate")) return 0;
  return cupid_seed_pair_validate(release, release_size, linux_bytes, linux_size,
                                   windows, windows_size, error, capacity);
}
static int race_unchanged(cupidbuild_host_observer_t *observer) {
  if (!race_pause("success")) return 0;
  return cupidbuild_host_observer_require_unchanged(observer);
}
int main(int argc, char **argv) {
  cupidbuild_artifact_request_t request;
  artifact_size_policy_result_t result;
  char error[CUPIDBUILD_ARTIFACT_ERROR_BYTES];
  if (argc != 5) return 2;
  request.repository_root = argv[1];
  request.policy_path = argv[2];
  request.linux_manifest_path = argv[3];
  race_phase = argv[4];
  if (!cupidbuild_verify_artifact_sizes(&request, &result, error, sizeof(error))) {
    (void)fprintf(stderr, "artifact size verification failed: %s\n", error);
    return 1;
  }
  (void)printf("Cupid artifact sizes: ok (%u exact artifacts)\n", result.artifact_count);
  return 0;
}
