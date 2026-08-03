/**
 * cupidc_lex.cc - Lexer for the CupidC compiler
 *
 * Tokenizes CupidC source code into a stream of tokens.
 * Handles keywords, identifiers, integer literals (decimal & hex),
 * string literals, character literals, operators, and delimiters.
 * Skips whitespace and // line comments.
*/

#include "cupidc.h"
#include "string.h"

static int cc_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int cc_is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int cc_is_digit(char c) { return c >= '0' && c <= '9'; }

static int cc_is_alnum(char c) { return cc_is_alpha(c) || cc_is_digit(c); }

static int cc_is_hexdigit(char c) {
  return cc_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static char cc_peek_char(cc_state_t *cc) {
  if (cc->source[cc->pos] == '\0')
    return '\0';
  return cc->source[cc->pos];
}

static char cc_next_char(cc_state_t *cc) {
  char c = cc->source[cc->pos];
  if (c == '\0')
    return '\0';
  if (c == '\n')
    cc->line++;
  cc->pos++;
  return c;
}

static char cc_peek_char2(cc_state_t *cc) {
  if (cc->source[cc->pos] == '\0')
    return '\0';
  if (cc->source[cc->pos + 1] == '\0')
    return '\0';
  return cc->source[cc->pos + 1];
}

/*
 * The 1536-bit workspace covers a full 95-character numeric token at the
 * binary64 subnormal boundary, including the shift needed for final rounding.
 */
#define CC_DECIMAL_BIG_LIMBS 48

typedef struct {
  uint32_t limb[CC_DECIMAL_BIG_LIMBS];
  uint32_t used;
} cc_big_uint_t;

static void cc_big_zero(cc_big_uint_t *value) {
  uint32_t index;
  value->used = 0u;
  for (index = 0u; index < CC_DECIMAL_BIG_LIMBS; index++)
    value->limb[index] = 0u;
}

static void cc_big_set_u32(cc_big_uint_t *value, uint32_t initial) {
  cc_big_zero(value);
  if (initial != 0u) {
    value->limb[0] = initial;
    value->used = 1u;
  }
}

static void cc_big_normalize(cc_big_uint_t *value) {
  while (value->used != 0u && value->limb[value->used - 1u] == 0u)
    value->used--;
}

static int cc_big_is_zero(const cc_big_uint_t *value) {
  return value->used == 0u;
}

static int cc_big_multiply_add(cc_big_uint_t *value, uint32_t multiplier,
                               uint32_t addend) {
  uint64_t carry = (uint64_t)addend;
  uint32_t index;
  for (index = 0u; index < value->used; index++) {
    uint64_t product =
        (uint64_t)value->limb[index] * (uint64_t)multiplier + carry;
    value->limb[index] = (uint32_t)product;
    carry = product >> 32;
  }
  if (carry != 0ull) {
    if (value->used == CC_DECIMAL_BIG_LIMBS)
      return 0;
    value->limb[value->used++] = (uint32_t)carry;
  } else if (value->used == 0u && addend != 0u) {
    value->limb[0] = addend;
    value->used = 1u;
  }
  return 1;
}

static int cc_big_multiply_power_ten(cc_big_uint_t *value, uint32_t power) {
  while (power != 0u) {
    if (!cc_big_multiply_add(value, 10u, 0u))
      return 0;
    power--;
  }
  return 1;
}

static uint32_t cc_u32_width(uint32_t value) {
  uint32_t width = 0u;
  while (value != 0u) {
    value >>= 1;
    width++;
  }
  return width;
}

static uint32_t cc_big_width(const cc_big_uint_t *value) {
  if (value->used == 0u)
    return 0u;
  return (value->used - 1u) * 32u +
         cc_u32_width(value->limb[value->used - 1u]);
}

static int cc_big_compare(const cc_big_uint_t *left,
                          const cc_big_uint_t *right) {
  uint32_t index;
  if (left->used != right->used)
    return left->used < right->used ? -1 : 1;
  index = left->used;
  while (index != 0u) {
    index--;
    if (left->limb[index] != right->limb[index])
      return left->limb[index] < right->limb[index] ? -1 : 1;
  }
  return 0;
}

static void cc_big_subtract(cc_big_uint_t *left,
                            const cc_big_uint_t *right) {
  uint64_t borrow = 0ull;
  uint32_t index;
  for (index = 0u; index < left->used; index++) {
    uint64_t minuend = (uint64_t)left->limb[index];
    uint64_t subtrahend =
        (index < right->used ? (uint64_t)right->limb[index] : 0ull) +
        borrow;
    if (minuend < subtrahend) {
      left->limb[index] =
          (uint32_t)((1ull << 32) + minuend - subtrahend);
      borrow = 1ull;
    } else {
      left->limb[index] = (uint32_t)(minuend - subtrahend);
      borrow = 0ull;
    }
  }
  cc_big_normalize(left);
}

static int cc_big_shift_left_copy(const cc_big_uint_t *value, uint32_t shift,
                                  cc_big_uint_t *result) {
  uint32_t word_shift = shift / 32u;
  uint32_t bit_shift = shift % 32u;
  uint32_t index;
  cc_big_zero(result);
  if (value->used == 0u)
    return 1;
  if (word_shift >= CC_DECIMAL_BIG_LIMBS)
    return 0;
  for (index = 0u; index < value->used; index++) {
    uint32_t destination = index + word_shift;
    uint64_t part;
    if (destination >= CC_DECIMAL_BIG_LIMBS)
      return 0;
    part = (uint64_t)value->limb[index] << bit_shift;
    result->limb[destination] |= (uint32_t)part;
    if ((part >> 32) != 0ull) {
      if (destination + 1u >= CC_DECIMAL_BIG_LIMBS)
        return 0;
      result->limb[destination + 1u] |= (uint32_t)(part >> 32);
    }
  }
  result->used = value->used + word_shift + (bit_shift != 0u ? 1u : 0u);
  if (result->used > CC_DECIMAL_BIG_LIMBS)
    result->used = CC_DECIMAL_BIG_LIMBS;
  cc_big_normalize(result);
  return 1;
}

static void cc_big_shift_right_one(cc_big_uint_t *value) {
  uint32_t carry = 0u;
  uint32_t index = value->used;
  while (index != 0u) {
    uint32_t next_carry;
    index--;
    next_carry = value->limb[index] & 1u;
    value->limb[index] = (value->limb[index] >> 1) | (carry << 31);
    carry = next_carry;
  }
  cc_big_normalize(value);
}

static int cc_big_binary_exponent(const cc_big_uint_t *numerator,
                                  const cc_big_uint_t *denominator,
                                  int32_t *exponent_out) {
  int32_t exponent =
      (int32_t)cc_big_width(numerator) - (int32_t)cc_big_width(denominator);
  cc_big_uint_t scaled;
  if (exponent >= 0) {
    if (!cc_big_shift_left_copy(denominator, (uint32_t)exponent, &scaled))
      return 0;
    if (cc_big_compare(numerator, &scaled) < 0)
      exponent--;
  } else {
    if (!cc_big_shift_left_copy(numerator, (uint32_t)(0 - exponent),
                                &scaled))
      return 0;
    if (cc_big_compare(&scaled, denominator) < 0)
      exponent--;
  }
  *exponent_out = exponent;
  return 1;
}

static int cc_big_round_ratio(const cc_big_uint_t *numerator_source,
                              const cc_big_uint_t *denominator_source,
                              int32_t binary_scale,
                              uint64_t *rounded_out) {
  cc_big_uint_t numerator = *numerator_source;
  cc_big_uint_t denominator = *denominator_source;
  cc_big_uint_t shifted;
  cc_big_uint_t divisor;
  cc_big_uint_t remainder;
  cc_big_uint_t doubled;
  uint64_t quotient = 0ull;
  uint32_t quotient_shift;

  if (binary_scale >= 0) {
    if (!cc_big_shift_left_copy(&numerator, (uint32_t)binary_scale, &shifted))
      return 0;
    numerator = shifted;
  } else {
    if (!cc_big_shift_left_copy(&denominator,
                                (uint32_t)(0 - binary_scale), &shifted))
      return 0;
    denominator = shifted;
  }

  remainder = numerator;
  if (cc_big_compare(&remainder, &denominator) >= 0) {
    quotient_shift = cc_big_width(&remainder) - cc_big_width(&denominator);
    if (quotient_shift >= 64u ||
        !cc_big_shift_left_copy(&denominator, quotient_shift, &divisor))
      return 0;
    for (;;) {
      if (cc_big_compare(&remainder, &divisor) >= 0) {
        cc_big_subtract(&remainder, &divisor);
        quotient |= 1ull << quotient_shift;
      }
      if (quotient_shift == 0u)
        break;
      quotient_shift--;
      cc_big_shift_right_one(&divisor);
    }
  }

  if (!cc_big_shift_left_copy(&remainder, 1u, &doubled))
    return 0;
  if (cc_big_compare(&doubled, &denominator) > 0 ||
      (cc_big_compare(&doubled, &denominator) == 0 &&
       (quotient & 1ull) != 0ull))
    quotient++;
  *rounded_out = quotient;
  return 1;
}

/*
 * Convert one decimal token with integer arithmetic. The result is rounded
 * once to the requested IEEE width using round-to-nearest, ties-to-even.
*/
static const char *cc_decimal_floating_bits(const char *text, int target_bits,
                                            uint64_t *bits_out) {
  int index = 0;
  int digit_count = 0;
  int significant_digits = 0;
  int fractional_digits = 0;
  int after_point = 0;
  int exponent_negative = 0;
  uint32_t exponent_magnitude = 0u;
  cc_big_uint_t numerator;
  cc_big_uint_t denominator;
  int32_t decimal_exponent;
  int32_t adjusted_decimal_exponent;
  int32_t binary_exponent;
  uint32_t precision = target_bits == 32 ? 24u : 53u;
  int32_t minimum_normal_exponent = target_bits == 32 ? -126 : -1022;
  int32_t minimum_subnormal_exponent = target_bits == 32 ? -149 : -1074;
  int32_t maximum_binary_exponent = target_bits == 32 ? 127 : 1023;
  int32_t maximum_decimal_exponent = target_bits == 32 ? 38 : 308;
  int32_t minimum_decimal_exponent = target_bits == 32 ? -46 : -324;
  uint32_t exponent_bias = target_bits == 32 ? 127u : 1023u;
  uint32_t mantissa_width = target_bits == 32 ? 23u : 52u;
  uint64_t rounded;
  uint64_t normal_significand = 1ull << (precision - 1u);
  uint64_t infinity_bits = target_bits == 32 ? 0x7f800000ull
                                             : 0x7ff0000000000000ull;

  if (target_bits != 32 && target_bits != 64)
    return "decimal floating literal has an invalid target width";

  cc_big_zero(&numerator);
  while (text[index] != '\0' && text[index] != 'e' && text[index] != 'E') {
    char character = text[index++];
    uint32_t digit;
    if (character == '.') {
      if (after_point)
        return "decimal floating literal has more than one decimal point";
      after_point = 1;
      continue;
    }
    if (!cc_is_digit(character))
      return "decimal floating literal contains an invalid digit";
    digit_count++;
    digit = (uint32_t)(character - '0');
    if (significant_digits != 0 || digit != 0u)
      significant_digits++;
    if (after_point)
      fractional_digits++;
    if (!cc_big_multiply_add(&numerator, 10u, digit))
      return "decimal floating literal is too long";
  }

  if (digit_count == 0)
    return "decimal floating literal requires a digit";

  if (text[index] == 'e' || text[index] == 'E') {
    int exponent_digits = 0;
    index++;
    if (text[index] == '+' || text[index] == '-') {
      exponent_negative = text[index] == '-';
      index++;
    }
    while (cc_is_digit(text[index])) {
      uint32_t digit = (uint32_t)(text[index] - '0');
      if (exponent_magnitude > 1000u ||
          exponent_magnitude * 10u + digit > 10000u)
        exponent_magnitude = 10000u;
      else
        exponent_magnitude = exponent_magnitude * 10u + digit;
      exponent_digits++;
      index++;
    }
    if (exponent_digits == 0)
      return "decimal floating literal exponent requires a digit";
  }

  if (text[index] != '\0')
    return "decimal floating literal has an invalid suffix";

  decimal_exponent =
      (exponent_negative ? 0 - (int32_t)exponent_magnitude
                         : (int32_t)exponent_magnitude) -
      (int32_t)fractional_digits;

  if (cc_big_is_zero(&numerator)) {
    *bits_out = 0ull;
    return NULL;
  }

  adjusted_decimal_exponent =
      decimal_exponent + (int32_t)significant_digits - 1;
  if (adjusted_decimal_exponent > maximum_decimal_exponent) {
    *bits_out = infinity_bits;
    return NULL;
  }
  if (adjusted_decimal_exponent < minimum_decimal_exponent) {
    *bits_out = 0ull;
    return NULL;
  }

  cc_big_set_u32(&denominator, 1u);
  if (decimal_exponent >= 0) {
    if (!cc_big_multiply_power_ten(&numerator,
                                   (uint32_t)decimal_exponent))
      return "decimal floating literal exceeds the conversion capacity";
  } else if (!cc_big_multiply_power_ten(
                 &denominator, (uint32_t)(0 - decimal_exponent))) {
    return "decimal floating literal exceeds the conversion capacity";
  }

  if (!cc_big_binary_exponent(&numerator, &denominator, &binary_exponent))
    return "decimal floating literal exceeds the conversion capacity";

  if (binary_exponent > maximum_binary_exponent) {
    *bits_out = infinity_bits;
    return NULL;
  }

  if (binary_exponent >= minimum_normal_exponent) {
    if (!cc_big_round_ratio(&numerator, &denominator,
                            (int32_t)(precision - 1u) - binary_exponent,
                            &rounded))
      return "decimal floating literal exceeds the conversion capacity";
    if (rounded == (1ull << precision)) {
      rounded >>= 1;
      binary_exponent++;
    }
    if (binary_exponent > maximum_binary_exponent) {
      *bits_out = infinity_bits;
      return NULL;
    }
    if (rounded < normal_significand ||
        rounded >= (1ull << precision))
      return "decimal floating literal conversion failed";
    *bits_out =
        ((uint64_t)((uint32_t)(binary_exponent +
                              (int32_t)exponent_bias))
         << mantissa_width) |
        (rounded - normal_significand);
    return NULL;
  }

  if (!cc_big_round_ratio(&numerator, &denominator,
                          0 - minimum_subnormal_exponent, &rounded))
    return "decimal floating literal exceeds the conversion capacity";
  if (rounded > normal_significand)
    return "decimal floating literal conversion failed";
  if (rounded == normal_significand)
    *bits_out = 1ull << mantissa_width;
  else
    *bits_out = rounded;
  return NULL;
}

static double cc_decimal_bits_as_double(uint64_t bits, int source_bits) {
  if (source_bits == 32) {
    uint32_t narrow_bits = (uint32_t)bits;
    float narrow;
    memcpy(&narrow, &narrow_bits, 4);
    return (double)narrow;
  }
  {
    double wide;
    memcpy(&wide, &bits, 8);
    return wide;
  }
}

static void cc_skip_whitespace(cc_state_t *cc) {
  for (;;) {
    char c = cc_peek_char(cc);

    /* Whitespace */
    if (cc_is_space(c)) {
      cc_next_char(cc);
      continue;
    }

    /* Line comment: // ... */
    if (c == '/' && cc_peek_char2(cc) == '/') {
      cc_next_char(cc);
      cc_next_char(cc);
      while (cc_peek_char(cc) != '\0' && cc_peek_char(cc) != '\n') {
        cc_next_char(cc);
      }
      continue;
    }

    /* Block comment: / * ... * / */
    if (c == '/' && cc_peek_char2(cc) == '*') {
      cc_next_char(cc);
      cc_next_char(cc);
      while (cc_peek_char(cc) != '\0') {
        if (cc_peek_char(cc) == '*' && cc_peek_char2(cc) == '/') {
          cc_next_char(cc);
          cc_next_char(cc);
          break;
        }
        cc_next_char(cc);
      }
      continue;
    }

    break;
  }
}

static cc_token_type_t cc_check_keyword(const char *text) {
  if (strcmp(text, "int") == 0)
    return CC_TOK_INT;
  if (strcmp(text, "char") == 0)
    return CC_TOK_CHAR;
  if (strcmp(text, "void") == 0)
    return CC_TOK_VOID;
  if (strcmp(text, "U0") == 0)
    return CC_TOK_U0;
  if (strcmp(text, "U8") == 0)
    return CC_TOK_U8;
  if (strcmp(text, "uint8_t") == 0)
    return CC_TOK_U8;
  if (strcmp(text, "U16") == 0)
    return CC_TOK_U16;
  if (strcmp(text, "uint16_t") == 0)
    return CC_TOK_U16;
  if (strcmp(text, "U32") == 0)
    return CC_TOK_U32;
  if (strcmp(text, "uint32_t") == 0)
    return CC_TOK_U32;
  if (strcmp(text, "I8") == 0)
    return CC_TOK_I8;
  if (strcmp(text, "int8_t") == 0)
    return CC_TOK_I8;
  if (strcmp(text, "I16") == 0)
    return CC_TOK_I16;
  if (strcmp(text, "int16_t") == 0)
    return CC_TOK_I16;
  if (strcmp(text, "I32") == 0)
    return CC_TOK_I32;
  if (strcmp(text, "int32_t") == 0)
    return CC_TOK_I32;
  if (strcmp(text, "float") == 0)
    return CC_TOK_FLOAT;
  if (strcmp(text, "double") == 0)
    return CC_TOK_DOUBLE;
  if (strcmp(text, "float4") == 0)
    return CC_TOK_FLOAT4;
  if (strcmp(text, "double2") == 0)
    return CC_TOK_DOUBLE2;
  if (strcmp(text, "bool") == 0)
    return CC_TOK_BOOL;
  if (strcmp(text, "Bool") == 0)
    return CC_TOK_BOOL;
  if (strcmp(text, "if") == 0)
    return CC_TOK_IF;
  if (strcmp(text, "else") == 0)
    return CC_TOK_ELSE;
  if (strcmp(text, "while") == 0)
    return CC_TOK_WHILE;
  if (strcmp(text, "for") == 0)
    return CC_TOK_FOR;
  if (strcmp(text, "do") == 0)
    return CC_TOK_DO;
  if (strcmp(text, "return") == 0)
    return CC_TOK_RETURN;
  if (strcmp(text, "asm") == 0)
    return CC_TOK_ASM;
  if (strcmp(text, "break") == 0)
    return CC_TOK_BREAK;
  if (strcmp(text, "continue") == 0)
    return CC_TOK_CONTINUE;
  if (strcmp(text, "struct") == 0)
    return CC_TOK_STRUCT;
  if (strcmp(text, "class") == 0)
    return CC_TOK_CLASS;
  if (strcmp(text, "sizeof") == 0)
    return CC_TOK_SIZEOF;
  if (strcmp(text, "switch") == 0)
    return CC_TOK_SWITCH;
  if (strcmp(text, "case") == 0)
    return CC_TOK_CASE;
  if (strcmp(text, "default") == 0)
    return CC_TOK_DEFAULT;
  if (strcmp(text, "new") == 0)
    return CC_TOK_NEW;
  if (strcmp(text, "del") == 0)
    return CC_TOK_DEL;
  if (strcmp(text, "enum") == 0)
    return CC_TOK_ENUM;
  if (strcmp(text, "unsigned") == 0)
    return CC_TOK_UNSIGNED;
  if (strcmp(text, "signed") == 0)
    return CC_TOK_SIGNED;
  if (strcmp(text, "long") == 0)
    return CC_TOK_LONG;
  if (strcmp(text, "short") == 0)
    return CC_TOK_SHORT;
  if (strcmp(text, "U64") == 0)
    return CC_TOK_U64;
  if (strcmp(text, "uint64_t") == 0)
    return CC_TOK_U64;
  if (strcmp(text, "I64") == 0)
    return CC_TOK_I64;
  if (strcmp(text, "int64_t") == 0)
    return CC_TOK_I64;
  if (strcmp(text, "typedef") == 0)
    return CC_TOK_TYPEDEF;
  if (strcmp(text, "const") == 0)
    return CC_TOK_CONST;
  if (strcmp(text, "static") == 0)
    return CC_TOK_STATIC;
  if (strcmp(text, "extern") == 0)
    return CC_TOK_EXTERN;
  if (strcmp(text, "inline") == 0)
    return CC_TOK_INLINE;
  if (strcmp(text, "register") == 0)
    return CC_TOK_REGISTER;
  if (strcmp(text, "restrict") == 0)
    return CC_TOK_RESTRICT;
  if (strcmp(text, "__restrict") == 0)
    return CC_TOK_RESTRICT;
  if (strcmp(text, "__restrict__") == 0)
    return CC_TOK_RESTRICT;
  if (strcmp(text, "goto") == 0)
    return CC_TOK_GOTO;
  if (strcmp(text, "__attribute__") == 0)
    return CC_TOK_ATTRIBUTE;
  if (strcmp(text, "volatile") == 0)
    return CC_TOK_VOLATILE;
  if (strcmp(text, "reg") == 0)
    return CC_TOK_REG;
  if (strcmp(text, "noreg") == 0)
    return CC_TOK_NOREG;
  return CC_TOK_IDENT;
}

void cc_lex_init(cc_state_t *cc, const char *source) {
  cc->source = source;
  cc->pos = 0;
  cc->line = 1;
  cc->has_peek = 0;
}

static char cc_parse_escape(cc_state_t *cc) {
  char c = cc_next_char(cc);
  switch (c) {
  case 'n':
    return '\n';
  case 't':
    return '\t';
  case 'r':
    return '\r';
  case 'b':
    return '\b';
  case '\\':
    return '\\';
  case '\'':
    return '\'';
  case '"':
    return '"';
  case '0':
    return '\0';
  case 'x': {
    /* Hexadecimal escape: \xNN */
    char h1 = cc_next_char(cc);
    char h2 = cc_next_char(cc);
    int v1 = (h1 >= '0' && h1 <= '9')   ? (h1 - '0')
             : (h1 >= 'a' && h1 <= 'f') ? (h1 - 'a' + 10)
             : (h1 >= 'A' && h1 <= 'F') ? (h1 - 'A' + 10)
                                        : 0;
    int v2 = (h2 >= '0' && h2 <= '9')   ? (h2 - '0')
             : (h2 >= 'a' && h2 <= 'f') ? (h2 - 'a' + 10)
             : (h2 >= 'A' && h2 <= 'F') ? (h2 - 'A' + 10)
                                        : 0;
    return (char)((v1 << 4) | v2);
  }
  default:
    return c;
  }
}

cc_token_t cc_lex_next(cc_state_t *cc) {
  /* If we have a peeked token, return it */
  if (cc->has_peek) {
    cc->has_peek = 0;
    cc->cur = cc->peek_buf;
    return cc->cur;
  }

  cc_skip_whitespace(cc);

  cc_token_t tok;
  memset(&tok, 0, sizeof(tok));
  tok.line = cc->line;

  char c = cc_peek_char(cc);

  /* End of file */
  if (c == '\0') {
    tok.type = CC_TOK_EOF;
    tok.text[0] = '\0';
    cc->cur = tok;
    return tok;
  }

  /* Identifier or keyword */
  if (cc_is_alpha(c)) {
    int i = 0;
    while (cc_is_alnum(cc_peek_char(cc)) && i < CC_MAX_IDENT - 1) {
      tok.text[i++] = cc_next_char(cc);
    }
    tok.text[i] = '\0';
    tok.type = cc_check_keyword(tok.text);

    /* Built-in constants: NULL, true, false -> integer literals */
    if (tok.type == CC_TOK_IDENT) {
      if (strcmp(tok.text, "NULL") == 0) {
        tok.type = CC_TOK_NUMBER;
        tok.int_value = 0;
      } else if (strcmp(tok.text, "true") == 0) {
        tok.type = CC_TOK_NUMBER;
        tok.int_value = 1;
      } else if (strcmp(tok.text, "false") == 0) {
        tok.type = CC_TOK_NUMBER;
        tok.int_value = 0;
      }
    }

    cc->cur = tok;
    return tok;
  }

  /* Number literal (integer or float) */
  /* Also handle leading-dot floats like .5 - '.' followed by a digit. */
  if (cc_is_digit(c) || (c == '.' && cc_is_digit(cc_peek_char2(cc)))) {
    int i = 0;
    int32_t val = 0;
    int is_float = 0;
    int f_suffix = 0;
    int literal_too_long = 0;

    /* Check for hex: 0x... or 0X... (integer only - no hex floats) */
    if (c == '0' && (cc_peek_char2(cc) == 'x' || cc_peek_char2(cc) == 'X')) {
      tok.text[i++] = cc_next_char(cc); /* '0' */
      tok.text[i++] = cc_next_char(cc); /* 'x'/'X' */
      while (cc_is_hexdigit(cc_peek_char(cc)) && i < CC_MAX_IDENT - 1) {
        char h = cc_next_char(cc);
        tok.text[i++] = h;
        val *= 16;
        if (h >= '0' && h <= '9')
          val += h - '0';
        else if (h >= 'a' && h <= 'f')
          val += h - 'a' + 10;
        else if (h >= 'A' && h <= 'F')
          val += h - 'A' + 10;
      }

      /* Accept optional unsigned suffix: 0xFFU */
      if ((cc_peek_char(cc) == 'u' || cc_peek_char(cc) == 'U') &&
          i < CC_MAX_IDENT - 1) {
        tok.text[i++] = cc_next_char(cc);
      }

      tok.text[i] = '\0';
      tok.type = CC_TOK_NUMBER;
      tok.int_value = val;
      cc->cur = tok;
      return tok;
    }

    /* Decimal integer part (may be empty for ".5") */
    while (cc_is_digit(cc_peek_char(cc))) {
      char d = cc_next_char(cc);
      if (i < CC_MAX_IDENT - 1) {
        tok.text[i++] = d;
        val = val * 10 + (d - '0');
      } else {
        literal_too_long = 1;
      }
    }

    /* Fractional part */
    if (cc_peek_char(cc) == '.') {
      is_float = 1;
      if (i < CC_MAX_IDENT - 1)
        tok.text[i++] = cc_next_char(cc); /* '.' */
      else {
        cc_next_char(cc);
        literal_too_long = 1;
      }
      while (cc_is_digit(cc_peek_char(cc))) {
        char digit = cc_next_char(cc);
        if (i < CC_MAX_IDENT - 1)
          tok.text[i++] = digit;
        else
          literal_too_long = 1;
      }
    }

    /* Exponent */
    if (cc_peek_char(cc) == 'e' || cc_peek_char(cc) == 'E') {
      is_float = 1;
      if (i < CC_MAX_IDENT - 1)
        tok.text[i++] = cc_next_char(cc); /* 'e'/'E' */
      else {
        cc_next_char(cc);
        literal_too_long = 1;
      }
      if (cc_peek_char(cc) == '+' || cc_peek_char(cc) == '-') {
        char sign = cc_next_char(cc);
        if (i < CC_MAX_IDENT - 1)
          tok.text[i++] = sign;
        else
          literal_too_long = 1;
      }
      while (cc_is_digit(cc_peek_char(cc))) {
        char digit = cc_next_char(cc);
        if (i < CC_MAX_IDENT - 1)
          tok.text[i++] = digit;
        else
          literal_too_long = 1;
      }
    }

    /* 'f'/'F' suffix forces 32-bit; consume but don't add to text */
    if (cc_peek_char(cc) == 'f' || cc_peek_char(cc) == 'F') {
      is_float = 1;
      f_suffix = 1;
      if (i >= CC_MAX_IDENT - 1)
        literal_too_long = 1;
      cc_next_char(cc);
    }

    if (literal_too_long) {
      const char *message = "numeric literal exceeds 95 characters";
      int message_index = 0;
      while (message[message_index] != '\0') {
        tok.text[message_index] = message[message_index];
        message_index++;
      }
      tok.text[message_index] = '\0';
      tok.type = CC_TOK_ERROR;
      cc->cur = tok;
      return tok;
    }

    if (is_float) {
      uint64_t literal_bits;
      int literal_width = f_suffix ? 32 : 64;
      const char *literal_error;
      tok.text[i] = '\0';
      literal_error =
          cc_decimal_floating_bits(tok.text, literal_width, &literal_bits);
      if (literal_error != NULL) {
        int error_index = 0;
        while (literal_error[error_index] != '\0' &&
               error_index < CC_MAX_STRING - 1) {
          tok.text[error_index] = literal_error[error_index];
          error_index++;
        }
        tok.text[error_index] = '\0';
        tok.type = CC_TOK_ERROR;
        cc->cur = tok;
        return tok;
      }
      tok.type = CC_TOK_FLIT;
      tok.fval = cc_decimal_bits_as_double(literal_bits, literal_width);
      tok.flit_bits = literal_width;
      cc->cur = tok;
      return tok;
    }

    /* Integer: accept optional unsigned suffix: 123u */
    if ((cc_peek_char(cc) == 'u' || cc_peek_char(cc) == 'U') &&
        i < CC_MAX_IDENT - 1) {
      tok.text[i++] = cc_next_char(cc);
    }

    tok.text[i] = '\0';
    tok.type = CC_TOK_NUMBER;
    tok.int_value = val;
    cc->cur = tok;
    return tok;
  }

  /* String literal */
  if (c == '"') {
    cc_next_char(cc); /* consume opening quote */
    int i = 0;
    while (cc_peek_char(cc) != '"' && cc_peek_char(cc) != '\0' &&
           i < CC_MAX_STRING - 1) {
      if (cc_peek_char(cc) == '\\') {
        cc_next_char(cc); /* consume backslash */
        tok.text[i++] = cc_parse_escape(cc);
      } else {
        tok.text[i++] = cc_next_char(cc);
      }
    }
    tok.text[i] = '\0';
    tok.int_value = i; /* store string length */
    if (cc_peek_char(cc) == '"')
      cc_next_char(cc); /* consume closing quote */
    tok.type = CC_TOK_STRING;
    cc->cur = tok;
    return tok;
  }

  /* Character literal */
  if (c == '\'') {
    cc_next_char(cc); /* consume opening quote */
    if (cc_peek_char(cc) == '\\') {
      cc_next_char(cc);
      tok.int_value = (int32_t)(unsigned char)cc_parse_escape(cc);
    } else {
      tok.int_value = (int32_t)(unsigned char)cc_next_char(cc);
    }
    tok.text[0] = (char)tok.int_value;
    tok.text[1] = '\0';
    if (cc_peek_char(cc) == '\'')
      cc_next_char(cc); /* consume closing quote */
    tok.type = CC_TOK_CHAR_LIT;
    cc->cur = tok;
    return tok;
  }

  /* Operators and delimiters */
  cc_next_char(cc); /* consume first character */

  switch (c) {
  case '+':
    if (cc_peek_char(cc) == '+') {
      cc_next_char(cc);
      tok.type = CC_TOK_PLUSPLUS;
      tok.text[0] = '+';
      tok.text[1] = '+';
      tok.text[2] = '\0';
    } else if (cc_peek_char(cc) == '=') {
      cc_next_char(cc);
      tok.type = CC_TOK_PLUSEQ;
      tok.text[0] = '+';
      tok.text[1] = '=';
      tok.text[2] = '\0';
    } else {
      tok.type = CC_TOK_PLUS;
      tok.text[0] = '+';
      tok.text[1] = '\0';
    }
    break;

  case '-':
    if (cc_peek_char(cc) == '-') {
      cc_next_char(cc);
      tok.type = CC_TOK_MINUSMINUS;
      tok.text[0] = '-';
      tok.text[1] = '-';
      tok.text[2] = '\0';
    } else if (cc_peek_char(cc) == '=') {
      cc_next_char(cc);
      tok.type = CC_TOK_MINUSEQ;
      tok.text[0] = '-';
      tok.text[1] = '=';
      tok.text[2] = '\0';
    } else if (cc_peek_char(cc) == '>') {
      cc_next_char(cc);
      tok.type = CC_TOK_ARROW;
      tok.text[0] = '-';
      tok.text[1] = '>';
      tok.text[2] = '\0';
    } else {
      tok.type = CC_TOK_MINUS;
      tok.text[0] = '-';
      tok.text[1] = '\0';
    }
    break;

  case '*':
    if (cc_peek_char(cc) == '=') {
      cc_next_char(cc);
      tok.type = CC_TOK_STAREQ;
      tok.text[0] = '*';
      tok.text[1] = '=';
      tok.text[2] = '\0';
    } else {
      tok.type = CC_TOK_STAR;
      tok.text[0] = '*';
      tok.text[1] = '\0';
    }
    break;

  case '/':
    if (cc_peek_char(cc) == '=') {
      cc_next_char(cc);
      tok.type = CC_TOK_SLASHEQ;
      tok.text[0] = '/';
      tok.text[1] = '=';
      tok.text[2] = '\0';
    } else {
      tok.type = CC_TOK_SLASH;
      tok.text[0] = '/';
      tok.text[1] = '\0';
    }
    break;

  case '%':
    tok.type = CC_TOK_PERCENT;
    tok.text[0] = '%';
    tok.text[1] = '\0';
    break;

  case '=':
    if (cc_peek_char(cc) == '=') {
      cc_next_char(cc);
      tok.type = CC_TOK_EQEQ;
      tok.text[0] = '=';
      tok.text[1] = '=';
      tok.text[2] = '\0';
    } else {
      tok.type = CC_TOK_EQ;
      tok.text[0] = '=';
      tok.text[1] = '\0';
    }
    break;

  case '!':
    if (cc_peek_char(cc) == '=') {
      cc_next_char(cc);
      tok.type = CC_TOK_NE;
      tok.text[0] = '!';
      tok.text[1] = '=';
      tok.text[2] = '\0';
    } else {
      tok.type = CC_TOK_NOT;
      tok.text[0] = '!';
      tok.text[1] = '\0';
    }
    break;

  case '<':
    if (cc_peek_char(cc) == '=') {
      cc_next_char(cc);
      tok.type = CC_TOK_LE;
      tok.text[0] = '<';
      tok.text[1] = '=';
      tok.text[2] = '\0';
    } else if (cc_peek_char(cc) == '<') {
      cc_next_char(cc);
      if (cc_peek_char(cc) == '=') {
        cc_next_char(cc);
        tok.type = CC_TOK_SHLEQ;
        tok.text[0] = '<';
        tok.text[1] = '<';
        tok.text[2] = '=';
        tok.text[3] = '\0';
      } else {
        tok.type = CC_TOK_SHL;
        tok.text[0] = '<';
        tok.text[1] = '<';
        tok.text[2] = '\0';
      }
    } else {
      tok.type = CC_TOK_LT;
      tok.text[0] = '<';
      tok.text[1] = '\0';
    }
    break;

  case '>':
    if (cc_peek_char(cc) == '=') {
      cc_next_char(cc);
      tok.type = CC_TOK_GE;
      tok.text[0] = '>';
      tok.text[1] = '=';
      tok.text[2] = '\0';
    } else if (cc_peek_char(cc) == '>') {
      cc_next_char(cc);
      if (cc_peek_char(cc) == '=') {
        cc_next_char(cc);
        tok.type = CC_TOK_SHREQ;
        tok.text[0] = '>';
        tok.text[1] = '>';
        tok.text[2] = '=';
        tok.text[3] = '\0';
      } else {
        tok.type = CC_TOK_SHR;
        tok.text[0] = '>';
        tok.text[1] = '>';
        tok.text[2] = '\0';
      }
    } else {
      tok.type = CC_TOK_GT;
      tok.text[0] = '>';
      tok.text[1] = '\0';
    }
    break;

  case '&':
    if (cc_peek_char(cc) == '&') {
      cc_next_char(cc);
      tok.type = CC_TOK_AND;
      tok.text[0] = '&';
      tok.text[1] = '&';
      tok.text[2] = '\0';
    } else if (cc_peek_char(cc) == '=') {
      cc_next_char(cc);
      tok.type = CC_TOK_ANDEQ;
      tok.text[0] = '&';
      tok.text[1] = '=';
      tok.text[2] = '\0';
    } else {
      tok.type = CC_TOK_AMP;
      tok.text[0] = '&';
      tok.text[1] = '\0';
    }
    break;

  case '|':
    if (cc_peek_char(cc) == '|') {
      cc_next_char(cc);
      tok.type = CC_TOK_OR;
      tok.text[0] = '|';
      tok.text[1] = '|';
      tok.text[2] = '\0';
    } else if (cc_peek_char(cc) == '=') {
      cc_next_char(cc);
      tok.type = CC_TOK_OREQ;
      tok.text[0] = '|';
      tok.text[1] = '=';
      tok.text[2] = '\0';
    } else {
      tok.type = CC_TOK_BOR;
      tok.text[0] = '|';
      tok.text[1] = '\0';
    }
    break;

  case '^':
    if (cc_peek_char(cc) == '=') {
      cc_next_char(cc);
      tok.type = CC_TOK_XOREQ;
      tok.text[0] = '^';
      tok.text[1] = '=';
      tok.text[2] = '\0';
    } else {
      tok.type = CC_TOK_BXOR;
      tok.text[0] = '^';
      tok.text[1] = '\0';
    }
    break;

  case '~':
    tok.type = CC_TOK_BNOT;
    tok.text[0] = '~';
    tok.text[1] = '\0';
    break;

  case '(':
    tok.type = CC_TOK_LPAREN;
    tok.text[0] = '(';
    tok.text[1] = '\0';
    break;
  case ')':
    tok.type = CC_TOK_RPAREN;
    tok.text[0] = ')';
    tok.text[1] = '\0';
    break;
  case '{':
    tok.type = CC_TOK_LBRACE;
    tok.text[0] = '{';
    tok.text[1] = '\0';
    break;
  case '}':
    tok.type = CC_TOK_RBRACE;
    tok.text[0] = '}';
    tok.text[1] = '\0';
    break;
  case '[':
    tok.type = CC_TOK_LBRACK;
    tok.text[0] = '[';
    tok.text[1] = '\0';
    break;
  case ']':
    tok.type = CC_TOK_RBRACK;
    tok.text[0] = ']';
    tok.text[1] = '\0';
    break;
  case ';':
    tok.type = CC_TOK_SEMICOLON;
    tok.text[0] = ';';
    tok.text[1] = '\0';
    break;
  case ',':
    tok.type = CC_TOK_COMMA;
    tok.text[0] = ',';
    tok.text[1] = '\0';
    break;
  case '.':
    if (cc_peek_char(cc) == '.' && cc_peek_char2(cc) == '.') {
      cc_next_char(cc);
      cc_next_char(cc);
      tok.type = CC_TOK_ELLIPSIS;
      tok.text[0] = '.';
      tok.text[1] = '.';
      tok.text[2] = '.';
      tok.text[3] = '\0';
    } else {
      tok.type = CC_TOK_DOT;
      tok.text[0] = '.';
      tok.text[1] = '\0';
    }
    break;
  case ':':
    tok.type = CC_TOK_COLON;
    tok.text[0] = ':';
    tok.text[1] = '\0';
    break;
  case '?':
    tok.type = CC_TOK_QUESTION;
    tok.text[0] = '?';
    tok.text[1] = '\0';
    break;

  default:
    tok.type = CC_TOK_ERROR;
    tok.text[0] = c;
    tok.text[1] = '\0';
    break;
  }

  cc->cur = tok;
  return tok;
}

cc_token_t cc_lex_peek(cc_state_t *cc) {
  if (cc->has_peek) {
    return cc->peek_buf;
  }
  /* Save current token, lex next, restore */
  cc_token_t saved = cc->cur;
  cc->peek_buf = cc_lex_next(cc);
  cc->cur = saved;
  cc->has_peek = 1;
  return cc->peek_buf;
}
