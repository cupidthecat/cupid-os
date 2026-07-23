#ifndef CUPID_HOSTED_I386_LINUX_STDLIB_H
#define CUPID_HOSTED_I386_LINUX_STDLIB_H

#include <cupid_host_abi.h>

void *malloc(size_t bytes);
void *realloc(void *allocation, size_t bytes);
void free(void *allocation);

#endif
