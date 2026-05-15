/**
 * jpeg.c - Minimal baseline JPEG decoder for cupid-os
 *
 * Decodes SOF0/SOF1 baseline JPEGs into 32bpp XRGB pixel buffers.
 * Uses two-pass O(N^2) IDCT in single-precision float (FPU enabled
 * at boot).  Handles 1-component grayscale and 3-component YCbCr
 * with sub-samplings 1x1, 2x1, 1x2, 2x2.  Restart markers are
 * honoured; progressive, arithmetic, 12-bit, and CMYK are rejected.
*/

#include "jpeg.h"
#include "memory.h"
#include "libm.h"

/* bit reader (MSB-first, byte stuffing) */

typedef struct {
    const uint8_t *src;
    uint32_t       len;
    uint32_t       pos;
    uint32_t       buf;
    int            buf_bits;
    uint8_t        marker;     /* if a marker was hit while feeding bits */
    int            eof;
} jp_bits_t;

static int jp_get_bit(jp_bits_t *s) {
    if (s->buf_bits == 0) {
        if (s->eof || s->pos >= s->len) { s->eof = 1; return 0; }
        uint8_t b = s->src[s->pos++];
        if (b == 0xFF) {
            if (s->pos >= s->len) { s->eof = 1; return 0; }
            uint8_t nx = s->src[s->pos++];
            if (nx != 0x00) {
                s->marker = nx;
                s->eof = 1;
                return 0;
            }
        }
        s->buf = (s->buf << 8) | b;
        s->buf_bits += 8;
    }
    s->buf_bits--;
    return (int)((s->buf >> (uint32_t)s->buf_bits) & 1u);
}

static int jp_get_bits(jp_bits_t *s, int n) {
    int v = 0;
    int i;
    for (i = 0; i < n; i++) {
        v = (v << 1) | jp_get_bit(s);
    }
    return v;
}

static int jp_extend(int v, int n) {
    if (n == 0) return 0;
    int vt = 1 << (n - 1);
    if (v < vt) v -= (1 << n) - 1;
    return v;
}

/* Huffman table (canonical) */

typedef struct {
    int16_t count[17];
    int16_t symbol[256];
    int     ok;
} jp_huff_t;

static int jp_huff_build(jp_huff_t *h, const uint8_t *bits16,
                         const uint8_t *vals, int nvals) {
    int i, len;
    int16_t offs[17];
    for (i = 0; i < 17; i++) h->count[i] = 0;
    for (len = 1; len <= 16; len++) {
        h->count[len] = (int16_t)bits16[len - 1];
    }
    int total = 0;
    for (len = 1; len <= 16; len++) total += (int)h->count[len];
    if (total != nvals || total > 256) { h->ok = 0; return -1; }
    offs[1] = 0;
    for (len = 1; len < 16; len++) {
        offs[len + 1] = (int16_t)(offs[len] + h->count[len]);
    }
    int idx = 0;
    for (len = 1; len <= 16; len++) {
        int c = (int)h->count[len];
        for (i = 0; i < c; i++) {
            h->symbol[offs[len]++] = (int16_t)vals[idx++];
        }
    }
    h->ok = 1;
    return 0;
}

static int jp_huff_decode(jp_bits_t *s, const jp_huff_t *h) {
    int code = 0, first = 0, idx = 0, len, count;
    for (len = 1; len <= 16; len++) {
        code = (code << 1) | jp_get_bit(s);
        if (s->eof) return -1;
        count = (int)h->count[len];
        if (code < first + count) {
            return (int)h->symbol[idx + (code - first)];
        }
        idx += count;
        first = (first + count) << 1;
    }
    return -1;
}

/* IDCT (two-pass O(N^2) using float cos table) */

static const uint8_t jp_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* IDCT basis-function table = 0.5 * Cu * cos((2j+1)u * pi/16). Hardcoded
 * to avoid depending on the kernel's runtime cosf() (which uses the FPU
 * fcos opcode and can return imprecise values from a cold boot context
 * before SSE/FPU stabilises). Values match Python's math.cos to single
 * precision; matches Blink's libjpeg-turbo equivalent jpeg_idct_islow
 * scaled-cosine constants.*/
static const float jp_cos_tbl[8][8] = {
    {  0.35355339f,  0.35355339f,  0.35355339f,  0.35355339f,  0.35355339f,  0.35355339f,  0.35355339f,  0.35355339f },
    {  0.49039264f,  0.41573481f,  0.27778512f,  0.09754516f, -0.09754516f, -0.27778512f, -0.41573481f, -0.49039264f },
    {  0.46193977f,  0.19134172f, -0.19134172f, -0.46193977f, -0.46193977f, -0.19134172f,  0.19134172f,  0.46193977f },
    {  0.41573481f, -0.09754516f, -0.49039264f, -0.27778512f,  0.27778512f,  0.49039264f,  0.09754516f, -0.41573481f },
    {  0.35355339f, -0.35355339f, -0.35355339f,  0.35355339f,  0.35355339f, -0.35355339f, -0.35355339f,  0.35355339f },
    {  0.27778512f, -0.49039264f,  0.09754516f,  0.41573481f, -0.41573481f, -0.09754516f,  0.49039264f, -0.27778512f },
    {  0.19134172f, -0.46193977f,  0.46193977f, -0.19134172f, -0.19134172f,  0.46193977f, -0.46193977f,  0.19134172f },
    {  0.09754516f, -0.27778512f,  0.41573481f, -0.49039264f,  0.49039264f, -0.41573481f,  0.27778512f, -0.09754516f },
};

static void jp_idct(const int16_t in[64], uint8_t out[64]) {
    float temp[64];
    int u, v, i, j;
    for (u = 0; u < 8; u++) {
        for (j = 0; j < 8; j++) {
            float sum = 0.0f;
            for (v = 0; v < 8; v++) {
                sum += (float)in[u * 8 + v] * jp_cos_tbl[v][j];
            }
            temp[u * 8 + j] = sum;
        }
    }
    for (j = 0; j < 8; j++) {
        for (i = 0; i < 8; i++) {
            float sum = 0.0f;
            for (u = 0; u < 8; u++) {
                sum += temp[u * 8 + j] * jp_cos_tbl[u][i];
            }
            int val = (int)(sum + 128.5f);
            if (val < 0)   val = 0;
            if (val > 255) val = 255;
            out[i * 8 + j] = (uint8_t)val;
        }
    }
}

/* decoder state */

typedef struct {
    uint8_t hsamp, vsamp;
    uint8_t qt_id;
    uint8_t dc_id, ac_id;
} jp_comp_t;

typedef struct {
    int       width, height;
    int       ncomp;
    jp_comp_t comp[3];
    int       max_h, max_v;
    int       restart_interval;
    int       restart_count;
    int       dc_pred[3];

    int16_t   qt[4][64];
    int       qt_present[4];
    jp_huff_t hdc[4];
    jp_huff_t hac[4];

    jp_bits_t bits;
} jp_state_t;

/* segment parsing */

static int jp_get_u16(const uint8_t *p) {
    return ((int)p[0] << 8) | (int)p[1];
}

static int jp_parse_dqt(jp_state_t *s, const uint8_t *seg, int len) {
    int p = 0;
    while (p < len) {
        if (p + 1 > len) return JPEG_EFORMAT;
        uint8_t pq_tq = seg[p++];
        int pq = (pq_tq >> 4) & 0xF;
        int tq = pq_tq & 0xF;
        if (tq > 3) return JPEG_EFORMAT;
        if (pq != 0) return JPEG_EFORMAT;  /* 8-bit only */
        if (p + 64 > len) return JPEG_EFORMAT;
        int i;
        for (i = 0; i < 64; i++) s->qt[tq][i] = (int16_t)seg[p + i];
        p += 64;
        s->qt_present[tq] = 1;
    }
    return JPEG_OK;
}

static int jp_parse_dht(jp_state_t *s, const uint8_t *seg, int len) {
    int p = 0;
    while (p < len) {
        if (p + 17 > len) return JPEG_EFORMAT;
        uint8_t tc_th = seg[p++];
        int tc = (tc_th >> 4) & 0xF;
        int th = tc_th & 0xF;
        if (tc > 1 || th > 3) return JPEG_EFORMAT;
        const uint8_t *bits = seg + p;
        p += 16;
        int nvals = 0;
        int i;
        for (i = 0; i < 16; i++) nvals += bits[i];
        if (nvals > 256 || p + nvals > len) return JPEG_EFORMAT;
        const uint8_t *vals = seg + p;
        p += nvals;
        jp_huff_t *h = (tc == 0) ? &s->hdc[th] : &s->hac[th];
        if (jp_huff_build(h, bits, vals, nvals) != 0) return JPEG_EFORMAT;
    }
    return JPEG_OK;
}

static int jp_parse_sof(jp_state_t *s, const uint8_t *seg, int len) {
    if (len < 6) return JPEG_EFORMAT;
    if (seg[0] != 8) return JPEG_EFORMAT;  /* sample precision */
    s->height = jp_get_u16(seg + 1);
    s->width  = jp_get_u16(seg + 3);
    s->ncomp  = seg[5];
    if (s->ncomp != 1 && s->ncomp != 3) return JPEG_EFORMAT;
    if (len < 6 + s->ncomp * 3) return JPEG_EFORMAT;
    if (s->width <= 0 || s->height <= 0 ||
        s->width > 4096 || s->height > 4096) return JPEG_EFORMAT;
    int i;
    s->max_h = 0;
    s->max_v = 0;
    for (i = 0; i < s->ncomp; i++) {
        const uint8_t *cp = seg + 6 + i * 3;
        /* cp[0] = component id (ignored after mapping) */
        s->comp[i].hsamp = (uint8_t)((cp[1] >> 4) & 0xF);
        s->comp[i].vsamp = (uint8_t)(cp[1] & 0xF);
        s->comp[i].qt_id = cp[2];
        if (s->comp[i].hsamp == 0 || s->comp[i].hsamp > 2) return JPEG_EFORMAT;
        if (s->comp[i].vsamp == 0 || s->comp[i].vsamp > 2) return JPEG_EFORMAT;
        if (s->comp[i].qt_id > 3) return JPEG_EFORMAT;
        if ((int)s->comp[i].hsamp > s->max_h) s->max_h = s->comp[i].hsamp;
        if ((int)s->comp[i].vsamp > s->max_v) s->max_v = s->comp[i].vsamp;
    }
    return JPEG_OK;
}

static int jp_parse_sos(jp_state_t *s, const uint8_t *seg, int len) {
    if (len < 1) return JPEG_EFORMAT;
    int n = seg[0];
    if (n != s->ncomp) return JPEG_EFORMAT;
    if (len < 1 + n * 2 + 3) return JPEG_EFORMAT;
    int i;
    for (i = 0; i < n; i++) {
        const uint8_t *cp = seg + 1 + i * 2;
        /* assume scan components are in same order as frame */
        s->comp[i].dc_id = (uint8_t)((cp[1] >> 4) & 0xF);
        s->comp[i].ac_id = (uint8_t)(cp[1] & 0xF);
        if (s->comp[i].dc_id > 3 || s->comp[i].ac_id > 3) return JPEG_EFORMAT;
    }
    /* Ss, Se, Ah/Al follow but are fixed for baseline; ignore */
    return JPEG_OK;
}

/* block decode */

static int jp_decode_block(jp_state_t *s, int comp_idx, uint8_t out[64]) {
    int16_t coef[64];
    int i;
    for (i = 0; i < 64; i++) coef[i] = 0;

    int qt_id = s->comp[comp_idx].qt_id;
    int dc_id = s->comp[comp_idx].dc_id;
    int ac_id = s->comp[comp_idx].ac_id;
    if (!s->qt_present[qt_id]) return JPEG_ESTREAM;
    if (!s->hdc[dc_id].ok || !s->hac[ac_id].ok) return JPEG_ESTREAM;
    const int16_t *qt = s->qt[qt_id];

    int t = jp_huff_decode(&s->bits, &s->hdc[dc_id]);
    if (t < 0 || t > 11) return JPEG_ESTREAM;
    int diff = t > 0 ? jp_extend(jp_get_bits(&s->bits, t), t) : 0;
    s->dc_pred[comp_idx] += diff;
    coef[0] = (int16_t)(s->dc_pred[comp_idx] * (int)qt[0]);

    int k = 1;
    while (k < 64) {
        int sym = jp_huff_decode(&s->bits, &s->hac[ac_id]);
        if (sym < 0) return JPEG_ESTREAM;
        int run = (sym >> 4) & 0xF;
        int sz  = sym & 0xF;
        if (sz == 0) {
            if (run == 15) { k += 16; continue; }
            break;  /* EOB */
        }
        k += run;
        if (k >= 64) return JPEG_ESTREAM;
        int v = jp_extend(jp_get_bits(&s->bits, sz), sz);
        coef[jp_zigzag[k]] = (int16_t)(v * (int)qt[k]);
        k++;
    }
    jp_idct(coef, out);
    return JPEG_OK;
}

static void jp_reset_dc(jp_state_t *s) {
    s->dc_pred[0] = 0;
    s->dc_pred[1] = 0;
    s->dc_pred[2] = 0;
}

/* color conversion */

static uint32_t jp_yuv_xrgb(int Y, int Cb, int Cr) {
    Cb -= 128;
    Cr -= 128;
    int R = Y + ((91881  * Cr) >> 16);
    int G = Y - ((22554  * Cb + 46802 * Cr) >> 16);
    int B = Y + ((116130 * Cb) >> 16);
    if (R < 0)   R = 0;
    if (R > 255) R = 255;
    if (G < 0)   G = 0;
    if (G > 255) G = 255;
    if (B < 0)   B = 0;
    if (B > 255) B = 255;
    /* JPEG has no alpha channel; emit fully opaque so the alpha-aware
     * blit path in gfx2d_image_draw_scaled doesn't treat the pixels as
     * transparent and skip them.*/
    return 0xFF000000u | ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
}

/* top-level decode */

int jpeg_decode_mem(const uint8_t *data, uint32_t len,
                    uint32_t **out_pixels, int *out_w, int *out_h) {
    if (!data || len < 4 || !out_pixels || !out_w || !out_h) return JPEG_EINVAL;
    if (data[0] != 0xFF || data[1] != 0xD8) return JPEG_EINVAL;

    jp_state_t st;
    int i;
    for (i = 0; i < 4; i++) {
        st.qt_present[i] = 0;
        st.hdc[i].ok = 0;
        st.hac[i].ok = 0;
    }
    st.restart_interval = 0;
    st.restart_count    = 0;
    st.ncomp = 0;

    uint32_t pos = 2;
    int      sos_seen = 0;

    /* Parse all metadata segments up to SOS */
    while (pos + 2 <= len && !sos_seen) {
        if (data[pos] != 0xFF) return JPEG_EFORMAT;
        /* skip 0xFF padding */
        while (pos < len && data[pos] == 0xFF) pos++;
        if (pos >= len) return JPEG_EFORMAT;
        uint8_t marker = data[pos++];
        if (marker == 0x00 || marker == 0xFF) return JPEG_EFORMAT;
        if (marker >= 0xD0 && marker <= 0xD7) continue; /* RSTn standalone */
        if (marker == 0xD9) return JPEG_EFORMAT;        /* EOI before SOS */
        if (marker == 0xD8) continue;                   /* extra SOI? */
        if (pos + 2 > len) return JPEG_EFORMAT;
        int seglen = jp_get_u16(data + pos);
        if (seglen < 2 || pos + (uint32_t)seglen > len) return JPEG_EFORMAT;
        const uint8_t *seg = data + pos + 2;
        int sl = seglen - 2;
        pos += (uint32_t)seglen;

        if (marker == 0xDB) {
            int rc = jp_parse_dqt(&st, seg, sl);
            if (rc != JPEG_OK) return rc;
        } else if (marker == 0xC4) {
            int rc = jp_parse_dht(&st, seg, sl);
            if (rc != JPEG_OK) return rc;
        } else if (marker == 0xC0 || marker == 0xC1) {
            int rc = jp_parse_sof(&st, seg, sl);
            if (rc != JPEG_OK) return rc;
        } else if (marker == 0xDD) {
            if (sl != 2) return JPEG_EFORMAT;
            st.restart_interval = jp_get_u16(seg);
        } else if (marker == 0xDA) {
            int rc = jp_parse_sos(&st, seg, sl);
            if (rc != JPEG_OK) return rc;
            sos_seen = 1;
        } else if (marker == 0xC2 || marker == 0xC3 ||
                   (marker >= 0xC5 && marker <= 0xCF && marker != 0xC8)) {
            return JPEG_EFORMAT;  /* progressive / lossless / arithmetic */
        }
        /* APPn (E0-EF), COM (FE), and others: ignore */
    }

    if (!sos_seen || st.ncomp == 0) return JPEG_EFORMAT;

    /* Output buffer */
    int W = st.width, H = st.height;
    uint32_t *pixels = (uint32_t *)kmalloc((uint32_t)W * (uint32_t)H * 4u);
    if (!pixels) return JPEG_ENOMEM;

    /* Entropy-coded scan */
    st.bits.src = data;
    st.bits.len = len;
    st.bits.pos = pos;
    st.bits.buf = 0;
    st.bits.buf_bits = 0;
    st.bits.marker = 0;
    st.bits.eof = 0;

    int max_h = st.max_h, max_v = st.max_v;
    int mcu_w = max_h * 8;
    int mcu_h = max_v * 8;
    int mcus_x = (W + mcu_w - 1) / mcu_w;
    int mcus_y = (H + mcu_h - 1) / mcu_h;

    jp_reset_dc(&st);
    int restart_left = st.restart_interval;

    /* Decoded sample blocks: blocks[c][bi][64] where bi < hsamp_c * vsamp_c <= 4 */
    uint8_t blocks[3][4][64];

    int my, mx;
    for (my = 0; my < mcus_y; my++) {
        for (mx = 0; mx < mcus_x; mx++) {
            int c, by, bx;
            for (c = 0; c < st.ncomp; c++) {
                int hc = st.comp[c].hsamp;
                int vc = st.comp[c].vsamp;
                for (by = 0; by < vc; by++) {
                    for (bx = 0; bx < hc; bx++) {
                        int bi = by * hc + bx;
                        int rc = jp_decode_block(&st, c, blocks[c][bi]);
                        if (rc != JPEG_OK || st.bits.eof) {
                            kfree(pixels);
                            return JPEG_ESTREAM;
                        }
                    }
                }
            }

            /* Write MCU pixels */
            int i_, j_;
            for (i_ = 0; i_ < mcu_h; i_++) {
                int oy = my * mcu_h + i_;
                if (oy >= H) break;
                for (j_ = 0; j_ < mcu_w; j_++) {
                    int ox = mx * mcu_w + j_;
                    if (ox >= W) break;

                    /* Y */
                    int y_row = (i_ * (int)st.comp[0].vsamp) / max_v;
                    int y_col = (j_ * (int)st.comp[0].hsamp) / max_h;
                    int y_bi = (y_row / 8) * (int)st.comp[0].hsamp + (y_col / 8);
                    int Y = (int)blocks[0][y_bi][(y_row & 7) * 8 + (y_col & 7)];

                    uint32_t px;
                    if (st.ncomp == 1) {
                        uint32_t g = (uint32_t)Y;
                        px = 0xFF000000u | (g << 16) | (g << 8) | g;
                    } else {
                        int cb_row = (i_ * (int)st.comp[1].vsamp) / max_v;
                        int cb_col = (j_ * (int)st.comp[1].hsamp) / max_h;
                        int cb_bi = (cb_row / 8) * (int)st.comp[1].hsamp + (cb_col / 8);
                        int Cb = (int)blocks[1][cb_bi][(cb_row & 7) * 8 + (cb_col & 7)];

                        int cr_row = (i_ * (int)st.comp[2].vsamp) / max_v;
                        int cr_col = (j_ * (int)st.comp[2].hsamp) / max_h;
                        int cr_bi = (cr_row / 8) * (int)st.comp[2].hsamp + (cr_col / 8);
                        int Cr = (int)blocks[2][cr_bi][(cr_row & 7) * 8 + (cr_col & 7)];

                        px = jp_yuv_xrgb(Y, Cb, Cr);
                    }
                    pixels[(uint32_t)oy * (uint32_t)W + (uint32_t)ox] = px;
                }
            }

            /* restart marker handling */
            if (st.restart_interval > 0) {
                restart_left--;
                if (restart_left == 0) {
                    /* skip to next byte boundary, expect FFD0..D7 */
                    st.bits.buf = 0;
                    st.bits.buf_bits = 0;
                    if (st.bits.marker >= 0xD0 && st.bits.marker <= 0xD7) {
                        st.bits.marker = 0;
                        st.bits.eof = 0;
                    } else {
                        /* search forward for RSTn */
                        while (st.bits.pos + 1 < len) {
                            if (data[st.bits.pos] == 0xFF) {
                                uint8_t nx = data[st.bits.pos + 1];
                                if (nx >= 0xD0 && nx <= 0xD7) {
                                    st.bits.pos += 2;
                                    st.bits.eof = 0;
                                    break;
                                }
                                if (nx == 0xD9) { /* EOI */
                                    st.bits.eof = 1;
                                    break;
                                }
                            }
                            st.bits.pos++;
                        }
                    }
                    jp_reset_dc(&st);
                    restart_left = st.restart_interval;
                }
            }
        }
    }

    *out_pixels = pixels;
    *out_w = W;
    *out_h = H;
    return JPEG_OK;
}
