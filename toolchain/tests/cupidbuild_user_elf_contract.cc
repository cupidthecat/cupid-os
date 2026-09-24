#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "cupidbuild.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check_candidate(const unsigned char *bytes, size_t size) {
  char reason[160];
  char repeated[160];
  char tiny[3] = {'a', 'b', 'c'};
  unsigned char *copy = (unsigned char *)malloc(size == 0u ? 1u : size);
  int valid;
  int again;
  if (copy == (unsigned char *)0) {
    return 0;
  }
  (void)memcpy(copy, bytes, size);
  (void)memset(reason, 'x', sizeof(reason));
  valid = cupidbuild_validate_user_executable_bytes(
      bytes, size, reason, sizeof(reason));
  if (memchr(reason, '\0', sizeof(reason)) == (void *)0 ||
      (valid && reason[0] != '\0') || (!valid && reason[0] == '\0')) {
    free(copy);
    return 0;
  }
  if (cupidbuild_validate_user_executable_bytes(
          bytes, size, (char *)0, sizeof(reason)) ||
      cupidbuild_validate_user_executable_bytes(bytes, size, tiny + 1u, 0u) ||
      tiny[0] != 'a' || tiny[1] != 'b' || tiny[2] != 'c' ||
      cupidbuild_validate_user_executable_bytes(
          bytes, size, tiny + 1u, 1u) != valid ||
      tiny[0] != 'a' || tiny[1] != '\0' || tiny[2] != 'c') {
    free(copy);
    return 0;
  }
  /* A failed call must not leak diagnostics into the next invocation. */
  if (cupidbuild_validate_user_executable_bytes(
          (const unsigned char *)0, size, repeated, sizeof(repeated)) ||
      strcmp(repeated, "ELF header is outside the linked executable") != 0) {
    free(copy);
    return 0;
  }
  (void)memset(repeated, 'y', sizeof(repeated));
  again = cupidbuild_validate_user_executable_bytes(
      bytes, size, repeated, sizeof(repeated));
  if (again != valid || strcmp(reason, repeated) != 0 ||
      memcmp(bytes, copy, size) != 0) {
    free(copy);
    return 0;
  }
  free(copy);
  (void)printf("%s\n", valid ? "ok" : reason);
  return 1;
}

static int run_batch(const char *path) {
  FILE *stream = fopen(path, "rb");
  unsigned char length[4];
  size_t received;
  int ok = 1;
  if (stream == (FILE *)0) {
    return 1;
  }
  for (;;) {
    size_t size;
    unsigned char *bytes;
    received = fread(length, 1u, sizeof(length), stream);
    if (received == 0u) {
      ok = !ferror(stream);
      break;
    }
    if (received != sizeof(length)) {
      ok = 0;
      break;
    }
    size = (size_t)length[0] | ((size_t)length[1] << 8u) |
           ((size_t)length[2] << 16u) | ((size_t)length[3] << 24u);
    if (size > 16777216u) {
      ok = 0;
      break;
    }
    bytes = (unsigned char *)malloc(size == 0u ? 1u : size);
    if (bytes == (unsigned char *)0) {
      ok = 0;
      break;
    }
    if (fread(bytes, 1u, size, stream) != size ||
        !check_candidate(bytes, size)) {
      ok = 0;
    }
    free(bytes);
    if (!ok) {
      break;
    }
  }
  if (fclose(stream) != 0) {
    ok = 0;
  }
  return ok ? 0 : 1;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "batch") == 0) {
    return run_batch(argv[2]);
  }
  (void)fprintf(stderr, "usage: cupidbuild-user-elf-contract batch REQUEST\n");
  return 2;
}
