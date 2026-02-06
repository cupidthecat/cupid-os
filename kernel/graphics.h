#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "types.h"

/* Initialize graphics subsystem (call after vga_set_mode_13h) */
void gfx_init(void);

/* ── Pixel-level ──────────────────────────────────────────────────── */
void gfx_plot_pixel(int16_t x, int16_t y, uint8_t color);

/* ── Lines ────────────────────────────────────────────────────────── */
void gfx_draw_hline(int16_t x, int16_t y, uint16_t w, uint8_t color);
void gfx_draw_vline(int16_t x, int16_t y, uint16_t h, uint8_t color);
void gfx_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                   uint8_t color);

/* ── Rectangles ───────────────────────────────────────────────────── */
void gfx_draw_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                   uint8_t color);
void gfx_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                   uint8_t color);

/* ── Text ─────────────────────────────────────────────────────────── */
void gfx_draw_char(int16_t x, int16_t y, char c, uint8_t color);
void gfx_draw_text(int16_t x, int16_t y, const char *text, uint8_t color);
uint16_t gfx_text_width(const char *text);

#endif
