#ifndef KERNEL_H
#define KERNEL_H

#include "types.h"

// Screen output functions

// Add these function declarations at the top, after the #defines
void print(const char* str);
void putchar(char c);
void clear_screen(void);
void init_vga(void);
#endif 