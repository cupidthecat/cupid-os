#ifndef CUPID_HOSTED_I386_LINUX_ERRNO_H
#define CUPID_HOSTED_I386_LINUX_ERRNO_H

#include <cupid_host_abi.h>

#define ENOENT 2
#define ERANGE 34
#define errno (*__errno_location())

int *__errno_location(void);

#endif
