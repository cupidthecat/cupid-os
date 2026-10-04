#include "cupidbuild_artifacts.h"
#include "cupidbuild.h"
#include "cupidbuild_host.h"
#include "seed_manifest.h"
#include "seed_release.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_LIMIT 67108864u
#define REQUEST_LIMIT 211812352u
#define ERROR_CAPACITY CUPIDBUILD_ARTIFACT_ERROR_BYTES
#define PATH_CAPACITY 8192u
static const char *const roles[6] = {
  "cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj", "cupidbuild"
};
static const char *const ordinary[4] = {
  "boot/boot.bin", "kernel/kernel.bin", "kernel/kernel.elf", "kernel/kernel.elf.pass1"
};
static const char windows_manifest[] = "bootstrap/seeds/i386-windows/manifest.json";
static const char release_path[] = "bootstrap/seeds/release.json";
typedef struct { unsigned char *bytes; size_t size; size_t capacity; } builder_t;
typedef struct { unsigned char *bytes; uint64_t size; } image_t;
static int fail(char *error, const char *message) {
  (void)snprintf(error, ERROR_CAPACITY, "%s", message); return 0;
}
static int append(builder_t *b, const void *bytes, size_t size, char *error) {
  size_t need;
  if (size > REQUEST_LIMIT || b->size > REQUEST_LIMIT - size)
    return fail(error, "native request exceeds its bounded capacity");
  need = b->size + size;
  if (need > b->capacity) {
    size_t next = b->capacity == 0u ? 4096u : b->capacity;
    unsigned char *replacement;
    while (next < need) {
      if (next > REQUEST_LIMIT / 2u) { next = REQUEST_LIMIT; break; }
      next *= 2u;
    }
    replacement = (unsigned char *)realloc(b->bytes, next);
    if (!replacement) return fail(error, "cannot allocate native request");
    b->bytes = replacement; b->capacity = next;
  }
  if (size != 0u) (void)memcpy(b->bytes + b->size, bytes, size);
  b->size = need; return 1;
}
static int number(builder_t *b, uint64_t n, unsigned int width, char *error) {
  unsigned char bytes[8]; unsigned int i;
  for (i = 0u; i < width; i++) { bytes[i] = (unsigned char)(n & 255u); n >>= 8u; }
  return append(b, bytes, width, error);
}
static int slice(builder_t *b, const void *bytes, size_t size, char *error) {
  return number(b, (uint64_t)size, 4u, error) && append(b, bytes, size, error);
}
static int text_slice(builder_t *b, const char *text, char *error) {
  return slice(b, text, strlen(text), error);
}
static void digest(const unsigned char *bytes, size_t size, char hex[65]) {
  static const char digits[] = "0123456789abcdef";
  unsigned char raw[32]; size_t i;
  cupidbuild_host_sha256_bytes(bytes, size, raw);
  for (i = 0u; i < 32u; i++) { hex[i*2u] = digits[raw[i] >> 4u]; hex[i*2u+1u] = digits[raw[i] & 15u]; }
  hex[64] = '\0';
}
static int capture(cupidbuild_host_observer_t *observer, const char *logical,
                    size_t limit, image_t *out, char *error) {
  if (!cupidbuild_host_observer_file(observer, logical, limit, &out->bytes, &out->size))
    return fail(error, cupidbuild_host_observer_error(observer));
  return 1;
}
static int split_path(const char *logical, char directory[PATH_CAPACITY],
                      const char **leaf, char *error) {
  const char *slash = strrchr(logical, '/'); size_t size = slash ? (size_t)(slash-logical) : 0u;
  if (strlen(logical) >= PATH_CAPACITY || size >= PATH_CAPACITY)
    return fail(error, "selected manifest path is too long");
  (void)memcpy(directory, logical, size); directory[size] = '\0';
  *leaf = slash ? slash + 1 : logical; return 1;
}
static int seed_paths(cupidbuild_host_observer_t *observer,
                       const char *manifest, unsigned int format,
                       char paths[6][PATH_CAPACITY], char *error) {
  char directory[PATH_CAPACITY]; char names[6][32]; const char *members[7];
  const char *leaf; size_t i;
  if (!split_path(manifest, directory, &leaf, error)) return 0;
  members[0] = leaf;
  for (i = 0u; i < 6u; i++) {
    int count;
    (void)snprintf(names[i], sizeof(names[i]), "%s.%s", roles[i], format == 1u ? "elf" : "exe");
    members[i + 1u] = names[i];
    count = snprintf(paths[i], PATH_CAPACITY, "%s%s%s", directory, directory[0] ? "/" : "", names[i]);
    if (count < 0 || (size_t)count >= PATH_CAPACITY) return fail(error, "selected seed path is too long");
  }
  if (!cupidbuild_host_observer_directory(observer, directory, members, 7u))
    return fail(error, cupidbuild_host_observer_error(observer));
  return 1;
}
static int execution_seed(cupidbuild_host_observer_t *observer,
                          const char *logical, const image_t *release,
                          char paths[6][PATH_CAPACITY], char *error) {
  image_t manifest = {NULL,0u};
  cupid_seed_manifest_result_t parsed;
  char directory[PATH_CAPACITY]; const char *leaf;
  size_t i; int ok;
  if (!split_path(logical, directory, &leaf, error)) return 0;
  if (strcmp(leaf, "manifest.json") != 0)
    return fail(error, "execution seed manifest must be named manifest.json");
  if (!capture(observer, logical, FILE_LIMIT, &manifest, error)) return 0;
  ok = cupid_seed_manifest_validate(manifest.bytes, (size_t)manifest.size, 1u,
                                   &parsed, error, ERROR_CAPACITY) &&
       cupid_seed_release_match_manifest(release->bytes, (size_t)release->size,
           manifest.bytes, (size_t)manifest.size, 1u, error, ERROR_CAPACITY);
  free(manifest.bytes);
  if (!ok || !seed_paths(observer, logical, 1u, paths, error)) return 0;
  for (i = 0u; i < 6u; i++) {
    image_t image = {NULL,0u}; char hash[65];
    if (!capture(observer, paths[i], FILE_LIMIT, &image, error)) return 0;
    digest(image.bytes, (size_t)image.size, hash);
    ok = image.size == parsed.artifacts[i].size &&
         strcmp(hash, parsed.artifacts[i].sha256) == 0 &&
         cupidbuild_validate_seed_image_bytes(image.bytes, (size_t)image.size,
             (cupidbuild_seed_image_format_t)1u, i, 1, 0);
    free(image.bytes);
    if (!ok) return fail(error, "execution seed image differs from the reviewed manifest or image profile");
  }
  return 1;
}
static int verify(const char *root, const char *policy_path, const char *linux_path,
                   const char *execution_path,
                   artifact_size_policy_result_t *result, char *error) {
  cupidbuild_host_observer_t *observer = NULL;
  image_t policy = {NULL,0u}, linux_image = {NULL,0u}, windows_image = {NULL,0u}, release = {NULL,0u};
  cupid_seed_manifest_result_t manifests[2];
  builder_t request = {NULL,0u,0u};
  char (*paths)[6][PATH_CAPACITY] = NULL;
  char hashes[2][6][65]; uint64_t sizes[2][6]; char linux_hash[65];
  unsigned int format; size_t i; int ok = 0;
  const char *artifact_paths[16];
  cupidbuild_host_file_observation_t observations[16];
  (void)memset(result, 0, sizeof(*result));
  paths = (char (*)[6][PATH_CAPACITY])malloc(2u * 6u * PATH_CAPACITY);
  if (!paths) return fail(error, "cannot allocate selected paths");
  if (!cupidbuild_host_observer_open(root, &observer)) { fail(error, "cannot retain repository root"); goto done; }
  if (!capture(observer, release_path, 65536u, &release, error) ||
      !capture(observer, policy_path, FILE_LIMIT, &policy, error) ||
      !capture(observer, linux_path, FILE_LIMIT, &linux_image, error) ||
      !capture(observer, windows_manifest, FILE_LIMIT, &windows_image, error)) goto done;
  if (!cupid_seed_pair_validate(release.bytes, (size_t)release.size,
        linux_image.bytes, (size_t)linux_image.size, windows_image.bytes,
        (size_t)windows_image.size, error, ERROR_CAPACITY)) goto done;
  if (!cupid_seed_manifest_validate(linux_image.bytes, (size_t)linux_image.size, 1u,
        &manifests[0], error, ERROR_CAPACITY) ||
      !cupid_seed_manifest_validate(windows_image.bytes, (size_t)windows_image.size, 2u,
        &manifests[1], error, ERROR_CAPACITY)) goto done;
  for (format = 0u; format < 2u; format++) {
    if (!seed_paths(observer, format == 0u ? linux_path : windows_manifest,
                    format + 1u, paths[format], error)) goto done;
    for (i = 0u; i < 6u; i++) {
      image_t image = {NULL,0u}; int valid;
      if (!capture(observer, paths[format][i], FILE_LIMIT, &image, error)) goto done;
      digest(image.bytes, (size_t)image.size, hashes[format][i]); sizes[format][i] = image.size;
      valid = image.size == manifests[format].artifacts[i].size &&
              strcmp(hashes[format][i], manifests[format].artifacts[i].sha256) == 0 &&
              cupidbuild_validate_seed_image_bytes(image.bytes, (size_t)image.size,
                (cupidbuild_seed_image_format_t)(format + 1u), i, 1,
                format == 1u ? (int)manifests[format].current_windows_plan : 0);
      free(image.bytes);
      if (!valid) { fail(error, "seed image differs from the reviewed manifest or image profile"); goto done; }
    }
  }
  digest(linux_image.bytes, (size_t)linux_image.size, linux_hash);
  if (!append(&request, "CUPSIZE2", 8u, error) ||
      !slice(&request, policy.bytes, (size_t)policy.size, error) ||
      !text_slice(&request, linux_path, error) ||
      !slice(&request, linux_image.bytes, (size_t)linux_image.size, error) ||
      !text_slice(&request, linux_hash, error) ||
      !text_slice(&request, windows_manifest, error) ||
      !slice(&request, windows_image.bytes, (size_t)windows_image.size, error) ||
      !number(&request, 6u, 4u, error)) goto done;
  for (i = 0u; i < 6u; i++) {
    if (!text_slice(&request, paths[1][i], error) || !number(&request, 1u, 4u, error) ||
        !number(&request, sizes[1][i], 8u, error) || !text_slice(&request, hashes[1][i], error)) goto done;
  }
  for (i = 0u; i < 16u; i++) {
    if (i < 4u) artifact_paths[i] = ordinary[i];
    else if (i < 10u) artifact_paths[i] = paths[0][i-4u];
    else artifact_paths[i] = paths[1][i-10u];
  }
  /* Partial observations are diagnostic only. The policy must reject every
   * unavailable marker, and observer poison separately prevents final success. */
  (void)cupidbuild_host_observer_files(observer, artifact_paths, 16u, observations);
  if (!number(&request, 16u, 4u, error)) goto done;
  for (i = 0u; i < 16u; i++) {
    if (!text_slice(&request, artifact_paths[i], error) ||
        !number(&request, observations[i].observed ? 1u : (unsigned int)observations[i].issue + 1u, 4u, error) ||
        !number(&request, observations[i].size, 8u, error)) goto done;
  }
  if (!artifact_size_policy_validate(request.bytes, request.size, result, error, ERROR_CAPACITY)) goto done;
  if (execution_path != NULL && strcmp(execution_path, linux_path) != 0 &&
      strcmp(execution_path, windows_manifest) != 0 &&
      !execution_seed(observer, execution_path, &release, paths[0], error)) goto done;
  if (!cupidbuild_host_observer_require_unchanged(observer)) {
    fail(error, cupidbuild_host_observer_error(observer)); goto done;
  }
  ok = 1;
done:
  if (!cupidbuild_host_observer_close(observer)) { fail(error, "cannot close retained observations"); ok = 0; }
  free(paths); free(request.bytes); free(policy.bytes); free(linux_image.bytes); free(windows_image.bytes); free(release.bytes);
  if (!ok) (void)memset(result, 0, sizeof(*result));
  return ok;
}
static int verify_request(const cupidbuild_artifact_request_t *request,
                                    const char *checked_path, const char *execution_path,
                                    artifact_size_policy_result_t *result,
                                    char *error, size_t error_capacity) {
  char *diagnostic;
  int ok;
  if (result != (artifact_size_policy_result_t *)0)
    (void)memset(result, 0, sizeof(*result));
  if (error_capacity != 0u && error == (char *)0) return 0;
  if (error_capacity != 0u) error[0] = 0;
  if (checked_path != NULL || execution_path != NULL) {
    const char *selection_error = NULL;
    if (checked_path == NULL || execution_path == NULL ||
        checked_path[0] == 0 || execution_path[0] == 0 ||
        strlen(checked_path) >= PATH_CAPACITY || strlen(execution_path) >= PATH_CAPACITY)
      selection_error = "invalid artifact seed selection";
    else if (strcmp(checked_path, windows_manifest) != 0)
      selection_error = "checked seed manifest is not the production Windows manifest";
    else if (cupidbuild_host_execution_format() == 2u &&
             strcmp(execution_path, windows_manifest) != 0)
      selection_error = "Windows execution seed is not the checked Windows seed";
    else if (cupidbuild_host_execution_format() == 1u &&
             strcmp(execution_path, windows_manifest) == 0)
      selection_error = "Linux execution seed must be an ELF32 cohort";
    else if (cupidbuild_host_execution_format() == 1u) {
      const char *leaf = strrchr(execution_path, '/');
      leaf = leaf != NULL ? leaf + 1 : execution_path;
      if (strcmp(leaf, "manifest.json") != 0)
        selection_error = "execution seed manifest must be named manifest.json";
    }
    if (selection_error != NULL) {
      if (error_capacity != 0u) (void)snprintf(error, error_capacity, "%s", selection_error);
      return 0;
    }
  }
  if (request == (const cupidbuild_artifact_request_t *)0 ||
      result == (artifact_size_policy_result_t *)0 ||
      request->repository_root == (const char *)0 ||
      request->policy_path == (const char *)0 ||
      request->linux_manifest_path == (const char *)0 ||
      request->repository_root[0] == 0 || request->policy_path[0] == 0 ||
      request->linux_manifest_path[0] == 0 ||
      strlen(request->repository_root) >= PATH_CAPACITY ||
      strlen(request->policy_path) >= PATH_CAPACITY ||
      strlen(request->linux_manifest_path) >= PATH_CAPACITY) {
    if (error_capacity != 0u)
      (void)snprintf(error, error_capacity, "invalid artifact verification request");
    return 0;
  }
  diagnostic = (char *)malloc(ERROR_CAPACITY);
  if (diagnostic == (char *)0) {
    if (error_capacity != 0u)
      (void)snprintf(error, error_capacity, "cannot allocate artifact diagnostic");
    return 0;
  }
  diagnostic[0] = 0;
  ok = verify(request->repository_root, request->policy_path,
              request->linux_manifest_path, execution_path, result, diagnostic);
  if (!ok && error_capacity != 0u)
    (void)snprintf(error, error_capacity, "%s", diagnostic);
  free(diagnostic);
  return ok;
}
int cupidbuild_verify_artifact_sizes(const cupidbuild_artifact_request_t *request,
                                    artifact_size_policy_result_t *result,
                                    char *error, size_t error_capacity) {
  return verify_request(request, NULL, NULL, result, error, error_capacity);
}
int cupidbuild_verify_artifact_sizes_selected(
    const cupidbuild_artifact_request_t *request,
    const char *checked_manifest_path, const char *execution_manifest_path,
    artifact_size_policy_result_t *result, char *error, size_t error_capacity) {
  return verify_request(request, checked_manifest_path != NULL ? checked_manifest_path : "",
                        execution_manifest_path != NULL ? execution_manifest_path : "",
                        result, error, error_capacity);
}
