#ifndef CUPID_TLS_P256_H
#define CUPID_TLS_P256_H

#include "../types.h"

/* NIST P-256 (secp256r1) curve operations.
 *
 * Field elements are 8 32-bit little-endian limbs (limb[0] is LS). The
 * curve is short Weierstrass y^2 = x^3 - 3x + b mod p, with
 *   p = 2^256 - 2^224 + 2^192 + 2^96 - 1
 *   n = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551
 *
 * The point representation alternates between affine (for I/O) and
 * Jacobian (X, Y, Z) for arithmetic.  All public scalar operations are
 * constant-time with respect to the scalar bits; field ops are NOT
 * constant-time with respect to the field-element values, which is fine
 * for verify-only paths and for ECDHE where the secret is the scalar
 * (not the field elements that flow through arithmetic).
 */

#define P256_LIMBS 8u
#define P256_BYTES 32u

typedef uint32_t p256_fe_t[P256_LIMBS];   /* field element */
typedef uint32_t p256_scalar_t[P256_LIMBS]; /* scalar mod n */

typedef struct {
    p256_fe_t X;
    p256_fe_t Y;
    p256_fe_t Z;          /* Z=0 => point at infinity */
} p256_jac_t;

typedef struct {
    p256_fe_t x;
    p256_fe_t y;
    int       infinity;   /* nonzero => point at infinity */
} p256_aff_t;

/* Curve parameters as field elements (defined in p256.c). */
extern const p256_fe_t P256_P;       /* prime */
extern const p256_fe_t P256_B;       /* curve constant */
extern const p256_fe_t P256_GX;
extern const p256_fe_t P256_GY;
extern const p256_fe_t P256_N;       /* group order */

/* Field ops mod p. */
void p256_fe_zero(p256_fe_t r);
void p256_fe_copy(p256_fe_t r, const p256_fe_t a);
int  p256_fe_iszero(const p256_fe_t a);
int  p256_fe_eq(const p256_fe_t a, const p256_fe_t b);
void p256_fe_add(p256_fe_t r, const p256_fe_t a, const p256_fe_t b);
void p256_fe_sub(p256_fe_t r, const p256_fe_t a, const p256_fe_t b);
void p256_fe_mul(p256_fe_t r, const p256_fe_t a, const p256_fe_t b);
void p256_fe_sqr(p256_fe_t r, const p256_fe_t a);
void p256_fe_inv(p256_fe_t r, const p256_fe_t a);   /* a != 0 */

/* Big-endian byte I/O for field elements (32 bytes). Returns 0 on
 * success, -1 if input >= p. */
int  p256_fe_from_be(p256_fe_t r, const uint8_t in[32]);
void p256_fe_to_be(uint8_t out[32], const p256_fe_t a);

/* Scalar arithmetic mod n. */
int  p256_scalar_from_be(p256_scalar_t r, const uint8_t in[32]); /* 0 ok, -1 if >= n */
void p256_scalar_to_be(uint8_t out[32], const p256_scalar_t a);
void p256_scalar_mod_n_from_be(p256_scalar_t r, const uint8_t *bytes, uint32_t len);
int  p256_scalar_iszero(const p256_scalar_t a);
void p256_scalar_inv(p256_scalar_t r, const p256_scalar_t a); /* a != 0 */
void p256_scalar_mul(p256_scalar_t r, const p256_scalar_t a, const p256_scalar_t b);

/* Point operations. */
void p256_jac_set_infinity(p256_jac_t *r);
int  p256_jac_is_infinity(const p256_jac_t *r);
void p256_jac_double(p256_jac_t *r, const p256_jac_t *a);
void p256_jac_add(p256_jac_t *r, const p256_jac_t *a, const p256_jac_t *b);
void p256_jac_to_affine(p256_aff_t *out, const p256_jac_t *in);
void p256_jac_from_affine(p256_jac_t *out, const p256_aff_t *in);

/* Validate that (x,y) lies on the curve. Returns 1 if on-curve and not
 * the identity, 0 otherwise. */
int  p256_aff_is_on_curve(const p256_aff_t *p);

/* Constant-time scalar multiplication: r = k * P. */
void p256_scalar_mul_point(p256_jac_t *r, const p256_scalar_t k,
                           const p256_aff_t *P);

/* Double-scalar mul (verify path, NOT constant-time): r = u1*G + u2*Q. */
void p256_double_scalar_mul(p256_jac_t *r,
                            const p256_scalar_t u1,
                            const p256_scalar_t u2,
                            const p256_aff_t *Q);

/* Public-key import: parse uncompressed SEC1 point (0x04 || X || Y),
 * verify on-curve. Returns 0 on success. */
int  p256_pub_from_uncompressed(p256_aff_t *out,
                                const uint8_t *enc, uint32_t enc_len);

#endif
