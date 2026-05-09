#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "types.h"

/* Initialize graphics subsystem (call after vga_init_vbe) */
void gfx_init(void);

/* Update cached framebuffer pointer (call after vga_flip) */
void gfx_set_framebuffer(uint32_t *fb);

void gfx_plot_pixel(int16_t x, int16_t y, uint32_t color);

void gfx_draw_hline(int16_t x, int16_t y, uint16_t w, uint32_t color);
void gfx_draw_vline(int16_t x, int16_t y, uint16_t h, uint32_t color);
void gfx_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                   uint32_t color);

void gfx_draw_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                   uint32_t color);
void gfx_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                   uint32_t color);

void gfx_draw_char(int16_t x, int16_t y, char c, uint32_t color);
void gfx_draw_text(int16_t x, int16_t y, const char *text, uint32_t color);
uint16_t gfx_text_width(const char *text);

void gfx_draw_char_scaled(int16_t x, int16_t y, char c, uint32_t color,
                          int scale);
void gfx_draw_text_scaled(int16_t x, int16_t y, const char *text,
                          uint32_t color, int scale);

void gfx_draw_3d_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                      bool raised);

#endif
