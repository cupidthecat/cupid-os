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
  static const char directive_text[] = "token\n/*a\n*/ #define VALUE 1\n";
  static const char *const unsupported_text[] = {
      "%:define VALUE 1\n", "__LINE__", "__STDC__", "__STDC_HOSTED__",
      "__STDC_VERSION__"};
  static const ctool_u32 unsupported_code[] = {
      CTOOL_C_PP_DIAG_DIRECTIVE,       CTOOL_C_PP_DIAG_MACRO_EXPANSION,
      CTOOL_C_PP_DIAG_MACRO_EXPANSION, CTOOL_C_PP_DIAG_MACRO_EXPANSION,
      CTOOL_C_PP_DIAG_MACRO_EXPANSION};
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_c_pp_macro_action_t action;
  ctool_path_t forced_include;
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
      diagnostic->code != CTOOL_C_PP_DIAG_DIRECTIVE ||
      diagnostic->line != 3u || diagnostic->column != 4u ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr, "unsupported: directive did not fail explicitly\n");
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/configured.c");
  source.contents = ctool_bytes(NULL, 0u);
  action.kind = CTOOL_C_PP_MACRO_DEFINE;
  action.name = ctool_string("VALUE");
  action.replacement = ctool_string("1");
  request.macro_actions = &action;
  request.macro_action_count = 1u;
  status = ctool_c_preprocess(job, &source, &request, &result);
  diagnostic = ctool_job_diagnostic(job, 1u);
  if (status != CTOOL_ERR_UNSUPPORTED || result.tokens != NULL ||
      result.token_count != 0u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PP_DIAG_UNSUPPORTED_CONFIGURATION ||
      !string_equal(diagnostic->path, "/configured.c")) {
    (void)fprintf(stderr,
                  "unsupported: configured features did not fail explicitly\n");
    ctool_job_close(job);
    return 1;
  }

  request.macro_actions = NULL;
  request.macro_action_count = 0u;
  forced_include.text = ctool_string("/forced.h");
  request.forced_includes = &forced_include;
  request.forced_include_count = 1u;
  status = ctool_c_preprocess(job, &source, &request, &result);
  diagnostic = ctool_job_diagnostic(job, 2u);
  if (status != CTOOL_ERR_UNSUPPORTED || result.tokens != NULL ||
      result.token_count != 0u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PP_DIAG_UNSUPPORTED_CONFIGURATION) {
    (void)fprintf(stderr,
                  "unsupported: forced include did not fail explicitly\n");
    ctool_job_close(job);
    return 1;
  }

  request.forced_includes = NULL;
  request.forced_include_count = 0u;
  source.path.text = ctool_string("/builtin.c");
  source.contents = ctool_bytes("__FILE__", 8u);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_c_preprocess(job, &source, &request, &result);
  diagnostic = ctool_job_diagnostic(job, 3u);
  if (status != CTOOL_ERR_UNSUPPORTED || result.tokens != NULL ||
      result.token_count != 0u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PP_DIAG_MACRO_EXPANSION ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr,
                  "unsupported: predefined macro did not fail explicitly\n");
    ctool_job_close(job);
    return 1;
  }

  for (index = 0u;
       index <
       (ctool_u32)(sizeof(unsupported_text) / sizeof(unsupported_text[0]));
       index++) {
    source.contents =
        ctool_bytes(unsupported_text[index],
                    (ctool_u32)strlen(unsupported_text[index]));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, 4u + index);
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

int main(int argc, char **argv) {
  if (argc != 2) {
    (void)fprintf(
        stderr,
        "usage: cupidc-pp-contract phases|tokens|errors|unsupported\n");
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
  (void)fprintf(stderr, "unknown mode: %s\n", argv[1]);
  return 2;
}
