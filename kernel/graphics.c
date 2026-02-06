/**
 * graphics.c - Graphics primitives for cupid-os VGA Mode 13h
 *
 * Provides pixel, line, rectangle, and text drawing with automatic
 * clipping to screen bounds (320x200).
 */

#include "graphics.h"
#include "font_8x8.h"
#include "string.h"
#include "../drivers/vga.h"

/* ── Internal helpers ─────────────────────────────────────────────── */

static uint8_t *fb;  /* Pointer to current back buffer */

void gfx_init(void) {
    fb = vga_get_framebuffer();
}

/* ── Pixel ────────────────────────────────────────────────────────── */

void gfx_plot_pixel(int16_t x, int16_t y, uint8_t color) {
    if (x < 0 || x >= VGA_GFX_WIDTH || y < 0 || y >= VGA_GFX_HEIGHT)
        return;
    fb[y * VGA_GFX_WIDTH + x] = color;
}

/* ── Horizontal / vertical lines (fast) ───────────────────────────── */

void gfx_draw_hline(int16_t x, int16_t y, uint16_t w, uint8_t color) {
    if (y < 0 || y >= VGA_GFX_HEIGHT) return;
    int16_t x1 = x;
    int16_t x2 = (int16_t)(x + (int16_t)w - 1);
    if (x1 < 0) x1 = 0;
    if (x2 >= VGA_GFX_WIDTH) x2 = VGA_GFX_WIDTH - 1;
    if (x1 > x2) return;
    memset(&fb[y * VGA_GFX_WIDTH + x1], (int)color, (size_t)(x2 - x1 + 1));
}

void gfx_draw_vline(int16_t x, int16_t y, uint16_t h, uint8_t color) {
    if (x < 0 || x >= VGA_GFX_WIDTH) return;
    int16_t y1 = y;
    int16_t y2 = (int16_t)(y + (int16_t)h - 1);
    if (y1 < 0) y1 = 0;
    if (y2 >= VGA_GFX_HEIGHT) y2 = VGA_GFX_HEIGHT - 1;
    for (int16_t row = y1; row <= y2; row++) {
        fb[row * VGA_GFX_WIDTH + x] = color;
    }
}

/* ── General line (Bresenham) ─────────────────────────────────────── */

void gfx_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                   uint8_t color) {
    int16_t dx = x2 - x1;
    int16_t dy = y2 - y1;
    int16_t sx = (dx >= 0) ? 1 : -1;
    int16_t sy = (dy >= 0) ? 1 : -1;
    if (dx < 0) dx = (int16_t)-dx;
    if (dy < 0) dy = (int16_t)-dy;

    int16_t err = dx - dy;

    for (;;) {
        gfx_plot_pixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int16_t e2 = (int16_t)(err * 2);
        if (e2 > -dy) { err = (int16_t)(err - dy); x1 = (int16_t)(x1 + sx); }
        if (e2 <  dx) { err = (int16_t)(err + dx); y1 = (int16_t)(y1 + sy); }
    }
}

/* ── Rectangles ───────────────────────────────────────────────────── */

void gfx_draw_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                   uint8_t color) {
    gfx_draw_hline(x, y, w, color);
    gfx_draw_hline(x, (int16_t)(y + (int16_t)h - 1), w, color);
    gfx_draw_vline(x, y, h, color);
    gfx_draw_vline((int16_t)(x + (int16_t)w - 1), y, h, color);
}

void gfx_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                   uint8_t color) {
    for (uint16_t row = 0; row < h; row++) {
        gfx_draw_hline(x, (int16_t)(y + (int16_t)row), w, color);
    }
}

/* ── Text ─────────────────────────────────────────────────────────── */

void gfx_draw_char(int16_t x, int16_t y, char c, uint8_t color) {
    uint8_t idx = (uint8_t)c;
    if (idx >= 128) idx = 0;
    const uint8_t *glyph = font_8x8[idx];

    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (0x80U >> (unsigned)col)) {
                gfx_plot_pixel((int16_t)(x + (int16_t)col),
                               (int16_t)(y + (int16_t)row), color);
            }
        }
    }
}

void gfx_draw_text(int16_t x, int16_t y, const char *text, uint8_t color) {
    int16_t cx = x;
    while (*text) {
        gfx_draw_char(cx, y, *text, color);
        cx = (int16_t)(cx + FONT_W);
        text++;
    }
}

uint16_t gfx_text_width(const char *text) {
    uint16_t len = 0;
    while (*text) { len++; text++; }
    return (uint16_t)(len * (uint16_t)FONT_W);
}
