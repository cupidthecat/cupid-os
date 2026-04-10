/**
 * graphics.c - Graphics primitives for cupid-os VBE 640x480 32bpp
 *
 * Provides pixel, line, rectangle, and text drawing with automatic
 * clipping to screen bounds (640x480).
 */

#include "graphics.h"
#include "font_8x8.h"
#include "gfx2d.h"
#include "simd.h"
#include "string.h"
#include "../drivers/vga.h"

static uint32_t *fb;  /* Pointer to current back buffer */

static uint32_t *gfx_current_fb(void) {
    uint32_t *active = gfx2d_get_active_fb();
    return active ? active : fb;
}

static int gfx_current_w(void) {
    uint32_t *active = gfx2d_get_active_fb();
    return active ? gfx2d_width() : VGA_GFX_WIDTH;
}

static int gfx_current_h(void) {
    uint32_t *active = gfx2d_get_active_fb();
    return active ? gfx2d_height() : VGA_GFX_HEIGHT;
}

static void gfx_clip_bounds(int *x1, int *y1, int *x2, int *y2) {
    int width = gfx_current_w();
    int height = gfx_current_h();

    *x1 = 0;
    *y1 = 0;
    *x2 = width - 1;
    *y2 = height - 1;

    if (gfx2d_clip_is_active()) {
        int clip_x, clip_y, clip_w, clip_h;
        gfx2d_clip_get(&clip_x, &clip_y, &clip_w, &clip_h);
        if (clip_x > *x1) *x1 = clip_x;
        if (clip_y > *y1) *y1 = clip_y;
        if (clip_x + clip_w - 1 < *x2) *x2 = clip_x + clip_w - 1;
        if (clip_y + clip_h - 1 < *y2) *y2 = clip_y + clip_h - 1;
    }
}

void gfx_init(void) {
    fb = vga_get_framebuffer();
}

void gfx_set_framebuffer(uint32_t *new_fb) {
    fb = new_fb;
}

void gfx_plot_pixel(int16_t x, int16_t y, uint32_t color) {
    uint32_t *dst = gfx_current_fb();
    int width = gfx_current_w();
    int clip_x1, clip_y1, clip_x2, clip_y2;
    gfx_clip_bounds(&clip_x1, &clip_y1, &clip_x2, &clip_y2);
    if (x < clip_x1 || x > clip_x2 || y < clip_y1 || y > clip_y2)
        return;
    dst[(int32_t)y * width + (int32_t)x] = color;
}

void gfx_draw_hline(int16_t x, int16_t y, uint16_t w, uint32_t color) {
    uint32_t *dst = gfx_current_fb();
    int width = gfx_current_w();
    int clip_x1, clip_y1, clip_x2, clip_y2;
    gfx_clip_bounds(&clip_x1, &clip_y1, &clip_x2, &clip_y2);
    if (y < clip_y1 || y > clip_y2) return;
    int16_t x1 = x;
    int16_t x2 = (int16_t)(x + (int16_t)w - 1);
    if (x1 < clip_x1) x1 = (int16_t)clip_x1;
    if (x2 > clip_x2) x2 = (int16_t)clip_x2;
    if (x1 > x2) return;
    uint32_t *row = &dst[(int32_t)y * width + (int32_t)x1];
    simd_memset32(row, color, (uint32_t)(x2 - x1 + 1));
}

void gfx_draw_vline(int16_t x, int16_t y, uint16_t h, uint32_t color) {
    uint32_t *dst = gfx_current_fb();
    int width = gfx_current_w();
    int clip_x1, clip_y1, clip_x2, clip_y2;
    gfx_clip_bounds(&clip_x1, &clip_y1, &clip_x2, &clip_y2);
    if (x < clip_x1 || x > clip_x2) return;
    int16_t y1 = y;
    int16_t y2 = (int16_t)(y + (int16_t)h - 1);
    if (y1 < clip_y1) y1 = (int16_t)clip_y1;
    if (y2 > clip_y2) y2 = (int16_t)clip_y2;
    if (y1 > y2) return;
    for (int16_t row = y1; row <= y2; row++) {
        dst[(int32_t)row * width + (int32_t)x] = color;
    }
}

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

void gfx_draw_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                   uint32_t color) {
    gfx_draw_hline(x, y, w, color);
    gfx_draw_hline(x, (int16_t)(y + (int16_t)h - 1), w, color);
    gfx_draw_vline(x, y, h, color);
    gfx_draw_vline((int16_t)(x + (int16_t)w - 1), y, h, color);
}

void gfx_fill_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                   uint32_t color) {
    uint32_t *dst = gfx_current_fb();
    int width = gfx_current_w();
    int clip_x1, clip_y1, clip_x2, clip_y2;
    gfx_clip_bounds(&clip_x1, &clip_y1, &clip_x2, &clip_y2);
    /* Clip once for the whole rect instead of per-row in gfx_draw_hline */
    int x1 = (int)x, y1 = (int)y;
    int x2 = x1 + (int)w - 1;
    int y2 = y1 + (int)h - 1;
    if (x1 < clip_x1) x1 = clip_x1;
    if (y1 < clip_y1) y1 = clip_y1;
    if (x2 > clip_x2) x2 = clip_x2;
    if (y2 > clip_y2) y2 = clip_y2;
    if (x1 > x2 || y1 > y2) return;
    simd_fill_rect(dst, (uint32_t)width,
                   x1, y1, x2 - x1 + 1, y2 - y1 + 1, color);
}

void gfx_draw_char(int16_t x, int16_t y, char c, uint32_t color) {
    uint32_t *dst = gfx_current_fb();
    int width = gfx_current_w();
    int height = gfx_current_h();
    int clip_x1, clip_y1, clip_x2, clip_y2;
    uint8_t idx = (uint8_t)c;
    if (idx >= 128U) idx = 0U;
    const uint8_t *glyph = font_8x8[idx];
    gfx_clip_bounds(&clip_x1, &clip_y1, &clip_x2, &clip_y2);

    /* Fast path: character entirely within current clip bounds. */
    if ((int)x >= clip_x1 && (int)x + FONT_W - 1 <= clip_x2 &&
        (int)y >= clip_y1 && (int)y + FONT_H - 1 <= clip_y2) {
        for (int row = 0; row < FONT_H; row++) {
            uint8_t bits = glyph[row];
            if (!bits) continue;
            uint32_t *rp = dst + (uint32_t)((int)y + row) * (uint32_t)width + (uint32_t)x;
            for (int col = 0; col < FONT_W; col++) {
                if (bits & (0x80U >> (unsigned)col)) rp[col] = color;
            }
        }
        return;
    }
    /* Slow path: clip per pixel for edge characters */
    for (int row = 0; row < FONT_H; row++) {
        int py = (int)y + row;
        if (py < clip_y1 || py > clip_y2 || py < 0 || py >= height) continue;
        uint8_t bits = glyph[row];
        if (!bits) continue;
        uint32_t *rp = dst + (uint32_t)py * (uint32_t)width;
        for (int col = 0; col < FONT_W; col++) {
            if (bits & (0x80U >> (unsigned)col)) {
                int px = (int)x + col;
                if (px >= clip_x1 && px <= clip_x2 && px >= 0 && px < width) rp[px] = color;
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

void gfx_draw_3d_rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                      bool raised) {
    uint32_t light = raised ? COLOR_TEXT_LIGHT : COLOR_TEXT;
    uint32_t dark  = raised ? COLOR_TEXT : COLOR_TEXT_LIGHT;
    gfx_draw_hline(x, y, w, light);
    gfx_draw_vline(x, y, h, light);
    gfx_draw_hline(x, (int16_t)(y + (int16_t)h - 1), w, dark);
    gfx_draw_vline((int16_t)(x + (int16_t)w - 1), y, h, dark);
}
