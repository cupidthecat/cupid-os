/**
 * graphics.c - Graphics primitives for cupid-os VBE 640x480 32bpp
 *
 * Provides pixel, line, rectangle, and text drawing with automatic
 * clipping to screen bounds (640x480).
 */

#include "graphics.h"
#include "font_8x8.h"
#include "string.h"
#include "../drivers/vga.h"

/* ── Internal helpers ─────────────────────────────────────────────── */

static uint32_t *fb;  /* Pointer to current back buffer */

void gfx_init(void) {
    fb = vga_get_framebuffer();
}

void gfx_set_framebuffer(uint32_t *new_fb) {
    fb = new_fb;
}

/* ── Pixel ────────────────────────────────────────────────────────── */

void gfx_plot_pixel(int16_t x, int16_t y, uint32_t color) {
    if (x < 0 || x >= VGA_GFX_WIDTH || y < 0 || y >= VGA_GFX_HEIGHT)
        return;
    fb[(int32_t)y * VGA_GFX_WIDTH + (int32_t)x] = color;
}

/* ── Horizontal / vertical lines (fast) ───────────────────────────── */

void gfx_draw_hline(int16_t x, int16_t y, uint16_t w, uint32_t color) {
    if (y < 0 || y >= VGA_GFX_HEIGHT) return;
    int16_t x1 = x;
    int16_t x2 = (int16_t)(x + (int16_t)w - 1);
    if (x1 < 0) x1 = 0;
    if (x2 >= VGA_GFX_WIDTH) x2 = VGA_GFX_WIDTH - 1;
    if (x1 > x2) return;
    uint32_t *row = &fb[(int32_t)y * VGA_GFX_WIDTH + (int32_t)x1];
    int16_t count = (int16_t)(x2 - x1 + 1);
    int16_t i;
    for (i = 0; i < count; i++) {
        row[i] = color;
    }
}

void gfx_draw_vline(int16_t x, int16_t y, uint16_t h, uint32_t color) {
    if (x < 0 || x >= VGA_GFX_WIDTH) return;
    int16_t y1 = y;
    int16_t y2 = (int16_t)(y + (int16_t)h - 1);
    if (y1 < 0) y1 = 0;
    if (y2 >= VGA_GFX_HEIGHT) y2 = VGA_GFX_HEIGHT - 1;
    for (int16_t row = y1; row <= y2; row++) {
        fb[(int32_t)row * VGA_GFX_WIDTH + (int32_t)x] = color;
    }
}

/* ── General line (Bresenham) ─────────────────────────────────────── */

void gfx_draw_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                   uint32_t color) {
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
                   uint32_t color) {
    gfx_draw_hline(x, y, w, color);
    gfx_draw_hline(x, (int16_t)(y + (int16_t)h - 1), w, color);
    gfx_draw_vline(x, y, h, color);
    gfx_draw_vline((int16_t)(x + (int16_t)w - 1), y, h, color);
}

void gfx_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                   uint32_t color) {
    /* Clip once for the whole rect instead of per-row in gfx_draw_hline */
    int x1 = (int)x, y1 = (int)y;
    int x2 = x1 + (int)w - 1;
    int y2 = y1 + (int)h - 1;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= VGA_GFX_WIDTH)  x2 = VGA_GFX_WIDTH - 1;
    if (y2 >= VGA_GFX_HEIGHT) y2 = VGA_GFX_HEIGHT - 1;
    if (x1 > x2 || y1 > y2) return;
    int count = x2 - x1 + 1;
    for (int row = y1; row <= y2; row++) {
        uint32_t *dst = fb + (uint32_t)row * VGA_GFX_WIDTH + (uint32_t)x1;
        for (int i = 0; i < count; i++) dst[i] = color;
    }
}

/* ── Text ─────────────────────────────────────────────────────────── */

void gfx_draw_char(int16_t x, int16_t y, char c, uint32_t color) {
    uint8_t idx = (uint8_t)c;
    if (idx >= 128U) idx = 0U;
    const uint8_t *glyph = font_8x8[idx];

    /* Fast path: character entirely within screen — no per-pixel clip needed */
    if ((int)x >= 0 && (int)x + FONT_W <= VGA_GFX_WIDTH &&
        (int)y >= 0 && (int)y + FONT_H <= VGA_GFX_HEIGHT) {
        for (int row = 0; row < FONT_H; row++) {
            uint8_t bits = glyph[row];
            if (!bits) continue;
            uint32_t *rp = fb + (uint32_t)((int)y + row) * VGA_GFX_WIDTH + (uint32_t)x;
            for (int col = 0; col < FONT_W; col++) {
                if (bits & (0x80U >> (unsigned)col)) rp[col] = color;
            }
        }
        return;
    }
    /* Slow path: clip per pixel for edge characters */
    for (int row = 0; row < FONT_H; row++) {
        int py = (int)y + row;
        if (py < 0 || py >= VGA_GFX_HEIGHT) continue;
        uint8_t bits = glyph[row];
        if (!bits) continue;
        uint32_t *rp = fb + (uint32_t)py * VGA_GFX_WIDTH;
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (0x80U >> (unsigned)col)) {
                int px = (int)x + col;
                if (px >= 0 && px < VGA_GFX_WIDTH) rp[px] = color;
            }
        }
    }
}

void gfx_draw_text(int16_t x, int16_t y, const char *text, uint32_t color) {
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

/* ── Scaled character drawing ─────────────────────────────────────── */

void gfx_draw_char_scaled(int16_t x, int16_t y, char c, uint32_t color,
                          int scale) {
    if (scale <= 1) { gfx_draw_char(x, y, c, color); return; }

    uint8_t idx = (uint8_t)c;
    if (idx >= 128U) idx = 0U;
    const uint8_t *glyph = font_8x8[idx];

    for (int row = 0; row < FONT_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (0x80U >> (unsigned)col)) {
                gfx_fill_rect((int16_t)(x + (int16_t)(col * scale)),
                              (int16_t)(y + (int16_t)(row * scale)),
                              (uint16_t)scale, (uint16_t)scale, color);
            }
        }
    }
}

void gfx_draw_text_scaled(int16_t x, int16_t y, const char *text,
                          uint32_t color, int scale) {
    int16_t cx = x;
    int cw = (scale <= 1) ? FONT_W : FONT_W * scale;
    while (*text) {
        gfx_draw_char_scaled(cx, y, *text, color, scale);
        cx = (int16_t)(cx + (int16_t)cw);
        text++;
    }
}

/* ── 3D raised/sunken rectangle (Windows 95 style) ────────────────── */

void gfx_draw_3d_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                      bool raised) {
    uint32_t light = raised ? COLOR_TEXT_LIGHT : COLOR_TEXT;
    uint32_t dark  = raised ? COLOR_TEXT : COLOR_TEXT_LIGHT;
    gfx_draw_hline(x, y, w, light);
    gfx_draw_vline(x, y, h, light);
    gfx_draw_hline(x, (int16_t)(y + (int16_t)h - 1), w, dark);
    gfx_draw_vline((int16_t)(x + (int16_t)w - 1), y, h, dark);
}
