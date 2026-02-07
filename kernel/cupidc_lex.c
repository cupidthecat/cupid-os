/**
 * cupidc_lex.c — Lexer for the CupidC compiler
 *
 * Tokenizes CupidC source code into a stream of tokens.
 * Handles keywords, identifiers, integer literals (decimal & hex),
 * string literals, character literals, operators, and delimiters.
 * Skips whitespace and // line comments.
 */

#include "cupidc.h"
#include "string.h"

/* ── Character classification helpers ────────────────────────────── */

static int cc_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int cc_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static int cc_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int cc_is_alnum(char c) {
    return cc_is_alpha(c) || cc_is_digit(c);
}

static int cc_is_hexdigit(char c) {
    return cc_is_digit(c) ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/* ── Peek at current character without consuming ─────────────────── */

static char cc_peek_char(cc_state_t *cc) {
    if (cc->source[cc->pos] == '\0') return '\0';
    return cc->source[cc->pos];
}

static char cc_next_char(cc_state_t *cc) {
    char c = cc->source[cc->pos];
    if (c == '\0') return '\0';
    if (c == '\n') cc->line++;
    cc->pos++;
    return c;
}

static char cc_peek_char2(cc_state_t *cc) {
    if (cc->source[cc->pos] == '\0') return '\0';
    if (cc->source[cc->pos + 1] == '\0') return '\0';
    return cc->source[cc->pos + 1];
}

/* ── Skip whitespace and comments ────────────────────────────────── */

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

/* ── Keyword matching ────────────────────────────────────────────── */

static cc_token_type_t cc_check_keyword(const char *text) {
    if (strcmp(text, "int") == 0)       return CC_TOK_INT;
    if (strcmp(text, "char") == 0)      return CC_TOK_CHAR;
    if (strcmp(text, "void") == 0)      return CC_TOK_VOID;
    if (strcmp(text, "if") == 0)        return CC_TOK_IF;
    if (strcmp(text, "else") == 0)      return CC_TOK_ELSE;
    if (strcmp(text, "while") == 0)     return CC_TOK_WHILE;
    if (strcmp(text, "for") == 0)       return CC_TOK_FOR;
    if (strcmp(text, "return") == 0)    return CC_TOK_RETURN;
    if (strcmp(text, "asm") == 0)       return CC_TOK_ASM;
    if (strcmp(text, "break") == 0)     return CC_TOK_BREAK;
    if (strcmp(text, "continue") == 0)  return CC_TOK_CONTINUE;
    return CC_TOK_IDENT;
}

/* ── Initialize lexer ────────────────────────────────────────────── */

void cc_lex_init(cc_state_t *cc, const char *source) {
    cc->source   = source;
    cc->pos      = 0;
    cc->line     = 1;
    cc->has_peek = 0;
}

/* ── Parse an escape character ───────────────────────────────────── */

static char cc_parse_escape(cc_state_t *cc) {
    char c = cc_next_char(cc);
    switch (c) {
        case 'n':  return '\n';
        case 't':  return '\t';
        case 'r':  return '\r';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"':  return '"';
        case '0':  return '\0';
        default:   return c;
    }
}

/* ── Lex next token ──────────────────────────────────────────────── */

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
        cc->cur = tok;
        return tok;
    }

    /* Number literal */
    if (cc_is_digit(c)) {
        int i = 0;
        int32_t val = 0;

        /* Check for hex: 0x... */
        if (c == '0' && cc_peek_char2(cc) == 'x') {
            tok.text[i++] = cc_next_char(cc); /* '0' */
            tok.text[i++] = cc_next_char(cc); /* 'x' */
            while (cc_is_hexdigit(cc_peek_char(cc)) && i < CC_MAX_IDENT - 1) {
                char h = cc_next_char(cc);
                tok.text[i++] = h;
                val *= 16;
                if (h >= '0' && h <= '9')      val += h - '0';
                else if (h >= 'a' && h <= 'f') val += h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') val += h - 'A' + 10;
            }
        } else {
            /* Decimal */
            while (cc_is_digit(cc_peek_char(cc)) && i < CC_MAX_IDENT - 1) {
                char d = cc_next_char(cc);
                tok.text[i++] = d;
                val = val * 10 + (d - '0');
            }
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
        if (cc_peek_char(cc) == '"') cc_next_char(cc); /* consume closing quote */
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
        if (cc_peek_char(cc) == '\'') cc_next_char(cc); /* consume closing quote */
        tok.type = CC_TOK_CHAR_LIT;
        cc->cur = tok;
        return tok;
    }

    /* Operators and delimiters */
    cc_next_char(cc); /* consume first character */

    switch (c) {
    case '+':
        if (cc_peek_char(cc) == '+') {
            cc_next_char(cc); tok.type = CC_TOK_PLUSPLUS;
            tok.text[0] = '+'; tok.text[1] = '+'; tok.text[2] = '\0';
        } else if (cc_peek_char(cc) == '=') {
            cc_next_char(cc); tok.type = CC_TOK_PLUSEQ;
            tok.text[0] = '+'; tok.text[1] = '='; tok.text[2] = '\0';
        } else {
            tok.type = CC_TOK_PLUS;
            tok.text[0] = '+'; tok.text[1] = '\0';
        }
        break;

    case '-':
        if (cc_peek_char(cc) == '-') {
            cc_next_char(cc); tok.type = CC_TOK_MINUSMINUS;
            tok.text[0] = '-'; tok.text[1] = '-'; tok.text[2] = '\0';
        } else if (cc_peek_char(cc) == '=') {
            cc_next_char(cc); tok.type = CC_TOK_MINUSEQ;
            tok.text[0] = '-'; tok.text[1] = '='; tok.text[2] = '\0';
        } else {
            tok.type = CC_TOK_MINUS;
            tok.text[0] = '-'; tok.text[1] = '\0';
        }
        break;

    case '*':
        if (cc_peek_char(cc) == '=') {
            cc_next_char(cc); tok.type = CC_TOK_STAREQ;
            tok.text[0] = '*'; tok.text[1] = '='; tok.text[2] = '\0';
        } else {
            tok.type = CC_TOK_STAR;
            tok.text[0] = '*'; tok.text[1] = '\0';
        }
        break;

    case '/':
        if (cc_peek_char(cc) == '=') {
            cc_next_char(cc); tok.type = CC_TOK_SLASHEQ;
            tok.text[0] = '/'; tok.text[1] = '='; tok.text[2] = '\0';
        } else {
            tok.type = CC_TOK_SLASH;
            tok.text[0] = '/'; tok.text[1] = '\0';
        }
        break;

    case '%':
        tok.type = CC_TOK_PERCENT;
        tok.text[0] = '%'; tok.text[1] = '\0';
        break;

    case '=':
        if (cc_peek_char(cc) == '=') {
            cc_next_char(cc); tok.type = CC_TOK_EQEQ;
            tok.text[0] = '='; tok.text[1] = '='; tok.text[2] = '\0';
        } else {
            tok.type = CC_TOK_EQ;
            tok.text[0] = '='; tok.text[1] = '\0';
        }
        break;

    case '!':
        if (cc_peek_char(cc) == '=') {
            cc_next_char(cc); tok.type = CC_TOK_NE;
            tok.text[0] = '!'; tok.text[1] = '='; tok.text[2] = '\0';
        } else {
            tok.type = CC_TOK_NOT;
            tok.text[0] = '!'; tok.text[1] = '\0';
        }
        break;

    case '<':
        if (cc_peek_char(cc) == '=') {
            cc_next_char(cc); tok.type = CC_TOK_LE;
            tok.text[0] = '<'; tok.text[1] = '='; tok.text[2] = '\0';
        } else if (cc_peek_char(cc) == '<') {
            cc_next_char(cc); tok.type = CC_TOK_SHL;
            tok.text[0] = '<'; tok.text[1] = '<'; tok.text[2] = '\0';
        } else {
            tok.type = CC_TOK_LT;
            tok.text[0] = '<'; tok.text[1] = '\0';
        }
        break;

    case '>':
        if (cc_peek_char(cc) == '=') {
            cc_next_char(cc); tok.type = CC_TOK_GE;
            tok.text[0] = '>'; tok.text[1] = '='; tok.text[2] = '\0';
        } else if (cc_peek_char(cc) == '>') {
            cc_next_char(cc); tok.type = CC_TOK_SHR;
            tok.text[0] = '>'; tok.text[1] = '>'; tok.text[2] = '\0';
        } else {
            tok.type = CC_TOK_GT;
            tok.text[0] = '>'; tok.text[1] = '\0';
        }
        break;

    case '&':
        if (cc_peek_char(cc) == '&') {
            cc_next_char(cc); tok.type = CC_TOK_AND;
            tok.text[0] = '&'; tok.text[1] = '&'; tok.text[2] = '\0';
        } else {
            tok.type = CC_TOK_AMP;
            tok.text[0] = '&'; tok.text[1] = '\0';
        }
        break;

    case '|':
        if (cc_peek_char(cc) == '|') {
            cc_next_char(cc); tok.type = CC_TOK_OR;
            tok.text[0] = '|'; tok.text[1] = '|'; tok.text[2] = '\0';
        } else {
            tok.type = CC_TOK_BOR;
            tok.text[0] = '|'; tok.text[1] = '\0';
        }
        break;

    case '^':
        tok.type = CC_TOK_BXOR;
        tok.text[0] = '^'; tok.text[1] = '\0';
        break;

    case '~':
        tok.type = CC_TOK_BNOT;
        tok.text[0] = '~'; tok.text[1] = '\0';
        break;

    case '(': tok.type = CC_TOK_LPAREN; tok.text[0] = '('; tok.text[1] = '\0'; break;
    case ')': tok.type = CC_TOK_RPAREN; tok.text[0] = ')'; tok.text[1] = '\0'; break;
    case '{': tok.type = CC_TOK_LBRACE; tok.text[0] = '{'; tok.text[1] = '\0'; break;
    case '}': tok.type = CC_TOK_RBRACE; tok.text[0] = '}'; tok.text[1] = '\0'; break;
    case '[': tok.type = CC_TOK_LBRACK; tok.text[0] = '['; tok.text[1] = '\0'; break;
    case ']': tok.type = CC_TOK_RBRACK; tok.text[0] = ']'; tok.text[1] = '\0'; break;
    case ';': tok.type = CC_TOK_SEMICOLON; tok.text[0] = ';'; tok.text[1] = '\0'; break;
    case ',': tok.type = CC_TOK_COMMA; tok.text[0] = ','; tok.text[1] = '\0'; break;

    default:
        tok.type = CC_TOK_ERROR;
        tok.text[0] = c;
        tok.text[1] = '\0';
        break;
    }

    cc->cur = tok;
    return tok;
}

/* ── Peek at next token without consuming ────────────────────────── */

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
