#include "cupidasm.h"

#define ASM_U32_MAX 4294967295u
#define ASM_U64_MAX 18446744073709551615ull

typedef enum {
  ASM_TOKEN_IDENT = 1,
  ASM_TOKEN_NUMBER,
  ASM_TOKEN_STRING,
  ASM_TOKEN_COMMA,
  ASM_TOKEN_COLON,
  ASM_TOKEN_LEFT_PAREN,
  ASM_TOKEN_RIGHT_PAREN,
  ASM_TOKEN_LEFT_BRACKET,
  ASM_TOKEN_RIGHT_BRACKET,
  ASM_TOKEN_PLUS,
  ASM_TOKEN_MINUS,
  ASM_TOKEN_STAR,
  ASM_TOKEN_TILDE,
  ASM_TOKEN_SHIFT_LEFT,
  ASM_TOKEN_SHIFT_RIGHT,
  ASM_TOKEN_CURRENT,
  ASM_TOKEN_SECTION_BASE,
  ASM_TOKEN_PREPROCESSOR
} asm_token_kind_t;

typedef struct {
  asm_token_kind_t kind;
  ctool_string_t text;
  ctool_u64 number;
  ctool_u32 line;
  ctool_u32 column;
} asm_token_t;

typedef enum {
  ASM_SYMBOL_UNDEFINED = 0,
  ASM_SYMBOL_LABEL,
  ASM_SYMBOL_CONSTANT,
  ASM_SYMBOL_ABSOLUTE
} asm_symbol_kind_t;

typedef struct asm_symbol asm_symbol_t;
typedef struct asm_expr asm_expr_t;
typedef struct asm_section asm_section_t;

struct asm_section {
  ctool_string_t name;
  ctool_elf32_section_type_t type;
  ctool_u32 flags;
  ctool_u32 alignment;
  ctool_u32 index;
  ctool_u32 size;
  ctool_u8 *contents;
  ctool_u32 load_address;
  ctool_u32 output_offset;
  asm_section_t *next;
};

struct asm_symbol {
  ctool_string_t name;
  ctool_string_t path;
  asm_symbol_kind_t kind;
  ctool_u64 value;
  asm_expr_t *definition;
  ctool_u32 definition_offset;
  ctool_bool declared;
  ctool_bool resolved;
  ctool_bool resolving;
  ctool_bool global;
  ctool_bool external;
  asm_section_t *section;
  ctool_u32 object_index;
  asm_symbol_t *next;
};

typedef enum {
  ASM_EXPR_LITERAL = 1,
  ASM_EXPR_SYMBOL,
  ASM_EXPR_CURRENT,
  ASM_EXPR_SECTION_BASE,
  ASM_EXPR_UNARY,
  ASM_EXPR_BINARY
} asm_expr_kind_t;

typedef enum {
  ASM_EXPR_OP_POSITIVE = 1,
  ASM_EXPR_OP_NEGATIVE,
  ASM_EXPR_OP_COMPLEMENT,
  ASM_EXPR_OP_ADD,
  ASM_EXPR_OP_SUBTRACT,
  ASM_EXPR_OP_MULTIPLY,
  ASM_EXPR_OP_SHIFT_LEFT,
  ASM_EXPR_OP_SHIFT_RIGHT
} asm_expr_op_t;

struct asm_expr {
  asm_expr_kind_t kind;
  ctool_string_t path;
  ctool_u32 line;
  ctool_u32 column;
  union {
    ctool_u64 literal;
    asm_symbol_t *symbol;
    struct {
      asm_expr_op_t op;
      asm_expr_t *operand;
    } unary;
    struct {
      asm_expr_op_t op;
      asm_expr_t *left;
      asm_expr_t *right;
    } binary;
  } as;
};

typedef enum {
  ASM_OPERAND_REGISTER = 1,
  ASM_OPERAND_EXPRESSION,
  ASM_OPERAND_MEMORY,
  ASM_OPERAND_FAR_POINTER
} asm_operand_kind_t;

typedef struct {
  asm_operand_kind_t kind;
  ctool_x86_reg_t reg;
  asm_expr_t *expression;
  asm_expr_t *second_expression;
  ctool_u16 width_bits;
  ctool_u16 address_bits;
  ctool_x86_reg_t segment;
  ctool_x86_reg_t base;
  ctool_x86_reg_t index;
  ctool_u8 scale;
} asm_operand_t;

typedef struct asm_data_value asm_data_value_t;
struct asm_data_value {
  asm_expr_t *expression;
  ctool_string_t string;
  ctool_bool is_string;
  ctool_u32 element_count;
  asm_data_value_t *next;
};

typedef enum {
  ASM_STATEMENT_LABEL = 1,
  ASM_STATEMENT_EQU,
  ASM_STATEMENT_INSTRUCTION,
  ASM_STATEMENT_DATA,
  ASM_STATEMENT_RESERVE
} asm_statement_kind_t;

typedef struct asm_statement asm_statement_t;
struct asm_statement {
  asm_statement_kind_t kind;
  ctool_string_t path;
  ctool_u32 line;
  ctool_u32 column;
  ctool_u32 offset;
  ctool_u32 size;
  ctool_x86_mode_t mode;
  asm_section_t *section;
  union {
    asm_symbol_t *label;
    asm_symbol_t *equ;
    struct {
      ctool_x86_mnemonic_t mnemonic;
      ctool_u8 prefixes;
      ctool_u16 address_bits;
      ctool_u16 branch_bits;
      ctool_u8 operand_count;
      asm_operand_t operands[CTOOL_X86_MAX_OPERANDS];
    } instruction;
    struct {
      ctool_u8 width;
      asm_data_value_t *values;
      ctool_u32 value_count;
      asm_expr_t *repeat;
      ctool_u32 repeat_count;
    } data;
    struct {
      ctool_u8 width;
      asm_expr_t *count;
      ctool_u32 element_count;
    } reserve;
  } as;
  asm_statement_t *next;
};

typedef struct {
  ctool_job_t *job;
  const ctool_source_t *source;
  const ctool_asm_request_t *request;
  asm_statement_t *statements;
  asm_statement_t *statement_tail;
  asm_symbol_t *symbols;
  asm_section_t *sections;
  asm_section_t *section_tail;
  asm_section_t *current_section;
  ctool_u32 section_count;
  ctool_string_t current_global;
  ctool_x86_mode_t mode;
  ctool_u32 origin;
  ctool_bool have_origin;
  ctool_path_t include_stack[CTOOL_ASM_MAX_INCLUDE_DEPTH];
  ctool_u32 include_depth;
  ctool_status_t failure_status;
  ctool_u32 failure_code;
  ctool_u32 failure_line;
  ctool_u32 failure_column;
  ctool_string_t failure_path;
  ctool_string_t active_path;
  const char *failure_message;
} asm_context_t;

static void asm_zero_result(ctool_asm_result_t *result) {
  result->artifact = (ctool_asm_artifact_kind_t)0;
  result->bytes.data = (const ctool_u8 *)0;
  result->bytes.size = 0u;
  result->regions = (const ctool_asm_region_t *)0;
  result->region_count = 0u;
  result->has_entry = CTOOL_FALSE;
  result->entry_address = 0u;
}

static ctool_bool asm_character_is_alpha(char character) {
  return ((character >= 'a' && character <= 'z') ||
          (character >= 'A' && character <= 'Z') || character == '_' ||
          character == '.')
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool asm_character_is_digit(char character) {
  return character >= '0' && character <= '9' ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool asm_character_is_alnum(char character) {
  return asm_character_is_alpha(character) == CTOOL_TRUE ||
                 asm_character_is_digit(character) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static char asm_lower(char character) {
  if (character >= 'A' && character <= 'Z') {
    return (char)(character + ('a' - 'A'));
  }
  return character;
}

static ctool_bool asm_string_equal(ctool_string_t left,
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

static ctool_bool asm_string_equal_folded(ctool_string_t left,
                                          ctool_string_t right) {
  ctool_u32 index;
  if (left.size != right.size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < left.size; index++) {
    if (asm_lower(left.data[index]) != asm_lower(right.data[index])) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool asm_string_equal_case(ctool_string_t value,
                                        const char *literal) {
  ctool_u32 size = 0u;
  ctool_u32 index;
  while (literal[size] != '\0') {
    size++;
  }
  if (value.size != size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < size; index++) {
    if (asm_lower(value.data[index]) != asm_lower(literal[index])) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static void asm_fail(asm_context_t *context, ctool_status_t status,
                     ctool_u32 code, ctool_u32 line, ctool_u32 column,
                     const char *message) {
  if (context->failure_status != CTOOL_OK) {
    return;
  }
  context->failure_status = status;
  context->failure_code = code;
  context->failure_line = line;
  context->failure_column = column;
  context->failure_path = context->active_path;
  context->failure_message = message;
}

static ctool_status_t asm_publish_failure(asm_context_t *context) {
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

static ctool_status_t asm_copy_name(asm_context_t *context,
                                    ctool_string_t name,
                                    ctool_string_t *copy_out) {
  return ctool_arena_copy_string(ctool_job_arena(context->job), name,
                                 copy_out);
}

static asm_section_t *asm_find_section(asm_context_t *context,
                                       ctool_string_t name) {
  asm_section_t *section = context->sections;
  while (section != (asm_section_t *)0) {
    if (asm_string_equal(section->name, name) == CTOOL_TRUE) {
      return section;
    }
    section = section->next;
  }
  return (asm_section_t *)0;
}

static asm_section_t *asm_get_section(asm_context_t *context,
                                      ctool_string_t spelling,
                                      ctool_u32 line,
                                      ctool_u32 column) {
  asm_section_t *section = asm_find_section(context, spelling);
  ctool_status_t status;
  if (section != (asm_section_t *)0) {
    return section;
  }
  status = ctool_arena_alloc_zero(ctool_job_arena(context->job), 1u,
                                  (ctool_u32)sizeof(*section),
                                  (ctool_u32)sizeof(void *),
                                  (void **)&section);
  if (status != CTOOL_OK) {
    asm_fail(context, status, CTOOL_ASM_DIAG_LIMIT, line, column,
             "CupidASM section storage limit exceeded");
    return (asm_section_t *)0;
  }
  status = asm_copy_name(context, spelling, &section->name);
  if (status != CTOOL_OK) {
    asm_fail(context, status, CTOOL_ASM_DIAG_LIMIT, line, column,
             "CupidASM section name storage limit exceeded");
    return (asm_section_t *)0;
  }
  section->type = CTOOL_ELF32_SHT_PROGBITS;
  section->alignment = 1u;
  if (asm_string_equal_case(spelling, ".text") == CTOOL_TRUE) {
    section->flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
    section->alignment = 16u;
  } else if (asm_string_equal_case(spelling, ".data") == CTOOL_TRUE) {
    section->flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
    section->alignment = 4u;
  } else if (asm_string_equal_case(spelling, ".bss") == CTOOL_TRUE) {
    section->type = CTOOL_ELF32_SHT_NOBITS;
    section->flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
    section->alignment = 4u;
  }
  section->index = context->section_count;
  context->section_count++;
  if (context->section_tail == (asm_section_t *)0) {
    context->sections = section;
  } else {
    context->section_tail->next = section;
  }
  context->section_tail = section;
  return section;
}

static asm_symbol_t *asm_find_symbol(asm_context_t *context,
                                     ctool_string_t name) {
  asm_symbol_t *symbol = context->symbols;
  while (symbol != (asm_symbol_t *)0) {
    ctool_bool equal = context->request->case_insensitive_symbols ==
                               CTOOL_TRUE
                           ? asm_string_equal_folded(symbol->name, name)
                           : asm_string_equal(symbol->name, name);
    if (equal == CTOOL_TRUE) {
      return symbol;
    }
    symbol = symbol->next;
  }
  return (asm_symbol_t *)0;
}

static ctool_status_t asm_qualified_name(asm_context_t *context,
                                         ctool_string_t spelling,
                                         ctool_string_t *name_out) {
  ctool_u32 size;
  char *storage;
  ctool_u32 index;
  if (spelling.size == 0u || spelling.data == (const char *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (spelling.data[0] != '.') {
    return asm_copy_name(context, spelling, name_out);
  }
  if (context->current_global.size == 0u) {
    return CTOOL_ERR_INPUT;
  }
  if (context->current_global.size > ASM_U32_MAX - spelling.size) {
    return CTOOL_ERR_OVERFLOW;
  }
  size = context->current_global.size + spelling.size;
  if (ctool_arena_alloc(ctool_job_arena(context->job), size + 1u, 1u,
                        (void **)&storage) != CTOOL_OK) {
    return CTOOL_ERR_NO_MEMORY;
  }
  for (index = 0u; index < context->current_global.size; index++) {
    storage[index] = context->current_global.data[index];
  }
  for (index = 0u; index < spelling.size; index++) {
    storage[context->current_global.size + index] = spelling.data[index];
  }
  storage[size] = '\0';
  name_out->data = storage;
  name_out->size = size;
  return CTOOL_OK;
}

static asm_symbol_t *asm_intern_symbol(asm_context_t *context,
                                       ctool_string_t spelling,
                                       ctool_u32 line, ctool_u32 column) {
  ctool_string_t name;
  asm_symbol_t *symbol;
  ctool_status_t status = asm_qualified_name(context, spelling, &name);
  if (status != CTOOL_OK) {
    asm_fail(context, status, CTOOL_ASM_DIAG_SYNTAX, line, column,
             "local label has no non-local scope");
    return (asm_symbol_t *)0;
  }
  symbol = asm_find_symbol(context, name);
  if (symbol != (asm_symbol_t *)0) {
    return symbol;
  }
  status = ctool_arena_alloc_zero(ctool_job_arena(context->job), 1u,
                                  (ctool_u32)sizeof(*symbol),
                                  (ctool_u32)sizeof(void *),
                                  (void **)&symbol);
  if (status != CTOOL_OK) {
    asm_fail(context, status, CTOOL_ASM_DIAG_LIMIT, line, column,
             "CupidASM symbol storage limit exceeded");
    return (asm_symbol_t *)0;
  }
  symbol->name = name;
  symbol->path = context->active_path;
  symbol->next = context->symbols;
  context->symbols = symbol;
  return symbol;
}

static ctool_bool asm_definition_name_is_valid(ctool_string_t name) {
  ctool_u32 index;
  if (name.data == (const char *)0 || name.size == 0u ||
      name.data[0] == '.' ||
      asm_character_is_alpha(name.data[0]) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  for (index = 1u; index < name.size; index++) {
    if (asm_character_is_alnum(name.data[index]) == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t asm_install_definitions(asm_context_t *context) {
  ctool_u32 index;
  for (index = 0u; index < context->request->definition_count; index++) {
    const ctool_asm_definition_t *definition =
        &context->request->definitions[index];
    asm_symbol_t *symbol;
    if (asm_definition_name_is_valid(definition->name) == CTOOL_FALSE ||
        (definition->kind != CTOOL_ASM_DEFINE_CONSTANT &&
         definition->kind != CTOOL_ASM_DEFINE_ABSOLUTE)) {
      asm_fail(context, CTOOL_ERR_INVALID_ARGUMENT,
               CTOOL_ASM_DIAG_INVALID_REQUEST, 0u, 0u,
               "invalid CupidASM request definition");
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
    symbol = asm_intern_symbol(context, definition->name, 0u, 0u);
    if (symbol == (asm_symbol_t *)0) {
      return context->failure_status;
    }
    if (symbol->declared == CTOOL_TRUE) {
      asm_fail(context, CTOOL_ERR_INPUT,
               CTOOL_ASM_DIAG_DUPLICATE_SYMBOL, 0u, 0u,
               "duplicate CupidASM request definition");
      return CTOOL_ERR_INPUT;
    }
    symbol->declared = CTOOL_TRUE;
    symbol->kind = definition->kind == CTOOL_ASM_DEFINE_CONSTANT
                       ? ASM_SYMBOL_CONSTANT
                       : ASM_SYMBOL_ABSOLUTE;
    symbol->value = (ctool_u64)definition->value;
    symbol->resolved = CTOOL_TRUE;
  }
  return CTOOL_OK;
}

static asm_statement_t *asm_append_statement(asm_context_t *context,
                                              asm_statement_kind_t kind,
                                              ctool_u32 line,
                                              ctool_u32 column) {
  asm_statement_t *statement;
  ctool_status_t status = ctool_arena_alloc_zero(
      ctool_job_arena(context->job), 1u,
      (ctool_u32)sizeof(*statement), (ctool_u32)sizeof(void *),
      (void **)&statement);
  if (status != CTOOL_OK) {
    asm_fail(context, status, CTOOL_ASM_DIAG_LIMIT, line, column,
             "CupidASM statement storage limit exceeded");
    return (asm_statement_t *)0;
  }
  statement->kind = kind;
  statement->path = context->active_path;
  statement->line = line;
  statement->column = column;
  statement->mode = context->mode;
  statement->section = context->current_section;
  if (context->statement_tail == (asm_statement_t *)0) {
    context->statements = statement;
  } else {
    context->statement_tail->next = statement;
  }
  context->statement_tail = statement;
  return statement;
}

static ctool_status_t asm_parse_number(ctool_string_t spelling,
                                       ctool_u64 *value_out) {
  ctool_u32 base = 10u;
  ctool_u32 index = 0u;
  ctool_u32 end = spelling.size;
  ctool_u64 value = 0ull;
  if (spelling.size >= 2u && spelling.data[0] == '0' &&
      (spelling.data[1] == 'x' || spelling.data[1] == 'X')) {
    base = 16u;
    index = 2u;
  } else if (spelling.size >= 2u &&
             (spelling.data[spelling.size - 1u] == 'b' ||
              spelling.data[spelling.size - 1u] == 'B')) {
    base = 2u;
    end--;
  }
  if (index == end) {
    return CTOOL_ERR_INPUT;
  }
  while (index < end) {
    ctool_u32 digit;
    char character = spelling.data[index];
    if (character >= '0' && character <= '9') {
      digit = (ctool_u32)(character - '0');
    } else if (character >= 'a' && character <= 'f') {
      digit = 10u + (ctool_u32)(character - 'a');
    } else if (character >= 'A' && character <= 'F') {
      digit = 10u + (ctool_u32)(character - 'A');
    } else {
      return CTOOL_ERR_INPUT;
    }
    if (digit >= base ||
        value > (ASM_U64_MAX - (ctool_u64)digit) / (ctool_u64)base) {
      return CTOOL_ERR_OVERFLOW;
    }
    value = value * (ctool_u64)base + (ctool_u64)digit;
    index++;
  }
  *value_out = value;
  return CTOOL_OK;
}

static ctool_status_t asm_tokenize_line(asm_context_t *context,
                                        const char *line_text,
                                        ctool_u32 line_size,
                                        ctool_u32 line_number,
                                        asm_token_t *tokens,
                                        ctool_u32 token_capacity,
                                        ctool_u32 *count_out) {
  ctool_u32 position = 0u;
  ctool_u32 count = 0u;
  while (position < line_size) {
    ctool_u32 start;
    char character = line_text[position];
    if (character == ' ' || character == '\t' || character == '\r') {
      position++;
      continue;
    }
    if (character == ';') {
      break;
    }
    if (count == token_capacity) {
      asm_fail(context, CTOOL_ERR_LIMIT, CTOOL_ASM_DIAG_LIMIT, line_number,
               position + 1u, "too many tokens on Cupid ASM source line");
      return CTOOL_ERR_LIMIT;
    }
    tokens[count].line = line_number;
    tokens[count].column = position + 1u;
    tokens[count].number = 0ull;
    if (character == '%' && position + 1u < line_size &&
        asm_character_is_alpha(line_text[position + 1u]) == CTOOL_TRUE) {
      start = position + 1u;
      position += 2u;
      while (position < line_size &&
             asm_character_is_alnum(line_text[position]) == CTOOL_TRUE) {
        position++;
      }
      tokens[count].kind = ASM_TOKEN_PREPROCESSOR;
      tokens[count].text.data = line_text + start;
      tokens[count].text.size = position - start;
    } else if (asm_character_is_alpha(character) == CTOOL_TRUE) {
      start = position;
      position++;
      while (position < line_size &&
             asm_character_is_alnum(line_text[position]) == CTOOL_TRUE) {
        position++;
      }
      tokens[count].kind = ASM_TOKEN_IDENT;
      tokens[count].text.data = line_text + start;
      tokens[count].text.size = position - start;
    } else if (asm_character_is_digit(character) == CTOOL_TRUE) {
      ctool_status_t status;
      start = position;
      position++;
      while (position < line_size &&
             asm_character_is_alnum(line_text[position]) == CTOOL_TRUE) {
        position++;
      }
      tokens[count].kind = ASM_TOKEN_NUMBER;
      tokens[count].text.data = line_text + start;
      tokens[count].text.size = position - start;
      status = asm_parse_number(tokens[count].text, &tokens[count].number);
      if (status != CTOOL_OK) {
        asm_fail(context, status, CTOOL_ASM_DIAG_LEXICAL, line_number,
                 start + 1u, "invalid numeric literal");
        return status;
      }
    } else if (character == '\'') {
      ctool_u64 value;
      start = position;
      position++;
      if (position == line_size) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_LEXICAL,
                 line_number, start + 1u, "unterminated character literal");
        return CTOOL_ERR_INPUT;
      }
      if (line_text[position] == '\\') {
        position++;
        if (position == line_size) {
          asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_LEXICAL,
                   line_number, start + 1u,
                   "unterminated character literal escape");
          return CTOOL_ERR_INPUT;
        }
        switch (line_text[position]) {
          case '0': value = 0ull; break;
          case 'n': value = 10ull; break;
          case 'r': value = 13ull; break;
          case 't': value = 9ull; break;
          case '\\': value = 92ull; break;
          case '\'': value = 39ull; break;
          case '"': value = 34ull; break;
          default:
            asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_LEXICAL,
                     line_number, position + 1u,
                     "unsupported character literal escape");
            return CTOOL_ERR_INPUT;
        }
      } else {
        value = (ctool_u64)(ctool_u8)line_text[position];
      }
      position++;
      if (position >= line_size || line_text[position] != '\'') {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_LEXICAL,
                 line_number, start + 1u,
                 "character literal must contain exactly one character");
        return CTOOL_ERR_INPUT;
      }
      position++;
      tokens[count].kind = ASM_TOKEN_NUMBER;
      tokens[count].number = value;
      tokens[count].text.data = line_text + start;
      tokens[count].text.size = position - start;
    } else if (character == '"') {
      start = position + 1u;
      position++;
      while (position < line_size && line_text[position] != '"') {
        if (line_text[position] == '\\') {
          position++;
          if (position == line_size) {
            asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_LEXICAL,
                     line_number, start,
                     "unterminated Cupid ASM string escape");
            return CTOOL_ERR_INPUT;
          }
        }
        position++;
      }
      if (position == line_size) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_LEXICAL,
                 line_number, start,
                 "unterminated Cupid ASM string literal");
        return CTOOL_ERR_INPUT;
      }
      tokens[count].kind = ASM_TOKEN_STRING;
      tokens[count].text.data = line_text + start;
      tokens[count].text.size = position - start;
      position++;
    } else {
      tokens[count].text.data = line_text + position;
      tokens[count].text.size = 1u;
      if (character == ',') {
        tokens[count].kind = ASM_TOKEN_COMMA;
      } else if (character == ':') {
        tokens[count].kind = ASM_TOKEN_COLON;
      } else if (character == '(') {
        tokens[count].kind = ASM_TOKEN_LEFT_PAREN;
      } else if (character == ')') {
        tokens[count].kind = ASM_TOKEN_RIGHT_PAREN;
      } else if (character == '[') {
        tokens[count].kind = ASM_TOKEN_LEFT_BRACKET;
      } else if (character == ']') {
        tokens[count].kind = ASM_TOKEN_RIGHT_BRACKET;
      } else if (character == '+') {
        tokens[count].kind = ASM_TOKEN_PLUS;
      } else if (character == '-') {
        tokens[count].kind = ASM_TOKEN_MINUS;
      } else if (character == '*') {
        tokens[count].kind = ASM_TOKEN_STAR;
      } else if (character == '~') {
        tokens[count].kind = ASM_TOKEN_TILDE;
      } else if ((character == '<' || character == '>') &&
                 position + 1u < line_size &&
                 line_text[position + 1u] == character) {
        tokens[count].kind = character == '<' ? ASM_TOKEN_SHIFT_LEFT
                                               : ASM_TOKEN_SHIFT_RIGHT;
        tokens[count].text.size = 2u;
        position++;
      } else if (character == '$') {
        if (position + 1u < line_size && line_text[position + 1u] == '$') {
          tokens[count].kind = ASM_TOKEN_SECTION_BASE;
          tokens[count].text.size = 2u;
          position++;
        } else {
          tokens[count].kind = ASM_TOKEN_CURRENT;
        }
      } else {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_LEXICAL,
                 line_number, position + 1u,
                 "unsupported character in Cupid ASM source");
        return CTOOL_ERR_INPUT;
      }
      position++;
    }
    count++;
  }
  *count_out = count;
  return CTOOL_OK;
}

static asm_expr_t *asm_new_expression(asm_context_t *context,
                                      asm_expr_kind_t kind,
                                      const asm_token_t *token) {
  asm_expr_t *expression;
  ctool_status_t status = ctool_arena_alloc_zero(
      ctool_job_arena(context->job), 1u, (ctool_u32)sizeof(*expression),
      (ctool_u32)sizeof(void *), (void **)&expression);
  if (status != CTOOL_OK) {
    asm_fail(context, status, CTOOL_ASM_DIAG_LIMIT, token->line,
             token->column, "CupidASM expression storage limit exceeded");
    return (asm_expr_t *)0;
  }
  expression->kind = kind;
  expression->path = context->active_path;
  expression->line = token->line;
  expression->column = token->column;
  return expression;
}

static ctool_status_t asm_parse_expression(asm_context_t *context,
                                           const asm_token_t *tokens,
                                           ctool_u32 count,
                                           ctool_u32 *index,
                                           asm_expr_t **expression_out);

static ctool_status_t asm_parse_primary(asm_context_t *context,
                                        const asm_token_t *tokens,
                                        ctool_u32 count,
                                        ctool_u32 *index,
                                        asm_expr_t **expression_out) {
  const asm_token_t *token;
  asm_expr_t *expression;
  ctool_status_t status;
  if (*index >= count) {
    return CTOOL_ERR_INPUT;
  }
  token = &tokens[*index];
  if (token->kind == ASM_TOKEN_LEFT_PAREN) {
    (*index)++;
    status = asm_parse_expression(context, tokens, count, index,
                                  expression_out);
    if (status != CTOOL_OK) {
      return status;
    }
    if (*index >= count || tokens[*index].kind != ASM_TOKEN_RIGHT_PAREN) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_EXPRESSION,
               token->line, token->column,
               "expression has no closing parenthesis");
      return CTOOL_ERR_INPUT;
    }
    (*index)++;
    return CTOOL_OK;
  }
  if (token->kind == ASM_TOKEN_NUMBER) {
    expression = asm_new_expression(context, ASM_EXPR_LITERAL, token);
    if (expression == (asm_expr_t *)0) {
      return context->failure_status;
    }
    expression->as.literal = token->number;
  } else if (token->kind == ASM_TOKEN_IDENT) {
    expression = asm_new_expression(context, ASM_EXPR_SYMBOL, token);
    if (expression == (asm_expr_t *)0) {
      return context->failure_status;
    }
    expression->as.symbol = asm_intern_symbol(
        context, token->text, token->line, token->column);
    if (expression->as.symbol == (asm_symbol_t *)0) {
      return context->failure_status;
    }
  } else if (token->kind == ASM_TOKEN_CURRENT) {
    expression = asm_new_expression(context, ASM_EXPR_CURRENT, token);
    if (expression == (asm_expr_t *)0) {
      return context->failure_status;
    }
  } else if (token->kind == ASM_TOKEN_SECTION_BASE) {
    expression = asm_new_expression(context, ASM_EXPR_SECTION_BASE, token);
    if (expression == (asm_expr_t *)0) {
      return context->failure_status;
    }
  } else {
    asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_EXPRESSION,
             token->line, token->column, "expected an expression atom");
    return CTOOL_ERR_INPUT;
  }
  (*index)++;
  *expression_out = expression;
  return CTOOL_OK;
}

static ctool_status_t asm_parse_unary(asm_context_t *context,
                                      const asm_token_t *tokens,
                                      ctool_u32 count,
                                      ctool_u32 *index,
                                      asm_expr_t **expression_out) {
  asm_expr_op_t op;
  asm_expr_t *expression;
  ctool_status_t status;
  if (*index >= count ||
      (tokens[*index].kind != ASM_TOKEN_PLUS &&
       tokens[*index].kind != ASM_TOKEN_MINUS &&
       tokens[*index].kind != ASM_TOKEN_TILDE)) {
    return asm_parse_primary(context, tokens, count, index, expression_out);
  }
  op = tokens[*index].kind == ASM_TOKEN_PLUS
           ? ASM_EXPR_OP_POSITIVE
           : tokens[*index].kind == ASM_TOKEN_MINUS
                 ? ASM_EXPR_OP_NEGATIVE
                 : ASM_EXPR_OP_COMPLEMENT;
  expression = asm_new_expression(context, ASM_EXPR_UNARY, &tokens[*index]);
  if (expression == (asm_expr_t *)0) {
    return context->failure_status;
  }
  expression->as.unary.op = op;
  (*index)++;
  status = asm_parse_unary(context, tokens, count, index,
                           &expression->as.unary.operand);
  if (status != CTOOL_OK) {
    return status;
  }
  *expression_out = expression;
  return CTOOL_OK;
}

static ctool_status_t asm_make_binary(asm_context_t *context,
                                      const asm_token_t *operator_token,
                                      asm_expr_op_t op,
                                      asm_expr_t *left,
                                      asm_expr_t *right,
                                      asm_expr_t **expression_out) {
  asm_expr_t *expression =
      asm_new_expression(context, ASM_EXPR_BINARY, operator_token);
  if (expression == (asm_expr_t *)0) {
    return context->failure_status;
  }
  expression->as.binary.op = op;
  expression->as.binary.left = left;
  expression->as.binary.right = right;
  *expression_out = expression;
  return CTOOL_OK;
}

static ctool_status_t asm_parse_multiply(asm_context_t *context,
                                         const asm_token_t *tokens,
                                         ctool_u32 count,
                                         ctool_u32 *index,
                                         asm_expr_t **expression_out) {
  asm_expr_t *left;
  ctool_status_t status =
      asm_parse_unary(context, tokens, count, index, &left);
  while (status == CTOOL_OK && *index < count &&
         tokens[*index].kind == ASM_TOKEN_STAR) {
    const asm_token_t *operator_token = &tokens[*index];
    asm_expr_t *right;
    (*index)++;
    status = asm_parse_unary(context, tokens, count, index, &right);
    if (status == CTOOL_OK) {
      status = asm_make_binary(context, operator_token,
                               ASM_EXPR_OP_MULTIPLY, left, right, &left);
    }
  }
  if (status == CTOOL_OK) {
    *expression_out = left;
  }
  return status;
}

static ctool_status_t asm_expression_failure(asm_context_t *context,
                                             const asm_expr_t *expression,
                                             ctool_bool diagnose,
                                             ctool_status_t status,
                                             ctool_u32 code,
                                             const char *message) {
  if (diagnose == CTOOL_TRUE) {
    asm_fail(context, status, code, expression->line, expression->column,
             message);
  }
  return status;
}

static ctool_status_t asm_evaluate_expression(asm_context_t *context,
                                              const asm_expr_t *expression,
                                              ctool_u32 current_offset,
                                              ctool_bool diagnose,
                                              ctool_u64 *value_out);

static ctool_status_t asm_evaluate_symbol(asm_context_t *context,
                                          const asm_expr_t *expression,
                                          ctool_u32 current_offset,
                                          ctool_bool diagnose,
                                          ctool_u64 *value_out) {
  asm_symbol_t *symbol = expression->as.symbol;
  ctool_status_t status;
  ctool_u64 value;
  if (symbol == (asm_symbol_t *)0 || symbol->declared == CTOOL_FALSE) {
    return asm_expression_failure(
        context, expression, diagnose, CTOOL_ERR_NOT_FOUND,
        CTOOL_ASM_DIAG_UNDEFINED_SYMBOL, "undefined Cupid ASM symbol");
  }
  if (symbol->kind == ASM_SYMBOL_LABEL) {
    ctool_u64 base = 0ull;
    if (symbol->resolved == CTOOL_FALSE) {
      return CTOOL_ERR_NOT_FOUND;
    }
    if (context->request->artifact == CTOOL_ASM_ARTIFACT_RAW) {
      base = (ctool_u64)context->origin;
    } else if (context->request->artifact ==
                   CTOOL_ASM_ARTIFACT_FIXED_IMAGE &&
               symbol->section != (asm_section_t *)0) {
      base = (ctool_u64)symbol->section->load_address;
    }
    value = symbol->value;
    if (value > ASM_U64_MAX - base) {
      return asm_expression_failure(
          context, expression, diagnose, CTOOL_ERR_OVERFLOW,
          CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW, "label address overflows");
    }
    *value_out = value + base;
    return CTOOL_OK;
  }
  if ((symbol->kind == ASM_SYMBOL_ABSOLUTE ||
       symbol->kind == ASM_SYMBOL_CONSTANT) &&
      symbol->resolved == CTOOL_TRUE) {
    *value_out = symbol->value;
    return CTOOL_OK;
  }
  if (symbol->definition == (asm_expr_t *)0) {
    return asm_expression_failure(
        context, expression, diagnose, CTOOL_ERR_NOT_FOUND,
        CTOOL_ASM_DIAG_UNDEFINED_SYMBOL, "undefined Cupid ASM symbol");
  }
  if (symbol->resolving == CTOOL_TRUE) {
    return asm_expression_failure(
        context, expression, diagnose, CTOOL_ERR_INPUT,
        CTOOL_ASM_DIAG_INVALID_EXPRESSION, "cyclic Cupid ASM definition");
  }
  symbol->resolving = CTOOL_TRUE;
  if (symbol->kind == ASM_SYMBOL_ABSOLUTE &&
      context->request->artifact == CTOOL_ASM_ARTIFACT_FIXED_IMAGE &&
      symbol->section != (asm_section_t *)0) {
    ctool_u32 saved_origin = context->origin;
    context->origin = symbol->section->load_address;
    status = asm_evaluate_expression(context, symbol->definition,
                                     symbol->definition_offset, diagnose,
                                     &value);
    context->origin = saved_origin;
  } else {
    status = asm_evaluate_expression(
        context, symbol->definition,
        symbol->kind == ASM_SYMBOL_ABSOLUTE ? symbol->definition_offset
                                            : current_offset,
        diagnose, &value);
  }
  symbol->resolving = CTOOL_FALSE;
  if (status == CTOOL_OK && symbol->kind == ASM_SYMBOL_ABSOLUTE) {
    symbol->value = value;
    symbol->resolved = CTOOL_TRUE;
  }
  if (status == CTOOL_OK) {
    *value_out = value;
  }
  return status;
}

static ctool_status_t asm_evaluate_expression(asm_context_t *context,
                                              const asm_expr_t *expression,
                                              ctool_u32 current_offset,
                                              ctool_bool diagnose,
                                              ctool_u64 *value_out) {
  ctool_u64 left;
  ctool_u64 right;
  ctool_status_t status;
  if (expression->kind == ASM_EXPR_LITERAL) {
    *value_out = expression->as.literal;
    return CTOOL_OK;
  }
  if (expression->kind == ASM_EXPR_SYMBOL) {
    return asm_evaluate_symbol(context, expression, current_offset, diagnose,
                               value_out);
  }
  if (expression->kind == ASM_EXPR_CURRENT) {
    *value_out = (ctool_u64)context->origin + (ctool_u64)current_offset;
    return CTOOL_OK;
  }
  if (expression->kind == ASM_EXPR_SECTION_BASE) {
    *value_out = (ctool_u64)context->origin;
    return CTOOL_OK;
  }
  if (expression->kind == ASM_EXPR_UNARY) {
    status = asm_evaluate_expression(context, expression->as.unary.operand,
                                     current_offset, diagnose, &right);
    if (status != CTOOL_OK) {
      return status;
    }
    if (expression->as.unary.op == ASM_EXPR_OP_POSITIVE) {
      *value_out = right;
    } else if (expression->as.unary.op == ASM_EXPR_OP_NEGATIVE) {
      *value_out = 0ull - right;
    } else {
      *value_out = ~right;
    }
    return CTOOL_OK;
  }
  status = asm_evaluate_expression(context, expression->as.binary.left,
                                   current_offset, diagnose, &left);
  if (status != CTOOL_OK) {
    return status;
  }
  status = asm_evaluate_expression(context, expression->as.binary.right,
                                   current_offset, diagnose, &right);
  if (status != CTOOL_OK) {
    return status;
  }
  switch (expression->as.binary.op) {
    case ASM_EXPR_OP_ADD:
      if (left > ASM_U64_MAX - right) {
        return asm_expression_failure(
            context, expression, diagnose, CTOOL_ERR_OVERFLOW,
            CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW, "expression addition overflows");
      }
      *value_out = left + right;
      return CTOOL_OK;
    case ASM_EXPR_OP_SUBTRACT:
      *value_out = left - right;
      return CTOOL_OK;
    case ASM_EXPR_OP_MULTIPLY:
      if (right != 0ull && left > ASM_U64_MAX / right) {
        return asm_expression_failure(
            context, expression, diagnose, CTOOL_ERR_OVERFLOW,
            CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW,
            "expression multiplication overflows");
      }
      *value_out = left * right;
      return CTOOL_OK;
    case ASM_EXPR_OP_SHIFT_LEFT:
      if (right >= 64ull || left > (ASM_U64_MAX >> (ctool_u32)right)) {
        return asm_expression_failure(
            context, expression, diagnose, CTOOL_ERR_OVERFLOW,
            CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW,
            "expression left shift overflows");
      }
      *value_out = left << (ctool_u32)right;
      return CTOOL_OK;
    case ASM_EXPR_OP_SHIFT_RIGHT:
      if (right >= 64ull) {
        return asm_expression_failure(
            context, expression, diagnose, CTOOL_ERR_OVERFLOW,
            CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW,
            "expression right shift is too large");
      }
      *value_out = left >> (ctool_u32)right;
      return CTOOL_OK;
    default:
      return asm_expression_failure(
          context, expression, diagnose, CTOOL_ERR_INTERNAL,
          CTOOL_ASM_DIAG_INVALID_EXPRESSION,
          "unknown Cupid ASM expression operator");
  }
}

static ctool_status_t asm_define_symbol(asm_context_t *context,
                                        const asm_token_t *name,
                                        asm_symbol_kind_t kind,
                                        asm_expr_t *definition,
                                        asm_symbol_t **symbol_out) {
  asm_symbol_t *symbol = asm_intern_symbol(
      context, name->text, name->line, name->column);
  if (symbol == (asm_symbol_t *)0) {
    return context->failure_status;
  }
  if (symbol->declared == CTOOL_TRUE) {
    asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_DUPLICATE_SYMBOL,
             name->line, name->column,
             "duplicate Cupid ASM symbol definition");
    return CTOOL_ERR_INPUT;
  }
  symbol->declared = CTOOL_TRUE;
  symbol->kind = kind;
  symbol->definition = definition;
  *symbol_out = symbol;
  return CTOOL_OK;
}

static ctool_u8 asm_data_width(ctool_string_t directive) {
  if (asm_string_equal_case(directive, "db") == CTOOL_TRUE) {
    return 1u;
  }
  if (asm_string_equal_case(directive, "dw") == CTOOL_TRUE) {
    return 2u;
  }
  if (asm_string_equal_case(directive, "dd") == CTOOL_TRUE) {
    return 4u;
  }
  if (asm_string_equal_case(directive, "dq") == CTOOL_TRUE) {
    return 8u;
  }
  return 0u;
}

static ctool_u8 asm_reserve_width(ctool_string_t directive) {
  if (asm_string_equal_case(directive, "resb") == CTOOL_TRUE ||
      asm_string_equal_case(directive, "rb") == CTOOL_TRUE ||
      asm_string_equal_case(directive, "reserve") == CTOOL_TRUE) {
    return 1u;
  }
  if (asm_string_equal_case(directive, "resw") == CTOOL_TRUE ||
      asm_string_equal_case(directive, "rw") == CTOOL_TRUE) {
    return 2u;
  }
  if (asm_string_equal_case(directive, "resd") == CTOOL_TRUE ||
      asm_string_equal_case(directive, "rd") == CTOOL_TRUE) {
    return 4u;
  }
  return 0u;
}

static ctool_status_t asm_string_byte(ctool_string_t string,
                                      ctool_u32 *index,
                                      ctool_u8 *value_out) {
  char character;
  if (*index >= string.size) {
    return CTOOL_ERR_NOT_FOUND;
  }
  character = string.data[*index];
  (*index)++;
  if (character != '\\') {
    *value_out = (ctool_u8)character;
    return CTOOL_OK;
  }
  if (*index >= string.size) {
    return CTOOL_ERR_INPUT;
  }
  character = string.data[*index];
  (*index)++;
  switch (character) {
    case '0': *value_out = 0u; break;
    case 'n': *value_out = 10u; break;
    case 'r': *value_out = 13u; break;
    case 't': *value_out = 9u; break;
    case '\\': *value_out = 92u; break;
    case '\'': *value_out = 39u; break;
    case '"': *value_out = 34u; break;
    default: return CTOOL_ERR_UNSUPPORTED;
  }
  return CTOOL_OK;
}

static ctool_status_t asm_string_size(ctool_string_t string,
                                      ctool_u32 *size_out) {
  ctool_u32 index = 0u;
  ctool_u32 size = 0u;
  while (index < string.size) {
    ctool_u8 value;
    ctool_status_t status = asm_string_byte(string, &index, &value);
    (void)value;
    if (status != CTOOL_OK) {
      return status;
    }
    if (size == ASM_U32_MAX) {
      return CTOOL_ERR_OVERFLOW;
    }
    size++;
  }
  *size_out = size;
  return CTOOL_OK;
}

static ctool_status_t asm_parse_data(asm_context_t *context,
                                     const asm_token_t *tokens,
                                     ctool_u32 count,
                                     ctool_u32 directive_index,
                                     asm_expr_t *repeat) {
  asm_statement_t *statement;
  asm_data_value_t *tail = (asm_data_value_t *)0;
  ctool_u32 index = directive_index + 1u;
  ctool_u8 width = asm_data_width(tokens[directive_index].text);
  if (width == 0u || index == count) {
    asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
             tokens[directive_index].line, tokens[directive_index].column,
             "data directive requires at least one expression");
    return CTOOL_ERR_INPUT;
  }
  statement = asm_append_statement(context, ASM_STATEMENT_DATA,
                                   tokens[directive_index].line,
                                   tokens[directive_index].column);
  if (statement == (asm_statement_t *)0) {
    return context->failure_status;
  }
  statement->as.data.width = width;
  statement->as.data.repeat = repeat;
  while (index < count) {
    asm_data_value_t *value;
    ctool_status_t status = ctool_arena_alloc_zero(
        ctool_job_arena(context->job), 1u, (ctool_u32)sizeof(*value),
        (ctool_u32)sizeof(void *), (void **)&value);
    if (status != CTOOL_OK) {
      asm_fail(context, status, CTOOL_ASM_DIAG_LIMIT, tokens[index].line,
               tokens[index].column, "CupidASM data storage limit exceeded");
      return status;
    }
    if (tokens[index].kind == ASM_TOKEN_STRING) {
      if (width != 1u) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
                 tokens[index].line, tokens[index].column,
                 "quoted data strings require DB width");
        return CTOOL_ERR_INPUT;
      }
      value->is_string = CTOOL_TRUE;
      value->string = tokens[index].text;
      status = asm_string_size(value->string, &value->element_count);
      if (status != CTOOL_OK) {
        asm_fail(context, status, CTOOL_ASM_DIAG_LEXICAL,
                 tokens[index].line, tokens[index].column,
                 "unsupported escape in Cupid ASM string");
        return status;
      }
      index++;
    } else {
      status = asm_parse_expression(context, tokens, count, &index,
                                    &value->expression);
      if (status != CTOOL_OK) {
        return status;
      }
      value->element_count = 1u;
    }
    if (tail == (asm_data_value_t *)0) {
      statement->as.data.values = value;
    } else {
      tail->next = value;
    }
    tail = value;
    if (statement->as.data.value_count >
        ASM_U32_MAX - value->element_count) {
      asm_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_LAYOUT,
               tokens[directive_index].line,
               tokens[directive_index].column,
               "data element count overflows");
      return CTOOL_ERR_OVERFLOW;
    }
    statement->as.data.value_count += value->element_count;
    if (index == count) {
      break;
    }
    if (tokens[index].kind != ASM_TOKEN_COMMA) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
               tokens[index].line, tokens[index].column,
               "expected comma between data expressions");
      return CTOOL_ERR_INPUT;
    }
    index++;
    if (index == count) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
               tokens[index - 1u].line, tokens[index - 1u].column,
               "expected data expression after comma");
      return CTOOL_ERR_INPUT;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t asm_parse_reserve(asm_context_t *context,
                                        const asm_token_t *tokens,
                                        ctool_u32 count,
                                        ctool_u32 directive_index) {
  asm_statement_t *statement;
  ctool_u32 index = directive_index + 1u;
  ctool_u8 width = asm_reserve_width(tokens[directive_index].text);
  ctool_status_t status;
  if (width == 0u || index == count) {
    asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
             tokens[directive_index].line, tokens[directive_index].column,
             "reserve directive requires one count expression");
    return CTOOL_ERR_INPUT;
  }
  statement = asm_append_statement(context, ASM_STATEMENT_RESERVE,
                                   tokens[directive_index].line,
                                   tokens[directive_index].column);
  if (statement == (asm_statement_t *)0) {
    return context->failure_status;
  }
  statement->as.reserve.width = width;
  status = asm_parse_expression(context, tokens, count, &index,
                                &statement->as.reserve.count);
  if (status != CTOOL_OK) {
    return status;
  }
  if (index != count) {
    asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
             tokens[index].line, tokens[index].column,
             "reserve directive accepts one count expression");
    return CTOOL_ERR_INPUT;
  }
  return CTOOL_OK;
}

static ctool_status_t asm_parse_add(asm_context_t *context,
                                    const asm_token_t *tokens,
                                    ctool_u32 count,
                                    ctool_u32 *index,
                                    asm_expr_t **expression_out) {
  asm_expr_t *left;
  ctool_status_t status =
      asm_parse_multiply(context, tokens, count, index, &left);
  while (status == CTOOL_OK && *index < count &&
         (tokens[*index].kind == ASM_TOKEN_PLUS ||
          tokens[*index].kind == ASM_TOKEN_MINUS)) {
    const asm_token_t *operator_token = &tokens[*index];
    asm_expr_op_t op = tokens[*index].kind == ASM_TOKEN_PLUS
                           ? ASM_EXPR_OP_ADD
                           : ASM_EXPR_OP_SUBTRACT;
    asm_expr_t *right;
    (*index)++;
    status = asm_parse_multiply(context, tokens, count, index, &right);
    if (status == CTOOL_OK) {
      status = asm_make_binary(context, operator_token, op, left, right,
                               &left);
    }
  }
  if (status == CTOOL_OK) {
    *expression_out = left;
  }
  return status;
}

static ctool_status_t asm_parse_expression(asm_context_t *context,
                                           const asm_token_t *tokens,
                                           ctool_u32 count,
                                           ctool_u32 *index,
                                           asm_expr_t **expression_out) {
  asm_expr_t *left;
  ctool_status_t status =
      asm_parse_add(context, tokens, count, index, &left);
  while (status == CTOOL_OK && *index < count &&
         (tokens[*index].kind == ASM_TOKEN_SHIFT_LEFT ||
          tokens[*index].kind == ASM_TOKEN_SHIFT_RIGHT)) {
    const asm_token_t *operator_token = &tokens[*index];
    asm_expr_op_t op = tokens[*index].kind == ASM_TOKEN_SHIFT_LEFT
                           ? ASM_EXPR_OP_SHIFT_LEFT
                           : ASM_EXPR_OP_SHIFT_RIGHT;
    asm_expr_t *right;
    (*index)++;
    status = asm_parse_add(context, tokens, count, index, &right);
    if (status == CTOOL_OK) {
      status = asm_make_binary(context, operator_token, op, left, right,
                               &left);
    }
  }
  if (status == CTOOL_OK) {
    *expression_out = left;
  }
  return status;
}

static ctool_u16 asm_size_qualifier(ctool_string_t spelling) {
  if (asm_string_equal_case(spelling, "byte") == CTOOL_TRUE) {
    return 8u;
  }
  if (asm_string_equal_case(spelling, "word") == CTOOL_TRUE) {
    return 16u;
  }
  if (asm_string_equal_case(spelling, "dword") == CTOOL_TRUE) {
    return 32u;
  }
  if (asm_string_equal_case(spelling, "qword") == CTOOL_TRUE) {
    return 64u;
  }
  return 0u;
}

static ctool_status_t asm_parse_memory_operand(
    asm_context_t *context, const asm_token_t *tokens, ctool_u32 count,
    ctool_u32 *index, asm_operand_t *operand) {
  asm_expr_t *displacement = (asm_expr_t *)0;
  ctool_u16 address_bits = 0u;
  ctool_u32 cursor = *index + 1u;
  ctool_bool first = CTOOL_TRUE;
  operand->kind = ASM_OPERAND_MEMORY;
  operand->segment.class_id = CTOOL_X86_REG_NONE;
  operand->segment.index = 0u;
  operand->base.class_id = CTOOL_X86_REG_NONE;
  operand->base.index = 0u;
  operand->index.class_id = CTOOL_X86_REG_NONE;
  operand->index.index = 0u;
  operand->scale = 1u;
  if (cursor + 1u < count &&
      tokens[cursor].kind == ASM_TOKEN_IDENT &&
      tokens[cursor + 1u].kind == ASM_TOKEN_COLON) {
    ctool_x86_reg_t segment;
    ctool_status_t status =
        ctool_x86_register_from_name(tokens[cursor].text, &segment);
    if (status != CTOOL_OK || segment.class_id != CTOOL_X86_REG_SEGMENT) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
               tokens[cursor].line, tokens[cursor].column,
               "memory segment override requires a segment register");
      return CTOOL_ERR_INPUT;
    }
    operand->segment = segment;
    cursor += 2u;
  }
  while (cursor < count &&
         tokens[cursor].kind != ASM_TOKEN_RIGHT_BRACKET) {
    ctool_bool negative = CTOOL_FALSE;
    const asm_token_t *operator_token = &tokens[cursor];
    ctool_x86_reg_t reg;
    ctool_status_t register_status;
    if (tokens[cursor].kind == ASM_TOKEN_PLUS ||
        tokens[cursor].kind == ASM_TOKEN_MINUS) {
      negative = tokens[cursor].kind == ASM_TOKEN_MINUS;
      cursor++;
      if (cursor >= count ||
          tokens[cursor].kind == ASM_TOKEN_RIGHT_BRACKET) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
                 operator_token->line, operator_token->column,
                 "memory operator has no following term");
        return CTOOL_ERR_INPUT;
      }
    } else if (first == CTOOL_FALSE) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
               tokens[cursor].line, tokens[cursor].column,
               "expected plus or minus in memory address");
      return CTOOL_ERR_INPUT;
    }
    register_status =
        tokens[cursor].kind == ASM_TOKEN_IDENT
            ? ctool_x86_register_from_name(tokens[cursor].text, &reg)
            : CTOOL_ERR_NOT_FOUND;
    if (register_status == CTOOL_OK &&
        (reg.class_id == CTOOL_X86_REG_GPR16 ||
         reg.class_id == CTOOL_X86_REG_GPR32)) {
      ctool_u8 scale = 1u;
      ctool_u16 reg_bits = reg.class_id == CTOOL_X86_REG_GPR16 ? 16u : 32u;
      if (negative == CTOOL_TRUE) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
                 tokens[cursor].line, tokens[cursor].column,
                 "memory registers cannot be subtracted");
        return CTOOL_ERR_INPUT;
      }
      cursor++;
      if (cursor + 1u < count &&
          tokens[cursor].kind == ASM_TOKEN_STAR &&
          tokens[cursor + 1u].kind == ASM_TOKEN_NUMBER) {
        ctool_u64 scale_value = tokens[cursor + 1u].number;
        if (scale_value != 1ull && scale_value != 2ull &&
            scale_value != 4ull && scale_value != 8ull) {
          asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
                   tokens[cursor + 1u].line, tokens[cursor + 1u].column,
                   "memory index scale must be 1, 2, 4, or 8");
          return CTOOL_ERR_INPUT;
        }
        scale = (ctool_u8)scale_value;
        cursor += 2u;
      }
      if (address_bits != 0u && address_bits != reg_bits) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
                 tokens[cursor - 1u].line, tokens[cursor - 1u].column,
                 "memory address mixes 16-bit and 32-bit registers");
        return CTOOL_ERR_INPUT;
      }
      address_bits = reg_bits;
      if (scale != 1u || operand->base.class_id != CTOOL_X86_REG_NONE) {
        if (operand->index.class_id != CTOOL_X86_REG_NONE) {
          asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
                   tokens[cursor - 1u].line, tokens[cursor - 1u].column,
                   "memory address contains too many index registers");
          return CTOOL_ERR_INPUT;
        }
        operand->index = reg;
        operand->scale = scale;
      } else {
        operand->base = reg;
      }
    } else {
      asm_expr_t *term;
      ctool_status_t status = asm_parse_unary(
          context, tokens, count, &cursor, &term);
      if (status != CTOOL_OK) {
        return status;
      }
      if (negative == CTOOL_TRUE) {
        asm_expr_t *negated =
            asm_new_expression(context, ASM_EXPR_UNARY, operator_token);
        if (negated == (asm_expr_t *)0) {
          return context->failure_status;
        }
        negated->as.unary.op = ASM_EXPR_OP_NEGATIVE;
        negated->as.unary.operand = term;
        term = negated;
      }
      if (displacement == (asm_expr_t *)0) {
        displacement = term;
      } else {
        status = asm_make_binary(context, operator_token,
                                 ASM_EXPR_OP_ADD, displacement, term,
                                 &displacement);
        if (status != CTOOL_OK) {
          return status;
        }
      }
    }
    first = CTOOL_FALSE;
  }
  if (cursor >= count ||
      tokens[cursor].kind != ASM_TOKEN_RIGHT_BRACKET ||
      first == CTOOL_TRUE) {
    const asm_token_t *token = &tokens[*index];
    asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
             token->line, token->column,
             "memory operand has no address or closing bracket");
    return CTOOL_ERR_INPUT;
  }
  operand->expression = displacement;
  if (operand->base.class_id == CTOOL_X86_REG_NONE &&
      operand->index.class_id == CTOOL_X86_REG_GPR32 &&
      operand->scale == 2u) {
    operand->base = operand->index;
    operand->scale = 1u;
  }
  operand->address_bits = address_bits;
  *index = cursor + 1u;
  return CTOOL_OK;
}

static ctool_status_t asm_parse_instruction(asm_context_t *context,
                                            const asm_token_t *tokens,
                                            ctool_u32 count,
                                            ctool_u32 start) {
  asm_statement_t *statement;
  ctool_x86_mnemonic_t mnemonic;
  ctool_u32 mnemonic_index = start;
  ctool_u32 index;
  ctool_u8 prefixes = 0u;
  ctool_u16 address_bits = 0u;
  ctool_status_t status;
  while (mnemonic_index < count &&
         tokens[mnemonic_index].kind == ASM_TOKEN_IDENT) {
    if (asm_string_equal_case(tokens[mnemonic_index].text, "a16") ==
        CTOOL_TRUE) {
      address_bits = 16u;
    } else if (asm_string_equal_case(tokens[mnemonic_index].text, "a32") ==
               CTOOL_TRUE) {
      address_bits = 32u;
    } else if (asm_string_equal_case(tokens[mnemonic_index].text, "lock") ==
               CTOOL_TRUE) {
      prefixes |= CTOOL_X86_PREFIX_LOCK;
    } else if (asm_string_equal_case(tokens[mnemonic_index].text, "rep") ==
                   CTOOL_TRUE ||
               asm_string_equal_case(tokens[mnemonic_index].text, "repe") ==
                   CTOOL_TRUE ||
               asm_string_equal_case(tokens[mnemonic_index].text, "repz") ==
                   CTOOL_TRUE) {
      prefixes |= CTOOL_X86_PREFIX_REP;
    } else if (asm_string_equal_case(tokens[mnemonic_index].text, "repne") ==
                   CTOOL_TRUE ||
               asm_string_equal_case(tokens[mnemonic_index].text, "repnz") ==
                   CTOOL_TRUE) {
      prefixes |= CTOOL_X86_PREFIX_REPNE;
    } else {
      break;
    }
    mnemonic_index++;
  }
  if (mnemonic_index == count) {
    asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
             tokens[start].line, tokens[start].column,
             "instruction prefix has no mnemonic");
    return CTOOL_ERR_INPUT;
  }
  status = ctool_x86_mnemonic_from_name(tokens[mnemonic_index].text,
                                        &mnemonic);
  if (status != CTOOL_OK) {
    asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
             tokens[mnemonic_index].line, tokens[mnemonic_index].column,
             "unknown Cupid ASM instruction mnemonic");
    return CTOOL_ERR_INPUT;
  }
  statement = asm_append_statement(context, ASM_STATEMENT_INSTRUCTION,
                                   tokens[mnemonic_index].line,
                                   tokens[mnemonic_index].column);
  if (statement == (asm_statement_t *)0) {
    return context->failure_status;
  }
  statement->as.instruction.mnemonic = mnemonic;
  statement->as.instruction.prefixes = prefixes;
  statement->as.instruction.address_bits = address_bits;
  index = mnemonic_index + 1u;
  while (index < count) {
    asm_operand_t *operand;
    ctool_u16 width_bits = 0u;
    if (statement->as.instruction.operand_count ==
        CTOOL_X86_MAX_OPERANDS) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
               tokens[index].line, tokens[index].column,
               "too many instruction operands");
      return CTOOL_ERR_INPUT;
    }
    operand = &statement->as.instruction
                   .operands[statement->as.instruction.operand_count];
    if (tokens[index].kind == ASM_TOKEN_IDENT) {
      width_bits = asm_size_qualifier(tokens[index].text);
      if (width_bits != 0u) {
        index++;
        if (index == count) {
          asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
                   tokens[index - 1u].line, tokens[index - 1u].column,
                   "operand size has no following operand");
          return CTOOL_ERR_INPUT;
        }
      }
    }
    operand->width_bits = width_bits;
    if (tokens[index].kind == ASM_TOKEN_LEFT_BRACKET) {
      status = asm_parse_memory_operand(context, tokens, count, &index,
                                        operand);
      if (status != CTOOL_OK) {
        return status;
      }
    } else {
      status =
          ctool_x86_register_from_name(tokens[index].text, &operand->reg);
      if (tokens[index].kind == ASM_TOKEN_IDENT && status == CTOOL_OK &&
        (index + 1u == count ||
         tokens[index + 1u].kind == ASM_TOKEN_COMMA)) {
        operand->kind = ASM_OPERAND_REGISTER;
        index++;
      } else {
        operand->kind = ASM_OPERAND_EXPRESSION;
        status = asm_parse_expression(context, tokens, count, &index,
                                      &operand->expression);
        if (status != CTOOL_OK) {
          return status;
        }
        if (index < count && tokens[index].kind == ASM_TOKEN_COLON) {
          operand->kind = ASM_OPERAND_FAR_POINTER;
          index++;
          status = asm_parse_expression(
              context, tokens, count, &index,
              &operand->second_expression);
          if (status != CTOOL_OK) {
            return status;
          }
        }
      }
    }
    statement->as.instruction.operand_count++;
    if (index == count) {
      break;
    }
    if (tokens[index].kind != ASM_TOKEN_COMMA) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
               tokens[index].line, tokens[index].column,
               "expected comma between instruction operands");
      return CTOOL_ERR_INPUT;
    }
    index++;
    if (index == count) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
               tokens[index - 1u].line, tokens[index - 1u].column,
               "expected operand after comma");
      return CTOOL_ERR_INPUT;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t asm_parse_label(asm_context_t *context,
                                      const asm_token_t *token) {
  asm_symbol_t *symbol = asm_intern_symbol(
      context, token->text, token->line, token->column);
  asm_statement_t *label;
  if (symbol == (asm_symbol_t *)0) {
    return context->failure_status;
  }
  if (symbol->declared == CTOOL_TRUE || symbol->external == CTOOL_TRUE) {
    asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_DUPLICATE_SYMBOL,
             token->line, token->column,
             "duplicate Cupid ASM symbol definition");
    return CTOOL_ERR_INPUT;
  }
  symbol->declared = CTOOL_TRUE;
  symbol->kind = ASM_SYMBOL_LABEL;
  symbol->section = context->current_section;
  if (token->text.data[0] != '.') {
    context->current_global = symbol->name;
  }
  label = asm_append_statement(context, ASM_STATEMENT_LABEL,
                               token->line, token->column);
  if (label == (asm_statement_t *)0) {
    return context->failure_status;
  }
  label->as.label = symbol;
  return CTOOL_OK;
}

static ctool_status_t asm_parse_source_file(asm_context_t *context,
                                            const ctool_source_t *source);

static ctool_status_t asm_decode_string(asm_context_t *context,
                                        ctool_string_t encoded,
                                        ctool_string_t *decoded_out) {
  ctool_u32 size;
  ctool_u32 input = 0u;
  ctool_u32 output = 0u;
  char *storage;
  ctool_status_t status = asm_string_size(encoded, &size);
  if (status != CTOOL_OK) {
    return status;
  }
  status = ctool_arena_alloc(ctool_job_arena(context->job), size + 1u, 1u,
                             (void **)&storage);
  if (status != CTOOL_OK) {
    return status;
  }
  while (input < encoded.size) {
    ctool_u8 byte;
    status = asm_string_byte(encoded, &input, &byte);
    if (status != CTOOL_OK || byte == 0u) {
      return status == CTOOL_OK ? CTOOL_ERR_INPUT : status;
    }
    storage[output++] = (char)byte;
  }
  storage[output] = '\0';
  decoded_out->data = storage;
  decoded_out->size = output;
  return CTOOL_OK;
}

static ctool_bool asm_include_is_active(asm_context_t *context,
                                        const ctool_path_t *path) {
  ctool_u32 index;
  for (index = 0u; index < context->include_depth; index++) {
    if (ctool_path_equal(&context->include_stack[index], path) ==
        CTOOL_TRUE) {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_status_t asm_try_include_path(asm_context_t *context,
                                           const ctool_path_t *base,
                                           ctool_string_t spelling,
                                           ctool_source_t *source_out) {
  ctool_path_t path;
  ctool_status_t status = ctool_path_resolve(
      ctool_job_arena(context->job), base, spelling,
      ctool_default_limits().path_bytes, &path);
  if (status != CTOOL_OK) {
    return status;
  }
  if (asm_include_is_active(context, &path) == CTOOL_TRUE) {
    return CTOOL_ERR_PATH;
  }
  return ctool_job_load_source(context->job, &path, source_out);
}

static ctool_status_t asm_parse_include(asm_context_t *context,
                                        const asm_token_t *token) {
  ctool_string_t spelling;
  ctool_path_t parent;
  ctool_source_t included;
  ctool_status_t status = asm_decode_string(context, token->text, &spelling);
  ctool_u32 root_index = 0u;
  if (status == CTOOL_OK) {
    status = ctool_path_parent(&context->source->path, &parent);
  }
  if (status == CTOOL_OK) {
    status = asm_try_include_path(context, &parent, spelling, &included);
  }
  while (status == CTOOL_ERR_NOT_FOUND &&
         root_index < context->request->include_root_count) {
    status = asm_try_include_path(
        context, &context->request->include_roots[root_index], spelling,
        &included);
    root_index++;
  }
  if (status == CTOOL_ERR_PATH) {
    asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INCLUDE_CYCLE,
             token->line, token->column,
             "Cupid ASM include cycle detected");
    return CTOOL_ERR_INPUT;
  }
  if (status == CTOOL_ERR_NOT_FOUND) {
    asm_fail(context, status, CTOOL_ASM_DIAG_INCLUDE_NOT_FOUND,
             token->line, token->column,
             "Cupid ASM include file was not found");
    return status;
  }
  if (status != CTOOL_OK) {
    asm_fail(context, status, CTOOL_ASM_DIAG_INCLUDE_PATH,
             token->line, token->column,
             "Cupid ASM include path could not be resolved");
    return status;
  }
  return asm_parse_source_file(context, &included);
}

static ctool_status_t asm_parse_line(asm_context_t *context,
                                     const asm_token_t *tokens,
                                     ctool_u32 count) {
  ctool_u32 start = 0u;
  ctool_u32 end = count;
  if (count == 0u) {
    return CTOOL_OK;
  }
  if (tokens[0].kind == ASM_TOKEN_PREPROCESSOR) {
    asm_expr_t *definition;
    asm_symbol_t *symbol;
    ctool_u32 index = 2u;
    ctool_status_t status;
    if (asm_string_equal_case(tokens[0].text, "include") == CTOOL_TRUE) {
      if (count != 2u || tokens[1].kind != ASM_TOKEN_STRING) {
        asm_fail(context, CTOOL_ERR_INPUT,
                 CTOOL_ASM_DIAG_UNKNOWN_DIRECTIVE,
                 tokens[0].line, tokens[0].column,
                 "%include requires one quoted logical path");
        return CTOOL_ERR_INPUT;
      }
      return asm_parse_include(context, &tokens[1]);
    }
    if (asm_string_equal_case(tokens[0].text, "define") == CTOOL_FALSE ||
        count < 3u || tokens[1].kind != ASM_TOKEN_IDENT) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_UNKNOWN_DIRECTIVE,
               tokens[0].line, tokens[0].column,
               "unsupported Cupid ASM preprocessor directive");
      return CTOOL_ERR_INPUT;
    }
    status = asm_parse_expression(context, tokens, count, &index,
                                  &definition);
    if (status != CTOOL_OK) {
      return status;
    }
    if (index != count) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_EXPRESSION,
               tokens[index].line, tokens[index].column,
               "%define requires one object expression");
      return CTOOL_ERR_INPUT;
    }
    return asm_define_symbol(context, &tokens[1], ASM_SYMBOL_CONSTANT,
                             definition, &symbol);
  }
  if (tokens[0].kind == ASM_TOKEN_LEFT_BRACKET) {
    if (count < 3u || tokens[count - 1u].kind != ASM_TOKEN_RIGHT_BRACKET) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
               tokens[0].line, tokens[0].column,
               "bracketed directive has no closing bracket");
      return CTOOL_ERR_INPUT;
    }
    start = 1u;
    end = count - 1u;
  }
  if (start == 0u && end >= 3u &&
      tokens[0].kind == ASM_TOKEN_IDENT &&
      tokens[1].kind == ASM_TOKEN_IDENT &&
      asm_string_equal_case(tokens[1].text, "equ") == CTOOL_TRUE) {
    asm_expr_t *definition;
    asm_symbol_t *symbol;
    asm_statement_t *statement;
    ctool_u32 index = 2u;
    ctool_status_t status = asm_parse_expression(
        context, tokens, end, &index, &definition);
    if (status != CTOOL_OK) {
      return status;
    }
    if (index != end) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_EXPRESSION,
               tokens[index].line, tokens[index].column,
               "EQU requires one expression");
      return CTOOL_ERR_INPUT;
    }
    status = asm_define_symbol(context, &tokens[0], ASM_SYMBOL_ABSOLUTE,
                               definition, &symbol);
    if (status != CTOOL_OK) {
      return status;
    }
    statement = asm_append_statement(context, ASM_STATEMENT_EQU,
                                     tokens[0].line, tokens[0].column);
    if (statement == (asm_statement_t *)0) {
      return context->failure_status;
    }
    statement->as.equ = symbol;
    symbol->section = statement->section;
    return CTOOL_OK;
  }
  if (start == 0u && count >= 2u && tokens[0].kind == ASM_TOKEN_IDENT &&
      tokens[1].kind == ASM_TOKEN_COLON) {
    ctool_status_t status = asm_parse_label(context, &tokens[0]);
    if (status != CTOOL_OK) {
      return status;
    }
    start = 2u;
    if (start == end) {
      return CTOOL_OK;
    }
  }
  if (start == 0u && count >= 2u &&
      tokens[0].kind == ASM_TOKEN_IDENT &&
      tokens[1].kind == ASM_TOKEN_IDENT &&
      (asm_data_width(tokens[1].text) != 0u ||
       asm_reserve_width(tokens[1].text) != 0u)) {
    ctool_status_t status = asm_parse_label(context, &tokens[0]);
    if (status != CTOOL_OK) {
      return status;
    }
    start = 1u;
  }
  if (tokens[start].kind != ASM_TOKEN_IDENT) {
    asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
             tokens[start].line, tokens[start].column,
             "expected directive, label, or instruction");
    return CTOOL_ERR_INPUT;
  }
  if (asm_string_equal_case(tokens[start].text, "section") == CTOOL_TRUE ||
      asm_string_equal_case(tokens[start].text, "segment") == CTOOL_TRUE) {
    asm_section_t *section;
    if (start + 2u != end ||
        tokens[start + 1u].kind != ASM_TOKEN_IDENT) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_SECTION,
               tokens[start].line, tokens[start].column,
               "SECTION requires one section name");
      return CTOOL_ERR_INPUT;
    }
    section = asm_get_section(context, tokens[start + 1u].text,
                              tokens[start + 1u].line,
                              tokens[start + 1u].column);
    if (section == (asm_section_t *)0) {
      return context->failure_status;
    }
    context->current_section = section;
    return CTOOL_OK;
  }
  if (asm_string_equal_case(tokens[start].text, "global") == CTOOL_TRUE ||
      asm_string_equal_case(tokens[start].text, "extern") == CTOOL_TRUE) {
    ctool_bool external =
        asm_string_equal_case(tokens[start].text, "extern");
    ctool_u32 index = start + 1u;
    if (index == end) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
               tokens[start].line, tokens[start].column,
               "GLOBAL and EXTERN require at least one symbol");
      return CTOOL_ERR_INPUT;
    }
    while (index < end) {
      asm_symbol_t *symbol;
      if (tokens[index].kind != ASM_TOKEN_IDENT) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
                 tokens[index].line, tokens[index].column,
                 "expected a symbol name in declaration");
        return CTOOL_ERR_INPUT;
      }
      symbol = asm_intern_symbol(context, tokens[index].text,
                                 tokens[index].line,
                                 tokens[index].column);
      if (symbol == (asm_symbol_t *)0) {
        return context->failure_status;
      }
      if (external == CTOOL_TRUE && symbol->declared == CTOOL_TRUE) {
        asm_fail(context, CTOOL_ERR_INPUT,
                 CTOOL_ASM_DIAG_DUPLICATE_SYMBOL,
                 tokens[index].line, tokens[index].column,
                 "defined Cupid ASM symbol cannot be external");
        return CTOOL_ERR_INPUT;
      }
      symbol->global = CTOOL_TRUE;
      if (external == CTOOL_TRUE) {
        symbol->external = CTOOL_TRUE;
      }
      index++;
      if (index == end) {
        break;
      }
      if (tokens[index].kind != ASM_TOKEN_COMMA) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
                 tokens[index].line, tokens[index].column,
                 "expected comma between declared symbols");
        return CTOOL_ERR_INPUT;
      }
      index++;
      if (index == end) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
                 tokens[index - 1u].line, tokens[index - 1u].column,
                 "expected symbol after declaration comma");
        return CTOOL_ERR_INPUT;
      }
    }
    return CTOOL_OK;
  }
  if (asm_string_equal_case(tokens[start].text, "bits") == CTOOL_TRUE) {
    if (start + 2u != end || tokens[start + 1u].kind != ASM_TOKEN_NUMBER ||
        (tokens[start + 1u].number != 16u &&
         tokens[start + 1u].number != 32u)) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_MODE,
               tokens[start].line, tokens[start].column,
               "BITS requires 16 or 32");
      return CTOOL_ERR_INPUT;
    }
    context->mode = tokens[start + 1u].number == 16u
                        ? CTOOL_X86_MODE_16
                        : CTOOL_X86_MODE_32;
    return CTOOL_OK;
  }
  if (asm_string_equal_case(tokens[start].text, "org") == CTOOL_TRUE) {
    asm_expr_t *expression;
    ctool_u64 origin;
    ctool_u32 index = start + 1u;
    ctool_status_t status;
    if (context->request->artifact != CTOOL_ASM_ARTIFACT_RAW) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_ORIGIN,
               tokens[start].line, tokens[start].column,
               "ORG is only valid for raw assembly output");
      return CTOOL_ERR_INPUT;
    }
    status = asm_parse_expression(
        context, tokens, end, &index, &expression);
    if (status != CTOOL_OK || index != end) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_ORIGIN,
               tokens[start].line, tokens[start].column,
               "ORG requires one constant address");
      return CTOOL_ERR_INPUT;
    }
    status = asm_evaluate_expression(context, expression, 0u, CTOOL_TRUE,
                                     &origin);
    if (status != CTOOL_OK || origin > (ctool_u64)ASM_U32_MAX) {
      if (status == CTOOL_OK) {
        asm_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_INVALID_ORIGIN,
                 tokens[start].line, tokens[start].column,
                 "ORG address exceeds 32-bit range");
      }
      return status == CTOOL_OK ? CTOOL_ERR_OVERFLOW : status;
    }
    context->origin = (ctool_u32)origin;
    context->have_origin = CTOOL_TRUE;
    return CTOOL_OK;
  }
  if (asm_string_equal_case(tokens[start].text, "times") == CTOOL_TRUE) {
    asm_expr_t *repeat;
    ctool_u32 index = start + 1u;
    ctool_status_t status = asm_parse_expression(
        context, tokens, end, &index, &repeat);
    if (status != CTOOL_OK) {
      return status;
    }
    if (index >= end || tokens[index].kind != ASM_TOKEN_IDENT ||
        asm_data_width(tokens[index].text) == 0u) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_SYNTAX,
               tokens[start].line, tokens[start].column,
               "TIMES requires a data directive in this profile");
      return CTOOL_ERR_INPUT;
    }
    return asm_parse_data(context, tokens, end, index, repeat);
  }
  if (asm_data_width(tokens[start].text) != 0u) {
    return asm_parse_data(context, tokens, end, start, (asm_expr_t *)0);
  }
  if (asm_reserve_width(tokens[start].text) != 0u) {
    return asm_parse_reserve(context, tokens, end, start);
  }
  if (end != count) {
    asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_UNKNOWN_DIRECTIVE,
             tokens[start].line, tokens[start].column,
             "only BITS and ORG may use bracketed directive syntax");
    return CTOOL_ERR_INPUT;
  }
  return asm_parse_instruction(context, tokens, end, start);
}

static ctool_status_t asm_parse_source_file(asm_context_t *context,
                                            const ctool_source_t *source) {
  const ctool_source_t *previous = context->source;
  ctool_string_t previous_path = context->active_path;
  ctool_u32 position = 0u;
  ctool_u32 line_number = 1u;
  ctool_status_t result = CTOOL_OK;
  if (context->include_depth == CTOOL_ASM_MAX_INCLUDE_DEPTH) {
    asm_fail(context, CTOOL_ERR_LIMIT, CTOOL_ASM_DIAG_INCLUDE_DEPTH,
             0u, 0u, "Cupid ASM include depth limit exceeded");
    return CTOOL_ERR_LIMIT;
  }
  context->include_stack[context->include_depth] = source->path;
  context->include_depth++;
  context->source = source;
  context->active_path = source->path.text;
  while (result == CTOOL_OK && position < source->contents.size) {
    ctool_u32 line_start = position;
    ctool_u32 line_size;
    asm_token_t *tokens = (asm_token_t *)0;
    ctool_u32 token_count = 0u;
    ctool_status_t status;
    while (position < source->contents.size &&
           source->contents.data[position] != (ctool_u8)'\n') {
      position++;
    }
    line_size = position - line_start;
    status = line_size == 0u
                 ? CTOOL_OK
                 : ctool_arena_alloc_zero(
                       ctool_job_arena(context->job), line_size,
                       (ctool_u32)sizeof(*tokens),
                       (ctool_u32)sizeof(void *), (void **)&tokens);
    if (status != CTOOL_OK) {
      asm_fail(context, status, CTOOL_ASM_DIAG_LIMIT, line_number, 1u,
               "Cupid ASM line tokens exceed the job arena limit");
    } else {
      status = asm_tokenize_line(
          context,
          (const char *)(const void *)(source->contents.data + line_start),
          line_size, line_number, tokens, line_size, &token_count);
    }
    if (status == CTOOL_OK) {
      status = asm_parse_line(context, tokens, token_count);
    }
    if (status != CTOOL_OK) {
      result = status;
      break;
    }
    if (position < source->contents.size) {
      position++;
    }
    line_number++;
  }
  context->source = previous;
  context->active_path = previous_path;
  context->include_depth--;
  return result;
}

static ctool_status_t asm_parse_source(asm_context_t *context) {
  return asm_parse_source_file(context, context->source);
}

static ctool_u16 asm_register_width(ctool_x86_reg_t reg) {
  if (reg.class_id == CTOOL_X86_REG_GPR8) {
    return 8u;
  }
  if (reg.class_id == CTOOL_X86_REG_GPR16 ||
      reg.class_id == CTOOL_X86_REG_SEGMENT) {
    return 16u;
  }
  if (reg.class_id == CTOOL_X86_REG_GPR32 ||
      reg.class_id == CTOOL_X86_REG_CONTROL ||
      reg.class_id == CTOOL_X86_REG_DEBUG) {
    return 32u;
  }
  if (reg.class_id == CTOOL_X86_REG_X87) {
    return 80u;
  }
  if (reg.class_id == CTOOL_X86_REG_MMX) {
    return 64u;
  }
  if (reg.class_id == CTOOL_X86_REG_XMM) {
    return 128u;
  }
  return 0u;
}

static ctool_u16 asm_default_memory_width(ctool_x86_mnemonic_t mnemonic,
                                          ctool_x86_mode_t mode) {
  switch (mnemonic) {
    case CTOOL_X86_MN_LDMXCSR:
    case CTOOL_X86_MN_STMXCSR:
    case CTOOL_X86_MN_MOVSS:
    case CTOOL_X86_MN_ADDSS:
    case CTOOL_X86_MN_SUBSS:
    case CTOOL_X86_MN_MULSS:
    case CTOOL_X86_MN_DIVSS:
    case CTOOL_X86_MN_MINSS:
    case CTOOL_X86_MN_MAXSS:
    case CTOOL_X86_MN_SQRTSS:
    case CTOOL_X86_MN_UCOMISS:
    case CTOOL_X86_MN_FLD:
    case CTOOL_X86_MN_FST:
    case CTOOL_X86_MN_FSTP:
      return 32u;
    case CTOOL_X86_MN_MOVUPS:
    case CTOOL_X86_MN_MOVAPS:
    case CTOOL_X86_MN_MOVUPD:
    case CTOOL_X86_MN_MOVAPD:
    case CTOOL_X86_MN_ADDPS:
    case CTOOL_X86_MN_ADDPD:
    case CTOOL_X86_MN_SUBPS:
    case CTOOL_X86_MN_SUBPD:
    case CTOOL_X86_MN_MULPS:
    case CTOOL_X86_MN_MULPD:
    case CTOOL_X86_MN_DIVPS:
    case CTOOL_X86_MN_DIVPD:
    case CTOOL_X86_MN_MINPS:
    case CTOOL_X86_MN_MINPD:
    case CTOOL_X86_MN_MAXPS:
    case CTOOL_X86_MN_MAXPD:
      return 128u;
    case CTOOL_X86_MN_ADC:
    case CTOOL_X86_MN_ADD:
    case CTOOL_X86_MN_AND:
    case CTOOL_X86_MN_CMP:
    case CTOOL_X86_MN_DEC:
    case CTOOL_X86_MN_INC:
    case CTOOL_X86_MN_MOV:
    case CTOOL_X86_MN_NEG:
    case CTOOL_X86_MN_NOT:
    case CTOOL_X86_MN_OR:
    case CTOOL_X86_MN_POP:
    case CTOOL_X86_MN_PUSH:
    case CTOOL_X86_MN_RCL:
    case CTOOL_X86_MN_RCR:
    case CTOOL_X86_MN_ROL:
    case CTOOL_X86_MN_ROR:
    case CTOOL_X86_MN_SAR:
    case CTOOL_X86_MN_SBB:
    case CTOOL_X86_MN_SHL:
    case CTOOL_X86_MN_SHR:
    case CTOOL_X86_MN_SUB:
    case CTOOL_X86_MN_TEST:
    case CTOOL_X86_MN_XOR:
      return (ctool_u16)mode;
    default:
      return 0u;
  }
}

static ctool_bool asm_mnemonic_is_direct_branch(
    ctool_x86_mnemonic_t mnemonic) {
  switch (mnemonic) {
    case CTOOL_X86_MN_CALL:
    case CTOOL_X86_MN_JA:
    case CTOOL_X86_MN_JAE:
    case CTOOL_X86_MN_JB:
    case CTOOL_X86_MN_JBE:
    case CTOOL_X86_MN_JE:
    case CTOOL_X86_MN_JG:
    case CTOOL_X86_MN_JGE:
    case CTOOL_X86_MN_JL:
    case CTOOL_X86_MN_JLE:
    case CTOOL_X86_MN_JMP:
    case CTOOL_X86_MN_JNE:
    case CTOOL_X86_MN_JNO:
    case CTOOL_X86_MN_JNP:
    case CTOOL_X86_MN_JNS:
    case CTOOL_X86_MN_JO:
    case CTOOL_X86_MN_JP:
    case CTOOL_X86_MN_JS:
      return CTOOL_TRUE;
    default:
      return CTOOL_FALSE;
  }
}

static ctool_status_t asm_expression_raw_value(asm_context_t *context,
                                               const asm_expr_t *expression,
                                               ctool_u32 current_offset,
                                               ctool_u32 *value_out) {
  ctool_u64 value;
  ctool_status_t status = asm_evaluate_expression(
      context, expression, current_offset, CTOOL_TRUE, &value);
  if (status == CTOOL_OK) {
    *value_out = (ctool_u32)value;
  }
  return status;
}

typedef struct {
  asm_symbol_t *positive;
  asm_symbol_t *negative;
  ctool_u64 constant;
} asm_linear_expression_t;

static ctool_status_t asm_linear_merge(asm_context_t *context,
                                       asm_linear_expression_t *left,
                                       asm_linear_expression_t right,
                                       ctool_bool subtract,
                                       const asm_expr_t *expression) {
  asm_symbol_t *positive = subtract == CTOOL_TRUE ? right.negative
                                                   : right.positive;
  asm_symbol_t *negative = subtract == CTOOL_TRUE ? right.positive
                                                   : right.negative;
  if (positive != (asm_symbol_t *)0) {
    if (left->positive != (asm_symbol_t *)0) {
      asm_fail(context, CTOOL_ERR_INPUT,
               CTOOL_ASM_DIAG_NON_RELOCATABLE_EXPRESSION,
               expression->line, expression->column,
               "expression contains more than one positive symbol");
      return CTOOL_ERR_INPUT;
    }
    left->positive = positive;
  }
  if (negative != (asm_symbol_t *)0) {
    if (left->negative != (asm_symbol_t *)0) {
      asm_fail(context, CTOOL_ERR_INPUT,
               CTOOL_ASM_DIAG_NON_RELOCATABLE_EXPRESSION,
               expression->line, expression->column,
               "expression contains more than one negative symbol");
      return CTOOL_ERR_INPUT;
    }
    left->negative = negative;
  }
  if (subtract == CTOOL_TRUE) {
    left->constant -= right.constant;
  } else {
    if (left->constant > ASM_U64_MAX - right.constant) {
      asm_fail(context, CTOOL_ERR_OVERFLOW,
               CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW,
               expression->line, expression->column,
               "expression addition overflows");
      return CTOOL_ERR_OVERFLOW;
    }
    left->constant += right.constant;
  }
  return CTOOL_OK;
}

static ctool_status_t asm_linearize_expression(
    asm_context_t *context, const asm_expr_t *expression,
    ctool_u32 current_offset, asm_linear_expression_t *linear_out) {
  asm_linear_expression_t result;
  ctool_status_t status = CTOOL_OK;
  result.positive = (asm_symbol_t *)0;
  result.negative = (asm_symbol_t *)0;
  result.constant = 0ull;
  if (expression->kind == ASM_EXPR_LITERAL) {
    result.constant = expression->as.literal;
  } else if (expression->kind == ASM_EXPR_CURRENT) {
    result.constant = (ctool_u64)current_offset;
  } else if (expression->kind == ASM_EXPR_SECTION_BASE) {
    result.constant = 0ull;
  } else if (expression->kind == ASM_EXPR_SYMBOL) {
    asm_symbol_t *symbol = expression->as.symbol;
    if (symbol->declared == CTOOL_TRUE &&
        (symbol->kind == ASM_SYMBOL_CONSTANT ||
         symbol->kind == ASM_SYMBOL_ABSOLUTE)) {
      status = asm_evaluate_symbol(context, expression, current_offset,
                                   CTOOL_TRUE, &result.constant);
    } else {
      if (symbol->declared == CTOOL_FALSE &&
          symbol->external == CTOOL_FALSE &&
          context->request->allow_implicit_externs == CTOOL_FALSE) {
        asm_fail(context, CTOOL_ERR_NOT_FOUND,
                 CTOOL_ASM_DIAG_UNDEFINED_SYMBOL,
                 expression->line, expression->column,
                 "undefined Cupid ASM symbol");
        return CTOOL_ERR_NOT_FOUND;
      }
      result.positive = symbol;
    }
  } else if (expression->kind == ASM_EXPR_UNARY) {
    status = asm_linearize_expression(context, expression->as.unary.operand,
                                      current_offset, &result);
    if (status == CTOOL_OK &&
        expression->as.unary.op == ASM_EXPR_OP_NEGATIVE) {
      asm_symbol_t *swap = result.positive;
      result.positive = result.negative;
      result.negative = swap;
      result.constant = 0ull - result.constant;
    } else if (status == CTOOL_OK &&
               expression->as.unary.op == ASM_EXPR_OP_COMPLEMENT) {
      if (result.positive != (asm_symbol_t *)0 ||
          result.negative != (asm_symbol_t *)0) {
        asm_fail(context, CTOOL_ERR_INPUT,
                 CTOOL_ASM_DIAG_NON_RELOCATABLE_EXPRESSION,
                 expression->line, expression->column,
                 "cannot complement a relocatable expression");
        return CTOOL_ERR_INPUT;
      }
      result.constant = ~result.constant;
    }
  } else {
    asm_linear_expression_t right;
    status = asm_linearize_expression(context, expression->as.binary.left,
                                      current_offset, &result);
    if (status == CTOOL_OK) {
      status = asm_linearize_expression(context,
                                        expression->as.binary.right,
                                        current_offset, &right);
    }
    if (status == CTOOL_OK &&
        (expression->as.binary.op == ASM_EXPR_OP_ADD ||
         expression->as.binary.op == ASM_EXPR_OP_SUBTRACT)) {
      status = asm_linear_merge(
          context, &result, right,
          expression->as.binary.op == ASM_EXPR_OP_SUBTRACT,
          expression);
    } else if (status == CTOOL_OK) {
      if (result.positive != (asm_symbol_t *)0 ||
          result.negative != (asm_symbol_t *)0 ||
          right.positive != (asm_symbol_t *)0 ||
          right.negative != (asm_symbol_t *)0) {
        asm_fail(context, CTOOL_ERR_INPUT,
                 CTOOL_ASM_DIAG_NON_RELOCATABLE_EXPRESSION,
                 expression->line, expression->column,
                 "operator cannot be applied to relocatable symbols");
        return CTOOL_ERR_INPUT;
      }
      if (expression->as.binary.op == ASM_EXPR_OP_MULTIPLY) {
        if (right.constant != 0ull &&
            result.constant > ASM_U64_MAX / right.constant) {
          status = CTOOL_ERR_OVERFLOW;
        } else {
          result.constant *= right.constant;
        }
      } else if (expression->as.binary.op == ASM_EXPR_OP_SHIFT_LEFT) {
        if (right.constant >= 64ull ||
            result.constant > (ASM_U64_MAX >> (ctool_u32)right.constant)) {
          status = CTOOL_ERR_OVERFLOW;
        } else {
          result.constant <<= (ctool_u32)right.constant;
        }
      } else if (expression->as.binary.op == ASM_EXPR_OP_SHIFT_RIGHT) {
        if (right.constant >= 64ull) {
          status = CTOOL_ERR_OVERFLOW;
        } else {
          result.constant >>= (ctool_u32)right.constant;
        }
      }
      if (status == CTOOL_ERR_OVERFLOW) {
        asm_fail(context, status, CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW,
                 expression->line, expression->column,
                 "Cupid ASM expression arithmetic overflows");
      }
    }
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (result.positive != (asm_symbol_t *)0 &&
      result.negative != (asm_symbol_t *)0 &&
      result.positive->declared == CTOOL_TRUE &&
      result.negative->declared == CTOOL_TRUE &&
      result.positive->kind == ASM_SYMBOL_LABEL &&
      result.negative->kind == ASM_SYMBOL_LABEL &&
      result.positive->section == result.negative->section) {
    ctool_u64 difference =
        result.positive->value - result.negative->value;
    if (result.constant > ASM_U64_MAX - difference) {
      asm_fail(context, CTOOL_ERR_OVERFLOW,
               CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW,
               expression->line, expression->column,
               "label-difference expression overflows");
      return CTOOL_ERR_OVERFLOW;
    }
    result.constant += difference;
    result.positive = (asm_symbol_t *)0;
    result.negative = (asm_symbol_t *)0;
  }
  *linear_out = result;
  return CTOOL_OK;
}

static ctool_i32 asm_i32_from_bits(ctool_u32 bits) {
  if (bits <= 0x7fffffffu) {
    return (ctool_i32)bits;
  }
  return -1 - (ctool_i32)(0xffffffffu - bits);
}

static ctool_status_t asm_object_value(asm_context_t *context,
                                       const asm_expr_t *expression,
                                       const asm_statement_t *statement,
                                       ctool_bool pc_relative,
                                       ctool_x86_value_t *value_out) {
  asm_linear_expression_t linear;
  ctool_status_t status = asm_linearize_expression(
      context, expression, statement->offset, &linear);
  if (status != CTOOL_OK) {
    return status;
  }
  if (linear.negative != (asm_symbol_t *)0) {
    asm_fail(context, CTOOL_ERR_INPUT,
             CTOOL_ASM_DIAG_NON_RELOCATABLE_EXPRESSION,
             expression->line, expression->column,
             "expression has an unresolved negative symbol");
    return CTOOL_ERR_INPUT;
  }
  if (linear.positive != (asm_symbol_t *)0 &&
      pc_relative == CTOOL_TRUE &&
      linear.positive->declared == CTOOL_TRUE &&
      linear.positive->kind == ASM_SYMBOL_LABEL &&
      linear.positive->section == statement->section) {
    ctool_u32 place = statement->offset + statement->size;
    value_out->kind = CTOOL_X86_VALUE_CONSTANT;
    value_out->bits = (ctool_u32)(linear.positive->value +
                                  linear.constant - (ctool_u64)place);
    value_out->addend = 0;
    value_out->reference = 0u;
    return CTOOL_OK;
  }
  if (linear.positive == (asm_symbol_t *)0) {
    value_out->kind = CTOOL_X86_VALUE_CONSTANT;
    value_out->bits = (ctool_u32)linear.constant;
    value_out->addend = 0;
    value_out->reference = 0u;
  } else {
    value_out->kind = CTOOL_X86_VALUE_REFERENCE;
    value_out->bits = 0u;
    value_out->addend = asm_i32_from_bits((ctool_u32)linear.constant);
    value_out->reference = linear.positive->object_index;
  }
  return CTOOL_OK;
}

static ctool_status_t asm_fixed_value(asm_context_t *context,
                                      const asm_expr_t *expression,
                                      const asm_statement_t *statement,
                                      ctool_bool pc_relative,
                                      ctool_x86_value_t *value_out) {
  asm_linear_expression_t linear;
  ctool_u64 value;
  ctool_status_t status = asm_linearize_expression(
      context, expression, statement->offset, &linear);
  if (status != CTOOL_OK) {
    return status;
  }
  if (linear.negative != (asm_symbol_t *)0) {
    asm_fail(context, CTOOL_ERR_INPUT,
             CTOOL_ASM_DIAG_NON_RELOCATABLE_EXPRESSION,
             expression->line, expression->column,
             "fixed image expression has an unresolved negative symbol");
    return CTOOL_ERR_INPUT;
  }
  value = linear.constant;
  if (linear.positive != (asm_symbol_t *)0) {
    if (linear.positive->declared == CTOOL_FALSE ||
        linear.positive->kind != ASM_SYMBOL_LABEL ||
        linear.positive->section == (asm_section_t *)0) {
      asm_fail(context, CTOOL_ERR_NOT_FOUND,
               CTOOL_ASM_DIAG_UNDEFINED_SYMBOL,
               expression->line, expression->column,
               "fixed image contains an unresolved symbol");
      return CTOOL_ERR_NOT_FOUND;
    }
    if (linear.constant >
        ASM_U64_MAX - (ctool_u64)linear.positive->section->load_address ||
        linear.constant +
                (ctool_u64)linear.positive->section->load_address >
            ASM_U64_MAX - linear.positive->value) {
      asm_fail(context, CTOOL_ERR_OVERFLOW,
               CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW,
               expression->line, expression->column,
               "fixed image expression overflows");
      return CTOOL_ERR_OVERFLOW;
    }
    value += (ctool_u64)linear.positive->section->load_address +
             linear.positive->value;
  }
  if (pc_relative == CTOOL_TRUE) {
    ctool_u64 place = (ctool_u64)statement->section->load_address +
                      (ctool_u64)statement->offset +
                      (ctool_u64)statement->size;
    value -= place;
  }
  value_out->kind = CTOOL_X86_VALUE_CONSTANT;
  value_out->bits = (ctool_u32)value;
  value_out->addend = 0;
  value_out->reference = 0u;
  return CTOOL_OK;
}

static ctool_status_t asm_encode_statement(asm_context_t *context,
                                           asm_statement_t *statement,
                                           ctool_bool final,
                                           ctool_x86_encoding_t *encoding) {
  ctool_x86_instruction_t instruction;
  ctool_u32 operand_index;
  ctool_u16 operand_bits = 0u;
  instruction.mnemonic = statement->as.instruction.mnemonic;
  instruction.operand_bits = 0u;
  instruction.address_bits = statement->as.instruction.address_bits != 0u
                                 ? statement->as.instruction.address_bits
                                 : (ctool_u16)statement->mode;
  instruction.prefixes = statement->as.instruction.prefixes;
  instruction.operand_count = statement->as.instruction.operand_count;
  for (operand_index = 0u; operand_index < CTOOL_X86_MAX_OPERANDS;
       operand_index++) {
    ctool_x86_operand_t *target = &instruction.operands[operand_index];
    target->kind = CTOOL_X86_OPERAND_NONE;
    target->width_bits = 0u;
    target->encoding_bits = 0u;
    target->as.value.kind = CTOOL_X86_VALUE_CONSTANT;
    target->as.value.bits = 0u;
    target->as.value.addend = 0;
    target->as.value.reference = 0u;
  }
  for (operand_index = 0u;
       operand_index < (ctool_u32)instruction.operand_count;
       operand_index++) {
    const asm_operand_t *source =
        &statement->as.instruction.operands[operand_index];
    ctool_x86_operand_t *target = &instruction.operands[operand_index];
    if (source->kind == ASM_OPERAND_REGISTER) {
      target->kind = CTOOL_X86_OPERAND_REGISTER;
      target->width_bits = asm_register_width(source->reg);
      target->as.reg = source->reg;
      if (target->width_bits > operand_bits && target->width_bits <= 32u) {
        operand_bits = target->width_bits;
      }
    } else if (source->kind == ASM_OPERAND_MEMORY) {
      ctool_u64 wide_value = 0ull;
      ctool_status_t status = CTOOL_OK;
      target->kind = CTOOL_X86_OPERAND_MEMORY;
      target->width_bits =
          source->width_bits != 0u ? source->width_bits : operand_bits;
      if (target->width_bits > operand_bits && target->width_bits <= 32u) {
        operand_bits = target->width_bits;
      }
      target->as.memory.address_bits =
          source->address_bits != 0u
              ? source->address_bits
              : instruction.address_bits;
      target->as.memory.segment = source->segment;
      target->as.memory.base = source->base;
      target->as.memory.index = source->index;
      target->as.memory.scale = source->scale;
      target->as.memory.displacement.kind = CTOOL_X86_VALUE_CONSTANT;
      target->as.memory.displacement.bits = 0u;
      target->as.memory.displacement.addend = 0;
      target->as.memory.displacement.reference = 0u;
      target->as.memory.displacement_bits =
          source->base.class_id == CTOOL_X86_REG_NONE &&
                  source->index.class_id == CTOOL_X86_REG_NONE
              ? target->as.memory.address_bits
              : 0u;
      if (source->expression == (asm_expr_t *)0) {
        status = CTOOL_OK;
      } else if (final == CTOOL_TRUE &&
          context->request->artifact == CTOOL_ASM_ARTIFACT_ELF32_REL) {
        status = asm_object_value(context, source->expression, statement,
                                  CTOOL_FALSE,
                                  &target->as.memory.displacement);
      } else if (final == CTOOL_TRUE &&
                 context->request->artifact ==
                     CTOOL_ASM_ARTIFACT_FIXED_IMAGE) {
        status = asm_fixed_value(context, source->expression, statement,
                                 CTOOL_FALSE,
                                 &target->as.memory.displacement);
      } else {
        status = asm_evaluate_expression(context, source->expression,
                                         statement->offset, final,
                                         &wide_value);
        if (status == CTOOL_OK) {
          target->as.memory.displacement.bits = (ctool_u32)wide_value;
        } else if (final == CTOOL_FALSE &&
                   status == CTOOL_ERR_NOT_FOUND) {
          if (context->request->artifact ==
              CTOOL_ASM_ARTIFACT_ELF32_REL) {
            target->as.memory.displacement.kind =
                CTOOL_X86_VALUE_REFERENCE;
            target->as.memory.displacement.reference = 1u;
            target->as.memory.displacement_bits = 32u;
          }
          status = CTOOL_OK;
        }
      }
      if (status != CTOOL_OK) {
        return status;
      }
    } else if (source->kind == ASM_OPERAND_FAR_POINTER) {
      ctool_status_t status = CTOOL_OK;
      ctool_u64 offset = 0ull;
      ctool_u64 segment = 0ull;
      target->kind = CTOOL_X86_OPERAND_FAR_POINTER;
      target->width_bits = source->width_bits != 0u
                               ? source->width_bits
                               : (ctool_u16)statement->mode;
      if (target->width_bits <= 32u) {
        operand_bits = target->width_bits;
      }
      target->as.far_pointer.offset.kind = CTOOL_X86_VALUE_CONSTANT;
      target->as.far_pointer.offset.bits = 0u;
      target->as.far_pointer.offset.addend = 0;
      target->as.far_pointer.offset.reference = 0u;
      target->as.far_pointer.segment.kind = CTOOL_X86_VALUE_CONSTANT;
      target->as.far_pointer.segment.bits = 0u;
      target->as.far_pointer.segment.addend = 0;
      target->as.far_pointer.segment.reference = 0u;
      if (final == CTOOL_TRUE &&
          context->request->artifact == CTOOL_ASM_ARTIFACT_ELF32_REL) {
        status = asm_object_value(context, source->second_expression,
                                  statement, CTOOL_FALSE,
                                  &target->as.far_pointer.offset);
        if (status == CTOOL_OK) {
          status = asm_object_value(context, source->expression, statement,
                                    CTOOL_FALSE,
                                    &target->as.far_pointer.segment);
        }
      } else if (final == CTOOL_TRUE &&
                 context->request->artifact ==
                     CTOOL_ASM_ARTIFACT_FIXED_IMAGE) {
        status = asm_fixed_value(context, source->second_expression,
                                 statement, CTOOL_FALSE,
                                 &target->as.far_pointer.offset);
        if (status == CTOOL_OK) {
          status = asm_fixed_value(context, source->expression, statement,
                                   CTOOL_FALSE,
                                   &target->as.far_pointer.segment);
        }
      } else {
        status = asm_evaluate_expression(
            context, source->second_expression, statement->offset, final,
            &offset);
        if (status == CTOOL_OK) {
          status = asm_evaluate_expression(
              context, source->expression, statement->offset, final,
              &segment);
        }
        if (status == CTOOL_OK) {
          target->as.far_pointer.offset.bits = (ctool_u32)offset;
          target->as.far_pointer.segment.bits = (ctool_u32)segment;
        } else if (final == CTOOL_FALSE &&
                   status == CTOOL_ERR_NOT_FOUND) {
          target->as.far_pointer.offset.kind = CTOOL_X86_VALUE_REFERENCE;
          target->as.far_pointer.offset.reference = 1u;
          target->as.far_pointer.segment.bits = (ctool_u32)segment;
          status = CTOOL_OK;
        }
      }
      if (status != CTOOL_OK) {
        return status;
      }
    } else {
      ctool_u32 value = 0u;
      ctool_bool branch = operand_index == 0u &&
                          asm_mnemonic_is_direct_branch(
                              instruction.mnemonic) == CTOOL_TRUE;
      target->kind = branch == CTOOL_TRUE ? CTOOL_X86_OPERAND_RELATIVE
                                          : CTOOL_X86_OPERAND_IMMEDIATE;
      target->width_bits = operand_bits;
      if (source->width_bits != 0u) {
        target->encoding_bits = source->width_bits;
      }
      target->as.value.kind = CTOOL_X86_VALUE_CONSTANT;
      if (branch == CTOOL_TRUE) {
        if (instruction.mnemonic != CTOOL_X86_MN_CALL) {
          target->encoding_bits =
              statement->as.instruction.branch_bits != 0u
                  ? statement->as.instruction.branch_bits
                  : 8u;
        } else {
          target->encoding_bits = (ctool_u16)statement->mode;
        }
        if (final == CTOOL_TRUE &&
            context->request->artifact == CTOOL_ASM_ARTIFACT_ELF32_REL) {
          ctool_status_t status = asm_object_value(
              context, source->expression, statement, CTOOL_TRUE,
              &target->as.value);
          if (status != CTOOL_OK) {
            return status;
          }
        } else if (final == CTOOL_TRUE &&
                   context->request->artifact ==
                       CTOOL_ASM_ARTIFACT_FIXED_IMAGE) {
          ctool_status_t status = asm_fixed_value(
              context, source->expression, statement, CTOOL_TRUE,
              &target->as.value);
          if (status != CTOOL_OK) {
            return status;
          }
        } else if (final == CTOOL_TRUE) {
          ctool_status_t status = asm_expression_raw_value(
              context, source->expression, statement->offset, &value);
          ctool_u32 place;
          if (status != CTOOL_OK) {
            return status;
          }
          if (context->origin > ASM_U32_MAX - statement->offset ||
              context->origin + statement->offset >
                  ASM_U32_MAX - statement->size) {
            return CTOOL_ERR_OVERFLOW;
          }
          place = context->origin + statement->offset + statement->size;
          value -= place;
          target->as.value.bits = value;
        } else if (instruction.mnemonic == CTOOL_X86_MN_CALL) {
          target->as.value.kind = CTOOL_X86_VALUE_REFERENCE;
          target->as.value.reference = 1u;
        }
      } else {
        if (final == CTOOL_TRUE &&
            context->request->artifact == CTOOL_ASM_ARTIFACT_ELF32_REL) {
          ctool_status_t status = asm_object_value(
              context, source->expression, statement, CTOOL_FALSE,
              &target->as.value);
          if (status != CTOOL_OK) {
            return status;
          }
        } else if (final == CTOOL_TRUE &&
                   context->request->artifact ==
                       CTOOL_ASM_ARTIFACT_FIXED_IMAGE) {
          ctool_status_t status = asm_fixed_value(
              context, source->expression, statement, CTOOL_FALSE,
              &target->as.value);
          if (status != CTOOL_OK) {
            return status;
          }
        } else if (final == CTOOL_FALSE &&
                   context->request->artifact !=
                       CTOOL_ASM_ARTIFACT_RAW) {
          asm_linear_expression_t linear;
          ctool_status_t status = asm_linearize_expression(
              context, source->expression, statement->offset, &linear);
          if (status != CTOOL_OK) {
            return status;
          }
          if (linear.negative != (asm_symbol_t *)0) {
            return CTOOL_ERR_INPUT;
          }
          if (linear.positive != (asm_symbol_t *)0) {
            target->as.value.kind = CTOOL_X86_VALUE_REFERENCE;
            target->as.value.reference = 1u;
            target->as.value.addend =
                asm_i32_from_bits((ctool_u32)linear.constant);
            target->encoding_bits = operand_bits != 0u
                                        ? operand_bits
                                        : (ctool_u16)statement->mode;
          } else {
            target->as.value.bits = (ctool_u32)linear.constant;
          }
        } else {
          ctool_u64 wide_value;
          ctool_status_t status = asm_evaluate_expression(
              context, source->expression, statement->offset, final,
              &wide_value);
          if (status == CTOOL_OK) {
            value = (ctool_u32)wide_value;
            target->as.value.bits = value;
          } else if (final == CTOOL_FALSE &&
                     status == CTOOL_ERR_NOT_FOUND) {
            if (context->request->artifact ==
                CTOOL_ASM_ARTIFACT_ELF32_REL) {
              target->as.value.kind = CTOOL_X86_VALUE_REFERENCE;
              target->as.value.reference = 1u;
              target->encoding_bits = operand_bits != 0u
                                          ? operand_bits
                                          : (ctool_u16)statement->mode;
            }
            status = CTOOL_OK;
          } else {
            return status;
          }
        }
      }
    }
  }
  instruction.operand_bits = operand_bits;
  for (operand_index = 0u;
       operand_index < (ctool_u32)instruction.operand_count;
       operand_index++) {
    ctool_x86_operand_t *operand = &instruction.operands[operand_index];
    if (operand->kind == CTOOL_X86_OPERAND_MEMORY &&
        operand->width_bits == 0u) {
      ctool_u16 default_width = asm_default_memory_width(
          instruction.mnemonic, statement->mode);
      if (instruction.mnemonic == CTOOL_X86_MN_LGDT ||
          instruction.mnemonic == CTOOL_X86_MN_LIDT ||
          instruction.mnemonic == CTOOL_X86_MN_SGDT ||
          instruction.mnemonic == CTOOL_X86_MN_SIDT) {
        operand->width_bits = 48u;
      } else if (operand_bits != 0u) {
        operand->width_bits = operand_bits;
      } else if (default_width != 0u) {
        operand->width_bits = default_width;
        if (operand_bits == 0u && default_width <= 32u) {
          operand_bits = default_width;
        }
      }
    }
  }
  instruction.operand_bits = operand_bits;
  return ctool_x86_encode(context->job, statement->mode, &instruction,
                          CTOOL_X86_FORM_AUTO, encoding);
}

static ctool_status_t asm_fixed_layout_sections(
    asm_context_t *context, ctool_u32 *code_file_out,
    ctool_u32 *code_memory_out, ctool_u32 *data_file_out,
    ctool_u32 *data_memory_out);

static ctool_status_t asm_layout_pass(asm_context_t *context) {
  asm_statement_t *statement = context->statements;
  asm_section_t *section = context->sections;
  asm_symbol_t *symbol = context->symbols;
  while (symbol != (asm_symbol_t *)0) {
    if (symbol->kind == ASM_SYMBOL_LABEL ||
        (symbol->kind == ASM_SYMBOL_ABSOLUTE &&
         symbol->definition != (asm_expr_t *)0)) {
      symbol->resolved = CTOOL_FALSE;
    }
    symbol = symbol->next;
  }
  while (section != (asm_section_t *)0) {
    section->size = 0u;
    section = section->next;
  }
  while (statement != (asm_statement_t *)0) {
    ctool_u32 offset = statement->section->size;
    context->active_path = statement->path;
    statement->offset = offset;
    if (statement->kind == ASM_STATEMENT_LABEL) {
      statement->as.label->value = (ctool_u64)offset;
      statement->as.label->resolved = CTOOL_TRUE;
      statement->size = 0u;
    } else if (statement->kind == ASM_STATEMENT_EQU) {
      statement->as.equ->definition_offset = offset;
      statement->size = 0u;
    } else if (statement->kind == ASM_STATEMENT_DATA) {
      ctool_u64 repeat = 1ull;
      ctool_u64 size;
      ctool_status_t status = CTOOL_OK;
      if (statement->as.data.repeat != (asm_expr_t *)0) {
        status = asm_evaluate_expression(context, statement->as.data.repeat,
                                         offset, CTOOL_TRUE, &repeat);
      }
      if (status != CTOOL_OK || repeat > (ctool_u64)ASM_U32_MAX) {
        if (status == CTOOL_OK) {
          asm_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_LAYOUT,
                   statement->line, statement->column,
                   "TIMES repeat count exceeds 32-bit range");
        }
        return status == CTOOL_OK ? CTOOL_ERR_OVERFLOW : status;
      }
      size = repeat * (ctool_u64)statement->as.data.value_count;
      if (statement->as.data.value_count != 0u &&
          size / (ctool_u64)statement->as.data.value_count != repeat) {
        asm_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_LAYOUT,
                 statement->line, statement->column,
                 "repeated data count overflows");
        return CTOOL_ERR_OVERFLOW;
      }
      if (size > (ctool_u64)ASM_U32_MAX /
                     (ctool_u64)statement->as.data.width) {
        asm_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_LAYOUT,
                 statement->line, statement->column,
                 "repeated data exceeds 32-bit layout range");
        return CTOOL_ERR_OVERFLOW;
      }
      statement->as.data.repeat_count = (ctool_u32)repeat;
      statement->size =
          (ctool_u32)(size * (ctool_u64)statement->as.data.width);
    } else if (statement->kind == ASM_STATEMENT_RESERVE) {
      ctool_u64 count;
      ctool_status_t status = asm_evaluate_expression(
          context, statement->as.reserve.count, offset, CTOOL_TRUE, &count);
      if (status != CTOOL_OK ||
          count > (ctool_u64)ASM_U32_MAX /
                      (ctool_u64)statement->as.reserve.width) {
        if (status == CTOOL_OK) {
          asm_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_LAYOUT,
                   statement->line, statement->column,
                   "reserve directive exceeds 32-bit layout range");
          status = CTOOL_ERR_OVERFLOW;
        }
        return status;
      }
      statement->as.reserve.element_count = (ctool_u32)count;
      statement->size =
          (ctool_u32)(count * (ctool_u64)statement->as.reserve.width);
    } else {
      ctool_x86_encoding_t encoding;
      ctool_status_t status =
          asm_encode_statement(context, statement, CTOOL_FALSE, &encoding);
      if (status != CTOOL_OK) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_ENCODING,
                 statement->line, statement->column,
                 "instruction has no supported x86 encoding");
        return status;
      }
      statement->size = (ctool_u32)encoding.size;
    }
    if (offset > ASM_U32_MAX - statement->size) {
      asm_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_LAYOUT,
               statement->line, statement->column,
               "raw assembly layout exceeds 32-bit range");
      return CTOOL_ERR_OVERFLOW;
    }
    statement->section->size = offset + statement->size;
    statement = statement->next;
  }
  return CTOOL_OK;
}

static ctool_bool asm_branch_fits_i8(ctool_u32 bits) {
  return bits <= 0x7fu || bits >= 0xffffff80u ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_status_t asm_relax_branches(asm_context_t *context,
                                         ctool_bool *changed_out) {
  asm_statement_t *statement = context->statements;
  *changed_out = CTOOL_FALSE;
  while (statement != (asm_statement_t *)0) {
    context->active_path = statement->path;
    if (statement->kind == ASM_STATEMENT_INSTRUCTION &&
        statement->as.instruction.mnemonic != CTOOL_X86_MN_CALL &&
        asm_mnemonic_is_direct_branch(
            statement->as.instruction.mnemonic) == CTOOL_TRUE &&
        statement->as.instruction.operand_count != 0u &&
        statement->as.instruction.operands[0].kind ==
            ASM_OPERAND_EXPRESSION &&
        statement->as.instruction.branch_bits == 0u) {
      const asm_expr_t *expression =
          statement->as.instruction.operands[0].expression;
      ctool_bool needs_near = CTOOL_FALSE;
      ctool_u32 displacement = 0u;
      ctool_status_t status = CTOOL_OK;
      if (context->request->artifact == CTOOL_ASM_ARTIFACT_ELF32_REL) {
        asm_linear_expression_t linear;
        status = asm_linearize_expression(context, expression,
                                          statement->offset, &linear);
        if (status == CTOOL_OK &&
            linear.negative != (asm_symbol_t *)0) {
          status = CTOOL_ERR_INPUT;
        }
        if (status == CTOOL_OK &&
            linear.positive != (asm_symbol_t *)0 &&
            (linear.positive->declared == CTOOL_FALSE ||
             linear.positive->kind != ASM_SYMBOL_LABEL ||
             linear.positive->section != statement->section)) {
          needs_near = CTOOL_TRUE;
        } else if (status == CTOOL_OK) {
          ctool_u64 target = linear.constant;
          ctool_u32 place = statement->offset + statement->size;
          if (linear.positive != (asm_symbol_t *)0) {
            target += linear.positive->value;
          }
          displacement = (ctool_u32)(target - (ctool_u64)place);
        }
      } else {
        ctool_u64 target;
        status = asm_evaluate_expression(context, expression,
                                         statement->offset, CTOOL_TRUE,
                                         &target);
        if (status == CTOOL_OK) {
          ctool_u64 place =
              (context->request->artifact ==
                       CTOOL_ASM_ARTIFACT_FIXED_IMAGE
                   ? (ctool_u64)statement->section->load_address
                   : (ctool_u64)context->origin) +
              (ctool_u64)statement->offset +
              (ctool_u64)statement->size;
          displacement = (ctool_u32)(target - place);
        }
      }
      if (status != CTOOL_OK) {
        if (context->failure_status == CTOOL_OK) {
          asm_fail(context, status, CTOOL_ASM_DIAG_LAYOUT,
                   statement->line, statement->column,
                   "branch target could not be laid out");
        }
        return status;
      }
      if (needs_near == CTOOL_TRUE ||
          asm_branch_fits_i8(displacement) == CTOOL_FALSE) {
        statement->as.instruction.branch_bits =
            (ctool_u16)statement->mode;
        *changed_out = CTOOL_TRUE;
      }
    }
    statement = statement->next;
  }
  return CTOOL_OK;
}

static ctool_status_t asm_layout(asm_context_t *context) {
  ctool_bool changed = CTOOL_TRUE;
  ctool_u32 passes = 0u;
  ctool_status_t status = CTOOL_OK;
  while (status == CTOOL_OK && changed == CTOOL_TRUE) {
    status = asm_layout_pass(context);
    if (status == CTOOL_OK &&
        context->request->artifact == CTOOL_ASM_ARTIFACT_FIXED_IMAGE) {
      ctool_u32 code_file;
      ctool_u32 code_memory;
      ctool_u32 data_file;
      ctool_u32 data_memory;
      status = asm_fixed_layout_sections(
          context, &code_file, &code_memory, &data_file, &data_memory);
    }
    if (status == CTOOL_OK) {
      status = asm_relax_branches(context, &changed);
    }
    passes++;
    if (status == CTOOL_OK && changed == CTOOL_TRUE &&
        passes == 65535u) {
      asm_fail(context, CTOOL_ERR_LIMIT, CTOOL_ASM_DIAG_LAYOUT, 0u, 0u,
               "CupidASM branch relaxation did not converge");
      status = CTOOL_ERR_LIMIT;
    }
  }
  return status;
}

static ctool_status_t asm_emit_raw(asm_context_t *context,
                                   ctool_buffer_t *output) {
  asm_statement_t *statement = context->statements;
  while (statement != (asm_statement_t *)0) {
    ctool_status_t status = CTOOL_OK;
    context->active_path = statement->path;
    if (ctool_buffer_view(output).size != statement->offset) {
      return CTOOL_ERR_INTERNAL;
    }
    if (statement->kind == ASM_STATEMENT_INSTRUCTION) {
      ctool_x86_encoding_t encoding;
      status = asm_encode_statement(context, statement, CTOOL_TRUE,
                                    &encoding);
      if (status == CTOOL_OK &&
          (ctool_u32)encoding.size != statement->size) {
        asm_fail(context, CTOOL_ERR_INTERNAL, CTOOL_ASM_DIAG_LAYOUT,
                 statement->line, statement->column,
                 "raw instruction changed size after layout");
        status = CTOOL_ERR_INTERNAL;
      }
      if (status == CTOOL_OK) {
        status = ctool_buffer_append(
            output, ctool_bytes(encoding.bytes, (ctool_u32)encoding.size));
      }
    } else if (statement->kind == ASM_STATEMENT_DATA) {
      ctool_u32 repeat_index;
      for (repeat_index = 0u;
           status == CTOOL_OK &&
           repeat_index < statement->as.data.repeat_count;
           repeat_index++) {
        asm_data_value_t *value = statement->as.data.values;
        while (status == CTOOL_OK && value != (asm_data_value_t *)0) {
          if (value->is_string == CTOOL_TRUE) {
            ctool_u32 string_index = 0u;
            while (status == CTOOL_OK &&
                   string_index < value->string.size) {
              ctool_u8 byte;
              status = asm_string_byte(value->string, &string_index,
                                       &byte);
              if (status == CTOOL_OK) {
                status = ctool_buffer_put_u8(output, byte);
              }
            }
          } else {
            ctool_u64 evaluated;
            ctool_u32 current_offset = ctool_buffer_view(output).size;
            status = asm_evaluate_expression(
                context, value->expression, current_offset, CTOOL_TRUE,
                &evaluated);
            if (status == CTOOL_OK && statement->as.data.width == 1u) {
              status = ctool_buffer_put_u8(output, (ctool_u8)evaluated);
            } else if (status == CTOOL_OK &&
                       statement->as.data.width == 2u) {
              status = ctool_buffer_put_le16(output,
                                             (ctool_u16)evaluated);
            } else if (status == CTOOL_OK &&
                       statement->as.data.width == 4u) {
              status = ctool_buffer_put_le32(output,
                                             (ctool_u32)evaluated);
            } else if (status == CTOOL_OK &&
                       statement->as.data.width == 8u) {
              status = ctool_buffer_put_le64(output, evaluated);
            }
          }
          value = value->next;
        }
      }
    } else if (statement->kind == ASM_STATEMENT_RESERVE) {
      status = ctool_buffer_fill(output, 0u, statement->size);
    }
    if (status != CTOOL_OK) {
      asm_fail(context, status,
               statement->kind == ASM_STATEMENT_INSTRUCTION
                   ? CTOOL_ASM_DIAG_ENCODING
                   : CTOOL_ASM_DIAG_OUTPUT,
               statement->line, statement->column,
               statement->kind == ASM_STATEMENT_INSTRUCTION
                   ? "instruction could not be encoded"
                   : "raw data could not be emitted");
      return status;
    }
    statement = statement->next;
  }
  return CTOOL_OK;
}

static ctool_status_t asm_object_counts(
    asm_context_t *context, ctool_u32 *symbol_count_out,
    ctool_u32 *relocation_capacity_out) {
  asm_symbol_t *symbol = context->symbols;
  asm_statement_t *statement = context->statements;
  ctool_u32 symbol_count = 0u;
  ctool_u32 relocation_capacity = 0u;
  while (symbol != (asm_symbol_t *)0) {
    context->active_path = symbol->path;
    if (symbol->kind != ASM_SYMBOL_CONSTANT) {
      if (symbol_count == ASM_U32_MAX) {
        return CTOOL_ERR_OVERFLOW;
      }
      symbol_count++;
    }
    symbol = symbol->next;
  }
  while (statement != (asm_statement_t *)0) {
    ctool_u32 addition = 0u;
    if (statement->kind == ASM_STATEMENT_INSTRUCTION) {
      addition = CTOOL_X86_MAX_FIELDS;
    } else if (statement->kind == ASM_STATEMENT_DATA) {
      ctool_u64 count =
          (ctool_u64)statement->as.data.repeat_count *
          (ctool_u64)statement->as.data.value_count;
      if (count > (ctool_u64)ASM_U32_MAX) {
        return CTOOL_ERR_OVERFLOW;
      }
      addition = (ctool_u32)count;
    }
    if (relocation_capacity > ASM_U32_MAX - addition) {
      return CTOOL_ERR_OVERFLOW;
    }
    relocation_capacity += addition;
    statement = statement->next;
  }
  *symbol_count_out = symbol_count;
  *relocation_capacity_out = relocation_capacity;
  return CTOOL_OK;
}

static ctool_status_t asm_object_prepare_sections(
    asm_context_t *context, ctool_elf32_section_spec_t **specs_out) {
  ctool_elf32_section_spec_t *specs;
  asm_section_t *section = context->sections;
  ctool_status_t status = ctool_arena_alloc_zero(
      ctool_job_arena(context->job), context->section_count,
      (ctool_u32)sizeof(*specs), (ctool_u32)sizeof(void *),
      (void **)&specs);
  if (status != CTOOL_OK) {
    asm_fail(context, status, CTOOL_ASM_DIAG_LIMIT, 0u, 0u,
             "CupidASM object section table exceeds the job limit");
    return status;
  }
  while (section != (asm_section_t *)0) {
    ctool_elf32_section_spec_t *spec = &specs[section->index];
    spec->name = section->name;
    spec->type = section->type;
    spec->flags = section->flags;
    spec->alignment = section->alignment;
    spec->entry_size = 0u;
    spec->size = section->size;
    if (section->type == CTOOL_ELF32_SHT_PROGBITS && section->size != 0u) {
      status = ctool_arena_alloc_zero(ctool_job_arena(context->job),
                                      section->size, 1u, 1u,
                                      (void **)&section->contents);
      if (status != CTOOL_OK) {
        asm_fail(context, status, CTOOL_ASM_DIAG_LIMIT, 0u, 0u,
                 "CupidASM object section contents exceed the job limit");
        return status;
      }
      spec->contents = ctool_bytes(section->contents, section->size);
    } else {
      section->contents = (ctool_u8 *)0;
      spec->contents = ctool_bytes((const void *)0, 0u);
    }
    section = section->next;
  }
  *specs_out = specs;
  return CTOOL_OK;
}

static ctool_status_t asm_object_prepare_symbols(
    asm_context_t *context, ctool_u32 symbol_count,
    ctool_elf32_symbol_spec_t **specs_out) {
  ctool_elf32_symbol_spec_t *specs =
      (ctool_elf32_symbol_spec_t *)0;
  asm_symbol_t *symbol = context->symbols;
  ctool_u32 index = 0u;
  ctool_status_t status = CTOOL_OK;
  if (symbol_count != 0u) {
    status = ctool_arena_alloc_zero(
        ctool_job_arena(context->job), symbol_count,
        (ctool_u32)sizeof(*specs), (ctool_u32)sizeof(void *),
        (void **)&specs);
  }
  if (status != CTOOL_OK) {
    asm_fail(context, status, CTOOL_ASM_DIAG_LIMIT, 0u, 0u,
             "CupidASM object symbol table exceeds the job limit");
    return status;
  }
  while (symbol != (asm_symbol_t *)0) {
    context->active_path = symbol->path;
    if (symbol->kind != ASM_SYMBOL_CONSTANT) {
      ctool_elf32_symbol_spec_t *spec = &specs[index];
      if (symbol->declared == CTOOL_FALSE &&
          symbol->external == CTOOL_FALSE &&
          context->request->allow_implicit_externs == CTOOL_FALSE) {
        asm_fail(context, CTOOL_ERR_NOT_FOUND,
                 CTOOL_ASM_DIAG_UNDEFINED_SYMBOL, 0u, 0u,
                 "undefined Cupid ASM object symbol");
        return CTOOL_ERR_NOT_FOUND;
      }
      if (symbol->declared == CTOOL_FALSE) {
        symbol->external = CTOOL_TRUE;
        symbol->global = CTOOL_TRUE;
      }
      symbol->object_index = index;
      spec->name = symbol->name;
      spec->binding = symbol->global == CTOOL_TRUE
                          ? CTOOL_ELF32_BIND_GLOBAL
                          : CTOOL_ELF32_BIND_LOCAL;
      spec->type = CTOOL_ELF32_SYMBOL_NOTYPE;
      spec->visibility = CTOOL_ELF32_VIS_DEFAULT;
      spec->section = CTOOL_ELF32_NO_SECTION;
      spec->size = 0u;
      spec->alignment = 0u;
      if (symbol->external == CTOOL_TRUE) {
        spec->placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
        spec->value = 0u;
      } else if (symbol->kind == ASM_SYMBOL_LABEL) {
        spec->placement = CTOOL_ELF32_SYMBOL_DEFINED;
        spec->section = symbol->section->index;
        spec->value = (ctool_u32)symbol->value;
      } else {
        ctool_u64 value;
        asm_expr_t wrapper;
        wrapper.kind = ASM_EXPR_SYMBOL;
        wrapper.line = 0u;
        wrapper.column = 0u;
        wrapper.as.symbol = symbol;
        status = asm_evaluate_symbol(context, &wrapper, 0u, CTOOL_TRUE,
                                     &value);
        if (status != CTOOL_OK || value > (ctool_u64)ASM_U32_MAX) {
          if (status == CTOOL_OK) {
            asm_fail(context, CTOOL_ERR_OVERFLOW,
                     CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW, 0u, 0u,
                     "absolute object symbol exceeds 32-bit range");
            status = CTOOL_ERR_OVERFLOW;
          }
          return status;
        }
        spec->placement = CTOOL_ELF32_SYMBOL_ABSOLUTE;
        spec->value = (ctool_u32)value;
      }
      index++;
    }
    symbol = symbol->next;
  }
  *specs_out = specs;
  return CTOOL_OK;
}

static ctool_status_t asm_object_add_relocation(
    asm_context_t *context, ctool_elf32_relocation_spec_t *relocations,
    ctool_u32 capacity, ctool_u32 *count,
    const asm_section_t *section, ctool_u32 offset,
    ctool_u32 symbol, ctool_elf32_relocation_type_t type,
    ctool_i32 addend, ctool_u32 line, ctool_u32 column) {
  ctool_elf32_relocation_spec_t *relocation;
  if (*count == capacity) {
    asm_fail(context, CTOOL_ERR_INTERNAL, CTOOL_ASM_DIAG_RELOCATION,
             line, column, "CupidASM relocation capacity was exhausted");
    return CTOOL_ERR_INTERNAL;
  }
  relocation = &relocations[*count];
  relocation->target_section = section->index;
  relocation->offset = offset;
  relocation->symbol = symbol;
  relocation->type = type;
  relocation->addend = addend;
  (*count)++;
  return CTOOL_OK;
}

static void asm_put_section_value(ctool_u8 *destination, ctool_u8 width,
                                  ctool_u64 value) {
  ctool_u8 index;
  for (index = 0u; index < width; index++) {
    destination[index] = (ctool_u8)(value >> ((ctool_u32)index * 8u));
  }
}

static ctool_status_t asm_object_emit_statements(
    asm_context_t *context, ctool_elf32_relocation_spec_t *relocations,
    ctool_u32 relocation_capacity, ctool_u32 *relocation_count_out) {
  asm_statement_t *statement = context->statements;
  ctool_u32 relocation_count = 0u;
  while (statement != (asm_statement_t *)0) {
    ctool_status_t status = CTOOL_OK;
    context->active_path = statement->path;
    if (statement->kind == ASM_STATEMENT_INSTRUCTION) {
      ctool_x86_encoding_t encoding;
      ctool_u32 index;
      status = asm_encode_statement(context, statement, CTOOL_TRUE,
                                    &encoding);
      if (status == CTOOL_OK &&
          (ctool_u32)encoding.size != statement->size) {
        asm_fail(context, CTOOL_ERR_INTERNAL, CTOOL_ASM_DIAG_LAYOUT,
                 statement->line, statement->column,
                 "object instruction changed size after layout");
        status = CTOOL_ERR_INTERNAL;
      }
      if (status == CTOOL_OK &&
          statement->section->type != CTOOL_ELF32_SHT_PROGBITS) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_SECTION,
                 statement->line, statement->column,
                 "instructions cannot be emitted into a NOBITS section");
        status = CTOOL_ERR_INPUT;
      }
      if (status == CTOOL_OK &&
          (statement->section->size < (ctool_u32)encoding.size ||
           statement->offset > statement->section->size -
                                   (ctool_u32)encoding.size)) {
        status = CTOOL_ERR_INTERNAL;
      }
      for (index = 0u;
           status == CTOOL_OK && index < (ctool_u32)encoding.size;
           index++) {
        statement->section->contents[statement->offset + index] =
            encoding.bytes[index];
      }
      for (index = 0u;
           status == CTOOL_OK && index < (ctool_u32)encoding.field_count;
           index++) {
        const ctool_x86_field_t *field = &encoding.fields[index];
        ctool_elf32_relocation_type_t type;
        if (field->relocation == CTOOL_X86_RELOC_NONE) {
          continue;
        }
        if (field->byte_width != 4u) {
          asm_fail(context, CTOOL_ERR_UNSUPPORTED,
                   CTOOL_ASM_DIAG_RELOCATION,
                   statement->line, statement->column,
                   "ELF32 object references require a four-byte field");
          status = CTOOL_ERR_UNSUPPORTED;
          break;
        }
        type = field->relocation == CTOOL_X86_RELOC_PC_RELATIVE
                   ? CTOOL_ELF32_R_386_PC32
                   : CTOOL_ELF32_R_386_32;
        status = asm_object_add_relocation(
            context, relocations, relocation_capacity, &relocation_count,
            statement->section,
            statement->offset + (ctool_u32)field->byte_offset,
            field->reference, type, field->encoded_addend,
            statement->line, statement->column);
      }
    } else if (statement->kind == ASM_STATEMENT_DATA) {
      ctool_u32 repeat_index;
      ctool_u32 offset = statement->offset;
      if (statement->section->type != CTOOL_ELF32_SHT_PROGBITS) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_SECTION,
                 statement->line, statement->column,
                 "initialized data cannot be emitted into a NOBITS section");
        return CTOOL_ERR_INPUT;
      }
      for (repeat_index = 0u;
           status == CTOOL_OK &&
           repeat_index < statement->as.data.repeat_count;
           repeat_index++) {
        asm_data_value_t *data = statement->as.data.values;
        while (status == CTOOL_OK && data != (asm_data_value_t *)0) {
          if (data->is_string == CTOOL_TRUE) {
            ctool_u32 string_index = 0u;
            while (status == CTOOL_OK &&
                   string_index < data->string.size) {
              ctool_u8 byte;
              status = asm_string_byte(data->string, &string_index, &byte);
              if (status == CTOOL_OK) {
                statement->section->contents[offset] = byte;
                offset++;
              }
            }
          } else {
            asm_linear_expression_t linear;
            status = asm_linearize_expression(context, data->expression,
                                              offset, &linear);
            if (status == CTOOL_OK &&
                linear.negative != (asm_symbol_t *)0) {
              asm_fail(context, CTOOL_ERR_INPUT,
                       CTOOL_ASM_DIAG_NON_RELOCATABLE_EXPRESSION,
                       data->expression->line, data->expression->column,
                       "data expression has an unresolved negative symbol");
              status = CTOOL_ERR_INPUT;
            }
            if (status == CTOOL_OK &&
                linear.positive != (asm_symbol_t *)0 &&
                statement->as.data.width != 4u) {
              asm_fail(context, CTOOL_ERR_UNSUPPORTED,
                       CTOOL_ASM_DIAG_RELOCATION,
                       data->expression->line, data->expression->column,
                       "ELF32 data references require DD width");
              status = CTOOL_ERR_UNSUPPORTED;
            }
            if (status == CTOOL_OK) {
              asm_put_section_value(statement->section->contents + offset,
                                    statement->as.data.width,
                                    linear.constant);
              if (linear.positive != (asm_symbol_t *)0) {
                status = asm_object_add_relocation(
                    context, relocations, relocation_capacity,
                    &relocation_count, statement->section, offset,
                    linear.positive->object_index, CTOOL_ELF32_R_386_32,
                    asm_i32_from_bits((ctool_u32)linear.constant),
                    data->expression->line, data->expression->column);
              }
            }
            offset += (ctool_u32)statement->as.data.width;
          }
          data = data->next;
        }
      }
    }
    if (status != CTOOL_OK) {
      if (context->failure_status == CTOOL_OK) {
        asm_fail(context, status, CTOOL_ASM_DIAG_ENCODING,
                 statement->line, statement->column,
                 "object statement could not be encoded");
      }
      return status;
    }
    statement = statement->next;
  }
  *relocation_count_out = relocation_count;
  return CTOOL_OK;
}

static ctool_status_t asm_emit_object(asm_context_t *context,
                                      ctool_buffer_t *output) {
  ctool_elf32_section_spec_t *sections;
  ctool_elf32_symbol_spec_t *symbols;
  ctool_elf32_relocation_spec_t *relocations =
      (ctool_elf32_relocation_spec_t *)0;
  ctool_elf32_object_spec_t object;
  ctool_u32 symbol_count;
  ctool_u32 relocation_capacity;
  ctool_u32 relocation_count = 0u;
  ctool_status_t status = asm_object_counts(
      context, &symbol_count, &relocation_capacity);
  if (status == CTOOL_OK) {
    status = asm_object_prepare_sections(context, &sections);
  }
  if (status == CTOOL_OK) {
    status = asm_object_prepare_symbols(context, symbol_count, &symbols);
  }
  if (status == CTOOL_OK && relocation_capacity != 0u) {
    status = ctool_arena_alloc_zero(
        ctool_job_arena(context->job), relocation_capacity,
        (ctool_u32)sizeof(*relocations), (ctool_u32)sizeof(void *),
        (void **)&relocations);
    if (status != CTOOL_OK) {
      asm_fail(context, status, CTOOL_ASM_DIAG_LIMIT, 0u, 0u,
               "CupidASM relocation table exceeds the job limit");
    }
  }
  if (status == CTOOL_OK) {
    status = asm_object_emit_statements(
        context, relocations, relocation_capacity, &relocation_count);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  object.sections = sections;
  object.section_count = context->section_count;
  object.symbols = symbols;
  object.symbol_count = symbol_count;
  object.relocations = relocations;
  object.relocation_count = relocation_count;
  return ctool_elf32_write(context->job, &object, output);
}

static ctool_bool asm_section_is_code(const asm_section_t *section) {
  return (section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t asm_align_u32(ctool_u32 value, ctool_u32 alignment,
                                    ctool_u32 *aligned_out) {
  ctool_u32 remainder;
  ctool_u32 addition;
  if (alignment == 0u ||
      (alignment & (alignment - 1u)) != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  remainder = value & (alignment - 1u);
  addition = remainder == 0u ? 0u : alignment - remainder;
  if (value > ASM_U32_MAX - addition) {
    return CTOOL_ERR_OVERFLOW;
  }
  *aligned_out = value + addition;
  return CTOOL_OK;
}

static ctool_status_t asm_fixed_layout_sections(
    asm_context_t *context, ctool_u32 *code_file_out,
    ctool_u32 *code_memory_out, ctool_u32 *data_file_out,
    ctool_u32 *data_memory_out) {
  asm_section_t *section = context->sections;
  ctool_u32 code_used = 0u;
  ctool_u32 code_file = 0u;
  ctool_u32 data_used = 0u;
  ctool_u32 data_file = 0u;
  while (section != (asm_section_t *)0) {
    ctool_bool code = asm_section_is_code(section);
    ctool_u32 *used = code == CTOOL_TRUE ? &code_used : &data_used;
    ctool_u32 *file = code == CTOOL_TRUE ? &code_file : &data_file;
    ctool_u32 base = code == CTOOL_TRUE
                         ? context->request->as.fixed.code.base_address
                         : context->request->as.fixed.data.base_address;
    ctool_u32 aligned;
    ctool_status_t status;
    if (section->size == 0u) {
      section->load_address = base + *used;
      section = section->next;
      continue;
    }
    status = asm_align_u32(*used, section->alignment, &aligned);
    if (status != CTOOL_OK || aligned > ASM_U32_MAX - section->size ||
        base > ASM_U32_MAX - aligned) {
      asm_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_LAYOUT,
               0u, 0u, "fixed image section address overflows");
      return CTOOL_ERR_OVERFLOW;
    }
    section->load_address = base + aligned;
    *used = aligned + section->size;
    if (section->type == CTOOL_ELF32_SHT_PROGBITS) {
      *file = *used;
    }
    section = section->next;
  }
  if (code_used > context->request->as.fixed.code.maximum_bytes ||
      data_used > context->request->as.fixed.data.maximum_bytes) {
    asm_fail(context, CTOOL_ERR_LIMIT, CTOOL_ASM_DIAG_LAYOUT, 0u, 0u,
             "fixed image exceeds its requested region limit");
    return CTOOL_ERR_LIMIT;
  }
  if (code_used != 0u && data_used != 0u) {
    ctool_u32 code_base = context->request->as.fixed.code.base_address;
    ctool_u32 data_base = context->request->as.fixed.data.base_address;
    ctool_u32 code_end;
    ctool_u32 data_end;
    if (code_base > ASM_U32_MAX - code_used ||
        data_base > ASM_U32_MAX - data_used) {
      asm_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_LAYOUT,
               0u, 0u, "fixed image region address overflows");
      return CTOOL_ERR_OVERFLOW;
    }
    code_end = code_base + code_used;
    data_end = data_base + data_used;
    if (code_base < data_end && data_base < code_end) {
      asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_LAYOUT, 0u, 0u,
               "fixed image code and data regions overlap");
      return CTOOL_ERR_INPUT;
    }
  }
  section = context->sections;
  while (section != (asm_section_t *)0) {
    if (asm_section_is_code(section) == CTOOL_TRUE) {
      section->output_offset =
          section->load_address -
          context->request->as.fixed.code.base_address;
    } else {
      section->output_offset =
          code_file + section->load_address -
          context->request->as.fixed.data.base_address;
    }
    section = section->next;
  }
  {
    asm_symbol_t *symbol = context->symbols;
    while (symbol != (asm_symbol_t *)0) {
      if (symbol->kind == ASM_SYMBOL_ABSOLUTE &&
          symbol->definition != (asm_expr_t *)0) {
        symbol->resolved = CTOOL_FALSE;
      }
      symbol = symbol->next;
    }
  }
  *code_file_out = code_file;
  *code_memory_out = code_used;
  *data_file_out = data_file;
  *data_memory_out = data_used;
  return CTOOL_OK;
}

static ctool_status_t asm_fixed_data_value(
    asm_context_t *context, const asm_expr_t *expression,
    const asm_statement_t *statement, ctool_u32 current_offset,
    ctool_u64 *value_out) {
  asm_linear_expression_t linear;
  ctool_status_t status = asm_linearize_expression(
      context, expression, current_offset, &linear);
  if (status != CTOOL_OK) {
    return status;
  }
  if (linear.negative != (asm_symbol_t *)0) {
    asm_fail(context, CTOOL_ERR_INPUT,
             CTOOL_ASM_DIAG_NON_RELOCATABLE_EXPRESSION,
             expression->line, expression->column,
             "fixed data has an unresolved negative symbol");
    return CTOOL_ERR_INPUT;
  }
  if (linear.positive != (asm_symbol_t *)0) {
    if (linear.positive->declared == CTOOL_FALSE ||
        linear.positive->kind != ASM_SYMBOL_LABEL ||
        linear.positive->section == (asm_section_t *)0) {
      asm_fail(context, CTOOL_ERR_NOT_FOUND,
               CTOOL_ASM_DIAG_UNDEFINED_SYMBOL,
               expression->line, expression->column,
               "fixed data contains an unresolved symbol");
      return CTOOL_ERR_NOT_FOUND;
    }
    if (linear.constant >
        ASM_U64_MAX - (ctool_u64)linear.positive->section->load_address ||
        linear.constant +
                (ctool_u64)linear.positive->section->load_address >
            ASM_U64_MAX - linear.positive->value) {
      asm_fail(context, CTOOL_ERR_OVERFLOW,
               CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW,
               expression->line, expression->column,
               "fixed data label expression overflows");
      return CTOOL_ERR_OVERFLOW;
    }
    linear.constant +=
        (ctool_u64)linear.positive->section->load_address +
        linear.positive->value;
  }
  (void)statement;
  *value_out = linear.constant;
  return CTOOL_OK;
}

static ctool_status_t asm_fixed_patch_value(ctool_buffer_t *output,
                                            ctool_u32 offset,
                                            ctool_u8 width,
                                            ctool_u64 value) {
  if (width == 1u) {
    return ctool_buffer_patch_u8(output, offset, (ctool_u8)value);
  }
  if (width == 2u) {
    return ctool_buffer_patch_le16(output, offset, (ctool_u16)value);
  }
  if (width == 4u) {
    return ctool_buffer_patch_le32(output, offset, (ctool_u32)value);
  }
  if (width == 8u) {
    return ctool_buffer_patch_le64(output, offset, value);
  }
  return CTOOL_ERR_INTERNAL;
}

static ctool_status_t asm_fixed_emit_statements(asm_context_t *context,
                                                ctool_buffer_t *output) {
  asm_statement_t *statement = context->statements;
  while (statement != (asm_statement_t *)0) {
    ctool_status_t status = CTOOL_OK;
    context->active_path = statement->path;
    ctool_u32 output_offset =
        statement->section->output_offset + statement->offset;
    if (statement->kind == ASM_STATEMENT_INSTRUCTION) {
      ctool_x86_encoding_t encoding;
      ctool_u32 index;
      if (statement->section->type != CTOOL_ELF32_SHT_PROGBITS) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_SECTION,
                 statement->line, statement->column,
                 "fixed image instruction is in a NOBITS section");
        return CTOOL_ERR_INPUT;
      }
      status = asm_encode_statement(context, statement, CTOOL_TRUE,
                                    &encoding);
      if (status == CTOOL_OK &&
          (ctool_u32)encoding.size != statement->size) {
        asm_fail(context, CTOOL_ERR_INTERNAL, CTOOL_ASM_DIAG_LAYOUT,
                 statement->line, statement->column,
                 "fixed image instruction changed size after layout");
        status = CTOOL_ERR_INTERNAL;
      }
      for (index = 0u;
           status == CTOOL_OK && index < (ctool_u32)encoding.size;
           index++) {
        status = ctool_buffer_patch_u8(
            output, output_offset + index, encoding.bytes[index]);
      }
    } else if (statement->kind == ASM_STATEMENT_DATA) {
      ctool_u32 repeat_index;
      ctool_u32 current = statement->offset;
      if (statement->section->type != CTOOL_ELF32_SHT_PROGBITS) {
        asm_fail(context, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_SECTION,
                 statement->line, statement->column,
                 "fixed initialized data is in a NOBITS section");
        return CTOOL_ERR_INPUT;
      }
      for (repeat_index = 0u;
           status == CTOOL_OK &&
           repeat_index < statement->as.data.repeat_count;
           repeat_index++) {
        asm_data_value_t *data = statement->as.data.values;
        while (status == CTOOL_OK && data != (asm_data_value_t *)0) {
          if (data->is_string == CTOOL_TRUE) {
            ctool_u32 string_index = 0u;
            while (status == CTOOL_OK &&
                   string_index < data->string.size) {
              ctool_u8 byte;
              status = asm_string_byte(data->string, &string_index, &byte);
              if (status == CTOOL_OK) {
                status = ctool_buffer_patch_u8(
                    output,
                    statement->section->output_offset + current, byte);
                current++;
              }
            }
          } else {
            ctool_u64 value;
            status = asm_fixed_data_value(context, data->expression,
                                          statement, current, &value);
            if (status == CTOOL_OK) {
              status = asm_fixed_patch_value(
                  output,
                  statement->section->output_offset + current,
                  statement->as.data.width, value);
            }
            current += (ctool_u32)statement->as.data.width;
          }
          data = data->next;
        }
      }
    }
    if (status != CTOOL_OK) {
      if (context->failure_status == CTOOL_OK) {
        asm_fail(context, status, CTOOL_ASM_DIAG_OUTPUT,
                 statement->line, statement->column,
                 "fixed image statement could not be emitted");
      }
      return status;
    }
    statement = statement->next;
  }
  return CTOOL_OK;
}

static ctool_status_t asm_fixed_select_entry(asm_context_t *context,
                                             ctool_asm_result_t *result) {
  ctool_u32 index;
  if (context->request->entry_candidate_count == 0u) {
    return CTOOL_OK;
  }
  for (index = 0u; index < context->request->entry_candidate_count; index++) {
    ctool_string_t name = context->request->entry_candidates[index];
    asm_symbol_t *symbol;
    if (asm_definition_name_is_valid(name) == CTOOL_FALSE) {
      asm_fail(context, CTOOL_ERR_INVALID_ARGUMENT,
               CTOOL_ASM_DIAG_INVALID_REQUEST, 0u, 0u,
               "invalid fixed image entry candidate");
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
    symbol = asm_find_symbol(context, name);
    if (symbol != (asm_symbol_t *)0 && symbol->declared == CTOOL_TRUE &&
        symbol->kind == ASM_SYMBOL_LABEL &&
        symbol->section != (asm_section_t *)0 &&
        asm_section_is_code(symbol->section) == CTOOL_TRUE) {
      result->has_entry = CTOOL_TRUE;
      result->entry_address = symbol->section->load_address +
                              (ctool_u32)symbol->value;
      return CTOOL_OK;
    }
  }
  asm_fail(context, CTOOL_ERR_NOT_FOUND, CTOOL_ASM_DIAG_ENTRY, 0u, 0u,
           "none of the fixed image entry candidates is defined in code");
  return CTOOL_ERR_NOT_FOUND;
}

static ctool_status_t asm_emit_fixed(asm_context_t *context,
                                     ctool_buffer_t *output,
                                     ctool_asm_result_t *result) {
  ctool_u32 code_file;
  ctool_u32 code_memory;
  ctool_u32 data_file;
  ctool_u32 data_memory;
  ctool_u32 total_file;
  ctool_u32 region_count = 0u;
  ctool_asm_region_t *regions;
  asm_section_t *section;
  ctool_status_t status = asm_fixed_layout_sections(
      context, &code_file, &code_memory, &data_file, &data_memory);
  if (status != CTOOL_OK) {
    return status;
  }
  if (code_file > ASM_U32_MAX - data_file) {
    asm_fail(context, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_LAYOUT, 0u, 0u,
             "fixed image file size overflows");
    return CTOOL_ERR_OVERFLOW;
  }
  total_file = code_file + data_file;
  if (code_memory != 0u) {
    region_count++;
  }
  if (data_memory != 0u) {
    region_count++;
  }
  regions = (ctool_asm_region_t *)0;
  if (region_count != 0u) {
    status = ctool_arena_alloc_zero(
        ctool_job_arena(context->job), region_count,
        (ctool_u32)sizeof(*regions), (ctool_u32)sizeof(void *),
        (void **)&regions);
  }
  if (status != CTOOL_OK) {
    asm_fail(context, status, CTOOL_ASM_DIAG_LIMIT, 0u, 0u,
             "fixed image region metadata exceeds the job limit");
    return status;
  }
  status = ctool_buffer_fill(output, 0u, total_file);
  if (status == CTOOL_OK) {
    status = asm_fixed_emit_statements(context, output);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  result->regions = regions;
  result->region_count = region_count;
  region_count = 0u;
  if (code_memory != 0u) {
    ctool_asm_region_t *region = &regions[region_count++];
    section = context->sections;
    while (section != (asm_section_t *)0 &&
           (section->size == 0u ||
            asm_section_is_code(section) == CTOOL_FALSE)) {
      section = section->next;
    }
    region->name = section->name;
    region->address = context->request->as.fixed.code.base_address;
    region->output_offset = 0u;
    region->file_size = code_file;
    region->memory_size = code_memory;
    region->flags = section->flags;
  }
  if (data_memory != 0u) {
    ctool_asm_region_t *region = &regions[region_count];
    section = context->sections;
    while (section != (asm_section_t *)0 &&
           (section->size == 0u ||
            asm_section_is_code(section) == CTOOL_TRUE)) {
      section = section->next;
    }
    region->name = section->name;
    region->address = context->request->as.fixed.data.base_address;
    region->output_offset = code_file;
    region->file_size = data_file;
    region->memory_size = data_memory;
    region->flags = section->flags;
  }
  return asm_fixed_select_entry(context, result);
}

static ctool_status_t asm_validate_request(const ctool_source_t *source,
                                           const ctool_asm_request_t *request,
                                           const ctool_buffer_t *output,
                                           ctool_asm_result_t *result) {
  ctool_bytes_t existing;
  if (source == (const ctool_source_t *)0 ||
      request == (const ctool_asm_request_t *)0 ||
      output == (const ctool_buffer_t *)0 ||
      result == (ctool_asm_result_t *)0 ||
      source->path.text.data == (const char *)0 ||
      source->path.text.size == 0u || source->path.text.data[0] != '/' ||
      (source->contents.data == (const ctool_u8 *)0 &&
       source->contents.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  existing = ctool_buffer_view(output);
  if (existing.size != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if ((request->artifact != CTOOL_ASM_ARTIFACT_RAW &&
       request->artifact != CTOOL_ASM_ARTIFACT_ELF32_REL &&
       request->artifact != CTOOL_ASM_ARTIFACT_FIXED_IMAGE) ||
      (request->initial_mode != CTOOL_X86_MODE_16 &&
       request->initial_mode != CTOOL_X86_MODE_32) ||
      (request->artifact == CTOOL_ASM_ARTIFACT_ELF32_REL &&
       request->as.elf32_rel.reserved != 0u) ||
      (request->definition_count != 0u &&
       request->definitions == (const ctool_asm_definition_t *)0) ||
      (request->include_root_count != 0u &&
       request->include_roots == (const ctool_path_t *)0) ||
      (request->entry_candidate_count != 0u &&
       request->entry_candidates == (const ctool_string_t *)0) ||
      (request->artifact == CTOOL_ASM_ARTIFACT_FIXED_IMAGE &&
       (request->as.fixed.code.maximum_bytes == 0u ||
        request->as.fixed.data.maximum_bytes == 0u))) {
    return CTOOL_ERR_UNSUPPORTED;
  }
  return CTOOL_OK;
}

ctool_status_t ctool_asm_assemble(ctool_job_t *job,
                                  const ctool_source_t *source,
                                  const ctool_asm_request_t *request,
                                  ctool_buffer_t *output,
                                  ctool_asm_result_t *result_out) {
  asm_context_t context;
  ctool_status_t status;
  if (result_out != (ctool_asm_result_t *)0) {
    asm_zero_result(result_out);
  }
  if (job == (ctool_job_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  status = asm_validate_request(source, request, output, result_out);
  if (status != CTOOL_OK) {
    ctool_diagnostic_t diagnostic;
    diagnostic.severity = CTOOL_DIAG_ERROR;
    diagnostic.code = CTOOL_ASM_DIAG_INVALID_REQUEST;
    diagnostic.path = source != (const ctool_source_t *)0
                          ? source->path.text
                          : ctool_string("/cupidasm");
    diagnostic.line = 0u;
    diagnostic.column = 0u;
    diagnostic.message = ctool_string("invalid CupidASM assembly request");
    (void)ctool_job_emit(job, &diagnostic);
    return status;
  }
  context.job = job;
  context.source = source;
  context.request = request;
  context.statements = (asm_statement_t *)0;
  context.statement_tail = (asm_statement_t *)0;
  context.symbols = (asm_symbol_t *)0;
  context.sections = (asm_section_t *)0;
  context.section_tail = (asm_section_t *)0;
  context.current_section = (asm_section_t *)0;
  context.section_count = 0u;
  context.current_global.data = (const char *)0;
  context.current_global.size = 0u;
  context.mode = request->initial_mode;
  context.origin = request->artifact == CTOOL_ASM_ARTIFACT_RAW
                       ? request->as.raw.initial_origin
                       : 0u;
  context.have_origin = CTOOL_FALSE;
  context.include_depth = 0u;
  context.failure_status = CTOOL_OK;
  context.failure_code = 0u;
  context.failure_line = 0u;
  context.failure_column = 0u;
  context.failure_path = source->path.text;
  context.active_path = source->path.text;
  context.failure_message = "CupidASM assembly failed";

  status = asm_install_definitions(&context);
  if (status == CTOOL_OK) {
    context.current_section =
        asm_get_section(&context, ctool_string(".text"), 1u, 1u);
    if (context.current_section == (asm_section_t *)0) {
      status = context.failure_status;
    } else {
      status = asm_parse_source(&context);
    }
  }

  if (status == CTOOL_OK) {
    status = asm_layout(&context);
  }
  if (status == CTOOL_OK) {
    if (request->artifact == CTOOL_ASM_ARTIFACT_RAW) {
      status = asm_emit_raw(&context, output);
    } else if (request->artifact == CTOOL_ASM_ARTIFACT_ELF32_REL) {
      status = asm_emit_object(&context, output);
    } else {
      status = asm_emit_fixed(&context, output, result_out);
    }
  }
  if (status != CTOOL_OK) {
    (void)ctool_buffer_rewind(output, 0u);
    asm_zero_result(result_out);
    return context.failure_status != CTOOL_OK
               ? asm_publish_failure(&context)
               : status;
  }
  result_out->artifact = request->artifact;
  result_out->bytes = ctool_buffer_view(output);
  if (request->artifact != CTOOL_ASM_ARTIFACT_FIXED_IMAGE) {
    result_out->regions = (const ctool_asm_region_t *)0;
    result_out->region_count = 0u;
    result_out->has_entry = CTOOL_FALSE;
    result_out->entry_address = 0u;
  }
  return CTOOL_OK;
}
