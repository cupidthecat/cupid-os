#include "cupidbuild.h"
#include "cupidbuild_host.h"
#include "ctool.h"
#include "ctool_host.h"
#include "elf32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CUPIDBUILD_PATH_BYTES 8192u
#define CUPIDBUILD_MANIFEST_BYTES 1048576u
typedef struct {
  char file[CUPIDBUILD_PATH_BYTES];
  char sha256[65];
  size_t size;
} cupidbuild_seed_artifact_t;

static int cupidbuild_path_safe(const char *path, int relative) {
  const char *cursor;
  if (path == (const char *)0 || path[0] == '\0' || strchr(path, '"') != 0) {
    return 0;
  }
  if (relative != 0 &&
      (path[0] == '/' || path[0] == '\\' || path[1] == ':')) {
    return 0;
  }
  cursor = path;
  while (*cursor != '\0') {
    const char *start = cursor;
    while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
      cursor++;
    }
    if ((cursor - start == 1 && start[0] == '.') ||
        (cursor - start == 2 && start[0] == '.' && start[1] == '.')) {
      return 0;
    }
    if (*cursor != '\0') {
      cursor++;
    }
  }
  return 1;
}

static int cupidbuild_join(char *destination, size_t capacity,
                           const char *left, const char *right) {
  size_t left_size = strlen(left);
  size_t right_size = strlen(right);
  int separator = left_size != 0u && left[left_size - 1u] != '/' &&
                          left[left_size - 1u] != '\\'
                      ? 1
                      : 0;
  if (left_size + (size_t)separator + right_size + 1u > capacity) {
    return 0;
  }
  (void)memcpy(destination, left, left_size);
  if (separator != 0) {
    destination[left_size++] = '/';
  }
  (void)memcpy(destination + left_size, right, right_size + 1u);
  return 1;
}

static int cupidbuild_repository_prefix(const char *path, const char *root) {
  size_t index;
  size_t root_size = strlen(root);
  for (index = 0u; index < root_size; index++) {
    char left = path[index];
    char right = root[index];
#if defined(_WIN32)
    if (left == '\\') {
      left = '/';
    }
    if (right == '\\') {
      right = '/';
    }
    if (left >= 'A' && left <= 'Z') {
      left = (char)(left - 'A' + 'a');
    }
    if (right >= 'A' && right <= 'Z') {
      right = (char)(right - 'A' + 'a');
    }
#endif
    if (left != right) {
      return 0;
    }
  }
  return path[root_size] == '/' || path[root_size] == '\\';
}

static unsigned char *cupidbuild_read_private(const char *path, size_t limit,
                                              size_t *size_out) {
  FILE *stream;
  long length;
  unsigned char *bytes;
#if defined(_WIN32)
  stream = (FILE *)0;
  if (fopen_s(&stream, path, "rb") != 0) {
    stream = (FILE *)0;
  }
#else
  stream = fopen(path, "rb");
#endif
  if (stream == (FILE *)0 || fseek(stream, 0L, SEEK_END) != 0) {
    if (stream != (FILE *)0) {
      (void)fclose(stream);
    }
    return (unsigned char *)0;
  }
  length = ftell(stream);
  if (length < 0L || (unsigned long)length > (unsigned long)limit ||
      fseek(stream, 0L, 0) != 0) {
    (void)fclose(stream);
    return (unsigned char *)0;
  }
  bytes = (unsigned char *)malloc((size_t)length + 1u);
  if (bytes == (unsigned char *)0 ||
      ((size_t)length != 0u &&
       fread(bytes, 1u, (size_t)length, stream) != (size_t)length) ||
      fclose(stream) != 0) {
    free(bytes);
    return (unsigned char *)0;
  }
  bytes[(size_t)length] = 0u;
  *size_out = (size_t)length;
  return bytes;
}

static int cupidbuild_json_artifact(const unsigned char *manifest,
                                    size_t manifest_size,
                                    const char *tool_name,
                                    cupidbuild_seed_artifact_t *artifact) {
  char name_pattern[64];
  const char *name;
  const char *object_start;
  const char *file;
  const char *value;
  const char *end;
  const char *object_end;
  const char *digest_field;
  const char *size_field;
  size_t parsed_size = 0u;
  int written = snprintf(name_pattern, sizeof(name_pattern),
                         "\"name\": \"%s\"", tool_name);
  if (written < 0 || (size_t)written >= sizeof(name_pattern)) {
    return 0;
  }
  name = strstr((const char *)manifest, name_pattern);
  if (name == (const char *)0 ||
      (size_t)(name - (const char *)manifest) >= manifest_size) {
    return 0;
  }
  object_start = name;
  while (object_start != (const char *)manifest && *object_start != '{') {
    object_start--;
  }
  file = strstr(object_start, "\"file\"");
  if (*object_start != '{' || file == (const char *)0 || file >= name) {
    return 0;
  }
  value = strchr(file, ':');
  if (value == (const char *)0) {
    return 0;
  }
  value++;
  while (*value == ' ' || *value == '\t' || *value == '\r' ||
         *value == '\n') {
    value++;
  }
  if (*value++ != '"') {
    return 0;
  }
  end = strchr(value, '"');
  if (end == (const char *)0 || end <= value ||
      (size_t)(end - value) + 1u > sizeof(artifact->file)) {
    return 0;
  }
  (void)memcpy(artifact->file, value, (size_t)(end - value));
  artifact->file[end - value] = '\0';
  object_end = strchr(name, '}');
  digest_field = strstr(name, "\"sha256\"");
  size_field = strstr(name, "\"size\"");
  if (object_end == (const char *)0 || digest_field == (const char *)0 ||
      digest_field >= object_end || size_field == (const char *)0 ||
      size_field >= object_end) {
    return 0;
  }
  value = strchr(digest_field, ':');
  if (value == (const char *)0 || value >= object_end) {
    return 0;
  }
  value++;
  while (*value == ' ' || *value == '\t' || *value == '\r' ||
         *value == '\n') {
    value++;
  }
  if (*value++ != '"') {
    return 0;
  }
  end = strchr(value, '"');
  if (end == (const char *)0 || end - value != 64) {
    return 0;
  }
  (void)memcpy(artifact->sha256, value, 64u);
  artifact->sha256[64] = '\0';
  value = strchr(size_field, ':');
  if (value == (const char *)0 || value >= object_end) {
    return 0;
  }
  value++;
  while (*value == ' ' || *value == '\t' || *value == '\r' ||
         *value == '\n') {
    value++;
  }
  if (*value < '0' || *value > '9') {
    return 0;
  }
  while (*value >= '0' && *value <= '9') {
    unsigned int digit = (unsigned int)(*value - '0');
    if (parsed_size > ((size_t)-1 - digit) / 10u) {
      return 0;
    }
    parsed_size = parsed_size * 10u + digit;
    value++;
  }
  artifact->size = parsed_size;
  return cupidbuild_path_safe(artifact->file, 1) &&
         strchr(artifact->file, '/') == (char *)0 &&
         strchr(artifact->file, '\\') == (char *)0;
}

static int cupidbuild_manifest_directory(const char *manifest, char *directory,
                                         size_t capacity) {
  const char *cursor = manifest;
  const char *last = (const char *)0;
  size_t size;
  while (*cursor != '\0') {
    if (*cursor == '/' || *cursor == '\\') {
      last = cursor;
    }
    cursor++;
  }
  if (last == (const char *)0) {
    return 0;
  }
  size = (size_t)(last - manifest);
  if (size == 0u || size + 1u > capacity) {
    return 0;
  }
  (void)memcpy(directory, manifest, size);
  directory[size] = '\0';
  return 1;
}

static int cupidbuild_digest_matches_hex(const unsigned char digest[32],
                                         const char *hex) {
  static const char digits[] = "0123456789abcdef";
  size_t index;
  for (index = 0u; index < 32u; index++) {
    if (hex[index * 2u] != digits[digest[index] >> 4u] ||
        hex[index * 2u + 1u] != digits[digest[index] & 15u]) {
      return 0;
    }
  }
  return hex[64] == '\0';
}

static int cupidbuild_artifact_matches(
    const cupidbuild_host_snapshot_t *snapshot,
    const cupidbuild_seed_artifact_t *artifact) {
  return snapshot->present != 0 && snapshot->size == artifact->size &&
         cupidbuild_digest_matches_hex(snapshot->sha256, artifact->sha256);
}

static int cupidbuild_validate_object(const unsigned char *bytes, size_t size) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_elf32_object_t object;
  ctool_u32 index;
  int executable = 0;
  if (size > 4294967295u ||
      ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
    return 0;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  if (ctool_job_open(&config, &job) != CTOOL_OK) {
    return 0;
  }
  source.path.text = ctool_string("/candidate.o");
  source.contents = ctool_bytes(bytes, (ctool_u32)size);
  if (ctool_elf32_read(job, &source, &object) == CTOOL_OK &&
      object.file_type == CTOOL_ELF32_ET_REL) {
    for (index = 0u; index < object.section_count; index++) {
      const ctool_elf32_section_t *section = &object.sections[index];
      if (section->type == CTOOL_ELF32_SHT_PROGBITS &&
          (section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u &&
          section->size != 0u) {
        executable = 1;
      }
    }
  }
  ctool_job_close(job);
  return executable;
}

int cupidbuild_assemble_object(const cupidbuild_object_request_t *request) {
  cupidbuild_host_transaction_t *transaction =
      (cupidbuild_host_transaction_t *)0;
  char manifest_path[CUPIDBUILD_PATH_BYTES];
  char manifest_directory[CUPIDBUILD_PATH_BYTES];
  char assembler_path[CUPIDBUILD_PATH_BYTES];
  char inspector_path[CUPIDBUILD_PATH_BYTES];
  const char *frozen_manifest = (const char *)0;
  const char *frozen_assembler = (const char *)0;
  const char *frozen_inspector = (const char *)0;
  cupidbuild_seed_artifact_t assembler_artifact;
  cupidbuild_seed_artifact_t compiler_artifact;
  cupidbuild_seed_artifact_t inspector_artifact;
  cupidbuild_seed_artifact_t linker_artifact;
  cupidbuild_seed_artifact_t object_artifact;
  cupidbuild_seed_artifact_t build_artifact;
  cupidbuild_host_snapshot_t assembler_snapshot;
  cupidbuild_host_snapshot_t compiler_snapshot;
  cupidbuild_host_snapshot_t inspector_snapshot;
  cupidbuild_host_snapshot_t linker_snapshot;
  cupidbuild_host_snapshot_t object_snapshot;
  cupidbuild_host_snapshot_t build_snapshot;
  cupidbuild_host_snapshot_t candidate_snapshot;
  unsigned char *manifest = (unsigned char *)0;
  unsigned char *candidate = (unsigned char *)0;
  size_t manifest_size = 0u;
  const char *assembler_arguments[6];
  const char *inspector_arguments[4];
  int result = 1;
  if (request == (const cupidbuild_object_request_t *)0 ||
      !cupidbuild_path_safe(request->repository_root, 0) ||
      !cupidbuild_path_safe(request->source, 1) ||
      !cupidbuild_path_safe(request->output, 1) ||
      !cupidbuild_path_safe(request->seed_manifest, 0)) {
    (void)fprintf(stderr, "cupidbuild: invalid guarded object request\n");
    return 1;
  }
  if (!cupidbuild_host_transaction_open(
          request->repository_root, request->source, request->output,
          &transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (request->seed_manifest[0] == '/' ||
      request->seed_manifest[0] == '\\' || request->seed_manifest[1] == ':') {
    if (strlen(request->seed_manifest) <= strlen(request->repository_root) ||
        !cupidbuild_repository_prefix(request->seed_manifest,
                                      request->repository_root)) {
      (void)fprintf(stderr,
                    "cupidbuild: checked seed manifest is outside the repository\n");
      goto done;
    }
    if (strlen(request->seed_manifest) + 1u > sizeof(manifest_path)) {
      goto done;
    }
    (void)memcpy(manifest_path, request->seed_manifest,
                 strlen(request->seed_manifest) + 1u);
  } else if (!cupidbuild_join(manifest_path, sizeof(manifest_path),
                              request->repository_root,
                              request->seed_manifest)) {
    goto done;
  }
  if (!cupidbuild_host_freeze_input(transaction, manifest_path,
                                    "manifest.json", &frozen_manifest,
                                    (cupidbuild_host_snapshot_t *)0)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  manifest = cupidbuild_read_private(frozen_manifest,
                                     CUPIDBUILD_MANIFEST_BYTES,
                                     &manifest_size);
  if (manifest == (unsigned char *)0 ||
      !cupidbuild_json_artifact(manifest, manifest_size, "cupidasm",
                                &assembler_artifact) ||
      !cupidbuild_json_artifact(manifest, manifest_size, "cupidc",
                                &compiler_artifact) ||
      !cupidbuild_json_artifact(manifest, manifest_size, "cupiddis",
                                &inspector_artifact) ||
      !cupidbuild_json_artifact(manifest, manifest_size, "cupidld",
                                &linker_artifact) ||
      !cupidbuild_json_artifact(manifest, manifest_size, "cupidobj",
                                &object_artifact) ||
      !cupidbuild_manifest_directory(manifest_path, manifest_directory,
                                     sizeof(manifest_directory)) ||
      !cupidbuild_join(assembler_path, sizeof(assembler_path),
                       manifest_directory, assembler_artifact.file) ||
      !cupidbuild_join(inspector_path, sizeof(inspector_path),
                       manifest_directory, inspector_artifact.file)) {
    (void)fprintf(stderr, "cupidbuild: checked seed manifest is invalid\n");
    goto done;
  }
  if (!cupidbuild_host_freeze_input(
          transaction, assembler_path, assembler_artifact.file,
          &frozen_assembler, &assembler_snapshot) ||
      !cupidbuild_host_make_input_executable(transaction,
                                             frozen_assembler) ||
      !cupidbuild_artifact_matches(&assembler_snapshot,
                                   &assembler_artifact)) {
    (void)fprintf(stderr, "cupidbuild: checked CupidASM digest mismatch\n");
    goto done;
  }
  if (!cupidbuild_join(assembler_path, sizeof(assembler_path),
                       manifest_directory, compiler_artifact.file) ||
      !cupidbuild_host_freeze_input(
          transaction, assembler_path, compiler_artifact.file,
          (const char **)0, &compiler_snapshot) ||
      !cupidbuild_artifact_matches(&compiler_snapshot, &compiler_artifact)) {
    (void)fprintf(stderr, "cupidbuild: checked CupidC digest mismatch\n");
    goto done;
  }
  if (!cupidbuild_host_freeze_input(
          transaction, inspector_path, inspector_artifact.file,
          &frozen_inspector, &inspector_snapshot) ||
      !cupidbuild_host_make_input_executable(transaction,
                                             frozen_inspector) ||
      !cupidbuild_artifact_matches(&inspector_snapshot,
                                   &inspector_artifact)) {
    (void)fprintf(stderr, "cupidbuild: checked CupidDis digest mismatch\n");
    goto done;
  }
  if (!cupidbuild_join(assembler_path, sizeof(assembler_path),
                       manifest_directory, linker_artifact.file) ||
      !cupidbuild_host_freeze_input(
          transaction, assembler_path, linker_artifact.file,
          (const char **)0, &linker_snapshot) ||
      !cupidbuild_artifact_matches(&linker_snapshot, &linker_artifact)) {
    (void)fprintf(stderr, "cupidbuild: checked CupidLD digest mismatch\n");
    goto done;
  }
  if (!cupidbuild_join(assembler_path, sizeof(assembler_path),
                       manifest_directory, object_artifact.file) ||
      !cupidbuild_host_freeze_input(
          transaction, assembler_path, object_artifact.file,
          (const char **)0, &object_snapshot) ||
      !cupidbuild_artifact_matches(&object_snapshot, &object_artifact)) {
    (void)fprintf(stderr, "cupidbuild: checked CupidObj digest mismatch\n");
    goto done;
  }
  if (strstr((const char *)manifest, "\"name\": \"cupidbuild\"") !=
      (const char *)0) {
    if (!cupidbuild_json_artifact(manifest, manifest_size, "cupidbuild",
                                  &build_artifact) ||
        !cupidbuild_join(assembler_path, sizeof(assembler_path),
                         manifest_directory, build_artifact.file) ||
        !cupidbuild_host_freeze_input(
            transaction, assembler_path, build_artifact.file,
            (const char **)0, &build_snapshot) ||
        !cupidbuild_artifact_matches(&build_snapshot, &build_artifact)) {
      (void)fprintf(stderr, "cupidbuild: checked CupidBuild digest mismatch\n");
      goto done;
    }
  }
  assembler_arguments[0] = "-f";
  assembler_arguments[1] = "elf32";
  assembler_arguments[2] = "-o";
  assembler_arguments[3] = cupidbuild_host_candidate(transaction);
  assembler_arguments[4] = cupidbuild_host_frozen_source(transaction);
  assembler_arguments[5] = (const char *)0;
  if (cupidbuild_host_run(transaction, frozen_assembler,
                          assembler_arguments, 60000u) != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidASM failed\n");
    goto done;
  }
  if (!cupidbuild_host_require_inputs(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot,
                                         &candidate) ||
      !cupidbuild_validate_object(candidate, candidate_snapshot.size)) {
    (void)fprintf(stderr,
                  "cupidbuild: checked CupidASM relocatable object validation failed\n");
    goto done;
  }
  free(candidate);
  candidate = (unsigned char *)0;
  if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  inspector_arguments[0] = "--require-known";
  inspector_arguments[1] = "--require-local-targets";
  inspector_arguments[2] = cupidbuild_host_candidate(transaction);
  inspector_arguments[3] = (const char *)0;
  if (cupidbuild_host_run(transaction, frozen_inspector,
                          inspector_arguments, 60000u) != 0) {
    (void)fprintf(stderr, "cupidbuild: checked CupidDis failed\n");
    goto done;
  }
  if (!cupidbuild_host_require_inputs(transaction) ||
      !cupidbuild_host_require_candidate(transaction, &candidate_snapshot) ||
      !cupidbuild_host_require_publication_boundary(transaction) ||
      !cupidbuild_host_publish(transaction)) {
    (void)fprintf(stderr, "cupidbuild: %s\n",
                  cupidbuild_host_error(transaction));
    goto done;
  }
  result = 0;

done:
  free(candidate);
  free(manifest);
  cupidbuild_host_transaction_close(transaction);
  return result;
}
