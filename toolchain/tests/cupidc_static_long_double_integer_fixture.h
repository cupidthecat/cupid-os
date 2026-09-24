#ifndef CUPIDC_STATIC_LONG_DOUBLE_INTEGER_FIXTURE_H
#define CUPIDC_STATIC_LONG_DOUBLE_INTEGER_FIXTURE_H

#include "cupidc_frontend.h"

typedef struct {
  ctool_c_initializer_kind_t initializer_kind;
  ctool_u64 significand;
  ctool_u32 high_bits;
} cupidc_static_integer_to_long_double_oracle_t;

typedef struct {
  ctool_c_type_kind_t type_kind;
  ctool_c_type_kind_t enum_compatible_kind;
  ctool_u32 qualifiers;
  ctool_u64 bits;
} cupidc_static_long_double_to_integer_oracle_t;

static const char cupidc_static_long_double_integer_source[] =
    "typedef enum cupidc_static_signed_enum {\n"
    "  CUPIDC_STATIC_SIGNED_ENUM_NEGATIVE = -3\n"
    "} cupidc_static_signed_enum_t;\n"
    "typedef enum cupidc_static_unsigned_enum {\n"
    "  CUPIDC_STATIC_UNSIGNED_ENUM_MAX = 0xffffffffffffffffull\n"
    "} cupidc_static_unsigned_enum_t;\n"
    "struct cupidc_static_long_double_integer_results {\n"
    "  const _Bool boolean_zero;\n"
    "  const _Bool boolean_positive_zero;\n"
    "  const _Bool boolean_nonzero;\n"
    "  const _Bool boolean_negative_nonzero;\n"
    "  const char plain_character;\n"
    "  const signed char signed_character;\n"
    "  const unsigned char unsigned_character;\n"
    "  const signed short signed_short_value;\n"
    "  const unsigned short unsigned_short_value;\n"
    "  const signed int signed_int_value;\n"
    "  const unsigned int unsigned_int_value;\n"
    "  const signed long signed_long_value;\n"
    "  const unsigned long unsigned_long_value;\n"
    "  const signed long long signed_wide_value;\n"
    "  const signed long long signed_wide_maximum;\n"
    "  const unsigned long long unsigned_wide_value;\n"
    "  const unsigned long long unsigned_fraction_zero;\n"
    "  const cupidc_static_signed_enum_t signed_enum_value;\n"
    "  const cupidc_static_unsigned_enum_t unsigned_enum_value;\n"
    "};\n"
    "static const long double cupidc_static_integer_to_long_double_file[] = {\n"
    "  (_Bool)0,\n"
    "  (_Bool)7,\n"
    "  (char)127,\n"
    "  (signed char)-128,\n"
    "  (unsigned char)255,\n"
    "  (signed short)-32768,\n"
    "  (unsigned short)65535,\n"
    "  (signed int)(-2147483647 - 1),\n"
    "  (unsigned int)4294967295u,\n"
    "  (signed long)(-2147483647L - 1L),\n"
    "  (unsigned long)4294967295ul,\n"
    "  (signed long long)(-9223372036854775807ll - 1ll),\n"
    "  (unsigned long long)18446744073709551615ull,\n"
    "  CUPIDC_STATIC_SIGNED_ENUM_NEGATIVE,\n"
    "  CUPIDC_STATIC_UNSIGNED_ENUM_MAX\n"
    "};\n"
    "static const struct cupidc_static_long_double_integer_results\n"
    "cupidc_static_long_double_to_integer_file = {\n"
    "  -0.0L,\n"
    "  +0.0L,\n"
    "  0.5L,\n"
    "  -0.5L,\n"
    "  127.875L,\n"
    "  -127.875L,\n"
    "  255.875L,\n"
    "  -32767.875L,\n"
    "  65535.875L,\n"
    "  -2147483647.875L,\n"
    "  4294967295.875L,\n"
    "  -2147483647.875L,\n"
    "  4294967295.875L,\n"
    "  -9223372036854775808e0L,\n"
    "  9223372036854775807e0L,\n"
    "  18446744073709551615e0L,\n"
    "  -0.5L,\n"
    "  -3.75L,\n"
    "  18446744073709551615e0L\n"
    "};\n"
    "void cupidc_static_long_double_integer_block_probe(void) {\n"
    "  static const long double cupidc_static_integer_to_long_double_block =\n"
    "      9223372036854775807ll;\n"
    "  static const unsigned long long cupidc_static_long_double_to_integer_block =\n"
    "      9223372036854775808e0L;\n"
    "}\n";

static const cupidc_static_integer_to_long_double_oracle_t
    cupidc_static_integer_to_long_double_oracles[] = {
        {CTOOL_C_INITIALIZER_ZERO, 0ull, 0u},
        {CTOOL_C_INITIALIZER_FLOATING, 0x8000000000000000ull, 0x3fffu},
        {CTOOL_C_INITIALIZER_FLOATING, 0xfe00000000000000ull, 0x4005u},
        {CTOOL_C_INITIALIZER_FLOATING, 0x8000000000000000ull, 0xc006u},
        {CTOOL_C_INITIALIZER_FLOATING, 0xff00000000000000ull, 0x4006u},
        {CTOOL_C_INITIALIZER_FLOATING, 0x8000000000000000ull, 0xc00eu},
        {CTOOL_C_INITIALIZER_FLOATING, 0xffff000000000000ull, 0x400eu},
        {CTOOL_C_INITIALIZER_FLOATING, 0x8000000000000000ull, 0xc01eu},
        {CTOOL_C_INITIALIZER_FLOATING, 0xffffffff00000000ull, 0x401eu},
        {CTOOL_C_INITIALIZER_FLOATING, 0x8000000000000000ull, 0xc01eu},
        {CTOOL_C_INITIALIZER_FLOATING, 0xffffffff00000000ull, 0x401eu},
        {CTOOL_C_INITIALIZER_FLOATING, 0x8000000000000000ull, 0xc03eu},
        {CTOOL_C_INITIALIZER_FLOATING, 0xffffffffffffffffull, 0x403eu},
        {CTOOL_C_INITIALIZER_FLOATING, 0xc000000000000000ull, 0xc000u},
        {CTOOL_C_INITIALIZER_FLOATING, 0xffffffffffffffffull, 0x403eu}};

static const cupidc_static_long_double_to_integer_oracle_t
    cupidc_static_long_double_to_integer_oracles[] = {
        {CTOOL_C_TYPE_BOOL, (ctool_c_type_kind_t)0, CTOOL_C_QUAL_CONST, 0ull},
        {CTOOL_C_TYPE_BOOL, (ctool_c_type_kind_t)0, CTOOL_C_QUAL_CONST, 0ull},
        {CTOOL_C_TYPE_BOOL, (ctool_c_type_kind_t)0, CTOOL_C_QUAL_CONST, 1ull},
        {CTOOL_C_TYPE_BOOL, (ctool_c_type_kind_t)0, CTOOL_C_QUAL_CONST, 1ull},
        {CTOOL_C_TYPE_CHAR, (ctool_c_type_kind_t)0, CTOOL_C_QUAL_CONST, 0x7full},
        {CTOOL_C_TYPE_SIGNED_CHAR, (ctool_c_type_kind_t)0,
         CTOOL_C_QUAL_CONST, 0x81ull},
        {CTOOL_C_TYPE_UNSIGNED_CHAR, (ctool_c_type_kind_t)0,
         CTOOL_C_QUAL_CONST, 0xffull},
        {CTOOL_C_TYPE_SIGNED_SHORT, (ctool_c_type_kind_t)0,
         CTOOL_C_QUAL_CONST, 0x8001ull},
        {CTOOL_C_TYPE_UNSIGNED_SHORT, (ctool_c_type_kind_t)0,
         CTOOL_C_QUAL_CONST, 0xffffull},
        {CTOOL_C_TYPE_SIGNED_INT, (ctool_c_type_kind_t)0,
         CTOOL_C_QUAL_CONST, 0x80000001ull},
        {CTOOL_C_TYPE_UNSIGNED_INT, (ctool_c_type_kind_t)0,
         CTOOL_C_QUAL_CONST, 0xffffffffull},
        {CTOOL_C_TYPE_SIGNED_LONG, (ctool_c_type_kind_t)0,
         CTOOL_C_QUAL_CONST, 0x80000001ull},
        {CTOOL_C_TYPE_UNSIGNED_LONG, (ctool_c_type_kind_t)0,
         CTOOL_C_QUAL_CONST, 0xffffffffull},
        {CTOOL_C_TYPE_SIGNED_LONG_LONG, (ctool_c_type_kind_t)0,
         CTOOL_C_QUAL_CONST, 0x8000000000000000ull},
        {CTOOL_C_TYPE_SIGNED_LONG_LONG, (ctool_c_type_kind_t)0,
         CTOOL_C_QUAL_CONST, 0x7fffffffffffffffull},
        {CTOOL_C_TYPE_UNSIGNED_LONG_LONG, (ctool_c_type_kind_t)0,
         CTOOL_C_QUAL_CONST, 0xffffffffffffffffull},
        {CTOOL_C_TYPE_UNSIGNED_LONG_LONG, (ctool_c_type_kind_t)0,
         CTOOL_C_QUAL_CONST, 0ull},
        {CTOOL_C_TYPE_ENUM, CTOOL_C_TYPE_SIGNED_INT,
         CTOOL_C_QUAL_CONST, 0xfffffffdull},
        {CTOOL_C_TYPE_ENUM, CTOOL_C_TYPE_UNSIGNED_LONG_LONG,
         CTOOL_C_QUAL_CONST, 0xffffffffffffffffull}};

#endif
