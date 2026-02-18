/**
 * gfx2d_effects.c - Filters & Post-Processing for cupid-os
 *
 * All filters operate on the current framebuffer (screen or active surface).
 * They read pixels with gfx2d_getpixel and write with gfx2d_pixel, so all
 * operations respect the current clipping rect.
 */

#include "gfx2d_effects.h"
#include "gfx2d.h"
#include "memory.h"
#include "simd.h"
#include "string.h"
#include "../drivers/serial.h"

static int clamp255(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return v;
}

/* Simple pseudo-RNG (xorshift32) */
static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/* Extract RGB */
#define R(c) (int)(((c) >> 16) & 0xFF)
#define G(c) (int)(((c) >> 8) & 0xFF)
#define B(c) (int)((c) & 0xFF)
#define RGB(r, g, b) ((uint32_t)(clamp255(r) << 16) | \
                      (uint32_t)(clamp255(g) << 8)  | \
                      (uint32_t)clamp255(b))

void gfx2d_effects_init(void) {
    /* Nothing to initialize currently */
}

/* Blur */

void gfx2d_blur_box(int x, int y, int w, int h, int radius) {
    uint32_t *tmp;
    uint32_t *tmp2;
    uint32_t tmp_size;
    uint32_t *fbuf;
    int fbuf_w;
    int fbuf_h;
    int row;
    int x0;
    int y0;
    int x1;
    int y1;

    if (radius < 1) radius = 1;
    if (radius > 8) radius = 8;
    if (w <= 0 || h <= 0) return;

    fbuf = gfx2d_get_active_fb();
    fbuf_w = gfx2d_width();
    fbuf_h = gfx2d_height();

    x0 = x;
    y0 = y;
    x1 = x + w;
    y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > fbuf_w) x1 = fbuf_w;
    if (y1 > fbuf_h) y1 = fbuf_h;
    if (x1 <= x0 || y1 <= y0) return;

    w = x1 - x0;
    h = y1 - y0;

    tmp_size = (uint32_t)w * (uint32_t)h * 4u;
    tmp = (uint32_t *)kmalloc(tmp_size);
    if (!tmp) return;

    tmp2 = (uint32_t *)kmalloc(tmp_size);
    if (!tmp2) {
        kfree(tmp);
        return;
    }

    for (row = 0; row < h; row++) {
        simd_memcpy(tmp + (uint32_t)row * (uint32_t)w,
                    fbuf + (uint32_t)(y0 + row) * (uint32_t)fbuf_w + (uint32_t)x0,
                    (uint32_t)w * 4u);
    }

    simd_blur_h_pass(tmp2, tmp, w, h, radius);
    simd_blur_v_pass(tmp, tmp2, w, h, radius);

    for (row = 0; row < h; row++) {
        simd_memcpy(fbuf + (uint32_t)(y0 + row) * (uint32_t)fbuf_w + (uint32_t)x0,
                    tmp + (uint32_t)row * (uint32_t)w,
                    (uint32_t)w * 4u);
    }

    kfree(tmp2);
    kfree(tmp);
}

void gfx2d_blur_box_surface(int surf_handle, int radius) {
    /* Temporarily set surface active, blur, restore */
    gfx2d_surface_set_active(surf_handle);
    gfx2d_blur_box(0, 0, gfx2d_width(), gfx2d_height(), radius);
    gfx2d_surface_unset_active();
}

void gfx2d_blur_gaussian(int x, int y, int w, int h, int radius) {
    /* 3-pass box blur approximates gaussian */
    gfx2d_blur_box(x, y, w, h, radius);
    gfx2d_blur_box(x, y, w, h, radius);
    gfx2d_blur_box(x, y, w, h, radius);
}

void gfx2d_blur_motion(int x, int y, int w, int h,
                       int angle, int distance) {
    int row, col, i;
    uint32_t *tmp;
    uint32_t tmp_size;
    int dx_step, dy_step;

    if (distance < 1) distance = 1;
    if (distance > 16) distance = 16;
    if (w <= 0 || h <= 0) return;

    /* Direction vector (simplified: 8 primary directions) */
    angle = ((angle % 360) + 360) % 360;
    if (angle < 23 || angle >= 338) { dx_step = 1; dy_step = 0; }
    else if (angle < 68)           { dx_step = 1; dy_step = 1; }
    else if (angle < 113)          { dx_step = 0; dy_step = 1; }
    else if (angle < 158)          { dx_step = -1; dy_step = 1; }
    else if (angle < 203)          { dx_step = -1; dy_step = 0; }
    else if (angle < 248)          { dx_step = -1; dy_step = -1; }
    else if (angle < 293)          { dx_step = 0; dy_step = -1; }
    else                           { dx_step = 1; dy_step = -1; }

    tmp_size = (uint32_t)w * (uint32_t)h * 4u;
    tmp = (uint32_t *)kmalloc(tmp_size);
    if (!tmp) return;

    /* Read source */
    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++)
            tmp[(uint32_t)row * (uint32_t)w + (uint32_t)col] =
                gfx2d_getpixel(x + col, y + row);

    /* Average along motion direction */
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            int sr = 0, sg = 0, sb = 0, cnt = 0;
            for (i = -distance; i <= distance; i++) {
                int sc = col + i * dx_step;
                int sr2 = row + i * dy_step;
                if (sc < 0 || sc >= w || sr2 < 0 || sr2 >= h) continue;
                uint32_t px = tmp[(uint32_t)sr2 * (uint32_t)w + (uint32_t)sc];
                sr += R(px);
                sg += G(px);
                sb += B(px);
                cnt++;
            }
            if (cnt > 0) {
                sr /= cnt; sg /= cnt; sb /= cnt;
            }
            gfx2d_pixel(x + col, y + row, RGB(sr, sg, sb));
        }
    }

    kfree(tmp);
}

/* Color Manipulation */

void gfx2d_brightness(int x, int y, int w, int h, int amount) {
    int row, col;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            uint32_t px = gfx2d_getpixel(x + col, y + row);
            int r = R(px) + amount;
            int g = G(px) + amount;
            int b = B(px) + amount;
            gfx2d_pixel(x + col, y + row, RGB(r, g, b));
        }
    }
}

void gfx2d_contrast(int x, int y, int w, int h, int amount) {
    int row, col;
    /* Contrast factor: (259 * (amount + 255)) / (255 * (259 - amount)) */
    /* Scaled to 256 for integer math */
    int factor;
    int denom = 255 * (259 - amount);
    if (denom == 0) denom = 1;
    factor = (259 * (amount + 255) * 256) / denom;

    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            uint32_t px = gfx2d_getpixel(x + col, y + row);
            int r = ((R(px) - 128) * factor) / 256 + 128;
            int g = ((G(px) - 128) * factor) / 256 + 128;
            int b = ((B(px) - 128) * factor) / 256 + 128;
            gfx2d_pixel(x + col, y + row, RGB(r, g, b));
        }
    }
}

void gfx2d_saturation(int x, int y, int w, int h, int amount) {
    int row, col;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            uint32_t px = gfx2d_getpixel(x + col, y + row);
            int r = R(px), g = G(px), b = B(px);
            /* Luminance (approximate) */
            int lum = (r * 77 + g * 150 + b * 29) >> 8;
            /* Interpolate between gray and original */
            r = lum + ((r - lum) * amount) / 256;
            g = lum + ((g - lum) * amount) / 256;
            b = lum + ((b - lum) * amount) / 256;
            gfx2d_pixel(x + col, y + row, RGB(r, g, b));
        }
    }
}

void gfx2d_hue_shift(int x, int y, int w, int h, int degrees) {
    int row, col;
    /* Simplified hue rotation using matrix approximation:
     *   R' = R*cos + (1-cos)/3 + sqrt(1/3)*sin * (G-B-related)
     * We use a lookup and fixed-point. For simplicity we do channel rotation
     * at 120-degree intervals and blend between. */

    /* Normalize */
    degrees = ((degrees % 360) + 360) % 360;
    if (degrees == 0) return;

    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            uint32_t px = gfx2d_getpixel(x + col, y + row);
            int r = R(px), g = G(px), b = B(px);
            int nr, ng, nb;

            if (degrees < 120) {
                /* Blend R->G, G->B, B->R */
                int t = degrees * 256 / 120;
                nr = (r * (256 - t) + b * t) >> 8;
                ng = (g * (256 - t) + r * t) >> 8;
                nb = (b * (256 - t) + g * t) >> 8;
            } else if (degrees < 240) {
                int t = (degrees - 120) * 256 / 120;
                nr = (b * (256 - t) + g * t) >> 8;
                ng = (r * (256 - t) + b * t) >> 8;
                nb = (g * (256 - t) + r * t) >> 8;
            } else {
                int t = (degrees - 240) * 256 / 120;
                nr = (g * (256 - t) + r * t) >> 8;
                ng = (b * (256 - t) + g * t) >> 8;
                nb = (r * (256 - t) + b * t) >> 8;
            }

            gfx2d_pixel(x + col, y + row, RGB(nr, ng, nb));
        }
    }
}

void gfx2d_tint_ex(int x, int y, int w, int h, uint32_t color,
                   int alpha, int mode) {
    int row, col;
    int tr = R(color), tg = G(color), tb = B(color);

    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;

    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            uint32_t px = gfx2d_getpixel(x + col, y + row);
            int r = R(px), g = G(px), b = B(px);
            int nr, ng, nb;

            switch (mode) {
            case GFX2D_TINT_MULTIPLY:
                nr = (r * tr) >> 8;
                ng = (g * tg) >> 8;
                nb = (b * tb) >> 8;
                break;
            case GFX2D_TINT_SCREEN:
                nr = 255 - (((255 - r) * (255 - tr)) >> 8);
                ng = 255 - (((255 - g) * (255 - tg)) >> 8);
                nb = 255 - (((255 - b) * (255 - tb)) >> 8);
                break;
            case GFX2D_TINT_OVERLAY:
                nr = r < 128 ? (2 * r * tr) >> 8
                             : 255 - ((2 * (255 - r) * (255 - tr)) >> 8);
                ng = g < 128 ? (2 * g * tg) >> 8
                             : 255 - ((2 * (255 - g) * (255 - tg)) >> 8);
                nb = b < 128 ? (2 * b * tb) >> 8
                             : 255 - ((2 * (255 - b) * (255 - tb)) >> 8);
                break;
            default:
                nr = tr; ng = tg; nb = tb;
                break;
            }

            /* Blend with alpha */
            int ia = 255 - alpha;
            nr = (nr * alpha + r * ia) / 255;
            ng = (ng * alpha + g * ia) / 255;
            nb = (nb * alpha + b * ia) / 255;

            gfx2d_pixel(x + col, y + row, RGB(nr, ng, nb));
        }
    }
}

/* Edge Detection & Stylization */

void gfx2d_edges(int x, int y, int w, int h, uint32_t edge_color) {
    int row, col;
    uint32_t *tmp;
    uint32_t tmp_size;

    if (w <= 2 || h <= 2) return;

    tmp_size = (uint32_t)w * (uint32_t)h * 4u;
    tmp = (uint32_t *)kmalloc(tmp_size);
    if (!tmp) return;

    /* Read source */
    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++)
            tmp[(uint32_t)row * (uint32_t)w + (uint32_t)col] =
                gfx2d_getpixel(x + col, y + row);

    /* Sobel operator */
    for (row = 1; row < h - 1; row++) {
        for (col = 1; col < w - 1; col++) {
            /* Convert 3x3 neighborhood to grayscale */
            int gray[9];
            int idx = 0;
            int dy, dx;
            int gx, gy, mag;
            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    uint32_t px = tmp[(uint32_t)(row + dy) * (uint32_t)w +
                                      (uint32_t)(col + dx)];
                    gray[idx++] = (R(px) * 77 + G(px) * 150 + B(px) * 29) >> 8;
                }
            }

            /* Sobel Gx = [-1 0 1; -2 0 2; -1 0 1] */
            gx = -gray[0] + gray[2] - 2*gray[3] + 2*gray[5] - gray[6] + gray[8];
            /* Sobel Gy = [-1 -2 -1; 0 0 0; 1 2 1] */
            gy = -gray[0] - 2*gray[1] - gray[2] + gray[6] + 2*gray[7] + gray[8];

            /* Magnitude (approximate) */
            if (gx < 0) gx = -gx;
            if (gy < 0) gy = -gy;
            mag = gx + gy;

            if (mag > 128)
                gfx2d_pixel(x + col, y + row, edge_color);
            else
                gfx2d_pixel(x + col, y + row, 0x00000000);
        }
    }

    kfree(tmp);
}

void gfx2d_emboss(int x, int y, int w, int h, int angle) {
    /* Emboss kernel based on angle */
    int kernel[9];
    int row, col;
    uint32_t *tmp;
    uint32_t tmp_size;

    (void)angle;  /* Simplified: use standard emboss kernel */

    /* Standard emboss kernel: [-2 -1 0; -1 1 1; 0 1 2] */
    kernel[0] = -2; kernel[1] = -1; kernel[2] = 0;
    kernel[3] = -1; kernel[4] = 1;  kernel[5] = 1;
    kernel[6] = 0;  kernel[7] = 1;  kernel[8] = 2;

    if (w <= 2 || h <= 2) return;

    tmp_size = (uint32_t)w * (uint32_t)h * 4u;
    tmp = (uint32_t *)kmalloc(tmp_size);
    if (!tmp) return;

    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++)
            tmp[(uint32_t)row * (uint32_t)w + (uint32_t)col] =
                gfx2d_getpixel(x + col, y + row);

    for (row = 1; row < h - 1; row++) {
        for (col = 1; col < w - 1; col++) {
            int sr = 0, sg = 0, sb = 0;
            int ki = 0;
            int dy, dx;
            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    uint32_t px = tmp[(uint32_t)(row + dy) * (uint32_t)w +
                                      (uint32_t)(col + dx)];
                    sr += R(px) * kernel[ki];
                    sg += G(px) * kernel[ki];
                    sb += B(px) * kernel[ki];
                    ki++;
                }
            }
            /* Offset to center around 128 */
            sr = sr + 128;
            sg = sg + 128;
            sb = sb + 128;
            gfx2d_pixel(x + col, y + row, RGB(sr, sg, sb));
        }
    }

    kfree(tmp);
}

void gfx2d_posterize(int x, int y, int w, int h, int levels) {
    int row, col;
    int step;

    if (levels < 2) levels = 2;
    if (levels > 256) levels = 256;
    step = 256 / levels;

    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            uint32_t px = gfx2d_getpixel(x + col, y + row);
            int r = (R(px) / step) * step;
            int g = (G(px) / step) * step;
            int b = (B(px) / step) * step;
            gfx2d_pixel(x + col, y + row, RGB(r, g, b));
        }
    }
}

/* Convolution Kernel System */

void gfx2d_convolve_3x3(int x, int y, int w, int h,
                         int kernel[9], int divisor) {
    int row, col;
    uint32_t *tmp;
    uint32_t tmp_size;

    if (w <= 2 || h <= 2) return;
    if (divisor == 0) divisor = 1;

    tmp_size = (uint32_t)w * (uint32_t)h * 4u;
    tmp = (uint32_t *)kmalloc(tmp_size);
    if (!tmp) return;

    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++)
            tmp[(uint32_t)row * (uint32_t)w + (uint32_t)col] =
                gfx2d_getpixel(x + col, y + row);

    for (row = 1; row < h - 1; row++) {
        for (col = 1; col < w - 1; col++) {
            int sr = 0, sg = 0, sb = 0;
            int ki = 0;
            int dy, dx;
            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    uint32_t px = tmp[(uint32_t)(row + dy) * (uint32_t)w +
                                      (uint32_t)(col + dx)];
                    sr += R(px) * kernel[ki];
                    sg += G(px) * kernel[ki];
                    sb += B(px) * kernel[ki];
                    ki++;
                }
            }
            sr /= divisor;
            sg /= divisor;
            sb /= divisor;
            gfx2d_pixel(x + col, y + row, RGB(sr, sg, sb));
        }
    }

    kfree(tmp);
}

void gfx2d_convolve_5x5(int x, int y, int w, int h,
                         int kernel[25], int divisor) {
    int row, col;
    uint32_t *tmp;
    uint32_t tmp_size;

    if (w <= 4 || h <= 4) return;
    if (divisor == 0) divisor = 1;

    tmp_size = (uint32_t)w * (uint32_t)h * 4u;
    tmp = (uint32_t *)kmalloc(tmp_size);
    if (!tmp) return;

    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++)
            tmp[(uint32_t)row * (uint32_t)w + (uint32_t)col] =
                gfx2d_getpixel(x + col, y + row);

    for (row = 2; row < h - 2; row++) {
        for (col = 2; col < w - 2; col++) {
            int sr = 0, sg = 0, sb = 0;
            int ki = 0;
            int dy, dx;
            for (dy = -2; dy <= 2; dy++) {
                for (dx = -2; dx <= 2; dx++) {
                    uint32_t px = tmp[(uint32_t)(row + dy) * (uint32_t)w +
                                      (uint32_t)(col + dx)];
                    sr += R(px) * kernel[ki];
                    sg += G(px) * kernel[ki];
                    sb += B(px) * kernel[ki];
                    ki++;
                }
            }
            sr /= divisor;
            sg /= divisor;
            sb /= divisor;
            gfx2d_pixel(x + col, y + row, RGB(sr, sg, sb));
        }
    }

    kfree(tmp);
}

/* Retro / CRT Effects */

void gfx2d_chromatic_aberration(int x, int y, int w, int h, int offset) {
    int row, col;
    uint32_t *tmp;
    uint32_t tmp_size;

    if (w <= 0 || h <= 0 || offset == 0) return;

    tmp_size = (uint32_t)w * (uint32_t)h * 4u;
    tmp = (uint32_t *)kmalloc(tmp_size);
    if (!tmp) return;

    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++)
            tmp[(uint32_t)row * (uint32_t)w + (uint32_t)col] =
                gfx2d_getpixel(x + col, y + row);

    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            /* Red channel shifted left */
            int r_col = col - offset;
            /* Blue channel shifted right */
            int b_col = col + offset;
            /* Green stays in place */

            uint32_t g_px = tmp[(uint32_t)row * (uint32_t)w + (uint32_t)col];
            int g = G(g_px);

            int r = 0;
            if (r_col >= 0 && r_col < w)
                r = R(tmp[(uint32_t)row * (uint32_t)w + (uint32_t)r_col]);
            else
                r = R(g_px);

            int b = 0;
            if (b_col >= 0 && b_col < w)
                b = B(tmp[(uint32_t)row * (uint32_t)w + (uint32_t)b_col]);
            else
                b = B(g_px);

            gfx2d_pixel(x + col, y + row, RGB(r, g, b));
        }
    }

    kfree(tmp);
}

void gfx2d_scanlines_ex(int x, int y, int w, int h,
                        int alpha, int pattern) {
    int row, col;

    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;

    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            int darken = 0;

            switch (pattern) {
            case GFX2D_SCANLINE_HORIZONTAL:
                darken = ((row + y) & 1);
                break;
            case GFX2D_SCANLINE_VERTICAL:
                darken = ((col + x) & 1);
                break;
            case GFX2D_SCANLINE_GRID:
                darken = ((row + y) & 1) || ((col + x) & 1);
                break;
            case GFX2D_SCANLINE_APERTURE:
                darken = (((row + y) % 3) == 0) || (((col + x) % 3) == 0);
                break;
            default:
                darken = ((row + y) & 1);
                break;
            }

            if (darken) {
                uint32_t px = gfx2d_getpixel(x + col, y + row);
                int r = (R(px) * (255 - alpha)) / 255;
                int g = (G(px) * (255 - alpha)) / 255;
                int b = (B(px) * (255 - alpha)) / 255;
                gfx2d_pixel(x + col, y + row, RGB(r, g, b));
            }
        }
    }
}

void gfx2d_noise(int x, int y, int w, int h,
                 int intensity, uint32_t seed) {
    int row, col;
    uint32_t rng = seed;

    if (intensity < 0) intensity = 0;
    if (intensity > 255) intensity = 255;
    if (rng == 0) rng = 0x12345678;

    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            uint32_t px = gfx2d_getpixel(x + col, y + row);
            int noise_val = (int)(xorshift32(&rng) & 0xFF) - 128;
            noise_val = (noise_val * intensity) / 256;

            int r = R(px) + noise_val;
            int g = G(px) + noise_val;
            int b = B(px) + noise_val;
            gfx2d_pixel(x + col, y + row, RGB(r, g, b));
        }
    }
}
