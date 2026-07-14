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

static const char active_call[] =
    "static uint32_t syscall_getpid(void) { return process_get_current_pid(); }";

static const char active_addition[] =
    "int add2(int x, int y) {\n"
    "    return x + y;\n"
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

static int arena_marks_equal(ctool_arena_mark_t left,
                             ctool_arena_mark_t right) {
  return left.owner == right.owner && left.block == right.block &&
                 left.used == right.used && left.generation == right.generation
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

static int parse_source(ctool_job_t *job, const char *path, const char *text,
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
  pp_request.gnu_extensions = CTOOL_FALSE;
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
  parse_request.gnu_extensions = CTOOL_FALSE;
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

static int active_source_is_unchanged(ctool_job_t *job) {
  ctool_path_t path;
  ctool_source_t source;
  ctool_status_t status;
  path.text = ctool_string("/toolchain/cupidc_emit.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active emitter source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_helper) == NULL) {
    (void)fprintf(stderr, "the active overflow helper changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/core/syscall.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active syscall source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_call) == NULL) {
    (void)fprintf(stderr, "the active getpid wrapper changed\n");
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

static uint64_t unit_fingerprint(const ctool_c_translation_unit_t *unit) {
  uint64_t hash = UINT64_C(1469598103934665603);
  hash = hash_bytes(hash, unit, sizeof(*unit));
  hash = hash_bytes(hash, unit->bindings,
                    (size_t)unit->binding_count * sizeof(*unit->bindings));
  hash = hash_bytes(hash, unit->parameters,
                    (size_t)unit->parameter_count * sizeof(*unit->parameters));
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

static int run_active_leaf(const char *host_root) {
  static const char simple_source[] =
      "int answer(void) { return 42; }\n"
      "static void idle(void) {}\n"
      "unsigned int as_unsigned(int value) { return value; }\n";
  static const char statement_source[] =
      "int choose(int value) {\n"
      "  if (value) return 1;\n"
      "  return 0;\n"
      "}\n";
  static const char expression_source[] =
      "int multiply_one(int value) { return value * 1; }\n";
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
  static const char conversion_source[] =
      "unsigned int cast_value(int value) {\n"
      "  return (unsigned int)value;\n"
      "}\n";
  static const char indirect_call_source[] =
      "int (*indirect_call)(int);\n"
      "int call_pointer(int value) { return indirect_call(value); }\n";
  static const char wide_call_source[] =
      "int wide_target(long long value);\n"
      "int call_wide(void) { return wide_target(1); }\n";
  static const char variadic_call_source[] =
      "int variadic_target(int first, ...);\n"
      "int call_variadic(void) { return variadic_target(1); }\n";
  static const char value_statement_source[] =
      "void discard_value(void) { 1; }\n";
  ctool_host_adapter_t adapter;
  ctool_host_adapter_t limited_adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_job_t *limited_job = NULL;
  ctool_c_translation_unit_t active_unit;
  ctool_c_translation_unit_t addition_unit;
  ctool_c_translation_unit_t simple_unit;
  ctool_c_translation_unit_t statement_unit;
  ctool_c_translation_unit_t expression_unit;
  ctool_c_translation_unit_t abi_unit;
  ctool_c_translation_unit_t inline_success_unit;
  ctool_c_translation_unit_t external_inline_unit;
  ctool_c_translation_unit_t extern_inline_unit;
  ctool_c_translation_unit_t conversion_unit;
  ctool_c_translation_unit_t call_unit;
  ctool_c_translation_unit_t indirect_call_unit;
  ctool_c_translation_unit_t wide_call_unit;
  ctool_c_translation_unit_t variadic_call_unit;
  ctool_c_translation_unit_t value_statement_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_function_definition_t invalid_definition;
  ctool_c_ir_unit_t ir;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  uint64_t fingerprint;
  char *fixture = NULL;
  char *call_fixture = NULL;
  int passed = 0;

  (void)memset(&active_unit, 0, sizeof(active_unit));
  (void)memset(&addition_unit, 0, sizeof(addition_unit));
  (void)memset(&simple_unit, 0, sizeof(simple_unit));
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

  if (!parse_source(job, "/active-cupidc-add2.c", active_addition,
                    &addition_unit)) {
    (void)fprintf(stderr, "active addition setup failed\n");
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

  if (!parse_source(job, "/unsupported-statement.c", statement_source,
                    &statement_unit) ||
      !expect_ir_failure(
          job, &statement_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_STATEMENT,
          "CupidC IR lowering does not yet support this statement",
          "unsupported statement")) {
    goto cleanup;
  }
  if (!parse_source(job, "/unsupported-expression.c", expression_source,
                    &expression_unit) ||
      !expect_ir_failure(
          job, &expression_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_EXPRESSION,
          "CupidC IR lowering does not yet support this expression",
          "unsupported expression")) {
    goto cleanup;
  }
  if (!parse_source(job, "/unsupported-conversion.c", conversion_source,
                    &conversion_unit) ||
      !expect_ir_failure(
          job, &conversion_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_CONVERSION,
          "CupidC IR lowering does not yet support this conversion",
          "unsupported explicit conversion")) {
    goto cleanup;
  }
  if (!parse_source(job, "/indirect-call.c", indirect_call_source,
                    &indirect_call_unit) ||
      !expect_ir_failure(
          job, &indirect_call_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_EXPRESSION,
          "CupidC IR lowering does not yet support this expression",
          "indirect call")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-call.c", wide_call_source,
                    &wide_call_unit) ||
      !expect_ir_failure(
          job, &wide_call_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports only fixed, nonvariadic direct calls "
          "with 32-bit integer arguments and void or 32-bit integer results",
          "wide direct call")) {
    goto cleanup;
  }
  if (!parse_source(job, "/variadic-call.c", variadic_call_source,
                    &variadic_call_unit) ||
      !expect_ir_failure(
          job, &variadic_call_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports only fixed, nonvariadic direct calls "
          "with 32-bit integer arguments and void or 32-bit integer results",
          "variadic direct call")) {
    goto cleanup;
  }
  if (!parse_source(job, "/value-statement.c", value_statement_source,
                    &value_statement_unit) ||
      !expect_ir_failure(
          job, &value_statement_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_STATEMENT,
          "CupidC IR lowering does not yet support this statement",
          "nonvoid expression statement")) {
    goto cleanup;
  }
  if (!parse_source(job, "/unsupported-abi.c", abi_source, &abi_unit) ||
      !expect_ir_failure(
          job, &abi_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports only fixed, nonvariadic cdecl functions "
          "with 32-bit integer parameters and void or 32-bit integer results",
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
  if (limited_job != NULL) {
    ctool_job_close(limited_job);
  }
  if (job != NULL) {
    ctool_job_close(job);
  }
  free(fixture);
  free(call_fixture);
  if (passed != 0) {
    (void)puts("active-leaf: ok");
    return 0;
  }
  return 1;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "active-leaf") == 0) {
    return run_active_leaf(argv[2]);
  }
  (void)fprintf(stderr,
                "usage: cupidc-ir-contract active-leaf HOST_ROOT\n");
  return 2;
}
