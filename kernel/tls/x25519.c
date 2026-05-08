/* X25519 (RFC 7748 §5). 8 x uint32_t field-element representation in
 * radix 2^32 with reduction by 2^256 ≡ 38 (mod p), p = 2^255 - 19.
 *
 * Constant-time goals:
 *   - All field ops have data-independent control flow.
 *   - cswap uses a mask, never a branch on the secret bit.
 *   - The Montgomery ladder runs the full 255 iterations regardless of
 *     where the scalar's highest bit sits.
 *
 * Limitations: this is a careful but unaudited port. Side-channel
 * resistance against power/EM attacks is out of scope for QEMU. For
 * hostile-physical-attacker threat models, replace with a verified
 * implementation (e.g. fiat-crypto). */

#include "x25519.h"

const uint8_t X25519_BASE_POINT[32] = {
    9, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0
};

typedef uint32_t fe[8];

static void fe_zero(fe r) {
    uint32_t i;
    for (i = 0; i < 8; i++) r[i] = 0;
}

static void fe_one(fe r) {
    fe_zero(r);
    r[0] = 1u;
}

static void fe_copy(fe r, const fe a) {
    uint32_t i;
    for (i = 0; i < 8; i++) r[i] = a[i];
}

static void fe_from_bytes(fe r, const uint8_t b[32]) {
    uint32_t i;
    for (i = 0; i < 8; i++) {
        r[i] = (uint32_t)b[4u*i]
             | ((uint32_t)b[4u*i + 1] <<  8)
             | ((uint32_t)b[4u*i + 2] << 16)
             | ((uint32_t)b[4u*i + 3] << 24);
    }
    /* RFC 7748 §5: clear the high bit of the input u-coordinate. */
    r[7] &= 0x7FFFFFFFu;
}

/* Conditionally subtract p from a in canonical form. Output a value
 * < p, encode as 32 LE bytes. */
static void fe_to_bytes(uint8_t out[32], const fe a) {
    fe       t;
    uint64_t carry;
    uint32_t i, mask;

    /* Compute t = a + 19. If t fits in 256 bits AND has top bit set,
     * then a >= p: real reduced value is t & (2^255-1). Else use a. */
    fe_copy(t, a);
    carry = (uint64_t)t[0] + 19u;
    t[0] = (uint32_t)carry; carry >>= 32;
    for (i = 1; i < 8; i++) {
        uint64_t s = (uint64_t)t[i] + carry;
        t[i] = (uint32_t)s;
        carry = s >> 32;
    }
    mask = (uint32_t)(0u - ((t[7] >> 31) & 1u));
    /* mask = 0xFFFFFFFFu when reduce, else 0. */
    {
        fe r;
        for (i = 0; i < 8; i++) {
            uint32_t reduced = t[i];
            if (i == 7) reduced &= 0x7FFFFFFFu;
            r[i] = (mask & reduced) | ((~mask) & a[i]);
        }
        for (i = 0; i < 8; i++) {
            out[4u*i + 0] = (uint8_t)( r[i]        & 0xFFu);
            out[4u*i + 1] = (uint8_t)((r[i] >>  8) & 0xFFu);
            out[4u*i + 2] = (uint8_t)((r[i] >> 16) & 0xFFu);
            out[4u*i + 3] = (uint8_t)((r[i] >> 24) & 0xFFu);
        }
    }
}

/* Reduce 9-limb intermediate (carry holds extra ≤ ~38 bits). */
static void fe_carry(fe r, uint64_t r0, uint64_t r1, uint64_t r2,
                     uint64_t r3, uint64_t r4, uint64_t r5,
                     uint64_t r6, uint64_t r7) {
    uint64_t c;
    /* Propagate carries upward. */
    r1 += r0 >> 32; r0 &= 0xFFFFFFFFu;
    r2 += r1 >> 32; r1 &= 0xFFFFFFFFu;
    r3 += r2 >> 32; r2 &= 0xFFFFFFFFu;
    r4 += r3 >> 32; r3 &= 0xFFFFFFFFu;
    r5 += r4 >> 32; r4 &= 0xFFFFFFFFu;
    r6 += r5 >> 32; r5 &= 0xFFFFFFFFu;
    r7 += r6 >> 32; r6 &= 0xFFFFFFFFu;
    c   = r7 >> 31;             /* bits ≥ 2^255 fold via *19 */
    r7 &= 0x7FFFFFFFu;
    /* Add c * 19 to r0, propagate. */
    r0 += c * 19u;
    r1 += r0 >> 32; r0 &= 0xFFFFFFFFu;
    r2 += r1 >> 32; r1 &= 0xFFFFFFFFu;
    r3 += r2 >> 32; r2 &= 0xFFFFFFFFu;
    r4 += r3 >> 32; r3 &= 0xFFFFFFFFu;
    r5 += r4 >> 32; r4 &= 0xFFFFFFFFu;
    r6 += r5 >> 32; r5 &= 0xFFFFFFFFu;
    r7 += r6 >> 32; r6 &= 0xFFFFFFFFu;
    /* Top bit may pop again; one more fold (max +1*19, no overflow). */
    {
        uint64_t cc = r7 >> 31;
        r7 &= 0x7FFFFFFFu;
        r0 += cc * 19u;
        r1 += r0 >> 32; r0 &= 0xFFFFFFFFu;
    }
    r[0] = (uint32_t)r0;
    r[1] = (uint32_t)r1;
    r[2] = (uint32_t)r2;
    r[3] = (uint32_t)r3;
    r[4] = (uint32_t)r4;
    r[5] = (uint32_t)r5;
    r[6] = (uint32_t)r6;
    r[7] = (uint32_t)r7;
}

static void fe_add(fe r, const fe a, const fe b) {
    uint64_t r0 = (uint64_t)a[0] + b[0];
    uint64_t r1 = (uint64_t)a[1] + b[1];
    uint64_t r2 = (uint64_t)a[2] + b[2];
    uint64_t r3 = (uint64_t)a[3] + b[3];
    uint64_t r4 = (uint64_t)a[4] + b[4];
    uint64_t r5 = (uint64_t)a[5] + b[5];
    uint64_t r6 = (uint64_t)a[6] + b[6];
    uint64_t r7 = (uint64_t)a[7] + b[7];
    fe_carry(r, r0, r1, r2, r3, r4, r5, r6, r7);
}

static void fe_sub(fe r, const fe a, const fe b) {
    /* Two-pass subtract: subtract b with borrow chain, then if final
     * borrow is set, add p back (constant-time via mask). p = 2^255 - 19
     * has limbs [0xFFFFFFED, 0xFFFFFFFFx6, 0x7FFFFFFF]. */
    static const uint32_t P_LIMBS[8] = {
        0xFFFFFFEDu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
        0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x7FFFFFFFu
    };
    uint32_t t[8];
    uint64_t borrow = 0;
    uint64_t carry  = 0;
    uint32_t mask;
    int      i;

    for (i = 0; i < 8; i++) {
        uint64_t d = (uint64_t)a[i] - b[i] - borrow;
        t[i] = (uint32_t)d;
        borrow = (d >> 32) & 1u;
    }

    mask = (uint32_t)(0u - borrow);
    for (i = 0; i < 8; i++) {
        uint64_t s = (uint64_t)t[i] + (P_LIMBS[i] & mask) + carry;
        t[i] = (uint32_t)s;
        carry = s >> 32;
    }

    for (i = 0; i < 8; i++) r[i] = t[i];
}

static void fe_mul(fe r, const fe a, const fe b) {
    uint64_t prod[16];
    uint64_t low[8];
    uint32_t i, j;

    for (i = 0; i < 16; i++) prod[i] = 0;
    for (i = 0; i < 8; i++) {
        uint64_t carry = 0;
        for (j = 0; j < 8; j++) {
            uint64_t pp = (uint64_t)a[i] * b[j] + prod[i + j] + carry;
            prod[i + j] = pp & 0xFFFFFFFFu;
            carry = pp >> 32;
        }
        prod[i + 8] = carry;
    }
    /* Fold high limbs (multiplied by 38) into low. */
    for (i = 0; i < 8; i++) {
        low[i] = prod[i] + prod[i + 8] * 38u;
    }
    fe_carry(r, low[0], low[1], low[2], low[3],
             low[4], low[5], low[6], low[7]);
}

static void fe_square(fe r, const fe a) {
    fe_mul(r, a, a);
}

static void fe_mul_u32(fe r, const fe a, uint32_t n) {
    uint64_t r0 = (uint64_t)a[0] * n;
    uint64_t r1 = (uint64_t)a[1] * n;
    uint64_t r2 = (uint64_t)a[2] * n;
    uint64_t r3 = (uint64_t)a[3] * n;
    uint64_t r4 = (uint64_t)a[4] * n;
    uint64_t r5 = (uint64_t)a[5] * n;
    uint64_t r6 = (uint64_t)a[6] * n;
    uint64_t r7 = (uint64_t)a[7] * n;
    fe_carry(r, r0, r1, r2, r3, r4, r5, r6, r7);
}

/* a^(p-2) mod p - Fermat inversion. Uses the standard short addition
 * chain for X25519 (e.g. djb's curve25519-donna). */
static void fe_invert(fe r, const fe a) {
    fe t0, t1, t2, t3;
    int i;

    fe_square(t0, a);                       /* a^2 */
    fe_square(t1, t0); fe_square(t1, t1);   /* a^8 */
    fe_mul(t1, a, t1);                      /* a^9 */
    fe_mul(t0, t0, t1);                     /* a^11 */
    fe_square(t2, t0);                      /* a^22 */
    fe_mul(t1, t1, t2);                     /* a^31 = 2^5 - 1 */
    fe_square(t2, t1); for (i = 1; i <  5; i++) fe_square(t2, t2);
    fe_mul(t1, t2, t1);                     /* 2^10 - 1 */
    fe_square(t2, t1); for (i = 1; i < 10; i++) fe_square(t2, t2);
    fe_mul(t2, t2, t1);                     /* 2^20 - 1 */
    fe_square(t3, t2); for (i = 1; i < 20; i++) fe_square(t3, t3);
    fe_mul(t2, t3, t2);                     /* 2^40 - 1 */
    fe_square(t2, t2); for (i = 1; i < 10; i++) fe_square(t2, t2);
    fe_mul(t1, t2, t1);                     /* 2^50 - 1 */
    fe_square(t2, t1); for (i = 1; i < 50; i++) fe_square(t2, t2);
    fe_mul(t2, t2, t1);                     /* 2^100 - 1 */
    fe_square(t3, t2); for (i = 1; i < 100; i++) fe_square(t3, t3);
    fe_mul(t2, t3, t2);                     /* 2^200 - 1 */
    fe_square(t2, t2); for (i = 1; i < 50; i++) fe_square(t2, t2);
    fe_mul(t1, t2, t1);                     /* 2^250 - 1 */
    fe_square(t1, t1); fe_square(t1, t1); fe_square(t1, t1);
    fe_square(t1, t1); fe_square(t1, t1);   /* 2^255 - 32 */
    fe_mul(r, t1, t0);                      /* 2^255 - 21 = p - 2 */
}

/* Conditional swap: if cond == 1, swap a and b. cond must be 0 or 1. */
static void fe_cswap(fe a, fe b, uint32_t cond) {
    uint32_t mask = (uint32_t)(0u - cond);
    uint32_t i;
    for (i = 0; i < 8; i++) {
        uint32_t t = mask & (a[i] ^ b[i]);
        a[i] ^= t;
        b[i] ^= t;
    }
}

void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t peer_u[32]) {
    uint8_t  s[32];
    fe       x1, x2, z2, x3, z3;
    fe       a, aa, bb, e, c, d, da, cb;
    fe       b_fe;
    uint32_t swap = 0;
    int      t;
    uint32_t i;

    /* Clamp scalar (RFC 7748 §5). */
    for (i = 0; i < 32u; i++) s[i] = scalar[i];
    s[0]  = (uint8_t)(s[0]  & 0xF8u);
    s[31] = (uint8_t)((s[31] & 0x7Fu) | 0x40u);

    fe_from_bytes(x1, peer_u);
    fe_one (x2); fe_zero(z2);
    fe_copy(x3, x1); fe_one(z3);

    for (t = 254; t >= 0; t--) {
        uint32_t kt = (uint32_t)((s[t / 8] >> (t & 7)) & 1u);
        swap ^= kt;
        fe_cswap(x2, x3, swap);
        fe_cswap(z2, z3, swap);
        swap = kt;

        fe_add(a,  x2, z2);
        fe_square(aa, a);
        fe_sub(b_fe, x2, z2);
        fe_square(bb, b_fe);
        fe_sub(e,  aa, bb);
        fe_add(c,  x3, z3);
        fe_sub(d,  x3, z3);
        fe_mul(da, d, a);
        fe_mul(cb, c, b_fe);

        fe_add(x3, da, cb); fe_square(x3, x3);
        fe_sub(z3, da, cb); fe_square(z3, z3); fe_mul(z3, z3, x1);

        fe_mul(x2, aa, bb);
        fe_mul_u32(z2, e, 121665u);
        fe_add(z2, aa, z2);
        fe_mul(z2, e, z2);
    }

    fe_cswap(x2, x3, swap);
    fe_cswap(z2, z3, swap);

    fe_invert(z2, z2);
    fe_mul(x2, x2, z2);
    fe_to_bytes(out, x2);

    /* Wipe scratch. */
    for (i = 0; i < 32u; i++) s[i] = 0;
}
