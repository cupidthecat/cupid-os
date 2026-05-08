#ifndef CUPID_TLS_BIGINT_H
#define CUPID_TLS_BIGINT_H

#include "../types.h"

/* Fixed-size big-int for RSA modexp.
 *
 * Sized for RSA-4096 (covers ISRG Root X1 and friends). Limbs are
 * uint32_t in little-endian order - limbs[0] is least significant.
 * All operations are constant-shape with respect to nlimbs (no early
 * exits on data); they are NOT constant-time with respect to secret
 * inputs and are therefore appropriate for verify-only RSA where every
 * input is public (signature, modulus, message). */

#define BN_MAX_BITS    4096u
#define BN_MAX_LIMBS   (BN_MAX_BITS / 32u)         /* = 128 */
#define BN_WORK_LIMBS  (BN_MAX_LIMBS * 2u)         /* 8192-bit working room */

typedef struct {
    uint32_t limbs[BN_MAX_LIMBS];
} bn_t;

/* Wide form for products. */
typedef struct {
    uint32_t limbs[BN_WORK_LIMBS];
} bn_wide_t;

void bn_zero(bn_t *a);
void bn_set_u32(bn_t *a, uint32_t v);
void bn_copy(bn_t *dst, const bn_t *src);
int  bn_iszero(const bn_t *a);
int  bn_cmp(const bn_t *a, const bn_t *b);   /* -1 / 0 / +1 */

/* Decode/encode in big-endian byte order. `len` may be up to BN_MAX_BITS/8.
 * Excess high bits are zero-padded. Returns 0 on success, -1 if the input
 * is too large. */
int  bn_from_be(bn_t *out, const uint8_t *bytes, uint32_t len);
void bn_to_be(uint8_t *out, uint32_t len, const bn_t *a);

/* Returns the (carry / borrow) out. */
uint32_t bn_add(bn_t *dst, const bn_t *a, const bn_t *b);
uint32_t bn_sub(bn_t *dst, const bn_t *a, const bn_t *b);

/* Wide product: (2*N) x 32-bit result. */
void bn_mul(bn_wide_t *dst, const bn_t *a, const bn_t *b);

/* `r = a mod n`. `a` may be wide (after a multiply). */
void bn_mod_wide(bn_t *r, const bn_wide_t *a, const bn_t *n);

/* Modular exponentiation: r = base^exp mod n.
 * `exp` is supplied as a big-endian byte array (typical RSA "e" or "d"). */
void bn_modexp(bn_t *r, const bn_t *base,
               const uint8_t *exp_be, uint32_t exp_len,
               const bn_t *n);

#endif
