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

typedef struct {
  ctool_job_t *job;
  const ctool_c_translation_unit_t *unit;
  ctool_c_ir_unit_t ir;
  ctool_arena_t *arena;
  ctool_buffer_t *text;
  ctool_buffer_t *rodata;
  ctool_buffer_t *data;
  ctool_buffer_t *object_output;
  ctool_u32 bss_size;
  ctool_u32 section_alignment[CEMIT_SECTION_COUNT];
  ctool_elf32_symbol_spec_t *symbols;
  ctool_u32 symbol_count;
  ctool_u32 symbol_capacity;
  ctool_elf32_relocation_spec_t *relocations;
  ctool_u32 relocation_count;
  ctool_u32 relocation_capacity;
  ctool_u32 *binding_symbols;
  ctool_u32 *binding_object_definitions;
  ctool_u32 *binding_function_definitions;
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

static ctool_status_t cemit_x86_branch(
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

static ctool_status_t cemit_validate_unit_shape(cemit_context_t *context) {
  const ctool_c_translation_unit_t *unit = context->unit;
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
      (unit->function_definition_count != 0u &&
       unit->function_definitions ==
           (const ctool_c_function_definition_t *)0)) {
    return cemit_invalid_unit(context,
                              (const ctool_c_pp_location_t *)0);
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
        function->first_instruction > context->ir.instruction_count ||
        function->instruction_count >
            context->ir.instruction_count - function->first_instruction) {
      return cemit_invalid_unit(context, &definition->location);
    }
    binding = &context->unit->bindings[definition->binding];
    if (binding->kind != CTOOL_C_BINDING_FUNCTION) {
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
        if (parent_type->record_kind != CTOOL_C_RECORD_STRUCT) {
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
  symbol->binding = binding->linkage == CTOOL_C_LINKAGE_INTERNAL
                        ? CTOOL_ELF32_BIND_LOCAL
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
  return section == CEMIT_SECTION_DATA ? context->data
                                       : (ctool_buffer_t *)0;
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
                           section == CEMIT_SECTION_TEXT ? 0x90u : 0u,
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
               type->record_kind == CTOOL_C_RECORD_STRUCT) {
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
    ctool_u32 alignment, ctool_u32 symbol_index,
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
  if (cemit_type_is_const(context, type) == CTOOL_TRUE) {
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
  if (context->section_alignment[section] < alignment) {
    context->section_alignment[section] = alignment;
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
      symbol_index, &definition->location);
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
      &binding->location);
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
  ctool_u32 offset = ctool_buffer_view(context->text).size;
  ctool_status_t status = ctool_x86_encode(
      context->job, CTOOL_X86_MODE_32, instruction, CTOOL_X86_FORM_AUTO,
      &encoding);
  if (status != CTOOL_OK) {
    return status;
  }
  status = ctool_buffer_append(
      context->text, ctool_bytes(encoding.bytes, encoding.size));
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

static ctool_status_t cemit_x86_add_eax_constant(
    cemit_context_t *context, ctool_u32 value) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_ADD, 32u);
  if (value == 0u) {
    return CTOOL_OK;
  }
  instruction.operand_count = 2u;
  instruction.operands[0] =
      cemit_x86_register_operand(CTOOL_X86_REG_GPR32, 0u);
  instruction.operands[1] = cemit_x86_value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, value);
  return cemit_x86_encode(context, &instruction,
                          (ctool_x86_encoding_t *)0,
                          (ctool_u32 *)0);
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

static ctool_status_t cemit_x86_load_register_at_register(
    cemit_context_t *context, ctool_u8 destination_register,
    ctool_u8 address_register, ctool_u32 byte_offset) {
  ctool_x86_instruction_t instruction =
      cemit_x86_instruction(CTOOL_X86_MN_MOV, 32u);
  if (byte_offset > 0x7fffffffu) {
    return CTOOL_ERR_OVERFLOW;
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

  repeat_target = ctool_buffer_view(context->text).size;
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

  subtract_target = ctool_buffer_view(context->text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->text, overflow_patch, overflow_after, subtract_target);
  }
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->text, greater_patch, greater_after, subtract_target);
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

  continue_target = ctool_buffer_view(context->text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->text, high_less_patch, high_less_after,
        continue_target);
  }
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->text, low_less_patch, low_less_after,
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
        context->text, repeat_patch, repeat_after, repeat_target);
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
  repeat_target = ctool_buffer_view(context->text).size;
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
        context->text, repeat_patch, repeat_after, repeat_target);
  }
  done_target = ctool_buffer_view(context->text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->text, done_patch, done_after, done_target);
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
    ctool_u32 frame_size, ctool_u32 stack_depth,
    ctool_u32 reserved_bytes, ctool_u32 *padding_out) {
  ctool_u32 stack_bytes;
  ctool_u32 residue;
  if (padding_out == (ctool_u32 *)0 || (frame_size & 3u) != 0u ||
      (reserved_bytes & 3u) != 0u ||
      cemit_multiply_overflows(stack_depth, 4u) == CTOOL_TRUE) {
    return CTOOL_ERR_INTERNAL;
  }
  stack_bytes = stack_depth * 4u;
  /* A conforming call enters at residue 12. PUSH EBP leaves residue 8. */
  residue = (8u + 16u - (frame_size & 15u) -
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
      context, CEMIT_SECTION_TEXT, relocation_offset, symbol,
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
  return cemit_add_relocation(context, CEMIT_SECTION_TEXT,
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

static ctool_bool cemit_ir_type_is_i32_pointer_value(
    const cemit_context_t *context, ctool_u32 type) {
  return cemit_ir_type_is_i32_pointer(context, type) == CTOOL_TRUE ||
                 cemit_ir_type_is_i32_function_pointer(context, type) ==
                     CTOOL_TRUE
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

static ctool_bool cemit_ir_float_promotion_is_valid(
    const cemit_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type) {
  const ctool_c_type_node_t *source =
      cemit_unwrapped_type(context, source_type);
  const ctool_c_type_node_t *target =
      cemit_unwrapped_type(context, target_type);
  return source != (const ctool_c_type_node_t *)0 &&
                 target != (const ctool_c_type_node_t *)0 &&
                 source->kind == CTOOL_C_TYPE_FLOAT &&
                 target->kind == CTOOL_C_TYPE_DOUBLE &&
                 cemit_ir_type_is_floating_value(context, source_type) ==
                     CTOOL_TRUE &&
                 cemit_ir_type_is_floating_value(context, target_type) ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cemit_ir_same_floating_conversion_is_valid(
    const cemit_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type, ctool_c_conversion_kind_t conversion) {
  const ctool_c_type_node_t *source =
      cemit_unwrapped_type(context, source_type);
  const ctool_c_type_node_t *target =
      cemit_unwrapped_type(context, target_type);
  return source != (const ctool_c_type_node_t *)0 &&
                 target != (const ctool_c_type_node_t *)0 &&
                 source->kind == target->kind &&
                 (source->kind == CTOOL_C_TYPE_FLOAT ||
                  source->kind == CTOOL_C_TYPE_DOUBLE) &&
                 cemit_ir_type_is_floating_value(context, source_type) ==
                     CTOOL_TRUE &&
                 cemit_ir_type_is_floating_value(context, target_type) ==
                     CTOOL_TRUE &&
                 conversion == CTOOL_C_CONVERSION_USUAL_ARITHMETIC
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
  equal_target = ctool_buffer_view(context->text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->text, equal_patch, equal_after, equal_target);
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
  done_target = ctool_buffer_view(context->text).size;
  if (status == CTOOL_OK) {
    status = cemit_patch_branch(
        context->text, done_patch, done_after, done_target);
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
                   node->record_kind == CTOOL_C_RECORD_STRUCT &&
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
    ctool_u32 frame_size, ctool_u32 stack_depth) {
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
  status = cemit_call_stack_padding(frame_size, stack_depth,
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
    ctool_u32 temporary_offset, ctool_u32 frame_size,
    ctool_u32 stack_depth) {
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
        temporary_offset, frame_size, stack_depth);
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
  status = cemit_call_stack_padding(frame_size, stack_depth, 0u,
                                    &padding);
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
    ctool_u32 temporary_offset, ctool_u32 frame_size,
    ctool_u32 stack_depth) {
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
        CTOOL_C_AST_NONE, temporary_offset, frame_size,
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
  argument_bytes = instruction->argument_count * 4u;
  if (status == CTOOL_OK) {
    status = cemit_call_stack_padding(frame_size, stack_depth, 0u,
                                      &padding);
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

static ctool_status_t cemit_emit_ir_instruction(
    cemit_context_t *context,
    const ctool_c_ir_instruction_t *ir_instruction,
    const ctool_c_type_node_t *function_type,
    const ctool_u32 *block_binding_offsets, ctool_u32 ir_offset,
    ctool_u32 value_temporary_offset,
    ctool_u32 frame_size, ctool_u32 stack_depth,
    ctool_u32 *branch_patches, ctool_u32 *branch_afters) {
  ctool_status_t status;
  if (ir_instruction->kind != CTOOL_C_IR_INSTRUCTION_CALL_DIRECT &&
      ir_instruction->kind != CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT &&
      ir_instruction->argument_count != 0u) {
    return CTOOL_ERR_INTERNAL;
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
    ctool_bool floating_promotion =
        ir_instruction->conversion ==
                    CTOOL_C_CONVERSION_FLOAT_PROMOTION &&
                cemit_ir_float_promotion_is_valid(
                    context, ir_instruction->input_type,
                    ir_instruction->type) == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool same_floating_conversion =
        cemit_ir_same_floating_conversion_is_valid(
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
        cemit_ir_type_is_i32_integer(context,
                                     ir_instruction->input_type) ==
                CTOOL_TRUE &&
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
                      context, ir_instruction->type) == CTOOL_TRUE)) &&
                cemit_ir_type_is_i32_function_pointer(
                    context, ir_instruction->input_type) == CTOOL_FALSE &&
                cemit_ir_type_is_i32_function_pointer(
                    context, ir_instruction->type) == CTOOL_FALSE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if ((integer_conversion == CTOOL_FALSE &&
         floating_promotion == CTOOL_FALSE &&
         same_floating_conversion == CTOOL_FALSE &&
         pointer_conversion == CTOOL_FALSE &&
         null_conversion == CTOOL_FALSE &&
         explicit_scalar_conversion == CTOOL_FALSE) ||
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
             CTOOL_C_CONVERSION_FLOAT_PROMOTION) ||
        (pointer_conversion == CTOOL_TRUE &&
         explicit_scalar_conversion == CTOOL_FALSE &&
         ir_instruction->conversion != CTOOL_C_CONVERSION_QUALIFICATION &&
         ir_instruction->conversion != CTOOL_C_CONVERSION_POINTER) ||
        (null_conversion == CTOOL_TRUE &&
         ir_instruction->conversion != CTOOL_C_CONVERSION_NULL_POINTER) ||
        (bit_field_promotion == CTOOL_FALSE &&
         ir_instruction->reference != CTOOL_C_AST_NONE) ||
        ir_instruction->integer_bits != 0u) {
      return CTOOL_ERR_INTERNAL;
    }
    if (floating_promotion == CTOOL_TRUE) {
      status = cemit_x86_x87_memory(
          context, CTOOL_X86_MN_FLD, 4u, 0, 32u);
      if (status == CTOOL_OK) {
        status = cemit_x86_discard_arguments(context, 4u);
      }
      return status == CTOOL_OK
                 ? cemit_x86_push_floating_result(
                       context, ir_instruction->type,
                       value_temporary_offset)
                 : status;
    }
    if (same_floating_conversion == CTOOL_TRUE) {
      return CTOOL_OK;
    }
    if (source_wide == CTOOL_FALSE && target_wide == CTOOL_TRUE) {
      const ctool_c_type_layout_t *source_layout =
          &context->unit->layout.types[ir_instruction->input_type];
      return cemit_x86_push_widened_integer_snapshot(
          context, value_temporary_offset, source_layout->is_signed);
    }
    if (source_wide == CTOOL_TRUE && target_wide == CTOOL_FALSE) {
      const ctool_c_type_node_t *target =
          cemit_unwrapped_type(context, ir_instruction->type);
      if (target == (const ctool_c_type_node_t *)0 ||
          cemit_ir_type_is_represented_integer(
              context, ir_instruction->type) == CTOOL_FALSE) {
        return CTOOL_ERR_INTERNAL;
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
    ctool_bool wide_comparison =
        cemit_ir_type_is_wide_integer(
            context, ir_instruction->input_type) == CTOOL_TRUE &&
                cemit_ir_type_is_plain_signed_int(
                    context, ir_instruction->type) == CTOOL_TRUE &&
                (relational_comparison == CTOOL_TRUE ||
                 ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_EQUAL ||
                 ir_instruction->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL)
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
    ctool_u8 result_register = 0u;
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
        frame_size, stack_depth);
  }
  if (ir_instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT) {
    return cemit_emit_indirect_call(
        context, ir_instruction, value_temporary_offset,
        frame_size, stack_depth);
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
        ((instruction->kind == CTOOL_C_IR_INSTRUCTION_LOAD ||
          instruction->kind ==
              CTOOL_C_IR_INSTRUCTION_VARIADIC_ARGUMENT ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_BINARY ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_UNARY &&
           instruction->operation ==
               CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE) ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_CONVERT &&
           instruction->conversion ==
               CTOOL_C_CONVERSION_FLOAT_PROMOTION)) &&
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
  ctool_u32 function_start;
  ctool_u32 function_size;
  ctool_u32 frame_size;
  ctool_u32 symbol_index = CTOOL_C_AST_NONE;
  ctool_u32 *instruction_offsets = (ctool_u32 *)0;
  ctool_u32 *branch_patches = (ctool_u32 *)0;
  ctool_u32 *branch_afters = (ctool_u32 *)0;
  ctool_u32 *stack_depths = (ctool_u32 *)0;
  ctool_u32 index;
  ctool_status_t status;
  if (function_type == (const ctool_c_type_node_t *)0 ||
      function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      cemit_power_of_two(alignment) == CTOOL_FALSE ||
      function->instruction_count == 0u ||
      function->first_instruction > context->ir.instruction_count ||
      function->instruction_count >
          context->ir.instruction_count - function->first_instruction ||
      function->instruction_count == 0xffffffffu) {
    return cemit_invalid_unit(context, &definition->location);
  }
  status = cemit_prepare_local_offsets(context, function, &frame_size);
  if (status == CTOOL_OK) {
    status = cemit_analyze_stack_depths(
        context, function, &stack_depths);
  }
  if (status == CTOOL_OK) {
    status = cemit_align_buffer(context, CEMIT_SECTION_TEXT, alignment);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  function_start = ctool_buffer_view(context->text).size;
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
  for (index = 0u; status == CTOOL_OK &&
                    index < function->instruction_count;
       index++) {
    const ctool_c_ir_instruction_t *instruction =
        &context->ir.instructions[function->first_instruction + index];
    instruction_offsets[index] =
        ctool_buffer_view(context->text).size - function_start;
    status = cemit_emit_ir_instruction(
        context, instruction, function_type, context->block_binding_offsets,
        index,
        context->value_temporary_offsets
            [function->first_instruction + index],
        frame_size, stack_depths[index],
        branch_patches, branch_afters);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  instruction_offsets[function->instruction_count] =
      ctool_buffer_view(context->text).size - function_start;
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
      status = cemit_patch_branch(context->text, branch_patches[index],
                                  branch_afters[index], target);
      if (status != CTOOL_OK) {
        return status;
      }
    }
  }
  function_size = ctool_buffer_view(context->text).size - function_start;
  if (function_size == 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  if (context->section_alignment[CEMIT_SECTION_TEXT] < alignment) {
    context->section_alignment[CEMIT_SECTION_TEXT] = alignment;
  }
  status = cemit_ensure_binding_symbol(context, definition->binding,
                                       &symbol_index);
  if (status != CTOOL_OK) {
    return status;
  }
  context->symbols[symbol_index].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  context->symbols[symbol_index].section = CEMIT_SECTION_TEXT;
  context->symbols[symbol_index].value = function_start;
  context->symbols[symbol_index].size = function_size;
  context->symbols[symbol_index].alignment = 0u;
  return CTOOL_OK;
}

static ctool_status_t cemit_build_sections(
    cemit_context_t *context, ctool_elf32_section_spec_t *sections,
    ctool_u32 *section_count_out) {
  ctool_u32 section_map[CEMIT_SECTION_COUNT];
  ctool_u32 section_count = 0u;
  ctool_u32 logical;
  ctool_u32 index;
  for (logical = 0u; logical < CEMIT_SECTION_COUNT; logical++) {
    ctool_u32 size = logical == CEMIT_SECTION_BSS
                         ? context->bss_size
                         : ctool_buffer_view(
                               cemit_section_buffer(context, logical))
                               .size;
    section_map[logical] = CTOOL_ELF32_NO_SECTION;
    if (size != 0u) {
      ctool_elf32_section_spec_t *section = &sections[section_count];
      section_map[logical] = section_count++;
      section->name = logical == CEMIT_SECTION_TEXT
                          ? ctool_string(".text")
                          : logical == CEMIT_SECTION_RODATA
                                ? ctool_string(".rodata")
                                : logical == CEMIT_SECTION_DATA
                                      ? ctool_string(".data")
                                      : ctool_string(".bss");
      section->type = logical == CEMIT_SECTION_BSS
                          ? CTOOL_ELF32_SHT_NOBITS
                          : CTOOL_ELF32_SHT_PROGBITS;
      section->flags = logical == CEMIT_SECTION_TEXT
                           ? CTOOL_ELF32_SHF_ALLOC |
                                 CTOOL_ELF32_SHF_EXECINSTR
                           : logical == CEMIT_SECTION_RODATA
                                 ? CTOOL_ELF32_SHF_ALLOC
                                 : CTOOL_ELF32_SHF_ALLOC |
                                       CTOOL_ELF32_SHF_WRITE;
      section->alignment = context->section_alignment[logical];
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
      if (symbol->section >= CEMIT_SECTION_COUNT ||
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
    if (relocation->target_section >= CEMIT_SECTION_COUNT ||
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
  return status;
}

ctool_status_t ctool_c_emit_object(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_buffer_t *output) {
  cemit_context_t context;
  ctool_arena_mark_t mark;
  ctool_elf32_section_spec_t sections[CEMIT_SECTION_COUNT];
  ctool_elf32_object_spec_t object;
  ctool_u32 text_relocation_count = 0u;
  ctool_u32 block_static_count = 0u;
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
  cemit_zero(sections, (ctool_u32)sizeof(sections));
  cemit_zero(&object, (ctool_u32)sizeof(object));
  context.job = job;
  context.unit = unit;
  context.arena = ctool_job_arena(job);
  mark = ctool_arena_mark(context.arena);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = cemit_validate_unit_shape(&context);
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
      if (cemit_has_binding_text_relocation(instruction->kind) ==
              CTOOL_TRUE ||
          instruction->kind ==
              CTOOL_C_IR_INSTRUCTION_STRING_LITERAL_ADDRESS ||
          instruction->kind == CTOOL_C_IR_INSTRUCTION_COPY_STRING ||
          (instruction->kind == CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS &&
           instruction->reference < unit->block_binding_count &&
           unit->block_bindings[instruction->reference].storage ==
               CTOOL_C_STORAGE_STATIC)) {
        text_relocation_count++;
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
      cemit_add_overflows(unit->initializer_count,
                          text_relocation_count) == CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
  }
  symbol_capacity = status == CTOOL_OK
                        ? unit->binding_count + unit->initializer_count +
                              unit->expression_count + block_static_count
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
    status = cemit_alloc_array(&context, unit->binding_count,
                               (ctool_u32)sizeof(ctool_bool),
                               (void **)&context.binding_needed);
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
    status = cemit_index_initializers(&context);
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
