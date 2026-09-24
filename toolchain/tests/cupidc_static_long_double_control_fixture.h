#ifndef CUPIDC_STATIC_LONG_DOUBLE_CONTROL_FIXTURE_H
#define CUPIDC_STATIC_LONG_DOUBLE_CONTROL_FIXTURE_H

#include "cupidc_frontend.h"

typedef struct {
  ctool_u64 bits;
  ctool_u32 high_bits;
} cupidc_static_long_double_control_floating_oracle_t;

typedef struct {
  ctool_c_initializer_kind_t kind;
  ctool_u64 bits;
  ctool_u32 high_bits;
} cupidc_static_long_double_control_scalar_oracle_t;

static const char cupidc_static_long_double_control_source[] =
    "typedef enum static_control_signed_enum {\n"
    "  STATIC_CONTROL_SIGNED_ENUM = -3\n"
    "} static_control_signed_enum_t;\n"
    "typedef enum static_control_unsigned_enum {\n"
    "  STATIC_CONTROL_UNSIGNED_ENUM = 0xffffffffffffffffull\n"
    "} static_control_unsigned_enum_t;\n"
    "long double static_control_unresolved(void);\n"
    "static const long double static_float_to_long_double[10] = {\n"
    "  1.5f, -0.0f, +0.0f, -2.5, +0.0, -0.0,\n"
    "  (1e-19f * 1e-19f),\n"
    "  (1.0f / 0.0f), (-1.0 / 0.0), (0.0f / 0.0f)\n"
    "};\n"
    "static const float static_long_double_to_float[7] = {\n"
    "  1.5L, +0.0L, -0.0L, 1.0000000000000000001L,\n"
    "  (long double)(1.0f / 0.0f),\n"
    "  (long double)(-1.0f / 0.0f),\n"
    "  (long double)(0.0f / 0.0f)\n"
    "};\n"
    "static const double static_long_double_to_double[7] = {\n"
    "  -2.5L, +0.0L, -0.0L, 1.0000000000000000001L,\n"
    "  (long double)(1.0 / 0.0),\n"
    "  (long double)(-1.0 / 0.0),\n"
    "  (long double)(0.0 / 0.0)\n"
    "};\n"
    "static const int static_long_double_truth[7] = {\n"
    "  !+0.0L, !-0.0L, !+1.25L, !-1.25L,\n"
    "  !(long double)(1.0f / 0.0f),\n"
    "  !(long double)(-1.0f / 0.0f),\n"
    "  !(long double)(0.0f / 0.0f)\n"
    "};\n"
    "static const int static_long_double_comparisons[31] = {\n"
    "  -2.0L < -1.0L,\n"
    "  2.0L <= 1.0L,\n"
    "  2.0L > 1.0L,\n"
    "  1.0L >= 2.0L,\n"
    "  -0.0L == +0.0L,\n"
    "  3.0L != 3.0L,\n"
    "  1.0000000000000000001L > 1.0L,\n"
    "  -1.0000000000000000001L < -1.0L,\n"
    "  1.25f < 1.5L,\n"
    "  1.5L == 1.5,\n"
    "  -2.0 > -3.0L,\n"
    "  -4.0L != -4.0f,\n"
    "  2 < 2.5L,\n"
    "  -2.5L < -2,\n"
    "  (signed char)-128 == -128.0L,\n"
    "  65535.0L == (unsigned short)65535,\n"
    "  (-9223372036854775807ll - 1ll) == -9223372036854775808e0L,\n"
    "  9223372036854775807ll == 9223372036854775807e0L,\n"
    "  18446744073709551615ull == 18446744073709551615e0L,\n"
    "  STATIC_CONTROL_SIGNED_ENUM == -3.0L,\n"
    "  18446744073709551615e0L == STATIC_CONTROL_UNSIGNED_ENUM,\n"
    "  (long double)(1.0f / 0.0f) > 1.0L,\n"
    "  (long double)(-1.0f / 0.0f) < -1.0L,\n"
    "  (long double)(1.0f / 0.0f) ==\n"
    "      (long double)(1.0 / 0.0),\n"
    "  (long double)(-1.0f / 0.0f) !=\n"
    "      (long double)(1.0 / 0.0),\n"
    "  (long double)(0.0f / 0.0f) < 0.0L,\n"
    "  (long double)(0.0f / 0.0f) <= 0.0L,\n"
    "  (long double)(0.0f / 0.0f) > 0.0L,\n"
    "  (long double)(0.0f / 0.0f) >= 0.0L,\n"
    "  (long double)(0.0f / 0.0f) ==\n"
    "      (long double)(0.0 / 0.0),\n"
    "  (long double)(0.0f / 0.0f) !=\n"
    "      (long double)(0.0 / 0.0)\n"
    "};\n"
    "static const int static_long_double_logic[6] = {\n"
    "  +1.0L && -2.0L,\n"
    "  +0.0L && static_control_unresolved(),\n"
    "  -1.0L || +0.0L,\n"
    "  -0.0L || -3.0L,\n"
    "  -0.0L || +0.0L,\n"
    "  +1.0L || static_control_unresolved()\n"
    "};\n"
    "static const long double static_long_double_choices[7] = {\n"
    "  +1.0L ? +2.0L : static_control_unresolved(),\n"
    "  -0.0L ? static_control_unresolved() : -2.0L,\n"
    "  1 ? 1.0000000000000000001L : static_control_unresolved(),\n"
    "  0 ? static_control_unresolved() : -0.0L,\n"
    "  1 ? 1.5f : 2.0L,\n"
    "  1 ? 3 : 2.5L,\n"
    "  1 ? STATIC_CONTROL_UNSIGNED_ENUM : 1.0L\n"
    "};\n"
    "static long double static_long_double_positive_zero_choice =\n"
    "    1 ? +0.0L : 1.0L;\n"
    "static long double static_long_double_negative_zero_choice =\n"
    "    0 ? 1.0L : -0.0L;\n";

static const ctool_u64 cupidc_static_long_double_control_truth_oracles[] = {
    1ull, 1ull, 0ull, 0ull, 0ull, 0ull, 0ull};

static const ctool_u64
    cupidc_static_long_double_control_comparison_oracles[] = {
        1ull, 0ull, 1ull, 0ull, 1ull, 0ull, 1ull,
        1ull, 1ull, 1ull, 1ull, 0ull, 1ull, 1ull,
        1ull, 1ull, 1ull, 1ull, 1ull, 1ull, 1ull,
        1ull, 1ull, 1ull, 1ull, 0ull, 0ull, 0ull,
        0ull, 0ull, 1ull};

static const ctool_u64 cupidc_static_long_double_control_logic_oracles[] = {
    1ull, 0ull, 1ull, 1ull, 0ull, 1ull};

static const cupidc_static_long_double_control_floating_oracle_t
    cupidc_static_long_double_control_choice_oracles[] = {
        {0x8000000000000000ull, 0x4000u},
        {0x8000000000000000ull, 0xc000u},
        {0x8000000000000001ull, 0x3fffu},
        {0ull, 0x8000u},
        {0xc000000000000000ull, 0x3fffu},
        {0xc000000000000000ull, 0x4000u},
        {0xffffffffffffffffull, 0x403eu}};

static const cupidc_static_long_double_control_floating_oracle_t
    cupidc_static_long_double_control_widening_oracles[] = {
        {0xc000000000000000ull, 0x3fffu},
        {0ull, 0x8000u},
        {0ull, 0u},
        {0xa000000000000000ull, 0xc000u},
        {0ull, 0u},
        {0ull, 0x8000u},
        {0xd9c7dc0000000000ull, 0x3f80u},
        {0x8000000000000000ull, 0x7fffu},
        {0x8000000000000000ull, 0xffffu},
        {0xc000000000000000ull, 0x7fffu}};

static const ctool_u64
    cupidc_static_long_double_control_float_narrowing_oracles[] = {
        0x3fc00000ull, 0ull, 0x80000000ull, 0x3f800000ull,
        0x7f800000ull, 0xff800000ull, 0x7fc00000ull};

static const ctool_u64
    cupidc_static_long_double_control_double_narrowing_oracles[] = {
        0xc004000000000000ull, 0ull, 0x8000000000000000ull,
        0x3ff0000000000000ull, 0x7ff0000000000000ull,
        0xfff0000000000000ull, 0x7ff8000000000000ull};

static const cupidc_static_long_double_control_scalar_oracle_t
    cupidc_static_long_double_control_zero_choice_oracles[] = {
        {CTOOL_C_INITIALIZER_FLOATING, 0ull, 0u},
        {CTOOL_C_INITIALIZER_FLOATING, 0ull, 0x8000u}};

#endif
