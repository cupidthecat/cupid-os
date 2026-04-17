#ifndef LIBM_H
#define LIBM_H

#include "types.h"

/* libm error flag — set to non-zero on domain or range errors.
 * 0 = ok, 1 = DOMAIN, 2 = RANGE. Not thread-local. */
extern int libm_errno;

/* Phase E Task 23: hardware fast-path libm operations (one x87/SSE
 * instruction each).
 *
 * These functions use a kernel-internal ABI tailored for CupidC kernel
 * bindings: cdecl stack-passed args (4 B for float, 8 B for double) and
 * the result returned in XMM0 (not ST(0), as System-V i386 would
 * dictate).  This matches the code CupidC emits for float/double calls
 * (Task 18).
 *
 * Implication: calling these from plain C within the kernel will NOT
 * work with the default GCC ABI.  No kernel C code calls them today,
 * and CupidC consumers see them via the BIND table.  If a C caller
 * becomes necessary later, wrap with a tiny trampoline that moves
 * XMM0 into ST(0) for the return. */
double sqrt (double x);
float  sqrtf(float x);
double sin  (double x);
float  sinf (float x);
double cos  (double x);
float  cosf (float x);
double tan  (double x);
float  tanf (float x);
double atan (double x);
float  atanf(float x);
double atan2(double y, double x);
float  atan2f(float y, float x);

/* Phase E Task 24: abs / rounding / fmod.  Same CupidC-internal ABI as
 * Task 23 (stack args, XMM0 return).  `round` uses x87 round-to-nearest-
 * even, which differs from C99 round-half-away-from-zero; see libm.c. */
double fabs  (double x);
float  fabsf (float x);
double floor (double x);
float  floorf(float x);
double ceil  (double x);
float  ceilf (float x);
double round (double x);
float  roundf(float x);
double trunc (double x);
float  truncf(float x);
double fmod  (double x, double y);
float  fmodf (float x, float y);

/* Phase E Task 25: exp / exp2 / log / log2 / pow + f-variants.  Same
 * CupidC-internal ABI as Tasks 23/24 (stack args, XMM0 return).
 *
 *   exp2   via x87 F2XM1 + FSCALE with FRNDINT range-reduce.
 *   exp    = exp2(x * log2(e))  — inlines the exp2 body after a multiply.
 *   log2   via x87 FYL2X with y=1.
 *   log    via x87 FYL2X with y=ln(2).
 *   pow(x,y) — domain-dispatched: y=0 -> 1; x<=0 with y!=0 sets
 *              libm_errno=1 and returns 0; otherwise exp(y*log(x)).
 *              (Integer-exponent negative-base special case is not
 *              implemented for this hobby kernel.) */
double exp   (double x);
float  expf  (float x);
double exp2  (double x);
float  exp2f (float x);
double log   (double x);
float  logf  (float x);
double log2  (double x);
float  log2f (float x);
double pow   (double x, double y);
float  powf  (float x, float y);

/* Phase E Task 26: asin / acos / sinh / cosh / tanh + f-variants.  Same
 * CupidC-internal ABI as Tasks 23/24/25 (stack args, XMM0 return).
 *
 *   asin(x)  = atan2(x, sqrt(1 - x*x))      domain |x| <= 1
 *   acos(x)  = atan2(sqrt(1 - x*x), x)      domain |x| <= 1
 *   sinh(x)  = (exp(x) - exp(-x)) / 2
 *   cosh(x)  = (exp(x) + exp(-x)) / 2
 *   tanh(x)  = (exp(x) - exp(-x)) / (exp(x) + exp(-x))
 *
 * asin/acos set libm_errno = 1 (DOMAIN) and return 0.0 when |x| > 1. */
double asin  (double x);
float  asinf (float x);
double acos  (double x);
float  acosf (float x);
double sinh  (double x);
float  sinhf (float x);
double cosh  (double x);
float  coshf (float x);
double tanh  (double x);
float  tanhf (float x);

/* Phase E Task 27: cbrt / hypot / nextafter + f-variants.  Same
 * CupidC-internal ABI as Tasks 23-26 (stack args, XMM0 return).
 *
 *   cbrt(x)       — bit-trick initial estimate (divide biased exponent
 *                   field by 3) followed by 3 Newton-Raphson iterations
 *                   of y = (2y + x/(y*y)) / 3.  Handles negative x by
 *                   sign factoring; handles 0 and denormals explicitly.
 *   hypot(x, y)   — scale-safe |x|,|y| → max * sqrt(1 + (min/max)^2),
 *                   avoiding intermediate overflow when x^2 would exceed
 *                   DBL_MAX even if sqrt(x^2+y^2) is representable.
 *   nextafter(x,y)— IEEE-754 bit-level increment/decrement of x toward y.
 *                   Walks the integer representation by ±1 ULP; handles
 *                   zero crossings and NaN (via x != y failing for NaN). */
double cbrt     (double x);
float  cbrtf    (float x);
double hypot    (double x, double y);
float  hypotf   (float x, float y);
double nextafter(double x, double y);
float  nextafterf(float x, float y);

#endif
