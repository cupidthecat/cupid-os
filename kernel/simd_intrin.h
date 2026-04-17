/**
 * simd_intrin.h - CupidC-recognized SSE packed intrinsics (Task 32).
 *
 * CupidC doesn't process #include directives, so this header is
 * documentation-only. Each name below is recognized by the CupidC code
 * generator and inlined as a single SSE instruction. See
 * kernel/cupidc_parse.c's `cc_intrin_table` for the full name->opcode
 * mapping and the shared `cc_emit_intrinsic` emitter.
 *
 * Codegen notes:
 *   - Two-operand intrinsics: arg0 is spilled to [ESP], arg1 evaluated
 *     into XMM0, then arg0 restored into XMM1. For commutative ops
 *     XMM0 = XMM0 <op> XMM1. For non-commutative (sub/div) we compute
 *     XMM1 <op>= XMM0 and then MOVAPS XMM0, XMM1 so the result is the
 *     conventional XMM0 accumulator. For cmpgt/cmpge we swap the
 *     operand roles and reuse the cmplt/cmple opcode with the same
 *     imm8 predicate.
 *   - One-operand intrinsics (sqrt): XMM0 = OP(XMM0).
 *   - _mm_set1_ps broadcasts its scalar argument to all four lanes
 *     via SHUFPS xmm0, xmm0, 0.
 *   - _mm_movemask_ps returns an int in EAX (not float4).
 */

#ifndef SIMD_INTRIN_H
#define SIMD_INTRIN_H

/* Arithmetic */
float4 _mm_add_ps(float4 a, float4 b);
float4 _mm_sub_ps(float4 a, float4 b);
float4 _mm_mul_ps(float4 a, float4 b);
float4 _mm_div_ps(float4 a, float4 b);
float4 _mm_sqrt_ps(float4 a);
float4 _mm_min_ps(float4 a, float4 b);
float4 _mm_max_ps(float4 a, float4 b);

/* Broadcast */
float4 _mm_set1_ps(float x);

/* Compare (returns all-ones mask per matching lane) */
float4 _mm_cmpeq_ps(float4 a, float4 b);
float4 _mm_cmplt_ps(float4 a, float4 b);
float4 _mm_cmple_ps(float4 a, float4 b);
float4 _mm_cmpgt_ps(float4 a, float4 b);  /* swap-operand + cmplt */
float4 _mm_cmpge_ps(float4 a, float4 b);  /* swap-operand + cmple */
float4 _mm_cmpneq_ps(float4 a, float4 b);

/* Bitwise */
float4 _mm_and_ps(float4 a, float4 b);
float4 _mm_or_ps(float4 a, float4 b);
float4 _mm_xor_ps(float4 a, float4 b);

/* Extract sign bits of 4 lanes into int (bit 0..3) */
int _mm_movemask_ps(float4 a);

/* ─── Task 33: double-precision packed (double2 = 2x double) ───────────
 * Same opcodes as the _ps variants with a 0x66 operand-size prefix.
 * Result is a double2. _mm_set1_pd broadcasts its scalar double into
 * both 64-bit lanes via SHUFPD xmm0,xmm0,0.
 */
double2 _mm_add_pd(double2 a, double2 b);
double2 _mm_sub_pd(double2 a, double2 b);
double2 _mm_mul_pd(double2 a, double2 b);
double2 _mm_div_pd(double2 a, double2 b);
double2 _mm_sqrt_pd(double2 a);
double2 _mm_min_pd(double2 a, double2 b);
double2 _mm_max_pd(double2 a, double2 b);
double2 _mm_and_pd(double2 a, double2 b);
double2 _mm_or_pd(double2 a, double2 b);
double2 _mm_xor_pd(double2 a, double2 b);
double2 _mm_set1_pd(double x);

#endif /* SIMD_INTRIN_H */
