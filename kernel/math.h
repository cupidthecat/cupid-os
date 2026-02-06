#ifndef MATH_H
#define MATH_H

#include "types.h"

// Function declarations
uint64_t udiv64(uint64_t dividend, uint32_t divisor);
char* itoa(int value, char* str);
void print_hex(uint32_t n);
void print_hex_word(uint16_t num);
void print_hex_byte(uint8_t num);
uint64_t __udivdi3(uint64_t dividend, uint64_t divisor);

#endif
