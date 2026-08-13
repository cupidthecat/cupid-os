/* Checked by the Cupid-owned native Windows toolchain closure. */

#include <direct.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CUPID_RUNTIME_UINT_MAX 4294967295u

static int allocator_contract(void) {
  unsigned char *allocation;
  unsigned char *replacement;
  void *overflow;
  size_t index;

  allocation = (unsigned char *)calloc(16u, 1u);
  if (allocation == (unsigned char *)0) {
    return 11;
  }
  for (index = 0u; index < 16u; index++) {
    if (allocation[index] != 0u) {
      free(allocation);
      return 12;
    }
    allocation[index] = (unsigned char)(index + 1u);
  }
  replacement = (unsigned char *)realloc(allocation, 64u);
  if (replacement == (unsigned char *)0) {
    free(allocation);
    return 13;
  }
  for (index = 0u; index < 16u; index++) {
    if (replacement[index] != (unsigned char)(index + 1u)) {
      free(replacement);
      return 14;
    }
  }
  free(replacement);

  errno = 0;
  overflow = calloc(CUPID_RUNTIME_UINT_MAX, 2u);
  if (overflow != (void *)0 || errno != ENOMEM) {
    free(overflow);
    return 15;
  }
  return 0;
}

static int file_contract(const char *output_path,
                         const char *missing_path) {
  static const char first[] = "head";
  static const char appended[] = "tail";
  static const char expected[] = "headtail";
  char contents[9];
  FILE *stream = (FILE *)0;

  if (fopen_s(&stream, output_path, "wb") != 0 ||
      stream == (FILE *)0) {
    return 21;
  }
  if (fwrite(first, 1u, 4u, stream) != 4u || fclose(stream) != 0) {
    return 22;
  }

  stream = fopen(output_path, "ab");
  if (stream == (FILE *)0) {
    return 23;
  }
  if (fseek(stream, 0L, 0) != 0 ||
      fwrite(appended, 1u, 4u, stream) != 4u ||
      fclose(stream) != 0) {
    return 24;
  }

  stream = fopen(output_path, "rb");
  if (stream == (FILE *)0) {
    return 25;
  }
  (void)memset(contents, 0, sizeof(contents));
  if (fread(contents, 1u, 8u, stream) != 8u ||
      fread(contents + 8, 1u, 1u, stream) != 0u ||
      ferror(stream) != 0 || fclose(stream) != 0 ||
      memcmp(contents, expected, 8u) != 0) {
    return 26;
  }

  errno = 0;
  if (fopen_s((FILE **)0, output_path, "rb") != EINVAL ||
      errno != EINVAL) {
    return 27;
  }
  errno = 0;
  stream = (FILE *)0;
  if (fopen_s(&stream, missing_path, "rb") != ENOENT ||
      stream != (FILE *)0 || errno != ENOENT) {
    return 28;
  }
  return 0;
}

static int directory_contract(void) {
  char directory[512];
  char small[1];

  if (getcwd(directory, sizeof(directory)) != directory ||
      directory[0] == '\0') {
    return 31;
  }
  errno = 0;
  if (_getcwd(small, 1) != (char *)0 || errno != ERANGE) {
    return 32;
  }
  errno = 0;
  if (getcwd((char *)0, sizeof(directory)) != (char *)0 ||
      errno != EINVAL) {
    return 33;
  }
  return 0;
}

int main(int argc, char **argv) {
  int result;
  int index;
  if (argc != 7 || strcmp(argv[1], "plain") != 0 ||
      strcmp(argv[2], "space arg") != 0 ||
      strcmp(argv[3], "quote\"arg") != 0 ||
      strcmp(argv[4], "trailing\\") != 0) {
    (void)fprintf(stderr, "windows runtime arguments: bad argc=%d", argc);
    for (index = 0; index < argc; index++) {
      (void)fprintf(stderr, " [%s]", argv[index]);
    }
    (void)fputc('\n', stderr);
    return 41;
  }
  result = allocator_contract();
  if (result == 0) {
    result = file_contract(argv[5], argv[6]);
  }
  if (result == 0) {
    result = directory_contract();
  }
  if (result != 0) {
    (void)fprintf(stderr, "windows runtime contract: %d\n", result);
    return result;
  }
  (void)puts("Cupid-built Windows tool runtime: ok");
  return 0;
}
