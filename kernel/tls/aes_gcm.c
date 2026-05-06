/* AES-128-GCM (NIST SP 800-38D / RFC 5116).
 *
 * GCM = CTR (encrypt) + GHASH (authenticate). For 12-byte nonces:
 *   J0   = nonce || 0x00000001
 *   C    = AES-CTR(K, inc32(J0), P)         [counter starts at J0+1]
 *   S    = GHASH(H, AAD || C || len64(AAD)||len64(C))   [bits, big-endian]
 *   tag  = S XOR AES_K(J0)
 *
 * GHASH multiplies in GF(2^128) under the polynomial
 *   p(x) = x^128 + x^7 + x^2 + x + 1
 * using the bit-by-bit shift-and-xor algorithm — slow but constant-time
 * and small.
 */

#include "aes_gcm.h"
#include "ct.h"

static void xor_block(uint8_t *dst, const uint8_t *a, const uint8_t *b) {
    uint32_t i;
    for (i = 0; i < 16u; i++) dst[i] = (uint8_t)(a[i] ^ b[i]);
}

/* GF(2^128) multiplication: x = x * y under the GCM polynomial.
 * Both inputs are 16-byte big-endian; the bit ordering follows NIST: the
 * MSB of byte 0 is the highest-degree coefficient. */
static void ghash_mul(uint8_t x[16], const uint8_t y[16]) {
    uint8_t z[16];
    uint8_t v[16];
    uint32_t i;
    int bit;

    for (i = 0; i < 16u; i++) z[i] = 0u;
    for (i = 0; i < 16u; i++) v[i] = x[i];

    for (i = 0; i < 16u; i++) {
        for (bit = 7; bit >= 0; bit--) {
            uint8_t mask = (uint8_t)(((y[i] >> (uint8_t)bit) & 1u) ? 0xFFu : 0x00u);
            uint32_t k;
            uint8_t lsb;
            uint8_t carry_mask;

            for (k = 0; k < 16u; k++) z[k] = (uint8_t)(z[k] ^ (v[k] & mask));

            /* v = v >> 1; if LSB of v was 1, v ^= R where R = 0xE1 || 0^120. */
            lsb = (uint8_t)(v[15] & 1u);
            for (k = 16u; k > 0u; k--) {
                uint32_t idx = k - 1u;
                uint8_t cur = v[idx];
                uint8_t prev_lsb = (idx == 0u) ? 0u : (uint8_t)(v[idx - 1u] & 1u);
                v[idx] = (uint8_t)((cur >> 1) | (uint8_t)(prev_lsb << 7));
            }
            carry_mask = (uint8_t)(lsb ? 0xFFu : 0x00u);
            v[0] = (uint8_t)(v[0] ^ (uint8_t)(0xE1u & carry_mask));
        }
    }

    for (i = 0; i < 16u; i++) x[i] = z[i];
}

/* Absorb `len` bytes into the running GHASH state. Caller is responsible
 * for any zero-padding semantics (we just iterate full 16-byte blocks
 * after copying short tails into a zero-filled scratch). */
static void ghash_update(uint8_t y[16], const uint8_t h[16],
                         const uint8_t *data, uint32_t len) {
    uint32_t off = 0;
    while (off < len) {
        uint8_t block[16];
        uint32_t i;
        uint32_t take = len - off;
        if (take > 16u) take = 16u;
        for (i = 0; i < 16u; i++) block[i] = 0u;
        for (i = 0; i < take; i++) block[i] = data[off + i];
        for (i = 0; i < 16u; i++) y[i] = (uint8_t)(y[i] ^ block[i]);
        ghash_mul(y, h);
        off += take;
    }
}

/* Increment the low 32 bits of a 16-byte counter (big-endian, NIST inc32). */
static void inc32(uint8_t ctr[16]) {
    uint32_t i;
    for (i = 16u; i > 12u; i--) {
        uint32_t idx = i - 1u;
        ctr[idx] = (uint8_t)(ctr[idx] + 1u);
        if (ctr[idx] != 0u) return;
    }
}

/* AES-CTR encrypt-or-decrypt (symmetric). `j0_inc` is the *first* counter
 * value used for the data (i.e., already incremented from J0). */
static void aes_ctr_xor(const aes128_ctx_t *ks,
                        uint8_t ctr[16],
                        const uint8_t *in, uint32_t len,
                        uint8_t *out) {
    uint32_t off = 0;
    while (off < len) {
        uint8_t ks_block[16];
        uint32_t i;
        uint32_t take = len - off;
        if (take > 16u) take = 16u;
        aes128_encrypt_block(ks, ctr, ks_block);
        for (i = 0; i < take; i++) out[off + i] = (uint8_t)(in[off + i] ^ ks_block[i]);
        inc32(ctr);
        off += take;
    }
}

static void put_be64(uint8_t *p, uint64_t v) {
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        p[i] = (uint8_t)((v >> (8u * (7u - i))) & 0xFFu);
    }
}

static void gcm_compute_tag(const aes128_ctx_t *ks,
                            const uint8_t h[16],
                            const uint8_t j0[16],
                            const uint8_t *aad, uint32_t aad_len,
                            const uint8_t *ct, uint32_t ct_len,
                            uint8_t tag_out[16]) {
    uint8_t y[16];
    uint8_t lenblock[16];
    uint8_t ek_j0[16];
    uint32_t i;

    for (i = 0; i < 16u; i++) y[i] = 0u;
    ghash_update(y, h, aad, aad_len);
    ghash_update(y, h, ct, ct_len);

    /* len(AAD) || len(C) in bits, big-endian. */
    put_be64(&lenblock[0], (uint64_t)aad_len * 8u);
    put_be64(&lenblock[8], (uint64_t)ct_len  * 8u);
    for (i = 0; i < 16u; i++) y[i] = (uint8_t)(y[i] ^ lenblock[i]);
    ghash_mul(y, h);

    aes128_encrypt_block(ks, j0, ek_j0);
    xor_block(tag_out, y, ek_j0);
}

void aes128_gcm_seal(const uint8_t key[AES128_GCM_KEY_SIZE],
                     const uint8_t nonce[AES128_GCM_NONCE_SIZE],
                     const uint8_t *aad, uint32_t aad_len,
                     const uint8_t *pt, uint32_t pt_len,
                     uint8_t *ct_out,
                     uint8_t tag_out[AES128_GCM_TAG_SIZE]) {
    aes128_ctx_t ks;
    uint8_t  h[16];
    uint8_t  zero[16];
    uint8_t  j0[16];
    uint8_t  ctr[16];
    uint32_t i;

    aes128_set_key(&ks, key);

    for (i = 0; i < 16u; i++) zero[i] = 0u;
    aes128_encrypt_block(&ks, zero, h);

    /* J0 for 12-byte nonce: nonce || 0x00000001. */
    for (i = 0; i < 12u; i++) j0[i] = nonce[i];
    j0[12] = 0u; j0[13] = 0u; j0[14] = 0u; j0[15] = 1u;

    for (i = 0; i < 16u; i++) ctr[i] = j0[i];
    inc32(ctr);

    aes_ctr_xor(&ks, ctr, pt, pt_len, ct_out);

    gcm_compute_tag(&ks, h, j0, aad, aad_len, ct_out, pt_len, tag_out);

    ct_wipe(&ks, sizeof(ks));
    ct_wipe(h,   sizeof(h));
}

int aes128_gcm_open(const uint8_t key[AES128_GCM_KEY_SIZE],
                    const uint8_t nonce[AES128_GCM_NONCE_SIZE],
                    const uint8_t *aad, uint32_t aad_len,
                    const uint8_t *ct, uint32_t ct_len,
                    const uint8_t tag[AES128_GCM_TAG_SIZE],
                    uint8_t *pt_out) {
    aes128_ctx_t ks;
    uint8_t  h[16];
    uint8_t  zero[16];
    uint8_t  j0[16];
    uint8_t  ctr[16];
    uint8_t  expected[16];
    uint32_t i;
    int      ok;

    aes128_set_key(&ks, key);

    for (i = 0; i < 16u; i++) zero[i] = 0u;
    aes128_encrypt_block(&ks, zero, h);

    for (i = 0; i < 12u; i++) j0[i] = nonce[i];
    j0[12] = 0u; j0[13] = 0u; j0[14] = 0u; j0[15] = 1u;

    gcm_compute_tag(&ks, h, j0, aad, aad_len, ct, ct_len, expected);

    ok = (ct_memcmp(expected, tag, 16u) == 0) ? 1 : 0;

    if (ok) {
        for (i = 0; i < 16u; i++) ctr[i] = j0[i];
        inc32(ctr);
        aes_ctr_xor(&ks, ctr, ct, ct_len, pt_out);
    }

    ct_wipe(&ks,        sizeof(ks));
    ct_wipe(h,          sizeof(h));
    ct_wipe(expected,   sizeof(expected));
    return ok;
}
