#ifndef CUPID_HOSTED_I386_LINUX_STRING_H
#define CUPID_HOSTED_I386_LINUX_STRING_H

#include <cupid_host_abi.h>

void *memcpy(void *destination, const void *source, size_t bytes);
void *memmove(void *destination, const void *source, size_t bytes);
int memcmp(const void *left, const void *right, size_t bytes);
void *memchr(const void *memory, int value, size_t bytes);
void *memset(void *destination, int value, size_t bytes);
char *strchr(const char *text, int character);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
char *strstr(const char *text, const char *needle);
size_t strlen(const char *text);

#endif
