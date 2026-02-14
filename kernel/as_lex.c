/**
 * as_lex.c — Lexer for the CupidASM assembler
 *
 * Tokenizes CupidASM source code into a stream of tokens.
 * Handles mnemonics, registers, integer literals (decimal, hex, binary),
 * string literals, labels, directives, and delimiters.
 * Skips whitespace (except newlines) and ; line comments.
 * Case-insensitive for mnemonics and registers.
 */

#include "as.h"
#include "string.h"

/* ── Character classification helpers ────────────────────────────── */

static int as_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r';
}

static int as_is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
         c == '.';
}

static int as_is_digit(char c) { return c >= '0' && c <= '9'; }

static int as_is_alnum(char c) { return as_is_alpha(c) || as_is_digit(c); }

static int as_is_hexdigit(char c) {
  return as_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* ── Case-insensitive string comparison ──────────────────────────── */
static int as_stricmp(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb) return ca - cb;
    a++; b++;
  }
  return (unsigned char)*a - (unsigned char)*b;
}

static void as_tolower_str(char *dst, const char *src, int max) {
  int i = 0;
  while (src[i] && i < max - 1) {
    char c = src[i];
    if (c >= 'A' && c <= 'Z') c += 32;
    dst[i] = c;
    i++;
  }
  dst[i] = '\0';
}

/* ── Peek at current character without consuming ─────────────────── */

static char as_peek_char(as_state_t *as) {
  if (as->source[as->pos] == '\0')
    return '\0';
  return as->source[as->pos];
}

static char as_next_char(as_state_t *as) {
  char c = as->source[as->pos];
  if (c == '\0')
    return '\0';
  if (c == '\n')
    as->line++;
  as->pos++;
  return c;
}

/* ── Skip whitespace (NOT newlines — they are tokens) and comments ─ */

static void as_skip_whitespace(as_state_t *as) {
  for (;;) {
    char c = as_peek_char(as);

    /* Spaces and tabs only (newlines are delimiters) */
    if (as_is_space(c)) {
      as_next_char(as);
      continue;
    }

    /* Line comment: ; ... */
    if (c == ';') {
      while (as_peek_char(as) != '\0' && as_peek_char(as) != '\n') {
        as_next_char(as);
      }
      continue;
    }

    break;
  }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Mnemonic Table — all supported x86 mnemonics
 * ══════════════════════════════════════════════════════════════════════ */

static const char *as_mnemonics[] = {
  "mov", "push", "pop", "call", "ret", "jmp",
  "je", "jne", "jz", "jnz", "jl", "jg", "jle", "jge",
  "jb", "jbe", "ja", "jae", "js", "jns", "jo", "jno",
  "add", "sub", "mul", "div", "imul", "idiv",
  "and", "or", "xor", "not", "neg",
  "shl", "shr", "sar", "rol", "ror",
  "cmp", "test",
  "inc", "dec",
  "nop", "hlt", "cli", "sti",
  "int", "iret",
  "in", "out",
  "lea",
  "rep", "movsd", "movsb", "stosb", "stosd",
  "pushad", "popad", "pushfd", "popfd",
  "cdq", "cbw", "cwde",
  "leave",
  "xchg",
  "movzx", "movsx",
  NULL
};

static int as_is_mnemonic(const char *word) {
  for (int i = 0; as_mnemonics[i]; i++) {
    if (as_stricmp(word, as_mnemonics[i]) == 0) return 1;
  }
  return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Register Table
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
  const char *name;
  int index;  /* 0=eax/ax/al, 1=ecx/cx/cl, ... 7=edi/di/bh */
  int size;   /* 1=8-bit, 2=16-bit, 4=32-bit */
} as_reg_info_t;

static const as_reg_info_t as_registers[] = {
  /* 32-bit */
  {"eax", 0, 4}, {"ecx", 1, 4}, {"edx", 2, 4}, {"ebx", 3, 4},
  {"esp", 4, 4}, {"ebp", 5, 4}, {"esi", 6, 4}, {"edi", 7, 4},
  /* 16-bit */
  {"ax",  0, 2}, {"cx",  1, 2}, {"dx",  2, 2}, {"bx",  3, 2},
  {"sp",  4, 2}, {"bp",  5, 2}, {"si",  6, 2}, {"di",  7, 2},
  /* 8-bit */
  {"al",  0, 1}, {"cl",  1, 1}, {"dl",  2, 1}, {"bl",  3, 1},
  {"ah",  4, 1}, {"ch",  5, 1}, {"dh",  6, 1}, {"bh",  7, 1},
  {NULL, 0, 0}
};

static const as_reg_info_t *as_find_register(const char *name) {
  for (int i = 0; as_registers[i].name; i++) {
    if (as_stricmp(name, as_registers[i].name) == 0)
      return &as_registers[i];
  }
  return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Directive Table
 * ══════════════════════════════════════════════════════════════════════ */

static const char *as_directives[] = {
  "db", "dw", "dd", "equ", "section", "global", "extern",
  "times", "resb", "resw", "resd", "rb", "rw", "rd", "reserve",
  NULL
};

static int as_is_directive(const char *word) {
  for (int i = 0; as_directives[i]; i++) {
    if (as_stricmp(word, as_directives[i]) == 0) return 1;
  }
  return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Lexer Init & Token Functions
 * ══════════════════════════════════════════════════════════════════════ */

void as_lex_init(as_state_t *as, const char *source) {
  as->source = source;
  as->pos = 0;
  as->line = 1;
  as->has_peek = 0;
}

as_token_t as_lex_peek(as_state_t *as) {
  if (!as->has_peek) {
    as->peek_buf = as_lex_next(as);
    as->has_peek = 1;
  }
  return as->peek_buf;
}

static as_token_t as_make_token(as_token_type_t type, const char *text,
                                int line) {
  as_token_t tok;
  tok.type = type;
  tok.int_value = 0;
  tok.reg_index = 0;
  tok.reg_size = 0;
  tok.line = line;
  int i = 0;
  if (text) {
    while (text[i] && i < AS_MAX_IDENT - 1) {
      tok.text[i] = text[i];
      i++;
    }
  }
  tok.text[i] = '\0';
  return tok;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Main Lexer — as_lex_next()
 * ══════════════════════════════════════════════════════════════════════ */

as_token_t as_lex_next(as_state_t *as) {
  /* Return peeked token if available */
  if (as->has_peek) {
    as->has_peek = 0;
    as->cur = as->peek_buf;
    return as->cur;
  }

  as_skip_whitespace(as);

  char c = as_peek_char(as);

  /* EOF */
  if (c == '\0') {
    as->cur = as_make_token(AS_TOK_EOF, "", as->line);
    return as->cur;
  }

  /* Newline — instruction boundary */
  if (c == '\n') {
    as_next_char(as);
    as->cur = as_make_token(AS_TOK_NEWLINE, "\n", as->line);
    return as->cur;
  }

  /* %include or %define directive */
  if (c == '%') {
    as_next_char(as);
    char word[AS_MAX_IDENT];
    int wi = 0;
    while (as_is_alnum(as_peek_char(as)) && wi < AS_MAX_IDENT - 1) {
      word[wi++] = as_next_char(as);
    }
    word[wi] = '\0';
    /* Store full directive name e.g. "%include" */
    char full[AS_MAX_IDENT];
    full[0] = '%';
    int fi = 1;
    for (int i = 0; word[i] && fi < AS_MAX_IDENT - 1; i++)
      full[fi++] = word[i];
    full[fi] = '\0';
    as->cur = as_make_token(AS_TOK_DIRECTIVE, full, as->line);
    return as->cur;
  }

  /* String literal */
  if (c == '"') {
    as_next_char(as); /* skip opening quote */
    char buf[AS_MAX_STRING];
    int bi = 0;
    while (as_peek_char(as) != '\0' && as_peek_char(as) != '"' &&
           as_peek_char(as) != '\n' && bi < AS_MAX_STRING - 1) {
      char sc = as_next_char(as);
      if (sc == '\\') {
        char esc = as_next_char(as);
        switch (esc) {
          case 'n': buf[bi++] = '\n'; break;
          case 'r': buf[bi++] = '\r'; break;
          case 't': buf[bi++] = '\t'; break;
          case '0': buf[bi++] = '\0'; break;
          case '"': buf[bi++] = '"'; break;
          case '\\': buf[bi++] = '\\'; break;
          default: buf[bi++] = esc; break;
        }
      } else {
        buf[bi++] = sc;
      }
    }
    buf[bi] = '\0';
    if (as_peek_char(as) == '"') as_next_char(as); /* skip closing quote */
    as->cur = as_make_token(AS_TOK_STRING, buf, as->line);
    return as->cur;
  }

  /* Character literal: 'A' */
  if (c == '\'') {
    as_next_char(as);
    char ch = as_next_char(as);
    if (ch == '\\') {
      char esc = as_next_char(as);
      switch (esc) {
        case 'n': ch = '\n'; break;
        case 'r': ch = '\r'; break;
        case 't': ch = '\t'; break;
        case '0': ch = '\0'; break;
        default: ch = esc; break;
      }
    }
    if (as_peek_char(as) == '\'') as_next_char(as);
    as->cur = as_make_token(AS_TOK_NUMBER, "", as->line);
    as->cur.int_value = (int32_t)(uint8_t)ch;
    return as->cur;
  }

  /* Number literal */
  if (as_is_digit(c)) {
    char nbuf[32];
    int ni = 0;
    int32_t val = 0;

    /* Hex: 0x... */
    if (c == '0' && (as->source[as->pos + 1] == 'x' ||
                     as->source[as->pos + 1] == 'X')) {
      as_next_char(as); /* 0 */
      as_next_char(as); /* x */
      while (as_is_hexdigit(as_peek_char(as)) && ni < 30) {
        char hc = as_next_char(as);
        nbuf[ni++] = hc;
        int digit;
        if (hc >= '0' && hc <= '9') digit = hc - '0';
        else if (hc >= 'a' && hc <= 'f') digit = 10 + hc - 'a';
        else digit = 10 + hc - 'A';
        val = (val << 4) | digit;
      }
    }
    /* Binary: 0b... */
    else if (c == '0' && (as->source[as->pos + 1] == 'b' ||
                          as->source[as->pos + 1] == 'B')) {
      as_next_char(as); /* 0 */
      as_next_char(as); /* b */
      while ((as_peek_char(as) == '0' || as_peek_char(as) == '1') && ni < 30) {
        char bc = as_next_char(as);
        nbuf[ni++] = bc;
        val = (val << 1) | (bc - '0');
      }
    }
    /* Decimal */
    else {
      while (as_is_digit(as_peek_char(as)) && ni < 30) {
        char dc = as_next_char(as);
        nbuf[ni++] = dc;
        val = val * 10 + (dc - '0');
      }
    }
    nbuf[ni] = '\0';

    /* Check for suffix 'h' (e.g. 0FFh) — not required but nice */
    as->cur = as_make_token(AS_TOK_NUMBER, nbuf, as->line);
    as->cur.int_value = val;
    return as->cur;
  }

  /* Identifier, mnemonic, register, directive, or label definition */
  if (as_is_alpha(c)) {
    char word[AS_MAX_IDENT];
    int wi = 0;
    while ((as_is_alnum(as_peek_char(as)) || as_peek_char(as) == '.') &&
           wi < AS_MAX_IDENT - 1) {
      word[wi++] = as_next_char(as);
    }
    word[wi] = '\0';

    /* Lowercase copy for matching */
    char lower[AS_MAX_IDENT];
    as_tolower_str(lower, word, AS_MAX_IDENT);

    /* Check for label definition: word followed by : */
    as_skip_whitespace(as);
    if (as_peek_char(as) == ':') {
      as_next_char(as); /* consume : */
      as->cur = as_make_token(AS_TOK_LABEL_DEF, word, as->line);
      return as->cur;
    }

    /* Check if it's a register */
    const as_reg_info_t *reg = as_find_register(lower);
    if (reg) {
      as->cur = as_make_token(AS_TOK_REGISTER, lower, as->line);
      as->cur.reg_index = reg->index;
      as->cur.reg_size = reg->size;
      return as->cur;
    }

    /* Check if it's a directive */
    if (as_is_directive(lower)) {
      as->cur = as_make_token(AS_TOK_DIRECTIVE, lower, as->line);
      return as->cur;
    }

    /* Check if it's a mnemonic */
    if (as_is_mnemonic(lower)) {
      as->cur = as_make_token(AS_TOK_MNEMONIC, lower, as->line);
      return as->cur;
    }

    /* Otherwise it's an identifier (label reference, equ name, etc.) */
    as->cur = as_make_token(AS_TOK_IDENT, word, as->line);
    return as->cur;
  }

  /* Single-character tokens */
  as_next_char(as);
  switch (c) {
    case '[': as->cur = as_make_token(AS_TOK_LBRACK, "[", as->line); return as->cur;
    case ']': as->cur = as_make_token(AS_TOK_RBRACK, "]", as->line); return as->cur;
    case '+': as->cur = as_make_token(AS_TOK_PLUS, "+", as->line); return as->cur;
    case '-':
      /* Check if this starts a negative number */
      if (as_is_digit(as_peek_char(as))) {
        char nbuf[32];
        int ni = 0;
        int32_t val = 0;
        while (as_is_digit(as_peek_char(as)) && ni < 30) {
          char dc = as_next_char(as);
          nbuf[ni++] = dc;
          val = val * 10 + (dc - '0');
        }
        nbuf[ni] = '\0';
        as->cur = as_make_token(AS_TOK_NUMBER, nbuf, as->line);
        as->cur.int_value = -val;
        return as->cur;
      }
      as->cur = as_make_token(AS_TOK_MINUS, "-", as->line);
      return as->cur;
    case '*': as->cur = as_make_token(AS_TOK_STAR, "*", as->line); return as->cur;
    case ',': as->cur = as_make_token(AS_TOK_COMMA, ",", as->line); return as->cur;
    case ':': as->cur = as_make_token(AS_TOK_COLON, ":", as->line); return as->cur;
    default: break;
  }

  /* Unknown character — skip */
  as->cur = as_make_token(AS_TOK_ERROR, "?", as->line);
  return as->cur;
}
