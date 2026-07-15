#include "cupidc_ir.h"

#define CIR_STACK_LIMIT (CTOOL_C_PARSE_NESTING_LIMIT + 1u)

typedef enum {
  CIR_STACK_VALUE = 1,
  CIR_STACK_ADDRESS
} cir_stack_kind_t;

typedef struct {
  cir_stack_kind_t kind;
  ctool_u32 type;
} cir_stack_entry_t;

typedef struct {
  ctool_job_t *job;
  const ctool_c_translation_unit_t *unit;
  ctool_arena_t *arena;
  ctool_c_ir_function_t *functions;
  ctool_c_ir_instruction_t *instructions;
  ctool_u32 instruction_count;
  ctool_u32 instruction_capacity;
  ctool_u32 function_first_instruction;
  ctool_u32 function_first_parameter;
  ctool_u32 function_parameter_count;
  ctool_u32 function_first_block_binding;
  ctool_u32 function_block_binding_count;
  ctool_u32 visible_block_binding_end;
  ctool_u32 block_binding_cursor;
  ctool_u32 function_result_type;
  cir_stack_entry_t stack[CIR_STACK_LIMIT];
  ctool_u32 stack_depth;
  ctool_u32 maximum_stack_depth;
  ctool_bool failure_reported;
} cir_context_t;

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

static ctool_bool cir_add_overflows(ctool_u32 left, ctool_u32 right) {
  return left > 0xffffffffu - right ? CTOOL_TRUE : CTOOL_FALSE;
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
  return cir_emit_failure(
      context, CTOOL_ERR_INPUT, CTOOL_C_IR_DIAG_INVALID_UNIT, location,
      "CupidC IR lowering received an invalid translation unit");
}

static ctool_status_t cir_unsupported_statement(
    cir_context_t *context, const ctool_c_pp_location_t *location) {
  return cir_emit_failure(
      context, CTOOL_ERR_UNSUPPORTED,
      CTOOL_C_IR_DIAG_UNSUPPORTED_STATEMENT, location,
      "CupidC IR lowering does not yet support this statement");
}

static ctool_status_t cir_unsupported_expression(
    cir_context_t *context, const ctool_c_pp_location_t *location) {
  return cir_emit_failure(
      context, CTOOL_ERR_UNSUPPORTED,
      CTOOL_C_IR_DIAG_UNSUPPORTED_EXPRESSION, location,
      "CupidC IR lowering does not yet support this expression");
}

static ctool_status_t cir_unsupported_conversion(
    cir_context_t *context, const ctool_c_pp_location_t *location) {
  return cir_emit_failure(
      context, CTOOL_ERR_UNSUPPORTED,
      CTOOL_C_IR_DIAG_UNSUPPORTED_CONVERSION, location,
      "CupidC IR lowering does not yet support this conversion");
}

static ctool_status_t cir_unsupported_type(
    cir_context_t *context, const ctool_c_pp_location_t *location) {
  return cir_emit_failure(
      context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
      location, "CupidC IR lowering does not yet support this value type");
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

static ctool_status_t cir_patch_reference(cir_context_t *context,
                                          ctool_u32 instruction,
                                          ctool_u32 reference) {
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
  status = cir_lower_expression(context, child, depth + 1u);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cir_pop(context, &source);
  if (status != CTOOL_OK) {
    return status;
  }
  if (expression->conversion == CTOOL_C_CONVERSION_LVALUE_TO_VALUE) {
    if (source.kind != CIR_STACK_ADDRESS || source.type !=
                                                   context->unit
                                                       ->expressions[child]
                                                       .type ||
        cir_type_is_i32_integer(context, source.type) == CTOOL_FALSE ||
        cir_type_is_i32_integer(context, expression->type) == CTOOL_FALSE) {
      return cir_unsupported_conversion(context, &expression->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_LOAD, expression->type, source.type,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, expression->conversion,
        CTOOL_C_AST_NONE, 0u, &expression->location,
        &expression->physical_location, (ctool_u32 *)0);
  } else if (expression->conversion == CTOOL_C_CONVERSION_QUALIFICATION ||
             expression->conversion ==
                 CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
             expression->conversion ==
                 CTOOL_C_CONVERSION_USUAL_ARITHMETIC ||
             expression->conversion == CTOOL_C_CONVERSION_ASSIGNMENT) {
    if (source.kind != CIR_STACK_VALUE ||
        cir_type_is_i32_integer(context, source.type) == CTOOL_FALSE ||
        cir_type_is_i32_integer(context, expression->type) == CTOOL_FALSE) {
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

static ctool_status_t cir_lower_binary(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  cir_stack_entry_t left;
  cir_stack_entry_t right;
  ctool_u32 left_child;
  ctool_u32 right_child;
  ctool_status_t status = cir_expression_child(
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
  if (left.kind != CIR_STACK_VALUE || right.kind != CIR_STACK_VALUE ||
      left.type != right.type ||
      cir_type_is_i32_integer(context, left.type) == CTOOL_FALSE ||
      cir_type_is_i32_integer(context, expression->type) == CTOOL_FALSE) {
    return cir_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE, &expression->location,
        "CupidC IR lowering does not yet support this value type");
  }
  if (expression->operation != CTOOL_C_EXPRESSION_OPERATOR_ADD &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_GREATER &&
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL) {
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
      cir_type_is_i32_integer(context, condition.type) == CTOOL_FALSE) {
    return cir_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE, &expression->location,
        "CupidC IR lowering does not yet support this value type");
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
        when_zero.kind != CIR_STACK_VALUE ||
        when_nonzero.type != expression->type ||
        when_zero.type != expression->type) {
      status = cir_invalid_unit(context, &expression->location);
    }
  }
  if (status == CTOOL_OK) {
    status = cir_patch_reference(context, jump,
                                 cir_function_offset(context));
  }
  return status;
}

static ctool_status_t cir_lower_direct_call(
    cir_context_t *context, ctool_u32 expression_index,
    const ctool_c_expression_t *expression, ctool_u32 depth) {
  const ctool_c_expression_t *callee;
  const ctool_c_expression_t *identifier;
  const ctool_c_binding_t *binding;
  const ctool_c_type_node_t *pointer_type;
  const ctool_c_type_node_t *function_type;
  ctool_u32 callee_index;
  ctool_u32 identifier_index;
  ctool_u32 base_depth = context->stack_depth;
  ctool_u32 argument;
  ctool_status_t status;
  status = cir_expression_child(context, expression_index, expression, 0u,
                                &callee_index);
  if (status != CTOOL_OK) {
    return status;
  }
  callee = &context->unit->expressions[callee_index];
  if (callee->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      callee->conversion != CTOOL_C_CONVERSION_FUNCTION_TO_POINTER) {
    return cir_unsupported_expression(context, &expression->location);
  }
  if (callee->child_count != 1u) {
    return cir_invalid_unit(context, &callee->location);
  }
  status = cir_expression_child(context, callee_index, callee, 0u,
                                &identifier_index);
  if (status != CTOOL_OK) {
    return status;
  }
  identifier = &context->unit->expressions[identifier_index];
  if (identifier->kind != CTOOL_C_EXPRESSION_IDENTIFIER) {
    return cir_unsupported_expression(context, &expression->location);
  }
  if (identifier->child_count != 0u ||
      identifier->reference >= context->unit->binding_count ||
      identifier->type >= context->unit->graph.type_count ||
      callee->type >= context->unit->graph.type_count) {
    return cir_invalid_unit(context, &identifier->location);
  }
  binding = &context->unit->bindings[identifier->reference];
  pointer_type = cir_unwrapped_type(context, callee->type);
  function_type = cir_unwrapped_type(context, identifier->type);
  if (binding->kind != CTOOL_C_BINDING_FUNCTION ||
      binding->type != identifier->type ||
      pointer_type == (const ctool_c_type_node_t *)0 ||
      pointer_type->kind != CTOOL_C_TYPE_POINTER ||
      pointer_type->referenced_type != identifier->type ||
      function_type == (const ctool_c_type_node_t *)0 ||
      function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->referenced_type != expression->type ||
      function_type->first_parameter >
          context->unit->graph.parameter_type_count ||
      function_type->parameter_count >
          context->unit->graph.parameter_type_count -
              function_type->first_parameter ||
      expression->child_count == 0u ||
      expression->child_count - 1u != function_type->parameter_count) {
    return cir_invalid_unit(context, &expression->location);
  }
  if (function_type->has_prototype == CTOOL_FALSE ||
      function_type->variadic == CTOOL_TRUE ||
      (cir_type_is_void(context, expression->type) == CTOOL_FALSE &&
       cir_type_is_i32_integer(context, expression->type) == CTOOL_FALSE)) {
    return cir_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
        &expression->location,
        "CupidC IR lowering supports only fixed, nonvariadic direct calls "
        "with 32-bit integer arguments and void or 32-bit integer results");
  }
  for (argument = 0u; argument < function_type->parameter_count; argument++) {
    ctool_u32 child;
    ctool_u32 parameter_type =
        context->unit->graph
            .parameter_types[function_type->first_parameter + argument];
    if (parameter_type >= context->unit->graph.type_count ||
        cir_type_is_i32_integer(context, parameter_type) == CTOOL_FALSE) {
      return cir_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
          &expression->location,
          "CupidC IR lowering supports only fixed, nonvariadic direct calls "
          "with 32-bit integer arguments and void or 32-bit integer results");
    }
    status = cir_expression_child(context, expression_index, expression,
                                  argument + 1u, &child);
    if (status == CTOOL_OK) {
      status = cir_lower_expression(context, child, depth + 1u);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (context->stack_depth != base_depth + argument + 1u ||
        context->stack[base_depth + argument].kind != CIR_STACK_VALUE ||
        context->stack[base_depth + argument].type != parameter_type) {
      return cir_invalid_unit(context, &expression->location);
    }
  }
  context->stack_depth = base_depth;
  status = cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_CALL_DIRECT, expression->type,
      identifier->type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      CTOOL_C_CONVERSION_NONE, identifier->reference, 0u,
      &expression->location, &expression->physical_location,
      (ctool_u32 *)0);
  if (status != CTOOL_OK ||
      cir_type_is_void(context, expression->type) == CTOOL_TRUE) {
    return status;
  }
  return cir_push(context, CIR_STACK_VALUE, expression->type);
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
            expression->type ||
        cir_type_is_i32_integer(context, expression->type) == CTOOL_FALSE) {
      return cir_invalid_unit(context, &expression->location);
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
    if (binding->type != expression->type ||
        expression->type >= context->unit->graph.type_count) {
      return cir_invalid_unit(context, &expression->location);
    }
    if (binding->kind == CTOOL_C_BINDING_FUNCTION ||
        binding->kind == CTOOL_C_BINDING_ENUMERATOR) {
      return cir_unsupported_expression(context, &expression->location);
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
    if (cir_type_is_i32_integer(context, expression->type) == CTOOL_FALSE) {
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
    if (binding->kind != CTOOL_C_BINDING_OBJECT ||
        binding->type != expression->type ||
        cir_type_is_i32_integer(context, expression->type) == CTOOL_FALSE) {
      return cir_invalid_unit(context, &expression->location);
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
  if (expression->kind == CTOOL_C_EXPRESSION_INTEGER_CONSTANT) {
    if (expression->child_count != 0u ||
        cir_type_is_i32_integer(context, expression->type) == CTOOL_FALSE ||
        (expression->integer_bits >> 32u) != 0u) {
      return cir_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE, &expression->location,
          "CupidC IR lowering does not yet support this value type");
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_INTEGER, expression->type,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE,
        expression->integer_bits, &expression->location,
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
  if (expression->kind == CTOOL_C_EXPRESSION_BINARY) {
    if (expression->child_count != 2u) {
      return cir_invalid_unit(context, &expression->location);
    }
    return cir_lower_binary(context, expression_index, expression, depth);
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
    return cir_lower_direct_call(context, expression_index, expression,
                                 depth);
  }
  if (expression->kind == CTOOL_C_EXPRESSION_CAST) {
    return cir_unsupported_conversion(context, &expression->location);
  }
  return cir_unsupported_expression(context, &expression->location);
}

static ctool_status_t cir_lower_declaration(
    cir_context_t *context, const ctool_c_statement_t *statement) {
  ctool_u32 binding_offset;
  for (binding_offset = 0u;
       binding_offset < statement->block_binding_count; binding_offset++) {
    const ctool_c_block_binding_t *binding;
    const ctool_c_type_layout_t *layout;
    const ctool_c_initializer_t *initializer;
    cir_stack_entry_t address;
    cir_stack_entry_t value;
    ctool_u32 initializer_base_depth;
    ctool_u32 binding_index =
        statement->first_block_binding + binding_offset;
    ctool_status_t status;
    if (binding_index >= context->unit->block_binding_count ||
        binding_index != context->block_binding_cursor) {
      return cir_invalid_unit(context, &statement->location);
    }
    binding = &context->unit->block_bindings[binding_index];
    if (binding->kind < CTOOL_C_BINDING_TYPEDEF ||
        binding->kind > CTOOL_C_BINDING_ENUMERATOR ||
        binding->storage < CTOOL_C_STORAGE_NONE ||
        binding->storage > CTOOL_C_STORAGE_REGISTER) {
      return cir_invalid_unit(context, &binding->location);
    }
    if (binding->kind != CTOOL_C_BINDING_OBJECT) {
      return cir_unsupported_statement(context, &binding->location);
    }
    if (binding->storage != CTOOL_C_STORAGE_NONE &&
        binding->storage != CTOOL_C_STORAGE_AUTO &&
        binding->storage != CTOOL_C_STORAGE_REGISTER) {
      return cir_unsupported_statement(context, &binding->location);
    }
    if (binding->type >= context->unit->layout.type_count) {
      return cir_invalid_unit(context, &binding->location);
    }
    layout = &context->unit->layout.types[binding->type];
    if (layout->is_complete_object == CTOOL_FALSE ||
        layout->is_object == CTOOL_FALSE || layout->alignment == 0u ||
        (layout->alignment & (layout->alignment - 1u)) != 0u) {
      return cir_invalid_unit(context, &binding->location);
    }
    if (layout->is_integer == CTOOL_FALSE || layout->size != 4u ||
        layout->alignment > 4u) {
      return cir_unsupported_type(context, &binding->location);
    }
    context->block_binding_cursor++;
    context->visible_block_binding_end = context->block_binding_cursor;
    if (binding->initializer == CTOOL_C_AST_NONE) {
      continue;
    }
    if (binding->initializer >= context->unit->initializer_count) {
      return cir_invalid_unit(context, &binding->location);
    }
    initializer = &context->unit->initializers[binding->initializer];
    if (initializer->kind != CTOOL_C_INITIALIZER_EXPRESSION ||
        initializer->type != binding->type ||
        initializer->expression >= context->unit->expression_count ||
        initializer->integer_bits != 0u ||
        initializer->string_bytes.data != (const ctool_u8 *)0 ||
        initializer->string_bytes.size != 0u ||
        initializer->address_kind != CTOOL_C_INITIALIZER_ADDRESS_NONE ||
        initializer->address_reference != CTOOL_C_AST_NONE ||
        initializer->address_addend != 0 ||
        initializer->first_element != CTOOL_C_AST_NONE ||
        initializer->element_count != 0u ||
        cir_integer_value_types_match(
            context, initializer->type,
            context->unit->expressions[initializer->expression].type) ==
            CTOOL_FALSE) {
      return cir_invalid_unit(context, &initializer->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS, binding->type,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, binding_index, 0u, &binding->location,
        &binding->physical_location, (ctool_u32 *)0);
    if (status == CTOOL_OK) {
      status = cir_push(context, CIR_STACK_ADDRESS, binding->type);
    }
    initializer_base_depth = context->stack_depth;
    if (status == CTOOL_OK) {
      status = cir_lower_expression(context, initializer->expression, 0u);
    }
    if (status == CTOOL_OK &&
        (cir_add_overflows(initializer_base_depth, 1u) == CTOOL_TRUE ||
         context->stack_depth != initializer_base_depth + 1u)) {
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
        address.kind != CIR_STACK_ADDRESS || address.type != binding->type ||
        cir_type_is_i32_integer(context, value.type) == CTOOL_FALSE ||
        cir_integer_value_types_match(context, binding->type, value.type) ==
            CTOOL_FALSE) {
      return cir_invalid_unit(context, &initializer->location);
    }
    status = cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_STORE, binding->type, value.type,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
        CTOOL_C_AST_NONE, 0u, &initializer->location,
        &initializer->physical_location, (ctool_u32 *)0);
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
  if (result.kind != CIR_STACK_VALUE ||
      result.type != context->function_result_type ||
      context->stack_depth != 0u) {
    return cir_invalid_unit(context, &statement->location);
  }
  return cir_append_instruction(
      context, CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
      context->function_result_type, result.type,
      CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
      CTOOL_C_AST_NONE, 0u, &statement->location,
      &statement->physical_location, (ctool_u32 *)0);
}

static ctool_status_t cir_lower_body(
    cir_context_t *context, const ctool_c_function_definition_t *definition) {
  const ctool_c_statement_t *body;
  const ctool_c_statement_t *statement;
  ctool_u32 declaration_count = 0u;
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
  context->function_first_block_binding = context->block_binding_cursor;
  context->function_block_binding_count = 0u;
  context->visible_block_binding_end = context->block_binding_cursor;
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
    statement_index =
        context->unit->statement_children[body->first_child + child_offset];
    if (statement_index >= definition->body ||
        statement_index >= context->unit->statement_count) {
      return cir_invalid_unit(context, &body->location);
    }
    statement = &context->unit->statements[statement_index];
    if (statement->kind != CTOOL_C_STATEMENT_DECLARATION) {
      break;
    }
    if (statement->first_block_binding == CTOOL_C_AST_NONE ||
        statement->first_block_binding > context->unit->block_binding_count ||
        statement->block_binding_count == 0u ||
        statement->block_binding_count >
            context->unit->block_binding_count -
                statement->first_block_binding) {
      return cir_invalid_unit(context, &statement->location);
    }
    if (cir_add_overflows(context->function_first_block_binding,
                          context->function_block_binding_count) ==
            CTOOL_TRUE ||
        (declaration_count == 0u &&
         statement->first_block_binding != context->block_binding_cursor) ||
        (declaration_count != 0u &&
         statement->first_block_binding !=
               context->function_first_block_binding +
                   context->function_block_binding_count)) {
      return cir_invalid_unit(context, &statement->location);
    }
    if (cir_add_overflows(context->function_block_binding_count,
                          statement->block_binding_count) == CTOOL_TRUE) {
      return cir_invalid_unit(context, &statement->location);
    }
    context->function_block_binding_count += statement->block_binding_count;
    declaration_count++;
  }
  if (declaration_count != 0u &&
      (cir_add_overflows(context->function_first_block_binding,
                         context->function_block_binding_count) ==
           CTOOL_TRUE ||
       context->function_first_block_binding +
               context->function_block_binding_count >
           context->unit->block_binding_count)) {
    return cir_invalid_unit(context, &body->location);
  }
  if (body->child_count - declaration_count != 1u) {
    return cir_unsupported_statement(context, &body->location);
  }
  for (child_offset = 0u; child_offset < declaration_count; child_offset++) {
    statement_index =
        context->unit->statement_children[body->first_child + child_offset];
    status = cir_lower_declaration(
        context, &context->unit->statements[statement_index]);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  statement_index = context->unit->statement_children
      [body->first_child + declaration_count];
  if (statement_index >= definition->body ||
      statement_index >= context->unit->statement_count) {
    return cir_invalid_unit(context, &body->location);
  }
  statement = &context->unit->statements[statement_index];
  if (statement->kind == CTOOL_C_STATEMENT_EXPRESSION) {
    if (declaration_count != 0u ||
        cir_type_is_void(context, context->function_result_type) ==
            CTOOL_FALSE ||
        statement->expression == CTOOL_C_AST_NONE) {
      return cir_unsupported_statement(context, &statement->location);
    }
    if (statement->expression >= context->unit->expression_count) {
      return cir_invalid_unit(context, &statement->location);
    }
    if (cir_type_is_void(
            context,
            context->unit->expressions[statement->expression].type) ==
        CTOOL_FALSE) {
      return cir_unsupported_statement(context, &statement->location);
    }
    status = cir_lower_expression(context, statement->expression, 0u);
    if (status != CTOOL_OK) {
      return status;
    }
    if (context->stack_depth != 0u) {
      return cir_invalid_unit(context, &statement->location);
    }
    return cir_append_instruction(
        context, CTOOL_C_IR_INSTRUCTION_RETURN_VOID, CTOOL_C_TYPE_NONE,
        CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
        CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u, &body->location,
        &body->physical_location, (ctool_u32 *)0);
  }
  return cir_lower_return(context, statement);
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
  binding = &context->unit->bindings[definition->binding];
  function_type = cir_unwrapped_type(context, definition->declared_type);
  if ((definition->function_declaration_flags &
       ~CTOOL_C_FUNCTION_DECL_ALL) != 0u ||
      binding->kind != CTOOL_C_BINDING_FUNCTION ||
      function_type == (const ctool_c_type_node_t *)0 ||
      function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->referenced_type >= context->unit->graph.type_count ||
      function_type->first_parameter > context->unit->parameter_count ||
      function_type->parameter_count >
          context->unit->parameter_count - function_type->first_parameter) {
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
  if (function_type->has_prototype == CTOOL_FALSE ||
      function_type->variadic == CTOOL_TRUE ||
      (cir_type_is_void(context, function_type->referenced_type) ==
           CTOOL_FALSE &&
       cir_type_is_i32_integer(context, function_type->referenced_type) ==
           CTOOL_FALSE)) {
    return cir_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
        &definition->location,
        "CupidC IR lowering supports only fixed, nonvariadic cdecl functions "
        "with 32-bit integer parameters and void or 32-bit integer results");
  }
  for (parameter = 0u; parameter < function_type->parameter_count;
       parameter++) {
    ctool_u32 absolute = function_type->first_parameter + parameter;
    if (context->unit->graph.parameter_types[absolute] >=
            context->unit->graph.type_count ||
        context->unit->parameters[absolute].type >=
            context->unit->graph.type_count ||
        cir_type_is_i32_integer(
            context, context->unit->parameters[absolute].type) ==
            CTOOL_FALSE) {
      return cir_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_ABI,
          &context->unit->parameters[absolute].location,
          "CupidC IR lowering supports only fixed, nonvariadic cdecl functions "
          "with 32-bit integer parameters and void or 32-bit integer results");
    }
  }
  context->function_first_instruction = context->instruction_count;
  context->function_first_parameter = function_type->first_parameter;
  context->function_parameter_count = function_type->parameter_count;
  context->function_result_type = function_type->referenced_type;
  context->stack_depth = 0u;
  context->maximum_stack_depth = 0u;
  status = cir_lower_body(context, definition);
  if (status != CTOOL_OK) {
    return status;
  }
  if (context->stack_depth != 0u) {
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
  return CTOOL_OK;
}

static ctool_status_t cir_lower_functions(cir_context_t *context) {
  ctool_u32 function;
  ctool_status_t status = CTOOL_OK;
  context->instruction_count = 0u;
  context->block_binding_cursor = 0u;
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
  return status;
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
  const ctool_c_pp_location_t *invalid_location =
      (const ctool_c_pp_location_t *)0;
  ctool_bool valid = CTOOL_TRUE;
  ctool_status_t status = cir_alloc_array(
      context, context->unit->initializer_count,
      (ctool_u32)sizeof(*owners), (void **)&owners);
  ctool_status_t rewind_status;
  ctool_u32 element_cursor = 0u;
  ctool_u32 index;
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
    status = cir_validate_initializer_ownership(&context);
  }
  if (status == CTOOL_OK) {
    status = cir_lower_functions(&context);
    instruction_count = context.instruction_count;
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
    context.instruction_capacity = instruction_count;
    status = cir_lower_functions(&context);
    if (status == CTOOL_OK &&
        context.instruction_count != instruction_count) {
      status = CTOOL_ERR_INTERNAL;
    }
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
  return CTOOL_OK;
}
