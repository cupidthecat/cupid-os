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
