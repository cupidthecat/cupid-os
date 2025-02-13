#ifndef VGA_H
#define VGA_H

#include "../kernel/types.h"

#define VGA_BLACK         0x0
#define VGA_BLUE          0x1
#define VGA_GREEN         0x2
#define VGA_CYAN          0x3
#define VGA_RED           0x4
#define VGA_MAGENTA       0x5
#define VGA_BROWN         0x6
#define VGA_LIGHT_GREY    0x7
#define VGA_DARK_GREY     0x8
#define VGA_LIGHT_BLUE    0x9
#define VGA_LIGHT_GREEN   0xA
#define VGA_LIGHT_CYAN    0xB
#define VGA_LIGHT_RED     0xC
#define VGA_LIGHT_MAGENTA 0xD
#define VGA_YELLOW        0xE
#define VGA_WHITE         0xF

#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5
#define VGA_OFFSET_LOW 0x0F
#define VGA_OFFSET_HIGH 0x0E

// VGA-related definitions
#define VGA_WIDTH 320
#define VGA_HEIGHT 200
#define VGA_MEMORY 0xA0000

#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define NUM_GLYPHS 256
extern uint8_t* font_data;
extern uint8_t vga_fg_color;
extern uint8_t vga_bg_color;

void putchar(char c);
void clear_screen(void);
void init_vga(void);
void putpixel(int x, int y, uint8_t color);
void draw_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t color);
uint8_t getpixel(int x, int y);
void load_font(uint8_t* font);

#endif 