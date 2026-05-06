/* RFC 8439 ChaCha20 block function and stream cipher.
 *
 * Used by the CSPRNG (kernel/tls/csprng.c) and by the AEAD construction
 * (kernel/tls/chacha20poly1305.c, added in a later phase). Pure portable
 * 32-bit C — no assembly, no SIMD. */

#include "chacha20.h"

static uint32_t rotl32(uint32_t x, unsigned n) {
    /* RFC 8439 only ever rotates by {7, 8, 12, 16} — n is never 0. */
    return (uint32_t)((x << n) | (x >> (32u - n)));
}

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

#define QR(a, b, c, d) do {                                  \
    a = (uint32_t)(a + b); d ^= a; d = rotl32(d, 16);        \
    c = (uint32_t)(c + d); b ^= c; b = rotl32(b, 12);        \
    a = (uint32_t)(a + b); d ^= a; d = rotl32(d,  8);        \
    c = (uint32_t)(c + d); b ^= c; b = rotl32(b,  7);        \
} while (0)

void chacha20_block(const uint8_t key[32], uint32_t counter,
                    const uint8_t nonce[12], uint8_t out[64]) {
    uint32_t s[16];
    uint32_t w[16];
    unsigned i;

    /* "expand 32-byte k" */
    s[0] = 0x61707865u;
    s[1] = 0x3320646eu;
    s[2] = 0x79622d32u;
    s[3] = 0x6b206574u;
    for (i = 0; i < 8; i++) s[4u + i] = load_le32(key + 4u * i);
    s[12] = counter;
    s[13] = load_le32(nonce + 0);
    s[14] = load_le32(nonce + 4);
    s[15] = load_le32(nonce + 8);

    for (i = 0; i < 16; i++) w[i] = s[i];

    for (i = 0; i < 10; i++) {
        QR(w[0], w[4], w[ 8], w[12]);
        QR(w[1], w[5], w[ 9], w[13]);
        QR(w[2], w[6], w[10], w[14]);
        QR(w[3], w[7], w[11], w[15]);
        QR(w[0], w[5], w[10], w[15]);
        QR(w[1], w[6], w[11], w[12]);
        QR(w[2], w[7], w[ 8], w[13]);
        QR(w[3], w[4], w[ 9], w[14]);
    }

    for (i = 0; i < 16; i++) {
        store_le32(out + 4u * i, (uint32_t)(w[i] + s[i]));
    }
}

void chacha20_xor(const uint8_t key[32], uint32_t counter,
                  const uint8_t nonce[12],
                  const uint8_t *in, uint8_t *out, uint32_t len) {
    uint8_t  block[64];
    uint32_t off = 0;
    while (off < len) {
        uint32_t n;
        uint32_t i;
        chacha20_block(key, counter, nonce, block);
        counter = (uint32_t)(counter + 1u);
        n = len - off;
        if (n > 64u) n = 64u;
        for (i = 0; i < n; i++) {
            out[off + i] = (uint8_t)(in[off + i] ^ block[i]);
        }
        off += n;
    }
}
