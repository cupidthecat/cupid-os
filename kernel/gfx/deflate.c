/* deflate.c - RFC 1951 raw DEFLATE decoder.
 *
 * Lifted from png.c. Hand-rolled bit reader + Huffman + inflate.
 * Static-block, fixed-block, and dynamic-block decoders all here.
 */

#include "deflate.h"

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
            s->err = KDEFLATE_ERR;
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
    s->err = KDEFLATE_ERR;
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
    if (s->src_pos + 4 > s->src_len) { s->err = KDEFLATE_ERR; return -1; }
    uint16_t len  = (uint16_t)(s->src[s->src_pos] |
                               ((uint16_t)s->src[s->src_pos + 1] << 8));
    uint16_t nlen = (uint16_t)(s->src[s->src_pos + 2] |
                               ((uint16_t)s->src[s->src_pos + 3] << 8));
    s->src_pos += 4;
    if ((uint16_t)(len ^ 0xFFFF) != nlen) { s->err = KDEFLATE_ERR; return -1; }
    if (s->src_pos + len > s->src_len)    { s->err = KDEFLATE_ERR; return -1; }
    if (s->out_pos + len > s->out_len)    { s->err = KDEFLATE_ERR; return -1; }
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
            if (s->out_pos >= s->out_len) { s->err = KDEFLATE_ERR; return -1; }
            s->out[s->out_pos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            int lcode = sym - 257;
            if (lcode >= 29) { s->err = KDEFLATE_ERR; return -1; }
            int len = (int)pi_length_base[lcode] +
                      pi_get_bits(s, (int)pi_length_extra[lcode]);
            int dcode = pi_decode_huff(s, dh);
            if (s->err) return -1;
            if (dcode >= 30) { s->err = KDEFLATE_ERR; return -1; }
            int dist = (int)pi_dist_base[dcode] +
                       pi_get_bits(s, (int)pi_dist_extra[dcode]);
            if (dist <= 0 || (uint32_t)dist > s->out_pos) {
                s->err = KDEFLATE_ERR; return -1;
            }
            if (s->out_pos + (uint32_t)len > s->out_len) {
                s->err = KDEFLATE_ERR; return -1;
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
        s->err = KDEFLATE_ERR; return -1;
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
        s->err = KDEFLATE_ERR; return -1;
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
            if (idx == 0) { s->err = KDEFLATE_ERR; return -1; }
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
            s->err = KDEFLATE_ERR; return -1;
        }
    }
    if (pi_build_huff(llh, lens, hlit) != 0) {
        s->err = KDEFLATE_ERR; return -1;
    }
    if (pi_build_huff(dh, lens + hlit, hdist) != 0) {
        s->err = KDEFLATE_ERR; return -1;
    }
    return 0;
}

int kdeflate_raw(const uint8_t *src, uint32_t src_len,
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
            return KDEFLATE_ERR;
        }
    } while (!bfinal);
    return KDEFLATE_OK;
}
