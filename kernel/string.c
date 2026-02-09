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

void* memset(void* ptr, int value, size_t num) {
    unsigned char* p = ptr;
    while(num-- > 0) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = dest;
    const unsigned char* s = src;
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dest;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++)) {}
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (n > 0 && *src) {
        *d++ = *src++;
        n--;
    }
    while (n > 0) {
        *d++ = '\0';
        n--;
    }
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++)) {}
    return dest;
}

char* strchr(const char* s, int c) {
    char* p = (char*)(unsigned long)s;
    while (*p) {
        if (*p == (char)c) return p;
        p++;
    }
    return (c == 0) ? p : (char*)0;
}

char* strrchr(const char* s, int c) {
    char* last = (char*)0;
    char* p = (char*)(unsigned long)s;
    while (*p) {
        if (*p == (char)c) last = p;
        p++;
    }
    return (c == 0) ? p : last;
}

char* strstr(const char* haystack, const char* needle) {
    char* h0 = (char*)(unsigned long)haystack;
    if (!*needle) return h0;
    while (*h0) {
        const char* h = h0;
        const char* n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) return h0;
        h0++;
    }
    return (char*)0;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = s1;
    const unsigned char* p2 = s2;
    while (n-- > 0) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}