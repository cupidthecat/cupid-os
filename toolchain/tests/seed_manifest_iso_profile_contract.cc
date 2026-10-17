#include "seed_manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_input(const char *name, size_t limit, size_t *size) {
  FILE *file = fopen(name, "rb");
  long length;
  unsigned char *bytes;
  if (!file) return NULL;
  if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
      (size_t)length > limit || fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }
  bytes = (unsigned char *)malloc((size_t)length + 1u);
  if (!bytes) { fclose(file); return NULL; }
  if (fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
    free(bytes);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *size = (size_t)length;
  return bytes;
}

static int cleared(const cupid_seed_manifest_result_t *result) {
  const unsigned char *bytes = (const unsigned char *)result;
  size_t index;
  for (index = 0u; index < sizeof(*result); index++) {
    if (bytes[index] != 0u) return 0;
  }
  return 1;
}

static int number(const char *text, unsigned int maximum, unsigned int *out) {
  unsigned int value = 0u;
  if (!text || !*text) return 0;
  while (*text) {
    unsigned int digit = (unsigned int)(unsigned char)*text++ - '0';
    if (digit > 9u || value > maximum / 10u ||
        value * 10u + digit > maximum) return 0;
    value = value * 10u + digit;
  }
  *out = value;
  return 1;
}

int main(int argc, char **argv) {
  unsigned int format;
  unsigned int capacity_value;
  unsigned int strict_value;
  size_t capacity;
  int strict;
  unsigned char *manifest;
  unsigned char *release = NULL;
  unsigned char *manifest_before;
  unsigned char *release_before = NULL;
  size_t manifest_size = 0u;
  size_t release_size = 0u;
  cupid_seed_manifest_result_t result;
  unsigned char diagnostic[136];
  char *error;
  size_t index;
  int status;
  if (argc != 6) return 2;
  if (!number(argv[1], 2u, &format) || format == 0u ||
      !number(argv[2], 128u, &capacity_value) ||
      !number(argv[3], 1u, &strict_value)) return 2;
  capacity = (size_t)capacity_value;
  strict = (int)strict_value;
  manifest = read_input(argv[4], 1048576u, &manifest_size);
  if (!manifest) return 3;
  manifest_before = (unsigned char *)malloc(manifest_size + 1u);
  if (!manifest_before) return 3;
  memcpy(manifest_before, manifest, manifest_size);
  if (!strict) {
    release = read_input(argv[5], 65536u, &release_size);
    if (!release) return 3;
    release_before = (unsigned char *)malloc(release_size + 1u);
    if (!release_before) return 3;
    memcpy(release_before, release, release_size);
  }
  memset(&result, 0xa5, sizeof(result));
  if (cupid_seed_manifest_validate(NULL, 1u, format, &result, NULL, 0u) != 0 ||
      !cleared(&result)) return 4;
  memset(&result, 0xa5, sizeof(result));
  memset(diagnostic, '!', sizeof(diagnostic));
  error = capacity ? (char *)diagnostic : NULL;
  status = strict ? cupid_seed_manifest_validate(manifest, manifest_size, format,
      &result, error, capacity) : cupid_seed_manifest_validate_release(release,
      release_size, manifest, manifest_size, format, &result, error, capacity);
  if (memcmp(manifest_before, manifest, manifest_size) != 0 ||
      (!strict && memcmp(release_before, release, release_size) != 0)) return 5;
  for (index = capacity; index < sizeof(diagnostic); index++) {
    if (diagnostic[index] != '!') return 6;
  }
  if (capacity && memchr(diagnostic, 0, capacity) == NULL) return 7;
  if (!status) {
    if (!cleared(&result) || (capacity > 1u && diagnostic[0] == 0u)) return 8;
    printf("0\n");
  } else {
    if (capacity && diagnostic[0] != 0u) return 8;
    printf("1 %u %u", result.artifact_count, result.current_windows_plan);
    for (index = 0u; index < result.artifact_count; index++) {
      printf(" %s %u %s", result.artifacts[index].file,
          result.artifacts[index].size, result.artifacts[index].sha256);
    }
    printf("\n");
  }
  free(release_before);
  free(release);
  free(manifest_before);
  free(manifest);
  return 0;
}
