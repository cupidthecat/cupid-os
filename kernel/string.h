#ifndef STRING_H
#define STRING_H

#include "types.h"

size_t strlen(const char* str);
int strcmp(const char* s1, const char* s2);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* ptr, int value, size_t num);
#endif  