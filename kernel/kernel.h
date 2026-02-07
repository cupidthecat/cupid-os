#ifndef KERNEL_H
#define KERNEL_H

#include "types.h"
#include "isr.h"

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

/* Check and perform deferred context switch at a safe voluntary point */
void kernel_check_reschedule(void);

/* Clear the deferred reschedule flag without switching */
void kernel_clear_reschedule(void);

void clear_screen(void);
void init_vga(void);
void print_int(uint32_t num);
uint64_t get_cpu_freq(void);
void print_hex(uint32_t n);
void fs_init(void);

// Timer callback functions
void timer_callback_channel0(struct registers* r, uint32_t channel);
void timer_callback_channel1(struct registers* r, uint32_t channel);
uint32_t timer_get_ticks_channel(uint32_t channel);

// Timer calibration functions
void calibrate_timer(void);
uint32_t get_pit_ticks_per_ms(void);

#endif 
