#ifndef CUPID_HOSTED_I386_LINUX_ABI_H
#define CUPID_HOSTED_I386_LINUX_ABI_H

#if !defined(__SIZEOF_POINTER__) || __SIZEOF_POINTER__ != 4
#error Cupid i386 Linux headers require a four-byte pointer target
#endif

#ifndef CUPID_HOSTED_SIZE_T_DEFINED
#define CUPID_HOSTED_SIZE_T_DEFINED
typedef unsigned int size_t;
#endif

#endif
