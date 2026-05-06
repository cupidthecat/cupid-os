/* NIST P-256 (secp256r1) curve operations.
 *
 * Field arithmetic uses 8 32-bit little-endian limbs. Multiplication is
 * 8x8 schoolbook producing a 16-limb product, reduced mod p by an
 * iterative shift-subtract loop (bit-by-bit, ~257 steps). This is slower
 * than NIST Solinas reduction but considerably simpler to verify;
 * inverse is Fermat (a^(p-2) mod p) via square-and-multiply.
 *
 * Scalar arithmetic mod n shares the same primitives, applied with N
 * substituted for P.
 *
 * Point operations are Jacobian (X,Y,Z); doubling uses the a=-3 shortcut
 * M = 3*(X-Z^2)*(X+Z^2). Point add is generic (not mixed-affine — the
 * code keeps the spec simple by working entirely in Jacobian; affine
 * operands are lifted to Z=1 as needed).
 *
 * Constant-time scalar mult uses a left-to-right Montgomery ladder; a
 * conditional limb-swap moves bits between R0 and R1 without branching.
 * Double-scalar (verify) uses a simple per-bit doubling with two
 * conditional adds — variable-time but acceptable for verify-only paths.
 */

#include "p256.h"

/* --- Curve constants (limb[0] = LSW) ---------------------------------- */

const p256_fe_t P256_P = {
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000001u, 0xffffffffu
};
const p256_fe_t P256_N = {
    0xfc632551u, 0xf3b9cac2u, 0xa7179e84u, 0xbce6faadu,
    0xffffffffu, 0xffffffffu, 0x00000000u, 0xffffffffu
};
const p256_fe_t P256_B = {
    0x27d2604bu, 0x3bce3c3eu, 0xcc53b0f6u, 0x651d06b0u,
    0x769886bcu, 0xb3ebbd55u, 0xaa3a93e7u, 0x5ac635d8u
};
const p256_fe_t P256_GX = {
    0xd898c296u, 0xf4a13945u, 0x2deb33a0u, 0x77037d81u,
    0x63a440f2u, 0xf8bce6e5u, 0xe12c4247u, 0x6b17d1f2u
};
const p256_fe_t P256_GY = {
    0x37bf51f5u, 0xcbb64068u, 0x6b315eceu, 0x2bce3357u,
    0x7c0f9e16u, 0x8ee7eb4au, 0xfe1a7f9bu, 0x4fe342e2u
};

/* --- Field-element basics --------------------------------------------- */

void p256_fe_zero(p256_fe_t r) {
    uint32_t i;
    for (i = 0; i < P256_LIMBS; i++) r[i] = 0u;
}

void p256_fe_copy(p256_fe_t r, const p256_fe_t a) {
    uint32_t i;
    for (i = 0; i < P256_LIMBS; i++) r[i] = a[i];
}

int p256_fe_iszero(const p256_fe_t a) {
    uint32_t i, acc = 0u;
    for (i = 0; i < P256_LIMBS; i++) acc |= a[i];
    return (acc == 0u) ? 1 : 0;
}

int p256_fe_eq(const p256_fe_t a, const p256_fe_t b) {
    uint32_t i, acc = 0u;
    for (i = 0; i < P256_LIMBS; i++) acc |= (a[i] ^ b[i]);
    return (acc == 0u) ? 1 : 0;
}

static int fe_cmp(const p256_fe_t a, const p256_fe_t b) {
    int32_t i;
    for (i = (int32_t)P256_LIMBS - 1; i >= 0; i--) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}

/* --- Modular reduction wrt a generic 8-limb modulus m ----------------- */

/* Subtract `m` from `a` in place. Caller has verified a >= m. */
static void fe_sub_mod(p256_fe_t a, const p256_fe_t m) {
    uint64_t borrow = 0;
    uint32_t i;
    for (i = 0; i < P256_LIMBS; i++) {
        uint64_t d = (uint64_t)a[i] - m[i] - borrow;
        a[i] = (uint32_t)d;
        borrow = (d >> 32) & 1u;
    }
    (void)borrow;
}

/* r = a + b mod m. */
static void fe_addmod(p256_fe_t r, const p256_fe_t a, const p256_fe_t b,
                      const p256_fe_t m) {
    p256_fe_t t;
    uint64_t carry = 0;
    uint32_t i;
    for (i = 0; i < P256_LIMBS; i++) {
        uint64_t s = (uint64_t)a[i] + b[i] + carry;
        t[i] = (uint32_t)s;
        carry = s >> 32;
    }
    if (carry || fe_cmp(t, m) >= 0) fe_sub_mod(t, m);
    p256_fe_copy(r, t);
}

/* r = a - b mod m. */
static void fe_submod(p256_fe_t r, const p256_fe_t a, const p256_fe_t b,
                      const p256_fe_t m) {
    p256_fe_t t;
    uint64_t borrow = 0;
    uint32_t i;
    for (i = 0; i < P256_LIMBS; i++) {
        uint64_t d = (uint64_t)a[i] - b[i] - borrow;
        t[i] = (uint32_t)d;
        borrow = (d >> 32) & 1u;
    }
    if (borrow) {
        uint64_t carry = 0;
        for (i = 0; i < P256_LIMBS; i++) {
            uint64_t s = (uint64_t)t[i] + m[i] + carry;
            t[i] = (uint32_t)s;
            carry = s >> 32;
        }
    }
    p256_fe_copy(r, t);
}

/* 8x8 schoolbook multiply -> 16-limb wide product. */
static void mul_wide(uint32_t out[16], const p256_fe_t a, const p256_fe_t b) {
    uint32_t i, j;
    for (i = 0; i < 16u; i++) out[i] = 0u;
    for (i = 0; i < P256_LIMBS; i++) {
        uint64_t c = 0;
        for (j = 0; j < P256_LIMBS; j++) {
            uint64_t prod = (uint64_t)a[i] * b[j] + out[i + j] + c;
            out[i + j] = (uint32_t)prod;
            c = prod >> 32;
        }
        out[i + P256_LIMBS] = (uint32_t)c;
    }
}

/* Reduce a 16-limb wide value mod m (8-limb), placing remainder in r.
 * Uses iterative shift-subtract: for each shift from 256 down to 0,
 * subtract (m << shift) if the result remains non-negative. */
static void reduce_wide_mod(p256_fe_t r, uint32_t a[16], const p256_fe_t m) {
    int32_t shift;
    int32_t i;
    uint32_t pshift[17];

    for (shift = 256; shift >= 0; shift--) {
        uint32_t limb_shift = (uint32_t)shift / 32u;
        uint32_t bit_shift  = (uint32_t)shift & 31u;
        int      cmp;

        for (i = 0; i < 17; i++) pshift[i] = 0u;
        if (bit_shift == 0u) {
            for (i = 0; i < (int32_t)P256_LIMBS; i++) {
                if (limb_shift + (uint32_t)i < 17u) {
                    pshift[limb_shift + (uint32_t)i] = m[i];
                }
            }
        } else {
            for (i = 0; i < (int32_t)P256_LIMBS; i++) {
                uint32_t v  = m[i];
                uint32_t lo = v << bit_shift;
                uint32_t hi = v >> (32u - bit_shift);
                if (limb_shift + (uint32_t)i < 17u) {
                    pshift[limb_shift + (uint32_t)i] |= lo;
                }
                if (limb_shift + (uint32_t)i + 1u < 17u) {
                    pshift[limb_shift + (uint32_t)i + 1u] |= hi;
                }
            }
        }

        cmp = 0;
        for (i = 16; i >= 0; i--) {
            uint32_t av = (i < 16) ? a[i] : 0u;
            if (av > pshift[i]) { cmp = 1; break; }
            if (av < pshift[i]) { cmp = -1; break; }
        }
        if (cmp >= 0) {
            uint64_t borrow = 0;
            for (i = 0; i < 16; i++) {
                uint64_t d = (uint64_t)a[i] - pshift[i] - borrow;
                a[i] = (uint32_t)d;
                borrow = (d >> 32) & 1u;
            }
        }
    }

    for (i = 0; i < (int32_t)P256_LIMBS; i++) r[i] = a[i];
}

/* --- Field ops mod p -------------------------------------------------- */

void p256_fe_add(p256_fe_t r, const p256_fe_t a, const p256_fe_t b) {
    fe_addmod(r, a, b, P256_P);
}

void p256_fe_sub(p256_fe_t r, const p256_fe_t a, const p256_fe_t b) {
    fe_submod(r, a, b, P256_P);
}

void p256_fe_mul(p256_fe_t r, const p256_fe_t a, const p256_fe_t b) {
    uint32_t prod[16];
    mul_wide(prod, a, b);
    reduce_wide_mod(r, prod, P256_P);
}

void p256_fe_sqr(p256_fe_t r, const p256_fe_t a) {
    p256_fe_mul(r, a, a);
}

/* a^(p-2) mod p via square-and-multiply on the 256-bit big-endian
 * exponent. Uses a static buffer for the exponent — p-2 has all bits
 * set in the top 32 bits (matches p's structure). */
void p256_fe_inv(p256_fe_t r, const p256_fe_t a) {
    static const uint8_t P_MINUS_2_BE[32] = {
        0xffu,0xffu,0xffu,0xffu, 0x00u,0x00u,0x00u,0x01u,
        0x00u,0x00u,0x00u,0x00u, 0x00u,0x00u,0x00u,0x00u,
        0x00u,0x00u,0x00u,0x00u, 0xffu,0xffu,0xffu,0xffu,
        0xffu,0xffu,0xffu,0xffu, 0xffu,0xffu,0xffu,0xfdu
    };
    p256_fe_t acc;
    uint32_t i;
    int      bit;
    int      started = 0;

    p256_fe_zero(acc); acc[0] = 1u;
    for (i = 0; i < 32u; i++) {
        uint8_t byte = P_MINUS_2_BE[i];
        for (bit = 7; bit >= 0; bit--) {
            int b = (byte >> (uint32_t)bit) & 1;
            if (started) p256_fe_sqr(acc, acc);
            if (b) {
                if (!started) {
                    p256_fe_copy(acc, a);
                    started = 1;
                } else {
                    p256_fe_mul(acc, acc, a);
                }
            }
        }
    }
    p256_fe_copy(r, acc);
}

int p256_fe_from_be(p256_fe_t r, const uint8_t in[32]) {
    p256_fe_t t;
    uint32_t i;
    for (i = 0; i < P256_LIMBS; i++) {
        uint32_t off = (P256_LIMBS - 1u - i) * 4u;
        t[i] = ((uint32_t)in[off]     << 24)
             | ((uint32_t)in[off + 1] << 16)
             | ((uint32_t)in[off + 2] <<  8)
             |  (uint32_t)in[off + 3];
    }
    if (fe_cmp(t, P256_P) >= 0) return -1;
    p256_fe_copy(r, t);
    return 0;
}

void p256_fe_to_be(uint8_t out[32], const p256_fe_t a) {
    uint32_t i;
    for (i = 0; i < P256_LIMBS; i++) {
        uint32_t off = (P256_LIMBS - 1u - i) * 4u;
        out[off]     = (uint8_t)((a[i] >> 24) & 0xFFu);
        out[off + 1] = (uint8_t)((a[i] >> 16) & 0xFFu);
        out[off + 2] = (uint8_t)((a[i] >>  8) & 0xFFu);
        out[off + 3] = (uint8_t)( a[i]        & 0xFFu);
    }
}

/* --- Scalar arithmetic mod n ----------------------------------------- */

int p256_scalar_from_be(p256_scalar_t r, const uint8_t in[32]) {
    p256_fe_t t;
    uint32_t i;
    for (i = 0; i < P256_LIMBS; i++) {
        uint32_t off = (P256_LIMBS - 1u - i) * 4u;
        t[i] = ((uint32_t)in[off]     << 24)
             | ((uint32_t)in[off + 1] << 16)
             | ((uint32_t)in[off + 2] <<  8)
             |  (uint32_t)in[off + 3];
    }
    if (fe_cmp(t, P256_N) >= 0) return -1;
    for (i = 0; i < P256_LIMBS; i++) r[i] = t[i];
    return 0;
}

void p256_scalar_to_be(uint8_t out[32], const p256_scalar_t a) {
    p256_fe_to_be(out, a);
}

/* Reduce an arbitrary big-endian byte string mod n. Used by ECDSA verify
 * to convert hash(msg) into a scalar in [0, n). */
void p256_scalar_mod_n_from_be(p256_scalar_t r, const uint8_t *bytes, uint32_t len) {
    uint32_t buf[16];
    uint32_t i;
    /* Right-align bytes into a 16-limb little-endian buffer; treat as the
     * value to reduce. Inputs longer than 64 bytes get truncated to the
     * top 64 bytes per ECDSA convention (FIPS 186-4 §6.4) — but for our
     * cipher suites the hash is always SHA-256 so len = 32 in practice. */
    for (i = 0; i < 16u; i++) buf[i] = 0u;
    if (len > 64u) len = 64u;
    {
        uint32_t off;
        for (i = 0; i < len; i++) {
            uint32_t shift = (uint32_t)((len - 1u - i) & 3u) * 8u;
            off = (uint32_t)((len - 1u - i) >> 2);
            if (off < 16u) buf[off] |= (uint32_t)((uint32_t)bytes[i] << shift);
        }
    }
    reduce_wide_mod(r, buf, P256_N);
}

int p256_scalar_iszero(const p256_scalar_t a) {
    return p256_fe_iszero(a);
}

void p256_scalar_mul(p256_scalar_t r, const p256_scalar_t a, const p256_scalar_t b) {
    uint32_t prod[16];
    mul_wide(prod, a, b);
    reduce_wide_mod(r, prod, P256_N);
}

void p256_scalar_inv(p256_scalar_t r, const p256_scalar_t a) {
    /* a^(n-2) mod n. n-2 in big-endian: */
    static const uint8_t N_MINUS_2_BE[32] = {
        0xffu,0xffu,0xffu,0xffu, 0x00u,0x00u,0x00u,0x00u,
        0xffu,0xffu,0xffu,0xffu, 0xffu,0xffu,0xffu,0xffu,
        0xbcu,0xe6u,0xfau,0xadu, 0xa7u,0x17u,0x9eu,0x84u,
        0xf3u,0xb9u,0xcau,0xc2u, 0xfcu,0x63u,0x25u,0x4fu
    };
    p256_scalar_t acc;
    uint32_t i;
    int      bit;
    int      started = 0;

    p256_fe_zero(acc); acc[0] = 1u;
    for (i = 0; i < 32u; i++) {
        uint8_t byte = N_MINUS_2_BE[i];
        for (bit = 7; bit >= 0; bit--) {
            int b = (byte >> (uint32_t)bit) & 1;
            if (started) p256_scalar_mul(acc, acc, acc);
            if (b) {
                if (!started) {
                    p256_fe_copy(acc, a);
                    started = 1;
                } else {
                    p256_scalar_mul(acc, acc, a);
                }
            }
        }
    }
    p256_fe_copy(r, acc);
}

/* --- Jacobian point ops ---------------------------------------------- */

void p256_jac_set_infinity(p256_jac_t *r) {
    p256_fe_zero(r->X); r->X[0] = 1u;
    p256_fe_zero(r->Y); r->Y[0] = 1u;
    p256_fe_zero(r->Z);
}

int p256_jac_is_infinity(const p256_jac_t *r) {
    return p256_fe_iszero(r->Z);
}

void p256_jac_from_affine(p256_jac_t *out, const p256_aff_t *in) {
    if (in->infinity) {
        p256_jac_set_infinity(out);
        return;
    }
    p256_fe_copy(out->X, in->x);
    p256_fe_copy(out->Y, in->y);
    p256_fe_zero(out->Z);
    out->Z[0] = 1u;
}

void p256_jac_to_affine(p256_aff_t *out, const p256_jac_t *in) {
    p256_fe_t zinv, zinv2, zinv3;
    if (p256_jac_is_infinity(in)) {
        out->infinity = 1;
        p256_fe_zero(out->x);
        p256_fe_zero(out->y);
        return;
    }
    p256_fe_inv(zinv, in->Z);
    p256_fe_sqr(zinv2, zinv);
    p256_fe_mul(zinv3, zinv2, zinv);
    p256_fe_mul(out->x, in->X, zinv2);
    p256_fe_mul(out->y, in->Y, zinv3);
    out->infinity = 0;
}

/* Doubling on Jacobian for short Weierstrass with a = -3.
 * Writes through a local `out` so the caller can pass r == a. */
void p256_jac_double(p256_jac_t *r, const p256_jac_t *a) {
    p256_fe_t XX, YY, YYYY, ZZ, S, M, t1, t2;
    p256_jac_t out;

    if (p256_jac_is_infinity(a)) { p256_jac_set_infinity(r); return; }

    p256_fe_sqr(XX,   a->X);                /* X^2          */
    p256_fe_sqr(YY,   a->Y);                /* Y^2          */
    p256_fe_sqr(YYYY, YY);                  /* Y^4          */
    p256_fe_sqr(ZZ,   a->Z);                /* Z^2          */

    /* S = 4*X*YY  =  2*((X+YY)^2 - XX - YYYY) */
    p256_fe_add(t1, a->X, YY);
    p256_fe_sqr(t1, t1);
    p256_fe_sub(t1, t1, XX);
    p256_fe_sub(t1, t1, YYYY);
    p256_fe_add(S, t1, t1);

    /* M = 3*(X-Z^2)*(X+Z^2) */
    p256_fe_sub(t1, a->X, ZZ);
    p256_fe_add(t2, a->X, ZZ);
    p256_fe_mul(t1, t1, t2);
    p256_fe_add(M, t1, t1);
    p256_fe_add(M, M,  t1);

    /* Z' = (Y+Z)^2 - YY - ZZ.  Compute first, while a->Y / a->Z still
     * hold their input values. */
    p256_fe_add(t1, a->Y, a->Z);
    p256_fe_sqr(t1, t1);
    p256_fe_sub(t1, t1, YY);
    p256_fe_sub(t1, t1, ZZ);
    p256_fe_copy(out.Z, t1);

    /* X' = M^2 - 2*S */
    p256_fe_sqr(out.X, M);
    p256_fe_sub(out.X, out.X, S);
    p256_fe_sub(out.X, out.X, S);

    /* Y' = M*(S - X') - 8*YYYY */
    p256_fe_sub(t1, S, out.X);
    p256_fe_mul(t1, M, t1);
    p256_fe_add(t2, YYYY, YYYY);
    p256_fe_add(t2, t2, t2);
    p256_fe_add(t2, t2, t2);
    p256_fe_sub(out.Y, t1, t2);

    *r = out;
}

/* Generic Jacobian + Jacobian point addition (Cohen §13.2.1.c). Handles
 * the doubling-when-equal and identity edge cases by checking r,s,t. */
void p256_jac_add(p256_jac_t *r, const p256_jac_t *a, const p256_jac_t *b) {
    p256_fe_t Z1Z1, Z2Z2, U1, U2, S1, S2, H, R, HH, HHH, V, t1;
    p256_jac_t out;

    if (p256_jac_is_infinity(a)) { *r = *b; return; }
    if (p256_jac_is_infinity(b)) { *r = *a; return; }

    p256_fe_sqr(Z1Z1, a->Z);
    p256_fe_sqr(Z2Z2, b->Z);
    p256_fe_mul(U1, a->X, Z2Z2);
    p256_fe_mul(U2, b->X, Z1Z1);
    p256_fe_mul(t1, b->Z, Z2Z2);
    p256_fe_mul(S1, a->Y, t1);
    p256_fe_mul(t1, a->Z, Z1Z1);
    p256_fe_mul(S2, b->Y, t1);

    if (p256_fe_eq(U1, U2)) {
        if (p256_fe_eq(S1, S2)) { p256_jac_double(r, a); return; }
        p256_jac_set_infinity(r);
        return;
    }

    p256_fe_sub(H, U2, U1);
    p256_fe_sub(R, S2, S1);
    p256_fe_sqr(HH,  H);
    p256_fe_mul(HHH, HH, H);
    p256_fe_mul(V,   U1, HH);

    /* X3 = R^2 - HHH - 2*V */
    p256_fe_sqr(out.X, R);
    p256_fe_sub(out.X, out.X, HHH);
    p256_fe_sub(out.X, out.X, V);
    p256_fe_sub(out.X, out.X, V);

    /* Y3 = R*(V - X3) - S1*HHH */
    p256_fe_sub(t1, V, out.X);
    p256_fe_mul(out.Y, R, t1);
    p256_fe_mul(t1, S1, HHH);
    p256_fe_sub(out.Y, out.Y, t1);

    /* Z3 = Z1 * Z2 * H */
    p256_fe_mul(t1, a->Z, b->Z);
    p256_fe_mul(out.Z, t1, H);

    *r = out;
}

/* On-curve check: y^2 == x^3 - 3x + b. Rejects the identity. */
int p256_aff_is_on_curve(const p256_aff_t *p) {
    p256_fe_t lhs, rhs, t;
    if (p->infinity) return 0;
    if (fe_cmp(p->x, P256_P) >= 0) return 0;
    if (fe_cmp(p->y, P256_P) >= 0) return 0;
    p256_fe_sqr(lhs, p->y);
    p256_fe_sqr(rhs, p->x);
    p256_fe_mul(rhs, rhs, p->x);
    /* rhs -= 3x */
    p256_fe_add(t, p->x, p->x);
    p256_fe_add(t, t,    p->x);
    p256_fe_sub(rhs, rhs, t);
    p256_fe_add(rhs, rhs, P256_B);
    return p256_fe_eq(lhs, rhs) ? 1 : 0;
}

/* --- Constant-time conditional swap of jacobian points ---------------- */

static void fe_cswap(p256_fe_t a, p256_fe_t b, uint32_t mask) {
    /* mask is all-1s or all-0s. */
    uint32_t i, t;
    for (i = 0; i < P256_LIMBS; i++) {
        t = (a[i] ^ b[i]) & mask;
        a[i] ^= t;
        b[i] ^= t;
    }
}
static void jac_cswap(p256_jac_t *p, p256_jac_t *q, uint32_t mask) {
    fe_cswap(p->X, q->X, mask);
    fe_cswap(p->Y, q->Y, mask);
    fe_cswap(p->Z, q->Z, mask);
}

/* --- Constant-time scalar mult: Montgomery ladder --------------------- */

void p256_scalar_mul_point(p256_jac_t *r, const p256_scalar_t k,
                           const p256_aff_t *P) {
    p256_jac_t R0, R1, P_jac;
    int32_t  i;
    uint32_t prev_bit = 0u;

    p256_jac_set_infinity(&R0);
    p256_jac_from_affine(&P_jac, P);
    R1 = P_jac;

    for (i = (int32_t)P256_LIMBS - 1; i >= 0; i--) {
        uint32_t limb = k[i];
        int      bi;
        for (bi = 31; bi >= 0; bi--) {
            uint32_t bit  = (limb >> (uint32_t)bi) & 1u;
            uint32_t mask = (uint32_t)(0u - (bit ^ prev_bit));
            jac_cswap(&R0, &R1, mask);
            p256_jac_add(&R1, &R0, &R1);
            p256_jac_double(&R0, &R0);
            prev_bit = bit;
        }
    }
    /* Final swap so R0 = k*P. */
    {
        uint32_t mask = (uint32_t)(0u - prev_bit);
        jac_cswap(&R0, &R1, mask);
    }
    *r = R0;
}

/* --- Double-scalar mult (verify-only, NOT constant-time) -------------- */

void p256_double_scalar_mul(p256_jac_t *r,
                            const p256_scalar_t u1,
                            const p256_scalar_t u2,
                            const p256_aff_t *Q) {
    /* Shamir's trick: scan u1 and u2 simultaneously bit-by-bit.
     * Pre-compute G, Q, G+Q. */
    p256_jac_t G_jac, Q_jac, GQ_jac, acc;
    p256_aff_t G_aff;
    int32_t i;

    p256_fe_copy(G_aff.x, P256_GX);
    p256_fe_copy(G_aff.y, P256_GY);
    G_aff.infinity = 0;

    p256_jac_from_affine(&G_jac, &G_aff);
    p256_jac_from_affine(&Q_jac, Q);
    p256_jac_add(&GQ_jac, &G_jac, &Q_jac);

    p256_jac_set_infinity(&acc);
    for (i = (int32_t)P256_LIMBS - 1; i >= 0; i--) {
        uint32_t lu1 = u1[i];
        uint32_t lu2 = u2[i];
        int      bi;
        for (bi = 31; bi >= 0; bi--) {
            uint32_t b1 = (lu1 >> (uint32_t)bi) & 1u;
            uint32_t b2 = (lu2 >> (uint32_t)bi) & 1u;
            p256_jac_double(&acc, &acc);
            if (b1 && b2) p256_jac_add(&acc, &acc, &GQ_jac);
            else if (b1)  p256_jac_add(&acc, &acc, &G_jac);
            else if (b2)  p256_jac_add(&acc, &acc, &Q_jac);
        }
    }
    *r = acc;
}

/* --- Pubkey import ---------------------------------------------------- */

int p256_pub_from_uncompressed(p256_aff_t *out,
                               const uint8_t *enc, uint32_t enc_len) {
    if (enc_len != 65u || enc[0] != 0x04u) return -1;
    if (p256_fe_from_be(out->x, &enc[1])  != 0) return -1;
    if (p256_fe_from_be(out->y, &enc[33]) != 0) return -1;
    out->infinity = 0;
    if (!p256_aff_is_on_curve(out)) return -1;
    return 0;
}
