/* ECDSA-P256 signature verification.
 *
 * Algorithm (FIPS 186-4 §6.4.2):
 *   1. Reject if r or s is 0 or >= n.
 *   2. e  = leftmost L_n bits of HASH(m) interpreted as an integer mod n
 *          (we accept the hash bytes the caller already computed).
 *   3. w  = s^-1 mod n
 *   4. u1 = e*w mod n,  u2 = r*w mod n
 *   5. (x1, y1) = u1*G + u2*Q ; reject if at infinity.
 *   6. Accept iff (x1 mod n) == r.
 *
 * All inputs are public, so this is variable-time.
 */

#include "ecdsa.h"
#include "hmac.h"
#include "string.h"

static int scalar_cmp(const p256_scalar_t a, const p256_scalar_t b) {
    int i;
    for (i = P256_LIMBS - 1; i >= 0; i--) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}

static uint32_t scalar_add_raw(p256_scalar_t r,
                               const p256_scalar_t a,
                               const p256_scalar_t b) {
    uint64_t carry = 0;
    uint32_t i;
    for (i = 0; i < P256_LIMBS; i++) {
        uint64_t v = (uint64_t)a[i] + b[i] + carry;
        r[i] = (uint32_t)v;
        carry = v >> 32;
    }
    return (uint32_t)carry;
}

static uint32_t scalar_sub_raw(p256_scalar_t r,
                               const p256_scalar_t a,
                               const p256_scalar_t b) {
    uint64_t borrow = 0;
    uint32_t i;
    for (i = 0; i < P256_LIMBS; i++) {
        uint64_t v = (uint64_t)a[i] - b[i] - borrow;
        r[i] = (uint32_t)v;
        borrow = (v >> 32) & 1u;
    }
    return (uint32_t)borrow;
}

static void scalar_add_mod_n(p256_scalar_t r,
                             const p256_scalar_t a,
                             const p256_scalar_t b) {
    p256_scalar_t t;
    uint32_t carry = scalar_add_raw(t, a, b);
    if (carry || scalar_cmp(t, P256_N) >= 0) {
        scalar_sub_raw(t, t, P256_N);
    }
    memcpy(r, t, sizeof(t));
}

static void bits2octets(const uint8_t *hash, uint32_t hash_len, uint8_t out[32]) {
    p256_scalar_t e;
    p256_scalar_mod_n_from_be(e, hash, hash_len);
    p256_scalar_to_be(out, e);
}

static void rfc6979_nonce(const uint8_t priv[32],
                          const uint8_t *hash, uint32_t hash_len,
                          uint32_t retry,
                          uint8_t nonce[32]) {
    uint8_t bx[64];
    uint8_t v[32];
    uint8_t k[32];
    uint8_t h1[32];
    uint8_t tmp[32 + 1 + 64 + 4];
    uint32_t off;
    uint32_t i;

    bits2octets(hash, hash_len, h1);
    for (i = 0; i < 32u; i++) {
        v[i] = 0x01u;
        k[i] = 0x00u;
        bx[i] = priv[i];
        bx[32u + i] = h1[i];
    }

    off = 0;
    memcpy(tmp + off, v, 32u); off += 32u;
    tmp[off++] = 0x00u;
    memcpy(tmp + off, bx, 64u); off += 64u;
    hmac_sha256(k, 32u, tmp, off, k);
    hmac_sha256(k, 32u, v, 32u, v);

    off = 0;
    memcpy(tmp + off, v, 32u); off += 32u;
    tmp[off++] = 0x01u;
    memcpy(tmp + off, bx, 64u); off += 64u;
    hmac_sha256(k, 32u, tmp, off, k);
    hmac_sha256(k, 32u, v, 32u, v);

    for (;;) {
        hmac_sha256(k, 32u, v, 32u, v);
        memcpy(nonce, v, 32u);
        if (retry == 0u) return;

        off = 0;
        memcpy(tmp + off, v, 32u); off += 32u;
        tmp[off++] = 0x00u;
        while (retry > 0u && off + 4u <= sizeof(tmp)) {
            tmp[off++] = (uint8_t)((retry >> 24) & 0xFFu);
            tmp[off++] = (uint8_t)((retry >> 16) & 0xFFu);
            tmp[off++] = (uint8_t)((retry >> 8) & 0xFFu);
            tmp[off++] = (uint8_t)(retry & 0xFFu);
            retry = 0u;
        }
        hmac_sha256(k, 32u, tmp, off, k);
        hmac_sha256(k, 32u, v, 32u, v);
    }
}

static int load_scalar(p256_scalar_t out,
                       const uint8_t *be, uint32_t len) {
    uint8_t buf[32];
    uint32_t i;
    if (len > 32u) {
        /* Trim leading zero bytes if any. */
        uint32_t skip = len - 32u;
        for (i = 0; i < skip; i++) {
            if (be[i] != 0u) return -1;
        }
        be  += skip;
        len  = 32u;
    }
    for (i = 0; i < 32u; i++) buf[i] = 0u;
    for (i = 0; i < len; i++) buf[32u - len + i] = be[i];
    return p256_scalar_from_be(out, buf);
}

int ecdsa_p256_verify(const p256_aff_t *pubkey,
                      const uint8_t *hash,    uint32_t hash_len,
                      const uint8_t *r_be,    uint32_t r_len,
                      const uint8_t *s_be,    uint32_t s_len) {
    p256_scalar_t r_scal, s_scal, e, w, u1, u2;
    p256_jac_t   R;
    p256_aff_t   R_aff;
    p256_fe_t    r_modn;
    uint8_t      x1_be[32];
    p256_scalar_t x1_modn;

    if (load_scalar(r_scal, r_be, r_len) != 0) return -1;
    if (load_scalar(s_scal, s_be, s_len) != 0) return -1;
    if (p256_scalar_iszero(r_scal) || p256_scalar_iszero(s_scal)) return -1;

    /* e = hash interpreted mod n. */
    p256_scalar_mod_n_from_be(e, hash, hash_len);

    /* w = s^-1 mod n. */
    p256_scalar_inv(w, s_scal);

    p256_scalar_mul(u1, e,      w);
    p256_scalar_mul(u2, r_scal, w);

    /* R = u1*G + u2*Q. */
    p256_double_scalar_mul(&R, u1, u2, pubkey);
    if (p256_jac_is_infinity(&R)) return -1;

    p256_jac_to_affine(&R_aff, &R);
    p256_fe_to_be(x1_be, R_aff.x);

    /* Reduce x1 mod n. */
    p256_scalar_mod_n_from_be(x1_modn, x1_be, 32u);

    /* r_modn is just r as a field element (r < n already). */
    {
        uint32_t i;
        for (i = 0; i < P256_LIMBS; i++) r_modn[i] = r_scal[i];
    }
    {
        uint32_t i, acc = 0u;
        for (i = 0; i < P256_LIMBS; i++) acc |= (x1_modn[i] ^ r_modn[i]);
        if (acc != 0u) return -1;
    }
    return 0;
}

int ecdsa_p256_sign(const uint8_t priv_be32[32],
                    const uint8_t *hash, uint32_t hash_len,
                    uint8_t r_be32[32], uint8_t s_be32[32]) {
    p256_scalar_t d, e, k, kinv, r, s, rd, sum;
    p256_jac_t R;
    p256_aff_t R_aff;
    p256_aff_t G;
    uint8_t k_be[32];
    uint8_t x_be[32];
    uint32_t retry;

    if (p256_scalar_from_be(d, priv_be32) != 0) return -1;
    if (p256_scalar_iszero(d)) return -1;
    p256_scalar_mod_n_from_be(e, hash, hash_len);

    p256_fe_copy(G.x, P256_GX);
    p256_fe_copy(G.y, P256_GY);
    G.infinity = 0;

    for (retry = 0; retry < 16u; retry++) {
        rfc6979_nonce(priv_be32, hash, hash_len, retry, k_be);
        if (p256_scalar_from_be(k, k_be) != 0 || p256_scalar_iszero(k))
            continue;

        p256_scalar_mul_point(&R, k, &G);
        if (p256_jac_is_infinity(&R)) continue;
        p256_jac_to_affine(&R_aff, &R);
        p256_fe_to_be(x_be, R_aff.x);
        p256_scalar_mod_n_from_be(r, x_be, 32u);
        if (p256_scalar_iszero(r)) continue;

        p256_scalar_mul(rd, r, d);
        scalar_add_mod_n(sum, e, rd);
        p256_scalar_inv(kinv, k);
        p256_scalar_mul(s, kinv, sum);
        if (p256_scalar_iszero(s)) continue;

        p256_scalar_to_be(r_be32, r);
        p256_scalar_to_be(s_be32, s);
        return 0;
    }
    return -1;
}
