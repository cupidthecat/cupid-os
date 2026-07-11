#include "ctool.h"
#include "ctool_host.h"
#include "cupidc_pp.h"

#include <stdio.h>
#include <string.h>

static int string_equal(ctool_string_t actual, const char *expected) {
  size_t expected_size = strlen(expected);
  return actual.size == (ctool_u32)expected_size &&
                 (expected_size == 0u ||
                  memcmp(actual.data, expected, expected_size) == 0)
             ? 1
             : 0;
}

static int string_starts_with(ctool_string_t actual, const char *prefix) {
  size_t prefix_size = strlen(prefix);
  return actual.size >= (ctool_u32)prefix_size &&
                 (prefix_size == 0u ||
                  memcmp(actual.data, prefix, prefix_size) == 0)
             ? 1
             : 0;
}

static ctool_u32 text_line_of(const char *text, const char *needle) {
  const char *found = strstr(text, needle);
  ctool_u32 line = 1u;
  if (found == NULL) {
    return 0u;
  }
  while (text != found) {
    if (*text == '\n') {
      line++;
    }
    text++;
  }
  return line;
}

static int arena_marks_equal(ctool_arena_mark_t left,
                             ctool_arena_mark_t right) {
  return left.owner == right.owner && left.block == right.block &&
                 left.used == right.used && left.generation == right.generation
             ? 1
             : 0;
}

static int open_job_at_root(const char *mode, const char *host_root,
                            ctool_host_adapter_t *adapter,
                            ctool_job_t **job_out) {
  ctool_job_config_t config;
  ctool_status_t status = ctool_host_adapter_init(adapter, host_root);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: host adapter: %s\n", mode,
                  ctool_status_name(status));
    return 1;
  }
  config = ctool_host_job_config(adapter, ctool_default_limits());
  status = ctool_job_open(&config, job_out);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: job open: %s\n", mode,
                  ctool_status_name(status));
    return 1;
  }
  return 0;
}

static int open_job(const char *mode, ctool_host_adapter_t *adapter,
                    ctool_job_t **job_out) {
  return open_job_at_root(mode, ".", adapter, job_out);
}

static int append_character(char *buffer, ctool_u32 capacity,
                            ctool_u32 *size, char value) {
  if (*size == capacity) {
    return 1;
  }
  buffer[*size] = value;
  (*size)++;
  return 0;
}

static int append_text(char *buffer, ctool_u32 capacity,
                       ctool_u32 *size, const char *text) {
  while (*text != '\0') {
    if (append_character(buffer, capacity, size, *text) != 0) {
      return 1;
    }
    text++;
  }
  return 0;
}

static int append_decimal(char *buffer, ctool_u32 capacity,
                          ctool_u32 *size, ctool_u32 value) {
  char digits[10];
  ctool_u32 count = 0u;
  do {
    digits[count] = (char)('0' + (char)(value % 10u));
    count++;
    value /= 10u;
  } while (value != 0u);
  while (count != 0u) {
    count--;
    if (append_character(buffer, capacity, size, digits[count]) != 0) {
      return 1;
    }
  }
  return 0;
}

static int append_parameter_name(char *buffer, ctool_u32 capacity,
                                 ctool_u32 *size, ctool_u32 index) {
  return append_character(buffer, capacity, size, 'p') != 0 ||
                 append_character(buffer, capacity, size,
                                  (char)('0' + (char)(index / 100u))) != 0 ||
                 append_character(
                     buffer, capacity, size,
                     (char)('0' + (char)((index / 10u) % 10u))) != 0 ||
                 append_character(buffer, capacity, size,
                                  (char)('0' + (char)(index % 10u))) != 0
             ? 1
             : 0;
}

typedef struct {
  ctool_string_t path;
  ctool_bytes_t contents;
} fixture_file_t;

typedef struct {
  const fixture_file_t *files;
  ctool_u32 file_count;
  ctool_u32 read_count;
} fixture_store_t;

static ctool_bool strings_equal(ctool_string_t left, ctool_string_t right) {
  return left.size == right.size &&
                 (left.size == 0u ||
                  memcmp(left.data, right.data, (size_t)left.size) == 0)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t fixture_file_size(void *context,
                                        ctool_string_t logical_path,
                                        ctool_u32 *size_out) {
  fixture_store_t *store = (fixture_store_t *)context;
  ctool_u32 index;
  for (index = 0u; index < store->file_count; index++) {
    if (strings_equal(store->files[index].path, logical_path) == CTOOL_TRUE) {
      *size_out = store->files[index].contents.size;
      return CTOOL_OK;
    }
  }
  return CTOOL_ERR_NOT_FOUND;
}

static ctool_status_t fixture_read_exact(void *context,
                                         ctool_string_t logical_path,
                                         ctool_u8 *destination,
                                         ctool_u32 size) {
  fixture_store_t *store = (fixture_store_t *)context;
  ctool_u32 index;
  for (index = 0u; index < store->file_count; index++) {
    if (strings_equal(store->files[index].path, logical_path) == CTOOL_TRUE) {
      if (size != store->files[index].contents.size) {
        return CTOOL_ERR_IO;
      }
      if (size != 0u) {
        (void)memcpy(destination, store->files[index].contents.data,
                     (size_t)size);
      }
      store->read_count++;
      return CTOOL_OK;
    }
  }
  return CTOOL_ERR_NOT_FOUND;
}

static ctool_status_t fixture_write_all(void *context,
                                        ctool_string_t logical_path,
                                        ctool_bytes_t contents) {
  (void)context;
  (void)logical_path;
  (void)contents;
  return CTOOL_ERR_UNSUPPORTED;
}

static int open_fixture_job(const char *mode, fixture_store_t *store,
                            ctool_host_adapter_t *adapter,
                            ctool_job_t **job_out) {
  ctool_job_config_t config;
  ctool_status_t status = ctool_host_adapter_init(adapter, ".");
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: host adapter: %s\n", mode,
                  ctool_status_name(status));
    return 1;
  }
  config = ctool_host_job_config(adapter, ctool_default_limits());
  config.files.context = store;
  config.files.file_size = fixture_file_size;
  config.files.read_exact = fixture_read_exact;
  config.files.write_all = fixture_write_all;
  status = ctool_job_open(&config, job_out);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: job open: %s\n", mode,
                  ctool_status_name(status));
    return 1;
  }
  return 0;
}

static int run_phases(void) {
  static const char source_text[] =
      "int joined = 1 + \\\n"
      "2; // removed comment\n"
      "const char *s = \"/*literal*/\";\n"
      "name/**/suffix\n";
  static const char *const expected_text[] = {
      "int", "joined", "=", "1", "+", "2", ";", "const",
      "char", "*",      "s", "=", "\"/*literal*/\"", ";",
      "name", "suffix"};
  static const ctool_c_pp_token_kind_t expected_kind[] = {
      CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_IDENTIFIER,
      CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_NUMBER,
      CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_NUMBER,
      CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_IDENTIFIER,
      CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_STRING,     CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_IDENTIFIER};
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;

  if (open_job("phases", &adapter, &job) != 0) {
    return 1;
  }

  source.path.text = ctool_string("/phase.c");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  (void)memset(&result, 0xa5, sizeof(result));

  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK ||
      result.token_count !=
          (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
    (void)fprintf(stderr, "phases: preprocessing failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  for (index = 0u; index < result.token_count; index++) {
    if (result.tokens[index].kind != expected_kind[index] ||
        !string_equal(result.tokens[index].spelling, expected_text[index]) ||
        !string_equal(result.tokens[index].location.path, "/phase.c") ||
        result.tokens[index].pack_alignment != 0u) {
      (void)fprintf(stderr, "phases: token %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }

  if (result.tokens[5].location.line != 2u ||
      result.tokens[5].location.column != 1u ||
      result.tokens[14].location.line != 4u ||
      result.tokens[14].location.column != 1u ||
      result.tokens[15].location.line != 4u ||
      result.tokens[15].location.column != 9u) {
    (void)fprintf(stderr, "phases: source locations differ\n");
    ctool_job_close(job);
    return 1;
  }

  ctool_job_close(job);
  (void)printf("phases: ok\n");
  return 0;
}

static int run_tokens(void) {
  static char source_text[] =
      "L'a' u8\"wide\" .5e+2 0x1p-2 >>= <<= ... <: :> <% %> -> "
      "hash # ## %: %:%:\n";
  static char source_path[] = "/tokens.c";
  static const char *const expected_text[] = {
      "L'a'", "u8\"wide\"", ".5e+2", "0x1p-2", ">>=", "<<=",
      "...",  "<:",         ":>",    "<%",     "%>",  "->",
      "hash", "#",          "##",    "%:",     "%:%:"};
  static const ctool_c_pp_token_kind_t expected_kind[] = {
      CTOOL_C_PP_TOKEN_CHARACTER,  CTOOL_C_PP_TOKEN_STRING,
      CTOOL_C_PP_TOKEN_NUMBER,     CTOOL_C_PP_TOKEN_NUMBER,
      CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_PUNCTUATOR};
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;

  if (open_job("tokens", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text.data = source_path;
  source.path.text.size = (ctool_u32)(sizeof(source_path) - 1u);
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;

  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK ||
      result.token_count !=
          (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
    (void)fprintf(stderr, "tokens: preprocessing failed\n");
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (result.tokens[index].kind != expected_kind[index] ||
        !string_equal(result.tokens[index].spelling, expected_text[index])) {
      (void)fprintf(stderr, "tokens: token %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }
  source_text[0] = 'X';
  source_path[1] = 'X';
  if (!string_equal(result.tokens[0].spelling, "L'a'") ||
      !string_equal(result.tokens[0].location.path, "/tokens.c")) {
    (void)fprintf(stderr, "tokens: result retained a borrowed source view\n");
    ctool_job_close(job);
    return 1;
  }
  source_text[0] = 'L';
  source_path[1] = 't';

  source.path.text = ctool_string("/empty.c");
  source.contents = ctool_bytes(NULL, 0u);
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.tokens != NULL ||
      result.token_count != 0u) {
    (void)fprintf(stderr, "tokens: empty input differs\n");
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("tokens: ok\n");
  return 0;
}

static int run_errors(void) {
  static const char invalid_text[] = "int retained;\n\"unterminated\n";
  static const char valid_text[] = "joined\\\r\n_name\n";
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;

  if (open_job("errors", &adapter, &job) != 0) {
    return 1;
  }

  source.path.text = ctool_string("/invalid.c");
  source.contents =
      ctool_bytes(invalid_text, (ctool_u32)(sizeof(invalid_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  (void)memset(&result, 0xa5, sizeof(result));
  mark = ctool_arena_mark(ctool_job_arena(job));

  status = ctool_c_preprocess(job, &source, &request, &result);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (status != CTOOL_ERR_INPUT || result.tokens != NULL ||
      result.token_count != 0u || ctool_job_diagnostic_count(job) != 1u ||
      diagnostic == NULL || diagnostic->severity != CTOOL_DIAG_ERROR ||
      diagnostic->code != CTOOL_C_PP_DIAG_LEXICAL ||
      !string_equal(diagnostic->path, "/invalid.c") ||
      diagnostic->line != 2u || diagnostic->column != 1u ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr, "errors: lexical failure was not transactional\n");
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/valid.c");
  source.contents =
      ctool_bytes(valid_text, (ctool_u32)(sizeof(valid_text) - 1u));
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 1u ||
      result.tokens == NULL ||
      !string_equal(result.tokens[0].spelling, "joined_name") ||
      result.tokens[0].location.line != 1u ||
      result.tokens[0].location.column != 1u ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr, "errors: recovery preprocessing failed\n");
    ctool_job_close(job);
    return 1;
  }

  ctool_job_close(job);
  (void)printf("errors: ok\n");
  return 0;
}

static int run_unsupported(void) {
  static const char *const unsupported_text[] = {
      "#pragma cupid_vendor_extension\n",
      "#define HEADER \"x.h\"\n#include HEADER\n",
      "#line 77 \"virtual.c\"\n"};
  static const ctool_u32 unsupported_code[] = {
      CTOOL_C_PP_DIAG_DIRECTIVE, CTOOL_C_PP_DIAG_INCLUDE_PATH,
      CTOOL_C_PP_DIAG_DIRECTIVE};
  static const ctool_u32 unsupported_line[] = {1u, 2u, 1u};
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 index;

  if (open_job("unsupported", &adapter, &job) != 0) {
    return 1;
  }
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  source.path.text = ctool_string("/builtin.c");
  for (index = 0u;
       index <
       (ctool_u32)(sizeof(unsupported_text) / sizeof(unsupported_text[0]));
       index++) {
    source.contents =
        ctool_bytes(unsupported_text[index],
                    (ctool_u32)strlen(unsupported_text[index]));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, index);
    if (status != CTOOL_ERR_UNSUPPORTED || result.tokens != NULL ||
        result.token_count != 0u || diagnostic == NULL ||
        diagnostic->code != unsupported_code[index] ||
        diagnostic->line != unsupported_line[index] ||
        diagnostic->column != 1u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr,
                    "unsupported: table case %u did not fail explicitly\n",
                    index);
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_job_close(job);
  (void)printf("unsupported: ok\n");
  return 0;
}

static int run_conditional_expressions(void) {
  static const char source_text[] =
      "ordinary_defined defined follower\n"
      "#define ONE 1\n"
      "#define VALUE (ONE + 1)\n"
      "#define TWICE(value) ((value) * 2)\n"
      "#define ALIAS ONE\n"
      "#define IGNORE(value) 1\n"
      "#if defined(ONE) && defined ALIAS && !defined(MISSING)\n"
      "defined_ok\n"
      "#endif\n"
      "#if IGNORE(defined())\n"
      "unused_defined_ok\n"
      "#endif\n"
      "#if UNKNOWN == 0 && VALUE == 2 && TWICE(3) == 6\n"
      "macro_ok\n"
      "#endif\n"
      "#if 1 + 2 * 3 == 7 && 20 / 4 == 5 && 20 % 6 == 2\n"
      "arithmetic_ok\n"
      "#endif\n"
      "#if (1 << 5) == 32 && (32 >> 3) == 4\n"
      "shifts_ok\n"
      "#endif\n"
      "#if 3 < 4 && 4 <= 4 && 5 > 4 && 5 >= 5 && 6 != 7\n"
      "relations_ok\n"
      "#endif\n"
      "#if ((0xf0U & 0x0fU) == 0U) && ((1U | 2U) == 3U) && "
      "((7U ^ 3U) == 4U) && (~0U != 0U)\n"
      "bitwise_ok\n"
      "#endif\n"
      "#if +1 == 1 && -1 < 0 && !0 && !!1 && -1 > 0U\n"
      "unary_ok\n"
      "#endif\n"
      "#if (0 && (1 / 0)) == 0 && (1 || (1 / 0)) == 1 && "
      "(0 ? (1 / 0) : 9) == 9 && (1 ? 7 : (1 / 0)) == 7\n"
      "short_circuit_ok\n"
      "#endif\n"
      "#if 010 == 8 && 0x10UL == 16 && '\\n' == 10 && "
      "'\\x41' == 'A' && '\\101' == 'A'\n"
      "literals_ok\n"
      "#endif\n"
      "#if U'\\x110000' == 0x110000 && U'\\xffffffff' == 0xffffffffU && "
      "'\\u0024' == '$' && '\\u0040' == '@' && '\\u0060' == '`'\n"
      "character_ranges_ok\n"
      "#endif\n"
      "#if L'\\x80000000' < 0 && L'\\xffffffff' == -1\n"
      "wide_ranges_ok\n"
      "#endif\n"
      "#if (0 && (1, 2)) == 0 && (1 || (1, 2)) == 1 && "
      "(0 ? (1, 2) : 3) == 3 && (1 ? -1 : (0, 0U)) > 0\n"
      "unevaluated_comma_ok\n"
      "#endif\n"
      "#if (0 ? 1 : 0 ? 2 : 3) == 3 && (1 ? -1 : 0U) > 0U\n"
      "conditional_ok\n"
      "#endif\n"
      "#if (1 + 2 << 2) == 12 && (1 | 2 && 0) == 0 && "
      "(1 || 0 ? 4 : 5) == 4\n"
      "precedence_ok\n"
      "#endif\n"
      "#if 1UL == 1LU && 1ULL == 1LLU && "
      "18446744073709551615ULL == ~0ULL\n"
      "suffixes_ok\n"
      "#endif\n"
      "#if 0xffffffff > 0 && -1 < 0xffffffff && -1 > 0xffffffffU\n"
      "preprocessor_width_ok\n"
      "#endif\n"
      "#if ONE && \\\n"
      "    VALUE == 2\n"
      "splice_ok\n"
      "#endif\n"
      "#if 0\n"
      "not_taken\n"
      "#elif (3 * 3) == 9\n"
      "elif_ok\n"
      "#else\n"
      "not_taken\n"
      "#endif\n"
      "%:if 1\n"
      "digraph_ok\n"
      "%:endif\n"
      "#if COMMAND_FLAG == 12\n"
      "command_ok\n"
      "#endif\n"
      "restored_defined defined follower\n";
  static const char gnu_text[] =
      "#if 0b1010ULL == 10 && 9223372036854775808 > 0 && '\\e' == 27\n"
      "gnu_literals_ok\n"
      "#endif\n";
  static const char *const expected_text[] = {
      "ordinary_defined", "defined", "follower", "defined_ok",
      "unused_defined_ok", "macro_ok", "arithmetic_ok", "shifts_ok",
      "relations_ok", "bitwise_ok", "unary_ok", "short_circuit_ok",
      "literals_ok", "character_ranges_ok", "wide_ranges_ok",
      "unevaluated_comma_ok", "conditional_ok", "precedence_ok",
      "suffixes_ok", "preprocessor_width_ok", "splice_ok",
      "elif_ok", "digraph_ok", "command_ok", "restored_defined",
      "defined", "follower"};
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_macro_action_t action;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;

  if (open_job("conditional-expressions", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/conditional-expressions.c");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  action.kind = CTOOL_C_PP_MACRO_DEFINE;
  action.name = ctool_string("COMMAND_FLAG");
  action.replacement = ctool_string("(3 << 2)");
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  request.macro_actions = &action;
  request.macro_action_count = 1u;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK ||
      result.token_count !=
          (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
    (void)fprintf(stderr,
                  "conditional-expressions: preprocessing failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
        !string_equal(result.tokens[index].location.path,
                      "/conditional-expressions.c")) {
      (void)fprintf(stderr,
                    "conditional-expressions: token %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }
  source.path.text = ctool_string("/conditional-gnu.c");
  source.contents =
      ctool_bytes(gnu_text, (ctool_u32)(sizeof(gnu_text) - 1u));
  request.gnu_extensions = CTOOL_TRUE;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 1u ||
      !string_equal(result.tokens[0].spelling, "gnu_literals_ok") ||
      !string_equal(result.tokens[0].location.path, "/conditional-gnu.c") ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr,
                  "conditional-expressions: GNU literal probe differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("conditional-expressions: ok\n");
  return 0;
}

static int run_predefined_macros(void) {
  static char source_text[] =
      "#define SITE __FILE__ __LINE__\n"
      "first __FILE__ __LINE__ __STDC__ __STDC_HOSTED__ __STDC_VERSION__\n"
      "site SITE\n"
      "#if defined(__FILE__) && defined __LINE__ && defined(__STDC__) && "
      "__STDC__ == 1 && __STDC_VERSION__ == 201112L && "
      "__STDC_HOSTED__ == 0\n"
      "predefined_condition\n"
      "#endif\n"
      "#ifdef __STDC__\n"
      "ifdef_ok\n"
      "#endif\n"
      "#ifndef __LINE__\n"
      "not_taken\n"
      "#endif\n"
      "timestamp __DATE__ __TIME__\n"
      "#if defined(__DATE__) && defined(__TIME__)\n"
      "timestamp_condition\n"
      "#endif\n";
  static char source_path[] = "/predefined/path.c";
  static const char hosted_text[] = "hosted __STDC_HOSTED__\n";
  static const char cupid_text[] =
      "cupid __FILE__ __LINE__ __STDC__ __STDC_HOSTED__ "
      "__STDC_VERSION__ __DATE__ __TIME__\n";
  static const char custom_text[] = "custom __DATE__ __TIME__\n";
  static char custom_date[] = "Feb 29 2024";
  static char custom_time[] = "23:59:58";
  static const char escaped_text[] = "escaped __FILE__\n";
  static char escaped_path[] = "/q\"\n\001x.c";
  static const char *const expected_text[] = {
      "first", "\"/predefined/path.c\"", "2", "1", "0", "201112L",
      "site", "\"/predefined/path.c\"", "3", "predefined_condition",
      "ifdef_ok", "timestamp", "\"Jan  1 1970\"", "\"00:00:00\"",
      "timestamp_condition"};
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;

  if (open_job("predefined", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text.data = source_path;
  source.path.text.size = (ctool_u32)(sizeof(source_path) - 1u);
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK ||
      result.token_count !=
          (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
    (void)fprintf(stderr,
                  "predefined: freestanding preprocessing failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
        !string_equal(result.tokens[index].location.path,
                      "/predefined/path.c")) {
      (void)fprintf(stderr, "predefined: token %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }
  if (result.tokens[1].location.line != 2u ||
      result.tokens[1].location.column != 7u ||
      result.tokens[7].location.line != 3u ||
      result.tokens[7].location.column != 6u ||
      result.tokens[8].location.line != 3u ||
      result.tokens[8].location.column != 6u) {
    (void)fprintf(stderr, "predefined: generated locations differ\n");
    ctool_job_close(job);
    return 1;
  }
  source_text[0] = 'X';
  source_path[1] = 'X';
  if (!string_equal(result.tokens[1].spelling,
                    "\"/predefined/path.c\"") ||
      !string_equal(result.tokens[7].spelling,
                    "\"/predefined/path.c\"") ||
      !string_equal(result.tokens[1].location.path,
                    "/predefined/path.c")) {
    (void)fprintf(stderr, "predefined: generated values retained input\n");
    ctool_job_close(job);
    return 1;
  }
  source_text[0] = '#';
  source_path[1] = 'p';

  source.path.text = ctool_string("/hosted.c");
  source.contents =
      ctool_bytes(hosted_text, (ctool_u32)(sizeof(hosted_text) - 1u));
  request.hosted_environment = CTOOL_TRUE;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 2u ||
      !string_equal(result.tokens[0].spelling, "hosted") ||
      !string_equal(result.tokens[1].spelling, "1") ||
      !string_equal(result.tokens[1].location.path, "/hosted.c") ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "predefined: hosted expansion differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/cupid.cc");
  source.contents =
      ctool_bytes(cupid_text, (ctool_u32)(sizeof(cupid_text) - 1u));
  request.mode = CTOOL_C_PP_MODE_CUPID;
  request.hosted_environment = CTOOL_FALSE;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 8u ||
      !string_equal(result.tokens[0].spelling, "cupid") ||
      !string_equal(result.tokens[1].spelling, "\"/cupid.cc\"") ||
      !string_equal(result.tokens[2].spelling, "1") ||
      !string_equal(result.tokens[3].spelling, "1") ||
      !string_equal(result.tokens[4].spelling, "0") ||
      !string_equal(result.tokens[5].spelling, "201112L") ||
      !string_equal(result.tokens[6].spelling, "\"Jan  1 1970\"") ||
      !string_equal(result.tokens[7].spelling, "\"00:00:00\"") ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "predefined: Cupid-mode expansion differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/custom-time.c");
  source.contents =
      ctool_bytes(custom_text, (ctool_u32)(sizeof(custom_text) - 1u));
  request.mode = CTOOL_C_PP_MODE_C11;
  request.translation_date = ctool_string(custom_date);
  request.translation_time = ctool_string(custom_time);
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 3u ||
      !string_equal(result.tokens[0].spelling, "custom") ||
      !string_equal(result.tokens[1].spelling, "\"Feb 29 2024\"") ||
      !string_equal(result.tokens[2].spelling, "\"23:59:58\"") ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "predefined: custom timestamp differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  custom_date[0] = 'X';
  custom_time[0] = 'X';
  if (!string_equal(result.tokens[1].spelling, "\"Feb 29 2024\"") ||
      !string_equal(result.tokens[2].spelling, "\"23:59:58\"")) {
    (void)fprintf(stderr, "predefined: custom timestamp retained input\n");
    ctool_job_close(job);
    return 1;
  }
  custom_date[0] = 'F';
  custom_time[0] = '2';

  source.path.text.data = escaped_path;
  source.path.text.size = (ctool_u32)(sizeof(escaped_path) - 1u);
  source.contents =
      ctool_bytes(escaped_text, (ctool_u32)(sizeof(escaped_text) - 1u));
  request.mode = CTOOL_C_PP_MODE_C11;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 2u ||
      !string_equal(result.tokens[0].spelling, "escaped") ||
      !string_equal(result.tokens[1].spelling,
                    "\"/q\\\"\\012\\001x.c\"") ||
      !string_equal(result.tokens[1].location.path, "/q\"\n\001x.c")) {
    (void)fprintf(stderr, "predefined: escaped file spelling differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  escaped_path[1] = 'X';
  if (!string_equal(result.tokens[1].spelling,
                    "\"/q\\\"\\012\\001x.c\"") ||
      !string_equal(result.tokens[1].location.path, "/q\"\n\001x.c")) {
    (void)fprintf(stderr, "predefined: escaped path retained input\n");
    ctool_job_close(job);
    return 1;
  }
  escaped_path[1] = 'q';
  ctool_job_close(job);
  (void)printf("predefined: ok\n");
  return 0;
}

static int run_predefined_files(void) {
  static char forced_text[] = "forced __FILE__ __LINE__\n";
  static char header_text[] =
      "included __FILE__ __LINE__\n"
      "#define HEADER_SITE __FILE__ __LINE__\n"
      "#define HEADER_FUNCTION(value) value __FILE__ __LINE__\n";
  static const char source_text[] =
      "#include \"predefined.h\"\n"
      "invoked HEADER_SITE\n"
      "#define STR(value) #value\n"
      "#define XSTR(value) STR(value)\n"
      "raw STR(__LINE__)\n"
      "expanded XSTR(__LINE__)\n"
      "#define CAT(left, right) left ## right\n"
      "paste CAT(__LINE__, X)\n"
      "#if defined(HEADER_SITE)\nheader_macro_ok\n#endif\n"
      "function_site HEADER_FUNCTION(value)\n";
  static const char *const expected_text[] = {
      "forced",   "\"/forced.h\"",                  "1",
      "included", "\"/predefined/predefined.h\"", "1",
      "invoked",  "\"/predefined/main.c\"",       "2",
      "raw",      "\"__LINE__\"",
      "expanded", "\"6\"",
      "paste",    "__LINE__X",
      "header_macro_ok",
      "function_site", "value", "\"/predefined/main.c\"", "12"};
  static const char *const expected_path[] = {
      "/forced.h", "/forced.h", "/forced.h",
      "/predefined/predefined.h", "/predefined/predefined.h",
      "/predefined/predefined.h", "/predefined/main.c",
      "/predefined/main.c", "/predefined/main.c", "/predefined/main.c",
      "/predefined/main.c", "/predefined/main.c", "/predefined/main.c",
      "/predefined/main.c", "/predefined/main.c", "/predefined/main.c",
      "/predefined/main.c", "/predefined/main.c", "/predefined/main.c",
      "/predefined/main.c"};
  fixture_file_t files[2];
  fixture_store_t store;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_path_t forced;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;

  files[0].path = ctool_string("/forced.h");
  files[0].contents =
      ctool_bytes(forced_text, (ctool_u32)(sizeof(forced_text) - 1u));
  files[1].path = ctool_string("/predefined/predefined.h");
  files[1].contents =
      ctool_bytes(header_text, (ctool_u32)(sizeof(header_text) - 1u));
  store.files = files;
  store.file_count = 2u;
  store.read_count = 0u;
  if (open_fixture_job("predefined-files", &store, &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/predefined/main.c");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  forced.text = ctool_string("/forced.h");
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  request.forced_includes = &forced;
  request.forced_include_count = 1u;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || store.read_count != 2u ||
      result.token_count !=
          (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
    (void)fprintf(stderr,
                  "predefined-files: preprocessing failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
        !string_equal(result.tokens[index].location.path,
                      expected_path[index])) {
      (void)fprintf(stderr,
                    "predefined-files: token %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }
  forced_text[0] = 'X';
  header_text[0] = 'X';
  if (!string_equal(result.tokens[1].spelling, "\"/forced.h\"") ||
      !string_equal(result.tokens[4].spelling,
                    "\"/predefined/predefined.h\"") ||
      !string_equal(result.tokens[7].spelling,
                    "\"/predefined/main.c\"") ||
      !string_equal(result.tokens[1].location.path, "/forced.h")) {
    (void)fprintf(stderr,
                  "predefined-files: result retained fixture storage\n");
    ctool_job_close(job);
    return 1;
  }
  forced_text[0] = 'f';
  header_text[0] = 'i';
  ctool_job_close(job);
  (void)printf("predefined-files: ok\n");
  return 0;
}

static int run_predefined_errors(void) {
  static const char *const names[] = {
      "__FILE__", "__LINE__", "__DATE__", "__TIME__", "__STDC__",
      "__STDC_HOSTED__", "__STDC_VERSION__", "defined"};
  static const char recovery_text[] =
      "#define ID(defined) defined\nrecovered ID(value) __STDC__\n";
  static const char action_text[] = "unreached\n";
  static const char *const invalid_dates[] = {
      "Foo 10 2026", "Jan 01 1970", "Jul 32 2026",
      "Feb 31 2026", "Apr 31 2026", "Feb 29 2025"};
  static const char *const invalid_times[] = {
      "24:00:00", "23:60:00", "23:59:61"};
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_macro_action_t action;
  ctool_c_pp_result_t result;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 diagnostic_index = 0u;
  ctool_u32 index;
  ctool_u32 operation;
  char source_text[96];

  if (open_job("predefined-errors", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/predefined-errors.c");
  for (index = 0u;
       index < (ctool_u32)(sizeof(names) / sizeof(names[0])); index++) {
    for (operation = 0u; operation < 2u; operation++) {
      int written = snprintf(
          source_text, sizeof(source_text),
          operation == 0u ? "#define %s 1\n" : "#undef %s\n",
          names[index]);
      if (written < 0 || (size_t)written >= sizeof(source_text)) {
        (void)fprintf(stderr,
                      "predefined-errors: source fixture overflowed\n");
        ctool_job_close(job);
        return 1;
      }
      source.contents = ctool_bytes(source_text, (ctool_u32)written);
      (void)memset(&request, 0, sizeof(request));
      request.mode = CTOOL_C_PP_MODE_C11;
      (void)memset(&result, 0xa5, sizeof(result));
      mark = ctool_arena_mark(ctool_job_arena(job));
      status = ctool_c_preprocess(job, &source, &request, &result);
      diagnostic = ctool_job_diagnostic(job, diagnostic_index);
      if (status != CTOOL_ERR_INPUT || result.tokens != NULL ||
          result.token_count != 0u || diagnostic == NULL ||
          diagnostic->code != CTOOL_C_PP_DIAG_MACRO_DEFINITION ||
          diagnostic->line != 1u ||
          diagnostic->column != (operation == 0u ? 9u : 8u) ||
          !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr,
                      "predefined-errors: source case %u/%u differs\n",
                      index, operation);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
      }
      diagnostic_index++;
    }
  }

  source.contents =
      ctool_bytes(action_text, (ctool_u32)(sizeof(action_text) - 1u));
  for (index = 0u;
       index < (ctool_u32)(sizeof(names) / sizeof(names[0])); index++) {
    for (operation = 0u; operation < 2u; operation++) {
      (void)memset(&request, 0, sizeof(request));
      request.mode = CTOOL_C_PP_MODE_C11;
      action.kind = operation == 0u ? CTOOL_C_PP_MACRO_DEFINE
                                    : CTOOL_C_PP_MACRO_UNDEF;
      action.name = ctool_string(names[index]);
      action.replacement = operation == 0u ? ctool_string("1")
                                           : ctool_string("");
      request.macro_actions = &action;
      request.macro_action_count = 1u;
      (void)memset(&result, 0xa5, sizeof(result));
      mark = ctool_arena_mark(ctool_job_arena(job));
      status = ctool_c_preprocess(job, &source, &request, &result);
      diagnostic = ctool_job_diagnostic(job, diagnostic_index);
      if (status != CTOOL_ERR_INPUT || result.tokens != NULL ||
          result.token_count != 0u || diagnostic == NULL ||
          diagnostic->code != CTOOL_C_PP_DIAG_MACRO_DEFINITION ||
          diagnostic->line != 0u || diagnostic->column != 0u ||
          !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr,
                      "predefined-errors: action case %u/%u differs\n",
                      index, operation);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
      }
      diagnostic_index++;
    }
  }

  for (index = 0u;
       index < (ctool_u32)(sizeof(invalid_dates) / sizeof(invalid_dates[0])) +
                   (ctool_u32)(sizeof(invalid_times) / sizeof(invalid_times[0]));
       index++) {
    ctool_u32 date_count =
        (ctool_u32)(sizeof(invalid_dates) / sizeof(invalid_dates[0]));
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    if (index < date_count) {
      request.translation_date = ctool_string(invalid_dates[index]);
    } else {
      request.translation_time =
          ctool_string(invalid_times[index - date_count]);
    }
    (void)memset(&result, 0xa5, sizeof(result));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, diagnostic_index);
    if (status != CTOOL_ERR_INVALID_ARGUMENT || result.tokens != NULL ||
        result.token_count != 0u || diagnostic == NULL ||
        diagnostic->code != CTOOL_C_PP_DIAG_INVALID_REQUEST ||
        diagnostic->line != 0u || diagnostic->column != 0u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr,
                    "predefined-errors: timestamp case %u differs\n", index);
      (void)ctool_job_render_diagnostics(job);
      ctool_job_close(job);
      return 1;
    }
    diagnostic_index++;
  }

  source.contents =
      ctool_bytes(recovery_text, (ctool_u32)(sizeof(recovery_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 3u ||
      !string_equal(result.tokens[0].spelling, "recovered") ||
      !string_equal(result.tokens[1].spelling, "value") ||
      !string_equal(result.tokens[2].spelling, "1") ||
      ctool_job_diagnostic_count(job) != diagnostic_index) {
    (void)fprintf(stderr, "predefined-errors: recovery differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("predefined-errors: ok\n");
  return 0;
}

static int run_pragmas(void) {
  static const char source_text[] =
      "natural\n"
      "#define OBJECT object\n"
      "#define FUNCTION(value) value __LINE__\n"
      "#define STRING(value) #value\n"
      "#define PASTE(left, right) left ## right\n"
      "#define VARIADIC(...) __VA_ARGS__\n"
      "#pragma pack(push, 1)\n"
      "packed direct OBJECT FUNCTION(argument) STRING(stringified) "
      "PASTE(paste, d) VARIADIC(variable) __FILE__\n"
      "#if 0\n"
      "#pragma cupid_ignored\n"
      "#pragma pack(pop)\n"
      "#endif\n"
      "still_packed\n"
      "%:pragma pack(push, 4)\n"
      "four_byte\n"
      "%:pragma pack(2)\n"
      "two_byte\n"
      "%:pragma pack()\n"
      "reset_inside_push\n"
      "%:pragma pack(pop)\n"
      "back_to_one\n"
      "#pragma pack(push)\n"
      "unnamed_same\n"
      "#pragma pack(pop)\n"
      "#pragma pack(push, outer, 0x8U)\n"
      "named_eight\n"
      "#pragma pack(push, inner, 2)\n"
      "named_two\n"
      "#pragma pack(pop, outer)\n"
      "named_back_to_one\n"
      "#pragma pack(push, same)\n"
      "named_same\n"
      "#pragma pack(pop, same)\n"
      "#pragma pack(push, duplicate, 4U)\n"
      "duplicate_four\n"
      "#pragma pack(push, duplicate, 02)\n"
      "duplicate_two\n"
      "#pragma pack(pop, duplicate)\n"
      "duplicate_back_four\n"
      "#pragma pack(pop, duplicate)\n"
      "duplicate_back_one\n"
      "#pragma pack(push, 0)\n"
      "pushed_natural\n"
      "#pragma pack(pop)\n"
      "after_zero_push\n"
      "#pragma pack(0U)\n"
      "direct_natural\n"
      "#pragma pack(0x10ULL)\n"
      "wide_sixteen\n"
      "#pragma pack(01)\n"
      "direct_octal_one\n"
      "#pragma pack(pop)\n"
      "natural_again\n";
  static const char unbalanced_text[] =
      "#pragma pack(push, dangling, 1)\n"
      "accepted_dangling_push\n";
  static const char *const expected_text[] = {
      "natural", "packed", "direct", "object", "argument", "8",
      "\"stringified\"", "pasted", "variable", "\"/pragmas.c\"",
      "still_packed", "four_byte", "two_byte",
      "reset_inside_push", "back_to_one", "unnamed_same", "named_eight",
      "named_two", "named_back_to_one", "named_same", "duplicate_four",
      "duplicate_two", "duplicate_back_four", "duplicate_back_one",
      "pushed_natural", "after_zero_push", "direct_natural",
      "wide_sixteen", "direct_octal_one", "natural_again"};
  static const ctool_u32 expected_pack[] = {
      0u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 4u, 2u, 0u,
      1u, 1u, 8u, 2u, 1u, 1u, 4u, 2u, 4u, 1u, 0u, 1u, 0u, 16u,
      1u, 0u};
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;
  ctool_u32 mode_index;

  if (open_job("pragmas", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/pragmas.c");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  for (mode_index = 0u; mode_index < 2u; mode_index++) {
    request.mode = mode_index == 0u ? CTOOL_C_PP_MODE_C11
                                    : CTOOL_C_PP_MODE_CUPID;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK ||
        result.token_count !=
            (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0])) ||
        result.token_count !=
            (ctool_u32)(sizeof(expected_pack) / sizeof(expected_pack[0]))) {
      (void)fprintf(stderr,
                    "pragmas: preprocessing failed (%s, %u tokens)\n",
                    ctool_status_name(status), result.token_count);
      (void)ctool_job_render_diagnostics(job);
      ctool_job_close(job);
      return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
      if (!string_equal(result.tokens[index].spelling,
                        expected_text[index]) ||
          !string_equal(result.tokens[index].location.path, "/pragmas.c") ||
          result.tokens[index].pack_alignment != expected_pack[index]) {
        (void)fprintf(stderr, "pragmas: mode %u token %u differs\n",
                      mode_index, index);
        ctool_job_close(job);
        return 1;
      }
    }
  }
  source.path.text = ctool_string("/unbalanced-pragma.c");
  source.contents = ctool_bytes(
      unbalanced_text, (ctool_u32)(sizeof(unbalanced_text) - 1u));
  request.mode = CTOOL_C_PP_MODE_C11;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 1u ||
      !string_equal(result.tokens[0].spelling, "accepted_dangling_push") ||
      result.tokens[0].pack_alignment != 1u) {
    (void)fprintf(stderr, "pragmas: dangling push policy differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("pragmas: ok\n");
  return 0;
}

static int run_pragma_files(void) {
  static const char forced_text[] =
      "#pragma once\n"
      "#pragma pack(push, forced_scope, 4)\n"
      "forced_once\n";
  static const char once_text[] =
      "before_once\n"
      "#pragma once\n"
      "after_once\n"
      "#include \"once.h\"\n";
  static const char pack_text[] =
      "#pragma pack(push, header_scope, 1)\n"
      "header_packed\n";
  static const char leak_text[] =
      "#pragma pack(2)\n"
      "leaked_header\n";
  static const char twin_text[] =
      "#pragma once\n"
      "path_once\n";
  static const char inactive_once_text[] =
      "#if 0\n"
      "#pragma once\n"
      "#endif\n"
      "inactive_once_repeat\n";
  static const char ambient_text[] = "ambient_replay\n";
  static const char source_text[] =
      "#pragma pack(pop, forced_scope)\n"
      "primary_natural\n"
      "#include \"once.h\"\n"
      "#include \"./once.h\"\n"
      "#include \"twin-a.h\"\n"
      "#include \"twin-b.h\"\n"
      "#include \"inactive-once.h\"\n"
      "#include \"inactive-once.h\"\n"
      "#pragma pack(1)\n"
      "#include \"ambient.h\"\n"
      "#pragma pack(2)\n"
      "#include \"ambient.h\"\n"
      "#pragma pack()\n"
      "#include \"pack.h\"\n"
      "#pragma pack(pop, header_scope)\n"
      "between_natural\n"
      "#include \"pack.h\"\n"
      "#pragma pack(pop, header_scope)\n"
      "after_natural\n"
      "#include \"leak.h\"\n"
      "after_leak\n"
      "#pragma pack()\n"
      "#include \"forced-once.h\"\n";
  static const char self_text[] =
      "#pragma once\n"
      "self_once\n"
      "#include \"./self.c\"\n";
  static const char *const expected_text[] = {
      "forced_once", "primary_natural", "before_once", "after_once",
      "path_once", "path_once", "inactive_once_repeat",
      "inactive_once_repeat", "ambient_replay", "ambient_replay",
      "header_packed", "between_natural", "header_packed", "after_natural",
      "leaked_header", "after_leak"};
  static const char *const expected_path[] = {
      "/forced-once.h", "/main.c", "/inc/once.h", "/inc/once.h",
      "/inc/twin-a.h", "/inc/twin-b.h", "/inc/inactive-once.h",
      "/inc/inactive-once.h", "/inc/ambient.h", "/inc/ambient.h",
      "/inc/pack.h", "/main.c", "/inc/pack.h", "/main.c",
      "/inc/leak.h", "/main.c"};
  static const ctool_u32 expected_pack[] = {
      4u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 2u, 1u, 0u, 1u, 0u,
      2u, 2u};
  fixture_file_t files[8];
  fixture_store_t store;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_path_t forced;
  ctool_c_pp_include_root_t roots[2];
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;

  files[0].path = ctool_string("/forced-once.h");
  files[0].contents =
      ctool_bytes(forced_text, (ctool_u32)(sizeof(forced_text) - 1u));
  files[1].path = ctool_string("/inc/once.h");
  files[1].contents =
      ctool_bytes(once_text, (ctool_u32)(sizeof(once_text) - 1u));
  files[2].path = ctool_string("/inc/pack.h");
  files[2].contents =
      ctool_bytes(pack_text, (ctool_u32)(sizeof(pack_text) - 1u));
  files[3].path = ctool_string("/inc/leak.h");
  files[3].contents =
      ctool_bytes(leak_text, (ctool_u32)(sizeof(leak_text) - 1u));
  files[4].path = ctool_string("/inc/twin-a.h");
  files[4].contents =
      ctool_bytes(twin_text, (ctool_u32)(sizeof(twin_text) - 1u));
  files[5].path = ctool_string("/inc/twin-b.h");
  files[5].contents =
      ctool_bytes(twin_text, (ctool_u32)(sizeof(twin_text) - 1u));
  files[6].path = ctool_string("/inc/inactive-once.h");
  files[6].contents = ctool_bytes(
      inactive_once_text, (ctool_u32)(sizeof(inactive_once_text) - 1u));
  files[7].path = ctool_string("/inc/ambient.h");
  files[7].contents =
      ctool_bytes(ambient_text, (ctool_u32)(sizeof(ambient_text) - 1u));
  store.files = files;
  store.file_count = 8u;
  store.read_count = 0u;
  if (open_fixture_job("pragma-files", &store, &adapter, &job) != 0) {
    return 1;
  }
  roots[0].directory.text = ctool_string("/inc");
  roots[0].forms = CTOOL_C_PP_INCLUDE_QUOTED;
  roots[1].directory.text = ctool_string("/");
  roots[1].forms = CTOOL_C_PP_INCLUDE_QUOTED;
  forced.text = ctool_string("/forced-once.h");
  source.path.text = ctool_string("/main.c");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  request.include_roots = roots;
  request.include_root_count = 2u;
  request.forced_includes = &forced;
  request.forced_include_count = 1u;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || store.read_count != 8u ||
      result.token_count !=
          (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
    (void)fprintf(stderr,
                  "pragma-files: preprocessing failed (%s, %u tokens, %u reads)\n",
                  ctool_status_name(status), result.token_count,
                  store.read_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
        !string_equal(result.tokens[index].location.path,
                      expected_path[index]) ||
        result.tokens[index].pack_alignment != expected_pack[index]) {
      (void)fprintf(stderr, "pragma-files: token %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }

  source.path.text = ctool_string("/self.c");
  source.contents =
      ctool_bytes(self_text, (ctool_u32)(sizeof(self_text) - 1u));
  request.forced_includes = (const ctool_path_t *)0;
  request.forced_include_count = 0u;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || store.read_count != 8u ||
      result.token_count != 1u ||
      !string_equal(result.tokens[0].spelling, "self_once") ||
      !string_equal(result.tokens[0].location.path, "/self.c") ||
      result.tokens[0].pack_alignment != 0u ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "pragma-files: primary once recursion differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  request.mode = CTOOL_C_PP_MODE_CUPID;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || store.read_count != 8u ||
      result.token_count != 1u ||
      !string_equal(result.tokens[0].spelling, "self_once") ||
      !string_equal(result.tokens[0].location.path, "/self.c") ||
      result.tokens[0].pack_alignment != 0u ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "pragma-files: once state leaked between jobs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("pragma-files: ok\n");
  return 0;
}

static int run_pragma_errors(void) {
  static const char *const error_text[] = {
      "#pragma once extra\n",
      "#pragma pack\n",
      "#pragma pack(3)\n",
      "#pragma pack(push,)\n",
      "#pragma pack(pop)\n",
      "#pragma pack(push, 1, 2)\n",
      "#pragma cupid_vendor_extension\n",
      "#pragma pack(pop, missing)\n",
      "#pragma pack(pop, 1)\n",
      "#pragma pack(pop, label, 2)\n",
      "#pragma pack(push, 123, 1)\n",
      "#pragma pack(1UU)\n",
      "#pragma pack(0x)\n",
      "#define CAP 1\n#pragma pack(CAP)\n"};
  static const ctool_status_t error_status[] = {
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_UNSUPPORTED, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT};
  static const ctool_u32 error_code[] = {
      CTOOL_C_PP_DIAG_DIRECTIVE,
      CTOOL_C_PP_DIAG_PRAGMA_PACK,
      CTOOL_C_PP_DIAG_PRAGMA_PACK,
      CTOOL_C_PP_DIAG_PRAGMA_PACK,
      CTOOL_C_PP_DIAG_PRAGMA_PACK,
      CTOOL_C_PP_DIAG_PRAGMA_PACK,
      CTOOL_C_PP_DIAG_DIRECTIVE,
      CTOOL_C_PP_DIAG_PRAGMA_PACK,
      CTOOL_C_PP_DIAG_PRAGMA_PACK,
      CTOOL_C_PP_DIAG_PRAGMA_PACK,
      CTOOL_C_PP_DIAG_PRAGMA_PACK,
      CTOOL_C_PP_DIAG_PRAGMA_PACK,
      CTOOL_C_PP_DIAG_PRAGMA_PACK,
      CTOOL_C_PP_DIAG_PRAGMA_PACK};
  static const ctool_u32 error_line[] = {
      1u, 1u, 1u, 1u, 1u, 1u, 1u,
      1u, 1u, 1u, 1u, 1u, 1u, 2u};
  static const char recovery_text[] =
      "#pragma pack(push, 2)\n"
      "recovered\n"
      "#pragma pack(pop)\n"
      "natural\n";
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 case_count =
      (ctool_u32)(sizeof(error_text) / sizeof(error_text[0]));
  ctool_u32 index;

  if (open_job("pragma-errors", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/pragma-errors.c");
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  for (index = 0u; index < case_count; index++) {
    source.contents =
        ctool_bytes(error_text[index], (ctool_u32)strlen(error_text[index]));
    (void)memset(&result, 0xa5, sizeof(result));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, index);
    if (status != error_status[index] || result.tokens != NULL ||
        result.token_count != 0u || diagnostic == NULL ||
        diagnostic->code != error_code[index] ||
        !string_equal(diagnostic->path, "/pragma-errors.c") ||
        diagnostic->line != error_line[index] || diagnostic->column != 1u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr, "pragma-errors: case %u differs (%s)\n", index,
                    ctool_status_name(status));
      (void)ctool_job_render_diagnostics(job);
      ctool_job_close(job);
      return 1;
    }
  }
  source.contents =
      ctool_bytes(recovery_text, (ctool_u32)(sizeof(recovery_text) - 1u));
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 2u ||
      !string_equal(result.tokens[0].spelling, "recovered") ||
      result.tokens[0].pack_alignment != 2u ||
      !string_equal(result.tokens[1].spelling, "natural") ||
      result.tokens[1].pack_alignment != 0u ||
      ctool_job_diagnostic_count(job) != case_count) {
    (void)fprintf(stderr, "pragma-errors: recovery differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("pragma-errors: ok\n");
  return 0;
}

static int run_pragma_scale(void) {
  enum {
    PACK_DEPTH_LIMIT = 256,
    REUSE_COUNT = 2048,
    REUSE_FORCED_COUNT = 16384,
    SOURCE_CAPACITY = 128 * 1024
  };
  static char source_text[SOURCE_CAPACITY];
  static char failure_text[SOURCE_CAPACITY];
  static const char recovery_text[] = "after_failure\n";
  static const char forced_reuse_text[] =
      "#pragma pack(push, 1)\n"
      "#pragma pack(pop)\n";
  static ctool_path_t forced_reuse[REUSE_FORCED_COUNT];
  fixture_file_t reuse_file;
  fixture_store_t reuse_store;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_job_config_t config;
  ctool_limits_t limits;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 source_size = 0u;
  ctool_u32 failure_size = 0u;
  ctool_u32 index;

  for (index = 0u; index < PACK_DEPTH_LIMIT; index++) {
    if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                    "#pragma pack(push, 1)\n") != 0 ||
        append_text(failure_text, SOURCE_CAPACITY, &failure_size,
                    "#pragma pack(push, 1)\n") != 0) {
      return 1;
    }
  }
  if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                  "at_limit\n") != 0 ||
      append_text(failure_text, SOURCE_CAPACITY, &failure_size,
                  "#pragma pack(push, 1)\n") != 0) {
    return 1;
  }
  for (index = 0u; index < PACK_DEPTH_LIMIT; index++) {
    if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                    "#pragma pack(pop)\n") != 0) {
      return 1;
    }
  }
  for (index = 0u; index < REUSE_COUNT; index++) {
    if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                    "#pragma pack(push, 2)\n#pragma pack(pop)\n") != 0) {
      return 1;
    }
  }
  if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                  "after_reuse\n") != 0) {
    return 1;
  }

  if (open_job("pragma-scale", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/pragma-scale.c");
  source.contents = ctool_bytes(source_text, source_size);
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 2u ||
      !string_equal(result.tokens[0].spelling, "at_limit") ||
      result.tokens[0].pack_alignment != 1u ||
      !string_equal(result.tokens[1].spelling, "after_reuse") ||
      result.tokens[1].pack_alignment != 0u) {
    (void)fprintf(stderr, "pragma-scale: bounded success differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/pragma-scale-fail.c");
  source.contents = ctool_bytes(failure_text, failure_size);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_preprocess(job, &source, &request, &result);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (status != CTOOL_ERR_LIMIT || result.tokens != NULL ||
      result.token_count != 0u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PP_DIAG_PRAGMA_PACK ||
      !string_equal(diagnostic->path, "/pragma-scale-fail.c") ||
      diagnostic->line != 257u || diagnostic->column != 1u ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr, "pragma-scale: depth failure differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/pragma-scale-recovery.c");
  source.contents =
      ctool_bytes(recovery_text, (ctool_u32)(sizeof(recovery_text) - 1u));
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 1u ||
      !string_equal(result.tokens[0].spelling, "after_failure") ||
      result.tokens[0].pack_alignment != 0u ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr, "pragma-scale: recovery differs\n");
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);

  reuse_file.path = ctool_string("/pack-reuse.h");
  reuse_file.contents = ctool_bytes(
      forced_reuse_text, (ctool_u32)(sizeof(forced_reuse_text) - 1u));
  reuse_store.files = &reuse_file;
  reuse_store.file_count = 1u;
  reuse_store.read_count = 0u;
  for (index = 0u; index < REUSE_FORCED_COUNT; index++) {
    forced_reuse[index].text = ctool_string("/pack-reuse.h");
  }
  limits = ctool_default_limits();
  limits.arena_block_bytes = 4096u;
  limits.arena_bytes = 256u * 1024u;
  config = ctool_host_job_config(&adapter, limits);
  config.files.context = &reuse_store;
  config.files.file_size = fixture_file_size;
  config.files.read_exact = fixture_read_exact;
  config.files.write_all = fixture_write_all;
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "pragma-scale: reuse job open failed (%s)\n",
                  ctool_status_name(status));
    return 1;
  }
  source.path.text = ctool_string("/pack-reuse-primary.c");
  source.contents = ctool_bytes("", 0u);
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  request.forced_includes = forced_reuse;
  request.forced_include_count = REUSE_FORCED_COUNT;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 0u ||
      reuse_store.read_count != 1u || ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr,
                  "pragma-scale: bounded frame reuse differs (%s, %u reads)\n",
                  ctool_status_name(status), reuse_store.read_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("pragma-scale: ok\n");
  return 0;
}

static int run_cupid_exe(void) {
  static const char source_text[] =
      "#define VALUE expanded\n"
      "#define CALL(value) value\n"
      "ordinary\n"
      "#pragma pack(2)\n"
      "#exe { VALUE __LINE__ CALL(\n"
      "cross_line)\n"
      "#if 1\n"
      "nested\n"
      "#endif\n"
      "}\n"
      "#if 0\n"
      "#exe malformed\n"
      "#endif\n"
      "%:exe { same_line }\n"
      "#pragma pack()\n"
      "after\n";
  static const char c11_skipped_text[] =
      "#if 0\n"
      "#exe malformed\n"
      "#endif\n"
      "c11_ok\n";
  static const char selected_text[] =
      "#if 1\n"
      "#exe { selected }\n"
      "#endif\n";
  static const char unclosed_text[] =
      "#exe {\n"
      "parser_owns_close\n";
  static const char spliced_text[] =
      "#ex\\\n"
      "e /* comment */ \\\n"
      "{ spliced }\n";
  static const char directive_name_text[] =
      "#define exe changed\n"
      "#exe {}\n"
      "exe\n";
  char owned_text[] = "#exe {}\n";
  char owned_path[] = "/owned-exe.cc";
  static const char *const expected_text[] = {
      "ordinary", "exe", "{", "expanded", "5", "cross_line", "nested",
      "}", "exe", "{", "same_line", "}", "after"};
  static const ctool_c_pp_token_kind_t expected_kind[] = {
      CTOOL_C_PP_TOKEN_IDENTIFIER,
      CTOOL_C_PP_TOKEN_CUPID_EXE,
      CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_IDENTIFIER,
      CTOOL_C_PP_TOKEN_NUMBER,
      CTOOL_C_PP_TOKEN_IDENTIFIER,
      CTOOL_C_PP_TOKEN_IDENTIFIER,
      CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_CUPID_EXE,
      CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_IDENTIFIER,
      CTOOL_C_PP_TOKEN_PUNCTUATOR,
      CTOOL_C_PP_TOKEN_IDENTIFIER};
  static const ctool_u32 expected_line[] = {
      3u, 5u, 5u, 5u, 5u, 6u, 8u, 10u, 14u, 14u, 14u, 14u, 16u};
  static const ctool_u32 expected_column[] = {
      1u, 1u, 6u, 8u, 14u, 1u, 1u, 1u, 1u, 7u, 9u, 19u, 1u};
  static const ctool_u32 expected_pack[] = {
      0u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 0u};
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;

  if (open_job("cupid-exe", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/cupid-exe.cc");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_CUPID;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK ||
      result.token_count !=
          (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
    (void)fprintf(stderr, "cupid-exe: preprocessing failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
        result.tokens[index].kind != expected_kind[index] ||
        !string_equal(result.tokens[index].location.path, "/cupid-exe.cc") ||
        result.tokens[index].location.line != expected_line[index] ||
        result.tokens[index].location.column != expected_column[index] ||
        result.tokens[index].pack_alignment != expected_pack[index]) {
      (void)fprintf(stderr, "cupid-exe: token %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }

  source.path.text = ctool_string("/c11-skipped-exe.c");
  source.contents = ctool_bytes(
      c11_skipped_text, (ctool_u32)(sizeof(c11_skipped_text) - 1u));
  request.mode = CTOOL_C_PP_MODE_C11;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 1u ||
      !string_equal(result.tokens[0].spelling, "c11_ok") ||
      result.tokens[0].kind != CTOOL_C_PP_TOKEN_IDENTIFIER ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "cupid-exe: inactive C11 directive differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/selected-exe.cc");
  source.contents = ctool_bytes(
      selected_text, (ctool_u32)(sizeof(selected_text) - 1u));
  request.mode = CTOOL_C_PP_MODE_CUPID;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 4u ||
      result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
      result.tokens[0].location.line != 2u ||
      !string_equal(result.tokens[1].spelling, "{") ||
      !string_equal(result.tokens[2].spelling, "selected") ||
      !string_equal(result.tokens[3].spelling, "}") ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "cupid-exe: selected conditional differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/unclosed-exe.cc");
  source.contents = ctool_bytes(
      unclosed_text, (ctool_u32)(sizeof(unclosed_text) - 1u));
  request.mode = CTOOL_C_PP_MODE_CUPID;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 3u ||
      result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
      !string_equal(result.tokens[1].spelling, "{") ||
      !string_equal(result.tokens[2].spelling, "parser_owns_close") ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "cupid-exe: parser ownership differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/spliced-exe.cc");
  source.contents =
      ctool_bytes(spliced_text, (ctool_u32)(sizeof(spliced_text) - 1u));
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 4u ||
      result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
      result.tokens[0].location.line != 1u ||
      result.tokens[0].location.column != 1u ||
      !string_equal(result.tokens[1].spelling, "{") ||
      result.tokens[1].location.line != 3u ||
      result.tokens[1].location.column != 1u ||
      !string_equal(result.tokens[2].spelling, "spliced") ||
      result.tokens[2].location.line != 3u ||
      result.tokens[2].location.column != 3u ||
      !string_equal(result.tokens[3].spelling, "}") ||
      result.tokens[3].location.line != 3u ||
      result.tokens[3].location.column != 11u) {
    (void)fprintf(stderr, "cupid-exe: spliced directive differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/exe-name-macro.cc");
  source.contents = ctool_bytes(
      directive_name_text,
      (ctool_u32)(sizeof(directive_name_text) - 1u));
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 4u ||
      result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
      !string_equal(result.tokens[0].spelling, "exe") ||
      !string_equal(result.tokens[1].spelling, "{") ||
      !string_equal(result.tokens[2].spelling, "}") ||
      !string_equal(result.tokens[3].spelling, "changed")) {
    (void)fprintf(stderr, "cupid-exe: directive name expansion differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string(owned_path);
  source.contents =
      ctool_bytes(owned_text, (ctool_u32)(sizeof(owned_text) - 1u));
  status = ctool_c_preprocess(job, &source, &request, &result);
  owned_text[1] = 'X';
  owned_path[1] = 'X';
  if (status != CTOOL_OK || result.token_count != 3u ||
      result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
      !string_equal(result.tokens[0].spelling, "exe") ||
      !string_equal(result.tokens[0].location.path, "/owned-exe.cc") ||
      result.tokens[0].location.line != 1u ||
      result.tokens[0].location.column != 1u) {
    (void)fprintf(stderr, "cupid-exe: marker ownership differs\n");
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("cupid-exe: ok\n");
  return 0;
}

static int run_cupid_exe_active(const char *host_root) {
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_path_t path;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 marker_count = 0u;
  ctool_u32 marker_index = 0u;
  ctool_u32 index;

  if (open_job_at_root("cupid-exe-active", host_root, &adapter, &job) != 0) {
    return 1;
  }
  path.text = ctool_string("/bin/feature6_exe.cc");
  status = ctool_job_load_source(job, &path, &source);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "cupid-exe-active: source load failed (%s)\n",
                  ctool_status_name(status));
    ctool_job_close(job);
    return 1;
  }
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_CUPID;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "cupid-exe-active: preprocessing failed (%s)\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (result.tokens[index].kind == CTOOL_C_PP_TOKEN_CUPID_EXE) {
      marker_count++;
      marker_index = index;
    }
    if (string_starts_with(result.tokens[index].spelling, "__cc_exe_")) {
      (void)fprintf(stderr, "cupid-exe-active: policy lowering leaked\n");
      ctool_job_close(job);
      return 1;
    }
  }
  if (marker_count != 1u || marker_index + 2u >= result.token_count ||
      !string_equal(result.tokens[marker_index].spelling, "exe") ||
      !string_equal(result.tokens[marker_index].location.path,
                    "/bin/feature6_exe.cc") ||
      result.tokens[marker_index].location.line != 7u ||
      result.tokens[marker_index].location.column != 1u ||
      result.tokens[marker_index].pack_alignment != 0u ||
      !string_equal(result.tokens[marker_index + 1u].spelling, "{") ||
      result.tokens[marker_index + 1u].location.line != 7u ||
      result.tokens[marker_index + 1u].location.column != 6u ||
      !string_equal(result.tokens[marker_index + 2u].spelling, "g_value") ||
      result.tokens[marker_index + 2u].location.line != 8u) {
    (void)fprintf(stderr, "cupid-exe-active: marker contract differs\n");
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("cupid-exe-active: ok\n");
  return 0;
}

static int run_cupid_exe_files(void) {
  static const char forced_text[] =
      "#pragma pack(4)\n"
      "#exe { forced_body }\n"
      "#pragma pack()\n";
  static const char guardless_text[] =
      "#exe { guardless_body }\n";
  static const char once_text[] =
      "#pragma once\n"
      "#exe { once_body }\n";
  static const char source_text[] =
      "#pragma pack(2)\n"
      "#include \"guardless-exe.h\"\n"
      "#pragma pack(1)\n"
      "#include \"./guardless-exe.h\"\n"
      "#pragma pack()\n"
      "#include \"once-exe.h\"\n"
      "#include \"once-exe.h\"\n"
      "#exe { primary_body }\n";
  static const char *const expected_path[] = {
      "/forced-exe.h", "/inc/guardless-exe.h", "/inc/guardless-exe.h",
      "/inc/once-exe.h", "/primary-exe.cc"};
  static const char *const expected_body[] = {
      "forced_body", "guardless_body", "guardless_body", "once_body",
      "primary_body"};
  static const ctool_u32 expected_pack[] = {4u, 2u, 1u, 0u, 0u};
  fixture_file_t files[3];
  fixture_store_t store;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_path_t forced;
  ctool_c_pp_include_root_t roots[2];
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;

  files[0].path = ctool_string("/forced-exe.h");
  files[0].contents =
      ctool_bytes(forced_text, (ctool_u32)(sizeof(forced_text) - 1u));
  files[1].path = ctool_string("/inc/guardless-exe.h");
  files[1].contents = ctool_bytes(
      guardless_text, (ctool_u32)(sizeof(guardless_text) - 1u));
  files[2].path = ctool_string("/inc/once-exe.h");
  files[2].contents =
      ctool_bytes(once_text, (ctool_u32)(sizeof(once_text) - 1u));
  store.files = files;
  store.file_count = 3u;
  store.read_count = 0u;
  if (open_fixture_job("cupid-exe-files", &store, &adapter, &job) != 0) {
    return 1;
  }
  roots[0].directory.text = ctool_string("/inc");
  roots[0].forms = CTOOL_C_PP_INCLUDE_QUOTED;
  roots[1].directory.text = ctool_string("/");
  roots[1].forms = CTOOL_C_PP_INCLUDE_QUOTED;
  forced.text = ctool_string("/forced-exe.h");
  source.path.text = ctool_string("/primary-exe.cc");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_CUPID;
  request.include_roots = roots;
  request.include_root_count = 2u;
  request.forced_includes = &forced;
  request.forced_include_count = 1u;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || store.read_count != 3u ||
      result.token_count != 20u) {
    (void)fprintf(stderr,
                  "cupid-exe-files: preprocessing failed (%s, %u tokens, "
                  "%u reads)\n",
                  ctool_status_name(status), result.token_count,
                  store.read_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < 5u; index++) {
    ctool_u32 offset = index * 4u;
    if (result.tokens[offset].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
        !string_equal(result.tokens[offset].spelling, "exe") ||
        !string_equal(result.tokens[offset].location.path,
                      expected_path[index]) ||
        result.tokens[offset].pack_alignment != expected_pack[index] ||
        !string_equal(result.tokens[offset + 1u].spelling, "{") ||
        !string_equal(result.tokens[offset + 2u].spelling,
                      expected_body[index]) ||
        !string_equal(result.tokens[offset + 2u].location.path,
                      expected_path[index]) ||
        result.tokens[offset + 2u].pack_alignment != expected_pack[index] ||
        !string_equal(result.tokens[offset + 3u].spelling, "}")) {
      (void)fprintf(stderr, "cupid-exe-files: group %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_job_close(job);
  (void)printf("cupid-exe-files: ok\n");
  return 0;
}

static int run_cupid_exe_errors(void) {
  static const char *const error_text[] = {
      "#exe\n",
      "#exe value\n",
      "#define OPEN {\n#exe OPEN\n",
      "#exe()\n",
      "#exe\n{}\n",
      "#exe \"file.cc\"\n",
      "#exe {\n}\n"};
  static const ctool_c_pp_mode_t error_mode[] = {
      CTOOL_C_PP_MODE_CUPID, CTOOL_C_PP_MODE_CUPID,
      CTOOL_C_PP_MODE_CUPID, CTOOL_C_PP_MODE_CUPID,
      CTOOL_C_PP_MODE_CUPID,
      CTOOL_C_PP_MODE_CUPID,
      CTOOL_C_PP_MODE_C11};
  static const ctool_status_t error_status[] = {
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_UNSUPPORTED};
  static const ctool_u32 error_line[] = {1u, 1u, 2u, 1u, 1u, 1u, 1u};
  static const char post_marker_error_text[] =
      "#define BAD(a, b) a ## b\n"
      "#exe { BAD(+, *) }\n";
  static const char recovery_text[] = "#exe { recovered }\n";
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 case_count =
      (ctool_u32)(sizeof(error_text) / sizeof(error_text[0]));
  ctool_u32 index;

  if (open_job("cupid-exe-errors", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/cupid-exe-errors.cc");
  (void)memset(&request, 0, sizeof(request));
  for (index = 0u; index < case_count; index++) {
    source.contents =
        ctool_bytes(error_text[index], (ctool_u32)strlen(error_text[index]));
    request.mode = error_mode[index];
    (void)memset(&result, 0xa5, sizeof(result));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, index);
    if (status != error_status[index] || result.tokens != NULL ||
        result.token_count != 0u || diagnostic == NULL ||
        diagnostic->code != CTOOL_C_PP_DIAG_CUPID_EXE ||
        !string_equal(diagnostic->path, "/cupid-exe-errors.cc") ||
        diagnostic->line != error_line[index] || diagnostic->column != 1u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr, "cupid-exe-errors: case %u differs (%s)\n",
                    index, ctool_status_name(status));
      (void)ctool_job_render_diagnostics(job);
      ctool_job_close(job);
      return 1;
    }
  }

  source.contents = ctool_bytes(
      post_marker_error_text,
      (ctool_u32)(sizeof(post_marker_error_text) - 1u));
  request.mode = CTOOL_C_PP_MODE_CUPID;
  (void)memset(&result, 0xa5, sizeof(result));
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_preprocess(job, &source, &request, &result);
  diagnostic = ctool_job_diagnostic(job, case_count);
  if (status != CTOOL_ERR_INPUT || result.tokens != NULL ||
      result.token_count != 0u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PP_DIAG_MACRO_PASTE ||
      !string_equal(diagnostic->path, "/cupid-exe-errors.cc") ||
      diagnostic->line != 2u || diagnostic->column != 8u ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr,
                  "cupid-exe-errors: post-marker rollback differs (%s)\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  source.contents =
      ctool_bytes(recovery_text, (ctool_u32)(sizeof(recovery_text) - 1u));
  request.mode = CTOOL_C_PP_MODE_CUPID;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 4u ||
      result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
      !string_equal(result.tokens[0].spelling, "exe") ||
      !string_equal(result.tokens[1].spelling, "{") ||
      !string_equal(result.tokens[2].spelling, "recovered") ||
      !string_equal(result.tokens[3].spelling, "}") ||
      ctool_job_diagnostic_count(job) != case_count + 1u) {
    (void)fprintf(stderr, "cupid-exe-errors: recovery differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("cupid-exe-errors: ok\n");
  return 0;
}

static int run_cupid_exe_scale(void) {
  enum { EXE_COUNT = 256, SOURCE_CAPACITY = 4096 };
  static char source_text[SOURCE_CAPACITY];
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 source_size = 0u;
  ctool_u32 index;

  for (index = 0u; index < EXE_COUNT; index++) {
    if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                    "#exe {}\n") != 0) {
      return 1;
    }
  }
  if (open_job("cupid-exe-scale", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/cupid-exe-scale.cc");
  source.contents = ctool_bytes(source_text, source_size);
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_CUPID;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != EXE_COUNT * 3u) {
    (void)fprintf(stderr,
                  "cupid-exe-scale: preprocessing failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < EXE_COUNT; index++) {
    ctool_u32 offset = index * 3u;
    if (result.tokens[offset].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
        !string_equal(result.tokens[offset].spelling, "exe") ||
        result.tokens[offset].location.line != index + 1u ||
        result.tokens[offset].location.column != 1u ||
        !string_equal(result.tokens[offset + 1u].spelling, "{") ||
        !string_equal(result.tokens[offset + 2u].spelling, "}")) {
      (void)fprintf(stderr, "cupid-exe-scale: marker %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_job_close(job);
  (void)printf("cupid-exe-scale: ok\n");
  return 0;
}

static int run_conditional_expression_errors(void) {
  static const char *const error_text[] = {
      "#if defined\n#endif\n",
      "#if defined()\n#endif\n",
      "#if 1 2\n#endif\n",
      "#if (1\n#endif\n",
      "#if 08\n#endif\n",
      "#if 1.0\n#endif\n",
      "#if 0x10000000000000000\n#endif\n",
      "#if 1 / 0\n#endif\n",
      "#if 1 % 0\n#endif\n",
      "#if 1 << 64\n#endif\n",
      "#if 1 << -1\n#endif\n",
      "#if 9223372036854775807 + 1\n#endif\n",
      "#if 4611686018427387904 * 2\n#endif\n",
      "#if -(-9223372036854775807 - 1)\n#endif\n",
      "#if 1 ? 2\n#endif\n",
      "#if \"text\"\n#endif\n",
      "#define MAKE_DEFINED defined\n#if MAKE_DEFINED(NAME)\n#endif\n",
      "#if defined(__VA_ARGS__)\n#endif\n",
      "#if '\\q'\n#endif\n",
      "#define EMPTY\n#if EMPTY\n#endif\n",
      "#if 1 = 1\n#endif\n",
      "#if 1, 2\n#endif\n",
      "#if 0 && (1 + )\n#endif\n",
      "#if '\\u0001'\n#endif\n",
      "#define A 1\n#define PASS(value) value\n#if PASS(defined(A))\n#endif\n",
      "#if '\\u0041'\n#endif\n",
      "#if '\\uD800'\n#endif\n",
      "#if '\\U00110000'\n#endif\n",
      "#if L'\\x100000000'\n#endif\n",
      "#if U'\\x100000000'\n#endif\n",
      "#if u'\\x10000'\n#endif\n",
      "#if 0b10\n#endif\n",
      "#if 9223372036854775808\n#endif\n",
      "#if '\\e'\n#endif\n",
      "#if 1lL\n#endif\n",
      "#if 18446744073709551616ULL\n#endif\n",
      "#if (1, 2)\n#endif\n",
      "#if 1 ? 2, 3 : 4\n#endif\n"};
  static const ctool_status_t error_status[] = {
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_OVERFLOW, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_OVERFLOW, CTOOL_ERR_OVERFLOW, CTOOL_ERR_OVERFLOW,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_OVERFLOW, CTOOL_ERR_OVERFLOW, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_OVERFLOW, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_OVERFLOW, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT};
  static const ctool_u32 error_code[] = {
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_MACRO_EXPANSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION};
  static const ctool_u32 error_line[] = {
      1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u,
      1u, 1u, 1u, 1u, 2u, 1u, 1u, 2u, 1u, 1u, 1u, 1u, 3u,
      1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u};
  static const ctool_u32 error_column[] = {
      5u, 5u, 7u, 5u, 5u, 5u, 5u, 7u, 7u, 7u, 7u, 25u,
      25u, 5u, 7u, 5u, 5u, 13u, 5u, 1u, 7u, 6u, 15u, 5u, 10u,
      5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 5u, 7u, 10u};
  static const char recovery_text[] =
      "#if 2 + 2 == 4\nrecovered\n#endif\n";
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 case_count =
      (ctool_u32)(sizeof(error_text) / sizeof(error_text[0]));
  ctool_u32 index;

  if (open_job("conditional-errors", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/conditional-errors.c");
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  for (index = 0u; index < case_count; index++) {
    source.contents =
        ctool_bytes(error_text[index], (ctool_u32)strlen(error_text[index]));
    (void)memset(&result, 0xa5, sizeof(result));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, index);
    if (status != error_status[index] || result.tokens != NULL ||
        result.token_count != 0u || diagnostic == NULL ||
        diagnostic->code != error_code[index] ||
        !string_equal(diagnostic->path, "/conditional-errors.c") ||
        diagnostic->line != error_line[index] ||
        diagnostic->column != error_column[index] ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr,
                    "conditional-errors: case %u differs (%s)\n", index,
                    ctool_status_name(status));
      (void)ctool_job_render_diagnostics(job);
      ctool_job_close(job);
      return 1;
    }
  }
  source.contents =
      ctool_bytes(recovery_text, (ctool_u32)(sizeof(recovery_text) - 1u));
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 1u ||
      !string_equal(result.tokens[0].spelling, "recovered") ||
      ctool_job_diagnostic_count(job) != case_count) {
    (void)fprintf(stderr, "conditional-errors: recovery failed\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("conditional-errors: ok\n");
  return 0;
}

static int run_conditional_active_cases(void) {
  typedef struct {
    const char *expression;
    ctool_u32 if_occurrences;
    ctool_u32 elif_occurrences;
    ctool_bool expected_i386;
  } conditional_case_t;
  enum { SOURCE_CAPACITY = 16 * 1024 };
  static const conditional_case_t cases[] = {
#define CUPIDC_PP_CONDITIONAL_CASE(expression, if_count, elif_count, expected) \
  {expression, if_count, elif_count, expected != 0 ? CTOOL_TRUE : CTOOL_FALSE},
#include "cupidc_pp_conditional_cases.inc"
#undef CUPIDC_PP_CONDITIONAL_CASE
  };
  static char source_text[SOURCE_CAPACITY];
  static const char host_text[] =
      "#if defined(_WIN32) && _WIN64 && _MSC_VER >= 1400 && "
      "defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8 && "
      "__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__\n"
      "host_profile_ok\n"
      "#endif\n";
  ctool_c_pp_macro_action_t actions[11];
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 source_size = 0u;
  ctool_u32 if_occurrences = 0u;
  ctool_u32 elif_occurrences = 0u;
  ctool_u32 probe_count = 0u;
  ctool_u32 output_index = 0u;
  ctool_u32 index;
  char expected[48];

  for (index = 0u;
       index < (ctool_u32)(sizeof(cases) / sizeof(cases[0])); index++) {
    if_occurrences += cases[index].if_occurrences;
    elif_occurrences += cases[index].elif_occurrences;
    if (cases[index].if_occurrences != 0u) {
      if (append_text(source_text, SOURCE_CAPACITY, &source_size, "#if ") != 0 ||
          append_text(source_text, SOURCE_CAPACITY, &source_size,
                      cases[index].expression) != 0 ||
          append_text(source_text, SOURCE_CAPACITY, &source_size, "\ncase") != 0 ||
          append_decimal(source_text, SOURCE_CAPACITY, &source_size, index) != 0 ||
          append_text(source_text, SOURCE_CAPACITY, &source_size,
                      "_if_true\n#else\ncase") != 0 ||
          append_decimal(source_text, SOURCE_CAPACITY, &source_size, index) != 0 ||
          append_text(source_text, SOURCE_CAPACITY, &source_size,
                      "_if_false\n#endif\n") != 0) {
        (void)fprintf(stderr,
                      "conditional-active: source fixture overflowed\n");
        return 1;
      }
      probe_count++;
    }
    if (cases[index].elif_occurrences != 0u) {
      if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                      "#if 0\nnot_taken\n#elif ") != 0 ||
          append_text(source_text, SOURCE_CAPACITY, &source_size,
                      cases[index].expression) != 0 ||
          append_text(source_text, SOURCE_CAPACITY, &source_size,
                      "\ncase") != 0 ||
          append_decimal(source_text, SOURCE_CAPACITY, &source_size, index) != 0 ||
          append_text(source_text, SOURCE_CAPACITY, &source_size,
                      "_elif_true\n#else\ncase") != 0 ||
          append_decimal(source_text, SOURCE_CAPACITY, &source_size, index) != 0 ||
          append_text(source_text, SOURCE_CAPACITY, &source_size,
                      "_elif_false\n#endif\n") != 0) {
        (void)fprintf(stderr,
                      "conditional-active: source fixture overflowed\n");
        return 1;
      }
      probe_count++;
    }
  }
  if ((ctool_u32)(sizeof(cases) / sizeof(cases[0])) != 21u ||
      if_occurrences != 97u || elif_occurrences != 4u ||
      probe_count != 22u) {
    (void)fprintf(stderr,
                  "conditional-active: checked manifest totals differ\n");
    return 1;
  }

  if (open_job("conditional-active", &adapter, &job) != 0) {
    return 1;
  }
  actions[0].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[0].name = ctool_string("__ORDER_LITTLE_ENDIAN__");
  actions[0].replacement = ctool_string("1234");
  actions[1].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[1].name = ctool_string("__ORDER_BIG_ENDIAN__");
  actions[1].replacement = ctool_string("4321");
  actions[2].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[2].name = ctool_string("__ORDER_PDP_ENDIAN__");
  actions[2].replacement = ctool_string("3412");
  actions[3].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[3].name = ctool_string("__BYTE_ORDER__");
  actions[3].replacement = ctool_string("__ORDER_LITTLE_ENDIAN__");
  actions[4].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[4].name = ctool_string("__SIZEOF_POINTER__");
  actions[4].replacement = ctool_string("4");
  actions[5].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[5].name = ctool_string("OPL_ENABLE_STEREOEXT");
  actions[5].replacement = ctool_string("0");
  actions[6].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[6].name = ctool_string("OPL_QUIRK_CHANNELSAMPLEDELAY");
  actions[6].replacement = ctool_string("(!OPL_ENABLE_STEREOEXT)");
  actions[7].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[7].name = ctool_string("__bool_true_false_are_defined");
  actions[7].replacement = ctool_string("1");
  actions[8].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[8].name = ctool_string("ORIGCODE");
  actions[8].replacement = ctool_string("1");
  actions[9].kind = CTOOL_C_PP_MACRO_UNDEF;
  actions[9].name = ctool_string("ORIGCODE");
  actions[9].replacement = ctool_string("");
  actions[10].kind = CTOOL_C_PP_MACRO_UNDEF;
  actions[10].name = ctool_string("_WIN32");
  actions[10].replacement = ctool_string("");
  source.path.text = ctool_string("/conditional-active.c");
  source.contents =
      ctool_bytes(source_text, source_size);
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  request.macro_actions = actions;
  request.macro_action_count = 11u;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != probe_count) {
    (void)fprintf(stderr,
                  "conditional-active: OS profile failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(cases) / sizeof(cases[0])); index++) {
    if (cases[index].if_occurrences != 0u) {
      int written = snprintf(
          expected, sizeof(expected), "case%u_if_%s", index,
          cases[index].expected_i386 == CTOOL_TRUE ? "true" : "false");
      if (written < 0 || (size_t)written >= sizeof(expected) ||
          !string_equal(result.tokens[output_index].spelling, expected) ||
          !string_equal(result.tokens[output_index].location.path,
                        "/conditional-active.c")) {
        (void)fprintf(stderr,
                      "conditional-active: #if case %u differs\n", index);
        ctool_job_close(job);
        return 1;
      }
      output_index++;
    }
    if (cases[index].elif_occurrences != 0u) {
      int written = snprintf(
          expected, sizeof(expected), "case%u_elif_%s", index,
          cases[index].expected_i386 == CTOOL_TRUE ? "true" : "false");
      if (written < 0 || (size_t)written >= sizeof(expected) ||
          !string_equal(result.tokens[output_index].spelling, expected) ||
          !string_equal(result.tokens[output_index].location.path,
                        "/conditional-active.c")) {
        (void)fprintf(stderr,
                      "conditional-active: #elif case %u differs\n", index);
        ctool_job_close(job);
        return 1;
      }
      output_index++;
    }
  }
  if (output_index != result.token_count) {
    (void)fprintf(stderr,
                  "conditional-active: OS profile output count differs\n");
    ctool_job_close(job);
    return 1;
  }

  actions[4].replacement = ctool_string("8");
  actions[8].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[8].name = ctool_string("_WIN32");
  actions[8].replacement = ctool_string("1");
  actions[9].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[9].name = ctool_string("_WIN64");
  actions[9].replacement = ctool_string("1");
  actions[10].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[10].name = ctool_string("_MSC_VER");
  actions[10].replacement = ctool_string("1930");
  source.path.text = ctool_string("/conditional-host.c");
  source.contents =
      ctool_bytes(host_text, (ctool_u32)(sizeof(host_text) - 1u));
  request.hosted_environment = CTOOL_TRUE;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 1u ||
      !string_equal(result.tokens[0].spelling, "host_profile_ok") ||
      !string_equal(result.tokens[0].location.path,
                    "/conditional-host.c") ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr,
                  "conditional-active: hosted profile failed\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("conditional-active: ok\n");
  return 0;
}

static int run_conditional_expression_scale(void) {
  enum {
    TERM_COUNT = 10000,
    NESTING_LIMIT = 256,
    SOURCE_CAPACITY = 128 * 1024
  };
  static char source_text[SOURCE_CAPACITY];
  static char limit_text[SOURCE_CAPACITY];
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 source_size = 0u;
  ctool_u32 limit_size = 0u;
  ctool_u32 index;

  if (append_text(source_text, SOURCE_CAPACITY, &source_size, "#if ") != 0) {
    return 1;
  }
  for (index = 0u; index < TERM_COUNT; index++) {
    if ((index != 0u &&
         append_character(source_text, SOURCE_CAPACITY, &source_size, '+') !=
             0) ||
        append_character(source_text, SOURCE_CAPACITY, &source_size, '1') !=
            0) {
      return 1;
    }
  }
  if (append_text(source_text, SOURCE_CAPACITY, &source_size, " == ") != 0 ||
      append_decimal(source_text, SOURCE_CAPACITY, &source_size, TERM_COUNT) !=
          0 ||
      append_text(source_text, SOURCE_CAPACITY, &source_size,
                  "\nchain_ok\n#endif\n#if ") != 0) {
    return 1;
  }
  for (index = 0u; index < NESTING_LIMIT; index++) {
    if (append_character(source_text, SOURCE_CAPACITY, &source_size, '(') !=
        0) {
      return 1;
    }
  }
  if (append_character(source_text, SOURCE_CAPACITY, &source_size, '1') !=
      0) {
    return 1;
  }
  for (index = 0u; index < NESTING_LIMIT; index++) {
    if (append_character(source_text, SOURCE_CAPACITY, &source_size, ')') !=
        0) {
      return 1;
    }
  }
  if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                  "\nnesting_ok\n#endif\n"
                  "#define SELF SELF\n"
                  "#if SELF\nnot_taken\n#else\nrecursion_ok\n#endif\n") !=
      0) {
    return 1;
  }

  if (append_text(limit_text, SOURCE_CAPACITY, &limit_size, "#if ") != 0) {
    return 1;
  }
  for (index = 0u; index < NESTING_LIMIT + 1u; index++) {
    if (append_character(limit_text, SOURCE_CAPACITY, &limit_size, '(') != 0) {
      return 1;
    }
  }
  if (append_character(limit_text, SOURCE_CAPACITY, &limit_size, '1') != 0) {
    return 1;
  }
  for (index = 0u; index < NESTING_LIMIT + 1u; index++) {
    if (append_character(limit_text, SOURCE_CAPACITY, &limit_size, ')') != 0) {
      return 1;
    }
  }
  if (append_text(limit_text, SOURCE_CAPACITY, &limit_size,
                  "\n#endif\n") != 0) {
    return 1;
  }

  if (open_job("conditional-scale", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/conditional-scale.c");
  source.contents = ctool_bytes(source_text, source_size);
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 3u ||
      !string_equal(result.tokens[0].spelling, "chain_ok") ||
      !string_equal(result.tokens[1].spelling, "nesting_ok") ||
      !string_equal(result.tokens[2].spelling, "recursion_ok")) {
    (void)fprintf(stderr,
                  "conditional-scale: positive scale failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/conditional-depth.c");
  source.contents = ctool_bytes(limit_text, limit_size);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_preprocess(job, &source, &request, &result);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (status != CTOOL_ERR_LIMIT || result.tokens != NULL ||
      result.token_count != 0u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION ||
      !string_equal(diagnostic->path, "/conditional-depth.c") ||
      diagnostic->line != 1u || diagnostic->column != 261u ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr,
                  "conditional-scale: nesting limit differs (%s)\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("conditional-scale: ok\n");
  return 0;
}

static int run_function_macros(void) {
  static char source_text[] =
      "owned\n"
      "#define EMPTY()\n"
      "#define ZERO() 5\n"
      "#define ID(value) value\n"
      "#define CALL(function, argument) function(argument)\n"
      "#define CALL_ALIAS CALL\n"
      "#define PAIR(left, right) left right\n"
      "#define REPEAT(first, unused, last) first first last\n"
      "#define NEED_TWO(left, right) left right\n"
      "#define SHOW_EMPTY(left, right) (left)(right)\n"
      "#define ERASE(value)\n"
      "#define SELF(value) SELF(value)\n"
      "#define MUTUAL_A(value) MUTUAL_B(value)\n"
      "#define MUTUAL_B(value) MUTUAL_A(value)\n"
      "#define SAME(left, right) left + right\n"
      "#define SAME(left, right) left  +  right\n"
      "#define PICK_22(p01, p02, p03, p04, p05, p06, p07, p08, p09, "
      "p10, p11, \\\n"
      "                p12, p13, p14, p15, p16, p17, p18, p19, p20, "
      "p21, p22) p01 p11 p22\n"
      "unused ZERO\n"
      "empty before EMPTY() after\n"
      "zero ZERO()\n"
      "nested PAIR(call(1, 2), (3, 4))\n"
      "alias CALL_ALIAS(ID, 7)\n"
      "prescan CALL(ID, ZERO())\n"
      "repeat REPEAT(alpha, NEED_TWO(1), omega)\n"
      "vacant SHOW_EMPTY(, tail) SHOW_EMPTY(head, )\n"
      "erased before ERASE(anything) after\n"
      "same SAME(8, 9)\n"
      "wide PICK_22(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, "
      "15, 16, 17, 18, 19, 20, 21, 22)\n"
      "self SELF(10)\n"
      "mutual MUTUAL_A(11)\n"
      "#define COMMA ,\n"
      "#define TWO_VALUES(left, right) left right\n"
      "#define FORWARD(value) TWO_VALUES(value)\n"
      "introduced FORWARD(12 COMMA 13)\n"
      "#define F(value) value\n"
      "#define G F\n"
      "hidden F(G)(14)\n"
      "#define TRAILING_ELLIPSIS(value) value ...\n"
      "ellipsis TRAILING_ELLIPSIS(15)\n"
      "#define H(value) J(value)\n"
      "#define J H\n"
      "intersection J(16)\n";
  static char source_path[] = "/function.c";
  static const char *const expected_text[] = {
      "owned", "unused", "ZERO", "empty", "before", "after", "zero",
      "5", "nested", "call", "(", "1", ",", "2", ")", "(", "3",
      ",", "4", ")", "alias", "7", "prescan", "5", "repeat",
      "alpha", "alpha", "omega", "vacant", "(", ")", "(", "tail",
      ")", "(", "head", ")", "(", ")", "erased", "before", "after",
      "same", "8", "+", "9", "wide", "1", "11", "22", "self",
      "SELF", "(", "10", ")", "mutual", "MUTUAL_A", "(", "11", ")",
      "introduced", "12", "13", "hidden", "F", "(", "14", ")",
      "ellipsis", "15", "...", "intersection", "H", "(", "16", ")"};
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;

  if (open_job("function-macros", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text.data = source_path;
  source.path.text.size = (ctool_u32)(sizeof(source_path) - 1u);
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;

  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK ||
      result.token_count !=
          (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
    (void)fprintf(stderr,
                  "function-macros: preprocessing failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
        !string_equal(result.tokens[index].location.path, "/function.c")) {
      (void)fprintf(stderr, "function-macros: token %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }
  if (result.tokens[0].location.line != 1u ||
      result.tokens[0].location.column != 1u ||
      result.tokens[2].location.line != 19u ||
      result.tokens[2].location.column != 8u ||
      result.tokens[7].location.line != 21u ||
      result.tokens[21].location.line != 23u ||
      result.tokens[23].location.line != 24u ||
      result.tokens[48].location.line != 29u ||
      result.tokens[51].location.line != 30u ||
      result.tokens[56].location.line != 31u) {
    (void)fprintf(stderr, "function-macros: source locations differ\n");
    ctool_job_close(job);
    return 1;
  }
  source_text[0] = 'X';
  source_path[1] = 'X';
  if (!string_equal(result.tokens[0].spelling, "owned") ||
      !string_equal(result.tokens[7].spelling, "5") ||
      !string_equal(result.tokens[56].location.path, "/function.c")) {
    (void)fprintf(stderr,
                  "function-macros: result retained a borrowed source view\n");
    ctool_job_close(job);
    return 1;
  }
  source_text[0] = 'o';
  source_path[1] = 'f';

  ctool_job_close(job);
  (void)printf("function-macros: ok\n");
  return 0;
}

static int run_function_macro_scale(void) {
  enum {
    PARAMETER_COUNT = 256,
    NOISE_TOKEN_COUNT = 10000,
    SOURCE_CAPACITY = 128 * 1024
  };
  static char source_text[SOURCE_CAPACITY];
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_u32 source_size = 0u;
  ctool_u32 index;
  ctool_status_t status;

  if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                  "#define SCALE(") != 0) {
    return 1;
  }
  for (index = 0u; index < PARAMETER_COUNT; index++) {
    if ((index != 0u &&
         append_text(source_text, SOURCE_CAPACITY, &source_size, ", ") != 0) ||
        append_parameter_name(source_text, SOURCE_CAPACITY, &source_size,
                              index) != 0) {
      return 1;
    }
  }
  if (append_text(source_text, SOURCE_CAPACITY, &source_size, ") ") != 0) {
    return 1;
  }
  for (index = 0u; index < NOISE_TOKEN_COUNT; index++) {
    if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                    "noise ") != 0) {
      return 1;
    }
  }
  if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                  "p000 p255\nSCALE(") != 0) {
    return 1;
  }
  for (index = 0u; index < PARAMETER_COUNT; index++) {
    if ((index != 0u &&
         append_text(source_text, SOURCE_CAPACITY, &source_size, ", ") != 0) ||
        append_decimal(source_text, SOURCE_CAPACITY, &source_size, index) !=
            0) {
      return 1;
    }
  }
  if (append_text(source_text, SOURCE_CAPACITY, &source_size, ")\n") != 0) {
    return 1;
  }

  if (open_job("function-scale", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/function-scale.c");
  source.contents = ctool_bytes(source_text, source_size);
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK ||
      result.token_count != NOISE_TOKEN_COUNT + 2u ||
      !string_equal(result.tokens[0].spelling, "noise") ||
      !string_equal(result.tokens[NOISE_TOKEN_COUNT - 1u].spelling,
                    "noise") ||
      !string_equal(result.tokens[NOISE_TOKEN_COUNT].spelling, "0") ||
      !string_equal(result.tokens[NOISE_TOKEN_COUNT + 1u].spelling, "255")) {
    (void)fprintf(stderr,
                  "function-scale: bounded expansion failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("function-scale: ok\n");
  return 0;
}

static int run_macro_operators(void) {
  static char source_text[] =
      "owned\n"
      "#define VALUE 17\n"
      "#define A b\n"
      "#define PREFIX VA\n"
      "#define SUFFIX LUE\n"
      "#define prefixed 88\n"
      "#define objectName 99\n"
      "#define commandName 123\n"
      "#define TARGET(value) value\n"
      "#define STR(value) #value\n"
      "#define EXPAND_STR(value) STR(value)\n"
      "#define EMPTY_OBJECT\n"
      "#define EMPTY_FUNCTION()\n"
      "#define F1(value) a value+b\n"
      "#define F4(value) a/**/value+b\n"
      "#define TRAIL(value) a value\n"
      "#define PASTE_EMPTY(value) a value##value+b\n"
      "#define PASTE_EMPTY_TRAIL(value) a value##value\n"
      "#define VSTR(...) #__VA_ARGS__\n"
      "#define CAT(left, right) left ## right\n"
      "#define EXPAND_CAT(left, right) CAT(left, right)\n"
      "#define TRI(left, middle, right) left ## middle ## right\n"
      "#define PUNCT(left, right) left ## right\n"
      "#define WIDE(prefix, literal) prefix ## literal\n"
      "#define VCAT(prefix, ...) prefix ## __VA_ARGS__\n"
      "#define LIST(head, ...) head __VA_ARGS__\n"
      "#define FORWARD(call, ...) call(__VA_ARGS__)\n"
      "#define LOG(fmt, ...) log(fmt, ##__VA_ARGS__)\n"
      "#define ONLY(...) before, ##__VA_ARGS__\n"
      "#define WORD expanded\n"
      "#define GNU_RAW(...) VSTR(prefix , ##__VA_ARGS__)\n"
      "#define FOUR(number) h ## number h ## number h ## number h ## number\n"
      "#define RAW_AND_EXPANDED(value) #value value\n"
      "#define PASTED_AND_EXPANDED(value) pre ## value value\n"
      "#define INTEGER_SUFFIX(value) value ## ULL\n"
      "#define SPLICED(value) \\\n"
      "  #value value ## Tail\n"
      "#define joinedTail 66\n"
      "#define F() CAT(F, )()\n"
      "#define PACKED(...) struct __VA_ARGS__ packed\n"
      "#define ACTIVE(a, b, c, d, ...) a b c d __VA_ARGS__\n"
      "#define SAME_V(first, ...) first __VA_ARGS__\n"
      "#define SAME_V(first, ...) first  __VA_ARGS__\n"
      "#define DSTR(value) %: value\n"
      "#define DCAT(left, right) left %:%: right\n"
      "%:define DDIRECT(value) %: value\n"
      "#define HASH #\n"
      "#define OBJECT_JOIN object ## Name\n"
      "#define RECURSE() RE ## CURSE()\n"
      "#define HASH_HASH # ## #\n"
      "#define JOIN_TEXT(left, right) EXPAND_STR(left HASH_HASH right)\n"
      "literal STR(VALUE + \"a\\\\b\")\n"
      "expanded EXPAND_STR(VALUE)\n"
      "joined EXPAND_STR(a+A)\n"
      "separated EXPAND_STR(a A)\n"
      "empty_param1 EXPAND_STR(F1())\n"
      "empty_param4 EXPAND_STR(F4())\n"
      "empty_object_space EXPAND_STR(a EMPTY_OBJECT+b)\n"
      "empty_function_space EXPAND_STR(a EMPTY_FUNCTION()+b)\n"
      "trailing_space EXPAND_STR(TRAIL()+b)\n"
      "placemarker_space EXPAND_STR(PASTE_EMPTY())\n"
      "placemarker_trailing EXPAND_STR(PASTE_EMPTY_TRAIL()+b)\n"
      "space STR(alpha/**/ beta   +gamma)\n"
      "variadic VSTR(alpha, beta)\n"
      "raw CAT(PREFIX, SUFFIX)\n"
      "expanded_cat EXPAND_CAT(PREFIX, SUFFIX)\n"
      "suffix CAT(TAR, GET)(7)\n"
      "empty_left CAT(, tail)\n"
      "empty_right CAT(head, )\n"
      "empty_both before CAT(, ) after\n"
      "triple TRI(pre, fix, ed)\n"
      "punct PUNCT(+, =)\n"
      "wide WIDE(L, \"text\")\n"
      "vcat VCAT(pre, fix, tail)\n"
      "list LIST(first, second, third)\n"
      "forward FORWARD(target, VALUE, 2)\n"
      "gnu_omitted LOG(\"x\")\n"
      "gnu_empty LOG(\"x\", )\n"
      "gnu_values LOG(\"x\", VALUE, 2)\n"
      "gnu_raw GNU_RAW(WORD)\n"
      "only_empty ONLY()\n"
      "only_values ONLY(VALUE, 2)\n"
      "many LIST(first, second, third, fourth, fifth, sixth, seventh, "
      "eighth, ninth, tenth, eleventh, twelfth)\n"
      "active ACTIVE(q1, q2, q3, q4, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10)\n"
      "same_v SAME_V(one, two)\n"
      "four FOUR(2)\n"
      "mixed_string RAW_AND_EXPANDED(VALUE)\n"
      "mixed_paste PASTED_AND_EXPANDED(SUFFIX)\n"
      "number_suffix INTEGER_SUFFIX(0xfffffffff)\n"
      "spliced SPLICED(joined)\n"
      "marker_hide F()\n"
      "packed PACKED({ int x; int y; }, next)\n"
      "packed_multiline PACKED (\n"
      "  { int z; }\n"
      ")\n"
      "digraph_string DSTR(digraph value)\n"
      "digraph_cat DCAT(VA, LUE)\n"
      "digraph_directive DDIRECT(direct value)\n"
      "literal_hash HASH include\n"
      "object OBJECT_JOIN\n"
      "command COMMAND\n"
      "recurse RECURSE()\n"
      "hash_hash JOIN_TEXT(x, y)\n"
      "#if 0\n"
      "__VA_ARGS__\n"
      "#endif\n"
      "inactive_ok\n"
      "#define DOTS(...) ...\n"
      "dots DOTS(ignored)\n";
  static char source_path[] = "/macro-operators.c";
  static const char strict_text[] =
      "#define STRICT(...) left, ##__VA_ARGS__\n"
      "#define FIXED(head, ...) head __VA_ARGS__\n"
      "strict STRICT() fixed FIXED(one,)\n";
  static const char *const expected_text[] = {
      "owned",
      "literal", "\"VALUE + \\\"a\\\\\\\\b\\\"\"",
      "expanded", "\"17\"",
      "joined", "\"a+b\"",
      "separated", "\"a b\"",
      "empty_param1", "\"a +b\"",
      "empty_param4", "\"a +b\"",
      "empty_object_space", "\"a +b\"",
      "empty_function_space", "\"a +b\"",
      "trailing_space", "\"a +b\"",
      "placemarker_space", "\"a +b\"",
      "placemarker_trailing", "\"a +b\"",
      "space", "\"alpha beta +gamma\"",
      "variadic", "\"alpha, beta\"",
      "raw", "PREFIXSUFFIX",
      "expanded_cat", "17",
      "suffix", "7",
      "empty_left", "tail",
      "empty_right", "head",
      "empty_both", "before", "after",
      "triple", "88",
      "punct", "+=",
      "wide", "L\"text\"",
      "vcat", "prefix", ",", "tail",
      "list", "first", "second", ",", "third",
      "forward", "target", "(", "17", ",", "2", ")",
      "gnu_omitted", "log", "(", "\"x\"", ")",
      "gnu_empty", "log", "(", "\"x\"", ",", ")",
      "gnu_values", "log", "(", "\"x\"", ",", "17", ",", "2", ")",
      "gnu_raw", "\"prefix ,WORD\"",
      "only_empty", "before",
      "only_values", "before", ",", "17", ",", "2",
      "many", "first", "second", ",", "third", ",", "fourth", ",",
      "fifth", ",", "sixth", ",", "seventh", ",", "eighth", ",",
      "ninth", ",", "tenth", ",", "eleventh", ",", "twelfth",
      "active", "q1", "q2", "q3", "q4", "1", ",", "2", ",", "3",
      ",", "4", ",", "5", ",", "6", ",", "7", ",", "8", ",",
      "9", ",", "10",
      "same_v", "one", "two",
      "four", "h2", "h2", "h2", "h2",
      "mixed_string", "\"VALUE\"", "17",
      "mixed_paste", "preSUFFIX", "LUE",
      "number_suffix", "0xfffffffffULL",
      "spliced", "\"joined\"", "66",
      "marker_hide", "F", "(", ")",
      "packed", "struct", "{", "int", "x", ";", "int", "y", ";",
      "}", ",", "next", "packed",
      "packed_multiline", "struct", "{", "int", "z", ";", "}",
      "packed",
      "digraph_string", "\"digraph value\"",
      "digraph_cat", "17",
      "digraph_directive", "\"direct value\"",
      "literal_hash", "#", "include",
      "object", "99",
      "command", "123",
      "recurse", "RECURSE", "(", ")",
      "hash_hash", "\"x ## y\"",
      "inactive_ok",
      "dots", "..."};
  static const char *const strict_expected[] = {
      "strict", "left", ",", "fixed", "one"};
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_macro_action_t action;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;
  ctool_u32 pasted_index = 0xffffffffu;
  ctool_u32 argument_index = 0xffffffffu;
  char *borrowed_paste;

  if (open_job("macro-operators", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text.data = source_path;
  source.path.text.size = (ctool_u32)(sizeof(source_path) - 1u);
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  request.gnu_extensions = CTOOL_TRUE;
  action.kind = CTOOL_C_PP_MACRO_DEFINE;
  action.name = ctool_string("COMMAND");
  action.replacement = ctool_string("command ## Name");
  request.macro_actions = &action;
  request.macro_action_count = 1u;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK ||
      result.token_count !=
          (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
    (void)fprintf(stderr,
                  "macro-operators: preprocessing failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
        !string_equal(result.tokens[index].location.path,
                      "/macro-operators.c")) {
      (void)fprintf(stderr, "macro-operators: token %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
    if (strcmp(expected_text[index], "PREFIXSUFFIX") == 0) {
      pasted_index = index;
    }
    if (strcmp(expected_text[index], "list") == 0) {
      argument_index = index + 1u;
    }
  }
  borrowed_paste = strstr(source_text, "raw CAT(PREFIX, SUFFIX)");
  if (borrowed_paste == NULL || pasted_index == 0xffffffffu) {
    (void)fprintf(stderr, "macro-operators: paste ownership setup failed\n");
    ctool_job_close(job);
    return 1;
  }
  if (argument_index == 0xffffffffu ||
      result.tokens[2].location.line !=
          text_line_of(source_text, "literal STR(") ||
      result.tokens[2].location.column != 9u ||
      result.tokens[pasted_index].location.line !=
          text_line_of(source_text, "raw CAT(") ||
      result.tokens[pasted_index].location.column != 5u ||
      result.tokens[argument_index].location.line !=
          text_line_of(source_text, "list LIST(") ||
      result.tokens[argument_index].location.column != 11u) {
    (void)fprintf(stderr,
                  "macro-operators: generated/argument locations differ\n");
    ctool_job_close(job);
    return 1;
  }
  source_text[0] = 'X';
  borrowed_paste[8] = 'X';
  source_path[1] = 'X';
  if (!string_equal(result.tokens[0].spelling, "owned") ||
      !string_equal(result.tokens[2].spelling,
                    "\"VALUE + \\\"a\\\\\\\\b\\\"\"") ||
      !string_equal(result.tokens[pasted_index].spelling, "PREFIXSUFFIX") ||
      !string_equal(result.tokens[result.token_count - 1u].spelling, "...") ||
      !string_equal(result.tokens[result.token_count - 1u].location.path,
                    "/macro-operators.c")) {
    (void)fprintf(stderr,
                  "macro-operators: generated tokens retained borrowed input\n");
    ctool_job_close(job);
    return 1;
  }
  source_text[0] = 'o';
  borrowed_paste[8] = 'P';
  source_path[1] = 'm';

  source.path.text = ctool_string("/macro-strict.c");
  source.contents =
      ctool_bytes(strict_text, (ctool_u32)(sizeof(strict_text) - 1u));
  request.gnu_extensions = CTOOL_FALSE;
  request.macro_actions = (const ctool_c_pp_macro_action_t *)0;
  request.macro_action_count = 0u;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK ||
      result.token_count !=
          (ctool_u32)(sizeof(strict_expected) / sizeof(strict_expected[0]))) {
    (void)fprintf(stderr,
                  "macro-operators: strict preprocessing failed (%s)\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (!string_equal(result.tokens[index].spelling,
                      strict_expected[index])) {
      (void)fprintf(stderr,
                    "macro-operators: strict token %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }

  ctool_job_close(job);
  (void)printf("macro-operators: ok\n");
  return 0;
}

static int run_macro_gnu(void) {
  static const char source_text[] =
      "#define EMPTY\n"
      "#define EXPANDED expanded\n"
      "#define END(fmt, ...) call(fmt, ##__VA_ARGS__)\n"
      "#define MID(fmt, ...) call(fmt, ##__VA_ARGS__, tail)\n"
      "#define ORD(fmt, ...) call(fmt, __VA_ARGS__)\n"
      "#define STR(...) #__VA_ARGS__\n"
      "#define RAW_JOIN(...) STR(p, ## __VA_ARGS__)\n"
      "#define WRAP(value) STR(value)\n"
      "#define OMIT_SPACE(...) a ,##__VA_ARGS__+b\n"
      "omitted END(x)\n"
      "explicit END(x,)\n"
      "comment END(x, /*empty*/)\n"
      "expanded_empty END(x, EMPTY)\n"
      "values END(x, 1, 2)\n"
      "mid_omitted MID(x)\n"
      "mid_explicit MID(x,)\n"
      "mid_expanded_empty MID(x, EMPTY)\n"
      "mid_values MID(x, 1, 2)\n"
      "ordinary ORD(x)\n"
      "nonempty_space RAW_JOIN(EXPANDED)\n"
      "omitted_space WRAP(OMIT_SPACE())\n";
  static const char *const expected_text[] = {
      "omitted", "call", "(", "x", ")",
      "explicit", "call", "(", "x", ",", ")",
      "comment", "call", "(", "x", ",", ")",
      "expanded_empty", "call", "(", "x", ",", ")",
      "values", "call", "(", "x", ",", "1", ",", "2", ")",
      "mid_omitted", "call", "(", "x", ",", "tail", ")",
      "mid_explicit", "call", "(", "x", ",", ",", "tail", ")",
      "mid_expanded_empty", "call", "(", "x", ",", ",", "tail", ")",
      "mid_values", "call", "(", "x", ",", "1", ",", "2", ",",
      "tail", ")",
      "ordinary", "call", "(", "x", ",", ")",
      "nonempty_space", "\"p,EXPANDED\"",
      "omitted_space", "\"a+b\""};
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;

  if (open_job("macro-gnu", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/macro-gnu.c");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  request.gnu_extensions = CTOOL_TRUE;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK ||
      result.token_count !=
          (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
    (void)fprintf(stderr,
                  "macro-gnu: preprocessing failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
        !string_equal(result.tokens[index].location.path, "/macro-gnu.c")) {
      (void)fprintf(stderr, "macro-gnu: token %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_job_close(job);
  (void)printf("macro-gnu: ok\n");
  return 0;
}

static int run_macro_operator_scale(void) {
  enum {
    PASTE_HALF_SIZE = 16384,
    STRINGIFY_SIZE = 32768,
    VARIADIC_COUNT = 2048,
    SOURCE_CAPACITY = 128 * 1024
  };
  static char source_text[SOURCE_CAPACITY];
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_u32 source_size = 0u;
  ctool_u32 index;
  ctool_u32 output_index;
  ctool_status_t status;

  if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                  "#define CAT(left, right) left ## right\n"
                  "#define STR(value) #value\n"
                  "#define FORWARD(...) __VA_ARGS__\nCAT(") != 0) {
    return 1;
  }
  for (index = 0u; index < PASTE_HALF_SIZE; index++) {
    if (append_character(source_text, SOURCE_CAPACITY, &source_size, 'a') !=
        0) {
      return 1;
    }
  }
  if (append_character(source_text, SOURCE_CAPACITY, &source_size, ',') !=
      0) {
    return 1;
  }
  for (index = 0u; index < PASTE_HALF_SIZE; index++) {
    if (append_character(source_text, SOURCE_CAPACITY, &source_size, 'b') !=
        0) {
      return 1;
    }
  }
  if (append_text(source_text, SOURCE_CAPACITY, &source_size, ")\nSTR(") !=
      0) {
    return 1;
  }
  for (index = 0u; index < STRINGIFY_SIZE; index++) {
    if (append_character(source_text, SOURCE_CAPACITY, &source_size, 'c') !=
        0) {
      return 1;
    }
  }
  if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                  ")\nFORWARD(") != 0) {
    return 1;
  }
  for (index = 0u; index < VARIADIC_COUNT; index++) {
    if ((index != 0u &&
         append_character(source_text, SOURCE_CAPACITY, &source_size, ',') !=
             0) ||
        append_character(source_text, SOURCE_CAPACITY, &source_size, 'v') !=
            0) {
      return 1;
    }
  }
  if (append_text(source_text, SOURCE_CAPACITY, &source_size, ")\n") != 0) {
    return 1;
  }

  if (open_job("macro-operator-scale", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/macro-operator-scale.c");
  source.contents = ctool_bytes(source_text, source_size);
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK ||
      result.token_count != 2u + VARIADIC_COUNT * 2u - 1u ||
      result.tokens[0].kind != CTOOL_C_PP_TOKEN_IDENTIFIER ||
      result.tokens[0].spelling.size != PASTE_HALF_SIZE * 2u ||
      result.tokens[1].kind != CTOOL_C_PP_TOKEN_STRING ||
      result.tokens[1].spelling.size != STRINGIFY_SIZE + 2u) {
    (void)fprintf(stderr,
                  "macro-operator-scale: preprocessing failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < PASTE_HALF_SIZE * 2u; index++) {
    char expected = index < PASTE_HALF_SIZE ? 'a' : 'b';
    if (result.tokens[0].spelling.data[index] != expected) {
      (void)fprintf(stderr,
                    "macro-operator-scale: pasted byte %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }
  if (result.tokens[1].spelling.data[0] != '"' ||
      result.tokens[1].spelling.data[STRINGIFY_SIZE + 1u] != '"') {
    (void)fprintf(stderr,
                  "macro-operator-scale: stringification quotes differ\n");
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < STRINGIFY_SIZE; index++) {
    if (result.tokens[1].spelling.data[index + 1u] != 'c') {
      (void)fprintf(stderr,
                    "macro-operator-scale: stringified byte %u differs\n",
                    index);
      ctool_job_close(job);
      return 1;
    }
  }
  for (output_index = 2u; output_index < result.token_count;
       output_index++) {
    const char *expected = (output_index & 1u) == 0u ? "v" : ",";
    if (!string_equal(result.tokens[output_index].spelling, expected)) {
      (void)fprintf(stderr,
                    "macro-operator-scale: variadic token %u differs\n",
                    output_index);
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_job_close(job);
  (void)printf("macro-operator-scale: ok\n");
  return 0;
}

static int run_macro_active_cases(const char *host_root) {
  static const char source_text[] =
      "#define X86_ACTIVE_CASE(name, mode, mnemonic, size, ...) "
      "case_marker __VA_ARGS__\n"
      "#include \"tests/x86_active_cases.inc\"\n"
      "#include \"tests/x86_inline_cases.inc\"\n";
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 active_count = 0u;
  ctool_u32 inline_count = 0u;
  ctool_u32 index;

  if (open_job_at_root("macro-active-cases", host_root, &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/macro-active-cases.c");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "macro-active-cases: preprocessing failed (%s)\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (string_equal(result.tokens[index].spelling, "case_marker")) {
      if (string_equal(result.tokens[index].location.path,
                       "/tests/x86_active_cases.inc")) {
        active_count++;
      } else if (string_equal(result.tokens[index].location.path,
                              "/tests/x86_inline_cases.inc")) {
        inline_count++;
      } else {
        (void)fprintf(stderr,
                      "macro-active-cases: marker path is not owned input\n");
        ctool_job_close(job);
        return 1;
      }
    }
  }
  if (active_count != 185u || inline_count != 129u) {
    (void)fprintf(stderr,
                  "macro-active-cases: expected 185+129 calls, got %u+%u\n",
                  active_count, inline_count);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("macro-active-cases: ok\n");
  return 0;
}

static int run_function_macro_errors(void) {
  static const char *const error_text[] = {
      "#define BAD(1) 1\n",
      "#define BAD(x, ) x\n",
      "#define DUP(x, x) x\n",
      "#define F(__VA_ARGS__) 7\n",
      "#define F(x) __VA_ARGS__\n",
      "#define OBJECT __VA_ARGS__\n",
      "#define __VA_ARGS__(x) x\n",
      "prefix\n#define TWO(a, b) a b\nTWO(1)\n",
      "#define TWO(a, b) a b\nTWO(1, 2, 3)\n",
      "#define ID(x) x\nID((1, 2)\n",
      ("#define SAME(left, right) left + right\n"
       "#define SAME(x, y) x + y\n"),
      ("#define TIGHT(value) value+value\n"
       "#define TIGHT(value) value + value\n")};
  static const ctool_status_t error_status[] = {
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      };
  static const ctool_u32 error_code[] = {
      CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_ARGUMENTS,
      CTOOL_C_PP_DIAG_MACRO_ARGUMENTS,
      CTOOL_C_PP_DIAG_MACRO_ARGUMENTS,
      CTOOL_C_PP_DIAG_MACRO_REDEFINITION,
      CTOOL_C_PP_DIAG_MACRO_REDEFINITION};
  static const ctool_u32 error_line[] = {1u, 1u, 1u, 1u, 1u, 1u, 1u,
                                        3u, 2u, 2u, 2u, 2u};
  static const ctool_u32 error_column[] = {13u, 16u, 16u, 11u, 14u, 16u,
                                          9u, 1u, 1u, 1u, 9u, 9u};
  static const char valid_text[] =
      "#define ID(value) value\n"
      "recovered ID(37)\n";
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_macro_action_t action;
  ctool_c_pp_result_t result;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 case_count =
      (ctool_u32)(sizeof(error_text) / sizeof(error_text[0]));
  ctool_u32 index;

  if (open_job("function-errors", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/function-errors.c");
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;

  for (index = 0u; index < case_count; index++) {
    source.contents =
        ctool_bytes(error_text[index], (ctool_u32)strlen(error_text[index]));
    (void)memset(&result, 0xa5, sizeof(result));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, index);
    if (status != error_status[index] || result.tokens != NULL ||
        result.token_count != 0u || diagnostic == NULL ||
        diagnostic->code != error_code[index] ||
        !string_equal(diagnostic->path, "/function-errors.c") ||
        diagnostic->line != error_line[index] ||
        diagnostic->column != error_column[index] ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr,
                    "function-errors: table case %u was not useful (%s)\n",
                    index, ctool_status_name(status));
      (void)ctool_job_render_diagnostics(job);
      ctool_job_close(job);
      return 1;
    }
  }

  source.contents = ctool_bytes(NULL, 0u);
  action.kind = CTOOL_C_PP_MACRO_DEFINE;
  action.name = ctool_string("COMMAND_OBJECT");
  action.replacement = ctool_string("__VA_ARGS__");
  request.macro_actions = &action;
  request.macro_action_count = 1u;
  (void)memset(&result, 0xa5, sizeof(result));
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_preprocess(job, &source, &request, &result);
  diagnostic = ctool_job_diagnostic(job, case_count);
  if (status != CTOOL_ERR_INPUT || result.tokens != NULL ||
      result.token_count != 0u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PP_DIAG_MACRO_DEFINITION ||
      !string_equal(diagnostic->path, "/function-errors.c") ||
      diagnostic->line != 1u || diagnostic->column != 1u ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr,
                  "function-errors: command macro reserved replacement failed\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/function-recovery.c");
  source.contents =
      ctool_bytes(valid_text, (ctool_u32)(sizeof(valid_text) - 1u));
  request.macro_actions = (const ctool_c_pp_macro_action_t *)0;
  request.macro_action_count = 0u;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 2u ||
      !string_equal(result.tokens[0].spelling, "recovered") ||
      !string_equal(result.tokens[1].spelling, "37") ||
      !string_equal(result.tokens[1].location.path,
                    "/function-recovery.c") ||
      result.tokens[1].location.line != 2u ||
      ctool_job_diagnostic_count(job) != case_count + 1u) {
    (void)fprintf(stderr, "function-errors: same-job recovery failed\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }

  ctool_job_close(job);
  (void)printf("function-errors: ok\n");
  return 0;
}

static int run_macro_operator_errors(void) {
  static const char *const error_text[] = {
      "#define BAD(x) # 7\n",
      "#define BAD(x) ## x\n",
      "#define BAD(x) x ##\n",
      "#define BAD ## x\n",
      "#define BAD x ##\n",
      "#define BAD(x, ..., y) x\n",
      "#define BAD(x, ...,) x\n",
      "#define BAD(args...) args\n",
      "#define S(x) #not_x\n",
      "#define V(x, ...) x\nV(1)\n",
      "#define JOIN(a, b) a ## b\nJOIN(+, *)\n",
      "#define JOIN(a, b) a ## b\nJOIN(/, *)\n",
      "#define JOIN(a, b) a ## ## b\nJOIN(x, y)\n",
      "__VA_ARGS__\n",
      "#define S(x) #x\nS(__VA_ARGS__)\n",
      "#define CAT(a, b) a ## b\nCAT(__VA_, ARGS__)\n",
      "#define JOIN(a, b) a ## b\nJOIN(\"a\", \"b\")\n",
      "#define BAD(x) %:%: x\n",
      "#define BAD(x) x %:%:\n",
      ("#define SAME(x) x\n"
       "#define SAME(x, ...) x\n"),
      ("#define SAME(x) #x\n"
       "#define SAME(x) %:x\n"),
      "#ifdef __VA_ARGS__\n#endif\n",
      "#ifndef __VA_ARGS__\n#endif\n"};
  static const ctool_status_t error_status[] = {
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_UNSUPPORTED, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT};
  static const ctool_u32 error_code[] = {
      CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_PASTE,
      CTOOL_C_PP_DIAG_MACRO_PASTE,
      CTOOL_C_PP_DIAG_MACRO_PASTE,
      CTOOL_C_PP_DIAG_MACRO_PASTE,
      CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_ARGUMENTS,
      CTOOL_C_PP_DIAG_MACRO_PASTE,
      CTOOL_C_PP_DIAG_MACRO_PASTE,
      CTOOL_C_PP_DIAG_MACRO_PASTE,
      CTOOL_C_PP_DIAG_MACRO_EXPANSION,
      CTOOL_C_PP_DIAG_MACRO_EXPANSION,
      CTOOL_C_PP_DIAG_MACRO_EXPANSION,
      CTOOL_C_PP_DIAG_MACRO_PASTE,
      CTOOL_C_PP_DIAG_MACRO_PASTE,
      CTOOL_C_PP_DIAG_MACRO_PASTE,
      CTOOL_C_PP_DIAG_MACRO_REDEFINITION,
      CTOOL_C_PP_DIAG_MACRO_REDEFINITION,
      CTOOL_C_PP_DIAG_MACRO_EXPANSION,
      CTOOL_C_PP_DIAG_MACRO_EXPANSION};
  static const ctool_u32 error_line[] = {
      1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 2u, 2u, 2u, 2u, 1u, 2u, 2u,
      2u, 1u, 1u, 2u, 2u, 1u, 1u};
  static const ctool_u32 error_column[] = {
      16u, 16u, 18u, 13u, 15u, 16u, 16u, 17u,
      14u, 1u,  1u,  1u,  1u,  1u,  3u,  1u, 1u, 16u, 18u, 9u, 9u,
      8u, 9u};
  static const ctool_bool error_gnu[] = {
      CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE,
      CTOOL_FALSE, CTOOL_FALSE, CTOOL_TRUE,  CTOOL_FALSE, CTOOL_FALSE,
      CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE,
      CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE,
      CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE};
  static const char valid_text[] =
      "#define CAT(left, right) left ## right\n"
      "recovered CAT(ok, _done)\n";
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 case_count =
      (ctool_u32)(sizeof(error_text) / sizeof(error_text[0]));
  ctool_u32 index;

  if (open_job("macro-operator-errors", &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/macro-errors.c");
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  for (index = 0u; index < case_count; index++) {
    source.contents =
        ctool_bytes(error_text[index], (ctool_u32)strlen(error_text[index]));
    request.gnu_extensions = error_gnu[index];
    (void)memset(&result, 0xa5, sizeof(result));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, index);
    if (status != error_status[index] || result.tokens != NULL ||
        result.token_count != 0u || diagnostic == NULL ||
        diagnostic->code != error_code[index] ||
        !string_equal(diagnostic->path, "/macro-errors.c") ||
        diagnostic->line != error_line[index] ||
        diagnostic->column != error_column[index] ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr,
                    "macro-operator-errors: case %u differs (%s)\n",
                    index, ctool_status_name(status));
      (void)ctool_job_render_diagnostics(job);
      ctool_job_close(job);
      return 1;
    }
  }
  source.path.text = ctool_string("/macro-recovery.c");
  source.contents =
      ctool_bytes(valid_text, (ctool_u32)(sizeof(valid_text) - 1u));
  request.gnu_extensions = CTOOL_FALSE;
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 2u ||
      !string_equal(result.tokens[0].spelling, "recovered") ||
      !string_equal(result.tokens[1].spelling, "ok_done") ||
      !string_equal(result.tokens[1].location.path, "/macro-recovery.c") ||
      ctool_job_diagnostic_count(job) != case_count) {
    (void)fprintf(stderr,
                  "macro-operator-errors: same-job recovery failed\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("macro-operator-errors: ok\n");
  return 0;
}

static int run_object_includes(void) {
  static const char primary_text[] =
      "#ifndef MAIN_GUARD\n"
      "#define MAIN_GUARD\n"
      "#define LOCAL_VALUE 1\n"
      "#undef LOCAL_VALUE\n"
      "#define LOCAL_VALUE 7\n"
      "#define LOCAL_VALUE 7\n"
      "#define SELF SELF\n"
      "#define MUTUAL_A MUTUAL_B\n"
      "#define MUTUAL_B MUTUAL_A\n"
      "#define REMOVED 99\n"
      "#undef REMOVED\n"
      "#include \"object.h\"\n"
      "#include <angle.h>\n"
      "#include <a b.h>\n"
      "#include <a/**/b.h>\n"
      "#include \"root_only.h\"\n"
      "#ifdef OMIT\n"
      "#error command-line undef did not apply\n"
      "#endif\n"
      "#if 0\n"
      "#ifdef\n"
      "#endif\n"
      "#include \"inactive-missing.h\"\n"
      "#endif\n"
      "#ifndef REMOVED\n"
      "#define CHOICE HEADER_VALUE\n"
      "#else\n"
      "#define CHOICE 0\n"
      "#endif\n"
      "#if 0\n"
      "#error inactive #if branch executed\n"
      "#elif 1\n"
      "#define CONDITIONAL_VALUE 41\n"
      "#else\n"
      "#error inactive #else branch executed\n"
      "#endif\n"
      "#if 1\n"
      "#define SKIPPED_ELIF_VALUE 43\n"
      "#elif NOT_EVALUATED + 1\n"
      "#error skipped #elif branch executed\n"
      "#endif\n"
      "#if 1\n"
      "#elif\n"
      "#endif\n"
      "#ifdef COMMAND_VALUE\n"
      "int values = LOCAL_VALUE + CHOICE + CYCLE_A_VALUE + "
      "CYCLE_B_VALUE + ANGLE_VALUE + FORCED_VALUE + COMMAND_VALUE + "
      "CONDITIONAL_VALUE + SKIPPED_ELIF_VALUE;\n"
      "#endif\n"
      "recursive SELF MUTUAL_A\n"
      "#include \"repeat.h\"\n"
      "#include \"repeat.h\"\n"
      "#include \"main.c\"\n"
      "#endif\n";
  static const char object_text[] =
      "#ifndef OBJECT_H\n"
      "#define OBJECT_H\n"
      "#define HEADER_VALUE 11\n"
      "#include \"cycle_a.h\"\n"
      "#endif\n";
  static const char cycle_a_text[] =
      "#ifndef CYCLE_A_H\n"
      "#define CYCLE_A_H\n"
      "#define CYCLE_A_VALUE 13\n"
      "#include \"cycle_b.h\"\n"
      "#endif\n";
  static const char cycle_b_text[] =
      "#ifndef CYCLE_B_H\n"
      "#define CYCLE_B_H\n"
      "#define CYCLE_B_VALUE 17\n"
      "#include \"cycle_a.h\"\n"
      "#endif\n";
  static const char angle_text[] = "#define ANGLE_VALUE 19\n";
  static const char forced_text[] =
      "#ifdef COMMAND_BASE\n"
      "#define FORCED_VALUE COMMAND_BASE\n"
      "#else\n"
      "#error command macro was not installed before forced include\n"
      "#endif\n";
  static const char repeat_text[] = "repeat 31\n";
  static const char root_only_text[] = "root_only 37\n";
  static const char spaced_text[] = "spaced 47\n";
  static const char commented_text[] = "commented 53\n";
  static const char *const expected_text[] = {
      "spaced", "47", "commented", "53", "root_only", "37", "int",
      "values", "=",  "7",         "+",  "11",        "+",  "13",
      "+",      "17", "+",         "19", "+",         "23", "+",
      "29",     "+",  "41",        "+",  "43",        ";",
      "recursive", "SELF", "MUTUAL_A", "repeat", "31", "repeat", "31"};
  fixture_file_t files[13];
  fixture_store_t store;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_include_root_t roots[3];
  ctool_path_t forced;
  ctool_c_pp_macro_action_t actions[4];
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_status_t status;
  ctool_u32 index;

  files[0].path = ctool_string("/fixtures/object.h");
  files[0].contents =
      ctool_bytes(object_text, (ctool_u32)(sizeof(object_text) - 1u));
  files[1].path = ctool_string("/fixtures/cycle_a.h");
  files[1].contents =
      ctool_bytes(cycle_a_text, (ctool_u32)(sizeof(cycle_a_text) - 1u));
  files[2].path = ctool_string("/fixtures/cycle_b.h");
  files[2].contents =
      ctool_bytes(cycle_b_text, (ctool_u32)(sizeof(cycle_b_text) - 1u));
  files[3].path = ctool_string("/angle/angle.h");
  files[3].contents =
      ctool_bytes(angle_text, (ctool_u32)(sizeof(angle_text) - 1u));
  files[4].path = ctool_string("/fixtures/forced.h");
  files[4].contents =
      ctool_bytes(forced_text, (ctool_u32)(sizeof(forced_text) - 1u));
  files[5].path = ctool_string("/fixtures/repeat.h");
  files[5].contents =
      ctool_bytes(repeat_text, (ctool_u32)(sizeof(repeat_text) - 1u));
  files[6].path = ctool_string("/quoted-first/root_only.h");
  files[6].contents = ctool_bytes(
      root_only_text, (ctool_u32)(sizeof(root_only_text) - 1u));
  files[7].path = ctool_string("/quoted-first/object.h");
  files[7].contents = ctool_bytes("wrong_object 1\n", 15u);
  files[8].path = ctool_string("/fixtures/angle.h");
  files[8].contents = ctool_bytes("wrong_angle 1\n", 14u);
  files[9].path = ctool_string("/angle/root_only.h");
  files[9].contents = ctool_bytes("wrong_form 1\n", 13u);
  files[10].path = ctool_string("/quoted-second/root_only.h");
  files[10].contents = ctool_bytes("wrong_order 1\n", 14u);
  files[11].path = ctool_string("/angle/a b.h");
  files[11].contents =
      ctool_bytes(spaced_text, (ctool_u32)(sizeof(spaced_text) - 1u));
  files[12].path = ctool_string("/angle/a/**/b.h");
  files[12].contents = ctool_bytes(
      commented_text, (ctool_u32)(sizeof(commented_text) - 1u));
  store.files = files;
  store.file_count = 13u;
  store.read_count = 0u;
  if (open_fixture_job("object-includes", &store, &adapter, &job) != 0) {
    return 1;
  }

  source.path.text = ctool_string("/fixtures/main.c");
  source.contents =
      ctool_bytes(primary_text, (ctool_u32)(sizeof(primary_text) - 1u));
  roots[0].directory.text = ctool_string("/angle");
  roots[0].forms = CTOOL_C_PP_INCLUDE_ANGLE;
  roots[1].directory.text = ctool_string("/quoted-first");
  roots[1].forms = CTOOL_C_PP_INCLUDE_QUOTED;
  roots[2].directory.text = ctool_string("/quoted-second");
  roots[2].forms = CTOOL_C_PP_INCLUDE_QUOTED;
  forced.text = ctool_string("/fixtures/forced.h");
  actions[0].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[0].name = ctool_string("COMMAND_VALUE");
  actions[0].replacement = ctool_string("29");
  actions[1].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[1].name = ctool_string("COMMAND_BASE");
  actions[1].replacement = ctool_string("23");
  actions[2].kind = CTOOL_C_PP_MACRO_DEFINE;
  actions[2].name = ctool_string("OMIT");
  actions[2].replacement = ctool_string("1");
  actions[3].kind = CTOOL_C_PP_MACRO_UNDEF;
  actions[3].name = ctool_string("OMIT");
  actions[3].replacement = ctool_string("");
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  request.include_roots = roots;
  request.include_root_count = 3u;
  request.forced_includes = &forced;
  request.forced_include_count = 1u;
  request.macro_actions = actions;
  request.macro_action_count = 4u;

  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK ||
      result.token_count !=
          (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
    (void)fprintf(stderr,
                  "object-includes: preprocessing failed (%s, %u tokens)\n",
                  ctool_status_name(status), result.token_count);
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < result.token_count; index++) {
    if (!string_equal(result.tokens[index].spelling, expected_text[index])) {
      (void)fprintf(stderr, "object-includes: token %u differs\n", index);
      ctool_job_close(job);
      return 1;
    }
  }
  if (store.read_count != 9u ||
      !string_equal(result.tokens[30].location.path, "/fixtures/repeat.h") ||
      !string_equal(result.tokens[32].location.path, "/fixtures/repeat.h")) {
    (void)fprintf(stderr,
                  "object-includes: source cache or locations differ\n");
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("object-includes: ok\n");
  return 0;
}

static int run_directive_errors(void) {
  static const char missing_text[] = "#include \"missing.h\"\n";
  static const char conditional_text[] = "#else\n";
  static const char loop_text[] = "#include \"loop.h\"\n";
  static const char *const error_text[] = {
      "#include <broken", "#define 7\n",
      "#define VALUE 1\n#define VALUE 2\n", "#ifdef VALUE\n",
      "#include \"\"\n", "#include <>\n", "#include <a>b>\n",
      "#define HEADER \"x.h\"\n#include HEADER\n",
      "#ifdef\n#endif\n", "#if 0\n#elif\n#endif\n",
      "#include \"loop.h\"\n"};
  static const ctool_status_t error_status[] = {
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
      CTOOL_ERR_UNSUPPORTED,
      CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_LIMIT};
  static const ctool_u32 error_code[] = {
      CTOOL_C_PP_DIAG_INCLUDE_PATH, CTOOL_C_PP_DIAG_MACRO_DEFINITION,
      CTOOL_C_PP_DIAG_MACRO_REDEFINITION, CTOOL_C_PP_DIAG_CONDITIONAL,
      CTOOL_C_PP_DIAG_INCLUDE_PATH, CTOOL_C_PP_DIAG_INCLUDE_PATH,
      CTOOL_C_PP_DIAG_INCLUDE_PATH, CTOOL_C_PP_DIAG_INCLUDE_PATH,
      CTOOL_C_PP_DIAG_CONDITIONAL,
      CTOOL_C_PP_DIAG_CONDITIONAL, CTOOL_C_PP_DIAG_INCLUDE_DEPTH};
  static const ctool_u32 error_line[] = {1u, 1u, 2u, 1u, 1u,
                                        1u, 1u, 2u, 1u, 2u, 1u};
  static const ctool_u32 error_column[] = {1u, 1u, 9u, 1u, 1u,
                                          1u, 1u, 1u, 1u, 1u, 1u};
  static const char *const error_path[] = {
      "/fixtures/case.c", "/fixtures/case.c", "/fixtures/case.c",
      "/fixtures/case.c", "/fixtures/case.c", "/fixtures/case.c",
      "/fixtures/case.c", "/fixtures/case.c", "/fixtures/case.c",
      "/fixtures/case.c", "/fixtures/loop.h"};
  fixture_file_t loop_file;
  fixture_store_t store;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_include_root_t root;
  ctool_path_t missing_forced;
  ctool_c_pp_request_t request;
  ctool_c_pp_result_t result;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 index;

  loop_file.path = ctool_string("/fixtures/loop.h");
  loop_file.contents =
      ctool_bytes(loop_text, (ctool_u32)(sizeof(loop_text) - 1u));
  store.files = &loop_file;
  store.file_count = 1u;
  store.read_count = 0u;
  if (open_fixture_job("directive-errors", &store, &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/fixtures/missing.c");
  source.contents =
      ctool_bytes(missing_text, (ctool_u32)(sizeof(missing_text) - 1u));
  root.directory.text = ctool_string("/fixtures");
  root.forms = CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE;
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  request.include_roots = &root;
  request.include_root_count = 1u;
  mark = ctool_arena_mark(ctool_job_arena(job));

  status = ctool_c_preprocess(job, &source, &request, &result);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (status != CTOOL_ERR_NOT_FOUND || result.tokens != NULL ||
      result.token_count != 0u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PP_DIAG_INCLUDE_NOT_FOUND ||
      diagnostic->line != 1u || diagnostic->column != 1u ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr,
                  "directive-errors: missing include was not transactional\n");
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/fixtures/conditional.c");
  source.contents = ctool_bytes(
      conditional_text, (ctool_u32)(sizeof(conditional_text) - 1u));
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_preprocess(job, &source, &request, &result);
  diagnostic = ctool_job_diagnostic(job, 1u);
  if (status != CTOOL_ERR_INPUT || result.tokens != NULL ||
      result.token_count != 0u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PP_DIAG_CONDITIONAL ||
      diagnostic->line != 1u || diagnostic->column != 1u ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr,
                  "directive-errors: unmatched conditional was not useful\n");
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/fixtures/case.c");
  for (index = 0u;
       index < (ctool_u32)(sizeof(error_text) / sizeof(error_text[0]));
       index++) {
    source.contents =
        ctool_bytes(error_text[index], (ctool_u32)strlen(error_text[index]));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, 2u + index);
    if (status != error_status[index] || result.tokens != NULL ||
        result.token_count != 0u || diagnostic == NULL ||
        diagnostic->code != error_code[index] ||
        diagnostic->line != error_line[index] ||
        diagnostic->column != error_column[index] ||
        !string_equal(diagnostic->path, error_path[index]) ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr,
                    "directive-errors: table case %u was not useful\n",
                    index);
      ctool_job_close(job);
      return 1;
    }
  }

  source.contents = ctool_bytes(NULL, 0u);
  missing_forced.text = ctool_string("/fixtures/missing-forced.h");
  request.forced_includes = &missing_forced;
  request.forced_include_count = 1u;
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_preprocess(job, &source, &request, &result);
  diagnostic = ctool_job_diagnostic(
      job, 2u + (ctool_u32)(sizeof(error_text) / sizeof(error_text[0])));
  if (status != CTOOL_ERR_NOT_FOUND || result.tokens != NULL ||
      result.token_count != 0u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PP_DIAG_INCLUDE_NOT_FOUND ||
      !string_equal(diagnostic->path, "/fixtures/missing-forced.h") ||
      diagnostic->line != 0u || diagnostic->column != 0u ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr,
                  "directive-errors: missing forced include was not useful\n");
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("directive-errors: ok\n");
  return 0;
}

static int expect_request_limit(ctool_job_t *job,
                                const ctool_source_t *source,
                                const ctool_c_pp_request_t *request,
                                ctool_u32 diagnostic_index,
                                const char *expected_path) {
  ctool_c_pp_result_t result;
  ctool_arena_mark_t mark = ctool_arena_mark(ctool_job_arena(job));
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;

  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_c_preprocess(job, source, request, &result);
  diagnostic = ctool_job_diagnostic(job, diagnostic_index);
  if (status != CTOOL_ERR_LIMIT || result.tokens != NULL ||
      result.token_count != 0u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PP_DIAG_LIMIT || diagnostic->line != 0u ||
      diagnostic->column != 0u ||
      !string_equal(diagnostic->path, expected_path) ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr, "limits: request case %u failed\n",
                  diagnostic_index);
    return 1;
  }
  return 0;
}

static int run_limits(void) {
  static const char oversized_text[] = "123456789";
  static const char valid_text[] = "int x;\n";
  ctool_limits_t limits = ctool_default_limits();
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_request_t request;
  ctool_c_pp_macro_action_t action;
  ctool_c_pp_include_root_t root;
  ctool_path_t forced;
  ctool_c_pp_result_t result;
  ctool_status_t status;

  limits.path_bytes = 8u;
  limits.source_bytes = 8u;
  status = ctool_host_adapter_init(&adapter, ".");
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "limits: host adapter: %s\n",
                  ctool_status_name(status));
    return 1;
  }
  config = ctool_host_job_config(&adapter, limits);
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "limits: job open: %s\n",
                  ctool_status_name(status));
    return 1;
  }

  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  source.path.text.data = "/";
  source.path.text.size = 0xffffffffu;
  source.contents = ctool_bytes(NULL, 0u);
  if (expect_request_limit(job, &source, &request, 0u, "/cupidc") != 0) {
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/ok.c");
  source.contents =
      ctool_bytes(oversized_text,
                  (ctool_u32)(sizeof(oversized_text) - 1u));
  if (expect_request_limit(job, &source, &request, 1u, "/ok.c") != 0) {
    ctool_job_close(job);
    return 1;
  }

  source.contents = ctool_bytes(NULL, 0u);
  action.kind = CTOOL_C_PP_MACRO_DEFINE;
  action.name = ctool_string("VALUE");
  action.replacement = ctool_string("123456789");
  request.macro_actions = &action;
  request.macro_action_count = 1u;
  if (expect_request_limit(job, &source, &request, 2u, "/ok.c") != 0) {
    ctool_job_close(job);
    return 1;
  }

  action.name.data = "A";
  action.name.size = 0xffffffffu;
  action.replacement = ctool_string("");
  if (expect_request_limit(job, &source, &request, 3u, "/ok.c") != 0) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  root.directory.text.data = "/";
  root.directory.text.size = 0xffffffffu;
  root.forms = CTOOL_C_PP_INCLUDE_QUOTED;
  request.include_roots = &root;
  request.include_root_count = 1u;
  if (expect_request_limit(job, &source, &request, 4u, "/ok.c") != 0) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  forced.text.data = "/";
  forced.text.size = 0xffffffffu;
  request.forced_includes = &forced;
  request.forced_include_count = 1u;
  if (expect_request_limit(job, &source, &request, 5u, "/ok.c") != 0) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  source.contents =
      ctool_bytes(valid_text, (ctool_u32)(sizeof(valid_text) - 1u));
  status = ctool_c_preprocess(job, &source, &request, &result);
  if (status != CTOOL_OK || result.token_count != 3u ||
      !string_equal(result.tokens[0].spelling, "int") ||
      !string_equal(result.tokens[1].spelling, "x") ||
      !string_equal(result.tokens[2].spelling, ";") ||
      ctool_job_diagnostic(job, 6u) != NULL) {
    (void)fprintf(stderr, "limits: same-job recovery failed\n");
    ctool_job_close(job);
    return 1;
  }

  ctool_job_close(job);
  (void)printf("limits: ok\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3 ||
      (argc == 3 && strcmp(argv[1], "macro-active-cases") != 0 &&
       strcmp(argv[1], "cupid-exe-active") != 0)) {
    (void)fprintf(
        stderr,
        "usage: cupidc-pp-contract phases|tokens|errors|unsupported|"
        "conditional-expressions|predefined|predefined-files|predefined-errors|"
        "pragmas|pragma-files|pragma-errors|pragma-scale|"
        "cupid-exe|cupid-exe-active <repo-root>|cupid-exe-files|"
        "cupid-exe-errors|"
        "cupid-exe-scale|"
        "conditional-errors|"
        "conditional-active|conditional-scale|"
        "function-macros|function-scale|macro-operators|macro-gnu|"
        "macro-operator-scale|macro-active-cases <toolchain-root>|"
        "macro-operator-errors|"
        "function-errors|object-includes|"
        "directive-errors|limits\n");
    return 2;
  }
  if (strcmp(argv[1], "phases") == 0) {
    return run_phases();
  }
  if (strcmp(argv[1], "tokens") == 0) {
    return run_tokens();
  }
  if (strcmp(argv[1], "errors") == 0) {
    return run_errors();
  }
  if (strcmp(argv[1], "unsupported") == 0) {
    return run_unsupported();
  }
  if (strcmp(argv[1], "conditional-expressions") == 0) {
    return run_conditional_expressions();
  }
  if (strcmp(argv[1], "predefined") == 0) {
    return run_predefined_macros();
  }
  if (strcmp(argv[1], "predefined-files") == 0) {
    return run_predefined_files();
  }
  if (strcmp(argv[1], "predefined-errors") == 0) {
    return run_predefined_errors();
  }
  if (strcmp(argv[1], "pragmas") == 0) {
    return run_pragmas();
  }
  if (strcmp(argv[1], "pragma-files") == 0) {
    return run_pragma_files();
  }
  if (strcmp(argv[1], "pragma-errors") == 0) {
    return run_pragma_errors();
  }
  if (strcmp(argv[1], "pragma-scale") == 0) {
    return run_pragma_scale();
  }
  if (strcmp(argv[1], "cupid-exe") == 0) {
    return run_cupid_exe();
  }
  if (strcmp(argv[1], "cupid-exe-active") == 0) {
    if (argc != 3) {
      (void)fprintf(stderr, "cupid-exe-active requires the repository root\n");
      return 2;
    }
    return run_cupid_exe_active(argv[2]);
  }
  if (strcmp(argv[1], "cupid-exe-files") == 0) {
    return run_cupid_exe_files();
  }
  if (strcmp(argv[1], "cupid-exe-errors") == 0) {
    return run_cupid_exe_errors();
  }
  if (strcmp(argv[1], "cupid-exe-scale") == 0) {
    return run_cupid_exe_scale();
  }
  if (strcmp(argv[1], "conditional-errors") == 0) {
    return run_conditional_expression_errors();
  }
  if (strcmp(argv[1], "conditional-active") == 0) {
    return run_conditional_active_cases();
  }
  if (strcmp(argv[1], "conditional-scale") == 0) {
    return run_conditional_expression_scale();
  }
  if (strcmp(argv[1], "function-macros") == 0) {
    return run_function_macros();
  }
  if (strcmp(argv[1], "function-scale") == 0) {
    return run_function_macro_scale();
  }
  if (strcmp(argv[1], "macro-operators") == 0) {
    return run_macro_operators();
  }
  if (strcmp(argv[1], "macro-gnu") == 0) {
    return run_macro_gnu();
  }
  if (strcmp(argv[1], "macro-operator-scale") == 0) {
    return run_macro_operator_scale();
  }
  if (strcmp(argv[1], "macro-active-cases") == 0) {
    if (argc != 3) {
      (void)fprintf(stderr,
                    "macro-active-cases requires the toolchain root\n");
      return 2;
    }
    return run_macro_active_cases(argv[2]);
  }
  if (strcmp(argv[1], "macro-operator-errors") == 0) {
    return run_macro_operator_errors();
  }
  if (strcmp(argv[1], "function-errors") == 0) {
    return run_function_macro_errors();
  }
  if (strcmp(argv[1], "object-includes") == 0) {
    return run_object_includes();
  }
  if (strcmp(argv[1], "directive-errors") == 0) {
    return run_directive_errors();
  }
  if (strcmp(argv[1], "limits") == 0) {
    return run_limits();
  }
  (void)fprintf(stderr, "unknown mode: %s\n", argv[1]);
  return 2;
}
