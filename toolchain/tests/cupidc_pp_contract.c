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

static int arena_marks_equal(ctool_arena_mark_t left,
                             ctool_arena_mark_t right) {
  return left.owner == right.owner && left.block == right.block &&
                 left.used == right.used && left.generation == right.generation
             ? 1
             : 0;
}

static int open_job(const char *mode, ctool_host_adapter_t *adapter,
                    ctool_job_t **job_out) {
  ctool_job_config_t config;
  ctool_status_t status = ctool_host_adapter_init(adapter, ".");
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
  static const char directive_text[] =
      "token\n/*a\n*/ #define FUNCTION(x) x\n";
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
  source.path.text = ctool_string("/directive.c");
  source.contents = ctool_bytes(
      directive_text, (ctool_u32)(sizeof(directive_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.mode = CTOOL_C_PP_MODE_C11;
  mark = ctool_arena_mark(ctool_job_arena(job));

  status = ctool_c_preprocess(job, &source, &request, &result);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (status != CTOOL_ERR_UNSUPPORTED || result.tokens != NULL ||
      result.token_count != 0u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PP_DIAG_MACRO_DEFINITION ||
      diagnostic->line != 3u || diagnostic->column != 12u ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr,
                  "unsupported: function macro did not fail explicitly\n");
    ctool_job_close(job);
    return 1;
  }

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
    diagnostic = ctool_job_diagnostic(job, 1u + index);
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
  if (argc != 2) {
    (void)fprintf(
        stderr,
        "usage: cupidc-pp-contract phases|tokens|errors|unsupported|"
        "object-includes|directive-errors|limits\n");
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
