#ifndef CUPID_HOSTED_I386_LINUX_STDIO_H
#define CUPID_HOSTED_I386_LINUX_STDIO_H

#include <cupid_host_abi.h>

#define SEEK_END 2

typedef struct _IO_FILE FILE;

extern FILE *stdout;
extern FILE *stderr;

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
int ferror(FILE *stream);
int fprintf(FILE *stream, const char *format, ...);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
size_t fread(void *destination, size_t width, size_t count, FILE *stream);
size_t fwrite(const void *source, size_t width, size_t count, FILE *stream);

#endif
