#include "cupidc_ir.h"

#define CIR_STACK_LIMIT (CTOOL_C_PARSE_NESTING_LIMIT + 1u)
#define CIR_CONTROL_PATCH_BREAK 1u
#define CIR_CONTROL_PATCH_CONTINUE 2u
#define CIR_SWITCH_EXIT_PATCH 3u
#define CIR_SWITCH_CASE_PATCH_TAG 0x4000000000000000ull
#define CIR_GOTO_PATCH_TAG 0x8000000000000000ull

typedef enum {
  CIR_STACK_VALUE = 1,
  CIR_STACK_ADDRESS,
  CIR_STACK_FUNCTION_DESIGNATOR
} cir_stack_kind_t;

typedef struct {
  cir_stack_kind_t kind;
  ctool_u32 type;
} cir_stack_entry_t;

typedef enum {
  CIR_INITIALIZER_ARRAY_ELEMENT = 1,
  CIR_INITIALIZER_RECORD_MEMBER
} cir_initializer_path_kind_t;

typedef struct {
  cir_initializer_path_kind_t kind;
  ctool_u32 parent_type;
  ctool_u32 child_type;
  ctool_u32 subobject;
} cir_initializer_path_t;

typedef struct {
  ctool_c_ir_instruction_kind_t address_kind;
  ctool_u32 reference;
  ctool_u32 type;
  const ctool_c_pp_location_t *location;
  const ctool_c_pp_location_t *physical_location;
} cir_initializer_object_t;

typedef struct {
  ctool_u32 left;
  ctool_u32 right;
  ctool_u32 left_carried_qualifiers;
  ctool_u32 right_carried_qualifiers;
  ctool_bool parameter_context;
} cir_type_pair_t;

typedef struct {
  cir_type_pair_t *items;
  ctool_u32 count;
  ctool_u32 capacity;
} cir_type_pair_vector_t;

typedef enum {
  CIR_CONTROL_LOOP = 1,
  CIR_CONTROL_SWITCH
} cir_control_kind_t;

typedef struct {
  cir_control_kind_t kind;
  ctool_u32 first_instruction;
  ctool_u32 continue_target;
  ctool_u32 condition_type;
  ctool_bool has_break;
  ctool_bool has_continue;
  ctool_bool switch_entry_reachable;
} cir_control_frame_t;

typedef struct {
  cir_control_kind_t kind;
  ctool_bool has_break;
  ctool_bool has_continue;
  ctool_bool has_default;
  ctool_bool switch_entry_reachable;
} cir_reach_control_frame_t;

typedef struct {
  ctool_job_t *job;
  const ctool_c_translation_unit_t *unit;
  ctool_arena_t *arena;
  ctool_c_ir_function_t *functions;
  ctool_c_ir_instruction_t *instructions;
  ctool_u32 instruction_count;
  ctool_u32 instruction_capacity;
  ctool_u32 *argument_types;
  ctool_u32 argument_type_count;
  ctool_u32 argument_type_capacity;
  ctool_u32 function_first_instruction;
  ctool_u32 function_first_parameter;
  ctool_u32 function_parameter_count;
  ctool_u32 function_first_block_binding;
  ctool_u32 function_block_binding_count;
  ctool_u32 visible_block_binding_end;
  ctool_u32 block_binding_cursor;
  ctool_u32 function_first_label;
  ctool_u32 function_label_count;
  ctool_u32 label_cursor;
  ctool_u32 statement_cursor;
  ctool_u32 *label_targets;
  ctool_bool *label_reachable;
  ctool_bool function_reachable_fallthrough;
  ctool_bool function_has_patched_target;
  ctool_u32 function_maximum_patched_target;
  ctool_bool count_only_validation;
  ctool_u32 function_result_type;
  ctool_bool function_variadic;
  cir_stack_entry_t stack[CIR_STACK_LIMIT];
  ctool_u32 stack_depth;
  ctool_u32 maximum_stack_depth;
  cir_initializer_path_t
      initializer_path[CTOOL_C_PARSE_NESTING_LIMIT];
  cir_control_frame_t control_frames[CTOOL_C_PARSE_NESTING_LIMIT];
  ctool_u32 control_depth;
  cir_reach_control_frame_t
      reach_control_frames[CTOOL_C_PARSE_NESTING_LIMIT];
  ctool_u32 reach_control_depth;
  ctool_bool failure_reported;
  ctool_status_t relation_status;
} cir_context_t;

ctool_status_t ctool_c_ir_validate_call_slices(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_ir_unit_t *ir, ctool_bool *valid_out) {
  ctool_u32 cursor = 0u;
  ctool_u32 instruction_index;
  if (unit == (const ctool_c_translation_unit_t *)0 ||
      ir == (const ctool_c_ir_unit_t *)0 ||
      valid_out == (ctool_bool *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *valid_out = CTOOL_FALSE;
  if ((unit->graph.type_count == 0u) !=
          (unit->graph.types == (const ctool_c_type_node_t *)0) ||
      (ir->instruction_count == 0u) !=
          (ir->instructions ==
           (const ctool_c_ir_instruction_t *)0) ||
      (ir->argument_type_count == 0u) !=
          (ir->argument_types == (const ctool_u32 *)0)) {
    return CTOOL_OK;
  }
  for (instruction_index = 0u;
       instruction_index < ir->instruction_count;
       instruction_index++) {
    const ctool_c_ir_instruction_t *instruction =
        &ir->instructions[instruction_index];
    ctool_bool call =
        instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
                instruction->kind == CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if (call == CTOOL_FALSE) {
      if (instruction->argument_count != 0u ||
          instruction->first_argument_type != CTOOL_C_AST_NONE) {
        return CTOOL_OK;
      }
      continue;
    }
    if (cursor > ir->argument_type_count ||
        instruction->first_argument_type != cursor ||
        instruction->argument_count > ir->argument_type_count - cursor) {
      return CTOOL_OK;
    }
    while (cursor < instruction->first_argument_type +
                        instruction->argument_count) {
      if (ir->argument_types[cursor] >= unit->graph.type_count) {
        return CTOOL_OK;
      }
      cursor++;
    }
  }
  *valid_out = cursor == ir->argument_type_count ? CTOOL_TRUE
                                                 : CTOOL_FALSE;
  return CTOOL_OK;
}

static ctool_status_t cir_alloc_array(cir_context_t *context,
                                      ctool_u32 count,
                                      ctool_u32 element_size,
                                      void **array_out);

static void cir_zero(void *destination, ctool_u32 size) {
  ctool_u8 *bytes = (ctool_u8 *)destination;
  ctool_u32 index;
  if (destination == (void *)0) {
    return;
  }
  for (index = 0u; index < size; index++) {
    bytes[index] = 0u;
  }
}

static void cir_zero_result(ctool_c_ir_unit_t *result) {
  if (result != (ctool_c_ir_unit_t *)0) {
    cir_zero(result, (ctool_u32)sizeof(*result));
  }
}

static ctool_bool cir_string_equal(ctool_string_t left,
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

static ctool_bool cir_add_overflows(ctool_u32 left, ctool_u32 right) {
  return left > 0xffffffffu - right ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool cir_multiply_overflows(ctool_u32 left,
                                         ctool_u32 right) {
  return left != 0u && right > 0xffffffffu / left ? CTOOL_TRUE
                                                  : CTOOL_FALSE;
}

static ctool_status_t cir_emit_failure(
    cir_context_t *context, ctool_status_t status, ctool_u32 code,
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

static ctool_status_t cir_invalid_unit(
    cir_context_t *context, const ctool_c_pp_location_t *location) {
  if (context->relation_status != CTOOL_OK) {
    return context->relation_status;
  }
  return cir_emit_failure(
      context, CTOOL_ERR_INPUT, CTOOL_C_IR_DIAG_INVALID_UNIT, location,
      "CupidC IR lowering received an invalid translation unit");
}

static ctool_status_t cir_unsupported_statement(
    cir_context_t *context, const ctool_c_pp_location_t *location) {
  if (context->relation_status != CTOOL_OK) {
    return context->relation_status;
  }
  return cir_emit_failure(
      context, CTOOL_ERR_UNSUPPORTED,
      CTOOL_C_IR_DIAG_UNSUPPORTED_STATEMENT, location,
      "CupidC IR lowering does not yet support this statement");
}

static ctool_status_t cir_unsupported_expression(
    cir_context_t *context, const ctool_c_pp_location_t *location) {
  if (context->relation_status != CTOOL_OK) {
    return context->relation_status;
  }
  return cir_emit_failure(
      context, CTOOL_ERR_UNSUPPORTED,
      CTOOL_C_IR_DIAG_UNSUPPORTED_EXPRESSION, location,
      "CupidC IR lowering does not yet support this expression");
}

static ctool_status_t cir_unsupported_conversion(
    cir_context_t *context, const ctool_c_pp_location_t *location) {
  if (context->relation_status != CTOOL_OK) {
    return context->relation_status;
  }
  return cir_emit_failure(
      context, CTOOL_ERR_UNSUPPORTED,
      CTOOL_C_IR_DIAG_UNSUPPORTED_CONVERSION, location,
      "CupidC IR lowering does not yet support this conversion");
}

static ctool_status_t cir_unsupported_type(
    cir_context_t *context, const ctool_c_pp_location_t *location) {
  if (context->relation_status != CTOOL_OK) {
    return context->relation_status;
  }
  return cir_emit_failure(
      context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
      location, "CupidC IR lowering does not yet support this value type");
}

static ctool_status_t cir_unsupported_initializer_leaf(
    cir_context_t *context, const ctool_c_pp_location_t *location,
    const char *message) {
  if (context->relation_status != CTOOL_OK) {
    return context->relation_status;
  }
  return cir_emit_failure(
      context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
      location, message);
}

static ctool_status_t cir_validate_unit_shape(cir_context_t *context) {
  const ctool_c_translation_unit_t *unit = context->unit;
  if ((unit->graph.type_count != 0u &&
       unit->graph.types == (const ctool_c_type_node_t *)0) ||
      (unit->graph.member_count != 0u &&
       unit->graph.members == (const ctool_c_record_member_t *)0) ||
      (unit->graph.parameter_type_count != 0u &&
       unit->graph.parameter_types == (const ctool_u32 *)0) ||
      unit->layout.type_count != unit->graph.type_count ||
      unit->layout.member_count != unit->graph.member_count ||
      (unit->layout.type_count != 0u &&
       unit->layout.types == (const ctool_c_type_layout_t *)0) ||
      (unit->layout.member_count != 0u &&
       unit->layout.members == (const ctool_c_member_layout_t *)0) ||
      (unit->binding_count != 0u &&
       unit->bindings == (const ctool_c_binding_t *)0) ||
      (unit->object_definition_count != 0u &&
       unit->object_definitions ==
           (const ctool_c_object_definition_t *)0) ||
      (unit->block_binding_count != 0u &&
       unit->block_bindings == (const ctool_c_block_binding_t *)0) ||
      (unit->initializer_count != 0u &&
       unit->initializers == (const ctool_c_initializer_t *)0) ||
      (unit->initializer_element_count != 0u &&
       unit->initializer_elements ==
           (const ctool_c_initializer_element_t *)0) ||
      (unit->label_count != 0u &&
       unit->labels == (const ctool_c_label_t *)0) ||
      unit->parameter_count != unit->graph.parameter_type_count ||
      (unit->parameter_count != 0u &&
       unit->parameters == (const ctool_c_parameter_t *)0) ||
      (unit->function_definition_count != 0u &&
       unit->function_definitions ==
           (const ctool_c_function_definition_t *)0) ||
      (unit->statement_count != 0u &&
       unit->statements == (const ctool_c_statement_t *)0) ||
      (unit->statement_child_count != 0u &&
       unit->statement_children == (const ctool_u32 *)0) ||
      (unit->expression_count != 0u &&
       unit->expressions == (const ctool_c_expression_t *)0) ||
      (unit->expression_child_count != 0u &&
       unit->expression_children == (const ctool_u32 *)0)) {
    return cir_invalid_unit(context, (const ctool_c_pp_location_t *)0);
  }
  return CTOOL_OK;
}

static const ctool_c_type_node_t *cir_type_node(const cir_context_t *context,
                                                ctool_u32 type) {
  return type < context->unit->graph.type_count
             ? &context->unit->graph.types[type]
             : (const ctool_c_type_node_t *)0;
}

static const ctool_c_type_node_t *cir_unwrapped_type(
    const cir_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cir_type_node(context, type);
  ctool_u32 traversed = 0u;
  while (node != (const ctool_c_type_node_t *)0 &&
         (node->kind == CTOOL_C_TYPE_ALIGNED ||
          node->kind == CTOOL_C_TYPE_QUALIFIED)) {
    if (traversed++ >= context->unit->graph.type_count) {
      return (const ctool_c_type_node_t *)0;
    }
    node = cir_type_node(context, node->referenced_type);
  }
  return node;
}

static ctool_bool cir_underlying_type(
    const cir_context_t *context, ctool_u32 type, ctool_u32 *base_out,
    ctool_u32 *qualifiers_out, const ctool_c_type_node_t **node_out) {
  ctool_u32 qualifiers = 0u;
  ctool_u32 traversed = 0u;
  const ctool_c_type_node_t *node;
  for (;;) {
    node = cir_type_node(context, type);
    if (node == (const ctool_c_type_node_t *)0) {
      return CTOOL_FALSE;
    }
    if (node->kind != CTOOL_C_TYPE_ALIGNED &&
        node->kind != CTOOL_C_TYPE_QUALIFIED) {
      *base_out = type;
      *qualifiers_out = qualifiers;
      *node_out = node;
      return CTOOL_TRUE;
    }
    qualifiers |= node->qualifiers;
    type = node->referenced_type;
    if (traversed++ >= context->unit->graph.type_count) {
      return CTOOL_FALSE;
    }
  }
}

static ctool_bool cir_type_has_atomic_qualification(
    const cir_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cir_type_node(context, type);
  ctool_u32 traversed = 0u;
  while (node != (const ctool_c_type_node_t *)0) {
    if ((node->qualifiers & CTOOL_C_QUAL_ATOMIC) != 0u) {
      return CTOOL_TRUE;
    }
    if (node->kind != CTOOL_C_TYPE_ALIGNED &&
        node->kind != CTOOL_C_TYPE_QUALIFIED) {
      break;
    }
    if (traversed++ >= context->unit->graph.type_count) {
      break;
    }
    node = cir_type_node(context, node->referenced_type);
  }
  return CTOOL_FALSE;
}

static ctool_bool cir_type_has_volatile_qualification(
    const cir_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cir_type_node(context, type);
  ctool_u32 traversed = 0u;
  while (node != (const ctool_c_type_node_t *)0) {
    if ((node->qualifiers & CTOOL_C_QUAL_VOLATILE) != 0u) {
      return CTOOL_TRUE;
    }
    if (node->kind != CTOOL_C_TYPE_ALIGNED &&
        node->kind != CTOOL_C_TYPE_QUALIFIED) {
      break;
    }
    if (traversed++ >= context->unit->graph.type_count) {
      break;
    }
    node = cir_type_node(context, node->referenced_type);
  }
  return CTOOL_FALSE;
}

static ctool_bool cir_type_is_void(const cir_context_t *context,
                                   ctool_u32 type) {
  const ctool_c_type_node_t *node = cir_unwrapped_type(context, type);
  return node != (const ctool_c_type_node_t *)0 &&
                 node->kind == CTOOL_C_TYPE_VOID
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_is_i32_integer(const cir_context_t *context,
                                          ctool_u32 type) {
  if (type >= context->unit->layout.type_count) {
    return CTOOL_FALSE;
  }
  return context->unit->layout.types[type].is_integer == CTOOL_TRUE &&
                 context->unit->layout.types[type].size == 4u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_is_represented_integer(
    const cir_context_t *context, ctool_u32 type) {
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

static ctool_bool cir_type_is_wide_integer(const cir_context_t *context,
                                           ctool_u32 type) {
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

static ctool_bool cir_type_is_value_integer(const cir_context_t *context,
                                            ctool_u32 type) {
  return cir_type_is_represented_integer(context, type) == CTOOL_TRUE ||
                 cir_type_is_wide_integer(context, type) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_is_i32_pointer(const cir_context_t *context,
                                          ctool_u32 type) {
  const ctool_c_type_node_t *node = cir_unwrapped_type(context, type);
  return type < context->unit->layout.type_count &&
                 node != (const ctool_c_type_node_t *)0 &&
                 node->kind == CTOOL_C_TYPE_POINTER &&
                 node->referenced_type < context->unit->layout.type_count &&
                 (context->unit->layout.types[node->referenced_type]
                              .is_object == CTOOL_TRUE ||
                  cir_type_is_void(context, node->referenced_type) ==
                      CTOOL_TRUE) &&
                 context->unit->layout.types[type].is_object == CTOOL_TRUE &&
                 context->unit->layout.types[type].is_complete_object ==
                     CTOOL_TRUE &&
                 context->unit->layout.types[type].size == 4u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_is_i32_function_pointer(
    const cir_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cir_unwrapped_type(context, type);
  const ctool_c_type_node_t *referent =
      node != (const ctool_c_type_node_t *)0 &&
              node->kind == CTOOL_C_TYPE_POINTER
          ? cir_unwrapped_type(context, node->referenced_type)
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

static ctool_bool cir_type_is_i32_pointer_value(
    const cir_context_t *context, ctool_u32 type) {
  return cir_type_is_i32_pointer(context, type) == CTOOL_TRUE ||
                 cir_type_is_i32_function_pointer(context, type) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_is_i32_scalar(const cir_context_t *context,
                                         ctool_u32 type) {
  return cir_type_is_i32_integer(context, type) == CTOOL_TRUE ||
                 cir_type_is_i32_pointer_value(context, type) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_is_floating_value(
    const cir_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cir_unwrapped_type(context, type);
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

static ctool_bool cir_type_is_represented_scalar(
    const cir_context_t *context, ctool_u32 type) {
  return cir_type_is_represented_integer(context, type) == CTOOL_TRUE ||
                 cir_type_is_i32_pointer_value(context, type) == CTOOL_TRUE ||
                 (cir_type_is_floating_value(context, type) == CTOOL_TRUE &&
                  context->unit->layout.types[type].size == 4u)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_is_value_scalar(const cir_context_t *context,
                                           ctool_u32 type) {
  return cir_type_is_represented_scalar(context, type) == CTOOL_TRUE ||
                 cir_type_is_wide_integer(context, type) == CTOOL_TRUE ||
                 cir_type_is_floating_value(context, type) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_is_truth_scalar(const cir_context_t *context,
                                           ctool_u32 type) {
  return cir_type_is_value_integer(context, type) == CTOOL_TRUE ||
                 cir_type_is_i32_pointer_value(context, type) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_compatible_scalar_kind(ctool_c_type_kind_t kind) {
  return kind == CTOOL_C_TYPE_VOID || kind == CTOOL_C_TYPE_BOOL ||
                 kind == CTOOL_C_TYPE_CHAR ||
                 kind == CTOOL_C_TYPE_SIGNED_CHAR ||
                 kind == CTOOL_C_TYPE_UNSIGNED_CHAR ||
                 kind == CTOOL_C_TYPE_SIGNED_SHORT ||
                 kind == CTOOL_C_TYPE_UNSIGNED_SHORT ||
                 kind == CTOOL_C_TYPE_SIGNED_INT ||
                 kind == CTOOL_C_TYPE_UNSIGNED_INT ||
                 kind == CTOOL_C_TYPE_SIGNED_LONG ||
                 kind == CTOOL_C_TYPE_UNSIGNED_LONG ||
                 kind == CTOOL_C_TYPE_SIGNED_LONG_LONG ||
                 kind == CTOOL_C_TYPE_UNSIGNED_LONG_LONG ||
                 kind == CTOOL_C_TYPE_FLOAT ||
                 kind == CTOOL_C_TYPE_DOUBLE ||
                 kind == CTOOL_C_TYPE_LONG_DOUBLE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_pair_equal(const cir_type_pair_t *left,
                                      const cir_type_pair_t *right) {
  return left->left == right->left && left->right == right->right &&
                 left->left_carried_qualifiers ==
                     right->left_carried_qualifiers &&
                 left->right_carried_qualifiers ==
                     right->right_carried_qualifiers &&
                 left->parameter_context == right->parameter_context
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_pair_seen(const cir_type_pair_vector_t *seen,
                                     const cir_type_pair_t *pair) {
  ctool_u32 index;
  for (index = 0u; index < seen->count; index++) {
    if (cir_type_pair_equal(&seen->items[index], pair) == CTOOL_TRUE) {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_status_t cir_type_pair_append(
    cir_context_t *context, cir_type_pair_vector_t *vector,
    const cir_type_pair_t *pair) {
  if (vector->count == vector->capacity) {
    cir_type_pair_t *replacement;
    ctool_u32 capacity = vector->capacity == 0u ? 16u : vector->capacity;
    ctool_u32 index;
    ctool_status_t status;
    if (vector->capacity != 0u) {
      if (vector->capacity > 0x7fffffffu) {
        return CTOOL_ERR_OVERFLOW;
      }
      capacity = vector->capacity * 2u;
    }
    status = cir_alloc_array(context, capacity,
                             (ctool_u32)sizeof(*replacement),
                             (void **)&replacement);
    if (status != CTOOL_OK) {
      return status;
    }
    if (replacement == (cir_type_pair_t *)0) {
      return CTOOL_ERR_INTERNAL;
    }
    for (index = 0u; index < vector->count; index++) {
      replacement[index] = vector->items[index];
    }
    vector->items = replacement;
    vector->capacity = capacity;
  }
  vector->items[vector->count++] = *pair;
  return CTOOL_OK;
}

static ctool_status_t cir_compare_types(
    cir_context_t *context, ctool_u32 left, ctool_u32 right,
    ctool_bool *compatible_out) {
  cir_type_pair_vector_t work;
  cir_type_pair_vector_t seen;
  cir_type_pair_t initial;
  ctool_arena_mark_t mark = ctool_arena_mark(context->arena);
  ctool_status_t status;
  ctool_status_t rewind_status;
  ctool_bool compatible = CTOOL_TRUE;
  cir_zero(&work, (ctool_u32)sizeof(work));
  cir_zero(&seen, (ctool_u32)sizeof(seen));
  initial.left = left;
  initial.right = right;
  initial.left_carried_qualifiers = 0u;
  initial.right_carried_qualifiers = 0u;
  initial.parameter_context = CTOOL_FALSE;
  status = cir_type_pair_append(context, &work, &initial);
  while (status == CTOOL_OK && compatible == CTOOL_TRUE &&
         work.count != 0u) {
    cir_type_pair_t pair = work.items[--work.count];
    ctool_u32 traversed = 0u;
    if (cir_type_pair_seen(&seen, &pair) == CTOOL_TRUE) {
      continue;
    }
    status = cir_type_pair_append(context, &seen, &pair);
    while (status == CTOOL_OK && compatible == CTOOL_TRUE) {
      const ctool_c_type_node_t *left_node;
      const ctool_c_type_node_t *right_node;
      ctool_u32 left_base;
      ctool_u32 right_base;
      ctool_u32 left_qualifiers;
      ctool_u32 right_qualifiers;
      if (traversed++ > context->unit->graph.type_count ||
          cir_underlying_type(context, pair.left, &left_base,
                              &left_qualifiers, &left_node) == CTOOL_FALSE ||
          cir_underlying_type(context, pair.right, &right_base,
                              &right_qualifiers,
                              &right_node) == CTOOL_FALSE) {
        compatible = CTOOL_FALSE;
        break;
      }
      left_qualifiers |=
          pair.left_carried_qualifiers | left_node->qualifiers;
      right_qualifiers |=
          pair.right_carried_qualifiers | right_node->qualifiers;
      pair.left_carried_qualifiers = 0u;
      pair.right_carried_qualifiers = 0u;
      if (pair.parameter_context == CTOOL_TRUE) {
        left_qualifiers &= CTOOL_C_QUAL_ATOMIC;
        right_qualifiers &= CTOOL_C_QUAL_ATOMIC;
        pair.parameter_context = CTOOL_FALSE;
      }
      if (left_node->kind == CTOOL_C_TYPE_ARRAY &&
          right_node->kind == CTOOL_C_TYPE_ARRAY) {
        if (left_base == right_base &&
            left_qualifiers == right_qualifiers) {
          break;
        }
        if (left_node->array_bound_kind == CTOOL_C_ARRAY_VARIABLE ||
            right_node->array_bound_kind == CTOOL_C_ARRAY_VARIABLE ||
            (left_node->array_bound_kind == CTOOL_C_ARRAY_FIXED &&
             right_node->array_bound_kind == CTOOL_C_ARRAY_FIXED &&
             left_node->element_count != right_node->element_count)) {
          compatible = CTOOL_FALSE;
          break;
        }
        pair.left = left_node->referenced_type;
        pair.right = right_node->referenced_type;
        pair.left_carried_qualifiers = left_qualifiers;
        pair.right_carried_qualifiers = right_qualifiers;
        continue;
      }
      if (left_qualifiers != right_qualifiers) {
        compatible = CTOOL_FALSE;
        break;
      }
      if (left_base == right_base) {
        break;
      }
      if (left_node->kind != right_node->kind) {
        if (left_node->kind == CTOOL_C_TYPE_ENUM) {
          pair.left = left_node->referenced_type;
          pair.right = right_base;
          continue;
        }
        if (right_node->kind == CTOOL_C_TYPE_ENUM) {
          pair.left = left_base;
          pair.right = right_node->referenced_type;
          continue;
        }
        compatible = CTOOL_FALSE;
        break;
      }
      if (cir_compatible_scalar_kind(left_node->kind) == CTOOL_TRUE) {
        break;
      }
      if (left_node->kind == CTOOL_C_TYPE_POINTER) {
        pair.left = left_node->referenced_type;
        pair.right = right_node->referenced_type;
        continue;
      }
      if (left_node->kind == CTOOL_C_TYPE_VECTOR) {
        if (left_node->element_count != right_node->element_count) {
          compatible = CTOOL_FALSE;
          break;
        }
        pair.left = left_node->referenced_type;
        pair.right = right_node->referenced_type;
        continue;
      }
      if (left_node->kind == CTOOL_C_TYPE_FUNCTION) {
        ctool_bool both_prototypes =
            left_node->has_prototype == CTOOL_TRUE &&
                    right_node->has_prototype == CTOOL_TRUE
                ? CTOOL_TRUE
                : CTOOL_FALSE;
        ctool_bool different_prototypes =
            left_node->has_prototype != right_node->has_prototype
                ? CTOOL_TRUE
                : CTOOL_FALSE;
        ctool_u32 index;
        if ((context->unit->graph.parameter_type_count != 0u &&
             context->unit->graph.parameter_types == (const ctool_u32 *)0) ||
            left_node->first_parameter >
                context->unit->graph.parameter_type_count ||
            left_node->parameter_count >
                context->unit->graph.parameter_type_count -
                    left_node->first_parameter ||
            right_node->first_parameter >
                context->unit->graph.parameter_type_count ||
            right_node->parameter_count >
                context->unit->graph.parameter_type_count -
                    right_node->first_parameter) {
          compatible = CTOOL_FALSE;
          break;
        }
        if (different_prototypes == CTOOL_TRUE) {
          const ctool_c_type_node_t *prototype =
              left_node->has_prototype == CTOOL_TRUE ? left_node
                                                     : right_node;
          if (prototype->variadic == CTOOL_TRUE) {
            compatible = CTOOL_FALSE;
            break;
          }
          for (index = 0u; index < prototype->parameter_count; index++) {
            const ctool_c_type_node_t *parameter = cir_unwrapped_type(
                context,
                context->unit->graph
                    .parameter_types[prototype->first_parameter + index]);
            if (parameter == (const ctool_c_type_node_t *)0 ||
                parameter->kind == CTOOL_C_TYPE_BOOL ||
                parameter->kind == CTOOL_C_TYPE_CHAR ||
                parameter->kind == CTOOL_C_TYPE_SIGNED_CHAR ||
                parameter->kind == CTOOL_C_TYPE_UNSIGNED_CHAR ||
                parameter->kind == CTOOL_C_TYPE_SIGNED_SHORT ||
                parameter->kind == CTOOL_C_TYPE_UNSIGNED_SHORT ||
                parameter->kind == CTOOL_C_TYPE_FLOAT) {
              compatible = CTOOL_FALSE;
              break;
            }
          }
        } else if (both_prototypes == CTOOL_TRUE) {
          if (left_node->variadic != right_node->variadic ||
              left_node->parameter_count != right_node->parameter_count) {
            compatible = CTOOL_FALSE;
            break;
          }
          for (index = 0u; index < left_node->parameter_count; index++) {
            cir_type_pair_t child;
            child.left = context->unit->graph.parameter_types
                [left_node->first_parameter + index];
            child.right = context->unit->graph.parameter_types
                [right_node->first_parameter + index];
            child.left_carried_qualifiers = 0u;
            child.right_carried_qualifiers = 0u;
            child.parameter_context = CTOOL_TRUE;
            status = cir_type_pair_append(context, &work, &child);
            if (status != CTOOL_OK) {
              break;
            }
          }
        }
        if (status == CTOOL_OK && compatible == CTOOL_TRUE) {
          cir_type_pair_t result;
          result.left = left_node->referenced_type;
          result.right = right_node->referenced_type;
          result.left_carried_qualifiers = 0u;
          result.right_carried_qualifiers = 0u;
          result.parameter_context = CTOOL_FALSE;
          status = cir_type_pair_append(context, &work, &result);
        }
        break;
      }
      compatible = CTOOL_FALSE;
      break;
    }
  }
  rewind_status = ctool_arena_rewind(context->arena, mark);
  if (status == CTOOL_OK && rewind_status == CTOOL_OK) {
    *compatible_out = compatible;
  }
  return status != CTOOL_OK ? status : rewind_status;
}

static ctool_bool cir_types_compatible(cir_context_t *context,
                                       ctool_u32 left, ctool_u32 right) {
  ctool_bool compatible = CTOOL_FALSE;
  if (context->relation_status == CTOOL_OK) {
    context->relation_status =
        cir_compare_types(context, left, right, &compatible);
  }
  return context->relation_status == CTOOL_OK ? compatible : CTOOL_FALSE;
}

static ctool_bool cir_pointer_values_compatible(
    cir_context_t *context, ctool_u32 left, ctool_u32 right) {
  const ctool_c_type_node_t *left_pointer;
  const ctool_c_type_node_t *right_pointer;
  ctool_u32 left_base;
  ctool_u32 right_base;
  ctool_u32 left_qualifiers;
  ctool_u32 right_qualifiers;
  if (cir_underlying_type(context, left, &left_base, &left_qualifiers,
                          &left_pointer) == CTOOL_FALSE ||
      cir_underlying_type(context, right, &right_base, &right_qualifiers,
                          &right_pointer) == CTOOL_FALSE ||
      left_pointer->kind != CTOOL_C_TYPE_POINTER ||
      right_pointer->kind != CTOOL_C_TYPE_POINTER) {
    return CTOOL_FALSE;
  }
  (void)left_base;
  (void)right_base;
  (void)left_qualifiers;
  (void)right_qualifiers;
  return cir_types_compatible(context, left_pointer->referenced_type,
                              right_pointer->referenced_type);
}

static ctool_bool cir_array_decay_types_match(
    cir_context_t *context, ctool_u32 array_type,
    ctool_u32 pointer_type) {
  const ctool_c_type_node_t *array;
  const ctool_c_type_node_t *pointer =
      cir_unwrapped_type(context, pointer_type);
  const ctool_c_type_node_t *element;
  const ctool_c_type_node_t *target;
  ctool_u32 array_base;
  ctool_u32 array_qualifiers;
  ctool_u32 element_base;
  ctool_u32 element_qualifiers;
  ctool_u32 target_base;
  ctool_u32 target_qualifiers;
  if (cir_underlying_type(context, array_type, &array_base,
                          &array_qualifiers, &array) == CTOOL_FALSE ||
      array->kind != CTOOL_C_TYPE_ARRAY ||
      array_type >= context->unit->layout.type_count ||
      context->unit->layout.types[array_type].is_complete_object ==
          CTOOL_FALSE ||
      pointer == (const ctool_c_type_node_t *)0 ||
      pointer->kind != CTOOL_C_TYPE_POINTER ||
      cir_type_is_i32_pointer(context, pointer_type) == CTOOL_FALSE ||
      cir_underlying_type(context, array->referenced_type, &element_base,
                          &element_qualifiers, &element) == CTOOL_FALSE ||
      cir_underlying_type(context, pointer->referenced_type, &target_base,
                          &target_qualifiers, &target) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  (void)array_base;
  array_qualifiers |= array->qualifiers;
  element_qualifiers |= element->qualifiers;
  target_qualifiers |= target->qualifiers;
  return target_qualifiers == (element_qualifiers | array_qualifiers) &&
                 cir_types_compatible(context, element_base, target_base) ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_query_unit_is_valid(
    const ctool_c_translation_unit_t *unit) {
  return unit != (const ctool_c_translation_unit_t *)0 &&
                 unit->graph.types != (const ctool_c_type_node_t *)0 &&
                 unit->layout.type_count == unit->graph.type_count &&
                 unit->layout.types != (const ctool_c_type_layout_t *)0 &&
                 (unit->graph.parameter_type_count == 0u ||
                  unit->graph.parameter_types != (const ctool_u32 *)0)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

ctool_status_t ctool_c_ir_function_types_compatible(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 left, ctool_u32 right, ctool_bool *compatible_out) {
  cir_context_t context;
  const ctool_c_type_node_t *left_function;
  const ctool_c_type_node_t *right_function;
  if (job == (ctool_job_t *)0 || compatible_out == (ctool_bool *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *compatible_out = CTOOL_FALSE;
  if (cir_type_query_unit_is_valid(unit) == CTOOL_FALSE ||
      left >= unit->graph.type_count || right >= unit->graph.type_count) {
    return CTOOL_OK;
  }
  cir_zero(&context, (ctool_u32)sizeof(context));
  context.job = job;
  context.unit = unit;
  context.arena = ctool_job_arena(job);
  left_function = cir_unwrapped_type(&context, left);
  right_function = cir_unwrapped_type(&context, right);
  if (left_function == (const ctool_c_type_node_t *)0 ||
      right_function == (const ctool_c_type_node_t *)0 ||
      left_function->kind != CTOOL_C_TYPE_FUNCTION ||
      right_function->kind != CTOOL_C_TYPE_FUNCTION) {
    return CTOOL_OK;
  }
  *compatible_out = cir_types_compatible(&context, left, right);
  return context.relation_status;
}

ctool_status_t ctool_c_ir_pointer_value_types_compatible(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 left, ctool_u32 right, ctool_bool *compatible_out) {
  cir_context_t context;
  if (job == (ctool_job_t *)0 || compatible_out == (ctool_bool *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *compatible_out = CTOOL_FALSE;
  if (cir_type_query_unit_is_valid(unit) == CTOOL_FALSE ||
      left >= unit->graph.type_count || right >= unit->graph.type_count) {
    return CTOOL_OK;
  }
  cir_zero(&context, (ctool_u32)sizeof(context));
  context.job = job;
  context.unit = unit;
  context.arena = ctool_job_arena(job);
  if (cir_type_is_i32_pointer_value(&context, left) == CTOOL_FALSE ||
      cir_type_is_i32_pointer_value(&context, right) == CTOOL_FALSE) {
    return CTOOL_OK;
  }
  *compatible_out = cir_pointer_values_compatible(&context, left, right);
  return context.relation_status;
}

static ctool_bool cir_pointer_value_types_match(
    cir_context_t *context, ctool_u32 object_type,
    ctool_u32 value_type) {
  return cir_type_is_i32_pointer_value(context, object_type) == CTOOL_TRUE &&
                 cir_type_is_i32_pointer_value(context, value_type) ==
                     CTOOL_TRUE &&
                 cir_pointer_values_compatible(context, object_type,
                                               value_type) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_pointer_comparison_types_match(
    cir_context_t *context, ctool_u32 left_type,
    ctool_u32 right_type, ctool_bool require_object_referents) {
  const ctool_c_type_node_t *left_pointer =
      cir_unwrapped_type(context, left_type);
  const ctool_c_type_node_t *right_pointer =
      cir_unwrapped_type(context, right_type);
  const ctool_c_type_node_t *left_referent;
  const ctool_c_type_node_t *right_referent;
  ctool_u32 left_base;
  ctool_u32 right_base;
  ctool_u32 left_qualifiers;
  ctool_u32 right_qualifiers;
  if (cir_type_is_i32_pointer_value(context, left_type) == CTOOL_FALSE ||
      cir_type_is_i32_pointer_value(context, right_type) == CTOOL_FALSE ||
      left_pointer == (const ctool_c_type_node_t *)0 ||
      right_pointer == (const ctool_c_type_node_t *)0 ||
      cir_underlying_type(context, left_pointer->referenced_type, &left_base,
                          &left_qualifiers, &left_referent) == CTOOL_FALSE ||
      cir_underlying_type(context, right_pointer->referenced_type,
                          &right_base, &right_qualifiers,
                          &right_referent) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  (void)left_qualifiers;
  (void)right_qualifiers;
  (void)left_referent;
  (void)right_referent;
  if (require_object_referents == CTOOL_TRUE &&
      (cir_type_is_i32_pointer(context, left_type) == CTOOL_FALSE ||
       cir_type_is_i32_pointer(context, right_type) == CTOOL_FALSE ||
       left_base >= context->unit->layout.type_count ||
       right_base >= context->unit->layout.type_count ||
       context->unit->layout.types[left_base].is_object == CTOOL_FALSE ||
       context->unit->layout.types[right_base].is_object == CTOOL_FALSE)) {
    return CTOOL_FALSE;
  }
  return cir_types_compatible(context, left_base, right_base);
}

ctool_status_t ctool_c_ir_pointer_comparison_types_compatible(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 left, ctool_u32 right, ctool_bool require_object_referents,
    ctool_bool *compatible_out) {
  cir_context_t context;
  if (job == (ctool_job_t *)0 || compatible_out == (ctool_bool *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *compatible_out = CTOOL_FALSE;
  if (cir_type_query_unit_is_valid(unit) == CTOOL_FALSE ||
      left >= unit->graph.type_count || right >= unit->graph.type_count) {
    return CTOOL_OK;
  }
  cir_zero(&context, (ctool_u32)sizeof(context));
  context.job = job;
  context.unit = unit;
  context.arena = ctool_job_arena(job);
  *compatible_out = cir_pointer_comparison_types_match(
      &context, left, right, require_object_referents);
  return context.relation_status;
}

ctool_status_t ctool_c_ir_pointer_arithmetic_types_compatible(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 left, ctool_u32 right, ctool_bool *compatible_out) {
  cir_context_t context;
  const ctool_c_type_node_t *left_pointer;
  const ctool_c_type_node_t *right_pointer;
  if (job == (ctool_job_t *)0 || compatible_out == (ctool_bool *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *compatible_out = CTOOL_FALSE;
  if (cir_type_query_unit_is_valid(unit) == CTOOL_FALSE ||
      left >= unit->graph.type_count || right >= unit->graph.type_count) {
    return CTOOL_OK;
  }
  cir_zero(&context, (ctool_u32)sizeof(context));
  context.job = job;
  context.unit = unit;
  context.arena = ctool_job_arena(job);
  left_pointer = cir_unwrapped_type(&context, left);
  right_pointer = cir_unwrapped_type(&context, right);
  if (left_pointer == (const ctool_c_type_node_t *)0 ||
      right_pointer == (const ctool_c_type_node_t *)0 ||
      left_pointer->kind != CTOOL_C_TYPE_POINTER ||
      right_pointer->kind != CTOOL_C_TYPE_POINTER ||
      left_pointer->referenced_type >= unit->layout.type_count ||
      right_pointer->referenced_type >= unit->layout.type_count ||
      unit->layout.types[left_pointer->referenced_type].is_complete_object ==
          CTOOL_FALSE ||
      unit->layout.types[right_pointer->referenced_type]
              .is_complete_object == CTOOL_FALSE ||
      unit->layout.types[left_pointer->referenced_type].size == 0u ||
      unit->layout.types[right_pointer->referenced_type].size == 0u) {
    return CTOOL_OK;
  }
  *compatible_out = cir_pointer_comparison_types_match(
      &context, left, right, CTOOL_TRUE);
  return context.relation_status;
}

ctool_status_t ctool_c_ir_array_decay_types_compatible(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 array_type, ctool_u32 pointer_type,
    ctool_bool *compatible_out) {
  cir_context_t context;
  if (job == (ctool_job_t *)0 || compatible_out == (ctool_bool *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *compatible_out = CTOOL_FALSE;
  if (cir_type_query_unit_is_valid(unit) == CTOOL_FALSE ||
      array_type >= unit->graph.type_count ||
      pointer_type >= unit->graph.type_count) {
    return CTOOL_OK;
  }
  cir_zero(&context, (ctool_u32)sizeof(context));
  context.job = job;
  context.unit = unit;
  context.arena = ctool_job_arena(job);
  *compatible_out =
      cir_array_decay_types_match(&context, array_type, pointer_type);
  return context.relation_status;
}

static ctool_bool cir_pointer_conversion_is_valid(
    cir_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type, ctool_c_conversion_kind_t conversion) {
  const ctool_c_type_node_t *source_pointer =
      cir_unwrapped_type(context, source_type);
  const ctool_c_type_node_t *target_pointer =
      cir_unwrapped_type(context, target_type);
  const ctool_c_type_node_t *source_referent;
  const ctool_c_type_node_t *target_referent;
  ctool_u32 source_base;
  ctool_u32 target_base;
  ctool_u32 source_qualifiers;
  ctool_u32 target_qualifiers;
  if (source_pointer == (const ctool_c_type_node_t *)0 ||
      target_pointer == (const ctool_c_type_node_t *)0 ||
      source_pointer->kind != CTOOL_C_TYPE_POINTER ||
      target_pointer->kind != CTOOL_C_TYPE_POINTER ||
      cir_underlying_type(context, source_pointer->referenced_type,
                          &source_base, &source_qualifiers,
                          &source_referent) == CTOOL_FALSE ||
      cir_underlying_type(context, target_pointer->referenced_type,
                          &target_base, &target_qualifiers,
                          &target_referent) == CTOOL_FALSE ||
      (source_qualifiers & ~target_qualifiers) != 0u ||
      ((source_qualifiers ^ target_qualifiers) & CTOOL_C_QUAL_ATOMIC) != 0u) {
    return CTOOL_FALSE;
  }
  if (conversion == CTOOL_C_CONVERSION_QUALIFICATION) {
    return cir_types_compatible(context, source_base, target_base);
  }
  if (conversion != CTOOL_C_CONVERSION_POINTER) {
    return CTOOL_FALSE;
  }
  if (target_referent->kind == CTOOL_C_TYPE_VOID &&
      source_base < context->unit->layout.type_count &&
      context->unit->layout.types[source_base].is_object == CTOOL_TRUE) {
    return CTOOL_TRUE;
  }
  return source_referent->kind == CTOOL_C_TYPE_VOID &&
                 target_base < context->unit->layout.type_count &&
                 context->unit->layout.types[target_base].is_object ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

ctool_status_t ctool_c_ir_pointer_conversion_is_valid(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_u32 source_type, ctool_u32 target_type,
    ctool_c_conversion_kind_t conversion, ctool_bool *valid_out) {
  cir_context_t context;
  if (job == (ctool_job_t *)0 || valid_out == (ctool_bool *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *valid_out = CTOOL_FALSE;
  if (cir_type_query_unit_is_valid(unit) == CTOOL_FALSE ||
      source_type >= unit->graph.type_count ||
      target_type >= unit->graph.type_count) {
    return CTOOL_OK;
  }
  cir_zero(&context, (ctool_u32)sizeof(context));
  context.job = job;
  context.unit = unit;
  context.arena = ctool_job_arena(job);
  *valid_out = cir_pointer_conversion_is_valid(
      &context, source_type, target_type, conversion);
  return context.relation_status;
}

static ctool_bool cir_i32_bits_are_canonical(ctool_u64 bits,
                                              ctool_bool is_signed) {
  ctool_u64 low = bits & 0xffffffffu;
  ctool_u64 expected_upper =
      is_signed == CTOOL_TRUE && (low & 0x80000000u) != 0u
          ? 0xffffffffu
          : 0u;
  return bits >> 32u == expected_upper ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool cir_type_is_plain_signed_int(
    const cir_context_t *context, ctool_u32 type) {
  return type < context->unit->graph.type_count &&
                 context->unit->graph.types[type].kind ==
                     CTOOL_C_TYPE_SIGNED_INT &&
                 context->unit->graph.types[type].qualifiers == 0u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_u32 cir_plain_signed_int_type(
    const cir_context_t *context) {
  ctool_u32 type;
  for (type = 0u; type < context->unit->graph.type_count; type++) {
    if (cir_type_is_plain_signed_int(context, type) == CTOOL_TRUE) {
      return type;
    }
  }
  return CTOOL_C_TYPE_NONE;
}

static ctool_bool cir_type_is_complete_aggregate_object(
    const cir_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cir_unwrapped_type(context, type);
  return type < context->unit->layout.type_count &&
                 node != (const ctool_c_type_node_t *)0 &&
                 ((node->kind == CTOOL_C_TYPE_ARRAY &&
                   node->array_bound_kind == CTOOL_C_ARRAY_FIXED) ||
                  (node->kind == CTOOL_C_TYPE_RECORD &&
                   node->record_complete == CTOOL_TRUE)) &&
                 context->unit->layout.types[type].is_object == CTOOL_TRUE &&
                 context->unit->layout.types[type].is_complete_object ==
                     CTOOL_TRUE &&
                 context->unit->layout.types[type].size != 0u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_is_complete_record_object(
    const cir_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cir_unwrapped_type(context, type);
  return node != (const ctool_c_type_node_t *)0 &&
                 node->kind == CTOOL_C_TYPE_RECORD &&
                 cir_type_is_complete_aggregate_object(context, type) ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_is_character_array(
    const cir_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *array = cir_unwrapped_type(context, type);
  const ctool_c_type_node_t *element =
      array != (const ctool_c_type_node_t *)0 &&
              array->kind == CTOOL_C_TYPE_ARRAY
          ? cir_unwrapped_type(context, array->referenced_type)
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

static ctool_bool cir_type_is_structure_value(
    const cir_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node;
  const ctool_c_type_layout_t *layout;
  ctool_u32 base;
  ctool_u32 qualifiers;
  if (cir_underlying_type(context, type, &base, &qualifiers, &node) ==
          CTOOL_FALSE ||
      type >= context->unit->layout.type_count ||
      base >= context->unit->layout.type_count ||
      node->kind != CTOOL_C_TYPE_RECORD ||
      node->record_kind != CTOOL_C_RECORD_STRUCT ||
      node->record_complete == CTOOL_FALSE ||
      (qualifiers & (CTOOL_C_QUAL_VOLATILE | CTOOL_C_QUAL_ATOMIC)) != 0u ||
      (node->qualifiers &
       (CTOOL_C_QUAL_VOLATILE | CTOOL_C_QUAL_ATOMIC)) != 0u) {
    return CTOOL_FALSE;
  }
  layout = &context->unit->layout.types[type];
  return layout->is_object == CTOOL_TRUE &&
                 layout->is_complete_object == CTOOL_TRUE &&
                 layout->size != 0u && layout->alignment != 0u &&
                 layout->alignment <= 4u &&
                 (layout->alignment & (layout->alignment - 1u)) == 0u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_type_is_structure_candidate(
    const cir_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cir_unwrapped_type(context, type);
  return node != (const ctool_c_type_node_t *)0 &&
                 node->kind == CTOOL_C_TYPE_RECORD &&
                 node->record_kind == CTOOL_C_RECORD_STRUCT
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_structure_value_types_match(
    const cir_context_t *context, ctool_u32 left, ctool_u32 right) {
  const ctool_c_type_node_t *left_node;
  const ctool_c_type_node_t *right_node;
  ctool_u32 left_base;
  ctool_u32 left_qualifiers;
  ctool_u32 right_base;
  ctool_u32 right_qualifiers;
  if (cir_type_is_structure_value(context, left) == CTOOL_FALSE ||
      cir_type_is_structure_value(context, right) == CTOOL_FALSE ||
      cir_underlying_type(context, left, &left_base, &left_qualifiers,
                          &left_node) == CTOOL_FALSE ||
      cir_underlying_type(context, right, &right_base, &right_qualifiers,
                          &right_node) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  (void)left_qualifiers;
  (void)right_qualifiers;
  return left_base == right_base && left_node == right_node &&
                 context->unit->layout.types[left].size ==
                     context->unit->layout.types[right].size
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_integer_value_types_match(
    const cir_context_t *context, ctool_u32 object_type,
    ctool_u32 value_type) {
  const ctool_c_type_node_t *object_node;
  const ctool_c_type_node_t *value_node;
  if (object_type == value_type) {
    return CTOOL_TRUE;
  }
  object_node = cir_unwrapped_type(context, object_type);
  value_node = cir_unwrapped_type(context, value_type);
  if (object_node == (const ctool_c_type_node_t *)0 ||
      value_node == (const ctool_c_type_node_t *)0 ||
      object_node->kind != value_node->kind) {
    return CTOOL_FALSE;
  }
  if (object_node->kind == CTOOL_C_TYPE_ENUM && object_node != value_node) {
    return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_bool cir_scalar_value_types_match(
    cir_context_t *context, ctool_u32 object_type,
    ctool_u32 value_type) {
  if (cir_type_is_value_integer(context, object_type) == CTOOL_TRUE &&
      cir_type_is_value_integer(context, value_type) == CTOOL_TRUE) {
    return cir_integer_value_types_match(context, object_type, value_type);
  }
  if (cir_type_is_floating_value(context, object_type) == CTOOL_TRUE &&
      cir_type_is_floating_value(context, value_type) == CTOOL_TRUE) {
    const ctool_c_type_node_t *object_node =
        cir_unwrapped_type(context, object_type);
    const ctool_c_type_node_t *value_node =
        cir_unwrapped_type(context, value_type);
    return object_node != (const ctool_c_type_node_t *)0 &&
                   value_node != (const ctool_c_type_node_t *)0 &&
                   object_node->kind == value_node->kind
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return cir_pointer_value_types_match(context, object_type, value_type);
}

static ctool_bool cir_narrow_integer_kind(ctool_c_type_kind_t kind) {
  return kind == CTOOL_C_TYPE_BOOL || kind == CTOOL_C_TYPE_CHAR ||
                 kind == CTOOL_C_TYPE_SIGNED_CHAR ||
                 kind == CTOOL_C_TYPE_UNSIGNED_CHAR ||
                 kind == CTOOL_C_TYPE_SIGNED_SHORT ||
                 kind == CTOOL_C_TYPE_UNSIGNED_SHORT
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_promoted_i32_integer_kind(
    ctool_c_type_kind_t kind) {
  return kind == CTOOL_C_TYPE_SIGNED_INT ||
                 kind == CTOOL_C_TYPE_UNSIGNED_INT ||
                 kind == CTOOL_C_TYPE_SIGNED_LONG ||
                 kind == CTOOL_C_TYPE_UNSIGNED_LONG
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_wide_standard_integer_kind(
    ctool_c_type_kind_t kind) {
  return kind == CTOOL_C_TYPE_SIGNED_LONG_LONG ||
                 kind == CTOOL_C_TYPE_UNSIGNED_LONG_LONG
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_integer_promotion_is_valid(
    const cir_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type) {
  const ctool_c_type_node_t *source =
      cir_unwrapped_type(context, source_type);
  const ctool_c_type_node_t *target =
      cir_unwrapped_type(context, target_type);
  ctool_c_type_kind_t expected;
  if (source == (const ctool_c_type_node_t *)0 ||
      target == (const ctool_c_type_node_t *)0) {
    return CTOOL_FALSE;
  }
  if (source->kind == CTOOL_C_TYPE_ENUM) {
    const ctool_c_type_node_t *compatible =
        cir_unwrapped_type(context, source->referenced_type);
    if (compatible == (const ctool_c_type_node_t *)0) {
      return CTOOL_FALSE;
    }
    expected = cir_narrow_integer_kind(compatible->kind) == CTOOL_TRUE
                   ? CTOOL_C_TYPE_SIGNED_INT
                   : compatible->kind;
  } else if (cir_narrow_integer_kind(source->kind) == CTOOL_TRUE) {
    expected = CTOOL_C_TYPE_SIGNED_INT;
  } else {
    return CTOOL_FALSE;
  }
  if (target->kind != expected) {
    return CTOOL_FALSE;
  }
  if (cir_type_is_i32_integer(context, target_type) == CTOOL_TRUE) {
    return cir_promoted_i32_integer_kind(expected);
  }
  return cir_type_is_wide_integer(context, target_type) == CTOOL_TRUE
             ? cir_wide_standard_integer_kind(expected)
             : CTOOL_FALSE;
}

static ctool_bool cir_usual_integer_conversion_is_valid(
    const cir_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type) {
  const ctool_c_type_node_t *source =
      cir_unwrapped_type(context, source_type);
  const ctool_c_type_node_t *target =
      cir_unwrapped_type(context, target_type);
  if (source == (const ctool_c_type_node_t *)0 ||
      target == (const ctool_c_type_node_t *)0 ||
      cir_type_is_i32_integer(context, source_type) == CTOOL_FALSE ||
      cir_type_is_i32_integer(context, target_type) == CTOOL_FALSE ||
      cir_promoted_i32_integer_kind(source->kind) == CTOOL_FALSE ||
      cir_promoted_i32_integer_kind(target->kind) == CTOOL_FALSE) {
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

static ctool_bool cir_wide_usual_integer_conversion_is_valid(
    const cir_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type) {
  const ctool_c_type_node_t *source =
      cir_unwrapped_type(context, source_type);
  const ctool_c_type_node_t *target =
      cir_unwrapped_type(context, target_type);
  if (source == (const ctool_c_type_node_t *)0 ||
      target == (const ctool_c_type_node_t *)0 ||
      cir_type_is_wide_integer(context, target_type) == CTOOL_FALSE ||
      cir_wide_standard_integer_kind(target->kind) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  if (cir_type_is_wide_integer(context, source_type) == CTOOL_TRUE) {
    return source->kind == CTOOL_C_TYPE_SIGNED_LONG_LONG &&
                   target->kind == CTOOL_C_TYPE_UNSIGNED_LONG_LONG
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return cir_type_is_i32_integer(context, source_type) == CTOOL_TRUE
             ? cir_promoted_i32_integer_kind(source->kind)
             : CTOOL_FALSE;
}

static ctool_bool cir_integer_conversion_is_valid(
    const cir_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type, ctool_c_conversion_kind_t conversion) {
  ctool_bool source_wide = cir_type_is_wide_integer(context, source_type);
  ctool_bool target_wide = cir_type_is_wide_integer(context, target_type);
  if (source_wide == CTOOL_TRUE || target_wide == CTOOL_TRUE) {
    if (cir_type_is_value_integer(context, source_type) == CTOOL_FALSE ||
        cir_type_is_value_integer(context, target_type) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
    if (conversion == CTOOL_C_CONVERSION_NONE ||
        conversion == CTOOL_C_CONVERSION_ASSIGNMENT) {
      return CTOOL_TRUE;
    }
    if (conversion == CTOOL_C_CONVERSION_QUALIFICATION) {
      return source_wide == CTOOL_TRUE && target_wide == CTOOL_TRUE
                 ? cir_integer_value_types_match(context, source_type,
                                                  target_type)
                 : CTOOL_FALSE;
    }
    if (conversion == CTOOL_C_CONVERSION_INTEGER_PROMOTION) {
      return source_wide == CTOOL_TRUE && target_wide == CTOOL_TRUE
                 ? cir_integer_promotion_is_valid(
                       context, source_type, target_type)
                 : CTOOL_FALSE;
    }
    return conversion == CTOOL_C_CONVERSION_USUAL_ARITHMETIC
               ? cir_wide_usual_integer_conversion_is_valid(
                     context, source_type, target_type)
               : CTOOL_FALSE;
  }
  if (cir_type_is_represented_integer(context, source_type) == CTOOL_FALSE ||
      cir_type_is_represented_integer(context, target_type) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  if (conversion == CTOOL_C_CONVERSION_NONE ||
      conversion == CTOOL_C_CONVERSION_ASSIGNMENT) {
    return CTOOL_TRUE;
  }
  if (conversion == CTOOL_C_CONVERSION_QUALIFICATION) {
    return cir_integer_value_types_match(context, source_type, target_type);
  }
  if (conversion == CTOOL_C_CONVERSION_USUAL_ARITHMETIC) {
    return cir_usual_integer_conversion_is_valid(
        context, source_type, target_type);
  }
  return conversion == CTOOL_C_CONVERSION_INTEGER_PROMOTION
             ? cir_integer_promotion_is_valid(context, source_type,
                                               target_type)
             : CTOOL_FALSE;
}

static ctool_bool cir_float_promotion_is_valid(
    const cir_context_t *context, ctool_u32 source_type,
    ctool_u32 target_type) {
  const ctool_c_type_node_t *source =
      cir_unwrapped_type(context, source_type);
  const ctool_c_type_node_t *target =
      cir_unwrapped_type(context, target_type);
  return source != (const ctool_c_type_node_t *)0 &&
                 target != (const ctool_c_type_node_t *)0 &&
                 source->kind == CTOOL_C_TYPE_FLOAT &&
                 target->kind == CTOOL_C_TYPE_DOUBLE &&
                 cir_type_is_floating_value(context, source_type) ==
                     CTOOL_TRUE &&
                 cir_type_is_floating_value(context, target_type) ==
                     CTOOL_TRUE &&
                 cir_type_has_atomic_qualification(context, source_type) ==
                     CTOOL_FALSE &&
                 cir_type_has_atomic_qualification(context, target_type) ==
                     CTOOL_FALSE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cir_push(cir_context_t *context,
                               cir_stack_kind_t kind, ctool_u32 type) {
  if (context->stack_depth >= CIR_STACK_LIMIT) {
    return cir_emit_failure(
        context, CTOOL_ERR_LIMIT, CTOOL_C_IR_DIAG_LIMIT,
        (const ctool_c_pp_location_t *)0,
        "CupidC IR lowering exceeded a configured resource limit");
  }
  context->stack[context->stack_depth].kind = kind;
  context->stack[context->stack_depth].type = type;
  context->stack_depth++;
  if (context->maximum_stack_depth < context->stack_depth) {
    context->maximum_stack_depth = context->stack_depth;
  }
  return CTOOL_OK;
}

static ctool_status_t cir_pop(cir_context_t *context,
                              cir_stack_entry_t *entry_out) {
  if (context->stack_depth == 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  context->stack_depth--;
  *entry_out = context->stack[context->stack_depth];
  return CTOOL_OK;
}

static ctool_u32 cir_function_offset(const cir_context_t *context) {
  return context->instruction_count - context->function_first_instruction;
}

static ctool_status_t cir_append_instruction(
    cir_context_t *context, ctool_c_ir_instruction_kind_t kind,
    ctool_u32 type, ctool_u32 input_type,
    ctool_c_expression_operator_t operation,
    ctool_c_conversion_kind_t conversion, ctool_u32 reference,
    ctool_u64 integer_bits, const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location,
    ctool_u32 *index_out) {
  ctool_c_ir_instruction_t *instruction;
  ctool_u32 index = context->instruction_count;
  if (context->instruction_count == 0xffffffffu ||
      (context->instructions != (ctool_c_ir_instruction_t *)0 &&
       context->instruction_count >= context->instruction_capacity)) {
    return CTOOL_ERR_OVERFLOW;
  }
  context->instruction_count++;
  if (index_out != (ctool_u32 *)0) {
    *index_out = index;
  }
  if (context->instructions == (ctool_c_ir_instruction_t *)0) {
    return CTOOL_OK;
  }
  instruction = &context->instructions[index];
  cir_zero(instruction, (ctool_u32)sizeof(*instruction));
  instruction->kind = kind;
  instruction->type = type;
  instruction->input_type = input_type;
  instruction->operation = operation;
  instruction->conversion = conversion;
  instruction->first_argument_type = CTOOL_C_AST_NONE;
  instruction->reference = reference;
  instruction->integer_bits = integer_bits;
  if (location != (const ctool_c_pp_location_t *)0) {
    instruction->location = *location;
  }
  if (physical_location != (const ctool_c_pp_location_t *)0) {
    instruction->physical_location = *physical_location;
  }
  return CTOOL_OK;
}

static ctool_status_t cir_append_argument_type(cir_context_t *context,
                                               ctool_u32 type) {
  ctool_u32 index = context->argument_type_count;
  if (type >= context->unit->graph.type_count) {
    return CTOOL_ERR_INTERNAL;
  }
  if (index >= CTOOL_C_AST_NONE - 1u ||
      (context->argument_types != (ctool_u32 *)0 &&
       index >= context->argument_type_capacity)) {
    return CTOOL_ERR_OVERFLOW;
  }
  context->argument_type_count++;
  if (context->argument_types != (ctool_u32 *)0) {
    context->argument_types[index] = type;
  }
  return CTOOL_OK;
}

static ctool_status_t cir_duplicate_address(
    cir_context_t *context, ctool_u32 type,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location) {
  ctool_status_t status;
  if (context->stack_depth == 0u ||
      context->stack[context->stack_depth - 1u].kind != CIR_STACK_ADDRESS ||
      context->stack[context->stack_depth - 1u].type != type ||
      (cir_type_is_value_scalar(context, type) == CTOOL_FALSE &&
       cir_type_is_complete_record_object(context, type) == CTOOL_FALSE)) {
    return cir_invalid_unit(context, location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_DUPLICATE_ADDRESS, type, type,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
      CTOOL_C_AST_NONE, 0u, location, physical_location,
      (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_ADDRESS, type);
}

static ctool_bool cir_integer_mutation_promotion_type(
    const cir_context_t *context, ctool_u32 type,
    ctool_u32 *promoted_type_out) {
  const ctool_c_type_node_t *node;
  ctool_u32 base;
  ctool_u32 qualifiers;
  if (cir_underlying_type(context, type, &base, &qualifiers, &node) ==
      CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  (void)qualifiers;
  if (node->kind == CTOOL_C_TYPE_ENUM) {
    if (cir_underlying_type(context, node->referenced_type, &base,
                            &qualifiers, &node) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  if (cir_narrow_integer_kind(node->kind) == CTOOL_TRUE) {
    ctool_u32 signed_int_type;
    if (node->kind == CTOOL_C_TYPE_BOOL) {
      return CTOOL_FALSE;
    }
    signed_int_type = cir_plain_signed_int_type(context);
    if (signed_int_type == CTOOL_C_TYPE_NONE ||
        cir_integer_promotion_is_valid(context, type, signed_int_type) ==
            CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
    *promoted_type_out = signed_int_type;
    return CTOOL_TRUE;
  }
  if (node->kind != CTOOL_C_TYPE_SIGNED_INT &&
      node->kind != CTOOL_C_TYPE_UNSIGNED_INT &&
      node->kind != CTOOL_C_TYPE_SIGNED_LONG &&
      node->kind != CTOOL_C_TYPE_UNSIGNED_LONG &&
      node->kind != CTOOL_C_TYPE_SIGNED_LONG_LONG &&
      node->kind != CTOOL_C_TYPE_UNSIGNED_LONG_LONG) {
    return CTOOL_FALSE;
  }
  if (cir_type_is_i32_integer(context, base) == CTOOL_FALSE &&
      cir_type_is_wide_integer(context, base) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  *promoted_type_out = base;
  return CTOOL_TRUE;
}

static ctool_bool cir_represented_bit_field_promotion_type(
    const cir_context_t *context, ctool_u32 type, ctool_u32 bit_width,
    ctool_u32 *promoted_type_out) {
  const ctool_c_type_node_t *node;
  ctool_u32 base;
  ctool_u32 qualifiers;
  if (cir_underlying_type(context, type, &base, &qualifiers, &node) ==
      CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  (void)base;
  (void)qualifiers;
  if (node->kind == CTOOL_C_TYPE_ENUM) {
    if (cir_underlying_type(context, node->referenced_type, &base,
                            &qualifiers, &node) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  if (node->kind == CTOOL_C_TYPE_UNSIGNED_INT && bit_width < 32u) {
    *promoted_type_out = cir_plain_signed_int_type(context);
    return *promoted_type_out != CTOOL_C_TYPE_NONE ? CTOOL_TRUE
                                                    : CTOOL_FALSE;
  }
  return cir_integer_mutation_promotion_type(context, type,
                                             promoted_type_out);
}

static ctool_status_t cir_convert_top_integer(
    cir_context_t *context, ctool_u32 input_type, ctool_u32 output_type,
    ctool_c_conversion_kind_t conversion,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location) {
  ctool_status_t status;
  if (input_type == output_type) {
    return CTOOL_OK;
  }
  if (context->stack_depth == 0u ||
      context->stack[context->stack_depth - 1u].kind != CIR_STACK_VALUE ||
      context->stack[context->stack_depth - 1u].type != input_type ||
      cir_integer_conversion_is_valid(context, input_type, output_type,
                                      conversion) == CTOOL_FALSE) {
    return cir_invalid_unit(context, location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_CONVERT, output_type, input_type,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, conversion, CTOOL_C_AST_NONE, 0u,
      location, physical_location, (ctool_u32 *)0);
  if (status == CTOOL_OK) {
    context->stack[context->stack_depth - 1u].type = output_type;
  }
  return status;
}

static ctool_status_t cir_convert_top_bit_field_promotion(
    cir_context_t *context, ctool_u32 input_type, ctool_u32 output_type,
    ctool_u32 bit_width, ctool_u32 member,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location) {
  ctool_u32 expected_type;
  ctool_status_t status;
  if (cir_represented_bit_field_promotion_type(
          context, input_type, bit_width, &expected_type) == CTOOL_FALSE ||
      expected_type != output_type) {
    return cir_invalid_unit(context, location);
  }
  if (input_type == output_type) {
    return CTOOL_OK;
  }
  if (context->stack_depth == 0u ||
      context->stack[context->stack_depth - 1u].kind != CIR_STACK_VALUE ||
      context->stack[context->stack_depth - 1u].type != input_type) {
    return cir_invalid_unit(context, location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_CONVERT, output_type, input_type,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_INTEGER_PROMOTION,
      member, 0u, location, physical_location, (ctool_u32 *)0);
  if (status == CTOOL_OK) {
    context->stack[context->stack_depth - 1u].type = output_type;
  }
  return status;
}

static ctool_status_t cir_require_integer_mutation_computation(
    cir_context_t *context, ctool_u32 value_type,
    ctool_u32 computation_type,
    const ctool_c_pp_location_t *location) {
  if (cir_type_is_wide_integer(context, value_type) == CTOOL_TRUE) {
    if (cir_type_is_wide_integer(context, computation_type) == CTOOL_TRUE) {
      return CTOOL_OK;
    }
    if (cir_type_is_represented_integer(context, computation_type) ==
        CTOOL_TRUE) {
      return cir_invalid_unit(context, location);
    }
    return cir_unsupported_type(context, location);
  }
  if (cir_type_is_i32_integer(context, computation_type) == CTOOL_TRUE) {
    return CTOOL_OK;
  }
  if (cir_type_is_represented_integer(context, computation_type) ==
      CTOOL_TRUE) {
    return cir_invalid_unit(context, location);
  }
  return cir_unsupported_type(context, location);
}

static ctool_status_t cir_require_mutation_right_operand(
    cir_context_t *context, const cir_stack_entry_t *right,
    ctool_u32 computation_type, ctool_bool shift,
    const ctool_c_pp_location_t *location) {
  if (right->kind != CIR_STACK_VALUE) {
    return cir_invalid_unit(context, location);
  }
  if (shift == CTOOL_FALSE) {
    return right->type == computation_type
               ? CTOOL_OK
               : cir_invalid_unit(context, location);
  }
  if (cir_type_is_i32_integer(context, right->type) == CTOOL_TRUE) {
    return CTOOL_OK;
  }
  return cir_type_is_value_integer(context, right->type) == CTOOL_TRUE
             ? cir_unsupported_type(context, location)
             : cir_invalid_unit(context, location);
}

static void cir_record_patched_target(cir_context_t *context,
                                      ctool_u32 target) {
  if (target == CTOOL_C_AST_NONE) {
    return;
  }
  if (context->function_has_patched_target == CTOOL_FALSE ||
      context->function_maximum_patched_target < target) {
    context->function_has_patched_target = CTOOL_TRUE;
    context->function_maximum_patched_target = target;
  }
}

static ctool_status_t cir_patch_reference(cir_context_t *context,
                                          ctool_u32 instruction,
                                          ctool_u32 reference) {
  cir_record_patched_target(context, reference);
  if (context->instructions == (ctool_c_ir_instruction_t *)0) {
    return CTOOL_OK;
  }
  if (instruction >= context->instruction_count ||
      instruction < context->function_first_instruction) {
    return CTOOL_ERR_INTERNAL;
  }
  context->instructions[instruction].reference = reference;
  return CTOOL_OK;
}

static ctool_status_t cir_expression_child(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 child_offset,
    ctool_u32 *child_out) {
  ctool_u32 child;
  *child_out = CTOOL_C_AST_NONE;
  if (expression->first_child > context->unit->expression_child_count ||
      expression->child_count >
          context->unit->expression_child_count - expression->first_child ||
      child_offset >= expression->child_count) {
    return cir_invalid_unit(context, &expression->location);
  }
  child = context->unit
              ->expression_children[expression->first_child + child_offset];
  if (child >= expression_index || child >= context->unit->expression_count) {
    return cir_invalid_unit(context, &expression->location);
  }
  *child_out = child;
  return CTOOL_OK;
}

static ctool_status_t cir_lower_expression(cir_context_t *context,
                                           ctool_u32 expression_index,
                                           ctool_u32 depth);

static ctool_status_t cir_duplicate_value(
    cir_context_t *context, ctool_u32 type,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location);

static ctool_status_t cir_require_initializable_aggregate(
    cir_context_t *context, ctool_u32 root_type,
    const ctool_c_pp_location_t *location);

static ctool_status_t cir_require_structure_value(
    cir_context_t *context, ctool_u32 type,
    const ctool_c_pp_location_t *location);

static ctool_status_t cir_lower_compound_literal_expression(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth);

typedef struct {
  const ctool_c_expression_t *record_expression;
  const ctool_c_record_member_t *member;
  const ctool_c_type_layout_t *record_layout;
  const ctool_c_type_layout_t *member_type_layout;
  const ctool_c_type_layout_t *designator_layout;
  const ctool_c_member_layout_t *member_layout;
  ctool_u32 record_child;
  ctool_u32 member_base;
  ctool_u32 member_qualifiers;
} cir_member_info_t;

static ctool_bool cir_member_info_complete(
    const cir_member_info_t *info) {
  return (info->record_expression !=
              (const ctool_c_expression_t *)0 &&
          info->member != (const ctool_c_record_member_t *)0 &&
          info->record_layout != (const ctool_c_type_layout_t *)0 &&
          info->member_type_layout !=
              (const ctool_c_type_layout_t *)0 &&
          info->designator_layout !=
              (const ctool_c_type_layout_t *)0 &&
          info->member_layout != (const ctool_c_member_layout_t *)0)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cir_validate_member(
    cir_context_t *context, ctool_u32 member_expression_index,
    const ctool_c_expression_t *member_expression,
    const ctool_c_pp_location_t *location, cir_member_info_t *info) {
  const ctool_c_type_node_t *record_node;
  const ctool_c_type_node_t *member_node;
  const ctool_c_type_node_t *designator_node;
  ctool_u32 record_base;
  ctool_u32 record_qualifiers;
  ctool_u32 designator_base;
  ctool_u32 designator_qualifiers;
  ctool_status_t status;
  cir_zero(info, (ctool_u32)sizeof(*info));
  if (member_expression->kind != CTOOL_C_EXPRESSION_MEMBER ||
      member_expression->child_count != 1u ||
      member_expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      member_expression->conversion != CTOOL_C_CONVERSION_NONE ||
      member_expression->computation_type != CTOOL_C_TYPE_NONE ||
      member_expression->reference >= context->unit->graph.member_count ||
      member_expression->reference >= context->unit->layout.member_count) {
    return cir_invalid_unit(context, location);
  }
  status = cir_expression_child(context, member_expression_index,
                                member_expression, 0u,
                                &info->record_child);
  if (status != CTOOL_OK) {
    return status;
  }
  info->record_expression =
      &context->unit->expressions[info->record_child];
  info->member =
      &context->unit->graph.members[member_expression->reference];
  if (cir_underlying_type(context, info->record_expression->type,
                          &record_base, &record_qualifiers,
                          &record_node) == CTOOL_FALSE ||
      cir_underlying_type(context, info->member->type,
                          &info->member_base,
                          &info->member_qualifiers,
                          &member_node) == CTOOL_FALSE ||
      cir_underlying_type(context, member_expression->type,
                          &designator_base, &designator_qualifiers,
                          &designator_node) == CTOOL_FALSE ||
      record_node->kind != CTOOL_C_TYPE_RECORD ||
      record_node->record_complete == CTOOL_FALSE ||
      member_expression->reference < record_node->first_member ||
      member_expression->reference - record_node->first_member >=
          record_node->member_count ||
      info->member_base != designator_base ||
      designator_qualifiers !=
          (info->member_qualifiers | record_qualifiers |
           record_node->qualifiers)) {
    return cir_invalid_unit(context, location);
  }
  (void)member_node;
  (void)designator_node;
  if (record_base >= context->unit->layout.type_count ||
      info->member->type >= context->unit->layout.type_count ||
      member_expression->type >= context->unit->layout.type_count) {
    return cir_invalid_unit(context, location);
  }
  info->record_layout = &context->unit->layout.types[record_base];
  info->member_type_layout =
      &context->unit->layout.types[info->member->type];
  info->designator_layout =
      &context->unit->layout.types[member_expression->type];
  info->member_layout =
      &context->unit->layout.members[member_expression->reference];
  if (info->record_layout->is_object == CTOOL_FALSE ||
      info->record_layout->is_complete_object == CTOOL_FALSE ||
      info->member_type_layout->is_object == CTOOL_FALSE ||
      info->member_type_layout->is_complete_object == CTOOL_FALSE ||
      info->designator_layout->is_object == CTOOL_FALSE ||
      info->designator_layout->is_complete_object == CTOOL_FALSE ||
      info->member_layout->size != info->member_type_layout->size ||
      info->designator_layout->size != info->member_type_layout->size ||
      info->member_layout->byte_offset > info->record_layout->size) {
    return cir_invalid_unit(context, location);
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_member_record_address(
    cir_context_t *context, const cir_member_info_t *info,
    const ctool_c_pp_location_t *location, ctool_u32 depth) {
  cir_stack_entry_t address;
  ctool_status_t status = cir_lower_expression(
      context, info->record_child, depth + 1u);
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (address.kind != CIR_STACK_ADDRESS ||
      address.type != info->record_expression->type) {
    return cir_invalid_unit(context, location);
  }
  return CTOOL_OK;
}

static ctool_status_t cir_require_represented_bit_field(
    cir_context_t *context, ctool_u32 member_expression_index,
    const ctool_c_expression_t *member_expression, ctool_u32 value_type,
    const ctool_c_pp_location_t *location, cir_member_info_t *info) {
  const ctool_c_type_node_t *value_node;
  const ctool_c_type_layout_t *value_layout;
  ctool_u32 value_base;
  ctool_u32 value_qualifiers;
  ctool_status_t status = cir_validate_member(
      context, member_expression_index, member_expression, location, info);
  if (status != CTOOL_OK) {
    return status;
  }
  if (cir_member_info_complete(info) == CTOOL_FALSE) {
    return cir_invalid_unit(context, location);
  }
  if (cir_underlying_type(context, value_type, &value_base,
                          &value_qualifiers, &value_node) == CTOOL_FALSE ||
      info->member->is_bit_field != CTOOL_TRUE ||
      info->member->bit_width == 0u || info->member_base != value_base ||
      value_qualifiers != 0u) {
    return cir_invalid_unit(context, location);
  }
  (void)value_node;
  if (value_type >= context->unit->layout.type_count) {
    return cir_invalid_unit(context, location);
  }
  value_layout = &context->unit->layout.types[value_type];
  if (value_layout->is_object == CTOOL_FALSE ||
      value_layout->is_complete_object == CTOOL_FALSE ||
      value_layout->size != info->member_type_layout->size ||
      info->member_layout->bit_width != info->member->bit_width ||
      info->member_layout->bit_width == 0u ||
      info->member_layout->size > 0xffffffffu / 8u ||
      info->member_layout->bit_offset >= info->member_layout->size * 8u ||
      info->member_layout->bit_width >
          info->member_layout->size * 8u -
              info->member_layout->bit_offset) {
    return cir_invalid_unit(context, location);
  }
  if (cir_type_has_atomic_qualification(
          context, member_expression->type) == CTOOL_TRUE ||
      info->member_layout->size >
          info->record_layout->size - info->member_layout->byte_offset) {
    return cir_unsupported_type(context, location);
  }
  if (cir_type_is_i32_integer(context, info->member->type) == CTOOL_FALSE ||
      cir_type_is_i32_integer(context, member_expression->type) ==
          CTOOL_FALSE ||
      cir_type_is_i32_integer(context, value_type) == CTOOL_FALSE) {
    return cir_unsupported_type(context, location);
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_bit_field_load(
    cir_context_t *context, const ctool_c_expression_t *conversion,
    ctool_u32 member_expression_index,
    const ctool_c_expression_t *member_expression, ctool_u32 depth) {
  cir_member_info_t info;
  ctool_status_t status;
  if (conversion->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      conversion->computation_type != CTOOL_C_TYPE_NONE) {
    return cir_invalid_unit(context, &conversion->location);
  }
  status = cir_require_represented_bit_field(
      context, member_expression_index, member_expression, conversion->type,
      &conversion->location, &info);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cir_lower_member_record_address(
      context, &info, &conversion->location, depth);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_BIT_FIELD_LOAD, conversion->type,
      info.record_expression->type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_LVALUE_TO_VALUE, member_expression->reference, 0u,
      &conversion->location, &conversion->physical_location,
      (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, conversion->type);
}

static ctool_status_t cir_lower_bit_field_store_value(
    cir_context_t *context, const ctool_c_expression_t *assignment,
    ctool_u32 member_expression_index,
    const ctool_c_expression_t *member_expression, ctool_u32 right_child,
    ctool_u32 depth) {
  cir_stack_entry_t address;
  cir_member_info_t info;
  cir_stack_entry_t value;
  ctool_u32 base_depth = context->stack_depth;
  ctool_status_t status = cir_require_represented_bit_field(
      context, member_expression_index, member_expression, assignment->type,
      &assignment->location, &info);
  if (status != CTOOL_OK) {
    return status;
  }
  if (cir_scalar_value_types_match(context, member_expression->type,
                                   assignment->type) == CTOOL_FALSE ||
      cir_scalar_value_types_match(
          context, context->unit->expressions[right_child].type,
          assignment->type) == CTOOL_FALSE) {
    return cir_invalid_unit(context, &assignment->location);
  }
  status = cir_lower_member_record_address(
      context, &info, &assignment->location, depth);
  if (status == CTOOL_OK && context->stack_depth != base_depth) {
    return cir_invalid_unit(context, &assignment->location);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_ADDRESS,
                      info.record_expression->type);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_expression(context, right_child, depth + 1u);
  }
  if (status == CTOOL_OK &&
      (cir_add_overflows(base_depth, 2u) == CTOOL_TRUE ||
       context->stack_depth != base_depth + 2u)) {
    return cir_invalid_unit(context, &assignment->location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &value);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (address.kind != CIR_STACK_ADDRESS ||
      address.type != info.record_expression->type ||
      value.kind != CIR_STACK_VALUE ||
      cir_scalar_value_types_match(context, value.type, assignment->type) ==
          CTOOL_FALSE) {
    return cir_invalid_unit(context, &assignment->location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_VALUE,
      assignment->type, info.record_expression->type,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
      member_expression->reference, 0u, &assignment->location,
      &assignment->physical_location, (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, assignment->type);
}

static ctool_status_t cir_lower_conversion(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  cir_stack_entry_t source;
  ctool_u32 child;
  ctool_status_t status = cir_expression_child(
      context, expression_index, expression, 0u, &child);
  if (status != CTOOL_OK) {
    return status;
  }
  if (expression->conversion == CTOOL_C_CONVERSION_LVALUE_TO_VALUE &&
      context->unit->expressions[child].kind ==
          CTOOL_C_EXPRESSION_MEMBER &&
      context->unit->expressions[child].reference <
          context->unit->graph.member_count &&
      context->unit
              ->graph.members[context->unit->expressions[child].reference]
              .is_bit_field == CTOOL_TRUE) {
    return cir_lower_bit_field_load(
        context, expression, child, &context->unit->expressions[child],
        depth);
  }
  status = cir_lower_expression(context, child, depth + 1u);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cir_pop(context, &source);
  if (status != CTOOL_OK) {
    return status;
  }
  if (expression->conversion == CTOOL_C_CONVERSION_LVALUE_TO_VALUE) {
    if (cir_type_has_atomic_qualification(context, source.type) ==
        CTOOL_TRUE) {
      return cir_unsupported_type(context, &expression->location);
    }
    if (source.kind != CIR_STACK_ADDRESS ||
        source.type != context->unit->expressions[child].type) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (cir_scalar_value_types_match(context, source.type,
                                     expression->type) == CTOOL_FALSE) {
      status = cir_require_structure_value(
          context, source.type, &expression->location);
      if (status == CTOOL_OK) {
        status = cir_require_structure_value(
            context, expression->type, &expression->location);
      }
      if (status != CTOOL_OK) {
        return status;
      }
      if (cir_structure_value_types_match(
              context, source.type, expression->type) == CTOOL_FALSE) {
        return cir_invalid_unit(context, &expression->location);
      }
    }
    if (cir_type_is_value_scalar(context, expression->type) ==
            CTOOL_FALSE &&
        cir_type_is_structure_value(context, expression->type) ==
            CTOOL_FALSE) {
      return cir_unsupported_conversion(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_LOAD, expression->type, source.type,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, expression->conversion,
        CTOOL_C_AST_NONE, 0u, &expression->location,
        &expression->physical_location, (ctool_u32 *)0);
  } else if (expression->conversion ==
             CTOOL_C_CONVERSION_FUNCTION_TO_POINTER) {
    const ctool_c_type_node_t *pointer =
        cir_unwrapped_type(context, expression->type);
    if (source.kind != CIR_STACK_FUNCTION_DESIGNATOR ||
        source.type != context->unit->expressions[child].type ||
        pointer == (const ctool_c_type_node_t *)0 ||
        pointer->kind != CTOOL_C_TYPE_POINTER ||
        pointer->referenced_type != source.type ||
        cir_type_is_i32_function_pointer(context, expression->type) ==
            CTOOL_FALSE) {
      return cir_unsupported_conversion(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_FUNCTION_TO_POINTER,
        expression->type, source.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        expression->conversion, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  } else if (expression->conversion ==
             CTOOL_C_CONVERSION_ARRAY_TO_POINTER) {
    if (source.kind != CIR_STACK_ADDRESS ||
        source.type != context->unit->expressions[child].type ||
        cir_array_decay_types_match(context, source.type,
                                    expression->type) == CTOOL_FALSE) {
      return cir_unsupported_conversion(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_ARRAY_TO_POINTER,
        expression->type, source.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        expression->conversion, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  } else if (expression->conversion ==
             CTOOL_C_CONVERSION_FLOAT_PROMOTION) {
    if (source.kind != CIR_STACK_VALUE ||
        source.type != context->unit->expressions[child].type ||
        cir_float_promotion_is_valid(
            context, source.type, expression->type) == CTOOL_FALSE) {
      return cir_invalid_unit(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_CONVERT, expression->type,
        source.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        expression->conversion, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  } else if (expression->conversion == CTOOL_C_CONVERSION_QUALIFICATION ||
             expression->conversion ==
                 CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
             expression->conversion ==
                 CTOOL_C_CONVERSION_USUAL_ARITHMETIC ||
             expression->conversion == CTOOL_C_CONVERSION_ASSIGNMENT ||
             expression->conversion == CTOOL_C_CONVERSION_POINTER) {
    ctool_bool integer_types =
        cir_type_is_value_integer(context, source.type) == CTOOL_TRUE &&
                cir_type_is_value_integer(context, expression->type) ==
                    CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    ctool_bool integer_conversion = cir_integer_conversion_is_valid(
        context, source.type, expression->type, expression->conversion);
    ctool_bool pointer_conversion =
        cir_type_is_i32_pointer_value(context, source.type) == CTOOL_TRUE &&
                cir_type_is_i32_pointer_value(context, expression->type) ==
                    CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if (integer_types == CTOOL_TRUE && integer_conversion == CTOOL_FALSE) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (source.kind != CIR_STACK_VALUE ||
        (integer_conversion == CTOOL_FALSE &&
         pointer_conversion == CTOOL_FALSE) ||
        (pointer_conversion == CTOOL_TRUE &&
         cir_pointer_conversion_is_valid(
             context, source.type, expression->type,
             expression->conversion) == CTOOL_FALSE)) {
      return cir_unsupported_conversion(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_CONVERT, expression->type,
        source.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        expression->conversion, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  } else if (expression->conversion ==
             CTOOL_C_CONVERSION_NULL_POINTER) {
    if (source.kind != CIR_STACK_VALUE ||
        cir_type_is_i32_integer(context, source.type) == CTOOL_FALSE ||
        cir_type_is_i32_pointer_value(context, expression->type) ==
            CTOOL_FALSE) {
      return cir_unsupported_conversion(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_CONVERT, expression->type,
        source.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        expression->conversion, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  } else {
    return cir_unsupported_conversion(context, &expression->location);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, expression->type);
}

static ctool_status_t cir_lower_cast(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  cir_stack_entry_t source;
  ctool_u32 base_depth = context->stack_depth;
  ctool_u32 child;
  ctool_u32 source_type;
  ctool_status_t status;
  if (expression->reference != CTOOL_C_AST_NONE ||
      expression->conversion != CTOOL_C_CONVERSION_NONE ||
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      expression->computation_type != CTOOL_C_TYPE_NONE) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_expression_child(
      context, expression_index, expression, 0u, &child);
  if (status != CTOOL_OK) {
    return status;
  }
  if (context->unit->expressions[child].type >=
          context->unit->layout.type_count ||
      expression->type >= context->unit->layout.type_count) {
    return cir_invalid_unit(context, &expression->location);
  }
  source_type = context->unit->expressions[child].type;
  if (cir_type_is_void(context, expression->type) == CTOOL_TRUE) {
    if (cir_type_is_void(context, source_type) == CTOOL_FALSE &&
        cir_type_is_value_scalar(context, source_type) == CTOOL_FALSE) {
      status = cir_require_structure_value(
          context, source_type, &expression->location);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    status = cir_lower_expression(context, child, depth + 1u);
    if (status != CTOOL_OK) {
      return status;
    }
    if (cir_type_is_void(context, source_type) == CTOOL_TRUE) {
      return context->stack_depth == base_depth
                 ? CTOOL_OK
                 : cir_invalid_unit(context, &expression->location);
    }
    if (context->stack_depth != base_depth + 1u) {
      return cir_invalid_unit(context, &expression->location);
    }
    status = cir_pop(context, &source);
    if (status != CTOOL_OK) {
      return status;
    }
    if (source.kind != CIR_STACK_VALUE || source.type != source_type ||
        context->stack_depth != base_depth) {
      return cir_invalid_unit(context, &expression->location);
    }
    return cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_DISCARD, CTOOL_C_TYPE_NONE,
        source.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (cir_type_is_i32_function_pointer(
          context, source_type) == CTOOL_TRUE ||
      cir_type_is_i32_function_pointer(context, expression->type) ==
          CTOOL_TRUE) {
    return cir_unsupported_conversion(context, &expression->location);
  }
  if (!((cir_type_is_i32_scalar(
             context, context->unit->expressions[child].type) == CTOOL_TRUE &&
         cir_type_is_i32_scalar(context, expression->type) == CTOOL_TRUE) ||
        (cir_type_is_represented_integer(
             context, source_type) == CTOOL_TRUE &&
         cir_type_is_represented_integer(context, expression->type) ==
             CTOOL_TRUE) ||
        (cir_type_is_value_integer(context, source_type) == CTOOL_TRUE &&
         cir_type_is_value_integer(context, expression->type) ==
             CTOOL_TRUE))) {
    return cir_unsupported_type(context, &expression->location);
  }
  status = cir_lower_expression(context, child, depth + 1u);
  if (status == CTOOL_OK) {
    status = cir_pop(context, &source);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (source.kind != CIR_STACK_VALUE ||
      source.type != context->unit->expressions[child].type) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_CONVERT, expression->type, source.type,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
      CTOOL_C_AST_NONE, 0u, &expression->location,
      &expression->physical_location, (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, expression->type);
}

static ctool_status_t cir_append_pointer_binary(
    cir_context_t *context, const cir_stack_entry_t *left,
    const cir_stack_entry_t *right, ctool_u32 result_type,
    ctool_c_expression_operator_t operation,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location) {
  const ctool_c_type_node_t *pointer;
  ctool_bool left_is_pointer;
  ctool_bool right_is_pointer;
  ctool_u32 instruction_index = 0u;
  ctool_u32 pointer_type;
  ctool_status_t status;
  if (left->kind != CIR_STACK_VALUE || right->kind != CIR_STACK_VALUE ||
      (operation != CTOOL_C_EXPRESSION_OPERATOR_ADD &&
       operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT)) {
    return cir_invalid_unit(context, location);
  }
  left_is_pointer = cir_type_is_i32_pointer(context, left->type);
  right_is_pointer = cir_type_is_i32_pointer(context, right->type);
  if (left_is_pointer == CTOOL_FALSE && right_is_pointer == CTOOL_FALSE) {
    return cir_invalid_unit(context, location);
  }
  pointer_type = left_is_pointer == CTOOL_TRUE ? left->type : right->type;
  pointer = cir_unwrapped_type(context, pointer_type);
  if (pointer == (const ctool_c_type_node_t *)0 ||
      pointer->referenced_type >= context->unit->layout.type_count ||
      context->unit->layout.types[pointer->referenced_type].is_object ==
          CTOOL_FALSE ||
      context->unit->layout.types[pointer->referenced_type]
              .is_complete_object == CTOOL_FALSE ||
      context->unit->layout.types[pointer->referenced_type].size == 0u) {
    return cir_invalid_unit(context, location);
  }
  if (operation == CTOOL_C_EXPRESSION_OPERATOR_ADD) {
    ctool_u32 integer_type = left_is_pointer == CTOOL_TRUE
                                 ? right->type
                                 : left->type;
    if (left_is_pointer == right_is_pointer ||
        integer_type >= context->unit->layout.type_count ||
        context->unit->layout.types[integer_type].is_integer == CTOOL_FALSE ||
        cir_pointer_value_types_match(context, pointer_type, result_type) ==
            CTOOL_FALSE) {
      return cir_invalid_unit(context, location);
    }
    if (cir_type_is_i32_integer(context, integer_type) == CTOOL_FALSE) {
      return cir_unsupported_type(context, location);
    }
  } else if (left_is_pointer == CTOOL_FALSE) {
    return cir_invalid_unit(context, location);
  } else if (right_is_pointer == CTOOL_TRUE) {
    const ctool_c_type_node_t *right_pointer =
        cir_unwrapped_type(context, right->type);
    if (right_pointer == (const ctool_c_type_node_t *)0 ||
        right_pointer->referenced_type >= context->unit->layout.type_count ||
        context->unit->layout.types[right_pointer->referenced_type]
                .is_complete_object == CTOOL_FALSE ||
        context->unit->layout.types[right_pointer->referenced_type].size ==
            0u ||
        cir_type_is_plain_signed_int(context, result_type) == CTOOL_FALSE ||
        cir_pointer_comparison_types_match(context, left->type, right->type,
                                           CTOOL_TRUE) == CTOOL_FALSE) {
      return cir_invalid_unit(context, location);
    }
  } else {
    if (right->type >= context->unit->layout.type_count ||
        context->unit->layout.types[right->type].is_integer == CTOOL_FALSE ||
        cir_pointer_value_types_match(context, left->type, result_type) ==
            CTOOL_FALSE) {
      return cir_invalid_unit(context, location);
    }
    if (cir_type_is_i32_integer(context, right->type) == CTOOL_FALSE) {
      return cir_unsupported_type(context, location);
    }
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_POINTER_BINARY, result_type,
      left->type, operation, CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      location, physical_location, &instruction_index);
  if (status != CTOOL_OK) {
    return status;
  }
  if (context->instructions != (ctool_c_ir_instruction_t *)0) {
    context->instructions[instruction_index].reference = right->type;
  }
  return cir_push(context, CIR_STACK_VALUE, result_type);
}

static ctool_status_t cir_lower_binary(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  cir_stack_entry_t left;
  cir_stack_entry_t right;
  ctool_bool is_comparison;
  ctool_bool is_pointer_comparison;
  ctool_bool is_pointer_arithmetic;
  ctool_bool is_relational_comparison;
  ctool_bool is_shift;
  ctool_bool is_bitwise_xor;
  ctool_bool is_wide_integer_binary;
  ctool_bool is_wide_integer_comparison;
  ctool_u32 left_child;
  ctool_u32 right_child;
  ctool_status_t status;
  if (expression->reference != CTOOL_C_AST_NONE ||
      expression->conversion != CTOOL_C_CONVERSION_NONE ||
      expression->computation_type != CTOOL_C_TYPE_NONE) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_expression_child(
      context, expression_index, expression, 0u, &left_child);
  if (status == CTOOL_OK) {
    status = cir_expression_child(context, expression_index, expression, 1u,
                                  &right_child);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_expression(context, left_child, depth + 1u);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_expression(context, right_child, depth + 1u);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &right);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &left);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  is_shift =
      (expression->operation == CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT ||
       expression->operation == CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT)
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  is_comparison =
      expression->operation == CTOOL_C_EXPRESSION_OPERATOR_LESS ||
              expression->operation ==
                  CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL ||
              expression->operation == CTOOL_C_EXPRESSION_OPERATOR_GREATER ||
              expression->operation ==
                  CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL ||
              expression->operation == CTOOL_C_EXPRESSION_OPERATOR_EQUAL ||
              expression->operation == CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  is_relational_comparison =
      expression->operation == CTOOL_C_EXPRESSION_OPERATOR_LESS ||
              expression->operation ==
                  CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL ||
              expression->operation == CTOOL_C_EXPRESSION_OPERATOR_GREATER ||
              expression->operation ==
                  CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  is_pointer_comparison =
      is_comparison == CTOOL_TRUE &&
              cir_type_is_i32_pointer_value(context, left.type) ==
                  CTOOL_TRUE &&
              cir_type_is_i32_pointer_value(context, right.type) ==
                  CTOOL_TRUE
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  is_pointer_arithmetic =
      (expression->operation == CTOOL_C_EXPRESSION_OPERATOR_ADD ||
       expression->operation == CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT) &&
              (cir_type_is_i32_pointer(context, left.type) == CTOOL_TRUE ||
               cir_type_is_i32_pointer(context, right.type) == CTOOL_TRUE)
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  is_bitwise_xor = expression->operation ==
                           CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR
                       ? CTOOL_TRUE
                       : CTOOL_FALSE;
  is_wide_integer_binary =
      cir_type_is_wide_integer(context, expression->type) == CTOOL_TRUE &&
              cir_type_is_wide_integer(context, left.type) == CTOOL_TRUE &&
              ((is_shift == CTOOL_TRUE && expression->type == left.type &&
                cir_type_is_i32_integer(context, right.type) ==
                    CTOOL_TRUE) ||
               ((expression->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND ||
                 expression->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR ||
                 is_bitwise_xor == CTOOL_TRUE ||
                 expression->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_ADD ||
                 expression->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
                 expression->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY ||
                 expression->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_DIVIDE ||
                 expression->operation ==
                     CTOOL_C_EXPRESSION_OPERATOR_REMAINDER) &&
                expression->type == left.type && left.type == right.type &&
                cir_type_is_wide_integer(context, right.type) ==
                    CTOOL_TRUE))
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  is_wide_integer_comparison =
      is_comparison == CTOOL_TRUE &&
              cir_type_is_wide_integer(context, left.type) == CTOOL_TRUE &&
              cir_type_is_wide_integer(context, right.type) == CTOOL_TRUE
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  if (is_pointer_arithmetic == CTOOL_TRUE) {
    return cir_append_pointer_binary(
        context, &left, &right, expression->type, expression->operation,
        &expression->location, &expression->physical_location);
  }
  if ((expression->operation == CTOOL_C_EXPRESSION_OPERATOR_DIVIDE ||
       expression->operation == CTOOL_C_EXPRESSION_OPERATOR_REMAINDER) &&
      cir_type_is_wide_integer(context, left.type) == CTOOL_TRUE &&
      cir_type_is_wide_integer(context, right.type) == CTOOL_TRUE &&
      left.type == right.type && expression->type != left.type) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (left.kind != CIR_STACK_VALUE || right.kind != CIR_STACK_VALUE ||
      (is_pointer_comparison == CTOOL_FALSE &&
       is_wide_integer_binary == CTOOL_FALSE &&
       is_wide_integer_comparison == CTOOL_FALSE &&
       cir_type_is_i32_integer(context, expression->type) == CTOOL_FALSE) ||
      (is_pointer_comparison == CTOOL_FALSE &&
       is_wide_integer_binary == CTOOL_FALSE &&
       is_wide_integer_comparison == CTOOL_FALSE &&
       (cir_type_is_i32_integer(context, left.type) == CTOOL_FALSE ||
        cir_type_is_i32_integer(context, right.type) == CTOOL_FALSE))) {
    return cir_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE, &expression->location,
        "CupidC IR lowering does not yet support this value type");
  }
  if (is_pointer_comparison == CTOOL_TRUE) {
    if (cir_type_is_plain_signed_int(context, expression->type) ==
            CTOOL_FALSE ||
        cir_pointer_comparison_types_match(
            context, left.type, right.type, is_relational_comparison) ==
            CTOOL_FALSE) {
      return cir_invalid_unit(context, &expression->location);
    }
  } else if (is_wide_integer_comparison == CTOOL_TRUE) {
    if (cir_type_is_plain_signed_int(context, expression->type) ==
            CTOOL_FALSE ||
        left.type != right.type) {
      return cir_invalid_unit(context, &expression->location);
    }
  } else if (is_shift == CTOOL_TRUE) {
    if (expression->type != left.type) {
      return cir_invalid_unit(context, &expression->location);
    }
  } else if (left.type != right.type) {
    return cir_unsupported_type(context, &expression->location);
  } else if (is_bitwise_xor == CTOOL_TRUE &&
             expression->type != left.type) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (expression->operation != CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_DIVIDE &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_REMAINDER &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_ADD &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_LESS &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_GREATER &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_EQUAL &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR &&
      is_bitwise_xor == CTOOL_FALSE &&
      is_shift == CTOOL_FALSE) {
    return cir_unsupported_expression(context, &expression->location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_BINARY, expression->type, left.type,
      expression->operation, CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &expression->location, &expression->physical_location,
      (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, expression->type);
}

static ctool_status_t cir_lower_unary(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  cir_stack_entry_t operand;
  ctool_bool logical_not;
  ctool_u32 child;
  ctool_status_t status;
  if (expression->reference != CTOOL_C_AST_NONE ||
      expression->conversion != CTOOL_C_CONVERSION_NONE ||
      expression->computation_type != CTOOL_C_TYPE_NONE) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_expression_child(
      context, expression_index, expression, 0u, &child);
  if (status == CTOOL_OK &&
      expression->operation == CTOOL_C_EXPRESSION_OPERATOR_ADDRESS) {
    const ctool_c_type_node_t *pointer;
    const ctool_c_type_node_t *addressed;
    ctool_bool function_address;
    ctool_u32 child_type = context->unit->expressions[child].type;
    status = cir_lower_expression(context, child, depth + 1u);
    if (status == CTOOL_OK) {
      status = cir_pop(context, &operand);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    pointer = cir_unwrapped_type(context, expression->type);
    addressed = cir_unwrapped_type(context, child_type);
    function_address =
        addressed != (const ctool_c_type_node_t *)0 &&
                addressed->kind == CTOOL_C_TYPE_FUNCTION
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if (operand.type != child_type ||
        ((function_address == CTOOL_TRUE &&
          operand.kind != CIR_STACK_FUNCTION_DESIGNATOR) ||
         (function_address == CTOOL_FALSE &&
          operand.kind != CIR_STACK_ADDRESS)) ||
        (function_address == CTOOL_FALSE &&
         (child_type >= context->unit->layout.type_count ||
          context->unit->layout.types[child_type].is_object == CTOOL_FALSE ||
          context->unit->layout.types[child_type].is_complete_object ==
              CTOOL_FALSE)) ||
        cir_type_is_i32_pointer_value(context, expression->type) ==
            CTOOL_FALSE ||
        pointer == (const ctool_c_type_node_t *)0 ||
        pointer->referenced_type != child_type) {
      return cir_invalid_unit(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_ADDRESS_OF, expression->type,
        child_type, expression->operation, CTOOL_C_CONVERSION_NONE,
        CTOOL_C_AST_NONE, 0u, &expression->location,
        &expression->physical_location, (ctool_u32 *)0);
    if (status != CTOOL_OK) {
      return status;
    }
    return cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (status == CTOOL_OK &&
      expression->operation == CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE) {
    const ctool_c_type_node_t *pointer;
    const ctool_c_type_node_t *referent;
    ctool_bool function_designator;
    status = cir_lower_expression(context, child, depth + 1u);
    if (status == CTOOL_OK) {
      status = cir_pop(context, &operand);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    pointer = cir_unwrapped_type(context, operand.type);
    referent = cir_unwrapped_type(context, expression->type);
    function_designator =
        referent != (const ctool_c_type_node_t *)0 &&
                referent->kind == CTOOL_C_TYPE_FUNCTION
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if (operand.kind != CIR_STACK_VALUE ||
        cir_type_is_i32_pointer_value(context, operand.type) == CTOOL_FALSE ||
        pointer == (const ctool_c_type_node_t *)0 ||
        pointer->referenced_type != expression->type ||
        (function_designator == CTOOL_FALSE &&
         cir_type_is_i32_pointer(context, operand.type) == CTOOL_FALSE)) {
      return cir_invalid_unit(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_DEREFERENCE, expression->type,
        operand.type, expression->operation, CTOOL_C_CONVERSION_NONE,
        CTOOL_C_AST_NONE, 0u, &expression->location,
        &expression->physical_location, (ctool_u32 *)0);
    if (status != CTOOL_OK) {
      return status;
    }
    return cir_push(context,
                    function_designator == CTOOL_TRUE
                        ? CIR_STACK_FUNCTION_DESIGNATOR
                        : CIR_STACK_ADDRESS,
                    expression->type);
  }
  logical_not = expression->operation ==
                        CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_NOT
                    ? CTOOL_TRUE
                    : CTOOL_FALSE;
  if (status == CTOOL_OK &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_UNARY_PLUS &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT &&
      logical_not == CTOOL_FALSE) {
    return cir_unsupported_expression(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_expression(context, child, depth + 1u);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &operand);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (operand.kind != CIR_STACK_VALUE) {
    return cir_invalid_unit(context, &expression->location);
  }
  if ((logical_not == CTOOL_FALSE &&
       cir_type_is_value_integer(context, operand.type) == CTOOL_FALSE) ||
      (logical_not == CTOOL_TRUE &&
       cir_type_is_truth_scalar(context, operand.type) ==
           CTOOL_FALSE) ||
      (logical_not == CTOOL_FALSE &&
       cir_type_is_value_integer(context, expression->type) ==
           CTOOL_FALSE) ||
      (logical_not == CTOOL_TRUE &&
       cir_type_is_i32_integer(context, expression->type) == CTOOL_FALSE)) {
    return cir_unsupported_type(context, &expression->location);
  }
  if (operand.type != context->unit->expressions[child].type ||
      (logical_not == CTOOL_FALSE && expression->type != operand.type) ||
      (logical_not == CTOOL_TRUE &&
       cir_type_is_plain_signed_int(context, expression->type) ==
           CTOOL_FALSE)) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_UNARY, expression->type, operand.type,
      expression->operation, CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &expression->location, &expression->physical_location,
      (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, expression->type);
}

static ctool_status_t cir_lower_logical_operand(
    cir_context_t *context, ctool_u32 child, ctool_u32 child_depth,
    ctool_u32 base_depth, const ctool_c_expression_t *expression,
    cir_stack_entry_t *operand_out) {
  ctool_status_t status = cir_lower_expression(context, child, child_depth);
  if (status == CTOOL_OK) {
    status = cir_pop(context, operand_out);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (context->stack_depth != base_depth ||
      operand_out->kind != CIR_STACK_VALUE) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (cir_type_is_truth_scalar(context, operand_out->type) ==
      CTOOL_FALSE) {
    return cir_unsupported_type(context, &expression->location);
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_logical_and(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  cir_stack_entry_t left;
  cir_stack_entry_t right;
  ctool_u32 left_child;
  ctool_u32 right_child;
  ctool_u32 left_zero_branch;
  ctool_u32 right_zero_branch;
  ctool_u32 result_jump;
  ctool_u32 base_depth = context->stack_depth;
  ctool_status_t status;
  if (expression->operation != CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_AND ||
      expression->reference != CTOOL_C_AST_NONE ||
      expression->conversion != CTOOL_C_CONVERSION_NONE ||
      expression->computation_type != CTOOL_C_TYPE_NONE ||
      cir_type_is_i32_integer(context, expression->type) == CTOOL_FALSE) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_expression_child(context, expression_index, expression, 0u,
                                &left_child);
  if (status == CTOOL_OK) {
    status = cir_expression_child(context, expression_index, expression, 1u,
                                  &right_child);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_logical_operand(
        context, left_child, depth + 1u, base_depth, expression, &left);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE,
      left.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &expression->location, &expression->physical_location,
      &left_zero_branch);
  if (status == CTOOL_OK) {
    status = cir_lower_logical_operand(
        context, right_child, depth + 1u, base_depth, expression, &right);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE,
      right.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &expression->location, &expression->physical_location,
      &right_zero_branch);
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_INTEGER, expression->type,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        &result_jump);
  }
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, left_zero_branch,
                                 cir_function_offset(context));
  }
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, right_zero_branch,
                                 cir_function_offset(context));
  }
  if (status != CTOOL_OK) {
    return status;
  }
  context->stack_depth = base_depth;
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_INTEGER, expression->type,
      CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &expression->location, &expression->physical_location,
      (ctool_u32 *)0);
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, result_jump,
                                 cir_function_offset(context));
  }
  return status;
}

static ctool_status_t cir_lower_logical_or(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  cir_stack_entry_t left;
  cir_stack_entry_t right;
  ctool_u32 left_child;
  ctool_u32 right_child;
  ctool_u32 right_branch;
  ctool_u32 left_result_jump;
  ctool_u32 false_branch;
  ctool_u32 right_result_jump;
  ctool_u32 base_depth = context->stack_depth;
  ctool_status_t status;
  if (expression->operation != CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_OR ||
      expression->reference != CTOOL_C_AST_NONE ||
      expression->conversion != CTOOL_C_CONVERSION_NONE ||
      expression->computation_type != CTOOL_C_TYPE_NONE ||
      cir_type_is_i32_integer(context, expression->type) == CTOOL_FALSE) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_expression_child(context, expression_index, expression, 0u,
                                &left_child);
  if (status == CTOOL_OK) {
    status = cir_expression_child(context, expression_index, expression, 1u,
                                  &right_child);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_logical_operand(
        context, left_child, depth + 1u, base_depth, expression, &left);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE,
      left.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &expression->location, &expression->physical_location, &right_branch);
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_INTEGER, expression->type,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        &left_result_jump);
  }
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, right_branch,
                                 cir_function_offset(context));
  }
  if (status != CTOOL_OK) {
    return status;
  }
  context->stack_depth = base_depth;
  status = cir_lower_logical_operand(
      context, right_child, depth + 1u, base_depth, expression, &right);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE,
      right.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &expression->location, &expression->physical_location, &false_branch);
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_INTEGER, expression->type,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        &right_result_jump);
  }
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, false_branch,
                                 cir_function_offset(context));
  }
  if (status != CTOOL_OK) {
    return status;
  }
  context->stack_depth = base_depth;
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_INTEGER, expression->type,
      CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &expression->location, &expression->physical_location,
      (ctool_u32 *)0);
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, left_result_jump,
                                 cir_function_offset(context));
  }
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, right_result_jump,
                                 cir_function_offset(context));
  }
  return status;
}

static ctool_c_expression_operator_t cir_compound_binary_operation(
    ctool_c_expression_operator_t operation) {
  switch (operation) {
  case CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY_ASSIGN:
    return CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY;
  case CTOOL_C_EXPRESSION_OPERATOR_DIVIDE_ASSIGN:
    return CTOOL_C_EXPRESSION_OPERATOR_DIVIDE;
  case CTOOL_C_EXPRESSION_OPERATOR_REMAINDER_ASSIGN:
    return CTOOL_C_EXPRESSION_OPERATOR_REMAINDER;
  case CTOOL_C_EXPRESSION_OPERATOR_ADD_ASSIGN:
    return CTOOL_C_EXPRESSION_OPERATOR_ADD;
  case CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT_ASSIGN:
    return CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT;
  case CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT_ASSIGN:
    return CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT;
  case CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT_ASSIGN:
    return CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT;
  case CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND_ASSIGN:
    return CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND;
  case CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR_ASSIGN:
    return CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR;
  case CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR_ASSIGN:
    return CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR;
  default:
    return CTOOL_C_EXPRESSION_OPERATOR_NONE;
  }
}

static ctool_status_t cir_lower_bit_field_mutation(
    cir_context_t *context, const ctool_c_expression_t *expression,
    ctool_u32 member_expression_index,
    const ctool_c_expression_t *member_expression, ctool_u32 right_child,
    ctool_u32 depth, ctool_c_expression_operator_t operation,
    ctool_bool postfix) {
  cir_stack_entry_t address;
  cir_member_info_t info;
  cir_stack_entry_t left;
  cir_stack_entry_t right;
  cir_stack_entry_t value;
  ctool_u32 base_depth = context->stack_depth;
  ctool_u32 promoted_type;
  ctool_bool update =
      right_child == CTOOL_C_AST_NONE ? CTOOL_TRUE : CTOOL_FALSE;
  ctool_bool shift = operation == CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT ||
                             operation ==
                                 CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT
                         ? CTOOL_TRUE
                         : CTOOL_FALSE;
  ctool_status_t status = cir_require_represented_bit_field(
      context, member_expression_index, member_expression, expression->type,
      &expression->location, &info);
  if (status != CTOOL_OK) {
    return status;
  }
  if (cir_type_has_volatile_qualification(context,
                                          member_expression->type) ==
          CTOOL_TRUE &&
      info.member_layout->bit_width != 32u) {
    return cir_unsupported_type(context, &expression->location);
  }
  if ((update == CTOOL_TRUE &&
       operation != CTOOL_C_EXPRESSION_OPERATOR_ADD &&
       operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT) ||
      cir_integer_value_types_match(context, member_expression->type,
                                    expression->type) == CTOOL_FALSE) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_require_integer_mutation_computation(
      context, expression->type, expression->computation_type,
      &expression->location);
  if (status != CTOOL_OK) {
    return status;
  }
  if (cir_represented_bit_field_promotion_type(
          context, expression->type, info.member->bit_width,
          &promoted_type) == CTOOL_FALSE) {
    return cir_unsupported_type(context, &expression->location);
  }
  if ((update == CTOOL_TRUE || shift == CTOOL_TRUE) &&
      promoted_type != expression->computation_type) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_lower_member_record_address(
      context, &info, &expression->location, depth);
  if (status == CTOOL_OK && context->stack_depth != base_depth) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_ADDRESS,
                      info.record_expression->type);
  }
  if (status == CTOOL_OK) {
    status = cir_duplicate_address(
        context, info.record_expression->type, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status == CTOOL_OK &&
      (address.kind != CIR_STACK_ADDRESS ||
       address.type != info.record_expression->type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_BIT_FIELD_LOAD, expression->type,
        info.record_expression->type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_LVALUE_TO_VALUE, member_expression->reference, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_duplicate_value(
        context, expression->type, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_convert_top_bit_field_promotion(
        context, expression->type, promoted_type, info.member->bit_width,
        member_expression->reference, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_convert_top_integer(
        context, promoted_type, expression->computation_type,
        CTOOL_C_CONVERSION_USUAL_ARITHMETIC, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK && update == CTOOL_FALSE) {
    status = cir_lower_expression(context, right_child, depth + 1u);
  }
  if (status == CTOOL_OK && update == CTOOL_TRUE) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_INTEGER,
        expression->computation_type, CTOOL_C_TYPE_NONE,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
        CTOOL_C_AST_NONE, 1u, &expression->location,
        &expression->physical_location, (ctool_u32 *)0);
  }
  if (status == CTOOL_OK && update == CTOOL_TRUE) {
    status = cir_push(context, CIR_STACK_VALUE,
                      expression->computation_type);
  }
  if (status == CTOOL_OK &&
      (cir_add_overflows(base_depth, postfix == CTOOL_TRUE ? 4u : 3u) ==
           CTOOL_TRUE ||
       context->stack_depth !=
           base_depth + (postfix == CTOOL_TRUE ? 4u : 3u))) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &right);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &left);
  }
  if (status == CTOOL_OK) {
    status = cir_require_mutation_right_operand(
        context, &right, expression->computation_type, shift,
        &expression->location);
  }
  if (status == CTOOL_OK &&
      (left.kind != CIR_STACK_VALUE ||
       left.type != expression->computation_type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_BINARY,
        expression->computation_type, expression->computation_type,
        operation, CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE,
                      expression->computation_type);
  }
  if (status == CTOOL_OK) {
    status = cir_convert_top_integer(
        context, expression->computation_type, expression->type,
        CTOOL_C_CONVERSION_ASSIGNMENT, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &value);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_pop(context, &left);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status == CTOOL_OK &&
      (address.kind != CIR_STACK_ADDRESS ||
       address.type != info.record_expression->type ||
       value.kind != CIR_STACK_VALUE || value.type != expression->type ||
       (postfix == CTOOL_TRUE &&
        (left.kind != CIR_STACK_VALUE || left.type != expression->type)))) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context,
        postfix == CTOOL_TRUE
            ? CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_OLD_VALUE
            : CTOOL_C_IR_INSTRUCTION_BIT_FIELD_STORE_VALUE,
        expression->type, info.record_expression->type,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
        member_expression->reference, 0u, &expression->location,
        &expression->physical_location, (ctool_u32 *)0);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, expression->type);
}

static ctool_status_t cir_lower_pointer_compound_assignment(
    cir_context_t *context, const ctool_c_expression_t *expression,
    const ctool_c_expression_t *left_expression, ctool_u32 left_child,
    ctool_u32 right_child, ctool_u32 depth,
    ctool_c_expression_operator_t operation) {
  cir_stack_entry_t address;
  cir_stack_entry_t left;
  cir_stack_entry_t result;
  cir_stack_entry_t right;
  ctool_u32 base_depth = context->stack_depth;
  ctool_status_t status;
  if (operation != CTOOL_C_EXPRESSION_OPERATOR_ADD &&
      operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (cir_type_is_i32_pointer(context, left_expression->type) == CTOOL_FALSE ||
      cir_type_is_i32_pointer(context, expression->type) == CTOOL_FALSE ||
      cir_type_is_i32_pointer(context, expression->computation_type) ==
          CTOOL_FALSE) {
    return cir_unsupported_type(context, &expression->location);
  }
  if (cir_pointer_value_types_match(context, left_expression->type,
                                    expression->type) == CTOOL_FALSE ||
      cir_pointer_value_types_match(context, expression->type,
                                    expression->computation_type) ==
          CTOOL_FALSE) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_lower_expression(context, left_child, depth + 1u);
  if (status == CTOOL_OK &&
      (cir_add_overflows(base_depth, 1u) == CTOOL_TRUE ||
       context->stack_depth != base_depth + 1u ||
       context->stack[base_depth].kind != CIR_STACK_ADDRESS ||
       context->stack[base_depth].type != left_expression->type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_duplicate_address(
        context, left_expression->type, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status == CTOOL_OK &&
      (address.kind != CIR_STACK_ADDRESS ||
       address.type != left_expression->type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_LOAD, expression->type,
        left_expression->type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_expression(context, right_child, depth + 1u);
  }
  if (status == CTOOL_OK &&
      (cir_add_overflows(base_depth, 3u) == CTOOL_TRUE ||
       context->stack_depth != base_depth + 3u)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &right);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &left);
  }
  if (status == CTOOL_OK) {
    status = cir_append_pointer_binary(
        context, &left, &right, expression->computation_type, operation,
        &expression->location, &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &result);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status == CTOOL_OK &&
      (address.kind != CIR_STACK_ADDRESS ||
       result.kind != CIR_STACK_VALUE ||
       address.type != left_expression->type ||
       result.type != expression->computation_type ||
       cir_pointer_value_types_match(context, expression->type,
                                     result.type) == CTOOL_FALSE)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_STORE_VALUE, expression->type,
        result.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, expression->type);
}

static ctool_status_t cir_lower_compound_assignment(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  const ctool_c_expression_t *left_expression;
  cir_stack_entry_t address;
  cir_stack_entry_t left;
  cir_stack_entry_t right;
  ctool_c_expression_operator_t operation =
      cir_compound_binary_operation(expression->operation);
  ctool_u32 left_child;
  ctool_u32 promoted_type;
  ctool_u32 right_child;
  ctool_u32 base_depth = context->stack_depth;
  ctool_bool shift;
  ctool_status_t status;
  if (operation == CTOOL_C_EXPRESSION_OPERATOR_NONE) {
    return cir_invalid_unit(context, &expression->location);
  }
  shift = operation == CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT ||
                  operation == CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT
              ? CTOOL_TRUE
              : CTOOL_FALSE;
  status = cir_expression_child(context, expression_index, expression, 0u,
                                &left_child);
  if (status == CTOOL_OK) {
    status = cir_expression_child(context, expression_index, expression, 1u,
                                  &right_child);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  left_expression = &context->unit->expressions[left_child];
  if (cir_type_has_atomic_qualification(context, left_expression->type) ==
      CTOOL_TRUE) {
    return cir_unsupported_type(context, &expression->location);
  }
  if (left_expression->kind == CTOOL_C_EXPRESSION_MEMBER &&
      left_expression->reference < context->unit->graph.member_count &&
      context->unit->graph.members[left_expression->reference].is_bit_field ==
          CTOOL_TRUE) {
    return cir_lower_bit_field_mutation(
        context, expression, left_child, left_expression, right_child, depth,
        operation, CTOOL_FALSE);
  }
  if (cir_type_is_i32_pointer(context, left_expression->type) == CTOOL_TRUE) {
    return cir_lower_pointer_compound_assignment(
        context, expression, left_expression, left_child, right_child, depth,
        operation);
  }
  if (cir_type_is_value_integer(context, left_expression->type) ==
          CTOOL_FALSE ||
      cir_type_is_value_integer(context, expression->type) ==
          CTOOL_FALSE) {
    return cir_unsupported_type(context, &expression->location);
  }
  status = cir_require_integer_mutation_computation(
      context, expression->type, expression->computation_type,
      &expression->location);
  if (status != CTOOL_OK) {
    return status;
  }
  if (cir_integer_value_types_match(context, left_expression->type,
                                    expression->type) == CTOOL_FALSE) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (cir_integer_mutation_promotion_type(context, expression->type,
                                          &promoted_type) == CTOOL_FALSE) {
    return cir_unsupported_type(context, &expression->location);
  }
  if (shift == CTOOL_TRUE &&
      promoted_type != expression->computation_type) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_lower_expression(context, left_child, depth + 1u);
  if (status == CTOOL_OK &&
      (cir_add_overflows(base_depth, 1u) == CTOOL_TRUE ||
       context->stack_depth != base_depth + 1u ||
       context->stack[base_depth].kind != CIR_STACK_ADDRESS ||
       context->stack[base_depth].type != left_expression->type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_duplicate_address(
        context, left_expression->type, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status == CTOOL_OK &&
      (address.kind != CIR_STACK_ADDRESS ||
       address.type != left_expression->type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_LOAD, expression->type,
        left_expression->type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (status == CTOOL_OK) {
    status = cir_convert_top_integer(
        context, expression->type, promoted_type,
        CTOOL_C_CONVERSION_INTEGER_PROMOTION, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_convert_top_integer(
        context, promoted_type, expression->computation_type,
        CTOOL_C_CONVERSION_USUAL_ARITHMETIC, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_expression(context, right_child, depth + 1u);
  }
  if (status == CTOOL_OK &&
      (cir_add_overflows(base_depth, 3u) == CTOOL_TRUE ||
       context->stack_depth != base_depth + 3u)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &right);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &left);
  }
  if (status == CTOOL_OK) {
    status = cir_require_mutation_right_operand(
        context, &right, expression->computation_type, shift,
        &expression->location);
  }
  if (status == CTOOL_OK &&
      (left.kind != CIR_STACK_VALUE ||
       left.type != expression->computation_type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_BINARY,
        expression->computation_type, expression->computation_type,
        operation, CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE,
                      expression->computation_type);
  }
  if (status == CTOOL_OK) {
    status = cir_convert_top_integer(
        context, expression->computation_type, expression->type,
        CTOOL_C_CONVERSION_ASSIGNMENT, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &right);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status == CTOOL_OK &&
      (address.kind != CIR_STACK_ADDRESS ||
       right.kind != CIR_STACK_VALUE ||
       address.type != left_expression->type ||
       right.type != expression->type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_STORE_VALUE, expression->type,
        right.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, expression->type);
}

static ctool_status_t cir_lower_assignment(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  cir_stack_entry_t address;
  cir_stack_entry_t value;
  ctool_u32 left_child;
  ctool_u32 right_child;
  ctool_u32 base_depth = context->stack_depth;
  ctool_status_t status;
  if (expression->reference != CTOOL_C_AST_NONE ||
      expression->conversion != CTOOL_C_CONVERSION_NONE ||
      expression->computation_type >= context->unit->graph.type_count) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (expression->operation != CTOOL_C_EXPRESSION_OPERATOR_ASSIGN) {
    if (expression->operation >=
            CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY_ASSIGN &&
        expression->operation <=
            CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR_ASSIGN) {
      return cir_lower_compound_assignment(
          context, expression_index, expression, depth);
    }
    return cir_invalid_unit(context, &expression->location);
  }
  if (expression->computation_type != expression->type) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (cir_type_is_value_scalar(context, expression->type) ==
      CTOOL_FALSE) {
    status = cir_require_structure_value(
        context, expression->type, &expression->location);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  status = cir_expression_child(context, expression_index, expression, 0u,
                                &left_child);
  if (status == CTOOL_OK) {
    status = cir_expression_child(context, expression_index, expression, 1u,
                                  &right_child);
  }
  if (status == CTOOL_OK &&
      cir_type_has_atomic_qualification(
          context, context->unit->expressions[left_child].type) ==
          CTOOL_TRUE) {
    return cir_unsupported_type(context, &expression->location);
  }
  if (status == CTOOL_OK &&
      context->unit->expressions[left_child].kind ==
          CTOOL_C_EXPRESSION_MEMBER &&
      context->unit->expressions[left_child].reference <
          context->unit->graph.member_count &&
      context->unit
              ->graph
              .members[context->unit->expressions[left_child].reference]
              .is_bit_field == CTOOL_TRUE) {
    return cir_lower_bit_field_store_value(
        context, expression, left_child,
        &context->unit->expressions[left_child], right_child, depth);
  }
  if (status == CTOOL_OK &&
      cir_type_is_value_scalar(context, expression->type) ==
          CTOOL_FALSE) {
    status = cir_require_structure_value(
        context, context->unit->expressions[left_child].type,
        &expression->location);
    if (status == CTOOL_OK) {
      status = cir_require_structure_value(
          context, context->unit->expressions[right_child].type,
          &expression->location);
    }
    if (status == CTOOL_OK &&
        (cir_structure_value_types_match(
             context, context->unit->expressions[left_child].type,
             expression->type) == CTOOL_FALSE ||
         cir_structure_value_types_match(
             context, context->unit->expressions[right_child].type,
             expression->type) == CTOOL_FALSE)) {
      return cir_invalid_unit(context, &expression->location);
    }
  }
  if (status == CTOOL_OK) {
    status = cir_lower_expression(context, left_child, depth + 1u);
  }
  if (status == CTOOL_OK &&
      (cir_add_overflows(base_depth, 1u) == CTOOL_TRUE ||
       context->stack_depth != base_depth + 1u ||
       context->stack[base_depth].kind != CIR_STACK_ADDRESS)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_expression(context, right_child, depth + 1u);
  }
  if (status == CTOOL_OK &&
      (cir_add_overflows(base_depth, 2u) == CTOOL_TRUE ||
       context->stack_depth != base_depth + 2u)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &value);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (address.kind != CIR_STACK_ADDRESS || value.kind != CIR_STACK_VALUE ||
      ((cir_type_is_value_scalar(context, expression->type) ==
            CTOOL_TRUE &&
        (cir_type_is_value_scalar(context, address.type) ==
             CTOOL_FALSE ||
         cir_type_is_value_scalar(context, value.type) == CTOOL_FALSE ||
         cir_scalar_value_types_match(context, address.type,
                                      expression->type) == CTOOL_FALSE ||
         cir_scalar_value_types_match(context, value.type,
                                      expression->type) == CTOOL_FALSE)) ||
       (cir_type_is_structure_value(context, expression->type) == CTOOL_TRUE &&
        (cir_structure_value_types_match(
             context, address.type, expression->type) == CTOOL_FALSE ||
         cir_structure_value_types_match(
             context, value.type, expression->type) == CTOOL_FALSE)))) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_STORE_VALUE, expression->type,
      value.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &expression->location, &expression->physical_location,
      (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, expression->type);
}

static ctool_status_t cir_lower_pointer_update(
    cir_context_t *context, const ctool_c_expression_t *expression,
    const ctool_c_expression_t *operand_expression, ctool_u32 child,
    ctool_u32 depth, ctool_c_expression_operator_t update_operation,
    ctool_bool postfix) {
  cir_stack_entry_t address;
  cir_stack_entry_t left;
  cir_stack_entry_t result;
  cir_stack_entry_t right;
  ctool_u32 base_depth = context->stack_depth;
  ctool_u32 integer_type = cir_plain_signed_int_type(context);
  ctool_status_t status;
  if (integer_type == CTOOL_C_TYPE_NONE ||
      cir_type_is_i32_pointer(context, operand_expression->type) ==
          CTOOL_FALSE ||
      cir_type_is_i32_pointer(context, expression->type) == CTOOL_FALSE ||
      cir_type_is_i32_pointer(context, expression->computation_type) ==
          CTOOL_FALSE ||
      (update_operation != CTOOL_C_EXPRESSION_OPERATOR_ADD &&
       update_operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (cir_pointer_value_types_match(context, operand_expression->type,
                                    expression->type) == CTOOL_FALSE ||
      cir_pointer_value_types_match(context, expression->type,
                                    expression->computation_type) ==
          CTOOL_FALSE) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_lower_expression(context, child, depth + 1u);
  if (status == CTOOL_OK &&
      (cir_add_overflows(base_depth, 1u) == CTOOL_TRUE ||
       context->stack_depth != base_depth + 1u ||
       context->stack[base_depth].kind != CIR_STACK_ADDRESS ||
       context->stack[base_depth].type != operand_expression->type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_duplicate_address(
        context, operand_expression->type, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status == CTOOL_OK &&
      (address.kind != CIR_STACK_ADDRESS ||
       address.type != operand_expression->type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_LOAD, expression->type,
        operand_expression->type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_INTEGER, integer_type,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, integer_type);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &right);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &left);
  }
  if (status == CTOOL_OK) {
    status = cir_append_pointer_binary(
        context, &left, &right, expression->computation_type,
        update_operation, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &result);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status == CTOOL_OK &&
      (address.kind != CIR_STACK_ADDRESS ||
       result.kind != CIR_STACK_VALUE ||
       address.type != operand_expression->type ||
       result.type != expression->computation_type ||
       cir_pointer_value_types_match(context, expression->type,
                                     result.type) == CTOOL_FALSE)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_STORE_VALUE, expression->type,
        result.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_INTEGER, integer_type,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_push(context, CIR_STACK_VALUE, integer_type);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_pop(context, &right);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_pop(context, &left);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_append_pointer_binary(
        context, &left, &right, expression->type,
        update_operation == CTOOL_C_EXPRESSION_OPERATOR_ADD
            ? CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT
            : CTOOL_C_EXPRESSION_OPERATOR_ADD,
        &expression->location, &expression->physical_location);
  }
  return status;
}

static ctool_status_t cir_lower_update(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  const ctool_c_expression_t *operand_expression;
  cir_stack_entry_t address;
  cir_stack_entry_t left;
  cir_stack_entry_t right;
  ctool_u32 child;
  ctool_u32 promoted_type;
  ctool_u32 base_depth = context->stack_depth;
  ctool_c_expression_operator_t update_operation;
  ctool_bool postfix;
  ctool_status_t status;
  if (expression->reference != CTOOL_C_AST_NONE ||
      expression->conversion != CTOOL_C_CONVERSION_NONE ||
      expression->computation_type >= context->unit->graph.type_count) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (expression->operation <
          CTOOL_C_EXPRESSION_OPERATOR_PREFIX_INCREMENT ||
      expression->operation >
          CTOOL_C_EXPRESSION_OPERATOR_POSTFIX_DECREMENT) {
    return cir_invalid_unit(context, &expression->location);
  }
  update_operation =
      expression->operation ==
                  CTOOL_C_EXPRESSION_OPERATOR_PREFIX_INCREMENT ||
              expression->operation ==
                  CTOOL_C_EXPRESSION_OPERATOR_POSTFIX_INCREMENT
          ? CTOOL_C_EXPRESSION_OPERATOR_ADD
          : CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT;
  postfix = expression->operation >=
                    CTOOL_C_EXPRESSION_OPERATOR_POSTFIX_INCREMENT
                ? CTOOL_TRUE
                : CTOOL_FALSE;
  status = cir_expression_child(context, expression_index, expression, 0u,
                                &child);
  if (status != CTOOL_OK) {
    return status;
  }
  operand_expression = &context->unit->expressions[child];
  if (cir_type_has_atomic_qualification(context,
                                        operand_expression->type) ==
      CTOOL_TRUE) {
    return cir_unsupported_type(context, &expression->location);
  }
  if (operand_expression->kind == CTOOL_C_EXPRESSION_MEMBER &&
      operand_expression->reference < context->unit->graph.member_count &&
      context->unit->graph.members[operand_expression->reference]
              .is_bit_field == CTOOL_TRUE) {
    return cir_lower_bit_field_mutation(
        context, expression, child, operand_expression, CTOOL_C_AST_NONE,
        depth, update_operation, postfix);
  }
  if (cir_type_is_i32_pointer(context, operand_expression->type) ==
      CTOOL_TRUE) {
    return cir_lower_pointer_update(
        context, expression, operand_expression, child, depth,
        update_operation, postfix);
  }
  if (cir_type_is_value_integer(context, operand_expression->type) ==
          CTOOL_FALSE ||
      cir_type_is_value_integer(context, expression->type) ==
          CTOOL_FALSE) {
    return cir_unsupported_type(context, &expression->location);
  }
  status = cir_require_integer_mutation_computation(
      context, expression->type, expression->computation_type,
      &expression->location);
  if (status != CTOOL_OK) {
    return status;
  }
  if (cir_integer_value_types_match(context, operand_expression->type,
                                    expression->type) == CTOOL_FALSE) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_lower_expression(context, child, depth + 1u);
  if (status == CTOOL_OK &&
      (cir_add_overflows(base_depth, 1u) == CTOOL_TRUE ||
       context->stack_depth != base_depth + 1u ||
       context->stack[base_depth].kind != CIR_STACK_ADDRESS ||
       context->stack[base_depth].type != operand_expression->type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK &&
      cir_integer_mutation_promotion_type(context, expression->type,
                                          &promoted_type) == CTOOL_FALSE) {
    return cir_unsupported_type(context, &expression->location);
  }
  if (status == CTOOL_OK &&
      promoted_type != expression->computation_type) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_duplicate_address(
        context, operand_expression->type, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status == CTOOL_OK &&
      (address.kind != CIR_STACK_ADDRESS ||
       address.type != operand_expression->type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_LOAD, expression->type,
        operand_expression->type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (status == CTOOL_OK) {
    status = cir_convert_top_integer(
        context, expression->type, promoted_type,
        CTOOL_C_CONVERSION_INTEGER_PROMOTION, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_INTEGER,
        expression->computation_type, CTOOL_C_TYPE_NONE,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
        CTOOL_C_AST_NONE, 1u, &expression->location,
        &expression->physical_location, (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE,
                      expression->computation_type);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &right);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &left);
  }
  if (status == CTOOL_OK &&
      (left.kind != CIR_STACK_VALUE || right.kind != CIR_STACK_VALUE ||
       left.type != expression->computation_type ||
       right.type != expression->computation_type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_BINARY,
        expression->computation_type, expression->computation_type,
        update_operation, CTOOL_C_CONVERSION_NONE,
        CTOOL_C_AST_NONE, 0u, &expression->location,
        &expression->physical_location, (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE,
                      expression->computation_type);
  }
  if (status == CTOOL_OK) {
    status = cir_convert_top_integer(
        context, expression->computation_type, expression->type,
        CTOOL_C_CONVERSION_ASSIGNMENT, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &right);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status == CTOOL_OK &&
      (address.kind != CIR_STACK_ADDRESS ||
       right.kind != CIR_STACK_VALUE ||
       address.type != operand_expression->type ||
       right.type != expression->type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_STORE_VALUE, expression->type,
        right.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  status = cir_push(context, CIR_STACK_VALUE, expression->type);
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_convert_top_integer(
        context, expression->type, promoted_type,
        CTOOL_C_CONVERSION_INTEGER_PROMOTION, &expression->location,
        &expression->physical_location);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_INTEGER,
        expression->computation_type, CTOOL_C_TYPE_NONE,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
        CTOOL_C_AST_NONE, 1u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_push(context, CIR_STACK_VALUE,
                      expression->computation_type);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_pop(context, &right);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_pop(context, &left);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE &&
      (left.kind != CIR_STACK_VALUE || right.kind != CIR_STACK_VALUE ||
       left.type != expression->computation_type ||
       right.type != expression->computation_type)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_BINARY,
        expression->computation_type, expression->computation_type,
        update_operation == CTOOL_C_EXPRESSION_OPERATOR_ADD
            ? CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT
            : CTOOL_C_EXPRESSION_OPERATOR_ADD,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_push(context, CIR_STACK_VALUE,
                      expression->computation_type);
  }
  if (status == CTOOL_OK && postfix == CTOOL_TRUE) {
    status = cir_convert_top_integer(
        context, expression->computation_type, expression->type,
        CTOOL_C_CONVERSION_ASSIGNMENT, &expression->location,
        &expression->physical_location);
  }
  return status;
}

static ctool_status_t cir_lower_member(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  cir_member_info_t info;
  ctool_status_t status;
  status = cir_validate_member(context, expression_index, expression,
                               &expression->location, &info);
  if (status != CTOOL_OK) {
    return status;
  }
  if (cir_member_info_complete(&info) == CTOOL_FALSE) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (info.member->is_bit_field == CTOOL_TRUE) {
    return cir_unsupported_expression(context, &expression->location);
  }
  if (info.member->bit_width != 0u || info.member_layout->bit_width != 0u ||
      info.member_layout->size >
          info.record_layout->size - info.member_layout->byte_offset) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_lower_member_record_address(
      context, &info, &expression->location, depth);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS, expression->type,
      info.record_expression->type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, expression->reference, 0u,
      &expression->location, &expression->physical_location,
      (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_ADDRESS, expression->type);
}

static ctool_status_t cir_lower_conditional(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  cir_stack_entry_t condition;
  cir_stack_entry_t when_nonzero;
  cir_stack_entry_t when_zero;
  ctool_u32 children[3];
  ctool_u32 branch;
  ctool_u32 jump;
  ctool_u32 base_depth;
  ctool_status_t status;
  ctool_u32 index;
  for (index = 0u; index < 3u; index++) {
    status = cir_expression_child(context, expression_index, expression,
                                  index, &children[index]);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  status = cir_lower_expression(context, children[0], depth + 1u);
  if (status == CTOOL_OK) {
    status = cir_pop(context, &condition);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (condition.kind != CIR_STACK_VALUE ||
      cir_type_is_truth_scalar(context, condition.type) ==
          CTOOL_FALSE) {
    return cir_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE, &expression->location,
        "CupidC IR lowering does not yet support this value type");
  }
  if (cir_type_is_truth_scalar(context, expression->type) ==
      CTOOL_FALSE) {
    status = cir_require_structure_value(
        context, expression->type, &expression->location);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  base_depth = context->stack_depth;
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE,
      condition.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, &expression->location,
      &expression->physical_location, &branch);
  if (status == CTOOL_OK) {
    status = cir_lower_expression(context, children[1], depth + 1u);
  }
  if (status == CTOOL_OK && context->stack_depth != base_depth + 1u) {
    status = CTOOL_ERR_INTERNAL;
  }
  if (status == CTOOL_OK) {
    when_nonzero = context->stack[context->stack_depth - 1u];
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location, &jump);
  }
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, branch,
                                 cir_function_offset(context));
  }
  if (status != CTOOL_OK) {
    return status;
  }
  context->stack_depth = base_depth;
  status = cir_lower_expression(context, children[2], depth + 1u);
  if (status == CTOOL_OK && context->stack_depth != base_depth + 1u) {
    status = CTOOL_ERR_INTERNAL;
  }
  if (status == CTOOL_OK) {
    when_zero = context->stack[context->stack_depth - 1u];
    if (when_nonzero.kind != CIR_STACK_VALUE ||
        when_zero.kind != CIR_STACK_VALUE) {
      status = cir_invalid_unit(context, &expression->location);
    } else if (cir_type_is_structure_value(context, expression->type) ==
               CTOOL_TRUE) {
      status = cir_require_structure_value(
          context, when_nonzero.type, &expression->location);
      if (status == CTOOL_OK) {
        status = cir_require_structure_value(
            context, when_zero.type, &expression->location);
      }
      if (status == CTOOL_OK &&
          (cir_structure_value_types_match(
               context, expression->type, when_nonzero.type) == CTOOL_FALSE ||
           cir_structure_value_types_match(
               context, expression->type, when_zero.type) == CTOOL_FALSE)) {
        status = cir_invalid_unit(context, &expression->location);
      }
      if (status == CTOOL_OK) {
        context->stack[context->stack_depth - 1u].type = expression->type;
      }
    } else if ((when_nonzero.type != expression->type &&
                cir_pointer_value_types_match(
                    context, expression->type,
                    when_nonzero.type) == CTOOL_FALSE) ||
               (when_zero.type != expression->type &&
                cir_pointer_value_types_match(
                    context, expression->type,
                    when_zero.type) == CTOOL_FALSE)) {
      status = cir_invalid_unit(context, &expression->location);
    } else {
      context->stack[context->stack_depth - 1u].type = expression->type;
    }
  }
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, jump,
                                 cir_function_offset(context));
  }
  return status;
}

static ctool_status_t cir_lower_call(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  const ctool_c_expression_t *callee;
  const ctool_c_expression_t *identifier =
      (const ctool_c_expression_t *)0;
  const ctool_c_binding_t *binding = (const ctool_c_binding_t *)0;
  const ctool_c_type_node_t *pointer_type;
  const ctool_c_type_node_t *function_type;
  ctool_u32 callee_index;
  ctool_u32 identifier_index = CTOOL_C_AST_NONE;
  ctool_u32 base_depth = context->stack_depth;
  ctool_u32 argument_base;
  ctool_u32 argument_count;
  ctool_u32 first_argument_type;
  ctool_u32 argument;
  ctool_bool direct = CTOOL_FALSE;
  ctool_status_t status;
  status = cir_expression_child(context, expression_index, expression, 0u,
                                &callee_index);
  if (status != CTOOL_OK) {
    return status;
  }
  callee = &context->unit->expressions[callee_index];
  pointer_type = cir_unwrapped_type(context, callee->type);
  if (pointer_type == (const ctool_c_type_node_t *)0 ||
      pointer_type->kind != CTOOL_C_TYPE_POINTER ||
      pointer_type->referenced_type >= context->unit->graph.type_count) {
    return cir_invalid_unit(context, &expression->location);
  }
  function_type =
      cir_unwrapped_type(context, pointer_type->referenced_type);
  if (function_type == (const ctool_c_type_node_t *)0 ||
      cir_type_is_i32_function_pointer(context, callee->type) == CTOOL_FALSE ||
      function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->referenced_type != expression->type ||
      function_type->first_parameter >
          context->unit->graph.parameter_type_count ||
      function_type->parameter_count >
          context->unit->graph.parameter_type_count -
              function_type->first_parameter ||
      expression->child_count == 0u) {
    return cir_invalid_unit(context, &expression->location);
  }
  argument_count = expression->child_count - 1u;
  if ((function_type->has_prototype == CTOOL_TRUE &&
       (argument_count < function_type->parameter_count ||
        (function_type->variadic == CTOOL_FALSE &&
         argument_count != function_type->parameter_count))) ||
      (function_type->has_prototype == CTOOL_FALSE &&
       (function_type->parameter_count != 0u ||
        function_type->variadic == CTOOL_TRUE))) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (callee->kind == CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION &&
      callee->conversion == CTOOL_C_CONVERSION_FUNCTION_TO_POINTER) {
    if (callee->child_count != 1u) {
      return cir_invalid_unit(context, &callee->location);
    }
    status = cir_expression_child(context, callee_index, callee, 0u,
                                  &identifier_index);
    if (status != CTOOL_OK) {
      return status;
    }
    identifier = &context->unit->expressions[identifier_index];
    if (identifier->kind == CTOOL_C_EXPRESSION_IDENTIFIER) {
      ctool_bool compatible;
      if (identifier->child_count != 0u ||
          identifier->reference >= context->unit->binding_count ||
          identifier->type >= context->unit->graph.type_count ||
          pointer_type->referenced_type != identifier->type) {
        return cir_invalid_unit(context, &identifier->location);
      }
      binding = &context->unit->bindings[identifier->reference];
      if (binding->kind != CTOOL_C_BINDING_FUNCTION ||
          binding->type >= context->unit->graph.type_count) {
        return cir_invalid_unit(context, &identifier->location);
      }
      compatible = cir_types_compatible(
          context, binding->type, identifier->type);
      if (context->relation_status != CTOOL_OK) {
        return context->relation_status;
      }
      if (compatible == CTOOL_FALSE) {
        return cir_invalid_unit(context, &identifier->location);
      }
      direct = CTOOL_TRUE;
    }
  }
  if (cir_type_is_void(context, expression->type) == CTOOL_FALSE &&
      cir_type_is_value_scalar(context, expression->type) ==
          CTOOL_FALSE) {
    if (cir_type_is_structure_candidate(context, expression->type) ==
        CTOOL_FALSE) {
      return cir_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
          &expression->location,
          "CupidC IR lowering supports calls with "
          "represented scalar or structure arguments and void, scalar, or "
          "structure results");
    }
    status = cir_require_structure_value(
        context, expression->type, &expression->location);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if (direct == CTOOL_FALSE) {
    status = cir_lower_expression(context, callee_index, depth + 1u);
    if (status != CTOOL_OK) {
      return status;
    }
    if (context->stack_depth != base_depth + 1u ||
        context->stack[base_depth].kind != CIR_STACK_VALUE ||
        cir_pointer_value_types_match(
            context, callee->type,
            context->stack[base_depth].type) == CTOOL_FALSE) {
      return cir_invalid_unit(context, &callee->location);
    }
  }
  argument_base = base_depth + (direct == CTOOL_TRUE ? 0u : 1u);
  for (argument = 0u; argument < argument_count; argument++) {
    ctool_u32 child;
    ctool_u32 argument_type = CTOOL_C_TYPE_NONE;
    ctool_bool undeclared_parameter_argument =
        argument >= function_type->parameter_count ? CTOOL_TRUE
                                                   : CTOOL_FALSE;
    if (undeclared_parameter_argument == CTOOL_FALSE) {
      argument_type =
          context->unit->graph
              .parameter_types[function_type->first_parameter + argument];
    }
    status = cir_expression_child(context, expression_index, expression,
                                  argument + 1u, &child);
    if (status != CTOOL_OK) {
      return status;
    }
    if (undeclared_parameter_argument == CTOOL_TRUE) {
      const ctool_c_expression_t *argument_expression =
          &context->unit->expressions[child];
      const ctool_c_type_layout_t *layout =
          argument_expression->type < context->unit->layout.type_count
              ? &context->unit->layout.types[argument_expression->type]
              : (const ctool_c_type_layout_t *)0;
      if (layout == (const ctool_c_type_layout_t *)0) {
        return cir_invalid_unit(context, &argument_expression->location);
      }
      if (cir_type_is_value_scalar(
              context, argument_expression->type) == CTOOL_FALSE ||
          cir_type_has_atomic_qualification(
              context, argument_expression->type) == CTOOL_TRUE) {
        return cir_emit_failure(
            context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
            &argument_expression->location,
            "CupidC IR lowering supports arguments without declared "
            "parameter types only for represented scalar values");
      }
      if (cir_type_is_floating_value(
              context, argument_expression->type) == CTOOL_TRUE &&
          layout->size != 8u) {
        return cir_emit_failure(
            context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
            &argument_expression->location,
            "CupidC IR lowering requires float-to-double promotion before "
            "an argument without a declared parameter type");
      }
      if (cir_type_is_represented_integer(
              context, argument_expression->type) == CTOOL_TRUE &&
          layout->size < 4u) {
        return cir_invalid_unit(context, &argument_expression->location);
      }
      argument_type = argument_expression->type;
    }
    if (argument_type >= context->unit->graph.type_count) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (cir_type_is_value_scalar(context, argument_type) == CTOOL_FALSE) {
      if (cir_type_is_structure_candidate(context, argument_type) ==
          CTOOL_FALSE) {
        return cir_emit_failure(
            context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
            &expression->location,
            "CupidC IR lowering supports calls with "
            "represented scalar or structure arguments and void, scalar, or "
            "structure results");
      }
      status = cir_require_structure_value(
          context, argument_type, &expression->location);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    status = cir_lower_expression(context, child, depth + 1u);
    if (status != CTOOL_OK) {
      return status;
    }
    if (context->stack_depth != argument_base + argument + 1u ||
        context->stack[argument_base + argument].kind != CIR_STACK_VALUE ||
        (cir_type_is_value_scalar(context, argument_type) == CTOOL_TRUE
             ? (context->stack[argument_base + argument].type !=
                        argument_type &&
                cir_scalar_value_types_match(
                    context, argument_type,
                    context->stack[argument_base + argument].type) ==
                    CTOOL_FALSE)
             : cir_structure_value_types_match(
                   context, argument_type,
                   context->stack[argument_base + argument].type) ==
                   CTOOL_FALSE)) {
      return cir_invalid_unit(context, &expression->location);
    }
  }
  first_argument_type = context->argument_type_count;
  for (argument = 0u; argument < argument_count; argument++) {
    status = cir_append_argument_type(
        context, context->stack[argument_base + argument].type);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  context->stack_depth = base_depth;
  {
    ctool_u32 instruction_index;
    status = cir_append_instruction(
      context,
      direct == CTOOL_TRUE ? CTOOL_C_IR_INSTRUCTION_CALL_DIRECT
                           : CTOOL_C_IR_INSTRUCTION_CALL_INDIRECT,
      expression->type,
      direct == CTOOL_TRUE ? identifier->type : callee->type,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
      direct == CTOOL_TRUE ? identifier->reference : CTOOL_C_AST_NONE, 0u,
      &expression->location, &expression->physical_location,
      &instruction_index);
    if (status == CTOOL_OK &&
        context->instructions != (ctool_c_ir_instruction_t *)0) {
      context->instructions[instruction_index].argument_count =
          argument_count;
      context->instructions[instruction_index].first_argument_type =
          first_argument_type;
    }
  }
  if (status != CTOOL_OK ||
      cir_type_is_void(context, expression->type) == CTOOL_TRUE) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, expression->type);
}

static ctool_bool cir_variadic_cursor_type(
    const cir_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *pointer;
  const ctool_c_type_node_t *character;
  ctool_u32 pointer_base;
  ctool_u32 pointer_qualifiers;
  ctool_u32 character_base;
  ctool_u32 character_qualifiers;
  if (type >= context->unit->layout.type_count ||
      cir_underlying_type(context, type, &pointer_base, &pointer_qualifiers,
                          &pointer) == CTOOL_FALSE ||
      pointer->kind != CTOOL_C_TYPE_POINTER ||
      pointer->referenced_type >= context->unit->graph.type_count ||
      cir_underlying_type(context, pointer->referenced_type,
                          &character_base, &character_qualifiers,
                          &character) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  (void)pointer_base;
  (void)character_base;
  return character->kind == CTOOL_C_TYPE_CHAR &&
                 ((pointer_qualifiers | pointer->qualifiers) &
                  CTOOL_C_QUAL_CONST) == 0u &&
                 (character_qualifiers | character->qualifiers) == 0u &&
                 context->unit->layout.types[type].is_object == CTOOL_TRUE &&
                 context->unit->layout.types[type].is_complete_object ==
                     CTOOL_TRUE &&
                 context->unit->layout.types[type].size == 4u &&
                 cir_type_has_atomic_qualification(context, type) ==
                     CTOOL_FALSE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cir_variadic_argument_type(
    const cir_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cir_unwrapped_type(context, type);
  const ctool_c_type_layout_t *layout;
  if (node == (const ctool_c_type_node_t *)0 ||
      type >= context->unit->layout.type_count ||
      cir_type_has_atomic_qualification(context, type) == CTOOL_TRUE) {
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

static ctool_status_t cir_lower_variadic_cursor_address(
    cir_context_t *context, ctool_u32 child, ctool_u32 depth,
    const ctool_c_pp_location_t *location,
    cir_stack_entry_t *address_out) {
  const ctool_c_expression_t *cursor;
  ctool_u32 base_depth = context->stack_depth;
  ctool_status_t status;
  if (child >= context->unit->expression_count) {
    return cir_invalid_unit(context, location);
  }
  cursor = &context->unit->expressions[child];
  if (cir_variadic_cursor_type(context, cursor->type) == CTOOL_FALSE) {
    return cir_invalid_unit(context, location);
  }
  status = cir_lower_expression(context, child, depth + 1u);
  if (status != CTOOL_OK) {
    return status;
  }
  if (cir_add_overflows(base_depth, 1u) == CTOOL_TRUE ||
      context->stack_depth != base_depth + 1u) {
    return cir_invalid_unit(context, location);
  }
  status = cir_pop(context, address_out);
  if (status != CTOOL_OK || address_out->kind != CIR_STACK_ADDRESS ||
      address_out->type != cursor->type) {
    return cir_invalid_unit(context, location);
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_variadic_builtin(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  cir_stack_entry_t address = {CIR_STACK_VALUE, CTOOL_C_TYPE_NONE};
  ctool_u32 cursor_child;
  ctool_u32 expected_children =
      expression->kind == CTOOL_C_EXPRESSION_VARIADIC_START ||
              expression->kind == CTOOL_C_EXPRESSION_VARIADIC_COPY
          ? 2u
          : 1u;
  ctool_status_t status;
  if (expression->child_count != expected_children ||
      expression->reference != CTOOL_C_AST_NONE ||
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      expression->conversion != CTOOL_C_CONVERSION_NONE ||
      expression->computation_type != CTOOL_C_TYPE_NONE ||
      expression->integer_bits != 0u ||
      expression->string_bytes.data != (const ctool_u8 *)0 ||
      expression->string_bytes.size != 0u ||
      ((expression->kind == CTOOL_C_EXPRESSION_VARIADIC_ARGUMENT) !=
       (cir_type_is_void(context, expression->type) == CTOOL_FALSE))) {
    return cir_invalid_unit(context, &expression->location);
  }
  status = cir_expression_child(context, expression_index, expression, 0u,
                                &cursor_child);
  if (status != CTOOL_OK) {
    return status;
  }
  if (expression->kind == CTOOL_C_EXPRESSION_VARIADIC_START) {
    const ctool_c_expression_t *last;
    ctool_u32 last_child;
    ctool_u32 expected_last;
    if (context->function_variadic == CTOOL_FALSE ||
        context->function_parameter_count == 0u ||
        cir_add_overflows(context->function_first_parameter,
                          context->function_parameter_count - 1u) ==
            CTOOL_TRUE) {
      return cir_invalid_unit(context, &expression->location);
    }
    expected_last = context->function_first_parameter +
                    context->function_parameter_count - 1u;
    status = cir_expression_child(context, expression_index, expression, 1u,
                                  &last_child);
    if (status != CTOOL_OK) {
      return status;
    }
    last = &context->unit->expressions[last_child];
    if (last->kind != CTOOL_C_EXPRESSION_PARAMETER ||
        last->child_count != 0u || last->first_child != CTOOL_C_AST_NONE ||
        last->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        last->conversion != CTOOL_C_CONVERSION_NONE ||
        last->computation_type != CTOOL_C_TYPE_NONE ||
        last->integer_bits != 0u ||
        last->string_bytes.data != (const ctool_u8 *)0 ||
        last->string_bytes.size != 0u ||
        last->reference != expected_last ||
        expected_last >= context->unit->parameter_count ||
        last->type != context->unit->parameters[expected_last].type) {
      return cir_invalid_unit(context, &expression->location);
    }
    status = cir_lower_variadic_cursor_address(
        context, cursor_child, depth, &expression->location, &address);
    if (status != CTOOL_OK) {
      return status;
    }
    return cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_VARIADIC_START,
        CTOOL_C_TYPE_NONE, address.type,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
        expected_last, 0u, &expression->location,
        &expression->physical_location, (ctool_u32 *)0);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_VARIADIC_COPY) {
    const ctool_c_expression_t *source;
    cir_stack_entry_t value;
    ctool_u32 source_child;
    ctool_u32 base_depth = context->stack_depth;
    status = cir_expression_child(context, expression_index, expression, 1u,
                                  &source_child);
    if (status != CTOOL_OK) {
      return status;
    }
    source = &context->unit->expressions[source_child];
    if (source->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
        source->conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
        cir_variadic_cursor_type(context, source->type) == CTOOL_FALSE) {
      return cir_invalid_unit(context, &expression->location);
    }
    status = cir_lower_variadic_cursor_address(
        context, cursor_child, depth, &expression->location, &address);
    if (status == CTOOL_OK) {
      status = cir_push(context, address.kind, address.type);
    }
    if (status == CTOOL_OK) {
      status = cir_lower_expression(context, source_child, depth + 1u);
    }
    if (status == CTOOL_OK &&
        (cir_add_overflows(base_depth, 2u) == CTOOL_TRUE ||
         context->stack_depth != base_depth + 2u)) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (status == CTOOL_OK) {
      status = cir_pop(context, &value);
    }
    if (status == CTOOL_OK) {
      status = cir_pop(context, &address);
    }
    if (status != CTOOL_OK || address.kind != CIR_STACK_ADDRESS ||
        value.kind != CIR_STACK_VALUE ||
        cir_scalar_value_types_match(context, address.type, value.type) ==
            CTOOL_FALSE) {
      return cir_invalid_unit(context, &expression->location);
    }
    return cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_STORE, address.type, value.type,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
        CTOOL_C_AST_NONE, 0u, &expression->location,
        &expression->physical_location, (ctool_u32 *)0);
  }
  status = cir_lower_variadic_cursor_address(
      context, cursor_child, depth, &expression->location, &address);
  if (status != CTOOL_OK) {
    return status;
  }
  if (expression->kind == CTOOL_C_EXPRESSION_VARIADIC_ARGUMENT) {
    if (cir_variadic_argument_type(context, expression->type) ==
        CTOOL_FALSE) {
      return cir_invalid_unit(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_VARIADIC_ARGUMENT,
        expression->type, address.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
    return status == CTOOL_OK
               ? cir_push(context, CIR_STACK_VALUE, expression->type)
               : status;
  }
  return cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_VARIADIC_END, CTOOL_C_TYPE_NONE,
      address.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &expression->location, &expression->physical_location,
      (ctool_u32 *)0);
}

static ctool_status_t cir_lower_enumerator_value(
    cir_context_t *context, ctool_u32 type, ctool_u64 integer_bits,
    ctool_bool integer_unsigned, const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location) {
  ctool_u64 low = integer_bits & 0xffffffffu;
  ctool_status_t status;
  if (integer_unsigned != CTOOL_FALSE && integer_unsigned != CTOOL_TRUE) {
    return cir_invalid_unit(context, location);
  }
  if (cir_type_is_i32_integer(context, type) == CTOOL_FALSE) {
    return cir_unsupported_type(context, location);
  }
  if (cir_i32_bits_are_canonical(
          integer_bits,
          integer_unsigned == CTOOL_FALSE ? CTOOL_TRUE : CTOOL_FALSE) ==
      CTOOL_FALSE) {
    return cir_invalid_unit(context, location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_INTEGER, type, CTOOL_C_TYPE_NONE,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
      CTOOL_C_AST_NONE, low, location, physical_location,
      (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, type);
}

static ctool_status_t cir_lower_expression(cir_context_t *context,
                                           ctool_u32 expression_index,
                                           ctool_u32 depth) {
  const ctool_c_expression_t *expression;
  ctool_status_t status;
  if (expression_index >= context->unit->expression_count) {
    return cir_invalid_unit(context, (const ctool_c_pp_location_t *)0);
  }
  expression = &context->unit->expressions[expression_index];
  if (depth > CTOOL_C_PARSE_NESTING_LIMIT) {
    return cir_emit_failure(
        context, CTOOL_ERR_LIMIT, CTOOL_C_IR_DIAG_LIMIT,
        &expression->location,
        "CupidC IR lowering exceeded a configured resource limit");
  }
  if (expression->kind == CTOOL_C_EXPRESSION_PARAMETER) {
    ctool_u32 parameter_end;
    if (expression->child_count != 0u ||
        cir_add_overflows(context->function_first_parameter,
                          context->function_parameter_count) == CTOOL_TRUE) {
      return cir_invalid_unit(context, &expression->location);
    }
    parameter_end = context->function_first_parameter +
                    context->function_parameter_count;
    if (expression->reference < context->function_first_parameter ||
        expression->reference >= parameter_end ||
        expression->reference >= context->unit->parameter_count ||
        context->unit->parameters[expression->reference].type !=
            expression->type) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (cir_type_is_value_scalar(context, expression->type) == CTOOL_FALSE) {
      status = cir_require_structure_value(
          context, expression->type, &expression->location);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, expression->type,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, expression->reference, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
    if (status != CTOOL_OK) {
      return status;
    }
    return cir_push(context, CIR_STACK_ADDRESS, expression->type);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_IDENTIFIER) {
    const ctool_c_binding_t *binding;
    ctool_bool compatible = CTOOL_FALSE;
    ctool_u32 definition;
    if (expression->child_count != 0u ||
        expression->first_child != CTOOL_C_AST_NONE ||
        expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        expression->conversion != CTOOL_C_CONVERSION_NONE ||
        expression->computation_type != CTOOL_C_TYPE_NONE ||
        expression->reference >= context->unit->binding_count) {
      return cir_invalid_unit(context, &expression->location);
    }
    binding = &context->unit->bindings[expression->reference];
    if (expression->type >= context->unit->graph.type_count ||
        binding->type >= context->unit->graph.type_count) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (binding->kind == CTOOL_C_BINDING_FUNCTION) {
      const ctool_c_type_node_t *expression_function =
          cir_unwrapped_type(context, expression->type);
      const ctool_c_type_node_t *binding_function =
          cir_unwrapped_type(context, binding->type);
      if (expression_function == (const ctool_c_type_node_t *)0 ||
          binding_function == (const ctool_c_type_node_t *)0 ||
          expression_function->kind != CTOOL_C_TYPE_FUNCTION ||
          binding_function->kind != CTOOL_C_TYPE_FUNCTION) {
        return cir_invalid_unit(context, &expression->location);
      }
      compatible = cir_types_compatible(
          context, binding->type, expression->type);
      if (context->relation_status != CTOOL_OK) {
        return context->relation_status;
      }
      if (compatible == CTOOL_FALSE) {
        return cir_invalid_unit(context, &expression->location);
      }
    } else if (binding->type != expression->type) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (binding->kind == CTOOL_C_BINDING_ENUMERATOR) {
      if (binding->storage != CTOOL_C_STORAGE_NONE ||
          binding->linkage != CTOOL_C_LINKAGE_NONE) {
        return cir_invalid_unit(context, &expression->location);
      }
      return cir_lower_enumerator_value(
          context, expression->type, binding->integer_bits,
          binding->integer_unsigned, &expression->location,
          &expression->physical_location);
    }
    if (binding->kind == CTOOL_C_BINDING_FUNCTION) {
      if (binding->linkage != CTOOL_C_LINKAGE_INTERNAL &&
          binding->linkage != CTOOL_C_LINKAGE_EXTERNAL) {
        return cir_invalid_unit(context, &expression->location);
      }
      if (binding->linkage == CTOOL_C_LINKAGE_INTERNAL) {
        for (definition = 0u;
             definition < context->unit->function_definition_count;
             definition++) {
          if (context->unit->function_definitions[definition].binding ==
              expression->reference) {
            break;
          }
        }
        if (definition == context->unit->function_definition_count) {
          return cir_invalid_unit(context, &expression->location);
        }
      }
      status = cir_append_instruction(
          context, CTOOL_C_IR_INSTRUCTION_FUNCTION_ADDRESS,
          expression->type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          expression->reference, 0u, &expression->location,
          &expression->physical_location, (ctool_u32 *)0);
      if (status != CTOOL_OK) {
        return status;
      }
      return cir_push(context, CIR_STACK_FUNCTION_DESIGNATOR,
                      expression->type);
    }
    if (binding->kind != CTOOL_C_BINDING_OBJECT ||
        (binding->linkage != CTOOL_C_LINKAGE_INTERNAL &&
         binding->linkage != CTOOL_C_LINKAGE_EXTERNAL)) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (binding->linkage == CTOOL_C_LINKAGE_INTERNAL) {
      for (definition = 0u;
           definition < context->unit->object_definition_count;
           definition++) {
        if (context->unit->object_definitions[definition].binding ==
            expression->reference) {
          break;
        }
      }
      if (definition == context->unit->object_definition_count) {
        return cir_invalid_unit(context, &expression->location);
      }
    }
    if (cir_type_is_value_scalar(context, expression->type) ==
            CTOOL_FALSE &&
        cir_type_is_complete_aggregate_object(context, expression->type) ==
            CTOOL_FALSE) {
      return cir_unsupported_type(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS, expression->type,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, expression->reference, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
    if (status != CTOOL_OK) {
      return status;
    }
    return cir_push(context, CIR_STACK_ADDRESS, expression->type);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_BLOCK_BINDING) {
    const ctool_c_block_binding_t *binding;
    ctool_u32 binding_end;
    if (expression->child_count != 0u ||
        context->function_first_block_binding == CTOOL_C_AST_NONE ||
        cir_add_overflows(context->function_first_block_binding,
                          context->function_block_binding_count) ==
            CTOOL_TRUE) {
      return cir_invalid_unit(context, &expression->location);
    }
    binding_end = context->function_first_block_binding +
                  context->function_block_binding_count;
    if (expression->reference < context->function_first_block_binding ||
        expression->reference >= binding_end ||
        expression->reference >= context->visible_block_binding_end ||
        expression->reference >= context->unit->block_binding_count) {
      return cir_invalid_unit(context, &expression->location);
    }
    binding = &context->unit->block_bindings[expression->reference];
    if (binding->type != expression->type ||
        binding->storage == CTOOL_C_STORAGE_EXTERN ||
        binding->linkage_binding != CTOOL_C_AST_NONE) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (binding->kind == CTOOL_C_BINDING_ENUMERATOR) {
      if (binding->storage != CTOOL_C_STORAGE_NONE ||
          binding->initializer != CTOOL_C_AST_NONE) {
        return cir_invalid_unit(context, &expression->location);
      }
      return cir_lower_enumerator_value(
          context, expression->type, binding->integer_bits,
          binding->integer_unsigned, &expression->location,
          &expression->physical_location);
    }
    if (binding->kind != CTOOL_C_BINDING_OBJECT ||
        binding->integer_bits != 0ull ||
        binding->integer_unsigned != CTOOL_FALSE) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (cir_type_is_value_scalar(context, expression->type) ==
            CTOOL_FALSE &&
        cir_type_is_complete_aggregate_object(
            context, expression->type) == CTOOL_FALSE) {
      return cir_unsupported_type(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS, expression->type,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, expression->reference, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
    if (status != CTOOL_OK) {
      return status;
    }
    return cir_push(context, CIR_STACK_ADDRESS, expression->type);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_COMPOUND_LITERAL) {
    return cir_lower_compound_literal_expression(
        context, expression_index, expression, depth);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_VARIADIC_START ||
      expression->kind == CTOOL_C_EXPRESSION_VARIADIC_ARGUMENT ||
      expression->kind == CTOOL_C_EXPRESSION_VARIADIC_COPY ||
      expression->kind == CTOOL_C_EXPRESSION_VARIADIC_END) {
    return cir_lower_variadic_builtin(context, expression_index, expression,
                                      depth);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_STRING) {
    ctool_status_t string_status;
    if (expression->child_count != 0u ||
        expression->first_child != CTOOL_C_AST_NONE ||
        expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        expression->conversion != CTOOL_C_CONVERSION_NONE ||
        expression->computation_type != CTOOL_C_TYPE_NONE ||
        expression->reference != CTOOL_C_AST_NONE ||
        expression->integer_bits != 0u ||
        expression->string_bytes.data == (const ctool_u8 *)0 ||
        expression->string_bytes.size == 0u ||
        cir_type_is_character_array(context, expression->type) ==
            CTOOL_FALSE ||
        expression->type >= context->unit->layout.type_count ||
        expression->string_bytes.size !=
            context->unit->layout.types[expression->type].size) {
      return cir_invalid_unit(context, &expression->location);
    }
    string_status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_STRING_LITERAL_ADDRESS,
        expression->type, CTOOL_C_TYPE_NONE,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
        expression_index, 0u, &expression->location,
        &expression->physical_location, (ctool_u32 *)0);
    if (string_status != CTOOL_OK) {
      return string_status;
    }
    return cir_push(context, CIR_STACK_ADDRESS, expression->type);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_INTEGER_CONSTANT) {
    ctool_bool is_signed;
    ctool_u64 integer_bits;
    if (expression->child_count != 0u ||
        cir_type_is_value_integer(context, expression->type) == CTOOL_FALSE) {
      return cir_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE, &expression->location,
          "CupidC IR lowering does not yet support this value type");
    }
    is_signed = context->unit->layout.types[expression->type].is_signed;
    if (is_signed != CTOOL_FALSE && is_signed != CTOOL_TRUE) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (cir_type_is_wide_integer(context, expression->type) == CTOOL_FALSE &&
        cir_i32_bits_are_canonical(expression->integer_bits, is_signed) ==
            CTOOL_FALSE) {
      return cir_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE, &expression->location,
          "CupidC IR lowering does not yet support this value type");
    }
    integer_bits = cir_type_is_wide_integer(context, expression->type) ==
                           CTOOL_TRUE
                       ? expression->integer_bits
                       : expression->integer_bits & 0xffffffffu;
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_INTEGER, expression->type,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, integer_bits,
        &expression->location,
        &expression->physical_location, (ctool_u32 *)0);
    if (status != CTOOL_OK) {
      return status;
    }
    return cir_push(context, CIR_STACK_VALUE, expression->type);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION) {
    if (expression->child_count != 1u) {
      return cir_invalid_unit(context, &expression->location);
    }
    return cir_lower_conversion(context, expression_index, expression, depth);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_UNARY) {
    if (expression->child_count != 1u) {
      return cir_invalid_unit(context, &expression->location);
    }
    return cir_lower_unary(context, expression_index, expression, depth);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_BINARY) {
    if (expression->child_count != 2u) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (expression->operation ==
        CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_AND) {
      return cir_lower_logical_and(context, expression_index, expression,
                                   depth);
    }
    if (expression->operation == CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_OR) {
      return cir_lower_logical_or(context, expression_index, expression,
                                  depth);
    }
    return cir_lower_binary(context, expression_index, expression, depth);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_ASSIGNMENT) {
    if (expression->child_count != 2u) {
      return cir_invalid_unit(context, &expression->location);
    }
    return cir_lower_assignment(context, expression_index, expression,
                                depth);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_UPDATE) {
    if (expression->child_count != 1u) {
      return cir_invalid_unit(context, &expression->location);
    }
    return cir_lower_update(context, expression_index, expression, depth);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_MEMBER) {
    if (expression->child_count != 1u) {
      return cir_invalid_unit(context, &expression->location);
    }
    return cir_lower_member(context, expression_index, expression, depth);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_CONDITIONAL) {
    if (expression->child_count != 3u) {
      return cir_invalid_unit(context, &expression->location);
    }
    return cir_lower_conditional(context, expression_index, expression,
                                 depth);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_CALL) {
    if (expression->child_count == 0u) {
      return cir_invalid_unit(context, &expression->location);
    }
    return cir_lower_call(context, expression_index, expression,
                                 depth);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_CAST) {
    if (expression->child_count != 1u) {
      return cir_invalid_unit(context, &expression->location);
    }
    return cir_lower_cast(context, expression_index, expression, depth);
  }
  return cir_unsupported_expression(context, &expression->location);
}

static ctool_u32 cir_inline_child_slot_count(
    const ctool_c_type_node_t *node) {
  if (node->kind == CTOOL_C_TYPE_ALIGNED ||
      node->kind == CTOOL_C_TYPE_QUALIFIED ||
      node->kind == CTOOL_C_TYPE_ARRAY ||
      node->kind == CTOOL_C_TYPE_VECTOR ||
      node->kind == CTOOL_C_TYPE_ENUM) {
    return 1u;
  }
  return node->kind == CTOOL_C_TYPE_RECORD ? node->member_count : 0u;
}

static ctool_bool cir_inline_child(
    const cir_context_t *context, const ctool_c_type_node_t *node,
    ctool_u32 slot, ctool_u32 *child_out) {
  if (node->kind != CTOOL_C_TYPE_RECORD &&
      cir_inline_child_slot_count(node) == 1u) {
    if (slot != 0u) {
      return CTOOL_FALSE;
    }
    *child_out = node->referenced_type;
    return CTOOL_TRUE;
  }
  if (node->kind == CTOOL_C_TYPE_RECORD && slot < node->member_count) {
    ctool_u32 member_index = node->first_member + slot;
    const ctool_c_record_member_t *member =
        &context->unit->graph.members[member_index];
    const ctool_c_type_node_t *member_node =
        cir_unwrapped_type(context, member->type);
    if ((member->is_bit_field == CTOOL_TRUE && member->bit_width == 0u) ||
        (slot + 1u == node->member_count &&
         member_node != (const ctool_c_type_node_t *)0 &&
         member_node->kind == CTOOL_C_TYPE_ARRAY &&
         member_node->array_bound_kind == CTOOL_C_ARRAY_UNSPECIFIED)) {
      return CTOOL_FALSE;
    }
    *child_out = member->type;
    return CTOOL_TRUE;
  }
  return CTOOL_FALSE;
}

static ctool_bool cir_inline_types_are_acyclic(
    const cir_context_t *context, const ctool_bool *seen,
    ctool_u32 *indegree, ctool_u32 *work) {
  ctool_u32 reachable_count = 0u;
  ctool_u32 processed_count = 0u;
  ctool_u32 work_count = 0u;
  ctool_u32 type;
  for (type = 0u; type < context->unit->graph.type_count; type++) {
    const ctool_c_type_node_t *node;
    ctool_u32 slot;
    if (seen[type] == CTOOL_FALSE) {
      continue;
    }
    reachable_count++;
    node = &context->unit->graph.types[type];
    for (slot = 0u; slot < cir_inline_child_slot_count(node); slot++) {
      ctool_u32 child;
      if (cir_inline_child(context, node, slot, &child) == CTOOL_FALSE) {
        continue;
      }
      if (child >= context->unit->graph.type_count ||
          seen[child] == CTOOL_FALSE || indegree[child] == 0xffffffffu) {
        return CTOOL_FALSE;
      }
      indegree[child]++;
    }
  }
  for (type = 0u; type < context->unit->graph.type_count; type++) {
    if (seen[type] == CTOOL_TRUE && indegree[type] == 0u) {
      work[work_count++] = type;
    }
  }
  while (work_count != 0u) {
    const ctool_c_type_node_t *node;
    ctool_u32 slot;
    type = work[--work_count];
    processed_count++;
    node = &context->unit->graph.types[type];
    for (slot = 0u; slot < cir_inline_child_slot_count(node); slot++) {
      ctool_u32 child;
      if (cir_inline_child(context, node, slot, &child) == CTOOL_FALSE) {
        continue;
      }
      if (child >= context->unit->graph.type_count ||
          indegree[child] == 0u) {
        return CTOOL_FALSE;
      }
      indegree[child]--;
      if (indegree[child] == 0u) {
        work[work_count++] = child;
      }
    }
  }
  return processed_count == reachable_count ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_status_t cir_require_initializable_aggregate(
    cir_context_t *context, ctool_u32 root_type,
    const ctool_c_pp_location_t *location) {
  ctool_arena_mark_t mark = ctool_arena_mark(context->arena);
  ctool_u32 *work = (ctool_u32 *)0;
  ctool_u32 *indegree = (ctool_u32 *)0;
  ctool_bool *seen = (ctool_bool *)0;
  ctool_u32 work_count = 0u;
  ctool_bool invalid = CTOOL_FALSE;
  ctool_bool unsupported = CTOOL_FALSE;
  ctool_status_t status;
  ctool_status_t rewind_status;
  if (root_type >= context->unit->graph.type_count ||
      root_type >= context->unit->layout.type_count) {
    return cir_invalid_unit(context, location);
  }
  status = cir_alloc_array(
      context, context->unit->graph.type_count, (ctool_u32)sizeof(*work),
      (void **)&work);
  if (status == CTOOL_OK) {
    status = cir_alloc_array(
        context, context->unit->graph.type_count, (ctool_u32)sizeof(*seen),
        (void **)&seen);
  }
  if (status == CTOOL_OK) {
    status = cir_alloc_array(
        context, context->unit->graph.type_count,
        (ctool_u32)sizeof(*indegree), (void **)&indegree);
  }
  if (status == CTOOL_OK) {
    work[work_count++] = root_type;
    seen[root_type] = CTOOL_TRUE;
  }
  while (status == CTOOL_OK && invalid == CTOOL_FALSE &&
         work_count != 0u) {
    ctool_u32 type = work[--work_count];
    const ctool_c_type_node_t *node = cir_type_node(context, type);
    const ctool_c_type_layout_t *layout;
    if (node == (const ctool_c_type_node_t *)0 ||
        type >= context->unit->layout.type_count) {
      invalid = CTOOL_TRUE;
      break;
    }
    layout = &context->unit->layout.types[type];
    if (node->kind < CTOOL_C_TYPE_VOID ||
        node->kind > CTOOL_C_TYPE_QUALIFIED || layout->alignment == 0u ||
        (layout->alignment & (layout->alignment - 1u)) != 0u) {
      invalid = CTOOL_TRUE;
      continue;
    }
    if ((node->qualifiers & (CTOOL_C_QUAL_VOLATILE | CTOOL_C_QUAL_ATOMIC)) !=
        0u) {
      unsupported = CTOOL_TRUE;
    }
    if (node->kind == CTOOL_C_TYPE_ALIGNED ||
        node->kind == CTOOL_C_TYPE_QUALIFIED) {
      const ctool_c_type_layout_t *referenced_layout;
      if (node->referenced_type >= context->unit->graph.type_count ||
          node->referenced_type >= context->unit->layout.type_count ||
          cir_unwrapped_type(context, type) ==
              (const ctool_c_type_node_t *)0) {
        invalid = CTOOL_TRUE;
        continue;
      }
      referenced_layout =
          &context->unit->layout.types[node->referenced_type];
      if (layout->is_object == CTOOL_FALSE ||
          layout->is_complete_object == CTOOL_FALSE || layout->size == 0u ||
          layout->size != referenced_layout->size ||
          layout->is_object != referenced_layout->is_object ||
          layout->is_complete_object !=
              referenced_layout->is_complete_object) {
        invalid = CTOOL_TRUE;
      } else if (seen[node->referenced_type] == CTOOL_FALSE) {
        seen[node->referenced_type] = CTOOL_TRUE;
        work[work_count++] = node->referenced_type;
      }
      continue;
    }
    if (node->kind == CTOOL_C_TYPE_POINTER) {
      if (node->referenced_type >= context->unit->graph.type_count ||
          layout->is_object == CTOOL_FALSE ||
          layout->is_complete_object == CTOOL_FALSE || layout->size != 4u) {
        invalid = CTOOL_TRUE;
      }
      continue;
    }
    if (node->kind == CTOOL_C_TYPE_ARRAY) {
      const ctool_c_type_layout_t *element_layout;
      if (node->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
          node->element_count == 0u ||
          node->referenced_type >= context->unit->graph.type_count ||
          node->referenced_type >= context->unit->layout.type_count ||
          layout->is_object == CTOOL_FALSE ||
          layout->is_complete_object == CTOOL_FALSE || layout->size == 0u) {
        invalid = CTOOL_TRUE;
        continue;
      }
      element_layout =
          &context->unit->layout.types[node->referenced_type];
      if (element_layout->is_object == CTOOL_FALSE ||
          element_layout->is_complete_object == CTOOL_FALSE ||
          element_layout->size == 0u ||
          layout->alignment != element_layout->alignment ||
          cir_multiply_overflows(node->element_count,
                                 element_layout->size) == CTOOL_TRUE ||
          node->element_count * element_layout->size != layout->size) {
        invalid = CTOOL_TRUE;
      } else if (seen[node->referenced_type] == CTOOL_FALSE) {
        seen[node->referenced_type] = CTOOL_TRUE;
        work[work_count++] = node->referenced_type;
      }
      continue;
    }
    if (node->kind == CTOOL_C_TYPE_VECTOR) {
      const ctool_c_type_layout_t *element_layout;
      if (node->element_count == 0u ||
          node->referenced_type >= context->unit->graph.type_count ||
          node->referenced_type >= context->unit->layout.type_count ||
          layout->is_object == CTOOL_FALSE ||
          layout->is_complete_object == CTOOL_FALSE || layout->size == 0u) {
        invalid = CTOOL_TRUE;
        continue;
      }
      element_layout =
          &context->unit->layout.types[node->referenced_type];
      if (element_layout->is_object == CTOOL_FALSE ||
          element_layout->is_complete_object == CTOOL_FALSE ||
          element_layout->size == 0u ||
          cir_multiply_overflows(node->element_count,
                                 element_layout->size) == CTOOL_TRUE ||
          node->element_count * element_layout->size != layout->size) {
        invalid = CTOOL_TRUE;
      } else if (seen[node->referenced_type] == CTOOL_FALSE) {
        seen[node->referenced_type] = CTOOL_TRUE;
        work[work_count++] = node->referenced_type;
      }
      continue;
    }
    if (node->kind == CTOOL_C_TYPE_RECORD) {
      ctool_u32 member_offset;
      if (node->record_kind < CTOOL_C_RECORD_STRUCT ||
          node->record_kind > CTOOL_C_RECORD_CLASS) {
        invalid = CTOOL_TRUE;
        continue;
      }
      if (node->record_kind != CTOOL_C_RECORD_STRUCT) {
        unsupported = CTOOL_TRUE;
      }
      if (node->record_complete == CTOOL_FALSE ||
          node->first_member > context->unit->graph.member_count ||
          node->member_count >
              context->unit->graph.member_count - node->first_member ||
          layout->is_object == CTOOL_FALSE ||
          layout->is_complete_object == CTOOL_FALSE || layout->size == 0u) {
        invalid = CTOOL_TRUE;
        continue;
      }
      for (member_offset = 0u; member_offset < node->member_count;
           member_offset++) {
        ctool_u32 member_index = node->first_member + member_offset;
        const ctool_c_record_member_t *member =
            &context->unit->graph.members[member_index];
        const ctool_c_member_layout_t *member_layout =
            &context->unit->layout.members[member_index];
        const ctool_c_type_node_t *member_node;
        const ctool_c_type_layout_t *member_type_layout;
        ctool_u32 member_type = member->type;
        if (member_type >= context->unit->graph.type_count ||
            member_type >= context->unit->layout.type_count) {
          invalid = CTOOL_TRUE;
          break;
        }
        member_node = cir_unwrapped_type(context, member_type);
        member_type_layout = &context->unit->layout.types[member_type];
        if (member_node == (const ctool_c_type_node_t *)0 ||
            member_layout->alignment == 0u ||
            (member_layout->alignment & (member_layout->alignment - 1u)) !=
                0u ||
            member_layout->byte_offset > layout->size ||
            member_layout->size >
                layout->size - member_layout->byte_offset) {
          invalid = CTOOL_TRUE;
          break;
        }
        if (member_offset + 1u == node->member_count &&
            member_node->kind == CTOOL_C_TYPE_ARRAY &&
            member_node->array_bound_kind == CTOOL_C_ARRAY_UNSPECIFIED) {
          if (node->record_kind != CTOOL_C_RECORD_STRUCT ||
              member_layout->size != 0u ||
              member_layout->byte_offset != layout->size) {
            invalid = CTOOL_TRUE;
          }
          continue;
        }
        if (member->is_bit_field == CTOOL_TRUE) {
          if (member->bit_width == 0u) {
            if (member_layout->bit_offset != 0u ||
                member_layout->bit_width != 0u ||
                member_layout->size != member_type_layout->size) {
              invalid = CTOOL_TRUE;
              break;
            }
            continue;
          }
          if (member_layout->size == 0u || member_layout->size > 8u ||
              member_layout->bit_width != member->bit_width ||
              member_layout->bit_offset > member_layout->size * 8u ||
              member_layout->bit_width >
                  member_layout->size * 8u - member_layout->bit_offset) {
            invalid = CTOOL_TRUE;
            break;
          }
          if (seen[member_type] == CTOOL_FALSE) {
            seen[member_type] = CTOOL_TRUE;
            work[work_count++] = member_type;
          }
          continue;
        }
        if (member->bit_width != 0u || member_layout->bit_offset != 0u ||
            member_layout->bit_width != 0u ||
            member_type_layout->is_object == CTOOL_FALSE ||
            member_type_layout->is_complete_object == CTOOL_FALSE ||
            member_type_layout->size == 0u ||
            member_layout->size != member_type_layout->size) {
          invalid = CTOOL_TRUE;
          break;
        }
        if (seen[member_type] == CTOOL_FALSE) {
          if (work_count >= context->unit->graph.type_count) {
            invalid = CTOOL_TRUE;
            break;
          }
          seen[member_type] = CTOOL_TRUE;
          work[work_count++] = member_type;
        }
      }
      continue;
    }
    if (node->kind == CTOOL_C_TYPE_ENUM) {
      if (node->referenced_type >= context->unit->graph.type_count ||
          node->referenced_type >= context->unit->layout.type_count) {
        invalid = CTOOL_TRUE;
      } else if (seen[node->referenced_type] == CTOOL_FALSE) {
        seen[node->referenced_type] = CTOOL_TRUE;
        work[work_count++] = node->referenced_type;
      }
    }
    if (layout->is_object == CTOOL_FALSE ||
        layout->is_complete_object == CTOOL_FALSE || layout->size == 0u) {
      invalid = CTOOL_TRUE;
    }
  }
  if (status == CTOOL_OK && invalid == CTOOL_FALSE &&
      cir_inline_types_are_acyclic(context, seen, indegree, work) ==
          CTOOL_FALSE) {
    invalid = CTOOL_TRUE;
  }
  rewind_status = ctool_arena_rewind(context->arena, mark);
  if (status != CTOOL_OK) {
    return rewind_status == CTOOL_OK ? status : rewind_status;
  }
  if (rewind_status != CTOOL_OK) {
    return rewind_status;
  }
  if (invalid == CTOOL_TRUE) {
    return cir_invalid_unit(context, location);
  }
  return unsupported == CTOOL_TRUE ? cir_unsupported_type(context, location)
                                   : CTOOL_OK;
}

static ctool_status_t cir_require_structure_value(
    cir_context_t *context, ctool_u32 type,
    const ctool_c_pp_location_t *location) {
  ctool_status_t status =
      cir_require_initializable_aggregate(context, type, location);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_type_is_structure_value(context, type) == CTOOL_TRUE
             ? CTOOL_OK
             : cir_unsupported_type(context, location);
}

static ctool_status_t cir_append_initializer_object_address(
    cir_context_t *context, const cir_initializer_object_t *object,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location) {
  ctool_status_t status;
  if (object == (const cir_initializer_object_t *)0 ||
      (object->address_kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS &&
       object->address_kind !=
           CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS &&
       object->address_kind !=
           CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS)) {
    return cir_invalid_unit(context, location);
  }
  status = cir_append_instruction(
      context, object->address_kind, object->type,
      CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, object->reference, 0u, location,
      physical_location, (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_ADDRESS, object->type);
}

static ctool_status_t cir_append_initializer_path(
    cir_context_t *context, const cir_initializer_object_t *object,
    ctool_u32 path_count,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location) {
  ctool_u32 index;
  ctool_status_t status = cir_append_initializer_object_address(
      context, object, location, physical_location);
  for (index = 0u; status == CTOOL_OK && index < path_count; index++) {
    const cir_initializer_path_t *path = &context->initializer_path[index];
    cir_stack_entry_t parent;
    status = cir_pop(context, &parent);
    if (status != CTOOL_OK) {
      return status;
    }
    if (parent.kind != CIR_STACK_ADDRESS ||
        parent.type != path->parent_type) {
      return cir_invalid_unit(context, location);
    }
    status = cir_append_instruction(
        context,
        path->kind == CIR_INITIALIZER_ARRAY_ELEMENT
            ? CTOOL_C_IR_INSTRUCTION_ELEMENT_ADDRESS
            : CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS,
        path->child_type, path->parent_type,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
        path->subobject, 0u, location, physical_location,
        (ctool_u32 *)0);
    if (status == CTOOL_OK) {
      status = cir_push(context, CIR_STACK_ADDRESS, path->child_type);
    }
  }
  return status;
}

static ctool_status_t cir_lower_string_initializer_leaf(
    cir_context_t *context, const cir_initializer_object_t *object,
    ctool_u32 initializer_index, ctool_u32 path_count) {
  const ctool_c_initializer_t *initializer;
  cir_stack_entry_t address;
  ctool_status_t status;
  if (initializer_index >= context->unit->initializer_count) {
    return cir_invalid_unit(
        context, object != (const cir_initializer_object_t *)0
                     ? object->location
                     : (const ctool_c_pp_location_t *)0);
  }
  initializer = &context->unit->initializers[initializer_index];
  if (object == (const cir_initializer_object_t *)0 ||
      initializer->kind != CTOOL_C_INITIALIZER_STRING ||
      initializer->expression != CTOOL_C_AST_NONE ||
      initializer->integer_bits != 0u ||
      initializer->string_bytes.data == (const ctool_u8 *)0 ||
      initializer->string_bytes.size == 0u ||
      initializer->address_kind != CTOOL_C_INITIALIZER_ADDRESS_NONE ||
      initializer->address_reference != CTOOL_C_AST_NONE ||
      initializer->address_addend != 0 ||
      initializer->first_element != CTOOL_C_AST_NONE ||
      initializer->element_count != 0u ||
      cir_type_is_character_array(context, initializer->type) ==
          CTOOL_FALSE ||
      initializer->type >= context->unit->layout.type_count ||
      initializer->string_bytes.size >
          context->unit->layout.types[initializer->type].size) {
    return cir_invalid_unit(context, &initializer->location);
  }
  status = cir_append_initializer_path(
      context, object, path_count, &initializer->location,
      &initializer->physical_location);
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (address.kind != CIR_STACK_ADDRESS ||
      address.type != initializer->type) {
    return cir_invalid_unit(context, &initializer->location);
  }
  return cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_COPY_STRING, initializer->type,
      initializer->type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, initializer_index, 0u,
      &initializer->location, &initializer->physical_location,
      (ctool_u32 *)0);
}

static ctool_status_t cir_lower_aggregate_initializer_leaf(
    cir_context_t *context, const cir_initializer_object_t *object,
    const ctool_c_initializer_t *initializer, ctool_u32 path_count,
    ctool_u32 expression_depth) {
  cir_stack_entry_t address;
  cir_stack_entry_t value;
  ctool_u32 base_depth;
  ctool_status_t status;
  if (initializer->kind != CTOOL_C_INITIALIZER_EXPRESSION ||
      initializer->expression >= context->unit->expression_count ||
      initializer->integer_bits != 0u ||
      initializer->string_bytes.data != (const ctool_u8 *)0 ||
      initializer->string_bytes.size != 0u ||
      initializer->address_kind != CTOOL_C_INITIALIZER_ADDRESS_NONE ||
      initializer->address_reference != CTOOL_C_AST_NONE ||
      initializer->address_addend != 0 ||
      initializer->first_element != CTOOL_C_AST_NONE ||
      initializer->element_count != 0u) {
    return cir_invalid_unit(context, &initializer->location);
  }
  if (cir_type_is_value_scalar(context, initializer->type) ==
      CTOOL_FALSE) {
    const ctool_c_type_node_t *type =
        cir_unwrapped_type(context, initializer->type);
    if (type == (const ctool_c_type_node_t *)0 ||
        type->kind != CTOOL_C_TYPE_RECORD) {
      return cir_unsupported_initializer_leaf(
          context, &initializer->location,
          "CupidC IR lowering does not yet support this value type in "
          "automatic aggregate initializer lists");
    }
    status = cir_require_structure_value(
        context, initializer->type, &initializer->location);
    if (status == CTOOL_OK) {
      status = cir_require_structure_value(
          context,
          context->unit->expressions[initializer->expression].type,
          &initializer->location);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (cir_structure_value_types_match(
            context, initializer->type,
            context->unit->expressions[initializer->expression].type) ==
        CTOOL_FALSE) {
      return cir_invalid_unit(context, &initializer->location);
    }
  } else if (cir_type_has_atomic_qualification(
                 context, initializer->type) == CTOOL_TRUE) {
    return cir_unsupported_initializer_leaf(
        context, &initializer->location,
        "CupidC IR lowering does not yet support this value type in "
        "automatic aggregate initializer lists");
  }
  status = cir_append_initializer_path(
      context, object, path_count, &initializer->location,
      &initializer->physical_location);
  base_depth = context->stack_depth;
  if (status == CTOOL_OK) {
    status = cir_lower_expression(
        context, initializer->expression, expression_depth);
  }
  if (status == CTOOL_OK &&
      (cir_add_overflows(base_depth, 1u) == CTOOL_TRUE ||
       context->stack_depth != base_depth + 1u)) {
    return cir_invalid_unit(context, &initializer->location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &value);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (value.kind != CIR_STACK_VALUE ||
      address.kind != CIR_STACK_ADDRESS ||
      address.type != initializer->type ||
      (cir_type_is_value_scalar(context, initializer->type) == CTOOL_TRUE
           ? cir_scalar_value_types_match(
                 context, initializer->type, value.type) == CTOOL_FALSE
           : cir_structure_value_types_match(
                 context, initializer->type, value.type) == CTOOL_FALSE)) {
    return cir_invalid_unit(context, &initializer->location);
  }
  return cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_STORE, initializer->type, value.type,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
      CTOOL_C_AST_NONE, 0u, &initializer->location,
      &initializer->physical_location, (ctool_u32 *)0);
}

static ctool_status_t cir_lower_aggregate_initializer_list(
    cir_context_t *context, const cir_initializer_object_t *object,
    ctool_u32 initializer_index, ctool_u32 depth,
    ctool_u32 expression_depth) {
  const ctool_c_initializer_t *initializer;
  const ctool_c_type_node_t *parent;
  ctool_u32 edge_offset;
  if (initializer_index >= context->unit->initializer_count ||
      depth >= CTOOL_C_PARSE_NESTING_LIMIT) {
    return cir_invalid_unit(
        context, object != (const cir_initializer_object_t *)0
                     ? object->location
                     : (const ctool_c_pp_location_t *)0);
  }
  initializer = &context->unit->initializers[initializer_index];
  parent = cir_unwrapped_type(context, initializer->type);
  if (initializer->kind != CTOOL_C_INITIALIZER_LIST ||
      parent == (const ctool_c_type_node_t *)0 ||
      initializer->expression != CTOOL_C_AST_NONE ||
      initializer->integer_bits != 0u ||
      initializer->string_bytes.data != (const ctool_u8 *)0 ||
      initializer->string_bytes.size != 0u ||
      initializer->address_kind != CTOOL_C_INITIALIZER_ADDRESS_NONE ||
      initializer->address_reference != CTOOL_C_AST_NONE ||
      initializer->address_addend != 0 || initializer->element_count == 0u ||
      initializer->first_element == CTOOL_C_AST_NONE ||
      initializer->first_element > context->unit->initializer_element_count ||
      initializer->element_count >
          context->unit->initializer_element_count -
              initializer->first_element) {
    return cir_invalid_unit(context, &initializer->location);
  }
  if ((parent->kind == CTOOL_C_TYPE_ARRAY &&
       (parent->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
        parent->referenced_type >= context->unit->graph.type_count)) ||
      (parent->kind == CTOOL_C_TYPE_RECORD &&
       (parent->record_kind != CTOOL_C_RECORD_STRUCT ||
        parent->record_complete == CTOOL_FALSE ||
        parent->first_member > context->unit->graph.member_count ||
        parent->member_count >
            context->unit->graph.member_count - parent->first_member)) ||
      (parent->kind != CTOOL_C_TYPE_ARRAY &&
       parent->kind != CTOOL_C_TYPE_RECORD)) {
    return cir_invalid_unit(context, &initializer->location);
  }
  for (edge_offset = 0u; edge_offset < initializer->element_count;
       edge_offset++) {
    const ctool_c_initializer_element_t *edge =
        &context->unit->initializer_elements[initializer->first_element +
                                             edge_offset];
    const ctool_c_initializer_t *child;
    cir_initializer_path_t *path = &context->initializer_path[depth];
    ctool_u32 previous;
    if (edge->initializer >= initializer_index) {
      return cir_invalid_unit(context, &initializer->location);
    }
    child = &context->unit->initializers[edge->initializer];
    for (previous = 0u; previous < edge_offset; previous++) {
      if (context->unit
              ->initializer_elements[initializer->first_element + previous]
              .subobject == edge->subobject) {
        return cir_invalid_unit(context, &initializer->location);
      }
    }
    path->parent_type = initializer->type;
    path->subobject = edge->subobject;
    if (parent->kind == CTOOL_C_TYPE_ARRAY) {
      if (edge->subobject >= parent->element_count) {
        return cir_invalid_unit(context, &initializer->location);
      }
      path->kind = CIR_INITIALIZER_ARRAY_ELEMENT;
      path->child_type = parent->referenced_type;
    } else {
      const ctool_c_record_member_t *member;
      if (edge->subobject < parent->first_member ||
          edge->subobject - parent->first_member >= parent->member_count ||
          edge->subobject >= context->unit->layout.member_count) {
        return cir_invalid_unit(context, &initializer->location);
      }
      member = &context->unit->graph.members[edge->subobject];
      if (member->type >= context->unit->graph.type_count) {
        return cir_invalid_unit(context, &initializer->location);
      }
      if (member->is_bit_field == CTOOL_TRUE) {
        return cir_unsupported_initializer_leaf(
            context, &child->location,
            "CupidC IR lowering does not yet support bit-field leaves in "
            "automatic aggregate initializer lists");
      }
      path->kind = CIR_INITIALIZER_RECORD_MEMBER;
      path->child_type = member->type;
    }
    if (child->type != path->child_type) {
      return cir_invalid_unit(context, &child->location);
    }
    if (child->kind == CTOOL_C_INITIALIZER_LIST) {
      ctool_status_t status = cir_lower_aggregate_initializer_list(
          context, object, edge->initializer, depth + 1u,
          expression_depth);
      if (status != CTOOL_OK) {
        return status;
      }
    } else if (child->kind == CTOOL_C_INITIALIZER_EXPRESSION) {
      ctool_status_t status = cir_lower_aggregate_initializer_leaf(
          context, object, child, depth + 1u, expression_depth);
      if (status != CTOOL_OK) {
        return status;
      }
    } else if (child->kind == CTOOL_C_INITIALIZER_STRING) {
      ctool_status_t status = cir_lower_string_initializer_leaf(
          context, object, edge->initializer, depth + 1u);
      if (status != CTOOL_OK) {
        return status;
      }
    } else {
      return cir_invalid_unit(context, &child->location);
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_aggregate_initializer(
    cir_context_t *context, const cir_initializer_object_t *object,
    ctool_u32 initializer_index, ctool_u32 expression_depth) {
  const ctool_c_initializer_t *initializer;
  cir_stack_entry_t address;
  ctool_status_t status;
  if (object == (const cir_initializer_object_t *)0 ||
      initializer_index >= context->unit->initializer_count) {
    return cir_invalid_unit(
        context, object != (const cir_initializer_object_t *)0
                     ? object->location
                     : (const ctool_c_pp_location_t *)0);
  }
  initializer = &context->unit->initializers[initializer_index];
  if (initializer->type != object->type) {
    return cir_invalid_unit(context, &initializer->location);
  }
  if (initializer->kind != CTOOL_C_INITIALIZER_LIST) {
    return cir_unsupported_type(context, &initializer->location);
  }
  status = cir_require_initializable_aggregate(
      context, object->type, &initializer->location);
  if (status == CTOOL_OK) {
    status = cir_append_initializer_object_address(
        context, object, &initializer->location,
        &initializer->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (address.kind != CIR_STACK_ADDRESS || address.type != object->type) {
    return cir_invalid_unit(context, &initializer->location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT, object->type,
      object->type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &initializer->location, &initializer->physical_location,
      (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_lower_aggregate_initializer_list(
      context, object, initializer_index, 0u, expression_depth);
}

static ctool_status_t cir_lower_string_initializer(
    cir_context_t *context, const cir_initializer_object_t *object,
    ctool_u32 initializer_index) {
  const ctool_c_initializer_t *initializer;
  cir_stack_entry_t address;
  ctool_status_t status;
  if (object == (const cir_initializer_object_t *)0 ||
      initializer_index >= context->unit->initializer_count) {
    return cir_invalid_unit(
        context, object != (const cir_initializer_object_t *)0
                     ? object->location
                     : (const ctool_c_pp_location_t *)0);
  }
  initializer = &context->unit->initializers[initializer_index];
  if (initializer->type != object->type ||
      initializer->kind != CTOOL_C_INITIALIZER_STRING ||
      cir_type_is_character_array(context, object->type) == CTOOL_FALSE) {
    return cir_invalid_unit(context, &initializer->location);
  }
  status = cir_require_initializable_aggregate(
      context, object->type, &initializer->location);
  if (status == CTOOL_OK) {
    status = cir_append_initializer_object_address(
        context, object, &initializer->location,
        &initializer->physical_location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (address.kind != CIR_STACK_ADDRESS || address.type != object->type) {
    return cir_invalid_unit(context, &initializer->location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_ZERO_OBJECT, object->type,
      object->type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &initializer->location, &initializer->physical_location,
      (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_lower_string_initializer_leaf(
      context, object, initializer_index, 0u);
}

static ctool_status_t cir_lower_expression_initializer(
    cir_context_t *context, const cir_initializer_object_t *object,
    const ctool_c_initializer_t *initializer, ctool_u32 expression_limit,
    ctool_u32 expression_depth) {
  cir_stack_entry_t address;
  cir_stack_entry_t value;
  ctool_u32 base_depth;
  ctool_status_t status;
  if (object == (const cir_initializer_object_t *)0 ||
      initializer == (const ctool_c_initializer_t *)0 ||
      initializer->kind != CTOOL_C_INITIALIZER_EXPRESSION ||
      initializer->type != object->type ||
      initializer->expression >= context->unit->expression_count ||
      (expression_limit != CTOOL_C_AST_NONE &&
       initializer->expression >= expression_limit) ||
      initializer->integer_bits != 0u ||
      initializer->string_bytes.data != (const ctool_u8 *)0 ||
      initializer->string_bytes.size != 0u ||
      initializer->address_kind != CTOOL_C_INITIALIZER_ADDRESS_NONE ||
      initializer->address_reference != CTOOL_C_AST_NONE ||
      initializer->address_addend != 0 ||
      initializer->first_element != CTOOL_C_AST_NONE ||
      initializer->element_count != 0u) {
    return cir_invalid_unit(
        context, initializer != (const ctool_c_initializer_t *)0
                     ? &initializer->location
                     : object != (const cir_initializer_object_t *)0
                           ? object->location
                           : (const ctool_c_pp_location_t *)0);
  }
  if (cir_type_is_value_scalar(context, object->type) == CTOOL_TRUE) {
    if (cir_type_is_value_scalar(
            context,
            context->unit->expressions[initializer->expression].type) ==
            CTOOL_FALSE ||
        cir_scalar_value_types_match(
            context, initializer->type,
            context->unit->expressions[initializer->expression].type) ==
        CTOOL_FALSE) {
      return cir_invalid_unit(context, &initializer->location);
    }
  } else {
    status = cir_require_structure_value(
        context, object->type, &initializer->location);
    if (status == CTOOL_OK) {
      status = cir_require_structure_value(
          context,
          context->unit->expressions[initializer->expression].type,
          &initializer->location);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (cir_structure_value_types_match(
            context, object->type,
            context->unit->expressions[initializer->expression].type) ==
        CTOOL_FALSE) {
      return cir_invalid_unit(context, &initializer->location);
    }
  }
  status = cir_append_initializer_object_address(
      context, object, object->location, object->physical_location);
  base_depth = context->stack_depth;
  if (status == CTOOL_OK) {
    status = cir_lower_expression(
        context, initializer->expression, expression_depth);
  }
  if (status == CTOOL_OK &&
      (cir_add_overflows(base_depth, 1u) == CTOOL_TRUE ||
       context->stack_depth != base_depth + 1u)) {
    return cir_invalid_unit(context, &initializer->location);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &value);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &address);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (value.kind != CIR_STACK_VALUE ||
      address.kind != CIR_STACK_ADDRESS || address.type != object->type ||
      (cir_type_is_value_scalar(context, object->type) == CTOOL_TRUE
           ? cir_scalar_value_types_match(
                 context, object->type, value.type) == CTOOL_FALSE
           : cir_structure_value_types_match(
                 context, object->type, value.type) == CTOOL_FALSE)) {
    return cir_invalid_unit(context, &initializer->location);
  }
  return cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_STORE, object->type, value.type,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
      CTOOL_C_AST_NONE, 0u, &initializer->location,
      &initializer->physical_location, (ctool_u32 *)0);
}

static ctool_status_t cir_lower_compound_literal_expression(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  const ctool_c_type_layout_t *layout;
  const ctool_c_initializer_t *initializer;
  cir_initializer_object_t object;
  cir_initializer_object_t staging;
  cir_stack_entry_t destination;
  cir_stack_entry_t source;
  ctool_status_t status;
  if (expression->child_count != 0u ||
      expression->first_child != CTOOL_C_AST_NONE ||
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      expression->conversion != CTOOL_C_CONVERSION_NONE ||
      expression->computation_type != CTOOL_C_TYPE_NONE ||
      expression->integer_bits != 0u ||
      expression->string_bytes.data != (const ctool_u8 *)0 ||
      expression->string_bytes.size != 0u ||
      expression->reference >= context->unit->initializer_count ||
      expression->type >= context->unit->layout.type_count) {
    return cir_invalid_unit(context, &expression->location);
  }
  layout = &context->unit->layout.types[expression->type];
  initializer = &context->unit->initializers[expression->reference];
  if (initializer->type != expression->type ||
      layout->is_object == CTOOL_FALSE ||
      layout->is_complete_object == CTOOL_FALSE || layout->size == 0u ||
      layout->alignment == 0u ||
      (layout->alignment & (layout->alignment - 1u)) != 0u) {
    return cir_invalid_unit(context, &expression->location);
  }
  if ((cir_type_is_value_scalar(context, expression->type) ==
           CTOOL_FALSE &&
       cir_type_is_complete_aggregate_object(context, expression->type) ==
           CTOOL_FALSE) ||
      layout->alignment > 4u) {
    return cir_unsupported_type(context, &expression->location);
  }
  object.address_kind =
      CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_ADDRESS;
  object.reference = expression_index;
  object.type = expression->type;
  object.location = &expression->location;
  object.physical_location = &expression->physical_location;
  if (initializer->kind == CTOOL_C_INITIALIZER_LIST) {
    if (cir_type_is_complete_aggregate_object(
            context, expression->type) == CTOOL_FALSE) {
      return cir_invalid_unit(context, &initializer->location);
    }
    staging = object;
    staging.address_kind =
        CTOOL_C_IR_INSTRUCTION_COMPOUND_LITERAL_STAGING_ADDRESS;
    status = cir_lower_aggregate_initializer(
        context, &staging, expression->reference, depth + 1u);
    if (status == CTOOL_OK) {
      status = cir_append_initializer_object_address(
          context, &object, &expression->location,
          &expression->physical_location);
    }
    if (status == CTOOL_OK) {
      status = cir_append_initializer_object_address(
          context, &staging, &expression->location,
          &expression->physical_location);
    }
    if (status == CTOOL_OK) {
      status = cir_pop(context, &source);
    }
    if (status == CTOOL_OK) {
      status = cir_pop(context, &destination);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (source.kind != CIR_STACK_ADDRESS ||
        destination.kind != CIR_STACK_ADDRESS ||
        source.type != expression->type ||
        destination.type != expression->type) {
      return cir_invalid_unit(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_COPY_OBJECT, expression->type,
        expression->type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &expression->location, &expression->physical_location,
        (ctool_u32 *)0);
  } else if (initializer->kind == CTOOL_C_INITIALIZER_EXPRESSION) {
    status = cir_lower_expression_initializer(
        context, &object, initializer, expression_index, depth + 1u);
  } else if (initializer->kind == CTOOL_C_INITIALIZER_STRING) {
    status = cir_lower_string_initializer(
        context, &object, expression->reference);
  } else {
    return cir_invalid_unit(context, &initializer->location);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_append_initializer_object_address(
      context, &object, &expression->location,
      &expression->physical_location);
}

static ctool_status_t cir_validate_block_enumerator(
    cir_context_t *context, ctool_u32 binding_index,
    ctool_u32 activation_expression,
    ctool_u32 activation_initializer,
    const ctool_c_pp_location_t *owner_location) {
  const ctool_c_block_binding_t *binding;
  const ctool_c_type_layout_t *enum_layout;
  if (binding_index >= context->unit->block_binding_count) {
    return cir_invalid_unit(context, owner_location);
  }
  binding = &context->unit->block_bindings[binding_index];
  if (binding->kind != CTOOL_C_BINDING_ENUMERATOR ||
      binding->storage != CTOOL_C_STORAGE_NONE ||
      binding->type >= context->unit->layout.type_count ||
      binding->linkage_binding != CTOOL_C_AST_NONE ||
      binding->initializer != CTOOL_C_AST_NONE ||
      binding->activation_expression != activation_expression ||
      binding->activation_initializer != activation_initializer ||
      (binding->integer_unsigned != CTOOL_FALSE &&
       binding->integer_unsigned != CTOOL_TRUE)) {
    return cir_invalid_unit(context, &binding->location);
  }
  enum_layout = &context->unit->layout.types[binding->type];
  if (enum_layout->is_integer != CTOOL_TRUE ||
      enum_layout->is_object != CTOOL_TRUE ||
      enum_layout->is_complete_object != CTOOL_TRUE ||
      (enum_layout->size != 4u && enum_layout->size != 8u) ||
      (enum_layout->size == 4u &&
       cir_i32_bits_are_canonical(
           binding->integer_bits,
           binding->integer_unsigned == CTOOL_FALSE ? CTOOL_TRUE
                                                     : CTOOL_FALSE) ==
           CTOOL_FALSE)) {
    return cir_invalid_unit(context, &binding->location);
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_declaration(
    cir_context_t *context, const ctool_c_statement_t *statement) {
  ctool_u32 binding_offset;
  for (binding_offset = 0u;
       binding_offset < statement->block_binding_count; binding_offset++) {
    const ctool_c_block_binding_t *binding;
    const ctool_c_type_layout_t *layout;
    const ctool_c_initializer_t *initializer;
    cir_initializer_object_t initializer_object;
    ctool_u32 binding_index =
        statement->first_block_binding + binding_offset;
    ctool_status_t status;
    if (binding_index >= context->unit->block_binding_count) {
      return cir_invalid_unit(context, &statement->location);
    }
    binding = &context->unit->block_bindings[binding_index];
    initializer_object.address_kind =
        CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS;
    initializer_object.reference = binding_index;
    initializer_object.type = binding->type;
    initializer_object.location = &binding->location;
    initializer_object.physical_location = &binding->physical_location;
    if (binding->kind < CTOOL_C_BINDING_TYPEDEF ||
        binding->kind > CTOOL_C_BINDING_ENUMERATOR ||
        binding->storage < CTOOL_C_STORAGE_NONE ||
        binding->storage > CTOOL_C_STORAGE_REGISTER) {
      return cir_invalid_unit(context, &binding->location);
    }
    if (binding->kind != CTOOL_C_BINDING_ENUMERATOR &&
        (binding->integer_bits != 0ull ||
         binding->integer_unsigned != CTOOL_FALSE ||
         binding->activation_expression != CTOOL_C_AST_NONE ||
         binding->activation_initializer != CTOOL_C_AST_NONE)) {
      return cir_invalid_unit(context, &binding->location);
    }
    if (binding->kind == CTOOL_C_BINDING_ENUMERATOR) {
      status = cir_validate_block_enumerator(
          context, binding_index, binding->activation_expression,
          binding->activation_initializer,
          &statement->location);
      if (status != CTOOL_OK) {
        return status;
      }
      continue;
    }
    if (binding->kind == CTOOL_C_BINDING_TYPEDEF) {
      if (binding->storage != CTOOL_C_STORAGE_TYPEDEF ||
          binding->type >= context->unit->graph.type_count ||
          binding->linkage_binding != CTOOL_C_AST_NONE ||
          binding->initializer != CTOOL_C_AST_NONE) {
        return cir_invalid_unit(context, &binding->location);
      }
      continue;
    }
    if (binding->kind == CTOOL_C_BINDING_FUNCTION) {
      const ctool_c_binding_t *linked;
      const ctool_c_type_node_t *block_function;
      const ctool_c_type_node_t *linked_function;
      ctool_bool compatible;
      if ((binding->storage != CTOOL_C_STORAGE_NONE &&
           binding->storage != CTOOL_C_STORAGE_EXTERN) ||
          binding->initializer != CTOOL_C_AST_NONE ||
          binding->type >= context->unit->graph.type_count ||
          binding->linkage_binding >= context->unit->binding_count) {
        return cir_invalid_unit(context, &binding->location);
      }
      linked = &context->unit->bindings[binding->linkage_binding];
      if (linked->type >= context->unit->graph.type_count) {
        return cir_invalid_unit(context, &binding->location);
      }
      block_function = cir_unwrapped_type(context, binding->type);
      linked_function = cir_unwrapped_type(context, linked->type);
      if (block_function == (const ctool_c_type_node_t *)0 ||
          linked_function == (const ctool_c_type_node_t *)0 ||
          block_function->kind != CTOOL_C_TYPE_FUNCTION ||
          linked_function->kind != CTOOL_C_TYPE_FUNCTION) {
        return cir_invalid_unit(context, &binding->location);
      }
      compatible =
          cir_types_compatible(context, binding->type, linked->type);
      if (context->relation_status != CTOOL_OK) {
        return context->relation_status;
      }
      if (linked->kind != CTOOL_C_BINDING_FUNCTION ||
          (linked->storage != CTOOL_C_STORAGE_NONE &&
           linked->storage != CTOOL_C_STORAGE_EXTERN &&
           linked->storage != CTOOL_C_STORAGE_STATIC) ||
          !((linked->storage == CTOOL_C_STORAGE_STATIC &&
             linked->linkage == CTOOL_C_LINKAGE_INTERNAL) ||
            ((linked->storage == CTOOL_C_STORAGE_NONE ||
              linked->storage == CTOOL_C_STORAGE_EXTERN) &&
             linked->linkage == CTOOL_C_LINKAGE_EXTERNAL)) ||
          (linked->file_scope_visible != CTOOL_FALSE &&
           linked->file_scope_visible != CTOOL_TRUE) ||
          (linked->file_scope_visible == CTOOL_FALSE &&
           linked->linkage != CTOOL_C_LINKAGE_EXTERNAL) ||
          cir_string_equal(binding->name, linked->name) == CTOOL_FALSE ||
          compatible == CTOOL_FALSE) {
        return cir_invalid_unit(context, &binding->location);
      }
      continue;
    }
    if (binding->kind != CTOOL_C_BINDING_OBJECT) {
      return cir_unsupported_statement(context, &binding->location);
    }
    if (binding->storage == CTOOL_C_STORAGE_EXTERN) {
      const ctool_c_binding_t *linked;
      ctool_bool compatible;
      if (binding->initializer != CTOOL_C_AST_NONE ||
          binding->type >= context->unit->graph.type_count ||
          binding->linkage_binding >= context->unit->binding_count) {
        return cir_invalid_unit(context, &binding->location);
      }
      linked = &context->unit->bindings[binding->linkage_binding];
      compatible =
          cir_types_compatible(context, binding->type, linked->type);
      if (context->relation_status != CTOOL_OK) {
        return context->relation_status;
      }
      if (linked->kind != CTOOL_C_BINDING_OBJECT ||
          linked->type >= context->unit->graph.type_count ||
          (linked->storage != CTOOL_C_STORAGE_NONE &&
           linked->storage != CTOOL_C_STORAGE_EXTERN &&
           linked->storage != CTOOL_C_STORAGE_STATIC) ||
          (linked->linkage != CTOOL_C_LINKAGE_INTERNAL &&
           linked->linkage != CTOOL_C_LINKAGE_EXTERNAL) ||
          !((linked->storage == CTOOL_C_STORAGE_STATIC &&
             linked->linkage == CTOOL_C_LINKAGE_INTERNAL) ||
            ((linked->storage == CTOOL_C_STORAGE_NONE ||
              linked->storage == CTOOL_C_STORAGE_EXTERN) &&
             linked->linkage == CTOOL_C_LINKAGE_EXTERNAL)) ||
          (linked->file_scope_visible != CTOOL_FALSE &&
           linked->file_scope_visible != CTOOL_TRUE) ||
          (linked->file_scope_visible == CTOOL_FALSE &&
           linked->storage != CTOOL_C_STORAGE_EXTERN) ||
          cir_string_equal(binding->name, linked->name) == CTOOL_FALSE ||
          compatible == CTOOL_FALSE) {
        return cir_invalid_unit(context, &binding->location);
      }
      continue;
    }
    if (binding->storage != CTOOL_C_STORAGE_NONE &&
        binding->storage != CTOOL_C_STORAGE_AUTO &&
        binding->storage != CTOOL_C_STORAGE_REGISTER &&
        binding->storage != CTOOL_C_STORAGE_STATIC) {
      return cir_unsupported_statement(context, &binding->location);
    }
    if (binding->linkage_binding != CTOOL_C_AST_NONE) {
      return cir_invalid_unit(context, &binding->location);
    }
    if (binding->type >= context->unit->layout.type_count) {
      return cir_invalid_unit(context, &binding->location);
    }
    layout = &context->unit->layout.types[binding->type];
    if (layout->is_complete_object == CTOOL_FALSE ||
        layout->is_object == CTOOL_FALSE || layout->size == 0u ||
        layout->alignment == 0u ||
        (layout->alignment & (layout->alignment - 1u)) != 0u) {
      return cir_invalid_unit(context, &binding->location);
    }
    if (binding->storage == CTOOL_C_STORAGE_STATIC) {
      if (binding->initializer >= context->unit->initializer_count) {
        return cir_invalid_unit(context, &binding->location);
      }
      initializer = &context->unit->initializers[binding->initializer];
      if (initializer->type != binding->type ||
          initializer->kind < CTOOL_C_INITIALIZER_ZERO ||
          initializer->kind > CTOOL_C_INITIALIZER_LIST) {
        return cir_invalid_unit(context, &initializer->location);
      }
      continue;
    }
    if ((cir_type_is_value_scalar(context, binding->type) ==
             CTOOL_FALSE &&
         cir_type_is_complete_aggregate_object(context, binding->type) ==
             CTOOL_FALSE) ||
        layout->alignment > 4u) {
      return cir_unsupported_type(context, &binding->location);
    }
    if (binding->initializer == CTOOL_C_AST_NONE) {
      continue;
    }
    if (binding->initializer >= context->unit->initializer_count) {
      return cir_invalid_unit(context, &binding->location);
    }
    initializer = &context->unit->initializers[binding->initializer];
    if (cir_type_is_value_scalar(context, binding->type) ==
        CTOOL_FALSE) {
      if (initializer->kind == CTOOL_C_INITIALIZER_LIST) {
        status = cir_lower_aggregate_initializer(
            context, &initializer_object, binding->initializer, 0u);
      } else if (initializer->kind == CTOOL_C_INITIALIZER_STRING) {
        status = cir_lower_string_initializer(
            context, &initializer_object, binding->initializer);
      } else if (initializer->kind == CTOOL_C_INITIALIZER_EXPRESSION) {
        status = cir_lower_expression_initializer(
            context, &initializer_object, initializer, CTOOL_C_AST_NONE,
            0u);
      } else {
        status = cir_invalid_unit(context, &initializer->location);
      }
      if (status != CTOOL_OK) {
        return status;
      }
      continue;
    }
    status = cir_lower_expression_initializer(
        context, &initializer_object, initializer, CTOOL_C_AST_NONE, 0u);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_return(
    cir_context_t *context, const ctool_c_statement_t *statement) {
  cir_stack_entry_t result;
  ctool_status_t status;
  if (statement->kind != CTOOL_C_STATEMENT_RETURN) {
    return cir_unsupported_statement(context, &statement->location);
  }
  if (cir_type_is_void(context, context->function_result_type) == CTOOL_TRUE) {
    if (statement->expression != CTOOL_C_AST_NONE) {
      return cir_invalid_unit(context, &statement->location);
    }
    return cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_RETURN_VOID, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &statement->location, &statement->physical_location,
        (ctool_u32 *)0);
  }
  if (statement->expression == CTOOL_C_AST_NONE) {
    return cir_invalid_unit(context, &statement->location);
  }
  status = cir_lower_expression(context, statement->expression, 0u);
  if (status == CTOOL_OK) {
    status = cir_pop(context, &result);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (result.kind != CIR_STACK_VALUE || context->stack_depth != 0u) {
    return cir_invalid_unit(context, &statement->location);
  }
  if (cir_type_is_value_scalar(context,
                              context->function_result_type) ==
      CTOOL_TRUE) {
    if (result.type != context->function_result_type &&
        cir_scalar_value_types_match(context, context->function_result_type,
                                     result.type) == CTOOL_FALSE) {
      return cir_invalid_unit(context, &statement->location);
    }
  } else {
    status = cir_require_structure_value(
        context, context->function_result_type, &statement->location);
    if (status == CTOOL_OK) {
      status = cir_require_structure_value(
          context, result.type, &statement->location);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (cir_structure_value_types_match(
            context, context->function_result_type,
            result.type) == CTOOL_FALSE) {
      return cir_invalid_unit(context, &statement->location);
    }
  }
  return cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      context->function_result_type, result.type,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
      CTOOL_C_AST_NONE, 0u, &statement->location,
      &statement->physical_location, (ctool_u32 *)0);
}

static void cir_prepare_count_only_validation(cir_context_t *validation,
                                              const cir_context_t *context) {
  *validation = *context;
  validation->functions = (ctool_c_ir_function_t *)0;
  validation->instructions = (ctool_c_ir_instruction_t *)0;
  validation->instruction_count = 0u;
  validation->instruction_capacity = 0u;
  validation->argument_types = (ctool_u32 *)0;
  validation->argument_type_count = 0u;
  validation->argument_type_capacity = 0u;
  validation->function_first_instruction = 0u;
  validation->maximum_stack_depth = 0u;
  validation->count_only_validation = CTOOL_TRUE;
}

static ctool_status_t cir_finish_count_only_validation(
    cir_context_t *context, const cir_context_t *validation,
    ctool_arena_mark_t mark, ctool_status_t status) {
  ctool_status_t rewind_status = ctool_arena_rewind(context->arena, mark);
  if (validation->failure_reported == CTOOL_TRUE) {
    context->failure_reported = CTOOL_TRUE;
  }
  if (status == CTOOL_OK && rewind_status == CTOOL_OK) {
    context->block_binding_cursor = validation->block_binding_cursor;
    context->visible_block_binding_end = validation->visible_block_binding_end;
  }
  return rewind_status == CTOOL_OK ? status : rewind_status;
}

static ctool_status_t cir_lower_statement(cir_context_t *context,
                                          ctool_u32 statement_index,
                                          ctool_u32 depth,
                                          ctool_bool entry_reachable,
                                          ctool_bool *falls_through_out);

static ctool_status_t cir_lower_statement_from_entry(
    cir_context_t *context, ctool_u32 statement_index, ctool_u32 depth,
    ctool_bool entry_reachable, ctool_bool *falls_through_out);

static ctool_bool cir_label_in_function(const cir_context_t *context,
                                        ctool_u32 label) {
  return label >= context->function_first_label &&
                 label - context->function_first_label <
                     context->function_label_count
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cir_patch_goto_jumps(cir_context_t *context,
                                           ctool_u32 label,
                                           ctool_u32 target) {
  ctool_u32 instruction;
  ctool_u64 patch = CIR_GOTO_PATCH_TAG | (ctool_u64)label;
  cir_record_patched_target(context, target);
  if (context->instructions == (ctool_c_ir_instruction_t *)0) {
    return CTOOL_OK;
  }
  for (instruction = context->function_first_instruction;
       instruction < context->instruction_count; instruction++) {
    ctool_c_ir_instruction_t *candidate = &context->instructions[instruction];
    if (candidate->kind == CTOOL_C_IR_INSTRUCTION_JUMP &&
        candidate->reference == CTOOL_C_AST_NONE &&
        candidate->integer_bits == patch) {
      candidate->reference = target;
      candidate->integer_bits = 0u;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_goto(cir_context_t *context,
                                     const ctool_c_statement_t *statement) {
  ctool_u32 target = CTOOL_C_AST_NONE;
  ctool_u64 patch = 0u;
  if (statement->first_child != CTOOL_C_AST_NONE ||
      statement->child_count != 0u ||
      statement->expression != CTOOL_C_AST_NONE ||
      statement->first_block_binding != CTOOL_C_AST_NONE ||
      statement->block_binding_count != 0u ||
      statement->initializer_statement != CTOOL_C_AST_NONE ||
      statement->condition != CTOOL_C_AST_NONE ||
      statement->iteration != CTOOL_C_AST_NONE ||
      statement->body != CTOOL_C_AST_NONE ||
      statement->else_body != CTOOL_C_AST_NONE ||
      cir_label_in_function(context, statement->label) == CTOOL_FALSE) {
    return cir_invalid_unit(context, &statement->location);
  }
  target = context->count_only_validation == CTOOL_TRUE
               ? CTOOL_C_AST_NONE
               : context->label_targets[statement->label];
  if (target == CTOOL_C_AST_NONE) {
    patch = CIR_GOTO_PATCH_TAG | (ctool_u64)statement->label;
  }
  return cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
      CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, target, patch, &statement->location,
      &statement->physical_location, (ctool_u32 *)0);
}

static ctool_status_t cir_lower_label(cir_context_t *context,
                                      ctool_u32 statement_index,
                                      const ctool_c_statement_t *statement,
                                      ctool_u32 depth,
                                      ctool_bool entry_reachable,
                                      ctool_bool *falls_through_out) {
  const ctool_c_label_t *label;
  ctool_bool label_entry_reachable;
  ctool_u32 target;
  ctool_status_t status;
  if (statement->first_child != CTOOL_C_AST_NONE ||
      statement->child_count != 0u ||
      statement->expression != CTOOL_C_AST_NONE ||
      statement->first_block_binding != CTOOL_C_AST_NONE ||
      statement->block_binding_count != 0u ||
      statement->initializer_statement != CTOOL_C_AST_NONE ||
      statement->condition != CTOOL_C_AST_NONE ||
      statement->iteration != CTOOL_C_AST_NONE ||
      statement->else_body != CTOOL_C_AST_NONE ||
      statement->body >= statement_index ||
      statement->body >= context->unit->statement_count ||
      context->unit->statements[statement->body].kind ==
          CTOOL_C_STATEMENT_DECLARATION ||
      cir_label_in_function(context, statement->label) == CTOOL_FALSE) {
    return cir_invalid_unit(context, &statement->location);
  }
  label = &context->unit->labels[statement->label];
  if (label->statement != statement_index ||
      (context->count_only_validation == CTOOL_FALSE &&
       context->label_targets[statement->label] != CTOOL_C_AST_NONE)) {
    return cir_invalid_unit(context, &statement->location);
  }
  target = cir_function_offset(context);
  status = CTOOL_OK;
  if (context->count_only_validation == CTOOL_FALSE) {
    context->label_targets[statement->label] = target;
    status = cir_patch_goto_jumps(context, statement->label, target);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  label_entry_reachable =
      entry_reachable == CTOOL_TRUE ||
              context->label_reachable[statement->label] == CTOOL_TRUE
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  return cir_lower_statement_from_entry(
      context, statement->body, depth + 1u, label_entry_reachable,
      falls_through_out);
}

static ctool_bool cir_statement_contains_reachable_label_inner(
    const cir_context_t *context, ctool_u32 statement_index,
    ctool_u32 depth, ctool_bool switch_labels_reachable) {
  const ctool_c_statement_t *statement;
  ctool_u32 child_offset;
  if (depth >= CTOOL_C_PARSE_NESTING_LIMIT ||
      statement_index >= context->unit->statement_count) {
    return CTOOL_FALSE;
  }
  statement = &context->unit->statements[statement_index];
  if (statement->kind == CTOOL_C_STATEMENT_LABEL) {
    if (cir_label_in_function(context, statement->label) == CTOOL_TRUE &&
        context->label_reachable[statement->label] == CTOOL_TRUE) {
      return CTOOL_TRUE;
    }
    return cir_statement_contains_reachable_label_inner(
        context, statement->body, depth + 1u, switch_labels_reachable);
  }
  if (statement->kind == CTOOL_C_STATEMENT_CASE ||
      statement->kind == CTOOL_C_STATEMENT_DEFAULT) {
    if (switch_labels_reachable == CTOOL_TRUE) {
      return CTOOL_TRUE;
    }
  }
  if (statement->kind == CTOOL_C_STATEMENT_COMPOUND) {
    if (statement->first_child > context->unit->statement_child_count ||
        statement->child_count >
            context->unit->statement_child_count - statement->first_child) {
      return CTOOL_FALSE;
    }
    for (child_offset = 0u; child_offset < statement->child_count;
         child_offset++) {
      ctool_u32 child = context->unit->statement_children
          [statement->first_child + child_offset];
      if (cir_statement_contains_reachable_label_inner(
              context, child, depth + 1u,
              switch_labels_reachable) == CTOOL_TRUE) {
        return CTOOL_TRUE;
      }
    }
    return CTOOL_FALSE;
  }
  if (statement->kind == CTOOL_C_STATEMENT_IF) {
    if (cir_statement_contains_reachable_label_inner(
            context, statement->body, depth + 1u,
            switch_labels_reachable) == CTOOL_TRUE) {
      return CTOOL_TRUE;
    }
    return statement->else_body != CTOOL_C_AST_NONE
               ? cir_statement_contains_reachable_label_inner(
                     context, statement->else_body, depth + 1u,
                     switch_labels_reachable)
               : CTOOL_FALSE;
  }
  if (statement->kind == CTOOL_C_STATEMENT_SWITCH) {
    return cir_statement_contains_reachable_label_inner(
        context, statement->body, depth + 1u, CTOOL_FALSE);
  }
  if (statement->kind == CTOOL_C_STATEMENT_WHILE ||
      statement->kind == CTOOL_C_STATEMENT_DO ||
      statement->kind == CTOOL_C_STATEMENT_FOR ||
      statement->kind == CTOOL_C_STATEMENT_CASE ||
      statement->kind == CTOOL_C_STATEMENT_DEFAULT) {
    return cir_statement_contains_reachable_label_inner(
        context, statement->body, depth + 1u, switch_labels_reachable);
  }
  return CTOOL_FALSE;
}

static ctool_bool cir_statement_contains_reachable_label(
    const cir_context_t *context, ctool_u32 statement_index,
    ctool_u32 depth) {
  ctool_u32 frame_index;
  ctool_bool switch_labels_reachable = CTOOL_FALSE;
  for (frame_index = context->control_depth; frame_index != 0u;
       frame_index--) {
    if (context->control_frames[frame_index - 1u].kind ==
        CIR_CONTROL_SWITCH) {
      switch_labels_reachable =
          context->control_frames[frame_index - 1u]
              .switch_entry_reachable;
      break;
    }
  }
  return cir_statement_contains_reachable_label_inner(
      context, statement_index, depth, switch_labels_reachable);
}

static ctool_status_t cir_validate_unreachable_statement(
    cir_context_t *context, ctool_u32 statement_index, ctool_u32 depth) {
  cir_context_t validation;
  ctool_arena_mark_t mark = ctool_arena_mark(context->arena);
  ctool_bool ignored_fallthrough = CTOOL_TRUE;
  ctool_status_t status;
  cir_prepare_count_only_validation(&validation, context);
  status = cir_lower_statement(&validation, statement_index, depth,
                               CTOOL_FALSE, &ignored_fallthrough);
  return cir_finish_count_only_validation(context, &validation, mark, status);
}

static ctool_status_t cir_lower_statement_from_entry(
    cir_context_t *context, ctool_u32 statement_index, ctool_u32 depth,
    ctool_bool entry_reachable, ctool_bool *falls_through_out) {
  *falls_through_out = CTOOL_FALSE;
  if (entry_reachable == CTOOL_FALSE &&
      cir_statement_contains_reachable_label(
          context, statement_index, depth) == CTOOL_FALSE) {
    return cir_validate_unreachable_statement(context, statement_index,
                                               depth);
  }
  return cir_lower_statement(context, statement_index, depth,
                             entry_reachable, falls_through_out);
}

static ctool_status_t cir_lower_discarded_expression(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location) {
  cir_stack_entry_t discarded;
  ctool_u32 expression_type;
  ctool_status_t status;
  if (expression_index >= context->unit->expression_count) {
    return cir_invalid_unit(context, location);
  }
  expression_type = context->unit->expressions[expression_index].type;
  status = cir_lower_expression(context, expression_index, 0u);
  if (status != CTOOL_OK) {
    return status;
  }
  if (cir_type_is_void(context, expression_type) == CTOOL_TRUE) {
    return context->stack_depth == 0u
               ? CTOOL_OK
               : cir_invalid_unit(context, location);
  }
  status = cir_pop(context, &discarded);
  if (status != CTOOL_OK) {
    return status;
  }
  if (discarded.kind != CIR_STACK_VALUE ||
      discarded.type != expression_type || context->stack_depth != 0u) {
    return cir_invalid_unit(context, location);
  }
  return cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_DISCARD, CTOOL_C_TYPE_NONE,
      expression_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, location,
      physical_location, (ctool_u32 *)0);
}

static ctool_status_t cir_lower_expression_statement(
    cir_context_t *context, const ctool_c_statement_t *statement) {
  if (statement->expression == CTOOL_C_AST_NONE) {
    return CTOOL_OK;
  }
  return cir_lower_discarded_expression(
      context, statement->expression, &statement->location,
      &statement->physical_location);
}

static ctool_status_t cir_validate_unreachable_discarded_expression(
    cir_context_t *context, ctool_u32 expression_index) {
  cir_context_t validation;
  ctool_arena_mark_t mark = ctool_arena_mark(context->arena);
  ctool_status_t status;
  cir_prepare_count_only_validation(&validation, context);
  if (expression_index >= context->unit->expression_count) {
    status = cir_invalid_unit(&validation, (const ctool_c_pp_location_t *)0);
  } else {
    const ctool_c_expression_t *expression =
        &context->unit->expressions[expression_index];
    status = cir_lower_discarded_expression(
        &validation, expression_index, &expression->location,
        &expression->physical_location);
  }
  return cir_finish_count_only_validation(context, &validation, mark, status);
}

static ctool_status_t cir_lower_compound(cir_context_t *context,
                                         ctool_u32 statement_index,
                                         const ctool_c_statement_t *statement,
                                         ctool_u32 depth,
                                         ctool_bool entry_reachable,
                                         ctool_bool *falls_through_out) {
  ctool_bool falls_through = entry_reachable;
  ctool_u32 child_offset;
  if (statement->first_child > context->unit->statement_child_count ||
      statement->child_count >
          context->unit->statement_child_count - statement->first_child) {
    return cir_invalid_unit(context, &statement->location);
  }
  for (child_offset = 0u; child_offset < statement->child_count;
       child_offset++) {
    ctool_bool child_falls_through = CTOOL_TRUE;
    ctool_u32 child = context->unit
                           ->statement_children[statement->first_child +
                                                child_offset];
    ctool_status_t status;
    if (child >= statement_index || child >= context->unit->statement_count) {
      return cir_invalid_unit(context, &statement->location);
    }
    status = cir_lower_statement_from_entry(
        context, child, depth + 1u, falls_through,
        &child_falls_through);
    if (status != CTOOL_OK) {
      return status;
    }
    if (falls_through == CTOOL_TRUE ||
        cir_statement_contains_reachable_label(
            context, child, depth + 1u) == CTOOL_TRUE) {
      falls_through = child_falls_through;
    }
  }
  *falls_through_out = falls_through;
  return CTOOL_OK;
}

static ctool_status_t cir_lower_scalar_condition_branch(
    cir_context_t *context, ctool_u32 condition_index,
    ctool_u32 *branch_out) {
  const ctool_c_expression_t *condition_expression =
      &context->unit->expressions[condition_index];
  cir_stack_entry_t condition;
  ctool_status_t status;
  *branch_out = CTOOL_C_AST_NONE;
  status = cir_lower_expression(context, condition_index, 0u);
  if (status == CTOOL_OK) {
    status = cir_pop(context, &condition);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (condition.kind != CIR_STACK_VALUE || context->stack_depth != 0u) {
    return cir_invalid_unit(context, &condition_expression->location);
  }
  if (cir_type_is_truth_scalar(context, condition.type) ==
      CTOOL_FALSE) {
    return cir_unsupported_type(context, &condition_expression->location);
  }
  return cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE,
      condition.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &condition_expression->location, &condition_expression->physical_location,
      branch_out);
}

static ctool_status_t cir_validate_unreachable_scalar_condition(
    cir_context_t *context, ctool_u32 condition_index) {
  cir_context_t validation;
  ctool_arena_mark_t mark = ctool_arena_mark(context->arena);
  ctool_u32 ignored_branch;
  ctool_status_t status;
  cir_prepare_count_only_validation(&validation, context);
  status = cir_lower_scalar_condition_branch(&validation, condition_index,
                                             &ignored_branch);
  return cir_finish_count_only_validation(context, &validation, mark, status);
}

static ctool_status_t cir_enter_loop(cir_context_t *context,
                                     ctool_u32 continue_target) {
  cir_control_frame_t *frame;
  if (context->control_depth >= CTOOL_C_PARSE_NESTING_LIMIT) {
    return cir_emit_failure(
        context, CTOOL_ERR_LIMIT, CTOOL_C_IR_DIAG_LIMIT,
        (const ctool_c_pp_location_t *)0,
        "CupidC IR lowering exceeded a configured resource limit");
  }
  frame = &context->control_frames[context->control_depth++];
  frame->kind = CIR_CONTROL_LOOP;
  frame->first_instruction = context->instruction_count;
  frame->continue_target = continue_target;
  frame->condition_type = CTOOL_C_TYPE_NONE;
  frame->has_break = CTOOL_FALSE;
  frame->has_continue = CTOOL_FALSE;
  frame->switch_entry_reachable = CTOOL_FALSE;
  return CTOOL_OK;
}

static cir_control_frame_t cir_leave_loop(cir_context_t *context) {
  context->control_depth--;
  return context->control_frames[context->control_depth];
}

static ctool_status_t cir_patch_tagged_jumps(
    cir_context_t *context, const cir_control_frame_t *frame,
    ctool_u64 patch, ctool_u32 target) {
  ctool_u32 instruction;
  cir_record_patched_target(context, target);
  if (context->instructions == (ctool_c_ir_instruction_t *)0) {
    return CTOOL_OK;
  }
  if (frame->first_instruction > context->instruction_count) {
    return CTOOL_ERR_INTERNAL;
  }
  for (instruction = frame->first_instruction;
       instruction < context->instruction_count; instruction++) {
    ctool_c_ir_instruction_t *candidate =
        &context->instructions[instruction];
    if (candidate->kind == CTOOL_C_IR_INSTRUCTION_JUMP &&
        candidate->reference == CTOOL_C_AST_NONE &&
        candidate->integer_bits == patch) {
      candidate->reference = target;
      candidate->integer_bits = 0u;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_control_jump(
    cir_context_t *context, const ctool_c_statement_t *statement) {
  cir_control_frame_t *frame;
  ctool_u32 frame_index;
  ctool_u32 reference = CTOOL_C_AST_NONE;
  ctool_u64 patch_kind = 0u;
  if (statement->first_child != CTOOL_C_AST_NONE ||
      statement->child_count != 0u ||
      statement->expression != CTOOL_C_AST_NONE ||
      statement->first_block_binding != CTOOL_C_AST_NONE ||
      statement->block_binding_count != 0u ||
      statement->label != CTOOL_C_AST_NONE ||
      statement->initializer_statement != CTOOL_C_AST_NONE ||
      statement->condition != CTOOL_C_AST_NONE ||
      statement->iteration != CTOOL_C_AST_NONE ||
      statement->body != CTOOL_C_AST_NONE ||
      statement->else_body != CTOOL_C_AST_NONE ||
      context->control_depth == 0u) {
    return cir_invalid_unit(context, &statement->location);
  }
  if (statement->kind == CTOOL_C_STATEMENT_BREAK) {
    frame = &context->control_frames[context->control_depth - 1u];
    frame->has_break = CTOOL_TRUE;
    patch_kind = CIR_CONTROL_PATCH_BREAK;
  } else if (statement->kind == CTOOL_C_STATEMENT_CONTINUE) {
    frame = (cir_control_frame_t *)0;
    for (frame_index = context->control_depth; frame_index != 0u;
         frame_index--) {
      cir_control_frame_t *candidate =
          &context->control_frames[frame_index - 1u];
      if (candidate->kind == CIR_CONTROL_LOOP) {
        frame = candidate;
        break;
      }
    }
    if (frame == (cir_control_frame_t *)0) {
      return cir_invalid_unit(context, &statement->location);
    }
    frame->has_continue = CTOOL_TRUE;
    if (frame->continue_target != CTOOL_C_AST_NONE) {
      reference = frame->continue_target;
    } else {
      patch_kind = CIR_CONTROL_PATCH_CONTINUE;
    }
  } else {
    return cir_invalid_unit(context, &statement->location);
  }
  return cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
      CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, reference, patch_kind, &statement->location,
      &statement->physical_location, (ctool_u32 *)0);
}

static ctool_status_t cir_lower_loop_body(
    cir_context_t *context, ctool_u32 body, ctool_u32 depth,
    ctool_bool entry_reachable, ctool_u32 continue_target,
    cir_control_frame_t *frame_out,
    ctool_bool *falls_through_out) {
  ctool_status_t status = cir_enter_loop(context, continue_target);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cir_lower_statement_from_entry(
      context, body, depth + 1u, entry_reachable, falls_through_out);
  *frame_out = cir_leave_loop(context);
  return status;
}

static ctool_status_t cir_lower_if(cir_context_t *context,
                                   ctool_u32 statement_index,
                                   const ctool_c_statement_t *statement,
                                   ctool_u32 depth,
                                   ctool_bool entry_reachable,
                                   ctool_bool *falls_through_out) {
  ctool_bool body_falls_through = CTOOL_FALSE;
  ctool_bool else_falls_through = CTOOL_FALSE;
  ctool_u32 branch;
  ctool_u32 jump = CTOOL_C_AST_NONE;
  ctool_status_t status;
  if (statement->first_child != CTOOL_C_AST_NONE ||
      statement->child_count != 0u ||
      statement->expression != CTOOL_C_AST_NONE ||
      statement->first_block_binding != CTOOL_C_AST_NONE ||
      statement->block_binding_count != 0u ||
      statement->label != CTOOL_C_AST_NONE ||
      statement->initializer_statement != CTOOL_C_AST_NONE ||
      statement->iteration != CTOOL_C_AST_NONE ||
      statement->condition == CTOOL_C_AST_NONE ||
      statement->condition >= context->unit->expression_count ||
      statement->body >= statement_index ||
      statement->body >= context->unit->statement_count ||
      context->unit->statements[statement->body].kind ==
          CTOOL_C_STATEMENT_DECLARATION ||
      (statement->else_body != CTOOL_C_AST_NONE &&
       (statement->else_body >= statement_index ||
        statement->else_body >= context->unit->statement_count ||
        context->unit->statements[statement->else_body].kind ==
            CTOOL_C_STATEMENT_DECLARATION))) {
    return cir_invalid_unit(context, &statement->location);
  }
  status = cir_lower_scalar_condition_branch(context, statement->condition,
                                             &branch);
  if (status == CTOOL_OK) {
    status = cir_lower_statement_from_entry(
        context, statement->body, depth + 1u, entry_reachable,
        &body_falls_through);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (statement->else_body == CTOOL_C_AST_NONE) {
    status = cir_patch_reference(context, branch,
                                 cir_function_offset(context));
    if (status == CTOOL_OK) {
      *falls_through_out = entry_reachable == CTOOL_TRUE ||
                                   body_falls_through == CTOOL_TRUE
                               ? CTOOL_TRUE
                               : CTOOL_FALSE;
    }
    return status;
  }
  if (body_falls_through == CTOOL_TRUE) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, &statement->location,
        &statement->physical_location, &jump);
  }
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, branch,
                                 cir_function_offset(context));
  }
  if (status == CTOOL_OK) {
    status = cir_lower_statement_from_entry(
        context, statement->else_body, depth + 1u, entry_reachable,
        &else_falls_through);
  }
  if (status == CTOOL_OK && jump != CTOOL_C_AST_NONE) {
    status = cir_patch_reference(context, jump,
                                 cir_function_offset(context));
  }
  if (status == CTOOL_OK) {
    *falls_through_out =
        body_falls_through == CTOOL_TRUE ||
                else_falls_through == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
  }
  return status;
}

static ctool_status_t cir_validate_loop_shape(
    cir_context_t *context, ctool_u32 statement_index,
    const ctool_c_statement_t *statement) {
  if (statement->first_child != CTOOL_C_AST_NONE ||
      statement->child_count != 0u ||
      statement->expression != CTOOL_C_AST_NONE ||
      statement->first_block_binding != CTOOL_C_AST_NONE ||
      statement->block_binding_count != 0u ||
      statement->label != CTOOL_C_AST_NONE ||
      statement->body >= statement_index ||
      statement->body >= context->unit->statement_count ||
      context->unit->statements[statement->body].kind ==
          CTOOL_C_STATEMENT_DECLARATION ||
      statement->else_body != CTOOL_C_AST_NONE) {
    return cir_invalid_unit(context, &statement->location);
  }
  return CTOOL_OK;
}

static ctool_status_t cir_validate_loop_statement(
    cir_context_t *context, ctool_u32 statement_index,
    const ctool_c_statement_t *statement) {
  ctool_status_t status =
      cir_validate_loop_shape(context, statement_index, statement);
  if (status != CTOOL_OK) {
    return status;
  }
  if (statement->initializer_statement != CTOOL_C_AST_NONE ||
      statement->iteration != CTOOL_C_AST_NONE ||
      statement->condition == CTOOL_C_AST_NONE ||
      statement->condition >= context->unit->expression_count) {
    return cir_invalid_unit(context, &statement->location);
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_while(cir_context_t *context,
                                      ctool_u32 statement_index,
                                      const ctool_c_statement_t *statement,
                                      ctool_u32 depth,
                                      ctool_bool entry_reachable,
                                      ctool_bool *falls_through_out) {
  cir_control_frame_t frame;
  ctool_bool body_falls_through = CTOOL_TRUE;
  ctool_u32 condition_target;
  ctool_u32 branch;
  ctool_u32 exit_target;
  ctool_status_t status =
      cir_validate_loop_statement(context, statement_index, statement);
  if (status != CTOOL_OK) {
    return status;
  }
  condition_target = cir_function_offset(context);
  status = cir_lower_scalar_condition_branch(context, statement->condition,
                                             &branch);
  if (status == CTOOL_OK) {
    status = cir_lower_loop_body(context, statement->body, depth,
                                 entry_reachable, condition_target, &frame,
                                 &body_falls_through);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (body_falls_through == CTOOL_TRUE) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, condition_target, 0u, &statement->location,
        &statement->physical_location, (ctool_u32 *)0);
  }
  exit_target = cir_function_offset(context);
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, branch, exit_target);
  }
  if (status == CTOOL_OK && frame.has_break == CTOOL_TRUE) {
    status = cir_patch_tagged_jumps(context, &frame,
                                    CIR_CONTROL_PATCH_BREAK, exit_target);
  }
  if (status == CTOOL_OK) {
    *falls_through_out =
        entry_reachable == CTOOL_TRUE || frame.has_break == CTOOL_TRUE ||
                body_falls_through == CTOOL_TRUE ||
                frame.has_continue == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
  }
  return status;
}

static ctool_status_t cir_lower_do(cir_context_t *context,
                                   ctool_u32 statement_index,
                                   const ctool_c_statement_t *statement,
                                   ctool_u32 depth,
                                   ctool_bool entry_reachable,
                                   ctool_bool *falls_through_out) {
  cir_control_frame_t frame;
  ctool_bool body_falls_through = CTOOL_TRUE;
  ctool_u32 body_target;
  ctool_u32 branch = CTOOL_C_AST_NONE;
  ctool_u32 condition_target;
  ctool_u32 exit_target;
  ctool_status_t status =
      cir_validate_loop_statement(context, statement_index, statement);
  if (status != CTOOL_OK) {
    return status;
  }
  body_target = cir_function_offset(context);
  status = cir_lower_loop_body(context, statement->body, depth,
                               entry_reachable, CTOOL_C_AST_NONE, &frame,
                               &body_falls_through);
  if (status != CTOOL_OK) {
    return status;
  }
  if (body_falls_through == CTOOL_FALSE &&
      frame.has_continue == CTOOL_FALSE) {
    status = cir_validate_unreachable_scalar_condition(context,
                                                       statement->condition);
  } else {
    condition_target = cir_function_offset(context);
    if (frame.has_continue == CTOOL_TRUE) {
      status = cir_patch_tagged_jumps(context, &frame,
                                      CIR_CONTROL_PATCH_CONTINUE,
                                      condition_target);
    }
    if (status == CTOOL_OK) {
      status = cir_lower_scalar_condition_branch(context,
                                                 statement->condition,
                                                 &branch);
    }
    if (status == CTOOL_OK) {
      status = cir_append_instruction(
          context, CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, body_target, 0u, &statement->location,
          &statement->physical_location, (ctool_u32 *)0);
    }
  }
  exit_target = cir_function_offset(context);
  if (status == CTOOL_OK && branch != CTOOL_C_AST_NONE) {
    status = cir_patch_reference(context, branch, exit_target);
  }
  if (status == CTOOL_OK && frame.has_break == CTOOL_TRUE) {
    status = cir_patch_tagged_jumps(context, &frame,
                                    CIR_CONTROL_PATCH_BREAK, exit_target);
  }
  if (status == CTOOL_OK) {
    *falls_through_out =
        frame.has_break == CTOOL_TRUE ||
                body_falls_through == CTOOL_TRUE ||
                frame.has_continue == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
  }
  return status;
}

static ctool_status_t cir_validate_for_statement(
    cir_context_t *context, ctool_u32 statement_index,
    const ctool_c_statement_t *statement) {
  const ctool_c_statement_t *initializer;
  ctool_status_t status =
      cir_validate_loop_shape(context, statement_index, statement);
  if (status != CTOOL_OK) {
    return status;
  }
  if ((statement->condition != CTOOL_C_AST_NONE &&
       statement->condition >= context->unit->expression_count) ||
      (statement->iteration != CTOOL_C_AST_NONE &&
       statement->iteration >= context->unit->expression_count)) {
    return cir_invalid_unit(context, &statement->location);
  }
  if (statement->initializer_statement == CTOOL_C_AST_NONE) {
    return CTOOL_OK;
  }
  if (statement->initializer_statement >= statement_index ||
      statement->initializer_statement >= context->unit->statement_count) {
    return cir_invalid_unit(context, &statement->location);
  }
  initializer =
      &context->unit->statements[statement->initializer_statement];
  if (initializer->kind != CTOOL_C_STATEMENT_EXPRESSION &&
      initializer->kind != CTOOL_C_STATEMENT_DECLARATION) {
    return cir_invalid_unit(context, &statement->location);
  }
  if (initializer->kind == CTOOL_C_STATEMENT_EXPRESSION &&
      initializer->expression == CTOOL_C_AST_NONE) {
    return cir_invalid_unit(context, &initializer->location);
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_for(cir_context_t *context,
                                    ctool_u32 statement_index,
                                    const ctool_c_statement_t *statement,
                                    ctool_u32 depth,
                                    ctool_bool entry_reachable,
                                    ctool_bool *falls_through_out) {
  const ctool_c_statement_t *initializer;
  cir_control_frame_t frame;
  ctool_bool body_falls_through = CTOOL_TRUE;
  ctool_u32 condition_target;
  ctool_u32 exit_branch = CTOOL_C_AST_NONE;
  ctool_u32 exit_target;
  ctool_status_t status =
      cir_validate_for_statement(context, statement_index, statement);
  if (status != CTOOL_OK) {
    return status;
  }
  if (statement->initializer_statement != CTOOL_C_AST_NONE) {
    initializer =
        &context->unit->statements[statement->initializer_statement];
    if (initializer->kind == CTOOL_C_STATEMENT_DECLARATION) {
      status = cir_lower_declaration(context, initializer);
    } else {
      status = cir_lower_expression_statement(context, initializer);
    }
    if (status != CTOOL_OK) {
      return status;
    }
  }
  condition_target = cir_function_offset(context);
  if (statement->condition != CTOOL_C_AST_NONE) {
    status = cir_lower_scalar_condition_branch(context, statement->condition,
                                               &exit_branch);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_loop_body(
        context, statement->body, depth, entry_reachable,
        statement->iteration == CTOOL_C_AST_NONE ? condition_target
                                                 : CTOOL_C_AST_NONE,
        &frame, &body_falls_through);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (body_falls_through == CTOOL_FALSE &&
      frame.has_continue == CTOOL_FALSE) {
    if (statement->iteration != CTOOL_C_AST_NONE) {
      status = cir_validate_unreachable_discarded_expression(
          context, statement->iteration);
    }
  } else {
    if (statement->iteration != CTOOL_C_AST_NONE) {
      const ctool_c_expression_t *iteration =
          &context->unit->expressions[statement->iteration];
      if (frame.has_continue == CTOOL_TRUE) {
        status = cir_patch_tagged_jumps(context, &frame,
                                        CIR_CONTROL_PATCH_CONTINUE,
                                        cir_function_offset(context));
      }
      if (status == CTOOL_OK) {
        status = cir_lower_discarded_expression(
            context, statement->iteration, &iteration->location,
            &iteration->physical_location);
      }
    }
    if (status == CTOOL_OK &&
        (body_falls_through == CTOOL_TRUE ||
         statement->iteration != CTOOL_C_AST_NONE)) {
      status = cir_append_instruction(
          context, CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, condition_target, 0u,
          &statement->location, &statement->physical_location,
          (ctool_u32 *)0);
    }
  }
  exit_target = cir_function_offset(context);
  if (status == CTOOL_OK && exit_branch != CTOOL_C_AST_NONE) {
    status = cir_patch_reference(context, exit_branch, exit_target);
  }
  if (status == CTOOL_OK && frame.has_break == CTOOL_TRUE) {
    status = cir_patch_tagged_jumps(context, &frame,
                                    CIR_CONTROL_PATCH_BREAK, exit_target);
  }
  if (status == CTOOL_OK) {
    if (statement->condition == CTOOL_C_AST_NONE) {
      *falls_through_out = frame.has_break;
    } else {
      *falls_through_out =
          entry_reachable == CTOOL_TRUE || frame.has_break == CTOOL_TRUE ||
                  body_falls_through == CTOOL_TRUE ||
                  frame.has_continue == CTOOL_TRUE
              ? CTOOL_TRUE
              : CTOOL_FALSE;
    }
  }
  return status;
}

static ctool_status_t cir_enter_switch(cir_context_t *context,
                                       ctool_u32 condition_type,
                                       ctool_bool entry_reachable) {
  cir_control_frame_t *frame;
  if (context->control_depth >= CTOOL_C_PARSE_NESTING_LIMIT) {
    return cir_emit_failure(
        context, CTOOL_ERR_LIMIT, CTOOL_C_IR_DIAG_LIMIT,
        (const ctool_c_pp_location_t *)0,
        "CupidC IR lowering exceeded a configured resource limit");
  }
  frame = &context->control_frames[context->control_depth++];
  frame->kind = CIR_CONTROL_SWITCH;
  frame->first_instruction = context->instruction_count;
  frame->continue_target = CTOOL_C_AST_NONE;
  frame->condition_type = condition_type;
  frame->has_break = CTOOL_FALSE;
  frame->has_continue = CTOOL_FALSE;
  frame->switch_entry_reachable = entry_reachable;
  return CTOOL_OK;
}

static cir_control_frame_t cir_leave_switch(cir_context_t *context) {
  context->control_depth--;
  return context->control_frames[context->control_depth];
}

static ctool_status_t cir_duplicate_value(
    cir_context_t *context, ctool_u32 type,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location) {
  ctool_status_t status;
  if (context->stack_depth == 0u ||
      context->stack[context->stack_depth - 1u].kind != CIR_STACK_VALUE ||
      context->stack[context->stack_depth - 1u].type != type ||
      (cir_type_is_i32_integer(context, type) == CTOOL_FALSE &&
       cir_type_is_wide_integer(context, type) == CTOOL_FALSE)) {
    return cir_invalid_unit(context, location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_DUPLICATE_VALUE, type, type,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
      CTOOL_C_AST_NONE, 0u, location, physical_location,
      (ctool_u32 *)0);
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, type);
}

static ctool_status_t cir_emit_switch_case_test(
    cir_context_t *context, ctool_u32 statement_index,
    const ctool_c_statement_t *statement, ctool_u32 condition_type) {
  const ctool_c_expression_t *case_expression;
  cir_stack_entry_t case_value;
  cir_stack_entry_t comparison_left;
  cir_stack_entry_t comparison_result;
  cir_stack_entry_t switch_value;
  ctool_u32 comparison_type = cir_plain_signed_int_type(context);
  ctool_u32 base_depth = context->stack_depth;
  ctool_u32 next_case_branch;
  ctool_u64 patch = CIR_SWITCH_CASE_PATCH_TAG | (ctool_u64)statement_index;
  ctool_status_t status;
  if (statement->expression >= context->unit->expression_count ||
      comparison_type == CTOOL_C_TYPE_NONE) {
    return cir_invalid_unit(context, &statement->location);
  }
  case_expression = &context->unit->expressions[statement->expression];
  if (case_expression->kind != CTOOL_C_EXPRESSION_INTEGER_CONSTANT ||
      case_expression->type != condition_type || base_depth == 0u) {
    return cir_invalid_unit(context, &statement->location);
  }
  switch_value = context->stack[base_depth - 1u];
  status = cir_duplicate_value(context, condition_type,
                               &case_expression->location,
                               &case_expression->physical_location);
  if (status == CTOOL_OK) {
    status = cir_lower_expression(context, statement->expression, 0u);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &case_value);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &comparison_left);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (context->stack_depth != base_depth ||
      comparison_left.kind != CIR_STACK_VALUE ||
      case_value.kind != CIR_STACK_VALUE ||
      comparison_left.type != condition_type ||
      case_value.type != condition_type) {
    return cir_invalid_unit(context, &statement->location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_BINARY, comparison_type,
      condition_type, CTOOL_C_EXPRESSION_OPERATOR_EQUAL,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &case_expression->location, &case_expression->physical_location,
      (ctool_u32 *)0);
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE, comparison_type);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &comparison_result);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (comparison_result.kind != CIR_STACK_VALUE ||
      comparison_result.type != comparison_type ||
      context->stack_depth != base_depth) {
    return cir_invalid_unit(context, &statement->location);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_C_TYPE_NONE,
      comparison_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
      &case_expression->location, &case_expression->physical_location,
      &next_case_branch);
  if (status == CTOOL_OK) {
    status = cir_pop(context, &switch_value);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_DISCARD, CTOOL_C_TYPE_NONE,
        condition_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &statement->location, &statement->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, patch,
        &statement->location, &statement->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, next_case_branch,
                                 cir_function_offset(context));
  }
  if (status == CTOOL_OK) {
    context->stack[base_depth - 1u] = switch_value;
    context->stack_depth = base_depth;
  }
  return status;
}

static ctool_status_t cir_emit_switch_dispatch(
    cir_context_t *context, ctool_u32 statement_index, ctool_u32 depth,
    ctool_u32 condition_type, ctool_u32 *default_statement) {
  const ctool_c_statement_t *statement;
  ctool_u32 child_offset;
  ctool_status_t status;
  if (depth >= CTOOL_C_PARSE_NESTING_LIMIT) {
    return cir_emit_failure(
        context, CTOOL_ERR_LIMIT, CTOOL_C_IR_DIAG_LIMIT,
        (const ctool_c_pp_location_t *)0,
        "CupidC IR lowering exceeded a configured resource limit");
  }
  if (statement_index >= context->unit->statement_count) {
    return cir_invalid_unit(context, (const ctool_c_pp_location_t *)0);
  }
  statement = &context->unit->statements[statement_index];
  if (statement->kind == CTOOL_C_STATEMENT_SWITCH) {
    return CTOOL_OK;
  }
  if (statement->kind == CTOOL_C_STATEMENT_CASE ||
      statement->kind == CTOOL_C_STATEMENT_DEFAULT) {
    if (statement->first_child != CTOOL_C_AST_NONE ||
        statement->child_count != 0u ||
        statement->first_block_binding != CTOOL_C_AST_NONE ||
        statement->block_binding_count != 0u ||
        statement->label != CTOOL_C_AST_NONE ||
        statement->initializer_statement != CTOOL_C_AST_NONE ||
        statement->condition != CTOOL_C_AST_NONE ||
        statement->iteration != CTOOL_C_AST_NONE ||
        statement->else_body != CTOOL_C_AST_NONE ||
        statement->body >= statement_index ||
        statement->body >= context->unit->statement_count ||
        context->unit->statements[statement->body].kind ==
            CTOOL_C_STATEMENT_DECLARATION ||
        (statement->kind == CTOOL_C_STATEMENT_CASE &&
         statement->expression == CTOOL_C_AST_NONE) ||
        (statement->kind == CTOOL_C_STATEMENT_DEFAULT &&
         statement->expression != CTOOL_C_AST_NONE)) {
      return cir_invalid_unit(context, &statement->location);
    }
    if (statement->kind == CTOOL_C_STATEMENT_CASE) {
      status = cir_emit_switch_case_test(context, statement_index, statement,
                                         condition_type);
    } else {
      if (*default_statement != CTOOL_C_AST_NONE) {
        return cir_invalid_unit(context, &statement->location);
      }
      *default_statement = statement_index;
      status = CTOOL_OK;
    }
    if (status != CTOOL_OK) {
      return status;
    }
    return cir_emit_switch_dispatch(context, statement->body, depth + 1u,
                                    condition_type, default_statement);
  }
  if (statement->kind == CTOOL_C_STATEMENT_COMPOUND) {
    if (statement->first_child > context->unit->statement_child_count ||
        statement->child_count >
            context->unit->statement_child_count - statement->first_child) {
      return cir_invalid_unit(context, &statement->location);
    }
    for (child_offset = 0u; child_offset < statement->child_count;
         child_offset++) {
      ctool_u32 child = context->unit->statement_children
          [statement->first_child + child_offset];
      if (child >= statement_index ||
          child >= context->unit->statement_count) {
        return cir_invalid_unit(context, &statement->location);
      }
      status = cir_emit_switch_dispatch(context, child, depth + 1u,
                                        condition_type, default_statement);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    return CTOOL_OK;
  }
  if (statement->kind == CTOOL_C_STATEMENT_IF) {
    status = cir_emit_switch_dispatch(context, statement->body, depth + 1u,
                                      condition_type, default_statement);
    if (status != CTOOL_OK || statement->else_body == CTOOL_C_AST_NONE) {
      return status;
    }
    return cir_emit_switch_dispatch(context, statement->else_body,
                                    depth + 1u, condition_type,
                                    default_statement);
  }
  if (statement->kind == CTOOL_C_STATEMENT_WHILE ||
      statement->kind == CTOOL_C_STATEMENT_DO ||
      statement->kind == CTOOL_C_STATEMENT_FOR ||
      statement->kind == CTOOL_C_STATEMENT_LABEL) {
    return cir_emit_switch_dispatch(context, statement->body, depth + 1u,
                                    condition_type, default_statement);
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_switch_label(
    cir_context_t *context, ctool_u32 statement_index,
    const ctool_c_statement_t *statement, ctool_u32 depth,
    ctool_bool entry_reachable,
    ctool_bool *falls_through_out) {
  cir_control_frame_t *frame = (cir_control_frame_t *)0;
  ctool_u32 frame_index;
  ctool_u64 patch;
  ctool_status_t status;
  for (frame_index = context->control_depth; frame_index != 0u;
       frame_index--) {
    cir_control_frame_t *candidate =
        &context->control_frames[frame_index - 1u];
    if (candidate->kind == CIR_CONTROL_SWITCH) {
      frame = candidate;
      break;
    }
  }
  if (frame == (cir_control_frame_t *)0) {
    return cir_invalid_unit(context, &statement->location);
  }
  if (statement->kind == CTOOL_C_STATEMENT_CASE) {
    if (statement->expression >= context->unit->expression_count ||
        context->unit->expressions[statement->expression].type !=
            frame->condition_type) {
      return cir_invalid_unit(context, &statement->location);
    }
  } else if (statement->kind == CTOOL_C_STATEMENT_DEFAULT) {
    if (statement->expression != CTOOL_C_AST_NONE) {
      return cir_invalid_unit(context, &statement->location);
    }
  } else {
    return cir_invalid_unit(context, &statement->location);
  }
  patch = CIR_SWITCH_CASE_PATCH_TAG | (ctool_u64)statement_index;
  status = cir_patch_tagged_jumps(context, frame, patch,
                                  cir_function_offset(context));
  if (status != CTOOL_OK) {
    return status;
  }
  return cir_lower_statement_from_entry(
      context, statement->body, depth + 1u,
      entry_reachable == CTOOL_TRUE ||
              frame->switch_entry_reachable == CTOOL_TRUE
          ? CTOOL_TRUE
          : CTOOL_FALSE,
      falls_through_out);
}

static ctool_status_t cir_lower_switch(cir_context_t *context,
                                       ctool_u32 statement_index,
                                       const ctool_c_statement_t *statement,
                                       ctool_u32 depth,
                                       ctool_bool entry_reachable,
                                       ctool_bool *falls_through_out) {
  const ctool_c_expression_t *condition_expression;
  cir_stack_entry_t condition;
  cir_control_frame_t frame;
  ctool_bool body_falls_through = CTOOL_TRUE;
  ctool_bool entered_switch = CTOOL_FALSE;
  ctool_u32 base_depth = context->stack_depth;
  ctool_u32 default_statement = CTOOL_C_AST_NONE;
  ctool_u32 exit_target;
  ctool_u64 final_patch;
  ctool_status_t status;
  if (statement->first_child != CTOOL_C_AST_NONE ||
      statement->child_count != 0u ||
      statement->expression != CTOOL_C_AST_NONE ||
      statement->first_block_binding != CTOOL_C_AST_NONE ||
      statement->block_binding_count != 0u ||
      statement->label != CTOOL_C_AST_NONE ||
      statement->initializer_statement != CTOOL_C_AST_NONE ||
      statement->condition == CTOOL_C_AST_NONE ||
      statement->condition >= context->unit->expression_count ||
      statement->iteration != CTOOL_C_AST_NONE ||
      statement->body >= statement_index ||
      statement->body >= context->unit->statement_count ||
      context->unit->statements[statement->body].kind ==
          CTOOL_C_STATEMENT_DECLARATION ||
      statement->else_body != CTOOL_C_AST_NONE) {
    return cir_invalid_unit(context, &statement->location);
  }
  condition_expression = &context->unit->expressions[statement->condition];
  status = cir_lower_expression(context, statement->condition, 0u);
  if (status == CTOOL_OK && context->stack_depth != base_depth + 1u) {
    status = cir_invalid_unit(context, &condition_expression->location);
  }
  if (status == CTOOL_OK) {
    condition = context->stack[base_depth];
    if (condition.kind != CIR_STACK_VALUE ||
        (cir_type_is_i32_integer(context, condition.type) == CTOOL_FALSE &&
         cir_type_is_wide_integer(context, condition.type) == CTOOL_FALSE)) {
      status = cir_unsupported_type(context, &condition_expression->location);
    }
  }
  if (status == CTOOL_OK) {
    status = cir_enter_switch(context, condition.type, entry_reachable);
    if (status == CTOOL_OK) {
      entered_switch = CTOOL_TRUE;
    }
  }
  if (status == CTOOL_OK) {
    if (entry_reachable == CTOOL_TRUE) {
      status = cir_emit_switch_dispatch(context, statement->body, depth + 1u,
                                        condition.type, &default_statement);
    } else {
      cir_context_t validation;
      ctool_arena_mark_t mark = ctool_arena_mark(context->arena);
      cir_prepare_count_only_validation(&validation, context);
      status = cir_emit_switch_dispatch(
          &validation, statement->body, depth + 1u, condition.type,
          &default_statement);
      status = cir_finish_count_only_validation(context, &validation, mark,
                                                status);
    }
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &condition);
  }
  if (status == CTOOL_OK) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_DISCARD, CTOOL_C_TYPE_NONE,
        condition.type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u,
        &statement->location, &statement->physical_location,
        (ctool_u32 *)0);
  }
  final_patch = default_statement == CTOOL_C_AST_NONE
                    ? CIR_SWITCH_EXIT_PATCH
                    : CIR_SWITCH_CASE_PATCH_TAG |
                          (ctool_u64)default_statement;
  if (status == CTOOL_OK && entry_reachable == CTOOL_TRUE) {
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, final_patch,
        &statement->location, &statement->physical_location,
        (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_statement_from_entry(
        context, statement->body, depth + 1u, CTOOL_FALSE,
        &body_falls_through);
  }
  if (entered_switch == CTOOL_FALSE) {
    return status;
  }
  frame = cir_leave_switch(context);
  if (status != CTOOL_OK) {
    return status;
  }
  exit_target = cir_function_offset(context);
  if (entry_reachable == CTOOL_TRUE &&
      default_statement == CTOOL_C_AST_NONE) {
    status = cir_patch_tagged_jumps(context, &frame,
                                    CIR_SWITCH_EXIT_PATCH, exit_target);
  }
  if (status == CTOOL_OK && frame.has_break == CTOOL_TRUE) {
    status = cir_patch_tagged_jumps(context, &frame,
                                    CIR_CONTROL_PATCH_BREAK, exit_target);
  }
  if (status == CTOOL_OK) {
    *falls_through_out =
        (entry_reachable == CTOOL_TRUE &&
         default_statement == CTOOL_C_AST_NONE) ||
                frame.has_break == CTOOL_TRUE ||
                body_falls_through == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
  }
  return status;
}

static ctool_status_t cir_lower_statement(cir_context_t *context,
                                          ctool_u32 statement_index,
                                          ctool_u32 depth,
                                          ctool_bool entry_reachable,
                                          ctool_bool *falls_through_out) {
  const ctool_c_statement_t *statement;
  ctool_status_t status;
  *falls_through_out = CTOOL_TRUE;
  if (depth >= CTOOL_C_PARSE_NESTING_LIMIT) {
    return cir_emit_failure(
        context, CTOOL_ERR_LIMIT, CTOOL_C_IR_DIAG_LIMIT,
        (const ctool_c_pp_location_t *)0,
        "CupidC IR lowering exceeded a configured resource limit");
  }
  if (statement_index >= context->unit->statement_count) {
    return cir_invalid_unit(context, (const ctool_c_pp_location_t *)0);
  }
  statement = &context->unit->statements[statement_index];
  if (statement->kind == CTOOL_C_STATEMENT_RETURN) {
    status = cir_lower_return(context, statement);
    if (status == CTOOL_OK) {
      *falls_through_out = CTOOL_FALSE;
    }
    return status;
  }
  if (statement->kind == CTOOL_C_STATEMENT_EXPRESSION) {
    return cir_lower_expression_statement(context, statement);
  }
  if (statement->kind == CTOOL_C_STATEMENT_DECLARATION) {
    return cir_lower_declaration(context, statement);
  }
  if (statement->kind == CTOOL_C_STATEMENT_GOTO) {
    status = cir_lower_goto(context, statement);
    if (status == CTOOL_OK) {
      *falls_through_out = CTOOL_FALSE;
    }
    return status;
  }
  if (statement->kind == CTOOL_C_STATEMENT_LABEL) {
    return cir_lower_label(context, statement_index, statement, depth,
                           entry_reachable, falls_through_out);
  }
  if (statement->kind == CTOOL_C_STATEMENT_COMPOUND) {
    return cir_lower_compound(context, statement_index, statement, depth,
                              entry_reachable, falls_through_out);
  }
  if (statement->kind == CTOOL_C_STATEMENT_IF) {
    return cir_lower_if(context, statement_index, statement, depth,
                        entry_reachable, falls_through_out);
  }
  if (statement->kind == CTOOL_C_STATEMENT_CASE ||
      statement->kind == CTOOL_C_STATEMENT_DEFAULT) {
    return cir_lower_switch_label(context, statement_index, statement,
                                  depth, entry_reachable,
                                  falls_through_out);
  }
  if (statement->kind == CTOOL_C_STATEMENT_BREAK ||
      statement->kind == CTOOL_C_STATEMENT_CONTINUE) {
    status = cir_lower_control_jump(context, statement);
    if (status == CTOOL_OK) {
      *falls_through_out = CTOOL_FALSE;
    }
    return status;
  }
  if (statement->kind == CTOOL_C_STATEMENT_WHILE) {
    return cir_lower_while(context, statement_index, statement, depth,
                           entry_reachable, falls_through_out);
  }
  if (statement->kind == CTOOL_C_STATEMENT_DO) {
    return cir_lower_do(context, statement_index, statement, depth,
                        entry_reachable, falls_through_out);
  }
  if (statement->kind == CTOOL_C_STATEMENT_FOR) {
    return cir_lower_for(context, statement_index, statement, depth,
                         entry_reachable, falls_through_out);
  }
  if (statement->kind == CTOOL_C_STATEMENT_SWITCH) {
    return cir_lower_switch(context, statement_index, statement, depth,
                            entry_reachable, falls_through_out);
  }
  return cir_unsupported_statement(context, &statement->location);
}

static ctool_status_t cir_scan_expression_bindings(
    cir_context_t *context, ctool_u32 expression_index, ctool_u32 depth,
    ctool_u32 *binding_cursor);

static ctool_status_t cir_scan_initializer_bindings(
    cir_context_t *context, ctool_u32 initializer_index, ctool_u32 depth,
    ctool_u32 *binding_cursor) {
  const ctool_c_initializer_t *initializer;
  ctool_u32 child_offset;
  if (depth >= CTOOL_C_PARSE_NESTING_LIMIT) {
    return cir_emit_failure(
        context, CTOOL_ERR_LIMIT, CTOOL_C_IR_DIAG_LIMIT,
        (const ctool_c_pp_location_t *)0,
        "CupidC IR lowering exceeded a configured resource limit");
  }
  if (initializer_index >= context->unit->initializer_count) {
    return cir_invalid_unit(context, (const ctool_c_pp_location_t *)0);
  }
  initializer = &context->unit->initializers[initializer_index];
  if ((initializer->first_block_binding == CTOOL_C_AST_NONE &&
       initializer->block_binding_count != 0u) ||
      (initializer->first_block_binding != CTOOL_C_AST_NONE &&
       (initializer->block_binding_count == 0u ||
        initializer->first_block_binding >
            context->unit->block_binding_count ||
        initializer->block_binding_count >
            context->unit->block_binding_count -
                initializer->first_block_binding))) {
    return cir_invalid_unit(context, &initializer->location);
  }
  if (initializer->first_block_binding != CTOOL_C_AST_NONE) {
    ctool_u32 binding_offset;
    if (initializer->first_block_binding != *binding_cursor) {
      return cir_invalid_unit(context, &initializer->location);
    }
    for (binding_offset = 0u;
         binding_offset < initializer->block_binding_count;
         binding_offset++) {
      ctool_status_t status = cir_validate_block_enumerator(
          context, initializer->first_block_binding + binding_offset,
          CTOOL_C_AST_NONE, initializer_index, &initializer->location);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    *binding_cursor += initializer->block_binding_count;
  }
  if (initializer->kind == CTOOL_C_INITIALIZER_EXPRESSION) {
    return cir_scan_expression_bindings(
        context, initializer->expression, depth + 1u, binding_cursor);
  }
  if (initializer->kind != CTOOL_C_INITIALIZER_LIST) {
    return CTOOL_OK;
  }
  if (initializer->first_element >
          context->unit->initializer_element_count ||
      initializer->element_count >
          context->unit->initializer_element_count -
              initializer->first_element) {
    return cir_invalid_unit(context, &initializer->location);
  }
  for (child_offset = 0u; child_offset < initializer->element_count;
       child_offset++) {
    ctool_u32 child = context->unit->initializer_elements
                          [initializer->first_element + child_offset]
                              .initializer;
    ctool_status_t status = cir_scan_initializer_bindings(
        context, child, depth + 1u, binding_cursor);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cir_scan_expression_bindings(
    cir_context_t *context, ctool_u32 expression_index, ctool_u32 depth,
    ctool_u32 *binding_cursor) {
  const ctool_c_expression_t *expression;
  ctool_u32 child_offset;
  if (depth >= CTOOL_C_PARSE_NESTING_LIMIT) {
    return cir_emit_failure(
        context, CTOOL_ERR_LIMIT, CTOOL_C_IR_DIAG_LIMIT,
        (const ctool_c_pp_location_t *)0,
        "CupidC IR lowering exceeded a configured resource limit");
  }
  if (expression_index >= context->unit->expression_count) {
    return cir_invalid_unit(context, (const ctool_c_pp_location_t *)0);
  }
  expression = &context->unit->expressions[expression_index];
  if ((expression->first_block_binding == CTOOL_C_AST_NONE &&
       (expression->block_binding_count != 0u ||
        expression->block_binding_child_offset != 0u)) ||
      (expression->first_block_binding != CTOOL_C_AST_NONE &&
       (expression->block_binding_count == 0u ||
        expression->block_binding_child_offset > expression->child_count ||
        expression->first_block_binding >
            context->unit->block_binding_count ||
        expression->block_binding_count >
            context->unit->block_binding_count -
                expression->first_block_binding))) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (expression->child_count != 0u &&
      (expression->first_child > context->unit->expression_child_count ||
       expression->child_count >
           context->unit->expression_child_count - expression->first_child)) {
    return cir_invalid_unit(context, &expression->location);
  }
  for (child_offset = 0u; child_offset <= expression->child_count;
       child_offset++) {
    if (expression->first_block_binding != CTOOL_C_AST_NONE &&
        expression->block_binding_child_offset == child_offset) {
      ctool_u32 binding_offset;
      if (expression->first_block_binding != *binding_cursor) {
        return cir_invalid_unit(context, &expression->location);
      }
      for (binding_offset = 0u;
           binding_offset < expression->block_binding_count;
           binding_offset++) {
        ctool_status_t status = cir_validate_block_enumerator(
            context, expression->first_block_binding + binding_offset,
            expression_index, CTOOL_C_AST_NONE, &expression->location);
        if (status != CTOOL_OK) {
          return status;
        }
      }
      *binding_cursor += expression->block_binding_count;
    }
    if (child_offset < expression->child_count) {
      ctool_u32 child = context->unit->expression_children
          [expression->first_child + child_offset];
      ctool_status_t status;
      if (child >= expression_index ||
          child >= context->unit->expression_count) {
        return cir_invalid_unit(context, &expression->location);
      }
      status = cir_scan_expression_bindings(
          context, child, depth + 1u, binding_cursor);
      if (status != CTOOL_OK) {
        return status;
      }
    }
  }
  if (expression->kind == CTOOL_C_EXPRESSION_BLOCK_BINDING &&
      (expression->reference < context->function_first_block_binding ||
       expression->reference >= *binding_cursor)) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_COMPOUND_LITERAL) {
    return cir_scan_initializer_bindings(
        context, expression->reference, depth + 1u, binding_cursor);
  }
  return CTOOL_OK;
}

static ctool_status_t cir_scan_declaration_bindings(
    cir_context_t *context, ctool_u32 statement_index, ctool_u32 depth,
    ctool_u32 *binding_cursor) {
  const ctool_c_statement_t *statement;
  ctool_u32 child_offset;
  ctool_status_t status;
  if (depth >= CTOOL_C_PARSE_NESTING_LIMIT) {
    return cir_emit_failure(
        context, CTOOL_ERR_LIMIT, CTOOL_C_IR_DIAG_LIMIT,
        (const ctool_c_pp_location_t *)0,
        "CupidC IR lowering exceeded a configured resource limit");
  }
  if (statement_index >= context->unit->statement_count) {
    return cir_invalid_unit(context, (const ctool_c_pp_location_t *)0);
  }
  statement = &context->unit->statements[statement_index];
  if (statement->kind == CTOOL_C_STATEMENT_DECLARATION) {
    ctool_u32 binding_end;
    if (statement->first_block_binding == CTOOL_C_AST_NONE ||
        statement->first_block_binding != *binding_cursor ||
        statement->first_block_binding > context->unit->block_binding_count ||
        statement->block_binding_count > context->unit->block_binding_count -
                                             statement->first_block_binding ||
        cir_add_overflows(statement->first_block_binding,
                          statement->block_binding_count) == CTOOL_TRUE) {
      return cir_invalid_unit(context, &statement->location);
    }
    binding_end = statement->first_block_binding +
                  statement->block_binding_count;
    while (*binding_cursor < binding_end) {
      const ctool_c_block_binding_t *binding =
          &context->unit->block_bindings[*binding_cursor];
      if (binding->activation_expression != CTOOL_C_AST_NONE ||
          binding->activation_initializer != CTOOL_C_AST_NONE) {
        return cir_invalid_unit(context, &binding->location);
      }
      if (binding->kind == CTOOL_C_BINDING_ENUMERATOR) {
        status = cir_validate_block_enumerator(
            context, *binding_cursor, CTOOL_C_AST_NONE, CTOOL_C_AST_NONE,
            &statement->location);
        if (status != CTOOL_OK) {
          return status;
        }
      }
      (*binding_cursor)++;
      if (binding->kind == CTOOL_C_BINDING_OBJECT &&
          binding->initializer != CTOOL_C_AST_NONE) {
        status = cir_scan_initializer_bindings(
            context, binding->initializer, depth + 1u, binding_cursor);
        if (status != CTOOL_OK || *binding_cursor > binding_end) {
          return status != CTOOL_OK
                     ? status
                     : cir_invalid_unit(context, &statement->location);
        }
      }
    }
    return *binding_cursor == binding_end
               ? CTOOL_OK
               : cir_invalid_unit(context, &statement->location);
  }
  if (statement->kind == CTOOL_C_STATEMENT_EXPRESSION ||
      statement->kind == CTOOL_C_STATEMENT_RETURN) {
    return statement->expression == CTOOL_C_AST_NONE
               ? CTOOL_OK
               : cir_scan_expression_bindings(
                     context, statement->expression, depth + 1u,
                     binding_cursor);
  }
  if (statement->kind == CTOOL_C_STATEMENT_COMPOUND) {
    if (statement->first_child > context->unit->statement_child_count ||
        statement->child_count >
            context->unit->statement_child_count - statement->first_child) {
      return cir_invalid_unit(context, &statement->location);
    }
    for (child_offset = 0u; child_offset < statement->child_count;
         child_offset++) {
      ctool_u32 child = context->unit->statement_children
          [statement->first_child + child_offset];
      if (child >= statement_index || child >= context->unit->statement_count) {
        return cir_invalid_unit(context, &statement->location);
      }
      status = cir_scan_declaration_bindings(context, child, depth + 1u,
                                             binding_cursor);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    return CTOOL_OK;
  }
  if (statement->kind == CTOOL_C_STATEMENT_FOR) {
    if (statement->initializer_statement != CTOOL_C_AST_NONE) {
      if (statement->initializer_statement >= statement_index ||
          statement->initializer_statement >= context->unit->statement_count) {
        return cir_invalid_unit(context, &statement->location);
      }
      status = cir_scan_declaration_bindings(
          context, statement->initializer_statement, depth + 1u,
          binding_cursor);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    if (statement->condition != CTOOL_C_AST_NONE) {
      status = cir_scan_expression_bindings(
          context, statement->condition, depth + 1u, binding_cursor);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    if (statement->iteration != CTOOL_C_AST_NONE) {
      status = cir_scan_expression_bindings(
          context, statement->iteration, depth + 1u, binding_cursor);
      if (status != CTOOL_OK) {
        return status;
      }
    }
  } else if (statement->kind == CTOOL_C_STATEMENT_IF ||
             statement->kind == CTOOL_C_STATEMENT_WHILE ||
             statement->kind == CTOOL_C_STATEMENT_SWITCH) {
    status = cir_scan_expression_bindings(
        context, statement->condition, depth + 1u, binding_cursor);
    if (status != CTOOL_OK) {
      return status;
    }
  } else if (statement->kind == CTOOL_C_STATEMENT_CASE) {
    status = cir_scan_expression_bindings(
        context, statement->expression, depth + 1u, binding_cursor);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if (statement->kind == CTOOL_C_STATEMENT_DO) {
    if (statement->body >= statement_index ||
        statement->body >= context->unit->statement_count) {
      return cir_invalid_unit(context, &statement->location);
    }
    status = cir_scan_declaration_bindings(
        context, statement->body, depth + 1u, binding_cursor);
    if (status != CTOOL_OK) {
      return status;
    }
    return cir_scan_expression_bindings(
        context, statement->condition, depth + 1u, binding_cursor);
  }
  if (statement->kind == CTOOL_C_STATEMENT_FOR ||
      statement->kind == CTOOL_C_STATEMENT_IF ||
      statement->kind == CTOOL_C_STATEMENT_WHILE ||
      statement->kind == CTOOL_C_STATEMENT_SWITCH ||
      statement->kind == CTOOL_C_STATEMENT_CASE ||
      statement->kind == CTOOL_C_STATEMENT_DEFAULT ||
      statement->kind == CTOOL_C_STATEMENT_LABEL) {
    if (statement->body >= statement_index ||
        statement->body >= context->unit->statement_count) {
      return cir_invalid_unit(context, &statement->location);
    }
    status = cir_scan_declaration_bindings(
        context, statement->body, depth + 1u, binding_cursor);
    if (status != CTOOL_OK) {
      return status;
    }
    if (statement->kind == CTOOL_C_STATEMENT_IF &&
        statement->else_body != CTOOL_C_AST_NONE) {
      if (statement->else_body >= statement_index ||
          statement->else_body >= context->unit->statement_count) {
        return cir_invalid_unit(context, &statement->location);
      }
      return cir_scan_declaration_bindings(
          context, statement->else_body, depth + 1u, binding_cursor);
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cir_prepare_function_block_bindings(
    cir_context_t *context,
    const ctool_c_function_definition_t *definition,
    const ctool_c_statement_t *body) {
  ctool_u32 binding_cursor = context->block_binding_cursor;
  ctool_u32 binding_offset;
  ctool_u32 child_offset;
  ctool_status_t status;
  context->function_first_block_binding = binding_cursor;
  context->function_block_binding_count = 0u;
  context->visible_block_binding_end = binding_cursor;
  if (definition->first_block_binding != binding_cursor ||
      definition->first_block_binding > context->unit->block_binding_count ||
      definition->block_binding_count >
          context->unit->block_binding_count -
              definition->first_block_binding) {
    return cir_invalid_unit(context, &definition->location);
  }
  for (binding_offset = 0u;
       binding_offset < definition->block_binding_count; binding_offset++) {
    status = cir_validate_block_enumerator(
        context, definition->first_block_binding + binding_offset,
        CTOOL_C_AST_NONE, CTOOL_C_AST_NONE,
        &definition->location);
    if (status != CTOOL_OK) {
      return status;
    }
    binding_cursor++;
  }
  for (child_offset = 0u; child_offset < body->child_count;
       child_offset++) {
    ctool_u32 child = context->unit->statement_children
        [body->first_child + child_offset];
    status = cir_scan_declaration_bindings(context, child, 0u,
                                           &binding_cursor);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  context->function_block_binding_count =
      binding_cursor - context->function_first_block_binding;
  context->block_binding_cursor = binding_cursor;
  context->visible_block_binding_end = binding_cursor;
  return CTOOL_OK;
}

static ctool_status_t cir_append_unreachable_exit(
    cir_context_t *context, const ctool_c_statement_t *body) {
  cir_stack_entry_t result;
  ctool_status_t status;
  if (cir_type_is_void(context, context->function_result_type) == CTOOL_TRUE) {
    return cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_RETURN_VOID, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, &body->location,
        &body->physical_location, (ctool_u32 *)0);
  }
  if (cir_type_is_floating_value(context,
                                 context->function_result_type) ==
      CTOOL_TRUE) {
    return cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_RETURN_VOID, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, &body->location,
        &body->physical_location, (ctool_u32 *)0);
  }
  if (cir_type_is_value_scalar(context,
                              context->function_result_type) ==
      CTOOL_FALSE) {
    status = cir_require_structure_value(
        context, context->function_result_type, &body->location);
    if (status != CTOOL_OK) {
      return status;
    }
    return cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_RETURN_VOID, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, &body->location,
        &body->physical_location, (ctool_u32 *)0);
  }
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_INTEGER, context->function_result_type,
      CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, &body->location,
      &body->physical_location, (ctool_u32 *)0);
  if (status == CTOOL_OK) {
    status = cir_push(context, CIR_STACK_VALUE,
                      context->function_result_type);
  }
  if (status == CTOOL_OK) {
    status = cir_pop(context, &result);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (result.kind != CIR_STACK_VALUE ||
      result.type != context->function_result_type) {
    return CTOOL_ERR_INTERNAL;
  }
  return cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      context->function_result_type, result.type,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
      CTOOL_C_AST_NONE, 0u, &body->location, &body->physical_location,
      (ctool_u32 *)0);
}

static ctool_status_t cir_lower_body(
    cir_context_t *context, const ctool_c_function_definition_t *definition) {
  const ctool_c_statement_t *body;
  ctool_bool falls_through = CTOOL_TRUE;
  ctool_u32 child_offset;
  ctool_u32 statement_index;
  ctool_status_t status;
  if (definition->body >= context->unit->statement_count) {
    return cir_invalid_unit(context, &definition->location);
  }
  body = &context->unit->statements[definition->body];
  if (body->kind != CTOOL_C_STATEMENT_COMPOUND ||
      body->first_child > context->unit->statement_child_count ||
      body->child_count >
          context->unit->statement_child_count - body->first_child) {
    return cir_invalid_unit(context, &body->location);
  }
  status = cir_prepare_function_block_bindings(context, definition, body);
  if (status != CTOOL_OK) {
    return status;
  }
  if (body->child_count == 0u) {
    if (cir_type_is_void(context, context->function_result_type) ==
        CTOOL_FALSE) {
      return cir_unsupported_statement(context, &body->location);
    }
    return cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_RETURN_VOID, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, &body->location,
        &body->physical_location, (ctool_u32 *)0);
  }
  for (child_offset = 0u; child_offset < body->child_count; child_offset++) {
    ctool_bool child_falls_through = CTOOL_TRUE;
    statement_index =
        context->unit->statement_children[body->first_child + child_offset];
    if (statement_index >= definition->body ||
        statement_index >= context->unit->statement_count) {
      return cir_invalid_unit(context, &body->location);
    }
    if (falls_through == CTOOL_FALSE &&
        cir_statement_contains_reachable_label(
            context, statement_index, 0u) == CTOOL_FALSE) {
      status = cir_validate_unreachable_statement(context, statement_index, 0u);
    } else {
      status = cir_lower_statement(context, statement_index, 0u,
                                   falls_through,
                                   &child_falls_through);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (falls_through == CTOOL_TRUE ||
        cir_statement_contains_reachable_label(
            context, statement_index, 0u) == CTOOL_TRUE) {
      falls_through = child_falls_through;
    }
  }
  if (cir_add_overflows(context->function_first_block_binding,
                        context->function_block_binding_count) == CTOOL_TRUE ||
      context->block_binding_cursor !=
          context->function_first_block_binding +
              context->function_block_binding_count) {
    return cir_invalid_unit(context, &body->location);
  }
  falls_through = context->function_reachable_fallthrough;
  if (falls_through == CTOOL_FALSE) {
    if (context->function_has_patched_target == CTOOL_TRUE &&
        context->function_maximum_patched_target ==
            cir_function_offset(context)) {
      return cir_append_unreachable_exit(context, body);
    }
    return CTOOL_OK;
  }
  if (cir_type_is_void(context, context->function_result_type) == CTOOL_FALSE) {
    return cir_unsupported_statement(context, &body->location);
  }
  return cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_RETURN_VOID, CTOOL_C_TYPE_NONE,
      CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, &body->location,
      &body->physical_location, (ctool_u32 *)0);
}

static ctool_status_t cir_prepare_function_labels(
    cir_context_t *context,
    const ctool_c_function_definition_t *definition) {
  ctool_u32 label_index;
  ctool_u32 statement_index;
  if (definition->first_label != context->label_cursor ||
      definition->first_label > context->unit->label_count ||
      definition->label_count >
          context->unit->label_count - definition->first_label ||
      definition->body < context->statement_cursor ||
      definition->body >= context->unit->statement_count) {
    return cir_invalid_unit(context, &definition->location);
  }
  context->function_first_label = definition->first_label;
  context->function_label_count = definition->label_count;
  for (label_index = definition->first_label;
       label_index < definition->first_label + definition->label_count;
       label_index++) {
    const ctool_c_label_t *label = &context->unit->labels[label_index];
    if (label->name.size == 0u || label->name.data == (const char *)0 ||
        label->statement < context->statement_cursor ||
        label->statement > definition->body ||
        context->unit->statements[label->statement].kind !=
            CTOOL_C_STATEMENT_LABEL ||
        context->unit->statements[label->statement].label != label_index) {
      return cir_invalid_unit(context, &label->location);
    }
  }
  for (statement_index = context->statement_cursor;
       statement_index <= definition->body; statement_index++) {
    const ctool_c_statement_t *statement =
        &context->unit->statements[statement_index];
    if (statement->kind == CTOOL_C_STATEMENT_LABEL ||
        statement->kind == CTOOL_C_STATEMENT_GOTO) {
      if (cir_label_in_function(context, statement->label) == CTOOL_FALSE) {
        return cir_invalid_unit(context, &statement->location);
      }
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cir_discover_statement_labels(
    cir_context_t *context, ctool_u32 statement_index, ctool_u32 depth,
    ctool_bool entry_reachable, ctool_bool *falls_through_out,
    ctool_bool *active_out, ctool_bool *changed_out) {
  const ctool_c_statement_t *statement;
  ctool_status_t status;
  *falls_through_out = CTOOL_FALSE;
  *active_out = entry_reachable;
  if (depth >= CTOOL_C_PARSE_NESTING_LIMIT) {
    return cir_emit_failure(
        context, CTOOL_ERR_LIMIT, CTOOL_C_IR_DIAG_LIMIT,
        (const ctool_c_pp_location_t *)0,
        "CupidC IR lowering exceeded a configured resource limit");
  }
  if (statement_index >= context->unit->statement_count) {
    return cir_invalid_unit(context, (const ctool_c_pp_location_t *)0);
  }
  statement = &context->unit->statements[statement_index];
  if (statement->kind == CTOOL_C_STATEMENT_GOTO) {
    if (cir_label_in_function(context, statement->label) == CTOOL_FALSE) {
      return cir_invalid_unit(context, &statement->location);
    }
    if (entry_reachable == CTOOL_TRUE &&
        context->label_reachable[statement->label] == CTOOL_FALSE) {
      context->label_reachable[statement->label] = CTOOL_TRUE;
      *changed_out = CTOOL_TRUE;
    }
    return CTOOL_OK;
  }
  if (statement->kind == CTOOL_C_STATEMENT_LABEL) {
    ctool_bool label_entry;
    ctool_bool body_active;
    if (cir_label_in_function(context, statement->label) == CTOOL_FALSE ||
        statement->body >= statement_index ||
        statement->body >= context->unit->statement_count ||
        context->unit->labels[statement->label].statement != statement_index) {
      return cir_invalid_unit(context, &statement->location);
    }
    label_entry =
        entry_reachable == CTOOL_TRUE ||
                context->label_reachable[statement->label] == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    status = cir_discover_statement_labels(
        context, statement->body, depth + 1u, label_entry,
        falls_through_out, &body_active, changed_out);
    if (status == CTOOL_OK) {
      *active_out = label_entry == CTOOL_TRUE || body_active == CTOOL_TRUE
                        ? CTOOL_TRUE
                        : CTOOL_FALSE;
    }
    return status;
  }
  if (statement->kind == CTOOL_C_STATEMENT_COMPOUND) {
    ctool_bool falls_through = entry_reachable;
    ctool_bool active = entry_reachable;
    ctool_u32 child_offset;
    if (statement->first_child > context->unit->statement_child_count ||
        statement->child_count >
            context->unit->statement_child_count - statement->first_child) {
      return cir_invalid_unit(context, &statement->location);
    }
    for (child_offset = 0u; child_offset < statement->child_count;
         child_offset++) {
      ctool_u32 child = context->unit->statement_children
          [statement->first_child + child_offset];
      ctool_bool child_falls;
      ctool_bool child_active;
      if (child >= statement_index ||
          child >= context->unit->statement_count) {
        return cir_invalid_unit(context, &statement->location);
      }
      status = cir_discover_statement_labels(
          context, child, depth + 1u, falls_through, &child_falls,
          &child_active, changed_out);
      if (status != CTOOL_OK) {
        return status;
      }
      if (child_active == CTOOL_TRUE) {
        active = CTOOL_TRUE;
      }
      falls_through = child_falls;
    }
    *falls_through_out = falls_through;
    *active_out = active;
    return CTOOL_OK;
  }
  if (statement->kind == CTOOL_C_STATEMENT_IF) {
    ctool_bool body_falls;
    ctool_bool body_active;
    ctool_bool else_falls = entry_reachable;
    ctool_bool else_active = CTOOL_FALSE;
    if (statement->body >= statement_index ||
        statement->body >= context->unit->statement_count ||
        (statement->else_body != CTOOL_C_AST_NONE &&
         (statement->else_body >= statement_index ||
          statement->else_body >= context->unit->statement_count))) {
      return cir_invalid_unit(context, &statement->location);
    }
    status = cir_discover_statement_labels(
        context, statement->body, depth + 1u, entry_reachable, &body_falls,
        &body_active, changed_out);
    if (status == CTOOL_OK && statement->else_body != CTOOL_C_AST_NONE) {
      status = cir_discover_statement_labels(
          context, statement->else_body, depth + 1u, entry_reachable,
          &else_falls, &else_active, changed_out);
    }
    if (status == CTOOL_OK) {
      *falls_through_out =
          body_falls == CTOOL_TRUE || else_falls == CTOOL_TRUE
              ? CTOOL_TRUE
              : CTOOL_FALSE;
      *active_out = entry_reachable == CTOOL_TRUE ||
                            body_active == CTOOL_TRUE ||
                            else_active == CTOOL_TRUE
                        ? CTOOL_TRUE
                        : CTOOL_FALSE;
    }
    return status;
  }
  if (statement->kind == CTOOL_C_STATEMENT_SWITCH) {
    cir_reach_control_frame_t frame;
    cir_reach_control_frame_t *active_frame;
    ctool_bool body_falls;
    ctool_bool body_active;
    if (statement->body >= statement_index ||
        statement->body >= context->unit->statement_count) {
      return cir_invalid_unit(context, &statement->location);
    }
    if (context->reach_control_depth >= CTOOL_C_PARSE_NESTING_LIMIT) {
      return cir_emit_failure(
          context, CTOOL_ERR_LIMIT, CTOOL_C_IR_DIAG_LIMIT,
          &statement->location,
          "CupidC IR lowering exceeded a configured resource limit");
    }
    active_frame =
        &context->reach_control_frames[context->reach_control_depth++];
    active_frame->kind = CIR_CONTROL_SWITCH;
    active_frame->has_break = CTOOL_FALSE;
    active_frame->has_continue = CTOOL_FALSE;
    active_frame->has_default = CTOOL_FALSE;
    active_frame->switch_entry_reachable = entry_reachable;
    status = cir_discover_statement_labels(
        context, statement->body, depth + 1u, CTOOL_FALSE, &body_falls,
        &body_active, changed_out);
    context->reach_control_depth--;
    frame = context->reach_control_frames[context->reach_control_depth];
    if (status == CTOOL_OK) {
      *falls_through_out =
          (entry_reachable == CTOOL_TRUE &&
           frame.has_default == CTOOL_FALSE) ||
                  frame.has_break == CTOOL_TRUE ||
                  body_falls == CTOOL_TRUE
              ? CTOOL_TRUE
              : CTOOL_FALSE;
      *active_out = entry_reachable == CTOOL_TRUE ||
                            body_active == CTOOL_TRUE
                        ? CTOOL_TRUE
                        : CTOOL_FALSE;
    }
    return status;
  }
  if (statement->kind == CTOOL_C_STATEMENT_CASE ||
      statement->kind == CTOOL_C_STATEMENT_DEFAULT) {
    cir_reach_control_frame_t *frame =
        (cir_reach_control_frame_t *)0;
    ctool_bool label_entry;
    ctool_bool body_active;
    ctool_u32 frame_index;
    if (statement->body >= statement_index ||
        statement->body >= context->unit->statement_count) {
      return cir_invalid_unit(context, &statement->location);
    }
    for (frame_index = context->reach_control_depth; frame_index != 0u;
         frame_index--) {
      cir_reach_control_frame_t *candidate =
          &context->reach_control_frames[frame_index - 1u];
      if (candidate->kind == CIR_CONTROL_SWITCH) {
        frame = candidate;
        break;
      }
    }
    if (frame == (cir_reach_control_frame_t *)0) {
      return cir_invalid_unit(context, &statement->location);
    }
    if (statement->kind == CTOOL_C_STATEMENT_DEFAULT) {
      if (frame->has_default == CTOOL_TRUE) {
        return cir_invalid_unit(context, &statement->location);
      }
      frame->has_default = CTOOL_TRUE;
    }
    label_entry = entry_reachable == CTOOL_TRUE ||
                          frame->switch_entry_reachable == CTOOL_TRUE
                      ? CTOOL_TRUE
                      : CTOOL_FALSE;
    status = cir_discover_statement_labels(
        context, statement->body, depth + 1u, label_entry,
        falls_through_out, &body_active, changed_out);
    if (status == CTOOL_OK) {
      *active_out = label_entry == CTOOL_TRUE || body_active == CTOOL_TRUE
                        ? CTOOL_TRUE
                        : CTOOL_FALSE;
    }
    return status;
  }
  if (statement->kind == CTOOL_C_STATEMENT_WHILE ||
      statement->kind == CTOOL_C_STATEMENT_DO ||
      statement->kind == CTOOL_C_STATEMENT_FOR) {
    cir_reach_control_frame_t frame;
    cir_reach_control_frame_t *active_frame;
    ctool_bool body_falls;
    ctool_bool body_active;
    if (statement->body >= statement_index ||
        statement->body >= context->unit->statement_count) {
      return cir_invalid_unit(context, &statement->location);
    }
    if (context->reach_control_depth >= CTOOL_C_PARSE_NESTING_LIMIT) {
      return cir_emit_failure(
          context, CTOOL_ERR_LIMIT, CTOOL_C_IR_DIAG_LIMIT,
          &statement->location,
          "CupidC IR lowering exceeded a configured resource limit");
    }
    active_frame =
        &context->reach_control_frames[context->reach_control_depth++];
    active_frame->kind = CIR_CONTROL_LOOP;
    active_frame->has_break = CTOOL_FALSE;
    active_frame->has_continue = CTOOL_FALSE;
    active_frame->has_default = CTOOL_FALSE;
    active_frame->switch_entry_reachable = CTOOL_FALSE;
    status = cir_discover_statement_labels(
        context, statement->body, depth + 1u, entry_reachable, &body_falls,
        &body_active, changed_out);
    context->reach_control_depth--;
    frame = context->reach_control_frames[context->reach_control_depth];
    if (status == CTOOL_OK) {
      *active_out = entry_reachable == CTOOL_TRUE ||
                            body_active == CTOOL_TRUE
                        ? CTOOL_TRUE
                        : CTOOL_FALSE;
      if (statement->kind == CTOOL_C_STATEMENT_FOR &&
          statement->condition == CTOOL_C_AST_NONE) {
        *falls_through_out = frame.has_break;
      } else if (statement->kind == CTOOL_C_STATEMENT_DO) {
        *falls_through_out =
            frame.has_break == CTOOL_TRUE || body_falls == CTOOL_TRUE ||
                    frame.has_continue == CTOOL_TRUE
                ? CTOOL_TRUE
                : CTOOL_FALSE;
      } else {
        *falls_through_out =
            entry_reachable == CTOOL_TRUE || frame.has_break == CTOOL_TRUE ||
                    body_falls == CTOOL_TRUE ||
                    frame.has_continue == CTOOL_TRUE
                ? CTOOL_TRUE
                : CTOOL_FALSE;
      }
    }
    return status;
  }
  if (statement->kind == CTOOL_C_STATEMENT_BREAK ||
      statement->kind == CTOOL_C_STATEMENT_CONTINUE) {
    cir_reach_control_frame_t *frame =
        (cir_reach_control_frame_t *)0;
    ctool_u32 frame_index;
    if (entry_reachable == CTOOL_FALSE) {
      return CTOOL_OK;
    }
    if (context->reach_control_depth == 0u) {
      return cir_invalid_unit(context, &statement->location);
    }
    if (statement->kind == CTOOL_C_STATEMENT_BREAK) {
      frame = &context->reach_control_frames
          [context->reach_control_depth - 1u];
      frame->has_break = CTOOL_TRUE;
    } else {
      for (frame_index = context->reach_control_depth; frame_index != 0u;
           frame_index--) {
        cir_reach_control_frame_t *candidate =
            &context->reach_control_frames[frame_index - 1u];
        if (candidate->kind == CIR_CONTROL_LOOP) {
          frame = candidate;
          break;
        }
      }
      if (frame == (cir_reach_control_frame_t *)0) {
        return cir_invalid_unit(context, &statement->location);
      }
      frame->has_continue = CTOOL_TRUE;
    }
    return CTOOL_OK;
  }
  if (statement->kind == CTOOL_C_STATEMENT_RETURN) {
    return CTOOL_OK;
  }
  *falls_through_out = entry_reachable;
  return CTOOL_OK;
}

static ctool_status_t cir_discover_reachable_labels(
    cir_context_t *context,
    const ctool_c_function_definition_t *definition) {
  ctool_u32 iteration;
  for (iteration = 0u; iteration <= context->function_label_count;
       iteration++) {
    ctool_bool changed = CTOOL_FALSE;
    ctool_bool falls_through;
    ctool_bool active;
    ctool_status_t status;
    context->reach_control_depth = 0u;
    status = cir_discover_statement_labels(
        context, definition->body, 0u, CTOOL_TRUE, &falls_through, &active,
        &changed);
    (void)falls_through;
    (void)active;
    if (status != CTOOL_OK || context->reach_control_depth != 0u) {
      if (status == CTOOL_OK) {
        return CTOOL_ERR_INTERNAL;
      }
      return status;
    }
    if (changed == CTOOL_FALSE) {
      context->function_reachable_fallthrough = falls_through;
      return CTOOL_OK;
    }
  }
  return CTOOL_ERR_INTERNAL;
}

static ctool_status_t cir_validate_resolved_gotos(cir_context_t *context) {
  ctool_u32 instruction;
  if (context->instructions == (ctool_c_ir_instruction_t *)0) {
    return CTOOL_OK;
  }
  for (instruction = context->function_first_instruction;
       instruction < context->instruction_count; instruction++) {
    const ctool_c_ir_instruction_t *candidate =
        &context->instructions[instruction];
    if (candidate->kind == CTOOL_C_IR_INSTRUCTION_JUMP &&
        (candidate->reference == CTOOL_C_AST_NONE ||
         candidate->integer_bits != 0u)) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cir_lower_function(
    cir_context_t *context, ctool_u32 function_index) {
  const ctool_c_function_definition_t *definition =
      &context->unit->function_definitions[function_index];
  const ctool_c_type_node_t *function_type;
  const ctool_c_binding_t *binding;
  ctool_c_ir_function_t *function;
  ctool_u32 parameter;
  ctool_status_t status;
  if (definition->binding >= context->unit->binding_count ||
      definition->declared_type >= context->unit->graph.type_count ||
      definition->body >= context->unit->statement_count) {
    return cir_invalid_unit(context, &definition->location);
  }
  status = cir_prepare_function_labels(context, definition);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cir_discover_reachable_labels(context, definition);
  if (status != CTOOL_OK) {
    return status;
  }
  binding = &context->unit->bindings[definition->binding];
  function_type = cir_unwrapped_type(context, definition->declared_type);
  if ((definition->function_declaration_flags &
       ~CTOOL_C_FUNCTION_DECL_ALL) != 0u ||
      binding->kind != CTOOL_C_BINDING_FUNCTION ||
      binding->type >= context->unit->graph.type_count ||
      function_type == (const ctool_c_type_node_t *)0 ||
      function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->referenced_type >= context->unit->graph.type_count ||
      function_type->first_parameter >
          context->unit->graph.parameter_type_count ||
      function_type->parameter_count >
          context->unit->graph.parameter_type_count -
              function_type->first_parameter ||
      function_type->first_parameter > context->unit->parameter_count ||
      function_type->parameter_count >
          context->unit->parameter_count - function_type->first_parameter ||
      cir_types_compatible(context, binding->type,
                           definition->declared_type) == CTOOL_FALSE) {
    return cir_invalid_unit(context, &definition->location);
  }
  if ((definition->function_declaration_flags &
       CTOOL_C_FUNCTION_DECL_INLINE) != 0u &&
      binding->linkage == CTOOL_C_LINKAGE_EXTERNAL) {
    return cir_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_IR_DIAG_EXTERNAL_INLINE, &definition->location,
        "CupidC IR lowering requires external-inline finalization before "
        "lowering this definition");
  }
  if (function_type->has_prototype == CTOOL_FALSE &&
      (function_type->parameter_count != 0u ||
       function_type->variadic == CTOOL_TRUE)) {
    return cir_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
        &definition->location,
        "CupidC IR lowering supports old-style definitions only when their "
        "identifier list is empty");
  }
  if (cir_type_is_void(context, function_type->referenced_type) ==
          CTOOL_FALSE &&
      cir_type_is_value_scalar(
          context, function_type->referenced_type) == CTOOL_FALSE) {
    if (cir_type_is_structure_candidate(
            context, function_type->referenced_type) == CTOOL_FALSE) {
      return cir_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
          &definition->location,
          "CupidC IR lowering supports cdecl functions with represented "
          "scalar or structure parameters and "
          "void, scalar, or structure results");
    }
    status = cir_require_structure_value(
        context, function_type->referenced_type, &definition->location);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  for (parameter = 0u; parameter < function_type->parameter_count;
       parameter++) {
    ctool_u32 absolute = function_type->first_parameter + parameter;
    ctool_u32 declared_type =
        context->unit->graph.parameter_types[absolute];
    ctool_u32 object_type = context->unit->parameters[absolute].type;
    if (declared_type >= context->unit->graph.type_count ||
        object_type >= context->unit->graph.type_count) {
      return cir_invalid_unit(
          context, &context->unit->parameters[absolute].location);
    }
    if (cir_type_is_value_scalar(context, declared_type) == CTOOL_TRUE ||
        cir_type_is_value_scalar(context, object_type) == CTOOL_TRUE) {
      if (cir_type_is_value_scalar(context, declared_type) == CTOOL_FALSE ||
          cir_type_is_value_scalar(context, object_type) == CTOOL_FALSE ||
          cir_scalar_value_types_match(context, declared_type, object_type) ==
              CTOOL_FALSE) {
        return cir_invalid_unit(
            context, &context->unit->parameters[absolute].location);
      }
      continue;
    }
    if (cir_type_is_structure_candidate(context, declared_type) ==
            CTOOL_FALSE ||
        cir_type_is_structure_candidate(context, object_type) ==
            CTOOL_FALSE) {
      return cir_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
          &context->unit->parameters[absolute].location,
          "CupidC IR lowering supports cdecl functions with represented "
          "scalar or structure parameters and "
          "void, scalar, or structure results");
    }
    status = cir_require_structure_value(
        context, declared_type,
        &context->unit->parameters[absolute].location);
    if (status == CTOOL_OK) {
      status = cir_require_structure_value(
          context, object_type,
          &context->unit->parameters[absolute].location);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (cir_structure_value_types_match(context, declared_type,
                                        object_type) == CTOOL_FALSE) {
      return cir_invalid_unit(
          context, &context->unit->parameters[absolute].location);
    }
  }
  context->function_first_instruction = context->instruction_count;
  context->function_first_parameter = function_type->first_parameter;
  context->function_parameter_count = function_type->parameter_count;
  context->function_result_type = function_type->referenced_type;
  context->function_variadic = function_type->variadic;
  context->stack_depth = 0u;
  context->maximum_stack_depth = 0u;
  context->control_depth = 0u;
  context->function_has_patched_target = CTOOL_FALSE;
  context->function_maximum_patched_target = 0u;
  status = cir_lower_body(context, definition);
  if (status == CTOOL_OK) {
    status = cir_validate_resolved_gotos(context);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (context->stack_depth != 0u || context->control_depth != 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  if (context->functions != (ctool_c_ir_function_t *)0) {
    function = &context->functions[function_index];
    function->binding = definition->binding;
    function->declared_type = definition->declared_type;
    function->first_instruction = context->function_first_instruction;
    function->instruction_count =
        context->instruction_count - context->function_first_instruction;
    function->maximum_stack_depth = context->maximum_stack_depth;
    function->location = definition->location;
    function->physical_location = definition->physical_location;
  }
  context->label_cursor += definition->label_count;
  context->statement_cursor = definition->body + 1u;
  return CTOOL_OK;
}

static ctool_status_t cir_lower_functions(cir_context_t *context) {
  ctool_u32 function;
  ctool_status_t status = CTOOL_OK;
  context->instruction_count = 0u;
  context->argument_type_count = 0u;
  context->block_binding_cursor = 0u;
  context->label_cursor = 0u;
  context->statement_cursor = 0u;
  for (function = 0u; function < context->unit->label_count; function++) {
    context->label_targets[function] = CTOOL_C_AST_NONE;
  }
  for (function = 0u;
       status == CTOOL_OK &&
       function < context->unit->function_definition_count;
       function++) {
    status = cir_lower_function(context, function);
  }
  if (status == CTOOL_OK &&
      context->block_binding_cursor != context->unit->block_binding_count) {
    status = cir_invalid_unit(context, (const ctool_c_pp_location_t *)0);
  }
  if (status == CTOOL_OK &&
      (context->label_cursor != context->unit->label_count ||
       context->statement_cursor != context->unit->statement_count)) {
    status = cir_invalid_unit(context, (const ctool_c_pp_location_t *)0);
  }
  return status;
}

static ctool_status_t cir_validate_argument_type_slices(
    const cir_context_t *context) {
  ctool_c_ir_unit_t candidate;
  ctool_bool valid = CTOOL_FALSE;
  ctool_status_t status;
  cir_zero(&candidate, (ctool_u32)sizeof(candidate));
  candidate.instructions = context->instructions;
  candidate.instruction_count = context->instruction_count;
  candidate.argument_types = context->argument_types;
  candidate.argument_type_count = context->argument_type_count;
  status = ctool_c_ir_validate_call_slices(
      context->unit, &candidate, &valid);
  return status == CTOOL_OK && valid == CTOOL_TRUE
             ? CTOOL_OK
             : CTOOL_ERR_INTERNAL;
}

static ctool_status_t cir_alloc_array(cir_context_t *context,
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

static ctool_status_t cir_validate_initializer_ownership(
    cir_context_t *context) {
  ctool_arena_mark_t mark = ctool_arena_mark(context->arena);
  ctool_u32 *owners = (ctool_u32 *)0;
  ctool_u32 *expression_limits = (ctool_u32 *)0;
  const ctool_c_pp_location_t *invalid_location =
      (const ctool_c_pp_location_t *)0;
  ctool_bool valid = CTOOL_TRUE;
  ctool_status_t status = cir_alloc_array(
      context, context->unit->initializer_count,
      (ctool_u32)sizeof(*owners), (void **)&owners);
  ctool_status_t rewind_status;
  ctool_u32 element_cursor = 0u;
  ctool_u32 index;
  if (status == CTOOL_OK) {
    status = cir_alloc_array(
        context, context->unit->initializer_count,
        (ctool_u32)sizeof(*expression_limits),
        (void **)&expression_limits);
  }
  for (index = 0u;
       status == CTOOL_OK && valid == CTOOL_TRUE &&
       index < context->unit->initializer_count;
       index++) {
    const ctool_c_initializer_t *initializer =
        &context->unit->initializers[index];
    ctool_u32 child_offset;
    if (initializer->kind != CTOOL_C_INITIALIZER_LIST) {
      continue;
    }
    if (initializer->element_count == 0u ||
        initializer->first_element != element_cursor ||
        initializer->first_element >
            context->unit->initializer_element_count ||
        initializer->element_count >
            context->unit->initializer_element_count -
                initializer->first_element) {
      invalid_location = &initializer->location;
      valid = CTOOL_FALSE;
      break;
    }
    for (child_offset = 0u;
         valid == CTOOL_TRUE &&
         child_offset < initializer->element_count;
         child_offset++) {
      ctool_u32 child = context->unit->initializer_elements
                            [initializer->first_element + child_offset]
                                .initializer;
      if (child >= index || owners[child] != 0u) {
        invalid_location = &initializer->location;
        if (child < context->unit->initializer_count) {
          invalid_location = &context->unit->initializers[child].location;
        }
        valid = CTOOL_FALSE;
      } else {
        owners[child] = 1u;
      }
    }
    element_cursor += initializer->element_count;
  }
  if (status == CTOOL_OK && valid == CTOOL_TRUE &&
      element_cursor != context->unit->initializer_element_count) {
    valid = CTOOL_FALSE;
  }
  for (index = 0u;
       status == CTOOL_OK && valid == CTOOL_TRUE &&
       index < context->unit->object_definition_count;
       index++) {
    const ctool_c_object_definition_t *definition =
        &context->unit->object_definitions[index];
    ctool_u32 root = definition->initializer;
    if (root >= context->unit->initializer_count || owners[root] != 0u) {
      invalid_location = &definition->location;
      valid = CTOOL_FALSE;
    } else {
      owners[root] = 1u;
    }
  }
  for (index = 0u;
       status == CTOOL_OK && valid == CTOOL_TRUE &&
       index < context->unit->block_binding_count;
       index++) {
    const ctool_c_block_binding_t *binding =
        &context->unit->block_bindings[index];
    ctool_u32 root = binding->initializer;
    if (root == CTOOL_C_AST_NONE) {
      continue;
    }
    if (root >= context->unit->initializer_count || owners[root] != 0u) {
      invalid_location = &binding->location;
      valid = CTOOL_FALSE;
    } else {
      owners[root] = 1u;
    }
  }
  for (index = 0u;
       status == CTOOL_OK && valid == CTOOL_TRUE &&
       index < context->unit->expression_count;
       index++) {
    const ctool_c_expression_t *expression =
        &context->unit->expressions[index];
    ctool_u32 root;
    if (expression->kind != CTOOL_C_EXPRESSION_COMPOUND_LITERAL) {
      continue;
    }
    root = expression->reference;
    if (root >= context->unit->initializer_count || owners[root] != 0u ||
        index == 0xffffffffu) {
      invalid_location = &expression->location;
      valid = CTOOL_FALSE;
    } else {
      owners[root] = 1u;
      expression_limits[root] = index + 1u;
    }
  }
  for (index = context->unit->initializer_count;
       status == CTOOL_OK && valid == CTOOL_TRUE && index != 0u;) {
    const ctool_c_initializer_t *initializer;
    ctool_u32 child_offset;
    index--;
    if (expression_limits[index] == 0u) {
      continue;
    }
    initializer = &context->unit->initializers[index];
    if (initializer->kind == CTOOL_C_INITIALIZER_EXPRESSION &&
        initializer->expression >= expression_limits[index] - 1u) {
      invalid_location = &initializer->location;
      valid = CTOOL_FALSE;
      break;
    }
    if (initializer->kind != CTOOL_C_INITIALIZER_LIST) {
      continue;
    }
    for (child_offset = 0u;
         valid == CTOOL_TRUE && child_offset < initializer->element_count;
         child_offset++) {
      ctool_u32 child = context->unit->initializer_elements
                            [initializer->first_element + child_offset]
                                .initializer;
      if (child >= index || expression_limits[child] != 0u) {
        invalid_location = &initializer->location;
        valid = CTOOL_FALSE;
      } else {
        expression_limits[child] = expression_limits[index];
      }
    }
  }
  for (index = 0u;
       status == CTOOL_OK && valid == CTOOL_TRUE &&
       index < context->unit->initializer_count;
       index++) {
    if (owners[index] != 1u) {
      invalid_location = &context->unit->initializers[index].location;
      valid = CTOOL_FALSE;
    }
  }
  rewind_status = ctool_arena_rewind(context->arena, mark);
  if (rewind_status != CTOOL_OK) {
    return rewind_status;
  }
  if (status != CTOOL_OK) {
    return status;
  }
  return valid == CTOOL_TRUE
             ? CTOOL_OK
             : cir_invalid_unit(context, invalid_location);
}

ctool_status_t ctool_c_lower_ir(ctool_job_t *job,
                                const ctool_c_translation_unit_t *unit,
                                ctool_c_ir_unit_t *result_out) {
  cir_context_t context;
  ctool_arena_mark_t mark;
  ctool_u32 instruction_count = 0u;
  ctool_u32 argument_type_count = 0u;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  ctool_status_t rewind_status;
  cir_zero_result(result_out);
  if (job == (ctool_job_t *)0 || result_out == (ctool_c_ir_unit_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  cir_zero(&context, (ctool_u32)sizeof(context));
  context.job = job;
  context.unit = unit;
  context.arena = ctool_job_arena(job);
  mark = ctool_arena_mark(context.arena);
  diagnostic_count = ctool_job_diagnostic_count(job);
  if (unit == (const ctool_c_translation_unit_t *)0) {
    status = cir_emit_failure(
        &context, CTOOL_ERR_INVALID_ARGUMENT,
        CTOOL_C_IR_DIAG_INVALID_REQUEST,
        (const ctool_c_pp_location_t *)0,
        "CupidC IR lowering requires a typed translation unit");
  } else {
    status = cir_validate_unit_shape(&context);
  }
  if (status == CTOOL_OK) {
    status = cir_alloc_array(
        &context, unit->label_count, (ctool_u32)sizeof(*context.label_targets),
        (void **)&context.label_targets);
  }
  if (status == CTOOL_OK) {
    status = cir_alloc_array(
        &context, unit->label_count,
        (ctool_u32)sizeof(*context.label_reachable),
        (void **)&context.label_reachable);
  }
  if (status == CTOOL_OK) {
    status = cir_validate_initializer_ownership(&context);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_functions(&context);
    instruction_count = context.instruction_count;
    argument_type_count = context.argument_type_count;
  }
  if (status == CTOOL_OK) {
    status = cir_alloc_array(
        &context, unit->function_definition_count,
        (ctool_u32)sizeof(ctool_c_ir_function_t),
        (void **)&context.functions);
  }
  if (status == CTOOL_OK) {
    status = cir_alloc_array(
        &context, instruction_count,
        (ctool_u32)sizeof(ctool_c_ir_instruction_t),
        (void **)&context.instructions);
  }
  if (status == CTOOL_OK) {
    status = cir_alloc_array(
        &context, argument_type_count, (ctool_u32)sizeof(ctool_u32),
        (void **)&context.argument_types);
  }
  if (status == CTOOL_OK) {
    context.instruction_capacity = instruction_count;
    context.argument_type_capacity = argument_type_count;
    status = cir_lower_functions(&context);
    if (status == CTOOL_OK &&
        (context.instruction_count != instruction_count ||
         context.argument_type_count != argument_type_count)) {
      status = CTOOL_ERR_INTERNAL;
    }
  }
  if (status == CTOOL_OK) {
    status = cir_validate_argument_type_slices(&context);
  }
  if (status != CTOOL_OK && context.failure_reported == CTOOL_FALSE &&
      ctool_job_diagnostic_count(job) == diagnostic_count) {
    if (status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_NO_MEMORY ||
        status == CTOOL_ERR_OVERFLOW) {
      status = cir_emit_failure(
          &context, status, CTOOL_C_IR_DIAG_LIMIT,
          (const ctool_c_pp_location_t *)0,
          "CupidC IR lowering exceeded a configured resource limit");
    } else {
      status = cir_emit_failure(
          &context, status, CTOOL_C_IR_DIAG_INTERNAL,
          (const ctool_c_pp_location_t *)0,
          "CupidC IR lowering failed before publishing a result");
    }
  }
  if (status != CTOOL_OK) {
    rewind_status = ctool_arena_rewind(context.arena, mark);
    cir_zero_result(result_out);
    return rewind_status == CTOOL_OK ? status : rewind_status;
  }
  result_out->functions = context.functions;
  result_out->function_count = unit->function_definition_count;
  result_out->instructions = context.instructions;
  result_out->instruction_count = instruction_count;
  result_out->argument_types = context.argument_types;
  result_out->argument_type_count = argument_type_count;
  return CTOOL_OK;
}
