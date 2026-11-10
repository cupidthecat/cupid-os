#ifndef CUPIDC_STATIC_LONG_DOUBLE_ARITHMETIC_FIXTURE_H
#define CUPIDC_STATIC_LONG_DOUBLE_ARITHMETIC_FIXTURE_H

#include "cupidc_frontend.h"

typedef struct {
  ctool_u64 significand;
  ctool_u32 high_bits;
} cupidc_static_long_double_arithmetic_oracle_t;

static const char cupidc_static_long_double_arithmetic_source_prefix[] =
    "#define STATIC_LD_P63 ((long double)9223372036854775808ull)\n"
    "#define STATIC_LD_P61 ((long double)2305843009213693952ull)\n"
    "#define STATIC_LD_P62 ((long double)4611686018427387904ull)\n"
    "#define STATIC_LD_P64 (STATIC_LD_P63 * 2.0L)\n"
    "#define STATIC_LD_P1024 (STATIC_LD_P64 * STATIC_LD_P64 * "
    "STATIC_LD_P64 * STATIC_LD_P64 * STATIC_LD_P64 * STATIC_LD_P64 * "
    "STATIC_LD_P64 * STATIC_LD_P64 * STATIC_LD_P64 * STATIC_LD_P64 * "
    "STATIC_LD_P64 * STATIC_LD_P64 * STATIC_LD_P64 * STATIC_LD_P64 * "
    "STATIC_LD_P64 * STATIC_LD_P64)\n"
    "#define STATIC_LD_P15360 (STATIC_LD_P1024 * STATIC_LD_P1024 * "
    "STATIC_LD_P1024 * STATIC_LD_P1024 * STATIC_LD_P1024 * "
    "STATIC_LD_P1024 * STATIC_LD_P1024 * STATIC_LD_P1024 * "
    "STATIC_LD_P1024 * STATIC_LD_P1024 * STATIC_LD_P1024 * "
    "STATIC_LD_P1024 * STATIC_LD_P1024 * STATIC_LD_P1024 * "
    "STATIC_LD_P1024)\n"
    "#define STATIC_LD_P960 (STATIC_LD_P64 * STATIC_LD_P64 * "
    "STATIC_LD_P64 * STATIC_LD_P64 * STATIC_LD_P64 * STATIC_LD_P64 * "
    "STATIC_LD_P64 * STATIC_LD_P64 * STATIC_LD_P64 * STATIC_LD_P64 * "
    "STATIC_LD_P64 * STATIC_LD_P64 * STATIC_LD_P64 * STATIC_LD_P64 * "
    "STATIC_LD_P64)\n"
    "#define STATIC_LD_P16320 (STATIC_LD_P15360 * STATIC_LD_P960)\n"
    "#define STATIC_LD_MIN_NORMAL "
    "(1.0L / STATIC_LD_P16320 / STATIC_LD_P62)\n"
    "#define STATIC_LD_MIN_SUBNORMAL "
    "(1.0L / STATIC_LD_P16320 / STATIC_LD_P64 / STATIC_LD_P61)\n"
    "#define STATIC_LD_LARGEST_SUBNORMAL "
    "(9223372036854775807ull * STATIC_LD_MIN_SUBNORMAL)\n"
    "#define STATIC_LD_MAX_FINITE "
    "(18446744073709551615ull * STATIC_LD_P16320)\n"
    "#define STATIC_LD_MAX_HALF_ULP "
    "(STATIC_LD_P16320 / 2.0L)\n"
    "#define STATIC_LD_MAX_QUARTER_ULP "
    "(STATIC_LD_P16320 / 4.0L)\n"
    "#define STATIC_LD_POSITIVE_INFINITY ((long double)(1.0 / 0.0))\n"
    "#define STATIC_LD_NEGATIVE_INFINITY ((long double)(-1.0 / 0.0))\n"
    "#define STATIC_LD_QUIET_NAN ((long double)(0.0 / 0.0))\n"
    "long double static_long_double_unresolved(void);\n";

static const char cupidc_static_long_double_arithmetic_source_cases[] =
    "static const long double static_long_double_add_subtract[16] = {\n"
    "  1.0L + 2.0L,\n"
    "  -1.0L + -2.0L,\n"
    "  5.0L - 2.0L,\n"
    "  -5.0L - -2.0L,\n"
    "  1.0L + 1.0L / STATIC_LD_P63 / 2.0L,\n"
    "  1.0L + 3.0L / STATIC_LD_P63 / 4.0L,\n"
    "  1.0000000000000000001L + 1.0L / STATIC_LD_P63 / 2.0L,\n"
    "  1.0L - 1.0L / STATIC_LD_P63 / 4.0L,\n"
    "  1.0L - 3.0L / STATIC_LD_P63 / 8.0L,\n"
    "  -1.0L + 1.0L / STATIC_LD_P63 / 4.0L,\n"
    "  10.0L + -3.0L,\n"
    "  1.0000000000000000001L - 1.0L,\n"
    "  1.0L - 1.0000000000000000001L,\n"
    "  1e-19L + -1e-19L,\n"
    "  -0.0L + -0.0L,\n"
    "  +0.0L - -0.0L\n"
    "};\n"
    "static const long double static_long_double_multiply_divide[16] = {\n"
    "  1.5L * 2.0L,\n"
    "  -1.5L * 2.0L,\n"
    "  18446744073709551615ull * 1.0L,\n"
    "  18446744073709551615ull * 2.0L,\n"
    "  +0.0L * -2.0L,\n"
    "  1.0000000000000000001L * 1.0L,\n"
    "  7.0L / 2.0L,\n"
    "  -7.0L / 2.0L,\n"
    "  1.0L / 3.0L,\n"
    "  2.0L / 3.0L,\n"
    "  18446744073709551615ull / 18446744073709551615e0L,\n"
    "  1.0L / 1.0000000000000000001L,\n"
    "  1.0000000000000000001L / 1.0L,\n"
    "  STATIC_LD_MIN_NORMAL / 2.0L,\n"
    "  STATIC_LD_MIN_SUBNORMAL * 2.0L,\n"
    "  STATIC_LD_MIN_SUBNORMAL / 2.0L\n"
    "};\n"
    "static const long double static_long_double_rounding[16] = {\n"
    "  1.0L + 1.0L / STATIC_LD_P63 / 2.0L,\n"
    "  1.0L + 3.0L / STATIC_LD_P63 / 4.0L,\n"
    "  1.0000000000000000001L + 1.0L / STATIC_LD_P63 / 2.0L,\n"
    "  1.0L - 1.0L / STATIC_LD_P63 / 4.0L,\n"
    "  1.0L - 3.0L / STATIC_LD_P63 / 8.0L,\n"
    "  STATIC_LD_MIN_SUBNORMAL / 2.0L,\n"
    "  STATIC_LD_MIN_SUBNORMAL * 3.0L / 4.0L,\n"
    "  STATIC_LD_MIN_SUBNORMAL * 3.0L / 2.0L,\n"
    "  STATIC_LD_LARGEST_SUBNORMAL + "
    "STATIC_LD_MIN_SUBNORMAL / 2.0L,\n"
    "  STATIC_LD_MAX_FINITE * 2.0L,\n"
    "  STATIC_LD_MAX_FINITE + STATIC_LD_MAX_HALF_ULP,\n"
    "  STATIC_LD_MAX_FINITE + STATIC_LD_MAX_QUARTER_ULP,\n"
    "  STATIC_LD_MIN_NORMAL / 2.0L,\n"
    "  STATIC_LD_MIN_NORMAL / STATIC_LD_P63,\n"
    "  STATIC_LD_MIN_NORMAL / STATIC_LD_P63 / 2.0L,\n"
    "  -STATIC_LD_MIN_NORMAL / STATIC_LD_P63 / 2.0L\n"
    "};\n"
    "static const long double static_long_double_edges[16] = {\n"
    "  STATIC_LD_MIN_NORMAL,\n"
    "  -STATIC_LD_MIN_NORMAL,\n"
    "  STATIC_LD_MIN_SUBNORMAL,\n"
    "  -STATIC_LD_MIN_SUBNORMAL,\n"
    "  STATIC_LD_LARGEST_SUBNORMAL,\n"
    "  -STATIC_LD_LARGEST_SUBNORMAL,\n"
    "  STATIC_LD_MAX_FINITE,\n"
    "  -STATIC_LD_MAX_FINITE,\n"
    "  STATIC_LD_MIN_NORMAL - STATIC_LD_MIN_SUBNORMAL,\n"
    "  STATIC_LD_LARGEST_SUBNORMAL + STATIC_LD_MIN_SUBNORMAL,\n"
    "  STATIC_LD_MIN_SUBNORMAL + STATIC_LD_MIN_SUBNORMAL,\n"
    "  STATIC_LD_MAX_FINITE / 2.0L,\n"
    "  STATIC_LD_MAX_FINITE * 1.0L,\n"
    "  STATIC_LD_MAX_FINITE * 2.0L,\n"
    "  STATIC_LD_MIN_SUBNORMAL * 1.0L,\n"
    "  STATIC_LD_MIN_SUBNORMAL / -2.0L\n"
    "};\n"
    "static long double static_long_double_specials[16] = {\n"
    "  STATIC_LD_POSITIVE_INFINITY + 1.0L,\n"
    "  STATIC_LD_NEGATIVE_INFINITY - 1.0L,\n"
    "  STATIC_LD_POSITIVE_INFINITY + STATIC_LD_NEGATIVE_INFINITY,\n"
    "  STATIC_LD_POSITIVE_INFINITY * +0.0L,\n"
    "  +0.0L / +0.0L,\n"
    "  STATIC_LD_POSITIVE_INFINITY / STATIC_LD_POSITIVE_INFINITY,\n"
    "  -1.0L / +0.0L,\n"
    "  -0.0L / 2.0L,\n"
    "  STATIC_LD_QUIET_NAN + 1.0L,\n"
    "  1.0L + STATIC_LD_QUIET_NAN,\n"
    "  STATIC_LD_QUIET_NAN - 1.0L,\n"
    "  1.0L - STATIC_LD_QUIET_NAN,\n"
    "  STATIC_LD_QUIET_NAN * 1.0L,\n"
    "  1.0L * STATIC_LD_QUIET_NAN,\n"
    "  STATIC_LD_QUIET_NAN / 1.0L,\n"
    "  1.0L / STATIC_LD_QUIET_NAN\n"
    "};\n";

static const cupidc_static_long_double_arithmetic_oracle_t
    cupidc_static_long_double_add_subtract_oracles[] = {
        {0xc000000000000000ull, 0x4000u},
        {0xc000000000000000ull, 0xc000u},
        {0xc000000000000000ull, 0x4000u},
        {0xc000000000000000ull, 0xc000u},
        {0x8000000000000000ull, 0x3fffu},
        {0x8000000000000001ull, 0x3fffu},
        {0x8000000000000002ull, 0x3fffu},
        {0x8000000000000000ull, 0x3fffu},
        {0xffffffffffffffffull, 0x3ffeu},
        {0x8000000000000000ull, 0xbfffu},
        {0xe000000000000000ull, 0x4001u},
        {0x8000000000000000ull, 0x3fc0u},
        {0x8000000000000000ull, 0xbfc0u},
        {0ull, 0u},
        {0ull, 0x8000u},
        {0ull, 0u}};

static const cupidc_static_long_double_arithmetic_oracle_t
    cupidc_static_long_double_multiply_divide_oracles[] = {
        {0xc000000000000000ull, 0x4000u},
        {0xc000000000000000ull, 0xc000u},
        {0xffffffffffffffffull, 0x403eu},
        {0xffffffffffffffffull, 0x403fu},
        {0ull, 0x8000u},
        {0x8000000000000001ull, 0x3fffu},
        {0xe000000000000000ull, 0x4000u},
        {0xe000000000000000ull, 0xc000u},
        {0xaaaaaaaaaaaaaaabull, 0x3ffdu},
        {0xaaaaaaaaaaaaaaabull, 0x3ffeu},
        {0x8000000000000000ull, 0x3fffu},
        {0xfffffffffffffffeull, 0x3ffeu},
        {0x8000000000000001ull, 0x3fffu},
        {0x4000000000000000ull, 0u},
        {2ull, 0u},
        {0ull, 0u}};

static const cupidc_static_long_double_arithmetic_oracle_t
    cupidc_static_long_double_rounding_oracles[] = {
        {0x8000000000000000ull, 0x3fffu},
        {0x8000000000000001ull, 0x3fffu},
        {0x8000000000000002ull, 0x3fffu},
        {0x8000000000000000ull, 0x3fffu},
        {0xffffffffffffffffull, 0x3ffeu},
        {0ull, 0u},
        {1ull, 0u},
        {2ull, 0u},
        {0x7fffffffffffffffull, 0u},
        {0x8000000000000000ull, 0x7fffu},
        {0x8000000000000000ull, 0x7fffu},
        {0xffffffffffffffffull, 0x7ffeu},
        {0x4000000000000000ull, 0u},
        {1ull, 0u},
        {0ull, 0u},
        {0ull, 0x8000u}};

static const cupidc_static_long_double_arithmetic_oracle_t
    cupidc_static_long_double_edge_oracles[] = {
        {0x8000000000000000ull, 0x0001u},
        {0x8000000000000000ull, 0x8001u},
        {1ull, 0u},
        {1ull, 0x8000u},
        {0x7fffffffffffffffull, 0u},
        {0x7fffffffffffffffull, 0x8000u},
        {0xffffffffffffffffull, 0x7ffeu},
        {0xffffffffffffffffull, 0xfffeu},
        {0x7fffffffffffffffull, 0u},
        {0x8000000000000000ull, 0x0001u},
        {2ull, 0u},
        {0xffffffffffffffffull, 0x7ffdu},
        {0xffffffffffffffffull, 0x7ffeu},
        {0x8000000000000000ull, 0x7fffu},
        {1ull, 0u},
        {0ull, 0x8000u}};

static const cupidc_static_long_double_arithmetic_oracle_t
    cupidc_static_long_double_special_oracles[] = {
        {0x8000000000000000ull, 0x7fffu},
        {0x8000000000000000ull, 0xffffu},
        {0xc000000000000000ull, 0x7fffu},
        {0xc000000000000000ull, 0x7fffu},
        {0xc000000000000000ull, 0x7fffu},
        {0xc000000000000000ull, 0x7fffu},
        {0x8000000000000000ull, 0xffffu},
        {0ull, 0x8000u},
        {0xc000000000000000ull, 0x7fffu},
        {0xc000000000000000ull, 0x7fffu},
        {0xc000000000000000ull, 0x7fffu},
        {0xc000000000000000ull, 0x7fffu},
        {0xc000000000000000ull, 0x7fffu},
        {0xc000000000000000ull, 0x7fffu},
        {0xc000000000000000ull, 0x7fffu},
        {0xc000000000000000ull, 0x7fffu}};

#endif
