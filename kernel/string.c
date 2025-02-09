#include "string.h"

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* strcpy(char* dest, const char* src) {
    char* tmp = dest;
    while ((*dest++ = *src++));
    return tmp;
}

char* strncpy(char* dest, const char* src, size_t n) {
    char* tmp = dest;
    // Copy characters from src to dest until n is 0 or a null is copied.
    while (n && (*dest = *src)) {
        dest++;
        src++;
        n--;
    }
    // If n is not yet 0, pad the remainder of dest with '\0'
    while (n--) {
        *dest++ = '\0';
    }
    return tmp;
}


void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = dest;
    const uint8_t* s = src;
    while (n--) *d++ = *s++;
    return dest;
}

void* memset(void* ptr, int value, size_t num) {
    unsigned char* p = ptr;
    while(num-- > 0) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}