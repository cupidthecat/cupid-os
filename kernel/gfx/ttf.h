/* ttf.h - TrueType parser for CupidOS fontsys.
 *
 * Reads exactly the tables needed for ASCII/Latin-1 glyph rendering:
 *   head, hhea, maxp, name, OS/2, cmap (formats 4 and 12), loca, glyf,
 *   hmtx. Composite glyphs are resolved recursively (depth cap 4). No
 *   hinting interpreter, no kerning, no GPOS.
 *
 * Reference: stb_truetype.h by Sean Barrett (public domain).
*/

#ifndef TTF_H
#define TTF_H

#include "types.h"

/* Big-endian primitive readers. blob[off] must be in-bounds. */
uint16_t ttf_u16(const uint8_t *blob, int off);
uint32_t ttf_u32(const uint8_t *blob, int off);
int16_t  ttf_i16(const uint8_t *blob, int off);

/* Walk the TTF table directory at the start of `blob`. On success
 * fills *out_off and *out_len with the table's location and returns 0.
 * Returns -1 if the tag isn't present.
 *   tag: 4-byte ASCII packed big-endian, e.g. ('h'<<24|'e'<<16|'a'<<8|'d').*/
int ttf_find_table(const uint8_t *blob, int blob_len,
                   uint32_t tag,
                   int *out_off, int *out_len);

/* head:  units_per_em (line 18), index_to_loc_fmt (line 50). */
int ttf_parse_head(const uint8_t *blob, int off,
                   int *units_per_em, int *index_to_loc_fmt);

/* hhea:  ascender, descender (signed), line_gap, num_h_metrics. */
int ttf_parse_hhea(const uint8_t *blob, int off,
                   int *ascent, int *descent, int *line_gap,
                   int *num_h_metrics);

/* maxp:  numGlyphs only. */
int ttf_parse_maxp(const uint8_t *blob, int off, int *num_glyphs);

/* OS/2:  usWeightClass, fsSelection bit 0 (italic). */
int ttf_parse_os2(const uint8_t *blob, int off,
                  int *weight, int *italic);

/* name:  pull a UTF-8/Latin-1 string for `name_id` (1=family, 2=subfamily).
 * Writes up to (out_cap-1) bytes plus NUL into out_buf. Returns the
 * number of bytes written, or 0 on failure.*/
int ttf_parse_name(const uint8_t *blob, int off,
                   int name_id,
                   char *out_buf, int out_cap);

/* cmap: codepoint -> glyph index. Returns 0 (notdef) if not found. */
int ttf_cmap_glyph(const uint8_t *blob, int cmap_off, int codepoint);

/* hmtx: advance width for glyph index. Returns 0 on bad input. */
int ttf_glyph_advance(const uint8_t *blob, int hmtx_off,
                      int num_h_metrics, int num_glyphs,
                      int glyph_idx);

/* loca: byte offset of glyph in glyf table; -1 if empty glyph (zero-len). */
int ttf_glyph_offset(const uint8_t *blob, int loca_off,
                     int idx, int index_to_loc_fmt);

/* Decode the outline for `glyph_idx` into caller-provided scratch arrays.
 * Outputs are flat per-point arrays plus contour endpoints, all in font
 * units (Y still in font convention - up is positive).
 *   xs/ys/on_curve: parallel arrays, capacity pt_cap. *out_n_pts written.
 *   contour_end:    per-contour last-point index, capacity ctr_cap.
 *                   *out_n_ctrs written.
 *   *out_xmin/ymin/xmax/ymax: bbox in font units.
 * Returns 0 on success, -1 on overflow / malformed glyph.
 * For composite glyphs the routine recurses (depth cap 4) and merges
 * the children's points + contours into the same output arrays.*/
int ttf_glyph_outline(const uint8_t *blob,
                      int glyf_off, int loca_off, int idx_to_loc_fmt,
                      int glyph_idx,
                      int   *xs,        int *ys, int *on_curve,
                      int    pt_cap,    int *out_n_pts,
                      int   *contour_end,
                      int    ctr_cap,   int *out_n_ctrs,
                      int *out_xmin, int *out_ymin,
                      int *out_xmax, int *out_ymax);

#endif /* TTF_H */
