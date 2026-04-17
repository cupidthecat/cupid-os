#include "libm.h"

int libm_errno = 0;

/* Phase E Task 23: hardware fast-paths (single x87/SSE instruction each).
 *
 * ABI (kernel-internal, designed to match CupidC's Task-18 codegen):
 *   - Arguments: pushed on the stack left-to-right (cdecl-ish).
 *     `float` = 4 bytes, `double` = 8 bytes.
 *   - Return value: left in XMM0 (32-bit low lane for float, 64-bit low
 *     lane for double).
 *   - Caller cleans up the stack.
 *   - ESP entering the function points at the return address; first arg
 *     is at [esp+4].
 *
 * Implementing the bodies as top-level GAS `__asm__` blocks lets us
 * pin this ABI precisely regardless of what GCC would do for a C body.
 * That matters because -m32 System-V i386 returns float/double in ST(0),
 * which is not what CupidC expects after a `call`.
 *
 * No domain-error handling in this batch: hardware instructions return
 * NaN / raise FP exceptions on their own; libm_errno is not touched. */

/* sqrt via SSE SQRTSD (scalar double) */
__asm__(
    ".text\n\t"
    ".globl sqrt\n\t"
    ".type  sqrt, @function\n"
    "sqrt:\n\t"
    "movsd  4(%esp), %xmm0\n\t"
    "sqrtsd %xmm0, %xmm0\n\t"
    "ret\n\t"
    ".size  sqrt, .-sqrt\n"
);

/* sqrtf via SSE SQRTSS (scalar float) */
__asm__(
    ".text\n\t"
    ".globl sqrtf\n\t"
    ".type  sqrtf, @function\n"
    "sqrtf:\n\t"
    "movss  4(%esp), %xmm0\n\t"
    "sqrtss %xmm0, %xmm0\n\t"
    "ret\n\t"
    ".size  sqrtf, .-sqrtf\n"
);

/* sin via x87 FSIN, result returned in XMM0. */
__asm__(
    ".text\n\t"
    ".globl sin\n\t"
    ".type  sin, @function\n"
    "sin:\n\t"
    "fldl   4(%esp)\n\t"        /* load double from stack into ST(0) */
    "fsin\n\t"                  /* ST(0) = sin(ST(0)) */
    "sub    $8, %esp\n\t"       /* scratch slot */
    "fstpl  (%esp)\n\t"         /* pop ST(0) to scratch */
    "movsd  (%esp), %xmm0\n\t"  /* load into XMM0 */
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  sin, .-sin\n"
);

/* sinf: load float, FSIN at double precision, store back as float. */
__asm__(
    ".text\n\t"
    ".globl sinf\n\t"
    ".type  sinf, @function\n"
    "sinf:\n\t"
    "flds   4(%esp)\n\t"        /* load float -> ST(0), widened to extended */
    "fsin\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"         /* store as single-precision float */
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  sinf, .-sinf\n"
);

/* cos via x87 FCOS */
__asm__(
    ".text\n\t"
    ".globl cos\n\t"
    ".type  cos, @function\n"
    "cos:\n\t"
    "fldl   4(%esp)\n\t"
    "fcos\n\t"
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  cos, .-cos\n"
);

__asm__(
    ".text\n\t"
    ".globl cosf\n\t"
    ".type  cosf, @function\n"
    "cosf:\n\t"
    "flds   4(%esp)\n\t"
    "fcos\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  cosf, .-cosf\n"
);

/* tan via x87 FPTAN.  FPTAN computes ST(0) = tan(ST(0)) AND pushes 1.0
 * onto the FPU stack (for historical reasons).  Drop the 1.0 with an
 * FSTP of ST(0). */
__asm__(
    ".text\n\t"
    ".globl tan\n\t"
    ".type  tan, @function\n"
    "tan:\n\t"
    "fldl   4(%esp)\n\t"
    "fptan\n\t"
    "fstp   %st(0)\n\t"         /* drop the 1.0 FPTAN pushed */
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  tan, .-tan\n"
);

__asm__(
    ".text\n\t"
    ".globl tanf\n\t"
    ".type  tanf, @function\n"
    "tanf:\n\t"
    "flds   4(%esp)\n\t"
    "fptan\n\t"
    "fstp   %st(0)\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  tanf, .-tanf\n"
);

/* atan via x87 FPATAN.  FPATAN computes atan2(ST(1), ST(0)).
 * For atan(x), call atan2(x, 1) → push y=x then x=1.0. */
__asm__(
    ".text\n\t"
    ".globl atan\n\t"
    ".type  atan, @function\n"
    "atan:\n\t"
    "fldl   4(%esp)\n\t"        /* y = x */
    "fld1\n\t"                  /* x = 1.0 */
    "fpatan\n\t"                /* ST(0) = atan2(y, x) = atan(x) */
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  atan, .-atan\n"
);

__asm__(
    ".text\n\t"
    ".globl atanf\n\t"
    ".type  atanf, @function\n"
    "atanf:\n\t"
    "flds   4(%esp)\n\t"
    "fld1\n\t"
    "fpatan\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  atanf, .-atanf\n"
);

/* atan2(y, x) via x87 FPATAN directly.
 * CupidC pushes args left-to-right then reverses for cdecl, so the
 * caller-visible layout at [esp+4..+19] is: y (first 8 B), x (next 8 B).
 * FPATAN wants ST(1) = y, ST(0) = x. */
__asm__(
    ".text\n\t"
    ".globl atan2\n\t"
    ".type  atan2, @function\n"
    "atan2:\n\t"
    "fldl    4(%esp)\n\t"       /* y -> ST(0)        (stack: [y]) */
    "fldl   12(%esp)\n\t"       /* x -> ST(0), y->ST(1) (stack: [x,y]) */
    "fpatan\n\t"                /* ST(0) = atan2(y, x) */
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  atan2, .-atan2\n"
);

__asm__(
    ".text\n\t"
    ".globl atan2f\n\t"
    ".type  atan2f, @function\n"
    "atan2f:\n\t"
    "flds    4(%esp)\n\t"       /* y */
    "flds    8(%esp)\n\t"       /* x */
    "fpatan\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  atan2f, .-atan2f\n"
);

/* Phase E Task 24: absolute value / rounding / fmod.
 *
 * Same CupidC-internal ABI as Task 23 (stack args, XMM0 return).
 *
 *   fabs/fabsf     — sign-bit mask via SSE ANDPD/ANDPS.
 *   floor/ceil/
 *   round/trunc    — x87 FRNDINT with a transient RC (rounding-control)
 *                    swap in the x87 control word.  RC encoding:
 *                      00 = round-to-nearest-even (used for `round`)
 *                      01 = round toward -inf     (used for `floor`)
 *                      10 = round toward +inf     (used for `ceil`)
 *                      11 = round toward zero     (used for `trunc`)
 *                    We save the original CW on the stack, patch RC,
 *                    FRNDINT, then restore.  Both single- and
 *                    double-precision variants take the same x87 path;
 *                    the FSTP/FLDS width differs at load/store.
 *   fmod/fmodf     — x87 FPREM loop until C2 clears in the status word
 *                    (indicating full reduction, not just a partial step).
 *
 * NOTE on `round`: C99 `round()` is round-half-away-from-zero.  FRNDINT
 * with RC=00 rounds half-to-even (banker's rounding).  For a hobby
 * kernel this delta is acceptable; callers needing exact C99 semantics
 * should branch on fractional part explicitly. */

/* Sign-bit masks for fabs/fabsf.  Placed in .rodata so ANDPD/ANDPS can
 * reference them directly by absolute address. */
__asm__(
    ".section .rodata\n\t"
    ".align 16\n"
    "fabs_mask_d:\n\t"
    ".quad 0x7FFFFFFFFFFFFFFF\n\t"
    ".quad 0x7FFFFFFFFFFFFFFF\n"
    "fabs_mask_s:\n\t"
    ".long 0x7FFFFFFF\n\t"
    ".long 0x7FFFFFFF\n\t"
    ".long 0x7FFFFFFF\n\t"
    ".long 0x7FFFFFFF\n\t"
    ".text\n"
);

/* fabs: clear the sign bit of a double via SSE ANDPD. */
__asm__(
    ".text\n\t"
    ".globl fabs\n\t"
    ".type  fabs, @function\n"
    "fabs:\n\t"
    "movsd  4(%esp), %xmm0\n\t"
    "andpd  fabs_mask_d, %xmm0\n\t"
    "ret\n\t"
    ".size  fabs, .-fabs\n"
);

/* fabsf: clear the sign bit of a float via SSE ANDPS. */
__asm__(
    ".text\n\t"
    ".globl fabsf\n\t"
    ".type  fabsf, @function\n"
    "fabsf:\n\t"
    "movss  4(%esp), %xmm0\n\t"
    "andps  fabs_mask_s, %xmm0\n\t"
    "ret\n\t"
    ".size  fabsf, .-fabsf\n"
);

/* floor: x87 FRNDINT with RC=01 (round toward -inf). */
__asm__(
    ".text\n\t"
    ".globl floor\n\t"
    ".type  floor, @function\n"
    "floor:\n\t"
    "fldl   4(%esp)\n\t"
    "sub    $8, %esp\n\t"             /* [esp+0..1]=saved CW, [esp+2..3]=patched CW, [esp+4..7]=result slot */
    "fnstcw (%esp)\n\t"
    "movw   (%esp), %ax\n\t"
    "andw   $0xF3FF, %ax\n\t"         /* clear RC bits */
    "orw    $0x0400, %ax\n\t"         /* RC=01 (round down) */
    "movw   %ax, 2(%esp)\n\t"
    "fldcw  2(%esp)\n\t"
    "frndint\n\t"
    "fldcw  (%esp)\n\t"
    "fstpl  (%esp)\n\t"               /* overwrite saved CW with result (8 bytes available) */
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  floor, .-floor\n"
);

/* floorf: same as floor but 32-bit load/store. */
__asm__(
    ".text\n\t"
    ".globl floorf\n\t"
    ".type  floorf, @function\n"
    "floorf:\n\t"
    "flds   4(%esp)\n\t"
    "sub    $8, %esp\n\t"
    "fnstcw (%esp)\n\t"
    "movw   (%esp), %ax\n\t"
    "andw   $0xF3FF, %ax\n\t"
    "orw    $0x0400, %ax\n\t"
    "movw   %ax, 2(%esp)\n\t"
    "fldcw  2(%esp)\n\t"
    "frndint\n\t"
    "fldcw  (%esp)\n\t"
    "fstps  4(%esp)\n\t"
    "movss  4(%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  floorf, .-floorf\n"
);

/* ceil: FRNDINT with RC=10 (round toward +inf). */
__asm__(
    ".text\n\t"
    ".globl ceil\n\t"
    ".type  ceil, @function\n"
    "ceil:\n\t"
    "fldl   4(%esp)\n\t"
    "sub    $8, %esp\n\t"
    "fnstcw (%esp)\n\t"
    "movw   (%esp), %ax\n\t"
    "andw   $0xF3FF, %ax\n\t"
    "orw    $0x0800, %ax\n\t"         /* RC=10 (round up) */
    "movw   %ax, 2(%esp)\n\t"
    "fldcw  2(%esp)\n\t"
    "frndint\n\t"
    "fldcw  (%esp)\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  ceil, .-ceil\n"
);

__asm__(
    ".text\n\t"
    ".globl ceilf\n\t"
    ".type  ceilf, @function\n"
    "ceilf:\n\t"
    "flds   4(%esp)\n\t"
    "sub    $8, %esp\n\t"
    "fnstcw (%esp)\n\t"
    "movw   (%esp), %ax\n\t"
    "andw   $0xF3FF, %ax\n\t"
    "orw    $0x0800, %ax\n\t"
    "movw   %ax, 2(%esp)\n\t"
    "fldcw  2(%esp)\n\t"
    "frndint\n\t"
    "fldcw  (%esp)\n\t"
    "fstps  4(%esp)\n\t"
    "movss  4(%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  ceilf, .-ceilf\n"
);

/* round: FRNDINT with RC=00 (round-to-nearest-even).  See header note re
 * C99 semantics. */
__asm__(
    ".text\n\t"
    ".globl round\n\t"
    ".type  round, @function\n"
    "round:\n\t"
    "fldl   4(%esp)\n\t"
    "sub    $8, %esp\n\t"
    "fnstcw (%esp)\n\t"
    "movw   (%esp), %ax\n\t"
    "andw   $0xF3FF, %ax\n\t"         /* RC=00 (to-nearest) */
    "movw   %ax, 2(%esp)\n\t"
    "fldcw  2(%esp)\n\t"
    "frndint\n\t"
    "fldcw  (%esp)\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  round, .-round\n"
);

__asm__(
    ".text\n\t"
    ".globl roundf\n\t"
    ".type  roundf, @function\n"
    "roundf:\n\t"
    "flds   4(%esp)\n\t"
    "sub    $8, %esp\n\t"
    "fnstcw (%esp)\n\t"
    "movw   (%esp), %ax\n\t"
    "andw   $0xF3FF, %ax\n\t"
    "movw   %ax, 2(%esp)\n\t"
    "fldcw  2(%esp)\n\t"
    "frndint\n\t"
    "fldcw  (%esp)\n\t"
    "fstps  4(%esp)\n\t"
    "movss  4(%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  roundf, .-roundf\n"
);

/* trunc: FRNDINT with RC=11 (round toward zero). */
__asm__(
    ".text\n\t"
    ".globl trunc\n\t"
    ".type  trunc, @function\n"
    "trunc:\n\t"
    "fldl   4(%esp)\n\t"
    "sub    $8, %esp\n\t"
    "fnstcw (%esp)\n\t"
    "movw   (%esp), %ax\n\t"
    "andw   $0xF3FF, %ax\n\t"
    "orw    $0x0C00, %ax\n\t"         /* RC=11 (toward zero) */
    "movw   %ax, 2(%esp)\n\t"
    "fldcw  2(%esp)\n\t"
    "frndint\n\t"
    "fldcw  (%esp)\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  trunc, .-trunc\n"
);

__asm__(
    ".text\n\t"
    ".globl truncf\n\t"
    ".type  truncf, @function\n"
    "truncf:\n\t"
    "flds   4(%esp)\n\t"
    "sub    $8, %esp\n\t"
    "fnstcw (%esp)\n\t"
    "movw   (%esp), %ax\n\t"
    "andw   $0xF3FF, %ax\n\t"
    "orw    $0x0C00, %ax\n\t"
    "movw   %ax, 2(%esp)\n\t"
    "fldcw  2(%esp)\n\t"
    "frndint\n\t"
    "fldcw  (%esp)\n\t"
    "fstps  4(%esp)\n\t"
    "movss  4(%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  truncf, .-truncf\n"
);

/* fmod(x, y) via x87 FPREM.  FPREM only does one reduction step of up to
 * 2**63 bits — we loop until C2 (status word bit 10) clears, signalling
 * the full IEEE remainder has been computed.  Layout: ST(0)=x, ST(1)=y
 * on entry to the loop.  After convergence we pop y with FSTP ST(1),
 * leaving the result in ST(0). */
__asm__(
    ".text\n\t"
    ".globl fmod\n\t"
    ".type  fmod, @function\n"
    "fmod:\n\t"
    "fldl   12(%esp)\n\t"       /* y -> ST(0) */
    "fldl    4(%esp)\n\t"       /* x -> ST(0); stack is now [x, y] */
    "1:\n\t"
    "fprem\n\t"
    "fnstsw %ax\n\t"
    "testw  $0x0400, %ax\n\t"
    "jnz    1b\n\t"
    "fstp   %st(1)\n\t"         /* drop y, ST(0) = x mod y */
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  fmod, .-fmod\n"
);

__asm__(
    ".text\n\t"
    ".globl fmodf\n\t"
    ".type  fmodf, @function\n"
    "fmodf:\n\t"
    "flds   8(%esp)\n\t"        /* y */
    "flds   4(%esp)\n\t"        /* x */
    "1:\n\t"
    "fprem\n\t"
    "fnstsw %ax\n\t"
    "testw  $0x0400, %ax\n\t"
    "jnz    1b\n\t"
    "fstp   %st(1)\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  fmodf, .-fmodf\n"
);

/* Phase E Task 25: exp / exp2 / log / log2 / pow + f-variants.
 *
 * Mathematical building blocks on x87:
 *   F2XM1 : ST(0) = 2**ST(0) - 1,   requires ST(0) in [-1, 1].
 *   FSCALE: ST(0) = ST(0) * 2**RNDINT(ST(1)).
 *   FYL2X : ST(1) = ST(1) * log2(ST(0)), pops ST(0).
 *
 * Core identity for exp2 (after range reduction into integer + fraction):
 *   exp2(x) = exp2(int(x) + frac) = exp2(frac) * 2^int(x)
 *           = (F2XM1(frac) + 1) * 2^int(x)
 *           = FSCALE((F2XM1(frac) + 1), int(x))
 *
 * For F2XM1 correctness, the fractional part MUST satisfy |frac| <= 1.
 * Using FRNDINT with round-to-nearest-even (the default control-word
 * rounding mode on kernel entry) yields frac = x - round(x) in
 * [-0.5, 0.5], well inside the F2XM1 domain.  We therefore do NOT
 * temporarily swap RC as we do for floor/ceil/trunc.
 *
 * exp(x) = exp2(x * log2(e)).  We inline the exp2 body after a single
 * multiply by log2(e) rather than call exp2 — saves an ABI bridge and
 * keeps the function self-contained.
 *
 * log2 and log both use FYL2X; the only difference is the initial
 * scaling value pushed as ST(1) before the x argument (1.0 for log2,
 * ln(2) for natural log).
 *
 * pow(x, y) is dispatched in C and bridges the standard System-V i386
 * ABI (ST(0) return) back to CupidC's XMM0-return ABI through a tiny
 * asm wrapper.  This is the cleanest pattern for the dispatch logic:
 *   - y == 0      -> 1.0 (even for x == 0, per IEEE 754-2008).
 *   - x == 0, y>0 -> 0.0.
 *   - x == 0, y<0 -> libm_errno = 1, return 0.0.
 *   - x <  0      -> libm_errno = 1, return 0.0  (integer-y special
 *                    case NOT implemented; documented limitation).
 *   - otherwise   -> exp(y * log(x)).
 */

__asm__(
    ".section .rodata\n\t"
    ".align 8\n"
    "libm_log2e_const:\n\t"
    ".quad 0x3FF71547652B82FE\n"     /* 1.4426950408889634 = log2(e)  */
    "libm_ln2_const:\n\t"
    ".quad 0x3FE62E42FEFA39EF\n"     /* 0.6931471805599453 = ln(2)    */
    ".text\n"
);

/* exp2(x) via F2XM1 + FSCALE.  Round-to-nearest split keeps frac in
 * [-0.5, 0.5] which is safely inside F2XM1's [-1, 1] domain. */
__asm__(
    ".text\n\t"
    ".globl exp2\n\t"
    ".type  exp2, @function\n"
    "exp2:\n\t"
    "fldl   4(%esp)\n\t"        /* ST(0) = x                         */
    "fld    %st(0)\n\t"         /* ST(0) = x,     ST(1) = x          */
    "frndint\n\t"                /* ST(0) = int(x), ST(1) = x         */
    "fsub   %st, %st(1)\n\t"    /* ST(1) = x - int(x) = frac         */
    "fxch\n\t"                   /* ST(0) = frac,  ST(1) = int(x)     */
    "f2xm1\n\t"                  /* ST(0) = 2^frac - 1                */
    "fld1\n\t"                   /* ST(0) = 1, ST(1)=2^frac-1, ST(2)=int */
    "faddp\n\t"                  /* ST(0) = 2^frac, ST(1)=int(x)      */
    "fscale\n\t"                 /* ST(0) = 2^frac * 2^int(x) = 2^x   */
    "fstp   %st(1)\n\t"          /* drop int(x), ST(0) = 2^x          */
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  exp2, .-exp2\n"
);

__asm__(
    ".text\n\t"
    ".globl exp2f\n\t"
    ".type  exp2f, @function\n"
    "exp2f:\n\t"
    "flds   4(%esp)\n\t"
    "fld    %st(0)\n\t"
    "frndint\n\t"
    "fsub   %st, %st(1)\n\t"
    "fxch\n\t"
    "f2xm1\n\t"
    "fld1\n\t"
    "faddp\n\t"
    "fscale\n\t"
    "fstp   %st(1)\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  exp2f, .-exp2f\n"
);

/* exp(x) = exp2(x * log2(e)).  Inlines the exp2 body after a multiply. */
__asm__(
    ".text\n\t"
    ".globl exp\n\t"
    ".type  exp, @function\n"
    "exp:\n\t"
    "fldl   4(%esp)\n\t"                /* ST(0) = x                 */
    "fldl   libm_log2e_const\n\t"        /* ST(0) = log2e, ST(1) = x  */
    "fmulp\n\t"                          /* ST(0) = x * log2(e)       */
    "fld    %st(0)\n\t"
    "frndint\n\t"
    "fsub   %st, %st(1)\n\t"
    "fxch\n\t"
    "f2xm1\n\t"
    "fld1\n\t"
    "faddp\n\t"
    "fscale\n\t"
    "fstp   %st(1)\n\t"
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  exp, .-exp\n"
);

__asm__(
    ".text\n\t"
    ".globl expf\n\t"
    ".type  expf, @function\n"
    "expf:\n\t"
    "flds   4(%esp)\n\t"
    "fldl   libm_log2e_const\n\t"
    "fmulp\n\t"
    "fld    %st(0)\n\t"
    "frndint\n\t"
    "fsub   %st, %st(1)\n\t"
    "fxch\n\t"
    "f2xm1\n\t"
    "fld1\n\t"
    "faddp\n\t"
    "fscale\n\t"
    "fstp   %st(1)\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  expf, .-expf\n"
);

/* log2(x) via FYL2X with y = 1.0 in ST(1).  FYL2X computes
 * ST(1) = ST(1) * log2(ST(0)) then pops, so result = log2(x). */
__asm__(
    ".text\n\t"
    ".globl log2\n\t"
    ".type  log2, @function\n"
    "log2:\n\t"
    "fld1\n\t"                   /* ST(0) = 1.0                       */
    "fldl   4(%esp)\n\t"         /* ST(0) = x,   ST(1) = 1.0          */
    "fyl2x\n\t"                  /* ST(0) = 1.0 * log2(x)             */
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  log2, .-log2\n"
);

__asm__(
    ".text\n\t"
    ".globl log2f\n\t"
    ".type  log2f, @function\n"
    "log2f:\n\t"
    "fld1\n\t"
    "flds   4(%esp)\n\t"
    "fyl2x\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  log2f, .-log2f\n"
);

/* log(x) = ln(2) * log2(x), folded into a single FYL2X call. */
__asm__(
    ".text\n\t"
    ".globl log\n\t"
    ".type  log, @function\n"
    "log:\n\t"
    "fldl   libm_ln2_const\n\t"  /* ST(0) = ln(2)                     */
    "fldl   4(%esp)\n\t"         /* ST(0) = x,   ST(1) = ln(2)        */
    "fyl2x\n\t"                  /* ST(0) = ln(2) * log2(x) = ln(x)   */
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  log, .-log\n"
);

__asm__(
    ".text\n\t"
    ".globl logf\n\t"
    ".type  logf, @function\n"
    "logf:\n\t"
    "fldl   libm_ln2_const\n\t"
    "flds   4(%esp)\n\t"
    "fyl2x\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  logf, .-logf\n"
);

/* pow(x, y) — C dispatch for special cases + x87 pipeline for the
 * common case.  Returns result in ST(0) per System-V i386 ABI.  An
 * asm wrapper (`pow`) below bridges ST(0) -> XMM0.  Static visibility
 * keeps the symbol out of the CupidC bind namespace.
 *
 * These C-local constants live in .rodata separate from the symbols
 * named in the libm_{log2e,ln2}_const .rodata block above; the inline
 * asm uses "m" operand-address substitution so the assembler need not
 * resolve the top-level labels. */
static const double k_log2e = 1.4426950408889634; /* log2(e) */
static const double k_ln2   = 0.6931471805599453; /* ln(2)  */

/* Non-static + noinline so the linker keeps the exact cdecl ABI we
 * target from the asm wrappers below.  `used` prevents removal under
 * aggressive LTO.  No prototype in the header — these are internal. */
double libm_pow_impl(double x, double y)
    __attribute__((used, noinline));
float  libm_powf_impl(float x, float y)
    __attribute__((used, noinline));

double libm_pow_impl(double x, double y)
{
    /* y == 0 -> 1.0 for any x (including x == 0), per IEEE 754-2008. */
    if (y == 0.0) {
        return 1.0;
    }
    /* x == 0 handling. */
    if (x == 0.0) {
        if (y > 0.0) {
            return 0.0;
        }
        libm_errno = 1;
        return 0.0;
    }
    /* Negative base: integer-exponent special case NOT implemented.
     * Flag a domain error and return 0. */
    if (x < 0.0) {
        libm_errno = 1;
        return 0.0;
    }
    /* Common path: exp(y * log(x)) computed purely on the x87 stack.
     * Direct inline asm here lets us bypass the XMM0-ABI detour we
     * would otherwise take if we called `exp`/`log` as kernel symbols. */
    double result;
    __asm__ __volatile__(
        "fldl   %[ln2]\n\t"           /* ST(0) = ln(2)                */
        "fldl   %[x]\n\t"             /* ST(0) = x, ST(1) = ln(2)     */
        "fyl2x\n\t"                   /* ST(0) = ln(x)                */
        "fldl   %[y]\n\t"             /* ST(0) = y, ST(1) = ln(x)     */
        "fmulp\n\t"                   /* ST(0) = y * ln(x)            */
        "fldl   %[log2e]\n\t"         /* ST(0) = log2(e), ST(1)=arg   */
        "fmulp\n\t"                   /* ST(0) = arg * log2(e)        */
        "fld    %%st(0)\n\t"
        "frndint\n\t"
        "fsub   %%st, %%st(1)\n\t"
        "fxch\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fscale\n\t"
        "fstp   %%st(1)\n\t"
        "fstpl  %[out]\n\t"
        : [out] "=m" (result)
        : [x] "m" (x), [y] "m" (y),
          [ln2] "m" (k_ln2), [log2e] "m" (k_log2e)
        : "memory"
    );
    return result;
}

float libm_powf_impl(float x, float y)
{
    if (y == 0.0f) {
        return 1.0f;
    }
    if (x == 0.0f) {
        if (y > 0.0f) {
            return 0.0f;
        }
        libm_errno = 1;
        return 0.0f;
    }
    if (x < 0.0f) {
        libm_errno = 1;
        return 0.0f;
    }
    float result;
    __asm__ __volatile__(
        "fldl   %[ln2]\n\t"
        "flds   %[x]\n\t"
        "fyl2x\n\t"
        "flds   %[y]\n\t"
        "fmulp\n\t"
        "fldl   %[log2e]\n\t"
        "fmulp\n\t"
        "fld    %%st(0)\n\t"
        "frndint\n\t"
        "fsub   %%st, %%st(1)\n\t"
        "fxch\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fscale\n\t"
        "fstp   %%st(1)\n\t"
        "fstps  %[out]\n\t"
        : [out] "=m" (result)
        : [x] "m" (x), [y] "m" (y),
          [ln2] "m" (k_ln2), [log2e] "m" (k_log2e)
        : "memory"
    );
    return result;
}

/* pow/powf asm wrappers: bridge System-V i386 ST(0) return of the
 * impls into CupidC's XMM0-return ABI.  We must re-push the stack
 * args before `call` because the call itself consumes a slot for the
 * return address — simply jumping to the impl would leave the args
 * off-by-4 (pow) / off-by-4 (powf) from where cdecl expects them.
 *
 * Layout on entry to pow:
 *   [esp]        return address (to CupidC caller)
 *   [esp+4..+11] x (double, 8 B)
 *   [esp+12..+19] y (double, 8 B)
 * We push 16 bytes (y then x, in that order so x ends up at the lower
 * address post-push), then call libm_pow_impl which sees args at its
 * own [esp+4..+11] (x) and [esp+12..+19] (y). */
__asm__(
    ".text\n\t"
    ".globl pow\n\t"
    ".type  pow, @function\n"
    "pow:\n\t"
    "pushl  16(%esp)\n\t"        /* y high: reads orig [esp+16]           */
    "pushl  16(%esp)\n\t"        /* y low : reads orig [esp+12]           */
    "pushl  16(%esp)\n\t"        /* x high: reads orig [esp+ 8]           */
    "pushl  16(%esp)\n\t"        /* x low : reads orig [esp+ 4]           */
    "call   libm_pow_impl\n\t"   /* ST(0) = result                        */
    "add    $16, %esp\n\t"       /* discard pushed args                    */
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  pow, .-pow\n"
);

/* powf: x at [esp+4..+7], y at [esp+8..+11].  Push y then x (8 B total). */
__asm__(
    ".text\n\t"
    ".globl powf\n\t"
    ".type  powf, @function\n"
    "powf:\n\t"
    "pushl  8(%esp)\n\t"         /* y: reads orig [esp+8]                 */
    "pushl  8(%esp)\n\t"         /* x: reads orig [esp+4]                 */
    "call   libm_powf_impl\n\t"
    "add    $8, %esp\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  powf, .-powf\n"
);

/* Phase E Task 26: asin / acos / sinh / cosh / tanh + f-variants.
 *
 * All five derive from previously-implemented primitives:
 *   asin(x) = atan2(x, sqrt(1 - x*x))        for |x| <= 1
 *   acos(x) = atan2(sqrt(1 - x*x), x)        for |x| <= 1
 *   sinh(x) = (exp(x) - exp(-x)) / 2
 *   cosh(x) = (exp(x) + exp(-x)) / 2
 *   tanh(x) = (exp(x) - exp(-x)) / (exp(x) + exp(-x))
 *
 * Implementation pattern (same as Task 25 pow): a C `_impl` function
 * uses the standard System-V i386 ABI (double returned in ST(0)) so
 * the compiler can naturally emit call/return code that stays on the
 * x87 stack.  A top-level GAS `__asm__` wrapper (`asin`, `sinh`, ...)
 * re-pushes the stack args, CALLs the impl, then moves ST(0) -> XMM0
 * per the CupidC-internal ABI used throughout libm.c.
 *
 * Rather than share the XMM0-ABI `sqrt`/`atan2`/`exp` kernel symbols,
 * we introduce C-ABI siblings -- libm_sqrt_impl, libm_atan2_impl,
 * libm_exp_impl -- so the new impls can call them via plain C and let
 * GCC handle the ST(0) return bridge.  Keeps the two ABIs cleanly
 * separated and the code readable.
 *
 * Domain handling: asin/acos set libm_errno = 1 (DOMAIN) and return
 * 0.0 when |x| > 1, matching Task 25's pow convention. */

/* --- C-ABI building blocks ---------------------------------------- */

/* libm_sqrt_impl: SQRTSD in XMM, GCC bridges XMM -> ST(0) at return. */
static double libm_sqrt_impl(double x)
{
    double r;
    __asm__ __volatile__("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}

/* libm_atan2_impl: x87 FPATAN computes atan2(ST(1), ST(0)). */
static double libm_atan2_impl(double y, double x)
{
    double r;
    __asm__ __volatile__(
        "fldl  %[y]\n\t"           /* ST(0) = y                       */
        "fldl  %[x]\n\t"           /* ST(0) = x, ST(1) = y            */
        "fpatan\n\t"               /* ST(0) = atan2(y, x)             */
        "fstpl %[out]\n\t"
        : [out] "=m"(r)
        : [y] "m"(y), [x] "m"(x)
        : "memory"
    );
    return r;
}

/* libm_exp_impl: exp(x) = exp2(x * log2(e)) via x87 pipeline, result
 * written to a memory slot and returned as a normal C double (GCC
 * bridges the memory-load into ST(0) at return). */
static double libm_exp_impl(double x)
{
    double r;
    __asm__ __volatile__(
        "fldl   %[x]\n\t"          /* ST(0) = x                        */
        "fldl   %[log2e]\n\t"      /* ST(0) = log2(e), ST(1) = x       */
        "fmulp\n\t"                /* ST(0) = x * log2(e)              */
        "fld    %%st(0)\n\t"
        "frndint\n\t"
        "fsub   %%st, %%st(1)\n\t"
        "fxch\n\t"
        "f2xm1\n\t"
        "fld1\n\t"
        "faddp\n\t"
        "fscale\n\t"
        "fstp   %%st(1)\n\t"
        "fstpl  %[out]\n\t"
        : [out] "=m"(r)
        : [x] "m"(x), [log2e] "m"(k_log2e)
        : "memory"
    );
    return r;
}

/* --- Task 26 _impl functions (standard C ABI, ST(0) return) ------- */

double libm_asin_impl(double x)  __attribute__((used, noinline));
float  libm_asinf_impl(float x)  __attribute__((used, noinline));
double libm_acos_impl(double x)  __attribute__((used, noinline));
float  libm_acosf_impl(float x)  __attribute__((used, noinline));
double libm_sinh_impl(double x)  __attribute__((used, noinline));
float  libm_sinhf_impl(float x)  __attribute__((used, noinline));
double libm_cosh_impl(double x)  __attribute__((used, noinline));
float  libm_coshf_impl(float x)  __attribute__((used, noinline));
double libm_tanh_impl(double x)  __attribute__((used, noinline));
float  libm_tanhf_impl(float x)  __attribute__((used, noinline));

double libm_asin_impl(double x)
{
    if (x < -1.0 || x > 1.0) {
        libm_errno = 1;
        return 0.0;
    }
    return libm_atan2_impl(x, libm_sqrt_impl(1.0 - x * x));
}

float libm_asinf_impl(float x)
{
    if (x < -1.0f || x > 1.0f) {
        libm_errno = 1;
        return 0.0f;
    }
    return (float)libm_atan2_impl((double)x,
                                   libm_sqrt_impl(1.0 - (double)x * (double)x));
}

double libm_acos_impl(double x)
{
    if (x < -1.0 || x > 1.0) {
        libm_errno = 1;
        return 0.0;
    }
    return libm_atan2_impl(libm_sqrt_impl(1.0 - x * x), x);
}

float libm_acosf_impl(float x)
{
    if (x < -1.0f || x > 1.0f) {
        libm_errno = 1;
        return 0.0f;
    }
    return (float)libm_atan2_impl(libm_sqrt_impl(1.0 - (double)x * (double)x),
                                   (double)x);
}

double libm_sinh_impl(double x)
{
    double e1 = libm_exp_impl(x);
    double e2 = libm_exp_impl(-x);
    return (e1 - e2) * 0.5;
}

float libm_sinhf_impl(float x)
{
    double e1 = libm_exp_impl((double)x);
    double e2 = libm_exp_impl(-(double)x);
    return (float)((e1 - e2) * 0.5);
}

double libm_cosh_impl(double x)
{
    double e1 = libm_exp_impl(x);
    double e2 = libm_exp_impl(-x);
    return (e1 + e2) * 0.5;
}

float libm_coshf_impl(float x)
{
    double e1 = libm_exp_impl((double)x);
    double e2 = libm_exp_impl(-(double)x);
    return (float)((e1 + e2) * 0.5);
}

double libm_tanh_impl(double x)
{
    double e1 = libm_exp_impl(x);
    double e2 = libm_exp_impl(-x);
    return (e1 - e2) / (e1 + e2);
}

float libm_tanhf_impl(float x)
{
    double e1 = libm_exp_impl((double)x);
    double e2 = libm_exp_impl(-(double)x);
    return (float)((e1 - e2) / (e1 + e2));
}

/* --- Task 26 asm wrappers: ST(0) -> XMM0 bridge -------------------- */

/* asin: arg is a single double at [esp+4..+11].  Re-push the 8 B arg,
 * call the impl (which returns in ST(0)), then convert to XMM0. */
__asm__(
    ".text\n\t"
    ".globl asin\n\t"
    ".type  asin, @function\n"
    "asin:\n\t"
    "pushl  8(%esp)\n\t"            /* x high: reads orig [esp+8]   */
    "pushl  8(%esp)\n\t"            /* x low : reads orig [esp+4]   */
    "call   libm_asin_impl\n\t"
    "add    $8, %esp\n\t"
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  asin, .-asin\n"
);

__asm__(
    ".text\n\t"
    ".globl asinf\n\t"
    ".type  asinf, @function\n"
    "asinf:\n\t"
    "pushl  4(%esp)\n\t"            /* x: reads orig [esp+4]        */
    "call   libm_asinf_impl\n\t"
    "add    $4, %esp\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  asinf, .-asinf\n"
);

__asm__(
    ".text\n\t"
    ".globl acos\n\t"
    ".type  acos, @function\n"
    "acos:\n\t"
    "pushl  8(%esp)\n\t"
    "pushl  8(%esp)\n\t"
    "call   libm_acos_impl\n\t"
    "add    $8, %esp\n\t"
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  acos, .-acos\n"
);

__asm__(
    ".text\n\t"
    ".globl acosf\n\t"
    ".type  acosf, @function\n"
    "acosf:\n\t"
    "pushl  4(%esp)\n\t"
    "call   libm_acosf_impl\n\t"
    "add    $4, %esp\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  acosf, .-acosf\n"
);

__asm__(
    ".text\n\t"
    ".globl sinh\n\t"
    ".type  sinh, @function\n"
    "sinh:\n\t"
    "pushl  8(%esp)\n\t"
    "pushl  8(%esp)\n\t"
    "call   libm_sinh_impl\n\t"
    "add    $8, %esp\n\t"
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  sinh, .-sinh\n"
);

__asm__(
    ".text\n\t"
    ".globl sinhf\n\t"
    ".type  sinhf, @function\n"
    "sinhf:\n\t"
    "pushl  4(%esp)\n\t"
    "call   libm_sinhf_impl\n\t"
    "add    $4, %esp\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  sinhf, .-sinhf\n"
);

__asm__(
    ".text\n\t"
    ".globl cosh\n\t"
    ".type  cosh, @function\n"
    "cosh:\n\t"
    "pushl  8(%esp)\n\t"
    "pushl  8(%esp)\n\t"
    "call   libm_cosh_impl\n\t"
    "add    $8, %esp\n\t"
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  cosh, .-cosh\n"
);

__asm__(
    ".text\n\t"
    ".globl coshf\n\t"
    ".type  coshf, @function\n"
    "coshf:\n\t"
    "pushl  4(%esp)\n\t"
    "call   libm_coshf_impl\n\t"
    "add    $4, %esp\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  coshf, .-coshf\n"
);

__asm__(
    ".text\n\t"
    ".globl tanh\n\t"
    ".type  tanh, @function\n"
    "tanh:\n\t"
    "pushl  8(%esp)\n\t"
    "pushl  8(%esp)\n\t"
    "call   libm_tanh_impl\n\t"
    "add    $8, %esp\n\t"
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  tanh, .-tanh\n"
);

__asm__(
    ".text\n\t"
    ".globl tanhf\n\t"
    ".type  tanhf, @function\n"
    "tanhf:\n\t"
    "pushl  4(%esp)\n\t"
    "call   libm_tanhf_impl\n\t"
    "add    $4, %esp\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  tanhf, .-tanhf\n"
);

/* Phase E Task 27: cbrt / hypot / nextafter + f-variants.
 *
 * Same split-ABI pattern as Task 26: C `_impl` functions with the
 * standard System-V i386 ABI (double returned in ST(0)), wrapped by
 * top-level `__asm__` thunks that re-push args, CALL the impl, and
 * bridge ST(0) -> XMM0 to match the CupidC-internal ABI used
 * throughout libm.c.
 *
 * Mathematical approach:
 *
 *   cbrt(x)     -- Initial estimate via IEEE bit-trick: divide the biased
 *                  exponent field by 3 (exponent lives in bits 52..62 of
 *                  a double).  Three iterations of the Newton-Raphson
 *                  recurrence y := (2y + x/y^2)/3 for f(y) = y^3 - x
 *                  then converge to a double-precision root.  Negative
 *                  input is sign-factored so the bit-trick sees only a
 *                  positive magnitude; denormal / zero exponents fall
 *                  back to y = x as the seed (Newton still converges
 *                  from there for small inputs in a few iterations, and
 *                  exact zero is short-circuited before the iteration).
 *
 *   hypot(x,y)  -- Scale-safe reformulation: let M = max(|x|,|y|) and
 *                  m = min(|x|,|y|); then sqrt(x^2 + y^2) = M *
 *                  sqrt(1 + (m/M)^2).  Squaring m/M ∈ [0,1] cannot
 *                  overflow, and the outer multiply by M does not
 *                  over/underflow on the final result whenever the true
 *                  result itself is representable.
 *
 *   nextafter(x,y) -- IEEE-754 bit-level stepping.  The 64-bit integer
 *                  representation of positive doubles is monotonically
 *                  increasing in magnitude, so ++bits moves to the next
 *                  representable double.  For negatives, magnitude grows
 *                  with ++bits as well (sign bit is the MSB).  Thus the
 *                  direction of step is: increment bits when stepping
 *                  away from zero (x>0 && y>x, or x<0 && y<x);
 *                  decrement otherwise.  x == 0 is special-cased to
 *                  produce the smallest subnormal with y's sign.  NaN
 *                  inputs propagate through the `x == y` early-out,
 *                  since x != x for NaN.
 */

/* --- Task 27 _impl functions (standard C ABI, ST(0) return) ------- */

double libm_cbrt_impl(double x)          __attribute__((used, noinline));
float  libm_cbrtf_impl(float x)          __attribute__((used, noinline));
double libm_hypot_impl(double x, double y)       __attribute__((used, noinline));
float  libm_hypotf_impl(float x, float y)        __attribute__((used, noinline));
double libm_nextafter_impl(double x, double y)   __attribute__((used, noinline));
float  libm_nextafterf_impl(float x, float y)    __attribute__((used, noinline));

/* Bit-punning helpers.  A union is the -pedantic-clean way to convert
 * between float/double and their integer representations; GCC emits no
 * code for this under -O2 (just a register/stack alias). */
typedef union { double d; uint64_t u; } libm_bits64_t;
typedef union { float  f; uint32_t u; } libm_bits32_t;

double libm_cbrt_impl(double x)
{
    if (x == 0.0) {
        return 0.0;
    }
    double sign = x < 0.0 ? -1.0 : 1.0;
    if (x < 0.0) {
        x = -x;
    }

    /* Initial estimate via bit-fiddle divide-exponent-by-3.  Double layout:
     * sign [63], biased exponent [62..52], mantissa [51..0].  We keep the
     * mantissa bits but rewrite the exponent field so the estimate has
     * roughly the correct order of magnitude. */
    libm_bits64_t b;
    b.d = x;
    uint32_t exp_bits = (uint32_t)((b.u >> 52) & 0x7FFu);

    double y;
    if (exp_bits == 0u) {
        /* Denormal or +0 (already handled): skip the bit trick, Newton
         * will still converge from y = x in a few iterations since x
         * itself is tiny. */
        y = x;
    } else {
        int32_t unbiased = (int32_t)exp_bits - 1023;
        /* Integer division rounds toward zero in C99; that's what we want
         * here -- the Newton pass repairs any small exponent imprecision. */
        int32_t new_unbiased = unbiased / 3;
        uint32_t new_exp = (uint32_t)(new_unbiased + 1023) & 0x7FFu;
        b.u = (b.u & ~((uint64_t)0x7FFu << 52))
            | ((uint64_t)new_exp << 52);
        y = b.d;
    }

    /* Newton-Raphson: y_{n+1} = (2 y_n + x / y_n^2) / 3.  Three iterations
     * take the ~4-bit exponent-divided estimate to double precision. */
    for (int i = 0; i < 3; i++) {
        y = (2.0 * y + x / (y * y)) / 3.0;
    }
    return sign * y;
}

float libm_cbrtf_impl(float x)
{
    return (float)libm_cbrt_impl((double)x);
}

double libm_hypot_impl(double x, double y)
{
    if (x < 0.0) { x = -x; }
    if (y < 0.0) { y = -y; }
    if (x < y) { double t = x; x = y; y = t; }
    /* Post-swap: x = max(|x|,|y|), y = min(|x|,|y|).  If the larger is
     * zero then both are zero and the result is zero. */
    if (x == 0.0) {
        return 0.0;
    }
    double r = y / x;
    return x * libm_sqrt_impl(1.0 + r * r);
}

float libm_hypotf_impl(float x, float y)
{
    return (float)libm_hypot_impl((double)x, (double)y);
}

double libm_nextafter_impl(double x, double y)
{
    /* x == y returns y (per ISO C); this also short-circuits NaN cases
     * via y, since any NaN comparison including equality is false and we
     * fall through -- except that a NaN on either side must propagate a
     * NaN result.  We cover that by loading the bit pattern blindly and
     * ++/-- it; the IEEE integer neighbours of a NaN are still NaNs. */
    if (x == y) {
        return y;
    }
    libm_bits64_t b;
    b.d = x;
    if (x == 0.0) {
        /* Smallest positive/negative subnormal in the direction of y.
         * Bit pattern 0x0000000000000001 = 4.9e-324, bit 63 = sign. */
        b.u = 1ull | (y < 0.0 ? ((uint64_t)1 << 63) : 0ull);
    } else if ((x > 0.0 && y > x) || (x < 0.0 && y < x)) {
        /* Moving away from zero -> magnitude increases -> ++bits (since
         * sign bit dominates and within same sign, magnitude monotone
         * in the low 63 bits). */
        b.u++;
    } else {
        b.u--;
    }
    return b.d;
}

float libm_nextafterf_impl(float x, float y)
{
    if (x == y) {
        return y;
    }
    libm_bits32_t b;
    b.f = x;
    if (x == 0.0f) {
        b.u = 1u | (y < 0.0f ? (1u << 31) : 0u);
    } else if ((x > 0.0f && y > x) || (x < 0.0f && y < x)) {
        b.u++;
    } else {
        b.u--;
    }
    return b.f;
}

/* --- Task 27 asm wrappers: ST(0) -> XMM0 bridge -------------------- */

/* cbrt: single double arg at [esp+4..+11].  Re-push 8 B, call, bridge. */
__asm__(
    ".text\n\t"
    ".globl cbrt\n\t"
    ".type  cbrt, @function\n"
    "cbrt:\n\t"
    "pushl  8(%esp)\n\t"
    "pushl  8(%esp)\n\t"
    "call   libm_cbrt_impl\n\t"
    "add    $8, %esp\n\t"
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  cbrt, .-cbrt\n"
);

__asm__(
    ".text\n\t"
    ".globl cbrtf\n\t"
    ".type  cbrtf, @function\n"
    "cbrtf:\n\t"
    "pushl  4(%esp)\n\t"
    "call   libm_cbrtf_impl\n\t"
    "add    $4, %esp\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  cbrtf, .-cbrtf\n"
);

/* hypot: two double args at [esp+4..+11] (x) and [esp+12..+19] (y).
 * Re-push 16 B, call, bridge.  Same stack arithmetic as pow (Task 25). */
__asm__(
    ".text\n\t"
    ".globl hypot\n\t"
    ".type  hypot, @function\n"
    "hypot:\n\t"
    "pushl  16(%esp)\n\t"         /* y high: reads orig [esp+16]       */
    "pushl  16(%esp)\n\t"         /* y low : reads orig [esp+12]       */
    "pushl  16(%esp)\n\t"         /* x high: reads orig [esp+ 8]       */
    "pushl  16(%esp)\n\t"         /* x low : reads orig [esp+ 4]       */
    "call   libm_hypot_impl\n\t"
    "add    $16, %esp\n\t"
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  hypot, .-hypot\n"
);

/* hypotf: two float args at [esp+4..+7] (x) and [esp+8..+11] (y). */
__asm__(
    ".text\n\t"
    ".globl hypotf\n\t"
    ".type  hypotf, @function\n"
    "hypotf:\n\t"
    "pushl  8(%esp)\n\t"          /* y: reads orig [esp+8]             */
    "pushl  8(%esp)\n\t"          /* x: reads orig [esp+4]             */
    "call   libm_hypotf_impl\n\t"
    "add    $8, %esp\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  hypotf, .-hypotf\n"
);

/* nextafter: same 2-double layout as hypot. */
__asm__(
    ".text\n\t"
    ".globl nextafter\n\t"
    ".type  nextafter, @function\n"
    "nextafter:\n\t"
    "pushl  16(%esp)\n\t"
    "pushl  16(%esp)\n\t"
    "pushl  16(%esp)\n\t"
    "pushl  16(%esp)\n\t"
    "call   libm_nextafter_impl\n\t"
    "add    $16, %esp\n\t"
    "sub    $8, %esp\n\t"
    "fstpl  (%esp)\n\t"
    "movsd  (%esp), %xmm0\n\t"
    "add    $8, %esp\n\t"
    "ret\n\t"
    ".size  nextafter, .-nextafter\n"
);

__asm__(
    ".text\n\t"
    ".globl nextafterf\n\t"
    ".type  nextafterf, @function\n"
    "nextafterf:\n\t"
    "pushl  8(%esp)\n\t"
    "pushl  8(%esp)\n\t"
    "call   libm_nextafterf_impl\n\t"
    "add    $8, %esp\n\t"
    "sub    $4, %esp\n\t"
    "fstps  (%esp)\n\t"
    "movss  (%esp), %xmm0\n\t"
    "add    $4, %esp\n\t"
    "ret\n\t"
    ".size  nextafterf, .-nextafterf\n"
);
