/**
 * png.c - Minimal PNG decoder for cupid-os
 *
 * Decodes a PNG image held in memory into a 32bpp XRGB buffer.
 * Supports 8-bit color types 0/2/3/6 (gray, RGB, palette, RGBA),
 * filters None/Sub/Up/Average/Paeth, non-interlaced only.
 *
 * The DEFLATE/inflate implementation is hand-rolled per RFC 1951;
 * the zlib wrapper (RFC 1950) header bytes are skipped and the
 * trailing Adler-32 is not verified.  CRC32 on PNG chunks is also
 * skipped — corrupt files surface as inflate errors instead.
 */

#include "png.h"
#include "memory.h"

/* ----- bit reader / Huffman / inflate ---------------------------------- */

typedef struct {
    const uint8_t *src;
    uint32_t       src_len;
    uint32_t       src_pos;
    uint32_t       bit_buf;
    int            bit_count;
    uint8_t       *out;
    uint32_t       out_len;
    uint32_t       out_pos;
    int            err;
} pi_state_t;

typedef struct {
    int16_t count[16];
    int16_t symbol[288];
} pi_huff_t;

static int pi_get_bits(pi_state_t *s, int n) {
    while (s->bit_count < n) {
        if (s->src_pos >= s->src_len) {
            s->err = PNG_EINFLATE;
            return 0;
        }
        s->bit_buf |= ((uint32_t)s->src[s->src_pos]) << (uint32_t)s->bit_count;
        s->src_pos++;
        s->bit_count += 8;
    }
    uint32_t mask = (n == 0) ? 0u : ((1u << (uint32_t)n) - 1u);
    int v = (int)(s->bit_buf & mask);
    s->bit_buf >>= (uint32_t)n;
    s->bit_count -= n;
    return v;
}

static int pi_build_huff(pi_huff_t *h, const uint8_t *lens, int n) {
    int i, len;
    int16_t offs[16];
    for (i = 0; i < 16; i++) h->count[i] = 0;
    for (i = 0; i < n; i++) {
        if (lens[i] > 15) return -1;
        h->count[lens[i]]++;
    }
    if ((int)h->count[0] == n) return 0;
    int left = 1;
    for (len = 1; len <= 15; len++) {
        left <<= 1;
        left -= (int)h->count[len];
        if (left < 0) return -1;
    }
    offs[1] = 0;
    for (len = 1; len < 15; len++) {
        offs[len + 1] = (int16_t)(offs[len] + h->count[len]);
    }
    for (i = 0; i < n; i++) {
        if (lens[i] != 0) {
            h->symbol[offs[lens[i]]] = (int16_t)i;
            offs[lens[i]]++;
        }
    }
    return 0;
}

static int pi_decode_huff(pi_state_t *s, const pi_huff_t *h) {
    int code = 0, first = 0, idx = 0;
    int len, count;
    for (len = 1; len <= 15; len++) {
        code |= pi_get_bits(s, 1);
        count = (int)h->count[len];
        if (code - count < first) {
            return (int)h->symbol[idx + (code - first)];
        }
        idx += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    s->err = PNG_EINFLATE;
    return -1;
}

/* RFC 1951 length and distance tables */
static const uint16_t pi_length_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,
    67,83,99,115,131,163,195,227,258
};
static const uint8_t pi_length_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t pi_dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t pi_dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
static const uint8_t pi_clen_order[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

static void pi_build_fixed(pi_huff_t *llh, pi_huff_t *dh) {
    uint8_t lens[288];
    int i;
    for (i = 0; i < 144; i++)   lens[i] = 8;
    for (i = 144; i < 256; i++) lens[i] = 9;
    for (i = 256; i < 280; i++) lens[i] = 7;
    for (i = 280; i < 288; i++) lens[i] = 8;
    (void)pi_build_huff(llh, lens, 288);
    for (i = 0; i < 30; i++) lens[i] = 5;
    (void)pi_build_huff(dh, lens, 30);
}

static int pi_inflate_stored(pi_state_t *s) {
    s->bit_buf = 0;
    s->bit_count = 0;
    if (s->src_pos + 4 > s->src_len) { s->err = PNG_EINFLATE; return -1; }
    uint16_t len  = (uint16_t)(s->src[s->src_pos] |
                               ((uint16_t)s->src[s->src_pos + 1] << 8));
    uint16_t nlen = (uint16_t)(s->src[s->src_pos + 2] |
                               ((uint16_t)s->src[s->src_pos + 3] << 8));
    s->src_pos += 4;
    if ((uint16_t)(len ^ 0xFFFF) != nlen) { s->err = PNG_EINFLATE; return -1; }
    if (s->src_pos + len > s->src_len)    { s->err = PNG_EINFLATE; return -1; }
    if (s->out_pos + len > s->out_len)    { s->err = PNG_EINFLATE; return -1; }
    uint32_t i;
    for (i = 0; i < (uint32_t)len; i++) {
        s->out[s->out_pos + i] = s->src[s->src_pos + i];
    }
    s->src_pos += (uint32_t)len;
    s->out_pos += (uint32_t)len;
    return 0;
}

static int pi_inflate_block(pi_state_t *s,
                            const pi_huff_t *llh, const pi_huff_t *dh) {
    int sym;
    while (1) {
        sym = pi_decode_huff(s, llh);
        if (s->err) return -1;
        if (sym < 256) {
            if (s->out_pos >= s->out_len) { s->err = PNG_EINFLATE; return -1; }
            s->out[s->out_pos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            int lcode = sym - 257;
            if (lcode >= 29) { s->err = PNG_EINFLATE; return -1; }
            int len = (int)pi_length_base[lcode] +
                      pi_get_bits(s, (int)pi_length_extra[lcode]);
            int dcode = pi_decode_huff(s, dh);
            if (s->err) return -1;
            if (dcode >= 30) { s->err = PNG_EINFLATE; return -1; }
            int dist = (int)pi_dist_base[dcode] +
                       pi_get_bits(s, (int)pi_dist_extra[dcode]);
            if (dist <= 0 || (uint32_t)dist > s->out_pos) {
                s->err = PNG_EINFLATE; return -1;
            }
            if (s->out_pos + (uint32_t)len > s->out_len) {
                s->err = PNG_EINFLATE; return -1;
            }
            uint32_t from = s->out_pos - (uint32_t)dist;
            int j;
            for (j = 0; j < len; j++) {
                s->out[s->out_pos] = s->out[from + (uint32_t)j];
                s->out_pos++;
            }
        }
    }
}

static int pi_inflate_dynamic(pi_state_t *s, pi_huff_t *llh, pi_huff_t *dh) {
    int hlit  = pi_get_bits(s, 5) + 257;
    int hdist = pi_get_bits(s, 5) + 1;
    int hclen = pi_get_bits(s, 4) + 4;
    if (s->err) return -1;
    if (hlit > 286 || hdist > 30 || hclen > 19) {
        s->err = PNG_EINFLATE; return -1;
    }
    uint8_t clen_lens[19];
    int i;
    for (i = 0; i < 19; i++) clen_lens[i] = 0;
    for (i = 0; i < hclen; i++) {
        clen_lens[pi_clen_order[i]] = (uint8_t)pi_get_bits(s, 3);
    }
    if (s->err) return -1;
    pi_huff_t clen_h;
    if (pi_build_huff(&clen_h, clen_lens, 19) != 0) {
        s->err = PNG_EINFLATE; return -1;
    }
    uint8_t lens[286 + 30];
    for (i = 0; i < 286 + 30; i++) lens[i] = 0;
    int n = hlit + hdist;
    int idx = 0;
    while (idx < n) {
        int sym = pi_decode_huff(s, &clen_h);
        if (s->err) return -1;
        if (sym < 16) {
            lens[idx++] = (uint8_t)sym;
        } else if (sym == 16) {
            if (idx == 0) { s->err = PNG_EINFLATE; return -1; }
            int rep = pi_get_bits(s, 2) + 3;
            uint8_t v = lens[idx - 1];
            while (rep-- > 0 && idx < n) lens[idx++] = v;
        } else if (sym == 17) {
            int rep = pi_get_bits(s, 3) + 3;
            while (rep-- > 0 && idx < n) lens[idx++] = 0;
        } else if (sym == 18) {
            int rep = pi_get_bits(s, 7) + 11;
            while (rep-- > 0 && idx < n) lens[idx++] = 0;
        } else {
            s->err = PNG_EINFLATE; return -1;
        }
    }
    if (pi_build_huff(llh, lens, hlit) != 0) {
        s->err = PNG_EINFLATE; return -1;
    }
    if (pi_build_huff(dh, lens + hlit, hdist) != 0) {
        s->err = PNG_EINFLATE; return -1;
    }
    return 0;
}

static int pi_inflate(const uint8_t *src, uint32_t src_len,
                      uint8_t *out, uint32_t out_len) {
    pi_state_t s;
    s.src = src; s.src_len = src_len; s.src_pos = 0;
    s.bit_buf = 0; s.bit_count = 0;
    s.out = out; s.out_len = out_len; s.out_pos = 0;
    s.err = 0;

    int bfinal, btype;
    do {
        bfinal = pi_get_bits(&s, 1);
        btype  = pi_get_bits(&s, 2);
        if (s.err) return s.err;
        if (btype == 0) {
            if (pi_inflate_stored(&s) != 0) return s.err;
        } else if (btype == 1) {
            pi_huff_t llh, dh;
            pi_build_fixed(&llh, &dh);
            if (pi_inflate_block(&s, &llh, &dh) != 0) return s.err;
        } else if (btype == 2) {
            pi_huff_t llh, dh;
            if (pi_inflate_dynamic(&s, &llh, &dh) != 0) return s.err;
            if (pi_inflate_block(&s, &llh, &dh) != 0) return s.err;
        } else {
            return PNG_EINFLATE;
        }
    } while (!bfinal);
    return 0;
}

/* ----- PNG chunk parsing + defilter + colour conversion ----------------- */

typedef struct {
    int     width, height;
    uint8_t bit_depth;
    uint8_t color_type;
    uint8_t interlace;
    int     channels;
    int     bytes_per_pixel;
    int     row_bytes;
    uint8_t palette[256 * 3];
    int     palette_size;
    uint8_t trans[256];
    int     trans_size;
    uint8_t trans_color[6]; /* tRNS for color types 0/2 */
    int     trans_color_set;
} png_state_t;

static uint32_t pi_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static int pi_paeth(int a, int b, int c) {
    int p  = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc)             return b;
    return c;
}

static int pi_unfilter(uint8_t *raw, int row_bytes, int height, int bpp_b) {
    int row, x;
    int stride = row_bytes + 1;
    uint8_t *prev = NULL;
    for (row = 0; row < height; row++) {
        uint8_t *cur = raw + (uint32_t)row * (uint32_t)stride + 1;
        uint8_t f = raw[(uint32_t)row * (uint32_t)stride];
        if (f == 0) {
            /* none */
        } else if (f == 1) {
            for (x = 0; x < row_bytes; x++) {
                int left = (x >= bpp_b) ? cur[x - bpp_b] : 0;
                cur[x] = (uint8_t)(((int)cur[x] + left) & 0xFF);
            }
        } else if (f == 2) {
            for (x = 0; x < row_bytes; x++) {
                int up = prev ? prev[x] : 0;
                cur[x] = (uint8_t)(((int)cur[x] + up) & 0xFF);
            }
        } else if (f == 3) {
            for (x = 0; x < row_bytes; x++) {
                int left = (x >= bpp_b) ? cur[x - bpp_b] : 0;
                int up   = prev ? prev[x] : 0;
                cur[x] = (uint8_t)(((int)cur[x] + (left + up) / 2) & 0xFF);
            }
        } else if (f == 4) {
            for (x = 0; x < row_bytes; x++) {
                int left = (x >= bpp_b) ? cur[x - bpp_b] : 0;
                int up   = prev ? prev[x] : 0;
                int upleft = (prev && x >= bpp_b) ? prev[x - bpp_b] : 0;
                cur[x] = (uint8_t)(((int)cur[x] +
                                    pi_paeth(left, up, upleft)) & 0xFF);
            }
        } else {
            return PNG_EINFLATE;
        }
        prev = cur;
    }
    return 0;
}

static int pi_convert(const uint8_t *raw, png_state_t *st, uint32_t *out) {
    int row, col;
    int stride = st->row_bytes + 1;
    int w = st->width, h = st->height;
    if (st->color_type == 2) {
        for (row = 0; row < h; row++) {
            const uint8_t *p = raw + (uint32_t)row * (uint32_t)stride + 1;
            for (col = 0; col < w; col++) {
                uint32_t r = p[col * 3];
                uint32_t g = p[col * 3 + 1];
                uint32_t b = p[col * 3 + 2];
                uint32_t a = 255u;
                if (st->trans_color_set &&
                    p[col * 3] == st->trans_color[1] &&
                    p[col * 3 + 1] == st->trans_color[3] &&
                    p[col * 3 + 2] == st->trans_color[5]) {
                    a = 0u;
                }
                out[(uint32_t)row * (uint32_t)w + (uint32_t)col] =
                    (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    } else if (st->color_type == 6) {
        for (row = 0; row < h; row++) {
            const uint8_t *p = raw + (uint32_t)row * (uint32_t)stride + 1;
            for (col = 0; col < w; col++) {
                uint32_t r = p[col * 4];
                uint32_t g = p[col * 4 + 1];
                uint32_t b = p[col * 4 + 2];
                uint32_t a = p[col * 4 + 3];
                out[(uint32_t)row * (uint32_t)w + (uint32_t)col] =
                    (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    } else if (st->color_type == 0) {
        for (row = 0; row < h; row++) {
            const uint8_t *p = raw + (uint32_t)row * (uint32_t)stride + 1;
            for (col = 0; col < w; col++) {
                uint32_t v = p[col];
                uint32_t a = 255u;
                if (st->trans_color_set && v == st->trans_color[1]) a = 0u;
                out[(uint32_t)row * (uint32_t)w + (uint32_t)col] =
                    (a << 24) | (v << 16) | (v << 8) | v;
            }
        }
    } else if (st->color_type == 3) {
        for (row = 0; row < h; row++) {
            const uint8_t *p = raw + (uint32_t)row * (uint32_t)stride + 1;
            for (col = 0; col < w; col++) {
                int idx = p[col];
                if (idx >= st->palette_size) return PNG_EINFLATE;
                uint32_t r = st->palette[idx * 3];
                uint32_t g = st->palette[idx * 3 + 1];
                uint32_t b = st->palette[idx * 3 + 2];
                uint32_t a = (idx < st->trans_size)
                              ? (uint32_t)st->trans[idx] : 255u;
                out[(uint32_t)row * (uint32_t)w + (uint32_t)col] =
                    (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    } else {
        return PNG_EFORMAT;
    }
    return 0;
}

int png_decode_mem(const uint8_t *data, uint32_t len,
                   uint32_t **out_pixels, int *out_w, int *out_h) {
    static const uint8_t sig[8] =
        { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
    if (!data || len < 8 || !out_pixels || !out_w || !out_h) return PNG_EINVAL;
    int i;
    for (i = 0; i < 8; i++) if (data[i] != sig[i]) return PNG_EINVAL;

    png_state_t st;
    st.palette_size    = 0;
    st.trans_size      = 0;
    st.trans_color_set = 0;
    int got_ihdr = 0;

    uint8_t *idat_buf = NULL;
    uint32_t idat_cap = 0;
    uint32_t idat_len = 0;

    uint32_t pos = 8;
    while (pos + 8 <= len) {
        uint32_t clen = pi_be32(data + pos);
        pos += 4;
        const uint8_t *ctype = data + pos;
        pos += 4;
        if (clen > len || pos + clen + 4 > len) {
            if (idat_buf) kfree(idat_buf);
            return PNG_EINVAL;
        }
        const uint8_t *cdata = data + pos;
        pos += clen + 4;

        if (ctype[0]=='I' && ctype[1]=='H' && ctype[2]=='D' && ctype[3]=='R') {
            if (clen < 13) { if (idat_buf) kfree(idat_buf); return PNG_EINVAL; }
            st.width  = (int)pi_be32(cdata);
            st.height = (int)pi_be32(cdata + 4);
            st.bit_depth  = cdata[8];
            st.color_type = cdata[9];
            st.interlace  = cdata[12];
            if (st.interlace != 0)               { if (idat_buf) kfree(idat_buf); return PNG_EFORMAT; }
            if (st.bit_depth != 8)               { if (idat_buf) kfree(idat_buf); return PNG_EFORMAT; }
            if (st.width <= 0 || st.height <= 0 ||
                st.width > 4096 || st.height > 4096) {
                if (idat_buf) kfree(idat_buf);
                return PNG_EFORMAT;
            }
            switch (st.color_type) {
                case 0: st.channels = 1; break;
                case 2: st.channels = 3; break;
                case 3: st.channels = 1; break;
                case 6: st.channels = 4; break;
                default:
                    if (idat_buf) kfree(idat_buf);
                    return PNG_EFORMAT;
            }
            st.bytes_per_pixel = st.channels;
            st.row_bytes = st.width * st.channels;
            got_ihdr = 1;
        } else if (ctype[0]=='P' && ctype[1]=='L' && ctype[2]=='T' && ctype[3]=='E') {
            if (clen > 256u * 3u || clen % 3u != 0u) {
                if (idat_buf) kfree(idat_buf);
                return PNG_EINVAL;
            }
            uint32_t k;
            for (k = 0; k < clen; k++) st.palette[k] = cdata[k];
            st.palette_size = (int)(clen / 3u);
        } else if (ctype[0]=='t' && ctype[1]=='R' && ctype[2]=='N' && ctype[3]=='S') {
            if (st.color_type == 3) {
                if (clen > 256u) { if (idat_buf) kfree(idat_buf); return PNG_EINVAL; }
                uint32_t k;
                for (k = 0; k < clen; k++) st.trans[k] = cdata[k];
                st.trans_size = (int)clen;
            } else if (clen <= 6u) {
                uint32_t k;
                for (k = 0; k < clen; k++) st.trans_color[k] = cdata[k];
                st.trans_color_set = 1;
            }
        } else if (ctype[0]=='I' && ctype[1]=='D' && ctype[2]=='A' && ctype[3]=='T') {
            if (idat_len + clen > idat_cap) {
                uint32_t newcap = idat_cap == 0 ? 16384 : idat_cap * 2;
                while (newcap < idat_len + clen) newcap *= 2;
                uint8_t *nb = (uint8_t *)kmalloc(newcap);
                if (!nb) { if (idat_buf) kfree(idat_buf); return PNG_ENOMEM; }
                if (idat_buf) {
                    uint32_t k;
                    for (k = 0; k < idat_len; k++) nb[k] = idat_buf[k];
                    kfree(idat_buf);
                }
                idat_buf = nb;
                idat_cap = newcap;
            }
            uint32_t k;
            for (k = 0; k < clen; k++) idat_buf[idat_len + k] = cdata[k];
            idat_len += clen;
        } else if (ctype[0]=='I' && ctype[1]=='E' && ctype[2]=='N' && ctype[3]=='D') {
            break;
        }
        /* unknown chunks ignored */
    }

    if (!got_ihdr || !idat_buf) {
        if (idat_buf) kfree(idat_buf);
        return PNG_EINVAL;
    }
    if (idat_len < 6) { kfree(idat_buf); return PNG_EINFLATE; }

    /* skip 2-byte zlib header and ignore 4-byte trailing Adler-32 */
    uint32_t raw_len = (uint32_t)st.height * (uint32_t)(st.row_bytes + 1);
    uint8_t *raw = (uint8_t *)kmalloc(raw_len);
    if (!raw) { kfree(idat_buf); return PNG_ENOMEM; }

    int rc = pi_inflate(idat_buf + 2, idat_len - 6, raw, raw_len);
    kfree(idat_buf);
    if (rc != 0) { kfree(raw); return rc; }

    rc = pi_unfilter(raw, st.row_bytes, st.height, st.bytes_per_pixel);
    if (rc != 0) { kfree(raw); return rc; }

    uint32_t pcount = (uint32_t)st.width * (uint32_t)st.height;
    uint32_t *pixels = (uint32_t *)kmalloc(pcount * 4u);
    if (!pixels) { kfree(raw); return PNG_ENOMEM; }

    rc = pi_convert(raw, &st, pixels);
    kfree(raw);
    if (rc != 0) { kfree(pixels); return rc; }

    *out_pixels = pixels;
    *out_w      = st.width;
    *out_h      = st.height;
    return PNG_OK;
}
