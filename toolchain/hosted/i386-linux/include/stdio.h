#ifndef CUPID_HOSTED_I386_LINUX_STDIO_H
#define CUPID_HOSTED_I386_LINUX_STDIO_H

#include <cupid_host_abi.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct _IO_FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
int fopen_s(FILE **stream_out, const char *path, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
int ferror(FILE *stream);
int fputc(int character, FILE *stream);
int putchar(int character);
int fputs(const char *text, FILE *stream);
int fprintf(FILE *stream, const char *format, ...);
int printf(const char *format, ...);
int puts(const char *text);
int snprintf(char *destination, size_t capacity, const char *format, ...);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
/* Cupid hosted extensions. Positions and deltas use signed 64-bit values;
 * ftell64 clears the required output on failure. Standard long APIs keep
 * their existing ABI. Seekable regular-file streams are unbuffered and require
 * serialized use. Seeking does not validate or flush a disk-image candidate. */
int cupid_fseek64(FILE *stream, long long offset, int origin);
int cupid_ftell64(FILE *stream, long long *position_out);
size_t fread(void *destination, size_t width, size_t count, FILE *stream);
size_t fwrite(const void *source, size_t width, size_t count, FILE *stream);

#endif
