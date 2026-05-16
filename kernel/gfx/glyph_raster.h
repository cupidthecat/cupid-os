/* glyph_raster.h - outline -> 8-bit alpha bitmap.
 *
 * Takes a font-unit outline (flat point arrays + per-contour endpoints,
 * as produced by ttf_glyph_outline) and produces an anti-aliased
 * coverage bitmap at a given pixel size. 4x4 supersampling, non-zero
 * winding fill, no hinting.*/

#ifndef GLYPH_RASTER_H
#define GLYPH_RASTER_H

#include "types.h"

/* Rasterize an outline into a freshly kmalloc'd alpha bitmap.
 *
 *   xs/ys/on_curve: per-point arrays, n_pts long, in font units.
 *                   Y is up (font convention).
 *   contour_end:    last-point index per contour, n_ctrs long.
 *   xmin/ymin/xmax/ymax: outline bbox in font units.
 *   units_per_em:   from head table.
 *   size_px:        target pixel height.
 *
 * On success returns 0 and fills:
 *   *out_alpha   : kmalloc'd w*h bytes (0..255). NULL if glyph has no
 *                  ink (e.g. space). Caller must kfree on eviction.
 *   *out_w/h     : bitmap dims (>=0). 0/0 if no ink.
 *   *out_bx      : x bearing in pixels (left edge offset from pen).
 *   *out_by      : pixels above baseline (positive = up; cap-height
 *                  glyphs sit at +ascent).
 *
 * Returns -1 on overflow of internal scratch buffers (rare; defensive).*/
int glyph_rasterize(const int *xs, const int *ys, const int *on_curve,
                    int n_pts,
                    const int *contour_end, int n_ctrs,
                    int xmin, int ymin, int xmax, int ymax,
                    int units_per_em, int size_px,
                    uint8_t **out_alpha,
                    int *out_w, int *out_h,
                    int *out_bx, int *out_by);

#endif /* GLYPH_RASTER_H */
