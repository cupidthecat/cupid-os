#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  FILE *stream;
  char bytes[16];
  size_t length;
  int result = 0;
  if (argc != 4) return 2;
  errno = 0;
  stream = fopen(!strcmp(argv[1], "nullpath") ? NULL : argv[2],
                 !strcmp(argv[1], "nullmode") ? NULL : argv[1]);
  if (!strcmp(argv[3], "invalid")) {
    if (stream != NULL || errno != EINVAL) result = 10;
  } else if (!strcmp(argv[3], "missing")) {
    if (stream != NULL || errno != ENOENT) result = 11;
  } else if (stream == NULL) result = 12;
  else if (!strcmp(argv[3], "update")) {
    if (fread(bytes, 1u, 2u, stream) != 2u || memcmp(bytes, "ab", 2u)) result = 13;
    if (!result && (fseek(stream, 2L, SEEK_SET) || fwrite("XYZ", 1u, 3u, stream) != 3u ||
                   fseek(stream, 0L, SEEK_SET))) result = 14;
    if (!result && (fread(bytes, 1u, 8u, stream) != 8u || memcmp(bytes, "abXYZfgh", 8u))) result = 15;
  } else if (!strcmp(argv[3], "truncate")) {
    if (ftell(stream) != 0L || fwrite("XYZ", 1u, 3u, stream) != 3u ||
        fseek(stream, 0L, SEEK_SET) || fread(bytes, 1u, 3u, stream) != 3u ||
        memcmp(bytes, "XYZ", 3u)) result = 16;
  } else if (!strcmp(argv[3], "append")) {
    if (fseek(stream, 0L, SEEK_SET) || fread(bytes, 1u, 8u, stream) != 8u ||
        memcmp(bytes, "abcdefgh", 8u) || fseek(stream, 0L, SEEK_SET) ||
        fwrite("XYZ", 1u, 3u, stream) != 3u || fseek(stream, 0L, SEEK_SET)) result = 17;
    if (!result && (fread(bytes, 1u, 11u, stream) != 11u ||
                   memcmp(bytes, "abcdefghXYZ", 11u))) result = 18;
  } else if (!strcmp(argv[3], "create-append")) {
    if (fseek(stream, 0L, SEEK_SET) || fread(bytes, 1u, 1u, stream) != 0u ||
        ferror(stream) || fseek(stream, 0L, SEEK_SET) ||
        fwrite("XYZ", 1u, 3u, stream) != 3u || fseek(stream, 0L, SEEK_SET)) result = 19;
    if (!result && (fread(bytes, 1u, 3u, stream) != 3u || memcmp(bytes, "XYZ", 3u))) result = 20;
  } else if (!strcmp(argv[3], "read")) {
    length = fread(bytes, 1u, sizeof(bytes), stream);
    if (length != 8u || memcmp(bytes, "abcdefgh", 8u)) result = 21;
  } else result = 22;
  if (stream != NULL && fclose(stream)) result = 23;
  if (result) fprintf(stderr, "update mode %s: %d errno=%d\n", argv[1], result, errno);
  return result;
}
