#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "bytes") == 0) {
    int character;
    for (character = -256; character < 512; character++) {
      if (putchar(character) != (int)(unsigned char)character) return 3;
    }
    return fflush(stdout) == 0 ? 0 : 4;
  }
  if (argc == 3 && strcmp(argv[1], "blocked") == 0) {
    FILE *original = stdout;
    FILE *blocked = fopen(argv[2], "rb");
    int result, failed, saved_error;
    if (blocked == (FILE *)0) return 5;
    stdout = blocked;
    errno = 0;
    result = putchar(0x14a);
    failed = ferror(blocked);
    saved_error = errno;
    stdout = original;
    if (fclose(blocked) != 0 || result != EOF || failed == 0 || saved_error == 0) return 6;
    if (putchar('R') != 'R' || fflush(stdout) != 0) return 7;
    return printf(" blocked=1 error=1 errno=1\n") > 0 ? 0 : 8;
  }
  return 2;
}
