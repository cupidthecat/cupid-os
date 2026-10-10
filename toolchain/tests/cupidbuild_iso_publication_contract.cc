#include "cupidbuild_iso_publication.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int publication_hex_digit(char byte) {
  if (byte >= '0' && byte <= '9') return byte - '0';
  if (byte >= 'a' && byte <= 'f') return byte - 'a' + 10;
  return -1;
}

static int publication_decode(const char *text, char *out) {
  size_t size = strlen(text), index;
  if (size >= 16384u || (size & 1u) != 0u) return 0;
  for (index = 0u; index < size / 2u; index++) {
    int a = publication_hex_digit(text[index * 2u]);
    int b = publication_hex_digit(text[index * 2u + 1u]);
    if (a < 0 || b < 0 || (a == 0 && b == 0)) return 0;
    out[index] = (char)(a * 16 + b);
  }
  out[size / 2u] = '\0'; return 1;
}

int main(int argc, char **argv) {
  char paths[7][8192];
  char error[264];
  cupidbuild_iso_publication_request_t request;
  cupidbuild_iso_publication_result_t result;
  cupidbuild_iso_publication_result_t empty;
  ctool_u32 capacity = 256u;
  int index, ok;
  const char *mode;
  if (argc != 9) return 90;
  for (index = 0; index < 7; index++)
    if (!publication_decode(argv[index + 1], paths[index])) return 91;
  mode = argv[8];
  request.repository_root = paths[0]; request.manifest_path = paths[1];
  request.fixtures_path = paths[2]; request.output_path = paths[3];
  request.linux_manifest_path = paths[4]; request.windows_manifest_path = paths[5];
  request.seed_release_path = paths[6];
  memset(error, '!', sizeof(error)); memset(&result, 0xa5, sizeof(result));
  memset(&empty, 0, sizeof(empty));
  if (strcmp(mode, "cap0") == 0) capacity = 0u;
  if (strcmp(mode, "cap1") == 0) capacity = 1u;
  ok = cupidbuild_iso_publish(strcmp(mode, "null-request") == 0 ? NULL : &request,
      strcmp(mode, "null-result") == 0 ? NULL : &result,
      capacity == 0u || strcmp(mode, "null-error") == 0 ? NULL : error, capacity);
  if (strcmp(mode, "null-error") != 0 && capacity != 0u) {
    for (index = (int)capacity; index < 264; index++) if (error[index] != '!') return 92;
    if (memchr(error, '\0', capacity) == NULL) return 93;
  }
  if (!ok) {
    if (strcmp(mode, "null-result") != 0 && memcmp(&result, &empty, sizeof(result)) != 0) return 94;
    (void)fprintf(stderr, "rejected%s%s\n", capacity && strcmp(mode, "null-error") != 0 ? ": " : "",
        capacity && strcmp(mode, "null-error") != 0 ? error : "");
    return 1;
  }
  if (capacity && error[0] != '\0') return 95;
  (void)printf("publish %d %d %u %u %u %llu %u\n", result.changed, result.committed,
      result.image.inventory.directories, result.image.inventory.files,
      result.image.inventory.directory_depth, result.image.inventory.file_bytes,
      result.image.image_bytes);
  return 0;
}
