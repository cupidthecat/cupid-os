#include "user_syscall_abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ABI_INPUT_COUNT CUPID_USER_ABI_INPUT_COUNT
#define ABI_MAX_SOURCE_BYTES CUPID_USER_ABI_SOURCE_BYTES
#define ABI_FAIL(...) \
  do { \
    (void)snprintf(abi_error, sizeof(abi_error), __VA_ARGS__); \
    return 0; \
  } while (0)

static char abi_error[CUPID_USER_ABI_ERROR_BYTES];
typedef struct { unsigned char *bytes; size_t size; } abi_file_t;

static char *abi_path_join(const char *root, const char *relative) {
  size_t root_size = strlen(root);
  size_t relative_size = strlen(relative);
  int separator = root_size != 0u && root[root_size - 1u] != '/' &&
                  root[root_size - 1u] != '\\';
  char *path = (char *)malloc(root_size + (size_t)separator + relative_size +
                              1u);
  if (path == (char *)0) {
    return (char *)0;
  }
  (void)memcpy(path, root, root_size);
  if (separator) {
    path[root_size++] = '/';
  }
  (void)memcpy(path + root_size, relative, relative_size);
  path[root_size + relative_size] = '\0';
  return path;
}

static void abi_file_release(abi_file_t *file) {
  free(file->bytes);
  file->bytes = (unsigned char *)0;
  file->size = 0u;
}

static int abi_read_file(const char *root, const char *relative,
                         abi_file_t *file) {
  char *path = abi_path_join(root, relative);
  FILE *stream;
  long length;
  size_t read_size;
  file->bytes = (unsigned char *)0;
  file->size = 0u;
  if (path == (char *)0) {
    ABI_FAIL("cannot allocate the path for ABI input %s", relative);
  }
  stream = fopen(path, "rb");
  free(path);
  if (stream == (FILE *)0) {
    ABI_FAIL("ABI input is unavailable: %s", relative);
  }
  if (fseek(stream, 0L, SEEK_END) != 0) {
    (void)fclose(stream);
    ABI_FAIL("cannot measure ABI input %s", relative);
  }
  length = ftell(stream);
  if (length < 0L || (unsigned long)length > ABI_MAX_SOURCE_BYTES ||
      fseek(stream, 0L, 0) != 0) {
    (void)fclose(stream);
    ABI_FAIL("ABI input has an unsupported size: %s", relative);
  }
  file->bytes = (unsigned char *)malloc((size_t)length + 1u);
  if (file->bytes == (unsigned char *)0) {
    (void)fclose(stream);
    ABI_FAIL("cannot allocate ABI input %s", relative);
  }
  read_size = fread(file->bytes, 1u, (size_t)length, stream);
  if (read_size != (size_t)length || fclose(stream) != 0) {
    abi_file_release(file);
    ABI_FAIL("cannot read ABI input %s", relative);
  }
  file->bytes[read_size] = 0u;
  file->size = read_size;
  return 1;
}

static int abi_reread_inputs(const char *root,
                             abi_file_t snapshots[ABI_INPUT_COUNT]) {
  size_t index;
  for (index = 0u; index < ABI_INPUT_COUNT; index++) {
    abi_file_t current;
    if (!abi_read_file(root, cupid_user_abi_input_path(index), &current)) {
      char detail[sizeof(abi_error)];
      (void)snprintf(detail, sizeof(detail), "%s", abi_error);
      ABI_FAIL("ABI input changed while checking: %s: %s",
               cupid_user_abi_input_path(index), detail);
    }
    if (current.size != snapshots[index].size ||
        memcmp(current.bytes, snapshots[index].bytes, current.size) != 0) {
      abi_file_release(&current);
      ABI_FAIL("ABI input changed while checking: %s",
               cupid_user_abi_input_path(index));
    }
    abi_file_release(&current);
  }
  return 1;
}

static int abi_run_check(const char *snapshot_root, const char *reread_root) {
  abi_file_t files[ABI_INPUT_COUNT];
  cupid_user_abi_report_t report;
  cupid_user_abi_input_t inputs[ABI_INPUT_COUNT];
  char json[CUPID_USER_ABI_JSON_BYTES];
  size_t index;
  int ok = 1;
  (void)memset(files, 0, sizeof(files));
  (void)memset(&report, 0, sizeof(report));
  abi_error[0] = '\0';
  for (index = 0u; index < ABI_INPUT_COUNT; index++) {
    if (!abi_read_file(snapshot_root, cupid_user_abi_input_path(index), &files[index])) {
      ok = 0;
      break;
    }
  }
  if (ok) {
    for (index = 0u; index < ABI_INPUT_COUNT; index++) {
      inputs[index].bytes = files[index].bytes;
      inputs[index].size = files[index].size;
    }
    ok = cupid_user_abi_validate(inputs, &report, abi_error, sizeof(abi_error));
  }
  if (ok) {
    ok = abi_reread_inputs(reread_root, files);
  }
  for (index = 0u; index < ABI_INPUT_COUNT; index++) {
    abi_file_release(&files[index]);
  }
  if (!ok) {
    (void)fprintf(stderr, "Cupid user ABI contract failed: %s\n", abi_error);
    return 1;
  }
  if (!cupid_user_abi_format_json(&report, json, sizeof(json)) ||
      printf("%s", json) < 0) {
    (void)fprintf(stderr, "Cupid user ABI contract failed: cannot write report\n");
    return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "check") == 0) {
    return abi_run_check(argv[2], argv[2]);
  }
  if (argc == 4 && strcmp(argv[1], "check-snapshot") == 0) {
    return abi_run_check(argv[2], argv[3]);
  }
  (void)fprintf(stderr,
                "usage: user-syscall-abi-contract check ROOT | "
                "check-snapshot SNAPSHOT_ROOT REREAD_ROOT\n");
  return 2;
}
