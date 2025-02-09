#ifndef STRING_H
#define STRING_H

#include "types.h"

size_t strlen(const char* str);
int strcmp(const char* s1, const char* s2);
void* memset(void* ptr, int value, size_t num);

#endif