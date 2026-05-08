/* AES-128 (FIPS 197) - table-based implementation.
 *
 * Uses the canonical 256-byte S-box plus on-the-fly mixing in MixColumns.
 * Avoids the 4 KB T-tables variant; the trade-off is slightly slower
 * encryption but smaller code and identical cache footprint per round.
 *
 * Public surface: aes128_set_key + aes128_encrypt_block. Decryption is
 * not implemented - TLS GCM uses encrypt-only.
 */

#include "aes.h"

static const uint8_t SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t RCON[10] = {
    0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

static uint32_t rotw(uint32_t w) { return (w << 8) | (w >> 24); }

static uint32_t subw(uint32_t w) {
    return ((uint32_t)SBOX[(w >> 24) & 0xFFu] << 24) |
           ((uint32_t)SBOX[(w >> 16) & 0xFFu] << 16) |
           ((uint32_t)SBOX[(w >>  8) & 0xFFu] <<  8) |
           ((uint32_t)SBOX[ w        & 0xFFu]      );
}

static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static void store_be32(uint8_t *p, uint32_t w) {
    p[0] = (uint8_t)((w >> 24) & 0xFFu);
    p[1] = (uint8_t)((w >> 16) & 0xFFu);
    p[2] = (uint8_t)((w >>  8) & 0xFFu);
    p[3] = (uint8_t)( w        & 0xFFu);
}

void aes128_set_key(aes128_ctx_t *ctx, const uint8_t key[AES128_KEY_SIZE]) {
    uint32_t i;
    uint32_t *rk = ctx->rk;

    rk[0] = load_be32(&key[0]);
    rk[1] = load_be32(&key[4]);
    rk[2] = load_be32(&key[8]);
    rk[3] = load_be32(&key[12]);

    for (i = 4; i < 44; i++) {
        uint32_t t = rk[i - 1];
        if ((i & 3u) == 0u) {
            t = subw(rotw(t)) ^ ((uint32_t)RCON[(i >> 2) - 1] << 24);
        }
        rk[i] = rk[i - 4] ^ t;
    }
}

/* GF(2^8) multiplication by 2: shift left, XOR 0x1b if MSB was set. */
static uint8_t xtime(uint8_t b) {
    uint8_t hi = (uint8_t)((b >> 7) & 1u);
    return (uint8_t)((b << 1) ^ (uint8_t)(hi * 0x1bu));
}

void aes128_encrypt_block(const aes128_ctx_t *ctx,
                          const uint8_t in[AES128_BLOCK],
                          uint8_t out[AES128_BLOCK]) {
    uint8_t s[16];
    uint8_t t[16];
    uint32_t r;
    uint32_t i;
    const uint32_t *rk = ctx->rk;

    /* Initial AddRoundKey. */
    for (i = 0; i < 4; i++) {
        store_be32(&s[i * 4], load_be32(&in[i * 4]) ^ rk[i]);
    }

    /* 9 main rounds. */
    for (r = 1; r < AES128_NR; r++) {
        /* SubBytes + ShiftRows: row k shifted left by k. */
        t[0]  = SBOX[s[0]];
        t[1]  = SBOX[s[5]];
        t[2]  = SBOX[s[10]];
        t[3]  = SBOX[s[15]];
        t[4]  = SBOX[s[4]];
        t[5]  = SBOX[s[9]];
        t[6]  = SBOX[s[14]];
        t[7]  = SBOX[s[3]];
        t[8]  = SBOX[s[8]];
        t[9]  = SBOX[s[13]];
        t[10] = SBOX[s[2]];
        t[11] = SBOX[s[7]];
        t[12] = SBOX[s[12]];
        t[13] = SBOX[s[1]];
        t[14] = SBOX[s[6]];
        t[15] = SBOX[s[11]];

        /* MixColumns: c'[0] = 2*c[0] ^ 3*c[1] ^ c[2] ^ c[3] etc. */
        for (i = 0; i < 4; i++) {
            uint8_t a0 = t[i * 4 + 0];
            uint8_t a1 = t[i * 4 + 1];
            uint8_t a2 = t[i * 4 + 2];
            uint8_t a3 = t[i * 4 + 3];
            uint8_t x  = (uint8_t)(a0 ^ a1 ^ a2 ^ a3);
            s[i * 4 + 0] = (uint8_t)(a0 ^ x ^ xtime((uint8_t)(a0 ^ a1)));
            s[i * 4 + 1] = (uint8_t)(a1 ^ x ^ xtime((uint8_t)(a1 ^ a2)));
            s[i * 4 + 2] = (uint8_t)(a2 ^ x ^ xtime((uint8_t)(a2 ^ a3)));
            s[i * 4 + 3] = (uint8_t)(a3 ^ x ^ xtime((uint8_t)(a3 ^ a0)));
        }

        /* AddRoundKey. */
        for (i = 0; i < 4; i++) {
            store_be32(&s[i * 4], load_be32(&s[i * 4]) ^ rk[r * 4 + i]);
        }
    }

    /* Final round: SubBytes + ShiftRows + AddRoundKey (no MixColumns). */
    t[0]  = SBOX[s[0]];
    t[1]  = SBOX[s[5]];
    t[2]  = SBOX[s[10]];
    t[3]  = SBOX[s[15]];
    t[4]  = SBOX[s[4]];
    t[5]  = SBOX[s[9]];
    t[6]  = SBOX[s[14]];
    t[7]  = SBOX[s[3]];
    t[8]  = SBOX[s[8]];
    t[9]  = SBOX[s[13]];
    t[10] = SBOX[s[2]];
    t[11] = SBOX[s[7]];
    t[12] = SBOX[s[12]];
    t[13] = SBOX[s[1]];
    t[14] = SBOX[s[6]];
    t[15] = SBOX[s[11]];

    for (i = 0; i < 4; i++) {
        store_be32(&out[i * 4], load_be32(&t[i * 4]) ^ rk[AES128_NR * 4 + i]);
    }
}
