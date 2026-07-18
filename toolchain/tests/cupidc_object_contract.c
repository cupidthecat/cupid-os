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

static int parse_source(ctool_job_t *job, const char *path, const char *text,
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
  pp_request.gnu_extensions = CTOOL_FALSE;
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
             job, "/toolchain/cupidld.c", "load active linker source",
             "the active linker selector callback changed",
             active_linker_selector_call, NULL);
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
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0xe8u, 0xfcu,
      0xffu, 0xffu, 0xffu, 0x83u, 0xc4u, 0x04u, 0x50u, 0x58u,
      0xc9u, 0xc3u};
  static const char *const function_names[] = {
      "read_indirect",        "address_member",     "write_indirect",
      "pass_pointer",         "qualify_pointer",    "erase_pointer",
      "restore_pointer",      "copy_pointer",       "set_global_pointer",
      "clear_global_pointer", "read_global_member", "call_pointer_result"};
  static const ctool_u32 function_offsets[] = {
      0u, 21u, 34u, 67u, 84u, 101u, 118u, 135u, 171u, 198u, 219u, 240u};
  static const ctool_u32 function_sizes[] = {
      21u, 13u, 33u, 17u, 17u, 17u, 17u, 36u, 27u, 21u, 21u, 26u};
  static const ctool_u32 relocation_offsets[] = {
      25u, 175u, 202u, 223u, 255u};
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
        relocation->type != relocation_types[index] ||
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
      0x59u, 0x58u, 0x89u, 0x08u, 0xe8u, 0xfcu, 0xffu, 0xffu,
      0xffu, 0x50u, 0x8du, 0x45u, 0xfcu, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x59u, 0x58u, 0x29u, 0xc8u, 0x50u, 0x8du,
      0x85u, 0x08u, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x92u,
      0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u, 0xc0u,
      0x0fu, 0x84u, 0x0au, 0x00u, 0x00u, 0x00u, 0xe8u, 0xfcu,
      0xffu, 0xffu, 0xffu, 0xe9u, 0xc4u, 0xffu, 0xffu, 0xffu,
      0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_CALL,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_CALL,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_SUB,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,   CTOOL_X86_MN_SETB,
      CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,    CTOOL_X86_MN_CALL,
      CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 branch_targets[] = {80u, 20u};
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
      object->relocations[1].offset != 21u ||
      object->relocations[1].symbol_file_index != timer->file_index ||
      object->relocations[1].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[1].addend_known != CTOOL_TRUE ||
      object->relocations[1].addend != -4 ||
      object->relocations[2].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[2].entry_index != 2u ||
      object->relocations[2].target_section_file_index != text->file_index ||
      object->relocations[2].offset != 71u ||
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
      0xfcu, 0x50u, 0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0x50u,
      0x59u, 0x58u, 0x89u, 0x08u, 0x51u, 0x58u, 0x8du, 0x45u,
      0xf8u, 0x50u, 0x8du, 0x45u, 0xfcu, 0x50u, 0x58u, 0x8bu,
      0x00u, 0x50u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0x29u,
      0xc8u, 0x50u, 0x59u, 0x58u, 0x89u, 0x08u, 0x51u, 0x58u,
      0x68u, 0x01u, 0x00u, 0x00u, 0x00u, 0xe8u, 0xfcu, 0xffu,
      0xffu, 0xffu, 0x83u, 0xc4u, 0x04u, 0x8du, 0x45u, 0xf8u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x68u, 0x00u, 0x00u,
      0x00u, 0x00u, 0x59u, 0x58u, 0x39u, 0xc8u, 0x0fu, 0x9eu,
      0xc0u, 0x0fu, 0xb6u, 0xc0u, 0x50u, 0x58u, 0x85u, 0xc0u,
      0x0fu, 0x84u, 0x05u, 0x00u, 0x00u, 0x00u, 0xe9u, 0x9bu,
      0xffu, 0xffu, 0xffu, 0xc9u, 0xc3u};
  static const ctool_x86_mnemonic_t instructions[] = {
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_MOV,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_CALL,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_SUB,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_CALL,  CTOOL_X86_MN_ADD,
      CTOOL_X86_MN_LEA,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,   CTOOL_X86_MN_PUSH,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_POP,   CTOOL_X86_MN_CMP,
      CTOOL_X86_MN_SETLE, CTOOL_X86_MN_MOVZX, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,   CTOOL_X86_MN_TEST,  CTOOL_X86_MN_JE,
      CTOOL_X86_MN_JMP,   CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_u32 branch_targets[] = {107u, 6u};
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
      object->relocations[0].offset != 11u ||
      object->relocations[0].symbol_file_index != time->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != -4 ||
      object->relocations[1].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[1].entry_index != 1u ||
      object->relocations[1].target_section_file_index != text->file_index ||
      object->relocations[1].offset != 62u ||
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
      0x55u, 0x89u, 0xe5u, 0xe8u, 0xfcu, 0xffu,
      0xffu, 0xffu, 0x50u, 0x58u, 0xc9u, 0xc3u};
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
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0xe8u, 0xfcu,
      0xffu, 0xffu, 0xffu, 0x50u, 0x8du, 0x85u, 0x10u, 0x00u,
      0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8bu,
      0x4cu, 0x24u, 0x08u, 0x8bu, 0x14u, 0x24u, 0x89u, 0x54u,
      0x24u, 0x08u, 0x89u, 0x0cu, 0x24u, 0xe8u, 0xfcu, 0xffu,
      0xffu, 0xffu, 0x83u, 0xc4u, 0x0cu, 0x50u, 0x58u, 0xc9u,
      0xc3u};
  static const ctool_u8 call_void_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0xe8u, 0xfcu,
      0xffu, 0xffu, 0xffu, 0x83u, 0xc4u, 0x04u, 0xc9u, 0xc3u};
  static const ctool_u8 add2_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u,
      0x00u, 0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x8du, 0x85u,
      0x0cu, 0x00u, 0x00u, 0x00u, 0x50u, 0x58u, 0x8bu, 0x00u,
      0x50u, 0x59u, 0x58u, 0x01u, 0xc8u, 0x50u, 0x58u, 0xc9u,
      0xc3u};
  static const ctool_u8 local_round_trip_bytes[] = {
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x08u, 0x8du, 0x45u,
      0xfcu, 0x50u, 0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0x50u,
      0x59u, 0x58u, 0x89u, 0x08u, 0x8du, 0x45u, 0xf8u, 0x50u,
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
      0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0x83u, 0xc4u, 0x08u,
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
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_CALL,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP, CTOOL_X86_MN_LEAVE,
      CTOOL_X86_MN_RET};
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
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_CALL, CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,  CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_CALL, CTOOL_X86_MN_ADD,  CTOOL_X86_MN_PUSH,
      CTOOL_X86_MN_POP,  CTOOL_X86_MN_LEAVE, CTOOL_X86_MN_RET};
  static const ctool_x86_mnemonic_t call_void_instructions[] = {
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_MOV, CTOOL_X86_MN_LEA,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP, CTOOL_X86_MN_MOV,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_CALL, CTOOL_X86_MN_ADD,
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
      CTOOL_X86_MN_LEA,  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_CALL,
      CTOOL_X86_MN_PUSH, CTOOL_X86_MN_POP,  CTOOL_X86_MN_POP,
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
      CTOOL_X86_MN_CALL, CTOOL_X86_MN_ADD,  CTOOL_X86_MN_PUSH,
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
                      12u) ||
      !symbol_matches(call_external, call_external->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 151u,
                      51u) ||
      !symbol_matches(call_nested, call_nested->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 202u,
                      57u) ||
      !symbol_matches(call_void, call_void->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 259u,
                      24u) ||
      !symbol_matches(add2, add2->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                       CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 283u,
                       33u) ||
      !symbol_matches(local_round_trip, local_round_trip->file_index,
                      CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                       CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 316u,
                       63u) ||
      !symbol_matches(local_call, local_call->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 379u,
                      64u) ||
      !symbol_matches(uninitialized_local, uninitialized_local->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 443u,
                      17u) ||
      !symbol_matches(vga_flip_ready, vga_flip_ready->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 460u,
                      61u) ||
      !symbol_matches(read_external_clock, read_external_clock->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 521u,
                      15u) ||
      !symbol_matches(signed_greater_equal,
                      signed_greater_equal->file_index,
                      CTOOL_ELF32_BIND_GLOBAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 536u,
                      39u) ||
      !symbol_matches(power_of_two, power_of_two->file_index,
                      CTOOL_ELF32_BIND_LOCAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 575u,
                      (ctool_u32)sizeof(power_of_two_bytes)) ||
      !symbol_matches(bool_valid, bool_valid->file_index,
                      CTOOL_ELF32_BIND_LOCAL, CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 718u,
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
      bool_valid->size == 0u || text->contents.size != 845u ||
      object->relocations[0].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[0].entry_index != 0u ||
      object->relocations[0].target_section_file_index != text->file_index ||
      object->relocations[0].offset != 143u ||
      object->relocations[0].symbol_file_index != local_target->file_index ||
      object->relocations[0].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[0].addend_known != CTOOL_TRUE ||
      object->relocations[0].addend != -4 ||
      object->relocations[1].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[1].entry_index != 1u ||
      object->relocations[1].target_section_file_index != text->file_index ||
      object->relocations[1].offset != 191u ||
      object->relocations[1].symbol_file_index != external_sum->file_index ||
      object->relocations[1].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[1].addend_known != CTOOL_TRUE ||
      object->relocations[1].addend != -4 ||
      object->relocations[2].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[2].entry_index != 2u ||
      object->relocations[2].target_section_file_index != text->file_index ||
      object->relocations[2].offset != 217u ||
      object->relocations[2].symbol_file_index != call_local->file_index ||
      object->relocations[2].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[2].addend_known != CTOOL_TRUE ||
      object->relocations[2].addend != -4 ||
      object->relocations[3].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[3].entry_index != 3u ||
      object->relocations[3].target_section_file_index != text->file_index ||
      object->relocations[3].offset != 248u ||
      object->relocations[3].symbol_file_index != external_three->file_index ||
      object->relocations[3].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[3].addend_known != CTOOL_TRUE ||
      object->relocations[3].addend != -4 ||
      object->relocations[4].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[4].entry_index != 4u ||
      object->relocations[4].target_section_file_index != text->file_index ||
      object->relocations[4].offset != 274u ||
      object->relocations[4].symbol_file_index != external_sink->file_index ||
      object->relocations[4].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[4].addend_known != CTOOL_TRUE ||
      object->relocations[4].addend != -4 ||
      object->relocations[5].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[5].entry_index != 5u ||
      object->relocations[5].target_section_file_index != text->file_index ||
      object->relocations[5].offset != 327u ||
      object->relocations[5].symbol_file_index !=
          timer_get_uptime_ms->file_index ||
      object->relocations[5].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[5].addend_known != CTOOL_TRUE ||
      object->relocations[5].addend != -4 ||
      object->relocations[6].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[6].entry_index != 6u ||
      object->relocations[6].target_section_file_index != text->file_index ||
      object->relocations[6].offset != 420u ||
      object->relocations[6].symbol_file_index != initializer_sum->file_index ||
      object->relocations[6].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[6].addend_known != CTOOL_TRUE ||
      object->relocations[6].addend != -4 ||
      object->relocations[7].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[7].entry_index != 7u ||
      object->relocations[7].target_section_file_index != text->file_index ||
      object->relocations[7].offset != 471u ||
      object->relocations[7].symbol_file_index !=
          timer_get_uptime_ms->file_index ||
      object->relocations[7].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[7].addend_known != CTOOL_TRUE ||
      object->relocations[7].addend != -4 ||
      object->relocations[8].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[8].entry_index != 8u ||
      object->relocations[8].target_section_file_index != text->file_index ||
      object->relocations[8].offset != 489u ||
      object->relocations[8].symbol_file_index != last_flip_ms->file_index ||
      object->relocations[8].type != CTOOL_ELF32_R_386_32 ||
      object->relocations[8].addend_known != CTOOL_TRUE ||
      object->relocations[8].addend != 0 ||
      object->relocations[9].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[9].entry_index != 9u ||
      object->relocations[9].target_section_file_index != text->file_index ||
      object->relocations[9].offset != 525u ||
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
      "struct unsupported_bits { unsigned int value : 3; };\n"
      "static struct unsupported_bits unsupported_state;\n"
      "unsigned int unsupported(void) {\n"
      "  return ++unsupported_state.value;\n"
      "}\n";
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
      "void terminal_for_wide(void) { for (; 1; 1LL) return; }\n";
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
      "  for (long long i = 0; i < 1; i = i + 1) {}\n"
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
  static const char wide_cast_text[] =
      "int wide_cast(int value) { return (long long)value; }\n";
  static const char void_cast_text[] =
      "extern void sink(void);\n"
      "void discard_sink(void) { (void)sink(); }\n";
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
  ctool_c_translation_unit_t wide_cast_unit;
  ctool_c_translation_unit_t void_cast_unit;
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
  (void)memset(&wide_cast_unit, 0, sizeof(wide_cast_unit));
  (void)memset(&void_cast_unit, 0, sizeof(void_cast_unit));
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
      !expect_object_failure(
          job, &unsupported_function_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_EXPRESSION,
          "CupidC IR lowering does not yet support this expression",
          "unsupported function expression") ||
      !parse_source(job, "/wide-selection.c", wide_selection_text,
                    &wide_selection_unit) ||
      !expect_object_failure(
          job, &wide_selection_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide selection condition object") ||
      !parse_source(job, "/wide-while.c", wide_while_text,
                    &wide_while_unit) ||
      !expect_object_failure(
          job, &wide_while_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide while condition object") ||
      !parse_source(job, "/wide-do.c", wide_do_text, &wide_do_unit) ||
      !expect_object_failure(
          job, &wide_do_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide do condition object") ||
      !parse_source(job, "/wide-for.c", wide_for_text, &wide_for_unit) ||
      !expect_object_failure_preserves_unit(
          job, &wide_for_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide for condition object") ||
      !parse_source(job, "/terminal-wide-for-iteration.c",
                    terminal_wide_for_iteration_text,
                    &terminal_wide_for_iteration_unit) ||
      !expect_object_failure_preserves_unit(
          job, &terminal_wide_for_iteration_unit, second,
          CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "unreachable wide for iteration object") ||
      !parse_source(job, "/wide-declaration-for.c",
                    wide_declaration_for_text,
                    &wide_declaration_for_unit) ||
      !expect_object_failure_preserves_unit(
          job, &wide_declaration_for_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide declaration for initializer object") ||
      !parse_source(job, "/unreachable-wide-declaration.c",
                    unreachable_wide_declaration_text,
                    &unreachable_wide_declaration_unit) ||
      !expect_object_failure_preserves_unit(
          job, &unreachable_wide_declaration_unit, second,
          CTOOL_ERR_UNSUPPORTED, CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "unreachable wide declaration object") ||
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
      !expect_object_failure(
          job, &wide_logical_not_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide logical-not object") ||
      !parse_source(job, "/wide-cast.c", wide_cast_text,
                    &wide_cast_unit) ||
      !expect_object_failure(
          job, &wide_cast_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide integer cast object") ||
      !parse_source(job, "/void-cast.c", void_cast_text,
                    &void_cast_unit) ||
      !expect_object_failure(
          job, &void_cast_unit, second, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "void cast of void call object") ||
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
      0x24u, 0x08u, 0x89u, 0x0cu, 0x24u, 0x8bu, 0x44u, 0x24u,
      0x0cu, 0xffu, 0xd0u, 0x83u, 0xc4u, 0x10u, 0x50u, 0x58u,
      0xc9u, 0xc3u};
  static const ctool_u32 relocation_offsets[] = {
      42u, 53u, 234u, 253u, 304u, 424u, 429u, 440u, 466u, 0u};
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
      object->symbol_count != 17u || text->contents.size != 477u ||
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
                      134u, 74u) ||
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
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 208u,
                      56u) ||
      !symbol_matches(select, select->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 264u,
                      47u) ||
      !symbol_matches(equal, equal->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 311u,
                      39u) ||
      !symbol_matches(missing, missing->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 350u,
                      33u) ||
      !symbol_matches(present, present->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 383u,
                      37u) ||
      !symbol_matches(install, install->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 420u,
                      31u) ||
      !symbol_matches(forward, forward->file_index, CTOOL_ELF32_BIND_GLOBAL,
                      CTOOL_ELF32_SYMBOL_FUNCTION,
                      CTOOL_ELF32_SYMBOL_DEFINED, text->file_index, 451u,
                      26u)) {
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
  static const char variadic_source[] =
      "typedef int (*variadic_callback_t)(int, ...);\n"
      "int call_variadic(variadic_callback_t callback) {\n"
      "  return callback(1);\n"
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
  ctool_c_translation_unit_t variadic_unit;
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
  (void)memset(&variadic_unit, 0, sizeof(variadic_unit));
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
      !parse_source(job, "/variadic-function-pointer-object.c",
                    variadic_source, &variadic_unit) ||
      !expect_object_failure_preserves_unit(
          job, &variadic_unit, output, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports only fixed, nonvariadic calls with "
          "one-byte, two-byte, or four-byte scalar arguments and void or "
          "represented scalar results",
          "variadic function pointer object")) {
    goto cleanup;
  }
  if (!parse_source(job, "/atomic-function-pointer-object.c", atomic_source,
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
      0x54u, 0x24u, 0x04u, 0x89u, 0x0cu, 0x24u, 0xe8u, 0xfcu,
      0xffu, 0xffu, 0xffu, 0x83u, 0xc4u, 0x08u, 0xc9u, 0xc3u,
      0x55u, 0x89u, 0xe5u, 0x83u, 0xecu, 0x0cu, 0x8du, 0x45u,
      0xf4u, 0x50u, 0x8du, 0x85u, 0x08u, 0x00u, 0x00u, 0x00u,
      0x50u, 0x58u, 0x8bu, 0x00u, 0x50u, 0x59u, 0x58u, 0xbau,
      0x04u, 0x00u, 0x00u, 0x00u, 0x0fu, 0xafu, 0xcau, 0x01u,
      0xc8u, 0x50u, 0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0x83u,
      0xc4u, 0x04u, 0xc9u, 0xc3u};
  static const char *const function_names[] = {
      "automatic_array", "automatic_record", "automatic_bytes",
      "automatic_mixed", "automatic_children"};
  static const ctool_u32 function_offsets[] = {0u, 86u, 134u, 154u,
                                                192u};
  static const ctool_u32 function_sizes[] = {86u, 48u, 20u, 38u, 44u};
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
      object->relocations[1].offset != 183u ||
      object->relocations[1].symbol_file_index != consume_layout->file_index ||
      object->relocations[1].type != CTOOL_ELF32_R_386_PC32 ||
      object->relocations[1].addend_known != CTOOL_TRUE ||
      object->relocations[1].addend != -4 ||
      object->relocations[2].relocation_section_file_index !=
          rel_text->file_index ||
      object->relocations[2].entry_index != 2u ||
      object->relocations[2].target_section_file_index != text->file_index ||
      object->relocations[2].offset != 227u ||
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
  static const char initialized_source[] =
      "typedef unsigned int ctool_u32;\n"
      "ctool_u32 initialized_array(void) {\n"
      "  ctool_u32 values[2] = {1u, 2u};\n"
      "  return 0u;\n"
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
  ctool_c_translation_unit_t initialized_unit;
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
  (void)memset(&initialized_unit, 0, sizeof(initialized_unit));
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
      !parse_source(job, "/initialized-automatic-array-object.c",
                    initialized_source, &initialized_unit) ||
      !expect_object_failure_preserves_unit(
          job, &initialized_unit, output, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "initialized automatic array object") ||
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
      0x83u, 0xc4u, 0x04u, 0x0fu, 0xb7u, 0xc0u, 0x50u};
  static const ctool_u8 indirect_u16_call_tail[] = {
      0x83u, 0xc4u, 0x08u, 0x0fu, 0xb7u, 0xc0u, 0x50u};
  static const ctool_u8 direct_bool_call_tail[] = {
      0x83u, 0xc4u, 0x04u, 0x0fu, 0xb6u, 0xc0u, 0x50u};
  static const ctool_u8 indirect_bool_call_tail[] = {
      0x83u, 0xc4u, 0x08u, 0x0fu, 0xb6u, 0xc0u, 0x50u};
  static const ctool_u8 direct_i8_call_tail[] = {
      0x83u, 0xc4u, 0x04u, 0x0fu, 0xbeu, 0xc0u, 0x50u};
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
#define NARROW_ORACLE_ESP 4u
#define NARROW_ORACLE_EBP 5u

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
  } else if (instruction->mnemonic == CTOOL_X86_MN_AND) {
    left_value &= right_value;
  } else if (instruction->mnemonic == CTOOL_X86_MN_XOR) {
    left_value ^= right_value;
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
  if (argc == 3 && strcmp(argv[1], "narrow-mutations") == 0) {
    return run_narrow_mutation_object(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "narrow-values") == 0) {
    return run_narrow_value_object(argv[2]);
  }
  (void)fprintf(stderr,
                "usage: cupidc-object-contract "
                "static-data|direct-goto|switch-object|integer-mutation|"
                "pointer-values|pointer-comparisons|pointer-conditions|"
                "pointer-arithmetic|function-pointers|automatic-objects|"
                "narrow-mutations|narrow-values "
                "HOST_ROOT\n");
  return 2;
}
