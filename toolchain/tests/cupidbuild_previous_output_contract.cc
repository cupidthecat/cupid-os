#include "cupidbuild_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif

static int unhex(char *out, size_t capacity, const char *text) {
  size_t offset = 0u;
  while (*text) {
    unsigned int value = 0u;
    unsigned int digit;
    for (digit = 0u; digit < 2u; digit++) {
      unsigned int part = (unsigned char)*text++;
      if (part >= '0' && part <= '9') part -= '0';
      else if (part >= 'a' && part <= 'f') part = part - 'a' + 10u;
      else return 0;
      value = value * 16u + part;
    }
    if (value == 0u || offset + 1u >= capacity) return 0;
    out[offset++] = (char)value;
  }
  out[offset] = 0;
  return 1;
}

static int decimal(const char *text, unsigned long long *value) {
  unsigned long long result = 0u;
  if (!*text) return 0;
  while (*text) {
    unsigned int digit = (unsigned char)*text++ - '0';
    if (digit > 9u || result > (18446744073709551615ULL - digit) / 10u) return 0;
    result = result * 10u + digit;
  }
  *value = result;
  return 1;
}

static int pattern(const char *path, unsigned long long size, unsigned int seed,
                   int write_file, const char *export_path) {
  unsigned char bytes[65536];
  unsigned long long offset = 0u;
#if defined(_WIN32)
  HANDLE file = CreateFileA(path, write_file ? GENERIC_WRITE : GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, (LPSECURITY_ATTRIBUTES)0,
      write_file ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, (HANDLE)0);
  int valid = file != INVALID_HANDLE_VALUE && file != (HANDLE)0;
#else
  FILE *file = fopen(path, write_file ? "wb" : "rb");
  int valid = file != (FILE *)0;
#endif
  FILE *copy = export_path ? fopen(export_path, "wb") : (FILE *)0;
  if (!valid || (export_path && copy == (FILE *)0)) {
#if defined(_WIN32)
    if (valid) (void)CloseHandle(file);
#else
    if (file) (void)fclose(file);
#endif
    if (copy) (void)fclose(copy);
    return 0;
  }
  while (valid && offset < size) {
    size_t wanted = size - offset > sizeof(bytes) ? sizeof(bytes) : (size_t)(size - offset);
    size_t index;
#if defined(_WIN32)
    DWORD count = 0u;
    if (!write_file && (!ReadFile(file, bytes, (DWORD)wanted, &count, (LPOVERLAPPED)0) || count != wanted)) {
      valid = 0; break;
    }
#else
    if (!write_file && fread(bytes, 1u, wanted, file) != wanted) { valid = 0; break; }
#endif
    for (index = 0u; index < wanted; index++) {
      unsigned char expected = (unsigned char)(((unsigned int)offset + index) * 29u + seed);
      if (write_file) bytes[index] = expected;
      else if (bytes[index] != expected) { valid = 0; break; }
    }
#if defined(_WIN32)
    if (valid && write_file && (!WriteFile(file, bytes, (DWORD)wanted, &count, (LPOVERLAPPED)0) || count != wanted)) valid = 0;
#else
    if (valid && write_file && fwrite(bytes, 1u, wanted, file) != wanted) valid = 0;
#endif
    if (valid && copy && fwrite(bytes, 1u, wanted, copy) != wanted) valid = 0;
    offset += wanted;
  }
#if defined(_WIN32)
  if (valid && !write_file) {
    DWORD count = 0u;
    if (!ReadFile(file, bytes, 1u, &count, (LPOVERLAPPED)0) || count) valid = 0;
  }
  if (!CloseHandle(file)) valid = 0;
#else
  if (valid && !write_file && (fread(bytes, 1u, 1u, file) || ferror(file))) valid = 0;
  if (fclose(file)) valid = 0;
#endif
  if (copy && fclose(copy)) valid = 0;
  return valid;
}

static int change_byte(const char *path) {
  unsigned char value = 42u;
  FILE *file = fopen(path, "r+b");
  int valid;
  if (!file) return 0;
  valid = fwrite(&value, 1u, 1u, file) == 1u;
  return fclose(file) == 0 && valid;
}

static int clear_result(const char *frozen, const cupidbuild_host_snapshot_t *snapshot) {
  cupidbuild_host_snapshot_t empty;
  (void)memset(&empty, 0, sizeof(empty));
  return frozen == (const char *)0 && memcmp(snapshot, &empty, sizeof(empty)) == 0;
}

int main(int argc, char **argv) {
  char root[8192];
  char output[8192];
  char source[8192];
  char alias[8192];
  char writer[8192];
  const char *mode;
  const char *frozen = (const char *)1;
  unsigned long long size;
  unsigned long long capacity;
  cupidbuild_host_transaction_t *transaction = (cupidbuild_host_transaction_t *)0;
  cupidbuild_host_snapshot_t previous;
  cupidbuild_host_snapshot_t candidate;
  int accepted;
  int closed;
  int changed = -1;
  unsigned int index;
  if (argc == 5 && strcmp(argv[1], "emit-file") == 0) {
    unsigned long long seed;
    if (!decimal(argv[3], &size) || !decimal(argv[4], &seed) || seed > 255u) return 91;
    return pattern(argv[2], size, (unsigned int)seed, 1, (const char *)0) ? 0 : 103;
  }
  if (argc != 6 || !unhex(root, sizeof(root), argv[1]) ||
      !decimal(argv[3], &size) || !decimal(argv[4], &capacity)) return 90;
  mode = argv[2];
  if (snprintf(output, sizeof(output), "%s/nested/result.bin", root) < 0 ||
      snprintf(source, sizeof(source), "%s/source.cc", root) < 0 ||
      snprintf(alias, sizeof(alias), "%s/alias.bin", root) < 0) return 91;
  accepted = strcmp(mode, "runner") == 0
      ? cupidbuild_host_runner_open(root, &transaction)
      : cupidbuild_host_transaction_open_bounded(root, "source.cc", "nested/result.bin",
                                                 capacity, &transaction);
  if (!accepted) {
    closed = cupidbuild_host_transaction_close(transaction);
    (void)printf("open rejected, close %d\n", closed);
    return strcmp(mode, "capacity-excess") == 0 && closed ? 0 : 92;
  }
  (void)memset(&previous, 0xa5, sizeof(previous));
#if defined(CUPIDBUILD_PREVIOUS_OUTPUT_TEST_NEW_API)
  accepted = cupidbuild_host_freeze_previous_output(transaction,
      strcmp(mode, "unsafe") == 0 ? "../old.bin" : "previous.bin", &frozen, &previous);
#else
  accepted = cupidbuild_host_freeze_input_bounded(transaction, output,
      "previous.bin", capacity, &frozen, &previous);
#endif
  if (strcmp(mode, "runner") == 0 || strcmp(mode, "unsafe") == 0) {
    int clear = clear_result(frozen, &previous);
    int rejected = !accepted && !cupidbuild_host_require_publication_boundary(transaction);
    closed = cupidbuild_host_transaction_close(transaction);
    (void)printf("invalid %d clear %d close %d\n", rejected, clear, closed);
    return rejected && clear && closed ? 0 : 94;
  }
  if (!accepted) {
    (void)printf("previous rejected: %s\n", cupidbuild_host_error(transaction));
    (void)cupidbuild_host_transaction_close(transaction);
    return 93;
  }
  if (strcmp(mode, "absent") == 0) {
    if (!clear_result(frozen, &previous)) return 95;
    (void)printf("previous absent\n");
  } else {
    if (!previous.present || previous.size != size || !frozen ||
        !pattern(frozen, size, 17u, 0, argv[5]) ||
        !cupidbuild_host_require_inputs(transaction) ||
        !cupidbuild_host_require_frozen_inputs(transaction)) return 95;
    (void)printf("previous %u ", (unsigned int)previous.size);
    for (index = 0u; index < 32u; index++) (void)printf("%02x", previous.sha256[index]);
    (void)printf("\n");
  }
  if (strcmp(mode, "duplicate") == 0) {
#if defined(CUPIDBUILD_PREVIOUS_OUTPUT_TEST_NEW_API)
    const char *second = (const char *)1;
    (void)memset(&candidate, 0xa5, sizeof(candidate));
    if (cupidbuild_host_freeze_previous_output(transaction, "second.bin", &second, &candidate) ||
        !clear_result(second, &candidate) || cupidbuild_host_require_publication_boundary(transaction)) return 96;
#else
    return 96;
#endif
  } else if (strcmp(mode, "ordinary-alias") == 0 || strcmp(mode, "hardlink-alias") == 0) {
    const char *second = (const char *)1;
    (void)memset(&candidate, 0xa5, sizeof(candidate));
    if (cupidbuild_host_freeze_input_bounded(transaction,
        strcmp(mode, "hardlink-alias") == 0 ? alias : output,
        "second.bin", capacity, &second, &candidate) || !clear_result(second, &candidate)) return 96;
  } else if (strcmp(mode, "live-drift") == 0 || strcmp(mode, "source-drift") == 0) {
    int edited = change_byte(strcmp(mode, "source-drift") == 0 ? source : output);
    int valid = cupidbuild_host_require_publication_boundary(transaction);
    if (edited ? valid : !valid) return 97;
    (void)printf("live drift %s\n", edited ? "rejected" : "blocked");
  } else if (strcmp(mode, "frozen-drift") == 0) {
    int edited = change_byte(frozen);
    int valid = cupidbuild_host_require_inputs(transaction);
    if (edited && valid) return 98;
    if (!edited && !pattern(frozen, size, 17u, 0, (const char *)0)) return 98;
    (void)printf("frozen drift %s\n", edited ? "rejected" : valid ? "blocked" : "blocked, metadata rejected");
  } else if (strcmp(mode, "payload-limit") == 0) {
    size_t payload_size = 999u;
    unsigned char *bytes = cupidbuild_host_read_frozen_input(transaction, frozen, (size_t)size, &payload_size);
    if (bytes || !cupidbuild_host_require_frozen_inputs(transaction)) { free(bytes); return 99; }
  } else if (strcmp(mode, "changed") == 0 || strcmp(mode, "equal") == 0 || strcmp(mode, "absent") == 0) {
    unsigned int seed = strcmp(mode, "equal") == 0 ? 17u : 55u;
    const char *writer_frozen = (const char *)0;
    const char *arguments[5];
    if (snprintf(writer, sizeof(writer), "%s/writer", root) < 0 ||
        !cupidbuild_host_freeze_input(transaction, writer, "writer.exe", &writer_frozen, (cupidbuild_host_snapshot_t *)0) ||
        !cupidbuild_host_make_input_executable(transaction, writer_frozen)) return 100;
    arguments[0] = "emit-file";
    arguments[1] = cupidbuild_host_candidate(transaction);
    arguments[2] = argv[3];
    arguments[3] = seed == 17u ? "17" : "55";
    arguments[4] = (const char *)0;
    if (cupidbuild_host_run(transaction, writer_frozen, arguments, 30000u) != 0 ||
        !cupidbuild_host_capture_candidate(transaction, &candidate, (unsigned char **)0) ||
        !cupidbuild_host_publish_if_changed(transaction, &changed) ||
        changed != (strcmp(mode, "equal") != 0) ||
        !cupidbuild_host_require_inputs(transaction) ||
        !cupidbuild_host_require_frozen_inputs(transaction) ||
        !pattern(output, size, seed, 0, (const char *)0) ||
        (frozen && !pattern(frozen, size, 17u, 0, (const char *)0))) {
      (void)printf("publication failed: %s\n", cupidbuild_host_error(transaction));
      (void)cupidbuild_host_transaction_close(transaction);
      return 100;
    }
    (void)printf("published %d committed %d\n", changed, cupidbuild_host_publication_committed(transaction));
  } else if (strcmp(mode, "copy") != 0) return 101;
  closed = cupidbuild_host_transaction_close(transaction);
  (void)printf("close %d\n", closed);
  return closed ? 0 : 102;
}
