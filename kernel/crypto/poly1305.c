/* Poly1305 (RFC 8439). 32-bit limbs in radix 2^26, mostly following the
 * poly1305-donna 32-bit reference. Used for the ChaCha20-Poly1305 AEAD
 * MAC.*/

#include "poly1305.h"
#include "ct.h"

static uint32_t load_le32(const uint8_t *p) {
    return  (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void store_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

void poly1305_init(poly1305_ctx_t *ctx, const uint8_t key[32]) {
    /* r = key[0..15] with clamping (RFC 8439 §2.5.1). */
    ctx->r[0] = (load_le32(key + 0)        ) & 0x03ffffffu;
    ctx->r[1] = (load_le32(key + 3)  >> 2  ) & 0x03ffff03u;
    ctx->r[2] = (load_le32(key + 6)  >> 4  ) & 0x03ffc0ffu;
    ctx->r[3] = (load_le32(key + 9)  >> 6  ) & 0x03f03fffu;
    ctx->r[4] = (load_le32(key + 12) >> 8  ) & 0x000fffffu;

    /* s = key[16..31] (added once at the end). */
    ctx->pad[0] = load_le32(key + 16);
    ctx->pad[1] = load_le32(key + 20);
    ctx->pad[2] = load_le32(key + 24);
    ctx->pad[3] = load_le32(key + 28);

    ctx->h[0] = 0; ctx->h[1] = 0; ctx->h[2] = 0;
    ctx->h[3] = 0; ctx->h[4] = 0;
    ctx->buflen = 0;
    ctx->finished = 0;
}

/* Process one 16-byte block. If `last_partial` is nonzero, do not OR in
 * the high "1" bit (caller has already padded with explicit 1 byte).*/
static void poly1305_block(poly1305_ctx_t *ctx,
                           const uint8_t m[16], int last_partial) {
    uint32_t r0 = ctx->r[0], r1 = ctx->r[1], r2 = ctx->r[2],
             r3 = ctx->r[3], r4 = ctx->r[4];
    uint32_t s1 = (uint32_t)(r1 * 5u);
    uint32_t s2 = (uint32_t)(r2 * 5u);
    uint32_t s3 = (uint32_t)(r3 * 5u);
    uint32_t s4 = (uint32_t)(r4 * 5u);
    uint32_t h0 = ctx->h[0], h1 = ctx->h[1], h2 = ctx->h[2],
             h3 = ctx->h[3], h4 = ctx->h[4];
    uint32_t hibit = last_partial ? 0u : (1u << 24);

    h0 = (uint32_t)(h0 + (load_le32(m + 0)        & 0x03ffffffu));
    h1 = (uint32_t)(h1 + ((load_le32(m + 3) >> 2) & 0x03ffffffu));
    h2 = (uint32_t)(h2 + ((load_le32(m + 6) >> 4) & 0x03ffffffu));
    h3 = (uint32_t)(h3 + ((load_le32(m + 9) >> 6) & 0x03ffffffu));
    h4 = (uint32_t)(h4 + ((load_le32(m + 12) >> 8) | hibit));

    {
        uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4
                    + (uint64_t)h2 * s3 + (uint64_t)h3 * s2
                    + (uint64_t)h4 * s1;
        uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0
                    + (uint64_t)h2 * s4 + (uint64_t)h3 * s3
                    + (uint64_t)h4 * s2;
        uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1
                    + (uint64_t)h2 * r0 + (uint64_t)h3 * s4
                    + (uint64_t)h4 * s3;
        uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2
                    + (uint64_t)h2 * r1 + (uint64_t)h3 * r0
                    + (uint64_t)h4 * s4;
        uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3
                    + (uint64_t)h2 * r2 + (uint64_t)h3 * r1
                    + (uint64_t)h4 * r0;
        uint32_t c;

        c = (uint32_t)(d0 >> 26); h0 = (uint32_t)(d0 & 0x03ffffffu);
        d1 += c;
        c = (uint32_t)(d1 >> 26); h1 = (uint32_t)(d1 & 0x03ffffffu);
        d2 += c;
        c = (uint32_t)(d2 >> 26); h2 = (uint32_t)(d2 & 0x03ffffffu);
        d3 += c;
        c = (uint32_t)(d3 >> 26); h3 = (uint32_t)(d3 & 0x03ffffffu);
        d4 += c;
        c = (uint32_t)(d4 >> 26); h4 = (uint32_t)(d4 & 0x03ffffffu);
        h0 = (uint32_t)(h0 + c * 5u);
        c  = h0 >> 26; h0 &= 0x03ffffffu;
        h1 = (uint32_t)(h1 + c);
    }

    ctx->h[0] = h0; ctx->h[1] = h1; ctx->h[2] = h2;
    ctx->h[3] = h3; ctx->h[4] = h4;
}

void poly1305_update(poly1305_ctx_t *ctx,
                     const uint8_t *m, uint32_t len) {
    uint32_t off = 0;

    if (ctx->buflen > 0) {
        uint32_t need = 16u - ctx->buflen;
        uint32_t take = (len < need) ? len : need;
        uint32_t i;
        for (i = 0; i < take; i++) {
            ctx->buffer[ctx->buflen + i] = m[i];
        }
        ctx->buflen += take;
        off = take;
        if (ctx->buflen == 16u) {
            poly1305_block(ctx, ctx->buffer, 0);
            ctx->buflen = 0;
        }
    }

    while ((len - off) >= 16u) {
        poly1305_block(ctx, m + off, 0);
        off += 16u;
    }

    {
        uint32_t i;
        for (i = off; i < len; i++) {
            ctx->buffer[ctx->buflen++] = m[i];
        }
    }
}

void poly1305_final(poly1305_ctx_t *ctx, uint8_t tag[16]) {
    uint32_t h0, h1, h2, h3, h4;
    uint32_t g0, g1, g2, g3, g4;
    uint32_t mask, c;
    uint64_t f;

    /* Pad final partial block: append 1, then zeros, set last_partial=1. */
    if (ctx->buflen > 0u) {
        ctx->buffer[ctx->buflen++] = 1u;
        while (ctx->buflen < 16u) ctx->buffer[ctx->buflen++] = 0;
        poly1305_block(ctx, ctx->buffer, 1);
    }

    /* Final reduction (RFC 8439 §2.5.2). */
    h0 = ctx->h[0]; h1 = ctx->h[1]; h2 = ctx->h[2];
    h3 = ctx->h[3]; h4 = ctx->h[4];

    c = h1 >> 26; h1 &= 0x03ffffffu; h2 = (uint32_t)(h2 + c);
    c = h2 >> 26; h2 &= 0x03ffffffu; h3 = (uint32_t)(h3 + c);
    c = h3 >> 26; h3 &= 0x03ffffffu; h4 = (uint32_t)(h4 + c);
    c = h4 >> 26; h4 &= 0x03ffffffu; h0 = (uint32_t)(h0 + c * 5u);
    c = h0 >> 26; h0 &= 0x03ffffffu; h1 = (uint32_t)(h1 + c);

    /* g = h + 2^130 - 5; if g overflows (i.e. >= 2^130), use g; else h. */
    g0 = (uint32_t)(h0 + 5u);
    c  = g0 >> 26; g0 &= 0x03ffffffu;
    g1 = (uint32_t)(h1 + c); c = g1 >> 26; g1 &= 0x03ffffffu;
    g2 = (uint32_t)(h2 + c); c = g2 >> 26; g2 &= 0x03ffffffu;
    g3 = (uint32_t)(h3 + c); c = g3 >> 26; g3 &= 0x03ffffffu;
    g4 = (uint32_t)(h4 + c) - (uint32_t)(1u << 26);

    mask = (uint32_t)((g4 >> 31) - 1u);    /* 0xFFFFFFFFu if g4 >= 0 (use g) */
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (uint32_t)((h0 & mask) | g0);
    h1 = (uint32_t)((h1 & mask) | g1);
    h2 = (uint32_t)((h2 & mask) | g2);
    h3 = (uint32_t)((h3 & mask) | g3);
    h4 = (uint32_t)((h4 & mask) | g4);

    /* Re-pack into 4x32-bit and add s. */
    h0 = (h0      ) | (h1 << 26);
    h1 = (h1 >> 6 ) | (h2 << 20);
    h2 = (h2 >> 12) | (h3 << 14);
    h3 = (h3 >> 18) | (h4 <<  8);

    f = (uint64_t)h0 + ctx->pad[0];
    h0 = (uint32_t)f;
    f  = (uint64_t)h1 + ctx->pad[1] + (f >> 32);
    h1 = (uint32_t)f;
    f  = (uint64_t)h2 + ctx->pad[2] + (f >> 32);
    h2 = (uint32_t)f;
    f  = (uint64_t)h3 + ctx->pad[3] + (f >> 32);
    h3 = (uint32_t)f;

    store_le32(tag + 0,  h0);
    store_le32(tag + 4,  h1);
    store_le32(tag + 8,  h2);
    store_le32(tag + 12, h3);

    ctx->finished = 1;
}

void poly1305_auth(uint8_t tag[16],
                   const uint8_t *m, uint32_t len,
                   const uint8_t key[32]) {
    poly1305_ctx_t ctx;
    poly1305_init(&ctx, key);
    poly1305_update(&ctx, m, len);
    poly1305_final(&ctx, tag);
    ct_wipe(&ctx, sizeof(ctx));
}

int poly1305_verify(const uint8_t a[16], const uint8_t b[16]) {
    return (ct_memcmp(a, b, 16u) == 0) ? 1 : 0;
}
