/**
 * gfx2d_effects.h - Filters & Post-Processing for cupid-os
 *
 * Image processing filters operating on screen regions or surfaces:
 * blur, color manipulation, edge detection, retro/CRT effects,
 * and custom convolution kernels.
 */

#ifndef GFX2D_EFFECTS_H
#define GFX2D_EFFECTS_H

#include "types.h"

#define GFX2D_TINT_MULTIPLY 0
#define GFX2D_TINT_SCREEN   1
#define GFX2D_TINT_OVERLAY  2

#define GFX2D_SCANLINE_HORIZONTAL  0
#define GFX2D_SCANLINE_VERTICAL    1
#define GFX2D_SCANLINE_GRID        2
#define GFX2D_SCANLINE_APERTURE    3

/** Box blur on a screen region. radius 1-8 recommended. */
void gfx2d_blur_box(int x, int y, int w, int h, int radius);

/** Box blur on an offscreen surface. */
void gfx2d_blur_box_surface(int surf_handle, int radius);

/** Gaussian blur approximation (3-pass box blur). */
void gfx2d_blur_gaussian(int x, int y, int w, int h, int radius);

/** Motion blur in a direction (angle in degrees, distance in pixels). */
void gfx2d_blur_motion(int x, int y, int w, int h, int angle, int distance);

/** Adjust brightness (-255 to +255). */
void gfx2d_brightness(int x, int y, int w, int h, int amount);

/** Adjust contrast (-255 to +255). */
void gfx2d_contrast(int x, int y, int w, int h, int amount);

/** Adjust saturation (0=grayscale, 256=normal, 512=2x saturated). */
void gfx2d_saturation(int x, int y, int w, int h, int amount);

/** Shift hue (0-359 degrees). */
void gfx2d_hue_shift(int x, int y, int w, int h, int degrees);

/** Color tint with blend mode (multiply, screen, overlay). */
void gfx2d_tint_ex(int x, int y, int w, int h, uint32_t color,
                   int alpha, int mode);

/** Sobel edge detection. Edges drawn in edge_color. */
void gfx2d_edges(int x, int y, int w, int h, uint32_t edge_color);

/** Emboss effect. angle: light direction in degrees. */
void gfx2d_emboss(int x, int y, int w, int h, int angle);

/** Posterize: reduce to given number of color levels per channel. */
void gfx2d_posterize(int x, int y, int w, int h, int levels);

/** Apply a 3x3 convolution kernel. Kernel values divided by divisor. */
void gfx2d_convolve_3x3(int x, int y, int w, int h,
                         int kernel[9], int divisor);

/** Apply a 5x5 convolution kernel. */
void gfx2d_convolve_5x5(int x, int y, int w, int h,
                         int kernel[25], int divisor);

/** Chromatic aberration (RGB channel offset). */
void gfx2d_chromatic_aberration(int x, int y, int w, int h, int offset);

/** Extended scan lines with pattern selection. */
void gfx2d_scanlines_ex(int x, int y, int w, int h, int alpha, int pattern);

/** Film grain / noise effect. */
void gfx2d_noise(int x, int y, int w, int h, int intensity, uint32_t seed);

void gfx2d_effects_init(void);

#endif /* GFX2D_EFFECTS_H */
