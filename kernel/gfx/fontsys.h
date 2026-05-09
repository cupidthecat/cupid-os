/* fontsys.h - OS-wide TTF font system for CupidOS.
 *
 * Resolves CSS-shaped font queries (family list, size_px, weight, italic)
 * to concrete font faces, rasterizes glyphs on demand from TrueType
 * outlines, caches the alpha bitmaps, and renders runs of text.
 *
 * Design references:
 *   - Lexbor (lxb_css_property_font_*) - CSS property surface, generic
 *     family enum ordering. See lexbor/source/lexbor/css/property.h.
 *   - Blink minimum-viable subset (FontDescription -> FontCache ->
 *     SimpleFontData -> GlyphPage). See blink/Source/platform/fonts/.
 *   - stb_truetype - outline + rasterizer reference.
 */

#ifndef FONTSYS_H
#define FONTSYS_H

#include "types.h"

/* Generic family keywords. Numeric ordering mirrors Lexbor's
 * LXB_CSS_FONT_FAMILY_* enum so a future Latin-1 codepath can pass the
 * same value through unchanged. */
#define FONTSYS_FAMILY_DEFAULT      0
#define FONTSYS_FAMILY_SERIF        1
#define FONTSYS_FAMILY_SANS_SERIF   2
#define FONTSYS_FAMILY_MONOSPACE    3
#define FONTSYS_FAMILY_CURSIVE      4   /* aliased to serif */
#define FONTSYS_FAMILY_FANTASY      5   /* aliased to sans  */
#define FONTSYS_FAMILY_SYSTEM_UI    6   /* aliased to sans  */

/* Initialise registry, glyph cache, and (if asset symbols are linked)
 * register the bundled Liberation TTFs. Idempotent. Called from kernel
 * boot just after gfx2d_init. */
void fontsys_init(void);

/* Register a TTF blob already in memory. Returns face_id (>=0) or -1.
 * Takes ownership of `blob` only if `take_ownership` is non-zero - in
 * that case fontsys frees it via kfree on shutdown. Otherwise the
 * caller must keep the blob alive for the lifetime of the face. */
int fontsys_register_blob(const char *blob, int blob_len, int take_ownership);

/* Register a TTF file from VFS by path. Reads it into kernel heap.
 * Returns face_id or -1. */
int fontsys_register_file(const char *path);

/* Drop a previously-registered face: free the blob if we own it, clear
 * the slot from the registry, and evict every cached glyph that pointed
 * at it. The browser uses this on per-page navigation to reclaim heap
 * held by site-supplied @font-face TTFs. Returns 0 on success or on
 * already-cleared slots, -1 if face_id is out of range. */
int fontsys_unregister(int face_id);

/* Wire a generic family kw to a concrete face_id. Used by fontsys_init
 * to set defaults; safe to call later to override. */
int fontsys_set_generic(int family_kw, int face_id);

/* Resolve a CSS-shaped query to a concrete face_id.
 *   family_name: NUL-terminated comma list, e.g. "Arial, sans-serif".
 *                Pass NULL or "" to skip name matching.
 *   generic_kw : FONTSYS_FAMILY_*. Used as fallback when no name in
 *                the list matches a registered face.
 *   weight     : 100..900 (CSS). 400 normal, 700 bold.
 *   italic     : 0 or 1.
 * Always returns a valid face_id (worst case the default face). */
int fontsys_match(const char *family_name, int generic_kw,
                  int weight, int italic);

/* Return how much synthetic styling fontsys_draw_run_styled will need
 * to apply on top of `face_id` to satisfy the CSS request. Bits 0/1
 * set when bold/italic synthesis is required. -1 on bad face. */
int fontsys_synth_flags(int face_id, int want_weight, int want_italic);

/* Per-face metrics scaled to size_px (rounded to integer pixels). */
int fontsys_ascent      (int face_id, int size_px);
int fontsys_descent     (int face_id, int size_px);
int fontsys_line_height (int face_id, int size_px);

/* Look up (and cache) a single glyph's coverage bitmap.
 * On success returns 0 and fills the out-parameters. The alpha buffer
 * is owned by the cache - caller must NOT free it.
 *   *out_alpha   : w*h coverage bytes, 0..255. NULL if glyph is empty.
 *   *out_w/h     : bitmap dimensions in pixels.
 *   *out_bx      : x bearing (where glyph starts relative to pen-x).
 *   *out_by      : y bearing in pixels above baseline (positive = up).
 *   *out_advance : pen advance in pixels.
 * Returns -1 if face_id invalid, codepoint missing, or rasterizer fails. */
int fontsys_glyph(int face_id, int codepoint, int size_px,
                  const uint8_t **out_alpha,
                  int *out_w, int *out_h,
                  int *out_bx, int *out_by, int *out_advance);

/* Sum advance widths of a UTF-8 byte run. Stops at len bytes or NUL,
 * whichever comes first. Returns 0 on bad face. */
int fontsys_run_width(int face_id, int size_px,
                      const char *bytes, int len);

/* Per-glyph extra horizontal advance applied when synthesising italic
 * via row-shear, so glyph N+1 doesn't collide with the slanted top of
 * glyph N. Layout multiplies this by the codepoint count of an italic
 * run before reserving line space. Mirrors the slope baked into
 * blit_glyph (fontsys.c). */
int fontsys_italic_extra(int size_px);

/* Draw a Latin-1 run anchored at (x, baseline_y). Rendered into the
 * currently-active gfx2d framebuffer using alpha-blended writes
 * through g2d_put_alpha. */
void fontsys_draw_run(int face_id, int size_px,
                      int x, int baseline_y,
                      const char *bytes, int len,
                      uint32_t color);

/* Same as fontsys_draw_run but synthesizes bold (second draw +1px) and
 * italic (per-row x shear) on top of the chosen face when the face
 * itself lacks the requested weight/italic. */
void fontsys_draw_run_styled(int face_id, int size_px,
                             int x, int baseline_y,
                             const char *bytes, int len,
                             uint32_t color,
                             int want_bold,
                             int want_italic);

/* Diagnostics. */
int fontsys_face_count(void);
const char *fontsys_face_family(int face_id);
int fontsys_face_weight(int face_id);
int fontsys_face_italic(int face_id);

/* OS-wide default face. -1 face_id means "no TTF default — bitmap path
 * stays in charge". gfx2d / graphics text primitives consult these on
 * every call. Setting face_id is cheap; readers are inlined-friendly. */
void fontsys_set_os_default(int face_id, int size_px);
int  fontsys_get_os_default_face(void);
int  fontsys_get_os_default_size(void);

/* Read /etc/font.conf and apply to the OS default. Format:
 *   family=<NUL-trimmed family or "__bitmap__">\n
 *   size=<integer pixels>\n
 * Returns 0 on success (file present and parsed), -1 on missing file
 * or parse error. Safe to call before any face is registered. */
int  fontsys_load_os_default_from_conf(void);

/* Pen advance in pixels for one codepoint at size_px. Faster than
 * fontsys_glyph for width-only loops — does not rasterize. Returns 0
 * on bad face / missing glyph. */
int  fontsys_advance(int face_id, int codepoint, int size_px);

#endif /* FONTSYS_H */
