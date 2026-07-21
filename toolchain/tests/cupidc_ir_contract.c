#include "ctool.h"
#include "ctool_host.h"
#include "cupidc_frontend.h"
#include "cupidc_ir.h"
#include "cupidc_pp.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char active_helper[] =
    "static ctool_bool cemit_add_overflows(ctool_u32 left, ctool_u32 right) {\n"
    "  return left > 0xffffffffu - right ? CTOOL_TRUE : CTOOL_FALSE;\n"
    "}\n";

static const char active_multiply_overflows[] =
    "static ctool_bool cemit_multiply_overflows(ctool_u32 left,\n"
    "                                            ctool_u32 right) {\n"
    "  return left != 0u && right > 0xffffffffu / left ? CTOOL_TRUE\n"
    "                                                  : CTOOL_FALSE;\n"
    "}\n";

static const char active_power_of_two[] =
    "static ctool_bool cemit_power_of_two(ctool_u32 value) {\n"
    "  return value != 0u && (value & (value - 1u)) == 0u ? CTOOL_TRUE\n"
    "                                                     : CTOOL_FALSE;\n"
    "}\n";

static const char active_asm_lower[] =
    "static char asm_lower(char character) {\n"
    "  if (character >= 'A' && character <= 'Z') {\n"
    "    return (char)(character + ('a' - 'A'));\n"
    "  }\n"
    "  return character;\n"
    "}\n";

static const char active_x86_class_width[] =
    "static ctool_u16 x86_class_width(x86_operand_class_t class_id) {\n"
    "  switch (class_id) {\n"
    "    case X86_OC_GPR8:\n"
    "    case X86_OC_RM8:\n"
    "    case X86_OC_MEM8:\n"
    "    case X86_OC_IMM8:\n"
    "    case X86_OC_REL8:\n"
    "      return 8u;\n"
    "    case X86_OC_GPR16:\n"
    "    case X86_OC_RM16:\n"
    "    case X86_OC_MEM16:\n"
    "    case X86_OC_IMM16:\n"
    "    case X86_OC_REL16:\n"
    "    case X86_OC_FAR16_16:\n"
    "      return 16u;\n"
    "    case X86_OC_GPR32:\n"
    "    case X86_OC_RM32:\n"
    "    case X86_OC_MEM32:\n"
    "    case X86_OC_MMX_RM32:\n"
    "    case X86_OC_XMM_RM32:\n"
    "    case X86_OC_IMM32:\n"
    "    case X86_OC_REL32:\n"
    "    case X86_OC_FAR16_32:\n"
    "      return 32u;\n"
    "    case X86_OC_MEM64:\n"
    "    case X86_OC_MMX_RM64:\n"
    "    case X86_OC_XMM_RM64:\n"
    "      return 64u;\n"
    "    case X86_OC_MEM48:\n"
    "      return 48u;\n"
    "    case X86_OC_MEM128:\n"
    "    case X86_OC_XMM_RM128:\n"
    "      return 128u;\n"
    "    default:\n"
    "      return 0u;\n"
    "  }\n"
    "}\n";

static const char active_x86_set_memory_width[] =
    "static void x86_set_memory_width(ctool_x86_operand_t *operand,\n"
    "                                 x86_operand_class_t class_id) {\n"
    "  operand->width_bits = x86_class_width(class_id);\n"
    "  if (operand->width_bits == 0u && class_id == X86_OC_MEM) {\n"
    "    operand->width_bits = 0u;\n"
    "  }\n"
    "}\n";

static const char active_x86_put_u8[] =
    "static ctool_status_t x86_put_u8(ctool_x86_encoding_t *encoding,\n"
    "                                 ctool_u8 value) {\n"
    "  if (encoding->size >= CTOOL_X86_MAX_INSTRUCTION_BYTES) {\n"
    "    return CTOOL_ERR_LIMIT;\n"
    "  }\n"
    "  encoding->bytes[encoding->size] = value;\n"
    "  encoding->size++;\n"
    "  return CTOOL_OK;\n"
    "}\n";

static const char active_conditional_children[] =
    "  ctool_u32 children[3];\n";

static const char active_conditional_child_access[] =
    "                                  index, &children[index]);\n";

static const char active_section_map[] =
    "  ctool_u32 section_map[CEMIT_SECTION_COUNT];\n";

static const char active_section_map_use[] =
    "    section_map[logical] = CTOOL_ELF32_NO_SECTION;\n";

static const char active_automatic_aggregate_initializer[] =
    "  ctool_string_t no_name = {(const char *)0, 0u};";

static const char active_bool_valid[] =
    "static ctool_bool cfront_bool_valid(ctool_bool value) {\n"
    "  return value == CTOOL_FALSE || value == CTOOL_TRUE ? CTOOL_TRUE\n"
    "                                                      : CTOOL_FALSE;\n"
    "}\n";

static const char active_integer_mask[] =
    "static ctool_u64 cfront_integer_mask(cfront_integer_kind_t kind) {\n"
    "  return cfront_integer_width(kind) == 32u ? 0xffffffffull\n"
    "                                           : 0xffffffffffffffffull;\n"
    "}\n";

static const char active_cpu_frequency[] =
    "uint64_t get_cpu_freq(void) {\n"
    "    return tsc_freq;\n"
    "}\n";

static const char active_cpu_frequency_crlf[] =
    "uint64_t get_cpu_freq(void) {\r\n"
    "    return tsc_freq;\r\n"
    "}\r\n";

static const char active_public_storage[] =
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

static const char active_doom_color_record[] = "struct color {";
static const char active_doom_red_field[] = "uint32_t r:8;";
static const char active_doom_red_store[] =
    "colors[i].r = gammatable[usegamma][*palette++];";
static const char active_doom_local_red_store[] =
    "color.r = GFX_RGB565_R(rgb565_palette[i]);";

static const char active_ata_read_advance_lf[] =
    "insw(ATA_PRIMARY_DATA, buf, 256);\n"
    "        buf += 256;";

static const char active_ata_read_advance_crlf[] =
    "insw(ATA_PRIMARY_DATA, buf, 256);\r\n"
    "        buf += 256;";

static const char active_ata_write_advance_lf[] =
    "outsw(ATA_PRIMARY_DATA, buf, 256);\n"
    "        buf += 256;";

static const char active_ata_write_advance_crlf[] =
    "outsw(ATA_PRIMARY_DATA, buf, 256);\r\n"
    "        buf += 256;";

static const char integer_update_source[] =
    "int prefix_increment(int value) { return ++value; }\n"
    "unsigned int prefix_decrement(unsigned int value) { return --value; }\n"
    "int postfix_increment(int value) { return value++; }\n"
    "unsigned int postfix_decrement(unsigned int value) { return value--; }\n";

static const char integer_compound_source[] =
    "unsigned int all_compounds(unsigned int value, unsigned int right) {\n"
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
    "int signed_compounds(int value, int right) {\n"
    "  value /= right;\n"
    "  value >>= right;\n"
    "  return value;\n"
    "}\n";

static const char integer_compound_conversion_source[] =
    "int add_unsigned(int value, unsigned int right) {\n"
    "  return value += right;\n"
    "}\n"
    "enum signed_step { STEP_NEGATIVE = -1, STEP_ZERO = 0 };\n"
    "enum signed_step add_enum_unsigned(enum signed_step value,\n"
    "                                   unsigned int right) {\n"
    "  return value += right;\n"
    "}\n";

static const char integer_update_conversion_source[] =
    "enum step { STEP_ZERO = 0, STEP_ONE = 1 };\n"
    "enum step increment_step(enum step value) { return ++value; }\n"
    "enum step add_step(enum step value) { return value += 1; }\n"
    "volatile unsigned int update_state;\n"
    "unsigned int read_then_increment(void) { return update_state++; }\n";

static const char atomic_update_source[] =
    "_Atomic unsigned int state;\n"
    "unsigned int update_atomic(void) { return ++state; }\n";

static const char wide_compound_source[] =
    "long long state;\n"
    "int update_wide(void) { state += 1; return 0; }\n";

static const char bit_field_update_source[] =
    "struct flags { unsigned int value : 3; };\n"
    "struct flags state;\n"
    "unsigned int update_bit_field(void) { return ++state.value; }\n";

static const char bit_field_compound_source[] =
    "struct flags { unsigned int value : 3; };\n"
    "struct flags state;\n"
    "unsigned int add_bit_field(void) { return state.value += 1; }\n";

static const char bit_field_compound_matrix_source[] =
    "struct flags { unsigned int value : 3; };\n"
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
    "}\n";

static const char bit_field_postfix_source[] =
    "struct flags { unsigned int value : 3; };\n"
    "struct flags state;\n"
    "unsigned int increment_bit_field(void) { return state.value++; }\n"
    "unsigned int decrement_bit_field(void) { return state.value--; }\n";

static const char bit_field_volatile_whole_source[] =
    "struct whole { volatile unsigned int value : 32; };\n"
    "unsigned int increment_whole(struct whole *state) {\n"
    "  return ++state->value;\n"
    "}\n"
    "unsigned int read_then_increment_whole(struct whole *state) {\n"
    "  return state->value++;\n"
    "}\n";

static const char narrow_update_source[] =
    "unsigned short state;\n"
    "unsigned int update_narrow(void) { return ++state; }\n";

static const char narrow_compound_source[] =
    "unsigned short state;\n"
    "unsigned int add_narrow(void) { return state += 1u; }\n";

static const char bool_update_source[] =
    "_Bool state;\n"
    "_Bool update_bool(void) { return state++; }\n";

static const char bool_compound_source[] =
    "_Bool state;\n"
    "_Bool add_bool(void) { return state += 1; }\n";

static const char invalid_update_source[] =
    "int update_value(int value) { return ++value; }\n";

static const char switch_lowering_source[] =
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

static const char switch_control_source[] =
    "int switch_break(int value) {\n"
    "  int result = 9;\n"
    "  switch (value) {\n"
    "  case 0:\n"
    "    result = 1;\n"
    "    break;\n"
    "  case 1:\n"
    "    result = 2;\n"
    "  default:\n"
    "    result = result + 3;\n"
    "  }\n"
    "  return result;\n"
    "}\n"
    "int switch_in_loop(int outer, int inner) {\n"
    "  int result = 0;\n"
    "  for (; outer; outer = outer - 1) {\n"
    "    switch (inner) {\n"
    "    case 0:\n"
    "      result = result + 1;\n"
    "      continue;\n"
    "    case 1:\n"
    "      break;\n"
    "    default:\n"
    "      result = result + 3;\n"
    "    }\n"
    "    result = result + 2;\n"
    "    break;\n"
    "  }\n"
    "  return result;\n"
    "}\n"
    "int switch_without_default(int value) {\n"
    "  switch (value) {\n"
    "  case 0: return 1;\n"
    "  }\n"
    "  return 2;\n"
    "}\n"
    "int switch_negative(int value) {\n"
    "  switch (value) {\n"
    "  case -1:\n"
    "    return 3;\n"
    "  default:\n"
    "    return 4;\n"
    "  }\n"
    "}\n";

static const char switch_nesting_source[] =
    "int nested_switch(int outer, int inner) {\n"
    "  int result = 0;\n"
    "  switch (outer) {\n"
    "  case 0:\n"
    "    switch (inner) {\n"
    "    case 0:\n"
    "      result = 1;\n"
    "      break;\n"
    "    default:\n"
    "      result = 2;\n"
    "    }\n"
    "    break;\n"
    "  default:\n"
    "    result = 3;\n"
    "  }\n"
    "  return result;\n"
    "}\n"
    "int case_in_if(int value) {\n"
    "  switch (value) {\n"
    "    if (value) {\n"
    "    case 1:\n"
    "      return 1;\n"
    "    }\n"
    "  default:\n"
    "    return 2;\n"
    "  }\n"
    "}\n"
    "int goto_case(int value) {\n"
    "  goto inside;\n"
    "  switch (value) {\n"
    "  case 0:\n"
    "inside:\n"
    "    return 1;\n"
    "  default:\n"
    "    return 2;\n"
    "  }\n"
    "}\n"
    "int unreachable_nested_switch(int outer, int inner) {\n"
    "  switch (outer) {\n"
    "  case 0:\n"
    "    return 1;\n"
    "    switch (inner) {\n"
    "    case 0:\n"
    "      return 2;\n"
    "    default:\n"
    "      return 4;\n"
    "    }\n"
    "  default:\n"
    "    return 3;\n"
    "  }\n"
    "}\n"
    "int unreachable_switch_prefix(int value) {\n"
    "  switch (value) {\n"
    "    break;\n"
    "  default:\n"
    "    return 1;\n"
    "  }\n"
    "}\n"
    "int dead_prefix_goto(int value) {\n"
    "  switch (value) {\n"
    "    goto dead;\n"
    "  default:\n"
    "    return 1;\n"
    "  }\n"
    "dead:\n"
    "  return 2;\n"
    "}\n"
    "int case_below_unused_label(int value) {\n"
    "  switch (value) {\n"
    "unused: {\n"
    "    goto dead;\n"
    "  case 1:\n"
    "    return 1;\n"
    "  }\n"
    "  default:\n"
    "    return 0;\n"
    "  }\n"
    "dead:\n"
    "  return 2;\n"
    "}\n"
    "int case_in_loops(int value) {\n"
    "  switch (value) {\n"
    "    while (value) {\n"
    "    case 1:\n"
    "      return 1;\n"
    "    }\n"
    "    do {\n"
    "    case 2:\n"
    "      return 2;\n"
    "    } while (value);\n"
    "    for (; value;) {\n"
    "    case 3:\n"
    "      return 3;\n"
    "    }\n"
    "  default:\n"
    "    return 0;\n"
    "  }\n"
    "}\n";

static const char wide_switch_source[] =
    "int wide_switch(int value) {\n"
    "  switch ((long long)value) {\n"
    "  case 0:\n"
    "    return 1;\n"
    "  default:\n"
    "    return 2;\n"
    "  }\n"
    "}\n";

static const char active_branch_fits[] =
    "static ctool_bool asm_branch_fits_i8(ctool_u32 bits) {\n"
    "  return bits <= 0x7fu || bits >= 0xffffff80u ? CTOOL_TRUE : CTOOL_FALSE;\n"
    "}\n";

static const char active_aes_rotw[] =
    "static uint32_t rotw(uint32_t w) { return (w << 8) | (w >> 24); }";

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

static const char active_initializer_success[] =
    "  return !cc->error;";

static const char active_size_increment[] =
    "      size++;";

static const char active_capacity_growth[] =
    "      capacity *= 2u;";

static const char active_decimal_shrink[] =
    "    value /= 10u;";

static const char active_pointer_member_helper[] =
    "static ctool_bool obj_region_less(const obj_flat_region_t *left,\n"
    "                                  const obj_flat_region_t *right) {\n"
    "  return left->address < right->address ||\n"
    "                 (left->address == right->address && left->order < right->order)\n"
    "             ? CTOOL_TRUE\n"
    "             : CTOOL_FALSE;\n"
    "}";

static const char pointer_member_source[] =
    "typedef unsigned int ctool_u32;\n"
    "typedef int ctool_bool;\n"
    "#define CTOOL_FALSE 0\n"
    "#define CTOOL_TRUE 1\n"
    "typedef struct { const unsigned char *data; ctool_u32 size; } ctool_bytes_t;\n"
    "typedef struct {\n"
    "  ctool_u32 address;\n"
    "  ctool_u32 order;\n"
    "  ctool_bytes_t contents;\n"
    "} obj_flat_region_t;\n"
    "static ctool_bool obj_region_less(const obj_flat_region_t *left,\n"
    "                                  const obj_flat_region_t *right) {\n"
    "  return left->address < right->address ||\n"
    "                 (left->address == right->address && left->order < right->order)\n"
    "             ? CTOOL_TRUE\n"
    "             : CTOOL_FALSE;\n"
    "}\n";

static const char pointer_value_source[] =
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

static const char pointer_comparison_source[] =
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

static const char void_pointer_equality_source[] =
    "int void_pointer_equal(void *left, void *right) { return left == right; }\n";

static const char pointer_condition_source[] =
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

static const char pointer_arithmetic_source[] =
    "int *advance(int *pointer, int index) { return pointer + index; }\n"
    "int *reverse_add(int index, int *pointer) { return index + pointer; }\n"
    "int *retreat(int *pointer, int index) { return pointer - index; }\n"
    "int distance(int *end, const int *begin) { return end - begin; }\n"
    "int read_index(int *pointer, unsigned int index) { return pointer[index]; }\n"
    "int read_reverse(int *pointer, unsigned int index) { return index[pointer]; }\n"
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

static const char atomic_pointer_update_source[] =
    "int * _Atomic shared_pointer;\n"
    "int *advance_atomic(void) { return shared_pointer++; }\n";

static const char wide_pointer_offset_source[] =
    "int *wide_assign(int *pointer) { return pointer += (long long)1; }\n";

static const char qualified_array_decay_source[] =
    "typedef int values_t[2];\n"
    "static const values_t const_values;\n"
    "static int *unqualified_pointer;\n"
    "const int *const_start(void) { return const_values; }\n";

static const char qualified_pointer_update_source[] =
    "static int *volatile cursor;\n"
    "int *advance_volatile(void) { return cursor++; }\n";

static const char wide_pointer_cast_source[] =
    "int *cast_wide_integer(void) { return (int *)(long long)0; }\n";

static const char atomic_pointer_condition_source[] =
    "int * _Atomic shared_pointer;\n"
    "int test_shared_pointer(void) { return !!shared_pointer; }\n";

static const char atomic_pointer_source[] =
    "int * _Atomic shared_pointer;\n"
    "int *read_shared_pointer(void) { return shared_pointer; }\n";

static const char function_pointer_call_source[] =
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
    "int invoke_three(combine_t callback, int first, int second, int third) {\n"
    "  return callback(first, second, third);\n"
    "}\n";

static const char function_pointer_value_source[] =
    "typedef int (*callback_t)(int);\n"
    "extern int target(int);\n"
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

static const char wide_function_pointer_source[] =
    "typedef long long (*wide_callback_t)(long long);\n"
    "int call_wide(wide_callback_t callback) { return callback(1); }\n";

static const char wide_return_source[] =
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

static const char wide_parameter_source[] =
    "typedef unsigned long long ctool_u64;\n"
    "ctool_u64 pass_wide(ctool_u64 value) { return value; }\n";

static const char wide_arithmetic_source[] =
    "typedef unsigned long long ctool_u64;\n"
    "ctool_u64 add_wide(void) { return 0x1122334455667788ull + 1ull; }\n";

static const char wide_conversion_source[] =
    "typedef unsigned long long ctool_u64;\n"
    "ctool_u64 widen_value(void) { return (ctool_u64)1u; }\n";

static const char wide_object_source[] =
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

static const char wide_atomic_load_source[] =
    "typedef unsigned long long ctool_u64;\n"
    "_Atomic ctool_u64 shared;\n"
    "ctool_u64 load_atomic(void) { return shared; }\n";

static const char wide_atomic_store_source[] =
    "typedef unsigned long long ctool_u64;\n"
    "_Atomic ctool_u64 shared;\n"
    "void store_atomic(void) { shared = 1ull; }\n";

static const char atomic_function_pointer_source[] =
    "typedef int (*callback_t)(int);\n"
    "callback_t _Atomic shared_callback;\n"
    "int callback_is_set(void) { return shared_callback != 0; }\n";

static const char function_pointer_qualification_source[] =
    "typedef int (*plain_callback_t)(int);\n"
    "typedef int (*qualified_parameter_callback_t)(const volatile int);\n"
    "typedef int (*atomic_parameter_callback_t)(_Atomic int);\n"
    "typedef int (*plain_pointer_callback_t)(int *);\n"
    "typedef int (*restrict_parameter_callback_t)(int *restrict);\n"
    "typedef int (*qualified_referent_callback_t)(const int *);\n"
    "typedef int (*old_style_callback_t)();\n"
    "typedef int (*promoted_parameter_callback_t)(int, double);\n"
    "typedef int (*promoted_char_callback_t)(char);\n";

static const char *const function_pointer_cast_sources[] = {
    "typedef int (*callback_t)(int);\n"
    "callback_t from_integer(unsigned value) { return (callback_t)value; }\n",
    "typedef int (*callback_t)(int);\n"
    "unsigned to_integer(callback_t value) { return (unsigned)value; }\n",
    "typedef int (*callback_t)(int);\n"
    "callback_t from_object(void *value) { return (callback_t)value; }\n",
    "typedef int (*callback_t)(int);\n"
    "void *to_object(callback_t value) { return (void *)value; }\n",
    "typedef int (*callback_t)(int);\n"
    "typedef int (*other_t)(void);\n"
    "other_t change_signature(callback_t value) { return (other_t)value; }\n",
    "typedef int (*callback_t)(int);\n"
    "callback_t keep_signature(callback_t value) { return (callback_t)value; }\n"};

static const char *const function_pointer_cast_paths[] = {
    "/function-pointer-cast-from-integer.c",
    "/function-pointer-cast-to-integer.c",
    "/function-pointer-cast-from-object.c",
    "/function-pointer-cast-to-object.c",
    "/function-pointer-cast-signature.c",
    "/function-pointer-cast-compatible.c"};

static const char active_simd_cpuid_return[] =
    "    return ((before ^ after) & (1u << 21)) != 0u;";

static const char active_job_arena[] =
    "ctool_arena_t *ctool_job_arena(ctool_job_t *job) {\n"
    "  return job != (ctool_job_t *)0 ? job->arena : (ctool_arena_t *)0;\n"
    "}";

static const char active_invocation_body_check[] =
    "      body == (ctool_invocation_body_t)0 ||";

static const char active_invocation_body_call[] =
    "    status = body(&invocation, user_data);";

static const char active_linker_selector_call[] =
    "          selector(section, selector_context) == CTOOL_TRUE &&";

static const char active_call[] =
    "static uint32_t syscall_getpid(void) { return process_get_current_pid(); }";

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

static const char forward_goto_source[] =
    "int goto_forward(int value) {\n"
    "  if (value) goto done;\n"
    "  return 1;\n"
    "done:\n"
    "  return 2;\n"
    "}\n"
    "int goto_backward(int value) {\n"
    "again:\n"
    "  if (value) {\n"
    "    value = value - 1;\n"
    "    goto again;\n"
    "  }\n"
    "  return value;\n"
    "}\n"
    "int goto_cycle(int value) {\n"
    "  goto check;\n"
    "again:\n"
    "  value = value - 1;\n"
    "check:\n"
    "  if (value) goto again;\n"
    "  return value;\n"
    "}\n"
    "int unreachable_goto(void) {\n"
    "  return 7;\n"
    "  goto dead;\n"
    "dead:\n"
    "  return 9;\n"
    "}\n";

static const char nested_goto_source[] =
    "int goto_nested(int value) {\n"
    "  goto inside;\n"
    "  {\n"
    "    value = value + 1;\n"
    "inside:\n"
    "    return value;\n"
    "  }\n"
    "}\n"
    "int goto_if_return(int value) {\n"
    "  goto inside_if;\n"
    "  if (value) {\n"
    "inside_if:\n"
    "    return 1;\n"
    "  } else {\n"
    "    value = value + 1;\n"
    "  }\n"
    "}\n"
    "int goto_after_break(int value) {\n"
    "  for (;;) { break; }\n"
    "  goto done_after_break;\n"
    "  return 1;\n"
    "done_after_break:\n"
    "  return value;\n"
    "}\n"
    "int goto_terminal_do(void) {\n"
    "  goto inside_terminal_do;\n"
    "  do {\n"
    "inside_terminal_do:\n"
    "    return 5;\n"
    "  } while (1);\n"
    "}\n"
    "int goto_into_break(void) {\n"
    "  goto inside_break;\n"
    "  for (;;) {\n"
    "inside_break:\n"
    "    break;\n"
    "  }\n"
    "  return 6;\n"
    "}\n"
    "int goto_into_continue(void) {\n"
    "  goto inside_continue;\n"
    "  for (;;) {\n"
    "inside_continue:\n"
    "    continue;\n"
    "  }\n"
    "  return 7;\n"
    "}\n"
    "int goto_declaration(int value) {\n"
    "  goto with_declaration;\n"
    "  return 9;\n"
    "with_declaration: {\n"
    "    int copy = value;\n"
    "    return copy;\n"
    "  }\n"
    "}\n";

static const char active_addition[] =
    "int add2(int x, int y) {\n"
    "    return x + y;\n"
    "}\n";

static const char active_paint_x[] =
    "  return CANVAS_X + (cx - view_x) * zoom_level;";
static const char active_paint_y[] =
    "  return CANVAS_Y + (cy - view_y) * zoom_level;";

static const char active_vga_object[] =
    "static uint32_t last_flip_ms = 0;";
static const char active_vga_wait_object[] =
    "static bool vga_wait_vsync = false;";
static const char active_vga_wait_setter[] =
    "void vga_set_vsync_wait(bool enabled) { vga_wait_vsync = enabled; }";
static const char active_vga_function[] =
    "bool vga_flip_ready(void) {\n"
    "  uint32_t now = timer_get_uptime_ms();\n"
    "  return (now - last_flip_ms) >= 16u;\n"
    "}\n";
static const char active_vga_function_crlf[] =
    "bool vga_flip_ready(void) {\r\n"
    "  uint32_t now = timer_get_uptime_ms();\r\n"
    "  return (now - last_flip_ms) >= 16u;\r\n"
    "}\r\n";
static const char active_bool_type[] =
    "typedef enum { false = 0, true = 1 } bool;";
static const char active_timer_ticks_member[] =
    "    uint64_t ticks;           // Total number of timer ticks since boot";
static const char active_timer_frequency_member[] =
    "    uint32_t frequency;       // Timer frequency in Hz";
static const char active_timer_ms_member[] =
    "    uint32_t ms_per_tick;     // Milliseconds per tick";
static const char active_timer_calibrated_member[] =
    "    bool is_calibrated;       // Whether timer has been calibrated";
static const char active_timer_type_end[] = "} timer_state_t;";

static const char active_timer_state[] =
    "static timer_state_t timer_state = {\n"
    "    .ticks = 0,\n"
    "    .frequency = 0,\n"
    "    .ms_per_tick = 0,\n"
    "    .is_calibrated = false\n"
    "};\n";
static const char active_timer_frequency[] =
    "uint32_t timer_get_frequency(void) {\n"
    "    return timer_state.frequency;\n"
    "}\n";
static const char active_doom_blue_member[] = "    uint32_t b:8;";
static const char active_doom_green_member[] = "    uint32_t g:8;";
static const char active_doom_red_member[] = "    uint32_t r:8;";
static const char active_doom_alpha_member[] = "    uint32_t a:8;";
static const char active_doom_red_read[] = "c.r >> 3";
static const char active_doom_green_read[] = "c.g >> 2";
static const char active_doom_blue_read[] = "c.b >> 3";

static const char local_fixture[] =
    "typedef unsigned int uint32_t;\n"
    "uint32_t timer_get_uptime_ms(void);\n"
    "uint32_t vga_flip_time_probe(uint32_t prior_value) {\n"
    "  uint32_t now = timer_get_uptime_ms();\n"
    "  register uint32_t prior = prior_value;\n"
    "  auto uint32_t unused;\n"
    "  return now + prior;\n"
    "}\n";

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

static ctool_u32 find_binding(const ctool_c_translation_unit_t *unit,
                              const char *name) {
  ctool_u32 index;
  for (index = 0u; index < unit->binding_count; index++) {
    if (string_equal(unit->bindings[index].name, name) != 0) {
      return index;
    }
  }
  return CTOOL_C_AST_NONE;
}

static ctool_u32 find_block_binding(const ctool_c_translation_unit_t *unit,
                                     const char *name) {
  ctool_u32 index;
  for (index = 0u; index < unit->block_binding_count; index++) {
    if (string_equal(unit->block_bindings[index].name, name) != 0) {
      return index;
    }
  }
  return CTOOL_C_AST_NONE;
}

static ctool_u32 find_member(const ctool_c_translation_unit_t *unit,
                             const char *name) {
  ctool_u32 index;
  for (index = 0u; index < unit->graph.member_count; index++) {
    if (string_equal(unit->graph.members[index].name, name) != 0) {
      return index;
    }
  }
  return CTOOL_C_AST_NONE;
}

static int arena_marks_equal(ctool_arena_mark_t left,
                             ctool_arena_mark_t right) {
  return left.owner == right.owner && left.block == right.block &&
                 left.used == right.used && left.generation == right.generation
             ? 1
             : 0;
}

static int pointer_value_query_matches(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 left, ctool_u32 right, ctool_bool expected) {
  ctool_bool compatible = expected == CTOOL_TRUE ? CTOOL_FALSE : CTOOL_TRUE;
  ctool_arena_mark_t mark = ctool_arena_mark(ctool_job_arena(job));
  ctool_status_t status = ctool_c_ir_pointer_value_types_compatible(
      job, unit, left, right, &compatible);
  return status == CTOOL_OK && compatible == expected &&
                 arena_marks_equal(
                     mark, ctool_arena_mark(ctool_job_arena(job))) != 0
             ? 1
             : 0;
}

static int pointer_arithmetic_query_matches(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 left, ctool_u32 right, ctool_bool expected) {
  ctool_bool compatible = expected == CTOOL_TRUE ? CTOOL_FALSE : CTOOL_TRUE;
  ctool_arena_mark_t mark = ctool_arena_mark(ctool_job_arena(job));
  ctool_status_t status = ctool_c_ir_pointer_arithmetic_types_compatible(
      job, unit, left, right, &compatible);
  return status == CTOOL_OK && compatible == expected &&
                 arena_marks_equal(
                     mark, ctool_arena_mark(ctool_job_arena(job))) != 0
             ? 1
             : 0;
}

static int array_decay_query_matches(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 array_type, ctool_u32 pointer_type, ctool_bool expected) {
  ctool_bool compatible = expected == CTOOL_TRUE ? CTOOL_FALSE : CTOOL_TRUE;
  ctool_arena_mark_t mark = ctool_arena_mark(ctool_job_arena(job));
  ctool_status_t status = ctool_c_ir_array_decay_types_compatible(
      job, unit, array_type, pointer_type, &compatible);
  return status == CTOOL_OK && compatible == expected &&
                 arena_marks_equal(
                     mark, ctool_arena_mark(ctool_job_arena(job))) != 0
             ? 1
             : 0;
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

static int open_limited_job(const char *host_root,
                            ctool_host_adapter_t *adapter,
                            ctool_job_t **job_out) {
  ctool_job_config_t config;
  ctool_limits_t limits = ctool_default_limits();
  ctool_status_t status = ctool_host_adapter_init(adapter, host_root);
  if (!check_status(status, CTOOL_OK, "limited host adapter init")) {
    return 0;
  }
  limits.arena_block_bytes = 128u;
  limits.arena_bytes = 256u;
  config = ctool_host_job_config(adapter, limits);
  status = ctool_job_open(&config, job_out);
  return check_status(status, CTOOL_OK, "limited job open");
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

  if (text_size > UINT32_MAX) {
    (void)fprintf(stderr, "%s: source exceeds the contract limit\n", path);
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
  if (status != CTOOL_OK || tape.tokens == NULL || tape.token_count == 0u ||
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

static int active_source_is_unchanged(ctool_job_t *job) {
  ctool_path_t path;
  ctool_source_t source;
  ctool_status_t status;
  path.text = ctool_string("/toolchain/cupidc_emit.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active emitter source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_helper) == NULL ||
      strstr((const char *)source.contents.data,
             active_multiply_overflows) == NULL ||
      strstr((const char *)source.contents.data, active_power_of_two) ==
          NULL ||
      strstr((const char *)source.contents.data, active_section_map) == NULL ||
      strstr((const char *)source.contents.data, active_section_map_use) ==
          NULL) {
    (void)fprintf(stderr, "an active emitter helper changed\n");
    return 0;
  }
  path.text = ctool_string("/toolchain/cupidc_frontend.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active frontend source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_bool_valid) == NULL ||
      strstr((const char *)source.contents.data, active_integer_mask) == NULL ||
      strstr((const char *)source.contents.data, active_public_storage) ==
          NULL) {
    (void)fprintf(stderr, "an active frontend helper changed\n");
    return 0;
  }
  path.text = ctool_string("/toolchain/ctool.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active core source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_size_increment) ==
          NULL ||
      strstr((const char *)source.contents.data, active_capacity_growth) ==
          NULL ||
      strstr((const char *)source.contents.data, active_decimal_shrink) ==
          NULL ||
      strstr((const char *)source.contents.data, active_job_arena) == NULL ||
      strstr((const char *)source.contents.data,
             active_invocation_body_check) == NULL ||
      strstr((const char *)source.contents.data,
             active_invocation_body_call) == NULL) {
    (void)fprintf(stderr, "an active CupidC source guard changed\n");
    return 0;
  }
  path.text = ctool_string("/toolchain/cupidc_ir.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active IR source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data,
             active_conditional_children) == NULL ||
      strstr((const char *)source.contents.data,
             active_conditional_child_access) == NULL) {
    (void)fprintf(stderr, "the active conditional child array changed\n");
    return 0;
  }
  path.text = ctool_string("/toolchain/cupiddis.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active disassembler source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_signed_bits) == NULL ||
      strstr((const char *)source.contents.data, active_dis_hex_body) == NULL) {
    (void)fprintf(stderr, "an active disassembler helper changed\n");
    return 0;
  }
  path.text = ctool_string("/toolchain/cupidobj.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active object-tool source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data,
             active_pointer_member_helper) == NULL) {
    (void)fprintf(stderr, "the active object-pointer helper changed\n");
    return 0;
  }
  path.text = ctool_string("/toolchain/cupidld.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active linker source") ||
      source.contents.data == NULL ||
      (strstr((const char *)source.contents.data, active_linker_goto) == NULL &&
       strstr((const char *)source.contents.data,
              active_linker_goto_crlf) == NULL) ||
      (strstr((const char *)source.contents.data, active_linker_label) ==
           NULL &&
       strstr((const char *)source.contents.data,
              active_linker_label_crlf) == NULL) ||
      strstr((const char *)source.contents.data,
             active_linker_selector_call) == NULL) {
    (void)fprintf(stderr, "the active linker cleanup path changed\n");
    return 0;
  }
  path.text = ctool_string("/bin/cupidc_parse.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active CupidC source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data,
             active_initializer_success) == NULL) {
    (void)fprintf(stderr, "the active initializer result changed\n");
    return 0;
  }
  path.text = ctool_string("/toolchain/cupidasm.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active assembler source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_branch_fits) == NULL ||
      strstr((const char *)source.contents.data, active_asm_lower) == NULL) {
    (void)fprintf(stderr, "an active assembler helper changed\n");
    return 0;
  }
  path.text = ctool_string("/toolchain/x86.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active x86 source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_x86_class_width) ==
          NULL ||
      strstr((const char *)source.contents.data,
             active_x86_set_memory_width) == NULL ||
      strstr((const char *)source.contents.data, active_x86_put_u8) == NULL) {
    (void)fprintf(stderr, "an active x86 width helper changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/crypto/aes.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active AES source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_aes_rotw) == NULL) {
    (void)fprintf(stderr, "the active AES word rotation changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/mm/memory.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active memory source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_align_up) == NULL) {
    (void)fprintf(stderr, "the active memory alignment helper changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/cpu/simd.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active SIMD source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data,
             active_simd_cpuid_return) == NULL) {
    (void)fprintf(stderr, "the active CPUID toggle expression changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/core/syscall.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active syscall source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_call) == NULL ||
      (strstr((const char *)source.contents.data, active_sleep) == NULL &&
       strstr((const char *)source.contents.data, active_sleep_crlf) == NULL)) {
    (void)fprintf(stderr, "an active syscall helper changed\n");
    return 0;
  }
  path.text = ctool_string("/bin/cupidc_test3.cc");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active addition source") ||
      source.contents.data == NULL ||
      (strstr((const char *)source.contents.data, active_addition) == NULL &&
       strstr((const char *)source.contents.data,
              "int add2(int x, int y) {\r\n"
              "    return x + y;\r\n"
              "}\r\n") == NULL)) {
    (void)fprintf(stderr, "the active add2 function changed\n");
    return 0;
  }
  path.text = ctool_string("/bin/browser/url_hash.cc");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active browser for loop") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_for_header) == NULL) {
    (void)fprintf(stderr, "the active browser for loop changed\n");
    return 0;
  }
  path.text = ctool_string("/bin/browser/woff.cc");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK,
                    "load active browser declaration loop") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data,
             active_declaration_for_header) == NULL) {
    (void)fprintf(stderr, "the active browser declaration loop changed\n");
    return 0;
  }
  path.text = ctool_string("/toolchain/cupidc_ir.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active CupidC IR source") ||
      source.contents.data == NULL ||
      (strstr((const char *)source.contents.data, active_loop_continue) ==
           NULL &&
       strstr((const char *)source.contents.data,
              active_loop_continue_crlf) == NULL) ||
      (strstr((const char *)source.contents.data, active_loop_break) == NULL &&
       strstr((const char *)source.contents.data, active_loop_break_crlf) ==
           NULL) ||
      (strstr((const char *)source.contents.data,
              active_nested_declaration) == NULL &&
       strstr((const char *)source.contents.data,
              active_nested_declaration_crlf) == NULL)) {
    (void)fprintf(stderr, "the active CupidC IR declaration source changed\n");
    return 0;
  }
  path.text = ctool_string("/bin/paint.cc");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active Paint source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, "int CANVAS_X = 56;") ==
          NULL ||
      strstr((const char *)source.contents.data, "int CANVAS_Y = 20;") ==
          NULL ||
      strstr((const char *)source.contents.data, "int zoom_level = 1;") ==
          NULL ||
      strstr((const char *)source.contents.data, "int view_x = 0;") == NULL ||
      strstr((const char *)source.contents.data, "int view_y = 0;") == NULL ||
      strstr((const char *)source.contents.data,
             "int canvas_to_screen_x(int cx) {") == NULL ||
      strstr((const char *)source.contents.data, active_paint_x) == NULL ||
      strstr((const char *)source.contents.data,
             "int canvas_to_screen_y(int cy) {") == NULL ||
      strstr((const char *)source.contents.data, active_paint_y) == NULL) {
    (void)fprintf(stderr, "the active Paint coordinate transforms changed\n");
    return 0;
  }
  path.text = ctool_string("/drivers/vga.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active VGA source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_vga_object) == NULL ||
      strstr((const char *)source.contents.data, active_vga_wait_object) ==
          NULL ||
      strstr((const char *)source.contents.data, active_vga_wait_setter) ==
          NULL ||
      (strstr((const char *)source.contents.data, active_vga_function) ==
           NULL &&
       strstr((const char *)source.contents.data,
              active_vga_function_crlf) == NULL)) {
    (void)fprintf(stderr, "the active VGA flip function changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/core/types.h");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active bool and timer types") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_bool_type) == NULL ||
      strstr((const char *)source.contents.data, active_timer_ticks_member) ==
          NULL ||
      strstr((const char *)source.contents.data,
             active_timer_frequency_member) == NULL ||
      strstr((const char *)source.contents.data, active_timer_ms_member) ==
          NULL ||
      strstr((const char *)source.contents.data,
             active_timer_calibrated_member) == NULL ||
      strstr((const char *)source.contents.data, active_timer_type_end) ==
          NULL) {
    (void)fprintf(stderr, "the active bool or timer type changed\n");
    return 0;
  }
  path.text = ctool_string("/drivers/timer.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active timer source") ||
      source.contents.data == NULL ||
      (strstr((const char *)source.contents.data, active_timer_state) ==
           NULL &&
       strstr((const char *)source.contents.data,
              "static timer_state_t timer_state = {\r\n"
              "    .ticks = 0,\r\n"
              "    .frequency = 0,\r\n"
              "    .ms_per_tick = 0,\r\n"
              "    .is_calibrated = false\r\n"
              "};\r\n") == NULL) ||
      (strstr((const char *)source.contents.data, active_timer_frequency) ==
           NULL &&
       strstr((const char *)source.contents.data,
              "uint32_t timer_get_frequency(void) {\r\n"
              "    return timer_state.frequency;\r\n"
              "}\r\n") == NULL)) {
    (void)fprintf(stderr, "the active timer state or getter changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/doom/src/i_video.h");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active Doom color type") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_doom_blue_member) ==
          NULL ||
      strstr((const char *)source.contents.data, active_doom_green_member) ==
          NULL ||
      strstr((const char *)source.contents.data, active_doom_red_member) ==
          NULL ||
      strstr((const char *)source.contents.data, active_doom_alpha_member) ==
          NULL) {
    (void)fprintf(stderr, "the active Doom color type changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/doom/src/i_video.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active Doom color reads") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_doom_red_read) ==
          NULL ||
      strstr((const char *)source.contents.data, active_doom_green_read) ==
          NULL ||
      strstr((const char *)source.contents.data, active_doom_blue_read) ==
          NULL) {
    (void)fprintf(stderr, "the active Doom color reads changed\n");
    return 0;
  }
  path.text = ctool_string("/drivers/ata.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active ATA source") ||
      source.contents.data == NULL ||
      (strstr((const char *)source.contents.data,
              active_ata_read_advance_lf) == NULL &&
       strstr((const char *)source.contents.data,
              active_ata_read_advance_crlf) == NULL) ||
      (strstr((const char *)source.contents.data,
              active_ata_write_advance_lf) == NULL &&
       strstr((const char *)source.contents.data,
              active_ata_write_advance_crlf) == NULL)) {
    (void)fprintf(stderr, "an active ATA pointer advance changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/core/kernel.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active CPU frequency source") ||
      source.contents.data == NULL ||
      (strstr((const char *)source.contents.data, active_cpu_frequency) ==
           NULL &&
       strstr((const char *)source.contents.data,
              active_cpu_frequency_crlf) == NULL)) {
    (void)fprintf(stderr, "the active CPU frequency helper changed\n");
    return 0;
  }
  return 1;
}

static int active_doom_bit_field_writes_are_unchanged(ctool_job_t *job) {
  ctool_path_t path;
  ctool_source_t source;
  ctool_status_t status;
  path.text = ctool_string("/kernel/doom/src/i_video.h");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active Doom video header") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_doom_color_record) ==
          NULL ||
      strstr((const char *)source.contents.data, active_doom_red_field) ==
          NULL) {
    (void)fprintf(stderr, "the active Doom color record changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/doom/src/i_video.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active Doom video source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_doom_red_store) ==
          NULL ||
      strstr((const char *)source.contents.data,
             active_doom_local_red_store) == NULL) {
    (void)fprintf(stderr, "the active Doom bit-field writes changed\n");
    return 0;
  }
  return 1;
}

static char *make_active_fixture(void) {
  static const char prefix[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef int ctool_bool;\n"
      "#define CTOOL_FALSE 0\n"
      "#define CTOOL_TRUE 1\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_helper);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_helper,
                 sizeof(active_helper));
  }
  return text;
}

static char *make_narrow_active_fixture(void) {
  static const char prefix[] =
      "typedef unsigned short ctool_u16;\n"
      "typedef struct { ctool_u16 width_bits; } ctool_x86_operand_t;\n"
      "typedef enum {\n"
      "  X86_OC_NONE = 0,\n"
      "  X86_OC_GPR8, X86_OC_GPR16, X86_OC_GPR32,\n"
      "  X86_OC_RM8, X86_OC_RM16, X86_OC_RM32,\n"
      "  X86_OC_MEM, X86_OC_MEM8, X86_OC_MEM16, X86_OC_MEM32,\n"
      "  X86_OC_MEM48, X86_OC_MEM64, X86_OC_MEM128,\n"
      "  X86_OC_SEGMENT, X86_OC_CONTROL, X86_OC_X87, X86_OC_MMX,\n"
      "  X86_OC_MMX_RM32, X86_OC_MMX_RM64, X86_OC_XMM,\n"
      "  X86_OC_XMM_RM32, X86_OC_XMM_RM64, X86_OC_XMM_RM128,\n"
      "  X86_OC_IMM8, X86_OC_IMM16, X86_OC_IMM32,\n"
      "  X86_OC_REL8, X86_OC_REL16, X86_OC_REL32,\n"
      "  X86_OC_FAR16_16, X86_OC_FAR16_32\n"
      "} x86_operand_class_t;\n";
  static const char suffix[] =
      "static unsigned short narrow_word;\n"
      "unsigned short narrow_file(unsigned short input) {\n"
      "  unsigned short local = input;\n"
      "  narrow_word = local;\n"
      "  return narrow_word;\n"
      "}\n"
      "typedef unsigned short (*narrow_callback_t)(signed char);\n"
      "unsigned short narrow_indirect(narrow_callback_t callback,\n"
      "                               signed char value) {\n"
      "  return callback(value);\n"
      "}\n"
      "int narrow_logic(unsigned char left, unsigned short right) {\n"
      "  return left && right;\n"
      "}\n";
  size_t prefix_size = sizeof(prefix) - 1u;
  size_t asm_size = sizeof(active_asm_lower) - 1u;
  size_t class_size = sizeof(active_x86_class_width) - 1u;
  size_t setter_size = sizeof(active_x86_set_memory_width) - 1u;
  size_t suffix_size = sizeof(suffix);
  size_t size = prefix_size + asm_size + class_size + setter_size +
                suffix_size;
  char *text = (char *)malloc(size);
  size_t offset = 0u;
  if (text == NULL) {
    return NULL;
  }
  (void)memcpy(text + offset, prefix, prefix_size);
  offset += prefix_size;
  (void)memcpy(text + offset, active_asm_lower, asm_size);
  offset += asm_size;
  (void)memcpy(text + offset, active_x86_class_width, class_size);
  offset += class_size;
  (void)memcpy(text + offset, active_x86_set_memory_width, setter_size);
  offset += setter_size;
  (void)memcpy(text + offset, suffix, suffix_size);
  return text;
}

static char *make_narrow_mutation_fixture(void) {
  static const char prefix[] =
      "typedef signed char ctool_i8;\n"
      "typedef unsigned char ctool_u8;\n"
      "typedef signed short ctool_i16;\n"
      "typedef unsigned short ctool_u16;\n"
      "typedef unsigned int ctool_u32;\n"
      "typedef enum { CTOOL_OK = 0, CTOOL_ERR_LIMIT = 1 } ctool_status_t;\n"
      "#define CTOOL_X86_MAX_INSTRUCTION_BYTES 15u\n"
      "typedef struct {\n"
      "  ctool_u8 bytes[CTOOL_X86_MAX_INSTRUCTION_BYTES];\n"
      "  ctool_u8 size;\n"
      "} ctool_x86_encoding_t;\n";
  static const char suffix[] =
      "ctool_u8 all_u8_compounds(ctool_u8 value, ctool_u32 right) {\n"
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
      "ctool_i16 signed_i16_compounds(ctool_i16 value, int right) {\n"
      "  value /= right;\n"
      "  value >>= right;\n"
      "  return value;\n"
      "}\n"
      "ctool_i8 prefix_i8(ctool_i8 value) { return ++value; }\n"
      "ctool_u8 prefix_u8(ctool_u8 value) { return --value; }\n"
      "ctool_i16 postfix_i16(ctool_i16 value) { return value++; }\n"
      "ctool_u16 postfix_u16(ctool_u16 value) { return value--; }\n"
      "volatile ctool_u8 volatile_byte;\n"
      "ctool_u8 volatile_postfix(void) { return volatile_byte++; }\n"
      "struct decoded_instruction { ctool_u8 prefixes; };\n"
      "struct decoded_value { struct decoded_instruction instruction; };\n"
      "ctool_u8 add_prefix(struct decoded_value *decoded, ctool_u8 flag) {\n"
      "  return decoded->instruction.prefixes |= flag;\n"
      "}\n";
  size_t prefix_size = sizeof(prefix) - 1u;
  size_t active_size = sizeof(active_x86_put_u8) - 1u;
  size_t suffix_size = sizeof(suffix);
  size_t size = prefix_size + active_size + suffix_size;
  size_t offset = 0u;
  char *text = (char *)malloc(size);
  if (text == NULL) {
    return NULL;
  }
  (void)memcpy(text + offset, prefix, prefix_size);
  offset += prefix_size;
  (void)memcpy(text + offset, active_x86_put_u8, active_size);
  offset += active_size;
  (void)memcpy(text + offset, suffix, suffix_size);
  return text;
}

static char *make_logic_fixture(void) {
  static const char prefix[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef int ctool_bool;\n"
      "#define CTOOL_FALSE 0\n"
      "#define CTOOL_TRUE 1\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_power_of_two);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_power_of_two,
                 sizeof(active_power_of_two));
  }
  return text;
}

static char *make_division_fixture(void) {
  static const char prefix[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef int ctool_bool;\n"
      "#define CTOOL_FALSE 0\n"
      "#define CTOOL_TRUE 1\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_multiply_overflows);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_multiply_overflows,
                 sizeof(active_multiply_overflows));
  }
  return text;
}

static char *make_logical_or_fixture(void) {
  static const char prefix[] =
      "typedef int ctool_bool;\n"
      "#define CTOOL_FALSE 0\n"
      "#define CTOOL_TRUE 1\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_bool_valid);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_bool_valid,
                 sizeof(active_bool_valid));
  }
  return text;
}

static char *make_branch_fit_fixture(void) {
  static const char prefix[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef int ctool_bool;\n"
      "#define CTOOL_FALSE 0\n"
      "#define CTOOL_TRUE 1\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_branch_fits);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_branch_fits,
                 sizeof(active_branch_fits));
  }
  return text;
}

static char *make_aes_rotw_fixture(void) {
  static const char prefix[] = "typedef unsigned int uint32_t;\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_aes_rotw);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_aes_rotw,
                 sizeof(active_aes_rotw));
  }
  return text;
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

static char *make_simd_cpuid_fixture(void) {
  static const char prefix[] =
      "typedef unsigned int uint32_t;\n"
      "typedef enum { false = 0, true = 1 } bool;\n"
      "static bool simd_cpuid_changed(uint32_t before, uint32_t after) {\n";
  static const char suffix[] = "\n}\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_simd_cpuid_return) - 1u +
                sizeof(suffix);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    size_t offset = sizeof(prefix) - 1u;
    (void)memcpy(text, prefix, offset);
    (void)memcpy(text + offset, active_simd_cpuid_return,
                 sizeof(active_simd_cpuid_return) - 1u);
    offset += sizeof(active_simd_cpuid_return) - 1u;
    (void)memcpy(text + offset, suffix, sizeof(suffix));
  }
  return text;
}

static char *make_call_fixture(void) {
  static const char prefix[] =
      "typedef unsigned int uint32_t;\n"
      "uint32_t process_get_current_pid(void);\n";
  static const char suffix[] =
      "\nint external_sum(int left, int right);\n"
      "int forward_sum(int left, int right) {\n"
      "  return external_sum(left, right);\n"
      "}\n"
      "static int local_target(void) { return 9; }\n"
      "int call_local(void) { return local_target(); }\n"
      "extern void external_sink(int value);\n"
      "void call_void(int value) { external_sink(value); }\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_call) - 1u +
                sizeof(suffix);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    size_t offset = 0u;
    (void)memcpy(text + offset, prefix, sizeof(prefix) - 1u);
    offset += sizeof(prefix) - 1u;
    (void)memcpy(text + offset, active_call, sizeof(active_call) - 1u);
    offset += sizeof(active_call) - 1u;
    (void)memcpy(text + offset, suffix, sizeof(suffix));
  }
  return text;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size) {
  const unsigned char *bytes = (const unsigned char *)data;
  size_t index;
  for (index = 0u; index < size; index++) {
    hash ^= bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static uint64_t hash_u32(uint64_t hash, ctool_u32 value) {
  ctool_u8 bytes[4];
  bytes[0] = (ctool_u8)(value & 0xffu);
  bytes[1] = (ctool_u8)((value >> 8u) & 0xffu);
  bytes[2] = (ctool_u8)((value >> 16u) & 0xffu);
  bytes[3] = (ctool_u8)((value >> 24u) & 0xffu);
  return hash_bytes(hash, bytes, sizeof(bytes));
}

static uint64_t hash_u64(uint64_t hash, ctool_u64 value) {
  ctool_u32 index;
  for (index = 0u; index < 8u; index++) {
    ctool_u8 byte = (ctool_u8)((value >> (index * 8u)) & 0xffu);
    hash = hash_bytes(hash, &byte, sizeof(byte));
  }
  return hash;
}

static uint64_t hash_location(uint64_t hash,
                              const ctool_c_pp_location_t *location) {
  hash = hash_u32(hash, location->path.size);
  hash = hash_bytes(hash, location->path.data, location->path.size);
  hash = hash_u32(hash, location->line);
  return hash_u32(hash, location->column);
}

static uint64_t ir_instruction_slice_fingerprint(
    const ctool_c_ir_unit_t *ir, ctool_u32 first, ctool_u32 count) {
  uint64_t hash = UINT64_C(1469598103934665603);
  ctool_u32 index;
  if (ir == NULL || ir->instructions == NULL || first > ir->instruction_count ||
      count > ir->instruction_count - first) {
    return 0u;
  }
  hash = hash_u32(hash, count);
  for (index = 0u; index < count; index++) {
    const ctool_c_ir_instruction_t *instruction =
        &ir->instructions[first + index];
    hash = hash_u32(hash, (ctool_u32)instruction->kind);
    hash = hash_u32(hash, instruction->type);
    hash = hash_u32(hash, instruction->input_type);
    hash = hash_u32(hash, (ctool_u32)instruction->operation);
    hash = hash_u32(hash, (ctool_u32)instruction->conversion);
    hash = hash_u32(hash, instruction->reference);
    hash = hash_u64(hash, instruction->integer_bits);
    hash = hash_location(hash, &instruction->location);
    hash = hash_location(hash, &instruction->physical_location);
  }
  return hash;
}

static uint64_t ir_instruction_fingerprint(const ctool_c_ir_unit_t *ir) {
  return ir == NULL
             ? 0u
             : ir_instruction_slice_fingerprint(
                   ir, 0u, ir->instruction_count);
}

static uint64_t unit_fingerprint(const ctool_c_translation_unit_t *unit) {
  uint64_t hash = UINT64_C(1469598103934665603);
  hash = hash_bytes(hash, unit, sizeof(*unit));
  hash = hash_bytes(hash, unit->graph.types,
                    (size_t)unit->graph.type_count *
                        sizeof(*unit->graph.types));
  hash = hash_bytes(hash, unit->graph.members,
                    (size_t)unit->graph.member_count *
                        sizeof(*unit->graph.members));
  hash = hash_bytes(hash, unit->graph.parameter_types,
                    (size_t)unit->graph.parameter_type_count *
                        sizeof(*unit->graph.parameter_types));
  hash = hash_bytes(hash, unit->layout.types,
                    (size_t)unit->layout.type_count *
                        sizeof(*unit->layout.types));
  hash = hash_bytes(hash, unit->layout.members,
                    (size_t)unit->layout.member_count *
                        sizeof(*unit->layout.members));
  hash = hash_bytes(hash, unit->bindings,
                    (size_t)unit->binding_count * sizeof(*unit->bindings));
  hash = hash_bytes(hash, unit->parameters,
                    (size_t)unit->parameter_count * sizeof(*unit->parameters));
  hash = hash_bytes(hash, unit->block_bindings,
                    (size_t)unit->block_binding_count *
                        sizeof(*unit->block_bindings));
  hash = hash_bytes(hash, unit->initializers,
                    (size_t)unit->initializer_count *
                        sizeof(*unit->initializers));
  hash = hash_bytes(hash, unit->initializer_elements,
                    (size_t)unit->initializer_element_count *
                        sizeof(*unit->initializer_elements));
  hash = hash_bytes(hash, unit->labels,
                    (size_t)unit->label_count * sizeof(*unit->labels));
  hash = hash_bytes(hash, unit->function_definitions,
                    (size_t)unit->function_definition_count *
                        sizeof(*unit->function_definitions));
  hash = hash_bytes(hash, unit->statements,
                    (size_t)unit->statement_count * sizeof(*unit->statements));
  hash = hash_bytes(hash, unit->statement_children,
                    (size_t)unit->statement_child_count *
                        sizeof(*unit->statement_children));
  hash = hash_bytes(hash, unit->expressions,
                    (size_t)unit->expression_count *
                        sizeof(*unit->expressions));
  hash = hash_bytes(hash, unit->expression_children,
                    (size_t)unit->expression_child_count *
                        sizeof(*unit->expression_children));
  return hash;
}

static int ir_is_zero(const ctool_c_ir_unit_t *ir) {
  const unsigned char *bytes = (const unsigned char *)ir;
  size_t index;
  for (index = 0u; index < sizeof(*ir); index++) {
    if (bytes[index] != 0u) {
      return 0;
    }
  }
  return 1;
}

static int expect_new_diagnostic(const ctool_job_t *job, ctool_u32 before,
                                 ctool_u32 code, const char *message,
                                 const char *context) {
  const ctool_diagnostic_t *diagnostic;
  if (ctool_job_diagnostic_count(job) != before + 1u) {
    (void)fprintf(stderr, "%s: expected one diagnostic\n", context);
    return 0;
  }
  diagnostic = ctool_job_diagnostic(job, before);
  if (diagnostic == NULL || diagnostic->severity != CTOOL_DIAG_ERROR ||
      diagnostic->code != code ||
      (message != NULL && string_equal(diagnostic->message, message) == 0)) {
    (void)fprintf(stderr, "%s: diagnostic differs\n", context);
    return 0;
  }
  return 1;
}

static int expect_ir_failure(ctool_job_t *job,
                             const ctool_c_translation_unit_t *unit,
                             ctool_status_t expected_status,
                             ctool_u32 expected_code,
                             const char *expected_message,
                             const char *context) {
  ctool_c_ir_unit_t ir;
  ctool_u32 diagnostic_count = ctool_job_diagnostic_count(job);
  ctool_arena_mark_t mark = ctool_arena_mark(ctool_job_arena(job));
  ctool_status_t status;
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, unit, &ir);
  if (!check_status(status, expected_status, context) || ir_is_zero(&ir) == 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      !expect_new_diagnostic(job, diagnostic_count, expected_code,
                             expected_message, context)) {
    (void)fprintf(stderr, "%s: failure transaction differs\n", context);
    return 0;
  }
  return 1;
}

static int expect_ir_failure_preserves_unit(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_status_t expected_status, ctool_u32 expected_code,
    const char *expected_message, const char *context) {
  uint64_t fingerprint = unit_fingerprint(unit);
  return expect_ir_failure(job, unit, expected_status, expected_code,
                           expected_message, context) &&
                 unit_fingerprint(unit) == fingerprint
             ? 1
             : 0;
}

static int instruction_matches(const ctool_c_ir_instruction_t *instruction,
                               ctool_c_ir_instruction_kind_t kind,
                               ctool_u32 type, ctool_u32 input_type,
                               ctool_c_expression_operator_t operation,
                               ctool_c_conversion_kind_t conversion,
                               ctool_u32 reference, ctool_u64 integer_bits) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == operation &&
                 instruction->conversion == conversion &&
                 instruction->reference == reference &&
                 instruction->integer_bits == integer_bits &&
                 string_equal(instruction->location.path,
                              "/active-cemit-add-overflows.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/active-cemit-add-overflows.c") != 0
             ? 1
             : 0;
}

static int validate_active_ir(const ctool_c_translation_unit_t *unit,
                              const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 first_parameter;
  ctool_u32 unsigned_type;
  ctool_u32 result_type;
  ctool_u32 index;

  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instruction_count != 12u ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "active IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      unit->parameter_count < 2u ||
      function_type->first_parameter > unit->parameter_count - 2u) {
    (void)fprintf(stderr, "active function type differs\n");
    return 0;
  }
  first_parameter = function_type->first_parameter;
  unsigned_type = unit->parameters[first_parameter].type;
  result_type = function_type->referenced_type;
  function = &ir->functions[0];
  instructions = ir->instructions + function->first_instruction;
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u || function->instruction_count != 12u ||
      function->maximum_stack_depth != 3u ||
      !string_equal(function->location.path,
                    "/active-cemit-add-overflows.c")) {
    (void)fprintf(stderr, "active IR function record differs\n");
    return 0;
  }
  if (!instruction_matches(
          &instructions[0], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          first_parameter, 0u) ||
      !instruction_matches(
          &instructions[1], CTOOL_C_IR_INSTRUCTION_LOAD, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !instruction_matches(
          &instructions[2], CTOOL_C_IR_INSTRUCTION_INTEGER, unsigned_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, UINT32_MAX) ||
      !instruction_matches(
          &instructions[3], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          first_parameter + 1u, 0u) ||
      !instruction_matches(
          &instructions[4], CTOOL_C_IR_INSTRUCTION_LOAD, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !instruction_matches(
          &instructions[5], CTOOL_C_IR_INSTRUCTION_BINARY, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !instruction_matches(
          &instructions[6], CTOOL_C_IR_INSTRUCTION_BINARY, result_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_GREATER,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !instruction_matches(
          &instructions[7], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 10u, 0u) ||
      !instruction_matches(
          &instructions[8], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u) ||
      !instruction_matches(
          &instructions[9], CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 11u, 0u) ||
      !instruction_matches(
          &instructions[10], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !instruction_matches(
          &instructions[11], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          result_type, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u)) {
    (void)fprintf(stderr, "active IR instruction stream differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    if (instructions[index].location.line == 0u ||
        instructions[index].physical_location.line == 0u) {
      (void)fprintf(stderr, "active IR lost source locations\n");
      return 0;
    }
  }
  return 1;
}

static int logic_instruction_matches(
    const ctool_c_ir_instruction_t *instruction,
    ctool_c_ir_instruction_kind_t kind, ctool_u32 type,
    ctool_u32 input_type, ctool_c_expression_operator_t operation,
    ctool_u32 reference, ctool_u64 integer_bits) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == operation &&
                 instruction->conversion == CTOOL_C_CONVERSION_NONE &&
                 instruction->reference == reference &&
                 instruction->integer_bits == integer_bits &&
                 string_equal(instruction->location.path,
                              "/active-cemit-power-of-two.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/active-cemit-power-of-two.c") != 0
             ? 1
             : 0;
}

static int validate_logic_ir(const ctool_c_translation_unit_t *unit,
                             const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 parameter;
  ctool_u32 unsigned_type;
  ctool_u32 result_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instruction_count != 23u ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "logic IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "logic function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  unsigned_type = unit->parameters[parameter].type;
  result_type = function_type->referenced_type;
  function = &ir->functions[0];
  instructions = ir->instructions + function->first_instruction;
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 23u ||
      function->maximum_stack_depth != 3u ||
      !logic_instruction_matches(
          &instructions[0], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, parameter, 0u) ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != unsigned_type ||
      instructions[1].input_type != unsigned_type ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      !logic_instruction_matches(
          &instructions[2], CTOOL_C_IR_INSTRUCTION_INTEGER, unsigned_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[3], CTOOL_C_IR_INSTRUCTION_BINARY, result_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[4], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          17u, 0u) ||
      !logic_instruction_matches(
          &instructions[5], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, parameter, 0u) ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[6].type != unsigned_type ||
      instructions[6].input_type != unsigned_type ||
      instructions[6].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      !logic_instruction_matches(
          &instructions[7], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, parameter, 0u) ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[8].type != unsigned_type ||
      instructions[8].input_type != unsigned_type ||
      instructions[8].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      !logic_instruction_matches(
          &instructions[9], CTOOL_C_IR_INSTRUCTION_INTEGER, unsigned_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 1u) ||
      !logic_instruction_matches(
          &instructions[10], CTOOL_C_IR_INSTRUCTION_BINARY, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[11], CTOOL_C_IR_INSTRUCTION_BINARY, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[12], CTOOL_C_IR_INSTRUCTION_INTEGER, unsigned_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[13], CTOOL_C_IR_INSTRUCTION_BINARY, result_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_EQUAL,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[14], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          17u, 0u) ||
      !logic_instruction_matches(
          &instructions[15], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 1u) ||
      !logic_instruction_matches(
          &instructions[16], CTOOL_C_IR_INSTRUCTION_JUMP,
          CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, 18u, 0u) ||
      !logic_instruction_matches(
          &instructions[17], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[18], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          21u, 0u) ||
      !logic_instruction_matches(
          &instructions[19], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 1u) ||
      !logic_instruction_matches(
          &instructions[20], CTOOL_C_IR_INSTRUCTION_JUMP,
          CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, 22u, 0u) ||
      !logic_instruction_matches(
          &instructions[21], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[22], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          result_type, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 0u)) {
    (void)fprintf(stderr, "logic IR instruction stream differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    if (instructions[index].location.line == 0u ||
        instructions[index].physical_location.line == 0u) {
      (void)fprintf(stderr, "logic IR lost source locations\n");
      return 0;
    }
  }
  return 1;
}

static int division_instruction_matches(
    const ctool_c_ir_instruction_t *instruction,
    ctool_c_ir_instruction_kind_t kind, ctool_u32 type,
    ctool_u32 input_type, ctool_c_expression_operator_t operation,
    ctool_c_conversion_kind_t conversion, ctool_u32 reference,
    ctool_u64 integer_bits) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == operation &&
                 instruction->conversion == conversion &&
                 instruction->reference == reference &&
                 instruction->integer_bits == integer_bits &&
                 string_equal(instruction->location.path,
                              "/active-cemit-multiply-overflows.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/active-cemit-multiply-overflows.c") != 0
             ? 1
             : 0;
}

static int validate_division_ir(const ctool_c_translation_unit_t *unit,
                                const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 left_parameter;
  ctool_u32 right_parameter;
  ctool_u32 unsigned_type;
  ctool_u32 result_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instruction_count != 21u ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "division IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      function_type->first_parameter > unit->parameter_count ||
      function_type->parameter_count >
          unit->parameter_count - function_type->first_parameter) {
    (void)fprintf(stderr, "division function type differs\n");
    return 0;
  }
  left_parameter = function_type->first_parameter;
  right_parameter = left_parameter + 1u;
  unsigned_type = unit->parameters[left_parameter].type;
  result_type = function_type->referenced_type;
  function = &ir->functions[0];
  instructions = ir->instructions + function->first_instruction;
  if (unit->parameters[right_parameter].type != unsigned_type ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 21u ||
      function->maximum_stack_depth != 3u ||
      !division_instruction_matches(
          &instructions[0], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          left_parameter, 0u) ||
      !division_instruction_matches(
          &instructions[1], CTOOL_C_IR_INSTRUCTION_LOAD, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[2], CTOOL_C_IR_INSTRUCTION_INTEGER, unsigned_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[3], CTOOL_C_IR_INSTRUCTION_BINARY, result_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[4], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 15u, 0u) ||
      !division_instruction_matches(
          &instructions[5], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          right_parameter, 0u) ||
      !division_instruction_matches(
          &instructions[6], CTOOL_C_IR_INSTRUCTION_LOAD, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[7], CTOOL_C_IR_INSTRUCTION_INTEGER, unsigned_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0xffffffffu) ||
      !division_instruction_matches(
          &instructions[8], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          left_parameter, 0u) ||
      !division_instruction_matches(
          &instructions[9], CTOOL_C_IR_INSTRUCTION_LOAD, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[10], CTOOL_C_IR_INSTRUCTION_BINARY, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_DIVIDE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[11], CTOOL_C_IR_INSTRUCTION_BINARY, result_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_GREATER,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[12], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 15u, 0u) ||
      !division_instruction_matches(
          &instructions[13], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u) ||
      !division_instruction_matches(
          &instructions[14], CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 16u, 0u) ||
      !division_instruction_matches(
          &instructions[15], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[16], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 19u, 0u) ||
      !division_instruction_matches(
          &instructions[17], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u) ||
      !division_instruction_matches(
          &instructions[18], CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 20u, 0u) ||
      !division_instruction_matches(
          &instructions[19], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[20], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          result_type, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u)) {
    (void)fprintf(stderr, "division IR instruction stream differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    if (instructions[index].location.line == 0u ||
        instructions[index].physical_location.line == 0u) {
      (void)fprintf(stderr, "division IR lost source locations\n");
      return 0;
    }
  }
  return 1;
}

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  ctool_bool has_type;
  ctool_bool has_input_type;
  ctool_c_expression_operator_t operation;
  ctool_c_conversion_kind_t conversion;
  ctool_u32 reference;
  ctool_u64 integer_bits;
} logical_or_expected_t;

static int validate_logical_or_ir(const ctool_c_translation_unit_t *unit,
                                  const ctool_c_ir_unit_t *ir) {
  const ctool_u32 parameter_reference = CTOOL_C_AST_NONE - 1u;
  static const logical_or_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, CTOOL_TRUE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, CTOOL_TRUE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_EQUAL, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_FALSE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 7u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_FALSE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 15u, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, CTOOL_TRUE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, CTOOL_TRUE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_EQUAL, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_FALSE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 14u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_FALSE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 15u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_FALSE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 18u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_FALSE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 19u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, CTOOL_TRUE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u}};
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 parameter;
  ctool_u32 result_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "logical-or IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "logical-or function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  result_type = function_type->referenced_type;
  function = &ir->functions[0];
  instructions = ir->instructions + function->first_instruction;
  if (unit->parameters[parameter].type != result_type ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      function->maximum_stack_depth != 2u) {
    (void)fprintf(stderr, "logical-or IR function record differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const logical_or_expected_t *wanted = &expected[index];
    const ctool_c_ir_instruction_t *actual = &instructions[index];
    ctool_u32 reference = wanted->reference == parameter_reference
                              ? parameter
                              : wanted->reference;
    if (actual->kind != wanted->kind ||
        actual->type !=
            (wanted->has_type == CTOOL_TRUE ? result_type
                                             : CTOOL_C_TYPE_NONE) ||
        actual->input_type !=
            (wanted->has_input_type == CTOOL_TRUE ? result_type
                                                   : CTOOL_C_TYPE_NONE) ||
        actual->operation != wanted->operation ||
        actual->conversion != wanted->conversion ||
        actual->reference != reference ||
        actual->integer_bits != wanted->integer_bits ||
        string_equal(actual->location.path,
                     "/active-cfront-bool-valid.c") == 0 ||
        string_equal(actual->physical_location.path,
                     "/active-cfront-bool-valid.c") == 0 ||
        actual->location.line == 0u ||
        actual->physical_location.line == 0u) {
      (void)fprintf(stderr, "logical-or IR instruction %u differs\n",
                    index);
      return 0;
    }
  }
  return 1;
}

typedef enum {
  BRANCH_EXPECT_NONE = 0,
  BRANCH_EXPECT_PARAMETER,
  BRANCH_EXPECT_RESULT
} branch_expected_type_t;

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  branch_expected_type_t type;
  branch_expected_type_t input_type;
  ctool_c_expression_operator_t operation;
  ctool_c_conversion_kind_t conversion;
  ctool_u32 reference;
  ctool_u64 integer_bits;
} branch_expected_t;

static int validate_branch_fit_ir(const ctool_c_translation_unit_t *unit,
                                  const ctool_c_ir_unit_t *ir) {
  const ctool_u32 parameter_reference = CTOOL_C_AST_NONE - 1u;
  static const branch_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, BRANCH_EXPECT_PARAMETER,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, BRANCH_EXPECT_PARAMETER,
       BRANCH_EXPECT_PARAMETER, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_PARAMETER,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0x7fu},
      {CTOOL_C_IR_INSTRUCTION_BINARY, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_PARAMETER, CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, BRANCH_EXPECT_NONE,
       BRANCH_EXPECT_RESULT, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, 7u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, BRANCH_EXPECT_NONE, BRANCH_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 15u, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, BRANCH_EXPECT_PARAMETER,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, BRANCH_EXPECT_PARAMETER,
       BRANCH_EXPECT_PARAMETER, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_PARAMETER,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0xffffff80u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_PARAMETER, CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, BRANCH_EXPECT_NONE,
       BRANCH_EXPECT_RESULT, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, 14u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, BRANCH_EXPECT_NONE, BRANCH_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 15u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, BRANCH_EXPECT_NONE,
       BRANCH_EXPECT_RESULT, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, 18u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, BRANCH_EXPECT_NONE, BRANCH_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 19u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_RESULT, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u}};
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  ctool_u32 parameter;
  ctool_u32 parameter_type;
  ctool_u32 result_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0]))) {
    (void)fprintf(stderr, "branch-range IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "branch-range function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  parameter_type = unit->parameters[parameter].type;
  result_type = function_type->referenced_type;
  function = &ir->functions[0];
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      function->maximum_stack_depth != 2u) {
    (void)fprintf(stderr, "branch-range IR function record differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const branch_expected_t *wanted = &expected[index];
    const ctool_c_ir_instruction_t *actual = &ir->instructions[index];
    ctool_u32 wanted_type = wanted->type == BRANCH_EXPECT_PARAMETER
                                ? parameter_type
                                : wanted->type == BRANCH_EXPECT_RESULT
                                      ? result_type
                                      : CTOOL_C_TYPE_NONE;
    ctool_u32 wanted_input = wanted->input_type == BRANCH_EXPECT_PARAMETER
                                 ? parameter_type
                                 : wanted->input_type == BRANCH_EXPECT_RESULT
                                       ? result_type
                                       : CTOOL_C_TYPE_NONE;
    ctool_u32 wanted_reference =
        wanted->reference == parameter_reference ? parameter
                                                  : wanted->reference;
    if (actual->kind != wanted->kind || actual->type != wanted_type ||
        actual->input_type != wanted_input ||
        actual->operation != wanted->operation ||
        actual->conversion != wanted->conversion ||
        actual->reference != wanted_reference ||
        actual->integer_bits != wanted->integer_bits ||
        string_equal(actual->location.path,
                     "/active-asm-branch-fits-i8.c") == 0 ||
        string_equal(actual->physical_location.path,
                     "/active-asm-branch-fits-i8.c") == 0 ||
        actual->location.line == 0u || actual->physical_location.line == 0u) {
      (void)fprintf(stderr, "branch-range IR instruction %u differs\n",
                    index);
      return 0;
    }
  }
  return 1;
}

typedef enum {
  ROTW_EXPECT_NONE = 0,
  ROTW_EXPECT_VALUE,
  ROTW_EXPECT_COUNT
} rotw_expected_type_t;

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  rotw_expected_type_t type;
  rotw_expected_type_t input_type;
  ctool_c_expression_operator_t operation;
  ctool_c_conversion_kind_t conversion;
  ctool_u32 reference;
  ctool_u64 integer_bits;
} rotw_expected_t;

static int validate_aes_rotw_ir(const ctool_c_translation_unit_t *unit,
                                const ctool_c_ir_unit_t *ir) {
  const ctool_u32 parameter_reference = CTOOL_C_AST_NONE - 1u;
  static const rotw_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, ROTW_EXPECT_VALUE,
       ROTW_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, ROTW_EXPECT_VALUE, ROTW_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, ROTW_EXPECT_COUNT, ROTW_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 8u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, ROTW_EXPECT_VALUE, ROTW_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, ROTW_EXPECT_VALUE,
       ROTW_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, ROTW_EXPECT_VALUE, ROTW_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, ROTW_EXPECT_COUNT, ROTW_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 24u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, ROTW_EXPECT_VALUE, ROTW_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, ROTW_EXPECT_VALUE, ROTW_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, ROTW_EXPECT_VALUE,
       ROTW_EXPECT_VALUE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u}};
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  ctool_u32 parameter;
  ctool_u32 value_type;
  ctool_u32 count_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0]))) {
    (void)fprintf(stderr, "AES word-rotation IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "AES word-rotation function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  count_type = ir->instructions[2].type;
  if (function_type->referenced_type != value_type ||
      value_type >= unit->layout.type_count ||
      count_type >= unit->layout.type_count ||
      unit->layout.types[value_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[value_type].size != 4u ||
      unit->layout.types[value_type].is_signed != CTOOL_FALSE ||
      unit->layout.types[count_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[count_type].size != 4u ||
      unit->layout.types[count_type].is_signed != CTOOL_TRUE ||
      ir->instructions[6].type != count_type) {
    (void)fprintf(stderr, "AES word-rotation operand types differ\n");
    return 0;
  }
  function = &ir->functions[0];
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      function->maximum_stack_depth != 3u) {
    (void)fprintf(stderr, "AES word-rotation IR function record differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const rotw_expected_t *wanted = &expected[index];
    const ctool_c_ir_instruction_t *actual = &ir->instructions[index];
    ctool_u32 wanted_type = wanted->type == ROTW_EXPECT_VALUE
                                ? value_type
                                : wanted->type == ROTW_EXPECT_COUNT
                                      ? count_type
                                      : CTOOL_C_TYPE_NONE;
    ctool_u32 wanted_input = wanted->input_type == ROTW_EXPECT_VALUE
                                 ? value_type
                                 : wanted->input_type == ROTW_EXPECT_COUNT
                                       ? count_type
                                       : CTOOL_C_TYPE_NONE;
    ctool_u32 wanted_reference =
        wanted->reference == parameter_reference ? parameter
                                                  : wanted->reference;
    if (actual->kind != wanted->kind || actual->type != wanted_type ||
        actual->input_type != wanted_input ||
        actual->operation != wanted->operation ||
        actual->conversion != wanted->conversion ||
        actual->reference != wanted_reference ||
        actual->integer_bits != wanted->integer_bits ||
        string_equal(actual->location.path, "/active-aes-rotw.c") == 0 ||
        string_equal(actual->physical_location.path,
                     "/active-aes-rotw.c") == 0 ||
        actual->location.line == 0u || actual->physical_location.line == 0u) {
      (void)fprintf(stderr, "AES word-rotation IR instruction %u differs\n",
                    index);
      return 0;
    }
  }
  return 1;
}

typedef enum {
  ALIGN_EXPECT_NONE = 0,
  ALIGN_EXPECT_VALUE,
  ALIGN_EXPECT_COUNT
} align_expected_type_t;

typedef enum {
  ALIGN_REFERENCE_NONE = 0,
  ALIGN_REFERENCE_VALUE,
  ALIGN_REFERENCE_ALIGNMENT
} align_expected_reference_t;

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  align_expected_type_t type;
  align_expected_type_t input_type;
  ctool_c_expression_operator_t operation;
  ctool_c_conversion_kind_t conversion;
  align_expected_reference_t reference;
  ctool_u64 integer_bits;
} align_expected_t;

static int validate_align_up_ir(const ctool_c_translation_unit_t *unit,
                                const ctool_c_ir_unit_t *ir) {
  static const align_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, ALIGN_EXPECT_VALUE,
       ALIGN_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, ALIGN_REFERENCE_VALUE, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, ALIGN_EXPECT_VALUE, ALIGN_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
       ALIGN_REFERENCE_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, ALIGN_EXPECT_VALUE,
       ALIGN_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, ALIGN_REFERENCE_ALIGNMENT, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, ALIGN_EXPECT_VALUE, ALIGN_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
       ALIGN_REFERENCE_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, ALIGN_EXPECT_VALUE, ALIGN_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_ADD, CTOOL_C_CONVERSION_NONE,
       ALIGN_REFERENCE_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, ALIGN_EXPECT_COUNT, ALIGN_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       ALIGN_REFERENCE_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, ALIGN_EXPECT_VALUE, ALIGN_EXPECT_COUNT,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_USUAL_ARITHMETIC, ALIGN_REFERENCE_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, ALIGN_EXPECT_VALUE, ALIGN_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT, CTOOL_C_CONVERSION_NONE,
       ALIGN_REFERENCE_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, ALIGN_EXPECT_VALUE,
       ALIGN_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, ALIGN_REFERENCE_ALIGNMENT, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, ALIGN_EXPECT_VALUE, ALIGN_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
       ALIGN_REFERENCE_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, ALIGN_EXPECT_COUNT, ALIGN_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       ALIGN_REFERENCE_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, ALIGN_EXPECT_VALUE, ALIGN_EXPECT_COUNT,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_USUAL_ARITHMETIC, ALIGN_REFERENCE_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, ALIGN_EXPECT_VALUE, ALIGN_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT, CTOOL_C_CONVERSION_NONE,
       ALIGN_REFERENCE_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_UNARY, ALIGN_EXPECT_VALUE, ALIGN_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT, CTOOL_C_CONVERSION_NONE,
       ALIGN_REFERENCE_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, ALIGN_EXPECT_VALUE, ALIGN_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND, CTOOL_C_CONVERSION_NONE,
       ALIGN_REFERENCE_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, ALIGN_EXPECT_VALUE,
       ALIGN_EXPECT_VALUE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, ALIGN_REFERENCE_NONE, 0u}};
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  ctool_u32 value_parameter;
  ctool_u32 alignment_parameter;
  ctool_u32 value_type;
  ctool_u32 count_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0]))) {
    (void)fprintf(stderr, "memory alignment IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      function_type->first_parameter >= unit->parameter_count ||
      unit->parameter_count - function_type->first_parameter < 2u) {
    (void)fprintf(stderr, "memory alignment function type differs\n");
    return 0;
  }
  value_parameter = function_type->first_parameter;
  alignment_parameter = value_parameter + 1u;
  value_type = unit->parameters[value_parameter].type;
  count_type = ir->instructions[5].type;
  if (unit->parameters[alignment_parameter].type != value_type ||
      function_type->referenced_type != value_type ||
      value_type >= unit->layout.type_count ||
      count_type >= unit->layout.type_count ||
      unit->layout.types[value_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[value_type].size != 4u ||
      unit->layout.types[value_type].is_signed != CTOOL_FALSE ||
      unit->layout.types[count_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[count_type].size != 4u ||
      unit->layout.types[count_type].is_signed != CTOOL_TRUE) {
    (void)fprintf(stderr, "memory alignment operand types differ\n");
    return 0;
  }
  function = &ir->functions[0];
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      function->maximum_stack_depth != 3u) {
    (void)fprintf(stderr, "memory alignment IR function record differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const align_expected_t *wanted = &expected[index];
    const ctool_c_ir_instruction_t *actual = &ir->instructions[index];
    ctool_u32 wanted_type = wanted->type == ALIGN_EXPECT_VALUE
                                ? value_type
                                : wanted->type == ALIGN_EXPECT_COUNT
                                      ? count_type
                                      : CTOOL_C_TYPE_NONE;
    ctool_u32 wanted_input = wanted->input_type == ALIGN_EXPECT_VALUE
                                 ? value_type
                                 : wanted->input_type == ALIGN_EXPECT_COUNT
                                       ? count_type
                                       : CTOOL_C_TYPE_NONE;
    ctool_u32 wanted_reference = CTOOL_C_AST_NONE;
    if (wanted->reference == ALIGN_REFERENCE_VALUE) {
      wanted_reference = value_parameter;
    } else if (wanted->reference == ALIGN_REFERENCE_ALIGNMENT) {
      wanted_reference = alignment_parameter;
    }
    if (actual->kind != wanted->kind || actual->type != wanted_type ||
        actual->input_type != wanted_input ||
        actual->operation != wanted->operation ||
        actual->conversion != wanted->conversion ||
        actual->reference != wanted_reference ||
        actual->integer_bits != wanted->integer_bits ||
        string_equal(actual->location.path,
                     "/active-memory-align-up.c") == 0 ||
        string_equal(actual->physical_location.path,
                     "/active-memory-align-up.c") == 0 ||
        actual->location.line == 0u || actual->physical_location.line == 0u) {
      (void)fprintf(stderr, "memory alignment IR instruction %u differs\n",
                    index);
      return 0;
    }
  }
  return 1;
}

static int validate_integer_unary_ir(const ctool_c_translation_unit_t *unit,
                                     const ctool_c_ir_unit_t *ir) {
  static const char *const names[] = {
      "unary_plus", "signed_negate", "unsigned_negate", "logical_not"};
  static const ctool_c_expression_operator_t operations[] = {
      CTOOL_C_EXPRESSION_OPERATOR_UNARY_PLUS,
      CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE,
      CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE,
      CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_NOT};
  static const ctool_bool input_signed[] = {
      CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE};
  static const ctool_bool result_signed[] = {
      CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_TRUE};
  ctool_u32 index;
  if (unit->function_definition_count != 4u || ir->function_count != 4u ||
      ir->instruction_count != 16u || ir->functions == NULL ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "integer unary IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < 4u; index++) {
    const ctool_c_function_definition_t *definition =
        &unit->function_definitions[index];
    const ctool_c_type_node_t *function_type;
    const ctool_c_ir_function_t *function = &ir->functions[index];
    const ctool_c_ir_instruction_t *instructions =
        &ir->instructions[index * 4u];
    ctool_u32 parameter;
    ctool_u32 input_type;
    ctool_u32 result_type;
    ctool_u32 instruction_index;
    if (definition->binding >= unit->binding_count ||
        definition->declared_type >= unit->graph.type_count ||
        !string_equal(unit->bindings[definition->binding].name,
                      names[index])) {
      (void)fprintf(stderr, "integer unary definition differs\n");
      return 0;
    }
    function_type = &unit->graph.types[definition->declared_type];
    if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
        function_type->parameter_count != 1u ||
        function_type->first_parameter >= unit->parameter_count) {
      (void)fprintf(stderr, "integer unary function type differs\n");
      return 0;
    }
    parameter = function_type->first_parameter;
    input_type = unit->parameters[parameter].type;
    result_type = function_type->referenced_type;
    if (input_type >= unit->layout.type_count ||
        result_type >= unit->layout.type_count ||
        unit->layout.types[input_type].is_integer != CTOOL_TRUE ||
        unit->layout.types[input_type].size != 4u ||
        unit->layout.types[input_type].is_signed != input_signed[index] ||
        unit->layout.types[result_type].is_integer != CTOOL_TRUE ||
        unit->layout.types[result_type].size != 4u ||
        unit->layout.types[result_type].is_signed != result_signed[index] ||
        function->binding != definition->binding ||
        function->declared_type != definition->declared_type ||
        function->first_instruction != index * 4u ||
        function->instruction_count != 4u ||
        function->maximum_stack_depth != 1u ||
        instructions[0].kind !=
            CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
        instructions[0].type != input_type ||
        instructions[0].input_type != CTOOL_C_TYPE_NONE ||
        instructions[0].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        instructions[0].conversion != CTOOL_C_CONVERSION_NONE ||
        instructions[0].reference != parameter ||
        instructions[0].integer_bits != 0u ||
        instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
        instructions[1].type != input_type ||
        instructions[1].input_type != input_type ||
        instructions[1].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
        instructions[1].reference != CTOOL_C_AST_NONE ||
        instructions[1].integer_bits != 0u ||
        instructions[2].kind != CTOOL_C_IR_INSTRUCTION_UNARY ||
        instructions[2].type != result_type ||
        instructions[2].input_type != input_type ||
        instructions[2].operation != operations[index] ||
        instructions[2].conversion != CTOOL_C_CONVERSION_NONE ||
        instructions[2].reference != CTOOL_C_AST_NONE ||
        instructions[2].integer_bits != 0u ||
        instructions[3].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
        instructions[3].type != result_type ||
        instructions[3].input_type != result_type ||
        instructions[3].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        instructions[3].conversion != CTOOL_C_CONVERSION_NONE ||
        instructions[3].reference != CTOOL_C_AST_NONE ||
        instructions[3].integer_bits != 0u) {
      (void)fprintf(stderr, "integer unary IR function differs\n");
      return 0;
    }
    for (instruction_index = 0u; instruction_index < 4u;
         instruction_index++) {
      if (!string_equal(instructions[instruction_index].location.path,
                        "/integer-unary.c") ||
          !string_equal(
              instructions[instruction_index].physical_location.path,
              "/integer-unary.c") ||
          instructions[instruction_index].location.line == 0u ||
          instructions[instruction_index].physical_location.line == 0u) {
        (void)fprintf(stderr, "integer unary IR lost source locations\n");
        return 0;
      }
    }
  }
  return 1;
}

static int integer_cast_instruction_matches(
    const ctool_c_ir_instruction_t *instruction,
    ctool_c_ir_instruction_kind_t kind, ctool_u32 type,
    ctool_u32 input_type, ctool_c_expression_operator_t operation,
    ctool_c_conversion_kind_t conversion, ctool_u32 reference,
    ctool_u64 integer_bits) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == operation &&
                 instruction->conversion == conversion &&
                 instruction->reference == reference &&
                 instruction->integer_bits == integer_bits &&
                 string_equal(instruction->location.path,
                              "/integer-cast.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/integer-cast.c") != 0 &&
                 instruction->location.line != 0u &&
                 instruction->physical_location.line != 0u
             ? 1
             : 0;
}

static int validate_integer_cast_ir(const ctool_c_translation_unit_t *unit,
                                    const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *signed_definition;
  const ctool_c_function_definition_t *unsigned_definition;
  const ctool_c_type_node_t *signed_function_type;
  const ctool_c_type_node_t *unsigned_function_type;
  const ctool_c_ir_function_t *signed_function;
  const ctool_c_ir_function_t *unsigned_function;
  const ctool_c_ir_instruction_t *signed_instructions;
  const ctool_c_ir_instruction_t *unsigned_instructions;
  ctool_u32 unsigned_parameter;
  ctool_u32 signed_parameter;
  ctool_u32 unsigned_type;
  ctool_u32 signed_type;

  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->instruction_count != 12u || ir->functions == NULL ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "integer cast IR inventory differs\n");
    return 0;
  }
  signed_definition = &unit->function_definitions[0];
  unsigned_definition = &unit->function_definitions[1];
  if (signed_definition->binding >= unit->binding_count ||
      unsigned_definition->binding >= unit->binding_count ||
      signed_definition->declared_type >= unit->graph.type_count ||
      unsigned_definition->declared_type >= unit->graph.type_count ||
      !string_equal(unit->bindings[signed_definition->binding].name,
                    "signed_bits_magnitude") ||
      !string_equal(unit->bindings[unsigned_definition->binding].name,
                    "unsigned_bits")) {
    (void)fprintf(stderr, "integer cast definitions differ\n");
    return 0;
  }
  signed_function_type =
      &unit->graph.types[signed_definition->declared_type];
  unsigned_function_type =
      &unit->graph.types[unsigned_definition->declared_type];
  if (signed_function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      unsigned_function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      signed_function_type->parameter_count != 1u ||
      unsigned_function_type->parameter_count != 1u ||
      signed_function_type->first_parameter >= unit->parameter_count ||
      unsigned_function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "integer cast function types differ\n");
    return 0;
  }
  unsigned_parameter = signed_function_type->first_parameter;
  signed_parameter = unsigned_function_type->first_parameter;
  unsigned_type = unit->parameters[unsigned_parameter].type;
  signed_type = unit->parameters[signed_parameter].type;
  if (signed_function_type->referenced_type != signed_type ||
      unsigned_function_type->referenced_type != unsigned_type ||
      unsigned_type >= unit->layout.type_count ||
      signed_type >= unit->layout.type_count ||
      unit->layout.types[unsigned_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[unsigned_type].size != 4u ||
      unit->layout.types[unsigned_type].is_signed != CTOOL_FALSE ||
      unit->layout.types[signed_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[signed_type].size != 4u ||
      unit->layout.types[signed_type].is_signed != CTOOL_TRUE) {
    (void)fprintf(stderr, "integer cast value types differ\n");
    return 0;
  }
  signed_function = &ir->functions[0];
  unsigned_function = &ir->functions[1];
  signed_instructions = &ir->instructions[0];
  unsigned_instructions = &ir->instructions[8];
  if (signed_function->binding != signed_definition->binding ||
      signed_function->declared_type != signed_definition->declared_type ||
      signed_function->first_instruction != 0u ||
      signed_function->instruction_count != 8u ||
      signed_function->maximum_stack_depth != 2u ||
      unsigned_function->binding != unsigned_definition->binding ||
      unsigned_function->declared_type != unsigned_definition->declared_type ||
      unsigned_function->first_instruction != 8u ||
      unsigned_function->instruction_count != 4u ||
      unsigned_function->maximum_stack_depth != 1u) {
    (void)fprintf(stderr, "integer cast function records differ\n");
    return 0;
  }
  if (!integer_cast_instruction_matches(
          &signed_instructions[0], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          unsigned_parameter, 0u) ||
      !integer_cast_instruction_matches(
          &signed_instructions[1], CTOOL_C_IR_INSTRUCTION_LOAD, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !integer_cast_instruction_matches(
          &signed_instructions[2], CTOOL_C_IR_INSTRUCTION_UNARY,
          unsigned_type, unsigned_type,
          CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT, CTOOL_C_CONVERSION_NONE,
          CTOOL_C_AST_NONE, 0u) ||
      !integer_cast_instruction_matches(
          &signed_instructions[3], CTOOL_C_IR_INSTRUCTION_INTEGER,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          CTOOL_C_AST_NONE, 1u) ||
      !integer_cast_instruction_matches(
          &signed_instructions[4], CTOOL_C_IR_INSTRUCTION_BINARY,
          unsigned_type, unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_ADD,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !integer_cast_instruction_matches(
          &signed_instructions[5], CTOOL_C_IR_INSTRUCTION_CONVERT, signed_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !integer_cast_instruction_matches(
          &signed_instructions[6], CTOOL_C_IR_INSTRUCTION_UNARY, signed_type,
          signed_type, CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !integer_cast_instruction_matches(
          &signed_instructions[7], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          signed_type, signed_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !integer_cast_instruction_matches(
          &unsigned_instructions[0],
          CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, signed_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, signed_parameter, 0u) ||
      !integer_cast_instruction_matches(
          &unsigned_instructions[1], CTOOL_C_IR_INSTRUCTION_LOAD, signed_type,
          signed_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !integer_cast_instruction_matches(
          &unsigned_instructions[2], CTOOL_C_IR_INSTRUCTION_CONVERT,
          unsigned_type, signed_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !integer_cast_instruction_matches(
          &unsigned_instructions[3], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          unsigned_type, unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u)) {
    (void)fprintf(stderr, "integer cast instruction stream differs\n");
    return 0;
  }
  return 1;
}

typedef enum {
  SIGNED_BITS_TYPE_NONE = 0,
  SIGNED_BITS_TYPE_SIGNED,
  SIGNED_BITS_TYPE_UNSIGNED
} signed_bits_type_t;

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  signed_bits_type_t type;
  signed_bits_type_t input_type;
  ctool_c_expression_operator_t operation;
  ctool_c_conversion_kind_t conversion;
  ctool_u32 reference;
  ctool_u64 integer_bits;
} signed_bits_instruction_t;

#define SIGNED_BITS_PARAMETER (CTOOL_C_AST_NONE - 1u)

static ctool_u32 signed_bits_type(signed_bits_type_t type,
                                  ctool_u32 signed_type,
                                  ctool_u32 unsigned_type) {
  if (type == SIGNED_BITS_TYPE_SIGNED) {
    return signed_type;
  }
  if (type == SIGNED_BITS_TYPE_UNSIGNED) {
    return unsigned_type;
  }
  return CTOOL_C_TYPE_NONE;
}

static int validate_signed_bits_ir(const ctool_c_translation_unit_t *unit,
                                   const ctool_c_ir_unit_t *ir) {
  static const signed_bits_instruction_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, SIGNED_BITS_PARAMETER, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_UNSIGNED, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0x7fffffffu},
      {CTOOL_C_IR_INSTRUCTION_BINARY, SIGNED_BITS_TYPE_SIGNED,
       SIGNED_BITS_TYPE_UNSIGNED, CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, SIGNED_BITS_TYPE_NONE,
       SIGNED_BITS_TYPE_SIGNED, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, 9u, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, SIGNED_BITS_PARAMETER, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_UNSIGNED, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, SIGNED_BITS_TYPE_SIGNED,
       SIGNED_BITS_TYPE_UNSIGNED, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, SIGNED_BITS_TYPE_SIGNED,
       SIGNED_BITS_TYPE_SIGNED, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, SIGNED_BITS_PARAMETER, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_UNSIGNED, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0x80000000u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, SIGNED_BITS_TYPE_SIGNED,
       SIGNED_BITS_TYPE_UNSIGNED, CTOOL_C_EXPRESSION_OPERATOR_EQUAL,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, SIGNED_BITS_TYPE_NONE,
       SIGNED_BITS_TYPE_SIGNED, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, 19u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, SIGNED_BITS_TYPE_SIGNED,
       SIGNED_BITS_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 2147483647u},
      {CTOOL_C_IR_INSTRUCTION_UNARY, SIGNED_BITS_TYPE_SIGNED,
       SIGNED_BITS_TYPE_SIGNED, CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, SIGNED_BITS_TYPE_SIGNED,
       SIGNED_BITS_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, SIGNED_BITS_TYPE_SIGNED,
       SIGNED_BITS_TYPE_SIGNED, CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, SIGNED_BITS_TYPE_SIGNED,
       SIGNED_BITS_TYPE_SIGNED, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, SIGNED_BITS_PARAMETER, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_UNSIGNED, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_UNARY, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_UNSIGNED, CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, SIGNED_BITS_TYPE_UNSIGNED,
       SIGNED_BITS_TYPE_UNSIGNED, CTOOL_C_EXPRESSION_OPERATOR_ADD,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, SIGNED_BITS_TYPE_SIGNED,
       SIGNED_BITS_TYPE_UNSIGNED, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_UNARY, SIGNED_BITS_TYPE_SIGNED,
       SIGNED_BITS_TYPE_SIGNED, CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, SIGNED_BITS_TYPE_SIGNED,
       SIGNED_BITS_TYPE_SIGNED, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u}};
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  ctool_u32 parameter;
  ctool_u32 signed_type;
  ctool_u32 unsigned_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      ir->functions == NULL || ir->instructions == NULL) {
    (void)fprintf(stderr, "signed-bit IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->binding >= unit->binding_count ||
      definition->declared_type >= unit->graph.type_count ||
      !string_equal(unit->bindings[definition->binding].name,
                    "dis_signed_bits")) {
    (void)fprintf(stderr, "signed-bit definition differs\n");
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "signed-bit function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  signed_type = function_type->referenced_type;
  unsigned_type = unit->parameters[parameter].type;
  function = &ir->functions[0];
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      function->maximum_stack_depth != 2u) {
    (void)fprintf(stderr, "signed-bit IR function differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const signed_bits_instruction_t *wanted = &expected[index];
    const ctool_c_ir_instruction_t *actual = &ir->instructions[index];
    ctool_u32 reference = wanted->reference == SIGNED_BITS_PARAMETER
                              ? parameter
                              : wanted->reference;
    if (actual->kind != wanted->kind ||
        actual->type !=
            signed_bits_type(wanted->type, signed_type, unsigned_type) ||
        actual->input_type != signed_bits_type(wanted->input_type, signed_type,
                                               unsigned_type) ||
        actual->operation != wanted->operation ||
        actual->conversion != wanted->conversion ||
        actual->reference != reference ||
        actual->integer_bits != wanted->integer_bits ||
        !string_equal(actual->location.path,
                      "/active-cupiddis-signed-bits.c") ||
        !string_equal(actual->physical_location.path,
                      "/active-cupiddis-signed-bits.c") ||
        actual->location.line == 0u || actual->physical_location.line == 0u) {
      (void)fprintf(stderr, "signed-bit IR instruction %lu differs\n",
                    (unsigned long)index);
      return 0;
    }
  }
  return 1;
}

typedef enum {
  CPUID_EXPECT_NONE = 0,
  CPUID_EXPECT_VALUE,
  CPUID_EXPECT_COUNT,
  CPUID_EXPECT_COMPARISON,
  CPUID_EXPECT_RESULT
} cpuid_expected_type_t;

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  cpuid_expected_type_t type;
  cpuid_expected_type_t input_type;
  ctool_c_expression_operator_t operation;
  ctool_c_conversion_kind_t conversion;
  ctool_u32 reference;
  ctool_u64 integer_bits;
} cpuid_expected_t;

static ctool_u32 cpuid_expected_type(cpuid_expected_type_t expected,
                                     ctool_u32 value_type,
                                     ctool_u32 count_type,
                                     ctool_u32 comparison_type,
                                     ctool_u32 result_type) {
  if (expected == CPUID_EXPECT_VALUE) {
    return value_type;
  }
  if (expected == CPUID_EXPECT_COUNT) {
    return count_type;
  }
  if (expected == CPUID_EXPECT_COMPARISON) {
    return comparison_type;
  }
  if (expected == CPUID_EXPECT_RESULT) {
    return result_type;
  }
  return CTOOL_C_TYPE_NONE;
}

static int validate_simd_cpuid_ir(const ctool_c_translation_unit_t *unit,
                                  const ctool_c_ir_unit_t *ir) {
  const ctool_u32 first_parameter_reference = CTOOL_C_AST_NONE - 1u;
  const ctool_u32 second_parameter_reference = CTOOL_C_AST_NONE - 2u;
  static const cpuid_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, CPUID_EXPECT_VALUE,
       CPUID_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, CPUID_EXPECT_VALUE, CPUID_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, CPUID_EXPECT_VALUE,
       CPUID_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE - 2u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, CPUID_EXPECT_VALUE, CPUID_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, CPUID_EXPECT_VALUE, CPUID_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CPUID_EXPECT_VALUE, CPUID_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CPUID_EXPECT_COUNT, CPUID_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 21u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, CPUID_EXPECT_VALUE, CPUID_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, CPUID_EXPECT_VALUE, CPUID_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CPUID_EXPECT_VALUE, CPUID_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, CPUID_EXPECT_COMPARISON,
       CPUID_EXPECT_VALUE, CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, CPUID_EXPECT_RESULT,
       CPUID_EXPECT_COMPARISON, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_ASSIGNMENT, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, CPUID_EXPECT_RESULT,
       CPUID_EXPECT_RESULT, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u}};
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  ctool_u32 first_parameter;
  ctool_u32 second_parameter;
  ctool_u32 value_type;
  ctool_u32 count_type;
  ctool_u32 comparison_type;
  ctool_u32 result_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0]))) {
    (void)fprintf(stderr, "CPUID toggle IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      function_type->first_parameter + 1u >= unit->parameter_count) {
    (void)fprintf(stderr, "CPUID toggle function type differs\n");
    return 0;
  }
  first_parameter = function_type->first_parameter;
  second_parameter = first_parameter + 1u;
  value_type = unit->parameters[first_parameter].type;
  count_type = ir->instructions[6].type;
  comparison_type = ir->instructions[10].type;
  result_type = function_type->referenced_type;
  if (unit->parameters[second_parameter].type != value_type ||
      value_type >= unit->layout.type_count ||
      count_type >= unit->layout.type_count ||
      comparison_type >= unit->layout.type_count ||
      result_type >= unit->layout.type_count ||
      unit->layout.types[value_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[value_type].size != 4u ||
      unit->layout.types[value_type].is_signed != CTOOL_FALSE ||
      unit->layout.types[count_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[count_type].size != 4u ||
      unit->layout.types[count_type].is_signed != CTOOL_TRUE ||
      unit->layout.types[comparison_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[comparison_type].size != 4u ||
      unit->layout.types[comparison_type].is_signed != CTOOL_TRUE ||
      unit->layout.types[result_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[result_type].size != 4u ||
      result_type == comparison_type) {
    (void)fprintf(stderr, "CPUID toggle operand types differ\n");
    return 0;
  }
  function = &ir->functions[0];
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      function->maximum_stack_depth != 3u) {
    (void)fprintf(stderr, "CPUID toggle IR function record differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const cpuid_expected_t *wanted = &expected[index];
    const ctool_c_ir_instruction_t *actual = &ir->instructions[index];
    ctool_u32 wanted_type = cpuid_expected_type(
        wanted->type, value_type, count_type, comparison_type, result_type);
    ctool_u32 wanted_input = cpuid_expected_type(
        wanted->input_type, value_type, count_type, comparison_type,
        result_type);
    ctool_u32 wanted_reference = wanted->reference;
    if (wanted_reference == first_parameter_reference) {
      wanted_reference = first_parameter;
    } else if (wanted_reference == second_parameter_reference) {
      wanted_reference = second_parameter;
    }
    if (actual->kind != wanted->kind || actual->type != wanted_type ||
        actual->input_type != wanted_input ||
        actual->operation != wanted->operation ||
        actual->conversion != wanted->conversion ||
        actual->reference != wanted_reference ||
        actual->integer_bits != wanted->integer_bits ||
        string_equal(actual->location.path, "/active-simd-cpuid.c") == 0 ||
        string_equal(actual->physical_location.path,
                     "/active-simd-cpuid.c") == 0 ||
        actual->location.line == 0u || actual->physical_location.line == 0u) {
      (void)fprintf(stderr, "CPUID toggle IR instruction %u differs\n",
                    index);
      return 0;
    }
  }
  return 1;
}

static int validate_simple_ir(const ctool_c_translation_unit_t *unit,
                              const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_function_t *answer;
  const ctool_c_ir_function_t *idle;
  const ctool_c_ir_function_t *as_unsigned;
  const ctool_c_function_definition_t *conversion_definition;
  const ctool_c_type_node_t *conversion_type;
  const ctool_c_ir_instruction_t *conversion;
  ctool_u32 parameter_type;
  ctool_u32 result_type;
  if (unit->function_definition_count != 3u || ir->function_count != 3u ||
      ir->instruction_count != 7u) {
    (void)fprintf(stderr, "simple IR inventory differs\n");
    return 0;
  }
  answer = &ir->functions[0];
  idle = &ir->functions[1];
  as_unsigned = &ir->functions[2];
  conversion_definition = &unit->function_definitions[2];
  if (conversion_definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  conversion_type = &unit->graph.types[conversion_definition->declared_type];
  if (conversion_type->kind != CTOOL_C_TYPE_FUNCTION ||
      conversion_type->parameter_count != 1u ||
      conversion_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  parameter_type = unit->parameters[conversion_type->first_parameter].type;
  result_type = conversion_type->referenced_type;
  conversion = &ir->instructions[as_unsigned->first_instruction];
  if (answer->instruction_count != 2u || answer->maximum_stack_depth != 1u ||
      ir->instructions[answer->first_instruction].kind !=
          CTOOL_C_IR_INSTRUCTION_INTEGER ||
      ir->instructions[answer->first_instruction].integer_bits != 42u ||
      ir->instructions[answer->first_instruction + 1u].kind !=
          CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      idle->instruction_count != 1u || idle->maximum_stack_depth != 0u ||
      ir->instructions[idle->first_instruction].kind !=
          CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      as_unsigned->instruction_count != 4u ||
      as_unsigned->maximum_stack_depth != 1u ||
      conversion[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      conversion[0].type != parameter_type ||
      conversion[0].reference != conversion_type->first_parameter ||
      conversion[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      conversion[1].type != parameter_type ||
      conversion[1].input_type != parameter_type ||
      conversion[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      conversion[2].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      conversion[2].type != result_type ||
      conversion[2].input_type != parameter_type ||
      conversion[2].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      conversion[3].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      conversion[3].type != result_type ||
      conversion[3].input_type != result_type) {
    (void)fprintf(stderr, "simple IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_selection_ir(const ctool_c_translation_unit_t *unit,
                                 const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_function_definition_t *else_definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_type_node_t *else_function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_function_t *else_function;
  const ctool_c_ir_instruction_t *instructions;
  const ctool_c_ir_instruction_t *else_instructions;
  ctool_u32 parameter;
  ctool_u32 else_parameter;
  ctool_u32 value_type;
  ctool_u32 else_value_type;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->instruction_count != 18u || ir->functions == NULL ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "selection IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  else_definition = &unit->function_definitions[1];
  if (definition->binding >= unit->binding_count ||
      definition->declared_type >= unit->graph.type_count ||
      else_definition->binding >= unit->binding_count ||
      else_definition->declared_type >= unit->graph.type_count ||
      !string_equal(unit->bindings[definition->binding].name, "choose") ||
      !string_equal(unit->bindings[else_definition->binding].name,
                    "choose_else")) {
    (void)fprintf(stderr, "selection definitions differ\n");
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  else_function_type = &unit->graph.types[else_definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count ||
      else_function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      else_function_type->parameter_count != 1u ||
      else_function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "selection function types differ\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  else_parameter = else_function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  else_value_type = unit->parameters[else_parameter].type;
  function = &ir->functions[0];
  else_function = &ir->functions[1];
  instructions = ir->instructions;
  else_instructions = &ir->instructions[7];
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u || function->instruction_count != 7u ||
      function->maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].reference != parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != value_type ||
      instructions[1].input_type != value_type ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[2].type != CTOOL_C_TYPE_NONE ||
      instructions[2].input_type != value_type ||
      instructions[2].reference != 5u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[3].type != value_type || instructions[3].integer_bits != 1u ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[4].type != value_type ||
      instructions[4].input_type != value_type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[5].type != value_type || instructions[5].integer_bits != 0u ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[6].type != value_type ||
      instructions[6].input_type != value_type ||
      else_function->binding != else_definition->binding ||
      else_function->declared_type != else_definition->declared_type ||
      else_function->first_instruction != 7u ||
      else_function->instruction_count != 11u ||
      else_function->maximum_stack_depth != 1u ||
      else_instructions[0].kind !=
          CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      else_instructions[0].type != else_value_type ||
      else_instructions[0].reference != else_parameter ||
      else_instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      else_instructions[1].type != else_value_type ||
      else_instructions[1].input_type != else_value_type ||
      else_instructions[1].conversion !=
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      else_instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      else_instructions[2].input_type != else_value_type ||
      else_instructions[2].reference != 7u ||
      else_instructions[3].kind !=
          CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      else_instructions[3].type != else_value_type ||
      else_instructions[3].reference != else_parameter ||
      else_instructions[4].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      else_instructions[4].type != else_value_type ||
      else_instructions[4].input_type != else_value_type ||
      else_instructions[4].conversion !=
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      else_instructions[5].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      else_instructions[5].input_type != else_value_type ||
      else_instructions[6].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      else_instructions[6].reference != 9u ||
      else_instructions[7].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      else_instructions[7].type != else_value_type ||
      else_instructions[7].integer_bits != 2u ||
      else_instructions[8].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      else_instructions[8].type != else_value_type ||
      else_instructions[9].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      else_instructions[9].type != else_value_type ||
      else_instructions[9].integer_bits != 3u ||
      else_instructions[10].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      else_instructions[10].type != else_value_type) {
    (void)fprintf(stderr, "selection IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_selection_edge_ir(const ctool_c_translation_unit_t *unit,
                                      const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *unreachable_definition;
  const ctool_c_function_definition_t *void_definition;
  const ctool_c_type_node_t *unreachable_type;
  const ctool_c_type_node_t *void_type;
  const ctool_c_ir_function_t *unreachable_function;
  const ctool_c_ir_function_t *void_function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->instruction_count != 7u || ir->functions == NULL ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "selection edge IR inventory differs\n");
    return 0;
  }
  unreachable_definition = &unit->function_definitions[0];
  void_definition = &unit->function_definitions[1];
  if (unreachable_definition->binding >= unit->binding_count ||
      unreachable_definition->declared_type >= unit->graph.type_count ||
      void_definition->binding >= unit->binding_count ||
      void_definition->declared_type >= unit->graph.type_count ||
      !string_equal(unit->bindings[unreachable_definition->binding].name,
                    "unreachable_tail") ||
      !string_equal(unit->bindings[void_definition->binding].name,
                    "maybe_return")) {
    (void)fprintf(stderr, "selection edge definitions differ\n");
    return 0;
  }
  unreachable_type =
      &unit->graph.types[unreachable_definition->declared_type];
  void_type = &unit->graph.types[void_definition->declared_type];
  if (unreachable_type->kind != CTOOL_C_TYPE_FUNCTION ||
      unreachable_type->parameter_count != 1u ||
      void_type->kind != CTOOL_C_TYPE_FUNCTION ||
      void_type->parameter_count != 1u ||
      void_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "selection edge function types differ\n");
    return 0;
  }
  parameter = void_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  unreachable_function = &ir->functions[0];
  void_function = &ir->functions[1];
  instructions = ir->instructions;
  if (unreachable_function->binding != unreachable_definition->binding ||
      unreachable_function->first_instruction != 0u ||
      unreachable_function->instruction_count != 2u ||
      unreachable_function->maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[0].type != unreachable_type->referenced_type ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[1].type != unreachable_type->referenced_type ||
      instructions[1].input_type != unreachable_type->referenced_type ||
      void_function->binding != void_definition->binding ||
      void_function->first_instruction != 2u ||
      void_function->instruction_count != 5u ||
      void_function->maximum_stack_depth != 1u ||
      instructions[2].kind !=
          CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[2].type != value_type ||
      instructions[2].reference != parameter ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].type != value_type ||
      instructions[3].input_type != value_type ||
      instructions[3].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[4].input_type != value_type ||
      instructions[4].reference != 4u ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)fprintf(stderr, "selection edge IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_while_ir(const ctool_c_translation_unit_t *unit,
                             const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 function_binding = find_binding(unit, "syscall_sleep_ms");
  ctool_u32 timer_binding = find_binding(unit, "timer_get_uptime_ms");
  ctool_u32 yield_binding = find_binding(unit, "process_yield");
  ctool_u32 start_binding = find_block_binding(unit, "start");
  ctool_u32 parameter;
  ctool_u32 value_type;
  ctool_u32 condition_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 14u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      timer_binding == CTOOL_C_AST_NONE || yield_binding == CTOOL_C_AST_NONE ||
      start_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "while IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->binding != function_binding ||
      definition->declared_type >= unit->graph.type_count ||
      unit->bindings[timer_binding].type >= unit->graph.type_count ||
      unit->bindings[yield_binding].type >= unit->graph.type_count) {
    (void)fprintf(stderr, "while IR definitions differ\n");
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "while IR function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  if (unit->block_bindings[start_binding].type != value_type) {
    (void)fprintf(stderr, "while IR local type differs\n");
    return 0;
  }
  function = &ir->functions[0];
  instructions = ir->instructions;
  condition_type = instructions[9].type;
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 14u ||
      function->maximum_stack_depth != 2u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != start_binding ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
      instructions[1].type != value_type ||
      instructions[1].input_type != unit->bindings[timer_binding].type ||
      instructions[1].reference != timer_binding ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[2].type != value_type ||
      instructions[2].input_type != value_type ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
      instructions[3].type != value_type ||
      instructions[3].input_type != unit->bindings[timer_binding].type ||
      instructions[3].reference != timer_binding ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[4].type != value_type ||
      instructions[4].reference != start_binding ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[5].type != value_type ||
      instructions[5].input_type != value_type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[6].type != value_type ||
      instructions[6].input_type != value_type ||
      instructions[6].operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[7].type != value_type ||
      instructions[7].reference != parameter ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[8].type != value_type ||
      instructions[8].input_type != value_type ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[9].input_type != value_type ||
      instructions[9].operation != CTOOL_C_EXPRESSION_OPERATOR_LESS ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[10].input_type != condition_type ||
      instructions[10].reference != 13u ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
      instructions[11].type != function_type->referenced_type ||
      instructions[11].input_type != unit->bindings[yield_binding].type ||
      instructions[11].reference != yield_binding ||
      instructions[12].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[12].reference != 3u ||
      instructions[13].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      !string_equal(instructions[10].location.path, "/active-while.c") ||
      !string_equal(instructions[12].location.path, "/active-while.c")) {
    (void)fprintf(stderr, "while IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_terminal_while_ir(const ctool_c_translation_unit_t *unit,
                                      const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 function_binding = find_binding(unit, "loop_return");
  ctool_u32 parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 5u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "terminal while IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->binding != function_binding ||
      definition->declared_type >= unit->graph.type_count) {
    (void)fprintf(stderr, "terminal while IR definition differs\n");
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "terminal while IR function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  function = &ir->functions[0];
  instructions = ir->instructions;
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 5u ||
      function->maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != value_type ||
      instructions[1].input_type != value_type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[2].input_type != value_type ||
      instructions[2].reference != 4u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)fprintf(stderr, "terminal while IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_do_ir(const ctool_c_translation_unit_t *unit,
                          const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 function_binding = find_binding(unit, "doom_wait_tick");
  ctool_u32 time_binding = find_binding(unit, "I_GetTime");
  ctool_u32 sleep_binding = find_binding(unit, "I_Sleep");
  ctool_u32 nowtime_binding = find_block_binding(unit, "nowtime");
  ctool_u32 tics_binding = find_block_binding(unit, "tics");
  ctool_u32 parameter;
  ctool_u32 value_type;
  ctool_u32 condition_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 21u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      time_binding == CTOOL_C_AST_NONE || sleep_binding == CTOOL_C_AST_NONE ||
      nowtime_binding == CTOOL_C_AST_NONE ||
      tics_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "do IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->binding != function_binding ||
      definition->declared_type >= unit->graph.type_count ||
      unit->bindings[time_binding].type >= unit->graph.type_count ||
      unit->bindings[sleep_binding].type >= unit->graph.type_count) {
    (void)fprintf(stderr, "do IR definitions differ\n");
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "do IR function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  if (unit->block_bindings[nowtime_binding].type != value_type ||
      unit->block_bindings[tics_binding].type != value_type) {
    (void)fprintf(stderr, "do IR local types differ\n");
    return 0;
  }
  function = &ir->functions[0];
  instructions = ir->instructions;
  condition_type = instructions[17].type;
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 21u ||
      function->maximum_stack_depth != 3u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != nowtime_binding ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
      instructions[1].type != value_type ||
      instructions[1].input_type != unit->bindings[time_binding].type ||
      instructions[1].reference != time_binding ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[2].type != value_type ||
      instructions[2].input_type != value_type ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[3].input_type != value_type ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[4].type != value_type ||
      instructions[4].reference != tics_binding ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[5].type != value_type ||
      instructions[5].reference != nowtime_binding ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[6].type != value_type ||
      instructions[6].input_type != value_type ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[7].type != value_type ||
      instructions[7].reference != parameter ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[8].type != value_type ||
      instructions[8].input_type != value_type ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[9].type != value_type ||
      instructions[9].input_type != value_type ||
      instructions[9].operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[10].type != value_type ||
      instructions[10].input_type != value_type ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[11].input_type != value_type ||
      instructions[12].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[12].type != value_type ||
      instructions[12].integer_bits != 1u ||
      instructions[13].kind != CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
      instructions[13].type != function_type->referenced_type ||
      instructions[13].input_type != unit->bindings[sleep_binding].type ||
      instructions[13].reference != sleep_binding ||
      instructions[14].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[14].type != value_type ||
      instructions[14].reference != tics_binding ||
      instructions[15].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[15].type != value_type ||
      instructions[15].input_type != value_type ||
      instructions[16].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[16].type != value_type ||
      instructions[16].integer_bits != 0u ||
      instructions[17].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[17].operation != CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL ||
      instructions[18].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[18].input_type != condition_type ||
      instructions[18].reference != 20u ||
      instructions[19].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[19].reference != 0u ||
      instructions[20].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      !string_equal(instructions[0].location.path, "/active-do.c") ||
      !string_equal(instructions[18].location.path, "/active-do.c") ||
      !string_equal(instructions[19].physical_location.path,
                    "/active-do.c")) {
    (void)fprintf(stderr, "do IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_terminal_do_ir(const ctool_c_translation_unit_t *unit,
                                   const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_ir_function_t *function;
  ctool_u32 function_binding = find_binding(unit, "do_return");
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 1u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "terminal do IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  function = &ir->functions[0];
  if (definition->binding != function_binding ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u || function->instruction_count != 1u ||
      function->maximum_stack_depth != 0u ||
      ir->instructions[0].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      !string_equal(ir->instructions[0].location.path, "/terminal-do.c")) {
    (void)fprintf(stderr, "terminal do IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_for_ir(const ctool_c_translation_unit_t *unit,
                           const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 function_binding = find_binding(unit, "url_hash_loop");
  ctool_u32 index_binding = find_block_binding(unit, "i");
  ctool_u32 value_type;
  ctool_u32 condition_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 23u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      index_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "for IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->binding != function_binding ||
      definition->declared_type >= unit->graph.type_count ||
      index_binding >= unit->block_binding_count) {
    (void)fprintf(stderr, "for IR definition differs\n");
    return 0;
  }
  value_type = unit->block_bindings[index_binding].type;
  function = &ir->functions[0];
  instructions = ir->instructions;
  condition_type = instructions[7].type;
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 23u ||
      function->maximum_stack_depth != 3u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != index_binding ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[1].type != value_type ||
      instructions[1].integer_bits != 0u ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[2].type != value_type ||
      instructions[2].input_type != value_type ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[3].input_type != value_type ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[4].type != value_type ||
      instructions[4].reference != index_binding ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[5].type != value_type ||
      instructions[5].input_type != value_type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[6].type != value_type ||
      instructions[6].integer_bits != 8u ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[7].operation != CTOOL_C_EXPRESSION_OPERATOR_LESS ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[8].input_type != condition_type ||
      instructions[8].reference != 20u ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[9].type != value_type ||
      instructions[9].reference != index_binding ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[10].type != value_type ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[11].input_type != value_type ||
      instructions[12].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[12].type != value_type ||
      instructions[12].reference != index_binding ||
      instructions[13].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[13].type != value_type ||
      instructions[13].reference != index_binding ||
      instructions[14].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[14].type != value_type ||
      instructions[15].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[15].type != value_type ||
      instructions[15].integer_bits != 1u ||
      instructions[16].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[16].type != value_type ||
      instructions[16].input_type != value_type ||
      instructions[16].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[17].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[17].type != value_type ||
      instructions[17].input_type != value_type ||
      instructions[18].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[18].input_type != value_type ||
      instructions[19].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[19].reference != 4u ||
      instructions[20].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[20].type != value_type ||
      instructions[20].reference != index_binding ||
      instructions[21].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[21].type != value_type ||
      instructions[22].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[22].type != value_type ||
      instructions[22].input_type != value_type ||
      !string_equal(instructions[0].location.path, "/active-for.c") ||
      !string_equal(instructions[8].location.path, "/active-for.c") ||
      !string_equal(instructions[19].physical_location.path,
                    "/active-for.c")) {
    (void)fprintf(stderr, "for IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_declaration_for_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 function_binding = find_binding(unit, "declaration_for");
  ctool_u32 index_binding = find_block_binding(unit, "i");
  ctool_u32 value_type;
  ctool_u32 condition_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 17u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      index_binding == CTOOL_C_AST_NONE ||
      index_binding >= unit->block_binding_count) {
    (void)fprintf(stderr, "declaration for IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  function = &ir->functions[0];
  instructions = ir->instructions;
  value_type = unit->block_bindings[index_binding].type;
  condition_type = instructions[6].type;
  if (definition->binding != function_binding ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 17u ||
      function->maximum_stack_depth != 3u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != index_binding ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[1].type != value_type ||
      instructions[1].integer_bits != 0u ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[2].type != value_type ||
      instructions[2].input_type != value_type ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[3].type != value_type ||
      instructions[3].reference != index_binding ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[4].type != value_type ||
      instructions[4].input_type != value_type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[5].type != value_type ||
      instructions[5].integer_bits != 1u ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[6].operation != CTOOL_C_EXPRESSION_OPERATOR_LESS ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[7].input_type != condition_type ||
      instructions[7].reference != 16u ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[8].type != value_type ||
      instructions[8].reference != index_binding ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[9].type != value_type ||
      instructions[9].reference != index_binding ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[10].type != value_type ||
      instructions[10].input_type != value_type ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[11].type != value_type ||
      instructions[11].integer_bits != 1u ||
      instructions[12].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[12].type != value_type ||
      instructions[12].input_type != value_type ||
      instructions[12].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[13].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[13].type != value_type ||
      instructions[13].input_type != value_type ||
      instructions[14].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[14].input_type != value_type ||
      instructions[15].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[15].reference != 3u ||
      instructions[16].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      !string_equal(instructions[0].location.path, "/declaration-for.c") ||
      !string_equal(instructions[7].location.path, "/declaration-for.c") ||
      !string_equal(instructions[15].physical_location.path,
                    "/declaration-for.c")) {
    (void)fprintf(stderr, "declaration for IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_nested_declaration_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  const ctool_c_type_node_t *function_type;
  ctool_u32 function_binding = find_binding(unit, "nested_declaration");
  ctool_u32 copy_binding = find_block_binding(unit, "copy");
  ctool_u32 zero_binding = find_block_binding(unit, "zero");
  ctool_u32 parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 16u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      copy_binding == CTOOL_C_AST_NONE || zero_binding == CTOOL_C_AST_NONE ||
      copy_binding >= unit->block_binding_count ||
      zero_binding >= unit->block_binding_count) {
    (void)fprintf(stderr, "nested declaration IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "nested declaration function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  function = &ir->functions[0];
  instructions = ir->instructions;
  if (definition->binding != function_binding ||
      unit->block_bindings[copy_binding].type != value_type ||
      unit->block_bindings[zero_binding].type != value_type ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 16u ||
      function->maximum_stack_depth != 2u ||
      instructions[0].kind !=
          CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != value_type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[2].input_type != value_type ||
      instructions[2].reference != 10u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[3].type != value_type ||
      instructions[3].reference != copy_binding ||
      instructions[4].kind !=
          CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[4].type != value_type ||
      instructions[4].reference != parameter ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[5].type != value_type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[6].type != value_type ||
      instructions[6].input_type != value_type ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[7].type != value_type ||
      instructions[7].reference != copy_binding ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[8].type != value_type ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[9].type != value_type ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[10].type != value_type ||
      instructions[10].reference != zero_binding ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[11].type != value_type ||
      instructions[11].integer_bits != 0u ||
      instructions[12].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[12].type != value_type ||
      instructions[12].input_type != value_type ||
      instructions[13].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[13].type != value_type ||
      instructions[13].reference != zero_binding ||
      instructions[14].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[14].type != value_type ||
      instructions[15].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[15].type != value_type ||
      !string_equal(instructions[3].location.path,
                    "/nested-declaration.c") ||
      !string_equal(instructions[10].physical_location.path,
                    "/nested-declaration.c")) {
    (void)fprintf(stderr, "nested declaration IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_loop_declaration_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 function_binding = find_binding(unit, "loop_declarations");
  ctool_u32 while_binding = find_block_binding(unit, "in_while");
  ctool_u32 do_binding = find_block_binding(unit, "in_do");
  ctool_u32 for_binding = find_block_binding(unit, "in_for");
  ctool_u32 parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 10u || ir->functions == NULL ||
      ir->instructions == NULL || unit->block_binding_count != 3u ||
      function_binding == CTOOL_C_AST_NONE ||
      while_binding == CTOOL_C_AST_NONE || do_binding == CTOOL_C_AST_NONE ||
      for_binding == CTOOL_C_AST_NONE || while_binding != 0u ||
      do_binding != 1u || for_binding != 2u) {
    (void)fprintf(stderr, "loop declaration IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "loop declaration function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  function = &ir->functions[0];
  instructions = ir->instructions;
  if (definition->binding != function_binding ||
      unit->block_bindings[while_binding].type != value_type ||
      unit->block_bindings[do_binding].type != value_type ||
      unit->block_bindings[for_binding].type != value_type ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 10u ||
      function->maximum_stack_depth != 1u ||
      instructions[0].kind !=
          CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != value_type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[2].input_type != value_type ||
      instructions[2].reference != 4u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[3].reference != 4u ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[4].reference != 5u ||
      instructions[5].kind !=
          CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[5].type != value_type ||
      instructions[5].reference != parameter ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[6].type != value_type ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[7].input_type != value_type ||
      instructions[7].reference != 9u ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[8].reference != 9u ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      !string_equal(instructions[0].location.path,
                    "/loop-declarations.c") ||
      !string_equal(instructions[9].physical_location.path,
                    "/loop-declarations.c")) {
    (void)fprintf(stderr, "loop declaration IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_unreachable_declaration_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir,
    const char *function_name, const char *path) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_ir_function_t *function;
  const ctool_c_type_node_t *function_type;
  ctool_u32 function_binding = find_binding(unit, function_name);
  ctool_u32 local_binding = find_block_binding(unit, "value");
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 2u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      local_binding == CTOOL_C_AST_NONE ||
      local_binding >= unit->block_binding_count) {
    (void)fprintf(stderr, "unreachable declaration IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  function = &ir->functions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  value_type = function_type->referenced_type;
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      value_type >= unit->layout.type_count ||
      definition->binding != function_binding ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 2u ||
      function->maximum_stack_depth != 1u ||
      ir->instructions[0].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      ir->instructions[0].type != value_type ||
      ir->instructions[0].integer_bits != 0u ||
      ir->instructions[1].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      ir->instructions[1].type != value_type ||
      ir->instructions[1].input_type != value_type ||
      !string_equal(ir->instructions[0].location.path, path)) {
    (void)fprintf(stderr, "unreachable declaration IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_for_edge_ir(const ctool_c_translation_unit_t *unit,
                                const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *spin_definition;
  const ctool_c_function_definition_t *once_definition;
  const ctool_c_type_node_t *once_type;
  ctool_u32 spin_binding = find_binding(unit, "spin");
  ctool_u32 once_binding = find_binding(unit, "maybe_once");
  ctool_u32 parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->instruction_count != 6u || ir->functions == NULL ||
      ir->instructions == NULL || spin_binding == CTOOL_C_AST_NONE ||
      once_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "for edge IR inventory differs\n");
    return 0;
  }
  spin_definition = &unit->function_definitions[0];
  once_definition = &unit->function_definitions[1];
  if (spin_definition->binding != spin_binding ||
      once_definition->binding != once_binding ||
      once_definition->declared_type >= unit->graph.type_count) {
    (void)fprintf(stderr, "for edge definitions differ\n");
    return 0;
  }
  once_type = &unit->graph.types[once_definition->declared_type];
  if (once_type->kind != CTOOL_C_TYPE_FUNCTION ||
      once_type->parameter_count != 1u ||
      once_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "for edge function type differs\n");
    return 0;
  }
  parameter = once_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  if (ir->functions[0].binding != spin_binding ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 1u ||
      ir->functions[0].maximum_stack_depth != 0u ||
      ir->instructions[0].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      ir->instructions[0].reference != 0u ||
      ir->functions[1].binding != once_binding ||
      ir->functions[1].first_instruction != 1u ||
      ir->functions[1].instruction_count != 5u ||
      ir->functions[1].maximum_stack_depth != 1u ||
      ir->instructions[1].kind !=
          CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      ir->instructions[1].type != value_type ||
      ir->instructions[1].reference != parameter ||
      ir->instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      ir->instructions[2].type != value_type ||
      ir->instructions[2].input_type != value_type ||
      ir->instructions[3].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      ir->instructions[3].input_type != value_type ||
      ir->instructions[3].reference != 4u ||
      ir->instructions[4].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      ir->instructions[5].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)fprintf(stderr, "for edge IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_terminal_wide_for_iteration_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_ir_function_t *function;
  ctool_u32 function_binding = find_binding(unit, "terminal_for_wide");
  ctool_u32 condition_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 4u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "terminal wide for IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  function = &ir->functions[0];
  condition_type = ir->instructions[0].type;
  if (definition->binding != function_binding ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 4u ||
      function->maximum_stack_depth != 1u ||
      ir->instructions[0].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      ir->instructions[0].integer_bits != 1u ||
      ir->instructions[1].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      ir->instructions[1].input_type != condition_type ||
      ir->instructions[1].reference != 3u ||
      ir->instructions[2].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      ir->instructions[3].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      !string_equal(ir->instructions[0].location.path,
                    "/terminal-wide-for-iteration.c") ||
      !string_equal(ir->instructions[2].location.path,
                    "/terminal-wide-for-iteration.c")) {
    (void)fprintf(stderr, "terminal wide for IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_forward_goto_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  static const struct {
    const char *name;
    ctool_u32 first_instruction;
    ctool_u32 instruction_count;
    ctool_u32 maximum_stack_depth;
    ctool_u32 first_label;
    ctool_u32 label_count;
  } expectations[] = {
      {"goto_forward", 0u, 8u, 1u, 0u, 1u},
      {"goto_backward", 8u, 14u, 3u, 1u, 1u},
      {"goto_cycle", 22u, 15u, 3u, 2u, 2u},
      {"unreachable_goto", 37u, 2u, 1u, 4u, 1u}};
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 expectation_count =
      (ctool_u32)(sizeof(expectations) / sizeof(expectations[0]));
  ctool_u32 index;
  if (unit->function_definition_count != expectation_count ||
      unit->label_count != 5u || ir->function_count != expectation_count ||
      ir->instruction_count != 39u || ir->functions == NULL ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "direct goto IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < expectation_count; index++) {
    const ctool_c_function_definition_t *definition =
        &unit->function_definitions[index];
    ctool_u32 binding = find_binding(unit, expectations[index].name);
    if (binding == CTOOL_C_AST_NONE || definition->binding != binding ||
        definition->declared_type >= unit->graph.type_count ||
        definition->first_label != expectations[index].first_label ||
        definition->label_count != expectations[index].label_count ||
        ir->functions[index].binding != binding ||
        ir->functions[index].declared_type != definition->declared_type ||
        ir->functions[index].first_instruction !=
            expectations[index].first_instruction ||
        ir->functions[index].instruction_count !=
            expectations[index].instruction_count ||
        ir->functions[index].maximum_stack_depth !=
            expectations[index].maximum_stack_depth) {
      (void)fprintf(stderr, "direct goto function %u differs\n", index);
      return 0;
    }
  }
  instructions = ir->instructions;
  if (instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[2].reference != 4u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[3].reference != 6u ||
      instructions[3].integer_bits != 0u ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "forward goto targets differ\n");
    return 0;
  }
  instructions = ir->instructions + expectations[1].first_instruction;
  if (instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[2].reference != 11u ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[7].operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[10].reference != 0u ||
      instructions[10].integer_bits != 0u ||
      instructions[13].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "backward goto targets differ\n");
    return 0;
  }
  instructions = ir->instructions + expectations[2].first_instruction;
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[0].reference != 8u ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[5].operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[10].reference != 12u ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[11].reference != 1u ||
      instructions[11].integer_bits != 0u ||
      instructions[14].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "cyclic goto targets differ\n");
    return 0;
  }
  instructions = ir->instructions + expectations[3].first_instruction;
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[0].integer_bits != 7u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "unreachable goto emitted dead instructions\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (ir->instructions[index].kind == CTOOL_C_IR_INSTRUCTION_JUMP &&
        (ir->instructions[index].reference == CTOOL_C_AST_NONE ||
         ir->instructions[index].integer_bits != 0u)) {
      (void)fprintf(stderr, "direct goto published an unresolved jump\n");
      return 0;
    }
  }
  return string_equal(ir->instructions[3].location.path,
                      "/forward-goto.c");
}

static int validate_nested_goto_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_function_definition_t *if_definition;
  const ctool_c_function_definition_t *break_definition;
  const ctool_c_function_definition_t *terminal_do_definition;
  const ctool_c_function_definition_t *loop_break_definition;
  const ctool_c_function_definition_t *loop_continue_definition;
  const ctool_c_function_definition_t *declaration_definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 binding = find_binding(unit, "goto_nested");
  ctool_u32 if_binding = find_binding(unit, "goto_if_return");
  ctool_u32 break_binding = find_binding(unit, "goto_after_break");
  ctool_u32 terminal_do_binding = find_binding(unit, "goto_terminal_do");
  ctool_u32 loop_break_binding = find_binding(unit, "goto_into_break");
  ctool_u32 loop_continue_binding = find_binding(unit, "goto_into_continue");
  ctool_u32 declaration_binding = find_binding(unit, "goto_declaration");
  ctool_u32 copy_binding = find_block_binding(unit, "copy");
  ctool_u32 parameter;
  ctool_u32 declaration_parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 7u || unit->label_count != 7u ||
      unit->block_binding_count != 1u || ir->function_count != 7u ||
      ir->instruction_count != 34u ||
      ir->functions == NULL || ir->instructions == NULL ||
      binding == CTOOL_C_AST_NONE || if_binding == CTOOL_C_AST_NONE ||
      break_binding == CTOOL_C_AST_NONE ||
      terminal_do_binding == CTOOL_C_AST_NONE ||
      loop_break_binding == CTOOL_C_AST_NONE ||
      loop_continue_binding == CTOOL_C_AST_NONE ||
      declaration_binding == CTOOL_C_AST_NONE ||
      copy_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr,
                  "nested goto IR inventory differs: functions=%u labels=%u "
                  "blocks=%u ir-functions=%u instructions=%u\n",
                  unit->function_definition_count, unit->label_count,
                  unit->block_binding_count, ir->function_count,
                  ir->instruction_count);
    return 0;
  }
  definition = &unit->function_definitions[0];
  if_definition = &unit->function_definitions[1];
  break_definition = &unit->function_definitions[2];
  terminal_do_definition = &unit->function_definitions[3];
  loop_break_definition = &unit->function_definitions[4];
  loop_continue_definition = &unit->function_definitions[5];
  declaration_definition = &unit->function_definitions[6];
  if (definition->binding != binding ||
      definition->declared_type >= unit->graph.type_count ||
      if_definition->binding != if_binding ||
      break_definition->binding != break_binding ||
      terminal_do_definition->binding != terminal_do_binding ||
      loop_break_definition->binding != loop_break_binding ||
      loop_continue_definition->binding != loop_continue_binding ||
      declaration_definition->binding != declaration_binding) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  function_type = &unit->graph.types[declaration_definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  declaration_parameter = function_type->first_parameter;
  instructions = ir->instructions;
  if (ir->functions[0].binding != binding ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 4u ||
      ir->functions[0].maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[0].reference != 1u ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[1].type != value_type ||
      instructions[1].reference != parameter ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      !string_equal(instructions[0].location.path, "/nested-goto.c") ||
      !string_equal(instructions[1].location.path, "/nested-goto.c")) {
    (void)fprintf(stderr, "nested goto IR instructions differ\n");
    return 0;
  }
  instructions = ir->instructions + 4u;
  if (ir->functions[1].binding != if_binding ||
      ir->functions[1].declared_type != if_definition->declared_type ||
      ir->functions[1].first_instruction != 4u ||
      ir->functions[1].instruction_count != 8u ||
      ir->functions[1].maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[0].reference != 4u ||
      instructions[0].integer_bits != 0u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[3].reference != 6u ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[4].integer_bits != 1u ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[6].integer_bits != 0u ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "nested selection goto IR differs\n");
    return 0;
  }
  instructions = ir->instructions + 12u;
  if (ir->functions[2].binding != break_binding ||
      ir->functions[2].declared_type != break_definition->declared_type ||
      ir->functions[2].first_instruction != 12u ||
      ir->functions[2].instruction_count != 5u ||
      ir->functions[2].maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[0].reference != 1u ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[1].reference != 2u ||
      instructions[1].integer_bits != 0u ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "loop exit goto IR differs\n");
    return 0;
  }
  instructions = ir->instructions + 17u;
  if (ir->functions[3].binding != terminal_do_binding ||
      ir->functions[3].declared_type !=
          terminal_do_definition->declared_type ||
      ir->functions[3].first_instruction != 17u ||
      ir->functions[3].instruction_count != 3u ||
      ir->functions[3].maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[0].reference != 1u ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[1].integer_bits != 5u ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "terminal do goto IR differs\n");
    return 0;
  }
  instructions = ir->instructions + 20u;
  if (ir->functions[4].binding != loop_break_binding ||
      ir->functions[4].declared_type != loop_break_definition->declared_type ||
      ir->functions[4].first_instruction != 20u ||
      ir->functions[4].instruction_count != 4u ||
      ir->functions[4].maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[0].reference != 1u ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[1].reference != 2u ||
      instructions[1].integer_bits != 0u ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[2].integer_bits != 6u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "goto-driven break IR differs\n");
    return 0;
  }
  instructions = ir->instructions + 24u;
  if (ir->functions[5].binding != loop_continue_binding ||
      ir->functions[5].declared_type !=
          loop_continue_definition->declared_type ||
      ir->functions[5].first_instruction != 24u ||
      ir->functions[5].instruction_count != 2u ||
      ir->functions[5].maximum_stack_depth != 0u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[0].reference != 1u ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[1].reference != 1u ||
      instructions[1].integer_bits != 0u) {
    (void)fprintf(stderr, "goto-driven continue IR differs\n");
    return 0;
  }
  instructions = ir->instructions + 26u;
  if (ir->functions[6].binding != declaration_binding ||
      ir->functions[6].declared_type != declaration_definition->declared_type ||
      ir->functions[6].first_instruction != 26u ||
      ir->functions[6].instruction_count != 8u ||
      ir->functions[6].maximum_stack_depth != 2u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[0].reference != 1u ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[1].reference != copy_binding ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[2].reference != declaration_parameter ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[5].reference != copy_binding ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[7].type != unit->block_bindings[copy_binding].type) {
    (void)fprintf(stderr, "label declaration IR differs\n");
    return 0;
  }
  return 1;
}

static int validate_break_ir(const ctool_c_translation_unit_t *unit,
                             const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_function_definition_t *do_definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_function_t *do_function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 function_binding = find_binding(unit, "break_loop");
  ctool_u32 do_binding = find_binding(unit, "break_do");
  ctool_u32 parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->instruction_count != 8u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      do_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "break IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  do_definition = &unit->function_definitions[1];
  if (definition->binding != function_binding ||
      definition->declared_type >= unit->graph.type_count ||
      do_definition->binding != do_binding) {
    (void)fprintf(stderr, "break IR definition differs\n");
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "break IR function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  function = &ir->functions[0];
  do_function = &ir->functions[1];
  instructions = ir->instructions;
  if (function->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 6u ||
      function->maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != value_type ||
      instructions[1].input_type != value_type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[2].input_type != value_type ||
      instructions[2].reference != 4u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[3].reference != 5u ||
      instructions[3].integer_bits != 0u ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[4].reference != 0u ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      do_function->binding != do_binding ||
      do_function->declared_type != do_definition->declared_type ||
      do_function->first_instruction != 6u ||
      do_function->instruction_count != 2u ||
      do_function->maximum_stack_depth != 0u ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[6].reference != 1u ||
      instructions[6].integer_bits != 0u ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      !string_equal(instructions[3].location.path, "/break-statement.c")) {
    (void)fprintf(stderr, "break IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_continue_ir(const ctool_c_translation_unit_t *unit,
                                const ctool_c_ir_unit_t *ir) {
  static const struct {
    const char *name;
    ctool_u32 first_instruction;
    ctool_u32 instruction_count;
    ctool_u32 maximum_depth;
  } expectations[] = {
      {"continue_while", 0u, 5u, 1u},
      {"continue_do", 5u, 6u, 1u},
      {"continue_for", 11u, 13u, 3u},
      {"continue_for_no_iteration", 24u, 5u, 1u},
      {"nested_continue", 29u, 9u, 1u},
      {"nested_break", 38u, 9u, 1u}};
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 expectation_count =
      (ctool_u32)(sizeof(expectations) / sizeof(expectations[0]));
  ctool_u32 index;
  if (unit->function_definition_count != expectation_count ||
      ir->function_count != expectation_count ||
      ir->instruction_count != 47u || ir->functions == NULL ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "continue IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < expectation_count; index++) {
    ctool_u32 binding = find_binding(unit, expectations[index].name);
    if (binding == CTOOL_C_AST_NONE ||
        unit->function_definitions[index].binding != binding ||
        ir->functions[index].binding != binding ||
        ir->functions[index].declared_type !=
            unit->function_definitions[index].declared_type ||
        ir->functions[index].first_instruction !=
            expectations[index].first_instruction ||
        ir->functions[index].instruction_count !=
            expectations[index].instruction_count ||
        ir->functions[index].maximum_stack_depth !=
            expectations[index].maximum_depth) {
      (void)fprintf(stderr, "continue IR function %u differs\n", index);
      return 0;
    }
  }
  instructions = ir->instructions;
  if (instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[2].reference != 4u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[3].reference != 0u ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[5].reference != 1u ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[8].reference != 5u ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[9].reference != 0u ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)fprintf(stderr, "while or do continue targets differ\n");
    return 0;
  }
  instructions = ir->instructions + expectations[2].first_instruction;
  if (instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[2].reference != 12u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[3].reference != 4u ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[8].operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[11].reference != 0u ||
      instructions[12].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)fprintf(stderr, "for continue target differs\n");
    return 0;
  }
  instructions = ir->instructions + expectations[3].first_instruction;
  if (instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[2].reference != 4u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[3].reference != 0u ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)fprintf(stderr, "for continue without iteration differs\n");
    return 0;
  }
  instructions = ir->instructions + expectations[4].first_instruction;
  if (instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[2].reference != 8u ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[5].reference != 7u ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[6].reference != 3u ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[7].reference != 0u ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)fprintf(stderr, "nested continue targets differ\n");
    return 0;
  }
  instructions = ir->instructions + expectations[5].first_instruction;
  if (instructions[2].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[2].reference != 8u ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
      instructions[5].reference != 7u ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[6].reference != 7u ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_JUMP ||
      instructions[7].reference != 8u ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)fprintf(stderr, "nested break targets differ\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (ir->instructions[index].kind == CTOOL_C_IR_INSTRUCTION_JUMP &&
        (ir->instructions[index].reference == CTOOL_C_AST_NONE ||
         ir->instructions[index].integer_bits != 0u)) {
      (void)fprintf(stderr, "continue IR published an unresolved jump\n");
      return 0;
    }
  }
  return 1;
}

static int validate_addition_ir(const ctool_c_translation_unit_t *unit,
                                 const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 first_parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 6u || ir->functions == NULL ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "addition IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      unit->parameter_count < 2u ||
      function_type->first_parameter > unit->parameter_count - 2u) {
    (void)fprintf(stderr, "addition function type differs\n");
    return 0;
  }
  first_parameter = function_type->first_parameter;
  value_type = unit->parameters[first_parameter].type;
  function = &ir->functions[0];
  instructions = ir->instructions + function->first_instruction;
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 6u ||
      function->maximum_stack_depth != 2u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != first_parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != value_type ||
      instructions[1].input_type != value_type ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[2].type != value_type ||
      instructions[2].reference != first_parameter + 1u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].type != value_type ||
      instructions[3].input_type != value_type ||
      instructions[3].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[4].type != value_type ||
      instructions[4].input_type != value_type ||
      instructions[4].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[5].type != value_type ||
      instructions[5].input_type != value_type ||
      !string_equal(instructions[4].location.path,
                    "/active-cupidc-add2.c") ||
      !string_equal(instructions[4].physical_location.path,
                    "/active-cupidc-add2.c")) {
    (void)fprintf(stderr, "addition IR instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int paint_instruction_matches(
    const ctool_c_ir_instruction_t *instruction,
    ctool_c_ir_instruction_kind_t kind, ctool_u32 type,
    ctool_u32 input_type, ctool_c_expression_operator_t operation,
    ctool_c_conversion_kind_t conversion, ctool_u32 reference) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == operation &&
                 instruction->conversion == conversion &&
                 instruction->reference == reference &&
                 instruction->integer_bits == 0u &&
                 string_equal(instruction->location.path,
                              "/active-paint-multiplication.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/active-paint-multiplication.c") != 0
             ? 1
             : 0;
}

static int validate_paint_multiplication_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  static const char *function_names[] = {"canvas_to_screen_x",
                                         "canvas_to_screen_y"};
  static const char *canvas_names[] = {"CANVAS_X", "CANVAS_Y"};
  static const char *view_names[] = {"view_x", "view_y"};
  ctool_u32 zoom = find_binding(unit, "zoom_level");
  ctool_u32 function_index;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->functions == NULL || ir->instruction_count != 24u ||
      ir->instructions == NULL || zoom == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "Paint multiplication IR inventory differs\n");
    return 0;
  }
  for (function_index = 0u; function_index < 2u; function_index++) {
    const ctool_c_function_definition_t *definition =
        &unit->function_definitions[function_index];
    const ctool_c_ir_function_t *function = &ir->functions[function_index];
    const ctool_c_ir_instruction_t *instructions =
        ir->instructions + function->first_instruction;
    const ctool_c_type_node_t *function_type;
    ctool_u32 function_binding =
        find_binding(unit, function_names[function_index]);
    ctool_u32 canvas = find_binding(unit, canvas_names[function_index]);
    ctool_u32 view = find_binding(unit, view_names[function_index]);
    ctool_u32 parameter;
    ctool_u32 integer_type;
    if (function_binding == CTOOL_C_AST_NONE || canvas == CTOOL_C_AST_NONE ||
        view == CTOOL_C_AST_NONE ||
        definition->declared_type >= unit->graph.type_count) {
      return 0;
    }
    function_type = &unit->graph.types[definition->declared_type];
    if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
        function_type->parameter_count != 1u ||
        function_type->first_parameter >= unit->parameter_count) {
      return 0;
    }
    parameter = function_type->first_parameter;
    integer_type = unit->parameters[parameter].type;
    if (function->binding != function_binding ||
        definition->binding != function_binding ||
        function->declared_type != definition->declared_type ||
        function->first_instruction != function_index * 12u ||
        function->instruction_count != 12u ||
        function->maximum_stack_depth != 3u ||
        unit->bindings[canvas].type != integer_type ||
        unit->bindings[view].type != integer_type ||
        unit->bindings[zoom].type != integer_type ||
        function_type->referenced_type != integer_type ||
        !paint_instruction_matches(
            &instructions[0], CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
            integer_type, CTOOL_C_TYPE_NONE,
            CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
            canvas) ||
        !paint_instruction_matches(
            &instructions[1], CTOOL_C_IR_INSTRUCTION_LOAD, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[2], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
            integer_type, CTOOL_C_TYPE_NONE,
            CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
            parameter) ||
        !paint_instruction_matches(
            &instructions[3], CTOOL_C_IR_INSTRUCTION_LOAD, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[4], CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
            integer_type, CTOOL_C_TYPE_NONE,
            CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, view) ||
        !paint_instruction_matches(
            &instructions[5], CTOOL_C_IR_INSTRUCTION_LOAD, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[6], CTOOL_C_IR_INSTRUCTION_BINARY, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[7], CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
            integer_type, CTOOL_C_TYPE_NONE,
            CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, zoom) ||
        !paint_instruction_matches(
            &instructions[8], CTOOL_C_IR_INSTRUCTION_LOAD, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[9], CTOOL_C_IR_INSTRUCTION_BINARY, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[10], CTOOL_C_IR_INSTRUCTION_BINARY, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_ADD,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[11], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
            integer_type, integer_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE)) {
      (void)fprintf(stderr,
                    "Paint multiplication IR differs for function %lu\n",
                    (unsigned long)function_index);
      return 0;
    }
  }
  return 1;
}

static int validate_unsigned_multiplication_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 function_binding = find_binding(unit, "multiply_unsigned");
  ctool_u32 parameter;
  ctool_u32 unsigned_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 5u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "unsigned multiplication IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  parameter = function_type->first_parameter;
  unsigned_type = unit->parameters[parameter].type;
  function = &ir->functions[0];
  instructions = ir->instructions;
  if (unsigned_type >= unit->layout.type_count ||
      unit->layout.types[unsigned_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[unsigned_type].is_signed != CTOOL_FALSE ||
      unit->layout.types[unsigned_type].size != 4u ||
      function_type->referenced_type != unsigned_type ||
      definition->binding != function_binding ||
      function->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 5u ||
      function->maximum_stack_depth != 2u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != unsigned_type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[0].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[0].reference != parameter ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != unsigned_type ||
      instructions[1].input_type != unsigned_type ||
      instructions[1].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[1].reference != CTOOL_C_AST_NONE ||
      instructions[1].integer_bits != 0u ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[2].type != unsigned_type ||
      instructions[2].input_type != CTOOL_C_TYPE_NONE ||
      instructions[2].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[2].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[2].reference != CTOOL_C_AST_NONE ||
      instructions[2].integer_bits != 0x80000001u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[3].type != unsigned_type ||
      instructions[3].input_type != unsigned_type ||
      instructions[3].operation != CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY ||
      instructions[3].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[3].reference != CTOOL_C_AST_NONE ||
      instructions[3].integer_bits != 0u ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[4].type != unsigned_type ||
      instructions[4].input_type != unsigned_type ||
      instructions[4].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[4].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[4].reference != CTOOL_C_AST_NONE ||
      instructions[4].integer_bits != 0u) {
    (void)fprintf(stderr, "unsigned multiplication IR stream differs\n");
    return 0;
  }
  for (index = 0u; index < 5u; index++) {
    if (!string_equal(instructions[index].location.path,
                      "/unsigned-multiplication.c") ||
        !string_equal(instructions[index].physical_location.path,
                      "/unsigned-multiplication.c")) {
      (void)fprintf(stderr,
                    "unsigned multiplication IR source path differs\n");
      return 0;
    }
  }
  return 1;
}

static int validate_file_assignment_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 function_binding = find_binding(unit, "vga_set_vsync_wait");
  ctool_u32 object_binding = find_binding(unit, "vga_wait_vsync");
  ctool_u32 parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 6u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      object_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "file assignment IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  function = &ir->functions[0];
  instructions = ir->instructions;
  if (function_type->referenced_type >= unit->graph.type_count ||
      unit->graph.types[function_type->referenced_type].kind !=
          CTOOL_C_TYPE_VOID ||
      unit->bindings[object_binding].type != value_type ||
      definition->binding != function_binding ||
      function->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 6u ||
      function->maximum_stack_depth != 2u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].reference != object_binding ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[1].type != value_type ||
      instructions[1].input_type != CTOOL_C_TYPE_NONE ||
      instructions[1].reference != parameter ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[2].type != value_type ||
      instructions[2].input_type != value_type ||
      instructions[2].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[3].type != value_type ||
      instructions[3].input_type != value_type ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[4].type != CTOOL_C_TYPE_NONE ||
      instructions[4].input_type != value_type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      !string_equal(instructions[3].location.path,
                    "/active-vga-file-assignment.c") ||
      !string_equal(instructions[4].physical_location.path,
                    "/active-vga-file-assignment.c")) {
    (void)fprintf(stderr, "file assignment IR instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_discard_ir(const ctool_c_translation_unit_t *unit,
                               const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_instruction_t *instructions = ir->instructions;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 3u || ir->functions == NULL ||
      instructions == NULL || ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 3u ||
      ir->functions[0].maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_INTEGER) {
    (void)fprintf(stderr, "discard IR inventory differs\n");
    return 0;
  }
  value_type = instructions[0].type;
  if (instructions[0].integer_bits != 1u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[1].type != CTOOL_C_TYPE_NONE ||
      instructions[1].input_type != value_type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)fprintf(stderr, "discard IR instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_chained_assignment_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions = ir->instructions;
  ctool_u32 function_binding = find_binding(unit, "set_both");
  ctool_u32 first = find_binding(unit, "first_state");
  ctool_u32 second = find_binding(unit, "second_state");
  ctool_u32 parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 7u || ir->functions == NULL ||
      instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      first == CTOOL_C_AST_NONE || second == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "chained assignment IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  function = &ir->functions[0];
  if (function_type->referenced_type != value_type ||
      unit->bindings[first].type != value_type ||
      unit->bindings[second].type != value_type ||
      function->binding != function_binding ||
      definition->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 7u ||
      function->maximum_stack_depth != 3u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != first ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[1].type != value_type ||
      instructions[1].reference != second ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[2].type != value_type ||
      instructions[2].reference != parameter ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].type != value_type ||
      instructions[3].input_type != value_type ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[4].type != value_type ||
      instructions[4].input_type != value_type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[5].type != value_type ||
      instructions[5].input_type != value_type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[6].type != value_type ||
      instructions[6].input_type != value_type ||
      !string_equal(instructions[4].location.path,
                    "/chained-assignment.c") ||
      !string_equal(instructions[5].physical_location.path,
                    "/chained-assignment.c")) {
    (void)fprintf(stderr, "chained assignment IR instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_local_parameter_assignment_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_type_node_t *local_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions = ir->instructions;
  ctool_u32 function_binding = find_binding(unit, "assign_local_parameter");
  ctool_u32 local = find_block_binding(unit, "local");
  ctool_u32 parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 8u || ir->functions == NULL ||
      instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      local == CTOOL_C_AST_NONE || local >= unit->block_binding_count) {
    (void)fprintf(stderr, "local/parameter assignment IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  if (unit->block_bindings[local].type >= unit->graph.type_count) {
    return 0;
  }
  local_type = &unit->graph.types[unit->block_bindings[local].type];
  function = &ir->functions[0];
  if (function_type->referenced_type >= unit->graph.type_count ||
      unit->graph.types[function_type->referenced_type].kind !=
          CTOOL_C_TYPE_VOID ||
      local_type->kind != CTOOL_C_TYPE_QUALIFIED ||
      (local_type->qualifiers & CTOOL_C_QUAL_VOLATILE) == 0u ||
      local_type->referenced_type != value_type ||
      definition->binding != function_binding ||
      function->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 8u ||
      function->maximum_stack_depth != 3u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[1].type != unit->block_bindings[local].type ||
      instructions[1].reference != local ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[2].type != value_type ||
      instructions[2].reference != parameter ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].type != value_type ||
      instructions[3].input_type != value_type ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[4].type != value_type ||
      instructions[4].input_type != value_type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[5].type != value_type ||
      instructions[5].input_type != value_type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[6].type != CTOOL_C_TYPE_NONE ||
      instructions[6].input_type != value_type ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      !string_equal(instructions[4].location.path,
                    "/local-parameter-assignment.c") ||
      !string_equal(instructions[6].physical_location.path,
                    "/local-parameter-assignment.c")) {
    (void)fprintf(stderr,
                  "local/parameter assignment IR instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_local_ir(const ctool_c_translation_unit_t *unit,
                             const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  const ctool_c_block_binding_t *binding;
  const ctool_c_initializer_t *initializer;
  ctool_u32 local = find_block_binding(unit, "now");
  ctool_u32 prior = find_block_binding(unit, "prior");
  ctool_u32 unused = find_block_binding(unit, "unused");
  ctool_u32 timer = find_binding(unit, "timer_get_uptime_ms");
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 13u || ir->functions == NULL ||
      ir->instructions == NULL || local == CTOOL_C_AST_NONE ||
      prior == CTOOL_C_AST_NONE || unused == CTOOL_C_AST_NONE ||
      timer == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "local IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  function = &ir->functions[0];
  binding = &unit->block_bindings[local];
  if (binding->initializer >= unit->initializer_count) {
    return 0;
  }
  initializer = &unit->initializers[binding->initializer];
  instructions = ir->instructions + function->first_instruction;
  if (initializer->kind != CTOOL_C_INITIALIZER_EXPRESSION ||
      initializer->type != binding->type ||
      initializer->expression >= unit->expression_count ||
      unit->block_bindings[prior].storage != CTOOL_C_STORAGE_REGISTER ||
      unit->block_bindings[unused].storage != CTOOL_C_STORAGE_AUTO ||
      unit->block_bindings[unused].initializer != CTOOL_C_AST_NONE ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 13u ||
      function->maximum_stack_depth != 2u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[0].type != binding->type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[0].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[0].reference != local ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
      instructions[1].type != binding->type ||
      instructions[1].input_type != unit->bindings[timer].type ||
      instructions[1].reference != timer ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[2].type != binding->type ||
      instructions[2].input_type != binding->type ||
      instructions[2].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[2].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[2].reference != CTOOL_C_AST_NONE ||
      instructions[2].integer_bits != 0u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[3].type != unit->block_bindings[prior].type ||
      instructions[3].input_type != CTOOL_C_TYPE_NONE ||
      instructions[3].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[3].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[3].reference != prior ||
      instructions[3].integer_bits != 0u ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[4].type != unit->parameters[0].type ||
      instructions[4].reference != 0u ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[5].type != unit->parameters[0].type ||
      instructions[5].input_type != unit->parameters[0].type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[6].type != unit->block_bindings[prior].type ||
      instructions[6].input_type != unit->parameters[0].type ||
      instructions[6].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[6].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[6].reference != CTOOL_C_AST_NONE ||
      instructions[6].integer_bits != 0u ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[7].type != binding->type ||
      instructions[7].input_type != CTOOL_C_TYPE_NONE ||
      instructions[7].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[7].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[7].reference != local ||
      instructions[7].integer_bits != 0u ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[8].type != binding->type ||
      instructions[8].input_type != binding->type ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[9].type != unit->block_bindings[prior].type ||
      instructions[9].input_type != CTOOL_C_TYPE_NONE ||
      instructions[9].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[9].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[9].reference != prior ||
      instructions[9].integer_bits != 0u ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[10].type != unit->block_bindings[prior].type ||
      instructions[10].input_type != unit->block_bindings[prior].type ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[11].type != binding->type ||
      instructions[11].input_type != binding->type ||
      instructions[11].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[12].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[12].type != binding->type ||
      instructions[12].input_type != binding->type ||
      !string_equal(instructions[0].location.path,
                    "/active-vga-local.c") ||
      !string_equal(instructions[2].physical_location.path,
                    "/active-vga-local.c")) {
    (void)fprintf(stderr, "local IR instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_file_object_load_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 comparison_type;
  ctool_u32 result_type;
  ctool_u32 local = find_block_binding(unit, "now");
  ctool_u32 timer = find_binding(unit, "timer_get_uptime_ms");
  ctool_u32 last_flip = find_binding(unit, "last_flip_ms");
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 12u || ir->functions == NULL ||
      ir->instructions == NULL || local == CTOOL_C_AST_NONE ||
      timer == CTOOL_C_AST_NONE || last_flip == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "file-object load IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->referenced_type >= unit->graph.type_count) {
    (void)fprintf(stderr, "file-object load function type differs\n");
    return 0;
  }
  result_type = function_type->referenced_type;
  function = &ir->functions[0];
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 12u ||
      function->maximum_stack_depth != 2u) {
    (void)fprintf(stderr, "file-object load IR function differs\n");
    return 0;
  }
  instructions = ir->instructions + function->first_instruction;
  comparison_type = instructions[9].type;
  if (comparison_type >= unit->layout.type_count ||
      unit->layout.types[comparison_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[comparison_type].is_signed == CTOOL_FALSE ||
      unit->layout.types[comparison_type].size != 4u ||
      comparison_type == result_type ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[0].type != unit->block_bindings[local].type ||
      instructions[0].reference != local ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
      instructions[1].type != unit->block_bindings[local].type ||
      instructions[1].input_type != unit->bindings[timer].type ||
      instructions[1].reference != timer ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[2].type != unit->block_bindings[local].type ||
      instructions[2].input_type != unit->block_bindings[local].type ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[3].type != unit->block_bindings[local].type ||
      instructions[3].reference != local ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[4].type != unit->block_bindings[local].type ||
      instructions[4].input_type != unit->block_bindings[local].type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[5].type != unit->bindings[last_flip].type ||
      instructions[5].input_type != CTOOL_C_TYPE_NONE ||
      instructions[5].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[5].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[5].reference != last_flip ||
      instructions[5].integer_bits != 0u ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[6].type != unit->bindings[last_flip].type ||
      instructions[6].input_type != unit->bindings[last_flip].type ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[7].type != unit->block_bindings[local].type ||
      instructions[7].input_type != unit->block_bindings[local].type ||
      instructions[7].operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[8].type != unit->block_bindings[local].type ||
      instructions[8].integer_bits != 16u ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[9].input_type != unit->block_bindings[local].type ||
      instructions[9].operation !=
          CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL ||
      instructions[9].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[9].reference != CTOOL_C_AST_NONE ||
      instructions[9].integer_bits != 0u ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[10].type != result_type ||
      instructions[10].input_type != comparison_type ||
      instructions[10].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[11].type != result_type ||
      instructions[11].input_type != result_type ||
      !string_equal(instructions[5].location.path,
                    "/active-vga-flip-ready.c") ||
      !string_equal(instructions[6].physical_location.path,
                    "/active-vga-flip-ready.c")) {
    (void)fprintf(stderr, "file-object load IR stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_external_file_object_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 object = find_binding(unit, "external_clock");
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 3u || ir->functions == NULL ||
      ir->instructions == NULL || object == CTOOL_C_AST_NONE ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 3u ||
      ir->functions[0].maximum_stack_depth != 1u) {
    (void)fprintf(stderr, "external file-object IR inventory differs\n");
    return 0;
  }
  instructions = ir->instructions;
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != unit->bindings[object].type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[0].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[0].reference != object ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != unit->bindings[object].type ||
      instructions[1].input_type != unit->bindings[object].type ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[2].type != unit->bindings[object].type ||
      instructions[2].input_type != unit->bindings[object].type) {
    (void)fprintf(stderr, "external file-object IR stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_file_member_ir(const ctool_c_translation_unit_t *unit,
                                   const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 object = find_binding(unit, "timer_state");
  ctool_u32 member = find_member(unit, "frequency");
  ctool_u32 object_type;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 4u || ir->functions == NULL ||
      ir->instructions == NULL || object == CTOOL_C_AST_NONE ||
      member == CTOOL_C_AST_NONE ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 4u ||
      ir->functions[0].maximum_stack_depth != 1u) {
    (void)fprintf(stderr, "file-member IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  object_type = unit->bindings[object].type;
  value_type = unit->graph.members[member].type;
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->referenced_type != value_type ||
      object_type >= unit->layout.type_count ||
      value_type >= unit->layout.type_count ||
      member >= unit->layout.member_count ||
      unit->layout.types[object_type].size != 20u ||
      unit->layout.members[member].byte_offset != 8u ||
      unit->layout.members[member].size != 4u) {
    (void)fprintf(stderr, "file-member type or layout differs\n");
    return 0;
  }
  instructions = ir->instructions;
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != object_type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].reference != object ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS ||
      instructions[1].type != value_type ||
      instructions[1].input_type != object_type ||
      instructions[1].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[1].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[1].reference != member ||
      instructions[1].integer_bits != 0u ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[2].type != value_type ||
      instructions[2].input_type != value_type ||
      instructions[2].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[3].type != value_type ||
      instructions[3].input_type != value_type ||
      !string_equal(instructions[1].location.path,
                    "/active-timer-frequency.c") ||
      !string_equal(instructions[1].physical_location.path,
                    "/active-timer-frequency.c")) {
    (void)fprintf(stderr, "file-member IR stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_bit_field_ir(const ctool_c_translation_unit_t *unit,
                                 const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 object = find_binding(unit, "state");
  ctool_u32 member = find_member(unit, "r");
  ctool_u32 object_type;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 3u || ir->functions == NULL ||
      ir->instructions == NULL || object == CTOOL_C_AST_NONE ||
      member == CTOOL_C_AST_NONE ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 3u ||
      ir->functions[0].maximum_stack_depth != 1u) {
    (void)fprintf(stderr, "bit-field IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  object_type = unit->bindings[object].type;
  value_type = unit->graph.members[member].type;
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->referenced_type != value_type ||
      object_type >= unit->layout.type_count ||
      value_type >= unit->layout.type_count ||
      member >= unit->layout.member_count ||
      unit->graph.members[member].is_bit_field != CTOOL_TRUE ||
      unit->graph.members[member].bit_width != 8u ||
      unit->layout.types[object_type].size != 4u ||
      unit->layout.members[member].byte_offset != 0u ||
      unit->layout.members[member].bit_offset != 16u ||
      unit->layout.members[member].bit_width != 8u ||
      unit->layout.members[member].size != 4u) {
    (void)fprintf(stderr, "bit-field type or layout differs\n");
    return 0;
  }
  instructions = ir->instructions;
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != object_type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].reference != object ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_BIT_FIELD_LOAD ||
      instructions[1].type != value_type ||
      instructions[1].input_type != object_type ||
      instructions[1].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[1].reference != member ||
      instructions[1].integer_bits != 0u ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[2].type != value_type ||
      instructions[2].input_type != value_type ||
      !string_equal(instructions[1].location.path,
                    "/bit-field-member.c") ||
      !string_equal(instructions[1].physical_location.path,
                    "/bit-field-member.c")) {
    (void)fprintf(stderr, "bit-field IR stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_packed_bit_field_unit(
    const ctool_c_translation_unit_t *unit) {
  ctool_u32 object = find_binding(unit, "state");
  ctool_u32 member = find_member(unit, "ready");
  ctool_u32 record_type;
  const ctool_c_type_node_t *record;
  const ctool_c_record_member_t *field;
  const ctool_c_type_layout_t *record_layout;
  const ctool_c_member_layout_t *field_layout;
  if (object == CTOOL_C_AST_NONE || member == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "packed bit-field inventory differs\n");
    return 0;
  }
  record_type = unit->bindings[object].type;
  if (record_type >= unit->graph.type_count ||
      record_type >= unit->layout.type_count ||
      member >= unit->layout.member_count) {
    return 0;
  }
  record = &unit->graph.types[record_type];
  field = &unit->graph.members[member];
  record_layout = &unit->layout.types[record_type];
  field_layout = &unit->layout.members[member];
  if (record->kind != CTOOL_C_TYPE_RECORD ||
      record->record_complete != CTOOL_TRUE ||
      record->record_packed != CTOOL_TRUE ||
      member < record->first_member ||
      member - record->first_member >= record->member_count ||
      field->is_bit_field != CTOOL_TRUE || field->bit_width != 1u ||
      record_layout->size != 1u || record_layout->alignment != 1u ||
      field_layout->byte_offset != 0u || field_layout->bit_offset != 0u ||
      field_layout->bit_width != 1u || field_layout->size != 4u ||
      field_layout->alignment != 1u) {
    (void)fprintf(stderr, "packed bit-field layout differs\n");
    return 0;
  }
  return 1;
}

static int validate_point_of_declaration_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 first = find_block_binding(unit, "first");
  ctool_u32 second = find_block_binding(unit, "second");
  if (unit->function_definition_count != 1u || unit->block_binding_count != 2u ||
      first == CTOOL_C_AST_NONE || second == CTOOL_C_AST_NONE ||
      ir->function_count != 1u || ir->instruction_count != 10u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 10u ||
      ir->functions[0].maximum_stack_depth != 2u) {
    (void)fprintf(stderr, "point-of-declaration IR inventory differs\n");
    return 0;
  }
  instructions = ir->instructions;
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[0].reference != first ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[1].reference != first ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[4].reference != second ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[5].integer_bits != 1u ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[7].reference != second ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "point-of-declaration IR stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_static_local_ir(const ctool_c_translation_unit_t *unit,
                                    const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 value = find_block_binding(unit, "value");
  if (unit->function_definition_count != 1u ||
      unit->block_binding_count != 1u || value == CTOOL_C_AST_NONE ||
      unit->block_bindings[value].storage != CTOOL_C_STORAGE_STATIC ||
      unit->block_bindings[value].initializer >= unit->initializer_count ||
      ir->function_count != 1u || ir->instruction_count != 3u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 3u ||
      ir->functions[0].maximum_stack_depth != 1u) {
    (void)fprintf(stderr, "static-local IR inventory differs\n");
    return 0;
  }
  instructions = ir->instructions;
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[0].type != unit->block_bindings[value].type ||
      instructions[0].reference != value ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != unit->block_bindings[value].type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[2].type != unit->block_bindings[value].type) {
    (void)fprintf(stderr, "static-local IR stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_inline_ir(const ctool_c_translation_unit_t *unit,
                              const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *local;
  const ctool_c_function_definition_t *mixed;
  const ctool_c_binding_t *local_binding;
  const ctool_c_binding_t *mixed_binding;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->instruction_count != 4u) {
    (void)fprintf(stderr, "inline IR inventory differs\n");
    return 0;
  }
  local = &unit->function_definitions[0];
  mixed = &unit->function_definitions[1];
  if (local->binding >= unit->binding_count ||
      mixed->binding >= unit->binding_count) {
    return 0;
  }
  local_binding = &unit->bindings[local->binding];
  mixed_binding = &unit->bindings[mixed->binding];
  if (local->storage != CTOOL_C_STORAGE_STATIC ||
      local->function_declaration_flags != CTOOL_C_FUNCTION_DECL_INLINE ||
      local_binding->linkage != CTOOL_C_LINKAGE_INTERNAL ||
      local_binding->function_declaration_flags !=
          CTOOL_C_FUNCTION_DECL_INLINE ||
      mixed->storage != CTOOL_C_STORAGE_NONE ||
      mixed->function_declaration_flags != 0u ||
      mixed_binding->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
      mixed_binding->function_declaration_flags !=
          CTOOL_C_FUNCTION_DECL_INLINE ||
      ir->functions[0].binding != local->binding ||
      ir->functions[0].instruction_count != 2u ||
      ir->instructions[ir->functions[0].first_instruction].kind !=
          CTOOL_C_IR_INSTRUCTION_INTEGER ||
      ir->instructions[ir->functions[0].first_instruction].integer_bits !=
          1u ||
      ir->functions[1].binding != mixed->binding ||
      ir->functions[1].instruction_count != 2u ||
      ir->instructions[ir->functions[1].first_instruction].kind !=
          CTOOL_C_IR_INSTRUCTION_INTEGER ||
      ir->instructions[ir->functions[1].first_instruction].integer_bits !=
          2u) {
    (void)fprintf(stderr, "inline IR policy differs\n");
    return 0;
  }
  return 1;
}

static int call_instruction_matches(
    const ctool_c_ir_instruction_t *instruction,
    ctool_c_ir_instruction_kind_t kind, ctool_u32 type,
    ctool_u32 input_type, ctool_c_conversion_kind_t conversion,
    ctool_u32 reference) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == CTOOL_C_EXPRESSION_OPERATOR_NONE &&
                 instruction->conversion == conversion &&
                 instruction->reference == reference &&
                 instruction->integer_bits == 0u &&
                 string_equal(instruction->location.path,
                              "/active-direct-calls.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/active-direct-calls.c") != 0
             ? 1
             : 0;
}

static int validate_call_ir(const ctool_c_translation_unit_t *unit,
                            const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *getpid_definition;
  const ctool_c_function_definition_t *forward_definition;
  const ctool_c_function_definition_t *local_definition;
  const ctool_c_function_definition_t *caller_definition;
  const ctool_c_function_definition_t *void_definition;
  const ctool_c_type_node_t *getpid_type;
  const ctool_c_type_node_t *forward_type;
  const ctool_c_type_node_t *local_type;
  const ctool_c_type_node_t *caller_type;
  const ctool_c_type_node_t *void_type;
  const ctool_c_type_node_t *external_type;
  const ctool_c_type_node_t *sink_type;
  const ctool_c_type_node_t *process_type;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 process_binding = find_binding(unit, "process_get_current_pid");
  ctool_u32 external_binding = find_binding(unit, "external_sum");
  ctool_u32 local_binding = find_binding(unit, "local_target");
  ctool_u32 sink_binding = find_binding(unit, "external_sink");
  ctool_u32 first_parameter;
  ctool_u32 integer_type;
  if (unit->function_definition_count != 5u || ir->function_count != 5u ||
      ir->functions == NULL || ir->instruction_count != 16u ||
      ir->instructions == NULL || process_binding == CTOOL_C_AST_NONE ||
      external_binding == CTOOL_C_AST_NONE ||
      local_binding == CTOOL_C_AST_NONE || sink_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "call IR inventory differs\n");
    return 0;
  }
  getpid_definition = &unit->function_definitions[0];
  forward_definition = &unit->function_definitions[1];
  local_definition = &unit->function_definitions[2];
  caller_definition = &unit->function_definitions[3];
  void_definition = &unit->function_definitions[4];
  if (getpid_definition->declared_type >= unit->graph.type_count ||
      forward_definition->declared_type >= unit->graph.type_count ||
      local_definition->declared_type >= unit->graph.type_count ||
      caller_definition->declared_type >= unit->graph.type_count ||
      void_definition->declared_type >= unit->graph.type_count ||
      unit->bindings[process_binding].type >= unit->graph.type_count ||
      unit->bindings[external_binding].type >= unit->graph.type_count ||
      unit->bindings[sink_binding].type >= unit->graph.type_count) {
    return 0;
  }
  getpid_type = &unit->graph.types[getpid_definition->declared_type];
  forward_type = &unit->graph.types[forward_definition->declared_type];
  local_type = &unit->graph.types[local_definition->declared_type];
  caller_type = &unit->graph.types[caller_definition->declared_type];
  void_type = &unit->graph.types[void_definition->declared_type];
  process_type = &unit->graph.types[unit->bindings[process_binding].type];
  external_type = &unit->graph.types[unit->bindings[external_binding].type];
  sink_type = &unit->graph.types[unit->bindings[sink_binding].type];
  if (getpid_type->kind != CTOOL_C_TYPE_FUNCTION ||
      process_type->kind != CTOOL_C_TYPE_FUNCTION ||
      forward_type->kind != CTOOL_C_TYPE_FUNCTION ||
      external_type->kind != CTOOL_C_TYPE_FUNCTION ||
      local_type->kind != CTOOL_C_TYPE_FUNCTION ||
      caller_type->kind != CTOOL_C_TYPE_FUNCTION ||
      void_type->kind != CTOOL_C_TYPE_FUNCTION ||
      sink_type->kind != CTOOL_C_TYPE_FUNCTION ||
      forward_type->parameter_count != 2u || unit->parameter_count < 2u ||
      forward_type->first_parameter > unit->parameter_count - 2u ||
      void_type->parameter_count != 1u ||
      void_type->first_parameter >= unit->parameter_count ||
      getpid_type->referenced_type != process_type->referenced_type ||
      local_type->referenced_type != caller_type->referenced_type ||
      void_type->referenced_type != sink_type->referenced_type) {
    (void)fprintf(stderr, "call IR function types differ\n");
    return 0;
  }
  first_parameter = forward_type->first_parameter;
  integer_type = unit->parameters[first_parameter].type;
  instructions = ir->instructions;
  if (ir->functions[0].binding != getpid_definition->binding ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 2u ||
      ir->functions[0].maximum_stack_depth != 1u ||
      !call_instruction_matches(
          &instructions[0], CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
          getpid_type->referenced_type, unit->bindings[process_binding].type,
          CTOOL_C_CONVERSION_NONE, process_binding) ||
      !call_instruction_matches(
          &instructions[1], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          getpid_type->referenced_type, getpid_type->referenced_type,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE) ||
      ir->functions[1].binding != forward_definition->binding ||
      ir->functions[1].first_instruction != 2u ||
      ir->functions[1].instruction_count != 6u ||
      ir->functions[1].maximum_stack_depth != 2u ||
      !call_instruction_matches(
          &instructions[2], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          integer_type, CTOOL_C_TYPE_NONE, CTOOL_C_CONVERSION_NONE,
          first_parameter) ||
      !call_instruction_matches(
          &instructions[3], CTOOL_C_IR_INSTRUCTION_LOAD, integer_type,
          integer_type, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
          CTOOL_C_AST_NONE) ||
      !call_instruction_matches(
          &instructions[4], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unit->parameters[first_parameter + 1u].type, CTOOL_C_TYPE_NONE,
          CTOOL_C_CONVERSION_NONE, first_parameter + 1u) ||
      !call_instruction_matches(
          &instructions[5], CTOOL_C_IR_INSTRUCTION_LOAD,
          unit->parameters[first_parameter + 1u].type,
          unit->parameters[first_parameter + 1u].type,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE) ||
      !call_instruction_matches(
          &instructions[6], CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
          forward_type->referenced_type,
          unit->bindings[external_binding].type, CTOOL_C_CONVERSION_NONE,
          external_binding) ||
      !call_instruction_matches(
          &instructions[7], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          forward_type->referenced_type, forward_type->referenced_type,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE) ||
      ir->functions[2].binding != local_definition->binding ||
      ir->functions[2].first_instruction != 8u ||
      ir->functions[2].instruction_count != 2u ||
      ir->functions[2].maximum_stack_depth != 1u ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[8].integer_bits != 9u ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      ir->functions[3].binding != caller_definition->binding ||
      ir->functions[3].first_instruction != 10u ||
      ir->functions[3].instruction_count != 2u ||
      ir->functions[3].maximum_stack_depth != 1u ||
      !call_instruction_matches(
          &instructions[10], CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
          caller_type->referenced_type, local_definition->declared_type,
          CTOOL_C_CONVERSION_NONE, local_binding) ||
      !call_instruction_matches(
          &instructions[11], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          caller_type->referenced_type, caller_type->referenced_type,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE) ||
      ir->functions[4].binding != void_definition->binding ||
      ir->functions[4].first_instruction != 12u ||
      ir->functions[4].instruction_count != 4u ||
      ir->functions[4].maximum_stack_depth != 1u ||
      !call_instruction_matches(
          &instructions[12], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unit->parameters[void_type->first_parameter].type,
          CTOOL_C_TYPE_NONE, CTOOL_C_CONVERSION_NONE,
          void_type->first_parameter) ||
      !call_instruction_matches(
          &instructions[13], CTOOL_C_IR_INSTRUCTION_LOAD,
          unit->parameters[void_type->first_parameter].type,
          unit->parameters[void_type->first_parameter].type,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE) ||
      !call_instruction_matches(
          &instructions[14], CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
          void_type->referenced_type, unit->bindings[sink_binding].type,
          CTOOL_C_CONVERSION_NONE, sink_binding) ||
      !call_instruction_matches(
          &instructions[15], CTOOL_C_IR_INSTRUCTION_RETURN_VOID,
          CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE, CTOOL_C_CONVERSION_NONE,
          CTOOL_C_AST_NONE)) {
    ctool_u32 index;
    (void)fprintf(stderr, "call IR instructions differ\n");
    for (index = 0u; index < ir->function_count; index++) {
      (void)fprintf(stderr,
                    "function %lu: binding=%lu first=%lu count=%lu "
                    "maximum=%lu\n",
                    (unsigned long)index,
                    (unsigned long)ir->functions[index].binding,
                    (unsigned long)ir->functions[index].first_instruction,
                    (unsigned long)ir->functions[index].instruction_count,
                    (unsigned long)ir->functions[index].maximum_stack_depth);
    }
    for (index = 0u; index < ir->instruction_count; index++) {
      (void)fprintf(stderr,
                    "instruction %lu: kind=%lu type=%lu input=%lu "
                    "operation=%lu conversion=%lu reference=%lu bits=%lu\n",
                    (unsigned long)index,
                    (unsigned long)instructions[index].kind,
                    (unsigned long)instructions[index].type,
                    (unsigned long)instructions[index].input_type,
                    (unsigned long)instructions[index].operation,
                    (unsigned long)instructions[index].conversion,
                    (unsigned long)instructions[index].reference,
                    (unsigned long)instructions[index].integer_bits);
    }
    return 0;
  }
  return 1;
}

static int validate_variadic_call_ir(const ctool_c_translation_unit_t *unit,
                                     const ctool_c_ir_unit_t *ir) {
  ctool_u32 direct_calls = 0u;
  ctool_u32 indirect_calls = 0u;
  ctool_u32 index;
  if (unit == NULL || ir == NULL || ir->function_count != 3u ||
      ir->instruction_count == 0u) {
    (void)fprintf(stderr, "variadic call IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction = &ir->instructions[index];
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT) {
      const ctool_c_type_node_t *function =
          instruction->input_type < unit->graph.type_count
              ? &unit->graph.types[instruction->input_type]
              : NULL;
      if (function == NULL || function->kind != CTOOL_C_TYPE_FUNCTION ||
          function->variadic != CTOOL_TRUE ||
          function->parameter_count != 1u ||
          instruction->argument_count != 3u) {
        (void)fprintf(stderr, "variadic direct call IR differs\n");
        return 0;
      }
      direct_calls++;
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT) {
      const ctool_c_type_node_t *pointer =
          instruction->input_type < unit->graph.type_count
              ? &unit->graph.types[instruction->input_type]
              : NULL;
      const ctool_c_type_node_t *function =
          pointer != NULL && pointer->kind == CTOOL_C_TYPE_POINTER &&
                  pointer->referenced_type < unit->graph.type_count
              ? &unit->graph.types[pointer->referenced_type]
              : NULL;
      if (function == NULL || function->kind != CTOOL_C_TYPE_FUNCTION ||
          function->variadic != CTOOL_TRUE ||
          function->parameter_count != 1u ||
          instruction->argument_count != 2u) {
        (void)fprintf(stderr, "variadic indirect call IR differs\n");
        return 0;
      }
      indirect_calls++;
    } else if (instruction->argument_count != 0u) {
      (void)fprintf(stderr, "non-call variadic metadata differs\n");
      return 0;
    }
  }
  if (direct_calls != 1u || indirect_calls != 1u ||
      ir->functions[0].maximum_stack_depth != 3u ||
      ir->functions[1].maximum_stack_depth != 3u ||
      ir->functions[2].maximum_stack_depth != 1u) {
    (void)fprintf(stderr, "variadic call stack contract differs\n");
    return 0;
  }
  return 1;
}

static int run_active_leaf(const char *host_root) {
  static const char simple_source[] =
      "int answer(void) { return 42; }\n"
      "static void idle(void) {}\n"
      "unsigned int as_unsigned(int value) { return value; }\n";
  static const char statement_source[] =
      "int choose(int value) {\n"
      "  if (value) return 1;\n"
      "  return 0;\n"
      "}\n"
      "int choose_else(int value) {\n"
      "  if (value) { value; } else return 2;\n"
      "  return 3;\n"
      "}\n";
  static const char wide_selection_source[] =
      "int choose_wide(void) {\n"
      "  if (1LL) return 1;\n"
      "  return 0;\n"
      "}\n";
  static const char selection_edge_source[] =
      "int unreachable_tail(int value) {\n"
      "  return 0;\n"
      "  if (value) return 1;\n"
      "}\n"
      "void maybe_return(int value) {\n"
      "  if (value) return;\n"
      "}\n";
  static const char nonvoid_selection_fallthrough_source[] =
      "int maybe_value(int value) {\n"
      "  if (value) return 1;\n"
      "}\n";
  static const char while_source[] =
      "typedef unsigned int uint32_t;\n"
      "uint32_t timer_get_uptime_ms(void);\n"
      "void process_yield(void);\n"
      "static void syscall_sleep_ms(uint32_t ms) {\n"
      "  uint32_t start = timer_get_uptime_ms();\n"
      "  while ((timer_get_uptime_ms() - start) < ms) {\n"
      "    process_yield();\n"
      "  }\n"
      "}\n";
  static const char wide_while_source[] =
      "void wait_wide(void) { while (1LL) {} }\n";
  static const char terminal_while_source[] =
      "void loop_return(int value) {\n"
      "  while (value) return;\n"
      "}\n";
  static const char do_source[] =
      "int I_GetTime(void);\n"
      "void I_Sleep(int delay);\n"
      "static void doom_wait_tick(int wipestart) {\n"
      "  int nowtime;\n"
      "  int tics;\n"
      "  do {\n"
      "    nowtime = I_GetTime();\n"
      "    tics = nowtime - wipestart;\n"
      "    I_Sleep(1);\n"
      "  } while (tics <= 0);\n"
      "}\n";
  static const char terminal_do_source[] =
      "void do_return(int value) {\n"
      "  do return; while (value);\n"
      "}\n";
  static const char wide_do_source[] =
      "void do_wide(void) { do {} while (1LL); }\n";
  static const char terminal_wide_do_source[] =
      "void terminal_do_wide(void) { do return; while (1LL); }\n";
  static const char for_source[] =
      "static int url_hash_loop(void) {\n"
      "  int i;\n"
      "  for (i = 0; i < 8; i = i + 1) {\n"
      "    i;\n"
      "  }\n"
      "  return i;\n"
      "}\n";
  static const char for_edge_source[] =
      "void spin(void) { for (;;) {} }\n"
      "void maybe_once(int value) { for (; value;) return; }\n";
  static const char wide_for_source[] =
      "void for_wide(void) { for (; 1LL;) {} }\n";
  static const char terminal_wide_for_iteration_source[] =
      "void terminal_for_wide(void) { for (; 1; 1LL) return; }\n";
  static const char declaration_for_source[] =
      "void declaration_for(void) {\n"
      "  for (int i = 0; i < 1; i = i + 1) {}\n"
      "}\n";
  static const char wide_declaration_for_source[] =
      "void wide_declaration_for(void) {\n"
      "  for (long long i = 0LL;;) { (void)i; break; }\n"
      "}\n";
  static const char nested_declaration_source[] =
      "int nested_declaration(int value) {\n"
      "  if (value) {\n"
      "    int copy = value;\n"
      "    return copy;\n"
      "  } else {\n"
      "    int zero = 0;\n"
      "    return zero;\n"
      "  }\n"
      "}\n";
  static const char loop_declaration_source[] =
      "void loop_declarations(int value) {\n"
      "  while (value) { int in_while; break; }\n"
      "  do { int in_do; break; } while (value);\n"
      "  for (; value;) { int in_for; break; }\n"
      "}\n";
  static const char unreachable_declaration_source[] =
      "int unreachable_declaration(void) {\n"
      "  return 0;\n"
      "  int value;\n"
      "}\n";
  static const char unreachable_wide_declaration_source[] =
      "int unreachable_wide_declaration(void) {\n"
      "  return 0;\n"
      "  long long value;\n"
      "}\n";
  static const char break_statement_source[] =
      "void break_loop(int value) {\n"
      "  for (;;) {\n"
      "    if (value) break;\n"
      "  }\n"
      "}\n"
      "void break_do(int value) {\n"
      "  do { break; } while (value);\n"
      "}\n";
  static const char continue_statement_source[] =
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
  static const char expression_source[] =
      "int unsupported_expression(void) { return 1; }\n";
  static const char integer_unary_source[] =
      "int unary_plus(int value) { return +value; }\n"
      "int signed_negate(int value) { return -value; }\n"
      "unsigned int unsigned_negate(unsigned int value) { return -value; }\n"
      "int logical_not(unsigned int value) { return !value; }\n";
  static const char multiplication_source[] =
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
  static const char unsigned_multiplication_source[] =
      "unsigned int multiply_unsigned(unsigned int value) {\n"
      "  return value * 0x80000001u;\n"
      "}\n";
  static const char file_assignment_source[] =
      "typedef enum { false = 0, true = 1 } bool;\n"
      "static bool vga_wait_vsync = false;\n"
      "void vga_set_vsync_wait(bool enabled) { vga_wait_vsync = enabled; }\n";
  static const char file_member_source[] =
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
  static const char bit_field_member_source[] =
      "typedef unsigned int uint32_t;\n"
      "struct color {\n"
      "  uint32_t b : 8;\n"
      "  uint32_t g : 8;\n"
      "  uint32_t r : 8;\n"
      "  uint32_t a : 8;\n"
      "};\n"
      "static volatile struct color state;\n"
      "uint32_t read_red(void) { return state.r; }\n";
  static const char narrow_bit_field_source[] =
      "struct flags { _Bool ready : 1; };\n"
      "static struct flags state;\n"
      "int read_ready(void) { return state.ready; }\n";
  static const char atomic_bit_field_source[] =
      "struct flags { _Atomic unsigned int ready : 1; };\n"
      "static struct flags state;\n"
      "unsigned int read_ready(void) { return state.ready; }\n";
  static const char packed_bit_field_source[] =
      "struct flags { unsigned int ready : 1; } "
      "__attribute__((packed));\n"
      "static struct flags state;\n"
      "unsigned int read_ready(void) { return state.ready; }\n";
  static const char atomic_assignment_source[] =
      "_Atomic int atomic_state;\n"
      "void set_atomic(void) { atomic_state = 1; }\n";
  static const char chained_assignment_source[] =
      "int first_state;\n"
      "int second_state;\n"
      "int set_both(int value) { return first_state = second_state = value; }\n";
  static const char local_parameter_assignment_source[] =
      "void assign_local_parameter(int value) {\n"
      "  volatile int local;\n"
      "  value = local = value;\n"
      "}\n";
  static const char wide_assignment_source[] =
      "long long wide_state;\n"
      "void set_wide(void) { wide_state = 1LL; }\n";
  static const char abi_source[] =
      "long long wide(long long value) { return value; }\n";
  static const char inline_success_source[] =
      "static inline int local_inline(void) { return 1; }\n"
      "inline int mixed_inline(void);\n"
      "int mixed_inline(void) { return 2; }\n";
  static const char external_inline_source[] =
      "inline int external_inline(void) { return 1; }\n";
  static const char extern_inline_source[] =
      "extern inline int extern_inline(void) { return 1; }\n";
  static const char wide_cast_source[] =
      "int wide_cast(int value) { return (long long)value; }\n";
  static const char wide_call_source[] =
      "int wide_target(long long value);\n"
      "int call_wide(void) { return wide_target(1); }\n";
  static const char wide_comparison_source[] =
      "int wide_less_equal(void) { return 1LL <= 2LL; }\n";
  static const char wide_logical_or_source[] =
      "int wide_logical_or(void) { return 1LL || 0LL; }\n";
  static const char wide_multiplication_source[] =
      "int wide_multiply(void) { return 2LL * 3LL; }\n";
  static const char wide_left_shift_source[] =
      "int wide_left_shift(void) { return 1LL << 1; }\n";
  static const char wide_right_shift_source[] =
      "int wide_right_shift(void) { return 8LL >> 1; }\n";
  static const char wide_bitwise_or_source[] =
      "int wide_bitwise_or(void) { return 1LL | 2LL; }\n";
  static const char wide_bitwise_xor_source[] =
      "int wide_bitwise_xor(void) { return 1LL ^ 2LL; }\n";
  static const char wide_bitwise_not_source[] =
      "int wide_bitwise_not(void) { return ~1LL; }\n";
  static const char wide_unary_plus_source[] =
      "int wide_unary_plus(void) { return +1LL; }\n";
  static const char wide_unary_negate_source[] =
      "int wide_unary_negate(void) { return -1LL; }\n";
  static const char wide_logical_not_source[] =
      "int wide_logical_not(void) { return !1LL; }\n";
  static const char wide_division_source[] =
      "int wide_divide(void) { return 8LL / 2LL; }\n";
  static const char wide_remainder_source[] =
      "int wide_remainder(void) { return 8LL % 3LL; }\n";
  static const char variadic_call_source[] =
      "typedef int (*variadic_callback_t)(int first, ...);\n"
      "int variadic_target(int first, ...);\n"
      "int call_variadic(signed char narrow, void *pointer) {\n"
      "  return variadic_target(1, narrow, pointer);\n"
      "}\n"
      "int call_variadic_indirect(variadic_callback_t callback,\n"
      "                           unsigned short narrow) {\n"
      "  return callback(2, narrow);\n"
      "}\n"
      "int variadic_definition(int first, ...) { return first; }\n";
  static const char variadic_structure_source[] =
      "struct pair { int value; };\n"
      "int variadic_target(int first, ...);\n"
      "int call_variadic_structure(struct pair value) {\n"
      "  return variadic_target(1, value);\n"
      "}\n";
  static const char value_statement_source[] =
      "void discard_value(void) { 1; }\n";
  static const char wide_local_source[] =
      "long long wide_local(void) {\n"
      "  long long value = 1LL;\n"
      "  return value;\n"
      "}\n";
  static const char overaligned_local_source[] =
      "typedef int aligned_int __attribute__((aligned(8)));\n"
      "int overaligned_local(void) {\n"
      "  aligned_int value = 1;\n"
      "  return value;\n"
      "}\n";
  static const char array_local_source[] =
      "int string_local(void) { char value[2] = \"x\"; return 0; }\n";
  static const char static_local_source[] =
      "int static_local(void) { static int value = 1; return value; }\n";
  static const char ownership_source[] =
      "int first_local(void) { int one = 1; return one; }\n"
      "int second_local(void) { int two = 2; return two; }\n";
  static const char point_of_declaration_source[] =
      "int point_of_declaration(void) {\n"
      "  int first = first;\n"
      "  int second = 1;\n"
      "  return second;\n"
      "}\n";
  static const char void_initializer_source[] =
      "void sink(void);\n"
      "int malformed_initializer(void) { int value = 1; return value; }\n"
      "void call_sink(void) { sink(); }\n"
      "unsigned int unsigned_value(void) { return 1u; }\n"
      "int qualified_local(int input) {\n"
      "  const int result = input;\n"
      "  return result;\n"
      "}\n";
  static const char global_frontier_source[] =
      "typedef unsigned int uint32_t;\n"
      "typedef enum { false = 0, true = 1 } bool;\n"
      "uint32_t timer_get_uptime_ms(void);\n"
      "static uint32_t last_flip_ms = 0;\n"
      "bool vga_flip_ready(void) {\n"
      "  uint32_t now = timer_get_uptime_ms();\n"
      "  return (now - last_flip_ms) >= 16u;\n"
      "}\n";
  static const char external_file_object_source[] =
      "extern unsigned int external_clock;\n"
      "unsigned int read_external_clock(void) { return external_clock; }\n";
  static const char enumerator_identifier_source[] =
      "enum E { E_ONE = 1 };\n"
      "int read_enumerator(void) { return E_ONE; }\n";
  ctool_host_adapter_t adapter;
  ctool_host_adapter_t limited_adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_job_t *limited_job = NULL;
  ctool_c_translation_unit_t active_unit;
  ctool_c_translation_unit_t logic_unit;
  ctool_c_translation_unit_t addition_unit;
  ctool_c_translation_unit_t multiplication_unit;
  ctool_c_translation_unit_t unsigned_multiplication_unit;
  ctool_c_translation_unit_t file_assignment_unit;
  ctool_c_translation_unit_t file_member_unit;
  ctool_c_translation_unit_t bit_field_member_unit;
  ctool_c_translation_unit_t narrow_bit_field_unit;
  ctool_c_translation_unit_t atomic_bit_field_unit;
  ctool_c_translation_unit_t packed_bit_field_unit;
  ctool_c_translation_unit_t atomic_assignment_unit;
  ctool_c_translation_unit_t chained_assignment_unit;
  ctool_c_translation_unit_t local_parameter_assignment_unit;
  ctool_c_translation_unit_t wide_assignment_unit;
  ctool_c_translation_unit_t local_unit;
  ctool_c_translation_unit_t simple_unit;
  ctool_c_translation_unit_t statement_unit;
  ctool_c_translation_unit_t selection_edge_unit;
  ctool_c_translation_unit_t nonvoid_selection_fallthrough_unit;
  ctool_c_translation_unit_t while_unit;
  ctool_c_translation_unit_t wide_while_unit;
  ctool_c_translation_unit_t terminal_while_unit;
  ctool_c_translation_unit_t do_unit;
  ctool_c_translation_unit_t terminal_do_unit;
  ctool_c_translation_unit_t wide_do_unit;
  ctool_c_translation_unit_t terminal_wide_do_unit;
  ctool_c_translation_unit_t for_unit;
  ctool_c_translation_unit_t for_edge_unit;
  ctool_c_translation_unit_t wide_for_unit;
  ctool_c_translation_unit_t terminal_wide_for_iteration_unit;
  ctool_c_translation_unit_t declaration_for_unit;
  ctool_c_translation_unit_t wide_declaration_for_unit;
  ctool_c_translation_unit_t nested_declaration_unit;
  ctool_c_translation_unit_t loop_declaration_unit;
  ctool_c_translation_unit_t unreachable_declaration_unit;
  ctool_c_translation_unit_t unreachable_wide_declaration_unit;
  ctool_c_translation_unit_t wide_selection_unit;
  ctool_c_translation_unit_t break_statement_unit;
  ctool_c_translation_unit_t continue_statement_unit;
  ctool_c_translation_unit_t expression_unit;
  ctool_c_translation_unit_t abi_unit;
  ctool_c_translation_unit_t inline_success_unit;
  ctool_c_translation_unit_t external_inline_unit;
  ctool_c_translation_unit_t extern_inline_unit;
  ctool_c_translation_unit_t conversion_unit;
  ctool_c_translation_unit_t signed_bits_unit;
  ctool_c_translation_unit_t wide_cast_unit;
  ctool_c_translation_unit_t call_unit;
  ctool_c_translation_unit_t wide_call_unit;
  ctool_c_translation_unit_t wide_comparison_unit;
  ctool_c_translation_unit_t division_unit;
  ctool_c_translation_unit_t logical_or_unit;
  ctool_c_translation_unit_t branch_fit_unit;
  ctool_c_translation_unit_t aes_rotw_unit;
  ctool_c_translation_unit_t align_up_unit;
  ctool_c_translation_unit_t integer_unary_unit;
  ctool_c_translation_unit_t simd_cpuid_unit;
  ctool_c_translation_unit_t wide_logical_or_unit;
  ctool_c_translation_unit_t wide_multiplication_unit;
  ctool_c_translation_unit_t wide_left_shift_unit;
  ctool_c_translation_unit_t wide_right_shift_unit;
  ctool_c_translation_unit_t wide_bitwise_or_unit;
  ctool_c_translation_unit_t wide_bitwise_xor_unit;
  ctool_c_translation_unit_t wide_bitwise_not_unit;
  ctool_c_translation_unit_t wide_unary_plus_unit;
  ctool_c_translation_unit_t wide_unary_negate_unit;
  ctool_c_translation_unit_t wide_logical_not_unit;
  ctool_c_translation_unit_t wide_division_unit;
  ctool_c_translation_unit_t wide_remainder_unit;
  ctool_c_translation_unit_t variadic_call_unit;
  ctool_c_translation_unit_t variadic_structure_unit;
  ctool_c_translation_unit_t value_statement_unit;
  ctool_c_translation_unit_t wide_local_unit;
  ctool_c_translation_unit_t overaligned_local_unit;
  ctool_c_translation_unit_t array_local_unit;
  ctool_c_translation_unit_t static_local_unit;
  ctool_c_translation_unit_t ownership_unit;
  ctool_c_translation_unit_t point_of_declaration_unit;
  ctool_c_translation_unit_t void_initializer_unit;
  ctool_c_translation_unit_t global_frontier_unit;
  ctool_c_translation_unit_t external_file_object_unit;
  ctool_c_translation_unit_t enumerator_identifier_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_function_definition_t invalid_definition;
  ctool_c_initializer_element_t dangling_initializer_element;
  ctool_c_statement_t *invalid_statements = NULL;
  ctool_c_statement_t *nested_declaration_statements = NULL;
  ctool_c_statement_t *selection_statements = NULL;
  ctool_c_statement_t *unreachable_statements = NULL;
  ctool_c_statement_t *loop_control_statements = NULL;
  ctool_u32 *loop_control_children = NULL;
  ctool_c_initializer_t *invalid_initializers = NULL;
  ctool_c_initializer_t *void_initializers = NULL;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_c_expression_t *unsupported_expressions = NULL;
  ctool_c_expression_t *cpuid_expressions = NULL;
  ctool_c_expression_t *align_up_expressions = NULL;
  ctool_c_expression_t *integer_unary_expressions = NULL;
  ctool_c_expression_t *cast_expressions = NULL;
  ctool_c_expression_t *ownership_expressions = NULL;
  ctool_c_expression_t *file_expressions = NULL;
  ctool_c_expression_t *assignment_expressions = NULL;
  ctool_u32 *assignment_children = NULL;
  ctool_c_expression_t *member_expressions = NULL;
  ctool_c_member_layout_t *member_layouts = NULL;
  ctool_c_member_layout_t *bit_field_layouts = NULL;
  ctool_c_record_member_t *bit_field_members = NULL;
  ctool_c_binding_t *file_bindings = NULL;
  ctool_c_type_layout_t *invalid_layouts = NULL;
  ctool_c_block_binding_t *invalid_block_bindings = NULL;
  ctool_c_ir_unit_t ir;
  ctool_c_ir_unit_t variadic_ir;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  ctool_u32 file_binding;
  uint64_t fingerprint;
  uint64_t while_fingerprint;
  uint64_t terminal_while_fingerprint;
  uint64_t do_fingerprint;
  uint64_t terminal_do_fingerprint;
  uint64_t for_fingerprint;
  uint64_t for_edge_fingerprint;
  uint64_t terminal_wide_for_iteration_fingerprint;
  uint64_t declaration_for_fingerprint;
  uint64_t wide_assignment_fingerprint;
  uint64_t wide_declaration_for_fingerprint;
  uint64_t unreachable_wide_declaration_fingerprint;
  uint64_t wide_local_fingerprint;
  uint64_t nested_declaration_fingerprint;
  uint64_t loop_declaration_fingerprint;
  char *fixture = NULL;
  char *logic_fixture = NULL;
  char *division_fixture = NULL;
  char *logical_or_fixture = NULL;
  char *branch_fit_fixture = NULL;
  char *aes_rotw_fixture = NULL;
  char *align_up_fixture = NULL;
  char *cast_fixture = NULL;
  char *signed_bits_fixture = NULL;
  char *simd_cpuid_fixture = NULL;
  char *call_fixture = NULL;
  ctool_u32 xor_expression;
  ctool_u32 complement_expression;
  ctool_u32 logical_not_expression;
  ctool_u32 cast_expression;
  ctool_u32 unsigned_unary_type;
  ctool_u32 signed_expression;
  ctool_u32 comparison_expression;
  ctool_u32 unreachable_statement;
  ctool_u32 index;
  int passed = 0;

  (void)memset(&active_unit, 0, sizeof(active_unit));
  (void)memset(&logic_unit, 0, sizeof(logic_unit));
  (void)memset(&division_unit, 0, sizeof(division_unit));
  (void)memset(&branch_fit_unit, 0, sizeof(branch_fit_unit));
  (void)memset(&aes_rotw_unit, 0, sizeof(aes_rotw_unit));
  (void)memset(&align_up_unit, 0, sizeof(align_up_unit));
  (void)memset(&integer_unary_unit, 0, sizeof(integer_unary_unit));
  (void)memset(&conversion_unit, 0, sizeof(conversion_unit));
  (void)memset(&signed_bits_unit, 0, sizeof(signed_bits_unit));
  (void)memset(&wide_cast_unit, 0, sizeof(wide_cast_unit));
  (void)memset(&simd_cpuid_unit, 0, sizeof(simd_cpuid_unit));
  (void)memset(&wide_bitwise_not_unit, 0,
               sizeof(wide_bitwise_not_unit));
  (void)memset(&wide_unary_plus_unit, 0, sizeof(wide_unary_plus_unit));
  (void)memset(&wide_unary_negate_unit, 0,
               sizeof(wide_unary_negate_unit));
  (void)memset(&wide_logical_not_unit, 0,
               sizeof(wide_logical_not_unit));
  (void)memset(&variadic_call_unit, 0, sizeof(variadic_call_unit));
  (void)memset(&variadic_structure_unit, 0,
               sizeof(variadic_structure_unit));
  (void)memset(&variadic_ir, 0, sizeof(variadic_ir));
  (void)memset(&addition_unit, 0, sizeof(addition_unit));
  (void)memset(&multiplication_unit, 0, sizeof(multiplication_unit));
  (void)memset(&unsigned_multiplication_unit, 0,
               sizeof(unsigned_multiplication_unit));
  (void)memset(&file_assignment_unit, 0, sizeof(file_assignment_unit));
  (void)memset(&file_member_unit, 0, sizeof(file_member_unit));
  (void)memset(&bit_field_member_unit, 0,
               sizeof(bit_field_member_unit));
  (void)memset(&narrow_bit_field_unit, 0,
               sizeof(narrow_bit_field_unit));
  (void)memset(&atomic_bit_field_unit, 0,
               sizeof(atomic_bit_field_unit));
  (void)memset(&packed_bit_field_unit, 0,
               sizeof(packed_bit_field_unit));
  (void)memset(&atomic_assignment_unit, 0,
               sizeof(atomic_assignment_unit));
  (void)memset(&chained_assignment_unit, 0,
               sizeof(chained_assignment_unit));
  (void)memset(&local_parameter_assignment_unit, 0,
               sizeof(local_parameter_assignment_unit));
  (void)memset(&wide_assignment_unit, 0, sizeof(wide_assignment_unit));
  (void)memset(&local_unit, 0, sizeof(local_unit));
  (void)memset(&simple_unit, 0, sizeof(simple_unit));
  (void)memset(&statement_unit, 0, sizeof(statement_unit));
  (void)memset(&selection_edge_unit, 0, sizeof(selection_edge_unit));
  (void)memset(&nonvoid_selection_fallthrough_unit, 0,
               sizeof(nonvoid_selection_fallthrough_unit));
  (void)memset(&while_unit, 0, sizeof(while_unit));
  (void)memset(&wide_while_unit, 0, sizeof(wide_while_unit));
  (void)memset(&terminal_while_unit, 0, sizeof(terminal_while_unit));
  (void)memset(&do_unit, 0, sizeof(do_unit));
  (void)memset(&terminal_do_unit, 0, sizeof(terminal_do_unit));
  (void)memset(&wide_do_unit, 0, sizeof(wide_do_unit));
  (void)memset(&terminal_wide_do_unit, 0,
               sizeof(terminal_wide_do_unit));
  (void)memset(&for_unit, 0, sizeof(for_unit));
  (void)memset(&for_edge_unit, 0, sizeof(for_edge_unit));
  (void)memset(&wide_for_unit, 0, sizeof(wide_for_unit));
  (void)memset(&terminal_wide_for_iteration_unit, 0,
               sizeof(terminal_wide_for_iteration_unit));
  (void)memset(&declaration_for_unit, 0, sizeof(declaration_for_unit));
  (void)memset(&wide_declaration_for_unit, 0,
               sizeof(wide_declaration_for_unit));
  (void)memset(&nested_declaration_unit, 0,
               sizeof(nested_declaration_unit));
  (void)memset(&loop_declaration_unit, 0,
               sizeof(loop_declaration_unit));
  (void)memset(&unreachable_declaration_unit, 0,
               sizeof(unreachable_declaration_unit));
  (void)memset(&unreachable_wide_declaration_unit, 0,
               sizeof(unreachable_wide_declaration_unit));
  (void)memset(&wide_selection_unit, 0, sizeof(wide_selection_unit));
  (void)memset(&break_statement_unit, 0, sizeof(break_statement_unit));
  (void)memset(&continue_statement_unit, 0,
               sizeof(continue_statement_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_is_unchanged(job)) {
    goto cleanup;
  }
  fixture = make_active_fixture();
  if (fixture == NULL ||
      !parse_source(job, "/active-cemit-add-overflows.c", fixture,
                    &active_unit)) {
    (void)fprintf(stderr, "active helper setup failed\n");
    goto cleanup;
  }
  logic_fixture = make_logic_fixture();
  if (logic_fixture == NULL ||
      !parse_source(job, "/active-cemit-power-of-two.c", logic_fixture,
                    &logic_unit)) {
    (void)fprintf(stderr, "active logic helper setup failed\n");
    goto cleanup;
  }
  division_fixture = make_division_fixture();
  if (division_fixture == NULL ||
      !parse_source(job, "/active-cemit-multiply-overflows.c",
                    division_fixture, &division_unit)) {
    (void)fprintf(stderr, "active division helper setup failed\n");
    goto cleanup;
  }
  logical_or_fixture = make_logical_or_fixture();
  if (logical_or_fixture == NULL ||
      !parse_source(job, "/active-cfront-bool-valid.c", logical_or_fixture,
                    &logical_or_unit)) {
    (void)fprintf(stderr, "active logical-or helper setup failed\n");
    goto cleanup;
  }
  branch_fit_fixture = make_branch_fit_fixture();
  if (branch_fit_fixture == NULL ||
      !parse_source(job, "/active-asm-branch-fits-i8.c",
                    branch_fit_fixture, &branch_fit_unit)) {
    (void)fprintf(stderr, "active branch-range helper setup failed\n");
    goto cleanup;
  }
  aes_rotw_fixture = make_aes_rotw_fixture();
  if (aes_rotw_fixture == NULL ||
      !parse_source(job, "/active-aes-rotw.c", aes_rotw_fixture,
                    &aes_rotw_unit)) {
    (void)fprintf(stderr, "active AES word-rotation setup failed\n");
    goto cleanup;
  }
  align_up_fixture = make_align_up_fixture();
  if (align_up_fixture == NULL ||
      !parse_source(job, "/active-memory-align-up.c", align_up_fixture,
                    &align_up_unit)) {
    (void)fprintf(stderr, "active memory alignment setup failed\n");
    goto cleanup;
  }
  if (!parse_source(job, "/integer-unary.c", integer_unary_source,
                    &integer_unary_unit)) {
    (void)fprintf(stderr, "integer unary setup failed\n");
    goto cleanup;
  }
  cast_fixture = make_integer_cast_fixture();
  if (cast_fixture == NULL ||
      !parse_source(job, "/integer-cast.c", cast_fixture,
                    &conversion_unit)) {
    (void)fprintf(stderr, "integer cast setup failed\n");
    goto cleanup;
  }
  signed_bits_fixture = make_signed_bits_fixture();
  if (signed_bits_fixture == NULL ||
      !parse_source(job, "/active-cupiddis-signed-bits.c",
                    signed_bits_fixture, &signed_bits_unit)) {
    (void)fprintf(stderr, "active signed-bit helper setup failed\n");
    goto cleanup;
  }
  simd_cpuid_fixture = make_simd_cpuid_fixture();
  if (simd_cpuid_fixture == NULL ||
      !parse_source(job, "/active-simd-cpuid.c", simd_cpuid_fixture,
                    &simd_cpuid_unit)) {
    (void)fprintf(stderr, "active CPUID toggle setup failed\n");
    goto cleanup;
  }

  if (!parse_source(job, "/active-cupidc-add2.c", active_addition,
                    &addition_unit)) {
    (void)fprintf(stderr, "active addition setup failed\n");
    goto cleanup;
  }

  if (!parse_source(job, "/active-paint-multiplication.c",
                    multiplication_source, &multiplication_unit)) {
    (void)fprintf(stderr, "active Paint multiplication setup failed\n");
    goto cleanup;
  }

  if (!parse_source(job, "/unsigned-multiplication.c",
                    unsigned_multiplication_source,
                    &unsigned_multiplication_unit)) {
    (void)fprintf(stderr, "unsigned multiplication setup failed\n");
    goto cleanup;
  }

  if (!parse_source(job, "/active-vga-file-assignment.c",
                    file_assignment_source, &file_assignment_unit)) {
    (void)fprintf(stderr, "active VGA file assignment setup failed\n");
    goto cleanup;
  }

  if (!parse_source(job, "/active-timer-frequency.c", file_member_source,
                    &file_member_unit)) {
    (void)fprintf(stderr, "active timer frequency setup failed\n");
    goto cleanup;
  }

  if (!parse_source(job, "/bit-field-member.c", bit_field_member_source,
                    &bit_field_member_unit)) {
    (void)fprintf(stderr, "bit-field member setup failed\n");
    goto cleanup;
  }
  if (!parse_source(job, "/narrow-bit-field.c", narrow_bit_field_source,
                    &narrow_bit_field_unit) ||
      !expect_ir_failure(
          job, &narrow_bit_field_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "narrow bit-field load")) {
    goto cleanup;
  }
  if (!parse_source(job, "/atomic-bit-field.c", atomic_bit_field_source,
                    &atomic_bit_field_unit) ||
      !expect_ir_failure(
          job, &atomic_bit_field_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic bit-field load")) {
    goto cleanup;
  }
  if (!parse_source_mode(job, "/packed-bit-field.c",
                         packed_bit_field_source, CTOOL_TRUE,
                         &packed_bit_field_unit) ||
      !validate_packed_bit_field_unit(&packed_bit_field_unit) ||
      !expect_ir_failure(
          job, &packed_bit_field_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "packed bit-field load")) {
    goto cleanup;
  }

  if (!parse_source(job, "/atomic-assignment.c", atomic_assignment_source,
                    &atomic_assignment_unit)) {
    goto cleanup;
  }

  if (!parse_source(job, "/chained-assignment.c", chained_assignment_source,
                    &chained_assignment_unit)) {
    (void)fprintf(stderr, "chained assignment setup failed\n");
    goto cleanup;
  }
  if (!parse_source(job, "/local-parameter-assignment.c",
                    local_parameter_assignment_source,
                    &local_parameter_assignment_unit)) {
    (void)fprintf(stderr, "local/parameter assignment setup failed\n");
    goto cleanup;
  }
  if (!expect_ir_failure(
          job, &atomic_assignment_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic assignment")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-assignment.c", wide_assignment_source,
                    &wide_assignment_unit)) {
    goto cleanup;
  }
  wide_assignment_fingerprint = unit_fingerprint(&wide_assignment_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &wide_assignment_unit, &ir);
  if (!check_status(status, CTOOL_OK, "wide assignment") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&wide_assignment_unit) != wide_assignment_fingerprint ||
      ir.function_count != 1u || ir.instruction_count != 5u ||
      ir.instructions == NULL ||
      ir.instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      ir.instructions[1].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      ir.instructions[2].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      ir.instructions[3].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      ir.instructions[4].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)fprintf(stderr, "wide assignment IR stream differs\n");
    goto cleanup;
  }

  if (!parse_source(job, "/active-vga-local.c", local_fixture,
                    &local_unit)) {
    (void)fprintf(stderr, "active VGA local setup failed\n");
    goto cleanup;
  }

  invalid_unit = local_unit;
  invalid_unit.block_bindings = NULL;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "missing local binding table")) {
    goto cleanup;
  }
  invalid_unit = local_unit;
  invalid_unit.object_definition_count = 1u;
  invalid_unit.object_definitions = NULL;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "missing object definition table")) {
    goto cleanup;
  }
  invalid_unit = local_unit;
  invalid_unit.initializers = NULL;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "missing initializer table")) {
    goto cleanup;
  }
  invalid_unit = local_unit;
  invalid_unit.initializer_element_count = 1u;
  invalid_unit.initializer_elements = NULL;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "missing initializer element table")) {
    goto cleanup;
  }
  invalid_block_bindings = (ctool_c_block_binding_t *)malloc(
      (size_t)local_unit.block_binding_count *
      sizeof(*invalid_block_bindings));
  if (invalid_block_bindings == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_block_bindings, local_unit.block_bindings,
               (size_t)local_unit.block_binding_count *
                   sizeof(*invalid_block_bindings));
  invalid_block_bindings[0].kind = (ctool_c_binding_kind_t)0;
  invalid_unit = local_unit;
  invalid_unit.block_bindings = invalid_block_bindings;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid local binding kind")) {
    goto cleanup;
  }
  (void)memcpy(invalid_block_bindings, local_unit.block_bindings,
               (size_t)local_unit.block_binding_count *
                   sizeof(*invalid_block_bindings));
  invalid_block_bindings[0].storage = (ctool_c_storage_class_t)99;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid local storage class")) {
    goto cleanup;
  }
  (void)memcpy(invalid_block_bindings, local_unit.block_bindings,
               (size_t)local_unit.block_binding_count *
                   sizeof(*invalid_block_bindings));
  if (local_unit.block_binding_count < 2u ||
      invalid_block_bindings[0].initializer == CTOOL_C_AST_NONE ||
      invalid_block_bindings[1].initializer == CTOOL_C_AST_NONE) {
    goto cleanup;
  }
  invalid_block_bindings[1].initializer =
      invalid_block_bindings[0].initializer;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "duplicate local initializer owner")) {
    goto cleanup;
  }
  (void)memcpy(invalid_block_bindings, local_unit.block_bindings,
               (size_t)local_unit.block_binding_count *
                   sizeof(*invalid_block_bindings));
  dangling_initializer_element.subobject = 0u;
  dangling_initializer_element.initializer =
      invalid_block_bindings[1].initializer;
  invalid_block_bindings[1].initializer = CTOOL_C_AST_NONE;
  invalid_unit = local_unit;
  invalid_unit.block_bindings = invalid_block_bindings;
  invalid_unit.initializer_elements = &dangling_initializer_element;
  invalid_unit.initializer_element_count = 1u;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "dangling initializer owner")) {
    goto cleanup;
  }
  invalid_initializers = (ctool_c_initializer_t *)malloc(
      (size_t)local_unit.initializer_count *
      sizeof(*invalid_initializers));
  if (invalid_initializers == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_initializers, local_unit.initializers,
               (size_t)local_unit.initializer_count *
                   sizeof(*invalid_initializers));
  invalid_initializers[local_unit.block_bindings[0].initializer].expression =
      local_unit.expression_count;
  invalid_unit = local_unit;
  invalid_unit.initializers = invalid_initializers;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid local initializer expression")) {
    goto cleanup;
  }
  (void)memcpy(invalid_initializers, local_unit.initializers,
               (size_t)local_unit.initializer_count *
                   sizeof(*invalid_initializers));
  invalid_initializers[local_unit.block_bindings[0].initializer]
      .first_element = 0u;
  invalid_initializers[local_unit.block_bindings[0].initializer]
      .element_count = 1u;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid expression initializer payload")) {
    goto cleanup;
  }
  invalid_layouts = (ctool_c_type_layout_t *)malloc(
      (size_t)local_unit.layout.type_count * sizeof(*invalid_layouts));
  if (invalid_layouts == NULL ||
      local_unit.block_bindings[0].type >= local_unit.layout.type_count) {
    goto cleanup;
  }
  (void)memcpy(invalid_layouts, local_unit.layout.types,
               (size_t)local_unit.layout.type_count *
                   sizeof(*invalid_layouts));
  invalid_layouts[local_unit.block_bindings[0].type].alignment = 0u;
  invalid_unit = local_unit;
  invalid_unit.layout.types = invalid_layouts;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid local layout")) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&local_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &local_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active VGA local lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&local_unit) != fingerprint ||
      !validate_local_ir(&local_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/point-of-declaration.c",
                    point_of_declaration_source,
                    &point_of_declaration_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&point_of_declaration_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &point_of_declaration_unit, &ir);
  if (!check_status(status, CTOOL_OK, "point-of-declaration lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&point_of_declaration_unit) != fingerprint ||
      !validate_point_of_declaration_ir(&point_of_declaration_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)point_of_declaration_unit.expression_count *
      sizeof(*invalid_expressions));
  if (invalid_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, point_of_declaration_unit.expressions,
               (size_t)point_of_declaration_unit.expression_count *
                   sizeof(*invalid_expressions));
  for (index = 0u; index < point_of_declaration_unit.expression_count;
       index++) {
    if (invalid_expressions[index].kind ==
            CTOOL_C_EXPRESSION_BLOCK_BINDING &&
        invalid_expressions[index].reference ==
            find_block_binding(&point_of_declaration_unit, "first")) {
      invalid_expressions[index].reference =
          find_block_binding(&point_of_declaration_unit, "second");
      break;
    }
  }
  invalid_unit = point_of_declaration_unit;
  invalid_unit.expressions = invalid_expressions;
  if (index == point_of_declaration_unit.expression_count ||
      !expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "forward local reference")) {
    goto cleanup;
  }
  if (!parse_source(job, "/void-initializer.c", void_initializer_source,
                    &void_initializer_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&void_initializer_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &void_initializer_unit, &ir);
  if (!check_status(status, CTOOL_OK, "qualified local lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&void_initializer_unit) != fingerprint ||
      ir.function_count != 4u) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  void_initializers = (ctool_c_initializer_t *)malloc(
      (size_t)void_initializer_unit.initializer_count *
      sizeof(*void_initializers));
  if (void_initializers == NULL ||
      void_initializer_unit.block_binding_count != 2u ||
      void_initializer_unit.block_bindings[0].initializer >=
          void_initializer_unit.initializer_count) {
    goto cleanup;
  }
  (void)memcpy(void_initializers, void_initializer_unit.initializers,
               (size_t)void_initializer_unit.initializer_count *
                   sizeof(*void_initializers));
  for (index = 0u; index < void_initializer_unit.expression_count; index++) {
    if (void_initializer_unit.expressions[index].kind ==
        CTOOL_C_EXPRESSION_CALL) {
      break;
    }
  }
  if (index == void_initializer_unit.expression_count) {
    goto cleanup;
  }
  void_initializers[void_initializer_unit.block_bindings[0].initializer]
      .expression = index;
  invalid_unit = void_initializer_unit;
  invalid_unit.initializers = void_initializers;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "void expression local initializer")) {
    goto cleanup;
  }
  (void)memcpy(void_initializers, void_initializer_unit.initializers,
               (size_t)void_initializer_unit.initializer_count *
                   sizeof(*void_initializers));
  for (index = 0u; index < void_initializer_unit.expression_count; index++) {
    if (void_initializer_unit.expressions[index].kind ==
            CTOOL_C_EXPRESSION_INTEGER_CONSTANT &&
        void_initializer_unit.expressions[index].type !=
            void_initializers[void_initializer_unit.block_bindings[0]
                                  .initializer]
                .type) {
      break;
    }
  }
  if (index == void_initializer_unit.expression_count) {
    goto cleanup;
  }
  void_initializers[void_initializer_unit.block_bindings[0].initializer]
      .expression = index;
  invalid_unit.initializers = void_initializers;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "unconverted local initializer")) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&addition_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &addition_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active addition lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&addition_unit) != fingerprint ||
      !validate_addition_ir(&addition_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&multiplication_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &multiplication_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active Paint multiplication lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&multiplication_unit) != fingerprint ||
      !validate_paint_multiplication_ir(&multiplication_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&unsigned_multiplication_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unsigned_multiplication_unit, &ir);
  if (!check_status(status, CTOOL_OK, "unsigned multiplication lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unsigned_multiplication_unit) != fingerprint ||
      !validate_unsigned_multiplication_ir(&unsigned_multiplication_unit,
                                           &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&file_assignment_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &file_assignment_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active VGA file assignment lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&file_assignment_unit) != fingerprint ||
      !validate_file_assignment_ir(&file_assignment_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&file_member_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &file_member_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active timer member lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&file_member_unit) != fingerprint ||
      !validate_file_member_ir(&file_member_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&bit_field_member_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &bit_field_member_unit, &ir);
  if (!check_status(status, CTOOL_OK, "bit-field member lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&bit_field_member_unit) != fingerprint ||
      !validate_bit_field_ir(&bit_field_member_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  index = find_member(&bit_field_member_unit, "r");
  bit_field_layouts = (ctool_c_member_layout_t *)malloc(
      (size_t)bit_field_member_unit.layout.member_count *
      sizeof(*bit_field_layouts));
  bit_field_members = (ctool_c_record_member_t *)malloc(
      (size_t)bit_field_member_unit.graph.member_count *
      sizeof(*bit_field_members));
  if (index == CTOOL_C_AST_NONE || bit_field_layouts == NULL ||
      bit_field_members == NULL) {
    goto cleanup;
  }
  (void)memcpy(bit_field_layouts, bit_field_member_unit.layout.members,
               (size_t)bit_field_member_unit.layout.member_count *
                   sizeof(*bit_field_layouts));
  bit_field_layouts[index].bit_offset = 31u;
  invalid_unit = bit_field_member_unit;
  invalid_unit.layout.members = bit_field_layouts;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "bit-field range outside storage unit")) {
    goto cleanup;
  }
  (void)memcpy(bit_field_layouts, bit_field_member_unit.layout.members,
               (size_t)bit_field_member_unit.layout.member_count *
                   sizeof(*bit_field_layouts));
  bit_field_layouts[index].byte_offset = 0xffffffffu;
  invalid_unit.layout.members = bit_field_layouts;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "bit-field storage outside record")) {
    goto cleanup;
  }
  (void)memcpy(bit_field_members, bit_field_member_unit.graph.members,
               (size_t)bit_field_member_unit.graph.member_count *
                   sizeof(*bit_field_members));
  bit_field_members[index].bit_width++;
  invalid_unit = bit_field_member_unit;
  invalid_unit.graph.members = bit_field_members;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "bit-field graph and layout width mismatch")) {
    goto cleanup;
  }
  for (index = 0u; index < file_member_unit.expression_count; index++) {
    if (file_member_unit.expressions[index].kind ==
        CTOOL_C_EXPRESSION_MEMBER) {
      break;
    }
  }
  member_expressions = (ctool_c_expression_t *)malloc(
      (size_t)file_member_unit.expression_count *
      sizeof(*member_expressions));
  member_layouts = (ctool_c_member_layout_t *)malloc(
      (size_t)file_member_unit.layout.member_count *
      sizeof(*member_layouts));
  if (index == file_member_unit.expression_count ||
      member_expressions == NULL || member_layouts == NULL ||
      file_member_unit.layout.member_count == 0u) {
    goto cleanup;
  }
  (void)memcpy(member_expressions, file_member_unit.expressions,
               (size_t)file_member_unit.expression_count *
                   sizeof(*member_expressions));
  member_expressions[index].reference = file_member_unit.graph.member_count;
  invalid_unit = file_member_unit;
  invalid_unit.expressions = member_expressions;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "out-of-range member identity")) {
    goto cleanup;
  }
  (void)memcpy(member_layouts, file_member_unit.layout.members,
               (size_t)file_member_unit.layout.member_count *
                   sizeof(*member_layouts));
  member_layouts[file_member_unit.expressions[index].reference].byte_offset =
      0xffffffffu;
  invalid_unit = file_member_unit;
  invalid_unit.layout.members = member_layouts;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "member layout outside its record")) {
    goto cleanup;
  }

  for (index = 0u; index < file_assignment_unit.expression_count; index++) {
    if (file_assignment_unit.expressions[index].kind ==
        CTOOL_C_EXPRESSION_ASSIGNMENT) {
      break;
    }
  }
  assignment_expressions = (ctool_c_expression_t *)malloc(
      (size_t)file_assignment_unit.expression_count *
      sizeof(*assignment_expressions));
  assignment_children = (ctool_u32 *)malloc(
      (size_t)file_assignment_unit.expression_child_count *
      sizeof(*assignment_children));
  if (index == file_assignment_unit.expression_count ||
      assignment_expressions == NULL || assignment_children == NULL ||
      file_assignment_unit.expressions[index].child_count != 2u ||
      file_assignment_unit.expressions[index].first_child >
          file_assignment_unit.expression_child_count ||
      2u > file_assignment_unit.expression_child_count -
               file_assignment_unit.expressions[index].first_child) {
    goto cleanup;
  }
  (void)memcpy(assignment_expressions, file_assignment_unit.expressions,
               (size_t)file_assignment_unit.expression_count *
                   sizeof(*assignment_expressions));
  assignment_expressions[index].computation_type =
      file_assignment_unit.function_definitions[0].declared_type;
  invalid_unit = file_assignment_unit;
  invalid_unit.expressions = assignment_expressions;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "mismatched assignment computation type")) {
    goto cleanup;
  }
  (void)memcpy(assignment_children,
               file_assignment_unit.expression_children,
               (size_t)file_assignment_unit.expression_child_count *
                   sizeof(*assignment_children));
  assignment_children[file_assignment_unit.expressions[index].first_child] =
      file_assignment_unit.expression_count;
  invalid_unit = file_assignment_unit;
  invalid_unit.expression_children = assignment_children;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "out-of-range assignment child")) {
    goto cleanup;
  }
  (void)memcpy(assignment_children,
               file_assignment_unit.expression_children,
               (size_t)file_assignment_unit.expression_child_count *
                   sizeof(*assignment_children));
  assignment_children[file_assignment_unit.expressions[index].first_child] =
      assignment_children[file_assignment_unit.expressions[index].first_child +
                          1u];
  invalid_unit = file_assignment_unit;
  invalid_unit.expression_children = assignment_children;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "value-producing assignment destination")) {
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&chained_assignment_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &chained_assignment_unit, &ir);
  if (!check_status(status, CTOOL_OK, "chained assignment lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&chained_assignment_unit) != fingerprint ||
      !validate_chained_assignment_ir(&chained_assignment_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&local_parameter_assignment_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &local_parameter_assignment_unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "local/parameter assignment lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&local_parameter_assignment_unit) != fingerprint ||
      !validate_local_parameter_assignment_ir(
          &local_parameter_assignment_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  call_fixture = make_call_fixture();
  if (call_fixture == NULL ||
      !parse_source(job, "/active-direct-calls.c", call_fixture,
                    &call_unit)) {
    (void)fprintf(stderr, "active call setup failed\n");
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&call_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &call_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active direct call lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&call_unit) != fingerprint ||
      !validate_call_ir(&call_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&active_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &active_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active helper lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&active_unit) != fingerprint ||
      !validate_active_ir(&active_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&logic_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &logic_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active logic helper lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&logic_unit) != fingerprint ||
      !validate_logic_ir(&logic_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&division_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &division_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active division helper lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&division_unit) != fingerprint ||
      !validate_division_ir(&division_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&logical_or_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &logical_or_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active logical-or helper lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&logical_or_unit) != fingerprint ||
      !validate_logical_or_ir(&logical_or_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&branch_fit_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &branch_fit_unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "active branch-range helper lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&branch_fit_unit) != fingerprint ||
      !validate_branch_fit_ir(&branch_fit_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&aes_rotw_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &aes_rotw_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active AES word-rotation lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&aes_rotw_unit) != fingerprint ||
      !validate_aes_rotw_ir(&aes_rotw_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&align_up_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &align_up_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active memory alignment lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&align_up_unit) != fingerprint ||
      !validate_align_up_ir(&align_up_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&integer_unary_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &integer_unary_unit, &ir);
  if (!check_status(status, CTOOL_OK, "integer unary lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&integer_unary_unit) != fingerprint ||
      !validate_integer_unary_ir(&integer_unary_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&conversion_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &conversion_unit, &ir);
  if (!check_status(status, CTOOL_OK, "integer cast lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&conversion_unit) != fingerprint ||
      !validate_integer_cast_ir(&conversion_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  cast_expression = CTOOL_C_AST_NONE;
  for (index = 0u; index < conversion_unit.expression_count; index++) {
    if (conversion_unit.expressions[index].kind ==
        CTOOL_C_EXPRESSION_CAST) {
      cast_expression = index;
      break;
    }
  }
  cast_expressions = (ctool_c_expression_t *)malloc(
      (size_t)conversion_unit.expression_count * sizeof(*cast_expressions));
  if (cast_expression == CTOOL_C_AST_NONE || cast_expressions == NULL) {
    (void)fprintf(stderr, "integer cast invalid-unit setup failed\n");
    goto cleanup;
  }
  (void)memcpy(cast_expressions, conversion_unit.expressions,
               (size_t)conversion_unit.expression_count *
                   sizeof(*cast_expressions));
  cast_expressions[cast_expression].conversion =
      CTOOL_C_CONVERSION_ASSIGNMENT;
  invalid_unit = conversion_unit;
  invalid_unit.expressions = cast_expressions;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "malformed explicit integer cast") ||
      unit_fingerprint(&conversion_unit) != fingerprint) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&signed_bits_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &signed_bits_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active signed-bit lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&signed_bits_unit) != fingerprint ||
      !validate_signed_bits_ir(&signed_bits_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&integer_unary_unit);
  logical_not_expression = CTOOL_C_AST_NONE;
  unsigned_unary_type = CTOOL_C_TYPE_NONE;
  for (index = 0u; index < integer_unary_unit.expression_count; index++) {
    const ctool_c_expression_t *expression =
        &integer_unary_unit.expressions[index];
    if (expression->kind == CTOOL_C_EXPRESSION_UNARY &&
        expression->operation == CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_NOT) {
      logical_not_expression = index;
    } else if (expression->kind == CTOOL_C_EXPRESSION_UNARY &&
               expression->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE &&
               expression->type < integer_unary_unit.layout.type_count &&
               integer_unary_unit.layout.types[expression->type].is_signed ==
                   CTOOL_FALSE) {
      unsigned_unary_type = expression->type;
    }
  }
  integer_unary_expressions = (ctool_c_expression_t *)malloc(
      (size_t)integer_unary_unit.expression_count *
      sizeof(*integer_unary_expressions));
  if (logical_not_expression == CTOOL_C_AST_NONE ||
      unsigned_unary_type == CTOOL_C_TYPE_NONE ||
      integer_unary_expressions == NULL) {
    (void)fprintf(stderr, "integer unary invalid-unit setup failed\n");
    goto cleanup;
  }
  (void)memcpy(integer_unary_expressions, integer_unary_unit.expressions,
               (size_t)integer_unary_unit.expression_count *
                   sizeof(*integer_unary_expressions));
  integer_unary_expressions[logical_not_expression].type =
      unsigned_unary_type;
  invalid_unit = integer_unary_unit;
  invalid_unit.expressions = integer_unary_expressions;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "mismatched logical-not result type") ||
      unit_fingerprint(&integer_unary_unit) != fingerprint) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&align_up_unit);
  complement_expression = CTOOL_C_AST_NONE;
  signed_expression = CTOOL_C_AST_NONE;
  for (index = 0u; index < align_up_unit.expression_count; index++) {
    const ctool_c_expression_t *expression =
        &align_up_unit.expressions[index];
    if (expression->kind == CTOOL_C_EXPRESSION_UNARY &&
        expression->operation == CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT) {
      complement_expression = index;
    } else if (expression->kind == CTOOL_C_EXPRESSION_INTEGER_CONSTANT &&
               align_up_unit.layout.types[expression->type].is_signed ==
                   CTOOL_TRUE) {
      signed_expression = index;
    }
  }
  align_up_expressions = (ctool_c_expression_t *)malloc(
      (size_t)align_up_unit.expression_count * sizeof(*align_up_expressions));
  if (complement_expression == CTOOL_C_AST_NONE ||
      signed_expression == CTOOL_C_AST_NONE || align_up_expressions == NULL) {
    (void)fprintf(stderr, "memory alignment invalid-unit setup failed\n");
    goto cleanup;
  }
  (void)memcpy(align_up_expressions, align_up_unit.expressions,
               (size_t)align_up_unit.expression_count *
                   sizeof(*align_up_expressions));
  align_up_expressions[complement_expression].type =
      align_up_unit.expressions[signed_expression].type;
  invalid_unit = align_up_unit;
  invalid_unit.expressions = align_up_expressions;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "mismatched bitwise complement result type") ||
      unit_fingerprint(&align_up_unit) != fingerprint) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&simd_cpuid_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &simd_cpuid_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active CPUID toggle lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&simd_cpuid_unit) != fingerprint ||
      !validate_simd_cpuid_ir(&simd_cpuid_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  xor_expression = CTOOL_C_AST_NONE;
  comparison_expression = CTOOL_C_AST_NONE;
  for (index = 0u; index < simd_cpuid_unit.expression_count; index++) {
    const ctool_c_expression_t *expression =
        &simd_cpuid_unit.expressions[index];
    if (expression->kind == CTOOL_C_EXPRESSION_BINARY &&
        expression->operation == CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR) {
      xor_expression = index;
    } else if (expression->kind == CTOOL_C_EXPRESSION_BINARY &&
               expression->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL) {
      comparison_expression = index;
    }
  }
  cpuid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)simd_cpuid_unit.expression_count * sizeof(*cpuid_expressions));
  if (xor_expression == CTOOL_C_AST_NONE ||
      comparison_expression == CTOOL_C_AST_NONE ||
      simd_cpuid_unit.expressions[xor_expression].type ==
          simd_cpuid_unit.expressions[comparison_expression].type ||
      cpuid_expressions == NULL) {
    (void)fprintf(stderr, "CPUID toggle invalid-unit setup failed\n");
    goto cleanup;
  }
  (void)memcpy(cpuid_expressions, simd_cpuid_unit.expressions,
               (size_t)simd_cpuid_unit.expression_count *
                   sizeof(*cpuid_expressions));
  cpuid_expressions[xor_expression].type =
      simd_cpuid_unit.expressions[comparison_expression].type;
  invalid_unit = simd_cpuid_unit;
  invalid_unit.expressions = cpuid_expressions;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "mismatched XOR result type") ||
      unit_fingerprint(&simd_cpuid_unit) != fingerprint) {
    goto cleanup;
  }

  if (!parse_source(job, "/simple-leaves.c", simple_source, &simple_unit)) {
    goto cleanup;
  }

  if (!parse_source(job, "/inline-success.c", inline_success_source,
                    &inline_success_unit)) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &inline_success_unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "static inline and later non-inline definition lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      !validate_inline_ir(&inline_success_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  if (!parse_source(job, "/external-inline.c", external_inline_source,
                    &external_inline_unit) ||
      !expect_ir_failure(
          job, &external_inline_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_EXTERNAL_INLINE,
          "CupidC IR lowering requires external-inline finalization before "
          "lowering this definition",
          "external inline definition") ||
      !parse_source(job, "/extern-inline.c", extern_inline_source,
                    &extern_inline_unit) ||
      !expect_ir_failure(
          job, &extern_inline_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_EXTERNAL_INLINE,
          "CupidC IR lowering requires external-inline finalization before "
          "lowering this definition",
          "extern inline definition")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &simple_unit, &ir);
  if (!check_status(status, CTOOL_OK, "simple leaf lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      !validate_simple_ir(&simple_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  invalid_unit = active_unit;
  invalid_definition = active_unit.function_definitions[0];
  invalid_definition.body = active_unit.statement_count;
  invalid_unit.function_definitions = &invalid_definition;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid function body")) {
    goto cleanup;
  }

  if (!parse_source(job, "/selection.c", statement_source, &statement_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&statement_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &statement_unit, &ir);
  if (!check_status(status, CTOOL_OK, "selection lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&statement_unit) != fingerprint ||
      !validate_selection_ir(&statement_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/selection-edges.c", selection_edge_source,
                    &selection_edge_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&selection_edge_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &selection_edge_unit, &ir);
  if (!check_status(status, CTOOL_OK, "selection edge lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&selection_edge_unit) != fingerprint ||
      !validate_selection_edge_ir(&selection_edge_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/active-while.c", while_source, &while_unit)) {
    goto cleanup;
  }
  while_fingerprint = unit_fingerprint(&while_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &while_unit, &ir);
  if (!check_status(status, CTOOL_OK, "while lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&while_unit) != while_fingerprint ||
      !validate_while_ir(&while_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/wide-while.c", wide_while_source,
                    &wide_while_unit) ||
      !expect_ir_failure(
          job, &wide_while_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide while condition")) {
    goto cleanup;
  }
  if (!parse_source(job, "/terminal-while.c", terminal_while_source,
                    &terminal_while_unit)) {
    goto cleanup;
  }
  if (!parse_source(job, "/active-do.c", do_source, &do_unit)) {
    goto cleanup;
  }
  do_fingerprint = unit_fingerprint(&do_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &do_unit, &ir);
  if (!check_status(status, CTOOL_OK, "do lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&do_unit) != do_fingerprint ||
      !validate_do_ir(&do_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/terminal-do.c", terminal_do_source,
                    &terminal_do_unit)) {
    goto cleanup;
  }
  terminal_do_fingerprint = unit_fingerprint(&terminal_do_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &terminal_do_unit, &ir);
  if (!check_status(status, CTOOL_OK, "terminal do lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&terminal_do_unit) != terminal_do_fingerprint ||
      !validate_terminal_do_ir(&terminal_do_unit, &ir) ||
      !parse_source(job, "/wide-do.c", wide_do_source, &wide_do_unit) ||
      !expect_ir_failure(
          job, &wide_do_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide do condition") ||
      !parse_source(job, "/terminal-wide-do.c", terminal_wide_do_source,
                    &terminal_wide_do_unit) ||
      !expect_ir_failure(
          job, &terminal_wide_do_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "unreachable wide do condition")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/active-for.c", for_source, &for_unit)) {
    goto cleanup;
  }
  for_fingerprint = unit_fingerprint(&for_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &for_unit, &ir);
  if (!check_status(status, CTOOL_OK, "for lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&for_unit) != for_fingerprint ||
      !validate_for_ir(&for_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/for-edges.c", for_edge_source,
                    &for_edge_unit)) {
    goto cleanup;
  }
  for_edge_fingerprint = unit_fingerprint(&for_edge_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &for_edge_unit, &ir);
  if (!check_status(status, CTOOL_OK, "for edge lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&for_edge_unit) != for_edge_fingerprint ||
      !validate_for_edge_ir(&for_edge_unit, &ir) ||
      !parse_source(job, "/wide-for.c", wide_for_source,
                    &wide_for_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &wide_for_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide for condition") ||
      !parse_source(job, "/terminal-wide-for-iteration.c",
                    terminal_wide_for_iteration_source,
                    &terminal_wide_for_iteration_unit) ||
      !parse_source(job, "/wide-declaration-for.c",
                    wide_declaration_for_source,
                    &wide_declaration_for_unit) ||
      !parse_source(job, "/declaration-for.c", declaration_for_source,
                    &declaration_for_unit)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  wide_declaration_for_fingerprint =
      unit_fingerprint(&wide_declaration_for_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &wide_declaration_for_unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "wide declaration for initializer") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&wide_declaration_for_unit) !=
          wide_declaration_for_fingerprint ||
      ir.function_count != 1u || ir.instruction_count == 0u) {
    (void)fprintf(stderr,
                  "wide declaration-for IR inventory differs: "
                  "functions=%u instructions=%u\n",
                  (unsigned int)ir.function_count,
                  (unsigned int)ir.instruction_count);
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  terminal_wide_for_iteration_fingerprint =
      unit_fingerprint(&terminal_wide_for_iteration_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &terminal_wide_for_iteration_unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "terminal wide for iteration lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&terminal_wide_for_iteration_unit) !=
          terminal_wide_for_iteration_fingerprint ||
      !validate_terminal_wide_for_iteration_ir(
          &terminal_wide_for_iteration_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  declaration_for_fingerprint = unit_fingerprint(&declaration_for_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &declaration_for_unit, &ir);
  if (!check_status(status, CTOOL_OK, "for declaration lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&declaration_for_unit) !=
          declaration_for_fingerprint ||
      !validate_declaration_for_ir(&declaration_for_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/nested-declaration.c",
                    nested_declaration_source,
                    &nested_declaration_unit)) {
    goto cleanup;
  }
  nested_declaration_fingerprint =
      unit_fingerprint(&nested_declaration_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &nested_declaration_unit, &ir);
  if (!check_status(status, CTOOL_OK, "nested declaration lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&nested_declaration_unit) !=
          nested_declaration_fingerprint ||
      !validate_nested_declaration_ir(&nested_declaration_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  nested_declaration_statements = (ctool_c_statement_t *)malloc(
      (size_t)nested_declaration_unit.statement_count *
      sizeof(*nested_declaration_statements));
  if (nested_declaration_statements == NULL) {
    goto cleanup;
  }
  (void)memcpy(nested_declaration_statements,
               nested_declaration_unit.statements,
               (size_t)nested_declaration_unit.statement_count *
                   sizeof(*nested_declaration_statements));
  for (index = 0u; index < nested_declaration_unit.statement_count; index++) {
    if (nested_declaration_statements[index].kind ==
            CTOOL_C_STATEMENT_DECLARATION &&
        nested_declaration_statements[index].first_block_binding == 1u) {
      nested_declaration_statements[index].first_block_binding = 0u;
      break;
    }
  }
  invalid_unit = nested_declaration_unit;
  invalid_unit.statements = nested_declaration_statements;
  if (index == nested_declaration_unit.statement_count ||
      !expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "overlapping nested declaration bindings") ||
      unit_fingerprint(&nested_declaration_unit) !=
          nested_declaration_fingerprint ||
      !parse_source(job, "/loop-declarations.c", loop_declaration_source,
                    &loop_declaration_unit)) {
    goto cleanup;
  }
  loop_declaration_fingerprint = unit_fingerprint(&loop_declaration_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &loop_declaration_unit, &ir);
  if (!check_status(status, CTOOL_OK, "loop declaration lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&loop_declaration_unit) !=
          loop_declaration_fingerprint ||
      !validate_loop_declaration_ir(&loop_declaration_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  terminal_while_fingerprint = unit_fingerprint(&terminal_while_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &terminal_while_unit, &ir);
  if (!check_status(status, CTOOL_OK, "terminal while lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&terminal_while_unit) != terminal_while_fingerprint ||
      !validate_terminal_while_ir(&terminal_while_unit, &ir)) {
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
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT, CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid unreachable selection condition") ||
      unit_fingerprint(&selection_edge_unit) != fingerprint ||
      !parse_source(job, "/unreachable-declaration.c",
                    unreachable_declaration_source,
                    &unreachable_declaration_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unreachable_declaration_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unreachable_declaration_unit, &ir);
  if (!check_status(status, CTOOL_OK, "unreachable declaration lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unreachable_declaration_unit) != fingerprint ||
      !validate_unreachable_declaration_ir(
          &unreachable_declaration_unit, &ir, "unreachable_declaration",
          "/unreachable-declaration.c")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/unreachable-wide-declaration.c",
                    unreachable_wide_declaration_source,
                    &unreachable_wide_declaration_unit)) {
    goto cleanup;
  }
  unreachable_wide_declaration_fingerprint =
      unit_fingerprint(&unreachable_wide_declaration_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unreachable_wide_declaration_unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "unreachable wide declaration lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unreachable_wide_declaration_unit) !=
          unreachable_wide_declaration_fingerprint ||
      !validate_unreachable_declaration_ir(
          &unreachable_wide_declaration_unit, &ir,
          "unreachable_wide_declaration",
          "/unreachable-wide-declaration.c")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&statement_unit);
  selection_statements = (ctool_c_statement_t *)malloc(
      (size_t)statement_unit.statement_count * sizeof(*selection_statements));
  if (selection_statements == NULL) {
    goto cleanup;
  }
  (void)memcpy(selection_statements, statement_unit.statements,
               (size_t)statement_unit.statement_count *
                   sizeof(*selection_statements));
  for (index = 0u; index < statement_unit.statement_count; index++) {
    if (selection_statements[index].kind == CTOOL_C_STATEMENT_IF) {
      selection_statements[index].condition = statement_unit.expression_count;
      break;
    }
  }
  invalid_unit = statement_unit;
  invalid_unit.statements = selection_statements;
  if (index == statement_unit.statement_count ||
      !expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid selection condition") ||
      unit_fingerprint(&statement_unit) != fingerprint ||
      !parse_source(job, "/wide-selection.c", wide_selection_source,
                    &wide_selection_unit) ||
      !expect_ir_failure(
          job, &wide_selection_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide selection condition") ||
      !parse_source(job, "/nonvoid-selection-fallthrough.c",
                    nonvoid_selection_fallthrough_source,
                    &nonvoid_selection_fallthrough_unit) ||
      !expect_ir_failure(
          job, &nonvoid_selection_fallthrough_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_STATEMENT,
          "CupidC IR lowering does not yet support this statement",
          "nonvoid selection fallthrough") ||
      !parse_source(job, "/break-statement.c", break_statement_source,
                    &break_statement_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&break_statement_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &break_statement_unit, &ir);
  if (!check_status(status, CTOOL_OK, "break statement lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&break_statement_unit) != fingerprint ||
      !validate_break_ir(&break_statement_unit, &ir) ||
      !parse_source(job, "/continue-statement.c", continue_statement_source,
                    &continue_statement_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&continue_statement_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &continue_statement_unit, &ir);
  if (!check_status(status, CTOOL_OK, "continue statement lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&continue_statement_unit) != fingerprint ||
      !validate_continue_ir(&continue_statement_unit, &ir)) {
    goto cleanup;
  }
  loop_control_statements = (ctool_c_statement_t *)malloc(
      (size_t)break_statement_unit.statement_count *
      sizeof(*loop_control_statements));
  if (loop_control_statements == NULL) {
    goto cleanup;
  }
  (void)memcpy(loop_control_statements, break_statement_unit.statements,
               (size_t)break_statement_unit.statement_count *
                   sizeof(*loop_control_statements));
  for (index = 0u; index < break_statement_unit.statement_count; index++) {
    if (loop_control_statements[index].kind == CTOOL_C_STATEMENT_BREAK) {
      break;
    }
  }
  if (index == break_statement_unit.statement_count) {
    goto cleanup;
  }
  loop_control_statements[index].expression = 0u;
  invalid_unit = break_statement_unit;
  invalid_unit.statements = loop_control_statements;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "break statement with expression payload")) {
    goto cleanup;
  }
  {
    const ctool_c_statement_t *body;
    const ctool_c_statement_t *loop;
    const ctool_c_statement_t *loop_body;
    ctool_u32 loop_index;
    ctool_u32 continue_index;
    if (continue_statement_unit.function_definition_count == 0u ||
        continue_statement_unit.statement_child_count == 0u) {
      goto cleanup;
    }
    body = &continue_statement_unit.statements
                [continue_statement_unit.function_definitions[0].body];
    if (body->kind != CTOOL_C_STATEMENT_COMPOUND ||
        body->child_count != 1u ||
        body->first_child >= continue_statement_unit.statement_child_count) {
      goto cleanup;
    }
    loop_index = continue_statement_unit.statement_children[body->first_child];
    if (loop_index >= continue_statement_unit.statement_count) {
      goto cleanup;
    }
    loop = &continue_statement_unit.statements[loop_index];
    if (loop->kind != CTOOL_C_STATEMENT_WHILE ||
        loop->body >= continue_statement_unit.statement_count) {
      goto cleanup;
    }
    loop_body = &continue_statement_unit.statements[loop->body];
    if (loop_body->kind != CTOOL_C_STATEMENT_COMPOUND ||
        loop_body->child_count != 1u ||
        loop_body->first_child >=
            continue_statement_unit.statement_child_count) {
      goto cleanup;
    }
    continue_index = continue_statement_unit
                         .statement_children[loop_body->first_child];
    if (continue_index >= continue_statement_unit.statement_count ||
        continue_statement_unit.statements[continue_index].kind !=
            CTOOL_C_STATEMENT_CONTINUE) {
      goto cleanup;
    }
    loop_control_children = (ctool_u32 *)malloc(
        (size_t)continue_statement_unit.statement_child_count *
        sizeof(*loop_control_children));
    if (loop_control_children == NULL) {
      goto cleanup;
    }
    (void)memcpy(loop_control_children,
                 continue_statement_unit.statement_children,
                 (size_t)continue_statement_unit.statement_child_count *
                     sizeof(*loop_control_children));
    loop_control_children[body->first_child] = continue_index;
    invalid_unit = continue_statement_unit;
    invalid_unit.statement_children = loop_control_children;
  }
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "continue statement without a loop context")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-local.c", wide_local_source,
                    &wide_local_unit)) {
    goto cleanup;
  }
  wide_local_fingerprint = unit_fingerprint(&wide_local_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &wide_local_unit, &ir);
  if (!check_status(status, CTOOL_OK, "wide automatic local") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&wide_local_unit) != wide_local_fingerprint ||
      ir.function_count != 1u || ir.instruction_count != 6u ||
      ir.instructions == NULL ||
      ir.instructions[0].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      ir.instructions[1].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      ir.instructions[2].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      ir.instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      ir.instructions[4].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      ir.instructions[5].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "wide automatic local IR stream differs\n");
    goto cleanup;
  }
  if (!parse_source_mode(job, "/overaligned-local.c",
                         overaligned_local_source, CTOOL_TRUE,
                         &overaligned_local_unit) ||
      !expect_ir_failure(
          job, &overaligned_local_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "over-aligned automatic local") ||
      !parse_source(job, "/array-local.c", array_local_source,
                    &array_local_unit)) {
    goto cleanup;
  }
  {
    ctool_u32 value_binding = find_block_binding(&array_local_unit, "value");
    fingerprint = unit_fingerprint(&array_local_unit);
    diagnostic_count = ctool_job_diagnostic_count(job);
    (void)memset(&ir, 0xa5, sizeof(ir));
    status = ctool_c_lower_ir(job, &array_local_unit, &ir);
    if (!check_status(status, CTOOL_OK, "automatic string initializer") ||
        ctool_job_diagnostic_count(job) != diagnostic_count ||
        unit_fingerprint(&array_local_unit) != fingerprint ||
        value_binding == CTOOL_C_AST_NONE || ir.function_count != 1u ||
        ir.instruction_count != 6u || ir.instructions == NULL ||
        ir.instructions[0].kind !=
            CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
        ir.instructions[0].reference != value_binding ||
        ir.instructions[1].kind != CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT ||
        ir.instructions[2].kind !=
            CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
        ir.instructions[2].reference != value_binding ||
        ir.instructions[3].kind != CTOOL_C_IR_INSTRUCTION_COPY_STRING ||
        ir.instructions[3].reference !=
            array_local_unit.block_bindings[value_binding].initializer ||
        ir.instructions[4].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
        ir.instructions[5].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
      (void)ctool_job_render_diagnostics(job);
      (void)fprintf(stderr, "automatic string initializer IR differs\n");
      goto cleanup;
    }
  }
  if (!parse_source(job, "/static-local.c", static_local_source,
                    &static_local_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&static_local_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &static_local_unit, &ir);
  if (!check_status(status, CTOOL_OK, "static block-local lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&static_local_unit) != fingerprint ||
      !validate_static_local_ir(&static_local_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/local-ownership.c", ownership_source,
                    &ownership_unit)) {
    goto cleanup;
  }
  invalid_statements = (ctool_c_statement_t *)malloc(
      (size_t)ownership_unit.statement_count * sizeof(*invalid_statements));
  if (invalid_statements == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_statements, ownership_unit.statements,
               (size_t)ownership_unit.statement_count *
                   sizeof(*invalid_statements));
  for (index = 0u; index < ownership_unit.statement_count; index++) {
    if (invalid_statements[index].kind == CTOOL_C_STATEMENT_DECLARATION &&
        invalid_statements[index].first_block_binding == 1u) {
      invalid_statements[index].first_block_binding = 0u;
      break;
    }
  }
  invalid_unit = ownership_unit;
  invalid_unit.statements = invalid_statements;
  if (index == ownership_unit.statement_count ||
      !expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "duplicate local ownership")) {
    goto cleanup;
  }
  ownership_expressions = (ctool_c_expression_t *)malloc(
      (size_t)ownership_unit.expression_count *
      sizeof(*ownership_expressions));
  if (ownership_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(ownership_expressions, ownership_unit.expressions,
               (size_t)ownership_unit.expression_count *
                   sizeof(*ownership_expressions));
  for (index = 0u; index < ownership_unit.expression_count; index++) {
    if (ownership_expressions[index].kind ==
            CTOOL_C_EXPRESSION_BLOCK_BINDING &&
        ownership_expressions[index].reference ==
            find_block_binding(&ownership_unit, "two")) {
      ownership_expressions[index].reference =
          find_block_binding(&ownership_unit, "one");
      break;
    }
  }
  invalid_unit = ownership_unit;
  invalid_unit.expressions = ownership_expressions;
  if (index == ownership_unit.expression_count ||
      !expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "cross-function local reference")) {
    goto cleanup;
  }
  if (!parse_source(job, "/active-vga-flip-ready.c",
                    global_frontier_source, &global_frontier_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&global_frontier_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &global_frontier_unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "active VGA flip readiness lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&global_frontier_unit) != fingerprint ||
      !validate_file_object_load_ir(&global_frontier_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/external-file-object.c",
                    external_file_object_source,
                    &external_file_object_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&external_file_object_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &external_file_object_unit, &ir);
  if (!check_status(status, CTOOL_OK, "external file-object lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&external_file_object_unit) != fingerprint ||
      !validate_external_file_object_ir(&external_file_object_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  file_expressions = (ctool_c_expression_t *)malloc(
      (size_t)external_file_object_unit.expression_count *
      sizeof(*file_expressions));
  if (file_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(file_expressions, external_file_object_unit.expressions,
               (size_t)external_file_object_unit.expression_count *
                   sizeof(*file_expressions));
  for (index = 0u; index < external_file_object_unit.expression_count;
       index++) {
    if (file_expressions[index].kind == CTOOL_C_EXPRESSION_IDENTIFIER &&
        file_expressions[index].reference ==
            find_binding(&external_file_object_unit, "external_clock")) {
      break;
    }
  }
  invalid_unit = external_file_object_unit;
  invalid_unit.expressions = file_expressions;
  if (index == external_file_object_unit.expression_count) {
    goto cleanup;
  }
  file_expressions[index].reference = external_file_object_unit.binding_count;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "out-of-range file-object binding")) {
    goto cleanup;
  }
  file_expressions[index].reference =
      find_binding(&external_file_object_unit, "read_external_clock");
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "mismatched file-object binding")) {
    goto cleanup;
  }
  file_expressions[index] = external_file_object_unit.expressions[index];
  file_expressions[index].first_child = 0u;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid file-object expression payload")) {
    goto cleanup;
  }
  file_bindings = (ctool_c_binding_t *)malloc(
      (size_t)external_file_object_unit.binding_count *
      sizeof(*file_bindings));
  file_binding = find_binding(&external_file_object_unit, "external_clock");
  if (file_bindings == NULL ||
      file_binding >= external_file_object_unit.binding_count) {
    goto cleanup;
  }
  (void)memcpy(file_bindings, external_file_object_unit.bindings,
               (size_t)external_file_object_unit.binding_count *
                   sizeof(*file_bindings));
  file_bindings[file_binding].linkage = CTOOL_C_LINKAGE_INTERNAL;
  invalid_unit = external_file_object_unit;
  invalid_unit.bindings = file_bindings;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "undefined internal file object")) {
    goto cleanup;
  }
  if (!parse_source(job, "/enumerator-identifier.c",
                    enumerator_identifier_source,
                    &enumerator_identifier_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&enumerator_identifier_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &enumerator_identifier_unit, &ir);
  if (!check_status(status, CTOOL_OK, "enumerator identifier") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&enumerator_identifier_unit) != fingerprint ||
      ir.function_count != 1u || ir.instruction_count != 2u ||
      ir.instructions == NULL ||
      ir.instructions[0].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      ir.instructions[0].integer_bits != 1u ||
      ir.instructions[1].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "enumerator identifier IR differs\n");
    goto cleanup;
  }
  if (!parse_source(job, "/unsupported-expression.c", expression_source,
                    &expression_unit) ||
      expression_unit.expression_count == 0u) {
    goto cleanup;
  }
  unsupported_expressions = (ctool_c_expression_t *)malloc(
      (size_t)expression_unit.expression_count *
      sizeof(*unsupported_expressions));
  if (unsupported_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(unsupported_expressions, expression_unit.expressions,
               (size_t)expression_unit.expression_count *
                   sizeof(*unsupported_expressions));
  for (index = 0u; index < expression_unit.expression_count; index++) {
    if (unsupported_expressions[index].kind ==
        CTOOL_C_EXPRESSION_INTEGER_CONSTANT) {
      unsupported_expressions[index].kind = (ctool_c_expression_kind_t)0;
      break;
    }
  }
  if (index == expression_unit.expression_count) {
    goto cleanup;
  }
  expression_unit.expressions = unsupported_expressions;
  if (!expect_ir_failure(
          job, &expression_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_EXPRESSION,
          "CupidC IR lowering does not yet support this expression",
          "unsupported expression")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-comparison.c", wide_comparison_source,
                    &wide_comparison_unit) ||
      !expect_ir_failure(
          job, &wide_comparison_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide less-than-or-equal expression")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-logical-or.c", wide_logical_or_source,
                    &wide_logical_or_unit) ||
      !expect_ir_failure(
          job, &wide_logical_or_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide logical-or operand")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-multiplication.c",
                    wide_multiplication_source,
                    &wide_multiplication_unit) ||
      !expect_ir_failure(
          job, &wide_multiplication_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide multiplication expression")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-left-shift.c", wide_left_shift_source,
                    &wide_left_shift_unit) ||
      !expect_ir_failure(
          job, &wide_left_shift_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide left-shift expression") ||
      !parse_source(job, "/wide-right-shift.c", wide_right_shift_source,
                    &wide_right_shift_unit) ||
      !expect_ir_failure(
          job, &wide_right_shift_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide right-shift expression") ||
      !parse_source(job, "/wide-unary-plus.c", wide_unary_plus_source,
                    &wide_unary_plus_unit) ||
      !expect_ir_failure(
          job, &wide_unary_plus_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide unary-plus expression") ||
      !parse_source(job, "/wide-unary-negate.c", wide_unary_negate_source,
                    &wide_unary_negate_unit) ||
      !expect_ir_failure(
          job, &wide_unary_negate_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide unary-negation expression") ||
      !parse_source(job, "/wide-logical-not.c", wide_logical_not_source,
                    &wide_logical_not_unit) ||
      !expect_ir_failure(
          job, &wide_logical_not_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide logical-not expression") ||
      !parse_source(job, "/wide-bitwise-or.c", wide_bitwise_or_source,
                    &wide_bitwise_or_unit) ||
      !expect_ir_failure(
          job, &wide_bitwise_or_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide bitwise-or expression") ||
      !parse_source(job, "/wide-bitwise-xor.c", wide_bitwise_xor_source,
                    &wide_bitwise_xor_unit) ||
      !expect_ir_failure(
          job, &wide_bitwise_xor_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide bitwise-xor expression") ||
      !parse_source(job, "/wide-bitwise-not.c", wide_bitwise_not_source,
                    &wide_bitwise_not_unit) ||
      !expect_ir_failure(
          job, &wide_bitwise_not_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide bitwise-complement expression")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-division.c", wide_division_source,
                    &wide_division_unit) ||
      !expect_ir_failure(
          job, &wide_division_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide division expression") ||
      !parse_source(job, "/wide-remainder.c", wide_remainder_source,
                    &wide_remainder_unit) ||
      !expect_ir_failure(
          job, &wide_remainder_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide remainder expression")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-cast.c", wide_cast_source,
                    &wide_cast_unit) ||
      !expect_ir_failure(
          job, &wide_cast_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide explicit integer cast")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-call.c", wide_call_source,
                    &wide_call_unit) ||
      !expect_ir_failure(
          job, &wide_call_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports calls with "
          "represented scalar or structure arguments and void, scalar, or "
          "structure results",
          "wide direct call")) {
    goto cleanup;
  }
  if (!parse_source(job, "/variadic-call.c", variadic_call_source,
                    &variadic_call_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&variadic_call_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &variadic_call_unit, &variadic_ir);
  if (!check_status(status, CTOOL_OK, "variadic call lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&variadic_call_unit) != fingerprint ||
      !validate_variadic_call_ir(&variadic_call_unit, &variadic_ir) ||
      !parse_source(job, "/variadic-structure.c",
                    variadic_structure_source, &variadic_structure_unit) ||
      !expect_ir_failure(
          job, &variadic_structure_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports arguments without declared parameter "
          "types only for represented scalar values",
          "variadic structure argument")) {
    goto cleanup;
  }
  if (!parse_source(job, "/value-statement.c", value_statement_source,
                    &value_statement_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&value_statement_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &value_statement_unit, &ir);
  if (!check_status(status, CTOOL_OK, "nonvoid expression statement") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&value_statement_unit) != fingerprint ||
      !validate_discard_ir(&value_statement_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/unsupported-abi.c", abi_source, &abi_unit) ||
      !expect_ir_failure(
          job, &abi_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports cdecl functions with represented "
          "scalar or structure parameters and void, scalar, or structure "
          "results",
          "unsupported ABI")) {
    goto cleanup;
  }

  if (!open_limited_job(host_root, &limited_adapter, &limited_job) ||
      !expect_ir_failure(
          limited_job, &active_unit, CTOOL_ERR_LIMIT,
          CTOOL_C_IR_DIAG_LIMIT,
          "CupidC IR lowering exceeded a configured resource limit",
          "limited IR storage")) {
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&active_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &active_unit, &ir);
  if (!check_status(status, CTOOL_OK, "same-job recovery") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&active_unit) != fingerprint ||
      !validate_active_ir(&active_unit, &ir)) {
    (void)fprintf(stderr, "same-job recovery differs\n");
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(unsupported_expressions);
  if (limited_job != NULL) {
    ctool_job_close(limited_job);
  }
  if (job != NULL) {
    ctool_job_close(job);
  }
  free(fixture);
  free(logic_fixture);
  free(division_fixture);
  free(logical_or_fixture);
  free(branch_fit_fixture);
  free(aes_rotw_fixture);
  free(align_up_fixture);
  free(cast_fixture);
  free(signed_bits_fixture);
  free(simd_cpuid_fixture);
  free(call_fixture);
  free(invalid_statements);
  free(nested_declaration_statements);
  free(selection_statements);
  free(unreachable_statements);
  free(loop_control_statements);
  free(loop_control_children);
  free(invalid_initializers);
  free(void_initializers);
  free(invalid_expressions);
  free(cpuid_expressions);
  free(align_up_expressions);
  free(integer_unary_expressions);
  free(cast_expressions);
  free(ownership_expressions);
  free(file_expressions);
  free(assignment_expressions);
  free(assignment_children);
  free(member_expressions);
  free(member_layouts);
  free(bit_field_layouts);
  free(bit_field_members);
  free(file_bindings);
  free(invalid_layouts);
  free(invalid_block_bindings);
  if (passed != 0) {
    (void)puts("active-leaf: ok");
    return 0;
  }
  return 1;
}

static int run_forward_goto(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_ir_unit_t ir;
  ctool_c_statement_t *invalid_statements = NULL;
  ctool_c_label_t *invalid_labels = NULL;
  ctool_u32 diagnostic_count;
  ctool_u32 goto_statement = CTOOL_C_AST_NONE;
  ctool_u32 statement_index;
  ctool_status_t status;
  uint64_t fingerprint;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/forward-goto.c", forward_goto_source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "forward goto lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_forward_goto_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (unit.statement_count == 0u || unit.label_count < 2u ||
      unit.expression_count == 0u ||
      sizeof(*invalid_statements) >
          SIZE_MAX / (size_t)unit.statement_count ||
      sizeof(*invalid_labels) > SIZE_MAX / (size_t)unit.label_count) {
    (void)fprintf(stderr, "forward goto invalid fixtures are too large\n");
    goto cleanup;
  }
  invalid_statements = (ctool_c_statement_t *)malloc(
      (size_t)unit.statement_count * sizeof(*invalid_statements));
  invalid_labels = (ctool_c_label_t *)malloc(
      (size_t)unit.label_count * sizeof(*invalid_labels));
  if (invalid_statements == NULL || invalid_labels == NULL) {
    (void)fprintf(stderr, "forward goto invalid fixture allocation failed\n");
    goto cleanup;
  }
  for (statement_index = 0u;
       statement_index <= unit.function_definitions[0].body;
       statement_index++) {
    if (unit.statements[statement_index].kind == CTOOL_C_STATEMENT_GOTO) {
      goto_statement = statement_index;
      break;
    }
  }
  if (goto_statement == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "forward goto statement was not found\n");
    goto cleanup;
  }

  (void)memcpy(invalid_statements, unit.statements,
               (size_t)unit.statement_count * sizeof(*invalid_statements));
  invalid_statements[goto_statement].expression = 0u;
  invalid_unit = unit;
  invalid_unit.statements = invalid_statements;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "goto expression payload")) {
    goto cleanup;
  }

  (void)memcpy(invalid_statements, unit.statements,
               (size_t)unit.statement_count * sizeof(*invalid_statements));
  invalid_statements[goto_statement].label =
      unit.function_definitions[1].first_label;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "cross-function goto")) {
    goto cleanup;
  }

  (void)memcpy(invalid_labels, unit.labels,
               (size_t)unit.label_count * sizeof(*invalid_labels));
  invalid_labels[0].statement = invalid_labels[1].statement;
  invalid_unit = unit;
  invalid_unit.labels = invalid_labels;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "label statement mismatch")) {
    goto cleanup;
  }

  invalid_unit = unit;
  invalid_unit.labels = NULL;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "missing label table") ||
      unit_fingerprint(&unit) != fingerprint) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_labels);
  free(invalid_statements);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("forward-goto: ok");
    return 0;
  }
  return 1;
}

static int run_nested_goto(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_ir_unit_t ir;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  uint64_t fingerprint;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/nested-goto.c", nested_goto_source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "nested goto lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_nested_goto_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("nested-goto: ok");
    return 0;
  }
  return 1;
}

static int switch_instruction_matches(
    const ctool_c_ir_instruction_t *instruction,
    ctool_c_ir_instruction_kind_t kind, ctool_u32 type,
    ctool_u32 input_type, ctool_c_expression_operator_t operation,
    ctool_c_conversion_kind_t conversion, ctool_u32 reference,
    ctool_u64 integer_bits) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == operation &&
                 instruction->conversion == conversion &&
                 instruction->reference == reference &&
                 instruction->integer_bits == integer_bits &&
                 string_equal(instruction->location.path,
                              "/switch-lowering.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/switch-lowering.c") != 0
             ? 1
             : 0;
}

static int validate_switch_lowering_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  static const ctool_u64 case_values[] = {1u, 2u, 3u, 4u, 5u, 0u};
  static const ctool_u32 next_case_targets[] = {9u, 15u, 21u,
                                                27u, 33u, 39u};
  static const ctool_u32 case_body_targets[] = {41u, 44u, 47u,
                                                50u, 53u, 56u};
  ctool_u32 index;
  if (unit->function_definition_count != 1u ||
      ir->function_count != 1u || ir->functions == NULL ||
      ir->instructions == NULL || ir->instruction_count != 59u ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != ir->instruction_count ||
      ir->functions[0].maximum_stack_depth != 3u ||
      !switch_instruction_matches(
          &ir->instructions[0], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          5u, CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 0u, 0u) ||
      !switch_instruction_matches(
          &ir->instructions[1], CTOOL_C_IR_INSTRUCTION_LOAD, 5u, 5u,
          CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !switch_instruction_matches(
          &ir->instructions[2], CTOOL_C_IR_INSTRUCTION_CONVERT, 1u, 5u,
          CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_INTEGER_PROMOTION, CTOOL_C_AST_NONE, 0u)) {
    (void)fprintf(stderr, "switch lowering IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < 6u; index++) {
    ctool_u32 base = 3u + index * 6u;
    if (!switch_instruction_matches(
            &ir->instructions[base],
            CTOOL_C_IR_INSTRUCTION_DUPLICATE_VALUE, 1u, 1u,
            CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
            CTOOL_C_AST_NONE, 0u) ||
        !switch_instruction_matches(
            &ir->instructions[base + 1u], CTOOL_C_IR_INSTRUCTION_INTEGER,
            1u, CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE,
            case_values[index]) ||
        !switch_instruction_matches(
            &ir->instructions[base + 2u], CTOOL_C_IR_INSTRUCTION_BINARY,
            0u, 1u, CTOOL_C_EXPRESSION_OPERATOR_EQUAL,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
        !switch_instruction_matches(
            &ir->instructions[base + 3u],
            CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 0u,
            CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
            next_case_targets[index], 0u) ||
        !switch_instruction_matches(
            &ir->instructions[base + 4u], CTOOL_C_IR_INSTRUCTION_DISCARD,
            CTOOL_C_TYPE_NONE, 1u, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
        !switch_instruction_matches(
            &ir->instructions[base + 5u], CTOOL_C_IR_INSTRUCTION_JUMP,
            CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
            CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
            case_body_targets[index], 0u)) {
      (void)fprintf(stderr, "switch lowering dispatch %u differs\n", index);
      return 0;
    }
  }
  if (!switch_instruction_matches(
          &ir->instructions[39], CTOOL_C_IR_INSTRUCTION_DISCARD,
          CTOOL_C_TYPE_NONE, 1u, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !switch_instruction_matches(
          &ir->instructions[40], CTOOL_C_IR_INSTRUCTION_JUMP,
          CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 56u,
          0u)) {
    (void)fprintf(stderr, "switch lowering default dispatch differs\n");
    return 0;
  }
  for (index = 0u; index < 6u; index++) {
    ctool_u32 base = 41u + index * 3u;
    if (!switch_instruction_matches(
            &ir->instructions[base], CTOOL_C_IR_INSTRUCTION_INTEGER, 0u,
            CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE,
            case_values[index]) ||
        !switch_instruction_matches(
            &ir->instructions[base + 1u], CTOOL_C_IR_INSTRUCTION_CONVERT,
            4u, 0u, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_ASSIGNMENT, CTOOL_C_AST_NONE, 0u) ||
        !switch_instruction_matches(
            &ir->instructions[base + 2u],
            CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 4u, 4u,
            CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
            CTOOL_C_AST_NONE, 0u)) {
      (void)fprintf(stderr, "switch lowering return %u differs\n", index);
      return 0;
    }
  }
  return 1;
}

static int run_switch_lowering(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t wide_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_ir_unit_t ir;
  ctool_c_statement_t *invalid_statements = NULL;
  ctool_c_binding_t *invalid_bindings = NULL;
  ctool_u32 diagnostic_count;
  ctool_u32 case_statement = CTOOL_C_AST_NONE;
  ctool_u32 default_statement = CTOOL_C_AST_NONE;
  ctool_u32 enumerator_binding = CTOOL_C_AST_NONE;
  ctool_u32 index;
  ctool_status_t status;
  uint64_t fingerprint;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_is_unchanged(job) ||
      !parse_source(job, "/switch-lowering.c", switch_lowering_source,
                    &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "switch lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_switch_lowering_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/wide-switch.c", wide_switch_source, &wide_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &wide_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide switch condition")) {
    goto cleanup;
  }
  if (unit.statement_count == 0u || unit.binding_count == 0u ||
      sizeof(*invalid_statements) >
          SIZE_MAX / (size_t)unit.statement_count ||
      sizeof(*invalid_bindings) > SIZE_MAX / (size_t)unit.binding_count) {
    (void)fprintf(stderr, "switch invalid fixtures are too large\n");
    goto cleanup;
  }
  invalid_statements = (ctool_c_statement_t *)malloc(
      (size_t)unit.statement_count * sizeof(*invalid_statements));
  invalid_bindings = (ctool_c_binding_t *)malloc(
      (size_t)unit.binding_count * sizeof(*invalid_bindings));
  if (invalid_statements == NULL || invalid_bindings == NULL) {
    (void)fprintf(stderr, "switch invalid fixture allocation failed\n");
    goto cleanup;
  }
  for (index = 0u; index < unit.statement_count; index++) {
    if (case_statement == CTOOL_C_AST_NONE &&
        unit.statements[index].kind == CTOOL_C_STATEMENT_CASE) {
      case_statement = index;
    }
    if (default_statement == CTOOL_C_AST_NONE &&
        unit.statements[index].kind == CTOOL_C_STATEMENT_DEFAULT) {
      default_statement = index;
    }
  }
  for (index = 0u; index < unit.binding_count; index++) {
    if (unit.bindings[index].kind == CTOOL_C_BINDING_ENUMERATOR) {
      enumerator_binding = index;
      break;
    }
  }
  if (case_statement == CTOOL_C_AST_NONE ||
      default_statement == CTOOL_C_AST_NONE ||
      enumerator_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "switch invalid fixture target was not found\n");
    goto cleanup;
  }

  (void)memcpy(invalid_statements, unit.statements,
               (size_t)unit.statement_count * sizeof(*invalid_statements));
  invalid_statements[case_statement].expression = unit.expression_count;
  invalid_unit = unit;
  invalid_unit.statements = invalid_statements;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "case expression range")) {
    goto cleanup;
  }

  (void)memcpy(invalid_statements, unit.statements,
               (size_t)unit.statement_count * sizeof(*invalid_statements));
  invalid_statements[case_statement].condition = 0u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "case condition payload")) {
    goto cleanup;
  }

  (void)memcpy(invalid_statements, unit.statements,
               (size_t)unit.statement_count * sizeof(*invalid_statements));
  invalid_statements[default_statement].expression = 0u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "default expression payload")) {
    goto cleanup;
  }

  (void)memcpy(invalid_bindings, unit.bindings,
               (size_t)unit.binding_count * sizeof(*invalid_bindings));
  invalid_bindings[enumerator_binding].integer_bits =
      0xffffffff00000000ull;
  invalid_unit = unit;
  invalid_unit.bindings = invalid_bindings;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "enumerator value width") ||
      unit_fingerprint(&unit) != fingerprint) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_bindings);
  free(invalid_statements);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("switch-lowering: ok");
    return 0;
  }
  return 1;
}

static int validate_switch_control_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  static const struct {
    ctool_u32 function;
    ctool_u32 source_line;
    ctool_u32 target_line;
  } expected_control_targets[] = {
      {0u, 6u, 12u}, {1u, 20u, 16u},
      {1u, 22u, 26u}, {1u, 27u, 29u}};
  ctool_u32 expected_target_index;
  ctool_u32 function_index;
  ctool_u32 jump_count = 0u;
  if (unit->function_definition_count != 4u ||
      ir->function_count != 4u || ir->functions == NULL ||
      ir->instructions == NULL || ir->instruction_count == 0u) {
    (void)fprintf(stderr, "switch control IR inventory differs\n");
    return 0;
  }
  for (function_index = 0u; function_index < ir->function_count;
       function_index++) {
    const ctool_c_ir_function_t *function = &ir->functions[function_index];
    ctool_u32 offset;
    if (function->first_instruction > ir->instruction_count ||
        function->instruction_count >
            ir->instruction_count - function->first_instruction) {
      (void)fprintf(stderr, "switch control function range differs\n");
      return 0;
    }
    for (offset = 0u; offset < function->instruction_count; offset++) {
      const ctool_c_ir_instruction_t *instruction =
          &ir->instructions[function->first_instruction + offset];
      if (instruction->kind == CTOOL_C_IR_INSTRUCTION_JUMP) {
        jump_count++;
      }
      if ((instruction->kind == CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
           instruction->kind == CTOOL_C_IR_INSTRUCTION_JUMP) &&
          (instruction->reference >= function->instruction_count ||
           instruction->integer_bits != 0u)) {
        (void)fprintf(stderr,
                      "switch control published an unresolved jump\n");
        return 0;
      }
    }
  }
  if (jump_count < 10u) {
    (void)fprintf(stderr, "switch control emitted too few control edges\n");
    return 0;
  }
  for (expected_target_index = 0u;
       expected_target_index <
           sizeof(expected_control_targets) / sizeof(expected_control_targets[0]);
       expected_target_index++) {
    const ctool_c_ir_function_t *function =
        &ir->functions[expected_control_targets[expected_target_index].function];
    ctool_u32 match_count = 0u;
    ctool_u32 offset;
    for (offset = 0u; offset < function->instruction_count; offset++) {
      const ctool_c_ir_instruction_t *instruction =
          &ir->instructions[function->first_instruction + offset];
      if (instruction->kind == CTOOL_C_IR_INSTRUCTION_JUMP &&
          instruction->location.line ==
              expected_control_targets[expected_target_index].source_line) {
        if (instruction->reference >= function->instruction_count ||
            ir->instructions[function->first_instruction +
                             instruction->reference]
                    .location.line !=
                expected_control_targets[expected_target_index].target_line) {
          (void)fprintf(stderr,
                        "switch control jump on line %u has the wrong target\n",
                        instruction->location.line);
          return 0;
        }
        match_count++;
      }
    }
    if (match_count != 1u) {
      (void)fprintf(stderr,
                    "switch control jump on line %u has %u matches\n",
                    expected_control_targets[expected_target_index].source_line,
                    match_count);
      return 0;
    }
  }
  {
    const ctool_c_ir_function_t *negative = &ir->functions[3];
    ctool_u32 negative_constant_count = 0u;
    ctool_u32 offset;
    for (offset = 0u; offset < negative->instruction_count; offset++) {
      const ctool_c_ir_instruction_t *instruction =
          &ir->instructions[negative->first_instruction + offset];
      if (instruction->kind == CTOOL_C_IR_INSTRUCTION_INTEGER &&
          instruction->location.line == 39u &&
          instruction->integer_bits == 0xffffffffu) {
        negative_constant_count++;
      }
    }
    if (negative_constant_count != 1u) {
      (void)fprintf(stderr,
                    "switch negative case did not retain 32-bit -1\n");
      return 0;
    }
  }
  return 1;
}

static int run_switch_control(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_ir_unit_t ir;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  uint64_t fingerprint;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/switch-control.c", switch_control_source,
                    &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "switch control lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_switch_control_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("switch-control: ok");
    return 0;
  }
  return 1;
}

static int validate_switch_nesting_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  static const ctool_u32 expected_equal_counts[] = {
      2u, 1u, 0u, 1u, 0u, 0u, 1u, 3u};
  static const ctool_u32 expected_return_counts[] = {
      1u, 2u, 1u, 2u, 1u, 1u, 2u, 4u};
  static const ctool_u32 minimum_stack_depths[] = {
      2u, 2u, 1u, 2u, 1u, 1u, 2u, 2u};
  ctool_u32 function_index;
  if (unit->function_definition_count != 8u || ir->function_count != 8u ||
      ir->functions == NULL || ir->instructions == NULL) {
    (void)fprintf(stderr, "switch nesting IR inventory differs\n");
    return 0;
  }
  for (function_index = 0u; function_index < ir->function_count;
       function_index++) {
    const ctool_c_ir_function_t *function =
        &ir->functions[function_index];
    ctool_u32 equal_count = 0u;
    ctool_u32 return_count = 0u;
    ctool_u32 offset;
    if (function->first_instruction > ir->instruction_count ||
        function->instruction_count >
            ir->instruction_count - function->first_instruction ||
        function->maximum_stack_depth <
            minimum_stack_depths[function_index]) {
      (void)fprintf(stderr, "switch nesting function range differs\n");
      return 0;
    }
    for (offset = 0u; offset < function->instruction_count; offset++) {
      const ctool_c_ir_instruction_t *instruction =
          &ir->instructions[function->first_instruction + offset];
      if (instruction->kind == CTOOL_C_IR_INSTRUCTION_BINARY &&
          instruction->operation == CTOOL_C_EXPRESSION_OPERATOR_EQUAL) {
        equal_count++;
      }
      if (instruction->kind == CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
        return_count++;
      }
      if ((instruction->kind == CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO ||
           instruction->kind == CTOOL_C_IR_INSTRUCTION_JUMP) &&
          (instruction->reference >= function->instruction_count ||
           instruction->integer_bits != 0u)) {
        (void)fprintf(stderr,
                      "switch nesting published an unresolved jump\n");
        return 0;
      }
    }
    if (equal_count != expected_equal_counts[function_index] ||
        return_count != expected_return_counts[function_index]) {
      (void)fprintf(stderr,
                    "switch nesting function %u differs: equal=%u "
                    "return=%u\n",
                    function_index, equal_count, return_count);
      return 0;
    }
  }
  return 1;
}

static int run_switch_nesting(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_ir_unit_t ir;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  uint64_t fingerprint;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/switch-nesting.c", switch_nesting_source,
                    &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "switch nesting lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_switch_nesting_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("switch-nesting: ok");
    return 0;
  }
  return 1;
}

static int validate_integer_update_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  static const char *const names[] = {
      "prefix_increment", "prefix_decrement",
      "postfix_increment", "postfix_decrement"};
  static const ctool_c_expression_operator_t update_operations[] = {
      CTOOL_C_EXPRESSION_OPERATOR_ADD,
      CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
      CTOOL_C_EXPRESSION_OPERATOR_ADD,
      CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT};
  static const ctool_c_expression_operator_t postfix_operations[] = {
      CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
      CTOOL_C_EXPRESSION_OPERATOR_ADD};
  ctool_u32 function_index;
  ctool_u32 instruction_cursor = 0u;
  if (unit->function_definition_count != 4u || ir->function_count != 4u ||
      ir->instruction_count != 32u || ir->functions == NULL ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "integer update IR inventory differs\n");
    return 0;
  }
  for (function_index = 0u; function_index < 4u; function_index++) {
    const ctool_c_function_definition_t *definition =
        &unit->function_definitions[function_index];
    const ctool_c_type_node_t *function_type;
    const ctool_c_ir_function_t *function = &ir->functions[function_index];
    const ctool_c_ir_instruction_t *instructions =
        &ir->instructions[instruction_cursor];
    ctool_u32 function_binding = find_binding(unit, names[function_index]);
    ctool_u32 parameter;
    ctool_u32 value_type;
    ctool_u32 expected_count = function_index < 2u ? 7u : 9u;
    ctool_u32 offset;
    if (definition->declared_type >= unit->graph.type_count ||
        function_binding == CTOOL_C_AST_NONE) {
      return 0;
    }
    function_type = &unit->graph.types[definition->declared_type];
    if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
        function_type->parameter_count != 1u ||
        function_type->first_parameter >= unit->parameter_count) {
      return 0;
    }
    parameter = function_type->first_parameter;
    value_type = unit->parameters[parameter].type;
    if (function_type->referenced_type != value_type ||
        definition->binding != function_binding ||
        function->binding != function_binding ||
        function->declared_type != definition->declared_type ||
        function->first_instruction != instruction_cursor ||
        function->instruction_count != expected_count ||
        function->maximum_stack_depth != 3u ||
        instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
        instructions[0].type != value_type ||
        instructions[0].input_type != CTOOL_C_TYPE_NONE ||
        instructions[0].reference != parameter ||
        instructions[1].kind != CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS ||
        instructions[1].type != value_type ||
        instructions[1].input_type != value_type ||
        instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
        instructions[2].type != value_type ||
        instructions[2].input_type != value_type ||
        instructions[2].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
        instructions[3].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
        instructions[3].type != value_type ||
        instructions[3].integer_bits != 1u ||
        instructions[4].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
        instructions[4].type != value_type ||
        instructions[4].input_type != value_type ||
        instructions[4].operation != update_operations[function_index] ||
        instructions[5].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
        instructions[5].type != value_type ||
        instructions[5].input_type != value_type) {
      (void)fprintf(stderr, "integer update function %u differs\n",
                    function_index);
      return 0;
    }
    if (function_index < 2u) {
      if (instructions[6].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
          instructions[6].type != value_type ||
          instructions[6].input_type != value_type) {
        return 0;
      }
    } else if (instructions[6].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
               instructions[6].type != value_type ||
               instructions[6].integer_bits != 1u ||
               instructions[7].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
               instructions[7].type != value_type ||
               instructions[7].input_type != value_type ||
               instructions[7].operation !=
                   postfix_operations[function_index] ||
               instructions[8].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
               instructions[8].type != value_type ||
               instructions[8].input_type != value_type) {
      (void)fprintf(stderr, "integer postfix result %u differs\n",
                    function_index);
      return 0;
    }
    for (offset = 0u; offset < expected_count; offset++) {
      if (!string_equal(instructions[offset].location.path,
                        "/integer-update.c") ||
          !string_equal(instructions[offset].physical_location.path,
                        "/integer-update.c")) {
        (void)fprintf(stderr, "integer update IR source path differs\n");
        return 0;
      }
    }
    instruction_cursor += expected_count;
  }
  return instruction_cursor == ir->instruction_count ? 1 : 0;
}

static int run_integer_updates(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_ir_unit_t ir;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  uint64_t fingerprint;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_is_unchanged(job) ||
      !parse_source(job, "/integer-update.c", integer_update_source,
                    &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "integer update lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_integer_update_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("integer-updates: ok");
    return 0;
  }
  return 1;
}

static int validate_compound_function(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir, ctool_u32 function_index,
    const char *name,
    const ctool_c_expression_operator_t *operations,
    ctool_u32 operation_count, ctool_bool is_signed,
    ctool_u32 *instruction_cursor_io) {
  const ctool_c_function_definition_t *definition =
      &unit->function_definitions[function_index];
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function = &ir->functions[function_index];
  const ctool_c_ir_instruction_t *instructions =
      &ir->instructions[*instruction_cursor_io];
  ctool_u32 function_binding = find_binding(unit, name);
  ctool_u32 value_parameter;
  ctool_u32 right_parameter;
  ctool_u32 value_type;
  ctool_u32 right_type;
  ctool_u32 expected_count = operation_count * 8u + 3u;
  ctool_u32 operation_index;
  ctool_u32 offset;
  if (definition->declared_type >= unit->graph.type_count ||
      function_binding == CTOOL_C_AST_NONE) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      function_type->first_parameter > unit->parameter_count ||
      2u > unit->parameter_count - function_type->first_parameter) {
    return 0;
  }
  value_parameter = function_type->first_parameter;
  right_parameter = value_parameter + 1u;
  value_type = unit->parameters[value_parameter].type;
  right_type = unit->parameters[right_parameter].type;
  if (value_type >= unit->layout.type_count || value_type != right_type ||
      unit->layout.types[value_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[value_type].size != 4u ||
      unit->layout.types[value_type].is_signed != is_signed ||
      function_type->referenced_type != value_type ||
      definition->binding != function_binding ||
      function->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != *instruction_cursor_io ||
      function->instruction_count != expected_count ||
      function->maximum_stack_depth != 3u) {
    (void)fprintf(stderr, "integer compound function inventory differs\n");
    return 0;
  }
  for (operation_index = 0u; operation_index < operation_count;
       operation_index++) {
    ctool_u32 base = operation_index * 8u;
    if (instructions[base].kind !=
            CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
        instructions[base].type != value_type ||
        instructions[base].reference != value_parameter ||
        instructions[base + 1u].kind !=
            CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS ||
        instructions[base + 1u].type != value_type ||
        instructions[base + 1u].input_type != value_type ||
        instructions[base + 2u].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
        instructions[base + 2u].type != value_type ||
        instructions[base + 2u].input_type != value_type ||
        instructions[base + 2u].conversion !=
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
        instructions[base + 3u].kind !=
            CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
        instructions[base + 3u].type != right_type ||
        instructions[base + 3u].reference != right_parameter ||
        instructions[base + 4u].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
        instructions[base + 4u].type != right_type ||
        instructions[base + 4u].input_type != right_type ||
        instructions[base + 4u].conversion !=
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
        instructions[base + 5u].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
        instructions[base + 5u].type != value_type ||
        instructions[base + 5u].input_type != value_type ||
        instructions[base + 5u].operation != operations[operation_index] ||
        instructions[base + 6u].kind !=
            CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
        instructions[base + 6u].type != value_type ||
        instructions[base + 6u].input_type != value_type ||
        instructions[base + 7u].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
        instructions[base + 7u].type != CTOOL_C_TYPE_NONE ||
        instructions[base + 7u].input_type != value_type) {
      (void)fprintf(stderr, "integer compound operation %u differs\n",
                    operation_index);
      return 0;
    }
  }
  offset = operation_count * 8u;
  if (instructions[offset].kind !=
          CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[offset].type != value_type ||
      instructions[offset].reference != value_parameter ||
      instructions[offset + 1u].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[offset + 1u].type != value_type ||
      instructions[offset + 1u].input_type != value_type ||
      instructions[offset + 2u].kind !=
          CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[offset + 2u].type != value_type ||
      instructions[offset + 2u].input_type != value_type) {
    (void)fprintf(stderr, "integer compound return differs\n");
    return 0;
  }
  for (offset = 0u; offset < expected_count; offset++) {
    if (!string_equal(instructions[offset].location.path,
                      "/integer-compound.c") ||
        !string_equal(instructions[offset].physical_location.path,
                      "/integer-compound.c")) {
      (void)fprintf(stderr, "integer compound IR source path differs\n");
      return 0;
    }
  }
  *instruction_cursor_io += expected_count;
  return 1;
}

static int validate_integer_compound_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  static const ctool_c_expression_operator_t all_operations[] = {
      CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY,
      CTOOL_C_EXPRESSION_OPERATOR_DIVIDE,
      CTOOL_C_EXPRESSION_OPERATOR_REMAINDER,
      CTOOL_C_EXPRESSION_OPERATOR_ADD,
      CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
      CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT,
      CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT,
      CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND,
      CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR,
      CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR};
  static const ctool_c_expression_operator_t signed_operations[] = {
      CTOOL_C_EXPRESSION_OPERATOR_DIVIDE,
      CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT};
  ctool_u32 instruction_cursor = 0u;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->instruction_count != 102u || ir->functions == NULL ||
      ir->instructions == NULL ||
      !validate_compound_function(
          unit, ir, 0u, "all_compounds", all_operations, 10u,
          CTOOL_FALSE, &instruction_cursor) ||
      !validate_compound_function(
          unit, ir, 1u, "signed_compounds", signed_operations, 2u,
          CTOOL_TRUE, &instruction_cursor)) {
    (void)fprintf(stderr, "integer compound IR differs\n");
    return 0;
  }
  return instruction_cursor == ir->instruction_count ? 1 : 0;
}

static int run_integer_compounds(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_ir_unit_t ir;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  uint64_t fingerprint;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/integer-compound.c", integer_compound_source,
                    &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "integer compound lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_integer_compound_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("integer-compounds: ok");
    return 0;
  }
  return 1;
}

static int validate_integer_compound_conversion_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_instruction_t *instructions = ir->instructions;
  ctool_u32 left_parameter;
  ctool_u32 right_parameter;
  ctool_u32 left_type;
  ctool_u32 right_type;
  ctool_u32 computation_type;
  ctool_u32 enum_type;
  ctool_u32 promoted_type;
  ctool_u32 index;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->instruction_count != 21u || ir->functions == NULL ||
      instructions == NULL ||
      unit->function_definitions[0].declared_type >= unit->graph.type_count) {
    (void)fprintf(stderr, "integer compound conversion inventory differs\n");
    return 0;
  }
  function_type =
      &unit->graph.types[unit->function_definitions[0].declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      function_type->first_parameter > unit->parameter_count ||
      2u > unit->parameter_count - function_type->first_parameter) {
    return 0;
  }
  left_parameter = function_type->first_parameter;
  right_parameter = left_parameter + 1u;
  left_type = unit->parameters[left_parameter].type;
  right_type = unit->parameters[right_parameter].type;
  computation_type = instructions[3].type;
  if (left_type == right_type || computation_type != right_type ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 10u ||
      ir->functions[0].maximum_stack_depth != 3u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != left_type ||
      instructions[0].reference != left_parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS ||
      instructions[1].type != left_type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[2].type != left_type ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[3].type != computation_type ||
      instructions[3].input_type != left_type ||
      instructions[3].conversion != CTOOL_C_CONVERSION_USUAL_ARITHMETIC ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[4].type != right_type ||
      instructions[4].reference != right_parameter ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[5].type != right_type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[6].type != computation_type ||
      instructions[6].input_type != computation_type ||
      instructions[6].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[7].type != left_type ||
      instructions[7].input_type != computation_type ||
      instructions[7].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[8].type != left_type ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[9].type != left_type) {
    (void)fprintf(stderr, "integer compound conversion IR differs\n");
    return 0;
  }
  if (unit->function_definitions[1].declared_type >=
      unit->graph.type_count) {
    return 0;
  }
  function_type =
      &unit->graph.types[unit->function_definitions[1].declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      function_type->first_parameter > unit->parameter_count ||
      2u > unit->parameter_count - function_type->first_parameter) {
    return 0;
  }
  left_parameter = function_type->first_parameter;
  right_parameter = left_parameter + 1u;
  enum_type = unit->parameters[left_parameter].type;
  right_type = unit->parameters[right_parameter].type;
  if (enum_type >= unit->graph.type_count ||
      unit->graph.types[enum_type].kind != CTOOL_C_TYPE_ENUM) {
    return 0;
  }
  promoted_type = unit->graph.types[enum_type].referenced_type;
  computation_type = instructions[14].type;
  if (promoted_type == enum_type || promoted_type == computation_type ||
      computation_type != right_type ||
      ir->functions[1].first_instruction != 10u ||
      ir->functions[1].instruction_count != 11u ||
      ir->functions[1].maximum_stack_depth != 3u ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[10].type != enum_type ||
      instructions[10].reference != left_parameter ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS ||
      instructions[11].type != enum_type ||
      instructions[12].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[12].type != enum_type ||
      instructions[13].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[13].type != promoted_type ||
      instructions[13].input_type != enum_type ||
      instructions[13].conversion != CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
      instructions[14].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[14].type != computation_type ||
      instructions[14].input_type != promoted_type ||
      instructions[14].conversion != CTOOL_C_CONVERSION_USUAL_ARITHMETIC ||
      instructions[15].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[15].type != right_type ||
      instructions[15].reference != right_parameter ||
      instructions[16].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[16].type != right_type ||
      instructions[17].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[17].type != computation_type ||
      instructions[17].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[18].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[18].type != enum_type ||
      instructions[18].input_type != computation_type ||
      instructions[18].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[19].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[19].type != enum_type ||
      instructions[20].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[20].type != enum_type) {
    (void)fprintf(stderr, "enum compound conversion IR differs\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (!string_equal(instructions[index].location.path,
                      "/integer-compound-conversion.c") ||
        !string_equal(instructions[index].physical_location.path,
                      "/integer-compound-conversion.c")) {
      return 0;
    }
  }
  return 1;
}

static int run_integer_compound_conversions(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_ir_unit_t ir;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  uint64_t fingerprint;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/integer-compound-conversion.c",
                    integer_compound_conversion_source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "integer compound conversion lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_integer_compound_conversion_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("integer-compound-conversions: ok");
    return 0;
  }
  return 1;
}

static int validate_integer_update_conversion_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_instruction_t *instructions = ir->instructions;
  const ctool_c_type_node_t *function_type;
  ctool_u32 enum_type;
  ctool_u32 computation_type;
  ctool_u32 state_binding = find_binding(unit, "update_state");
  ctool_u32 state_type;
  ctool_u32 value_type;
  ctool_u32 index;
  if (unit->function_definition_count != 3u || ir->function_count != 3u ||
      ir->instruction_count != 28u || ir->functions == NULL ||
      instructions == NULL || state_binding == CTOOL_C_AST_NONE ||
      unit->function_definitions[0].declared_type >= unit->graph.type_count) {
    (void)fprintf(stderr, "integer update conversion inventory differs\n");
    return 0;
  }
  function_type =
      &unit->graph.types[unit->function_definitions[0].declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  enum_type = unit->parameters[function_type->first_parameter].type;
  computation_type = instructions[3].type;
  state_type = unit->bindings[state_binding].type;
  value_type = instructions[21].type;
  if (enum_type >= unit->layout.type_count ||
      computation_type >= unit->layout.type_count ||
      state_type >= unit->graph.type_count ||
      value_type >= unit->layout.type_count ||
      unit->layout.types[enum_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[enum_type].size != 4u ||
      unit->layout.types[computation_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[computation_type].size != 4u ||
      enum_type == computation_type ||
      unit->graph.types[state_type].kind != CTOOL_C_TYPE_QUALIFIED ||
      unit->graph.types[state_type].referenced_type != value_type) {
    return 0;
  }
  if (ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 9u ||
      ir->functions[0].maximum_stack_depth != 3u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != enum_type ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS ||
      instructions[1].type != enum_type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[2].type != enum_type ||
      instructions[2].input_type != enum_type ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[3].type != computation_type ||
      instructions[3].input_type != enum_type ||
      instructions[3].conversion != CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[4].type != computation_type ||
      instructions[4].integer_bits != 1u ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[5].type != computation_type ||
      instructions[5].input_type != computation_type ||
      instructions[5].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[6].type != enum_type ||
      instructions[6].input_type != computation_type ||
      instructions[6].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[7].type != enum_type ||
      instructions[7].input_type != enum_type ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[8].type != enum_type) {
    (void)fprintf(stderr, "enum prefix update conversion differs\n");
    return 0;
  }
  if (ir->functions[1].first_instruction != 9u ||
      ir->functions[1].instruction_count != 10u ||
      ir->functions[1].maximum_stack_depth != 3u ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[9].type != enum_type ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS ||
      instructions[10].type != enum_type ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[11].type != enum_type ||
      instructions[12].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[12].type != computation_type ||
      instructions[12].conversion != CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
      instructions[13].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[14].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[14].type != computation_type ||
      instructions[14].input_type != instructions[13].type ||
      instructions[14].conversion != CTOOL_C_CONVERSION_USUAL_ARITHMETIC ||
      instructions[15].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[15].type != computation_type ||
      instructions[15].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[16].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[16].type != enum_type ||
      instructions[16].input_type != computation_type ||
      instructions[16].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[17].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[17].type != enum_type ||
      instructions[18].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[18].type != enum_type) {
    (void)fprintf(stderr, "enum compound update conversion differs\n");
    return 0;
  }
  if (ir->functions[2].first_instruction != 19u ||
      ir->functions[2].instruction_count != 9u ||
      ir->functions[2].maximum_stack_depth != 3u ||
      instructions[19].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[19].type != state_type ||
      instructions[19].reference != state_binding ||
      instructions[20].kind !=
          CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS ||
      instructions[20].type != state_type ||
      instructions[21].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[21].type != value_type ||
      instructions[21].input_type != state_type ||
      instructions[22].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[22].type != value_type ||
      instructions[23].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[23].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[24].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[24].type != value_type ||
      instructions[25].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[26].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[26].operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
      instructions[27].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[27].type != value_type) {
    (void)fprintf(stderr, "volatile postfix update differs\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (!string_equal(instructions[index].location.path,
                      "/integer-update-conversion.c") ||
        !string_equal(instructions[index].physical_location.path,
                      "/integer-update-conversion.c")) {
      return 0;
    }
  }
  return 1;
}

static int run_integer_update_conversions(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_ir_unit_t ir;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  uint64_t fingerprint;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/integer-update-conversion.c",
                    integer_update_conversion_source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "integer update conversion lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_integer_update_conversion_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("integer-update-conversions: ok");
    return 0;
  }
  return 1;
}

static int validate_narrow_update_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 state_binding = find_binding(unit, "state");
  ctool_u32 function_binding = find_binding(unit, "update_narrow");
  ctool_u32 state_type;
  ctool_u32 computation_type;
  ctool_u32 return_type;
  ctool_u32 index;
  if (state_binding == CTOOL_C_AST_NONE ||
      function_binding == CTOOL_C_AST_NONE ||
      unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 10u || ir->functions == NULL ||
      ir->instructions == NULL) {
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  function = &ir->functions[0];
  instructions = ir->instructions;
  state_type = unit->bindings[state_binding].type;
  return_type = function_type->referenced_type;
  computation_type = instructions[3].type;
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 0u ||
      state_type >= unit->layout.type_count ||
      return_type >= unit->layout.type_count ||
      computation_type >= unit->layout.type_count ||
      unit->layout.types[state_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[state_type].is_signed == CTOOL_TRUE ||
      unit->layout.types[state_type].size != 2u ||
      unit->layout.types[computation_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[computation_type].is_signed == CTOOL_FALSE ||
      unit->layout.types[computation_type].size != 4u ||
      unit->layout.types[return_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[return_type].is_signed == CTOOL_TRUE ||
      unit->layout.types[return_type].size != 4u ||
      definition->binding != function_binding ||
      function->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u || function->instruction_count != 10u ||
      function->maximum_stack_depth != 3u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != state_type ||
      instructions[0].reference != state_binding ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS ||
      instructions[1].type != state_type ||
      instructions[1].input_type != state_type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[2].type != state_type ||
      instructions[2].input_type != state_type ||
      instructions[2].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[3].input_type != state_type ||
      instructions[3].conversion != CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[4].type != computation_type ||
      instructions[4].integer_bits != 1u ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[5].type != computation_type ||
      instructions[5].input_type != computation_type ||
      instructions[5].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[6].type != state_type ||
      instructions[6].input_type != computation_type ||
      instructions[6].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[7].type != state_type ||
      instructions[7].input_type != state_type ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[8].type != return_type ||
      instructions[8].input_type != state_type ||
      instructions[8].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[9].type != return_type ||
      instructions[9].input_type != return_type) {
    (void)fprintf(stderr, "narrow update IR differs\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (!string_equal(instructions[index].location.path, "/narrow-update.c") ||
        !string_equal(instructions[index].physical_location.path,
                      "/narrow-update.c")) {
      return 0;
    }
  }
  return 1;
}

static int validate_narrow_compound_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 state_binding = find_binding(unit, "state");
  ctool_u32 function_binding = find_binding(unit, "add_narrow");
  ctool_u32 state_type;
  ctool_u32 promoted_type;
  ctool_u32 computation_type;
  ctool_u32 return_type;
  ctool_u32 index;
  if (state_binding == CTOOL_C_AST_NONE ||
      function_binding == CTOOL_C_AST_NONE ||
      unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 11u || ir->functions == NULL ||
      ir->instructions == NULL) {
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  function = &ir->functions[0];
  instructions = ir->instructions;
  state_type = unit->bindings[state_binding].type;
  return_type = function_type->referenced_type;
  promoted_type = instructions[3].type;
  computation_type = instructions[4].type;
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 0u ||
      state_type >= unit->layout.type_count ||
      promoted_type >= unit->layout.type_count ||
      computation_type >= unit->layout.type_count ||
      return_type >= unit->layout.type_count ||
      unit->layout.types[state_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[state_type].is_signed == CTOOL_TRUE ||
      unit->layout.types[state_type].size != 2u ||
      unit->layout.types[promoted_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[promoted_type].is_signed == CTOOL_FALSE ||
      unit->layout.types[promoted_type].size != 4u ||
      unit->layout.types[computation_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[computation_type].is_signed == CTOOL_TRUE ||
      unit->layout.types[computation_type].size != 4u ||
      return_type != computation_type || definition->binding != function_binding ||
      function->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u || function->instruction_count != 11u ||
      function->maximum_stack_depth != 3u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != state_type ||
      instructions[0].reference != state_binding ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS ||
      instructions[1].type != state_type ||
      instructions[1].input_type != state_type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[2].type != state_type ||
      instructions[2].input_type != state_type ||
      instructions[2].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[3].input_type != state_type ||
      instructions[3].conversion != CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[4].input_type != promoted_type ||
      instructions[4].conversion != CTOOL_C_CONVERSION_USUAL_ARITHMETIC ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[5].type != computation_type ||
      instructions[5].integer_bits != 1u ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[6].type != computation_type ||
      instructions[6].input_type != computation_type ||
      instructions[6].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[7].type != state_type ||
      instructions[7].input_type != computation_type ||
      instructions[7].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[8].type != state_type ||
      instructions[8].input_type != state_type ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[9].type != return_type ||
      instructions[9].input_type != state_type ||
      instructions[9].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[10].type != return_type ||
      instructions[10].input_type != return_type) {
    (void)fprintf(stderr, "narrow compound IR differs\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (!string_equal(instructions[index].location.path,
                      "/narrow-compound.c") ||
        !string_equal(instructions[index].physical_location.path,
                      "/narrow-compound.c")) {
      return 0;
    }
  }
  return 1;
}

static int validate_narrow_mutation_matrix_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  ctool_u32 volatile_binding = find_binding(unit, "volatile_byte");
  ctool_u32 active_binding = find_binding(unit, "x86_put_u8");
  ctool_u32 operation_counts[CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR + 1u];
  ctool_u32 narrow_addresses = 0u;
  ctool_u32 narrow_loads = 0u;
  ctool_u32 narrow_promotions = 0u;
  ctool_u32 narrow_assignments = 0u;
  ctool_u32 narrow_stores = 0u;
  ctool_u32 volatile_loads = 0u;
  ctool_u32 function_index;
  ctool_u32 index;
  if (volatile_binding == CTOOL_C_AST_NONE ||
      active_binding == CTOOL_C_AST_NONE ||
      unit->function_definition_count != 9u || ir->function_count != 9u ||
      ir->functions == NULL || ir->instructions == NULL ||
      unit->bindings[volatile_binding].type >= unit->graph.type_count ||
      (unit->graph.types[unit->bindings[volatile_binding].type].qualifiers &
       CTOOL_C_QUAL_VOLATILE) == 0u) {
    (void)fprintf(stderr, "narrow mutation matrix inventory differs\n");
    return 0;
  }
  (void)memset(operation_counts, 0, sizeof(operation_counts));
  for (function_index = 0u; function_index < ir->function_count;
       function_index++) {
    const ctool_c_ir_function_t *function = &ir->functions[function_index];
    if (function->binding >= unit->binding_count ||
        function->instruction_count == 0u ||
        function->first_instruction > ir->instruction_count ||
        function->instruction_count >
            ir->instruction_count - function->first_instruction) {
      return 0;
    }
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction = &ir->instructions[index];
    ctool_u32 type = instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD
                         ? instruction->input_type
                         : instruction->type;
    ctool_bool narrow =
        type < unit->layout.type_count &&
                unit->layout.types[type].is_integer == CTOOL_TRUE &&
                (unit->layout.types[type].size == 1u ||
                 unit->layout.types[type].size == 2u)
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if (!string_equal(instruction->location.path,
                      "/toolchain/narrow-mutations.c") ||
        !string_equal(instruction->physical_location.path,
                      "/toolchain/narrow-mutations.c")) {
      return 0;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS &&
        narrow == CTOOL_TRUE) {
      narrow_addresses++;
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD &&
               narrow == CTOOL_TRUE) {
      narrow_loads++;
      if (instruction->input_type == unit->bindings[volatile_binding].type) {
        volatile_loads++;
      }
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CONVERT &&
               instruction->conversion ==
                   CTOOL_C_CONVERSION_INTEGER_PROMOTION &&
               instruction->input_type < unit->layout.type_count &&
               (unit->layout.types[instruction->input_type].size == 1u ||
                unit->layout.types[instruction->input_type].size == 2u)) {
      narrow_promotions++;
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CONVERT &&
               instruction->conversion == CTOOL_C_CONVERSION_ASSIGNMENT &&
               narrow == CTOOL_TRUE) {
      narrow_assignments++;
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE_VALUE &&
               narrow == CTOOL_TRUE) {
      narrow_stores++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_BINARY &&
        instruction->operation <= CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR) {
      operation_counts[instruction->operation]++;
    }
  }
  if (narrow_addresses != 19u || narrow_loads != 25u ||
      narrow_promotions != 26u || narrow_assignments != 23u ||
      narrow_stores != 20u || volatile_loads != 1u ||
      operation_counts[CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY] != 1u ||
      operation_counts[CTOOL_C_EXPRESSION_OPERATOR_DIVIDE] != 2u ||
      operation_counts[CTOOL_C_EXPRESSION_OPERATOR_REMAINDER] != 1u ||
      operation_counts[CTOOL_C_EXPRESSION_OPERATOR_ADD] != 6u ||
      operation_counts[CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT] != 6u ||
      operation_counts[CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT] != 1u ||
      operation_counts[CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT] != 2u ||
      operation_counts[CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND] != 1u ||
      operation_counts[CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR] != 1u ||
      operation_counts[CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR] != 2u) {
    (void)fprintf(stderr,
                  "narrow mutation operations differ: address=%u load=%u "
                  "promotion=%u assignment=%u store=%u volatile=%u\n",
                  (unsigned int)narrow_addresses,
                  (unsigned int)narrow_loads,
                  (unsigned int)narrow_promotions,
                  (unsigned int)narrow_assignments,
                  (unsigned int)narrow_stores,
                  (unsigned int)volatile_loads);
    return 0;
  }
  for (function_index = 0u; function_index < ir->function_count;
       function_index++) {
    if (ir->functions[function_index].binding == active_binding) {
      return 1;
    }
  }
  return 0;
}

static int run_narrow_mutations(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t compound_unit;
  ctool_c_translation_unit_t matrix_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_ir_unit_t ir;
  ctool_c_ir_unit_t compound_ir;
  ctool_c_ir_unit_t matrix_ir;
  ctool_c_expression_t *invalid_expressions = NULL;
  char *matrix_fixture = NULL;
  ctool_u32 diagnostic_count;
  ctool_u32 update_expression = CTOOL_C_AST_NONE;
  ctool_u32 index;
  ctool_status_t status;
  uint64_t fingerprint;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&compound_unit, 0, sizeof(compound_unit));
  (void)memset(&matrix_unit, 0, sizeof(matrix_unit));
  (void)memset(&invalid_unit, 0, sizeof(invalid_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_is_unchanged(job) ||
      !parse_source(job, "/narrow-update.c", narrow_update_source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "narrow update lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_narrow_update_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < unit.expression_count; index++) {
    if (unit.expressions[index].kind == CTOOL_C_EXPRESSION_UPDATE) {
      update_expression = index;
      break;
    }
  }
  if (update_expression == CTOOL_C_AST_NONE || unit.expression_count == 0u ||
      sizeof(*invalid_expressions) >
          SIZE_MAX / (size_t)unit.expression_count) {
    (void)fprintf(stderr, "narrow update fixture differs\n");
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)unit.expression_count * sizeof(*invalid_expressions));
  if (invalid_expressions == NULL) {
    (void)fprintf(stderr, "narrow update fixture allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_expressions[update_expression].computation_type =
      invalid_expressions[update_expression].type;
  invalid_unit = unit;
  invalid_unit.expressions = invalid_expressions;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT, CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "narrow update computation type")) {
    goto cleanup;
  }
  if (!parse_source(job, "/narrow-compound.c", narrow_compound_source,
                    &compound_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&compound_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&compound_ir, 0xa5, sizeof(compound_ir));
  status = ctool_c_lower_ir(job, &compound_unit, &compound_ir);
  if (!check_status(status, CTOOL_OK, "narrow compound lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&compound_unit) != fingerprint ||
      !validate_narrow_compound_ir(&compound_unit, &compound_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  matrix_fixture = make_narrow_mutation_fixture();
  if (matrix_fixture == NULL ||
      !parse_source(job, "/toolchain/narrow-mutations.c", matrix_fixture,
                    &matrix_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&matrix_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&matrix_ir, 0xa5, sizeof(matrix_ir));
  status = ctool_c_lower_ir(job, &matrix_unit, &matrix_ir);
  if (!check_status(status, CTOOL_OK, "narrow mutation matrix lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&matrix_unit) != fingerprint ||
      !validate_narrow_mutation_matrix_ir(&matrix_unit, &matrix_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_expressions);
  free(matrix_fixture);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("narrow-mutations: ok");
    return 0;
  }
  return 1;
}

static int validate_bit_field_mutation_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir,
    const char *path) {
  static const ctool_c_ir_instruction_kind_t expected_kinds[] = {
      CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_BIT_FIELD_LOAD,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_INTEGER,
      CTOOL_C_IR_INSTRUCTION_BINARY,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_VALUE,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE};
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 member;
  ctool_u32 state;
  ctool_u32 index;
  if (unit == NULL || ir == NULL || path == NULL ||
      unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->instruction_count != 9u) {
    (void)fprintf(stderr, "bit-field mutation IR inventory differs\n");
    return 0;
  }
  member = find_member(unit, "value");
  state = find_binding(unit, "state");
  function = &ir->functions[0];
  instructions = &ir->instructions[function->first_instruction];
  if (member == CTOOL_C_AST_NONE || state == CTOOL_C_AST_NONE ||
      member >= unit->graph.member_count || member >= unit->layout.member_count ||
      unit->graph.members[member].is_bit_field != CTOOL_TRUE ||
      unit->graph.members[member].bit_width != 3u ||
      unit->layout.members[member].bit_width != 3u ||
      unit->layout.members[member].size != 4u ||
      function->binding != unit->function_definitions[0].binding ||
      function->declared_type != unit->function_definitions[0].declared_type ||
      function->first_instruction != 0u || function->instruction_count != 9u ||
      function->maximum_stack_depth != 3u) {
    (void)fprintf(stderr, "bit-field mutation metadata differs\n");
    return 0;
  }
  for (index = 0u; index < 9u; index++) {
    if (instructions[index].kind != expected_kinds[index]) {
      (void)fprintf(stderr, "bit-field mutation instruction %u differs\n",
                    (unsigned)index);
      return 0;
    }
  }
  if (instructions[0].reference != state ||
      instructions[1].type != instructions[0].type ||
      instructions[1].input_type != instructions[0].type ||
      instructions[2].reference != member ||
      instructions[2].input_type != instructions[0].type ||
      instructions[2].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[3].input_type != instructions[2].type ||
      instructions[3].conversion != CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
      instructions[3].reference != member ||
      instructions[4].type != instructions[3].type ||
      instructions[4].integer_bits != 1u ||
      instructions[5].type != instructions[3].type ||
      instructions[5].input_type != instructions[3].type ||
      instructions[5].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[6].type != instructions[2].type ||
      instructions[6].input_type != instructions[3].type ||
      instructions[6].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[7].type != instructions[2].type ||
      instructions[7].input_type != instructions[0].type ||
      instructions[7].reference != member ||
      instructions[8].type != instructions[7].type ||
      instructions[8].input_type != instructions[7].type ||
      !string_equal(instructions[7].location.path, path) ||
      !string_equal(instructions[7].physical_location.path, path)) {
    (void)fprintf(stderr, "bit-field mutation instruction metadata differs\n");
    return 0;
  }
  return 1;
}

static int validate_bit_field_postfix_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  static const ctool_c_ir_instruction_kind_t expected_kinds[] = {
      CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_BIT_FIELD_LOAD,
      CTOOL_C_IR_INSTRUCTION_DUPLICATE_VALUE,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_INTEGER,
      CTOOL_C_IR_INSTRUCTION_BINARY,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_OLD_VALUE,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE};
  ctool_u32 member;
  ctool_u32 state;
  ctool_u32 function_index;
  if (unit == NULL || ir == NULL || unit->function_definition_count != 2u ||
      ir->function_count != 2u || ir->functions == NULL ||
      ir->instructions == NULL || ir->instruction_count != 20u) {
    (void)fprintf(stderr, "bit-field postfix IR inventory differs\n");
    return 0;
  }
  member = find_member(unit, "value");
  state = find_binding(unit, "state");
  if (member == CTOOL_C_AST_NONE || state == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "bit-field postfix fixture differs\n");
    return 0;
  }
  for (function_index = 0u; function_index < 2u; function_index++) {
    const ctool_c_ir_function_t *function = &ir->functions[function_index];
    const ctool_c_ir_instruction_t *instructions =
        &ir->instructions[function->first_instruction];
    ctool_u32 index;
    if (function->binding !=
            unit->function_definitions[function_index].binding ||
        function->declared_type !=
            unit->function_definitions[function_index].declared_type ||
        function->first_instruction != function_index * 10u ||
        function->instruction_count != 10u ||
        function->maximum_stack_depth != 4u) {
      (void)fprintf(stderr, "bit-field postfix function differs\n");
      return 0;
    }
    for (index = 0u; index < 10u; index++) {
      if (instructions[index].kind != expected_kinds[index]) {
        (void)fprintf(stderr,
                      "bit-field postfix instruction %u differs\n",
                      (unsigned)index);
        return 0;
      }
    }
    if (instructions[0].reference != state ||
        instructions[2].reference != member ||
        instructions[3].type != instructions[2].type ||
        instructions[3].input_type != instructions[2].type ||
        instructions[4].conversion != CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
        instructions[4].reference != member ||
        instructions[5].integer_bits != 1u ||
        instructions[6].operation !=
            (function_index == 0u ? CTOOL_C_EXPRESSION_OPERATOR_ADD
                                  : CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT) ||
        instructions[7].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
        instructions[8].reference != member ||
        instructions[8].type != instructions[2].type ||
        instructions[8].input_type != instructions[0].type ||
        instructions[9].type != instructions[8].type ||
        !string_equal(instructions[8].location.path,
                      "/bit-field-postfix.c") ||
        !string_equal(instructions[8].physical_location.path,
                      "/bit-field-postfix.c")) {
      (void)fprintf(stderr, "bit-field postfix metadata differs\n");
      return 0;
    }
  }
  return 1;
}

static int validate_bit_field_compound_matrix_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  static const ctool_c_expression_operator_t operations[] = {
      CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY,
      CTOOL_C_EXPRESSION_OPERATOR_DIVIDE,
      CTOOL_C_EXPRESSION_OPERATOR_REMAINDER,
      CTOOL_C_EXPRESSION_OPERATOR_ADD,
      CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
      CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT,
      CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT,
      CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND,
      CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR,
      CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR};
  static const ctool_c_ir_instruction_kind_t expected_kinds[] = {
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_DEREFERENCE,
      CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_BIT_FIELD_LOAD,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_BINARY,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_VALUE,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE};
  ctool_u32 member;
  ctool_u32 function_index;
  if (unit == NULL || ir == NULL || unit->function_definition_count != 10u ||
      ir->function_count != 10u || ir->functions == NULL ||
      ir->instructions == NULL || ir->instruction_count != 120u) {
    (void)fprintf(stderr, "bit-field compound matrix inventory differs\n");
    return 0;
  }
  member = find_member(unit, "value");
  if (member == CTOOL_C_AST_NONE || member >= unit->graph.member_count ||
      member >= unit->layout.member_count ||
      unit->graph.members[member].is_bit_field != CTOOL_TRUE ||
      unit->graph.members[member].bit_width != 3u ||
      unit->layout.members[member].bit_width != 3u ||
      unit->layout.members[member].size != 4u) {
    (void)fprintf(stderr, "bit-field compound matrix member differs\n");
    return 0;
  }
  for (function_index = 0u; function_index < 10u; function_index++) {
    const ctool_c_ir_function_t *function = &ir->functions[function_index];
    const ctool_c_ir_instruction_t *instructions =
        &ir->instructions[function->first_instruction];
    ctool_u32 instruction_index;
    if (function->binding !=
            unit->function_definitions[function_index].binding ||
        function->declared_type !=
            unit->function_definitions[function_index].declared_type ||
        function->first_instruction != function_index * 12u ||
        function->instruction_count != 12u ||
        function->maximum_stack_depth != 3u) {
      (void)fprintf(stderr, "bit-field compound function %u differs\n",
                    (unsigned)function_index);
      return 0;
    }
    for (instruction_index = 0u; instruction_index < 12u;
         instruction_index++) {
      if (instructions[instruction_index].kind !=
          expected_kinds[instruction_index]) {
        (void)fprintf(stderr,
                      "bit-field compound instruction %u:%u differs\n",
                      (unsigned)function_index,
                      (unsigned)instruction_index);
        return 0;
      }
    }
    if (instructions[0].reference != function_index * 2u ||
        instructions[4].reference != member ||
        instructions[4].conversion !=
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
        instructions[5].reference != member ||
        instructions[5].conversion !=
            CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
        instructions[6].reference != function_index * 2u + 1u ||
        instructions[8].operation != operations[function_index] ||
        instructions[9].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
        instructions[10].reference != member ||
        instructions[10].type != instructions[4].type ||
        instructions[10].input_type != instructions[2].type ||
        instructions[11].type != instructions[10].type ||
        instructions[11].input_type != instructions[10].type ||
        !string_equal(instructions[10].location.path,
                      "/bit-field-compound-matrix.c") ||
        !string_equal(instructions[10].physical_location.path,
                      "/bit-field-compound-matrix.c")) {
      (void)fprintf(stderr, "bit-field compound metadata %u differs\n",
                    (unsigned)function_index);
      return 0;
    }
  }
  return 1;
}

static int validate_volatile_whole_bit_field_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  ctool_u32 member;
  ctool_u32 function_index;
  if (unit == NULL || ir == NULL || unit->function_definition_count != 2u ||
      ir->function_count != 2u || ir->functions == NULL ||
      ir->instructions == NULL || ir->instruction_count != 19u) {
    (void)fprintf(stderr, "volatile whole bit-field inventory differs\n");
    return 0;
  }
  member = find_member(unit, "value");
  if (member == CTOOL_C_AST_NONE || member >= unit->graph.member_count ||
      member >= unit->layout.member_count ||
      unit->graph.members[member].is_bit_field != CTOOL_TRUE ||
      unit->graph.members[member].bit_width != 32u ||
      unit->layout.members[member].bit_width != 32u ||
      unit->layout.members[member].size != 4u ||
      unit->graph.members[member].type >= unit->graph.type_count ||
      (unit->graph.types[unit->graph.members[member].type].qualifiers &
       CTOOL_C_QUAL_VOLATILE) == 0u) {
    (void)fprintf(stderr, "volatile whole bit-field member differs\n");
    return 0;
  }
  for (function_index = 0u; function_index < 2u; function_index++) {
    const ctool_c_ir_function_t *function = &ir->functions[function_index];
    const ctool_c_ir_instruction_t *instructions =
        &ir->instructions[function->first_instruction];
    ctool_u32 store_index = function_index == 0u ? 7u : 8u;
    if (function->first_instruction != (function_index == 0u ? 0u : 9u) ||
        function->instruction_count != (function_index == 0u ? 9u : 10u) ||
        function->maximum_stack_depth !=
            (function_index == 0u ? 3u : 4u) ||
        instructions[0].kind !=
            CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
        instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
        instructions[2].kind != CTOOL_C_IR_INSTRUCTION_DEREFERENCE ||
        instructions[3].kind != CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS ||
        instructions[4].kind != CTOOL_C_IR_INSTRUCTION_BIT_FIELD_LOAD ||
        instructions[4].reference != member ||
        (function_index == 1u &&
         instructions[5].kind != CTOOL_C_IR_INSTRUCTION_DUPLICATE_VALUE) ||
        instructions[store_index].kind !=
            (function_index == 0u
                 ? CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_VALUE
                 : CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_OLD_VALUE) ||
        instructions[store_index].reference != member ||
        instructions[store_index + 1u].kind !=
            CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
      (void)fprintf(stderr, "volatile whole bit-field function %u differs\n",
                    (unsigned)function_index);
      return 0;
    }
  }
  return 1;
}

static int run_bit_field_mutations(const char *host_root) {
  static const char narrow_source[] =
      "struct flags { unsigned char value : 3; };\n"
      "unsigned int update(struct flags *state) {\n"
      "  return ++state->value;\n"
      "}\n";
  static const char bool_source[] =
      "struct flags { _Bool value : 1; };\n"
      "_Bool update(struct flags *state) { return state->value += 1; }\n";
  static const char atomic_source[] =
      "struct flags { _Atomic unsigned int value : 3; };\n"
      "unsigned int update(struct flags *state) {\n"
      "  return state->value++;\n"
      "}\n";
  static const char packed_source[] =
      "struct flags { unsigned int value : 3; } "
      "__attribute__((packed));\n"
      "unsigned int update(struct flags *state) {\n"
      "  return --state->value;\n"
      "}\n";
  static const char volatile_source[] =
      "struct flags { volatile unsigned int value : 3; };\n"
      "unsigned int update(struct flags *state) {\n"
      "  return state->value++;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t update_unit;
  ctool_c_translation_unit_t compound_unit;
  ctool_c_translation_unit_t matrix_unit;
  ctool_c_translation_unit_t postfix_unit;
  ctool_c_translation_unit_t volatile_whole_unit;
  ctool_c_translation_unit_t narrow_unit;
  ctool_c_translation_unit_t bool_unit;
  ctool_c_translation_unit_t atomic_unit;
  ctool_c_translation_unit_t packed_unit;
  ctool_c_translation_unit_t volatile_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_ir_unit_t update_ir;
  ctool_c_ir_unit_t compound_ir;
  ctool_c_ir_unit_t matrix_ir;
  ctool_c_ir_unit_t postfix_ir;
  ctool_c_ir_unit_t volatile_whole_ir;
  ctool_c_ir_unit_t recovery_ir;
  ctool_c_member_layout_t *invalid_layouts = NULL;
  ctool_c_record_member_t *invalid_members = NULL;
  ctool_u64 fingerprint;
  ctool_u64 ir_fingerprint;
  ctool_u32 diagnostic_count;
  ctool_u32 member;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&update_unit, 0, sizeof(update_unit));
  (void)memset(&compound_unit, 0, sizeof(compound_unit));
  (void)memset(&matrix_unit, 0, sizeof(matrix_unit));
  (void)memset(&postfix_unit, 0, sizeof(postfix_unit));
  (void)memset(&volatile_whole_unit, 0, sizeof(volatile_whole_unit));
  (void)memset(&narrow_unit, 0, sizeof(narrow_unit));
  (void)memset(&bool_unit, 0, sizeof(bool_unit));
  (void)memset(&atomic_unit, 0, sizeof(atomic_unit));
  (void)memset(&packed_unit, 0, sizeof(packed_unit));
  (void)memset(&volatile_unit, 0, sizeof(volatile_unit));
  (void)memset(&invalid_unit, 0, sizeof(invalid_unit));
  (void)memset(&update_ir, 0xa5, sizeof(update_ir));
  (void)memset(&compound_ir, 0xa5, sizeof(compound_ir));
  (void)memset(&matrix_ir, 0xa5, sizeof(matrix_ir));
  (void)memset(&postfix_ir, 0xa5, sizeof(postfix_ir));
  (void)memset(&volatile_whole_ir, 0xa5, sizeof(volatile_whole_ir));
  (void)memset(&recovery_ir, 0xa5, sizeof(recovery_ir));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/bit-field-update.c", bit_field_update_source,
                    &update_unit) ||
      !parse_source(job, "/bit-field-compound.c", bit_field_compound_source,
                    &compound_unit) ||
      !parse_source(job, "/bit-field-compound-matrix.c",
                    bit_field_compound_matrix_source, &matrix_unit) ||
      !parse_source(job, "/bit-field-postfix.c", bit_field_postfix_source,
                    &postfix_unit) ||
      !parse_source(job, "/volatile-whole-bit-field.c",
                    bit_field_volatile_whole_source,
                    &volatile_whole_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&update_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &update_unit, &update_ir);
  if (!check_status(status, CTOOL_OK, "bit-field update lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&update_unit) != fingerprint ||
      !validate_bit_field_mutation_ir(&update_unit, &update_ir,
                                      "/bit-field-update.c")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  ir_fingerprint = ir_instruction_fingerprint(&update_ir);
  status = ctool_c_lower_ir(job, &compound_unit, &compound_ir);
  if (!check_status(status, CTOOL_OK, "bit-field compound lowering") ||
      !validate_bit_field_mutation_ir(&compound_unit, &compound_ir,
                                      "/bit-field-compound.c")) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  status = ctool_c_lower_ir(job, &matrix_unit, &matrix_ir);
  if (!check_status(status, CTOOL_OK,
                    "bit-field compound matrix lowering") ||
      !validate_bit_field_compound_matrix_ir(&matrix_unit, &matrix_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  status = ctool_c_lower_ir(job, &postfix_unit, &postfix_ir);
  if (!check_status(status, CTOOL_OK, "bit-field postfix lowering") ||
      !validate_bit_field_postfix_ir(&postfix_unit, &postfix_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  status = ctool_c_lower_ir(job, &volatile_whole_unit, &volatile_whole_ir);
  if (!check_status(status, CTOOL_OK,
                    "volatile whole bit-field lowering") ||
      !validate_volatile_whole_bit_field_ir(&volatile_whole_unit,
                                            &volatile_whole_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/narrow-bit-field-mutation.c", narrow_source,
                    &narrow_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &narrow_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "narrow bit-field mutation") ||
      !parse_source(job, "/bool-bit-field-mutation.c", bool_source,
                    &bool_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &bool_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "Boolean bit-field mutation") ||
      !parse_source(job, "/atomic-bit-field-mutation.c", atomic_source,
                    &atomic_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &atomic_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic bit-field mutation") ||
      !parse_source_mode(job, "/packed-bit-field-mutation.c", packed_source,
                         CTOOL_TRUE, &packed_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &packed_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "packed bit-field mutation") ||
      !parse_source(job, "/volatile-bit-field-mutation.c", volatile_source,
                    &volatile_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &volatile_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "volatile bit-field mutation")) {
    goto cleanup;
  }
  member = find_member(&update_unit, "value");
  invalid_layouts = (ctool_c_member_layout_t *)malloc(
      (size_t)update_unit.layout.member_count * sizeof(*invalid_layouts));
  invalid_members = (ctool_c_record_member_t *)malloc(
      (size_t)update_unit.graph.member_count * sizeof(*invalid_members));
  if (member == CTOOL_C_AST_NONE || invalid_layouts == NULL ||
      invalid_members == NULL) {
    goto cleanup;
  }
  invalid_unit = update_unit;
  invalid_unit.layout.members = invalid_layouts;
  (void)memcpy(invalid_layouts, update_unit.layout.members,
               (size_t)update_unit.layout.member_count *
                   sizeof(*invalid_layouts));
  invalid_layouts[member].bit_width++;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "bit-field mutation layout width")) {
    goto cleanup;
  }
  invalid_unit = update_unit;
  invalid_unit.graph.members = invalid_members;
  (void)memcpy(invalid_members, update_unit.graph.members,
               (size_t)update_unit.graph.member_count *
                   sizeof(*invalid_members));
  invalid_members[member].bit_width++;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "bit-field mutation graph width")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &update_unit, &recovery_ir);
  if (!check_status(status, CTOOL_OK, "bit-field mutation recovery") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&update_unit) != fingerprint ||
      ir_instruction_fingerprint(&recovery_ir) != ir_fingerprint) {
    (void)fprintf(stderr, "bit-field mutation lowering did not recover\n");
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_members);
  free(invalid_layouts);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("bit-field-mutations: ok");
    return 0;
  }
  return 1;
}

static int run_integer_mutation_rejections(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t atomic_unit;
  ctool_c_translation_unit_t wide_unit;
  ctool_c_translation_unit_t bool_unit;
  ctool_c_translation_unit_t bool_compound_unit;
  ctool_c_translation_unit_t valid_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_u32 update_expression = CTOOL_C_AST_NONE;
  ctool_u32 index;
  int passed = 0;
  (void)memset(&atomic_unit, 0, sizeof(atomic_unit));
  (void)memset(&wide_unit, 0, sizeof(wide_unit));
  (void)memset(&bool_unit, 0, sizeof(bool_unit));
  (void)memset(&bool_compound_unit, 0, sizeof(bool_compound_unit));
  (void)memset(&valid_unit, 0, sizeof(valid_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/atomic-update.c", atomic_update_source,
                    &atomic_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &atomic_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic integer update") ||
      !parse_source(job, "/wide-compound.c", wide_compound_source,
                    &wide_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &wide_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide integer compound assignment") ||
      !parse_source(job, "/bool-update.c", bool_update_source, &bool_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &bool_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "Boolean postfix update") ||
      !parse_source(job, "/bool-compound.c", bool_compound_source,
                    &bool_compound_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &bool_compound_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "Boolean compound assignment") ||
      !parse_source(job, "/invalid-update.c", invalid_update_source,
                    &valid_unit)) {
    goto cleanup;
  }
  for (index = 0u; index < valid_unit.expression_count; index++) {
    if (valid_unit.expressions[index].kind == CTOOL_C_EXPRESSION_UPDATE) {
      update_expression = index;
      break;
    }
  }
  if (update_expression == CTOOL_C_AST_NONE ||
      valid_unit.expression_count == 0u ||
      sizeof(*invalid_expressions) >
          SIZE_MAX / (size_t)valid_unit.expression_count) {
    (void)fprintf(stderr, "invalid update fixture differs\n");
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)valid_unit.expression_count * sizeof(*invalid_expressions));
  if (invalid_expressions == NULL) {
    (void)fprintf(stderr, "invalid update fixture allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, valid_unit.expressions,
               (size_t)valid_unit.expression_count *
                   sizeof(*invalid_expressions));
  invalid_expressions[update_expression].operation =
      CTOOL_C_EXPRESSION_OPERATOR_NONE;
  invalid_unit = valid_unit;
  invalid_unit.expressions = invalid_expressions;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "missing update operation")) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, valid_unit.expressions,
               (size_t)valid_unit.expression_count *
                   sizeof(*invalid_expressions));
  invalid_expressions[update_expression].computation_type =
      valid_unit.graph.type_count;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "update computation type range")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_expressions);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("integer-mutation-rejections: ok");
    return 0;
  }
  return 1;
}

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  ctool_u32 type;
  ctool_u32 input_type;
  ctool_c_expression_operator_t operation;
  ctool_c_conversion_kind_t conversion;
  ctool_u32 reference;
  ctool_u64 integer_bits;
  ctool_u32 line;
  ctool_u32 column;
} pointer_ir_expected_t;

static int pointer_ir_instruction_matches(
    const ctool_c_ir_instruction_t *actual,
    const pointer_ir_expected_t *expected, const char *path) {
  return actual->kind == expected->kind &&
                 actual->type == expected->type &&
                 actual->input_type == expected->input_type &&
                 actual->operation == expected->operation &&
                 actual->conversion == expected->conversion &&
                 actual->reference == expected->reference &&
                 actual->integer_bits == expected->integer_bits &&
                 string_equal(actual->location.path, path) != 0 &&
                 string_equal(actual->physical_location.path, path) != 0 &&
                 actual->location.line == expected->line &&
                 actual->location.column == expected->column &&
                 actual->physical_location.line == expected->line &&
                 actual->physical_location.column == expected->column
             ? 1
             : 0;
}

static int validate_pointer_member_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  static const pointer_ir_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 8u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       13u, 10u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 8u, 8u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 13u, 10u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 7u, 8u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 13u, 14u},
      {CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS, 12u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 2u, 0u,
       13u, 16u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 0u, 12u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 13u, 16u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 10u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 1u, 0u,
       13u, 26u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 10u, 10u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 13u, 26u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 9u, 10u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 13u, 31u},
      {CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS, 13u, 9u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 2u, 0u,
       13u, 33u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 0u, 13u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 13u, 33u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, 1u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_LESS, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 13u, 24u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 14u, 0u,
       13u, 41u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 1u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u, 13u, 41u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 45u, 0u,
       13u, 41u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 8u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       14u, 19u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 8u, 8u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 14u, 19u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 7u, 8u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 14u, 23u},
      {CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS, 14u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 2u, 0u,
       14u, 25u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 0u, 14u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 14u, 25u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 10u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 1u, 0u,
       14u, 36u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 10u, 10u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 14u, 36u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 9u, 10u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 14u, 41u},
      {CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS, 15u, 9u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 2u, 0u,
       14u, 43u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 0u, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 14u, 43u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, 1u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_EQUAL, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 14u, 33u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 40u, 0u,
       14u, 51u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 8u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       14u, 54u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 8u, 8u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 14u, 54u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 7u, 8u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 14u, 58u},
      {CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS, 16u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 3u, 0u,
       14u, 60u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 0u, 16u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 14u, 60u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 10u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 1u, 0u,
       14u, 68u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 10u, 10u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 14u, 68u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 9u, 10u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 14u, 73u},
      {CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS, 17u, 9u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 3u, 0u,
       14u, 75u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 0u, 17u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 14u, 75u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, 1u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_LESS, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 14u, 66u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 40u, 0u,
       14u, 51u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 1u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u, 14u, 51u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 41u, 0u,
       14u, 51u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 1u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 14u, 51u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 44u, 0u,
       13u, 41u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 1u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u, 13u, 41u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 45u, 0u,
       13u, 41u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 1u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 13u, 41u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 48u, 0u,
       15u, 14u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 1u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u, 15u, 16u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 49u, 0u,
       15u, 14u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 1u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 16u, 16u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 1u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 13u, 3u}};
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->functions[0].binding != unit->function_definitions[0].binding ||
      ir->functions[0].declared_type !=
          unit->function_definitions[0].declared_type ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != ir->instruction_count ||
      ir->functions[0].maximum_stack_depth != 2u ||
      find_member(unit, "address") != 2u ||
      find_member(unit, "order") != 3u) {
    (void)fprintf(stderr, "object-pointer member IR shape differs\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (!pointer_ir_instruction_matches(
            &ir->instructions[index], &expected[index],
            "/active-obj-region-less.c")) {
      (void)fprintf(stderr,
                    "object-pointer member instruction %u differs\n",
                    (unsigned)index);
      return 0;
    }
  }
  return 1;
}

static int validate_pointer_value_ir(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  static const pointer_ir_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 7u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       7u, 43u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 7u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 7u, 43u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 1u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 7u, 42u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 1u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 7u, 42u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 1u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 7u, 35u},
      {CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 4u, 0u,
       8u, 37u},
      {CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS, 1u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       8u, 50u},
      {CTOOL_C_IR_INSTRUCTION_ADDRESS_OF, 11u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_ADDRESS, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 8u, 36u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 9u, 11u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 8u, 29u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 13u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 1u, 0u,
       9u, 49u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 13u, 13u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 9u, 49u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 1u, 13u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 9u, 48u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 1u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 2u, 0u,
       9u, 59u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 1u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 9u, 59u},
      {CTOOL_C_IR_INSTRUCTION_STORE_VALUE, 1u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 9u, 57u},
      {CTOOL_C_IR_INSTRUCTION_DISCARD, CTOOL_C_TYPE_NONE, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 9u, 48u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VOID, CTOOL_C_TYPE_NONE,
       CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, 9u, 46u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 15u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 3u, 0u,
       10u, 56u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 15u, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 10u, 56u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 16u, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 10u, 49u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 19u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 4u, 0u,
       11u, 51u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 19u, 19u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 11u, 51u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, 20u, 19u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_QUALIFICATION,
       CTOOL_C_AST_NONE, 0u, 11u, 51u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 20u, 20u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 11u, 44u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 22u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 5u, 0u,
       12u, 44u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 22u, 22u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 12u, 44u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, 23u, 22u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_POINTER,
       CTOOL_C_AST_NONE, 0u, 12u, 44u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 23u, 23u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 12u, 37u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 25u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 6u, 0u,
       13u, 46u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 25u, 25u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 13u, 46u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, 26u, 25u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_POINTER,
       CTOOL_C_AST_NONE, 0u, 13u, 46u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 26u, 26u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 13u, 39u},
      {CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS, 32u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       15u, 33u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 28u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 7u, 0u,
       15u, 40u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 34u, 28u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 15u, 40u},
      {CTOOL_C_IR_INSTRUCTION_STORE, 32u, 34u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 15u, 40u},
      {CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS, 32u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       16u, 10u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 35u, 32u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 16u, 10u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 30u, 35u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 16u, 3u},
      {CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS, 6u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 5u, 0u,
       18u, 45u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 36u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 8u, 0u,
       18u, 62u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 36u, 36u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 18u, 62u},
      {CTOOL_C_IR_INSTRUCTION_STORE_VALUE, 6u, 36u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 18u, 60u},
      {CTOOL_C_IR_INSTRUCTION_DISCARD, CTOOL_C_TYPE_NONE, 6u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 18u, 45u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VOID, CTOOL_C_TYPE_NONE,
       CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, 18u, 43u},
      {CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS, 6u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 5u, 0u,
       19u, 35u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 1u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 19u, 52u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, 6u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NULL_POINTER,
       CTOOL_C_AST_NONE, 0u, 19u, 52u},
      {CTOOL_C_IR_INSTRUCTION_STORE_VALUE, 6u, 6u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 19u, 50u},
      {CTOOL_C_IR_INSTRUCTION_DISCARD, CTOOL_C_TYPE_NONE, 6u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 19u, 35u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VOID, CTOOL_C_TYPE_NONE,
       CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, 19u, 33u},
      {CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS, 6u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 5u, 0u,
       20u, 39u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 6u, 6u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 20u, 39u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 0u, 6u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 20u, 53u},
      {CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS, 1u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       20u, 55u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 1u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 20u, 55u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 1u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 20u, 32u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 40u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 9u, 0u,
       21u, 76u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 40u, 40u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 21u, 76u},
      {CTOOL_C_IR_INSTRUCTION_CALL_DIRECT, 16u, 17u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 9u, 0u,
       21u, 75u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 41u, 16u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 21u, 56u}};
  static const ctool_u32 function_counts[] = {
      5u, 4u, 8u, 3u, 4u, 4u, 4u, 7u, 6u, 6u, 6u, 4u};
  static const ctool_u32 stack_depths[] = {
      1u, 1u, 2u, 1u, 1u, 1u, 1u, 2u, 2u, 2u, 1u, 1u};
  ctool_c_translation_unit_t malformed_unit;
  ctool_u32 index;
  ctool_u32 first = 0u;
  malformed_unit = *unit;
  malformed_unit.layout.types = NULL;
  if (unit->function_definition_count != 12u || ir->function_count != 12u ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      ir->functions == NULL || ir->instructions == NULL ||
      unit->graph.type_count <= 43u ||
      unit->graph.types[15].kind != CTOOL_C_TYPE_POINTER ||
      unit->graph.types[16].kind != CTOOL_C_TYPE_POINTER ||
      unit->graph.types[15].referenced_type != 5u ||
      unit->graph.types[16].referenced_type != 3u ||
      unit->graph.types[5].kind != CTOOL_C_TYPE_ARRAY ||
      unit->graph.types[5].element_count != 2u ||
      unit->graph.types[5].referenced_type != 4u ||
      unit->graph.types[4].kind != CTOOL_C_TYPE_QUALIFIED ||
      unit->graph.types[4].qualifiers != CTOOL_C_QUAL_CONST ||
      unit->graph.types[4].referenced_type != 1u ||
      unit->graph.types[3].kind != CTOOL_C_TYPE_QUALIFIED ||
      unit->graph.types[3].qualifiers != CTOOL_C_QUAL_CONST ||
      unit->graph.types[3].referenced_type != 2u ||
      unit->graph.types[2].kind != CTOOL_C_TYPE_ARRAY ||
      unit->graph.types[2].element_count != 2u ||
      unit->graph.types[2].referenced_type != 1u ||
      unit->graph.types[28].kind != CTOOL_C_TYPE_POINTER ||
      unit->graph.types[28].referenced_type != 5u ||
      unit->graph.types[28].qualifiers !=
          (CTOOL_C_QUAL_CONST | CTOOL_C_QUAL_VOLATILE) ||
      unit->graph.types[32].kind != CTOOL_C_TYPE_POINTER ||
      unit->graph.types[32].referenced_type != 3u ||
      unit->graph.types[32].qualifiers !=
          (CTOOL_C_QUAL_CONST | CTOOL_C_QUAL_VOLATILE) ||
      unit->graph.types[34].kind != CTOOL_C_TYPE_POINTER ||
      unit->graph.types[34].referenced_type != 5u ||
      unit->graph.types[34].qualifiers != 0u ||
      unit->graph.types[35].kind != CTOOL_C_TYPE_POINTER ||
      unit->graph.types[35].referenced_type != 3u ||
      unit->graph.types[35].qualifiers != 0u ||
      !pointer_value_query_matches(job, unit, 15u, 16u, CTOOL_TRUE) ||
      !pointer_value_query_matches(job, unit, 16u, 15u, CTOOL_TRUE) ||
      !pointer_value_query_matches(job, unit, 7u, 15u, CTOOL_FALSE) ||
      !pointer_value_query_matches(job, unit, 15u, 7u, CTOOL_FALSE) ||
      !pointer_value_query_matches(job, unit, 28u, 34u, CTOOL_TRUE) ||
      !pointer_value_query_matches(job, unit, 34u, 28u, CTOOL_TRUE) ||
      !pointer_value_query_matches(job, unit, 32u, 35u, CTOOL_TRUE) ||
      !pointer_value_query_matches(job, unit, 32u, 34u, CTOOL_TRUE) ||
      !pointer_value_query_matches(job, unit, 19u, 20u, CTOOL_FALSE) ||
      !pointer_value_query_matches(
          job, &malformed_unit, 15u, 16u, CTOOL_FALSE) ||
      !pointer_value_query_matches(job, NULL, 15u, 16u, CTOOL_FALSE)) {
    (void)fprintf(stderr, "pointer value IR shape differs\n");
    return 0;
  }
  for (index = 0u; index < ir->function_count; index++) {
    if (ir->functions[index].binding !=
            unit->function_definitions[index].binding ||
        ir->functions[index].declared_type !=
            unit->function_definitions[index].declared_type ||
        ir->functions[index].first_instruction != first ||
        ir->functions[index].instruction_count != function_counts[index] ||
        ir->functions[index].maximum_stack_depth != stack_depths[index]) {
      (void)fprintf(stderr, "pointer function %u IR slice differs\n",
                    (unsigned)index);
      return 0;
    }
    first += function_counts[index];
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (!pointer_ir_instruction_matches(
            &ir->instructions[index], &expected[index],
            "/pointer-values.c")) {
      (void)fprintf(stderr, "pointer instruction %u differs\n",
                    (unsigned)index);
      return 0;
    }
  }
  return 1;
}

static int run_pointer_member_loads(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_ir_unit_t ir;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  uint64_t fingerprint;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_is_unchanged(job) ||
      !parse_source(job, "/active-obj-region-less.c", pointer_member_source,
                    &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "object-pointer member lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint || ir.function_count != 1u ||
      ir.functions == NULL || ir.instructions == NULL ||
      !validate_pointer_member_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("pointer-member-loads: ok");
    return 0;
  }
  return 1;
}

static int run_pointer_values(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t atomic_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_c_ir_unit_t ir;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  ctool_u32 dereference_expression = CTOOL_C_AST_NONE;
  ctool_u32 address_expression = CTOOL_C_AST_NONE;
  ctool_u32 null_conversion_expression = CTOOL_C_AST_NONE;
  ctool_u32 qualification_conversion_expression = CTOOL_C_AST_NONE;
  ctool_u32 incompatible_pointer_type = CTOOL_C_TYPE_NONE;
  ctool_u32 index;
  uint64_t fingerprint;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&atomic_unit, 0, sizeof(atomic_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/pointer-values.c", pointer_value_source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "object-pointer value lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_pointer_value_ir(job, &unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < unit.expression_count; index++) {
    const ctool_c_expression_t *expression = &unit.expressions[index];
    if (expression->kind == CTOOL_C_EXPRESSION_UNARY &&
        expression->operation == CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE &&
        dereference_expression == CTOOL_C_AST_NONE) {
      dereference_expression = index;
    } else if (expression->kind == CTOOL_C_EXPRESSION_UNARY &&
               expression->operation == CTOOL_C_EXPRESSION_OPERATOR_ADDRESS &&
               address_expression == CTOOL_C_AST_NONE) {
      address_expression = index;
    }
    if (expression->kind == CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION &&
        expression->conversion == CTOOL_C_CONVERSION_NULL_POINTER) {
      null_conversion_expression = index;
    } else if (expression->kind == CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION &&
               expression->conversion == CTOOL_C_CONVERSION_QUALIFICATION) {
      qualification_conversion_expression = index;
    }
    if (expression->kind == CTOOL_C_EXPRESSION_PARAMETER &&
        expression->location.line == 10u) {
      incompatible_pointer_type = expression->type;
    }
  }
  if (dereference_expression == CTOOL_C_AST_NONE ||
      address_expression == CTOOL_C_AST_NONE ||
      null_conversion_expression == CTOOL_C_AST_NONE ||
      qualification_conversion_expression == CTOOL_C_AST_NONE ||
      incompatible_pointer_type == CTOOL_C_TYPE_NONE ||
      unit.expression_count == 0u ||
      sizeof(*invalid_expressions) >
          SIZE_MAX / (size_t)unit.expression_count) {
    (void)fprintf(stderr, "pointer rejection fixtures differ\n");
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)unit.expression_count * sizeof(*invalid_expressions));
  if (invalid_expressions == NULL) {
    (void)fprintf(stderr, "pointer rejection allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_unit = unit;
  invalid_unit.expressions = invalid_expressions;
  invalid_expressions[dereference_expression].type = unit.graph.type_count;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT, CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "dereference result type range")) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_expressions[address_expression].type = unit.graph.type_count;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT, CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "address result type range")) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_expressions[null_conversion_expression].conversion =
      CTOOL_C_CONVERSION_POINTER;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_CONVERSION,
          "CupidC IR lowering does not yet support this conversion",
          "null pointer conversion kind")) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_expressions[qualification_conversion_expression].type =
      incompatible_pointer_type;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_CONVERSION,
          "CupidC IR lowering does not yet support this conversion",
          "incompatible pointer qualification")) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_expressions[qualification_conversion_expression].conversion =
      CTOOL_C_CONVERSION_ASSIGNMENT;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_CONVERSION,
          "CupidC IR lowering does not yet support this conversion",
          "pointer assignment conversion kind")) {
    goto cleanup;
  }
  if (!parse_source(job, "/atomic-pointer.c", atomic_pointer_source,
                    &atomic_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &atomic_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic pointer load")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_expressions);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("pointer-values: ok");
    return 0;
  }
  return 1;
}

static int validate_function_pointer_call_ir(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_function_definition_t *address_definition;
  const ctool_c_function_definition_t *explicit_address_definition;
  const ctool_c_function_definition_t *dereference_definition;
  const ctool_c_function_definition_t *notify_definition;
  const ctool_c_function_definition_t *three_definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_type_node_t *address_function_type;
  const ctool_c_type_node_t *explicit_address_function_type;
  const ctool_c_type_node_t *dereference_function_type;
  const ctool_c_type_node_t *notify_function_type;
  const ctool_c_type_node_t *three_function_type;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 function_binding = find_binding(unit, "invoke");
  ctool_u32 address_binding = find_binding(unit, "target_address");
  ctool_u32 explicit_address_binding =
      find_binding(unit, "explicit_target_address");
  ctool_u32 dereference_binding =
      find_binding(unit, "invoke_dereferenced");
  ctool_u32 notify_binding = find_binding(unit, "notify");
  ctool_u32 three_binding = find_binding(unit, "invoke_three");
  ctool_u32 target_binding = find_binding(unit, "target");
  ctool_u32 callback_parameter;
  ctool_u32 value_parameter;
  ctool_u32 callback_type;
  ctool_u32 value_type;
  ctool_u32 dereference_callback_parameter;
  ctool_u32 dereference_value_parameter;
  ctool_u32 dereference_callback_type;
  ctool_u32 dereference_value_type;
  ctool_u32 notify_parameter;
  ctool_u32 notify_value_parameter;
  ctool_u32 three_parameter;
  ctool_u32 three_argument;
  if (unit->function_definition_count != 6u || ir->function_count != 6u ||
      ir->instruction_count != 36u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      address_binding == CTOOL_C_AST_NONE ||
      explicit_address_binding == CTOOL_C_AST_NONE ||
      dereference_binding == CTOOL_C_AST_NONE ||
      notify_binding == CTOOL_C_AST_NONE ||
      three_binding == CTOOL_C_AST_NONE ||
      target_binding == CTOOL_C_AST_NONE) {
    ctool_u32 index;
    (void)fprintf(stderr,
                  "function pointer call IR inventory differs: %u functions, "
                  "%u instructions\n",
                  (unsigned int)ir->function_count,
                  (unsigned int)ir->instruction_count);
    if (ir->instructions != NULL) {
      for (index = 0u; index < ir->instruction_count; index++) {
        (void)fprintf(stderr, "%u: kind=%u type=%u input=%u ref=%u\n",
                      (unsigned int)index,
                      (unsigned int)ir->instructions[index].kind,
                      (unsigned int)ir->instructions[index].type,
                      (unsigned int)ir->instructions[index].input_type,
                      (unsigned int)ir->instructions[index].reference);
      }
    }
    return 0;
  }
  definition = &unit->function_definitions[0];
  address_definition = &unit->function_definitions[1];
  explicit_address_definition = &unit->function_definitions[2];
  dereference_definition = &unit->function_definitions[3];
  notify_definition = &unit->function_definitions[4];
  three_definition = &unit->function_definitions[5];
  if (definition->declared_type >= unit->graph.type_count ||
      address_definition->declared_type >= unit->graph.type_count ||
      explicit_address_definition->declared_type >= unit->graph.type_count ||
      dereference_definition->declared_type >= unit->graph.type_count ||
      notify_definition->declared_type >= unit->graph.type_count ||
      three_definition->declared_type >= unit->graph.type_count ||
      unit->bindings[target_binding].type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  address_function_type =
      &unit->graph.types[address_definition->declared_type];
  explicit_address_function_type =
      &unit->graph.types[explicit_address_definition->declared_type];
  dereference_function_type =
      &unit->graph.types[dereference_definition->declared_type];
  notify_function_type =
      &unit->graph.types[notify_definition->declared_type];
  three_function_type = &unit->graph.types[three_definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      function_type->first_parameter > unit->parameter_count ||
      2u > unit->parameter_count - function_type->first_parameter ||
      dereference_function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      dereference_function_type->parameter_count != 2u ||
      dereference_function_type->first_parameter > unit->parameter_count ||
      2u > unit->parameter_count -
               dereference_function_type->first_parameter ||
      notify_function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      notify_function_type->parameter_count != 2u ||
      notify_function_type->first_parameter > unit->parameter_count ||
      2u > unit->parameter_count - notify_function_type->first_parameter ||
      three_function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      three_function_type->parameter_count != 4u ||
      three_function_type->first_parameter > unit->parameter_count ||
      4u > unit->parameter_count - three_function_type->first_parameter) {
    return 0;
  }
  callback_parameter = function_type->first_parameter;
  value_parameter = callback_parameter + 1u;
  callback_type = unit->parameters[callback_parameter].type;
  value_type = unit->parameters[value_parameter].type;
  dereference_callback_parameter =
      dereference_function_type->first_parameter;
  dereference_value_parameter = dereference_callback_parameter + 1u;
  dereference_callback_type =
      unit->parameters[dereference_callback_parameter].type;
  dereference_value_type = unit->parameters[dereference_value_parameter].type;
  notify_parameter = notify_function_type->first_parameter;
  notify_value_parameter = notify_parameter + 1u;
  three_parameter = three_function_type->first_parameter;
  three_argument = three_parameter + 1u;
  instructions = ir->instructions;
  if (definition->binding != function_binding ||
      ir->functions[0].binding != function_binding ||
      ir->functions[0].declared_type != definition->declared_type ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 6u ||
      ir->functions[0].maximum_stack_depth != 2u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != callback_type ||
      instructions[0].reference != callback_parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != callback_type ||
      instructions[1].input_type != callback_type ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[2].type != value_type ||
      instructions[2].reference != value_parameter ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].type != value_type ||
      instructions[3].input_type != value_type ||
      instructions[3].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT ||
      instructions[4].type != value_type ||
      instructions[4].input_type != callback_type ||
      instructions[4].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[4].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[4].reference != CTOOL_C_AST_NONE ||
      instructions[4].integer_bits != 0u ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[5].type != value_type ||
      instructions[5].input_type != value_type ||
      address_definition->binding != address_binding ||
      address_function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      address_function_type->parameter_count != 0u ||
      ir->functions[1].binding != address_binding ||
      ir->functions[1].declared_type != address_definition->declared_type ||
      ir->functions[1].first_instruction != 6u ||
      ir->functions[1].instruction_count != 3u ||
      ir->functions[1].maximum_stack_depth != 1u ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_FUNCTION_ADDRESS ||
      instructions[6].type != unit->bindings[target_binding].type ||
      instructions[6].input_type != CTOOL_C_TYPE_NONE ||
      instructions[6].reference != target_binding ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_FUNCTION_TO_POINTER ||
      instructions[7].input_type != unit->bindings[target_binding].type ||
      instructions[7].conversion != CTOOL_C_CONVERSION_FUNCTION_TO_POINTER ||
      instructions[7].reference != CTOOL_C_AST_NONE ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[8].type != address_function_type->referenced_type ||
      instructions[8].input_type != instructions[7].type ||
      !pointer_value_query_matches(
          job, unit, instructions[7].type,
          address_function_type->referenced_type, CTOOL_TRUE) ||
      explicit_address_definition->binding != explicit_address_binding ||
      explicit_address_function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      explicit_address_function_type->parameter_count != 0u ||
      ir->functions[2].binding != explicit_address_binding ||
      ir->functions[2].declared_type !=
          explicit_address_definition->declared_type ||
      ir->functions[2].first_instruction != 9u ||
      ir->functions[2].instruction_count != 3u ||
      ir->functions[2].maximum_stack_depth != 1u ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_FUNCTION_ADDRESS ||
      instructions[9].type != unit->bindings[target_binding].type ||
      instructions[9].input_type != CTOOL_C_TYPE_NONE ||
      instructions[9].reference != target_binding ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_ADDRESS_OF ||
      instructions[10].input_type != instructions[9].type ||
      instructions[10].operation != CTOOL_C_EXPRESSION_OPERATOR_ADDRESS ||
      instructions[10].reference != CTOOL_C_AST_NONE ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[11].type !=
          explicit_address_function_type->referenced_type ||
      instructions[11].input_type != instructions[10].type ||
      !pointer_value_query_matches(
          job, unit, instructions[10].type,
          explicit_address_function_type->referenced_type, CTOOL_TRUE) ||
      dereference_definition->binding != dereference_binding ||
      ir->functions[3].binding != dereference_binding ||
      ir->functions[3].declared_type != dereference_definition->declared_type ||
      ir->functions[3].first_instruction != 12u ||
      ir->functions[3].instruction_count != 8u ||
      ir->functions[3].maximum_stack_depth != 2u ||
      instructions[12].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[12].type != dereference_callback_type ||
      instructions[12].reference != dereference_callback_parameter ||
      instructions[13].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[13].type != dereference_callback_type ||
      instructions[13].input_type != dereference_callback_type ||
      instructions[14].kind != CTOOL_C_IR_INSTRUCTION_DEREFERENCE ||
      instructions[14].input_type != dereference_callback_type ||
      instructions[14].operation !=
          CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE ||
      instructions[15].kind !=
          CTOOL_C_IR_INSTRUCTION_FUNCTION_TO_POINTER ||
      instructions[15].input_type != instructions[14].type ||
      instructions[15].conversion !=
          CTOOL_C_CONVERSION_FUNCTION_TO_POINTER ||
      instructions[16].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[16].type != dereference_value_type ||
      instructions[16].reference != dereference_value_parameter ||
      instructions[17].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[17].type != dereference_value_type ||
      instructions[17].input_type != dereference_value_type ||
      instructions[18].kind != CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT ||
      instructions[18].type != dereference_value_type ||
      instructions[18].input_type != instructions[15].type ||
      instructions[18].reference != CTOOL_C_AST_NONE ||
      instructions[19].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[19].type != dereference_value_type ||
      instructions[19].input_type != dereference_value_type ||
      notify_definition->binding != notify_binding ||
      ir->functions[4].binding != notify_binding ||
      ir->functions[4].declared_type != notify_definition->declared_type ||
      ir->functions[4].first_instruction != 20u ||
      ir->functions[4].instruction_count != 6u ||
      ir->functions[4].maximum_stack_depth != 2u ||
      instructions[20].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[20].type != unit->parameters[notify_parameter].type ||
      instructions[20].reference != notify_parameter ||
      instructions[21].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[21].type != unit->parameters[notify_parameter].type ||
      instructions[22].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[22].type !=
          unit->parameters[notify_value_parameter].type ||
      instructions[22].reference != notify_value_parameter ||
      instructions[23].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[24].kind != CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT ||
      instructions[24].type != notify_function_type->referenced_type ||
      instructions[24].input_type !=
          unit->parameters[notify_parameter].type ||
      instructions[25].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      three_definition->binding != three_binding ||
      ir->functions[5].binding != three_binding ||
      ir->functions[5].declared_type != three_definition->declared_type ||
      ir->functions[5].first_instruction != 26u ||
      ir->functions[5].instruction_count != 10u ||
      ir->functions[5].maximum_stack_depth != 4u ||
      instructions[26].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[26].type != unit->parameters[three_parameter].type ||
      instructions[26].reference != three_parameter ||
      instructions[27].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[28].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[28].reference != three_argument ||
      instructions[29].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[30].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[30].reference != three_argument + 1u ||
      instructions[31].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[32].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[32].reference != three_argument + 2u ||
      instructions[33].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[34].kind != CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT ||
      instructions[34].type != three_function_type->referenced_type ||
      instructions[34].input_type != unit->parameters[three_parameter].type ||
      instructions[35].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[35].type != three_function_type->referenced_type ||
      instructions[35].input_type != three_function_type->referenced_type ||
      ir_instruction_fingerprint(ir) != UINT64_C(0x859a99312600f810)) {
    ctool_u32 index;
    (void)fprintf(stderr,
                  "function pointer call IR differs: slices %u/%u and %u/%u, "
                  "depths %u/%u, fingerprint %016llx\n",
                  (unsigned int)ir->functions[0].first_instruction,
                  (unsigned int)ir->functions[0].instruction_count,
                  (unsigned int)ir->functions[1].first_instruction,
                  (unsigned int)ir->functions[1].instruction_count,
                  (unsigned int)ir->functions[0].maximum_stack_depth,
                  (unsigned int)ir->functions[1].maximum_stack_depth,
                  (unsigned long long)ir_instruction_fingerprint(ir));
    for (index = 0u; index < ir->instruction_count; index++) {
      (void)fprintf(
          stderr,
          "%u: kind=%u type=%u input=%u op=%u conversion=%u ref=%u\n",
          (unsigned int)index,
          (unsigned int)ir->instructions[index].kind,
          (unsigned int)ir->instructions[index].type,
          (unsigned int)ir->instructions[index].input_type,
          (unsigned int)ir->instructions[index].operation,
          (unsigned int)ir->instructions[index].conversion,
          (unsigned int)ir->instructions[index].reference);
    }
    return 0;
  }
  return 1;
}

static int validate_function_pointer_value_ir(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  static const char *const function_names[] = {
      "round_trip",       "select_callback", "callbacks_equal",
      "callback_missing", "callback_present", "install_target",
      "forward_callback"};
  static const ctool_u32 first_instructions[] = {0u, 12u, 21u, 27u,
                                                 33u, 38u, 46u};
  static const ctool_u32 instruction_counts[] = {12u, 9u, 6u, 6u,
                                                 5u,  8u, 4u};
  static const ctool_u32 maximum_depths[] = {2u, 1u, 2u, 2u,
                                             1u, 2u, 1u};
  static const ctool_c_ir_instruction_kind_t expected_kinds[] = {
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_STORE,
      CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_STORE_VALUE,
      CTOOL_C_IR_INSTRUCTION_DISCARD,
      CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_JUMP,
      CTOOL_C_IR_INSTRUCTION_FUNCTION_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_FUNCTION_TO_POINTER,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_BINARY,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_INTEGER,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_BINARY,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_UNARY,
      CTOOL_C_IR_INSTRUCTION_UNARY,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_FUNCTION_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_FUNCTION_TO_POINTER,
      CTOOL_C_IR_INSTRUCTION_STORE_VALUE,
      CTOOL_C_IR_INSTRUCTION_DISCARD,
      CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE};
  const ctool_c_initializer_t *initializer;
  ctool_u32 stored_binding = find_binding(unit, "stored_callback");
  ctool_u32 target_binding = find_binding(unit, "target");
  ctool_u32 use_binding = find_binding(unit, "use_callback");
  ctool_u32 object_definition = CTOOL_C_AST_NONE;
  ctool_u32 index;
  if (unit->function_definition_count != 7u || ir->function_count != 7u ||
      ir->instruction_count != 50u || ir->functions == NULL ||
      ir->instructions == NULL || unit->object_definition_count != 1u ||
      unit->initializer_count != 2u || stored_binding == CTOOL_C_AST_NONE ||
      target_binding == CTOOL_C_AST_NONE || use_binding == CTOOL_C_AST_NONE ||
      (ctool_u32)(sizeof(expected_kinds) / sizeof(expected_kinds[0])) !=
          ir->instruction_count) {
    (void)fprintf(stderr,
                  "function pointer value IR inventory differs: functions "
                  "%u/%u, instructions %u, objects %u, initializers %u\n",
                  (unsigned int)unit->function_definition_count,
                  (unsigned int)ir->function_count,
                  (unsigned int)ir->instruction_count,
                  (unsigned int)unit->object_definition_count,
                  (unsigned int)unit->initializer_count);
    return 0;
  }
  for (index = 0u; index < unit->object_definition_count; index++) {
    if (unit->object_definitions[index].binding == stored_binding) {
      object_definition = index;
      break;
    }
  }
  if (object_definition == CTOOL_C_AST_NONE ||
      unit->object_definitions[object_definition].initializer >=
          unit->initializer_count) {
    (void)fprintf(stderr, "stored callback definition differs\n");
    return 0;
  }
  initializer =
      &unit->initializers
           [unit->object_definitions[object_definition].initializer];
  if (initializer->kind != CTOOL_C_INITIALIZER_ADDRESS ||
      initializer->type != unit->bindings[stored_binding].type ||
      initializer->address_kind != CTOOL_C_INITIALIZER_ADDRESS_BINDING ||
      initializer->address_reference != target_binding ||
      initializer->address_addend != 0) {
    (void)fprintf(stderr, "stored callback initializer differs\n");
    return 0;
  }
  for (index = 0u; index < ir->function_count; index++) {
    ctool_u32 binding = find_binding(unit, function_names[index]);
    if (binding == CTOOL_C_AST_NONE ||
        unit->function_definitions[index].binding != binding ||
        ir->functions[index].binding != binding ||
        ir->functions[index].declared_type !=
            unit->function_definitions[index].declared_type ||
        ir->functions[index].first_instruction != first_instructions[index] ||
        ir->functions[index].instruction_count != instruction_counts[index] ||
        ir->functions[index].maximum_stack_depth != maximum_depths[index]) {
      (void)fprintf(stderr, "function pointer value slice %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (ir->instructions[index].kind != expected_kinds[index]) {
      (void)fprintf(stderr, "function pointer instruction %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  if (ir->instructions[25].operation != CTOOL_C_EXPRESSION_OPERATOR_EQUAL ||
      ir->instructions[31].operation != CTOOL_C_EXPRESSION_OPERATOR_EQUAL ||
      ir->instructions[30].conversion != CTOOL_C_CONVERSION_NULL_POINTER ||
      ir->instructions[35].operation !=
          CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_NOT ||
      ir->instructions[36].operation !=
          CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_NOT ||
      ir->instructions[39].reference != target_binding ||
      ir->instructions[48].reference != use_binding ||
      ir->instructions[48].input_type != unit->bindings[use_binding].type ||
      !pointer_value_query_matches(
          job, unit, ir->instructions[30].type,
          ir->instructions[28].type, CTOOL_TRUE) ||
      ir_instruction_fingerprint(ir) != UINT64_C(0x2ea1d26a05bc88aa)) {
    (void)fprintf(stderr,
                  "function pointer value semantics differ: fingerprint "
                  "%016llx\n",
                  (unsigned long long)ir_instruction_fingerprint(ir));
    return 0;
  }
  return 1;
}

static int append_callback_relation_layer(
    char *source, size_t capacity, size_t *used,
    const char *name, unsigned int level) {
  int written = snprintf(
      source + *used, capacity - *used,
      "typedef int (*%s%u_t)(%s%u_t, %s%u_t);\n",
      name, level, name, level - 1u, name, level - 1u);
  if (written < 0 || (size_t)written >= capacity - *used) {
    return 0;
  }
  *used += (size_t)written;
  return 1;
}

static int validate_function_pointer_relation_worklist(
    const char *host_root, ctool_job_t *job) {
  char source[8192];
  char left_name[32];
  char right_name[32];
  char different_name[32];
  ctool_c_translation_unit_t unit;
  ctool_host_adapter_t limited_adapter;
  ctool_job_t *limited_job = NULL;
  ctool_u32 left_binding;
  ctool_u32 right_binding;
  ctool_u32 different_binding;
  ctool_u32 left_type;
  ctool_u32 right_type;
  ctool_u32 different_type;
  ctool_bool compatible = CTOOL_TRUE;
  ctool_arena_mark_t mark;
  ctool_status_t status;
  size_t used;
  unsigned int level;
  int written = snprintf(
      source, sizeof(source),
      "typedef int (*left0_t)(int);\n"
      "typedef int (*right0_t)(int);\n"
      "typedef int (*different0_t)(unsigned);\n");
  if (written < 0 || (size_t)written >= sizeof(source)) {
    return 0;
  }
  used = (size_t)written;
  for (level = 1u; level <= 24u; level++) {
    if (!append_callback_relation_layer(
            source, sizeof(source), &used, "left", level) ||
        !append_callback_relation_layer(
            source, sizeof(source), &used, "right", level) ||
        !append_callback_relation_layer(
            source, sizeof(source), &used, "different", level)) {
      return 0;
    }
  }
  (void)memset(&unit, 0, sizeof(unit));
  if (!parse_source(job, "/function-pointer-relation-worklist.c", source,
                    &unit)) {
    return 0;
  }
  if (snprintf(left_name, sizeof(left_name), "left%u_t", level - 1u) < 0 ||
      snprintf(right_name, sizeof(right_name), "right%u_t", level - 1u) <
          0 ||
      snprintf(different_name, sizeof(different_name), "different%u_t",
               level - 1u) < 0) {
    return 0;
  }
  left_binding = find_binding(&unit, left_name);
  right_binding = find_binding(&unit, right_name);
  different_binding = find_binding(&unit, different_name);
  if (left_binding == CTOOL_C_AST_NONE ||
      right_binding == CTOOL_C_AST_NONE ||
      different_binding == CTOOL_C_AST_NONE) {
    return 0;
  }
  left_type = unit.bindings[left_binding].type;
  right_type = unit.bindings[right_binding].type;
  different_type = unit.bindings[different_binding].type;
  if (left_type == right_type || left_type == different_type ||
      unit.graph.types[left_type].kind != CTOOL_C_TYPE_POINTER ||
      unit.graph.types[right_type].kind != CTOOL_C_TYPE_POINTER ||
      unit.graph.types[left_type].referenced_type ==
          unit.graph.types[right_type].referenced_type ||
      !pointer_value_query_matches(
          job, &unit, left_type, right_type, CTOOL_TRUE) ||
      !pointer_value_query_matches(
          job, &unit, right_type, left_type, CTOOL_TRUE) ||
      !pointer_value_query_matches(
          job, &unit, left_type, different_type, CTOOL_FALSE)) {
    (void)fprintf(stderr, "function pointer relation worklist differs\n");
    return 0;
  }
  if (!open_limited_job(host_root, &limited_adapter, &limited_job)) {
    return 0;
  }
  mark = ctool_arena_mark(ctool_job_arena(limited_job));
  status = ctool_c_ir_pointer_value_types_compatible(
      limited_job, &unit, left_type, right_type, &compatible);
  if (status != CTOOL_ERR_LIMIT || compatible != CTOOL_FALSE ||
      arena_marks_equal(
          mark, ctool_arena_mark(ctool_job_arena(limited_job))) == 0) {
    (void)fprintf(stderr,
                  "function pointer relation limit rollback differs\n");
    ctool_job_close(limited_job);
    return 0;
  }
  ctool_job_close(limited_job);
  return 1;
}

static int run_function_pointers(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t value_unit;
  ctool_c_translation_unit_t wide_unit;
  ctool_c_translation_unit_t atomic_unit;
  ctool_c_translation_unit_t cast_unit;
  ctool_c_translation_unit_t qualification_unit;
  ctool_c_translation_unit_t malformed_qualification_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_c_ir_unit_t ir;
  ctool_c_ir_unit_t value_ir;
  ctool_u64 fingerprint;
  ctool_u32 diagnostic_count;
  ctool_u32 relational_expression = CTOOL_C_AST_NONE;
  ctool_u32 plain_callback;
  ctool_u32 qualified_parameter_callback;
  ctool_u32 atomic_parameter_callback;
  ctool_u32 plain_pointer_callback;
  ctool_u32 restrict_parameter_callback;
  ctool_u32 qualified_referent_callback;
  ctool_u32 old_style_callback;
  ctool_u32 promoted_parameter_callback;
  ctool_u32 promoted_char_callback;
  ctool_bool query_result = CTOOL_TRUE;
  ctool_u32 index;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&value_unit, 0, sizeof(value_unit));
  (void)memset(&wide_unit, 0, sizeof(wide_unit));
  (void)memset(&atomic_unit, 0, sizeof(atomic_unit));
  (void)memset(&cast_unit, 0, sizeof(cast_unit));
  (void)memset(&qualification_unit, 0, sizeof(qualification_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/function-pointer-call.c",
                    function_pointer_call_source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "function pointer call lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_function_pointer_call_ir(job, &unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/function-pointer-values.c",
                    function_pointer_value_source, &value_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&value_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&value_ir, 0xa5, sizeof(value_ir));
  status = ctool_c_lower_ir(job, &value_unit, &value_ir);
  if (!check_status(status, CTOOL_OK, "function pointer value lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&value_unit) != fingerprint ||
      !validate_function_pointer_value_ir(job, &value_unit, &value_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < value_unit.expression_count; index++) {
    const ctool_c_expression_t *expression = &value_unit.expressions[index];
    if (expression->kind == CTOOL_C_EXPRESSION_BINARY &&
        expression->operation == CTOOL_C_EXPRESSION_OPERATOR_EQUAL) {
      relational_expression = index;
      break;
    }
  }
  if (relational_expression == CTOOL_C_AST_NONE ||
      value_unit.expression_count == 0u ||
      sizeof(*invalid_expressions) >
          SIZE_MAX / (size_t)value_unit.expression_count) {
    (void)fprintf(stderr, "function pointer rejection fixture differs\n");
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)value_unit.expression_count * sizeof(*invalid_expressions));
  if (invalid_expressions == NULL) {
    (void)fprintf(stderr, "function pointer rejection allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, value_unit.expressions,
               (size_t)value_unit.expression_count *
                   sizeof(*invalid_expressions));
  invalid_unit = value_unit;
  invalid_unit.expressions = invalid_expressions;
  invalid_expressions[relational_expression].operation =
      CTOOL_C_EXPRESSION_OPERATOR_LESS;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT, CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "function pointer relational comparison")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-function-pointer.c",
                    wide_function_pointer_source, &wide_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &wide_unit, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports calls with "
          "represented scalar or structure arguments and void, scalar, or "
          "structure results",
          "wide indirect call")) {
    goto cleanup;
  }
  if (!parse_source(job, "/atomic-function-pointer.c",
                    atomic_function_pointer_source, &atomic_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &atomic_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic function pointer load")) {
    goto cleanup;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(function_pointer_cast_sources) /
                           sizeof(function_pointer_cast_sources[0]));
       index++) {
    (void)memset(&cast_unit, 0, sizeof(cast_unit));
    if (!parse_source(job, function_pointer_cast_paths[index],
                      function_pointer_cast_sources[index], &cast_unit) ||
        !expect_ir_failure_preserves_unit(
            job, &cast_unit, CTOOL_ERR_UNSUPPORTED,
            CTOOL_C_IR_DIAG_UNSUPPORTED_CONVERSION,
            "CupidC IR lowering does not yet support this conversion",
            "explicit function pointer cast")) {
      goto cleanup;
    }
  }
  if (!parse_source(job, "/function-pointer-qualification.c",
                    function_pointer_qualification_source,
                    &qualification_unit)) {
    goto cleanup;
  }
  plain_callback = find_binding(&qualification_unit, "plain_callback_t");
  qualified_parameter_callback = find_binding(
      &qualification_unit, "qualified_parameter_callback_t");
  atomic_parameter_callback =
      find_binding(&qualification_unit, "atomic_parameter_callback_t");
  plain_pointer_callback =
      find_binding(&qualification_unit, "plain_pointer_callback_t");
  restrict_parameter_callback = find_binding(
      &qualification_unit, "restrict_parameter_callback_t");
  qualified_referent_callback = find_binding(
      &qualification_unit, "qualified_referent_callback_t");
  old_style_callback =
      find_binding(&qualification_unit, "old_style_callback_t");
  promoted_parameter_callback = find_binding(
      &qualification_unit, "promoted_parameter_callback_t");
  promoted_char_callback =
      find_binding(&qualification_unit, "promoted_char_callback_t");
  malformed_qualification_unit = qualification_unit;
  malformed_qualification_unit.graph.parameter_types = NULL;
  if (plain_callback == CTOOL_C_AST_NONE ||
      qualified_parameter_callback == CTOOL_C_AST_NONE ||
      atomic_parameter_callback == CTOOL_C_AST_NONE ||
      plain_pointer_callback == CTOOL_C_AST_NONE ||
      restrict_parameter_callback == CTOOL_C_AST_NONE ||
      qualified_referent_callback == CTOOL_C_AST_NONE ||
      old_style_callback == CTOOL_C_AST_NONE ||
      promoted_parameter_callback == CTOOL_C_AST_NONE ||
      promoted_char_callback == CTOOL_C_AST_NONE ||
      qualification_unit.graph.parameter_type_count == 0u ||
      ctool_c_ir_pointer_value_types_compatible(
          NULL, &qualification_unit,
          qualification_unit.bindings[plain_callback].type,
          qualification_unit.bindings[qualified_parameter_callback].type,
          &query_result) != CTOOL_ERR_INVALID_ARGUMENT ||
      query_result != CTOOL_TRUE ||
      ctool_c_ir_pointer_value_types_compatible(
          job, &qualification_unit,
          qualification_unit.bindings[plain_callback].type,
          qualification_unit.bindings[qualified_parameter_callback].type,
          NULL) != CTOOL_ERR_INVALID_ARGUMENT ||
      !pointer_value_query_matches(
          job, &qualification_unit,
          qualification_unit.bindings[plain_callback].type,
          qualification_unit.bindings[qualified_parameter_callback].type,
          CTOOL_TRUE) ||
      !pointer_value_query_matches(
          job, &qualification_unit,
          qualification_unit.bindings[qualified_parameter_callback].type,
          qualification_unit.bindings[plain_callback].type, CTOOL_TRUE) ||
      !pointer_value_query_matches(
          job, &qualification_unit,
          qualification_unit.bindings[plain_callback].type,
          qualification_unit.bindings[atomic_parameter_callback].type,
          CTOOL_FALSE) ||
      !pointer_value_query_matches(
          job, &qualification_unit,
          qualification_unit.bindings[atomic_parameter_callback].type,
          qualification_unit.bindings[plain_callback].type, CTOOL_FALSE) ||
      !pointer_value_query_matches(
          job, &qualification_unit,
          qualification_unit.bindings[plain_pointer_callback].type,
          qualification_unit.bindings[restrict_parameter_callback].type,
          CTOOL_TRUE) ||
      !pointer_value_query_matches(
          job, &qualification_unit,
          qualification_unit.bindings[restrict_parameter_callback].type,
          qualification_unit.bindings[plain_pointer_callback].type,
          CTOOL_TRUE) ||
      !pointer_value_query_matches(
          job, &qualification_unit,
          qualification_unit.bindings[plain_pointer_callback].type,
          qualification_unit.bindings[qualified_referent_callback].type,
          CTOOL_FALSE) ||
      !pointer_value_query_matches(
          job, &qualification_unit,
          qualification_unit.bindings[qualified_referent_callback].type,
          qualification_unit.bindings[plain_pointer_callback].type,
          CTOOL_FALSE) ||
      !pointer_value_query_matches(
          job, &qualification_unit,
          qualification_unit.bindings[old_style_callback].type,
          qualification_unit.bindings[promoted_parameter_callback].type,
          CTOOL_TRUE) ||
      !pointer_value_query_matches(
          job, &qualification_unit,
          qualification_unit.bindings[promoted_parameter_callback].type,
          qualification_unit.bindings[old_style_callback].type,
          CTOOL_TRUE) ||
      !pointer_value_query_matches(
          job, &qualification_unit,
          qualification_unit.bindings[old_style_callback].type,
          qualification_unit.bindings[promoted_char_callback].type,
          CTOOL_FALSE) ||
      !pointer_value_query_matches(
          job, &qualification_unit,
          qualification_unit.bindings[promoted_char_callback].type,
          qualification_unit.bindings[old_style_callback].type,
          CTOOL_FALSE) ||
      !pointer_value_query_matches(
          job, &malformed_qualification_unit,
          qualification_unit.bindings[plain_callback].type,
          qualification_unit.bindings[qualified_parameter_callback].type,
          CTOOL_FALSE) ||
      !validate_function_pointer_relation_worklist(host_root, job)) {
    (void)fprintf(stderr,
                  "function pointer parameter qualification differs\n");
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_expressions);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("function-pointers: ok");
    return 0;
  }
  return 1;
}

static int validate_pointer_comparison_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  static const char *const function_names[] = {
      "ctool_job_arena",       "object_pointer_equal",
      "object_pointer_less",  "object_pointer_bits",
      "object_pointer_erase", "object_pointer_restore"};
  static const ctool_u32 function_counts[] = {15u, 6u, 6u, 4u, 4u, 4u};
  static const ctool_u32 stack_depths[] = {2u, 2u, 2u, 1u, 1u, 1u};
  static const pointer_ir_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 3u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       4u, 10u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 3u, 3u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 4u, 10u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 7u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 4u, 32u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, 6u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 4u, 17u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, 7u, 3u,
       CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 4u, 14u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 12u, 0u,
       4u, 34u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 3u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       4u, 36u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 3u, 3u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 4u, 36u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 1u, 3u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 4u, 39u},
      {CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS, 2u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       4u, 41u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 2u, 2u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 4u, 41u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 14u, 0u,
       4u, 34u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 7u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 4u, 66u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, 8u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 4u, 49u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 4u, 9u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 4u, 3u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 11u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 1u, 0u,
       7u, 10u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 11u, 11u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 7u, 10u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 13u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 2u, 0u,
       7u, 18u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 13u, 13u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 7u, 18u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, 7u, 11u,
       CTOOL_C_EXPRESSION_OPERATOR_EQUAL, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 7u, 15u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 7u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 7u, 3u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 16u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 3u, 0u,
       10u, 10u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 16u, 16u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 10u, 10u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 18u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 4u, 0u,
       10u, 17u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 18u, 18u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 10u, 17u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, 7u, 16u,
       CTOOL_C_EXPRESSION_OPERATOR_LESS, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 10u, 15u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 7u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 10u, 3u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 22u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 5u, 0u,
       13u, 24u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 22u, 22u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 13u, 24u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, 20u, 22u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 13u, 10u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 20u, 20u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 13u, 3u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 25u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 6u, 0u,
       15u, 59u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 25u, 25u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 15u, 59u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, 28u, 25u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 15u, 51u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 26u, 28u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 15u, 44u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 29u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 7u, 0u,
       16u, 60u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 29u, 29u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 16u, 60u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, 32u, 29u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 16u, 53u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 30u, 32u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 16u, 46u}};
  ctool_u32 first = 0u;
  ctool_u32 index;
  if (unit == NULL || ir == NULL || ir->function_count != 6u ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      ir->functions == NULL || ir->instructions == NULL) {
    (void)fprintf(stderr, "pointer comparison IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < ir->function_count; index++) {
    ctool_u32 binding = find_binding(unit, function_names[index]);
    const ctool_c_ir_function_t *function = &ir->functions[index];
    if (binding == CTOOL_C_AST_NONE || function->binding != binding ||
        function->first_instruction != first ||
        function->instruction_count != function_counts[index] ||
        function->maximum_stack_depth != stack_depths[index]) {
      (void)fprintf(stderr, "pointer comparison function %u differs\n",
                    (unsigned int)index);
      return 0;
    }
    first += function_counts[index];
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (!pointer_ir_instruction_matches(
            &ir->instructions[index], &expected[index],
            "/pointer-comparisons.c")) {
      (void)fprintf(stderr, "pointer comparison instruction %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  return 1;
}

static int run_pointer_comparisons(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t wide_cast_unit;
  ctool_c_translation_unit_t void_equality_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_translation_unit_t invalid_void_unit;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_c_expression_t *invalid_void_expressions = NULL;
  ctool_c_ir_unit_t ir;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  ctool_u32 comparison_expression = CTOOL_C_AST_NONE;
  ctool_u32 void_comparison_expression = CTOOL_C_AST_NONE;
  ctool_u32 index;
  uint64_t fingerprint;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&wide_cast_unit, 0, sizeof(wide_cast_unit));
  (void)memset(&void_equality_unit, 0, sizeof(void_equality_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_is_unchanged(job) ||
      !parse_source(job, "/pointer-comparisons.c", pointer_comparison_source,
                    &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "object-pointer comparison lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_pointer_comparison_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < unit.expression_count; index++) {
    if (unit.expressions[index].kind == CTOOL_C_EXPRESSION_BINARY &&
        unit.expressions[index].operation ==
            CTOOL_C_EXPRESSION_OPERATOR_EQUAL) {
      comparison_expression = index;
      break;
    }
  }
  if (comparison_expression == CTOOL_C_AST_NONE ||
      unit.expression_count == 0u ||
      sizeof(*invalid_expressions) >
          SIZE_MAX / (size_t)unit.expression_count) {
    (void)fprintf(stderr, "pointer comparison rejection fixtures differ\n");
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)unit.expression_count * sizeof(*invalid_expressions));
  if (invalid_expressions == NULL) {
    (void)fprintf(stderr, "pointer comparison rejection allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_unit = unit;
  invalid_unit.expressions = invalid_expressions;
  invalid_expressions[comparison_expression].operation =
      CTOOL_C_EXPRESSION_OPERATOR_ADD;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "malformed pointer arithmetic") ||
      !parse_source(job, "/void-pointer-equality.c",
                    void_pointer_equality_source, &void_equality_unit)) {
    goto cleanup;
  }
  for (index = 0u; index < void_equality_unit.expression_count; index++) {
    if (void_equality_unit.expressions[index].kind ==
            CTOOL_C_EXPRESSION_BINARY &&
        void_equality_unit.expressions[index].operation ==
            CTOOL_C_EXPRESSION_OPERATOR_EQUAL) {
      void_comparison_expression = index;
      break;
    }
  }
  if (void_comparison_expression == CTOOL_C_AST_NONE ||
      void_equality_unit.expression_count == 0u ||
      sizeof(*invalid_void_expressions) >
          SIZE_MAX / (size_t)void_equality_unit.expression_count) {
    (void)fprintf(stderr, "void pointer rejection fixture differs\n");
    goto cleanup;
  }
  invalid_void_expressions = (ctool_c_expression_t *)malloc(
      (size_t)void_equality_unit.expression_count *
      sizeof(*invalid_void_expressions));
  if (invalid_void_expressions == NULL) {
    (void)fprintf(stderr, "void pointer rejection allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_void_expressions, void_equality_unit.expressions,
               (size_t)void_equality_unit.expression_count *
                   sizeof(*invalid_void_expressions));
  invalid_void_unit = void_equality_unit;
  invalid_void_unit.expressions = invalid_void_expressions;
  invalid_void_expressions[void_comparison_expression].operation =
      CTOOL_C_EXPRESSION_OPERATOR_LESS;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_void_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "malformed void pointer order") ||
      !parse_source(job, "/wide-pointer-cast.c", wide_pointer_cast_source,
                    &wide_cast_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &wide_cast_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide integer pointer cast")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_void_expressions);
  free(invalid_expressions);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("pointer-comparisons: ok");
    return 0;
  }
  return 1;
}

static int validate_pointer_condition_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  static const char *const function_names[] = {
      "pointer_not", "pointer_and", "pointer_or", "pointer_select",
      "pointer_if",  "pointer_while", "pointer_do", "pointer_for"};
  static const ctool_u32 function_counts[] = {4u, 10u, 12u, 7u,
                                               7u, 6u,  10u, 6u};
  static const pointer_ir_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 1u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u, 1u,
       41u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 1u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 1u, 41u},
      {CTOOL_C_IR_INSTRUCTION_UNARY, 0u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_NOT, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 1u, 40u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 1u, 33u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 3u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 1u, 0u, 2u,
       49u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 3u, 3u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 2u, 49u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 3u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 8u, 0u, 2u,
       54u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 4u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 2u, 0u, 2u,
       57u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 4u, 4u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 2u, 57u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 4u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 8u, 0u, 2u,
       54u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u, 2u, 54u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 9u, 0u, 2u,
       54u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 2u, 54u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 2u, 42u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 6u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 3u, 0u, 3u,
       48u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 6u, 6u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 3u, 48u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 6u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 5u, 0u, 3u,
       53u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u, 3u, 53u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 11u, 0u, 3u,
       53u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 7u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 4u, 0u, 3u,
       56u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 7u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 3u, 56u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 10u, 0u, 3u,
       53u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u, 3u, 53u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 11u, 0u, 3u,
       53u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 3u, 53u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 3u, 41u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 9u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 5u, 0u, 4u,
       43u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 9u, 9u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 4u, 43u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 9u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 5u, 0u, 4u,
       51u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u, 4u, 53u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 6u, 0u, 4u,
       51u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 4u, 57u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 4u, 36u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 11u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 6u, 0u, 5u,
       36u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 11u, 11u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 5u, 36u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 11u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 5u, 0u, 5u,
       36u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u, 5u, 52u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 5u, 45u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 5u, 62u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 5u, 55u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 13u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 7u, 0u, 6u,
       42u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 13u, 13u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 6u, 42u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 13u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 4u, 0u, 6u,
       42u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 4u, 0u, 6u,
       51u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 6u, 65u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 6u, 58u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 15u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 8u, 0u, 8u,
       12u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 15u, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 8u, 12u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 4u, 0u, 8u,
       12u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 8u, 0u, 8u,
       21u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 15u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 8u, 0u, 8u,
       37u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 15u, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 8u, 37u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 8u, 0u, 8u,
       37u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u, 8u,
       3u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 9u, 10u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 9u, 3u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 17u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 9u, 0u, 11u,
       40u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 17u, 17u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 11u, 40u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE, 17u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 4u, 0u, 11u,
       40u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 4u, 0u, 11u,
       50u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 11u, 64u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 11u, 57u}};
  ctool_u32 first = 0u;
  ctool_u32 index;
  if (unit == NULL || ir == NULL || ir->function_count != 8u ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      ir->functions == NULL || ir->instructions == NULL) {
    (void)fprintf(stderr, "pointer condition IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < ir->function_count; index++) {
    const ctool_c_ir_function_t *function = &ir->functions[index];
    ctool_u32 binding = find_binding(unit, function_names[index]);
    if (binding == CTOOL_C_AST_NONE || function->binding != binding ||
        function->first_instruction != first ||
        function->instruction_count != function_counts[index] ||
        function->maximum_stack_depth != 1u) {
      (void)fprintf(stderr, "pointer condition function %u differs\n",
                    (unsigned int)index);
      return 0;
    }
    first += function_counts[index];
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (!pointer_ir_instruction_matches(
            &ir->instructions[index], &expected[index],
            "/pointer-conditions.c")) {
      (void)fprintf(stderr, "pointer condition instruction %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  return 1;
}

static int run_pointer_conditions(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t atomic_unit;
  ctool_c_ir_unit_t ir;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  uint64_t fingerprint;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&atomic_unit, 0, sizeof(atomic_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/pointer-conditions.c", pointer_condition_source,
                    &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "object-pointer condition lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_pointer_condition_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/atomic-pointer-condition.c",
                    atomic_pointer_condition_source, &atomic_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &atomic_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic pointer condition")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("pointer-conditions: ok");
    return 0;
  }
  return 1;
}

static int validate_ata_and_pointer_update_ir(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir, ctool_u32 first_instruction) {
  static const ctool_u32 referent_sizes[] = {2u, 2u, 4u, 4u};
  static const ctool_u32 parameter_columns[] = {55u, 68u, 45u, 46u};
  static const ctool_u32 operation_columns[] = {59u, 72u, 52u, 44u};
  static const ctool_u32 return_columns[] = {48u, 61u, 38u, 37u};
  pointer_ir_expected_t expected[30];
  ctool_u32 expected_count = 0u;
  ctool_u32 function_index;
  ctool_u32 instruction_index;
  for (function_index = 0u; function_index < 4u; function_index++) {
    const ctool_c_function_definition_t *definition =
        &unit->function_definitions[12u + function_index];
    const ctool_c_type_node_t *function_type;
    const ctool_c_type_node_t *pointer_type;
    ctool_u32 parameter;
    ctool_u32 parameter_type;
    ctool_u32 result_type;
    ctool_u32 line = 15u + function_index;
    if (definition->declared_type >= unit->graph.type_count) {
      return 0;
    }
    function_type = &unit->graph.types[definition->declared_type];
    if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
        function_type->parameter_count != 1u ||
        function_type->first_parameter >= unit->parameter_count) {
      return 0;
    }
    parameter = function_type->first_parameter;
    parameter_type = unit->parameters[parameter].type;
    result_type = function_type->referenced_type;
    if (parameter_type >= unit->graph.type_count ||
        result_type >= unit->graph.type_count ||
        !pointer_value_query_matches(
            job, unit, parameter_type, result_type, CTOOL_TRUE)) {
      return 0;
    }
    pointer_type = &unit->graph.types[parameter_type];
    if (pointer_type->kind != CTOOL_C_TYPE_POINTER ||
        pointer_type->referenced_type >= unit->layout.type_count ||
        unit->layout.types[pointer_type->referenced_type].size !=
            referent_sizes[function_index]) {
      return 0;
    }
    expected[expected_count++] = (pointer_ir_expected_t){
        CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, parameter_type,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, parameter, 0u, line,
        parameter_columns[function_index]};
    expected[expected_count++] = (pointer_ir_expected_t){
        CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS, parameter_type,
        parameter_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, line,
        operation_columns[function_index]};
    expected[expected_count++] = (pointer_ir_expected_t){
        CTOOL_C_IR_INSTRUCTION_LOAD, parameter_type, parameter_type,
        CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, line,
        operation_columns[function_index]};
    expected[expected_count++] = (pointer_ir_expected_t){
        CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
        CTOOL_C_AST_NONE, function_index < 2u ? 256u : 1u, line,
        function_index < 2u ? operation_columns[function_index] + 3u
                            : operation_columns[function_index]};
    expected[expected_count++] = (pointer_ir_expected_t){
        CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, parameter_type,
        parameter_type,
        function_index == 3u
            ? CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT
            : CTOOL_C_EXPRESSION_OPERATOR_ADD,
        CTOOL_C_CONVERSION_NONE, 0u, 0u, line,
        operation_columns[function_index]};
    expected[expected_count++] = (pointer_ir_expected_t){
        CTOOL_C_IR_INSTRUCTION_STORE_VALUE, parameter_type, parameter_type,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
        CTOOL_C_AST_NONE, 0u, line, operation_columns[function_index]};
    if (function_index == 2u) {
      expected[expected_count++] = (pointer_ir_expected_t){
          CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          CTOOL_C_AST_NONE, 1u, line, operation_columns[function_index]};
      expected[expected_count++] = (pointer_ir_expected_t){
          CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, parameter_type,
          parameter_type, CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
          CTOOL_C_CONVERSION_NONE, 0u, 0u, line,
          operation_columns[function_index]};
    }
    expected[expected_count++] = (pointer_ir_expected_t){
        CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, result_type, parameter_type,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
        CTOOL_C_AST_NONE, 0u, line, return_columns[function_index]};
  }
  if (expected_count != 30u || first_instruction > ir->instruction_count ||
      expected_count > ir->instruction_count - first_instruction) {
    return 0;
  }
  for (instruction_index = 0u; instruction_index < expected_count;
       instruction_index++) {
    if (!pointer_ir_instruction_matches(
            &ir->instructions[first_instruction + instruction_index],
            &expected[instruction_index], "/pointer-arithmetic.c")) {
      (void)fprintf(stderr,
                    "ATA and pointer update instruction %u differs\n",
                    (unsigned int)instruction_index);
      return 0;
    }
  }
  return 1;
}

static int validate_pointer_arithmetic_ir(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  static const char *const function_names[] = {
      "advance",      "reverse_add",      "retreat", "distance",
      "read_index",   "read_reverse",     "global_start",
      "read_global_index", "prefix_advance", "postfix_retreat",
      "assign_advance", "assign_retreat", "advance_read_sector",
      "advance_write_sector", "postfix_advance", "prefix_retreat"};
  static const ctool_u32 function_counts[] = {
      6u, 6u, 6u, 6u, 8u, 8u, 3u, 8u, 7u, 9u, 8u, 8u, 7u, 7u, 9u, 7u};
  static const ctool_u32 stack_depths[] = {
      2u, 2u, 2u, 2u, 2u, 2u, 1u, 2u, 3u, 3u, 3u, 3u, 3u, 3u, 3u, 3u};
  static const ctool_u32 function_lines[] = {
      1u, 2u, 3u, 4u, 5u, 6u, 8u, 9u, 10u, 11u, 12u, 13u, 15u, 16u,
      17u, 18u};
  static const pointer_ir_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 1u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       1u, 48u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 1u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 1u, 48u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 1u, 0u,
       1u, 58u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 1u, 58u},
      {CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, 1u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_ADD, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       1u, 56u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 2u, 1u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 1u, 41u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 2u, 0u,
       2u, 52u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 2u, 52u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 4u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 3u, 0u,
       2u, 60u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 4u, 4u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 2u, 60u},
      {CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, 4u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_ADD, CTOOL_C_CONVERSION_NONE, 4u, 0u,
       2u, 58u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 5u, 4u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 2u, 45u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 7u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 4u, 0u,
       3u, 48u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 7u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 3u, 48u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 5u, 0u,
       3u, 58u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 3u, 58u},
      {CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, 7u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT, CTOOL_C_CONVERSION_NONE, 0u,
       0u, 3u, 56u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 8u, 7u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 3u, 41u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 10u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 6u, 0u,
       4u, 51u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 10u, 10u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 4u, 51u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 12u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 7u, 0u,
       4u, 57u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 12u, 12u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 4u, 57u},
      {CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, 0u, 10u,
       CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT, CTOOL_C_CONVERSION_NONE, 12u,
       0u, 4u, 55u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 4u, 44u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 14u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 8u, 0u,
       5u, 59u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 14u, 14u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 5u, 59u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 15u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 9u, 0u,
       5u, 67u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 15u, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 5u, 67u},
      {CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, 14u, 14u,
       CTOOL_C_EXPRESSION_OPERATOR_ADD, CTOOL_C_CONVERSION_NONE, 15u, 0u,
       5u, 66u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 0u, 14u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 5u, 66u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 5u, 66u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 5u, 52u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 15u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 11u, 0u,
       6u, 61u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 15u, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 6u, 61u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 17u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 10u, 0u,
       6u, 67u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 17u, 17u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 6u, 67u},
      {CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, 17u, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_ADD, CTOOL_C_CONVERSION_NONE, 17u, 0u,
       6u, 66u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 0u, 17u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 6u, 66u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 6u, 66u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 6u, 54u},
      {CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS, 19u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 6u, 0u,
       8u, 34u},
      {CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER, 22u, 19u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_ARRAY_TO_POINTER, CTOOL_C_AST_NONE, 0u, 8u, 34u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 20u, 22u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 8u, 27u},
      {CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS, 19u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 6u, 0u,
       9u, 52u},
      {CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER, 24u, 19u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_ARRAY_TO_POINTER, CTOOL_C_AST_NONE, 0u, 9u, 52u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 15u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 12u, 0u,
       9u, 66u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 15u, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 9u, 66u},
      {CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, 24u, 24u,
       CTOOL_C_EXPRESSION_OPERATOR_ADD, CTOOL_C_CONVERSION_NONE, 15u, 0u,
       9u, 65u},
      {CTOOL_C_IR_INSTRUCTION_DEREFERENCE, 0u, 24u,
       CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 9u, 65u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 9u, 65u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 0u, 0u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 9u, 45u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 25u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 13u, 0u,
       10u, 46u},
      {CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS, 25u, 25u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 10u, 44u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 25u, 25u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 10u, 44u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u, 10u, 44u},
      {CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, 25u, 25u,
       CTOOL_C_EXPRESSION_OPERATOR_ADD, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       10u, 44u},
      {CTOOL_C_IR_INSTRUCTION_STORE_VALUE, 25u, 25u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 10u, 44u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 26u, 25u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 10u, 37u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 28u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 14u, 0u,
       11u, 45u},
      {CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS, 28u, 28u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 11u, 52u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 28u, 28u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 11u, 52u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u, 11u, 52u},
      {CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, 28u, 28u,
       CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT, CTOOL_C_CONVERSION_NONE, 0u,
       0u, 11u, 52u},
      {CTOOL_C_IR_INSTRUCTION_STORE_VALUE, 28u, 28u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 11u, 52u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, 0u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u, 11u, 52u},
      {CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, 28u, 28u,
       CTOOL_C_EXPRESSION_OPERATOR_ADD, CTOOL_C_CONVERSION_NONE, 0u, 0u,
       11u, 52u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 29u, 28u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 11u, 38u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 31u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 15u, 0u,
       12u, 64u},
      {CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS, 31u, 31u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 12u, 72u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 31u, 31u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 12u, 72u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 15u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 16u, 0u,
       12u, 75u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 15u, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 12u, 75u},
      {CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, 31u, 31u,
       CTOOL_C_EXPRESSION_OPERATOR_ADD, CTOOL_C_CONVERSION_NONE, 15u, 0u,
       12u, 72u},
      {CTOOL_C_IR_INSTRUCTION_STORE_VALUE, 31u, 31u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 12u, 72u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 32u, 31u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 12u, 57u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 34u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 17u, 0u,
       13u, 64u},
      {CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS, 34u, 34u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 13u, 72u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 34u, 34u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 13u, 72u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, 15u, CTOOL_C_TYPE_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 18u, 0u,
       13u, 75u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, 15u, 15u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u, 13u, 75u},
      {CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, 34u, 34u,
       CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT, CTOOL_C_CONVERSION_NONE, 15u,
       0u, 13u, 72u},
      {CTOOL_C_IR_INSTRUCTION_STORE_VALUE, 34u, 34u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 13u, 72u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, 35u, 34u,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u, 13u, 57u}};
  ctool_u32 first = 0u;
  ctool_u32 index;
  if (unit == NULL || ir == NULL || unit->function_definition_count != 16u ||
      ir->function_count != 16u || ir->instruction_count != 113u ||
      ir->functions == NULL || ir->instructions == NULL) {
    (void)fprintf(stderr, "pointer arithmetic IR inventory differs\n");
    return 0;
  }
  if (!pointer_arithmetic_query_matches(
          job, unit, ir->instructions[22].input_type,
          ir->instructions[22].reference, CTOOL_TRUE) ||
      !pointer_arithmetic_query_matches(
          job, unit, ir->instructions[4].input_type,
          ir->instructions[4].reference, CTOOL_FALSE) ||
      !pointer_arithmetic_query_matches(
          job, NULL, ir->instructions[22].input_type,
          ir->instructions[22].reference, CTOOL_FALSE)) {
    (void)fprintf(stderr, "pointer arithmetic type query differs\n");
    return 0;
  }
  for (index = 0u; index < ir->function_count; index++) {
    const ctool_c_function_definition_t *definition =
        &unit->function_definitions[index];
    const ctool_c_ir_function_t *function = &ir->functions[index];
    ctool_u32 binding = find_binding(unit, function_names[index]);
    if (binding == CTOOL_C_AST_NONE || definition->binding != binding ||
        function->binding != binding ||
        function->declared_type != definition->declared_type ||
        function->first_instruction != first ||
        function->instruction_count != function_counts[index] ||
        function->maximum_stack_depth != stack_depths[index] ||
        !string_equal(function->location.path, "/pointer-arithmetic.c") ||
        !string_equal(function->physical_location.path,
                      "/pointer-arithmetic.c") ||
        function->location.line != function_lines[index] ||
        function->physical_location.line != function_lines[index]) {
      (void)fprintf(stderr, "pointer arithmetic function %u differs\n",
                    (unsigned int)index);
      return 0;
    }
    first += function_counts[index];
  }
  for (index = 0u; index < 83u; index++) {
    if (!pointer_ir_instruction_matches(
            &ir->instructions[index], &expected[index],
            "/pointer-arithmetic.c")) {
      (void)fprintf(stderr, "pointer arithmetic instruction %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  return validate_ata_and_pointer_update_ir(job, unit, ir, 83u);
}

static int validate_qualified_array_decay_ir(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 array_binding = find_binding(unit, "const_values");
  ctool_u32 function_binding = find_binding(unit, "const_start");
  ctool_u32 unqualified_binding = find_binding(unit, "unqualified_pointer");
  ctool_u32 array_type;
  ctool_u32 pointer_type;
  ctool_u32 unqualified_pointer_type;
  ctool_u32 index;
  if (array_binding == CTOOL_C_AST_NONE ||
      function_binding == CTOOL_C_AST_NONE ||
      unqualified_binding == CTOOL_C_AST_NONE ||
      unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 3u || ir->functions == NULL ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "qualified array decay inventory differs\n");
    return 0;
  }
  instructions = ir->instructions;
  array_type = unit->bindings[array_binding].type;
  pointer_type = instructions[1].type;
  unqualified_pointer_type = unit->bindings[unqualified_binding].type;
  if (!array_decay_query_matches(
          job, unit, array_type, pointer_type, CTOOL_TRUE) ||
      !array_decay_query_matches(
          job, unit, array_type, unqualified_pointer_type, CTOOL_FALSE) ||
      !array_decay_query_matches(
          job, NULL, array_type, pointer_type, CTOOL_FALSE) ||
      ir->functions[0].binding != function_binding ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 3u ||
      ir->functions[0].maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != array_type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[0].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[0].reference != array_binding ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER ||
      instructions[1].input_type != array_type ||
      instructions[1].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[1].conversion != CTOOL_C_CONVERSION_ARRAY_TO_POINTER ||
      instructions[1].reference != CTOOL_C_AST_NONE ||
      instructions[1].integer_bits != 0u ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[2].input_type != pointer_type ||
      instructions[2].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[2].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[2].reference != CTOOL_C_AST_NONE ||
      instructions[2].integer_bits != 0u) {
    (void)fprintf(stderr, "qualified array decay IR differs\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (!string_equal(instructions[index].location.path,
                      "/qualified-array-decay.c") ||
        !string_equal(instructions[index].physical_location.path,
                      "/qualified-array-decay.c") ||
        instructions[index].location.line != 4u ||
        instructions[index].physical_location.line != 4u) {
      return 0;
    }
  }
  return 1;
}

static int validate_qualified_pointer_update_ir(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 cursor_binding = find_binding(unit, "cursor");
  ctool_u32 function_binding = find_binding(unit, "advance_volatile");
  ctool_u32 object_type;
  ctool_u32 value_type;
  ctool_u32 integer_type;
  ctool_u32 index;
  if (cursor_binding == CTOOL_C_AST_NONE ||
      function_binding == CTOOL_C_AST_NONE ||
      unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 9u || ir->functions == NULL ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "qualified pointer update inventory differs\n");
    return 0;
  }
  instructions = ir->instructions;
  object_type = unit->bindings[cursor_binding].type;
  value_type = instructions[2].type;
  integer_type = instructions[3].type;
  if (!pointer_value_query_matches(
          job, unit, object_type, value_type, CTOOL_TRUE) ||
      ir->functions[0].binding != function_binding ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 9u ||
      ir->functions[0].maximum_stack_depth != 3u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != object_type ||
      instructions[0].reference != cursor_binding ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS ||
      instructions[1].type != object_type ||
      instructions[1].input_type != object_type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[2].input_type != object_type ||
      instructions[2].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[3].input_type != CTOOL_C_TYPE_NONE ||
      instructions[3].integer_bits != 1u ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_POINTER_BINARY ||
      instructions[4].type != value_type ||
      instructions[4].input_type != value_type ||
      instructions[4].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[4].reference != integer_type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[5].type != value_type ||
      instructions[5].input_type != value_type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[6].type != integer_type ||
      instructions[6].integer_bits != 1u ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_POINTER_BINARY ||
      instructions[7].type != value_type ||
      instructions[7].input_type != value_type ||
      instructions[7].operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
      instructions[7].reference != integer_type ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[8].input_type != value_type) {
    (void)fprintf(stderr, "qualified pointer update IR differs\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (!string_equal(instructions[index].location.path,
                      "/qualified-pointer-update.c") ||
        !string_equal(instructions[index].physical_location.path,
                      "/qualified-pointer-update.c") ||
        instructions[index].location.line != 2u ||
        instructions[index].physical_location.line != 2u) {
      return 0;
    }
  }
  return 1;
}

static int run_pointer_arithmetic(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t atomic_unit;
  ctool_c_translation_unit_t wide_unit;
  ctool_c_translation_unit_t qualified_unit;
  ctool_c_translation_unit_t qualified_pointer_unit;
  ctool_c_translation_unit_t invalid_decay_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_ir_unit_t ir;
  ctool_c_ir_unit_t qualified_ir;
  ctool_c_ir_unit_t qualified_pointer_ir;
  ctool_c_expression_t *invalid_decay_expressions = NULL;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_u32 assignment_expression = CTOOL_C_AST_NONE;
  ctool_u32 decay_expression = CTOOL_C_AST_NONE;
  ctool_u32 unqualified_pointer_binding = CTOOL_C_AST_NONE;
  ctool_u32 index;
  ctool_status_t status;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&atomic_unit, 0, sizeof(atomic_unit));
  (void)memset(&wide_unit, 0, sizeof(wide_unit));
  (void)memset(&qualified_unit, 0, sizeof(qualified_unit));
  (void)memset(&qualified_pointer_unit, 0,
               sizeof(qualified_pointer_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_is_unchanged(job) ||
      !parse_source(job, "/pointer-arithmetic.c", pointer_arithmetic_source,
                    &unit)) {
    goto cleanup;
  }
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "object-pointer arithmetic lowering") ||
      !validate_pointer_arithmetic_ir(job, &unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  (void)memset(&qualified_ir, 0xa5, sizeof(qualified_ir));
  if (!parse_source(job, "/qualified-array-decay.c",
                    qualified_array_decay_source, &qualified_unit)) {
    goto cleanup;
  }
  status = ctool_c_lower_ir(job, &qualified_unit, &qualified_ir);
  if (!check_status(status, CTOOL_OK, "qualified array decay lowering") ||
      !validate_qualified_array_decay_ir(
          job, &qualified_unit, &qualified_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < qualified_unit.expression_count; index++) {
    if (qualified_unit.expressions[index].kind ==
            CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION &&
        qualified_unit.expressions[index].conversion ==
            CTOOL_C_CONVERSION_ARRAY_TO_POINTER) {
      decay_expression = index;
      break;
    }
  }
  unqualified_pointer_binding =
      find_binding(&qualified_unit, "unqualified_pointer");
  if (decay_expression == CTOOL_C_AST_NONE ||
      unqualified_pointer_binding == CTOOL_C_AST_NONE ||
      qualified_unit.expression_count == 0u ||
      sizeof(*invalid_decay_expressions) >
          SIZE_MAX / (size_t)qualified_unit.expression_count) {
    (void)fprintf(stderr, "array decay rejection fixture differs\n");
    goto cleanup;
  }
  invalid_decay_expressions = (ctool_c_expression_t *)malloc(
      (size_t)qualified_unit.expression_count *
      sizeof(*invalid_decay_expressions));
  if (invalid_decay_expressions == NULL) {
    (void)fprintf(stderr, "array decay rejection allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_decay_expressions, qualified_unit.expressions,
               (size_t)qualified_unit.expression_count *
                   sizeof(*invalid_decay_expressions));
  invalid_decay_expressions[decay_expression].type =
      qualified_unit.bindings[unqualified_pointer_binding].type;
  invalid_decay_unit = qualified_unit;
  invalid_decay_unit.expressions = invalid_decay_expressions;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_decay_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_CONVERSION,
          "CupidC IR lowering does not yet support this conversion",
          "qualified array decay target")) {
    goto cleanup;
  }
  (void)memset(&qualified_pointer_ir, 0xa5,
               sizeof(qualified_pointer_ir));
  if (!parse_source(job, "/qualified-pointer-update.c",
                    qualified_pointer_update_source,
                    &qualified_pointer_unit)) {
    goto cleanup;
  }
  status = ctool_c_lower_ir(job, &qualified_pointer_unit,
                            &qualified_pointer_ir);
  if (!check_status(status, CTOOL_OK, "qualified pointer update lowering") ||
      !validate_qualified_pointer_update_ir(
          job, &qualified_pointer_unit, &qualified_pointer_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/atomic-pointer-update.c",
                    atomic_pointer_update_source, &atomic_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &atomic_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic pointer update") ||
      !parse_source(job, "/wide-pointer-offset.c",
                    wide_pointer_offset_source, &wide_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &wide_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide pointer offset")) {
    goto cleanup;
  }
  for (index = 0u; index < unit.expression_count; index++) {
    if (unit.expressions[index].kind == CTOOL_C_EXPRESSION_ASSIGNMENT &&
        unit.expressions[index].operation ==
            CTOOL_C_EXPRESSION_OPERATOR_ADD_ASSIGN) {
      assignment_expression = index;
      break;
    }
  }
  if (assignment_expression == CTOOL_C_AST_NONE ||
      unit.expression_count == 0u ||
      sizeof(*invalid_expressions) >
          SIZE_MAX / (size_t)unit.expression_count) {
    (void)fprintf(stderr, "pointer mutation rejection fixture differs\n");
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)unit.expression_count * sizeof(*invalid_expressions));
  if (invalid_expressions == NULL) {
    (void)fprintf(stderr, "pointer mutation rejection allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_expressions[assignment_expression].operation =
      CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY_ASSIGN;
  invalid_unit = unit;
  invalid_unit.expressions = invalid_expressions;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "malformed pointer compound assignment")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_decay_expressions);
  free(invalid_expressions);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("pointer-arithmetic: ok");
    return 0;
  }
  return 1;
}

static int validate_automatic_object_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  static const ctool_c_ir_instruction_kind_t expected_kinds[] = {
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_POINTER_BINARY,
      CTOOL_C_IR_INSTRUCTION_DEREFERENCE,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_STORE_VALUE,
      CTOOL_C_IR_INSTRUCTION_DISCARD,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_POINTER_BINARY,
      CTOOL_C_IR_INSTRUCTION_DEREFERENCE,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_STORE_VALUE,
      CTOOL_C_IR_INSTRUCTION_DISCARD,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER,
      CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
      CTOOL_C_IR_INSTRUCTION_RETURN_VOID,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER,
      CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
      CTOOL_C_IR_INSTRUCTION_RETURN_VOID,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_POINTER_BINARY,
      CTOOL_C_IR_INSTRUCTION_DEREFERENCE,
      CTOOL_C_IR_INSTRUCTION_ADDRESS_OF,
      CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
      CTOOL_C_IR_INSTRUCTION_RETURN_VOID};
  static const char *const function_names[] = {
      "automatic_array", "automatic_record", "automatic_bytes",
      "automatic_mixed", "automatic_children"};
  static const ctool_u32 function_counts[] = {18u, 10u, 4u, 6u, 9u};
  static const ctool_u32 stack_depths[] = {2u, 2u, 1u, 2u, 2u};
  ctool_u32 array_binding = find_block_binding(unit, "section_map");
  ctool_u32 record_binding = find_block_binding(unit, "pair");
  ctool_u32 bytes_binding = find_block_binding(unit, "bytes");
  ctool_u32 padding_binding = find_block_binding(unit, "padding_bytes");
  ctool_u32 words_binding = find_block_binding(unit, "words");
  ctool_u32 children_binding = find_block_binding(unit, "children");
  ctool_u32 right_member = find_member(unit, "right");
  ctool_u32 consume_binding = find_binding(unit, "consume_bytes");
  ctool_u32 consume_layout_binding = find_binding(unit, "consume_layout");
  ctool_u32 consume_child_binding = find_binding(unit, "consume_child");
  ctool_u32 first = 0u;
  ctool_u32 index;
  if (array_binding == CTOOL_C_AST_NONE ||
      record_binding == CTOOL_C_AST_NONE ||
      bytes_binding == CTOOL_C_AST_NONE || right_member == CTOOL_C_AST_NONE ||
      padding_binding == CTOOL_C_AST_NONE ||
      words_binding == CTOOL_C_AST_NONE ||
      children_binding == CTOOL_C_AST_NONE ||
      consume_binding == CTOOL_C_AST_NONE ||
      consume_layout_binding == CTOOL_C_AST_NONE ||
      consume_child_binding == CTOOL_C_AST_NONE ||
      ir->function_count != 5u ||
      ir->functions == NULL || ir->instructions == NULL) {
    return 0;
  }
  if (unit->block_bindings[array_binding].type >=
          unit->layout.type_count ||
      unit->block_bindings[record_binding].type >=
          unit->layout.type_count ||
      unit->block_bindings[bytes_binding].type >= unit->layout.type_count ||
      unit->block_bindings[padding_binding].type >=
          unit->layout.type_count ||
      unit->block_bindings[words_binding].type >= unit->layout.type_count ||
      unit->block_bindings[children_binding].type >=
          unit->layout.type_count ||
      unit->layout.types[unit->block_bindings[array_binding].type].size !=
          16u ||
      unit->layout.types[unit->block_bindings[array_binding].type]
              .alignment != 4u ||
      unit->layout.types[unit->block_bindings[record_binding].type].size !=
          8u ||
      unit->layout.types[unit->block_bindings[record_binding].type]
              .alignment != 4u ||
      unit->layout.types[unit->block_bindings[bytes_binding].type].size != 3u ||
      unit->layout.types[unit->block_bindings[bytes_binding].type]
              .alignment != 1u ||
      unit->layout.types[unit->block_bindings[padding_binding].type].size !=
          3u ||
      unit->layout.types[unit->block_bindings[padding_binding].type]
              .alignment != 1u ||
      unit->layout.types[unit->block_bindings[words_binding].type].size != 8u ||
      unit->layout.types[unit->block_bindings[words_binding].type]
              .alignment != 4u ||
      unit->layout.types[unit->block_bindings[children_binding].type].size !=
          12u ||
      unit->layout.types[unit->block_bindings[children_binding].type]
              .alignment != 4u ||
      right_member >= unit->layout.member_count ||
      unit->layout.members[right_member].byte_offset != 4u ||
      unit->layout.members[right_member].size != 4u ||
      unit->layout.members[right_member].alignment != 4u) {
    (void)fprintf(stderr, "automatic object target layouts differ\n");
    return 0;
  }
  if (ir->instruction_count !=
      (ctool_u32)(sizeof(expected_kinds) / sizeof(expected_kinds[0]))) {
    (void)fprintf(stderr, "automatic object IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < ir->function_count; index++) {
    ctool_u32 function_binding = find_binding(unit, function_names[index]);
    const ctool_c_ir_function_t *function = &ir->functions[index];
    if (function_binding == CTOOL_C_AST_NONE ||
        function->binding != function_binding ||
        function->first_instruction != first ||
        function->instruction_count != function_counts[index] ||
        function->maximum_stack_depth != stack_depths[index]) {
      (void)fprintf(stderr, "automatic object function %u differs\n",
                    (unsigned int)index);
      return 0;
    }
    first += function_counts[index];
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (ir->instructions[index].kind != expected_kinds[index]) {
      (void)fprintf(stderr, "automatic object IR instruction %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  if (ir->instructions[0].reference != array_binding ||
      ir->instructions[10].reference != array_binding ||
      ir->instructions[1].input_type !=
          unit->block_bindings[array_binding].type ||
      ir->instructions[11].input_type !=
          unit->block_bindings[array_binding].type ||
      ir->instructions[18].reference != record_binding ||
      ir->instructions[24].reference != record_binding ||
      ir->instructions[19].reference != right_member ||
      ir->instructions[25].reference != right_member ||
      ir->instructions[28].reference != bytes_binding ||
      ir->instructions[29].input_type !=
          unit->block_bindings[bytes_binding].type ||
      ir->instructions[30].reference != consume_binding ||
      ir->instructions[32].reference != padding_binding ||
      ir->instructions[33].input_type !=
          unit->block_bindings[padding_binding].type ||
      ir->instructions[34].reference != words_binding ||
      ir->instructions[35].input_type !=
          unit->block_bindings[words_binding].type ||
      ir->instructions[36].reference != consume_layout_binding ||
      ir->instructions[38].reference != children_binding ||
      ir->instructions[39].input_type !=
          unit->block_bindings[children_binding].type ||
      ir->instructions[45].reference != consume_child_binding) {
    (void)fprintf(stderr, "automatic object address identity differs\n");
    return 0;
  }
  return 1;
}

static int run_automatic_objects(const char *host_root) {
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
  static const char volatile_initialized_source[] =
      "typedef unsigned int ctool_u32;\n"
      "ctool_u32 volatile_initialized_array(void) {\n"
      "  volatile ctool_u32 values[2] = {1u, 2u};\n"
      "  return 0u;\n"
      "}\n";
  static const char aligned_source[] =
      "typedef struct { unsigned int value; } aligned_record "
      "__attribute__((aligned(8)));\n"
      "unsigned int aligned_automatic(void) {\n"
      "  aligned_record value;\n"
      "  return 0u;\n"
      "}\n";
  static const char aggregate_assignment_source[] =
      "typedef struct { unsigned int value; } record_t;\n"
      "void aggregate_assignment(void) {\n"
      "  record_t left;\n"
      "  record_t right;\n"
      "  left = right;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t volatile_initialized_unit;
  ctool_c_translation_unit_t aligned_unit;
  ctool_c_translation_unit_t aggregate_assignment_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_type_layout_t *invalid_layouts = NULL;
  ctool_c_ir_unit_t ir;
  ctool_c_ir_unit_t aggregate_assignment_ir;
  ctool_u64 fingerprint;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&volatile_initialized_unit, 0,
               sizeof(volatile_initialized_unit));
  (void)memset(&aligned_unit, 0, sizeof(aligned_unit));
  (void)memset(&aggregate_assignment_unit, 0,
               sizeof(aggregate_assignment_unit));
  (void)memset(&aggregate_assignment_ir, 0xa5,
               sizeof(aggregate_assignment_ir));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_is_unchanged(job) ||
      !parse_source(job, "/automatic-objects.c", source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "automatic object lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_automatic_object_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  invalid_layouts = (ctool_c_type_layout_t *)malloc(
      (size_t)unit.layout.type_count * sizeof(*invalid_layouts));
  if (invalid_layouts == NULL ||
      find_block_binding(&unit, "section_map") == CTOOL_C_AST_NONE) {
    goto cleanup;
  }
  (void)memcpy(invalid_layouts, unit.layout.types,
               (size_t)unit.layout.type_count * sizeof(*invalid_layouts));
  invalid_layouts[unit.block_bindings[
      find_block_binding(&unit, "section_map")].type].size = 0u;
  invalid_unit = unit;
  invalid_unit.layout.types = invalid_layouts;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "zero-sized automatic object layout")) {
    goto cleanup;
  }
  if (!parse_source(job, "/volatile-initialized-automatic-array.c",
                    volatile_initialized_source,
                    &volatile_initialized_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &volatile_initialized_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "volatile initialized automatic array") ||
      !parse_source_mode(job, "/aligned-automatic-record.c", aligned_source,
                         CTOOL_TRUE, &aligned_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &aligned_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "over-aligned automatic record") ||
      !parse_source(job, "/automatic-aggregate-assignment.c",
                    aggregate_assignment_source,
                    &aggregate_assignment_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&aggregate_assignment_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &aggregate_assignment_unit,
                            &aggregate_assignment_ir);
  if (!check_status(status, CTOOL_OK, "automatic aggregate assignment") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&aggregate_assignment_unit) != fingerprint ||
      aggregate_assignment_ir.function_count != 1u ||
      aggregate_assignment_ir.instruction_count != 6u ||
      aggregate_assignment_ir.instructions == NULL ||
      aggregate_assignment_ir.instructions[0].kind !=
          CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      aggregate_assignment_ir.instructions[1].kind !=
          CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      aggregate_assignment_ir.instructions[2].kind !=
          CTOOL_C_IR_INSTRUCTION_LOAD ||
      aggregate_assignment_ir.instructions[3].kind !=
          CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      aggregate_assignment_ir.instructions[4].kind !=
          CTOOL_C_IR_INSTRUCTION_DISCARD ||
      aggregate_assignment_ir.instructions[5].kind !=
          CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)ctool_job_render_diagnostics(job);
    (void)fprintf(stderr, "automatic aggregate assignment IR differs\n");
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_layouts);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("automatic-objects: ok");
    return 0;
  }
  return 1;
}

static int aggregate_ir_location_matches(
    const ctool_c_ir_instruction_t *instruction, ctool_u32 line,
    ctool_u32 column) {
  return string_equal(instruction->location.path,
                      "/aggregate-initializers.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/aggregate-initializers.c") != 0 &&
                 instruction->location.line == line &&
                 instruction->location.column == column &&
                 instruction->physical_location.line == line &&
                 instruction->physical_location.column == column
             ? 1
             : 0;
}

static int validate_aggregate_initializer_ir(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  static const ctool_c_ir_instruction_kind_t expected_kinds[] = {
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_INTEGER,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_STORE,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_INTEGER,
      CTOOL_C_IR_INSTRUCTION_STORE,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ELEMENT_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ELEMENT_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_STORE,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_STORE,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_BINARY,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER,
      CTOOL_C_IR_INSTRUCTION_INTEGER,
      CTOOL_C_IR_INSTRUCTION_POINTER_BINARY,
      CTOOL_C_IR_INSTRUCTION_DEREFERENCE,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER,
      CTOOL_C_IR_INSTRUCTION_INTEGER,
      CTOOL_C_IR_INSTRUCTION_POINTER_BINARY,
      CTOOL_C_IR_INSTRUCTION_DEREFERENCE,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_BINARY,
      CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER,
      CTOOL_C_IR_INSTRUCTION_INTEGER,
      CTOOL_C_IR_INSTRUCTION_POINTER_BINARY,
      CTOOL_C_IR_INSTRUCTION_DEREFERENCE,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER,
      CTOOL_C_IR_INSTRUCTION_INTEGER,
      CTOOL_C_IR_INSTRUCTION_POINTER_BINARY,
      CTOOL_C_IR_INSTRUCTION_DEREFERENCE,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_CONVERT,
      CTOOL_C_IR_INSTRUCTION_BINARY,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE};
  static const ctool_u32 initializer_lines[] = {
      7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 7u, 8u, 8u, 9u,
      9u, 9u, 9u, 9u, 9u, 9u, 9u, 9u, 10u, 10u, 10u, 10u, 10u};
  static const ctool_u32 initializer_columns[] = {
      28u, 28u, 29u, 29u, 43u, 29u, 29u, 46u, 46u,
      46u, 46u, 15u, 15u, 41u, 41u, 41u, 41u, 41u,
      41u, 41u, 41u, 41u, 17u, 17u, 17u, 17u, 17u};
  const ctool_c_ir_instruction_t *instructions = ir->instructions;
  ctool_u32 function_binding = find_binding(unit, "aggregate_initializers");
  ctool_u32 no_name_binding = find_block_binding(unit, "no_name");
  ctool_u32 map_binding = find_block_binding(unit, "map");
  ctool_u32 data_member = find_member(unit, "data");
  ctool_u32 size_member = find_member(unit, "size");
  ctool_u32 columns_member = find_member(unit, "columns");
  ctool_u32 marker_member = find_member(unit, "marker");
  ctool_u32 rows_member = find_member(unit, "rows");
  ctool_u32 no_name_type;
  ctool_u32 map_type;
  ctool_u32 data_type;
  ctool_u32 size_type;
  ctool_u32 rows_type;
  ctool_u32 row_type;
  ctool_u32 columns_type;
  ctool_u32 element_type;
  ctool_u32 seed_type;
  ctool_u32 index;
  ctool_bool pointer_compatible = CTOOL_FALSE;
  if (function_binding == CTOOL_C_AST_NONE ||
      no_name_binding == CTOOL_C_AST_NONE ||
      map_binding == CTOOL_C_AST_NONE || data_member == CTOOL_C_AST_NONE ||
      size_member == CTOOL_C_AST_NONE ||
      columns_member == CTOOL_C_AST_NONE ||
      marker_member == CTOOL_C_AST_NONE || rows_member == CTOOL_C_AST_NONE ||
      ir->function_count != 1u || ir->functions == NULL ||
      ir->instructions == NULL ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected_kinds) / sizeof(expected_kinds[0])) ||
      ir->functions[0].binding != function_binding ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != ir->instruction_count ||
      ir->functions[0].maximum_stack_depth != 3u) {
    (void)fprintf(stderr, "aggregate initializer IR inventory differs\n");
    return 0;
  }
  no_name_type = unit->block_bindings[no_name_binding].type;
  map_type = unit->block_bindings[map_binding].type;
  data_type = unit->graph.members[data_member].type;
  size_type = unit->graph.members[size_member].type;
  rows_type = unit->graph.members[rows_member].type;
  columns_type = unit->graph.members[columns_member].type;
  if (rows_type >= unit->graph.type_count ||
      columns_type >= unit->graph.type_count ||
      unit->graph.types[rows_type].kind != CTOOL_C_TYPE_ARRAY ||
      unit->graph.types[columns_type].kind != CTOOL_C_TYPE_ARRAY ||
      unit->function_definition_count != 1u) {
    (void)fprintf(stderr, "aggregate initializer type graph differs\n");
    return 0;
  }
  row_type = unit->graph.types[rows_type].referenced_type;
  element_type = unit->graph.types[columns_type].referenced_type;
  if (unit->function_definitions[0].binding != function_binding ||
      unit->function_definitions[0].declared_type >= unit->graph.type_count ||
      unit->graph.types[unit->function_definitions[0].declared_type]
              .first_parameter >= unit->graph.parameter_type_count) {
    (void)fprintf(stderr, "aggregate initializer parameter graph differs\n");
    return 0;
  }
  seed_type = unit->graph.parameter_types
      [unit->graph.types[unit->function_definitions[0].declared_type]
           .first_parameter];
  for (index = 0u; index < ir->instruction_count; index++) {
    if (instructions[index].kind != expected_kinds[index]) {
      (void)fprintf(stderr,
                    "aggregate initializer instruction %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(initializer_lines) /
                           sizeof(initializer_lines[0]));
       index++) {
    if (!aggregate_ir_location_matches(&instructions[index],
                                       initializer_lines[index],
                                       initializer_columns[index])) {
      (void)fprintf(stderr,
                    "aggregate initializer location %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  if (instructions[0].type != no_name_type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].reference != no_name_binding ||
      instructions[1].type != no_name_type ||
      instructions[1].input_type != no_name_type ||
      instructions[1].reference != CTOOL_C_AST_NONE ||
      instructions[2].type != no_name_type ||
      instructions[2].reference != no_name_binding ||
      instructions[3].type != data_type ||
      instructions[3].input_type != no_name_type ||
      instructions[3].reference != data_member ||
      instructions[4].integer_bits != 0u ||
      instructions[5].input_type != instructions[4].type ||
      instructions[5].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[6].type != data_type ||
      instructions[6].input_type != instructions[5].type ||
      instructions[7].type != no_name_type ||
      instructions[7].reference != no_name_binding ||
      instructions[8].type != size_type ||
      instructions[8].input_type != no_name_type ||
      instructions[8].reference != size_member ||
      instructions[9].type != size_type ||
      instructions[9].integer_bits != 0u ||
      instructions[10].type != size_type ||
      instructions[10].input_type != size_type ||
      instructions[11].type != map_type ||
      instructions[11].reference != map_binding ||
      instructions[12].type != map_type ||
      instructions[12].input_type != map_type ||
      instructions[12].reference != CTOOL_C_AST_NONE ||
      instructions[13].type != map_type ||
      instructions[13].reference != map_binding ||
      instructions[14].type != rows_type ||
      instructions[14].input_type != map_type ||
      instructions[14].reference != rows_member ||
      instructions[15].type != row_type ||
      instructions[15].input_type != rows_type ||
      instructions[15].reference != 1u ||
      instructions[16].type != columns_type ||
      instructions[16].input_type != row_type ||
      instructions[16].reference != columns_member ||
      instructions[17].type != element_type ||
      instructions[17].input_type != columns_type ||
      instructions[17].reference != 2u ||
      instructions[18].type != seed_type ||
      instructions[18].reference != 0u ||
      instructions[19].type != seed_type ||
      instructions[19].input_type != seed_type ||
      instructions[19].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[20].type != element_type ||
      instructions[20].input_type != seed_type ||
      instructions[20].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[21].type != element_type ||
      instructions[21].input_type != element_type ||
      instructions[22].type != map_type ||
      instructions[22].reference != map_binding ||
      instructions[23].type != size_type ||
      instructions[23].input_type != map_type ||
      instructions[23].reference != marker_member ||
      instructions[24].type != seed_type ||
      instructions[24].reference != 0u ||
      instructions[25].type != seed_type ||
      instructions[25].input_type != seed_type ||
      instructions[25].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[26].type != size_type ||
      instructions[26].input_type != seed_type) {
    (void)fprintf(stderr,
                  "aggregate initializer address or value identity differs\n");
    return 0;
  }
  if (ctool_c_ir_pointer_value_types_compatible(
          job, unit, instructions[6].type, instructions[6].input_type,
          &pointer_compatible) != CTOOL_OK ||
      pointer_compatible != CTOOL_TRUE) {
    (void)fprintf(stderr,
                  "aggregate initializer pointer leaf identity differs\n");
    return 0;
  }
  for (index = 0u; index <= 26u; index++) {
    ctool_c_conversion_kind_t expected_conversion = CTOOL_C_CONVERSION_NONE;
    if (index == 19u || index == 25u) {
      expected_conversion = CTOOL_C_CONVERSION_LVALUE_TO_VALUE;
    } else if (index == 20u) {
      expected_conversion = CTOOL_C_CONVERSION_ASSIGNMENT;
    }
    if (instructions[index].operation !=
            CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        instructions[index].conversion != expected_conversion ||
        instructions[index].integer_bits != 0u) {
      (void)fprintf(stderr,
                    "aggregate initializer payload %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  return 1;
}

static int validate_block_extern_ir(const ctool_c_translation_unit_t *unit,
                                    const ctool_c_ir_unit_t *ir) {
  ctool_u32 block_index;
  const ctool_c_block_binding_t *block;
  ctool_u32 linked_index;
  const ctool_c_binding_t *linked;
  ctool_u32 array_linked_index = CTOOL_C_AST_NONE;
  ctool_u32 array_declarations = 0u;
  ctool_u32 file_addresses = 0u;
  ctool_u32 scalar_file_addresses = 0u;
  ctool_u32 array_file_addresses = 0u;
  ctool_u32 local_addresses = 0u;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 index;
  if (unit == NULL || ir == NULL ||
      (unit->binding_count != 0u && unit->bindings == NULL) ||
      (unit->function_definition_count != 0u &&
       unit->function_definitions == NULL) ||
      (unit->block_binding_count != 0u && unit->block_bindings == NULL) ||
      (ir->function_count != 0u && ir->functions == NULL) ||
      (ir->instruction_count != 0u && ir->instructions == NULL)) {
    return 0;
  }
  block_index = find_block_binding(unit, "external_value");
  block = block_index == CTOOL_C_AST_NONE
              ? NULL
              : &unit->block_bindings[block_index];
  linked_index =
      block == NULL ? CTOOL_C_AST_NONE : block->linkage_binding;
  linked = linked_index >= unit->binding_count
               ? NULL
               : &unit->bindings[linked_index];
  for (index = 0u; index < unit->block_binding_count; index++) {
    const ctool_c_block_binding_t *candidate = &unit->block_bindings[index];
    if (string_equal(candidate->name, "external_values") == 0) {
      continue;
    }
    if (candidate->storage != CTOOL_C_STORAGE_EXTERN ||
        candidate->initializer != CTOOL_C_AST_NONE ||
        candidate->linkage_binding >= unit->binding_count ||
        (array_linked_index != CTOOL_C_AST_NONE &&
         candidate->linkage_binding != array_linked_index)) {
      return 0;
    }
    array_linked_index = candidate->linkage_binding;
    array_declarations++;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (ir->instructions[index].kind ==
        CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS) {
      file_addresses++;
      if (ir->instructions[index].reference == linked_index) {
        scalar_file_addresses++;
      }
      if (ir->instructions[index].reference == array_linked_index) {
        array_file_addresses++;
      }
    }
    if (ir->instructions[index].kind ==
        CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS) {
      local_addresses++;
    }
  }
  if (block == NULL || linked == NULL ||
      block->storage != CTOOL_C_STORAGE_EXTERN ||
      block->initializer != CTOOL_C_AST_NONE ||
      linked->kind != CTOOL_C_BINDING_OBJECT ||
      linked->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
      linked->file_scope_visible != CTOOL_FALSE ||
      array_declarations != 2u ||
      array_linked_index == CTOOL_C_AST_NONE ||
      unit->bindings[array_linked_index].kind != CTOOL_C_BINDING_OBJECT ||
      unit->bindings[array_linked_index].linkage !=
          CTOOL_C_LINKAGE_EXTERNAL ||
      unit->bindings[array_linked_index].file_scope_visible != CTOOL_FALSE ||
      unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->functions == NULL || ir->instruction_count < 6u ||
      ir->instructions == NULL || file_addresses != 2u ||
      scalar_file_addresses != 1u || array_file_addresses != 1u ||
      local_addresses != 0u) {
    (void)fprintf(stderr, "block extern IR inventory differs\n");
    return 0;
  }
  function = &ir->functions[0];
  instructions = ir->instructions + function->first_instruction;
  if (function->binding != unit->function_definitions[0].binding ||
      function->declared_type !=
          unit->function_definitions[0].declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 3u ||
      function->maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != block->type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].reference != linked_index ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != block->type ||
      instructions[1].input_type != block->type ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[2].type != block->type ||
      instructions[2].input_type != block->type) {
    (void)fprintf(stderr, "block extern IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int run_block_externs(const char *host_root) {
  static const char source[] =
      "int read_external(void) {\n"
      "  extern int external_value;\n"
      "  return external_value;\n"
      "}\n"
      "int read_external_array(void) {\n"
      "  extern int external_values[];\n"
      "  extern int external_values[2];\n"
      "  return external_values[1];\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_block_binding_t *invalid_blocks = NULL;
  ctool_c_binding_t *invalid_bindings = NULL;
  ctool_c_ir_unit_t ir;
  ctool_u64 fingerprint;
  ctool_u32 block_index;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/block-extern-ir.c", source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "block extern lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_block_extern_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  block_index = find_block_binding(&unit, "external_value");
  invalid_blocks = (ctool_c_block_binding_t *)malloc(
      (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  if (block_index == CTOOL_C_AST_NONE || invalid_blocks == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_blocks[block_index].linkage_binding = CTOOL_C_AST_NONE;
  invalid_unit = unit;
  invalid_unit.block_bindings = invalid_blocks;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block extern missing linked identity")) {
    goto cleanup;
  }
  invalid_bindings = (ctool_c_binding_t *)malloc(
      (size_t)unit.binding_count * sizeof(*invalid_bindings));
  if (invalid_bindings == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_bindings, unit.bindings,
               (size_t)unit.binding_count * sizeof(*invalid_bindings));
  invalid_bindings[unit.block_bindings[block_index].linkage_binding].kind =
      CTOOL_C_BINDING_TYPEDEF;
  invalid_unit = unit;
  invalid_unit.bindings = invalid_bindings;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block extern linked to non-object binding")) {
    goto cleanup;
  }
  (void)memcpy(invalid_bindings, unit.bindings,
               (size_t)unit.binding_count * sizeof(*invalid_bindings));
  invalid_bindings[unit.block_bindings[block_index].linkage_binding].name =
      ctool_string("wrong_external");
  invalid_unit = unit;
  invalid_unit.bindings = invalid_bindings;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block extern linked to different name")) {
    goto cleanup;
  }
  (void)memcpy(invalid_bindings, unit.bindings,
               (size_t)unit.binding_count * sizeof(*invalid_bindings));
  invalid_bindings[unit.block_bindings[block_index].linkage_binding].storage =
      CTOOL_C_STORAGE_STATIC;
  invalid_unit = unit;
  invalid_unit.bindings = invalid_bindings;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block extern linked to impossible storage and linkage")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_bindings);
  free(invalid_blocks);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("block-externs: ok");
    return 0;
  }
  return 1;
}

static int validate_block_function_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  ctool_u32 external_block = find_block_binding(unit, "external_helper");
  ctool_u32 local_block = find_block_binding(unit, "local_helper");
  ctool_u32 external_linked =
      external_block < unit->block_binding_count
          ? unit->block_bindings[external_block].linkage_binding
          : CTOOL_C_AST_NONE;
  ctool_u32 local_linked =
      local_block < unit->block_binding_count
          ? unit->block_bindings[local_block].linkage_binding
          : CTOOL_C_AST_NONE;
  ctool_u32 external_calls = 0u;
  ctool_u32 local_calls = 0u;
  ctool_u32 external_addresses = 0u;
  ctool_u32 local_addresses = 0u;
  ctool_u32 index;
  if (external_block >= unit->block_binding_count ||
      local_block >= unit->block_binding_count ||
      external_linked >= unit->binding_count ||
      local_linked >= unit->binding_count ||
      unit->block_bindings[external_block].kind !=
          CTOOL_C_BINDING_FUNCTION ||
      unit->block_bindings[external_block].storage != CTOOL_C_STORAGE_NONE ||
      unit->block_bindings[local_block].kind != CTOOL_C_BINDING_FUNCTION ||
      unit->block_bindings[local_block].storage != CTOOL_C_STORAGE_EXTERN ||
      unit->bindings[external_linked].kind != CTOOL_C_BINDING_FUNCTION ||
      unit->bindings[external_linked].linkage != CTOOL_C_LINKAGE_EXTERNAL ||
      unit->bindings[external_linked].file_scope_visible != CTOOL_FALSE ||
      unit->bindings[local_linked].kind != CTOOL_C_BINDING_FUNCTION ||
      unit->bindings[local_linked].linkage != CTOOL_C_LINKAGE_INTERNAL ||
      unit->bindings[local_linked].file_scope_visible != CTOOL_TRUE ||
      unit->function_definition_count != 5u || ir->function_count != 5u ||
      ir->instruction_count != 18u) {
    (void)fprintf(stderr, "block function IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction = &ir->instructions[index];
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT) {
      if (instruction->reference == external_linked) {
        external_calls++;
      }
      if (instruction->reference == local_linked) {
        local_calls++;
      }
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS) {
      local_addresses++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_FUNCTION_ADDRESS &&
        instruction->reference == external_linked) {
      external_addresses++;
    }
  }
  if (external_calls != 2u || local_calls != 1u ||
      external_addresses != 1u || local_addresses != 0u ||
      ir->functions[0].instruction_count != 4u ||
      ir->functions[0].maximum_stack_depth != 1u ||
      ir->functions[1].instruction_count != 5u ||
      ir->functions[1].maximum_stack_depth != 2u ||
      ir->functions[2].instruction_count != 4u ||
      ir->functions[2].maximum_stack_depth != 1u ||
      ir->functions[3].instruction_count != 3u ||
      ir->functions[3].maximum_stack_depth != 1u ||
      ir->functions[4].instruction_count != 2u ||
      ir->functions[4].maximum_stack_depth != 1u) {
    (void)fprintf(stderr, "block function IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int run_block_functions(const char *host_root) {
  static const char source[] =
      "int invoke_external(int value) {\n"
      "  int external_helper(int);\n"
      "  int unused_helper(int);\n"
      "  return external_helper(value);\n"
      "}\n"
      "static int local_helper(int value) { return value + 1; }\n"
      "int invoke_local(int value) {\n"
      "  extern int local_helper(int);\n"
      "  return local_helper(value);\n"
      "}\n"
      "int (*address_external(void))() {\n"
      "  int external_helper();\n"
      "  return external_helper;\n"
      "}\n"
      "int invoke_old_style(void) {\n"
      "  int external_helper();\n"
      "  return external_helper();\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_block_binding_t *invalid_blocks = NULL;
  ctool_c_binding_t *invalid_bindings = NULL;
  ctool_c_ir_unit_t ir;
  ctool_u64 fingerprint;
  ctool_u32 block_index;
  ctool_u32 linked_index;
  ctool_u32 unused_block_index;
  ctool_u32 unused_linked_index;
  ctool_u32 old_style_block_index = CTOOL_C_AST_NONE;
  ctool_u32 signed_int_type = CTOOL_C_TYPE_NONE;
  ctool_bool compatible = CTOOL_FALSE;
  ctool_u32 diagnostic_count;
  ctool_u32 index;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/block-function-ir.c", source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "block function lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_block_function_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  block_index = find_block_binding(&unit, "external_helper");
  linked_index =
      block_index < unit.block_binding_count
          ? unit.block_bindings[block_index].linkage_binding
          : CTOOL_C_AST_NONE;
  if (block_index >= unit.block_binding_count ||
      linked_index >= unit.binding_count) {
    goto cleanup;
  }
  unused_block_index = find_block_binding(&unit, "unused_helper");
  unused_linked_index =
      unused_block_index < unit.block_binding_count
          ? unit.block_bindings[unused_block_index].linkage_binding
          : CTOOL_C_AST_NONE;
  for (index = 0u; index < unit.graph.type_count; index++) {
    if (unit.graph.types[index].kind == CTOOL_C_TYPE_SIGNED_INT &&
        unit.graph.types[index].qualifiers == 0u) {
      signed_int_type = index;
      break;
    }
  }
  for (index = 0u; index < unit.block_binding_count; index++) {
    const ctool_c_block_binding_t *candidate = &unit.block_bindings[index];
    if (string_equal(candidate->name, "external_helper") != 0 &&
        candidate->type < unit.graph.type_count &&
        unit.graph.types[candidate->type].kind == CTOOL_C_TYPE_FUNCTION &&
        unit.graph.types[candidate->type].has_prototype == CTOOL_FALSE) {
      old_style_block_index = index;
    }
  }
  if (unused_block_index >= unit.block_binding_count ||
      unused_linked_index >= unit.binding_count ||
      old_style_block_index >= unit.block_binding_count ||
      signed_int_type == CTOOL_C_TYPE_NONE) {
    goto cleanup;
  }
  status = ctool_c_ir_function_types_compatible(
      job, &unit, unit.bindings[linked_index].type,
      unit.block_bindings[old_style_block_index].type, &compatible);
  if (!check_status(status, CTOOL_OK,
                    "compatible block function type query") ||
      compatible != CTOOL_TRUE) {
    goto cleanup;
  }
  compatible = CTOOL_TRUE;
  status = ctool_c_ir_function_types_compatible(
      job, &unit, unit.bindings[linked_index].type,
      signed_int_type, &compatible);
  if (!check_status(status, CTOOL_OK,
                    "non-function block type query") ||
      compatible != CTOOL_FALSE) {
    goto cleanup;
  }
  invalid_blocks = (ctool_c_block_binding_t *)malloc(
      (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_bindings = (ctool_c_binding_t *)malloc(
      (size_t)unit.binding_count * sizeof(*invalid_bindings));
  if (invalid_blocks == NULL || invalid_bindings == NULL) {
    goto cleanup;
  }

  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_blocks[block_index].linkage_binding = CTOOL_C_AST_NONE;
  invalid_unit = unit;
  invalid_unit.block_bindings = invalid_blocks;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block function missing linked identity")) {
    goto cleanup;
  }

  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_blocks[block_index].storage = CTOOL_C_STORAGE_STATIC;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block function invalid storage")) {
    goto cleanup;
  }

  (void)memcpy(invalid_bindings, unit.bindings,
               (size_t)unit.binding_count * sizeof(*invalid_bindings));
  invalid_bindings[linked_index].kind = CTOOL_C_BINDING_OBJECT;
  invalid_unit = unit;
  invalid_unit.bindings = invalid_bindings;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block function linked to non-function binding")) {
    goto cleanup;
  }

  (void)memcpy(invalid_bindings, unit.bindings,
               (size_t)unit.binding_count * sizeof(*invalid_bindings));
  invalid_bindings[linked_index].name = ctool_string("wrong_helper");
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block function linked to different name")) {
    goto cleanup;
  }

  (void)memcpy(invalid_bindings, unit.bindings,
               (size_t)unit.binding_count * sizeof(*invalid_bindings));
  invalid_bindings[linked_index].storage = CTOOL_C_STORAGE_STATIC;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block function linked to impossible storage and linkage")) {
    goto cleanup;
  }

  (void)memcpy(invalid_bindings, unit.bindings,
               (size_t)unit.binding_count * sizeof(*invalid_bindings));
  invalid_bindings[linked_index].storage = CTOOL_C_STORAGE_STATIC;
  invalid_bindings[linked_index].linkage = CTOOL_C_LINKAGE_INTERNAL;
  invalid_bindings[linked_index].file_scope_visible = CTOOL_FALSE;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block function linked to hidden internal function")) {
    goto cleanup;
  }

  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  (void)memcpy(invalid_bindings, unit.bindings,
               (size_t)unit.binding_count * sizeof(*invalid_bindings));
  invalid_blocks[unused_block_index].type = signed_int_type;
  invalid_bindings[unused_linked_index].type = signed_int_type;
  invalid_unit = unit;
  invalid_unit.block_bindings = invalid_blocks;
  invalid_unit.bindings = invalid_bindings;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block function linked through non-function types")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_bindings);
  free(invalid_blocks);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("block-functions: ok");
    return 0;
  }
  return 1;
}

static int run_block_typedefs(const char *host_root) {
  static const char source[] =
      "unsigned int block_typedef(unsigned int input) {\n"
      "  typedef unsigned char byte_t;\n"
      "  byte_t value = (byte_t)input;\n"
      "  return value;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_block_binding_t *invalid_blocks = NULL;
  ctool_c_ir_unit_t first;
  ctool_c_ir_unit_t second;
  ctool_u64 fingerprint;
  ctool_u64 first_ir_fingerprint;
  ctool_u32 typedef_index;
  ctool_u32 value_index;
  ctool_u32 local_addresses = 0u;
  ctool_u32 index;
  ctool_status_t status;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&invalid_unit, 0, sizeof(invalid_unit));
  (void)memset(&first, 0xa5, sizeof(first));
  (void)memset(&second, 0xa5, sizeof(second));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/block-typedef-ir.c", source, &unit)) {
    goto cleanup;
  }
  typedef_index = find_block_binding(&unit, "byte_t");
  value_index = find_block_binding(&unit, "value");
  if (typedef_index == CTOOL_C_AST_NONE ||
      value_index == CTOOL_C_AST_NONE || typedef_index >= value_index ||
      unit.block_binding_count != 2u ||
      unit.block_bindings[typedef_index].kind != CTOOL_C_BINDING_TYPEDEF ||
      unit.block_bindings[typedef_index].storage != CTOOL_C_STORAGE_TYPEDEF ||
      unit.block_bindings[typedef_index].type >= unit.graph.type_count ||
      unit.block_bindings[typedef_index].initializer != CTOOL_C_AST_NONE ||
      unit.block_bindings[typedef_index].linkage_binding != CTOOL_C_AST_NONE ||
      unit.block_bindings[value_index].kind != CTOOL_C_BINDING_OBJECT ||
      unit.block_bindings[value_index].type !=
          unit.block_bindings[typedef_index].type) {
    (void)fprintf(stderr, "block typedef frontend inventory differs\n");
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  status = ctool_c_lower_ir(job, &unit, &first);
  if (!check_status(status, CTOOL_OK, "block typedef lowering") ||
      unit_fingerprint(&unit) != fingerprint || first.function_count != 1u ||
      first.functions == NULL || first.instruction_count == 0u ||
      first.instructions == NULL || first.functions[0].first_instruction != 0u ||
      first.functions[0].instruction_count != first.instruction_count) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < first.instruction_count; index++) {
    if (first.instructions[index].kind ==
        CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS) {
      if (first.instructions[index].reference != value_index) {
        (void)fprintf(stderr, "block typedef acquired runtime storage\n");
        goto cleanup;
      }
      local_addresses++;
    }
  }
  if (local_addresses != 2u) {
    (void)fprintf(stderr, "block typedef local address inventory differs\n");
    goto cleanup;
  }
  first_ir_fingerprint = ir_instruction_fingerprint(&first);
  status = ctool_c_lower_ir(job, &unit, &second);
  if (!check_status(status, CTOOL_OK, "repeat block typedef lowering") ||
      second.function_count != first.function_count ||
      second.instruction_count != first.instruction_count ||
      ir_instruction_fingerprint(&second) != first_ir_fingerprint ||
      unit_fingerprint(&unit) != fingerprint) {
    (void)fprintf(stderr, "block typedef IR is not deterministic\n");
    goto cleanup;
  }
  invalid_blocks = (ctool_c_block_binding_t *)malloc(
      (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  if (invalid_blocks == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_unit = unit;
  invalid_unit.block_bindings = invalid_blocks;
  invalid_blocks[typedef_index].storage = CTOOL_C_STORAGE_NONE;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block typedef missing typedef storage")) {
    goto cleanup;
  }
  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_blocks[typedef_index].initializer = 0u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block typedef with initializer")) {
    goto cleanup;
  }
  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_blocks[typedef_index].linkage_binding = 0u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block typedef with linked identity")) {
    goto cleanup;
  }
  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_blocks[typedef_index].type = unit.graph.type_count;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block typedef with invalid type")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_blocks);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("block-typedefs: ok");
    return 0;
  }
  return 1;
}

static int run_aggregate_initializers(const char *host_root) {
  static const char source[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef unsigned short ctool_u16;\n"
      "typedef struct { const char *data; ctool_u32 size; } ctool_string_t;\n"
      "typedef struct { ctool_u16 columns[3]; } row_t;\n"
      "typedef struct { ctool_u32 marker; row_t rows[2]; } map_t;\n"
      "ctool_u32 aggregate_initializers(ctool_u32 seed) {\n"
      "  ctool_string_t no_name = {(const char *)0, 0u};\n"
      "  map_t map = {\n"
      "      .rows = {[1] = {.columns = {[2] = seed}}},\n"
      "      .marker = seed};\n"
      "  return no_name.size + map.marker + map.rows[0].columns[0] +\n"
      "         map.rows[1].columns[2];\n"
      "}\n";
  static const char omitted_subobjects_source[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef struct { ctool_u32 leaf; } inner_t;\n"
      "typedef struct {\n"
      "  ctool_u32 flag : 1;\n"
      "  volatile ctool_u32 : 0;\n"
      "  inner_t omitted;\n"
      "  ctool_u32 head;\n"
      "  unsigned long long wide_tail;\n"
      "} omitted_t;\n"
      "ctool_u32 omitted_subobjects(ctool_u32 seed) {\n"
      "  omitted_t value = {.head = seed};\n"
      "  return value.head;\n"
      "}\n";
  static const char wide_leaf_source[] =
      "typedef struct { unsigned long long value; } wide_t;\n"
      "unsigned int wide_leaf(void) {\n"
      "  wide_t value = {1ULL};\n"
      "  return 0u;\n"
      "}\n";
  static const char volatile_bit_field_source[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef struct {\n"
      "  volatile ctool_u32 flag : 1;\n"
      "  ctool_u32 value;\n"
      "} volatile_bits_t;\n"
      "ctool_u32 volatile_bit_field(ctool_u32 seed) {\n"
      "  volatile_bits_t bits = {.value = seed};\n"
      "  return bits.value;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_path_t active_path;
  ctool_source_t active_file;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t omitted_subobjects_unit;
  ctool_c_translation_unit_t wide_leaf_unit;
  ctool_c_translation_unit_t volatile_bit_field_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_ir_unit_t ir;
  ctool_c_ir_unit_t omitted_subobjects_ir;
  ctool_c_ir_unit_t wide_leaf_ir;
  ctool_c_initializer_element_t *invalid_elements = NULL;
  ctool_c_type_layout_t *invalid_layouts = NULL;
  ctool_c_record_member_t *invalid_members = NULL;
  ctool_u32 rows_member;
  ctool_u32 rows_type;
  ctool_u32 row_type;
  ctool_u32 selector_edge = CTOOL_C_AST_NONE;
  ctool_u32 index;
  ctool_u32 diagnostic_count;
  ctool_u32 leaf_member;
  ctool_u32 inner_type = CTOOL_C_AST_NONE;
  ctool_u32 zero_count;
  ctool_u32 store_count;
  ctool_u32 wide_constant_count;
  ctool_u32 wide_store_count;
  uint64_t fingerprint;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&omitted_subobjects_unit, 0,
               sizeof(omitted_subobjects_unit));
  (void)memset(&wide_leaf_unit, 0, sizeof(wide_leaf_unit));
  (void)memset(&volatile_bit_field_unit, 0,
               sizeof(volatile_bit_field_unit));
  (void)memset(&ir, 0, sizeof(ir));
  (void)memset(&omitted_subobjects_ir, 0,
               sizeof(omitted_subobjects_ir));
  (void)memset(&wide_leaf_ir, 0, sizeof(wide_leaf_ir));
  if (!open_job(host_root, &adapter, &config, &job)) {
    goto cleanup;
  }
  active_path.text = ctool_string("/toolchain/cupidc_pp.c");
  (void)memset(&active_file, 0xa5, sizeof(active_file));
  status = ctool_job_load_source(job, &active_path, &active_file);
  if (!check_status(status, CTOOL_OK,
                    "load active aggregate initializer source") ||
      active_file.contents.data == NULL ||
      strstr((const char *)active_file.contents.data,
             active_automatic_aggregate_initializer) == NULL ||
      !parse_source(job, "/aggregate-initializers.c", source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "aggregate initializer lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_aggregate_initializer_ir(job, &unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  rows_member = find_member(&unit, "rows");
  if (rows_member == CTOOL_C_AST_NONE) {
    goto cleanup;
  }
  rows_type = unit.graph.members[rows_member].type;
  if (rows_type >= unit.graph.type_count ||
      unit.graph.types[rows_type].kind != CTOOL_C_TYPE_ARRAY) {
    goto cleanup;
  }
  row_type = unit.graph.types[rows_type].referenced_type;
  for (index = 0u; index < unit.initializer_element_count; index++) {
    const ctool_c_initializer_element_t *edge =
        &unit.initializer_elements[index];
    if (edge->initializer < unit.initializer_count &&
        edge->subobject == 1u &&
        unit.initializers[edge->initializer].type == row_type) {
      selector_edge = index;
      break;
    }
  }
  if (selector_edge == CTOOL_C_AST_NONE ||
      (unit.initializer_element_count != 0u &&
       sizeof(*invalid_elements) >
           SIZE_MAX / (size_t)unit.initializer_element_count)) {
    (void)fprintf(stderr,
                  "aggregate initializer selector fixture differs\n");
    goto cleanup;
  }
  invalid_elements = (ctool_c_initializer_element_t *)malloc(
      (size_t)unit.initializer_element_count * sizeof(*invalid_elements));
  if (invalid_elements == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_elements, unit.initializer_elements,
               (size_t)unit.initializer_element_count *
                   sizeof(*invalid_elements));
  invalid_elements[selector_edge].subobject =
      unit.graph.types[rows_type].element_count;
  invalid_unit = unit;
  invalid_unit.initializer_elements = invalid_elements;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "out-of-range aggregate initializer selector")) {
    goto cleanup;
  }
  if (unit.layout.type_count == 0u ||
      rows_type >= unit.layout.type_count ||
      sizeof(*invalid_layouts) >
          SIZE_MAX / (size_t)unit.layout.type_count) {
    goto cleanup;
  }
  invalid_layouts = (ctool_c_type_layout_t *)malloc(
      (size_t)unit.layout.type_count * sizeof(*invalid_layouts));
  if (invalid_layouts == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_layouts, unit.layout.types,
               (size_t)unit.layout.type_count * sizeof(*invalid_layouts));
  invalid_layouts[rows_type].size++;
  invalid_unit = unit;
  invalid_unit.layout.types = invalid_layouts;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "inconsistent aggregate initializer array layout")) {
    goto cleanup;
  }
  if (!parse_source(job, "/omitted-aggregate-subobjects.c",
                    omitted_subobjects_source,
                    &omitted_subobjects_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&omitted_subobjects_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &omitted_subobjects_unit,
                            &omitted_subobjects_ir);
  zero_count = 0u;
  store_count = 0u;
  for (index = 0u; index < omitted_subobjects_ir.instruction_count;
       index++) {
    if (omitted_subobjects_ir.instructions[index].kind ==
        CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT) {
      zero_count++;
    } else if (omitted_subobjects_ir.instructions[index].kind ==
               CTOOL_C_IR_INSTRUCTION_STORE) {
      store_count++;
    }
  }
  if (!check_status(status, CTOOL_OK,
                    "omitted aggregate subobject lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&omitted_subobjects_unit) != fingerprint ||
      omitted_subobjects_ir.function_count != 1u || zero_count != 1u ||
      store_count != 1u) {
      (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  leaf_member = find_member(&omitted_subobjects_unit, "leaf");
  if (leaf_member == CTOOL_C_AST_NONE ||
      omitted_subobjects_unit.graph.member_count == 0u ||
      leaf_member >= omitted_subobjects_unit.graph.member_count ||
      leaf_member >= omitted_subobjects_unit.layout.member_count ||
      sizeof(*invalid_members) >
          SIZE_MAX /
              (size_t)omitted_subobjects_unit.graph.member_count) {
    goto cleanup;
  }
  for (index = 0u; index < omitted_subobjects_unit.graph.type_count;
       index++) {
    const ctool_c_type_node_t *node =
        &omitted_subobjects_unit.graph.types[index];
    if (node->kind == CTOOL_C_TYPE_RECORD &&
        leaf_member >= node->first_member &&
        leaf_member - node->first_member < node->member_count) {
      inner_type = index;
      break;
    }
  }
  if (inner_type == CTOOL_C_AST_NONE ||
      inner_type >= omitted_subobjects_unit.layout.type_count ||
      omitted_subobjects_unit.layout.members[leaf_member].size !=
          omitted_subobjects_unit.layout.types[inner_type].size) {
    goto cleanup;
  }
  invalid_members = (ctool_c_record_member_t *)malloc(
      (size_t)omitted_subobjects_unit.graph.member_count *
      sizeof(*invalid_members));
  if (invalid_members == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_members, omitted_subobjects_unit.graph.members,
               (size_t)omitted_subobjects_unit.graph.member_count *
                   sizeof(*invalid_members));
  invalid_members[leaf_member].type = inner_type;
  invalid_unit = omitted_subobjects_unit;
  invalid_unit.graph.members = invalid_members;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "inline automatic aggregate type cycle")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-aggregate-leaf.c", wide_leaf_source,
                    &wide_leaf_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&wide_leaf_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &wide_leaf_unit, &wide_leaf_ir);
  wide_constant_count = 0u;
  wide_store_count = 0u;
  if (status == CTOOL_OK) {
    for (index = 0u; index < wide_leaf_ir.instruction_count; index++) {
      const ctool_c_ir_instruction_t *instruction =
          &wide_leaf_ir.instructions[index];
      const ctool_c_type_layout_t *layout =
          instruction->type < wide_leaf_unit.layout.type_count
              ? &wide_leaf_unit.layout.types[instruction->type]
              : (const ctool_c_type_layout_t *)0;
      if (layout != (const ctool_c_type_layout_t *)0 &&
          layout->is_integer == CTOOL_TRUE && layout->size == 8u) {
        if (instruction->kind == CTOOL_C_IR_INSTRUCTION_INTEGER) {
          wide_constant_count++;
        } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE) {
          wide_store_count++;
        }
      }
    }
  }
  if (!check_status(status, CTOOL_OK,
                    "wide automatic aggregate expression leaf") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&wide_leaf_unit) != fingerprint ||
      wide_constant_count != 1u || wide_store_count != 1u) {
    (void)fprintf(stderr, "wide aggregate initializer leaf differs\n");
    goto cleanup;
  }
  if (!parse_source(job, "/volatile-aggregate-bit-field.c",
                    volatile_bit_field_source,
                    &volatile_bit_field_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &volatile_bit_field_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "omitted volatile aggregate bit-field")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_elements);
  free(invalid_layouts);
  free(invalid_members);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("aggregate-initializers: ok");
    return 0;
  }
  return 1;
}

static int validate_narrow_load_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_type_node_t *pointer_type;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 first_parameter;
  ctool_u32 byte_type;
  ctool_u32 index_type;
  ctool_u32 promoted_type = CTOOL_C_TYPE_NONE;
  ctool_u32 index;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->functions == NULL || ir->instruction_count != 23u ||
      ir->instructions == NULL) {
    (void)fprintf(stderr,
                  "narrow load IR inventory differs: definitions=%u "
                  "functions=%u instructions=%u\n",
                  (unsigned int)unit->function_definition_count,
                  (unsigned int)ir->function_count,
                  (unsigned int)ir->instruction_count);
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      function_type->first_parameter > unit->parameter_count - 2u) {
    (void)fprintf(stderr, "narrow load function type differs\n");
    return 0;
  }
  first_parameter = function_type->first_parameter;
  pointer_type = &unit->graph.types[
      unit->parameters[first_parameter].type];
  index_type = unit->parameters[first_parameter + 1u].type;
  if (pointer_type->kind != CTOOL_C_TYPE_POINTER ||
      pointer_type->referenced_type >= unit->layout.type_count ||
      index_type >= unit->layout.type_count ||
      function_type->referenced_type >= unit->layout.type_count) {
    return 0;
  }
  byte_type = pointer_type->referenced_type;
  for (index = 0u; index < unit->graph.type_count; index++) {
    if (unit->graph.types[index].kind == CTOOL_C_TYPE_SIGNED_INT &&
        unit->graph.types[index].qualifiers == 0u &&
        index < unit->layout.type_count &&
        unit->layout.types[index].size == 4u) {
      promoted_type = index;
      break;
    }
  }
  if (promoted_type == CTOOL_C_TYPE_NONE ||
      unit->layout.types[byte_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[byte_type].size != 1u ||
      unit->layout.types[byte_type].is_signed != CTOOL_FALSE ||
      unit->layout.types[index_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[index_type].size != 4u ||
      unit->layout.types[function_type->referenced_type].is_integer !=
          CTOOL_TRUE ||
      unit->layout.types[function_type->referenced_type].size != 4u) {
    (void)fprintf(stderr, "narrow load target types differ\n");
    return 0;
  }
  if (ir->functions[0].binding != definition->binding ||
      ir->functions[0].declared_type != definition->declared_type ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 11u ||
      ir->functions[0].maximum_stack_depth != 2u) {
    (void)fprintf(stderr, "narrow load function record differs\n");
    return 0;
  }
  instructions = ir->instructions;
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != unit->parameters[first_parameter].type ||
      instructions[0].reference != first_parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != unit->parameters[first_parameter].type ||
      instructions[1].input_type != unit->parameters[first_parameter].type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[2].type != index_type ||
      instructions[2].reference != first_parameter + 1u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].type != index_type ||
      instructions[3].input_type != index_type ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_POINTER_BINARY ||
      instructions[4].type != unit->parameters[first_parameter].type ||
      instructions[4].input_type != unit->parameters[first_parameter].type ||
      instructions[4].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[4].reference != index_type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_DEREFERENCE ||
      instructions[5].type != byte_type ||
      instructions[5].input_type != unit->parameters[first_parameter].type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[6].type != byte_type ||
      instructions[6].input_type != byte_type ||
      instructions[6].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[7].type != promoted_type ||
      instructions[7].input_type != byte_type ||
      instructions[7].conversion != CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
      promoted_type != function_type->referenced_type ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[8].type != promoted_type ||
      instructions[8].integer_bits != 0u ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[9].type != promoted_type ||
      instructions[9].input_type != promoted_type ||
      instructions[9].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[10].type != function_type->referenced_type ||
      instructions[10].input_type != function_type->referenced_type) {
    (void)fprintf(stderr, "narrow load instruction stream differs\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    if (!string_equal(instructions[index].location.path,
                      "/narrow-values.c") ||
        !string_equal(instructions[index].physical_location.path,
                      "/narrow-values.c")) {
      return 0;
    }
  }
  return 1;
}

static int validate_narrow_store_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_type_node_t *pointer_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 first_parameter;
  ctool_u32 byte_type;
  ctool_u32 index_type;
  ctool_u32 value_type;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->functions == NULL || ir->instruction_count != 23u ||
      ir->instructions == NULL) {
    return 0;
  }
  definition = &unit->function_definitions[1];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 3u ||
      function_type->first_parameter > unit->parameter_count - 3u) {
    return 0;
  }
  first_parameter = function_type->first_parameter;
  pointer_type =
      &unit->graph.types[unit->parameters[first_parameter].type];
  index_type = unit->parameters[first_parameter + 1u].type;
  value_type = unit->parameters[first_parameter + 2u].type;
  if (pointer_type->kind != CTOOL_C_TYPE_POINTER ||
      pointer_type->referenced_type >= unit->layout.type_count ||
      index_type >= unit->layout.type_count ||
      value_type >= unit->layout.type_count ||
      function_type->referenced_type >= unit->layout.type_count) {
    return 0;
  }
  byte_type = pointer_type->referenced_type;
  function = &ir->functions[1];
  instructions = &ir->instructions[function->first_instruction];
  if (unit->layout.types[byte_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[byte_type].size != 1u ||
      unit->layout.types[byte_type].is_signed != CTOOL_FALSE ||
      unit->layout.types[index_type].size != 4u ||
      unit->layout.types[value_type].size != 4u ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 11u ||
      function->instruction_count != 12u ||
      function->maximum_stack_depth != 2u) {
    (void)fprintf(stderr, "narrow store function record differs\n");
    return 0;
  }
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != unit->parameters[first_parameter].type ||
      instructions[0].reference != first_parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != unit->parameters[first_parameter].type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[2].type != index_type ||
      instructions[2].reference != first_parameter + 1u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].type != index_type ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_POINTER_BINARY ||
      instructions[4].type != unit->parameters[first_parameter].type ||
      instructions[4].input_type != unit->parameters[first_parameter].type ||
      instructions[4].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[4].reference != index_type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_DEREFERENCE ||
      instructions[5].type != byte_type ||
      instructions[5].input_type != unit->parameters[first_parameter].type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[6].type != value_type ||
      instructions[6].reference != first_parameter + 2u ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[7].type != value_type ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[8].type != byte_type ||
      instructions[8].input_type != value_type ||
      instructions[8].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[9].type != byte_type ||
      instructions[9].input_type != byte_type ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[10].type != function_type->referenced_type ||
      instructions[10].input_type != byte_type ||
      instructions[10].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[11].type != function_type->referenced_type ||
      instructions[11].input_type != function_type->referenced_type) {
    (void)fprintf(stderr, "narrow store instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_narrow_active_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  static const char *const function_names[] = {
      "asm_lower", "x86_class_width", "x86_set_memory_width",
      "narrow_file", "narrow_indirect", "narrow_logic"};
  ctool_u32 narrow_parameters = 0u;
  ctool_u32 signed_byte_loads = 0u;
  ctool_u32 signed_byte_casts = 0u;
  ctool_u32 signed_byte_returns = 0u;
  ctool_u32 word_locals = 0u;
  ctool_u32 word_files = 0u;
  ctool_u32 word_loads = 0u;
  ctool_u32 word_stores = 0u;
  ctool_u32 word_promotions = 0u;
  ctool_u32 word_direct_calls = 0u;
  ctool_u32 word_indirect_calls = 0u;
  ctool_u32 word_returns = 0u;
  ctool_u32 narrow_branches = 0u;
  ctool_u32 index;
  if (unit->function_definition_count != 6u || ir->function_count != 6u ||
      ir->functions == NULL || ir->instructions == NULL) {
    (void)fprintf(stderr, "narrow active IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < 6u; index++) {
    ctool_u32 binding = find_binding(unit, function_names[index]);
    if (binding == CTOOL_C_AST_NONE ||
        unit->function_definitions[index].binding != binding ||
        ir->functions[index].binding != binding ||
        ir->functions[index].declared_type !=
            unit->function_definitions[index].declared_type) {
      (void)fprintf(stderr, "narrow active function %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction = &ir->instructions[index];
    const ctool_c_type_layout_t *type_layout =
        instruction->type < unit->layout.type_count
            ? &unit->layout.types[instruction->type]
            : NULL;
    const ctool_c_type_layout_t *input_layout =
        instruction->input_type < unit->layout.type_count
            ? &unit->layout.types[instruction->input_type]
            : NULL;
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS &&
        type_layout != NULL && type_layout->is_integer == CTOOL_TRUE &&
        (type_layout->size == 1u || type_layout->size == 2u)) {
      narrow_parameters++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD &&
        type_layout != NULL && type_layout->is_integer == CTOOL_TRUE &&
        type_layout->size == 1u && type_layout->is_signed == CTOOL_TRUE) {
      signed_byte_loads++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CONVERT &&
        type_layout != NULL && input_layout != NULL &&
        type_layout->is_integer == CTOOL_TRUE && type_layout->size == 1u &&
        type_layout->is_signed == CTOOL_TRUE && input_layout->size == 4u) {
      signed_byte_casts++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_RETURN_VALUE &&
        type_layout != NULL && type_layout->is_integer == CTOOL_TRUE &&
        type_layout->size == 1u && type_layout->is_signed == CTOOL_TRUE) {
      signed_byte_returns++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS &&
        type_layout != NULL && type_layout->is_integer == CTOOL_TRUE &&
        type_layout->size == 2u) {
      word_locals++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS &&
        type_layout != NULL && type_layout->is_integer == CTOOL_TRUE &&
        type_layout->size == 2u) {
      word_files++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD &&
        type_layout != NULL && type_layout->is_integer == CTOOL_TRUE &&
        type_layout->size == 2u) {
      word_loads++;
    }
    if ((instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE ||
         instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE_VALUE) &&
        type_layout != NULL && type_layout->is_integer == CTOOL_TRUE &&
        type_layout->size == 2u) {
      word_stores++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CONVERT &&
        type_layout != NULL && input_layout != NULL &&
        type_layout->is_integer == CTOOL_TRUE && type_layout->size == 4u &&
        input_layout->is_integer == CTOOL_TRUE && input_layout->size == 2u) {
      word_promotions++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT &&
        type_layout != NULL && type_layout->is_integer == CTOOL_TRUE &&
        type_layout->size == 2u) {
      word_direct_calls++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT &&
        type_layout != NULL && type_layout->is_integer == CTOOL_TRUE &&
        type_layout->size == 2u) {
      word_indirect_calls++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_RETURN_VALUE &&
        type_layout != NULL && type_layout->is_integer == CTOOL_TRUE &&
        type_layout->size == 2u) {
      word_returns++;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO &&
        input_layout != NULL && input_layout->is_integer == CTOOL_TRUE &&
        (input_layout->size == 1u || input_layout->size == 2u)) {
      narrow_branches++;
    }
  }
  if (narrow_parameters < 3u || signed_byte_loads == 0u ||
      signed_byte_casts == 0u || signed_byte_returns < 2u ||
      word_locals == 0u || word_files < 2u || word_loads < 3u ||
      word_stores < 2u || word_promotions == 0u ||
      word_direct_calls == 0u || word_indirect_calls == 0u ||
      word_returns < 2u || narrow_branches < 2u) {
    (void)fprintf(stderr,
                  "narrow active operations differ: params=%u load8=%u "
                  "cast8=%u return8=%u local16=%u file16=%u load16=%u "
                  "store16=%u promote16=%u direct16=%u indirect16=%u "
                  "return16=%u branches=%u\n",
                  (unsigned int)narrow_parameters,
                  (unsigned int)signed_byte_loads,
                  (unsigned int)signed_byte_casts,
                  (unsigned int)signed_byte_returns,
                  (unsigned int)word_locals, (unsigned int)word_files,
                  (unsigned int)word_loads, (unsigned int)word_stores,
                  (unsigned int)word_promotions,
                  (unsigned int)word_direct_calls,
                  (unsigned int)word_indirect_calls,
                  (unsigned int)word_returns,
                  (unsigned int)narrow_branches);
    return 0;
  }
  return 1;
}

static int run_narrow_values(const char *host_root) {
  static const char source[] =
      "typedef unsigned char ctool_u8;\n"
      "typedef unsigned int ctool_u32;\n"
      "int load_byte(ctool_u8 *values, ctool_u32 index) {\n"
      "  return values[index] + 0;\n"
      "}\n"
      "int store_byte(ctool_u8 *values, ctool_u32 index, "
      "ctool_u32 value) {\n"
      "  return values[index] = (ctool_u8)value;\n"
      "}\n";
  static const char conversion_source[] =
      "typedef unsigned short u16;\n"
      "typedef unsigned int u32;\n"
      "u16 narrow_return(u32 value) { return value; }\n"
      "u32 promoted_return(u16 value) { return value + 0u; }\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t active_unit;
  ctool_c_translation_unit_t conversion_unit;
  ctool_c_translation_unit_t invalid_conversion_unit;
  ctool_c_translation_unit_t invalid_parameter_unit;
  ctool_c_translation_unit_t invalid_binding_unit;
  ctool_c_ir_unit_t ir;
  ctool_c_ir_unit_t active_ir;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_u32 *invalid_parameter_types = NULL;
  ctool_c_binding_t *invalid_bindings = NULL;
  char *active_fixture = NULL;
  ctool_u64 fingerprint;
  ctool_u32 conversion_index = CTOOL_C_AST_NONE;
  ctool_u32 promotion_index = CTOOL_C_AST_NONE;
  ctool_u32 conversion_unsigned_int_type = CTOOL_C_TYPE_NONE;
  ctool_u32 signed_int_type = CTOOL_C_TYPE_NONE;
  ctool_u32 asm_binding;
  ctool_u32 other_binding;
  ctool_u32 index;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&active_unit, 0, sizeof(active_unit));
  (void)memset(&conversion_unit, 0, sizeof(conversion_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_is_unchanged(job) ||
      !parse_source(job, "/narrow-values.c", source, &unit) ||
      !parse_source(job, "/narrow-conversion.c", conversion_source,
                    &conversion_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "narrow integer load lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_narrow_load_ir(&unit, &ir) ||
      !validate_narrow_store_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  active_fixture = make_narrow_active_fixture();
  if (active_fixture == NULL ||
      !parse_source(job, "/toolchain/narrow-active.c", active_fixture,
                    &active_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&active_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&active_ir, 0xa5, sizeof(active_ir));
  status = ctool_c_lower_ir(job, &active_unit, &active_ir);
  if (!check_status(status, CTOOL_OK, "active narrow integer lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&active_unit) != fingerprint ||
      !validate_narrow_active_ir(&active_unit, &active_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < conversion_unit.expression_count; index++) {
    if (conversion_index == CTOOL_C_AST_NONE &&
        conversion_unit.expressions[index].kind ==
            CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION &&
        conversion_unit.expressions[index].conversion ==
            CTOOL_C_CONVERSION_ASSIGNMENT &&
        conversion_unit.expressions[index].type <
            conversion_unit.layout.type_count &&
        conversion_unit
                .layout.types[conversion_unit.expressions[index].type]
                .size == 2u) {
      conversion_index = index;
    }
    if (promotion_index == CTOOL_C_AST_NONE &&
        conversion_unit.expressions[index].kind ==
            CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION &&
        conversion_unit.expressions[index].conversion ==
            CTOOL_C_CONVERSION_INTEGER_PROMOTION) {
      promotion_index = index;
    }
  }
  for (index = 0u; index < conversion_unit.graph.type_count; index++) {
    if (conversion_unit.graph.types[index].kind ==
            CTOOL_C_TYPE_UNSIGNED_INT &&
        conversion_unit.graph.types[index].qualifiers == 0u &&
        index < conversion_unit.layout.type_count &&
        conversion_unit.layout.types[index].size == 4u) {
      conversion_unsigned_int_type = index;
      break;
    }
  }
  if (conversion_index == CTOOL_C_AST_NONE ||
      promotion_index == CTOOL_C_AST_NONE ||
      conversion_unsigned_int_type == CTOOL_C_TYPE_NONE ||
      conversion_unit.expression_count == 0u ||
      sizeof(*invalid_expressions) >
          SIZE_MAX / (size_t)conversion_unit.expression_count) {
    (void)fprintf(stderr,
                  "narrow conversion fixture differs: assignment=%u "
                  "promotion=%u unsigned-int=%u\n",
                  (unsigned int)conversion_index,
                  (unsigned int)promotion_index,
                  (unsigned int)conversion_unsigned_int_type);
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)conversion_unit.expression_count * sizeof(*invalid_expressions));
  if (invalid_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, conversion_unit.expressions,
               (size_t)conversion_unit.expression_count *
                   sizeof(*invalid_expressions));
  invalid_expressions[conversion_index].conversion =
      CTOOL_C_CONVERSION_INTEGER_PROMOTION;
  invalid_conversion_unit = conversion_unit;
  invalid_conversion_unit.expressions = invalid_expressions;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_conversion_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "narrowing mislabeled as integer promotion")) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, conversion_unit.expressions,
               (size_t)conversion_unit.expression_count *
                   sizeof(*invalid_expressions));
  invalid_expressions[promotion_index].type =
      conversion_unsigned_int_type;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_conversion_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "integer promotion with the wrong target")) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, conversion_unit.expressions,
               (size_t)conversion_unit.expression_count *
                   sizeof(*invalid_expressions));
  invalid_expressions[promotion_index].conversion =
      CTOOL_C_CONVERSION_USUAL_ARITHMETIC;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_conversion_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "usual arithmetic conversion before integer promotion")) {
    goto cleanup;
  }
  if (active_unit.function_definition_count == 0u ||
      active_unit.graph.parameter_type_count == 0u ||
      active_unit.binding_count == 0u ||
      sizeof(*invalid_parameter_types) >
          SIZE_MAX / (size_t)active_unit.graph.parameter_type_count ||
      sizeof(*invalid_bindings) >
          SIZE_MAX / (size_t)active_unit.binding_count) {
    goto cleanup;
  }
  for (index = 0u; index < active_unit.graph.type_count; index++) {
    if (active_unit.graph.types[index].kind == CTOOL_C_TYPE_SIGNED_INT &&
        active_unit.graph.types[index].qualifiers == 0u &&
        index < active_unit.layout.type_count &&
        active_unit.layout.types[index].size == 4u) {
      signed_int_type = index;
      break;
    }
  }
  invalid_parameter_types = (ctool_u32 *)malloc(
      (size_t)active_unit.graph.parameter_type_count *
      sizeof(*invalid_parameter_types));
  invalid_bindings = (ctool_c_binding_t *)malloc(
      (size_t)active_unit.binding_count * sizeof(*invalid_bindings));
  asm_binding = find_binding(&active_unit, "asm_lower");
  other_binding = find_binding(&active_unit, "x86_class_width");
  if (signed_int_type == CTOOL_C_TYPE_NONE ||
      invalid_parameter_types == NULL || invalid_bindings == NULL ||
      asm_binding == CTOOL_C_AST_NONE || other_binding == CTOOL_C_AST_NONE ||
      active_unit.function_definitions[0].declared_type >=
          active_unit.graph.type_count ||
      active_unit
              .graph.types[active_unit.function_definitions[0].declared_type]
              .kind != CTOOL_C_TYPE_FUNCTION ||
      active_unit
              .graph.types[active_unit.function_definitions[0].declared_type]
              .parameter_count == 0u) {
    goto cleanup;
  }
  (void)memcpy(invalid_parameter_types,
               active_unit.graph.parameter_types,
               (size_t)active_unit.graph.parameter_type_count *
                   sizeof(*invalid_parameter_types));
  index = active_unit
              .graph.types[active_unit.function_definitions[0].declared_type]
              .first_parameter;
  invalid_parameter_types[index] = signed_int_type;
  invalid_parameter_unit = active_unit;
  invalid_parameter_unit.graph.parameter_types = invalid_parameter_types;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_parameter_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "narrow function parameter ABI mismatch")) {
    goto cleanup;
  }
  (void)memcpy(invalid_bindings, active_unit.bindings,
               (size_t)active_unit.binding_count * sizeof(*invalid_bindings));
  invalid_bindings[asm_binding].type =
      active_unit.bindings[other_binding].type;
  invalid_binding_unit = active_unit;
  invalid_binding_unit.bindings = invalid_bindings;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_binding_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "narrow function binding ABI mismatch")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_bindings);
  free(invalid_parameter_types);
  free(invalid_expressions);
  free(active_fixture);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("narrow-values: ok");
    return 0;
  }
  return 1;
}

static int void_cast_instruction_matches(
    const ctool_c_ir_instruction_t *instruction, const char *path,
    ctool_c_ir_instruction_kind_t kind, ctool_u32 type,
    ctool_u32 input_type, ctool_c_conversion_kind_t conversion,
    ctool_u32 reference, ctool_u32 line, ctool_u32 column) {
  if (instruction->kind == kind && instruction->type == type &&
      instruction->input_type == input_type &&
      instruction->operation == CTOOL_C_EXPRESSION_OPERATOR_NONE &&
      instruction->conversion == conversion &&
      instruction->reference == reference &&
      instruction->integer_bits == 0u &&
      string_equal(instruction->location.path, path) != 0 &&
      string_equal(instruction->physical_location.path, path) != 0 &&
      instruction->location.line == line &&
      instruction->location.column == column &&
      instruction->physical_location.line == line &&
      instruction->physical_location.column == column) {
    return 1;
  }
  (void)fprintf(
      stderr,
      "void-cast instruction differs: kind %u/%u type %u/%u input %u/%u "
      "location %u:%u/%u:%u\n",
      (ctool_u32)instruction->kind, (ctool_u32)kind, instruction->type, type,
      instruction->input_type, input_type, instruction->location.line,
      instruction->location.column, line, column);
  return 0;
}

static int validate_void_cast_ir(const ctool_c_translation_unit_t *unit,
                                 const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 byte_binding = find_binding(unit, "byte_state");
  ctool_u32 byte_type;
  ctool_u32 byte_value_type;
  ctool_u32 function_binding = find_binding(unit, "discard_values");
  ctool_u32 produce_binding = find_binding(unit, "produce");
  ctool_u32 sink_binding = find_binding(unit, "sink");
  ctool_u32 first_parameter;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instruction_count != 16u ||
      ir->instructions == NULL || byte_binding >= unit->binding_count ||
      function_binding >= unit->binding_count ||
      produce_binding >= unit->binding_count ||
      sink_binding >= unit->binding_count) {
    (void)fprintf(stderr, "void-cast IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->binding != function_binding ||
      definition->declared_type >= unit->graph.type_count) {
    (void)fprintf(stderr, "void-cast function definition differs\n");
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  byte_type = unit->bindings[byte_binding].type;
  if (byte_type >= unit->graph.type_count ||
      unit->graph.types[byte_type].kind != CTOOL_C_TYPE_QUALIFIED ||
      unit->graph.types[byte_type].referenced_type >=
          unit->graph.type_count) {
    (void)fprintf(stderr, "void-cast volatile byte type differs\n");
    return 0;
  }
  byte_value_type = unit->graph.types[byte_type].referenced_type;
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 3u ||
      function_type->first_parameter > unit->parameter_count - 3u) {
    (void)fprintf(stderr, "void-cast function type differs\n");
    return 0;
  }
  first_parameter = function_type->first_parameter;
  function = &ir->functions[0];
  instructions = ir->instructions + function->first_instruction;
  if (function->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 16u ||
      function->maximum_stack_depth != 1u ||
      string_equal(function->location.path, "/void-casts.c") == 0 ||
      string_equal(function->physical_location.path, "/void-casts.c") == 0) {
    (void)fprintf(stderr, "void-cast function record differs\n");
    return 0;
  }
  for (index = 0u; index < 3u; index++) {
    ctool_u32 base = index * 3u;
    ctool_u32 type = unit->parameters[first_parameter + index].type;
    if (!void_cast_instruction_matches(
            &instructions[base], "/void-casts.c",
            CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, type,
            CTOOL_C_TYPE_NONE, CTOOL_C_CONVERSION_NONE,
            first_parameter + index, 8u + index, 9u) ||
        !void_cast_instruction_matches(
            &instructions[base + 1u], "/void-casts.c",
            CTOOL_C_IR_INSTRUCTION_LOAD, type, type,
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE,
            8u + index, 9u) ||
        !void_cast_instruction_matches(
            &instructions[base + 2u], "/void-casts.c",
            CTOOL_C_IR_INSTRUCTION_DISCARD, CTOOL_C_TYPE_NONE, type,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 8u + index, 3u)) {
      (void)fprintf(stderr, "void-cast parameter discard %u differs\n",
                    index);
      return 0;
    }
  }
  if (!void_cast_instruction_matches(
          &instructions[9], "/void-casts.c",
          CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
          byte_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_CONVERSION_NONE, byte_binding, 11u, 9u) ||
      !void_cast_instruction_matches(
          &instructions[10], "/void-casts.c", CTOOL_C_IR_INSTRUCTION_LOAD,
          byte_value_type, byte_type,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 11u, 9u) ||
      !void_cast_instruction_matches(
          &instructions[11], "/void-casts.c",
          CTOOL_C_IR_INSTRUCTION_DISCARD, CTOOL_C_TYPE_NONE,
          byte_value_type, CTOOL_C_CONVERSION_NONE,
          CTOOL_C_AST_NONE, 11u, 3u) ||
      !void_cast_instruction_matches(
          &instructions[12], "/void-casts.c",
          CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
          unit->graph.types[unit->bindings[produce_binding].type]
              .referenced_type,
          unit->bindings[produce_binding].type, CTOOL_C_CONVERSION_NONE,
          produce_binding, 12u, 16u) ||
      !void_cast_instruction_matches(
          &instructions[13], "/void-casts.c",
          CTOOL_C_IR_INSTRUCTION_DISCARD, CTOOL_C_TYPE_NONE,
          unit->graph.types[unit->bindings[produce_binding].type]
              .referenced_type,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 12u, 3u) ||
      !void_cast_instruction_matches(
          &instructions[14], "/void-casts.c",
          CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
          unit->graph.types[unit->bindings[sink_binding].type]
              .referenced_type,
          unit->bindings[sink_binding].type, CTOOL_C_CONVERSION_NONE,
          sink_binding, 13u, 13u) ||
      !void_cast_instruction_matches(
          &instructions[15], "/void-casts.c",
          CTOOL_C_IR_INSTRUCTION_RETURN_VOID, CTOOL_C_TYPE_NONE,
          CTOOL_C_TYPE_NONE, CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 7u,
          67u)) {
    (void)fprintf(stderr, "void-cast side-effect stream differs\n");
    return 0;
  }
  if (unit->layout.types[instructions[10].type].size != 1u) {
    (void)fprintf(stderr, "void-cast volatile byte lost its width\n");
    return 0;
  }
  return 1;
}

static int validate_active_host_void_cast_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  ctool_u32 calls = 0u;
  ctool_u32 converts = 0u;
  ctool_u32 discards = 0u;
  ctool_u32 index;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->functions == NULL || ir->instruction_count != 18u ||
      ir->instructions == NULL ||
      ir->functions[0].instruction_count != 8u ||
      ir->functions[1].instruction_count != 10u ||
      ir->functions[0].maximum_stack_depth != 1u ||
      ir->functions[1].maximum_stack_depth != 1u) {
    (void)fprintf(stderr, "active host void-cast IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction = &ir->instructions[index];
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT) {
      calls++;
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CONVERT) {
      converts++;
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_DISCARD) {
      discards++;
    }
    if (string_equal(instruction->location.path,
                     "/active-ctool-host-void-casts.c") == 0 ||
        string_equal(instruction->physical_location.path,
                     "/active-ctool-host-void-casts.c") == 0) {
      (void)fprintf(stderr, "active host void-cast location differs\n");
      return 0;
    }
  }
  if (calls != 2u || converts != 1u || discards != 3u ||
      ir->instructions[7].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      ir->instructions[17].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)fprintf(stderr, "active host void-cast instruction mix differs\n");
    return 0;
  }
  return 1;
}

static int run_void_casts(const char *host_root) {
  static const char source_text[] =
      "typedef unsigned char u8;\n"
      "typedef unsigned int u32;\n"
      "typedef u32 (*callback_t)(u32);\n"
      "extern void sink(void);\n"
      "extern u32 produce(void);\n"
      "volatile u8 byte_state;\n"
      "void discard_values(u32 value, u32 *pointer, callback_t callback) {\n"
      "  (void)value;\n"
      "  (void)pointer;\n"
      "  (void)callback;\n"
      "  (void)byte_state;\n"
      "  (void)produce();\n"
      "  (void)sink();\n"
      "}\n";
  static const char active_allocate[] =
      "static void *ctool_host_allocate(void *context, ctool_u32 bytes) {\n"
      "  (void)context;\n"
      "  return malloc((size_t)bytes);\n"
      "}\n";
  static const char active_release[] =
      "static void ctool_host_release(void *context, void *allocation,\n"
      "                               ctool_u32 bytes) {\n"
      "  (void)context;\n"
      "  (void)bytes;\n"
      "  free(allocation);\n"
      "}\n";
  static const char active_source[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef unsigned int size_t;\n"
      "void *malloc(size_t bytes);\n"
      "void free(void *allocation);\n"
      "static void *ctool_host_allocate(void *context, ctool_u32 bytes) {\n"
      "  (void)context;\n"
      "  return malloc((size_t)bytes);\n"
      "}\n"
      "static void ctool_host_release(void *context, void *allocation,\n"
      "                               ctool_u32 bytes) {\n"
      "  (void)context;\n"
      "  (void)bytes;\n"
      "  free(allocation);\n"
      "}\n";
  static const char wide_source[] =
      "void discard_wide(void) { (void)1LL; }\n";
  static const char record_source[] =
      "struct pair { int left; int right; };\n"
      "struct pair record_state;\n"
      "void discard_record(void) { (void)record_state; }\n";
  static const char atomic_source[] =
      "_Atomic int state;\n"
      "void discard_atomic(void) { (void)state; }\n";
  static const char malformed_source[] =
      "void discard_value(int value) { (void)value; }\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_path_t active_path;
  ctool_source_t active_file;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t active_unit;
  ctool_c_translation_unit_t wide_unit;
  ctool_c_translation_unit_t record_unit;
  ctool_c_translation_unit_t atomic_unit;
  ctool_c_translation_unit_t malformed_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_ir_unit_t ir;
  ctool_c_ir_unit_t record_ir;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_u32 diagnostic_count;
  ctool_u32 cast_index = CTOOL_C_AST_NONE;
  ctool_u32 index;
  ctool_status_t status;
  uint64_t fingerprint;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&record_ir, 0xa5, sizeof(record_ir));
  if (!open_job(host_root, &adapter, &config, &job)) {
    goto cleanup;
  }
  active_path.text = ctool_string("/toolchain/ctool_host.c");
  (void)memset(&active_file, 0xa5, sizeof(active_file));
  status = ctool_job_load_source(job, &active_path, &active_file);
  if (!check_status(status, CTOOL_OK, "load active host source") ||
      active_file.contents.data == NULL ||
      strstr((const char *)active_file.contents.data, active_allocate) ==
          NULL ||
      strstr((const char *)active_file.contents.data, active_release) ==
          NULL ||
      !parse_source(job, "/void-casts.c", source_text, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "void cast lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_void_cast_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/active-ctool-host-void-casts.c", active_source,
                    &active_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&active_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &active_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active host void casts") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&active_unit) != fingerprint ||
      !validate_active_host_void_cast_ir(&active_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/wide-void-cast.c", wide_source, &wide_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&wide_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &wide_unit, &ir);
  if (!check_status(status, CTOOL_OK, "wide void-cast operand") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&wide_unit) != fingerprint ||
      ir.function_count != 1u || ir.instruction_count != 3u ||
      ir.functions == NULL || ir.instructions == NULL ||
      ir.functions[0].instruction_count != 3u ||
      ir.functions[0].maximum_stack_depth != 1u ||
      ir.instructions[0].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      ir.instructions[0].integer_bits != 1u ||
      ir.instructions[1].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      ir.instructions[1].input_type != ir.instructions[0].type ||
      ir.instructions[2].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)ctool_job_render_diagnostics(job);
    (void)fprintf(stderr, "wide void-cast IR differs\n");
    goto cleanup;
  }
  if (!parse_source(job, "/record-void-cast.c", record_source,
                    &record_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&record_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &record_unit, &record_ir);
  if (!check_status(status, CTOOL_OK, "record void-cast operand") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&record_unit) != fingerprint ||
      record_ir.function_count != 1u || record_ir.instruction_count != 4u ||
      record_ir.instructions == NULL ||
      record_ir.instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      record_ir.instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      record_ir.instructions[2].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      record_ir.instructions[3].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)ctool_job_render_diagnostics(job);
    (void)fprintf(stderr, "record void-cast IR differs\n");
    goto cleanup;
  }
  if (!parse_source(job, "/atomic-void-cast.c", atomic_source,
                    &atomic_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &atomic_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic void-cast operand") ||
      !parse_source(job, "/malformed-void-cast.c", malformed_source,
                    &malformed_unit)) {
    goto cleanup;
  }
  for (index = 0u; index < malformed_unit.expression_count; index++) {
    if (malformed_unit.expressions[index].kind == CTOOL_C_EXPRESSION_CAST) {
      cast_index = index;
      break;
    }
  }
  if (cast_index == CTOOL_C_AST_NONE ||
      (malformed_unit.expression_count != 0u &&
       sizeof(*invalid_expressions) >
           SIZE_MAX / (size_t)malformed_unit.expression_count)) {
    (void)fprintf(stderr, "malformed void-cast fixture differs\n");
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)malformed_unit.expression_count * sizeof(*invalid_expressions));
  if (invalid_expressions == NULL) {
    (void)fprintf(stderr, "malformed void-cast fixture allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, malformed_unit.expressions,
               (size_t)malformed_unit.expression_count *
                   sizeof(*invalid_expressions));
  invalid_expressions[cast_index].operation =
      CTOOL_C_EXPRESSION_OPERATOR_ADD;
  invalid_unit = malformed_unit;
  invalid_unit.expressions = invalid_expressions;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "void cast with an operator payload")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_expressions);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("void-casts: ok");
    return 0;
  }
  return 1;
}

static int run_structure_values(const char *host_root) {
  static const char source[] =
      "typedef struct { int left; int right; } pair_t;\n"
      "pair_t copy_pair(pair_t value) { return value; }\n"
      "pair_t choose_pair(int condition, pair_t left, pair_t right) {\n"
      "  return condition ? left : right;\n"
      "}\n"
      "pair_t assign_pair(pair_t *target, pair_t value) {\n"
      "  return *target = value;\n"
      "}\n"
      "pair_t chain_pair(pair_t *first, pair_t *second, pair_t value) {\n"
      "  return *first = *second = value;\n"
      "}\n"
      "pair_t initialize_pair(pair_t value) {\n"
      "  pair_t local = value;\n"
      "  return copy_pair(local);\n"
      "}\n"
      "pair_t indirect_pair(pair_t (*function)(pair_t), pair_t value) {\n"
      "  return function(value);\n"
      "}\n"
      "void discard_pair(pair_t value) { value; }\n";
  static const char union_source[] =
      "typedef union { int word; unsigned int bits; } choice_t;\n"
      "choice_t copy_choice(choice_t value) { return value; }\n";
  static const char volatile_source[] =
      "typedef struct { int left; int right; } pair_t;\n"
      "pair_t read_pair(volatile pair_t *value) { return *value; }\n";
  static const char aligned_source[] =
      "typedef struct { int left; int right; } aligned_pair "
      "__attribute__((aligned(8)));\n"
      "aligned_pair copy_aligned(aligned_pair value) { return value; }\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t union_unit;
  ctool_c_translation_unit_t volatile_unit;
  ctool_c_translation_unit_t aligned_unit;
  ctool_c_ir_unit_t ir;
  ctool_u32 aggregate_loads = 0u;
  ctool_u32 aggregate_stores = 0u;
  ctool_u32 aggregate_calls = 0u;
  ctool_u32 aggregate_returns = 0u;
  ctool_u32 aggregate_parameters = 0u;
  ctool_u32 aggregate_discards = 0u;
  ctool_u32 aggregate_value_stores = 0u;
  ctool_u32 direct_calls = 0u;
  ctool_u32 indirect_calls = 0u;
  ctool_bool aggregate_payloads_valid = CTOOL_TRUE;
  ctool_u32 index;
  ctool_u32 diagnostic_count;
  uint64_t fingerprint;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&union_unit, 0, sizeof(union_unit));
  (void)memset(&volatile_unit, 0, sizeof(volatile_unit));
  (void)memset(&aligned_unit, 0, sizeof(aligned_unit));
  (void)memset(&ir, 0xa5, sizeof(ir));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/structure-values.c", source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "structure value lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint || ir.function_count != 7u ||
      ir.functions == NULL || ir.instructions == NULL) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < ir.instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction = &ir.instructions[index];
    const ctool_c_type_node_t *type =
        instruction->type < unit.graph.type_count
            ? &unit.graph.types[instruction->type]
            : NULL;
    const ctool_c_type_node_t *input_type =
        instruction->input_type < unit.graph.type_count
            ? &unit.graph.types[instruction->input_type]
            : NULL;
    while (type != NULL &&
           (type->kind == CTOOL_C_TYPE_ALIGNED ||
            type->kind == CTOOL_C_TYPE_QUALIFIED)) {
      type = type->referenced_type < unit.graph.type_count
                 ? &unit.graph.types[type->referenced_type]
                 : NULL;
    }
    while (input_type != NULL &&
           (input_type->kind == CTOOL_C_TYPE_ALIGNED ||
            input_type->kind == CTOOL_C_TYPE_QUALIFIED)) {
      input_type = input_type->referenced_type < unit.graph.type_count
                       ? &unit.graph.types[input_type->referenced_type]
                       : NULL;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS &&
        type != NULL && type->kind == CTOOL_C_TYPE_RECORD) {
      aggregate_parameters++;
      if (instruction->input_type != CTOOL_C_TYPE_NONE ||
          instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          instruction->reference == CTOOL_C_AST_NONE ||
          instruction->integer_bits != 0u) {
        aggregate_payloads_valid = CTOOL_FALSE;
      }
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD &&
               type != NULL && type->kind == CTOOL_C_TYPE_RECORD) {
      aggregate_loads++;
      if (input_type != type ||
          instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
          instruction->reference != CTOOL_C_AST_NONE ||
          instruction->integer_bits != 0u) {
        aggregate_payloads_valid = CTOOL_FALSE;
      }
    } else if ((instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE ||
                instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE_VALUE) &&
               type != NULL && type->kind == CTOOL_C_TYPE_RECORD) {
      aggregate_stores++;
      if (instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE_VALUE) {
        aggregate_value_stores++;
      }
      if (input_type != type ||
          instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          instruction->reference != CTOOL_C_AST_NONE ||
          instruction->integer_bits != 0u) {
        aggregate_payloads_valid = CTOOL_FALSE;
      }
    } else if ((instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
                instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT) &&
               type != NULL && type->kind == CTOOL_C_TYPE_RECORD) {
      aggregate_calls++;
      if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT) {
        direct_calls++;
      } else {
        indirect_calls++;
      }
      if (instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT
               ? instruction->reference == CTOOL_C_AST_NONE
               : instruction->reference != CTOOL_C_AST_NONE) ||
          instruction->integer_bits != 0u) {
        aggregate_payloads_valid = CTOOL_FALSE;
      }
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_RETURN_VALUE &&
               type != NULL && type->kind == CTOOL_C_TYPE_RECORD) {
      aggregate_returns++;
      if (input_type != type ||
          instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          instruction->reference != CTOOL_C_AST_NONE ||
          instruction->integer_bits != 0u) {
        aggregate_payloads_valid = CTOOL_FALSE;
      }
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_DISCARD &&
               input_type != NULL &&
               input_type->kind == CTOOL_C_TYPE_RECORD) {
      aggregate_discards++;
      if (instruction->type != CTOOL_C_TYPE_NONE ||
          instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          instruction->reference != CTOOL_C_AST_NONE ||
          instruction->integer_bits != 0u) {
        aggregate_payloads_valid = CTOOL_FALSE;
      }
    }
  }
  if (aggregate_payloads_valid == CTOOL_FALSE || aggregate_parameters != 8u ||
      aggregate_loads != 9u || aggregate_stores != 4u ||
      aggregate_value_stores != 3u || aggregate_calls != 2u ||
      direct_calls != 1u || indirect_calls != 1u ||
      aggregate_returns != 6u || aggregate_discards != 1u) {
    (void)fprintf(stderr, "structure value IR inventory differs\n");
    goto cleanup;
  }
  if (!parse_source(job, "/union-value.c", union_source, &union_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &union_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports cdecl functions with represented "
          "scalar or structure parameters and void, scalar, or structure "
          "results",
          "union value ABI") ||
      !parse_source(job, "/volatile-structure-value.c", volatile_source,
                    &volatile_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &volatile_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "volatile structure value") ||
      !parse_source_mode(job, "/aligned-structure-value.c", aligned_source,
                         CTOOL_TRUE, &aligned_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &aligned_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "over-aligned structure value")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("structure-values: ok");
    return 0;
  }
  return 1;
}

static int compound_literal_function_identity(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir,
    ctool_u32 function_index, ctool_u32 expected_address_count,
    ctool_u32 *identity_out) {
  const ctool_c_ir_function_t *function;
  ctool_u32 identity = CTOOL_C_AST_NONE;
  ctool_u32 address_count = 0u;
  ctool_u32 offset;
  if (function_index >= ir->function_count || identity_out == NULL) {
    return 0;
  }
  function = &ir->functions[function_index];
  if (function->first_instruction > ir->instruction_count ||
      function->instruction_count >
          ir->instruction_count - function->first_instruction) {
    return 0;
  }
  for (offset = 0u; offset < function->instruction_count; offset++) {
    const ctool_c_ir_instruction_t *instruction =
        &ir->instructions[function->first_instruction + offset];
    if (instruction->kind !=
            CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS &&
        instruction->kind !=
            CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS) {
      continue;
    }
    if (instruction->reference >= unit->expression_count ||
        unit->expressions[instruction->reference].kind !=
            CTOOL_C_EXPRESSION_COMPOUND_LITERAL ||
        instruction->type != unit->expressions[instruction->reference].type ||
        instruction->input_type != CTOOL_C_TYPE_NONE ||
        instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        instruction->integer_bits != 0u ||
        (identity != CTOOL_C_AST_NONE &&
         instruction->reference != identity)) {
      return 0;
    }
    identity = instruction->reference;
    address_count++;
  }
  if (identity == CTOOL_C_AST_NONE ||
      address_count != expected_address_count) {
    return 0;
  }
  *identity_out = identity;
  return 1;
}

static int validate_compound_literal_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir,
    ctool_u32 identities[4]) {
  static const char *const function_names[] = {
      "active_shape", "scalar_value", "scalar_address", "reinitialize"};
  static const ctool_u32 function_counts[] = {21u, 7u, 7u, 22u};
  static const ctool_u32 stack_depths[] = {3u, 2u, 2u, 3u};
  static const ctool_u32 address_counts[] = {6u, 2u, 2u, 2u};
  static const ctool_c_ir_instruction_kind_t expected_kinds[] = {
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT,
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_STORE,
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_STORE,
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_COPY_OBJECT,
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_STORE,
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_STORE,
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_ADDRESS_OF,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_STORE,
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_STORE_VALUE,
      CTOOL_C_IR_INSTRUCTION_DISCARD,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_INTEGER,
      CTOOL_C_IR_INSTRUCTION_BINARY,
      CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_INTEGER,
      CTOOL_C_IR_INSTRUCTION_STORE_VALUE,
      CTOOL_C_IR_INSTRUCTION_DISCARD,
      CTOOL_C_IR_INSTRUCTION_JUMP,
      CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
      CTOOL_C_IR_INSTRUCTION_LOAD,
      CTOOL_C_IR_INSTRUCTION_RETURN_VALUE};
  const ctool_c_ir_instruction_t *active;
  const ctool_c_ir_instruction_t *scalar;
  const ctool_c_ir_instruction_t *address;
  const ctool_c_ir_instruction_t *loop;
  const ctool_c_type_node_t *function_type;
  ctool_u32 first = 0u;
  ctool_u32 active_parameter;
  ctool_u32 data_member = find_member(unit, "data");
  ctool_u32 size_member = find_member(unit, "size");
  ctool_u32 equal_binding = find_binding(unit, "pp_string_equal");
  ctool_u32 index;
  if (unit->function_definition_count != 4u || ir->function_count != 4u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected_kinds) / sizeof(expected_kinds[0])) ||
      data_member == CTOOL_C_AST_NONE || size_member == CTOOL_C_AST_NONE ||
      equal_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "compound literal IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < 4u; index++) {
    ctool_u32 binding = find_binding(unit, function_names[index]);
    const ctool_c_ir_function_t *function = &ir->functions[index];
    if (binding == CTOOL_C_AST_NONE ||
        unit->function_definitions[index].binding != binding ||
        function->binding != binding ||
        function->declared_type !=
            unit->function_definitions[index].declared_type ||
        function->first_instruction != first ||
        function->instruction_count != function_counts[index] ||
        function->maximum_stack_depth != stack_depths[index] ||
        !compound_literal_function_identity(
            unit, ir, index, address_counts[index], &identities[index])) {
      (void)fprintf(stderr, "compound literal function %u differs\n",
                    (unsigned int)index);
      return 0;
    }
    first += function_counts[index];
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction = &ir->instructions[index];
    if (instruction->kind != expected_kinds[index] ||
        !string_equal(instruction->location.path, "/compound-literals.c") ||
        !string_equal(instruction->physical_location.path,
                      "/compound-literals.c") ||
        instruction->location.line == 0u ||
        instruction->physical_location.line == 0u) {
      (void)fprintf(stderr, "compound literal instruction %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }
  for (index = 0u; index < 4u; index++) {
    ctool_u32 other;
    const ctool_c_expression_t *expression =
        &unit->expressions[identities[index]];
    if (expression->reference >= unit->initializer_count ||
        unit->initializers[expression->reference].type != expression->type) {
      (void)fprintf(stderr, "compound literal root %u differs\n",
                    (unsigned int)index);
      return 0;
    }
    for (other = 0u; other < index; other++) {
      if (identities[other] == identities[index]) {
        (void)fprintf(stderr, "compound literal identities overlap\n");
        return 0;
      }
    }
  }
  if (unit->initializers[unit->expressions[identities[0]].reference].kind !=
          CTOOL_C_INITIALIZER_LIST ||
      unit->initializers[unit->expressions[identities[0]].reference]
              .element_count != 2u) {
    (void)fprintf(stderr, "aggregate compound literal root differs\n");
    return 0;
  }
  for (index = 1u; index < 4u; index++) {
    const ctool_c_initializer_t *initializer =
        &unit->initializers[unit->expressions[identities[index]].reference];
    if (initializer->kind != CTOOL_C_INITIALIZER_EXPRESSION ||
        initializer->expression >= identities[index]) {
      (void)fprintf(stderr, "scalar compound literal root %u differs\n",
                    (unsigned int)index);
      return 0;
    }
  }

  active = &ir->instructions[ir->functions[0].first_instruction];
  if (unit->function_definitions[0].declared_type >=
      unit->graph.type_count) {
    return 0;
  }
  function_type =
      &unit->graph.types[unit->function_definitions[0].declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 3u ||
      unit->parameter_count < 3u ||
      function_type->first_parameter > unit->parameter_count - 3u) {
    return 0;
  }
  active_parameter = function_type->first_parameter;
  if (active[0].reference != active_parameter ||
      active[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      active[2].reference != identities[0] ||
      active[3].type != unit->expressions[identities[0]].type ||
      active[3].input_type != active[3].type ||
      active[4].reference != identities[0] ||
      active[5].reference != data_member ||
      active[6].reference != active_parameter + 1u ||
      active[7].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      active[8].type != unit->graph.members[data_member].type ||
      active[9].reference != identities[0] ||
      active[10].reference != size_member ||
      active[11].reference != active_parameter + 2u ||
      active[12].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      active[13].type != unit->graph.members[size_member].type ||
      active[14].reference != identities[0] ||
      active[15].reference != identities[0] ||
      active[16].type != unit->expressions[identities[0]].type ||
      active[16].input_type != active[16].type ||
      active[17].reference != identities[0] ||
      active[18].type != unit->expressions[identities[0]].type ||
      active[18].input_type != active[18].type ||
      active[18].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      active[19].reference != equal_binding) {
    (void)fprintf(stderr, "aggregate compound literal lowering differs\n");
    return 0;
  }

  scalar = &ir->instructions[ir->functions[1].first_instruction];
  address = &ir->instructions[ir->functions[2].first_instruction];
  loop = &ir->instructions[ir->functions[3].first_instruction];
  if (scalar[0].reference != identities[1] ||
      scalar[4].reference != identities[1] ||
      scalar[5].type != unit->expressions[identities[1]].type ||
      scalar[5].input_type != scalar[5].type ||
      scalar[5].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      address[0].reference != identities[2] ||
      address[4].reference != identities[2] ||
      address[5].input_type != unit->expressions[identities[2]].type ||
      address[5].operation != CTOOL_C_EXPRESSION_OPERATOR_ADDRESS ||
      address[5].conversion != CTOOL_C_CONVERSION_NONE ||
      loop[1].reference != identities[3] ||
      loop[5].reference != identities[3] ||
      loop[6].type != unit->expressions[identities[3]].type ||
      loop[6].input_type != loop[6].type ||
      loop[6].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      loop[13].reference != 19u || loop[18].reference >= 1u) {
    (void)fprintf(stderr, "scalar compound literal lowering differs\n");
    return 0;
  }
  return 1;
}

static int validate_compound_literal_array_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_function_t *function;
  const ctool_c_expression_t *expression;
  const ctool_c_type_node_t *array_type;
  ctool_u32 identity = CTOOL_C_AST_NONE;
  ctool_u32 zero = CTOOL_C_AST_NONE;
  ctool_u32 copy = CTOOL_C_AST_NONE;
  ctool_u32 last_load = CTOOL_C_AST_NONE;
  ctool_u32 object_address_count = 0u;
  ctool_u32 staging_address_count = 0u;
  ctool_u32 element_count = 0u;
  ctool_u32 store_count = 0u;
  ctool_u32 index;
  if (unit == NULL || ir == NULL || unit->function_definition_count != 1u ||
      ir->function_count != 1u || ir->functions == NULL ||
      ir->instructions == NULL) {
    return 0;
  }
  for (index = 0u; index < unit->expression_count; index++) {
    if (unit->expressions[index].kind !=
        CTOOL_C_EXPRESSION_COMPOUND_LITERAL) {
      continue;
    }
    if (identity != CTOOL_C_AST_NONE) {
      return 0;
    }
    identity = index;
  }
  if (identity == CTOOL_C_AST_NONE) {
    return 0;
  }
  expression = &unit->expressions[identity];
  if (expression->reference >= unit->initializer_count ||
      expression->type >= unit->graph.type_count ||
      unit->initializers[expression->reference].kind !=
          CTOOL_C_INITIALIZER_LIST ||
      unit->initializers[expression->reference].element_count != 2u) {
    return 0;
  }
  array_type = &unit->graph.types[expression->type];
  if (array_type->kind != CTOOL_C_TYPE_ARRAY ||
      array_type->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
      array_type->element_count != 2u ||
      array_type->referenced_type >= unit->graph.type_count) {
    return 0;
  }
  function = &ir->functions[0];
  if (function->first_instruction > ir->instruction_count ||
      function->instruction_count >
          ir->instruction_count - function->first_instruction) {
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction =
        &ir->instructions[function->first_instruction + index];
    if (instruction->kind ==
        CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS) {
      if (instruction->reference != identity) {
        return 0;
      }
      object_address_count++;
    } else if (instruction->kind ==
               CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS) {
      if (instruction->reference != identity) {
        return 0;
      }
      staging_address_count++;
    } else if (instruction->kind ==
               CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT) {
      const ctool_c_ir_instruction_t *address =
          index == 0u
              ? (const ctool_c_ir_instruction_t *)0
              : &ir->instructions[function->first_instruction + index - 1u];
      if (zero != CTOOL_C_AST_NONE ||
          address == (const ctool_c_ir_instruction_t *)0 ||
          address->kind !=
              CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS ||
          address->reference != identity ||
          instruction->type != expression->type ||
          instruction->input_type != expression->type) {
        return 0;
      }
      zero = index;
    } else if (instruction->kind ==
               CTOOL_C_IR_INSTRUCTION_ELEMENT_ADDRESS) {
      const ctool_c_ir_instruction_t *address =
          index == 0u
              ? (const ctool_c_ir_instruction_t *)0
              : &ir->instructions[function->first_instruction + index - 1u];
      if (element_count >= 2u ||
          address == (const ctool_c_ir_instruction_t *)0 ||
          address->kind !=
              CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS ||
          address->reference != identity ||
          instruction->reference != element_count ||
          instruction->type != array_type->referenced_type ||
          instruction->input_type != expression->type) {
        return 0;
      }
      element_count++;
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE) {
      if (instruction->type != array_type->referenced_type) {
        return 0;
      }
      store_count++;
    } else if (instruction->kind ==
               CTOOL_C_IR_INSTRUCTION_COPY_OBJECT) {
      const ctool_c_ir_instruction_t *destination =
          index < 2u
              ? (const ctool_c_ir_instruction_t *)0
              : &ir->instructions[function->first_instruction + index - 2u];
      const ctool_c_ir_instruction_t *source =
          index == 0u
              ? (const ctool_c_ir_instruction_t *)0
              : &ir->instructions[function->first_instruction + index - 1u];
      if (copy != CTOOL_C_AST_NONE ||
          destination == (const ctool_c_ir_instruction_t *)0 ||
          source == (const ctool_c_ir_instruction_t *)0 ||
          destination->kind !=
              CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS ||
          source->kind !=
              CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS ||
          destination->reference != identity ||
          source->reference != identity) {
        return 0;
      }
      copy = index;
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD) {
      last_load = index;
    }
  }
  if (object_address_count != 2u || staging_address_count != 4u ||
      element_count != 2u || store_count != 2u ||
      zero == CTOOL_C_AST_NONE || copy == CTOOL_C_AST_NONE ||
      last_load == CTOOL_C_AST_NONE || zero >= copy || copy >= last_load) {
    (void)fprintf(stderr, "array compound literal lowering differs\n");
    return 0;
  }
  return 1;
}

static int validate_compound_literal_alias_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_function_t *function;
  ctool_u32 identity = CTOOL_C_AST_NONE;
  ctool_u32 load = CTOOL_C_AST_NONE;
  ctool_u32 zero = CTOOL_C_AST_NONE;
  ctool_u32 first_store = CTOOL_C_AST_NONE;
  ctool_u32 last_store = CTOOL_C_AST_NONE;
  ctool_u32 copy = CTOOL_C_AST_NONE;
  ctool_u32 store_count = 0u;
  ctool_u32 object_address_count = 0u;
  ctool_u32 staging_address_count = 0u;
  ctool_u32 index;
  if (unit == NULL || ir == NULL || unit->function_definition_count != 1u ||
      ir->function_count != 1u || ir->functions == NULL ||
      ir->instructions == NULL) {
    return 0;
  }
  for (index = 0u; index < unit->expression_count; index++) {
    if (unit->expressions[index].kind !=
        CTOOL_C_EXPRESSION_COMPOUND_LITERAL) {
      continue;
    }
    if (identity != CTOOL_C_AST_NONE) {
      return 0;
    }
    identity = index;
  }
  if (identity == CTOOL_C_AST_NONE ||
      unit->expressions[identity].reference >= unit->initializer_count ||
      unit->initializers[unit->expressions[identity].reference].kind !=
          CTOOL_C_INITIALIZER_LIST ||
      unit->initializers[unit->expressions[identity].reference]
              .element_count != 2u) {
    return 0;
  }
  function = &ir->functions[0];
  if (function->first_instruction > ir->instruction_count ||
      function->instruction_count >
          ir->instruction_count - function->first_instruction) {
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction =
        &ir->instructions[function->first_instruction + index];
    if (instruction->kind ==
        CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS) {
      if (instruction->reference != identity) {
        return 0;
      }
      object_address_count++;
    } else if (instruction->kind ==
               CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS) {
      if (instruction->reference != identity) {
        return 0;
      }
      staging_address_count++;
    }
    if (instruction->location.line != 6u) {
      continue;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD) {
      load = index;
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT) {
      const ctool_c_ir_instruction_t *address =
          index == 0u
              ? (const ctool_c_ir_instruction_t *)0
              : &ir->instructions[function->first_instruction + index - 1u];
      if (zero != CTOOL_C_AST_NONE ||
          address == (const ctool_c_ir_instruction_t *)0 ||
          address->kind !=
              CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS ||
          address->reference != identity) {
        return 0;
      }
      zero = index;
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE) {
      if (first_store == CTOOL_C_AST_NONE) {
        first_store = index;
      }
      last_store = index;
      store_count++;
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_COPY_OBJECT) {
      const ctool_c_ir_instruction_t *destination =
          index < 2u
              ? (const ctool_c_ir_instruction_t *)0
              : &ir->instructions[function->first_instruction + index - 2u];
      const ctool_c_ir_instruction_t *source =
          index == 0u
              ? (const ctool_c_ir_instruction_t *)0
              : &ir->instructions[function->first_instruction + index - 1u];
      if (copy != CTOOL_C_AST_NONE ||
          destination == (const ctool_c_ir_instruction_t *)0 ||
          source == (const ctool_c_ir_instruction_t *)0 ||
          destination->kind !=
              CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS ||
          source->kind !=
              CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS ||
          destination->reference != identity ||
          source->reference != identity) {
        return 0;
      }
      copy = index;
    }
  }
  if (object_address_count != 2u || staging_address_count != 4u ||
      load == CTOOL_C_AST_NONE || zero == CTOOL_C_AST_NONE ||
      first_store == CTOOL_C_AST_NONE || last_store == CTOOL_C_AST_NONE ||
      copy == CTOOL_C_AST_NONE || store_count != 2u ||
      zero >= first_store || first_store >= load || load >= last_store ||
      last_store >= copy) {
    (void)fprintf(
        stderr,
        "compound literal alias read was not ordered between staged stores "
        "before commit\n");
    return 0;
  }
  return 1;
}

static int expect_compound_literal_payload_rejections(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_c_translation_unit_t *invalid_unit,
    ctool_c_expression_t *invalid_expressions, ctool_u32 identity) {
  static const char *const contexts[] = {
      "compound literal first child payload",
      "compound literal child count payload",
      "compound literal conversion payload",
      "compound literal operation payload",
      "compound literal computation type payload",
      "compound literal integer payload",
      "compound literal string data payload",
      "compound literal string size payload"};
  static const ctool_u8 nonempty_string_byte = 0u;
  ctool_u32 index;
  if (job == NULL || unit == NULL || invalid_unit == NULL ||
      invalid_expressions == NULL || identity >= unit->expression_count) {
    return 0;
  }
  for (index = 0u; index < 8u; index++) {
    ctool_c_expression_t *expression;
    (void)memcpy(invalid_expressions, unit->expressions,
                 (size_t)unit->expression_count *
                     sizeof(*invalid_expressions));
    expression = &invalid_expressions[identity];
    if (index == 0u) {
      expression->first_child = 0u;
    } else if (index == 1u) {
      expression->child_count = 1u;
    } else if (index == 2u) {
      expression->conversion = CTOOL_C_CONVERSION_LVALUE_TO_VALUE;
    } else if (index == 3u) {
      expression->operation = CTOOL_C_EXPRESSION_OPERATOR_ADDRESS;
    } else if (index == 4u) {
      expression->computation_type = expression->type;
    } else if (index == 5u) {
      expression->integer_bits = 1u;
    } else if (index == 6u) {
      expression->string_bytes.data = &nonempty_string_byte;
    } else {
      expression->string_bytes.size = 1u;
    }
    if (!expect_ir_failure_preserves_unit(
            job, invalid_unit, CTOOL_ERR_INPUT,
            CTOOL_C_IR_DIAG_INVALID_UNIT,
            "CupidC IR lowering received an invalid translation unit",
            contexts[index])) {
      return 0;
    }
  }
  return 1;
}

typedef struct {
  ctool_c_translation_unit_t unit;
  ctool_c_expression_t *expressions;
  ctool_c_initializer_t *initializers;
  ctool_u32 *expression_children;
  ctool_c_statement_t *statements;
} compound_literal_depth_fixture_t;

static void free_compound_literal_depth_fixture(
    compound_literal_depth_fixture_t *fixture) {
  if (fixture == NULL) {
    return;
  }
  free(fixture->statements);
  free(fixture->expression_children);
  free(fixture->initializers);
  free(fixture->expressions);
  (void)memset(fixture, 0, sizeof(*fixture));
}

static int make_compound_literal_depth_fixture(
    const ctool_c_translation_unit_t *base,
    compound_literal_depth_fixture_t *fixture) {
  const ctool_u32 additional_layers =
      CTOOL_C_PARSE_NESTING_LIMIT / 2u + 1u;
  ctool_u32 compound = CTOOL_C_AST_NONE;
  ctool_u32 conversion = CTOOL_C_AST_NONE;
  ctool_u32 return_statement = CTOOL_C_AST_NONE;
  ctool_u32 initializer;
  ctool_u32 expression_count;
  ctool_u32 initializer_count;
  ctool_u32 expression_child_count;
  ctool_u32 previous_value;
  ctool_u32 index;
  if (base == NULL || fixture == NULL) {
    return 0;
  }
  (void)memset(fixture, 0, sizeof(*fixture));
  for (index = 0u; index < base->expression_count; index++) {
    if (base->expressions[index].kind !=
        CTOOL_C_EXPRESSION_COMPOUND_LITERAL) {
      continue;
    }
    if (compound != CTOOL_C_AST_NONE) {
      return 0;
    }
    compound = index;
  }
  if (compound == CTOOL_C_AST_NONE) {
    return 0;
  }
  for (index = 0u; index < base->expression_count; index++) {
    const ctool_c_expression_t *expression = &base->expressions[index];
    if (expression->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
        expression->conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
        expression->child_count != 1u ||
        expression->first_child >= base->expression_child_count ||
        base->expression_children[expression->first_child] != compound) {
      continue;
    }
    if (conversion != CTOOL_C_AST_NONE) {
      return 0;
    }
    conversion = index;
  }
  if (conversion == CTOOL_C_AST_NONE) {
    return 0;
  }
  for (index = 0u; index < base->statement_count; index++) {
    if (base->statements[index].kind != CTOOL_C_STATEMENT_RETURN ||
        base->statements[index].expression != conversion) {
      continue;
    }
    if (return_statement != CTOOL_C_AST_NONE) {
      return 0;
    }
    return_statement = index;
  }
  initializer = base->expressions[compound].reference;
  if (return_statement == CTOOL_C_AST_NONE ||
      initializer >= base->initializer_count ||
      base->initializers[initializer].kind !=
          CTOOL_C_INITIALIZER_EXPRESSION ||
      additional_layers >
          (0xffffffffu - base->expression_count) / 2u ||
      additional_layers > 0xffffffffu - base->initializer_count ||
      additional_layers > 0xffffffffu - base->expression_child_count) {
    return 0;
  }
  expression_count = base->expression_count + additional_layers * 2u;
  initializer_count = base->initializer_count + additional_layers;
  expression_child_count =
      base->expression_child_count + additional_layers;
  if (sizeof(*fixture->expressions) >
          SIZE_MAX / (size_t)expression_count ||
      sizeof(*fixture->initializers) >
          SIZE_MAX / (size_t)initializer_count ||
      sizeof(*fixture->expression_children) >
          SIZE_MAX / (size_t)expression_child_count ||
      base->statement_count == 0u ||
      sizeof(*fixture->statements) >
          SIZE_MAX / (size_t)base->statement_count) {
    return 0;
  }
  fixture->expressions = (ctool_c_expression_t *)malloc(
      (size_t)expression_count * sizeof(*fixture->expressions));
  fixture->initializers = (ctool_c_initializer_t *)malloc(
      (size_t)initializer_count * sizeof(*fixture->initializers));
  fixture->expression_children = (ctool_u32 *)malloc(
      (size_t)expression_child_count *
      sizeof(*fixture->expression_children));
  fixture->statements = (ctool_c_statement_t *)malloc(
      (size_t)base->statement_count * sizeof(*fixture->statements));
  if (fixture->expressions == NULL || fixture->initializers == NULL ||
      fixture->expression_children == NULL || fixture->statements == NULL) {
    free_compound_literal_depth_fixture(fixture);
    return 0;
  }
  (void)memcpy(fixture->expressions, base->expressions,
               (size_t)base->expression_count *
                   sizeof(*fixture->expressions));
  (void)memcpy(fixture->initializers, base->initializers,
               (size_t)base->initializer_count *
                   sizeof(*fixture->initializers));
  (void)memcpy(fixture->expression_children, base->expression_children,
               (size_t)base->expression_child_count *
                   sizeof(*fixture->expression_children));
  (void)memcpy(fixture->statements, base->statements,
               (size_t)base->statement_count * sizeof(*fixture->statements));
  previous_value = conversion;
  for (index = 0u; index < additional_layers; index++) {
    ctool_u32 new_initializer = base->initializer_count + index;
    ctool_u32 new_compound = base->expression_count + index * 2u;
    ctool_u32 new_conversion = new_compound + 1u;
    ctool_u32 new_child = base->expression_child_count + index;
    fixture->initializers[new_initializer] = base->initializers[initializer];
    fixture->initializers[new_initializer].expression = previous_value;
    fixture->expressions[new_compound] = base->expressions[compound];
    fixture->expressions[new_compound].reference = new_initializer;
    fixture->expressions[new_conversion] = base->expressions[conversion];
    fixture->expressions[new_conversion].first_child = new_child;
    fixture->expression_children[new_child] = new_compound;
    previous_value = new_conversion;
  }
  fixture->statements[return_statement].expression = previous_value;
  fixture->unit = *base;
  fixture->unit.expressions = fixture->expressions;
  fixture->unit.expression_count = expression_count;
  fixture->unit.initializers = fixture->initializers;
  fixture->unit.initializer_count = initializer_count;
  fixture->unit.expression_children = fixture->expression_children;
  fixture->unit.expression_child_count = expression_child_count;
  fixture->unit.statements = fixture->statements;
  return 1;
}

static int validate_string_compound_literal_frontend(
    const ctool_c_translation_unit_t *unit) {
  const ctool_c_expression_t *literal = NULL;
  const ctool_c_initializer_t *initializer;
  const ctool_c_type_node_t *array;
  ctool_u32 index;
  ctool_u32 literal_count = 0u;
  if (unit == NULL) {
    return 0;
  }
  for (index = 0u; index < unit->expression_count; index++) {
    if (unit->expressions[index].kind ==
        CTOOL_C_EXPRESSION_COMPOUND_LITERAL) {
      literal = &unit->expressions[index];
      literal_count++;
    }
  }
  if (literal_count != 1u || literal == NULL ||
      literal->reference >= unit->initializer_count ||
      literal->type >= unit->graph.type_count) {
    (void)fprintf(stderr, "string compound literal inventory differs\n");
    return 0;
  }
  initializer = &unit->initializers[literal->reference];
  array = &unit->graph.types[literal->type];
  if (initializer->kind != CTOOL_C_INITIALIZER_STRING ||
      initializer->type != literal->type ||
      initializer->string_bytes.size != 6u ||
      initializer->string_bytes.data == NULL ||
      memcmp(initializer->string_bytes.data, "Cupid\0", 6u) != 0 ||
      array->kind != CTOOL_C_TYPE_ARRAY ||
      array->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
      array->element_count != 6u ||
      array->referenced_type >= unit->graph.type_count ||
      unit->graph.types[array->referenced_type].kind != CTOOL_C_TYPE_CHAR ||
      literal->first_child != CTOOL_C_AST_NONE || literal->child_count != 0u ||
      literal->conversion != CTOOL_C_CONVERSION_NONE ||
      literal->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      literal->computation_type != CTOOL_C_TYPE_NONE ||
      literal->integer_bits != 0ull || literal->string_bytes.data != NULL ||
      literal->string_bytes.size != 0u) {
    (void)fprintf(stderr, "string compound literal frontend differs\n");
    return 0;
  }
  return 1;
}

static int validate_string_compound_literal_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  ctool_u32 compound_identity = CTOOL_C_AST_NONE;
  ctool_u32 string_identity = CTOOL_C_AST_NONE;
  ctool_u32 compound_function = CTOOL_C_AST_NONE;
  ctool_u32 address_function = CTOOL_C_AST_NONE;
  ctool_u32 compound_binding = find_binding(unit, "string_literal");
  ctool_u32 address_binding = find_binding(unit, "string_address");
  ctool_u32 index;
  if (unit == NULL || ir == NULL || compound_binding == CTOOL_C_AST_NONE ||
      address_binding == CTOOL_C_AST_NONE ||
      unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->functions == NULL || ir->instructions == NULL) {
    return 0;
  }
  for (index = 0u; index < unit->expression_count; index++) {
    const ctool_c_expression_t *expression = &unit->expressions[index];
    if (expression->kind == CTOOL_C_EXPRESSION_COMPOUND_LITERAL) {
      if (compound_identity != CTOOL_C_AST_NONE) {
        return 0;
      }
      compound_identity = index;
    } else if (expression->kind == CTOOL_C_EXPRESSION_STRING) {
      if (string_identity != CTOOL_C_AST_NONE) {
        return 0;
      }
      string_identity = index;
    }
  }
  for (index = 0u; index < ir->function_count; index++) {
    if (ir->functions[index].binding == compound_binding) {
      compound_function = index;
    } else if (ir->functions[index].binding == address_binding) {
      address_function = index;
    }
  }
  if (compound_identity == CTOOL_C_AST_NONE ||
      string_identity == CTOOL_C_AST_NONE ||
      compound_function == CTOOL_C_AST_NONE ||
      address_function == CTOOL_C_AST_NONE ||
      unit->expressions[compound_identity].reference >=
          unit->initializer_count ||
      unit->initializers[unit->expressions[compound_identity].reference]
              .kind != CTOOL_C_INITIALIZER_STRING) {
    (void)fprintf(stderr, "runtime string IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < ir->function_count; index++) {
    const ctool_c_ir_function_t *function = &ir->functions[index];
    ctool_u32 compound_address_count = 0u;
    ctool_u32 string_address_count = 0u;
    ctool_u32 zero_count = 0u;
    ctool_u32 copy_count = 0u;
    ctool_u32 decay_count = 0u;
    ctool_u32 return_count = 0u;
    ctool_u32 offset;
    if (function->first_instruction > ir->instruction_count ||
        function->instruction_count >
            ir->instruction_count - function->first_instruction) {
      return 0;
    }
    for (offset = 0u; offset < function->instruction_count; offset++) {
      const ctool_c_ir_instruction_t *instruction =
          &ir->instructions[function->first_instruction + offset];
      if (!string_equal(instruction->location.path,
                        "/compound-literal-string.c") ||
          !string_equal(instruction->physical_location.path,
                        "/compound-literal-string.c")) {
        return 0;
      }
      if (instruction->kind ==
          CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS) {
        if (instruction->reference != compound_identity ||
            instruction->type != unit->expressions[compound_identity].type) {
          return 0;
        }
        compound_address_count++;
      } else if (instruction->kind ==
                 CTOOL_C_IR_INSTRUCTION_STRING_LITERAL_ADDRESS) {
        if (instruction->reference != string_identity ||
            instruction->type != unit->expressions[string_identity].type ||
            instruction->input_type != CTOOL_C_TYPE_NONE) {
          return 0;
        }
        string_address_count++;
      } else if (instruction->kind ==
                 CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT) {
        if (instruction->type != unit->expressions[compound_identity].type ||
            instruction->input_type != instruction->type) {
          return 0;
        }
        zero_count++;
      } else if (instruction->kind ==
                 CTOOL_C_IR_INSTRUCTION_COPY_STRING) {
        ctool_u32 initializer =
            unit->expressions[compound_identity].reference;
        if (instruction->reference != initializer ||
            instruction->type != unit->initializers[initializer].type ||
            instruction->input_type != instruction->type) {
          return 0;
        }
        copy_count++;
      } else if (instruction->kind ==
                 CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER) {
        decay_count++;
      } else if (instruction->kind ==
                 CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
        return_count++;
      }
    }
    if (index == compound_function) {
      if (compound_address_count != 3u || string_address_count != 0u ||
          zero_count != 1u || copy_count != 1u || decay_count != 1u ||
          return_count != 1u) {
        (void)fprintf(stderr,
                      "string compound literal lowering differs\n");
        return 0;
      }
    } else if (index == address_function) {
      if (compound_address_count != 0u || string_address_count != 1u ||
          zero_count != 0u || copy_count != 0u || decay_count != 1u ||
          return_count != 1u) {
        (void)fprintf(stderr, "runtime string address lowering differs\n");
        return 0;
      }
    } else {
      return 0;
    }
  }
  return 1;
}

static int expect_runtime_string_payload_rejections(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit) {
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_expression_t *expressions = NULL;
  ctool_c_initializer_t *initializers = NULL;
  ctool_u32 compound_identity = CTOOL_C_AST_NONE;
  ctool_u32 string_identity = CTOOL_C_AST_NONE;
  ctool_u32 initializer_identity;
  ctool_u32 index;
  int passed = 0;
  if (unit == NULL || unit->expression_count == 0u ||
      unit->initializer_count == 0u ||
      sizeof(*expressions) > SIZE_MAX / (size_t)unit->expression_count ||
      sizeof(*initializers) > SIZE_MAX / (size_t)unit->initializer_count) {
    return 0;
  }
  for (index = 0u; index < unit->expression_count; index++) {
    if (unit->expressions[index].kind ==
        CTOOL_C_EXPRESSION_COMPOUND_LITERAL) {
      compound_identity = index;
    } else if (unit->expressions[index].kind == CTOOL_C_EXPRESSION_STRING) {
      string_identity = index;
    }
  }
  if (compound_identity == CTOOL_C_AST_NONE ||
      string_identity == CTOOL_C_AST_NONE ||
      unit->expressions[compound_identity].reference >=
          unit->initializer_count) {
    return 0;
  }
  initializer_identity = unit->expressions[compound_identity].reference;
  expressions = (ctool_c_expression_t *)malloc(
      (size_t)unit->expression_count * sizeof(*expressions));
  initializers = (ctool_c_initializer_t *)malloc(
      (size_t)unit->initializer_count * sizeof(*initializers));
  if (expressions == NULL || initializers == NULL) {
    goto cleanup;
  }
  invalid_unit = *unit;
  invalid_unit.expressions = expressions;
  invalid_unit.initializers = initializers;
  (void)memcpy(expressions, unit->expressions,
               (size_t)unit->expression_count * sizeof(*expressions));
  (void)memcpy(initializers, unit->initializers,
               (size_t)unit->initializer_count * sizeof(*initializers));
  initializers[initializer_identity].string_bytes.size++;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "oversized automatic string initializer payload")) {
    goto cleanup;
  }
  (void)memcpy(expressions, unit->expressions,
               (size_t)unit->expression_count * sizeof(*expressions));
  (void)memcpy(initializers, unit->initializers,
               (size_t)unit->initializer_count * sizeof(*initializers));
  expressions[string_identity].string_bytes.size--;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "undersized runtime string expression payload")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(initializers);
  free(expressions);
  return passed;
}

static int run_compound_literals(const char *host_root) {
  static const char source[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef struct { const char *data; ctool_u32 size; } ctool_string_t;\n"
      "int pp_string_equal(ctool_string_t left, ctool_string_t right);\n"
      "int active_shape(ctool_string_t value, const char *literal, "
      "ctool_u32 size) {\n"
      "  return pp_string_equal(value, (ctool_string_t){literal, size});\n"
      "}\n"
      "ctool_u32 scalar_value(ctool_u32 seed) {\n"
      "  return (ctool_u32){seed};\n"
      "}\n"
      "ctool_u32 *scalar_address(ctool_u32 seed) {\n"
      "  return &(ctool_u32){seed};\n"
      "}\n"
      "ctool_u32 reinitialize(ctool_u32 seed) {\n"
      "again:\n"
      "  seed = (ctool_u32){seed};\n"
      "  if (seed != 0u) {\n"
      "    seed = 0u;\n"
      "    goto again;\n"
      "  }\n"
      "  return seed;\n"
      "}\n";
  static const char depth_source[] =
      "typedef unsigned int ctool_u32;\n"
      "ctool_u32 deep_literal(ctool_u32 seed) {\n"
      "  return (ctool_u32){seed};\n"
      "}\n";
  static const char array_source[] =
      "typedef unsigned int ctool_u32;\n"
      "ctool_u32 array_value(ctool_u32 left, ctool_u32 right) {\n"
      "  return ((ctool_u32[]){left, right})[1];\n"
      "}\n";
  static const char alias_source[] =
      "typedef struct { unsigned value; unsigned omitted; } pair_t;\n"
      "pair_t *escaped;\n"
      "unsigned alias_reinitialize(void) {\n"
      "  unsigned count = 0u;\n"
      "again:\n"
      "  escaped = &(pair_t){.value = escaped ? escaped->omitted + 1u : 1u, "
      ".omitted = escaped ? escaped->value + 2u : 2u};\n"
      "  count++;\n"
      "  if (count < 2u) goto again;\n"
      "  return escaped->value;\n"
      "}\n";
  static const char string_source[] =
      "char *string_literal(void) {\n"
      "  return (char[]){\"Cupid\"};\n"
      "}\n"
      "char *string_address(void) {\n"
      "  return \"Cupid\";\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_translation_unit_t depth_base_unit;
  ctool_c_translation_unit_t array_unit;
  ctool_c_translation_unit_t alias_unit;
  ctool_c_translation_unit_t string_unit;
  compound_literal_depth_fixture_t depth_fixture;
  ctool_c_ir_unit_t ir;
  ctool_c_ir_unit_t repeated_ir;
  ctool_c_ir_unit_t array_ir;
  ctool_c_ir_unit_t alias_ir;
  ctool_c_ir_unit_t string_ir;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_c_initializer_t *invalid_initializers = NULL;
  ctool_u32 identities[4];
  ctool_u32 aggregate_root;
  ctool_u32 scalar_root;
  ctool_u32 aggregate_child;
  ctool_u32 diagnostic_count;
  uint64_t fingerprint;
  uint64_t ir_fingerprint;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&depth_base_unit, 0, sizeof(depth_base_unit));
  (void)memset(&array_unit, 0, sizeof(array_unit));
  (void)memset(&alias_unit, 0, sizeof(alias_unit));
  (void)memset(&string_unit, 0, sizeof(string_unit));
  (void)memset(&depth_fixture, 0, sizeof(depth_fixture));
  (void)memset(&ir, 0xa5, sizeof(ir));
  (void)memset(&repeated_ir, 0xa5, sizeof(repeated_ir));
  (void)memset(&array_ir, 0xa5, sizeof(array_ir));
  (void)memset(&alias_ir, 0xa5, sizeof(alias_ir));
  (void)memset(&string_ir, 0xa5, sizeof(string_ir));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/compound-literals.c", source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "compound literal lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_compound_literal_ir(&unit, &ir, identities)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  ir_fingerprint = ir_instruction_fingerprint(&ir);
  status = ctool_c_lower_ir(job, &unit, &repeated_ir);
  if (!check_status(status, CTOOL_OK, "repeated compound literal lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      ir_instruction_fingerprint(&ir) != ir_fingerprint ||
      ir_instruction_fingerprint(&repeated_ir) != ir_fingerprint ||
      !validate_compound_literal_ir(&unit, &repeated_ir, identities)) {
    (void)ctool_job_render_diagnostics(job);
    (void)fprintf(stderr, "repeated compound literal IR differs\n");
    goto cleanup;
  }

  if (unit.expression_count == 0u || unit.initializer_count == 0u ||
      sizeof(*invalid_expressions) >
          SIZE_MAX / (size_t)unit.expression_count ||
      sizeof(*invalid_initializers) >
          SIZE_MAX / (size_t)unit.initializer_count) {
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_initializers = (ctool_c_initializer_t *)malloc(
      (size_t)unit.initializer_count * sizeof(*invalid_initializers));
  if (invalid_expressions == NULL || invalid_initializers == NULL) {
    goto cleanup;
  }
  invalid_unit = unit;
  invalid_unit.expressions = invalid_expressions;
  invalid_unit.initializers = invalid_initializers;
  (void)memcpy(invalid_initializers, unit.initializers,
               (size_t)unit.initializer_count * sizeof(*invalid_initializers));
  if (!expect_compound_literal_payload_rejections(
          job, &unit, &invalid_unit, invalid_expressions, identities[1])) {
    goto cleanup;
  }
  aggregate_root = unit.expressions[identities[0]].reference;
  scalar_root = unit.expressions[identities[1]].reference;
  if (aggregate_root >= unit.initializer_count ||
      scalar_root >= unit.initializer_count ||
      unit.initializers[aggregate_root].kind != CTOOL_C_INITIALIZER_LIST ||
      unit.initializers[aggregate_root].first_element >=
          unit.initializer_element_count ||
      unit.initializers[scalar_root].kind !=
          CTOOL_C_INITIALIZER_EXPRESSION) {
    goto cleanup;
  }
  aggregate_child = unit.initializer_elements
                        [unit.initializers[aggregate_root].first_element]
                            .initializer;
  if (aggregate_child >= aggregate_root) {
    goto cleanup;
  }

  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  (void)memcpy(invalid_initializers, unit.initializers,
               (size_t)unit.initializer_count * sizeof(*invalid_initializers));
  invalid_expressions[identities[0]].reference = aggregate_child;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "compound literal child used as root")) {
    goto cleanup;
  }

  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  (void)memcpy(invalid_initializers, unit.initializers,
               (size_t)unit.initializer_count * sizeof(*invalid_initializers));
  invalid_initializers[scalar_root].expression = unit.expression_count;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "compound literal initializer expression reference")) {
    goto cleanup;
  }

  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  (void)memcpy(invalid_initializers, unit.initializers,
               (size_t)unit.initializer_count * sizeof(*invalid_initializers));
  invalid_initializers[scalar_root].expression = identities[1];
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "compound literal initializer expression order")) {
    goto cleanup;
  }
  if (!parse_source(job, "/compound-literal-array.c", array_source,
                    &array_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&array_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &array_unit, &array_ir);
  if (!check_status(status, CTOOL_OK, "array compound literal lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&array_unit) != fingerprint ||
      !validate_compound_literal_array_ir(&array_unit, &array_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/compound-literal-depth.c", depth_source,
                    &depth_base_unit) ||
      !make_compound_literal_depth_fixture(&depth_base_unit,
                                           &depth_fixture) ||
      !expect_ir_failure_preserves_unit(
          job, &depth_fixture.unit, CTOOL_ERR_LIMIT,
          CTOOL_C_IR_DIAG_LIMIT,
          "CupidC IR lowering exceeded a configured resource limit",
          "compound literal expression nesting limit")) {
    goto cleanup;
  }
  if (!parse_source(job, "/compound-literal-alias.c", alias_source,
                    &alias_unit)) {
    goto cleanup;
  }
  status = ctool_c_lower_ir(job, &alias_unit, &alias_ir);
  if (!check_status(status, CTOOL_OK, "compound literal alias lowering") ||
      !validate_compound_literal_alias_ir(&alias_unit, &alias_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/compound-literal-string.c", string_source,
                    &string_unit) ||
      !validate_string_compound_literal_frontend(&string_unit) ||
      (status = ctool_c_lower_ir(job, &string_unit, &string_ir)) !=
          CTOOL_OK ||
      !validate_string_compound_literal_ir(&string_unit, &string_ir) ||
      !expect_runtime_string_payload_rejections(job, &string_unit)) {
    if (status != CTOOL_OK) {
      (void)ctool_job_render_diagnostics(job);
    }
    goto cleanup;
  }
  passed = 1;

cleanup:
  free_compound_literal_depth_fixture(&depth_fixture);
  free(invalid_initializers);
  free(invalid_expressions);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("compound-literals: ok");
    return 0;
  }
  return 1;
}

static int variadic_ir_instruction_is(
    const ctool_c_ir_instruction_t *instruction,
    ctool_c_ir_instruction_kind_t kind, ctool_u32 type,
    ctool_u32 input_type, ctool_u32 reference) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == CTOOL_C_EXPRESSION_OPERATOR_NONE &&
                 instruction->conversion ==
                     (kind == CTOOL_C_IR_INSTRUCTION_LOAD
                          ? CTOOL_C_CONVERSION_LVALUE_TO_VALUE
                          : CTOOL_C_CONVERSION_NONE) &&
                 instruction->argument_count == 0u &&
                 instruction->reference == reference &&
                 instruction->integer_bits == 0u
             ? 1
             : 0;
}

static int validate_old_style_empty_ir(const ctool_c_translation_unit_t *unit,
                                       const ctool_c_ir_unit_t *ir) {
  ctool_u32 tick = find_binding(unit, "tick");
  ctool_u32 invoke = find_binding(unit, "invoke");
  ctool_u32 consume = find_binding(unit, "consume");
  ctool_u32 promoted = find_binding(unit, "invoke_promoted");
  ctool_u32 indirect = find_binding(unit, "invoke_indirect");
  const ctool_c_function_definition_t *tick_definition =
      unit->function_definition_count == 4u
          ? &unit->function_definitions[0]
          : NULL;
  const ctool_c_function_definition_t *invoke_definition =
      unit->function_definition_count == 4u
          ? &unit->function_definitions[1]
          : NULL;
  const ctool_c_type_node_t *tick_type =
      tick_definition != NULL &&
              tick_definition->declared_type < unit->graph.type_count
          ? &unit->graph.types[tick_definition->declared_type]
          : NULL;
  const ctool_c_ir_instruction_t *instructions = ir->instructions;
  ctool_u32 result_type;
  ctool_u32 direct_zero_calls = 0u;
  ctool_u32 direct_promoted_calls = 0u;
  ctool_u32 indirect_promoted_calls = 0u;
  ctool_u32 integer_promotions = 0u;
  ctool_u32 function_addresses = 0u;
  ctool_u32 function_decays = 0u;
  ctool_u32 instruction;
  ctool_u32 function_index;

  if (tick == CTOOL_C_AST_NONE || invoke == CTOOL_C_AST_NONE ||
      consume == CTOOL_C_AST_NONE || promoted == CTOOL_C_AST_NONE ||
      indirect == CTOOL_C_AST_NONE ||
      tick_definition == NULL || invoke_definition == NULL ||
      tick_definition->binding != tick || invoke_definition->binding != invoke ||
      tick_type == NULL || tick_type->kind != CTOOL_C_TYPE_FUNCTION ||
      tick_type->has_prototype != CTOOL_FALSE ||
      tick_type->parameter_count != 0u || tick_type->variadic != CTOOL_FALSE ||
      ir->function_count != 4u || ir->instruction_count != 20u ||
      ir->functions == NULL || instructions == NULL) {
    return 0;
  }
  result_type = tick_type->referenced_type;
  for (function_index = 0u; function_index < ir->function_count;
       function_index++) {
    const ctool_c_ir_function_t *function = &ir->functions[function_index];
    if ((function->binding == tick || function->binding == invoke) &&
        function->maximum_stack_depth != 1u) {
      return 0;
    }
    if (function->binding == promoted && function->maximum_stack_depth != 3u) {
      return 0;
    }
    if (function->binding == indirect && function->maximum_stack_depth != 2u) {
      return 0;
    }
  }
  for (instruction = 0u; instruction < ir->instruction_count; instruction++) {
    const ctool_c_ir_instruction_t *candidate = &instructions[instruction];
    if (candidate->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT &&
        candidate->reference == tick && candidate->argument_count == 0u) {
      direct_zero_calls++;
    } else if (candidate->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT &&
               candidate->reference == consume &&
               candidate->argument_count == 3u) {
      direct_promoted_calls++;
    } else if (candidate->kind == CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT &&
               candidate->argument_count == 1u) {
      indirect_promoted_calls++;
    } else if (candidate->kind == CTOOL_C_IR_INSTRUCTION_CONVERT &&
               candidate->conversion == CTOOL_C_CONVERSION_INTEGER_PROMOTION &&
               candidate->type == result_type) {
      integer_promotions++;
    } else if (candidate->kind == CTOOL_C_IR_INSTRUCTION_FUNCTION_ADDRESS &&
               candidate->reference == tick) {
      function_addresses++;
    } else if (candidate->kind ==
               CTOOL_C_IR_INSTRUCTION_FUNCTION_TO_POINTER) {
      function_decays++;
    }
  }
  return ir->functions[0].binding == tick &&
                 ir->functions[0].declared_type ==
                     tick_definition->declared_type &&
                 ir->functions[0].first_instruction == 0u &&
                 ir->functions[0].instruction_count == 2u &&
                 ir->functions[0].maximum_stack_depth == 1u &&
                 ir->functions[1].binding == invoke &&
                 ir->functions[1].declared_type ==
                     invoke_definition->declared_type &&
                 ir->functions[1].first_instruction == 2u &&
                 ir->functions[1].instruction_count == 2u &&
                 ir->functions[1].maximum_stack_depth == 1u &&
                 instructions[0].kind == CTOOL_C_IR_INSTRUCTION_INTEGER &&
                 instructions[0].type == result_type &&
                 instructions[0].input_type == CTOOL_C_TYPE_NONE &&
                 instructions[0].argument_count == 0u &&
                 instructions[0].reference == CTOOL_C_AST_NONE &&
                 instructions[0].integer_bits == 37u &&
                 instructions[1].kind ==
                     CTOOL_C_IR_INSTRUCTION_RETURN_VALUE &&
                 instructions[1].type == result_type &&
                 instructions[1].input_type == result_type &&
                 instructions[1].argument_count == 0u &&
                 instructions[2].kind ==
                     CTOOL_C_IR_INSTRUCTION_CALL_DIRECT &&
                 instructions[2].type == result_type &&
                 instructions[2].input_type == unit->bindings[tick].type &&
                 instructions[2].argument_count == 0u &&
                 instructions[2].reference == tick &&
                 instructions[3].kind ==
                     CTOOL_C_IR_INSTRUCTION_RETURN_VALUE &&
                 instructions[3].type == result_type &&
                 instructions[3].input_type == result_type &&
                 instructions[3].argument_count == 0u &&
                 direct_zero_calls == 1u && direct_promoted_calls == 1u &&
                 indirect_promoted_calls == 1u && integer_promotions == 2u &&
                 function_addresses == 1u && function_decays == 1u &&
                 string_equal(instructions[0].location.path,
                              "/old-style-empty-ir.c") &&
                 string_equal(instructions[2].location.path,
                              "/old-style-empty-ir.c")
             ? 1
             : 0;
}

static int run_old_style_empty_functions(const char *host_root) {
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
      "static int invoke_promoted(signed char small, int *pointer)\n"
      "{\n"
      "  return consume(small, pointer, tick);\n"
      "}\n"
      "static int invoke_indirect(int (*callee)(), unsigned short small)\n"
      "{\n"
      "  return callee(small);\n"
      "}\n";
  static const char aggregate_source[] =
      "typedef struct { int value; } box_t;\n"
      "extern int consume();\n"
      "int bad(box_t box) { return consume(box); }\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t aggregate_unit;
  ctool_c_ir_unit_t ir;
  uint64_t fingerprint;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;

  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source_mode(job, "/old-style-empty-ir.c", source, CTOOL_FALSE,
                         &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "old-style empty function lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_old_style_empty_ir(&unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source_mode(job, "/old-style-aggregate-ir.c", aggregate_source,
                         CTOOL_FALSE, &aggregate_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &aggregate_unit, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports arguments without declared parameter "
          "types only for represented scalar values",
          "old-style aggregate argument")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("old-style-empty-functions: ok");
    return 0;
  }
  return 1;
}

static int validate_variadic_callee_ir(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 ap = find_block_binding(unit, "ap");
  ctool_u32 copy = find_block_binding(unit, "copy");
  ctool_u32 source = find_block_binding(unit, "source");
  ctool_u32 value = find_block_binding(unit, "value");
  ctool_u32 cursor_type;
  ctool_u32 value_type;
  ctool_u32 last_parameter;

  if (unit->function_definition_count != 1u ||
      unit->block_binding_count != 4u || ir->function_count != 1u ||
      ir->instruction_count != 18u || ir->functions == NULL ||
      ir->instructions == NULL || ap == CTOOL_C_AST_NONE ||
      copy == CTOOL_C_AST_NONE || source == CTOOL_C_AST_NONE ||
      value == CTOOL_C_AST_NONE) {
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  function = &ir->functions[0];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->has_prototype != CTOOL_TRUE ||
      function_type->variadic != CTOOL_TRUE ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 18u ||
      function->maximum_stack_depth != 2u) {
    return 0;
  }
  cursor_type = unit->block_bindings[ap].type;
  value_type = unit->block_bindings[value].type;
  last_parameter = function_type->first_parameter;
  if (unit->block_bindings[copy].type != cursor_type ||
      unit->block_bindings[source].type == cursor_type) {
    return 0;
  }
  instructions = &ir->instructions[function->first_instruction];
  {
    typedef struct {
      ctool_c_ir_instruction_kind_t kind;
      ctool_u32 type;
      ctool_u32 input_type;
      ctool_u32 reference;
    } variadic_ir_expectation_t;
    const variadic_ir_expectation_t expected[] = {
        {CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS, cursor_type,
         CTOOL_C_TYPE_NONE, ap},
        {CTOOL_C_IR_INSTRUCTION_VARIADIC_START, CTOOL_C_TYPE_NONE,
         cursor_type, last_parameter},
        {CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS, cursor_type,
         CTOOL_C_TYPE_NONE, copy},
        {CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS, cursor_type,
         CTOOL_C_TYPE_NONE, ap},
        {CTOOL_C_IR_INSTRUCTION_LOAD, cursor_type, cursor_type,
         CTOOL_C_AST_NONE},
        {CTOOL_C_IR_INSTRUCTION_STORE, cursor_type, cursor_type,
         CTOOL_C_AST_NONE},
        {CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS, value_type,
         CTOOL_C_TYPE_NONE, value},
        {CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS, cursor_type,
         CTOOL_C_TYPE_NONE, copy},
        {CTOOL_C_IR_INSTRUCTION_VARIADIC_ARGUMENT, value_type, cursor_type,
         CTOOL_C_AST_NONE},
        {CTOOL_C_IR_INSTRUCTION_STORE_VALUE, value_type, value_type,
         CTOOL_C_AST_NONE},
        {CTOOL_C_IR_INSTRUCTION_DISCARD, CTOOL_C_TYPE_NONE, value_type,
         CTOOL_C_AST_NONE},
        {CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS, cursor_type,
         CTOOL_C_TYPE_NONE, copy},
        {CTOOL_C_IR_INSTRUCTION_VARIADIC_END, CTOOL_C_TYPE_NONE, cursor_type,
         CTOOL_C_AST_NONE},
        {CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS, cursor_type,
         CTOOL_C_TYPE_NONE, ap},
        {CTOOL_C_IR_INSTRUCTION_VARIADIC_END, CTOOL_C_TYPE_NONE, cursor_type,
         CTOOL_C_AST_NONE},
        {CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS, value_type,
         CTOOL_C_TYPE_NONE, value},
        {CTOOL_C_IR_INSTRUCTION_LOAD, value_type, value_type,
         CTOOL_C_AST_NONE},
        {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, value_type, value_type,
         CTOOL_C_AST_NONE}};
    ctool_u32 instruction;
    for (instruction = 0u;
         instruction < (ctool_u32)(sizeof(expected) / sizeof(expected[0]));
         instruction++) {
      if (!variadic_ir_instruction_is(
              &instructions[instruction], expected[instruction].kind,
              expected[instruction].type, expected[instruction].input_type,
              expected[instruction].reference)) {
        (void)fprintf(stderr,
                      "variadic-callees: instruction %u fields differ\n",
                      (unsigned int)instruction);
        return 0;
      }
    }
  }
  return 1;
}

static int run_variadic_callees(const char *host_root) {
  static const char source[] =
      "typedef __builtin_va_list va_list;\n"
      "int read_args(int last, ...) {\n"
      "  va_list ap;\n"
      "  va_list copy;\n"
      "  va_list const source;\n"
      "  int value;\n"
      "  __builtin_va_start(ap, last);\n"
      "  __builtin_va_copy(copy, ap);\n"
      "  value = __builtin_va_arg(copy, int);\n"
      "  __builtin_va_end(copy);\n"
      "  __builtin_va_end(ap);\n"
      "  return value;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_ir_unit_t ir;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_u32 *invalid_children = NULL;
  ctool_u32 start_expression = CTOOL_C_AST_NONE;
  ctool_u32 argument_expression = CTOOL_C_AST_NONE;
  ctool_u32 cursor_expression = CTOOL_C_AST_NONE;
  ctool_u32 last_expression = CTOOL_C_AST_NONE;
  ctool_u32 source_binding = CTOOL_C_AST_NONE;
  ctool_u32 index;
  ctool_u32 diagnostic_count;
  uint64_t fingerprint;
  ctool_status_t status;
  int passed = 0;

  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source_mode(job, "/variadic-ir.c", source, CTOOL_TRUE, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &unit, &ir);
  if (!check_status(status, CTOOL_OK, "variadic callee lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_variadic_callee_ir(&unit, &ir)) {
    (void)fprintf(
        stderr,
        "variadic-callees: IR inventory differs functions=%u instructions=%u "
        "maximum-stack=%u\n",
        (unsigned int)ir.function_count, (unsigned int)ir.instruction_count,
        ir.function_count != 0u && ir.functions != NULL
            ? (unsigned int)ir.functions[0].maximum_stack_depth
            : 0u);
    (void)fprintf(
        stderr,
        "  unit functions=%u blocks=%u ap=%u copy=%u value=%u "
        "declared-type=%u kind=%u prototype=%u variadic=%u parameters=%u\n",
        (unsigned int)unit.function_definition_count,
        (unsigned int)unit.block_binding_count,
        (unsigned int)find_block_binding(&unit, "ap"),
        (unsigned int)find_block_binding(&unit, "copy"),
        (unsigned int)find_block_binding(&unit, "value"),
        unit.function_definition_count != 0u
            ? (unsigned int)unit.function_definitions[0].declared_type
            : 0u,
        unit.function_definition_count != 0u &&
                unit.function_definitions[0].declared_type <
                    unit.graph.type_count
            ? (unsigned int)unit.graph
                  .types[unit.function_definitions[0].declared_type]
                  .kind
            : 0u,
        unit.function_definition_count != 0u &&
                unit.function_definitions[0].declared_type <
                    unit.graph.type_count
            ? (unsigned int)unit.graph
                  .types[unit.function_definitions[0].declared_type]
                  .has_prototype
            : 0u,
        unit.function_definition_count != 0u &&
                unit.function_definitions[0].declared_type <
                    unit.graph.type_count
            ? (unsigned int)unit.graph
                  .types[unit.function_definitions[0].declared_type]
                  .variadic
            : 0u,
        unit.function_definition_count != 0u &&
                unit.function_definitions[0].declared_type <
                    unit.graph.type_count
            ? (unsigned int)unit.graph
                  .types[unit.function_definitions[0].declared_type]
                  .parameter_count
            : 0u);
    if (unit.function_definition_count != 0u && ir.function_count != 0u) {
      (void)fprintf(
          stderr,
          "  definition binding=%u IR binding=%u IR declared=%u first=%u "
          "count=%u cursor-types=%u/%u value-type=%u\n",
          (unsigned int)unit.function_definitions[0].binding,
          (unsigned int)ir.functions[0].binding,
          (unsigned int)ir.functions[0].declared_type,
          (unsigned int)ir.functions[0].first_instruction,
          (unsigned int)ir.functions[0].instruction_count,
          (unsigned int)unit.block_bindings[0].type,
          (unsigned int)unit.block_bindings[1].type,
          (unsigned int)unit.block_bindings[2].type);
    }
    for (index = 0u; index < ir.instruction_count; index++) {
      const ctool_c_ir_instruction_t *instruction = &ir.instructions[index];
      (void)fprintf(
          stderr, "  %u kind=%u type=%u input=%u reference=%u\n",
          (unsigned int)index, (unsigned int)instruction->kind,
          (unsigned int)instruction->type,
          (unsigned int)instruction->input_type,
          (unsigned int)instruction->reference);
    }
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < unit.expression_count; index++) {
    if (unit.expressions[index].kind ==
        CTOOL_C_EXPRESSION_VARIADIC_START) {
      start_expression = index;
    } else if (unit.expressions[index].kind ==
               CTOOL_C_EXPRESSION_VARIADIC_ARGUMENT) {
      argument_expression = index;
    }
  }
  if (start_expression == CTOOL_C_AST_NONE ||
      argument_expression == CTOOL_C_AST_NONE ||
      unit.expression_count == 0u || unit.expression_child_count == 0u ||
      sizeof(*invalid_expressions) >
          SIZE_MAX / (size_t)unit.expression_count ||
      sizeof(*invalid_children) >
          SIZE_MAX / (size_t)unit.expression_child_count) {
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_children = (ctool_u32 *)malloc(
      (size_t)unit.expression_child_count * sizeof(*invalid_children));
  if (invalid_expressions == NULL || invalid_children == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  (void)memcpy(invalid_children, unit.expression_children,
               (size_t)unit.expression_child_count * sizeof(*invalid_children));
  invalid_unit = unit;
  invalid_unit.expressions = invalid_expressions;
  invalid_unit.expression_children = invalid_children;
  source_binding = find_block_binding(&unit, "source");
  cursor_expression =
      invalid_children[invalid_expressions[start_expression].first_child];
  last_expression =
      invalid_children[invalid_expressions[start_expression].first_child + 1u];
  if (source_binding == CTOOL_C_AST_NONE ||
      cursor_expression >= unit.expression_count ||
      last_expression >= unit.expression_count) {
    goto cleanup;
  }
  invalid_children[invalid_expressions[start_expression].first_child + 1u] =
      invalid_children[invalid_expressions[start_expression].first_child];
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "variadic start final parameter reference")) {
    goto cleanup;
  }
  (void)memcpy(invalid_children, unit.expression_children,
               (size_t)unit.expression_child_count * sizeof(*invalid_children));
  invalid_expressions[last_expression].type =
      unit.expressions[start_expression].type;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "variadic start final parameter type")) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_expressions[last_expression].operation =
      CTOOL_C_EXPRESSION_OPERATOR_ADD;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "variadic start final parameter metadata")) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_expressions[last_expression].integer_bits = 1u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "variadic start final parameter payload")) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_expressions[cursor_expression].reference = source_binding;
  invalid_expressions[cursor_expression].type =
      unit.block_bindings[source_binding].type;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "const variadic cursor destination")) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, unit.expressions,
               (size_t)unit.expression_count * sizeof(*invalid_expressions));
  invalid_expressions[argument_expression].type =
      unit.expressions[start_expression].type;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "variadic argument result type")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_children);
  free(invalid_expressions);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("variadic-callees: ok");
    return 0;
  }
  return 1;
}

static int run_block_records(const char *host_root) {
  static const char source[] =
      "typedef unsigned int ctool_u32;\n"
      "ctool_u32 block_records(void) {\n"
      "  static struct Value;\n"
      "  extern struct Value { ctool_u32 member; };\n"
      "  struct Value value = { 7u };\n"
      "  return value.member;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_ir_unit_t first;
  ctool_c_ir_unit_t second;
  ctool_c_statement_t *invalid_statements = NULL;
  uint64_t fingerprint;
  uint64_t first_ir_fingerprint;
  ctool_u32 zero_binding_declarations = 0u;
  ctool_u32 first_zero_binding = CTOOL_C_AST_NONE;
  ctool_u32 index;
  ctool_status_t status;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&invalid_unit, 0, sizeof(invalid_unit));
  (void)memset(&first, 0xa5, sizeof(first));
  (void)memset(&second, 0xa5, sizeof(second));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source(job, "/block-record-ir.c", source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  status = ctool_c_lower_ir(job, &unit, &first);
  if (!check_status(status, CTOOL_OK, "block record lowering") ||
      unit_fingerprint(&unit) != fingerprint || unit.tag_count != 0u ||
      unit.block_binding_count != 1u || first.function_count != 1u ||
      first.instruction_count != 10u || first.instructions == NULL ||
      first.functions == NULL || first.functions[0].first_instruction != 0u ||
      first.functions[0].instruction_count != 10u ||
      first.functions[0].maximum_stack_depth != 2u) {
    (void)fprintf(stderr, "block record IR inventory differs\n");
    goto cleanup;
  }
  for (index = 0u; index < unit.statement_count; index++) {
    if (unit.statements[index].kind == CTOOL_C_STATEMENT_DECLARATION &&
        unit.statements[index].block_binding_count == 0u) {
      if (first_zero_binding == CTOOL_C_AST_NONE) {
        first_zero_binding = index;
      }
      zero_binding_declarations++;
    }
  }
  if (zero_binding_declarations != 2u ||
      first_zero_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "block record declaration inventory differs\n");
    goto cleanup;
  }
  first_ir_fingerprint = ir_instruction_fingerprint(&first);
  status = ctool_c_lower_ir(job, &unit, &second);
  if (!check_status(status, CTOOL_OK, "repeat block record lowering") ||
      second.function_count != first.function_count ||
      second.instruction_count != first.instruction_count ||
      ir_instruction_fingerprint(&second) != first_ir_fingerprint ||
      unit_fingerprint(&unit) != fingerprint) {
    (void)fprintf(stderr, "block record IR is not deterministic\n");
    goto cleanup;
  }
  if (unit.statement_count == 0u ||
      sizeof(*invalid_statements) >
          SIZE_MAX / (size_t)unit.statement_count) {
    goto cleanup;
  }
  invalid_statements = (ctool_c_statement_t *)malloc(
      (size_t)unit.statement_count * sizeof(*invalid_statements));
  if (invalid_statements == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_statements, unit.statements,
               (size_t)unit.statement_count * sizeof(*invalid_statements));
  invalid_unit = unit;
  invalid_unit.statements = invalid_statements;
  invalid_statements[first_zero_binding].first_block_binding = 1u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block record declaration binding cursor")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_statements);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("block-records: ok");
    return 0;
  }
  return 1;
}

static int run_block_enums(const char *host_root) {
  static const char source[] =
      "int cursor_enum(void) {\n"
      "  enum { CURSOR_W = 8, CURSOR_H = 10, CURSOR_PAD = 1 };\n"
      "  return CURSOR_W + CURSOR_H + CURSOR_PAD;\n"
      "}\n"
      "int repl_enum(void) {\n"
      "  enum { CC_REPL_LINE_MAX = 512,\n"
      "         CC_REPL_SRC_MAX = 64 * 1024 };\n"
      "  return CC_REPL_LINE_MAX + CC_REPL_SRC_MAX;\n"
      "}\n"
      "int shadow_enum(int input) {\n"
      "  enum { VALUE = 3 };\n"
      "  {\n"
      "    enum { VALUE = 5 };\n"
      "    input += VALUE;\n"
      "  }\n"
      "  return input + VALUE;\n"
      "}\n"
      "int wide_unused_enum(void) {\n"
      "  enum { WIDE = 0x100000000ull };\n"
      "  return 0;\n"
      "}\n"
      "int member_enum(void) {\n"
      "  struct Holder { enum { MEMBER_FIRST = 19, MEMBER_LAST = 23 } value; };\n"
      "  return MEMBER_FIRST + MEMBER_LAST;\n"
      "}\n"
      "int parameter_enum(\n"
      "    enum { PARAM_FIRST = 13, PARAM_LAST = 17 } value) {\n"
      "  return value + PARAM_FIRST + PARAM_LAST;\n"
      "}\n";
  static const char wide_source[] =
      "int wide_block_enum(void) {\n"
      "  enum { WIDE = 0x100000000ull };\n"
      "  return WIDE;\n"
      "}\n";
  static const char type_name_source[] =
      "int type_name_enum(void) {\n"
      "  int size = sizeof(enum SizeTag { TYPE_VALUE = 11 });\n"
      "  enum SizeTag value = TYPE_VALUE;\n"
      "  return size + value;\n"
      "}\n"
      "int cast_enum(int input) {\n"
      "  return (enum CastTag { CAST_VALUE = 7 })input + CAST_VALUE;\n"
      "}\n"
      "int align_enum(void) {\n"
      "  _Alignof(enum AlignTag { ALIGN_VALUE = 13 });\n"
      "  enum AlignTag aligned = ALIGN_VALUE;\n"
      "  return aligned;\n"
      "}\n"
      "int literal_enum(void) {\n"
      "  return (enum LiteralTag { LITERAL_VALUE = 17 }){ LITERAL_VALUE };\n"
      "}\n"
      "int offset_enum(void) {\n"
      "  return __builtin_offsetof(\n"
      "      struct Offset { enum OffsetTag { OFFSET_VALUE = 19 } member;\n"
      "                      int tail; }, tail) + OFFSET_VALUE;\n"
      "}\n"
      "int case_enum(int input) {\n"
      "  switch (input) {\n"
      "  case (enum CaseTag { CASE_VALUE = 23 })23:\n"
      "    return CASE_VALUE;\n"
      "  default:\n"
      "    return 0;\n"
      "  }\n"
      "}\n"
      "int iteration_enum(int input) {\n"
      "  for (; input; (enum IterTag { ITER_VALUE = 29 })input) {\n"
      "    return ITER_VALUE;\n"
      "  }\n"
      "  return 0;\n"
      "}\n"
      "int variadic_enum(int marker, ...) {\n"
      "  __builtin_va_list cursor;\n"
      "  __builtin_va_start(cursor, marker);\n"
      "  int value = __builtin_va_arg(\n"
      "      cursor, enum VaTag { VA_VALUE = 31 });\n"
      "  __builtin_va_end(cursor);\n"
      "  return value + VA_VALUE;\n"
      "}\n"
      "int designator_enum(void) {\n"
      "  int values[1] = {\n"
      "      [sizeof(enum DesignatorTag { DESIGNATOR_VALUE = 0 }) - 4] =\n"
      "          DESIGNATOR_VALUE\n"
      "  };\n"
      "  return values[0];\n"
      "}\n"
      "int literal_designator_enum(void) {\n"
      "  return ((int[1]) {\n"
      "      [sizeof(enum LiteralDesignatorTag {\n"
      "          LITERAL_DESIGNATOR_VALUE = 0 }) - 4] =\n"
      "          LITERAL_DESIGNATOR_VALUE\n"
      "  })[0];\n"
      "}\n"
      "int unevaluated_designator_enum(void) {\n"
      "  (void)sizeof((int[1]) {\n"
      "      [sizeof(enum UnevaluatedDesignatorTag {\n"
      "          UNEVALUATED_DESIGNATOR_VALUE = 0 }) - 4] =\n"
      "          UNEVALUATED_DESIGNATOR_VALUE\n"
      "  });\n"
      "  return UNEVALUATED_DESIGNATOR_VALUE;\n"
      "}\n";
  static const ctool_u64 expected_values[] = {
      8ull, 10ull, 1ull, 512ull, 65536ull, 5ull, 3ull, 0ull,
      19ull, 23ull, 13ull, 17ull};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t wide_unit;
  ctool_c_translation_unit_t type_name_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_translation_unit_t invalid_type_name_unit;
  ctool_c_block_binding_t *invalid_blocks = NULL;
  ctool_c_block_binding_t *invalid_type_name_blocks = NULL;
  ctool_c_function_definition_t *invalid_definitions = NULL;
  ctool_c_expression_t *invalid_type_name_expressions = NULL;
  ctool_c_initializer_t *invalid_type_name_initializers = NULL;
  ctool_c_ir_unit_t first;
  ctool_c_ir_unit_t second;
  ctool_c_ir_unit_t type_name_ir;
  ctool_u64 fingerprint;
  ctool_u64 type_name_fingerprint;
  ctool_u64 first_ir_fingerprint;
  ctool_u32 diagnostic_count;
  ctool_u32 value_counts[sizeof(expected_values) /
                         sizeof(expected_values[0])] = {0u};
  ctool_u32 expected_value_count =
      (ctool_u32)(sizeof(expected_values) / sizeof(expected_values[0]));
  ctool_u32 local_addresses = 0u;
  ctool_u32 type_name_event_count = 0u;
  ctool_u32 initializer_event_count = 0u;
  ctool_u32 cast_expression = CTOOL_C_AST_NONE;
  ctool_u32 va_expression = CTOOL_C_AST_NONE;
  ctool_u32 designator_initializer = CTOOL_C_AST_NONE;
  ctool_u32 index;
  ctool_status_t status;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&wide_unit, 0, sizeof(wide_unit));
  (void)memset(&type_name_unit, 0, sizeof(type_name_unit));
  (void)memset(&first, 0xa5, sizeof(first));
  (void)memset(&second, 0xa5, sizeof(second));
  (void)memset(&type_name_ir, 0xa5, sizeof(type_name_ir));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !parse_source_mode(job, "/block-enum-ir.c", source, CTOOL_TRUE,
                         &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &unit, &first);
  if (!check_status(status, CTOOL_OK, "block enum lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      unit.block_binding_count != 12u || first.function_count != 6u ||
      unit.function_definitions[4].first_block_binding != 8u ||
      unit.function_definitions[4].block_binding_count != 0u ||
      unit.function_definitions[5].first_block_binding != 10u ||
      unit.function_definitions[5].block_binding_count != 2u) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < first.instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction = &first.instructions[index];
    ctool_u32 value_index;
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_INTEGER) {
      for (value_index = 0u; value_index < expected_value_count;
           value_index++) {
        if (instruction->integer_bits == expected_values[value_index]) {
          value_counts[value_index]++;
        }
      }
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS) {
      local_addresses++;
    }
  }
  for (index = 0u; index < expected_value_count; index++) {
    if (value_counts[index] != 1u) {
      (void)fprintf(stderr,
                    "block enum IR constant %u has %u matches\n", index,
                    value_counts[index]);
      goto cleanup;
    }
  }
  if (local_addresses != 0u) {
    (void)fprintf(stderr, "block enum IR allocated local storage\n");
    goto cleanup;
  }
  first_ir_fingerprint = ir_instruction_fingerprint(&first);
  status = ctool_c_lower_ir(job, &unit, &second);
  if (!check_status(status, CTOOL_OK, "repeat block enum lowering") ||
      unit_fingerprint(&unit) != fingerprint ||
      second.function_count != first.function_count ||
      second.instruction_count != first.instruction_count ||
      ir_instruction_fingerprint(&second) != first_ir_fingerprint) {
    (void)fprintf(stderr, "block enum IR is not deterministic\n");
    goto cleanup;
  }
  if (!parse_source_mode(job, "/type-name-enum-ir.c", type_name_source,
                         CTOOL_TRUE, &type_name_unit)) {
    goto cleanup;
  }
  type_name_fingerprint = unit_fingerprint(&type_name_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &type_name_unit, &type_name_ir);
  if (!check_status(status, CTOOL_OK, "type-name enum lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&type_name_unit) != type_name_fingerprint ||
      type_name_unit.function_definition_count != 11u ||
      type_name_unit.block_binding_count != 17u ||
      type_name_ir.function_count != 11u ||
      type_name_unit.function_definitions[0].first_block_binding != 0u ||
      type_name_unit.function_definitions[1].first_block_binding != 3u ||
      type_name_unit.function_definitions[7].first_block_binding != 10u ||
      type_name_unit.function_definitions[8].first_block_binding != 13u ||
      type_name_unit.function_definitions[9].first_block_binding != 15u ||
      type_name_unit.function_definitions[10].first_block_binding != 16u) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < type_name_unit.block_binding_count; index++) {
    const ctool_c_block_binding_t *binding =
        &type_name_unit.block_bindings[index];
    ctool_bool event =
        index == 1u || index == 3u || index == 4u || index == 6u ||
                index == 7u || index == 8u || index == 9u || index == 12u ||
                index == 14u || index == 15u || index == 16u
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if (event == CTOOL_TRUE) {
      if (index == 14u || index == 15u) {
        const ctool_c_initializer_t *initializer;
        if (binding->kind != CTOOL_C_BINDING_ENUMERATOR ||
            binding->activation_expression != CTOOL_C_AST_NONE ||
            binding->activation_initializer >=
                type_name_unit.initializer_count) {
          (void)fprintf(stderr,
                        "type-name enum IR initializer event differs\n");
          goto cleanup;
        }
        initializer = &type_name_unit
                           .initializers[binding->activation_initializer];
        if (initializer->first_block_binding != index ||
            initializer->block_binding_count != 1u) {
          (void)fprintf(stderr,
                        "type-name enum IR initializer ownership differs\n");
          goto cleanup;
        }
        if (index == 14u) {
          designator_initializer = binding->activation_initializer;
        }
        initializer_event_count++;
        continue;
      }
      const ctool_c_expression_t *expression;
      ctool_u32 expected_child_offset = index == 12u ? 1u : 0u;
      if (binding->kind != CTOOL_C_BINDING_ENUMERATOR ||
          binding->activation_initializer != CTOOL_C_AST_NONE ||
          binding->activation_expression >=
              type_name_unit.expression_count) {
        (void)fprintf(stderr,
                      "type-name enum IR event inventory differs\n");
        goto cleanup;
      }
      expression = &type_name_unit
                        .expressions[binding->activation_expression];
      if (expression->first_block_binding != index ||
          expression->block_binding_count != 1u ||
          expression->block_binding_child_offset != expected_child_offset) {
        (void)fprintf(stderr,
                      "type-name enum IR event ownership differs\n");
        goto cleanup;
      }
      type_name_event_count++;
      if (index == 3u) {
        cast_expression = binding->activation_expression;
      }
      if (index == 12u) {
        va_expression = binding->activation_expression;
      }
    } else if (binding->kind != CTOOL_C_BINDING_OBJECT ||
               binding->activation_expression != CTOOL_C_AST_NONE ||
               binding->activation_initializer != CTOOL_C_AST_NONE) {
      (void)fprintf(stderr,
                    "type-name enum IR object inventory differs\n");
      goto cleanup;
    }
  }
  if (type_name_event_count != 9u || cast_expression == CTOOL_C_AST_NONE ||
      va_expression == CTOOL_C_AST_NONE || initializer_event_count != 2u ||
      designator_initializer == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "type-name enum IR event count differs\n");
    goto cleanup;
  }
  if (!parse_source_mode(job, "/wide-block-enum-ir.c", wide_source,
                         CTOOL_TRUE, &wide_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &wide_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide block enumerator expression")) {
    goto cleanup;
  }
  invalid_blocks = (ctool_c_block_binding_t *)malloc(
      (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  if (invalid_blocks == NULL) {
    goto cleanup;
  }
  invalid_unit = unit;
  invalid_unit.block_bindings = invalid_blocks;

  invalid_definitions = (ctool_c_function_definition_t *)malloc(
      (size_t)unit.function_definition_count * sizeof(*invalid_definitions));
  if (invalid_definitions == NULL) {
    goto cleanup;
  }

  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_blocks[0].storage = CTOOL_C_STORAGE_AUTO;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block enumerator storage")) {
    goto cleanup;
  }

  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_blocks[0].initializer = 0u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block enumerator initializer")) {
    goto cleanup;
  }

  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_blocks[0].linkage_binding = 0u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block enumerator linked identity")) {
    goto cleanup;
  }

  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_blocks[0].integer_unsigned = (ctool_bool)2;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block enumerator unsignedness")) {
    goto cleanup;
  }

  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_blocks[0].integer_bits = 0xffffffffull;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "block enumerator canonical value")) {
    goto cleanup;
  }

  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  invalid_blocks[0].kind = CTOOL_C_BINDING_OBJECT;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "non-enumerator with enum value")) {
    goto cleanup;
  }

  (void)memcpy(invalid_blocks, unit.block_bindings,
               (size_t)unit.block_binding_count * sizeof(*invalid_blocks));
  (void)memcpy(invalid_definitions, unit.function_definitions,
               (size_t)unit.function_definition_count *
                   sizeof(*invalid_definitions));
  invalid_unit.function_definitions = invalid_definitions;
  invalid_definitions[5].first_block_binding = 9u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "parameter enumerator ownership start")) {
    goto cleanup;
  }

  (void)memcpy(invalid_definitions, unit.function_definitions,
               (size_t)unit.function_definition_count *
                   sizeof(*invalid_definitions));
  invalid_definitions[4].block_binding_count = 1u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "body declaration claimed by a function prefix")) {
    goto cleanup;
  }

  invalid_type_name_blocks = (ctool_c_block_binding_t *)malloc(
      (size_t)type_name_unit.block_binding_count *
      sizeof(*invalid_type_name_blocks));
  invalid_type_name_expressions = (ctool_c_expression_t *)malloc(
      (size_t)type_name_unit.expression_count *
      sizeof(*invalid_type_name_expressions));
  invalid_type_name_initializers = (ctool_c_initializer_t *)malloc(
      (size_t)type_name_unit.initializer_count *
      sizeof(*invalid_type_name_initializers));
  if (invalid_type_name_blocks == NULL ||
      invalid_type_name_expressions == NULL ||
      invalid_type_name_initializers == NULL) {
    goto cleanup;
  }
  invalid_type_name_unit = type_name_unit;
  invalid_type_name_unit.block_bindings = invalid_type_name_blocks;
  invalid_type_name_unit.expressions = invalid_type_name_expressions;
  invalid_type_name_unit.initializers = invalid_type_name_initializers;

  (void)memcpy(invalid_type_name_blocks, type_name_unit.block_bindings,
               (size_t)type_name_unit.block_binding_count *
                   sizeof(*invalid_type_name_blocks));
  (void)memcpy(invalid_type_name_expressions, type_name_unit.expressions,
               (size_t)type_name_unit.expression_count *
                   sizeof(*invalid_type_name_expressions));
  (void)memcpy(invalid_type_name_initializers, type_name_unit.initializers,
               (size_t)type_name_unit.initializer_count *
                   sizeof(*invalid_type_name_initializers));
  invalid_type_name_expressions
      [type_name_unit.block_bindings[1u].activation_expression]
          .first_block_binding = 2u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_type_name_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "type-name enum ownership start")) {
    goto cleanup;
  }

  (void)memcpy(invalid_type_name_blocks, type_name_unit.block_bindings,
               (size_t)type_name_unit.block_binding_count *
                   sizeof(*invalid_type_name_blocks));
  (void)memcpy(invalid_type_name_expressions, type_name_unit.expressions,
               (size_t)type_name_unit.expression_count *
                   sizeof(*invalid_type_name_expressions));
  invalid_type_name_blocks[1u].activation_expression = cast_expression;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_type_name_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "type-name enum activation back-reference")) {
    goto cleanup;
  }

  (void)memcpy(invalid_type_name_blocks, type_name_unit.block_bindings,
               (size_t)type_name_unit.block_binding_count *
                   sizeof(*invalid_type_name_blocks));
  (void)memcpy(invalid_type_name_expressions, type_name_unit.expressions,
               (size_t)type_name_unit.expression_count *
                   sizeof(*invalid_type_name_expressions));
  invalid_type_name_expressions[va_expression]
      .block_binding_child_offset =
      invalid_type_name_expressions[va_expression].child_count + 1u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_type_name_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "type-name enum activation child offset")) {
    goto cleanup;
  }

  (void)memcpy(invalid_type_name_blocks, type_name_unit.block_bindings,
               (size_t)type_name_unit.block_binding_count *
                   sizeof(*invalid_type_name_blocks));
  (void)memcpy(invalid_type_name_expressions, type_name_unit.expressions,
               (size_t)type_name_unit.expression_count *
                   sizeof(*invalid_type_name_expressions));
  if (invalid_type_name_expressions[cast_expression].child_count != 1u ||
      invalid_type_name_expressions[cast_expression].first_child >=
          type_name_unit.expression_child_count) {
    (void)fprintf(stderr, "type-name enum cast shape differs\n");
    goto cleanup;
  }
  index = type_name_unit.expression_children
      [invalid_type_name_expressions[cast_expression].first_child];
  if (index >= type_name_unit.expression_count) {
    (void)fprintf(stderr, "type-name enum cast child differs\n");
    goto cleanup;
  }
  invalid_type_name_expressions[index].kind =
      CTOOL_C_EXPRESSION_BLOCK_BINDING;
  invalid_type_name_expressions[index].reference = 3u;
  invalid_type_name_expressions[cast_expression]
      .block_binding_child_offset = 1u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_type_name_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "type-name enum reference before activation")) {
    goto cleanup;
  }

  (void)memcpy(invalid_type_name_blocks, type_name_unit.block_bindings,
               (size_t)type_name_unit.block_binding_count *
                   sizeof(*invalid_type_name_blocks));
  (void)memcpy(invalid_type_name_expressions, type_name_unit.expressions,
               (size_t)type_name_unit.expression_count *
                   sizeof(*invalid_type_name_expressions));
  (void)memcpy(invalid_type_name_initializers, type_name_unit.initializers,
               (size_t)type_name_unit.initializer_count *
                   sizeof(*invalid_type_name_initializers));
  invalid_type_name_initializers[designator_initializer]
      .first_block_binding = 13u;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_type_name_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "designator enum ownership start")) {
    goto cleanup;
  }

  (void)memcpy(invalid_type_name_blocks, type_name_unit.block_bindings,
               (size_t)type_name_unit.block_binding_count *
                   sizeof(*invalid_type_name_blocks));
  (void)memcpy(invalid_type_name_initializers, type_name_unit.initializers,
               (size_t)type_name_unit.initializer_count *
                   sizeof(*invalid_type_name_initializers));
  invalid_type_name_initializers[designator_initializer]
      .first_block_binding = CTOOL_C_AST_NONE;
  invalid_type_name_initializers[designator_initializer]
      .block_binding_count = 0u;
  invalid_type_name_blocks[14u].activation_initializer = CTOOL_C_AST_NONE;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_type_name_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "designator enum reference before activation")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_type_name_initializers);
  free(invalid_type_name_expressions);
  free(invalid_type_name_blocks);
  free(invalid_definitions);
  free(invalid_blocks);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("block-enums: ok");
    return 0;
  }
  return 1;
}

static int validate_bit_field_store_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  static const char *const member_names[] = {"r", "delta", "value"};
  static const ctool_u32 bit_offsets[] = {16u, 8u, 0u};
  static const ctool_u32 bit_widths[] = {8u, 5u, 32u};
  ctool_u32 members[3];
  ctool_u32 colors;
  ctool_u32 index;
  if (unit->function_definition_count != 4u || ir->function_count != 4u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->instruction_count != 31u) {
    (void)fprintf(stderr, "bit-field store IR inventory differs\n");
    return 0;
  }
  for (index = 0u; index < 3u; index++) {
    const ctool_c_ir_function_t *function = &ir->functions[index];
    const ctool_c_ir_instruction_t *instructions;
    const ctool_c_member_layout_t *layout;
    members[index] = find_member(unit, member_names[index]);
    if (members[index] == CTOOL_C_AST_NONE ||
        members[index] >= unit->layout.member_count) {
      (void)fprintf(stderr, "bit-field store member %u is missing\n",
                    (unsigned)index);
      return 0;
    }
    layout = &unit->layout.members[members[index]];
    if (unit->graph.members[members[index]].is_bit_field != CTOOL_TRUE ||
        unit->graph.members[members[index]].bit_width != bit_widths[index] ||
        layout->byte_offset != 0u ||
        layout->bit_offset != bit_offsets[index] ||
        layout->bit_width != bit_widths[index] || layout->size != 4u ||
        function->binding != unit->function_definitions[index].binding ||
        function->declared_type !=
            unit->function_definitions[index].declared_type ||
        function->first_instruction != index * 7u ||
        function->instruction_count != 7u ||
        function->maximum_stack_depth != 2u) {
      (void)fprintf(stderr, "bit-field store function %u differs\n",
                    (unsigned)index);
      return 0;
    }
    instructions = &ir->instructions[function->first_instruction];
    if (instructions[0].kind !=
            CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
        instructions[0].reference != index * 2u ||
        instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
        instructions[2].kind != CTOOL_C_IR_INSTRUCTION_DEREFERENCE ||
        instructions[3].kind !=
            CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
        instructions[3].reference != index * 2u + 1u ||
        instructions[4].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
        instructions[5].kind !=
            CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_VALUE ||
        instructions[5].type != instructions[4].type ||
        instructions[5].input_type != instructions[2].type ||
        instructions[5].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        instructions[5].conversion != CTOOL_C_CONVERSION_NONE ||
        instructions[5].reference != members[index] ||
        instructions[5].integer_bits != 0u ||
        instructions[6].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
        instructions[6].type != instructions[5].type ||
        instructions[6].input_type != instructions[5].type ||
        !string_equal(instructions[5].location.path,
                      "/bit-field-stores.c") ||
        !string_equal(instructions[5].physical_location.path,
                      "/bit-field-stores.c")) {
      (void)fprintf(stderr,
                    "bit-field store instruction stream %u differs\n",
                    (unsigned)index);
      return 0;
    }
  }
  colors = find_binding(unit, "colors");
  if (colors == CTOOL_C_AST_NONE || ir->functions[3].binding !=
                                        unit->function_definitions[3].binding ||
      ir->functions[3].declared_type !=
          unit->function_definitions[3].declared_type ||
      ir->functions[3].first_instruction != 21u ||
      ir->functions[3].instruction_count != 10u ||
      ir->functions[3].maximum_stack_depth != 2u ||
      ir->instructions[21].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      ir->instructions[21].reference != colors ||
      ir->instructions[22].kind !=
          CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER ||
      ir->instructions[23].kind !=
          CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      ir->instructions[23].reference != 6u ||
      ir->instructions[24].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      ir->instructions[25].kind != CTOOL_C_IR_INSTRUCTION_POINTER_BINARY ||
      ir->instructions[26].kind != CTOOL_C_IR_INSTRUCTION_DEREFERENCE ||
      ir->instructions[27].kind !=
          CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      ir->instructions[27].reference != 7u ||
      ir->instructions[28].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      ir->instructions[29].kind !=
          CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_VALUE ||
      ir->instructions[29].reference != members[0] ||
      ir->instructions[30].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "indexed bit-field store IR differs\n");
    return 0;
  }
  return 1;
}

static int run_bit_field_stores(const char *host_root) {
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
  static const char narrow_source[] =
      "struct flags { unsigned char value : 3; };\n"
      "unsigned int write_narrow(struct flags *state, unsigned int value) {\n"
      "  return state->value = value;\n"
      "}\n";
  static const char bool_source[] =
      "struct flags { _Bool value : 1; };\n"
      "_Bool write_bool(struct flags *state, _Bool value) {\n"
      "  return state->value = value;\n"
      "}\n";
  static const char atomic_source[] =
      "struct flags { _Atomic unsigned int value : 3; };\n"
      "unsigned int write_atomic(struct flags *state, unsigned int value) {\n"
      "  return state->value = value;\n"
      "}\n";
  static const char packed_source[] =
      "struct flags { unsigned int value : 3; } "
      "__attribute__((packed));\n"
      "unsigned int write_packed(struct flags *state, unsigned int value) {\n"
      "  return state->value = value;\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t narrow_unit;
  ctool_c_translation_unit_t bool_unit;
  ctool_c_translation_unit_t atomic_unit;
  ctool_c_translation_unit_t packed_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_member_layout_t *invalid_layouts = NULL;
  ctool_c_record_member_t *invalid_members = NULL;
  ctool_c_ir_unit_t first;
  ctool_c_ir_unit_t second;
  ctool_u64 fingerprint;
  ctool_u64 ir_fingerprint;
  ctool_u32 diagnostic_count;
  ctool_u32 red_member;
  ctool_status_t status;
  int passed = 0;

  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&narrow_unit, 0, sizeof(narrow_unit));
  (void)memset(&bool_unit, 0, sizeof(bool_unit));
  (void)memset(&atomic_unit, 0, sizeof(atomic_unit));
  (void)memset(&packed_unit, 0, sizeof(packed_unit));
  (void)memset(&first, 0xa5, sizeof(first));
  (void)memset(&second, 0xa5, sizeof(second));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_doom_bit_field_writes_are_unchanged(job) ||
      !parse_source(job, "/bit-field-stores.c", source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &unit, &first);
  if (!check_status(status, CTOOL_OK, "bit-field store lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_bit_field_store_ir(&unit, &first)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  ir_fingerprint = ir_instruction_fingerprint(&first);
  status = ctool_c_lower_ir(job, &unit, &second);
  if (!check_status(status, CTOOL_OK, "repeat bit-field store lowering") ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_bit_field_store_ir(&unit, &second) ||
      ir_instruction_fingerprint(&second) != ir_fingerprint) {
    (void)fprintf(stderr, "bit-field store IR is not deterministic\n");
    goto cleanup;
  }
  if (!parse_source(job, "/narrow-bit-field-store.c", narrow_source,
                    &narrow_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &narrow_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "narrow bit-field store") ||
      !parse_source(job, "/bool-bit-field-store.c", bool_source,
                    &bool_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &bool_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "Boolean bit-field store") ||
      !parse_source(job, "/atomic-bit-field-store.c", atomic_source,
                    &atomic_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &atomic_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic bit-field store") ||
      !parse_source_mode(job, "/packed-bit-field-store.c", packed_source,
                         CTOOL_TRUE, &packed_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &packed_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "packed bit-field store")) {
    goto cleanup;
  }
  red_member = find_member(&unit, "r");
  invalid_layouts = (ctool_c_member_layout_t *)malloc(
      (size_t)unit.layout.member_count * sizeof(*invalid_layouts));
  invalid_members = (ctool_c_record_member_t *)malloc(
      (size_t)unit.graph.member_count * sizeof(*invalid_members));
  if (red_member == CTOOL_C_AST_NONE || invalid_layouts == NULL ||
      invalid_members == NULL) {
    goto cleanup;
  }
  invalid_unit = unit;
  invalid_unit.layout.members = invalid_layouts;
  (void)memcpy(invalid_layouts, unit.layout.members,
               (size_t)unit.layout.member_count * sizeof(*invalid_layouts));
  invalid_layouts[red_member].bit_width++;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "bit-field store layout width")) {
    goto cleanup;
  }
  invalid_unit = unit;
  invalid_unit.graph.members = invalid_members;
  (void)memcpy(invalid_members, unit.graph.members,
               (size_t)unit.graph.member_count * sizeof(*invalid_members));
  invalid_members[red_member].bit_width++;
  if (!expect_ir_failure_preserves_unit(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "bit-field store graph width")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&second, 0xa5, sizeof(second));
  status = ctool_c_lower_ir(job, &unit, &second);
  if (!check_status(status, CTOOL_OK, "bit-field store recovery") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      ir_instruction_fingerprint(&second) != ir_fingerprint) {
    (void)fprintf(stderr, "bit-field store lowering did not recover\n");
    goto cleanup;
  }
  passed = 1;

cleanup:
  free(invalid_members);
  free(invalid_layouts);
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("bit-field-stores: ok");
    return 0;
  }
  return 1;
}

static int ir_type_is_wide_integer(
    const ctool_c_translation_unit_t *unit, ctool_u32 type) {
  return type < unit->layout.type_count &&
                 unit->layout.types[type].is_integer == CTOOL_TRUE &&
                 unit->layout.types[type].is_object == CTOOL_TRUE &&
                 unit->layout.types[type].is_complete_object == CTOOL_TRUE &&
                 unit->layout.types[type].size == 8u
             ? 1
             : 0;
}

static int ir_type_has_volatile_qualification(
    const ctool_c_translation_unit_t *unit, ctool_u32 type) {
  ctool_u32 traversed = 0u;
  if (unit == NULL) {
    return 0;
  }
  while (type < unit->graph.type_count &&
         traversed++ < unit->graph.type_count) {
    const ctool_c_type_node_t *node = &unit->graph.types[type];
    if ((node->qualifiers & CTOOL_C_QUAL_VOLATILE) != 0u) {
      return 1;
    }
    if (node->kind != CTOOL_C_TYPE_ALIGNED &&
        node->kind != CTOOL_C_TYPE_QUALIFIED) {
      return 0;
    }
    type = node->referenced_type;
  }
  return 0;
}

static int validate_wide_return_ir(const ctool_c_translation_unit_t *unit,
                                   const ctool_c_ir_unit_t *ir) {
  ctool_u32 wide_constants = 0u;
  ctool_u32 direct_calls = 0u;
  ctool_u32 indirect_calls = 0u;
  ctool_u32 wide_returns = 0u;
  ctool_u32 wide_discards = 0u;
  ctool_bool payloads_valid = CTOOL_TRUE;
  ctool_u32 index;
  if (unit->function_definition_count != 6u || ir->function_count != 6u ||
      ir->functions == NULL || ir->instructions == NULL) {
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction = &ir->instructions[index];
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_INTEGER &&
        ir_type_is_wide_integer(unit, instruction->type) != 0) {
      wide_constants++;
      if (instruction->input_type != CTOOL_C_TYPE_NONE ||
          instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          instruction->reference != CTOOL_C_AST_NONE ||
          (instruction->integer_bits != UINT64_C(0xffffffff) &&
           instruction->integer_bits != UINT64_C(0xffffffffffffffff) &&
           instruction->integer_bits != UINT64_C(0x1122334455667788))) {
        payloads_valid = CTOOL_FALSE;
      }
    } else if ((instruction->kind ==
                    CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
                instruction->kind ==
                    CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT) &&
               ir_type_is_wide_integer(unit, instruction->type) != 0) {
      if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT) {
        direct_calls++;
      } else {
        indirect_calls++;
      }
      if (instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT
               ? instruction->reference == CTOOL_C_AST_NONE
               : instruction->reference != CTOOL_C_AST_NONE) ||
          instruction->integer_bits != 0u) {
        payloads_valid = CTOOL_FALSE;
      }
    } else if (instruction->kind ==
                   CTOOL_C_IR_INSTRUCTION_RETURN_VALUE &&
               ir_type_is_wide_integer(unit, instruction->type) != 0) {
      wide_returns++;
      if (ir_type_is_wide_integer(unit, instruction->input_type) == 0 ||
          instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          instruction->reference != CTOOL_C_AST_NONE ||
          instruction->integer_bits != 0u) {
        payloads_valid = CTOOL_FALSE;
      }
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_DISCARD &&
               ir_type_is_wide_integer(unit, instruction->input_type) != 0) {
      wide_discards++;
      if (instruction->type != CTOOL_C_TYPE_NONE ||
          instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          instruction->reference != CTOOL_C_AST_NONE ||
          instruction->integer_bits != 0u) {
        payloads_valid = CTOOL_FALSE;
      }
    }
  }
  return payloads_valid == CTOOL_TRUE && wide_constants == 3u &&
                 direct_calls == 2u && indirect_calls == 1u &&
                 wide_returns == 4u && wide_discards == 1u
             ? 1
             : 0;
}

static int validate_wide_object_ir(const ctool_c_translation_unit_t *unit,
                                   const ctool_c_ir_unit_t *ir) {
  static const struct {
    const char *name;
    ctool_u32 instruction_count;
    ctool_u32 stack_depth;
    uint64_t fingerprint;
  } expected[] = {
      {"get_cpu_freq", 3u, 1u, UINT64_C(0x3f56fdf59f187fc8)},
      {"load_pointer", 5u, 1u, UINT64_C(0xae61ec1f5600b983)},
      {"local_round_trip", 9u, 2u, UINT64_C(0x875abfc33f0f8186)},
      {"assign_pointer", 9u, 2u, UINT64_C(0xb6941ebe5bbeba14)},
      {"chain_pointer", 13u, 3u, UINT64_C(0xbbf4c111cc336e9e)},
      {"load_member", 6u, 1u, UINT64_C(0xe884a0753bb2b92c)},
      {"aggregate_round_trip", 17u, 2u,
       UINT64_C(0xcb112ad3602fa5a9)},
      {"load_index", 8u, 2u, UINT64_C(0x5806c4f5a4e65f02)},
      {"block_static", 3u, 1u, UINT64_C(0x8c5b37cafbacded7)},
      {"volatile_round_trip", 14u, 2u,
       UINT64_C(0xc33090682d9357a9)},
      {"discard_pointer", 6u, 1u, UINT64_C(0x5f3eed00ffd90fc0)}};
  ctool_u32 loads = 0u;
  ctool_u32 stores = 0u;
  ctool_u32 value_stores = 0u;
  ctool_u32 file_addresses = 0u;
  ctool_u32 local_addresses = 0u;
  ctool_u32 member_addresses = 0u;
  ctool_u32 indexed_addresses = 0u;
  ctool_u32 returns = 0u;
  ctool_u32 discards = 0u;
  ctool_u32 volatile_loads = 0u;
  ctool_u32 wide_types = 0u;
  ctool_u32 expected_first = 0u;
  ctool_bool functions_valid = CTOOL_TRUE;
  ctool_u32 index;
  if (unit == NULL || ir == NULL ||
      unit->function_definition_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      ir->function_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      ir->functions == NULL ||
      ir->instructions == NULL) {
    return 0;
  }
  for (index = 0u; index < unit->layout.type_count; index++) {
    if (ir_type_is_wide_integer(unit, index) != 0) {
      if (unit->layout.types[index].alignment != 4u) {
        return 0;
      }
      wide_types++;
    }
  }
  for (index = 0u; index < ir->function_count; index++) {
    const ctool_c_function_definition_t *definition =
        &unit->function_definitions[index];
    const ctool_c_ir_function_t *function = &ir->functions[index];
    const ctool_c_binding_t *binding =
        definition->binding < unit->binding_count
            ? &unit->bindings[definition->binding]
            : NULL;
    uint64_t fingerprint = ir_instruction_slice_fingerprint(
        ir, function->first_instruction, function->instruction_count);
    if (binding == NULL ||
        string_equal(binding->name, expected[index].name) == 0 ||
        function->binding != definition->binding ||
        function->declared_type != definition->declared_type ||
        function->first_instruction != expected_first ||
        function->instruction_count != expected[index].instruction_count ||
        function->maximum_stack_depth != expected[index].stack_depth ||
        fingerprint != expected[index].fingerprint) {
      (void)fprintf(
          stderr,
          "wide object function %u (%s): first=%u count=%u depth=%u "
          "fingerprint=%016llx\n",
          (unsigned int)index, expected[index].name,
          (unsigned int)function->first_instruction,
          (unsigned int)function->instruction_count,
          (unsigned int)function->maximum_stack_depth,
          (unsigned long long)fingerprint);
      functions_valid = CTOOL_FALSE;
    }
    if (expected_first > ir->instruction_count ||
        function->instruction_count > ir->instruction_count - expected_first) {
      return 0;
    }
    expected_first += function->instruction_count;
  }
  if (expected_first != ir->instruction_count) {
    return 0;
  }
  for (index = 0u; index < ir->instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction = &ir->instructions[index];
    int wide_type = ir_type_is_wide_integer(unit, instruction->type);
    int wide_input = ir_type_is_wide_integer(unit, instruction->input_type);
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD &&
        wide_type != 0) {
      if (wide_input == 0 ||
          instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion !=
              CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
          instruction->reference != CTOOL_C_AST_NONE ||
          instruction->integer_bits != 0u) {
        return 0;
      }
      loads++;
      if (ir_type_has_volatile_qualification(unit,
                                             instruction->input_type) != 0) {
        volatile_loads++;
      }
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE &&
               wide_type != 0) {
      if (wide_input == 0 ||
          instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          instruction->reference != CTOOL_C_AST_NONE ||
          instruction->integer_bits != 0u) {
        return 0;
      }
      stores++;
    } else if (instruction->kind ==
                   CTOOL_C_IR_INSTRUCTION_STORE_VALUE &&
               wide_type != 0) {
      if (wide_input == 0 ||
          instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          instruction->reference != CTOOL_C_AST_NONE ||
          instruction->integer_bits != 0u) {
        return 0;
      }
      value_stores++;
    } else if (instruction->kind ==
                   CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS &&
               wide_type != 0) {
      if (instruction->input_type != CTOOL_C_TYPE_NONE ||
          instruction->reference == CTOOL_C_AST_NONE) {
        return 0;
      }
      file_addresses++;
    } else if (instruction->kind ==
                   CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS &&
               wide_type != 0) {
      if (instruction->input_type != CTOOL_C_TYPE_NONE ||
          instruction->reference == CTOOL_C_AST_NONE) {
        return 0;
      }
      local_addresses++;
    } else if (instruction->kind ==
                   CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS &&
               wide_type != 0) {
      if (instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          instruction->reference == CTOOL_C_AST_NONE ||
          instruction->integer_bits != 0u) {
        return 0;
      }
      member_addresses++;
    } else if (instruction->kind ==
               CTOOL_C_IR_INSTRUCTION_POINTER_BINARY) {
      if (instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
          instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          instruction->reference >= unit->layout.type_count ||
          unit->layout.types[instruction->reference].is_integer !=
              CTOOL_TRUE ||
          unit->layout.types[instruction->reference].size != 4u ||
          instruction->integer_bits != 0u) {
        return 0;
      }
      indexed_addresses++;
    } else if (instruction->kind ==
                   CTOOL_C_IR_INSTRUCTION_RETURN_VALUE &&
               wide_type != 0) {
      if (wide_input == 0) {
        return 0;
      }
      returns++;
    } else if (instruction->kind == CTOOL_C_IR_INSTRUCTION_DISCARD &&
               wide_input != 0) {
      discards++;
    }
  }
  if (functions_valid == CTOOL_FALSE || wide_types == 0u || loads != 14u ||
      stores != 2u ||
      value_stores != 4u || file_addresses != 1u ||
      local_addresses != 3u || member_addresses != 3u ||
      indexed_addresses != 1u || returns != 10u || discards != 2u ||
      volatile_loads != 1u) {
    (void)fprintf(
        stderr,
        "wide object IR inventory differs: loads=%u stores=%u "
        "value-stores=%u files=%u locals=%u members=%u indexes=%u "
        "returns=%u discards=%u volatile-loads=%u\n",
        (unsigned int)loads, (unsigned int)stores,
        (unsigned int)value_stores, (unsigned int)file_addresses,
        (unsigned int)local_addresses, (unsigned int)member_addresses,
        (unsigned int)indexed_addresses, (unsigned int)returns,
        (unsigned int)discards, (unsigned int)volatile_loads);
    return 0;
  }
  return 1;
}

static int run_wide_objects(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t atomic_load_unit;
  ctool_c_translation_unit_t atomic_store_unit;
  ctool_c_ir_unit_t first_ir;
  ctool_c_ir_unit_t second_ir;
  uint64_t fingerprint;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&atomic_load_unit, 0, sizeof(atomic_load_unit));
  (void)memset(&atomic_store_unit, 0, sizeof(atomic_store_unit));
  (void)memset(&first_ir, 0xa5, sizeof(first_ir));
  (void)memset(&second_ir, 0xa5, sizeof(second_ir));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_is_unchanged(job) ||
      !parse_source(job, "/wide-objects.c", wide_object_source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &unit, &first_ir);
  if (!check_status(status, CTOOL_OK, "wide object lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_wide_object_ir(&unit, &first_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  status = ctool_c_lower_ir(job, &unit, &second_ir);
  if (!check_status(status, CTOOL_OK, "repeat wide object lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_wide_object_ir(&unit, &second_ir) ||
      ir_instruction_fingerprint(&first_ir) !=
          ir_instruction_fingerprint(&second_ir)) {
    (void)fprintf(stderr, "wide object lowering is not deterministic\n");
    goto cleanup;
  }
  if (!parse_source(job, "/wide-atomic-load.c", wide_atomic_load_source,
                    &atomic_load_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &atomic_load_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide atomic load") ||
      !parse_source(job, "/wide-atomic-store.c", wide_atomic_store_source,
                    &atomic_store_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &atomic_store_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide atomic store")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("wide-objects: ok");
    return 0;
  }
  return 1;
}

static int run_wide_returns(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t parameter_unit;
  ctool_c_translation_unit_t arithmetic_unit;
  ctool_c_translation_unit_t conversion_unit;
  ctool_c_ir_unit_t first_ir;
  ctool_c_ir_unit_t second_ir;
  uint64_t fingerprint;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  int passed = 0;
  (void)memset(&unit, 0, sizeof(unit));
  (void)memset(&parameter_unit, 0, sizeof(parameter_unit));
  (void)memset(&arithmetic_unit, 0, sizeof(arithmetic_unit));
  (void)memset(&conversion_unit, 0, sizeof(conversion_unit));
  (void)memset(&first_ir, 0xa5, sizeof(first_ir));
  (void)memset(&second_ir, 0xa5, sizeof(second_ir));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_is_unchanged(job) ||
      !parse_source(job, "/wide-returns.c", wide_return_source, &unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = ctool_c_lower_ir(job, &unit, &first_ir);
  if (!check_status(status, CTOOL_OK, "wide return lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_wide_return_ir(&unit, &first_ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  status = ctool_c_lower_ir(job, &unit, &second_ir);
  if (!check_status(status, CTOOL_OK, "repeat wide return lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unit) != fingerprint ||
      !validate_wide_return_ir(&unit, &second_ir) ||
      ir_instruction_fingerprint(&first_ir) !=
          ir_instruction_fingerprint(&second_ir)) {
    (void)fprintf(stderr, "wide return lowering is not deterministic\n");
    goto cleanup;
  }
  if (!parse_source(job, "/wide-parameter.c", wide_parameter_source,
                    &parameter_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &parameter_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports cdecl functions with represented "
          "scalar or structure parameters and void, scalar, or structure "
          "results",
          "wide parameter ABI") ||
      !parse_source(job, "/wide-arithmetic.c", wide_arithmetic_source,
                    &arithmetic_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &arithmetic_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide arithmetic") ||
      !parse_source(job, "/wide-conversion.c", wide_conversion_source,
                    &conversion_unit) ||
      !expect_ir_failure_preserves_unit(
          job, &conversion_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide conversion")) {
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  if (passed != 0) {
    (void)puts("wide-returns: ok");
    return 0;
  }
  return 1;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "active-leaf") == 0) {
    return run_active_leaf(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "forward-goto") == 0) {
    return run_forward_goto(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "nested-goto") == 0) {
    return run_nested_goto(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "switch-lowering") == 0) {
    return run_switch_lowering(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "switch-control") == 0) {
    return run_switch_control(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "switch-nesting") == 0) {
    return run_switch_nesting(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "integer-updates") == 0) {
    return run_integer_updates(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "integer-compounds") == 0) {
    return run_integer_compounds(argv[2]);
  }
  if (argc == 3 &&
      strcmp(argv[1], "integer-compound-conversions") == 0) {
    return run_integer_compound_conversions(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "integer-update-conversions") == 0) {
    return run_integer_update_conversions(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "narrow-mutations") == 0) {
    return run_narrow_mutations(argv[2]);
  }
  if (argc == 3 &&
      strcmp(argv[1], "integer-mutation-rejections") == 0) {
    return run_integer_mutation_rejections(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "pointer-member-loads") == 0) {
    return run_pointer_member_loads(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "pointer-values") == 0) {
    return run_pointer_values(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "pointer-comparisons") == 0) {
    return run_pointer_comparisons(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "pointer-conditions") == 0) {
    return run_pointer_conditions(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "pointer-arithmetic") == 0) {
    return run_pointer_arithmetic(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "function-pointers") == 0) {
    return run_function_pointers(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "automatic-objects") == 0) {
    return run_automatic_objects(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "block-externs") == 0) {
    return run_block_externs(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "block-functions") == 0) {
    return run_block_functions(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "block-typedefs") == 0) {
    return run_block_typedefs(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "aggregate-initializers") == 0) {
    return run_aggregate_initializers(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "structure-values") == 0) {
    return run_structure_values(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "compound-literals") == 0) {
    return run_compound_literals(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "old-style-empty-functions") == 0) {
    return run_old_style_empty_functions(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "variadic-callees") == 0) {
    return run_variadic_callees(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "block-records") == 0) {
    return run_block_records(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "block-enums") == 0) {
    return run_block_enums(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "bit-field-stores") == 0) {
    return run_bit_field_stores(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "bit-field-mutations") == 0) {
    return run_bit_field_mutations(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "narrow-values") == 0) {
    return run_narrow_values(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "void-casts") == 0) {
    return run_void_casts(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "wide-returns") == 0) {
    return run_wide_returns(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "wide-objects") == 0) {
    return run_wide_objects(argv[2]);
  }
  (void)fprintf(stderr,
                "usage: cupidc-ir-contract "
                "active-leaf|forward-goto|nested-goto|switch-lowering|"
                "switch-control|switch-nesting|integer-updates|"
                "integer-compounds|integer-compound-conversions|"
                "integer-update-conversions|narrow-mutations|"
                "integer-mutation-rejections|pointer-member-loads|"
                "pointer-values|pointer-comparisons|pointer-conditions|"
                "pointer-arithmetic|function-pointers|automatic-objects|"
                "block-externs|block-functions|block-typedefs|"
                "aggregate-initializers|"
                "compound-literals|"
                "old-style-empty-functions|variadic-callees|block-records|"
                "block-enums|bit-field-stores|bit-field-mutations|"
                "narrow-values|void-casts|wide-returns|wide-objects "
                "HOST_ROOT\n");
  return 2;
}
