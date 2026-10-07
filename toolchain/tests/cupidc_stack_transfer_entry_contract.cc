/* Keep a valid IR branch entering the consumer with two live operands. */
#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif
#define ctool_c_lower_ir stack_test_lower_ir
#include "../cupidc_emit.cc"
#undef ctool_c_lower_ir

#include "ctool_host.h"
#include <stdio.h>
#include <string.h>

ctool_status_t ctool_c_lower_ir(ctool_job_t *job,
    const ctool_c_translation_unit_t *unit, ctool_c_ir_unit_t *result_out);

static int result_entry;
static int conversion_entry;

ctool_status_t stack_test_lower_ir(ctool_job_t *job,
    const ctool_c_translation_unit_t *unit, ctool_c_ir_unit_t *result_out) {
  ctool_c_ir_function_t *functions;
  ctool_c_ir_instruction_t *instructions;
  ctool_u32 index;
  ctool_u32 prefix = conversion_entry ? 2u : result_entry ? 5u : 4u;
  ctool_u32 inserted = result_entry ? 3u : 0u;
  ctool_status_t status = ctool_c_lower_ir(job, unit, result_out);
  if (status != CTOOL_OK) return status;
  if (result_out->function_count != 1u || result_out->instruction_count != prefix + 2u ||
      result_out->instructions[prefix - 1u].kind !=
          (result_entry ? CTOOL_C_IR_INSTRUCTION_BINARY : CTOOL_C_IR_INSTRUCTION_LOAD) ||
      result_out->instructions[prefix].kind !=
          (conversion_entry ? CTOOL_C_IR_INSTRUCTION_CONVERT :
           result_entry ? CTOOL_C_IR_INSTRUCTION_UNARY : CTOOL_C_IR_INSTRUCTION_BINARY))
    return CTOOL_ERR_INTERNAL;
  status = ctool_arena_alloc(ctool_job_arena(job), (ctool_u32)sizeof(*functions),
                            4u, (void **)&functions);
  if (status == CTOOL_OK)
    status = ctool_arena_alloc(ctool_job_arena(job),
        (prefix * 2u + 3u + inserted) * (ctool_u32)sizeof(*instructions), 4u, (void **)&instructions);
  if (status != CTOOL_OK) return status;
  functions[0] = result_out->functions[0];
  for (index = 0u; index < prefix; index++) {
    instructions[index] = result_out->instructions[index];
    instructions[index + prefix + 1u + inserted] = result_out->instructions[index];
  }
  if (result_entry) {
    instructions[prefix] = result_out->instructions[0];
    instructions[prefix + 1u] = result_out->instructions[1];
    (void)memset(&instructions[prefix + 2u], 0, sizeof(instructions[prefix + 2u]));
    instructions[prefix + 2u].kind = CTOOL_C_IR_INSTRUCTION_DISCARD;
    instructions[prefix + 2u].type = CTOOL_C_TYPE_NONE;
    instructions[prefix + 2u].input_type = result_out->instructions[prefix - 1u].type;
    instructions[prefix + 2u].reference = CTOOL_C_AST_NONE;
    instructions[prefix + 2u].operation = CTOOL_C_EXPRESSION_OPERATOR_NONE;
    instructions[prefix + 2u].conversion = CTOOL_C_CONVERSION_NONE;
    instructions[prefix + 2u].first_argument_type = CTOOL_C_AST_NONE;
  }
  (void)memset(&instructions[prefix + inserted], 0, sizeof(instructions[prefix + inserted]));
  instructions[prefix + inserted].kind = CTOOL_C_IR_INSTRUCTION_JUMP;
  instructions[prefix + inserted].type = CTOOL_C_TYPE_NONE;
  instructions[prefix + inserted].input_type = CTOOL_C_TYPE_NONE;
  instructions[prefix + inserted].first_argument_type = CTOOL_C_AST_NONE;
  instructions[prefix + inserted].reference = prefix * 2u + 1u + inserted;
  instructions[prefix * 2u + 1u + inserted] = result_out->instructions[prefix];
  instructions[prefix * 2u + 2u + inserted] = result_out->instructions[prefix + 1u];
  functions[0].first_instruction = 0u;
  functions[0].instruction_count = prefix * 2u + 3u + inserted;
  result_out->functions = functions;
  result_out->instructions = instructions;
  result_out->instruction_count = prefix * 2u + 3u + inserted;
  return CTOOL_OK;
}

int main(int argc, char **argv) {
  static const char text[] =
      "unsigned int entry_guard(unsigned int left, unsigned int right) {"
      " return left ^ right; }";
  static const char result_text[] =
      "unsigned int entry_guard(unsigned int left, unsigned int right) {"
      " return ~(left ^ right); }";
  static const char conversion_text[] =
      "int entry_guard(enum { VALUE } left, unsigned int right) {"
      " return left; }";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_buffer_t *output;
  ctool_source_t source;
  ctool_c_pp_request_t pp;
  ctool_c_pp_result_t tape;
  ctool_c_parse_request_t parse;
  ctool_c_translation_unit_t unit;
  ctool_bytes_t object;
  FILE *file;
  int okay;
  if ((argc != 3 && argc != 4) ||
      (argc == 4 && strcmp(argv[3], "result") != 0 &&
       strcmp(argv[3], "conversion") != 0) ||
      ctool_host_adapter_init(&adapter, argv[1]) != CTOOL_OK) return 1;
  result_entry = argc == 4 && strcmp(argv[3], "result") == 0;
  conversion_entry = argc == 4 && strcmp(argv[3], "conversion") == 0;
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  if (ctool_job_open(&config, &job) != CTOOL_OK) return 2;
  source.path.text = ctool_string("/entry-guard.cc");
  source.contents = conversion_entry
      ? ctool_bytes(conversion_text, (ctool_u32)(sizeof(conversion_text) - 1u))
      : result_entry
      ? ctool_bytes(result_text, (ctool_u32)(sizeof(result_text) - 1u))
      : ctool_bytes(text, (ctool_u32)(sizeof(text) - 1u));
  (void)memset(&pp, 0, sizeof(pp));
  pp.mode = CTOOL_C_PP_MODE_C11;
  (void)memset(&parse, 0, sizeof(parse));
  parse.mode = CTOOL_C_PP_MODE_C11;
  if (ctool_c_preprocess(job, &source, &pp, &tape) != CTOOL_OK ||
      ctool_c_parse(job, &tape, &parse, &unit) != CTOOL_OK ||
      ctool_job_open_buffer(job, 256u, config.limits.output_bytes, &output) != CTOOL_OK ||
      ctool_c_emit_object(job, &unit, output) != CTOOL_OK) {
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 3;
  }
  object = ctool_buffer_view(output);
  file = fopen(argv[2], "wb");
  if (file == NULL) { ctool_job_close(job); return 4; }
  okay = fwrite(object.data, 1u, object.size, file) == object.size;
  if (fclose(file) != 0) okay = 0;
  ctool_job_close(job);
  return okay ? 0 : 5;
}
