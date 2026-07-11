#include "cupidc_pp.h"

typedef struct {
  char *text;
  ctool_u32 size;
  ctool_u32 *splice_offsets;
  ctool_u32 splice_count;
} pp_input_t;

typedef struct {
  const pp_input_t *input;
  ctool_u32 position;
  ctool_u32 splice_index;
  ctool_u32 line;
  ctool_u32 column;
} pp_cursor_t;

typedef struct {
  ctool_c_pp_token_kind_t kind;
  ctool_u32 offset;
  ctool_u32 size;
  ctool_u32 line;
  ctool_u32 column;
  ctool_bool leading_space;
  ctool_bool at_line_start;
} pp_lexeme_t;

typedef struct {
  pp_cursor_t cursor;
  ctool_bool at_line_start;
  ctool_bool leading_space;
} pp_lexer_t;

typedef struct {
  ctool_job_t *job;
  const ctool_source_t *source;
  const ctool_c_pp_request_t *request;
  ctool_string_t path;
  ctool_status_t failure_status;
  ctool_u32 failure_code;
  ctool_u32 failure_line;
  ctool_u32 failure_column;
  const char *failure_message;
} pp_context_t;

static void pp_zero_result(ctool_c_pp_result_t *result) {
  result->tokens = (const ctool_c_pp_token_t *)0;
  result->token_count = 0u;
}

static ctool_bool pp_is_alpha(char character) {
  return ((character >= 'a' && character <= 'z') ||
          (character >= 'A' && character <= 'Z') || character == '_')
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool pp_is_digit(char character) {
  return character >= '0' && character <= '9' ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool pp_is_alnum(char character) {
  return pp_is_alpha(character) == CTOOL_TRUE ||
                 pp_is_digit(character) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool pp_is_space(char character) {
  return character == ' ' || character == '\t' || character == '\n' ||
                 character == '\v' || character == '\f'
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool pp_string_is_identifier(ctool_string_t value) {
  ctool_u32 index;
  if (value.data == (const char *)0 || value.size == 0u ||
      pp_is_alpha(value.data[0]) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  for (index = 1u; index < value.size; index++) {
    if (pp_is_alnum(value.data[index]) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t pp_validate_request(
    const ctool_source_t *source, const ctool_c_pp_request_t *request,
    const ctool_c_pp_result_t *result) {
  ctool_u32 index;
  if (source == (const ctool_source_t *)0 ||
      request == (const ctool_c_pp_request_t *)0 ||
      result == (const ctool_c_pp_result_t *)0 ||
      ctool_path_is_canonical(&source->path) == CTOOL_FALSE ||
      (source->contents.data == (const ctool_u8 *)0 &&
       source->contents.size != 0u) ||
      (request->mode != CTOOL_C_PP_MODE_C11 &&
       request->mode != CTOOL_C_PP_MODE_CUPID) ||
      (request->hosted_environment != CTOOL_FALSE &&
       request->hosted_environment != CTOOL_TRUE) ||
      (request->gnu_extensions != CTOOL_FALSE &&
       request->gnu_extensions != CTOOL_TRUE) ||
      (request->include_root_count != 0u &&
       request->include_roots == (const ctool_c_pp_include_root_t *)0) ||
      (request->forced_include_count != 0u &&
       request->forced_includes == (const ctool_path_t *)0) ||
      (request->macro_action_count != 0u &&
       request->macro_actions == (const ctool_c_pp_macro_action_t *)0)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  for (index = 0u; index < request->include_root_count; index++) {
    const ctool_c_pp_include_root_t *root = &request->include_roots[index];
    if (ctool_path_is_canonical(&root->directory) == CTOOL_FALSE ||
        root->forms == 0u ||
        (root->forms & ~(CTOOL_C_PP_INCLUDE_QUOTED |
                         CTOOL_C_PP_INCLUDE_ANGLE)) != 0u) {
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
  }
  for (index = 0u; index < request->forced_include_count; index++) {
    if (ctool_path_is_canonical(&request->forced_includes[index]) ==
        CTOOL_FALSE) {
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
  }
  for (index = 0u; index < request->macro_action_count; index++) {
    const ctool_c_pp_macro_action_t *action = &request->macro_actions[index];
    if ((action->kind != CTOOL_C_PP_MACRO_DEFINE &&
         action->kind != CTOOL_C_PP_MACRO_UNDEF) ||
        pp_string_is_identifier(action->name) == CTOOL_FALSE ||
        (action->replacement.data == (const char *)0 &&
         action->replacement.size != 0u) ||
        (action->kind == CTOOL_C_PP_MACRO_UNDEF &&
         action->replacement.size != 0u)) {
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
  }
  return CTOOL_OK;
}

static void pp_fail(pp_context_t *context, ctool_status_t status,
                    ctool_u32 code, ctool_u32 line, ctool_u32 column,
                    const char *message) {
  if (context->failure_status != CTOOL_OK) {
    return;
  }
  context->failure_status = status;
  context->failure_code = code;
  context->failure_line = line;
  context->failure_column = column;
  context->failure_message = message;
}

static ctool_status_t pp_publish_failure(pp_context_t *context) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t emitted;
  if (context->failure_status == CTOOL_OK) {
    return CTOOL_OK;
  }
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = context->failure_code;
  diagnostic.path = context->path;
  diagnostic.line = context->failure_line;
  diagnostic.column = context->failure_column;
  diagnostic.message = ctool_string(context->failure_message);
  emitted = ctool_job_emit(context->job, &diagnostic);
  return emitted == CTOOL_OK ? context->failure_status : emitted;
}

static ctool_bool pp_raw_newline(const ctool_u8 *data, ctool_u32 size,
                                 ctool_u32 position,
                                 ctool_u32 *byte_count_out) {
  if (position >= size) {
    return CTOOL_FALSE;
  }
  if (data[position] == (ctool_u8)'\n') {
    *byte_count_out = 1u;
    return CTOOL_TRUE;
  }
  if (data[position] == (ctool_u8)'\r') {
    *byte_count_out = position + 1u < size &&
                              data[position + 1u] == (ctool_u8)'\n'
                          ? 2u
                          : 1u;
    return CTOOL_TRUE;
  }
  return CTOOL_FALSE;
}

static ctool_status_t pp_normalize(pp_context_t *context,
                                   pp_input_t *input) {
  const ctool_u8 *raw = context->source->contents.data;
  ctool_u32 raw_size = context->source->contents.size;
  ctool_u32 raw_position = 0u;
  ctool_u32 output_size = 0u;
  ctool_u32 splice_count = 0u;
  ctool_u32 newline_size;
  ctool_u32 output_position = 0u;
  ctool_u32 splice_index = 0u;
  ctool_status_t status;

  while (raw_position < raw_size) {
    if (raw[raw_position] == (ctool_u8)'\\' &&
        pp_raw_newline(raw, raw_size, raw_position + 1u, &newline_size) ==
            CTOOL_TRUE) {
      splice_count++;
      raw_position += 1u + newline_size;
    } else {
      if (pp_raw_newline(raw, raw_size, raw_position, &newline_size) ==
          CTOOL_TRUE) {
        raw_position += newline_size;
      } else {
        raw_position++;
      }
      output_size++;
    }
  }
  if (output_size == 0xffffffffu) {
    pp_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_C_PP_DIAG_LIMIT, 0u, 0u,
            "CupidC preprocessing input is too large");
    return CTOOL_ERR_OVERFLOW;
  }
  status = ctool_arena_alloc(ctool_job_arena(context->job), output_size + 1u,
                             1u, (void **)&input->text);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, 0u, 0u,
            "CupidC preprocessing source storage limit exceeded");
    return status;
  }
  input->splice_offsets = (ctool_u32 *)0;
  if (splice_count != 0u) {
    status = ctool_arena_alloc_zero(
        ctool_job_arena(context->job), splice_count,
        (ctool_u32)sizeof(*input->splice_offsets),
        (ctool_u32)sizeof(ctool_u32), (void **)&input->splice_offsets);
    if (status != CTOOL_OK) {
      pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, 0u, 0u,
              "CupidC preprocessing splice storage limit exceeded");
      return status;
    }
  }

  raw_position = 0u;
  while (raw_position < raw_size) {
    if (raw[raw_position] == (ctool_u8)'\\' &&
        pp_raw_newline(raw, raw_size, raw_position + 1u, &newline_size) ==
            CTOOL_TRUE) {
      input->splice_offsets[splice_index] = output_position;
      splice_index++;
      raw_position += 1u + newline_size;
    } else if (pp_raw_newline(raw, raw_size, raw_position, &newline_size) ==
               CTOOL_TRUE) {
      input->text[output_position] = '\n';
      output_position++;
      raw_position += newline_size;
    } else {
      input->text[output_position] = (char)raw[raw_position];
      output_position++;
      raw_position++;
    }
  }
  input->text[output_position] = '\0';
  input->size = output_position;
  input->splice_count = splice_count;
  return CTOOL_OK;
}

static void pp_cursor_init(pp_cursor_t *cursor, const pp_input_t *input) {
  cursor->input = input;
  cursor->position = 0u;
  cursor->splice_index = 0u;
  cursor->line = 1u;
  cursor->column = 1u;
}

static void pp_lexer_init(pp_lexer_t *lexer, const pp_input_t *input) {
  pp_cursor_init(&lexer->cursor, input);
  lexer->at_line_start = CTOOL_TRUE;
  lexer->leading_space = CTOOL_FALSE;
}

static void pp_cursor_sync(pp_cursor_t *cursor) {
  while (cursor->splice_index < cursor->input->splice_count &&
         cursor->input->splice_offsets[cursor->splice_index] ==
             cursor->position) {
    cursor->line++;
    cursor->column = 1u;
    cursor->splice_index++;
  }
}

static ctool_bool pp_cursor_current(pp_cursor_t *cursor, char *character_out) {
  pp_cursor_sync(cursor);
  if (cursor->position >= cursor->input->size) {
    return CTOOL_FALSE;
  }
  *character_out = cursor->input->text[cursor->position];
  return CTOOL_TRUE;
}

static void pp_cursor_advance(pp_cursor_t *cursor) {
  char character;
  if (pp_cursor_current(cursor, &character) == CTOOL_FALSE) {
    return;
  }
  cursor->position++;
  if (character == '\n') {
    cursor->line++;
    cursor->column = 1u;
  } else {
    cursor->column++;
  }
}

static ctool_bool pp_cursor_peek(pp_cursor_t cursor, ctool_u32 distance,
                                 char *character_out) {
  ctool_u32 index;
  for (index = 0u; index < distance; index++) {
    if (pp_cursor_current(&cursor, character_out) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
    pp_cursor_advance(&cursor);
  }
  return pp_cursor_current(&cursor, character_out);
}

static ctool_bool pp_cursor_matches(pp_cursor_t cursor,
                                    const char *literal) {
  ctool_u32 index = 0u;
  char character;
  while (literal[index] != '\0') {
    if (pp_cursor_current(&cursor, &character) == CTOOL_FALSE ||
        character != literal[index]) {
      return CTOOL_FALSE;
    }
    pp_cursor_advance(&cursor);
    index++;
  }
  return CTOOL_TRUE;
}

static ctool_bool pp_is_literal_start(pp_cursor_t cursor,
                                      ctool_u32 *prefix_size_out,
                                      char *quote_out) {
  char first;
  char second;
  char third;
  if (pp_cursor_peek(cursor, 0u, &first) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  if (first == '\'' || first == '"') {
    *prefix_size_out = 0u;
    *quote_out = first;
    return CTOOL_TRUE;
  }
  if ((first == 'L' || first == 'u' || first == 'U') &&
      pp_cursor_peek(cursor, 1u, &second) == CTOOL_TRUE &&
      (second == '\'' || second == '"')) {
    *prefix_size_out = 1u;
    *quote_out = second;
    return CTOOL_TRUE;
  }
  if (first == 'u' && pp_cursor_peek(cursor, 1u, &second) == CTOOL_TRUE &&
      second == '8' && pp_cursor_peek(cursor, 2u, &third) == CTOOL_TRUE &&
      third == '"') {
    *prefix_size_out = 2u;
    *quote_out = third;
    return CTOOL_TRUE;
  }
  return CTOOL_FALSE;
}

static ctool_u32 pp_punctuator_size(pp_cursor_t cursor) {
  static const char *const punctuators[] = {
      "%:%:", ">>=", "<<=", "...", "##", "->", "++", "--", "<<",
      ">>",   "<=",  ">=",  "==", "!=", "&&", "||", "*=", "/=",
      "%=",   "+=",  "-=",  "&=", "^=", "|=", "<:", ":>", "<%",
      "%>",   "%:"};
  static const char singles[] = "[](){}.,&*+-~!/%<>=^|?:;#";
  ctool_u32 index;
  char character;
  for (index = 0u;
       index < (ctool_u32)(sizeof(punctuators) / sizeof(punctuators[0]));
       index++) {
    if (pp_cursor_matches(cursor, punctuators[index]) == CTOOL_TRUE) {
      ctool_u32 size = 0u;
      while (punctuators[index][size] != '\0') {
        size++;
      }
      return size;
    }
  }
  if (pp_cursor_current(&cursor, &character) == CTOOL_FALSE) {
    return 0u;
  }
  for (index = 0u; singles[index] != '\0'; index++) {
    if (character == singles[index]) {
      return 1u;
    }
  }
  return 0u;
}

static ctool_bool pp_lexeme_equal(const pp_input_t *input,
                                  const pp_lexeme_t *lexeme,
                                  const char *literal) {
  ctool_u32 index = 0u;
  while (literal[index] != '\0') {
    if (index >= lexeme->size ||
        input->text[lexeme->offset + index] != literal[index]) {
      return CTOOL_FALSE;
    }
    index++;
  }
  return index == lexeme->size ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool pp_lexeme_is_predefined(const pp_input_t *input,
                                          const pp_lexeme_t *lexeme) {
  return pp_lexeme_equal(input, lexeme, "__FILE__") == CTOOL_TRUE ||
                 pp_lexeme_equal(input, lexeme, "__LINE__") == CTOOL_TRUE ||
                 pp_lexeme_equal(input, lexeme, "__STDC__") == CTOOL_TRUE ||
                 pp_lexeme_equal(input, lexeme, "__STDC_HOSTED__") ==
                     CTOOL_TRUE ||
                 pp_lexeme_equal(input, lexeme, "__STDC_VERSION__") ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t pp_next_token(pp_context_t *context,
                                    pp_lexer_t *lexer,
                                    pp_lexeme_t *lexeme,
                                    ctool_bool *have_token_out) {
  pp_cursor_t *cursor = &lexer->cursor;
  char character;
  char next;
  ctool_u32 prefix_size;
  char quote;
  ctool_u32 punctuator_size;
  ctool_u32 index;

  *have_token_out = CTOOL_FALSE;
  for (;;) {
    if (pp_cursor_current(cursor, &character) == CTOOL_FALSE) {
      return CTOOL_OK;
    }
    if (pp_is_space(character) == CTOOL_TRUE) {
      lexer->leading_space = CTOOL_TRUE;
      if (character == '\n') {
        lexer->at_line_start = CTOOL_TRUE;
      }
      pp_cursor_advance(cursor);
      continue;
    }
    if (character == '/' &&
        pp_cursor_peek(*cursor, 1u, &next) == CTOOL_TRUE && next == '/') {
      lexer->leading_space = CTOOL_TRUE;
      pp_cursor_advance(cursor);
      pp_cursor_advance(cursor);
      while (pp_cursor_current(cursor, &character) == CTOOL_TRUE &&
             character != '\n') {
        pp_cursor_advance(cursor);
      }
      continue;
    }
    if (character == '/' &&
        pp_cursor_peek(*cursor, 1u, &next) == CTOOL_TRUE && next == '*') {
      ctool_u32 comment_line = cursor->line;
      ctool_u32 comment_column = cursor->column;
      ctool_bool closed = CTOOL_FALSE;
      lexer->leading_space = CTOOL_TRUE;
      pp_cursor_advance(cursor);
      pp_cursor_advance(cursor);
      while (pp_cursor_current(cursor, &character) == CTOOL_TRUE) {
        if (character == '*' &&
            pp_cursor_peek(*cursor, 1u, &next) == CTOOL_TRUE && next == '/') {
          pp_cursor_advance(cursor);
          pp_cursor_advance(cursor);
          closed = CTOOL_TRUE;
          break;
        }
        if (character == '\n') {
          lexer->at_line_start = CTOOL_TRUE;
        }
        pp_cursor_advance(cursor);
      }
      if (closed == CTOOL_FALSE) {
        pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LEXICAL,
                comment_line, comment_column,
                "unterminated CupidC block comment");
        return CTOOL_ERR_INPUT;
      }
      continue;
    }
    break;
  }

  lexeme->offset = cursor->position;
  lexeme->line = cursor->line;
  lexeme->column = cursor->column;
  lexeme->leading_space = lexer->leading_space;
  lexeme->at_line_start = lexer->at_line_start;
  if (pp_is_literal_start(*cursor, &prefix_size, &quote) == CTOOL_TRUE) {
    for (index = 0u; index <= prefix_size; index++) {
      pp_cursor_advance(cursor);
    }
    for (;;) {
      if (pp_cursor_current(cursor, &character) == CTOOL_FALSE ||
          character == '\n') {
        pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LEXICAL,
                lexeme->line, lexeme->column,
                quote == '"' ? "unterminated CupidC string literal"
                             : "unterminated CupidC character literal");
        return CTOOL_ERR_INPUT;
      }
      pp_cursor_advance(cursor);
      if (character == quote) {
        break;
      }
      if (character == '\\') {
        if (pp_cursor_current(cursor, &character) == CTOOL_FALSE ||
            character == '\n') {
          pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LEXICAL,
                  lexeme->line, lexeme->column,
                  quote == '"' ? "unterminated CupidC string literal"
                               : "unterminated CupidC character literal");
          return CTOOL_ERR_INPUT;
        }
        pp_cursor_advance(cursor);
      }
    }
    lexeme->kind = quote == '"' ? CTOOL_C_PP_TOKEN_STRING
                                 : CTOOL_C_PP_TOKEN_CHARACTER;
  } else if (pp_is_alpha(character) == CTOOL_TRUE) {
    do {
      pp_cursor_advance(cursor);
    } while (pp_cursor_current(cursor, &character) == CTOOL_TRUE &&
             pp_is_alnum(character) == CTOOL_TRUE);
    lexeme->kind = CTOOL_C_PP_TOKEN_IDENTIFIER;
  } else if (pp_is_digit(character) == CTOOL_TRUE ||
             (character == '.' &&
              pp_cursor_peek(*cursor, 1u, &next) == CTOOL_TRUE &&
              pp_is_digit(next) == CTOOL_TRUE)) {
    pp_cursor_advance(cursor);
    while (pp_cursor_current(cursor, &character) == CTOOL_TRUE) {
      char previous = cursor->input->text[cursor->position - 1u];
      if (pp_is_alnum(character) == CTOOL_TRUE || character == '_' ||
          character == '.') {
        pp_cursor_advance(cursor);
      } else if ((character == '+' || character == '-') &&
                 (previous == 'e' || previous == 'E' || previous == 'p' ||
                  previous == 'P')) {
        pp_cursor_advance(cursor);
      } else {
        break;
      }
    }
    lexeme->kind = CTOOL_C_PP_TOKEN_NUMBER;
  } else {
    punctuator_size = pp_punctuator_size(*cursor);
    if (punctuator_size == 0u) {
      pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LEXICAL,
              lexeme->line, lexeme->column,
              "invalid character in CupidC preprocessing input");
      return CTOOL_ERR_INPUT;
    }
    if (lexeme->at_line_start == CTOOL_TRUE &&
        ((punctuator_size == 1u && character == '#') ||
         (punctuator_size == 2u &&
          pp_cursor_matches(*cursor, "%:") == CTOOL_TRUE))) {
      pp_fail(context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PP_DIAG_DIRECTIVE,
              lexeme->line, lexeme->column,
              "CupidC preprocessing directives are not implemented yet");
      return CTOOL_ERR_UNSUPPORTED;
    }
    for (index = 0u; index < punctuator_size; index++) {
      pp_cursor_advance(cursor);
    }
    lexeme->kind = CTOOL_C_PP_TOKEN_PUNCTUATOR;
  }
  lexeme->size = cursor->position - lexeme->offset;
  if (lexeme->kind == CTOOL_C_PP_TOKEN_IDENTIFIER &&
      pp_lexeme_is_predefined(cursor->input, lexeme) == CTOOL_TRUE) {
    pp_fail(context, CTOOL_ERR_UNSUPPORTED,
            CTOOL_C_PP_DIAG_MACRO_EXPANSION, lexeme->line, lexeme->column,
            "CupidC predefined macro expansion is not implemented yet");
    return CTOOL_ERR_UNSUPPORTED;
  }
  lexer->at_line_start = CTOOL_FALSE;
  lexer->leading_space = CTOOL_FALSE;
  *have_token_out = CTOOL_TRUE;
  return CTOOL_OK;
}

static ctool_status_t pp_lex(pp_context_t *context, const pp_input_t *input,
                             ctool_c_pp_result_t *result) {
  pp_lexer_t lexer;
  pp_lexeme_t lexeme;
  ctool_bool have_token;
  ctool_u32 token_count = 0u;
  ctool_u32 index;
  ctool_c_pp_token_t *tokens = (ctool_c_pp_token_t *)0;
  ctool_status_t status;

  pp_lexer_init(&lexer, input);
  for (;;) {
    status = pp_next_token(context, &lexer, &lexeme, &have_token);
    if (status != CTOOL_OK || have_token == CTOOL_FALSE) {
      break;
    }
    if (token_count == 0xffffffffu) {
      pp_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_C_PP_DIAG_LIMIT,
              lexeme.line, lexeme.column,
              "CupidC preprocessing token count overflowed");
      return CTOOL_ERR_OVERFLOW;
    }
    token_count++;
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (token_count == 0u) {
    return CTOOL_OK;
  }
  status = ctool_arena_alloc_zero(
      ctool_job_arena(context->job), token_count,
      (ctool_u32)sizeof(*tokens), (ctool_u32)sizeof(void *),
      (void **)&tokens);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, 0u, 0u,
            "CupidC preprocessing token storage limit exceeded");
    return status;
  }

  pp_lexer_init(&lexer, input);
  for (index = 0u; index < token_count; index++) {
    status = pp_next_token(context, &lexer, &lexeme, &have_token);
    if (status != CTOOL_OK || have_token == CTOOL_FALSE) {
      pp_fail(context, CTOOL_ERR_INTERNAL, CTOOL_C_PP_DIAG_LIMIT, 0u, 0u,
              "CupidC preprocessing tokenization became inconsistent");
      return status != CTOOL_OK ? status : CTOOL_ERR_INTERNAL;
    }
    tokens[index].kind = lexeme.kind;
    tokens[index].spelling.data = input->text + lexeme.offset;
    tokens[index].spelling.size = lexeme.size;
    tokens[index].location.path = context->path;
    tokens[index].location.line = lexeme.line;
    tokens[index].location.column = lexeme.column;
    tokens[index].pack_alignment = 0u;
  }
  result->tokens = tokens;
  result->token_count = token_count;
  return CTOOL_OK;
}

ctool_status_t ctool_c_preprocess(ctool_job_t *job,
                                  const ctool_source_t *primary,
                                  const ctool_c_pp_request_t *request,
                                  ctool_c_pp_result_t *result_out) {
  pp_context_t context;
  pp_input_t input;
  ctool_arena_t *arena;
  ctool_arena_mark_t mark;
  ctool_status_t status;
  ctool_status_t published;
  ctool_status_t rewound;
  ctool_string_t path_copy;

  if (result_out != (ctool_c_pp_result_t *)0) {
    pp_zero_result(result_out);
  }
  if (job == (ctool_job_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  status = pp_validate_request(primary, request, result_out);
  if (status != CTOOL_OK) {
    ctool_diagnostic_t diagnostic;
    diagnostic.severity = CTOOL_DIAG_ERROR;
    diagnostic.code = CTOOL_C_PP_DIAG_INVALID_REQUEST;
    diagnostic.path = primary != (const ctool_source_t *)0 &&
                              ctool_path_is_canonical(&primary->path) ==
                                  CTOOL_TRUE
                          ? primary->path.text
                          : ctool_string("/cupidc");
    diagnostic.line = 0u;
    diagnostic.column = 0u;
    diagnostic.message = ctool_string(
        status == CTOOL_ERR_UNSUPPORTED
            ? "unsupported CupidC preprocessing request"
            : "invalid CupidC preprocessing request");
    (void)ctool_job_emit(job, &diagnostic);
    return status;
  }
  if (request->macro_action_count != 0u ||
      request->forced_include_count != 0u) {
    ctool_diagnostic_t diagnostic;
    diagnostic.severity = CTOOL_DIAG_ERROR;
    diagnostic.code = CTOOL_C_PP_DIAG_UNSUPPORTED_CONFIGURATION;
    diagnostic.path = primary->path.text;
    diagnostic.line = 0u;
    diagnostic.column = 0u;
    diagnostic.message = ctool_string(
        request->macro_action_count != 0u
            ? "CupidC command-line macro actions are not implemented yet"
            : "CupidC forced includes are not implemented yet");
    status = ctool_job_emit(job, &diagnostic);
    return status == CTOOL_OK ? CTOOL_ERR_UNSUPPORTED : status;
  }

  arena = ctool_job_arena(job);
  mark = ctool_arena_mark(arena);
  context.job = job;
  context.source = primary;
  context.request = request;
  context.path = primary->path.text;
  context.failure_status = CTOOL_OK;
  context.failure_code = 0u;
  context.failure_line = 0u;
  context.failure_column = 0u;
  context.failure_message = "CupidC preprocessing failed";
  input.text = (char *)0;
  input.size = 0u;
  input.splice_offsets = (ctool_u32 *)0;
  input.splice_count = 0u;

  status = ctool_arena_copy_string(arena, primary->path.text, &path_copy);
  if (status != CTOOL_OK) {
    pp_fail(&context, status, CTOOL_C_PP_DIAG_LIMIT, 0u, 0u,
            "CupidC preprocessing path storage limit exceeded");
  }
  if (status == CTOOL_OK) {
    context.path = path_copy;
  }
  if (status == CTOOL_OK) {
    status = pp_normalize(&context, &input);
  }
  if (status == CTOOL_OK) {
    status = pp_lex(&context, &input, result_out);
  }
  if (status == CTOOL_OK) {
    return CTOOL_OK;
  }

  pp_zero_result(result_out);
  published = context.failure_status != CTOOL_OK
                  ? pp_publish_failure(&context)
                  : status;
  rewound = ctool_arena_rewind(arena, mark);
  return rewound == CTOOL_OK ? published : rewound;
}
