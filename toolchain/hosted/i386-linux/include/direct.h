#ifndef CUPID_HOSTED_I386_LINUX_DIRECT_H
#define CUPID_HOSTED_I386_LINUX_DIRECT_H

#include <cupid_host_abi.h>

char *_getcwd(char *destination, int capacity);
char *_fullpath(char *destination, const char *path, size_t capacity);

#endif
