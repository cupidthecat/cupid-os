#include "cupidc_emit.h"

#include "cupidc_ir.h"
#include "elf32.h"
#include "x86.h"

#define CEMIT_SECTION_TEXT 0u
#define CEMIT_SECTION_RODATA 1u
#define CEMIT_SECTION_DATA 2u
#define CEMIT_SECTION_BSS 3u
#define CEMIT_SECTION_COUNT 4u

#define CEMIT_WIDE_DIVIDEND_LOW_STACK 0u
#define CEMIT_WIDE_DIVIDEND_HIGH_STACK 4u
#define CEMIT_WIDE_DIVISOR_LOW_STACK 8u
#define CEMIT_WIDE_DIVISOR_HIGH_STACK 12u
#define CEMIT_WIDE_QUOTIENT_LOW_STACK 16u
#define CEMIT_WIDE_QUOTIENT_HIGH_STACK 20u
#define CEMIT_WIDE_REMAINDER_LOW_STACK 24u
#define CEMIT_WIDE_REMAINDER_HIGH_STACK 28u
#define CEMIT_WIDE_QUOTIENT_SIGN_STACK 32u
#define CEMIT_WIDE_REMAINDER_SIGN_STACK 36u
#define CEMIT_WIDE_DIVIDE_STACK_SIZE 40u

typedef enum {
  CEMIT_FILE_ASSEMBLY_NONE = 0,
  CEMIT_FILE_ASSEMBLY_SQRT,
  CEMIT_FILE_ASSEMBLY_SQRTF,
  CEMIT_FILE_ASSEMBLY_SIN,
  CEMIT_FILE_ASSEMBLY_SINF,
  CEMIT_FILE_ASSEMBLY_COS,
  CEMIT_FILE_ASSEMBLY_COSF,
  CEMIT_FILE_ASSEMBLY_TAN,
  CEMIT_FILE_ASSEMBLY_TANF,
  CEMIT_FILE_ASSEMBLY_ATAN,
  CEMIT_FILE_ASSEMBLY_ATANF,
  CEMIT_FILE_ASSEMBLY_ATAN2,
  CEMIT_FILE_ASSEMBLY_ATAN2F,
  CEMIT_FILE_ASSEMBLY_FABS,
  CEMIT_FILE_ASSEMBLY_FABSF,
  CEMIT_FILE_ASSEMBLY_FLOOR,
  CEMIT_FILE_ASSEMBLY_FLOORF,
  CEMIT_FILE_ASSEMBLY_CEIL,
  CEMIT_FILE_ASSEMBLY_CEILF,
  CEMIT_FILE_ASSEMBLY_ROUND,
  CEMIT_FILE_ASSEMBLY_ROUNDF,
  CEMIT_FILE_ASSEMBLY_TRUNC,
  CEMIT_FILE_ASSEMBLY_TRUNCF,
  CEMIT_FILE_ASSEMBLY_FMOD,
  CEMIT_FILE_ASSEMBLY_FMODF,
  CEMIT_FILE_ASSEMBLY_EXP2,
  CEMIT_FILE_ASSEMBLY_EXP2F,
  CEMIT_FILE_ASSEMBLY_EXP,
  CEMIT_FILE_ASSEMBLY_EXPF,
  CEMIT_FILE_ASSEMBLY_LOG2,
  CEMIT_FILE_ASSEMBLY_LOG2F,
  CEMIT_FILE_ASSEMBLY_LOG,
  CEMIT_FILE_ASSEMBLY_LOGF,
  CEMIT_FILE_ASSEMBLY_POW,
  CEMIT_FILE_ASSEMBLY_POWF,
  CEMIT_FILE_ASSEMBLY_ASIN,
  CEMIT_FILE_ASSEMBLY_ASINF,
  CEMIT_FILE_ASSEMBLY_ACOS,
  CEMIT_FILE_ASSEMBLY_ACOSF,
  CEMIT_FILE_ASSEMBLY_SINH,
  CEMIT_FILE_ASSEMBLY_SINHF,
  CEMIT_FILE_ASSEMBLY_COSH,
  CEMIT_FILE_ASSEMBLY_COSHF,
  CEMIT_FILE_ASSEMBLY_TANH,
  CEMIT_FILE_ASSEMBLY_TANHF,
  CEMIT_FILE_ASSEMBLY_CBRT,
  CEMIT_FILE_ASSEMBLY_CBRTF,
  CEMIT_FILE_ASSEMBLY_HYPOT,
  CEMIT_FILE_ASSEMBLY_HYPOTF,
  CEMIT_FILE_ASSEMBLY_NEXTAFTER,
  CEMIT_FILE_ASSEMBLY_NEXTAFTERF,
  CEMIT_FILE_ASSEMBLY_FABS_MASKS,
  CEMIT_FILE_ASSEMBLY_EXP_LOG_CONSTANTS,
  CEMIT_FILE_ASSEMBLY_DGLIBC_JUMPS,
  CEMIT_FILE_ASSEMBLY_COUNT
} cemit_file_assembly_kind_t;

typedef struct {
  ctool_string_t name;
  ctool_u32 flags;
  ctool_u32 alignment;
  ctool_buffer_t *contents;
} cemit_named_section_t;

typedef struct {
  ctool_job_t *job;
  const ctool_c_translation_unit_t *unit;
  ctool_c_ir_unit_t ir;
  ctool_arena_t *arena;
  ctool_buffer_t *text;
  ctool_buffer_t *active_text;
  ctool_buffer_t *rodata;
  ctool_buffer_t *data;
  ctool_buffer_t *object_output;
  ctool_u32 bss_size;
  ctool_u32 active_text_section;
  ctool_u32 section_alignment[CEMIT_SECTION_COUNT];
  cemit_named_section_t *named_sections;
  ctool_u32 named_section_count;
  ctool_u32 named_section_capacity;
  ctool_u32 *binding_sections;
  ctool_elf32_symbol_spec_t *symbols;
  ctool_u32 symbol_count;
  ctool_u32 symbol_capacity;
  ctool_elf32_relocation_spec_t *relocations;
  ctool_u32 relocation_count;
  ctool_u32 relocation_capacity;
  ctool_u32 *binding_symbols;
  ctool_u32 *binding_object_definitions;
  ctool_u32 *binding_function_definitions;
  ctool_u32 *file_assembly_bindings;
  ctool_u32 *file_assembly_callee_bindings;
  ctool_u32 *file_assembly_kinds;
  ctool_u32 fabs_mask_assembly;
  ctool_u32 fabs_mask_d_symbol;
  ctool_u32 fabs_mask_s_symbol;
  ctool_u32 exp_log_constant_assembly;
  ctool_u32 log2e_constant_symbol;
  ctool_u32 ln2_constant_symbol;
  ctool_u32 *block_binding_symbols;
  ctool_u32 *block_binding_offsets;
  ctool_u32 *compound_literal_offsets;
  ctool_u32 *compound_literal_staging_offsets;
  ctool_u32 *value_temporary_offsets;
  ctool_bool *binding_needed;
  ctool_bool *initializer_is_zero;
  ctool_u32 literal_count;
  ctool_bool failure_reported;
  ctool_status_t relation_status;
} cemit_context_t;

static ctool_bool cemit_ir_function_types_match(
    cemit_context_t *context, ctool_u32 left, ctool_u32 right);

static ctool_status_t cemit_patch_branch(ctool_buffer_t *text,
                                         ctool_u32 patch,
                                         ctool_u32 after,
                                         ctool_u32 target);

static ctool_status_t cemit_patch_short_branch(
    ctool_buffer_t *text, ctool_u32 patch, ctool_u32 after,
    ctool_u32 target);

static ctool_status_t cemit_x86_branch(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u32 *patch_out, ctool_u32 *after_out);

static ctool_status_t cemit_x86_short_branch(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u32 *patch_out, ctool_u32 *after_out);

static void cemit_zero(void *destination, ctool_u32 size) {
  ctool_u8 *bytes = (ctool_u8 *)destination;
  ctool_u32 index;
  for (index = 0u; index < size; index++) {
    bytes[index] = 0u;
  }
}

static ctool_bool cemit_add_overflows(ctool_u32 left, ctool_u32 right) {
  return left > 0xffffffffu - right ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool cemit_multiply_overflows(ctool_u32 left,
                                            ctool_u32 right) {
  return left != 0u && right > 0xffffffffu / left ? CTOOL_TRUE
                                                  : CTOOL_FALSE;
}

static ctool_bool cemit_power_of_two(ctool_u32 value) {
  return value != 0u && (value & (value - 1u)) == 0u ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
}

static ctool_bool cemit_strings_equal(ctool_string_t left,
                                      ctool_string_t right) {
  ctool_u32 index;
  if (left.size != right.size ||
      (left.size != 0u &&
       (left.data == (const char *)0 || right.data == (const char *)0))) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < left.size; index++) {
    if (left.data[index] != right.data[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool cemit_kernel_bss_clear_template(
    ctool_string_t template_text) {
  return cemit_strings_equal(
      template_text,
      ctool_string(
          "mov $0xF00000, %%esp\n"
          "mov %%esp, %%ebp\n"
          "mov $_bss_start, %%edi\n"
          "mov $_kernel_end, %%ecx\n"
          "sub %%edi, %%ecx\n"
          "shr $2, %%ecx\n"
          "xor %%eax, %%eax\n"
          "cld\n"
          "rep stosl\n"));
}

static ctool_bool cemit_find_external_object_binding(
    const cemit_context_t *context, ctool_string_t name,
    ctool_u32 *binding_out) {
  ctool_u32 index;
  if (context == (const cemit_context_t *)0 ||
      context->unit == (const ctool_c_translation_unit_t *)0 ||
      (context->unit->layout.type_count != 0u &&
       context->unit->layout.types ==
           (const ctool_c_type_layout_t *)0)) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < context->unit->binding_count; index++) {
    const ctool_c_binding_t *binding = &context->unit->bindings[index];
    if (binding->kind == CTOOL_C_BINDING_OBJECT &&
        binding->linkage == CTOOL_C_LINKAGE_EXTERNAL &&
        binding->file_scope_visible == CTOOL_TRUE &&
        binding->type < context->unit->graph.type_count &&
        binding->type < context->unit->layout.type_count &&
        context->unit->layout.types[binding->type].is_object ==
            CTOOL_TRUE &&
        cemit_strings_equal(binding->name, name) == CTOOL_TRUE) {
      if (binding_out != (ctool_u32 *)0) {
        *binding_out = index;
      }
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_bool cemit_kernel_bss_clear_assembly_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  const ctool_u32 register_clobbers =
      CTOOL_C_ASSEMBLY_EAX_CLOBBER |
      CTOOL_C_ASSEMBLY_ECX_CLOBBER |
      CTOOL_C_ASSEMBLY_EDI_CLOBBER;
  if (cemit_kernel_bss_clear_template(
          assembly->template_text) == CTOOL_FALSE) {
    return (assembly->flags & register_clobbers) == 0u
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return assembly->flags ==
                     (CTOOL_C_ASSEMBLY_VOLATILE |
                      CTOOL_C_ASSEMBLY_MEMORY_CLOBBER |
                      register_clobbers) &&
                 assembly->output_count == 0u &&
                 assembly->input_count == 0u &&
                 assembly->direct_call_binding_plus_one == 0u &&
                 cemit_find_external_object_binding(
                     context, ctool_string("_bss_start"),
                     (ctool_u32 *)0) == CTOOL_TRUE &&
                 cemit_find_external_object_binding(
                     context, ctool_string("_kernel_end"),
                     (ctool_u32 *)0) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_identifier_first_character(char character) {
  return (character >= 'a' && character <= 'z') ||
                 (character >= 'A' && character <= 'Z') ||
                 character == '_'
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_identifier_character(char character) {
  return cemit_identifier_first_character(character) == CTOOL_TRUE ||
                 (character >= '0' && character <= '9')
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_naked_ipi_wrapper_template(
    ctool_string_t template_text, ctool_string_t *callee_out) {
  static const char prefix[] = "pushal\ncall ";
  static const char suffix[] = "\npopal\niret\n";
  const ctool_u32 prefix_size = (ctool_u32)sizeof(prefix) - 1u;
  const ctool_u32 suffix_size = (ctool_u32)sizeof(suffix) - 1u;
  ctool_u32 callee_size;
  ctool_u32 index;
  if (template_text.data == (const char *)0 ||
      template_text.size <= prefix_size + suffix_size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < prefix_size; index++) {
    if (template_text.data[index] != prefix[index]) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < suffix_size; index++) {
    if (template_text.data[template_text.size - suffix_size + index] !=
        suffix[index]) {
      return CTOOL_FALSE;
    }
  }
  callee_size = template_text.size - prefix_size - suffix_size;
  if (cemit_identifier_first_character(
          template_text.data[prefix_size]) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  for (index = 1u; index < callee_size; index++) {
    if (cemit_identifier_character(
            template_text.data[prefix_size + index]) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  if (callee_out != (ctool_string_t *)0) {
    callee_out->data = template_text.data + prefix_size;
    callee_out->size = callee_size;
  }
  return CTOOL_TRUE;
}

static ctool_bool cemit_naked_panic_template(
    ctool_string_t template_text) {
  return cemit_strings_equal(
      template_text, ctool_string("cli\n1: hlt\njmp 1b\n"));
}

static ctool_bool cemit_naked_control_assembly_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  ctool_string_t callee = {0};
  ctool_bool wrapper =
      cemit_naked_ipi_wrapper_template(assembly->template_text, &callee);
  ctool_bool panic =
      cemit_naked_panic_template(assembly->template_text);
  if (wrapper == CTOOL_FALSE && panic == CTOOL_FALSE) {
    return assembly->direct_call_binding_plus_one == 0u
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (assembly->flags !=
          (CTOOL_C_ASSEMBLY_BASIC | CTOOL_C_ASSEMBLY_VOLATILE) ||
      assembly->output_count != 0u || assembly->input_count != 0u) {
    return CTOOL_FALSE;
  }
  if (panic == CTOOL_TRUE) {
    return assembly->direct_call_binding_plus_one == 0u
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (assembly->direct_call_binding_plus_one == 0u ||
      assembly->direct_call_binding_plus_one >
          context->unit->binding_count ||
      context->unit->bindings ==
          (const ctool_c_binding_t *)0) {
    return CTOOL_FALSE;
  }
  {
    const ctool_c_binding_t *binding =
        &context->unit->bindings[
            assembly->direct_call_binding_plus_one - 1u];
    return binding->kind == CTOOL_C_BINDING_FUNCTION &&
                   binding->file_scope_visible == CTOOL_TRUE &&
                   cemit_strings_equal(binding->name, callee) == CTOOL_TRUE
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
}

static ctool_status_t cemit_align_value(ctool_u32 value,
                                         ctool_u32 alignment,
                                         ctool_u32 *aligned_out) {
  ctool_u32 padding;
  if (aligned_out == (ctool_u32 *)0 ||
      cemit_power_of_two(alignment) == CTOOL_FALSE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  padding = (0u - value) & (alignment - 1u);
  if (cemit_add_overflows(value, padding) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  *aligned_out = value + padding;
  return CTOOL_OK;
}

static ctool_status_t cemit_emit_failure(
    cemit_context_t *context, ctool_status_t status, ctool_u32 code,
    const ctool_c_pp_location_t *location, const char *message) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t emitted;
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = code;
  diagnostic.path = location != (const ctool_c_pp_location_t *)0
                        ? location->path
                        : ctool_string("");
  diagnostic.line = location != (const ctool_c_pp_location_t *)0
                        ? location->line
                        : 0u;
  diagnostic.column = location != (const ctool_c_pp_location_t *)0
                          ? location->column
                          : 0u;
  diagnostic.message = ctool_string(message);
  context->failure_reported = CTOOL_TRUE;
  emitted = ctool_job_emit(context->job, &diagnostic);
  return emitted == CTOOL_OK ? status : emitted;
}

static ctool_status_t cemit_invalid_unit(
    cemit_context_t *context, const ctool_c_pp_location_t *location) {
  return cemit_emit_failure(
      context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_INVALID_UNIT, location,
      "CupidC object emission received an invalid translation unit");
}

static ctool_status_t cemit_alloc_array(cemit_context_t *context,
                                         ctool_u32 count,
                                         ctool_u32 element_size,
                                         void **array_out) {
  *array_out = (void *)0;
  if (count == 0u) {
    return CTOOL_OK;
  }
  return ctool_arena_alloc_zero(context->arena, count, element_size,
                                (ctool_u32)sizeof(void *), array_out);
}

static const ctool_c_type_node_t *cemit_type_node(
    const cemit_context_t *context, ctool_u32 type) {
  return type < context->unit->graph.type_count
             ? &context->unit->graph.types[type]
             : (const ctool_c_type_node_t *)0;
}

static const ctool_c_type_node_t *cemit_unwrapped_type(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cemit_type_node(context, type);
  ctool_u32 traversed = 0u;
  while (node != (const ctool_c_type_node_t *)0 &&
         (node->kind == CTOOL_C_TYPE_ALIGNED ||
          node->kind == CTOOL_C_TYPE_QUALIFIED)) {
    if (traversed++ >= context->unit->graph.type_count) {
      return (const ctool_c_type_node_t *)0;
    }
    node = cemit_type_node(context, node->referenced_type);
  }
  return node;
}

static ctool_bool cemit_underlying_type(
    const cemit_context_t *context, ctool_u32 type,
    ctool_u32 *qualifiers_out, const ctool_c_type_node_t **node_out) {
  const ctool_c_type_node_t *node;
  ctool_u32 qualifiers = 0u;
  ctool_u32 traversed = 0u;
  if (qualifiers_out == (ctool_u32 *)0 ||
      node_out == (const ctool_c_type_node_t **)0) {
    return CTOOL_FALSE;
  }
  *qualifiers_out = 0u;
  *node_out = (const ctool_c_type_node_t *)0;
  for (;;) {
    node = cemit_type_node(context, type);
    if (node == (const ctool_c_type_node_t *)0) {
      return CTOOL_FALSE;
    }
    qualifiers |= node->qualifiers;
    if (node->kind != CTOOL_C_TYPE_ALIGNED &&
        node->kind != CTOOL_C_TYPE_QUALIFIED) {
      *qualifiers_out = qualifiers;
      *node_out = node;
      return CTOOL_TRUE;
    }
    type = node->referenced_type;
    if (traversed++ >= context->unit->graph.type_count) {
      return CTOOL_FALSE;
    }
  }
}

static ctool_bool cemit_type_has_atomic_qualification(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node;
  ctool_u32 qualifiers;
  return cemit_underlying_type(context, type, &qualifiers, &node) ==
                     CTOOL_TRUE &&
                 (qualifiers & CTOOL_C_QUAL_ATOMIC) != 0u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

typedef struct {
  const ctool_c_type_node_t *record;
  const ctool_c_type_node_t *member_type;
  const ctool_c_type_node_t *result_type;
  const ctool_c_record_member_t *member;
  const ctool_c_type_layout_t *record_layout;
  const ctool_c_type_layout_t *member_type_layout;
  const ctool_c_type_layout_t *result_layout;
  const ctool_c_member_layout_t *member_layout;
} cemit_member_info_t;

static ctool_status_t cemit_validate_member_instruction(
    const cemit_context_t *context,
    const ctool_c_ir_instruction_t *ir_instruction,
    ctool_c_conversion_kind_t expected_conversion,
    cemit_member_info_t *info) {
  if (ir_instruction->input_type >= context->unit->graph.type_count ||
      ir_instruction->input_type >= context->unit->layout.type_count ||
      ir_instruction->type >= context->unit->graph.type_count ||
      ir_instruction->type >= context->unit->layout.type_count ||
      ir_instruction->reference >= context->unit->graph.member_count ||
      ir_instruction->reference >= context->unit->layout.member_count ||
      ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      ir_instruction->conversion != expected_conversion ||
      ir_instruction->integer_bits != 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  info->record =
      cemit_unwrapped_type(context, ir_instruction->input_type);
  info->member =
      &context->unit->graph.members[ir_instruction->reference];
  if (info->member->type >= context->unit->graph.type_count ||
      info->member->type >= context->unit->layout.type_count) {
    return CTOOL_ERR_INTERNAL;
  }
  info->member_type = cemit_unwrapped_type(context, info->member->type);
  info->result_type =
      cemit_unwrapped_type(context, ir_instruction->type);
  info->record_layout =
      &context->unit->layout.types[ir_instruction->input_type];
  info->member_type_layout =
      &context->unit->layout.types[info->member->type];
  info->result_layout =
      &context->unit->layout.types[ir_instruction->type];
  info->member_layout =
      &context->unit->layout.members[ir_instruction->reference];
  if (info->record == (const ctool_c_type_node_t *)0 ||
      info->record->kind != CTOOL_C_TYPE_RECORD ||
      info->record->record_complete == CTOOL_FALSE ||
      ir_instruction->reference < info->record->first_member ||
      ir_instruction->reference - info->record->first_member >=
          info->record->member_count ||
      info->member_type == (const ctool_c_type_node_t *)0 ||
      info->member_type != info->result_type ||
      info->record_layout->is_object == CTOOL_FALSE ||
      info->record_layout->is_complete_object == CTOOL_FALSE ||
      info->member_type_layout->is_object == CTOOL_FALSE ||
      info->member_type_layout->is_complete_object == CTOOL_FALSE ||
      info->result_layout->is_object == CTOOL_FALSE ||
      info->result_layout->is_complete_object == CTOOL_FALSE ||
      info->member_layout->size != info->member_type_layout->size ||
      info->result_layout->size != info->member_type_layout->size ||
      info->member_layout->byte_offset > info->record_layout->size ||
      info->member_layout->size >
          info->record_layout->size - info->member_layout->byte_offset) {
    return CTOOL_ERR_INTERNAL;
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_validate_i32_bit_field_instruction(
    const cemit_context_t *context,
    const ctool_c_ir_instruction_t *ir_instruction,
    ctool_c_conversion_kind_t expected_conversion,
    cemit_member_info_t *info) {
  ctool_status_t status = cemit_validate_member_instruction(
      context, ir_instruction, expected_conversion, info);
  if (status != CTOOL_OK) {
    return status;
  }
  if (info->member->is_bit_field != CTOOL_TRUE ||
      info->member->bit_width == 0u ||
      info->member_type_layout->is_integer == CTOOL_FALSE ||
      info->member_type_layout->size != 4u ||
      info->result_layout->is_integer == CTOOL_FALSE ||
      info->result_layout->size != 4u ||
      info->result_layout->is_signed !=
          info->member_type_layout->is_signed ||
      info->member_layout->size != 4u ||
      info->member_layout->bit_width != info->member->bit_width ||
      info->member_layout->bit_width == 0u ||
      info->member_layout->bit_offset >= 32u ||
      info->member_layout->bit_width >
          32u - info->member_layout->bit_offset) {
    return CTOOL_ERR_INTERNAL;
  }
  return CTOOL_OK;
}

static ctool_bool cemit_bit_field_promotion_is_valid(
    const cemit_context_t *context,
    const ctool_c_ir_instruction_t *ir_instruction) {
  const ctool_c_type_node_t *source;
  const ctool_c_type_node_t *target;
  const ctool_c_type_node_t *member_type;
  const ctool_c_type_node_t *compatible;
  const ctool_c_record_member_t *member;
  const ctool_c_type_layout_t *source_layout;
  const ctool_c_type_layout_t *target_layout;
  const ctool_c_type_layout_t *member_type_layout;
  const ctool_c_member_layout_t *member_layout;
  if (ir_instruction->conversion != CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
      ir_instruction->input_type >= context->unit->graph.type_count ||
      ir_instruction->input_type >= context->unit->layout.type_count ||
      ir_instruction->type >= context->unit->graph.type_count ||
      ir_instruction->type >= context->unit->layout.type_count ||
      ir_instruction->reference >= context->unit->graph.member_count ||
      ir_instruction->reference >= context->unit->layout.member_count) {
    return CTOOL_FALSE;
  }
  member = &context->unit->graph.members[ir_instruction->reference];
  if (member->type >= context->unit->graph.type_count ||
      member->type >= context->unit->layout.type_count) {
    return CTOOL_FALSE;
  }
  source = cemit_unwrapped_type(context, ir_instruction->input_type);
  target = cemit_unwrapped_type(context, ir_instruction->type);
  member_type = cemit_unwrapped_type(context, member->type);
  if (source == (const ctool_c_type_node_t *)0 ||
      target == (const ctool_c_type_node_t *)0 ||
      member_type == (const ctool_c_type_node_t *)0 ||
      source != member_type) {
    return CTOOL_FALSE;
  }
  compatible = source;
  if (compatible->kind == CTOOL_C_TYPE_ENUM) {
    compatible = cemit_unwrapped_type(context, compatible->referenced_type);
  }
  source_layout = &context->unit->layout.types[ir_instruction->input_type];
  target_layout = &context->unit->layout.types[ir_instruction->type];
  member_type_layout = &context->unit->layout.types[member->type];
  member_layout = &context->unit->layout.members[ir_instruction->reference];
  return compatible != (const ctool_c_type_node_t *)0 &&
                 compatible->kind == CTOOL_C_TYPE_UNSIGNED_INT &&
                 target->kind == CTOOL_C_TYPE_SIGNED_INT &&
                 member->is_bit_field == CTOOL_TRUE &&
                 member->bit_width != 0u && member->bit_width < 32u &&
                 source_layout->is_integer == CTOOL_TRUE &&
                 source_layout->is_signed == CTOOL_FALSE &&
                 source_layout->size == 4u &&
                 target_layout->is_integer == CTOOL_TRUE &&
                 target_layout->is_signed == CTOOL_TRUE &&
                 target_layout->size == 4u &&
                 member_type_layout->is_integer == CTOOL_TRUE &&
                 member_type_layout->is_signed == CTOOL_FALSE &&
                 member_type_layout->size == 4u && member_layout->size == 4u &&
                 member_layout->bit_width == member->bit_width &&
                 member_layout->bit_offset < 32u &&
                 member_layout->bit_width <= 32u - member_layout->bit_offset
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_type_is_const(const cemit_context_t *context,
                                       ctool_u32 type) {
  ctool_u32 traversed = 0u;
  const ctool_c_type_node_t *node = cemit_type_node(context, type);
  while (node != (const ctool_c_type_node_t *)0 &&
         traversed++ < context->unit->graph.type_count) {
    if ((node->qualifiers & CTOOL_C_QUAL_CONST) != 0u) {
      return CTOOL_TRUE;
    }
    if (node->kind == CTOOL_C_TYPE_ALIGNED ||
        node->kind == CTOOL_C_TYPE_QUALIFIED ||
        node->kind == CTOOL_C_TYPE_ARRAY) {
      node = cemit_type_node(context, node->referenced_type);
    } else {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_bool cemit_binding_has_function_definition(
    const ctool_c_translation_unit_t *unit, ctool_u32 binding) {
  ctool_u32 index;
  for (index = 0u; index < unit->function_definition_count; index++) {
    if (unit->function_definitions[index].binding == binding) {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_status_t cemit_validate_unit_shape(cemit_context_t *context) {
  const ctool_c_translation_unit_t *unit = context->unit;
  ctool_u32 binding;
  if ((unit->graph.type_count != 0u &&
       unit->graph.types == (const ctool_c_type_node_t *)0) ||
      (unit->graph.member_count != 0u &&
       unit->graph.members == (const ctool_c_record_member_t *)0) ||
      (unit->layout.type_count != unit->graph.type_count) ||
      (unit->layout.member_count != unit->graph.member_count) ||
      (unit->layout.type_count != 0u &&
       unit->layout.types == (const ctool_c_type_layout_t *)0) ||
      (unit->layout.member_count != 0u &&
       unit->layout.members == (const ctool_c_member_layout_t *)0) ||
      (unit->binding_count != 0u &&
       unit->bindings == (const ctool_c_binding_t *)0) ||
      (unit->block_binding_count != 0u &&
       unit->block_bindings == (const ctool_c_block_binding_t *)0) ||
      (unit->object_definition_count != 0u &&
       unit->object_definitions ==
           (const ctool_c_object_definition_t *)0) ||
      (unit->initializer_count != 0u &&
       unit->initializers == (const ctool_c_initializer_t *)0) ||
      (unit->initializer_element_count != 0u &&
       unit->initializer_elements ==
           (const ctool_c_initializer_element_t *)0) ||
      (unit->expression_count != 0u &&
       unit->expressions == (const ctool_c_expression_t *)0) ||
      (unit->assembly_count != 0u &&
       unit->assemblies == (const ctool_c_assembly_t *)0) ||
      (unit->file_assembly_count == 0u) !=
          (unit->file_assemblies == (const ctool_c_assembly_t *)0) ||
      (unit->assembly_operand_count != 0u &&
       unit->assembly_operands ==
           (const ctool_c_assembly_operand_t *)0) ||
      (unit->function_definition_count != 0u &&
       unit->function_definitions ==
           (const ctool_c_function_definition_t *)0)) {
    return cemit_invalid_unit(context,
                              (const ctool_c_pp_location_t *)0);
  }
  for (binding = 0u; binding < unit->binding_count; binding++) {
    const ctool_c_binding_t *candidate = &unit->bindings[binding];
    ctool_bool has_section =
        (candidate->attributes & CTOOL_C_DECL_ATTR_SECTION) != 0u
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool unused =
        (candidate->attributes & CTOOL_C_DECL_ATTR_UNUSED) != 0u
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool used =
        (candidate->attributes & CTOOL_C_DECL_ATTR_USED) != 0u
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool noinline =
        (candidate->attributes & CTOOL_C_DECL_ATTR_NOINLINE) != 0u
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool naked =
        (candidate->attributes & CTOOL_C_DECL_ATTR_NAKED) != 0u
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool general_regs_only =
        (candidate->attributes &
         CTOOL_C_DECL_ATTR_TARGET_GENERAL_REGS_ONLY) != 0u
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool weak =
        (candidate->attributes & CTOOL_C_DECL_ATTR_WEAK) != 0u
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool external_definition =
        (candidate->function_declaration_flags &
         CTOOL_C_FUNCTION_DECL_EXTERNAL_DEFINITION) != 0u
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool inline_declaration =
        (candidate->function_declaration_flags &
         CTOOL_C_FUNCTION_DECL_INLINE) != 0u
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if ((candidate->attributes & ~CTOOL_C_DECL_ATTR_ALL) != 0u ||
        (candidate->function_declaration_flags &
         ~CTOOL_C_FUNCTION_DECL_ALL) != 0u ||
        (candidate->function_declaration_flags != 0u &&
         candidate->kind != CTOOL_C_BINDING_FUNCTION) ||
        (inline_declaration == CTOOL_TRUE &&
         candidate->linkage == CTOOL_C_LINKAGE_EXTERNAL &&
         cemit_binding_has_function_definition(unit, binding) ==
             CTOOL_FALSE) ||
        (external_definition == CTOOL_TRUE &&
         (inline_declaration == CTOOL_FALSE ||
          candidate->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
          candidate->file_scope_visible == CTOOL_FALSE)) ||
        (weak == CTOOL_TRUE &&
         (candidate->kind != CTOOL_C_BINDING_OBJECT &&
          candidate->kind != CTOOL_C_BINDING_FUNCTION)) ||
        (weak == CTOOL_TRUE &&
         candidate->linkage != CTOOL_C_LINKAGE_EXTERNAL) ||
        (has_section == CTOOL_FALSE &&
         candidate->section_name.size != 0u) ||
        (has_section == CTOOL_TRUE &&
         candidate->type >= unit->graph.type_count) ||
        (has_section == CTOOL_TRUE &&
         (candidate->kind != CTOOL_C_BINDING_OBJECT &&
          candidate->kind != CTOOL_C_BINDING_FUNCTION)) ||
        (has_section == CTOOL_TRUE &&
         candidate->file_scope_visible == CTOOL_FALSE) ||
        (has_section == CTOOL_TRUE &&
         ctool_c_section_name_is_valid(candidate->section_name) ==
             CTOOL_FALSE) ||
        (unused == CTOOL_TRUE &&
         candidate->type >= unit->graph.type_count) ||
        (unused == CTOOL_TRUE &&
         (candidate->kind != CTOOL_C_BINDING_OBJECT &&
          candidate->kind != CTOOL_C_BINDING_FUNCTION)) ||
        (unused == CTOOL_TRUE &&
         candidate->file_scope_visible == CTOOL_FALSE) ||
        (used == CTOOL_TRUE &&
         candidate->type >= unit->graph.type_count) ||
        (used == CTOOL_TRUE &&
         (candidate->kind != CTOOL_C_BINDING_OBJECT &&
          candidate->kind != CTOOL_C_BINDING_FUNCTION)) ||
        (used == CTOOL_TRUE &&
         candidate->file_scope_visible == CTOOL_FALSE) ||
        ((noinline == CTOOL_TRUE || naked == CTOOL_TRUE ||
          general_regs_only == CTOOL_TRUE) &&
         (candidate->kind != CTOOL_C_BINDING_FUNCTION ||
          candidate->type >= unit->graph.type_count ||
          candidate->file_scope_visible == CTOOL_FALSE))) {
      return cemit_invalid_unit(context, &candidate->location);
    }
  }
  return CTOOL_OK;
}

static ctool_bool cemit_has_binding_text_relocation(
    ctool_c_ir_instruction_kind_t kind) {
  return kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
                 kind == CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
                 kind == CTOOL_C_IR_INSTRUCTION_FUNCTION_ADDRESS
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_index_definitions(cemit_context_t *context) {
  ctool_u32 index;
  if (context->ir.function_count !=
          context->unit->function_definition_count ||
      (context->unit->binding_count != 0u &&
       (context->binding_symbols == (ctool_u32 *)0 ||
        context->binding_object_definitions == (ctool_u32 *)0 ||
        context->binding_function_definitions == (ctool_u32 *)0 ||
        context->binding_needed == (ctool_bool *)0)) ||
      (context->ir.function_count != 0u &&
       context->ir.functions == (const ctool_c_ir_function_t *)0) ||
      (context->ir.instruction_count != 0u &&
       context->ir.instructions == (const ctool_c_ir_instruction_t *)0)) {
    return cemit_invalid_unit(context,
                              (const ctool_c_pp_location_t *)0);
  }
  for (index = 0u; index < context->unit->binding_count; index++) {
    context->binding_symbols[index] = CTOOL_C_AST_NONE;
    context->binding_object_definitions[index] = CTOOL_C_AST_NONE;
    context->binding_function_definitions[index] = CTOOL_C_AST_NONE;
  }
  for (index = 0u; index < context->unit->object_definition_count; index++) {
    const ctool_c_object_definition_t *definition =
        &context->unit->object_definitions[index];
    const ctool_c_binding_t *binding;
    if (definition->binding >= context->unit->binding_count ||
        definition->declared_type >= context->unit->graph.type_count ||
        definition->initializer >= context->unit->initializer_count ||
        context->binding_object_definitions[definition->binding] !=
            CTOOL_C_AST_NONE) {
      return cemit_invalid_unit(context, &definition->location);
    }
    binding = &context->unit->bindings[definition->binding];
    if (binding->kind != CTOOL_C_BINDING_OBJECT ||
        (definition->kind != CTOOL_C_OBJECT_DEFINITION_EXPLICIT &&
         definition->kind != CTOOL_C_OBJECT_DEFINITION_TENTATIVE)) {
      return cemit_invalid_unit(context, &definition->location);
    }
    context->binding_object_definitions[definition->binding] = index;
    context->binding_needed[definition->binding] = CTOOL_TRUE;
  }
  for (index = 0u; index < context->unit->function_definition_count;
       index++) {
    const ctool_c_function_definition_t *definition =
        &context->unit->function_definitions[index];
    const ctool_c_ir_function_t *function = &context->ir.functions[index];
    const ctool_c_binding_t *binding;
    if (definition->binding >= context->unit->binding_count ||
        definition->declared_type >= context->unit->graph.type_count ||
        definition->body >= context->unit->statement_count ||
        context->binding_object_definitions[definition->binding] !=
            CTOOL_C_AST_NONE ||
        context->binding_function_definitions[definition->binding] !=
            CTOOL_C_AST_NONE ||
        function->binding != definition->binding ||
        function->declared_type != definition->declared_type ||
        function->function_codegen_attributes !=
            (context->unit->bindings[definition->binding].attributes &
             CTOOL_C_DECL_ATTR_FUNCTION_CODEGEN) ||
        function->first_instruction > context->ir.instruction_count ||
        function->instruction_count >
            context->ir.instruction_count - function->first_instruction) {
      return cemit_invalid_unit(context, &definition->location);
    }
    binding = &context->unit->bindings[definition->binding];
    if (binding->kind != CTOOL_C_BINDING_FUNCTION ||
        cemit_strings_equal(
            function->section_name,
            (binding->attributes & CTOOL_C_DECL_ATTR_SECTION) != 0u
                ? binding->section_name
                : ctool_string("")) == CTOOL_FALSE) {
      return cemit_invalid_unit(context, &definition->location);
    }
    context->binding_function_definitions[definition->binding] = index;
    context->binding_needed[definition->binding] = CTOOL_TRUE;
  }
  for (index = 0u; index < context->ir.instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction =
        &context->ir.instructions[index];
    if (cemit_has_binding_text_relocation(instruction->kind) == CTOOL_TRUE) {
      const ctool_c_binding_t *binding;
      if (instruction->reference >= context->unit->binding_count) {
        return cemit_invalid_unit(context, &instruction->location);
      }
      binding = &context->unit->bindings[instruction->reference];
      if ((instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT &&
           (binding->kind != CTOOL_C_BINDING_FUNCTION ||
            cemit_ir_function_types_match(
                context, binding->type, instruction->input_type) ==
                CTOOL_FALSE)) ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS &&
           (binding->kind != CTOOL_C_BINDING_OBJECT ||
            binding->type != instruction->type)) ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_FUNCTION_ADDRESS &&
           (binding->kind != CTOOL_C_BINDING_FUNCTION ||
            cemit_ir_function_types_match(
                context, binding->type, instruction->type) ==
                CTOOL_FALSE))) {
        return cemit_invalid_unit(context, &instruction->location);
      }
      context->binding_needed[instruction->reference] = CTOOL_TRUE;
    } else if (instruction->kind ==
                   CTOOL_C_IR_INSTRUCTION_ASSEMBLY) {
      const ctool_c_assembly_t *assembly;
      ctool_u32 direct_binding;
      ctool_u32 bss_start_binding;
      ctool_u32 kernel_end_binding;
      if (instruction->reference >= context->unit->assembly_count ||
          context->unit->assemblies ==
              (const ctool_c_assembly_t *)0) {
        return cemit_invalid_unit(context, &instruction->location);
      }
      assembly =
          &context->unit->assemblies[instruction->reference];
      if (cemit_kernel_bss_clear_assembly_metadata_is_valid(
              context, assembly) == CTOOL_FALSE) {
        return cemit_invalid_unit(context, &assembly->location);
      }
      if (cemit_kernel_bss_clear_template(
              assembly->template_text) == CTOOL_TRUE) {
        if (cemit_find_external_object_binding(
                context, ctool_string("_bss_start"),
                &bss_start_binding) == CTOOL_FALSE ||
            cemit_find_external_object_binding(
                context, ctool_string("_kernel_end"),
                &kernel_end_binding) == CTOOL_FALSE) {
          return cemit_invalid_unit(context, &assembly->location);
        }
        context->binding_needed[bss_start_binding] = CTOOL_TRUE;
        context->binding_needed[kernel_end_binding] = CTOOL_TRUE;
        continue;
      }
      if (cemit_naked_control_assembly_metadata_is_valid(
              context, assembly) == CTOOL_FALSE) {
        return cemit_invalid_unit(context, &assembly->location);
      }
      if (assembly->direct_call_binding_plus_one == 0u) {
        continue;
      }
      direct_binding =
          assembly->direct_call_binding_plus_one - 1u;
      context->binding_needed[direct_binding] = CTOOL_TRUE;
    }
  }
  return CTOOL_OK;
}

static cemit_file_assembly_kind_t cemit_file_assembly_template_kind(
    ctool_string_t text) {
  static const char *const templates[] = {
      "",
      ".text\n\t.globl sqrt\n\t.type  sqrt, @function\n"
      "sqrt:\n\tmovsd  4(%esp), %xmm0\n\t"
      "sqrtsd %xmm0, %xmm0\n\tret\n\t.size  sqrt, .-sqrt\n",
      ".text\n\t.globl sqrtf\n\t.type  sqrtf, @function\n"
      "sqrtf:\n\tmovss  4(%esp), %xmm0\n\t"
      "sqrtss %xmm0, %xmm0\n\tret\n\t.size  sqrtf, .-sqrtf\n",
      ".text\n\t.globl sin\n\t.type  sin, @function\n"
      "sin:\n\tfldl   4(%esp)\n\tfsin\n\tsub    $8, %esp\n\t"
      "fstpl  (%esp)\n\tmovsd  (%esp), %xmm0\n\t"
      "add    $8, %esp\n\tret\n\t.size  sin, .-sin\n",
      ".text\n\t.globl sinf\n\t.type  sinf, @function\n"
      "sinf:\n\tflds   4(%esp)\n\tfsin\n\tsub    $4, %esp\n\t"
      "fstps  (%esp)\n\tmovss  (%esp), %xmm0\n\t"
      "add    $4, %esp\n\tret\n\t.size  sinf, .-sinf\n",
      ".text\n\t.globl cos\n\t.type  cos, @function\n"
      "cos:\n\tfldl   4(%esp)\n\tfcos\n\tsub    $8, %esp\n\t"
      "fstpl  (%esp)\n\tmovsd  (%esp), %xmm0\n\t"
      "add    $8, %esp\n\tret\n\t.size  cos, .-cos\n",
      ".text\n\t.globl cosf\n\t.type  cosf, @function\n"
      "cosf:\n\tflds   4(%esp)\n\tfcos\n\tsub    $4, %esp\n\t"
      "fstps  (%esp)\n\tmovss  (%esp), %xmm0\n\t"
      "add    $4, %esp\n\tret\n\t.size  cosf, .-cosf\n",
      ".text\n\t.globl tan\n\t.type  tan, @function\n"
      "tan:\n\tfldl   4(%esp)\n\tfptan\n\tfstp   %st(0)\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  tan, .-tan\n",
      ".text\n\t.globl tanf\n\t.type  tanf, @function\n"
      "tanf:\n\tflds   4(%esp)\n\tfptan\n\tfstp   %st(0)\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  tanf, .-tanf\n",
      ".text\n\t.globl atan\n\t.type  atan, @function\n"
      "atan:\n\tfldl   4(%esp)\n\tfld1\n\tfpatan\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  atan, .-atan\n",
      ".text\n\t.globl atanf\n\t.type  atanf, @function\n"
      "atanf:\n\tflds   4(%esp)\n\tfld1\n\tfpatan\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  atanf, .-atanf\n",
      ".text\n\t.globl atan2\n\t.type  atan2, @function\n"
      "atan2:\n\tfldl    4(%esp)\n\tfldl   12(%esp)\n\t"
      "fpatan\n\tsub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  atan2, .-atan2\n",
      ".text\n\t.globl atan2f\n\t.type  atan2f, @function\n"
      "atan2f:\n\tflds    4(%esp)\n\tflds    8(%esp)\n\t"
      "fpatan\n\tsub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  atan2f, .-atan2f\n",
      ".text\n\t.globl fabs\n\t.type  fabs, @function\n"
      "fabs:\n\tmovsd  4(%esp), %xmm0\n\t"
      "andpd  fabs_mask_d, %xmm0\n\tret\n\t"
      ".size  fabs, .-fabs\n",
      ".text\n\t.globl fabsf\n\t.type  fabsf, @function\n"
      "fabsf:\n\tmovss  4(%esp), %xmm0\n\t"
      "andps  fabs_mask_s, %xmm0\n\tret\n\t"
      ".size  fabsf, .-fabsf\n",
      ".text\n\t.globl floor\n\t.type  floor, @function\n"
      "floor:\n\tfldl   4(%esp)\n\tsub    $8, %esp\n\t"
      "fnstcw (%esp)\n\tmovw   (%esp), %ax\n\t"
      "andw   $0xF3FF, %ax\n\torw    $0x0400, %ax\n\t"
      "movw   %ax, 2(%esp)\n\tfldcw  2(%esp)\n\t"
      "frndint\n\tfldcw  (%esp)\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  floor, .-floor\n",
      ".text\n\t.globl floorf\n\t.type  floorf, @function\n"
      "floorf:\n\tflds   4(%esp)\n\tsub    $8, %esp\n\t"
      "fnstcw (%esp)\n\tmovw   (%esp), %ax\n\t"
      "andw   $0xF3FF, %ax\n\torw    $0x0400, %ax\n\t"
      "movw   %ax, 2(%esp)\n\tfldcw  2(%esp)\n\t"
      "frndint\n\tfldcw  (%esp)\n\tfstps  4(%esp)\n\t"
      "movss  4(%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  floorf, .-floorf\n",
      ".text\n\t.globl ceil\n\t.type  ceil, @function\n"
      "ceil:\n\tfldl   4(%esp)\n\tsub    $8, %esp\n\t"
      "fnstcw (%esp)\n\tmovw   (%esp), %ax\n\t"
      "andw   $0xF3FF, %ax\n\torw    $0x0800, %ax\n\t"
      "movw   %ax, 2(%esp)\n\tfldcw  2(%esp)\n\t"
      "frndint\n\tfldcw  (%esp)\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  ceil, .-ceil\n",
      ".text\n\t.globl ceilf\n\t.type  ceilf, @function\n"
      "ceilf:\n\tflds   4(%esp)\n\tsub    $8, %esp\n\t"
      "fnstcw (%esp)\n\tmovw   (%esp), %ax\n\t"
      "andw   $0xF3FF, %ax\n\torw    $0x0800, %ax\n\t"
      "movw   %ax, 2(%esp)\n\tfldcw  2(%esp)\n\t"
      "frndint\n\tfldcw  (%esp)\n\tfstps  4(%esp)\n\t"
      "movss  4(%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  ceilf, .-ceilf\n",
      ".text\n\t.globl round\n\t.type  round, @function\n"
      "round:\n\tfldl   4(%esp)\n\tsub    $8, %esp\n\t"
      "fnstcw (%esp)\n\tmovw   (%esp), %ax\n\t"
      "andw   $0xF3FF, %ax\n\tmovw   %ax, 2(%esp)\n\t"
      "fldcw  2(%esp)\n\tfrndint\n\tfldcw  (%esp)\n\t"
      "fstpl  (%esp)\n\tmovsd  (%esp), %xmm0\n\t"
      "add    $8, %esp\n\tret\n\t.size  round, .-round\n",
      ".text\n\t.globl roundf\n\t.type  roundf, @function\n"
      "roundf:\n\tflds   4(%esp)\n\tsub    $8, %esp\n\t"
      "fnstcw (%esp)\n\tmovw   (%esp), %ax\n\t"
      "andw   $0xF3FF, %ax\n\tmovw   %ax, 2(%esp)\n\t"
      "fldcw  2(%esp)\n\tfrndint\n\tfldcw  (%esp)\n\t"
      "fstps  4(%esp)\n\tmovss  4(%esp), %xmm0\n\t"
      "add    $8, %esp\n\tret\n\t.size  roundf, .-roundf\n",
      ".text\n\t.globl trunc\n\t.type  trunc, @function\n"
      "trunc:\n\tfldl   4(%esp)\n\tsub    $8, %esp\n\t"
      "fnstcw (%esp)\n\tmovw   (%esp), %ax\n\t"
      "andw   $0xF3FF, %ax\n\torw    $0x0C00, %ax\n\t"
      "movw   %ax, 2(%esp)\n\tfldcw  2(%esp)\n\t"
      "frndint\n\tfldcw  (%esp)\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  trunc, .-trunc\n",
      ".text\n\t.globl truncf\n\t.type  truncf, @function\n"
      "truncf:\n\tflds   4(%esp)\n\tsub    $8, %esp\n\t"
      "fnstcw (%esp)\n\tmovw   (%esp), %ax\n\t"
      "andw   $0xF3FF, %ax\n\torw    $0x0C00, %ax\n\t"
      "movw   %ax, 2(%esp)\n\tfldcw  2(%esp)\n\t"
      "frndint\n\tfldcw  (%esp)\n\tfstps  4(%esp)\n\t"
      "movss  4(%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  truncf, .-truncf\n",
      ".text\n\t.globl fmod\n\t.type  fmod, @function\n"
      "fmod:\n\tfldl   12(%esp)\n\tfldl    4(%esp)\n\t"
      "1:\n\tfprem\n\tfnstsw %ax\n\t"
      "testw  $0x0400, %ax\n\tjnz    1b\n\t"
      "fstp   %st(1)\n\tsub    $8, %esp\n\t"
      "fstpl  (%esp)\n\tmovsd  (%esp), %xmm0\n\t"
      "add    $8, %esp\n\tret\n\t.size  fmod, .-fmod\n",
      ".text\n\t.globl fmodf\n\t.type  fmodf, @function\n"
      "fmodf:\n\tflds   8(%esp)\n\tflds   4(%esp)\n\t"
      "1:\n\tfprem\n\tfnstsw %ax\n\t"
      "testw  $0x0400, %ax\n\tjnz    1b\n\t"
      "fstp   %st(1)\n\tsub    $4, %esp\n\t"
      "fstps  (%esp)\n\tmovss  (%esp), %xmm0\n\t"
      "add    $4, %esp\n\tret\n\t.size  fmodf, .-fmodf\n",
      ".text\n\t.globl exp2\n\t.type  exp2, @function\n"
      "exp2:\n\tfldl   4(%esp)\n\tfld    %st(0)\n\t"
      "frndint\n\tfsub   %st, %st(1)\n\tfxch\n\t"
      "f2xm1\n\tfld1\n\tfaddp\n\tfscale\n\t"
      "fstp   %st(1)\n\tsub    $8, %esp\n\t"
      "fstpl  (%esp)\n\tmovsd  (%esp), %xmm0\n\t"
      "add    $8, %esp\n\tret\n\t.size  exp2, .-exp2\n",
      ".text\n\t.globl exp2f\n\t.type  exp2f, @function\n"
      "exp2f:\n\tflds   4(%esp)\n\tfld    %st(0)\n\t"
      "frndint\n\tfsub   %st, %st(1)\n\tfxch\n\t"
      "f2xm1\n\tfld1\n\tfaddp\n\tfscale\n\t"
      "fstp   %st(1)\n\tsub    $4, %esp\n\t"
      "fstps  (%esp)\n\tmovss  (%esp), %xmm0\n\t"
      "add    $4, %esp\n\tret\n\t.size  exp2f, .-exp2f\n",
      ".text\n\t.globl exp\n\t.type  exp, @function\n"
      "exp:\n\tfldl   4(%esp)\n\t"
      "fldl   libm_log2e_const\n\tfmulp\n\t"
      "fld    %st(0)\n\tfrndint\n\t"
      "fsub   %st, %st(1)\n\tfxch\n\tf2xm1\n\t"
      "fld1\n\tfaddp\n\tfscale\n\tfstp   %st(1)\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  exp, .-exp\n",
      ".text\n\t.globl expf\n\t.type  expf, @function\n"
      "expf:\n\tflds   4(%esp)\n\t"
      "fldl   libm_log2e_const\n\tfmulp\n\t"
      "fld    %st(0)\n\tfrndint\n\t"
      "fsub   %st, %st(1)\n\tfxch\n\tf2xm1\n\t"
      "fld1\n\tfaddp\n\tfscale\n\tfstp   %st(1)\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  expf, .-expf\n",
      ".text\n\t.globl log2\n\t.type  log2, @function\n"
      "log2:\n\tfld1\n\tfldl   4(%esp)\n\tfyl2x\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  log2, .-log2\n",
      ".text\n\t.globl log2f\n\t.type  log2f, @function\n"
      "log2f:\n\tfld1\n\tflds   4(%esp)\n\tfyl2x\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  log2f, .-log2f\n",
      ".text\n\t.globl log\n\t.type  log, @function\n"
      "log:\n\tfldl   libm_ln2_const\n\t"
      "fldl   4(%esp)\n\tfyl2x\n\tsub    $8, %esp\n\t"
      "fstpl  (%esp)\n\tmovsd  (%esp), %xmm0\n\t"
      "add    $8, %esp\n\tret\n\t.size  log, .-log\n",
      ".text\n\t.globl logf\n\t.type  logf, @function\n"
      "logf:\n\tfldl   libm_ln2_const\n\t"
      "flds   4(%esp)\n\tfyl2x\n\tsub    $4, %esp\n\t"
      "fstps  (%esp)\n\tmovss  (%esp), %xmm0\n\t"
      "add    $4, %esp\n\tret\n\t.size  logf, .-logf\n",
      ".text\n\t.globl pow\n\t.type  pow, @function\n"
      "pow:\n\tpushl  16(%esp)\n\tpushl  16(%esp)\n\t"
      "pushl  16(%esp)\n\tpushl  16(%esp)\n\t"
      "call   libm_pow_impl\n\tadd    $16, %esp\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  pow, .-pow\n",
      ".text\n\t.globl powf\n\t.type  powf, @function\n"
      "powf:\n\tpushl  8(%esp)\n\tpushl  8(%esp)\n\t"
      "call   libm_powf_impl\n\tadd    $8, %esp\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  powf, .-powf\n",
      ".text\n\t.globl asin\n\t.type  asin, @function\n"
      "asin:\n\tpushl  8(%esp)\n\tpushl  8(%esp)\n\t"
      "call   libm_asin_impl\n\tadd    $8, %esp\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  asin, .-asin\n",
      ".text\n\t.globl asinf\n\t.type  asinf, @function\n"
      "asinf:\n\tpushl  4(%esp)\n\t"
      "call   libm_asinf_impl\n\tadd    $4, %esp\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  asinf, .-asinf\n",
      ".text\n\t.globl acos\n\t.type  acos, @function\n"
      "acos:\n\tpushl  8(%esp)\n\tpushl  8(%esp)\n\t"
      "call   libm_acos_impl\n\tadd    $8, %esp\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  acos, .-acos\n",
      ".text\n\t.globl acosf\n\t.type  acosf, @function\n"
      "acosf:\n\tpushl  4(%esp)\n\t"
      "call   libm_acosf_impl\n\tadd    $4, %esp\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  acosf, .-acosf\n",
      ".text\n\t.globl sinh\n\t.type  sinh, @function\n"
      "sinh:\n\tpushl  8(%esp)\n\tpushl  8(%esp)\n\t"
      "call   libm_sinh_impl\n\tadd    $8, %esp\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  sinh, .-sinh\n",
      ".text\n\t.globl sinhf\n\t.type  sinhf, @function\n"
      "sinhf:\n\tpushl  4(%esp)\n\t"
      "call   libm_sinhf_impl\n\tadd    $4, %esp\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  sinhf, .-sinhf\n",
      ".text\n\t.globl cosh\n\t.type  cosh, @function\n"
      "cosh:\n\tpushl  8(%esp)\n\tpushl  8(%esp)\n\t"
      "call   libm_cosh_impl\n\tadd    $8, %esp\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  cosh, .-cosh\n",
      ".text\n\t.globl coshf\n\t.type  coshf, @function\n"
      "coshf:\n\tpushl  4(%esp)\n\t"
      "call   libm_coshf_impl\n\tadd    $4, %esp\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  coshf, .-coshf\n",
      ".text\n\t.globl tanh\n\t.type  tanh, @function\n"
      "tanh:\n\tpushl  8(%esp)\n\tpushl  8(%esp)\n\t"
      "call   libm_tanh_impl\n\tadd    $8, %esp\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  tanh, .-tanh\n",
      ".text\n\t.globl tanhf\n\t.type  tanhf, @function\n"
      "tanhf:\n\tpushl  4(%esp)\n\t"
      "call   libm_tanhf_impl\n\tadd    $4, %esp\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  tanhf, .-tanhf\n",
      ".text\n\t.globl cbrt\n\t.type  cbrt, @function\n"
      "cbrt:\n\tpushl  8(%esp)\n\tpushl  8(%esp)\n\t"
      "call   libm_cbrt_impl\n\tadd    $8, %esp\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  cbrt, .-cbrt\n",
      ".text\n\t.globl cbrtf\n\t.type  cbrtf, @function\n"
      "cbrtf:\n\tpushl  4(%esp)\n\t"
      "call   libm_cbrtf_impl\n\tadd    $4, %esp\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  cbrtf, .-cbrtf\n",
      ".text\n\t.globl hypot\n\t.type  hypot, @function\n"
      "hypot:\n\tpushl  16(%esp)\n\tpushl  16(%esp)\n\t"
      "pushl  16(%esp)\n\tpushl  16(%esp)\n\t"
      "call   libm_hypot_impl\n\tadd    $16, %esp\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  hypot, .-hypot\n",
      ".text\n\t.globl hypotf\n\t.type  hypotf, @function\n"
      "hypotf:\n\tpushl  8(%esp)\n\tpushl  8(%esp)\n\t"
      "call   libm_hypotf_impl\n\tadd    $8, %esp\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  hypotf, .-hypotf\n",
      ".text\n\t.globl nextafter\n\t.type  nextafter, @function\n"
      "nextafter:\n\tpushl  16(%esp)\n\tpushl  16(%esp)\n\t"
      "pushl  16(%esp)\n\tpushl  16(%esp)\n\t"
      "call   libm_nextafter_impl\n\tadd    $16, %esp\n\t"
      "sub    $8, %esp\n\tfstpl  (%esp)\n\t"
      "movsd  (%esp), %xmm0\n\tadd    $8, %esp\n\t"
      "ret\n\t.size  nextafter, .-nextafter\n",
      ".text\n\t.globl nextafterf\n\t.type  nextafterf, @function\n"
      "nextafterf:\n\tpushl  8(%esp)\n\tpushl  8(%esp)\n\t"
      "call   libm_nextafterf_impl\n\tadd    $8, %esp\n\t"
      "sub    $4, %esp\n\tfstps  (%esp)\n\t"
      "movss  (%esp), %xmm0\n\tadd    $4, %esp\n\t"
      "ret\n\t.size  nextafterf, .-nextafterf\n",
      ".section .rodata\n\t.align 16\n"
      "fabs_mask_d:\n\t.quad 0x7FFFFFFFFFFFFFFF\n\t"
      ".quad 0x7FFFFFFFFFFFFFFF\n"
      "fabs_mask_s:\n\t.long 0x7FFFFFFF\n\t"
      ".long 0x7FFFFFFF\n\t.long 0x7FFFFFFF\n\t"
      ".long 0x7FFFFFFF\n\t.text\n",
      ".section .rodata\n\t.align 8\n"
      "libm_log2e_const:\n\t.quad 0x3FF71547652B82FE\n"
      "libm_ln2_const:\n\t.quad 0x3FE62E42FEFA39EF\n"
      ".text\n",
      ".global dg_setjmp\n"
      "dg_setjmp:\n"
      "    movl  4(%esp), %eax\n"
      "    movl  %ebx,  0(%eax)\n"
      "    movl  %esi,  4(%eax)\n"
      "    movl  %edi,  8(%eax)\n"
      "    movl  %ebp, 12(%eax)\n"
      "    movl  %esp, 16(%eax)\n"
      "    movl  (%esp), %ecx\n"
      "    movl  %ecx, 20(%eax)\n"
      "    xorl  %eax, %eax\n"
      "    ret\n"
      ".global dg_longjmp\n"
      "dg_longjmp:\n"
      "    movl  4(%esp), %eax\n"
      "    movl  8(%esp), %ecx\n"
      "    testl %ecx, %ecx\n"
      "    jnz   1f\n"
      "    movl  $1, %ecx\n"
      "1:\n"
      "    movl  0(%eax), %ebx\n"
      "    movl  4(%eax), %esi\n"
      "    movl  8(%eax), %edi\n"
      "    movl 12(%eax), %ebp\n"
      "    movl 16(%eax), %esp\n"
      "    movl 20(%eax), %edx\n"
      "    movl  %ecx,  %eax\n"
      "    jmp  *%edx\n"};
  ctool_u32 kind;
  for (kind = 1u; kind < CEMIT_FILE_ASSEMBLY_COUNT; kind++) {
    if (cemit_strings_equal(text, ctool_string(templates[kind])) ==
        CTOOL_TRUE) {
      return (cemit_file_assembly_kind_t)kind;
    }
  }
  return CEMIT_FILE_ASSEMBLY_NONE;
}

static const char *cemit_file_assembly_function_name(
    cemit_file_assembly_kind_t kind) {
  static const char *const names[] = {
      "", "sqrt", "sqrtf", "sin", "sinf", "cos", "cosf",
      "tan", "tanf", "atan", "atanf", "atan2", "atan2f",
      "fabs", "fabsf", "floor", "floorf", "ceil", "ceilf",
      "round", "roundf", "trunc", "truncf", "fmod", "fmodf",
      "exp2", "exp2f", "exp", "expf", "log2", "log2f",
      "log", "logf", "pow", "powf", "asin", "asinf",
      "acos", "acosf", "sinh", "sinhf", "cosh", "coshf",
      "tanh", "tanhf", "cbrt", "cbrtf", "hypot", "hypotf",
      "nextafter", "nextafterf", "", "", ""};
  return kind > CEMIT_FILE_ASSEMBLY_NONE &&
                 kind < CEMIT_FILE_ASSEMBLY_COUNT
             ? names[(ctool_u32)kind]
             : "";
}

static ctool_bool cemit_file_assembly_is_cdecl_bridge(
    cemit_file_assembly_kind_t kind) {
  return kind >= CEMIT_FILE_ASSEMBLY_POW &&
                 kind <= CEMIT_FILE_ASSEMBLY_NEXTAFTERF
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static const char *cemit_file_assembly_cdecl_callee_name(
    cemit_file_assembly_kind_t kind) {
  static const char *const names[] = {
      "libm_pow_impl", "libm_powf_impl",
      "libm_asin_impl", "libm_asinf_impl",
      "libm_acos_impl", "libm_acosf_impl",
      "libm_sinh_impl", "libm_sinhf_impl",
      "libm_cosh_impl", "libm_coshf_impl",
      "libm_tanh_impl", "libm_tanhf_impl",
      "libm_cbrt_impl", "libm_cbrtf_impl",
      "libm_hypot_impl", "libm_hypotf_impl",
      "libm_nextafter_impl", "libm_nextafterf_impl"};
  ctool_u32 index;
  if (cemit_file_assembly_is_cdecl_bridge(kind) == CTOOL_FALSE) {
    return "";
  }
  index = (ctool_u32)kind - (ctool_u32)CEMIT_FILE_ASSEMBLY_POW;
  return index < (ctool_u32)(sizeof(names) / sizeof(names[0]))
             ? names[index]
             : "";
}

static ctool_u32 cemit_file_assembly_parameter_count(
    cemit_file_assembly_kind_t kind) {
  if (kind == CEMIT_FILE_ASSEMBLY_ATAN2 ||
      kind == CEMIT_FILE_ASSEMBLY_ATAN2F ||
      kind == CEMIT_FILE_ASSEMBLY_FMOD ||
      kind == CEMIT_FILE_ASSEMBLY_FMODF ||
      kind == CEMIT_FILE_ASSEMBLY_POW ||
      kind == CEMIT_FILE_ASSEMBLY_POWF ||
      kind == CEMIT_FILE_ASSEMBLY_HYPOT ||
      kind == CEMIT_FILE_ASSEMBLY_HYPOTF ||
      kind == CEMIT_FILE_ASSEMBLY_NEXTAFTER ||
      kind == CEMIT_FILE_ASSEMBLY_NEXTAFTERF) {
    return 2u;
  }
  if (kind > CEMIT_FILE_ASSEMBLY_NONE &&
      kind < CEMIT_FILE_ASSEMBLY_FABS_MASKS) {
    return 1u;
  }
  return 0u;
}

static ctool_bool cemit_file_assembly_has_text_relocation(
    cemit_file_assembly_kind_t kind) {
  return kind == CEMIT_FILE_ASSEMBLY_FABS ||
                 kind == CEMIT_FILE_ASSEMBLY_FABSF ||
                 kind == CEMIT_FILE_ASSEMBLY_EXP ||
                 kind == CEMIT_FILE_ASSEMBLY_EXPF ||
                 kind == CEMIT_FILE_ASSEMBLY_LOG ||
                 kind == CEMIT_FILE_ASSEMBLY_LOGF ||
                 cemit_file_assembly_is_cdecl_bridge(kind) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_file_assembly_scalar_type_matches(
    const cemit_context_t *context, ctool_u32 type,
    ctool_bool single_precision) {
  const ctool_c_type_node_t *node = cemit_unwrapped_type(context, type);
  const ctool_c_type_layout_t *layout =
      type < context->unit->layout.type_count
          ? &context->unit->layout.types[type]
          : (const ctool_c_type_layout_t *)0;
  return node != (const ctool_c_type_node_t *)0 &&
                 layout != (const ctool_c_type_layout_t *)0 &&
                 node->qualifiers == 0u &&
                 node->kind ==
                     (single_precision == CTOOL_TRUE
                          ? CTOOL_C_TYPE_FLOAT
                          : CTOOL_C_TYPE_DOUBLE) &&
                 layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size ==
                     (single_precision == CTOOL_TRUE ? 4u : 8u)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_dglibc_integer_type_matches(
    const cemit_context_t *context, ctool_u32 type,
    ctool_bool is_unsigned) {
  const ctool_c_type_node_t *node = cemit_unwrapped_type(context, type);
  const ctool_c_type_layout_t *layout =
      type < context->unit->layout.type_count
          ? &context->unit->layout.types[type]
          : (const ctool_c_type_layout_t *)0;
  return node != (const ctool_c_type_node_t *)0 &&
                 layout != (const ctool_c_type_layout_t *)0 &&
                 node->qualifiers == 0u &&
                 node->kind ==
                     (is_unsigned == CTOOL_TRUE
                          ? CTOOL_C_TYPE_UNSIGNED_INT
                          : CTOOL_C_TYPE_SIGNED_INT) &&
                 layout->is_integer == CTOOL_TRUE &&
                 layout->size == 4u &&
                 layout->is_signed ==
                     (is_unsigned == CTOOL_TRUE ? CTOOL_FALSE : CTOOL_TRUE)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_dglibc_environment_pointer_type_matches(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *pointer = cemit_unwrapped_type(context, type);
  const ctool_c_type_layout_t *layout =
      type < context->unit->layout.type_count
          ? &context->unit->layout.types[type]
          : (const ctool_c_type_layout_t *)0;
  return pointer != (const ctool_c_type_node_t *)0 &&
                 layout != (const ctool_c_type_layout_t *)0 &&
                 pointer->kind == CTOOL_C_TYPE_POINTER &&
                 pointer->qualifiers == 0u &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size == 4u &&
                 cemit_dglibc_integer_type_matches(
                     context, pointer->referenced_type, CTOOL_TRUE) ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_dglibc_jump_function_type_matches(
    const cemit_context_t *context, ctool_u32 type,
    ctool_bool is_longjmp) {
  const ctool_c_type_node_t *function = cemit_unwrapped_type(context, type);
  ctool_u32 expected_parameters =
      is_longjmp == CTOOL_TRUE ? 2u : 1u;
  if (function == (const ctool_c_type_node_t *)0 ||
      function->kind != CTOOL_C_TYPE_FUNCTION ||
      function->qualifiers != 0u ||
      function->has_prototype != CTOOL_TRUE ||
      function->variadic != CTOOL_FALSE ||
      function->parameter_count != expected_parameters ||
      function->first_parameter >
          context->unit->graph.parameter_type_count ||
      function->parameter_count >
          context->unit->graph.parameter_type_count -
              function->first_parameter ||
      cemit_dglibc_environment_pointer_type_matches(
          context,
          context->unit->graph
              .parameter_types[function->first_parameter]) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  if (is_longjmp == CTOOL_TRUE) {
    const ctool_c_type_node_t *result =
        cemit_unwrapped_type(context, function->referenced_type);
    return result != (const ctool_c_type_node_t *)0 &&
                   result->kind == CTOOL_C_TYPE_VOID &&
                   result->qualifiers == 0u &&
                   cemit_dglibc_integer_type_matches(
                       context,
                       context->unit->graph.parameter_types[
                           function->first_parameter + 1u],
                       CTOOL_FALSE) == CTOOL_TRUE
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return cemit_dglibc_integer_type_matches(
      context, function->referenced_type, CTOOL_FALSE);
}

static ctool_bool cemit_file_assembly_function_type_matches(
    const cemit_context_t *context, ctool_u32 type,
    cemit_file_assembly_kind_t kind) {
  const ctool_c_type_node_t *function = cemit_unwrapped_type(context, type);
  ctool_bool single_precision =
      (((ctool_u32)kind - 1u) & 1u) != 0u ? CTOOL_TRUE : CTOOL_FALSE;
  ctool_u32 expected_parameters =
      cemit_file_assembly_parameter_count(kind);
  ctool_u32 parameter;
  if (function == (const ctool_c_type_node_t *)0 ||
      expected_parameters == 0u ||
      function->kind != CTOOL_C_TYPE_FUNCTION ||
      function->has_prototype != CTOOL_TRUE ||
      function->variadic != CTOOL_FALSE ||
      function->parameter_count != expected_parameters ||
      function->first_parameter >
          context->unit->graph.parameter_type_count ||
      function->parameter_count >
          context->unit->graph.parameter_type_count -
              function->first_parameter ||
      cemit_file_assembly_scalar_type_matches(
          context, function->referenced_type, single_precision) ==
          CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  for (parameter = 0u; parameter < expected_parameters; parameter++) {
    if (cemit_file_assembly_scalar_type_matches(
            context,
            context->unit->graph.parameter_types[
                function->first_parameter + parameter],
            single_precision) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t cemit_index_file_assemblies(
    cemit_context_t *context) {
  ctool_u32 index;
  if (context->ir.file_assembly_count !=
          context->unit->file_assembly_count ||
      (context->ir.file_assembly_count != 0u &&
       (context->ir.file_assemblies == (const ctool_u32 *)0 ||
        context->file_assembly_bindings == (ctool_u32 *)0 ||
        context->file_assembly_callee_bindings == (ctool_u32 *)0 ||
        context->file_assembly_kinds == (ctool_u32 *)0))) {
    return cemit_invalid_unit(
        context, (const ctool_c_pp_location_t *)0);
  }
  context->fabs_mask_assembly = CTOOL_C_AST_NONE;
  context->fabs_mask_d_symbol = CTOOL_C_AST_NONE;
  context->fabs_mask_s_symbol = CTOOL_C_AST_NONE;
  context->exp_log_constant_assembly = CTOOL_C_AST_NONE;
  context->log2e_constant_symbol = CTOOL_C_AST_NONE;
  context->ln2_constant_symbol = CTOOL_C_AST_NONE;
  for (index = 0u; index < context->ir.file_assembly_count; index++) {
    ctool_u32 assembly_index = context->ir.file_assemblies[index];
    const ctool_c_assembly_t *assembly;
    cemit_file_assembly_kind_t kind;
    if (assembly_index != index ||
        assembly_index >= context->unit->file_assembly_count) {
      return cemit_invalid_unit(
          context, (const ctool_c_pp_location_t *)0);
    }
    assembly = &context->unit->file_assemblies[assembly_index];
    kind = cemit_file_assembly_template_kind(assembly->template_text);
    if (kind == CEMIT_FILE_ASSEMBLY_NONE) {
      return cemit_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_EMIT_DIAG_UNSUPPORTED, &assembly->location,
          "GNU file-scope assembly template is outside this i386 "
          "emission slice");
    }
    context->file_assembly_bindings[index] = CTOOL_C_AST_NONE;
    context->file_assembly_callee_bindings[index] = CTOOL_C_AST_NONE;
    context->file_assembly_kinds[index] = (ctool_u32)kind;
    if (kind == CEMIT_FILE_ASSEMBLY_FABS_MASKS) {
      ctool_u32 binding;
      if (context->fabs_mask_assembly != CTOOL_C_AST_NONE) {
        return cemit_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
            &assembly->location,
            "GNU fabs mask block is defined twice");
      }
      for (binding = 0u; binding < context->unit->binding_count;
           binding++) {
        ctool_string_t name =
            context->unit->bindings[binding].name;
        if (cemit_strings_equal(
                name, ctool_string("fabs_mask_d")) == CTOOL_TRUE ||
            cemit_strings_equal(
                name, ctool_string("fabs_mask_s")) == CTOOL_TRUE) {
          return cemit_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
              &assembly->location,
              "GNU fabs mask label conflicts with a C declaration");
        }
      }
      context->fabs_mask_assembly = index;
    } else if (kind == CEMIT_FILE_ASSEMBLY_EXP_LOG_CONSTANTS) {
      ctool_u32 binding;
      if (context->exp_log_constant_assembly != CTOOL_C_AST_NONE) {
        return cemit_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
            &assembly->location,
            "GNU exp/log constant block is defined twice");
      }
      for (binding = 0u; binding < context->unit->binding_count;
           binding++) {
        ctool_string_t name =
            context->unit->bindings[binding].name;
        if (cemit_strings_equal(
                name, ctool_string("libm_log2e_const")) == CTOOL_TRUE ||
            cemit_strings_equal(
                name, ctool_string("libm_ln2_const")) == CTOOL_TRUE) {
          return cemit_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
              &assembly->location,
              "GNU exp/log constant label conflicts with a C declaration");
        }
      }
      context->exp_log_constant_assembly = index;
    }
  }
  for (index = 0u; index < context->ir.file_assembly_count; index++) {
    const ctool_c_assembly_t *assembly =
        &context->unit->file_assemblies[
            context->ir.file_assemblies[index]];
    cemit_file_assembly_kind_t kind =
        (cemit_file_assembly_kind_t)
            context->file_assembly_kinds[index];
    const char *name;
    ctool_u32 binding;
    ctool_bool found = CTOOL_FALSE;
    ctool_u32 prior;
    if (kind == CEMIT_FILE_ASSEMBLY_DGLIBC_JUMPS) {
      ctool_u32 setjmp_binding = CTOOL_C_AST_NONE;
      ctool_u32 longjmp_binding = CTOOL_C_AST_NONE;
      for (binding = 0u; binding < context->unit->binding_count; binding++) {
        const ctool_c_binding_t *candidate =
            &context->unit->bindings[binding];
        if (cemit_strings_equal(
                candidate->name, ctool_string("dg_setjmp")) == CTOOL_TRUE) {
          setjmp_binding = binding;
        } else if (cemit_strings_equal(
                       candidate->name,
                       ctool_string("dg_longjmp")) == CTOOL_TRUE) {
          longjmp_binding = binding;
        }
      }
      if (setjmp_binding == CTOOL_C_AST_NONE ||
          longjmp_binding == CTOOL_C_AST_NONE) {
        return cemit_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
            &assembly->location,
            "GNU dglibc jump assembly requires matching external "
            "declarations");
      }
      {
        const ctool_c_binding_t *setjmp_candidate =
            &context->unit->bindings[setjmp_binding];
        const ctool_c_binding_t *longjmp_candidate =
            &context->unit->bindings[longjmp_binding];
        if (setjmp_candidate->kind != CTOOL_C_BINDING_FUNCTION ||
            longjmp_candidate->kind != CTOOL_C_BINDING_FUNCTION ||
            setjmp_candidate->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
            longjmp_candidate->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
            setjmp_candidate->file_scope_visible != CTOOL_TRUE ||
            longjmp_candidate->file_scope_visible != CTOOL_TRUE ||
            setjmp_candidate->attributes != 0u ||
            longjmp_candidate->attributes != 0u ||
            context->binding_function_definitions[setjmp_binding] !=
                CTOOL_C_AST_NONE ||
            context->binding_function_definitions[longjmp_binding] !=
                CTOOL_C_AST_NONE ||
            cemit_dglibc_jump_function_type_matches(
                context, setjmp_candidate->type, CTOOL_FALSE) ==
                CTOOL_FALSE ||
            cemit_dglibc_jump_function_type_matches(
                context, longjmp_candidate->type, CTOOL_TRUE) ==
                CTOOL_FALSE) {
          return cemit_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
              &assembly->location,
              "GNU dglibc jump assembly does not match its external "
              "function declarations");
        }
      }
      for (prior = 0u; prior < index; prior++) {
        if (context->file_assembly_bindings[prior] == setjmp_binding ||
            context->file_assembly_callee_bindings[prior] ==
                longjmp_binding) {
          return cemit_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
              &assembly->location,
              "GNU dglibc jump assembly defines its functions twice");
        }
      }
      context->file_assembly_bindings[index] = setjmp_binding;
      context->file_assembly_callee_bindings[index] = longjmp_binding;
      context->binding_needed[setjmp_binding] = CTOOL_TRUE;
      context->binding_needed[longjmp_binding] = CTOOL_TRUE;
      continue;
    }
    if (kind == CEMIT_FILE_ASSEMBLY_FABS_MASKS ||
        kind == CEMIT_FILE_ASSEMBLY_EXP_LOG_CONSTANTS) {
      continue;
    }
    if ((kind == CEMIT_FILE_ASSEMBLY_FABS ||
         kind == CEMIT_FILE_ASSEMBLY_FABSF) &&
        (context->fabs_mask_assembly == CTOOL_C_AST_NONE ||
         context->fabs_mask_assembly >= index)) {
      return cemit_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
          &assembly->location,
          "GNU fabs wrapper requires the exact file-scope mask block");
    }
    if ((kind == CEMIT_FILE_ASSEMBLY_EXP ||
         kind == CEMIT_FILE_ASSEMBLY_EXPF ||
         kind == CEMIT_FILE_ASSEMBLY_LOG ||
         kind == CEMIT_FILE_ASSEMBLY_LOGF) &&
        (context->exp_log_constant_assembly == CTOOL_C_AST_NONE ||
         context->exp_log_constant_assembly >= index)) {
      return cemit_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
          &assembly->location,
          "GNU exp/log wrapper requires the exact file-scope "
          "constant block");
    }
    name = cemit_file_assembly_function_name(kind);
    for (binding = 0u; binding < context->unit->binding_count; binding++) {
      const ctool_c_binding_t *candidate =
          &context->unit->bindings[binding];
      if (cemit_strings_equal(
              candidate->name, ctool_string(name)) == CTOOL_TRUE) {
        found = CTOOL_TRUE;
        break;
      }
    }
    if (found == CTOOL_FALSE) {
      return cemit_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
          &assembly->location,
          "GNU file-scope assembly requires a matching external "
          "function declaration");
    }
    {
      const ctool_c_binding_t *candidate =
          &context->unit->bindings[binding];
      if (candidate->kind != CTOOL_C_BINDING_FUNCTION ||
          candidate->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
          candidate->file_scope_visible != CTOOL_TRUE ||
          candidate->attributes != 0u ||
          context->binding_function_definitions[binding] !=
              CTOOL_C_AST_NONE ||
          cemit_file_assembly_function_type_matches(
              context, candidate->type, kind) == CTOOL_FALSE) {
        return cemit_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
            &assembly->location,
            "GNU file-scope assembly does not match its external "
            "function declaration");
      }
    }
    for (prior = 0u; prior < index; prior++) {
      if (context->file_assembly_bindings[prior] == binding) {
        return cemit_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
            &assembly->location,
            "GNU file-scope assembly defines one function twice");
      }
    }
    context->file_assembly_bindings[index] = binding;
    context->binding_needed[binding] = CTOOL_TRUE;
    if (cemit_file_assembly_is_cdecl_bridge(kind) == CTOOL_TRUE) {
      const char *callee_name =
          cemit_file_assembly_cdecl_callee_name(kind);
      ctool_u32 callee_binding;
      ctool_bool callee_found = CTOOL_FALSE;
      for (callee_binding = 0u;
           callee_binding < context->unit->binding_count;
           callee_binding++) {
        if (cemit_strings_equal(
                context->unit->bindings[callee_binding].name,
                ctool_string(callee_name)) == CTOOL_TRUE) {
          callee_found = CTOOL_TRUE;
          break;
        }
      }
      if (callee_found == CTOOL_FALSE) {
        return cemit_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
            &assembly->location,
            "GNU cdecl bridge requires a matching external "
            "callee declaration");
      }
      {
        const ctool_c_binding_t *callee =
            &context->unit->bindings[callee_binding];
        if (callee->kind != CTOOL_C_BINDING_FUNCTION ||
            callee->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
            callee->file_scope_visible != CTOOL_TRUE ||
            cemit_file_assembly_function_type_matches(
                context, callee->type, kind) == CTOOL_FALSE) {
          return cemit_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
              &assembly->location,
              "GNU cdecl bridge does not match its external "
              "callee declaration");
        }
      }
      context->file_assembly_callee_bindings[index] =
          callee_binding;
      context->binding_needed[callee_binding] = CTOOL_TRUE;
    }
  }
  return CTOOL_OK;
}

static ctool_bool cemit_bytes_are_zero(ctool_bytes_t bytes) {
  ctool_u32 index;
  if (bytes.data == (const ctool_u8 *)0 && bytes.size != 0u) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < bytes.size; index++) {
    if (bytes.data[index] != 0u) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t cemit_index_initializers(cemit_context_t *context) {
  ctool_u32 element_cursor = 0u;
  ctool_u32 index;
  for (index = 0u; index < context->unit->initializer_count; index++) {
    const ctool_c_initializer_t *initializer =
        &context->unit->initializers[index];
    ctool_bool is_zero = CTOOL_FALSE;
    ctool_u32 edge_index;
    if (initializer->type >= context->unit->graph.type_count) {
      return cemit_invalid_unit(context, &initializer->location);
    }
    if (initializer->kind == CTOOL_C_INITIALIZER_EXPRESSION) {
      if (initializer->expression >= context->unit->expression_count ||
          context->unit->expressions[initializer->expression].type >=
              context->unit->graph.type_count ||
          initializer->integer_bits != 0u ||
          initializer->string_bytes.data != (const ctool_u8 *)0 ||
          initializer->string_bytes.size != 0u ||
          initializer->address_kind != CTOOL_C_INITIALIZER_ADDRESS_NONE ||
          initializer->address_reference != CTOOL_C_AST_NONE ||
          initializer->address_addend != 0 ||
          initializer->first_element != CTOOL_C_AST_NONE ||
          initializer->element_count != 0u) {
        return cemit_invalid_unit(context, &initializer->location);
      }
    } else if (initializer->kind == CTOOL_C_INITIALIZER_ZERO) {
      is_zero = CTOOL_TRUE;
    } else if (initializer->kind == CTOOL_C_INITIALIZER_INTEGER) {
      is_zero = initializer->integer_bits == 0u ? CTOOL_TRUE : CTOOL_FALSE;
    } else if (initializer->kind == CTOOL_C_INITIALIZER_FLOATING) {
      const ctool_c_type_node_t *floating;
      const ctool_c_type_layout_t *layout;
      ctool_u32 qualifiers;
      if (initializer->type >= context->unit->layout.type_count ||
          cemit_underlying_type(
              context, initializer->type, &qualifiers,
              &floating) == CTOOL_FALSE) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      layout = &context->unit->layout.types[initializer->type];
      if (((qualifiers | floating->qualifiers) & CTOOL_C_QUAL_ATOMIC) !=
              0u ||
          !((floating->kind == CTOOL_C_TYPE_FLOAT &&
             layout->size == 4u &&
             (initializer->integer_bits &
              0xffffffff00000000ull) == 0ull) ||
            (floating->kind == CTOOL_C_TYPE_DOUBLE &&
             layout->size == 8u)) ||
          initializer->expression != CTOOL_C_AST_NONE ||
          initializer->string_bytes.data != (const ctool_u8 *)0 ||
          initializer->string_bytes.size != 0u ||
          initializer->address_kind !=
              CTOOL_C_INITIALIZER_ADDRESS_NONE ||
          initializer->address_reference != CTOOL_C_AST_NONE ||
          initializer->address_addend != 0 ||
          initializer->first_element != CTOOL_C_AST_NONE ||
          initializer->element_count != 0u) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      is_zero =
          initializer->integer_bits == 0u ? CTOOL_TRUE : CTOOL_FALSE;
    } else if (initializer->kind == CTOOL_C_INITIALIZER_STRING) {
      if (initializer->string_bytes.data == (const ctool_u8 *)0 &&
          initializer->string_bytes.size != 0u) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      is_zero = cemit_bytes_are_zero(initializer->string_bytes);
    } else if (initializer->kind == CTOOL_C_INITIALIZER_ADDRESS) {
      if ((initializer->address_kind ==
               CTOOL_C_INITIALIZER_ADDRESS_STRING &&
           (initializer->address_reference != CTOOL_C_AST_NONE ||
            initializer->string_bytes.data == (const ctool_u8 *)0 ||
            initializer->string_bytes.size == 0u)) ||
          (initializer->address_kind ==
               CTOOL_C_INITIALIZER_ADDRESS_BINDING &&
           initializer->address_reference >= context->unit->binding_count) ||
          (initializer->address_kind !=
               CTOOL_C_INITIALIZER_ADDRESS_STRING &&
           initializer->address_kind !=
               CTOOL_C_INITIALIZER_ADDRESS_BINDING)) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      if (initializer->address_kind ==
          CTOOL_C_INITIALIZER_ADDRESS_BINDING) {
        context->binding_needed[initializer->address_reference] = CTOOL_TRUE;
      }
    } else if (initializer->kind == CTOOL_C_INITIALIZER_LIST) {
      const ctool_c_type_node_t *parent_type =
          cemit_unwrapped_type(context, initializer->type);
      if (parent_type == (const ctool_c_type_node_t *)0 ||
          initializer->element_count == 0u ||
          initializer->first_element != element_cursor ||
          initializer->first_element >
              context->unit->initializer_element_count ||
          initializer->element_count >
              context->unit->initializer_element_count -
                  initializer->first_element) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      if (parent_type->kind == CTOOL_C_TYPE_ARRAY) {
        if (parent_type->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
            parent_type->referenced_type >=
                context->unit->graph.type_count) {
          return cemit_invalid_unit(context, &initializer->location);
        }
      } else if (parent_type->kind == CTOOL_C_TYPE_RECORD) {
        if (parent_type->record_kind != CTOOL_C_RECORD_STRUCT &&
            parent_type->record_kind != CTOOL_C_RECORD_UNION) {
          return cemit_emit_failure(
              context, CTOOL_ERR_UNSUPPORTED,
              CTOOL_C_EMIT_DIAG_INITIALIZER, &initializer->location,
              "CupidC object emission does not yet support this aggregate initializer");
        }
        if (parent_type->record_complete == CTOOL_FALSE ||
            parent_type->first_member >
                context->unit->graph.member_count ||
            parent_type->member_count >
                context->unit->graph.member_count -
                    parent_type->first_member) {
          return cemit_invalid_unit(context, &initializer->location);
        }
        if (parent_type->record_kind == CTOOL_C_RECORD_UNION &&
            initializer->element_count != 1u) {
          return cemit_invalid_unit(context, &initializer->location);
        }
      } else {
        return cemit_invalid_unit(context, &initializer->location);
      }
      is_zero = CTOOL_TRUE;
      for (edge_index = 0u; edge_index < initializer->element_count;
           edge_index++) {
        const ctool_c_initializer_element_t *edge =
            &context->unit->initializer_elements
                 [initializer->first_element + edge_index];
        const ctool_c_initializer_t *child;
        ctool_u32 child_type = CTOOL_C_TYPE_NONE;
        ctool_u32 previous;
        if (edge->initializer >= index) {
          return cemit_invalid_unit(context, &initializer->location);
        }
        child = &context->unit->initializers[edge->initializer];
        for (previous = 0u; previous < edge_index; previous++) {
          const ctool_c_initializer_element_t *previous_edge =
              &context->unit->initializer_elements
                   [initializer->first_element + previous];
          if (previous_edge->subobject == edge->subobject) {
            return cemit_invalid_unit(context, &initializer->location);
          }
        }
        if (parent_type->kind == CTOOL_C_TYPE_ARRAY) {
          if (edge->subobject >= parent_type->element_count) {
            return cemit_invalid_unit(context, &initializer->location);
          }
          child_type = parent_type->referenced_type;
        } else {
          const ctool_c_record_member_t *member;
          const ctool_c_type_node_t *member_type;
          if (edge->subobject < parent_type->first_member ||
              edge->subobject - parent_type->first_member >=
                  parent_type->member_count ||
              edge->subobject >= context->unit->layout.member_count) {
            return cemit_invalid_unit(context, &initializer->location);
          }
          member = &context->unit->graph.members[edge->subobject];
          if (member->type >= context->unit->graph.type_count ||
              (member->is_bit_field == CTOOL_TRUE &&
               member->name.size == 0u)) {
            return cemit_invalid_unit(context, &initializer->location);
          }
          member_type = cemit_unwrapped_type(context, member->type);
          if (member_type == (const ctool_c_type_node_t *)0 ||
              (edge->subobject + 1u ==
                   parent_type->first_member + parent_type->member_count &&
               member_type->kind == CTOOL_C_TYPE_ARRAY &&
               member_type->array_bound_kind ==
                   CTOOL_C_ARRAY_UNSPECIFIED)) {
            return cemit_invalid_unit(context, &initializer->location);
          }
          child_type = member->type;
        }
        if (child->type != child_type) {
          return cemit_invalid_unit(context, &initializer->location);
        }
        ctool_bool child_is_zero =
            context->initializer_is_zero[edge->initializer];
        if (parent_type->kind == CTOOL_C_TYPE_RECORD &&
            context->unit->graph.members[edge->subobject].is_bit_field ==
                CTOOL_TRUE) {
          ctool_u32 width =
              context->unit->layout.members[edge->subobject].bit_width;
          if (width == 0u || width > 64u ||
              (child->kind != CTOOL_C_INITIALIZER_ZERO &&
               child->kind != CTOOL_C_INITIALIZER_INTEGER)) {
            return cemit_invalid_unit(context, &initializer->location);
          }
          if (child->kind == CTOOL_C_INITIALIZER_INTEGER) {
            ctool_u64 mask = width == 64u
                                 ? ~(ctool_u64)0u
                                 : (((ctool_u64)1u << width) - 1u);
            child_is_zero = (child->integer_bits & mask) == 0u
                                ? CTOOL_TRUE
                                : CTOOL_FALSE;
          }
        }
        if (child_is_zero == CTOOL_FALSE) {
          is_zero = CTOOL_FALSE;
        }
      }
      element_cursor += initializer->element_count;
    } else {
      return cemit_invalid_unit(context, &initializer->location);
    }
    context->initializer_is_zero[index] = is_zero;
  }
  if (element_cursor != context->unit->initializer_element_count) {
    return cemit_invalid_unit(context,
                              (const ctool_c_pp_location_t *)0);
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_make_literal_name(cemit_context_t *context,
                                               ctool_string_t *name_out) {
  char reversed[10];
  char *name;
  ctool_u32 value = context->literal_count;
  ctool_u32 digits = 0u;
  ctool_u32 index;
  ctool_status_t status;
  do {
    reversed[digits++] = (char)('0' + (char)(value % 10u));
    value /= 10u;
  } while (value != 0u);
  status = ctool_arena_alloc(context->arena, 3u + digits + 1u, 1u,
                             (void **)&name);
  if (status != CTOOL_OK) {
    return status;
  }
  name[0] = '.';
  name[1] = 'L';
  name[2] = 'C';
  for (index = 0u; index < digits; index++) {
    name[3u + index] = reversed[digits - 1u - index];
  }
  name[3u + digits] = '\0';
  name_out->data = name;
  name_out->size = 3u + digits;
  context->literal_count++;
  return CTOOL_OK;
}

static ctool_status_t cemit_make_block_static_name(
    cemit_context_t *context, ctool_u32 block_binding_index,
    ctool_string_t *name_out) {
  const ctool_c_block_binding_t *binding;
  char reversed[10];
  char *name;
  ctool_u32 value = block_binding_index;
  ctool_u32 digits = 0u;
  ctool_u32 prefix_size;
  ctool_u32 name_size;
  ctool_u32 index;
  ctool_status_t status;
  if (block_binding_index >= context->unit->block_binding_count ||
      name_out == (ctool_string_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  binding = &context->unit->block_bindings[block_binding_index];
  if (binding->name.data == (const char *)0 || binding->name.size == 0u) {
    return cemit_invalid_unit(context, &binding->location);
  }
  do {
    reversed[digits++] = (char)('0' + (char)(value % 10u));
    value /= 10u;
  } while (value != 0u);
  prefix_size = 5u + digits;
  if (cemit_add_overflows(prefix_size, binding->name.size) == CTOOL_TRUE ||
      prefix_size + binding->name.size == 0xffffffffu) {
    return CTOOL_ERR_OVERFLOW;
  }
  name_size = prefix_size + binding->name.size;
  status = ctool_arena_alloc(context->arena, name_size + 1u, 1u,
                             (void **)&name);
  if (status != CTOOL_OK) {
    return status;
  }
  name[0] = '.';
  name[1] = 'L';
  name[2] = 'B';
  name[3] = 'S';
  for (index = 0u; index < digits; index++) {
    name[4u + index] = reversed[digits - 1u - index];
  }
  name[4u + digits] = '.';
  for (index = 0u; index < binding->name.size; index++) {
    name[prefix_size + index] = binding->name.data[index];
  }
  name[name_size] = '\0';
  name_out->data = name;
  name_out->size = name_size;
  return CTOOL_OK;
}

static ctool_status_t cemit_ensure_binding_symbol(
    cemit_context_t *context, ctool_u32 binding_index,
    ctool_u32 *symbol_out) {
  const ctool_c_binding_t *binding;
  ctool_elf32_symbol_spec_t *symbol;
  ctool_u32 symbol_index;
  if (binding_index >= context->unit->binding_count) {
    return cemit_invalid_unit(context,
                              (const ctool_c_pp_location_t *)0);
  }
  if (context->binding_symbols[binding_index] != CTOOL_C_AST_NONE) {
    *symbol_out = context->binding_symbols[binding_index];
    return CTOOL_OK;
  }
  binding = &context->unit->bindings[binding_index];
  if ((binding->kind != CTOOL_C_BINDING_OBJECT &&
       binding->kind != CTOOL_C_BINDING_FUNCTION) ||
      (binding->linkage != CTOOL_C_LINKAGE_INTERNAL &&
       binding->linkage != CTOOL_C_LINKAGE_EXTERNAL) ||
      (binding->attributes & ~CTOOL_C_DECL_ATTR_ALL) != 0u ||
      ((binding->attributes & CTOOL_C_DECL_ATTR_NORETURN) != 0u &&
       binding->kind != CTOOL_C_BINDING_FUNCTION) ||
      ((binding->attributes &
        CTOOL_C_DECL_ATTR_FUNCTION_CODEGEN) != 0u &&
       binding->kind != CTOOL_C_BINDING_FUNCTION) ||
      ((binding->attributes & CTOOL_C_DECL_ATTR_WEAK) != 0u &&
       binding->linkage != CTOOL_C_LINKAGE_EXTERNAL) ||
      binding->name.data == (const char *)0 || binding->name.size == 0u) {
    return cemit_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
        &binding->location,
        "CupidC cannot create an ELF symbol for this binding");
  }
  if (context->symbol_count >= context->symbol_capacity) {
    return CTOOL_ERR_INTERNAL;
  }
  symbol_index = context->symbol_count++;
  symbol = &context->symbols[symbol_index];
  symbol->name = binding->name;
  symbol->binding =
      binding->linkage == CTOOL_C_LINKAGE_INTERNAL
          ? CTOOL_ELF32_BIND_LOCAL
          : (binding->attributes & CTOOL_C_DECL_ATTR_WEAK) != 0u
                ? CTOOL_ELF32_BIND_WEAK
                : CTOOL_ELF32_BIND_GLOBAL;
  symbol->type = binding->kind == CTOOL_C_BINDING_FUNCTION
                     ? CTOOL_ELF32_SYMBOL_FUNCTION
                     : CTOOL_ELF32_SYMBOL_OBJECT;
  symbol->visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbol->placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbol->section = CTOOL_ELF32_NO_SECTION;
  context->binding_symbols[binding_index] = symbol_index;
  *symbol_out = symbol_index;
  return CTOOL_OK;
}

static ctool_status_t cemit_add_file_assembly_symbol(
    cemit_context_t *context, const char *name,
    ctool_u32 *symbol_out) {
  ctool_elf32_symbol_spec_t *symbol;
  ctool_u32 symbol_index;
  if (name == (const char *)0 || name[0] == '\0' ||
      symbol_out == (ctool_u32 *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (context->symbol_count >= context->symbol_capacity) {
    return CTOOL_ERR_INTERNAL;
  }
  symbol_index = context->symbol_count++;
  symbol = &context->symbols[symbol_index];
  symbol->name = ctool_string(name);
  symbol->binding = CTOOL_ELF32_BIND_LOCAL;
  symbol->type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbol->visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbol->placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbol->section = CTOOL_ELF32_NO_SECTION;
  symbol->value = 0u;
  symbol->size = 0u;
  symbol->alignment = 0u;
  *symbol_out = symbol_index;
  return CTOOL_OK;
}

static ctool_status_t cemit_index_file_assembly_symbols(
    cemit_context_t *context) {
  ctool_status_t status = CTOOL_OK;
  if (context->fabs_mask_assembly == CTOOL_C_AST_NONE) {
    if (context->fabs_mask_d_symbol != CTOOL_C_AST_NONE ||
        context->fabs_mask_s_symbol != CTOOL_C_AST_NONE) {
      return CTOOL_ERR_INTERNAL;
    }
  } else {
    if (context->fabs_mask_assembly >=
            context->ir.file_assembly_count ||
        context->fabs_mask_d_symbol != CTOOL_C_AST_NONE ||
        context->fabs_mask_s_symbol != CTOOL_C_AST_NONE) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_add_file_assembly_symbol(
        context, "fabs_mask_d", &context->fabs_mask_d_symbol);
    if (status == CTOOL_OK) {
      status = cemit_add_file_assembly_symbol(
          context, "fabs_mask_s", &context->fabs_mask_s_symbol);
    }
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (context->exp_log_constant_assembly == CTOOL_C_AST_NONE) {
    return context->log2e_constant_symbol == CTOOL_C_AST_NONE &&
                   context->ln2_constant_symbol == CTOOL_C_AST_NONE
               ? CTOOL_OK
               : CTOOL_ERR_INTERNAL;
  }
  if (context->exp_log_constant_assembly >=
          context->ir.file_assembly_count ||
      context->log2e_constant_symbol != CTOOL_C_AST_NONE ||
      context->ln2_constant_symbol != CTOOL_C_AST_NONE) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_add_file_assembly_symbol(
      context, "libm_log2e_const", &context->log2e_constant_symbol);
  if (status == CTOOL_OK) {
    status = cemit_add_file_assembly_symbol(
        context, "libm_ln2_const", &context->ln2_constant_symbol);
  }
  return status;
}

static ctool_status_t cemit_ensure_block_binding_symbol(
    cemit_context_t *context, ctool_u32 block_binding_index,
    ctool_u32 *symbol_out) {
  const ctool_c_block_binding_t *binding;
  ctool_elf32_symbol_spec_t *symbol;
  ctool_u32 symbol_index;
  ctool_status_t status;
  if (block_binding_index >= context->unit->block_binding_count ||
      symbol_out == (ctool_u32 *)0 ||
      context->block_binding_symbols == (ctool_u32 *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (context->block_binding_symbols[block_binding_index] !=
      CTOOL_C_AST_NONE) {
    *symbol_out = context->block_binding_symbols[block_binding_index];
    return CTOOL_OK;
  }
  binding = &context->unit->block_bindings[block_binding_index];
  if (binding->kind != CTOOL_C_BINDING_OBJECT ||
      binding->storage != CTOOL_C_STORAGE_STATIC ||
      binding->initializer >= context->unit->initializer_count ||
      binding->type >= context->unit->layout.type_count) {
    return cemit_invalid_unit(context, &binding->location);
  }
  if (context->symbol_count >= context->symbol_capacity) {
    return CTOOL_ERR_INTERNAL;
  }
  symbol_index = context->symbol_count;
  symbol = &context->symbols[symbol_index];
  status = cemit_make_block_static_name(context, block_binding_index,
                                        &symbol->name);
  if (status != CTOOL_OK) {
    return status;
  }
  symbol->binding = CTOOL_ELF32_BIND_LOCAL;
  symbol->type = CTOOL_ELF32_SYMBOL_OBJECT;
  symbol->visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbol->placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbol->section = CTOOL_ELF32_NO_SECTION;
  context->block_binding_symbols[block_binding_index] = symbol_index;
  context->symbol_count++;
  *symbol_out = symbol_index;
  return CTOOL_OK;
}

static ctool_status_t cemit_index_symbols(cemit_context_t *context) {
  ctool_u32 binding;
  for (binding = 0u; binding < context->unit->binding_count; binding++) {
    if (context->binding_needed[binding] == CTOOL_TRUE) {
      ctool_u32 symbol;
      ctool_status_t status =
          cemit_ensure_binding_symbol(context, binding, &symbol);
      if (status != CTOOL_OK) {
        return status;
      }
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_index_block_static_symbols(
    cemit_context_t *context) {
  ctool_u32 index;
  if (context->unit->block_binding_count != 0u &&
      context->block_binding_symbols == (ctool_u32 *)0) {
    return CTOOL_ERR_INTERNAL;
  }
  for (index = 0u; index < context->unit->block_binding_count; index++) {
    context->block_binding_symbols[index] = CTOOL_C_AST_NONE;
  }
  for (index = 0u; index < context->unit->block_binding_count; index++) {
    if (context->unit->block_bindings[index].storage ==
        CTOOL_C_STORAGE_STATIC) {
      ctool_u32 symbol;
      ctool_status_t status =
          cemit_ensure_block_binding_symbol(context, index, &symbol);
      if (status != CTOOL_OK) {
        return status;
      }
    }
  }
  return CTOOL_OK;
}

static ctool_buffer_t *cemit_section_buffer(cemit_context_t *context,
                                             ctool_u32 section) {
  if (section == CEMIT_SECTION_TEXT) {
    return context->text;
  }
  if (section == CEMIT_SECTION_RODATA) {
    return context->rodata;
  }
  if (section == CEMIT_SECTION_DATA) {
    return context->data;
  }
  return section >= CEMIT_SECTION_COUNT &&
                 section - CEMIT_SECTION_COUNT <
                     context->named_section_count
             ? context
                   ->named_sections[section - CEMIT_SECTION_COUNT]
                   .contents
             : (ctool_buffer_t *)0;
}

static ctool_u32 cemit_section_flags(const cemit_context_t *context,
                                     ctool_u32 section) {
  if (section == CEMIT_SECTION_TEXT) {
    return CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  }
  if (section == CEMIT_SECTION_RODATA) {
    return CTOOL_ELF32_SHF_ALLOC;
  }
  if (section == CEMIT_SECTION_DATA || section == CEMIT_SECTION_BSS) {
    return CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  }
  return section >= CEMIT_SECTION_COUNT &&
                 section - CEMIT_SECTION_COUNT <
                     context->named_section_count
             ? context
                   ->named_sections[section - CEMIT_SECTION_COUNT]
                   .flags
             : 0u;
}

static ctool_string_t cemit_section_name(
    const cemit_context_t *context, ctool_u32 section) {
  if (section == CEMIT_SECTION_TEXT) {
    return ctool_string(".text");
  }
  if (section == CEMIT_SECTION_RODATA) {
    return ctool_string(".rodata");
  }
  if (section == CEMIT_SECTION_DATA) {
    return ctool_string(".data");
  }
  if (section == CEMIT_SECTION_BSS) {
    return ctool_string(".bss");
  }
  return section >= CEMIT_SECTION_COUNT &&
                 section - CEMIT_SECTION_COUNT <
                     context->named_section_count
             ? context
                   ->named_sections[section - CEMIT_SECTION_COUNT]
                   .name
             : ctool_string("");
}

static ctool_u32 cemit_section_alignment(
    const cemit_context_t *context, ctool_u32 section) {
  if (section < CEMIT_SECTION_COUNT) {
    return context->section_alignment[section];
  }
  return section - CEMIT_SECTION_COUNT < context->named_section_count
             ? context
                   ->named_sections[section - CEMIT_SECTION_COUNT]
                   .alignment
             : 0u;
}

static ctool_status_t cemit_raise_section_alignment(
    cemit_context_t *context, ctool_u32 section, ctool_u32 alignment) {
  if (section < CEMIT_SECTION_COUNT) {
    if (context->section_alignment[section] < alignment) {
      context->section_alignment[section] = alignment;
    }
    return CTOOL_OK;
  }
  if (section - CEMIT_SECTION_COUNT >= context->named_section_count) {
    return CTOOL_ERR_INTERNAL;
  }
  if (context->named_sections[section - CEMIT_SECTION_COUNT].alignment <
      alignment) {
    context->named_sections[section - CEMIT_SECTION_COUNT].alignment =
        alignment;
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_get_named_section(
    cemit_context_t *context, ctool_string_t name, ctool_u32 flags,
    const ctool_c_pp_location_t *location, ctool_u32 *section_out) {
  const ctool_limits_t *limits = ctool_job_limits(context->job);
  ctool_u32 initial_capacity =
      limits->output_bytes < 256u ? limits->output_bytes : 256u;
  ctool_u32 section;
  ctool_status_t status;
  for (section = 0u; section < CEMIT_SECTION_COUNT; section++) {
    if (cemit_strings_equal(name, cemit_section_name(context, section)) ==
        CTOOL_TRUE) {
      if (section == CEMIT_SECTION_BSS ||
          cemit_section_flags(context, section) != flags) {
        return cemit_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SECTION, location,
            "ELF section name is reused with incompatible flags");
      }
      *section_out = section;
      return CTOOL_OK;
    }
  }
  for (section = 0u; section < context->named_section_count; section++) {
    cemit_named_section_t *candidate =
        &context->named_sections[section];
    if (cemit_strings_equal(name, candidate->name) == CTOOL_TRUE) {
      if (candidate->flags != flags) {
        return cemit_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SECTION, location,
            "ELF section name is reused with incompatible flags");
      }
      *section_out = CEMIT_SECTION_COUNT + section;
      return CTOOL_OK;
    }
  }
  if (context->named_section_count >= context->named_section_capacity) {
    return CTOOL_ERR_INTERNAL;
  }
  section = context->named_section_count;
  status = ctool_job_open_buffer(
      context->job, initial_capacity, limits->output_bytes,
      &context->named_sections[section].contents);
  if (status != CTOOL_OK) {
    return status;
  }
  context->named_sections[section].name = name;
  context->named_sections[section].flags = flags;
  context->named_sections[section].alignment = 0u;
  context->named_section_count++;
  *section_out = CEMIT_SECTION_COUNT + section;
  return CTOOL_OK;
}

static ctool_status_t cemit_index_named_sections(
    cemit_context_t *context) {
  ctool_u32 binding;
  for (binding = 0u; binding < context->unit->binding_count; binding++) {
    const ctool_c_binding_t *candidate =
        &context->unit->bindings[binding];
    ctool_u32 flags;
    ctool_status_t status;
    context->binding_sections[binding] = CTOOL_C_AST_NONE;
    if ((candidate->attributes & CTOOL_C_DECL_ATTR_SECTION) == 0u) {
      continue;
    }
    flags = candidate->kind == CTOOL_C_BINDING_FUNCTION
                ? CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR
                : cemit_type_is_const(context, candidate->type) ==
                          CTOOL_TRUE
                      ? CTOOL_ELF32_SHF_ALLOC
                      : CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
    status = cemit_get_named_section(
        context, candidate->section_name, flags, &candidate->location,
        &context->binding_sections[binding]);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_align_buffer(cemit_context_t *context,
                                          ctool_u32 section,
                                          ctool_u32 alignment) {
  ctool_buffer_t *buffer = cemit_section_buffer(context, section);
  ctool_u32 aligned;
  ctool_u32 size;
  ctool_status_t status;
  if (buffer == (ctool_buffer_t *)0) {
    return CTOOL_ERR_INTERNAL;
  }
  size = ctool_buffer_view(buffer).size;
  status = cemit_align_value(size, alignment, &aligned);
  if (status != CTOOL_OK) {
    return status;
  }
  return ctool_buffer_fill(buffer,
                           (cemit_section_flags(context, section) &
                            CTOOL_ELF32_SHF_EXECINSTR) != 0u
                               ? 0x90u
                               : 0u,
                           aligned - size);
}

static ctool_status_t cemit_patch_integer(cemit_context_t *context,
                                           ctool_u32 section,
                                           ctool_u32 offset,
                                           ctool_u32 size,
                                           ctool_u64 bits) {
  ctool_buffer_t *buffer = cemit_section_buffer(context, section);
  ctool_bytes_t view;
  ctool_u32 index;
  ctool_status_t status = CTOOL_OK;
  if (buffer == (ctool_buffer_t *)0 || size == 0u || size > 8u) {
    return CTOOL_ERR_INTERNAL;
  }
  view = ctool_buffer_view(buffer);
  if (offset > view.size || size > view.size - offset) {
    return CTOOL_ERR_INTERNAL;
  }
  for (index = 0u; index < size && status == CTOOL_OK; index++) {
    status = ctool_buffer_patch_u8(
        buffer, offset + index,
        (ctool_u8)((bits >> (index * 8u)) & 0xffu));
  }
  return status;
}

static ctool_status_t cemit_add_relocation(
    cemit_context_t *context, ctool_u32 section, ctool_u32 offset,
    ctool_u32 symbol, ctool_elf32_relocation_type_t type,
    ctool_i32 addend) {
  ctool_elf32_relocation_spec_t *relocation;
  ctool_status_t status;
  if (context->relocation_count >= context->relocation_capacity ||
      symbol >= context->symbol_count) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_patch_integer(context, section, offset, 4u,
                               (ctool_u64)(ctool_u32)addend);
  if (status != CTOOL_OK) {
    return status;
  }
  relocation = &context->relocations[context->relocation_count++];
  relocation->target_section = section;
  relocation->offset = offset;
  relocation->symbol = symbol;
  relocation->type = type;
  relocation->addend = addend;
  return CTOOL_OK;
}

static ctool_status_t cemit_add_literal_bytes(
    cemit_context_t *context, ctool_bytes_t bytes,
    ctool_u32 *symbol_out) {
  ctool_elf32_symbol_spec_t *symbol;
  ctool_u32 offset = ctool_buffer_view(context->rodata).size;
  ctool_status_t status;
  if (context->symbol_count >= context->symbol_capacity) {
    return CTOOL_ERR_INTERNAL;
  }
  if (bytes.data == (const ctool_u8 *)0 || bytes.size == 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = ctool_buffer_append(context->rodata, bytes);
  if (status != CTOOL_OK) {
    return status;
  }
  symbol = &context->symbols[context->symbol_count];
  status = cemit_make_literal_name(context, &symbol->name);
  if (status != CTOOL_OK) {
    return status;
  }
  symbol->binding = CTOOL_ELF32_BIND_LOCAL;
  symbol->type = CTOOL_ELF32_SYMBOL_OBJECT;
  symbol->visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbol->placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbol->section = CEMIT_SECTION_RODATA;
  symbol->value = offset;
  symbol->size = bytes.size;
  symbol->alignment = 0u;
  *symbol_out = context->symbol_count++;
  if (context->section_alignment[CEMIT_SECTION_RODATA] < 1u) {
    context->section_alignment[CEMIT_SECTION_RODATA] = 1u;
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_add_string_literal(
    cemit_context_t *context, const ctool_c_initializer_t *initializer,
    ctool_u32 *symbol_out) {
  return cemit_add_literal_bytes(context, initializer->string_bytes,
                                 symbol_out);
}

static ctool_status_t cemit_encode_initializer(
    cemit_context_t *context, ctool_u32 initializer_index,
    ctool_u32 section, ctool_u32 offset, ctool_u32 depth);

static ctool_status_t cemit_encode_bit_field(
    cemit_context_t *context, const ctool_c_initializer_t *initializer,
    const ctool_c_member_layout_t *layout, ctool_u32 section,
    ctool_u32 offset) {
  ctool_buffer_t *buffer = cemit_section_buffer(context, section);
  ctool_bytes_t view;
  ctool_u64 storage = 0u;
  ctool_u64 mask;
  ctool_u64 value;
  ctool_u32 index;
  if (buffer == (ctool_buffer_t *)0 || layout->size == 0u ||
      layout->size > 8u || layout->bit_width == 0u ||
      layout->bit_width > 64u ||
      layout->bit_offset > layout->size * 8u ||
      layout->bit_width > layout->size * 8u - layout->bit_offset ||
      (initializer->kind != CTOOL_C_INITIALIZER_ZERO &&
       initializer->kind != CTOOL_C_INITIALIZER_INTEGER)) {
    return cemit_invalid_unit(context, &initializer->location);
  }
  view = ctool_buffer_view(buffer);
  if (offset > view.size || layout->size > view.size - offset) {
    return cemit_invalid_unit(context, &initializer->location);
  }
  for (index = 0u; index < layout->size; index++) {
    storage |= (ctool_u64)view.data[offset + index] << (index * 8u);
  }
  mask = layout->bit_width == 64u
             ? ~(ctool_u64)0u
             : (((ctool_u64)1u << layout->bit_width) - 1u);
  value = initializer->kind == CTOOL_C_INITIALIZER_INTEGER
              ? initializer->integer_bits & mask
              : 0u;
  storage &= ~(mask << layout->bit_offset);
  storage |= value << layout->bit_offset;
  return cemit_patch_integer(context, section, offset, layout->size,
                             storage);
}

static ctool_status_t cemit_encode_list(
    cemit_context_t *context, const ctool_c_initializer_t *initializer,
    ctool_u32 section, ctool_u32 offset, ctool_u32 depth) {
  const ctool_c_type_node_t *type =
      cemit_unwrapped_type(context, initializer->type);
  ctool_u32 edge_index;
  if (type == (const ctool_c_type_node_t *)0) {
    return cemit_invalid_unit(context, &initializer->location);
  }
  for (edge_index = 0u; edge_index < initializer->element_count;
       edge_index++) {
    const ctool_c_initializer_element_t *edge =
        &context->unit->initializer_elements
             [initializer->first_element + edge_index];
    const ctool_c_initializer_t *child =
        &context->unit->initializers[edge->initializer];
    ctool_u32 child_offset;
    ctool_status_t status;
    if (type->kind == CTOOL_C_TYPE_ARRAY) {
      const ctool_c_type_layout_t *element_layout;
      if (type->referenced_type >= context->unit->layout.type_count ||
          edge->subobject >= type->element_count) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      element_layout = &context->unit->layout.types[type->referenced_type];
      if (cemit_multiply_overflows(edge->subobject, element_layout->size) ==
              CTOOL_TRUE ||
          cemit_add_overflows(offset,
                              edge->subobject * element_layout->size) ==
              CTOOL_TRUE) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      child_offset = offset + edge->subobject * element_layout->size;
    } else if (type->kind == CTOOL_C_TYPE_RECORD &&
               (type->record_kind == CTOOL_C_RECORD_STRUCT ||
                type->record_kind == CTOOL_C_RECORD_UNION)) {
      const ctool_c_record_member_t *member;
      const ctool_c_member_layout_t *member_layout;
      if (type->first_member > context->unit->graph.member_count ||
          type->member_count >
              context->unit->graph.member_count - type->first_member ||
          edge->subobject < type->first_member ||
          edge->subobject - type->first_member >= type->member_count ||
          edge->subobject >= context->unit->layout.member_count) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      member = &context->unit->graph.members[edge->subobject];
      member_layout = &context->unit->layout.members[edge->subobject];
      if (cemit_add_overflows(offset, member_layout->byte_offset) ==
          CTOOL_TRUE) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      child_offset = offset + member_layout->byte_offset;
      if (member->is_bit_field == CTOOL_TRUE) {
        status = cemit_encode_bit_field(context, child, member_layout,
                                        section, child_offset);
        if (status != CTOOL_OK) {
          return status;
        }
        continue;
      }
    } else {
      return cemit_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_INITIALIZER,
          &initializer->location,
          "CupidC object emission does not yet support this aggregate initializer");
    }
    status = cemit_encode_initializer(context, edge->initializer, section,
                                      child_offset, depth + 1u);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_encode_initializer(
    cemit_context_t *context, ctool_u32 initializer_index,
    ctool_u32 section, ctool_u32 offset, ctool_u32 depth) {
  const ctool_c_initializer_t *initializer;
  const ctool_c_type_layout_t *layout;
  ctool_buffer_t *buffer;
  ctool_status_t status;
  ctool_u32 symbol = CTOOL_C_AST_NONE;
  ctool_u32 index;
  if (initializer_index >= context->unit->initializer_count ||
      depth > CTOOL_C_PARSE_NESTING_LIMIT) {
    return cemit_invalid_unit(context,
                              (const ctool_c_pp_location_t *)0);
  }
  initializer = &context->unit->initializers[initializer_index];
  layout = &context->unit->layout.types[initializer->type];
  if (initializer->kind == CTOOL_C_INITIALIZER_EXPRESSION) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_INITIALIZER,
        &initializer->location,
        "CupidC object emission requires static initializer values");
  }
  if (initializer->kind == CTOOL_C_INITIALIZER_ZERO) {
    return CTOOL_OK;
  }
  if (initializer->kind == CTOOL_C_INITIALIZER_INTEGER) {
    return cemit_patch_integer(context, section, offset, layout->size,
                               initializer->integer_bits);
  }
  if (initializer->kind == CTOOL_C_INITIALIZER_FLOATING) {
    return cemit_patch_integer(context, section, offset, layout->size,
                               initializer->integer_bits);
  }
  if (initializer->kind == CTOOL_C_INITIALIZER_STRING) {
    buffer = cemit_section_buffer(context, section);
    if (buffer == (ctool_buffer_t *)0 ||
        initializer->string_bytes.size > layout->size) {
      return cemit_invalid_unit(context, &initializer->location);
    }
    for (index = 0u; index < initializer->string_bytes.size; index++) {
      status = ctool_buffer_patch_u8(
          buffer, offset + index, initializer->string_bytes.data[index]);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    return CTOOL_OK;
  }
  if (initializer->kind == CTOOL_C_INITIALIZER_ADDRESS) {
    if (layout->size != 4u) {
      return cemit_invalid_unit(context, &initializer->location);
    }
    if (initializer->address_kind ==
        CTOOL_C_INITIALIZER_ADDRESS_BINDING) {
      status = cemit_ensure_binding_symbol(
          context, initializer->address_reference, &symbol);
    } else {
      status = cemit_add_string_literal(context, initializer, &symbol);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (symbol == CTOOL_C_AST_NONE) {
      return CTOOL_ERR_INTERNAL;
    }
    return cemit_add_relocation(context, section, offset, symbol,
                                CTOOL_ELF32_R_386_32,
                                initializer->address_addend);
  }
  if (initializer->kind == CTOOL_C_INITIALIZER_LIST) {
    return cemit_encode_list(context, initializer, section, offset, depth);
  }
  return cemit_invalid_unit(context, &initializer->location);
}

static ctool_status_t cemit_place_static_object(
    cemit_context_t *context, ctool_u32 type, ctool_u32 initializer_index,
    ctool_u32 alignment, ctool_u32 symbol_index, ctool_u32 section_override,
    const ctool_c_pp_location_t *location) {
  const ctool_c_type_layout_t *layout;
  const ctool_c_initializer_t *initializer;
  ctool_elf32_symbol_spec_t *symbol;
  ctool_u32 section;
  ctool_u32 offset;
  ctool_status_t status;
  ctool_mut_bytes_t reserved;
  if (type >= context->unit->layout.type_count ||
      initializer_index >= context->unit->initializer_count ||
      symbol_index >= context->symbol_count) {
    return cemit_invalid_unit(context, location);
  }
  layout = &context->unit->layout.types[type];
  initializer = &context->unit->initializers[initializer_index];
  symbol = &context->symbols[symbol_index];
  if (layout->is_complete_object == CTOOL_FALSE ||
      layout->is_object == CTOOL_FALSE || layout->size == 0u ||
      layout->alignment == 0u ||
      cemit_power_of_two(layout->alignment) == CTOOL_FALSE ||
      alignment < layout->alignment ||
      cemit_power_of_two(alignment) == CTOOL_FALSE ||
      initializer->type != type ||
      symbol->type != CTOOL_ELF32_SYMBOL_OBJECT ||
      symbol->placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      symbol->section != CTOOL_ELF32_NO_SECTION) {
    return cemit_invalid_unit(context, location);
  }
  if (section_override != CTOOL_C_AST_NONE) {
    section = section_override;
  } else if (cemit_type_is_const(context, type) == CTOOL_TRUE) {
    section = CEMIT_SECTION_RODATA;
  } else if (context->initializer_is_zero[initializer_index] ==
             CTOOL_TRUE) {
    section = CEMIT_SECTION_BSS;
  } else {
    section = CEMIT_SECTION_DATA;
  }
  if (section == CEMIT_SECTION_BSS) {
    status = cemit_align_value(context->bss_size, alignment, &offset);
    if (status == CTOOL_OK &&
        cemit_add_overflows(offset, layout->size) == CTOOL_TRUE) {
      status = CTOOL_ERR_OVERFLOW;
    }
    if (status != CTOOL_OK) {
      return status;
    }
    context->bss_size = offset + layout->size;
  } else {
    ctool_buffer_t *buffer = cemit_section_buffer(context, section);
    status = cemit_align_buffer(context, section, alignment);
    if (status != CTOOL_OK) {
      return status;
    }
    status = ctool_buffer_reserve_zero(buffer, layout->size, &offset,
                                       &reserved);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  status = cemit_raise_section_alignment(context, section, alignment);
  if (status != CTOOL_OK) {
    return status;
  }
  symbol->placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbol->section = section;
  symbol->value = offset;
  symbol->size = layout->size;
  symbol->alignment = 0u;
  if (section != CEMIT_SECTION_BSS) {
    status = cemit_encode_initializer(context, initializer_index, section,
                                      offset, 0u);
  }
  return status;
}

static ctool_status_t cemit_place_definition(
    cemit_context_t *context, ctool_u32 definition_index) {
  const ctool_c_object_definition_t *definition;
  const ctool_c_binding_t *binding;
  const ctool_c_type_layout_t *layout;
  ctool_u32 alignment;
  ctool_u32 symbol_index = CTOOL_C_AST_NONE;
  ctool_status_t status;
  if (definition_index >= context->unit->object_definition_count) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  definition = &context->unit->object_definitions[definition_index];
  if (definition->binding >= context->unit->binding_count ||
      definition->declared_type >= context->unit->layout.type_count) {
    return cemit_invalid_unit(context, &definition->location);
  }
  binding = &context->unit->bindings[definition->binding];
  layout = &context->unit->layout.types[definition->declared_type];
  alignment = layout->alignment;
  if (binding->minimum_alignment > alignment) {
    alignment = binding->minimum_alignment;
  }
  status = cemit_ensure_binding_symbol(context, definition->binding,
                                        &symbol_index);
  if (status != CTOOL_OK) {
    return status;
  }
  return cemit_place_static_object(
      context, definition->declared_type, definition->initializer, alignment,
      symbol_index, context->binding_sections[definition->binding],
      &definition->location);
}

static ctool_status_t cemit_place_block_static(
    cemit_context_t *context, ctool_u32 block_binding_index) {
  const ctool_c_block_binding_t *binding;
  ctool_u32 symbol_index = CTOOL_C_AST_NONE;
  ctool_status_t status;
  if (block_binding_index >= context->unit->block_binding_count) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  binding = &context->unit->block_bindings[block_binding_index];
  if (binding->storage != CTOOL_C_STORAGE_STATIC) {
    return CTOOL_OK;
  }
  if (binding->kind != CTOOL_C_BINDING_OBJECT ||
      binding->type >= context->unit->layout.type_count ||
      binding->initializer >= context->unit->initializer_count) {
    return cemit_invalid_unit(context, &binding->location);
  }
  status = cemit_ensure_block_binding_symbol(
      context, block_binding_index, &symbol_index);
  if (status != CTOOL_OK) {
    return status;
  }
  return cemit_place_static_object(
      context, binding->type, binding->initializer,
      context->unit->layout.types[binding->type].alignment, symbol_index,
      CTOOL_C_AST_NONE, &binding->location);
}

static ctool_x86_reg_t cemit_x86_register(
    ctool_x86_reg_class_t class_id, ctool_u8 index) {
  ctool_x86_reg_t reg;
  reg.class_id = class_id;
  reg.index = index;
  return reg;
}

static ctool_x86_value_t cemit_x86_constant(ctool_u32 bits) {
  ctool_x86_value_t value;
  value.kind = CTOOL_X86_VALUE_CONSTANT;
  value.bits = bits;
  value.addend = 0;
  value.reference = 0u;
  return value;
}

static ctool_x86_operand_t cemit_x86_register_operand(
    ctool_x86_reg_class_t class_id, ctool_u8 index) {
  ctool_x86_operand_t operand;
  cemit_zero(&operand, (ctool_u32)sizeof(operand));
  operand.kind = CTOOL_X86_OPERAND_REGISTER;
  operand.as.reg = cemit_x86_register(class_id, index);
  return operand;
}

static ctool_x86_operand_t cemit_x86_value_operand(
    ctool_x86_operand_kind_t kind, ctool_u16 width_bits,
    ctool_u16 encoding_bits, ctool_u32 bits) {
  ctool_x86_operand_t operand;
  cemit_zero(&operand, (ctool_u32)sizeof(operand));
  operand.kind = kind;
  operand.width_bits = width_bits;
  operand.encoding_bits = encoding_bits;
  operand.as.value = cemit_x86_constant(bits);
  return operand;
}

static ctool_x86_operand_t cemit_x86_memory_operand(
    ctool_x86_reg_t base, ctool_i32 displacement,
    ctool_u16 displacement_bits) {
  ctool_x86_operand_t operand;
  cemit_zero(&operand, (ctool_u32)sizeof(operand));
  operand.kind = CTOOL_X86_OPERAND_MEMORY;
  operand.width_bits = 32u;
  operand.as.memory.address_bits = 32u;
  operand.as.memory.segment =
      cemit_x86_register(CTOOL_X86_REG_NONE, 0u);
  operand.as.memory.base = base;
  operand.as.memory.index =
      cemit_x86_register(CTOOL_X86_REG_NONE, 0u);
  operand.as.memory.scale = 1u;
  operand.as.memory.displacement =
      cemit_x86_constant((ctool_u32)displacement);
  operand.as.memory.displacement_bits = displacement_bits;
  return operand;
}

static ctool_x86_instruction_t cemit_x86_instruction(
    ctool_x86_mnemonic_t mnemonic, ctool_u16 operand_bits) {
  ctool_x86_instruction_t instruction;
  cemit_zero(&instruction, (ctool_u32)sizeof(instruction));
  instruction.mnemonic = mnemonic;
  instruction.operand_bits = operand_bits;
  instruction.address_bits = 32u;
  return instruction;
}

static ctool_status_t cemit_x86_encode(
    cemit_context_t *context, const ctool_x86_instruction_t *instruction,
    ctool_x86_encoding_t *encoding_out, ctool_u32 *offset_out) {
  ctool_x86_encoding_t encoding;
  ctool_u32 offset = ctool_buffer_view(context->active_text).size;
  ctool_status_t status = ctool_x86_encode(
      context->job, CTOOL_X86_MODE_32, instruction, CTOOL_X86_FORM_AUTO,
      &encoding);
  if (status != CTOOL_OK) {
    return status;
  }
  status = ctool_buffer_append(
      context->active_text, ctool_bytes(encoding.bytes, encoding.size));
  if (status != CTOOL_OK) {
    return status;
  }
  if (encoding_out != (ctool_x86_encoding_t *)0) {
    *encoding_out = encoding;
  }
  if (offset_out != (ctool_u32 *)0) {
    *offset_out = offset;
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_x86_no_operand(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, 32u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_x87_memory(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u8 base_register, ctool_i32 displacement,
    ctool_u16 width_bits) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, width_bits);
  if ((mnemonic != CTOOL_X86_MN_FLD &&
       mnemonic != CTOOL_X86_MN_FSTP) ||
      (width_bits != 32u && width_bits != 64u)) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, base_register),
      displacement, 0u);
  instruction.operands[0].width_bits = width_bits;
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_x87_absolute_symbol(
    cemit_context_t *context, ctool_u32 symbol) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_FLD, 64u);
  ctool_x86_operand_t memory = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_NONE, 0u), 0, 32u);
  ctool_x86_encoding_t encoding;
  ctool_u32 offset;
  ctool_u32 relocation_offset;
  ctool_status_t status;
  if (symbol >= context->symbol_count) {
    return CTOOL_ERR_INTERNAL;
  }
  memory.width_bits = 64u;
  memory.as.memory.displacement.kind = CTOOL_X86_VALUE_REFERENCE;
  memory.as.memory.displacement.bits = 0u;
  memory.as.memory.displacement.addend = 0;
  memory.as.memory.displacement.reference = symbol;
  instruction.operand_count = 1u;
  instruction.operands[0] = memory;
  status = cemit_x86_encode(
      context, &instruction, &encoding, &offset);
  if (status != CTOOL_OK) {
    return status;
  }
  if (encoding.size != 6u ||
      encoding.field_count != 1u ||
      encoding.fields[0].kind != CTOOL_X86_FIELD_DISPLACEMENT ||
      encoding.fields[0].relocation != CTOOL_X86_RELOC_ABSOLUTE ||
      encoding.fields[0].operand_index != 0u ||
      encoding.fields[0].byte_offset != 2u ||
      encoding.fields[0].byte_width != 4u ||
      encoding.fields[0].pc_bias != 0u ||
      encoding.fields[0].reference != symbol ||
      encoding.fields[0].encoded_addend != 0 ||
      cemit_add_overflows(
          offset, encoding.fields[0].byte_offset) == CTOOL_TRUE) {
    return CTOOL_ERR_INTERNAL;
  }
  relocation_offset = offset + encoding.fields[0].byte_offset;
  return cemit_add_relocation(
      context, context->active_text_section, relocation_offset, symbol,
      CTOOL_ELF32_R_386_32, 0);
}

static ctool_status_t cemit_x86_sse_memory(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_bool load, ctool_u8 xmm_register,
    ctool_u8 base_register, ctool_i32 displacement,
    ctool_u16 width_bits) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, 32u);
  ctool_x86_operand_t xmm =
      cemit_x86_register_operand(
          CTOOL_X86_REG_XMM, xmm_register);
  ctool_x86_operand_t memory = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, base_register),
      displacement, 0u);
  if ((mnemonic != CTOOL_X86_MN_MOVSS || width_bits != 32u) &&
      (mnemonic != CTOOL_X86_MN_MOVSD || width_bits != 64u)) {
    return CTOOL_ERR_INTERNAL;
  }
  memory.width_bits = width_bits;
  instruction.operand_count = 2u;
  instruction.operands[0] =
      load == CTOOL_TRUE ? xmm : memory;
  instruction.operands[1] =
      load == CTOOL_TRUE ? memory : xmm;
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_simd_memory(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_bool load, ctool_u8 xmm_register,
    ctool_u8 base_register, ctool_i32 displacement,
    ctool_u16 width_bits) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, width_bits);
  ctool_x86_operand_t xmm =
      cemit_x86_register_operand(
          CTOOL_X86_REG_XMM, xmm_register);
  ctool_x86_operand_t memory = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, base_register),
      displacement, 0u);
  if (!((mnemonic == CTOOL_X86_MN_MOVDQU &&
         width_bits == 128u) ||
        (mnemonic == CTOOL_X86_MN_MOVNTDQ &&
         width_bits == 128u && load == CTOOL_FALSE) ||
        (mnemonic == CTOOL_X86_MN_MOVD &&
         width_bits == 32u && load == CTOOL_TRUE)) ||
      xmm_register >= 8u || base_register >= 8u) {
    return CTOOL_ERR_INTERNAL;
  }
  memory.width_bits = width_bits;
  instruction.operand_count = 2u;
  instruction.operands[0] =
      load == CTOOL_TRUE ? xmm : memory;
  instruction.operands[1] =
      load == CTOOL_TRUE ? memory : xmm;
  return cemit_x86_encode(
      context, &instruction, (ctool_x86_encoding_t *)0,
      (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_xmm_shuffle_immediate(
    cemit_context_t *context, ctool_u8 destination,
    ctool_u8 source, ctool_u8 immediate) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_PSHUFD, 128u);
  if (destination >= 8u || source >= 8u) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 3u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_XMM, destination);
  instruction.operands[1] =
      cemit_x86_register_operand(CTOOL_X86_REG_XMM, source);
  instruction.operands[2] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 8u, 8u, immediate);
  return cemit_x86_encode(
      context, &instruction, (ctool_x86_encoding_t *)0,
      (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_xmm_shift_immediate(
    cemit_context_t *context, ctool_u8 destination,
    ctool_u8 immediate) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_PSRLW, 128u);
  if (destination >= 8u) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_XMM, destination);
  instruction.operands[1] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 8u, 8u, immediate);
  return cemit_x86_encode(
      context, &instruction, (ctool_x86_encoding_t *)0,
      (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_sse_absolute_mask(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u32 symbol) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, 128u);
  ctool_x86_operand_t memory = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_NONE, 0u), 0, 32u);
  ctool_x86_encoding_t encoding;
  ctool_u32 offset;
  ctool_u32 relocation_offset;
  ctool_u8 expected_size;
  ctool_u8 expected_field_offset;
  ctool_status_t status;
  if ((mnemonic != CTOOL_X86_MN_ANDPD &&
       mnemonic != CTOOL_X86_MN_ANDPS) ||
      symbol >= context->symbol_count) {
    return CTOOL_ERR_INTERNAL;
  }
  expected_size =
      mnemonic == CTOOL_X86_MN_ANDPD ? 8u : 7u;
  expected_field_offset =
      mnemonic == CTOOL_X86_MN_ANDPD ? 4u : 3u;
  memory.width_bits = 128u;
  memory.as.memory.displacement.kind = CTOOL_X86_VALUE_REFERENCE;
  memory.as.memory.displacement.bits = 0u;
  memory.as.memory.displacement.addend = 0;
  memory.as.memory.displacement.reference = symbol;
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_XMM, 0u);
  instruction.operands[1] = memory;
  status = cemit_x86_encode(
      context, &instruction, &encoding, &offset);
  if (status != CTOOL_OK) {
    return status;
  }
  if (encoding.size != expected_size ||
      encoding.field_count != 1u ||
      encoding.fields[0].kind != CTOOL_X86_FIELD_DISPLACEMENT ||
      encoding.fields[0].relocation != CTOOL_X86_RELOC_ABSOLUTE ||
      encoding.fields[0].operand_index != 1u ||
      encoding.fields[0].byte_offset != expected_field_offset ||
      encoding.fields[0].byte_width != 4u ||
      encoding.fields[0].pc_bias != 0u ||
      encoding.fields[0].reference != symbol ||
      encoding.fields[0].encoded_addend != 0 ||
      cemit_add_overflows(
          offset, encoding.fields[0].byte_offset) == CTOOL_TRUE) {
    return CTOOL_ERR_INTERNAL;
  }
  relocation_offset = offset + encoding.fields[0].byte_offset;
  return cemit_add_relocation(
      context, context->active_text_section, relocation_offset, symbol,
      CTOOL_ELF32_R_386_32, 0);
}

static ctool_status_t cemit_x86_fxsave_memory(
    cemit_context_t *context, ctool_u8 base_register) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_FXSAVE, 32u);
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, base_register),
      0, 0u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_ldmxcsr_memory(
    cemit_context_t *context, ctool_u8 base_register) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_LDMXCSR, 32u);
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, base_register),
      0, 0u);
  instruction.operands[0].width_bits = 32u;
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_lgdt_memory(
    cemit_context_t *context, ctool_u8 base_register) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_LGDT, 32u);
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, base_register),
      0, 0u);
  instruction.operands[0].width_bits = 48u;
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_move_ax_immediate(
    cemit_context_t *context, ctool_u16 immediate) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 16u);
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR16, 0u);
  instruction.operands[1] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 16u, 16u, immediate);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_move_segment_ax(
    cemit_context_t *context, ctool_u8 segment_register) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 16u);
  if (segment_register >= 6u) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(
          CTOOL_X86_REG_SEGMENT, segment_register);
  instruction.operands[1] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR16, 0u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_state_memory(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u8 base_register, ctool_u16 width_bits) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, width_bits);
  if (!((mnemonic == CTOOL_X86_MN_FNSTSW ||
         mnemonic == CTOOL_X86_MN_FNSTCW) &&
        width_bits == 16u) &&
      !(mnemonic == CTOOL_X86_MN_STMXCSR &&
        width_bits == 32u)) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, base_register),
      0, 0u);
  instruction.operands[0].width_bits = width_bits;
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_x87_control_memory(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u8 base_register, ctool_i32 displacement) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, 16u);
  if (mnemonic != CTOOL_X86_MN_FNSTCW &&
      mnemonic != CTOOL_X86_MN_FLDCW) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, base_register),
      displacement, 0u);
  instruction.operands[0].width_bits = 16u;
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_move_word_stack_ax(
    cemit_context_t *context, ctool_bool load,
    ctool_i32 displacement) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 16u);
  ctool_x86_operand_t memory = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 4u),
      displacement, 0u);
  ctool_x86_operand_t ax =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR16, 0u);
  memory.width_bits = 16u;
  instruction.operand_count = 2u;
  instruction.operands[0] = load == CTOOL_TRUE ? ax : memory;
  instruction.operands[1] = load == CTOOL_TRUE ? memory : ax;
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_word_stack_immediate(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_i32 displacement, ctool_u16 immediate) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, 16u);
  if (mnemonic != CTOOL_X86_MN_AND &&
      mnemonic != CTOOL_X86_MN_OR) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 4u),
      displacement, 0u);
  instruction.operands[0].width_bits = 16u;
  instruction.operands[1] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 16u, 16u, immediate);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_word_ax_immediate(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u16 immediate) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, 16u);
  if (mnemonic != CTOOL_X86_MN_AND &&
      mnemonic != CTOOL_X86_MN_OR &&
      mnemonic != CTOOL_X86_MN_TEST) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR16, 0u);
  instruction.operands[1] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 16u, 16u, immediate);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_repeat_string(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u16 operand_bits) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, operand_bits);
  instruction.prefixes = CTOOL_X86_PREFIX_REP;
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_one_register(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_x86_reg_class_t class_id, ctool_u8 index,
    ctool_u16 operand_bits) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, operand_bits);
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_register_operand(class_id, index);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_two_registers(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_x86_reg_class_t left_class, ctool_u8 left_index,
    ctool_x86_reg_class_t right_class, ctool_u8 right_index,
    ctool_u16 operand_bits) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, operand_bits);
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(left_class, left_index);
  instruction.operands[1] =
      cemit_x86_register_operand(right_class, right_index);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_load_segment_absolute_register(
    cemit_context_t *context, ctool_u8 destination_register,
    ctool_u8 segment_register, ctool_u32 address) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  if (destination_register >= 8u || segment_register >= 6u) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(
          CTOOL_X86_REG_GPR32, destination_register);
  instruction.operands[1] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_NONE, 0u),
      (ctool_i32)address, 32u);
  instruction.operands[1].as.memory.segment =
      cemit_x86_register(CTOOL_X86_REG_SEGMENT, segment_register);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_push_integer(cemit_context_t *context,
                                             ctool_u32 bits) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_PUSH, 32u);
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u, bits);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_push_stack_dword(
    cemit_context_t *context, ctool_u32 displacement) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_PUSH, 32u);
  if (displacement == 0u || displacement > 0x7fu) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 4u),
      (ctool_i32)displacement, 8u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_move_register_constant(
    cemit_context_t *context, ctool_u8 register_index, ctool_u32 bits) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, register_index);
  instruction.operands[1] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u, bits);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_move_register_symbol(
    cemit_context_t *context, ctool_u8 register_index,
    ctool_u32 symbol) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  ctool_x86_encoding_t encoding;
  ctool_u32 offset;
  ctool_u32 relocation_offset;
  ctool_status_t status;
  if (register_index >= 8u) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, register_index);
  instruction.operands[1] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u, 0u);
  instruction.operands[1].as.value.kind = CTOOL_X86_VALUE_REFERENCE;
  instruction.operands[1].as.value.reference = symbol;
  status = cemit_x86_encode(context, &instruction, &encoding, &offset);
  if (status != CTOOL_OK) {
    return status;
  }
  if (encoding.size != 5u || encoding.field_count != 1u ||
      encoding.fields[0].kind != CTOOL_X86_FIELD_IMMEDIATE ||
      encoding.fields[0].operand_index != 1u ||
      encoding.fields[0].relocation != CTOOL_X86_RELOC_ABSOLUTE ||
      encoding.fields[0].byte_offset != 1u ||
      encoding.fields[0].byte_width != 4u ||
      encoding.fields[0].pc_bias != 0u ||
      encoding.fields[0].reference != symbol ||
      encoding.fields[0].encoded_addend != 0 ||
      cemit_add_overflows(offset, encoding.fields[0].byte_offset) ==
          CTOOL_TRUE) {
    return CTOOL_ERR_INTERNAL;
  }
  relocation_offset = offset + encoding.fields[0].byte_offset;
  return cemit_add_relocation(
      context, context->active_text_section, relocation_offset,
      symbol, CTOOL_ELF32_R_386_32, 0);
}

static ctool_status_t cemit_x86_scale_register(
    cemit_context_t *context, ctool_u8 register_index, ctool_u32 scale) {
  ctool_status_t status;
  if (scale == 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  if (scale == 1u) {
    return CTOOL_OK;
  }
  status = cemit_x86_move_register_constant(context, 2u, scale);
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_IMUL, CTOOL_X86_REG_GPR32, register_index,
        CTOOL_X86_REG_GPR32, 2u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_lea_parameter(cemit_context_t *context,
                                              ctool_u32 offset) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_LEA, 32u);
  if (offset > 0x7fffffffu) {
    return CTOOL_ERR_OVERFLOW;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, 0u);
  instruction.operands[1] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 5u),
      (ctool_i32)offset, 32u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_reserve_locals(cemit_context_t *context,
                                                ctool_u32 byte_count) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_SUB, 32u);
  if (byte_count == 0u) {
    return CTOOL_OK;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, 4u);
  instruction.operands[1] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, byte_count);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_lea_local(cemit_context_t *context,
                                          ctool_u32 offset) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_LEA, 32u);
  if (offset == 0u || offset > 0x7fffffffu) {
    return CTOOL_ERR_OVERFLOW;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, 0u);
  instruction.operands[1] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 5u),
      0 - (ctool_i32)offset, 0u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_lea_stack(
    cemit_context_t *context, ctool_u8 register_index,
    ctool_u32 offset) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_LEA, 32u);
  if (offset > 0x7fffffffu) {
    return CTOOL_ERR_OVERFLOW;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, register_index);
  instruction.operands[1] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 4u),
      (ctool_i32)offset, 0u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_load_frame(
    cemit_context_t *context, ctool_u8 register_index,
    ctool_u32 offset) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  if (offset > 0x7fffffffu) {
    return CTOOL_ERR_OVERFLOW;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, register_index);
  instruction.operands[1] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 5u),
      (ctool_i32)offset, 32u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_load_eax(cemit_context_t *context,
                                         ctool_u32 type) {
  const ctool_c_type_layout_t *layout;
  ctool_x86_mnemonic_t mnemonic;
  ctool_x86_instruction_t instruction;
  ctool_u16 width_bits;
  if (type >= context->unit->layout.type_count) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->is_object == CTOOL_FALSE ||
      layout->is_complete_object == CTOOL_FALSE ||
      (layout->size != 1u && layout->size != 2u && layout->size != 4u) ||
      (layout->is_integer == CTOOL_FALSE && layout->size != 4u)) {
    return CTOOL_ERR_INTERNAL;
  }
  width_bits = (ctool_u16)(layout->size * 8u);
  mnemonic = layout->size == 4u
                 ? CTOOL_X86_MN_MOV
                 : layout->is_signed == CTOOL_TRUE ? CTOOL_X86_MN_MOVSX
                                                    : CTOOL_X86_MN_MOVZX;
  instruction = cemit_x86_instruction(mnemonic, 32u);
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, 0u);
  instruction.operands[1] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 0u), 0, 0u);
  instruction.operands[1].width_bits = width_bits;
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_store_ecx_at_eax(
    cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_layout_t *layout;
  ctool_x86_reg_class_t register_class;
  ctool_u16 width_bits;
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  if (type >= context->unit->layout.type_count) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->is_object == CTOOL_FALSE ||
      layout->is_complete_object == CTOOL_FALSE ||
      (layout->size != 1u && layout->size != 2u && layout->size != 4u) ||
      (layout->is_integer == CTOOL_FALSE && layout->size != 4u)) {
    return CTOOL_ERR_INTERNAL;
  }
  width_bits = (ctool_u16)(layout->size * 8u);
  register_class = layout->size == 1u
                       ? CTOOL_X86_REG_GPR8
                       : layout->size == 2u ? CTOOL_X86_REG_GPR16
                                            : CTOOL_X86_REG_GPR32;
  instruction.operand_bits = width_bits;
  instruction.operand_count = 2u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 0u), 0, 0u);
  instruction.operands[0].width_bits = width_bits;
  instruction.operands[1] =
      cemit_x86_register_operand(register_class, 1u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_atomic_memory_ecx(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u32 type, ctool_bool locked) {
  const ctool_c_type_layout_t *layout;
  ctool_x86_reg_class_t register_class;
  ctool_x86_instruction_t instruction;
  ctool_u16 width_bits;
  if (type >= context->unit->layout.type_count ||
      (mnemonic != CTOOL_X86_MN_XCHG &&
       mnemonic != CTOOL_X86_MN_XADD) ||
      (locked == CTOOL_TRUE && mnemonic != CTOOL_X86_MN_XADD)) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->is_integer == CTOOL_FALSE ||
      layout->is_object == CTOOL_FALSE ||
      layout->is_complete_object == CTOOL_FALSE ||
      (layout->size != 1u && layout->size != 2u &&
       layout->size != 4u)) {
    return CTOOL_ERR_INTERNAL;
  }
  width_bits = (ctool_u16)(layout->size * 8u);
  register_class = layout->size == 1u
                       ? CTOOL_X86_REG_GPR8
                       : layout->size == 2u ? CTOOL_X86_REG_GPR16
                                            : CTOOL_X86_REG_GPR32;
  instruction = cemit_x86_instruction(mnemonic, width_bits);
  instruction.prefixes =
      locked == CTOOL_TRUE ? CTOOL_X86_PREFIX_LOCK : 0u;
  instruction.operand_count = 2u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 0u), 0, 0u);
  instruction.operands[0].width_bits = width_bits;
  instruction.operands[1] =
      cemit_x86_register_operand(register_class, 1u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_load_atomic_eax_at_edx(
    cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_layout_t *layout;
  ctool_x86_mnemonic_t mnemonic;
  ctool_x86_instruction_t instruction;
  ctool_u16 width_bits;
  if (type >= context->unit->layout.type_count) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->is_integer == CTOOL_FALSE ||
      layout->is_object == CTOOL_FALSE ||
      layout->is_complete_object == CTOOL_FALSE ||
      (layout->size != 1u && layout->size != 2u &&
       layout->size != 4u)) {
    return CTOOL_ERR_INTERNAL;
  }
  width_bits = (ctool_u16)(layout->size * 8u);
  mnemonic = layout->size == 4u
                 ? CTOOL_X86_MN_MOV
                 : layout->is_signed == CTOOL_TRUE ? CTOOL_X86_MN_MOVSX
                                                    : CTOOL_X86_MN_MOVZX;
  instruction = cemit_x86_instruction(mnemonic, 32u);
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, 0u);
  instruction.operands[1] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 2u), 0, 0u);
  instruction.operands[1].width_bits = width_bits;
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_atomic_cmpxchg_edx_ebx(
    cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_layout_t *layout;
  ctool_x86_reg_class_t register_class;
  ctool_x86_instruction_t instruction;
  ctool_u16 width_bits;
  if (type >= context->unit->layout.type_count) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->is_integer == CTOOL_FALSE ||
      layout->is_object == CTOOL_FALSE ||
      layout->is_complete_object == CTOOL_FALSE ||
      (layout->size != 1u && layout->size != 2u &&
       layout->size != 4u)) {
    return CTOOL_ERR_INTERNAL;
  }
  width_bits = (ctool_u16)(layout->size * 8u);
  register_class = layout->size == 1u
                       ? CTOOL_X86_REG_GPR8
                       : layout->size == 2u ? CTOOL_X86_REG_GPR16
                                            : CTOOL_X86_REG_GPR32;
  instruction = cemit_x86_instruction(
      CTOOL_X86_MN_CMPXCHG, width_bits);
  instruction.prefixes = CTOOL_X86_PREFIX_LOCK;
  instruction.operand_count = 2u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 2u), 0, 0u);
  instruction.operands[0].width_bits = width_bits;
  instruction.operands[1] =
      cemit_x86_register_operand(register_class, 3u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_canonicalize_eax_lane(
    cemit_context_t *context, ctool_u32 type);

static ctool_status_t cemit_emit_atomic_fetch_or(
    cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_layout_t *layout;
  ctool_x86_reg_class_t register_class;
  ctool_u32 repeat_target;
  ctool_u32 repeat_patch = CTOOL_C_AST_NONE;
  ctool_u32 repeat_after = CTOOL_C_AST_NONE;
  ctool_status_t status;
  if (type >= context->unit->layout.type_count) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->is_integer == CTOOL_FALSE ||
      layout->is_object == CTOOL_FALSE ||
      layout->is_complete_object == CTOOL_FALSE ||
      (layout->size != 1u && layout->size != 2u &&
       layout->size != 4u)) {
    return CTOOL_ERR_INTERNAL;
  }
  register_class = layout->size == 1u
                       ? CTOOL_X86_REG_GPR8
                       : layout->size == 2u ? CTOOL_X86_REG_GPR16
                                            : CTOOL_X86_REG_GPR32;
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 2u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 3u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_atomic_eax_at_edx(context, type);
  }
  repeat_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 3u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_OR, register_class, 3u,
        register_class, 1u, (ctool_u16)(layout->size * 8u));
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_atomic_cmpxchg_edx_ebx(context, type);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JNE, &repeat_patch, &repeat_after);
  }
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, repeat_patch, repeat_after, repeat_target);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 3u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_canonicalize_eax_lane(context, type);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_canonicalize_eax_lane(
    cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_layout_t *layout;
  ctool_x86_reg_class_t source_class;
  ctool_x86_mnemonic_t mnemonic;
  if (type >= context->unit->layout.type_count) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->is_integer == CTOOL_FALSE ||
      layout->is_object == CTOOL_FALSE ||
      layout->is_complete_object == CTOOL_FALSE ||
      (layout->size != 1u && layout->size != 2u && layout->size != 4u)) {
    return CTOOL_ERR_INTERNAL;
  }
  if (layout->size == 4u) {
    return CTOOL_OK;
  }
  source_class = layout->size == 1u ? CTOOL_X86_REG_GPR8
                                    : CTOOL_X86_REG_GPR16;
  mnemonic = layout->is_signed == CTOOL_TRUE ? CTOOL_X86_MN_MOVSX
                                              : CTOOL_X86_MN_MOVZX;
  return cemit_x86_two_registers(
      context, mnemonic, CTOOL_X86_REG_GPR32, 0u, source_class, 0u, 32u);
}

static ctool_status_t cemit_x86_convert_eax_to_integer(
    cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cemit_unwrapped_type(context, type);
  ctool_status_t status;
  if (node == (const ctool_c_type_node_t *)0) {
    return CTOOL_ERR_INTERNAL;
  }
  if (node->kind != CTOOL_C_TYPE_BOOL) {
    return cemit_x86_canonicalize_eax_lane(context, type);
  }
  status = cemit_x86_two_registers(
      context, CTOOL_X86_MN_TEST, CTOOL_X86_REG_GPR32, 0u,
      CTOOL_X86_REG_GPR32, 0u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_SETNE, CTOOL_X86_REG_GPR8, 0u, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOVZX, CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_GPR8, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_canonicalize_scalar_eax(
    cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_layout_t *layout;
  if (type >= context->unit->layout.type_count) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->is_integer == CTOOL_TRUE) {
    return cemit_x86_canonicalize_eax_lane(context, type);
  }
  return layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size == 4u
             ? CTOOL_OK
             : CTOOL_ERR_INTERNAL;
}

static ctool_status_t cemit_x86_add_register_constant(
    cemit_context_t *context, ctool_u8 register_index,
    ctool_u32 value) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_ADD, 32u);
  if (value == 0u) {
    return CTOOL_OK;
  }
  if (register_index >= 8u) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(
          CTOOL_X86_REG_GPR32, register_index);
  instruction.operands[1] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, value);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_add_eax_constant(
    cemit_context_t *context, ctool_u32 value) {
  return cemit_x86_add_register_constant(context, 0u, value);
}

static ctool_status_t cemit_x86_shift_register(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u8 register_index, ctool_u32 count) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, 32u);
  if (count >= 32u ||
      (mnemonic != CTOOL_X86_MN_SHL && mnemonic != CTOOL_X86_MN_SHR &&
       mnemonic != CTOOL_X86_MN_SAR && mnemonic != CTOOL_X86_MN_RCL &&
       mnemonic != CTOOL_X86_MN_RCR)) {
    return CTOOL_ERR_INTERNAL;
  }
  if (count == 0u) {
    return CTOOL_OK;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, register_index);
  instruction.operands[1] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 8u, 8u, count);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_shift_eax(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u32 count) {
  return cemit_x86_shift_register(context, mnemonic, 0u, count);
}

static ctool_status_t cemit_x86_and_register_constant(
    cemit_context_t *context, ctool_u8 register_index, ctool_u32 value) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_AND, 32u);
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, register_index);
  instruction.operands[1] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, value);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_store_eax_at_edx(
    cemit_context_t *context) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  instruction.operand_count = 2u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 2u), 0, 32u);
  instruction.operands[1] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, 0u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_load_stack(
    cemit_context_t *context, ctool_u8 register_index,
    ctool_u32 stack_offset) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  if (stack_offset > 0x7fffffffu) {
    return CTOOL_ERR_OVERFLOW;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, register_index);
  instruction.operands[1] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 4u),
      (ctool_i32)stack_offset, 0u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_store_stack(
    cemit_context_t *context, ctool_u32 stack_offset,
    ctool_u8 register_index) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  if (stack_offset > 0x7fffffffu) {
    return CTOOL_ERR_OVERFLOW;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 4u),
      (ctool_i32)stack_offset, 0u);
  instruction.operands[1] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, register_index);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_store_local_register(
    cemit_context_t *context, ctool_u32 local_offset,
    ctool_u32 byte_offset, ctool_u8 register_index) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  ctool_i32 displacement;
  if (local_offset == 0u || local_offset > 0x7fffffffu ||
      byte_offset > local_offset) {
    return CTOOL_ERR_INTERNAL;
  }
  displacement = 0 - (ctool_i32)local_offset + (ctool_i32)byte_offset;
  instruction.operand_count = 2u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 5u), displacement, 32u);
  instruction.operands[1] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, register_index);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_load_local_register(
    cemit_context_t *context, ctool_u8 register_index,
    ctool_u32 local_offset, ctool_u32 byte_offset) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  ctool_i32 displacement;
  if (local_offset == 0u || local_offset > 0x7fffffffu ||
      byte_offset > local_offset) {
    return CTOOL_ERR_INTERNAL;
  }
  displacement = 0 - (ctool_i32)local_offset + (ctool_i32)byte_offset;
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, register_index);
  instruction.operands[1] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, 5u), displacement, 32u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_load_register_at_register_encoding(
    cemit_context_t *context, ctool_u8 destination_register,
    ctool_u8 address_register, ctool_u32 byte_offset,
    ctool_u16 displacement_bits) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  if (byte_offset > 0x7fffffffu) {
    return CTOOL_ERR_OVERFLOW;
  }
  if (displacement_bits != 0u && displacement_bits != 32u) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] = cemit_x86_register_operand(
      CTOOL_X86_REG_GPR32, destination_register);
  instruction.operands[1] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, address_register),
      (ctool_i32)byte_offset, displacement_bits);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_load_register_at_register(
    cemit_context_t *context, ctool_u8 destination_register,
    ctool_u8 address_register, ctool_u32 byte_offset) {
  return cemit_x86_load_register_at_register_encoding(
      context, destination_register, address_register, byte_offset, 32u);
}

static ctool_status_t cemit_x86_load_register_at_register_compact(
    cemit_context_t *context, ctool_u8 destination_register,
    ctool_u8 address_register, ctool_u32 byte_offset) {
  return cemit_x86_load_register_at_register_encoding(
      context, destination_register, address_register, byte_offset, 0u);
}

static ctool_status_t cemit_x86_store_register_at_register_offset(
    cemit_context_t *context, ctool_u8 address_register,
    ctool_u32 byte_offset, ctool_u8 source_register) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  if (byte_offset > 0x7fffffffu) {
    return CTOOL_ERR_OVERFLOW;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, address_register),
      (ctool_i32)byte_offset, 0u);
  instruction.operands[1] = cemit_x86_register_operand(
      CTOOL_X86_REG_GPR32, source_register);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_store_register_at_register(
    cemit_context_t *context, ctool_u8 address_register,
    ctool_u8 source_register) {
  return cemit_x86_store_register_at_register_offset(
      context, address_register, 0u, source_register);
}

static ctool_status_t cemit_x86_binary_register_at_register(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u8 destination_register, ctool_u8 address_register,
    ctool_u32 byte_offset) {
  ctool_x86_instruction_t instruction = cemit_x86_instruction(mnemonic, 32u);
  if (byte_offset > 0x7fffffffu ||
      (mnemonic != CTOOL_X86_MN_ADC && mnemonic != CTOOL_X86_MN_ADD &&
       mnemonic != CTOOL_X86_MN_AND && mnemonic != CTOOL_X86_MN_CMP &&
       mnemonic != CTOOL_X86_MN_IMUL &&
       mnemonic != CTOOL_X86_MN_OR && mnemonic != CTOOL_X86_MN_SBB &&
       mnemonic != CTOOL_X86_MN_SUB && mnemonic != CTOOL_X86_MN_XOR)) {
    return CTOOL_ERR_INTERNAL;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] = cemit_x86_register_operand(
      CTOOL_X86_REG_GPR32, destination_register);
  instruction.operands[1] = cemit_x86_memory_operand(
      cemit_x86_register(CTOOL_X86_REG_GPR32, address_register),
      (ctool_i32)byte_offset, 32u);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_push_wide_constant_snapshot(
    cemit_context_t *context, ctool_u32 temporary_offset,
    ctool_u64 bits) {
  ctool_status_t status;
  if (temporary_offset < 8u || temporary_offset > 0x7fffffffu) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_move_register_constant(
      context, 0u, (ctool_u32)(bits & 0xffffffffu));
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, 0u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_register_constant(
        context, 0u, (ctool_u32)(bits >> 32u));
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, 4u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_lea_local(context, temporary_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_push_wide_result_snapshot(
    cemit_context_t *context, ctool_u32 temporary_offset) {
  ctool_status_t status;
  if (temporary_offset < 8u || temporary_offset > 0x7fffffffu) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_store_local_register(
      context, temporary_offset, 0u, 0u);
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, 4u, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_lea_local(context, temporary_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_push_widened_integer_snapshot(
    cemit_context_t *context, ctool_u32 temporary_offset,
    ctool_bool source_signed) {
  ctool_status_t status;
  if (temporary_offset < 8u || temporary_offset > 0x7fffffffu ||
      (source_signed != CTOOL_FALSE && source_signed != CTOOL_TRUE)) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 2u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK && source_signed == CTOOL_TRUE) {
    status = cemit_x86_shift_register(
        context, CTOOL_X86_MN_SAR, 2u, 31u);
  } else if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 2u,
        CTOOL_X86_REG_GPR32, 2u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, 0u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, 4u, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_lea_local(context, temporary_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_push_narrowed_wide_integer(
    cemit_context_t *context, ctool_u32 target_type,
    ctool_bool target_boolean) {
  ctool_status_t status;
  if (target_boolean != CTOOL_FALSE && target_boolean != CTOOL_TRUE) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 0u, 1u, 0u);
  }
  if (status == CTOOL_OK && target_boolean == CTOOL_TRUE) {
    status = cemit_x86_load_register_at_register(
        context, 2u, 1u, 4u);
  }
  if (status == CTOOL_OK && target_boolean == CTOOL_TRUE) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_OR, CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_GPR32, 2u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_convert_eax_to_integer(context, target_type);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_push_wide_register_snapshot(
    cemit_context_t *context, ctool_u32 temporary_offset,
    ctool_u8 low_register, ctool_u8 high_register) {
  ctool_status_t status;
  if (temporary_offset < 8u || temporary_offset > 0x7fffffffu) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_store_local_register(
      context, temporary_offset, 0u, low_register);
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, 4u, high_register);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_lea_local(context, temporary_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_push_wide_bitwise_snapshot(
    cemit_context_t *context, ctool_u32 temporary_offset,
    ctool_x86_mnemonic_t mnemonic) {
  ctool_status_t status;
  if (mnemonic != CTOOL_X86_MN_AND && mnemonic != CTOOL_X86_MN_OR &&
      mnemonic != CTOOL_X86_MN_XOR) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 2u, 0u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, mnemonic, 2u, 1u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, 0u, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 2u, 0u, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, mnemonic, 2u, 1u, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, 4u, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_lea_local(context, temporary_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_push_wide_add_subtract_snapshot(
    cemit_context_t *context, ctool_u32 temporary_offset,
    ctool_bool subtract) {
  ctool_x86_mnemonic_t low_mnemonic;
  ctool_x86_mnemonic_t high_mnemonic;
  ctool_status_t status;
  if (subtract != CTOOL_FALSE && subtract != CTOOL_TRUE) {
    return CTOOL_ERR_INTERNAL;
  }
  low_mnemonic = subtract == CTOOL_TRUE ? CTOOL_X86_MN_SUB
                                        : CTOOL_X86_MN_ADD;
  high_mnemonic = subtract == CTOOL_TRUE ? CTOOL_X86_MN_SBB
                                         : CTOOL_X86_MN_ADC;
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 2u, 0u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, low_mnemonic, 2u, 1u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, 0u, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 2u, 0u, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, high_mnemonic, 2u, 1u, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, 4u, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_lea_local(context, temporary_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_push_wide_multiply_snapshot(
    cemit_context_t *context, ctool_u32 temporary_offset) {
  ctool_status_t status;
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 2u, 0u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, CTOOL_X86_MN_IMUL, 2u, 1u, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 2u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 2u, 0u, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, CTOOL_X86_MN_IMUL, 2u, 1u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, CTOOL_X86_MN_ADD, 2u, 4u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(context, 0u, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 0u, 0u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 1u, 1u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_MUL, CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, CTOOL_X86_MN_ADD, 2u, 4u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_push_wide_register_snapshot(
        context, temporary_offset, 0u, 2u);
  }
  return status;
}

static ctool_status_t cemit_x86_absolute_wide_stack_value(
    cemit_context_t *context, ctool_u32 low_offset) {
  ctool_status_t status;
  if (low_offset > CEMIT_WIDE_DIVIDE_STACK_SIZE - 8u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_load_stack(context, 0u, low_offset);
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(context, 2u, low_offset + 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 1u,
        CTOOL_X86_REG_GPR32, 2u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_shift_register(
        context, CTOOL_X86_MN_SAR, 1u, 31u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 2u,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_SUB, CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_SBB, CTOOL_X86_REG_GPR32, 2u,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(context, low_offset, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(context, low_offset + 4u, 2u);
  }
  return status;
}

static ctool_status_t cemit_x86_push_wide_divide_remainder_snapshot(
    cemit_context_t *context, ctool_u32 temporary_offset,
    ctool_bool value_signed, ctool_bool remainder) {
  ctool_u32 greater_patch = CTOOL_C_AST_NONE;
  ctool_u32 greater_after = CTOOL_C_AST_NONE;
  ctool_u32 overflow_patch = CTOOL_C_AST_NONE;
  ctool_u32 overflow_after = CTOOL_C_AST_NONE;
  ctool_u32 high_less_patch = CTOOL_C_AST_NONE;
  ctool_u32 high_less_after = CTOOL_C_AST_NONE;
  ctool_u32 low_less_patch = CTOOL_C_AST_NONE;
  ctool_u32 low_less_after = CTOOL_C_AST_NONE;
  ctool_u32 repeat_patch = CTOOL_C_AST_NONE;
  ctool_u32 repeat_after = CTOOL_C_AST_NONE;
  ctool_u32 repeat_target;
  ctool_u32 subtract_target;
  ctool_u32 continue_target;
  ctool_u32 result_low_offset;
  ctool_u32 result_high_offset;
  ctool_u32 result_sign_offset;
  ctool_status_t status;
  if ((value_signed != CTOOL_FALSE && value_signed != CTOOL_TRUE) ||
      (remainder != CTOOL_FALSE && remainder != CTOOL_TRUE)) {
    return CTOOL_ERR_INTERNAL;
  }
  result_low_offset = remainder == CTOOL_TRUE
                          ? CEMIT_WIDE_REMAINDER_LOW_STACK
                          : CEMIT_WIDE_QUOTIENT_LOW_STACK;
  result_high_offset = remainder == CTOOL_TRUE
                           ? CEMIT_WIDE_REMAINDER_HIGH_STACK
                           : CEMIT_WIDE_QUOTIENT_HIGH_STACK;
  result_sign_offset = remainder == CTOOL_TRUE
                           ? CEMIT_WIDE_REMAINDER_SIGN_STACK
                           : CEMIT_WIDE_QUOTIENT_SIGN_STACK;

  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_reserve_locals(
        context, CEMIT_WIDE_DIVIDE_STACK_SIZE);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(context, 2u, 0u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_DIVIDEND_LOW_STACK, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(context, 2u, 0u, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_DIVIDEND_HIGH_STACK, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(context, 2u, 1u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_DIVISOR_LOW_STACK, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(context, 2u, 1u, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_DIVISOR_HIGH_STACK, 2u);
  }

  if (status == CTOOL_OK && value_signed == CTOOL_TRUE) {
    status = cemit_x86_load_stack(
        context, 2u, CEMIT_WIDE_DIVIDEND_HIGH_STACK);
    if (status == CTOOL_OK) {
      status = cemit_x86_load_stack(
          context, 0u, CEMIT_WIDE_DIVISOR_HIGH_STACK);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 2u,
          CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_stack(
          context, CEMIT_WIDE_QUOTIENT_SIGN_STACK, 2u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_load_stack(
          context, 2u, CEMIT_WIDE_DIVIDEND_HIGH_STACK);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_stack(
          context, CEMIT_WIDE_REMAINDER_SIGN_STACK, 2u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_absolute_wide_stack_value(
          context, CEMIT_WIDE_DIVIDEND_LOW_STACK);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_absolute_wide_stack_value(
          context, CEMIT_WIDE_DIVISOR_LOW_STACK);
    }
  } else if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 2u,
        CTOOL_X86_REG_GPR32, 2u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_store_stack(
          context, CEMIT_WIDE_QUOTIENT_SIGN_STACK, 2u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_stack(
          context, CEMIT_WIDE_REMAINDER_SIGN_STACK, 2u);
    }
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 2u,
        CTOOL_X86_REG_GPR32, 2u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_QUOTIENT_LOW_STACK, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_QUOTIENT_HIGH_STACK, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_REMAINDER_LOW_STACK, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_REMAINDER_HIGH_STACK, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_register_constant(context, 1u, 64u);
  }

  repeat_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(
        context, 0u, CEMIT_WIDE_QUOTIENT_LOW_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(
        context, 2u, CEMIT_WIDE_QUOTIENT_HIGH_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_shift_register(
        context, CTOOL_X86_MN_SHL, 0u, 1u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_shift_register(
        context, CTOOL_X86_MN_RCL, 2u, 1u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_QUOTIENT_LOW_STACK, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_QUOTIENT_HIGH_STACK, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(
        context, 0u, CEMIT_WIDE_DIVIDEND_LOW_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(
        context, 2u, CEMIT_WIDE_DIVIDEND_HIGH_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_shift_register(
        context, CTOOL_X86_MN_SHL, 0u, 1u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_shift_register(
        context, CTOOL_X86_MN_RCL, 2u, 1u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_DIVIDEND_LOW_STACK, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_DIVIDEND_HIGH_STACK, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(
        context, 0u, CEMIT_WIDE_REMAINDER_LOW_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(
        context, 2u, CEMIT_WIDE_REMAINDER_HIGH_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_shift_register(
        context, CTOOL_X86_MN_RCL, 0u, 1u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_shift_register(
        context, CTOOL_X86_MN_RCL, 2u, 1u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_REMAINDER_LOW_STACK, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_REMAINDER_HIGH_STACK, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JB, &overflow_patch, &overflow_after);
  }

  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(
        context, 2u, CEMIT_WIDE_REMAINDER_HIGH_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, CTOOL_X86_MN_CMP, 2u, 4u,
        CEMIT_WIDE_DIVISOR_HIGH_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JA, &greater_patch, &greater_after);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JB, &high_less_patch,
        &high_less_after);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(
        context, 0u, CEMIT_WIDE_REMAINDER_LOW_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, CTOOL_X86_MN_CMP, 0u, 4u,
        CEMIT_WIDE_DIVISOR_LOW_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JB, &low_less_patch, &low_less_after);
  }

  subtract_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, overflow_patch, overflow_after,
        subtract_target);
  }
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, greater_patch, greater_after,
        subtract_target);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(
        context, 0u, CEMIT_WIDE_REMAINDER_LOW_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(
        context, 2u, CEMIT_WIDE_REMAINDER_HIGH_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, CTOOL_X86_MN_SUB, 0u, 4u,
        CEMIT_WIDE_DIVISOR_LOW_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, CTOOL_X86_MN_SBB, 2u, 4u,
        CEMIT_WIDE_DIVISOR_HIGH_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_REMAINDER_LOW_STACK, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_REMAINDER_HIGH_STACK, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(
        context, 0u, CEMIT_WIDE_QUOTIENT_LOW_STACK);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_INC, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_stack(
        context, CEMIT_WIDE_QUOTIENT_LOW_STACK, 0u);
  }

  continue_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, high_less_patch, high_less_after,
        continue_target);
  }
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, low_less_patch, low_less_after,
        continue_target);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_DEC, CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JNE, &repeat_patch, &repeat_after);
  }
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, repeat_patch, repeat_after, repeat_target);
  }

  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(context, 0u, result_low_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(context, 2u, result_high_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(context, 1u, result_sign_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_shift_register(
        context, CTOOL_X86_MN_SAR, 1u, 31u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 2u,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_SUB, CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_SBB, CTOOL_X86_REG_GPR32, 2u,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_lea_stack(
        context, 4u, CEMIT_WIDE_DIVIDE_STACK_SIZE);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_push_wide_register_snapshot(
        context, temporary_offset, 0u, 2u);
  }
  return status;
}

static ctool_status_t cemit_x86_push_wide_unary_snapshot(
    cemit_context_t *context, ctool_u32 temporary_offset,
    ctool_c_expression_operator_t operation) {
  ctool_status_t status;
  if (operation != CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE &&
      operation != CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 0u, 1u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 2u, 1u, 4u);
  }
  if (status == CTOOL_OK &&
      operation == CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE) {
    status = cemit_x86_move_register_constant(context, 1u, 0u);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_NEG, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_ADC, CTOOL_X86_REG_GPR32, 2u,
          CTOOL_X86_REG_GPR32, 1u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_NEG, CTOOL_X86_REG_GPR32, 2u, 32u);
    }
  } else if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_NOT, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_NOT, CTOOL_X86_REG_GPR32, 2u, 32u);
    }
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_push_wide_register_snapshot(
        context, temporary_offset, 0u, 2u);
  }
  return status;
}

static ctool_status_t cemit_x86_push_wide_shift_snapshot(
    cemit_context_t *context, ctool_u32 temporary_offset,
    ctool_bool shift_left, ctool_bool value_signed) {
  ctool_u32 done_patch = CTOOL_C_AST_NONE;
  ctool_u32 done_after = CTOOL_C_AST_NONE;
  ctool_u32 repeat_patch = CTOOL_C_AST_NONE;
  ctool_u32 repeat_after = CTOOL_C_AST_NONE;
  ctool_u32 repeat_target;
  ctool_u32 done_target;
  ctool_status_t status;
  if ((shift_left != CTOOL_FALSE && shift_left != CTOOL_TRUE) ||
      (value_signed != CTOOL_FALSE && value_signed != CTOOL_TRUE)) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_and_register_constant(context, 1u, 63u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 2u, 0u, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 0u, 0u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_TEST, CTOOL_X86_REG_GPR32, 1u,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JE, &done_patch, &done_after);
  }
  repeat_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK && shift_left == CTOOL_TRUE) {
    status = cemit_x86_shift_register(
        context, CTOOL_X86_MN_SHL, 0u, 1u);
    if (status == CTOOL_OK) {
      status = cemit_x86_shift_register(
          context, CTOOL_X86_MN_RCL, 2u, 1u);
    }
  } else if (status == CTOOL_OK) {
    status = cemit_x86_shift_register(
        context,
        value_signed == CTOOL_TRUE ? CTOOL_X86_MN_SAR
                                   : CTOOL_X86_MN_SHR,
        2u, 1u);
    if (status == CTOOL_OK) {
      status = cemit_x86_shift_register(
          context, CTOOL_X86_MN_RCR, 0u, 1u);
    }
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_DEC, CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JNE, &repeat_patch, &repeat_after);
  }
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, repeat_patch, repeat_after, repeat_target);
  }
  done_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, done_patch, done_after, done_target);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_push_wide_register_snapshot(
        context, temporary_offset, 0u, 2u);
  }
  return status;
}

static ctool_status_t cemit_x86_pop_wide_result(
    cemit_context_t *context) {
  ctool_status_t status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 0u, 1u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(
        context, 2u, 1u, 4u);
  }
  return status;
}

static ctool_status_t cemit_x86_discard_arguments(
    cemit_context_t *context, ctool_u32 byte_count) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_ADD, 32u);
  if (byte_count == 0u) {
    return CTOOL_OK;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, 4u);
  instruction.operands[1] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, byte_count);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_call_stack_padding(
    ctool_u32 stack_base_residue, ctool_u32 frame_size,
    ctool_u32 stack_depth,
    ctool_u32 reserved_bytes, ctool_u32 *padding_out) {
  ctool_u32 stack_bytes;
  ctool_u32 residue;
  if (padding_out == (ctool_u32 *)0 ||
      stack_base_residue > 15u ||
      (stack_base_residue & 3u) != 0u ||
      (frame_size & 3u) != 0u ||
      (reserved_bytes & 3u) != 0u ||
      cemit_multiply_overflows(stack_depth, 4u) == CTOOL_TRUE) {
    return CTOOL_ERR_INTERNAL;
  }
  stack_bytes = stack_depth * 4u;
  residue = (stack_base_residue + 16u - (frame_size & 15u) -
             (stack_bytes & 15u)) &
            15u;
  *padding_out = (residue + 16u - (reserved_bytes & 15u)) & 15u;
  return (*padding_out & 3u) == 0u ? CTOOL_OK : CTOOL_ERR_INTERNAL;
}

static ctool_status_t cemit_x86_shift_call_arguments(
    cemit_context_t *context, ctool_u32 argument_bytes,
    ctool_u32 padding) {
  ctool_u32 offset;
  ctool_status_t status;
  if ((argument_bytes & 3u) != 0u || (padding & 3u) != 0u ||
      padding > 12u) {
    return CTOOL_ERR_INTERNAL;
  }
  if (padding == 0u) {
    return CTOOL_OK;
  }
  status = cemit_x86_reserve_locals(context, padding);
  for (offset = 0u; status == CTOOL_OK && offset < argument_bytes;
       offset += 4u) {
    if (cemit_add_overflows(padding, offset) == CTOOL_TRUE) {
      return CTOOL_ERR_OVERFLOW;
    }
    status = cemit_x86_load_stack(context, 1u, padding + offset);
    if (status == CTOOL_OK) {
      status = cemit_x86_store_stack(context, offset, 1u);
    }
  }
  return status;
}

static ctool_status_t cemit_x86_zero_stack_area(
    cemit_context_t *context, ctool_u32 byte_count) {
  ctool_status_t status;
  if (byte_count == 0u) {
    return CTOOL_OK;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 7u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_lea_stack(context, 7u, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_register_constant(context, 1u, byte_count);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_CLD);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_repeat_string(context, CTOOL_X86_MN_STOSB, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 7u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_copy_edx_to_eax(
    cemit_context_t *context, ctool_u32 byte_count) {
  ctool_status_t status;
  if (byte_count == 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 6u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 7u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 6u,
        CTOOL_X86_REG_GPR32, 2u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 7u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_register_constant(context, 1u, byte_count);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_CLD);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_repeat_string(context, CTOOL_X86_MN_MOVSB, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 7u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 6u, 32u);
  }
  return status;
}

/* EAX holds the current cursor and EDX holds the cursor object's address. */
static ctool_status_t cemit_x86_push_wide_variadic_snapshot(
    cemit_context_t *context, ctool_u32 temporary_offset,
    ctool_u32 cursor_type) {
  ctool_status_t status;
  if (temporary_offset < 8u || temporary_offset > 0x7fffffffu ||
      (temporary_offset & 3u) != 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 2u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 2u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_lea_local(context, temporary_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_copy_edx_to_eax(context, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_add_eax_constant(context, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 1u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_ecx_at_eax(context, cursor_type);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_lea_local(context, temporary_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_return_and_pop(
    cemit_context_t *context, ctool_u32 byte_count) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_RET, 32u);
  if (byte_count > 0xffffu) {
    return CTOOL_ERR_OVERFLOW;
  }
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 16u, 16u, byte_count);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_call_symbol(
    cemit_context_t *context, ctool_u32 symbol) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_CALL, 32u);
  ctool_x86_encoding_t encoding;
  ctool_u32 offset;
  ctool_u32 relocation_offset;
  ctool_status_t status;
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_RELATIVE, 32u, 32u, 0u);
  instruction.operands[0].as.value.kind = CTOOL_X86_VALUE_REFERENCE;
  instruction.operands[0].as.value.reference = symbol;
  status = cemit_x86_encode(context, &instruction, &encoding, &offset);
  if (status != CTOOL_OK) {
    return status;
  }
  if (encoding.field_count != 1u ||
      encoding.fields[0].kind != CTOOL_X86_FIELD_RELATIVE ||
      encoding.fields[0].relocation != CTOOL_X86_RELOC_PC_RELATIVE ||
      encoding.fields[0].byte_width != 4u ||
      encoding.fields[0].pc_bias != 4u ||
      encoding.fields[0].reference != symbol ||
      encoding.fields[0].encoded_addend != -4 ||
      encoding.size < 4u ||
      encoding.fields[0].byte_offset > encoding.size - 4u ||
      cemit_add_overflows(offset, encoding.fields[0].byte_offset) ==
          CTOOL_TRUE) {
    return CTOOL_ERR_INTERNAL;
  }
  relocation_offset = offset + encoding.fields[0].byte_offset;
  return cemit_add_relocation(
      context, context->active_text_section, relocation_offset, symbol,
      CTOOL_ELF32_R_386_PC32, encoding.fields[0].encoded_addend);
}

static ctool_status_t cemit_x86_call_register(
    cemit_context_t *context, ctool_u8 register_index) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_CALL, 32u);
  instruction.operand_count = 1u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, register_index);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_jump_register(
    cemit_context_t *context, ctool_u8 register_index) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_JMP, 32u);
  instruction.operand_count = 1u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, register_index);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
}

static ctool_status_t cemit_x86_push_symbol(
    cemit_context_t *context, ctool_u32 symbol) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_PUSH, 32u);
  ctool_x86_encoding_t encoding;
  ctool_u32 offset;
  ctool_u32 relocation_offset;
  ctool_status_t status;
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u, 0u);
  instruction.operands[0].as.value.kind = CTOOL_X86_VALUE_REFERENCE;
  instruction.operands[0].as.value.reference = symbol;
  status = cemit_x86_encode(context, &instruction, &encoding, &offset);
  if (status != CTOOL_OK) {
    return status;
  }
  if (encoding.size != 5u || encoding.field_count != 1u ||
      encoding.fields[0].kind != CTOOL_X86_FIELD_IMMEDIATE ||
      encoding.fields[0].relocation != CTOOL_X86_RELOC_ABSOLUTE ||
      encoding.fields[0].byte_offset != 1u ||
      encoding.fields[0].byte_width != 4u ||
      encoding.fields[0].pc_bias != 0u ||
      encoding.fields[0].reference != symbol ||
      encoding.fields[0].encoded_addend != 0 ||
      cemit_add_overflows(offset, encoding.fields[0].byte_offset) ==
          CTOOL_TRUE) {
    return CTOOL_ERR_INTERNAL;
  }
  relocation_offset = offset + encoding.fields[0].byte_offset;
  return cemit_add_relocation(context, context->active_text_section,
                              relocation_offset, symbol,
                              CTOOL_ELF32_R_386_32, 0);
}

static ctool_status_t cemit_x86_branch(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u32 *patch_out, ctool_u32 *after_out) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, 32u);
  ctool_x86_encoding_t encoding;
  ctool_u32 offset;
  ctool_status_t status;
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_RELATIVE, 32u, 32u, 0u);
  status = cemit_x86_encode(context, &instruction, &encoding, &offset);
  if (status != CTOOL_OK) {
    return status;
  }
  if (encoding.field_count != 1u ||
      encoding.fields[0].kind != CTOOL_X86_FIELD_RELATIVE ||
      encoding.fields[0].byte_width != 4u ||
      encoding.size < 4u ||
      encoding.fields[0].byte_offset > encoding.size - 4u) {
    return CTOOL_ERR_INTERNAL;
  }
  *patch_out = offset + encoding.fields[0].byte_offset;
  *after_out = offset + encoding.size;
  return CTOOL_OK;
}

static ctool_status_t cemit_x86_short_branch(
    cemit_context_t *context, ctool_x86_mnemonic_t mnemonic,
    ctool_u32 *patch_out, ctool_u32 *after_out) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(mnemonic, 32u);
  ctool_x86_encoding_t encoding;
  ctool_u32 offset;
  ctool_status_t status;
  instruction.operand_count = 1u;
  instruction.operands[0] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_RELATIVE, 32u, 8u, 0u);
  status = cemit_x86_encode(context, &instruction, &encoding, &offset);
  if (status != CTOOL_OK) {
    return status;
  }
  if (encoding.field_count != 1u ||
      encoding.fields[0].kind != CTOOL_X86_FIELD_RELATIVE ||
      encoding.fields[0].byte_width != 1u ||
      encoding.fields[0].byte_offset >= encoding.size) {
    return CTOOL_ERR_INTERNAL;
  }
  *patch_out = offset + encoding.fields[0].byte_offset;
  *after_out = offset + encoding.size;
  return CTOOL_OK;
}

static ctool_bool cemit_ir_type_is_i32_integer(
    const cemit_context_t *context, ctool_u32 type) {
  return type < context->unit->layout.type_count &&
                 context->unit->layout.types[type].is_integer ==
                     CTOOL_TRUE &&
                 context->unit->layout.types[type].size == 4u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_represented_integer(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_layout_t *layout;
  if (type >= context->unit->layout.type_count) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[type];
  return layout->is_integer == CTOOL_TRUE &&
                 layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 (layout->size == 1u || layout->size == 2u ||
                  layout->size == 4u)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_wide_integer(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_layout_t *layout;
  if (type >= context->unit->layout.type_count) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[type];
  return layout->is_integer == CTOOL_TRUE &&
                 layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size == 8u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_value_integer(
    const cemit_context_t *context, ctool_u32 type) {
  return cemit_ir_type_is_represented_integer(context, type) == CTOOL_TRUE ||
                 cemit_ir_type_is_wide_integer(context, type) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_i32_pointer(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cemit_unwrapped_type(context, type);
  const ctool_c_type_node_t *referent =
      node != (const ctool_c_type_node_t *)0 &&
              node->kind == CTOOL_C_TYPE_POINTER
          ? cemit_unwrapped_type(context, node->referenced_type)
          : (const ctool_c_type_node_t *)0;
  return type < context->unit->layout.type_count &&
                 node != (const ctool_c_type_node_t *)0 &&
                 node->kind == CTOOL_C_TYPE_POINTER &&
                 node->referenced_type < context->unit->layout.type_count &&
                 referent != (const ctool_c_type_node_t *)0 &&
                 (context->unit->layout.types[node->referenced_type]
                              .is_object == CTOOL_TRUE ||
                  referent->kind == CTOOL_C_TYPE_VOID) &&
                 context->unit->layout.types[type].is_object == CTOOL_TRUE &&
                 context->unit->layout.types[type].is_complete_object ==
                     CTOOL_TRUE &&
                 context->unit->layout.types[type].size == 4u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_i32_function_pointer(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cemit_unwrapped_type(context, type);
  const ctool_c_type_node_t *referent =
      node != (const ctool_c_type_node_t *)0 &&
              node->kind == CTOOL_C_TYPE_POINTER
          ? cemit_unwrapped_type(context, node->referenced_type)
          : (const ctool_c_type_node_t *)0;
  return type < context->unit->layout.type_count &&
                 node != (const ctool_c_type_node_t *)0 &&
                 node->kind == CTOOL_C_TYPE_POINTER &&
                 referent != (const ctool_c_type_node_t *)0 &&
                 referent->kind == CTOOL_C_TYPE_FUNCTION &&
                 context->unit->layout.types[type].is_object == CTOOL_TRUE &&
                 context->unit->layout.types[type].is_complete_object ==
                     CTOOL_TRUE &&
                 context->unit->layout.types[type].size == 4u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_function_pointer_cast_is_valid(
    const cemit_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type) {
  ctool_bool source_is_function_pointer =
      cemit_ir_type_is_i32_function_pointer(context, source_type);
  ctool_bool target_is_function_pointer =
      cemit_ir_type_is_i32_function_pointer(context, target_type);
  return (source_is_function_pointer == CTOOL_TRUE &&
          (target_is_function_pointer == CTOOL_TRUE ||
           cemit_ir_type_is_i32_integer(context, target_type) ==
               CTOOL_TRUE)) ||
                 (target_is_function_pointer == CTOOL_TRUE &&
                  cemit_ir_type_is_i32_integer(context, source_type) ==
                      CTOOL_TRUE)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_i32_pointer_value(
    const cemit_context_t *context, ctool_u32 type) {
  return cemit_ir_type_is_i32_pointer(context, type) == CTOOL_TRUE ||
                 cemit_ir_type_is_i32_function_pointer(context, type) ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_i32_void_pointer(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *pointer;
  const ctool_c_type_node_t *referent;
  ctool_u32 pointer_qualifiers;
  ctool_u32 referent_qualifiers;
  if (cemit_underlying_type(context, type, &pointer_qualifiers,
                            &pointer) == CTOOL_FALSE ||
      pointer->kind != CTOOL_C_TYPE_POINTER ||
      cemit_underlying_type(context, pointer->referenced_type,
                            &referent_qualifiers,
                            &referent) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  return cemit_ir_type_is_i32_pointer(context, type) == CTOOL_TRUE &&
                 (pointer_qualifiers & CTOOL_C_QUAL_ATOMIC) == 0u &&
                 referent->kind == CTOOL_C_TYPE_VOID &&
                 referent_qualifiers == 0u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_i32_scalar(
    const cemit_context_t *context, ctool_u32 type) {
  return cemit_ir_type_is_i32_integer(context, type) == CTOOL_TRUE ||
                 cemit_ir_type_is_i32_pointer_value(context, type) ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_floating_value(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cemit_unwrapped_type(context, type);
  const ctool_c_type_layout_t *layout;
  if (node == (const ctool_c_type_node_t *)0 ||
      type >= context->unit->layout.type_count ||
      (node->kind != CTOOL_C_TYPE_FLOAT &&
       node->kind != CTOOL_C_TYPE_DOUBLE)) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[type];
  return layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 ((node->kind == CTOOL_C_TYPE_FLOAT && layout->size == 4u) ||
                  (node->kind == CTOOL_C_TYPE_DOUBLE && layout->size == 8u))
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_represented_scalar(
    const cemit_context_t *context, ctool_u32 type) {
  return cemit_ir_type_is_represented_integer(context, type) == CTOOL_TRUE ||
                 cemit_ir_type_is_i32_pointer_value(context, type) ==
                     CTOOL_TRUE ||
                 (cemit_ir_type_is_floating_value(context, type) ==
                      CTOOL_TRUE &&
                  context->unit->layout.types[type].size == 4u)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_value_scalar(
    const cemit_context_t *context, ctool_u32 type) {
  return cemit_ir_type_is_represented_scalar(context, type) == CTOOL_TRUE ||
                 cemit_ir_type_is_wide_integer(context, type) == CTOOL_TRUE ||
                 cemit_ir_type_is_floating_value(context, type) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_truth_scalar(
    const cemit_context_t *context, ctool_u32 type) {
  return cemit_ir_type_is_value_integer(context, type) == CTOOL_TRUE ||
                 cemit_ir_type_is_i32_pointer_value(context, type) ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_variadic_cursor(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *pointer = cemit_unwrapped_type(context, type);
  const ctool_c_type_node_t *character =
      pointer != (const ctool_c_type_node_t *)0 &&
              pointer->kind == CTOOL_C_TYPE_POINTER
          ? cemit_unwrapped_type(context, pointer->referenced_type)
          : (const ctool_c_type_node_t *)0;
  return type < context->unit->layout.type_count &&
                 pointer != (const ctool_c_type_node_t *)0 &&
                 pointer->kind == CTOOL_C_TYPE_POINTER &&
                 pointer->referenced_type <
                     context->unit->layout.type_count &&
                 character != (const ctool_c_type_node_t *)0 &&
                 character->kind == CTOOL_C_TYPE_CHAR &&
                 character->qualifiers == 0u &&
                 context->unit->layout.types[type].is_object == CTOOL_TRUE &&
                 context->unit->layout.types[type].is_complete_object ==
                     CTOOL_TRUE &&
                 context->unit->layout.types[type].size == 4u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_variadic_argument(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cemit_unwrapped_type(context, type);
  const ctool_c_type_layout_t *layout;
  if (node == (const ctool_c_type_node_t *)0 ||
      type >= context->unit->layout.type_count) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[type];
  if (layout->is_object == CTOOL_FALSE ||
      layout->is_complete_object == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  if (node->kind == CTOOL_C_TYPE_POINTER) {
    return layout->size == 4u ? CTOOL_TRUE : CTOOL_FALSE;
  }
  if (node->kind == CTOOL_C_TYPE_DOUBLE) {
    return layout->size == 8u ? CTOOL_TRUE : CTOOL_FALSE;
  }
  return layout->is_integer == CTOOL_TRUE &&
                 (layout->size == 4u || layout->size == 8u) &&
                 (node->kind == CTOOL_C_TYPE_SIGNED_INT ||
                  node->kind == CTOOL_C_TYPE_UNSIGNED_INT ||
                  node->kind == CTOOL_C_TYPE_SIGNED_LONG ||
                  node->kind == CTOOL_C_TYPE_UNSIGNED_LONG ||
                  node->kind == CTOOL_C_TYPE_SIGNED_LONG_LONG ||
                  node->kind == CTOOL_C_TYPE_UNSIGNED_LONG_LONG ||
                  node->kind == CTOOL_C_TYPE_ENUM)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_pointer_arithmetic_size(
    const cemit_context_t *context, ctool_u32 type, ctool_u32 *size_out) {
  const ctool_c_type_node_t *pointer = cemit_unwrapped_type(context, type);
  if (size_out == (ctool_u32 *)0 ||
      cemit_ir_type_is_i32_pointer(context, type) == CTOOL_FALSE ||
      pointer == (const ctool_c_type_node_t *)0 ||
      pointer->referenced_type >= context->unit->layout.type_count ||
      context->unit->layout.types[pointer->referenced_type]
              .is_complete_object == CTOOL_FALSE ||
      context->unit->layout.types[pointer->referenced_type].size == 0u) {
    return CTOOL_FALSE;
  }
  *size_out = context->unit->layout.types[pointer->referenced_type].size;
  return CTOOL_TRUE;
}

static ctool_bool cemit_ir_relation_result(
    cemit_context_t *context, ctool_status_t status, ctool_bool result) {
  if (status != CTOOL_OK) {
    if (context->relation_status == CTOOL_OK) {
      context->relation_status = status;
    }
    return CTOOL_FALSE;
  }
  return result;
}

static ctool_bool cemit_ir_function_types_match(
    cemit_context_t *context, ctool_u32 left, ctool_u32 right) {
  ctool_bool compatible = CTOOL_FALSE;
  ctool_status_t status = ctool_c_ir_function_types_compatible(
      context->job, context->unit, left, right, &compatible);
  return cemit_ir_relation_result(context, status, compatible);
}

static ctool_bool cemit_ir_pointer_types_match(
    cemit_context_t *context, ctool_u32 object_type,
    ctool_u32 value_type) {
  ctool_bool compatible = CTOOL_FALSE;
  ctool_status_t status;
  if (cemit_ir_type_is_i32_pointer_value(context, object_type) ==
          CTOOL_FALSE ||
      cemit_ir_type_is_i32_pointer_value(context, value_type) ==
          CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  status = ctool_c_ir_pointer_value_types_compatible(
      context->job, context->unit, object_type, value_type, &compatible);
  return cemit_ir_relation_result(context, status, compatible);
}

static ctool_bool cemit_ir_scalar_types_match(
    cemit_context_t *context, ctool_u32 object_type,
    ctool_u32 value_type) {
  if (cemit_ir_type_is_value_integer(context, object_type) == CTOOL_TRUE &&
      cemit_ir_type_is_value_integer(context, value_type) == CTOOL_TRUE) {
    const ctool_c_type_node_t *object_node;
    const ctool_c_type_node_t *value_node;
    if (object_type == value_type) {
      return CTOOL_TRUE;
    }
    object_node = cemit_unwrapped_type(context, object_type);
    value_node = cemit_unwrapped_type(context, value_type);
    if (object_node == (const ctool_c_type_node_t *)0 ||
        value_node == (const ctool_c_type_node_t *)0 ||
        object_node->kind != value_node->kind ||
        (object_node->kind == CTOOL_C_TYPE_ENUM &&
         object_node != value_node)) {
      return CTOOL_FALSE;
    }
    return CTOOL_TRUE;
  }
  if (cemit_ir_type_is_floating_value(context, object_type) == CTOOL_TRUE &&
      cemit_ir_type_is_floating_value(context, value_type) == CTOOL_TRUE) {
    const ctool_c_type_node_t *object_node =
        cemit_unwrapped_type(context, object_type);
    const ctool_c_type_node_t *value_node =
        cemit_unwrapped_type(context, value_type);
    return object_node != (const ctool_c_type_node_t *)0 &&
                   value_node != (const ctool_c_type_node_t *)0 &&
                   object_node->kind == value_node->kind
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return cemit_ir_pointer_types_match(context, object_type, value_type);
}

static ctool_bool cemit_ir_narrow_integer_kind(
    ctool_c_type_kind_t kind) {
  return kind == CTOOL_C_TYPE_BOOL || kind == CTOOL_C_TYPE_CHAR ||
                 kind == CTOOL_C_TYPE_SIGNED_CHAR ||
                 kind == CTOOL_C_TYPE_UNSIGNED_CHAR ||
                 kind == CTOOL_C_TYPE_SIGNED_SHORT ||
                 kind == CTOOL_C_TYPE_UNSIGNED_SHORT
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_promoted_i32_integer_kind(
    ctool_c_type_kind_t kind) {
  return kind == CTOOL_C_TYPE_SIGNED_INT ||
                 kind == CTOOL_C_TYPE_UNSIGNED_INT ||
                 kind == CTOOL_C_TYPE_SIGNED_LONG ||
                 kind == CTOOL_C_TYPE_UNSIGNED_LONG
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_wide_standard_integer_kind(
    ctool_c_type_kind_t kind) {
  return kind == CTOOL_C_TYPE_SIGNED_LONG_LONG ||
                 kind == CTOOL_C_TYPE_UNSIGNED_LONG_LONG
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_integer_promotion_is_valid(
    const cemit_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type) {
  const ctool_c_type_node_t *source =
      cemit_unwrapped_type(context, source_type);
  const ctool_c_type_node_t *target =
      cemit_unwrapped_type(context, target_type);
  ctool_c_type_kind_t expected;
  if (source == (const ctool_c_type_node_t *)0 ||
      target == (const ctool_c_type_node_t *)0) {
    return CTOOL_FALSE;
  }
  if (source->kind == CTOOL_C_TYPE_ENUM) {
    const ctool_c_type_node_t *compatible =
        cemit_unwrapped_type(context, source->referenced_type);
    if (compatible == (const ctool_c_type_node_t *)0) {
      return CTOOL_FALSE;
    }
    expected =
        cemit_ir_narrow_integer_kind(compatible->kind) == CTOOL_TRUE
            ? CTOOL_C_TYPE_SIGNED_INT
            : compatible->kind;
  } else if (cemit_ir_narrow_integer_kind(source->kind) == CTOOL_TRUE) {
    expected = CTOOL_C_TYPE_SIGNED_INT;
  } else {
    return CTOOL_FALSE;
  }
  if (target->kind != expected) {
    return CTOOL_FALSE;
  }
  if (cemit_ir_type_is_i32_integer(context, target_type) == CTOOL_TRUE) {
    return cemit_ir_promoted_i32_integer_kind(expected);
  }
  return cemit_ir_type_is_wide_integer(context, target_type) == CTOOL_TRUE
             ? cemit_ir_wide_standard_integer_kind(expected)
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_usual_integer_conversion_is_valid(
    const cemit_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type) {
  const ctool_c_type_node_t *source =
      cemit_unwrapped_type(context, source_type);
  const ctool_c_type_node_t *target =
      cemit_unwrapped_type(context, target_type);
  if (source == (const ctool_c_type_node_t *)0 ||
      target == (const ctool_c_type_node_t *)0 ||
      cemit_ir_type_is_i32_integer(context, source_type) == CTOOL_FALSE ||
      cemit_ir_type_is_i32_integer(context, target_type) == CTOOL_FALSE ||
      cemit_ir_promoted_i32_integer_kind(source->kind) == CTOOL_FALSE ||
      cemit_ir_promoted_i32_integer_kind(target->kind) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  if (source->kind == CTOOL_C_TYPE_SIGNED_INT) {
    return target->kind == CTOOL_C_TYPE_UNSIGNED_INT ||
                   target->kind == CTOOL_C_TYPE_SIGNED_LONG ||
                   target->kind == CTOOL_C_TYPE_UNSIGNED_LONG
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (source->kind == CTOOL_C_TYPE_UNSIGNED_INT ||
      source->kind == CTOOL_C_TYPE_SIGNED_LONG) {
    return target->kind == CTOOL_C_TYPE_UNSIGNED_LONG ? CTOOL_TRUE
                                                       : CTOOL_FALSE;
  }
  return CTOOL_FALSE;
}

static ctool_bool cemit_ir_wide_usual_integer_conversion_is_valid(
    const cemit_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type) {
  const ctool_c_type_node_t *source =
      cemit_unwrapped_type(context, source_type);
  const ctool_c_type_node_t *target =
      cemit_unwrapped_type(context, target_type);
  if (source == (const ctool_c_type_node_t *)0 ||
      target == (const ctool_c_type_node_t *)0 ||
      cemit_ir_type_is_wide_integer(context, target_type) == CTOOL_FALSE ||
      cemit_ir_wide_standard_integer_kind(target->kind) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  if (cemit_ir_type_is_wide_integer(context, source_type) == CTOOL_TRUE) {
    return source->kind == CTOOL_C_TYPE_SIGNED_LONG_LONG &&
                   target->kind == CTOOL_C_TYPE_UNSIGNED_LONG_LONG
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return cemit_ir_type_is_i32_integer(context, source_type) == CTOOL_TRUE
             ? cemit_ir_promoted_i32_integer_kind(source->kind)
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_integer_conversion_is_valid(
    const cemit_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type, ctool_c_conversion_kind_t conversion) {
  const ctool_c_type_node_t *target;
  const ctool_c_type_node_t *source;
  ctool_bool source_wide =
      cemit_ir_type_is_wide_integer(context, source_type);
  ctool_bool target_wide =
      cemit_ir_type_is_wide_integer(context, target_type);
  if (source_wide == CTOOL_TRUE || target_wide == CTOOL_TRUE) {
    if (cemit_ir_type_is_value_integer(context, source_type) == CTOOL_FALSE ||
        cemit_ir_type_is_value_integer(context, target_type) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
    if (conversion == CTOOL_C_CONVERSION_NONE ||
        conversion == CTOOL_C_CONVERSION_ASSIGNMENT) {
      return CTOOL_TRUE;
    }
    if (conversion == CTOOL_C_CONVERSION_USUAL_ARITHMETIC) {
      return cemit_ir_wide_usual_integer_conversion_is_valid(
          context, source_type, target_type);
    }
    if (conversion == CTOOL_C_CONVERSION_INTEGER_PROMOTION) {
      return source_wide == CTOOL_TRUE && target_wide == CTOOL_TRUE
                 ? cemit_ir_integer_promotion_is_valid(
                       context, source_type, target_type)
                 : CTOOL_FALSE;
    }
    if (conversion != CTOOL_C_CONVERSION_QUALIFICATION ||
        source_wide == CTOOL_FALSE || target_wide == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
    source = cemit_unwrapped_type(context, source_type);
    target = cemit_unwrapped_type(context, target_type);
    return source != (const ctool_c_type_node_t *)0 &&
                   target != (const ctool_c_type_node_t *)0 &&
                   source->kind == target->kind
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (cemit_ir_type_is_represented_integer(context, source_type) ==
          CTOOL_FALSE ||
      cemit_ir_type_is_represented_integer(context, target_type) ==
          CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  if (conversion == CTOOL_C_CONVERSION_NONE ||
      conversion == CTOOL_C_CONVERSION_ASSIGNMENT) {
    return CTOOL_TRUE;
  }
  source = cemit_unwrapped_type(context, source_type);
  target = cemit_unwrapped_type(context, target_type);
  if (source == (const ctool_c_type_node_t *)0 ||
      target == (const ctool_c_type_node_t *)0) {
    return CTOOL_FALSE;
  }
  if (conversion == CTOOL_C_CONVERSION_QUALIFICATION) {
    return source->kind == target->kind &&
                   (source->kind != CTOOL_C_TYPE_ENUM || source == target)
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (conversion == CTOOL_C_CONVERSION_USUAL_ARITHMETIC) {
    return cemit_ir_usual_integer_conversion_is_valid(
        context, source_type, target_type);
  }
  return conversion == CTOOL_C_CONVERSION_INTEGER_PROMOTION
             ? cemit_ir_integer_promotion_is_valid(
                   context, source_type, target_type)
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_floating_conversion_is_valid(
    const cemit_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type, ctool_c_conversion_kind_t conversion) {
  const ctool_c_type_node_t *source =
      cemit_unwrapped_type(context, source_type);
  const ctool_c_type_node_t *target =
      cemit_unwrapped_type(context, target_type);
  ctool_bool source_floating;
  ctool_bool target_floating;
  if (source == (const ctool_c_type_node_t *)0 ||
      target == (const ctool_c_type_node_t *)0 ||
      source_type >= context->unit->layout.type_count ||
      target_type >= context->unit->layout.type_count) {
    return CTOOL_FALSE;
  }
  source_floating =
      cemit_ir_type_is_floating_value(context, source_type) == CTOOL_TRUE &&
              (source->kind == CTOOL_C_TYPE_FLOAT ||
               source->kind == CTOOL_C_TYPE_DOUBLE)
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  target_floating =
      cemit_ir_type_is_floating_value(context, target_type) == CTOOL_TRUE &&
              (target->kind == CTOOL_C_TYPE_FLOAT ||
               target->kind == CTOOL_C_TYPE_DOUBLE)
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  if (source_floating == CTOOL_TRUE &&
      target_floating == CTOOL_FALSE) {
    const ctool_c_type_layout_t *layout =
        &context->unit->layout.types[target_type];
    ctool_bool represented_conversion =
        cemit_ir_type_is_represented_integer(
            context, target_type) == CTOOL_TRUE &&
                target->kind != CTOOL_C_TYPE_BOOL &&
                (layout->is_signed == CTOOL_TRUE ||
                 layout->size < 4u) &&
                (conversion == CTOOL_C_CONVERSION_NONE ||
                 conversion == CTOOL_C_CONVERSION_ASSIGNMENT)
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool unsigned_wide_conversion =
        source->kind == CTOOL_C_TYPE_DOUBLE &&
                target->kind == CTOOL_C_TYPE_UNSIGNED_LONG_LONG &&
                cemit_ir_type_is_wide_integer(
                    context, target_type) == CTOOL_TRUE &&
                layout->is_signed == CTOOL_FALSE &&
                conversion == CTOOL_C_CONVERSION_NONE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    return represented_conversion == CTOOL_TRUE ||
                   unsigned_wide_conversion == CTOOL_TRUE
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (source_floating == CTOOL_FALSE &&
      target_floating == CTOOL_TRUE) {
    return cemit_ir_type_is_represented_integer(
               context, source_type) == CTOOL_TRUE &&
                   context->unit->layout.types[source_type].size <= 4u &&
                   (conversion == CTOOL_C_CONVERSION_NONE ||
                    conversion == CTOOL_C_CONVERSION_ASSIGNMENT ||
                    conversion ==
                        CTOOL_C_CONVERSION_USUAL_ARITHMETIC)
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (source_floating == CTOOL_FALSE ||
      target_floating == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  if (conversion == CTOOL_C_CONVERSION_FLOAT_PROMOTION) {
    return source->kind == CTOOL_C_TYPE_FLOAT &&
                   target->kind == CTOOL_C_TYPE_DOUBLE
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (conversion == CTOOL_C_CONVERSION_USUAL_ARITHMETIC) {
    return source->kind == target->kind ||
                   (source->kind == CTOOL_C_TYPE_FLOAT &&
                    target->kind == CTOOL_C_TYPE_DOUBLE)
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return conversion == CTOOL_C_CONVERSION_NONE ||
                 conversion == CTOOL_C_CONVERSION_ASSIGNMENT
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_pointer_conversion_is_valid(
    cemit_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type, ctool_c_conversion_kind_t conversion) {
  ctool_bool valid = CTOOL_FALSE;
  ctool_status_t status = ctool_c_ir_pointer_conversion_is_valid(
      context->job, context->unit, source_type, target_type, conversion,
      &valid);
  return cemit_ir_relation_result(context, status, valid);
}

static ctool_bool cemit_ir_pointer_comparison_types_match(
    cemit_context_t *context, ctool_u32 left_type, ctool_u32 right_type,
    ctool_bool require_object_referents) {
  ctool_bool compatible = CTOOL_FALSE;
  ctool_status_t status =
      ctool_c_ir_pointer_comparison_types_compatible(
          context->job, context->unit, left_type, right_type,
          require_object_referents, &compatible);
  return cemit_ir_relation_result(context, status, compatible);
}

static ctool_bool cemit_ir_pointer_arithmetic_types_match(
    cemit_context_t *context, ctool_u32 left_type, ctool_u32 right_type) {
  ctool_bool compatible = CTOOL_FALSE;
  ctool_status_t status = ctool_c_ir_pointer_arithmetic_types_compatible(
      context->job, context->unit, left_type, right_type, &compatible);
  return cemit_ir_relation_result(context, status, compatible);
}

static ctool_bool cemit_ir_array_decay_types_match(
    cemit_context_t *context, ctool_u32 array_type, ctool_u32 pointer_type) {
  ctool_bool compatible = CTOOL_FALSE;
  ctool_status_t status = ctool_c_ir_array_decay_types_compatible(
      context->job, context->unit, array_type, pointer_type, &compatible);
  return cemit_ir_relation_result(context, status, compatible);
}

static ctool_bool cemit_ir_type_is_plain_signed_int(
    const cemit_context_t *context, ctool_u32 type) {
  return type < context->unit->graph.type_count &&
                 context->unit->graph.types[type].kind ==
                     CTOOL_C_TYPE_SIGNED_INT &&
                 context->unit->graph.types[type].qualifiers == 0u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_x86_mnemonic_t cemit_comparison_predicate(
    ctool_c_expression_operator_t operation, ctool_bool is_signed) {
  if (operation == CTOOL_C_EXPRESSION_OPERATOR_EQUAL) {
    return CTOOL_X86_MN_SETE;
  }
  if (operation == CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL) {
    return CTOOL_X86_MN_SETNE;
  }
  if (operation == CTOOL_C_EXPRESSION_OPERATOR_LESS) {
    return is_signed == CTOOL_TRUE ? CTOOL_X86_MN_SETL
                                   : CTOOL_X86_MN_SETB;
  }
  if (operation == CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL) {
    return is_signed == CTOOL_TRUE ? CTOOL_X86_MN_SETLE
                                   : CTOOL_X86_MN_SETBE;
  }
  if (operation == CTOOL_C_EXPRESSION_OPERATOR_GREATER) {
    return is_signed == CTOOL_TRUE ? CTOOL_X86_MN_SETG
                                   : CTOOL_X86_MN_SETA;
  }
  return is_signed == CTOOL_TRUE ? CTOOL_X86_MN_SETGE
                                 : CTOOL_X86_MN_SETAE;
}

static ctool_status_t cemit_x86_push_wide_comparison(
    cemit_context_t *context, ctool_c_expression_operator_t operation,
    ctool_bool is_signed) {
  ctool_u32 equal_patch = CTOOL_C_AST_NONE;
  ctool_u32 equal_after = CTOOL_C_AST_NONE;
  ctool_u32 done_patch = CTOOL_C_AST_NONE;
  ctool_u32 done_after = CTOOL_C_AST_NONE;
  ctool_u32 equal_target;
  ctool_u32 done_target;
  ctool_x86_mnemonic_t predicate;
  ctool_status_t status;
  if (is_signed != CTOOL_FALSE && is_signed != CTOOL_TRUE) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(context, 2u, 0u, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, CTOOL_X86_MN_CMP, 2u, 1u, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JE, &equal_patch, &equal_after);
  }
  predicate = cemit_comparison_predicate(operation, is_signed);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, predicate, CTOOL_X86_REG_GPR8, 2u, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOVZX, CTOOL_X86_REG_GPR32, 2u,
        CTOOL_X86_REG_GPR8, 2u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JMP, &done_patch, &done_after);
  }
  equal_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, equal_patch, equal_after, equal_target);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_register_at_register(context, 2u, 0u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_binary_register_at_register(
        context, CTOOL_X86_MN_CMP, 2u, 1u, 0u);
  }
  predicate = cemit_comparison_predicate(operation, CTOOL_FALSE);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, predicate, CTOOL_X86_REG_GPR8, 2u, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOVZX, CTOOL_X86_REG_GPR32, 2u,
        CTOOL_X86_REG_GPR8, 2u, 32u);
  }
  done_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, done_patch, done_after, done_target);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 2u, 32u);
  }
  return status;
}

static ctool_bool cemit_ir_type_is_complete_aggregate_object(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cemit_unwrapped_type(context, type);
  return type < context->unit->layout.type_count &&
                 node != (const ctool_c_type_node_t *)0 &&
                 ((node->kind == CTOOL_C_TYPE_ARRAY &&
                   node->array_bound_kind == CTOOL_C_ARRAY_FIXED) ||
                  (node->kind == CTOOL_C_TYPE_RECORD &&
                   node->record_complete == CTOOL_TRUE)) &&
                 context->unit->layout.types[type].is_object == CTOOL_TRUE &&
                 context->unit->layout.types[type].is_complete_object ==
                     CTOOL_TRUE
              ? CTOOL_TRUE
              : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_addressable_unspecified_array(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *array = cemit_unwrapped_type(context, type);
  const ctool_c_type_layout_t *array_layout;
  const ctool_c_type_layout_t *element_layout;
  if (type >= context->unit->layout.type_count ||
      array == (const ctool_c_type_node_t *)0 ||
      array->kind != CTOOL_C_TYPE_ARRAY ||
      array->array_bound_kind != CTOOL_C_ARRAY_UNSPECIFIED ||
      array->referenced_type >= context->unit->layout.type_count ||
      cemit_type_has_atomic_qualification(context, type) == CTOOL_TRUE ||
      cemit_type_has_atomic_qualification(
          context, array->referenced_type) == CTOOL_TRUE) {
    return CTOOL_FALSE;
  }
  array_layout = &context->unit->layout.types[type];
  element_layout = &context->unit->layout.types[array->referenced_type];
  return array_layout->is_object == CTOOL_TRUE &&
                 array_layout->is_complete_object == CTOOL_FALSE &&
                 array_layout->size == 0u &&
                 element_layout->is_object == CTOOL_TRUE &&
                 element_layout->is_complete_object == CTOOL_TRUE &&
                 element_layout->size != 0u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_structure_value(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cemit_unwrapped_type(context, type);
  const ctool_c_type_layout_t *layout =
      type < context->unit->layout.type_count
          ? &context->unit->layout.types[type]
          : (const ctool_c_type_layout_t *)0;
  return node != (const ctool_c_type_node_t *)0 &&
                 layout != (const ctool_c_type_layout_t *)0 &&
                 node->kind == CTOOL_C_TYPE_RECORD &&
                 node->record_kind == CTOOL_C_RECORD_STRUCT &&
                 node->record_complete == CTOOL_TRUE &&
                 layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size != 0u && layout->alignment != 0u &&
                 layout->alignment <= 4u &&
                 cemit_power_of_two(layout->alignment) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_complete_record_object(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cemit_unwrapped_type(context, type);
  return node != (const ctool_c_type_node_t *)0 &&
                 node->kind == CTOOL_C_TYPE_RECORD &&
                 cemit_ir_type_is_complete_aggregate_object(context, type) ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_structure_types_match(
    const cemit_context_t *context, ctool_u32 left, ctool_u32 right) {
  const ctool_c_type_node_t *left_node = cemit_unwrapped_type(context, left);
  const ctool_c_type_node_t *right_node = cemit_unwrapped_type(context, right);
  return cemit_ir_type_is_structure_value(context, left) == CTOOL_TRUE &&
                 cemit_ir_type_is_structure_value(context, right) ==
                     CTOOL_TRUE &&
                 left_node == right_node &&
                 context->unit->layout.types[left].size ==
                     context->unit->layout.types[right].size
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_ir_argument_size(
    const cemit_context_t *context, ctool_u32 type,
    ctool_u32 *size_out) {
  if (size_out == (ctool_u32 *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *size_out = 0u;
  if (cemit_ir_type_is_represented_scalar(context, type) == CTOOL_TRUE) {
    *size_out = 4u;
    return CTOOL_OK;
  }
  if (cemit_ir_type_is_wide_integer(context, type) == CTOOL_TRUE) {
    *size_out = 8u;
    return CTOOL_OK;
  }
  if (cemit_ir_type_is_floating_value(context, type) == CTOOL_TRUE) {
    *size_out = context->unit->layout.types[type].size;
    return CTOOL_OK;
  }
  if (cemit_ir_type_is_structure_value(context, type) == CTOOL_FALSE) {
    return CTOOL_ERR_INTERNAL;
  }
  return cemit_align_value(context->unit->layout.types[type].size, 4u,
                           size_out);
}

static ctool_bool cemit_ir_function_returns_structure(
    const cemit_context_t *context,
    const ctool_c_type_node_t *function_type) {
  return function_type != (const ctool_c_type_node_t *)0 &&
                 function_type->kind == CTOOL_C_TYPE_FUNCTION &&
                 cemit_ir_type_is_structure_value(
                     context, function_type->referenced_type) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_ir_parameter_offset(
    const cemit_context_t *context,
    const ctool_c_type_node_t *function_type,
    ctool_u32 relative_parameter, ctool_u32 *offset_out) {
  ctool_u32 offset;
  ctool_u32 parameter;
  if (offset_out == (ctool_u32 *)0 ||
      function_type == (const ctool_c_type_node_t *)0 ||
      function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      relative_parameter >= function_type->parameter_count ||
      function_type->first_parameter >
          context->unit->graph.parameter_type_count ||
      function_type->parameter_count >
          context->unit->graph.parameter_type_count -
              function_type->first_parameter) {
    return CTOOL_ERR_INTERNAL;
  }
  offset = cemit_ir_function_returns_structure(context, function_type) ==
                   CTOOL_TRUE
               ? 12u
               : 8u;
  for (parameter = 0u; parameter < relative_parameter; parameter++) {
    ctool_u32 parameter_size;
    ctool_status_t status = cemit_ir_argument_size(
        context,
        context->unit->graph.parameter_types
            [function_type->first_parameter + parameter],
        &parameter_size);
    if (status != CTOOL_OK) {
      return status;
    }
    if (cemit_add_overflows(offset, parameter_size) == CTOOL_TRUE ||
        offset + parameter_size > 0x7fffffffu) {
      return CTOOL_ERR_OVERFLOW;
    }
    offset += parameter_size;
  }
  *offset_out = offset;
  return CTOOL_OK;
}

static ctool_status_t cemit_x86_push_floating_result(
    cemit_context_t *context, ctool_u32 type,
    ctool_u32 temporary_offset) {
  const ctool_c_type_layout_t *layout;
  ctool_status_t status;
  if (cemit_ir_type_is_floating_value(context, type) == CTOOL_FALSE) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->size == 4u) {
    status = cemit_x86_reserve_locals(context, 4u);
    return status == CTOOL_OK
               ? cemit_x86_x87_memory(
                     context, CTOOL_X86_MN_FSTP, 4u, 0, 32u)
               : status;
  }
  if (temporary_offset == CTOOL_C_AST_NONE || temporary_offset < 8u ||
      temporary_offset > 0x7fffffffu ||
      (temporary_offset & (layout->alignment - 1u)) != 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_x87_memory(
      context, CTOOL_X86_MN_FSTP, 5u,
      0 - (ctool_i32)temporary_offset, 64u);
  if (status == CTOOL_OK) {
    status = cemit_x86_lea_local(context, temporary_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_load_floating_stack_value(
    cemit_context_t *context, ctool_u32 type,
    ctool_u32 stack_offset) {
  const ctool_c_type_layout_t *layout;
  ctool_status_t status;
  if (cemit_ir_type_is_floating_value(context, type) == CTOOL_FALSE ||
      stack_offset > 0x7fffffffu) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->size == 4u) {
    return cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 4u, (ctool_i32)stack_offset, 32u);
  }
  status = cemit_x86_load_register_at_register(
      context, 0u, 4u, stack_offset);
  return status == CTOOL_OK
             ? cemit_x86_x87_memory(
                   context, CTOOL_X86_MN_FLD, 0u, 0, 64u)
             : status;
}

static ctool_status_t cemit_x86_load_floating_xmm_stack_value(
    cemit_context_t *context, ctool_u32 type,
    ctool_u8 xmm_register) {
  const ctool_c_type_layout_t *layout;
  ctool_status_t status;
  if (cemit_ir_type_is_floating_value(context, type) == CTOOL_FALSE) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->size == 4u) {
    status = cemit_x86_sse_memory(
        context, CTOOL_X86_MN_MOVSS, CTOOL_TRUE,
        xmm_register, 4u, 0, 32u);
    return status == CTOOL_OK
               ? cemit_x86_discard_arguments(context, 4u)
               : status;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  return status == CTOOL_OK
             ? cemit_x86_sse_memory(
                   context, CTOOL_X86_MN_MOVSD, CTOOL_TRUE,
                   xmm_register, 0u, 0, 64u)
             : status;
}

static ctool_status_t cemit_x86_push_floating_comparison(
    cemit_context_t *context, ctool_u32 type,
    ctool_c_expression_operator_t operation) {
  const ctool_c_type_layout_t *layout;
  ctool_x86_mnemonic_t predicate;
  ctool_x86_mnemonic_t comparison;
  ctool_bool ordered_sensitive;
  ctool_u32 unordered_patch = CTOOL_C_AST_NONE;
  ctool_u32 unordered_after = CTOOL_C_AST_NONE;
  ctool_u32 done_patch = CTOOL_C_AST_NONE;
  ctool_u32 done_after = CTOOL_C_AST_NONE;
  ctool_u32 unordered_target;
  ctool_u32 done_target;
  ctool_status_t status;
  if (cemit_ir_type_is_floating_value(context, type) == CTOOL_FALSE ||
      (operation != CTOOL_C_EXPRESSION_OPERATOR_EQUAL &&
       operation != CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL &&
       operation != CTOOL_C_EXPRESSION_OPERATOR_LESS &&
       operation != CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL &&
       operation != CTOOL_C_EXPRESSION_OPERATOR_GREATER &&
       operation != CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL)) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->size != 4u && layout->size != 8u) {
    return CTOOL_ERR_INTERNAL;
  }
  comparison = layout->size == 4u ? CTOOL_X86_MN_UCOMISS
                                  : CTOOL_X86_MN_UCOMISD;
  predicate = cemit_comparison_predicate(operation, CTOOL_FALSE);
  ordered_sensitive =
      operation == CTOOL_C_EXPRESSION_OPERATOR_GREATER ||
              operation == CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL
          ? CTOOL_FALSE
          : CTOOL_TRUE;

  status = cemit_x86_load_floating_xmm_stack_value(context, type, 1u);
  if (status == CTOOL_OK) {
    status = cemit_x86_load_floating_xmm_stack_value(context, type, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, comparison, CTOOL_X86_REG_XMM, 0u,
        CTOOL_X86_REG_XMM, 1u, 32u);
  }
  if (status == CTOOL_OK && ordered_sensitive == CTOOL_TRUE) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JP, &unordered_patch, &unordered_after);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, predicate, CTOOL_X86_REG_GPR8, 0u, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOVZX, CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_GPR8, 0u, 32u);
  }
  if (status == CTOOL_OK && ordered_sensitive == CTOOL_TRUE) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JMP, &done_patch, &done_after);
  }
  unordered_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK && ordered_sensitive == CTOOL_TRUE) {
    status = cemit_patch_branch(
        context->active_text, unordered_patch, unordered_after,
        unordered_target);
  }
  if (status == CTOOL_OK && ordered_sensitive == CTOOL_TRUE) {
    status = cemit_x86_move_register_constant(
        context, 0u,
        operation == CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL ? 1u : 0u);
  }
  done_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK && ordered_sensitive == CTOOL_TRUE) {
    status = cemit_patch_branch(
        context->active_text, done_patch, done_after, done_target);
  }
  return status == CTOOL_OK
             ? cemit_x86_one_register(
                   context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32,
                   0u, 32u)
             : status;
}

static ctool_status_t cemit_x86_push_floating_xmm_result(
    cemit_context_t *context, ctool_u32 type,
    ctool_u32 temporary_offset, ctool_u8 xmm_register) {
  const ctool_c_type_layout_t *layout;
  ctool_status_t status;
  if (cemit_ir_type_is_floating_value(context, type) == CTOOL_FALSE) {
    return CTOOL_ERR_INTERNAL;
  }
  layout = &context->unit->layout.types[type];
  if (layout->size == 4u) {
    status = cemit_x86_reserve_locals(context, 4u);
    return status == CTOOL_OK
               ? cemit_x86_sse_memory(
                     context, CTOOL_X86_MN_MOVSS, CTOOL_FALSE,
                     xmm_register, 4u, 0, 32u)
               : status;
  }
  if (temporary_offset == CTOOL_C_AST_NONE ||
      temporary_offset < 8u ||
      temporary_offset > 0x7fffffffu ||
      (temporary_offset & 3u) != 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_sse_memory(
      context, CTOOL_X86_MN_MOVSD, CTOOL_FALSE,
      xmm_register, 5u, 0 - (ctool_i32)temporary_offset, 64u);
  if (status == CTOOL_OK) {
    status = cemit_x86_lea_local(context, temporary_offset);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_x86_convert_integer_to_floating(
    cemit_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type, ctool_u32 temporary_offset) {
  const ctool_c_type_layout_t *source_layout;
  const ctool_c_type_node_t *target;
  ctool_x86_mnemonic_t conversion;
  ctool_status_t status;
  if (source_type >= context->unit->layout.type_count ||
      cemit_ir_type_is_represented_integer(
          context, source_type) == CTOOL_FALSE ||
      cemit_ir_type_is_floating_value(
          context, target_type) == CTOOL_FALSE) {
    return CTOOL_ERR_INTERNAL;
  }
  source_layout = &context->unit->layout.types[source_type];
  target = cemit_unwrapped_type(context, target_type);
  if (target == (const ctool_c_type_node_t *)0 ||
      (target->kind != CTOOL_C_TYPE_FLOAT &&
       target->kind != CTOOL_C_TYPE_DOUBLE)) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  if (status != CTOOL_OK) {
    return status;
  }
  if (source_layout->size == 4u &&
      source_layout->is_signed == CTOOL_FALSE) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 2u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_shift_register(
          context, CTOOL_X86_MN_SHR, 0u, 1u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_and_register_constant(context, 2u, 1u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_CVTSI2SD,
          CTOOL_X86_REG_XMM, 0u,
          CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_ADDSD,
          CTOOL_X86_REG_XMM, 0u,
          CTOOL_X86_REG_XMM, 0u, 64u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_CVTSI2SD,
          CTOOL_X86_REG_XMM, 1u,
          CTOOL_X86_REG_GPR32, 2u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_ADDSD,
          CTOOL_X86_REG_XMM, 0u,
          CTOOL_X86_REG_XMM, 1u, 64u);
    }
    if (status == CTOOL_OK &&
        target->kind == CTOOL_C_TYPE_FLOAT) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_CVTSD2SS,
          CTOOL_X86_REG_XMM, 0u,
          CTOOL_X86_REG_XMM, 0u, 32u);
    }
  } else {
    conversion =
        target->kind == CTOOL_C_TYPE_FLOAT
            ? CTOOL_X86_MN_CVTSI2SS
            : CTOOL_X86_MN_CVTSI2SD;
    status = cemit_x86_two_registers(
        context, conversion, CTOOL_X86_REG_XMM, 0u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status == CTOOL_OK
             ? cemit_x86_push_floating_xmm_result(
                   context, target_type, temporary_offset, 0u)
             : status;
}

static ctool_status_t cemit_x86_load_double_constant(
    cemit_context_t *context, ctool_u8 xmm_register,
    ctool_u32 low, ctool_u32 high) {
  ctool_status_t status = cemit_x86_push_integer(context, high);
  if (status == CTOOL_OK) {
    status = cemit_x86_push_integer(context, low);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_sse_memory(
        context, CTOOL_X86_MN_MOVSD, CTOOL_TRUE,
        xmm_register, 4u, 0, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_discard_arguments(context, 8u);
  }
  return status;
}

static ctool_status_t cemit_x86_truncate_double_to_u32(
    cemit_context_t *context, ctool_u8 source_xmm,
    ctool_u8 result_register) {
  ctool_u32 low_patch = CTOOL_C_AST_NONE;
  ctool_u32 low_after = CTOOL_C_AST_NONE;
  ctool_u32 done_patch = CTOOL_C_AST_NONE;
  ctool_u32 done_after = CTOOL_C_AST_NONE;
  ctool_u32 low_target;
  ctool_u32 done_target;
  ctool_status_t status;
  if (source_xmm >= 7u || result_register >= 8u ||
      result_register == 1u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_load_double_constant(
      context, 7u, 0x00000000u, 0x41e00000u);
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_UCOMISD,
        CTOOL_X86_REG_XMM, source_xmm,
        CTOOL_X86_REG_XMM, 7u, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JB, &low_patch, &low_after);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_SUBSD,
        CTOOL_X86_REG_XMM, source_xmm,
        CTOOL_X86_REG_XMM, 7u, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_CVTTSD2SI,
        CTOOL_X86_REG_GPR32, result_register,
        CTOOL_X86_REG_XMM, source_xmm, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_register_constant(
        context, 1u, 0x80000000u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_OR,
        CTOOL_X86_REG_GPR32, result_register,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JMP, &done_patch, &done_after);
  }
  low_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, low_patch, low_after, low_target);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_CVTTSD2SI,
        CTOOL_X86_REG_GPR32, result_register,
        CTOOL_X86_REG_XMM, source_xmm, 32u);
  }
  done_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, done_patch, done_after, done_target);
  }
  return status;
}

static ctool_status_t cemit_x86_convert_double_to_unsigned_wide(
    cemit_context_t *context, ctool_u32 source_type,
    ctool_u32 temporary_offset) {
  ctool_status_t status;
  if (temporary_offset == CTOOL_C_AST_NONE ||
      temporary_offset < 8u || temporary_offset > 0x7fffffffu) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_load_floating_xmm_stack_value(
      context, source_type, 0u);
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOVSD,
        CTOOL_X86_REG_XMM, 2u,
        CTOOL_X86_REG_XMM, 0u, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_double_constant(
        context, 1u, 0x00000000u, 0x41f00000u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_DIVSD,
        CTOOL_X86_REG_XMM, 0u,
        CTOOL_X86_REG_XMM, 1u, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_truncate_double_to_u32(
        context, 0u, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV,
        CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_GPR32, 2u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV,
        CTOOL_X86_REG_GPR32, 1u,
        CTOOL_X86_REG_GPR32, 2u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_shift_register(
        context, CTOOL_X86_MN_SHR, 0u, 1u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_and_register_constant(
        context, 1u, 1u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_CVTSI2SD,
        CTOOL_X86_REG_XMM, 3u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_ADDSD,
        CTOOL_X86_REG_XMM, 3u,
        CTOOL_X86_REG_XMM, 3u, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_CVTSI2SD,
        CTOOL_X86_REG_XMM, 4u,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_ADDSD,
        CTOOL_X86_REG_XMM, 3u,
        CTOOL_X86_REG_XMM, 4u, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MULSD,
        CTOOL_X86_REG_XMM, 3u,
        CTOOL_X86_REG_XMM, 1u, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_SUBSD,
        CTOOL_X86_REG_XMM, 2u,
        CTOOL_X86_REG_XMM, 3u, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_truncate_double_to_u32(
        context, 2u, 0u);
  }
  return status == CTOOL_OK
             ? cemit_x86_push_wide_result_snapshot(
                   context, temporary_offset)
             : status;
}

static ctool_status_t cemit_x86_convert_floating_to_integer(
    cemit_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type, ctool_u32 temporary_offset) {
  const ctool_c_type_node_t *source =
      cemit_unwrapped_type(context, source_type);
  const ctool_c_type_node_t *target =
      cemit_unwrapped_type(context, target_type);
  ctool_x86_mnemonic_t conversion;
  ctool_status_t status;
  if (source == (const ctool_c_type_node_t *)0 ||
      target == (const ctool_c_type_node_t *)0 ||
      (source->kind != CTOOL_C_TYPE_FLOAT &&
       source->kind != CTOOL_C_TYPE_DOUBLE) ||
      (cemit_ir_type_is_represented_integer(
           context, target_type) == CTOOL_FALSE &&
       cemit_ir_type_is_wide_integer(
           context, target_type) == CTOOL_FALSE)) {
    return CTOOL_ERR_INTERNAL;
  }
  if (cemit_ir_type_is_wide_integer(
          context, target_type) == CTOOL_TRUE) {
    if (source->kind != CTOOL_C_TYPE_DOUBLE ||
        target->kind != CTOOL_C_TYPE_UNSIGNED_LONG_LONG ||
        context->unit->layout.types[target_type].is_signed ==
            CTOOL_TRUE) {
      return CTOOL_ERR_INTERNAL;
    }
    return cemit_x86_convert_double_to_unsigned_wide(
        context, source_type, temporary_offset);
  }
  status = cemit_x86_load_floating_xmm_stack_value(
      context, source_type, 0u);
  conversion =
      source->kind == CTOOL_C_TYPE_FLOAT
          ? CTOOL_X86_MN_CVTTSS2SI
          : CTOOL_X86_MN_CVTTSD2SI;
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, conversion, CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_XMM, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_canonicalize_scalar_eax(
        context, target_type);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_bool cemit_ir_type_is_automatic_object(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_layout_t *layout;
  if (type >= context->unit->layout.type_count) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[type];
  return (cemit_ir_type_is_value_scalar(context, type) == CTOOL_TRUE ||
          cemit_ir_type_is_complete_aggregate_object(context, type) ==
              CTOOL_TRUE) &&
                 layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size != 0u && layout->alignment != 0u &&
                 layout->alignment <= 4u &&
                 cemit_power_of_two(layout->alignment) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_initializable_aggregate_object(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cemit_unwrapped_type(context, type);
  return node != (const ctool_c_type_node_t *)0 &&
                 cemit_ir_type_is_automatic_object(context, type) ==
                     CTOOL_TRUE &&
                 ((node->kind == CTOOL_C_TYPE_ARRAY &&
                   node->array_bound_kind == CTOOL_C_ARRAY_FIXED) ||
                  (node->kind == CTOOL_C_TYPE_RECORD &&
                   (node->record_kind == CTOOL_C_RECORD_STRUCT ||
                    node->record_kind == CTOOL_C_RECORD_UNION) &&
                   node->record_complete == CTOOL_TRUE))
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_character_array(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *array = cemit_unwrapped_type(context, type);
  const ctool_c_type_node_t *element =
      array != (const ctool_c_type_node_t *)0 &&
              array->kind == CTOOL_C_TYPE_ARRAY
          ? cemit_unwrapped_type(context, array->referenced_type)
          : (const ctool_c_type_node_t *)0;
  const ctool_c_type_layout_t *array_layout =
      type < context->unit->layout.type_count
          ? &context->unit->layout.types[type]
          : (const ctool_c_type_layout_t *)0;
  const ctool_c_type_layout_t *element_layout =
      array != (const ctool_c_type_node_t *)0 &&
              array->referenced_type < context->unit->layout.type_count
          ? &context->unit->layout.types[array->referenced_type]
          : (const ctool_c_type_layout_t *)0;
  return array != (const ctool_c_type_node_t *)0 &&
                 element != (const ctool_c_type_node_t *)0 &&
                 array_layout != (const ctool_c_type_layout_t *)0 &&
                 element_layout != (const ctool_c_type_layout_t *)0 &&
                 array->kind == CTOOL_C_TYPE_ARRAY &&
                 array->array_bound_kind == CTOOL_C_ARRAY_FIXED &&
                 array->element_count != 0u &&
                 (element->kind == CTOOL_C_TYPE_CHAR ||
                  element->kind == CTOOL_C_TYPE_SIGNED_CHAR ||
                  element->kind == CTOOL_C_TYPE_UNSIGNED_CHAR) &&
                 element_layout->is_object == CTOOL_TRUE &&
                 element_layout->is_complete_object == CTOOL_TRUE &&
                 element_layout->size == 1u &&
                 array_layout->is_object == CTOOL_TRUE &&
                 array_layout->is_complete_object == CTOOL_TRUE &&
                 array_layout->size == array->element_count
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_type_is_void(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cemit_unwrapped_type(context, type);
  return node != (const ctool_c_type_node_t *)0 &&
                 node->kind == CTOOL_C_TYPE_VOID
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_call_argument_count_is_valid(
    const ctool_c_type_node_t *function_type,
    const ctool_c_ir_instruction_t *instruction) {
  return function_type != (const ctool_c_type_node_t *)0 &&
                 instruction != (const ctool_c_ir_instruction_t *)0 &&
                 function_type->kind == CTOOL_C_TYPE_FUNCTION &&
                 ((function_type->has_prototype == CTOOL_TRUE &&
                   instruction->argument_count >=
                       function_type->parameter_count &&
                   (function_type->variadic == CTOOL_TRUE ||
                    instruction->argument_count ==
                        function_type->parameter_count)) ||
                  (function_type->has_prototype == CTOOL_FALSE &&
                   function_type->parameter_count == 0u &&
                   function_type->variadic == CTOOL_FALSE)) &&
                 instruction->argument_count <= 0x1fffffffu
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_validate_argument_type_slices(
    cemit_context_t *context) {
  ctool_bool valid = CTOOL_FALSE;
  ctool_status_t status = ctool_c_ir_validate_call_slices(
      context->unit, &context->ir, &valid);
  if (status != CTOOL_OK) {
    return CTOOL_ERR_INTERNAL;
  }
  return valid == CTOOL_TRUE
             ? CTOOL_OK
             : cemit_invalid_unit(
                   context, (const ctool_c_pp_location_t *)0);
}

static ctool_status_t cemit_call_argument_transport_type(
    cemit_context_t *context,
    const ctool_c_type_node_t *function_type,
    const ctool_c_ir_instruction_t *instruction, ctool_u32 argument,
    ctool_u32 *type_out) {
  ctool_u32 actual_type;
  if (function_type == (const ctool_c_type_node_t *)0 ||
      instruction == (const ctool_c_ir_instruction_t *)0 ||
      type_out == (ctool_u32 *)0 ||
      cemit_call_argument_count_is_valid(function_type, instruction) ==
          CTOOL_FALSE ||
      argument >= instruction->argument_count ||
      instruction->first_argument_type > context->ir.argument_type_count ||
      instruction->argument_count >
          context->ir.argument_type_count -
              instruction->first_argument_type ||
      context->ir.argument_types == (const ctool_u32 *)0) {
    return CTOOL_ERR_INTERNAL;
  }
  actual_type = context->ir.argument_types
      [instruction->first_argument_type + argument];
  if (actual_type >= context->unit->graph.type_count ||
      actual_type >= context->unit->layout.type_count) {
    return CTOOL_ERR_INTERNAL;
  }
  if (argument < function_type->parameter_count) {
    ctool_u32 declared_type =
        context->unit->graph.parameter_types
            [function_type->first_parameter + argument];
    if ((cemit_ir_type_is_value_scalar(context, declared_type) ==
             CTOOL_TRUE &&
         cemit_ir_type_is_value_scalar(context, actual_type) ==
             CTOOL_TRUE &&
         cemit_ir_scalar_types_match(context, declared_type, actual_type) ==
             CTOOL_TRUE) ||
        cemit_ir_structure_types_match(
            context, declared_type, actual_type) == CTOOL_TRUE) {
      *type_out = declared_type;
      return CTOOL_OK;
    }
    return CTOOL_ERR_INTERNAL;
  }
  if (cemit_ir_type_is_variadic_argument(context, actual_type) ==
      CTOOL_FALSE) {
    return CTOOL_ERR_INTERNAL;
  }
  *type_out = actual_type;
  return CTOOL_OK;
}

static ctool_status_t cemit_call_uses_outgoing_area(
    cemit_context_t *context,
    const ctool_c_type_node_t *function_type,
    const ctool_c_ir_instruction_t *instruction,
    ctool_bool *uses_outgoing_area_out) {
  ctool_u32 argument;
  if (uses_outgoing_area_out == (ctool_bool *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *uses_outgoing_area_out = cemit_ir_function_returns_structure(
      context, function_type);
  for (argument = 0u; argument < instruction->argument_count; argument++) {
    ctool_u32 transport_type;
    ctool_status_t status = cemit_call_argument_transport_type(
        context, function_type, instruction, argument, &transport_type);
    if (status != CTOOL_OK) {
      return status;
    }
    if (cemit_ir_type_is_structure_value(context, transport_type) ==
            CTOOL_TRUE ||
        cemit_ir_type_is_wide_integer(context, transport_type) == CTOOL_TRUE ||
        (cemit_ir_type_is_floating_value(context, transport_type) ==
             CTOOL_TRUE &&
         context->unit->layout.types[transport_type].size == 8u)) {
      *uses_outgoing_area_out = CTOOL_TRUE;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_emit_outgoing_area_call(
    cemit_context_t *context,
    const ctool_c_ir_instruction_t *instruction,
    const ctool_c_type_node_t *function_type, ctool_bool direct,
    ctool_u32 symbol, ctool_u32 temporary_offset,
    ctool_u32 stack_base_residue, ctool_u32 frame_size,
    ctool_u32 stack_depth) {
  ctool_bool structure_result =
      cemit_ir_function_returns_structure(context, function_type);
  ctool_bool wide_result =
      cemit_ir_type_is_wide_integer(context, instruction->type);
  ctool_bool floating_result =
      cemit_ir_type_is_floating_value(context, instruction->type);
  ctool_u32 hidden_bytes = structure_result == CTOOL_TRUE ? 4u : 0u;
  ctool_u32 outgoing_bytes = hidden_bytes;
  ctool_u32 reserved_bytes;
  ctool_u32 placeholder_bytes;
  ctool_u32 destination_offset = hidden_bytes;
  ctool_u32 padding;
  ctool_u32 argument;
  ctool_status_t status = CTOOL_OK;
  if (function_type == (const ctool_c_type_node_t *)0 ||
      function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count > 0x1fffffffu ||
      cemit_call_argument_count_is_valid(function_type, instruction) ==
          CTOOL_FALSE ||
      (direct != CTOOL_FALSE && direct != CTOOL_TRUE)) {
    return CTOOL_ERR_INTERNAL;
  }
  for (argument = 0u; argument < instruction->argument_count; argument++) {
    ctool_u32 transport_type;
    ctool_u32 argument_size;
    status = cemit_call_argument_transport_type(
        context, function_type, instruction, argument, &transport_type);
    if (status == CTOOL_OK) {
      status = cemit_ir_argument_size(
          context, transport_type, &argument_size);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (cemit_add_overflows(outgoing_bytes, argument_size) == CTOOL_TRUE ||
        outgoing_bytes + argument_size > 0x7fffffffu) {
      return CTOOL_ERR_OVERFLOW;
    }
    outgoing_bytes += argument_size;
  }
  if (cemit_multiply_overflows(instruction->argument_count, 4u) ==
      CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  placeholder_bytes = instruction->argument_count * 4u;
  if (direct == CTOOL_FALSE) {
    if (cemit_add_overflows(placeholder_bytes, 4u) == CTOOL_TRUE) {
      return CTOOL_ERR_OVERFLOW;
    }
    placeholder_bytes += 4u;
  }
  if (structure_result == CTOOL_TRUE) {
    const ctool_c_type_layout_t *layout =
        &context->unit->layout.types[instruction->type];
    if (temporary_offset == CTOOL_C_AST_NONE || temporary_offset == 0u ||
        temporary_offset < layout->size || temporary_offset > 0x7fffffffu ||
        (temporary_offset & (layout->alignment - 1u)) != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
  } else if (wide_result == CTOOL_TRUE ||
             (floating_result == CTOOL_TRUE &&
              context->unit->layout.types[instruction->type].size == 8u)) {
    const ctool_c_type_layout_t *layout =
        &context->unit->layout.types[instruction->type];
    if (temporary_offset == CTOOL_C_AST_NONE || temporary_offset == 0u ||
        temporary_offset < layout->size || temporary_offset > 0x7fffffffu ||
        (temporary_offset & (layout->alignment - 1u)) != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  status = cemit_call_stack_padding(
      stack_base_residue, frame_size, stack_depth,
      outgoing_bytes, &padding);
  if (status != CTOOL_OK ||
      cemit_add_overflows(outgoing_bytes, padding) == CTOOL_TRUE) {
    return status == CTOOL_OK ? CTOOL_ERR_OVERFLOW : status;
  }
  reserved_bytes = outgoing_bytes + padding;
  status = cemit_x86_reserve_locals(context, reserved_bytes);
  if (status == CTOOL_OK) {
    status = cemit_x86_zero_stack_area(context, outgoing_bytes);
  }
  if (status == CTOOL_OK && structure_result == CTOOL_TRUE) {
    status = cemit_x86_lea_local(context, temporary_offset);
    if (status == CTOOL_OK) {
      status = cemit_x86_store_stack(context, 0u, 0u);
    }
  }
  for (argument = 0u; status == CTOOL_OK &&
                      argument < instruction->argument_count;
       argument++) {
    ctool_u32 transport_type;
    ctool_u32 argument_size;
    ctool_u32 handle_offset;
    status = cemit_call_argument_transport_type(
        context, function_type, instruction, argument, &transport_type);
    if (status == CTOOL_OK) {
      status = cemit_ir_argument_size(context, transport_type,
                                      &argument_size);
    }
    if (status != CTOOL_OK ||
        cemit_multiply_overflows(
            instruction->argument_count - 1u - argument, 4u) ==
            CTOOL_TRUE) {
      return status == CTOOL_OK ? CTOOL_ERR_OVERFLOW : status;
    }
    handle_offset =
        (instruction->argument_count - 1u - argument) * 4u;
    if (cemit_add_overflows(reserved_bytes, handle_offset) == CTOOL_TRUE) {
      return CTOOL_ERR_OVERFLOW;
    }
    handle_offset += reserved_bytes;
    status = cemit_x86_load_stack(context, 2u, handle_offset);
    if (status == CTOOL_OK &&
        cemit_ir_type_is_represented_scalar(context, transport_type) ==
            CTOOL_TRUE) {
      status = cemit_x86_store_stack(context, destination_offset, 2u);
    } else if (status == CTOOL_OK) {
      status = cemit_x86_lea_stack(context, 0u, destination_offset);
      if (status == CTOOL_OK) {
        status = cemit_x86_copy_edx_to_eax(
            context,
            context->unit->layout.types[transport_type].size);
      }
    }
    if (status == CTOOL_OK) {
      if (cemit_add_overflows(destination_offset, argument_size) ==
          CTOOL_TRUE) {
        return CTOOL_ERR_OVERFLOW;
      }
      destination_offset += argument_size;
    }
  }
  if (status != CTOOL_OK || destination_offset != outgoing_bytes) {
    return status == CTOOL_OK ? CTOOL_ERR_INTERNAL : status;
  }
  if (direct == CTOOL_FALSE) {
    ctool_u32 callee_offset;
    if (cemit_multiply_overflows(instruction->argument_count, 4u) ==
            CTOOL_TRUE ||
        cemit_add_overflows(reserved_bytes,
                            instruction->argument_count * 4u) ==
            CTOOL_TRUE) {
      return CTOOL_ERR_OVERFLOW;
    }
    callee_offset =
        reserved_bytes + instruction->argument_count * 4u;
    status = cemit_x86_load_stack(context, 0u, callee_offset);
    if (status == CTOOL_OK) {
      status = cemit_x86_call_register(context, 0u);
    }
  } else {
    status = cemit_x86_call_symbol(context, symbol);
  }
  if (status == CTOOL_OK) {
    ctool_u32 cleanup = outgoing_bytes - hidden_bytes;
    if (cemit_add_overflows(cleanup, padding) == CTOOL_TRUE) {
      return CTOOL_ERR_OVERFLOW;
    }
    cleanup += padding;
    if (cemit_add_overflows(cleanup, placeholder_bytes) == CTOOL_TRUE) {
      return CTOOL_ERR_OVERFLOW;
    }
    cleanup += placeholder_bytes;
    status = cemit_x86_discard_arguments(context, cleanup);
  }
  if (status == CTOOL_OK && wide_result == CTOOL_TRUE) {
    status = cemit_x86_push_wide_result_snapshot(
        context, temporary_offset);
  } else if (status == CTOOL_OK && floating_result == CTOOL_TRUE) {
    status = cemit_x86_push_floating_result(
        context, instruction->type, temporary_offset);
  } else if (status == CTOOL_OK && structure_result == CTOOL_FALSE &&
      cemit_ir_type_is_void(context, instruction->type) == CTOOL_FALSE) {
    status = cemit_x86_canonicalize_scalar_eax(context, instruction->type);
  }
  if (status == CTOOL_OK && wide_result == CTOOL_FALSE &&
      floating_result == CTOOL_FALSE &&
      cemit_ir_type_is_void(context, instruction->type) == CTOOL_FALSE) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_emit_direct_call(
    cemit_context_t *context,
    const ctool_c_ir_instruction_t *instruction,
    ctool_u32 temporary_offset, ctool_u32 stack_base_residue,
    ctool_u32 frame_size, ctool_u32 stack_depth) {
  const ctool_c_binding_t *binding;
  const ctool_c_type_node_t *function_type;
  ctool_bool uses_outgoing_area;
  ctool_u32 argument;
  ctool_u32 argument_bytes;
  ctool_u32 padding;
  ctool_u32 symbol;
  ctool_status_t status = CTOOL_OK;
  if (instruction->reference >= context->unit->binding_count ||
      instruction->input_type >= context->unit->graph.type_count) {
    return CTOOL_ERR_INTERNAL;
  }
  binding = &context->unit->bindings[instruction->reference];
  function_type = cemit_unwrapped_type(context, instruction->input_type);
  if (binding->kind != CTOOL_C_BINDING_FUNCTION ||
      cemit_ir_function_types_match(
          context, binding->type, instruction->input_type) == CTOOL_FALSE ||
      function_type == (const ctool_c_type_node_t *)0 ||
      function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->referenced_type != instruction->type ||
      function_type->first_parameter >
          context->unit->graph.parameter_type_count ||
      function_type->parameter_count >
          context->unit->graph.parameter_type_count -
              function_type->first_parameter ||
      function_type->parameter_count > 0x1fffffffu ||
      cemit_call_argument_count_is_valid(function_type, instruction) ==
          CTOOL_FALSE ||
      (cemit_ir_type_is_void(context, instruction->type) == CTOOL_FALSE &&
       cemit_ir_type_is_value_scalar(context, instruction->type) ==
           CTOOL_FALSE &&
       cemit_ir_type_is_structure_value(context, instruction->type) ==
           CTOOL_FALSE) ||
      instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instruction->conversion != CTOOL_C_CONVERSION_NONE ||
      instruction->integer_bits != 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_call_uses_outgoing_area(
      context, function_type, instruction, &uses_outgoing_area);
  if (status != CTOOL_OK) {
    return status;
  }
  symbol = context->binding_symbols[instruction->reference];
  if (symbol == CTOOL_C_AST_NONE || symbol >= context->symbol_count) {
    return CTOOL_ERR_INTERNAL;
  }
  if (uses_outgoing_area == CTOOL_TRUE) {
    return cemit_emit_outgoing_area_call(
        context, instruction, function_type, CTOOL_TRUE, symbol,
        temporary_offset, stack_base_residue, frame_size,
        stack_depth);
  }
  for (argument = 0u; status == CTOOL_OK &&
                      argument < instruction->argument_count / 2u;
       argument++) {
    ctool_u32 low_offset = argument * 4u;
    ctool_u32 high_offset =
        (instruction->argument_count - 1u - argument) * 4u;
    status = cemit_x86_load_stack(context, 1u, high_offset);
    if (status == CTOOL_OK) {
      status = cemit_x86_load_stack(context, 2u, low_offset);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_stack(context, high_offset, 2u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_stack(context, low_offset, 1u);
    }
  }
  if (status != CTOOL_OK) {
    return status;
  }
  argument_bytes = instruction->argument_count * 4u;
  status = cemit_call_stack_padding(
      stack_base_residue, frame_size, stack_depth, 0u, &padding);
  if (status == CTOOL_OK) {
    status = cemit_x86_shift_call_arguments(
        context, argument_bytes, padding);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_call_symbol(context, symbol);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (cemit_add_overflows(argument_bytes, padding) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  argument_bytes += padding;
  status = cemit_x86_discard_arguments(context, argument_bytes);
  if (status == CTOOL_OK &&
      cemit_ir_type_is_wide_integer(context, instruction->type) ==
          CTOOL_TRUE) {
    status = cemit_x86_push_wide_result_snapshot(
        context, temporary_offset);
  } else if (status == CTOOL_OK &&
             cemit_ir_type_is_floating_value(
                 context, instruction->type) == CTOOL_TRUE) {
    status = cemit_x86_push_floating_result(
        context, instruction->type, temporary_offset);
  } else if (status == CTOOL_OK &&
      cemit_ir_type_is_void(context, instruction->type) == CTOOL_FALSE) {
    status = cemit_x86_canonicalize_scalar_eax(context, instruction->type);
  }
  if (status == CTOOL_OK &&
      cemit_ir_type_is_wide_integer(context, instruction->type) ==
          CTOOL_FALSE &&
      cemit_ir_type_is_floating_value(context, instruction->type) ==
          CTOOL_FALSE &&
      cemit_ir_type_is_void(context, instruction->type) == CTOOL_FALSE) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_emit_indirect_call(
    cemit_context_t *context,
    const ctool_c_ir_instruction_t *instruction,
    ctool_u32 temporary_offset, ctool_u32 stack_base_residue,
    ctool_u32 frame_size, ctool_u32 stack_depth) {
  const ctool_c_type_node_t *pointer_type;
  const ctool_c_type_node_t *function_type;
  ctool_bool uses_outgoing_area;
  ctool_u32 argument;
  ctool_u32 argument_bytes;
  ctool_u32 consumed_bytes;
  ctool_u32 padding;
  ctool_status_t status = CTOOL_OK;
  pointer_type = cemit_unwrapped_type(context, instruction->input_type);
  function_type =
      pointer_type != (const ctool_c_type_node_t *)0 &&
              pointer_type->kind == CTOOL_C_TYPE_POINTER
          ? cemit_unwrapped_type(context, pointer_type->referenced_type)
          : (const ctool_c_type_node_t *)0;
  if (cemit_ir_type_is_i32_function_pointer(
          context, instruction->input_type) == CTOOL_FALSE ||
      function_type == (const ctool_c_type_node_t *)0 ||
      function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->referenced_type != instruction->type ||
      function_type->first_parameter >
          context->unit->graph.parameter_type_count ||
      function_type->parameter_count >
          context->unit->graph.parameter_type_count -
              function_type->first_parameter ||
      function_type->parameter_count > 0x1fffffffu ||
      cemit_call_argument_count_is_valid(function_type, instruction) ==
          CTOOL_FALSE ||
      (cemit_ir_type_is_void(context, instruction->type) == CTOOL_FALSE &&
       cemit_ir_type_is_value_scalar(context, instruction->type) ==
           CTOOL_FALSE &&
       cemit_ir_type_is_structure_value(context, instruction->type) ==
           CTOOL_FALSE) ||
      instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instruction->conversion != CTOOL_C_CONVERSION_NONE ||
      instruction->reference != CTOOL_C_AST_NONE ||
      instruction->integer_bits != 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_call_uses_outgoing_area(
      context, function_type, instruction, &uses_outgoing_area);
  if (status != CTOOL_OK) {
    return status;
  }
  if (uses_outgoing_area == CTOOL_TRUE) {
    return cemit_emit_outgoing_area_call(
        context, instruction, function_type, CTOOL_FALSE,
        CTOOL_C_AST_NONE, temporary_offset, stack_base_residue,
        frame_size, stack_depth);
  }
  for (argument = 0u; status == CTOOL_OK &&
                      argument < instruction->argument_count / 2u;
       argument++) {
    ctool_u32 low_offset = argument * 4u;
    ctool_u32 high_offset =
        (instruction->argument_count - 1u - argument) * 4u;
    status = cemit_x86_load_stack(context, 1u, high_offset);
    if (status == CTOOL_OK) {
      status = cemit_x86_load_stack(context, 2u, low_offset);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_stack(context, high_offset, 2u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_stack(context, low_offset, 1u);
    }
  }
  argument_bytes = instruction->argument_count * 4u;
  if (status == CTOOL_OK) {
    status = cemit_call_stack_padding(
        stack_base_residue, frame_size, stack_depth, 0u, &padding);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_shift_call_arguments(
        context, argument_bytes, padding);
  }
  if (status == CTOOL_OK &&
      cemit_add_overflows(argument_bytes, padding) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(
        context, 0u, argument_bytes + padding);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_call_register(context, 0u);
  }
  if (status != CTOOL_OK ||
      cemit_add_overflows(argument_bytes, padding) == CTOOL_TRUE ||
      cemit_add_overflows(argument_bytes + padding, 4u) == CTOOL_TRUE) {
    return status == CTOOL_OK ? CTOOL_ERR_OVERFLOW : status;
  }
  consumed_bytes = argument_bytes + padding + 4u;
  if (status == CTOOL_OK) {
    status = cemit_x86_discard_arguments(context, consumed_bytes);
  }
  if (status == CTOOL_OK &&
      cemit_ir_type_is_wide_integer(context, instruction->type) ==
          CTOOL_TRUE) {
    status = cemit_x86_push_wide_result_snapshot(
        context, temporary_offset);
  } else if (status == CTOOL_OK &&
             cemit_ir_type_is_floating_value(
                 context, instruction->type) == CTOOL_TRUE) {
    status = cemit_x86_push_floating_result(
        context, instruction->type, temporary_offset);
  } else if (status == CTOOL_OK &&
      cemit_ir_type_is_void(context, instruction->type) == CTOOL_FALSE) {
    status = cemit_x86_canonicalize_scalar_eax(context, instruction->type);
  }
  if (status == CTOOL_OK &&
      cemit_ir_type_is_wide_integer(context, instruction->type) ==
          CTOOL_FALSE &&
      cemit_ir_type_is_floating_value(context, instruction->type) ==
          CTOOL_FALSE &&
      cemit_ir_type_is_void(context, instruction->type) == CTOOL_FALSE) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_bool cemit_string_equals_literal(
    ctool_string_t value, const char *literal) {
  ctool_u32 index = 0u;
  if (literal == (const char *)0 ||
      (value.data == (const char *)0 && value.size != 0u)) {
    return CTOOL_FALSE;
  }
  while (literal[index] != '\0') {
    if (index >= value.size || value.data[index] != literal[index]) {
      return CTOOL_FALSE;
    }
    index++;
  }
  return index == value.size ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool cemit_assembly_output_fixed_register(
    ctool_string_t constraint, ctool_u8 *register_out) {
  if (cemit_string_equals_literal(constraint, "=a") == CTOOL_TRUE) {
    *register_out = 0u;
  } else if (cemit_string_equals_literal(constraint, "=c") == CTOOL_TRUE) {
    *register_out = 1u;
  } else if (cemit_string_equals_literal(constraint, "=d") == CTOOL_TRUE) {
    *register_out = 2u;
  } else if (cemit_string_equals_literal(constraint, "=b") == CTOOL_TRUE) {
    *register_out = 3u;
  } else {
    return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_bool cemit_assembly_input_fixed_register(
    ctool_string_t constraint, ctool_u8 *register_out) {
  if (cemit_string_equals_literal(constraint, "a") == CTOOL_TRUE) {
    *register_out = 0u;
  } else if (cemit_string_equals_literal(constraint, "c") == CTOOL_TRUE) {
    *register_out = 1u;
  } else if (cemit_string_equals_literal(constraint, "d") == CTOOL_TRUE ||
             cemit_string_equals_literal(constraint, "Nd") == CTOOL_TRUE) {
    *register_out = 2u;
  } else {
    return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

typedef enum {
  CEMIT_PRIVILEGED_ASSEMBLY_NONE = 0,
  CEMIT_PRIVILEGED_ASSEMBLY_READ_CR0,
  CEMIT_PRIVILEGED_ASSEMBLY_READ_CR2,
  CEMIT_PRIVILEGED_ASSEMBLY_READ_CR4,
  CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR0,
  CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR3,
  CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR4,
  CEMIT_PRIVILEGED_ASSEMBLY_RDMSR
} cemit_privileged_assembly_kind_t;

static cemit_privileged_assembly_kind_t
cemit_privileged_assembly_template_kind(
    ctool_string_t template_text) {
  if (cemit_string_equals_literal(
          template_text, "mov %%cr0, %0") == CTOOL_TRUE) {
    return CEMIT_PRIVILEGED_ASSEMBLY_READ_CR0;
  }
  if (cemit_string_equals_literal(
          template_text, "mov %%cr2, %0") == CTOOL_TRUE) {
    return CEMIT_PRIVILEGED_ASSEMBLY_READ_CR2;
  }
  if (cemit_string_equals_literal(
          template_text, "mov %%cr4, %0") == CTOOL_TRUE) {
    return CEMIT_PRIVILEGED_ASSEMBLY_READ_CR4;
  }
  if (cemit_string_equals_literal(
          template_text, "mov %0, %%cr0") == CTOOL_TRUE) {
    return CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR0;
  }
  if (cemit_string_equals_literal(
          template_text, "mov %0, %%cr3") == CTOOL_TRUE) {
    return CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR3;
  }
  if (cemit_string_equals_literal(
          template_text, "mov %0, %%cr4") == CTOOL_TRUE) {
    return CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR4;
  }
  return cemit_string_equals_literal(
             template_text, "rdmsr") == CTOOL_TRUE
             ? CEMIT_PRIVILEGED_ASSEMBLY_RDMSR
             : CEMIT_PRIVILEGED_ASSEMBLY_NONE;
}

static ctool_bool cemit_privileged_assembly_operand_matches(
    const cemit_context_t *context,
    const ctool_c_assembly_operand_t *operand,
    const char *constraint, ctool_bool output,
    ctool_bool allow_pointer) {
  const ctool_c_type_layout_t *layout;
  const ctool_c_type_node_t *node;
  ctool_u32 qualifiers;
  if (operand == (const ctool_c_assembly_operand_t *)0 ||
      operand->expression >= context->unit->expression_count ||
      operand->matching_output != CTOOL_C_AST_NONE ||
      operand->type >= context->unit->layout.type_count ||
      cemit_string_equals_literal(
          operand->constraint, constraint) == CTOOL_FALSE ||
      cemit_underlying_type(
          context, operand->type, &qualifiers, &node) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  (void)node;
  layout = &context->unit->layout.types[operand->type];
  if (layout->size != 4u || layout->is_object == CTOOL_FALSE ||
      layout->is_complete_object == CTOOL_FALSE ||
      (output == CTOOL_TRUE &&
       (qualifiers &
        (CTOOL_C_QUAL_CONST | CTOOL_C_QUAL_ATOMIC)) != 0u)) {
    return CTOOL_FALSE;
  }
  return layout->is_integer == CTOOL_TRUE ||
                 (allow_pointer == CTOOL_TRUE &&
                  cemit_ir_type_is_i32_pointer(
                      context, operand->type) == CTOOL_TRUE)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_privileged_assembly_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    cemit_privileged_assembly_kind_t kind) {
  const ctool_c_assembly_operand_t *operands;
  ctool_bool read;
  ctool_bool write;
  if (kind == CEMIT_PRIVILEGED_ASSEMBLY_NONE ||
      (assembly->flags & CTOOL_C_ASSEMBLY_BASIC) != 0u ||
      assembly->first_operand >
          context->unit->assembly_operand_count ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  operands =
      &context->unit->assembly_operands[assembly->first_operand];
  read = kind == CEMIT_PRIVILEGED_ASSEMBLY_READ_CR0 ||
                 kind == CEMIT_PRIVILEGED_ASSEMBLY_READ_CR2 ||
                 kind == CEMIT_PRIVILEGED_ASSEMBLY_READ_CR4
             ? CTOOL_TRUE
             : CTOOL_FALSE;
  write = kind == CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR0 ||
                  kind == CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR3 ||
                  kind == CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR4
              ? CTOOL_TRUE
              : CTOOL_FALSE;
  if (read == CTOOL_TRUE) {
    return assembly->flags == CTOOL_C_ASSEMBLY_VOLATILE &&
                   assembly->output_count == 1u &&
                   assembly->input_count == 0u &&
                   assembly->first_operand <
                       context->unit->assembly_operand_count &&
                   cemit_privileged_assembly_operand_matches(
                       context, &operands[0], "=r", CTOOL_TRUE,
                       CTOOL_FALSE) == CTOOL_TRUE
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (write == CTOOL_TRUE) {
    return (assembly->flags == CTOOL_C_ASSEMBLY_VOLATILE ||
            assembly->flags ==
                (CTOOL_C_ASSEMBLY_VOLATILE |
                 CTOOL_C_ASSEMBLY_MEMORY_CLOBBER)) &&
                   assembly->output_count == 0u &&
                   assembly->input_count == 1u &&
                   assembly->first_operand <
                       context->unit->assembly_operand_count &&
                   cemit_privileged_assembly_operand_matches(
                       context, &operands[0], "r", CTOOL_FALSE,
                       CTOOL_TRUE) == CTOOL_TRUE
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return context->unit->assembly_operand_count >= 3u &&
                 assembly->flags == CTOOL_C_ASSEMBLY_VOLATILE &&
                 assembly->output_count == 2u &&
                 assembly->input_count == 1u &&
                 assembly->first_operand <=
                     context->unit->assembly_operand_count - 3u &&
                 cemit_privileged_assembly_operand_matches(
                     context, &operands[0], "=a", CTOOL_TRUE,
                     CTOOL_FALSE) == CTOOL_TRUE &&
                 cemit_privileged_assembly_operand_matches(
                     context, &operands[1], "=d", CTOOL_TRUE,
                     CTOOL_FALSE) == CTOOL_TRUE &&
                 cemit_privileged_assembly_operand_matches(
                     context, &operands[2], "c", CTOOL_FALSE,
                     CTOOL_FALSE) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_privileged_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    cemit_privileged_assembly_kind_t kind) {
  ctool_u8 control_register;
  ctool_bool read;
  ctool_bool write;
  ctool_status_t status;
  if (cemit_privileged_assembly_metadata_is_valid(
          context, assembly, kind) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_EMIT_DIAG_UNSUPPORTED, &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  read = kind == CEMIT_PRIVILEGED_ASSEMBLY_READ_CR0 ||
                 kind == CEMIT_PRIVILEGED_ASSEMBLY_READ_CR2 ||
                 kind == CEMIT_PRIVILEGED_ASSEMBLY_READ_CR4
             ? CTOOL_TRUE
             : CTOOL_FALSE;
  write = kind == CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR0 ||
                  kind == CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR3 ||
                  kind == CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR4
              ? CTOOL_TRUE
              : CTOOL_FALSE;
  if (read == CTOOL_TRUE || write == CTOOL_TRUE) {
    control_register =
        kind == CEMIT_PRIVILEGED_ASSEMBLY_READ_CR0 ||
                kind == CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR0
            ? 0u
            : kind == CEMIT_PRIVILEGED_ASSEMBLY_READ_CR2
                  ? 2u
                  : kind == CEMIT_PRIVILEGED_ASSEMBLY_WRITE_CR3
                        ? 3u
                        : 4u;
    if (read == CTOOL_TRUE) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 1u,
          CTOOL_X86_REG_CONTROL, control_register, 32u);
      if (status == CTOOL_OK) {
        status = cemit_x86_one_register(
            context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_store_register_at_register(
            context, 0u, 1u);
      }
      return status;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_CONTROL,
          control_register, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_RDMSR);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_register_at_register(
        context, 1u, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_register_at_register(
        context, 1u, 0u);
  }
  return status;
}

typedef enum {
  CEMIT_STATE_MEMORY_NONE = 0,
  CEMIT_STATE_MEMORY_FNSTSW,
  CEMIT_STATE_MEMORY_FNSTCW,
  CEMIT_STATE_MEMORY_STMXCSR
} cemit_state_memory_kind_t;

static cemit_state_memory_kind_t cemit_state_memory_template_kind(
    ctool_string_t template_text) {
  if (cemit_string_equals_literal(
          template_text, "fnstsw %0") == CTOOL_TRUE) {
    return CEMIT_STATE_MEMORY_FNSTSW;
  }
  if (cemit_string_equals_literal(
          template_text, "fnstcw %0") == CTOOL_TRUE) {
    return CEMIT_STATE_MEMORY_FNSTCW;
  }
  return cemit_string_equals_literal(
             template_text, "stmxcsr %0") == CTOOL_TRUE
             ? CEMIT_STATE_MEMORY_STMXCSR
             : CEMIT_STATE_MEMORY_NONE;
}

static ctool_bool cemit_assembly_uses_state_memory_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_state_memory_template_kind(
                     assembly->template_text) !=
                     CEMIT_STATE_MEMORY_NONE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_state_memory_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    cemit_state_memory_kind_t kind) {
  const ctool_c_assembly_operand_t *operand;
  const ctool_c_type_layout_t *layout;
  const ctool_c_type_node_t *node;
  ctool_u32 qualifiers;
  ctool_u32 expected_size =
      kind == CEMIT_STATE_MEMORY_STMXCSR ? 4u : 2u;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      kind == CEMIT_STATE_MEMORY_NONE ||
      assembly->flags != CTOOL_C_ASSEMBLY_VOLATILE ||
      assembly->output_count != 1u ||
      assembly->input_count != 0u ||
      assembly->first_operand >=
          context->unit->assembly_operand_count ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  operand =
      &context->unit->assembly_operands[assembly->first_operand];
  if (operand->expression >= context->unit->expression_count ||
      operand->type >= context->unit->layout.type_count ||
      operand->matching_output != CTOOL_C_AST_NONE ||
      cemit_string_equals_literal(
          operand->constraint, "=m") == CTOOL_FALSE ||
      cemit_underlying_type(
          context, operand->type, &qualifiers, &node) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[operand->type];
  qualifiers |= node->qualifiers;
  return layout->is_integer == CTOOL_TRUE &&
                 layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size == expected_size &&
                 (qualifiers &
                  (CTOOL_C_QUAL_CONST | CTOOL_C_QUAL_ATOMIC)) == 0u &&
                 context->unit->expressions !=
                     (const ctool_c_expression_t *)0 &&
                 context->unit->expressions[operand->expression].type ==
                     operand->type
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_state_memory_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  cemit_state_memory_kind_t kind =
      cemit_state_memory_template_kind(assembly->template_text);
  ctool_x86_mnemonic_t mnemonic;
  ctool_u16 width_bits;
  ctool_status_t status;
  if (temporary_offset != 0u ||
      cemit_state_memory_metadata_is_valid(
          context, assembly, kind) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_EMIT_DIAG_UNSUPPORTED, &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  if (kind == CEMIT_STATE_MEMORY_FNSTSW) {
    mnemonic = CTOOL_X86_MN_FNSTSW;
    width_bits = 16u;
  } else if (kind == CEMIT_STATE_MEMORY_FNSTCW) {
    mnemonic = CTOOL_X86_MN_FNSTCW;
    width_bits = 16u;
  } else {
    mnemonic = CTOOL_X86_MN_STMXCSR;
    width_bits = 32u;
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_state_memory(
        context, mnemonic, 0u, width_bits);
  }
  return status;
}

typedef enum {
  CEMIT_PORT_IO_NONE = 0,
  CEMIT_PORT_IO_INB,
  CEMIT_PORT_IO_OUTB,
  CEMIT_PORT_IO_INW,
  CEMIT_PORT_IO_OUTW,
  CEMIT_PORT_IO_INL,
  CEMIT_PORT_IO_OUTL,
  CEMIT_PORT_IO_INSW,
  CEMIT_PORT_IO_OUTSW
} cemit_port_io_kind_t;

static cemit_port_io_kind_t cemit_port_io_template_kind(
    ctool_string_t template_text) {
  if (cemit_string_equals_literal(
          template_text, "in %%dx, %%al") == CTOOL_TRUE) {
    return CEMIT_PORT_IO_INB;
  }
  if (cemit_string_equals_literal(
          template_text, "inb %1, %0") == CTOOL_TRUE) {
    return CEMIT_PORT_IO_INB;
  }
  if (cemit_string_equals_literal(
          template_text, "out %%al, %%dx") == CTOOL_TRUE) {
    return CEMIT_PORT_IO_OUTB;
  }
  if (cemit_string_equals_literal(
          template_text, "outb %0, %1") == CTOOL_TRUE) {
    return CEMIT_PORT_IO_OUTB;
  }
  if (cemit_string_equals_literal(
          template_text, "in %%dx, %%ax") == CTOOL_TRUE) {
    return CEMIT_PORT_IO_INW;
  }
  if (cemit_string_equals_literal(
          template_text, "out %%ax, %%dx") == CTOOL_TRUE) {
    return CEMIT_PORT_IO_OUTW;
  }
  if (cemit_string_equals_literal(
          template_text, "in %%dx, %%eax") == CTOOL_TRUE) {
    return CEMIT_PORT_IO_INL;
  }
  if (cemit_string_equals_literal(
          template_text, "out %%eax, %%dx") == CTOOL_TRUE) {
    return CEMIT_PORT_IO_OUTL;
  }
  if (cemit_string_equals_literal(
          template_text, "cld; rep insw") == CTOOL_TRUE) {
    return CEMIT_PORT_IO_INSW;
  }
  if (cemit_string_equals_literal(
          template_text, "cld; rep outsw") == CTOOL_TRUE) {
    return CEMIT_PORT_IO_OUTSW;
  }
  return CEMIT_PORT_IO_NONE;
}

static ctool_bool cemit_assembly_uses_port_io_path(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  ctool_u32 operand_count;
  ctool_u32 operand;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0 ||
      cemit_add_overflows(
          assembly->output_count, assembly->input_count) == CTOOL_TRUE ||
      assembly->first_operand >
          context->unit->assembly_operand_count) {
    return CTOOL_FALSE;
  }
  operand_count = assembly->output_count + assembly->input_count;
  if (operand_count >
      context->unit->assembly_operand_count - assembly->first_operand) {
    return CTOOL_FALSE;
  }
  for (operand = 0u; operand < assembly->output_count; operand++) {
    const ctool_c_assembly_operand_t *candidate =
        &context->unit->assembly_operands[
            assembly->first_operand + operand];
    if (candidate->type < context->unit->layout.type_count &&
        cemit_string_equals_literal(
            candidate->constraint, "=a") == CTOOL_TRUE &&
        context->unit->layout.types[candidate->type].size != 4u) {
      return CTOOL_TRUE;
    }
    if (candidate->constraint.size != 0u &&
        candidate->constraint.data != (const char *)0 &&
        candidate->constraint.data[0] == '+') {
      return CTOOL_TRUE;
    }
  }
  for (operand = assembly->output_count;
       operand < operand_count; operand++) {
    if (context->unit->assembly_operands[
            assembly->first_operand + operand].matching_output ==
        CTOOL_C_AST_NONE) {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_bool cemit_assembly_uses_fxsave_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_string_equals_literal(
                     assembly->template_text, "fxsave (%0)") ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_fxsave_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  const ctool_c_assembly_operand_t *operand;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      assembly->flags !=
          (CTOOL_C_ASSEMBLY_VOLATILE |
           CTOOL_C_ASSEMBLY_MEMORY_CLOBBER) ||
      assembly->output_count != 0u ||
      assembly->input_count != 1u ||
      assembly->first_operand >=
          context->unit->assembly_operand_count ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  operand =
      &context->unit->assembly_operands[assembly->first_operand];
  return cemit_string_equals_literal(
             operand->constraint, "r") == CTOOL_TRUE &&
                 operand->matching_output == CTOOL_C_AST_NONE &&
                 operand->expression <
                     context->unit->expression_count &&
                 context->unit->expressions !=
                     (const ctool_c_expression_t *)0 &&
                 context->unit->expressions[operand->expression].type ==
                     operand->type &&
                 cemit_ir_type_is_i32_pointer(
                     context, operand->type) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_fxsave_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  ctool_status_t status;
  if (temporary_offset != 0u ||
      cemit_fxsave_metadata_is_valid(
          context, assembly) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_fxsave_memory(context, 0u);
  }
  return status;
}

static ctool_bool cemit_assembly_uses_ldmxcsr_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_string_equals_literal(
                     assembly->template_text, "ldmxcsr %0") ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ldmxcsr_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  const ctool_c_assembly_operand_t *operand;
  const ctool_c_type_layout_t *layout;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      assembly->flags != CTOOL_C_ASSEMBLY_VOLATILE ||
      assembly->output_count != 0u ||
      assembly->input_count != 1u ||
      assembly->first_operand >=
          context->unit->assembly_operand_count ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0 ||
      context->unit->expressions ==
          (const ctool_c_expression_t *)0) {
    return CTOOL_FALSE;
  }
  operand =
      &context->unit->assembly_operands[assembly->first_operand];
  if (operand->expression >= context->unit->expression_count ||
      operand->type >= context->unit->layout.type_count ||
      operand->matching_output != CTOOL_C_AST_NONE ||
      cemit_string_equals_literal(
          operand->constraint, "m") == CTOOL_FALSE ||
      context->unit->expressions[operand->expression].type !=
          operand->type ||
      cemit_type_has_atomic_qualification(
          context, operand->type) == CTOOL_TRUE) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[operand->type];
  return layout->is_integer == CTOOL_TRUE &&
                 layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size == 4u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_ldmxcsr_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  ctool_status_t status;
  if (temporary_offset != 0u ||
      cemit_ldmxcsr_metadata_is_valid(
          context, assembly) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_ldmxcsr_memory(context, 0u);
  }
  return status;
}

typedef enum {
  CEMIT_MOVSS_MEMORY_NONE = 0,
  CEMIT_MOVSS_MEMORY_ROUND_TRIP,
  CEMIT_MOVSS_MEMORY_LOAD,
  CEMIT_MOVSS_MEMORY_STORE
} cemit_movss_memory_kind_t;

typedef enum {
  CEMIT_KERNEL_SIMD_NONE = 0,
  CEMIT_KERNEL_SIMD_COPY_64,
  CEMIT_KERNEL_SIMD_COPY_16,
  CEMIT_KERNEL_SIMD_BROADCAST,
  CEMIT_KERNEL_SIMD_STORE_16,
  CEMIT_KERNEL_SIMD_BLEND_16,
  CEMIT_KERNEL_SIMD_ADD_16
} cemit_kernel_simd_kind_t;

static cemit_kernel_simd_kind_t cemit_kernel_simd_template_kind(
    ctool_string_t template_text) {
  if (cemit_string_equals_literal(
          template_text,
          "movdqu   (%1), %%xmm0\n\t"
          "movdqu 16(%1), %%xmm1\n\t"
          "movdqu 32(%1), %%xmm2\n\t"
          "movdqu 48(%1), %%xmm3\n\t"
          "movntdq %%xmm0,   (%0)\n\t"
          "movntdq %%xmm1, 16(%0)\n\t"
          "movntdq %%xmm2, 32(%0)\n\t"
          "movntdq %%xmm3, 48(%0)\n\t") == CTOOL_TRUE) {
    return CEMIT_KERNEL_SIMD_COPY_64;
  }
  if (cemit_string_equals_literal(
          template_text,
          "movdqu (%1), %%xmm0\n\t"
          "movntdq %%xmm0, (%0)\n\t") == CTOOL_TRUE) {
    return CEMIT_KERNEL_SIMD_COPY_16;
  }
  if (cemit_string_equals_literal(
          template_text,
          "movd %0, %%xmm0\n\t"
          "pshufd $0x00, %%xmm0, %%xmm0\n\t") == CTOOL_TRUE) {
    return CEMIT_KERNEL_SIMD_BROADCAST;
  }
  if (cemit_string_equals_literal(
          template_text,
          "movntdq %%xmm0, (%0)\n\t") == CTOOL_TRUE) {
    return CEMIT_KERNEL_SIMD_STORE_16;
  }
  if (cemit_string_equals_literal(
          template_text,
          "movd %2, %%xmm5\n\t"
          "movd %3, %%xmm6\n\t"
          "movd %4, %%xmm7\n\t"
          "punpcklwd %%xmm5, %%xmm5\n\t"
          "punpcklwd %%xmm6, %%xmm6\n\t"
          "punpcklwd %%xmm7, %%xmm7\n\t"
          "pshufd $0x00, %%xmm5, %%xmm5\n\t"
          "pshufd $0x00, %%xmm6, %%xmm6\n\t"
          "pshufd $0x00, %%xmm7, %%xmm7\n\t"
          "pxor %%xmm4, %%xmm4\n\t"
          "movdqu (%1), %%xmm0\n\t"
          "movdqu (%0), %%xmm1\n\t"
          "movdqa %%xmm0, %%xmm2\n\t"
          "punpcklbw %%xmm4, %%xmm2\n\t"
          "movdqa %%xmm1, %%xmm3\n\t"
          "punpcklbw %%xmm4, %%xmm3\n\t"
          "pmullw %%xmm5, %%xmm2\n\t"
          "pmullw %%xmm6, %%xmm3\n\t"
          "paddw %%xmm3, %%xmm2\n\t"
          "paddw %%xmm7, %%xmm2\n\t"
          "psrlw $8, %%xmm2\n\t"
          "punpckhbw %%xmm4, %%xmm0\n\t"
          "punpckhbw %%xmm4, %%xmm1\n\t"
          "pmullw %%xmm5, %%xmm0\n\t"
          "pmullw %%xmm6, %%xmm1\n\t"
          "paddw %%xmm1, %%xmm0\n\t"
          "paddw %%xmm7, %%xmm0\n\t"
          "psrlw $8, %%xmm0\n\t"
          "packuswb %%xmm0, %%xmm2\n\t"
          "movdqu %%xmm2, (%0)\n\t") == CTOOL_TRUE) {
    return CEMIT_KERNEL_SIMD_BLEND_16;
  }
  return cemit_string_equals_literal(
             template_text,
             "movdqu (%1), %%xmm0\n\t"
             "movdqu (%0), %%xmm1\n\t"
             "paddusb %%xmm0, %%xmm1\n\t"
             "movdqu %%xmm1, (%0)\n\t") == CTOOL_TRUE
             ? CEMIT_KERNEL_SIMD_ADD_16
             : CEMIT_KERNEL_SIMD_NONE;
}

static ctool_bool cemit_assembly_uses_kernel_simd_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_kernel_simd_template_kind(
                     assembly->template_text) != CEMIT_KERNEL_SIMD_NONE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static cemit_movss_memory_kind_t cemit_movss_memory_template_kind(
    ctool_string_t template_text) {
  if (cemit_string_equals_literal(
          template_text,
          "movss %1, %%xmm0\n\tmovss %%xmm0, %0\n\t") == CTOOL_TRUE) {
    return CEMIT_MOVSS_MEMORY_ROUND_TRIP;
  }
  if (cemit_string_equals_literal(
          template_text, "movss %0, %%xmm0") == CTOOL_TRUE) {
    return CEMIT_MOVSS_MEMORY_LOAD;
  }
  return cemit_string_equals_literal(
             template_text, "movss %%xmm0, %0") == CTOOL_TRUE
             ? CEMIT_MOVSS_MEMORY_STORE
             : CEMIT_MOVSS_MEMORY_NONE;
}

static ctool_bool cemit_assembly_uses_movss_memory_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_movss_memory_template_kind(
                     assembly->template_text) != CEMIT_MOVSS_MEMORY_NONE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_assembly_uses_sqrtsd_register_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_string_equals_literal(
                     assembly->template_text, "sqrtsd %1, %0") ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_assembly_uses_x87_atan2_memory_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_string_equals_literal(
                     assembly->template_text,
                     "fldl  %1\n\t"
                     "fldl  %2\n\t"
                     "fpatan\n\t"
                     "fstpl %0\n\t") == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_assembly_uses_x87_exp_memory_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_string_equals_literal(
                     assembly->template_text,
                     "fldl   %1\n\t"
                     "fldl   %2\n\t"
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
                     "fstpl  %0\n\t") == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_movss_memory_operand_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_operand_t *operand,
    const char *constraint, ctool_bool output) {
  const ctool_c_type_node_t *node;
  const ctool_c_type_layout_t *layout;
  ctool_u32 qualifiers;
  if (operand == (const ctool_c_assembly_operand_t *)0 ||
      operand->type >= context->unit->layout.type_count ||
      operand->expression >= context->unit->expression_count ||
      operand->matching_output != CTOOL_C_AST_NONE ||
      context->unit->expressions == (const ctool_c_expression_t *)0 ||
      context->unit->expressions[operand->expression].type != operand->type ||
      cemit_string_equals_literal(
          operand->constraint, constraint) == CTOOL_FALSE ||
      cemit_underlying_type(
          context, operand->type, &qualifiers, &node) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[operand->type];
  return node->kind == CTOOL_C_TYPE_FLOAT &&
                 layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size == 4u &&
                 (qualifiers & CTOOL_C_QUAL_ATOMIC) == 0u &&
                 (output == CTOOL_FALSE ||
                  (qualifiers & CTOOL_C_QUAL_CONST) == 0u)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_movss_memory_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    cemit_movss_memory_kind_t kind) {
  const ctool_c_assembly_operand_t *output;
  const ctool_c_assembly_operand_t *input;
  ctool_u32 expected_outputs;
  ctool_u32 expected_inputs;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      kind == CEMIT_MOVSS_MEMORY_NONE) {
    return CTOOL_FALSE;
  }
  expected_outputs =
      kind == CEMIT_MOVSS_MEMORY_LOAD ? 0u : 1u;
  expected_inputs =
      kind == CEMIT_MOVSS_MEMORY_STORE ? 0u : 1u;
  if (assembly->flags !=
          (CTOOL_C_ASSEMBLY_VOLATILE |
           CTOOL_C_ASSEMBLY_XMM0_CLOBBER) ||
      assembly->output_count != expected_outputs ||
      assembly->input_count != expected_inputs ||
      assembly->first_operand > context->unit->assembly_operand_count ||
      expected_outputs + expected_inputs >
          context->unit->assembly_operand_count - assembly->first_operand ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  output = expected_outputs == 0u
               ? (const ctool_c_assembly_operand_t *)0
               : &context->unit
                      ->assembly_operands[assembly->first_operand];
  input = expected_inputs == 0u
              ? (const ctool_c_assembly_operand_t *)0
              : &context->unit->assembly_operands[
                    assembly->first_operand + expected_outputs];
  return (output == (const ctool_c_assembly_operand_t *)0 ||
          cemit_movss_memory_operand_is_valid(
              context, output, "=m", CTOOL_TRUE) == CTOOL_TRUE) &&
                 (input == (const ctool_c_assembly_operand_t *)0 ||
                  cemit_movss_memory_operand_is_valid(
                      context, input, "m", CTOOL_FALSE) == CTOOL_TRUE)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_movss_memory_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  cemit_movss_memory_kind_t kind =
      cemit_movss_memory_template_kind(assembly->template_text);
  ctool_bool has_input =
      kind != CEMIT_MOVSS_MEMORY_STORE ? CTOOL_TRUE : CTOOL_FALSE;
  ctool_bool has_output =
      kind != CEMIT_MOVSS_MEMORY_LOAD ? CTOOL_TRUE : CTOOL_FALSE;
  ctool_status_t status = CTOOL_OK;
  if (temporary_offset != 0u ||
      cemit_movss_memory_metadata_is_valid(
          context, assembly, kind) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  if (has_input == CTOOL_TRUE) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_sse_memory(
          context, CTOOL_X86_MN_MOVSS, CTOOL_TRUE,
          0u, 0u, 0, 32u);
    }
  }
  if (status == CTOOL_OK && has_output == CTOOL_TRUE) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_sse_memory(
          context, CTOOL_X86_MN_MOVSS, CTOOL_FALSE,
          0u, 0u, 0, 32u);
    }
  }
  return status;
}

static ctool_bool cemit_kernel_simd_operand_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_operand_t *operand, ctool_bool pointer) {
  const ctool_c_type_node_t *node;
  const ctool_c_type_node_t *referent;
  const ctool_c_type_layout_t *layout;
  ctool_u32 qualifiers;
  ctool_u32 referent_qualifiers;
  if (operand == (const ctool_c_assembly_operand_t *)0 ||
      operand->type >= context->unit->layout.type_count ||
      operand->expression >= context->unit->expression_count ||
      operand->matching_output != CTOOL_C_AST_NONE ||
      context->unit->expressions == (const ctool_c_expression_t *)0 ||
      context->unit->expressions[operand->expression].type != operand->type ||
      cemit_string_equals_literal(
          operand->constraint, "r") == CTOOL_FALSE ||
      cemit_underlying_type(
          context, operand->type, &qualifiers, &node) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[operand->type];
  if (pointer == CTOOL_FALSE) {
    return layout->is_integer == CTOOL_TRUE &&
                   layout->is_object == CTOOL_TRUE &&
                   layout->is_complete_object == CTOOL_TRUE &&
                   layout->size == 4u
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (node->kind != CTOOL_C_TYPE_POINTER ||
      cemit_underlying_type(
          context, node->referenced_type, &referent_qualifiers,
          &referent) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  (void)qualifiers;
  (void)referent_qualifiers;
  return referent->kind != CTOOL_C_TYPE_FUNCTION &&
                 layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size == 4u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_kernel_simd_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    cemit_kernel_simd_kind_t kind) {
  ctool_u32 expected_flags = CTOOL_C_ASSEMBLY_VOLATILE;
  ctool_u32 expected_inputs = 1u;
  ctool_u32 pointer_inputs = 0u;
  ctool_u32 input;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      kind == CEMIT_KERNEL_SIMD_NONE) {
    return CTOOL_FALSE;
  }
  if (kind == CEMIT_KERNEL_SIMD_COPY_64) {
    expected_flags |= CTOOL_C_ASSEMBLY_MEMORY_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM0_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM1_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM2_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM3_CLOBBER;
    expected_inputs = 2u;
    pointer_inputs = 2u;
  } else if (kind == CEMIT_KERNEL_SIMD_COPY_16) {
    expected_flags |= CTOOL_C_ASSEMBLY_MEMORY_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM0_CLOBBER;
    expected_inputs = 2u;
    pointer_inputs = 2u;
  } else if (kind == CEMIT_KERNEL_SIMD_BROADCAST) {
    expected_flags |= CTOOL_C_ASSEMBLY_XMM0_CLOBBER;
  } else if (kind == CEMIT_KERNEL_SIMD_STORE_16) {
    expected_flags |= CTOOL_C_ASSEMBLY_MEMORY_CLOBBER;
    pointer_inputs = 1u;
  } else if (kind == CEMIT_KERNEL_SIMD_BLEND_16) {
    expected_flags |= CTOOL_C_ASSEMBLY_MEMORY_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM0_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM1_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM2_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM3_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM4_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM5_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM6_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM7_CLOBBER;
    expected_inputs = 5u;
    pointer_inputs = 2u;
  } else {
    expected_flags |= CTOOL_C_ASSEMBLY_MEMORY_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM0_CLOBBER |
                      CTOOL_C_ASSEMBLY_XMM1_CLOBBER;
    expected_inputs = 2u;
    pointer_inputs = 2u;
  }
  if (assembly->flags != expected_flags ||
      assembly->output_count != 0u ||
      assembly->input_count != expected_inputs ||
      assembly->first_operand > context->unit->assembly_operand_count ||
      expected_inputs >
          context->unit->assembly_operand_count - assembly->first_operand ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  for (input = 0u; input < expected_inputs; input++) {
    if (cemit_kernel_simd_operand_is_valid(
            context,
            &context->unit->assembly_operands[
                assembly->first_operand + input],
            input < pointer_inputs ? CTOOL_TRUE : CTOOL_FALSE) ==
        CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t cemit_kernel_simd_pop_pointer_pair(
    cemit_context_t *context) {
  ctool_status_t status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 2u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_emit_kernel_simd_copy(
    cemit_context_t *context, cemit_kernel_simd_kind_t kind) {
  ctool_u32 lanes =
      kind == CEMIT_KERNEL_SIMD_COPY_64 ? 4u : 1u;
  ctool_u32 lane;
  ctool_status_t status = cemit_kernel_simd_pop_pointer_pair(context);
  for (lane = 0u; status == CTOOL_OK && lane < lanes; lane++) {
    status = cemit_x86_simd_memory(
        context, CTOOL_X86_MN_MOVDQU, CTOOL_TRUE,
        (ctool_u8)lane, 2u, (ctool_i32)(lane * 16u), 128u);
  }
  for (lane = 0u; status == CTOOL_OK && lane < lanes; lane++) {
    status = cemit_x86_simd_memory(
        context, CTOOL_X86_MN_MOVNTDQ, CTOOL_FALSE,
        (ctool_u8)lane, 0u, (ctool_i32)(lane * 16u), 128u);
  }
  return status;
}

static ctool_status_t cemit_emit_kernel_simd_broadcast(
    cemit_context_t *context) {
  ctool_status_t status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOVD, CTOOL_X86_REG_XMM, 0u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_xmm_shuffle_immediate(
        context, 0u, 0u, 0u);
  }
  return status;
}

static ctool_status_t cemit_emit_kernel_simd_store(
    cemit_context_t *context) {
  ctool_status_t status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_simd_memory(
        context, CTOOL_X86_MN_MOVNTDQ, CTOOL_FALSE,
        0u, 0u, 0, 128u);
  }
  return status;
}

static ctool_status_t cemit_emit_kernel_simd_blend(
    cemit_context_t *context) {
  ctool_status_t status = cemit_x86_simd_memory(
      context, CTOOL_X86_MN_MOVD, CTOOL_TRUE, 7u, 4u, 0, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_simd_memory(
        context, CTOOL_X86_MN_MOVD, CTOOL_TRUE, 6u, 4u, 4, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_simd_memory(
        context, CTOOL_X86_MN_MOVD, CTOOL_TRUE, 5u, 4u, 8, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_discard_arguments(context, 12u);
  }
  if (status == CTOOL_OK) {
    status = cemit_kernel_simd_pop_pointer_pair(context);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PUNPCKLWD, CTOOL_X86_REG_XMM, 5u,
        CTOOL_X86_REG_XMM, 5u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PUNPCKLWD, CTOOL_X86_REG_XMM, 6u,
        CTOOL_X86_REG_XMM, 6u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PUNPCKLWD, CTOOL_X86_REG_XMM, 7u,
        CTOOL_X86_REG_XMM, 7u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_xmm_shuffle_immediate(
        context, 5u, 5u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_xmm_shuffle_immediate(
        context, 6u, 6u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_xmm_shuffle_immediate(
        context, 7u, 7u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PXOR, CTOOL_X86_REG_XMM, 4u,
        CTOOL_X86_REG_XMM, 4u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_simd_memory(
        context, CTOOL_X86_MN_MOVDQU, CTOOL_TRUE,
        0u, 2u, 0, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_simd_memory(
        context, CTOOL_X86_MN_MOVDQU, CTOOL_TRUE,
        1u, 0u, 0, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOVDQA, CTOOL_X86_REG_XMM, 2u,
        CTOOL_X86_REG_XMM, 0u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PUNPCKLBW, CTOOL_X86_REG_XMM, 2u,
        CTOOL_X86_REG_XMM, 4u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOVDQA, CTOOL_X86_REG_XMM, 3u,
        CTOOL_X86_REG_XMM, 1u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PUNPCKLBW, CTOOL_X86_REG_XMM, 3u,
        CTOOL_X86_REG_XMM, 4u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PMULLW, CTOOL_X86_REG_XMM, 2u,
        CTOOL_X86_REG_XMM, 5u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PMULLW, CTOOL_X86_REG_XMM, 3u,
        CTOOL_X86_REG_XMM, 6u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PADDW, CTOOL_X86_REG_XMM, 2u,
        CTOOL_X86_REG_XMM, 3u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PADDW, CTOOL_X86_REG_XMM, 2u,
        CTOOL_X86_REG_XMM, 7u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_xmm_shift_immediate(context, 2u, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PUNPCKHBW, CTOOL_X86_REG_XMM, 0u,
        CTOOL_X86_REG_XMM, 4u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PUNPCKHBW, CTOOL_X86_REG_XMM, 1u,
        CTOOL_X86_REG_XMM, 4u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PMULLW, CTOOL_X86_REG_XMM, 0u,
        CTOOL_X86_REG_XMM, 5u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PMULLW, CTOOL_X86_REG_XMM, 1u,
        CTOOL_X86_REG_XMM, 6u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PADDW, CTOOL_X86_REG_XMM, 0u,
        CTOOL_X86_REG_XMM, 1u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PADDW, CTOOL_X86_REG_XMM, 0u,
        CTOOL_X86_REG_XMM, 7u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_xmm_shift_immediate(context, 0u, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PACKUSWB, CTOOL_X86_REG_XMM, 2u,
        CTOOL_X86_REG_XMM, 0u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_simd_memory(
        context, CTOOL_X86_MN_MOVDQU, CTOOL_FALSE,
        2u, 0u, 0, 128u);
  }
  return status;
}

static ctool_status_t cemit_emit_kernel_simd_add(
    cemit_context_t *context) {
  ctool_status_t status = cemit_kernel_simd_pop_pointer_pair(context);
  if (status == CTOOL_OK) {
    status = cemit_x86_simd_memory(
        context, CTOOL_X86_MN_MOVDQU, CTOOL_TRUE,
        0u, 2u, 0, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_simd_memory(
        context, CTOOL_X86_MN_MOVDQU, CTOOL_TRUE,
        1u, 0u, 0, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_PADDUSB, CTOOL_X86_REG_XMM, 1u,
        CTOOL_X86_REG_XMM, 0u, 128u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_simd_memory(
        context, CTOOL_X86_MN_MOVDQU, CTOOL_FALSE,
        1u, 0u, 0, 128u);
  }
  return status;
}

static ctool_status_t cemit_emit_kernel_simd_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  cemit_kernel_simd_kind_t kind =
      cemit_kernel_simd_template_kind(assembly->template_text);
  if (temporary_offset != 0u ||
      cemit_kernel_simd_metadata_is_valid(
          context, assembly, kind) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  if (kind == CEMIT_KERNEL_SIMD_COPY_64 ||
      kind == CEMIT_KERNEL_SIMD_COPY_16) {
    return cemit_emit_kernel_simd_copy(context, kind);
  }
  if (kind == CEMIT_KERNEL_SIMD_BROADCAST) {
    return cemit_emit_kernel_simd_broadcast(context);
  }
  if (kind == CEMIT_KERNEL_SIMD_STORE_16) {
    return cemit_emit_kernel_simd_store(context);
  }
  if (kind == CEMIT_KERNEL_SIMD_BLEND_16) {
    return cemit_emit_kernel_simd_blend(context);
  }
  return cemit_emit_kernel_simd_add(context);
}

static ctool_bool cemit_assembly_uses_x87_sine_memory_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_string_equals_literal(
                     assembly->template_text,
                     "fldl %1\n\tfsin\n\tfstpl %0\n\t") == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_assembly_uses_x87_round_down_memory_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_string_equals_literal(
                     assembly->template_text,
                     "fldl %1\n\t"
                     "fnstcw -2(%%esp)\n\t"
                     "movw  -2(%%esp), %%ax\n\t"
                     "movw  %%ax, -4(%%esp)\n\t"
                     "andw  $0xF3FF, -4(%%esp)\n\t"
                     "orw   $0x0400, -4(%%esp)\n\t"
                     "fldcw -4(%%esp)\n\t"
                     "frndint\n\t"
                     "fldcw -2(%%esp)\n\t"
                     "fstpl %0\n\t") == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_assembly_uses_x87_pow_memory_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_string_equals_literal(
                     assembly->template_text,
                     "fldl   %3\n\t"
                     "fldl   %1\n\t"
                     "fyl2x\n\t"
                     "fldl   %2\n\t"
                     "fmulp\n\t"
                     "fldl   %4\n\t"
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
                     "fstpl  %0\n\t") == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_assembly_uses_x87_powf_memory_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_string_equals_literal(
                     assembly->template_text,
                     "fldl   %3\n\t"
                     "flds   %1\n\t"
                     "fyl2x\n\t"
                     "flds   %2\n\t"
                     "fmulp\n\t"
                     "fldl   %4\n\t"
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
                     "fstps  %0\n\t") == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_x87_double_memory_operand_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_operand_t *operand,
    const char *constraint, ctool_bool output) {
  const ctool_c_type_node_t *node;
  const ctool_c_type_layout_t *layout;
  ctool_u32 qualifiers;
  if (operand == (const ctool_c_assembly_operand_t *)0 ||
      operand->type >= context->unit->layout.type_count ||
      operand->expression >= context->unit->expression_count ||
      operand->matching_output != CTOOL_C_AST_NONE ||
      context->unit->expressions == (const ctool_c_expression_t *)0 ||
      context->unit->expressions[operand->expression].type != operand->type ||
      cemit_string_equals_literal(
          operand->constraint, constraint) == CTOOL_FALSE ||
      cemit_underlying_type(
          context, operand->type, &qualifiers, &node) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[operand->type];
  return node->kind == CTOOL_C_TYPE_DOUBLE &&
                 layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size == 8u &&
                 (qualifiers & CTOOL_C_QUAL_ATOMIC) == 0u &&
                 (output == CTOOL_FALSE ||
                  (qualifiers & CTOOL_C_QUAL_CONST) == 0u)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_sqrtsd_register_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  const ctool_c_assembly_operand_t *output;
  const ctool_c_assembly_operand_t *input;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      cemit_assembly_uses_sqrtsd_register_path(
          assembly) == CTOOL_FALSE ||
      assembly->flags != CTOOL_C_ASSEMBLY_VOLATILE ||
      assembly->output_count != 1u ||
      assembly->input_count != 1u ||
      assembly->first_operand > context->unit->assembly_operand_count ||
      2u > context->unit->assembly_operand_count -
               assembly->first_operand ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  output =
      &context->unit->assembly_operands[assembly->first_operand];
  input = &context->unit
               ->assembly_operands[assembly->first_operand + 1u];
  return cemit_x87_double_memory_operand_is_valid(
             context, output, "=x", CTOOL_TRUE) == CTOOL_TRUE &&
                 cemit_x87_double_memory_operand_is_valid(
                     context, input, "x", CTOOL_FALSE) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_sqrtsd_register_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  const ctool_c_assembly_operand_t *input;
  ctool_status_t status;
  if (temporary_offset != 0u ||
      cemit_sqrtsd_register_metadata_is_valid(
          context, assembly) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  input = &context->unit
               ->assembly_operands[assembly->first_operand + 1u];
  status = cemit_x86_load_floating_xmm_stack_value(
      context, input->type, 0u);
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_SQRTSD, CTOOL_X86_REG_XMM, 0u,
        CTOOL_X86_REG_XMM, 0u, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_sse_memory(
        context, CTOOL_X86_MN_MOVSD, CTOOL_FALSE,
        0u, 0u, 0, 64u);
  }
  return status;
}

static ctool_bool cemit_x87_atan2_memory_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  const ctool_c_assembly_operand_t *operand;
  ctool_u32 index;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      cemit_assembly_uses_x87_atan2_memory_path(
          assembly) == CTOOL_FALSE ||
      assembly->flags !=
          (CTOOL_C_ASSEMBLY_VOLATILE |
           CTOOL_C_ASSEMBLY_MEMORY_CLOBBER) ||
      assembly->output_count != 1u ||
      assembly->input_count != 2u ||
      assembly->first_operand > context->unit->assembly_operand_count ||
      3u > context->unit->assembly_operand_count -
               assembly->first_operand ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < 3u; index++) {
    operand = &context->unit
                   ->assembly_operands[assembly->first_operand + index];
    if (cemit_x87_double_memory_operand_is_valid(
            context, operand, index == 0u ? "=m" : "m",
            index == 0u ? CTOOL_TRUE : CTOOL_FALSE) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t cemit_emit_x87_atan2_memory_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  ctool_status_t status;
  if (temporary_offset != 0u ||
      cemit_x87_atan2_memory_metadata_is_valid(
          context, assembly) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  status = cemit_x86_load_stack(context, 0u, 4u);
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 0u, 0, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 0u, 0, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FPATAN);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FSTP, 0u, 0, 64u);
  }
  return status;
}

static ctool_bool cemit_x87_exp_memory_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  const ctool_c_assembly_operand_t *operand;
  ctool_u32 index;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      cemit_assembly_uses_x87_exp_memory_path(
          assembly) == CTOOL_FALSE ||
      assembly->flags !=
          (CTOOL_C_ASSEMBLY_VOLATILE |
           CTOOL_C_ASSEMBLY_MEMORY_CLOBBER) ||
      assembly->output_count != 1u ||
      assembly->input_count != 2u ||
      assembly->first_operand > context->unit->assembly_operand_count ||
      3u > context->unit->assembly_operand_count -
               assembly->first_operand ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < 3u; index++) {
    operand = &context->unit
                   ->assembly_operands[assembly->first_operand + index];
    if (cemit_x87_double_memory_operand_is_valid(
            context, operand, index == 0u ? "=m" : "m",
            index == 0u ? CTOOL_TRUE : CTOOL_FALSE) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t cemit_emit_x87_exp_memory_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  ctool_status_t status;
  if (temporary_offset != 0u ||
      cemit_x87_exp_memory_metadata_is_valid(
          context, assembly) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  status = cemit_x86_load_stack(context, 0u, 4u);
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 0u, 0, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 0u, 0, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FMULP, CTOOL_X86_REG_X87, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FLD, CTOOL_X86_REG_X87, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FRNDINT);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_FSUBR,
        CTOOL_X86_REG_X87, 1u, CTOOL_X86_REG_X87, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FXCH, CTOOL_X86_REG_X87, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_F2XM1);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FLD1);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FADDP, CTOOL_X86_REG_X87, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FSCALE);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FSTP, CTOOL_X86_REG_X87, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FSTP, 0u, 0, 64u);
  }
  return status;
}

static ctool_bool cemit_x87_sine_memory_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  const ctool_c_assembly_operand_t *output;
  const ctool_c_assembly_operand_t *input;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      cemit_assembly_uses_x87_sine_memory_path(
          assembly) == CTOOL_FALSE ||
      assembly->flags != CTOOL_C_ASSEMBLY_VOLATILE ||
      assembly->output_count != 1u ||
      assembly->input_count != 1u ||
      assembly->first_operand > context->unit->assembly_operand_count ||
      2u > context->unit->assembly_operand_count -
               assembly->first_operand ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  output = &context->unit->assembly_operands[assembly->first_operand];
  input = &context->unit
               ->assembly_operands[assembly->first_operand + 1u];
  return cemit_x87_double_memory_operand_is_valid(
             context, output, "=m", CTOOL_TRUE) == CTOOL_TRUE &&
                 cemit_x87_double_memory_operand_is_valid(
                     context, input, "m", CTOOL_FALSE) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_x87_sine_memory_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  ctool_status_t status;
  if (temporary_offset != 0u ||
      cemit_x87_sine_memory_metadata_is_valid(
          context, assembly) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 0u, 0, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FSIN);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FSTP, 0u, 0, 64u);
  }
  return status;
}

static ctool_bool cemit_x87_round_down_memory_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  const ctool_c_assembly_operand_t *output;
  const ctool_c_assembly_operand_t *input;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      cemit_assembly_uses_x87_round_down_memory_path(
          assembly) == CTOOL_FALSE ||
      assembly->flags !=
          (CTOOL_C_ASSEMBLY_VOLATILE |
           CTOOL_C_ASSEMBLY_MEMORY_CLOBBER |
           CTOOL_C_ASSEMBLY_AX_CLOBBER) ||
      assembly->output_count != 1u ||
      assembly->input_count != 1u ||
      assembly->first_operand > context->unit->assembly_operand_count ||
      2u > context->unit->assembly_operand_count -
               assembly->first_operand ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  output = &context->unit->assembly_operands[assembly->first_operand];
  input = &context->unit
               ->assembly_operands[assembly->first_operand + 1u];
  return cemit_x87_double_memory_operand_is_valid(
             context, output, "=m", CTOOL_TRUE) == CTOOL_TRUE &&
                 cemit_x87_double_memory_operand_is_valid(
                     context, input, "m", CTOOL_FALSE) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_x87_round_down_memory_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  ctool_status_t status;
  if (temporary_offset != 0u ||
      cemit_x87_round_down_memory_metadata_is_valid(
          context, assembly) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 0u, 0, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_control_memory(
        context, CTOOL_X86_MN_FNSTCW, 4u, -2);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_word_stack_ax(context, CTOOL_TRUE, -2);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_word_stack_ax(context, CTOOL_FALSE, -4);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_word_stack_immediate(
        context, CTOOL_X86_MN_AND, -4, 0xf3ffu);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_word_stack_immediate(
        context, CTOOL_X86_MN_OR, -4, 0x0400u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_control_memory(
        context, CTOOL_X86_MN_FLDCW, 4u, -4);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FRNDINT);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_control_memory(
        context, CTOOL_X86_MN_FLDCW, 4u, -2);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FSTP, 0u, 0, 64u);
  }
  return status;
}

static ctool_bool cemit_x87_pow_memory_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  const ctool_c_assembly_operand_t *operand;
  ctool_u32 index;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      cemit_assembly_uses_x87_pow_memory_path(
          assembly) == CTOOL_FALSE ||
      assembly->flags !=
          (CTOOL_C_ASSEMBLY_VOLATILE |
           CTOOL_C_ASSEMBLY_MEMORY_CLOBBER) ||
      assembly->output_count != 1u ||
      assembly->input_count != 4u ||
      assembly->first_operand > context->unit->assembly_operand_count ||
      5u > context->unit->assembly_operand_count -
               assembly->first_operand ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < 5u; index++) {
    operand = &context->unit
                   ->assembly_operands[assembly->first_operand + index];
    if (cemit_x87_double_memory_operand_is_valid(
            context, operand, index == 0u ? "=m" : "m",
            index == 0u ? CTOOL_TRUE : CTOOL_FALSE) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool cemit_x87_powf_memory_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  const ctool_c_assembly_operand_t *operand;
  ctool_u32 index;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      cemit_assembly_uses_x87_powf_memory_path(
          assembly) == CTOOL_FALSE ||
      assembly->flags !=
          (CTOOL_C_ASSEMBLY_VOLATILE |
           CTOOL_C_ASSEMBLY_MEMORY_CLOBBER) ||
      assembly->output_count != 1u ||
      assembly->input_count != 4u ||
      assembly->first_operand > context->unit->assembly_operand_count ||
      5u > context->unit->assembly_operand_count -
               assembly->first_operand ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < 5u; index++) {
    operand = &context->unit
                   ->assembly_operands[assembly->first_operand + index];
    if ((index <= 2u
             ? cemit_movss_memory_operand_is_valid(
                   context, operand, index == 0u ? "=m" : "m",
                   index == 0u ? CTOOL_TRUE : CTOOL_FALSE)
             : cemit_x87_double_memory_operand_is_valid(
                   context, operand, "m", CTOOL_FALSE)) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t cemit_emit_x87_pow_sequence(
    cemit_context_t *context, ctool_u16 value_width_bits,
    ctool_u16 output_width_bits) {
  ctool_status_t status;
  if ((value_width_bits != 32u && value_width_bits != 64u) ||
      (output_width_bits != 32u && output_width_bits != 64u)) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_load_stack(context, 0u, 4u);
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 0u, 0, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(context, 0u, 12u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 0u, 0, value_width_bits);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FYL2X);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(context, 0u, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 0u, 0, value_width_bits);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FMULP, CTOOL_X86_REG_X87, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(context, 0u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 0u, 0, 64u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FMULP, CTOOL_X86_REG_X87, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FLD, CTOOL_X86_REG_X87, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FRNDINT);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_FSUBR,
        CTOOL_X86_REG_X87, 1u, CTOOL_X86_REG_X87, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FXCH, CTOOL_X86_REG_X87, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_F2XM1);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FLD1);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FADDP, CTOOL_X86_REG_X87, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FSCALE);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FSTP, CTOOL_X86_REG_X87, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(context, 0u, 16u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FSTP, 0u, 0, output_width_bits);
  }
  return status == CTOOL_OK
             ? cemit_x86_add_register_constant(context, 4u, 20u)
             : status;
}

static ctool_status_t cemit_emit_x87_pow_memory_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  if (temporary_offset != 0u ||
      cemit_x87_pow_memory_metadata_is_valid(
          context, assembly) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  return cemit_emit_x87_pow_sequence(context, 64u, 64u);
}

static ctool_status_t cemit_emit_x87_powf_memory_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  if (temporary_offset != 0u ||
      cemit_x87_powf_memory_metadata_is_valid(
          context, assembly) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  return cemit_emit_x87_pow_sequence(context, 32u, 32u);
}

typedef enum {
  CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_NONE = 0,
  CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_DATA,
  CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_RELOAD_CODE,
  CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_ALL,
  CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_GS
} cemit_descriptor_table_assembly_kind_t;

static cemit_descriptor_table_assembly_kind_t
cemit_descriptor_table_assembly_kind(ctool_string_t template_text) {
  if (cemit_string_equals_literal(
          template_text,
          "lgdt %0\n"
          "mov $0x10, %%ax\n"
          "mov %%ax, %%ds\n"
          "mov %%ax, %%es\n"
          "mov %%ax, %%ss\n") == CTOOL_TRUE) {
    return CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_DATA;
  }
  if (cemit_string_equals_literal(
          template_text,
          "pushl $0x08\n"
          "pushl $1f\n"
          "lretl\n"
          "1:\n") == CTOOL_TRUE) {
    return CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_RELOAD_CODE;
  }
  if (cemit_string_equals_literal(
          template_text,
          "lgdt %0\n"
          "mov $0x10, %%ax\n"
          "mov %%ax, %%ds\n"
          "mov %%ax, %%es\n"
          "mov %%ax, %%ss\n"
          "ljmp $0x08, $1f\n"
          "1:\n") == CTOOL_TRUE) {
    return CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_ALL;
  }
  return cemit_string_equals_literal(
             template_text, "mov %0, %%gs") == CTOOL_TRUE
             ? CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_GS
             : CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_NONE;
}

static ctool_bool cemit_assembly_uses_descriptor_table_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_descriptor_table_assembly_kind(
                     assembly->template_text) !=
                     CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_NONE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_descriptor_table_memory_operand_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_operand_t *operand) {
  const ctool_c_type_node_t *node;
  const ctool_c_type_layout_t *layout;
  ctool_u32 qualifiers;
  if (operand == (const ctool_c_assembly_operand_t *)0 ||
      operand->type >= context->unit->layout.type_count ||
      operand->expression >= context->unit->expression_count ||
      operand->matching_output != CTOOL_C_AST_NONE ||
      context->unit->expressions ==
          (const ctool_c_expression_t *)0 ||
      context->unit->expressions[operand->expression].type !=
          operand->type ||
      cemit_string_equals_literal(
          operand->constraint, "m") == CTOOL_FALSE ||
      cemit_underlying_type(
          context, operand->type, &qualifiers, &node) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  (void)node;
  layout = &context->unit->layout.types[operand->type];
  return layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size == 6u &&
                 (qualifiers & CTOOL_C_QUAL_ATOMIC) == 0u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_descriptor_table_selector_operand_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_operand_t *operand) {
  const ctool_c_type_layout_t *layout;
  if (operand == (const ctool_c_assembly_operand_t *)0 ||
      operand->type >= context->unit->layout.type_count ||
      operand->expression >= context->unit->expression_count ||
      operand->matching_output != CTOOL_C_AST_NONE ||
      context->unit->expressions ==
          (const ctool_c_expression_t *)0 ||
      context->unit->expressions[operand->expression].type !=
          operand->type ||
      cemit_string_equals_literal(
          operand->constraint, "r") == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[operand->type];
  return layout->is_integer == CTOOL_TRUE && layout->size == 2u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_descriptor_table_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    cemit_descriptor_table_assembly_kind_t kind) {
  const ctool_c_assembly_operand_t *operand;
  ctool_u32 expected_flags = CTOOL_C_ASSEMBLY_VOLATILE;
  ctool_u32 expected_inputs = 0u;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      kind == CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_NONE) {
    return CTOOL_FALSE;
  }
  if (kind == CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_DATA ||
      kind == CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_ALL) {
    expected_flags |= CTOOL_C_ASSEMBLY_MEMORY_CLOBBER |
                      CTOOL_C_ASSEMBLY_AX_CLOBBER;
    expected_inputs = 1u;
  } else if (kind ==
             CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_RELOAD_CODE) {
    expected_flags |= CTOOL_C_ASSEMBLY_MEMORY_CLOBBER;
  } else if (kind == CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_GS) {
    expected_inputs = 1u;
  }
  if (assembly->flags != expected_flags ||
      assembly->output_count != 0u ||
      assembly->input_count != expected_inputs ||
      assembly->first_operand > context->unit->assembly_operand_count ||
      expected_inputs >
          context->unit->assembly_operand_count - assembly->first_operand ||
      (expected_inputs != 0u &&
       context->unit->assembly_operands ==
           (const ctool_c_assembly_operand_t *)0)) {
    return CTOOL_FALSE;
  }
  if (expected_inputs == 0u) {
    return CTOOL_TRUE;
  }
  operand =
      &context->unit->assembly_operands[assembly->first_operand];
  return kind == CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_GS
             ? cemit_descriptor_table_selector_operand_is_valid(
                   context, operand)
             : cemit_descriptor_table_memory_operand_is_valid(
                   context, operand);
}

static ctool_status_t cemit_emit_far_code_reload(
    cemit_context_t *context) {
  ctool_u32 call_patch = 0u;
  ctool_u32 call_after = 0u;
  ctool_u32 jump_patch = 0u;
  ctool_u32 jump_after = 0u;
  ctool_u32 continuation;
  ctool_u32 trampoline;
  ctool_u32 done;
  ctool_status_t status = cemit_x86_push_integer(context, 0x08u);
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_CALL, &call_patch, &call_after);
  }
  continuation = ctool_buffer_mark(context->active_text);
  if (status == CTOOL_OK && continuation != call_after) {
    status = CTOOL_ERR_INTERNAL;
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JMP, &jump_patch, &jump_after);
  }
  trampoline = ctool_buffer_mark(context->active_text);
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_RETF);
  }
  done = ctool_buffer_mark(context->active_text);
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, call_patch, call_after, trampoline);
  }
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, jump_patch, jump_after, done);
  }
  return status;
}

static ctool_status_t cemit_emit_descriptor_table_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  cemit_descriptor_table_assembly_kind_t kind =
      cemit_descriptor_table_assembly_kind(assembly->template_text);
  ctool_status_t status = CTOOL_OK;
  if (temporary_offset != 0u ||
      cemit_descriptor_table_metadata_is_valid(
          context, assembly, kind) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU descriptor-table assembly is outside this i386 emission "
        "slice");
  }
  if (kind == CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_DATA ||
      kind == CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_ALL) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_lgdt_memory(context, 0u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_move_ax_immediate(context, 0x10u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_move_segment_ax(context, 3u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_move_segment_ax(context, 0u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_move_segment_ax(context, 2u);
    }
  } else if (kind == CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_GS) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_move_segment_ax(context, 5u);
    }
  }
  if (status == CTOOL_OK &&
      (kind == CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_RELOAD_CODE ||
       kind == CEMIT_DESCRIPTOR_TABLE_ASSEMBLY_LOAD_ALL)) {
    status = cemit_emit_far_code_reload(context);
  }
  return status;
}

static ctool_bool cemit_port_io_operand_matches(
    const cemit_context_t *context,
    const ctool_c_assembly_operand_t *operand,
    const char *constraint, ctool_u32 size,
    ctool_bool pointer, ctool_bool output) {
  const ctool_c_type_layout_t *layout;
  const ctool_c_type_node_t *node;
  ctool_u32 qualifiers;
  if (operand == (const ctool_c_assembly_operand_t *)0 ||
      operand->expression >= context->unit->expression_count ||
      operand->matching_output != CTOOL_C_AST_NONE ||
      operand->type >= context->unit->layout.type_count ||
      cemit_string_equals_literal(
          operand->constraint, constraint) == CTOOL_FALSE ||
      cemit_underlying_type(
          context, operand->type, &qualifiers, &node) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  (void)node;
  layout = &context->unit->layout.types[operand->type];
  if (layout->size != size || layout->is_object == CTOOL_FALSE ||
      layout->is_complete_object == CTOOL_FALSE ||
      (output == CTOOL_TRUE &&
       (qualifiers &
        (CTOOL_C_QUAL_CONST | CTOOL_C_QUAL_ATOMIC)) != 0u)) {
    return CTOOL_FALSE;
  }
  return pointer == CTOOL_TRUE
             ? cemit_ir_type_is_i32_pointer(context, operand->type)
             : layout->is_integer;
}

static ctool_bool cemit_port_io_port_operand_matches(
    const cemit_context_t *context,
    const ctool_c_assembly_operand_t *operand) {
  return cemit_port_io_operand_matches(
             context, operand, "d", 2u,
             CTOOL_FALSE, CTOOL_FALSE) == CTOOL_TRUE ||
                 cemit_port_io_operand_matches(
                     context, operand, "Nd", 2u,
                     CTOOL_FALSE, CTOOL_FALSE) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_port_io_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    cemit_port_io_kind_t kind) {
  const ctool_c_assembly_operand_t *operands;
  ctool_u32 width;
  ctool_bool input_form;
  if (kind == CEMIT_PORT_IO_NONE ||
      (assembly->flags & CTOOL_C_ASSEMBLY_BASIC) != 0u ||
      assembly->first_operand >
          context->unit->assembly_operand_count ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0) {
    return CTOOL_FALSE;
  }
  operands =
      &context->unit->assembly_operands[assembly->first_operand];
  if (kind == CEMIT_PORT_IO_INSW ||
      kind == CEMIT_PORT_IO_OUTSW) {
    ctool_u32 expected_flags =
        kind == CEMIT_PORT_IO_INSW
            ? CTOOL_C_ASSEMBLY_VOLATILE |
                  CTOOL_C_ASSEMBLY_MEMORY_CLOBBER
            : CTOOL_C_ASSEMBLY_VOLATILE;
    const char *pointer_constraint =
        kind == CEMIT_PORT_IO_INSW ? "+D" : "+S";
    return assembly->flags == expected_flags &&
                   assembly->output_count == 2u &&
                   assembly->input_count == 1u &&
                   assembly->first_operand <=
                       context->unit->assembly_operand_count - 3u &&
                   cemit_port_io_operand_matches(
                       context, &operands[0], pointer_constraint, 4u,
                       CTOOL_TRUE, CTOOL_TRUE) == CTOOL_TRUE &&
                   cemit_port_io_operand_matches(
                       context, &operands[1], "+c", 4u,
                       CTOOL_FALSE, CTOOL_TRUE) == CTOOL_TRUE &&
                   cemit_port_io_port_operand_matches(
                       context, &operands[2]) == CTOOL_TRUE
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  input_form = kind == CEMIT_PORT_IO_INB ||
                       kind == CEMIT_PORT_IO_INW ||
                       kind == CEMIT_PORT_IO_INL
                   ? CTOOL_TRUE
                   : CTOOL_FALSE;
  width = kind == CEMIT_PORT_IO_INB ||
                  kind == CEMIT_PORT_IO_OUTB
              ? 1u
              : kind == CEMIT_PORT_IO_INW ||
                        kind == CEMIT_PORT_IO_OUTW
                    ? 2u
                    : 4u;
  if (assembly->flags != CTOOL_C_ASSEMBLY_VOLATILE ||
      assembly->output_count != (input_form == CTOOL_TRUE ? 1u : 0u) ||
      assembly->input_count != (input_form == CTOOL_TRUE ? 1u : 2u) ||
      assembly->first_operand >
          context->unit->assembly_operand_count - 2u) {
    return CTOOL_FALSE;
  }
  if (input_form == CTOOL_TRUE) {
    return cemit_port_io_operand_matches(
               context, &operands[0], "=a", width,
               CTOOL_FALSE, CTOOL_TRUE) == CTOOL_TRUE &&
                   cemit_port_io_port_operand_matches(
                       context, &operands[1]) == CTOOL_TRUE
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return cemit_port_io_operand_matches(
             context, &operands[0], "a", width,
             CTOOL_FALSE, CTOOL_FALSE) == CTOOL_TRUE &&
                 cemit_port_io_port_operand_matches(
                     context, &operands[1]) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_port_io_failure(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  return cemit_emit_failure(
      context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
      &assembly->location,
      "GNU inline assembly template is outside this i386 emission slice");
}

static ctool_status_t cemit_emit_port_io_scalar(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    cemit_port_io_kind_t kind) {
  ctool_bool input_form =
      kind == CEMIT_PORT_IO_INB ||
              kind == CEMIT_PORT_IO_INW ||
              kind == CEMIT_PORT_IO_INL
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  ctool_u16 width =
      kind == CEMIT_PORT_IO_INB || kind == CEMIT_PORT_IO_OUTB
          ? 8u
          : kind == CEMIT_PORT_IO_INW ||
                    kind == CEMIT_PORT_IO_OUTW
                ? 16u
                : 32u;
  ctool_x86_reg_class_t accumulator_class =
      width == 8u
          ? CTOOL_X86_REG_GPR8
          : width == 16u ? CTOOL_X86_REG_GPR16
                         : CTOOL_X86_REG_GPR32;
  ctool_status_t status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 2u, 32u);
  if (status == CTOOL_OK && input_form == CTOOL_FALSE) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context,
        input_form == CTOOL_TRUE ? CTOOL_X86_MN_IN
                                 : CTOOL_X86_MN_OUT,
        input_form == CTOOL_TRUE ? accumulator_class
                                 : CTOOL_X86_REG_GPR16,
        input_form == CTOOL_TRUE ? 0u : 2u,
        input_form == CTOOL_TRUE ? CTOOL_X86_REG_GPR16
                                 : accumulator_class,
        input_form == CTOOL_TRUE ? 2u : 0u, width);
  }
  if (status == CTOOL_OK && input_form == CTOOL_TRUE) {
    const ctool_c_assembly_operand_t *output =
        &context->unit->assembly_operands[assembly->first_operand];
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 1u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_ecx_at_eax(context, output->type);
    }
  }
  return status;
}

static ctool_status_t cemit_emit_port_io_string(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    cemit_port_io_kind_t kind, ctool_u32 temporary_offset) {
  const ctool_c_assembly_operand_t *outputs =
      &context->unit->assembly_operands[assembly->first_operand];
  ctool_u8 string_register =
      kind == CEMIT_PORT_IO_INSW ? 7u : 6u;
  ctool_x86_mnemonic_t mnemonic =
      kind == CEMIT_PORT_IO_INSW ? CTOOL_X86_MN_INSW
                                 : CTOOL_X86_MN_OUTSW;
  ctool_status_t status;
  if (temporary_offset < 12u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_store_local_register(
      context, temporary_offset, 8u, string_register);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 2u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, 4u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_eax(context, outputs[1].type);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 1u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, 0u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_eax(context, outputs[0].type);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32,
        string_register, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_CLD);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_repeat_string(context, mnemonic, 16u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_local_register(
        context, 0u, temporary_offset, 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_ecx_at_eax(context, outputs[1].type);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 1u,
        CTOOL_X86_REG_GPR32, string_register, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_local_register(
        context, 0u, temporary_offset, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_ecx_at_eax(context, outputs[0].type);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_local_register(
        context, string_register, temporary_offset, 8u);
  }
  return status;
}

static ctool_status_t cemit_emit_port_io_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  cemit_port_io_kind_t kind =
      cemit_port_io_template_kind(assembly->template_text);
  if (cemit_port_io_metadata_is_valid(
          context, assembly, kind) == CTOOL_FALSE) {
    return cemit_emit_port_io_failure(context, assembly);
  }
  if (kind == CEMIT_PORT_IO_INSW ||
      kind == CEMIT_PORT_IO_OUTSW) {
    return cemit_emit_port_io_string(
        context, assembly, kind, temporary_offset);
  }
  return cemit_emit_port_io_scalar(context, assembly, kind);
}

static ctool_bool cemit_assembly_output_type_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_operand_t *operand) {
  const ctool_c_type_node_t *node;
  const ctool_c_type_layout_t *layout;
  ctool_u32 qualifiers;
  ctool_u32 required_size;
  ctool_bool is_pointer;
  if (operand->type >= context->unit->layout.type_count ||
      cemit_underlying_type(
          context, operand->type, &qualifiers, &node) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  (void)node;
  layout = &context->unit->layout.types[operand->type];
  is_pointer =
      cemit_ir_type_is_i32_pointer(context, operand->type);
  required_size =
      cemit_string_equals_literal(
          operand->constraint, "=qm") == CTOOL_TRUE
          ? 1u
          : 4u;
  return (qualifiers &
          (CTOOL_C_QUAL_CONST | CTOOL_C_QUAL_ATOMIC)) == 0u &&
                 layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 ((is_pointer == CTOOL_TRUE &&
                   cemit_string_equals_literal(
                       operand->constraint, "=r") == CTOOL_TRUE &&
                   layout->size == 4u) ||
                  (is_pointer == CTOOL_FALSE &&
                   layout->is_integer == CTOOL_TRUE &&
                   layout->size == required_size))
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_plan_assembly_registers(
    cemit_context_t *context, const ctool_c_assembly_t *assembly,
    ctool_u8 registers[4]) {
  ctool_u32 used = 0u;
  ctool_u32 output;
  if (assembly->output_count == 0u || assembly->output_count > 4u ||
      assembly->first_operand > context->unit->assembly_operand_count ||
      cemit_add_overflows(
          assembly->output_count, assembly->input_count) == CTOOL_TRUE ||
      assembly->output_count + assembly->input_count >
          context->unit->assembly_operand_count - assembly->first_operand) {
    return CTOOL_ERR_INTERNAL;
  }
  for (output = 0u; output < assembly->output_count; output++) {
    const ctool_c_assembly_operand_t *operand =
        &context->unit->assembly_operands[assembly->first_operand + output];
    ctool_u8 reg = 0u;
    if (cemit_assembly_output_fixed_register(
            operand->constraint, &reg) == CTOOL_TRUE) {
      if ((used & (1u << reg)) != 0u) {
        return CTOOL_ERR_INTERNAL;
      }
      registers[output] = reg;
      used |= 1u << reg;
    } else if (cemit_string_equals_literal(
                   operand->constraint, "=r") == CTOOL_FALSE &&
               cemit_string_equals_literal(
                   operand->constraint, "=qm") == CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  for (output = 0u; output < assembly->output_count; output++) {
    const ctool_c_assembly_operand_t *operand =
        &context->unit->assembly_operands[assembly->first_operand + output];
    ctool_u8 reg;
    if (cemit_assembly_output_fixed_register(
            operand->constraint, &reg) == CTOOL_TRUE) {
      continue;
    }
    for (reg = 0u; reg < 4u && (used & (1u << reg)) != 0u; reg++) {
    }
    if (reg == 4u) {
      return CTOOL_ERR_INTERNAL;
    }
    registers[output] = reg;
    used |= 1u << reg;
  }
  return CTOOL_OK;
}

static void cemit_assembly_skip_space(
    ctool_string_t text, ctool_u32 *cursor) {
  while (*cursor < text.size &&
         (text.data[*cursor] == ' ' || text.data[*cursor] == '\t' ||
          text.data[*cursor] == '\r' || text.data[*cursor] == '\n')) {
    (*cursor)++;
  }
}

static ctool_bool cemit_assembly_take_literal(
    ctool_string_t text, ctool_u32 *cursor, const char *literal) {
  ctool_u32 start = *cursor;
  ctool_u32 index = 0u;
  if (literal == (const char *)0) {
    return CTOOL_FALSE;
  }
  while (literal[index] != '\0') {
    if (*cursor >= text.size ||
        text.data[*cursor] != literal[index]) {
      *cursor = start;
      return CTOOL_FALSE;
    }
    (*cursor)++;
    index++;
  }
  return CTOOL_TRUE;
}

static ctool_bool cemit_assembly_take_word(
    ctool_string_t text, ctool_u32 *cursor, const char *word) {
  ctool_u32 start = *cursor;
  ctool_u32 index = 0u;
  while (word[index] != '\0' && *cursor < text.size &&
         text.data[*cursor] == word[index]) {
    (*cursor)++;
    index++;
  }
  if (word[index] != '\0' ||
      (*cursor < text.size &&
       ((text.data[*cursor] >= 'a' && text.data[*cursor] <= 'z') ||
        (text.data[*cursor] >= 'A' && text.data[*cursor] <= 'Z') ||
        (text.data[*cursor] >= '0' && text.data[*cursor] <= '9') ||
        text.data[*cursor] == '_'))) {
    *cursor = start;
    return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_bool cemit_assembly_take_no_operand_instruction(
    ctool_string_t text, ctool_u32 *cursor,
    ctool_x86_mnemonic_t *mnemonic_out) {
  if (cemit_assembly_take_word(text, cursor, "pause") == CTOOL_TRUE) {
    *mnemonic_out = CTOOL_X86_MN_PAUSE;
  } else if (cemit_assembly_take_word(
                 text, cursor, "nop") == CTOOL_TRUE) {
    *mnemonic_out = CTOOL_X86_MN_NOP;
  } else if (cemit_assembly_take_word(
                 text, cursor, "sti") == CTOOL_TRUE) {
    *mnemonic_out = CTOOL_X86_MN_STI;
  } else if (cemit_assembly_take_word(
                 text, cursor, "hlt") == CTOOL_TRUE) {
    *mnemonic_out = CTOOL_X86_MN_HLT;
  } else if (cemit_assembly_take_word(
                 text, cursor, "cli") == CTOOL_TRUE) {
    *mnemonic_out = CTOOL_X86_MN_CLI;
  } else if (cemit_assembly_take_word(
                 text, cursor, "cld") == CTOOL_TRUE) {
    *mnemonic_out = CTOOL_X86_MN_CLD;
  } else if (cemit_assembly_take_word(
                 text, cursor, "sfence") == CTOOL_TRUE) {
    *mnemonic_out = CTOOL_X86_MN_SFENCE;
  } else if (cemit_assembly_take_word(
                 text, cursor, "fninit") == CTOOL_TRUE) {
    *mnemonic_out = CTOOL_X86_MN_FNINIT;
  } else {
    return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_bool cemit_assembly_take_operand(
    ctool_string_t text, ctool_u32 *cursor, ctool_u32 *operand_out) {
  ctool_u32 operand = 0u;
  ctool_bool saw_digit = CTOOL_FALSE;
  cemit_assembly_skip_space(text, cursor);
  if (*cursor >= text.size || text.data[*cursor] != '%') {
    return CTOOL_FALSE;
  }
  (*cursor)++;
  while (*cursor < text.size && text.data[*cursor] >= '0' &&
         text.data[*cursor] <= '9') {
    ctool_u32 digit = (ctool_u32)(text.data[*cursor] - '0');
    if (operand > (0xffffffffu - digit) / 10u) {
      return CTOOL_FALSE;
    }
    operand = operand * 10u + digit;
    (*cursor)++;
    saw_digit = CTOOL_TRUE;
  }
  if (saw_digit == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  *operand_out = operand;
  return CTOOL_TRUE;
}

static ctool_bool cemit_assembly_take_gpr32(
    ctool_string_t text, ctool_u32 *cursor,
    ctool_u8 *register_out) {
  if (cemit_assembly_take_literal(
          text, cursor, "%%eax") == CTOOL_TRUE) {
    *register_out = 0u;
  } else if (cemit_assembly_take_literal(
                 text, cursor, "%%ecx") == CTOOL_TRUE) {
    *register_out = 1u;
  } else if (cemit_assembly_take_literal(
                 text, cursor, "%%edx") == CTOOL_TRUE) {
    *register_out = 2u;
  } else if (cemit_assembly_take_literal(
                 text, cursor, "%%ebx") == CTOOL_TRUE) {
    *register_out = 3u;
  } else if (cemit_assembly_take_literal(
                 text, cursor, "%%esp") == CTOOL_TRUE) {
    *register_out = 4u;
  } else if (cemit_assembly_take_literal(
                 text, cursor, "%%ebp") == CTOOL_TRUE) {
    *register_out = 5u;
  } else if (cemit_assembly_take_literal(
                 text, cursor, "%%esi") == CTOOL_TRUE) {
    *register_out = 6u;
  } else if (cemit_assembly_take_literal(
                 text, cursor, "%%edi") == CTOOL_TRUE) {
    *register_out = 7u;
  } else {
    return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_bool cemit_assembly_register_snapshot_template(
    ctool_string_t text, ctool_u8 *source_register_out) {
  ctool_u32 cursor = 0u;
  ctool_u32 operand = CTOOL_C_AST_NONE;
  cemit_assembly_skip_space(text, &cursor);
  if (cemit_assembly_take_word(text, &cursor, "mov") == CTOOL_FALSE &&
      cemit_assembly_take_word(text, &cursor, "movl") == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  cemit_assembly_skip_space(text, &cursor);
  if (cemit_assembly_take_gpr32(
          text, &cursor, source_register_out) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  cemit_assembly_skip_space(text, &cursor);
  if (cursor >= text.size || text.data[cursor] != ',') {
    return CTOOL_FALSE;
  }
  cursor++;
  if (cemit_assembly_take_operand(
          text, &cursor, &operand) == CTOOL_FALSE ||
      operand != 0u) {
    return CTOOL_FALSE;
  }
  cemit_assembly_skip_space(text, &cursor);
  return cursor == text.size ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool cemit_assembly_return_slot_snapshot_template(
    ctool_string_t text) {
  ctool_u32 cursor = 0u;
  ctool_u32 operand = CTOOL_C_AST_NONE;
  cemit_assembly_skip_space(text, &cursor);
  if (cemit_assembly_take_word(
          text, &cursor, "movl") == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  cemit_assembly_skip_space(text, &cursor);
  if (cemit_assembly_take_literal(
          text, &cursor, "4(%%ebp)") == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  cemit_assembly_skip_space(text, &cursor);
  if (cursor >= text.size || text.data[cursor] != ',') {
    return CTOOL_FALSE;
  }
  cursor++;
  if (cemit_assembly_take_operand(
          text, &cursor, &operand) == CTOOL_FALSE ||
      operand != 0u) {
    return CTOOL_FALSE;
  }
  cemit_assembly_skip_space(text, &cursor);
  return cursor == text.size ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool cemit_assembly_flags_snapshot_template(
    ctool_string_t text, ctool_bool *disable_interrupts_out) {
  ctool_u32 cursor = 0u;
  ctool_u32 separator_start;
  ctool_u32 operand = CTOOL_C_AST_NONE;
  ctool_bool saw_line_break = CTOOL_FALSE;
  cemit_assembly_skip_space(text, &cursor);
  if (cemit_assembly_take_word(
          text, &cursor, "pushf") == CTOOL_FALSE &&
      cemit_assembly_take_word(
          text, &cursor, "pushfl") == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  separator_start = cursor;
  while (cursor < text.size &&
         (text.data[cursor] == ' ' || text.data[cursor] == '\t' ||
          text.data[cursor] == '\r' || text.data[cursor] == '\n')) {
    if (text.data[cursor] == '\r' || text.data[cursor] == '\n') {
      saw_line_break = CTOOL_TRUE;
    }
    cursor++;
  }
  if (cursor < text.size && text.data[cursor] == ';') {
    cursor++;
    cemit_assembly_skip_space(text, &cursor);
  } else if (saw_line_break == CTOOL_FALSE) {
    cursor = separator_start;
    return CTOOL_FALSE;
  }
  if (cemit_assembly_take_word(
          text, &cursor, "pop") == CTOOL_FALSE &&
      cemit_assembly_take_word(
          text, &cursor, "popl") == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  if (cemit_assembly_take_operand(
          text, &cursor, &operand) == CTOOL_FALSE ||
      operand != 0u) {
    return CTOOL_FALSE;
  }
  cemit_assembly_skip_space(text, &cursor);
  if (cursor == text.size) {
    *disable_interrupts_out = CTOOL_FALSE;
    return CTOOL_TRUE;
  }
  if (text.data[cursor] != ';') {
    return CTOOL_FALSE;
  }
  cursor++;
  cemit_assembly_skip_space(text, &cursor);
  if (cemit_assembly_take_word(
          text, &cursor, "cli") == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  cemit_assembly_skip_space(text, &cursor);
  if (cursor != text.size) {
    return CTOOL_FALSE;
  }
  *disable_interrupts_out = CTOOL_TRUE;
  return CTOOL_TRUE;
}

static ctool_bool cemit_assembly_uses_flags_restore_path(
    const ctool_c_assembly_t *assembly) {
  return assembly != (const ctool_c_assembly_t *)0 &&
                 cemit_string_equals_literal(
                     assembly->template_text,
                     "pushl %0\n\tpopfl\n\t") == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_flags_restore_metadata_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  const ctool_c_assembly_operand_t *operand;
  const ctool_c_type_layout_t *layout;
  if (context == (const cemit_context_t *)0 ||
      assembly == (const ctool_c_assembly_t *)0 ||
      cemit_assembly_uses_flags_restore_path(
          assembly) == CTOOL_FALSE ||
      assembly->flags !=
          (CTOOL_C_ASSEMBLY_VOLATILE |
           CTOOL_C_ASSEMBLY_CC_CLOBBER) ||
      assembly->output_count != 0u ||
      assembly->input_count != 1u ||
      assembly->first_operand >=
          context->unit->assembly_operand_count ||
      context->unit->assembly_operands ==
          (const ctool_c_assembly_operand_t *)0 ||
      context->unit->expressions ==
          (const ctool_c_expression_t *)0) {
    return CTOOL_FALSE;
  }
  operand =
      &context->unit->assembly_operands[assembly->first_operand];
  if (operand->expression >= context->unit->expression_count ||
      operand->type >= context->unit->layout.type_count ||
      operand->matching_output != CTOOL_C_AST_NONE ||
      cemit_string_equals_literal(
          operand->constraint, "r") == CTOOL_FALSE ||
      context->unit->expressions[operand->expression].type !=
          operand->type ||
      cemit_type_has_atomic_qualification(
          context, operand->type) == CTOOL_TRUE) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[operand->type];
  return layout->is_integer == CTOOL_TRUE &&
                 layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size == 4u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_flags_restore_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly,
    ctool_u32 temporary_offset) {
  ctool_status_t status;
  if (temporary_offset != 0u ||
      cemit_flags_restore_metadata_is_valid(
          context, assembly) == CTOOL_FALSE) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_POPF);
  }
  return status;
}

static ctool_bool cemit_assembly_snapshot_output_is_valid(
    const cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  const ctool_c_assembly_operand_t *operand =
      assembly->output_count == 1u &&
              context->unit->assembly_operands !=
                  (const ctool_c_assembly_operand_t *)0
          ? &context->unit->assembly_operands[assembly->first_operand]
          : (const ctool_c_assembly_operand_t *)0;
  return assembly->flags == CTOOL_C_ASSEMBLY_VOLATILE &&
                 assembly->output_count == 1u &&
                 assembly->input_count == 0u &&
                 operand != (const ctool_c_assembly_operand_t *)0 &&
                 cemit_string_equals_literal(
                     operand->constraint, "=r") == CTOOL_TRUE &&
                 cemit_ir_type_is_i32_integer(
                     context, operand->type) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_kernel_bss_clear_assembly(
    cemit_context_t *context,
    const ctool_c_assembly_t *assembly) {
  ctool_u32 bss_start_binding;
  ctool_u32 kernel_end_binding;
  ctool_u32 bss_start_symbol;
  ctool_u32 kernel_end_symbol;
  ctool_status_t status;
  if (cemit_kernel_bss_clear_assembly_metadata_is_valid(
          context, assembly) == CTOOL_FALSE ||
      cemit_find_external_object_binding(
          context, ctool_string("_bss_start"),
          &bss_start_binding) == CTOOL_FALSE ||
      cemit_find_external_object_binding(
          context, ctool_string("_kernel_end"),
          &kernel_end_binding) == CTOOL_FALSE) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_ensure_binding_symbol(
      context, bss_start_binding, &bss_start_symbol);
  if (status == CTOOL_OK) {
    status = cemit_ensure_binding_symbol(
        context, kernel_end_binding, &kernel_end_symbol);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_register_constant(
        context, 4u, 0x00f00000u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 5u,
        CTOOL_X86_REG_GPR32, 4u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_register_symbol(
        context, 7u, bss_start_symbol);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_register_symbol(
        context, 1u, kernel_end_symbol);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_SUB, CTOOL_X86_REG_GPR32, 1u,
        CTOOL_X86_REG_GPR32, 7u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_shift_register(
        context, CTOOL_X86_MN_SHR, 1u, 2u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_CLD);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_repeat_string(
        context, CTOOL_X86_MN_STOSD, 32u);
  }
  return status;
}

static ctool_status_t cemit_emit_assembly_template(
    cemit_context_t *context, const ctool_c_assembly_t *assembly,
    const ctool_u8 registers[4]) {
  ctool_u32 cursor = 0u;
  ctool_u32 output;
  ctool_u8 snapshot_source_register = 0u;
  ctool_bool snapshot_disables_interrupts = CTOOL_FALSE;
  ctool_bool emitted_instruction = CTOOL_FALSE;
  ctool_bool has_pointer_output = CTOOL_FALSE;
  ctool_status_t status = CTOOL_OK;
  if (cemit_naked_ipi_wrapper_template(
          assembly->template_text, (ctool_string_t *)0) == CTOOL_TRUE) {
    ctool_u32 symbol = CTOOL_C_AST_NONE;
    if (cemit_naked_control_assembly_metadata_is_valid(
            context, assembly) == CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_ensure_binding_symbol(
        context, assembly->direct_call_binding_plus_one - 1u, &symbol);
    if (status == CTOOL_OK) {
      status = cemit_x86_no_operand(context, CTOOL_X86_MN_PUSHA);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_call_symbol(context, symbol);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_no_operand(context, CTOOL_X86_MN_POPA);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_no_operand(context, CTOOL_X86_MN_IRET);
    }
    return status;
  }
  if (cemit_naked_panic_template(
          assembly->template_text) == CTOOL_TRUE) {
    ctool_u32 loop_target;
    ctool_u32 jump_patch = 0u;
    ctool_u32 jump_after = 0u;
    if (cemit_naked_control_assembly_metadata_is_valid(
            context, assembly) == CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_CLI);
    loop_target = ctool_buffer_view(context->active_text).size;
    if (status == CTOOL_OK) {
      status = cemit_x86_no_operand(context, CTOOL_X86_MN_HLT);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_branch(
          context, CTOOL_X86_MN_JMP, &jump_patch, &jump_after);
    }
    if (status == CTOOL_OK) {
      status = cemit_patch_branch(
          context->active_text, jump_patch, jump_after, loop_target);
    }
    return status;
  }
  if (cemit_string_equals_literal(
          assembly->template_text,
          "call 1f\n1: popl %0") == CTOOL_TRUE) {
    ctool_u32 patch_offset = 0u;
    ctool_u32 after_call = 0u;
    if (cemit_assembly_snapshot_output_is_valid(
            context, assembly) == CTOOL_FALSE) {
      return cemit_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_EMIT_DIAG_UNSUPPORTED, &assembly->location,
          "GNU inline assembly template is outside this i386 emission "
          "slice");
    }
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_CALL, &patch_offset, &after_call);
    if (status == CTOOL_OK &&
        (patch_offset > 0xfffffffbu ||
         after_call != patch_offset + 4u)) {
      status = CTOOL_ERR_INTERNAL;
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32,
          registers[0], 32u);
    }
    return status;
  }
  if (cemit_assembly_register_snapshot_template(
          assembly->template_text,
          &snapshot_source_register) == CTOOL_TRUE) {
    if (cemit_assembly_snapshot_output_is_valid(
            context, assembly) == CTOOL_FALSE) {
      return cemit_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_EMIT_DIAG_UNSUPPORTED, &assembly->location,
          "GNU inline assembly template is outside this i386 emission "
          "slice");
    }
    return cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32,
        registers[0], CTOOL_X86_REG_GPR32,
        snapshot_source_register, 32u);
  }
  if (cemit_assembly_return_slot_snapshot_template(
          assembly->template_text) == CTOOL_TRUE) {
    if (cemit_assembly_snapshot_output_is_valid(
            context, assembly) == CTOOL_FALSE) {
      return cemit_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_EMIT_DIAG_UNSUPPORTED, &assembly->location,
          "GNU inline assembly template is outside this i386 emission "
          "slice");
    }
    return cemit_x86_load_register_at_register(
        context, registers[0], 5u, 4u);
  }
  if (cemit_assembly_flags_snapshot_template(
          assembly->template_text,
          &snapshot_disables_interrupts) == CTOOL_TRUE) {
    if (cemit_assembly_snapshot_output_is_valid(
            context, assembly) == CTOOL_FALSE) {
      return cemit_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_EMIT_DIAG_UNSUPPORTED, &assembly->location,
          "GNU inline assembly template is outside this i386 emission "
          "slice");
    }
    status = cemit_x86_no_operand(
        context, CTOOL_X86_MN_PUSHF);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32,
          registers[0], 32u);
    }
    if (status == CTOOL_OK &&
        snapshot_disables_interrupts == CTOOL_TRUE) {
      status = cemit_x86_no_operand(
          context, CTOOL_X86_MN_CLI);
    }
    return status;
  }
  for (output = 0u; output < assembly->output_count; output++) {
    const ctool_c_assembly_operand_t *operand =
        &context->unit->assembly_operands[
            assembly->first_operand + output];
    if (cemit_ir_type_is_i32_pointer(
            context, operand->type) == CTOOL_TRUE) {
      has_pointer_output = CTOOL_TRUE;
    }
  }
  if (has_pointer_output == CTOOL_TRUE &&
      (assembly->output_count != 1u ||
       assembly->input_count != 0u ||
       cemit_string_equals_literal(
           assembly->template_text,
           "mov %%gs:0, %0") == CTOOL_FALSE)) {
    status = CTOOL_ERR_UNSUPPORTED;
  }
  while (status == CTOOL_OK) {
    ctool_u32 operand;
    ctool_x86_mnemonic_t no_operand_mnemonic;
    cemit_assembly_skip_space(assembly->template_text, &cursor);
    if (cursor == assembly->template_text.size) {
      break;
    }
    if (cemit_assembly_take_word(
            assembly->template_text, &cursor,
            "mov") == CTOOL_TRUE) {
      cemit_assembly_skip_space(assembly->template_text, &cursor);
      if (cemit_assembly_take_literal(
              assembly->template_text, &cursor,
              "%%gs:0") == CTOOL_FALSE) {
        status = CTOOL_ERR_UNSUPPORTED;
      }
      cemit_assembly_skip_space(assembly->template_text, &cursor);
      if (status == CTOOL_OK &&
          (cursor >= assembly->template_text.size ||
           assembly->template_text.data[cursor] != ',')) {
        status = CTOOL_ERR_UNSUPPORTED;
      } else if (status == CTOOL_OK) {
        cursor++;
      }
      if (status == CTOOL_OK &&
          (cemit_assembly_take_operand(
               assembly->template_text, &cursor, &operand) ==
               CTOOL_FALSE ||
           assembly->output_count != 1u ||
           assembly->input_count != 0u || operand != 0u ||
           cemit_ir_type_is_i32_pointer(
               context,
               context->unit->assembly_operands[
                   assembly->first_operand].type) == CTOOL_FALSE ||
           cemit_string_equals_literal(
               context->unit->assembly_operands[
                   assembly->first_operand].constraint,
               "=r") == CTOOL_FALSE)) {
        status = CTOOL_ERR_UNSUPPORTED;
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_load_segment_absolute_register(
            context, registers[0], 5u, 0u);
      }
    } else if (cemit_assembly_take_word(
            assembly->template_text, &cursor, "rdtsc") == CTOOL_TRUE) {
      if (assembly->output_count != 2u || assembly->input_count != 0u ||
          !((registers[0] == 0u && registers[1] == 2u) ||
            (registers[0] == 2u && registers[1] == 0u))) {
        status = CTOOL_ERR_UNSUPPORTED;
      } else {
        status = cemit_x86_no_operand(context, CTOOL_X86_MN_RDTSC);
      }
    } else if (cemit_assembly_take_word(
                   assembly->template_text, &cursor,
                   "cpuid") == CTOOL_TRUE) {
      ctool_u32 register_mask =
          (1u << registers[0]) | (1u << registers[1]) |
          (1u << registers[2]) | (1u << registers[3]);
      const ctool_c_assembly_operand_t *input =
          assembly->input_count == 1u
              ? &context->unit->assembly_operands[
                    assembly->first_operand + assembly->output_count]
              : (const ctool_c_assembly_operand_t *)0;
      if (assembly->output_count != 4u ||
          input == (const ctool_c_assembly_operand_t *)0 ||
          register_mask != 0x0fu || input->matching_output >= 4u ||
          registers[input->matching_output] != 0u) {
        status = CTOOL_ERR_UNSUPPORTED;
      } else {
        status = cemit_x86_no_operand(context, CTOOL_X86_MN_CPUID);
      }
    } else if (cemit_assembly_take_no_operand_instruction(
                   assembly->template_text, &cursor,
                   &no_operand_mnemonic) == CTOOL_TRUE) {
      if (no_operand_mnemonic != CTOOL_X86_MN_NOP &&
          (assembly->output_count != 0u ||
           assembly->input_count != 0u)) {
        status = CTOOL_ERR_UNSUPPORTED;
      } else {
        status = cemit_x86_no_operand(context, no_operand_mnemonic);
      }
    } else if (cemit_assembly_take_word(
                   assembly->template_text, &cursor,
                   "rdrand") == CTOOL_TRUE) {
      if (cemit_assembly_take_operand(
              assembly->template_text, &cursor, &operand) == CTOOL_FALSE ||
          operand >= assembly->output_count ||
          context->unit->assembly_operands[
              assembly->first_operand + operand].type >=
              context->unit->layout.type_count ||
          context->unit->layout.types[
              context->unit->assembly_operands[
                  assembly->first_operand + operand].type].size != 4u) {
        status = CTOOL_ERR_UNSUPPORTED;
      } else {
        status = cemit_x86_one_register(
            context, CTOOL_X86_MN_RDRAND, CTOOL_X86_REG_GPR32,
            registers[operand], 32u);
      }
    } else if (cemit_assembly_take_word(
                   assembly->template_text, &cursor,
                   "setc") == CTOOL_TRUE) {
      if (cemit_assembly_take_operand(
              assembly->template_text, &cursor, &operand) == CTOOL_FALSE ||
          operand >= assembly->output_count || registers[operand] >= 4u ||
          context->unit->assembly_operands[
              assembly->first_operand + operand].type >=
              context->unit->layout.type_count ||
          context->unit->layout.types[
              context->unit->assembly_operands[
                  assembly->first_operand + operand].type].size != 1u) {
        status = CTOOL_ERR_UNSUPPORTED;
      } else {
        status = cemit_x86_one_register(
            context, CTOOL_X86_MN_SETB, CTOOL_X86_REG_GPR8,
            registers[operand], 8u);
      }
    } else {
      status = CTOOL_ERR_UNSUPPORTED;
    }
    if (status == CTOOL_OK) {
      emitted_instruction = CTOOL_TRUE;
    }
    cemit_assembly_skip_space(assembly->template_text, &cursor);
    if (status == CTOOL_OK && cursor < assembly->template_text.size) {
      if (assembly->template_text.data[cursor] != ';') {
        status = CTOOL_ERR_UNSUPPORTED;
      } else {
        cursor++;
      }
    }
  }
  if (status == CTOOL_OK && emitted_instruction == CTOOL_FALSE) {
    status = CTOOL_ERR_UNSUPPORTED;
  }
  if (status == CTOOL_ERR_UNSUPPORTED) {
    return cemit_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &assembly->location,
        "GNU inline assembly template is outside this i386 emission slice");
  }
  return status;
}

static ctool_status_t cemit_emit_assembly(
    cemit_context_t *context,
    const ctool_c_ir_instruction_t *ir_instruction,
    ctool_u32 temporary_offset) {
  const ctool_c_assembly_t *assembly;
  ctool_u8 registers[4] = {0u, 0u, 0u, 0u};
  ctool_u32 input;
  ctool_u32 operand_count;
  ctool_u32 output;
  ctool_u32 ebx_byte_offset;
  ctool_status_t status;
  if (ir_instruction->reference >= context->unit->assembly_count ||
      context->unit->assemblies == (const ctool_c_assembly_t *)0 ||
      ir_instruction->type != CTOOL_C_TYPE_NONE ||
      ir_instruction->input_type != CTOOL_C_TYPE_NONE ||
      ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
      ir_instruction->argument_count != 0u ||
      ir_instruction->first_argument_type != CTOOL_C_AST_NONE ||
      ir_instruction->integer_bits != 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  assembly = &context->unit->assemblies[ir_instruction->reference];
  if ((assembly->flags &
      ~(CTOOL_C_ASSEMBLY_BASIC | CTOOL_C_ASSEMBLY_VOLATILE |
         CTOOL_C_ASSEMBLY_MEMORY_CLOBBER |
         CTOOL_C_ASSEMBLY_XMM0_CLOBBER |
         CTOOL_C_ASSEMBLY_AX_CLOBBER |
         CTOOL_C_ASSEMBLY_CC_CLOBBER |
         CTOOL_C_ASSEMBLY_EAX_CLOBBER |
         CTOOL_C_ASSEMBLY_ECX_CLOBBER |
         CTOOL_C_ASSEMBLY_EDI_CLOBBER |
         CTOOL_C_ASSEMBLY_XMM1_CLOBBER |
         CTOOL_C_ASSEMBLY_XMM2_CLOBBER |
         CTOOL_C_ASSEMBLY_XMM3_CLOBBER |
         CTOOL_C_ASSEMBLY_XMM4_CLOBBER |
         CTOOL_C_ASSEMBLY_XMM5_CLOBBER |
         CTOOL_C_ASSEMBLY_XMM6_CLOBBER |
         CTOOL_C_ASSEMBLY_XMM7_CLOBBER)) != 0u ||
      assembly->template_text.data == (const char *)0 ||
      assembly->first_operand > context->unit->assembly_operand_count ||
      cemit_add_overflows(
          assembly->output_count, assembly->input_count) == CTOOL_TRUE ||
      assembly->output_count + assembly->input_count >
          context->unit->assembly_operand_count - assembly->first_operand) {
    return CTOOL_ERR_INTERNAL;
  }
  operand_count = assembly->output_count + assembly->input_count;
  if (assembly->template_text.size == 0u) {
    return assembly->flags ==
                   (CTOOL_C_ASSEMBLY_VOLATILE |
                    CTOOL_C_ASSEMBLY_MEMORY_CLOBBER) &&
               operand_count == 0u &&
               temporary_offset == 0u
           ? CTOOL_OK
           : CTOOL_ERR_INTERNAL;
  }
  if (cemit_kernel_bss_clear_template(
          assembly->template_text) == CTOOL_TRUE) {
    return operand_count == 0u && temporary_offset == 0u
               ? cemit_emit_kernel_bss_clear_assembly(context, assembly)
               : CTOOL_ERR_INTERNAL;
  }
  {
    cemit_privileged_assembly_kind_t privileged_kind =
        cemit_privileged_assembly_template_kind(
            assembly->template_text);
    if (privileged_kind != CEMIT_PRIVILEGED_ASSEMBLY_NONE) {
      return cemit_emit_privileged_assembly(
          context, assembly, privileged_kind);
    }
  }
  if (cemit_assembly_uses_fxsave_path(assembly) == CTOOL_TRUE) {
    return cemit_emit_fxsave_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_ldmxcsr_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_ldmxcsr_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_flags_restore_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_flags_restore_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_kernel_simd_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_kernel_simd_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_movss_memory_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_movss_memory_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_sqrtsd_register_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_sqrtsd_register_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_x87_atan2_memory_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_x87_atan2_memory_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_x87_exp_memory_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_x87_exp_memory_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_x87_sine_memory_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_x87_sine_memory_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_x87_round_down_memory_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_x87_round_down_memory_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_x87_pow_memory_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_x87_pow_memory_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_x87_powf_memory_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_x87_powf_memory_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_descriptor_table_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_descriptor_table_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_state_memory_path(
          assembly) == CTOOL_TRUE) {
    return cemit_emit_state_memory_assembly(
        context, assembly, temporary_offset);
  }
  if (cemit_assembly_uses_port_io_path(
          context, assembly) == CTOOL_TRUE) {
    return cemit_emit_port_io_assembly(
        context, assembly, temporary_offset);
  }
  if (((assembly->flags & CTOOL_C_ASSEMBLY_BASIC) != 0u &&
      (assembly->flags !=
           (CTOOL_C_ASSEMBLY_BASIC |
            CTOOL_C_ASSEMBLY_VOLATILE) ||
        operand_count != 0u)) ||
      (operand_count == 0u &&
       (((assembly->flags & CTOOL_C_ASSEMBLY_VOLATILE) == 0u) ||
        temporary_offset != 0u)) ||
      (operand_count != 0u &&
       (temporary_offset == 0u ||
        context->unit->assembly_operands ==
            (const ctool_c_assembly_operand_t *)0))) {
    return CTOOL_ERR_INTERNAL;
  }
  if (operand_count == 0u) {
    return cemit_emit_assembly_template(context, assembly, registers);
  }
  for (output = 0u; output < assembly->output_count; output++) {
    const ctool_c_assembly_operand_t *operand =
        &context->unit->assembly_operands[
            assembly->first_operand + output];
    const ctool_c_type_layout_t *layout =
        operand->type < context->unit->layout.type_count
            ? &context->unit->layout.types[operand->type]
            : (const ctool_c_type_layout_t *)0;
    if (operand->expression >= context->unit->expression_count ||
        operand->matching_output != CTOOL_C_AST_NONE ||
        layout == (const ctool_c_type_layout_t *)0 ||
        cemit_assembly_output_type_is_valid(
            context, operand) == CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  for (input = 0u; input < assembly->input_count; input++) {
    const ctool_c_assembly_operand_t *operand =
        &context->unit->assembly_operands[
            assembly->first_operand + assembly->output_count + input];
    const ctool_c_assembly_operand_t *matched;
    ctool_u8 input_register = 0u;
    ctool_u8 output_register = 0u;
    ctool_bool numeric_match;
    ctool_bool fixed_match;
    if (operand->expression >= context->unit->expression_count ||
        operand->matching_output >= assembly->output_count ||
        operand->constraint.data == (const char *)0) {
      return CTOOL_ERR_INTERNAL;
    }
    matched = &context->unit->assembly_operands[
        assembly->first_operand + operand->matching_output];
    numeric_match =
        operand->constraint.size == 1u &&
                operand->constraint.data[0] ==
                    (char)('0' + operand->matching_output)
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    fixed_match =
        cemit_assembly_input_fixed_register(
            operand->constraint, &input_register) == CTOOL_TRUE &&
                cemit_assembly_output_fixed_register(
                    matched->constraint, &output_register) == CTOOL_TRUE &&
                input_register == output_register
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if (operand->type >= context->unit->layout.type_count ||
        matched->type >= context->unit->layout.type_count ||
        (numeric_match == CTOOL_FALSE &&
         fixed_match == CTOOL_FALSE) ||
        (fixed_match == CTOOL_TRUE &&
         (cemit_ir_type_is_represented_integer(
              context, operand->type) == CTOOL_FALSE ||
          cemit_ir_type_is_represented_integer(
              context, matched->type) == CTOOL_FALSE)) ||
        context->unit->layout.types[operand->type].size !=
            context->unit->layout.types[matched->type].size) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  status = cemit_plan_assembly_registers(context, assembly, registers);
  ebx_byte_offset = assembly->output_count * 4u;
  if (status == CTOOL_OK) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, ebx_byte_offset, 3u);
  }
  for (input = assembly->input_count; status == CTOOL_OK && input != 0u;) {
    const ctool_c_assembly_operand_t *operand;
    input--;
    operand = &context->unit->assembly_operands[
        assembly->first_operand + assembly->output_count + input];
    if (operand->matching_output >= assembly->output_count) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32,
        registers[operand->matching_output], 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_emit_assembly_template(context, assembly, registers);
  }
  for (output = 0u; status == CTOOL_OK &&
                      output < assembly->output_count;
       output++) {
    status = cemit_x86_store_local_register(
        context, temporary_offset, output * 4u, registers[output]);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_local_register(
        context, 3u, temporary_offset, ebx_byte_offset);
  }
  for (output = assembly->output_count;
       status == CTOOL_OK && output != 0u;) {
    const ctool_c_assembly_operand_t *operand;
    output--;
    operand = &context->unit->assembly_operands[
        assembly->first_operand + output];
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_load_local_register(
          context, 1u, temporary_offset, output * 4u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_ecx_at_eax(context, operand->type);
    }
  }
  return status;
}

static ctool_bool cemit_atomic_order_valid(
    ctool_c_ir_instruction_kind_t kind, ctool_u32 order) {
  if (order > 5u) {
    return CTOOL_FALSE;
  }
  if (kind == CTOOL_C_IR_INSTRUCTION_ATOMIC_LOAD) {
    return order == 0u || order == 1u || order == 2u || order == 5u
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (kind == CTOOL_C_IR_INSTRUCTION_ATOMIC_STORE) {
    return order == 0u || order == 3u || order == 5u ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_status_t cemit_emit_atomic(
    cemit_context_t *context,
    const ctool_c_ir_instruction_t *instruction) {
  const ctool_c_type_node_t *pointer;
  const ctool_c_type_node_t *object;
  ctool_u32 pointer_qualifiers;
  ctool_u32 object_qualifiers;
  ctool_status_t status;
  if (cemit_ir_type_is_represented_integer(
          context, instruction->type) == CTOOL_FALSE ||
      cemit_ir_type_is_i32_pointer(
          context, instruction->input_type) == CTOOL_FALSE ||
      cemit_underlying_type(
          context, instruction->input_type, &pointer_qualifiers,
          &pointer) == CTOOL_FALSE ||
      pointer->kind != CTOOL_C_TYPE_POINTER ||
      cemit_underlying_type(
          context, pointer->referenced_type, &object_qualifiers,
          &object) == CTOOL_FALSE ||
      cemit_ir_scalar_types_match(
          context, pointer->referenced_type, instruction->type) ==
          CTOOL_FALSE ||
      ((instruction->kind == CTOOL_C_IR_INSTRUCTION_ATOMIC_FETCH_ADD ||
        instruction->kind == CTOOL_C_IR_INSTRUCTION_ATOMIC_FETCH_OR) &&
       object->kind == CTOOL_C_TYPE_BOOL) ||
      (instruction->kind != CTOOL_C_IR_INSTRUCTION_ATOMIC_LOAD &&
       (object_qualifiers & CTOOL_C_QUAL_CONST) != 0u) ||
      instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instruction->conversion != CTOOL_C_CONVERSION_NONE ||
      instruction->argument_count != 0u ||
      instruction->first_argument_type != CTOOL_C_AST_NONE ||
      instruction->reference != CTOOL_C_AST_NONE ||
      instruction->integer_bits > 5u ||
      cemit_atomic_order_valid(
          instruction->kind, (ctool_u32)instruction->integer_bits) ==
          CTOOL_FALSE) {
    return CTOOL_ERR_INTERNAL;
  }
  (void)pointer_qualifiers;
  if (instruction->kind == CTOOL_C_IR_INSTRUCTION_ATOMIC_LOAD) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_load_eax(context, instruction->type);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  if (instruction->kind ==
      CTOOL_C_IR_INSTRUCTION_ATOMIC_FETCH_OR) {
    return cemit_emit_atomic_fetch_or(context, instruction->type);
  }
  status = cemit_x86_one_register(
      context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (instruction->kind == CTOOL_C_IR_INSTRUCTION_ATOMIC_STORE &&
      instruction->integer_bits != 5u) {
    return cemit_x86_store_ecx_at_eax(context, instruction->type);
  }
  if (instruction->kind ==
      CTOOL_C_IR_INSTRUCTION_ATOMIC_FETCH_ADD) {
    status = cemit_x86_atomic_memory_ecx(
        context, CTOOL_X86_MN_XADD, instruction->type, CTOOL_TRUE);
  } else {
    status = cemit_x86_atomic_memory_ecx(
        context, CTOOL_X86_MN_XCHG, instruction->type, CTOOL_FALSE);
  }
  if (status != CTOOL_OK ||
      instruction->kind == CTOOL_C_IR_INSTRUCTION_ATOMIC_STORE) {
    return status;
  }
  status = cemit_x86_two_registers(
      context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 0u,
      CTOOL_X86_REG_GPR32, 1u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_canonicalize_eax_lane(
        context, instruction->type);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  return status;
}

static ctool_status_t cemit_emit_ir_instruction(
    cemit_context_t *context,
    const ctool_c_ir_instruction_t *ir_instruction,
    const ctool_c_type_node_t *function_type,
    const ctool_u32 *block_binding_offsets, ctool_u32 ir_offset,
    ctool_u32 value_temporary_offset,
    ctool_u32 stack_base_residue, ctool_u32 frame_size,
    ctool_u32 stack_depth,
    ctool_u32 *branch_patches, ctool_u32 *branch_afters) {
  ctool_status_t status;
  if (ir_instruction->kind != CTOOL_C_IR_INSTRUCTION_CALL_DIRECT &&
      ir_instruction->kind != CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT &&
      ir_instruction->argument_count != 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_ASSEMBLY) {
    return cemit_emit_assembly(
        context, ir_instruction, value_temporary_offset);
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_ATOMIC_LOAD ||
      ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_ATOMIC_STORE ||
      ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_ATOMIC_EXCHANGE ||
      ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_ATOMIC_FETCH_ADD ||
      ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_ATOMIC_FETCH_OR) {
    return cemit_emit_atomic(context, ir_instruction);
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS) {
    ctool_u32 relative_parameter;
    ctool_u32 parameter_offset;
    if (ir_instruction->reference < function_type->first_parameter ||
        ir_instruction->reference - function_type->first_parameter >=
            function_type->parameter_count ||
        ir_instruction->reference >= context->unit->parameter_count ||
        ir_instruction->reference >=
            context->unit->graph.parameter_type_count ||
        context->unit->parameters[ir_instruction->reference].type !=
            ir_instruction->type ||
        context->unit->graph.parameter_types[ir_instruction->reference] >=
            context->unit->graph.type_count ||
        ((cemit_ir_type_is_value_scalar(
              context, ir_instruction->type) == CTOOL_TRUE &&
          cemit_ir_scalar_types_match(
              context,
              context->unit->graph
                  .parameter_types[ir_instruction->reference],
              ir_instruction->type) == CTOOL_FALSE) ||
         (cemit_ir_type_is_structure_value(
              context, ir_instruction->type) == CTOOL_TRUE &&
          cemit_ir_structure_types_match(
              context,
              context->unit->graph
                  .parameter_types[ir_instruction->reference],
              ir_instruction->type) == CTOOL_FALSE) ||
         (cemit_ir_type_is_value_scalar(
              context, ir_instruction->type) == CTOOL_FALSE &&
          cemit_ir_type_is_structure_value(
              context, ir_instruction->type) == CTOOL_FALSE)) ||
        ir_instruction->input_type != CTOOL_C_TYPE_NONE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    relative_parameter =
        ir_instruction->reference - function_type->first_parameter;
    status = cemit_ir_parameter_offset(
        context, function_type, relative_parameter, &parameter_offset);
    if (status != CTOOL_OK) {
      return status;
    }
    status = cemit_x86_lea_parameter(context, parameter_offset);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS) {
    const ctool_c_block_binding_t *binding;
    const ctool_c_type_layout_t *layout;
    ctool_u32 offset;
    if (block_binding_offsets == (const ctool_u32 *)0 ||
        ir_instruction->reference >= context->unit->block_binding_count ||
        ir_instruction->input_type != CTOOL_C_TYPE_NONE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    binding = &context->unit->block_bindings[ir_instruction->reference];
    layout = ir_instruction->type < context->unit->layout.type_count
                 ? &context->unit->layout.types[ir_instruction->type]
                 : (const ctool_c_type_layout_t *)0;
    if (binding->storage == CTOOL_C_STORAGE_STATIC) {
      ctool_u32 symbol;
      if (context->block_binding_symbols == (ctool_u32 *)0 ||
          binding->kind != CTOOL_C_BINDING_OBJECT ||
          binding->type != ir_instruction->type ||
          layout == (const ctool_c_type_layout_t *)0 ||
          layout->is_object == CTOOL_FALSE ||
          layout->is_complete_object == CTOOL_FALSE || layout->size == 0u ||
          layout->alignment == 0u ||
          cemit_power_of_two(layout->alignment) == CTOOL_FALSE ||
          (cemit_ir_type_is_value_scalar(
               context, ir_instruction->type) == CTOOL_FALSE &&
           cemit_ir_type_is_complete_aggregate_object(
               context, ir_instruction->type) == CTOOL_FALSE)) {
        return CTOOL_ERR_INTERNAL;
      }
      symbol =
          context->block_binding_symbols[ir_instruction->reference];
      if (symbol == CTOOL_C_AST_NONE || symbol >= context->symbol_count ||
          context->symbols[symbol].binding != CTOOL_ELF32_BIND_LOCAL ||
          context->symbols[symbol].type != CTOOL_ELF32_SYMBOL_OBJECT ||
          context->symbols[symbol].placement !=
              CTOOL_ELF32_SYMBOL_DEFINED) {
        return CTOOL_ERR_INTERNAL;
      }
      return cemit_x86_push_symbol(context, symbol);
    }
    offset = block_binding_offsets[ir_instruction->reference];
    if (binding->kind != CTOOL_C_BINDING_OBJECT ||
        (binding->storage != CTOOL_C_STORAGE_NONE &&
         binding->storage != CTOOL_C_STORAGE_AUTO &&
         binding->storage != CTOOL_C_STORAGE_REGISTER) ||
        binding->type != ir_instruction->type || offset == CTOOL_C_AST_NONE ||
        offset == 0u || layout == (const ctool_c_type_layout_t *)0 ||
        cemit_ir_type_is_automatic_object(context, ir_instruction->type) ==
            CTOOL_FALSE ||
        offset < layout->size || offset > 0x7fffffffu ||
        (offset & (layout->alignment - 1u)) != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_lea_local(context, offset);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_VARIADIC_START) {
    ctool_u32 final_parameter;
    ctool_u32 parameter_offset;
    ctool_u32 parameter_size;
    if (function_type == (const ctool_c_type_node_t *)0 ||
        function_type->kind != CTOOL_C_TYPE_FUNCTION ||
        function_type->variadic == CTOOL_FALSE ||
        function_type->parameter_count == 0u ||
        function_type->first_parameter >
            context->unit->graph.parameter_type_count ||
        function_type->parameter_count >
            context->unit->graph.parameter_type_count -
                function_type->first_parameter ||
        function_type->first_parameter > context->unit->parameter_count ||
        function_type->parameter_count >
            context->unit->parameter_count -
                function_type->first_parameter ||
        ir_instruction->type != CTOOL_C_TYPE_NONE ||
        cemit_ir_type_is_variadic_cursor(
            context, ir_instruction->input_type) == CTOOL_FALSE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    final_parameter = function_type->first_parameter +
                      function_type->parameter_count - 1u;
    if (ir_instruction->reference != final_parameter) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_ir_parameter_offset(
        context, function_type, function_type->parameter_count - 1u,
        &parameter_offset);
    if (status == CTOOL_OK) {
      status = cemit_ir_argument_size(
          context,
          context->unit->graph.parameter_types[final_parameter],
          &parameter_size);
    }
    if (status != CTOOL_OK ||
        cemit_add_overflows(parameter_offset, parameter_size) == CTOOL_TRUE ||
        parameter_offset + parameter_size > 0x7fffffffu) {
      return status == CTOOL_OK ? CTOOL_ERR_OVERFLOW : status;
    }
    parameter_offset += parameter_size;
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 2u,
          CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_lea_parameter(context, parameter_offset);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 1u,
          CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 2u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_ecx_at_eax(
          context, ir_instruction->input_type);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_VARIADIC_ARGUMENT) {
    ctool_bool indirect =
        cemit_ir_type_is_wide_integer(context, ir_instruction->type) ==
                CTOOL_TRUE ||
                (cemit_ir_type_is_floating_value(
                     context, ir_instruction->type) == CTOOL_TRUE &&
                 context->unit->layout.types[ir_instruction->type].size == 8u)
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if (cemit_ir_type_is_variadic_cursor(
            context, ir_instruction->input_type) == CTOOL_FALSE ||
        cemit_ir_type_is_variadic_argument(context, ir_instruction->type) ==
            CTOOL_FALSE ||
        (indirect == CTOOL_TRUE &&
         (value_temporary_offset == CTOOL_C_AST_NONE ||
          value_temporary_offset < 8u ||
          value_temporary_offset > 0x7fffffffu ||
          (value_temporary_offset & 3u) != 0u)) ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 2u,
          CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_load_eax(context, ir_instruction->input_type);
    }
    if (status == CTOOL_OK && indirect == CTOOL_TRUE) {
      status = cemit_x86_push_wide_variadic_snapshot(
          context, value_temporary_offset, ir_instruction->input_type);
    }
    if (indirect == CTOOL_TRUE) {
      return status;
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_add_eax_constant(context, 4u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 1u,
          CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 2u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_ecx_at_eax(
          context, ir_instruction->input_type);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_load_eax(context, ir_instruction->type);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_VARIADIC_END) {
    if (ir_instruction->type != CTOOL_C_TYPE_NONE ||
        cemit_ir_type_is_variadic_cursor(
            context, ir_instruction->input_type) == CTOOL_FALSE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    return cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (ir_instruction->kind ==
          CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS ||
      ir_instruction->kind ==
          CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS) {
    const ctool_c_expression_t *expression;
    const ctool_c_type_layout_t *layout;
    const ctool_u32 *offsets =
        ir_instruction->kind ==
                CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS
            ? context->compound_literal_offsets
            : context->compound_literal_staging_offsets;
    ctool_u32 offset;
    if (offsets == (const ctool_u32 *)0 ||
        ir_instruction->reference >= context->unit->expression_count ||
        ir_instruction->input_type != CTOOL_C_TYPE_NONE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    expression =
        &context->unit->expressions[ir_instruction->reference];
    layout = ir_instruction->type < context->unit->layout.type_count
                 ? &context->unit->layout.types[ir_instruction->type]
                 : (const ctool_c_type_layout_t *)0;
    offset = offsets[ir_instruction->reference];
    if (expression->kind != CTOOL_C_EXPRESSION_COMPOUND_LITERAL ||
        expression->type != ir_instruction->type ||
        expression->reference >= context->unit->initializer_count ||
        context->unit->initializers[expression->reference].type !=
            ir_instruction->type ||
        layout == (const ctool_c_type_layout_t *)0 ||
        cemit_ir_type_is_automatic_object(context, ir_instruction->type) ==
            CTOOL_FALSE ||
        (ir_instruction->kind ==
             CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS &&
         (context->unit->initializers[expression->reference].kind !=
              CTOOL_C_INITIALIZER_LIST ||
          cemit_ir_type_is_initializable_aggregate_object(
              context, ir_instruction->type) == CTOOL_FALSE)) ||
        offset == CTOOL_C_AST_NONE || offset == 0u ||
        offset < layout->size || offset > 0x7fffffffu ||
        (offset & (layout->alignment - 1u)) != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_lea_local(context, offset);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind ==
      CTOOL_C_IR_INSTRUCTION_STRING_LITERAL_ADDRESS) {
    const ctool_c_expression_t *expression;
    ctool_u32 symbol;
    if (ir_instruction->reference >= context->unit->expression_count ||
        ir_instruction->input_type != CTOOL_C_TYPE_NONE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    expression = &context->unit->expressions[ir_instruction->reference];
    if (expression->kind != CTOOL_C_EXPRESSION_STRING ||
        expression->type != ir_instruction->type ||
        expression->child_count != 0u ||
        expression->first_child != CTOOL_C_AST_NONE ||
        expression->reference != CTOOL_C_AST_NONE ||
        expression->string_bytes.data == (const ctool_u8 *)0 ||
        expression->string_bytes.size == 0u ||
        cemit_ir_type_is_character_array(
            context, ir_instruction->type) == CTOOL_FALSE ||
        ir_instruction->type >= context->unit->layout.type_count ||
        expression->string_bytes.size !=
            context->unit->layout.types[ir_instruction->type].size) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_add_literal_bytes(
        context, expression->string_bytes, &symbol);
    return status == CTOOL_OK ? cemit_x86_push_symbol(context, symbol)
                              : status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT) {
    const ctool_c_type_layout_t *layout =
        ir_instruction->type < context->unit->layout.type_count
            ? &context->unit->layout.types[ir_instruction->type]
            : (const ctool_c_type_layout_t *)0;
    if (layout == (const ctool_c_type_layout_t *)0 ||
        cemit_ir_type_is_initializable_aggregate_object(
            context, ir_instruction->type) == CTOOL_FALSE ||
        layout->size == 0u ||
        ir_instruction->input_type != ir_instruction->type ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 7u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 7u,
          CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_move_register_constant(context, 1u, layout->size);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_no_operand(context, CTOOL_X86_MN_CLD);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_repeat_string(context, CTOOL_X86_MN_STOSB, 8u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 7u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_COPY_OBJECT) {
    const ctool_c_type_layout_t *layout =
        ir_instruction->type < context->unit->layout.type_count
            ? &context->unit->layout.types[ir_instruction->type]
            : (const ctool_c_type_layout_t *)0;
    if (layout == (const ctool_c_type_layout_t *)0 ||
        cemit_ir_type_is_initializable_aggregate_object(
            context, ir_instruction->type) == CTOOL_FALSE ||
        layout->size == 0u ||
        ir_instruction->input_type != ir_instruction->type ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 2u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_copy_edx_to_eax(context, layout->size);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_COPY_STRING) {
    const ctool_c_initializer_t *initializer;
    const ctool_c_type_layout_t *layout;
    ctool_u32 symbol;
    if (ir_instruction->reference >= context->unit->initializer_count ||
        ir_instruction->input_type != ir_instruction->type ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->integer_bits != 0u ||
        cemit_ir_type_is_character_array(
            context, ir_instruction->type) == CTOOL_FALSE ||
        ir_instruction->type >= context->unit->layout.type_count) {
      return CTOOL_ERR_INTERNAL;
    }
    initializer =
        &context->unit->initializers[ir_instruction->reference];
    layout = &context->unit->layout.types[ir_instruction->type];
    if (initializer->kind != CTOOL_C_INITIALIZER_STRING ||
        initializer->type != ir_instruction->type ||
        initializer->expression != CTOOL_C_AST_NONE ||
        initializer->integer_bits != 0u ||
        initializer->string_bytes.data == (const ctool_u8 *)0 ||
        initializer->string_bytes.size == 0u ||
        initializer->string_bytes.size > layout->size ||
        initializer->address_kind != CTOOL_C_INITIALIZER_ADDRESS_NONE ||
        initializer->address_reference != CTOOL_C_AST_NONE ||
        initializer->address_addend != 0 ||
        initializer->first_element != CTOOL_C_AST_NONE ||
        initializer->element_count != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_add_literal_bytes(
        context, initializer->string_bytes, &symbol);
    if (status == CTOOL_OK) {
      status = cemit_x86_push_symbol(context, symbol);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 2u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_copy_edx_to_eax(
          context, initializer->string_bytes.size);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS) {
    const ctool_c_binding_t *binding;
    ctool_u32 symbol;
    if (ir_instruction->reference >= context->unit->binding_count ||
        ir_instruction->input_type != CTOOL_C_TYPE_NONE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    binding = &context->unit->bindings[ir_instruction->reference];
    symbol = context->binding_symbols[ir_instruction->reference];
    if (binding->kind != CTOOL_C_BINDING_OBJECT ||
        binding->type != ir_instruction->type ||
        symbol == CTOOL_C_AST_NONE || symbol >= context->symbol_count ||
        (cemit_ir_type_is_value_scalar(context,
                                       ir_instruction->type) ==
             CTOOL_FALSE &&
         cemit_ir_type_is_complete_aggregate_object(
             context, ir_instruction->type) == CTOOL_FALSE &&
         cemit_ir_type_is_addressable_unspecified_array(
             context, ir_instruction->type) == CTOOL_FALSE)) {
      return CTOOL_ERR_INTERNAL;
    }
    return cemit_x86_push_symbol(context, symbol);
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_FUNCTION_ADDRESS) {
    const ctool_c_binding_t *binding;
    const ctool_c_type_node_t *addressed_type;
    ctool_u32 symbol;
    if (ir_instruction->reference >= context->unit->binding_count ||
        ir_instruction->input_type != CTOOL_C_TYPE_NONE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    binding = &context->unit->bindings[ir_instruction->reference];
    addressed_type = cemit_unwrapped_type(context, ir_instruction->type);
    symbol = context->binding_symbols[ir_instruction->reference];
    if (binding->kind != CTOOL_C_BINDING_FUNCTION ||
        cemit_ir_function_types_match(
            context, binding->type, ir_instruction->type) == CTOOL_FALSE ||
        addressed_type == (const ctool_c_type_node_t *)0 ||
        addressed_type->kind != CTOOL_C_TYPE_FUNCTION ||
        symbol == CTOOL_C_AST_NONE || symbol >= context->symbol_count) {
      return CTOOL_ERR_INTERNAL;
    }
    return cemit_x86_push_symbol(context, symbol);
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_ELEMENT_ADDRESS) {
    const ctool_c_type_node_t *array =
        cemit_unwrapped_type(context, ir_instruction->input_type);
    const ctool_c_type_layout_t *array_layout =
        ir_instruction->input_type < context->unit->layout.type_count
            ? &context->unit->layout.types[ir_instruction->input_type]
            : (const ctool_c_type_layout_t *)0;
    const ctool_c_type_layout_t *element_layout =
        ir_instruction->type < context->unit->layout.type_count
            ? &context->unit->layout.types[ir_instruction->type]
            : (const ctool_c_type_layout_t *)0;
    ctool_u32 offset;
    if (array == (const ctool_c_type_node_t *)0 ||
        array_layout == (const ctool_c_type_layout_t *)0 ||
        element_layout == (const ctool_c_type_layout_t *)0 ||
        array->kind != CTOOL_C_TYPE_ARRAY ||
        array->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
        array->referenced_type != ir_instruction->type ||
        ir_instruction->reference >= array->element_count ||
        array_layout->is_object == CTOOL_FALSE ||
        array_layout->is_complete_object == CTOOL_FALSE ||
        element_layout->is_object == CTOOL_FALSE ||
        element_layout->is_complete_object == CTOOL_FALSE ||
        element_layout->size == 0u ||
        cemit_multiply_overflows(array->element_count,
                                 element_layout->size) == CTOOL_TRUE ||
        array->element_count * element_layout->size != array_layout->size ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->integer_bits != 0u ||
        cemit_multiply_overflows(ir_instruction->reference,
                                 element_layout->size) == CTOOL_TRUE) {
      return CTOOL_ERR_INTERNAL;
    }
    offset = ir_instruction->reference * element_layout->size;
    if (offset > array_layout->size ||
        element_layout->size > array_layout->size - offset) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_add_eax_constant(context, offset);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS) {
    cemit_member_info_t info;
    status = cemit_validate_member_instruction(
        context, ir_instruction, CTOOL_C_CONVERSION_NONE, &info);
    if (status != CTOOL_OK) {
      return status;
    }
    if (info.member->is_bit_field == CTOOL_TRUE ||
        info.member->bit_width != 0u || info.member_layout->bit_width != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_add_eax_constant(context,
                                           info.member_layout->byte_offset);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_BIT_FIELD_LOAD) {
    cemit_member_info_t info;
    ctool_u32 left_shift;
    ctool_u32 right_shift;
    status = cemit_validate_i32_bit_field_instruction(
        context, ir_instruction, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
        &info);
    if (status != CTOOL_OK) {
      return status;
    }
    left_shift =
        32u - info.member_layout->bit_offset - info.member_layout->bit_width;
    right_shift = 32u - info.member_layout->bit_width;
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_add_eax_constant(context,
                                           info.member_layout->byte_offset);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_load_eax(context, ir_instruction->type);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_shift_eax(context, CTOOL_X86_MN_SHL,
                                    left_shift);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_shift_eax(
          context,
          info.result_layout->is_signed == CTOOL_TRUE ? CTOOL_X86_MN_SAR
                                                       : CTOOL_X86_MN_SHR,
          right_shift);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind ==
          CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_VALUE ||
      ir_instruction->kind ==
          CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_OLD_VALUE) {
    cemit_member_info_t info;
    ctool_bool preserve_old =
        ir_instruction->kind ==
                CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_OLD_VALUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_u32 value_mask;
    ctool_u32 field_mask;
    status = cemit_validate_i32_bit_field_instruction(
        context, ir_instruction, CTOOL_C_CONVERSION_NONE, &info);
    if (status != CTOOL_OK) {
      return status;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
    if (status == CTOOL_OK && preserve_old == CTOOL_TRUE) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 2u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK && preserve_old == CTOOL_TRUE) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 2u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_add_eax_constant(context,
                                           info.member_layout->byte_offset);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (info.member_layout->bit_width == 32u) {
      status = cemit_x86_store_ecx_at_eax(context, ir_instruction->type);
      if (status == CTOOL_OK && preserve_old == CTOOL_FALSE) {
        status = cemit_x86_one_register(
            context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 1u, 32u);
      }
      return status;
    }
    value_mask = (1u << info.member_layout->bit_width) - 1u;
    field_mask = value_mask << info.member_layout->bit_offset;
    status = cemit_x86_and_register_constant(context, 1u, value_mask);
    if (status == CTOOL_OK && preserve_old == CTOOL_FALSE) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 2u,
          CTOOL_X86_REG_GPR32, 1u, 32u);
    }
    if (status == CTOOL_OK && preserve_old == CTOOL_FALSE &&
        info.result_layout->is_signed == CTOOL_TRUE) {
      status = cemit_x86_shift_register(
          context, CTOOL_X86_MN_SHL, 2u,
          32u - info.member_layout->bit_width);
    }
    if (status == CTOOL_OK && preserve_old == CTOOL_FALSE &&
        info.result_layout->is_signed == CTOOL_TRUE) {
      status = cemit_x86_shift_register(
          context, CTOOL_X86_MN_SAR, 2u,
          32u - info.member_layout->bit_width);
    }
    if (status == CTOOL_OK && preserve_old == CTOOL_FALSE) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 2u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_shift_register(
          context, CTOOL_X86_MN_SHL, 1u,
          info.member_layout->bit_offset);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_load_eax(context, ir_instruction->type);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_and_register_constant(context, 0u, ~field_mask);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_OR, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 1u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 2u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_eax_at_edx(context);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD) {
    ctool_bool scalar = cemit_ir_scalar_types_match(
        context, ir_instruction->input_type, ir_instruction->type);
    ctool_bool structure = cemit_ir_structure_types_match(
        context, ir_instruction->input_type, ir_instruction->type);
    ctool_bool indirect =
        cemit_ir_type_is_wide_integer(context, ir_instruction->type) ==
                CTOOL_TRUE ||
                (cemit_ir_type_is_floating_value(
                     context, ir_instruction->type) == CTOOL_TRUE &&
                 context->unit->layout.types[ir_instruction->type].size == 8u)
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if ((scalar == CTOOL_FALSE && structure == CTOOL_FALSE) ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion !=
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    if (structure == CTOOL_TRUE || indirect == CTOOL_TRUE) {
      const ctool_c_type_layout_t *layout =
          &context->unit->layout.types[ir_instruction->type];
      if (value_temporary_offset == CTOOL_C_AST_NONE ||
          value_temporary_offset == 0u ||
          value_temporary_offset < layout->size ||
          value_temporary_offset > 0x7fffffffu ||
          (value_temporary_offset & (layout->alignment - 1u)) != 0u) {
        return CTOOL_ERR_INTERNAL;
      }
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 2u, 32u);
      if (status == CTOOL_OK) {
        status = cemit_x86_lea_local(
            context, value_temporary_offset);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_copy_edx_to_eax(context, layout->size);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_one_register(
            context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
      }
      return status;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_load_eax(context, ir_instruction->type);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_DEREFERENCE) {
    const ctool_c_type_node_t *pointer =
        cemit_unwrapped_type(context, ir_instruction->input_type);
    const ctool_c_type_node_t *referent =
        cemit_unwrapped_type(context, ir_instruction->type);
    if (pointer == (const ctool_c_type_node_t *)0 ||
        referent == (const ctool_c_type_node_t *)0 ||
        cemit_ir_type_is_i32_pointer_value(
            context, ir_instruction->input_type) ==
            CTOOL_FALSE ||
        pointer->referenced_type != ir_instruction->type ||
        (referent->kind != CTOOL_C_TYPE_FUNCTION &&
         cemit_ir_type_is_i32_pointer(
             context, ir_instruction->input_type) == CTOOL_FALSE) ||
        ir_instruction->operation !=
            CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    return CTOOL_OK;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_ADDRESS_OF) {
    const ctool_c_type_node_t *pointer =
        cemit_unwrapped_type(context, ir_instruction->type);
    const ctool_c_type_node_t *addressed =
        cemit_unwrapped_type(context, ir_instruction->input_type);
    if (pointer == (const ctool_c_type_node_t *)0 ||
        addressed == (const ctool_c_type_node_t *)0 ||
        cemit_ir_type_is_i32_pointer_value(context, ir_instruction->type) ==
            CTOOL_FALSE ||
        (addressed->kind != CTOOL_C_TYPE_FUNCTION &&
         (ir_instruction->input_type >= context->unit->layout.type_count ||
          context->unit->layout.types[ir_instruction->input_type].is_object ==
              CTOOL_FALSE ||
          context->unit->layout.types[ir_instruction->input_type]
                  .is_complete_object == CTOOL_FALSE)) ||
        pointer->referenced_type != ir_instruction->input_type ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_ADDRESS ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    return CTOOL_OK;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER) {
    if (cemit_ir_array_decay_types_match(
            context, ir_instruction->input_type,
            ir_instruction->type) == CTOOL_FALSE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_ARRAY_TO_POINTER ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    return CTOOL_OK;
  }
  if (ir_instruction->kind ==
      CTOOL_C_IR_INSTRUCTION_FUNCTION_TO_POINTER) {
    const ctool_c_type_node_t *pointer =
        cemit_unwrapped_type(context, ir_instruction->type);
    const ctool_c_type_node_t *function =
        cemit_unwrapped_type(context, ir_instruction->input_type);
    if (cemit_ir_type_is_i32_function_pointer(
            context, ir_instruction->type) == CTOOL_FALSE ||
        pointer == (const ctool_c_type_node_t *)0 ||
        function == (const ctool_c_type_node_t *)0 ||
        pointer->kind != CTOOL_C_TYPE_POINTER ||
        function->kind != CTOOL_C_TYPE_FUNCTION ||
        pointer->referenced_type != ir_instruction->input_type ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion !=
            CTOOL_C_CONVERSION_FUNCTION_TO_POINTER ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    return CTOOL_OK;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE ||
      ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE_VALUE) {
    ctool_bool preserve_value =
        ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_STORE_VALUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool scalar = cemit_ir_scalar_types_match(
        context, ir_instruction->type, ir_instruction->input_type);
    ctool_bool structure = cemit_ir_structure_types_match(
        context, ir_instruction->type, ir_instruction->input_type);
    ctool_bool indirect =
        cemit_ir_type_is_wide_integer(context, ir_instruction->type) ==
                CTOOL_TRUE ||
                (cemit_ir_type_is_floating_value(
                     context, ir_instruction->type) == CTOOL_TRUE &&
                 context->unit->layout.types[ir_instruction->type].size == 8u)
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if ((scalar == CTOOL_FALSE && structure == CTOOL_FALSE) ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    if (structure == CTOOL_TRUE || indirect == CTOOL_TRUE) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 2u, 32u);
      if (status == CTOOL_OK) {
        status = cemit_x86_one_register(
            context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_copy_edx_to_eax(
            context,
            context->unit->layout.types[ir_instruction->type].size);
      }
      if (status == CTOOL_OK && preserve_value == CTOOL_TRUE) {
        status = cemit_x86_one_register(
            context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 2u, 32u);
      }
      return status;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_store_ecx_at_eax(context, ir_instruction->type);
    }
    if (status == CTOOL_OK && preserve_value == CTOOL_TRUE) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 1u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_DUPLICATE_VALUE ||
      ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS) {
    ctool_bool supported_type =
        ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_DUPLICATE_VALUE
            ? cemit_ir_type_is_value_scalar(context, ir_instruction->type)
            : (cemit_ir_type_is_value_scalar(
                   context, ir_instruction->type) == CTOOL_TRUE ||
               cemit_ir_type_is_complete_record_object(
                   context, ir_instruction->type) == CTOOL_TRUE)
                  ? CTOOL_TRUE
                  : CTOOL_FALSE;
    if (ir_instruction->type != ir_instruction->input_type ||
        supported_type == CTOOL_FALSE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_DISCARD) {
    if (ir_instruction->type != CTOOL_C_TYPE_NONE ||
        (cemit_ir_type_is_value_scalar(
             context, ir_instruction->input_type) == CTOOL_FALSE &&
         cemit_ir_type_is_structure_value(
             context, ir_instruction->input_type) == CTOOL_FALSE) ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    return cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_INTEGER) {
    if (ir_instruction->input_type != CTOOL_C_TYPE_NONE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE) {
      return CTOOL_ERR_INTERNAL;
    }
    if (cemit_ir_type_is_wide_integer(
            context, ir_instruction->type) == CTOOL_TRUE) {
      return cemit_x86_push_wide_constant_snapshot(
          context, value_temporary_offset, ir_instruction->integer_bits);
    }
    if (cemit_ir_type_is_represented_integer(
            context, ir_instruction->type) == CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    }
    return cemit_x86_push_integer(
        context, (ctool_u32)ir_instruction->integer_bits);
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_FLOATING) {
    const ctool_c_type_node_t *type =
        cemit_unwrapped_type(context, ir_instruction->type);
    const ctool_c_type_layout_t *layout =
        ir_instruction->type < context->unit->layout.type_count
            ? &context->unit->layout.types[ir_instruction->type]
            : (const ctool_c_type_layout_t *)0;
    if (type == (const ctool_c_type_node_t *)0 ||
        (type->kind != CTOOL_C_TYPE_FLOAT &&
         type->kind != CTOOL_C_TYPE_DOUBLE) ||
        layout == (const ctool_c_type_layout_t *)0 ||
        cemit_ir_type_is_floating_value(
            context, ir_instruction->type) == CTOOL_FALSE ||
        ir_instruction->input_type != CTOOL_C_TYPE_NONE ||
        ir_instruction->operation !=
            CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        (layout->size == 4u &&
         ir_instruction->integer_bits > 0xffffffffull) ||
        (layout->size != 4u && layout->size != 8u)) {
      return CTOOL_ERR_INTERNAL;
    }
    return layout->size == 4u
               ? cemit_x86_push_integer(
                     context,
                     (ctool_u32)ir_instruction->integer_bits)
               : cemit_x86_push_wide_constant_snapshot(
                     context, value_temporary_offset,
                     ir_instruction->integer_bits);
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_CONVERT) {
    ctool_bool source_wide = cemit_ir_type_is_wide_integer(
        context, ir_instruction->input_type);
    ctool_bool target_wide = cemit_ir_type_is_wide_integer(
        context, ir_instruction->type);
    ctool_bool bit_field_promotion =
        cemit_bit_field_promotion_is_valid(context, ir_instruction);
    ctool_bool integer_conversion =
        cemit_ir_integer_conversion_is_valid(
            context, ir_instruction->input_type, ir_instruction->type,
            ir_instruction->conversion) == CTOOL_TRUE ||
                bit_field_promotion == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool floating_conversion =
        cemit_ir_floating_conversion_is_valid(
            context, ir_instruction->input_type, ir_instruction->type,
            ir_instruction->conversion);
    ctool_bool pointer_conversion =
        cemit_ir_type_is_i32_pointer_value(
            context, ir_instruction->input_type) ==
                CTOOL_TRUE &&
                cemit_ir_type_is_i32_pointer_value(
                    context, ir_instruction->type) ==
                    CTOOL_TRUE &&
                cemit_ir_pointer_conversion_is_valid(
                    context, ir_instruction->input_type,
                    ir_instruction->type,
                    ir_instruction->conversion) == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool null_conversion =
        (cemit_ir_type_is_i32_integer(
             context, ir_instruction->input_type) == CTOOL_TRUE ||
         cemit_ir_type_is_i32_void_pointer(
             context, ir_instruction->input_type) == CTOOL_TRUE) &&
                cemit_ir_type_is_i32_pointer_value(
                    context, ir_instruction->type) ==
                    CTOOL_TRUE &&
                ir_instruction->conversion ==
                    CTOOL_C_CONVERSION_NULL_POINTER
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool explicit_scalar_conversion =
        ir_instruction->conversion == CTOOL_C_CONVERSION_NONE &&
                ((cemit_ir_type_is_i32_scalar(
                      context, ir_instruction->input_type) == CTOOL_TRUE &&
                  cemit_ir_type_is_i32_scalar(
                      context, ir_instruction->type) == CTOOL_TRUE) ||
                 (cemit_ir_type_is_represented_integer(
                      context, ir_instruction->input_type) == CTOOL_TRUE &&
                  cemit_ir_type_is_represented_integer(
                      context, ir_instruction->type) == CTOOL_TRUE) ||
                 (cemit_ir_type_is_i32_pointer(
                      context, ir_instruction->input_type) == CTOOL_TRUE &&
                  cemit_ir_type_is_wide_integer(
                      context, ir_instruction->type) == CTOOL_TRUE) ||
                 (cemit_ir_type_is_wide_integer(
                      context, ir_instruction->input_type) == CTOOL_TRUE &&
                  cemit_ir_type_is_i32_pointer(
                      context, ir_instruction->type) == CTOOL_TRUE)) &&
                cemit_ir_type_is_i32_function_pointer(
                    context, ir_instruction->input_type) == CTOOL_FALSE &&
                cemit_ir_type_is_i32_function_pointer(
                    context, ir_instruction->type) == CTOOL_FALSE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool function_pointer_conversion =
        ir_instruction->conversion == CTOOL_C_CONVERSION_NONE &&
                cemit_ir_function_pointer_cast_is_valid(
                    context, ir_instruction->input_type,
                    ir_instruction->type) == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if ((integer_conversion == CTOOL_FALSE &&
         floating_conversion == CTOOL_FALSE &&
         pointer_conversion == CTOOL_FALSE &&
         null_conversion == CTOOL_FALSE &&
         explicit_scalar_conversion == CTOOL_FALSE &&
         function_pointer_conversion == CTOOL_FALSE) ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        (ir_instruction->conversion != CTOOL_C_CONVERSION_NONE &&
         ir_instruction->conversion != CTOOL_C_CONVERSION_QUALIFICATION &&
         ir_instruction->conversion !=
             CTOOL_C_CONVERSION_INTEGER_PROMOTION &&
         ir_instruction->conversion !=
             CTOOL_C_CONVERSION_USUAL_ARITHMETIC &&
         ir_instruction->conversion != CTOOL_C_CONVERSION_ASSIGNMENT &&
         ir_instruction->conversion != CTOOL_C_CONVERSION_POINTER &&
         ir_instruction->conversion != CTOOL_C_CONVERSION_NULL_POINTER &&
         ir_instruction->conversion !=
             CTOOL_C_CONVERSION_FLOAT_PROMOTION &&
         ir_instruction->conversion !=
             CTOOL_C_CONVERSION_COMPATIBILITY_POINTER) ||
        (pointer_conversion == CTOOL_TRUE &&
         explicit_scalar_conversion == CTOOL_FALSE &&
         ir_instruction->conversion != CTOOL_C_CONVERSION_QUALIFICATION &&
         ir_instruction->conversion != CTOOL_C_CONVERSION_POINTER &&
         ir_instruction->conversion !=
             CTOOL_C_CONVERSION_COMPATIBILITY_POINTER) ||
        (null_conversion == CTOOL_TRUE &&
         ir_instruction->conversion != CTOOL_C_CONVERSION_NULL_POINTER) ||
        (bit_field_promotion == CTOOL_FALSE &&
         ir_instruction->reference != CTOOL_C_AST_NONE) ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    if (floating_conversion == CTOOL_TRUE) {
      ctool_bool source_floating =
          cemit_ir_type_is_floating_value(
              context, ir_instruction->input_type);
      ctool_bool target_floating =
          cemit_ir_type_is_floating_value(
              context, ir_instruction->type);
      if (source_floating == CTOOL_FALSE &&
          target_floating == CTOOL_TRUE) {
        return cemit_x86_convert_integer_to_floating(
            context, ir_instruction->input_type,
            ir_instruction->type, value_temporary_offset);
      }
      if (source_floating == CTOOL_TRUE &&
          target_floating == CTOOL_FALSE) {
        return cemit_x86_convert_floating_to_integer(
            context, ir_instruction->input_type,
            ir_instruction->type, value_temporary_offset);
      }
      if (ir_instruction->input_type ==
          ir_instruction->type) {
        return CTOOL_OK;
      }
      status = cemit_x86_load_floating_stack_value(
          context, ir_instruction->input_type, 0u);
      if (status == CTOOL_OK) {
        status = cemit_x86_discard_arguments(context, 4u);
      }
      return status == CTOOL_OK
                 ? cemit_x86_push_floating_result(
                       context, ir_instruction->type,
                       value_temporary_offset)
                 : status;
    }
    if (source_wide == CTOOL_FALSE && target_wide == CTOOL_TRUE) {
      const ctool_c_type_layout_t *source_layout =
          &context->unit->layout.types[ir_instruction->input_type];
      return cemit_x86_push_widened_integer_snapshot(
          context, value_temporary_offset,
          cemit_ir_type_is_i32_pointer(
              context, ir_instruction->input_type) == CTOOL_TRUE
              ? CTOOL_FALSE
              : source_layout->is_signed);
    }
    if (source_wide == CTOOL_TRUE && target_wide == CTOOL_FALSE) {
      const ctool_c_type_node_t *target =
          cemit_unwrapped_type(context, ir_instruction->type);
      if (target == (const ctool_c_type_node_t *)0 ||
          (cemit_ir_type_is_represented_integer(
               context, ir_instruction->type) == CTOOL_FALSE &&
           cemit_ir_type_is_i32_pointer(
               context, ir_instruction->type) == CTOOL_FALSE)) {
        return CTOOL_ERR_INTERNAL;
      }
      if (cemit_ir_type_is_i32_pointer(
              context, ir_instruction->type) == CTOOL_TRUE) {
        status = cemit_x86_one_register(
            context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
        if (status == CTOOL_OK) {
          status = cemit_x86_load_register_at_register(
              context, 0u, 1u, 0u);
        }
        if (status == CTOOL_OK) {
          status = cemit_x86_one_register(
              context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
        }
        return status;
      }
      return cemit_x86_push_narrowed_wide_integer(
          context, ir_instruction->type,
          target->kind == CTOOL_C_TYPE_BOOL ? CTOOL_TRUE : CTOOL_FALSE);
    }
    if (integer_conversion == CTOOL_TRUE &&
        context->unit->layout.types[ir_instruction->type].size < 4u) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
      if (status == CTOOL_OK) {
        status = cemit_x86_convert_eax_to_integer(
            context, ir_instruction->type);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_one_register(
            context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
      }
      return status;
    }
    return CTOOL_OK;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_UNARY) {
    ctool_bool logical_not =
        ir_instruction->operation ==
                CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_NOT
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool wide_integer =
        logical_not == CTOOL_FALSE &&
                cemit_ir_type_is_wide_integer(
                    context, ir_instruction->input_type) == CTOOL_TRUE &&
                ir_instruction->input_type == ir_instruction->type
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool floating_unary =
        logical_not == CTOOL_FALSE &&
                ir_instruction->input_type == ir_instruction->type &&
                cemit_ir_type_is_floating_value(
                    context, ir_instruction->input_type) == CTOOL_TRUE &&
                (ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_UNARY_PLUS ||
                 ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE)
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if ((logical_not == CTOOL_FALSE &&
         wide_integer == CTOOL_FALSE &&
         floating_unary == CTOOL_FALSE &&
         cemit_ir_type_is_i32_integer(context,
                                      ir_instruction->input_type) ==
             CTOOL_FALSE) ||
        (logical_not == CTOOL_TRUE &&
         cemit_ir_type_is_truth_scalar(
             context, ir_instruction->input_type) ==
             CTOOL_FALSE) ||
        (wide_integer == CTOOL_FALSE && floating_unary == CTOOL_FALSE &&
         cemit_ir_type_is_i32_integer(context, ir_instruction->type) ==
             CTOOL_FALSE) ||
        (logical_not == CTOOL_FALSE &&
         ir_instruction->input_type != ir_instruction->type) ||
        (logical_not == CTOOL_TRUE &&
         cemit_ir_type_is_plain_signed_int(context,
                                           ir_instruction->type) ==
             CTOOL_FALSE) ||
        (ir_instruction->operation !=
             CTOOL_C_EXPRESSION_OPERATOR_UNARY_PLUS &&
         ir_instruction->operation !=
             CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE &&
         ir_instruction->operation !=
             CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT &&
         logical_not == CTOOL_FALSE) ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    if (ir_instruction->operation ==
        CTOOL_C_EXPRESSION_OPERATOR_UNARY_PLUS) {
      return CTOOL_OK;
    }
    if (floating_unary == CTOOL_TRUE) {
      status = cemit_x86_load_floating_stack_value(
          context, ir_instruction->input_type, 0u);
      if (status == CTOOL_OK) {
        status = cemit_x86_discard_arguments(context, 4u);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_no_operand(context, CTOOL_X86_MN_FCHS);
      }
      return status == CTOOL_OK
                 ? cemit_x86_push_floating_result(
                       context, ir_instruction->type,
                       value_temporary_offset)
                 : status;
    }
    if (logical_not == CTOOL_TRUE &&
        cemit_ir_type_is_wide_integer(
            context, ir_instruction->input_type) == CTOOL_TRUE) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
      if (status == CTOOL_OK) {
        status = cemit_x86_load_register_at_register(context, 0u, 1u, 0u);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_load_register_at_register(context, 2u, 1u, 4u);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_two_registers(
            context, CTOOL_X86_MN_OR, CTOOL_X86_REG_GPR32, 0u,
            CTOOL_X86_REG_GPR32, 2u, 32u);
      }
    } else if (wide_integer == CTOOL_TRUE) {
      return cemit_x86_push_wide_unary_snapshot(
          context, value_temporary_offset, ir_instruction->operation);
    } else {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK &&
        ir_instruction->operation ==
            CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_NEG, CTOOL_X86_REG_GPR32, 0u, 32u);
    } else if (status == CTOOL_OK &&
               ir_instruction->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_NOT, CTOOL_X86_REG_GPR32, 0u, 32u);
    } else if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_TEST, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 0u, 32u);
      if (status == CTOOL_OK) {
        status = cemit_x86_one_register(
            context, CTOOL_X86_MN_SETE, CTOOL_X86_REG_GPR8, 0u, 8u);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_two_registers(
            context, CTOOL_X86_MN_MOVZX, CTOOL_X86_REG_GPR32, 0u,
            CTOOL_X86_REG_GPR8, 0u, 32u);
      }
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_POINTER_BINARY) {
    ctool_u32 left_type = ir_instruction->input_type;
    ctool_u32 right_type = ir_instruction->reference;
    ctool_bool left_is_pointer =
        cemit_ir_type_is_i32_pointer(context, left_type);
    ctool_bool right_is_pointer =
        cemit_ir_type_is_i32_pointer(context, right_type);
    ctool_u32 referent_size = 0u;
    ctool_u8 scale_register = 1u;
    if (ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->integer_bits != 0u ||
        (ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_ADD &&
         ir_instruction->operation !=
             CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT)) {
      return CTOOL_ERR_INTERNAL;
    }
    if (ir_instruction->operation == CTOOL_C_EXPRESSION_OPERATOR_ADD) {
      ctool_u32 pointer_type =
          left_is_pointer == CTOOL_TRUE ? left_type : right_type;
      ctool_u32 integer_type =
          left_is_pointer == CTOOL_TRUE ? right_type : left_type;
      scale_register = left_is_pointer == CTOOL_TRUE ? 1u : 0u;
      if (left_is_pointer == right_is_pointer ||
          cemit_ir_type_is_i32_integer(context, integer_type) == CTOOL_FALSE ||
          cemit_ir_pointer_types_match(context, pointer_type,
                                       ir_instruction->type) == CTOOL_FALSE ||
          cemit_ir_pointer_arithmetic_size(context, pointer_type,
                                           &referent_size) == CTOOL_FALSE) {
        return CTOOL_ERR_INTERNAL;
      }
    } else if (left_is_pointer == CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    } else if (right_is_pointer == CTOOL_TRUE) {
      ctool_u32 right_size = 0u;
      if (cemit_ir_type_is_plain_signed_int(context,
                                            ir_instruction->type) ==
              CTOOL_FALSE ||
          cemit_ir_pointer_arithmetic_types_match(
              context, left_type, right_type) == CTOOL_FALSE ||
          cemit_ir_pointer_arithmetic_size(context, left_type,
                                           &referent_size) == CTOOL_FALSE ||
          cemit_ir_pointer_arithmetic_size(context, right_type,
                                           &right_size) == CTOOL_FALSE ||
          referent_size != right_size) {
        return CTOOL_ERR_INTERNAL;
      }
    } else if (cemit_ir_type_is_i32_integer(context, right_type) ==
                   CTOOL_FALSE ||
               cemit_ir_pointer_types_match(
                   context, left_type, ir_instruction->type) == CTOOL_FALSE ||
               cemit_ir_pointer_arithmetic_size(context, left_type,
                                                &referent_size) ==
                   CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK && left_is_pointer != right_is_pointer) {
      status = cemit_x86_scale_register(context, scale_register,
                                        referent_size);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context,
          ir_instruction->operation == CTOOL_C_EXPRESSION_OPERATOR_ADD
              ? CTOOL_X86_MN_ADD
              : CTOOL_X86_MN_SUB,
          CTOOL_X86_REG_GPR32, 0u, CTOOL_X86_REG_GPR32, 1u, 32u);
    }
    if (status == CTOOL_OK && left_is_pointer == CTOOL_TRUE &&
        right_is_pointer == CTOOL_TRUE && referent_size != 1u) {
      status = cemit_x86_move_register_constant(context, 1u,
                                                referent_size);
      if (status == CTOOL_OK) {
        status = cemit_x86_no_operand(context, CTOOL_X86_MN_CDQ);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_one_register(
            context, CTOOL_X86_MN_IDIV, CTOOL_X86_REG_GPR32, 1u, 32u);
      }
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_BINARY) {
    ctool_bool wide_integer =
        cemit_ir_type_is_wide_integer(
            context, ir_instruction->input_type) == CTOOL_TRUE &&
                cemit_ir_type_is_wide_integer(
                    context, ir_instruction->type) == CTOOL_TRUE &&
                ir_instruction->input_type == ir_instruction->type
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool relational_comparison =
        ir_instruction->operation == CTOOL_C_EXPRESSION_OPERATOR_LESS ||
                ir_instruction->operation ==
                    CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL ||
                ir_instruction->operation ==
                    CTOOL_C_EXPRESSION_OPERATOR_GREATER ||
                ir_instruction->operation ==
                    CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool comparison =
        relational_comparison == CTOOL_TRUE ||
                ir_instruction->operation ==
                    CTOOL_C_EXPRESSION_OPERATOR_EQUAL ||
                ir_instruction->operation ==
                    CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool wide_comparison =
        cemit_ir_type_is_wide_integer(
            context, ir_instruction->input_type) == CTOOL_TRUE &&
                cemit_ir_type_is_plain_signed_int(
                    context, ir_instruction->type) == CTOOL_TRUE &&
                comparison == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool pointer_comparison =
        cemit_ir_type_is_i32_pointer_value(
            context, ir_instruction->input_type) ==
                CTOOL_TRUE &&
                cemit_ir_type_is_plain_signed_int(context,
                                                  ir_instruction->type) ==
                    CTOOL_TRUE &&
                (ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_LESS ||
                 ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL ||
                 ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_GREATER ||
                 ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL ||
                 ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_EQUAL ||
                 ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL)
                && cemit_ir_pointer_comparison_types_match(
                       context, ir_instruction->input_type,
                       ir_instruction->input_type,
                       relational_comparison) == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool floating_binary =
        ir_instruction->input_type == ir_instruction->type &&
                cemit_ir_type_is_floating_value(
                    context, ir_instruction->input_type) == CTOOL_TRUE &&
                (ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_ADD ||
                 ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
                 ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY ||
                 ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_DIVIDE)
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool floating_comparison =
        cemit_ir_type_is_floating_value(
            context, ir_instruction->input_type) == CTOOL_TRUE &&
                cemit_ir_type_is_plain_signed_int(
                    context, ir_instruction->type) == CTOOL_TRUE &&
                comparison == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_u8 result_register = 0u;
    if (floating_comparison == CTOOL_TRUE) {
      if (ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          ir_instruction->reference != CTOOL_C_AST_NONE ||
          ir_instruction->integer_bits != 0u) {
        return CTOOL_ERR_INTERNAL;
      }
      return cemit_x86_push_floating_comparison(
          context, ir_instruction->input_type,
          ir_instruction->operation);
    }
    if (floating_binary == CTOOL_TRUE) {
      ctool_x86_mnemonic_t mnemonic = CTOOL_X86_MN_FADDP;
      if (ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          ir_instruction->reference != CTOOL_C_AST_NONE ||
          ir_instruction->integer_bits != 0u) {
        return CTOOL_ERR_INTERNAL;
      }
      if (ir_instruction->operation ==
          CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT) {
        mnemonic = CTOOL_X86_MN_FSUBP;
      } else if (ir_instruction->operation ==
                 CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY) {
        mnemonic = CTOOL_X86_MN_FMULP;
      } else if (ir_instruction->operation ==
                 CTOOL_C_EXPRESSION_OPERATOR_DIVIDE) {
        mnemonic = CTOOL_X86_MN_FDIVP;
      }
      status = cemit_x86_load_floating_stack_value(
          context, ir_instruction->input_type, 4u);
      if (status == CTOOL_OK) {
        status = cemit_x86_load_floating_stack_value(
            context, ir_instruction->input_type, 0u);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_discard_arguments(context, 8u);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_one_register(
            context, mnemonic, CTOOL_X86_REG_X87, 1u, 32u);
      }
      return status == CTOOL_OK
                 ? cemit_x86_push_floating_result(
                       context, ir_instruction->type,
                       value_temporary_offset)
                 : status;
    }
    if (wide_comparison == CTOOL_TRUE) {
      if (ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          ir_instruction->reference != CTOOL_C_AST_NONE ||
          ir_instruction->integer_bits != 0u) {
        return CTOOL_ERR_INTERNAL;
      }
      return cemit_x86_push_wide_comparison(
          context, ir_instruction->operation,
          context->unit->layout.types[ir_instruction->input_type].is_signed);
    }
    if (wide_integer == CTOOL_TRUE) {
      ctool_x86_mnemonic_t mnemonic = CTOOL_X86_MN_AND;
      ctool_bool bitwise = CTOOL_FALSE;
      if (ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
          ir_instruction->reference != CTOOL_C_AST_NONE ||
          ir_instruction->integer_bits != 0u) {
        return CTOOL_ERR_INTERNAL;
      }
      if (ir_instruction->operation ==
              CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT ||
          ir_instruction->operation ==
              CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT) {
        return cemit_x86_push_wide_shift_snapshot(
            context, value_temporary_offset,
            ir_instruction->operation ==
                    CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT
                ? CTOOL_TRUE
                : CTOOL_FALSE,
            context->unit->layout.types[ir_instruction->input_type]
                .is_signed);
      }
      if (ir_instruction->operation ==
              CTOOL_C_EXPRESSION_OPERATOR_ADD ||
          ir_instruction->operation ==
              CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT) {
        return cemit_x86_push_wide_add_subtract_snapshot(
            context, value_temporary_offset,
            ir_instruction->operation ==
                    CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT
                ? CTOOL_TRUE
                : CTOOL_FALSE);
      }
      if (ir_instruction->operation ==
          CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY) {
        return cemit_x86_push_wide_multiply_snapshot(
            context, value_temporary_offset);
      }
      if (ir_instruction->operation ==
              CTOOL_C_EXPRESSION_OPERATOR_DIVIDE ||
          ir_instruction->operation ==
              CTOOL_C_EXPRESSION_OPERATOR_REMAINDER) {
        return cemit_x86_push_wide_divide_remainder_snapshot(
            context, value_temporary_offset,
            context->unit->layout.types[ir_instruction->input_type]
                .is_signed,
            ir_instruction->operation ==
                    CTOOL_C_EXPRESSION_OPERATOR_REMAINDER
                ? CTOOL_TRUE
                : CTOOL_FALSE);
      }
      if (ir_instruction->operation ==
          CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND) {
        mnemonic = CTOOL_X86_MN_AND;
        bitwise = CTOOL_TRUE;
      } else if (ir_instruction->operation ==
                 CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR) {
        mnemonic = CTOOL_X86_MN_OR;
        bitwise = CTOOL_TRUE;
      } else if (ir_instruction->operation ==
                 CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR) {
        mnemonic = CTOOL_X86_MN_XOR;
        bitwise = CTOOL_TRUE;
      }
      if (bitwise == CTOOL_FALSE) {
        return CTOOL_ERR_INTERNAL;
      }
      return cemit_x86_push_wide_bitwise_snapshot(
          context, value_temporary_offset, mnemonic);
    }
    if ((pointer_comparison == CTOOL_FALSE &&
         cemit_ir_type_is_i32_integer(context,
                                      ir_instruction->input_type) ==
             CTOOL_FALSE) ||
        cemit_ir_type_is_i32_integer(context, ir_instruction->type) ==
            CTOOL_FALSE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (ir_instruction->operation ==
        CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_IMUL, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 1u, 32u);
    } else if (ir_instruction->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_DIVIDE ||
               ir_instruction->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_REMAINDER) {
      if (context->unit->layout.types[ir_instruction->input_type].is_signed ==
          CTOOL_TRUE) {
        status = cemit_x86_no_operand(context, CTOOL_X86_MN_CDQ);
        if (status == CTOOL_OK) {
          status = cemit_x86_one_register(
              context, CTOOL_X86_MN_IDIV, CTOOL_X86_REG_GPR32, 1u, 32u);
        }
      } else {
        status = cemit_x86_two_registers(
            context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 2u,
            CTOOL_X86_REG_GPR32, 2u, 32u);
        if (status == CTOOL_OK) {
          status = cemit_x86_one_register(
              context, CTOOL_X86_MN_DIV, CTOOL_X86_REG_GPR32, 1u, 32u);
        }
      }
      if (ir_instruction->operation ==
          CTOOL_C_EXPRESSION_OPERATOR_REMAINDER) {
        result_register = 2u;
      }
    } else if (ir_instruction->operation ==
               CTOOL_C_EXPRESSION_OPERATOR_ADD) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_ADD, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 1u, 32u);
    } else if (ir_instruction->operation ==
               CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_SUB, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 1u, 32u);
    } else if (ir_instruction->operation ==
               CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_AND, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 1u, 32u);
    } else if (ir_instruction->operation ==
               CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_OR, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 1u, 32u);
    } else if (ir_instruction->operation ==
               CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 1u, 32u);
    } else if (ir_instruction->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT ||
               ir_instruction->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT) {
      ctool_x86_mnemonic_t mnemonic = CTOOL_X86_MN_SHL;
      if (ir_instruction->operation ==
          CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT) {
        mnemonic = context->unit->layout
                           .types[ir_instruction->input_type]
                           .is_signed == CTOOL_TRUE
                       ? CTOOL_X86_MN_SAR
                       : CTOOL_X86_MN_SHR;
      }
      status = cemit_x86_two_registers(
          context, mnemonic, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR8, 1u, 32u);
    } else if (ir_instruction->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_LESS ||
               ir_instruction->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL ||
               ir_instruction->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_GREATER ||
               ir_instruction->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL ||
               ir_instruction->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_EQUAL ||
               ir_instruction->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL) {
      ctool_x86_mnemonic_t predicate = cemit_comparison_predicate(
          ir_instruction->operation,
          pointer_comparison == CTOOL_TRUE
              ? CTOOL_FALSE
              : context->unit->layout.types[ir_instruction->input_type]
                    .is_signed);
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_CMP, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 1u, 32u);
      if (status == CTOOL_OK) {
        status = cemit_x86_one_register(
            context, predicate, CTOOL_X86_REG_GPR8, 0u, 8u);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_two_registers(
            context, CTOOL_X86_MN_MOVZX, CTOOL_X86_REG_GPR32, 0u,
            CTOOL_X86_REG_GPR8, 0u, 32u);
      }
    } else {
      return CTOOL_ERR_INTERNAL;
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32,
          result_register, 32u);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT) {
    return cemit_emit_direct_call(
        context, ir_instruction, value_temporary_offset,
        stack_base_residue, frame_size, stack_depth);
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT) {
    return cemit_emit_indirect_call(
        context, ir_instruction, value_temporary_offset,
        stack_base_residue, frame_size, stack_depth);
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO) {
    if (ir_instruction->type != CTOOL_C_TYPE_NONE ||
        cemit_ir_type_is_truth_scalar(
            context, ir_instruction->input_type) ==
            CTOOL_FALSE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    if (cemit_ir_type_is_wide_integer(
            context, ir_instruction->input_type) == CTOOL_TRUE) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 1u, 32u);
      if (status == CTOOL_OK) {
        status = cemit_x86_load_register_at_register(context, 0u, 1u, 0u);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_load_register_at_register(context, 2u, 1u, 4u);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_two_registers(
            context, CTOOL_X86_MN_OR, CTOOL_X86_REG_GPR32, 0u,
            CTOOL_X86_REG_GPR32, 2u, 32u);
      }
    } else {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_TEST, CTOOL_X86_REG_GPR32, 0u,
          CTOOL_X86_REG_GPR32, 0u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_branch(context, CTOOL_X86_MN_JE,
                                &branch_patches[ir_offset],
                                &branch_afters[ir_offset]);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_JUMP) {
    return cemit_x86_branch(context, CTOOL_X86_MN_JMP,
                            &branch_patches[ir_offset],
                            &branch_afters[ir_offset]);
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    ctool_bool scalar = cemit_ir_scalar_types_match(
        context, ir_instruction->type, ir_instruction->input_type);
    ctool_bool structure = cemit_ir_structure_types_match(
        context, ir_instruction->type, ir_instruction->input_type);
    ctool_bool wide =
        cemit_ir_type_is_wide_integer(context, ir_instruction->type) ==
                CTOOL_TRUE &&
                cemit_ir_type_is_wide_integer(
                    context, ir_instruction->input_type) == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if (function_type == (const ctool_c_type_node_t *)0 ||
        function_type->kind != CTOOL_C_TYPE_FUNCTION ||
        function_type->referenced_type != ir_instruction->type ||
        (scalar == CTOOL_FALSE && structure == CTOOL_FALSE) ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    if (cemit_ir_type_is_floating_value(
            context, ir_instruction->type) == CTOOL_TRUE) {
      const ctool_c_type_layout_t *layout =
          &context->unit->layout.types[ir_instruction->type];
      if (layout->size == 4u) {
        status = cemit_x86_x87_memory(
            context, CTOOL_X86_MN_FLD, 4u, 0, 32u);
        if (status == CTOOL_OK) {
          status = cemit_x86_discard_arguments(context, 4u);
        }
      } else {
        status = cemit_x86_one_register(
            context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
        if (status == CTOOL_OK) {
          status = cemit_x86_x87_memory(
              context, CTOOL_X86_MN_FLD, 0u, 0, 64u);
        }
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_no_operand(context, CTOOL_X86_MN_LEAVE);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_no_operand(context, CTOOL_X86_MN_RET);
      }
      return status;
    }
    if (wide == CTOOL_TRUE) {
      status = cemit_x86_pop_wide_result(context);
      if (status == CTOOL_OK) {
        status = cemit_x86_no_operand(context, CTOOL_X86_MN_LEAVE);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_no_operand(context, CTOOL_X86_MN_RET);
      }
      return status;
    }
    if (structure == CTOOL_TRUE) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 2u, 32u);
      if (status == CTOOL_OK) {
        status = cemit_x86_load_frame(context, 0u, 8u);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_copy_edx_to_eax(
            context,
            context->unit->layout.types[ir_instruction->type].size);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_no_operand(context, CTOOL_X86_MN_LEAVE);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_return_and_pop(context, 4u);
      }
      return status;
    }
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_POP, CTOOL_X86_REG_GPR32, 0u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_canonicalize_scalar_eax(
          context, ir_instruction->type);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_no_operand(context, CTOOL_X86_MN_LEAVE);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_no_operand(context, CTOOL_X86_MN_RET);
    }
    return status;
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    if (ir_instruction->type != CTOOL_C_TYPE_NONE ||
        ir_instruction->input_type != CTOOL_C_TYPE_NONE ||
        ir_instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        ir_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
        ir_instruction->reference != CTOOL_C_AST_NONE ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    if (cemit_ir_function_returns_structure(context, function_type) ==
        CTOOL_TRUE) {
      status = cemit_x86_load_frame(context, 0u, 8u);
      if (status == CTOOL_OK) {
        status = cemit_x86_no_operand(context, CTOOL_X86_MN_LEAVE);
      }
      if (status == CTOOL_OK) {
        status = cemit_x86_return_and_pop(context, 4u);
      }
      return status;
    }
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_LEAVE);
    if (status == CTOOL_OK) {
      status = cemit_x86_no_operand(context, CTOOL_X86_MN_RET);
    }
    return status;
  }
  return CTOOL_ERR_INTERNAL;
}

static ctool_status_t cemit_patch_branch(ctool_buffer_t *text,
                                         ctool_u32 patch,
                                         ctool_u32 after,
                                         ctool_u32 target) {
  ctool_u32 displacement;
  if (target >= after) {
    if (target - after > 0x7fffffffu) {
      return CTOOL_ERR_OVERFLOW;
    }
    displacement = target - after;
  } else {
    ctool_u32 magnitude = after - target;
    if (magnitude > 0x80000000u) {
      return CTOOL_ERR_OVERFLOW;
    }
    displacement = 0u - magnitude;
  }
  return ctool_buffer_patch_le32(text, patch, displacement);
}

static ctool_status_t cemit_patch_short_branch(
    ctool_buffer_t *text, ctool_u32 patch, ctool_u32 after,
    ctool_u32 target) {
  ctool_u32 displacement;
  if (target >= after) {
    if (target - after > 0x7fu) {
      return CTOOL_ERR_OVERFLOW;
    }
    displacement = target - after;
  } else {
    ctool_u32 magnitude = after - target;
    if (magnitude > 0x80u) {
      return CTOOL_ERR_OVERFLOW;
    }
    displacement = 0u - magnitude;
  }
  return ctool_buffer_patch_u8(text, patch, (ctool_u8)displacement);
}

static ctool_status_t cemit_ir_stack_effect(
    const cemit_context_t *context,
    const ctool_c_ir_instruction_t *instruction,
    ctool_u32 *consumed_out, ctool_u32 *produced_out) {
  ctool_u32 consumed = 0u;
  ctool_u32 produced = 0u;
  if (context == (const cemit_context_t *)0 ||
      instruction == (const ctool_c_ir_instruction_t *)0 ||
      consumed_out == (ctool_u32 *)0 ||
      produced_out == (ctool_u32 *)0) {
    return CTOOL_ERR_INTERNAL;
  }
  switch (instruction->kind) {
    case CTOOL_C_IR_INSTRUCTION_INTEGER:
    case CTOOL_C_IR_INSTRUCTION_FLOATING:
    case CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS:
    case CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS:
    case CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS:
    case CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS:
    case CTOOL_C_IR_INSTRUCTION_STRING_LITERAL_ADDRESS:
    case CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS:
    case CTOOL_C_IR_INSTRUCTION_FUNCTION_ADDRESS:
      produced = 1u;
      break;
    case CTOOL_C_IR_INSTRUCTION_LOAD:
    case CTOOL_C_IR_INSTRUCTION_CONVERT:
    case CTOOL_C_IR_INSTRUCTION_UNARY:
    case CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS:
    case CTOOL_C_IR_INSTRUCTION_BIT_FIELD_LOAD:
    case CTOOL_C_IR_INSTRUCTION_DEREFERENCE:
    case CTOOL_C_IR_INSTRUCTION_ADDRESS_OF:
    case CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER:
    case CTOOL_C_IR_INSTRUCTION_FUNCTION_TO_POINTER:
    case CTOOL_C_IR_INSTRUCTION_ELEMENT_ADDRESS:
    case CTOOL_C_IR_INSTRUCTION_VARIADIC_ARGUMENT:
    case CTOOL_C_IR_INSTRUCTION_ATOMIC_LOAD:
      consumed = 1u;
      produced = 1u;
      break;
    case CTOOL_C_IR_INSTRUCTION_BINARY:
    case CTOOL_C_IR_INSTRUCTION_POINTER_BINARY:
      consumed = 2u;
      produced = 1u;
      break;
    case CTOOL_C_IR_INSTRUCTION_CALL_DIRECT:
    case CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT: {
      const ctool_c_type_node_t *call_type;
      if (instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT) {
        call_type = cemit_unwrapped_type(context, instruction->input_type);
      } else {
        const ctool_c_type_node_t *pointer =
            cemit_unwrapped_type(context, instruction->input_type);
        call_type = pointer != (const ctool_c_type_node_t *)0 &&
                            pointer->kind == CTOOL_C_TYPE_POINTER
                        ? cemit_unwrapped_type(
                              context, pointer->referenced_type)
                        : (const ctool_c_type_node_t *)0;
        consumed = 1u;
      }
      if (call_type == (const ctool_c_type_node_t *)0 ||
          call_type->kind != CTOOL_C_TYPE_FUNCTION ||
          cemit_call_argument_count_is_valid(call_type, instruction) ==
              CTOOL_FALSE ||
          cemit_add_overflows(consumed, instruction->argument_count) ==
              CTOOL_TRUE) {
        return CTOOL_ERR_INTERNAL;
      }
      consumed += instruction->argument_count;
      produced = cemit_ir_type_is_void(context, instruction->type) ==
                         CTOOL_TRUE
                     ? 0u
                     : 1u;
      break;
    }
    case CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO:
    case CTOOL_C_IR_INSTRUCTION_RETURN_VALUE:
    case CTOOL_C_IR_INSTRUCTION_DISCARD:
    case CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT:
    case CTOOL_C_IR_INSTRUCTION_COPY_STRING:
    case CTOOL_C_IR_INSTRUCTION_VARIADIC_START:
    case CTOOL_C_IR_INSTRUCTION_VARIADIC_END:
      consumed = 1u;
      break;
    case CTOOL_C_IR_INSTRUCTION_JUMP:
    case CTOOL_C_IR_INSTRUCTION_RETURN_VOID:
      break;
    case CTOOL_C_IR_INSTRUCTION_STORE:
    case CTOOL_C_IR_INSTRUCTION_COPY_OBJECT:
    case CTOOL_C_IR_INSTRUCTION_ATOMIC_STORE:
      consumed = 2u;
      break;
    case CTOOL_C_IR_INSTRUCTION_STORE_VALUE:
    case CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_VALUE:
      consumed = 2u;
      produced = 1u;
      break;
    case CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_OLD_VALUE:
      consumed = 3u;
      produced = 1u;
      break;
    case CTOOL_C_IR_INSTRUCTION_ATOMIC_EXCHANGE:
    case CTOOL_C_IR_INSTRUCTION_ATOMIC_FETCH_ADD:
    case CTOOL_C_IR_INSTRUCTION_ATOMIC_FETCH_OR:
      consumed = 2u;
      produced = 1u;
      break;
    case CTOOL_C_IR_INSTRUCTION_ASSEMBLY:
      if (instruction->reference >= context->unit->assembly_count ||
          context->unit->assemblies ==
              (const ctool_c_assembly_t *)0) {
        return CTOOL_ERR_INTERNAL;
      }
      if (cemit_add_overflows(
              context->unit->assemblies[instruction->reference].output_count,
              context->unit->assemblies[instruction->reference].input_count) ==
          CTOOL_TRUE) {
        return CTOOL_ERR_OVERFLOW;
      }
      consumed =
          context->unit->assemblies[instruction->reference].output_count +
          context->unit->assemblies[instruction->reference].input_count;
      break;
    case CTOOL_C_IR_INSTRUCTION_DUPLICATE_VALUE:
    case CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS:
      consumed = 1u;
      produced = 2u;
      break;
    default:
      return CTOOL_ERR_INTERNAL;
  }
  *consumed_out = consumed;
  *produced_out = produced;
  return CTOOL_OK;
}

static ctool_status_t cemit_record_stack_depth(
    ctool_u32 instruction_count, ctool_u32 target,
    ctool_u32 depth, ctool_u32 *depths, ctool_u32 *worklist,
    ctool_u32 *worklist_count) {
  if (target >= instruction_count || depths == (ctool_u32 *)0 ||
      worklist == (ctool_u32 *)0 ||
      worklist_count == (ctool_u32 *)0) {
    return CTOOL_ERR_INTERNAL;
  }
  if (depths[target] == CTOOL_C_AST_NONE) {
    if (*worklist_count >= instruction_count) {
      return CTOOL_ERR_INTERNAL;
    }
    depths[target] = depth;
    worklist[(*worklist_count)++] = target;
    return CTOOL_OK;
  }
  return depths[target] == depth ? CTOOL_OK : CTOOL_ERR_INTERNAL;
}

static ctool_status_t cemit_analyze_stack_depths(
    cemit_context_t *context, const ctool_c_ir_function_t *function,
    ctool_u32 **depths_out) {
  ctool_u32 *depths = (ctool_u32 *)0;
  ctool_u32 *worklist = (ctool_u32 *)0;
  ctool_u32 worklist_count = 0u;
  ctool_u32 worklist_cursor = 0u;
  ctool_u32 index;
  ctool_status_t status;
  if (depths_out == (ctool_u32 **)0 ||
      function == (const ctool_c_ir_function_t *)0 ||
      function->instruction_count == 0u ||
      function->first_instruction > context->ir.instruction_count ||
      function->instruction_count >
          context->ir.instruction_count - function->first_instruction) {
    return CTOOL_ERR_INTERNAL;
  }
  *depths_out = (ctool_u32 *)0;
  status = cemit_alloc_array(
      context, function->instruction_count,
      (ctool_u32)sizeof(ctool_u32), (void **)&depths);
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        context, function->instruction_count,
        (ctool_u32)sizeof(ctool_u32), (void **)&worklist);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    depths[index] = CTOOL_C_AST_NONE;
  }
  status = cemit_record_stack_depth(
      function->instruction_count, 0u, 0u, depths, worklist,
      &worklist_count);
  while (status == CTOOL_OK && worklist_cursor < worklist_count) {
    ctool_u32 relative = worklist[worklist_cursor++];
    const ctool_c_ir_instruction_t *instruction =
        &context->ir.instructions[function->first_instruction + relative];
    ctool_u32 consumed;
    ctool_u32 produced;
    ctool_u32 next_depth;
    status = cemit_ir_stack_effect(
        context, instruction, &consumed, &produced);
    if (status != CTOOL_OK || depths[relative] < consumed ||
        cemit_add_overflows(depths[relative] - consumed, produced) ==
            CTOOL_TRUE) {
      return CTOOL_ERR_INTERNAL;
    }
    next_depth = depths[relative] - consumed + produced;
    if (next_depth > function->maximum_stack_depth) {
      return CTOOL_ERR_INTERNAL;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
        instruction->kind == CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
      continue;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_JUMP ||
        instruction->kind == CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO) {
      status = cemit_record_stack_depth(
          function->instruction_count, instruction->reference,
          next_depth, depths, worklist, &worklist_count);
      if (status != CTOOL_OK ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_JUMP) {
        continue;
      }
    }
    if (relative + 1u >= function->instruction_count) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_record_stack_depth(
        function->instruction_count, relative + 1u, next_depth,
        depths, worklist, &worklist_count);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction =
        &context->ir.instructions[function->first_instruction + index];
    if ((instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
         instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT) &&
        depths[index] == CTOOL_C_AST_NONE) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  *depths_out = depths;
  return CTOOL_OK;
}

static ctool_status_t cemit_prepare_local_offsets(
    cemit_context_t *context, const ctool_c_ir_function_t *function,
    ctool_u32 *frame_size_out) {
  ctool_u32 frame_size = 0u;
  ctool_u32 index;
  *frame_size_out = 0u;
  if (context->unit->block_binding_count != 0u &&
      context->block_binding_offsets == (ctool_u32 *)0) {
    return CTOOL_ERR_INTERNAL;
  }
  if (context->unit->expression_count != 0u &&
      (context->compound_literal_offsets == (ctool_u32 *)0 ||
       context->compound_literal_staging_offsets == (ctool_u32 *)0)) {
    return CTOOL_ERR_INTERNAL;
  }
  if (function->instruction_count != 0u &&
      context->value_temporary_offsets == (ctool_u32 *)0) {
    return CTOOL_ERR_INTERNAL;
  }
  for (index = 0u; index < context->unit->block_binding_count; index++) {
    context->block_binding_offsets[index] = CTOOL_C_AST_NONE;
  }
  for (index = 0u; index < context->unit->expression_count; index++) {
    context->compound_literal_offsets[index] = CTOOL_C_AST_NONE;
    context->compound_literal_staging_offsets[index] = CTOOL_C_AST_NONE;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    ctool_u32 absolute = function->first_instruction + index;
    const ctool_c_ir_instruction_t *instruction =
        &context->ir.instructions[absolute];
    context->value_temporary_offsets[absolute] = CTOOL_C_AST_NONE;
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_ASSEMBLY) {
      if (instruction->reference >= context->unit->assembly_count ||
          context->unit->assemblies ==
              (const ctool_c_assembly_t *)0) {
        return CTOOL_ERR_INTERNAL;
      }
      context->value_temporary_offsets[absolute] = 0u;
    }
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS) {
      const ctool_c_block_binding_t *binding;
      if (instruction->reference >= context->unit->block_binding_count ||
          instruction->type >= context->unit->layout.type_count) {
        return CTOOL_ERR_INTERNAL;
      }
      binding = &context->unit->block_bindings[instruction->reference];
      if (binding->kind != CTOOL_C_BINDING_OBJECT ||
          binding->type != instruction->type) {
        return CTOOL_ERR_INTERNAL;
      }
      if (binding->storage == CTOOL_C_STORAGE_STATIC) {
        ctool_u32 symbol;
        if (context->block_binding_symbols == (ctool_u32 *)0 ||
            (cemit_ir_type_is_value_scalar(
                 context, instruction->type) == CTOOL_FALSE &&
             cemit_ir_type_is_complete_aggregate_object(
                 context, instruction->type) == CTOOL_FALSE)) {
          return CTOOL_ERR_INTERNAL;
        }
        symbol = context->block_binding_symbols[instruction->reference];
        if (symbol == CTOOL_C_AST_NONE || symbol >= context->symbol_count) {
          return CTOOL_ERR_INTERNAL;
        }
        continue;
      }
      if ((binding->storage != CTOOL_C_STORAGE_NONE &&
           binding->storage != CTOOL_C_STORAGE_AUTO &&
           binding->storage != CTOOL_C_STORAGE_REGISTER) ||
          cemit_ir_type_is_automatic_object(context, instruction->type) ==
              CTOOL_FALSE) {
        return CTOOL_ERR_INTERNAL;
      }
      context->block_binding_offsets[instruction->reference] = 0u;
    }
    if (instruction->kind ==
            CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS ||
        instruction->kind ==
            CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS) {
      const ctool_c_expression_t *expression;
      ctool_u32 *offsets =
          instruction->kind ==
                  CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS
              ? context->compound_literal_offsets
              : context->compound_literal_staging_offsets;
      if (instruction->reference >= context->unit->expression_count ||
          instruction->type >= context->unit->layout.type_count) {
        return CTOOL_ERR_INTERNAL;
      }
      expression = &context->unit->expressions[instruction->reference];
      if (expression->kind != CTOOL_C_EXPRESSION_COMPOUND_LITERAL ||
          expression->type != instruction->type ||
          expression->reference >= context->unit->initializer_count ||
          context->unit->initializers[expression->reference].type !=
              instruction->type ||
          cemit_ir_type_is_automatic_object(context, instruction->type) ==
              CTOOL_FALSE ||
          (instruction->kind ==
               CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS &&
           (context->unit->initializers[expression->reference].kind !=
                CTOOL_C_INITIALIZER_LIST ||
            cemit_ir_type_is_initializable_aggregate_object(
                context, instruction->type) == CTOOL_FALSE))) {
        return CTOOL_ERR_INTERNAL;
      }
      offsets[instruction->reference] = 0u;
    }
    if (((instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT) &&
         cemit_ir_type_is_structure_value(context, instruction->type) ==
             CTOOL_TRUE) ||
        ((instruction->kind == CTOOL_C_IR_INSTRUCTION_INTEGER ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_BINARY ||
          instruction->kind ==
              CTOOL_C_IR_INSTRUCTION_VARIADIC_ARGUMENT ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_UNARY &&
           (instruction->operation ==
                CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE ||
            instruction->operation ==
                CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT)) ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_CONVERT &&
           cemit_ir_type_is_wide_integer(
               context, instruction->input_type) == CTOOL_FALSE)) &&
         cemit_ir_type_is_wide_integer(context, instruction->type) ==
             CTOOL_TRUE) ||
        ((instruction->kind == CTOOL_C_IR_INSTRUCTION_FLOATING ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD ||
          instruction->kind ==
              CTOOL_C_IR_INSTRUCTION_VARIADIC_ARGUMENT ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_BINARY ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_UNARY &&
           instruction->operation ==
               CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE) ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_CONVERT &&
           cemit_ir_floating_conversion_is_valid(
               context, instruction->input_type, instruction->type,
               instruction->conversion) == CTOOL_TRUE)) &&
         cemit_ir_type_is_floating_value(context, instruction->type) ==
             CTOOL_TRUE &&
         context->unit->layout.types[instruction->type].size == 8u)) {
      context->value_temporary_offsets[absolute] = 0u;
    }
  }
  for (index = 0u; index < context->unit->expression_count; index++) {
    const ctool_c_expression_t *expression =
        &context->unit->expressions[index];
    ctool_bool object_used =
        context->compound_literal_offsets[index] != CTOOL_C_AST_NONE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool staging_used =
        context->compound_literal_staging_offsets[index] != CTOOL_C_AST_NONE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if (staging_used == CTOOL_TRUE &&
        (object_used == CTOOL_FALSE ||
         expression->kind != CTOOL_C_EXPRESSION_COMPOUND_LITERAL ||
         expression->reference >= context->unit->initializer_count ||
         context->unit->initializers[expression->reference].kind !=
             CTOOL_C_INITIALIZER_LIST)) {
      return CTOOL_ERR_INTERNAL;
    }
    if (object_used == CTOOL_TRUE &&
        expression->kind == CTOOL_C_EXPRESSION_COMPOUND_LITERAL &&
        expression->reference < context->unit->initializer_count &&
        context->unit->initializers[expression->reference].kind ==
            CTOOL_C_INITIALIZER_LIST &&
        staging_used == CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  for (index = 0u; index < context->unit->block_binding_count; index++) {
    if (context->block_binding_offsets[index] == 0u) {
      const ctool_c_block_binding_t *binding =
          &context->unit->block_bindings[index];
      const ctool_c_type_layout_t *layout =
          &context->unit->layout.types[binding->type];
      ctool_u32 alignment_mask = layout->alignment - 1u;
      ctool_u32 offset;
      if (layout->size > 0x7fffffffu ||
          frame_size > 0x7fffffffu - layout->size) {
        return CTOOL_ERR_OVERFLOW;
      }
      offset = frame_size + layout->size;
      if (offset > 0x7fffffffu - alignment_mask) {
        return CTOOL_ERR_OVERFLOW;
      }
      offset = (offset + alignment_mask) & ~alignment_mask;
      if (offset == 0u || offset > 0x7fffffffu) {
        return CTOOL_ERR_OVERFLOW;
      }
      frame_size = offset;
      context->block_binding_offsets[index] = offset;
    }
  }
  for (index = 0u; index < context->unit->expression_count; index++) {
    if (context->compound_literal_offsets[index] == 0u) {
      const ctool_c_expression_t *expression =
          &context->unit->expressions[index];
      const ctool_c_type_layout_t *layout =
          expression->type < context->unit->layout.type_count
              ? &context->unit->layout.types[expression->type]
              : (const ctool_c_type_layout_t *)0;
      ctool_u32 alignment_mask;
      ctool_u32 offset;
      if (expression->kind != CTOOL_C_EXPRESSION_COMPOUND_LITERAL ||
          layout == (const ctool_c_type_layout_t *)0 ||
          cemit_ir_type_is_automatic_object(context, expression->type) ==
              CTOOL_FALSE) {
        return CTOOL_ERR_INTERNAL;
      }
      if (layout->size > 0x7fffffffu ||
          frame_size > 0x7fffffffu - layout->size) {
        return CTOOL_ERR_OVERFLOW;
      }
      alignment_mask = layout->alignment - 1u;
      offset = frame_size + layout->size;
      if (offset > 0x7fffffffu - alignment_mask) {
        return CTOOL_ERR_OVERFLOW;
      }
      offset = (offset + alignment_mask) & ~alignment_mask;
      if (offset == 0u || offset > 0x7fffffffu) {
        return CTOOL_ERR_OVERFLOW;
      }
      frame_size = offset;
      context->compound_literal_offsets[index] = offset;
    }
  }
  for (index = 0u; index < context->unit->expression_count; index++) {
    if (context->compound_literal_staging_offsets[index] == 0u) {
      const ctool_c_expression_t *expression =
          &context->unit->expressions[index];
      const ctool_c_type_layout_t *layout =
          expression->type < context->unit->layout.type_count
              ? &context->unit->layout.types[expression->type]
              : (const ctool_c_type_layout_t *)0;
      ctool_u32 alignment_mask;
      ctool_u32 offset;
      if (expression->kind != CTOOL_C_EXPRESSION_COMPOUND_LITERAL ||
          layout == (const ctool_c_type_layout_t *)0 ||
          expression->reference >= context->unit->initializer_count ||
          context->unit->initializers[expression->reference].kind !=
              CTOOL_C_INITIALIZER_LIST ||
          cemit_ir_type_is_initializable_aggregate_object(
              context, expression->type) == CTOOL_FALSE) {
        return CTOOL_ERR_INTERNAL;
      }
      if (layout->size > 0x7fffffffu ||
          frame_size > 0x7fffffffu - layout->size) {
        return CTOOL_ERR_OVERFLOW;
      }
      alignment_mask = layout->alignment - 1u;
      offset = frame_size + layout->size;
      if (offset > 0x7fffffffu - alignment_mask) {
        return CTOOL_ERR_OVERFLOW;
      }
      offset = (offset + alignment_mask) & ~alignment_mask;
      if (offset == 0u || offset > 0x7fffffffu) {
        return CTOOL_ERR_OVERFLOW;
      }
      frame_size = offset;
      context->compound_literal_staging_offsets[index] = offset;
    }
  }
  for (index = 0u; index < function->instruction_count; index++) {
    ctool_u32 absolute = function->first_instruction + index;
    if (context->value_temporary_offsets[absolute] == 0u) {
      const ctool_c_ir_instruction_t *instruction =
          &context->ir.instructions[absolute];
      if (instruction->kind == CTOOL_C_IR_INSTRUCTION_ASSEMBLY) {
        const ctool_c_assembly_t *assembly;
        ctool_u32 size;
        ctool_u32 offset;
        if (instruction->reference >= context->unit->assembly_count ||
            context->unit->assemblies ==
                (const ctool_c_assembly_t *)0) {
          return CTOOL_ERR_INTERNAL;
        }
        assembly = &context->unit->assemblies[instruction->reference];
        if (assembly->output_count > 4u ||
            assembly->output_count > 0x3fffffffu) {
          return CTOOL_ERR_INTERNAL;
        }
        if (cemit_privileged_assembly_template_kind(
                assembly->template_text) !=
            CEMIT_PRIVILEGED_ASSEMBLY_NONE) {
          continue;
        }
        if (cemit_assembly_uses_fxsave_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_ldmxcsr_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_flags_restore_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_kernel_simd_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_movss_memory_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_sqrtsd_register_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_x87_atan2_memory_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_x87_exp_memory_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_x87_sine_memory_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_x87_round_down_memory_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_x87_pow_memory_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_x87_powf_memory_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_descriptor_table_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_state_memory_path(
                assembly) == CTOOL_TRUE) {
          continue;
        }
        if (cemit_assembly_uses_port_io_path(
                context, assembly) == CTOOL_TRUE) {
          cemit_port_io_kind_t kind =
              cemit_port_io_template_kind(
                  assembly->template_text);
          if (kind != CEMIT_PORT_IO_INSW &&
              kind != CEMIT_PORT_IO_OUTSW) {
            continue;
          }
          size = 12u;
          if (frame_size > 0x7fffffffu - size) {
            return CTOOL_ERR_OVERFLOW;
          }
          offset = frame_size + size;
          if (offset == 0u || offset > 0x7fffffffu) {
            return CTOOL_ERR_OVERFLOW;
          }
          frame_size = offset;
          context->value_temporary_offsets[absolute] = offset;
          continue;
        }
        if (assembly->output_count == 0u) {
          if (assembly->input_count != 0u) {
            return CTOOL_ERR_INTERNAL;
          }
          continue;
        }
        size = (assembly->output_count + 1u) * 4u;
        if (frame_size > 0x7fffffffu - size) {
          return CTOOL_ERR_OVERFLOW;
        }
        offset = frame_size + size;
        if (offset == 0u || offset > 0x7fffffffu) {
          return CTOOL_ERR_OVERFLOW;
        }
        frame_size = offset;
        context->value_temporary_offsets[absolute] = offset;
        continue;
      }
      const ctool_c_type_layout_t *layout =
          instruction->type < context->unit->layout.type_count
              ? &context->unit->layout.types[instruction->type]
              : (const ctool_c_type_layout_t *)0;
      ctool_u32 alignment_mask;
      ctool_u32 offset;
      if (layout == (const ctool_c_type_layout_t *)0 ||
          (cemit_ir_type_is_structure_value(
               context, instruction->type) == CTOOL_FALSE &&
           cemit_ir_type_is_wide_integer(
               context, instruction->type) == CTOOL_FALSE &&
           (cemit_ir_type_is_floating_value(
                context, instruction->type) == CTOOL_FALSE ||
            context->unit->layout.types[instruction->type].size != 8u))) {
        return CTOOL_ERR_INTERNAL;
      }
      if (layout->size > 0x7fffffffu ||
          frame_size > 0x7fffffffu - layout->size) {
        return CTOOL_ERR_OVERFLOW;
      }
      alignment_mask = layout->alignment - 1u;
      offset = frame_size + layout->size;
      if (offset > 0x7fffffffu - alignment_mask) {
        return CTOOL_ERR_OVERFLOW;
      }
      offset = (offset + alignment_mask) & ~alignment_mask;
      if (offset == 0u || offset > 0x7fffffffu) {
        return CTOOL_ERR_OVERFLOW;
      }
      frame_size = offset;
      context->value_temporary_offsets[absolute] = offset;
    }
  }
  if (frame_size > 0x7ffffffcu) {
    return CTOOL_ERR_OVERFLOW;
  }
  frame_size = (frame_size + 3u) & ~3u;
  *frame_size_out = frame_size;
  return CTOOL_OK;
}

static ctool_status_t cemit_validate_general_regs_only_codegen(
    cemit_context_t *context, const ctool_c_ir_function_t *function,
    const ctool_c_pp_location_t *location) {
  ctool_u32 instruction_index;
  if ((function->function_codegen_attributes &
       CTOOL_C_DECL_ATTR_TARGET_GENERAL_REGS_ONLY) == 0u) {
    return CTOOL_OK;
  }
  for (instruction_index = function->first_instruction;
       instruction_index <
           function->first_instruction + function->instruction_count;
       instruction_index++) {
    const ctool_c_ir_instruction_t *instruction =
        &context->ir.instructions[instruction_index];
    ctool_u32 argument;
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_FLOATING ||
        cemit_ir_type_is_floating_value(
            context, instruction->type) == CTOOL_TRUE ||
        cemit_ir_type_is_floating_value(
            context, instruction->input_type) == CTOOL_TRUE) {
      return cemit_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_EMIT_DIAG_UNSUPPORTED, location,
          "general-regs-only function cannot use compiler-generated floating "
          "code");
    }
    if (instruction->kind != CTOOL_C_IR_INSTRUCTION_CALL_DIRECT &&
        instruction->kind != CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT) {
      continue;
    }
    if (instruction->first_argument_type >
            context->ir.argument_type_count ||
        instruction->argument_count >
            context->ir.argument_type_count -
                instruction->first_argument_type) {
      return cemit_invalid_unit(context, location);
    }
    for (argument = 0u; argument < instruction->argument_count;
         argument++) {
      if (cemit_ir_type_is_floating_value(
              context,
              context->ir.argument_types[
                  instruction->first_argument_type + argument]) ==
          CTOOL_TRUE) {
        return cemit_emit_failure(
            context, CTOOL_ERR_UNSUPPORTED,
            CTOOL_C_EMIT_DIAG_UNSUPPORTED, location,
            "general-regs-only function cannot use compiler-generated "
            "floating code");
      }
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_finish_file_assembly_x87_result(
    cemit_context_t *context, ctool_u16 width_bits) {
  ctool_u32 byte_count =
      width_bits == 32u ? 4u : width_bits == 64u ? 8u : 0u;
  ctool_x86_mnemonic_t move =
      width_bits == 32u ? CTOOL_X86_MN_MOVSS : CTOOL_X86_MN_MOVSD;
  ctool_status_t status;
  if (byte_count == 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_reserve_locals(context, byte_count);
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FSTP, 4u, 0, width_bits);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_sse_memory(
        context, move, CTOOL_TRUE, 0u, 4u, 0, width_bits);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_add_register_constant(context, 4u, byte_count);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_RET);
  }
  return status;
}

static ctool_bool cemit_file_assembly_is_rounding(
    cemit_file_assembly_kind_t kind) {
  return kind >= CEMIT_FILE_ASSEMBLY_FLOOR &&
                 kind <= CEMIT_FILE_ASSEMBLY_TRUNCF
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_file_assembly_is_fmod(
    cemit_file_assembly_kind_t kind) {
  return kind == CEMIT_FILE_ASSEMBLY_FMOD ||
                 kind == CEMIT_FILE_ASSEMBLY_FMODF
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_u16 cemit_file_assembly_rounding_control(
    cemit_file_assembly_kind_t kind) {
  if (kind == CEMIT_FILE_ASSEMBLY_FLOOR ||
      kind == CEMIT_FILE_ASSEMBLY_FLOORF) {
    return 0x0400u;
  }
  if (kind == CEMIT_FILE_ASSEMBLY_CEIL ||
      kind == CEMIT_FILE_ASSEMBLY_CEILF) {
    return 0x0800u;
  }
  if (kind == CEMIT_FILE_ASSEMBLY_ROUND ||
      kind == CEMIT_FILE_ASSEMBLY_ROUNDF) {
    return 0u;
  }
  if (kind == CEMIT_FILE_ASSEMBLY_TRUNC ||
      kind == CEMIT_FILE_ASSEMBLY_TRUNCF) {
    return 0x0c00u;
  }
  return 0xffffu;
}

static ctool_status_t cemit_emit_file_assembly_rounding_body(
    cemit_context_t *context, cemit_file_assembly_kind_t kind,
    ctool_bool single_precision) {
  ctool_u16 width_bits =
      single_precision == CTOOL_TRUE ? 32u : 64u;
  ctool_i32 result_displacement =
      single_precision == CTOOL_TRUE ? 4 : 0;
  ctool_x86_mnemonic_t move =
      single_precision == CTOOL_TRUE ? CTOOL_X86_MN_MOVSS
                                     : CTOOL_X86_MN_MOVSD;
  ctool_u16 control_bits =
      cemit_file_assembly_rounding_control(kind);
  ctool_status_t status;
  if (cemit_file_assembly_is_rounding(kind) == CTOOL_FALSE ||
      control_bits == 0xffffu) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_x87_memory(
      context, CTOOL_X86_MN_FLD, 4u, 4, width_bits);
  if (status == CTOOL_OK) {
    status = cemit_x86_reserve_locals(context, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_control_memory(
        context, CTOOL_X86_MN_FNSTCW, 4u, 0);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_word_stack_ax(
        context, CTOOL_TRUE, 0);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_word_ax_immediate(
        context, CTOOL_X86_MN_AND, 0xf3ffu);
  }
  if (status == CTOOL_OK && control_bits != 0u) {
    status = cemit_x86_word_ax_immediate(
        context, CTOOL_X86_MN_OR, control_bits);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_word_stack_ax(
        context, CTOOL_FALSE, 2);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_control_memory(
        context, CTOOL_X86_MN_FLDCW, 4u, 2);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(
        context, CTOOL_X86_MN_FRNDINT);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_control_memory(
        context, CTOOL_X86_MN_FLDCW, 4u, 0);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FSTP, 4u,
        result_displacement, width_bits);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_sse_memory(
        context, move, CTOOL_TRUE, 0u, 4u,
        result_displacement, width_bits);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_add_register_constant(
        context, 4u, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(
        context, CTOOL_X86_MN_RET);
  }
  return status;
}

static ctool_status_t cemit_emit_file_assembly_fmod_body(
    cemit_context_t *context, cemit_file_assembly_kind_t kind,
    ctool_bool single_precision) {
  ctool_u16 width_bits =
      single_precision == CTOOL_TRUE ? 32u : 64u;
  ctool_u32 repeat_target;
  ctool_u32 repeat_patch;
  ctool_u32 repeat_after;
  ctool_status_t status;
  if (cemit_file_assembly_is_fmod(kind) == CTOOL_FALSE) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_x87_memory(
      context, CTOOL_X86_MN_FLD, 4u,
      single_precision == CTOOL_TRUE ? 8 : 12, width_bits);
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 4u, 4, width_bits);
  }
  repeat_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FPREM);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FNSTSW, CTOOL_X86_REG_GPR16, 0u, 16u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_word_ax_immediate(
        context, CTOOL_X86_MN_TEST, 0x0400u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_short_branch(
        context, CTOOL_X86_MN_JNE, &repeat_patch, &repeat_after);
  }
  if (status == CTOOL_OK) {
    status = cemit_patch_short_branch(
        context->active_text, repeat_patch, repeat_after, repeat_target);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FSTP, CTOOL_X86_REG_X87, 1u, 32u);
  }
  return status == CTOOL_OK
             ? cemit_finish_file_assembly_x87_result(
                   context, width_bits)
             : status;
}

static ctool_bool cemit_file_assembly_is_exponential(
    cemit_file_assembly_kind_t kind) {
  return kind >= CEMIT_FILE_ASSEMBLY_EXP2 &&
                 kind <= CEMIT_FILE_ASSEMBLY_EXPF
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_file_assembly_is_logarithm(
    cemit_file_assembly_kind_t kind) {
  return kind >= CEMIT_FILE_ASSEMBLY_LOG2 &&
                 kind <= CEMIT_FILE_ASSEMBLY_LOGF
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_file_assembly_exp_sequence(
    cemit_context_t *context) {
  ctool_status_t status = cemit_x86_one_register(
      context, CTOOL_X86_MN_FLD, CTOOL_X86_REG_X87, 0u, 32u);
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FRNDINT);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_FSUBR,
        CTOOL_X86_REG_X87, 1u, CTOOL_X86_REG_X87, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FXCH, CTOOL_X86_REG_X87, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_F2XM1);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FLD1);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FADDP, CTOOL_X86_REG_X87, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FSCALE);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FSTP, CTOOL_X86_REG_X87, 1u, 32u);
  }
  return status;
}

static ctool_status_t cemit_emit_file_assembly_exponential_body(
    cemit_context_t *context, cemit_file_assembly_kind_t kind,
    ctool_bool single_precision) {
  ctool_u16 width_bits =
      single_precision == CTOOL_TRUE ? 32u : 64u;
  ctool_bool natural =
      kind == CEMIT_FILE_ASSEMBLY_EXP ||
              kind == CEMIT_FILE_ASSEMBLY_EXPF
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  ctool_status_t status;
  if (cemit_file_assembly_is_exponential(kind) == CTOOL_FALSE ||
      (natural == CTOOL_TRUE &&
       (context->log2e_constant_symbol == CTOOL_C_AST_NONE ||
        context->log2e_constant_symbol >= context->symbol_count))) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_x87_memory(
      context, CTOOL_X86_MN_FLD, 4u, 4, width_bits);
  if (status == CTOOL_OK && natural == CTOOL_TRUE) {
    status = cemit_x86_x87_absolute_symbol(
        context, context->log2e_constant_symbol);
  }
  if (status == CTOOL_OK && natural == CTOOL_TRUE) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_FMULP, CTOOL_X86_REG_X87, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_emit_file_assembly_exp_sequence(context);
  }
  return status == CTOOL_OK
             ? cemit_finish_file_assembly_x87_result(
                   context, width_bits)
             : status;
}

static ctool_status_t cemit_emit_file_assembly_logarithm_body(
    cemit_context_t *context, cemit_file_assembly_kind_t kind,
    ctool_bool single_precision) {
  ctool_u16 width_bits =
      single_precision == CTOOL_TRUE ? 32u : 64u;
  ctool_bool natural =
      kind == CEMIT_FILE_ASSEMBLY_LOG ||
              kind == CEMIT_FILE_ASSEMBLY_LOGF
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  ctool_status_t status;
  if (cemit_file_assembly_is_logarithm(kind) == CTOOL_FALSE ||
      (natural == CTOOL_TRUE &&
       (context->ln2_constant_symbol == CTOOL_C_AST_NONE ||
        context->ln2_constant_symbol >= context->symbol_count))) {
    return CTOOL_ERR_INTERNAL;
  }
  status = natural == CTOOL_TRUE
               ? cemit_x86_x87_absolute_symbol(
                     context, context->ln2_constant_symbol)
               : cemit_x86_no_operand(context, CTOOL_X86_MN_FLD1);
  if (status == CTOOL_OK) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 4u, 4, width_bits);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FYL2X);
  }
  return status == CTOOL_OK
             ? cemit_finish_file_assembly_x87_result(
                   context, width_bits)
             : status;
}

static ctool_status_t cemit_emit_file_assembly_cdecl_bridge_body(
    cemit_context_t *context, cemit_file_assembly_kind_t kind,
    ctool_bool single_precision, ctool_u32 callee_symbol) {
  ctool_u32 parameter_count =
      cemit_file_assembly_parameter_count(kind);
  ctool_u32 scalar_bytes =
      single_precision == CTOOL_TRUE ? 4u : 8u;
  ctool_u32 argument_bytes;
  ctool_u32 argument_words;
  ctool_u32 word;
  ctool_status_t status = CTOOL_OK;
  if (cemit_file_assembly_is_cdecl_bridge(kind) == CTOOL_FALSE ||
      (parameter_count != 1u && parameter_count != 2u) ||
      callee_symbol >= context->symbol_count) {
    return CTOOL_ERR_INTERNAL;
  }
  argument_bytes = parameter_count * scalar_bytes;
  argument_words = argument_bytes / 4u;
  for (word = 0u; status == CTOOL_OK && word < argument_words;
       word++) {
    status = cemit_x86_push_stack_dword(
        context, argument_bytes);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_call_symbol(context, callee_symbol);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_add_register_constant(
        context, 4u, argument_bytes);
  }
  return status == CTOOL_OK
             ? cemit_finish_file_assembly_x87_result(
                   context,
                   single_precision == CTOOL_TRUE ? 32u : 64u)
             : status;
}

static ctool_status_t cemit_emit_file_assembly_body(
    cemit_context_t *context, cemit_file_assembly_kind_t kind,
    ctool_u32 callee_symbol) {
  ctool_bool single_precision =
      (((ctool_u32)kind - 1u) & 1u) != 0u ? CTOOL_TRUE : CTOOL_FALSE;
  ctool_u16 width_bits = single_precision == CTOOL_TRUE ? 32u : 64u;
  ctool_x86_mnemonic_t move =
      single_precision == CTOOL_TRUE ? CTOOL_X86_MN_MOVSS
                                     : CTOOL_X86_MN_MOVSD;
  ctool_status_t status;
  if (cemit_file_assembly_is_cdecl_bridge(kind) == CTOOL_TRUE) {
    return cemit_emit_file_assembly_cdecl_bridge_body(
        context, kind, single_precision, callee_symbol);
  }
  if (callee_symbol != CTOOL_C_AST_NONE) {
    return CTOOL_ERR_INTERNAL;
  }
  if (kind == CEMIT_FILE_ASSEMBLY_FABS ||
      kind == CEMIT_FILE_ASSEMBLY_FABSF) {
    ctool_u32 mask_symbol =
        kind == CEMIT_FILE_ASSEMBLY_FABSF
            ? context->fabs_mask_s_symbol
            : context->fabs_mask_d_symbol;
    ctool_x86_mnemonic_t bitwise_and =
        kind == CEMIT_FILE_ASSEMBLY_FABSF
            ? CTOOL_X86_MN_ANDPS
            : CTOOL_X86_MN_ANDPD;
    if (mask_symbol == CTOOL_C_AST_NONE ||
        mask_symbol >= context->symbol_count) {
      return CTOOL_ERR_INTERNAL;
    }
    status = cemit_x86_sse_memory(
        context, move, CTOOL_TRUE, 0u, 4u, 4, width_bits);
    if (status == CTOOL_OK) {
      status = cemit_x86_sse_absolute_mask(
          context, bitwise_and, mask_symbol);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_no_operand(context, CTOOL_X86_MN_RET);
    }
    return status;
  }
  if (cemit_file_assembly_is_rounding(kind) == CTOOL_TRUE) {
    return cemit_emit_file_assembly_rounding_body(
        context, kind, single_precision);
  }
  if (cemit_file_assembly_is_fmod(kind) == CTOOL_TRUE) {
    return cemit_emit_file_assembly_fmod_body(
        context, kind, single_precision);
  }
  if (cemit_file_assembly_is_exponential(kind) == CTOOL_TRUE) {
    return cemit_emit_file_assembly_exponential_body(
        context, kind, single_precision);
  }
  if (cemit_file_assembly_is_logarithm(kind) == CTOOL_TRUE) {
    return cemit_emit_file_assembly_logarithm_body(
        context, kind, single_precision);
  }
  if (kind == CEMIT_FILE_ASSEMBLY_SQRT ||
      kind == CEMIT_FILE_ASSEMBLY_SQRTF) {
    ctool_x86_mnemonic_t square_root =
        single_precision == CTOOL_TRUE ? CTOOL_X86_MN_SQRTSS
                                       : CTOOL_X86_MN_SQRTSD;
    status = cemit_x86_sse_memory(
        context, move, CTOOL_TRUE, 0u, 4u, 4, width_bits);
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, square_root, CTOOL_X86_REG_XMM, 0u,
          CTOOL_X86_REG_XMM, 0u, width_bits);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_no_operand(context, CTOOL_X86_MN_RET);
    }
    return status;
  }
  if (kind <= CEMIT_FILE_ASSEMBLY_NONE ||
      kind >= CEMIT_FILE_ASSEMBLY_COUNT) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_x87_memory(
      context, CTOOL_X86_MN_FLD, 4u, 4, width_bits);
  if (status == CTOOL_OK &&
      (kind == CEMIT_FILE_ASSEMBLY_ATAN2 ||
       kind == CEMIT_FILE_ASSEMBLY_ATAN2F)) {
    status = cemit_x86_x87_memory(
        context, CTOOL_X86_MN_FLD, 4u,
        single_precision == CTOOL_TRUE ? 8 : 12, width_bits);
  }
  if (status == CTOOL_OK &&
      (kind == CEMIT_FILE_ASSEMBLY_SIN ||
       kind == CEMIT_FILE_ASSEMBLY_SINF)) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FSIN);
  } else if (status == CTOOL_OK &&
             (kind == CEMIT_FILE_ASSEMBLY_COS ||
              kind == CEMIT_FILE_ASSEMBLY_COSF)) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FCOS);
  } else if (status == CTOOL_OK &&
             (kind == CEMIT_FILE_ASSEMBLY_TAN ||
              kind == CEMIT_FILE_ASSEMBLY_TANF)) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FPTAN);
    if (status == CTOOL_OK) {
      status = cemit_x86_one_register(
          context, CTOOL_X86_MN_FSTP, CTOOL_X86_REG_X87, 0u, 32u);
    }
  } else if (status == CTOOL_OK &&
             (kind == CEMIT_FILE_ASSEMBLY_ATAN ||
              kind == CEMIT_FILE_ASSEMBLY_ATANF)) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FLD1);
    if (status == CTOOL_OK) {
      status = cemit_x86_no_operand(context, CTOOL_X86_MN_FPATAN);
    }
  } else if (status == CTOOL_OK &&
             (kind == CEMIT_FILE_ASSEMBLY_ATAN2 ||
              kind == CEMIT_FILE_ASSEMBLY_ATAN2F)) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_FPATAN);
  } else if (status == CTOOL_OK) {
    return CTOOL_ERR_INTERNAL;
  }
  return status == CTOOL_OK
             ? cemit_finish_file_assembly_x87_result(
                   context, width_bits)
             : status;
}

static ctool_status_t cemit_place_fabs_masks(
    cemit_context_t *context) {
  static const ctool_u8 bytes[] = {
      0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x7fu,
      0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x7fu,
      0xffu, 0xffu, 0xffu, 0x7fu,
      0xffu, 0xffu, 0xffu, 0x7fu,
      0xffu, 0xffu, 0xffu, 0x7fu,
      0xffu, 0xffu, 0xffu, 0x7fu};
  ctool_elf32_symbol_spec_t *mask_d;
  ctool_elf32_symbol_spec_t *mask_s;
  ctool_u32 start;
  ctool_status_t status;
  if (context->fabs_mask_d_symbol >= context->symbol_count ||
      context->fabs_mask_s_symbol >= context->symbol_count) {
    return CTOOL_ERR_INTERNAL;
  }
  mask_d = &context->symbols[context->fabs_mask_d_symbol];
  mask_s = &context->symbols[context->fabs_mask_s_symbol];
  if (cemit_strings_equal(
          mask_d->name, ctool_string("fabs_mask_d")) == CTOOL_FALSE ||
      cemit_strings_equal(
          mask_s->name, ctool_string("fabs_mask_s")) == CTOOL_FALSE ||
      mask_d->binding != CTOOL_ELF32_BIND_LOCAL ||
      mask_s->binding != CTOOL_ELF32_BIND_LOCAL ||
      mask_d->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
      mask_s->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
      mask_d->visibility != CTOOL_ELF32_VIS_DEFAULT ||
      mask_s->visibility != CTOOL_ELF32_VIS_DEFAULT ||
      mask_d->placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      mask_s->placement != CTOOL_ELF32_SYMBOL_UNDEFINED) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_align_buffer(context, CEMIT_SECTION_RODATA, 16u);
  if (status == CTOOL_OK) {
    status = cemit_raise_section_alignment(
        context, CEMIT_SECTION_RODATA, 16u);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  start = ctool_buffer_view(context->rodata).size;
  if (start > 0xffffffffu - (ctool_u32)sizeof(bytes)) {
    return CTOOL_ERR_OVERFLOW;
  }
  status = ctool_buffer_append(
      context->rodata, ctool_bytes(bytes, (ctool_u32)sizeof(bytes)));
  if (status != CTOOL_OK) {
    return status;
  }
  mask_d->placement = CTOOL_ELF32_SYMBOL_DEFINED;
  mask_d->section = CEMIT_SECTION_RODATA;
  mask_d->value = start;
  mask_d->size = 0u;
  mask_d->alignment = 0u;
  mask_s->placement = CTOOL_ELF32_SYMBOL_DEFINED;
  mask_s->section = CEMIT_SECTION_RODATA;
  mask_s->value = start + 16u;
  mask_s->size = 0u;
  mask_s->alignment = 0u;
  return CTOOL_OK;
}

static ctool_status_t cemit_place_exp_log_constants(
    cemit_context_t *context) {
  static const ctool_u8 bytes[] = {
      0xfeu, 0x82u, 0x2bu, 0x65u, 0x47u, 0x15u, 0xf7u, 0x3fu,
      0xefu, 0x39u, 0xfau, 0xfeu, 0x42u, 0x2eu, 0xe6u, 0x3fu};
  ctool_elf32_symbol_spec_t *log2e;
  ctool_elf32_symbol_spec_t *ln2;
  ctool_u32 start;
  ctool_status_t status;
  if (context->log2e_constant_symbol >= context->symbol_count ||
      context->ln2_constant_symbol >= context->symbol_count) {
    return CTOOL_ERR_INTERNAL;
  }
  log2e = &context->symbols[context->log2e_constant_symbol];
  ln2 = &context->symbols[context->ln2_constant_symbol];
  if (cemit_strings_equal(
          log2e->name,
          ctool_string("libm_log2e_const")) == CTOOL_FALSE ||
      cemit_strings_equal(
          ln2->name, ctool_string("libm_ln2_const")) == CTOOL_FALSE ||
      log2e->binding != CTOOL_ELF32_BIND_LOCAL ||
      ln2->binding != CTOOL_ELF32_BIND_LOCAL ||
      log2e->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
      ln2->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
      log2e->visibility != CTOOL_ELF32_VIS_DEFAULT ||
      ln2->visibility != CTOOL_ELF32_VIS_DEFAULT ||
      log2e->placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      ln2->placement != CTOOL_ELF32_SYMBOL_UNDEFINED) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_align_buffer(context, CEMIT_SECTION_RODATA, 8u);
  if (status == CTOOL_OK) {
    status = cemit_raise_section_alignment(
        context, CEMIT_SECTION_RODATA, 8u);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  start = ctool_buffer_view(context->rodata).size;
  if (start > 0xffffffffu - (ctool_u32)sizeof(bytes)) {
    return CTOOL_ERR_OVERFLOW;
  }
  status = ctool_buffer_append(
      context->rodata, ctool_bytes(bytes, (ctool_u32)sizeof(bytes)));
  if (status != CTOOL_OK) {
    return status;
  }
  log2e->placement = CTOOL_ELF32_SYMBOL_DEFINED;
  log2e->section = CEMIT_SECTION_RODATA;
  log2e->value = start;
  log2e->size = 0u;
  log2e->alignment = 0u;
  ln2->placement = CTOOL_ELF32_SYMBOL_DEFINED;
  ln2->section = CEMIT_SECTION_RODATA;
  ln2->value = start + 8u;
  ln2->size = 0u;
  ln2->alignment = 0u;
  return CTOOL_OK;
}

static ctool_status_t cemit_emit_dglibc_setjmp_body(
    cemit_context_t *context) {
  static const ctool_u8 saved_registers[] = {3u, 6u, 7u, 5u, 4u};
  ctool_u32 index;
  ctool_status_t status = cemit_x86_load_stack(context, 0u, 4u);
  for (index = 0u; status == CTOOL_OK &&
                   index < (ctool_u32)sizeof(saved_registers);
       index++) {
    status = cemit_x86_store_register_at_register_offset(
        context, 0u, index * 4u, saved_registers[index]);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(context, 1u, 0u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_store_register_at_register_offset(
        context, 0u, 20u, 1u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_XOR, CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_GPR32, 0u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(context, CTOOL_X86_MN_RET);
  }
  return status;
}

static ctool_status_t cemit_emit_dglibc_longjmp_body(
    cemit_context_t *context) {
  static const ctool_u8 restored_registers[] = {
      3u, 6u, 7u, 5u, 4u, 2u};
  ctool_u32 nonzero_patch = 0u;
  ctool_u32 nonzero_after = 0u;
  ctool_u32 nonzero_target;
  ctool_u32 index;
  ctool_status_t status = cemit_x86_load_stack(context, 0u, 4u);
  if (status == CTOOL_OK) {
    status = cemit_x86_load_stack(context, 1u, 8u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_TEST, CTOOL_X86_REG_GPR32, 1u,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_short_branch(
        context, CTOOL_X86_MN_JNE, &nonzero_patch, &nonzero_after);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_move_register_constant(context, 1u, 1u);
  }
  nonzero_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_short_branch(
        context->active_text, nonzero_patch, nonzero_after, nonzero_target);
  }
  for (index = 0u; status == CTOOL_OK &&
                   index < (ctool_u32)sizeof(restored_registers);
       index++) {
    status = cemit_x86_load_register_at_register_compact(
        context, restored_registers[index], 0u, index * 4u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_two_registers(
        context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 0u,
        CTOOL_X86_REG_GPR32, 1u, 32u);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_jump_register(context, 2u);
  }
  return status;
}

static ctool_status_t cemit_define_file_assembly_function(
    cemit_context_t *context, ctool_u32 binding, ctool_u32 start,
    ctool_u32 size, const ctool_c_pp_location_t *location) {
  ctool_u32 symbol_index;
  ctool_status_t status =
      cemit_ensure_binding_symbol(context, binding, &symbol_index);
  if (status != CTOOL_OK) {
    return status;
  }
  if (symbol_index >= context->symbol_count ||
      context->symbols[symbol_index].placement !=
          CTOOL_ELF32_SYMBOL_UNDEFINED) {
    return cemit_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL, location,
        "GNU file-scope assembly defines one function twice");
  }
  context->symbols[symbol_index].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  context->symbols[symbol_index].section = CEMIT_SECTION_TEXT;
  context->symbols[symbol_index].value = start;
  context->symbols[symbol_index].size = size;
  context->symbols[symbol_index].alignment = 0u;
  return CTOOL_OK;
}

static ctool_status_t cemit_place_dglibc_jumps(
    cemit_context_t *context, const ctool_c_assembly_t *assembly,
    ctool_u32 setjmp_binding, ctool_u32 longjmp_binding) {
  ctool_u32 setjmp_start;
  ctool_u32 setjmp_size;
  ctool_u32 longjmp_start;
  ctool_u32 longjmp_size;
  ctool_status_t status;
  if (setjmp_binding >= context->unit->binding_count ||
      longjmp_binding >= context->unit->binding_count) {
    return cemit_invalid_unit(context, &assembly->location);
  }
  context->active_text = context->text;
  context->active_text_section = CEMIT_SECTION_TEXT;
  setjmp_start = ctool_buffer_view(context->text).size;
  status = cemit_emit_dglibc_setjmp_body(context);
  setjmp_size = ctool_buffer_view(context->text).size - setjmp_start;
  longjmp_start = ctool_buffer_view(context->text).size;
  if (status == CTOOL_OK) {
    status = cemit_emit_dglibc_longjmp_body(context);
  }
  longjmp_size = ctool_buffer_view(context->text).size - longjmp_start;
  if (status == CTOOL_OK && (setjmp_size == 0u || longjmp_size == 0u)) {
    status = CTOOL_ERR_INTERNAL;
  }
  if (status == CTOOL_OK) {
    status = cemit_raise_section_alignment(
        context, CEMIT_SECTION_TEXT, 1u);
  }
  if (status == CTOOL_OK) {
    status = cemit_define_file_assembly_function(
        context, setjmp_binding, setjmp_start, setjmp_size,
        &assembly->location);
  }
  if (status == CTOOL_OK) {
    status = cemit_define_file_assembly_function(
        context, longjmp_binding, longjmp_start, longjmp_size,
        &assembly->location);
  }
  return status;
}

static ctool_status_t cemit_place_file_assembly(
    cemit_context_t *context, ctool_u32 index) {
  const ctool_c_assembly_t *assembly;
  cemit_file_assembly_kind_t kind;
  ctool_u32 binding;
  ctool_u32 callee_binding;
  ctool_u32 callee_symbol = CTOOL_C_AST_NONE;
  ctool_u32 start;
  ctool_u32 size;
  ctool_status_t status;
  if (index >= context->ir.file_assembly_count ||
      context->ir.file_assemblies[index] >=
          context->unit->file_assembly_count ||
      context->file_assembly_bindings == (ctool_u32 *)0 ||
      context->file_assembly_callee_bindings == (ctool_u32 *)0 ||
      context->file_assembly_kinds == (ctool_u32 *)0) {
    return cemit_invalid_unit(
        context, (const ctool_c_pp_location_t *)0);
  }
  assembly = &context->unit->file_assemblies[
      context->ir.file_assemblies[index]];
  kind = (cemit_file_assembly_kind_t)
      context->file_assembly_kinds[index];
  if (kind <= CEMIT_FILE_ASSEMBLY_NONE ||
      kind >= CEMIT_FILE_ASSEMBLY_COUNT) {
    return cemit_invalid_unit(context, &assembly->location);
  }
  binding = context->file_assembly_bindings[index];
  callee_binding =
      context->file_assembly_callee_bindings[index];
  if (kind == CEMIT_FILE_ASSEMBLY_FABS_MASKS) {
    if (binding != CTOOL_C_AST_NONE ||
        callee_binding != CTOOL_C_AST_NONE ||
        context->fabs_mask_assembly != index) {
      return cemit_invalid_unit(context, &assembly->location);
    }
    return cemit_place_fabs_masks(context);
  }
  if (kind == CEMIT_FILE_ASSEMBLY_EXP_LOG_CONSTANTS) {
    if (binding != CTOOL_C_AST_NONE ||
        callee_binding != CTOOL_C_AST_NONE ||
        context->exp_log_constant_assembly != index) {
      return cemit_invalid_unit(context, &assembly->location);
    }
    return cemit_place_exp_log_constants(context);
  }
  if (kind == CEMIT_FILE_ASSEMBLY_DGLIBC_JUMPS) {
    return cemit_place_dglibc_jumps(
        context, assembly, binding, callee_binding);
  }
  if (binding >= context->unit->binding_count) {
    return cemit_invalid_unit(context, &assembly->location);
  }
  if (cemit_file_assembly_is_cdecl_bridge(kind) == CTOOL_TRUE) {
    if (callee_binding >= context->unit->binding_count) {
      return cemit_invalid_unit(context, &assembly->location);
    }
    status = cemit_ensure_binding_symbol(
        context, callee_binding, &callee_symbol);
    if (status != CTOOL_OK) {
      return status;
    }
  } else if (callee_binding != CTOOL_C_AST_NONE) {
    return cemit_invalid_unit(context, &assembly->location);
  }
  context->active_text = context->text;
  context->active_text_section = CEMIT_SECTION_TEXT;
  start = ctool_buffer_view(context->text).size;
  status = cemit_emit_file_assembly_body(
      context, kind, callee_symbol);
  if (status != CTOOL_OK) {
    return status;
  }
  size = ctool_buffer_view(context->text).size - start;
  if (size == 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_raise_section_alignment(
      context, CEMIT_SECTION_TEXT, 1u);
  if (status != CTOOL_OK) {
    return status;
  }
  return cemit_define_file_assembly_function(
      context, binding, start, size, &assembly->location);
}

static ctool_bool cemit_assembly_is_naked_control(
    const ctool_c_assembly_t *assembly) {
  return cemit_naked_ipi_wrapper_template(
             assembly->template_text, (ctool_string_t *)0) == CTOOL_TRUE ||
                 cemit_naked_panic_template(
                     assembly->template_text) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_validate_naked_codegen(
    cemit_context_t *context,
    const ctool_c_ir_function_t *function,
    const ctool_c_type_node_t *function_type,
    const ctool_c_pp_location_t *location) {
  const ctool_c_ir_instruction_t *assembly_instruction;
  const ctool_c_ir_instruction_t *return_instruction;
  ctool_u32 instruction_index;
  ctool_bool naked =
      (function->function_codegen_attributes &
       CTOOL_C_DECL_ATTR_NAKED) != 0u
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  if (naked == CTOOL_FALSE) {
    for (instruction_index = function->first_instruction;
         instruction_index <
             function->first_instruction + function->instruction_count;
         instruction_index++) {
      const ctool_c_ir_instruction_t *instruction =
          &context->ir.instructions[instruction_index];
      if (instruction->kind == CTOOL_C_IR_INSTRUCTION_ASSEMBLY &&
          instruction->reference < context->unit->assembly_count &&
          cemit_assembly_is_naked_control(
              &context->unit->assemblies[instruction->reference]) ==
              CTOOL_TRUE) {
        return cemit_invalid_unit(context, &instruction->location);
      }
    }
    return CTOOL_OK;
  }
  if (function_type->has_prototype == CTOOL_FALSE ||
      function_type->parameter_count != 0u ||
      function_type->variadic == CTOOL_TRUE ||
      cemit_ir_type_is_void(
          context, function_type->referenced_type) == CTOOL_FALSE ||
      function->instruction_count != 2u ||
      function->maximum_stack_depth != 0u) {
    return cemit_invalid_unit(context, location);
  }
  assembly_instruction =
      &context->ir.instructions[function->first_instruction];
  return_instruction =
      &context->ir.instructions[function->first_instruction + 1u];
  if (assembly_instruction->kind !=
          CTOOL_C_IR_INSTRUCTION_ASSEMBLY ||
      assembly_instruction->reference >= context->unit->assembly_count ||
      cemit_assembly_is_naked_control(
          &context->unit
               ->assemblies[assembly_instruction->reference]) ==
          CTOOL_FALSE ||
      cemit_naked_control_assembly_metadata_is_valid(
          context,
          &context->unit
               ->assemblies[assembly_instruction->reference]) ==
          CTOOL_FALSE ||
      return_instruction->kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      return_instruction->type != CTOOL_C_TYPE_NONE ||
      return_instruction->input_type != CTOOL_C_TYPE_NONE ||
      return_instruction->operation !=
          CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      return_instruction->conversion != CTOOL_C_CONVERSION_NONE ||
      return_instruction->argument_count != 0u ||
      return_instruction->first_argument_type != CTOOL_C_AST_NONE ||
      return_instruction->reference != CTOOL_C_AST_NONE ||
      return_instruction->integer_bits != 0u) {
    return cemit_invalid_unit(context, location);
  }
  return CTOOL_OK;
}

static ctool_bool cemit_kernel_entry_stack_reset_is_valid(
    const cemit_context_t *context,
    const ctool_c_ir_function_t *function,
    const ctool_c_binding_t *binding,
    const ctool_c_type_node_t *function_type,
    ctool_u32 frame_size, const ctool_u32 *stack_depths) {
  const ctool_c_ir_instruction_t *instruction;
  if (context == (const cemit_context_t *)0 ||
      function == (const ctool_c_ir_function_t *)0 ||
      binding == (const ctool_c_binding_t *)0 ||
      function_type == (const ctool_c_type_node_t *)0 ||
      stack_depths == (const ctool_u32 *)0 ||
      function->instruction_count == 0u ||
      function->first_instruction >= context->ir.instruction_count) {
    return CTOOL_FALSE;
  }
  instruction =
      &context->ir.instructions[function->first_instruction];
  return binding->kind == CTOOL_C_BINDING_FUNCTION &&
                 binding->linkage == CTOOL_C_LINKAGE_EXTERNAL &&
                 binding->file_scope_visible == CTOOL_TRUE &&
                 cemit_strings_equal(
                     binding->name, ctool_string("_start")) == CTOOL_TRUE &&
                 (binding->attributes &
                  CTOOL_C_DECL_ATTR_SECTION) != 0u &&
                 cemit_strings_equal(
                     binding->section_name,
                     ctool_string(".text.start")) == CTOOL_TRUE &&
                 function->function_codegen_attributes == 0u &&
                 function_type->kind == CTOOL_C_TYPE_FUNCTION &&
                 function_type->has_prototype == CTOOL_TRUE &&
                 function_type->parameter_count == 0u &&
                 function_type->variadic == CTOOL_FALSE &&
                 cemit_ir_type_is_void(
                     context, function_type->referenced_type) == CTOOL_TRUE &&
                 frame_size == 0u &&
                 stack_depths[0] == 0u &&
                 instruction->kind ==
                     CTOOL_C_IR_INSTRUCTION_ASSEMBLY &&
                 instruction->reference < context->unit->assembly_count &&
                 cemit_kernel_bss_clear_template(
                     context->unit->assemblies[instruction->reference]
                         .template_text) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cemit_emit_kernel_entry_terminal(
    cemit_context_t *context,
    const ctool_c_ir_instruction_t *instruction) {
  ctool_u32 loop_target;
  ctool_u32 jump_patch = 0u;
  ctool_u32 jump_after = 0u;
  ctool_status_t status;
  if (instruction == (const ctool_c_ir_instruction_t *)0 ||
      instruction->kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      instruction->type != CTOOL_C_TYPE_NONE ||
      instruction->input_type != CTOOL_C_TYPE_NONE ||
      instruction->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instruction->conversion != CTOOL_C_CONVERSION_NONE ||
      instruction->argument_count != 0u ||
      instruction->first_argument_type != CTOOL_C_AST_NONE ||
      instruction->reference != CTOOL_C_AST_NONE ||
      instruction->integer_bits != 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_x86_no_operand(
      context, CTOOL_X86_MN_CLI);
  loop_target = ctool_buffer_view(context->active_text).size;
  if (status == CTOOL_OK) {
    status = cemit_x86_no_operand(
        context, CTOOL_X86_MN_HLT);
  }
  if (status == CTOOL_OK) {
    status = cemit_x86_branch(
        context, CTOOL_X86_MN_JMP, &jump_patch, &jump_after);
  }
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->active_text, jump_patch, jump_after, loop_target);
  }
  return status;
}

static ctool_status_t cemit_place_function(cemit_context_t *context,
                                           ctool_u32 function_index) {
  const ctool_c_function_definition_t *definition =
      &context->unit->function_definitions[function_index];
  const ctool_c_ir_function_t *function =
      &context->ir.functions[function_index];
  const ctool_c_binding_t *binding =
      &context->unit->bindings[definition->binding];
  const ctool_c_type_node_t *function_type =
      cemit_unwrapped_type(context, definition->declared_type);
  ctool_u32 alignment = binding->minimum_alignment == 0u
                            ? 1u
                            : binding->minimum_alignment;
  ctool_u32 section = context->binding_sections[definition->binding] ==
                              CTOOL_C_AST_NONE
                          ? CEMIT_SECTION_TEXT
                          : context->binding_sections[definition->binding];
  ctool_u32 function_start;
  ctool_u32 function_size;
  ctool_u32 frame_size;
  ctool_u32 symbol_index = CTOOL_C_AST_NONE;
  ctool_u32 *instruction_offsets = (ctool_u32 *)0;
  ctool_u32 *branch_patches = (ctool_u32 *)0;
  ctool_u32 *branch_afters = (ctool_u32 *)0;
  ctool_u32 *stack_depths = (ctool_u32 *)0;
  ctool_u32 stack_base_residue = 8u;
  ctool_bool kernel_entry_stack_reset = CTOOL_FALSE;
  ctool_bool naked =
      (function->function_codegen_attributes &
       CTOOL_C_DECL_ATTR_NAKED) != 0u
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  ctool_u32 index;
  ctool_status_t status;
  if (function_type == (const ctool_c_type_node_t *)0 ||
      function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      cemit_power_of_two(alignment) == CTOOL_FALSE ||
      function->instruction_count == 0u ||
      function->first_instruction > context->ir.instruction_count ||
      function->instruction_count >
          context->ir.instruction_count - function->first_instruction ||
      function->instruction_count == 0xffffffffu ||
      cemit_strings_equal(
          function->section_name,
          (binding->attributes & CTOOL_C_DECL_ATTR_SECTION) != 0u
              ? binding->section_name
              : ctool_string("")) == CTOOL_FALSE ||
      (cemit_section_flags(context, section) &
       CTOOL_ELF32_SHF_EXECINSTR) == 0u) {
    return cemit_invalid_unit(context, &definition->location);
  }
  status = cemit_validate_general_regs_only_codegen(
      context, function, &definition->location);
  if (status == CTOOL_OK) {
    status = cemit_validate_naked_codegen(
        context, function, function_type, &definition->location);
  }
  if (status == CTOOL_OK) {
    status = cemit_prepare_local_offsets(context, function, &frame_size);
  }
  if (status == CTOOL_OK && naked == CTOOL_TRUE &&
      frame_size != 0u) {
    status = cemit_invalid_unit(context, &definition->location);
  }
  if (status == CTOOL_OK) {
    status = cemit_analyze_stack_depths(
        context, function, &stack_depths);
  }
  for (index = 0u; status == CTOOL_OK &&
                    index < function->instruction_count;
       index++) {
    const ctool_c_ir_instruction_t *instruction =
        &context->ir.instructions[function->first_instruction + index];
    if (instruction->kind == CTOOL_C_IR_INSTRUCTION_ASSEMBLY &&
        instruction->reference < context->unit->assembly_count &&
        cemit_kernel_bss_clear_template(
            context->unit->assemblies[instruction->reference]
                .template_text) == CTOOL_TRUE) {
      if (kernel_entry_stack_reset == CTOOL_TRUE || index != 0u) {
        status = cemit_invalid_unit(
            context, &instruction->location);
      } else if (frame_size != 0u) {
        status = cemit_emit_failure(
            context, CTOOL_ERR_UNSUPPORTED,
            CTOOL_C_EMIT_DIAG_UNSUPPORTED,
            &instruction->location,
            "kernel entry stack reset cannot use a compiler-managed frame");
      } else if (cemit_kernel_entry_stack_reset_is_valid(
                     context, function, binding, function_type,
                     frame_size, stack_depths) == CTOOL_FALSE) {
        status = cemit_invalid_unit(
            context, &instruction->location);
      } else {
        kernel_entry_stack_reset = CTOOL_TRUE;
      }
    }
  }
  if (status == CTOOL_OK) {
    status = cemit_align_buffer(context, section, alignment);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  context->active_text = cemit_section_buffer(context, section);
  context->active_text_section = section;
  if (context->active_text == (ctool_buffer_t *)0) {
    return CTOOL_ERR_INTERNAL;
  }
  function_start = ctool_buffer_view(context->active_text).size;
  status = cemit_alloc_array(
      context, function->instruction_count + 1u,
      (ctool_u32)sizeof(ctool_u32), (void **)&instruction_offsets);
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        context, function->instruction_count,
        (ctool_u32)sizeof(ctool_u32), (void **)&branch_patches);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        context, function->instruction_count,
        (ctool_u32)sizeof(ctool_u32), (void **)&branch_afters);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    branch_patches[index] = CTOOL_C_AST_NONE;
    branch_afters[index] = CTOOL_C_AST_NONE;
  }
  if (naked == CTOOL_FALSE) {
    status = cemit_x86_one_register(
        context, CTOOL_X86_MN_PUSH, CTOOL_X86_REG_GPR32, 5u, 32u);
    if (status == CTOOL_OK) {
      status = cemit_x86_two_registers(
          context, CTOOL_X86_MN_MOV, CTOOL_X86_REG_GPR32, 5u,
          CTOOL_X86_REG_GPR32, 4u, 32u);
    }
    if (status == CTOOL_OK) {
      status = cemit_x86_reserve_locals(context, frame_size);
    }
  } else {
    status = CTOOL_OK;
  }
  for (index = 0u; status == CTOOL_OK &&
                    index < function->instruction_count;
       index++) {
    const ctool_c_ir_instruction_t *instruction =
        &context->ir.instructions[function->first_instruction + index];
    instruction_offsets[index] =
        ctool_buffer_view(context->active_text).size - function_start;
    if (naked == CTOOL_TRUE &&
        index + 1u == function->instruction_count) {
      status = CTOOL_OK;
    } else if (kernel_entry_stack_reset == CTOOL_TRUE &&
               instruction->kind ==
                   CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
      status = cemit_emit_kernel_entry_terminal(
          context, instruction);
    } else {
      status = cemit_emit_ir_instruction(
          context, instruction, function_type,
          context->block_binding_offsets, index,
          context->value_temporary_offsets
              [function->first_instruction + index],
          stack_base_residue, frame_size, stack_depths[index],
          branch_patches, branch_afters);
      if (status == CTOOL_OK &&
          kernel_entry_stack_reset == CTOOL_TRUE &&
          index == 0u) {
        stack_base_residue = 0u;
      }
    }
  }
  if (status != CTOOL_OK) {
    return status;
  }
  instruction_offsets[function->instruction_count] =
      ctool_buffer_view(context->active_text).size - function_start;
  for (index = 0u; index < function->instruction_count; index++) {
    const ctool_c_ir_instruction_t *instruction =
        &context->ir.instructions[function->first_instruction + index];
    if (branch_patches[index] != CTOOL_C_AST_NONE) {
      ctool_u32 target;
      if ((instruction->kind != CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO &&
           instruction->kind != CTOOL_C_IR_INSTRUCTION_JUMP) ||
          instruction->reference >= function->instruction_count) {
        return CTOOL_ERR_INTERNAL;
      }
      target = function_start + instruction_offsets[instruction->reference];
      status = cemit_patch_branch(context->active_text,
                                  branch_patches[index],
                                  branch_afters[index], target);
      if (status != CTOOL_OK) {
        return status;
      }
    }
  }
  function_size =
      ctool_buffer_view(context->active_text).size - function_start;
  if (function_size == 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_raise_section_alignment(context, section, alignment);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cemit_ensure_binding_symbol(context, definition->binding,
                                       &symbol_index);
  if (status != CTOOL_OK) {
    return status;
  }
  context->symbols[symbol_index].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  context->symbols[symbol_index].section = section;
  context->symbols[symbol_index].value = function_start;
  context->symbols[symbol_index].size = function_size;
  context->symbols[symbol_index].alignment = 0u;
  return CTOOL_OK;
}

static ctool_status_t cemit_build_sections(
    cemit_context_t *context, ctool_elf32_section_spec_t *sections,
    ctool_u32 *section_count_out) {
  ctool_u32 logical_count =
      CEMIT_SECTION_COUNT + context->named_section_count;
  ctool_u32 *section_map = (ctool_u32 *)0;
  ctool_u32 section_count = 0u;
  ctool_u32 logical;
  ctool_u32 index;
  ctool_status_t status = cemit_alloc_array(
      context, logical_count, (ctool_u32)sizeof(*section_map),
      (void **)&section_map);
  if (status != CTOOL_OK) {
    return status;
  }
  for (logical = 0u; logical < logical_count; logical++) {
    ctool_u32 size = logical == CEMIT_SECTION_BSS
                         ? context->bss_size
                         : ctool_buffer_view(
                               cemit_section_buffer(context, logical))
                               .size;
    section_map[logical] = CTOOL_ELF32_NO_SECTION;
    if (size != 0u) {
      ctool_elf32_section_spec_t *section = &sections[section_count];
      section_map[logical] = section_count++;
      section->name = cemit_section_name(context, logical);
      section->type = logical == CEMIT_SECTION_BSS
                          ? CTOOL_ELF32_SHT_NOBITS
                          : CTOOL_ELF32_SHT_PROGBITS;
      section->flags = cemit_section_flags(context, logical);
      section->alignment = cemit_section_alignment(context, logical);
      section->entry_size = 0u;
      section->size = size;
      section->contents = logical == CEMIT_SECTION_BSS
                              ? ctool_bytes((const void *)0, 0u)
                              : ctool_buffer_view(
                                    cemit_section_buffer(context, logical));
    }
  }
  for (index = 0u; index < context->symbol_count; index++) {
    ctool_elf32_symbol_spec_t *symbol = &context->symbols[index];
    if (symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED) {
      if (symbol->section >= logical_count ||
          section_map[symbol->section] == CTOOL_ELF32_NO_SECTION) {
        return CTOOL_ERR_INTERNAL;
      }
      symbol->section = section_map[symbol->section];
    } else if (symbol->binding == CTOOL_ELF32_BIND_LOCAL) {
      return cemit_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
          (const ctool_c_pp_location_t *)0,
          "CupidC found an unresolved internal-linkage symbol");
    }
  }
  for (index = 0u; index < context->relocation_count; index++) {
    ctool_elf32_relocation_spec_t *relocation =
        &context->relocations[index];
    if (relocation->target_section >= logical_count ||
        section_map[relocation->target_section] ==
            CTOOL_ELF32_NO_SECTION) {
      return CTOOL_ERR_INTERNAL;
    }
    relocation->target_section = section_map[relocation->target_section];
  }
  *section_count_out = section_count;
  return CTOOL_OK;
}

static ctool_status_t cemit_open_buffers(cemit_context_t *context) {
  const ctool_limits_t *limits = ctool_job_limits(context->job);
  ctool_u32 initial_capacity =
      limits->output_bytes < 256u ? limits->output_bytes : 256u;
  ctool_status_t status = ctool_job_open_buffer(
      context->job, initial_capacity, limits->output_bytes,
      &context->text);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(
      context->job, initial_capacity, limits->output_bytes,
      &context->rodata);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(context->job, initial_capacity,
                                   limits->output_bytes, &context->data);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(context->job, initial_capacity,
                                   limits->output_bytes,
                                   &context->object_output);
  }
  if (status == CTOOL_OK) {
    context->active_text = context->text;
    context->active_text_section = CEMIT_SECTION_TEXT;
  }
  return status;
}

ctool_status_t ctool_c_emit_object(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_buffer_t *output) {
  cemit_context_t context;
  ctool_arena_mark_t mark;
  ctool_elf32_section_spec_t *sections = (ctool_elf32_section_spec_t *)0;
  ctool_elf32_object_spec_t object;
  ctool_u32 text_relocation_count = 0u;
  ctool_u32 block_static_count = 0u;
  ctool_u32 file_assembly_symbol_count = 0u;
  ctool_u32 section_count = 0u;
  ctool_u32 symbol_capacity;
  ctool_u32 index;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  ctool_status_t rewind_status;
  if (job == (ctool_job_t *)0 ||
      unit == (const ctool_c_translation_unit_t *)0 ||
      output == (ctool_buffer_t *)0 || ctool_buffer_view(output).size != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  cemit_zero(&context, (ctool_u32)sizeof(context));
  cemit_zero(&object, (ctool_u32)sizeof(object));
  context.job = job;
  context.unit = unit;
  context.arena = ctool_job_arena(job);
  mark = ctool_arena_mark(context.arena);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = cemit_validate_unit_shape(&context);
  if (status == CTOOL_OK &&
      unit->binding_count > 0xffffffffu - CEMIT_SECTION_COUNT) {
    status = CTOOL_ERR_OVERFLOW;
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, CEMIT_SECTION_COUNT + unit->binding_count,
        (ctool_u32)sizeof(*sections), (void **)&sections);
  }
  if (status == CTOOL_OK) {
    status = ctool_c_lower_ir(job, unit, &context.ir);
  }
  if (status == CTOOL_OK) {
    status = cemit_validate_argument_type_slices(&context);
  }
  if (status == CTOOL_OK) {
    for (index = 0u; index < unit->block_binding_count; index++) {
      if (unit->block_bindings[index].storage == CTOOL_C_STORAGE_STATIC) {
        block_static_count++;
      }
    }
    for (index = 0u; index < context.ir.instruction_count; index++) {
      const ctool_c_ir_instruction_t *instruction =
          &context.ir.instructions[index];
      ctool_u32 added_relocations = 0u;
      if (instruction->kind == CTOOL_C_IR_INSTRUCTION_ASSEMBLY &&
          instruction->reference < unit->assembly_count &&
          cemit_kernel_bss_clear_template(
              unit->assemblies[instruction->reference]
                  .template_text) == CTOOL_TRUE) {
        added_relocations = 2u;
      } else if (
          cemit_has_binding_text_relocation(instruction->kind) ==
              CTOOL_TRUE ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_ASSEMBLY &&
           instruction->reference < unit->assembly_count &&
           unit->assemblies[instruction->reference]
                   .direct_call_binding_plus_one != 0u) ||
          instruction->kind ==
              CTOOL_C_IR_INSTRUCTION_STRING_LITERAL_ADDRESS ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_COPY_STRING ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS &&
           instruction->reference < unit->block_binding_count &&
           unit->block_bindings[instruction->reference].storage ==
               CTOOL_C_STORAGE_STATIC)) {
        added_relocations = 1u;
      }
      if (cemit_add_overflows(
              text_relocation_count, added_relocations) == CTOOL_TRUE) {
        status = CTOOL_ERR_OVERFLOW;
        break;
      }
      text_relocation_count += added_relocations;
    }
    for (index = 0u; status == CTOOL_OK &&
                      index < context.ir.file_assembly_count;
         index++) {
      ctool_u32 assembly_index = context.ir.file_assemblies[index];
      cemit_file_assembly_kind_t kind;
      if (assembly_index >= unit->file_assembly_count) {
        status = CTOOL_ERR_INTERNAL;
        break;
      }
      kind = cemit_file_assembly_template_kind(
          unit->file_assemblies[assembly_index].template_text);
      if (kind == CEMIT_FILE_ASSEMBLY_FABS_MASKS ||
          kind == CEMIT_FILE_ASSEMBLY_EXP_LOG_CONSTANTS) {
        if (file_assembly_symbol_count > 0xfffffffdu) {
          status = CTOOL_ERR_OVERFLOW;
        } else {
          file_assembly_symbol_count += 2u;
        }
      } else if (cemit_file_assembly_has_text_relocation(kind) ==
                 CTOOL_TRUE) {
        if (text_relocation_count == 0xffffffffu) {
          status = CTOOL_ERR_OVERFLOW;
        } else {
          text_relocation_count++;
        }
      }
    }
  }
  if (status == CTOOL_OK &&
      cemit_add_overflows(unit->binding_count, unit->initializer_count) ==
          CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
  }
  if (status == CTOOL_OK &&
      cemit_add_overflows(unit->binding_count + unit->initializer_count,
                          unit->expression_count) == CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
  }
  if (status == CTOOL_OK &&
      cemit_add_overflows(unit->binding_count + unit->initializer_count +
                              unit->expression_count,
                          block_static_count) == CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
  }
  if (status == CTOOL_OK &&
      cemit_add_overflows(unit->binding_count + unit->initializer_count +
                              unit->expression_count +
                              block_static_count,
                          file_assembly_symbol_count) == CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
  }
  if (status == CTOOL_OK &&
      cemit_add_overflows(unit->initializer_count,
                          text_relocation_count) == CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
  }
  symbol_capacity = status == CTOOL_OK
                        ? unit->binding_count + unit->initializer_count +
                              unit->expression_count + block_static_count +
                              file_assembly_symbol_count
                        : 0u;
  context.symbol_capacity = symbol_capacity;
  context.relocation_capacity =
      status == CTOOL_OK
          ? unit->initializer_count + text_relocation_count
          : 0u;
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, symbol_capacity,
        (ctool_u32)sizeof(ctool_elf32_symbol_spec_t),
        (void **)&context.symbols);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, context.relocation_capacity,
        (ctool_u32)sizeof(ctool_elf32_relocation_spec_t),
        (void **)&context.relocations);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(&context, unit->binding_count,
                               (ctool_u32)sizeof(ctool_u32),
                               (void **)&context.binding_symbols);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(&context, unit->binding_count,
                               (ctool_u32)sizeof(ctool_u32),
                               (void **)&context.binding_object_definitions);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, unit->binding_count, (ctool_u32)sizeof(ctool_u32),
        (void **)&context.binding_function_definitions);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, context.ir.file_assembly_count,
        (ctool_u32)sizeof(ctool_u32),
        (void **)&context.file_assembly_bindings);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, context.ir.file_assembly_count,
        (ctool_u32)sizeof(ctool_u32),
        (void **)&context.file_assembly_callee_bindings);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, context.ir.file_assembly_count,
        (ctool_u32)sizeof(ctool_u32),
        (void **)&context.file_assembly_kinds);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(&context, unit->binding_count,
                               (ctool_u32)sizeof(ctool_bool),
                               (void **)&context.binding_needed);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, unit->binding_count,
        (ctool_u32)sizeof(*context.binding_sections),
        (void **)&context.binding_sections);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, unit->binding_count,
        (ctool_u32)sizeof(*context.named_sections),
        (void **)&context.named_sections);
    context.named_section_capacity = unit->binding_count;
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(&context, unit->initializer_count,
                               (ctool_u32)sizeof(ctool_bool),
                               (void **)&context.initializer_is_zero);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(&context, unit->block_binding_count,
                               (ctool_u32)sizeof(ctool_u32),
                               (void **)&context.block_binding_symbols);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(&context, unit->block_binding_count,
                               (ctool_u32)sizeof(ctool_u32),
                               (void **)&context.block_binding_offsets);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(&context, unit->expression_count,
                               (ctool_u32)sizeof(ctool_u32),
                               (void **)&context.compound_literal_offsets);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, unit->expression_count, (ctool_u32)sizeof(ctool_u32),
        (void **)&context.compound_literal_staging_offsets);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, context.ir.instruction_count,
        (ctool_u32)sizeof(ctool_u32),
        (void **)&context.value_temporary_offsets);
  }
  if (status == CTOOL_OK) {
    status = cemit_index_definitions(&context);
  }
  if (status == CTOOL_OK) {
    status = cemit_index_file_assemblies(&context);
  }
  if (status == CTOOL_OK) {
    status = cemit_index_initializers(&context);
  }
  if (status == CTOOL_OK) {
    status = cemit_index_file_assembly_symbols(&context);
  }
  if (status == CTOOL_OK) {
    status = cemit_index_symbols(&context);
  }
  if (status == CTOOL_OK) {
    status = cemit_index_block_static_symbols(&context);
  }
  if (status == CTOOL_OK) {
    status = cemit_open_buffers(&context);
  }
  if (status == CTOOL_OK) {
    status = cemit_index_named_sections(&context);
  }
  for (index = 0u; status == CTOOL_OK &&
                    index < context.ir.file_assembly_count;
       index++) {
    if (index == context.fabs_mask_assembly ||
        index == context.exp_log_constant_assembly) {
      status = cemit_place_file_assembly(&context, index);
    }
  }
  for (index = 0u; status == CTOOL_OK &&
                    index < unit->object_definition_count;
       index++) {
    status = cemit_place_definition(&context, index);
  }
  for (index = 0u; status == CTOOL_OK &&
                     index < unit->block_binding_count;
       index++) {
    status = cemit_place_block_static(&context, index);
  }
  for (index = 0u; status == CTOOL_OK &&
                    index < context.ir.file_assembly_count;
       index++) {
    if (index != context.fabs_mask_assembly &&
        index != context.exp_log_constant_assembly) {
      status = cemit_place_file_assembly(&context, index);
    }
  }
  for (index = 0u; status == CTOOL_OK &&
                    index < unit->function_definition_count;
       index++) {
    status = cemit_place_function(&context, index);
  }
  if (context.relation_status != CTOOL_OK) {
    status = context.relation_status;
  }
  if (status == CTOOL_OK) {
    status = cemit_build_sections(&context, sections, &section_count);
  }
  if (status == CTOOL_OK) {
    object.sections = sections;
    object.section_count = section_count;
    object.symbols = context.symbols;
    object.symbol_count = context.symbol_count;
    object.relocations = context.relocations;
    object.relocation_count = context.relocation_count;
    status = ctool_elf32_write(job, &object, context.object_output);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_append(output,
                                 ctool_buffer_view(context.object_output));
  }
  if (status != CTOOL_OK && context.failure_reported == CTOOL_FALSE &&
      ctool_job_diagnostic_count(job) == diagnostic_count) {
    if (status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_NO_MEMORY ||
        status == CTOOL_ERR_OVERFLOW) {
      status = cemit_emit_failure(
          &context, status, CTOOL_C_EMIT_DIAG_LIMIT,
          (const ctool_c_pp_location_t *)0,
          "CupidC object emission exceeded a configured resource limit");
    } else {
      status = cemit_emit_failure(
          &context, status, CTOOL_C_EMIT_DIAG_INTERNAL,
          (const ctool_c_pp_location_t *)0,
          "CupidC object emission failed before writing an object");
    }
  }
  for (index = 0u; index < context.named_section_count; index++) {
    ctool_buffer_close(context.named_sections[index].contents);
  }
  ctool_buffer_close(context.object_output);
  ctool_buffer_close(context.data);
  ctool_buffer_close(context.rodata);
  ctool_buffer_close(context.text);
  rewind_status = ctool_arena_rewind(context.arena, mark);
  if (status == CTOOL_OK && rewind_status != CTOOL_OK) {
    status = rewind_status;
  }
  return status;
}
