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
      "__FILE__", "__LINE__", "__STDC__", "__STDC_HOSTED__",
      "__STDC_VERSION__", "#if VALUE + 1\n#endif\n"};
  static const ctool_u32 unsupported_code[] = {
      CTOOL_C_PP_DIAG_MACRO_EXPANSION, CTOOL_C_PP_DIAG_MACRO_EXPANSION,
      CTOOL_C_PP_DIAG_MACRO_EXPANSION, CTOOL_C_PP_DIAG_MACRO_EXPANSION,
      CTOOL_C_PP_DIAG_MACRO_EXPANSION,
      CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION};
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
        diagnostic->line != 1u || diagnostic->column != 1u ||
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
      (argc == 3 && strcmp(argv[1], "macro-active-cases") != 0)) {
    (void)fprintf(
        stderr,
        "usage: cupidc-pp-contract phases|tokens|errors|unsupported|"
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
