/* glyph_raster.c - see glyph_raster.h.
 *
 * Pipeline:
 *   1. Walk every contour, emitting line segments into a flat seg[] array.
 *      On-curve and off-curve points form quadratic Beziers; consecutive
 *      off-curve points imply an implicit on-curve midpoint (TT spec).
 *      Curves are flattened by recursive midpoint subdivision until the
 *      segment is shorter than 0.35 px in pixel space.
 *   2. Allocate the alpha buffer at ceil(bbox in pixels).
 *   3. For each subpixel row (4 per output pixel), find edge crossings,
 *      sort by x, fill using non-zero winding, accumulate per-pixel
 *      coverage. After 4 sub-rows, write coverage * 16 (clamped) to
 *      the alpha buffer.
 *
 * Floating-point work is kept inside this file. Public API takes/returns
 * ints only - matches the warning in bin/feature15_libm.cc:3 about
 * CupidC FP-in-user-function edge cases (kernel side is plain C and
 * doesn't hit that, but we keep the rule for symmetry).*/

#include "glyph_raster.h"
#include "../mm/memory.h"
#include "string.h"

/* libm's floor/ceil use a CupidC-internal ABI (return in xmm0, not ST(0)),
 * so plain kernel C cannot call them - see kernel/cpu/libm.h:17. We do all
 * the rounding work locally with these inline helpers.*/
static inline int float_floor_int(float v) {
    int i = (int)v;                     /* truncation toward zero */
    if (v < 0.0f && (float)i != v) i--; /* adjust for negative non-integer */
    return i;
}
static inline int float_ceil_int(float v) {
    int i = (int)v;
    if (v > 0.0f && (float)i != v) i++;
    return i;
}

#define MAX_SEGS    8192
#define MAX_CROSS    256
#define SUBPIX         4

/* Per-call scratch. Glyphs are processed serially in the cache; one
 * static arena is enough and avoids heap churn on every glyph miss.*/
static float seg_x0[MAX_SEGS];
static float seg_y0[MAX_SEGS];
static float seg_x1[MAX_SEGS];
static float seg_y1[MAX_SEGS];
static int   seg_n;

static float xcross[MAX_CROSS];
static int   xdir  [MAX_CROSS];

static void emit_seg(float x0, float y0, float x1, float y1) {
    if (seg_n >= MAX_SEGS) return;
    /* Drop horizontal segments - they never cross a scanline. */
    if (y0 == y1) return;
    seg_x0[seg_n] = x0; seg_y0[seg_n] = y0;
    seg_x1[seg_n] = x1; seg_y1[seg_n] = y1;
    seg_n++;
}

/* Recursive midpoint subdivision of a quadratic Bezier (p0, p1, p2). */
static void flatten_quad(float x0, float y0,
                         float x1, float y1,
                         float x2, float y2,
                         int depth) {
    /* Distance from control point to the chord midpoint in pixel space. */
    float mx = (x0 + x2) * 0.5f;
    float my = (y0 + y2) * 0.5f;
    float dx = x1 - mx;
    float dy = y1 - my;
    float d2 = dx * dx + dy * dy;
    if (d2 < 0.123f || depth >= 12) {
        /* < ~0.35 px deviation - approximate with a single line. */
        emit_seg(x0, y0, x2, y2);
        return;
    }
    float ax = (x0 + x1) * 0.5f;
    float ay = (y0 + y1) * 0.5f;
    float bx = (x1 + x2) * 0.5f;
    float by = (y1 + y2) * 0.5f;
    float cx = (ax + bx) * 0.5f;
    float cy = (ay + by) * 0.5f;
    flatten_quad(x0, y0, ax, ay, cx, cy, depth + 1);
    flatten_quad(cx, cy, bx, by, x2, y2, depth + 1);
}

/* Walk one contour [first .. last] and emit segments into seg[]. */
static void emit_contour(const int *xs, const int *ys, const int *on_curve,
                         int first, int last,
                         float scale, float pen_y_offset) {
    int n = last - first + 1;
    if (n < 2) return;

    /* Pre-resolve "starting" point. If first point is off-curve we walk
     * back to find an on-curve start; if every point is off-curve, the
     * synthetic start is the midpoint of the first and last.*/
    int start_idx = first;
    int end_idx   = last;
    float start_x, start_y;
    if (on_curve[start_idx]) {
        start_x = (float)xs[start_idx] * scale;
        start_y = pen_y_offset - (float)ys[start_idx] * scale;
    } else if (on_curve[end_idx]) {
        start_x = (float)xs[end_idx] * scale;
        start_y = pen_y_offset - (float)ys[end_idx] * scale;
        end_idx--;
        n--;
    } else {
        /* Both ends off-curve - synthesize on-curve start at midpoint. */
        start_x = ((float)xs[start_idx] + (float)xs[end_idx]) * 0.5f * scale;
        start_y = pen_y_offset
                - ((float)ys[start_idx] + (float)ys[end_idx]) * 0.5f * scale;
    }

    float prev_x = start_x;
    float prev_y = start_y;
    int   pending_off = 0;
    float ox = 0.0f, oy = 0.0f;

    for (int i = start_idx; i <= end_idx; i++) {
        float x = (float)xs[i] * scale;
        float y = pen_y_offset - (float)ys[i] * scale;
        if (on_curve[i]) {
            if (pending_off) {
                flatten_quad(prev_x, prev_y, ox, oy, x, y, 0);
                pending_off = 0;
            } else {
                emit_seg(prev_x, prev_y, x, y);
            }
            prev_x = x; prev_y = y;
        } else {
            if (pending_off) {
                /* Two off-curve in a row: implicit on-curve at midpoint. */
                float mx = (ox + x) * 0.5f;
                float my = (oy + y) * 0.5f;
                flatten_quad(prev_x, prev_y, ox, oy, mx, my, 0);
                prev_x = mx; prev_y = my;
            }
            ox = x; oy = y;
            pending_off = 1;
        }
    }
    /* Close the contour back to the start. */
    if (pending_off) {
        flatten_quad(prev_x, prev_y, ox, oy, start_x, start_y, 0);
    } else {
        emit_seg(prev_x, prev_y, start_x, start_y);
    }
}

/* Insertion sort the first n entries of xcross[] / xdir[] by xcross. */
static void sort_crossings(int n) {
    for (int i = 1; i < n; i++) {
        float kx = xcross[i];
        int   kd = xdir[i];
        int j = i - 1;
        while (j >= 0 && xcross[j] > kx) {
            xcross[j + 1] = xcross[j];
            xdir  [j + 1] = xdir  [j];
            j--;
        }
        xcross[j + 1] = kx;
        xdir  [j + 1] = kd;
    }
}

int glyph_rasterize(const int *xs, const int *ys, const int *on_curve,
                    int n_pts,
                    const int *contour_end, int n_ctrs,
                    int xmin, int ymin, int xmax, int ymax,
                    int units_per_em, int size_px,
                    uint8_t **out_alpha,
                    int *out_w, int *out_h,
                    int *out_bx, int *out_by) {
    *out_alpha = NULL;
    *out_w = 0; *out_h = 0; *out_bx = 0; *out_by = 0;
    if (n_pts <= 0 || n_ctrs <= 0 || size_px <= 0 || units_per_em <= 0) {
        return 0;
    }

    float scale = (float)size_px / (float)units_per_em;

    /* Pixel-space bbox: Y is flipped (font Y up -> bitmap Y down). The
     * glyph's font-space ymax sits at the smallest pixel-space y. We
     * translate so the bbox starts at pixel-space (0, 0).*/
    float pxmin = (float)xmin * scale;
    float pxmax = (float)xmax * scale;
    float pymin = -(float)ymax * scale;       /* top */
    float pymax = -(float)ymin * scale;       /* bottom */

    int ix0 = float_floor_int(pxmin);
    int ix1 = float_ceil_int (pxmax);
    int iy0 = float_floor_int(pymin);
    int iy1 = float_ceil_int (pymax);
    int w = ix1 - ix0;
    int h = iy1 - iy0;
    if (w <= 0 || h <= 0) return 0;

    /* pen_y_offset translates "outline at font-space Y" into bitmap row.
     * In bitmap row coords, baseline sits at (0 - iy0). For a point at
     * font-space Y yF, the bitmap row is (-yF * scale) - iy0.*/
    float pen_y_off = -(float)iy0;
    /* x_off so leftmost pixel-space coord lands at column 0. */
    float pen_x_off = -(float)ix0;

    seg_n = 0;
    int prev = 0;
    for (int c = 0; c < n_ctrs; c++) {
        int last = contour_end[c];
        if (last >= n_pts) last = n_pts - 1;
        /* Translate + scale all points by emit_contour, but x_off is
         * applied here by passing pre-shifted scale results - simpler
         * to subtract ix0 inside emit by passing scale and then shifting
         * the segs. Doing it in a tight post-loop instead.*/
        emit_contour(xs, ys, on_curve, prev, last, scale, pen_y_off);
        prev = last + 1;
    }
    /* Apply x offset to all segments. */
    for (int i = 0; i < seg_n; i++) {
        seg_x0[i] += pen_x_off;
        seg_x1[i] += pen_x_off;
    }

    if (seg_n == 0) return 0;       /* glyph has no ink */

    size_t bytes = (size_t)w * (size_t)h;
    uint8_t *alpha = (uint8_t *)kmalloc(bytes);
    if (!alpha) return -1;
    memset(alpha, 0, bytes);

    /* Per-pixel-row coverage accumulator: counts subpixels lit (0..16). */
    int *cov = (int *)kmalloc((size_t)w * sizeof(int));
    if (!cov) { kfree(alpha); return -1; }

    for (int y_pix = 0; y_pix < h; y_pix++) {
        for (int i = 0; i < w; i++) cov[i] = 0;

        for (int sub = 0; sub < SUBPIX; sub++) {
            float sy = (float)y_pix + ((float)sub + 0.5f) / (float)SUBPIX;

            /* Collect edges that straddle sy, in [y0, y1) half-open. */
            int n_cross = 0;
            for (int i = 0; i < seg_n; i++) {
                float y0 = seg_y0[i];
                float y1 = seg_y1[i];
                int dir;
                float ylo, yhi, x_at_lo, x_at_hi;
                if (y0 < y1) {
                    dir = -1;             /* down (winding -) */
                    ylo = y0; yhi = y1;
                    x_at_lo = seg_x0[i]; x_at_hi = seg_x1[i];
                } else {
                    dir = 1;              /* up   (winding +) */
                    ylo = y1; yhi = y0;
                    x_at_lo = seg_x1[i]; x_at_hi = seg_x0[i];
                }
                if (sy < ylo || sy >= yhi) continue;
                float t = (sy - ylo) / (yhi - ylo);
                float x = x_at_lo + (x_at_hi - x_at_lo) * t;
                if (n_cross < MAX_CROSS) {
                    xcross[n_cross] = x;
                    xdir  [n_cross] = dir;
                    n_cross++;
                }
            }
            if (n_cross < 2) continue;
            sort_crossings(n_cross);

            /* Non-zero winding sweep across this subpixel row. */
            int wn = 0;
            float xs_open = 0.0f;
            for (int k = 0; k < n_cross; k++) {
                int new_wn = wn + xdir[k];
                if (wn == 0 && new_wn != 0) {
                    xs_open = xcross[k];
                } else if (wn != 0 && new_wn == 0) {
                    float xa = xs_open;
                    float xb = xcross[k];
                    int ia = float_floor_int(xa * (float)SUBPIX);
                    int ib = float_ceil_int (xb * (float)SUBPIX);
                    if (ia < 0) ia = 0;
                    if (ib > w * SUBPIX) ib = w * SUBPIX;
                    /* Each lit subpixel column lights one slot of the
                     * containing output pixel.*/
                    for (int sx = ia; sx < ib; sx++) {
                        cov[sx / SUBPIX]++;
                    }
                }
                wn = new_wn;
            }
        }

        /* Resolve 0..16 coverage to 0..255 alpha. */
        for (int x_pix = 0; x_pix < w; x_pix++) {
            int c = cov[x_pix];
            if (c < 0) c = 0;
            if (c > 16) c = 16;
            int a = c * 16;
            if (a > 255) a = 255;
            alpha[y_pix * w + x_pix] = (uint8_t)a;
        }
    }

    kfree(cov);

    *out_alpha = alpha;
    *out_w = w;
    *out_h = h;
    *out_bx = ix0;
    /* Bitmap row 0 is at pixel-space y = iy0. baseline_y_pix = 0 (we
     * placed baseline at the origin of "pixel space" by negating font
     * Y). So pixels above baseline = -iy0 (the bitmap top row sits
     * iy0 below baseline if iy0>=0, or above if iy0<0). by reports
     * "pixels above baseline" as out_by = -iy0.*/
    *out_by = -iy0;
    return 0;
}
