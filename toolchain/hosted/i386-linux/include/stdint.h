#ifndef CUPID_HOSTED_I386_LINUX_STDINT_H
#define CUPID_HOSTED_I386_LINUX_STDINT_H

#include <cupid_host_abi.h>

typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef signed long long int64_t;
typedef unsigned long long uint64_t;
typedef signed int intptr_t;
typedef unsigned int uintptr_t;

#define INT8_MIN (-127 - 1)
#define INT8_MAX 127
#define UINT8_MAX 255u
#define INT16_MIN (-32767 - 1)
#define INT16_MAX 32767
#define UINT16_MAX 65535u
#define INT32_MIN (-2147483647 - 1)
#define INT32_MAX 2147483647
#define UINT32_MAX 4294967295u
#define INT64_C(value) value##ll
#define UINT64_C(value) value##ull
#define INT64_MIN (-INT64_C(9223372036854775807) - 1)
#define INT64_MAX INT64_C(9223372036854775807)
#define UINT64_MAX UINT64_C(18446744073709551615)
#define INTPTR_MIN INT32_MIN
#define INTPTR_MAX INT32_MAX
#define UINTPTR_MAX UINT32_MAX
#define SIZE_MAX UINT32_MAX

#endif
