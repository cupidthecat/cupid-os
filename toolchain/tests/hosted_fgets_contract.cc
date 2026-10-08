#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  char storage[66];
  char *buffer = storage + 1;
  const char *mode;
  FILE *stream;
  unsigned int capacity = 0u, index, calls = 0u;
  int negative = 0, argument = 0;
  if (argc != 4) return 2;
  if (argv[2][0] == '-') { negative = 1; argument++; }
  while (argv[2][argument] != '\0') {
    if (argv[2][argument] < '0' || argv[2][argument] > '9' || capacity > 100u) return 3;
    capacity = capacity * 10u + (unsigned int)(argv[2][argument++] - '0');
  }
  if (capacity > 64u) return 4;
  mode = argv[3];
  stream = fopen(argv[1], strcmp(mode, "error") == 0 ? "ab" : "rb");
  if (stream == NULL) return 5;
  for (;;) {
    char *result;
    long before = ftell(stream), bytes;
    if (before < 0) return 20;
    memset(storage, 0xa5, sizeof(storage));
    errno = 123;
    result = fgets(buffer, negative ? -(int)capacity : (int)capacity, stream);
    if ((unsigned char)storage[0] != 0xa5u || (unsigned char)storage[65] != 0xa5u) return 6;
    if (strcmp(mode, "error") == 0) {
      int saved = errno;
#if defined(CUPID_FGETS_NATIVE) && defined(_WIN32)
      if (result != NULL || !ferror(stream) || feof(stream) || saved != 123) return 7;
#else
      if (result != NULL || !ferror(stream) || feof(stream) || saved == 123 || saved == 0) return 7;
#endif
      clearerr(stream);
      printf("error cleared=%d errno=%d\n", !ferror(stream) && !feof(stream), errno == saved);
      break;
    }
    if (negative || capacity == 0u) {
      if (result != NULL || errno != EINVAL || ferror(stream) || feof(stream)) return 8;
      for (index = 0u; index < sizeof(storage); index++) if ((unsigned char)storage[index] != 0xa5u) return 9;
      puts("invalid preserved=1");
      break;
    }
    if (capacity == 1u) {
      if (result != buffer || buffer[0] != '\0' || ftell(stream) != 0 || feof(stream) || ferror(stream)) return 10;
      puts("unit preserved=1");
      break;
    }
    if (result == NULL) {
      if (!feof(stream) || ferror(stream)) return 11;
      for (index = 0u; index < sizeof(storage); index++) if ((unsigned char)storage[index] != 0xa5u) return 12;
      printf("end eof=%d error=%d errno=%d\n", feof(stream) != 0, ferror(stream) != 0, errno == 123);
      if (strcmp(mode, "tell") == 0) {
        if (ftell(stream) < 0 || !feof(stream)) return 13;
        puts("tell kept-eof=1");
      } else if (strcmp(mode, "seek") == 0 || strcmp(mode, "seek64") == 0) {
        int status;
        if (strcmp(mode, "seek64") == 0) {
#if defined(CUPID_FGETS_NATIVE)
          status = fseek(stream, 0, SEEK_SET);
#else
          status = cupid_fseek64(stream, 0, SEEK_SET);
#endif
        } else status = fseek(stream, 0, SEEK_SET);
        if (status != 0 || feof(stream) || ferror(stream) || fgets(buffer, 64, stream) != buffer) return 14;
        puts("seek cleared-eof=1 reread=1");
      } else if (strcmp(mode, "clear") == 0) {
        clearerr(stream);
        if (feof(stream) || ferror(stream)) return 15;
        puts("clear kept-error-free=1");
      } else if (strcmp(mode, "fread") == 0) {
        clearerr(stream);
        if (fread(buffer, 1u, 1u, stream) != 0u || !feof(stream) || ferror(stream)) return 16;
        puts("fread set-eof=1");
      }
      break;
    }
    if (result != buffer || ferror(stream) || errno != 123) return 17;
    bytes = ftell(stream) - before;
    if (bytes < 0 || bytes >= (long)capacity || buffer[bytes] != '\0') return 21;
    for (index = (unsigned int)bytes + 1u; index < 64u; index++) {
      if ((unsigned char)buffer[index] != 0xa5u) return 22;
    }
    printf("line ");
    for (index = 0u; index < (unsigned int)bytes; index++) {
      printf("%02x", (unsigned int)(unsigned char)buffer[index]);
    }
    printf(" eof=%d\n", feof(stream) != 0);
    if (++calls > 10000u) return 18;
  }
  return fclose(stream) == 0 ? 0 : 19;
}
