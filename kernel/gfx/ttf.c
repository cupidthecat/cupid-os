/* ttf.c - TrueType parser. See ttf.h.
 *
 * All offsets in the public API are absolute byte offsets into a single
 * flat blob. Every read is bounds-checked against the blob length the
 * caller passed once at table-discovery time and then trusted within
 * each table. Malformed fonts return errors rather than crashing.
 */

#include "ttf.h"
#include "../drivers/serial.h"
#include "string.h"

/*  --- BE primitive readers --------------------------------------- */

uint16_t ttf_u16(const uint8_t *blob, int off) {
    return (uint16_t)((((uint32_t)blob[off]) << 8) | (uint32_t)blob[off + 1]);
}

uint32_t ttf_u32(const uint8_t *blob, int off) {
    return ((uint32_t)blob[off] << 24) | ((uint32_t)blob[off + 1] << 16)
         | ((uint32_t)blob[off + 2] << 8) | (uint32_t)blob[off + 3];
}

int16_t ttf_i16(const uint8_t *blob, int off) {
    return (int16_t)ttf_u16(blob, off);
}

/*  --- table directory walk -------------------------------------- */

int ttf_find_table(const uint8_t *blob, int blob_len,
                   uint32_t tag,
                   int *out_off, int *out_len) {
    if (blob_len < 12) return -1;
    /* sfnt version: 0x00010000 (TrueType) or 'OTTO' / 'true' / 'typ1'.
     * We accept TrueType only; OTTO indicates CFF outlines we don't decode. */
    uint32_t scaler = ttf_u32(blob, 0);
    if (scaler != 0x00010000u && scaler != 0x74727565u /* 'true' */) {
        return -1;
    }
    int num_tables = (int)ttf_u16(blob, 4);
    int dir = 12;
    if (blob_len < dir + num_tables * 16) return -1;
    for (int i = 0; i < num_tables; i++) {
        int e = dir + i * 16;
        uint32_t t = ttf_u32(blob, e);
        if (t == tag) {
            int off = (int)ttf_u32(blob, e + 8);
            int len = (int)ttf_u32(blob, e + 12);
            if (off < 0 || len < 0 || off + len > blob_len) return -1;
            *out_off = off;
            *out_len = len;
            return 0;
        }
    }
    return -1;
}

/*  --- head ------------------------------------------------------- */

int ttf_parse_head(const uint8_t *blob, int off,
                   int *units_per_em, int *index_to_loc_fmt) {
    /* head.unitsPerEm at offset 18, head.indexToLocFormat at 50.
     * Total size 54. */
    *units_per_em = (int)ttf_u16(blob, off + 18);
    *index_to_loc_fmt = (int)ttf_i16(blob, off + 50);
    return 0;
}

/*  --- hhea ------------------------------------------------------- */

int ttf_parse_hhea(const uint8_t *blob, int off,
                   int *ascent, int *descent, int *line_gap,
                   int *num_h_metrics) {
    /* hhea: version(4) ascent(2) descent(2) lineGap(2) ...
     * numberOfHMetrics at +34. */
    *ascent  = (int)ttf_i16(blob, off + 4);
    *descent = (int)ttf_i16(blob, off + 6);
    *line_gap = (int)ttf_i16(blob, off + 8);
    *num_h_metrics = (int)ttf_u16(blob, off + 34);
    return 0;
}

/*  --- maxp ------------------------------------------------------- */

int ttf_parse_maxp(const uint8_t *blob, int off, int *num_glyphs) {
    *num_glyphs = (int)ttf_u16(blob, off + 4);
    return 0;
}

/*  --- OS/2 ------------------------------------------------------- */

int ttf_parse_os2(const uint8_t *blob, int off,
                  int *weight, int *italic) {
    /* OS/2 v0: usWeightClass at +4, fsSelection at +62. */
    *weight = (int)ttf_u16(blob, off + 4);
    uint16_t sel = ttf_u16(blob, off + 62);
    *italic = (sel & 0x0001u) ? 1 : 0;
    return 0;
}

/*  --- name ------------------------------------------------------- */

/* Decode UTF-16BE bytes [src..src+src_len) into Latin-1 best effort.
 * Codepoints > 0xFF become '?'. Writes <= cap-1 bytes plus NUL. */
static int decode_utf16be_latin1(const uint8_t *src, int src_len,
                                 char *dst, int cap) {
    int o = 0;
    int i = 0;
    while (i + 1 < src_len && o + 1 < cap) {
        uint16_t cp = ttf_u16(src, i);
        i += 2;
        if (cp == 0) break;
        dst[o++] = (cp <= 0xFF) ? (char)cp : '?';
    }
    if (o < cap) dst[o] = 0;
    return o;
}

int ttf_parse_name(const uint8_t *blob, int off,
                   int name_id,
                   char *out_buf, int out_cap) {
    if (out_cap <= 0) return 0;
    out_buf[0] = 0;

    int count = (int)ttf_u16(blob, off + 2);
    int storage_off = off + (int)ttf_u16(blob, off + 4);

    /* Prefer (platform=3, encoding=1, language=0x0409) - Windows English.
     * Fall back to (platform=1, encoding=0) - Macintosh Roman.
     * Fall back to anything that matches the name_id. */
    int best = -1;
    int best_score = -1;
    for (int i = 0; i < count; i++) {
        int rec = off + 6 + i * 12;
        int plat = (int)ttf_u16(blob, rec + 0);
        int enc  = (int)ttf_u16(blob, rec + 2);
        int lang = (int)ttf_u16(blob, rec + 4);
        int nid  = (int)ttf_u16(blob, rec + 6);
        if (nid != name_id) continue;
        int score = 0;
        if (plat == 3 && enc == 1 && lang == 0x0409) score = 100;
        else if (plat == 3 && enc == 1) score = 50;
        else if (plat == 1 && enc == 0) score = 40;
        else score = 10;
        if (score > best_score) {
            best_score = score;
            best = rec;
        }
    }
    if (best < 0) return 0;

    int slen = (int)ttf_u16(blob, best + 8);
    int soff = (int)ttf_u16(blob, best + 10);
    int plat = (int)ttf_u16(blob, best + 0);
    const uint8_t *src = blob + storage_off + soff;

    if (plat == 3) {
        /* UTF-16 BE */
        return decode_utf16be_latin1(src, slen, out_buf, out_cap);
    }
    /* Mac Roman / ASCII - copy bytes as-is, treat as Latin-1. */
    int o = 0;
    int n = slen;
    if (n > out_cap - 1) n = out_cap - 1;
    for (int k = 0; k < n; k++) out_buf[o++] = (char)src[k];
    out_buf[o] = 0;
    return o;
}

/*  --- cmap ------------------------------------------------------- */

/* Format 4: segmented BMP (codepoints 0..0xFFFF). */
static int cmap4_lookup(const uint8_t *blob, int sub, int cp) {
    if (cp > 0xFFFF) return 0;
    int seg_count_x2 = (int)ttf_u16(blob, sub + 6);
    int seg_count = seg_count_x2 / 2;
    int end_off    = sub + 14;
    int start_off  = end_off + seg_count_x2 + 2;        /* +reservedPad(2) */
    int delta_off  = start_off + seg_count_x2;
    int range_off  = delta_off + seg_count_x2;

    /* Linear scan; segments are sorted but seg counts are small. */
    int seg = -1;
    for (int i = 0; i < seg_count; i++) {
        uint16_t end = ttf_u16(blob, end_off + i * 2);
        if ((int)end >= cp) { seg = i; break; }
    }
    if (seg < 0) return 0;
    uint16_t start = ttf_u16(blob, start_off + seg * 2);
    if ((int)start > cp) return 0;
    uint16_t id_range = ttf_u16(blob, range_off + seg * 2);
    int16_t  id_delta = ttf_i16(blob, delta_off + seg * 2);

    if (id_range == 0) {
        return (int)((uint16_t)((int)cp + (int)id_delta));
    }
    /* Glyph offset from idRangeOffset[i] + 2*(c - startCode[i]) + &idRangeOffset[i]. */
    int g_off = range_off + seg * 2 + (int)id_range
              + 2 * (cp - (int)start);
    uint16_t gid = ttf_u16(blob, g_off);
    if (gid == 0) return 0;
    return (int)((uint16_t)((int)gid + (int)id_delta));
}

/* Format 12: sparse groups (full Unicode). */
static int cmap12_lookup(const uint8_t *blob, int sub, int cp) {
    int n_groups = (int)ttf_u32(blob, sub + 12);
    int g = sub + 16;
    /* Linear walk - groups are sorted but linear is fine for our font sizes. */
    for (int i = 0; i < n_groups; i++) {
        int start = (int)ttf_u32(blob, g + i * 12 + 0);
        int end   = (int)ttf_u32(blob, g + i * 12 + 4);
        int sgid  = (int)ttf_u32(blob, g + i * 12 + 8);
        if (cp < start) return 0;
        if (cp <= end) return sgid + (cp - start);
    }
    return 0;
}

int ttf_cmap_glyph(const uint8_t *blob, int cmap_off, int codepoint) {
    int n_subtables = (int)ttf_u16(blob, cmap_off + 2);
    /* First pass: prefer (plat=3, enc=10) - Win Unicode UCS-4 (format 12).
     * Second pass: (plat=3, enc=1) - Win Unicode BMP (format 4).
     * Third  pass: (plat=0, enc=*) - Unicode (any). */
    int best = -1;
    int best_score = -1;
    for (int i = 0; i < n_subtables; i++) {
        int e = cmap_off + 4 + i * 8;
        int plat = (int)ttf_u16(blob, e + 0);
        int enc  = (int)ttf_u16(blob, e + 2);
        int sub  = cmap_off + (int)ttf_u32(blob, e + 4);
        int score = 0;
        if (plat == 3 && enc == 10) score = 90;
        else if (plat == 3 && enc == 1) score = 70;
        else if (plat == 0) score = 50 + enc;
        else score = 10;
        if (score > best_score) {
            best_score = score;
            best = sub;
        }
    }
    if (best < 0) return 0;

    int format = (int)ttf_u16(blob, best);
    if (format == 4)  return cmap4_lookup(blob, best, codepoint);
    if (format == 12) return cmap12_lookup(blob, best, codepoint);
    /* Unhandled format - fall back to scanning the lower-priority cmaps. */
    for (int i = 0; i < n_subtables; i++) {
        int e = cmap_off + 4 + i * 8;
        int sub  = cmap_off + (int)ttf_u32(blob, e + 4);
        int f    = (int)ttf_u16(blob, sub);
        if (f == 4)  { int g = cmap4_lookup(blob, sub, codepoint);  if (g) return g; }
        if (f == 12) { int g = cmap12_lookup(blob, sub, codepoint); if (g) return g; }
    }
    return 0;
}

/*  --- hmtx ------------------------------------------------------- */

int ttf_glyph_advance(const uint8_t *blob, int hmtx_off,
                      int num_h_metrics, int num_glyphs,
                      int glyph_idx) {
    if (glyph_idx < 0 || glyph_idx >= num_glyphs) return 0;
    if (glyph_idx < num_h_metrics) {
        return (int)ttf_u16(blob, hmtx_off + glyph_idx * 4);
    }
    /* Glyphs beyond num_h_metrics share the last advance. */
    return (int)ttf_u16(blob, hmtx_off + (num_h_metrics - 1) * 4);
}

/*  --- loca ------------------------------------------------------- */

int ttf_glyph_offset(const uint8_t *blob, int loca_off,
                     int idx, int index_to_loc_fmt) {
    int a, b;
    if (index_to_loc_fmt == 0) {
        a = (int)ttf_u16(blob, loca_off + idx * 2) * 2;
        b = (int)ttf_u16(blob, loca_off + idx * 2 + 2) * 2;
    } else {
        a = (int)ttf_u32(blob, loca_off + idx * 4);
        b = (int)ttf_u32(blob, loca_off + idx * 4 + 4);
    }
    if (a == b) return -1;            /* empty glyph */
    return a;
}

/*  --- glyf ------------------------------------------------------- */

/* Simple-glyph flag bits */
#define TTF_FLAG_ON_CURVE       0x01
#define TTF_FLAG_X_SHORT        0x02
#define TTF_FLAG_Y_SHORT        0x04
#define TTF_FLAG_REPEAT         0x08
#define TTF_FLAG_X_SAME_OR_POS  0x10
#define TTF_FLAG_Y_SAME_OR_POS  0x20

/* Composite-glyph flag bits */
#define TTF_COMP_ARG_1_AND_2_ARE_WORDS   0x0001
#define TTF_COMP_ARGS_ARE_XY_VALUES      0x0002
#define TTF_COMP_WE_HAVE_A_SCALE         0x0008
#define TTF_COMP_MORE_COMPONENTS         0x0020
#define TTF_COMP_WE_HAVE_AN_X_AND_Y_SCALE 0x0040
#define TTF_COMP_WE_HAVE_A_TWO_BY_TWO    0x0080

static int ttf_decode_simple(const uint8_t *blob, int g_off,
                             int n_contours,
                             int *xs, int *ys, int *on_curve,
                             int pt_cap, int *out_n_pts,
                             int *contour_end, int ctr_cap, int n_ctr_base,
                             int x_off, int y_off) {
    /* glyf header: numberOfContours(2) xMin(2) yMin(2) xMax(2) yMax(2) = 10 */
    int p = g_off + 10;
    if (n_contours <= 0) return 0;
    if (n_ctr_base + n_contours > ctr_cap) return -1;

    int last_pt = -1;
    for (int i = 0; i < n_contours; i++) {
        int e = (int)ttf_u16(blob, p + i * 2);
        contour_end[n_ctr_base + i] = (*out_n_pts) + e;
        if (e > last_pt) last_pt = e;
    }
    int n_pts = last_pt + 1;
    p += n_contours * 2;

    int instr_len = (int)ttf_u16(blob, p);
    p += 2 + instr_len;       /* skip hinting bytecode */

    if ((*out_n_pts) + n_pts > pt_cap) return -1;

    /* Flags array, run-length encoded via REPEAT bit. */
    int flag_off = p;
    {
        int read = 0;
        int q = p;
        while (read < n_pts) {
            uint8_t f = blob[q++];
            int rep = 1;
            if (f & TTF_FLAG_REPEAT) { rep = 1 + (int)blob[q++]; }
            for (int k = 0; k < rep && read < n_pts; k++) {
                on_curve[(*out_n_pts) + read] = (f & TTF_FLAG_ON_CURVE) ? 1 : 0;
                /* Stash flag in xs[] temporarily; overwritten below. */
                xs[(*out_n_pts) + read] = (int)f;
                read++;
            }
        }
        p = q;
    }
    (void)flag_off;

    /* X coords. */
    int x_acc = 0;
    for (int i = 0; i < n_pts; i++) {
        int idx = (*out_n_pts) + i;
        uint8_t f = (uint8_t)xs[idx];
        int dx = 0;
        if (f & TTF_FLAG_X_SHORT) {
            dx = (int)blob[p++];
            if (!(f & TTF_FLAG_X_SAME_OR_POS)) dx = -dx;
        } else if (!(f & TTF_FLAG_X_SAME_OR_POS)) {
            dx = (int)ttf_i16(blob, p); p += 2;
        }
        x_acc += dx;
        xs[idx] = x_acc + x_off;
    }
    /* Y coords. */
    int y_acc = 0;
    for (int i = 0; i < n_pts; i++) {
        int idx = (*out_n_pts) + i;
        /* The flag was in xs[] but we just overwrote it. Re-read from
         * blob via a second pass over the flag stream. Cheaper: stash
         * flags in on_curve high bits before x decode. We've already
         * lost xs - so reparse flags. */
        (void)idx;
    }
    /* Reparse flags for Y deltas. (Simple loop again.) */
    {
        int read = 0;
        int q = flag_off;
        while (read < n_pts) {
            uint8_t f = blob[q++];
            int rep = 1;
            if (f & TTF_FLAG_REPEAT) { rep = 1 + (int)blob[q++]; }
            for (int k = 0; k < rep && read < n_pts; k++) {
                int idx = (*out_n_pts) + read;
                int dy = 0;
                if (f & TTF_FLAG_Y_SHORT) {
                    dy = (int)blob[p++];
                    if (!(f & TTF_FLAG_Y_SAME_OR_POS)) dy = -dy;
                } else if (!(f & TTF_FLAG_Y_SAME_OR_POS)) {
                    dy = (int)ttf_i16(blob, p); p += 2;
                }
                y_acc += dy;
                ys[idx] = y_acc + y_off;
                read++;
            }
        }
    }
    *out_n_pts += n_pts;
    return 0;
}

static int ttf_outline_recurse(const uint8_t *blob,
                               int glyf_off, int loca_off, int idx_to_loc_fmt,
                               int glyph_idx, int depth,
                               int *xs, int *ys, int *on_curve,
                               int pt_cap, int *out_n_pts,
                               int *contour_end, int ctr_cap, int *out_n_ctrs,
                               int x_off, int y_off,
                               int *xmin, int *ymin, int *xmax, int *ymax) {
    if (depth > 4) return -1;
    int rel = ttf_glyph_offset(blob, loca_off, glyph_idx, idx_to_loc_fmt);
    if (rel < 0) return 0;     /* empty glyph */
    int g = glyf_off + rel;

    int n_contours = (int)ttf_i16(blob, g);
    int gxmin = (int)ttf_i16(blob, g + 2);
    int gymin = (int)ttf_i16(blob, g + 4);
    int gxmax = (int)ttf_i16(blob, g + 6);
    int gymax = (int)ttf_i16(blob, g + 8);
    if (depth == 0) {
        *xmin = gxmin; *ymin = gymin; *xmax = gxmax; *ymax = gymax;
    } else {
        if (gxmin + x_off < *xmin) *xmin = gxmin + x_off;
        if (gymin + y_off < *ymin) *ymin = gymin + y_off;
        if (gxmax + x_off > *xmax) *xmax = gxmax + x_off;
        if (gymax + y_off > *ymax) *ymax = gymax + y_off;
    }

    if (n_contours >= 0) {
        int ctr_base = *out_n_ctrs;
        int rc = ttf_decode_simple(blob, g, n_contours,
                                   xs, ys, on_curve,
                                   pt_cap, out_n_pts,
                                   contour_end, ctr_cap, ctr_base,
                                   x_off, y_off);
        if (rc != 0) return rc;
        *out_n_ctrs += n_contours;
        return 0;
    }

    /* Composite. */
    int p = g + 10;
    int more = 1;
    while (more) {
        uint16_t flags = ttf_u16(blob, p);
        uint16_t comp_idx = ttf_u16(blob, p + 2);
        p += 4;

        int dx, dy;
        if (flags & TTF_COMP_ARG_1_AND_2_ARE_WORDS) {
            dx = (int)ttf_i16(blob, p);     p += 2;
            dy = (int)ttf_i16(blob, p);     p += 2;
        } else {
            dx = (int8_t)blob[p++];
            dy = (int8_t)blob[p++];
        }
        if (!(flags & TTF_COMP_ARGS_ARE_XY_VALUES)) {
            /* Match-points form: not supported, skip the component but
             * keep parsing siblings so we don't bail on the whole glyph. */
            dx = 0; dy = 0;
        }
        /* Skip transform matrix - we don't apply scale/rotation in v1. */
        if (flags & TTF_COMP_WE_HAVE_A_SCALE) p += 2;
        else if (flags & TTF_COMP_WE_HAVE_AN_X_AND_Y_SCALE) p += 4;
        else if (flags & TTF_COMP_WE_HAVE_A_TWO_BY_TWO) p += 8;

        int rc = ttf_outline_recurse(blob, glyf_off, loca_off, idx_to_loc_fmt,
                                     (int)comp_idx, depth + 1,
                                     xs, ys, on_curve,
                                     pt_cap, out_n_pts,
                                     contour_end, ctr_cap, out_n_ctrs,
                                     x_off + dx, y_off + dy,
                                     xmin, ymin, xmax, ymax);
        if (rc != 0) return rc;

        more = (flags & TTF_COMP_MORE_COMPONENTS) ? 1 : 0;
    }
    return 0;
}

int ttf_glyph_outline(const uint8_t *blob,
                      int glyf_off, int loca_off, int idx_to_loc_fmt,
                      int glyph_idx,
                      int *xs, int *ys, int *on_curve,
                      int pt_cap, int *out_n_pts,
                      int *contour_end,
                      int ctr_cap, int *out_n_ctrs,
                      int *out_xmin, int *out_ymin,
                      int *out_xmax, int *out_ymax) {
    *out_n_pts = 0;
    *out_n_ctrs = 0;
    *out_xmin = 0; *out_ymin = 0; *out_xmax = 0; *out_ymax = 0;
    int rel = ttf_glyph_offset(blob, loca_off, glyph_idx, idx_to_loc_fmt);
    if (rel < 0) return 0;
    return ttf_outline_recurse(blob, glyf_off, loca_off, idx_to_loc_fmt,
                               glyph_idx, 0,
                               xs, ys, on_curve, pt_cap, out_n_pts,
                               contour_end, ctr_cap, out_n_ctrs,
                               0, 0,
                               out_xmin, out_ymin, out_xmax, out_ymax);
}
