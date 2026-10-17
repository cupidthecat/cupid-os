#include "cupidbuild_iso_publication.h"
#include "cupidbuild_iso_capture.h"
#include "cupidbuild.h"
#include "iso_fixture_bundle.h"
#include "seed_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ISO_PUBLISH_PATH_BYTES 8192u
#define ISO_PUBLISH_FILE_BYTES 67108864u
#define ISO_PUBLISH_MANIFEST_BYTES 1048576u
#define ISO_PUBLISH_RELEASE_BYTES 65536u
#define ISO_PUBLISH_INPUTS 528u

static int iso_publish_error(char *error, ctool_u32 capacity, const char *text) {
  if (capacity != 0u) {
    size_t size = strlen(text);
    if (size >= capacity) size = capacity - 1u;
    memcpy(error, text, size); error[size] = '\0';
  }
  return 0;
}

static int iso_publish_relative(const char *path) {
  size_t start = 0u, index = 0u;
  if (path == NULL || path[0] == '\0') return 0;
  while (path[index] != '\0') {
    unsigned char byte = (unsigned char)path[index];
    if (index >= ISO_PUBLISH_PATH_BYTES - 1u || byte < 32u || byte == 127u ||
        byte == '\\' || byte == ':') return 0;
    if (byte == '/') {
      size_t size = index - start;
      if (size == 0u || size > 1023u ||
          (size == 1u && path[start] == '.') ||
          (size == 2u && path[start] == '.' && path[start + 1u] == '.')) return 0;
      start = index + 1u;
    }
    index++;
  }
  index -= start;
  return index != 0u && index <= 1023u &&
         !(index == 1u && path[start] == '.') &&
         !(index == 2u && path[start] == '.' && path[start + 1u] == '.');
}

static int iso_publish_join(char *out, const char *left, const char *right) {
  size_t a = strlen(left), b = strlen(right);
  int separator = a != 0u && left[a - 1u] != '/' && left[a - 1u] != '\\';
  if (a >= ISO_PUBLISH_PATH_BYTES || b >= ISO_PUBLISH_PATH_BYTES ||
      a + b + (size_t)separator >= ISO_PUBLISH_PATH_BYTES) return 0;
  memcpy(out, left, a);
  if (separator) out[a++] = '/';
  memcpy(out + a, right, b + 1u); return 1;
}

static void iso_publish_directory(const char *path, char *directory) {
  const char *slash = strrchr(path, '/');
  size_t size = slash == NULL ? 0u : (size_t)(slash - path);
  memcpy(directory, path, size); directory[size] = '\0';
}

static int iso_publish_within(const char *path, const char *directory) {
  size_t size = strlen(directory);
  return size == 0u || (strncmp(path, directory, size) == 0 &&
                         (path[size] == '/' || path[size] == '\0'));
}

static void *iso_publish_allocate(void *context, ctool_u32 bytes) {
  (void)context; return malloc(bytes);
}

static void iso_publish_release(void *context, void *allocation, ctool_u32 bytes) {
  (void)context; (void)bytes; free(allocation);
}

/* The capture and the transaction must agree on bytes independently of their
 * capture times. Freeze also registers original identities and output aliases. */
static int iso_publish_freeze(
    cupidbuild_host_transaction_t *transaction, const char *root,
    const char *logical, const char *private_name,
    const unsigned char *bytes, size_t size, const char **frozen_out) {
  char live[ISO_PUBLISH_PATH_BYTES];
  cupidbuild_host_snapshot_t snapshot;
  unsigned char digest[32];
  if (!iso_publish_join(live, root, logical) ||
      !cupidbuild_host_freeze_input(transaction, live, private_name,
                                     frozen_out, &snapshot)) return 0;
  cupidbuild_host_sha256_bytes(bytes, size, digest);
  return snapshot.present && snapshot.size == size &&
         memcmp(snapshot.sha256, digest, sizeof(digest)) == 0;
}

static int iso_publish_observe_freeze(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_observer_t *observer, const char *root,
    const char *logical, const char *private_name, size_t limit,
    unsigned char **bytes_out, size_t *size_out, const char **frozen_out) {
  uint64_t size = 0u;
  *bytes_out = NULL; *size_out = 0u;
  if (!cupidbuild_host_observer_file(observer, logical, limit, bytes_out, &size)) return 0;
  *size_out = (size_t)size;
  return iso_publish_freeze(transaction, root, logical, private_name,
                             *bytes_out, *size_out, frozen_out);
}

static int iso_publish_artifact(
    const unsigned char *bytes, size_t size,
    const cupid_seed_manifest_artifact_t *artifact) {
  static const char hex[] = "0123456789abcdef";
  unsigned char digest[32];
  size_t index;
  if (size != artifact->size) return 0;
  cupidbuild_host_sha256_bytes(bytes, size, digest);
  for (index = 0u; index < 32u; index++) {
    if (artifact->sha256[index * 2u] != hex[digest[index] >> 4] ||
        artifact->sha256[index * 2u + 1u] != hex[digest[index] & 15u]) return 0;
  }
  return 1;
}

int cupidbuild_iso_publish(
    const cupidbuild_iso_publication_request_t *request,
    cupidbuild_iso_publication_result_t *result,
    char *error, ctool_u32 error_capacity) {
  cupidbuild_host_output_parent_t *parent = NULL;
  cupidbuild_host_transaction_t *transaction = NULL;
  cupidbuild_host_observer_t *observer = NULL;
  cupidbuild_iso_capture_t *capture = NULL;
  ctool_arena_t *arena = NULL;
  ctool_allocator_t allocator;
  const ctool_source_t *manifest = NULL;
  const ctool_obj_iso_fixture_request_t *inventory = NULL;
  unsigned char *documents[3] = {NULL, NULL, NULL};
  size_t document_sizes[3] = {0u, 0u, 0u};
  const char *document_paths[3];
  const char *document_names[3] = {"iso-release.json", "iso-linux.json", "iso-windows.json"};
  size_t document_limits[3] = {ISO_PUBLISH_RELEASE_BYTES, ISO_PUBLISH_MANIFEST_BYTES, ISO_PUBLISH_MANIFEST_BYTES};
  cupid_seed_manifest_result_t cohorts[2];
  char directories[2][ISO_PUBLISH_PATH_BYTES];
  const char *selected_tool = NULL;
  unsigned char *tool_bytes = NULL, *candidate_bytes = NULL, *source_bytes = NULL;
  size_t source_size = 0u, tool_size = 0u;
  cupidbuild_host_snapshot_t candidate_snapshot, bundle_snapshot;
  unsigned char bundle_digest[32];
  ctool_bytes_t bundle;
  ctool_source_t image;
  cupidbuild_iso_image_report_t image_report;
  const char *failure = "ISO publication failed";
  unsigned int host_format = cupidbuild_host_execution_format();
  size_t index, format;
  int success = 0, committed = 0, changed = 0;
  if (result != NULL) memset(result, 0, sizeof(*result));
  if (error_capacity != 0u && error == NULL) return 0;
  if (error_capacity != 0u) error[0] = '\0';
  if (request == NULL || result == NULL || request->repository_root == NULL ||
      (host_format != 1u && host_format != 2u) ||
      !iso_publish_relative(request->manifest_path) ||
      !iso_publish_relative(request->fixtures_path) ||
      !iso_publish_relative(request->output_path) ||
      !iso_publish_relative(request->linux_manifest_path) ||
      !iso_publish_relative(request->windows_manifest_path) ||
      !iso_publish_relative(request->seed_release_path))
    return iso_publish_error(error, error_capacity, "ISO publication requires normalized input paths and an absolute root");
  document_paths[0] = request->seed_release_path;
  document_paths[1] = request->linux_manifest_path;
  document_paths[2] = request->windows_manifest_path;
  iso_publish_directory(document_paths[1], directories[0]);
  iso_publish_directory(document_paths[2], directories[1]);
  if (iso_publish_within(request->output_path, request->fixtures_path) ||
      iso_publish_within(request->output_path, directories[0]) ||
      iso_publish_within(request->output_path, directories[1]))
    return iso_publish_error(error, error_capacity, "ISO output is inside a fixture or seed directory");
  if (!cupidbuild_host_output_parent_prepare(request->repository_root, request->output_path, &parent)) {
    failure = cupidbuild_host_output_parent_error(parent); goto failed;
  }
  if (!cupidbuild_host_output_transaction_open(request->repository_root,
          request->manifest_path, request->output_path, parent, &transaction) ||
      !cupidbuild_host_reserve_inputs(transaction, ISO_PUBLISH_INPUTS)) {
    failure = cupidbuild_host_error(transaction); goto failed;
  }
  /* Open after the transaction creates its own root namespace. Subsequent
   * fixture/cohort capture retains strict metadata and exact memberships. */
  if (!cupidbuild_host_observer_open(request->repository_root, &observer)) {
    failure = cupidbuild_host_observer_error(observer); goto failed;
  }
  if (!cupidbuild_iso_capture_open(observer, request->manifest_path,
          request->fixtures_path, &capture, error, error_capacity)) goto failed;
  manifest = cupidbuild_iso_capture_manifest(capture);
  inventory = cupidbuild_iso_capture_inventory(capture);
  source_bytes = cupidbuild_host_read_frozen_input(transaction,
      cupidbuild_host_frozen_source(transaction), CTOOL_ISO_BUNDLE_MANIFEST_BYTES, &source_size);
  if (source_bytes == NULL || source_size != manifest->contents.size ||
      memcmp(source_bytes, manifest->contents.data, source_size) != 0) {
    failure = "ISO manifest changed between frozen and observed capture"; goto failed;
  }
  free(source_bytes); source_bytes = NULL;
  for (index = 0u; index < inventory->entry_count; index++) {
    const ctool_obj_iso_fixture_entry_t *entry = &inventory->entries[index];
    char name[32];
    if (entry->kind != CTOOL_OBJ_ISO_FIXTURE_FILE) continue;
    (void)snprintf(name, sizeof(name), "iso-payload-%03u", (unsigned int)index);
    if (!iso_publish_freeze(transaction, request->repository_root,
            entry->source->path.text.data, name, entry->source->contents.data,
            entry->source->contents.size, NULL)) {
      failure = "ISO fixture freeze or captured payload mismatch"; goto failed;
    }
  }
  for (index = 0u; index < 3u; index++) {
    if (!iso_publish_observe_freeze(transaction, observer, request->repository_root,
            document_paths[index], document_names[index], document_limits[index],
            &documents[index], &document_sizes[index], NULL)) {
      failure = "ISO seed authority freeze or observation mismatch"; goto failed;
    }
  }
  if (!cupid_seed_pair_validate(documents[0], document_sizes[0],
          documents[1], document_sizes[1], documents[2], document_sizes[2], error, error_capacity)) goto failed;
  for (format = 0u; format < 2u; format++) {
    const char *members[7];
    const char *basename = strrchr(document_paths[format + 1u], '/');
    if (!cupid_seed_manifest_validate_release(documents[0], document_sizes[0],
            documents[format + 1u], document_sizes[format + 1u], (unsigned int)format + 1u,
            &cohorts[format], error, error_capacity)) goto failed;
    if (cohorts[format].artifact_count != 6u) {
      failure = "ISO publication requires two complete six-tool cohorts"; goto failed;
    }
    members[0] = basename == NULL ? document_paths[format + 1u] : basename + 1;
    for (index = 0u; index < 6u; index++) members[index + 1u] = cohorts[format].artifacts[index].file;
    if (!cupidbuild_host_observer_directory(observer, directories[format], members, 7u)) {
      failure = cupidbuild_host_observer_error(observer); goto failed;
    }
    for (index = 0u; index < 6u; index++) {
      char logical[ISO_PUBLISH_PATH_BYTES], name[32];
      const char *frozen = NULL;
      cupid_seed_manifest_artifact_t *artifact = &cohorts[format].artifacts[index];
      (void)snprintf(name, sizeof(name), "iso-seed-%u-%u", (unsigned int)format, (unsigned int)index);
      if (!iso_publish_join(logical, directories[format], artifact->file) ||
          !iso_publish_observe_freeze(transaction, observer, request->repository_root,
              logical, name, ISO_PUBLISH_FILE_BYTES, &tool_bytes, &tool_size, &frozen) ||
          !iso_publish_artifact(tool_bytes, tool_size, artifact)) {
        failure = "ISO seed image capture or digest mismatch"; goto failed;
      }
      if (!cupidbuild_validate_seed_image_bytes(tool_bytes, tool_size,
              (cupidbuild_seed_image_format_t)(format + 1u), index, 1,
              (int)cohorts[format].current_windows_plan)) {
        failure = "ISO seed image execution profile mismatch"; goto failed;
      }
      free(tool_bytes); tool_bytes = NULL;
      if (format + 1u == host_format && index == 4u) {
        if (!cupidbuild_host_make_input_executable(transaction, frozen)) {
          failure = cupidbuild_host_error(transaction); goto failed;
        }
        selected_tool = frozen;
      }
    }
  }
  allocator.context = NULL; allocator.allocate = iso_publish_allocate;
  allocator.release = iso_publish_release;
  if (ctool_arena_open(allocator, 4096u, 268435456u, &arena) != CTOOL_OK) {
    failure = "ISO publication scratch allocation failed"; goto failed;
  }
  if (!ctool_iso_fixture_bundle_encode(arena, manifest, inventory, &bundle, error, error_capacity)) goto failed;
  cupidbuild_host_sha256_bytes(bundle.data, bundle.size, bundle_digest);
  if (!cupidbuild_host_write_private_output_bounded(transaction, bundle.data, bundle.size,
          ISO_PUBLISH_FILE_BYTES + CTOOL_ISO_BUNDLE_METADATA_BYTES) ||
      !cupidbuild_host_capture_private_output(transaction, &bundle_snapshot, NULL) ||
      bundle_snapshot.size != bundle.size ||
      memcmp(bundle_snapshot.sha256, bundle_digest, sizeof(bundle_digest)) != 0 ||
      !cupidbuild_host_require_private_output(transaction, &bundle_snapshot) ||
      !cupidbuild_host_require_frozen_inputs(transaction) ||
      !cupidbuild_host_transaction_borrow_observer(transaction, observer) ||
      !cupidbuild_host_require_publication_boundary(transaction)) {
    failure = cupidbuild_host_error(transaction); goto failed;
  }
  {
    const char *arguments[5];
    arguments[0] = "iso-fixture-bundle";
    arguments[1] = cupidbuild_host_private_output(transaction);
    arguments[2] = "-o"; arguments[3] = cupidbuild_host_candidate(transaction);
    arguments[4] = NULL;
    if (cupidbuild_host_run(transaction, selected_tool, arguments, 60000u) != 0 ||
        !cupidbuild_host_require_private_output(transaction, &bundle_snapshot) ||
        !cupidbuild_host_require_frozen_inputs(transaction) ||
        !cupidbuild_host_require_publication_boundary(transaction) ||
        !cupidbuild_host_capture_candidate(transaction, &candidate_snapshot, &candidate_bytes)) {
      failure = cupidbuild_host_error(transaction); goto failed;
    }
  }
  image.path.text = ctool_string(request->output_path);
  image.contents = ctool_bytes(candidate_bytes, (ctool_u32)candidate_snapshot.size);
  if (!cupidbuild_iso_image_validate(arena, manifest, inventory, &image,
          &image_report, error, error_capacity)) goto failed;
  if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot) ||
      !cupidbuild_host_require_private_output(transaction, &bundle_snapshot) ||
      !cupidbuild_host_require_frozen_inputs(transaction) ||
      !cupidbuild_host_publish_if_changed(transaction, &changed)) {
    failure = cupidbuild_host_error(transaction); goto failed;
  }
  success = 1;
failed:
  if (!success && (error_capacity == 0u || error[0] == '\0'))
    (void)iso_publish_error(error, error_capacity, failure);
  committed = cupidbuild_host_publication_committed(transaction);
  /* The observer and parent are borrowed by transaction: close it first. */
  if (!cupidbuild_host_transaction_close(transaction)) {
    if (success) (void)iso_publish_error(error, error_capacity, "ISO transaction cleanup failed after publication");
    success = 0;
  }
  cupidbuild_iso_capture_close(capture);
  if (!cupidbuild_host_observer_close(observer)) {
    if (success) (void)iso_publish_error(error, error_capacity, "ISO observer close failed after publication");
    success = 0;
  }
  if (!cupidbuild_host_output_parent_close(parent)) {
    if (success) (void)iso_publish_error(error, error_capacity, "ISO output-parent close failed after publication");
    success = 0;
  }
  free(source_bytes); free(tool_bytes); free(candidate_bytes);
  for (index = 0u; index < 3u; index++) free(documents[index]);
  ctool_arena_close(arena);
  if (success) { result->image = image_report; result->changed = changed; }
  result->committed = committed;
  return success;
}
