/* RFC 6234 SHA-256 - pure portable implementation.
 *
 * Streaming API (init/update/final) plus a one-shot helper. Used as the
 * transcript hash, the HMAC hash, and the HKDF hash for TLS 1.3 with
 * SHA-256 cipher suites.*/

#include "sha256.h"

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t rotr32(uint32_t x, unsigned n) {
    return (uint32_t)((x >> n) | (x << (32u - n)));
}

#define CH(x,y,z)  (uint32_t)(((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (uint32_t)(((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x)   (uint32_t)(rotr32(x, 2) ^ rotr32(x, 13) ^ rotr32(x, 22))
#define BSIG1(x)   (uint32_t)(rotr32(x, 6) ^ rotr32(x, 11) ^ rotr32(x, 25))
#define SSIG0(x)   (uint32_t)(rotr32(x, 7) ^ rotr32(x, 18) ^ ((x) >> 3))
#define SSIG1(x)   (uint32_t)(rotr32(x, 17) ^ rotr32(x, 19) ^ ((x) >> 10))

static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)
         |  (uint32_t)p[3];
}

static void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >>  8) & 0xFFu);
    p[3] = (uint8_t)( v        & 0xFFu);
}

static void sha256_compress(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    unsigned t;

    for (t = 0; t < 16; t++) W[t] = load_be32(block + 4u * t);
    for (t = 16; t < 64; t++) {
        W[t] = (uint32_t)(SSIG1(W[t - 2]) + W[t - 7]
                        + SSIG0(W[t - 15]) + W[t - 16]);
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (t = 0; t < 64; t++) {
        uint32_t T1 = (uint32_t)(h + BSIG1(e) + CH(e, f, g) + K[t] + W[t]);
        uint32_t T2 = (uint32_t)(BSIG0(a) + MAJ(a, b, c));
        h = g;
        g = f;
        f = e;
        e = (uint32_t)(d + T1);
        d = c;
        c = b;
        b = a;
        a = (uint32_t)(T1 + T2);
    }

    state[0] = (uint32_t)(state[0] + a);
    state[1] = (uint32_t)(state[1] + b);
    state[2] = (uint32_t)(state[2] + c);
    state[3] = (uint32_t)(state[3] + d);
    state[4] = (uint32_t)(state[4] + e);
    state[5] = (uint32_t)(state[5] + f);
    state[6] = (uint32_t)(state[6] + g);
    state[7] = (uint32_t)(state[7] + h);
}

void sha256_init(sha256_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bitlen   = 0;
    ctx->buflen   = 0;
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    uint32_t off = 0;
    ctx->bitlen = (uint64_t)(ctx->bitlen + ((uint64_t)len << 3));

    if (ctx->buflen > 0) {
        uint32_t need = SHA256_BLOCK_SIZE - ctx->buflen;
        uint32_t take = (len < need) ? len : need;
        uint32_t i;
        for (i = 0; i < take; i++) {
            ctx->buffer[ctx->buflen + i] = data[i];
        }
        ctx->buflen += take;
        off = take;
        if (ctx->buflen == SHA256_BLOCK_SIZE) {
            sha256_compress(ctx->state, ctx->buffer);
            ctx->buflen = 0;
        }
    }

    while ((len - off) >= SHA256_BLOCK_SIZE) {
        sha256_compress(ctx->state, data + off);
        off += SHA256_BLOCK_SIZE;
    }

    {
        uint32_t i;
        for (i = off; i < len; i++) {
            ctx->buffer[ctx->buflen++] = data[i];
        }
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t out[SHA256_DIGEST_SIZE]) {
    uint64_t bitlen = ctx->bitlen;
    uint32_t i;

    /* Pad: 0x80 then zeros to length ≡ 56 (mod 64), then 8-byte length BE. */
    ctx->buffer[ctx->buflen++] = 0x80u;
    if (ctx->buflen > 56u) {
        while (ctx->buflen < SHA256_BLOCK_SIZE) ctx->buffer[ctx->buflen++] = 0;
        sha256_compress(ctx->state, ctx->buffer);
        ctx->buflen = 0;
    }
    while (ctx->buflen < 56u) ctx->buffer[ctx->buflen++] = 0;

    for (i = 0; i < 8; i++) {
        ctx->buffer[56u + i] =
            (uint8_t)((bitlen >> (8u * (7u - i))) & 0xFFu);
    }
    sha256_compress(ctx->state, ctx->buffer);

    for (i = 0; i < 8; i++) {
        store_be32(out + 4u * i, ctx->state[i]);
    }
}

void sha256(const uint8_t *data, uint32_t len,
            uint8_t out[SHA256_DIGEST_SIZE]) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}
