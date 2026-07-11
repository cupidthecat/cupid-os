#include "cupidc_pp.h"

#define PP_INCLUDE_DEPTH_LIMIT 64u
#define PP_CONDITIONAL_DEPTH_LIMIT 64u
#define PP_CONDITIONAL_EXPRESSION_DEPTH_LIMIT 256u
#define PP_MACRO_EXPANSION_DEPTH_LIMIT 256u
#define PP_MACRO_PARAMETER_LIMIT 256u
#define PP_PRAGMA_PACK_DEPTH_LIMIT 256u
#define PP_NOT_A_PARAMETER 0xffffffffu
#define PP_OUTPUT_CHUNK_TOKENS 128u
#define PP_TOKEN_LIMIT 1000000u
#define PP_IF_SIGN_BIT 0x8000000000000000ull
#define PP_IF_SIGNED_MAX 0x7fffffffffffffffull
#define PP_IF_U64_MAX 0xffffffffffffffffull
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
typedef struct pp_hide pp_hide_t;
typedef struct pp_expand_node pp_expand_node_t;
typedef struct pp_pack_frame pp_pack_frame_t;

typedef enum {
  PP_EXPAND_TOKEN = 1,
  PP_EXPAND_PLACEMARKER,
  PP_EXPAND_PASTE
} pp_expand_kind_t;

struct pp_macro {
  ctool_string_t name;
  ctool_bool defined;
  ctool_bool function_like;
  ctool_bool variadic;
  const ctool_string_t *parameters;
  ctool_u32 parameter_count;
  const pp_token_t *replacement;
  const ctool_u32 *replacement_parameters;
  const ctool_bool *parameter_expanded;
  ctool_u32 replacement_count;
  pp_macro_t *next;
};

struct pp_hide {
  pp_macro_t *macro;
  const pp_hide_t *next;
};

struct pp_expand_node {
  pp_expand_kind_t kind;
  pp_token_t token;
  const pp_hide_t *hide;
  ctool_bool condition_defined;
  pp_expand_node_t *next;
};

typedef struct {
  pp_expand_node_t *head;
  pp_expand_node_t *tail;
  ctool_u32 count;
  ctool_bool trailing_space;
} pp_expand_list_t;

typedef struct {
  pp_expand_node_t *raw;
  ctool_u32 raw_count;
  pp_expand_list_t expanded;
  ctool_bool omitted;
} pp_macro_argument_t;

struct pp_source_cache {
  ctool_source_t source;
  pp_token_t *tokens;
  ctool_u32 token_count;
  ctool_bool tokenized;
  ctool_bool entered;
  ctool_bool pragma_once;
  pp_source_cache_t *next;
};

struct pp_pack_frame {
  ctool_u32 saved_alignment;
  ctool_string_t name;
  ctool_bool named;
  pp_pack_frame_t *next;
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
  pp_source_cache_t *source_entry;
  pp_pack_frame_t *pack_stack;
  pp_pack_frame_t *free_pack_frames;
  pp_output_chunk_t *output_head;
  pp_output_chunk_t *output_tail;
  ctool_u32 output_count;
  ctool_u32 include_depth;
  ctool_u32 expansion_depth;
  ctool_u32 expansion_node_count;
  ctool_u32 pack_depth;
  ctool_u32 pack_alignment;
  ctool_bool condition_expression;
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

static ctool_bool pp_translation_date_is_valid(ctool_string_t value) {
  static const char months[] =
      "JanFebMarAprMayJunJulAugSepOctNovDec";
  static const ctool_u32 month_days[] = {
      31u, 28u, 31u, 30u, 31u, 30u,
      31u, 31u, 30u, 31u, 30u, 31u};
  ctool_u32 month;
  ctool_u32 day;
  ctool_u32 year = 0u;
  ctool_u32 maximum_day;
  ctool_u32 index;
  if (value.size == 0u) {
    return CTOOL_TRUE;
  }
  if (value.data == (const char *)0 || value.size != 11u ||
      value.data[3] != ' ' || value.data[6] != ' ' ||
      (value.data[4] != ' ' && pp_is_digit(value.data[4]) == CTOOL_FALSE) ||
      pp_is_digit(value.data[5]) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  for (month = 0u; month < 12u; month++) {
    if (value.data[0] == months[month * 3u] &&
        value.data[1] == months[month * 3u + 1u] &&
        value.data[2] == months[month * 3u + 2u]) {
      break;
    }
  }
  if (month == 12u) {
    return CTOOL_FALSE;
  }
  day = (value.data[4] == ' ' ? 0u
                                : (ctool_u32)(value.data[4] - '0') * 10u) +
        (ctool_u32)(value.data[5] - '0');
  if (day == 0u ||
      (day < 10u && value.data[4] != ' ') ||
      (day >= 10u && value.data[4] == ' ')) {
    return CTOOL_FALSE;
  }
  for (index = 7u; index < 11u; index++) {
    if (pp_is_digit(value.data[index]) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
    year = year * 10u + (ctool_u32)(value.data[index] - '0');
  }
  if (year == 0u) {
    return CTOOL_FALSE;
  }
  maximum_day = month_days[month];
  if (month == 1u &&
      (year % 400u == 0u ||
       (year % 4u == 0u && year % 100u != 0u))) {
    maximum_day = 29u;
  }
  return day <= maximum_day ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool pp_translation_time_is_valid(ctool_string_t value) {
  ctool_u32 hour;
  ctool_u32 minute;
  ctool_u32 second;
  ctool_u32 index;
  if (value.size == 0u) {
    return CTOOL_TRUE;
  }
  if (value.data == (const char *)0 || value.size != 8u ||
      value.data[2] != ':' || value.data[5] != ':') {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < 8u; index++) {
    if (index != 2u && index != 5u &&
        pp_is_digit(value.data[index]) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  hour = (ctool_u32)(value.data[0] - '0') * 10u +
         (ctool_u32)(value.data[1] - '0');
  minute = (ctool_u32)(value.data[3] - '0') * 10u +
           (ctool_u32)(value.data[4] - '0');
  second = (ctool_u32)(value.data[6] - '0') * 10u +
           (ctool_u32)(value.data[7] - '0');
  return hour <= 23u && minute <= 59u && second <= 60u ? CTOOL_TRUE
                                                       : CTOOL_FALSE;
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
      pp_translation_date_is_valid(request->translation_date) ==
          CTOOL_FALSE ||
      pp_translation_time_is_valid(request->translation_time) ==
          CTOOL_FALSE ||
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

static ctool_bool pp_token_is_stringify_operator(const pp_token_t *token) {
  return pp_token_equal_literal(token, "#") == CTOOL_TRUE ||
                 pp_token_equal_literal(token, "%:") == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool pp_token_is_paste_operator(const pp_token_t *token) {
  return pp_token_equal_literal(token, "##") == CTOOL_TRUE ||
                 pp_token_equal_literal(token, "%:%:") == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool pp_name_is_predefined(ctool_string_t name) {
  return pp_string_equal_literal(name, "__FILE__") == CTOOL_TRUE ||
                 pp_string_equal_literal(name, "__LINE__") == CTOOL_TRUE ||
                 pp_string_equal_literal(name, "__DATE__") == CTOOL_TRUE ||
                 pp_string_equal_literal(name, "__TIME__") == CTOOL_TRUE ||
                 pp_string_equal_literal(name, "__STDC__") == CTOOL_TRUE ||
                 pp_string_equal_literal(name, "__STDC_HOSTED__") ==
                     CTOOL_TRUE ||
                 pp_string_equal_literal(name, "__STDC_VERSION__") ==
                     CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool pp_name_is_variadic_identifier(ctool_string_t name) {
  return pp_string_equal_literal(name, "__VA_ARGS__");
}

static ctool_status_t pp_reject_reserved_variadic_identifier(
    pp_context_t *context, const pp_token_t *token) {
  if (token->kind != CTOOL_C_PP_TOKEN_IDENTIFIER ||
      pp_name_is_variadic_identifier(token->spelling) == CTOOL_FALSE) {
    return CTOOL_OK;
  }
  pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_EXPANSION,
          token->location.line, token->location.column,
          "CupidC __VA_ARGS__ is reserved for variadic macro replacement lists");
  return CTOOL_ERR_INPUT;
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

static ctool_bool pp_macro_is_defined(pp_context_t *context,
                                      ctool_string_t name) {
  return pp_name_is_predefined(name) == CTOOL_TRUE ||
                 pp_macro_find(context, name) != (pp_macro_t *)0
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool pp_parameter_index(const ctool_string_t *parameters,
                                     ctool_u32 parameter_count,
                                     ctool_string_t name,
                                     ctool_u32 *index_out) {
  ctool_u32 index;
  for (index = 0u; index < parameter_count; index++) {
    if (pp_string_equal(parameters[index], name) == CTOOL_TRUE) {
      *index_out = index;
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_bool pp_replacement_parameter_index(
    const ctool_string_t *parameters, ctool_u32 parameter_count,
    ctool_bool variadic, ctool_string_t name, ctool_u32 *index_out) {
  if (pp_parameter_index(parameters, parameter_count, name, index_out) ==
      CTOOL_TRUE) {
    return CTOOL_TRUE;
  }
  if (variadic == CTOOL_TRUE &&
      pp_name_is_variadic_identifier(name) == CTOOL_TRUE) {
    *index_out = parameter_count;
    return CTOOL_TRUE;
  }
  return CTOOL_FALSE;
}

static ctool_bool pp_macro_replacement_equal(
    const pp_macro_t *macro, ctool_bool function_like,
    const ctool_string_t *parameters, ctool_u32 parameter_count,
    ctool_bool variadic, const pp_token_t *replacement,
    ctool_u32 replacement_count) {
  ctool_u32 index;
  if (macro->function_like != function_like || macro->variadic != variadic ||
      macro->parameter_count != parameter_count ||
      macro->replacement_count != replacement_count) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < parameter_count; index++) {
    if (pp_string_equal(macro->parameters[index], parameters[index]) ==
        CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < replacement_count; index++) {
    if (macro->replacement[index].kind != replacement[index].kind ||
        (index != 0u &&
         macro->replacement[index].leading_space !=
             replacement[index].leading_space) ||
        pp_string_equal(macro->replacement[index].spelling,
                        replacement[index].spelling) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t pp_define_macro(
    pp_context_t *context, ctool_string_t name, ctool_bool function_like,
    const ctool_string_t *parameters, ctool_u32 parameter_count,
    ctool_bool variadic, const pp_token_t *replacement,
    ctool_u32 replacement_count, ctool_u32 line, ctool_u32 column) {
  pp_macro_t *existing;
  pp_macro_t *macro;
  ctool_string_t *parameter_copy = (ctool_string_t *)0;
  ctool_u32 *replacement_parameters = (ctool_u32 *)0;
  ctool_bool *parameter_expanded = (ctool_bool *)0;
  ctool_u32 total_parameter_count =
      parameter_count + (variadic == CTOOL_TRUE ? 1u : 0u);
  ctool_u32 index;
  ctool_status_t status;
  if (pp_name_is_predefined(name) == CTOOL_TRUE) {
    pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_DEFINITION,
            line, column, "CupidC predefined macros cannot be redefined");
    return CTOOL_ERR_INPUT;
  }
  if (pp_string_equal_literal(name, "defined") == CTOOL_TRUE) {
    pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_DEFINITION,
            line, column,
            "CupidC defined operator cannot be used as a macro name");
    return CTOOL_ERR_INPUT;
  }
  if (pp_name_is_variadic_identifier(name) == CTOOL_TRUE) {
    pp_fail(context, CTOOL_ERR_INPUT,
            CTOOL_C_PP_DIAG_MACRO_DEFINITION, line, column,
            "CupidC __VA_ARGS__ is reserved for variadic macro replacement lists");
    return CTOOL_ERR_INPUT;
  }
  if (variadic == CTOOL_FALSE) {
    for (index = 0u; index < replacement_count; index++) {
      if (replacement[index].kind == CTOOL_C_PP_TOKEN_IDENTIFIER &&
          pp_name_is_variadic_identifier(replacement[index].spelling) ==
              CTOOL_TRUE) {
        pp_fail(
            context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_DEFINITION,
            replacement[index].location.line,
            replacement[index].location.column,
            "CupidC __VA_ARGS__ is reserved for variadic macro replacement lists");
        return CTOOL_ERR_INPUT;
      }
    }
  }
  for (index = 0u; index < replacement_count; index++) {
    if (pp_token_is_paste_operator(&replacement[index]) == CTOOL_TRUE &&
        (index == 0u || index + 1u == replacement_count)) {
      pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_PASTE,
              replacement[index].location.line,
              replacement[index].location.column,
              "CupidC macro paste cannot be first or last in a replacement");
      return CTOOL_ERR_INPUT;
    }
    if (function_like == CTOOL_TRUE &&
        pp_token_is_stringify_operator(&replacement[index]) == CTOOL_TRUE) {
      ctool_u32 parameter = 0u;
      if (index + 1u == replacement_count ||
          replacement[index + 1u].kind != CTOOL_C_PP_TOKEN_IDENTIFIER ||
          pp_replacement_parameter_index(
              parameters, parameter_count, variadic,
              replacement[index + 1u].spelling, &parameter) == CTOOL_FALSE) {
        pp_fail(context, CTOOL_ERR_INPUT,
                CTOOL_C_PP_DIAG_MACRO_DEFINITION,
                replacement[index].location.line,
                replacement[index].location.column,
                "CupidC macro stringification requires a parameter");
        return CTOOL_ERR_INPUT;
      }
    }
  }
  existing = pp_macro_find(context, name);
  if (existing != (pp_macro_t *)0) {
    if (pp_macro_replacement_equal(existing, function_like, parameters,
                                   parameter_count, variadic, replacement,
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
  if (parameter_count != 0u) {
    status = ctool_arena_alloc_zero(
        ctool_job_arena(context->job), parameter_count,
        (ctool_u32)sizeof(*parameter_copy), (ctool_u32)sizeof(void *),
        (void **)&parameter_copy);
    if (status != CTOOL_OK) {
      pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, line, column,
              "CupidC macro parameter storage limit exceeded");
      return status;
    }
    for (index = 0u; index < parameter_count; index++) {
      status = ctool_arena_copy_string(ctool_job_arena(context->job),
                                       parameters[index],
                                       &parameter_copy[index]);
      if (status != CTOOL_OK) {
        pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, line, column,
                "CupidC macro parameter name storage limit exceeded");
        return status;
      }
    }
  }
  if (total_parameter_count != 0u) {
    status = ctool_arena_alloc_zero(
        ctool_job_arena(context->job), total_parameter_count,
        (ctool_u32)sizeof(*parameter_expanded),
        (ctool_u32)sizeof(*parameter_expanded),
        (void **)&parameter_expanded);
    if (status != CTOOL_OK) {
      pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, line, column,
              "CupidC macro parameter-use storage limit exceeded");
      return status;
    }
  }
  if (function_like == CTOOL_TRUE && replacement_count != 0u) {
    status = ctool_arena_alloc_zero(
        ctool_job_arena(context->job), replacement_count,
        (ctool_u32)sizeof(*replacement_parameters),
        (ctool_u32)sizeof(*replacement_parameters),
        (void **)&replacement_parameters);
    if (status != CTOOL_OK) {
      pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT, line, column,
              "CupidC macro substitution-plan storage limit exceeded");
      return status;
    }
    for (index = 0u; index < replacement_count; index++) {
      ctool_u32 parameter = PP_NOT_A_PARAMETER;
      if (replacement[index].kind == CTOOL_C_PP_TOKEN_IDENTIFIER &&
          pp_replacement_parameter_index(
              parameter_copy, parameter_count, variadic,
              replacement[index].spelling, &parameter) == CTOOL_TRUE) {
        ctool_bool stringified =
            index != 0u && function_like == CTOOL_TRUE &&
                    pp_token_is_stringify_operator(&replacement[index - 1u]) ==
                        CTOOL_TRUE
                ? CTOOL_TRUE
                : CTOOL_FALSE;
        ctool_bool pasted =
            (index != 0u &&
             pp_token_is_paste_operator(&replacement[index - 1u]) ==
                 CTOOL_TRUE) ||
                    (index + 1u < replacement_count &&
                     pp_token_is_paste_operator(&replacement[index + 1u]) ==
                         CTOOL_TRUE)
                ? CTOOL_TRUE
                : CTOOL_FALSE;
        if (stringified == CTOOL_FALSE && pasted == CTOOL_FALSE) {
          parameter_expanded[parameter] = CTOOL_TRUE;
        }
      }
      replacement_parameters[index] = parameter;
    }
  }
  macro->defined = CTOOL_TRUE;
  macro->function_like = function_like;
  macro->variadic = variadic;
  macro->parameters = parameter_copy;
  macro->parameter_count = parameter_count;
  macro->replacement = replacement;
  macro->replacement_parameters = replacement_parameters;
  macro->parameter_expanded = parameter_expanded;
  macro->replacement_count = replacement_count;
  macro->next = context->macros;
  context->macros = macro;
  return CTOOL_OK;
}

static ctool_status_t pp_define_object_macro(
    pp_context_t *context, ctool_string_t name,
    const pp_token_t *replacement, ctool_u32 replacement_count,
    ctool_u32 line, ctool_u32 column) {
  return pp_define_macro(context, name, CTOOL_FALSE,
                         (const ctool_string_t *)0, 0u, CTOOL_FALSE,
                         replacement, replacement_count, line, column);
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
  if (pp_string_equal_literal(name, "defined") == CTOOL_TRUE) {
    pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_DEFINITION,
            line, column,
            "CupidC defined operator cannot be used as a macro name");
    return CTOOL_ERR_INPUT;
  }
  if (pp_name_is_variadic_identifier(name) == CTOOL_TRUE) {
    pp_fail(context, CTOOL_ERR_INPUT,
            CTOOL_C_PP_DIAG_MACRO_DEFINITION, line, column,
            "CupidC __VA_ARGS__ is reserved for variadic macro replacement lists");
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

static ctool_bool pp_hide_contains(const pp_hide_t *hide,
                                   const pp_macro_t *macro) {
  while (hide != (const pp_hide_t *)0) {
    if (hide->macro == macro) {
      return CTOOL_TRUE;
    }
    hide = hide->next;
  }
  return CTOOL_FALSE;
}

static ctool_status_t pp_hide_add(pp_context_t *context,
                                  const pp_hide_t *hide,
                                  pp_macro_t *macro,
                                  const pp_token_t *location,
                                  const pp_hide_t **result_out) {
  pp_hide_t *entry;
  ctool_status_t status;
  if (pp_hide_contains(hide, macro) == CTOOL_TRUE) {
    *result_out = hide;
    return CTOOL_OK;
  }
  status = ctool_arena_alloc_zero(
      ctool_job_arena(context->job), 1u, (ctool_u32)sizeof(*entry),
      (ctool_u32)sizeof(void *), (void **)&entry);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
            location->location.line, location->location.column,
            "CupidC macro hide-set storage limit exceeded");
    return status;
  }
  entry->macro = macro;
  entry->next = hide;
  *result_out = entry;
  return CTOOL_OK;
}

static ctool_status_t pp_hide_union(pp_context_t *context,
                                    const pp_hide_t *left,
                                    const pp_hide_t *right,
                                    const pp_token_t *location,
                                    const pp_hide_t **result_out) {
  ctool_status_t status;
  *result_out = left;
  while (right != (const pp_hide_t *)0) {
    status = pp_hide_add(context, *result_out, right->macro, location,
                         result_out);
    if (status != CTOOL_OK) {
      return status;
    }
    right = right->next;
  }
  return CTOOL_OK;
}

static ctool_status_t pp_hide_intersection(
    pp_context_t *context, const pp_hide_t *left, const pp_hide_t *right,
    const pp_token_t *location, const pp_hide_t **result_out) {
  ctool_status_t status;
  *result_out = (const pp_hide_t *)0;
  while (left != (const pp_hide_t *)0) {
    if (pp_hide_contains(right, left->macro) == CTOOL_TRUE) {
      status = pp_hide_add(context, *result_out, left->macro, location,
                           result_out);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    left = left->next;
  }
  return CTOOL_OK;
}

static void pp_expand_list_zero(pp_expand_list_t *list) {
  list->head = (pp_expand_node_t *)0;
  list->tail = (pp_expand_node_t *)0;
  list->count = 0u;
  list->trailing_space = CTOOL_FALSE;
}

static ctool_status_t pp_expand_node_new(pp_context_t *context,
                                         const pp_token_t *token,
                                         const pp_hide_t *hide,
                                         pp_expand_node_t **node_out) {
  pp_expand_node_t *node;
  ctool_status_t status;
  if (context->expansion_node_count == PP_TOKEN_LIMIT) {
    pp_fail(context, CTOOL_ERR_LIMIT, CTOOL_C_PP_DIAG_MACRO_EXPANSION,
            token->location.line, token->location.column,
            "CupidC macro expansion token limit exceeded");
    return CTOOL_ERR_LIMIT;
  }
  status = ctool_arena_alloc_zero(
      ctool_job_arena(context->job), 1u, (ctool_u32)sizeof(*node),
      (ctool_u32)sizeof(void *), (void **)&node);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
            token->location.line, token->location.column,
            "CupidC macro expansion storage limit exceeded");
    return status;
  }
  node->token = *token;
  node->kind = PP_EXPAND_TOKEN;
  node->hide = hide;
  context->expansion_node_count++;
  *node_out = node;
  return CTOOL_OK;
}

static void pp_expand_list_append_node(pp_expand_list_t *list,
                                       pp_expand_node_t *node) {
  node->next = (pp_expand_node_t *)0;
  if (list->tail == (pp_expand_node_t *)0) {
    list->head = node;
  } else {
    list->tail->next = node;
  }
  list->tail = node;
  list->count++;
}

static ctool_status_t pp_expand_list_append_copy(
    pp_context_t *context, pp_expand_list_t *list,
    const pp_token_t *token, const pp_hide_t *hide) {
  pp_expand_node_t *node;
  ctool_status_t status = pp_expand_node_new(context, token, hide, &node);
  if (status == CTOOL_OK) {
    pp_expand_list_append_node(list, node);
  }
  return status;
}

static ctool_status_t pp_expand_list_append_special(
    pp_context_t *context, pp_expand_list_t *list, pp_expand_kind_t kind,
    const pp_token_t *token, const pp_hide_t *hide) {
  pp_expand_node_t *node;
  ctool_status_t status = pp_expand_node_new(context, token, hide, &node);
  if (status == CTOOL_OK) {
    node->kind = kind;
    pp_expand_list_append_node(list, node);
  }
  return status;
}

static ctool_status_t pp_expand_list_copy_range(
    pp_context_t *context, pp_expand_node_t *node, ctool_u32 count,
    pp_expand_list_t *copy_out) {
  ctool_u32 index;
  ctool_status_t status;
  pp_expand_list_zero(copy_out);
  for (index = 0u; index < count; index++) {
    if (node == (pp_expand_node_t *)0) {
      pp_fail(context, CTOOL_ERR_INTERNAL,
              CTOOL_C_PP_DIAG_MACRO_EXPANSION, 0u, 0u,
              "CupidC macro argument range became inconsistent");
      return CTOOL_ERR_INTERNAL;
    }
    status = pp_expand_list_append_copy(context, copy_out, &node->token,
                                        node->hide);
    if (status != CTOOL_OK) {
      return status;
    }
    copy_out->tail->condition_defined = node->condition_defined;
    node = node->next;
  }
  return CTOOL_OK;
}

static pp_expand_node_t *pp_expand_list_pop(pp_expand_list_t *list) {
  pp_expand_node_t *node = list->head;
  if (node == (pp_expand_node_t *)0) {
    return node;
  }
  list->head = node->next;
  if (list->head == (pp_expand_node_t *)0) {
    list->tail = (pp_expand_node_t *)0;
  }
  node->next = (pp_expand_node_t *)0;
  list->count--;
  return node;
}

static void pp_expand_list_prepend(pp_expand_list_t *list,
                                   pp_expand_list_t *prefix) {
  if (prefix->head == (pp_expand_node_t *)0) {
    return;
  }
  prefix->tail->next = list->head;
  if (list->tail == (pp_expand_node_t *)0) {
    list->tail = prefix->tail;
  }
  list->head = prefix->head;
  list->count += prefix->count;
  pp_expand_list_zero(prefix);
}

static ctool_status_t pp_expand_work(pp_context_t *context,
                                     pp_expand_list_t *work,
                                     pp_expand_list_t *private_output);

static ctool_status_t pp_collect_macro_arguments(
    pp_context_t *context, pp_macro_t *macro, pp_expand_node_t *name,
    pp_expand_node_t **close_out, pp_macro_argument_t **arguments_out,
    ctool_u32 *argument_count_out) {
  pp_expand_node_t *open = name->next;
  pp_expand_node_t *cursor;
  pp_expand_node_t *argument_start;
  pp_expand_node_t *close = (pp_expand_node_t *)0;
  pp_macro_argument_t *arguments = (pp_macro_argument_t *)0;
  ctool_u32 depth = 0u;
  ctool_u32 separators = 0u;
  ctool_u32 syntactic_argument_count;
  ctool_u32 argument_count =
      macro->parameter_count +
      (macro->variadic == CTOOL_TRUE ? 1u : 0u);
  ctool_u32 argument_index = 0u;
  ctool_u32 token_count = 0u;
  ctool_status_t status;

  if (open == (pp_expand_node_t *)0 ||
      pp_token_equal_literal(&open->token, "(") == CTOOL_FALSE) {
    pp_fail(context, CTOOL_ERR_INTERNAL, CTOOL_C_PP_DIAG_MACRO_ARGUMENTS,
            name->token.location.line, name->token.location.column,
            "CupidC function macro opening delimiter is missing");
    return CTOOL_ERR_INTERNAL;
  }
  cursor = open->next;
  while (cursor != (pp_expand_node_t *)0) {
    if (pp_token_equal_literal(&cursor->token, "(") == CTOOL_TRUE) {
      depth++;
    } else if (pp_token_equal_literal(&cursor->token, ")") == CTOOL_TRUE) {
      if (depth == 0u) {
        close = cursor;
        break;
      }
      depth--;
    } else if (depth == 0u &&
               pp_token_equal_literal(&cursor->token, ",") == CTOOL_TRUE) {
      separators++;
    }
    cursor = cursor->next;
  }
  if (close == (pp_expand_node_t *)0) {
    pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_ARGUMENTS,
            name->token.location.line, name->token.location.column,
            "CupidC function macro invocation is unterminated");
    return CTOOL_ERR_INPUT;
  }
  if (open->next == close && macro->parameter_count == 0u &&
      (macro->variadic == CTOOL_FALSE ||
       context->request->gnu_extensions == CTOOL_TRUE)) {
    syntactic_argument_count = 0u;
  } else {
    syntactic_argument_count = separators + 1u;
  }
  if ((macro->variadic == CTOOL_FALSE &&
       syntactic_argument_count != macro->parameter_count) ||
      (macro->variadic == CTOOL_TRUE &&
       syntactic_argument_count < macro->parameter_count) ||
      (macro->variadic == CTOOL_TRUE &&
       syntactic_argument_count == macro->parameter_count &&
       context->request->gnu_extensions == CTOOL_FALSE)) {
    pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_ARGUMENTS,
            name->token.location.line, name->token.location.column,
            "CupidC function macro argument count differs");
    return CTOOL_ERR_INPUT;
  }
  if (argument_count != 0u) {
    status = ctool_arena_alloc_zero(
        ctool_job_arena(context->job), argument_count,
        (ctool_u32)sizeof(*arguments), (ctool_u32)sizeof(void *),
        (void **)&arguments);
    if (status != CTOOL_OK) {
      pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
              name->token.location.line, name->token.location.column,
              "CupidC macro argument storage limit exceeded");
      return status;
    }
    if (arguments == (pp_macro_argument_t *)0) {
      pp_fail(context, CTOOL_ERR_INTERNAL,
              CTOOL_C_PP_DIAG_MACRO_EXPANSION,
              name->token.location.line, name->token.location.column,
              "CupidC macro argument storage was not returned");
      return CTOOL_ERR_INTERNAL;
    }
  } else {
    *close_out = close;
    *arguments_out = (pp_macro_argument_t *)0;
    *argument_count_out = 0u;
    return CTOOL_OK;
  }

  argument_start = open->next;
  depth = 0u;
  cursor = open->next;
  while (cursor != close) {
    ctool_bool separator =
        depth == 0u &&
                pp_token_equal_literal(&cursor->token, ",") == CTOOL_TRUE &&
                (macro->variadic == CTOOL_FALSE ||
                 argument_index < macro->parameter_count)
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if (separator == CTOOL_TRUE) {
      arguments[argument_index].raw = argument_start;
      arguments[argument_index].raw_count = token_count;
      pp_expand_list_zero(&arguments[argument_index].expanded);
      arguments[argument_index].omitted = CTOOL_FALSE;
      argument_index++;
      argument_start = cursor->next;
      token_count = 0u;
    } else {
      if (pp_token_equal_literal(&cursor->token, "(") == CTOOL_TRUE) {
        depth++;
      } else if (pp_token_equal_literal(&cursor->token, ")") == CTOOL_TRUE) {
        depth--;
      }
      token_count++;
    }
    cursor = cursor->next;
  }
  if (syntactic_argument_count != 0u) {
    arguments[argument_index].raw = argument_start;
    arguments[argument_index].raw_count = token_count;
    pp_expand_list_zero(&arguments[argument_index].expanded);
    arguments[argument_index].omitted = CTOOL_FALSE;
  }
  if (macro->variadic == CTOOL_TRUE &&
      syntactic_argument_count == macro->parameter_count) {
    arguments[macro->parameter_count].raw = close;
    arguments[macro->parameter_count].raw_count = 0u;
    pp_expand_list_zero(&arguments[macro->parameter_count].expanded);
    arguments[macro->parameter_count].omitted = CTOOL_TRUE;
  }
  *close_out = close;
  *arguments_out = arguments;
  *argument_count_out = argument_count;
  return CTOOL_OK;
}

static ctool_status_t pp_expand_macro_arguments(
    pp_context_t *context, pp_macro_t *macro,
    pp_macro_argument_t *arguments, ctool_u32 argument_count,
    const pp_token_t *invocation) {
  ctool_u32 index;
  ctool_status_t status;
  if (context->expansion_depth == PP_MACRO_EXPANSION_DEPTH_LIMIT) {
    pp_fail(context, CTOOL_ERR_LIMIT, CTOOL_C_PP_DIAG_MACRO_EXPANSION,
            invocation->location.line, invocation->location.column,
            "CupidC macro expansion depth limit exceeded");
    return CTOOL_ERR_LIMIT;
  }
  context->expansion_depth++;
  for (index = 0u; index < argument_count; index++) {
    pp_expand_list_t argument_work;
    if (macro->parameter_expanded[index] == CTOOL_FALSE) {
      continue;
    }
    status = pp_expand_list_copy_range(context, arguments[index].raw,
                                       arguments[index].raw_count,
                                       &argument_work);
    if (status == CTOOL_OK) {
      status = pp_expand_work(context, &argument_work,
                              &arguments[index].expanded);
    }
    if (status != CTOOL_OK) {
      context->expansion_depth--;
      return status;
    }
  }
  context->expansion_depth--;
  return CTOOL_OK;
}

static pp_token_t pp_replacement_token(const pp_token_t *source,
                                       const pp_token_t *invocation) {
  pp_token_t token = *source;
  token.location = invocation->location;
  token.pack_alignment = invocation->pack_alignment;
  token.at_line_start = CTOOL_FALSE;
  token.header_name = CTOOL_FALSE;
  return token;
}

static ctool_status_t pp_generated_token(
    pp_context_t *context, const pp_token_t *source,
    ctool_c_pp_token_kind_t kind, ctool_string_t spelling,
    pp_token_t *token_out) {
  ctool_status_t status;
  *token_out = *source;
  token_out->kind = kind;
  status = ctool_arena_copy_string(ctool_job_arena(context->job), spelling,
                                   &token_out->spelling);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
            source->location.line, source->location.column,
            "CupidC generated token storage limit exceeded");
  }
  return status;
}

static ctool_u32 pp_decimal_u32(char *output, ctool_u32 value) {
  char reverse[10];
  ctool_u32 count = 0u;
  ctool_u32 index;
  do {
    reverse[count] = (char)('0' + (char)(value % 10u));
    count++;
    value /= 10u;
  } while (value != 0u);
  for (index = 0u; index < count; index++) {
    output[index] = reverse[count - index - 1u];
  }
  return count;
}

static ctool_status_t pp_predefined_file_token(
    pp_context_t *context, const pp_token_t *source,
    pp_token_t *token_out) {
  ctool_string_t path = source->location.path;
  ctool_u32 size = 2u;
  ctool_u32 index;
  ctool_u32 offset = 0u;
  char *spelling;
  ctool_status_t status;
  for (index = 0u; index < path.size; index++) {
    ctool_u8 value = (ctool_u8)path.data[index];
    ctool_u32 width = value == (ctool_u8)'\\' || value == (ctool_u8)'"'
                          ? 2u
                          : value < 0x20u || value >= 0x7fu ? 4u : 1u;
    if (size > 0xffffffffu - width) {
      pp_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_C_PP_DIAG_LIMIT,
              source->location.line, source->location.column,
              "CupidC __FILE__ spelling is too large");
      return CTOOL_ERR_OVERFLOW;
    }
    size += width;
  }
  if (size == 0xffffffffu) {
    pp_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_C_PP_DIAG_LIMIT,
            source->location.line, source->location.column,
            "CupidC __FILE__ spelling is too large");
    return CTOOL_ERR_OVERFLOW;
  }
  status = ctool_arena_alloc_zero(ctool_job_arena(context->job), size + 1u,
                                  1u, 1u, (void **)&spelling);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
            source->location.line, source->location.column,
            "CupidC __FILE__ token storage limit exceeded");
    return status;
  }
  spelling[offset++] = '"';
  for (index = 0u; index < path.size; index++) {
    ctool_u8 value = (ctool_u8)path.data[index];
    if (value == (ctool_u8)'\\' || value == (ctool_u8)'"') {
      spelling[offset++] = '\\';
      spelling[offset++] = (char)value;
    } else if (value < 0x20u || value >= 0x7fu) {
      spelling[offset++] = '\\';
      spelling[offset++] = (char)('0' + (char)((value >> 6u) & 7u));
      spelling[offset++] = (char)('0' + (char)((value >> 3u) & 7u));
      spelling[offset++] = (char)('0' + (char)(value & 7u));
    } else {
      spelling[offset++] = (char)value;
    }
  }
  spelling[offset++] = '"';
  *token_out = *source;
  token_out->kind = CTOOL_C_PP_TOKEN_STRING;
  token_out->spelling.data = spelling;
  token_out->spelling.size = size;
  return CTOOL_OK;
}

static ctool_status_t pp_predefined_quoted_token(
    pp_context_t *context, const pp_token_t *source,
    ctool_string_t value, pp_token_t *token_out) {
  char *spelling;
  ctool_u32 index;
  ctool_status_t status = ctool_arena_alloc_zero(
      ctool_job_arena(context->job), value.size + 3u, 1u, 1u,
      (void **)&spelling);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
            source->location.line, source->location.column,
            "CupidC predefined string token storage limit exceeded");
    return status;
  }
  spelling[0] = '"';
  for (index = 0u; index < value.size; index++) {
    spelling[index + 1u] = value.data[index];
  }
  spelling[value.size + 1u] = '"';
  *token_out = *source;
  token_out->kind = CTOOL_C_PP_TOKEN_STRING;
  token_out->spelling.data = spelling;
  token_out->spelling.size = value.size + 2u;
  return CTOOL_OK;
}

static ctool_status_t pp_build_predefined_replacement(
    pp_context_t *context, const pp_token_t *source,
    pp_expand_list_t *replacement_out) {
  static const char one[] = "1";
  static const char zero[] = "0";
  static const char c11[] = "201112L";
  static const char default_date[] = "Jan  1 1970";
  static const char default_time[] = "00:00:00";
  char line[10];
  ctool_string_t spelling;
  pp_token_t token;
  ctool_status_t status;
  pp_expand_list_zero(replacement_out);
  if (pp_token_equal_literal(source, "__FILE__") == CTOOL_TRUE) {
    status = pp_predefined_file_token(context, source, &token);
  } else if (pp_token_equal_literal(source, "__LINE__") == CTOOL_TRUE) {
    spelling.data = line;
    spelling.size = pp_decimal_u32(line, source->location.line);
    status = pp_generated_token(context, source, CTOOL_C_PP_TOKEN_NUMBER,
                                spelling, &token);
  } else if (pp_token_equal_literal(source, "__DATE__") == CTOOL_TRUE ||
             pp_token_equal_literal(source, "__TIME__") == CTOOL_TRUE) {
    ctool_string_t value =
        pp_token_equal_literal(source, "__DATE__") == CTOOL_TRUE
            ? context->request->translation_date
            : context->request->translation_time;
    if (value.size == 0u) {
      if (pp_token_equal_literal(source, "__DATE__") == CTOOL_TRUE) {
        value.data = default_date;
        value.size = (ctool_u32)(sizeof(default_date) - 1u);
      } else {
        value.data = default_time;
        value.size = (ctool_u32)(sizeof(default_time) - 1u);
      }
    }
    status = pp_predefined_quoted_token(context, source, value, &token);
  } else {
    if (pp_token_equal_literal(source, "__STDC_HOSTED__") == CTOOL_TRUE &&
        context->request->hosted_environment == CTOOL_FALSE) {
      spelling.data = zero;
      spelling.size = 1u;
    } else if (pp_token_equal_literal(source, "__STDC_VERSION__") ==
               CTOOL_TRUE) {
      spelling.data = c11;
      spelling.size = 7u;
    } else {
      spelling.data = one;
      spelling.size = 1u;
    }
    status = pp_generated_token(context, source, CTOOL_C_PP_TOKEN_NUMBER,
                                spelling, &token);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  return pp_expand_list_append_copy(context, replacement_out, &token,
                                    (const pp_hide_t *)0);
}

static ctool_status_t pp_append_argument_replacement(
    pp_context_t *context, const pp_macro_argument_t *argument,
    ctool_bool expanded, ctool_bool placemarker_if_empty,
    const pp_token_t *placeholder, const pp_token_t *invocation,
    const pp_hide_t *base_hide, pp_expand_list_t *replacement_out) {
  pp_expand_node_t *node;
  ctool_u32 count;
  ctool_u32 index;
  ctool_status_t status;
  if (argument == (const pp_macro_argument_t *)0) {
    pp_fail(context, CTOOL_ERR_INTERNAL,
            CTOOL_C_PP_DIAG_MACRO_EXPANSION,
            invocation->location.line, invocation->location.column,
            "CupidC macro argument replacement is missing");
    return CTOOL_ERR_INTERNAL;
  }
  node = expanded == CTOOL_TRUE ? argument->expanded.head : argument->raw;
  count = expanded == CTOOL_TRUE ? argument->expanded.count
                                  : argument->raw_count;
  if (count == 0u && placemarker_if_empty == CTOOL_TRUE) {
    pp_token_t marker = pp_replacement_token(placeholder, invocation);
    return pp_expand_list_append_special(
        context, replacement_out, PP_EXPAND_PLACEMARKER, &marker,
        base_hide);
  }
  for (index = 0u; index < count; index++) {
    pp_token_t token;
    const pp_hide_t *hide;
    if (node == (pp_expand_node_t *)0) {
      pp_fail(context, CTOOL_ERR_INTERNAL,
              CTOOL_C_PP_DIAG_MACRO_EXPANSION,
              invocation->location.line, invocation->location.column,
              "CupidC macro argument replacement became inconsistent");
      return CTOOL_ERR_INTERNAL;
    }
    token = node->token;
    status = pp_hide_union(context, node->hide, base_hide, invocation,
                           &hide);
    if (status != CTOOL_OK) {
      return status;
    }
    if (index == 0u) {
      token.leading_space = placeholder->leading_space;
    }
    token.pack_alignment = invocation->pack_alignment;
    token.at_line_start = CTOOL_FALSE;
    token.header_name = CTOOL_FALSE;
    status = pp_expand_list_append_copy(context, replacement_out, &token,
                                        hide);
    if (status != CTOOL_OK) {
      return status;
    }
    replacement_out->tail->condition_defined = CTOOL_FALSE;
    node = node->next;
  }
  return CTOOL_OK;
}

static ctool_status_t pp_stringify_argument(
    pp_context_t *context, const pp_macro_argument_t *argument,
    const pp_token_t *operator_token, const pp_token_t *invocation,
    const pp_hide_t *base_hide, pp_expand_list_t *replacement_out) {
  pp_expand_node_t *node = argument->raw;
  ctool_u32 size = 2u;
  ctool_u32 index;
  ctool_u32 offset = 0u;
  char *spelling;
  pp_token_t token;
  ctool_status_t status;
  for (index = 0u; index < argument->raw_count; index++) {
    ctool_u32 character;
    if (node == (pp_expand_node_t *)0) {
      pp_fail(context, CTOOL_ERR_INTERNAL,
              CTOOL_C_PP_DIAG_MACRO_EXPANSION,
              invocation->location.line, invocation->location.column,
              "CupidC stringification argument became inconsistent");
      return CTOOL_ERR_INTERNAL;
    }
    if (index != 0u && node->token.leading_space == CTOOL_TRUE) {
      if (size == 0xffffffffu) {
        pp_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_C_PP_DIAG_LIMIT,
                invocation->location.line, invocation->location.column,
                "CupidC stringified macro argument is too large");
        return CTOOL_ERR_OVERFLOW;
      }
      size++;
    }
    for (character = 0u; character < node->token.spelling.size;
         character++) {
      ctool_u32 addition =
          (node->token.kind == CTOOL_C_PP_TOKEN_STRING ||
           node->token.kind == CTOOL_C_PP_TOKEN_CHARACTER) &&
                  (node->token.spelling.data[character] == '\\' ||
                   node->token.spelling.data[character] == '"')
              ? 2u
              : 1u;
      if (size > 0xffffffffu - addition) {
        pp_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_C_PP_DIAG_LIMIT,
                invocation->location.line, invocation->location.column,
                "CupidC stringified macro argument is too large");
        return CTOOL_ERR_OVERFLOW;
      }
      size += addition;
    }
    node = node->next;
  }
  if (size == 0xffffffffu) {
    pp_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_C_PP_DIAG_LIMIT,
            invocation->location.line, invocation->location.column,
            "CupidC stringified macro argument is too large");
    return CTOOL_ERR_OVERFLOW;
  }
  status = ctool_arena_alloc_zero(ctool_job_arena(context->job), size + 1u,
                                  1u, 1u, (void **)&spelling);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
            invocation->location.line, invocation->location.column,
            "CupidC stringified macro storage limit exceeded");
    return status;
  }
  spelling[offset] = '"';
  offset++;
  node = argument->raw;
  for (index = 0u; index < argument->raw_count; index++) {
    ctool_u32 character;
    if (index != 0u && node->token.leading_space == CTOOL_TRUE) {
      spelling[offset] = ' ';
      offset++;
    }
    for (character = 0u; character < node->token.spelling.size;
         character++) {
      char value = node->token.spelling.data[character];
      if ((node->token.kind == CTOOL_C_PP_TOKEN_STRING ||
           node->token.kind == CTOOL_C_PP_TOKEN_CHARACTER) &&
          (value == '\\' || value == '"')) {
        spelling[offset] = '\\';
        offset++;
      }
      spelling[offset] = value;
      offset++;
    }
    node = node->next;
  }
  spelling[offset] = '"';
  token = pp_replacement_token(operator_token, invocation);
  token.kind = CTOOL_C_PP_TOKEN_STRING;
  token.spelling.data = spelling;
  token.spelling.size = size;
  return pp_expand_list_append_copy(context, replacement_out, &token,
                                    base_hide);
}

static ctool_status_t pp_paste_tokens(
    pp_context_t *context, pp_expand_node_t *left,
    const pp_expand_node_t *right, const pp_token_t *invocation) {
  ctool_u32 size;
  char *spelling;
  pp_input_t input;
  pp_token_t *tokens;
  ctool_u32 token_count;
  pp_context_t scratch;
  const pp_hide_t *hide;
  ctool_status_t status;
  if (left->kind == PP_EXPAND_PLACEMARKER &&
      right->kind == PP_EXPAND_PLACEMARKER) {
    left->condition_defined = CTOOL_FALSE;
    return CTOOL_OK;
  }
  if (left->kind == PP_EXPAND_PLACEMARKER) {
    ctool_bool leading_space = left->token.leading_space;
    left->kind = right->kind;
    left->token = right->token;
    left->token.leading_space = leading_space;
    left->hide = right->hide;
    left->condition_defined = CTOOL_FALSE;
    return CTOOL_OK;
  }
  if (right->kind == PP_EXPAND_PLACEMARKER) {
    left->condition_defined = CTOOL_FALSE;
    return CTOOL_OK;
  }
  if (left->kind != PP_EXPAND_TOKEN || right->kind != PP_EXPAND_TOKEN) {
    pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_PASTE,
            invocation->location.line, invocation->location.column,
            "CupidC macro paste operands are invalid");
    return CTOOL_ERR_INPUT;
  }
  if (left->token.spelling.size >
      0xffffffffu - right->token.spelling.size) {
    pp_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_C_PP_DIAG_LIMIT,
            invocation->location.line, invocation->location.column,
            "CupidC pasted token is too large");
    return CTOOL_ERR_OVERFLOW;
  }
  size = left->token.spelling.size + right->token.spelling.size;
  if (size == 0xffffffffu) {
    pp_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_C_PP_DIAG_LIMIT,
            invocation->location.line, invocation->location.column,
            "CupidC pasted token is too large");
    return CTOOL_ERR_OVERFLOW;
  }
  status = ctool_arena_alloc_zero(ctool_job_arena(context->job), size + 1u,
                                  1u, 1u, (void **)&spelling);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
            invocation->location.line, invocation->location.column,
            "CupidC pasted token storage limit exceeded");
    return status;
  }
  for (token_count = 0u; token_count < left->token.spelling.size;
       token_count++) {
    spelling[token_count] = left->token.spelling.data[token_count];
  }
  for (token_count = 0u; token_count < right->token.spelling.size;
       token_count++) {
    spelling[left->token.spelling.size + token_count] =
        right->token.spelling.data[token_count];
  }
  input.text = spelling;
  input.size = size;
  input.splice_offsets = (ctool_u32 *)0;
  input.splice_count = 0u;
  scratch = *context;
  scratch.failure_status = CTOOL_OK;
  scratch.failure_code = 0u;
  scratch.failure_line = 0u;
  scratch.failure_column = 0u;
  scratch.failure_message = "CupidC pasted-token validation failed";
  status = pp_tokenize(&scratch, &input, &tokens, &token_count);
  if (status != CTOOL_OK || token_count != 1u ||
      tokens[0].spelling.size != size) {
    if (status != CTOOL_OK && status != CTOOL_ERR_INPUT) {
      pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
              invocation->location.line, invocation->location.column,
              "CupidC pasted-token validation exceeded a limit");
      return status;
    }
    pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_PASTE,
            invocation->location.line, invocation->location.column,
            "CupidC macro paste does not form one preprocessing token");
    return CTOOL_ERR_INPUT;
  }
  status = pp_hide_intersection(context, left->hide, right->hide,
                                invocation, &hide);
  if (status != CTOOL_OK) {
    return status;
  }
  tokens[0].location = invocation->location;
  tokens[0].pack_alignment = invocation->pack_alignment;
  tokens[0].leading_space = left->token.leading_space;
  tokens[0].at_line_start = CTOOL_FALSE;
  tokens[0].header_name = CTOOL_FALSE;
  left->token = tokens[0];
  left->hide = hide;
  left->condition_defined = CTOOL_FALSE;
  return CTOOL_OK;
}

static ctool_status_t pp_resolve_pastes(pp_context_t *context,
                                        pp_expand_list_t *replacement,
                                        const pp_token_t *invocation) {
  pp_expand_node_t *node = replacement->head;
  pp_expand_node_t *previous = (pp_expand_node_t *)0;
  ctool_status_t status;
  while (node != (pp_expand_node_t *)0) {
    if (node->kind == PP_EXPAND_PASTE) {
      pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_PASTE,
              invocation->location.line, invocation->location.column,
              "CupidC macro paste has no left operand");
      return CTOOL_ERR_INPUT;
    }
    if (node->next != (pp_expand_node_t *)0 &&
        node->next->kind == PP_EXPAND_PASTE) {
      pp_expand_node_t *right = node->next->next;
      if (right == (pp_expand_node_t *)0 ||
          right->kind == PP_EXPAND_PASTE) {
        pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_MACRO_PASTE,
                invocation->location.line, invocation->location.column,
                "CupidC macro paste has an invalid operand");
        return CTOOL_ERR_INPUT;
      }
      status = pp_paste_tokens(context, node, right, invocation);
      if (status != CTOOL_OK) {
        return status;
      }
      node->next = right->next;
      replacement->count -= 2u;
      continue;
    }
    previous = node;
    node = node->next;
  }
  replacement->tail = previous;
  previous = (pp_expand_node_t *)0;
  node = replacement->head;
  while (node != (pp_expand_node_t *)0) {
    if (node->kind == PP_EXPAND_PLACEMARKER) {
      if (node->token.leading_space == CTOOL_TRUE) {
        if (node->next != (pp_expand_node_t *)0) {
          node->next->token.leading_space = CTOOL_TRUE;
        } else {
          replacement->trailing_space = CTOOL_TRUE;
        }
      }
      if (previous == (pp_expand_node_t *)0) {
        replacement->head = node->next;
      } else {
        previous->next = node->next;
      }
      replacement->count--;
      node = node->next;
      continue;
    }
    previous = node;
    node = node->next;
  }
  replacement->tail = previous;
  return CTOOL_OK;
}

static void pp_apply_pending_space(pp_expand_list_t *replacement,
                                   pp_expand_node_t *old_tail,
                                   ctool_bool *pending_space) {
  pp_expand_node_t *first = old_tail == (pp_expand_node_t *)0
                                ? replacement->head
                                : old_tail->next;
  if (first != (pp_expand_node_t *)0 && *pending_space == CTOOL_TRUE) {
    first->token.leading_space = CTOOL_TRUE;
    *pending_space = CTOOL_FALSE;
  }
}

static ctool_status_t pp_build_replacement(
    pp_context_t *context, pp_macro_t *macro,
    pp_expand_node_t *invocation, pp_macro_argument_t *arguments,
    const pp_hide_t *base_hide, pp_expand_list_t *replacement_out) {
  ctool_u32 index = 0u;
  ctool_bool pending_space = CTOOL_FALSE;
  ctool_status_t status;
  pp_expand_list_zero(replacement_out);
  if (macro->function_like == CTOOL_TRUE &&
      macro->replacement_count != 0u &&
      macro->replacement_parameters == (ctool_u32 *)0) {
    pp_fail(context, CTOOL_ERR_INTERNAL,
            CTOOL_C_PP_DIAG_MACRO_EXPANSION,
            invocation->token.location.line, invocation->token.location.column,
            "CupidC function macro substitution plan is missing");
    return CTOOL_ERR_INTERNAL;
  }
  if (macro->function_like == CTOOL_TRUE &&
      (macro->parameter_count != 0u || macro->variadic == CTOOL_TRUE) &&
      arguments == (pp_macro_argument_t *)0) {
    pp_fail(context, CTOOL_ERR_INTERNAL,
            CTOOL_C_PP_DIAG_MACRO_EXPANSION,
            invocation->token.location.line, invocation->token.location.column,
            "CupidC function macro arguments are missing");
    return CTOOL_ERR_INTERNAL;
  }
  while (index < macro->replacement_count) {
    pp_expand_node_t *old_tail = replacement_out->tail;
    ctool_u32 old_count = replacement_out->count;
    const pp_token_t *replacement = &macro->replacement[index];
    ctool_u32 parameter = PP_NOT_A_PARAMETER;
    if (macro->function_like == CTOOL_TRUE) {
      parameter = macro->replacement_parameters[index];
    }
    if (context->request->gnu_extensions == CTOOL_TRUE &&
        macro->variadic == CTOOL_TRUE && arguments != (pp_macro_argument_t *)0 &&
        index + 2u < macro->replacement_count &&
        macro->replacement_parameters != (ctool_u32 *)0 &&
        pp_token_equal_literal(replacement, ",") == CTOOL_TRUE &&
        pp_token_is_paste_operator(&macro->replacement[index + 1u]) ==
            CTOOL_TRUE &&
        macro->replacement_parameters[index + 2u] ==
            macro->parameter_count) {
      const pp_macro_argument_t *argument =
          &arguments[macro->parameter_count];
      if (argument->omitted == CTOOL_FALSE) {
        pp_token_t comma =
            pp_replacement_token(replacement, &invocation->token);
        pp_token_t variadic_placeholder =
            macro->replacement[index + 2u];
        variadic_placeholder.leading_space = CTOOL_FALSE;
        status = pp_expand_list_append_copy(context, replacement_out,
                                            &comma, base_hide);
        if (status == CTOOL_OK) {
          status = pp_append_argument_replacement(
              context, argument, CTOOL_FALSE, CTOOL_FALSE,
              &variadic_placeholder, &invocation->token,
              base_hide, replacement_out);
        }
        if (status != CTOOL_OK) {
          return status;
        }
        pp_apply_pending_space(replacement_out, old_tail, &pending_space);
      }
      index += 3u;
      continue;
    }
    if (macro->function_like == CTOOL_TRUE &&
        pp_token_is_stringify_operator(replacement) == CTOOL_TRUE) {
      if (macro->replacement_parameters == (ctool_u32 *)0 ||
          arguments == (pp_macro_argument_t *)0 ||
          index + 1u >= macro->replacement_count) {
        pp_fail(context, CTOOL_ERR_INTERNAL,
                CTOOL_C_PP_DIAG_MACRO_EXPANSION,
                invocation->token.location.line,
                invocation->token.location.column,
                "CupidC macro stringification plan is invalid");
        return CTOOL_ERR_INTERNAL;
      }
      parameter = macro->replacement_parameters[index + 1u];
      status = pp_stringify_argument(
          context, &arguments[parameter], replacement, &invocation->token,
          base_hide, replacement_out);
      if (status != CTOOL_OK) {
        return status;
      }
      pp_apply_pending_space(replacement_out, old_tail, &pending_space);
      index += 2u;
      continue;
    }
    if (pp_token_is_paste_operator(replacement) == CTOOL_TRUE) {
      pp_token_t paste =
          pp_replacement_token(replacement, &invocation->token);
      status = pp_expand_list_append_special(
          context, replacement_out, PP_EXPAND_PASTE, &paste, base_hide);
    } else if (parameter != PP_NOT_A_PARAMETER) {
      ctool_bool pasted =
          (index != 0u &&
           pp_token_is_paste_operator(&macro->replacement[index - 1u]) ==
               CTOOL_TRUE) ||
                  (index + 1u < macro->replacement_count &&
                   pp_token_is_paste_operator(&macro->replacement[index + 1u]) ==
                       CTOOL_TRUE)
              ? CTOOL_TRUE
              : CTOOL_FALSE;
      ctool_u32 argument_count = macro->parameter_count +
                                 (macro->variadic == CTOOL_TRUE ? 1u : 0u);
      if (arguments == (pp_macro_argument_t *)0 ||
          parameter >= argument_count) {
        pp_fail(context, CTOOL_ERR_INTERNAL,
                CTOOL_C_PP_DIAG_MACRO_EXPANSION,
                invocation->token.location.line,
                invocation->token.location.column,
                "CupidC macro substitution parameter is invalid");
        return CTOOL_ERR_INTERNAL;
      }
      status = pp_append_argument_replacement(
          context, &arguments[parameter],
          pasted == CTOOL_TRUE ? CTOOL_FALSE : CTOOL_TRUE, pasted,
          replacement, &invocation->token, base_hide, replacement_out);
    } else {
      pp_token_t token =
          pp_replacement_token(replacement, &invocation->token);
      status = pp_expand_list_append_copy(context, replacement_out, &token,
                                          base_hide);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (replacement_out->count != old_count) {
      pp_apply_pending_space(replacement_out, old_tail, &pending_space);
    } else if (parameter != PP_NOT_A_PARAMETER &&
               pp_token_is_paste_operator(replacement) == CTOOL_FALSE &&
               replacement->leading_space == CTOOL_TRUE) {
      pending_space = CTOOL_TRUE;
    }
    index++;
  }
  replacement_out->trailing_space = pending_space;
  if (replacement_out->head != (pp_expand_node_t *)0) {
    replacement_out->head->token.leading_space =
        invocation->token.leading_space;
  }
  status = pp_resolve_pastes(context, replacement_out, &invocation->token);
  if (status == CTOOL_OK &&
      replacement_out->head != (pp_expand_node_t *)0) {
    replacement_out->head->token.leading_space =
        invocation->token.leading_space;
  }
  return status;
}

static ctool_status_t pp_build_object_replacement(
    pp_context_t *context, pp_macro_t *macro, pp_expand_node_t *invocation,
    pp_expand_list_t *replacement_out) {
  const pp_hide_t *hide;
  ctool_status_t status = pp_hide_add(
      context, invocation->hide, macro, &invocation->token, &hide);
  if (status != CTOOL_OK) {
    return status;
  }
  return pp_build_replacement(context, macro, invocation,
                              (pp_macro_argument_t *)0, hide,
                              replacement_out);
}

static ctool_status_t pp_build_function_replacement(
    pp_context_t *context, pp_macro_t *macro,
    pp_expand_node_t *invocation, pp_expand_node_t *close,
    pp_macro_argument_t *arguments, pp_expand_list_t *replacement_out) {
  const pp_hide_t *base_hide;
  ctool_status_t status = pp_hide_intersection(
      context, invocation->hide, close->hide, &invocation->token, &base_hide);
  if (status == CTOOL_OK) {
    status = pp_hide_add(context, base_hide, macro, &invocation->token,
                         &base_hide);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  return pp_build_replacement(context, macro, invocation, arguments,
                              base_hide, replacement_out);
}

static ctool_status_t pp_expand_condition_defined(
    pp_context_t *context, pp_expand_list_t *work) {
  static const char zero[] = "0";
  static const char one[] = "1";
  pp_expand_node_t *operator_node = work->head;
  pp_expand_node_t *operand;
  ctool_bool parenthesized = CTOOL_FALSE;
  ctool_string_t spelling;
  pp_token_t number;
  pp_expand_list_t replacement;
  ctool_status_t status;
  if (operator_node->condition_defined == CTOOL_FALSE) {
    pp_fail(context, CTOOL_ERR_INPUT,
            CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
            operator_node->token.location.line,
            operator_node->token.location.column,
            "CupidC macro expansion produced a defined operator");
    return CTOOL_ERR_INPUT;
  }
  (void)pp_expand_list_pop(work);
  if (work->head != (pp_expand_node_t *)0 &&
      pp_token_equal_literal(&work->head->token, "(") == CTOOL_TRUE) {
    parenthesized = CTOOL_TRUE;
    (void)pp_expand_list_pop(work);
  }
  operand = work->head;
  if (operand == (pp_expand_node_t *)0 ||
      operand->token.kind != CTOOL_C_PP_TOKEN_IDENTIFIER) {
    pp_fail(context, CTOOL_ERR_INPUT,
            CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
            operator_node->token.location.line,
            operator_node->token.location.column,
            "CupidC defined requires one macro identifier");
    return CTOOL_ERR_INPUT;
  }
  status = pp_reject_reserved_variadic_identifier(context, &operand->token);
  if (status != CTOOL_OK) {
    return status;
  }
  (void)pp_expand_list_pop(work);
  if (parenthesized == CTOOL_TRUE) {
    if (work->head == (pp_expand_node_t *)0 ||
        pp_token_equal_literal(&work->head->token, ")") == CTOOL_FALSE) {
      pp_fail(context, CTOOL_ERR_INPUT,
              CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
              operator_node->token.location.line,
              operator_node->token.location.column,
              "CupidC parenthesized defined operand is malformed");
      return CTOOL_ERR_INPUT;
    }
    (void)pp_expand_list_pop(work);
  }
  spelling.data = pp_macro_is_defined(context, operand->token.spelling) ==
                          CTOOL_TRUE
                      ? one
                      : zero;
  spelling.size = 1u;
  status = pp_generated_token(context, &operator_node->token,
                              CTOOL_C_PP_TOKEN_NUMBER, spelling, &number);
  if (status != CTOOL_OK) {
    return status;
  }
  pp_expand_list_zero(&replacement);
  status = pp_expand_list_append_copy(
      context, &replacement, &number, operator_node->hide);
  if (status != CTOOL_OK) {
    return status;
  }
  replacement.head->condition_defined = CTOOL_FALSE;
  pp_expand_list_prepend(work, &replacement);
  return CTOOL_OK;
}

static ctool_status_t pp_expand_work(pp_context_t *context,
                                     pp_expand_list_t *work,
                                     pp_expand_list_t *private_output) {
  while (work->head != (pp_expand_node_t *)0) {
    pp_expand_node_t *node = work->head;
    pp_macro_t *macro = (pp_macro_t *)0;
    ctool_status_t status;
    if (node->kind != PP_EXPAND_TOKEN) {
      pp_fail(context, CTOOL_ERR_INTERNAL,
              CTOOL_C_PP_DIAG_MACRO_EXPANSION,
              node->token.location.line, node->token.location.column,
              "CupidC private macro operator escaped replacement resolution");
      return CTOOL_ERR_INTERNAL;
    }
    if (node->token.kind == CTOOL_C_PP_TOKEN_IDENTIFIER) {
      if (context->condition_expression == CTOOL_TRUE &&
          context->expansion_depth == 0u &&
          pp_token_equal_literal(&node->token, "defined") == CTOOL_TRUE) {
        status = pp_expand_condition_defined(context, work);
        if (status != CTOOL_OK) {
          return status;
        }
        continue;
      }
      status = pp_reject_reserved_variadic_identifier(context, &node->token);
      if (status != CTOOL_OK) {
        return status;
      }
      if (pp_name_is_predefined(node->token.spelling) == CTOOL_TRUE) {
        pp_expand_list_t replacement;
        node = pp_expand_list_pop(work);
        status = pp_build_predefined_replacement(
            context, &node->token, &replacement);
        if (status != CTOOL_OK) {
          return status;
        }
        pp_expand_list_prepend(work, &replacement);
        continue;
      }
      macro = pp_macro_find(context, node->token.spelling);
      if (macro != (pp_macro_t *)0 &&
          pp_hide_contains(node->hide, macro) == CTOOL_TRUE) {
        macro = (pp_macro_t *)0;
      }
    }
    if (macro == (pp_macro_t *)0 ||
        (macro->function_like == CTOOL_TRUE &&
         (node->next == (pp_expand_node_t *)0 ||
          pp_token_equal_literal(&node->next->token, "(") == CTOOL_FALSE))) {
      node = pp_expand_list_pop(work);
      if (private_output == (pp_expand_list_t *)0) {
        status = pp_output_append(context, &node->token);
      } else {
        pp_expand_list_append_node(private_output, node);
        status = CTOOL_OK;
      }
      if (status != CTOOL_OK) {
        return status;
      }
      continue;
    }
    if (macro->function_like == CTOOL_FALSE) {
      pp_expand_list_t replacement;
      ctool_bool invocation_space;
      node = pp_expand_list_pop(work);
      invocation_space = node->token.leading_space;
      status = pp_build_object_replacement(context, macro, node,
                                           &replacement);
      if (status != CTOOL_OK) {
        return status;
      }
      if (work->head != (pp_expand_node_t *)0 &&
          (replacement.trailing_space == CTOOL_TRUE ||
           (replacement.head == (pp_expand_node_t *)0 &&
            invocation_space == CTOOL_TRUE))) {
        work->head->token.leading_space = CTOOL_TRUE;
      }
      pp_expand_list_prepend(work, &replacement);
    } else {
      pp_expand_node_t *close;
      pp_macro_argument_t *arguments;
      ctool_u32 argument_count;
      ctool_u32 consumed = 0u;
      ctool_bool invocation_space = node->token.leading_space;
      pp_expand_node_t *cursor;
      pp_expand_list_t replacement;
      status = pp_collect_macro_arguments(context, macro, node, &close,
                                          &arguments, &argument_count);
      if (status == CTOOL_OK) {
        status = pp_expand_macro_arguments(context, macro, arguments,
                                           argument_count, &node->token);
      }
      if (status == CTOOL_OK) {
        status = pp_build_function_replacement(
            context, macro, node, close, arguments, &replacement);
      }
      if (status != CTOOL_OK) {
        return status;
      }
      cursor = node;
      for (;;) {
        consumed++;
        if (cursor == close) {
          break;
        }
        cursor = cursor->next;
      }
      while (consumed != 0u) {
        (void)pp_expand_list_pop(work);
        consumed--;
      }
      if (work->head != (pp_expand_node_t *)0 &&
          (replacement.trailing_space == CTOOL_TRUE ||
           (replacement.head == (pp_expand_node_t *)0 &&
            invocation_space == CTOOL_TRUE))) {
        work->head->token.leading_space = CTOOL_TRUE;
      }
      pp_expand_list_prepend(work, &replacement);
    }
  }
  return CTOOL_OK;
}

static ctool_status_t pp_emit_expanded_range(pp_context_t *context,
                                             const pp_token_t *tokens,
                                             ctool_u32 token_count) {
  pp_expand_list_t work;
  ctool_u32 index;
  ctool_status_t status;
  pp_expand_list_zero(&work);
  for (index = 0u; index < token_count; index++) {
    status = pp_reject_reserved_variadic_identifier(context, &tokens[index]);
    if (status != CTOOL_OK) {
      return status;
    }
    status = pp_expand_list_append_copy(context, &work, &tokens[index],
                                        (const pp_hide_t *)0);
    if (status != CTOOL_OK) {
      return status;
    }
    work.tail->token.pack_alignment = context->pack_alignment;
  }
  return pp_expand_work(context, &work, (pp_expand_list_t *)0);
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
  entry->entered = CTOOL_FALSE;
  entry->pragma_once = CTOOL_FALSE;
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

static ctool_status_t pp_function_macro_unsupported(
    pp_context_t *context, const pp_token_t *token, ctool_u32 code,
    const char *message) {
  pp_fail(context, CTOOL_ERR_UNSUPPORTED, code, token->location.line,
          token->location.column, message);
  return CTOOL_ERR_UNSUPPORTED;
}

static ctool_status_t pp_handle_function_define(
    pp_context_t *context, const pp_token_t *marker,
    const pp_token_t *tokens, ctool_u32 name_index, ctool_u32 end) {
  ctool_string_t *parameters = (ctool_string_t *)0;
  pp_token_t *replacement = (pp_token_t *)0;
  ctool_u32 parameter_count = 0u;
  ctool_u32 parameter_index = 0u;
  ctool_u32 close = name_index + 2u;
  ctool_u32 index;
  ctool_u32 replacement_count;
  ctool_bool variadic = CTOOL_FALSE;
  ctool_status_t status;

  if (close > end) {
    return pp_directive_error(context, marker,
                              CTOOL_C_PP_DIAG_MACRO_DEFINITION,
                              "CupidC function macro parameter list is unterminated");
  }
  if (close < end &&
      pp_token_equal_literal(&tokens[close], ")") == CTOOL_TRUE) {
    /* Empty parameter list. */
  } else {
    for (;;) {
      if (close >= end) {
        return pp_directive_error(
            context, marker, CTOOL_C_PP_DIAG_MACRO_DEFINITION,
            "CupidC function macro parameter list is unterminated");
      }
      if (pp_token_equal_literal(&tokens[close], "...") == CTOOL_TRUE) {
        variadic = CTOOL_TRUE;
        close++;
        if (close >= end ||
            pp_token_equal_literal(&tokens[close], ")") == CTOOL_FALSE) {
          pp_fail(context, CTOOL_ERR_INPUT,
                  CTOOL_C_PP_DIAG_MACRO_DEFINITION,
                  tokens[close - 1u].location.line,
                  tokens[close - 1u].location.column,
                  "CupidC variadic ellipsis must end the parameter list");
          return CTOOL_ERR_INPUT;
        }
        break;
      }
      if (tokens[close].kind == CTOOL_C_PP_TOKEN_IDENTIFIER &&
          pp_name_is_variadic_identifier(tokens[close].spelling) ==
              CTOOL_TRUE) {
        pp_fail(context, CTOOL_ERR_INPUT,
                CTOOL_C_PP_DIAG_MACRO_DEFINITION,
                tokens[close].location.line, tokens[close].location.column,
                "CupidC __VA_ARGS__ is reserved for variadic macro replacement lists");
        return CTOOL_ERR_INPUT;
      }
      if (pp_token_is_identifier(&tokens[close]) == CTOOL_FALSE) {
        pp_fail(context, CTOOL_ERR_INPUT,
                CTOOL_C_PP_DIAG_MACRO_DEFINITION,
                tokens[close].location.line, tokens[close].location.column,
                "CupidC macro parameter must be an identifier");
        return CTOOL_ERR_INPUT;
      }
      if (parameter_count == PP_MACRO_PARAMETER_LIMIT) {
        pp_fail(context, CTOOL_ERR_LIMIT, CTOOL_C_PP_DIAG_LIMIT,
                tokens[close].location.line, tokens[close].location.column,
                "CupidC macro parameter limit exceeded");
        return CTOOL_ERR_LIMIT;
      }
      parameter_count++;
      close++;
      if (close >= end) {
        return pp_directive_error(
            context, marker, CTOOL_C_PP_DIAG_MACRO_DEFINITION,
            "CupidC function macro parameter list is unterminated");
      }
      if (pp_token_equal_literal(&tokens[close], ")") == CTOOL_TRUE) {
        break;
      }
      if (pp_token_equal_literal(&tokens[close], "...") == CTOOL_TRUE) {
        return pp_function_macro_unsupported(
            context, &tokens[close], CTOOL_C_PP_DIAG_MACRO_DEFINITION,
            "CupidC named GNU variadic parameters are not implemented yet");
      }
      if (pp_token_equal_literal(&tokens[close], ",") == CTOOL_FALSE) {
        pp_fail(context, CTOOL_ERR_INPUT,
                CTOOL_C_PP_DIAG_MACRO_DEFINITION,
                tokens[close].location.line, tokens[close].location.column,
                "CupidC macro parameters require comma separators");
        return CTOOL_ERR_INPUT;
      }
      close++;
    }
  }

  if (parameter_count != 0u) {
    status = ctool_arena_alloc_zero(
        ctool_job_arena(context->job), parameter_count,
        (ctool_u32)sizeof(*parameters), (ctool_u32)sizeof(void *),
        (void **)&parameters);
    if (status != CTOOL_OK) {
      pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
              tokens[name_index].location.line,
              tokens[name_index].location.column,
              "CupidC macro parameter storage limit exceeded");
      return status;
    }
  }
  index = name_index + 2u;
  while (parameter_index < parameter_count) {
    ctool_u32 previous;
    parameters[parameter_index] = tokens[index].spelling;
    for (previous = 0u; previous < parameter_index; previous++) {
      if (pp_string_equal(parameters[previous], parameters[parameter_index]) ==
          CTOOL_TRUE) {
        pp_fail(context, CTOOL_ERR_INPUT,
                CTOOL_C_PP_DIAG_MACRO_DEFINITION,
                tokens[index].location.line, tokens[index].location.column,
                "CupidC macro parameter name is duplicated");
        return CTOOL_ERR_INPUT;
      }
    }
    parameter_index++;
    index++;
    if (parameter_index < parameter_count) {
      index++;
    }
  }

  replacement_count = end - close - 1u;
  status = pp_copy_token_range(context, tokens + close + 1u,
                               replacement_count, &replacement);
  if (status != CTOOL_OK) {
    pp_fail(context, status, CTOOL_C_PP_DIAG_LIMIT,
            tokens[name_index].location.line,
            tokens[name_index].location.column,
            "CupidC macro replacement storage limit exceeded");
    return status;
  }
  return pp_define_macro(
      context, tokens[name_index].spelling, CTOOL_TRUE, parameters,
      parameter_count, variadic, replacement, replacement_count,
      tokens[name_index].location.line, tokens[name_index].location.column);
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
    return pp_handle_function_define(context, marker, tokens, begin, end);
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

static ctool_status_t pp_pragma_pack_error(pp_context_t *context,
                                           const pp_token_t *marker,
                                           ctool_status_t status,
                                           const char *message) {
  pp_fail(context, status, CTOOL_C_PP_DIAG_PRAGMA_PACK,
          marker->location.line, marker->location.column, message);
  return status;
}

static ctool_i32 pp_integer_digit_value(char character);
static ctool_bool pp_integer_is_u_suffix(char character);
static ctool_bool pp_integer_is_l_suffix(char character);

static ctool_bool pp_pragma_pack_alignment(const pp_token_t *token,
                                           ctool_u32 *alignment_out) {
  ctool_string_t text = token->spelling;
  ctool_u32 index = 0u;
  ctool_u32 base = 10u;
  ctool_u32 value = 0u;
  ctool_bool have_digit = CTOOL_FALSE;
  ctool_bool saw_u_suffix = CTOOL_FALSE;
  if (token->kind != CTOOL_C_PP_TOKEN_NUMBER) {
    return CTOOL_FALSE;
  }
  if (text.size == 0u) {
    return CTOOL_FALSE;
  }
  if (text.data[0] == '0') {
    have_digit = CTOOL_TRUE;
    index = 1u;
    base = 8u;
    if (index < text.size &&
        (text.data[index] == 'x' || text.data[index] == 'X')) {
      base = 16u;
      have_digit = CTOOL_FALSE;
      index++;
    }
  }
  while (index < text.size) {
    ctool_i32 digit = pp_integer_digit_value(text.data[index]);
    if (digit < 0 || (ctool_u32)digit >= base) {
      break;
    }
    have_digit = CTOOL_TRUE;
    if (value > (16u - (ctool_u32)digit) / base) {
      return CTOOL_FALSE;
    }
    value = value * base + (ctool_u32)digit;
    index++;
  }
  if (have_digit == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  if (index < text.size &&
      pp_integer_is_u_suffix(text.data[index]) == CTOOL_TRUE) {
    saw_u_suffix = CTOOL_TRUE;
    index++;
  }
  if (index < text.size &&
      pp_integer_is_l_suffix(text.data[index]) == CTOOL_TRUE) {
    char first = text.data[index];
    index++;
    if (index < text.size && text.data[index] == first) {
      index++;
    }
  }
  if (saw_u_suffix == CTOOL_FALSE && index < text.size &&
      pp_integer_is_u_suffix(text.data[index]) == CTOOL_TRUE) {
    index++;
  }
  if (index != text.size ||
      (value != 0u && value != 1u && value != 2u && value != 4u &&
       value != 8u && value != 16u)) {
    return CTOOL_FALSE;
  }
  *alignment_out = value;
  return CTOOL_TRUE;
}

static ctool_status_t pp_pragma_pack_push(pp_context_t *context,
                                          const pp_token_t *marker,
                                          ctool_bool named,
                                          ctool_string_t name,
                                          ctool_bool changes_alignment,
                                          ctool_u32 alignment) {
  pp_pack_frame_t *frame;
  ctool_status_t status;
  if (context->pack_depth == PP_PRAGMA_PACK_DEPTH_LIMIT) {
    return pp_pragma_pack_error(context, marker, CTOOL_ERR_LIMIT,
                                "CupidC #pragma pack stack limit exceeded");
  }
  frame = context->free_pack_frames;
  if (frame != (pp_pack_frame_t *)0) {
    context->free_pack_frames = frame->next;
  } else {
    status = ctool_arena_alloc_zero(
        ctool_job_arena(context->job), 1u, (ctool_u32)sizeof(*frame),
        (ctool_u32)sizeof(void *), (void **)&frame);
    if (status != CTOOL_OK) {
      return pp_pragma_pack_error(
          context, marker, status,
          "CupidC #pragma pack stack storage limit exceeded");
    }
  }
  frame->saved_alignment = context->pack_alignment;
  frame->name = name;
  frame->named = named;
  frame->next = context->pack_stack;
  context->pack_stack = frame;
  context->pack_depth++;
  if (changes_alignment == CTOOL_TRUE) {
    context->pack_alignment = alignment;
  }
  return CTOOL_OK;
}

static void pp_pragma_pack_recycle_top(pp_context_t *context) {
  pp_pack_frame_t *frame = context->pack_stack;
  context->pack_alignment = frame->saved_alignment;
  context->pack_stack = frame->next;
  context->pack_depth--;
  frame->next = context->free_pack_frames;
  context->free_pack_frames = frame;
}

static ctool_status_t pp_pragma_pack_pop(pp_context_t *context,
                                         const pp_token_t *marker,
                                         ctool_bool named,
                                         ctool_string_t name) {
  pp_pack_frame_t *target = context->pack_stack;
  if (named == CTOOL_TRUE) {
    while (target != (pp_pack_frame_t *)0 &&
           (target->named == CTOOL_FALSE ||
            pp_string_equal(target->name, name) == CTOOL_FALSE)) {
      target = target->next;
    }
  }
  if (target == (pp_pack_frame_t *)0) {
    return pp_pragma_pack_error(
        context, marker, CTOOL_ERR_INPUT,
        named == CTOOL_TRUE
            ? "CupidC #pragma pack pop name has no matching push"
            : "CupidC #pragma pack pop has no matching push");
  }
  for (;;) {
    ctool_bool reached_target =
        context->pack_stack == target ? CTOOL_TRUE : CTOOL_FALSE;
    pp_pragma_pack_recycle_top(context);
    if (reached_target == CTOOL_TRUE) {
      break;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t pp_handle_pragma_pack(
    pp_context_t *context, const pp_token_t *marker,
    const pp_token_t *tokens, ctool_u32 begin, ctool_u32 end) {
  ctool_u32 inner_begin;
  ctool_u32 inner_end;
  ctool_u32 inner_count;
  ctool_u32 alignment = 0u;
  ctool_string_t no_name = {(const char *)0, 0u};
  if (end - begin < 2u ||
      pp_token_equal_literal(&tokens[begin], "(") == CTOOL_FALSE ||
      pp_token_equal_literal(&tokens[end - 1u], ")") == CTOOL_FALSE) {
    return pp_pragma_pack_error(
        context, marker, CTOOL_ERR_INPUT,
        "CupidC #pragma pack requires a parenthesized action");
  }
  inner_begin = begin + 1u;
  inner_end = end - 1u;
  inner_count = inner_end - inner_begin;
  if (inner_count == 0u) {
    context->pack_alignment = 0u;
    return CTOOL_OK;
  }
  if (inner_count == 1u) {
    if (pp_pragma_pack_alignment(&tokens[inner_begin], &alignment) ==
        CTOOL_TRUE) {
      context->pack_alignment = alignment;
      return CTOOL_OK;
    }
    if (pp_token_equal_literal(&tokens[inner_begin], "push") == CTOOL_TRUE) {
      return pp_pragma_pack_push(context, marker, CTOOL_FALSE, no_name,
                                 CTOOL_FALSE, 0u);
    }
    if (pp_token_equal_literal(&tokens[inner_begin], "pop") == CTOOL_TRUE) {
      return pp_pragma_pack_pop(context, marker, CTOOL_FALSE, no_name);
    }
    return pp_pragma_pack_error(
        context, marker, CTOOL_ERR_INPUT,
        "CupidC #pragma pack action or alignment is invalid");
  }
  if (inner_count == 3u &&
      pp_token_equal_literal(&tokens[inner_begin + 1u], ",") == CTOOL_TRUE) {
    if (pp_token_equal_literal(&tokens[inner_begin], "push") == CTOOL_TRUE) {
      if (pp_pragma_pack_alignment(&tokens[inner_begin + 2u], &alignment) ==
          CTOOL_TRUE) {
        return pp_pragma_pack_push(context, marker, CTOOL_FALSE, no_name,
                                   CTOOL_TRUE, alignment);
      }
      if (pp_token_is_identifier(&tokens[inner_begin + 2u]) == CTOOL_TRUE) {
        return pp_pragma_pack_push(
            context, marker, CTOOL_TRUE, tokens[inner_begin + 2u].spelling,
            CTOOL_FALSE, 0u);
      }
    } else if (pp_token_equal_literal(&tokens[inner_begin], "pop") ==
                   CTOOL_TRUE &&
               pp_token_is_identifier(&tokens[inner_begin + 2u]) ==
                   CTOOL_TRUE) {
      return pp_pragma_pack_pop(context, marker, CTOOL_TRUE,
                                tokens[inner_begin + 2u].spelling);
    }
    return pp_pragma_pack_error(
        context, marker, CTOOL_ERR_INPUT,
        "CupidC #pragma pack push or pop operands are invalid");
  }
  if (inner_count == 5u &&
      pp_token_equal_literal(&tokens[inner_begin], "push") == CTOOL_TRUE &&
      pp_token_equal_literal(&tokens[inner_begin + 1u], ",") == CTOOL_TRUE &&
      pp_token_is_identifier(&tokens[inner_begin + 2u]) == CTOOL_TRUE &&
      pp_token_equal_literal(&tokens[inner_begin + 3u], ",") == CTOOL_TRUE &&
      pp_pragma_pack_alignment(&tokens[inner_begin + 4u], &alignment) ==
          CTOOL_TRUE) {
    return pp_pragma_pack_push(
        context, marker, CTOOL_TRUE, tokens[inner_begin + 2u].spelling,
        CTOOL_TRUE, alignment);
  }
  return pp_pragma_pack_error(
      context, marker, CTOOL_ERR_INPUT,
      "CupidC #pragma pack syntax is invalid");
}

static ctool_status_t pp_handle_pragma(pp_context_t *context,
                                       const pp_token_t *marker,
                                       const pp_token_t *tokens,
                                       ctool_u32 begin,
                                       ctool_u32 end) {
  if (begin < end &&
      pp_token_equal_literal(&tokens[begin], "once") == CTOOL_TRUE) {
    if (end - begin != 1u) {
      return pp_directive_error(context, marker, CTOOL_C_PP_DIAG_DIRECTIVE,
                                "CupidC #pragma once has trailing tokens");
    }
    if (context->source_entry == (pp_source_cache_t *)0) {
      pp_fail(context, CTOOL_ERR_INTERNAL, CTOOL_C_PP_DIAG_DIRECTIVE,
              marker->location.line, marker->location.column,
              "CupidC #pragma once has no active source");
      return CTOOL_ERR_INTERNAL;
    }
    context->source_entry->pragma_once = CTOOL_TRUE;
    return CTOOL_OK;
  }
  if (begin < end &&
      pp_token_equal_literal(&tokens[begin], "pack") == CTOOL_TRUE) {
    return pp_handle_pragma_pack(context, marker, tokens, begin + 1u, end);
  }
  pp_fail(context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PP_DIAG_DIRECTIVE,
          marker->location.line, marker->location.column,
          "CupidC pragma is not implemented yet");
  return CTOOL_ERR_UNSUPPORTED;
}

static ctool_status_t pp_handle_cupid_exe(
    pp_context_t *context, const pp_token_t *marker,
    const pp_token_t *name, const pp_token_t *tokens,
    ctool_u32 begin, ctool_u32 directive_end,
    ctool_u32 expansion_end) {
  pp_token_t exe_token;
  ctool_status_t status;
  if (context->request->mode != CTOOL_C_PP_MODE_CUPID) {
    pp_fail(context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PP_DIAG_CUPID_EXE,
            marker->location.line, marker->location.column,
            "CupidC #exe requires Cupid language mode");
    return CTOOL_ERR_UNSUPPORTED;
  }
  if (begin == directive_end ||
      pp_token_equal_literal(&tokens[begin], "{") == CTOOL_FALSE) {
    pp_fail(context, CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_CUPID_EXE,
            marker->location.line, marker->location.column,
            "CupidC #exe requires a raw opening brace");
    return CTOOL_ERR_INPUT;
  }
  exe_token = *name;
  exe_token.kind = CTOOL_C_PP_TOKEN_CUPID_EXE;
  exe_token.location = marker->location;
  exe_token.pack_alignment = context->pack_alignment;
  exe_token.leading_space = CTOOL_FALSE;
  exe_token.at_line_start = CTOOL_FALSE;
  exe_token.header_name = CTOOL_FALSE;
  status = pp_output_append(context, &exe_token);
  if (status != CTOOL_OK) {
    return status;
  }
  return pp_emit_expanded_range(context, tokens + begin,
                                expansion_end - begin);
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
  status =
      pp_reject_reserved_variadic_identifier(context, &tokens[begin]);
  if (status != CTOOL_OK) {
    return status;
  }
  defined = pp_macro_is_defined(context, tokens[begin].spelling);
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

typedef struct {
  ctool_u64 bits;
  ctool_bool is_unsigned;
} pp_if_value_t;

typedef struct {
  pp_context_t *context;
  const pp_token_t *marker;
  pp_expand_node_t *cursor;
  ctool_u32 depth;
} pp_if_parser_t;

static ctool_status_t pp_if_expression_fail(
    pp_context_t *context, const pp_token_t *marker,
    const pp_token_t *token, ctool_status_t status,
    const char *message) {
  const pp_token_t *location =
      token != (const pp_token_t *)0 ? token : marker;
  pp_fail(context, status, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
          location->location.line, location->location.column, message);
  return status;
}

static ctool_status_t pp_if_parser_fail(
    pp_if_parser_t *parser, const pp_token_t *token,
    ctool_status_t status, const char *message) {
  return pp_if_expression_fail(parser->context, parser->marker, token,
                               status, message);
}

static ctool_bool pp_if_value_truth(pp_if_value_t value) {
  return value.bits != 0ull ? CTOOL_TRUE : CTOOL_FALSE;
}

static pp_if_value_t pp_if_signed_value(ctool_u64 bits) {
  pp_if_value_t value;
  value.bits = bits;
  value.is_unsigned = CTOOL_FALSE;
  return value;
}

static pp_if_value_t pp_if_unsigned_value(ctool_u64 bits) {
  pp_if_value_t value;
  value.bits = bits;
  value.is_unsigned = CTOOL_TRUE;
  return value;
}

static ctool_bool pp_if_is_negative(pp_if_value_t value) {
  return value.is_unsigned == CTOOL_FALSE &&
                 (value.bits & PP_IF_SIGN_BIT) != 0ull
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_u64 pp_if_signed_magnitude(ctool_u64 bits) {
  return (bits & PP_IF_SIGN_BIT) != 0ull ? (~bits) + 1ull : bits;
}

static ctool_bool pp_if_signed_less(ctool_u64 left, ctool_u64 right) {
  ctool_bool left_negative =
      (left & PP_IF_SIGN_BIT) != 0ull ? CTOOL_TRUE : CTOOL_FALSE;
  ctool_bool right_negative =
      (right & PP_IF_SIGN_BIT) != 0ull ? CTOOL_TRUE : CTOOL_FALSE;
  if (left_negative != right_negative) {
    return left_negative;
  }
  return left < right ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_i32 pp_integer_digit_value(char character) {
  if (character >= '0' && character <= '9') {
    return (ctool_i32)(character - '0');
  }
  if (character >= 'a' && character <= 'f') {
    return (ctool_i32)(character - 'a') + 10;
  }
  if (character >= 'A' && character <= 'F') {
    return (ctool_i32)(character - 'A') + 10;
  }
  return -1;
}

static ctool_bool pp_integer_is_u_suffix(char character) {
  return character == 'u' || character == 'U' ? CTOOL_TRUE
                                               : CTOOL_FALSE;
}

static ctool_bool pp_integer_is_l_suffix(char character) {
  return character == 'l' || character == 'L' ? CTOOL_TRUE
                                               : CTOOL_FALSE;
}

static ctool_status_t pp_if_parse_integer(
    pp_if_parser_t *parser, const pp_token_t *token,
    pp_if_value_t *value_out) {
  ctool_string_t text = token->spelling;
  ctool_u32 index = 0u;
  ctool_u32 base = 10u;
  ctool_bool have_digit = CTOOL_FALSE;
  ctool_bool unsigned_suffix = CTOOL_FALSE;
  ctool_u64 value = 0ull;
  if (text.size == 0u) {
    return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                             "CupidC conditional integer is empty");
  }
  if (text.data[0] == '0') {
    have_digit = CTOOL_TRUE;
    index = 1u;
    base = 8u;
    if (index < text.size &&
        (text.data[index] == 'x' || text.data[index] == 'X')) {
      base = 16u;
      have_digit = CTOOL_FALSE;
      index++;
    } else if (index < text.size &&
               (text.data[index] == 'b' || text.data[index] == 'B')) {
      if (parser->context->request->gnu_extensions == CTOOL_FALSE) {
        return pp_if_parser_fail(
            parser, token, CTOOL_ERR_INPUT,
            "CupidC binary conditional constants require GNU mode");
      }
      base = 2u;
      have_digit = CTOOL_FALSE;
      index++;
    }
  }
  while (index < text.size) {
    ctool_i32 digit = pp_integer_digit_value(text.data[index]);
    if (digit < 0 || (ctool_u32)digit >= base) {
      break;
    }
    have_digit = CTOOL_TRUE;
    if (value > (PP_IF_U64_MAX - (ctool_u64)(ctool_u32)digit) /
                    (ctool_u64)base) {
      return pp_if_parser_fail(parser, token, CTOOL_ERR_OVERFLOW,
                               "CupidC conditional integer overflows uintmax");
    }
    value = value * (ctool_u64)base + (ctool_u64)(ctool_u32)digit;
    index++;
  }
  if (have_digit == CTOOL_FALSE) {
    return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                             "CupidC conditional integer has no digits");
  }
  if (index < text.size &&
      pp_integer_is_u_suffix(text.data[index]) == CTOOL_TRUE) {
    unsigned_suffix = CTOOL_TRUE;
    index++;
  }
  if (index < text.size &&
      pp_integer_is_l_suffix(text.data[index]) == CTOOL_TRUE) {
    char first = text.data[index];
    index++;
    if (index < text.size && text.data[index] == first) {
      index++;
    }
  }
  if (unsigned_suffix == CTOOL_FALSE && index < text.size &&
      pp_integer_is_u_suffix(text.data[index]) == CTOOL_TRUE) {
    unsigned_suffix = CTOOL_TRUE;
    index++;
  }
  if (index != text.size) {
    return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                             "CupidC conditional integer suffix is invalid");
  }
  if (unsigned_suffix == CTOOL_TRUE ||
      (base != 10u && value > PP_IF_SIGNED_MAX)) {
    *value_out = pp_if_unsigned_value(value);
    return CTOOL_OK;
  }
  if (value > PP_IF_SIGNED_MAX) {
    if (parser->context->request->gnu_extensions == CTOOL_TRUE) {
      *value_out = pp_if_unsigned_value(value);
      return CTOOL_OK;
    }
    return pp_if_parser_fail(
        parser, token, CTOOL_ERR_OVERFLOW,
        "CupidC decimal conditional integer overflows intmax");
  }
  *value_out = pp_if_signed_value(value);
  return CTOOL_OK;
}

static ctool_status_t pp_if_parse_escape(
    pp_if_parser_t *parser, const pp_token_t *token,
    ctool_u32 end, ctool_u32 *index, ctool_u32 *code_out) {
  ctool_string_t text = token->spelling;
  char character;
  if (*index >= end) {
    return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                             "CupidC character escape is incomplete");
  }
  character = text.data[*index];
  (*index)++;
  if (character != '\\') {
    *code_out = (ctool_u32)(ctool_u8)character;
    return CTOOL_OK;
  }
  if (*index >= end) {
    return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                             "CupidC character escape is incomplete");
  }
  character = text.data[*index];
  (*index)++;
  if (character == '\'' || character == '"' || character == '?' ||
      character == '\\') {
    *code_out = (ctool_u32)(ctool_u8)character;
    return CTOOL_OK;
  }
  if (character == 'a') {
    *code_out = 7u;
    return CTOOL_OK;
  }
  if (character == 'b') {
    *code_out = 8u;
    return CTOOL_OK;
  }
  if (character == 'f') {
    *code_out = 12u;
    return CTOOL_OK;
  }
  if (character == 'n') {
    *code_out = 10u;
    return CTOOL_OK;
  }
  if (character == 'r') {
    *code_out = 13u;
    return CTOOL_OK;
  }
  if (character == 't') {
    *code_out = 9u;
    return CTOOL_OK;
  }
  if (character == 'v') {
    *code_out = 11u;
    return CTOOL_OK;
  }
  if (character >= '0' && character <= '7') {
    ctool_u32 value = (ctool_u32)(character - '0');
    ctool_u32 count = 1u;
    while (*index < end && count < 3u &&
           text.data[*index] >= '0' && text.data[*index] <= '7') {
      value = value * 8u + (ctool_u32)(text.data[*index] - '0');
      (*index)++;
      count++;
    }
    *code_out = value;
    return CTOOL_OK;
  }
  if (character == 'x') {
    ctool_u32 value = 0u;
    ctool_bool have_digit = CTOOL_FALSE;
    while (*index < end) {
      ctool_i32 digit = pp_integer_digit_value(text.data[*index]);
      if (digit < 0) {
        break;
      }
      have_digit = CTOOL_TRUE;
      if (value > (0xffffffffu - (ctool_u32)digit) / 16u) {
        return pp_if_parser_fail(
            parser, token, CTOOL_ERR_OVERFLOW,
            "CupidC hexadecimal character escape overflows");
      }
      value = value * 16u + (ctool_u32)digit;
      (*index)++;
    }
    if (have_digit == CTOOL_FALSE) {
      return pp_if_parser_fail(
          parser, token, CTOOL_ERR_INPUT,
          "CupidC hexadecimal character escape has no digits");
    }
    *code_out = value;
    return CTOOL_OK;
  }
  if (character == 'u' || character == 'U') {
    ctool_u32 required = character == 'u' ? 4u : 8u;
    ctool_u32 value = 0u;
    ctool_u32 count;
    if (end - *index < required) {
      return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                               "CupidC universal character escape is short");
    }
    for (count = 0u; count < required; count++) {
      ctool_i32 digit = pp_integer_digit_value(text.data[*index]);
      if (digit < 0) {
        return pp_if_parser_fail(
            parser, token, CTOOL_ERR_INPUT,
            "CupidC universal character escape is invalid");
      }
      value = value * 16u + (ctool_u32)digit;
      (*index)++;
    }
    if (value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu) ||
        (value < 0x00a0u && value != 0x0024u && value != 0x0040u &&
         value != 0x0060u)) {
      return pp_if_parser_fail(
          parser, token, CTOOL_ERR_INPUT,
          "CupidC universal character value is invalid");
    }
    *code_out = value;
    return CTOOL_OK;
  }
  if (character == 'e' &&
      parser->context->request->gnu_extensions == CTOOL_TRUE) {
    *code_out = 27u;
    return CTOOL_OK;
  }
  return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                           "CupidC character escape is invalid");
}

typedef enum {
  PP_IF_CHARACTER_ORDINARY = 0,
  PP_IF_CHARACTER_WIDE,
  PP_IF_CHARACTER_UTF16,
  PP_IF_CHARACTER_UTF32
} pp_if_character_kind_t;

static ctool_status_t pp_if_parse_character(
    pp_if_parser_t *parser, const pp_token_t *token,
    pp_if_value_t *value_out) {
  ctool_string_t text = token->spelling;
  ctool_u32 prefix = 0u;
  pp_if_character_kind_t kind = PP_IF_CHARACTER_ORDINARY;
  ctool_u32 index;
  ctool_u32 end;
  ctool_u32 count = 0u;
  ctool_u64 value = 0ull;
  if (text.size < 3u) {
    return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                             "CupidC character constant is empty");
  }
  if (text.data[0] == 'L') {
    prefix = 1u;
    kind = PP_IF_CHARACTER_WIDE;
  } else if (text.data[0] == 'u') {
    prefix = 1u;
    kind = PP_IF_CHARACTER_UTF16;
  } else if (text.data[0] == 'U') {
    prefix = 1u;
    kind = PP_IF_CHARACTER_UTF32;
  }
  if (text.data[prefix] != '\'' || text.data[text.size - 1u] != '\'') {
    return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                             "CupidC character constant is malformed");
  }
  index = prefix + 1u;
  end = text.size - 1u;
  while (index < end) {
    ctool_u32 code = 0u;
    ctool_status_t status =
        pp_if_parse_escape(parser, token, end, &index, &code);
    if (status != CTOOL_OK) {
      return status;
    }
    count++;
    if (kind != PP_IF_CHARACTER_ORDINARY && count != 1u) {
      return pp_if_parser_fail(
          parser, token, CTOOL_ERR_INPUT,
          "CupidC wide character constant has multiple characters");
    }
    if (kind == PP_IF_CHARACTER_ORDINARY) {
      if (code > 0xffu || count > 4u) {
        return pp_if_parser_fail(
            parser, token, CTOOL_ERR_INPUT,
            "CupidC ordinary character constant is out of range");
      }
      value = (value << 8u) | (ctool_u64)code;
    } else {
      value = (ctool_u64)code;
    }
  }
  if (count == 0u) {
    return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                             "CupidC character constant is empty");
  }
  if (kind == PP_IF_CHARACTER_UTF16) {
    if (value > 0xffffull) {
      return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                               "CupidC char16 constant is out of range");
    }
    *value_out = pp_if_unsigned_value(value);
  } else if (kind == PP_IF_CHARACTER_UTF32) {
    if (value > 0xffffffffull) {
      return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                               "CupidC char32 constant is out of range");
    }
    *value_out = pp_if_unsigned_value(value);
  } else if (kind == PP_IF_CHARACTER_WIDE) {
    if (value > 0xffffffffull) {
      return pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                               "CupidC wide character is out of range");
    }
    if ((value & 0x80000000ull) != 0ull) {
      value |= 0xffffffff00000000ull;
    }
    *value_out = pp_if_signed_value(value);
  } else {
    if (count == 1u && (value & 0x80ull) != 0ull) {
      value |= PP_IF_U64_MAX ^ 0xffull;
    } else if (count > 1u && (value & 0x80000000ull) != 0ull) {
      value |= PP_IF_U64_MAX ^ 0xffffffffull;
    }
    *value_out = pp_if_signed_value(value);
  }
  return CTOOL_OK;
}

static ctool_u32 pp_if_binary_precedence(const pp_token_t *token) {
  if (pp_token_equal_literal(token, "||") == CTOOL_TRUE) {
    return 1u;
  }
  if (pp_token_equal_literal(token, "&&") == CTOOL_TRUE) {
    return 2u;
  }
  if (pp_token_equal_literal(token, "|") == CTOOL_TRUE) {
    return 3u;
  }
  if (pp_token_equal_literal(token, "^") == CTOOL_TRUE) {
    return 4u;
  }
  if (pp_token_equal_literal(token, "&") == CTOOL_TRUE) {
    return 5u;
  }
  if (pp_token_equal_literal(token, "==") == CTOOL_TRUE ||
      pp_token_equal_literal(token, "!=") == CTOOL_TRUE) {
    return 6u;
  }
  if (pp_token_equal_literal(token, "<") == CTOOL_TRUE ||
      pp_token_equal_literal(token, "<=") == CTOOL_TRUE ||
      pp_token_equal_literal(token, ">") == CTOOL_TRUE ||
      pp_token_equal_literal(token, ">=") == CTOOL_TRUE) {
    return 7u;
  }
  if (pp_token_equal_literal(token, "<<") == CTOOL_TRUE ||
      pp_token_equal_literal(token, ">>") == CTOOL_TRUE) {
    return 8u;
  }
  if (pp_token_equal_literal(token, "+") == CTOOL_TRUE ||
      pp_token_equal_literal(token, "-") == CTOOL_TRUE) {
    return 9u;
  }
  if (pp_token_equal_literal(token, "*") == CTOOL_TRUE ||
      pp_token_equal_literal(token, "/") == CTOOL_TRUE ||
      pp_token_equal_literal(token, "%") == CTOOL_TRUE) {
    return 10u;
  }
  return 0u;
}

static ctool_status_t pp_if_parse_expression(
    pp_if_parser_t *parser, ctool_u32 minimum_precedence,
    ctool_bool evaluate, ctool_bool allow_comma,
    pp_if_value_t *value_out);

static ctool_status_t pp_if_parse_prefix(
    pp_if_parser_t *parser, ctool_bool evaluate,
    pp_if_value_t *value_out) {
  pp_expand_node_t *node = parser->cursor;
  const pp_token_t *token;
  ctool_status_t status;
  if (node == (pp_expand_node_t *)0) {
    return pp_if_parser_fail(
        parser, (const pp_token_t *)0, CTOOL_ERR_INPUT,
        "CupidC conditional expression needs an operand");
  }
  token = &node->token;
  if (pp_token_equal_literal(token, "+") == CTOOL_TRUE ||
      pp_token_equal_literal(token, "-") == CTOOL_TRUE ||
      pp_token_equal_literal(token, "!") == CTOOL_TRUE ||
      pp_token_equal_literal(token, "~") == CTOOL_TRUE) {
    pp_if_value_t operand = pp_if_signed_value(0ull);
    if (parser->depth == PP_CONDITIONAL_EXPRESSION_DEPTH_LIMIT) {
      return pp_if_parser_fail(
          parser, token, CTOOL_ERR_LIMIT,
          "CupidC conditional expression nesting is too deep");
    }
    parser->depth++;
    parser->cursor = node->next;
    status = pp_if_parse_prefix(parser, evaluate, &operand);
    if (status == CTOOL_OK) {
      if (pp_token_equal_literal(token, "!") == CTOOL_TRUE) {
        *value_out = pp_if_signed_value(
            evaluate == CTOOL_TRUE &&
                    pp_if_value_truth(operand) == CTOOL_FALSE
                ? 1ull
                : 0ull);
      } else if (pp_token_equal_literal(token, "~") == CTOOL_TRUE) {
        *value_out = operand;
        value_out->bits = evaluate == CTOOL_TRUE ? ~operand.bits : 0ull;
      } else if (pp_token_equal_literal(token, "-") == CTOOL_TRUE) {
        *value_out = operand;
        if (evaluate == CTOOL_FALSE) {
          value_out->bits = 0ull;
        } else if (operand.is_unsigned == CTOOL_FALSE &&
                   operand.bits == PP_IF_SIGN_BIT) {
          status = pp_if_parser_fail(
              parser, token, CTOOL_ERR_OVERFLOW,
              "CupidC conditional unary minus overflows intmax");
        } else {
          value_out->bits = 0ull - operand.bits;
        }
      } else {
        *value_out = operand;
        if (evaluate == CTOOL_FALSE) {
          value_out->bits = 0ull;
        }
      }
    }
    parser->depth--;
    return status;
  }
  if (pp_token_equal_literal(token, "(") == CTOOL_TRUE) {
    pp_expand_node_t *close;
    if (parser->depth == PP_CONDITIONAL_EXPRESSION_DEPTH_LIMIT) {
      return pp_if_parser_fail(
          parser, token, CTOOL_ERR_LIMIT,
          "CupidC conditional expression nesting is too deep");
    }
    parser->depth++;
    parser->cursor = node->next;
    status = pp_if_parse_expression(parser, 0u, evaluate, CTOOL_TRUE,
                                    value_out);
    close = parser->cursor;
    if (status == CTOOL_OK &&
        (close == (pp_expand_node_t *)0 ||
         pp_token_equal_literal(&close->token, ")") == CTOOL_FALSE)) {
      status = pp_if_parser_fail(
          parser, token, CTOOL_ERR_INPUT,
          "CupidC conditional parenthesis is not closed");
    }
    if (status == CTOOL_OK && close != (pp_expand_node_t *)0) {
      parser->cursor = close->next;
    }
    parser->depth--;
    return status;
  }
  parser->cursor = node->next;
  if (token->kind == CTOOL_C_PP_TOKEN_NUMBER) {
    status = pp_if_parse_integer(parser, token, value_out);
  } else if (token->kind == CTOOL_C_PP_TOKEN_CHARACTER) {
    status = pp_if_parse_character(parser, token, value_out);
  } else if (token->kind == CTOOL_C_PP_TOKEN_IDENTIFIER) {
    if (pp_token_equal_literal(token, "defined") == CTOOL_TRUE) {
      status = pp_if_parser_fail(
          parser, token, CTOOL_ERR_INPUT,
          "CupidC macro expansion produced a defined operator");
    } else {
      *value_out = pp_if_signed_value(0ull);
      status = CTOOL_OK;
    }
  } else {
    status = pp_if_parser_fail(parser, token, CTOOL_ERR_INPUT,
                               "CupidC conditional operand is not an integer");
  }
  if (status == CTOOL_OK && evaluate == CTOOL_FALSE) {
    value_out->bits = 0ull;
  }
  return status;
}

static ctool_status_t pp_if_apply_binary(
    pp_if_parser_t *parser, const pp_token_t *operator_token,
    pp_if_value_t left, pp_if_value_t right, ctool_bool evaluate,
    pp_if_value_t *value_out) {
  ctool_bool common_unsigned =
      left.is_unsigned == CTOOL_TRUE || right.is_unsigned == CTOOL_TRUE
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  ctool_bool comparison =
      pp_token_equal_literal(operator_token, "<") == CTOOL_TRUE ||
              pp_token_equal_literal(operator_token, "<=") == CTOOL_TRUE ||
              pp_token_equal_literal(operator_token, ">") == CTOOL_TRUE ||
              pp_token_equal_literal(operator_token, ">=") == CTOOL_TRUE ||
              pp_token_equal_literal(operator_token, "==") == CTOOL_TRUE ||
              pp_token_equal_literal(operator_token, "!=") == CTOOL_TRUE ||
              pp_token_equal_literal(operator_token, "&&") == CTOOL_TRUE ||
              pp_token_equal_literal(operator_token, "||") == CTOOL_TRUE
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  if (comparison == CTOOL_TRUE) {
    *value_out = pp_if_signed_value(0ull);
  } else if (pp_token_equal_literal(operator_token, "<<") == CTOOL_TRUE ||
             pp_token_equal_literal(operator_token, ">>") == CTOOL_TRUE) {
    *value_out = left;
    value_out->bits = 0ull;
  } else {
    value_out->bits = 0ull;
    value_out->is_unsigned = common_unsigned;
  }
  if (evaluate == CTOOL_FALSE) {
    return CTOOL_OK;
  }
  if (pp_token_equal_literal(operator_token, "&&") == CTOOL_TRUE) {
    value_out->bits =
        pp_if_value_truth(left) == CTOOL_TRUE &&
                pp_if_value_truth(right) == CTOOL_TRUE
            ? 1ull
            : 0ull;
    return CTOOL_OK;
  }
  if (pp_token_equal_literal(operator_token, "||") == CTOOL_TRUE) {
    value_out->bits =
        pp_if_value_truth(left) == CTOOL_TRUE ||
                pp_if_value_truth(right) == CTOOL_TRUE
            ? 1ull
            : 0ull;
    return CTOOL_OK;
  }
  if (pp_token_equal_literal(operator_token, "==") == CTOOL_TRUE ||
      pp_token_equal_literal(operator_token, "!=") == CTOOL_TRUE) {
    ctool_bool equal = left.bits == right.bits ? CTOOL_TRUE : CTOOL_FALSE;
    value_out->bits =
        pp_token_equal_literal(operator_token, "==") == CTOOL_TRUE
            ? (equal == CTOOL_TRUE ? 1ull : 0ull)
            : (equal == CTOOL_TRUE ? 0ull : 1ull);
    return CTOOL_OK;
  }
  if (pp_token_equal_literal(operator_token, "<") == CTOOL_TRUE ||
      pp_token_equal_literal(operator_token, "<=") == CTOOL_TRUE ||
      pp_token_equal_literal(operator_token, ">") == CTOOL_TRUE ||
      pp_token_equal_literal(operator_token, ">=") == CTOOL_TRUE) {
    ctool_bool less = common_unsigned == CTOOL_TRUE
                          ? (left.bits < right.bits ? CTOOL_TRUE
                                                   : CTOOL_FALSE)
                          : pp_if_signed_less(left.bits, right.bits);
    ctool_bool equal = left.bits == right.bits ? CTOOL_TRUE : CTOOL_FALSE;
    if (pp_token_equal_literal(operator_token, "<") == CTOOL_TRUE) {
      value_out->bits = less == CTOOL_TRUE ? 1ull : 0ull;
    } else if (pp_token_equal_literal(operator_token, "<=") == CTOOL_TRUE) {
      value_out->bits = less == CTOOL_TRUE || equal == CTOOL_TRUE ? 1ull
                                                                  : 0ull;
    } else if (pp_token_equal_literal(operator_token, ">") == CTOOL_TRUE) {
      value_out->bits = less == CTOOL_FALSE && equal == CTOOL_FALSE ? 1ull
                                                                    : 0ull;
    } else {
      value_out->bits = less == CTOOL_FALSE ? 1ull : 0ull;
    }
    return CTOOL_OK;
  }
  if (pp_token_equal_literal(operator_token, "&") == CTOOL_TRUE) {
    value_out->bits = left.bits & right.bits;
    return CTOOL_OK;
  }
  if (pp_token_equal_literal(operator_token, "^") == CTOOL_TRUE) {
    value_out->bits = left.bits ^ right.bits;
    return CTOOL_OK;
  }
  if (pp_token_equal_literal(operator_token, "|") == CTOOL_TRUE) {
    value_out->bits = left.bits | right.bits;
    return CTOOL_OK;
  }
  if (pp_token_equal_literal(operator_token, "<<") == CTOOL_TRUE ||
      pp_token_equal_literal(operator_token, ">>") == CTOOL_TRUE) {
    ctool_u32 count;
    if (pp_if_is_negative(right) == CTOOL_TRUE || right.bits >= 64ull) {
      return pp_if_parser_fail(parser, operator_token, CTOOL_ERR_INPUT,
                               "CupidC conditional shift count is invalid");
    }
    count = (ctool_u32)right.bits;
    if (pp_token_equal_literal(operator_token, "<<") == CTOOL_TRUE) {
      if (left.is_unsigned == CTOOL_FALSE &&
          ((left.bits & PP_IF_SIGN_BIT) != 0ull ||
           (count != 0u && left.bits > (PP_IF_SIGNED_MAX >> count)))) {
        return pp_if_parser_fail(parser, operator_token,
                                 CTOOL_ERR_OVERFLOW,
                                 "CupidC signed conditional left shift overflows");
      }
      value_out->bits = left.bits << count;
    } else if (left.is_unsigned == CTOOL_TRUE ||
               (left.bits & PP_IF_SIGN_BIT) == 0ull || count == 0u) {
      value_out->bits = left.bits >> count;
    } else {
      value_out->bits =
          (left.bits >> count) | (PP_IF_U64_MAX << (64u - count));
    }
    return CTOOL_OK;
  }
  if (pp_token_equal_literal(operator_token, "+") == CTOOL_TRUE) {
    ctool_u64 result = left.bits + right.bits;
    if (common_unsigned == CTOOL_FALSE &&
        ((left.bits ^ result) & (right.bits ^ result) & PP_IF_SIGN_BIT) !=
            0ull) {
      return pp_if_parser_fail(parser, operator_token, CTOOL_ERR_OVERFLOW,
                               "CupidC signed conditional addition overflows");
    }
    value_out->bits = result;
    return CTOOL_OK;
  }
  if (pp_token_equal_literal(operator_token, "-") == CTOOL_TRUE) {
    ctool_u64 result = left.bits - right.bits;
    if (common_unsigned == CTOOL_FALSE &&
        ((left.bits ^ right.bits) & (left.bits ^ result) & PP_IF_SIGN_BIT) !=
            0ull) {
      return pp_if_parser_fail(parser, operator_token, CTOOL_ERR_OVERFLOW,
                               "CupidC signed conditional subtraction overflows");
    }
    value_out->bits = result;
    return CTOOL_OK;
  }
  if (pp_token_equal_literal(operator_token, "*") == CTOOL_TRUE) {
    if (common_unsigned == CTOOL_TRUE) {
      value_out->bits = left.bits * right.bits;
    } else {
      ctool_bool negative =
          ((left.bits ^ right.bits) & PP_IF_SIGN_BIT) != 0ull
              ? CTOOL_TRUE
              : CTOOL_FALSE;
      ctool_u64 left_magnitude = pp_if_signed_magnitude(left.bits);
      ctool_u64 right_magnitude = pp_if_signed_magnitude(right.bits);
      ctool_u64 limit =
          negative == CTOOL_TRUE ? PP_IF_SIGN_BIT : PP_IF_SIGNED_MAX;
      ctool_u64 magnitude;
      if (left_magnitude != 0ull &&
          right_magnitude > limit / left_magnitude) {
        return pp_if_parser_fail(
            parser, operator_token, CTOOL_ERR_OVERFLOW,
            "CupidC signed conditional multiplication overflows");
      }
      magnitude = left_magnitude * right_magnitude;
      value_out->bits = negative == CTOOL_TRUE && magnitude != 0ull
                            ? (~magnitude) + 1ull
                            : magnitude;
    }
    return CTOOL_OK;
  }
  if (pp_token_equal_literal(operator_token, "/") == CTOOL_TRUE ||
      pp_token_equal_literal(operator_token, "%") == CTOOL_TRUE) {
    if (right.bits == 0ull) {
      return pp_if_parser_fail(parser, operator_token, CTOOL_ERR_INPUT,
                               "CupidC conditional division by zero");
    }
    if (common_unsigned == CTOOL_TRUE) {
      value_out->bits =
          pp_token_equal_literal(operator_token, "/") == CTOOL_TRUE
              ? left.bits / right.bits
              : left.bits % right.bits;
    } else {
      ctool_bool left_negative =
          (left.bits & PP_IF_SIGN_BIT) != 0ull ? CTOOL_TRUE : CTOOL_FALSE;
      ctool_bool right_negative =
          (right.bits & PP_IF_SIGN_BIT) != 0ull ? CTOOL_TRUE : CTOOL_FALSE;
      ctool_u64 left_magnitude = pp_if_signed_magnitude(left.bits);
      ctool_u64 right_magnitude = pp_if_signed_magnitude(right.bits);
      ctool_u64 magnitude;
      ctool_bool negative;
      if (left.bits == PP_IF_SIGN_BIT && right.bits == PP_IF_U64_MAX) {
        return pp_if_parser_fail(
            parser, operator_token, CTOOL_ERR_OVERFLOW,
            "CupidC signed conditional division overflows");
      }
      if (pp_token_equal_literal(operator_token, "/") == CTOOL_TRUE) {
        magnitude = left_magnitude / right_magnitude;
        negative = left_negative != right_negative ? CTOOL_TRUE
                                                   : CTOOL_FALSE;
      } else {
        magnitude = left_magnitude % right_magnitude;
        negative = left_negative;
      }
      value_out->bits = negative == CTOOL_TRUE && magnitude != 0ull
                            ? (~magnitude) + 1ull
                            : magnitude;
    }
    return CTOOL_OK;
  }
  return pp_if_parser_fail(parser, operator_token, CTOOL_ERR_INTERNAL,
                           "CupidC conditional operator is not implemented");
}

static ctool_status_t pp_if_parse_expression(
    pp_if_parser_t *parser, ctool_u32 minimum_precedence,
    ctool_bool evaluate, ctool_bool allow_comma,
    pp_if_value_t *value_out) {
  pp_if_value_t left = pp_if_signed_value(0ull);
  ctool_status_t status;
  status = pp_if_parse_prefix(parser, evaluate, &left);
  while (status == CTOOL_OK && parser->cursor != (pp_expand_node_t *)0) {
    pp_expand_node_t *operator_node = parser->cursor;
    ctool_u32 precedence =
        pp_if_binary_precedence(&operator_node->token);
    pp_if_value_t right = pp_if_signed_value(0ull);
    ctool_bool right_evaluate = evaluate;
    if (precedence == 0u || precedence < minimum_precedence) {
      break;
    }
    parser->cursor = operator_node->next;
    if (evaluate == CTOOL_TRUE &&
        pp_token_equal_literal(&operator_node->token, "&&") == CTOOL_TRUE &&
        pp_if_value_truth(left) == CTOOL_FALSE) {
      right_evaluate = CTOOL_FALSE;
    } else if (evaluate == CTOOL_TRUE &&
               pp_token_equal_literal(&operator_node->token, "||") ==
                   CTOOL_TRUE &&
               pp_if_value_truth(left) == CTOOL_TRUE) {
      right_evaluate = CTOOL_FALSE;
    }
    status = pp_if_parse_expression(parser, precedence + 1u,
                                    right_evaluate, allow_comma, &right);
    if (status == CTOOL_OK) {
      status = pp_if_apply_binary(parser, &operator_node->token, left, right,
                                  evaluate, &left);
    }
  }
  if (status == CTOOL_OK && minimum_precedence == 0u &&
      parser->cursor != (pp_expand_node_t *)0 &&
      pp_token_equal_literal(&parser->cursor->token, "?") == CTOOL_TRUE) {
    pp_expand_node_t *question = parser->cursor;
    pp_if_value_t when_true = pp_if_signed_value(0ull);
    pp_if_value_t when_false = pp_if_signed_value(0ull);
    pp_expand_node_t *colon;
    ctool_bool condition = pp_if_value_truth(left);
    if (parser->depth == PP_CONDITIONAL_EXPRESSION_DEPTH_LIMIT) {
      return pp_if_parser_fail(
          parser, &question->token, CTOOL_ERR_LIMIT,
          "CupidC conditional expression nesting is too deep");
    }
    parser->depth++;
    parser->cursor = question->next;
    status = pp_if_parse_expression(
        parser, 0u,
        evaluate == CTOOL_TRUE && condition == CTOOL_TRUE ? CTOOL_TRUE
                                                          : CTOOL_FALSE,
        CTOOL_TRUE, &when_true);
    colon = parser->cursor;
    if (status == CTOOL_OK &&
        (colon == (pp_expand_node_t *)0 ||
         pp_token_equal_literal(&colon->token, ":") == CTOOL_FALSE)) {
      status = pp_if_parser_fail(
          parser, &question->token, CTOOL_ERR_INPUT,
          "CupidC conditional operator requires a colon");
    }
    if (status == CTOOL_OK && colon != (pp_expand_node_t *)0) {
      parser->cursor = colon->next;
      status = pp_if_parse_expression(
          parser, 0u,
          evaluate == CTOOL_TRUE && condition == CTOOL_FALSE ? CTOOL_TRUE
                                                             : CTOOL_FALSE,
          CTOOL_FALSE, &when_false);
    }
    if (status == CTOOL_OK) {
      ctool_bool result_unsigned =
          when_true.is_unsigned == CTOOL_TRUE ||
                  when_false.is_unsigned == CTOOL_TRUE
              ? CTOOL_TRUE
              : CTOOL_FALSE;
      *value_out = condition == CTOOL_TRUE ? when_true : when_false;
      value_out->is_unsigned = result_unsigned;
      if (evaluate == CTOOL_FALSE) {
        value_out->bits = 0ull;
      }
      left = *value_out;
    }
    parser->depth--;
  }
  while (status == CTOOL_OK && minimum_precedence == 0u &&
         allow_comma == CTOOL_TRUE &&
         parser->cursor != (pp_expand_node_t *)0 &&
         pp_token_equal_literal(&parser->cursor->token, ",") == CTOOL_TRUE) {
    pp_expand_node_t *comma = parser->cursor;
    pp_if_value_t right = pp_if_signed_value(0ull);
    if (evaluate == CTOOL_TRUE) {
      status = pp_if_parser_fail(
          parser, &comma->token, CTOOL_ERR_INPUT,
          "CupidC conditional comma operator is evaluated");
      break;
    }
    parser->cursor = comma->next;
    status = pp_if_parse_expression(parser, 0u, CTOOL_FALSE, CTOOL_FALSE,
                                    &right);
    if (status == CTOOL_OK) {
      left = right;
      left.bits = 0ull;
    }
  }
  if (status == CTOOL_OK) {
    *value_out = left;
  }
  return status;
}

static ctool_status_t pp_expand_condition_tokens(
    pp_context_t *context, const pp_token_t *tokens,
    ctool_u32 begin, ctool_u32 end,
    pp_expand_list_t *expanded_out) {
  pp_expand_list_t work;
  ctool_u32 index;
  ctool_status_t status;
  pp_expand_list_zero(&work);
  for (index = begin; index < end; index++) {
    status = pp_expand_list_append_copy(
        context, &work, &tokens[index], (const pp_hide_t *)0);
    if (status != CTOOL_OK) {
      return status;
    }
    if (tokens[index].kind == CTOOL_C_PP_TOKEN_IDENTIFIER &&
        pp_token_equal_literal(&tokens[index], "defined") == CTOOL_TRUE) {
      work.tail->condition_defined = CTOOL_TRUE;
    }
  }
  pp_expand_list_zero(expanded_out);
  return pp_expand_work(context, &work, expanded_out);
}

static ctool_status_t pp_evaluate_condition(
    pp_context_t *context, const pp_token_t *marker,
    const pp_token_t *tokens, ctool_u32 begin, ctool_u32 end,
    ctool_bool *value_out) {
  ctool_arena_t *arena = ctool_job_arena(context->job);
  ctool_arena_mark_t mark;
  ctool_u32 saved_expansion_node_count;
  ctool_bool saved_condition_expression;
  pp_expand_list_t expanded;
  pp_if_parser_t parser;
  pp_if_value_t value = pp_if_signed_value(0ull);
  ctool_status_t status;
  ctool_status_t rewound;
  if (begin == end) {
    return pp_directive_error(context, marker,
                              CTOOL_C_PP_DIAG_CONDITIONAL,
                              "CupidC conditional requires an expression");
  }
  mark = ctool_arena_mark(arena);
  pp_expand_list_zero(&expanded);
  saved_expansion_node_count = context->expansion_node_count;
  saved_condition_expression = context->condition_expression;
  context->condition_expression = CTOOL_TRUE;
  status = pp_expand_condition_tokens(context, tokens, begin, end, &expanded);
  if (status == CTOOL_OK && expanded.head == (pp_expand_node_t *)0) {
    status = pp_if_expression_fail(
        context, marker, marker, CTOOL_ERR_INPUT,
        "CupidC conditional macro expansion is empty");
  }
  if (status == CTOOL_OK) {
    parser.context = context;
    parser.marker = marker;
    parser.cursor = expanded.head;
    parser.depth = 0u;
    status = pp_if_parse_expression(&parser, 0u, CTOOL_TRUE, CTOOL_FALSE,
                                    &value);
    if (status == CTOOL_OK && parser.cursor != (pp_expand_node_t *)0) {
      status = pp_if_parser_fail(
          &parser, &parser.cursor->token, CTOOL_ERR_INPUT,
          "CupidC conditional expression has trailing tokens");
    }
    if (status == CTOOL_OK) {
      *value_out = pp_if_value_truth(value);
    }
  }
  context->condition_expression = saved_condition_expression;
  context->expansion_node_count = saved_expansion_node_count;
  rewound = ctool_arena_rewind(arena, mark);
  if (rewound != CTOOL_OK) {
    if (status == CTOOL_OK) {
      pp_fail(context, rewound, CTOOL_C_PP_DIAG_LIMIT,
              marker->location.line, marker->location.column,
              "CupidC conditional scratch storage could not be rewound");
    }
    return rewound;
  }
  return status;
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
        ctool_u32 range_end = index + 1u;
        while (range_end < token_count &&
               pp_token_is_directive_marker(&tokens[range_end]) ==
                   CTOOL_FALSE) {
          range_end++;
        }
        status = pp_emit_expanded_range(context, tokens + index,
                                        range_end - index);
        if (status != CTOOL_OK) {
          return status;
        }
        index = range_end;
      } else {
        index++;
      }
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
          status = pp_evaluate_condition(context, marker, tokens, arguments,
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
          status = pp_evaluate_condition(context, marker, tokens, arguments,
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
      } else if (pp_token_equal_literal(name, "pragma") == CTOOL_TRUE) {
        status = pp_handle_pragma(context, marker, tokens, arguments, end);
        if (status != CTOOL_OK) {
          return status;
        }
      } else if (pp_token_equal_literal(name, "exe") == CTOOL_TRUE) {
        ctool_u32 exe_end = end;
        /* Keep the raw opener on this logical directive line, but expand the
         * ordinary suffix as one range so a macro call may cross newlines. */
        while (exe_end < token_count &&
               pp_token_is_directive_marker(&tokens[exe_end]) ==
                   CTOOL_FALSE) {
          exe_end++;
        }
        status = pp_handle_cupid_exe(context, marker, name, tokens,
                                     arguments, end, exe_end);
        if (status != CTOOL_OK) {
          return status;
        }
        end = exe_end;
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
  pp_source_cache_t *previous_entry = context->source_entry;
  pp_source_cache_t *entry = pp_source_cache_find(context, &source->path);
  pp_input_t input;
  ctool_status_t status;
  if (entry == (pp_source_cache_t *)0) {
    status = pp_source_cache_add(context, source, &entry);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if (entry->pragma_once == CTOOL_TRUE && entry->entered == CTOOL_TRUE) {
    return CTOOL_OK;
  }
  entry->entered = CTOOL_TRUE;
  context->source = &entry->source;
  context->path = entry->source.path.text;
  context->source_entry = entry;
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
  context->source_entry = previous_entry;
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
  context.source_entry = (pp_source_cache_t *)0;
  context.pack_stack = (pp_pack_frame_t *)0;
  context.free_pack_frames = (pp_pack_frame_t *)0;
  context.output_head = (pp_output_chunk_t *)0;
  context.output_tail = (pp_output_chunk_t *)0;
  context.output_count = 0u;
  context.include_depth = 0u;
  context.expansion_depth = 0u;
  context.expansion_node_count = 0u;
  context.pack_depth = 0u;
  context.pack_alignment = 0u;
  context.condition_expression = CTOOL_FALSE;
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
