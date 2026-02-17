/**
 * gfx2d_transform.h - 2D Affine Transform System for cupid-os
 *
 * Push/pop transform stack with translate, rotate, scale operations.
 * Uses 16.16 fixed-point math for precision without floating-point.
 */

#ifndef GFX2D_TRANSFORM_H
#define GFX2D_TRANSFORM_H

#include "types.h"

#define FP_SHIFT   16
#define FP_ONE     (1 << FP_SHIFT)          /* 65536 = 1.0 */
#define FP_HALF    (1 << (FP_SHIFT - 1))    /* 32768 = 0.5 */

/* Convert int to fixed-point */
#define INT_TO_FP(x) ((int)(x) << FP_SHIFT)

/* Convert fixed-point to int (truncate) */
#define FP_TO_INT(x) ((x) >> FP_SHIFT)

/* Fixed-point multiply: (a * b) >> 16 */
#define FP_MUL(a, b) ((int)(((int64_t)(a) * (int64_t)(b)) >> FP_SHIFT))

#define GFX2D_TRANSFORM_STACK_DEPTH 8

/** Push current transform onto the stack. */
void gfx2d_push_transform(void);

/** Pop and restore the previous transform. */
void gfx2d_pop_transform(void);

/** Reset current transform to identity (no transform). */
void gfx2d_reset_transform(void);

/** Translate the origin by (dx, dy) pixels. */
void gfx2d_translate(int dx, int dy);

/** Rotate by angle degrees (0-359). */
void gfx2d_rotate(int angle);

/** Scale by (sx, sy) in 16.16 fixed-point. FP_ONE = 1x. */
void gfx2d_scale(int sx, int sy);

/** Rotate around point (cx, cy) by angle degrees. */
void gfx2d_rotate_around(int cx, int cy, int angle);

/** Set the transform matrix directly.
 *  m[6] = {a, b, c, d, tx, ty} in fixed-point. */
void gfx2d_set_matrix(int m[6]);

/** Get current transform matrix m[6]. */
void gfx2d_get_matrix(int m[6]);

/** Transform a point (x,y) through the current matrix.
 *  Output written to *out_x, *out_y (integer screen coords). */
void gfx2d_transform_point(int x, int y, int *out_x, int *out_y);

/** Draw image (from gfx2d_assets) with current transform applied. */
void gfx2d_image_draw_transformed(int handle, int x, int y);

/** Draw sprite (from gfx2d) with current transform applied. */
void gfx2d_sprite_draw_transformed(int handle, int x, int y);

/** Draw text with current transform applied. */
void gfx2d_text_transformed(int x, int y, const char *str,
                            uint32_t color, int font);

void gfx2d_transform_init(void);

#endif /* GFX2D_TRANSFORM_H */
