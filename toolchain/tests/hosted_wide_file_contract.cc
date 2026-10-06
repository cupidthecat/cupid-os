#ifndef CUPID_TEST_WIDE_RUNTIME
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef CUPID_TEST_WIDE_RUNTIME
#ifndef _WIN32
#include <sys/types.h>
#endif

static int cupid_fseek64(FILE *stream, long long offset, int origin) {
  if (stream == (FILE *)0) { errno = EBADF; return -1; }
  if (origin < SEEK_SET || origin > SEEK_END ||
      (origin == SEEK_SET && offset < 0)) { errno = EINVAL; return -1; }
#ifdef _WIN32
  return _fseeki64(stream, offset, origin);
#else
  return fseeko(stream, (off_t)offset, origin);
#endif
}

static int cupid_ftell64(FILE *stream, long long *position_out) {
  long long result;
  if (position_out == (long long *)0) { errno = EINVAL; return -1; }
  *position_out = 0;
  if (stream == (FILE *)0) { errno = EBADF; return -1; }
#ifdef _WIN32
  result = _ftelli64(stream);
#else
  result = (long long)ftello(stream);
#endif
  if (result < 0) return -1;
  *position_out = result;
  return 0;
}
#endif

int main(int argc, char **argv) {
  FILE *stream = (FILE *)0;
  long long four_gib = (long long)1 << 32;
  long long position = 99;
  unsigned long long bits;
  unsigned char value = 0;
  unsigned int count = 0;
  int seek = -9, tell = -9, failed = 0, failed_code = 0, error = -1;
  char mode;
  if (argc != 3 || argv[1][0] == '\0' || argv[1][1] != '\0') return 2;
  mode = argv[1][0];
  if (mode == 'm') {
    errno = 0;
    seek = cupid_fseek64((FILE *)0, 0, SEEK_SET);
    failed = errno != 0;
    failed_code = errno;
    tell = cupid_ftell64((FILE *)0, &position);
  } else {
    stream = fopen(argv[2], (mode == 'g' || mode == 'h') ? "a+b" : "r+b");
    if (stream == (FILE *)0) {
      printf("open=0 seek=-9 tell=-9 high=0 low=0 count=0 value=0 errno=%d errno_code=%d error=-1\n", errno != 0, errno);
      return 0;
    }
    errno = 0;
    if (mode == 'a') seek = cupid_fseek64(stream, (long long)1 << 31, SEEK_SET);
    else if (mode == 'b') seek = cupid_fseek64(stream, four_gib + 33, SEEK_SET);
    else if (mode == 'c') seek = cupid_fseek64(stream, four_gib - 1, SEEK_SET);
    else if (mode == 'r') {
      if (cupid_fseek64(stream, 33, SEEK_SET) != 0) return 3;
      if (cupid_fseek64(stream, -34, SEEK_CUR) != -1 || errno == 0) return 5;
      errno = 0;
      seek = cupid_fseek64(stream, four_gib - 1, SEEK_SET);
    }
    else if (mode == 'd') {
      if (cupid_fseek64(stream, four_gib + 33, SEEK_SET) != 0) return 3;
      seek = cupid_fseek64(stream, -four_gib, SEEK_CUR);
    } else if (mode == 'e') seek = cupid_fseek64(stream, -17, SEEK_END);
    else if (mode == 'i') {
      if (cupid_fseek64(stream, 33, SEEK_SET) != 0) return 3;
      seek = cupid_fseek64(stream, four_gib, SEEK_CUR);
    } else if (mode == 'q') seek = cupid_fseek64(stream, -(four_gib + 32), SEEK_END);
    else if (mode == 'f') {
      seek = cupid_fseek64(stream, four_gib + 9, SEEK_SET);
      if (seek == 0) count = (unsigned int)fwrite("wide", 1, 4, stream);
    } else if (mode == 'g' || mode == 'h') {
      seek = cupid_fseek64(stream, 0, SEEK_SET);
      if (seek == 0) count = (unsigned int)fwrite("++", 1, 2, stream);
    } else if (mode == 'j' || mode == 'k' || mode == 'l' || mode == 'n') {
      if (cupid_fseek64(stream, 33, SEEK_SET) != 0) return 3;
      if (mode == 'j') seek = cupid_fseek64(stream, 0, 3);
      else if (mode == 'k') seek = cupid_fseek64(stream, -1, SEEK_SET);
      else if (mode == 'l') seek = cupid_fseek64(stream, -34, SEEK_CUR);
      else seek = cupid_ftell64(stream, (long long *)0);
    } else if (mode != 'o') return 2;
    failed = errno != 0;
    failed_code = errno;
    if ((mode == 'f' || mode == 'g' || mode == 'h') && fflush(stream) != 0) return 4;
    if (seek == 0 && (mode == 'a' || mode == 'b' || mode == 'c' ||
        mode == 'd' || mode == 'e' || mode == 'i' || mode == 'q' || mode == 'r')) {
      count = (unsigned int)fread(&value, 1, 1, stream);
    }
    tell = cupid_ftell64(stream, &position);
    error = ferror(stream) != 0;
  }
  bits = (unsigned long long)position;
  printf("open=%d seek=%d tell=%d high=%u low=%u count=%u value=%u errno=%d errno_code=%d error=%d\n",
      stream != (FILE *)0, seek, tell, (unsigned int)(bits >> 32),
      (unsigned int)bits, count, (unsigned int)value, failed, failed_code, error);
  if (stream != (FILE *)0 && fclose(stream) != 0) return 4;
  return 0;
}
