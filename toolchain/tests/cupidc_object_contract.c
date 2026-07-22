#include "ctool.h"
#include "ctool_host.h"
#include "cupidc_emit.h"
#include "cupidc_frontend.h"
#include "cupidc_ir.h"
#include "cupidc_pp.h"
#include "elf32.h"
#include "x86.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char active_align_up[] =
    "static inline uint32_t align_up(uint32_t val, uint32_t align) {\n"
    "  return (val + align - 1) & ~(align - 1);\n"
    "}\n";

static const char active_signed_bits_negation[] =
    "  return -(ctool_i32)((~value) + 1u);";

static const char active_signed_bits[] =
    "static ctool_i32 dis_signed_bits(ctool_u32 value) {\n"
    "  if (value <= 0x7fffffffu) {\n"
    "    return (ctool_i32)value;\n"
    "  }\n"
    "  if (value == 0x80000000u) {\n"
    "    return (-2147483647 - 1);\n"
    "  }\n"
    "  return -(ctool_i32)((~value) + 1u);\n"
    "}";

static const char active_dis_hex_body[] =
    "  static const char hex[] = \"0123456789ABCDEF\";\n"
    "  char text[8];\n"
    "  ctool_u32 index;\n"
    "  for (index = 0u; index < digits; index++) {\n"
    "    ctool_u32 shift = (digits - index - 1u) * 4u;\n"
    "    text[index] = hex[(value >> shift) & 0x0fu];\n"
    "  }";

static const char active_host_release[] =
    "static void ctool_host_release(void *context, void *allocation,\n"
    "                               ctool_u32 bytes) {\n"
    "  (void)context;\n"
    "  (void)bytes;\n"
    "  free(allocation);\n"
    "}\n";

static const char active_zero_record_initializer[] =
    "  ctool_c_type_node_t node = {0};";

static const char active_serial_hex_initializer[] =
    "    const char hex[] = \"0123456789abcdef\";";

static const char active_compound_literal_call[] =
    "  return pp_string_equal(value, (ctool_string_t){literal, size});";

static const char active_integer_mask[] =
    "static ctool_u64 cfront_integer_mask(cfront_integer_kind_t kind) {\n"
    "  return cfront_integer_width(kind) == 32u ? 0xffffffffull\n"
    "                                           : 0xffffffffffffffffull;\n"
    "}\n";

static const char active_pp_if_signed_magnitude_arithmetic_object[] =
    "static ctool_u64 pp_if_signed_magnitude(ctool_u64 bits) {\n"
    "  return (bits & PP_IF_SIGN_BIT) != 0ull ? (~bits) + 1ull : bits;\n"
    "}\n";

static const char active_asm_wide_unary_arithmetic_object[] =
    "    if (expression->as.unary.op == ASM_EXPR_OP_POSITIVE) {\n"
    "      *value_out = right;\n"
    "    } else if (expression->as.unary.op == ASM_EXPR_OP_NEGATIVE) {\n"
    "      *value_out = 0ull - right;\n"
    "    } else {\n"
    "      *value_out = ~right;\n"
    "    }\n";

static const char active_x25519_wide_multiply_body[] =
    "static void fe_mul_u32(fe r, const fe a, uint32_t n) {\n"
    "    uint64_t r0 = (uint64_t)a[0] * n;\n"
    "    uint64_t r1 = (uint64_t)a[1] * n;\n"
    "    uint64_t r2 = (uint64_t)a[2] * n;\n"
    "    uint64_t r3 = (uint64_t)a[3] * n;\n"
    "    uint64_t r4 = (uint64_t)a[4] * n;\n"
    "    uint64_t r5 = (uint64_t)a[5] * n;\n"
    "    uint64_t r6 = (uint64_t)a[6] * n;\n"
    "    uint64_t r7 = (uint64_t)a[7] * n;\n"
    "    fe_carry(r, r0, r1, r2, r3, r4, r5, r6, r7);\n"
    "}\n";

static const char active_x25519_wide_multiply_body_crlf[] =
    "static void fe_mul_u32(fe r, const fe a, uint32_t n) {\r\n"
    "    uint64_t r0 = (uint64_t)a[0] * n;\r\n"
    "    uint64_t r1 = (uint64_t)a[1] * n;\r\n"
    "    uint64_t r2 = (uint64_t)a[2] * n;\r\n"
    "    uint64_t r3 = (uint64_t)a[3] * n;\r\n"
    "    uint64_t r4 = (uint64_t)a[4] * n;\r\n"
    "    uint64_t r5 = (uint64_t)a[5] * n;\r\n"
    "    uint64_t r6 = (uint64_t)a[6] * n;\r\n"
    "    uint64_t r7 = (uint64_t)a[7] * n;\r\n"
    "    fe_carry(r, r0, r1, r2, r3, r4, r5, r6, r7);\r\n"
    "}\r\n";

static const char active_cpu_frequency[] =
    "uint64_t get_cpu_freq(void) {\n"
    "    return tsc_freq;\n"
    "}\n";

static const char active_cpu_frequency_crlf[] =
    "uint64_t get_cpu_freq(void) {\r\n"
    "    return tsc_freq;\r\n"
    "}\r\n";

static const char wide_return_object_source[] =
    "typedef unsigned int ctool_u32;\n"
    "typedef unsigned long long ctool_u64;\n"
    "typedef enum { WIDE_I32, WIDE_I64 } wide_kind_t;\n"
    "static ctool_u32 integer_width(wide_kind_t kind) {\n"
    "  return kind == WIDE_I32 ? 32u : 64u;\n"
    "}\n"
    "static ctool_u64 integer_mask(wide_kind_t kind) {\n"
    "  return integer_width(kind) == 32u ? 0xffffffffull\n"
    "                                    : 0xffffffffffffffffull;\n"
    "}\n"
    "ctool_u64 wide_literal(void) { return 0x1122334455667788ull; }\n"
    "ctool_u64 direct_relay(wide_kind_t kind) {\n"
    "  return integer_mask(kind);\n"
    "}\n"
    "ctool_u64 indirect_relay(wide_kind_t kind) {\n"
    "  ctool_u64 (*callback)(wide_kind_t) = integer_mask;\n"
    "  return callback(kind);\n"
    "}\n"
    "void discard_mask(void) { integer_mask(WIDE_I32); }\n";

static const char wide_parameter_object_source[] =
    "typedef unsigned int ctool_u32;\n"
    "typedef unsigned long long ctool_u64;\n"
    "typedef __builtin_va_list va_list;\n"
    "typedef ctool_u64 (*wide_callback_t)(ctool_u32, ctool_u64,\n"
    "                                     ctool_u32, ctool_u64);\n"
    "ctool_u64 pass_wide(ctool_u64 value) { return value; }\n"
    "ctool_u64 choose_wide(ctool_u32 choose_second, ctool_u64 first,\n"
    "                       ctool_u32 marker, ctool_u64 second) {\n"
    "  (void)marker;\n"
    "  return choose_second ? second : first;\n"
    "}\n"
    "ctool_u64 call_wide(ctool_u64 value) { return pass_wide(value); }\n"
    "ctool_u64 call_mixed(ctool_u64 first, ctool_u64 second) {\n"
    "  return choose_wide(1u, first, 0xa5a5a5a5u, second);\n"
    "}\n"
    "ctool_u64 call_indirect(ctool_u32 choose_second, ctool_u64 first,\n"
    "                         ctool_u64 second) {\n"
    "  wide_callback_t callback = choose_wide;\n"
    "  return callback(choose_second, first, 0x5a5a5a5au, second);\n"
    "}\n"
    "ctool_u64 variadic_named(ctool_u64 value, ...) { return value; }\n"
    "ctool_u64 call_variadic_named(ctool_u64 value) {\n"
    "  return variadic_named(value, 0x13579bdfu);\n"
    "}\n"
    "ctool_u32 read_after_wide(ctool_u64 value, ...) {\n"
    "  va_list arguments;\n"
    "  ctool_u32 next;\n"
    "  __builtin_va_start(arguments, value);\n"
    "  next = __builtin_va_arg(arguments, ctool_u32);\n"
    "  __builtin_va_end(arguments);\n"
    "  return next;\n"
    "}\n"
    "ctool_u32 call_read_after_wide(ctool_u64 value) {\n"
    "  return read_after_wide(value, 0x13579bdfu);\n"
    "}\n";

static const char wide_variadic_argument_object_source[] =
    "typedef unsigned long long ctool_u64;\n"
    "void sink(int marker, ...);\n"
    "void pass_variadic(ctool_u64 value) { sink(0, value); }\n";

static const char wide_unprototyped_argument_object_source[] =
    "typedef unsigned long long ctool_u64;\n"
    "void sink();\n"
    "void pass_unprototyped(ctool_u64 value) { sink(value); }\n";

static const char wide_conversion_object_source[] =
    "typedef signed char ctool_i8;\n"
    "typedef unsigned char ctool_u8;\n"
    "typedef short ctool_i16;\n"
    "typedef unsigned short ctool_u16;\n"
    "typedef unsigned int ctool_u32;\n"
    "typedef unsigned long long ctool_u64;\n"
    "typedef long long ctool_i64;\n"
    "ctool_u64 widen_unsigned(ctool_u32 value) { return value; }\n"
    "ctool_i64 widen_signed(int value) { return value; }\n"
    "ctool_u64 widen_byte(ctool_u8 value) { return value; }\n"
    "ctool_i64 widen_word(ctool_i16 value) { return value; }\n"
    "ctool_i64 cast_signed_byte(ctool_i8 value) {\n"
    "  return (ctool_i64)value;\n"
    "}\n"
    "ctool_u64 cast_unsigned_word(ctool_u16 value) {\n"
    "  return (ctool_u64)value;\n"
    "}\n"
    "ctool_i64 retype_assignment(ctool_u64 value) { return value; }\n"
    "ctool_u32 narrow_unsigned(ctool_u64 value) {\n"
    "  return (ctool_u32)value;\n"
    "}\n"
    "ctool_u8 narrow_byte(ctool_u64 value) { return (ctool_u8)value; }\n"
    "ctool_u16 narrow_word(ctool_u64 value) { return (ctool_u16)value; }\n"
    "ctool_u16 narrow_assignment(ctool_u64 value) { return value; }\n"
    "ctool_i8 narrow_signed(ctool_i64 value) { return (ctool_i8)value; }\n"
    "ctool_i16 narrow_signed_word(ctool_i64 value) {\n"
    "  return (ctool_i16)value;\n"
    "}\n"
    "_Bool narrow_bool(ctool_u64 value) { return (_Bool)value; }\n";

static const char wide_operation_object_source[] =
    "typedef unsigned char ctool_u8;\n"
    "typedef unsigned int ctool_u32;\n"
    "typedef unsigned long long ctool_u64;\n"
    "typedef long long ctool_i64;\n"
    "#define PP_IF_SIGN_BIT 0x8000000000000000ull\n"
    "enum wide_enum { WIDE_ENUM_NEGATIVE = -1,\n"
    "                 WIDE_ENUM_LARGE = 0xffffffffu };\n"
    "ctool_u64 shift_left(ctool_u64 value, ctool_u32 count) {\n"
    "  return value << count;\n"
    "}\n"
    "ctool_u64 shift_right_unsigned(ctool_u64 value, ctool_u32 count) {\n"
    "  return value >> count;\n"
    "}\n"
    "ctool_i64 shift_right_signed(ctool_i64 value, ctool_u32 count) {\n"
    "  return value >> count;\n"
    "}\n"
    "ctool_u64 bitwise_and(ctool_u64 left, ctool_u64 right) {\n"
    "  return left & right;\n"
    "}\n"
    "ctool_u64 bitwise_mixed(ctool_i64 left, ctool_u64 right) {\n"
    "  return left & right;\n"
    "}\n"
    "ctool_u64 bitwise_enum(enum wide_enum left, ctool_u64 right) {\n"
    "  return left & right;\n"
    "}\n"
    "ctool_u64 bitwise_or(ctool_u64 left, ctool_u64 right) {\n"
    "  return left | right;\n"
    "}\n"
    "ctool_u64 bitwise_xor(ctool_u64 left, ctool_u64 right) {\n"
    "  return left ^ right;\n"
    "}\n"
    "ctool_u64 add_unsigned(ctool_u64 left, ctool_u64 right) {\n"
    "  return left + right;\n"
    "}\n"
    "ctool_i64 add_signed(ctool_i64 left, ctool_i64 right) {\n"
    "  return left + right;\n"
    "}\n"
    "ctool_u64 subtract_unsigned(ctool_u64 left, ctool_u64 right) {\n"
    "  return left - right;\n"
    "}\n"
    "ctool_i64 subtract_signed(ctool_i64 left, ctool_i64 right) {\n"
    "  return left - right;\n"
    "}\n"
    "ctool_u64 chained_add_subtract(ctool_u64 left, ctool_u64 right) {\n"
    "  return (left + right) - left;\n"
    "}\n"
    "ctool_u64 plus_unsigned(ctool_u64 value) { return +value; }\n"
    "ctool_i64 plus_signed(ctool_i64 value) { return +value; }\n"
    "ctool_u64 negate_unsigned(ctool_u64 value) { return -value; }\n"
    "ctool_i64 negate_signed(ctool_i64 value) { return -value; }\n"
    "ctool_u64 not_unsigned(ctool_u64 value) { return ~value; }\n"
    "ctool_i64 not_signed(ctool_i64 value) { return ~value; }\n"
    "ctool_u64 negate_twice_unsigned(ctool_u64 value) {\n"
    "  return -(-value);\n"
    "}\n"
    "ctool_i64 not_twice_signed(ctool_i64 value) {\n"
    "  return ~(~value);\n"
    "}\n"
    "ctool_u64 negate_then_add_unsigned(ctool_u64 value) {\n"
    "  return -value + value;\n"
    "}\n"
    "ctool_u64 not_then_xor_unsigned(ctool_u64 value) {\n"
    "  return (~value) ^ value;\n"
    "}\n"
    "ctool_u8 extract_byte(ctool_u64 value, ctool_u32 index) {\n"
    "  return (ctool_u8)((value >> (index * 8u)) & 0xffu);\n"
    "}\n"
    "static ctool_u64 pp_if_signed_magnitude(ctool_u64 bits) {\n"
    "  return (bits & PP_IF_SIGN_BIT) != 0ull ? (~bits) + 1ull : bits;\n"
    "}\n";

static const char wide_count_operation_object_source[] =
    "typedef unsigned long long ctool_u64;\n"
    "ctool_u64 shift_wide_count(ctool_u64 value, ctool_u64 count) {\n"
    "  return value << count;\n"
    "}\n";

static const char wide_multiplication_object_source[] =
    "typedef unsigned int ctool_u32;\n"
    "typedef unsigned long long ctool_u64;\n"
    "typedef long long ctool_i64;\n"
    "ctool_u64 multiply_unsigned(ctool_u64 left, ctool_u64 right) {\n"
    "  return left * right;\n"
    "}\n"
    "ctool_i64 multiply_signed(ctool_i64 left, ctool_i64 right) {\n"
    "  return left * right;\n"
    "}\n"
    "ctool_u64 multiply_mixed(ctool_i64 left, ctool_u64 right) {\n"
    "  return left * right;\n"
    "}\n"
    "ctool_u64 multiply_wide_narrow(ctool_u64 left, ctool_u32 right) {\n"
    "  return left * right;\n"
    "}\n"
    "ctool_u64 multiply_chained(ctool_u64 left, ctool_u64 right,\n"
    "                             ctool_u64 third) {\n"
    "  return (left * right) * third;\n"
    "}\n"
    "ctool_u64 multiply_then_reuse_left(ctool_u64 left,\n"
    "                                    ctool_u64 right) {\n"
    "  return (left * right) + left;\n"
    "}\n";

static const char wide_condition_object_source[] =
    "typedef unsigned long long ctool_u64;\n"
    "typedef long long ctool_i64;\n"
    "typedef int ctool_bool;\n"
    "typedef struct {\n"
    "  ctool_u64 bits;\n"
    "  ctool_bool is_unsigned;\n"
    "} pp_if_value_t;\n"
    "enum { CTOOL_FALSE = 0, CTOOL_TRUE = 1 };\n"
    "#define PP_IF_SIGN_BIT 0x8000000000000000ull\n"
    "int signed_less(ctool_i64 left, ctool_i64 right) { return left < right; }\n"
    "int signed_less_equal(ctool_i64 left, ctool_i64 right) { return left <= right; }\n"
    "int signed_greater(ctool_i64 left, ctool_i64 right) { return left > right; }\n"
    "int signed_greater_equal(ctool_i64 left, ctool_i64 right) { return left >= right; }\n"
    "int signed_equal(ctool_i64 left, ctool_i64 right) { return left == right; }\n"
    "int signed_not_equal(ctool_i64 left, ctool_i64 right) { return left != right; }\n"
    "int unsigned_less(ctool_u64 left, ctool_u64 right) { return left < right; }\n"
    "int unsigned_less_equal(ctool_u64 left, ctool_u64 right) { return left <= right; }\n"
    "int unsigned_greater(ctool_u64 left, ctool_u64 right) { return left > right; }\n"
    "int unsigned_greater_equal(ctool_u64 left, ctool_u64 right) { return left >= right; }\n"
    "int unsigned_equal(ctool_u64 left, ctool_u64 right) { return left == right; }\n"
    "int unsigned_not_equal(ctool_u64 left, ctool_u64 right) { return left != right; }\n"
    "int mixed_less(ctool_i64 left, ctool_u64 right) { return left < right; }\n"
    "int wide_not(ctool_u64 value) { return !value; }\n"
    "int wide_and(ctool_u64 value) {\n"
    "  int observed = 0;\n"
    "  value && (observed = 1);\n"
    "  return observed;\n"
    "}\n"
    "int wide_or(ctool_u64 value) {\n"
    "  int observed = 0;\n"
    "  value || (observed = 1);\n"
    "  return observed;\n"
    "}\n"
    "int wide_select(ctool_u64 value) { return value ? 7 : 9; }\n"
    "int wide_if(ctool_u64 value) {\n"
    "  if (value) return 11;\n"
    "  return 13;\n"
    "}\n"
    "int wide_while(ctool_u64 value) {\n"
    "  int result = 0;\n"
    "  while (value) { result = 17; break; }\n"
    "  return result;\n"
    "}\n"
    "int wide_do(ctool_u64 value) {\n"
    "  int result = 0;\n"
    "  do { result++; } while (value && result < 2);\n"
    "  return result;\n"
    "}\n"
    "int wide_for(ctool_u64 value) {\n"
    "  int result = 0;\n"
    "  for (; value;) { result = 29; break; }\n"
    "  return result;\n"
    "}\n"
    "static ctool_bool pp_if_value_truth(pp_if_value_t value) {\n"
    "  return value.bits != 0ull ? CTOOL_TRUE : CTOOL_FALSE;\n"
    "}\n"
    "static ctool_bool pp_if_is_negative(pp_if_value_t value) {\n"
    "  return value.is_unsigned == CTOOL_FALSE &&\n"
    "                 (value.bits & PP_IF_SIGN_BIT) != 0ull\n"
    "             ? CTOOL_TRUE\n"
    "             : CTOOL_FALSE;\n"
    "}\n"
    "static ctool_bool pp_if_signed_less(ctool_u64 left, ctool_u64 right) {\n"
    "  ctool_bool left_negative =\n"
    "      (left & PP_IF_SIGN_BIT) != 0ull ? CTOOL_TRUE : CTOOL_FALSE;\n"
    "  ctool_bool right_negative =\n"
    "      (right & PP_IF_SIGN_BIT) != 0ull ? CTOOL_TRUE : CTOOL_FALSE;\n"
    "  if (left_negative != right_negative) {\n"
    "    return left_negative;\n"
    "  }\n"
    "  return left < right ? CTOOL_TRUE : CTOOL_FALSE;\n"
    "}\n";

static const char wide_switch_object_source[] =
    "typedef unsigned long long ctool_u64;\n"
    "typedef long long ctool_i64;\n"
    "int signed_wide_switch(ctool_i64 value) {\n"
    "  switch (value) {\n"
    "  case -0x112233445566778ll:\n"
    "    return 11;\n"
    "  case 0x1122334455667788ll:\n"
    "    return 13;\n"
    "  default:\n"
    "    return 17;\n"
    "  }\n"
    "}\n"
    "int unsigned_wide_switch(ctool_u64 value) {\n"
    "  switch (value) {\n"
    "  case 0x1122334455667788ull:\n"
    "    return 19;\n"
    "  case 0x8877665544332211ull:\n"
    "    return 23;\n"
    "  default:\n"
    "    return 29;\n"
    "  }\n"
    "}\n";

static const char active_pp_if_value_truth_object[] =
    "static ctool_bool pp_if_value_truth(pp_if_value_t value) {\n"
    "  return value.bits != 0ull ? CTOOL_TRUE : CTOOL_FALSE;\n"
    "}\n";

static const char active_pp_if_is_negative_object[] =
    "static ctool_bool pp_if_is_negative(pp_if_value_t value) {\n"
    "  return value.is_unsigned == CTOOL_FALSE &&\n"
    "                 (value.bits & PP_IF_SIGN_BIT) != 0ull\n"
    "             ? CTOOL_TRUE\n"
    "             : CTOOL_FALSE;\n"
    "}\n";

static const char active_pp_if_signed_less_object[] =
    "static ctool_bool pp_if_signed_less(ctool_u64 left, ctool_u64 right) {\n"
    "  ctool_bool left_negative =\n"
    "      (left & PP_IF_SIGN_BIT) != 0ull ? CTOOL_TRUE : CTOOL_FALSE;\n"
    "  ctool_bool right_negative =\n"
    "      (right & PP_IF_SIGN_BIT) != 0ull ? CTOOL_TRUE : CTOOL_FALSE;\n"
    "  if (left_negative != right_negative) {\n"
    "    return left_negative;\n"
    "  }\n"
    "  return left < right ? CTOOL_TRUE : CTOOL_FALSE;\n"
    "}\n";

static const char active_wide_helper_object_source[] =
    "typedef unsigned char ctool_u8;\n"
    "typedef unsigned int ctool_u32;\n"
    "typedef unsigned long long ctool_u64;\n"
    "typedef int ctool_status_t;\n"
    "typedef struct ctool_buffer ctool_buffer_t;\n"
    "typedef struct { const ctool_u8 *data; ctool_u32 size; } ctool_bytes_t;\n"
    "ctool_bytes_t ctool_bytes(const void *data, ctool_u32 size);\n"
    "ctool_status_t ctool_buffer_append(ctool_buffer_t *buffer,\n"
    "                                    ctool_bytes_t bytes);\n"
    "ctool_status_t ctool_buffer_patch(ctool_buffer_t *buffer,\n"
    "                                   ctool_u32 offset,\n"
    "                                   const ctool_u8 *bytes,\n"
    "                                   ctool_u32 count);\n"
    "ctool_status_t ctool_buffer_put_le64(ctool_buffer_t *buffer, "
    "ctool_u64 value) {\n"
    "  ctool_u8 bytes[8];\n"
    "  ctool_u32 index;\n"
    "  for (index = 0; index < 8u; index++) {\n"
    "    bytes[index] = (ctool_u8)((value >> (index * 8u)) & 0xffu);\n"
    "  }\n"
    "  return ctool_buffer_append(buffer, ctool_bytes(bytes, 8u));\n"
    "}\n"
    "ctool_status_t ctool_buffer_patch_le64(ctool_buffer_t *buffer,\n"
    "                                       ctool_u32 offset, "
    "ctool_u64 value) {\n"
    "  ctool_u8 bytes[8];\n"
    "  ctool_u32 index;\n"
    "  for (index = 0; index < 8u; index++) {\n"
    "    bytes[index] = (ctool_u8)((value >> (index * 8u)) & 0xffu);\n"
    "  }\n"
    "  return ctool_buffer_patch(buffer, offset, bytes, 8u);\n"
    "}\n";

static const char wide_value_object_source[] =
    "typedef unsigned int ctool_u32;\n"
    "typedef unsigned long long ctool_u64;\n"
    "typedef struct { ctool_u32 tag; ctool_u64 value; } wide_holder_t;\n"
    "static ctool_u64 tsc_freq = 0x1122334455667788ull;\n"
    "ctool_u64 get_cpu_freq(void) { return tsc_freq; }\n"
    "ctool_u64 load_pointer(ctool_u64 *source) { return *source; }\n"
    "ctool_u64 local_round_trip(ctool_u64 *source) {\n"
    "  ctool_u64 local = *source;\n"
    "  return local;\n"
    "}\n"
    "ctool_u64 assign_pointer(ctool_u64 *destination, ctool_u64 *source) {\n"
    "  return *destination = *source;\n"
    "}\n"
    "ctool_u64 chain_pointer(ctool_u64 *first, ctool_u64 *second,\n"
    "                        ctool_u64 *source) {\n"
    "  return *first = *second = *source;\n"
    "}\n"
    "ctool_u64 load_member(wide_holder_t *record) {\n"
    "  return record->value;\n"
    "}\n"
    "ctool_u64 aggregate_round_trip(ctool_u64 *source) {\n"
    "  wide_holder_t local = {7u, *source};\n"
    "  return local.value;\n"
    "}\n"
    "ctool_u64 load_index(ctool_u64 *values, ctool_u32 index) {\n"
    "  return values[index];\n"
    "}\n"
    "ctool_u64 block_static(void) {\n"
    "  static ctool_u64 value = 0xaabbccddeeff0011ull;\n"
    "  return value;\n"
    "}\n"
    "ctool_u64 volatile_round_trip(volatile ctool_u64 *destination,\n"
    "                                ctool_u64 *source) {\n"
    "  *destination = *source;\n"
    "  return *destination;\n"
    "}\n"
    "void discard_pointer(ctool_u64 *source) { (void)*source; }\n";

static const char wide_atomic_object_source[] =
    "typedef unsigned long long ctool_u64;\n"
    "_Atomic ctool_u64 shared;\n"
    "ctool_u64 load_atomic(void) { return shared; }\n";

static const char void_cast_object_source[] =
    "typedef unsigned int u32;\n"
    "typedef u32 (*callback_t)(u32);\n"
    "extern void sink(void);\n"
    "void discard_values(u32 value, u32 *pointer, callback_t callback) {\n"
    "  (void)value;\n"
    "  (void)pointer;\n"
    "  (void)callback;\n"
    "  (void)sink();\n"
    "}\n";

static const char switch_object_source[] =
    "typedef enum {\n"
    "  CTOOL_C_STORAGE_NONE = 0,\n"
    "  CTOOL_C_STORAGE_TYPEDEF,\n"
    "  CTOOL_C_STORAGE_EXTERN,\n"
    "  CTOOL_C_STORAGE_STATIC,\n"
    "  CTOOL_C_STORAGE_AUTO,\n"
    "  CTOOL_C_STORAGE_REGISTER\n"
    "} ctool_c_storage_class_t;\n"
    "typedef enum {\n"
    "  CFRONT_STORAGE_NONE = 0,\n"
    "  CFRONT_STORAGE_TYPEDEF,\n"
    "  CFRONT_STORAGE_EXTERN,\n"
    "  CFRONT_STORAGE_STATIC,\n"
    "  CFRONT_STORAGE_AUTO,\n"
    "  CFRONT_STORAGE_REGISTER\n"
    "} cfront_storage_t;\n"
    "static ctool_c_storage_class_t cfront_public_storage(\n"
    "    cfront_storage_t storage) {\n"
    "  switch (storage) {\n"
    "  case CFRONT_STORAGE_TYPEDEF:\n"
    "    return CTOOL_C_STORAGE_TYPEDEF;\n"
    "  case CFRONT_STORAGE_EXTERN:\n"
    "    return CTOOL_C_STORAGE_EXTERN;\n"
    "  case CFRONT_STORAGE_STATIC:\n"
    "    return CTOOL_C_STORAGE_STATIC;\n"
    "  case CFRONT_STORAGE_AUTO:\n"
    "    return CTOOL_C_STORAGE_AUTO;\n"
    "  case CFRONT_STORAGE_REGISTER:\n"
    "    return CTOOL_C_STORAGE_REGISTER;\n"
    "  case CFRONT_STORAGE_NONE:\n"
    "  default:\n"
    "    return CTOOL_C_STORAGE_NONE;\n"
    "  }\n"
    "}\n";

static const char integer_mutation_object_source[] =
    "static unsigned int mutation_state;\n"
    "int prefix_update(int value) { return ++value; }\n"
    "unsigned int postfix_update(unsigned int value) { return value--; }\n"
    "unsigned int compound_update(unsigned int value) {\n"
    "  value *= 2u;\n"
    "  value >>= 1u;\n"
    "  return value;\n"
    "}\n"
    "unsigned int file_update(void) { return mutation_state++; }\n";

static const char pointer_value_object_source[] =
    "typedef struct { int member; } value_t;\n"
    "typedef int row_a_t[2];\n"
    "typedef const row_a_t wrapped_row_t;\n"
    "typedef const int row_b_t[2];\n"
    "value_t global_value;\n"
    "value_t *global_pointer;\n"
    "int read_indirect(int *pointer) { return *pointer; }\n"
    "int *address_member(void) { return &global_value.member; }\n"
    "void write_indirect(int *pointer, int value) { *pointer = value; }\n"
    "wrapped_row_t *pass_pointer(row_b_t *pointer) { return pointer; }\n"
    "const int *qualify_pointer(int *pointer) { return pointer; }\n"
    "void *erase_pointer(int *pointer) { return pointer; }\n"
    "int *restore_pointer(void *pointer) { return pointer; }\n"
    "wrapped_row_t *copy_pointer(row_b_t *const volatile pointer) {\n"
    "  wrapped_row_t *const volatile copy = pointer;\n"
    "  return copy;\n"
    "}\n"
    "void set_global_pointer(value_t *pointer) { global_pointer = pointer; }\n"
    "void clear_global_pointer(void) { global_pointer = 0; }\n"
    "int read_global_member(void) { return global_pointer->member; }\n"
    "wrapped_row_t *call_pointer_result(row_b_t *pointer) { return pass_pointer(pointer); }\n";

static const char pointer_comparison_object_source[] =
    "typedef struct ctool_arena ctool_arena_t;\n"
    "typedef struct { ctool_arena_t *arena; } ctool_job_t;\n"
    "ctool_arena_t *ctool_job_arena(ctool_job_t *job) {\n"
    "  return job != (ctool_job_t *)0 ? job->arena : (ctool_arena_t *)0;\n"
    "}\n"
    "int object_pointer_equal(const int *left, volatile int *right) {\n"
    "  return left == right;\n"
    "}\n"
    "int object_pointer_less(const int *left, volatile int *right) {\n"
    "  return left < right;\n"
    "}\n"
    "unsigned int object_pointer_bits(const int *pointer) {\n"
    "  return (unsigned int)pointer;\n"
    "}\n"
    "void *object_pointer_erase(int *pointer) { return (void *)pointer; }\n"
    "int *object_pointer_restore(void *pointer) { return (int *)pointer; }\n";

static const char pointer_condition_object_source[] =
    "int pointer_not(int *pointer) { return !pointer; }\n"
    "int pointer_and(int *left, int *right) { return left && right; }\n"
    "int pointer_or(int *left, int *right) { return left || right; }\n"
    "int pointer_select(int *pointer) { return pointer ? 1 : 0; }\n"
    "int pointer_if(int *pointer) { if (pointer) return 1; return 0; }\n"
    "int pointer_while(int *pointer) { while (pointer) break; return 0; }\n"
    "int pointer_do(int *pointer) {\n"
    "  do { if (pointer) break; } while (pointer);\n"
    "  return 0;\n"
    "}\n"
    "int pointer_for(int *pointer) { for (; pointer;) break; return 0; }\n";

static const char pointer_arithmetic_object_source[] =
    "typedef struct { int first; int second; int third; } triple_t;\n"
    "int *advance(int *pointer, int index) { return pointer + index; }\n"
    "int *reverse_add(int index, int *pointer) { return index + pointer; }\n"
    "int *retreat(int *pointer, int index) { return pointer - index; }\n"
    "int distance(int *end, const int *begin) { return end - begin; }\n"
    "int read_index(int *pointer, unsigned int index) { return pointer[index]; }\n"
    "int read_reverse(int *pointer, unsigned int index) { return index[pointer]; }\n"
    "char *advance_byte(char *pointer, int index) { return pointer + index; }\n"
    "triple_t *advance_triple(triple_t *pointer, int index) { return pointer + index; }\n"
    "int triple_distance(triple_t *end, const triple_t *begin) { return end - begin; }\n"
    "static int global_values[4];\n"
    "int *global_start(void) { return global_values; }\n"
    "int read_global_index(unsigned int index) { return global_values[index]; }\n"
    "int *prefix_advance(int *pointer) { return ++pointer; }\n"
    "int *postfix_retreat(int *pointer) { return pointer--; }\n"
    "int *assign_advance(int *pointer, unsigned int index) { return pointer += index; }\n"
    "int *assign_retreat(int *pointer, unsigned int index) { return pointer -= index; }\n"
    "typedef unsigned short uint16_t;\n"
    "uint16_t *advance_read_sector(uint16_t *buf) { return buf += 256; }\n"
    "const uint16_t *advance_write_sector(const uint16_t *buf) { return buf += 256; }\n"
    "int *postfix_advance(int *pointer) { return pointer++; }\n"
    "int *prefix_retreat(int *pointer) { return --pointer; }\n";

static const char active_initializer_success[] =
    "  return !cc->error;";

static const char active_sleep[] =
    "static void syscall_sleep_ms(uint32_t ms) {\n"
    "  uint32_t start = timer_get_uptime_ms();\n"
    "  while ((timer_get_uptime_ms() - start) < ms) {\n"
    "    process_yield();\n"
    "  }\n"
    "}";
static const char active_sleep_crlf[] =
    "static void syscall_sleep_ms(uint32_t ms) {\r\n"
    "  uint32_t start = timer_get_uptime_ms();\r\n"
    "  while ((timer_get_uptime_ms() - start) < ms) {\r\n"
    "    process_yield();\r\n"
    "  }\r\n"
    "}";

static const char active_for_header[] =
    "for (i = 0; i < 8; i = i + 1)";

static const char active_declaration_for_header[] =
    "for (int i = 0; i < total_sfnt; i = i + 1)";

static const char active_nested_declaration[] =
    "    const ctool_c_initializer_t *initializer =\n"
    "        &context->unit->initializers[index];\n"
    "    ctool_u32 child_offset;";
static const char active_nested_declaration_crlf[] =
    "    const ctool_c_initializer_t *initializer =\r\n"
    "        &context->unit->initializers[index];\r\n"
    "    ctool_u32 child_offset;";

static const char active_loop_continue[] =
    "    if (initializer->kind != CTOOL_C_INITIALIZER_LIST) {\n"
    "      continue;\n"
    "    }";
static const char active_loop_continue_crlf[] =
    "    if (initializer->kind != CTOOL_C_INITIALIZER_LIST) {\r\n"
    "      continue;\r\n"
    "    }";
static const char active_loop_break[] =
    "      invalid_location = &initializer->location;\n"
    "      valid = CTOOL_FALSE;\n"
    "      break;\n"
    "    }";
static const char active_loop_break_crlf[] =
    "      invalid_location = &initializer->location;\r\n"
    "      valid = CTOOL_FALSE;\r\n"
    "      break;\r\n"
    "    }";
static const char active_linker_goto[] =
    "  ctool_status_t status = ld_find_entry(link, &entry);\n"
    "  if (status != CTOOL_OK) {\n"
    "    goto done;\n"
    "  }";
static const char active_linker_goto_crlf[] =
    "  ctool_status_t status = ld_find_entry(link, &entry);\r\n"
    "  if (status != CTOOL_OK) {\r\n"
    "    goto done;\r\n"
    "  }";
static const char active_linker_label[] =
    "done:\n"
    "  if (status != CTOOL_OK &&\n"
    "      ctool_job_diagnostic_count(link->job) == diagnostics_before) {";
static const char active_linker_label_crlf[] =
    "done:\r\n"
    "  if (status != CTOOL_OK &&\r\n"
    "      ctool_job_diagnostic_count(link->job) == diagnostics_before) {";

static const char active_invocation_body_call[] =
    "    status = body(&invocation, user_data);";

static const char active_wide_parameter_function[] =
    "ctool_status_t ctool_buffer_put_le64(ctool_buffer_t *buffer, "
    "ctool_u64 value) {";

static const char active_wide_put_body[] =
    "ctool_status_t ctool_buffer_put_le64(ctool_buffer_t *buffer, "
    "ctool_u64 value) {\n"
    "  ctool_u8 bytes[8];\n"
    "  ctool_u32 index;\n"
    "  for (index = 0; index < 8u; index++) {\n"
    "    bytes[index] = (ctool_u8)((value >> (index * 8u)) & 0xffu);\n"
    "  }\n"
    "  return ctool_buffer_append(buffer, ctool_bytes(bytes, 8u));\n"
    "}";

static const char active_wide_put_body_crlf[] =
    "ctool_status_t ctool_buffer_put_le64(ctool_buffer_t *buffer, "
    "ctool_u64 value) {\r\n"
    "  ctool_u8 bytes[8];\r\n"
    "  ctool_u32 index;\r\n"
    "  for (index = 0; index < 8u; index++) {\r\n"
    "    bytes[index] = (ctool_u8)((value >> (index * 8u)) & 0xffu);\r\n"
    "  }\r\n"
    "  return ctool_buffer_append(buffer, ctool_bytes(bytes, 8u));\r\n"
    "}";

static const char active_wide_patch_body[] =
    "ctool_status_t ctool_buffer_patch_le64(ctool_buffer_t *buffer,\n"
    "                                       ctool_u32 offset, ctool_u64 value) {\n"
    "  ctool_u8 bytes[8];\n"
    "  ctool_u32 index;\n"
    "  for (index = 0; index < 8u; index++) {\n"
    "    bytes[index] = (ctool_u8)((value >> (index * 8u)) & 0xffu);\n"
    "  }\n"
    "  return ctool_buffer_patch(buffer, offset, bytes, 8u);\n"
    "}";

static const char active_wide_patch_body_crlf[] =
    "ctool_status_t ctool_buffer_patch_le64(ctool_buffer_t *buffer,\r\n"
    "                                       ctool_u32 offset, ctool_u64 value) {\r\n"
    "  ctool_u8 bytes[8];\r\n"
    "  ctool_u32 index;\r\n"
    "  for (index = 0; index < 8u; index++) {\r\n"
    "    bytes[index] = (ctool_u8)((value >> (index * 8u)) & 0xffu);\r\n"
    "  }\r\n"
    "  return ctool_buffer_patch(buffer, offset, bytes, 8u);\r\n"
    "}";

static const char active_wide_argument_call[] =
    "              status = ctool_buffer_put_le64(output, evaluated);";

static const char active_linker_selector_call[] =
    "          selector(section, selector_context) == CTOOL_TRUE &&";

static const char active_doom_tick_loop[] =
    "\tdo\n"
    "\t{\n"
    "\t    nowtime = I_GetTime ();\n"
    "\t    tics = nowtime - wipestart;\n"
    "            I_Sleep(1);\n"
    "\t} while (tics <= 0);";
static const char active_doom_tick_loop_crlf[] =
    "\tdo\r\n"
    "\t{\r\n"
    "\t    nowtime = I_GetTime ();\r\n"
    "\t    tics = nowtime - wipestart;\r\n"
    "            I_Sleep(1);\r\n"
    "\t} while (tics <= 0);";

typedef struct {
  ctool_c_translation_unit_t unit;
  ctool_c_binding_t *bindings;
  ctool_c_object_definition_t *object_definitions;
  ctool_c_block_binding_t *block_bindings;
  ctool_c_initializer_t *initializers;
  ctool_c_initializer_element_t *initializer_elements;
  ctool_c_label_t *labels;
  ctool_c_function_definition_t *function_definitions;
  ctool_c_statement_t *statements;
  ctool_u32 *statement_children;
  ctool_c_expression_t *expressions;
  ctool_u32 *expression_children;
} unit_snapshot_t;

static int check_status(ctool_status_t actual, ctool_status_t expected,
                        const char *context) {
  if (actual == expected) {
    return 1;
  }
  (void)fprintf(stderr, "%s: expected %s, got %s\n", context,
                ctool_status_name(expected), ctool_status_name(actual));
  return 0;
}

static int string_equal(ctool_string_t actual, const char *expected) {
  size_t expected_size = strlen(expected);
  return actual.size == (ctool_u32)expected_size &&
                 (expected_size == 0u ||
                  memcmp(actual.data, expected, expected_size) == 0)
             ? 1
             : 0;
}

static int arena_marks_equal(ctool_arena_mark_t left,
                             ctool_arena_mark_t right) {
  return left.owner == right.owner && left.block == right.block &&
                 left.used == right.used &&
                 left.generation == right.generation
             ? 1
             : 0;
}

static const ctool_elf32_section_t *find_section(
    const ctool_elf32_object_t *object, const char *name) {
  ctool_u32 index;
  for (index = 0u; index < object->section_count; index++) {
    if (string_equal(object->sections[index].name, name) != 0) {
      return &object->sections[index];
    }
  }
  return (const ctool_elf32_section_t *)0;
}

static const ctool_elf32_symbol_t *find_symbol(
    const ctool_elf32_object_t *object, const char *name) {
  ctool_u32 index;
  for (index = 0u; index < object->symbol_count; index++) {
    if (string_equal(object->symbols[index].name, name) != 0) {
      return &object->symbols[index];
    }
  }
  return (const ctool_elf32_symbol_t *)0;
}

static int open_job(const char *host_root, ctool_host_adapter_t *adapter,
                    ctool_job_config_t *config, ctool_job_t **job_out) {
  ctool_status_t status = ctool_host_adapter_init(adapter, host_root);
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 0;
  }
  *config = ctool_host_job_config(adapter, ctool_default_limits());
  status = ctool_job_open(config, job_out);
  return check_status(status, CTOOL_OK, "job open");
}

static int parse_source_mode(ctool_job_t *job, const char *path,
                             const char *text,
                             ctool_bool gnu_extensions,
                             ctool_c_translation_unit_t *unit_out) {
  ctool_source_t source;
  ctool_c_pp_request_t pp_request;
  ctool_c_pp_result_t tape;
  ctool_c_parse_request_t parse_request;
  ctool_status_t status;
  ctool_u32 diagnostic_count = ctool_job_diagnostic_count(job);
  size_t text_size = strlen(text);

  if (text_size > 0xffffffffu) {
    (void)fprintf(stderr, "%s: source is too large for the contract\n", path);
    return 0;
  }
  source.path.text = ctool_string(path);
  source.contents = ctool_bytes(text, (ctool_u32)text_size);
  (void)memset(&pp_request, 0, sizeof(pp_request));
  pp_request.mode = CTOOL_C_PP_MODE_C11;
  pp_request.gnu_extensions = gnu_extensions;
  pp_request.hosted_environment = CTOOL_FALSE;
  (void)memset(&tape, 0xa5, sizeof(tape));
  status = ctool_c_preprocess(job, &source, &pp_request, &tape);
  if (status != CTOOL_OK || tape.tokens == (const ctool_c_pp_token_t *)0 ||
      tape.token_count == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count) {
    (void)fprintf(stderr, "%s: preprocessing failed: %s\n", path,
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    return 0;
  }

  (void)memset(&parse_request, 0, sizeof(parse_request));
  parse_request.mode = CTOOL_C_PP_MODE_C11;
  parse_request.gnu_extensions = gnu_extensions;
  (void)memset(unit_out, 0xa5, sizeof(*unit_out));
  status = ctool_c_parse(job, &tape, &parse_request, unit_out);
  if (status != CTOOL_OK ||
      ctool_job_diagnostic_count(job) != diagnostic_count) {
    (void)fprintf(stderr, "%s: parsing failed: %s\n", path,
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    return 0;
  }
  return 1;
}

static int parse_source(ctool_job_t *job, const char *path, const char *text,
                        ctool_c_translation_unit_t *unit_out) {
  return parse_source_mode(job, path, text, CTOOL_FALSE, unit_out);
}

static int active_source_contains(ctool_job_t *job, const char *path_text,
                                  const char *load_context,
                                  const char *change_message,
                                  const char *expected,
                                  const char *alternate) {
  ctool_path_t path;
  ctool_source_t source;
  ctool_status_t status;
  path.text = ctool_string(path_text);
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, load_context) ||
      source.contents.data == NULL || expected == NULL ||
      (strstr((const char *)source.contents.data, expected) == NULL &&
       (alternate == NULL ||
        strstr((const char *)source.contents.data, alternate) == NULL))) {
    (void)fprintf(stderr, "%s\n", change_message);
    return 0;
  }
  return 1;
}

static int active_object_sources_are_unchanged(ctool_job_t *job) {
  return active_source_contains(
             job, "/kernel/mm/memory.c", "load active memory source",
             "the active memory alignment helper changed", active_align_up,
             NULL) &&
         active_source_contains(
              job, "/toolchain/cupiddis.c",
              "load active disassembler source",
              "the active signed-bit conversion changed", active_signed_bits,
              NULL) &&
         active_source_contains(
             job, "/toolchain/cupiddis.c",
             "load active disassembler source",
             "the active fixed-width hexadecimal helper changed",
             active_dis_hex_body, NULL) &&
         active_source_contains(
             job, "/bin/cupidc_parse.c", "load active CupidC source",
             "the active initializer result changed",
             active_initializer_success, NULL) &&
         active_source_contains(
             job, "/kernel/core/syscall.c", "load active syscall source",
             "the active sleep helper changed", active_sleep,
             active_sleep_crlf) &&
         active_source_contains(
             job, "/kernel/doom/src/d_main.c",
             "load active Doom display source",
             "the active Doom tick loop changed", active_doom_tick_loop,
             active_doom_tick_loop_crlf) &&
         active_source_contains(
             job, "/bin/browser/url_hash.cc",
             "load active browser for loop",
             "the active browser for loop changed", active_for_header,
             NULL) &&
         active_source_contains(job, "/bin/browser/woff.cc",
                                "load active browser declaration loop",
                                "the active browser declaration loop changed",
                                active_declaration_for_header, NULL) &&
         active_source_contains(
             job, "/toolchain/cupidc_ir.c",
             "load active CupidC IR source",
             "the active CupidC IR continue changed", active_loop_continue,
             active_loop_continue_crlf) &&
         active_source_contains(
             job, "/toolchain/cupidc_ir.c",
             "load active CupidC IR source",
             "the active CupidC IR break changed", active_loop_break,
             active_loop_break_crlf) &&
         active_source_contains(
             job, "/toolchain/cupidc_ir.c", "load active CupidC IR source",
             "the active CupidC IR nested declaration changed",
             active_nested_declaration, active_nested_declaration_crlf) &&
         active_source_contains(
             job, "/toolchain/cupidld.c", "load active linker source",
             "the active linker goto changed", active_linker_goto,
             active_linker_goto_crlf) &&
         active_source_contains(
             job, "/toolchain/cupidld.c", "load active linker source",
             "the active linker label changed", active_linker_label,
             active_linker_label_crlf) &&
          active_source_contains(
              job, "/toolchain/ctool.c", "load active core source",
              "the active invocation callback changed",
              active_invocation_body_call, NULL) &&
          active_source_contains(
              job, "/toolchain/ctool.c", "load active core source",
              "the active wide parameter function changed",
              active_wide_parameter_function, NULL) &&
          active_source_contains(
              job, "/toolchain/ctool.c", "load active core source",
              "the active wide put helper changed", active_wide_put_body,
              active_wide_put_body_crlf) &&
          active_source_contains(
              job, "/toolchain/ctool.c", "load active core source",
              "the active wide patch helper changed", active_wide_patch_body,
              active_wide_patch_body_crlf) &&
          active_source_contains(
              job, "/toolchain/cupidasm.c", "load active assembler source",
              "the active wide argument call changed",
              active_wide_argument_call, NULL) &&
          active_source_contains(
              job, "/toolchain/cupidasm.c", "load active assembler source",
              "the active wide unary branch changed",
              active_asm_wide_unary_arithmetic_object, NULL) &&
          active_source_contains(
              job, "/toolchain/cupidc_pp.c",
              "load active preprocessor arithmetic source",
              "the active preprocessor magnitude helper changed",
              active_pp_if_signed_magnitude_arithmetic_object, NULL) &&
          active_source_contains(
             job, "/toolchain/cupidld.c", "load active linker source",
             "the active linker selector callback changed",
             active_linker_selector_call, NULL) &&
         active_source_contains(
              job, "/toolchain/ctool_host.c", "load active host source",
              "the active host release helper changed", active_host_release,
              NULL) &&
         active_source_contains(
             job, "/toolchain/cupidc_frontend.c",
             "load active CupidC frontend source",
             "the active integer mask helper changed", active_integer_mask,
             NULL) &&
         active_source_contains(
             job, "/kernel/core/kernel.c",
             "load active CPU frequency source",
             "the active CPU frequency helper changed",
             active_cpu_frequency, active_cpu_frequency_crlf);
}

static char *make_align_up_fixture(void) {
  static const char prefix[] = "typedef unsigned int uint32_t;\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_align_up);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_align_up,
                 sizeof(active_align_up));
  }
  return text;
}

static char *make_do_fixture(void) {
  static const char prefix[] =
      "int I_GetTime(void);\n"
      "void I_Sleep(int delay);\n"
      "static void doom_wait_tick(int wipestart) {\n"
      "  int nowtime;\n"
      "  int tics;\n";
  static const char suffix[] = "\n}\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_doom_tick_loop) - 1u +
                sizeof(suffix);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    size_t offset = sizeof(prefix) - 1u;
    (void)memcpy(text, prefix, offset);
    (void)memcpy(text + offset, active_doom_tick_loop,
                 sizeof(active_doom_tick_loop) - 1u);
    offset += sizeof(active_doom_tick_loop) - 1u;
    (void)memcpy(text + offset, suffix, sizeof(suffix));
  }
  return text;
}

static char *make_integer_cast_fixture(void) {
  static const char prefix[] =
      "typedef signed int ctool_i32;\n"
      "typedef unsigned int ctool_u32;\n"
      "ctool_i32 signed_bits_magnitude(ctool_u32 value) {\n";
  static const char suffix[] =
      "\n}\n"
      "ctool_u32 unsigned_bits(ctool_i32 value) {\n"
      "  return (ctool_u32)value;\n"
      "}\n";
  size_t size = sizeof(prefix) - 1u +
                sizeof(active_signed_bits_negation) - 1u + sizeof(suffix);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    size_t offset = sizeof(prefix) - 1u;
    (void)memcpy(text, prefix, offset);
    (void)memcpy(text + offset, active_signed_bits_negation,
                 sizeof(active_signed_bits_negation) - 1u);
    offset += sizeof(active_signed_bits_negation) - 1u;
    (void)memcpy(text + offset, suffix, sizeof(suffix));
  }
  return text;
}

static char *make_signed_bits_fixture(void) {
  static const char prefix[] =
      "typedef signed int ctool_i32;\n"
      "typedef unsigned int ctool_u32;\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_signed_bits) + 1u;
  char *text = (char *)malloc(size);
  if (text != NULL) {
    size_t offset = sizeof(prefix) - 1u;
    (void)memcpy(text, prefix, offset);
    (void)memcpy(text + offset, active_signed_bits,
                 sizeof(active_signed_bits) - 1u);
    offset += sizeof(active_signed_bits) - 1u;
    text[offset++] = '\n';
    text[offset] = '\0';
  }
  return text;
}

static int copy_array(const void *source, ctool_u32 count, size_t item_size,
                      void **copy_out) {
  size_t bytes;
  void *copy;
  *copy_out = NULL;
  if (count == 0u) {
    return 1;
  }
  if (item_size > SIZE_MAX / (size_t)count) {
    return 0;
  }
  bytes = item_size * (size_t)count;
  copy = malloc(bytes);
  if (copy == NULL) {
    return 0;
  }
  (void)memcpy(copy, source, bytes);
  *copy_out = copy;
  return 1;
}

static int take_unit_snapshot(const ctool_c_translation_unit_t *unit,
                              unit_snapshot_t *snapshot) {
  void *bindings = NULL;
  void *object_definitions = NULL;
  void *block_bindings = NULL;
  void *initializers = NULL;
  void *initializer_elements = NULL;
  void *labels = NULL;
  void *function_definitions = NULL;
  void *statements = NULL;
  void *statement_children = NULL;
  void *expressions = NULL;
  void *expression_children = NULL;
  (void)memset(snapshot, 0, sizeof(*snapshot));
  snapshot->unit = *unit;
  if (copy_array(unit->bindings, unit->binding_count,
                 sizeof(*unit->bindings), &bindings) == 0 ||
      copy_array(unit->object_definitions, unit->object_definition_count,
                  sizeof(*unit->object_definitions), &object_definitions) == 0 ||
      copy_array(unit->block_bindings, unit->block_binding_count,
                 sizeof(*unit->block_bindings), &block_bindings) == 0 ||
      copy_array(unit->initializers, unit->initializer_count,
                  sizeof(*unit->initializers), &initializers) == 0 ||
      copy_array(unit->initializer_elements, unit->initializer_element_count,
                 sizeof(*unit->initializer_elements),
                 &initializer_elements) == 0 ||
      copy_array(unit->labels, unit->label_count, sizeof(*unit->labels),
                 &labels) == 0 ||
      copy_array(unit->function_definitions,
                 unit->function_definition_count,
                 sizeof(*unit->function_definitions),
                 &function_definitions) == 0 ||
      copy_array(unit->statements, unit->statement_count,
                 sizeof(*unit->statements), &statements) == 0 ||
      copy_array(unit->statement_children, unit->statement_child_count,
                 sizeof(*unit->statement_children),
                 &statement_children) == 0 ||
      copy_array(unit->expressions, unit->expression_count,
                 sizeof(*unit->expressions), &expressions) == 0 ||
      copy_array(unit->expression_children, unit->expression_child_count,
                 sizeof(*unit->expression_children),
                 &expression_children) == 0) {
    free(expression_children);
    free(expressions);
    free(statement_children);
    free(statements);
    free(function_definitions);
    free(labels);
    free(initializer_elements);
    free(initializers);
    free(block_bindings);
    free(object_definitions);
    free(bindings);
    (void)memset(snapshot, 0, sizeof(*snapshot));
    return 0;
  }
  snapshot->bindings = (ctool_c_binding_t *)bindings;
  snapshot->object_definitions =
      (ctool_c_object_definition_t *)object_definitions;
  snapshot->block_bindings = (ctool_c_block_binding_t *)block_bindings;
  snapshot->initializers = (ctool_c_initializer_t *)initializers;
  snapshot->initializer_elements =
      (ctool_c_initializer_element_t *)initializer_elements;
  snapshot->labels = (ctool_c_label_t *)labels;
  snapshot->function_definitions =
      (ctool_c_function_definition_t *)function_definitions;
  snapshot->statements = (ctool_c_statement_t *)statements;
  snapshot->statement_children = (ctool_u32 *)statement_children;
  snapshot->expressions = (ctool_c_expression_t *)expressions;
  snapshot->expression_children = (ctool_u32 *)expression_children;
  return 1;
}

static int unit_snapshot_matches(const unit_snapshot_t *snapshot,
                                 const ctool_c_translation_unit_t *unit) {
  return memcmp(&snapshot->unit, unit, sizeof(*unit)) == 0 &&
                 (unit->binding_count == 0u ||
                  memcmp(snapshot->bindings, unit->bindings,
                         (size_t)unit->binding_count *
                             sizeof(*unit->bindings)) == 0) &&
                 (unit->object_definition_count == 0u ||
                  memcmp(snapshot->object_definitions,
                         unit->object_definitions,
                         (size_t)unit->object_definition_count *
                              sizeof(*unit->object_definitions)) == 0) &&
                 (unit->block_binding_count == 0u ||
                  memcmp(snapshot->block_bindings, unit->block_bindings,
                         (size_t)unit->block_binding_count *
                             sizeof(*unit->block_bindings)) == 0) &&
                 (unit->initializer_count == 0u ||
                  memcmp(snapshot->initializers, unit->initializers,
                          (size_t)unit->initializer_count *
                              sizeof(*unit->initializers)) == 0) &&
                 (unit->initializer_element_count == 0u ||
                  memcmp(snapshot->initializer_elements,
                         unit->initializer_elements,
                         (size_t)unit->initializer_element_count *
                             sizeof(*unit->initializer_elements)) == 0) &&
                 (unit->label_count == 0u ||
                  memcmp(snapshot->labels, unit->labels,
                         (size_t)unit->label_count *
                             sizeof(*unit->labels)) == 0) &&
                 (unit->function_definition_count == 0u ||
                  memcmp(snapshot->function_definitions,
                         unit->function_definitions,
                         (size_t)unit->function_definition_count *
                             sizeof(*unit->function_definitions)) == 0) &&
                 (unit->statement_count == 0u ||
                  memcmp(snapshot->statements, unit->statements,
                         (size_t)unit->statement_count *
                             sizeof(*unit->statements)) == 0) &&
                 (unit->statement_child_count == 0u ||
                  memcmp(snapshot->statement_children,
                         unit->statement_children,
                         (size_t)unit->statement_child_count *
                             sizeof(*unit->statement_children)) == 0) &&
                 (unit->expression_count == 0u ||
                  memcmp(snapshot->expressions, unit->expressions,
                         (size_t)unit->expression_count *
                             sizeof(*unit->expressions)) == 0) &&
                 (unit->expression_child_count == 0u ||
                  memcmp(snapshot->expression_children,
                         unit->expression_children,
                         (size_t)unit->expression_child_count *
                             sizeof(*unit->expression_children)) == 0)
             ? 1
             : 0;
}

static void dispose_unit_snapshot(unit_snapshot_t *snapshot) {
  free(snapshot->expression_children);
  free(snapshot->expressions);
  free(snapshot->statement_children);
  free(snapshot->statements);
  free(snapshot->function_definitions);
  free(snapshot->labels);
  free(snapshot->initializer_elements);
  free(snapshot->initializers);
  free(snapshot->block_bindings);
  free(snapshot->object_definitions);
  free(snapshot->bindings);
  (void)memset(snapshot, 0, sizeof(*snapshot));
}

static int expect_new_diagnostic(const ctool_job_t *job, ctool_u32 before,
                                 ctool_u32 code,
                                 const char *expected_message,
                                 const char *context) {
  const ctool_diagnostic_t *diagnostic;
  if (ctool_job_diagnostic_count(job) != before + 1u) {
    (void)fprintf(stderr, "%s: expected one diagnostic\n", context);
    return 0;
  }
  diagnostic = ctool_job_diagnostic(job, before);
  if (diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->severity != CTOOL_DIAG_ERROR || diagnostic->code != code ||
      diagnostic->message.size == 0u ||
      (expected_message != NULL &&
       string_equal(diagnostic->message, expected_message) == 0)) {
    (void)fprintf(stderr, "%s: diagnostic differs\n", context);
    return 0;
  }
  return 1;
}

static int expect_object_failure(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_buffer_t *output, ctool_status_t expected_status,
    ctool_u32 expected_code, const char *expected_message,
    const char *context) {
  ctool_u32 diagnostic_count = ctool_job_diagnostic_count(job);
  ctool_arena_mark_t mark = ctool_arena_mark(ctool_job_arena(job));
  ctool_status_t status = ctool_c_emit_object(job, unit, output);
  if (!check_status(status, expected_status, context) ||
      ctool_buffer_view(output).size != 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      !expect_new_diagnostic(job, diagnostic_count, expected_code,
                             expected_message, context)) {
    (void)fprintf(stderr, "%s: failure transaction differs\n", context);
    return 0;
  }
  return 1;
}

static int expect_object_failure_preserves_unit(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_buffer_t *output, ctool_status_t expected_status,
    ctool_u32 expected_code, const char *expected_message,
    const char *context) {
  unit_snapshot_t snapshot;
  int matches;
  if (take_unit_snapshot(unit, &snapshot) == 0) {
    (void)fprintf(stderr, "%s: input snapshot allocation failed\n", context);
    return 0;
  }
  matches = expect_object_failure(
                job, unit, output, expected_status, expected_code,
                expected_message, context) &&
            unit_snapshot_matches(&snapshot, unit) != 0;
  dispose_unit_snapshot(&snapshot);
  return matches;
}

static int expect_object_success_preserves_unit(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_buffer_t *output, const char *context) {
  unit_snapshot_t snapshot;
  ctool_u32 diagnostic_count = ctool_job_diagnostic_count(job);
  ctool_arena_mark_t mark = ctool_arena_mark(ctool_job_arena(job));
  ctool_status_t status;
  int matches;
  if (ctool_buffer_view(output).size != 0u ||
      take_unit_snapshot(unit, &snapshot) == 0) {
    (void)fprintf(stderr, "%s: success setup differs\n", context);
    return 0;
  }
  status = ctool_c_emit_object(job, unit, output);
  matches = check_status(status, CTOOL_OK, context) &&
            ctool_buffer_view(output).size != 0u &&
            ctool_job_diagnostic_count(job) == diagnostic_count &&
            arena_marks_equal(
                mark, ctool_arena_mark(ctool_job_arena(job))) != 0 &&
            unit_snapshot_matches(&snapshot, unit) != 0;
  if (matches == 0) {
    (void)fprintf(stderr, "%s: success transaction differs\n", context);
  }
  dispose_unit_snapshot(&snapshot);
  return matches;
}

static int symbol_matches(const ctool_elf32_symbol_t *symbol,
                          ctool_u32 file_index, ctool_u32 binding,
                          ctool_u32 type,
                          ctool_elf32_symbol_placement_t placement,
                          ctool_u32 section_file_index, ctool_u32 value,
                          ctool_u32 size) {
  return symbol != (const ctool_elf32_symbol_t *)0 &&
                 symbol->file_index == file_index &&
                 symbol->binding == binding && symbol->type == type &&
                 symbol->visibility == CTOOL_ELF32_VIS_DEFAULT &&
                 symbol->placement == placement &&
                 symbol->section_file_index == section_file_index &&
                 symbol->value == value && symbol->size == size
             ? 1
             : 0;
}

static int decode_function(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *symbol,
    const ctool_x86_mnemonic_t *expected, ctool_u32 expected_count,
    const ctool_u8 *expected_bytes, ctool_u32 expected_size,
    const ctool_u32 *expected_branch_targets,
    ctool_u32 expected_branch_count,
    const char *context) {
  ctool_u32 cursor = 0u;
  ctool_u32 branch_index = 0u;
  ctool_u32 index;
  if (text == NULL || symbol == NULL || symbol->value > text->contents.size ||
      symbol->size > text->contents.size - symbol->value) {
    (void)fprintf(stderr, "%s: function range differs\n", context);
    return 0;
  }
  if (symbol->size != expected_size ||
      (expected_size != 0u &&
       memcmp(text->contents.data + symbol->value, expected_bytes,
              (size_t)expected_size) != 0)) {
    (void)fprintf(stderr, "%s: exact machine bytes differ\n", context);
    return 0;
  }
  for (index = 0u; index < expected_count; index++) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining;
    ctool_status_t status;
    if (cursor >= symbol->size) {
      (void)fprintf(stderr, "%s: instruction stream ended early\n", context);
      return 0;
    }
    remaining = ctool_bytes(text->contents.data + symbol->value + cursor,
                            symbol->size - cursor);
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u ||
        decoded.instruction.mnemonic != expected[index]) {
      (void)fprintf(stderr,
                    "%s: instruction %u at %u differs: expected %s, got %s; "
                    "bytes %02x %02x %02x %02x\n",
                    context, index, cursor,
                    ctool_x86_mnemonic_name(expected[index]).data,
                    status == CTOOL_OK &&
                            decoded.kind == CTOOL_X86_DECODE_KNOWN
                        ? ctool_x86_mnemonic_name(
                              decoded.instruction.mnemonic)
                              .data
                        : "invalid",
                    remaining.size > 0u ? remaining.data[0] : 0u,
                    remaining.size > 1u ? remaining.data[1] : 0u,
                    remaining.size > 2u ? remaining.data[2] : 0u,
                    remaining.size > 3u ? remaining.data[3] : 0u);
      return 0;
    }
    if (expected[index] == CTOOL_X86_MN_JE ||
        expected[index] == CTOOL_X86_MN_JMP) {
      int64_t target;
      int32_t displacement;
      if (branch_index >= expected_branch_count ||
          expected_branch_targets == NULL ||
          decoded.instruction.operand_count != 1u ||
          decoded.instruction.operands[0].kind !=
              CTOOL_X86_OPERAND_RELATIVE ||
          decoded.instruction.operands[0].as.value.kind !=
              CTOOL_X86_VALUE_CONSTANT ||
          decoded.encoding.field_count != 1u ||
          decoded.encoding.fields[0].kind != CTOOL_X86_FIELD_RELATIVE ||
          decoded.encoding.fields[0].byte_width != 4u) {
        (void)fprintf(stderr, "%s: branch %u shape differs\n", context,
                      branch_index);
        return 0;
      }
      displacement =
          (int32_t)decoded.instruction.operands[0].as.value.bits;
      target = (int64_t)cursor + (int64_t)decoded.consumed +
               (int64_t)displacement;
      if (target != (int64_t)expected_branch_targets[branch_index] ||
          decoded.encoding.fields[0].encoded_addend != displacement) {
        (void)fprintf(stderr,
                      "%s: branch %u target differs: expected %u, got %lld\n",
                      context, branch_index,
                      expected_branch_targets[branch_index],
                      (long long)target);
        return 0;
      }
      branch_index++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != symbol->size || branch_index != expected_branch_count) {
    (void)fprintf(stderr, "%s: trailing function bytes differ\n", context);
    return 0;
  }
  return 1;
}

static int validate_direct_goto_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 forward_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x05u, 0x00u, 0x00u, 0x00u, 0xe9u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x68u, 0x01u, 0x00u, 0x00u,
      0x00u, 0x58u, 0xc9u, 0xc3u, 0x68u, 0x02u, 0x00u, 0x00u,
      0x00u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t forward_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_JMP,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_u32 forward_targets[] = {28u, 36u};
  static const ctool_u8 backward_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x27u, 0x00u, 0x00u, 0x00u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0x59u, 0x58u,
      0x29u, 0xc8u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x51u,
      0x58u, 0xe9u, 0xc5u, 0xffu, 0xffu, 0xffu, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t backward_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 backward_targets[] = {62u, 3u};
  static const ctool_u8 terminal_if_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0xe9u, 0x0eu, 0x00u, 0x00u, 0x00u,
      0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0x58u, 0x85u, 0xc0u,
      0x0fu, 0x84u, 0x08u, 0x00u, 0x00u, 0x00u, 0x68u, 0x03u,
      0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u, 0x68u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 terminal_while_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0xe9u, 0x0eu, 0x00u, 0x00u, 0x00u,
      0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0x58u, 0x85u, 0xc0u,
      0x0fu, 0x84u, 0x08u, 0x00u, 0x00u, 0x00u, 0x68u, 0x04u,
      0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u, 0x68u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t terminal_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 terminal_targets[] = {22u, 30u};
  static const ctool_u8 declaration_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x04u, 0xe9u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x8du, 0x45u, 0xfcu, 0x50u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x8du, 0x45u,
      0xfcu, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u,
      0xc3u};
  static const ctool_x86_mnemonic_t declaration_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_u32 declaration_targets[] = {11u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *forward =
      find_symbol(object, "object_goto");
  const ctool_elf32_symbol_t *backward =
      find_symbol(object, "object_backward");
  const ctool_elf32_symbol_t *terminal_if =
      find_symbol(object, "object_terminal_if");
  const ctool_elf32_symbol_t *terminal_while =
      find_symbol(object, "object_terminal_while");
  const ctool_elf32_symbol_t *declaration =
      find_symbol(object, "object_label_declaration");
  if (text == NULL || rel_text != NULL || forward == NULL ||
      backward == NULL || terminal_if == NULL || terminal_while == NULL ||
      declaration == NULL ||
      text->contents.size !=
          (ctool_u32)(sizeof(forward_bytes) + sizeof(backward_bytes) +
                      sizeof(terminal_if_bytes) +
                      sizeof(terminal_while_bytes) +
                      sizeof(declaration_bytes)) ||
      text->relocation_count != 0u || object->symbol_count != 6u ||
      object->relocation_count != 0u ||
      !symbol_matches(forward, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(forward_bytes)) ||
      !symbol_matches(backward, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)sizeof(forward_bytes),
                      (ctool_u32)sizeof(backward_bytes)) ||
      !symbol_matches(terminal_if, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(forward_bytes) +
                                  sizeof(backward_bytes)),
                      (ctool_u32)sizeof(terminal_if_bytes)) ||
      !symbol_matches(terminal_while, 4u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(forward_bytes) +
                                  sizeof(backward_bytes) +
                                  sizeof(terminal_if_bytes)),
                      (ctool_u32)sizeof(terminal_while_bytes)) ||
      !symbol_matches(declaration, 5u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(forward_bytes) +
                                  sizeof(backward_bytes) +
                                  sizeof(terminal_if_bytes) +
                                  sizeof(terminal_while_bytes)),
                      (ctool_u32)sizeof(declaration_bytes)) ||
      !decode_function(
          job, text, forward, forward_instructions,
          (ctool_u32)(sizeof(forward_instructions) /
                      sizeof(forward_instructions[0])),
          forward_bytes, (ctool_u32)sizeof(forward_bytes), forward_targets,
          (ctool_u32)(sizeof(forward_targets) / sizeof(forward_targets[0])),
          "object_goto") ||
      !decode_function(
          job, text, backward, backward_instructions,
          (ctool_u32)(sizeof(backward_instructions) /
                      sizeof(backward_instructions[0])),
          backward_bytes, (ctool_u32)sizeof(backward_bytes),
          backward_targets,
          (ctool_u32)(sizeof(backward_targets) /
                      sizeof(backward_targets[0])),
          "object_backward") ||
      !decode_function(
          job, text, terminal_if, terminal_instructions,
          (ctool_u32)(sizeof(terminal_instructions) /
                      sizeof(terminal_instructions[0])),
          terminal_if_bytes, (ctool_u32)sizeof(terminal_if_bytes),
          terminal_targets,
          (ctool_u32)(sizeof(terminal_targets) /
                      sizeof(terminal_targets[0])),
          "object_terminal_if") ||
      !decode_function(
          job, text, terminal_while, terminal_instructions,
          (ctool_u32)(sizeof(terminal_instructions) /
                      sizeof(terminal_instructions[0])),
          terminal_while_bytes, (ctool_u32)sizeof(terminal_while_bytes),
          terminal_targets,
          (ctool_u32)(sizeof(terminal_targets) /
                      sizeof(terminal_targets[0])),
          "object_terminal_while") ||
      !decode_function(
          job, text, declaration, declaration_instructions,
          (ctool_u32)(sizeof(declaration_instructions) /
                      sizeof(declaration_instructions[0])),
          declaration_bytes, (ctool_u32)sizeof(declaration_bytes),
          declaration_targets,
          (ctool_u32)(sizeof(declaration_targets) /
                      sizeof(declaration_targets[0])),
          "object_label_declaration")) {
    (void)fprintf(stderr, "direct goto object differs\n");
    return 0;
  }
  return 1;
}

static int validate_switch_object(ctool_job_t *job,
                                  const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_text[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x50u,
      0x50u, 0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0x59u, 0x58u,
      0x39u, 0xc8u, 0x0fu, 0x94u, 0xc0u, 0x0fu, 0xb6u, 0xc0u,
      0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u, 0x06u, 0x00u,
      0x00u, 0x00u, 0x58u, 0xe9u, 0xb0u, 0x00u, 0x00u, 0x00u,
      0x58u, 0x50u, 0x50u, 0x68u, 0x02u, 0x00u, 0x00u, 0x00u,
      0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x94u, 0xc0u, 0x0fu,
      0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u,
      0x06u, 0x00u, 0x00u, 0x00u, 0x58u, 0xe9u, 0x96u, 0x00u,
      0x00u, 0x00u, 0x58u, 0x50u, 0x50u, 0x68u, 0x03u, 0x00u,
      0x00u, 0x00u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x94u,
      0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u, 0xc0u,
      0x0fu, 0x84u, 0x06u, 0x00u, 0x00u, 0x00u, 0x58u, 0xe9u,
      0x7cu, 0x00u, 0x00u, 0x00u, 0x58u, 0x50u, 0x50u, 0x68u,
      0x04u, 0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0x39u, 0xc8u,
      0x0fu, 0x94u, 0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u,
      0x85u, 0xc0u, 0x0fu, 0x84u, 0x06u, 0x00u, 0x00u, 0x00u,
      0x58u, 0xe9u, 0x62u, 0x00u, 0x00u, 0x00u, 0x58u, 0x50u,
      0x50u, 0x68u, 0x05u, 0x00u, 0x00u, 0x00u, 0x59u, 0x58u,
      0x39u, 0xc8u, 0x0fu, 0x94u, 0xc0u, 0x0fu, 0xb6u, 0xc0u,
      0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u, 0x06u, 0x00u,
      0x00u, 0x00u, 0x58u, 0xe9u, 0x48u, 0x00u, 0x00u, 0x00u,
      0x58u, 0x50u, 0x50u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x94u, 0xc0u, 0x0fu,
      0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u,
      0x06u, 0x00u, 0x00u, 0x00u, 0x58u, 0xe9u, 0x2eu, 0x00u,
      0x00u, 0x00u, 0x58u, 0xe9u, 0x28u, 0x00u, 0x00u, 0x00u,
      0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u,
      0x68u, 0x02u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u,
      0x68u, 0x03u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u,
      0x68u, 0x04u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u,
      0x68u, 0x05u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u,
      0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "cfront_public_storage");
  ctool_u32 cursor = 0u;
  ctool_u32 comparison_count = 0u;
  ctool_u32 conditional_branch_count = 0u;
  ctool_u32 jump_count = 0u;
  ctool_u32 return_count = 0u;
  if (text == NULL || rel_text != NULL || function == NULL ||
      object->symbol_count != 2u || object->relocation_count != 0u ||
      text->relocation_count != 0u || function->value != 0u ||
      function->size != (ctool_u32)sizeof(expected_text) ||
      function->size != text->contents.size ||
      memcmp(text->contents.data, expected_text, sizeof(expected_text)) != 0 ||
      !symbol_matches(function, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      function->size)) {
    (void)fprintf(stderr, "switch object inventory differs\n");
    return 0;
  }
  while (cursor < function->size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining =
        ctool_bytes(text->contents.data + function->value + cursor,
                    function->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "switch object decode failed at %u\n", cursor);
      return 0;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_CMP) {
      comparison_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_JE ||
               decoded.instruction.mnemonic == CTOOL_X86_MN_JMP) {
      int64_t target;
      int32_t displacement;
      if (decoded.instruction.operand_count != 1u ||
          decoded.instruction.operands[0].kind !=
              CTOOL_X86_OPERAND_RELATIVE ||
          decoded.instruction.operands[0].as.value.kind !=
              CTOOL_X86_VALUE_CONSTANT) {
        (void)fprintf(stderr, "switch object branch shape differs\n");
        return 0;
      }
      displacement =
          (int32_t)decoded.instruction.operands[0].as.value.bits;
      target = (int64_t)cursor + (int64_t)decoded.consumed +
               (int64_t)displacement;
      if (target < 0 || target >= (int64_t)function->size) {
        (void)fprintf(stderr, "switch object branch target differs\n");
        return 0;
      }
      if (decoded.instruction.mnemonic == CTOOL_X86_MN_JE) {
        conditional_branch_count++;
      } else {
        jump_count++;
      }
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      return_count++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != function->size || comparison_count != 6u ||
      conditional_branch_count != 6u || jump_count != 7u ||
      return_count != 6u) {
    (void)fprintf(stderr,
                  "switch object operations differ: cmp=%u je=%u jmp=%u "
                  "ret=%u size=%u\n",
                  comparison_count, conditional_branch_count, jump_count,
                  return_count, function->size);
    return 0;
  }
  return 1;
}

static int validate_integer_mutation_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_text[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x50u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0x59u, 0x58u,
      0x01u, 0xc8u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x51u,
      0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x50u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x01u, 0x00u, 0x00u,
      0x00u, 0x59u, 0x58u, 0x29u, 0xc8u, 0x50u, 0x59u, 0x58u,
      0x89u, 0x08u, 0x51u, 0x68u, 0x01u, 0x00u, 0x00u, 0x00u,
      0x59u, 0x58u, 0x01u, 0xc8u, 0x50u, 0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x50u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x68u, 0x02u, 0x00u, 0x00u, 0x00u, 0x59u, 0x58u,
      0x0fu, 0xafu, 0xc1u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u,
      0x51u, 0x58u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x50u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0xd3u,
      0xe8u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x51u, 0x58u,
      0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u,
      0xe5u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0x50u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x01u, 0x00u,
      0x00u, 0x00u, 0x59u, 0x58u, 0x01u, 0xc8u, 0x50u, 0x59u,
      0x58u, 0x89u, 0x08u, 0x51u, 0x68u, 0x01u, 0x00u, 0x00u,
      0x00u, 0x59u, 0x58u, 0x29u, 0xc8u, 0x50u, 0x58u, 0xc9u,
      0xc3u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *state =
      find_symbol(object, "mutation_state");
  const ctool_elf32_symbol_t *prefix =
      find_symbol(object, "prefix_update");
  const ctool_elf32_symbol_t *postfix =
      find_symbol(object, "postfix_update");
  const ctool_elf32_symbol_t *compound =
      find_symbol(object, "compound_update");
  const ctool_elf32_symbol_t *file = find_symbol(object, "file_update");
  ctool_u32 cursor = 0u;
  ctool_u32 add_count = 0u;
  ctool_u32 subtract_count = 0u;
  ctool_u32 multiply_count = 0u;
  ctool_u32 shift_count = 0u;
  ctool_u32 return_count = 0u;
  if (text == NULL || bss == NULL || rel_text == NULL || state == NULL ||
      prefix == NULL || postfix == NULL || compound == NULL || file == NULL ||
      text->contents.data == NULL ||
      text->contents.size != (ctool_u32)sizeof(expected_text) ||
      memcmp(text->contents.data, expected_text, sizeof(expected_text)) != 0 ||
      text->relocation_first != 0u || text->relocation_count != 1u ||
      bss->type != CTOOL_ELF32_SHT_NOBITS || bss->alignment != 4u ||
      bss->size != 4u || bss->contents.size != 0u ||
      object->symbol_count != 6u || object->relocation_count != 1u ||
      object->relocations == NULL ||
      !symbol_matches(state, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u, 4u) ||
      !symbol_matches(prefix, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u, 35u) ||
      !symbol_matches(postfix, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 35u, 45u) ||
      !symbol_matches(compound, 4u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 80u, 78u) ||
      !symbol_matches(file, 5u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 158u, 43u) ||
      object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].offset != 162u ||
      object->relocations[0].symbol_file_index != state->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != 0) {
    (void)fprintf(stderr, "integer mutation object inventory differs\n");
    return 0;
  }
  while (cursor < text->contents.size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + cursor, text->contents.size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr,
                    "integer mutation object decode failed at %u\n", cursor);
      return 0;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_ADD) {
      add_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_SUB) {
      subtract_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_IMUL) {
      multiply_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_SHR) {
      shift_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      return_count++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != text->contents.size || add_count != 3u ||
      subtract_count != 2u || multiply_count != 1u || shift_count != 1u ||
      return_count != 4u) {
    (void)fprintf(stderr, "integer mutation object operations differ\n");
    return 0;
  }
  return 1;
}

static int validate_pointer_value_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_text[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u,
      0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0x50u, 0x58u,
      0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u,
      0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x8du, 0x85u, 0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x51u,
      0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u,
      0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u,
      0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u,
      0x89u, 0xe5u, 0x83u, 0xecu, 0x04u, 0x8du, 0x45u, 0xfcu,
      0x50u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u,
      0x8du, 0x45u, 0xfcu, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x68u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u,
      0x89u, 0x08u, 0x51u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u,
      0xe5u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0x68u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0x89u, 0x08u, 0x51u,
      0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x68u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x83u, 0xecu,
      0x04u, 0x8bu, 0x4cu, 0x24u, 0x04u, 0x89u, 0x0cu, 0x24u,
      0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0x83u, 0xc4u, 0x08u,
      0x50u, 0x58u, 0xc9u, 0xc3u};
  static const char *const function_names[] = {
      "read_indirect",        "address_member",     "write_indirect",
      "pass_pointer",         "qualify_pointer",    "erase_pointer",
      "restore_pointer",      "copy_pointer",       "set_global_pointer",
      "clear_global_pointer", "read_global_member", "call_pointer_result"};
  static const ctool_u32 function_offsets[] = {
      0u, 21u, 34u, 67u, 84u, 101u, 118u, 135u, 171u, 198u, 219u, 240u};
  static const ctool_u32 function_sizes[] = {
      21u, 13u, 33u, 17u, 17u, 17u, 17u, 36u, 27u, 21u, 21u, 36u};
  static const ctool_u32 relocation_offsets[] = {
      25u, 175u, 202u, 223u, 265u};
  static const char *const relocation_symbols[] = {
      "global_value", "global_pointer", "global_pointer",
      "global_pointer", "pass_pointer"};
  static const ctool_u32 relocation_types[] = {
      CTOOL_ELF32_R_386_32, CTOOL_ELF32_R_386_32,
      CTOOL_ELF32_R_386_32, CTOOL_ELF32_R_386_32,
      CTOOL_ELF32_R_386_PC32};
  static const ctool_i32 relocation_addends[] = {0, 0, 0, 0, -4};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *global_value =
      find_symbol(object, "global_value");
  const ctool_elf32_symbol_t *global_pointer =
      find_symbol(object, "global_pointer");
  ctool_u32 return_count = 0u;
  ctool_u32 call_count = 0u;
  ctool_u32 cursor = 0u;
  ctool_u32 index;
  if (text == NULL || bss == NULL || rel_text == NULL ||
      global_value == NULL || global_pointer == NULL ||
      text->contents.data == NULL ||
      text->contents.size != (ctool_u32)sizeof(expected_text) ||
      memcmp(text->contents.data, expected_text, sizeof(expected_text)) != 0 ||
      text->relocation_count != 5u || object->relocation_count != 5u ||
      object->relocations == NULL || object->symbol_count != 15u ||
      bss->type != CTOOL_ELF32_SHT_NOBITS || bss->alignment != 4u ||
      bss->size != 8u || bss->contents.size != 0u ||
      !symbol_matches(global_value, global_value->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u, 4u) ||
      !symbol_matches(global_pointer, global_pointer->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 4u, 4u)) {
    (void)fprintf(stderr, "pointer value object inventory differs\n");
    return 0;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(function_names) / sizeof(function_names[0]));
       index++) {
    const ctool_elf32_symbol_t *function =
        find_symbol(object, function_names[index]);
    if (function == NULL || function->binding != CTOOL_ELF32_BIND_GLOBAL ||
        function->type != CTOOL_ELF32_SYMBOL_FUNCTION ||
        function->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        function->section_file_index != text->file_index ||
        function->value != function_offsets[index] ||
        function->size != function_sizes[index] ||
        function->value > text->contents.size ||
        function->size > text->contents.size - function->value) {
      (void)fprintf(stderr, "pointer function %s differs\n",
                    function_names[index]);
      return 0;
    }
  }
  for (index = 0u; index < object->relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation = &object->relocations[index];
    const ctool_elf32_symbol_t *target =
        find_symbol(object, relocation_symbols[index]);
    if (relocation->relocation_section_file_index != rel_text->file_index ||
        relocation->entry_index != index ||
        relocation->target_section_file_index != text->file_index ||
        relocation->offset != relocation_offsets[index] || target == NULL ||
        relocation->symbol_file_index != target->file_index ||
        relocation->type != (ctool_u32)relocation_types[index] ||
        relocation->addend_known != CTOOL_TRUE ||
        relocation->addend != relocation_addends[index]) {
      (void)fprintf(stderr, "pointer relocation %u differs\n",
                    (unsigned)index);
      return 0;
    }
  }
  while (cursor < text->contents.size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining =
        ctool_bytes(text->contents.data + cursor,
                    text->contents.size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                             &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "pointer object decode failed at %u\n",
                    (unsigned)cursor);
      return 0;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      return_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_CALL) {
      call_count++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != text->contents.size || return_count != 12u ||
      call_count != 1u) {
    (void)fprintf(stderr, "pointer object operations differ (%u/%u)\n",
                  (unsigned)return_count, (unsigned)call_count);
    return 0;
  }
  return 1;
}

static int validate_pointer_comparison_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_text[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu,
      0x95u, 0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x16u, 0x00u, 0x00u, 0x00u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x58u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0xe9u, 0x05u, 0x00u, 0x00u, 0x00u, 0x68u, 0x00u, 0x00u,
      0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u,
      0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x8du, 0x85u, 0x0cu, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u,
      0x39u, 0xc8u, 0x0fu, 0x94u, 0xc0u, 0x0fu, 0xb6u, 0xc0u,
      0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x8du, 0x85u, 0x0cu, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0x39u,
      0xc8u, 0x0fu, 0x92u, 0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u,
      0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u,
      0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const char *const function_names[] = {
      "ctool_job_arena",       "object_pointer_equal",
      "object_pointer_less",  "object_pointer_bits",
      "object_pointer_erase", "object_pointer_restore"};
  static const ctool_u32 function_offsets[] = {
      0u, 69u, 108u, 147u, 164u, 181u};
  static const ctool_u32 function_sizes[] = {69u, 39u, 39u, 17u, 17u, 17u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  ctool_u32 compare_count = 0u;
  ctool_u32 equal_count = 0u;
  ctool_u32 not_equal_count = 0u;
  ctool_u32 below_count = 0u;
  ctool_u32 return_count = 0u;
  ctool_u32 cursor = 0u;
  ctool_u32 index;
  if (text == NULL || text->contents.data == NULL ||
      text->contents.size != (ctool_u32)sizeof(expected_text) ||
      memcmp(text->contents.data, expected_text, sizeof(expected_text)) != 0 ||
      text->relocation_count != 0u ||
      object->relocation_count != 0u || object->symbol_count != 7u) {
    (void)fprintf(stderr, "pointer comparison object inventory differs\n");
    return 0;
  }
  for (index = 0u;
       index <
           (ctool_u32)(sizeof(function_names) / sizeof(function_names[0]));
       index++) {
    const ctool_elf32_symbol_t *function =
        find_symbol(object, function_names[index]);
    if (function == NULL || function->binding != CTOOL_ELF32_BIND_GLOBAL ||
        function->type != CTOOL_ELF32_SYMBOL_FUNCTION ||
        function->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        function->section_file_index != text->file_index ||
        function->value != function_offsets[index] ||
        function->size != function_sizes[index] ||
        function->value > text->contents.size ||
        function->size > text->contents.size - function->value) {
      (void)fprintf(stderr, "pointer comparison function %s differs\n",
                    function_names[index]);
      return 0;
    }
  }
  while (cursor < text->contents.size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining =
        ctool_bytes(text->contents.data + cursor,
                    text->contents.size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "pointer comparison decode failed at %u\n",
                    (unsigned int)cursor);
      return 0;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_CMP) {
      compare_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_SETE) {
      equal_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_SETNE) {
      not_equal_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_SETB) {
      below_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      return_count++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != text->contents.size || compare_count != 3u ||
      equal_count != 1u || not_equal_count != 1u || below_count != 1u ||
      return_count != 6u) {
    (void)fprintf(stderr,
                  "pointer comparison operations differ (%u/%u/%u/%u/%u)\n",
                  (unsigned int)compare_count, (unsigned int)equal_count,
                  (unsigned int)not_equal_count, (unsigned int)below_count,
                  (unsigned int)return_count);
    return 0;
  }
  return 1;
}

static int validate_pointer_condition_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_text[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x94u, 0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u,
      0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u, 0x1eu, 0x00u,
      0x00u, 0x00u, 0x8du, 0x85u, 0x0cu, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u, 0xc0u,
      0x0fu, 0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u, 0x01u,
      0x00u, 0x00u, 0x00u, 0xe9u, 0x05u, 0x00u, 0x00u, 0x00u,
      0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u,
      0x01u, 0x00u, 0x00u, 0x00u, 0xe9u, 0x23u, 0x00u, 0x00u,
      0x00u, 0x8du, 0x85u, 0x0cu, 0x00u, 0x00u, 0x00u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u, 0x01u, 0x00u,
      0x00u, 0x00u, 0xe9u, 0x05u, 0x00u, 0x00u, 0x00u, 0x68u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u, 0x55u,
      0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u, 0xc0u,
      0x0fu, 0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u, 0x01u,
      0x00u, 0x00u, 0x00u, 0xe9u, 0x05u, 0x00u, 0x00u, 0x00u,
      0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x08u, 0x00u, 0x00u, 0x00u, 0x68u,
      0x01u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u, 0x68u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u, 0x55u,
      0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u, 0xc0u,
      0x0fu, 0x84u, 0x05u, 0x00u, 0x00u, 0x00u, 0xe9u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u, 0x05u, 0x00u,
      0x00u, 0x00u, 0xe9u, 0x19u, 0x00u, 0x00u, 0x00u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u, 0x05u,
      0x00u, 0x00u, 0x00u, 0xe9u, 0xceu, 0xffu, 0xffu, 0xffu,
      0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x05u, 0x00u, 0x00u, 0x00u, 0xe9u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x68u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x58u, 0xc9u, 0xc3u};
  static const char *const function_names[] = {
      "pointer_not", "pointer_and", "pointer_or", "pointer_select",
      "pointer_if",  "pointer_while", "pointer_do", "pointer_for"};
  static const ctool_u32 function_offsets[] = {
      0u, 27u, 88u, 159u, 200u, 239u, 275u, 336u};
  static const ctool_u32 function_sizes[] = {
      27u, 61u, 71u, 41u, 39u, 36u, 61u, 36u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  ctool_u32 test_count = 0u;
  ctool_u32 set_equal_count = 0u;
  ctool_u32 return_count = 0u;
  ctool_u32 cursor = 0u;
  ctool_u32 index;
  if (text == NULL || text->contents.data == NULL ||
      text->contents.size != (ctool_u32)sizeof(expected_text) ||
      memcmp(text->contents.data, expected_text, sizeof(expected_text)) != 0 ||
      text->relocation_count != 0u ||
      object->relocation_count != 0u || object->symbol_count != 9u) {
    (void)fprintf(stderr, "pointer condition object inventory differs\n");
    return 0;
  }
  for (index = 0u;
       index <
           (ctool_u32)(sizeof(function_names) / sizeof(function_names[0]));
       index++) {
    const ctool_elf32_symbol_t *function =
        find_symbol(object, function_names[index]);
    if (function == NULL || function->binding != CTOOL_ELF32_BIND_GLOBAL ||
        function->type != CTOOL_ELF32_SYMBOL_FUNCTION ||
        function->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        function->section_file_index != text->file_index ||
        function->value != function_offsets[index] ||
        function->size != function_sizes[index] ||
        function->value > text->contents.size ||
        function->size > text->contents.size - function->value) {
      (void)fprintf(stderr, "pointer condition function %s differs\n",
                    function_names[index]);
      return 0;
    }
  }
  while (cursor < text->contents.size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining =
        ctool_bytes(text->contents.data + cursor,
                    text->contents.size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "pointer condition decode failed at %u\n",
                    (unsigned int)cursor);
      return 0;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_TEST) {
      test_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_SETE) {
      set_equal_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      return_count++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != text->contents.size || test_count != 11u ||
      set_equal_count != 1u || return_count != 9u) {
    (void)fprintf(stderr, "pointer condition operations differ (%u/%u/%u)\n",
                  (unsigned int)test_count,
                  (unsigned int)set_equal_count,
                  (unsigned int)return_count);
    return 0;
  }
  return 1;
}

static int validate_pointer_arithmetic_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_text[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0xbau, 0x04u, 0x00u, 0x00u, 0x00u,
      0x0fu, 0xafu, 0xcau, 0x01u, 0xc8u, 0x50u, 0x58u, 0xc9u,
      0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du,
      0x85u, 0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x59u, 0x58u, 0xbau, 0x04u, 0x00u, 0x00u,
      0x00u, 0x0fu, 0xafu, 0xc2u, 0x01u, 0xc8u, 0x50u, 0x58u,
      0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u,
      0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x8du, 0x85u, 0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0xbau, 0x04u, 0x00u,
      0x00u, 0x00u, 0x0fu, 0xafu, 0xcau, 0x29u, 0xc8u, 0x50u,
      0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x8du, 0x85u, 0x0cu, 0x00u, 0x00u, 0x00u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0x29u, 0xc8u,
      0xb9u, 0x04u, 0x00u, 0x00u, 0x00u, 0x99u, 0xf7u, 0xf9u,
      0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x8du, 0x85u, 0x0cu, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0xbau,
      0x04u, 0x00u, 0x00u, 0x00u, 0x0fu, 0xafu, 0xcau, 0x01u,
      0xc8u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u,
      0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x0cu, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x59u, 0x58u, 0xbau, 0x04u, 0x00u, 0x00u,
      0x00u, 0x0fu, 0xafu, 0xc2u, 0x01u, 0xc8u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u,
      0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u, 0x0cu, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u,
      0x58u, 0x01u, 0xc8u, 0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u,
      0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u, 0x0cu,
      0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x59u, 0x58u, 0xbau, 0x0cu, 0x00u, 0x00u, 0x00u, 0x0fu,
      0xafu, 0xcau, 0x01u, 0xc8u, 0x50u, 0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x29u, 0xc8u, 0xb9u, 0x0cu, 0x00u,
      0x00u, 0x00u, 0x99u, 0xf7u, 0xf9u, 0x50u, 0x58u, 0xc9u,
      0xc3u, 0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x68u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x8du, 0x85u, 0x08u, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u,
      0x58u, 0xbau, 0x04u, 0x00u, 0x00u, 0x00u, 0x0fu, 0xafu,
      0xcau, 0x01u, 0xc8u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x50u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x01u, 0x00u, 0x00u,
      0x00u, 0x59u, 0x58u, 0xbau, 0x04u, 0x00u, 0x00u, 0x00u,
      0x0fu, 0xafu, 0xcau, 0x01u, 0xc8u, 0x50u, 0x59u, 0x58u,
      0x89u, 0x08u, 0x51u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u,
      0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u,
      0x58u, 0x50u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u,
      0x01u, 0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0xbau, 0x04u,
      0x00u, 0x00u, 0x00u, 0x0fu, 0xafu, 0xcau, 0x29u, 0xc8u,
      0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x51u, 0x68u, 0x01u,
      0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0xbau, 0x04u, 0x00u,
      0x00u, 0x00u, 0x0fu, 0xafu, 0xcau, 0x01u, 0xc8u, 0x50u,
      0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x50u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u, 0x0cu, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u,
      0x58u, 0xbau, 0x04u, 0x00u, 0x00u, 0x00u, 0x0fu, 0xafu,
      0xcau, 0x01u, 0xc8u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u,
      0x51u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x50u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u, 0x0cu,
      0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x59u, 0x58u, 0xbau, 0x04u, 0x00u, 0x00u, 0x00u, 0x0fu,
      0xafu, 0xcau, 0x29u, 0xc8u, 0x50u, 0x59u, 0x58u, 0x89u,
      0x08u, 0x51u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u,
      0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u,
      0x50u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x00u,
      0x01u, 0x00u, 0x00u, 0x59u, 0x58u, 0xbau, 0x02u, 0x00u,
      0x00u, 0x00u, 0x0fu, 0xafu, 0xcau, 0x01u, 0xc8u, 0x50u,
      0x59u, 0x58u, 0x89u, 0x08u, 0x51u, 0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x50u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x68u, 0x00u, 0x01u, 0x00u, 0x00u, 0x59u, 0x58u,
      0xbau, 0x02u, 0x00u, 0x00u, 0x00u, 0x0fu, 0xafu, 0xcau,
      0x01u, 0xc8u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x51u,
      0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x50u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x01u, 0x00u, 0x00u,
      0x00u, 0x59u, 0x58u, 0xbau, 0x04u, 0x00u, 0x00u, 0x00u,
      0x0fu, 0xafu, 0xcau, 0x01u, 0xc8u, 0x50u, 0x59u, 0x58u,
      0x89u, 0x08u, 0x51u, 0x68u, 0x01u, 0x00u, 0x00u, 0x00u,
      0x59u, 0x58u, 0xbau, 0x04u, 0x00u, 0x00u, 0x00u, 0x0fu,
      0xafu, 0xcau, 0x29u, 0xc8u, 0x50u, 0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x50u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0x59u, 0x58u,
      0xbau, 0x04u, 0x00u, 0x00u, 0x00u, 0x0fu, 0xafu, 0xcau,
      0x29u, 0xc8u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x51u,
      0x58u, 0xc9u, 0xc3u};
  static const char *const function_names[] = {
      "advance",       "reverse_add",       "retreat",
      "distance",      "read_index",        "read_reverse",
      "advance_byte",  "advance_triple",    "triple_distance",
      "global_start",  "read_global_index", "prefix_advance",
      "postfix_retreat", "assign_advance",  "assign_retreat",
      "advance_read_sector", "advance_write_sector", "postfix_advance",
      "prefix_retreat"};
  static const ctool_u32 function_offsets[] = {
      0u, 41u, 82u, 123u, 164u, 209u, 254u, 287u, 328u, 369u, 380u,
      419u, 462u, 523u, 572u, 621u, 664u, 707u, 768u};
  static const ctool_u32 function_sizes[] = {
      41u, 41u, 41u, 41u, 45u, 45u, 33u, 41u, 41u, 11u, 39u,
      43u, 61u, 49u, 49u, 43u, 43u, 61u, 43u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *global_values =
      find_symbol(object, "global_values");
  ctool_u32 add_count = 0u;
  ctool_u32 subtract_count = 0u;
  ctool_u32 multiply_count = 0u;
  ctool_u32 divide_count = 0u;
  ctool_u32 return_count = 0u;
  ctool_u32 cursor = 0u;
  ctool_u32 index;
  if (text == NULL || bss == NULL || rel_text == NULL ||
      global_values == NULL || text->contents.data == NULL ||
      text->contents.size != (ctool_u32)sizeof(expected_text) ||
      memcmp(text->contents.data, expected_text, sizeof(expected_text)) != 0 ||
      text->relocation_first != 0u || text->relocation_count != 2u ||
      object->relocation_count != 2u || object->relocations == NULL ||
      object->symbol_count != 21u || bss->type != CTOOL_ELF32_SHT_NOBITS ||
      bss->alignment != 4u || bss->size != 16u ||
      bss->contents.size != 0u ||
      !symbol_matches(global_values, global_values->file_index,
                      CTOOL_ELF32_BIND_LOCAL, CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u, 16u)) {
    (void)fprintf(stderr, "pointer arithmetic object inventory differs\n");
    return 0;
  }
  for (index = 0u;
       index <
           (ctool_u32)(sizeof(function_names) / sizeof(function_names[0]));
       index++) {
    const ctool_elf32_symbol_t *function =
        find_symbol(object, function_names[index]);
    if (function == NULL || function->binding != CTOOL_ELF32_BIND_GLOBAL ||
        function->type != CTOOL_ELF32_SYMBOL_FUNCTION ||
        function->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        function->section_file_index != text->file_index ||
        function->value != function_offsets[index] ||
        function->size != function_sizes[index] ||
        function->value > text->contents.size ||
        function->size > text->contents.size - function->value) {
      (void)fprintf(stderr, "pointer arithmetic function %s differs\n",
                    function_names[index]);
      return 0;
    }
  }
  for (index = 0u; index < object->relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation = &object->relocations[index];
    static const ctool_u32 relocation_offsets[] = {373u, 384u};
    if (relocation->relocation_section_file_index != rel_text->file_index ||
        relocation->entry_index != index ||
        relocation->target_section_file_index != text->file_index ||
        relocation->offset != relocation_offsets[index] ||
        relocation->symbol_file_index != global_values->file_index ||
        relocation->type != CTOOL_ELF32_R_386_32 ||
        relocation->addend_known != CTOOL_TRUE ||
        relocation->addend != 0) {
      (void)fprintf(stderr, "pointer arithmetic relocation %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  while (cursor < text->contents.size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining =
        ctool_bytes(text->contents.data + cursor,
                    text->contents.size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "pointer arithmetic decode failed at %u\n",
                    (unsigned int)cursor);
      return 0;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_ADD) {
      add_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_SUB) {
      subtract_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_IMUL) {
      multiply_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_IDIV) {
      divide_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      return_count++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != text->contents.size || add_count != 13u ||
      subtract_count != 7u || multiply_count != 17u || divide_count != 2u ||
      return_count != 19u) {
    (void)fprintf(stderr,
                  "pointer arithmetic operations differ (%u/%u/%u/%u/%u)\n",
                  (unsigned int)add_count, (unsigned int)subtract_count,
                  (unsigned int)multiply_count, (unsigned int)divide_count,
                  (unsigned int)return_count);
    return 0;
  }
  return 1;
}

static int validate_external_object_load(
    const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_text[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text =
      find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "read_external_clock");
  const ctool_elf32_symbol_t *external =
      find_symbol(object, "external_clock");
  if (text == NULL || rel_text == NULL || function == NULL ||
      external == NULL ||
      text->contents.size != (ctool_u32)sizeof(expected_text) ||
      text->contents.data == NULL ||
      memcmp(text->contents.data, expected_text,
             sizeof(expected_text)) != 0 ||
      text->relocation_first != 0u || text->relocation_count != 1u ||
      object->relocation_count != 1u || object->relocations == NULL ||
      !symbol_matches(function, function->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(expected_text)) ||
      !symbol_matches(external, external->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_UNDEFINED,
                      CTOOL_ELF32_NO_SECTION, 0u, 0u) ||
      object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].offset != 4u ||
      object->relocations[0].symbol_file_index != external->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != 0) {
    (void)fprintf(stderr, "isolated external object load differs\n");
    return 0;
  }
  return 1;
}

static int validate_file_assignment_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u,
      0x89u, 0x08u, 0x51u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_text =
      find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "vga_set_vsync_wait");
  const ctool_elf32_symbol_t *state =
      find_symbol(object, "vga_wait_vsync");
  if (text == NULL || bss == NULL || rel_text == NULL || function == NULL ||
      state == NULL ||
      text->contents.size != (ctool_u32)sizeof(function_bytes) ||
      text->relocation_first != 0u || text->relocation_count != 1u ||
      bss->type != CTOOL_ELF32_SHT_NOBITS || bss->alignment != 4u ||
      bss->size != 4u || bss->contents.size != 0u ||
      object->symbol_count != 3u || object->relocation_count != 1u ||
      object->relocations == NULL ||
      !symbol_matches(state, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u, 4u) ||
      !symbol_matches(function, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(function_bytes)) ||
      object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].offset != 4u ||
      object->relocations[0].symbol_file_index != state->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != 0 ||
      !decode_function(
          job, text, function, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), NULL, 0u,
          "vga_set_vsync_wait")) {
    (void)fprintf(stderr, "file assignment object differs\n");
    return 0;
  }
  return 1;
}

static int validate_file_member_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x58u, 0x83u, 0xc0u, 0x08u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_ADD, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_text =
      find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "timer_get_frequency");
  const ctool_elf32_symbol_t *state = find_symbol(object, "timer_state");
  if (text == NULL || bss == NULL || rel_text == NULL || function == NULL ||
      state == NULL ||
      text->contents.size != (ctool_u32)sizeof(function_bytes) ||
      text->relocation_first != 0u || text->relocation_count != 1u ||
      bss->type != CTOOL_ELF32_SHT_NOBITS || bss->alignment != 4u ||
      bss->size != 20u || bss->contents.size != 0u ||
      object->symbol_count != 3u || object->relocation_count != 1u ||
      object->relocations == NULL ||
      !symbol_matches(state, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u, 20u) ||
      !symbol_matches(function, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(function_bytes)) ||
      object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].offset != 4u ||
      object->relocations[0].symbol_file_index != state->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != 0 ||
      !decode_function(
          job, text, function, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), NULL, 0u,
          "timer_get_frequency")) {
    (void)fprintf(stderr, "file-member object differs\n");
    return 0;
  }
  return 1;
}

static int validate_bit_field_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 unsigned_function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x58u, 0x8bu, 0x00u, 0xc1u, 0xe0u, 0x08u,
      0xc1u, 0xe8u, 0x18u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t unsigned_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_SHL,
      CTOOL_X86_MN_SHR,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u8 signed_function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x58u, 0x83u, 0xc0u, 0x04u, 0x8bu, 0x00u,
      0xc1u, 0xe0u, 0x18u, 0xc1u, 0xf8u, 0x1bu, 0x50u,
      0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t signed_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_ADD,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_SHL,   CTOOL_X86_MN_SAR,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u8 whole_function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x58u, 0x83u, 0xc0u, 0x08u, 0x8bu, 0x00u,
      0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t whole_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_ADD,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_text =
      find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *unsigned_function =
      find_symbol(object, "read_red");
  const ctool_elf32_symbol_t *signed_function =
      find_symbol(object, "read_delta");
  const ctool_elf32_symbol_t *whole_function =
      find_symbol(object, "read_whole");
  const ctool_elf32_symbol_t *unsigned_state =
      find_symbol(object, "color_state");
  const ctool_elf32_symbol_t *signed_state =
      find_symbol(object, "signed_state");
  if (text == NULL || bss == NULL || rel_text == NULL ||
      unsigned_function == NULL || signed_function == NULL ||
      whole_function == NULL || unsigned_state == NULL ||
      signed_state == NULL ||
      text->contents.size != (ctool_u32)(sizeof(unsigned_function_bytes) +
                                         sizeof(signed_function_bytes) +
                                         sizeof(whole_function_bytes)) ||
      text->relocation_first != 0u || text->relocation_count != 3u ||
      bss->type != CTOOL_ELF32_SHT_NOBITS || bss->alignment != 4u ||
      bss->size != 16u || bss->contents.size != 0u ||
      object->symbol_count != 6u || object->relocation_count != 3u ||
      object->relocations == NULL ||
      !symbol_matches(unsigned_state, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u, 4u) ||
      !symbol_matches(signed_state, 2u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 4u, 12u) ||
      !symbol_matches(unsigned_function, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(unsigned_function_bytes)) ||
      !symbol_matches(signed_function, 4u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)sizeof(unsigned_function_bytes),
                      (ctool_u32)sizeof(signed_function_bytes)) ||
      !symbol_matches(whole_function, 5u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(unsigned_function_bytes) +
                                  sizeof(signed_function_bytes)),
                      (ctool_u32)sizeof(whole_function_bytes)) ||
      object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].offset != 4u ||
      object->relocations[0].symbol_file_index != unsigned_state->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != 0 ||
      object->relocations[1].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[1].entry_index != 1u ||
      object->relocations[1].target_section_file_index != text->file_index ||
      object->relocations[1].offset != 25u ||
      object->relocations[1].symbol_file_index != signed_state->file_index ||
      object->relocations[1].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[1].addend_known != CTOOL_TRUE ||
      object->relocations[1].addend != 0 ||
      object->relocations[2].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[2].entry_index != 2u ||
      object->relocations[2].target_section_file_index != text->file_index ||
      object->relocations[2].offset != 49u ||
      object->relocations[2].symbol_file_index != signed_state->file_index ||
      object->relocations[2].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[2].addend_known != CTOOL_TRUE ||
      object->relocations[2].addend != 0 ||
      !decode_function(
          job, text, unsigned_function, unsigned_instructions,
          (ctool_u32)(sizeof(unsigned_instructions) /
                      sizeof(unsigned_instructions[0])),
          unsigned_function_bytes,
          (ctool_u32)sizeof(unsigned_function_bytes), NULL, 0u,
          "read_red") ||
      !decode_function(
          job, text, signed_function, signed_instructions,
          (ctool_u32)(sizeof(signed_instructions) /
                      sizeof(signed_instructions[0])),
          signed_function_bytes, (ctool_u32)sizeof(signed_function_bytes),
          NULL, 0u, "read_delta") ||
      !decode_function(
          job, text, whole_function, whole_instructions,
          (ctool_u32)(sizeof(whole_instructions) /
                      sizeof(whole_instructions[0])),
          whole_function_bytes, (ctool_u32)sizeof(whole_function_bytes),
          NULL, 0u, "read_whole")) {
    (void)fprintf(stderr, "bit-field object differs\n");
    return 0;
  }
  return 1;
}

static int validate_chained_assignment_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u,
      0x51u, 0x59u, 0x58u, 0x89u, 0x08u, 0x51u, 0x58u,
      0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 relocation_offsets[] = {4u, 9u};
  static const char *const relocation_symbols[] = {
      "first_state", "second_state"};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_text =
      find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *first = find_symbol(object, "first_state");
  const ctool_elf32_symbol_t *second = find_symbol(object, "second_state");
  const ctool_elf32_symbol_t *function = find_symbol(object, "set_both");
  ctool_u32 relocation;
  if (text == NULL || bss == NULL || rel_text == NULL || first == NULL ||
      second == NULL || function == NULL ||
      text->contents.size != (ctool_u32)sizeof(function_bytes) ||
      text->relocation_first != 0u || text->relocation_count != 2u ||
      bss->type != CTOOL_ELF32_SHT_NOBITS || bss->alignment != 4u ||
      bss->size != 8u || bss->contents.size != 0u ||
      object->symbol_count != 4u || object->relocation_count != 2u ||
      object->relocations == NULL ||
      !symbol_matches(first, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u, 4u) ||
      !symbol_matches(second, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 4u, 4u) ||
      !symbol_matches(function, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(function_bytes)) ||
      !decode_function(
          job, text, function, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), NULL, 0u,
          "set_both")) {
    (void)fprintf(stderr, "chained assignment object differs\n");
    return 0;
  }
  for (relocation = 0u; relocation < 2u; relocation++) {
    const ctool_elf32_symbol_t *symbol =
        find_symbol(object, relocation_symbols[relocation]);
    if (symbol == NULL ||
        object->relocations[relocation].relocation_section_file_index !=
            rel_text->file_index ||
        object->relocations[relocation].entry_index != relocation ||
        object->relocations[relocation].target_section_file_index !=
            text->file_index ||
        object->relocations[relocation].offset !=
            relocation_offsets[relocation] ||
        object->relocations[relocation].symbol_file_index !=
            symbol->file_index ||
        object->relocations[relocation].type != CTOOL_ELF32_R_386_32 ||
        object->relocations[relocation].addend_known != CTOOL_TRUE ||
        object->relocations[relocation].addend != 0) {
      (void)fprintf(stderr, "chained assignment relocation differs\n");
      return 0;
    }
  }
  return 1;
}

static int validate_paint_multiplication_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u, 0x08u, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x59u, 0x58u, 0x29u, 0xc8u, 0x50u, 0x68u, 0x00u, 0x00u,
      0x00u, 0x00u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u,
      0x0fu, 0xafu, 0xc1u, 0x50u, 0x59u, 0x58u, 0x01u, 0xc8u,
      0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 expected_data[] = {
      0x38u, 0x00u, 0x00u, 0x00u, 0x14u, 0x00u,
      0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,  CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_IMUL, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,  CTOOL_X86_MN_ADD,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_u32 relocation_offsets[] = {
      4u, 24u, 38u, 64u, 84u, 98u};
  static const char *const relocation_symbols[] = {
      "CANVAS_X", "view_x", "zoom_level",
      "CANVAS_Y", "view_y", "zoom_level"};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *data = find_section(object, ".data");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_text =
      find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *canvas_x = find_symbol(object, "CANVAS_X");
  const ctool_elf32_symbol_t *canvas_y = find_symbol(object, "CANVAS_Y");
  const ctool_elf32_symbol_t *zoom = find_symbol(object, "zoom_level");
  const ctool_elf32_symbol_t *view_x = find_symbol(object, "view_x");
  const ctool_elf32_symbol_t *view_y = find_symbol(object, "view_y");
  const ctool_elf32_symbol_t *function_x =
      find_symbol(object, "canvas_to_screen_x");
  const ctool_elf32_symbol_t *function_y =
      find_symbol(object, "canvas_to_screen_y");
  ctool_u32 relocation;
  if (text == NULL || data == NULL || bss == NULL || rel_text == NULL ||
      canvas_x == NULL || canvas_y == NULL || zoom == NULL ||
      view_x == NULL || view_y == NULL || function_x == NULL ||
      function_y == NULL || text->contents.size != 120u ||
      text->relocation_first != 0u || text->relocation_count != 6u ||
      data->contents.size != (ctool_u32)sizeof(expected_data) ||
      data->contents.data == NULL ||
      memcmp(data->contents.data, expected_data, sizeof(expected_data)) != 0 ||
      bss->type != CTOOL_ELF32_SHT_NOBITS || bss->alignment != 4u ||
      bss->size != 8u || bss->contents.size != 0u ||
      object->symbol_count != 8u || object->relocation_count != 6u ||
      object->relocations == NULL ||
      !symbol_matches(canvas_x, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 0u, 4u) ||
      !symbol_matches(canvas_y, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 4u, 4u) ||
      !symbol_matches(zoom, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 8u, 4u) ||
      !symbol_matches(view_x, 4u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u, 4u) ||
      !symbol_matches(view_y, 5u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 4u, 4u) ||
      !symbol_matches(function_x, 6u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u, 60u) ||
      !symbol_matches(function_y, 7u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 60u,
                      60u) ||
      !decode_function(
          job, text, function_x, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), NULL, 0u,
          "canvas_to_screen_x") ||
      !decode_function(
          job, text, function_y, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), NULL, 0u,
          "canvas_to_screen_y")) {
    (void)fprintf(stderr, "Paint multiplication object differs\n");
    return 0;
  }
  for (relocation = 0u; relocation < 6u; relocation++) {
    const ctool_elf32_symbol_t *symbol =
        find_symbol(object, relocation_symbols[relocation]);
    if (symbol == NULL ||
        object->relocations[relocation].relocation_section_file_index !=
            rel_text->file_index ||
        object->relocations[relocation].entry_index != relocation ||
        object->relocations[relocation].target_section_file_index !=
            text->file_index ||
        object->relocations[relocation].offset !=
            relocation_offsets[relocation] ||
        object->relocations[relocation].symbol_file_index !=
            symbol->file_index ||
        object->relocations[relocation].type != CTOOL_ELF32_R_386_32 ||
        object->relocations[relocation].addend_known != CTOOL_TRUE ||
        object->relocations[relocation].addend != 0) {
      (void)fprintf(stderr,
                    "Paint multiplication relocation %lu differs\n",
                    (unsigned long)relocation);
      return 0;
    }
  }
  return 1;
}

static int validate_unsigned_multiplication_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x68u, 0x01u, 0x00u, 0x00u, 0x80u, 0x59u, 0x58u,
      0x0fu, 0xafu, 0xc1u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_IMUL,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text =
      find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "multiply_unsigned");
  if (text == NULL || rel_text != NULL || function == NULL ||
      text->contents.size != (ctool_u32)sizeof(function_bytes) ||
      text->relocation_count != 0u || object->symbol_count != 2u ||
      object->relocation_count != 0u ||
      !symbol_matches(function, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(function_bytes)) ||
      !decode_function(
          job, text, function, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), NULL, 0u,
          "multiply_unsigned")) {
    (void)fprintf(stderr, "unsigned multiplication object differs\n");
    return 0;
  }
  return 1;
}

static int validate_division_object(ctool_job_t *job,
                                    const ctool_elf32_object_t *object) {
  static const ctool_u8 signed_divide_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x99u, 0xf7u, 0xf9u, 0x50u, 0x58u,
      0xc9u, 0xc3u};
  static const ctool_u8 signed_remainder_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x99u, 0xf7u, 0xf9u, 0x52u, 0x58u,
      0xc9u, 0xc3u};
  static const ctool_u8 unsigned_divide_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x31u, 0xd2u, 0xf7u, 0xf1u, 0x50u,
      0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 unsigned_remainder_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x31u, 0xd2u, 0xf7u, 0xf1u, 0x52u,
      0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t signed_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,  CTOOL_X86_MN_CDQ,
      CTOOL_X86_MN_IDIV, CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t unsigned_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,  CTOOL_X86_MN_XOR,
      CTOOL_X86_MN_DIV,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text =
      find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *signed_divide =
      find_symbol(object, "signed_divide");
  const ctool_elf32_symbol_t *signed_remainder =
      find_symbol(object, "signed_remainder");
  const ctool_elf32_symbol_t *unsigned_divide =
      find_symbol(object, "unsigned_divide");
  const ctool_elf32_symbol_t *unsigned_remainder =
      find_symbol(object, "unsigned_remainder");
  ctool_u32 signed_divide_offset = 0u;
  ctool_u32 signed_remainder_offset =
      signed_divide_offset + (ctool_u32)sizeof(signed_divide_bytes);
  ctool_u32 unsigned_divide_offset =
      signed_remainder_offset + (ctool_u32)sizeof(signed_remainder_bytes);
  ctool_u32 unsigned_remainder_offset =
      unsigned_divide_offset + (ctool_u32)sizeof(unsigned_divide_bytes);
  ctool_u32 total_size =
      unsigned_remainder_offset +
      (ctool_u32)sizeof(unsigned_remainder_bytes);
  if (text == NULL || rel_text != NULL || signed_divide == NULL ||
      signed_remainder == NULL || unsigned_divide == NULL ||
      unsigned_remainder == NULL || text->contents.size != total_size ||
      text->relocation_count != 0u || object->symbol_count != 5u ||
      object->relocation_count != 0u ||
      !symbol_matches(signed_divide, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      signed_divide_offset,
                      (ctool_u32)sizeof(signed_divide_bytes)) ||
      !symbol_matches(signed_remainder, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      signed_remainder_offset,
                      (ctool_u32)sizeof(signed_remainder_bytes)) ||
      !symbol_matches(unsigned_divide, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      unsigned_divide_offset,
                      (ctool_u32)sizeof(unsigned_divide_bytes)) ||
      !symbol_matches(unsigned_remainder, 4u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      unsigned_remainder_offset,
                      (ctool_u32)sizeof(unsigned_remainder_bytes)) ||
      !decode_function(
          job, text, signed_divide, signed_instructions,
          (ctool_u32)(sizeof(signed_instructions) /
                      sizeof(signed_instructions[0])),
          signed_divide_bytes, (ctool_u32)sizeof(signed_divide_bytes),
          NULL, 0u, "signed_divide") ||
      !decode_function(
          job, text, signed_remainder, signed_instructions,
          (ctool_u32)(sizeof(signed_instructions) /
                      sizeof(signed_instructions[0])),
          signed_remainder_bytes,
          (ctool_u32)sizeof(signed_remainder_bytes), NULL, 0u,
          "signed_remainder") ||
      !decode_function(
          job, text, unsigned_divide, unsigned_instructions,
          (ctool_u32)(sizeof(unsigned_instructions) /
                      sizeof(unsigned_instructions[0])),
          unsigned_divide_bytes,
          (ctool_u32)sizeof(unsigned_divide_bytes), NULL, 0u,
          "unsigned_divide") ||
      !decode_function(
          job, text, unsigned_remainder, unsigned_instructions,
          (ctool_u32)(sizeof(unsigned_instructions) /
                      sizeof(unsigned_instructions[0])),
          unsigned_remainder_bytes,
          (ctool_u32)sizeof(unsigned_remainder_bytes), NULL, 0u,
          "unsigned_remainder")) {
    (void)fprintf(stderr, "division object differs\n");
    return 0;
  }
  return 1;
}

static int validate_branch_fit_object(ctool_job_t *job,
                                      const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x7fu,
      0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu,
      0x96u, 0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u,
      0x01u, 0x00u, 0x00u, 0x00u, 0xe9u, 0x33u, 0x00u, 0x00u,
      0x00u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x80u, 0xffu, 0xffu,
      0xffu, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x93u, 0xc0u,
      0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u, 0x01u, 0x00u,
      0x00u, 0x00u, 0xe9u, 0x05u, 0x00u, 0x00u, 0x00u, 0x68u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u, 0x01u, 0x00u,
      0x00u, 0x00u, 0xe9u, 0x05u, 0x00u, 0x00u, 0x00u, 0x68u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 signed_less_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x9cu, 0xc0u,
      0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 signed_less_equal_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x9eu, 0xc0u,
      0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 unsigned_less_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x92u, 0xc0u,
      0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u32 branch_targets[] = {
      49u, 100u, 95u, 100u, 119u, 124u};
  static const ctool_x86_mnemonic_t branch_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,   CTOOL_X86_MN_SETBE,
      CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,    CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_CMP,   CTOOL_X86_MN_SETAE, CTOOL_X86_MN_MOVZX,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t signed_less_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,
      CTOOL_X86_MN_SETL,  CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t signed_less_equal_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,
      CTOOL_X86_MN_SETLE, CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t unsigned_less_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,
      CTOOL_X86_MN_SETB,  CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "asm_branch_fits_i8");
  const ctool_elf32_symbol_t *signed_less =
      find_symbol(object, "signed_less");
  const ctool_elf32_symbol_t *signed_less_equal =
      find_symbol(object, "signed_less_equal");
  const ctool_elf32_symbol_t *unsigned_less =
      find_symbol(object, "unsigned_less");
  ctool_u32 signed_less_offset = (ctool_u32)sizeof(function_bytes);
  ctool_u32 signed_less_equal_offset =
      signed_less_offset + (ctool_u32)sizeof(signed_less_bytes);
  ctool_u32 unsigned_less_offset =
      signed_less_equal_offset +
      (ctool_u32)sizeof(signed_less_equal_bytes);
  ctool_u32 total_size =
      unsigned_less_offset + (ctool_u32)sizeof(unsigned_less_bytes);
  if (text == NULL || rel_text != NULL || function == NULL ||
      signed_less == NULL || signed_less_equal == NULL ||
      unsigned_less == NULL || text->contents.size != total_size ||
      text->relocation_count != 0u || object->symbol_count != 5u ||
      object->relocation_count != 0u ||
      !symbol_matches(function, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(function_bytes)) ||
      !symbol_matches(signed_less, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      signed_less_offset,
                      (ctool_u32)sizeof(signed_less_bytes)) ||
      !symbol_matches(signed_less_equal, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      signed_less_equal_offset,
                      (ctool_u32)sizeof(signed_less_equal_bytes)) ||
      !symbol_matches(unsigned_less, 4u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      unsigned_less_offset,
                      (ctool_u32)sizeof(unsigned_less_bytes)) ||
      !decode_function(
          job, text, function, branch_instructions,
          (ctool_u32)(sizeof(branch_instructions) /
                      sizeof(branch_instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), branch_targets,
          (ctool_u32)(sizeof(branch_targets) / sizeof(branch_targets[0])),
          "asm_branch_fits_i8") ||
      !decode_function(
          job, text, signed_less, signed_less_instructions,
          (ctool_u32)(sizeof(signed_less_instructions) /
                      sizeof(signed_less_instructions[0])),
          signed_less_bytes, (ctool_u32)sizeof(signed_less_bytes), NULL, 0u,
          "signed_less") ||
      !decode_function(
          job, text, signed_less_equal, signed_less_equal_instructions,
          (ctool_u32)(sizeof(signed_less_equal_instructions) /
                      sizeof(signed_less_equal_instructions[0])),
          signed_less_equal_bytes,
          (ctool_u32)sizeof(signed_less_equal_bytes), NULL, 0u,
          "signed_less_equal") ||
      !decode_function(
          job, text, unsigned_less, unsigned_less_instructions,
          (ctool_u32)(sizeof(unsigned_less_instructions) /
                      sizeof(unsigned_less_instructions[0])),
          unsigned_less_bytes, (ctool_u32)sizeof(unsigned_less_bytes), NULL,
          0u, "unsigned_less")) {
    (void)fprintf(stderr, "branch-range object differs\n");
    return 0;
  }
  return 1;
}

static int validate_aes_rotw_object(ctool_job_t *job,
                                    const ctool_elf32_object_t *object) {
  static const ctool_u8 rotw_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x08u,
      0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0xd3u, 0xe0u, 0x50u,
      0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x68u, 0x18u, 0x00u, 0x00u, 0x00u,
      0x59u, 0x58u, 0xd3u, 0xe8u, 0x50u, 0x59u, 0x58u, 0x09u,
      0xc8u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 signed_right_shift_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0xd3u, 0xf8u, 0x50u, 0x58u, 0xc9u,
      0xc3u};
  static const ctool_x86_mnemonic_t rotw_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_SHL,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,  CTOOL_X86_MN_SHR,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_OR,    CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t signed_right_shift_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_SAR,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *rotw_symbol = find_symbol(object, "rotw");
  const ctool_elf32_symbol_t *signed_right_shift_symbol =
      find_symbol(object, "signed_right_shift");
  ctool_u32 signed_right_shift_offset = (ctool_u32)sizeof(rotw_bytes);
  ctool_u32 total_size =
      signed_right_shift_offset +
      (ctool_u32)sizeof(signed_right_shift_bytes);
  if (text == NULL || rel_text != NULL || rotw_symbol == NULL ||
      signed_right_shift_symbol == NULL || text->contents.size != total_size ||
      text->relocation_count != 0u || object->symbol_count != 3u ||
      object->relocation_count != 0u ||
      !symbol_matches(rotw_symbol, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(rotw_bytes)) ||
      !symbol_matches(signed_right_shift_symbol, 2u,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      signed_right_shift_offset,
                      (ctool_u32)sizeof(signed_right_shift_bytes)) ||
      !decode_function(
          job, text, rotw_symbol, rotw_instructions,
          (ctool_u32)(sizeof(rotw_instructions) /
                      sizeof(rotw_instructions[0])),
          rotw_bytes, (ctool_u32)sizeof(rotw_bytes), NULL, 0u,
          "rotw") ||
      !decode_function(
          job, text, signed_right_shift_symbol,
          signed_right_shift_instructions,
          (ctool_u32)(sizeof(signed_right_shift_instructions) /
                      sizeof(signed_right_shift_instructions[0])),
          signed_right_shift_bytes,
          (ctool_u32)sizeof(signed_right_shift_bytes), NULL, 0u,
          "signed_right_shift")) {
    (void)fprintf(stderr, "AES word-rotation object differs\n");
    return 0;
  }
  return 1;
}

static int validate_align_up_object(ctool_job_t *job,
                                    const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x01u, 0xc8u, 0x50u, 0x68u, 0x01u,
      0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0x29u, 0xc8u, 0x50u,
      0x8du, 0x85u, 0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x68u, 0x01u, 0x00u, 0x00u, 0x00u,
      0x59u, 0x58u, 0x29u, 0xc8u, 0x50u, 0x58u, 0xf7u, 0xd0u,
      0x50u, 0x59u, 0x58u, 0x21u, 0xc8u, 0x50u, 0x58u, 0xc9u,
      0xc3u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,  CTOOL_X86_MN_ADD,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_SUB,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,  CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,  CTOOL_X86_MN_NOT,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_AND,   CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *function = find_symbol(object, "align_up");
  if (text == NULL || rel_text != NULL || function == NULL ||
      text->contents.size != (ctool_u32)sizeof(function_bytes) ||
      text->relocation_count != 0u || object->symbol_count != 2u ||
      object->relocation_count != 0u ||
      !symbol_matches(function, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(function_bytes)) ||
      !decode_function(
          job, text, function, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), NULL, 0u,
          "align_up")) {
    (void)fprintf(stderr, "memory alignment object differs\n");
    return 0;
  }
  return 1;
}

static int validate_integer_unary_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 unary_plus_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u,
      0xc3u};
  static const ctool_u8 negate_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x58u, 0xf7u, 0xd8u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 logical_not_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x58u, 0x85u, 0xc0u, 0x0fu, 0x94u, 0xc0u, 0x0fu,
      0xb6u, 0xc0u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t unary_plus_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP, CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP, CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t negate_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP, CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP, CTOOL_X86_MN_NEG,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP, CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t logical_not_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_SETE, CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *unary_plus =
      find_symbol(object, "unary_plus");
  const ctool_elf32_symbol_t *signed_negate =
      find_symbol(object, "signed_negate");
  const ctool_elf32_symbol_t *unsigned_negate =
      find_symbol(object, "unsigned_negate");
  const ctool_elf32_symbol_t *logical_not =
      find_symbol(object, "logical_not");
  ctool_u32 unary_plus_offset = 0u;
  ctool_u32 signed_negate_offset =
      unary_plus_offset + (ctool_u32)sizeof(unary_plus_bytes);
  ctool_u32 unsigned_negate_offset =
      signed_negate_offset + (ctool_u32)sizeof(negate_bytes);
  ctool_u32 logical_not_offset =
      unsigned_negate_offset + (ctool_u32)sizeof(negate_bytes);
  ctool_u32 total_size =
      logical_not_offset + (ctool_u32)sizeof(logical_not_bytes);
  if (text == NULL || rel_text != NULL || unary_plus == NULL ||
      signed_negate == NULL || unsigned_negate == NULL ||
      logical_not == NULL || text->contents.size != total_size ||
      text->relocation_count != 0u || object->symbol_count != 5u ||
      object->relocation_count != 0u ||
      !symbol_matches(unary_plus, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      unary_plus_offset,
                      (ctool_u32)sizeof(unary_plus_bytes)) ||
      !symbol_matches(signed_negate, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      signed_negate_offset,
                      (ctool_u32)sizeof(negate_bytes)) ||
      !symbol_matches(unsigned_negate, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      unsigned_negate_offset,
                      (ctool_u32)sizeof(negate_bytes)) ||
      !symbol_matches(logical_not, 4u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      logical_not_offset,
                      (ctool_u32)sizeof(logical_not_bytes)) ||
      !decode_function(
          job, text, unary_plus, unary_plus_instructions,
          (ctool_u32)(sizeof(unary_plus_instructions) /
                      sizeof(unary_plus_instructions[0])),
          unary_plus_bytes, (ctool_u32)sizeof(unary_plus_bytes), NULL, 0u,
          "unary_plus") ||
      !decode_function(
          job, text, signed_negate, negate_instructions,
          (ctool_u32)(sizeof(negate_instructions) /
                      sizeof(negate_instructions[0])),
          negate_bytes, (ctool_u32)sizeof(negate_bytes), NULL, 0u,
          "signed_negate") ||
      !decode_function(
          job, text, unsigned_negate, negate_instructions,
          (ctool_u32)(sizeof(negate_instructions) /
                      sizeof(negate_instructions[0])),
          negate_bytes, (ctool_u32)sizeof(negate_bytes), NULL, 0u,
          "unsigned_negate") ||
      !decode_function(
          job, text, logical_not, logical_not_instructions,
          (ctool_u32)(sizeof(logical_not_instructions) /
                      sizeof(logical_not_instructions[0])),
          logical_not_bytes, (ctool_u32)sizeof(logical_not_bytes), NULL, 0u,
          "logical_not")) {
    (void)fprintf(stderr, "integer unary object differs\n");
    return 0;
  }
  return 1;
}

static int validate_integer_cast_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 signed_bits_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xf7u,
      0xd0u, 0x50u, 0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0x59u,
      0x58u, 0x01u, 0xc8u, 0x50u, 0x58u, 0xf7u, 0xd8u, 0x50u,
      0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 unsigned_bits_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u,
      0xc3u};
  static const ctool_x86_mnemonic_t signed_bits_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_NOT,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_ADD,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_NEG,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t unsigned_bits_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP, CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP, CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *signed_bits =
      find_symbol(object, "signed_bits_magnitude");
  const ctool_elf32_symbol_t *unsigned_bits =
      find_symbol(object, "unsigned_bits");
  ctool_u32 unsigned_bits_offset = (ctool_u32)sizeof(signed_bits_bytes);
  ctool_u32 total_size =
      unsigned_bits_offset + (ctool_u32)sizeof(unsigned_bits_bytes);
  if (text == NULL || rel_text != NULL || signed_bits == NULL ||
      unsigned_bits == NULL || text->contents.size != total_size ||
      text->relocation_count != 0u || object->symbol_count != 3u ||
      object->relocation_count != 0u ||
      !symbol_matches(signed_bits, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(signed_bits_bytes)) ||
      !symbol_matches(unsigned_bits, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      unsigned_bits_offset,
                      (ctool_u32)sizeof(unsigned_bits_bytes)) ||
      !decode_function(
          job, text, signed_bits, signed_bits_instructions,
          (ctool_u32)(sizeof(signed_bits_instructions) /
                      sizeof(signed_bits_instructions[0])),
          signed_bits_bytes, (ctool_u32)sizeof(signed_bits_bytes), NULL, 0u,
          "signed_bits_magnitude") ||
      !decode_function(
          job, text, unsigned_bits, unsigned_bits_instructions,
          (ctool_u32)(sizeof(unsigned_bits_instructions) /
                      sizeof(unsigned_bits_instructions[0])),
          unsigned_bits_bytes, (ctool_u32)sizeof(unsigned_bits_bytes), NULL,
          0u, "unsigned_bits")) {
    (void)fprintf(stderr, "integer cast object differs\n");
    return 0;
  }
  return 1;
}

static int validate_signed_bits_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0xffu,
      0xffu, 0xffu, 0x7fu, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu,
      0x96u, 0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x0eu, 0x00u, 0x00u, 0x00u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u, 0x8du, 0x85u, 0x08u,
      0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x68u, 0x00u, 0x00u, 0x00u, 0x80u, 0x59u, 0x58u, 0x39u,
      0xc8u, 0x0fu, 0x94u, 0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u,
      0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u, 0x16u, 0x00u, 0x00u,
      0x00u, 0x68u, 0xffu, 0xffu, 0xffu, 0x7fu, 0x58u, 0xf7u,
      0xd8u, 0x50u, 0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0x59u,
      0x58u, 0x29u, 0xc8u, 0x50u, 0x58u, 0xc9u, 0xc3u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x58u, 0xf7u, 0xd0u, 0x50u, 0x68u, 0x01u,
      0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0x01u, 0xc8u, 0x50u,
      0x58u, 0xf7u, 0xd8u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u32 branch_targets[] = {53u, 111u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,   CTOOL_X86_MN_SETBE,
      CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,    CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET,   CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_CMP,   CTOOL_X86_MN_SETE,  CTOOL_X86_MN_MOVZX,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_NEG,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET,   CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_NOT,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_ADD,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_NEG,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "dis_signed_bits");
  if (text == NULL || rel_text != NULL || function == NULL ||
      text->contents.size != (ctool_u32)sizeof(function_bytes) ||
      text->relocation_count != 0u || object->symbol_count != 2u ||
      object->relocation_count != 0u ||
      !symbol_matches(function, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(function_bytes)) ||
      !decode_function(
          job, text, function, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), branch_targets,
          (ctool_u32)(sizeof(branch_targets) / sizeof(branch_targets[0])),
          "dis_signed_bits")) {
    (void)fprintf(stderr, "signed-bit object differs\n");
    return 0;
  }
  return 1;
}

static int validate_while_object(ctool_job_t *job,
                                 const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x04u, 0x8du, 0x45u,
      0xfcu, 0x50u, 0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0x50u,
      0x59u, 0x58u, 0x89u, 0x08u, 0x83u, 0xecu, 0x04u, 0xe8u,
      0xfcu, 0xffu, 0xffu, 0xffu, 0x83u, 0xc4u, 0x04u, 0x50u,
      0x8du, 0x45u, 0xfcu, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x59u, 0x58u, 0x29u, 0xc8u, 0x50u, 0x8du, 0x85u, 0x08u,
      0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x92u, 0xc0u, 0x0fu,
      0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u,
      0x10u, 0x00u, 0x00u, 0x00u, 0x83u, 0xecu, 0x04u, 0xe8u,
      0xfcu, 0xffu, 0xffu, 0xffu, 0x83u, 0xc4u, 0x04u, 0xe9u,
      0xb8u, 0xffu, 0xffu, 0xffu, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_CALL,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_SUB,   CTOOL_X86_MN_CALL,
      CTOOL_X86_MN_ADD,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_SUB,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,   CTOOL_X86_MN_SETB,
      CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,    CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_CALL,  CTOOL_X86_MN_ADD,   CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 branch_targets[] = {92u, 20u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "syscall_sleep_ms");
  const ctool_elf32_symbol_t *timer =
      find_symbol(object, "timer_get_uptime_ms");
  const ctool_elf32_symbol_t *yield = find_symbol(object, "process_yield");
  if (text == NULL || rel_text == NULL || function == NULL || timer == NULL ||
      yield == NULL || text->contents.size != (ctool_u32)sizeof(function_bytes) ||
      text->relocation_count != 3u || object->symbol_count != 4u ||
      object->relocation_count != 3u ||
      !symbol_matches(function, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(function_bytes)) ||
      !symbol_matches(timer, timer->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u) ||
      !symbol_matches(yield, yield->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u) ||
      object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].offset != 11u ||
      object->relocations[0].symbol_file_index != timer->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != -4 ||
      object->relocations[1].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[1].entry_index != 1u ||
      object->relocations[1].target_section_file_index != text->file_index ||
      object->relocations[1].offset != 24u ||
      object->relocations[1].symbol_file_index != timer->file_index ||
      object->relocations[1].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[1].addend_known != CTOOL_TRUE ||
      object->relocations[1].addend != -4 ||
      object->relocations[2].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[2].entry_index != 2u ||
      object->relocations[2].target_section_file_index != text->file_index ||
      object->relocations[2].offset != 80u ||
      object->relocations[2].symbol_file_index != yield->file_index ||
      object->relocations[2].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[2].addend_known != CTOOL_TRUE ||
      object->relocations[2].addend != -4 ||
      !decode_function(
          job, text, function, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), branch_targets,
          (ctool_u32)(sizeof(branch_targets) / sizeof(branch_targets[0])),
          "syscall_sleep_ms")) {
    (void)fprintf(stderr, "while object differs\n");
    return 0;
  }
  return 1;
}

static int validate_do_object(ctool_job_t *job,
                              const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x08u, 0x8du, 0x45u,
      0xfcu, 0x50u, 0x83u, 0xecu, 0x0cu, 0xe8u, 0xfcu, 0xffu,
      0xffu, 0xffu, 0x83u, 0xc4u, 0x0cu, 0x50u, 0x59u, 0x58u,
      0x89u, 0x08u, 0x51u, 0x58u, 0x8du, 0x45u, 0xf8u, 0x50u,
      0x8du, 0x45u, 0xfcu, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0x29u, 0xc8u, 0x50u,
      0x59u, 0x58u, 0x89u, 0x08u, 0x51u, 0x58u, 0x68u, 0x01u,
      0x00u, 0x00u, 0x00u, 0x83u, 0xecu, 0x0cu, 0x8bu, 0x4cu,
      0x24u, 0x0cu, 0x89u, 0x0cu, 0x24u, 0xe8u, 0xfcu, 0xffu,
      0xffu, 0xffu, 0x83u, 0xc4u, 0x10u, 0x8du, 0x45u, 0xf8u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x00u, 0x00u,
      0x00u, 0x00u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x9eu,
      0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u, 0xc0u,
      0x0fu, 0x84u, 0x05u, 0x00u, 0x00u, 0x00u, 0xe9u, 0x8bu,
      0xffu, 0xffu, 0xffu, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_CALL,  CTOOL_X86_MN_ADD,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_SUB,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_CALL,  CTOOL_X86_MN_ADD,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,
      CTOOL_X86_MN_SETLE, CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,
      CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 branch_targets[] = {123u, 6u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "doom_wait_tick");
  const ctool_elf32_symbol_t *time = find_symbol(object, "I_GetTime");
  const ctool_elf32_symbol_t *sleep = find_symbol(object, "I_Sleep");
  if (text == NULL || rel_text == NULL || function == NULL || time == NULL ||
      sleep == NULL ||
      text->contents.size != (ctool_u32)sizeof(function_bytes) ||
      text->relocation_count != 2u || object->symbol_count != 4u ||
      object->relocation_count != 2u ||
      !symbol_matches(function, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(function_bytes)) ||
      !symbol_matches(time, time->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u) ||
      !symbol_matches(sleep, sleep->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u) ||
      object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].offset != 14u ||
      object->relocations[0].symbol_file_index != time->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != -4 ||
      object->relocations[1].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[1].entry_index != 1u ||
      object->relocations[1].target_section_file_index != text->file_index ||
      object->relocations[1].offset != 78u ||
      object->relocations[1].symbol_file_index != sleep->file_index ||
      object->relocations[1].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[1].addend_known != CTOOL_TRUE ||
      object->relocations[1].addend != -4 ||
      !decode_function(
          job, text, function, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), branch_targets,
          (ctool_u32)(sizeof(branch_targets) / sizeof(branch_targets[0])),
          "doom_wait_tick")) {
    (void)fprintf(stderr, "do object differs\n");
    return 0;
  }
  return 1;
}

static int validate_for_object(ctool_job_t *job,
                               const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x04u, 0x8du, 0x45u,
      0xfcu, 0x50u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0x59u,
      0x58u, 0x89u, 0x08u, 0x51u, 0x58u, 0x8du, 0x45u, 0xfcu,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x08u, 0x00u,
      0x00u, 0x00u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x9cu,
      0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u, 0xc0u,
      0x0fu, 0x84u, 0x2au, 0x00u, 0x00u, 0x00u, 0x8du, 0x45u,
      0xfcu, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x8du,
      0x45u, 0xfcu, 0x50u, 0x8du, 0x45u, 0xfcu, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x68u, 0x01u, 0x00u, 0x00u, 0x00u,
      0x59u, 0x58u, 0x01u, 0xc8u, 0x50u, 0x59u, 0x58u, 0x89u,
      0x08u, 0x51u, 0x58u, 0xe9u, 0xb5u, 0xffu, 0xffu, 0xffu,
      0x8du, 0x45u, 0xfcu, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,   CTOOL_X86_MN_SETL,
      CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,    CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_ADD,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_u32 branch_targets[] = {96u, 21u};
  static const ctool_u8 break_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x05u, 0x00u, 0x00u, 0x00u, 0xe9u, 0x05u, 0x00u, 0x00u,
      0x00u, 0xe9u, 0xe2u, 0xffu, 0xffu, 0xffu, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t break_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_JMP,   CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 break_targets[] = {28u, 33u, 3u};
  static const ctool_u8 break_do_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0xe9u, 0x00u,
      0x00u, 0x00u, 0x00u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t break_do_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 break_do_targets[] = {8u};
  static const ctool_u8 continue_while_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x05u, 0x00u, 0x00u, 0x00u, 0xe9u, 0xe7u, 0xffu, 0xffu,
      0xffu, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t continue_while_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_u32 continue_while_targets[] = {28u, 3u};
  static const ctool_u8 continue_do_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0xe9u, 0x00u, 0x00u, 0x00u, 0x00u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u, 0x05u, 0x00u, 0x00u,
      0x00u, 0xe9u, 0xe2u, 0xffu, 0xffu, 0xffu, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t continue_do_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,    CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 continue_do_targets[] = {8u, 33u, 3u};
  static const ctool_u8 continue_for_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x2cu, 0x00u, 0x00u, 0x00u, 0xe9u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0x29u,
      0xc8u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x51u, 0x58u, 0xe9u,
      0xc0u, 0xffu, 0xffu, 0xffu, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t continue_for_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_SUB,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_u32 continue_for_targets[] = {67u, 28u, 3u};
  static const ctool_u8 nested_continue_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x1eu, 0x00u, 0x00u, 0x00u, 0x8du, 0x85u, 0x0cu, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x05u, 0x00u, 0x00u, 0x00u, 0xe9u, 0xe7u,
      0xffu, 0xffu, 0xffu, 0xe9u, 0xceu, 0xffu, 0xffu, 0xffu, 0xc9u,
      0xc3u};
  static const ctool_x86_mnemonic_t nested_continue_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,
      CTOOL_X86_MN_JMP,   CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_u32 nested_continue_targets[] = {53u, 48u, 23u, 3u};
  static const ctool_u8 nested_break_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x1eu, 0x00u, 0x00u, 0x00u, 0x8du, 0x85u, 0x0cu, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x05u, 0x00u, 0x00u, 0x00u, 0xe9u, 0x00u,
      0x00u, 0x00u, 0x00u, 0xe9u, 0x00u, 0x00u, 0x00u, 0x00u, 0xc9u,
      0xc3u};
  static const ctool_x86_mnemonic_t nested_break_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,
      CTOOL_X86_MN_JMP,   CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_u32 nested_break_targets[] = {53u, 48u, 48u, 53u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "url_hash_loop");
  const ctool_elf32_symbol_t *break_function =
      find_symbol(object, "break_loop");
  const ctool_elf32_symbol_t *break_do = find_symbol(object, "break_do");
  const ctool_elf32_symbol_t *continue_while =
      find_symbol(object, "continue_while");
  const ctool_elf32_symbol_t *continue_do =
      find_symbol(object, "continue_do");
  const ctool_elf32_symbol_t *continue_for =
      find_symbol(object, "continue_for");
  const ctool_elf32_symbol_t *continue_for_no_iteration =
      find_symbol(object, "continue_for_no_iteration");
  const ctool_elf32_symbol_t *nested_continue =
      find_symbol(object, "nested_continue");
  const ctool_elf32_symbol_t *nested_break =
      find_symbol(object, "nested_break");
  if (text == NULL || rel_text != NULL || function == NULL ||
      break_function == NULL || break_do == NULL || continue_while == NULL ||
      continue_do == NULL || continue_for == NULL ||
      continue_for_no_iteration == NULL ||
      nested_continue == NULL || nested_break == NULL ||
      text->contents.size !=
          (ctool_u32)(sizeof(function_bytes) + sizeof(break_bytes) +
                      sizeof(break_do_bytes) +
                      sizeof(continue_while_bytes) +
                      sizeof(continue_do_bytes) +
                      sizeof(continue_for_bytes) +
                      sizeof(continue_while_bytes) +
                      sizeof(nested_continue_bytes) +
                      sizeof(nested_break_bytes)) ||
      text->relocation_count != 0u || object->symbol_count != 10u ||
      object->relocation_count != 0u ||
      !symbol_matches(function, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(function_bytes)) ||
      !symbol_matches(break_function, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)sizeof(function_bytes),
                      (ctool_u32)sizeof(break_bytes)) ||
      !symbol_matches(break_do, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(function_bytes) +
                                  sizeof(break_bytes)),
                      (ctool_u32)sizeof(break_do_bytes)) ||
      !symbol_matches(continue_while, 4u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(function_bytes) +
                                  sizeof(break_bytes) +
                                  sizeof(break_do_bytes)),
                      (ctool_u32)sizeof(continue_while_bytes)) ||
      !symbol_matches(continue_do, 5u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(function_bytes) +
                                  sizeof(break_bytes) +
                                  sizeof(break_do_bytes) +
                                  sizeof(continue_while_bytes)),
                      (ctool_u32)sizeof(continue_do_bytes)) ||
      !symbol_matches(continue_for, 6u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(function_bytes) +
                                  sizeof(break_bytes) +
                                  sizeof(break_do_bytes) +
                                  sizeof(continue_while_bytes) +
                                  sizeof(continue_do_bytes)),
                      (ctool_u32)sizeof(continue_for_bytes)) ||
      !symbol_matches(continue_for_no_iteration, 7u,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(function_bytes) +
                                  sizeof(break_bytes) +
                                  sizeof(break_do_bytes) +
                                  sizeof(continue_while_bytes) +
                                  sizeof(continue_do_bytes) +
                                  sizeof(continue_for_bytes)),
                      (ctool_u32)sizeof(continue_while_bytes)) ||
      !symbol_matches(nested_continue, 8u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(function_bytes) +
                                  sizeof(break_bytes) +
                                  sizeof(break_do_bytes) +
                                  sizeof(continue_while_bytes) +
                                  sizeof(continue_do_bytes) +
                                  sizeof(continue_for_bytes) +
                                  sizeof(continue_while_bytes)),
                      (ctool_u32)sizeof(nested_continue_bytes)) ||
      !symbol_matches(nested_break, 9u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(function_bytes) +
                                  sizeof(break_bytes) +
                                  sizeof(break_do_bytes) +
                                  sizeof(continue_while_bytes) +
                                  sizeof(continue_do_bytes) +
                                  sizeof(continue_for_bytes) +
                                  sizeof(continue_while_bytes) +
                                  sizeof(nested_continue_bytes)),
                      (ctool_u32)sizeof(nested_break_bytes)) ||
      !decode_function(
          job, text, function, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), branch_targets,
          (ctool_u32)(sizeof(branch_targets) / sizeof(branch_targets[0])),
          "url_hash_loop") ||
      !decode_function(
          job, text, break_function, break_instructions,
          (ctool_u32)(sizeof(break_instructions) /
                      sizeof(break_instructions[0])),
          break_bytes, (ctool_u32)sizeof(break_bytes), break_targets,
          (ctool_u32)(sizeof(break_targets) / sizeof(break_targets[0])),
          "break_loop") ||
      !decode_function(
          job, text, break_do, break_do_instructions,
          (ctool_u32)(sizeof(break_do_instructions) /
                      sizeof(break_do_instructions[0])),
          break_do_bytes, (ctool_u32)sizeof(break_do_bytes), break_do_targets,
          (ctool_u32)(sizeof(break_do_targets) /
                      sizeof(break_do_targets[0])),
          "break_do") ||
      !decode_function(
          job, text, continue_while, continue_while_instructions,
          (ctool_u32)(sizeof(continue_while_instructions) /
                      sizeof(continue_while_instructions[0])),
          continue_while_bytes, (ctool_u32)sizeof(continue_while_bytes),
          continue_while_targets,
          (ctool_u32)(sizeof(continue_while_targets) /
                      sizeof(continue_while_targets[0])),
          "continue_while") ||
      !decode_function(
          job, text, continue_do, continue_do_instructions,
          (ctool_u32)(sizeof(continue_do_instructions) /
                      sizeof(continue_do_instructions[0])),
          continue_do_bytes, (ctool_u32)sizeof(continue_do_bytes),
          continue_do_targets,
          (ctool_u32)(sizeof(continue_do_targets) /
                      sizeof(continue_do_targets[0])),
          "continue_do") ||
      !decode_function(
          job, text, continue_for, continue_for_instructions,
          (ctool_u32)(sizeof(continue_for_instructions) /
                      sizeof(continue_for_instructions[0])),
          continue_for_bytes, (ctool_u32)sizeof(continue_for_bytes),
          continue_for_targets,
          (ctool_u32)(sizeof(continue_for_targets) /
                      sizeof(continue_for_targets[0])),
          "continue_for") ||
      !decode_function(
          job, text, continue_for_no_iteration,
          continue_while_instructions,
          (ctool_u32)(sizeof(continue_while_instructions) /
                      sizeof(continue_while_instructions[0])),
          continue_while_bytes, (ctool_u32)sizeof(continue_while_bytes),
          continue_while_targets,
          (ctool_u32)(sizeof(continue_while_targets) /
                      sizeof(continue_while_targets[0])),
          "continue_for_no_iteration") ||
      !decode_function(
          job, text, nested_continue, nested_continue_instructions,
          (ctool_u32)(sizeof(nested_continue_instructions) /
                      sizeof(nested_continue_instructions[0])),
          nested_continue_bytes, (ctool_u32)sizeof(nested_continue_bytes),
          nested_continue_targets,
          (ctool_u32)(sizeof(nested_continue_targets) /
                      sizeof(nested_continue_targets[0])),
          "nested_continue") ||
      !decode_function(
          job, text, nested_break, nested_break_instructions,
          (ctool_u32)(sizeof(nested_break_instructions) /
                      sizeof(nested_break_instructions[0])),
          nested_break_bytes, (ctool_u32)sizeof(nested_break_bytes),
          nested_break_targets,
          (ctool_u32)(sizeof(nested_break_targets) /
                      sizeof(nested_break_targets[0])),
          "nested_break")) {
    (void)fprintf(stderr, "for object shape differs\n");
    return 0;
  }
  return 1;
}

static int validate_declaration_for_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x04u, 0x8du, 0x45u,
      0xfcu, 0x50u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0x59u,
      0x58u, 0x89u, 0x08u, 0x8du, 0x45u, 0xfcu, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x68u, 0x01u, 0x00u, 0x00u, 0x00u,
      0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x9cu, 0xc0u, 0x0fu,
      0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u,
      0x21u, 0x00u, 0x00u, 0x00u, 0x8du, 0x45u, 0xfcu, 0x50u,
      0x8du, 0x45u, 0xfcu, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0x01u,
      0xc8u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x51u, 0x58u,
      0xe9u, 0xbeu, 0xffu, 0xffu, 0xffu, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,
      CTOOL_X86_MN_SETL,  CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_ADD,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 branch_targets[] = {85u, 19u};
  static const ctool_u8 nested_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x08u, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u, 0x1eu, 0x00u,
      0x00u, 0x00u, 0x8du, 0x45u, 0xfcu, 0x50u, 0x8du, 0x85u,
      0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x8du, 0x45u, 0xfcu,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u,
      0x8du, 0x45u, 0xf8u, 0x50u, 0x68u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x59u, 0x58u, 0x89u, 0x08u, 0x8du, 0x45u, 0xf8u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t nested_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,    CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_u32 nested_targets[] = {56u};
  static const ctool_u8 unreachable_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u,
      0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t unreachable_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP, CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u8 loop_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x05u, 0x00u, 0x00u, 0x00u, 0xe9u,
      0x00u, 0x00u, 0x00u, 0x00u, 0xe9u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x05u, 0x00u, 0x00u, 0x00u, 0xe9u, 0x00u, 0x00u,
      0x00u, 0x00u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t loop_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,   CTOOL_X86_MN_JMP,  CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_TEST, CTOOL_X86_MN_JE,   CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 loop_targets[] = {28u, 28u, 33u, 58u, 58u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "declaration_for");
  const ctool_elf32_symbol_t *nested =
      find_symbol(object, "nested_declaration");
  const ctool_elf32_symbol_t *unreachable =
      find_symbol(object, "unreachable_declaration");
  const ctool_elf32_symbol_t *loop =
      find_symbol(object, "loop_declarations");
  if (text == NULL || rel_text != NULL || function == NULL || nested == NULL ||
      unreachable == NULL || loop == NULL ||
      text->contents.size !=
          (ctool_u32)(sizeof(function_bytes) + sizeof(nested_bytes) +
                      sizeof(unreachable_bytes) + sizeof(loop_bytes)) ||
      text->relocation_count != 0u || object->symbol_count != 5u ||
      object->relocation_count != 0u ||
      !symbol_matches(function, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(function_bytes)) ||
      !symbol_matches(nested, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)sizeof(function_bytes),
                      (ctool_u32)sizeof(nested_bytes)) ||
      !symbol_matches(unreachable, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(function_bytes) +
                                  sizeof(nested_bytes)),
                      (ctool_u32)sizeof(unreachable_bytes)) ||
      !symbol_matches(loop, 4u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)(sizeof(function_bytes) +
                                  sizeof(nested_bytes) +
                                  sizeof(unreachable_bytes)),
                      (ctool_u32)sizeof(loop_bytes)) ||
      !decode_function(
          job, text, function, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes), branch_targets,
          (ctool_u32)(sizeof(branch_targets) / sizeof(branch_targets[0])),
          "declaration_for") ||
      !decode_function(
          job, text, nested, nested_instructions,
          (ctool_u32)(sizeof(nested_instructions) /
                      sizeof(nested_instructions[0])),
          nested_bytes, (ctool_u32)sizeof(nested_bytes), nested_targets,
          (ctool_u32)(sizeof(nested_targets) / sizeof(nested_targets[0])),
          "nested_declaration") ||
      !decode_function(
          job, text, unreachable, unreachable_instructions,
          (ctool_u32)(sizeof(unreachable_instructions) /
                      sizeof(unreachable_instructions[0])),
          unreachable_bytes, (ctool_u32)sizeof(unreachable_bytes), NULL, 0u,
          "unreachable_declaration") ||
      !decode_function(
          job, text, loop, loop_instructions,
          (ctool_u32)(sizeof(loop_instructions) /
                      sizeof(loop_instructions[0])),
          loop_bytes, (ctool_u32)sizeof(loop_bytes), loop_targets,
          (ctool_u32)(sizeof(loop_targets) / sizeof(loop_targets[0])),
          "loop_declarations")) {
    (void)fprintf(stderr, "declaration for object differs\n");
    return 0;
  }
  return 1;
}

static int validate_selection_edge_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 unreachable_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u,
      0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t unreachable_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP, CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u8 void_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x58u, 0x85u, 0xc0u, 0x0fu, 0x84u, 0x02u, 0x00u,
      0x00u, 0x00u, 0xc9u, 0xc3u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t void_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV, CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP, CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP, CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 void_branch_targets[] = {25u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *unreachable =
      find_symbol(object, "unreachable_tail");
  const ctool_elf32_symbol_t *void_function =
      find_symbol(object, "maybe_return");
  if (text == NULL || rel_text != NULL || unreachable == NULL ||
      void_function == NULL ||
      text->contents.size !=
          (ctool_u32)(sizeof(unreachable_bytes) + sizeof(void_bytes)) ||
      text->relocation_count != 0u || object->symbol_count != 3u ||
      object->relocation_count != 0u ||
      !symbol_matches(unreachable, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(unreachable_bytes)) ||
      !symbol_matches(void_function, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      (ctool_u32)sizeof(unreachable_bytes),
                      (ctool_u32)sizeof(void_bytes)) ||
      !decode_function(
          job, text, unreachable, unreachable_instructions,
          (ctool_u32)(sizeof(unreachable_instructions) /
                      sizeof(unreachable_instructions[0])),
          unreachable_bytes, (ctool_u32)sizeof(unreachable_bytes), NULL, 0u,
          "unreachable_tail") ||
      !decode_function(
          job, text, void_function, void_instructions,
          (ctool_u32)(sizeof(void_instructions) /
                      sizeof(void_instructions[0])),
          void_bytes, (ctool_u32)sizeof(void_bytes), void_branch_targets,
          (ctool_u32)(sizeof(void_branch_targets) /
                      sizeof(void_branch_targets[0])),
          "maybe_return")) {
    (void)fprintf(stderr, "selection edge object differs\n");
    return 0;
  }
  return 1;
}

static int validate_simd_cpuid_object(ctool_job_t *job,
                                      const ctool_elf32_object_t *object) {
  static const ctool_u8 simd_cpuid_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x31u, 0xc8u, 0x50u, 0x68u, 0x01u,
      0x00u, 0x00u, 0x00u, 0x68u, 0x15u, 0x00u, 0x00u, 0x00u,
      0x59u, 0x58u, 0xd3u, 0xe0u, 0x50u, 0x59u, 0x58u, 0x21u,
      0xc8u, 0x50u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0x59u,
      0x58u, 0x39u, 0xc8u, 0x0fu, 0x95u, 0xc0u, 0x0fu, 0xb6u,
      0xc0u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t simd_cpuid_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_XOR,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_SHL,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_AND,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,
      CTOOL_X86_MN_SETNE, CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *simd_cpuid_symbol =
      find_symbol(object, "simd_cpuid_changed");
  if (text == NULL || rel_text != NULL || simd_cpuid_symbol == NULL ||
      text->contents.size != (ctool_u32)sizeof(simd_cpuid_bytes) ||
      text->relocation_count != 0u || object->symbol_count != 2u ||
      object->relocation_count != 0u ||
      !symbol_matches(simd_cpuid_symbol, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(simd_cpuid_bytes)) ||
      !decode_function(
          job, text, simd_cpuid_symbol, simd_cpuid_instructions,
          (ctool_u32)(sizeof(simd_cpuid_instructions) /
                      sizeof(simd_cpuid_instructions[0])),
          simd_cpuid_bytes, (ctool_u32)sizeof(simd_cpuid_bytes), NULL, 0u,
          "simd_cpuid_changed")) {
    (void)fprintf(stderr, "CPUID toggle object differs\n");
    return 0;
  }
  return 1;
}

static int validate_function_object(ctool_job_t *job,
                                    const ctool_elf32_object_t *object) {
  static const ctool_u8 implemented_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x2au, 0x00u,
      0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 helper_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0xffu,
      0xffu, 0xffu, 0xffu, 0x8du, 0x85u, 0x0cu, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u,
      0x29u, 0xc8u, 0x50u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu,
      0x97u, 0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u,
      0x01u, 0x00u, 0x00u, 0x00u, 0xe9u, 0x05u, 0x00u, 0x00u,
      0x00u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u,
      0xc3u};
  static const ctool_u8 signed_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x9fu, 0xc0u,
      0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 idle_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0xc9u, 0xc3u};
  static const ctool_u8 local_target_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x09u, 0x00u,
      0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 call_local_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x08u, 0xe8u, 0xfcu,
      0xffu, 0xffu, 0xffu, 0x83u, 0xc4u, 0x08u, 0x50u, 0x58u,
      0xc9u, 0xc3u};
  static const ctool_u8 call_external_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x8bu, 0x4cu, 0x24u, 0x04u, 0x8bu, 0x14u, 0x24u,
      0x89u, 0x54u, 0x24u, 0x04u, 0x89u, 0x0cu, 0x24u, 0xe8u,
      0xfcu, 0xffu, 0xffu, 0xffu, 0x83u, 0xc4u, 0x08u, 0x50u,
      0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 call_nested_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x83u, 0xecu,
      0x04u, 0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0x83u, 0xc4u,
      0x04u, 0x50u, 0x8du, 0x85u, 0x10u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8bu, 0x4cu, 0x24u,
      0x08u, 0x8bu, 0x14u, 0x24u, 0x89u, 0x54u, 0x24u, 0x08u,
      0x89u, 0x0cu, 0x24u, 0x83u, 0xecu, 0x0cu, 0x8bu, 0x4cu,
      0x24u, 0x0cu, 0x89u, 0x0cu, 0x24u, 0x8bu, 0x4cu, 0x24u,
      0x10u, 0x89u, 0x4cu, 0x24u, 0x04u, 0x8bu, 0x4cu, 0x24u,
      0x14u, 0x89u, 0x4cu, 0x24u, 0x08u, 0xe8u, 0xfcu, 0xffu,
      0xffu, 0xffu, 0x83u, 0xc4u, 0x18u, 0x50u, 0x58u, 0xc9u,
      0xc3u};
  static const ctool_u8 call_void_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x83u, 0xecu,
      0x04u, 0x8bu, 0x4cu, 0x24u, 0x04u, 0x89u, 0x0cu, 0x24u,
      0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0x83u, 0xc4u, 0x08u,
      0xc9u, 0xc3u};
  static const ctool_u8 add2_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x01u, 0xc8u, 0x50u, 0x58u, 0xc9u,
      0xc3u};
  static const ctool_u8 local_round_trip_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x08u, 0x8du, 0x45u,
      0xfcu, 0x50u, 0x83u, 0xecu, 0x0cu, 0xe8u, 0xfcu, 0xffu,
      0xffu, 0xffu, 0x83u, 0xc4u, 0x0cu, 0x50u, 0x59u, 0x58u,
      0x89u, 0x08u, 0x8du, 0x45u, 0xf8u, 0x50u,
      0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x8du,
      0x45u, 0xfcu, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du,
      0x45u, 0xf8u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u,
      0x58u, 0x01u, 0xc8u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 local_call_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x04u, 0x8du, 0x45u,
      0xfcu, 0x50u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x02u, 0x00u,
      0x00u, 0x00u, 0x8bu, 0x4cu, 0x24u, 0x04u, 0x8bu, 0x14u,
      0x24u, 0x89u, 0x54u, 0x24u, 0x04u, 0x89u, 0x0cu, 0x24u,
      0x83u, 0xecu, 0x08u, 0x8bu, 0x4cu, 0x24u, 0x08u, 0x89u,
      0x0cu, 0x24u, 0x8bu, 0x4cu, 0x24u, 0x0cu, 0x89u, 0x4cu,
      0x24u, 0x04u, 0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0x83u,
      0xc4u, 0x10u,
      0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x8du, 0x45u, 0xfcu,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 uninitialized_local_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x04u, 0x8du, 0x45u, 0xfcu,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 vga_flip_ready_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x04u, 0x8du, 0x45u,
      0xfcu, 0x50u, 0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0x50u,
      0x59u, 0x58u, 0x89u, 0x08u, 0x8du, 0x45u, 0xfcu, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0x29u,
      0xc8u, 0x50u, 0x68u, 0x10u, 0x00u, 0x00u, 0x00u, 0x59u,
      0x58u, 0x39u, 0xc8u, 0x0fu, 0x93u, 0xc0u, 0x0fu, 0xb6u,
      0xc0u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 external_clock_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 signed_greater_equal_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x9du, 0xc0u,
      0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 power_of_two_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu,
      0x95u, 0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x48u, 0x00u, 0x00u, 0x00u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x01u, 0x00u,
      0x00u, 0x00u, 0x59u, 0x58u, 0x29u, 0xc8u, 0x50u, 0x59u,
      0x58u, 0x21u, 0xc8u, 0x50u, 0x68u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x94u, 0xc0u,
      0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u, 0x01u, 0x00u,
      0x00u, 0x00u, 0xe9u, 0x05u, 0x00u, 0x00u, 0x00u, 0x68u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u, 0x01u, 0x00u,
      0x00u, 0x00u, 0xe9u, 0x05u, 0x00u, 0x00u, 0x00u, 0x68u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u8 bool_valid_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu,
      0x94u, 0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u,
      0xc0u, 0x0fu, 0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u,
      0x01u, 0x00u, 0x00u, 0x00u, 0xe9u, 0x33u, 0x00u, 0x00u,
      0x00u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x01u, 0x00u, 0x00u,
      0x00u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x94u, 0xc0u,
      0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u, 0x01u, 0x00u,
      0x00u, 0x00u, 0xe9u, 0x05u, 0x00u, 0x00u, 0x00u, 0x68u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0x85u, 0xc0u, 0x0fu,
      0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0x68u, 0x01u, 0x00u,
      0x00u, 0x00u, 0xe9u, 0x05u, 0x00u, 0x00u, 0x00u, 0x68u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u32 helper_branch_targets[] = {65u, 70u};
  static const ctool_u32 power_of_two_branch_targets[] = {
      111u, 111u, 116u, 135u, 140u};
  static const ctool_u32 bool_valid_branch_targets[] = {
      49u, 100u, 95u, 100u, 119u, 124u};
  static const ctool_x86_mnemonic_t implemented_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP, CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t helper_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_SUB,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,   CTOOL_X86_MN_SETA,
      CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,    CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_JMP,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t idle_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t local_target_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP, CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t call_local_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_CALL, CTOOL_X86_MN_ADD, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP, CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t call_external_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_CALL, CTOOL_X86_MN_ADD,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t call_nested_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_SUB,  CTOOL_X86_MN_CALL,
      CTOOL_X86_MN_ADD,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_SUB,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_CALL, CTOOL_X86_MN_ADD,
      CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t call_void_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP, CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_SUB, CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_MOV, CTOOL_X86_MN_CALL, CTOOL_X86_MN_ADD,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t add2_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,  CTOOL_X86_MN_ADD,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t local_round_trip_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,  CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_CALL, CTOOL_X86_MN_ADD,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,  CTOOL_X86_MN_ADD,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t local_call_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,  CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_SUB,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_CALL,
      CTOOL_X86_MN_ADD,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t uninitialized_local_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_LEA, CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t vga_flip_ready_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_CALL,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_SUB,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,
      CTOOL_X86_MN_SETAE, CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t external_clock_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP, CTOOL_X86_MN_MOV, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP, CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t signed_greater_equal_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,
      CTOOL_X86_MN_SETGE, CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t signed_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,
      CTOOL_X86_MN_SETG,  CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t power_of_two_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,   CTOOL_X86_MN_SETNE,
      CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,    CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_SUB,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_AND,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_CMP,   CTOOL_X86_MN_SETE,  CTOOL_X86_MN_MOVZX,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t bool_valid_instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,   CTOOL_X86_MN_SETE,
      CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,    CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_CMP,   CTOOL_X86_MN_SETE,  CTOOL_X86_MN_MOVZX,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,
      CTOOL_X86_MN_JE,    CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_JMP,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *data = find_section(object, ".data");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *implemented =
      find_symbol(object, "implemented");
  const ctool_elf32_symbol_t *helper =
      find_symbol(object, "cemit_add_overflows");
  const ctool_elf32_symbol_t *signed_greater =
      find_symbol(object, "signed_greater");
  const ctool_elf32_symbol_t *idle = find_symbol(object, "idle");
  const ctool_elf32_symbol_t *local_target =
      find_symbol(object, "local_target");
  const ctool_elf32_symbol_t *call_local = find_symbol(object, "call_local");
  const ctool_elf32_symbol_t *call_external =
      find_symbol(object, "call_external");
  const ctool_elf32_symbol_t *call_nested =
      find_symbol(object, "call_nested");
  const ctool_elf32_symbol_t *call_void = find_symbol(object, "call_void");
  const ctool_elf32_symbol_t *add2 = find_symbol(object, "add2");
  const ctool_elf32_symbol_t *local_round_trip =
      find_symbol(object, "vga_flip_time_probe");
  const ctool_elf32_symbol_t *local_call =
      find_symbol(object, "local_call_probe");
  const ctool_elf32_symbol_t *uninitialized_local =
      find_symbol(object, "uninitialized_local_probe");
  const ctool_elf32_symbol_t *vga_flip_ready =
      find_symbol(object, "vga_flip_ready");
  const ctool_elf32_symbol_t *read_external_clock =
      find_symbol(object, "read_external_clock");
  const ctool_elf32_symbol_t *signed_greater_equal =
      find_symbol(object, "signed_greater_equal");
  const ctool_elf32_symbol_t *power_of_two =
      find_symbol(object, "cemit_power_of_two");
  const ctool_elf32_symbol_t *bool_valid =
      find_symbol(object, "cfront_bool_valid");
  const ctool_elf32_symbol_t *external_sum =
      find_symbol(object, "external_sum");
  const ctool_elf32_symbol_t *external_three =
      find_symbol(object, "external_three");
  const ctool_elf32_symbol_t *external_sink =
      find_symbol(object, "external_sink");
  const ctool_elf32_symbol_t *timer_get_uptime_ms =
      find_symbol(object, "timer_get_uptime_ms");
  const ctool_elf32_symbol_t *initializer_sum =
      find_symbol(object, "initializer_sum");
  const ctool_elf32_symbol_t *function_data =
      find_symbol(object, "function_data");
  const ctool_elf32_symbol_t *last_flip_ms =
      find_symbol(object, "last_flip_ms");
  const ctool_elf32_symbol_t *external_clock =
      find_symbol(object, "external_clock");
  const ctool_elf32_symbol_t *now = find_symbol(object, "now");
  const ctool_elf32_symbol_t *prior = find_symbol(object, "prior");
  const ctool_elf32_symbol_t *unused = find_symbol(object, "unused");
  const ctool_elf32_symbol_t *value = find_symbol(object, "value");
  if (text == NULL || text->type != CTOOL_ELF32_SHT_PROGBITS ||
      text->flags != (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR) ||
      text->alignment != 1u || text->contents.size == 0u ||
      rel_text == NULL || text->relocation_first != 0u ||
      text->relocation_count != 10u || object->relocation_count != 10u ||
      object->symbol_count != 27u ||
      data == NULL || data->contents.size != 4u ||
      bss == NULL || bss->type != CTOOL_ELF32_SHT_NOBITS ||
      bss->flags != (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE) ||
      bss->size != 4u || bss->contents.size != 0u ||
      implemented == NULL || helper == NULL || idle == NULL ||
      signed_greater == NULL || local_target == NULL || call_local == NULL ||
      call_external == NULL || call_nested == NULL || call_void == NULL ||
      add2 == NULL || local_round_trip == NULL || local_call == NULL ||
      uninitialized_local == NULL || vga_flip_ready == NULL ||
      read_external_clock == NULL || signed_greater_equal == NULL ||
      power_of_two == NULL || bool_valid == NULL ||
      external_sum == NULL || external_three == NULL ||
      external_sink == NULL || timer_get_uptime_ms == NULL ||
      initializer_sum == NULL ||
      function_data == NULL || last_flip_ms == NULL ||
      external_clock == NULL || now != NULL ||
      prior != NULL ||
      unused != NULL || value != NULL ||
      !symbol_matches(implemented, implemented->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      implemented->size) ||
      !symbol_matches(helper, helper->file_index, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      implemented->size, helper->size) ||
      !symbol_matches(signed_greater, signed_greater->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      implemented->size + helper->size,
                      signed_greater->size) ||
      !symbol_matches(idle, idle->file_index, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      implemented->size + helper->size +
                          signed_greater->size,
                      idle->size) ||
      !symbol_matches(local_target, local_target->file_index,
                      CTOOL_ELF32_BIND_LOCAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 128u,
                      11u) ||
      !symbol_matches(call_local, call_local->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 139u,
                      18u) ||
      !symbol_matches(call_external, call_external->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 157u,
                      51u) ||
      !symbol_matches(call_nested, call_nested->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 208u,
                      89u) ||
      !symbol_matches(call_void, call_void->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 297u,
                      34u) ||
      !symbol_matches(add2, add2->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                       CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 331u,
                       33u) ||
      !symbol_matches(local_round_trip, local_round_trip->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                       CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 364u,
                       69u) ||
      !symbol_matches(local_call, local_call->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 433u,
                      82u) ||
      !symbol_matches(uninitialized_local, uninitialized_local->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 515u,
                      17u) ||
      !symbol_matches(vga_flip_ready, vga_flip_ready->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 532u,
                      61u) ||
      !symbol_matches(read_external_clock, read_external_clock->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 593u,
                      15u) ||
      !symbol_matches(signed_greater_equal,
                      signed_greater_equal->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 608u,
                      39u) ||
      !symbol_matches(power_of_two, power_of_two->file_index,
                      CTOOL_ELF32_BIND_LOCAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 647u,
                      (ctool_u32)sizeof(power_of_two_bytes)) ||
      !symbol_matches(bool_valid, bool_valid->file_index,
                      CTOOL_ELF32_BIND_LOCAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 790u,
                      (ctool_u32)sizeof(bool_valid_bytes)) ||
      !symbol_matches(external_sum, external_sum->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u) ||
      !symbol_matches(external_three, external_three->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u) ||
      !symbol_matches(external_sink, external_sink->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                       0u, 0u) ||
      !symbol_matches(timer_get_uptime_ms, timer_get_uptime_ms->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                       CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                       0u, 0u) ||
      !symbol_matches(initializer_sum, initializer_sum->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u) ||
      !symbol_matches(function_data, function_data->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 0u, 4u) ||
      !symbol_matches(last_flip_ms, last_flip_ms->file_index,
                      CTOOL_ELF32_BIND_LOCAL, CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u, 4u) ||
      !symbol_matches(external_clock, external_clock->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_UNDEFINED,
                      CTOOL_ELF32_NO_SECTION, 0u, 0u) ||
      data->contents.data[0] != 7u || data->contents.data[1] != 0u ||
      data->contents.data[2] != 0u || data->contents.data[3] != 0u ||
      implemented->size == 0u || helper->size == 0u ||
      signed_greater->size == 0u || idle->size == 0u ||
      bool_valid->size == 0u || text->contents.size != 917u ||
      object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].offset != 146u ||
      object->relocations[0].symbol_file_index != local_target->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != -4 ||
      object->relocations[1].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[1].entry_index != 1u ||
      object->relocations[1].target_section_file_index != text->file_index ||
      object->relocations[1].offset != 197u ||
      object->relocations[1].symbol_file_index != external_sum->file_index ||
      object->relocations[1].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[1].addend_known != CTOOL_TRUE ||
      object->relocations[1].addend != -4 ||
      object->relocations[2].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[2].entry_index != 2u ||
      object->relocations[2].target_section_file_index != text->file_index ||
      object->relocations[2].offset != 226u ||
      object->relocations[2].symbol_file_index != call_local->file_index ||
      object->relocations[2].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[2].addend_known != CTOOL_TRUE ||
      object->relocations[2].addend != -4 ||
      object->relocations[3].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[3].entry_index != 3u ||
      object->relocations[3].target_section_file_index != text->file_index ||
      object->relocations[3].offset != 286u ||
      object->relocations[3].symbol_file_index != external_three->file_index ||
      object->relocations[3].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[3].addend_known != CTOOL_TRUE ||
      object->relocations[3].addend != -4 ||
      object->relocations[4].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[4].entry_index != 4u ||
      object->relocations[4].target_section_file_index != text->file_index ||
      object->relocations[4].offset != 322u ||
      object->relocations[4].symbol_file_index != external_sink->file_index ||
      object->relocations[4].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[4].addend_known != CTOOL_TRUE ||
      object->relocations[4].addend != -4 ||
      object->relocations[5].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[5].entry_index != 5u ||
      object->relocations[5].target_section_file_index != text->file_index ||
      object->relocations[5].offset != 378u ||
      object->relocations[5].symbol_file_index !=
          timer_get_uptime_ms->file_index ||
      object->relocations[5].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[5].addend_known != CTOOL_TRUE ||
      object->relocations[5].addend != -4 ||
      object->relocations[6].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[6].entry_index != 6u ||
      object->relocations[6].target_section_file_index != text->file_index ||
      object->relocations[6].offset != 492u ||
      object->relocations[6].symbol_file_index != initializer_sum->file_index ||
      object->relocations[6].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[6].addend_known != CTOOL_TRUE ||
      object->relocations[6].addend != -4 ||
      object->relocations[7].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[7].entry_index != 7u ||
      object->relocations[7].target_section_file_index != text->file_index ||
      object->relocations[7].offset != 543u ||
      object->relocations[7].symbol_file_index !=
          timer_get_uptime_ms->file_index ||
      object->relocations[7].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[7].addend_known != CTOOL_TRUE ||
      object->relocations[7].addend != -4 ||
      object->relocations[8].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[8].entry_index != 8u ||
      object->relocations[8].target_section_file_index != text->file_index ||
      object->relocations[8].offset != 561u ||
      object->relocations[8].symbol_file_index != last_flip_ms->file_index ||
      object->relocations[8].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[8].addend_known != CTOOL_TRUE ||
      object->relocations[8].addend != 0 ||
      object->relocations[9].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[9].entry_index != 9u ||
      object->relocations[9].target_section_file_index != text->file_index ||
      object->relocations[9].offset != 597u ||
      object->relocations[9].symbol_file_index != external_clock->file_index ||
      object->relocations[9].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[9].addend_known != CTOOL_TRUE ||
      object->relocations[9].addend != 0) {
    (void)fprintf(stderr, "function object structure differs\n");
    (void)fprintf(stderr,
                  "sections=%lu symbols=%lu relocations=%lu text=%lu "
                  "text-first=%lu text-count=%lu rel-text=%lu\n",
                  (unsigned long)object->section_count,
                  (unsigned long)object->symbol_count,
                  (unsigned long)object->relocation_count,
                  text != NULL ? (unsigned long)text->contents.size : 0ul,
                  text != NULL ? (unsigned long)text->relocation_first : 0ul,
                  text != NULL ? (unsigned long)text->relocation_count : 0ul,
                  rel_text != NULL
                      ? (unsigned long)rel_text->relocation_count
                      : 0ul);
    if (local_target != NULL && call_local != NULL &&
        call_external != NULL && call_nested != NULL && call_void != NULL &&
        external_sum != NULL) {
      (void)fprintf(stderr,
                    "local=%lu/%lu call-local=%lu/%lu "
                    "call-external=%lu/%lu call-nested=%lu/%lu "
                    "call-void=%lu/%lu "
                    "external=%lu/%lu\n",
                    (unsigned long)local_target->value,
                    (unsigned long)local_target->size,
                    (unsigned long)call_local->value,
                    (unsigned long)call_local->size,
                    (unsigned long)call_external->value,
                    (unsigned long)call_external->size,
                    (unsigned long)call_nested->value,
                    (unsigned long)call_nested->size,
                    (unsigned long)call_void->value,
                    (unsigned long)call_void->size,
                    (unsigned long)external_sum->placement,
                    (unsigned long)external_sum->file_index);
    }
    if (object->relocation_count >= 5u) {
      (void)fprintf(stderr,
                    "rel0=%lu/%lu/%ld/%lu rel1=%lu/%lu/%ld/%lu "
                    "rel2=%lu/%lu/%ld/%lu rel3=%lu/%lu/%ld/%lu "
                    "rel4=%lu/%lu/%ld/%lu\n",
                    (unsigned long)object->relocations[0].offset,
                    (unsigned long)object->relocations[0].type,
                    (long)object->relocations[0].addend,
                    (unsigned long)object->relocations[0].symbol_file_index,
                    (unsigned long)object->relocations[1].offset,
                    (unsigned long)object->relocations[1].type,
                    (long)object->relocations[1].addend,
                    (unsigned long)object->relocations[1].symbol_file_index,
                    (unsigned long)object->relocations[2].offset,
                    (unsigned long)object->relocations[2].type,
                    (long)object->relocations[2].addend,
                    (unsigned long)object->relocations[2].symbol_file_index,
                    (unsigned long)object->relocations[3].offset,
                    (unsigned long)object->relocations[3].type,
                    (long)object->relocations[3].addend,
                    (unsigned long)object->relocations[3].symbol_file_index,
                    (unsigned long)object->relocations[4].offset,
                    (unsigned long)object->relocations[4].type,
                    (long)object->relocations[4].addend,
                    (unsigned long)object->relocations[4].symbol_file_index);
    }
    return 0;
  }
  return decode_function(
             job, text, implemented, implemented_instructions,
             (ctool_u32)(sizeof(implemented_instructions) /
                         sizeof(implemented_instructions[0])),
             implemented_bytes, (ctool_u32)sizeof(implemented_bytes),
             (const ctool_u32 *)0, 0u,
             "implemented") &&
                 decode_function(
                     job, text, helper, helper_instructions,
                     (ctool_u32)(sizeof(helper_instructions) /
                                 sizeof(helper_instructions[0])),
                     helper_bytes, (ctool_u32)sizeof(helper_bytes),
                     helper_branch_targets,
                     (ctool_u32)(sizeof(helper_branch_targets) /
                                 sizeof(helper_branch_targets[0])),
                     "cemit_add_overflows") &&
                 decode_function(
                     job, text, signed_greater, signed_instructions,
                     (ctool_u32)(sizeof(signed_instructions) /
                                 sizeof(signed_instructions[0])),
                     signed_bytes, (ctool_u32)sizeof(signed_bytes),
                     (const ctool_u32 *)0, 0u,
                     "signed_greater") &&
                 decode_function(
                     job, text, idle, idle_instructions,
                     (ctool_u32)(sizeof(idle_instructions) /
                                 sizeof(idle_instructions[0])),
                     idle_bytes, (ctool_u32)sizeof(idle_bytes),
                     (const ctool_u32 *)0, 0u,
                     "idle") &&
                 decode_function(
                     job, text, local_target, local_target_instructions,
                     (ctool_u32)(sizeof(local_target_instructions) /
                                 sizeof(local_target_instructions[0])),
                     local_target_bytes,
                     (ctool_u32)sizeof(local_target_bytes),
                     (const ctool_u32 *)0, 0u, "local_target") &&
                 decode_function(
                     job, text, call_local, call_local_instructions,
                     (ctool_u32)(sizeof(call_local_instructions) /
                                 sizeof(call_local_instructions[0])),
                     call_local_bytes, (ctool_u32)sizeof(call_local_bytes),
                     (const ctool_u32 *)0, 0u, "call_local") &&
                 decode_function(
                     job, text, call_external, call_external_instructions,
                     (ctool_u32)(sizeof(call_external_instructions) /
                                 sizeof(call_external_instructions[0])),
                     call_external_bytes,
                     (ctool_u32)sizeof(call_external_bytes),
                     (const ctool_u32 *)0, 0u, "call_external") &&
                 decode_function(
                     job, text, call_nested, call_nested_instructions,
                     (ctool_u32)(sizeof(call_nested_instructions) /
                                 sizeof(call_nested_instructions[0])),
                     call_nested_bytes,
                     (ctool_u32)sizeof(call_nested_bytes),
                     (const ctool_u32 *)0, 0u, "call_nested") &&
                 decode_function(
                     job, text, call_void, call_void_instructions,
                     (ctool_u32)(sizeof(call_void_instructions) /
                                 sizeof(call_void_instructions[0])),
                     call_void_bytes, (ctool_u32)sizeof(call_void_bytes),
                     (const ctool_u32 *)0, 0u, "call_void") &&
                 decode_function(
                     job, text, add2, add2_instructions,
                     (ctool_u32)(sizeof(add2_instructions) /
                                 sizeof(add2_instructions[0])),
                     add2_bytes, (ctool_u32)sizeof(add2_bytes),
                     (const ctool_u32 *)0, 0u, "add2") &&
                  decode_function(
                      job, text, local_round_trip,
                     local_round_trip_instructions,
                     (ctool_u32)(sizeof(local_round_trip_instructions) /
                                 sizeof(local_round_trip_instructions[0])),
                      local_round_trip_bytes,
                      (ctool_u32)sizeof(local_round_trip_bytes),
                      (const ctool_u32 *)0, 0u, "vga_flip_time_probe") &&
                  decode_function(
                      job, text, local_call, local_call_instructions,
                      (ctool_u32)(sizeof(local_call_instructions) /
                                  sizeof(local_call_instructions[0])),
                      local_call_bytes, (ctool_u32)sizeof(local_call_bytes),
                      (const ctool_u32 *)0, 0u, "local_call_probe") &&
                   decode_function(
                       job, text, uninitialized_local,
                      uninitialized_local_instructions,
                      (ctool_u32)(sizeof(uninitialized_local_instructions) /
                                  sizeof(uninitialized_local_instructions[0])),
                      uninitialized_local_bytes,
                      (ctool_u32)sizeof(uninitialized_local_bytes),
                       (const ctool_u32 *)0, 0u,
                       "uninitialized_local_probe") &&
                   decode_function(
                       job, text, vga_flip_ready,
                       vga_flip_ready_instructions,
                       (ctool_u32)(sizeof(vga_flip_ready_instructions) /
                                   sizeof(vga_flip_ready_instructions[0])),
                       vga_flip_ready_bytes,
                       (ctool_u32)sizeof(vga_flip_ready_bytes),
                       (const ctool_u32 *)0, 0u, "vga_flip_ready") &&
                   decode_function(
                       job, text, read_external_clock,
                       external_clock_instructions,
                       (ctool_u32)(sizeof(external_clock_instructions) /
                                   sizeof(external_clock_instructions[0])),
                       external_clock_bytes,
                       (ctool_u32)sizeof(external_clock_bytes),
                       (const ctool_u32 *)0, 0u,
                       "read_external_clock") &&
                   decode_function(
                       job, text, signed_greater_equal,
                       signed_greater_equal_instructions,
                       (ctool_u32)(sizeof(signed_greater_equal_instructions) /
                                   sizeof(signed_greater_equal_instructions[0])),
                       signed_greater_equal_bytes,
                       (ctool_u32)sizeof(signed_greater_equal_bytes),
                       (const ctool_u32 *)0, 0u,
                       "signed_greater_equal") &&
                   decode_function(
                       job, text, power_of_two, power_of_two_instructions,
                       (ctool_u32)(sizeof(power_of_two_instructions) /
                                   sizeof(power_of_two_instructions[0])),
                       power_of_two_bytes,
                       (ctool_u32)sizeof(power_of_two_bytes),
                       power_of_two_branch_targets,
                       (ctool_u32)(sizeof(power_of_two_branch_targets) /
                                   sizeof(power_of_two_branch_targets[0])),
                       "cemit_power_of_two") &&
                   decode_function(
                       job, text, bool_valid, bool_valid_instructions,
                       (ctool_u32)(sizeof(bool_valid_instructions) /
                                   sizeof(bool_valid_instructions[0])),
                       bool_valid_bytes,
                       (ctool_u32)sizeof(bool_valid_bytes),
                       bool_valid_branch_targets,
                       (ctool_u32)(sizeof(bool_valid_branch_targets) /
                                   sizeof(bool_valid_branch_targets[0])),
                       "cfront_bool_valid")
             ? 1
             : 0;
}

static int validate_object(const ctool_elf32_object_t *object) {
  static const char *const section_names[] = {
      "", ".data", ".bss", ".rel.data", ".symtab", ".strtab", ".shstrtab"};
  static const char *const symbol_names[] = {
      "",           "local_word", "hidden_zero",     "imported",
      "callback",   "message",    "common_zero",     "imported_pointer",
      "hook"};
  static const ctool_u8 expected_data[] = {
      0x44u, 0x33u, 0x22u, 0x11u, 0x6fu, 0x6bu, 0x00u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
  const ctool_elf32_section_t *data = find_section(object, ".data");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_data = find_section(object, ".rel.data");
  const ctool_elf32_symbol_t *local_word = find_symbol(object, "local_word");
  const ctool_elf32_symbol_t *hidden_zero =
      find_symbol(object, "hidden_zero");
  const ctool_elf32_symbol_t *imported = find_symbol(object, "imported");
  const ctool_elf32_symbol_t *callback = find_symbol(object, "callback");
  const ctool_elf32_symbol_t *message = find_symbol(object, "message");
  const ctool_elf32_symbol_t *common_zero =
      find_symbol(object, "common_zero");
  const ctool_elf32_symbol_t *imported_pointer =
      find_symbol(object, "imported_pointer");
  const ctool_elf32_symbol_t *hook = find_symbol(object, "hook");
  ctool_u32 index;

  if (object->file_type != CTOOL_ELF32_ET_REL || object->entry_point != 0u ||
      object->flags != 0u || object->program_header_count != 0u ||
      object->program_headers != (const ctool_elf32_program_header_t *)0 ||
      object->section_count != 7u || object->symbol_count != 9u ||
      object->relocation_count != 2u) {
    (void)fprintf(stderr, "ELF32 object inventory differs\n");
    return 0;
  }
  for (index = 0u; index < 7u; index++) {
    if (object->sections[index].file_index != index ||
        string_equal(object->sections[index].name, section_names[index]) == 0) {
      (void)fprintf(stderr, "ELF32 section order differs at index %u\n", index);
      return 0;
    }
  }
  if (find_section(object, ".rodata") !=
          (const ctool_elf32_section_t *)0 ||
      data == (const ctool_elf32_section_t *)0 ||
      data->type != CTOOL_ELF32_SHT_PROGBITS ||
      data->flags != (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE) ||
      data->alignment != 4u || data->entry_size != 0u || data->size != 16u ||
      data->contents.size != (ctool_u32)sizeof(expected_data) ||
      memcmp(data->contents.data, expected_data, sizeof(expected_data)) != 0 ||
      data->relocation_first != 0u || data->relocation_count != 2u ||
      bss == (const ctool_elf32_section_t *)0 ||
      bss->type != CTOOL_ELF32_SHT_NOBITS ||
      bss->flags != (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE) ||
      bss->alignment != 4u || bss->entry_size != 0u || bss->size != 8u ||
      bss->contents.size != 0u || bss->relocation_count != 0u ||
      rel_data == (const ctool_elf32_section_t *)0 ||
      rel_data->file_index != 3u) {
    (void)fprintf(stderr, "ELF32 static storage differs\n");
    return 0;
  }

  for (index = 0u; index < 9u; index++) {
    if (object->symbols[index].file_index != index ||
        string_equal(object->symbols[index].name, symbol_names[index]) == 0) {
      (void)fprintf(stderr, "ELF32 symbol order differs at index %u\n", index);
      return 0;
    }
  }
  if (!symbol_matches(local_word, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 0u, 4u) ||
      !symbol_matches(hidden_zero, 2u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u, 4u) ||
      !symbol_matches(imported, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION, 0u,
                      0u) ||
      !symbol_matches(callback, 4u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION, 0u,
                      0u) ||
      !symbol_matches(message, 5u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 4u, 4u) ||
      !symbol_matches(common_zero, 6u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 4u, 4u) ||
      !symbol_matches(imported_pointer, 7u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 8u, 4u) ||
      !symbol_matches(hook, 8u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 12u, 4u)) {
    (void)fprintf(stderr, "ELF32 symbol semantics differ\n");
    return 0;
  }
  if (object->symbols[0].binding != CTOOL_ELF32_BIND_LOCAL ||
      object->symbols[0].type != CTOOL_ELF32_SYMBOL_NOTYPE ||
      object->symbols[0].placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      object->symbols[0].section_file_index != CTOOL_ELF32_NO_SECTION) {
    (void)fprintf(stderr, "ELF32 null symbol differs\n");
    return 0;
  }

  if (object->relocations[0].relocation_section_file_index !=
          rel_data->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != data->file_index ||
      object->relocations[0].offset != 8u ||
      object->relocations[0].symbol_file_index != imported->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != 0 ||
      object->relocations[1].relocation_section_file_index !=
          rel_data->file_index ||
      object->relocations[1].entry_index != 1u ||
      object->relocations[1].target_section_file_index != data->file_index ||
      object->relocations[1].offset != 12u ||
      object->relocations[1].symbol_file_index != callback->file_index ||
      object->relocations[1].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[1].addend_known != CTOOL_TRUE ||
      object->relocations[1].addend != 0) {
    (void)fprintf(stderr, "ELF32 static relocations differ\n");
    return 0;
  }
  return 1;
}

static int validate_layout_object(const ctool_elf32_object_t *object) {
  static const char *const section_names[] = {
      "",          ".rodata", ".data",   ".bss",
      ".rel.data", ".symtab", ".strtab", ".shstrtab"};
  static const char *const symbol_names[] = {
      "",                "const_word",      "const_record",
      "local_data",      "local_zero",      "masked_zero",
      "data_pointer",    "zero_pointer",    "array_data",
      "array_second",    "array_offset",    ".LC0",
      "const_text",      "literal_pointer", "holder"};
  static const ctool_u8 expected_rodata[] = {
      0xd4u, 0xc3u, 0xb2u, 0xa1u, 0x78u, 0x79u, 0x00u,
      0x00u, 0x5au, 0x8du, 0x00u, 0x00u, 0x44u, 0x33u,
      0x22u, 0x11u, 0x68u, 0x69u, 0x00u};
  static const ctool_u8 expected_data[] = {
      0x07u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x09u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x01u, 0x00u, 0x00u, 0x00u, 0x02u, 0x00u, 0x00u, 0x00u,
      0x03u, 0x00u, 0x00u, 0x00u, 0x04u, 0x00u, 0x00u, 0x00u,
      0x08u, 0x00u, 0x00u, 0x00u};
  const ctool_elf32_section_t *rodata = find_section(object, ".rodata");
  const ctool_elf32_section_t *data = find_section(object, ".data");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_data = find_section(object, ".rel.data");
  const ctool_elf32_symbol_t *const_word =
      find_symbol(object, "const_word");
  const ctool_elf32_symbol_t *const_text =
      find_symbol(object, "const_text");
  const ctool_elf32_symbol_t *const_record =
      find_symbol(object, "const_record");
  const ctool_elf32_symbol_t *local_data =
      find_symbol(object, "local_data");
  const ctool_elf32_symbol_t *local_zero =
      find_symbol(object, "local_zero");
  const ctool_elf32_symbol_t *masked_zero =
      find_symbol(object, "masked_zero");
  const ctool_elf32_symbol_t *literal_pointer =
      find_symbol(object, "literal_pointer");
  const ctool_elf32_symbol_t *data_pointer =
      find_symbol(object, "data_pointer");
  const ctool_elf32_symbol_t *zero_pointer =
      find_symbol(object, "zero_pointer");
  const ctool_elf32_symbol_t *array_data =
      find_symbol(object, "array_data");
  const ctool_elf32_symbol_t *array_second =
      find_symbol(object, "array_second");
  const ctool_elf32_symbol_t *array_offset =
      find_symbol(object, "array_offset");
  const ctool_elf32_symbol_t *holder = find_symbol(object, "holder");
  const ctool_elf32_symbol_t *literal = find_symbol(object, ".LC0");
  ctool_u32 index;

  if (object->file_type != CTOOL_ELF32_ET_REL || object->entry_point != 0u ||
      object->flags != 0u || object->program_header_count != 0u ||
      object->program_headers != (const ctool_elf32_program_header_t *)0 ||
      object->section_count != 8u || object->symbol_count != 15u ||
      object->relocation_count != 6u ||
      object->symbol_table_section_file_index != 5u) {
    (void)fprintf(stderr, "source-derived ELF32 inventory differs\n");
    return 0;
  }
  for (index = 0u; index < 8u; index++) {
    if (object->sections[index].file_index != index ||
        string_equal(object->sections[index].name, section_names[index]) == 0) {
      (void)fprintf(stderr,
                    "source-derived section order differs at index %u\n",
                    index);
      return 0;
    }
  }
  if (rodata == (const ctool_elf32_section_t *)0 ||
      rodata->type != CTOOL_ELF32_SHT_PROGBITS ||
      rodata->flags != CTOOL_ELF32_SHF_ALLOC || rodata->alignment != 4u ||
      rodata->entry_size != 0u || rodata->size != 19u ||
      rodata->contents.size != (ctool_u32)sizeof(expected_rodata) ||
      memcmp(rodata->contents.data, expected_rodata,
             sizeof(expected_rodata)) != 0 ||
      rodata->relocation_count != 0u ||
      data == (const ctool_elf32_section_t *)0 ||
      data->type != CTOOL_ELF32_SHT_PROGBITS ||
      data->flags != (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE) ||
      data->alignment != 4u || data->entry_size != 0u || data->size != 44u ||
      data->contents.size != (ctool_u32)sizeof(expected_data) ||
      memcmp(data->contents.data, expected_data, sizeof(expected_data)) != 0 ||
      data->relocation_first != 0u || data->relocation_count != 6u ||
      bss == (const ctool_elf32_section_t *)0 ||
      bss->type != CTOOL_ELF32_SHT_NOBITS ||
      bss->flags != (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE) ||
      bss->alignment != 4u || bss->entry_size != 0u || bss->size != 8u ||
      bss->contents.size != 0u || bss->relocation_count != 0u ||
      rel_data == (const ctool_elf32_section_t *)0 ||
      rel_data->file_index != 4u) {
    (void)fprintf(stderr, "source-derived static storage differs\n");
    return 0;
  }

  for (index = 0u; index < 15u; index++) {
    if (object->symbols[index].file_index != index ||
        string_equal(object->symbols[index].name, symbol_names[index]) == 0) {
      (void)fprintf(stderr,
                    "source-derived symbol order differs at index %u\n",
                    index);
      return 0;
    }
  }
  if (!symbol_matches(const_word, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, rodata->file_index, 0u, 4u) ||
      !symbol_matches(const_record, 2u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, rodata->file_index, 8u, 8u) ||
      !symbol_matches(local_data, 3u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 0u, 4u) ||
      !symbol_matches(local_zero, 4u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u, 4u) ||
      !symbol_matches(masked_zero, 5u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 4u, 4u) ||
      !symbol_matches(data_pointer, 6u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 8u, 4u) ||
      !symbol_matches(zero_pointer, 7u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 12u, 4u) ||
      !symbol_matches(array_data, 8u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 24u, 12u) ||
      !symbol_matches(array_second, 9u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 36u, 4u) ||
      !symbol_matches(array_offset, 10u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 40u, 4u) ||
      !symbol_matches(literal, 11u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, rodata->file_index, 16u, 3u) ||
      !symbol_matches(const_text, 12u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, rodata->file_index, 4u, 4u) ||
      !symbol_matches(literal_pointer, 13u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 4u, 4u) ||
      !symbol_matches(holder, 14u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 16u, 8u)) {
    (void)fprintf(stderr, "source-derived symbol semantics differ\n");
    return 0;
  }
  if (object->symbols[0].binding != CTOOL_ELF32_BIND_LOCAL ||
      object->symbols[0].type != CTOOL_ELF32_SYMBOL_NOTYPE ||
      object->symbols[0].placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      object->symbols[0].section_file_index != CTOOL_ELF32_NO_SECTION) {
    (void)fprintf(stderr, "source-derived null symbol differs\n");
    return 0;
  }

  if (object->relocations[0].relocation_section_file_index !=
          rel_data->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != data->file_index ||
      object->relocations[0].offset != 4u ||
      object->relocations[0].symbol_file_index != literal->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != 0 ||
      object->relocations[1].relocation_section_file_index !=
          rel_data->file_index ||
      object->relocations[1].entry_index != 1u ||
      object->relocations[1].target_section_file_index != data->file_index ||
      object->relocations[1].offset != 8u ||
      object->relocations[1].symbol_file_index != local_data->file_index ||
      object->relocations[1].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[1].addend_known != CTOOL_TRUE ||
      object->relocations[1].addend != 0 ||
      object->relocations[2].relocation_section_file_index !=
          rel_data->file_index ||
      object->relocations[2].entry_index != 2u ||
      object->relocations[2].target_section_file_index != data->file_index ||
      object->relocations[2].offset != 12u ||
      object->relocations[2].symbol_file_index != local_zero->file_index ||
      object->relocations[2].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[2].addend_known != CTOOL_TRUE ||
      object->relocations[2].addend != 0 ||
      object->relocations[3].relocation_section_file_index !=
          rel_data->file_index ||
      object->relocations[3].entry_index != 3u ||
      object->relocations[3].target_section_file_index != data->file_index ||
      object->relocations[3].offset != 20u ||
      object->relocations[3].symbol_file_index != local_data->file_index ||
      object->relocations[3].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[3].addend_known != CTOOL_TRUE ||
      object->relocations[3].addend != 0 ||
      object->relocations[4].relocation_section_file_index !=
          rel_data->file_index ||
      object->relocations[4].entry_index != 4u ||
      object->relocations[4].target_section_file_index != data->file_index ||
      object->relocations[4].offset != 36u ||
      object->relocations[4].symbol_file_index != array_data->file_index ||
      object->relocations[4].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[4].addend_known != CTOOL_TRUE ||
      object->relocations[4].addend != 4 ||
      object->relocations[5].relocation_section_file_index !=
          rel_data->file_index ||
      object->relocations[5].entry_index != 5u ||
      object->relocations[5].target_section_file_index != data->file_index ||
      object->relocations[5].offset != 40u ||
      object->relocations[5].symbol_file_index != array_data->file_index ||
      object->relocations[5].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[5].addend_known != CTOOL_TRUE ||
      object->relocations[5].addend != 8) {
    (void)fprintf(stderr, "source-derived static relocations differ\n");
    return 0;
  }
  return 1;
}

static int run_static_data(const char *host_root) {
  static const char source_text[] =
      "extern int imported;\n"
      "extern void callback(void);\n"
      "static unsigned local_word = 0x11223344u;\n"
      "char message[4] = \"ok\";\n"
      "static int hidden_zero;\n"
      "int common_zero;\n"
      "int *imported_pointer = &imported;\n"
      "void (*hook)(void) = callback;\n";
  static const char function_text[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef int ctool_bool;\n"
      "#define CTOOL_FALSE 0\n"
      "#define CTOOL_TRUE 1\n"
      "int function_data = 7;\n"
      "int implemented(void) { return 42; }\n"
      "static ctool_bool cemit_add_overflows(ctool_u32 left, "
      "ctool_u32 right) {\n"
      "  return left > 0xffffffffu - right ? CTOOL_TRUE : CTOOL_FALSE;\n"
      "}\n"
      "int signed_greater(int left, int right) { return left > right; }\n"
      "static void idle(void) {}\n"
      "static int local_target(void) { return 9; }\n"
      "int call_local(void) { return local_target(); }\n"
      "extern int external_sum(int left, int right);\n"
      "int call_external(int left, int right) {\n"
      "  return external_sum(left, right);\n"
      "}\n"
      "extern int external_three(int first, int second, int third);\n"
      "int call_nested(int left, int middle, int right) {\n"
      "  return external_three(left, call_local(), right);\n"
      "}\n"
      "extern void external_sink(int value);\n"
      "void call_void(int value) { external_sink(value); }\n"
      "int add2(int x, int y) {\n"
      "    return x + y;\n"
      "}\n"
      "typedef unsigned int uint32_t;\n"
      "typedef enum { false = 0, true = 1 } bool;\n"
      "extern uint32_t timer_get_uptime_ms(void);\n"
      "uint32_t vga_flip_time_probe(uint32_t prior_value) {\n"
      "  uint32_t now = timer_get_uptime_ms();\n"
      "  register uint32_t prior = prior_value;\n"
      "  auto uint32_t unused;\n"
      "  return now + prior;\n"
      "}\n"
      "extern int initializer_sum(int left, int right);\n"
      "int local_call_probe(int prior_value) {\n"
      "  int value = initializer_sum(prior_value, 2);\n"
      "  return value;\n"
      "}\n"
      "int uninitialized_local_probe(void) {\n"
      "  auto int value;\n"
      "  return value;\n"
      "}\n"
      "static uint32_t last_flip_ms = 0;\n"
      "bool vga_flip_ready(void) {\n"
      "  uint32_t now = timer_get_uptime_ms();\n"
      "  return (now - last_flip_ms) >= 16u;\n"
      "}\n"
      "extern uint32_t external_clock;\n"
      "uint32_t read_external_clock(void) { return external_clock; }\n"
      "int signed_greater_equal(int left, int right) {\n"
      "  return left >= right;\n"
      "}\n"
      "static ctool_bool cemit_power_of_two(ctool_u32 value) {\n"
      "  return value != 0u && (value & (value - 1u)) == 0u ? CTOOL_TRUE\n"
      "                                                     : CTOOL_FALSE;\n"
      "}\n"
      "static ctool_bool cfront_bool_valid(ctool_bool value) {\n"
      "  return value == CTOOL_FALSE || value == CTOOL_TRUE ? CTOOL_TRUE\n"
      "                                                      : CTOOL_FALSE;\n"
      "}\n";
  static const char unsupported_function_text[] =
      "int unsupported(void) { return 1; }\n";
  static const char wide_selection_text[] =
      "int choose_wide(void) {\n"
      "  if (1LL) return 1;\n"
      "  return 0;\n"
      "}\n";
  static const char selection_edge_text[] =
      "int unreachable_tail(int value) {\n"
      "  return 0;\n"
      "  if (value) return 1;\n"
      "}\n"
      "void maybe_return(int value) {\n"
      "  if (value) return;\n"
      "}\n";
  static const char nonvoid_selection_fallthrough_text[] =
      "int maybe_value(int value) {\n"
      "  if (value) return 1;\n"
      "}\n";
  static const char while_text[] =
      "typedef unsigned int uint32_t;\n"
      "uint32_t timer_get_uptime_ms(void);\n"
      "void process_yield(void);\n"
      "static void syscall_sleep_ms(uint32_t ms) {\n"
      "  uint32_t start = timer_get_uptime_ms();\n"
      "  while ((timer_get_uptime_ms() - start) < ms) {\n"
      "    process_yield();\n"
      "  }\n"
      "}\n";
  static const char wide_while_text[] =
      "void wait_wide(void) { while (1LL) {} }\n";
  static const char wide_do_text[] =
      "void do_wide(void) { do {} while (1LL); }\n";
  static const char for_text[] =
      "static int url_hash_loop(void) {\n"
      "  int i;\n"
      "  for (i = 0; i < 8; i = i + 1) {\n"
      "    i;\n"
      "  }\n"
      "  return i;\n"
      "}\n"
      "void break_loop(int value) {\n"
      "  for (;;) {\n"
      "    if (value) break;\n"
      "  }\n"
      "}\n"
      "void break_do(int value) {\n"
      "  do { break; } while (value);\n"
      "}\n"
      "void continue_while(int value) {\n"
      "  while (value) { continue; }\n"
      "}\n"
      "void continue_do(int value) {\n"
      "  do { continue; } while (value);\n"
      "}\n"
      "void continue_for(int value) {\n"
      "  for (; value; value = value - 1) { continue; }\n"
      "}\n"
      "void continue_for_no_iteration(int value) {\n"
      "  for (; value;) { continue; }\n"
      "}\n"
      "void nested_continue(int outer, int inner) {\n"
      "  while (outer) {\n"
      "    while (inner) { continue; }\n"
      "    continue;\n"
      "  }\n"
      "}\n"
      "void nested_break(int outer, int inner) {\n"
      "  while (outer) {\n"
      "    while (inner) { break; }\n"
      "    break;\n"
      "  }\n"
      "}\n";
  static const char wide_for_text[] =
      "void for_wide(void) { for (; 1LL;) {} }\n";
  static const char terminal_wide_for_iteration_text[] =
      "void terminal_for_wide(long long *value) {\n"
      "  for (; 1; *value) return;\n"
      "}\n";
  static const char declaration_for_text[] =
      "void declaration_for(void) {\n"
      "  for (int i = 0; i < 1; i = i + 1) {}\n"
      "}\n"
      "int nested_declaration(int value) {\n"
      "  if (value) {\n"
      "    int copy = value;\n"
      "    return copy;\n"
      "  } else {\n"
      "    int zero = 0;\n"
      "    return zero;\n"
      "  }\n"
      "}\n"
      "int unreachable_declaration(void) {\n"
      "  return 0;\n"
      "  int value;\n"
      "}\n"
      "void loop_declarations(int value) {\n"
      "  while (value) { int in_while; break; }\n"
      "  do { int in_do; break; } while (value);\n"
      "  for (; value;) { int in_for; break; }\n"
      "}\n";
  static const char wide_declaration_for_text[] =
      "void wide_declaration_for(void) {\n"
      "  for (long long i = 0LL;;) { (void)i; break; }\n"
      "}\n";
  static const char unreachable_wide_declaration_text[] =
      "int unreachable_wide_declaration(void) {\n"
      "  return 0;\n"
      "  long long value;\n"
      "}\n";
  static const char integer_unary_text[] =
      "int unary_plus(int value) { return +value; }\n"
      "int signed_negate(int value) { return -value; }\n"
      "unsigned int unsigned_negate(unsigned int value) { return -value; }\n"
      "int logical_not(unsigned int value) { return !value; }\n";
  static const char wide_logical_not_text[] =
      "int wide_logical_not(void) { return !1LL; }\n";
  static const char multiplication_text[] =
      "int CANVAS_X = 56;\n"
      "int CANVAS_Y = 20;\n"
      "int zoom_level = 1;\n"
      "int view_x = 0;\n"
      "int view_y = 0;\n"
      "int canvas_to_screen_x(int cx) {\n"
      "  return CANVAS_X + (cx - view_x) * zoom_level;\n"
      "}\n"
      "int canvas_to_screen_y(int cy) {\n"
      "  return CANVAS_Y + (cy - view_y) * zoom_level;\n"
      "}\n";
  static const char unsigned_multiplication_text[] =
      "unsigned int multiply_unsigned(unsigned int value) {\n"
      "  return value * 0x80000001u;\n"
      "}\n";
  static const char division_text[] =
      "int signed_divide(int left, int right) { return left / right; }\n"
      "int signed_remainder(int left, int right) { return left % right; }\n"
      "unsigned int unsigned_divide(unsigned int left, "
      "unsigned int right) {\n"
      "  return left / right;\n"
      "}\n"
      "unsigned int unsigned_remainder(unsigned int left, "
      "unsigned int right) {\n"
      "  return left % right;\n"
      "}\n";
  static const char branch_fit_text[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef int ctool_bool;\n"
      "#define CTOOL_FALSE 0\n"
      "#define CTOOL_TRUE 1\n"
      "static ctool_bool asm_branch_fits_i8(ctool_u32 bits) {\n"
      "  return bits <= 0x7fu || bits >= 0xffffff80u ? CTOOL_TRUE : "
      "CTOOL_FALSE;\n"
      "}\n"
      "int signed_less(int left, int right) { return left < right; }\n"
      "int signed_less_equal(int left, int right) { return left <= right; }\n"
      "int unsigned_less(unsigned int left, unsigned int right) {\n"
      "  return left < right;\n"
      "}\n";
  static const char aes_rotw_text[] =
      "typedef unsigned int uint32_t;\n"
      "static uint32_t rotw(uint32_t w) { return (w << 8) | (w >> 24); }\n"
      "int signed_right_shift(int value, int count) { return value >> count; }\n";
  static const char simd_cpuid_text[] =
      "typedef unsigned int uint32_t;\n"
      "typedef enum { false = 0, true = 1 } bool;\n"
      "static bool simd_cpuid_changed(uint32_t before, uint32_t after) {\n"
      "    return ((before ^ after) & (1u << 21)) != 0u;\n"
      "}\n";
  static const char file_assignment_text[] =
      "typedef enum { false = 0, true = 1 } bool;\n"
      "static bool vga_wait_vsync = false;\n"
      "void vga_set_vsync_wait(bool enabled) { vga_wait_vsync = enabled; }\n";
  static const char file_member_text[] =
      "typedef unsigned int uint32_t;\n"
      "typedef unsigned long long uint64_t;\n"
      "typedef enum { false = 0, true = 1 } bool;\n"
      "typedef struct {\n"
      "  uint64_t ticks;\n"
      "  uint32_t frequency;\n"
      "  uint32_t ms_per_tick;\n"
      "  bool is_calibrated;\n"
      "} timer_state_t;\n"
      "static timer_state_t timer_state = {\n"
      "    .ticks = 0,\n"
      "    .frequency = 0,\n"
      "    .ms_per_tick = 0,\n"
      "    .is_calibrated = false\n"
      "};\n"
      "uint32_t timer_get_frequency(void) {\n"
      "    return timer_state.frequency;\n"
      "}\n";
  static const char bit_field_text[] =
      "typedef unsigned int uint32_t;\n"
      "struct color {\n"
      "  uint32_t b : 8;\n"
      "  uint32_t g : 8;\n"
      "  uint32_t r : 8;\n"
      "  uint32_t a : 8;\n"
      "};\n"
      "static volatile struct color color_state;\n"
      "struct signed_flags {\n"
      "  unsigned int word;\n"
      "  unsigned int prefix : 3;\n"
      "  signed int delta : 5;\n"
      "  unsigned int whole : 32;\n"
      "};\n"
      "static struct signed_flags signed_state;\n"
      "uint32_t read_red(void) { return color_state.r; }\n"
      "int read_delta(void) { return signed_state.delta; }\n"
      "unsigned int read_whole(void) { return signed_state.whole; }\n";
  static const char chained_assignment_text[] =
      "int first_state;\n"
      "int second_state;\n"
      "int set_both(int value) { return first_state = second_state = value; }\n";
  static const char external_inline_text[] =
      "inline int external_inline(void) { return 1; }\n";
  static const char external_object_text[] =
      "extern unsigned int external_clock;\n"
      "unsigned int read_external_clock(void) { return external_clock; }\n";
  static const char layout_text[] =
      "typedef struct {\n"
      "  unsigned char tag;\n"
      "  unsigned low : 3;\n"
      "  unsigned high : 5;\n"
      "  unsigned value;\n"
      "} const_record_t;\n"
      "typedef struct { int count; int *pointer; } holder_t;\n"
      "typedef struct { unsigned value : 3; } masked_t;\n"
      "static const unsigned const_word = 0xa1b2c3d4u;\n"
      "const char const_text[4] = \"xy\";\n"
      "static const const_record_t const_record = {\n"
      "    0x5a, 5u, 17u, 0x11223344u};\n"
      "static int local_data = 7;\n"
      "static int local_zero;\n"
      "static masked_t masked_zero = {8u};\n"
      "char *literal_pointer = \"hi\";\n"
      "static int *data_pointer = &local_data;\n"
      "static int *zero_pointer = &local_zero;\n"
      "holder_t holder = {9, &local_data};\n"
      "static int array_data[3] = {1, 2, 3};\n"
      "static int *array_second = &array_data[1];\n"
      "static int *array_offset = array_data + 2;\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t function_unit;
  ctool_c_translation_unit_t external_object_unit;
  ctool_c_translation_unit_t multiplication_unit;
  ctool_c_translation_unit_t unsigned_multiplication_unit;
  ctool_c_translation_unit_t division_unit;
  ctool_c_translation_unit_t branch_fit_unit;
  ctool_c_translation_unit_t aes_rotw_unit;
  ctool_c_translation_unit_t align_up_unit;
  ctool_c_translation_unit_t integer_unary_unit;
  ctool_c_translation_unit_t integer_cast_unit;
  ctool_c_translation_unit_t signed_bits_unit;
  ctool_c_translation_unit_t simd_cpuid_unit;
  ctool_c_translation_unit_t file_assignment_unit;
  ctool_c_translation_unit_t file_member_unit;
  ctool_c_translation_unit_t bit_field_unit;
  ctool_c_translation_unit_t chained_assignment_unit;
  ctool_c_translation_unit_t unsupported_function_unit;
  ctool_c_translation_unit_t selection_edge_unit;
  ctool_c_translation_unit_t while_unit;
  ctool_c_translation_unit_t wide_while_unit;
  ctool_c_translation_unit_t do_unit;
  ctool_c_translation_unit_t wide_do_unit;
  ctool_c_translation_unit_t for_unit;
  ctool_c_translation_unit_t wide_for_unit;
  ctool_c_translation_unit_t terminal_wide_for_iteration_unit;
  ctool_c_translation_unit_t declaration_for_unit;
  ctool_c_translation_unit_t wide_declaration_for_unit;
  ctool_c_translation_unit_t unreachable_wide_declaration_unit;
  ctool_c_translation_unit_t nonvoid_selection_fallthrough_unit;
  ctool_c_translation_unit_t wide_selection_unit;
  ctool_c_translation_unit_t wide_logical_not_unit;
  ctool_c_translation_unit_t external_inline_unit;
  ctool_c_translation_unit_t layout_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_object_definition_t *invalid_definitions = NULL;
  ctool_c_initializer_t *invalid_initializers = NULL;
  ctool_c_block_binding_t *invalid_block_bindings = NULL;
  ctool_c_binding_t *invalid_bindings = NULL;
  ctool_c_initializer_t *invalid_layout_initializers = NULL;
  ctool_c_initializer_element_t *invalid_elements = NULL;
  ctool_c_statement_t *lexical_declaration_statements = NULL;
  ctool_c_statement_t *unreachable_statements = NULL;
  ctool_c_statement_t *loop_control_statements = NULL;
  ctool_c_expression_t *unsupported_expressions = NULL;
  ctool_c_expression_t invalid_expression;
  unit_snapshot_t snapshot;
  unit_snapshot_t function_snapshot;
  unit_snapshot_t external_object_snapshot;
  unit_snapshot_t multiplication_snapshot;
  unit_snapshot_t unsigned_multiplication_snapshot;
  unit_snapshot_t division_snapshot;
  unit_snapshot_t branch_fit_snapshot;
  unit_snapshot_t aes_rotw_snapshot;
  unit_snapshot_t align_up_snapshot;
  unit_snapshot_t integer_unary_snapshot;
  unit_snapshot_t integer_cast_snapshot;
  unit_snapshot_t signed_bits_snapshot;
  unit_snapshot_t selection_edge_snapshot;
  unit_snapshot_t while_snapshot;
  unit_snapshot_t do_snapshot;
  unit_snapshot_t for_snapshot;
  unit_snapshot_t declaration_for_snapshot;
  unit_snapshot_t simd_cpuid_snapshot;
  unit_snapshot_t file_assignment_snapshot;
  unit_snapshot_t file_member_snapshot;
  unit_snapshot_t bit_field_snapshot;
  unit_snapshot_t chained_assignment_snapshot;
  unit_snapshot_t layout_snapshot;
  ctool_u8 *expected_object = NULL;
  ctool_u32 expected_object_size = 0u;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_arena_mark_t mark;
  ctool_bytes_t bytes;
  ctool_bytes_t layout_bytes;
  ctool_u8 *function_object = NULL;
  ctool_u32 function_object_size = 0u;
  ctool_u8 *multiplication_object = NULL;
  ctool_u32 multiplication_object_size = 0u;
  ctool_u8 *division_object = NULL;
  ctool_u32 division_object_size = 0u;
  ctool_u8 *branch_fit_object = NULL;
  ctool_u32 branch_fit_object_size = 0u;
  ctool_u8 *aes_rotw_object = NULL;
  ctool_u32 aes_rotw_object_size = 0u;
  ctool_u8 *align_up_object = NULL;
  ctool_u32 align_up_object_size = 0u;
  ctool_u8 *integer_unary_object = NULL;
  ctool_u32 integer_unary_object_size = 0u;
  ctool_u8 *integer_cast_object = NULL;
  ctool_u32 integer_cast_object_size = 0u;
  ctool_u8 *signed_bits_object = NULL;
  ctool_u32 signed_bits_object_size = 0u;
  ctool_u8 *while_object = NULL;
  ctool_u32 while_object_size = 0u;
  ctool_u8 *do_object = NULL;
  ctool_u32 do_object_size = 0u;
  ctool_u8 *for_object = NULL;
  ctool_u32 for_object_size = 0u;
  ctool_u8 *declaration_for_object = NULL;
  ctool_u32 declaration_for_object_size = 0u;
  ctool_u8 *simd_cpuid_object = NULL;
  ctool_u32 simd_cpuid_object_size = 0u;
  ctool_u8 *file_member_object = NULL;
  ctool_u32 file_member_object_size = 0u;
  ctool_u8 *bit_field_object = NULL;
  ctool_u32 bit_field_object_size = 0u;
  ctool_u8 *chained_assignment_object = NULL;
  ctool_u32 chained_assignment_object_size = 0u;
  ctool_status_t status;
  size_t invalid_binding_bytes;
  size_t invalid_definition_bytes;
  size_t invalid_initializer_bytes;
  size_t invalid_layout_initializer_bytes;
  size_t invalid_element_bytes;
  ctool_u32 diagnostic_count;
  ctool_u32 definition_index;
  ctool_u32 duplicate_initializer = CTOOL_C_AST_NONE;
  ctool_u32 initializer_index;
  ctool_u32 expression_index;
  ctool_u32 statement_index;
  ctool_u32 masked_child = CTOOL_C_AST_NONE;
  ctool_u32 masked_edge = CTOOL_C_AST_NONE;
  ctool_u32 masked_initializer = CTOOL_C_AST_NONE;
  ctool_u32 wrong_child_type = CTOOL_C_TYPE_NONE;
  ctool_u32 unreachable_statement = CTOOL_C_AST_NONE;
  char *align_up_text = NULL;
  char *integer_cast_text = NULL;
  char *signed_bits_text = NULL;
  char *do_text = NULL;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&function_unit, 0, sizeof(function_unit));
  (void)memset(&external_object_unit, 0, sizeof(external_object_unit));
  (void)memset(&multiplication_unit, 0, sizeof(multiplication_unit));
  (void)memset(&unsigned_multiplication_unit, 0,
               sizeof(unsigned_multiplication_unit));
  (void)memset(&division_unit, 0, sizeof(division_unit));
  (void)memset(&branch_fit_unit, 0, sizeof(branch_fit_unit));
  (void)memset(&aes_rotw_unit, 0, sizeof(aes_rotw_unit));
  (void)memset(&align_up_unit, 0, sizeof(align_up_unit));
  (void)memset(&integer_unary_unit, 0, sizeof(integer_unary_unit));
  (void)memset(&integer_cast_unit, 0, sizeof(integer_cast_unit));
  (void)memset(&signed_bits_unit, 0, sizeof(signed_bits_unit));
  (void)memset(&simd_cpuid_unit, 0, sizeof(simd_cpuid_unit));
  (void)memset(&file_assignment_unit, 0, sizeof(file_assignment_unit));
  (void)memset(&file_member_unit, 0, sizeof(file_member_unit));
  (void)memset(&bit_field_unit, 0, sizeof(bit_field_unit));
  (void)memset(&chained_assignment_unit, 0,
               sizeof(chained_assignment_unit));
  (void)memset(&unsupported_function_unit, 0,
               sizeof(unsupported_function_unit));
  (void)memset(&selection_edge_unit, 0, sizeof(selection_edge_unit));
  (void)memset(&while_unit, 0, sizeof(while_unit));
  (void)memset(&wide_while_unit, 0, sizeof(wide_while_unit));
  (void)memset(&do_unit, 0, sizeof(do_unit));
  (void)memset(&wide_do_unit, 0, sizeof(wide_do_unit));
  (void)memset(&for_unit, 0, sizeof(for_unit));
  (void)memset(&wide_for_unit, 0, sizeof(wide_for_unit));
  (void)memset(&terminal_wide_for_iteration_unit, 0,
               sizeof(terminal_wide_for_iteration_unit));
  (void)memset(&declaration_for_unit, 0, sizeof(declaration_for_unit));
  (void)memset(&wide_declaration_for_unit, 0,
               sizeof(wide_declaration_for_unit));
  (void)memset(&unreachable_wide_declaration_unit, 0,
               sizeof(unreachable_wide_declaration_unit));
  (void)memset(&nonvoid_selection_fallthrough_unit, 0,
               sizeof(nonvoid_selection_fallthrough_unit));
  (void)memset(&wide_selection_unit, 0, sizeof(wide_selection_unit));
  (void)memset(&wide_logical_not_unit, 0,
               sizeof(wide_logical_not_unit));
  (void)memset(&external_inline_unit, 0, sizeof(external_inline_unit));
  (void)memset(&layout_unit, 0, sizeof(layout_unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  (void)memset(&function_snapshot, 0, sizeof(function_snapshot));
  (void)memset(&external_object_snapshot, 0,
               sizeof(external_object_snapshot));
  (void)memset(&multiplication_snapshot, 0,
               sizeof(multiplication_snapshot));
  (void)memset(&unsigned_multiplication_snapshot, 0,
               sizeof(unsigned_multiplication_snapshot));
  (void)memset(&division_snapshot, 0, sizeof(division_snapshot));
  (void)memset(&branch_fit_snapshot, 0, sizeof(branch_fit_snapshot));
  (void)memset(&aes_rotw_snapshot, 0, sizeof(aes_rotw_snapshot));
  (void)memset(&align_up_snapshot, 0, sizeof(align_up_snapshot));
  (void)memset(&integer_unary_snapshot, 0,
               sizeof(integer_unary_snapshot));
  (void)memset(&integer_cast_snapshot, 0,
               sizeof(integer_cast_snapshot));
  (void)memset(&signed_bits_snapshot, 0, sizeof(signed_bits_snapshot));
  (void)memset(&selection_edge_snapshot, 0,
               sizeof(selection_edge_snapshot));
  (void)memset(&while_snapshot, 0, sizeof(while_snapshot));
  (void)memset(&do_snapshot, 0, sizeof(do_snapshot));
  (void)memset(&for_snapshot, 0, sizeof(for_snapshot));
  (void)memset(&declaration_for_snapshot, 0,
               sizeof(declaration_for_snapshot));
  (void)memset(&simd_cpuid_snapshot, 0, sizeof(simd_cpuid_snapshot));
  (void)memset(&file_assignment_snapshot, 0,
               sizeof(file_assignment_snapshot));
  (void)memset(&file_member_snapshot, 0, sizeof(file_member_snapshot));
  (void)memset(&bit_field_snapshot, 0, sizeof(bit_field_snapshot));
  (void)memset(&chained_assignment_snapshot, 0,
               sizeof(chained_assignment_snapshot));
  (void)memset(&layout_snapshot, 0, sizeof(layout_snapshot));
  (void)memset(&invalid_expression, 0, sizeof(invalid_expression));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_object_sources_are_unchanged(job)) {
    goto cleanup;
  }
  if (!parse_source(job, "/static-data.c", source_text, &unit) ||
      unit.object_definition_count == 0u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    (void)fprintf(stderr, "static-data: source setup failed\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                   &second);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 16u, 64u, &limited);
  }
  if (!check_status(status, CTOOL_OK, "object buffers")) {
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first static object") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0) {
    (void)fprintf(stderr, "first static emission contract differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  expected_object_size = bytes.size;
  expected_object = (ctool_u8 *)malloc((size_t)expected_object_size);
  if (expected_object == NULL) {
    (void)fprintf(stderr, "static object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(expected_object, bytes.data, (size_t)bytes.size);

  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "second static object") ||
      bytes.size != expected_object_size ||
      memcmp(bytes.data, expected_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0) {
    (void)fprintf(stderr, "static emission is not deterministic\n");
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, limited);
  if (!check_status(status, CTOOL_ERR_LIMIT, "limited static object") ||
      ctool_buffer_view(limited).size != 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      !expect_new_diagnostic(job, diagnostic_count, CTOOL_C_EMIT_DIAG_LIMIT,
                             NULL, "limited static object") ||
      unit_snapshot_matches(&snapshot, &unit) == 0 ||
      ctool_buffer_view(first).size != expected_object_size ||
      memcmp(ctool_buffer_view(first).data, expected_object,
             (size_t)expected_object_size) != 0) {
    (void)fprintf(stderr, "limited emission recovery contract differs\n");
    goto cleanup;
  }

  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    (void)fprintf(stderr, "second output rewind failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "post-limit static object") ||
      bytes.size != expected_object_size ||
      memcmp(bytes.data, expected_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0) {
    (void)fprintf(stderr, "emission did not recover after a limit\n");
    goto cleanup;
  }

  if (unit.object_definition_count == 0u || unit.initializer_count == 0u ||
      sizeof(*invalid_definitions) >
          SIZE_MAX / (size_t)unit.object_definition_count ||
      sizeof(*invalid_initializers) >
          SIZE_MAX / (size_t)unit.initializer_count) {
    (void)fprintf(stderr, "invalid-unit fixtures require parsed records\n");
    goto cleanup;
  }

  invalid_definition_bytes =
      (size_t)unit.object_definition_count * sizeof(*invalid_definitions);
  invalid_initializer_bytes =
      (size_t)unit.initializer_count * sizeof(*invalid_initializers);
  invalid_definitions = (ctool_c_object_definition_t *)calloc(
      1u,
      invalid_definition_bytes < sizeof(*invalid_definitions)
          ? sizeof(*invalid_definitions)
          : invalid_definition_bytes);
  if (invalid_definitions == NULL) {
    (void)fprintf(stderr, "invalid-unit fixture allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_definitions, unit.object_definitions,
               invalid_definition_bytes);
  invalid_definitions[0].binding = unit.binding_count;
  invalid_unit = unit;
  invalid_unit.object_definitions = invalid_definitions;
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &invalid_unit, second);
  if (!check_status(status, CTOOL_ERR_INPUT, "invalid frozen unit") ||
      ctool_buffer_view(second).size != 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      !expect_new_diagnostic(job, diagnostic_count,
                             CTOOL_C_EMIT_DIAG_INVALID_UNIT,
                             "CupidC object emission received an invalid "
                             "translation unit",
                             "invalid frozen unit") ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "invalid frozen-unit contract differs\n");
    goto cleanup;
  }

  (void)memcpy(invalid_definitions, unit.object_definitions,
               invalid_definition_bytes);
  invalid_definitions[0].initializer = unit.initializer_count;
  invalid_unit = unit;
  invalid_unit.object_definitions = invalid_definitions;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid initializer root") ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }

  invalid_initializers = (ctool_c_initializer_t *)calloc(
      1u,
      invalid_initializer_bytes < sizeof(*invalid_initializers)
          ? sizeof(*invalid_initializers)
          : invalid_initializer_bytes);
  if (invalid_initializers == NULL) {
    (void)fprintf(stderr, "invalid-initializer fixture allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_initializers, unit.initializers,
               invalid_initializer_bytes);
  invalid_initializers[0].kind = CTOOL_C_INITIALIZER_EXPRESSION;
  invalid_initializers[0].expression = 0u;
  invalid_initializers[0].integer_bits = 0u;
  invalid_initializers[0].string_bytes.data = (const ctool_u8 *)0;
  invalid_initializers[0].string_bytes.size = 0u;
  invalid_initializers[0].address_kind =
      CTOOL_C_INITIALIZER_ADDRESS_NONE;
  invalid_initializers[0].address_reference = CTOOL_C_AST_NONE;
  invalid_initializers[0].address_addend = 0;
  invalid_initializers[0].first_element = 0u;
  invalid_initializers[0].element_count = 1u;
  invalid_expression.kind = CTOOL_C_EXPRESSION_INTEGER_CONSTANT;
  invalid_expression.type = invalid_initializers[0].type;
  invalid_unit = unit;
  invalid_unit.initializers = invalid_initializers;
  invalid_unit.expressions = &invalid_expression;
  invalid_unit.expression_count = 1u;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_EMIT_DIAG_INVALID_UNIT,
          "CupidC object emission received an invalid translation unit",
          "invalid static expression initializer payload") ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }

  (void)memcpy(invalid_initializers, unit.initializers,
               invalid_initializer_bytes);
  invalid_initializers[0].kind = CTOOL_C_INITIALIZER_EXPRESSION;
  invalid_initializers[0].expression = 0u;
  invalid_expression.kind = CTOOL_C_EXPRESSION_INTEGER_CONSTANT;
  invalid_expression.type = unit.graph.type_count;
  invalid_unit = unit;
  invalid_unit.initializers = invalid_initializers;
  invalid_unit.expressions = &invalid_expression;
  invalid_unit.expression_count = 1u;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_EMIT_DIAG_INVALID_UNIT,
          "CupidC object emission received an invalid translation unit",
          "invalid runtime initializer expression type") ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }

  (void)memcpy(invalid_initializers, unit.initializers,
               invalid_initializer_bytes);
  invalid_initializers[0].type = unit.graph.type_count;
  invalid_unit = unit;
  invalid_unit.initializers = invalid_initializers;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_EMIT_DIAG_INVALID_UNIT,
          "CupidC object emission received an invalid translation unit",
          "invalid initializer type") ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }

  (void)memcpy(invalid_initializers, unit.initializers,
               invalid_initializer_bytes);
  invalid_initializers[0].kind = CTOOL_C_INITIALIZER_EXPRESSION;
  invalid_initializers[0].expression = 0u;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_EMIT_DIAG_INVALID_UNIT,
          "CupidC object emission received an invalid translation unit",
          "invalid runtime initializer reference") ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }

  (void)memcpy(invalid_initializers, unit.initializers,
               invalid_initializer_bytes);
  for (initializer_index = 0u; initializer_index < unit.initializer_count;
       initializer_index++) {
    if (invalid_initializers[initializer_index].kind ==
            CTOOL_C_INITIALIZER_ADDRESS &&
        invalid_initializers[initializer_index].address_kind ==
            CTOOL_C_INITIALIZER_ADDRESS_BINDING) {
      break;
    }
  }
  if (initializer_index == unit.initializer_count) {
    (void)fprintf(stderr, "binding-address fixture is absent\n");
    goto cleanup;
  }
  invalid_initializers[initializer_index].address_reference =
      unit.binding_count;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_EMIT_DIAG_INVALID_UNIT,
          "CupidC object emission received an invalid translation unit",
          "invalid address binding") ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }

  invalid_binding_bytes =
      (size_t)unit.binding_count * sizeof(*invalid_bindings);
  invalid_bindings = (ctool_c_binding_t *)calloc(
      1u,
      invalid_binding_bytes < sizeof(*invalid_bindings)
          ? sizeof(*invalid_bindings)
          : invalid_binding_bytes);
  if (invalid_bindings == NULL) {
    (void)fprintf(stderr, "invalid-binding fixture allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_bindings, unit.bindings, invalid_binding_bytes);
  invalid_bindings[unit.object_definitions[0].binding].minimum_alignment = 6u;
  invalid_unit = unit;
  invalid_unit.bindings = invalid_bindings;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_EMIT_DIAG_INVALID_UNIT,
          "CupidC object emission received an invalid translation unit",
          "invalid object alignment") ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "nonempty object output") ||
      ctool_buffer_view(first).size != expected_object_size ||
      memcmp(ctool_buffer_view(first).data, expected_object,
             (size_t)expected_object_size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count) {
    (void)fprintf(stderr, "nonempty output precondition differs\n");
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, (const ctool_c_translation_unit_t *)0,
                               second);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT, "null frozen unit") ||
      ctool_buffer_view(second).size != 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count) {
    (void)fprintf(stderr, "null unit precondition differs\n");
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, (ctool_buffer_t *)0);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT, "null output") ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count) {
    (void)fprintf(stderr, "null output precondition differs\n");
    goto cleanup;
  }

  if (!parse_source(job, "/function-definition.c", function_text,
                    &function_unit) ||
      function_unit.function_definition_count != 18u ||
      function_unit.object_definition_count == 0u ||
      function_unit.block_binding_count == 0u ||
      !take_unit_snapshot(&function_unit, &function_snapshot)) {
    (void)fprintf(stderr, "function object fixture differs\n");
    goto cleanup;
  }
  free(invalid_initializers);
  invalid_initializers = NULL;
  invalid_block_bindings = (ctool_c_block_binding_t *)malloc(
      (size_t)function_unit.block_binding_count *
      sizeof(*invalid_block_bindings));
  if (invalid_block_bindings == NULL ||
      function_unit.block_binding_count < 2u ||
      function_unit.block_bindings[0].initializer == CTOOL_C_AST_NONE ||
      function_unit.block_bindings[1].initializer == CTOOL_C_AST_NONE) {
    goto cleanup;
  }
  (void)memcpy(invalid_block_bindings, function_unit.block_bindings,
               (size_t)function_unit.block_binding_count *
                   sizeof(*invalid_block_bindings));
  invalid_block_bindings[1].initializer =
      invalid_block_bindings[0].initializer;
  invalid_unit = function_unit;
  invalid_unit.block_bindings = invalid_block_bindings;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "duplicate local initializer owner") ||
      unit_snapshot_matches(&function_snapshot, &function_unit) == 0) {
    goto cleanup;
  }
  invalid_initializers = (ctool_c_initializer_t *)malloc(
      (size_t)function_unit.initializer_count *
      sizeof(*invalid_initializers));
  if (invalid_initializers == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_initializers, function_unit.initializers,
               (size_t)function_unit.initializer_count *
                   sizeof(*invalid_initializers));
  initializer_index = function_unit.object_definitions[0].initializer;
  definition_index = function_unit.block_bindings[0].initializer;
  if (initializer_index >= function_unit.initializer_count ||
      definition_index >= function_unit.initializer_count ||
      function_unit.initializers[definition_index].kind !=
          CTOOL_C_INITIALIZER_EXPRESSION) {
    goto cleanup;
  }
  invalid_initializers[definition_index].first_element = 0u;
  invalid_initializers[definition_index].element_count = 1u;
  invalid_unit = function_unit;
  invalid_unit.initializers = invalid_initializers;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid expression initializer payload") ||
      unit_snapshot_matches(&function_snapshot, &function_unit) == 0) {
    goto cleanup;
  }
  (void)memcpy(invalid_initializers, function_unit.initializers,
               (size_t)function_unit.initializer_count *
                   sizeof(*invalid_initializers));
  for (definition_index = 0u;
       definition_index < function_unit.block_binding_count;
       definition_index++) {
    ctool_u32 candidate =
        function_unit.block_bindings[definition_index].initializer;
    if (candidate < function_unit.initializer_count &&
        function_unit.initializers[candidate].kind ==
            CTOOL_C_INITIALIZER_EXPRESSION &&
        function_unit.initializers[candidate].type ==
            invalid_initializers[initializer_index].type) {
      break;
    }
  }
  if (definition_index == function_unit.block_binding_count) {
    (void)fprintf(stderr,
                  "matching runtime initializer fixture is absent\n");
    goto cleanup;
  }
  definition_index =
      function_unit.block_bindings[definition_index].initializer;
  invalid_initializers[initializer_index].kind =
      CTOOL_C_INITIALIZER_EXPRESSION;
  invalid_initializers[initializer_index].expression =
      function_unit.initializers[definition_index].expression;
  invalid_initializers[initializer_index].integer_bits = 0u;
  invalid_initializers[initializer_index].string_bytes.data =
      (const ctool_u8 *)0;
  invalid_initializers[initializer_index].string_bytes.size = 0u;
  invalid_initializers[initializer_index].address_kind =
      CTOOL_C_INITIALIZER_ADDRESS_NONE;
  invalid_initializers[initializer_index].address_reference =
      CTOOL_C_AST_NONE;
  invalid_initializers[initializer_index].address_addend = 0;
  invalid_initializers[initializer_index].first_element =
      CTOOL_C_AST_NONE;
  invalid_initializers[initializer_index].element_count = 0u;
  invalid_unit = function_unit;
  invalid_unit.initializers = invalid_initializers;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_EMIT_DIAG_INITIALIZER,
          "CupidC object emission requires static initializer values",
          "static runtime initializer boundary") ||
      unit_snapshot_matches(&function_snapshot, &function_unit) == 0) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &function_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first function object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&snapshot, &unit) == 0 ||
      unit_snapshot_matches(&function_snapshot, &function_unit) == 0) {
    (void)fprintf(stderr, "first function emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  function_object_size = bytes.size;
  function_object = (ctool_u8 *)malloc((size_t)function_object_size);
  if (function_object == NULL) {
    (void)fprintf(stderr, "function object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(function_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    (void)fprintf(stderr, "function output rewind failed\n");
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &function_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat function object") ||
      bytes.size != function_object_size ||
      memcmp(bytes.data, function_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&function_snapshot, &function_unit) == 0) {
    (void)fprintf(stderr, "function emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/function-definition.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read function object") ||
      !validate_function_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/external-object-load.c", external_object_text,
                    &external_object_unit) ||
      !take_unit_snapshot(&external_object_unit,
                          &external_object_snapshot)) {
    (void)fprintf(stderr, "isolated external object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &external_object_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "isolated external object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&external_object_snapshot,
                            &external_object_unit) == 0) {
    (void)fprintf(stderr, "isolated external object emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  object_source.path.text = ctool_string("/external-object-load.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read isolated external object") ||
      !validate_external_object_load(&object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/active-paint-multiplication.c",
                    multiplication_text, &multiplication_unit) ||
      !take_unit_snapshot(&multiplication_unit,
                          &multiplication_snapshot)) {
    (void)fprintf(stderr, "Paint multiplication object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &multiplication_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first Paint multiplication object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&multiplication_snapshot,
                            &multiplication_unit) == 0) {
    (void)fprintf(stderr, "first Paint multiplication emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  multiplication_object_size = bytes.size;
  multiplication_object =
      (ctool_u8 *)malloc((size_t)multiplication_object_size);
  if (multiplication_object == NULL) {
    (void)fprintf(stderr, "Paint multiplication snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(multiplication_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &multiplication_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat Paint multiplication object") ||
      bytes.size != multiplication_object_size ||
      memcmp(bytes.data, multiplication_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&multiplication_snapshot,
                            &multiplication_unit) == 0) {
    (void)fprintf(stderr,
                  "Paint multiplication emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-paint-multiplication.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read Paint multiplication object") ||
      !validate_paint_multiplication_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/unsigned-multiplication.c",
                    unsigned_multiplication_text,
                    &unsigned_multiplication_unit) ||
      !take_unit_snapshot(&unsigned_multiplication_unit,
                          &unsigned_multiplication_snapshot)) {
    (void)fprintf(stderr, "unsigned multiplication object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unsigned_multiplication_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "unsigned multiplication object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&unsigned_multiplication_snapshot,
                            &unsigned_multiplication_unit) == 0) {
    (void)fprintf(stderr, "unsigned multiplication emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  object_source.path.text = ctool_string("/unsigned-multiplication.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read unsigned multiplication object") ||
      !validate_unsigned_multiplication_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/division.c", division_text, &division_unit) ||
      !take_unit_snapshot(&division_unit, &division_snapshot)) {
    (void)fprintf(stderr, "division object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &division_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first division object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&division_snapshot, &division_unit) == 0) {
    (void)fprintf(stderr, "first division emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  division_object_size = bytes.size;
  division_object = (ctool_u8 *)malloc((size_t)division_object_size);
  if (division_object == NULL) {
    (void)fprintf(stderr, "division object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(division_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &division_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat division object") ||
      bytes.size != division_object_size ||
      memcmp(bytes.data, division_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&division_snapshot, &division_unit) == 0) {
    (void)fprintf(stderr, "division emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/division.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read division object") ||
      !validate_division_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/active-asm-branch-fits-i8.c",
                    branch_fit_text, &branch_fit_unit) ||
      !take_unit_snapshot(&branch_fit_unit, &branch_fit_snapshot)) {
    (void)fprintf(stderr, "branch-range object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &branch_fit_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first branch-range object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&branch_fit_snapshot, &branch_fit_unit) == 0) {
    (void)fprintf(stderr, "first branch-range emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  branch_fit_object_size = bytes.size;
  branch_fit_object = (ctool_u8 *)malloc((size_t)branch_fit_object_size);
  if (branch_fit_object == NULL) {
    (void)fprintf(stderr, "branch-range object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(branch_fit_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &branch_fit_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat branch-range object") ||
      bytes.size != branch_fit_object_size ||
      memcmp(bytes.data, branch_fit_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&branch_fit_snapshot, &branch_fit_unit) == 0) {
    (void)fprintf(stderr, "branch-range emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-asm-branch-fits-i8.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read branch-range object") ||
      !validate_branch_fit_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/active-aes-rotw.c", aes_rotw_text,
                    &aes_rotw_unit) ||
      !take_unit_snapshot(&aes_rotw_unit, &aes_rotw_snapshot)) {
    (void)fprintf(stderr, "AES word-rotation object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &aes_rotw_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first AES word-rotation object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&aes_rotw_snapshot, &aes_rotw_unit) == 0) {
    (void)fprintf(stderr, "first AES word-rotation emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  aes_rotw_object_size = bytes.size;
  aes_rotw_object = (ctool_u8 *)malloc((size_t)aes_rotw_object_size);
  if (aes_rotw_object == NULL) {
    (void)fprintf(stderr,
                  "AES word-rotation object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(aes_rotw_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &aes_rotw_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat AES word-rotation object") ||
      bytes.size != aes_rotw_object_size ||
      memcmp(bytes.data, aes_rotw_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&aes_rotw_snapshot, &aes_rotw_unit) == 0) {
    (void)fprintf(stderr,
                  "AES word-rotation emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-aes-rotw.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read AES word-rotation object") ||
      !validate_aes_rotw_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  align_up_text = make_align_up_fixture();
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      align_up_text == NULL ||
      !parse_source(job, "/active-memory-align-up.c", align_up_text,
                    &align_up_unit) ||
      !take_unit_snapshot(&align_up_unit, &align_up_snapshot)) {
    (void)fprintf(stderr, "memory alignment object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &align_up_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first memory alignment object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&align_up_snapshot, &align_up_unit) == 0) {
    (void)fprintf(stderr, "first memory alignment emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  align_up_object_size = bytes.size;
  align_up_object = (ctool_u8 *)malloc((size_t)align_up_object_size);
  if (align_up_object == NULL) {
    (void)fprintf(stderr, "memory alignment snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(align_up_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &align_up_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat memory alignment object") ||
      bytes.size != align_up_object_size ||
      memcmp(bytes.data, align_up_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&align_up_snapshot, &align_up_unit) == 0) {
    (void)fprintf(stderr, "memory alignment emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-memory-align-up.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read memory alignment object") ||
      !validate_align_up_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/integer-unary.c", integer_unary_text,
                    &integer_unary_unit) ||
      !take_unit_snapshot(&integer_unary_unit, &integer_unary_snapshot)) {
    (void)fprintf(stderr, "integer unary object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &integer_unary_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first integer unary object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&integer_unary_snapshot,
                            &integer_unary_unit) == 0) {
    (void)fprintf(stderr, "first integer unary emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  integer_unary_object_size = bytes.size;
  integer_unary_object =
      (ctool_u8 *)malloc((size_t)integer_unary_object_size);
  if (integer_unary_object == NULL) {
    (void)fprintf(stderr, "integer unary snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(integer_unary_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &integer_unary_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat integer unary object") ||
      bytes.size != integer_unary_object_size ||
      memcmp(bytes.data, integer_unary_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&integer_unary_snapshot,
                            &integer_unary_unit) == 0) {
    (void)fprintf(stderr, "integer unary emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/integer-unary.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read integer unary object") ||
      !validate_integer_unary_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  integer_cast_text = make_integer_cast_fixture();
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      integer_cast_text == NULL ||
      !parse_source(job, "/integer-cast.c", integer_cast_text,
                    &integer_cast_unit) ||
      !take_unit_snapshot(&integer_cast_unit, &integer_cast_snapshot)) {
    (void)fprintf(stderr, "integer cast object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &integer_cast_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first integer cast object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&integer_cast_snapshot, &integer_cast_unit) == 0) {
    (void)fprintf(stderr, "first integer cast emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  integer_cast_object_size = bytes.size;
  integer_cast_object =
      (ctool_u8 *)malloc((size_t)integer_cast_object_size);
  if (integer_cast_object == NULL) {
    (void)fprintf(stderr, "integer cast snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(integer_cast_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &integer_cast_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat integer cast object") ||
      bytes.size != integer_cast_object_size ||
      memcmp(bytes.data, integer_cast_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&integer_cast_snapshot, &integer_cast_unit) == 0) {
    (void)fprintf(stderr, "integer cast emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/integer-cast.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read integer cast object") ||
      !validate_integer_cast_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  signed_bits_text = make_signed_bits_fixture();
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      signed_bits_text == NULL ||
      !parse_source(job, "/active-cupiddis-signed-bits.c", signed_bits_text,
                    &signed_bits_unit) ||
      !take_unit_snapshot(&signed_bits_unit, &signed_bits_snapshot)) {
    (void)fprintf(stderr, "signed-bit object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &signed_bits_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first signed-bit object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&signed_bits_snapshot, &signed_bits_unit) == 0) {
    (void)fprintf(stderr, "first signed-bit emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  signed_bits_object_size = bytes.size;
  signed_bits_object = (ctool_u8 *)malloc((size_t)signed_bits_object_size);
  if (signed_bits_object == NULL) {
    (void)fprintf(stderr, "signed-bit snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(signed_bits_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &signed_bits_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat signed-bit object") ||
      bytes.size != signed_bits_object_size ||
      memcmp(bytes.data, signed_bits_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&signed_bits_snapshot, &signed_bits_unit) == 0) {
    (void)fprintf(stderr, "signed-bit emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-cupiddis-signed-bits.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read signed-bit object") ||
      !validate_signed_bits_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/active-while.c", while_text, &while_unit) ||
      !take_unit_snapshot(&while_unit, &while_snapshot)) {
    (void)fprintf(stderr, "while object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &while_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first while object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&while_snapshot, &while_unit) == 0) {
    (void)fprintf(stderr, "first while emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  while_object_size = bytes.size;
  while_object = (ctool_u8 *)malloc((size_t)while_object_size);
  if (while_object == NULL) {
    (void)fprintf(stderr, "while object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(while_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &while_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat while object") ||
      bytes.size != while_object_size ||
      memcmp(bytes.data, while_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&while_snapshot, &while_unit) == 0) {
    (void)fprintf(stderr, "while emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-while.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read while object") ||
      !validate_while_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  do_text = make_do_fixture();
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK || do_text == NULL ||
      !parse_source(job, "/active-do.c", do_text, &do_unit) ||
      !take_unit_snapshot(&do_unit, &do_snapshot)) {
    (void)fprintf(stderr, "do object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &do_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first do object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&do_snapshot, &do_unit) == 0) {
    (void)fprintf(stderr, "first do emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  do_object_size = bytes.size;
  do_object = (ctool_u8 *)malloc((size_t)do_object_size);
  if (do_object == NULL) {
    (void)fprintf(stderr, "do object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(do_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &do_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat do object") ||
      bytes.size != do_object_size ||
      memcmp(bytes.data, do_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&do_snapshot, &do_unit) == 0) {
    (void)fprintf(stderr, "do emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-do.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read do object") ||
      !validate_do_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/active-for.c", for_text, &for_unit) ||
      !take_unit_snapshot(&for_unit, &for_snapshot)) {
    (void)fprintf(stderr, "for object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &for_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first for object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&for_snapshot, &for_unit) == 0) {
    (void)fprintf(stderr, "first for emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for_object_size = bytes.size;
  for_object = (ctool_u8 *)malloc((size_t)for_object_size);
  if (for_object == NULL) {
    (void)fprintf(stderr, "for object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(for_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &for_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat for object") ||
      bytes.size != for_object_size ||
      memcmp(bytes.data, for_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&for_snapshot, &for_unit) == 0) {
    (void)fprintf(stderr, "for emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-for.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read for object") ||
      !validate_for_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/declaration-for.c", declaration_for_text,
                    &declaration_for_unit) ||
      !take_unit_snapshot(&declaration_for_unit,
                          &declaration_for_snapshot)) {
    (void)fprintf(stderr, "declaration for object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &declaration_for_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first declaration for object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&declaration_for_snapshot,
                            &declaration_for_unit) == 0) {
    (void)fprintf(stderr, "first declaration for emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  declaration_for_object_size = bytes.size;
  declaration_for_object =
      (ctool_u8 *)malloc((size_t)declaration_for_object_size);
  if (declaration_for_object == NULL) {
    goto cleanup;
  }
  (void)memcpy(declaration_for_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &declaration_for_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat declaration for object") ||
      bytes.size != declaration_for_object_size ||
      memcmp(bytes.data, declaration_for_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&declaration_for_snapshot,
                            &declaration_for_unit) == 0) {
    (void)fprintf(stderr, "declaration for emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/declaration-for.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read declaration for object") ||
      !validate_declaration_for_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  lexical_declaration_statements = (ctool_c_statement_t *)malloc(
      (size_t)declaration_for_unit.statement_count *
      sizeof(*lexical_declaration_statements));
  if (lexical_declaration_statements == NULL) {
    goto cleanup;
  }
  (void)memcpy(lexical_declaration_statements,
               declaration_for_unit.statements,
               (size_t)declaration_for_unit.statement_count *
                   sizeof(*lexical_declaration_statements));
  for (statement_index = 0u;
       statement_index < declaration_for_unit.statement_count;
       statement_index++) {
    if (lexical_declaration_statements[statement_index].kind ==
            CTOOL_C_STATEMENT_DECLARATION &&
        lexical_declaration_statements[statement_index]
                .first_block_binding == 2u) {
      lexical_declaration_statements[statement_index].first_block_binding =
          1u;
      break;
    }
  }
  invalid_unit = declaration_for_unit;
  invalid_unit.statements = lexical_declaration_statements;
  if (statement_index == declaration_for_unit.statement_count ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !expect_object_failure_preserves_unit(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "overlapping nested declaration bindings object") ||
      unit_snapshot_matches(&declaration_for_snapshot,
                            &declaration_for_unit) == 0) {
    goto cleanup;
  }
  loop_control_statements = (ctool_c_statement_t *)malloc(
      (size_t)for_unit.statement_count * sizeof(*loop_control_statements));
  if (loop_control_statements == NULL) {
    goto cleanup;
  }
  (void)memcpy(loop_control_statements, for_unit.statements,
               (size_t)for_unit.statement_count *
                   sizeof(*loop_control_statements));
  for (definition_index = 0u; definition_index < for_unit.statement_count;
       definition_index++) {
    if (loop_control_statements[definition_index].kind ==
        CTOOL_C_STATEMENT_BREAK) {
      break;
    }
  }
  if (definition_index == for_unit.statement_count) {
    goto cleanup;
  }
  loop_control_statements[definition_index].expression = 0u;
  invalid_unit = for_unit;
  invalid_unit.statements = loop_control_statements;
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "break statement with expression payload object") ||
      unit_snapshot_matches(&for_snapshot, &for_unit) == 0) {
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/selection-edges.c", selection_edge_text,
                    &selection_edge_unit) ||
      !take_unit_snapshot(&selection_edge_unit, &selection_edge_snapshot)) {
    (void)fprintf(stderr, "selection edge object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &selection_edge_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "selection edge object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&selection_edge_snapshot,
                            &selection_edge_unit) == 0) {
    (void)fprintf(stderr, "selection edge emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  object_source.path.text = ctool_string("/selection-edges.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read selection edge object") ||
      !validate_selection_edge_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (selection_edge_unit.function_definition_count != 2u ||
      selection_edge_unit.function_definitions[0].body >=
          selection_edge_unit.statement_count) {
    goto cleanup;
  }
  {
    const ctool_c_statement_t *body =
        &selection_edge_unit
             .statements[selection_edge_unit.function_definitions[0].body];
    if (body->kind != CTOOL_C_STATEMENT_COMPOUND || body->child_count != 2u ||
        body->first_child > selection_edge_unit.statement_child_count ||
        body->child_count >
            selection_edge_unit.statement_child_count - body->first_child) {
      goto cleanup;
    }
    unreachable_statement =
        selection_edge_unit.statement_children[body->first_child + 1u];
  }
  unreachable_statements = (ctool_c_statement_t *)malloc(
      (size_t)selection_edge_unit.statement_count *
      sizeof(*unreachable_statements));
  if (unreachable_statements == NULL ||
      unreachable_statement >= selection_edge_unit.statement_count ||
      selection_edge_unit.statements[unreachable_statement].kind !=
          CTOOL_C_STATEMENT_IF) {
    goto cleanup;
  }
  (void)memcpy(unreachable_statements, selection_edge_unit.statements,
               (size_t)selection_edge_unit.statement_count *
                   sizeof(*unreachable_statements));
  unreachable_statements[unreachable_statement].condition =
      selection_edge_unit.expression_count;
  invalid_unit = selection_edge_unit;
  invalid_unit.statements = unreachable_statements;
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid unreachable selection condition object") ||
      unit_snapshot_matches(&selection_edge_snapshot,
                            &selection_edge_unit) == 0) {
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/active-simd-cpuid.c", simd_cpuid_text,
                    &simd_cpuid_unit) ||
      !take_unit_snapshot(&simd_cpuid_unit, &simd_cpuid_snapshot)) {
    (void)fprintf(stderr, "CPUID toggle object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &simd_cpuid_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "first CPUID toggle object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&simd_cpuid_snapshot, &simd_cpuid_unit) == 0) {
    (void)fprintf(stderr, "first CPUID toggle emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  simd_cpuid_object_size = bytes.size;
  simd_cpuid_object = (ctool_u8 *)malloc((size_t)simd_cpuid_object_size);
  if (simd_cpuid_object == NULL) {
    (void)fprintf(stderr, "CPUID toggle object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(simd_cpuid_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &simd_cpuid_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat CPUID toggle object") ||
      bytes.size != simd_cpuid_object_size ||
      memcmp(bytes.data, simd_cpuid_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&simd_cpuid_snapshot, &simd_cpuid_unit) == 0) {
    (void)fprintf(stderr, "CPUID toggle emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-simd-cpuid.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read CPUID toggle object") ||
      !validate_simd_cpuid_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/active-vga-file-assignment.c",
                    file_assignment_text, &file_assignment_unit) ||
      !take_unit_snapshot(&file_assignment_unit,
                          &file_assignment_snapshot)) {
    (void)fprintf(stderr, "file assignment object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &file_assignment_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "file assignment object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&file_assignment_snapshot,
                            &file_assignment_unit) == 0) {
    (void)fprintf(stderr, "file assignment emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-vga-file-assignment.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read file assignment object") ||
      !validate_file_assignment_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/active-timer-frequency.c", file_member_text,
                    &file_member_unit) ||
      !take_unit_snapshot(&file_member_unit, &file_member_snapshot)) {
    (void)fprintf(stderr, "file-member object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &file_member_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "file-member object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&file_member_snapshot, &file_member_unit) == 0) {
    (void)fprintf(stderr, "file-member emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-timer-frequency.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read file-member object") ||
      !validate_file_member_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  file_member_object_size = bytes.size;
  file_member_object = (ctool_u8 *)malloc((size_t)bytes.size);
  if (file_member_object == NULL) {
    (void)fprintf(stderr, "file-member object copy failed\n");
    goto cleanup;
  }
  (void)memcpy(file_member_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    (void)fprintf(stderr, "file-member repeat rewind failed\n");
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &file_member_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeated file-member object") ||
      bytes.size != file_member_object_size ||
      memcmp(bytes.data, file_member_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&file_member_snapshot, &file_member_unit) == 0) {
    (void)fprintf(stderr, "file-member emission is not deterministic\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/active-doom-color.c", bit_field_text,
                    &bit_field_unit) ||
      !take_unit_snapshot(&bit_field_unit, &bit_field_snapshot)) {
    (void)fprintf(stderr, "bit-field object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &bit_field_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "bit-field object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&bit_field_snapshot, &bit_field_unit) == 0) {
    (void)fprintf(stderr, "bit-field emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-doom-color.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read bit-field object") ||
      !validate_bit_field_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  bit_field_object_size = bytes.size;
  bit_field_object = (ctool_u8 *)malloc((size_t)bytes.size);
  if (bit_field_object == NULL) {
    (void)fprintf(stderr, "bit-field object copy failed\n");
    goto cleanup;
  }
  (void)memcpy(bit_field_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    (void)fprintf(stderr, "bit-field repeat rewind failed\n");
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &bit_field_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeated bit-field object") ||
      bytes.size != bit_field_object_size ||
      memcmp(bytes.data, bit_field_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&bit_field_snapshot, &bit_field_unit) == 0) {
    (void)fprintf(stderr, "bit-field emission is not deterministic\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/chained-assignment.c", chained_assignment_text,
                    &chained_assignment_unit) ||
      !take_unit_snapshot(&chained_assignment_unit,
                          &chained_assignment_snapshot)) {
    (void)fprintf(stderr, "chained assignment object setup failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &chained_assignment_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "chained assignment object") ||
      bytes.size == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&chained_assignment_snapshot,
                            &chained_assignment_unit) == 0) {
    (void)fprintf(stderr, "chained assignment emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  chained_assignment_object_size = bytes.size;
  chained_assignment_object =
      (ctool_u8 *)malloc((size_t)chained_assignment_object_size);
  if (chained_assignment_object == NULL) {
    (void)fprintf(stderr, "chained assignment snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(chained_assignment_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &chained_assignment_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat chained assignment object") ||
      bytes.size != chained_assignment_object_size ||
      memcmp(bytes.data, chained_assignment_object, (size_t)bytes.size) != 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_snapshot_matches(&chained_assignment_snapshot,
                            &chained_assignment_unit) == 0) {
    (void)fprintf(stderr,
                  "chained assignment emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/chained-assignment.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read chained assignment object") ||
      !validate_chained_assignment_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/unsupported-function.c",
                    unsupported_function_text,
                    &unsupported_function_unit) ||
      unsupported_function_unit.expression_count == 0u) {
    goto cleanup;
  }
  unsupported_expressions = (ctool_c_expression_t *)malloc(
      (size_t)unsupported_function_unit.expression_count *
      sizeof(*unsupported_expressions));
  if (unsupported_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(unsupported_expressions,
               unsupported_function_unit.expressions,
               (size_t)unsupported_function_unit.expression_count *
                   sizeof(*unsupported_expressions));
  for (expression_index = 0u;
       expression_index < unsupported_function_unit.expression_count;
       expression_index++) {
    if (unsupported_expressions[expression_index].kind ==
        CTOOL_C_EXPRESSION_INTEGER_CONSTANT) {
      unsupported_expressions[expression_index].kind =
          (ctool_c_expression_kind_t)0;
      break;
    }
  }
  if (expression_index == unsupported_function_unit.expression_count) {
    goto cleanup;
  }
  unsupported_function_unit.expressions = unsupported_expressions;
  if (!expect_object_failure(
          job, &unsupported_function_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_EXPRESSION,
          "CupidC IR lowering does not yet support this expression",
          "unsupported function expression") ||
      !parse_source(job, "/wide-selection.c", wide_selection_text,
                     &wide_selection_unit) ||
      !expect_object_success_preserves_unit(
          job, &wide_selection_unit, second,
          "wide selection condition object") ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/wide-while.c", wide_while_text,
                     &wide_while_unit) ||
      !expect_object_success_preserves_unit(
          job, &wide_while_unit, second,
          "wide while condition object") ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/wide-do.c", wide_do_text, &wide_do_unit) ||
      !expect_object_success_preserves_unit(
          job, &wide_do_unit, second,
          "wide do condition object") ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/wide-for.c", wide_for_text, &wide_for_unit) ||
      !expect_object_success_preserves_unit(
          job, &wide_for_unit, second,
          "wide for condition object") ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/terminal-wide-for-iteration.c",
                    terminal_wide_for_iteration_text,
                    &terminal_wide_for_iteration_unit) ||
      !expect_object_success_preserves_unit(
          job, &terminal_wide_for_iteration_unit, second,
          "unreachable wide for iteration object") ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/wide-declaration-for.c",
                    wide_declaration_for_text,
                    &wide_declaration_for_unit) ||
      !expect_object_success_preserves_unit(
          job, &wide_declaration_for_unit, second,
          "wide declaration for initializer object") ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/unreachable-wide-declaration.c",
                    unreachable_wide_declaration_text,
                    &unreachable_wide_declaration_unit) ||
      !expect_object_success_preserves_unit(
          job, &unreachable_wide_declaration_unit, second,
          "unreachable wide declaration object") ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/nonvoid-selection-fallthrough.c",
                    nonvoid_selection_fallthrough_text,
                    &nonvoid_selection_fallthrough_unit) ||
      !expect_object_failure(
          job, &nonvoid_selection_fallthrough_unit, second,
          CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_UNSUPPORTED_STATEMENT,
          "CupidC IR lowering does not yet support this statement",
          "nonvoid selection fallthrough object") ||
      !parse_source(job, "/wide-logical-not.c", wide_logical_not_text,
                     &wide_logical_not_unit) ||
      !expect_object_success_preserves_unit(
          job, &wide_logical_not_unit, second,
          "wide logical-not object") ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/external-inline.c", external_inline_text,
                    &external_inline_unit) ||
      !expect_object_failure(
          job, &external_inline_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_EXTERNAL_INLINE,
          "CupidC IR lowering requires external-inline finalization before "
          "lowering this definition",
          "external inline object") ||
      !expect_object_failure(job, &function_unit, limited, CTOOL_ERR_LIMIT,
                             CTOOL_C_EMIT_DIAG_LIMIT, NULL,
                             "limited function object")) {
    goto cleanup;
  }

  object_source.path.text = ctool_string("/static-data.o");
  object_source.contents = ctool_buffer_view(first);
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read static object") ||
      validate_object(&object) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  if (!parse_source(job, "/source-derived-static-layout.c", layout_text,
                    &layout_unit) ||
      layout_unit.object_definition_count != 13u ||
      !take_unit_snapshot(&layout_unit, &layout_snapshot)) {
    (void)fprintf(stderr, "source-derived layout setup failed\n");
    goto cleanup;
  }
  if (ctool_buffer_rewind(first, 0u) != CTOOL_OK ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    (void)fprintf(stderr, "source-derived output rewind failed\n");
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &layout_unit, first);
  layout_bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first source-derived object") ||
      layout_bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&layout_snapshot, &layout_unit) == 0) {
    (void)fprintf(stderr, "first source-derived emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &layout_unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "second source-derived object") ||
      bytes.size != layout_bytes.size ||
      memcmp(bytes.data, layout_bytes.data, (size_t)layout_bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&layout_snapshot, &layout_unit) == 0) {
    (void)fprintf(stderr, "source-derived emission is not deterministic\n");
    goto cleanup;
  }

  object_source.path.text = ctool_string("/source-derived-static-layout.o");
  object_source.contents = layout_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read source-derived static object") ||
      validate_layout_object(&object) == 0 ||
      unit_snapshot_matches(&layout_snapshot, &layout_unit) == 0) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    (void)fprintf(stderr, "malformed-layout output rewind failed\n");
    goto cleanup;
  }
  invalid_layout_initializer_bytes =
      (size_t)layout_unit.initializer_count *
      sizeof(*invalid_layout_initializers);
  invalid_element_bytes = (size_t)layout_unit.initializer_element_count *
                          sizeof(*invalid_elements);
  invalid_layout_initializers = (ctool_c_initializer_t *)calloc(
      1u,
      invalid_layout_initializer_bytes < sizeof(*invalid_layout_initializers)
          ? sizeof(*invalid_layout_initializers)
          : invalid_layout_initializer_bytes);
  invalid_elements = (ctool_c_initializer_element_t *)calloc(
      1u,
      invalid_element_bytes < sizeof(*invalid_elements)
          ? sizeof(*invalid_elements)
          : invalid_element_bytes);
  if (invalid_layout_initializers == NULL || invalid_elements == NULL) {
    (void)fprintf(stderr, "malformed-layout fixture allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_layout_initializers, layout_unit.initializers,
               invalid_layout_initializer_bytes);
  for (initializer_index = 0u;
       initializer_index < layout_unit.initializer_count;
       initializer_index++) {
    if (invalid_layout_initializers[initializer_index].kind ==
        CTOOL_C_INITIALIZER_LIST) {
      break;
    }
  }
  if (initializer_index == layout_unit.initializer_count) {
    (void)fprintf(stderr, "list-initializer fixture is absent\n");
    goto cleanup;
  }
  invalid_layout_initializers[initializer_index].first_element =
      layout_unit.initializer_element_count + 1u;
  invalid_unit = layout_unit;
  invalid_unit.initializers = invalid_layout_initializers;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid initializer list slice") ||
      unit_snapshot_matches(&layout_snapshot, &layout_unit) == 0) {
    goto cleanup;
  }

  (void)memcpy(invalid_layout_initializers, layout_unit.initializers,
               invalid_layout_initializer_bytes);
  for (initializer_index = 0u;
       initializer_index < layout_unit.initializer_count;
       initializer_index++) {
    if (invalid_layout_initializers[initializer_index].kind ==
            CTOOL_C_INITIALIZER_ADDRESS &&
        invalid_layout_initializers[initializer_index].address_kind ==
            CTOOL_C_INITIALIZER_ADDRESS_STRING) {
      break;
    }
  }
  if (initializer_index == layout_unit.initializer_count) {
    (void)fprintf(stderr, "string-address fixture is absent\n");
    goto cleanup;
  }
  invalid_layout_initializers[initializer_index].string_bytes.data = NULL;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_EMIT_DIAG_INVALID_UNIT,
          "CupidC object emission received an invalid translation unit",
          "invalid string address") ||
      unit_snapshot_matches(&layout_snapshot, &layout_unit) == 0) {
    goto cleanup;
  }

  (void)memcpy(invalid_elements, layout_unit.initializer_elements,
               invalid_element_bytes);
  invalid_elements[0].initializer = layout_unit.initializer_count;
  invalid_unit = layout_unit;
  invalid_unit.initializer_elements = invalid_elements;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid initializer child") ||
      unit_snapshot_matches(&layout_snapshot, &layout_unit) == 0) {
    goto cleanup;
  }

  for (initializer_index = 0u;
       initializer_index < layout_unit.initializer_count;
       initializer_index++) {
    if (layout_unit.initializers[initializer_index].kind ==
            CTOOL_C_INITIALIZER_LIST &&
        layout_unit.initializers[initializer_index].element_count >= 2u) {
      duplicate_initializer = initializer_index;
      break;
    }
  }
  if (duplicate_initializer >= layout_unit.initializer_count) {
    (void)fprintf(stderr, "duplicate-selector fixture is absent\n");
    goto cleanup;
  }
  (void)memcpy(invalid_elements, layout_unit.initializer_elements,
               invalid_element_bytes);
  masked_edge =
      layout_unit.initializers[duplicate_initializer].first_element;
  if (layout_unit.initializer_element_count < 2u ||
      masked_edge > layout_unit.initializer_element_count - 2u) {
    (void)fprintf(stderr, "duplicate-selector edges are absent\n");
    goto cleanup;
  }
  invalid_elements[masked_edge + 1u].subobject =
      invalid_elements[masked_edge].subobject;
  invalid_unit = layout_unit;
  invalid_unit.initializer_elements = invalid_elements;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_EMIT_DIAG_INVALID_UNIT,
          "CupidC object emission received an invalid translation unit",
          "duplicate initializer selector") ||
      unit_snapshot_matches(&layout_snapshot, &layout_unit) == 0) {
    goto cleanup;
  }

  for (definition_index = 0u;
       definition_index < layout_unit.object_definition_count;
       definition_index++) {
    const ctool_c_object_definition_t *definition =
        &layout_unit.object_definitions[definition_index];
    if (definition->binding < layout_unit.binding_count) {
      const ctool_c_binding_t *binding =
          &layout_unit.bindings[definition->binding];
      if (string_equal(binding->name, "local_data") != 0) {
        wrong_child_type = binding->type;
      } else if (string_equal(binding->name, "masked_zero") != 0) {
        masked_initializer = definition->initializer;
      }
    }
  }
  if (masked_initializer >= layout_unit.initializer_count ||
      layout_unit.initializers[masked_initializer].kind !=
          CTOOL_C_INITIALIZER_LIST ||
      layout_unit.initializers[masked_initializer].element_count == 0u) {
    (void)fprintf(stderr, "BSS list fixture is absent\n");
    goto cleanup;
  }
  masked_edge = layout_unit.initializers[masked_initializer].first_element;
  if (masked_edge >= layout_unit.initializer_element_count) {
    (void)fprintf(stderr, "BSS list edge fixture is absent\n");
    goto cleanup;
  }
  masked_child = layout_unit.initializer_elements[masked_edge].initializer;
  if (masked_child >= layout_unit.initializer_count ||
      layout_unit.initializers[masked_child].kind !=
          CTOOL_C_INITIALIZER_INTEGER) {
    (void)fprintf(stderr, "BSS list child fixture is absent\n");
    goto cleanup;
  }
  (void)memcpy(invalid_layout_initializers, layout_unit.initializers,
               invalid_layout_initializer_bytes);
  invalid_layout_initializers[masked_child].integer_bits = 0u;
  (void)memcpy(invalid_elements, layout_unit.initializer_elements,
               invalid_element_bytes);
  invalid_elements[masked_edge].subobject = layout_unit.graph.member_count;
  invalid_unit = layout_unit;
  invalid_unit.initializers = invalid_layout_initializers;
  invalid_unit.initializer_elements = invalid_elements;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_EMIT_DIAG_INVALID_UNIT,
          "CupidC object emission received an invalid translation unit",
          "invalid BSS initializer subobject") ||
      unit_snapshot_matches(&layout_snapshot, &layout_unit) == 0) {
    goto cleanup;
  }

  if (wrong_child_type >= layout_unit.graph.type_count ||
      wrong_child_type ==
          layout_unit.initializers[masked_child].type) {
    (void)fprintf(stderr, "wrong-child-type fixture is absent\n");
    goto cleanup;
  }
  (void)memcpy(invalid_layout_initializers, layout_unit.initializers,
               invalid_layout_initializer_bytes);
  invalid_layout_initializers[masked_child].type = wrong_child_type;
  invalid_layout_initializers[masked_child].integer_bits = 0u;
  invalid_unit = layout_unit;
  invalid_unit.initializers = invalid_layout_initializers;
  if (!expect_object_failure(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_EMIT_DIAG_INVALID_UNIT,
          "CupidC object emission received an invalid translation unit",
          "wrong initializer child type") ||
      unit_snapshot_matches(&layout_snapshot, &layout_unit) == 0) {
    goto cleanup;
  }

  passed = 1;

cleanup:
  free(unsupported_expressions);
  free(invalid_elements);
  free(invalid_layout_initializers);
  free(invalid_bindings);
  free(invalid_block_bindings);
  free(invalid_initializers);
  free(invalid_definitions);
  free(expected_object);
  free(function_object);
  free(multiplication_object);
  free(division_object);
  free(branch_fit_object);
  free(aes_rotw_object);
  free(align_up_object);
  free(integer_unary_object);
  free(integer_cast_object);
  free(signed_bits_object);
  free(while_object);
  free(do_object);
  free(for_object);
  free(declaration_for_object);
  free(lexical_declaration_statements);
  free(unreachable_statements);
  free(loop_control_statements);
  free(simd_cpuid_object);
  free(file_member_object);
  free(bit_field_object);
  free(chained_assignment_object);
  dispose_unit_snapshot(&layout_snapshot);
  dispose_unit_snapshot(&bit_field_snapshot);
  dispose_unit_snapshot(&file_member_snapshot);
  dispose_unit_snapshot(&file_assignment_snapshot);
  dispose_unit_snapshot(&chained_assignment_snapshot);
  dispose_unit_snapshot(&division_snapshot);
  dispose_unit_snapshot(&branch_fit_snapshot);
  dispose_unit_snapshot(&aes_rotw_snapshot);
  dispose_unit_snapshot(&align_up_snapshot);
  dispose_unit_snapshot(&integer_unary_snapshot);
  dispose_unit_snapshot(&integer_cast_snapshot);
  dispose_unit_snapshot(&signed_bits_snapshot);
  dispose_unit_snapshot(&selection_edge_snapshot);
  dispose_unit_snapshot(&while_snapshot);
  dispose_unit_snapshot(&do_snapshot);
  dispose_unit_snapshot(&for_snapshot);
  dispose_unit_snapshot(&declaration_for_snapshot);
  dispose_unit_snapshot(&simd_cpuid_snapshot);
  dispose_unit_snapshot(&unsigned_multiplication_snapshot);
  dispose_unit_snapshot(&multiplication_snapshot);
  dispose_unit_snapshot(&external_object_snapshot);
  dispose_unit_snapshot(&function_snapshot);
  dispose_unit_snapshot(&snapshot);
  free(align_up_text);
  free(integer_cast_text);
  free(signed_bits_text);
  free(do_text);
  if (limited != (ctool_buffer_t *)0) {
    ctool_buffer_close(limited);
  }
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("static-data: ok");
    return 0;
  }
  return 1;
}

static int run_direct_goto(const char *host_root) {
  static const char source_text[] =
      "int object_goto(int value) {\n"
      "  if (value) goto done;\n"
      "  return 1;\n"
      "done:\n"
      "  return 2;\n"
      "}\n"
      "int object_backward(int value) {\n"
      "again:\n"
      "  if (value) {\n"
      "    value = value - 1;\n"
      "    goto again;\n"
      "  }\n"
      "  return value;\n"
      "}\n"
      "int object_terminal_if(void) {\n"
      "  goto inside_if;\n"
      "  if (1) {\n"
      "inside_if:\n"
      "    return 3;\n"
      "  }\n"
      "}\n"
      "int object_terminal_while(void) {\n"
      "  goto inside_while;\n"
      "  while (1) {\n"
      "inside_while:\n"
      "    return 4;\n"
      "  }\n"
      "}\n"
      "int object_label_declaration(int value) {\n"
      "  goto with_local;\n"
      "  return 9;\n"
      "with_local: {\n"
      "    int copy = value;\n"
      "    return copy;\n"
      "  }\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_statement_t *invalid_statements = NULL;
  void *invalid_statement_copy = NULL;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_arena_mark_t mark;
  ctool_bytes_t bytes;
  ctool_u8 *expected_object = NULL;
  ctool_u32 expected_object_size = 0u;
  ctool_u32 diagnostic_count;
  ctool_u32 goto_statement = CTOOL_C_AST_NONE;
  ctool_u32 statement_index;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_object_sources_are_unchanged(job) ||
      !parse_source(job, "/direct-goto.c", source_text, &unit) ||
      unit.function_definition_count != 5u || unit.label_count != 5u ||
      unit.block_binding_count != 1u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    (void)fprintf(stderr, "direct goto object setup failed\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "direct goto object buffers")) {
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first direct goto object") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "first direct goto emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  expected_object_size = bytes.size;
  expected_object = (ctool_u8 *)malloc((size_t)expected_object_size);
  if (expected_object == NULL) {
    (void)fprintf(stderr, "direct goto object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(expected_object, bytes.data, (size_t)bytes.size);

  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat direct goto object") ||
      bytes.size != expected_object_size ||
      memcmp(bytes.data, expected_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "direct goto emission is not deterministic\n");
    goto cleanup;
  }

  object_source.path.text = ctool_string("/direct-goto.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read direct goto object") ||
      !validate_direct_goto_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  if (unit.statement_count == 0u ||
      unit.function_definitions[0].body >= unit.statement_count ||
      copy_array(unit.statements, unit.statement_count,
                 sizeof(*unit.statements), &invalid_statement_copy) == 0) {
    (void)fprintf(stderr, "direct goto invalid fixture copy failed\n");
    goto cleanup;
  }
  invalid_statements = (ctool_c_statement_t *)invalid_statement_copy;
  for (statement_index = 0u;
       statement_index <= unit.function_definitions[0].body;
       statement_index++) {
    if (invalid_statements[statement_index].kind ==
        CTOOL_C_STATEMENT_GOTO) {
      goto_statement = statement_index;
      break;
    }
  }
  if (goto_statement == CTOOL_C_AST_NONE ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    (void)fprintf(stderr, "direct goto invalid fixture setup failed\n");
    goto cleanup;
  }
  invalid_statements[goto_statement].label =
      unit.function_definitions[1].first_label;
  invalid_unit = unit;
  invalid_unit.statements = invalid_statements;
  if (!expect_object_failure_preserves_unit(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "cross-function goto object") ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_statements);
  free(expected_object);
  dispose_unit_snapshot(&snapshot);
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("direct-goto: ok");
    return 0;
  }
  return 1;
}

static int run_switch_object(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_statement_t *invalid_statements = NULL;
  void *invalid_statement_copy = NULL;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_arena_mark_t mark;
  ctool_bytes_t bytes;
  ctool_u8 *expected_object = NULL;
  ctool_u32 expected_object_size = 0u;
  ctool_u32 diagnostic_count;
  ctool_u32 case_statement = CTOOL_C_AST_NONE;
  ctool_u32 statement_index;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_object_sources_are_unchanged(job) ||
      !parse_source(job, "/switch-object.c", switch_object_source, &unit) ||
      unit.function_definition_count != 1u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    (void)fprintf(stderr, "switch object setup failed\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "switch object buffers")) {
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first switch object") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "first switch object emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  expected_object_size = bytes.size;
  expected_object = (ctool_u8 *)malloc((size_t)expected_object_size);
  if (expected_object == NULL) {
    (void)fprintf(stderr, "switch object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(expected_object, bytes.data, (size_t)bytes.size);

  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat switch object") ||
      bytes.size != expected_object_size ||
      memcmp(bytes.data, expected_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "switch object emission is not deterministic\n");
    goto cleanup;
  }

  object_source.path.text = ctool_string("/switch-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read switch object") ||
      !validate_switch_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  if (copy_array(unit.statements, unit.statement_count,
                 sizeof(*unit.statements), &invalid_statement_copy) == 0) {
    (void)fprintf(stderr, "switch invalid fixture copy failed\n");
    goto cleanup;
  }
  invalid_statements = (ctool_c_statement_t *)invalid_statement_copy;
  for (statement_index = 0u; statement_index < unit.statement_count;
       statement_index++) {
    if (invalid_statements[statement_index].kind == CTOOL_C_STATEMENT_CASE) {
      case_statement = statement_index;
      break;
    }
  }
  if (case_statement == CTOOL_C_AST_NONE ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK) {
    (void)fprintf(stderr, "switch invalid fixture setup failed\n");
    goto cleanup;
  }
  invalid_statements[case_statement].expression = unit.expression_count;
  invalid_unit = unit;
  invalid_unit.statements = invalid_statements;
  if (!expect_object_failure_preserves_unit(
          job, &invalid_unit, second, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "malformed case object") ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_statements);
  free(expected_object);
  dispose_unit_snapshot(&snapshot);
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("switch-object: ok");
    return 0;
  }
  return 1;
}

static int run_integer_mutation_object(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_arena_mark_t mark;
  ctool_bytes_t bytes;
  ctool_u8 *expected_object = NULL;
  ctool_u32 expected_object_size = 0u;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/integer-mutation-object.c",
                    integer_mutation_object_source, &unit) ||
      unit.function_definition_count != 4u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    (void)fprintf(stderr, "integer mutation object setup failed\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "integer mutation object buffers")) {
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first integer mutation object") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "first integer mutation emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  expected_object_size = bytes.size;
  expected_object = (ctool_u8 *)malloc((size_t)expected_object_size);
  if (expected_object == NULL) {
    (void)fprintf(stderr,
                  "integer mutation object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(expected_object, bytes.data, (size_t)bytes.size);

  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat integer mutation object") ||
      bytes.size != expected_object_size ||
      memcmp(bytes.data, expected_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr,
                  "integer mutation emission is not deterministic\n");
    goto cleanup;
  }

  object_source.path.text = ctool_string("/integer-mutation-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read integer mutation object") ||
      !validate_integer_mutation_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(expected_object);
  dispose_unit_snapshot(&snapshot);
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("integer-mutation: ok");
    return 0;
  }
  return 1;
}

static int run_pointer_value_object(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_arena_mark_t mark;
  ctool_bytes_t bytes;
  ctool_u8 *expected_object = NULL;
  ctool_u32 expected_object_size = 0u;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/pointer-value-object.c",
                    pointer_value_object_source, &unit) ||
      unit.function_definition_count != 12u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    (void)fprintf(stderr, "pointer value object setup failed\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "pointer value object buffers")) {
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first pointer value object") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "first pointer value emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  expected_object_size = bytes.size;
  expected_object = (ctool_u8 *)malloc((size_t)expected_object_size);
  if (expected_object == NULL) {
    (void)fprintf(stderr, "pointer object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(expected_object, bytes.data, (size_t)bytes.size);

  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat pointer value object") ||
      bytes.size != expected_object_size ||
      memcmp(bytes.data, expected_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "pointer value emission is not deterministic\n");
    goto cleanup;
  }

  object_source.path.text = ctool_string("/pointer-value-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read pointer value object") ||
      !validate_pointer_value_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(expected_object);
  dispose_unit_snapshot(&snapshot);
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("pointer-values: ok");
    return 0;
  }
  return 1;
}

static int run_pointer_comparison_object(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_arena_mark_t mark;
  ctool_bytes_t bytes;
  ctool_u8 *expected_object = NULL;
  ctool_u32 expected_object_size = 0u;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/pointer-comparison-object.c",
                    pointer_comparison_object_source, &unit) ||
      unit.function_definition_count != 6u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    (void)fprintf(stderr, "pointer comparison object setup failed\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "pointer comparison object buffers")) {
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first pointer comparison object") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "first pointer comparison emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  expected_object_size = bytes.size;
  expected_object = (ctool_u8 *)malloc((size_t)expected_object_size);
  if (expected_object == NULL) {
    (void)fprintf(stderr, "pointer comparison snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(expected_object, bytes.data, (size_t)bytes.size);

  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat pointer comparison object") ||
      bytes.size != expected_object_size ||
      memcmp(bytes.data, expected_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr,
                  "pointer comparison emission is not deterministic\n");
    goto cleanup;
  }

  object_source.path.text = ctool_string("/pointer-comparison-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read pointer comparison object") ||
      !validate_pointer_comparison_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(expected_object);
  dispose_unit_snapshot(&snapshot);
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("pointer-comparisons: ok");
    return 0;
  }
  return 1;
}

static int run_pointer_condition_object(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_arena_mark_t mark;
  ctool_bytes_t bytes;
  ctool_u8 *expected_object = NULL;
  ctool_u32 expected_object_size = 0u;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/pointer-condition-object.c",
                    pointer_condition_object_source, &unit) ||
      unit.function_definition_count != 8u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    (void)fprintf(stderr, "pointer condition object setup failed\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "pointer condition object buffers")) {
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first pointer condition object") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "first pointer condition emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  expected_object_size = bytes.size;
  expected_object = (ctool_u8 *)malloc((size_t)expected_object_size);
  if (expected_object == NULL) {
    (void)fprintf(stderr, "pointer condition snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(expected_object, bytes.data, (size_t)bytes.size);

  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat pointer condition object") ||
      bytes.size != expected_object_size ||
      memcmp(bytes.data, expected_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "pointer condition emission is not deterministic\n");
    goto cleanup;
  }

  object_source.path.text = ctool_string("/pointer-condition-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read pointer condition object") ||
      !validate_pointer_condition_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(expected_object);
  dispose_unit_snapshot(&snapshot);
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("pointer-conditions: ok");
    return 0;
  }
  return 1;
}

static int run_pointer_arithmetic_object(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_arena_mark_t mark;
  ctool_bytes_t bytes;
  ctool_u8 *expected_object = NULL;
  ctool_u32 expected_object_size = 0u;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/pointer-arithmetic-object.c",
                    pointer_arithmetic_object_source, &unit) ||
      unit.function_definition_count != 19u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    (void)fprintf(stderr, "pointer arithmetic object setup failed\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "pointer arithmetic object buffers")) {
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first pointer arithmetic object") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "first pointer arithmetic emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  expected_object_size = bytes.size;
  expected_object = (ctool_u8 *)malloc((size_t)expected_object_size);
  if (expected_object == NULL) {
    (void)fprintf(stderr,
                  "pointer arithmetic snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(expected_object, bytes.data, (size_t)bytes.size);

  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat pointer arithmetic object") ||
      bytes.size != expected_object_size ||
      memcmp(bytes.data, expected_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr,
                  "pointer arithmetic emission is not deterministic\n");
    goto cleanup;
  }

  object_source.path.text = ctool_string("/pointer-arithmetic-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read pointer arithmetic object") ||
      !validate_pointer_arithmetic_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(expected_object);
  dispose_unit_snapshot(&snapshot);
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("pointer-arithmetic: ok");
    return 0;
  }
  return 1;
}

static int validate_function_pointer_call_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_text[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x8bu, 0x44u, 0x24u, 0x04u, 0xffu, 0xd0u, 0x83u,
      0xc4u, 0x08u, 0x50u, 0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x8bu, 0x44u, 0x24u, 0x04u, 0xffu, 0xd0u, 0x83u,
      0xc4u, 0x08u, 0x50u, 0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x8bu, 0x44u, 0x24u, 0x04u, 0xffu, 0xd0u, 0x83u,
      0xc4u, 0x08u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x8du, 0x85u, 0x10u, 0x00u, 0x00u, 0x00u, 0x50u,
      0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u, 0x14u, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8bu,
      0x4cu, 0x24u, 0x08u, 0x8bu, 0x14u, 0x24u, 0x89u, 0x54u,
      0x24u, 0x08u, 0x89u, 0x0cu, 0x24u, 0x83u, 0xecu, 0x08u,
      0x8bu, 0x4cu, 0x24u, 0x08u, 0x89u, 0x0cu, 0x24u, 0x8bu,
      0x4cu, 0x24u, 0x0cu, 0x89u, 0x4cu, 0x24u, 0x04u, 0x8bu,
      0x4cu, 0x24u, 0x10u, 0x89u, 0x4cu, 0x24u, 0x08u, 0x8bu,
      0x44u, 0x24u, 0x14u, 0xffu, 0xd0u, 0x83u, 0xc4u, 0x18u,
      0x50u, 0x58u, 0xc9u, 0xc3u};
  static const ctool_u32 relocation_offsets[] = {
      42u, 53u, 260u, 279u, 330u, 450u, 455u, 466u, 502u, 0u};
  static const ctool_u32 relocation_types[] = {
      CTOOL_ELF32_R_386_32,   CTOOL_ELF32_R_386_32,
      CTOOL_ELF32_R_386_32,   CTOOL_ELF32_R_386_32,
      CTOOL_ELF32_R_386_32,   CTOOL_ELF32_R_386_32,
      CTOOL_ELF32_R_386_32,   CTOOL_ELF32_R_386_32,
      CTOOL_ELF32_R_386_PC32, CTOOL_ELF32_R_386_32};
  static const ctool_i32 relocation_addends[] = {
      0, 0, 0, 0, 0, 0, 0, 0, -4, 0};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *data = find_section(object, ".data");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_section_t *rel_data = find_section(object, ".rel.data");
  const ctool_elf32_symbol_t *function = find_symbol(object, "invoke");
  const ctool_elf32_symbol_t *address =
      find_symbol(object, "target_address");
  const ctool_elf32_symbol_t *explicit_address =
      find_symbol(object, "explicit_target_address");
  const ctool_elf32_symbol_t *dereferenced =
      find_symbol(object, "invoke_dereferenced");
  const ctool_elf32_symbol_t *notify = find_symbol(object, "notify");
  const ctool_elf32_symbol_t *three = find_symbol(object, "invoke_three");
  const ctool_elf32_symbol_t *stored =
      find_symbol(object, "stored_callback");
  const ctool_elf32_symbol_t *use = find_symbol(object, "use_callback");
  const ctool_elf32_symbol_t *target = find_symbol(object, "target");
  const ctool_elf32_symbol_t *round_trip =
      find_symbol(object, "round_trip");
  const ctool_elf32_symbol_t *select =
      find_symbol(object, "select_callback");
  const ctool_elf32_symbol_t *equal =
      find_symbol(object, "callbacks_equal");
  const ctool_elf32_symbol_t *missing =
      find_symbol(object, "callback_missing");
  const ctool_elf32_symbol_t *present =
      find_symbol(object, "callback_present");
  const ctool_elf32_symbol_t *install =
      find_symbol(object, "install_target");
  const ctool_elf32_symbol_t *forward =
      find_symbol(object, "forward_callback");
  const ctool_elf32_symbol_t *relocation_symbols[] = {
      target, target, stored, stored, target,
      stored, target, stored, use,    target};
  ctool_u32 cursor = 0u;
  ctool_u32 indirect_call_count = 0u;
  ctool_u32 direct_call_count = 0u;
  ctool_u32 return_count = 0u;
  if (text == NULL || data == NULL || rel_text == NULL || rel_data == NULL ||
      function == NULL ||
      address == NULL || explicit_address == NULL || dereferenced == NULL ||
      notify == NULL || three == NULL || stored == NULL || use == NULL ||
      target == NULL || round_trip == NULL || select == NULL || equal == NULL ||
      missing == NULL || present == NULL || install == NULL || forward == NULL ||
      object->relocations == NULL || object->relocation_count != 10u ||
      text->relocation_count != 9u || data->relocation_count != 1u ||
      object->symbol_count != 17u || text->contents.size != 513u ||
      memcmp(text->contents.data, expected_text, sizeof(expected_text)) != 0 ||
      data->type != CTOOL_ELF32_SHT_PROGBITS || data->alignment != 4u ||
      data->size != 4u || data->contents.size != 4u ||
      data->contents.data == NULL || data->contents.data[0] != 0u ||
      data->contents.data[1] != 0u || data->contents.data[2] != 0u ||
      data->contents.data[3] != 0u ||
      function->type != CTOOL_ELF32_SYMBOL_FUNCTION ||
      function->section_file_index != text->file_index ||
      function->value != 0u || function->size != 38u ||
      !symbol_matches(address, address->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      38u, 11u) ||
      !symbol_matches(
          explicit_address, explicit_address->file_index,
          CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
          CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
          49u, 11u) ||
      !symbol_matches(
          dereferenced, dereferenced->file_index, CTOOL_ELF32_BIND_GLOBAL,
          CTOOL_ELF32_SYMBOL_FUNCTION, CTOOL_ELF32_SYMBOL_DEFINED,
          text->file_index, 60u, 38u) ||
      !symbol_matches(notify, notify->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      98u, 36u) ||
      !symbol_matches(three, three->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      134u, 100u) ||
      !symbol_matches(target, target->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u) ||
      !symbol_matches(stored, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 0u, 4u) ||
      !symbol_matches(use, use->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u) ||
      !symbol_matches(round_trip, round_trip->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 234u,
                      56u) ||
      !symbol_matches(select, select->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 290u,
                      47u) ||
      !symbol_matches(equal, equal->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 337u,
                      39u) ||
      !symbol_matches(missing, missing->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 376u,
                      33u) ||
      !symbol_matches(present, present->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 409u,
                      37u) ||
      !symbol_matches(install, install->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 446u,
                      31u) ||
      !symbol_matches(forward, forward->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 477u,
                      36u)) {
    (void)fprintf(stderr, "function pointer object inventory differs\n");
    return 0;
  }
  for (cursor = 0u; cursor < object->relocation_count; cursor++) {
    const ctool_elf32_relocation_t *relocation = &object->relocations[cursor];
    const ctool_elf32_section_t *target_section =
        cursor < 9u ? text : data;
    const ctool_elf32_section_t *relocation_section =
        cursor < 9u ? rel_text : rel_data;
    if (relocation_symbols[cursor] == NULL ||
        relocation->relocation_section_file_index !=
            relocation_section->file_index ||
        relocation->entry_index != (cursor < 9u ? cursor : 0u) ||
        relocation->target_section_file_index != target_section->file_index ||
        relocation->offset != relocation_offsets[cursor] ||
        relocation->symbol_file_index !=
            relocation_symbols[cursor]->file_index ||
        relocation->type != relocation_types[cursor] ||
        relocation->addend_known != CTOOL_TRUE ||
        relocation->addend != relocation_addends[cursor]) {
      (void)fprintf(stderr, "function pointer relocation %u differs\n",
                    (unsigned int)cursor);
      return 0;
    }
  }
  cursor = 0u;
  while (cursor < text->contents.size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + cursor, text->contents.size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr,
                    "function pointer object decode failed at %u (0x%02x)\n",
                    (unsigned int)cursor,
                    (unsigned int)text->contents.data[cursor]);
      return 0;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_CALL) {
      if (decoded.instruction.operand_count != 1u) {
        (void)fprintf(stderr, "function pointer call operand differs\n");
        return 0;
      }
      if (decoded.instruction.operands[0].kind ==
          CTOOL_X86_OPERAND_REGISTER) {
        if (decoded.instruction.operands[0].as.reg.class_id !=
                CTOOL_X86_REG_GPR32 ||
            decoded.instruction.operands[0].as.reg.index != 0u) {
          (void)fprintf(stderr, "indirect callback operand differs\n");
          return 0;
        }
        indirect_call_count++;
      } else if (decoded.instruction.operands[0].kind ==
                 CTOOL_X86_OPERAND_RELATIVE) {
        direct_call_count++;
      } else {
        (void)fprintf(stderr, "function pointer call form differs\n");
        return 0;
      }
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      return_count++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != text->contents.size || indirect_call_count != 4u ||
      direct_call_count != 1u || return_count != 13u) {
    (void)fprintf(stderr, "function pointer instruction inventory differs\n");
    return 0;
  }
  return 1;
}

static int validate_local_function_address_object(
    const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_text[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x58u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x68u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x58u, 0xc9u, 0xc3u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *target =
      find_symbol(object, "local_target");
  const ctool_elf32_symbol_t *address =
      find_symbol(object, "local_target_address");
  const ctool_elf32_relocation_t *relocation;
  if (text == NULL || rel_text == NULL || target == NULL || address == NULL ||
      object->symbol_count != 3u || object->relocation_count != 1u ||
      text->relocation_count != 1u || text->contents.size != 28u ||
      memcmp(text->contents.data, expected_text, sizeof(expected_text)) != 0 ||
      !symbol_matches(target, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      0u, 17u) ||
      !symbol_matches(address, address->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      17u, 11u)) {
    (void)fprintf(stderr, "local function address inventory differs\n");
    return 0;
  }
  relocation = &object->relocations[0];
  if (relocation->relocation_section_file_index != rel_text->file_index ||
      relocation->entry_index != 0u ||
      relocation->target_section_file_index != text->file_index ||
      relocation->offset != 21u ||
      relocation->symbol_file_index != target->file_index ||
      relocation->type != CTOOL_ELF32_R_386_32 ||
      relocation->addend_known != CTOOL_TRUE || relocation->addend != 0) {
    (void)fprintf(stderr, "local function address relocation differs\n");
    return 0;
  }
  return 1;
}

static int run_function_pointer_object(const char *host_root) {
  static const char source_text[] =
      "typedef int (*callback_t)(int);\n"
      "extern int target(int);\n"
      "int invoke(callback_t callback, int value) { return callback(value); }\n"
      "callback_t target_address(void) { return target; }\n"
      "callback_t explicit_target_address(void) { return &target; }\n"
      "int invoke_dereferenced(callback_t callback, int value) {\n"
      "  return (*callback)(value);\n"
      "}\n"
      "typedef void (*notify_t)(int);\n"
      "void notify(notify_t callback, int value) { callback(value); }\n"
      "typedef int (*combine_t)(int, int, int);\n"
      "int invoke_three(combine_t callback, int a, int b, int c) {\n"
      "  return callback(a, b, c);\n"
      "}\n"
      "extern int use_callback(callback_t);\n"
      "static callback_t stored_callback = target;\n"
      "callback_t round_trip(callback_t callback) {\n"
      "  callback_t local = callback;\n"
      "  stored_callback = local;\n"
      "  return stored_callback;\n"
      "}\n"
      "callback_t select_callback(callback_t callback) {\n"
      "  return callback ? callback : target;\n"
      "}\n"
      "int callbacks_equal(callback_t left, callback_t right) {\n"
      "  return left == right;\n"
      "}\n"
      "int callback_missing(callback_t callback) { return callback == 0; }\n"
      "int callback_present(callback_t callback) { return !!callback; }\n"
      "callback_t install_target(void) {\n"
      "  stored_callback = target;\n"
      "  return stored_callback;\n"
      "}\n"
      "int forward_callback(callback_t callback) {\n"
      "  return use_callback(callback);\n"
      "}\n";
  static const char atomic_source[] =
      "typedef int (*callback_t)(int);\n"
      "callback_t _Atomic shared_callback;\n"
      "int callback_is_set(void) { return shared_callback != 0; }\n";
  static const char local_address_source[] =
      "typedef int (*callback_t)(int);\n"
      "static int local_target(int value) { return value; }\n"
      "callback_t local_target_address(void) { return local_target; }\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t atomic_unit;
  ctool_c_translation_unit_t local_address_unit;
  unit_snapshot_t local_address_snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t bytes;
  ctool_u8 *first_object = NULL;
  ctool_u32 first_object_size = 0u;
  ctool_arena_mark_t mark;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&atomic_unit, 0, sizeof(atomic_unit));
  (void)memset(&local_address_unit, 0, sizeof(local_address_unit));
  (void)memset(&local_address_snapshot, 0, sizeof(local_address_snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_object_sources_are_unchanged(job) ||
      !parse_source(job, "/function-pointer-object.c", source_text, &unit)) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "function pointer object buffer")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, output);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "function pointer object emission") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  first_object_size = bytes.size;
  first_object = (ctool_u8 *)malloc((size_t)first_object_size);
  if (first_object == NULL) {
    (void)fprintf(stderr, "function pointer object copy failed\n");
    goto cleanup;
  }
  (void)memcpy(first_object, bytes.data, (size_t)first_object_size);
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK) {
    (void)fprintf(stderr, "function pointer object rewind failed\n");
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, output);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "repeat function pointer object") ||
      bytes.size != first_object_size ||
      memcmp(bytes.data, first_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0) {
    (void)fprintf(stderr, "function pointer emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/function-pointer-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read function pointer object") ||
      !validate_function_pointer_call_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !parse_source(job, "/local-function-address-object.c",
                    local_address_source, &local_address_unit) ||
      !take_unit_snapshot(&local_address_unit, &local_address_snapshot)) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &local_address_unit, output);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "local function address object") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(
          &local_address_snapshot, &local_address_unit) == 0) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  object_source.path.text =
      ctool_string("/local-function-address-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read local function address object") ||
      !validate_local_function_address_object(&object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !parse_source(job, "/atomic-function-pointer-object.c", atomic_source,
                    &atomic_unit) ||
      !expect_object_failure_preserves_unit(
          job, &atomic_unit, output, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic function pointer object")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(first_object);
  dispose_unit_snapshot(&local_address_snapshot);
  if (output != (ctool_buffer_t *)0) {
    ctool_buffer_close(output);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("function-pointers: ok");
    return 0;
  }
  return 1;
}

typedef enum {
  AGGREGATE_SYMBOLIC_UNKNOWN = 0,
  AGGREGATE_SYMBOLIC_FRAME_ADDRESS,
  AGGREGATE_SYMBOLIC_CONSTANT,
  AGGREGATE_SYMBOLIC_CALL
} aggregate_symbolic_kind_t;

typedef struct {
  aggregate_symbolic_kind_t kind;
  ctool_i32 frame_offset;
  ctool_u32 bits;
} aggregate_symbolic_value_t;

typedef struct {
  ctool_i32 relative_offset;
  aggregate_symbolic_kind_t value_kind;
  ctool_u32 value_bits;
} aggregate_expected_store_t;

#define AGGREGATE_SYMBOLIC_STACK_LIMIT 128u

static aggregate_symbolic_value_t aggregate_unknown_value(void) {
  aggregate_symbolic_value_t value;
  (void)memset(&value, 0, sizeof(value));
  value.kind = AGGREGATE_SYMBOLIC_UNKNOWN;
  return value;
}

static int aggregate_gpr_index(ctool_x86_reg_t reg, ctool_u32 *index_out) {
  if (reg.class_id != CTOOL_X86_REG_GPR32 || reg.index >= 8u) {
    return 0;
  }
  *index_out = reg.index;
  return 1;
}

static int aggregate_register_is(ctool_x86_reg_t reg, ctool_u32 index) {
  return reg.class_id == CTOOL_X86_REG_GPR32 && reg.index == index ? 1 : 0;
}

static int aggregate_memory_frame_offset(
    const ctool_x86_memory_t *memory,
    const aggregate_symbolic_value_t registers[8],
    ctool_i32 *offset_out) {
  int64_t offset;
  ctool_u32 base_index;
  if (memory == NULL || memory->address_bits != 32u ||
      memory->index.class_id != CTOOL_X86_REG_NONE ||
      memory->displacement.kind != CTOOL_X86_VALUE_CONSTANT) {
    return 0;
  }
  if (aggregate_register_is(memory->base, 5u)) {
    offset = 0;
  } else if (aggregate_gpr_index(memory->base, &base_index) != 0 &&
             registers[base_index].kind ==
                 AGGREGATE_SYMBOLIC_FRAME_ADDRESS) {
    offset = registers[base_index].frame_offset;
  } else {
    return 0;
  }
  offset += (ctool_i32)memory->displacement.bits;
  if (offset < INT32_MIN || offset > INT32_MAX) {
    return 0;
  }
  *offset_out = (ctool_i32)offset;
  return 1;
}

static aggregate_symbolic_value_t aggregate_read_operand(
    const ctool_x86_operand_t *operand,
    const aggregate_symbolic_value_t registers[8]) {
  aggregate_symbolic_value_t value = aggregate_unknown_value();
  ctool_u32 index;
  if (operand == NULL) {
    return value;
  }
  if (operand->kind == CTOOL_X86_OPERAND_REGISTER &&
      aggregate_gpr_index(operand->as.reg, &index) != 0) {
    return registers[index];
  }
  if (operand->kind == CTOOL_X86_OPERAND_IMMEDIATE &&
      operand->as.value.kind == CTOOL_X86_VALUE_CONSTANT) {
    value.kind = AGGREGATE_SYMBOLIC_CONSTANT;
    value.bits = operand->as.value.bits;
  }
  return value;
}

static int aggregate_adjust_symbolic_value(aggregate_symbolic_value_t *value,
                                           ctool_i32 amount) {
  int64_t adjusted;
  if (value->kind == AGGREGATE_SYMBOLIC_FRAME_ADDRESS) {
    adjusted = (int64_t)value->frame_offset + (int64_t)amount;
    if (adjusted < INT32_MIN || adjusted > INT32_MAX) {
      return 0;
    }
    value->frame_offset = (ctool_i32)adjusted;
    return 1;
  }
  if (value->kind == AGGREGATE_SYMBOLIC_CONSTANT) {
    value->bits += (ctool_u32)amount;
    return 1;
  }
  *value = aggregate_unknown_value();
  return 1;
}

static int validate_aggregate_initializer_function(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *function, ctool_u32 expected_zero_bytes,
    const aggregate_expected_store_t *expected_stores,
    ctool_u32 expected_store_count, ctool_u32 expected_call_count,
    const char *context) {
  aggregate_symbolic_value_t registers[8];
  aggregate_symbolic_value_t stack[AGGREGATE_SYMBOLIC_STACK_LIMIT];
  ctool_u32 stack_depth = 0u;
  ctool_u32 cursor = 0u;
  ctool_u32 call_count = 0u;
  ctool_u32 store_count = 0u;
  ctool_u32 zero_count = 0u;
  ctool_u32 edi_push_count = 0u;
  ctool_u32 edi_pop_count = 0u;
  ctool_u32 edi_push_cursor = CTOOL_C_AST_NONE;
  ctool_u32 edi_pop_cursor = CTOOL_C_AST_NONE;
  ctool_u32 stos_cursor = CTOOL_C_AST_NONE;
  ctool_i32 zero_start = 0;
  int cld_ready = 0;
  ctool_u32 index;
  if (text == NULL || function == NULL || text->contents.data == NULL ||
      function->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      function->section_file_index != text->file_index ||
      function->value > text->contents.size ||
      function->size > text->contents.size - function->value) {
    (void)fprintf(stderr, "%s: function range differs\n", context);
    return 0;
  }
  for (index = 0u; index < 8u; index++) {
    registers[index] = aggregate_unknown_value();
  }
  while (cursor < function->size) {
    ctool_x86_decoded_t decoded;
    const ctool_x86_instruction_t *instruction;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + function->value + cursor,
        function->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "%s: decode failed at byte %u\n", context,
                    cursor);
      return 0;
    }
    instruction = &decoded.instruction;
    if (instruction->mnemonic == CTOOL_X86_MN_PUSH &&
        instruction->operand_count == 1u) {
      if (stack_depth >= AGGREGATE_SYMBOLIC_STACK_LIMIT) {
        (void)fprintf(stderr, "%s: symbolic stack overflowed\n", context);
        return 0;
      }
      stack[stack_depth++] =
          aggregate_read_operand(&instruction->operands[0], registers);
      if (instruction->operands[0].kind ==
              CTOOL_X86_OPERAND_REGISTER &&
          aggregate_register_is(instruction->operands[0].as.reg, 7u)) {
        edi_push_count++;
        if (edi_push_cursor == CTOOL_C_AST_NONE) {
          edi_push_cursor = cursor;
        }
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_POP &&
               instruction->operand_count == 1u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      ctool_u32 destination;
      if (stack_depth == 0u ||
          aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) == 0) {
        (void)fprintf(stderr, "%s: symbolic stack underflowed\n", context);
        return 0;
      }
      registers[destination] = stack[--stack_depth];
      if (destination == 7u) {
        edi_pop_count++;
        edi_pop_cursor = cursor;
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_LEA &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER &&
               instruction->operands[1].kind == CTOOL_X86_OPERAND_MEMORY) {
      ctool_u32 destination;
      ctool_i32 frame_offset;
      if (aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) == 0) {
        return 0;
      }
      if (aggregate_memory_frame_offset(
              &instruction->operands[1].as.memory, registers,
              &frame_offset) != 0) {
        registers[destination].kind = AGGREGATE_SYMBOLIC_FRAME_ADDRESS;
        registers[destination].frame_offset = frame_offset;
        registers[destination].bits = 0u;
      } else {
        registers[destination] = aggregate_unknown_value();
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
               instruction->operand_count == 2u) {
      const ctool_x86_operand_t *destination = &instruction->operands[0];
      const ctool_x86_operand_t *source = &instruction->operands[1];
      if (destination->kind == CTOOL_X86_OPERAND_REGISTER) {
        ctool_u32 destination_index;
        if (aggregate_gpr_index(destination->as.reg,
                                &destination_index) != 0) {
          registers[destination_index] =
              aggregate_read_operand(source, registers);
        }
      } else if (destination->kind == CTOOL_X86_OPERAND_MEMORY) {
        aggregate_symbolic_value_t stored =
            aggregate_read_operand(source, registers);
        ctool_i32 frame_offset;
        if ((stored.kind == AGGREGATE_SYMBOLIC_CALL ||
             stored.kind == AGGREGATE_SYMBOLIC_CONSTANT) &&
            aggregate_memory_frame_offset(&destination->as.memory,
                                          registers, &frame_offset) != 0) {
          int64_t relative =
              (int64_t)frame_offset - (int64_t)zero_start;
          if (zero_count != 1u || store_count >= expected_store_count ||
              destination->width_bits != 32u || relative < INT32_MIN ||
              relative > INT32_MAX ||
              expected_stores[store_count].relative_offset !=
                  (ctool_i32)relative ||
              expected_stores[store_count].value_kind != stored.kind ||
              expected_stores[store_count].value_bits != stored.bits) {
            (void)fprintf(stderr,
                          "%s: scalar store %u differs at frame offset %d\n",
                          context, store_count, frame_offset);
            return 0;
          }
          store_count++;
        }
      }
    } else if ((instruction->mnemonic == CTOOL_X86_MN_ADD ||
                instruction->mnemonic == CTOOL_X86_MN_SUB) &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      ctool_u32 destination;
      aggregate_symbolic_value_t amount =
          aggregate_read_operand(&instruction->operands[1], registers);
      if (aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) != 0) {
        if (instruction->mnemonic == CTOOL_X86_MN_SUB &&
            instruction->operands[1].kind ==
                CTOOL_X86_OPERAND_REGISTER &&
            aggregate_register_is(instruction->operands[1].as.reg,
                                  destination)) {
          registers[destination].kind = AGGREGATE_SYMBOLIC_CONSTANT;
          registers[destination].bits = 0u;
        } else if (amount.kind == AGGREGATE_SYMBOLIC_CONSTANT &&
                   aggregate_adjust_symbolic_value(
                       &registers[destination],
                       instruction->mnemonic == CTOOL_X86_MN_ADD
                           ? (ctool_i32)amount.bits
                           : -(ctool_i32)amount.bits) == 0) {
          return 0;
        } else if (amount.kind != AGGREGATE_SYMBOLIC_CONSTANT) {
          registers[destination] = aggregate_unknown_value();
        }
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_XOR &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER &&
               instruction->operands[1].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      ctool_u32 destination;
      if (aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) != 0) {
        if (aggregate_register_is(instruction->operands[1].as.reg,
                                  destination)) {
          registers[destination].kind = AGGREGATE_SYMBOLIC_CONSTANT;
          registers[destination].bits = 0u;
        } else {
          registers[destination] = aggregate_unknown_value();
        }
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_CALL) {
      if (zero_count != 1u || call_count >= expected_call_count) {
        (void)fprintf(stderr, "%s: call ordering differs\n", context);
        return 0;
      }
      call_count++;
      registers[0].kind = AGGREGATE_SYMBOLIC_CALL;
      registers[0].bits = call_count;
      registers[1] = aggregate_unknown_value();
      registers[2] = aggregate_unknown_value();
    } else if (instruction->mnemonic == CTOOL_X86_MN_CLD) {
      cld_ready = 1;
    } else if (instruction->mnemonic == CTOOL_X86_MN_STOSB ||
               instruction->mnemonic == CTOOL_X86_MN_STOSW ||
               instruction->mnemonic == CTOOL_X86_MN_STOSD) {
      int64_t zero_bytes;
      ctool_u32 zero_width =
          instruction->mnemonic == CTOOL_X86_MN_STOSB
              ? 1u
              : (instruction->mnemonic == CTOOL_X86_MN_STOSW ? 2u : 4u);
      if (instruction->prefixes != CTOOL_X86_PREFIX_REP || cld_ready == 0 ||
          zero_count != 0u ||
          registers[7].kind != AGGREGATE_SYMBOLIC_FRAME_ADDRESS ||
          registers[1].kind != AGGREGATE_SYMBOLIC_CONSTANT ||
          registers[0].kind != AGGREGATE_SYMBOLIC_CONSTANT ||
          registers[0].bits != 0u) {
        (void)fprintf(stderr, "%s: REP STOS zeroing shape differs\n",
                      context);
        return 0;
      }
      zero_bytes = (int64_t)registers[1].bits * zero_width;
      if (zero_bytes != expected_zero_bytes) {
        (void)fprintf(stderr,
                      "%s: expected %u zeroed bytes, got %lld\n", context,
                      expected_zero_bytes, (long long)zero_bytes);
        return 0;
      }
      zero_start = registers[7].frame_offset;
      stos_cursor = cursor;
      zero_count++;
      cld_ready = 0;
      if (aggregate_adjust_symbolic_value(
              &registers[7], (ctool_i32)expected_zero_bytes) == 0) {
        return 0;
      }
      registers[1].bits = 0u;
    }
    cursor += decoded.consumed;
  }
  if (cursor != function->size || zero_count != 1u ||
      store_count != expected_store_count || call_count != expected_call_count ||
      edi_push_count != 1u || edi_pop_count != 1u ||
      edi_push_cursor == CTOOL_C_AST_NONE ||
      edi_pop_cursor == CTOOL_C_AST_NONE || stos_cursor == CTOOL_C_AST_NONE ||
      edi_push_cursor >= stos_cursor || edi_pop_cursor <= stos_cursor) {
    (void)fprintf(stderr, "%s: initializer instruction inventory differs\n",
                  context);
    return 0;
  }
  return 1;
}

static int validate_aggregate_initializer_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const char *const call_names[] = {"first_leaf", "second_leaf",
                                           "third_leaf", "fourth_leaf"};
  static const aggregate_expected_store_t path_stores[] = {
      {32, AGGREGATE_SYMBOLIC_CALL, 1u},
      {24, AGGREGATE_SYMBOLIC_CALL, 2u},
      {12, AGGREGATE_SYMBOLIC_CALL, 3u},
      {0, AGGREGATE_SYMBOLIC_CALL, 4u}};
  static const aggregate_expected_store_t zero_record_stores[] = {
      {0, AGGREGATE_SYMBOLIC_CONSTANT, 0u}};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *paths =
      find_symbol(object, "aggregate_paths");
  const ctool_elf32_symbol_t *zero_record =
      find_symbol(object, "active_zero_record");
  const ctool_elf32_symbol_t *call_symbols[4];
  ctool_u32 index;
  if (text == NULL || rel_text == NULL || paths == NULL ||
      zero_record == NULL || text->contents.data == NULL ||
      object->symbol_count != 7u || object->relocation_count != 4u ||
      text->relocation_count != 4u || object->relocations == NULL ||
      !symbol_matches(paths, 5u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      paths->size) ||
      !symbol_matches(zero_record, 6u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      paths->size, zero_record->size) ||
      paths->size == 0u || zero_record->size == 0u ||
      text->contents.size != paths->size + zero_record->size) {
    (void)fprintf(stderr, "aggregate initializer object inventory differs\n");
    return 0;
  }
  for (index = 0u; index < 4u; index++) {
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[index];
    call_symbols[index] = find_symbol(object, call_names[index]);
    if (!symbol_matches(call_symbols[index], index + 1u,
                        CTOOL_ELF32_BIND_GLOBAL,
                        CTOOL_ELF32_SYMBOL_FUNCTION,
                        CTOOL_ELF32_SYMBOL_UNDEFINED,
                        CTOOL_ELF32_NO_SECTION, 0u, 0u) ||
        relocation->relocation_section_file_index != rel_text->file_index ||
        relocation->entry_index != index ||
        relocation->target_section_file_index != text->file_index ||
        relocation->offset <= paths->value ||
        relocation->offset > paths->value + paths->size - 4u ||
        relocation->symbol_file_index != call_symbols[index]->file_index ||
        relocation->type != CTOOL_ELF32_R_386_PC32 ||
        relocation->addend_known != CTOOL_TRUE || relocation->addend != -4 ||
        (index != 0u &&
         relocation->offset <= object->relocations[index - 1u].offset)) {
      (void)fprintf(stderr,
                    "aggregate initializer call relocation %u differs\n",
                    index);
      return 0;
    }
  }
  return validate_aggregate_initializer_function(
             job, text, paths, 40u, path_stores,
             (ctool_u32)(sizeof(path_stores) / sizeof(path_stores[0])), 4u,
             "aggregate_paths") &&
         validate_aggregate_initializer_function(
             job, text, zero_record, 16u, zero_record_stores,
             (ctool_u32)(sizeof(zero_record_stores) /
                         sizeof(zero_record_stores[0])),
             0u, "active_zero_record");
}

static int validate_runtime_string_function(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *function, ctool_u32 expected_zero_bytes,
    ctool_u32 expected_copy_bytes, const char *context) {
  aggregate_symbolic_value_t registers[8];
  aggregate_symbolic_value_t stack[AGGREGATE_SYMBOLIC_STACK_LIMIT];
  ctool_u32 stack_depth = 0u;
  ctool_u32 cursor = 0u;
  ctool_u32 zero_count = 0u;
  ctool_u32 copy_count = 0u;
  ctool_u32 return_count = 0u;
  ctool_u32 edi_push_count = 0u;
  ctool_u32 edi_pop_count = 0u;
  ctool_u32 esi_push_count = 0u;
  ctool_u32 esi_pop_count = 0u;
  int cld_ready = 0;
  ctool_u32 index;
  if (job == (ctool_job_t *)0 || text == NULL || function == NULL ||
      text->contents.data == NULL ||
      function->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      function->section_file_index != text->file_index ||
      function->value > text->contents.size ||
      function->size > text->contents.size - function->value) {
    return 0;
  }
  for (index = 0u; index < 8u; index++) {
    registers[index] = aggregate_unknown_value();
  }
  while (cursor < function->size) {
    ctool_x86_decoded_t decoded;
    const ctool_x86_instruction_t *instruction;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + function->value + cursor,
        function->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "%s: decode failed at byte %u\n", context,
                    cursor);
      return 0;
    }
    instruction = &decoded.instruction;
    if (instruction->mnemonic == CTOOL_X86_MN_PUSH &&
        instruction->operand_count == 1u) {
      if (stack_depth >= AGGREGATE_SYMBOLIC_STACK_LIMIT) {
        (void)fprintf(stderr, "%s: symbolic stack overflowed\n", context);
        return 0;
      }
      stack[stack_depth++] =
          aggregate_read_operand(&instruction->operands[0], registers);
      if (instruction->operands[0].kind ==
          CTOOL_X86_OPERAND_REGISTER) {
        if (aggregate_register_is(instruction->operands[0].as.reg, 7u)) {
          edi_push_count++;
        } else if (aggregate_register_is(instruction->operands[0].as.reg,
                                         6u)) {
          esi_push_count++;
        }
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_POP &&
               instruction->operand_count == 1u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      ctool_u32 destination;
      if (stack_depth == 0u ||
          aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) == 0) {
        (void)fprintf(stderr, "%s: symbolic stack underflowed\n", context);
        return 0;
      }
      registers[destination] = stack[--stack_depth];
      if (destination == 7u) {
        edi_pop_count++;
      } else if (destination == 6u) {
        esi_pop_count++;
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_LEA &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER &&
               instruction->operands[1].kind == CTOOL_X86_OPERAND_MEMORY) {
      ctool_u32 destination;
      ctool_i32 frame_offset;
      if (aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) == 0) {
        return 0;
      }
      if (aggregate_memory_frame_offset(
              &instruction->operands[1].as.memory, registers,
              &frame_offset) != 0) {
        registers[destination].kind = AGGREGATE_SYMBOLIC_FRAME_ADDRESS;
        registers[destination].frame_offset = frame_offset;
        registers[destination].bits = 0u;
      } else {
        registers[destination] = aggregate_unknown_value();
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      ctool_u32 destination;
      if (aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) != 0) {
        registers[destination] =
            aggregate_read_operand(&instruction->operands[1], registers);
      }
    } else if ((instruction->mnemonic == CTOOL_X86_MN_ADD ||
                instruction->mnemonic == CTOOL_X86_MN_SUB) &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      ctool_u32 destination;
      aggregate_symbolic_value_t amount =
          aggregate_read_operand(&instruction->operands[1], registers);
      if (aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) != 0) {
        if (instruction->mnemonic == CTOOL_X86_MN_SUB &&
            instruction->operands[1].kind ==
                CTOOL_X86_OPERAND_REGISTER &&
            aggregate_register_is(instruction->operands[1].as.reg,
                                  destination)) {
          registers[destination].kind = AGGREGATE_SYMBOLIC_CONSTANT;
          registers[destination].bits = 0u;
        } else if (amount.kind == AGGREGATE_SYMBOLIC_CONSTANT) {
          if (aggregate_adjust_symbolic_value(
                  &registers[destination],
                  instruction->mnemonic == CTOOL_X86_MN_ADD
                      ? (ctool_i32)amount.bits
                      : -(ctool_i32)amount.bits) == 0) {
            return 0;
          }
        } else {
          registers[destination] = aggregate_unknown_value();
        }
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_XOR &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER &&
               instruction->operands[1].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      ctool_u32 destination;
      if (aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) != 0) {
        if (aggregate_register_is(instruction->operands[1].as.reg,
                                  destination)) {
          registers[destination].kind = AGGREGATE_SYMBOLIC_CONSTANT;
          registers[destination].bits = 0u;
        } else {
          registers[destination] = aggregate_unknown_value();
        }
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_CLD) {
      cld_ready = 1;
    } else if (instruction->mnemonic == CTOOL_X86_MN_STOSB) {
      if (instruction->prefixes != CTOOL_X86_PREFIX_REP || cld_ready == 0 ||
          expected_zero_bytes == 0u || zero_count != 0u ||
          registers[7].kind != AGGREGATE_SYMBOLIC_FRAME_ADDRESS ||
          registers[1].kind != AGGREGATE_SYMBOLIC_CONSTANT ||
          registers[1].bits != expected_zero_bytes ||
          registers[0].kind != AGGREGATE_SYMBOLIC_CONSTANT ||
          registers[0].bits != 0u) {
        (void)fprintf(stderr, "%s: string destination zeroing differs\n",
                      context);
        return 0;
      }
      zero_count++;
      cld_ready = 0;
      registers[1].bits = 0u;
    } else if (instruction->mnemonic == CTOOL_X86_MN_MOVSB) {
      if (instruction->prefixes != CTOOL_X86_PREFIX_REP || cld_ready == 0 ||
          expected_copy_bytes == 0u || copy_count != 0u ||
          registers[1].kind != AGGREGATE_SYMBOLIC_CONSTANT ||
          registers[1].bits != expected_copy_bytes ||
          (expected_zero_bytes != 0u && zero_count != 1u)) {
        (void)fprintf(stderr, "%s: retained string copy differs\n", context);
        return 0;
      }
      copy_count++;
      cld_ready = 0;
      registers[1].bits = 0u;
    } else if (instruction->mnemonic == CTOOL_X86_MN_RET) {
      return_count++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != function->size || return_count != 1u ||
      zero_count != (expected_zero_bytes == 0u ? 0u : 1u) ||
      copy_count != (expected_copy_bytes == 0u ? 0u : 1u) ||
      edi_push_count != zero_count + copy_count ||
      edi_pop_count != edi_push_count || esi_push_count != copy_count ||
      esi_pop_count != esi_push_count) {
    (void)fprintf(stderr, "%s: runtime string inventory differs\n",
                  context);
    return 0;
  }
  return 1;
}

static int validate_runtime_string_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_rodata[] = {
      'a', 'b', 'c', 0u, 'h', 'i', 0u, 'C', 'u', 'p',
      'i', 'd', 0u, 'C', 'u', 'p', 'i', 'd', 0u};
  static const char *const function_names[] = {
      "string_leaf", "string_tail", "string_address", "string_compound"};
  static const char *const literal_names[] = {
      ".LC0", ".LC1", ".LC2", ".LC3"};
  static const ctool_u32 literal_offsets[] = {0u, 4u, 7u, 13u};
  static const ctool_u32 literal_sizes[] = {4u, 3u, 6u, 6u};
  static const ctool_u32 zero_sizes[] = {8u, 6u, 0u, 6u};
  static const ctool_u32 copy_sizes[] = {4u, 3u, 0u, 6u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rodata = find_section(object, ".rodata");
  const ctool_elf32_section_t *rel_text =
      find_section(object, ".rel.text");
  ctool_u32 index;
  if (job == (ctool_job_t *)0 || text == NULL || rodata == NULL ||
      rel_text == NULL || text->contents.data == NULL ||
      rodata->contents.data == NULL || object->symbol_count != 9u ||
      object->relocation_count != 4u || object->relocations == NULL ||
      text->relocation_count != 4u ||
      rodata->contents.size != (ctool_u32)sizeof(expected_rodata) ||
      memcmp(rodata->contents.data, expected_rodata,
             sizeof(expected_rodata)) != 0) {
    (void)fprintf(stderr, "runtime string object inventory differs\n");
    return 0;
  }
  for (index = 0u; index < 4u; index++) {
    const ctool_elf32_symbol_t *function =
        find_symbol(object, function_names[index]);
    const ctool_elf32_symbol_t *literal =
        find_symbol(object, literal_names[index]);
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[index];
    if (function == NULL || literal == NULL || function->size == 0u ||
        function->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        function->section_file_index != text->file_index ||
        literal->binding != CTOOL_ELF32_BIND_LOCAL ||
        literal->type != CTOOL_ELF32_SYMBOL_OBJECT ||
        literal->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        literal->section_file_index != rodata->file_index ||
        literal->value != literal_offsets[index] ||
        literal->size != literal_sizes[index] ||
        relocation->relocation_section_file_index != rel_text->file_index ||
        relocation->entry_index != index ||
        relocation->target_section_file_index != text->file_index ||
        relocation->offset < function->value ||
        relocation->offset > function->value + function->size - 4u ||
        relocation->symbol_file_index != literal->file_index ||
        relocation->type != CTOOL_ELF32_R_386_32 ||
        relocation->addend_known != CTOOL_TRUE || relocation->addend != 0 ||
        !validate_runtime_string_function(
            job, text, function, zero_sizes[index], copy_sizes[index],
            function_names[index])) {
      (void)fprintf(stderr, "runtime string object %u differs\n", index);
      return 0;
    }
  }
  return 1;
}

static int run_aggregate_initializer_object(const char *host_root) {
  static const char source[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef struct { ctool_u32 words[4]; } row_t;\n"
      "typedef struct { ctool_u32 marker; row_t rows[2]; ctool_u32 tail; } aggregate_t;\n"
      "extern ctool_u32 first_leaf(void);\n"
      "extern ctool_u32 second_leaf(void);\n"
      "extern ctool_u32 third_leaf(void);\n"
      "extern ctool_u32 fourth_leaf(void);\n"
      "ctool_u32 aggregate_paths(void) {\n"
      "  aggregate_t value = {\n"
      "      .rows = {\n"
      "          [1] = {.words = {[3] = first_leaf(), [1] = second_leaf()}},\n"
      "          [0] = {.words = {[2] = third_leaf()}}},\n"
      "      .marker = fourth_leaf()};\n"
      "  return value.rows[1].words[3] + value.rows[1].words[1] +\n"
      "         value.rows[0].words[2] + value.marker + value.tail;\n"
      "}\n"
      "typedef struct {\n"
      "  ctool_u32 kind;\n"
      "  ctool_u32 referenced_type;\n"
      "  ctool_u32 first_member;\n"
      "  ctool_u32 member_count;\n"
      "} ctool_c_type_node_t;\n"
      "ctool_u32 active_zero_record(void) {\n"
      "  ctool_c_type_node_t node = {0};\n"
      "  return node.kind + node.referenced_type + node.first_member +\n"
      "         node.member_count;\n"
      "}\n";
  static const char string_source[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef struct { char text[4]; ctool_u32 value; } string_record_t;\n"
      "ctool_u32 string_leaf(void) {\n"
      "  string_record_t record = {\"abc\", 1u};\n"
      "  return record.value + record.text[3];\n"
      "}\n"
      "ctool_u32 string_tail(void) {\n"
      "  char text[6] = \"hi\";\n"
      "  return text[5];\n"
      "}\n"
      "char *string_address(void) {\n"
      "  return \"Cupid\";\n"
      "}\n"
      "char *string_compound(void) {\n"
      "  return (char[]){\"Cupid\"};\n"
      "}\n";
  static const char bit_field_source[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef struct { ctool_u32 flag : 1; ctool_u32 value; } bit_record_t;\n"
      "ctool_u32 bit_field_leaf(ctool_u32 seed) {\n"
      "  bit_record_t record = {.flag = seed, .value = seed};\n"
      "  return record.value;\n"
      "}\n";
  static const char record_expression_source[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef struct { ctool_u32 left; ctool_u32 right; } pair_t;\n"
      "typedef struct { pair_t pair; ctool_u32 tail; } pair_box_t;\n"
      "ctool_u32 record_expression_leaf(void) {\n"
      "  pair_t pair = {1u, 2u};\n"
      "  pair_box_t box = {pair, 3u};\n"
      "  return box.tail;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t string_unit;
  ctool_c_translation_unit_t invalid_string_unit;
  ctool_c_translation_unit_t bit_field_unit;
  ctool_c_translation_unit_t record_expression_unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t bytes;
  ctool_u8 *first_object = NULL;
  ctool_u8 *string_object = NULL;
  ctool_c_initializer_t *invalid_string_initializers = NULL;
  ctool_c_expression_t *invalid_string_expressions = NULL;
  ctool_u32 first_object_size = 0u;
  ctool_u32 string_object_size = 0u;
  ctool_u32 string_initializer = CTOOL_C_AST_NONE;
  ctool_u32 string_expression = CTOOL_C_AST_NONE;
  ctool_u32 index;
  ctool_arena_mark_t mark;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&string_unit, 0, sizeof(string_unit));
  (void)memset(&invalid_string_unit, 0, sizeof(invalid_string_unit));
  (void)memset(&bit_field_unit, 0, sizeof(bit_field_unit));
  (void)memset(&record_expression_unit, 0,
               sizeof(record_expression_unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_contains(
          job, "/toolchain/cupidc_frontend.c",
          "load active CupidC frontend source",
          "the active zero-initialized type-node record changed",
          active_zero_record_initializer, NULL) ||
      !parse_source(job, "/aggregate-initializer-object.c", source, &unit) ||
      unit.function_definition_count != 2u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    (void)fprintf(stderr, "aggregate initializer setup failed\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK,
                    "aggregate initializer object buffer")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, output);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK,
                    "aggregate initializer object emission") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  first_object_size = bytes.size;
  first_object = (ctool_u8 *)malloc((size_t)first_object_size);
  if (first_object == NULL) {
    (void)fprintf(stderr, "aggregate initializer object copy failed\n");
    goto cleanup;
  }
  (void)memcpy(first_object, bytes.data, (size_t)first_object_size);
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, output);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK,
                    "repeat aggregate initializer object") ||
      bytes.size != first_object_size ||
      memcmp(bytes.data, first_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr,
                  "aggregate initializer object is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/aggregate-initializer-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read aggregate initializer object") ||
      !validate_aggregate_initializer_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !active_source_contains(
          job, "/drivers/serial.c", "load active serial source",
          "the active automatic hexadecimal string changed",
          active_serial_hex_initializer, NULL) ||
      !parse_source(job, "/aggregate-string-leaf.c", string_source,
                    &string_unit) ||
      string_unit.function_definition_count != 4u ||
      !expect_object_success_preserves_unit(
          job, &string_unit, output, "runtime automatic strings")) {
    goto cleanup;
  }
  bytes = ctool_buffer_view(output);
  string_object_size = bytes.size;
  string_object = (ctool_u8 *)malloc((size_t)string_object_size);
  if (string_object == NULL) {
    goto cleanup;
  }
  (void)memcpy(string_object, bytes.data, (size_t)string_object_size);
  object_source.path.text = ctool_string("/runtime-automatic-strings.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read runtime string object") ||
      !validate_runtime_string_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !expect_object_success_preserves_unit(
          job, &string_unit, output, "repeat runtime automatic strings")) {
    goto cleanup;
  }
  bytes = ctool_buffer_view(output);
  if (bytes.size != string_object_size ||
      memcmp(bytes.data, string_object, (size_t)bytes.size) != 0) {
    (void)fprintf(stderr,
                  "runtime automatic string object is not deterministic\n");
    goto cleanup;
  }
  for (index = 0u; index < string_unit.initializer_count; index++) {
    if (string_unit.initializers[index].kind ==
            CTOOL_C_INITIALIZER_STRING &&
        string_initializer == CTOOL_C_AST_NONE) {
      string_initializer = index;
    }
  }
  for (index = 0u; index < string_unit.expression_count; index++) {
    if (string_unit.expressions[index].kind == CTOOL_C_EXPRESSION_STRING &&
        string_expression == CTOOL_C_AST_NONE) {
      string_expression = index;
    }
  }
  if (string_initializer == CTOOL_C_AST_NONE ||
      string_expression == CTOOL_C_AST_NONE ||
      sizeof(*invalid_string_initializers) >
          SIZE_MAX / (size_t)string_unit.initializer_count ||
      sizeof(*invalid_string_expressions) >
          SIZE_MAX / (size_t)string_unit.expression_count) {
    goto cleanup;
  }
  invalid_string_initializers = (ctool_c_initializer_t *)malloc(
      (size_t)string_unit.initializer_count *
      sizeof(*invalid_string_initializers));
  invalid_string_expressions = (ctool_c_expression_t *)malloc(
      (size_t)string_unit.expression_count *
      sizeof(*invalid_string_expressions));
  if (invalid_string_initializers == NULL ||
      invalid_string_expressions == NULL) {
    goto cleanup;
  }
  invalid_string_unit = string_unit;
  invalid_string_unit.initializers = invalid_string_initializers;
  invalid_string_unit.expressions = invalid_string_expressions;
  (void)memcpy(invalid_string_initializers, string_unit.initializers,
               (size_t)string_unit.initializer_count *
                   sizeof(*invalid_string_initializers));
  (void)memcpy(invalid_string_expressions, string_unit.expressions,
               (size_t)string_unit.expression_count *
                   sizeof(*invalid_string_expressions));
  invalid_string_initializers[string_initializer].string_bytes.size++;
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !expect_object_failure_preserves_unit(
          job, &invalid_string_unit, output, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "oversized automatic string initializer object")) {
    goto cleanup;
  }
  (void)memcpy(invalid_string_initializers, string_unit.initializers,
               (size_t)string_unit.initializer_count *
                   sizeof(*invalid_string_initializers));
  (void)memcpy(invalid_string_expressions, string_unit.expressions,
               (size_t)string_unit.expression_count *
                   sizeof(*invalid_string_expressions));
  invalid_string_expressions[string_expression].string_bytes.size--;
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !expect_object_failure_preserves_unit(
          job, &invalid_string_unit, output, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "undersized runtime string expression object") ||
      ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !parse_source(job, "/aggregate-bit-field-leaf.c", bit_field_source,
                    &bit_field_unit) ||
      !expect_object_failure_preserves_unit(
          job, &bit_field_unit, output, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support bit-field leaves in "
          "automatic aggregate initializer lists",
          "automatic aggregate bit-field leaf") ||
      ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !parse_source(job, "/aggregate-record-expression-leaf.c",
                    record_expression_source, &record_expression_unit) ||
      !expect_object_success_preserves_unit(
          job, &record_expression_unit, output,
          "automatic aggregate record-valued expression leaf")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_string_expressions);
  free(invalid_string_initializers);
  free(string_object);
  free(first_object);
  dispose_unit_snapshot(&snapshot);
  if (output != (ctool_buffer_t *)0) {
    ctool_buffer_close(output);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("aggregate-initializers: ok");
    return 0;
  }
  return 1;
}

static int validate_automatic_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_text[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x10u, 0x8du, 0x45u,
      0xf0u, 0x50u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0xbau,
      0x04u, 0x00u, 0x00u, 0x00u, 0x0fu, 0xafu, 0xcau, 0x01u,
      0xc8u, 0x50u, 0x8du, 0x85u, 0x0cu, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0x89u,
      0x08u, 0x51u, 0x58u, 0x8du, 0x45u, 0xf0u, 0x50u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x59u, 0x58u, 0xbau, 0x04u, 0x00u, 0x00u,
      0x00u, 0x0fu, 0xafu, 0xcau, 0x01u, 0xc8u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u,
      0xe5u, 0x83u, 0xecu, 0x08u, 0x8du, 0x45u, 0xf8u, 0x50u,
      0x58u, 0x83u, 0xc0u, 0x04u, 0x50u, 0x8du, 0x85u, 0x08u,
      0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u,
      0x59u, 0x58u, 0x89u, 0x08u, 0x51u, 0x58u, 0x8du, 0x45u,
      0xf8u, 0x50u, 0x58u, 0x83u, 0xc0u, 0x04u, 0x50u, 0x58u,
      0x8bu, 0x00u, 0x50u, 0x58u, 0xc9u, 0xc3u, 0x55u, 0x89u,
      0xe5u, 0x83u, 0xecu, 0x04u, 0x8du, 0x45u, 0xfdu, 0x50u,
      0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0x83u, 0xc4u, 0x04u,
      0xc9u, 0xc3u, 0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x0cu,
      0x8du, 0x45u, 0xfdu, 0x50u, 0x8du, 0x45u, 0xf4u, 0x50u,
      0x8bu, 0x4cu, 0x24u, 0x04u, 0x8bu, 0x14u, 0x24u, 0x89u,
      0x54u, 0x24u, 0x04u, 0x89u, 0x0cu, 0x24u, 0x83u, 0xecu,
      0x04u, 0x8bu, 0x4cu, 0x24u, 0x04u, 0x89u, 0x0cu, 0x24u,
      0x8bu, 0x4cu, 0x24u, 0x08u, 0x89u, 0x4cu, 0x24u, 0x04u,
      0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0x83u, 0xc4u, 0x0cu,
      0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x0cu, 0x8du, 0x45u,
      0xf4u, 0x50u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0xbau,
      0x04u, 0x00u, 0x00u, 0x00u, 0x0fu, 0xafu, 0xcau, 0x01u,
      0xc8u, 0x50u, 0x83u, 0xecu, 0x08u, 0x8bu, 0x4cu, 0x24u,
      0x08u, 0x89u, 0x0cu, 0x24u, 0xe8u, 0xfcu, 0xffu, 0xffu,
      0xffu, 0x83u, 0xc4u, 0x0cu, 0xc9u, 0xc3u};
  static const char *const function_names[] = {
      "automatic_array", "automatic_record", "automatic_bytes",
      "automatic_mixed", "automatic_children"};
  static const ctool_u32 function_offsets[] = {0u, 86u, 134u, 154u,
                                                210u};
  static const ctool_u32 function_sizes[] = {86u, 48u, 20u, 56u, 54u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *consume =
      find_symbol(object, "consume_bytes");
  const ctool_elf32_symbol_t *consume_layout =
      find_symbol(object, "consume_layout");
  const ctool_elf32_symbol_t *consume_child =
      find_symbol(object, "consume_child");
  ctool_u32 cursor = 0u;
  ctool_u32 return_count = 0u;
  ctool_u32 call_count = 0u;
  ctool_u32 index;
  if (text == NULL || rel_text == NULL || consume == NULL ||
      consume_layout == NULL || consume_child == NULL ||
      text->contents.data == NULL ||
      text->contents.size != (ctool_u32)sizeof(expected_text) ||
      memcmp(text->contents.data, expected_text, sizeof(expected_text)) != 0 ||
      text->relocation_count != 3u || object->relocation_count != 3u ||
      object->relocations == NULL || object->symbol_count != 9u ||
      !symbol_matches(consume, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u) ||
      !symbol_matches(consume_layout, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u) ||
      !symbol_matches(consume_child, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u)) {
    (void)fprintf(stderr, "automatic object inventory differs\n");
    return 0;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(function_names) / sizeof(function_names[0]));
       index++) {
    const ctool_elf32_symbol_t *function =
        find_symbol(object, function_names[index]);
    if (!symbol_matches(function, index + 4u,
                        CTOOL_ELF32_BIND_GLOBAL,
                        CTOOL_ELF32_SYMBOL_FUNCTION,
                        CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                        function_offsets[index], function_sizes[index])) {
      (void)fprintf(stderr, "automatic function %s differs\n",
                    function_names[index]);
      return 0;
    }
  }
  if (object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].offset != 145u ||
      object->relocations[0].symbol_file_index != consume->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != -4 ||
      object->relocations[1].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[1].entry_index != 1u ||
      object->relocations[1].target_section_file_index != text->file_index ||
      object->relocations[1].offset != 201u ||
      object->relocations[1].symbol_file_index != consume_layout->file_index ||
      object->relocations[1].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[1].addend_known != CTOOL_TRUE ||
      object->relocations[1].addend != -4 ||
      object->relocations[2].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[2].entry_index != 2u ||
      object->relocations[2].target_section_file_index != text->file_index ||
      object->relocations[2].offset != 255u ||
      object->relocations[2].symbol_file_index != consume_child->file_index ||
      object->relocations[2].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[2].addend_known != CTOOL_TRUE ||
      object->relocations[2].addend != -4) {
    (void)fprintf(stderr, "automatic object call relocation differs\n");
    return 0;
  }
  while (cursor < text->contents.size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + cursor, text->contents.size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr,
                    "automatic object decode failed at %u (0x%02x)\n",
                    (unsigned int)cursor,
                    (unsigned int)text->contents.data[cursor]);
      return 0;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      return_count++;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_CALL) {
      call_count++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != text->contents.size || return_count != 5u ||
      call_count != 3u) {
    (void)fprintf(stderr, "automatic object instruction inventory differs\n");
    return 0;
  }
  return 1;
}

static int run_automatic_object(const char *host_root) {
  static const char source[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef unsigned char ctool_u8;\n"
      "typedef struct { ctool_u32 left; ctool_u32 right; } pair_t;\n"
      "void consume_bytes(ctool_u8 *bytes);\n"
      "void consume_layout(ctool_u8 *bytes, ctool_u32 *words);\n"
      "void consume_child(ctool_u32 *child);\n"
      "ctool_u32 automatic_array(ctool_u32 index, ctool_u32 value) {\n"
      "  ctool_u32 section_map[4];\n"
      "  section_map[index] = value;\n"
      "  return section_map[index];\n"
      "}\n"
      "ctool_u32 automatic_record(ctool_u32 value) {\n"
      "  pair_t pair;\n"
      "  pair.right = value;\n"
      "  return pair.right;\n"
      "}\n"
      "void automatic_bytes(void) {\n"
      "  ctool_u8 bytes[3];\n"
      "  consume_bytes(bytes);\n"
      "}\n"
      "void automatic_mixed(void) {\n"
      "  ctool_u8 padding_bytes[3];\n"
      "  ctool_u32 words[2];\n"
      "  consume_layout(padding_bytes, words);\n"
      "}\n"
      "void automatic_children(ctool_u32 index) {\n"
      "  ctool_u32 children[3];\n"
      "  consume_child(&children[index]);\n"
      "}\n";
  static const char oversized_source[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef unsigned char ctool_u8;\n"
      "void consume_word(ctool_u32 *value);\n"
      "void consume_bytes(ctool_u8 *bytes);\n"
      "void oversized_automatic(void) {\n"
      "  ctool_u32 first;\n"
      "  ctool_u8 bytes[4294967295u];\n"
      "  consume_word(&first);\n"
      "  consume_bytes(bytes);\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t oversized_unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t bytes;
  ctool_u8 *first_object = NULL;
  ctool_u32 first_object_size = 0u;
  ctool_arena_mark_t mark;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&oversized_unit, 0, sizeof(oversized_unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/automatic-object.c", source, &unit) ||
      !take_unit_snapshot(&unit, &snapshot)) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "automatic object buffer")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, output);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "automatic object emission") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  first_object_size = bytes.size;
  first_object = (ctool_u8 *)malloc((size_t)first_object_size);
  if (first_object == NULL) {
    goto cleanup;
  }
  (void)memcpy(first_object, bytes.data, (size_t)first_object_size);
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, output);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "repeat automatic object emission") ||
      bytes.size != first_object_size ||
      memcmp(bytes.data, first_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "automatic object emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/automatic-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read automatic object") ||
      !validate_automatic_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !parse_source(job, "/oversized-automatic-object.c", oversized_source,
                    &oversized_unit) ||
      !expect_object_failure_preserves_unit(
          job, &oversized_unit, output, CTOOL_ERR_OVERFLOW,
          CTOOL_C_EMIT_DIAG_LIMIT,
          "CupidC object emission exceeded a configured resource limit",
          "oversized automatic object")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(first_object);
  dispose_unit_snapshot(&snapshot);
  if (output != (ctool_buffer_t *)0) {
    ctool_buffer_close(output);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("automatic-objects: ok");
    return 0;
  }
  return 1;
}

static int bytes_contain(ctool_bytes_t haystack, const ctool_u8 *needle,
                         ctool_u32 needle_size) {
  ctool_u32 offset;
  if (needle == NULL || needle_size == 0u || needle_size > haystack.size ||
      haystack.data == NULL) {
    return 0;
  }
  for (offset = 0u; offset <= haystack.size - needle_size; offset++) {
    if (memcmp(haystack.data + offset, needle, (size_t)needle_size) == 0) {
      return 1;
    }
  }
  return 0;
}

static int symbol_bytes_contain(const ctool_elf32_section_t *section,
                                const ctool_elf32_symbol_t *symbol,
                                const ctool_u8 *needle,
                                ctool_u32 needle_size) {
  ctool_bytes_t contents;
  if (section == NULL || symbol == NULL ||
      symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      symbol->section_file_index != section->file_index ||
      symbol->value > section->contents.size ||
      symbol->size > section->contents.size - symbol->value) {
    return 0;
  }
  contents = ctool_bytes(section->contents.data + symbol->value,
                         symbol->size);
  return bytes_contain(contents, needle, needle_size);
}

static int validate_narrow_value_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 bool_conversion[] = {
      0x85u, 0xc0u, 0x0fu, 0x95u, 0xc0u, 0x0fu, 0xb6u, 0xc0u};
  static const ctool_u8 signed_byte_load[] = {0x0fu, 0xbeu, 0x00u};
  static const ctool_u8 unsigned_byte_load[] = {0x0fu, 0xb6u, 0x00u};
  static const ctool_u8 signed_word_load[] = {0x0fu, 0xbfu, 0x00u};
  static const ctool_u8 unsigned_word_load[] = {0x0fu, 0xb7u, 0x00u};
  static const ctool_u8 byte_store[] = {0x88u, 0x08u};
  static const ctool_u8 word_store[] = {0x66u, 0x89u, 0x08u};
  static const ctool_u8 signed_byte_lane[] = {0x0fu, 0xbeu, 0xc0u};
  static const ctool_u8 unsigned_byte_lane[] = {0x0fu, 0xb6u, 0xc0u};
  static const ctool_u8 signed_word_lane[] = {0x0fu, 0xbfu, 0xc0u};
  static const ctool_u8 unsigned_word_lane[] = {0x0fu, 0xb7u, 0xc0u};
  static const ctool_u8 direct_u16_call_tail[] = {
      0x83u, 0xc4u, 0x08u, 0x0fu, 0xb7u, 0xc0u, 0x50u};
  static const ctool_u8 indirect_u16_call_tail[] = {
      0x83u, 0xc4u, 0x08u, 0x0fu, 0xb7u, 0xc0u, 0x50u};
  static const ctool_u8 direct_bool_call_tail[] = {
      0x83u, 0xc4u, 0x08u, 0x0fu, 0xb6u, 0xc0u, 0x50u};
  static const ctool_u8 indirect_bool_call_tail[] = {
      0x83u, 0xc4u, 0x08u, 0x0fu, 0xb6u, 0xc0u, 0x50u};
  static const ctool_u8 direct_i8_call_tail[] = {
      0x83u, 0xc4u, 0x08u, 0x0fu, 0xbeu, 0xc0u, 0x50u};
  static const ctool_u8 indirect_i16_call_tail[] = {
      0x83u, 0xc4u, 0x08u, 0x0fu, 0xbfu, 0xc0u, 0x50u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_symbol_t *word = find_symbol(object, "narrow_word");
  const ctool_elf32_symbol_t *asm_lower =
      find_symbol(object, "asm_lower");
  const ctool_elf32_symbol_t *load_i8 = find_symbol(object, "load_i8");
  const ctool_elf32_symbol_t *load_u8 = find_symbol(object, "load_u8");
  const ctool_elf32_symbol_t *load_i16 = find_symbol(object, "load_i16");
  const ctool_elf32_symbol_t *load_u16 = find_symbol(object, "load_u16");
  const ctool_elf32_symbol_t *store_i8 = find_symbol(object, "store_i8");
  const ctool_elf32_symbol_t *store_u8 = find_symbol(object, "store_u8");
  const ctool_elf32_symbol_t *store_i16 = find_symbol(object, "store_i16");
  const ctool_elf32_symbol_t *store_u16 = find_symbol(object, "store_u16");
  const ctool_elf32_symbol_t *load_bool = find_symbol(object, "load_bool");
  const ctool_elf32_symbol_t *store_bool = find_symbol(object, "store_bool");
  const ctool_elf32_symbol_t *bool_target =
      find_symbol(object, "bool_target");
  const ctool_elf32_symbol_t *narrow_target =
      find_symbol(object, "narrow_target");
  const ctool_elf32_symbol_t *direct_narrow =
      find_symbol(object, "direct_narrow");
  const ctool_elf32_symbol_t *indirect_narrow =
      find_symbol(object, "indirect_narrow");
  const ctool_elf32_symbol_t *direct_bool =
      find_symbol(object, "direct_bool");
  const ctool_elf32_symbol_t *indirect_bool =
      find_symbol(object, "indirect_bool");
  const ctool_elf32_symbol_t *i8_target =
      find_symbol(object, "i8_target");
  const ctool_elf32_symbol_t *direct_i8 =
      find_symbol(object, "direct_i8");
  const ctool_elf32_symbol_t *u8_target =
      find_symbol(object, "u8_target");
  const ctool_elf32_symbol_t *direct_u8 =
      find_symbol(object, "direct_u8");
  const ctool_elf32_symbol_t *i16_target =
      find_symbol(object, "i16_target");
  const ctool_elf32_symbol_t *indirect_i16 =
      find_symbol(object, "indirect_i16");
  ctool_u32 signed_byte_loads = 0u;
  ctool_u32 unsigned_byte_loads = 0u;
  ctool_u32 signed_word_loads = 0u;
  ctool_u32 unsigned_word_loads = 0u;
  ctool_u32 byte_stores = 0u;
  ctool_u32 word_stores = 0u;
  ctool_u32 direct_calls = 0u;
  ctool_u32 indirect_calls = 0u;
  ctool_u32 returns = 0u;
  ctool_u32 function_symbols = 0u;
  ctool_u32 symbol_index;
  ctool_u32 cursor = 0u;
  int direct_call_abi = 0;
  int indirect_call_abi = 0;
  if (text != NULL) {
    for (symbol_index = 0u; symbol_index < object->symbol_count;
         symbol_index++) {
      const ctool_elf32_symbol_t *symbol =
          &object->symbols[symbol_index];
      if (symbol->type == CTOOL_ELF32_SYMBOL_FUNCTION &&
          symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED &&
          symbol->section_file_index == text->file_index) {
        function_symbols++;
      }
    }
  }
  if (text == NULL || bss == NULL || word == NULL ||
      text->contents.data == NULL || text->contents.size == 0u ||
      function_symbols != 30u ||
      bss->alignment != 2u || bss->size != 2u ||
      word->binding != CTOOL_ELF32_BIND_LOCAL ||
      word->type != CTOOL_ELF32_SYMBOL_OBJECT ||
      word->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      word->section_file_index != bss->file_index || word->value != 0u ||
      word->size != 2u ||
      !bytes_contain(text->contents, bool_conversion,
                     (ctool_u32)sizeof(bool_conversion)) ||
      !bytes_contain(text->contents, signed_byte_load,
                     (ctool_u32)sizeof(signed_byte_load)) ||
      !bytes_contain(text->contents, unsigned_byte_load,
                     (ctool_u32)sizeof(unsigned_byte_load)) ||
      !bytes_contain(text->contents, signed_word_load,
                     (ctool_u32)sizeof(signed_word_load)) ||
      !bytes_contain(text->contents, unsigned_word_load,
                     (ctool_u32)sizeof(unsigned_word_load)) ||
      !bytes_contain(text->contents, byte_store,
                     (ctool_u32)sizeof(byte_store)) ||
      !bytes_contain(text->contents, word_store,
                     (ctool_u32)sizeof(word_store)) ||
      !bytes_contain(text->contents, signed_byte_lane,
                     (ctool_u32)sizeof(signed_byte_lane)) ||
      !bytes_contain(text->contents, unsigned_byte_lane,
                     (ctool_u32)sizeof(unsigned_byte_lane)) ||
      !bytes_contain(text->contents, signed_word_lane,
                     (ctool_u32)sizeof(signed_word_lane)) ||
      !bytes_contain(text->contents, unsigned_word_lane,
                      (ctool_u32)sizeof(unsigned_word_lane)) ||
      !symbol_bytes_contain(text, asm_lower, signed_byte_load,
                            (ctool_u32)sizeof(signed_byte_load)) ||
      !symbol_bytes_contain(text, asm_lower, signed_byte_lane,
                            (ctool_u32)sizeof(signed_byte_lane)) ||
      !symbol_bytes_contain(text, load_i8, signed_byte_load,
                            (ctool_u32)sizeof(signed_byte_load)) ||
      !symbol_bytes_contain(text, load_u8, unsigned_byte_load,
                            (ctool_u32)sizeof(unsigned_byte_load)) ||
      !symbol_bytes_contain(text, load_i16, signed_word_load,
                            (ctool_u32)sizeof(signed_word_load)) ||
      !symbol_bytes_contain(text, load_u16, unsigned_word_load,
                            (ctool_u32)sizeof(unsigned_word_load)) ||
      !symbol_bytes_contain(text, store_i8, byte_store,
                            (ctool_u32)sizeof(byte_store)) ||
      !symbol_bytes_contain(text, store_i8, signed_byte_lane,
                            (ctool_u32)sizeof(signed_byte_lane)) ||
      !symbol_bytes_contain(text, store_u8, byte_store,
                            (ctool_u32)sizeof(byte_store)) ||
      !symbol_bytes_contain(text, store_u8, unsigned_byte_lane,
                            (ctool_u32)sizeof(unsigned_byte_lane)) ||
      !symbol_bytes_contain(text, store_i16, word_store,
                            (ctool_u32)sizeof(word_store)) ||
      !symbol_bytes_contain(text, store_i16, signed_word_lane,
                            (ctool_u32)sizeof(signed_word_lane)) ||
      !symbol_bytes_contain(text, store_u16, word_store,
                            (ctool_u32)sizeof(word_store)) ||
      !symbol_bytes_contain(text, store_u16, unsigned_word_lane,
                            (ctool_u32)sizeof(unsigned_word_lane)) ||
      !symbol_bytes_contain(text, load_bool, unsigned_byte_load,
                            (ctool_u32)sizeof(unsigned_byte_load)) ||
      !symbol_bytes_contain(text, store_bool, bool_conversion,
                            (ctool_u32)sizeof(bool_conversion)) ||
      !symbol_bytes_contain(text, store_bool, byte_store,
                            (ctool_u32)sizeof(byte_store)) ||
      !symbol_bytes_contain(text, bool_target, unsigned_byte_lane,
                            (ctool_u32)sizeof(unsigned_byte_lane)) ||
      !symbol_bytes_contain(text, narrow_target, unsigned_word_lane,
                            (ctool_u32)sizeof(unsigned_word_lane)) ||
      !symbol_bytes_contain(text, direct_narrow, direct_u16_call_tail,
                            (ctool_u32)sizeof(direct_u16_call_tail)) ||
      !symbol_bytes_contain(text, indirect_narrow, indirect_u16_call_tail,
                            (ctool_u32)sizeof(indirect_u16_call_tail)) ||
      !symbol_bytes_contain(text, direct_bool, direct_bool_call_tail,
                            (ctool_u32)sizeof(direct_bool_call_tail)) ||
      !symbol_bytes_contain(text, indirect_bool, indirect_bool_call_tail,
                            (ctool_u32)sizeof(indirect_bool_call_tail)) ||
      !symbol_bytes_contain(text, i8_target, signed_byte_lane,
                            (ctool_u32)sizeof(signed_byte_lane)) ||
      !symbol_bytes_contain(text, direct_i8, unsigned_word_load,
                            (ctool_u32)sizeof(unsigned_word_load)) ||
      !symbol_bytes_contain(text, direct_i8, direct_i8_call_tail,
                            (ctool_u32)sizeof(direct_i8_call_tail)) ||
      !symbol_bytes_contain(text, u8_target, unsigned_byte_lane,
                            (ctool_u32)sizeof(unsigned_byte_lane)) ||
      !symbol_bytes_contain(text, direct_u8, direct_bool_call_tail,
                            (ctool_u32)sizeof(direct_bool_call_tail)) ||
      !symbol_bytes_contain(text, i16_target, signed_word_lane,
                            (ctool_u32)sizeof(signed_word_lane)) ||
      !symbol_bytes_contain(text, indirect_i16,
                            indirect_i16_call_tail,
                            (ctool_u32)sizeof(indirect_i16_call_tail))) {
    (void)fprintf(stderr, "narrow value object inventory differs\n");
    return 0;
  }
  while (cursor < text->contents.size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + cursor, text->contents.size - cursor);
    const ctool_x86_instruction_t *instruction;
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "narrow value decode failed at %u\n",
                    (unsigned int)cursor);
      return 0;
    }
    instruction = &decoded.instruction;
    if ((instruction->mnemonic == CTOOL_X86_MN_MOVSX ||
         instruction->mnemonic == CTOOL_X86_MN_MOVZX) &&
        instruction->operand_count == 2u &&
        instruction->operands[1].kind == CTOOL_X86_OPERAND_MEMORY) {
      if (instruction->mnemonic == CTOOL_X86_MN_MOVSX &&
          instruction->operands[1].width_bits == 8u) {
        signed_byte_loads++;
      } else if (instruction->mnemonic == CTOOL_X86_MN_MOVZX &&
                 instruction->operands[1].width_bits == 8u) {
        unsigned_byte_loads++;
      } else if (instruction->mnemonic == CTOOL_X86_MN_MOVSX &&
                 instruction->operands[1].width_bits == 16u) {
        signed_word_loads++;
      } else if (instruction->mnemonic == CTOOL_X86_MN_MOVZX &&
                 instruction->operands[1].width_bits == 16u) {
        unsigned_word_loads++;
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind == CTOOL_X86_OPERAND_MEMORY &&
               instruction->operands[1].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      if (instruction->operands[0].width_bits == 8u &&
          instruction->operands[1].as.reg.class_id == CTOOL_X86_REG_GPR8) {
        byte_stores++;
      } else if (instruction->operands[0].width_bits == 16u &&
                 instruction->operands[1].as.reg.class_id ==
                     CTOOL_X86_REG_GPR16) {
        word_stores++;
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_CALL &&
               instruction->operand_count == 1u) {
      if (instruction->operands[0].kind == CTOOL_X86_OPERAND_RELATIVE) {
        direct_calls++;
        if (cursor + decoded.consumed <= text->contents.size &&
            text->contents.size - cursor - decoded.consumed >=
                (ctool_u32)sizeof(direct_u16_call_tail) &&
            memcmp(text->contents.data + cursor + decoded.consumed,
                   direct_u16_call_tail,
                   sizeof(direct_u16_call_tail)) == 0) {
          direct_call_abi = 1;
        }
      } else if (instruction->operands[0].kind ==
                 CTOOL_X86_OPERAND_REGISTER) {
        indirect_calls++;
        if (cursor + decoded.consumed <= text->contents.size &&
            text->contents.size - cursor - decoded.consumed >=
                (ctool_u32)sizeof(indirect_u16_call_tail) &&
            memcmp(text->contents.data + cursor + decoded.consumed,
                   indirect_u16_call_tail,
                   sizeof(indirect_u16_call_tail)) == 0) {
          indirect_call_abi = 1;
        }
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_RET) {
      returns++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != text->contents.size || signed_byte_loads == 0u ||
      unsigned_byte_loads == 0u || signed_word_loads == 0u ||
      unsigned_word_loads == 0u || byte_stores == 0u || word_stores == 0u ||
      direct_calls != 4u || indirect_calls != 3u || returns != 31u ||
      direct_call_abi == 0 || indirect_call_abi == 0) {
    (void)fprintf(stderr,
                  "narrow operation inventory differs: sx8=%u zx8=%u "
                  "sx16=%u zx16=%u store8=%u store16=%u direct=%u "
                  "indirect=%u returns=%u\n",
                  (unsigned int)signed_byte_loads,
                  (unsigned int)unsigned_byte_loads,
                  (unsigned int)signed_word_loads,
                  (unsigned int)unsigned_word_loads,
                  (unsigned int)byte_stores, (unsigned int)word_stores,
                  (unsigned int)direct_calls, (unsigned int)indirect_calls,
                  (unsigned int)returns);
    return 0;
  }
  return 1;
}

#define NARROW_ORACLE_MEMORY_SIZE 256u
#define NARROW_ORACLE_INITIAL_ESP 192u
#define NARROW_ORACLE_EAX 0u
#define NARROW_ORACLE_ECX 1u
#define NARROW_ORACLE_EDX 2u
#define NARROW_ORACLE_EBX 3u
#define NARROW_ORACLE_ESP 4u
#define NARROW_ORACLE_EBP 5u
#define NARROW_ORACLE_ESI 6u
#define NARROW_ORACLE_EDI 7u

typedef struct {
  ctool_u32 registers[8];
  ctool_u8 memory[NARROW_ORACLE_MEMORY_SIZE];
} narrow_oracle_machine_t;

typedef struct {
  ctool_u8 parent;
  ctool_u8 shift;
  ctool_u16 width_bits;
  ctool_u32 mask;
} narrow_oracle_register_lane_t;

static int narrow_oracle_memory_range(ctool_u32 address, ctool_u32 width) {
  return address <= NARROW_ORACLE_MEMORY_SIZE &&
                 width <= NARROW_ORACLE_MEMORY_SIZE - address
             ? 1
             : 0;
}

static int narrow_oracle_read_memory(const narrow_oracle_machine_t *machine,
                                     ctool_u32 address, ctool_u16 width_bits,
                                     ctool_u32 *value) {
  ctool_u32 width = (ctool_u32)width_bits / 8u;
  ctool_u32 result = 0u;
  ctool_u32 index;
  if (machine == NULL || value == NULL ||
      (width_bits != 8u && width_bits != 16u && width_bits != 32u) ||
      !narrow_oracle_memory_range(address, width)) {
    return 0;
  }
  for (index = 0u; index < width; index++) {
    result |= (ctool_u32)machine->memory[address + index] << (index * 8u);
  }
  *value = result;
  return 1;
}

static int narrow_oracle_write_memory(narrow_oracle_machine_t *machine,
                                      ctool_u32 address,
                                      ctool_u16 width_bits,
                                      ctool_u32 value) {
  ctool_u32 width = (ctool_u32)width_bits / 8u;
  ctool_u32 index;
  if (machine == NULL ||
      (width_bits != 8u && width_bits != 16u && width_bits != 32u) ||
      !narrow_oracle_memory_range(address, width)) {
    return 0;
  }
  for (index = 0u; index < width; index++) {
    machine->memory[address + index] =
        (ctool_u8)(value >> (index * 8u));
  }
  return 1;
}

static int narrow_oracle_register_lane(
    ctool_x86_reg_t reg, narrow_oracle_register_lane_t *lane) {
  if (lane == NULL || reg.index >= 8u) {
    return 0;
  }
  lane->parent = reg.index;
  lane->shift = 0u;
  if (reg.class_id == CTOOL_X86_REG_GPR32) {
    lane->width_bits = 32u;
    lane->mask = 0xffffffffu;
    return 1;
  }
  if (reg.class_id == CTOOL_X86_REG_GPR16) {
    lane->width_bits = 16u;
    lane->mask = 0xffffu;
    return 1;
  }
  if (reg.class_id == CTOOL_X86_REG_GPR8) {
    lane->width_bits = 8u;
    lane->mask = 0xffu;
    if (reg.index >= 4u) {
      lane->parent = (ctool_u8)(reg.index - 4u);
      lane->shift = 8u;
    }
    return 1;
  }
  return 0;
}

static int narrow_oracle_read_register(const narrow_oracle_machine_t *machine,
                                       ctool_x86_reg_t reg,
                                       ctool_u32 *value) {
  narrow_oracle_register_lane_t lane;
  if (machine == NULL || value == NULL ||
      !narrow_oracle_register_lane(reg, &lane)) {
    return 0;
  }
  *value = (machine->registers[lane.parent] >> lane.shift) & lane.mask;
  return 1;
}

static int narrow_oracle_write_register(narrow_oracle_machine_t *machine,
                                        ctool_x86_reg_t reg,
                                        ctool_u32 value) {
  narrow_oracle_register_lane_t lane;
  ctool_u32 shifted_mask;
  if (machine == NULL || !narrow_oracle_register_lane(reg, &lane)) {
    return 0;
  }
  shifted_mask = lane.mask << lane.shift;
  machine->registers[lane.parent] =
      (machine->registers[lane.parent] & ~shifted_mask) |
      ((value & lane.mask) << lane.shift);
  return 1;
}

static int narrow_oracle_memory_address(
    const narrow_oracle_machine_t *machine,
    const ctool_x86_memory_t *memory, ctool_u32 *address) {
  ctool_u32 result;
  ctool_u32 register_value;
  if (machine == NULL || memory == NULL || address == NULL ||
      memory->address_bits != 32u ||
      memory->segment.class_id != CTOOL_X86_REG_NONE ||
      memory->displacement.kind != CTOOL_X86_VALUE_CONSTANT) {
    return 0;
  }
  result = memory->displacement.bits +
           (ctool_u32)memory->displacement.addend;
  if (memory->base.class_id != CTOOL_X86_REG_NONE) {
    if (memory->base.class_id != CTOOL_X86_REG_GPR32 ||
        !narrow_oracle_read_register(machine, memory->base,
                                     &register_value)) {
      return 0;
    }
    result += register_value;
  }
  if (memory->index.class_id != CTOOL_X86_REG_NONE) {
    if (memory->index.class_id != CTOOL_X86_REG_GPR32 ||
        !narrow_oracle_read_register(machine, memory->index,
                                     &register_value) ||
        (memory->scale != 1u && memory->scale != 2u &&
         memory->scale != 4u && memory->scale != 8u)) {
      return 0;
    }
    result += register_value * (ctool_u32)memory->scale;
  }
  *address = result;
  return 1;
}

static ctool_u16 narrow_oracle_operand_width(
    const ctool_x86_operand_t *operand) {
  if (operand == NULL) {
    return 0u;
  }
  if (operand->kind == CTOOL_X86_OPERAND_REGISTER) {
    narrow_oracle_register_lane_t lane;
    return narrow_oracle_register_lane(operand->as.reg, &lane)
               ? lane.width_bits
               : 0u;
  }
  return operand->width_bits;
}

static int narrow_oracle_read_operand(const narrow_oracle_machine_t *machine,
                                      const ctool_x86_operand_t *operand,
                                      ctool_u32 *value) {
  ctool_u32 address;
  if (machine == NULL || operand == NULL || value == NULL) {
    return 0;
  }
  if (operand->kind == CTOOL_X86_OPERAND_REGISTER) {
    return narrow_oracle_read_register(machine, operand->as.reg, value);
  }
  if (operand->kind == CTOOL_X86_OPERAND_IMMEDIATE &&
      operand->as.value.kind == CTOOL_X86_VALUE_CONSTANT) {
    *value = operand->as.value.bits + (ctool_u32)operand->as.value.addend;
    return 1;
  }
  if (operand->kind == CTOOL_X86_OPERAND_MEMORY &&
      narrow_oracle_memory_address(machine, &operand->as.memory, &address)) {
    return narrow_oracle_read_memory(machine, address, operand->width_bits,
                                     value);
  }
  return 0;
}

static int narrow_oracle_write_operand(narrow_oracle_machine_t *machine,
                                       const ctool_x86_operand_t *operand,
                                       ctool_u32 value) {
  ctool_u32 address;
  if (machine == NULL || operand == NULL) {
    return 0;
  }
  if (operand->kind == CTOOL_X86_OPERAND_REGISTER) {
    return narrow_oracle_write_register(machine, operand->as.reg, value);
  }
  if (operand->kind == CTOOL_X86_OPERAND_MEMORY &&
      narrow_oracle_memory_address(machine, &operand->as.memory, &address)) {
    return narrow_oracle_write_memory(machine, address, operand->width_bits,
                                      value);
  }
  return 0;
}

static ctool_u32 narrow_oracle_extend(ctool_u32 value,
                                      ctool_u16 source_width,
                                      ctool_bool is_signed) {
  if (source_width == 8u) {
    value &= 0xffu;
    if (is_signed == CTOOL_TRUE && (value & 0x80u) != 0u) {
      value |= 0xffffff00u;
    }
  } else if (source_width == 16u) {
    value &= 0xffffu;
    if (is_signed == CTOOL_TRUE && (value & 0x8000u) != 0u) {
      value |= 0xffff0000u;
    }
  }
  return value;
}

static int narrow_oracle_step(narrow_oracle_machine_t *machine,
                              const ctool_x86_instruction_t *instruction,
                              ctool_bool *returned) {
  const ctool_x86_operand_t *left;
  const ctool_x86_operand_t *right;
  ctool_u32 left_value;
  ctool_u32 right_value;
  ctool_u32 address;
  ctool_u32 stack_pointer;
  ctool_u16 width;
  if (machine == NULL || instruction == NULL || returned == NULL) {
    return 0;
  }
  *returned = CTOOL_FALSE;
  if (instruction->mnemonic == CTOOL_X86_MN_NOP &&
      instruction->operand_count == 0u) {
    return 1;
  }
  if (instruction->mnemonic == CTOOL_X86_MN_RET &&
      instruction->operand_count == 0u) {
    *returned = CTOOL_TRUE;
    return 1;
  }
  if (instruction->mnemonic == CTOOL_X86_MN_CDQ &&
      instruction->operand_count == 0u) {
    machine->registers[NARROW_ORACLE_EDX] =
        (machine->registers[NARROW_ORACLE_EAX] & 0x80000000u) != 0u
            ? 0xffffffffu
            : 0u;
    return 1;
  }
  if (instruction->mnemonic == CTOOL_X86_MN_LEAVE &&
      instruction->operand_count == 0u) {
    machine->registers[NARROW_ORACLE_ESP] =
        machine->registers[NARROW_ORACLE_EBP];
    stack_pointer = machine->registers[NARROW_ORACLE_ESP];
    if (!narrow_oracle_read_memory(machine, stack_pointer, 32u,
                                   &machine->registers[NARROW_ORACLE_EBP])) {
      return 0;
    }
    machine->registers[NARROW_ORACLE_ESP] = stack_pointer + 4u;
    return 1;
  }
  if (instruction->operand_count != 1u &&
      instruction->operand_count != 2u) {
    return 0;
  }
  left = &instruction->operands[0];
  right = instruction->operand_count == 2u ? &instruction->operands[1]
                                            : NULL;
  if (instruction->mnemonic == CTOOL_X86_MN_PUSH && right == NULL &&
      narrow_oracle_read_operand(machine, left, &left_value)) {
    stack_pointer = machine->registers[NARROW_ORACLE_ESP] - 4u;
    if (!narrow_oracle_write_memory(machine, stack_pointer, 32u,
                                    left_value)) {
      return 0;
    }
    machine->registers[NARROW_ORACLE_ESP] = stack_pointer;
    return 1;
  }
  if (instruction->mnemonic == CTOOL_X86_MN_POP && right == NULL) {
    stack_pointer = machine->registers[NARROW_ORACLE_ESP];
    if (!narrow_oracle_read_memory(machine, stack_pointer, 32u,
                                   &left_value) ||
        !narrow_oracle_write_operand(machine, left, left_value)) {
      return 0;
    }
    machine->registers[NARROW_ORACLE_ESP] = stack_pointer + 4u;
    return 1;
  }
  if ((instruction->mnemonic == CTOOL_X86_MN_DIV ||
       instruction->mnemonic == CTOOL_X86_MN_IDIV) &&
      right == NULL && narrow_oracle_read_operand(machine, left, &left_value)) {
    if (left_value == 0u) {
      return 0;
    }
    if (instruction->mnemonic == CTOOL_X86_MN_DIV) {
      uint64_t dividend =
          ((uint64_t)machine->registers[NARROW_ORACLE_EDX] << 32u) |
          machine->registers[NARROW_ORACLE_EAX];
      uint64_t quotient = dividend / left_value;
      if (quotient > 0xffffffffu) {
        return 0;
      }
      machine->registers[NARROW_ORACLE_EAX] = (ctool_u32)quotient;
      machine->registers[NARROW_ORACLE_EDX] =
          (ctool_u32)(dividend % left_value);
    } else {
      int64_t dividend =
          (int64_t)(int32_t)machine->registers[NARROW_ORACLE_EDX] *
              INT64_C(4294967296) +
          machine->registers[NARROW_ORACLE_EAX];
      int64_t divisor = (int64_t)(int32_t)left_value;
      int64_t quotient;
      if (divisor == 0) {
        return 0;
      }
      quotient = dividend / divisor;
      if (quotient < INT64_C(-2147483647) - 1 ||
          quotient > INT64_C(2147483647)) {
        return 0;
      }
      machine->registers[NARROW_ORACLE_EAX] =
          (ctool_u32)(int32_t)quotient;
      machine->registers[NARROW_ORACLE_EDX] =
          (ctool_u32)(int32_t)(dividend % divisor);
    }
    return 1;
  }
  if (right == NULL) {
    return 0;
  }
  if (instruction->mnemonic == CTOOL_X86_MN_LEA &&
      right->kind == CTOOL_X86_OPERAND_MEMORY &&
      narrow_oracle_memory_address(machine, &right->as.memory, &address)) {
    return narrow_oracle_write_operand(machine, left, address);
  }
  if (!narrow_oracle_read_operand(machine, right, &right_value)) {
    return 0;
  }
  if (instruction->mnemonic == CTOOL_X86_MN_MOV) {
    return narrow_oracle_write_operand(machine, left, right_value);
  }
  if (instruction->mnemonic == CTOOL_X86_MN_MOVSX ||
      instruction->mnemonic == CTOOL_X86_MN_MOVZX) {
    width = narrow_oracle_operand_width(right);
    if (width != 8u && width != 16u) {
      return 0;
    }
    return narrow_oracle_write_operand(
        machine, left,
        narrow_oracle_extend(
            right_value, width,
            instruction->mnemonic == CTOOL_X86_MN_MOVSX ? CTOOL_TRUE
                                                         : CTOOL_FALSE));
  }
  if (!narrow_oracle_read_operand(machine, left, &left_value)) {
    return 0;
  }
  if (instruction->mnemonic == CTOOL_X86_MN_ADD) {
    left_value += right_value;
  } else if (instruction->mnemonic == CTOOL_X86_MN_SUB) {
    left_value -= right_value;
  } else if (instruction->mnemonic == CTOOL_X86_MN_IMUL) {
    left_value =
        (ctool_u32)((uint64_t)left_value * (uint64_t)right_value);
  } else if (instruction->mnemonic == CTOOL_X86_MN_AND) {
    left_value &= right_value;
  } else if (instruction->mnemonic == CTOOL_X86_MN_OR) {
    left_value |= right_value;
  } else if (instruction->mnemonic == CTOOL_X86_MN_XOR) {
    left_value ^= right_value;
  } else if (instruction->mnemonic == CTOOL_X86_MN_SHL) {
    right_value &= 31u;
    left_value = right_value == 0u ? left_value
                                   : left_value << right_value;
  } else if (instruction->mnemonic == CTOOL_X86_MN_SHR) {
    right_value &= 31u;
    left_value = right_value == 0u ? left_value
                                   : left_value >> right_value;
  } else if (instruction->mnemonic == CTOOL_X86_MN_SAR) {
    right_value &= 31u;
    if (right_value != 0u) {
      left_value = (left_value >> right_value) |
                   ((left_value & 0x80000000u) != 0u
                        ? 0xffffffffu << (32u - right_value)
                        : 0u);
    }
  } else {
    return 0;
  }
  return narrow_oracle_write_operand(machine, left, left_value);
}

static int narrow_oracle_execute(ctool_job_t *job,
                                 const ctool_elf32_section_t *text,
                                 const ctool_elf32_symbol_t *symbol,
                                 ctool_u32 input, ctool_u16 stored_width_bits,
                                 ctool_u32 *result, ctool_u32 *stored_slot) {
  narrow_oracle_machine_t machine;
  ctool_u32 cursor = 0u;
  ctool_u32 initial_slot;
  ctool_bool returned = CTOOL_FALSE;
  if (job == NULL || text == NULL || symbol == NULL || result == NULL ||
      stored_slot == NULL || symbol->value > text->contents.size ||
      symbol->size > text->contents.size - symbol->value ||
      (stored_width_bits != 8u && stored_width_bits != 16u)) {
    return 0;
  }
  (void)memset(&machine, 0xcd, sizeof(machine));
  machine.registers[NARROW_ORACLE_ESP] = NARROW_ORACLE_INITIAL_ESP;
  machine.registers[NARROW_ORACLE_EBP] = 64u;
  initial_slot = stored_width_bits == 8u
                     ? 0xa5b6c700u | (input & 0xffu)
                     : 0xa5b60000u | (input & 0xffffu);
  if (!narrow_oracle_write_memory(&machine, NARROW_ORACLE_INITIAL_ESP, 32u,
                                  0x13579bdfu) ||
      !narrow_oracle_write_memory(&machine, NARROW_ORACLE_INITIAL_ESP + 4u,
                                  32u, initial_slot)) {
    return 0;
  }
  while (cursor < symbol->size && returned == CTOOL_FALSE) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + symbol->value + cursor, symbol->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u ||
        !narrow_oracle_step(&machine, &decoded.instruction, &returned)) {
      (void)fprintf(
          stderr, "narrow mutation oracle stopped at %u on %s\n",
          (unsigned int)cursor,
          status == CTOOL_OK && decoded.kind == CTOOL_X86_DECODE_KNOWN
              ? ctool_x86_mnemonic_name(decoded.instruction.mnemonic).data
              : "invalid instruction");
      return 0;
    }
    cursor += decoded.consumed;
  }
  if (returned == CTOOL_FALSE || cursor != symbol->size ||
      machine.registers[NARROW_ORACLE_ESP] != NARROW_ORACLE_INITIAL_ESP ||
      !narrow_oracle_read_memory(&machine, NARROW_ORACLE_INITIAL_ESP, 32u,
                                 &initial_slot) ||
      initial_slot != 0x13579bdfu ||
      !narrow_oracle_read_memory(&machine, NARROW_ORACLE_INITIAL_ESP + 4u,
                                 32u, stored_slot)) {
    return 0;
  }
  *result = machine.registers[NARROW_ORACLE_EAX];
  return 1;
}

static int validate_narrow_mutation_results(
    ctool_job_t *job, const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *text) {
  typedef struct {
    const char *function_name;
    ctool_u32 input;
    ctool_u32 expected_result;
    ctool_u32 expected_stored_value;
    ctool_u16 stored_width_bits;
  } narrow_mutation_case_t;
  static const narrow_mutation_case_t cases[] = {
      {"prefix_i8", 0u, 1u, 1u, 8u},
      {"prefix_i8", 0x7fu, 0xffffff80u, 0x80u, 8u},
      {"prefix_i8", 0x80u, 0xffffff81u, 0x81u, 8u},
      {"prefix_u8", 0u, 0xffu, 0xffu, 8u},
      {"prefix_u8", 1u, 0u, 0u, 8u},
      {"prefix_u8", 0xffu, 0xfeu, 0xfeu, 8u},
      {"postfix_i16", 0u, 0u, 1u, 16u},
      {"postfix_i16", 0x7fffu, 0x7fffu, 0x8000u, 16u},
      {"postfix_i16", 0x8000u, 0xffff8000u, 0x8001u, 16u},
      {"postfix_u16", 0u, 0u, 0xffffu, 16u},
      {"postfix_u16", 1u, 1u, 0u, 16u},
      {"postfix_u16", 0xffffu, 0xffffu, 0xfffeu, 16u}};
  ctool_u32 index;
  for (index = 0u; index < (ctool_u32)(sizeof(cases) / sizeof(cases[0]));
       index++) {
    const ctool_elf32_symbol_t *function =
        find_symbol(object, cases[index].function_name);
    ctool_u32 result = 0u;
    ctool_u32 stored_slot = 0u;
    ctool_u32 expected_slot =
        cases[index].stored_width_bits == 8u
            ? 0xa5b6c700u | (cases[index].expected_stored_value & 0xffu)
            : 0xa5b60000u |
                  (cases[index].expected_stored_value & 0xffffu);
    if (function == NULL ||
        !narrow_oracle_execute(job, text, function, cases[index].input,
                               cases[index].stored_width_bits, &result,
                               &stored_slot) ||
        result != cases[index].expected_result ||
        stored_slot != expected_slot) {
      (void)fprintf(stderr,
                    "narrow mutation result %s case %u differs: "
                    "eax=%08x slot=%08x\n",
                    cases[index].function_name, (unsigned int)index,
                    (unsigned int)result, (unsigned int)stored_slot);
      return 0;
    }
  }
  return 1;
}

static int validate_narrow_mutation_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  typedef struct {
    const char *name;
    ctool_u32 offset;
    ctool_u32 size;
  } narrow_mutation_function_t;
  static const narrow_mutation_function_t functions[] = {
      {"all_u8_compounds", 0u, 446u},
      {"signed_i16_compounds", 446u, 108u},
      {"prefix_i8", 554u, 44u},
      {"prefix_u8", 598u, 44u},
      {"postfix_i16", 642u, 60u},
      {"postfix_u16", 702u, 60u},
      {"volatile_postfix", 762u, 57u},
      {"add_prefix", 819u, 59u}};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *volatile_byte_symbol =
      find_symbol(object, "volatile_byte");
  ctool_u32 cursor = 0u;
  ctool_u32 expected_offset = 0u;
  ctool_u32 signed_byte_loads = 0u;
  ctool_u32 unsigned_byte_loads = 0u;
  ctool_u32 signed_word_loads = 0u;
  ctool_u32 unsigned_word_loads = 0u;
  ctool_u32 byte_stores = 0u;
  ctool_u32 word_stores = 0u;
  ctool_u32 multiply_count = 0u;
  ctool_u32 signed_divide_count = 0u;
  ctool_u32 unsigned_divide_count = 0u;
  ctool_u32 left_shift_count = 0u;
  ctool_u32 right_shift_count = 0u;
  ctool_u32 return_count = 0u;
  ctool_u32 index;
  if (text == NULL || bss == NULL || rel_text == NULL ||
      volatile_byte_symbol == NULL ||
      text->contents.data == NULL || text->contents.size != 878u ||
      text->relocation_count != 1u || object->relocation_count != 1u ||
      object->relocations == NULL || object->symbol_count != 10u ||
      bss->type != CTOOL_ELF32_SHT_NOBITS || bss->alignment != 1u ||
      bss->size != 1u || bss->contents.size != 0u ||
      volatile_byte_symbol->file_index != 7u ||
      !symbol_matches(volatile_byte_symbol, volatile_byte_symbol->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u, 1u)) {
    (void)fprintf(
        stderr,
        "narrow mutation object inventory differs: text=%u bss-align=%u "
        "bss-size=%u symbols=%u relocations=%u text-relocations=%u "
        "state=%u/%u/%u/%u/%u\n",
        text == NULL ? 0u : (unsigned int)text->contents.size,
        bss == NULL ? 0u : (unsigned int)bss->alignment,
        bss == NULL ? 0u : (unsigned int)bss->size,
        (unsigned int)object->symbol_count,
        (unsigned int)object->relocation_count,
        text == NULL ? 0u : (unsigned int)text->relocation_count,
        volatile_byte_symbol == NULL
            ? 0u
            : (unsigned int)volatile_byte_symbol->file_index,
        volatile_byte_symbol == NULL
            ? 0u
            : (unsigned int)volatile_byte_symbol->binding,
        volatile_byte_symbol == NULL
            ? 0u
            : (unsigned int)volatile_byte_symbol->section_file_index,
        volatile_byte_symbol == NULL
            ? 0u
            : (unsigned int)volatile_byte_symbol->value,
        volatile_byte_symbol == NULL
            ? 0u
            : (unsigned int)volatile_byte_symbol->size);
    return 0;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(functions) / sizeof(functions[0]));
       index++) {
    const ctool_elf32_symbol_t *function =
        find_symbol(object, functions[index].name);
    if (function == NULL || function->binding != CTOOL_ELF32_BIND_GLOBAL ||
        function->type != CTOOL_ELF32_SYMBOL_FUNCTION ||
        function->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        function->section_file_index != text->file_index ||
        function->value != expected_offset ||
        function->value != functions[index].offset ||
        function->size != functions[index].size ||
        function->value > text->contents.size ||
        function->size > text->contents.size - function->value) {
      (void)fprintf(stderr, "narrow mutation function %s differs\n",
                    functions[index].name);
      return 0;
    }
    expected_offset += function->size;
  }
  if (expected_offset != text->contents.size ||
      object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].symbol_file_index !=
          volatile_byte_symbol->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != 0) {
    (void)fprintf(stderr, "narrow mutation relocation differs\n");
    return 0;
  }
  if (!validate_narrow_mutation_results(job, object, text)) {
    return 0;
  }
  while (cursor < text->contents.size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + cursor, text->contents.size - cursor);
    const ctool_x86_instruction_t *instruction;
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u, &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "narrow mutation decode failed at %u\n",
                    (unsigned int)cursor);
      return 0;
    }
    instruction = &decoded.instruction;
    if ((instruction->mnemonic == CTOOL_X86_MN_MOVSX ||
         instruction->mnemonic == CTOOL_X86_MN_MOVZX) &&
        instruction->operand_count == 2u &&
        instruction->operands[1].kind == CTOOL_X86_OPERAND_MEMORY) {
      if (instruction->mnemonic == CTOOL_X86_MN_MOVSX &&
          instruction->operands[1].width_bits == 8u) {
        signed_byte_loads++;
      } else if (instruction->mnemonic == CTOOL_X86_MN_MOVZX &&
                 instruction->operands[1].width_bits == 8u) {
        unsigned_byte_loads++;
      } else if (instruction->mnemonic == CTOOL_X86_MN_MOVSX &&
                 instruction->operands[1].width_bits == 16u) {
        signed_word_loads++;
      } else if (instruction->mnemonic == CTOOL_X86_MN_MOVZX &&
                 instruction->operands[1].width_bits == 16u) {
        unsigned_word_loads++;
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind == CTOOL_X86_OPERAND_MEMORY &&
               instruction->operands[1].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      if (instruction->operands[0].width_bits == 8u &&
          instruction->operands[1].as.reg.class_id == CTOOL_X86_REG_GPR8) {
        byte_stores++;
      } else if (instruction->operands[0].width_bits == 16u &&
                 instruction->operands[1].as.reg.class_id ==
                     CTOOL_X86_REG_GPR16) {
        word_stores++;
      }
    }
    if (instruction->mnemonic == CTOOL_X86_MN_IMUL) {
      multiply_count++;
    } else if (instruction->mnemonic == CTOOL_X86_MN_IDIV) {
      signed_divide_count++;
    } else if (instruction->mnemonic == CTOOL_X86_MN_DIV) {
      unsigned_divide_count++;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SHL) {
      left_shift_count++;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SAR ||
               instruction->mnemonic == CTOOL_X86_MN_SHR) {
      right_shift_count++;
    } else if (instruction->mnemonic == CTOOL_X86_MN_RET) {
      return_count++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != text->contents.size || signed_byte_loads == 0u ||
      unsigned_byte_loads == 0u || signed_word_loads == 0u ||
      unsigned_word_loads == 0u || byte_stores != 14u ||
      word_stores != 4u || multiply_count != 1u ||
      signed_divide_count != 1u || unsigned_divide_count != 2u ||
      left_shift_count != 1u || right_shift_count != 2u ||
      return_count != 8u) {
    (void)fprintf(stderr,
                  "narrow mutation operation inventory differs: sx8=%u "
                  "zx8=%u sx16=%u zx16=%u store8=%u store16=%u ret=%u\n",
                  (unsigned int)signed_byte_loads,
                  (unsigned int)unsigned_byte_loads,
                  (unsigned int)signed_word_loads,
                  (unsigned int)unsigned_word_loads,
                  (unsigned int)byte_stores, (unsigned int)word_stores,
                  (unsigned int)return_count);
    return 0;
  }
  return 1;
}

static int run_narrow_mutation_object(const char *host_root) {
  static const char source[] =
      "typedef signed char i8;\n"
      "typedef unsigned char u8;\n"
      "typedef signed short i16;\n"
      "typedef unsigned short u16;\n"
      "typedef unsigned int u32;\n"
      "u8 all_u8_compounds(u8 value, u32 right) {\n"
      "  value *= right;\n"
      "  value /= right;\n"
      "  value %= right;\n"
      "  value += right;\n"
      "  value -= right;\n"
      "  value <<= right;\n"
      "  value >>= right;\n"
      "  value &= right;\n"
      "  value ^= right;\n"
      "  value |= right;\n"
      "  return value;\n"
      "}\n"
      "i16 signed_i16_compounds(i16 value, int right) {\n"
      "  value /= right;\n"
      "  value >>= right;\n"
      "  return value;\n"
      "}\n"
      "i8 prefix_i8(i8 value) { return ++value; }\n"
      "u8 prefix_u8(u8 value) { return --value; }\n"
      "i16 postfix_i16(i16 value) { return value++; }\n"
      "u16 postfix_u16(u16 value) { return value--; }\n"
      "volatile u8 volatile_byte;\n"
      "u8 volatile_postfix(void) { return volatile_byte++; }\n"
      "struct decoded_instruction { u8 prefixes; };\n"
      "struct decoded_value { struct decoded_instruction instruction; };\n"
      "u8 add_prefix(struct decoded_value *decoded, u8 flag) {\n"
      "  return decoded->instruction.prefixes |= flag;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t bytes;
  ctool_u8 *first_object = NULL;
  ctool_u32 first_object_size = 0u;
  ctool_arena_mark_t mark;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/narrow-mutations.c", source, &unit) ||
      unit.function_definition_count != 8u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "narrow mutation object buffer")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, output);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "narrow mutation object emission") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  first_object_size = bytes.size;
  first_object = (ctool_u8 *)malloc((size_t)first_object_size);
  if (first_object == NULL) {
    goto cleanup;
  }
  (void)memcpy(first_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, output);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "repeat narrow mutation emission") ||
      bytes.size != first_object_size ||
      memcmp(bytes.data, first_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "narrow mutation object is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/narrow-mutations.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read narrow mutation object") ||
      !validate_narrow_mutation_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(first_object);
  dispose_unit_snapshot(&snapshot);
  if (output != (ctool_buffer_t *)0) {
    ctool_buffer_close(output);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("narrow-mutations: ok");
    return 0;
  }
  return 1;
}

static int run_narrow_value_object(const char *host_root) {
  static const char source[] =
      "typedef signed char i8;\n"
      "typedef unsigned char u8;\n"
      "typedef signed short i16;\n"
      "typedef unsigned short u16;\n"
      "typedef unsigned int u32;\n"
      "static char asm_lower(char character) {\n"
      "  if (character >= 'A' && character <= 'Z') {\n"
      "    return (char)(character + ('a' - 'A'));\n"
      "  }\n"
      "  return character;\n"
      "}\n"
      "int load_i8(i8 *value) { return *value; }\n"
      "int load_u8(u8 *value) { return *value; }\n"
      "int load_i16(i16 *value) { return *value; }\n"
      "int load_u16(u16 *value) { return *value; }\n"
      "i8 store_i8(i8 *target, u32 value) {\n"
      "  return *target = (i8)value;\n"
      "}\n"
      "u8 store_u8(u8 *target, u32 value) {\n"
      "  return *target = (u8)value;\n"
      "}\n"
      "i16 store_i16(i16 *target, u32 value) {\n"
      "  return *target = (i16)value;\n"
      "}\n"
      "u16 store_u16(u16 *target, u32 value) {\n"
      "  return *target = (u16)value;\n"
      "}\n"
      "_Bool to_bool(u32 value) { return (_Bool)value; }\n"
      "_Bool load_bool(_Bool *value) { return *value; }\n"
      "_Bool store_bool(_Bool *target, u32 value) {\n"
      "  return *target = value;\n"
      "}\n"
      "_Bool bool_target(u32 value) { return value; }\n"
      "_Bool direct_bool(u32 value) { return bool_target(value); }\n"
      "typedef _Bool (*bool_callback_t)(u32);\n"
      "_Bool indirect_bool(bool_callback_t callback, u32 value) {\n"
      "  return callback(value);\n"
      "}\n"
      "u16 narrow_target(i8 value) { return (u16)value; }\n"
      "u16 direct_narrow(i8 value) { return narrow_target(value); }\n"
      "typedef u16 (*narrow_callback_t)(i8);\n"
      "u16 indirect_narrow(narrow_callback_t callback, i8 value) {\n"
      "  return callback(value);\n"
      "}\n"
      "i8 i8_target(u16 value) { return (i8)value; }\n"
      "i8 direct_i8(u16 value) { return i8_target(value); }\n"
      "u8 u8_target(u16 value) { return (u8)value; }\n"
      "u8 direct_u8(u16 value) { return u8_target(value); }\n"
      "i16 i16_target(u16 value) { return (i16)value; }\n"
      "typedef i16 (*i16_callback_t)(u16);\n"
      "i16 indirect_i16(i16_callback_t callback, u16 value) {\n"
      "  return callback(value);\n"
      "}\n"
      "static u16 narrow_word;\n"
      "u16 narrow_local_file(u16 input) {\n"
      "  u16 local = input;\n"
      "  narrow_word = local;\n"
      "  return narrow_word;\n"
      "}\n"
      "int narrow_truth(u8 value) { return value ? 1 : 0; }\n"
      "int narrow_not(u8 value) { return !value; }\n"
      "int narrow_logic(u8 left, u16 right) { return left && right; }\n"
      "struct narrow_pair { i8 byte_value; u16 word_value; };\n"
      "int narrow_record(struct narrow_pair *pair, u32 value) {\n"
      "  pair->byte_value = (i8)value;\n"
      "  pair->word_value = (u16)value;\n"
      "  return pair->byte_value + pair->word_value;\n"
      "}\n"
      "u16 narrow_auto(u32 index, u16 value) {\n"
      "  u16 values[2];\n"
      "  values[index] = value;\n"
      "  return values[index];\n"
      "}\n";
  static const char conversion_source[] =
      "typedef unsigned short u16;\n"
      "typedef unsigned int u32;\n"
      "u32 promoted_return(u16 value) { return value + 0u; }\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t conversion_unit;
  ctool_c_translation_unit_t invalid_conversion_unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t bytes;
  ctool_u8 *first_object = NULL;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_u32 first_object_size = 0u;
  ctool_u32 promotion_index = CTOOL_C_AST_NONE;
  ctool_u32 unsigned_int_type = CTOOL_C_TYPE_NONE;
  ctool_u32 index;
  ctool_arena_mark_t mark;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&conversion_unit, 0, sizeof(conversion_unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/narrow-values.c", source, &unit) ||
      !take_unit_snapshot(&unit, &snapshot)) {
    goto cleanup;
  }
  if (unit.function_definition_count != 30u) {
    (void)fprintf(stderr, "narrow value function inventory differs\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "narrow value object buffer")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, output);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "narrow value object emission") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  first_object_size = bytes.size;
  first_object = (ctool_u8 *)malloc((size_t)first_object_size);
  if (first_object == NULL) {
    goto cleanup;
  }
  (void)memcpy(first_object, bytes.data, (size_t)first_object_size);
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, output);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "repeat narrow value emission") ||
      bytes.size != first_object_size ||
      memcmp(bytes.data, first_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "narrow value object is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/narrow-values.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read narrow value object") ||
      !validate_narrow_value_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !parse_source(job, "/narrow-object-conversion.c",
                    conversion_source, &conversion_unit)) {
    goto cleanup;
  }
  for (index = 0u; index < conversion_unit.expression_count; index++) {
    if (conversion_unit.expressions[index].kind ==
            CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION &&
        conversion_unit.expressions[index].conversion ==
            CTOOL_C_CONVERSION_INTEGER_PROMOTION) {
      promotion_index = index;
      break;
    }
  }
  for (index = 0u; index < conversion_unit.graph.type_count; index++) {
    if (conversion_unit.graph.types[index].kind ==
            CTOOL_C_TYPE_UNSIGNED_INT &&
        conversion_unit.graph.types[index].qualifiers == 0u &&
        index < conversion_unit.layout.type_count &&
        conversion_unit.layout.types[index].size == 4u) {
      unsigned_int_type = index;
      break;
    }
  }
  if (promotion_index == CTOOL_C_AST_NONE ||
      unsigned_int_type == CTOOL_C_TYPE_NONE ||
      conversion_unit.expression_count == 0u ||
      sizeof(*invalid_expressions) >
          SIZE_MAX / (size_t)conversion_unit.expression_count) {
    (void)fprintf(stderr, "narrow object conversion fixture differs\n");
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)conversion_unit.expression_count *
      sizeof(*invalid_expressions));
  if (invalid_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, conversion_unit.expressions,
               (size_t)conversion_unit.expression_count *
                   sizeof(*invalid_expressions));
  invalid_expressions[promotion_index].type = unsigned_int_type;
  invalid_conversion_unit = conversion_unit;
  invalid_conversion_unit.expressions = invalid_expressions;
  if (!expect_object_failure_preserves_unit(
          job, &invalid_conversion_unit, output, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "object integer promotion with the wrong target")) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, conversion_unit.expressions,
               (size_t)conversion_unit.expression_count *
                   sizeof(*invalid_expressions));
  invalid_expressions[promotion_index].conversion =
      CTOOL_C_CONVERSION_USUAL_ARITHMETIC;
  if (!expect_object_failure_preserves_unit(
          job, &invalid_conversion_unit, output, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "object usual conversion before integer promotion")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_expressions);
  free(first_object);
  dispose_unit_snapshot(&snapshot);
  if (output != (ctool_buffer_t *)0) {
    ctool_buffer_close(output);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("narrow-values: ok");
    return 0;
  }
  return 1;
}

static int validate_void_cast_object(ctool_job_t *job,
                                     const ctool_elf32_object_t *object) {
  static const ctool_u8 function_bytes[] = {
      0x55u, 0x89u, 0xe5u,
      0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x58u,
      0x8du, 0x85u, 0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x58u,
      0x8du, 0x85u, 0x10u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x58u,
      0x83u, 0xecu, 0x08u, 0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu,
      0x83u, 0xc4u, 0x08u, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_SUB, CTOOL_X86_MN_CALL, CTOOL_X86_MN_ADD,
      CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text =
      find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *sink = find_symbol(object, "sink");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "discard_values");
  if (text == NULL || rel_text == NULL || sink == NULL || function == NULL ||
      text->contents.size != (ctool_u32)sizeof(function_bytes) ||
      text->relocation_count != 1u || object->symbol_count != 3u ||
      object->relocation_count != 1u ||
      !symbol_matches(sink, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED,
                      CTOOL_ELF32_NO_SECTION, 0u, 0u) ||
      !symbol_matches(function, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      (ctool_u32)sizeof(function_bytes)) ||
      object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].offset != 43u ||
      object->relocations[0].symbol_file_index != sink->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != -4 ||
      !decode_function(
          job, text, function, instructions,
          (ctool_u32)(sizeof(instructions) / sizeof(instructions[0])),
          function_bytes, (ctool_u32)sizeof(function_bytes),
          (const ctool_u32 *)0, 0u, "void-cast discard_values")) {
    (void)fprintf(stderr, "void-cast object differs\n");
    return 0;
  }
  return 1;
}

static int run_void_cast_object(const char *host_root) {
  static const char wide_source[] =
      "void discard_wide(long long *value) { (void)*value; }\n";
  static const char float_source[] =
      "void discard_float(volatile float *value) { (void)*value; }\n";
  static const char record_source[] =
      "struct pair { unsigned int left; unsigned int right; };\n"
      "void discard_record(struct pair *value) { (void)*value; }\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t wide_unit;
  ctool_c_translation_unit_t float_unit;
  ctool_c_translation_unit_t record_unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_arena_mark_t mark;
  ctool_bytes_t bytes;
  ctool_u8 *expected_object = NULL;
  ctool_u32 expected_object_size = 0u;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_object_sources_are_unchanged(job) ||
      !parse_source(job, "/void-cast-object.c", void_cast_object_source,
                    &unit) ||
      unit.function_definition_count != 1u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    (void)fprintf(stderr, "void-cast object setup failed\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "void-cast object buffers")) {
    goto cleanup;
  }

  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first void-cast object") ||
      bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "first void-cast object emission differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  expected_object_size = bytes.size;
  expected_object = (ctool_u8 *)malloc((size_t)expected_object_size);
  if (expected_object == NULL) {
    (void)fprintf(stderr, "void-cast object snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(expected_object, bytes.data, (size_t)bytes.size);

  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat void-cast object") ||
      bytes.size != expected_object_size ||
      memcmp(bytes.data, expected_object, (size_t)bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "void-cast object emission is not deterministic\n");
    goto cleanup;
  }

  object_source.path.text = ctool_string("/void-cast-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read void-cast object") ||
      !validate_void_cast_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/wide-void-cast-object.c", wide_source,
                    &wide_unit) ||
      !expect_object_success_preserves_unit(
          job, &wide_unit, second,
          "wide void-cast object operand") ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/float-void-cast-object.c", float_source,
                    &float_unit) ||
      !expect_object_failure_preserves_unit(
          job, &float_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "floating void-cast object operand") ||
      ctool_buffer_rewind(second, 0u) != CTOOL_OK ||
      !parse_source(job, "/record-void-cast-object.c", record_source,
                    &record_unit) ||
      !expect_object_success_preserves_unit(
          job, &record_unit, second,
          "record void-cast object operand")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(expected_object);
  dispose_unit_snapshot(&snapshot);
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("void-casts: ok");
    return 0;
  }
  return 1;
}

typedef struct {
  const char *name;
  ctool_u32 value;
  ctool_u32 size;
  const ctool_i32 *positive_frame_offsets;
  ctool_u32 positive_frame_offset_count;
  ctool_u32 move_count;
  ctool_u32 zero_count;
  ctool_u32 direct_call_count;
  ctool_u32 indirect_call_count;
  ctool_u32 plain_return_count;
  ctool_u32 cleanup_return_count;
  ctool_u32 caller_cleanup;
} structure_function_expectation_t;

static ctool_u32 structure_text_fingerprint(ctool_bytes_t bytes) {
  ctool_u32 fingerprint = 2166136261u;
  ctool_u32 index;
  for (index = 0u; index < bytes.size; index++) {
    fingerprint ^= bytes.data[index];
    fingerprint *= 16777619u;
  }
  return fingerprint;
}

static int validate_structure_function(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *symbol,
    const structure_function_expectation_t *expected) {
  ctool_u32 cursor = 0u;
  ctool_u32 frame_index = 0u;
  ctool_u32 move_count = 0u;
  ctool_u32 zero_count = 0u;
  ctool_u32 cld_count = 0u;
  ctool_u32 direct_call_count = 0u;
  ctool_u32 indirect_call_count = 0u;
  ctool_u32 plain_return_count = 0u;
  ctool_u32 cleanup_return_count = 0u;
  ctool_u32 caller_cleanup_count = 0u;
  ctool_x86_mnemonic_t last_mnemonic = CTOOL_X86_MN_INVALID;
  if (!symbol_matches(symbol, symbol->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      expected->value, expected->size)) {
    (void)fprintf(stderr, "%s: symbol range differs\n", expected->name);
    return 0;
  }
  while (cursor < symbol->size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + symbol->value + cursor,
        symbol->size - cursor);
    ctool_u32 operand_index;
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "%s: instruction at %u does not decode\n",
                    expected->name, cursor);
      return 0;
    }
    last_mnemonic = decoded.instruction.mnemonic;
    for (operand_index = 0u;
         operand_index < decoded.instruction.operand_count;
         operand_index++) {
      const ctool_x86_operand_t *operand =
          &decoded.instruction.operands[operand_index];
      if (operand->kind == CTOOL_X86_OPERAND_MEMORY &&
          operand->as.memory.base.class_id == CTOOL_X86_REG_GPR32 &&
          operand->as.memory.base.index == 5u &&
          operand->as.memory.displacement.kind ==
              CTOOL_X86_VALUE_CONSTANT &&
          (ctool_i32)operand->as.memory.displacement.bits > 0) {
        ctool_i32 displacement =
            (ctool_i32)operand->as.memory.displacement.bits;
        if (frame_index >= expected->positive_frame_offset_count ||
            displacement != expected->positive_frame_offsets[frame_index]) {
          (void)fprintf(stderr,
                        "%s: positive EBP displacement %u differs\n",
                        expected->name, frame_index);
          return 0;
        }
        frame_index++;
      }
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_CLD) {
      cld_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_MOVSB) {
      if (decoded.instruction.prefixes != CTOOL_X86_PREFIX_REP) {
        (void)fprintf(stderr, "%s: MOVSB is not repeated\n",
                      expected->name);
        return 0;
      }
      move_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_STOSB) {
      if (decoded.instruction.prefixes != CTOOL_X86_PREFIX_REP) {
        (void)fprintf(stderr, "%s: STOSB is not repeated\n",
                      expected->name);
        return 0;
      }
      zero_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_CALL) {
      if (decoded.instruction.operand_count != 1u) {
        (void)fprintf(stderr, "%s: call shape differs\n", expected->name);
        return 0;
      }
      if (decoded.instruction.operands[0].kind ==
          CTOOL_X86_OPERAND_RELATIVE) {
        direct_call_count++;
      } else if (decoded.instruction.operands[0].kind ==
                     CTOOL_X86_OPERAND_REGISTER &&
                 decoded.instruction.operands[0].as.reg.class_id ==
                     CTOOL_X86_REG_GPR32 &&
                 decoded.instruction.operands[0].as.reg.index == 0u) {
        indirect_call_count++;
      } else {
        (void)fprintf(stderr, "%s: call target differs\n", expected->name);
        return 0;
      }
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_ADD &&
               decoded.instruction.operand_count == 2u &&
               decoded.instruction.operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER &&
               decoded.instruction.operands[0].as.reg.class_id ==
                   CTOOL_X86_REG_GPR32 &&
               decoded.instruction.operands[0].as.reg.index == 4u &&
               decoded.instruction.operands[1].kind ==
                   CTOOL_X86_OPERAND_IMMEDIATE) {
      if (expected->caller_cleanup == 0u ||
          decoded.instruction.operands[1].as.value.kind !=
              CTOOL_X86_VALUE_CONSTANT ||
          decoded.instruction.operands[1].as.value.bits !=
              expected->caller_cleanup) {
        (void)fprintf(stderr, "%s: caller cleanup differs\n",
                      expected->name);
        return 0;
      }
      caller_cleanup_count++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      if (decoded.instruction.operand_count == 0u &&
          decoded.encoding.field_count == 0u) {
        plain_return_count++;
      } else if (decoded.instruction.operand_count == 1u &&
                 decoded.instruction.operands[0].kind ==
                     CTOOL_X86_OPERAND_IMMEDIATE &&
                 decoded.instruction.operands[0].width_bits == 16u &&
                 decoded.instruction.operands[0].encoding_bits == 16u &&
                 decoded.instruction.operands[0].as.value.kind ==
                     CTOOL_X86_VALUE_CONSTANT &&
                 decoded.instruction.operands[0].as.value.bits == 4u &&
                 decoded.encoding.field_count == 1u &&
                 decoded.encoding.fields[0].kind ==
                     CTOOL_X86_FIELD_IMMEDIATE &&
                 decoded.encoding.fields[0].byte_width == 2u) {
        cleanup_return_count++;
      } else {
        (void)fprintf(stderr, "%s: return cleanup differs\n",
                      expected->name);
        return 0;
      }
    }
    cursor += decoded.consumed;
  }
  if (cursor != symbol->size ||
      frame_index != expected->positive_frame_offset_count ||
      move_count != expected->move_count ||
      zero_count != expected->zero_count ||
      cld_count != move_count + zero_count ||
      direct_call_count != expected->direct_call_count ||
      indirect_call_count != expected->indirect_call_count ||
      plain_return_count != expected->plain_return_count ||
      cleanup_return_count != expected->cleanup_return_count ||
      caller_cleanup_count != (expected->caller_cleanup == 0u ? 0u : 1u) ||
      last_mnemonic != CTOOL_X86_MN_RET) {
    (void)fprintf(stderr, "%s: decoded ABI inventory differs\n",
                  expected->name);
    return 0;
  }
  return 1;
}

static int validate_structure_value_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_i32 call8_frame[] = {8};
  static const ctool_i32 call12_frame[] = {8};
  static const ctool_i32 call3_frame[] = {8};
  static const ctool_i32 copy8_frame[] = {12, 8};
  static const ctool_i32 copy12_frame[] = {12, 8};
  static const ctool_i32 bridge_frame[] = {12, 16, 28, 8};
  static const ctool_i32 indirect_frame[] = {12, 16, 20, 32, 8};
  static const ctool_i32 assign_frame[] = {16, 12, 8};
  static const structure_function_expectation_t functions[] = {
      {"call8", 0u, 114u, call8_frame, 1u, 2u, 1u, 1u, 0u, 1u, 0u,
       32u},
      {"call12", 114u, 114u, call12_frame, 1u, 2u, 1u, 1u, 0u, 1u,
       0u, 44u},
      {"call3", 228u, 114u, call3_frame, 1u, 2u, 1u, 1u, 0u, 1u, 0u,
       36u},
      {"copy8", 342u, 61u, copy8_frame, 2u, 2u, 0u, 0u, 0u, 0u, 1u,
       0u},
      {"copy12", 403u, 61u, copy12_frame, 2u, 2u, 0u, 0u, 0u, 0u,
       1u, 0u},
      {"bridge", 464u, 157u, bridge_frame, 4u, 3u, 1u, 1u, 0u, 0u,
       1u, 32u},
      {"indirect_bridge", 621u, 169u, indirect_frame, 5u, 3u, 1u, 0u,
       1u, 0u, 1u, 48u},
      {"assign8", 790u, 138u, assign_frame, 3u, 5u, 0u, 0u, 0u, 0u,
       1u, 0u}};
  static const ctool_u32 relocation_offsets[] = {
      103u, 217u, 331u, 586u};
  static const ctool_u32 relocation_symbol_indices[] = {1u, 2u, 4u, 3u};
  static const char *const relocation_symbols[] = {
      "take8", "take12", "take3", "transform"};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  ctool_u32 index;
  if (text == (const ctool_elf32_section_t *)0 ||
      rel_text == (const ctool_elf32_section_t *)0 ||
      text->contents.size != 928u ||
      structure_text_fingerprint(text->contents) != 0x31d58b50u ||
      text->relocation_count != 4u || text->relocation_first != 0u ||
      object->relocation_count != 4u || object->relocations == NULL ||
      object->symbol_count != 13u || object->symbols == NULL ||
      object->symbols[0].file_index != 0u ||
      object->symbols[0].name.size != 0u ||
      object->symbols[0].binding != CTOOL_ELF32_BIND_LOCAL ||
      object->symbols[0].type != CTOOL_ELF32_SYMBOL_NOTYPE ||
      object->symbols[0].placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      object->symbols[0].section_file_index != CTOOL_ELF32_NO_SECTION ||
      object->symbols[0].value != 0u || object->symbols[0].size != 0u) {
    (void)fprintf(stderr,
                  "structure object inventory differs: text=%u "
                  "fingerprint=%08x text-relocations=%u relocations=%u "
                  "symbols=%u\n",
                  text == (const ctool_elf32_section_t *)0
                      ? 0u
                      : text->contents.size,
                  text == (const ctool_elf32_section_t *)0
                      ? 0u
                      : structure_text_fingerprint(text->contents),
                  text == (const ctool_elf32_section_t *)0
                      ? 0u
                      : text->relocation_count,
                  object->relocation_count, object->symbol_count);
    return 0;
  }
  for (index = 0u; index < 4u; index++) {
    const ctool_elf32_symbol_t *external =
        find_symbol(object, relocation_symbols[index]);
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[index];
    if (!symbol_matches(external, relocation_symbol_indices[index],
                        CTOOL_ELF32_BIND_GLOBAL,
                        CTOOL_ELF32_SYMBOL_FUNCTION,
                        CTOOL_ELF32_SYMBOL_UNDEFINED,
                        CTOOL_ELF32_NO_SECTION, 0u, 0u) ||
        relocation->relocation_section_file_index != rel_text->file_index ||
        relocation->entry_index != index ||
        relocation->target_section_file_index != text->file_index ||
        relocation->offset != relocation_offsets[index] ||
        relocation->symbol_file_index != external->file_index ||
        relocation->type != CTOOL_ELF32_R_386_PC32 ||
        relocation->addend_known != CTOOL_TRUE ||
        relocation->addend != -4) {
      (void)fprintf(stderr, "structure relocation %u differs\n", index);
      return 0;
    }
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(functions) / sizeof(functions[0]));
       index++) {
    const ctool_elf32_symbol_t *function =
        find_symbol(object, functions[index].name);
    if (function == (const ctool_elf32_symbol_t *)0 ||
        function->file_index != index + 5u ||
        !validate_structure_function(job, text, function,
                                     &functions[index])) {
      return 0;
    }
  }
  return 1;
}

static int run_structure_value_object(const char *host_root) {
  static const char source[] =
      "typedef unsigned int u32;\n"
      "typedef struct { unsigned char bytes[3]; } s3_t;\n"
      "typedef struct { u32 a; u32 b; } s8_t;\n"
      "typedef struct { u32 a; u32 b; u32 c; } s12_t;\n"
      "extern u32 take8(u32 before, s8_t value, u32 after);\n"
      "extern u32 take12(u32 before, s12_t value, u32 after);\n"
      "extern s8_t transform(u32 before, s12_t value, u32 after);\n"
      "extern u32 take3(u32 before, s3_t value, u32 after);\n"
      "u32 call8(s8_t value) {\n"
      "  return take8(0x11111111u, value, 0x22222222u);\n"
      "}\n"
      "u32 call12(s12_t value) {\n"
      "  return take12(0x11111111u, value, 0x22222222u);\n"
      "}\n"
      "u32 call3(s3_t value) {\n"
      "  return take3(0x11111111u, value, 0x22222222u);\n"
      "}\n"
      "s8_t copy8(s8_t value) { return value; }\n"
      "s12_t copy12(s12_t value) { return value; }\n"
      "s8_t bridge(u32 before, s12_t value, u32 after) {\n"
      "  return transform(before, value, after);\n"
      "}\n"
      "s8_t indirect_bridge(s8_t (*function)(u32, s12_t, u32),\n"
      "                     u32 before, s12_t value, u32 after) {\n"
      "  return function(before, value, after);\n"
      "}\n"
      "s8_t assign8(s8_t *target, s8_t value) {\n"
      "  s8_t local = value;\n"
      "  return *target = local;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_arena_mark_t mark;
  ctool_bytes_t first_bytes;
  ctool_bytes_t second_bytes;
  ctool_u8 *expected_object = NULL;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/structure-value-object.c", source, &unit) ||
      !take_unit_snapshot(&unit, &snapshot)) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "structure value object buffers")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  first_bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first structure value object") ||
      first_bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  expected_object = (ctool_u8 *)malloc((size_t)first_bytes.size);
  if (expected_object == NULL) {
    goto cleanup;
  }
  (void)memcpy(expected_object, first_bytes.data, (size_t)first_bytes.size);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  second_bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat structure value object") ||
      second_bytes.size != first_bytes.size ||
      memcmp(second_bytes.data, expected_object,
             (size_t)second_bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }
  object_source.path.text = ctool_string("/structure-value-object.o");
  object_source.contents = second_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read structure value object") ||
      !validate_structure_value_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(expected_object);
  dispose_unit_snapshot(&snapshot);
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("structure-values: ok");
    return 0;
  }
  return 1;
}

static int call_alignment_adjust_stack(
    const ctool_x86_instruction_t *instruction, ctool_u32 *esp_residue,
    ctool_u32 *ebp_residue, int *ebp_known) {
  const ctool_x86_operand_t *left;
  const ctool_x86_operand_t *right;
  if (instruction == NULL || esp_residue == NULL || ebp_residue == NULL ||
      ebp_known == NULL) {
    return 0;
  }
  if (instruction->mnemonic == CTOOL_X86_MN_PUSH) {
    *esp_residue = (*esp_residue + 12u) & 15u;
    return 1;
  }
  if (instruction->mnemonic == CTOOL_X86_MN_POP) {
    if (instruction->operand_count == 1u &&
        instruction->operands[0].kind == CTOOL_X86_OPERAND_REGISTER &&
        aggregate_register_is(instruction->operands[0].as.reg, 4u) != 0) {
      return 0;
    }
    *esp_residue = (*esp_residue + 4u) & 15u;
    if (instruction->operand_count == 1u &&
        instruction->operands[0].kind == CTOOL_X86_OPERAND_REGISTER &&
        aggregate_register_is(instruction->operands[0].as.reg, 5u) != 0) {
      *ebp_known = 0;
    }
    return 1;
  }
  if (instruction->mnemonic == CTOOL_X86_MN_LEAVE) {
    if (*ebp_known == 0) {
      return 0;
    }
    *esp_residue = (*ebp_residue + 4u) & 15u;
    *ebp_known = 0;
    return 1;
  }
  if (instruction->operand_count == 2u) {
    left = &instruction->operands[0];
    right = &instruction->operands[1];
    if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
        left->kind == CTOOL_X86_OPERAND_REGISTER &&
        right->kind == CTOOL_X86_OPERAND_REGISTER &&
        aggregate_register_is(left->as.reg, 5u) != 0 &&
        aggregate_register_is(right->as.reg, 4u) != 0) {
      *ebp_residue = *esp_residue;
      *ebp_known = 1;
      return 1;
    }
    if ((instruction->mnemonic == CTOOL_X86_MN_ADD ||
         instruction->mnemonic == CTOOL_X86_MN_SUB) &&
        left->kind == CTOOL_X86_OPERAND_REGISTER &&
        aggregate_register_is(left->as.reg, 4u) != 0 &&
        right->kind == CTOOL_X86_OPERAND_IMMEDIATE &&
        right->as.value.kind == CTOOL_X86_VALUE_CONSTANT) {
      ctool_u32 amount = right->as.value.bits & 15u;
      *esp_residue = instruction->mnemonic == CTOOL_X86_MN_ADD
                         ? (*esp_residue + amount) & 15u
                         : (*esp_residue + 16u - amount) & 15u;
      return 1;
    }
    if (left->kind == CTOOL_X86_OPERAND_REGISTER &&
        (aggregate_register_is(left->as.reg, 4u) != 0 ||
         aggregate_register_is(left->as.reg, 5u) != 0) &&
        instruction->mnemonic != CTOOL_X86_MN_CMP &&
        instruction->mnemonic != CTOOL_X86_MN_TEST) {
      return 0;
    }
    if (instruction->mnemonic == CTOOL_X86_MN_XCHG &&
        right->kind == CTOOL_X86_OPERAND_REGISTER &&
        (aggregate_register_is(right->as.reg, 4u) != 0 ||
         aggregate_register_is(right->as.reg, 5u) != 0)) {
      return 0;
    }
  } else if (instruction->operand_count == 1u &&
             instruction->operands[0].kind ==
                 CTOOL_X86_OPERAND_REGISTER &&
             (aggregate_register_is(instruction->operands[0].as.reg, 4u) !=
                  0 ||
              aggregate_register_is(instruction->operands[0].as.reg, 5u) !=
                  0) &&
             instruction->mnemonic != CTOOL_X86_MN_CALL &&
             instruction->mnemonic != CTOOL_X86_MN_JMP) {
    return 0;
  }
  switch (instruction->mnemonic) {
    case CTOOL_X86_MN_PUSHA:
    case CTOOL_X86_MN_PUSHAD:
    case CTOOL_X86_MN_PUSHF:
    case CTOOL_X86_MN_PUSHFD:
    case CTOOL_X86_MN_POPA:
    case CTOOL_X86_MN_POPAD:
    case CTOOL_X86_MN_POPF:
    case CTOOL_X86_MN_POPFD:
      return 0;
    default:
      break;
  }
  return 1;
}

typedef struct {
  ctool_u32 esp_residue;
  ctool_u32 ebp_residue;
  int ebp_known;
  int reached;
} call_alignment_state_t;

static int call_alignment_is_conditional_jump(
    ctool_x86_mnemonic_t mnemonic) {
  switch (mnemonic) {
    case CTOOL_X86_MN_JA:
    case CTOOL_X86_MN_JAE:
    case CTOOL_X86_MN_JB:
    case CTOOL_X86_MN_JBE:
    case CTOOL_X86_MN_JE:
    case CTOOL_X86_MN_JG:
    case CTOOL_X86_MN_JGE:
    case CTOOL_X86_MN_JL:
    case CTOOL_X86_MN_JLE:
    case CTOOL_X86_MN_JNE:
    case CTOOL_X86_MN_JNO:
    case CTOOL_X86_MN_JNP:
    case CTOOL_X86_MN_JNS:
    case CTOOL_X86_MN_JO:
    case CTOOL_X86_MN_JP:
    case CTOOL_X86_MN_JS:
      return 1;
    default:
      return 0;
  }
}

static int call_alignment_branch_target(
    const ctool_x86_decoded_t *decoded, ctool_u32 cursor,
    ctool_u32 function_size, ctool_u32 *target_out) {
  int64_t target;
  int32_t displacement;
  if (decoded == NULL || target_out == NULL ||
      decoded->instruction.operand_count != 1u ||
      decoded->instruction.operands[0].kind !=
          CTOOL_X86_OPERAND_RELATIVE ||
      decoded->instruction.operands[0].as.value.kind !=
          CTOOL_X86_VALUE_CONSTANT) {
    return 0;
  }
  displacement =
      (int32_t)decoded->instruction.operands[0].as.value.bits;
  target = (int64_t)cursor + (int64_t)decoded->consumed +
           (int64_t)displacement;
  if (target < 0 || target >= (int64_t)function_size) {
    return 0;
  }
  *target_out = (ctool_u32)target;
  return 1;
}

static int call_alignment_record_state(
    ctool_u32 function_size, ctool_u32 target,
    const ctool_u8 *instruction_starts,
    const call_alignment_state_t *incoming,
    call_alignment_state_t *states, ctool_u32 *worklist,
    ctool_u32 *worklist_count, ctool_u32 *join_count,
    const char *context) {
  call_alignment_state_t *state;
  if (instruction_starts == NULL || incoming == NULL || states == NULL ||
      worklist == NULL || worklist_count == NULL || join_count == NULL ||
      context == NULL || target >= function_size ||
      instruction_starts[target] == 0u) {
    (void)fprintf(stderr, "%s: control-flow target %u differs\n", context,
                  (unsigned int)target);
    return 0;
  }
  state = &states[target];
  if (state->reached == 0) {
    if (*worklist_count >= function_size) {
      (void)fprintf(stderr, "%s: control-flow worklist overflowed\n",
                    context);
      return 0;
    }
    *state = *incoming;
    state->reached = 1;
    worklist[(*worklist_count)++] = target;
    return 1;
  }
  if (state->esp_residue != incoming->esp_residue ||
      state->ebp_known != incoming->ebp_known ||
      (state->ebp_known != 0 &&
       state->ebp_residue != incoming->ebp_residue)) {
    (void)fprintf(stderr,
                  "%s: stack state differs at control-flow join %u\n",
                  context, (unsigned int)target);
    return 0;
  }
  (*join_count)++;
  return 1;
}

static int validate_call_alignment_function(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *function, ctool_u32 expected_calls,
    ctool_u32 callee_cleanup_call_mask,
    ctool_u32 minimum_conditional_jumps, ctool_u32 minimum_joins,
    ctool_u32 minimum_back_edges,
    const char *context) {
  ctool_u8 *instruction_starts = NULL;
  call_alignment_state_t *states = NULL;
  ctool_u32 *worklist = NULL;
  ctool_u32 decode_cursor = 0u;
  ctool_u32 worklist_count = 0u;
  ctool_u32 worklist_cursor = 0u;
  ctool_u32 call_count = 0u;
  ctool_u32 conditional_jump_count = 0u;
  ctool_u32 join_count = 0u;
  ctool_u32 back_edge_count = 0u;
  int passed = 0;
  if (job == NULL || text == NULL || function == NULL || context == NULL ||
      function->size == 0u ||
      function->value > text->contents.size ||
      function->size > text->contents.size - function->value) {
    goto cleanup;
  }
  instruction_starts = (ctool_u8 *)calloc((size_t)function->size,
                                          sizeof(*instruction_starts));
  states = (call_alignment_state_t *)calloc((size_t)function->size,
                                             sizeof(*states));
  worklist = (ctool_u32 *)calloc((size_t)function->size,
                                 sizeof(*worklist));
  if (instruction_starts == NULL || states == NULL || worklist == NULL) {
    (void)fprintf(stderr, "%s: control-flow allocation failed\n", context);
    goto cleanup;
  }
  while (decode_cursor < function->size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + function->value + decode_cursor,
        function->size - decode_cursor);
    ctool_status_t status;
    instruction_starts[decode_cursor] = 1u;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "%s: decode failed at %u\n", context,
                    (unsigned int)decode_cursor);
      goto cleanup;
    }
    decode_cursor += decoded.consumed;
  }
  if (decode_cursor != function->size) {
    (void)fprintf(stderr, "%s: decoded function size differs\n", context);
    goto cleanup;
  }
  states[0].esp_residue = 12u;
  states[0].ebp_residue = 0u;
  states[0].ebp_known = 0;
  states[0].reached = 1;
  worklist[worklist_count++] = 0u;
  while (worklist_cursor < worklist_count) {
    ctool_u32 cursor = worklist[worklist_cursor++];
    call_alignment_state_t outgoing = states[cursor];
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + function->value + cursor,
        function->size - cursor);
    ctool_u32 fallthrough;
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "%s: reachable decode failed at %u\n", context,
                    (unsigned int)cursor);
      goto cleanup;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_CALL) {
      if (outgoing.esp_residue != 0u) {
        (void)fprintf(stderr,
                      "%s: call %u has ESP residue %u instead of zero\n",
                      context, (unsigned int)call_count,
                      (unsigned int)outgoing.esp_residue);
        goto cleanup;
      }
      if (call_count < 32u &&
          (callee_cleanup_call_mask & (1u << call_count)) != 0u) {
        outgoing.esp_residue = (outgoing.esp_residue + 4u) & 15u;
      }
      call_count++;
    }
    if (!call_alignment_adjust_stack(
            &decoded.instruction, &outgoing.esp_residue,
            &outgoing.ebp_residue, &outgoing.ebp_known)) {
      (void)fprintf(stderr, "%s: stack model failed at %u\n", context,
                    (unsigned int)cursor);
      goto cleanup;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      continue;
    }
    fallthrough = cursor + decoded.consumed;
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_JMP ||
        call_alignment_is_conditional_jump(
            decoded.instruction.mnemonic) != 0) {
      ctool_u32 target;
      if (call_alignment_is_conditional_jump(
              decoded.instruction.mnemonic) != 0) {
        conditional_jump_count++;
      }
      if (!call_alignment_branch_target(&decoded, cursor, function->size,
                                        &target) ||
          !call_alignment_record_state(
              function->size, target, instruction_starts, &outgoing,
              states, worklist, &worklist_count, &join_count,
              context)) {
        goto cleanup;
      }
      if (target <= cursor) {
        back_edge_count++;
      }
      if (decoded.instruction.mnemonic == CTOOL_X86_MN_JMP) {
        continue;
      }
    }
    if (fallthrough >= function->size ||
        !call_alignment_record_state(
            function->size, fallthrough, instruction_starts, &outgoing,
            states, worklist, &worklist_count, &join_count, context)) {
      goto cleanup;
    }
  }
  if (call_count != expected_calls ||
      conditional_jump_count < minimum_conditional_jumps ||
      join_count < minimum_joins || back_edge_count < minimum_back_edges) {
    (void)fprintf(stderr,
                  "%s: CFG inventory differs: calls=%u conditional=%u "
                  "joins=%u back_edges=%u\n",
                  context, (unsigned int)call_count,
                  (unsigned int)conditional_jump_count,
                  (unsigned int)join_count,
                  (unsigned int)back_edge_count);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(worklist);
  free(states);
  free(instruction_starts);
  return passed;
}

#define CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT 128u
#define CALL_ALIGNMENT_SYMBOLIC_ENTRY_SLOT \
  (CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT / 2u)
#define CALL_ALIGNMENT_SYMBOLIC_SAVED_FRAME_SLOTS 1u
#define CALL_ALIGNMENT_SYMBOLIC_CALLEE_BITS 0xc011ee00u

static int call_alignment_symbolic_stack_slot(
    const ctool_x86_memory_t *memory, ctool_u32 esp_slot,
    ctool_u32 *slot_out) {
  int64_t slot;
  ctool_i32 displacement;
  if (memory == NULL || slot_out == NULL || memory->address_bits != 32u ||
      aggregate_register_is(memory->base, 4u) == 0 ||
      memory->index.class_id != CTOOL_X86_REG_NONE ||
      memory->displacement.kind != CTOOL_X86_VALUE_CONSTANT) {
    return 0;
  }
  displacement = (ctool_i32)memory->displacement.bits;
  if ((displacement % 4) != 0) {
    return 0;
  }
  slot = (int64_t)esp_slot + (int64_t)(displacement / 4);
  if (slot < 0 || slot >= (int64_t)CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT) {
    return 0;
  }
  *slot_out = (ctool_u32)slot;
  return 1;
}

static aggregate_symbolic_value_t call_alignment_read_symbolic_operand(
    const ctool_x86_operand_t *operand,
    const aggregate_symbolic_value_t registers[8],
    const aggregate_symbolic_value_t stack[CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT],
    ctool_u32 esp_slot) {
  aggregate_symbolic_value_t value;
  ctool_i32 frame_offset;
  ctool_u32 slot;
  if (operand != NULL && operand->kind == CTOOL_X86_OPERAND_MEMORY &&
      call_alignment_symbolic_stack_slot(&operand->as.memory, esp_slot,
                                         &slot)) {
    return stack[slot];
  }
  if (operand != NULL && operand->kind == CTOOL_X86_OPERAND_MEMORY &&
      aggregate_memory_frame_offset(&operand->as.memory, registers,
                                    &frame_offset) != 0 &&
      frame_offset == 8) {
    value = aggregate_unknown_value();
    value.kind = AGGREGATE_SYMBOLIC_CONSTANT;
    value.bits = CALL_ALIGNMENT_SYMBOLIC_CALLEE_BITS;
    return value;
  }
  return aggregate_read_operand(operand, registers);
}

static int validate_call_alignment_arguments(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *function, ctool_bool indirect,
    const char *context) {
  static const ctool_u32 expected_arguments[] = {
      0x11223344u, 0x55667788u, 0x99aabbccu};
  aggregate_symbolic_value_t registers[8];
  aggregate_symbolic_value_t stack[CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT];
  ctool_u32 argument_count =
      (ctool_u32)(sizeof(expected_arguments) /
                  sizeof(expected_arguments[0]));
  ctool_u32 call_slot = CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT;
  ctool_u32 esp_slot = CALL_ALIGNMENT_SYMBOLIC_ENTRY_SLOT;
  ctool_u32 cursor = 0u;
  ctool_u32 index;
  if (job == NULL || text == NULL || function == NULL || context == NULL ||
      (indirect != CTOOL_FALSE && indirect != CTOOL_TRUE) ||
      text->contents.data == NULL ||
      function->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      function->section_file_index != text->file_index ||
      function->value > text->contents.size ||
      function->size > text->contents.size - function->value) {
    return 0;
  }
  for (index = 0u; index < 8u; index++) {
    registers[index] = aggregate_unknown_value();
  }
  for (index = 0u; index < CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT; index++) {
    stack[index] = aggregate_unknown_value();
  }
  while (cursor < function->size) {
    ctool_x86_decoded_t decoded;
    const ctool_x86_instruction_t *instruction;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + function->value + cursor,
        function->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "%s: argument decode failed at %u\n", context,
                    (unsigned int)cursor);
      return 0;
    }
    instruction = &decoded.instruction;
    if (instruction->mnemonic == CTOOL_X86_MN_PUSH &&
        instruction->operand_count == 1u) {
      if (esp_slot == 0u) {
        (void)fprintf(stderr, "%s: symbolic stack overflowed\n", context);
        return 0;
      }
      esp_slot--;
      stack[esp_slot] = call_alignment_read_symbolic_operand(
          &instruction->operands[0], registers, stack, esp_slot + 1u);
    } else if (instruction->mnemonic == CTOOL_X86_MN_POP &&
               instruction->operand_count == 1u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      ctool_u32 destination;
      if (esp_slot >= CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT ||
          aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) == 0) {
        (void)fprintf(stderr, "%s: symbolic stack underflowed\n", context);
        return 0;
      }
      registers[destination] = stack[esp_slot++];
    } else if (instruction->mnemonic == CTOOL_X86_MN_LEA &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER &&
               instruction->operands[1].kind == CTOOL_X86_OPERAND_MEMORY) {
      ctool_i32 frame_offset;
      ctool_u32 destination;
      if (aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) == 0 ||
          aggregate_memory_frame_offset(
              &instruction->operands[1].as.memory, registers,
              &frame_offset) == 0) {
        return 0;
      }
      registers[destination] = aggregate_unknown_value();
      registers[destination].kind = AGGREGATE_SYMBOLIC_FRAME_ADDRESS;
      registers[destination].frame_offset = frame_offset;
    } else if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
               instruction->operand_count == 2u) {
      const ctool_x86_operand_t *destination = &instruction->operands[0];
      aggregate_symbolic_value_t source =
          call_alignment_read_symbolic_operand(
              &instruction->operands[1], registers, stack, esp_slot);
      if (destination->kind == CTOOL_X86_OPERAND_REGISTER) {
        ctool_u32 destination_index;
        if (aggregate_gpr_index(destination->as.reg,
                                &destination_index) == 0) {
          return 0;
        }
        registers[destination_index] = source;
      } else if (destination->kind == CTOOL_X86_OPERAND_MEMORY) {
        ctool_u32 destination_slot;
        if (!call_alignment_symbolic_stack_slot(
                &destination->as.memory, esp_slot, &destination_slot)) {
          return 0;
        }
        stack[destination_slot] = source;
      } else {
        return 0;
      }
    } else if ((instruction->mnemonic == CTOOL_X86_MN_ADD ||
                instruction->mnemonic == CTOOL_X86_MN_SUB) &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER &&
               aggregate_register_is(
                   instruction->operands[0].as.reg, 4u) != 0 &&
               instruction->operands[1].kind ==
                   CTOOL_X86_OPERAND_IMMEDIATE &&
               instruction->operands[1].as.value.kind ==
                   CTOOL_X86_VALUE_CONSTANT) {
      ctool_u32 byte_count = instruction->operands[1].as.value.bits;
      ctool_u32 slots;
      if ((byte_count & 3u) != 0u) {
        return 0;
      }
      slots = byte_count / 4u;
      if (instruction->mnemonic == CTOOL_X86_MN_SUB) {
        ctool_u32 previous = esp_slot;
        if (slots > esp_slot) {
          return 0;
        }
        esp_slot -= slots;
        for (index = esp_slot; index < previous; index++) {
          stack[index] = aggregate_unknown_value();
        }
      } else {
        ctool_u32 expected_base =
            CALL_ALIGNMENT_SYMBOLIC_ENTRY_SLOT -
            CALL_ALIGNMENT_SYMBOLIC_SAVED_FRAME_SLOTS;
        if (slots > CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT - esp_slot) {
          return 0;
        }
        if (call_slot != CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT &&
            (esp_slot != call_slot || call_slot > expected_base ||
             slots != expected_base - call_slot)) {
          (void)fprintf(stderr,
                        "%s: caller cleanup did not consume the callee, "
                        "three arguments, and padding\n",
                        context);
          return 0;
        }
        esp_slot += slots;
        if (call_slot != CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT) {
          return esp_slot == expected_base ? 1 : 0;
        }
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_CALL) {
      if (call_slot != CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT ||
          esp_slot + argument_count >
              CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT) {
        (void)fprintf(stderr, "%s: call stack shape differs\n", context);
        return 0;
      }
      for (index = 0u; index < argument_count; index++) {
        if (esp_slot + index >= CALL_ALIGNMENT_SYMBOLIC_SLOT_COUNT ||
            stack[esp_slot + index].kind !=
                AGGREGATE_SYMBOLIC_CONSTANT ||
            stack[esp_slot + index].bits != expected_arguments[index]) {
          (void)fprintf(stderr,
                        "%s: argument %u was not preserved across padding\n",
                        context, (unsigned int)index);
          return 0;
        }
      }
      if (indirect == CTOOL_TRUE) {
        if (instruction->operand_count != 1u ||
            instruction->operands[0].kind !=
                CTOOL_X86_OPERAND_REGISTER ||
            aggregate_register_is(instruction->operands[0].as.reg, 0u) ==
                0 ||
            registers[0].kind != AGGREGATE_SYMBOLIC_CONSTANT ||
            registers[0].bits != CALL_ALIGNMENT_SYMBOLIC_CALLEE_BITS) {
          (void)fprintf(stderr,
                        "%s: indirect callee was not preserved below the "
                        "actual arguments\n",
                        context);
          return 0;
        }
      } else if (instruction->operand_count != 1u ||
                 instruction->operands[0].kind !=
                     CTOOL_X86_OPERAND_RELATIVE) {
        (void)fprintf(stderr, "%s: direct call target differs\n", context);
        return 0;
      }
      call_slot = esp_slot;
      registers[0] = aggregate_unknown_value();
    } else {
      (void)fprintf(stderr,
                    "%s: unsupported symbolic instruction at %u\n",
                    context, (unsigned int)cursor);
      return 0;
    }
    cursor += decoded.consumed;
  }
  (void)fprintf(stderr,
                "%s: expected call and caller cleanup were not found\n",
                context);
  return 0;
}

static int validate_call_alignment_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  typedef struct {
    const char *name;
    ctool_u32 calls;
    ctool_u32 callee_cleanup_call_mask;
    ctool_u32 conditional_jumps;
    ctool_u32 joins;
    ctool_u32 back_edges;
  } call_alignment_case_t;
  static const ctool_i32 variadic_structure_frame[] = {8};
  structure_function_expectation_t variadic_structure_expected;
  static const call_alignment_case_t cases[] = {
      {"aligned_zero", 1u, 0u, 0u, 0u, 0u},
      {"aligned_one", 1u, 0u, 0u, 0u, 0u},
      {"aligned_nested", 2u, 0u, 0u, 0u, 0u},
      {"aligned_indirect", 1u, 0u, 0u, 0u, 0u},
      {"aligned_arguments", 1u, 0u, 0u, 0u, 0u},
      {"aligned_variadic_arguments", 1u, 0u, 0u, 0u, 0u},
      {"aligned_variadic_indirect", 1u, 0u, 0u, 0u, 0u},
      {"aligned_variadic_structure", 1u, 0u, 0u, 0u, 0u},
      {"variadic_definition", 0u, 0u, 0u, 0u, 0u},
      {"aligned_branch", 3u, 0u, 1u, 1u, 0u},
      {"aligned_loop", 1u, 0u, 1u, 1u, 1u},
      {"aligned_structure", 2u, 1u, 0u, 0u, 0u},
      {"aligned_structure_indirect", 1u, 1u, 0u, 0u, 0u}};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_symbol_t *variadic_structure;
  ctool_u32 index;
  if (text == NULL || text->contents.data == NULL) {
    (void)fprintf(stderr, "call alignment text section differs\n");
    return 0;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(cases) / sizeof(cases[0])); index++) {
    const ctool_elf32_symbol_t *function =
        find_symbol(object, cases[index].name);
    if (!validate_call_alignment_function(job, text, function,
                                           cases[index].calls,
                                           cases[index].callee_cleanup_call_mask,
                                           cases[index].conditional_jumps,
                                           cases[index].joins,
                                           cases[index].back_edges,
                                           cases[index].name)) {
      return 0;
    }
  }
  if (!validate_call_alignment_arguments(
          job, text, find_symbol(object, "aligned_arguments"),
          CTOOL_FALSE, "aligned_arguments")) {
    return 0;
  }
  if (!validate_call_alignment_arguments(
          job, text, find_symbol(object, "aligned_variadic_arguments"),
          CTOOL_FALSE, "aligned_variadic_arguments")) {
    return 0;
  }
  if (!validate_call_alignment_arguments(
          job, text, find_symbol(object, "aligned_variadic_indirect"),
          CTOOL_TRUE, "aligned_variadic_indirect")) {
    return 0;
  }
  variadic_structure =
      find_symbol(object, "aligned_variadic_structure");
  if (variadic_structure == (const ctool_elf32_symbol_t *)0) {
    return 0;
  }
  (void)memset(&variadic_structure_expected, 0,
               sizeof(variadic_structure_expected));
  variadic_structure_expected.name = "aligned_variadic_structure";
  variadic_structure_expected.value = variadic_structure->value;
  variadic_structure_expected.size = variadic_structure->size;
  variadic_structure_expected.positive_frame_offsets =
      variadic_structure_frame;
  variadic_structure_expected.positive_frame_offset_count = 1u;
  variadic_structure_expected.move_count = 2u;
  variadic_structure_expected.zero_count = 1u;
  variadic_structure_expected.direct_call_count = 1u;
  variadic_structure_expected.plain_return_count = 1u;
  variadic_structure_expected.caller_cleanup = 32u;
  return validate_structure_function(
      job, text, variadic_structure, &variadic_structure_expected);
}

static int run_call_alignment_object(const char *host_root) {
  static const char source[] =
      "typedef unsigned int u32;\n"
      "typedef struct { u32 first; u32 second; } pair_t;\n"
      "extern u32 no_args(void);\n"
      "extern u32 scalar1(u32 value);\n"
      "extern u32 scalar3(u32 first, u32 second, u32 third);\n"
      "extern u32 variadic3(u32 first, ...);\n"
      "typedef u32 (*variadic_callback_t)(u32 first, ...);\n"
      "extern u32 variadic_pair(u32 first, pair_t value, ...);\n"
      "extern pair_t pair_call(u32 before, pair_t value, u32 after);\n"
      "u32 aligned_zero(void) { return no_args(); }\n"
      "u32 aligned_one(u32 value) { return scalar1(value); }\n"
      "u32 aligned_nested(u32 value) {\n"
      "  u32 local = value;\n"
      "  return scalar3(local, no_args(), local);\n"
      "}\n"
      "u32 aligned_indirect(u32 (*function)(u32, u32), u32 value) {\n"
      "  return function(value, value);\n"
      "}\n"
      "u32 aligned_arguments(void) {\n"
      "  return scalar3(0x11223344u, 0x55667788u, 0x99aabbccu);\n"
      "}\n"
      "u32 aligned_variadic_arguments(void) {\n"
      "  return variadic3(0x11223344u, 0x55667788u, 0x99aabbccu);\n"
      "}\n"
      "u32 aligned_variadic_indirect(variadic_callback_t function) {\n"
      "  return function(0x11223344u, 0x55667788u, 0x99aabbccu);\n"
      "}\n"
      "u32 aligned_variadic_structure(pair_t value) {\n"
      "  return variadic_pair(1u, value, 2u);\n"
      "}\n"
      "u32 variadic_definition(u32 first, ...) { return first; }\n"
      "u32 aligned_branch(u32 value) {\n"
      "  u32 selected;\n"
      "  if (value) selected = scalar1(value);\n"
      "  else selected = no_args();\n"
      "  return scalar1(selected);\n"
      "}\n"
      "u32 aligned_loop(u32 count) {\n"
      "  u32 result = 0u;\n"
      "  while (count) {\n"
      "    result = scalar1(result);\n"
      "    count = count - 1u;\n"
      "  }\n"
      "  return result;\n"
      "}\n"
      "pair_t aligned_structure(pair_t value) {\n"
      "  pair_t result = pair_call(1u, value, 2u);\n"
      "  result.first = scalar1(result.first);\n"
      "  return result;\n"
      "}\n"
      "pair_t aligned_structure_indirect(\n"
      "    pair_t (*function)(u32, pair_t, u32), pair_t value) {\n"
      "  return function(1u, value, 2u);\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_arena_mark_t mark;
  ctool_bytes_t first_bytes;
  ctool_bytes_t second_bytes;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/call-alignment.c", source, &unit) ||
      !take_unit_snapshot(&unit, &snapshot)) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 16u, 64u, &limited);
  }
  if (!check_status(status, CTOOL_OK, "call alignment buffers")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  if (!check_status(status, CTOOL_OK, "first call alignment object") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }
  first_bytes = ctool_buffer_view(first);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  if (!check_status(status, CTOOL_OK, "repeat call alignment object") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }
  second_bytes = ctool_buffer_view(second);
  if (first_bytes.size == 0u || second_bytes.size != first_bytes.size ||
      memcmp(second_bytes.data, first_bytes.data,
             (size_t)first_bytes.size) != 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "call alignment object is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/call-alignment.o");
  object_source.contents = second_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read call alignment object") ||
      !validate_call_alignment_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, limited);
  if (!check_status(status, CTOOL_ERR_LIMIT,
                    "limited call alignment object") ||
      ctool_buffer_view(limited).size != 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      !expect_new_diagnostic(job, diagnostic_count, CTOOL_C_EMIT_DIAG_LIMIT,
                             NULL, "limited call alignment object") ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }
  if (ctool_buffer_rewind(first, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  first_bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "recovered call alignment object") ||
      first_bytes.size != second_bytes.size ||
      memcmp(first_bytes.data, second_bytes.data,
             (size_t)second_bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  dispose_unit_snapshot(&snapshot);
  if (limited != (ctool_buffer_t *)0) {
    ctool_buffer_close(limited);
  }
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("call-alignment: ok");
    return 0;
  }
  return 1;
}

static const ctool_elf32_relocation_t *find_relocation_at(
    const ctool_elf32_object_t *object, ctool_u32 target_section,
    ctool_u32 offset) {
  ctool_u32 index;
  for (index = 0u; index < object->relocation_count; index++) {
    if (object->relocations[index].target_section_file_index ==
            target_section &&
        object->relocations[index].offset == offset) {
      return &object->relocations[index];
    }
  }
  return (const ctool_elf32_relocation_t *)0;
}

static int block_static_relocation_matches(
    const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *target_section, ctool_u32 offset,
    const ctool_elf32_symbol_t *symbol) {
  const ctool_elf32_relocation_t *relocation =
      target_section == (const ctool_elf32_section_t *)0
          ? (const ctool_elf32_relocation_t *)0
          : find_relocation_at(object, target_section->file_index, offset);
  return relocation != (const ctool_elf32_relocation_t *)0 &&
                 symbol != (const ctool_elf32_symbol_t *)0 &&
                 relocation->symbol_file_index == symbol->file_index &&
                 relocation->type == CTOOL_ELF32_R_386_32 &&
                 relocation->addend_known == CTOOL_TRUE &&
                 relocation->addend == 0
             ? 1
             : 0;
}

static int validate_block_static_object(const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_rodata[] = {
      '0', '1', '2', '3', 0u, 'o', 'k', 0u, 0u, 0u, 0u,
      0u,  't', 'a', 'g', 0u,  'd', 'e', 'a', 'd', 0u};
  static const ctool_u8 expected_data[] = {
      9u,   0u,   0u,   0u,   5u,   0u,   0u,   0u,
      7u,   0u,   0u,   0u,   8u,   0u,   0u,   0u,
      3u,   0u,   0u,   0u,   0u,   0u,   0u,   0u,
      0u,   0u,   0u,   0u,   0u,   0u,   0u,   0u,
      0u,   0u,   0u,   0u,   0u,   0u,   0u,   0u,
      0x88u, 0x77u, 0x66u, 0x55u, 0x44u, 0x33u, 0x22u, 0x11u,
      11u,  0u,   0u,   0u,   22u,  0u,   0u,   0u};
  static const char *const static_names[] = {
      ".LBS0.hex",    ".LBS1.value", ".LBS2.zero",  ".LBS3.values",
      ".LBS4.holder", ".LBS5.label", ".LBS6.refs",  ".LBS7.wide",
      ".LBS8.same",   ".LBS9.same",  ".LBS10.unused"};
  static const ctool_u32 static_values[] = {0u,  4u, 0u, 8u, 16u, 8u,
                                            32u, 40u, 48u, 52u, 16u};
  static const ctool_u32 static_sizes[] = {5u, 4u, 4u, 8u, 16u, 4u,
                                           8u, 8u, 4u, 4u,  5u};
  static const ctool_u32 text_reference_counts[] = {1u, 2u, 2u, 1u, 1u, 0u,
                                                    1u, 0u, 1u, 1u, 0u};
  static const ctool_u8 short_frame_reservation[] = {0x83u, 0xecu};
  static const ctool_u8 long_frame_reservation[] = {0x81u, 0xecu};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rodata = find_section(object, ".rodata");
  const ctool_elf32_section_t *data = find_section(object, ".data");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_section_t *rel_rodata =
      find_section(object, ".rel.rodata");
  const ctool_elf32_section_t *rel_data = find_section(object, ".rel.data");
  const ctool_elf32_symbol_t *file_state = find_symbol(object, "file_state");
  const ctool_elf32_symbol_t *callback = find_symbol(object, "callback");
  const ctool_elf32_symbol_t *external_state =
      find_symbol(object, "external_state");
  const ctool_elf32_symbol_t *literal_ok = find_symbol(object, ".LC0");
  const ctool_elf32_symbol_t *literal_tag = find_symbol(object, ".LC1");
  const ctool_elf32_symbol_t *bump = find_symbol(object, "bump");
  const ctool_elf32_symbol_t *shadow = find_symbol(object, "shadow");
  const ctool_elf32_symbol_t *dead = find_symbol(object, "dead");
  const ctool_elf32_symbol_t *static_symbols[11];
  ctool_u32 seen_text_references[11];
  ctool_u32 index;
  if (text == (const ctool_elf32_section_t *)0 ||
      rodata == (const ctool_elf32_section_t *)0 ||
      data == (const ctool_elf32_section_t *)0 ||
      bss == (const ctool_elf32_section_t *)0 ||
      rel_text == (const ctool_elf32_section_t *)0 ||
      rel_rodata == (const ctool_elf32_section_t *)0 ||
      rel_data == (const ctool_elf32_section_t *)0 ||
      rodata->contents.size != (ctool_u32)sizeof(expected_rodata) ||
      memcmp(rodata->contents.data, expected_rodata,
             sizeof(expected_rodata)) != 0 ||
      data->contents.size != (ctool_u32)sizeof(expected_data) ||
      memcmp(data->contents.data, expected_data, sizeof(expected_data)) != 0 ||
      bss->size != 4u || bss->alignment != 4u ||
      text->relocation_count != 10u || rodata->relocation_count != 1u ||
      data->relocation_count != 5u || object->relocation_count != 16u ||
      file_state == (const ctool_elf32_symbol_t *)0 ||
      file_state->binding != CTOOL_ELF32_BIND_LOCAL ||
      file_state->type != CTOOL_ELF32_SYMBOL_OBJECT ||
      file_state->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      file_state->section_file_index != data->file_index ||
      file_state->value != 0u || file_state->size != 4u ||
      callback == (const ctool_elf32_symbol_t *)0 ||
      callback->placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      external_state == (const ctool_elf32_symbol_t *)0 ||
      external_state->placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      literal_ok == (const ctool_elf32_symbol_t *)0 ||
      literal_ok->section_file_index != rodata->file_index ||
      literal_ok->value != 5u || literal_ok->size != 3u ||
      literal_tag == (const ctool_elf32_symbol_t *)0 ||
      literal_tag->section_file_index != rodata->file_index ||
      literal_tag->value != 12u || literal_tag->size != 4u ||
      bump == (const ctool_elf32_symbol_t *)0 ||
      shadow == (const ctool_elf32_symbol_t *)0 ||
      dead == (const ctool_elf32_symbol_t *)0 ||
      symbol_bytes_contain(text, bump, short_frame_reservation,
                           (ctool_u32)sizeof(short_frame_reservation)) != 0 ||
      symbol_bytes_contain(text, bump, long_frame_reservation,
                           (ctool_u32)sizeof(long_frame_reservation)) != 0 ||
      symbol_bytes_contain(text, shadow, short_frame_reservation,
                           (ctool_u32)sizeof(short_frame_reservation)) != 0 ||
      symbol_bytes_contain(text, dead, short_frame_reservation,
                           (ctool_u32)sizeof(short_frame_reservation)) != 0) {
    (void)fprintf(
        stderr,
        "block-static ELF inventory differs: sections=%d/%d/%d/%d rel=%d/%d/%d "
        "sizes=%lu/%lu/%lu relocs=%lu/%lu/%lu/%lu\n",
        text != NULL, rodata != NULL, data != NULL, bss != NULL,
        rel_text != NULL, rel_rodata != NULL, rel_data != NULL,
        rodata != NULL ? (unsigned long)rodata->contents.size : 0ul,
        data != NULL ? (unsigned long)data->contents.size : 0ul,
        bss != NULL ? (unsigned long)bss->size : 0ul,
        text != NULL ? (unsigned long)text->relocation_count : 0ul,
        rodata != NULL ? (unsigned long)rodata->relocation_count : 0ul,
        data != NULL ? (unsigned long)data->relocation_count : 0ul,
        (unsigned long)object->relocation_count);
    return 0;
  }
  for (index = 0u; index < 11u; index++) {
    const ctool_elf32_section_t *section =
        index == 0u || index == 5u || index == 10u
            ? rodata
            : (index == 2u ? bss : data);
    static_symbols[index] = find_symbol(object, static_names[index]);
    seen_text_references[index] = 0u;
    if (static_symbols[index] == (const ctool_elf32_symbol_t *)0 ||
        static_symbols[index]->binding != CTOOL_ELF32_BIND_LOCAL ||
        static_symbols[index]->type != CTOOL_ELF32_SYMBOL_OBJECT ||
        static_symbols[index]->visibility != CTOOL_ELF32_VIS_DEFAULT ||
        static_symbols[index]->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        static_symbols[index]->section_file_index != section->file_index ||
        static_symbols[index]->value != static_values[index] ||
        static_symbols[index]->size != static_sizes[index]) {
      (void)fprintf(stderr, "block-static symbol %s differs\n",
                    static_names[index]);
      return 0;
    }
  }
  if (!block_static_relocation_matches(object, data, 20u, file_state) ||
      !block_static_relocation_matches(object, data, 24u, callback) ||
      !block_static_relocation_matches(object, data, 28u, literal_ok) ||
      !block_static_relocation_matches(object, data, 32u, file_state) ||
      !block_static_relocation_matches(object, data, 36u, external_state) ||
      !block_static_relocation_matches(object, rodata, 8u, literal_tag)) {
    (void)fprintf(stderr, "block-static initializer relocations differ\n");
    return 0;
  }
  for (index = 0u; index < object->relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation = &object->relocations[index];
    ctool_u32 symbol_index;
    if (relocation->target_section_file_index != text->file_index) {
      continue;
    }
    if (relocation->type != CTOOL_ELF32_R_386_32 ||
        relocation->addend_known != CTOOL_TRUE || relocation->addend != 0) {
      (void)fprintf(stderr, "block-static text relocation differs\n");
      return 0;
    }
    for (symbol_index = 0u; symbol_index < 11u; symbol_index++) {
      if (relocation->symbol_file_index ==
          static_symbols[symbol_index]->file_index) {
        seen_text_references[symbol_index]++;
        break;
      }
    }
    if (symbol_index == 11u) {
      (void)fprintf(stderr, "block-static text target differs\n");
      return 0;
    }
  }
  for (index = 0u; index < 11u; index++) {
    if (seen_text_references[index] != text_reference_counts[index]) {
      (void)fprintf(stderr, "block-static text count for %s differs\n",
                    static_names[index]);
      return 0;
    }
  }
  return 1;
}

static int run_block_static_object(const char *host_root) {
  static const char source[] =
      "typedef int callback_t(int);\n"
      "typedef struct {\n"
      "  int count; int *state; callback_t *call; const char *name;\n"
      "} holder_t;\n"
      "extern int external_state;\n"
      "extern int callback(int);\n"
      "static int file_state = 9;\n"
      "int bump(int delta) {\n"
      "  static const char hex[] = \"0123\";\n"
      "  static int value = 5;\n"
      "  static int zero;\n"
      "  static int values[2] = {7, 8};\n"
      "  static holder_t holder = {3, &file_state, callback, \"ok\"};\n"
      "  static const char *const label = \"tag\";\n"
      "  static int *refs[2] = {&file_state, &external_state};\n"
      "  static unsigned long long wide = 0x1122334455667788ULL;\n"
      "  value += delta;\n"
      "  zero = value;\n"
      "  return hex[zero & 3] + values[1] + holder.count + *refs[0];\n"
      "}\n"
      "int shadow(int choose) {\n"
      "  static int same = 11;\n"
      "  if (choose) { static int same = 22; return same; }\n"
      "  return same;\n"
      "}\n"
      "int dead(void) { return 0; static const char unused[] = \"dead\"; }\n";
  static const char referenced_wide_source[] =
      "long long read_wide(void) {\n"
      "  static long long wide = 7;\n"
      "  return wide;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t referenced_wide_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_block_binding_t *invalid_block_bindings = NULL;
  ctool_c_initializer_t *invalid_initializers = NULL;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t first_bytes;
  ctool_bytes_t second_bytes;
  ctool_arena_mark_t mark;
  ctool_u32 diagnostic_count;
  ctool_u32 root;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&referenced_wide_unit, 0, sizeof(referenced_wide_unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/block-static.c", source, &unit) ||
      unit.block_binding_count != 11u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 16u, 64u, &limited);
  }
  if (!check_status(status, CTOOL_OK, "block-static object buffer")) {
    goto cleanup;
  }
  if (!expect_object_success_preserves_unit(
          job, &unit, first, "first block-static object")) {
    goto cleanup;
  }
  first_bytes = ctool_buffer_view(first);
  if (!expect_object_success_preserves_unit(
          job, &unit, second, "repeat block-static object")) {
    goto cleanup;
  }
  second_bytes = ctool_buffer_view(second);
  if (first_bytes.size != second_bytes.size ||
      memcmp(first_bytes.data, second_bytes.data,
             (size_t)first_bytes.size) != 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr, "block-static object is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/block-static.o");
  object_source.contents = second_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read block-static object") ||
      !validate_block_static_object(&object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!expect_object_failure_preserves_unit(
          job, &unit, limited, CTOOL_ERR_LIMIT, CTOOL_C_EMIT_DIAG_LIMIT,
          "CupidC object emission exceeded a configured resource limit",
          "limited block-static object")) {
    goto cleanup;
  }
  invalid_block_bindings = (ctool_c_block_binding_t *)malloc(
      (size_t)unit.block_binding_count * sizeof(*invalid_block_bindings));
  invalid_initializers = (ctool_c_initializer_t *)malloc(
      (size_t)unit.initializer_count * sizeof(*invalid_initializers));
  if (invalid_block_bindings == NULL || invalid_initializers == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_block_bindings, unit.block_bindings,
               (size_t)unit.block_binding_count *
                   sizeof(*invalid_block_bindings));
  (void)memcpy(invalid_initializers, unit.initializers,
               (size_t)unit.initializer_count * sizeof(*invalid_initializers));
  invalid_unit = unit;
  invalid_unit.block_bindings = invalid_block_bindings;
  invalid_block_bindings[0].initializer = CTOOL_C_AST_NONE;
  if (ctool_buffer_rewind(first, 0u) != CTOOL_OK ||
      !expect_object_failure_preserves_unit(
          job, &invalid_unit, first, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "missing block-static initializer")) {
    goto cleanup;
  }
  invalid_block_bindings[0] = unit.block_bindings[0];
  invalid_block_bindings[0].initializer = unit.initializer_count;
  if (!expect_object_failure_preserves_unit(
          job, &invalid_unit, first, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "out-of-range block-static initializer")) {
    goto cleanup;
  }
  invalid_block_bindings[0] = unit.block_bindings[0];
  invalid_unit.block_bindings = unit.block_bindings;
  invalid_unit.initializers = invalid_initializers;
  root = unit.block_bindings[0].initializer;
  invalid_initializers[root].type = unit.block_bindings[1].type;
  if (!expect_object_failure_preserves_unit(
          job, &invalid_unit, first, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "mismatched block-static initializer type")) {
    goto cleanup;
  }
  invalid_initializers[root] = unit.initializers[root];
  invalid_initializers[root].kind = CTOOL_C_INITIALIZER_EXPRESSION;
  if (!expect_object_failure_preserves_unit(
          job, &invalid_unit, first, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "runtime block-static initializer")) {
    goto cleanup;
  }
  if (!parse_source(job, "/referenced-wide-static.c", referenced_wide_source,
                    &referenced_wide_unit) ||
      !expect_object_success_preserves_unit(
          job, &referenced_wide_unit, first,
          "referenced wide block-static object") ||
      ctool_buffer_rewind(first, 0u) != CTOOL_OK) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  first_bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "recovered block-static object") ||
      first_bytes.size != second_bytes.size ||
      memcmp(first_bytes.data, second_bytes.data,
             (size_t)second_bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_initializers);
  free(invalid_block_bindings);
  dispose_unit_snapshot(&snapshot);
  if (limited != (ctool_buffer_t *)0) {
    ctool_buffer_close(limited);
  }
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("block-statics: ok");
    return 0;
  }
  return 1;
}

static int validate_compound_literal_function(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *function, ctool_u32 *call_offset_out) {
  aggregate_symbolic_value_t registers[8];
  aggregate_symbolic_value_t stack[AGGREGATE_SYMBOLIC_STACK_LIMIT];
  static const ctool_i32 expected_member_offsets[] = {0, 4};
  ctool_u32 stack_depth = 0u;
  ctool_u32 cursor = 0u;
  ctool_u32 member_store_count = 0u;
  ctool_u32 compound_zero_count = 0u;
  ctool_u32 outgoing_zero_count = 0u;
  ctool_u32 structure_copy_count = 0u;
  ctool_u32 staging_commit_count = 0u;
  ctool_u32 persistent_read_count = 0u;
  ctool_u32 call_count = 0u;
  ctool_u32 return_count = 0u;
  ctool_i32 staging_start = 0;
  ctool_i32 persistent_start = 0;
  int cld_ready = 0;
  ctool_u32 index;
  if (job == (ctool_job_t *)0 || text == (const ctool_elf32_section_t *)0 ||
      function == (const ctool_elf32_symbol_t *)0 ||
      call_offset_out == (ctool_u32 *)0 || text->contents.data == NULL ||
      function->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      function->section_file_index != text->file_index ||
      function->value > text->contents.size ||
      function->size > text->contents.size - function->value) {
    (void)fprintf(stderr, "compound literal function range differs\n");
    return 0;
  }
  for (index = 0u; index < 8u; index++) {
    registers[index] = aggregate_unknown_value();
  }
  *call_offset_out = CTOOL_C_AST_NONE;
  while (cursor < function->size) {
    ctool_x86_decoded_t decoded;
    const ctool_x86_instruction_t *instruction;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + function->value + cursor,
        function->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr,
                    "compound literal decode failed at byte %u\n", cursor);
      return 0;
    }
    instruction = &decoded.instruction;
    if (instruction->mnemonic == CTOOL_X86_MN_PUSH &&
        instruction->operand_count == 1u) {
      if (stack_depth >= AGGREGATE_SYMBOLIC_STACK_LIMIT) {
        (void)fprintf(stderr, "compound literal symbolic stack overflowed\n");
        return 0;
      }
      stack[stack_depth++] =
          aggregate_read_operand(&instruction->operands[0], registers);
    } else if (instruction->mnemonic == CTOOL_X86_MN_POP &&
               instruction->operand_count == 1u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      ctool_u32 destination;
      if (stack_depth == 0u ||
          aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) == 0) {
        (void)fprintf(stderr, "compound literal symbolic stack underflowed\n");
        return 0;
      }
      registers[destination] = stack[--stack_depth];
    } else if (instruction->mnemonic == CTOOL_X86_MN_LEA &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER &&
               instruction->operands[1].kind == CTOOL_X86_OPERAND_MEMORY) {
      ctool_u32 destination;
      ctool_i32 frame_offset;
      if (aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) == 0) {
        return 0;
      }
      if (aggregate_memory_frame_offset(
              &instruction->operands[1].as.memory, registers,
              &frame_offset) != 0) {
        registers[destination].kind = AGGREGATE_SYMBOLIC_FRAME_ADDRESS;
        registers[destination].frame_offset = frame_offset;
        registers[destination].bits = 0u;
      } else {
        registers[destination] = aggregate_unknown_value();
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
               instruction->operand_count == 2u) {
      const ctool_x86_operand_t *destination = &instruction->operands[0];
      const ctool_x86_operand_t *source = &instruction->operands[1];
      if (destination->kind == CTOOL_X86_OPERAND_REGISTER) {
        ctool_u32 destination_index;
        if (aggregate_gpr_index(destination->as.reg,
                                &destination_index) != 0) {
          registers[destination_index] =
              aggregate_read_operand(source, registers);
        }
      } else if (destination->kind == CTOOL_X86_OPERAND_MEMORY &&
                 compound_zero_count != 0u) {
        ctool_i32 frame_offset;
        if (aggregate_memory_frame_offset(&destination->as.memory, registers,
                                          &frame_offset) != 0 &&
            frame_offset >= staging_start &&
            frame_offset < staging_start + 8) {
          ctool_i32 relative = frame_offset - staging_start;
          if (member_store_count >=
                  (ctool_u32)(sizeof(expected_member_offsets) /
                              sizeof(expected_member_offsets[0])) ||
              destination->width_bits != 32u ||
              relative != expected_member_offsets[member_store_count]) {
            (void)fprintf(stderr,
                          "compound literal member store %u differs\n",
                          member_store_count);
            return 0;
          }
          member_store_count++;
        }
      }
    } else if ((instruction->mnemonic == CTOOL_X86_MN_ADD ||
                instruction->mnemonic == CTOOL_X86_MN_SUB) &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      ctool_u32 destination;
      aggregate_symbolic_value_t amount =
          aggregate_read_operand(&instruction->operands[1], registers);
      if (aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) != 0) {
        if (instruction->mnemonic == CTOOL_X86_MN_SUB &&
            instruction->operands[1].kind ==
                CTOOL_X86_OPERAND_REGISTER &&
            aggregate_register_is(instruction->operands[1].as.reg,
                                  destination)) {
          registers[destination].kind = AGGREGATE_SYMBOLIC_CONSTANT;
          registers[destination].bits = 0u;
        } else if (amount.kind == AGGREGATE_SYMBOLIC_CONSTANT &&
                   aggregate_adjust_symbolic_value(
                       &registers[destination],
                       instruction->mnemonic == CTOOL_X86_MN_ADD
                           ? (ctool_i32)amount.bits
                           : -(ctool_i32)amount.bits) == 0) {
          return 0;
        } else if (amount.kind != AGGREGATE_SYMBOLIC_CONSTANT) {
          registers[destination] = aggregate_unknown_value();
        }
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_XOR &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER &&
               instruction->operands[1].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      ctool_u32 destination;
      if (aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) != 0) {
        if (aggregate_register_is(instruction->operands[1].as.reg,
                                  destination)) {
          registers[destination].kind = AGGREGATE_SYMBOLIC_CONSTANT;
          registers[destination].bits = 0u;
        } else {
          registers[destination] = aggregate_unknown_value();
        }
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_CLD) {
      cld_ready = 1;
    } else if (instruction->mnemonic == CTOOL_X86_MN_STOSB) {
      if (instruction->prefixes != CTOOL_X86_PREFIX_REP || cld_ready == 0 ||
          (registers[7].kind != AGGREGATE_SYMBOLIC_FRAME_ADDRESS &&
           registers[7].kind != AGGREGATE_SYMBOLIC_UNKNOWN) ||
          registers[1].kind != AGGREGATE_SYMBOLIC_CONSTANT ||
          registers[0].kind != AGGREGATE_SYMBOLIC_CONSTANT ||
          registers[0].bits != 0u) {
        (void)fprintf(stderr, "compound literal zeroing shape differs\n");
        return 0;
      }
      if (registers[7].kind == AGGREGATE_SYMBOLIC_FRAME_ADDRESS &&
          registers[1].bits == 8u) {
        if (compound_zero_count != 0u ||
            registers[7].frame_offset >= 0) {
          (void)fprintf(stderr,
                        "compound literal staging object differs\n");
          return 0;
        }
        staging_start = registers[7].frame_offset;
        compound_zero_count++;
      } else if (registers[1].bits == 16u) {
        outgoing_zero_count++;
      } else {
        (void)fprintf(stderr,
                      "compound literal zero extent differs: %u\n",
                      registers[1].bits);
        return 0;
      }
      cld_ready = 0;
      (void)aggregate_adjust_symbolic_value(
          &registers[7], (ctool_i32)registers[1].bits);
      registers[1].bits = 0u;
    } else if (instruction->mnemonic == CTOOL_X86_MN_MOVSB) {
      if (instruction->prefixes != CTOOL_X86_PREFIX_REP || cld_ready == 0 ||
          registers[1].kind != AGGREGATE_SYMBOLIC_CONSTANT ||
          registers[1].bits != 8u) {
        (void)fprintf(stderr, "compound literal structure copy differs\n");
        return 0;
      }
      if (registers[6].kind == AGGREGATE_SYMBOLIC_FRAME_ADDRESS &&
          registers[6].frame_offset == staging_start) {
        if (staging_commit_count != 0u || member_store_count != 2u ||
            registers[7].kind != AGGREGATE_SYMBOLIC_FRAME_ADDRESS ||
            registers[7].frame_offset >= 0 ||
            registers[7].frame_offset == staging_start ||
            (registers[7].frame_offset < staging_start + 8 &&
             staging_start < registers[7].frame_offset + 8) ||
            (registers[7].frame_offset & 3) != 0) {
          (void)fprintf(stderr,
                        "compound literal staging commit differs\n");
          return 0;
        }
        persistent_start = registers[7].frame_offset;
        staging_commit_count++;
      } else if (staging_commit_count != 0u &&
                 registers[6].kind ==
                     AGGREGATE_SYMBOLIC_FRAME_ADDRESS &&
                 registers[6].frame_offset == persistent_start) {
        persistent_read_count++;
      }
      structure_copy_count++;
      cld_ready = 0;
      (void)aggregate_adjust_symbolic_value(&registers[6], 8);
      (void)aggregate_adjust_symbolic_value(&registers[7], 8);
      registers[1].bits = 0u;
    } else if (instruction->mnemonic == CTOOL_X86_MN_CALL) {
      const ctool_x86_field_t *field;
      if (call_count != 0u || instruction->operand_count != 1u ||
          instruction->operands[0].kind != CTOOL_X86_OPERAND_RELATIVE ||
          decoded.encoding.field_count != 1u) {
        (void)fprintf(stderr, "compound literal call shape differs\n");
        return 0;
      }
      field = &decoded.encoding.fields[0];
      if (field->kind != CTOOL_X86_FIELD_RELATIVE ||
          field->byte_width != 4u ||
          function->value > 0xffffffffu - cursor ||
          function->value + cursor > 0xffffffffu - field->byte_offset) {
        (void)fprintf(stderr, "compound literal call field differs\n");
        return 0;
      }
      *call_offset_out =
          function->value + cursor + (ctool_u32)field->byte_offset;
      call_count++;
      registers[0] = aggregate_unknown_value();
      registers[1] = aggregate_unknown_value();
      registers[2] = aggregate_unknown_value();
    } else if (instruction->mnemonic == CTOOL_X86_MN_RET) {
      return_count++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != function->size || compound_zero_count != 1u ||
      outgoing_zero_count != 1u || member_store_count != 2u ||
      staging_commit_count != 1u || persistent_read_count == 0u ||
      structure_copy_count < 3u || call_count != 1u || return_count != 1u ||
      *call_offset_out == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr,
                  "compound literal instruction inventory differs: "
                  "zero=%u outgoing-zero=%u stores=%u commits=%u reads=%u "
                  "copies=%u calls=%u returns=%u\n",
                  compound_zero_count, outgoing_zero_count,
                  member_store_count, staging_commit_count,
                  persistent_read_count, structure_copy_count, call_count,
                  return_count);
    return 0;
  }
  return 1;
}

static int validate_compound_literal_array_function(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *function) {
  aggregate_symbolic_value_t registers[8];
  ctool_u32 cursor = 0u;
  ctool_u32 zero_count = 0u;
  ctool_u32 element_store_count = 0u;
  ctool_u32 commit_count = 0u;
  ctool_u32 persistent_read_count = 0u;
  ctool_u32 return_count = 0u;
  ctool_u32 index;
  if (job == (ctool_job_t *)0 || text == (const ctool_elf32_section_t *)0 ||
      function == (const ctool_elf32_symbol_t *)0 ||
      text->contents.data == NULL ||
      function->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      function->section_file_index != text->file_index ||
      function->value > text->contents.size ||
      function->size > text->contents.size - function->value) {
    return 0;
  }
  for (index = 0u; index < 8u; index++) {
    registers[index] = aggregate_unknown_value();
  }
  while (cursor < function->size) {
    ctool_x86_decoded_t decoded;
    const ctool_x86_instruction_t *instruction;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + function->value + cursor,
        function->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr,
                    "array compound literal decode failed at byte %u\n",
                    cursor);
      return 0;
    }
    instruction = &decoded.instruction;
    if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
        instruction->operand_count == 2u) {
      const ctool_x86_operand_t *destination = &instruction->operands[0];
      const ctool_x86_operand_t *source = &instruction->operands[1];
      if (destination->kind == CTOOL_X86_OPERAND_REGISTER) {
        ctool_u32 destination_index;
        if (aggregate_gpr_index(destination->as.reg,
                                &destination_index) != 0) {
          registers[destination_index] =
              aggregate_read_operand(source, registers);
        }
      } else if (destination->kind == CTOOL_X86_OPERAND_MEMORY &&
                 source->kind == CTOOL_X86_OPERAND_REGISTER &&
                 destination->width_bits == 32u && zero_count == 1u &&
                 commit_count == 0u) {
        element_store_count++;
      }
      if (source->kind == CTOOL_X86_OPERAND_MEMORY &&
          source->width_bits == 32u && commit_count == 1u) {
        persistent_read_count++;
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_XOR &&
               instruction->operand_count == 2u &&
               instruction->operands[0].kind ==
                   CTOOL_X86_OPERAND_REGISTER &&
               instruction->operands[1].kind ==
                   CTOOL_X86_OPERAND_REGISTER) {
      ctool_u32 destination;
      if (aggregate_gpr_index(instruction->operands[0].as.reg,
                              &destination) != 0) {
        if (aggregate_register_is(instruction->operands[1].as.reg,
                                  destination)) {
          registers[destination].kind = AGGREGATE_SYMBOLIC_CONSTANT;
          registers[destination].bits = 0u;
        } else {
          registers[destination] = aggregate_unknown_value();
        }
      }
    } else if (instruction->mnemonic == CTOOL_X86_MN_STOSB) {
      if (instruction->prefixes != CTOOL_X86_PREFIX_REP ||
          registers[1].kind != AGGREGATE_SYMBOLIC_CONSTANT ||
          registers[1].bits != 8u || zero_count != 0u ||
          commit_count != 0u) {
        (void)fprintf(stderr, "array compound literal zero differs\n");
        return 0;
      }
      zero_count++;
    } else if (instruction->mnemonic == CTOOL_X86_MN_MOVSB) {
      if (instruction->prefixes != CTOOL_X86_PREFIX_REP ||
          registers[1].kind != AGGREGATE_SYMBOLIC_CONSTANT ||
          registers[1].bits != 8u || zero_count != 1u ||
          element_store_count != 2u || commit_count != 0u) {
        (void)fprintf(stderr, "array compound literal commit differs\n");
        return 0;
      }
      commit_count++;
    } else if (instruction->mnemonic == CTOOL_X86_MN_RET) {
      return_count++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != function->size || zero_count != 1u ||
      element_store_count != 2u || commit_count != 1u ||
      persistent_read_count == 0u || return_count != 1u) {
    (void)fprintf(stderr,
                  "array compound literal instruction inventory differs: "
                  "zero=%u stores=%u commits=%u reads=%u returns=%u\n",
                  zero_count, element_store_count, commit_count,
                  persistent_read_count, return_count);
    return 0;
  }
  return 1;
}

static int validate_compound_literal_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *target =
      find_symbol(object, "pp_string_equal");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "pp_string_equal_literal");
  const ctool_elf32_symbol_t *array_function =
      find_symbol(object, "array_literal_value");
  const ctool_elf32_relocation_t *relocation;
  ctool_u32 call_offset = CTOOL_C_AST_NONE;
  if (text == (const ctool_elf32_section_t *)0 ||
      rel_text == (const ctool_elf32_section_t *)0 ||
      target == (const ctool_elf32_symbol_t *)0 ||
      function == (const ctool_elf32_symbol_t *)0 ||
      array_function == (const ctool_elf32_symbol_t *)0 ||
      text->contents.data == NULL || object->symbol_count != 4u ||
      object->relocation_count != 1u || object->relocations == NULL ||
      text->relocation_first != 0u || text->relocation_count != 1u ||
      !symbol_matches(target, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED,
                      CTOOL_ELF32_NO_SECTION, 0u, 0u) ||
      !symbol_matches(function, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      function->size) ||
      !symbol_matches(array_function, 3u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      array_function->value, array_function->size) ||
      function->size == 0u || array_function->size == 0u ||
      function->size > array_function->value ||
      array_function->value > text->contents.size ||
      array_function->size !=
          text->contents.size - array_function->value ||
      !validate_compound_literal_function(job, text, function,
                                          &call_offset) ||
      !validate_compound_literal_array_function(job, text,
                                                array_function) ||
      !validate_call_alignment_function(job, text, function, 1u, 0u, 0u,
                                        0u, 0u,
                                        "pp_string_equal_literal")) {
    (void)fprintf(stderr, "compound literal object inventory differs\n");
    return 0;
  }
  relocation = &object->relocations[0];
  if (relocation->relocation_section_file_index != rel_text->file_index ||
      relocation->entry_index != 0u ||
      relocation->target_section_file_index != text->file_index ||
      relocation->offset != call_offset ||
      relocation->symbol_file_index != target->file_index ||
      relocation->type != CTOOL_ELF32_R_386_PC32 ||
      relocation->addend_known != CTOOL_TRUE || relocation->addend != -4) {
    (void)fprintf(stderr, "compound literal call relocation differs\n");
    return 0;
  }
  return 1;
}

static int run_compound_literal_object(const char *host_root) {
  static const char source[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef int ctool_bool;\n"
      "typedef struct { const char *data; ctool_u32 size; } ctool_string_t;\n"
      "ctool_bool pp_string_equal(ctool_string_t left, ctool_string_t right);\n"
      "ctool_bool pp_string_equal_literal(ctool_string_t value,\n"
      "                                   const char *literal,\n"
      "                                   ctool_u32 size) {\n"
      "  return pp_string_equal(value, (ctool_string_t){literal, size});\n"
      "}\n"
      "ctool_u32 array_literal_value(ctool_u32 left, ctool_u32 right) {\n"
      "  return *(ctool_u32[]){left, right};\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_arena_mark_t mark;
  ctool_bytes_t first_bytes;
  ctool_bytes_t second_bytes;
  ctool_u8 *expected_object = NULL;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_contains(
          job, "/toolchain/cupidc_pp.c",
          "load active CupidC preprocessor source",
          "the active compound-literal call to pp_string_equal changed",
          active_compound_literal_call, NULL) ||
      !parse_source(job, "/compound-literal-object.c", source, &unit) ||
      unit.function_definition_count != 2u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    (void)fprintf(stderr, "compound literal object setup failed\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "compound literal object buffers")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  first_bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first compound literal object") ||
      first_bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  expected_object = (ctool_u8 *)malloc((size_t)first_bytes.size);
  if (expected_object == NULL) {
    goto cleanup;
  }
  (void)memcpy(expected_object, first_bytes.data,
               (size_t)first_bytes.size);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  second_bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat compound literal object") ||
      second_bytes.size != first_bytes.size ||
      memcmp(second_bytes.data, expected_object,
             (size_t)second_bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr,
                  "compound literal object is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/compound-literal-object.o");
  object_source.contents = second_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read compound literal object") ||
      !validate_compound_literal_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(expected_object);
  dispose_unit_snapshot(&snapshot);
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("compound-literals: ok");
    return 0;
  }
  return 1;
}

static int variadic_oracle_execute(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *symbol, ctool_u32 first,
    ctool_u32 second, ctool_u32 third, ctool_u32 *result_out) {
  narrow_oracle_machine_t machine;
  ctool_u32 first_pointer = 224u;
  ctool_u32 cursor = 0u;
  ctool_u32 preserved;
  ctool_bool returned = CTOOL_FALSE;
  if (job == NULL || text == NULL || symbol == NULL || result_out == NULL ||
      symbol->value > text->contents.size ||
      symbol->size > text->contents.size - symbol->value) {
    return 0;
  }
  (void)memset(&machine, 0, sizeof(machine));
  machine.registers[NARROW_ORACLE_ESP] = NARROW_ORACLE_INITIAL_ESP;
  machine.registers[NARROW_ORACLE_EBP] = 64u;
  if (!narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP, 32u, 0x13579bdfu) ||
      !narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 4u, 32u, 0x11111111u) ||
      !narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 8u, 32u, 0x22222222u) ||
      !narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 12u, 32u,
          first_pointer) ||
      !narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 16u, 32u, second) ||
      !narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 20u, 32u, third) ||
      !narrow_oracle_write_memory(
          &machine, first_pointer, 32u, first)) {
    return 0;
  }
  while (cursor < symbol->size && returned == CTOOL_FALSE) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + symbol->value + cursor,
        symbol->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u ||
        !narrow_oracle_step(&machine, &decoded.instruction, &returned)) {
      (void)fprintf(
          stderr, "variadic callee oracle stopped at %u on %s\n",
          (unsigned int)cursor,
          status == CTOOL_OK && decoded.kind == CTOOL_X86_DECODE_KNOWN
              ? ctool_x86_mnemonic_name(decoded.instruction.mnemonic).data
              : "invalid instruction");
      return 0;
    }
    cursor += decoded.consumed;
  }
  if (returned == CTOOL_FALSE || cursor != symbol->size ||
      machine.registers[NARROW_ORACLE_ESP] != NARROW_ORACLE_INITIAL_ESP ||
      !narrow_oracle_read_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP, 32u, &preserved) ||
      preserved != 0x13579bdfu ||
      !narrow_oracle_read_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 4u, 32u, &preserved) ||
      preserved != 0x11111111u ||
      !narrow_oracle_read_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 8u, 32u, &preserved) ||
      preserved != 0x22222222u ||
      !narrow_oracle_read_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 12u, 32u, &preserved) ||
      preserved != first_pointer ||
      !narrow_oracle_read_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 16u, 32u, &preserved) ||
      preserved != second ||
      !narrow_oracle_read_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 20u, 32u, &preserved) ||
      preserved != third) {
    return 0;
  }
  *result_out = machine.registers[NARROW_ORACLE_EAX];
  return 1;
}

static int validate_variadic_callee_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_symbol_t *function = find_symbol(object, "sum_after");
  ctool_u32 cursor = 0u;
  ctool_u32 positive_frame_offsets = 0u;
  ctool_u32 returns = 0u;
  ctool_u32 leaves = 0u;
  ctool_u32 result = 0u;
  if (text == NULL || function == NULL || text->relocation_count != 0u ||
      object->relocation_count != 0u || function->size == 0u ||
      function->size != text->contents.size ||
      !symbol_matches(function, function->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      text->contents.size)) {
    return 0;
  }
  while (cursor < function->size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + function->value + cursor,
        function->size - cursor);
    ctool_u32 operand;
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      return 0;
    }
    for (operand = 0u; operand < decoded.instruction.operand_count;
         operand++) {
      const ctool_x86_operand_t *value =
          &decoded.instruction.operands[operand];
      if (value->kind == CTOOL_X86_OPERAND_MEMORY &&
          value->as.memory.base.class_id == CTOOL_X86_REG_GPR32 &&
          value->as.memory.base.index == NARROW_ORACLE_EBP &&
          value->as.memory.displacement.kind ==
              CTOOL_X86_VALUE_CONSTANT &&
          (ctool_i32)value->as.memory.displacement.bits > 0) {
        if ((ctool_i32)value->as.memory.displacement.bits != 16) {
          return 0;
        }
        positive_frame_offsets++;
      }
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      returns++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_LEAVE) {
      leaves++;
    }
    cursor += decoded.consumed;
  }
  return cursor == function->size && positive_frame_offsets == 1u &&
                 returns == 1u && leaves == 1u &&
                 variadic_oracle_execute(
                     job, text, function, 0x01020304u, 0x10203040u,
                     0x55667788u, &result) &&
                 result == 0x21426384u
             ? 1
             : 0;
}

static int validate_old_style_empty_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *tick = find_symbol(object, "tick");
  const ctool_elf32_symbol_t *invoke = find_symbol(object, "invoke");
  const ctool_elf32_symbol_t *consume = find_symbol(object, "consume");
  const ctool_elf32_symbol_t *promoted =
      find_symbol(object, "invoke_promoted");
  const ctool_elf32_symbol_t *indirect =
      find_symbol(object, "invoke_indirect");
  ctool_u32 cursor;
  ctool_u32 call_offset = CTOOL_C_AST_NONE;
  ctool_u32 calls = 0u;
  ctool_u32 returns = 0u;
  ctool_u32 tick_calls = 0u;
  ctool_u32 consume_calls = 0u;
  ctool_u32 relocation;

  if (text == NULL || rel_text == NULL || tick == NULL || invoke == NULL ||
      consume == NULL || promoted == NULL || indirect == NULL ||
      text->contents.data == NULL || object->symbol_count != 6u ||
      object->relocation_count != 2u || object->relocations == NULL ||
      text->relocation_first != 0u || text->relocation_count != 2u ||
      text->contents.size != 181u ||
      structure_text_fingerprint(text->contents) != 0x898ebd57u ||
      tick->size != 11u || invoke->size != 18u || promoted->size != 70u ||
      indirect->size != 82u || tick->value != 0u ||
      invoke->value != tick->size ||
      promoted->value != invoke->value + invoke->size ||
      indirect->value != promoted->value + promoted->size ||
      indirect->size != text->contents.size - indirect->value ||
      !symbol_matches(tick, 1u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      tick->size) ||
      !symbol_matches(invoke, 2u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      invoke->value, invoke->size) ||
      !symbol_matches(promoted, 3u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      promoted->value, promoted->size) ||
      !symbol_matches(indirect, 4u, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      indirect->value, indirect->size) ||
      !symbol_matches(consume, 5u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u) ||
      !validate_call_alignment_function(job, text, invoke, 1u, 0u, 0u, 0u,
                                        0u, "invoke") ||
      !validate_call_alignment_function(job, text, promoted, 1u, 0u, 0u, 0u,
                                        0u, "invoke_promoted") ||
      !validate_call_alignment_function(job, text, indirect, 1u, 0u, 0u, 0u,
                                        0u, "invoke_indirect") ||
      !validate_call_alignment_arguments(
          job, text, promoted, CTOOL_FALSE, "invoke_promoted") ||
      !validate_call_alignment_arguments(
          job, text, indirect, CTOOL_TRUE, "invoke_indirect")) {
    return 0;
  }
  cursor = 0u;
  while (cursor < invoke->size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + invoke->value + cursor,
        invoke->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u, &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      return 0;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_CALL) {
      call_offset = invoke->value + cursor + 1u;
      calls++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      returns++;
    }
    cursor += decoded.consumed;
  }
  for (relocation = 0u; relocation < object->relocation_count; relocation++) {
    const ctool_elf32_relocation_t *entry = &object->relocations[relocation];
    if (entry->relocation_section_file_index != rel_text->file_index ||
        entry->entry_index != relocation ||
        entry->target_section_file_index != text->file_index ||
        entry->addend_known != CTOOL_TRUE) {
      return 0;
    }
    if (entry->type == CTOOL_ELF32_R_386_PC32 && entry->addend == -4 &&
        entry->symbol_file_index == tick->file_index &&
        entry->offset == call_offset) {
      tick_calls++;
    } else if (entry->type == CTOOL_ELF32_R_386_PC32 &&
               entry->addend == -4 &&
               entry->symbol_file_index == consume->file_index) {
      consume_calls++;
    } else {
      return 0;
    }
  }
  return cursor == invoke->size && calls == 1u && returns == 1u &&
                 call_offset != CTOOL_C_AST_NONE && tick_calls == 1u &&
                 consume_calls == 1u
             ? 1
             : 0;
}

static int run_old_style_empty_object(const char *host_root) {
  static const char source[] =
      "extern int consume();\n"
      "static int tick();\n"
      "static int tick()\n"
      "{\n"
      "  return 37;\n"
      "}\n"
      "static int invoke(void)\n"
      "{\n"
      "  return tick();\n"
      "}\n"
      "static int invoke_promoted(void)\n"
      "{\n"
      "  return consume(0x11223344u, 0x55667788u, 0x99aabbccu);\n"
      "}\n"
      "static int invoke_indirect(int (*callee)())\n"
      "{\n"
      "  return callee(0x11223344u, 0x55667788u, 0x99aabbccu);\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t first_bytes;
  ctool_bytes_t second_bytes;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));

  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source_mode(job, "/old-style-empty-object.c", source,
                         CTOOL_FALSE, &unit) ||
      unit.function_definition_count != 4u) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "old-style empty object buffers") ||
      !expect_object_success_preserves_unit(
          job, &unit, first, "first old-style empty object") ||
      !expect_object_success_preserves_unit(
          job, &unit, second, "repeat old-style empty object")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  first_bytes = ctool_buffer_view(first);
  second_bytes = ctool_buffer_view(second);
  if (first_bytes.size != second_bytes.size ||
      memcmp(first_bytes.data, second_bytes.data, first_bytes.size) != 0) {
    (void)fprintf(stderr, "old-style empty object is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/old-style-empty-object.o");
  object_source.contents = second_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read old-style empty object") ||
      !validate_old_style_empty_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("old-style-empty-functions: ok");
    return 0;
  }
  return 1;
}

static int validate_block_record_object(
    const ctool_elf32_object_t *object) {
  static const ctool_u8 expected_rodata[] = {
      0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u,
      0u, 0u, 0u, 0u, 2u, 0u, 0u, 0u,
      0u, 0u, 0u, 0u, 3u, 0u, 0u, 0u,
      'd', 'o', 'o', 'm', '2', 0u,
      't', 'n', 't', 0u,
      'p', 'l', 'u', 't', 'o', 'n', 'i', 'a', 0u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rodata = find_section(object, ".rodata");
  const ctool_elf32_symbol_t *function =
      find_symbol(object, "block_records");
  const ctool_elf32_symbol_t *static_function =
      find_symbol(object, "block_record_static");
  const ctool_elf32_symbol_t *packs = find_symbol(object, ".LBS1.packs");
  const ctool_elf32_symbol_t *doom2 = find_symbol(object, ".LC0");
  const ctool_elf32_symbol_t *tnt = find_symbol(object, ".LC1");
  const ctool_elf32_symbol_t *plutonia = find_symbol(object, ".LC2");
  const ctool_elf32_relocation_t *packs_reference =
      text == (const ctool_elf32_section_t *)0 ||
              text->relocation_count != 1u ||
              text->relocation_first >= object->relocation_count
          ? (const ctool_elf32_relocation_t *)0
          : &object->relocations[text->relocation_first];
  ctool_u32 fingerprint =
      text == (const ctool_elf32_section_t *)0
          ? 0u
          : structure_text_fingerprint(text->contents);
  if (text == (const ctool_elf32_section_t *)0 ||
      rodata == (const ctool_elf32_section_t *)0 ||
      function == (const ctool_elf32_symbol_t *)0 ||
      static_function == (const ctool_elf32_symbol_t *)0 ||
      packs == (const ctool_elf32_symbol_t *)0 ||
      doom2 == (const ctool_elf32_symbol_t *)0 ||
      tnt == (const ctool_elf32_symbol_t *)0 ||
      plutonia == (const ctool_elf32_symbol_t *)0 ||
      text->contents.size != 91u || fingerprint != 0x8c3e7e1cu ||
      text->relocation_count != 1u || rodata->contents.size != 43u ||
      rodata->size != 43u || rodata->alignment != 4u ||
      rodata->relocation_count != 3u ||
      memcmp(rodata->contents.data, expected_rodata,
             sizeof(expected_rodata)) != 0 ||
      object->symbol_count != 7u || object->relocation_count != 4u ||
      !symbol_matches(function, function->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      53u) ||
      !symbol_matches(static_function, static_function->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 53u,
                      38u) ||
      !symbol_matches(packs, packs->file_index, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, rodata->file_index, 0u,
                      24u) ||
      !symbol_matches(doom2, doom2->file_index, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, rodata->file_index, 24u,
                      6u) ||
      !symbol_matches(tnt, tnt->file_index, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, rodata->file_index, 30u,
                      4u) ||
      !symbol_matches(plutonia, plutonia->file_index,
                      CTOOL_ELF32_BIND_LOCAL, CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, rodata->file_index, 34u,
                      9u) ||
      packs_reference == (const ctool_elf32_relocation_t *)0 ||
      packs_reference->target_section_file_index != text->file_index ||
      packs_reference->offset != 57u ||
      packs_reference->symbol_file_index != packs->file_index ||
      packs_reference->type != CTOOL_ELF32_R_386_32 ||
      packs_reference->addend_known != CTOOL_TRUE ||
      packs_reference->addend != 0 ||
      !block_static_relocation_matches(object, rodata, 0u, doom2) ||
      !block_static_relocation_matches(object, rodata, 8u, tnt) ||
      !block_static_relocation_matches(object, rodata, 16u, plutonia)) {
    (void)fprintf(stderr,
                  "block record object differs: text=%u fingerprint=%08x "
                  "symbols=%u relocations=%u function=%u/%u packs=%u/%u "
                  "rodata=%u/%u/%u binding=%u/%u/%u indices=%u/%u/%u "
                  "text-rel=%u/%u/%u/%d\n",
                  text == (const ctool_elf32_section_t *)0
                      ? 0u
                      : text->contents.size,
                  fingerprint, object->symbol_count,
                  object->relocation_count,
                  static_function == (const ctool_elf32_symbol_t *)0
                      ? 0u
                      : static_function->value,
                  static_function == (const ctool_elf32_symbol_t *)0
                      ? 0u
                      : static_function->size,
                  packs == (const ctool_elf32_symbol_t *)0 ? 0u
                                                           : packs->value,
                  packs == (const ctool_elf32_symbol_t *)0 ? 0u
                                                           : packs->size,
                  rodata == (const ctool_elf32_section_t *)0
                      ? 0u
                      : rodata->contents.size,
                  rodata == (const ctool_elf32_section_t *)0
                      ? 0u
                      : rodata->alignment,
                  rodata == (const ctool_elf32_section_t *)0
                      ? 0u
                      : rodata->relocation_count,
                  function == (const ctool_elf32_symbol_t *)0
                      ? 0u
                      : function->binding,
                  static_function == (const ctool_elf32_symbol_t *)0
                      ? 0u
                      : static_function->binding,
                  packs == (const ctool_elf32_symbol_t *)0 ? 0u
                                                           : packs->binding,
                  function == (const ctool_elf32_symbol_t *)0
                      ? 0u
                      : function->file_index,
                  static_function == (const ctool_elf32_symbol_t *)0
                      ? 0u
                      : static_function->file_index,
                  packs == (const ctool_elf32_symbol_t *)0
                      ? 0u
                      : packs->file_index,
                  packs_reference == (const ctool_elf32_relocation_t *)0
                      ? 0u
                      : packs_reference->offset,
                  packs_reference == (const ctool_elf32_relocation_t *)0
                      ? 0u
                      : packs_reference->type,
                  packs_reference == (const ctool_elf32_relocation_t *)0
                      ? 0u
                      : packs_reference->symbol_file_index,
                  packs_reference == (const ctool_elf32_relocation_t *)0
                      ? 0
                      : packs_reference->addend);
    return 0;
  }
  return 1;
}

static int run_block_record_object(const char *host_root) {
  static const char source[] =
      "typedef unsigned int ctool_u32;\n"
      "ctool_u32 block_records(void) {\n"
      "  struct Value;\n"
      "  struct Value { ctool_u32 member; };\n"
      "  struct Value value = { 7u };\n"
      "  return value.member;\n"
      "}\n"
      "ctool_u32 block_record_static(void) {\n"
      "  static const struct { char *name; ctool_u32 mission; } packs[] = {\n"
      "    { \"doom2\", 1u },\n"
      "    { \"tnt\", 2u },\n"
      "    { \"plutonia\", 3u },\n"
      "  };\n"
      "  return packs[1].mission;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t first_bytes;
  ctool_bytes_t second_bytes;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));

  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/block-record-object.c", source, &unit) ||
      unit.tag_count != 0u || unit.block_binding_count != 2u ||
      unit.function_definition_count != 2u) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "block record object buffers") ||
      !expect_object_success_preserves_unit(
          job, &unit, first, "first block record object") ||
      !expect_object_success_preserves_unit(
          job, &unit, second, "repeat block record object")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  first_bytes = ctool_buffer_view(first);
  second_bytes = ctool_buffer_view(second);
  if (first_bytes.size != second_bytes.size ||
      memcmp(first_bytes.data, second_bytes.data, first_bytes.size) != 0) {
    (void)fprintf(stderr, "block record object is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/block-record-object.o");
  object_source.contents = second_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read block record object") ||
      !validate_block_record_object(&object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("block-records: ok");
    return 0;
  }
  return 1;
}

static int run_variadic_callee_object(const char *host_root) {
  static const char source[] =
      "typedef __builtin_va_list va_list;\n"
      "int sum_after(int prefix, int marker, ...) {\n"
      "  va_list ap;\n"
      "  va_list copy;\n"
      "  int *first;\n"
      "  unsigned long second;\n"
      "  unsigned long original_second;\n"
      "  __builtin_va_start(ap, marker);\n"
      "  first = __builtin_va_arg(ap, int *);\n"
      "  __builtin_va_copy(copy, ap);\n"
      "  second = __builtin_va_arg(copy, unsigned long);\n"
      "  original_second = __builtin_va_arg(ap, unsigned long);\n"
      "  __builtin_va_end(copy);\n"
      "  __builtin_va_end(ap);\n"
      "  return *first + second + original_second;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t first_bytes;
  ctool_bytes_t second_bytes;
  ctool_u8 *expected_object = NULL;
  ctool_u32 diagnostic_count;
  ctool_arena_mark_t mark;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source_mode(job, "/variadic-object.c", source, CTOOL_TRUE,
                         &unit) ||
      unit.function_definition_count != 1u ||
      !take_unit_snapshot(&unit, &snapshot)) {
    (void)fprintf(stderr, "variadic callee object setup failed\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (!check_status(status, CTOOL_OK, "variadic callee object buffers")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, first);
  first_bytes = ctool_buffer_view(first);
  if (!check_status(status, CTOOL_OK, "first variadic callee object") ||
      first_bytes.size == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  expected_object = (ctool_u8 *)malloc((size_t)first_bytes.size);
  if (expected_object == NULL) {
    goto cleanup;
  }
  (void)memcpy(expected_object, first_bytes.data,
               (size_t)first_bytes.size);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_emit_object(job, &unit, second);
  second_bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "repeat variadic callee object") ||
      second_bytes.size != first_bytes.size ||
      memcmp(second_bytes.data, expected_object,
             (size_t)second_bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      unit_snapshot_matches(&snapshot, &unit) == 0) {
    (void)fprintf(stderr,
                  "variadic callee object emission is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/variadic-object.o");
  object_source.contents = second_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read variadic callee object") ||
      !validate_variadic_callee_object(job, &object)) {
    (void)fprintf(stderr, "variadic callee object differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(expected_object);
  dispose_unit_snapshot(&snapshot);
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("variadic-callees: ok");
    return 0;
  }
  return 1;
}

static int run_block_extern_object(const char *host_root) {
  static const char source[] =
      "unsigned int read_external_clock(void) {\n"
      "  extern unsigned int external_clock;\n"
      "  return external_clock;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  unit_snapshot_t snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t bytes;
  ctool_u8 *first_object = NULL;
  ctool_u32 first_object_size = 0u;
  ctool_u32 linked_index = CTOOL_C_AST_NONE;
  ctool_u32 index;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&snapshot, 0, sizeof(snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/block-extern-object.c", source, &unit) ||
      unit.block_binding_count != 1u ||
      unit.block_bindings[0].storage != CTOOL_C_STORAGE_EXTERN ||
      unit.block_bindings[0].initializer != CTOOL_C_AST_NONE ||
      unit.block_bindings[0].linkage_binding >= unit.binding_count ||
      !take_unit_snapshot(&unit, &snapshot)) {
    goto cleanup;
  }
  linked_index = unit.block_bindings[0].linkage_binding;
  if (unit.bindings[linked_index].kind != CTOOL_C_BINDING_OBJECT ||
      unit.bindings[linked_index].linkage != CTOOL_C_LINKAGE_EXTERNAL ||
      unit.bindings[linked_index].file_scope_visible != CTOOL_FALSE) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "block extern object buffer") ||
      !expect_object_success_preserves_unit(
          job, &unit, output, "block extern object emission")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  bytes = ctool_buffer_view(output);
  first_object_size = bytes.size;
  first_object = (ctool_u8 *)malloc((size_t)first_object_size);
  if (first_object == NULL) {
    goto cleanup;
  }
  (void)memcpy(first_object, bytes.data, (size_t)first_object_size);
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !expect_object_success_preserves_unit(
          job, &unit, output, "repeat block extern object emission")) {
    goto cleanup;
  }
  bytes = ctool_buffer_view(output);
  if (bytes.size != first_object_size ||
      memcmp(bytes.data, first_object, (size_t)bytes.size) != 0) {
    (void)fprintf(stderr, "block extern object is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/block-extern-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read block extern object") ||
      object.symbol_count != 3u ||
      !validate_external_object_load(&object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < object.symbol_count; index++) {
    const ctool_elf32_symbol_t *symbol = &object.symbols[index];
    if (symbol->type == CTOOL_ELF32_SYMBOL_OBJECT &&
        string_equal(symbol->name, "external_clock") == 0) {
      (void)fprintf(stderr, "block extern created a second object symbol\n");
      goto cleanup;
    }
  }
  passed = 1;

cleanup:
  free(first_object);
  dispose_unit_snapshot(&snapshot);
  if (output != (ctool_buffer_t *)0) {
    ctool_buffer_close(output);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("block-externs: ok");
    return 0;
  }
  return 1;
}

static int validate_block_function_object(
    const ctool_elf32_object_t *object) {
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *invoke =
      find_symbol(object, "invoke_external");
  const ctool_elf32_symbol_t *invoke_old =
      find_symbol(object, "invoke_old_style");
  const ctool_elf32_symbol_t *address =
      find_symbol(object, "address_external");
  const ctool_elf32_symbol_t *helper =
      find_symbol(object, "external_helper");
  ctool_u32 helper_symbols = 0u;
  ctool_u32 direct_calls = 0u;
  ctool_u32 function_addresses = 0u;
  ctool_u32 index;

  if (text == (const ctool_elf32_section_t *)0 ||
      rel_text == (const ctool_elf32_section_t *)0 ||
      invoke == (const ctool_elf32_symbol_t *)0 ||
      invoke_old == (const ctool_elf32_symbol_t *)0 ||
      address == (const ctool_elf32_symbol_t *)0 ||
      helper == (const ctool_elf32_symbol_t *)0 ||
      text->contents.size == 0u || text->relocation_first != 0u ||
      text->relocation_count != 3u || object->symbol_count != 5u ||
      object->relocation_count != 3u || object->relocations == NULL ||
      !symbol_matches(invoke_old, invoke_old->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      invoke_old->size) ||
      invoke_old->size == 0u ||
      !symbol_matches(invoke, invoke->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      invoke_old->size,
                      invoke->size) ||
      invoke->size == 0u ||
      !symbol_matches(address, address->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                      invoke_old->size + invoke->size, address->size) ||
      address->size == 0u ||
      invoke_old->size > text->contents.size ||
      invoke->size > text->contents.size - invoke_old->size ||
      address->size !=
          text->contents.size - invoke_old->size - invoke->size ||
      !symbol_matches(helper, helper->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_UNDEFINED, CTOOL_ELF32_NO_SECTION,
                      0u, 0u)) {
    (void)fprintf(stderr, "block function ELF inventory differs\n");
    return 0;
  }
  for (index = 0u; index < object->relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[index];
    if (relocation->relocation_section_file_index != rel_text->file_index ||
        relocation->entry_index != index ||
        relocation->target_section_file_index != text->file_index ||
        relocation->offset > text->contents.size ||
        text->contents.size - relocation->offset < 4u ||
        relocation->symbol_file_index != helper->file_index ||
        relocation->addend_known != CTOOL_TRUE) {
      (void)fprintf(stderr,
                    "block function relocation metadata differs\n");
      return 0;
    }
    if (relocation->type == CTOOL_ELF32_R_386_PC32 &&
        relocation->addend == -4) {
      direct_calls++;
    } else if (relocation->type == CTOOL_ELF32_R_386_32 &&
               relocation->addend == 0) {
      function_addresses++;
    } else {
      (void)fprintf(stderr,
                    "block function relocation kind differs: %u/%d\n",
                    relocation->type, relocation->addend);
      return 0;
    }
  }
  for (index = 0u; index < object->symbol_count; index++) {
    const ctool_elf32_symbol_t *symbol = &object->symbols[index];
    if (symbol->type == CTOOL_ELF32_SYMBOL_FUNCTION &&
        string_equal(symbol->name, "external_helper") != 0) {
      helper_symbols++;
    }
  }
  if (helper_symbols != 1u || direct_calls != 2u ||
      function_addresses != 1u) {
    (void)fprintf(stderr,
                  "block function symbol use differs: %u/%u/%u\n",
                  helper_symbols, direct_calls, function_addresses);
    return 0;
  }
  return 1;
}

static int run_block_function_object(const char *host_root) {
  static const char block_source[] =
      "int invoke_old_style(void) {\n"
      "  int external_helper();\n"
      "  return external_helper();\n"
      "}\n"
      "int invoke_external(int value);\n"
      "int invoke_external(int value) {\n"
      "  int external_helper(int value);\n"
      "  return external_helper(value);\n"
      "}\n"
      "int (*address_external(void))() {\n"
      "  int external_helper();\n"
      "  return external_helper;\n"
      "}\n";
  static const char file_source[] =
      "int invoke_old_style(void);\n"
      "int external_helper();\n"
      "int invoke_old_style(void) {\n"
      "  return external_helper();\n"
      "}\n"
      "int invoke_external(int value);\n"
      "int external_helper(int value);\n"
      "int invoke_external(int value) {\n"
      "  return external_helper(value);\n"
      "}\n"
      "int (*address_external(void))() {\n"
      "  return external_helper;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *block_output = (ctool_buffer_t *)0;
  ctool_buffer_t *file_output = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t block_unit;
  ctool_c_translation_unit_t file_unit;
  unit_snapshot_t block_snapshot;
  unit_snapshot_t file_snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t block_bytes;
  ctool_bytes_t file_bytes;
  ctool_u8 *first_object = NULL;
  ctool_u32 first_object_size = 0u;
  ctool_u32 linked_index;
  ctool_status_t status;
  int passed = 0;

  (void)memset(&block_unit, 0, sizeof(block_unit));
  (void)memset(&file_unit, 0, sizeof(file_unit));
  (void)memset(&block_snapshot, 0, sizeof(block_snapshot));
  (void)memset(&file_snapshot, 0, sizeof(file_snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/block-function-object.c", block_source,
                    &block_unit) ||
      !parse_source(job, "/block-function-object.c", file_source,
                    &file_unit) ||
      block_unit.block_binding_count != 3u ||
      file_unit.block_binding_count != 0u ||
      block_unit.block_bindings[0].kind != CTOOL_C_BINDING_FUNCTION ||
      block_unit.block_bindings[0].storage != CTOOL_C_STORAGE_NONE ||
      block_unit.block_bindings[0].initializer != CTOOL_C_AST_NONE ||
      block_unit.block_bindings[0].linkage_binding >=
          block_unit.binding_count ||
      block_unit.block_bindings[1].kind != CTOOL_C_BINDING_FUNCTION ||
      block_unit.block_bindings[1].storage != CTOOL_C_STORAGE_NONE ||
      block_unit.block_bindings[1].initializer != CTOOL_C_AST_NONE ||
      block_unit.block_bindings[1].linkage_binding !=
          block_unit.block_bindings[0].linkage_binding ||
      block_unit.block_bindings[2].kind != CTOOL_C_BINDING_FUNCTION ||
      block_unit.block_bindings[2].storage != CTOOL_C_STORAGE_NONE ||
      block_unit.block_bindings[2].initializer != CTOOL_C_AST_NONE ||
      block_unit.block_bindings[2].linkage_binding !=
          block_unit.block_bindings[0].linkage_binding ||
      block_unit.block_bindings[0].type >= block_unit.graph.type_count ||
      block_unit.block_bindings[1].type >= block_unit.graph.type_count ||
      block_unit.block_bindings[2].type >= block_unit.graph.type_count ||
      block_unit.graph.types[block_unit.block_bindings[0].type].kind !=
          CTOOL_C_TYPE_FUNCTION ||
      block_unit.graph.types[block_unit.block_bindings[1].type].kind !=
          CTOOL_C_TYPE_FUNCTION ||
      block_unit.graph.types[block_unit.block_bindings[2].type].kind !=
          CTOOL_C_TYPE_FUNCTION ||
      block_unit.graph.types[block_unit.block_bindings[0].type]
              .has_prototype != CTOOL_FALSE ||
      block_unit.graph.types[block_unit.block_bindings[1].type]
              .has_prototype != CTOOL_TRUE ||
      block_unit.graph.types[block_unit.block_bindings[2].type]
              .has_prototype != CTOOL_FALSE ||
      !take_unit_snapshot(&block_unit, &block_snapshot) ||
      !take_unit_snapshot(&file_unit, &file_snapshot)) {
    goto cleanup;
  }
  linked_index = block_unit.block_bindings[0].linkage_binding;
  if (block_unit.bindings[linked_index].kind != CTOOL_C_BINDING_FUNCTION ||
      block_unit.bindings[linked_index].linkage !=
          CTOOL_C_LINKAGE_EXTERNAL ||
      block_unit.bindings[linked_index].file_scope_visible != CTOOL_FALSE) {
    (void)fprintf(stderr, "block function linkage metadata differs\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &block_output);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                   &file_output);
  }
  if (!check_status(status, CTOOL_OK, "block function object buffers") ||
      !expect_object_success_preserves_unit(
          job, &block_unit, block_output,
          "block function object emission")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  block_bytes = ctool_buffer_view(block_output);
  first_object_size = block_bytes.size;
  first_object = (ctool_u8 *)malloc((size_t)first_object_size);
  if (first_object == NULL) {
    goto cleanup;
  }
  (void)memcpy(first_object, block_bytes.data, (size_t)first_object_size);
  if (ctool_buffer_rewind(block_output, 0u) != CTOOL_OK ||
      !expect_object_success_preserves_unit(
          job, &block_unit, block_output,
          "repeat block function object emission") ||
      !expect_object_success_preserves_unit(
          job, &file_unit, file_output,
          "file prototype object emission")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  block_bytes = ctool_buffer_view(block_output);
  file_bytes = ctool_buffer_view(file_output);
  if (block_bytes.size == 0u || block_bytes.size != first_object_size ||
      memcmp(block_bytes.data, first_object,
             (size_t)block_bytes.size) != 0 ||
      block_bytes.size != file_bytes.size ||
      memcmp(block_bytes.data, file_bytes.data,
             (size_t)block_bytes.size) != 0) {
    (void)fprintf(stderr,
                  "block function changed the emitted object\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/block-function-object.o");
  object_source.contents = block_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read block function object") ||
      !validate_block_function_object(&object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(first_object);
  dispose_unit_snapshot(&file_snapshot);
  dispose_unit_snapshot(&block_snapshot);
  if (file_output != (ctool_buffer_t *)0) {
    ctool_buffer_close(file_output);
  }
  if (block_output != (ctool_buffer_t *)0) {
    ctool_buffer_close(block_output);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("block-functions: ok");
    return 0;
  }
  return 1;
}

static int run_block_typedef_object(const char *host_root) {
  static const char typedef_source[] =
      "unsigned int block_typedef(unsigned int input) {\n"
      "  typedef unsigned char byte_t;\n"
      "  byte_t value = (byte_t)input;\n"
      "  return value;\n"
      "}\n";
  static const char direct_source[] =
      "unsigned int block_typedef(unsigned int input) {\n"
      "  unsigned char value = (unsigned char)input;\n"
      "  return value;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *typedef_output = (ctool_buffer_t *)0;
  ctool_buffer_t *direct_output = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t typedef_unit;
  ctool_c_translation_unit_t direct_unit;
  unit_snapshot_t typedef_snapshot;
  unit_snapshot_t direct_snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t typedef_bytes;
  ctool_bytes_t direct_bytes;
  const ctool_elf32_section_t *text;
  const ctool_elf32_symbol_t *function;
  ctool_u32 typedef_index;
  ctool_u32 value_index;
  ctool_status_t status;
  int passed = 0;

  (void)memset(&typedef_unit, 0, sizeof(typedef_unit));
  (void)memset(&direct_unit, 0, sizeof(direct_unit));
  (void)memset(&typedef_snapshot, 0, sizeof(typedef_snapshot));
  (void)memset(&direct_snapshot, 0, sizeof(direct_snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/block-typedef-object.c", typedef_source,
                    &typedef_unit) ||
      !parse_source(job, "/block-typedef-object.c", direct_source,
                    &direct_unit) ||
      !take_unit_snapshot(&typedef_unit, &typedef_snapshot) ||
      !take_unit_snapshot(&direct_unit, &direct_snapshot)) {
    goto cleanup;
  }
  if (typedef_unit.block_binding_count != 2u ||
      direct_unit.block_binding_count != 1u) {
    (void)fprintf(stderr, "block typedef object bindings differ\n");
    goto cleanup;
  }
  typedef_index = 0u;
  value_index = 1u;
  if (typedef_unit.block_bindings[typedef_index].kind !=
          CTOOL_C_BINDING_TYPEDEF ||
      typedef_unit.block_bindings[typedef_index].storage !=
          CTOOL_C_STORAGE_TYPEDEF ||
      typedef_unit.block_bindings[typedef_index].initializer !=
          CTOOL_C_AST_NONE ||
      typedef_unit.block_bindings[typedef_index].linkage_binding !=
          CTOOL_C_AST_NONE ||
      typedef_unit.block_bindings[value_index].kind !=
          CTOOL_C_BINDING_OBJECT ||
      typedef_unit.block_bindings[value_index].type !=
          typedef_unit.block_bindings[typedef_index].type) {
    (void)fprintf(stderr, "block typedef object metadata differs\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &typedef_output);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                   &direct_output);
  }
  if (!check_status(status, CTOOL_OK, "block typedef object buffers") ||
      !expect_object_success_preserves_unit(
          job, &typedef_unit, typedef_output,
          "block typedef object emission") ||
      !expect_object_success_preserves_unit(
          job, &direct_unit, direct_output,
          "direct spelling object emission")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  typedef_bytes = ctool_buffer_view(typedef_output);
  direct_bytes = ctool_buffer_view(direct_output);
  if (typedef_bytes.size == 0u ||
      typedef_bytes.size != direct_bytes.size ||
      memcmp(typedef_bytes.data, direct_bytes.data,
             (size_t)typedef_bytes.size) != 0) {
    (void)fprintf(stderr,
                  "block typedef changed the emitted object\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/block-typedef-object.o");
  object_source.contents = typedef_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read block typedef object")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  text = find_section(&object, ".text");
  function = find_symbol(&object, "block_typedef");
  if (text == (const ctool_elf32_section_t *)0 ||
      function == (const ctool_elf32_symbol_t *)0 ||
      text->contents.size == 0u || object.relocation_count != 0u ||
      !symbol_matches(function, function->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      text->contents.size) ||
      find_symbol(&object, "byte_t") !=
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "value") !=
          (const ctool_elf32_symbol_t *)0) {
    (void)fprintf(stderr, "block typedef ELF inventory differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  dispose_unit_snapshot(&direct_snapshot);
  dispose_unit_snapshot(&typedef_snapshot);
  if (direct_output != (ctool_buffer_t *)0) {
    ctool_buffer_close(direct_output);
  }
  if (typedef_output != (ctool_buffer_t *)0) {
    ctool_buffer_close(typedef_output);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("block-typedefs: ok");
    return 0;
  }
  return 1;
}

static int run_block_enum_object(const char *host_root) {
  static const char enum_source[] =
      "int cursor_constants(void) {\n"
      "  enum { CURSOR_W = 8, CURSOR_H = 10, CURSOR_PAD = 1 };\n"
      "  return CURSOR_W + CURSOR_H + CURSOR_PAD;\n"
      "}\n"
      "int repl_constants(void) {\n"
      "  enum { CC_REPL_LINE_MAX = 512,\n"
      "         CC_REPL_SRC_MAX = 64 * 1024 };\n"
      "  return CC_REPL_LINE_MAX + CC_REPL_SRC_MAX;\n"
      "}\n"
      "int shadow_constant(int input) {\n"
      "  enum { VALUE = 3 };\n"
      "  {\n"
      "    enum { VALUE = 5 };\n"
      "    input += VALUE;\n"
      "  }\n"
      "  return input + VALUE;\n"
      "}\n"
      "int member_constants(void) {\n"
      "  struct Holder { enum { MEMBER_FIRST = 19, MEMBER_LAST = 23 } value; };\n"
      "  return MEMBER_FIRST + MEMBER_LAST;\n"
      "}\n"
      "int parameter_constants(\n"
      "    enum { PARAM_FIRST = 13, PARAM_LAST = 17 } value) {\n"
      "  return value + PARAM_FIRST + PARAM_LAST;\n"
      "}\n"
      "int size_constant(void) {\n"
      "  (void)sizeof(enum SizeTag { SIZE_VALUE = 11 });\n"
      "  return SIZE_VALUE;\n"
      "}\n"
      "int cast_constant(int input) {\n"
      "  return (enum CastTag { CAST_VALUE = 7 })input + CAST_VALUE;\n"
      "}\n"
      "int align_constant(void) {\n"
      "  (void)_Alignof(enum AlignTag { ALIGN_VALUE = 13 });\n"
      "  return ALIGN_VALUE;\n"
      "}\n"
      "int literal_constant(void) {\n"
      "  return (enum LiteralTag { LITERAL_VALUE = 17 }){ LITERAL_VALUE };\n"
      "}\n"
      "int offset_constant(void) {\n"
      "  return __builtin_offsetof(\n"
      "      struct Offset { enum { OFFSET_VALUE = 19 } member;\n"
      "                      int tail; }, tail) + OFFSET_VALUE;\n"
      "}\n"
      "int case_constant(int input) {\n"
      "  switch (input) {\n"
      "  case (enum CaseTag { CASE_VALUE = 23 })23:\n"
      "    return CASE_VALUE;\n"
      "  default:\n"
      "    return 0;\n"
      "  }\n"
      "}\n"
      "int iteration_constant(int input) {\n"
      "  for (; input; (enum IterTag { ITER_VALUE = 29 })input) {\n"
      "    return ITER_VALUE;\n"
      "  }\n"
      "  return 0;\n"
      "}\n"
      "int variadic_constant(int marker, ...) {\n"
      "  __builtin_va_list cursor;\n"
      "  __builtin_va_start(cursor, marker);\n"
      "  int value = __builtin_va_arg(\n"
      "      cursor, enum VaTag { VA_VALUE = 31 });\n"
      "  __builtin_va_end(cursor);\n"
      "  return value + VA_VALUE;\n"
      "}\n"
      "int designator_constant(void) {\n"
      "  int values[1] = {\n"
      "      [sizeof(enum DesignatorTag { DESIGNATOR_VALUE = 0 }) - 4] =\n"
      "          DESIGNATOR_VALUE\n"
      "  };\n"
      "  return values[0];\n"
      "}\n"
      "int literal_designator_constant(void) {\n"
      "  return ((int[1]) {\n"
      "      [sizeof(enum LiteralDesignatorTag {\n"
      "          LITERAL_DESIGNATOR_VALUE = 0 }) - 4] =\n"
      "          LITERAL_DESIGNATOR_VALUE\n"
      "  })[0];\n"
      "}\n";
  static const char direct_source[] =
      "int cursor_constants(void) {\n"
      "  return 8 + 10 + 1;\n"
      "}\n"
      "int repl_constants(void) {\n"
      "  return 512 + 65536;\n"
      "}\n"
      "int shadow_constant(int input) {\n"
      "  {\n"
      "    input += 5;\n"
      "  }\n"
      "  return input + 3;\n"
      "}\n"
      "int member_constants(void) {\n"
      "  return 19 + 23;\n"
      "}\n"
      "int parameter_constants(int value) {\n"
      "  return value + 13 + 17;\n"
      "}\n"
      "int size_constant(void) {\n"
      "  (void)sizeof(int);\n"
      "  return 11;\n"
      "}\n"
      "int cast_constant(int input) {\n"
      "  return (int)input + 7;\n"
      "}\n"
      "int align_constant(void) {\n"
      "  (void)_Alignof(int);\n"
      "  return 13;\n"
      "}\n"
      "int literal_constant(void) {\n"
      "  return (int){ 17 };\n"
      "}\n"
      "int offset_constant(void) {\n"
      "  return __builtin_offsetof(\n"
      "      struct Offset { int member; int tail; }, tail) + 19;\n"
      "}\n"
      "int case_constant(int input) {\n"
      "  switch (input) {\n"
      "  case (int)23:\n"
      "    return 23;\n"
      "  default:\n"
      "    return 0;\n"
      "  }\n"
      "}\n"
      "int iteration_constant(int input) {\n"
      "  for (; input; (int)input) {\n"
      "    return 29;\n"
      "  }\n"
      "  return 0;\n"
      "}\n"
      "int variadic_constant(int marker, ...) {\n"
      "  __builtin_va_list cursor;\n"
      "  __builtin_va_start(cursor, marker);\n"
      "  int value = __builtin_va_arg(cursor, int);\n"
      "  __builtin_va_end(cursor);\n"
      "  return value + 31;\n"
      "}\n"
      "int designator_constant(void) {\n"
      "  int values[1] = { [0] = 0 };\n"
      "  return values[0];\n"
      "}\n"
      "int literal_designator_constant(void) {\n"
      "  return ((int[1]) { [0] = 0 })[0];\n"
      "}\n";
  static const char *const enumerator_names[] = {
      "CURSOR_W", "CURSOR_H", "CURSOR_PAD", "CC_REPL_LINE_MAX",
      "CC_REPL_SRC_MAX", "VALUE", "MEMBER_FIRST", "MEMBER_LAST",
      "PARAM_FIRST", "PARAM_LAST", "SIZE_VALUE", "CAST_VALUE",
      "ALIGN_VALUE", "LITERAL_VALUE", "OFFSET_VALUE", "CASE_VALUE",
      "ITER_VALUE", "VA_VALUE", "DESIGNATOR_VALUE",
      "LITERAL_DESIGNATOR_VALUE"};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *enum_output = (ctool_buffer_t *)0;
  ctool_buffer_t *direct_output = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t enum_unit;
  ctool_c_translation_unit_t direct_unit;
  unit_snapshot_t enum_snapshot;
  unit_snapshot_t direct_snapshot;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t enum_bytes;
  ctool_bytes_t direct_bytes;
  const ctool_elf32_section_t *text;
  ctool_u32 enumerator_name_count =
      (ctool_u32)(sizeof(enumerator_names) / sizeof(enumerator_names[0]));
  ctool_u32 expression_event_count = 0u;
  ctool_u32 initializer_event_count = 0u;
  ctool_u32 index;
  ctool_status_t status;
  int passed = 0;

  (void)memset(&enum_unit, 0, sizeof(enum_unit));
  (void)memset(&direct_unit, 0, sizeof(direct_unit));
  (void)memset(&enum_snapshot, 0, sizeof(enum_snapshot));
  (void)memset(&direct_snapshot, 0, sizeof(direct_snapshot));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source_mode(job, "/block-enum-object.c", enum_source,
                         CTOOL_TRUE, &enum_unit) ||
      !parse_source_mode(job, "/block-enum-object.c", direct_source,
                         CTOOL_TRUE, &direct_unit) ||
      enum_unit.block_binding_count != 24u ||
      direct_unit.block_binding_count != 3u ||
      enum_unit.function_definition_count != 15u ||
      enum_unit.function_definitions[3].first_block_binding != 7u ||
      enum_unit.function_definitions[3].block_binding_count != 0u ||
      enum_unit.function_definitions[4].first_block_binding != 9u ||
      enum_unit.function_definitions[4].block_binding_count != 2u ||
      enum_unit.function_definitions[12].first_block_binding != 18u ||
      enum_unit.function_definitions[12].block_binding_count != 0u ||
      enum_unit.function_definitions[13].first_block_binding != 21u ||
      enum_unit.function_definitions[13].block_binding_count != 0u ||
      enum_unit.function_definitions[14].first_block_binding != 23u ||
      enum_unit.function_definitions[14].block_binding_count != 0u ||
      !take_unit_snapshot(&enum_unit, &enum_snapshot) ||
      !take_unit_snapshot(&direct_unit, &direct_snapshot)) {
    goto cleanup;
  }
  for (index = 0u; index < enum_unit.block_binding_count; index++) {
    const ctool_c_block_binding_t *binding =
        &enum_unit.block_bindings[index];
    if (binding->kind == CTOOL_C_BINDING_ENUMERATOR) {
      if (binding->storage != CTOOL_C_STORAGE_NONE ||
          binding->linkage_binding != CTOOL_C_AST_NONE ||
          binding->initializer != CTOOL_C_AST_NONE) {
        (void)fprintf(stderr, "block enum object metadata differs\n");
        goto cleanup;
      }
      if (index >= 11u) {
        const ctool_c_expression_t *expression;
        ctool_u32 expected_child_offset = index == 20u ? 1u : 0u;
        if (index == 22u || index == 23u) {
          const ctool_c_initializer_t *initializer;
          if (binding->activation_expression != CTOOL_C_AST_NONE ||
              binding->activation_initializer >=
                  enum_unit.initializer_count) {
            (void)fprintf(stderr,
                          "block enum object initializer event differs\n");
            goto cleanup;
          }
          initializer =
              &enum_unit.initializers[binding->activation_initializer];
          if (initializer->first_block_binding != index ||
              initializer->block_binding_count != 1u) {
            (void)fprintf(
                stderr,
                "block enum object initializer ownership differs\n");
            goto cleanup;
          }
          initializer_event_count++;
          continue;
        }
        if (binding->activation_expression >= enum_unit.expression_count ||
            binding->activation_initializer != CTOOL_C_AST_NONE) {
          (void)fprintf(stderr,
                        "block enum object event index differs\n");
          goto cleanup;
        }
        expression = &enum_unit.expressions[binding->activation_expression];
        if (expression->first_block_binding != index ||
            expression->block_binding_count != 1u ||
            expression->block_binding_child_offset !=
                expected_child_offset) {
          (void)fprintf(stderr,
                        "block enum object event ownership differs\n");
          goto cleanup;
        }
        expression_event_count++;
      } else if (binding->activation_expression != CTOOL_C_AST_NONE ||
                 binding->activation_initializer != CTOOL_C_AST_NONE) {
        (void)fprintf(stderr,
                      "block enum declaration event metadata differs\n");
        goto cleanup;
      }
    } else if (binding->kind != CTOOL_C_BINDING_OBJECT ||
               (index != 18u && index != 19u && index != 21u) ||
               binding->activation_expression != CTOOL_C_AST_NONE ||
               binding->activation_initializer != CTOOL_C_AST_NONE) {
      (void)fprintf(stderr, "block enum object metadata differs\n");
      goto cleanup;
    }
  }
  if (expression_event_count != 8u || initializer_event_count != 2u) {
    (void)fprintf(stderr, "block enum object event count differs\n");
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &enum_output);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                   &direct_output);
  }
  if (!check_status(status, CTOOL_OK, "block enum object buffers") ||
      !expect_object_success_preserves_unit(
          job, &enum_unit, enum_output, "block enum object emission") ||
      !expect_object_success_preserves_unit(
          job, &direct_unit, direct_output, "literal object emission")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  enum_bytes = ctool_buffer_view(enum_output);
  direct_bytes = ctool_buffer_view(direct_output);
  if (enum_bytes.size == 0u || enum_bytes.size != direct_bytes.size ||
      memcmp(enum_bytes.data, direct_bytes.data,
             (size_t)enum_bytes.size) != 0) {
    ctool_u32 mismatch = 0u;
    ctool_u32 common = enum_bytes.size < direct_bytes.size
                           ? enum_bytes.size
                           : direct_bytes.size;
    while (mismatch < common &&
           enum_bytes.data[mismatch] == direct_bytes.data[mismatch]) {
      mismatch++;
    }
    (void)fprintf(stderr,
                  "block enums changed the emitted object at %u (%u/%u)\n",
                  mismatch, enum_bytes.size, direct_bytes.size);
    goto cleanup;
  }
  object_source.path.text = ctool_string("/block-enum-object.o");
  object_source.contents = enum_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  text = status == CTOOL_OK ? find_section(&object, ".text")
                            : (const ctool_elf32_section_t *)0;
  if (!check_status(status, CTOOL_OK, "read block enum object") ||
      text == (const ctool_elf32_section_t *)0 ||
      text->contents.size == 0u || object.relocation_count != 0u ||
      find_symbol(&object, "cursor_constants") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "repl_constants") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "shadow_constant") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "member_constants") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "parameter_constants") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "size_constant") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "cast_constant") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "align_constant") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "literal_constant") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "offset_constant") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "case_constant") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "iteration_constant") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "variadic_constant") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "designator_constant") ==
          (const ctool_elf32_symbol_t *)0 ||
      find_symbol(&object, "literal_designator_constant") ==
          (const ctool_elf32_symbol_t *)0) {
    (void)fprintf(stderr, "block enum ELF inventory differs\n");
    goto cleanup;
  }
  for (index = 0u; index < enumerator_name_count; index++) {
    if (find_symbol(&object, enumerator_names[index]) !=
        (const ctool_elf32_symbol_t *)0) {
      (void)fprintf(stderr, "block enum created an ELF symbol\n");
      goto cleanup;
    }
  }
  passed = 1;

cleanup:
  dispose_unit_snapshot(&direct_snapshot);
  dispose_unit_snapshot(&enum_snapshot);
  if (direct_output != (ctool_buffer_t *)0) {
    ctool_buffer_close(direct_output);
  }
  if (enum_output != (ctool_buffer_t *)0) {
    ctool_buffer_close(enum_output);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("block-enums: ok");
    return 0;
  }
  return 1;
}

static int bit_field_store_oracle_execute(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *symbol, ctool_u32 initial_storage,
    ctool_u32 value, ctool_u32 *result, ctool_u32 *stored_value) {
  narrow_oracle_machine_t machine;
  const ctool_u32 object_address = 32u;
  ctool_u32 cursor = 0u;
  ctool_u32 observed;
  ctool_bool returned = CTOOL_FALSE;
  if (job == NULL || text == NULL || symbol == NULL || result == NULL ||
      stored_value == NULL || symbol->value > text->contents.size ||
      symbol->size > text->contents.size - symbol->value) {
    return 0;
  }
  (void)memset(&machine, 0xcd, sizeof(machine));
  machine.registers[NARROW_ORACLE_ESP] = NARROW_ORACLE_INITIAL_ESP;
  machine.registers[NARROW_ORACLE_EBP] = 64u;
  if (!narrow_oracle_write_memory(&machine, object_address, 32u,
                                  initial_storage) ||
      !narrow_oracle_write_memory(&machine, NARROW_ORACLE_INITIAL_ESP, 32u,
                                  0x13579bdfu) ||
      !narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 4u, 32u, object_address) ||
      !narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 8u, 32u, value)) {
    return 0;
  }
  while (cursor < symbol->size && returned == CTOOL_FALSE) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + symbol->value + cursor,
        symbol->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u ||
        !narrow_oracle_step(&machine, &decoded.instruction, &returned)) {
      (void)fprintf(
          stderr, "bit-field store oracle stopped at %u on %s\n",
          (unsigned)cursor,
          status == CTOOL_OK && decoded.kind == CTOOL_X86_DECODE_KNOWN
              ? ctool_x86_mnemonic_name(decoded.instruction.mnemonic).data
              : "invalid instruction");
      return 0;
    }
    cursor += decoded.consumed;
  }
  if (returned == CTOOL_FALSE || cursor != symbol->size ||
      machine.registers[NARROW_ORACLE_ESP] != NARROW_ORACLE_INITIAL_ESP ||
      machine.registers[NARROW_ORACLE_EBP] != 64u ||
      !narrow_oracle_read_memory(&machine, object_address, 32u,
                                 stored_value) ||
      !narrow_oracle_read_memory(&machine, object_address - 4u, 32u,
                                 &observed) ||
      observed != 0xcdcdcdcdu ||
      !narrow_oracle_read_memory(&machine, object_address + 4u, 32u,
                                 &observed) ||
      observed != 0xcdcdcdcdu ||
      !narrow_oracle_read_memory(&machine, NARROW_ORACLE_INITIAL_ESP, 32u,
                                 &observed) ||
      observed != 0x13579bdfu ||
      !narrow_oracle_read_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 4u, 32u, &observed) ||
      observed != object_address ||
      !narrow_oracle_read_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 8u, 32u, &observed) ||
      observed != value) {
    return 0;
  }
  *result = machine.registers[NARROW_ORACLE_EAX];
  return 1;
}

static int validate_bit_field_store_function(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *symbol, ctool_u32 expected_and,
    ctool_u32 expected_or, ctool_u32 expected_shl,
    ctool_u32 expected_sar, ctool_u32 expected_storage_loads) {
  ctool_u32 cursor = 0u;
  ctool_u32 and_count = 0u;
  ctool_u32 or_count = 0u;
  ctool_u32 shl_count = 0u;
  ctool_u32 sar_count = 0u;
  ctool_u32 storage_loads = 0u;
  ctool_u32 storage_stores = 0u;
  ctool_u32 returns = 0u;
  if (job == NULL || text == NULL || symbol == NULL || symbol->size == 0u ||
      symbol->value > text->contents.size ||
      symbol->size > text->contents.size - symbol->value) {
    return 0;
  }
  while (cursor < symbol->size) {
    ctool_x86_decoded_t decoded;
    const ctool_x86_instruction_t *instruction;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + symbol->value + cursor,
        symbol->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "bit-field store decode failed at %u\n",
                    (unsigned)cursor);
      return 0;
    }
    instruction = &decoded.instruction;
    if (instruction->mnemonic == CTOOL_X86_MN_AND) {
      and_count++;
    } else if (instruction->mnemonic == CTOOL_X86_MN_OR) {
      or_count++;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SHL) {
      shl_count++;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SAR) {
      sar_count++;
    } else if (instruction->mnemonic == CTOOL_X86_MN_RET) {
      returns++;
    }
    if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
        instruction->operand_count == 2u &&
        instruction->operands[0].kind == CTOOL_X86_OPERAND_REGISTER &&
        instruction->operands[0].as.reg.class_id == CTOOL_X86_REG_GPR32 &&
        instruction->operands[0].as.reg.index == NARROW_ORACLE_EAX &&
        instruction->operands[1].kind == CTOOL_X86_OPERAND_MEMORY &&
        instruction->operands[1].width_bits == 32u &&
        instruction->operands[1].as.memory.base.class_id ==
            CTOOL_X86_REG_GPR32 &&
        instruction->operands[1].as.memory.base.index ==
            NARROW_ORACLE_EAX) {
      storage_loads++;
    }
    if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
        instruction->operand_count == 2u &&
        instruction->operands[0].kind == CTOOL_X86_OPERAND_MEMORY &&
        instruction->operands[0].width_bits == 32u &&
        instruction->operands[1].kind == CTOOL_X86_OPERAND_REGISTER &&
        instruction->operands[1].as.reg.class_id ==
            CTOOL_X86_REG_GPR32) {
      storage_stores++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != symbol->size || and_count != expected_and ||
      or_count != expected_or || shl_count != expected_shl ||
      sar_count != expected_sar ||
      storage_loads != expected_storage_loads || storage_stores != 1u ||
      returns != 1u) {
    (void)fprintf(
        stderr,
        "bit-field store instruction inventory differs: "
        "and=%u or=%u shl=%u sar=%u loads=%u stores=%u returns=%u\n",
        (unsigned)and_count, (unsigned)or_count, (unsigned)shl_count,
        (unsigned)sar_count, (unsigned)storage_loads,
        (unsigned)storage_stores, (unsigned)returns);
    return 0;
  }
  return 1;
}

static int validate_bit_field_store_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  typedef struct {
    const char *name;
    ctool_u32 and_count;
    ctool_u32 or_count;
    ctool_u32 shl_count;
    ctool_u32 sar_count;
    ctool_u32 storage_loads;
  } bit_field_function_t;
  typedef struct {
    const char *name;
    ctool_u32 initial_storage;
    ctool_u32 value;
    ctool_u32 expected_result;
    ctool_u32 expected_storage;
  } bit_field_case_t;
  static const bit_field_function_t functions[] = {
      {"write_red", 2u, 1u, 1u, 0u, 3u},
      {"write_delta", 2u, 1u, 2u, 1u, 3u},
      {"write_whole", 0u, 0u, 0u, 0u, 2u},
      {"write_indexed_red", 2u, 1u, 1u, 0u, 3u}};
  static const bit_field_case_t cases[] = {
      {"write_red", 0xa5b6c7d8u, 0x123u, 0x23u, 0xa523c7d8u},
      {"write_red", 0xa5b6c7d8u, 0xffffffffu, 0xffu, 0xa5ffc7d8u},
      {"write_delta", 0xa5b6c7d8u, 0x1fu, 0xffffffffu,
       0xa5b6dfd8u},
      {"write_delta", 0xa5b6c7d8u, 0x12u, 0xfffffff2u,
       0xa5b6d2d8u},
      {"write_delta", 0xa5b6c7d8u, 0x0fu, 0x0fu, 0xa5b6cfd8u},
      {"write_whole", 0xa5b6c7d8u, 0x12345678u, 0x12345678u,
       0x12345678u}};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *bss = find_section(object, ".bss");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *colors = find_symbol(object, "colors");
  ctool_u32 expected_offset = 0u;
  ctool_u32 index;
  if (job == NULL || object == NULL || text == NULL || bss == NULL ||
      rel_text == NULL || colors == NULL ||
      text->contents.data == NULL || text->contents.size == 0u ||
      text->relocation_count != 1u || object->relocation_count != 1u ||
      bss->type != CTOOL_ELF32_SHT_NOBITS || bss->alignment != 4u ||
      bss->size != 1024u || bss->contents.size != 0u ||
      !symbol_matches(colors, colors->file_index, CTOOL_ELF32_BIND_LOCAL,
                      CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, bss->file_index, 0u,
                      1024u) ||
      object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].symbol_file_index != colors->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != 0) {
    (void)fprintf(stderr, "bit-field store ELF inventory differs\n");
    return 0;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(functions) / sizeof(functions[0]));
       index++) {
    const ctool_elf32_symbol_t *symbol =
        find_symbol(object, functions[index].name);
    if (symbol == NULL || symbol->binding != CTOOL_ELF32_BIND_GLOBAL ||
        symbol->type != CTOOL_ELF32_SYMBOL_FUNCTION ||
        symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        symbol->section_file_index != text->file_index ||
        symbol->value != expected_offset ||
        !validate_bit_field_store_function(
            job, text, symbol, functions[index].and_count,
            functions[index].or_count, functions[index].shl_count,
            functions[index].sar_count, functions[index].storage_loads)) {
      (void)fprintf(stderr, "bit-field store function %s differs\n",
                    functions[index].name);
      return 0;
    }
    expected_offset += symbol->size;
  }
  if (expected_offset != text->contents.size) {
    (void)fprintf(stderr, "bit-field store text coverage differs\n");
    return 0;
  }
  for (index = 0u; index < (ctool_u32)(sizeof(cases) / sizeof(cases[0]));
       index++) {
    const ctool_elf32_symbol_t *symbol =
        find_symbol(object, cases[index].name);
    ctool_u32 result = 0u;
    ctool_u32 stored_value = 0u;
    if (symbol == NULL ||
        !bit_field_store_oracle_execute(
            job, text, symbol, cases[index].initial_storage,
            cases[index].value, &result, &stored_value) ||
        result != cases[index].expected_result ||
        stored_value != cases[index].expected_storage) {
      (void)fprintf(
          stderr,
          "bit-field store result %s case %u differs: eax=%08x "
          "storage=%08x\n",
          cases[index].name, (unsigned)index, (unsigned)result,
          (unsigned)stored_value);
      return 0;
    }
  }
  return 1;
}

static int run_bit_field_store_object(const char *host_root) {
  static const char source[] =
      "typedef unsigned int uint32_t;\n"
      "struct color {\n"
      "  uint32_t b : 8;\n"
      "  uint32_t g : 8;\n"
      "  uint32_t r : 8;\n"
      "  uint32_t a : 8;\n"
      "};\n"
      "struct signed_flags {\n"
      "  unsigned int pad : 8;\n"
      "  signed int delta : 5;\n"
      "  unsigned int rest : 19;\n"
      "};\n"
      "struct whole { unsigned int value : 32; };\n"
      "static struct color colors[256];\n"
      "uint32_t write_red(struct color *state, uint32_t value) {\n"
      "  return state->r = value;\n"
      "}\n"
      "int write_delta(struct signed_flags *state, int value) {\n"
      "  return state->delta = value;\n"
      "}\n"
      "uint32_t write_whole(struct whole *state, uint32_t value) {\n"
      "  return state->value = value;\n"
      "}\n"
      "uint32_t write_indexed_red(uint32_t index, uint32_t value) {\n"
      "  return colors[index].r = value;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t bytes;
  ctool_u8 *first_object = NULL;
  ctool_u32 first_object_size = 0u;
  ctool_status_t status;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_contains(
          job, "/kernel/doom/src/i_video.h",
          "load active Doom video header",
          "the active Doom color record changed", "struct color {",
          NULL) ||
      !active_source_contains(
          job, "/kernel/doom/src/i_video.c",
          "load active Doom video source",
          "the active Doom bit-field write changed",
          "colors[i].r = gammatable[usegamma][*palette++];", NULL) ||
      !parse_source(job, "/bit-field-store-object.c", source, &unit)) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "bit-field store object buffer") ||
      !expect_object_success_preserves_unit(
          job, &unit, output, "bit-field store object emission")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  bytes = ctool_buffer_view(output);
  first_object_size = bytes.size;
  first_object = (ctool_u8 *)malloc((size_t)first_object_size);
  if (first_object == NULL) {
    goto cleanup;
  }
  (void)memcpy(first_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !expect_object_success_preserves_unit(
          job, &unit, output, "repeat bit-field store object emission")) {
    goto cleanup;
  }
  bytes = ctool_buffer_view(output);
  if (bytes.size != first_object_size ||
      memcmp(bytes.data, first_object, (size_t)bytes.size) != 0) {
    (void)fprintf(stderr, "bit-field store object is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/bit-field-store-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read bit-field store object") ||
      !validate_bit_field_store_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(first_object);
  if (output != (ctool_buffer_t *)0) {
    ctool_buffer_close(output);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("bit-field-stores: ok");
    return 0;
  }
  return 1;
}

static int validate_bit_field_mutation_function(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *symbol,
    ctool_u32 expected_eax_indirect_loads,
    ctool_u32 expected_memory_stores) {
  ctool_u32 cursor = 0u;
  ctool_u32 eax_indirect_loads = 0u;
  ctool_u32 memory_stores = 0u;
  ctool_u32 returns = 0u;
  if (job == NULL || text == NULL || symbol == NULL || symbol->size == 0u ||
      symbol->value > text->contents.size ||
      symbol->size > text->contents.size - symbol->value) {
    return 0;
  }
  while (cursor < symbol->size) {
    ctool_x86_decoded_t decoded;
    const ctool_x86_instruction_t *instruction;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + symbol->value + cursor,
        symbol->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "bit-field mutation decode failed at %u\n",
                    (unsigned)cursor);
      return 0;
    }
    instruction = &decoded.instruction;
    if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
        instruction->operand_count == 2u &&
        instruction->operands[0].kind == CTOOL_X86_OPERAND_REGISTER &&
        instruction->operands[0].as.reg.class_id == CTOOL_X86_REG_GPR32 &&
        instruction->operands[0].as.reg.index == NARROW_ORACLE_EAX &&
        instruction->operands[1].kind == CTOOL_X86_OPERAND_MEMORY &&
        instruction->operands[1].width_bits == 32u &&
        instruction->operands[1].as.memory.base.class_id ==
            CTOOL_X86_REG_GPR32 &&
        instruction->operands[1].as.memory.base.index ==
            NARROW_ORACLE_EAX) {
      eax_indirect_loads++;
    }
    if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
        instruction->operand_count == 2u &&
        instruction->operands[0].kind == CTOOL_X86_OPERAND_MEMORY &&
        instruction->operands[0].width_bits == 32u &&
        instruction->operands[1].kind == CTOOL_X86_OPERAND_REGISTER &&
        instruction->operands[1].as.reg.class_id ==
            CTOOL_X86_REG_GPR32) {
      memory_stores++;
    }
    if (instruction->mnemonic == CTOOL_X86_MN_RET) {
      returns++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != symbol->size ||
      eax_indirect_loads != expected_eax_indirect_loads ||
      memory_stores != expected_memory_stores || returns != 1u) {
    (void)fprintf(stderr,
                  "bit-field mutation instruction inventory differs: "
                  "loads=%u stores=%u returns=%u\n",
                  (unsigned)eax_indirect_loads, (unsigned)memory_stores,
                  (unsigned)returns);
    return 0;
  }
  return 1;
}

static int validate_bit_field_designator_side_effect(
    ctool_job_t *job, const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *text) {
  narrow_oracle_machine_t machine;
  const ctool_elf32_symbol_t *symbol =
      find_symbol(object, "indexed_postfix_increment_once");
  const ctool_u32 array_address = 32u;
  const ctool_u32 index_address = 48u;
  ctool_u32 cursor = 0u;
  ctool_u32 observed;
  ctool_bool returned = CTOOL_FALSE;
  if (job == NULL || object == NULL || text == NULL || symbol == NULL ||
      symbol->value > text->contents.size ||
      symbol->size > text->contents.size - symbol->value) {
    return 0;
  }
  (void)memset(&machine, 0xcd, sizeof(machine));
  machine.registers[NARROW_ORACLE_ESP] = NARROW_ORACLE_INITIAL_ESP;
  machine.registers[NARROW_ORACLE_EBP] = 64u;
  if (!narrow_oracle_write_memory(&machine, array_address, 32u,
                                  0x11223344u) ||
      !narrow_oracle_write_memory(&machine, array_address + 4u, 32u,
                                  0xa5b6c7ffu) ||
      !narrow_oracle_write_memory(&machine, index_address, 32u, 1u) ||
      !narrow_oracle_write_memory(&machine, NARROW_ORACLE_INITIAL_ESP, 32u,
                                  0x13579bdfu) ||
      !narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 4u, 32u, array_address) ||
      !narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 8u, 32u, index_address)) {
    return 0;
  }
  while (cursor < symbol->size && returned == CTOOL_FALSE) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + symbol->value + cursor,
        symbol->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u ||
        !narrow_oracle_step(&machine, &decoded.instruction, &returned)) {
      (void)fprintf(stderr,
                    "bit-field designator oracle stopped at %u\n",
                    (unsigned)cursor);
      return 0;
    }
    cursor += decoded.consumed;
  }
  if (returned == CTOOL_FALSE || cursor != symbol->size ||
      machine.registers[NARROW_ORACLE_EAX] != 7u ||
      machine.registers[NARROW_ORACLE_ESP] != NARROW_ORACLE_INITIAL_ESP ||
      machine.registers[NARROW_ORACLE_EBP] != 64u ||
      !narrow_oracle_read_memory(&machine, array_address, 32u, &observed) ||
      observed != 0x11223344u ||
      !narrow_oracle_read_memory(&machine, array_address + 4u, 32u,
                                 &observed) ||
      observed != 0xa5b6c78fu ||
      !narrow_oracle_read_memory(&machine, index_address, 32u, &observed) ||
      observed != 2u ||
      !narrow_oracle_read_memory(&machine, array_address - 4u, 32u,
                                 &observed) ||
      observed != 0xcdcdcdcdu ||
      !narrow_oracle_read_memory(&machine, array_address + 8u, 32u,
                                 &observed) ||
      observed != 0xcdcdcdcdu ||
      !narrow_oracle_read_memory(&machine, index_address - 4u, 32u,
                                 &observed) ||
      observed != 0xcdcdcdcdu ||
      !narrow_oracle_read_memory(&machine, index_address + 4u, 32u,
                                 &observed) ||
      observed != 0xcdcdcdcdu ||
      !narrow_oracle_read_memory(&machine, NARROW_ORACLE_INITIAL_ESP, 32u,
                                 &observed) ||
      observed != 0x13579bdfu ||
      !narrow_oracle_read_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 4u, 32u, &observed) ||
      observed != array_address ||
      !narrow_oracle_read_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 8u, 32u, &observed) ||
      observed != index_address) {
    (void)fprintf(stderr, "bit-field designator side effect differs\n");
    return 0;
  }
  return 1;
}

static int validate_bit_field_mutation_results(
    ctool_job_t *job, const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *text) {
  typedef struct {
    const char *name;
    ctool_u32 initial_storage;
    ctool_u32 right;
    ctool_u32 expected_result;
    ctool_u32 expected_storage;
  } bit_field_mutation_case_t;
  static const bit_field_mutation_case_t cases[] = {
      {"prefix_increment", 0xa5b6c7ffu, 0u, 0u, 0xa5b6c78fu},
      {"prefix_decrement", 0xa5b6c78fu, 0u, 7u, 0xa5b6c7ffu},
      {"postfix_increment", 0xa5b6c7ffu, 0u, 7u, 0xa5b6c78fu},
      {"postfix_decrement", 0xa5b6c78fu, 0u, 0u, 0xa5b6c7ffu},
      {"multiply_field", 0xa5b6c7bfu, 3u, 1u, 0xa5b6c79fu},
      {"divide_field", 0xa5b6c7ffu, 2u, 3u, 0xa5b6c7bfu},
      {"remainder_field", 0xa5b6c7ffu, 4u, 3u, 0xa5b6c7bfu},
      {"add_field", 0xa5b6c7efu, 5u, 3u, 0xa5b6c7bfu},
      {"subtract_field", 0xa5b6c79fu, 3u, 6u, 0xa5b6c7efu},
      {"shift_left_field", 0xa5b6c7bfu, 2u, 4u, 0xa5b6c7cfu},
      {"shift_right_field", 0xa5b6c7efu, 1u, 3u, 0xa5b6c7bfu},
      {"and_field", 0xa5b6c7ffu, 2u, 2u, 0xa5b6c7afu},
      {"xor_field", 0xa5b6c7dfu, 3u, 6u, 0xa5b6c7efu},
      {"or_field", 0xa5b6c79fu, 4u, 5u, 0xa5b6c7dfu},
      {"signed_prefix_decrement", 0xa5b6c7cfu, 0u, 3u,
       0xa5b6c7bfu},
      {"signed_postfix_increment", 0xa5b6c7bfu, 0u, 3u,
       0xa5b6c7cfu},
      {"signed_add_field", 0xa5b6c7efu, 1u, 0xffffffffu,
       0xa5b6c7ffu},
      {"whole_prefix_increment", 0xffffffffu, 0u, 0u, 0u},
      {"whole_postfix_increment", 0xffffffffu, 0u, 0xffffffffu, 0u}};
  ctool_u32 index;
  for (index = 0u; index < (ctool_u32)(sizeof(cases) / sizeof(cases[0]));
       index++) {
    const ctool_elf32_symbol_t *symbol =
        find_symbol(object, cases[index].name);
    ctool_u32 result = 0u;
    ctool_u32 stored_value = 0u;
    if (symbol == NULL ||
        !bit_field_store_oracle_execute(
            job, text, symbol, cases[index].initial_storage,
            cases[index].right, &result, &stored_value) ||
        result != cases[index].expected_result ||
        stored_value != cases[index].expected_storage) {
      (void)fprintf(stderr,
                    "bit-field mutation result %s case %u differs: "
                    "eax=%08x storage=%08x\n",
                    cases[index].name, (unsigned)index, (unsigned)result,
                    (unsigned)stored_value);
      return 0;
    }
  }
  return validate_bit_field_designator_side_effect(job, object, text);
}

static int validate_bit_field_mutation_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  typedef struct {
    const char *name;
    ctool_u32 eax_indirect_loads;
    ctool_u32 memory_stores;
  } bit_field_mutation_function_t;
  static const bit_field_mutation_function_t functions[] = {
      {"prefix_increment", 3u, 1u},
      {"prefix_decrement", 3u, 1u},
      {"postfix_increment", 3u, 1u},
      {"postfix_decrement", 3u, 1u},
      {"indexed_postfix_increment_once", 5u, 2u},
      {"multiply_field", 4u, 1u},
      {"divide_field", 4u, 1u},
      {"remainder_field", 4u, 1u},
      {"add_field", 4u, 1u},
      {"subtract_field", 4u, 1u},
      {"shift_left_field", 4u, 1u},
      {"shift_right_field", 4u, 1u},
      {"and_field", 4u, 1u},
      {"xor_field", 4u, 1u},
      {"or_field", 4u, 1u},
      {"signed_prefix_decrement", 3u, 1u},
      {"signed_postfix_increment", 3u, 1u},
      {"signed_add_field", 4u, 1u},
      {"whole_prefix_increment", 2u, 1u},
      {"whole_postfix_increment", 2u, 1u}};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  ctool_u32 expected_offset = 0u;
  ctool_u32 index;
  if (job == NULL || object == NULL || text == NULL ||
      text->contents.data == NULL || text->contents.size != 1415u ||
      text->relocation_count != 0u || object->relocation_count != 0u ||
      object->symbol_count != 21u) {
    (void)fprintf(stderr,
                  "bit-field mutation ELF inventory differs: text=%u "
                  "symbols=%u relocations=%u\n",
                  text == NULL ? 0u : (unsigned)text->contents.size,
                  object == NULL ? 0u : (unsigned)object->symbol_count,
                  object == NULL ? 0u : (unsigned)object->relocation_count);
    return 0;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(functions) / sizeof(functions[0]));
       index++) {
    const ctool_elf32_symbol_t *symbol =
        find_symbol(object, functions[index].name);
    if (symbol == NULL || symbol->binding != CTOOL_ELF32_BIND_GLOBAL ||
        symbol->type != CTOOL_ELF32_SYMBOL_FUNCTION ||
        symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        symbol->section_file_index != text->file_index ||
        symbol->value != expected_offset ||
        !validate_bit_field_mutation_function(
            job, text, symbol, functions[index].eax_indirect_loads,
            functions[index].memory_stores)) {
      (void)fprintf(stderr, "bit-field mutation function %s differs\n",
                    functions[index].name);
      return 0;
    }
    expected_offset += symbol->size;
  }
  if (expected_offset != text->contents.size) {
    (void)fprintf(stderr, "bit-field mutation text coverage differs\n");
    return 0;
  }
  return validate_bit_field_mutation_results(job, object, text);
}

static int run_bit_field_mutation_object(const char *host_root) {
  static const char source[] =
      "struct flags {\n"
      "  unsigned int before : 4;\n"
      "  unsigned int value : 3;\n"
      "  unsigned int after : 25;\n"
      "};\n"
      "unsigned int prefix_increment(struct flags *state) {\n"
      "  return ++state->value;\n"
      "}\n"
      "unsigned int prefix_decrement(struct flags *state) {\n"
      "  return --state->value;\n"
      "}\n"
      "unsigned int postfix_increment(struct flags *state) {\n"
      "  return state->value++;\n"
      "}\n"
      "unsigned int postfix_decrement(struct flags *state) {\n"
      "  return state->value--;\n"
      "}\n"
      "unsigned int indexed_postfix_increment_once(\n"
      "    struct flags *states, unsigned int *index) {\n"
      "  return states[(*index)++].value++;\n"
      "}\n"
      "unsigned int multiply_field(struct flags *state, int right) {\n"
      "  return state->value *= right;\n"
      "}\n"
      "unsigned int divide_field(struct flags *state, int right) {\n"
      "  return state->value /= right;\n"
      "}\n"
      "unsigned int remainder_field(struct flags *state, int right) {\n"
      "  return state->value %= right;\n"
      "}\n"
      "unsigned int add_field(struct flags *state, int right) {\n"
      "  return state->value += right;\n"
      "}\n"
      "unsigned int subtract_field(struct flags *state, int right) {\n"
      "  return state->value -= right;\n"
      "}\n"
      "unsigned int shift_left_field(struct flags *state, int right) {\n"
      "  return state->value <<= right;\n"
      "}\n"
      "unsigned int shift_right_field(struct flags *state, int right) {\n"
      "  return state->value >>= right;\n"
      "}\n"
      "unsigned int and_field(struct flags *state, int right) {\n"
      "  return state->value &= right;\n"
      "}\n"
      "unsigned int xor_field(struct flags *state, int right) {\n"
      "  return state->value ^= right;\n"
      "}\n"
      "unsigned int or_field(struct flags *state, int right) {\n"
      "  return state->value |= right;\n"
      "}\n"
      "struct signed_flags {\n"
      "  unsigned int before : 4;\n"
      "  signed int value : 3;\n"
      "  unsigned int after : 25;\n"
      "};\n"
      "int signed_prefix_decrement(struct signed_flags *state) {\n"
      "  return --state->value;\n"
      "}\n"
      "int signed_postfix_increment(struct signed_flags *state) {\n"
      "  return state->value++;\n"
      "}\n"
      "int signed_add_field(struct signed_flags *state, int right) {\n"
      "  return state->value += right;\n"
      "}\n"
      "struct whole { volatile unsigned int value : 32; };\n"
      "unsigned int whole_prefix_increment(struct whole *state) {\n"
      "  return ++state->value;\n"
      "}\n"
      "unsigned int whole_postfix_increment(struct whole *state) {\n"
      "  return state->value++;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t bytes;
  ctool_u8 *first_object = NULL;
  ctool_u32 first_object_size = 0u;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/bit-field-mutation-object.c", source, &unit)) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "bit-field mutation object buffer") ||
      !expect_object_success_preserves_unit(
          job, &unit, output, "bit-field mutation object emission")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  bytes = ctool_buffer_view(output);
  first_object_size = bytes.size;
  first_object = (ctool_u8 *)malloc((size_t)first_object_size);
  if (first_object == NULL) {
    goto cleanup;
  }
  (void)memcpy(first_object, bytes.data, (size_t)bytes.size);
  if (ctool_buffer_rewind(output, 0u) != CTOOL_OK ||
      !expect_object_success_preserves_unit(
          job, &unit, output, "repeat bit-field mutation object emission")) {
    goto cleanup;
  }
  bytes = ctool_buffer_view(output);
  if (bytes.size != first_object_size ||
      memcmp(bytes.data, first_object, (size_t)bytes.size) != 0) {
    (void)fprintf(stderr, "bit-field mutation object is not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/bit-field-mutation-object.o");
  object_source.contents = bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read bit-field mutation object") ||
      !validate_bit_field_mutation_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(first_object);
  if (output != (ctool_buffer_t *)0) {
    ctool_buffer_close(output);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("bit-field-mutations: ok");
    return 0;
  }
  return 1;
}

static int wide_memory_base_is(const ctool_x86_operand_t *operand,
                               ctool_u32 register_index,
                               ctool_i32 displacement) {
  return operand != NULL && operand->kind == CTOOL_X86_OPERAND_MEMORY &&
                 operand->as.memory.base.class_id == CTOOL_X86_REG_GPR32 &&
                 operand->as.memory.base.index == register_index &&
                 operand->as.memory.displacement.kind ==
                     CTOOL_X86_VALUE_CONSTANT &&
                 (ctool_i32)operand->as.memory.displacement.bits ==
                     displacement
             ? 1
             : 0;
}

static int validate_wide_snapshot_function(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *function, ctool_u32 minimum_eax_stores,
    ctool_u32 minimum_edx_stores, ctool_u32 expected_return_pairs,
    int require_literal_words, const char *context) {
  ctool_u32 cursor = 0u;
  ctool_u32 eax_stores = 0u;
  ctool_u32 edx_stores = 0u;
  ctool_u32 eax_return_loads = 0u;
  ctool_u32 edx_return_loads = 0u;
  ctool_u32 literal_low = 0u;
  ctool_u32 literal_high = 0u;
  ctool_x86_mnemonic_t last = CTOOL_X86_MN_INVALID;
  if (job == NULL || text == NULL || function == NULL || context == NULL ||
      function->size == 0u || function->value > text->contents.size ||
      function->size > text->contents.size - function->value) {
    return 0;
  }
  while (cursor < function->size) {
    ctool_x86_decoded_t decoded;
    const ctool_x86_instruction_t *instruction;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + function->value + cursor,
        function->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      (void)fprintf(stderr, "%s: decode failed at %u\n", context,
                    (unsigned int)cursor);
      return 0;
    }
    instruction = &decoded.instruction;
    last = instruction->mnemonic;
    if (instruction->mnemonic == CTOOL_X86_MN_MOV &&
        instruction->operand_count == 2u) {
      const ctool_x86_operand_t *left = &instruction->operands[0];
      const ctool_x86_operand_t *right = &instruction->operands[1];
      if (left->kind == CTOOL_X86_OPERAND_MEMORY &&
          left->as.memory.base.class_id == CTOOL_X86_REG_GPR32 &&
          left->as.memory.base.index == NARROW_ORACLE_EBP &&
          left->as.memory.displacement.kind == CTOOL_X86_VALUE_CONSTANT &&
          (ctool_i32)left->as.memory.displacement.bits < 0 &&
          right->kind == CTOOL_X86_OPERAND_REGISTER &&
          aggregate_register_is(right->as.reg, NARROW_ORACLE_EAX) != 0) {
        eax_stores++;
      } else if (left->kind == CTOOL_X86_OPERAND_MEMORY &&
                 left->as.memory.base.class_id == CTOOL_X86_REG_GPR32 &&
                 left->as.memory.base.index == NARROW_ORACLE_EBP &&
                 left->as.memory.displacement.kind ==
                     CTOOL_X86_VALUE_CONSTANT &&
                 (ctool_i32)left->as.memory.displacement.bits < 0 &&
                 right->kind == CTOOL_X86_OPERAND_REGISTER &&
                 aggregate_register_is(right->as.reg, NARROW_ORACLE_EDX) !=
                     0) {
        edx_stores++;
      }
      if (left->kind == CTOOL_X86_OPERAND_REGISTER &&
          aggregate_register_is(left->as.reg, NARROW_ORACLE_EAX) != 0 &&
          wide_memory_base_is(right, NARROW_ORACLE_ECX, 0) != 0) {
        eax_return_loads++;
      }
      if (left->kind == CTOOL_X86_OPERAND_REGISTER &&
          aggregate_register_is(left->as.reg, NARROW_ORACLE_EDX) != 0 &&
          wide_memory_base_is(right, NARROW_ORACLE_ECX, 4) != 0) {
        edx_return_loads++;
      }
      if (left->kind == CTOOL_X86_OPERAND_REGISTER &&
          aggregate_register_is(left->as.reg, NARROW_ORACLE_EAX) != 0 &&
          right->kind == CTOOL_X86_OPERAND_IMMEDIATE &&
          right->as.value.kind == CTOOL_X86_VALUE_CONSTANT) {
        if (right->as.value.bits == 0x55667788u) {
          literal_low++;
        } else if (right->as.value.bits == 0x11223344u) {
          literal_high++;
        }
      }
    }
    cursor += decoded.consumed;
  }
  if (cursor != function->size || last != CTOOL_X86_MN_RET ||
      eax_stores < minimum_eax_stores || edx_stores < minimum_edx_stores ||
      eax_return_loads != expected_return_pairs ||
      edx_return_loads != expected_return_pairs ||
      (require_literal_words != 0 &&
       (literal_low != 1u || literal_high != 1u))) {
    (void)fprintf(stderr,
                  "%s: wide snapshot inventory differs: eax-store=%u "
                  "edx-store=%u eax-load=%u edx-load=%u low=%u high=%u\n",
                  context, (unsigned int)eax_stores,
                  (unsigned int)edx_stores,
                  (unsigned int)eax_return_loads,
                  (unsigned int)edx_return_loads,
                  (unsigned int)literal_low, (unsigned int)literal_high);
    return 0;
  }
  return 1;
}

#define WIDE_ORACLE_TEXT_LIMIT 16384u
#define WIDE_ORACLE_INITIAL_ESP 204u
#define WIDE_ORACLE_RETURN_SENTINEL 0x13579bdfu
#define WIDE_ORACLE_EBX_SENTINEL 0xc3d2e1f0u
#define WIDE_ORACLE_ESI_SENTINEL 0x4b5a6978u
#define WIDE_ORACLE_EDI_SENTINEL 0x8796a5b4u
#define WIDE_ORACLE_STEP_LIMIT 4096u
#define WIDE_ORACLE_CALL_LIMIT 32u

static const ctool_elf32_symbol_t *wide_oracle_symbol_by_file_index(
    const ctool_elf32_object_t *object, ctool_u32 file_index) {
  ctool_u32 index;
  if (object == NULL || object->symbols == NULL) {
    return (const ctool_elf32_symbol_t *)0;
  }
  for (index = 0u; index < object->symbol_count; index++) {
    if (object->symbols[index].file_index == file_index) {
      return &object->symbols[index];
    }
  }
  return (const ctool_elf32_symbol_t *)0;
}

static int wide_oracle_relocate_text_with_data(
    const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *text,
    const ctool_elf32_section_t *data, ctool_u32 data_base,
    ctool_u8 *relocated,
    ctool_u32 relocated_capacity) {
  ctool_u32 index;
  if (object == NULL || text == NULL || relocated == NULL ||
      text->contents.data == NULL || text->contents.size > relocated_capacity) {
    return 0;
  }
  (void)memcpy(relocated, text->contents.data, text->contents.size);
  for (index = 0u; index < object->relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[index];
    const ctool_elf32_symbol_t *target;
    int64_t target_value;
    int64_t relocated_value;
    ctool_u32 encoded;
    if (relocation->target_section_file_index != text->file_index) {
      continue;
    }
    target = wide_oracle_symbol_by_file_index(
        object, relocation->symbol_file_index);
    if (target == NULL ||
        target->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        relocation->addend_known != CTOOL_TRUE ||
        relocation->offset > text->contents.size ||
        4u > text->contents.size - relocation->offset) {
      return 0;
    }
    if (target->section_file_index == text->file_index) {
      target_value = (int64_t)target->value;
    } else if (data != NULL &&
               target->section_file_index == data->file_index &&
               target->value <= data->contents.size &&
               target->size <= data->contents.size - target->value &&
               target->value <= 0xffffffffu - data_base) {
      target_value = (int64_t)(data_base + target->value);
    } else {
      return 0;
    }
    if (relocation->type == CTOOL_ELF32_R_386_PC32) {
      relocated_value = target_value + relocation->addend -
                        (int64_t)relocation->offset;
    } else if (relocation->type == CTOOL_ELF32_R_386_32) {
      relocated_value = target_value + relocation->addend;
    } else {
      return 0;
    }
    encoded = (ctool_u32)(uint64_t)relocated_value;
    relocated[relocation->offset] = (ctool_u8)(encoded & 0xffu);
    relocated[relocation->offset + 1u] =
        (ctool_u8)((encoded >> 8u) & 0xffu);
    relocated[relocation->offset + 2u] =
        (ctool_u8)((encoded >> 16u) & 0xffu);
    relocated[relocation->offset + 3u] =
        (ctool_u8)((encoded >> 24u) & 0xffu);
  }
  return 1;
}

static int wide_oracle_relocate_text(
    const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *text, ctool_u8 *relocated,
    ctool_u32 relocated_capacity) {
  return wide_oracle_relocate_text_with_data(
      object, text, NULL, 0u, relocated, relocated_capacity);
}

static int wide_oracle_add_subtract_step(
    narrow_oracle_machine_t *machine,
    const ctool_x86_instruction_t *instruction, ctool_bool *carry_flag,
    ctool_bool *handled) {
  ctool_u32 left;
  ctool_u32 right;
  uint64_t result;
  if (machine == NULL || instruction == NULL || carry_flag == NULL ||
      handled == NULL) {
    return 0;
  }
  *handled = CTOOL_FALSE;
  if (instruction->mnemonic != CTOOL_X86_MN_ADD &&
      instruction->mnemonic != CTOOL_X86_MN_ADC &&
      instruction->mnemonic != CTOOL_X86_MN_SUB &&
      instruction->mnemonic != CTOOL_X86_MN_SBB) {
    return 1;
  }
  if (instruction->operand_count != 2u ||
      !narrow_oracle_read_operand(machine, &instruction->operands[0],
                                  &left) ||
      !narrow_oracle_read_operand(machine, &instruction->operands[1],
                                  &right)) {
    return 0;
  }
  if (instruction->mnemonic == CTOOL_X86_MN_ADD ||
      instruction->mnemonic == CTOOL_X86_MN_ADC) {
    result = (uint64_t)left + (uint64_t)right;
    if (instruction->mnemonic == CTOOL_X86_MN_ADC &&
        *carry_flag == CTOOL_TRUE) {
      result++;
    }
    *carry_flag = result > UINT64_C(0xffffffff) ? CTOOL_TRUE : CTOOL_FALSE;
  } else {
    uint64_t subtrahend = (uint64_t)right;
    if (instruction->mnemonic == CTOOL_X86_MN_SBB &&
        *carry_flag == CTOOL_TRUE) {
      subtrahend++;
    }
    result = (uint64_t)left - subtrahend;
    *carry_flag = (uint64_t)left < subtrahend ? CTOOL_TRUE : CTOOL_FALSE;
  }
  if (!narrow_oracle_write_operand(machine, &instruction->operands[0],
                                   (ctool_u32)result)) {
    return 0;
  }
  *handled = CTOOL_TRUE;
  return 1;
}

static int wide_oracle_unary_step(
    narrow_oracle_machine_t *machine,
    const ctool_x86_instruction_t *instruction, ctool_bool *carry_flag,
    ctool_bool *handled) {
  ctool_u32 value;
  if (machine == NULL || instruction == NULL || carry_flag == NULL ||
      handled == NULL) {
    return 0;
  }
  *handled = CTOOL_FALSE;
  if (instruction->mnemonic != CTOOL_X86_MN_NEG &&
      instruction->mnemonic != CTOOL_X86_MN_NOT) {
    return 1;
  }
  if (instruction->operand_count != 1u ||
      !narrow_oracle_read_operand(machine, &instruction->operands[0],
                                  &value)) {
    return 0;
  }
  if (instruction->mnemonic == CTOOL_X86_MN_NEG) {
    *carry_flag = value == 0u ? CTOOL_FALSE : CTOOL_TRUE;
    value = 0u - value;
  } else {
    value = ~value;
  }
  if (!narrow_oracle_write_operand(machine, &instruction->operands[0],
                                   value)) {
    return 0;
  }
  *handled = CTOOL_TRUE;
  return 1;
}

static int wide_oracle_multiply_step(
    narrow_oracle_machine_t *machine,
    const ctool_x86_instruction_t *instruction, ctool_bool *handled) {
  ctool_u32 right;
  uint64_t product;
  if (machine == NULL || instruction == NULL || handled == NULL) {
    return 0;
  }
  *handled = CTOOL_FALSE;
  if (instruction->mnemonic != CTOOL_X86_MN_MUL) {
    return 1;
  }
  if (instruction->operand_count != 1u ||
      instruction->operands[0].width_bits != 32u ||
      !narrow_oracle_read_operand(machine, &instruction->operands[0],
                                  &right)) {
    return 0;
  }
  product = (uint64_t)machine->registers[NARROW_ORACLE_EAX] *
            (uint64_t)right;
  machine->registers[NARROW_ORACLE_EAX] = (ctool_u32)product;
  machine->registers[NARROW_ORACLE_EDX] =
      (ctool_u32)(product >> 32u);
  *handled = CTOOL_TRUE;
  return 1;
}

static int wide_oracle_shift_step(
    narrow_oracle_machine_t *machine,
    const ctool_x86_instruction_t *instruction, ctool_bool *carry_flag,
    ctool_bool *handled) {
  ctool_u32 value;
  ctool_u32 count;
  ctool_u32 step;
  if (machine == NULL || instruction == NULL || carry_flag == NULL ||
      handled == NULL) {
    return 0;
  }
  *handled = CTOOL_FALSE;
  if (instruction->mnemonic != CTOOL_X86_MN_SHL &&
      instruction->mnemonic != CTOOL_X86_MN_SHR &&
      instruction->mnemonic != CTOOL_X86_MN_SAR &&
      instruction->mnemonic != CTOOL_X86_MN_RCL &&
      instruction->mnemonic != CTOOL_X86_MN_RCR) {
    return 1;
  }
  if (instruction->operand_count != 2u ||
      !narrow_oracle_read_operand(machine, &instruction->operands[0],
                                  &value) ||
      !narrow_oracle_read_operand(machine, &instruction->operands[1],
                                  &count)) {
    return 0;
  }
  count &= 31u;
  for (step = 0u; step < count; step++) {
    ctool_bool next_carry;
    if (instruction->mnemonic == CTOOL_X86_MN_SHL) {
      next_carry = (value & 0x80000000u) != 0u ? CTOOL_TRUE : CTOOL_FALSE;
      value <<= 1u;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SHR) {
      next_carry = (value & 1u) != 0u ? CTOOL_TRUE : CTOOL_FALSE;
      value >>= 1u;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SAR) {
      ctool_u32 sign = value & 0x80000000u;
      next_carry = (value & 1u) != 0u ? CTOOL_TRUE : CTOOL_FALSE;
      value = (value >> 1u) | sign;
    } else if (instruction->mnemonic == CTOOL_X86_MN_RCL) {
      next_carry = (value & 0x80000000u) != 0u ? CTOOL_TRUE : CTOOL_FALSE;
      value = (value << 1u) |
              (*carry_flag == CTOOL_TRUE ? 1u : 0u);
    } else {
      next_carry = (value & 1u) != 0u ? CTOOL_TRUE : CTOOL_FALSE;
      value = (value >> 1u) |
              (*carry_flag == CTOOL_TRUE ? 0x80000000u : 0u);
    }
    *carry_flag = next_carry;
  }
  if (!narrow_oracle_write_operand(machine, &instruction->operands[0],
                                   value)) {
    return 0;
  }
  *handled = CTOOL_TRUE;
  return 1;
}

static int wide_oracle_flag_step(
    narrow_oracle_machine_t *machine,
    const ctool_x86_instruction_t *instruction, ctool_bool *zero_flag,
    ctool_bool *carry_flag, ctool_bool *sign_flag,
    ctool_bool *overflow_flag,
    ctool_bool *handled) {
  ctool_u32 left;
  ctool_u32 right;
  ctool_u32 result;
  if (machine == NULL || instruction == NULL || zero_flag == NULL ||
      carry_flag == NULL || sign_flag == NULL || overflow_flag == NULL ||
      handled == NULL) {
    return 0;
  }
  *handled = CTOOL_FALSE;
  if (instruction->mnemonic == CTOOL_X86_MN_DEC &&
      instruction->operand_count == 1u &&
      narrow_oracle_read_operand(machine, &instruction->operands[0],
                                 &left)) {
    right = left;
    left--;
    if (!narrow_oracle_write_operand(machine, &instruction->operands[0],
                                     left)) {
      return 0;
    }
    *zero_flag = left == 0u ? CTOOL_TRUE : CTOOL_FALSE;
    *sign_flag =
        (left & 0x80000000u) != 0u ? CTOOL_TRUE : CTOOL_FALSE;
    *overflow_flag =
        right == 0x80000000u ? CTOOL_TRUE : CTOOL_FALSE;
    *handled = CTOOL_TRUE;
    return 1;
  }
  if ((instruction->mnemonic == CTOOL_X86_MN_CMP ||
       instruction->mnemonic == CTOOL_X86_MN_TEST) &&
      instruction->operand_count == 2u) {
    if (!narrow_oracle_read_operand(machine, &instruction->operands[0],
                                    &left) ||
        !narrow_oracle_read_operand(machine, &instruction->operands[1],
                                    &right)) {
      return 0;
    }
    result = instruction->mnemonic == CTOOL_X86_MN_CMP
                 ? left - right
                 : left & right;
    *zero_flag = result == 0u ? CTOOL_TRUE : CTOOL_FALSE;
    *sign_flag =
        (result & 0x80000000u) != 0u ? CTOOL_TRUE : CTOOL_FALSE;
    if (instruction->mnemonic == CTOOL_X86_MN_CMP) {
      *carry_flag = left < right ? CTOOL_TRUE : CTOOL_FALSE;
      *overflow_flag =
          (((left ^ right) & (left ^ result) & 0x80000000u) != 0u)
              ? CTOOL_TRUE
              : CTOOL_FALSE;
    } else {
      *carry_flag = CTOOL_FALSE;
      *overflow_flag = CTOOL_FALSE;
    }
    *handled = CTOOL_TRUE;
    return 1;
  }
  if ((instruction->mnemonic == CTOOL_X86_MN_SETE ||
       instruction->mnemonic == CTOOL_X86_MN_SETNE ||
       instruction->mnemonic == CTOOL_X86_MN_SETB ||
       instruction->mnemonic == CTOOL_X86_MN_SETBE ||
       instruction->mnemonic == CTOOL_X86_MN_SETA ||
       instruction->mnemonic == CTOOL_X86_MN_SETAE ||
       instruction->mnemonic == CTOOL_X86_MN_SETL ||
       instruction->mnemonic == CTOOL_X86_MN_SETLE ||
       instruction->mnemonic == CTOOL_X86_MN_SETG ||
       instruction->mnemonic == CTOOL_X86_MN_SETGE) &&
      instruction->operand_count == 1u) {
    ctool_bool predicate = CTOOL_FALSE;
    if (instruction->mnemonic == CTOOL_X86_MN_SETE) {
      predicate = *zero_flag;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SETNE) {
      predicate =
          *zero_flag == CTOOL_TRUE ? CTOOL_FALSE : CTOOL_TRUE;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SETB) {
      predicate = *carry_flag;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SETBE) {
      predicate = *carry_flag == CTOOL_TRUE ||
                          *zero_flag == CTOOL_TRUE
                      ? CTOOL_TRUE
                      : CTOOL_FALSE;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SETA) {
      predicate = *carry_flag == CTOOL_FALSE &&
                          *zero_flag == CTOOL_FALSE
                      ? CTOOL_TRUE
                      : CTOOL_FALSE;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SETAE) {
      predicate =
          *carry_flag == CTOOL_TRUE ? CTOOL_FALSE : CTOOL_TRUE;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SETL) {
      predicate = *sign_flag != *overflow_flag ? CTOOL_TRUE : CTOOL_FALSE;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SETLE) {
      predicate = *zero_flag == CTOOL_TRUE ||
                          *sign_flag != *overflow_flag
                      ? CTOOL_TRUE
                      : CTOOL_FALSE;
    } else if (instruction->mnemonic == CTOOL_X86_MN_SETG) {
      predicate = *zero_flag == CTOOL_FALSE &&
                          *sign_flag == *overflow_flag
                      ? CTOOL_TRUE
                      : CTOOL_FALSE;
    } else {
      predicate =
          *sign_flag == *overflow_flag ? CTOOL_TRUE : CTOOL_FALSE;
    }
    if (!narrow_oracle_write_operand(
            machine, &instruction->operands[0],
            predicate == CTOOL_TRUE ? 1u : 0u)) {
      return 0;
    }
    *handled = CTOOL_TRUE;
  }
  return 1;
}

static int wide_oracle_execute_arguments(
    ctool_job_t *job, const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *symbol, const ctool_u32 *arguments,
    ctool_u32 argument_count,
    ctool_u32 *low_out, ctool_u32 *high_out) {
  ctool_u8 relocated_text[WIDE_ORACLE_TEXT_LIMIT];
  narrow_oracle_machine_t machine;
  ctool_u32 pc;
  ctool_u32 call_depth = 0u;
  ctool_u32 step_count = 0u;
  ctool_u32 preserved;
  ctool_u32 stack_pointer;
  ctool_u32 argument;
  ctool_bool zero_flag = CTOOL_FALSE;
  ctool_bool carry_flag = CTOOL_FALSE;
  ctool_bool sign_flag = CTOOL_FALSE;
  ctool_bool overflow_flag = CTOOL_FALSE;
  ctool_bool direction_clear = CTOOL_FALSE;
  ctool_bool returned = CTOOL_FALSE;
  if (job == NULL || object == NULL || text == NULL || symbol == NULL ||
      low_out == NULL || high_out == NULL ||
      (argument_count != 0u && arguments == NULL) ||
      argument_count >
          (NARROW_ORACLE_MEMORY_SIZE - WIDE_ORACLE_INITIAL_ESP - 4u) / 4u ||
      symbol->value > text->contents.size ||
      symbol->size > text->contents.size - symbol->value ||
      !wide_oracle_relocate_text(object, text, relocated_text,
                                 WIDE_ORACLE_TEXT_LIMIT)) {
    return 0;
  }
  (void)memset(&machine, 0, sizeof(machine));
  machine.registers[NARROW_ORACLE_ESP] = WIDE_ORACLE_INITIAL_ESP;
  machine.registers[NARROW_ORACLE_EBP] = 64u;
  machine.registers[NARROW_ORACLE_EBX] = WIDE_ORACLE_EBX_SENTINEL;
  machine.registers[NARROW_ORACLE_ESI] = WIDE_ORACLE_ESI_SENTINEL;
  machine.registers[NARROW_ORACLE_EDI] = WIDE_ORACLE_EDI_SENTINEL;
  if (!narrow_oracle_write_memory(&machine, WIDE_ORACLE_INITIAL_ESP, 32u,
                                  WIDE_ORACLE_RETURN_SENTINEL)) {
    return 0;
  }
  for (argument = 0u; argument < argument_count; argument++) {
    if (!narrow_oracle_write_memory(
            &machine, WIDE_ORACLE_INITIAL_ESP + 4u + argument * 4u, 32u,
            arguments[argument])) {
      return 0;
    }
  }
  pc = symbol->value;
  while (pc < text->contents.size && returned == CTOOL_FALSE &&
         step_count < WIDE_ORACLE_STEP_LIMIT) {
    ctool_x86_decoded_t decoded;
    const ctool_x86_instruction_t *instruction;
    ctool_bytes_t remaining =
        ctool_bytes(relocated_text + pc, text->contents.size - pc);
    ctool_u32 next_pc;
    ctool_u32 target;
    ctool_bool handled;
    ctool_status_t status;
    ctool_bool leaf_return = CTOOL_FALSE;
    step_count++;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u || decoded.consumed > text->contents.size - pc) {
      (void)fprintf(stderr, "wide return oracle decode stopped at %u\n",
                    (unsigned int)pc);
      return 0;
    }
    instruction = &decoded.instruction;
    next_pc = pc + decoded.consumed;
    if (instruction->mnemonic == CTOOL_X86_MN_CALL) {
      if (call_depth >= WIDE_ORACLE_CALL_LIMIT ||
          instruction->operand_count != 1u) {
        return 0;
      }
      if (instruction->operands[0].kind == CTOOL_X86_OPERAND_RELATIVE) {
        if (!call_alignment_branch_target(&decoded, pc,
                                          text->contents.size, &target)) {
          return 0;
        }
      } else if (!narrow_oracle_read_operand(
                     &machine, &instruction->operands[0], &target) ||
                 target >= text->contents.size) {
        return 0;
      }
      stack_pointer = machine.registers[NARROW_ORACLE_ESP] - 4u;
      if (!narrow_oracle_write_memory(&machine, stack_pointer, 32u,
                                      next_pc)) {
        return 0;
      }
      machine.registers[NARROW_ORACLE_ESP] = stack_pointer;
      call_depth++;
      pc = target;
      continue;
    }
    if (instruction->mnemonic == CTOOL_X86_MN_RET &&
        instruction->operand_count == 0u) {
      stack_pointer = machine.registers[NARROW_ORACLE_ESP];
      if (!narrow_oracle_read_memory(&machine, stack_pointer, 32u,
                                     &target)) {
        return 0;
      }
      machine.registers[NARROW_ORACLE_ESP] = stack_pointer + 4u;
      if (call_depth == 0u) {
        if (target != WIDE_ORACLE_RETURN_SENTINEL) {
          return 0;
        }
        returned = CTOOL_TRUE;
      } else {
        call_depth--;
        if (target >= text->contents.size) {
          return 0;
        }
        pc = target;
      }
      continue;
    }
    if (instruction->mnemonic == CTOOL_X86_MN_JMP ||
        instruction->mnemonic == CTOOL_X86_MN_JE ||
        instruction->mnemonic == CTOOL_X86_MN_JNE) {
      if (!call_alignment_branch_target(&decoded, pc,
                                        text->contents.size, &target)) {
        return 0;
      }
      pc = instruction->mnemonic == CTOOL_X86_MN_JMP ||
                   (instruction->mnemonic == CTOOL_X86_MN_JE &&
                    zero_flag == CTOOL_TRUE) ||
                   (instruction->mnemonic == CTOOL_X86_MN_JNE &&
                    zero_flag == CTOOL_FALSE)
               ? target
               : next_pc;
      continue;
    }
    if (instruction->mnemonic == CTOOL_X86_MN_CLD) {
      direction_clear = CTOOL_TRUE;
      pc = next_pc;
      continue;
    }
    if (instruction->mnemonic == CTOOL_X86_MN_MOVSB) {
      ctool_u32 count = machine.registers[1u];
      ctool_u32 source = machine.registers[6u];
      ctool_u32 destination = machine.registers[7u];
      ctool_u32 byte;
      if (instruction->prefixes != CTOOL_X86_PREFIX_REP ||
          direction_clear == CTOOL_FALSE ||
          !narrow_oracle_memory_range(source, count) ||
          !narrow_oracle_memory_range(destination, count)) {
        return 0;
      }
      for (byte = 0u; byte < count; byte++) {
        machine.memory[destination + byte] = machine.memory[source + byte];
      }
      machine.registers[1u] = 0u;
      machine.registers[6u] = source + count;
      machine.registers[7u] = destination + count;
      pc = next_pc;
      continue;
    }
    if (instruction->mnemonic == CTOOL_X86_MN_STOSB) {
      ctool_u32 count = machine.registers[1u];
      ctool_u32 destination = machine.registers[7u];
      ctool_u32 byte;
      if (instruction->prefixes != CTOOL_X86_PREFIX_REP ||
          direction_clear == CTOOL_FALSE ||
          !narrow_oracle_memory_range(destination, count)) {
        return 0;
      }
      for (byte = 0u; byte < count; byte++) {
        machine.memory[destination + byte] =
            (ctool_u8)(machine.registers[NARROW_ORACLE_EAX] & 0xffu);
      }
      machine.registers[1u] = 0u;
      machine.registers[7u] = destination + count;
      pc = next_pc;
      continue;
    }
    if (!wide_oracle_add_subtract_step(&machine, instruction, &carry_flag,
                                       &handled) ||
        (handled == CTOOL_FALSE &&
         !wide_oracle_unary_step(&machine, instruction, &carry_flag,
                                 &handled)) ||
        (handled == CTOOL_FALSE &&
         !wide_oracle_multiply_step(&machine, instruction, &handled)) ||
        (handled == CTOOL_FALSE &&
         !wide_oracle_shift_step(&machine, instruction, &carry_flag,
                                 &handled)) ||
        (handled == CTOOL_FALSE &&
         !wide_oracle_flag_step(&machine, instruction, &zero_flag,
                                &carry_flag, &sign_flag, &overflow_flag,
                                &handled)) ||
        (handled == CTOOL_FALSE &&
         !narrow_oracle_step(&machine, instruction, &leaf_return)) ||
        leaf_return != CTOOL_FALSE) {
      (void)fprintf(stderr, "wide return oracle stopped at %u on %s\n",
                    (unsigned int)pc,
                    ctool_x86_mnemonic_name(instruction->mnemonic).data);
      return 0;
    }
    pc = next_pc;
  }
  if (returned == CTOOL_FALSE || call_depth != 0u ||
      step_count >= WIDE_ORACLE_STEP_LIMIT ||
      machine.registers[NARROW_ORACLE_ESP] != WIDE_ORACLE_INITIAL_ESP + 4u ||
      machine.registers[NARROW_ORACLE_EBP] != 64u ||
      machine.registers[NARROW_ORACLE_EBX] != WIDE_ORACLE_EBX_SENTINEL ||
      machine.registers[NARROW_ORACLE_ESI] != WIDE_ORACLE_ESI_SENTINEL ||
      machine.registers[NARROW_ORACLE_EDI] != WIDE_ORACLE_EDI_SENTINEL ||
      !narrow_oracle_read_memory(&machine, WIDE_ORACLE_INITIAL_ESP, 32u,
                                 &preserved) ||
      preserved != WIDE_ORACLE_RETURN_SENTINEL) {
    return 0;
  }
  for (argument = 0u; argument < argument_count; argument++) {
    if (!narrow_oracle_read_memory(
            &machine, WIDE_ORACLE_INITIAL_ESP + 4u + argument * 4u, 32u,
            &preserved) ||
        preserved != arguments[argument]) {
      return 0;
    }
  }
  *low_out = machine.registers[NARROW_ORACLE_EAX];
  *high_out = machine.registers[NARROW_ORACLE_EDX];
  return 1;
}

static int wide_oracle_execute(
    ctool_job_t *job, const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *symbol, ctool_u32 input,
    ctool_u32 *low_out, ctool_u32 *high_out) {
  return wide_oracle_execute_arguments(
      job, object, text, symbol, &input, 1u, low_out, high_out);
}

static int wide_function_symbol_is_valid(
    const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *symbol) {
  return object != NULL && text != NULL && symbol != NULL &&
                 symbol->type == CTOOL_ELF32_SYMBOL_FUNCTION &&
                 symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED &&
                 symbol->section_file_index == text->file_index &&
                 symbol->size != 0u && symbol->value <= text->contents.size &&
                 symbol->size <= text->contents.size - symbol->value
             ? 1
             : 0;
}

static int wide_value_oracle_run(
    ctool_job_t *job, ctool_bytes_t text,
    const ctool_elf32_symbol_t *symbol, narrow_oracle_machine_t *machine,
    ctool_u32 expected_copies) {
  ctool_u32 cursor = 0u;
  ctool_u32 copy_count = 0u;
  ctool_bool returned = CTOOL_FALSE;
  ctool_bool cld_ready = CTOOL_FALSE;
  if (job == NULL || text.data == NULL || symbol == NULL || machine == NULL ||
      symbol->value > text.size || symbol->size > text.size - symbol->value) {
    return 0;
  }
  while (cursor < symbol->size && returned == CTOOL_FALSE) {
    ctool_x86_decoded_t decoded;
    const ctool_x86_instruction_t *instruction;
    ctool_bytes_t remaining = ctool_bytes(
        text.data + symbol->value + cursor, symbol->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      return 0;
    }
    instruction = &decoded.instruction;
    if (instruction->mnemonic == CTOOL_X86_MN_CLD) {
      cld_ready = CTOOL_TRUE;
    } else if (instruction->mnemonic == CTOOL_X86_MN_MOVSB) {
      ctool_u32 count = machine->registers[1u];
      ctool_u32 source = machine->registers[6u];
      ctool_u32 destination = machine->registers[7u];
      ctool_u32 index;
      if (instruction->prefixes != CTOOL_X86_PREFIX_REP ||
          cld_ready == CTOOL_FALSE || count != 8u ||
          !narrow_oracle_memory_range(source, count) ||
          !narrow_oracle_memory_range(destination, count)) {
        return 0;
      }
      for (index = 0u; index < count; index++) {
        machine->memory[destination + index] = machine->memory[source + index];
      }
      machine->registers[1u] = 0u;
      machine->registers[6u] = source + count;
      machine->registers[7u] = destination + count;
      cld_ready = CTOOL_FALSE;
      copy_count++;
    } else if (!narrow_oracle_step(machine, instruction, &returned)) {
      (void)fprintf(stderr, "wide object oracle stopped at %u on %s\n",
                    (unsigned int)cursor,
                    ctool_x86_mnemonic_name(instruction->mnemonic).data);
      return 0;
    }
    cursor += decoded.consumed;
  }
  return returned == CTOOL_TRUE && cursor == symbol->size &&
                 copy_count == expected_copies
             ? 1
             : 0;
}

static int wide_value_oracle_execute(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *symbol, ctool_bool chained,
    ctool_u32 expected_copies, ctool_u32 source_low,
    ctool_u32 source_high) {
  narrow_oracle_machine_t machine;
  const ctool_u32 first_address = 32u;
  const ctool_u32 second_address = 48u;
  const ctool_u32 source_address = 64u;
  ctool_u32 observed;
  if (job == NULL || text == NULL || symbol == NULL ||
      symbol->value > text->contents.size ||
      symbol->size > text->contents.size - symbol->value) {
    return 0;
  }
  (void)memset(&machine, 0xcd, sizeof(machine));
  machine.registers[NARROW_ORACLE_ESP] = NARROW_ORACLE_INITIAL_ESP;
  machine.registers[NARROW_ORACLE_EBP] = 96u;
  if (!narrow_oracle_write_memory(
          &machine, first_address, 32u, 0xdeadbeefu) ||
      !narrow_oracle_write_memory(
          &machine, first_address + 4u, 32u, 0xcafebabeu) ||
      !narrow_oracle_write_memory(
          &machine, second_address, 32u, 0x01020304u) ||
      !narrow_oracle_write_memory(
          &machine, second_address + 4u, 32u, 0x05060708u) ||
      !narrow_oracle_write_memory(
          &machine, source_address, 32u, source_low) ||
      !narrow_oracle_write_memory(
          &machine, source_address + 4u, 32u, source_high) ||
      !narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP, 32u, 0x13579bdfu) ||
      !narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 4u, 32u,
          first_address) ||
      !narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP + 8u, 32u,
          chained == CTOOL_TRUE ? second_address : source_address) ||
      (chained == CTOOL_TRUE &&
       !narrow_oracle_write_memory(
           &machine, NARROW_ORACLE_INITIAL_ESP + 12u, 32u,
           source_address))) {
    return 0;
  }
  if (!wide_value_oracle_run(
          job, text->contents, symbol, &machine, expected_copies) ||
      machine.registers[NARROW_ORACLE_ESP] != NARROW_ORACLE_INITIAL_ESP ||
      machine.registers[NARROW_ORACLE_EBP] != 96u ||
      machine.registers[NARROW_ORACLE_EAX] != source_low ||
      machine.registers[NARROW_ORACLE_EDX] != source_high ||
      !narrow_oracle_read_memory(
          &machine, first_address, 32u, &observed) ||
      observed != source_low ||
      !narrow_oracle_read_memory(
          &machine, first_address + 4u, 32u, &observed) ||
      observed != source_high ||
      !narrow_oracle_read_memory(
          &machine, second_address, 32u, &observed) ||
      observed != (chained == CTOOL_TRUE ? source_low : 0x01020304u) ||
      !narrow_oracle_read_memory(
          &machine, second_address + 4u, 32u, &observed) ||
      observed != (chained == CTOOL_TRUE ? source_high : 0x05060708u) ||
      !narrow_oracle_read_memory(
          &machine, source_address, 32u, &observed) ||
      observed != source_low ||
      !narrow_oracle_read_memory(
          &machine, source_address + 4u, 32u, &observed) ||
      observed != source_high ||
      !narrow_oracle_read_memory(
          &machine, first_address - 4u, 32u, &observed) ||
      observed != 0xcdcdcdcdu ||
      !narrow_oracle_read_memory(
          &machine, first_address + 8u, 32u, &observed) ||
      observed != 0xcdcdcdcdu ||
      !narrow_oracle_read_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP, 32u, &observed) ||
      observed != 0x13579bdfu) {
    return 0;
  }
  return 1;
}

static int wide_file_value_oracle_execute(
    ctool_job_t *job, const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *text,
    const ctool_elf32_section_t *data,
    const ctool_elf32_symbol_t *symbol, ctool_u32 expected_low,
    ctool_u32 expected_high) {
  const ctool_u32 data_base = 32u;
  ctool_u8 relocated_text[WIDE_ORACLE_TEXT_LIMIT];
  narrow_oracle_machine_t machine;
  ctool_u32 observed;
  if (job == NULL || object == NULL || text == NULL || data == NULL ||
      symbol == NULL || text->contents.size > WIDE_ORACLE_TEXT_LIMIT ||
      data->contents.data == NULL ||
      data->contents.size > NARROW_ORACLE_MEMORY_SIZE - data_base ||
      symbol->value > text->contents.size ||
      symbol->size > text->contents.size - symbol->value ||
      !wide_oracle_relocate_text_with_data(
          object, text, data, data_base, relocated_text,
          WIDE_ORACLE_TEXT_LIMIT)) {
    return 0;
  }
  (void)memset(&machine, 0xcd, sizeof(machine));
  (void)memcpy(machine.memory + data_base, data->contents.data,
               data->contents.size);
  machine.registers[NARROW_ORACLE_ESP] = NARROW_ORACLE_INITIAL_ESP;
  machine.registers[NARROW_ORACLE_EBP] = 96u;
  if (!narrow_oracle_write_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP, 32u,
          WIDE_ORACLE_RETURN_SENTINEL) ||
      !wide_value_oracle_run(
          job, ctool_bytes(relocated_text, text->contents.size), symbol,
          &machine, 1u) ||
      machine.registers[NARROW_ORACLE_ESP] != NARROW_ORACLE_INITIAL_ESP ||
      machine.registers[NARROW_ORACLE_EBP] != 96u ||
      machine.registers[NARROW_ORACLE_EAX] != expected_low ||
      machine.registers[NARROW_ORACLE_EDX] != expected_high ||
      !narrow_oracle_read_memory(
          &machine, NARROW_ORACLE_INITIAL_ESP, 32u, &observed) ||
      observed != WIDE_ORACLE_RETURN_SENTINEL ||
      memcmp(machine.memory + data_base, data->contents.data,
             data->contents.size) != 0 ||
      !narrow_oracle_read_memory(&machine, data_base - 4u, 32u,
                                 &observed) ||
      observed != 0xcdcdcdcdu ||
      !narrow_oracle_read_memory(
          &machine, data_base + data->contents.size, 32u, &observed) ||
      observed != 0xcdcdcdcdu) {
    return 0;
  }
  return 1;
}

static int validate_wide_value_function(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *symbol, ctool_u32 expected_copies,
    ctool_u32 expected_zeroes) {
  ctool_u32 cursor = 0u;
  ctool_u32 copies = 0u;
  ctool_u32 zeroes = 0u;
  ctool_u32 clears = 0u;
  ctool_u32 returns = 0u;
  if (job == NULL || text == NULL || symbol == NULL || symbol->size == 0u ||
      symbol->value > text->contents.size ||
      symbol->size > text->contents.size - symbol->value) {
    return 0;
  }
  while (cursor < symbol->size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + symbol->value + cursor,
        symbol->size - cursor);
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      return 0;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_MOVSB) {
      if (decoded.instruction.prefixes != CTOOL_X86_PREFIX_REP) {
        return 0;
      }
      copies++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_STOSB) {
      if (decoded.instruction.prefixes != CTOOL_X86_PREFIX_REP) {
        return 0;
      }
      zeroes++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_CLD) {
      clears++;
    } else if (decoded.instruction.mnemonic == CTOOL_X86_MN_RET) {
      returns++;
    }
    cursor += decoded.consumed;
  }
  if (cursor != symbol->size || copies != expected_copies ||
      zeroes != expected_zeroes ||
      clears != expected_copies + expected_zeroes || returns != 1u) {
    (void)fprintf(stderr,
                  "wide value function inventory differs: copies=%u/%u "
                  "zeroes=%u/%u clears=%u returns=%u cursor=%u/%u\n",
                  (unsigned int)copies, (unsigned int)expected_copies,
                  (unsigned int)zeroes, (unsigned int)expected_zeroes,
                  (unsigned int)clears, (unsigned int)returns,
                  (unsigned int)cursor, (unsigned int)symbol->size);
    return 0;
  }
  return 1;
}

static int validate_wide_value_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const struct {
    const char *name;
    ctool_u32 copies;
    ctool_u32 zeroes;
  } expected[] = {
      {"get_cpu_freq", 1u, 0u},
      {"load_pointer", 1u, 0u},
      {"local_round_trip", 3u, 0u},
      {"assign_pointer", 2u, 0u},
      {"chain_pointer", 3u, 0u},
      {"load_member", 1u, 0u},
      {"aggregate_round_trip", 3u, 1u},
      {"load_index", 1u, 0u},
      {"block_static", 1u, 0u},
      {"volatile_round_trip", 3u, 0u},
      {"discard_pointer", 1u, 0u}};
  static const ctool_u8 expected_data[] = {
      0x88u, 0x77u, 0x66u, 0x55u, 0x44u, 0x33u, 0x22u, 0x11u,
      0x11u, 0x00u, 0xffu, 0xeeu, 0xddu, 0xccu, 0xbbu, 0xaau};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *data = find_section(object, ".data");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *frequency = find_symbol(object, "tsc_freq");
  const ctool_elf32_symbol_t *block_value =
      find_symbol(object, ".LBS2.value");
  const ctool_elf32_symbol_t *functions[
      sizeof(expected) / sizeof(expected[0])];
  ctool_u32 frequency_relocations = 0u;
  ctool_u32 block_relocations = 0u;
  ctool_u32 index;
  for (index = 0u;
       index < (ctool_u32)(sizeof(functions) / sizeof(functions[0]));
       index++) {
    functions[index] = find_symbol(object, expected[index].name);
    if (!wide_function_symbol_is_valid(object, text, functions[index])) {
      (void)fprintf(stderr, "wide value function %s differs\n",
                    expected[index].name);
      return 0;
    }
  }
  if (text == NULL || data == NULL || rel_text == NULL || frequency == NULL ||
      block_value == NULL ||
      data->contents.data == NULL ||
      data->contents.size != sizeof(expected_data) ||
      memcmp(data->contents.data, expected_data, sizeof(expected_data)) != 0 ||
      text->contents.size != 879u ||
      structure_text_fingerprint(text->contents) != 0x2448a1cdu ||
      object->symbol_count != 14u || object->symbols == NULL ||
      !symbol_matches(frequency, frequency->file_index,
                      CTOOL_ELF32_BIND_LOCAL, CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 0u, 8u) ||
      !symbol_matches(block_value, block_value->file_index,
                      CTOOL_ELF32_BIND_LOCAL, CTOOL_ELF32_SYMBOL_OBJECT,
                      CTOOL_ELF32_SYMBOL_DEFINED, data->file_index, 8u, 8u) ||
      text->relocation_count != 2u || object->relocation_count != 2u) {
    (void)fprintf(
        stderr,
        "wide value object inventory differs: text=%u fingerprint=%08x "
        "symbols=%u block=%u\n",
        text == NULL ? 0u : (unsigned int)text->contents.size,
        text == NULL
            ? 0u
            : (unsigned int)structure_text_fingerprint(text->contents),
        (unsigned int)object->symbol_count, block_value == NULL ? 0u : 1u);
    return 0;
  }
  for (index = 0u; index < object->relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[index];
    const ctool_elf32_symbol_t *target = wide_oracle_symbol_by_file_index(
        object, relocation->symbol_file_index);
    if (relocation->relocation_section_file_index != rel_text->file_index ||
        relocation->target_section_file_index != text->file_index ||
        relocation->type != CTOOL_ELF32_R_386_32 ||
        relocation->addend_known != CTOOL_TRUE || relocation->addend != 0 ||
        target == NULL || target->type != CTOOL_ELF32_SYMBOL_OBJECT ||
        target->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
        target->section_file_index != data->file_index || target->size != 8u) {
      return 0;
    }
    if (target->file_index == frequency->file_index) {
      ctool_u32 relative;
      if (relocation->offset < functions[0]->value) {
        return 0;
      }
      relative = relocation->offset - functions[0]->value;
      if (relative > functions[0]->size ||
          4u > functions[0]->size - relative) {
        return 0;
      }
      frequency_relocations++;
    } else if (target->file_index == block_value->file_index) {
      ctool_u32 relative;
      if (relocation->offset < functions[8]->value) {
        return 0;
      }
      relative = relocation->offset - functions[8]->value;
      if (relative > functions[8]->size ||
          4u > functions[8]->size - relative) {
        return 0;
      }
      block_relocations++;
    } else {
      return 0;
    }
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(functions) / sizeof(functions[0]));
       index++) {
    if (!validate_wide_value_function(
            job, text, functions[index], expected[index].copies,
            expected[index].zeroes)) {
      (void)fprintf(stderr, "wide value decoder %s differs\n",
                    expected[index].name);
      return 0;
    }
  }
  return frequency_relocations == 1u && block_relocations == 1u &&
                 wide_file_value_oracle_execute(
                     job, object, text, data, functions[0], 0x55667788u,
                     0x11223344u) &&
                 wide_file_value_oracle_execute(
                     job, object, text, data, functions[8], 0xeeff0011u,
                     0xaabbccddu) &&
                 wide_value_oracle_execute(
                     job, text, functions[3], CTOOL_FALSE, 2u,
                     0x89abcdefu,
                     0x01234567u) &&
                 wide_value_oracle_execute(
                     job, text, functions[4], CTOOL_TRUE, 3u,
                     0x76543210u,
                     0xfedcba98u)
             ? 1
             : 0;
}

static int run_wide_value_object(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_buffer_t *failure = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t atomic_unit;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t first_bytes;
  ctool_bytes_t second_bytes;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&atomic_unit, 0, sizeof(atomic_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_object_sources_are_unchanged(job) ||
      !parse_source(job, "/wide-value-object.c", wide_value_object_source,
                    &unit) ||
      !parse_source(job, "/wide-atomic-object.c", wide_atomic_object_source,
                    &atomic_unit)) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &failure);
  }
  if (!check_status(status, CTOOL_OK, "wide value object buffers") ||
      !expect_object_success_preserves_unit(job, &unit, first,
                                            "first wide value object") ||
      !expect_object_success_preserves_unit(job, &unit, second,
                                            "repeat wide value object")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  first_bytes = ctool_buffer_view(first);
  second_bytes = ctool_buffer_view(second);
  if (first_bytes.size != second_bytes.size ||
      memcmp(first_bytes.data, second_bytes.data,
             (size_t)first_bytes.size) != 0) {
    (void)fprintf(stderr, "wide value objects are not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/wide-value-object.o");
  object_source.contents = second_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read wide value object") ||
      !validate_wide_value_object(job, &object) ||
      !expect_object_failure_preserves_unit(
          job, &atomic_unit, failure, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide atomic object")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (failure != (ctool_buffer_t *)0) {
    ctool_buffer_close(failure);
  }
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("wide-objects: ok");
    return 0;
  }
  return 1;
}

static int validate_wide_return_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_symbol_t *integer_width =
      find_symbol(object, "integer_width");
  const ctool_elf32_symbol_t *integer_mask =
      find_symbol(object, "integer_mask");
  const ctool_elf32_symbol_t *wide_literal =
      find_symbol(object, "wide_literal");
  const ctool_elf32_symbol_t *direct_relay =
      find_symbol(object, "direct_relay");
  const ctool_elf32_symbol_t *indirect_relay =
      find_symbol(object, "indirect_relay");
  const ctool_elf32_symbol_t *discard_mask =
      find_symbol(object, "discard_mask");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  ctool_u32 pc_relative_relocations = 0u;
  ctool_u32 absolute_relocations = 0u;
  ctool_u32 index;
  ctool_u32 low;
  ctool_u32 high;
  if (text == NULL || rel_text == NULL || text->contents.size != 439u ||
      structure_text_fingerprint(text->contents) != 0x181ca1ecu ||
      text->relocation_first != 0u || text->relocation_count != 4u ||
      object->relocation_count != 4u || object->relocations == NULL ||
      object->symbol_count != 7u || object->symbols == NULL) {
    (void)fprintf(stderr, "wide object inventory differs\n");
    return 0;
  }
  for (index = 0u; index < object->relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[index];
    if (relocation->relocation_section_file_index != rel_text->file_index ||
        relocation->target_section_file_index != text->file_index ||
        relocation->addend_known != CTOOL_TRUE) {
      return 0;
    }
    if (relocation->type == CTOOL_ELF32_R_386_PC32 &&
        relocation->addend == -4) {
      pc_relative_relocations++;
    } else if (relocation->type == CTOOL_ELF32_R_386_32 &&
               relocation->addend == 0) {
      absolute_relocations++;
    } else {
      return 0;
    }
  }
  if (pc_relative_relocations != 3u || absolute_relocations != 1u ||
      !wide_function_symbol_is_valid(object, text, integer_width) ||
      !wide_function_symbol_is_valid(object, text, integer_mask) ||
      !wide_function_symbol_is_valid(object, text, wide_literal) ||
      !wide_function_symbol_is_valid(object, text, direct_relay) ||
      !wide_function_symbol_is_valid(object, text, indirect_relay) ||
      !wide_function_symbol_is_valid(object, text, discard_mask) ||
      !validate_wide_snapshot_function(job, text, integer_mask, 4u, 0u, 1u,
                                       0, "integer_mask") ||
      !validate_wide_snapshot_function(job, text, wide_literal, 2u, 0u, 1u,
                                       1, "wide_literal") ||
      !validate_wide_snapshot_function(job, text, direct_relay, 1u, 1u, 1u,
                                       0, "direct_relay") ||
      !validate_wide_snapshot_function(job, text, indirect_relay, 1u, 1u,
                                       1u, 0, "indirect_relay") ||
      !validate_wide_snapshot_function(job, text, discard_mask, 1u, 1u, 0u,
                                       0, "discard_mask") ||
      !wide_oracle_execute(job, object, text, integer_mask, 0u, &low,
                           &high) ||
      low != 0xffffffffu || high != 0u ||
      !wide_oracle_execute(job, object, text, integer_mask, 1u, &low,
                           &high) ||
      low != 0xffffffffu || high != 0xffffffffu ||
      !wide_oracle_execute(job, object, text, wide_literal, 0u, &low,
                           &high) ||
      low != 0x55667788u || high != 0x11223344u ||
      !wide_oracle_execute(job, object, text, direct_relay, 0u, &low,
                           &high) ||
      low != 0xffffffffu || high != 0u ||
      !wide_oracle_execute(job, object, text, direct_relay, 1u, &low,
                           &high) ||
      low != 0xffffffffu || high != 0xffffffffu ||
      !wide_oracle_execute(job, object, text, indirect_relay, 0u, &low,
                           &high) ||
      low != 0xffffffffu || high != 0u ||
      !wide_oracle_execute(job, object, text, indirect_relay, 1u, &low,
                           &high) ||
      low != 0xffffffffu || high != 0xffffffffu ||
      !validate_call_alignment_function(job, text, integer_mask, 1u, 0u, 1u,
                                        1u, 0u, "integer_mask alignment") ||
      !validate_call_alignment_function(job, text, direct_relay, 1u, 0u, 0u,
                                        0u, 0u, "direct_relay alignment") ||
      !validate_call_alignment_function(job, text, indirect_relay, 1u, 0u,
                                        0u, 0u, 0u,
                                        "indirect_relay alignment") ||
      !validate_call_alignment_function(job, text, discard_mask, 1u, 0u, 0u,
                                        0u, 0u, "discard_mask alignment")) {
    return 0;
  }
  return 1;
}

static int validate_wide_parameter_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u32 relocation_callers[] = {2u, 3u, 4u, 6u, 8u};
  static const ctool_u32 relocation_targets[] = {0u, 1u, 1u, 5u, 7u};
  static const ctool_elf32_relocation_type_t relocation_types[] = {
      CTOOL_ELF32_R_386_PC32, CTOOL_ELF32_R_386_PC32,
      CTOOL_ELF32_R_386_32, CTOOL_ELF32_R_386_PC32,
      CTOOL_ELF32_R_386_PC32};
  static const int relocation_addends[] = {-4, -4, 0, -4, -4};
  static const struct {
    const char *name;
    ctool_u32 copies;
    ctool_u32 zeroes;
    ctool_u32 calls;
  } expected[] = {
      {"pass_wide", 1u, 0u, 0u},
      {"choose_wide", 2u, 0u, 0u},
      {"call_wide", 2u, 1u, 1u},
      {"call_mixed", 4u, 1u, 1u},
      {"call_indirect", 4u, 1u, 1u},
      {"variadic_named", 1u, 0u, 0u},
      {"call_variadic_named", 2u, 1u, 1u},
      {"read_after_wide", 0u, 0u, 0u},
      {"call_read_after_wide", 2u, 1u, 1u}};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *functions[
      sizeof(expected) / sizeof(expected[0])];
  const ctool_u32 first_low = 0x55667788u;
  const ctool_u32 first_high = 0x11223344u;
  const ctool_u32 second_low = 0xeeff0011u;
  const ctool_u32 second_high = 0xaabbccddu;
  const ctool_u32 pass_arguments[] = {first_low, first_high};
  const ctool_u32 choose_first_arguments[] = {
      0u, first_low, first_high, 0x12345678u,
      second_low, second_high};
  const ctool_u32 choose_second_arguments[] = {
      1u, first_low, first_high, 0x87654321u,
      second_low, second_high};
  const ctool_u32 mixed_arguments[] = {
      first_low, first_high, second_low, second_high};
  const ctool_u32 indirect_first_arguments[] = {
      0u, first_low, first_high, second_low, second_high};
  const ctool_u32 indirect_second_arguments[] = {
      1u, first_low, first_high, second_low, second_high};
  const ctool_u32 variadic_named_arguments[] = {
      first_low, first_high, 0x2468ace0u};
  ctool_u32 low;
  ctool_u32 high;
  ctool_u32 index;
  if (job == NULL || object == NULL || text == NULL || rel_text == NULL ||
      text->contents.data == NULL || text->contents.size == 0u ||
      object->symbol_count != 10u || object->symbols == NULL ||
      object->relocation_count != 5u || object->relocations == NULL ||
      text->relocation_count != 5u) {
    (void)fprintf(stderr, "wide parameter object inventory differs\n");
    return 0;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(expected) / sizeof(expected[0]));
       index++) {
    functions[index] = find_symbol(object, expected[index].name);
    if (!wide_function_symbol_is_valid(object, text, functions[index]) ||
        !validate_wide_value_function(
            job, text, functions[index], expected[index].copies,
            expected[index].zeroes) ||
        !validate_call_alignment_function(
            job, text, functions[index], expected[index].calls, 0u, 0u, 0u,
            0u, expected[index].name)) {
      (void)fprintf(stderr, "wide parameter function %s differs\n",
                    expected[index].name);
      return 0;
    }
  }
  for (index = 0u; index < object->relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[index];
    const ctool_elf32_symbol_t *caller =
        functions[relocation_callers[index]];
    const ctool_elf32_symbol_t *expected_target =
        functions[relocation_targets[index]];
    const ctool_elf32_symbol_t *target = wide_oracle_symbol_by_file_index(
        object, relocation->symbol_file_index);
    if (relocation->relocation_section_file_index != rel_text->file_index ||
        relocation->entry_index != index ||
        relocation->target_section_file_index != text->file_index ||
        relocation->addend_known != CTOOL_TRUE || caller->size < 4u ||
        relocation->offset < caller->value ||
        relocation->offset - caller->value > caller->size - 4u ||
        target == NULL || target->file_index != expected_target->file_index ||
        target->section_file_index != text->file_index ||
        target->type != CTOOL_ELF32_SYMBOL_FUNCTION ||
        relocation->type != (ctool_u32)relocation_types[index] ||
        relocation->addend != relocation_addends[index]) {
      (void)fprintf(stderr,
                    "wide parameter relocation %u caller/target differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  return wide_oracle_execute_arguments(
                     job, object, text, functions[0], pass_arguments,
                     (ctool_u32)(sizeof(pass_arguments) /
                                 sizeof(pass_arguments[0])),
                     &low, &high) &&
                 low == first_low && high == first_high &&
                 wide_oracle_execute_arguments(
                     job, object, text, functions[7],
                     variadic_named_arguments,
                     (ctool_u32)(sizeof(variadic_named_arguments) /
                                 sizeof(variadic_named_arguments[0])),
                     &low, &high) &&
                 low == 0x2468ace0u &&
                 wide_oracle_execute_arguments(
                     job, object, text, functions[8], pass_arguments,
                     (ctool_u32)(sizeof(pass_arguments) /
                                 sizeof(pass_arguments[0])),
                     &low, &high) &&
                 low == 0x13579bdfu &&
                 wide_oracle_execute_arguments(
                     job, object, text, functions[1], choose_first_arguments,
                     (ctool_u32)(sizeof(choose_first_arguments) /
                                 sizeof(choose_first_arguments[0])),
                     &low, &high) &&
                 low == first_low && high == first_high &&
                 wide_oracle_execute_arguments(
                     job, object, text, functions[1], choose_second_arguments,
                     (ctool_u32)(sizeof(choose_second_arguments) /
                                 sizeof(choose_second_arguments[0])),
                     &low, &high) &&
                 low == second_low && high == second_high &&
                 wide_oracle_execute_arguments(
                     job, object, text, functions[5],
                     variadic_named_arguments,
                     (ctool_u32)(sizeof(variadic_named_arguments) /
                                 sizeof(variadic_named_arguments[0])),
                     &low, &high) &&
                 low == first_low && high == first_high &&
                 wide_oracle_execute_arguments(
                     job, object, text, functions[6], pass_arguments,
                     (ctool_u32)(sizeof(pass_arguments) /
                                 sizeof(pass_arguments[0])),
                     &low, &high) &&
                 low == first_low && high == first_high &&
                 wide_oracle_execute_arguments(
                     job, object, text, functions[2], pass_arguments,
                     (ctool_u32)(sizeof(pass_arguments) /
                                 sizeof(pass_arguments[0])),
                     &low, &high) &&
                 low == first_low && high == first_high &&
                 wide_oracle_execute_arguments(
                     job, object, text, functions[3], mixed_arguments,
                     (ctool_u32)(sizeof(mixed_arguments) /
                                 sizeof(mixed_arguments[0])),
                     &low, &high) &&
                 low == second_low && high == second_high &&
                 wide_oracle_execute_arguments(
                     job, object, text, functions[4],
                     indirect_first_arguments,
                     (ctool_u32)(sizeof(indirect_first_arguments) /
                                 sizeof(indirect_first_arguments[0])),
                     &low, &high) &&
                 low == first_low && high == first_high &&
                 wide_oracle_execute_arguments(
                     job, object, text, functions[4],
                     indirect_second_arguments,
                     (ctool_u32)(sizeof(indirect_second_arguments) /
                                 sizeof(indirect_second_arguments[0])),
                     &low, &high) &&
                 low == second_low && high == second_high
             ? 1
             : 0;
}

static int expect_wide_oracle_result(
    ctool_job_t *job, const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *text, const char *symbol_name,
    const ctool_u32 *arguments, ctool_u32 argument_count,
    ctool_u32 expected_low, ctool_u32 expected_high,
    const char *context) {
  const ctool_elf32_symbol_t *symbol = find_symbol(object, symbol_name);
  ctool_u32 low;
  ctool_u32 high;
  if (!wide_function_symbol_is_valid(object, text, symbol) ||
      !wide_oracle_execute_arguments(job, object, text, symbol, arguments,
                                     argument_count, &low, &high) ||
      low != expected_low || high != expected_high) {
    (void)fprintf(stderr, "%s: wide execution result differs\n", context);
    return 0;
  }
  return 1;
}

static int expect_wide_oracle_low_result(
    ctool_job_t *job, const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *text, const char *symbol_name,
    const ctool_u32 *arguments, ctool_u32 argument_count,
    ctool_u32 expected_low, const char *context) {
  const ctool_elf32_symbol_t *symbol = find_symbol(object, symbol_name);
  ctool_u32 low;
  ctool_u32 ignored_high;
  if (!wide_function_symbol_is_valid(object, text, symbol) ||
      !wide_oracle_execute_arguments(job, object, text, symbol, arguments,
                                     argument_count, &low, &ignored_high) ||
      low != expected_low) {
    (void)fprintf(stderr, "%s: scalar execution result differs\n", context);
    return 0;
  }
  return 1;
}

static int validate_wide_conversion_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_u32 widen_unsigned_arguments[] = {0x89abcdefu};
  const ctool_u32 widen_signed_arguments[] = {0x80000001u};
  const ctool_u32 widen_byte_arguments[] = {0x000000ffu};
  const ctool_u32 widen_word_arguments[] = {0xffff8001u};
  const ctool_u32 cast_signed_byte_arguments[] = {0xffffff80u};
  const ctool_u32 cast_unsigned_word_arguments[] = {0x0000ffffu};
  const ctool_u32 retype_assignment_arguments[] = {
      0x89abcdefu, 0x81234567u};
  const ctool_u32 narrow_unsigned_arguments[] = {
      0x89abcdefu, 0x81234567u};
  const ctool_u32 narrow_byte_arguments[] = {0x123400ffu, 0x81234567u};
  const ctool_u32 narrow_word_arguments[] = {0x1234ffffu, 0x81234567u};
  const ctool_u32 narrow_assignment_arguments[] = {
      0x12348001u, 0x81234567u};
  const ctool_u32 narrow_signed_arguments[] = {0x00000080u, 0xffffffffu};
  const ctool_u32 narrow_signed_word_arguments[] = {
      0x00008000u, 0xffffffffu};
  const ctool_u32 bool_zero_arguments[] = {0u, 0u};
  const ctool_u32 bool_low_arguments[] = {1u, 0u};
  const ctool_u32 bool_high_arguments[] = {0u, 1u};
  if (job == NULL || object == NULL || text == NULL ||
      text->contents.data == NULL || text->contents.size == 0u ||
      object->relocation_count != 0u || text->relocation_count != 0u) {
    (void)fprintf(stderr, "wide conversion object inventory differs\n");
    return 0;
  }
  return expect_wide_oracle_result(
             job, object, text, "widen_unsigned", widen_unsigned_arguments,
             1u, 0x89abcdefu, 0u, "unsigned wide conversion") &&
         expect_wide_oracle_result(
             job, object, text, "widen_signed", widen_signed_arguments, 1u,
             0x80000001u, 0xffffffffu, "signed wide conversion") &&
         expect_wide_oracle_result(
             job, object, text, "widen_byte", widen_byte_arguments, 1u,
             0x000000ffu, 0u, "unsigned byte widening") &&
         expect_wide_oracle_result(
             job, object, text, "widen_word", widen_word_arguments, 1u,
             0xffff8001u, 0xffffffffu, "signed word widening") &&
         expect_wide_oracle_result(
             job, object, text, "cast_signed_byte",
             cast_signed_byte_arguments, 1u, 0xffffff80u, 0xffffffffu,
             "explicit signed byte widening") &&
         expect_wide_oracle_result(
             job, object, text, "cast_unsigned_word",
             cast_unsigned_word_arguments, 1u, 0x0000ffffu, 0u,
             "explicit unsigned word widening") &&
         expect_wide_oracle_result(
             job, object, text, "retype_assignment",
             retype_assignment_arguments, 2u, 0x89abcdefu, 0x81234567u,
             "wide assignment retyping") &&
         expect_wide_oracle_low_result(
             job, object, text, "narrow_unsigned", narrow_unsigned_arguments,
             2u, 0x89abcdefu, "unsigned word narrowing") &&
         expect_wide_oracle_low_result(
             job, object, text, "narrow_byte", narrow_byte_arguments, 2u,
             0x000000ffu, "unsigned byte narrowing") &&
         expect_wide_oracle_low_result(
             job, object, text, "narrow_word", narrow_word_arguments, 2u,
             0x0000ffffu, "unsigned short narrowing") &&
         expect_wide_oracle_low_result(
             job, object, text, "narrow_assignment",
             narrow_assignment_arguments, 2u, 0x00008001u,
             "implicit unsigned short narrowing") &&
         expect_wide_oracle_low_result(
             job, object, text, "narrow_signed", narrow_signed_arguments, 2u,
             0xffffff80u, "signed byte narrowing") &&
         expect_wide_oracle_low_result(
             job, object, text, "narrow_signed_word",
             narrow_signed_word_arguments, 2u, 0xffff8000u,
             "signed short narrowing") &&
         expect_wide_oracle_low_result(
             job, object, text, "narrow_bool", bool_zero_arguments, 2u, 0u,
             "zero Boolean narrowing") &&
         expect_wide_oracle_low_result(
             job, object, text, "narrow_bool", bool_low_arguments, 2u, 1u,
             "low-word Boolean narrowing") &&
         expect_wide_oracle_low_result(
             job, object, text, "narrow_bool", bool_high_arguments, 2u, 1u,
             "high-word Boolean narrowing");
}

static int validate_wide_shift_branches(
    ctool_job_t *job, const ctool_elf32_section_t *text,
    const ctool_elf32_symbol_t *symbol, const char *context) {
  ctool_u32 cursor = 0u;
  ctool_u32 zero_branches = 0u;
  ctool_u32 repeat_branches = 0u;
  if (job == NULL || text == NULL || symbol == NULL || context == NULL ||
      symbol->value > text->contents.size ||
      symbol->size > text->contents.size - symbol->value) {
    return 0;
  }
  while (cursor < symbol->size) {
    ctool_x86_decoded_t decoded;
    ctool_bytes_t remaining = ctool_bytes(
        text->contents.data + symbol->value + cursor,
        symbol->size - cursor);
    ctool_u32 target;
    ctool_status_t status;
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                              &decoded);
    if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
        decoded.consumed == 0u) {
      return 0;
    }
    if (decoded.instruction.mnemonic == CTOOL_X86_MN_JE ||
        decoded.instruction.mnemonic == CTOOL_X86_MN_JNE) {
      if (!call_alignment_branch_target(
              &decoded, symbol->value + cursor, text->contents.size,
              &target) ||
          target < symbol->value || target >= symbol->value + symbol->size) {
        (void)fprintf(stderr, "%s: branch leaves its function\n", context);
        return 0;
      }
      if (decoded.instruction.mnemonic == CTOOL_X86_MN_JE) {
        zero_branches++;
      } else {
        repeat_branches++;
      }
    }
    cursor += decoded.consumed;
  }
  return cursor == symbol->size && zero_branches == 1u &&
                 repeat_branches == 1u
             ? 1
             : 0;
}

static int validate_wide_operation_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u8 extracted[] = {
      0xefu, 0xcdu, 0xabu, 0x89u, 0x67u, 0x45u, 0x23u, 0x81u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_symbol_t *shift_left =
      find_symbol(object, "shift_left");
  const ctool_elf32_symbol_t *shift_right_unsigned =
      find_symbol(object, "shift_right_unsigned");
  const ctool_elf32_symbol_t *shift_right_signed =
      find_symbol(object, "shift_right_signed");
  const ctool_elf32_symbol_t *extract_byte =
      find_symbol(object, "extract_byte");
  const ctool_elf32_symbol_t *add_unsigned =
      find_symbol(object, "add_unsigned");
  const ctool_elf32_symbol_t *add_signed =
      find_symbol(object, "add_signed");
  const ctool_elf32_symbol_t *subtract_unsigned =
      find_symbol(object, "subtract_unsigned");
  const ctool_elf32_symbol_t *subtract_signed =
      find_symbol(object, "subtract_signed");
  const ctool_elf32_symbol_t *chained_add_subtract =
      find_symbol(object, "chained_add_subtract");
  const ctool_elf32_symbol_t *plus_unsigned =
      find_symbol(object, "plus_unsigned");
  const ctool_elf32_symbol_t *negate_unsigned =
      find_symbol(object, "negate_unsigned");
  const ctool_elf32_symbol_t *not_unsigned =
      find_symbol(object, "not_unsigned");
  const ctool_elf32_symbol_t *pp_if_signed_magnitude =
      find_symbol(object, "pp_if_signed_magnitude");
  const ctool_u32 bitwise_arguments[] = {
      0x89abcdefu, 0x81234567u, 0xaa55aa55u, 0x0ff00ff0u};
  const ctool_u32 add_arguments[] = {
      0xffffffffu, 0x11223344u, 1u, 0u};
  const ctool_u32 add_full_width_arguments[] = {
      0x50607080u, 0x10203040u, 0x05060708u, 0x01020304u};
  const ctool_u32 add_signed_arguments[] = {
      0xfffffffeu, 0xffffffffu, 1u, 0u};
  const ctool_u32 subtract_arguments[] = {
      0u, 0x11223345u, 1u, 0u};
  const ctool_u32 subtract_wrap_arguments[] = {0u, 0u, 1u, 0u};
  const ctool_u32 subtract_signed_arguments[] = {0u, 1u, 1u, 0u};
  const ctool_u32 subtract_signed_negative_arguments[] = {
      0xffffffffu, 0xffffffffu, 1u, 0u};
  const ctool_u32 chained_arguments[] = {
      0x50607080u, 0x10203040u, 0x05060708u, 0x01020304u};
  const ctool_u32 unary_zero_arguments[] = {0u, 0u};
  const ctool_u32 unary_full_arguments[] = {
      0x89abcdefu, 0x81234567u};
  const ctool_u32 unary_sign_boundary_arguments[] = {0u, 0x80000000u};
  const ctool_u32 unary_carry_arguments[] = {0xffffffffu, 0u};
  const ctool_u32 unary_signed_arguments[] = {0u, 0xffffffffu};
  const ctool_u32 magnitude_positive_arguments[] = {
      0x89abcdefu, 0x01234567u};
  const ctool_u32 magnitude_negative_one_arguments[] = {
      0xffffffffu, 0xffffffffu};
  const ctool_u32 magnitude_boundary_arguments[] = {0u, 0x80000000u};
  ctool_u32 shift_arguments[] = {0x89abcdefu, 0x81234567u, 0u};
  const uint64_t value = UINT64_C(0x8123456789abcdef);
  ctool_u32 index;
  if (job == NULL || object == NULL || text == NULL ||
      text->contents.data == NULL || text->contents.size != 3156u ||
      structure_text_fingerprint(text->contents) != 0xb52392eau ||
      object->symbol_count != 26u || object->relocation_count != 0u ||
      text->relocation_count != 0u) {
    (void)fprintf(stderr, "wide operation object inventory differs\n");
    return 0;
  }
  if (!wide_function_symbol_is_valid(object, text, shift_left) ||
      !wide_function_symbol_is_valid(object, text,
                                     shift_right_unsigned) ||
      !wide_function_symbol_is_valid(object, text, shift_right_signed) ||
      !wide_function_symbol_is_valid(object, text, add_unsigned) ||
      !wide_function_symbol_is_valid(object, text, add_signed) ||
      !wide_function_symbol_is_valid(object, text, subtract_unsigned) ||
      !wide_function_symbol_is_valid(object, text, subtract_signed) ||
      !wide_function_symbol_is_valid(object, text,
                                     chained_add_subtract) ||
      !wide_function_symbol_is_valid(object, text, plus_unsigned) ||
      !wide_function_symbol_is_valid(object, text, negate_unsigned) ||
      !wide_function_symbol_is_valid(object, text, not_unsigned) ||
      !wide_function_symbol_is_valid(object, text,
                                     pp_if_signed_magnitude) ||
      !wide_function_symbol_is_valid(object, text, extract_byte) ||
      !validate_wide_shift_branches(
          job, text, shift_left, "wide left shift") ||
      !validate_wide_shift_branches(
          job, text, shift_right_unsigned, "unsigned wide right shift") ||
      !validate_wide_shift_branches(
          job, text, shift_right_signed, "signed wide right shift") ||
      !validate_wide_shift_branches(
          job, text, extract_byte, "wide byte extraction")) {
    return 0;
  }
  if (!validate_wide_snapshot_function(
          job, text, plus_unsigned, 0u, 0u, 1u, 0,
          "wide unary plus snapshot") ||
      !validate_wide_snapshot_function(
          job, text, negate_unsigned, 1u, 1u, 2u, 0,
          "wide unary negation snapshot") ||
      !validate_wide_snapshot_function(
          job, text, not_unsigned, 1u, 1u, 2u, 0,
          "wide bitwise complement snapshot")) {
    return 0;
  }
  for (index = 0u; index < 64u; index++) {
    uint64_t expected_left = value << index;
    uint64_t expected_unsigned = value >> index;
    uint64_t expected_signed =
        index == 0u
            ? value
            : (value >> index) |
                  (~UINT64_C(0) << (64u - index));
    shift_arguments[2] = index;
    if (!expect_wide_oracle_result(
            job, object, text, "shift_left", shift_arguments, 3u,
            (ctool_u32)expected_left,
            (ctool_u32)(expected_left >> 32u), "wide left shift") ||
        !expect_wide_oracle_result(
            job, object, text, "shift_right_unsigned", shift_arguments, 3u,
            (ctool_u32)expected_unsigned,
            (ctool_u32)(expected_unsigned >> 32u),
            "unsigned wide right shift") ||
        !expect_wide_oracle_result(
            job, object, text, "shift_right_signed", shift_arguments, 3u,
            (ctool_u32)expected_signed,
            (ctool_u32)(expected_signed >> 32u),
            "signed wide right shift")) {
      return 0;
    }
  }
  if (!expect_wide_oracle_result(
          job, object, text, "bitwise_and", bitwise_arguments, 4u,
          0x88018845u, 0x01200560u, "wide bitwise AND") ||
      !expect_wide_oracle_result(
          job, object, text, "bitwise_mixed", bitwise_arguments, 4u,
          0x88018845u, 0x01200560u,
          "mixed-signedness wide bitwise AND") ||
      !expect_wide_oracle_result(
          job, object, text, "bitwise_enum", bitwise_arguments, 4u,
          0x88018845u, 0x01200560u,
          "wide enum bitwise AND") ||
      !expect_wide_oracle_result(
          job, object, text, "bitwise_or", bitwise_arguments, 4u,
          0xabffefffu, 0x8ff34ff7u, "wide bitwise OR") ||
      !expect_wide_oracle_result(
          job, object, text, "bitwise_xor", bitwise_arguments, 4u,
          0x23fe67bau, 0x8ed34a97u, "wide bitwise XOR") ||
      !expect_wide_oracle_result(
          job, object, text, "add_unsigned", add_arguments, 4u,
          0u, 0x11223345u, "unsigned wide addition carry") ||
      !expect_wide_oracle_result(
          job, object, text, "add_unsigned", add_full_width_arguments, 4u,
          0x55667788u, 0x11223344u,
          "unsigned wide addition full-width result") ||
      !expect_wide_oracle_result(
          job, object, text, "add_signed", add_signed_arguments, 4u,
          0xffffffffu, 0xffffffffu, "signed wide addition") ||
      !expect_wide_oracle_result(
          job, object, text, "subtract_unsigned", subtract_arguments, 4u,
          0xffffffffu, 0x11223344u,
          "unsigned wide subtraction borrow") ||
      !expect_wide_oracle_result(
          job, object, text, "subtract_unsigned", subtract_wrap_arguments,
          4u, 0xffffffffu, 0xffffffffu,
          "unsigned wide subtraction wrap") ||
      !expect_wide_oracle_result(
          job, object, text, "subtract_signed", subtract_signed_arguments,
          4u, 0xffffffffu, 0u, "signed wide subtraction borrow") ||
      !expect_wide_oracle_result(
          job, object, text, "subtract_signed",
          subtract_signed_negative_arguments, 4u, 0xfffffffeu,
          0xffffffffu, "signed wide subtraction negative result") ||
      !expect_wide_oracle_result(
          job, object, text, "chained_add_subtract", chained_arguments, 4u,
          0x05060708u, 0x01020304u,
          "fresh wide arithmetic snapshots") ||
      !expect_wide_oracle_result(
          job, object, text, "plus_unsigned", unary_full_arguments, 2u,
          0x89abcdefu, 0x81234567u, "unsigned wide unary plus") ||
      !expect_wide_oracle_result(
          job, object, text, "plus_signed", unary_sign_boundary_arguments,
          2u, 0u, 0x80000000u, "signed wide unary plus boundary") ||
      !expect_wide_oracle_result(
          job, object, text, "negate_unsigned", unary_zero_arguments, 2u,
          0u, 0u, "unsigned wide negation zero") ||
      !expect_wide_oracle_result(
          job, object, text, "negate_unsigned", unary_carry_arguments, 2u,
          1u, 0xffffffffu, "unsigned wide negation carry") ||
      !expect_wide_oracle_result(
          job, object, text, "negate_unsigned",
          unary_sign_boundary_arguments, 2u, 0u, 0x80000000u,
          "unsigned wide negation boundary") ||
      !expect_wide_oracle_result(
          job, object, text, "negate_signed", unary_signed_arguments, 2u,
          0u, 1u, "signed wide negation") ||
      !expect_wide_oracle_result(
          job, object, text, "not_unsigned", unary_zero_arguments, 2u,
          0xffffffffu, 0xffffffffu, "unsigned wide bitwise complement") ||
      !expect_wide_oracle_result(
          job, object, text, "not_signed", unary_sign_boundary_arguments,
          2u, 0xffffffffu, 0x7fffffffu,
          "signed wide bitwise complement boundary") ||
      !expect_wide_oracle_result(
          job, object, text, "negate_twice_unsigned", unary_full_arguments,
          2u, 0x89abcdefu, 0x81234567u,
          "unsigned wide negation involution") ||
      !expect_wide_oracle_result(
          job, object, text, "not_twice_signed", unary_full_arguments, 2u,
          0x89abcdefu, 0x81234567u,
          "signed wide complement involution") ||
      !expect_wide_oracle_result(
          job, object, text, "negate_then_add_unsigned",
          unary_full_arguments, 2u, 0u, 0u,
          "fresh wide negation result") ||
      !expect_wide_oracle_result(
          job, object, text, "not_then_xor_unsigned",
          unary_full_arguments, 2u, 0xffffffffu, 0xffffffffu,
          "fresh wide complement result") ||
      !expect_wide_oracle_result(
          job, object, text, "pp_if_signed_magnitude",
          magnitude_positive_arguments, 2u, 0x89abcdefu, 0x01234567u,
          "active positive magnitude") ||
      !expect_wide_oracle_result(
          job, object, text, "pp_if_signed_magnitude",
          magnitude_negative_one_arguments, 2u, 1u, 0u,
          "active negative-one magnitude") ||
      !expect_wide_oracle_result(
          job, object, text, "pp_if_signed_magnitude",
          magnitude_boundary_arguments, 2u, 0u, 0x80000000u,
          "active signed-boundary magnitude")) {
    return 0;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(extracted) / sizeof(extracted[0]));
       index++) {
    shift_arguments[2] = index;
    if (!expect_wide_oracle_low_result(
            job, object, text, "extract_byte", shift_arguments, 3u,
            (ctool_u32)extracted[index], "wide byte extraction")) {
      return 0;
    }
  }
  return 1;
}

static int validate_wide_multiplication_object(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const char *const function_names[] = {
      "multiply_unsigned",       "multiply_signed",
      "multiply_mixed",          "multiply_wide_narrow",
      "multiply_chained",        "multiply_then_reuse_left"};
  static const ctool_u32 function_sizes[] = {
      154u, 154u, 154u, 158u, 259u, 224u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  ctool_u32 fingerprint =
      text == NULL ? 0u : structure_text_fingerprint(text->contents);
  const ctool_u32 zero_arguments[] = {
      0x89abcdefu, 0x01234567u, 0u, 0u};
  const ctool_u32 identity_arguments[] = {
      0x89abcdefu, 0x01234567u, 1u, 0u};
  const ctool_u32 carry_arguments[] = {
      0xffffffffu, 0u, 0xffffffffu, 0u};
  const ctool_u32 cross_word_arguments[] = {0u, 1u, 2u, 0u};
  const ctool_u32 high_bit_arguments[] = {0u, 0x80000000u, 2u, 0u};
  const ctool_u32 signed_negative_arguments[] = {
      0xfffffffdu, 0xffffffffu, 7u, 0u};
  const ctool_u32 signed_boundary_arguments[] = {
      0u, 0x80000000u, 1u, 0u};
  const ctool_u32 signed_both_negative_arguments[] = {
      0xfffffffdu, 0xffffffffu, 0xfffffff9u, 0xffffffffu};
  const ctool_u32 wide_narrow_arguments[] = {1u, 1u, 3u};
  const ctool_u32 chained_arguments[] = {
      1u, 1u, 0xffffffffu, 0u, 2u, 0u};
  const ctool_u32 reused_arguments[] = {
      1u, 1u, 0xffffffffu, 0u};
  ctool_u32 expected_offset = 0u;
  ctool_u32 multiply_count = 0u;
  ctool_u32 signed_multiply_count = 0u;
  ctool_u32 call_count = 0u;
  ctool_u32 divide_count = 0u;
  ctool_u32 return_count = 0u;
  ctool_u32 index;
  if (job == NULL || object == NULL || text == NULL ||
      text->type != CTOOL_ELF32_SHT_PROGBITS ||
      text->flags != (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR) ||
      text->alignment != 1u || text->contents.data == NULL ||
      text->contents.size != 1103u || fingerprint != 0xe357be84u ||
      text->relocation_count != 0u ||
      object->relocation_count != 0u || object->symbol_count != 7u) {
    (void)fprintf(stderr,
                  "wide multiplication ELF inventory differs: text=%u "
                  "fingerprint=%08x symbols=%u relocations=%u\n",
                  text == NULL ? 0u : text->contents.size,
                  fingerprint,
                  object == NULL ? 0u : object->symbol_count,
                  object == NULL ? 0u : object->relocation_count);
    return 0;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(function_names) /
                           sizeof(function_names[0]));
       index++) {
    const ctool_elf32_symbol_t *function =
        find_symbol(object, function_names[index]);
    ctool_u32 cursor = 0u;
    ctool_x86_mnemonic_t last = CTOOL_X86_MN_INVALID;
    if (function == NULL || function->size != function_sizes[index] ||
        !symbol_matches(function, index + 1u, CTOOL_ELF32_BIND_GLOBAL,
                        CTOOL_ELF32_SYMBOL_FUNCTION,
                        CTOOL_ELF32_SYMBOL_DEFINED, text->file_index,
                        expected_offset, function_sizes[index])) {
      (void)fprintf(stderr,
                    "wide multiplication symbol %s differs at %u\n",
                    function_names[index], (unsigned int)expected_offset);
      return 0;
    }
    while (cursor < function->size) {
      ctool_x86_decoded_t decoded;
      const ctool_x86_instruction_t *instruction;
      ctool_bytes_t remaining = ctool_bytes(
          text->contents.data + function->value + cursor,
          function->size - cursor);
      ctool_status_t status;
      (void)memset(&decoded, 0xa5, sizeof(decoded));
      status = ctool_x86_decode(job, CTOOL_X86_MODE_32, remaining, 0u,
                                &decoded);
      if (status != CTOOL_OK || decoded.kind != CTOOL_X86_DECODE_KNOWN ||
          decoded.consumed == 0u) {
        (void)fprintf(stderr,
                      "wide multiplication decode failed in %s at %u\n",
                      function_names[index], (unsigned int)cursor);
        return 0;
      }
      instruction = &decoded.instruction;
      last = instruction->mnemonic;
      if (instruction->mnemonic == CTOOL_X86_MN_MUL) {
        if (instruction->operand_count != 1u ||
            instruction->operands[0].width_bits != 32u) {
          (void)fprintf(stderr,
                        "wide multiplication MUL form differs in %s\n",
                        function_names[index]);
          return 0;
        }
        multiply_count++;
      } else if (instruction->mnemonic == CTOOL_X86_MN_IMUL) {
        if (instruction->operand_count != 2u ||
            instruction->operands[0].kind !=
                CTOOL_X86_OPERAND_REGISTER ||
            instruction->operands[0].as.reg.class_id !=
                CTOOL_X86_REG_GPR32 ||
            instruction->operands[0].width_bits != 32u ||
            instruction->operands[1].width_bits != 32u) {
          (void)fprintf(stderr,
                        "wide multiplication IMUL form differs in %s\n",
                        function_names[index]);
          return 0;
        }
        signed_multiply_count++;
      } else if (instruction->mnemonic == CTOOL_X86_MN_CALL) {
        call_count++;
      } else if (instruction->mnemonic == CTOOL_X86_MN_DIV ||
                 instruction->mnemonic == CTOOL_X86_MN_IDIV) {
        divide_count++;
      } else if (instruction->mnemonic == CTOOL_X86_MN_RET) {
        return_count++;
      }
      cursor += decoded.consumed;
    }
    if (cursor != function->size || last != CTOOL_X86_MN_RET) {
      (void)fprintf(stderr,
                    "wide multiplication function %s boundary differs\n",
                    function_names[index]);
      return 0;
    }
    expected_offset += function_sizes[index];
  }
  if (expected_offset != text->contents.size || multiply_count != 7u ||
      signed_multiply_count != 14u || call_count != 0u ||
      divide_count != 0u || return_count != 6u) {
    (void)fprintf(stderr,
                  "wide multiplication instruction inventory differs: "
                  "mul=%u imul=%u call=%u divide=%u return=%u text=%u "
                  "fingerprint=%08x\n",
                  (unsigned int)multiply_count,
                  (unsigned int)signed_multiply_count,
                  (unsigned int)call_count, (unsigned int)divide_count,
                  (unsigned int)return_count,
                  (unsigned int)text->contents.size,
                  (unsigned int)fingerprint);
    return 0;
  }
  return expect_wide_oracle_result(
             job, object, text, "multiply_unsigned", zero_arguments, 4u,
             0u, 0u, "unsigned wide multiplication by zero") &&
                 expect_wide_oracle_result(
                     job, object, text, "multiply_unsigned",
                     identity_arguments, 4u, 0x89abcdefu, 0x01234567u,
                     "unsigned wide multiplication identity") &&
                 expect_wide_oracle_result(
                     job, object, text, "multiply_unsigned",
                     carry_arguments, 4u, 1u, 0xfffffffeu,
                     "unsigned wide multiplication low-word carry") &&
                 expect_wide_oracle_result(
                     job, object, text, "multiply_unsigned",
                     cross_word_arguments, 4u, 0u, 2u,
                     "unsigned wide multiplication cross word") &&
                 expect_wide_oracle_result(
                     job, object, text, "multiply_unsigned",
                     high_bit_arguments, 4u, 0u, 0u,
                     "unsigned wide multiplication wrap") &&
                 expect_wide_oracle_result(
                     job, object, text, "multiply_signed",
                     signed_negative_arguments, 4u, 0xffffffebu,
                     0xffffffffu, "signed wide multiplication negative") &&
                 expect_wide_oracle_result(
                     job, object, text, "multiply_signed",
                     signed_boundary_arguments, 4u, 0u, 0x80000000u,
                     "signed wide multiplication boundary") &&
                 expect_wide_oracle_result(
                     job, object, text, "multiply_signed",
                     signed_both_negative_arguments, 4u, 21u, 0u,
                     "signed wide multiplication positive") &&
                 expect_wide_oracle_result(
                     job, object, text, "multiply_mixed",
                     signed_negative_arguments, 4u, 0xffffffebu,
                     0xffffffffu,
                     "mixed-signedness wide multiplication") &&
                 expect_wide_oracle_result(
                     job, object, text, "multiply_wide_narrow",
                     wide_narrow_arguments, 3u, 3u, 3u,
                     "wide multiplication by represented narrow value") &&
                 expect_wide_oracle_result(
                     job, object, text, "multiply_chained",
                     chained_arguments, 6u, 0xfffffffeu, 0xffffffffu,
                     "chained wide multiplication snapshots") &&
                 expect_wide_oracle_result(
                     job, object, text, "multiply_then_reuse_left",
                     reused_arguments, 4u, 0u, 1u,
                     "wide multiplication preserves its left snapshot")
             ? 1
             : 0;
}

static int validate_active_wide_helper_object(
    const ctool_elf32_object_t *object) {
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_section_t *rel_text = find_section(object, ".rel.text");
  const ctool_elf32_symbol_t *put =
      find_symbol(object, "ctool_buffer_put_le64");
  const ctool_elf32_symbol_t *patch =
      find_symbol(object, "ctool_buffer_patch_le64");
  ctool_u32 ctool_bytes_calls = 0u;
  ctool_u32 append_calls = 0u;
  ctool_u32 patch_calls = 0u;
  ctool_u32 index;
  if (object == NULL || text == NULL || rel_text == NULL ||
      !wide_function_symbol_is_valid(object, text, put) ||
      !wide_function_symbol_is_valid(object, text, patch) ||
      object->relocation_count != 3u || text->relocation_count != 3u) {
    (void)fprintf(stderr, "active wide helper object inventory differs\n");
    return 0;
  }
  for (index = 0u; index < object->relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation = &object->relocations[index];
    const ctool_elf32_symbol_t *target = wide_oracle_symbol_by_file_index(
        object, relocation->symbol_file_index);
    const ctool_elf32_symbol_t *caller;
    if (relocation->offset >= put->value &&
        relocation->offset - put->value < put->size) {
      caller = put;
    } else if (relocation->offset >= patch->value &&
               relocation->offset - patch->value < patch->size) {
      caller = patch;
    } else {
      return 0;
    }
    if (target == NULL ||
        target->placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
        relocation->relocation_section_file_index != rel_text->file_index ||
        relocation->target_section_file_index != text->file_index ||
        relocation->type != CTOOL_ELF32_R_386_PC32 ||
        relocation->addend_known != CTOOL_TRUE || relocation->addend != -4 ||
        caller->size < 4u || relocation->offset < caller->value ||
        relocation->offset - caller->value > caller->size - 4u) {
      return 0;
    }
    if (string_equal(target->name, "ctool_bytes") != 0 && caller == put) {
      ctool_bytes_calls++;
    } else if (string_equal(target->name, "ctool_buffer_append") != 0 &&
               caller == put) {
      append_calls++;
    } else if (string_equal(target->name, "ctool_buffer_patch") != 0 &&
               caller == patch) {
      patch_calls++;
    } else {
      return 0;
    }
  }
  return ctool_bytes_calls == 1u && append_calls == 1u && patch_calls == 1u
             ? 1
             : 0;
}

static int wide_condition_result_matches(
    ctool_job_t *job, const ctool_elf32_object_t *object,
    const ctool_elf32_section_t *text, const char *name,
    const ctool_u32 *arguments, ctool_u32 argument_count,
    ctool_u32 expected) {
  const ctool_elf32_symbol_t *symbol = find_symbol(object, name);
  ctool_u32 low = 0u;
  ctool_u32 high = 0u;
  if (!wide_function_symbol_is_valid(object, text, symbol) ||
      !wide_oracle_execute_arguments(
          job, object, text, symbol, arguments, argument_count, &low, &high) ||
      low != expected) {
    (void)fprintf(stderr, "%s returned %u instead of %u\n", name,
                  (unsigned int)low, (unsigned int)expected);
    return 0;
  }
  return 1;
}

static int validate_wide_condition_execution(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const char *const signed_names[] = {
      "signed_less",          "signed_less_equal", "signed_greater",
      "signed_greater_equal", "signed_equal",      "signed_not_equal"};
  static const char *const unsigned_names[] = {
      "unsigned_less",          "unsigned_less_equal", "unsigned_greater",
      "unsigned_greater_equal", "unsigned_equal",      "unsigned_not_equal"};
  static const ctool_u32 signed_expected[][6] = {
      {1u, 1u, 0u, 0u, 0u, 1u},
      {0u, 1u, 0u, 1u, 1u, 0u},
      {1u, 1u, 0u, 0u, 0u, 1u},
      {1u, 1u, 0u, 0u, 0u, 1u}};
  static const ctool_u32 unsigned_expected[][6] = {
      {0u, 0u, 1u, 1u, 0u, 1u},
      {0u, 1u, 0u, 1u, 1u, 0u},
      {1u, 1u, 0u, 0u, 0u, 1u},
      {0u, 0u, 1u, 1u, 0u, 1u}};
  static const ctool_u32 signed_arguments[][4] = {
      {0xffffffffu, 0xffffffffu, 0u, 0u},
      {0x76543210u, 1u, 0x76543210u, 1u},
      {0u, 1u, 0xffffffffu, 1u},
      {0u, 0x80000000u, 0u, 1u}};
  static const ctool_u32 unsigned_arguments[][4] = {
      {0xffffffffu, 0xffffffffu, 0u, 0u},
      {0x76543210u, 1u, 0x76543210u, 1u},
      {0u, 1u, 0xffffffffu, 1u},
      {0u, 0x80000000u, 0u, 1u}};
  static const char *const condition_names[] = {
      "wide_not", "wide_and", "wide_or", "wide_select",
      "wide_if", "wide_while", "wide_do", "wide_for"};
  static const ctool_u32 zero_expected[] = {1u, 0u, 1u, 9u,
                                             13u, 0u, 1u, 0u};
  static const ctool_u32 high_expected[] = {0u, 1u, 0u, 7u,
                                             11u, 17u, 2u, 29u};
  static const ctool_u32 zero_argument[] = {0u, 0u};
  static const ctool_u32 high_argument[] = {0u, 1u};
  static const ctool_u32 mixed_negative[] = {
      0xffffffffu, 0xffffffffu, 1u, 0u};
  static const ctool_u32 mixed_positive[] = {
      0u, 0u, 0xffffffffu, 0xffffffffu};
  static const ctool_u32 truth_zero[] = {0u, 0u, 0u};
  static const ctool_u32 truth_high[] = {0u, 1u, 0u};
  static const ctool_u32 negative_signed[] = {0u, 0x80000000u, 0u};
  static const ctool_u32 negative_unsigned[] = {0u, 0x80000000u, 1u};
  static const ctool_u32 signed_less_arguments[] = {
      0u, 0x80000000u, 0u, 0u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  ctool_u32 operation;
  ctool_u32 vector;
  ctool_u32 fingerprint =
      text == NULL ? 0u : structure_text_fingerprint(text->contents);
  if (text == NULL || text->contents.data == NULL ||
      text->contents.size != 3341u || fingerprint != 0x16626ce1u ||
      object->symbol_count != 25u || object->relocation_count != 0u) {
    (void)fprintf(stderr,
                  "wide condition object differs: text=%u fingerprint=%08x "
                  "symbols=%u relocations=%u\n",
                  text == NULL ? 0u : text->contents.size,
                  fingerprint, object->symbol_count,
                  object->relocation_count);
    return 0;
  }
  for (vector = 0u; vector < 4u; vector++) {
    for (operation = 0u; operation < 6u; operation++) {
      if (!wide_condition_result_matches(
              job, object, text, signed_names[operation],
              signed_arguments[vector], 4u,
              signed_expected[vector][operation]) ||
          !wide_condition_result_matches(
              job, object, text, unsigned_names[operation],
              unsigned_arguments[vector], 4u,
              unsigned_expected[vector][operation])) {
        return 0;
      }
    }
  }
  if (!wide_condition_result_matches(
          job, object, text, "mixed_less", mixed_negative, 4u, 0u) ||
      !wide_condition_result_matches(
          job, object, text, "mixed_less", mixed_positive, 4u, 1u)) {
    return 0;
  }
  for (operation = 0u; operation < 8u; operation++) {
    if (!wide_condition_result_matches(
            job, object, text, condition_names[operation],
            zero_argument, 2u, zero_expected[operation]) ||
        !wide_condition_result_matches(
            job, object, text, condition_names[operation],
            high_argument, 2u, high_expected[operation])) {
      return 0;
    }
  }
  return wide_condition_result_matches(
             job, object, text, "pp_if_value_truth",
             truth_zero, 3u, 0u) &&
                 wide_condition_result_matches(
                     job, object, text, "pp_if_value_truth",
                     truth_high, 3u, 1u) &&
                 wide_condition_result_matches(
                     job, object, text, "pp_if_is_negative",
                     negative_signed, 3u, 1u) &&
                 wide_condition_result_matches(
                     job, object, text, "pp_if_is_negative",
                     negative_unsigned, 3u, 0u) &&
                 wide_condition_result_matches(
                     job, object, text, "pp_if_signed_less",
                     signed_less_arguments, 4u, 1u)
             ? 1
             : 0;
}

static int validate_wide_switch_execution(
    ctool_job_t *job, const ctool_elf32_object_t *object) {
  static const ctool_u32 signed_case_negative[] = {
      0xbaa99888u, 0xfeeddccbu};
  static const ctool_u32 signed_case_positive[] = {
      0x55667788u, 0x11223344u};
  static const ctool_u32 signed_same_low[] = {
      0x55667788u, 0x11223345u};
  static const ctool_u32 signed_same_high[] = {
      0x55667789u, 0x11223344u};
  static const ctool_u32 unsigned_case_positive[] = {
      0x55667788u, 0x11223344u};
  static const ctool_u32 unsigned_case_high_bit[] = {
      0x44332211u, 0x88776655u};
  static const ctool_u32 unsigned_same_low[] = {
      0x55667788u, 0x11223345u};
  static const ctool_u32 unsigned_same_high[] = {
      0x55667789u, 0x11223344u};
  static const ctool_u32 default_value[] = {0u, 0u};
  const ctool_elf32_section_t *text = find_section(object, ".text");
  const ctool_elf32_symbol_t *signed_switch =
      find_symbol(object, "signed_wide_switch");
  const ctool_elf32_symbol_t *unsigned_switch =
      find_symbol(object, "unsigned_wide_switch");
  ctool_u32 fingerprint =
      text == NULL ? 0u : structure_text_fingerprint(text->contents);
  if (text == NULL || text->contents.data == NULL ||
      text->contents.size != 504u || fingerprint != 0xdbc82148u ||
      object->symbol_count != 3u || object->relocation_count != 0u ||
      !symbol_matches(signed_switch, 1u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 0u,
                      252u) ||
      !symbol_matches(unsigned_switch, 2u, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 252u,
                      252u)) {
    (void)fprintf(stderr,
                  "wide switch object differs: text=%u fingerprint=%08x "
                  "symbols=%u relocations=%u\n",
                  text == NULL ? 0u : text->contents.size,
                  fingerprint, object->symbol_count,
                  object->relocation_count);
    return 0;
  }
  return wide_condition_result_matches(
             job, object, text, "signed_wide_switch",
             signed_case_negative, 2u, 11u) &&
                 wide_condition_result_matches(
                     job, object, text, "signed_wide_switch",
                     signed_case_positive, 2u, 13u) &&
                 wide_condition_result_matches(
                     job, object, text, "signed_wide_switch",
                     signed_same_low, 2u, 17u) &&
                 wide_condition_result_matches(
                     job, object, text, "signed_wide_switch",
                     signed_same_high, 2u, 17u) &&
                 wide_condition_result_matches(
                     job, object, text, "signed_wide_switch",
                     default_value, 2u, 17u) &&
                 wide_condition_result_matches(
                     job, object, text, "unsigned_wide_switch",
                     unsigned_case_positive, 2u, 19u) &&
                 wide_condition_result_matches(
                     job, object, text, "unsigned_wide_switch",
                     unsigned_case_high_bit, 2u, 23u) &&
                 wide_condition_result_matches(
                     job, object, text, "unsigned_wide_switch",
                     unsigned_same_low, 2u, 29u) &&
                 wide_condition_result_matches(
                     job, object, text, "unsigned_wide_switch",
                     unsigned_same_high, 2u, 29u) &&
                 wide_condition_result_matches(
                     job, object, text, "unsigned_wide_switch",
                     default_value, 2u, 29u)
             ? 1
             : 0;
}

static int run_wide_condition_object(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_buffer_t *failure = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_buffer_t *switch_first = (ctool_buffer_t *)0;
  ctool_buffer_t *switch_second = (ctool_buffer_t *)0;
  ctool_buffer_t *switch_limited = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t switch_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t first_bytes;
  ctool_bytes_t second_bytes;
  ctool_bytes_t switch_first_bytes;
  ctool_bytes_t switch_second_bytes;
  ctool_u32 comparison_expression = CTOOL_C_AST_NONE;
  ctool_u32 index;
  ctool_status_t status;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&switch_unit, 0, sizeof(switch_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_contains(
          job, "/toolchain/cupidc_pp.c",
          "load active preprocessor condition source",
          "the active preprocessor truth helper changed",
          active_pp_if_value_truth_object, NULL) ||
      !active_source_contains(
          job, "/toolchain/cupidc_pp.c",
          "load active preprocessor condition source",
          "the active preprocessor sign helper changed",
          active_pp_if_is_negative_object, NULL) ||
      !active_source_contains(
          job, "/toolchain/cupidc_pp.c",
          "load active preprocessor condition source",
          "the active preprocessor comparison helper changed",
          active_pp_if_signed_less_object, NULL) ||
      !parse_source(job, "/wide-condition-object.c",
                    wide_condition_object_source, &unit) ||
      !parse_source(job, "/wide-switch.c", wide_switch_object_source,
                    &switch_unit)) {
    goto cleanup;
  }
  for (index = 0u; index < unit.expression_count; index++) {
    const ctool_c_expression_t *expression = &unit.expressions[index];
    if (expression->kind == CTOOL_C_EXPRESSION_BINARY &&
        expression->operation == CTOOL_C_EXPRESSION_OPERATOR_LESS &&
        expression->child_count == 2u &&
        expression->first_child < unit.expression_child_count) {
      ctool_u32 child = unit.expression_children[expression->first_child];
      if (child < unit.expression_count &&
          unit.expressions[child].type < unit.layout.type_count &&
          unit.layout.types[unit.expressions[child].type].is_integer ==
              CTOOL_TRUE &&
          unit.layout.types[unit.expressions[child].type].size == 8u) {
        comparison_expression = index;
        break;
      }
    }
  }
  if (comparison_expression == CTOOL_C_AST_NONE ||
      unit.expression_count == 0u ||
      sizeof(*invalid_expressions) >
          SIZE_MAX / (size_t)unit.expression_count) {
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)unit.expression_count * sizeof(*invalid_expressions));
  if (invalid_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_unit = unit;
  invalid_unit.expressions = invalid_expressions;
  invalid_expressions[comparison_expression].type =
      invalid_expressions[unit.expression_children[
          invalid_expressions[comparison_expression].first_child]]
          .type;
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &failure);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 16u, 64u, &limited);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &switch_first);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &switch_second);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 16u, 64u, &switch_limited);
  }
  if (!check_status(status, CTOOL_OK, "wide condition object buffer") ||
      !expect_object_success_preserves_unit(
          job, &unit, first, "wide comparison and condition object") ||
      !expect_object_success_preserves_unit(
          job, &unit, second, "repeat wide comparison and condition object") ||
      !expect_object_failure_preserves_unit(
          job, &invalid_unit, failure, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "wide comparison object with a wide result") ||
      !expect_object_failure_preserves_unit(
          job, &unit, limited, CTOOL_ERR_LIMIT,
          CTOOL_C_EMIT_DIAG_LIMIT, NULL,
          "limited wide comparison and condition object") ||
      !expect_object_success_preserves_unit(
          job, &switch_unit, switch_first, "wide switch object") ||
      !expect_object_success_preserves_unit(
          job, &switch_unit, switch_second, "repeat wide switch object") ||
      !expect_object_failure_preserves_unit(
          job, &switch_unit, switch_limited, CTOOL_ERR_LIMIT,
          CTOOL_C_EMIT_DIAG_LIMIT, NULL, "limited wide switch object")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  first_bytes = ctool_buffer_view(first);
  second_bytes = ctool_buffer_view(second);
  if (first_bytes.size != second_bytes.size ||
      memcmp(first_bytes.data, second_bytes.data,
             (size_t)first_bytes.size) != 0 ||
      ctool_buffer_rewind(first, 0u) != CTOOL_OK ||
      !expect_object_success_preserves_unit(
          job, &unit, first,
          "recovered wide comparison and condition object")) {
    (void)fprintf(stderr,
                  "wide comparison and condition objects are not deterministic\n");
    goto cleanup;
  }
  switch_first_bytes = ctool_buffer_view(switch_first);
  switch_second_bytes = ctool_buffer_view(switch_second);
  if (switch_first_bytes.size != switch_second_bytes.size ||
      memcmp(switch_first_bytes.data, switch_second_bytes.data,
             (size_t)switch_first_bytes.size) != 0 ||
      ctool_buffer_rewind(switch_first, 0u) != CTOOL_OK ||
      !expect_object_success_preserves_unit(
          job, &switch_unit, switch_first, "recovered wide switch object")) {
    (void)fprintf(stderr, "wide switch objects are not deterministic\n");
    goto cleanup;
  }
  switch_first_bytes = ctool_buffer_view(switch_first);
  if (switch_first_bytes.size != switch_second_bytes.size ||
      memcmp(switch_first_bytes.data, switch_second_bytes.data,
             (size_t)switch_first_bytes.size) != 0) {
    goto cleanup;
  }
  first_bytes = ctool_buffer_view(first);
  if (first_bytes.size != second_bytes.size ||
      memcmp(first_bytes.data, second_bytes.data,
             (size_t)first_bytes.size) != 0) {
    goto cleanup;
  }
  object_source.path.text = ctool_string("/wide-condition-object.o");
  object_source.contents = second_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read wide condition object") ||
      !validate_wide_condition_execution(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  object_source.path.text = ctool_string("/wide-switch.o");
  object_source.contents = switch_second_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read wide switch object") ||
      !validate_wide_switch_execution(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_expressions);
  if (switch_limited != (ctool_buffer_t *)0) {
    ctool_buffer_close(switch_limited);
  }
  if (switch_second != (ctool_buffer_t *)0) {
    ctool_buffer_close(switch_second);
  }
  if (switch_first != (ctool_buffer_t *)0) {
    ctool_buffer_close(switch_first);
  }
  if (limited != (ctool_buffer_t *)0) {
    ctool_buffer_close(limited);
  }
  if (failure != (ctool_buffer_t *)0) {
    ctool_buffer_close(failure);
  }
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("wide-conditions: ok");
    return 0;
  }
  return 1;
}

static int run_wide_multiplication_object(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t first_bytes;
  ctool_bytes_t second_bytes;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_contains(
          job, "/kernel/crypto/x25519.c",
          "load active X25519 multiplication source",
          "the active X25519 wide multiplication helper changed",
          active_x25519_wide_multiply_body,
          active_x25519_wide_multiply_body_crlf) ||
      !parse_source(job, "/wide-multiplication-object.c",
                    wide_multiplication_object_source, &unit)) {
    goto cleanup;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 16u, 64u, &limited);
  }
  if (!check_status(status, CTOOL_OK,
                    "wide multiplication object buffers") ||
      !expect_object_success_preserves_unit(
          job, &unit, first, "wide multiplication object") ||
      !expect_object_success_preserves_unit(
          job, &unit, second, "repeat wide multiplication object")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  first_bytes = ctool_buffer_view(first);
  second_bytes = ctool_buffer_view(second);
  if (first_bytes.size != second_bytes.size ||
      memcmp(first_bytes.data, second_bytes.data,
             (size_t)first_bytes.size) != 0 ||
      !expect_object_failure_preserves_unit(
          job, &unit, limited, CTOOL_ERR_LIMIT,
          CTOOL_C_EMIT_DIAG_LIMIT, NULL,
          "limited wide multiplication object") ||
      ctool_buffer_rewind(first, 0u) != CTOOL_OK ||
      !expect_object_success_preserves_unit(
          job, &unit, first, "recovered wide multiplication object")) {
    (void)fprintf(stderr,
                  "wide multiplication object transaction differs\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  first_bytes = ctool_buffer_view(first);
  second_bytes = ctool_buffer_view(second);
  if (first_bytes.size != second_bytes.size ||
      memcmp(first_bytes.data, second_bytes.data,
             (size_t)first_bytes.size) != 0) {
    (void)fprintf(stderr,
                  "wide multiplication objects are not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/wide-multiplication-object.o");
  object_source.contents = second_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK,
                    "read wide multiplication object") ||
      !validate_wide_multiplication_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (limited != (ctool_buffer_t *)0) {
    ctool_buffer_close(limited);
  }
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  return passed != 0 ? 0 : 1;
}

static int run_wide_return_object(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_buffer_t *parameter_output = (ctool_buffer_t *)0;
  ctool_buffer_t *parameter_repeat = (ctool_buffer_t *)0;
  ctool_buffer_t *conversion_output = (ctool_buffer_t *)0;
  ctool_buffer_t *conversion_repeat = (ctool_buffer_t *)0;
  ctool_buffer_t *operation_output = (ctool_buffer_t *)0;
  ctool_buffer_t *operation_repeat = (ctool_buffer_t *)0;
  ctool_buffer_t *active_helper_output = (ctool_buffer_t *)0;
  ctool_buffer_t *active_helper_repeat = (ctool_buffer_t *)0;
  ctool_buffer_t *failure = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t parameter_unit;
  ctool_c_translation_unit_t variadic_argument_unit;
  ctool_c_translation_unit_t unprototyped_argument_unit;
  ctool_c_translation_unit_t conversion_unit;
  ctool_c_translation_unit_t invalid_conversion_unit;
  ctool_c_translation_unit_t invalid_reverse_conversion_unit;
  ctool_c_translation_unit_t operation_unit;
  ctool_c_translation_unit_t invalid_promotion_unit;
  ctool_c_translation_unit_t wide_count_operation_unit;
  ctool_c_translation_unit_t active_helper_unit;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_bytes_t first_bytes;
  ctool_bytes_t second_bytes;
  ctool_bytes_t parameter_bytes;
  ctool_bytes_t parameter_repeat_bytes;
  ctool_bytes_t conversion_bytes;
  ctool_bytes_t conversion_repeat_bytes;
  ctool_bytes_t operation_bytes;
  ctool_bytes_t operation_repeat_bytes;
  ctool_bytes_t active_helper_bytes;
  ctool_bytes_t active_helper_repeat_bytes;
  ctool_c_expression_t *invalid_conversion_expressions = NULL;
  ctool_c_expression_t *invalid_reverse_conversion_expressions = NULL;
  ctool_c_expression_t *invalid_promotion_expressions = NULL;
  ctool_u32 narrowing_conversion = CTOOL_C_AST_NONE;
  ctool_u32 reverse_conversion = CTOOL_C_AST_NONE;
  ctool_u32 wide_enum_promotion = CTOOL_C_AST_NONE;
  ctool_u32 unsigned_wide_type = CTOOL_C_TYPE_NONE;
  ctool_u32 index;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&parameter_unit, 0, sizeof(parameter_unit));
  (void)memset(&variadic_argument_unit, 0,
               sizeof(variadic_argument_unit));
  (void)memset(&unprototyped_argument_unit, 0,
               sizeof(unprototyped_argument_unit));
  (void)memset(&conversion_unit, 0, sizeof(conversion_unit));
  (void)memset(&operation_unit, 0, sizeof(operation_unit));
  (void)memset(&wide_count_operation_unit, 0,
               sizeof(wide_count_operation_unit));
  (void)memset(&active_helper_unit, 0, sizeof(active_helper_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_object_sources_are_unchanged(job) ||
      !parse_source(job, "/wide-return-object.c", wide_return_object_source,
                    &unit) ||
      !parse_source_mode(job, "/wide-parameter-object.c",
                         wide_parameter_object_source, CTOOL_TRUE,
                         &parameter_unit) ||
      !parse_source(job, "/wide-variadic-argument-object.c",
                    wide_variadic_argument_object_source,
                    &variadic_argument_unit) ||
      !parse_source(job, "/wide-unprototyped-argument-object.c",
                    wide_unprototyped_argument_object_source,
                    &unprototyped_argument_unit) ||
      !parse_source(job, "/wide-conversion-object.c",
                    wide_conversion_object_source, &conversion_unit) ||
      !parse_source_mode(job, "/wide-operation-object.c",
                         wide_operation_object_source, CTOOL_TRUE,
                         &operation_unit) ||
      !parse_source(job, "/wide-count-operation-object.c",
                    wide_count_operation_object_source,
                    &wide_count_operation_unit) ||
      !parse_source(job, "/active-wide-helper-object.c",
                    active_wide_helper_object_source,
                    &active_helper_unit)) {
    goto cleanup;
  }
  for (index = 0u; index < conversion_unit.expression_count; index++) {
    const ctool_c_expression_t *expression =
        &conversion_unit.expressions[index];
    ctool_u32 child;
    if (expression->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
        expression->conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
        expression->child_count != 1u ||
        expression->first_child >= conversion_unit.expression_child_count) {
      continue;
    }
    child = conversion_unit.expression_children[expression->first_child];
    if (child >= conversion_unit.expression_count ||
        expression->type >= conversion_unit.layout.type_count ||
        conversion_unit.expressions[child].type >=
            conversion_unit.layout.type_count) {
      continue;
    }
    if (conversion_unit.layout.types[expression->type].size == 2u &&
        conversion_unit.layout.types
                [conversion_unit.expressions[child].type]
                    .size == 8u) {
      narrowing_conversion = index;
    } else if (conversion_unit.layout.types[expression->type].size == 8u &&
               conversion_unit.layout.types[expression->type].is_signed ==
                   CTOOL_TRUE &&
               conversion_unit.layout.types
                       [conversion_unit.expressions[child].type]
                           .size == 8u &&
               conversion_unit.layout.types
                       [conversion_unit.expressions[child].type]
                           .is_signed == CTOOL_FALSE) {
      reverse_conversion = index;
    }
  }
  if (narrowing_conversion == CTOOL_C_AST_NONE ||
      reverse_conversion == CTOOL_C_AST_NONE ||
      conversion_unit.expression_count == 0u ||
      sizeof(*invalid_conversion_expressions) >
          SIZE_MAX / (size_t)conversion_unit.expression_count) {
    (void)fprintf(stderr,
                  "wide object conversion fixture metadata differs\n");
    goto cleanup;
  }
  invalid_conversion_expressions = (ctool_c_expression_t *)malloc(
      (size_t)conversion_unit.expression_count *
      sizeof(*invalid_conversion_expressions));
  invalid_reverse_conversion_expressions =
      (ctool_c_expression_t *)malloc(
          (size_t)conversion_unit.expression_count *
          sizeof(*invalid_reverse_conversion_expressions));
  if (invalid_conversion_expressions == NULL ||
      invalid_reverse_conversion_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_conversion_expressions, conversion_unit.expressions,
               (size_t)conversion_unit.expression_count *
                   sizeof(*invalid_conversion_expressions));
  invalid_conversion_expressions[narrowing_conversion].conversion =
      CTOOL_C_CONVERSION_INTEGER_PROMOTION;
  (void)memcpy(invalid_reverse_conversion_expressions,
               conversion_unit.expressions,
               (size_t)conversion_unit.expression_count *
                   sizeof(*invalid_reverse_conversion_expressions));
  invalid_reverse_conversion_expressions[reverse_conversion].conversion =
      CTOOL_C_CONVERSION_USUAL_ARITHMETIC;
  invalid_conversion_unit = conversion_unit;
  invalid_conversion_unit.expressions = invalid_conversion_expressions;
  invalid_reverse_conversion_unit = conversion_unit;
  invalid_reverse_conversion_unit.expressions =
      invalid_reverse_conversion_expressions;
  for (index = 0u; index < operation_unit.expression_count; index++) {
    const ctool_c_expression_t *expression =
        &operation_unit.expressions[index];
    ctool_u32 child;
    if (expression->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
        expression->child_count != 1u ||
        expression->first_child >= operation_unit.expression_child_count ||
        expression->type >= operation_unit.layout.type_count) {
      continue;
    }
    child = operation_unit.expression_children[expression->first_child];
    if (child >= operation_unit.expression_count ||
        operation_unit.expressions[child].type >=
            operation_unit.layout.type_count ||
        operation_unit.expressions[child].type >=
            operation_unit.graph.type_count) {
      continue;
    }
    if (expression->conversion == CTOOL_C_CONVERSION_INTEGER_PROMOTION &&
        operation_unit.layout.types[expression->type].size == 8u &&
        operation_unit.layout.types
                [operation_unit.expressions[child].type]
                    .size == 8u &&
        operation_unit.graph.types
                [operation_unit.expressions[child].type]
                    .kind == CTOOL_C_TYPE_ENUM) {
      wide_enum_promotion = index;
    } else if (expression->conversion ==
                   CTOOL_C_CONVERSION_USUAL_ARITHMETIC &&
               operation_unit.layout.types[expression->type].size == 8u &&
               operation_unit.layout.types[expression->type].is_signed ==
                   CTOOL_FALSE) {
      unsigned_wide_type = expression->type;
    }
  }
  if (wide_enum_promotion == CTOOL_C_AST_NONE ||
      unsigned_wide_type == CTOOL_C_TYPE_NONE ||
      operation_unit.expression_count == 0u ||
      sizeof(*invalid_promotion_expressions) >
          SIZE_MAX / (size_t)operation_unit.expression_count) {
    (void)fprintf(stderr,
                  "wide object promotion fixture metadata differs\n");
    goto cleanup;
  }
  invalid_promotion_expressions = (ctool_c_expression_t *)malloc(
      (size_t)operation_unit.expression_count *
      sizeof(*invalid_promotion_expressions));
  if (invalid_promotion_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_promotion_expressions, operation_unit.expressions,
               (size_t)operation_unit.expression_count *
                   sizeof(*invalid_promotion_expressions));
  invalid_promotion_expressions[wide_enum_promotion].type =
      unsigned_wide_type;
  invalid_promotion_unit = operation_unit;
  invalid_promotion_unit.expressions = invalid_promotion_expressions;
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &parameter_output);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &parameter_repeat);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &conversion_output);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &conversion_repeat);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &operation_output);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &operation_repeat);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &active_helper_output);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &active_helper_repeat);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &failure);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 16u, 64u, &limited);
  }
  if (!check_status(status, CTOOL_OK, "wide return object buffers") ||
      !expect_object_success_preserves_unit(job, &unit, first,
                                            "first wide return object") ||
      !expect_object_success_preserves_unit(job, &unit, second,
                                            "repeat wide return object")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  first_bytes = ctool_buffer_view(first);
  second_bytes = ctool_buffer_view(second);
  if (first_bytes.size != second_bytes.size ||
      memcmp(first_bytes.data, second_bytes.data,
             (size_t)first_bytes.size) != 0) {
    (void)fprintf(stderr, "wide return objects are not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/wide-return-object.o");
  object_source.contents = second_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read wide return object") ||
      !validate_wide_return_object(job, &object) ||
      !expect_object_success_preserves_unit(
          job, &parameter_unit, parameter_output,
          "wide parameter object ABI") ||
      !expect_object_success_preserves_unit(
          job, &parameter_unit, parameter_repeat,
          "repeat wide parameter object ABI")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  parameter_bytes = ctool_buffer_view(parameter_output);
  parameter_repeat_bytes = ctool_buffer_view(parameter_repeat);
  if (parameter_bytes.size != parameter_repeat_bytes.size ||
      memcmp(parameter_bytes.data, parameter_repeat_bytes.data,
             (size_t)parameter_bytes.size) != 0) {
    (void)fprintf(stderr,
                  "wide parameter objects are not deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/wide-parameter-object.o");
  object_source.contents = parameter_repeat_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read wide parameter object") ||
      !validate_wide_parameter_object(job, &object) ||
      !expect_object_failure_preserves_unit(
          job, &variadic_argument_unit, failure, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports arguments without declared "
          "parameter types only for represented scalar values",
          "wide variadic argument object") ||
      !expect_object_failure_preserves_unit(
          job, &unprototyped_argument_unit, failure,
          CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports arguments without declared "
          "parameter types only for represented scalar values",
          "wide unprototyped argument object") ||
      !expect_object_success_preserves_unit(
          job, &conversion_unit, conversion_output,
          "wide conversion object") ||
      !expect_object_success_preserves_unit(
          job, &conversion_unit, conversion_repeat,
          "repeat wide conversion object") ||
      !expect_object_success_preserves_unit(
          job, &operation_unit, operation_output,
          "wide operation object") ||
      !expect_object_success_preserves_unit(
          job, &operation_unit, operation_repeat,
          "repeat wide operation object") ||
      !expect_object_failure_preserves_unit(
          job, &wide_count_operation_unit, failure,
          CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide shift count object") ||
      !expect_object_failure_preserves_unit(
          job, &invalid_conversion_unit, failure, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "wide object narrowing mislabeled as integer promotion") ||
      !expect_object_failure_preserves_unit(
          job, &invalid_reverse_conversion_unit, failure, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "unsigned wide object conversion mislabeled as usual "
          "arithmetic") ||
      !expect_object_failure_preserves_unit(
          job, &invalid_promotion_unit, failure, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "wide enum object promotion to the wrong compatible type") ||
      !expect_object_success_preserves_unit(
          job, &active_helper_unit, active_helper_output,
          "active wide helper object") ||
      !expect_object_success_preserves_unit(
          job, &active_helper_unit, active_helper_repeat,
          "repeat active wide helper object") ||
      !expect_object_failure_preserves_unit(
          job, &operation_unit, limited, CTOOL_ERR_LIMIT,
          CTOOL_C_EMIT_DIAG_LIMIT, NULL,
          "limited wide operation object")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (ctool_buffer_rewind(operation_output, 0u) != CTOOL_OK ||
      !expect_object_success_preserves_unit(
          job, &operation_unit, operation_output,
          "recovered wide operation object")) {
    goto cleanup;
  }
  conversion_bytes = ctool_buffer_view(conversion_output);
  conversion_repeat_bytes = ctool_buffer_view(conversion_repeat);
  operation_bytes = ctool_buffer_view(operation_output);
  operation_repeat_bytes = ctool_buffer_view(operation_repeat);
  active_helper_bytes = ctool_buffer_view(active_helper_output);
  active_helper_repeat_bytes = ctool_buffer_view(active_helper_repeat);
  if (conversion_bytes.size != conversion_repeat_bytes.size ||
      memcmp(conversion_bytes.data, conversion_repeat_bytes.data,
             (size_t)conversion_bytes.size) != 0 ||
      operation_bytes.size != operation_repeat_bytes.size ||
      memcmp(operation_bytes.data, operation_repeat_bytes.data,
             (size_t)operation_bytes.size) != 0 ||
      active_helper_bytes.size != active_helper_repeat_bytes.size ||
      memcmp(active_helper_bytes.data, active_helper_repeat_bytes.data,
             (size_t)active_helper_bytes.size) != 0) {
    (void)fprintf(stderr,
                  "wide conversion or operation objects are not "
                  "deterministic\n");
    goto cleanup;
  }
  object_source.path.text = ctool_string("/wide-conversion-object.o");
  object_source.contents = conversion_repeat_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read wide conversion object") ||
      !validate_wide_conversion_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  object_source.path.text = ctool_string("/wide-operation-object.o");
  object_source.contents = operation_repeat_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read wide operation object") ||
      !validate_wide_operation_object(job, &object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  object_source.path.text = ctool_string("/active-wide-helper-object.o");
  object_source.contents = active_helper_repeat_bytes;
  (void)memset(&object, 0xa5, sizeof(object));
  status = ctool_elf32_read(job, &object_source, &object);
  if (!check_status(status, CTOOL_OK, "read active wide helper object") ||
      !validate_active_wide_helper_object(&object)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_conversion_expressions);
  free(invalid_reverse_conversion_expressions);
  free(invalid_promotion_expressions);
  if (limited != (ctool_buffer_t *)0) {
    ctool_buffer_close(limited);
  }
  if (failure != (ctool_buffer_t *)0) {
    ctool_buffer_close(failure);
  }
  if (active_helper_output != (ctool_buffer_t *)0) {
    ctool_buffer_close(active_helper_output);
  }
  if (active_helper_repeat != (ctool_buffer_t *)0) {
    ctool_buffer_close(active_helper_repeat);
  }
  if (operation_output != (ctool_buffer_t *)0) {
    ctool_buffer_close(operation_output);
  }
  if (operation_repeat != (ctool_buffer_t *)0) {
    ctool_buffer_close(operation_repeat);
  }
  if (conversion_output != (ctool_buffer_t *)0) {
    ctool_buffer_close(conversion_output);
  }
  if (conversion_repeat != (ctool_buffer_t *)0) {
    ctool_buffer_close(conversion_repeat);
  }
  if (parameter_repeat != (ctool_buffer_t *)0) {
    ctool_buffer_close(parameter_repeat);
  }
  if (parameter_output != (ctool_buffer_t *)0) {
    ctool_buffer_close(parameter_output);
  }
  if (second != (ctool_buffer_t *)0) {
    ctool_buffer_close(second);
  }
  if (first != (ctool_buffer_t *)0) {
    ctool_buffer_close(first);
  }
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("wide-returns: ok");
    return 0;
  }
  return 1;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "static-data") == 0) {
    return run_static_data(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "direct-goto") == 0) {
    return run_direct_goto(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "switch-object") == 0) {
    return run_switch_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "integer-mutation") == 0) {
    return run_integer_mutation_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "pointer-values") == 0) {
    return run_pointer_value_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "pointer-comparisons") == 0) {
    return run_pointer_comparison_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "pointer-conditions") == 0) {
    return run_pointer_condition_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "pointer-arithmetic") == 0) {
    return run_pointer_arithmetic_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "function-pointers") == 0) {
    return run_function_pointer_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "automatic-objects") == 0) {
    return run_automatic_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "block-externs") == 0) {
    return run_block_extern_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "block-functions") == 0) {
    return run_block_function_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "block-typedefs") == 0) {
    return run_block_typedef_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "block-enums") == 0) {
    return run_block_enum_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "bit-field-stores") == 0) {
    return run_bit_field_store_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "bit-field-mutations") == 0) {
    return run_bit_field_mutation_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "aggregate-initializers") == 0) {
    return run_aggregate_initializer_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "narrow-mutations") == 0) {
    return run_narrow_mutation_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "narrow-values") == 0) {
    return run_narrow_value_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "void-casts") == 0) {
    return run_void_cast_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "structure-values") == 0) {
    return run_structure_value_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "call-alignment") == 0) {
    return run_call_alignment_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "block-statics") == 0) {
    return run_block_static_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "compound-literals") == 0) {
    return run_compound_literal_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "old-style-empty-functions") == 0) {
    return run_old_style_empty_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "block-records") == 0) {
    return run_block_record_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "variadic-callees") == 0) {
    return run_variadic_callee_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "wide-returns") == 0) {
    if (run_wide_multiplication_object(argv[2]) != 0) {
      return 1;
    }
    return run_wide_return_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "wide-conditions") == 0) {
    return run_wide_condition_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "wide-objects") == 0) {
    return run_wide_value_object(argv[2]);
  }
  (void)fprintf(stderr,
                "usage: cupidc-object-contract "
                "static-data|direct-goto|switch-object|integer-mutation|"
                "pointer-values|pointer-comparisons|pointer-conditions|"
                "pointer-arithmetic|function-pointers|automatic-objects|"
                "block-externs|block-functions|block-typedefs|block-enums|"
                "bit-field-stores|bit-field-mutations|"
                "aggregate-initializers|"
                "narrow-mutations|"
                "narrow-values|"
                "void-casts|structure-values|call-alignment|block-statics|"
                "compound-literals|old-style-empty-functions|block-records|"
                "variadic-callees|wide-returns|wide-conditions|wide-objects "
                "HOST_ROOT\n");
  return 2;
}
