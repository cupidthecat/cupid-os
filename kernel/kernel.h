#ifndef KERNEL_H
#define KERNEL_H

#include "types.h"

// Expose cursor position variables
extern int cursor_x;
extern int cursor_y;

// VGA-related definitions
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000
#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5
#define VGA_OFFSET_LOW 0x0F
#define VGA_OFFSET_HIGH 0x0E

// Function declarations
void print(const char* str);
void putchar(char c);
void clear_screen(void);
void init_vga(void);
void print_int(uint32_t num);

#endif 