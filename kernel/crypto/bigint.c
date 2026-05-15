/* Fixed-size 4096-bit big-int for RSA-PSS / PKCS#1 verify.
 *
 * Schoolbook multiply, bit-by-bit reduction. Slow (tens of ms per 4096-
 * bit modexp in QEMU) but compact and correct. Variable-time - fine
 * because RSA verify operates only on public inputs.*/

#include "bigint.h"

void bn_zero(bn_t *a) {
    uint32_t i;
    for (i = 0; i < BN_MAX_LIMBS; i++) a->limbs[i] = 0;
}

void bn_set_u32(bn_t *a, uint32_t v) {
    bn_zero(a);
    a->limbs[0] = v;
}

void bn_copy(bn_t *dst, const bn_t *src) {
    uint32_t i;
    for (i = 0; i < BN_MAX_LIMBS; i++) dst->limbs[i] = src->limbs[i];
}

int bn_iszero(const bn_t *a) {
    uint32_t i;
    for (i = 0; i < BN_MAX_LIMBS; i++) if (a->limbs[i]) return 0;
    return 1;
}

int bn_cmp(const bn_t *a, const bn_t *b) {
    int32_t i;
    for (i = (int32_t)BN_MAX_LIMBS - 1; i >= 0; i--) {
        if (a->limbs[i] > b->limbs[i]) return 1;
        if (a->limbs[i] < b->limbs[i]) return -1;
    }
    return 0;
}

int bn_from_be(bn_t *out, const uint8_t *bytes, uint32_t len) {
    uint32_t i;
    if (len > BN_MAX_LIMBS * 4u) return -1;
    bn_zero(out);
    /* bytes[0] is most-significant; assemble limb 0 from the LSB tail. */
    for (i = 0; i < len; i++) {
        uint32_t shift = (uint32_t)((len - 1u - i) & 3u) * 8u;
        uint32_t limb  = (uint32_t)((len - 1u - i) >> 2);
        out->limbs[limb] |= (uint32_t)((uint32_t)bytes[i] << shift);
    }
    return 0;
}

void bn_to_be(uint8_t *out, uint32_t len, const bn_t *a) {
    uint32_t i;
    for (i = 0; i < len; i++) {
        uint32_t shift = (uint32_t)((len - 1u - i) & 3u) * 8u;
        uint32_t limb  = (uint32_t)((len - 1u - i) >> 2);
        if (limb < BN_MAX_LIMBS) {
            out[i] = (uint8_t)((a->limbs[limb] >> shift) & 0xFFu);
        } else {
            out[i] = 0;
        }
    }
}

uint32_t bn_add(bn_t *dst, const bn_t *a, const bn_t *b) {
    uint64_t carry = 0;
    uint32_t i;
    for (i = 0; i < BN_MAX_LIMBS; i++) {
        uint64_t sum = (uint64_t)a->limbs[i] + b->limbs[i] + carry;
        dst->limbs[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    return (uint32_t)carry;
}

uint32_t bn_sub(bn_t *dst, const bn_t *a, const bn_t *b) {
    uint64_t borrow = 0;
    uint32_t i;
    for (i = 0; i < BN_MAX_LIMBS; i++) {
        uint64_t diff = (uint64_t)a->limbs[i] - b->limbs[i] - borrow;
        dst->limbs[i] = (uint32_t)diff;
        borrow = (diff >> 32) & 1u;
    }
    return (uint32_t)borrow;
}

void bn_mul(bn_wide_t *dst, const bn_t *a, const bn_t *b) {
    uint32_t i, j;
    for (i = 0; i < BN_WORK_LIMBS; i++) dst->limbs[i] = 0;
    for (i = 0; i < BN_MAX_LIMBS; i++) {
        uint64_t carry = 0;
        for (j = 0; j < BN_MAX_LIMBS; j++) {
            uint64_t prod = (uint64_t)a->limbs[i] * b->limbs[j]
                          + dst->limbs[i + j] + carry;
            dst->limbs[i + j] = (uint32_t)prod;
            carry = prod >> 32;
        }
        dst->limbs[i + BN_MAX_LIMBS] = (uint32_t)carry;
    }
}

/* Bit length of n (highest set bit + 1). 0 if zero. */
static uint32_t bn_bits(const bn_t *n) {
    int32_t i;
    for (i = (int32_t)BN_MAX_LIMBS - 1; i >= 0; i--) {
        if (n->limbs[i]) {
            uint32_t v = n->limbs[i];
            uint32_t bit = 32u;
            while ((v & 0x80000000u) == 0u) { v <<= 1; bit--; }
            return (uint32_t)i * 32u + bit;
        }
    }
    return 0;
}

/* Wide compare: a vs (n << shift_bits). Returns -1/0/+1. shift_bits ≤ 4096. */
static int bn_wide_cmp_shifted(const bn_wide_t *a, const bn_t *n,
                               uint32_t shift_bits) {
    int32_t i;
    uint32_t limb_shift = shift_bits / 32u;
    uint32_t bit_shift  = shift_bits & 31u;
    for (i = (int32_t)BN_WORK_LIMBS - 1; i >= 0; i--) {
        uint32_t nv = 0;
        int32_t  src = (int32_t)i - (int32_t)limb_shift;
        if (bit_shift == 0u) {
            if (src >= 0 && src < (int32_t)BN_MAX_LIMBS) nv = n->limbs[src];
        } else {
            uint32_t hi = 0, lo = 0;
            if (src >= 0 && src < (int32_t)BN_MAX_LIMBS) lo = n->limbs[src];
            if (src - 1 >= 0 && src - 1 < (int32_t)BN_MAX_LIMBS) hi = n->limbs[src - 1];
            nv = (uint32_t)((lo << bit_shift) | (hi >> (32u - bit_shift)));
        }
        if (a->limbs[i] > nv) return 1;
        if (a->limbs[i] < nv) return -1;
    }
    return 0;
}

/* In-place a -= (n << shift_bits). Caller has verified a >= shifted n. */
static void bn_wide_sub_shifted(bn_wide_t *a, const bn_t *n,
                                uint32_t shift_bits) {
    uint32_t limb_shift = shift_bits / 32u;
    uint32_t bit_shift  = shift_bits & 31u;
    uint64_t borrow = 0;
    uint32_t i;
    for (i = 0; i < BN_WORK_LIMBS; i++) {
        uint32_t nv = 0;
        int32_t  src = (int32_t)i - (int32_t)limb_shift;
        if (bit_shift == 0u) {
            if (src >= 0 && src < (int32_t)BN_MAX_LIMBS) nv = n->limbs[src];
        } else {
            uint32_t hi = 0, lo = 0;
            if (src >= 0 && src < (int32_t)BN_MAX_LIMBS) lo = n->limbs[src];
            if (src - 1 >= 0 && src - 1 < (int32_t)BN_MAX_LIMBS) hi = n->limbs[src - 1];
            nv = (uint32_t)((lo << bit_shift) | (hi >> (32u - bit_shift)));
        }
        {
            uint64_t diff = (uint64_t)a->limbs[i] - nv - borrow;
            a->limbs[i] = (uint32_t)diff;
            borrow = (diff >> 32) & 1u;
        }
    }
}

void bn_mod_wide(bn_t *r, const bn_wide_t *a_in, const bn_t *n) {
    bn_wide_t a;
    uint32_t  nbits, abits;
    int32_t   i;
    uint32_t  k;

    /* Copy because we mutate. */
    for (k = 0; k < BN_WORK_LIMBS; k++) a.limbs[k] = a_in->limbs[k];

    nbits = bn_bits(n);
    if (nbits == 0u) {
        /* Division by zero - caller bug. Return zero. */
        bn_zero(r);
        return;
    }

    /* Find a's bit length. */
    abits = 0;
    for (i = (int32_t)BN_WORK_LIMBS - 1; i >= 0; i--) {
        if (a.limbs[i]) {
            uint32_t v = a.limbs[i];
            uint32_t bit = 32u;
            while ((v & 0x80000000u) == 0u) { v <<= 1; bit--; }
            abits = (uint32_t)i * 32u + bit;
            break;
        }
    }

    if (abits >= nbits) {
        int32_t shift = (int32_t)abits - (int32_t)nbits;
        for (; shift >= 0; shift--) {
            if (bn_wide_cmp_shifted(&a, n, (uint32_t)shift) >= 0) {
                bn_wide_sub_shifted(&a, n, (uint32_t)shift);
            }
        }
    }

    /* Low BN_MAX_LIMBS of a is the remainder. High should be zero. */
    for (k = 0; k < BN_MAX_LIMBS; k++) r->limbs[k] = a.limbs[k];
}

void bn_modexp(bn_t *r, const bn_t *base,
               const uint8_t *exp_be, uint32_t exp_len,
               const bn_t *n) {
    bn_t      acc;
    bn_wide_t prod;
    uint32_t  i;
    int       started = 0;

    bn_set_u32(&acc, 1u);

    for (i = 0; i < exp_len; i++) {
        uint8_t byte = exp_be[i];
        int     bit;
        for (bit = 7; bit >= 0; bit--) {
            int b = (byte >> bit) & 1;
            if (started) {
                bn_mul(&prod, &acc, &acc);
                bn_mod_wide(&acc, &prod, n);
            }
            if (b) {
                if (!started) {
                    bn_copy(&acc, base);
                    started = 1;
                } else {
                    bn_mul(&prod, &acc, base);
                    bn_mod_wide(&acc, &prod, n);
                }
            }
        }
    }

    if (!started) {
        /* Exponent was zero - result is 1 mod n. */
        bn_set_u32(r, 1u);
    } else {
        bn_copy(r, &acc);
    }
}
