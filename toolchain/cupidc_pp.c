#include "cupidc_pp.h"

#define PP_INCLUDE_DEPTH_LIMIT 64u
#define PP_CONDITIONAL_DEPTH_LIMIT 64u
#define PP_MACRO_EXPANSION_DEPTH_LIMIT 256u
#define PP_OUTPUT_CHUNK_TOKENS 128u
#define PP_TOKEN_LIMIT 1000000u
#define PP_LEXER_DIRECTIVE_NONE 0u
#define PP_LEXER_DIRECTIVE_MARKER 1u
#define PP_LEXER_DIRECTIVE_INCLUDE 2u

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
  ctool_bool header_name;
} pp_lexeme_t;

typedef struct {
  pp_cursor_t cursor;
  ctool_bool at_line_start;
  ctool_bool leading_space;
  ctool_u32 directive_state;
} pp_lexer_t;

typedef struct {
  ctool_c_pp_token_kind_t kind;
  ctool_string_t spelling;
  ctool_c_pp_location_t location;
  ctool_u32 pack_alignment;
  ctool_bool leading_space;
  ctool_bool at_line_start;
  ctool_bool header_name;
} pp_token_t;

typedef struct pp_macro pp_macro_t;
typedef struct pp_source_cache pp_source_cache_t;

struct pp_macro {
  ctool_string_t name;
  ctool_bool defined;
  ctool_bool function_like;
  const pp_token_t *replacement;
  ctool_u32 replacement_count;
  pp_macro_t *next;
};

struct pp_source_cache {
  ctool_source_t source;
  pp_token_t *tokens;
  ctool_u32 token_count;
  ctool_bool tokenized;
  pp_source_cache_t *next;
};

typedef struct pp_output_chunk pp_output_chunk_t;

struct pp_output_chunk {
  ctool_c_pp_token_t tokens[PP_OUTPUT_CHUNK_TOKENS];
  ctool_u32 count;
  pp_output_chunk_t *next;
};

typedef struct pp_conditional pp_conditional_t;

struct pp_conditional {
  ctool_bool parent_active;
  ctool_bool current_active;
  ctool_bool branch_taken;
  ctool_bool saw_else;
  ctool_u32 opening_line;
  ctool_u32 opening_column;
  pp_conditional_t *previous;
};

typedef struct {
  ctool_job_t *job;
  const ctool_source_t *source;
  const ctool_c_pp_request_t *request;
  ctool_string_t path;
  ctool_string_t failure_path;
  pp_macro_t *macros;
  pp_source_cache_t *sources;
  pp_output_chunk_t *output_head;
  pp_output_chunk_t *output_tail;
  ctool_u32 output_count;
  ctool_u32 include_depth;
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
    const ctool_c_pp_result_t *result, const ctool_limits_t *limits) {
  ctool_u32 index;
  if (source == (const ctool_source_t *)0 ||
      request == (const ctool_c_pp_request_t *)0 ||
      result == (const ctool_c_pp_result_t *)0 ||
      limits == (const ctool_limits_t *)0 ||
      source->path.text.data == (const char *)0 ||
      source->path.text.size == 0u ||
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
  if (source->path.text.size > limits->path_bytes ||
      source->contents.size > limits->source_bytes) {
    return CTOOL_ERR_LIMIT;
  }
  if (ctool_path_is_canonical(&source->path) == CTOOL_FALSE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  for (index = 0u; index < request->include_root_count; index++) {
    const ctool_c_pp_include_root_t *root = &request->include_roots[index];
    if (root->directory.text.data == (const char *)0 ||
        root->directory.text.size == 0u || root->forms == 0u ||
        (root->forms & ~(CTOOL_C_PP_INCLUDE_QUOTED |
                         CTOOL_C_PP_INCLUDE_ANGLE)) != 0u) {
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
    if (root->directory.text.size > limits->path_bytes) {
      return CTOOL_ERR_LIMIT;
    }
    if (ctool_path_is_canonical(&root->directory) == CTOOL_FALSE) {
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
  }
  for (index = 0u; index < request->forced_include_count; index++) {
    if (request->forced_includes[index].text.data == (const char *)0 ||
        request->forced_includes[index].text.size == 0u) {
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
    if (request->forced_includes[index].text.size > limits->path_bytes) {
      return CTOOL_ERR_LIMIT;
    }
    if (ctool_path_is_canonical(&request->forced_includes[index]) ==
        CTOOL_FALSE) {
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
  }
  for (index = 0u; index < request->macro_action_count; index++) {
    const ctool_c_pp_macro_action_t *action = &request->macro_actions[index];
    if ((action->kind != CTOOL_C_PP_MACRO_DEFINE &&
         action->kind != CTOOL_C_PP_MACRO_UNDEF) ||
        action->name.data == (const char *)0 || action->name.size == 0u ||
        (action->replacement.data == (const char *)0 &&
         action->replacement.size != 0u) ||
        (action->kind == CTOOL_C_PP_MACRO_UNDEF &&
         action->replacement.size != 0u)) {
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
    if (action->name.size > limits->source_bytes ||
        action->replacement.size > limits->source_bytes) {
      return CTOOL_ERR_LIMIT;
    }
    if (pp_string_is_identifier(action->name) == CTOOL_FALSE) {
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
  context->failure_path = context->path;
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
  diagnostic.path = context->failure_path;
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
  lexer->directive_state = PP_LEXER_DIRECTIVE_NONE;
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
        lexer->directive_state = PP_LEXER_DIRECTIVE_NONE;
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
          lexer->directive_state = PP_LEXER_DIRECTIVE_NONE;
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
  lexeme->header_name = CTOOL_FALSE;
  if (lexer->directive_state == PP_LEXER_DIRECTIVE_INCLUDE &&
      character == '<') {
    pp_cursor_t header = *cursor;
    pp_cursor_advance(&header);
    while (pp_cursor_current(&header, &character) == CTOOL_TRUE &&
           character != '\n' && character != '>') {
      pp_cursor_advance(&header);
    }
    if (pp_cursor_current(&header, &character) == CTOOL_TRUE &&
        character == '>') {
      pp_cursor_advance(&header);
      *cursor = header;
      lexeme->kind = CTOOL_C_PP_TOKEN_PUNCTUATOR;
      lexeme->header_name = CTOOL_TRUE;
    }
  }
  if (lexeme->header_name == CTOOL_FALSE) {
    (void)pp_cursor_current(cursor, &character);
  }
  if (lexeme->header_name == CTOOL_TRUE) {
    /* Header-name contents are not tokenized as comments or whitespace. */
  } else if (pp_is_literal_start(*cursor, &prefix_size, &quote) ==
             CTOOL_TRUE) {
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
    for (index = 0u; index < punctuator_size; index++) {
      pp_cursor_advance(cursor);
    }
    lexeme->kind = CTOOL_C_PP_TOKEN_PUNCTUATOR;
  }
  lexeme->size = cursor->position - lexeme->offset;
  if (lexeme->header_name == CTOOL_TRUE) {
    lexer->directive_state = PP_LEXER_DIRECTIVE_NONE;
  } else if (lexeme->at_line_start == CTOOL_TRUE &&
             lexeme->kind == CTOOL_C_PP_TOKEN_PUNCTUATOR &&
             (pp_lexeme_equal(cursor->input, lexeme, "#") == CTOOL_TRUE ||
              pp_lexeme_equal(cursor->input, lexeme, "%:") == CTOOL_TRUE)) {
    lexer->directive_state = PP_LEXER_DIRECTIVE_MARKER;
  } else if (lexer->directive_state == PP_LEXER_DIRECTIVE_MARKER &&
             lexeme->kind == CTOOL_C_PP_TOKEN_IDENTIFIER &&
             pp_lexeme_equal(cursor->input, lexeme, "include") ==
                 CTOOL_TRUE) {
    lexer->directive_state = PP_LEXER_DIRECTIVE_INCLUDE;
  } else {
    lexer->directive_state = PP_LEXER_DIRECTIVE_NONE;
  }
  lexer->at_line_start = CTOOL_FALSE;
  lexer->leading_space = CTOOL_FALSE;
  *have_token_out = CTOOL_TRUE;
  return CTOOL_OK;
}

static ctool_status_t pp_tokenize(pp_context_t *context,
                                  const pp_input_t *input,
                                  pp_token_t **tokens_out,
                                  ctool_u32 *token_count_out) {
  pp_lexer_t lexer;
  pp_lexeme_t lexeme;
  ctool_bool have_token;
  ctool_u32 token_count = 0u;
  ctool_u32 index;
  pp_token_t *tokens = (pp_token_t *)0;
  ctool_status_t status;

  *tokens_out = (pp_token_t *)0;
  *token_count_out = 0u;
  pp_lexer_init(&lexer, input);
  for (;;) {
    status = pp_next_token(context, &lexer, &lexeme, &have_token);
    if (status != CTOOL_OK || have_token == CTOOL_FALSE) {
      break;
    }
    if (token_count == PP_TOKEN_LIMIT) {
      pp_fail(context, CTOOL_ERR_LIMIT, CTOOL_C_PP_DIAG_LIMIT,
              lexeme.line, lexeme.column,
              "CupidC preprocessing token limit exceeded");
      return CTOOL_ERR_LIMIT;
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
    tokens[index].leading_space = lexeme.leading_space;
    tokens[index].at_line_start = lexeme.at_line_start;
    tokens[index].header_name = lexeme.header_name;
  }
  *tokens_out = tokens;
  *token_count_out = token_count;
  return CTOOL_OK;
}

static ctool_bool pp_string_equal(ctool_string_t left,
                                  ctool_string_t right) {
  ctool_u32 index;
  if (left.size != right.size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < left.size; index++) {
    if (left.data[index] != right.data[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool pp_string_equal_literal(ctool_string_t value,
                                          const char *literal) {
  ctool_u32 size = 0u;
  while (literal[size] != '\0') {
    size++;
  }
  return pp_string_equal(value, (ctool_string_t){literal, size});
}

static ctool_bool pp_token_equal_literal(const pp_token_t *token,
                                         const char *literal) {
  return pp_string_equal_literal(token->spelling, literal);
}

static ctool_bool pp_name_is_predefined(ctool_string_t name) {
  return pp_string_equal_literal(name, "__FILE__") == CTOOL_TRUE ||
                 pp_string_equal_literal(name, "__LINE__") == CTOOL_TRUE ||
                 pp_string_equal_literal(name, "__STDC__") == CTOOL_TRUE ||
                 pp_string_equal_literal(name, "__STDC_HOSTED__") ==
                     CTOOL_TRUE ||
                 pp_string_equal_literal(name, "__STDC_VERSION__") ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static pp_macro_t *pp_macro_entry(pp_context_t *context,
                                  ctool_string_t name) {
  pp_macro_t *macro = context->macros;
  while (macro != (pp_macro_t *)0) {
    if (pp_string_equal(macro->name, name) == CTOOL_TRUE) {
      return macro;
    }
    macro = macro->next;
  }
  return (pp_macro_t *)0;
}

static pp_macro_t *pp_macro_find(pp_context_t *context,
                                 ctool_string_t name) {
  pp_macro_t *macro = pp_macro_entry(context, name);
  return macro != (pp_macro_t *)0 && macro->defined == CTOOL_TRUE
             ? macro
             : (pp_macro_t *)0;
}

static ctool_bool pp_macro_replacement_equal(const pp_macro_t *macro,
                                             const pp_token_t *replacement,
                                             ctool_u32 replacement_count) {
  ctool_u32 index;
  if (macro->function_like == CTOOL_TRUE ||
      macro->replacement_count != replacement_count) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < replacement_count; index++) {
    if (macro->replacement[index].kind != replacement[index].kind ||
        pp_string_equal(macro->replacement[index].spelling,
                        replacement[index].spelling) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t pp_define_object_macro(
    pp_context_t *context, ctool_string_t name,
    const pp_token_t *replacement, ctool_u32 replacement_count,
    ctool_u32 line, ctool_u32 column) {
  pp_macro_t *existing;
  pp_macro_t *macro;
  ctool_status_t status;
  if (pp_name_is_predefined(name) == CTOOL_TRUE) {
    pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_DEFINITION,
            line, column, "CupidC predefined macros cannot be redefined");
    return CTOOL_ERR_INPUT;
  }
  existing = pp_macro_find(context, name);
  if (existing != (pp_macro_t *)0) {
    if (pp_macro_replacement_equal(existing, replacement,
                                   replacement_count) == CTOOL_TRUE) {
      return CTOOL_OK;
    }
    pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_REDEFINITION,
            line, column, "CupidC macro has a different redefinition");
    return CTOOL_ERR_INPUT;
  }
  status = ctool_arena_alloc_zero(
      ctool_job_arena(context->job), 1u, (ctool_u32)sizeof(*macro),
      (ctool_u32)sizeof(void *), (void **)&macro);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, line, column,
            "CupidC macro storage limit exceeded");
    return status;
  }
  status = ctool_arena_copy_string(ctool_job_arena(context->job), name,
                                   &macro->name);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, line, column,
            "CupidC macro name storage limit exceeded");
    return status;
  }
  macro->defined = CTOOL_TRUE;
  macro->function_like = CTOOL_FALSE;
  macro->replacement = replacement;
  macro->replacement_count = replacement_count;
  macro->next = context->macros;
  context->macros = macro;
  return CTOOL_OK;
}

static ctool_status_t pp_undefine_macro(pp_context_t *context,
                                       ctool_string_t name,
                                       ctool_u32 line,
                                       ctool_u32 column) {
  pp_macro_t *macro;
  ctool_status_t status;
  if (pp_name_is_predefined(name) == CTOOL_TRUE) {
    pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_DEFINITION,
            line, column, "CupidC predefined macros cannot be undefined");
    return CTOOL_ERR_INPUT;
  }
  status = ctool_arena_alloc_zero(
      ctool_job_arena(context->job), 1u, (ctool_u32)sizeof(*macro),
      (ctool_u32)sizeof(void *), (void **)&macro);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, line, column,
            "CupidC macro storage limit exceeded");
    return status;
  }
  status = ctool_arena_copy_string(ctool_job_arena(context->job), name,
                                   &macro->name);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, line, column,
            "CupidC macro name storage limit exceeded");
    return status;
  }
  macro->defined = CTOOL_FALSE;
  macro->next = context->macros;
  context->macros = macro;
  return CTOOL_OK;
}

static ctool_status_t pp_output_append(pp_context_t *context,
                                       const pp_token_t *token) {
  pp_output_chunk_t *chunk = context->output_tail;
  ctool_c_pp_token_t *output;
  ctool_status_t status;
  if (context->output_count == PP_TOKEN_LIMIT) {
    pp_fail(context, CTOOL_ERR_LIMIT, CTOOL_C_PP_DIAG_LIMIT,
            token->location.line, token->location.column,
            "CupidC expanded token limit exceeded");
    return CTOOL_ERR_LIMIT;
  }
  if (chunk == (pp_output_chunk_t *)0 ||
      chunk->count == PP_OUTPUT_CHUNK_TOKENS) {
    status = ctool_arena_alloc_zero(
        ctool_job_arena(context->job), 1u, (ctool_u32)sizeof(*chunk),
        (ctool_u32)sizeof(void *), (void **)&chunk);
    if (status != CTOOL_OK) {
      pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
              token->location.line, token->location.column,
              "CupidC expanded token storage limit exceeded");
      return status;
    }
    if (context->output_tail == (pp_output_chunk_t *)0) {
      context->output_head = chunk;
    } else {
      context->output_tail->next = chunk;
    }
    context->output_tail = chunk;
  }
  output = &chunk->tokens[chunk->count];
  output->kind = token->kind;
  output->spelling = token->spelling;
  output->location = token->location;
  output->pack_alignment = token->pack_alignment;
  chunk->count++;
  context->output_count++;
  return CTOOL_OK;
}

static ctool_bool pp_macro_is_disabled(pp_macro_t *macro,
                                       pp_macro_t *const *disabled,
                                       ctool_u32 disabled_count) {
  ctool_u32 index;
  for (index = 0u; index < disabled_count; index++) {
    if (disabled[index] == macro) {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_status_t pp_expand_token(pp_context_t *context,
                                      const pp_token_t *token,
                                      pp_macro_t **disabled,
                                      ctool_u32 disabled_count) {
  pp_macro_t *macro;
  ctool_u32 index;
  ctool_status_t status;
  if (token->kind != CTOOL_C_PP_TOKEN_IDENTIFIER) {
    return pp_output_append(context, token);
  }
  if (pp_name_is_predefined(token->spelling) == CTOOL_TRUE) {
    pp_fail(context, CTOOL_ERR_UNSUPPORTED,
            CTOOL_C_PP_DIAG_MACRO_EXPANSION, token->location.line,
            token->location.column,
            "CupidC predefined macro expansion is not implemented yet");
    return CTOOL_ERR_UNSUPPORTED;
  }
  macro = pp_macro_find(context, token->spelling);
  if (macro == (pp_macro_t *)0 ||
      pp_macro_is_disabled(macro, disabled, disabled_count) == CTOOL_TRUE) {
    return pp_output_append(context, token);
  }
  if (disabled_count == PP_MACRO_EXPANSION_DEPTH_LIMIT) {
    pp_fail(context, CTOOL_ERR_LIMIT, CTOOL_C_PP_DIAG_MACRO_EXPANSION,
            token->location.line, token->location.column,
            "CupidC macro expansion depth limit exceeded");
    return CTOOL_ERR_LIMIT;
  }
  disabled[disabled_count] = macro;
  for (index = 0u; index < macro->replacement_count; index++) {
    pp_token_t replacement = macro->replacement[index];
    replacement.location = token->location;
    replacement.pack_alignment = token->pack_alignment;
    status = pp_expand_token(context, &replacement, disabled,
                             disabled_count + 1u);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t pp_emit_expanded(pp_context_t *context,
                                       const pp_token_t *token) {
  pp_macro_t *disabled[PP_MACRO_EXPANSION_DEPTH_LIMIT];
  return pp_expand_token(context, token, disabled, 0u);
}

static ctool_status_t pp_publish_output(pp_context_t *context,
                                        ctool_c_pp_result_t *result) {
  pp_output_chunk_t *chunk;
  ctool_c_pp_token_t *tokens;
  ctool_u32 offset = 0u;
  ctool_u32 index;
  ctool_status_t status;
  if (context->output_count == 0u) {
    return CTOOL_OK;
  }
  status = ctool_arena_alloc_zero(
      ctool_job_arena(context->job), context->output_count,
      (ctool_u32)sizeof(*tokens), (ctool_u32)sizeof(void *),
      (void **)&tokens);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, 0u, 0u,
            "CupidC result token storage limit exceeded");
    return status;
  }
  chunk = context->output_head;
  while (chunk != (pp_output_chunk_t *)0) {
    for (index = 0u; index < chunk->count; index++) {
      tokens[offset] = chunk->tokens[index];
      offset++;
    }
    chunk = chunk->next;
  }
  result->tokens = tokens;
  result->token_count = context->output_count;
  return CTOOL_OK;
}

static ctool_bool pp_token_is_identifier(const pp_token_t *token) {
  return token->kind == CTOOL_C_PP_TOKEN_IDENTIFIER ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
}

static pp_source_cache_t *pp_source_cache_find(
    pp_context_t *context, const ctool_path_t *path) {
  pp_source_cache_t *entry = context->sources;
  while (entry != (pp_source_cache_t *)0) {
    if (ctool_path_equal(&entry->source.path, path) == CTOOL_TRUE) {
      return entry;
    }
    entry = entry->next;
  }
  return (pp_source_cache_t *)0;
}

static ctool_status_t pp_source_cache_add(pp_context_t *context,
                                          const ctool_source_t *source,
                                          pp_source_cache_t **entry_out) {
  pp_source_cache_t *entry;
  ctool_status_t status = ctool_arena_alloc_zero(
      ctool_job_arena(context->job), 1u, (ctool_u32)sizeof(*entry),
      (ctool_u32)sizeof(void *), (void **)&entry);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, 0u, 0u,
            "CupidC source cache storage limit exceeded");
    return status;
  }
  entry->source = *source;
  entry->tokens = (pp_token_t *)0;
  entry->token_count = 0u;
  entry->tokenized = CTOOL_FALSE;
  entry->next = context->sources;
  context->sources = entry;
  *entry_out = entry;
  return CTOOL_OK;
}

static ctool_bool pp_token_is_directive_marker(const pp_token_t *token) {
  return token->at_line_start == CTOOL_TRUE &&
                 (pp_token_equal_literal(token, "#") == CTOOL_TRUE ||
                  pp_token_equal_literal(token, "%:") == CTOOL_TRUE)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_u32 pp_directive_end(const pp_token_t *tokens,
                                  ctool_u32 token_count,
                                  ctool_u32 begin) {
  ctool_u32 end = begin;
  while (end < token_count && tokens[end].at_line_start == CTOOL_FALSE) {
    end++;
  }
  return end;
}

static ctool_status_t pp_directive_error(pp_context_t *context,
                                         const pp_token_t *marker,
                                         ctool_u32 code,
                                         const char *message) {
  pp_fail(context, CTOOL_ERR_INPUT, code, marker->location.line,
          marker->location.column, message);
  return CTOOL_ERR_INPUT;
}

static ctool_status_t pp_copy_token_range(pp_context_t *context,
                                          const pp_token_t *tokens,
                                          ctool_u32 count,
                                          pp_token_t **copy_out) {
  pp_token_t *copy;
  ctool_u32 index;
  ctool_status_t status;
  *copy_out = (pp_token_t *)0;
  if (count == 0u) {
    return CTOOL_OK;
  }
  status = ctool_arena_alloc_zero(
      ctool_job_arena(context->job), count, (ctool_u32)sizeof(*copy),
      (ctool_u32)sizeof(void *), (void **)&copy);
  if (status != CTOOL_OK) {
    return status;
  }
  for (index = 0u; index < count; index++) {
    copy[index] = tokens[index];
  }
  *copy_out = copy;
  return CTOOL_OK;
}

static ctool_status_t pp_handle_define(pp_context_t *context,
                                       const pp_token_t *marker,
                                       const pp_token_t *tokens,
                                       ctool_u32 begin,
                                       ctool_u32 end) {
  pp_token_t *replacement = (pp_token_t *)0;
  ctool_u32 replacement_count;
  ctool_status_t status;
  if (begin >= end || pp_token_is_identifier(&tokens[begin]) == CTOOL_FALSE) {
    return pp_directive_error(context, marker,
                              CTOOL_C_PP_DIAG_MACRO_DEFINITION,
                              "CupidC #define requires a macro name");
  }
  if (begin + 1u < end &&
      pp_token_equal_literal(&tokens[begin + 1u], "(") == CTOOL_TRUE &&
      tokens[begin + 1u].leading_space == CTOOL_FALSE) {
    pp_fail(context, CTOOL_ERR_UNSUPPORTED,
            CTOOL_C_PP_DIAG_MACRO_DEFINITION,
            tokens[begin].location.line, tokens[begin].location.column,
            "CupidC function-like macros are not implemented yet");
    return CTOOL_ERR_UNSUPPORTED;
  }
  replacement_count = end - begin - 1u;
  status = pp_copy_token_range(context, tokens + begin + 1u,
                               replacement_count, &replacement);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
            tokens[begin].location.line, tokens[begin].location.column,
            "CupidC macro replacement storage limit exceeded");
    return status;
  }
  return pp_define_object_macro(
      context, tokens[begin].spelling, replacement, replacement_count,
      tokens[begin].location.line, tokens[begin].location.column);
}

static ctool_status_t pp_handle_undef(pp_context_t *context,
                                      const pp_token_t *marker,
                                      const pp_token_t *tokens,
                                      ctool_u32 begin,
                                      ctool_u32 end) {
  if (end - begin != 1u ||
      pp_token_is_identifier(&tokens[begin]) == CTOOL_FALSE) {
    return pp_directive_error(context, marker,
                              CTOOL_C_PP_DIAG_MACRO_DEFINITION,
                              "CupidC #undef requires one macro name");
  }
  return pp_undefine_macro(context, tokens[begin].spelling,
                           tokens[begin].location.line,
                           tokens[begin].location.column);
}

static ctool_status_t pp_include_spelling(
    pp_context_t *context, const pp_token_t *marker,
    const pp_token_t *tokens, ctool_u32 begin, ctool_u32 end,
    ctool_bool *quoted_out, ctool_string_t *spelling_out) {
  if (end - begin == 1u && tokens[begin].header_name == CTOOL_TRUE &&
      tokens[begin].spelling.size >= 2u &&
      tokens[begin].spelling.data[0] == '<' &&
      tokens[begin].spelling.data[tokens[begin].spelling.size - 1u] == '>') {
    if (tokens[begin].spelling.size == 2u) {
      return pp_directive_error(context, marker,
                                CTOOL_C_PP_DIAG_INCLUDE_PATH,
                                "CupidC angle include path is empty");
    }
    spelling_out->data = tokens[begin].spelling.data + 1u;
    spelling_out->size = tokens[begin].spelling.size - 2u;
    *quoted_out = CTOOL_FALSE;
    return CTOOL_OK;
  }
  if (end - begin == 1u && tokens[begin].kind == CTOOL_C_PP_TOKEN_STRING &&
      tokens[begin].spelling.size >= 2u &&
      tokens[begin].spelling.data[0] == '"' &&
      tokens[begin].spelling.data[tokens[begin].spelling.size - 1u] == '"') {
    if (tokens[begin].spelling.size == 2u) {
      return pp_directive_error(context, marker,
                                CTOOL_C_PP_DIAG_INCLUDE_PATH,
                                "CupidC quoted include path is empty");
    }
    spelling_out->data = tokens[begin].spelling.data + 1u;
    spelling_out->size = tokens[begin].spelling.size - 2u;
    *quoted_out = CTOOL_TRUE;
    return CTOOL_OK;
  }
  if (begin < end && pp_token_is_identifier(&tokens[begin]) == CTOOL_TRUE) {
    pp_fail(context, CTOOL_ERR_UNSUPPORTED,
            CTOOL_C_PP_DIAG_INCLUDE_PATH, marker->location.line,
            marker->location.column,
            "CupidC macro-expanded includes are not implemented yet");
    return CTOOL_ERR_UNSUPPORTED;
  }
  return pp_directive_error(context, marker,
                            CTOOL_C_PP_DIAG_INCLUDE_PATH,
                            "CupidC #include requires \"path\" or <path>");
}

static ctool_status_t pp_process_source(pp_context_t *context,
                                        const ctool_source_t *source);

static ctool_status_t pp_try_include(pp_context_t *context,
                                     const ctool_path_t *base,
                                     ctool_string_t spelling,
                                     ctool_source_t *source_out,
                                     ctool_bool *found_out) {
  ctool_arena_t *arena = ctool_job_arena(context->job);
  ctool_arena_mark_t mark = ctool_arena_mark(arena);
  ctool_path_t path;
  pp_source_cache_t *cached;
  ctool_status_t status;
  *found_out = CTOOL_FALSE;
  status = ctool_path_resolve(arena, base, spelling,
                              ctool_job_limits(context->job)->path_bytes,
                              &path);
  if (status == CTOOL_OK) {
    cached = pp_source_cache_find(context, &path);
    if (cached != (pp_source_cache_t *)0) {
      *source_out = cached->source;
      *found_out = CTOOL_TRUE;
      (void)ctool_arena_rewind(arena, mark);
      return CTOOL_OK;
    }
    status = ctool_job_load_source(context->job, &path, source_out);
  }
  if (status == CTOOL_ERR_NOT_FOUND) {
    (void)ctool_arena_rewind(arena, mark);
    return CTOOL_OK;
  }
  if (status != CTOOL_OK) {
    (void)ctool_arena_rewind(arena, mark);
    return status;
  }
  *found_out = CTOOL_TRUE;
  return CTOOL_OK;
}

static ctool_status_t pp_enter_include(pp_context_t *context,
                                       const pp_token_t *marker,
                                       const ctool_source_t *source) {
  ctool_status_t status;
  if (context->include_depth == PP_INCLUDE_DEPTH_LIMIT) {
    pp_fail(context, CTOOL_ERR_LIMIT, CTOOL_C_PP_DIAG_INCLUDE_DEPTH,
            marker->location.line, marker->location.column,
            "CupidC include depth limit exceeded");
    return CTOOL_ERR_LIMIT;
  }
  context->include_depth++;
  status = pp_process_source(context, source);
  context->include_depth--;
  return status;
}

static ctool_u32 pp_include_diagnostic_code(ctool_status_t status) {
  if (status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
      status == CTOOL_ERR_NO_MEMORY) {
    return CTOOL_C_PP_DIAG_LIMIT;
  }
  return CTOOL_C_PP_DIAG_INCLUDE_PATH;
}

static ctool_status_t pp_handle_include(pp_context_t *context,
                                        const pp_token_t *marker,
                                        const pp_token_t *tokens,
                                        ctool_u32 begin,
                                        ctool_u32 end) {
  ctool_bool quoted;
  ctool_string_t spelling;
  ctool_path_t parent;
  ctool_source_t included;
  ctool_bool found;
  ctool_u32 index;
  ctool_status_t status = pp_include_spelling(
      context, marker, tokens, begin, end, &quoted, &spelling);
  if (status != CTOOL_OK) {
    return status;
  }
  if (quoted == CTOOL_TRUE) {
    status = ctool_path_parent(&context->source->path, &parent);
    if (status == CTOOL_OK) {
      status = pp_try_include(context, &parent, spelling, &included, &found);
    }
    if (status != CTOOL_OK) {
      pp_fail(context, status, pp_include_diagnostic_code(status),
              marker->location.line, marker->location.column,
              "CupidC quoted include path could not be resolved");
      return status;
    }
    if (found == CTOOL_TRUE) {
      return pp_enter_include(context, marker, &included);
    }
  }
  for (index = 0u; index < context->request->include_root_count; index++) {
    const ctool_c_pp_include_root_t *root =
        &context->request->include_roots[index];
    ctool_u32 required = quoted == CTOOL_TRUE ? CTOOL_C_PP_INCLUDE_QUOTED
                                               : CTOOL_C_PP_INCLUDE_ANGLE;
    if ((root->forms & required) == 0u) {
      continue;
    }
    status = pp_try_include(context, &root->directory, spelling, &included,
                            &found);
    if (status != CTOOL_OK) {
      pp_fail(context, status, pp_include_diagnostic_code(status),
              marker->location.line, marker->location.column,
              "CupidC include root could not be searched");
      return status;
    }
    if (found == CTOOL_TRUE) {
      return pp_enter_include(context, marker, &included);
    }
  }
  pp_fail(context, CTOOL_ERR_NOT_FOUND, CTOOL_C_PP_DIAG_INCLUDE_NOT_FOUND,
          marker->location.line, marker->location.column,
          "CupidC include file was not found");
  return CTOOL_ERR_NOT_FOUND;
}

static ctool_status_t pp_validate_condition_name(
    pp_context_t *context, const pp_token_t *marker,
    const pp_token_t *tokens, ctool_u32 begin, ctool_u32 end);

static ctool_status_t pp_condition_name(pp_context_t *context,
                                        const pp_token_t *marker,
                                        const pp_token_t *tokens,
                                        ctool_u32 begin,
                                        ctool_u32 end,
                                        ctool_bool negate,
                                        ctool_bool *value_out) {
  ctool_bool defined;
  ctool_status_t status = pp_validate_condition_name(
      context, marker, tokens, begin, end);
  if (status != CTOOL_OK) {
    return status;
  }
  if (pp_name_is_predefined(tokens[begin].spelling) == CTOOL_TRUE) {
    pp_fail(context, CTOOL_ERR_UNSUPPORTED,
            CTOOL_C_PP_DIAG_MACRO_EXPANSION,
            tokens[begin].location.line, tokens[begin].location.column,
            "CupidC predefined macro expansion is not implemented yet");
    return CTOOL_ERR_UNSUPPORTED;
  }
  defined = pp_macro_find(context, tokens[begin].spelling) !=
                    (pp_macro_t *)0
                ? CTOOL_TRUE
                : CTOOL_FALSE;
  *value_out = negate == CTOOL_TRUE
                   ? (defined == CTOOL_TRUE ? CTOOL_FALSE : CTOOL_TRUE)
                   : defined;
  return CTOOL_OK;
}

static ctool_status_t pp_validate_condition_name(
    pp_context_t *context, const pp_token_t *marker,
    const pp_token_t *tokens, ctool_u32 begin, ctool_u32 end) {
  if (end - begin != 1u ||
      pp_token_is_identifier(&tokens[begin]) == CTOOL_FALSE) {
    return pp_directive_error(context, marker,
                              CTOOL_C_PP_DIAG_CONDITIONAL,
                              "CupidC conditional requires one macro name");
  }
  return CTOOL_OK;
}

static ctool_status_t pp_simple_if_expression(
    pp_context_t *context, const pp_token_t *marker,
    const pp_token_t *tokens, ctool_u32 begin, ctool_u32 end,
    ctool_bool *value_out) {
  if (begin == end) {
    return pp_directive_error(context, marker,
                              CTOOL_C_PP_DIAG_CONDITIONAL,
                              "CupidC conditional requires an expression");
  }
  if (end - begin == 1u && tokens[begin].kind == CTOOL_C_PP_TOKEN_NUMBER &&
      (pp_token_equal_literal(&tokens[begin], "0") == CTOOL_TRUE ||
       pp_token_equal_literal(&tokens[begin], "1") == CTOOL_TRUE)) {
    *value_out = pp_token_equal_literal(&tokens[begin], "1");
    return CTOOL_OK;
  }
  pp_fail(context, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
          marker->location.line, marker->location.column,
          "CupidC conditional expressions are not implemented yet");
  return CTOOL_ERR_UNSUPPORTED;
}

static ctool_status_t pp_process_tokens(pp_context_t *context,
                                        const pp_token_t *tokens,
                                        ctool_u32 token_count) {
  pp_conditional_t *conditional_stack = (pp_conditional_t *)0;
  ctool_u32 conditional_depth = 0u;
  ctool_bool active = CTOOL_TRUE;
  ctool_u32 index = 0u;
  ctool_status_t status;
  while (index < token_count) {
    if (pp_token_is_directive_marker(&tokens[index]) == CTOOL_FALSE) {
      if (active == CTOOL_TRUE) {
        status = pp_emit_expanded(context, &tokens[index]);
        if (status != CTOOL_OK) {
          return status;
        }
      }
      index++;
      continue;
    }
    {
      const pp_token_t *marker = &tokens[index];
      ctool_u32 name_index = index + 1u;
      ctool_u32 end = pp_directive_end(tokens, token_count, name_index);
      const pp_token_t *name;
      ctool_u32 arguments;
      ctool_bool condition;
      if (name_index == end) {
        index = end;
        continue;
      }
      name = &tokens[name_index];
      arguments = name_index + 1u;
      if (pp_token_is_identifier(name) == CTOOL_FALSE) {
        return pp_directive_error(context, marker,
                                  CTOOL_C_PP_DIAG_DIRECTIVE,
                                  "CupidC directive name is invalid");
      }
      if (pp_token_equal_literal(name, "ifdef") == CTOOL_TRUE ||
          pp_token_equal_literal(name, "ifndef") == CTOOL_TRUE ||
          pp_token_equal_literal(name, "if") == CTOOL_TRUE) {
        if (conditional_depth == PP_CONDITIONAL_DEPTH_LIMIT) {
          pp_fail(context, CTOOL_ERR_LIMIT, CTOOL_C_PP_DIAG_CONDITIONAL,
                  marker->location.line, marker->location.column,
                  "CupidC conditional nesting limit exceeded");
          return CTOOL_ERR_LIMIT;
        }
        if (active == CTOOL_FALSE) {
          condition = CTOOL_FALSE;
          status = CTOOL_OK;
        } else if (pp_token_equal_literal(name, "if") == CTOOL_TRUE) {
          status = pp_simple_if_expression(context, marker, tokens, arguments,
                                           end, &condition);
        } else {
          status = pp_condition_name(
              context, marker, tokens, arguments, end,
              pp_token_equal_literal(name, "ifndef"), &condition);
        }
        if (status != CTOOL_OK) {
          return status;
        }
        {
          pp_conditional_t *conditional;
          status = ctool_arena_alloc_zero(
              ctool_job_arena(context->job), 1u,
              (ctool_u32)sizeof(*conditional),
              (ctool_u32)sizeof(void *), (void **)&conditional);
          if (status != CTOOL_OK) {
            pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
                    marker->location.line, marker->location.column,
                    "CupidC conditional storage limit exceeded");
            return status;
          }
          conditional->parent_active = active;
          conditional->current_active =
            active == CTOOL_TRUE && condition == CTOOL_TRUE ? CTOOL_TRUE
                                                            : CTOOL_FALSE;
          conditional->branch_taken = condition;
          conditional->saw_else = CTOOL_FALSE;
          conditional->opening_line = marker->location.line;
          conditional->opening_column = marker->location.column;
          conditional->previous = conditional_stack;
          conditional_stack = conditional;
          active = conditional->current_active;
        }
        conditional_depth++;
      } else if (pp_token_equal_literal(name, "elif") == CTOOL_TRUE) {
        if (conditional_depth == 0u) {
          return pp_directive_error(context, marker,
                                    CTOOL_C_PP_DIAG_CONDITIONAL,
                                    "CupidC #elif has no matching #if");
        }
        if (conditional_stack->saw_else == CTOOL_TRUE) {
          return pp_directive_error(context, marker,
                                    CTOOL_C_PP_DIAG_CONDITIONAL,
                                    "CupidC #elif follows #else");
        }
        if (conditional_stack->parent_active == CTOOL_FALSE ||
            conditional_stack->branch_taken == CTOOL_TRUE) {
          condition = CTOOL_FALSE;
          status = CTOOL_OK;
        } else {
          status = pp_simple_if_expression(context, marker, tokens, arguments,
                                           end, &condition);
        }
        if (status != CTOOL_OK) {
          return status;
        }
        conditional_stack->current_active =
            conditional_stack->parent_active == CTOOL_TRUE &&
                    conditional_stack->branch_taken == CTOOL_FALSE &&
                    condition == CTOOL_TRUE
                ? CTOOL_TRUE
                : CTOOL_FALSE;
        if (condition == CTOOL_TRUE) {
          conditional_stack->branch_taken = CTOOL_TRUE;
        }
        active = conditional_stack->current_active;
      } else if (pp_token_equal_literal(name, "else") == CTOOL_TRUE) {
        if (conditional_depth == 0u || arguments != end) {
          return pp_directive_error(context, marker,
                                    CTOOL_C_PP_DIAG_CONDITIONAL,
                                    "CupidC #else is unmatched or malformed");
        }
        if (conditional_stack->saw_else == CTOOL_TRUE) {
          return pp_directive_error(context, marker,
                                    CTOOL_C_PP_DIAG_CONDITIONAL,
                                    "CupidC conditional has multiple #else branches");
        }
        conditional_stack->saw_else = CTOOL_TRUE;
        conditional_stack->current_active =
            conditional_stack->parent_active == CTOOL_TRUE &&
                    conditional_stack->branch_taken == CTOOL_FALSE
                ? CTOOL_TRUE
                : CTOOL_FALSE;
        conditional_stack->branch_taken = CTOOL_TRUE;
        active = conditional_stack->current_active;
      } else if (pp_token_equal_literal(name, "endif") == CTOOL_TRUE) {
        if (conditional_depth == 0u || arguments != end) {
          return pp_directive_error(context, marker,
                                    CTOOL_C_PP_DIAG_CONDITIONAL,
                                    "CupidC #endif is unmatched or malformed");
        }
        conditional_depth--;
        conditional_stack = conditional_stack->previous;
        active = conditional_stack == (pp_conditional_t *)0
                     ? CTOOL_TRUE
                     : conditional_stack->current_active;
      } else if (active == CTOOL_FALSE) {
        /* Non-conditional directives in skipped groups have no effect. */
      } else if (pp_token_equal_literal(name, "define") == CTOOL_TRUE) {
        status = pp_handle_define(context, marker, tokens, arguments, end);
        if (status != CTOOL_OK) {
          return status;
        }
      } else if (pp_token_equal_literal(name, "undef") == CTOOL_TRUE) {
        status = pp_handle_undef(context, marker, tokens, arguments, end);
        if (status != CTOOL_OK) {
          return status;
        }
      } else if (pp_token_equal_literal(name, "include") == CTOOL_TRUE) {
        status = pp_handle_include(context, marker, tokens, arguments, end);
        if (status != CTOOL_OK) {
          return status;
        }
      } else if (pp_token_equal_literal(name, "error") == CTOOL_TRUE) {
        return pp_directive_error(context, marker,
                                  CTOOL_C_PP_DIAG_ERROR_DIRECTIVE,
                                  "CupidC source reached an active #error");
      } else {
        pp_fail(context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PP_DIAG_DIRECTIVE,
                marker->location.line, marker->location.column,
                "CupidC preprocessing directive is not implemented yet");
        return CTOOL_ERR_UNSUPPORTED;
      }
      index = end;
    }
  }
  if (conditional_depth != 0u) {
    pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_CONDITIONAL,
            conditional_stack->opening_line,
            conditional_stack->opening_column,
            "CupidC source ends inside a conditional group");
    return CTOOL_ERR_INPUT;
  }
  return CTOOL_OK;
}

static ctool_status_t pp_process_source(pp_context_t *context,
                                        const ctool_source_t *source) {
  const ctool_source_t *previous_source = context->source;
  ctool_string_t previous_path = context->path;
  pp_source_cache_t *entry = pp_source_cache_find(context, &source->path);
  pp_input_t input;
  ctool_status_t status;
  if (entry == (pp_source_cache_t *)0) {
    status = pp_source_cache_add(context, source, &entry);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  context->source = &entry->source;
  context->path = entry->source.path.text;
  status = CTOOL_OK;
  if (entry->tokenized == CTOOL_FALSE) {
    input.text = (char *)0;
    input.size = 0u;
    input.splice_offsets = (ctool_u32 *)0;
    input.splice_count = 0u;
    status = pp_normalize(context, &input);
    if (status == CTOOL_OK) {
      status = pp_tokenize(context, &input, &entry->tokens,
                           &entry->token_count);
    }
    if (status == CTOOL_OK) {
      entry->tokenized = CTOOL_TRUE;
    }
  }
  if (status == CTOOL_OK) {
    status = pp_process_tokens(context, entry->tokens, entry->token_count);
  }
  context->source = previous_source;
  context->path = previous_path;
  return status;
}

static ctool_status_t pp_install_macro_actions(pp_context_t *context) {
  ctool_u32 index;
  for (index = 0u; index < context->request->macro_action_count; index++) {
    const ctool_c_pp_macro_action_t *action =
        &context->request->macro_actions[index];
    ctool_status_t status;
    if (action->kind == CTOOL_C_PP_MACRO_UNDEF) {
      status = pp_undefine_macro(context, action->name, 0u, 0u);
    } else {
      ctool_source_t replacement_source;
      pp_input_t input;
      pp_token_t *replacement;
      ctool_u32 replacement_count;
      const ctool_source_t *previous_source = context->source;
      ctool_string_t previous_path = context->path;
      replacement_source.path.text = context->path;
      replacement_source.contents = ctool_bytes(
          action->replacement.data, action->replacement.size);
      context->source = &replacement_source;
      input.text = (char *)0;
      input.size = 0u;
      input.splice_offsets = (ctool_u32 *)0;
      input.splice_count = 0u;
      status = pp_normalize(context, &input);
      if (status == CTOOL_OK) {
        status = pp_tokenize(context, &input, &replacement,
                             &replacement_count);
      }
      context->source = previous_source;
      context->path = previous_path;
      if (status == CTOOL_OK) {
        status = pp_define_object_macro(context, action->name, replacement,
                                        replacement_count, 0u, 0u);
      }
    }
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t pp_process_forced_includes(pp_context_t *context) {
  ctool_u32 index;
  for (index = 0u; index < context->request->forced_include_count; index++) {
    ctool_source_t source;
    pp_source_cache_t *cached = pp_source_cache_find(
        context, &context->request->forced_includes[index]);
    ctool_status_t status;
    if (cached != (pp_source_cache_t *)0) {
      source = cached->source;
      status = CTOOL_OK;
    } else {
      status = ctool_job_load_source(
          context->job, &context->request->forced_includes[index], &source);
    }
    if (status != CTOOL_OK) {
      ctool_string_t previous_path = context->path;
      ctool_u32 code = status == CTOOL_ERR_NOT_FOUND
                           ? CTOOL_C_PP_DIAG_INCLUDE_NOT_FOUND
                           : pp_include_diagnostic_code(status);
      context->path = context->request->forced_includes[index].text;
      pp_fail(context, status, code, 0u, 0u,
              status == CTOOL_ERR_NOT_FOUND
                  ? "CupidC forced include file was not found"
                  : "CupidC forced include file could not be loaded");
      context->path = previous_path;
      return status;
    }
    if (context->include_depth == PP_INCLUDE_DEPTH_LIMIT) {
      pp_fail(context, CTOOL_ERR_LIMIT, CTOOL_C_PP_DIAG_INCLUDE_DEPTH, 0u,
              0u, "CupidC forced include depth limit exceeded");
      return CTOOL_ERR_LIMIT;
    }
    context->include_depth++;
    status = pp_process_source(context, &source);
    context->include_depth--;
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

ctool_status_t ctool_c_preprocess(ctool_job_t *job,
                                  const ctool_source_t *primary,
                                  const ctool_c_pp_request_t *request,
                                  ctool_c_pp_result_t *result_out) {
  pp_context_t context;
  ctool_source_t primary_source;
  ctool_arena_t *arena;
  ctool_arena_mark_t mark;
  ctool_status_t status;
  ctool_status_t published;
  ctool_status_t rewound;
  ctool_string_t path_copy;
  pp_source_cache_t *primary_cache;
  const ctool_limits_t *limits;

  if (result_out != (ctool_c_pp_result_t *)0) {
    pp_zero_result(result_out);
  }
  if (job == (ctool_job_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  limits = ctool_job_limits(job);
  status = pp_validate_request(primary, request, result_out, limits);
  if (status != CTOOL_OK) {
    ctool_diagnostic_t diagnostic;
    diagnostic.severity = CTOOL_DIAG_ERROR;
    diagnostic.code = status == CTOOL_ERR_LIMIT ? CTOOL_C_PP_DIAG_LIMIT
                                                 : CTOOL_C_PP_DIAG_INVALID_REQUEST;
    diagnostic.path = primary != (const ctool_source_t *)0 &&
                              primary->path.text.size <= limits->path_bytes &&
                              ctool_path_is_canonical(&primary->path) ==
                                  CTOOL_TRUE
                          ? primary->path.text
                          : (limits->path_bytes >= 7u
                                 ? ctool_string("/cupidc")
                                 : ctool_string("/"));
    diagnostic.line = 0u;
    diagnostic.column = 0u;
    diagnostic.message = ctool_string(status == CTOOL_ERR_LIMIT
                                          ? "CupidC preprocessing request exceeds job limits"
                                          : "invalid CupidC preprocessing request");
    (void)ctool_job_emit(job, &diagnostic);
    return status;
  }
  arena = ctool_job_arena(job);
  mark = ctool_arena_mark(arena);
  context.job = job;
  context.source = primary;
  context.request = request;
  context.path = primary->path.text;
  context.failure_path = primary->path.text;
  context.macros = (pp_macro_t *)0;
  context.sources = (pp_source_cache_t *)0;
  context.output_head = (pp_output_chunk_t *)0;
  context.output_tail = (pp_output_chunk_t *)0;
  context.output_count = 0u;
  context.include_depth = 0u;
  context.failure_status = CTOOL_OK;
  context.failure_code = 0u;
  context.failure_line = 0u;
  context.failure_column = 0u;
  context.failure_message = "CupidC preprocessing failed";
  primary_source = *primary;

  status = ctool_arena_copy_string(arena, primary->path.text, &path_copy);
  if (status != CTOOL_OK) {
    pp_fail(&context, status, CTOOL_C_PP_DIAG_LIMIT, 0u, 0u,
            "CupidC preprocessing path storage limit exceeded");
  }
  if (status == CTOOL_OK) {
    context.path = path_copy;
    primary_source.path.text = path_copy;
    context.source = &primary_source;
  }
  if (status == CTOOL_OK) {
    status = pp_source_cache_add(&context, &primary_source, &primary_cache);
  }
  if (status == CTOOL_OK) {
    status = pp_install_macro_actions(&context);
  }
  if (status == CTOOL_OK) {
    status = pp_process_forced_includes(&context);
  }
  if (status == CTOOL_OK) {
    status = pp_process_source(&context, &primary_source);
  }
  if (status == CTOOL_OK) {
    status = pp_publish_output(&context, result_out);
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
