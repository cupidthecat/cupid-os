#ifndef CUPID_HOSTED_I386_LINUX_STDDEF_H
#define CUPID_HOSTED_I386_LINUX_STDDEF_H

#include "cupid_host_abi.h"

typedef int ptrdiff_t;
typedef int wchar_t;

typedef union {
  long long integer_alignment;
  long double floating_alignment;
  void *pointer_alignment;
} max_align_t;

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#endif
