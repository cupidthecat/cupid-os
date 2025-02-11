#ifndef KERNEL_H
#define KERNEL_H

#include "types.h"

// Expose cursor position variables
extern int cursor_x;
extern int cursor_y;

// Function declarations
void print(const char* str);
void print_int(uint32_t num);
uint64_t get_cpu_freq(void);
void print_hex(uint32_t n);
void vga_set_color(uint8_t fg, uint8_t bg);
void boot_log(const char* msg, uint8_t fg_color);
void boot_log_status(const char* status, uint8_t fg_color);

// Boot status macros
#define BOOT_START(msg) print(msg); boot_status_column = cursor_x + 12
#define BOOT_OK() boot_log_status("[  OK  ]", VGA_GREEN)
#define BOOT_FAIL() boot_log_status("[ FAIL ]", VGA_RED)
#define BOOT_INFO(msg) boot_log(msg, VGA_LIGHT_GREY)

#endif 