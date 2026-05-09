/* SHA-384 / SHA-512 (FIPS 180-4).  Plain C reference implementation,
 * no SIMD, no assembly.  64-bit arithmetic via uint64_t.  Intended for
 * X.509 cert chain validation; not perf-critical. */

#include "sha512.h"

static uint64_t rotr64(uint64_t x, uint32_t n) {
    return (x >> n) | (x << (64u - n));
}

#define Ch(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define Maj(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x)   (rotr64((x),28) ^ rotr64((x),34) ^ rotr64((x),39))
#define BSIG1(x)   (rotr64((x),14) ^ rotr64((x),18) ^ rotr64((x),41))
#define SSIG0(x)   (rotr64((x), 1) ^ rotr64((x), 8) ^ ((x) >> 7))
#define SSIG1(x)   (rotr64((x),19) ^ rotr64((x),61) ^ ((x) >> 6))

static const uint64_t K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

static uint64_t load_be64(const uint8_t *p) {
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; i++) {
        v = (v << 8) | (uint64_t)p[i];
    }
    return v;
}

static void store_be64(uint8_t *p, uint64_t v) {
    int i;
    for (i = 7; i >= 0; i--) {
        p[i] = (uint8_t)(v & 0xFFu);
        v >>= 8;
    }
}

static void sha512_compress(uint64_t state[8], const uint8_t block[128]) {
    uint64_t W[80];
    uint64_t a, b, c, d, e, f, g, h;
    uint64_t t1, t2;
    int t;

    for (t = 0; t < 16; t++) {
        W[t] = load_be64(block + t * 8);
    }
    for (t = 16; t < 80; t++) {
        W[t] = SSIG1(W[t - 2]) + W[t - 7] + SSIG0(W[t - 15]) + W[t - 16];
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (t = 0; t < 80; t++) {
        t1 = h + BSIG1(e) + Ch(e, f, g) + K[t] + W[t];
        t2 = BSIG0(a) + Maj(a, b, c);
        h = g; g = f; f = e;
        e = d + t1;
        d = c; c = b; b = a;
        a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha512_init(sha512_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667f3bcc908ULL;
    ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL;
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL;
    ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL;
    ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->bitlen_lo = 0;
    ctx->bitlen_hi = 0;
    ctx->buflen = 0;
}

void sha384_init(sha512_ctx_t *ctx) {
    ctx->state[0] = 0xcbbb9d5dc1059ed8ULL;
    ctx->state[1] = 0x629a292a367cd507ULL;
    ctx->state[2] = 0x9159015a3070dd17ULL;
    ctx->state[3] = 0x152fecd8f70e5939ULL;
    ctx->state[4] = 0x67332667ffc00b31ULL;
    ctx->state[5] = 0x8eb44a8768581511ULL;
    ctx->state[6] = 0xdb0c2e0d64f98fa7ULL;
    ctx->state[7] = 0x47b5481dbefa4fa4ULL;
    ctx->bitlen_lo = 0;
    ctx->bitlen_hi = 0;
    ctx->buflen = 0;
}

void sha512_update(sha512_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    uint32_t i = 0;
    uint64_t old_lo = ctx->bitlen_lo;
    ctx->bitlen_lo += (uint64_t)len * 8u;
    if (ctx->bitlen_lo < old_lo) ctx->bitlen_hi++;

    while (i < len) {
        uint32_t take = SHA512_BLOCK_SIZE - ctx->buflen;
        uint32_t avail = len - i;
        if (avail < take) take = avail;
        {
            uint32_t k;
            for (k = 0; k < take; k++) {
                ctx->buffer[ctx->buflen + k] = data[i + k];
            }
        }
        ctx->buflen += take;
        i += take;
        if (ctx->buflen == SHA512_BLOCK_SIZE) {
            sha512_compress(ctx->state, ctx->buffer);
            ctx->buflen = 0;
        }
    }
}

static void sha512_finalize_state(sha512_ctx_t *ctx) {
    uint8_t pad[SHA512_BLOCK_SIZE];
    uint32_t i;
    uint32_t padlen;
    uint64_t bl_lo = ctx->bitlen_lo;
    uint64_t bl_hi = ctx->bitlen_hi;

    /* Append 0x80 then zero-pad until length is 112 mod 128. */
    pad[0] = 0x80u;
    for (i = 1; i < SHA512_BLOCK_SIZE; i++) pad[i] = 0u;

    if (ctx->buflen < 112u) {
        padlen = 112u - ctx->buflen;
    } else {
        padlen = (SHA512_BLOCK_SIZE - ctx->buflen) + 112u;
    }
    sha512_update(ctx, pad, padlen);

    /* Now buflen is exactly 112; append 128-bit length big-endian. */
    {
        uint8_t lenbuf[16];
        store_be64(lenbuf, bl_hi);
        store_be64(lenbuf + 8, bl_lo);
        sha512_update(ctx, lenbuf, 16);
    }
    /* The two updates above leave buflen = 0 by construction. */
}

void sha512_final(sha512_ctx_t *ctx, uint8_t out[SHA512_DIGEST_SIZE]) {
    int i;
    sha512_finalize_state(ctx);
    for (i = 0; i < 8; i++) {
        store_be64(out + i * 8, ctx->state[i]);
    }
}

void sha384_final(sha512_ctx_t *ctx, uint8_t out[SHA384_DIGEST_SIZE]) {
    int i;
    sha512_finalize_state(ctx);
    for (i = 0; i < 6; i++) {
        store_be64(out + i * 8, ctx->state[i]);
    }
}

void sha512(const uint8_t *data, uint32_t len, uint8_t out[SHA512_DIGEST_SIZE]) {
    sha512_ctx_t ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, data, len);
    sha512_final(&ctx, out);
}

void sha384(const uint8_t *data, uint32_t len, uint8_t out[SHA384_DIGEST_SIZE]) {
    sha512_ctx_t ctx;
    sha384_init(&ctx);
    sha512_update(&ctx, data, len);
    sha384_final(&ctx, out);
}
