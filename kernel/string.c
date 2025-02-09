#include "string.h"

/**
 * strlen - Calculate the length of a null-terminated string
 * @str: Pointer to the null-terminated string to measure
 * 
 * Returns: The number of characters in the string (excluding null terminator)
 */
size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

/**
 * strcmp - Compare two null-terminated strings
 * @s1: First string to compare
 * @s2: Second string to compare
 * 
 * Returns: 
 *   <0 if s1 is less than s2
 *    0 if s1 is equal to s2
 *   >0 if s1 is greater than s2
 */
int strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

/**
 * strcpy - Copy a null-terminated string
 * @dest: Pointer to destination buffer
 * @src: Pointer to source string to copy
 * 
 * Returns: Pointer to destination buffer
 * 
 * Note: Destination buffer must be large enough to hold src + null terminator
 */
char* strcpy(char* dest, const char* src) {
    char* tmp = dest;
    while ((*dest++ = *src++));
    return tmp;
}

/**
 * strncpy - Copy up to n characters from a string
 * @dest: Pointer to destination buffer
 * @src: Pointer to source string to copy
 * @n: Maximum number of characters to copy
 * 
 * Returns: Pointer to destination buffer
 * 
 * If src is shorter than n, the remainder of dest is padded with null bytes
 */
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

/**
 * memcpy - Copy n bytes from source to destination
 * @dest: Pointer to destination buffer
 * @src: Pointer to source buffer
 * @n: Number of bytes to copy
 * 
 * Returns: Pointer to destination buffer
 * 
 * Note: Memory areas must not overlap (use memmove for overlapping areas)
 */
void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = dest;
    const uint8_t* s = src;
    while (n--) *d++ = *s++;
    return dest;
}

/**
 * memset - Fill memory with a constant byte
 * @ptr: Pointer to memory area to fill
 * @value: Value to set (converted to unsigned char)
 * @num: Number of bytes to set
 * 
 * Returns: Pointer to the memory area
 */
void* memset(void* ptr, int value, size_t num) {
    unsigned char* p = ptr;
    while(num-- > 0) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}