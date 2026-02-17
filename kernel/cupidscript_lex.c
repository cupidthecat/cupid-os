/*
 * cupidscript_lex.c - Lexer/tokenizer for CupidScript
 *
 * Breaks script source text into a flat array of tokens.
 */
#include "cupidscript.h"
#include "string.h"
#include "../drivers/serial.h"

static int is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

static int is_word_char(char c) {
    /* Characters that can appear in an unquoted word/argument */
    return is_alnum(c) || c == '/' || c == '.' || c == '-' ||
           c == '_' || c == '*' || c == '?' || c == '~' ||
           c == '+' || c == '%' || c == ':' || c == ',';
}

/* Check if a word is a keyword and return its token type.
 * Returns TOK_WORD if not a keyword. */
static token_type_t keyword_type(const char *word) {
    if (strcmp(word, "if") == 0)     return TOK_IF;
    if (strcmp(word, "then") == 0)   return TOK_THEN;
    if (strcmp(word, "else") == 0)   return TOK_ELSE;
    if (strcmp(word, "elif") == 0)   return TOK_ELIF;
    if (strcmp(word, "fi") == 0)     return TOK_FI;
    if (strcmp(word, "while") == 0)  return TOK_WHILE;
    if (strcmp(word, "do") == 0)     return TOK_DO;
    if (strcmp(word, "done") == 0)   return TOK_DONE;
    if (strcmp(word, "for") == 0)    return TOK_FOR;
    if (strcmp(word, "in") == 0)     return TOK_IN;
    if (strcmp(word, "return") == 0) return TOK_RETURN;
    return TOK_WORD;
}

/* Copy at most (max-1) chars from src to dst and null-terminate */
static void safe_copy(char *dst, const char *src, int len, int max) {
    int n = len < (max - 1) ? len : (max - 1);
    for (int i = 0; i < n; i++) {
        dst[i] = src[i];
    }
    dst[n] = '\0';
}

/* cupidscript_tokenize
 * Tokenizes `source` (length bytes) into the `tokens` array.
 * Returns the number of tokens produced.
 */
int cupidscript_tokenize(const char *source, uint32_t length,
                         token_t *tokens, int max_tokens)
{
    int count = 0;
    uint32_t pos = 0;
    int line = 1;

    while (pos < length && count < max_tokens - 1) {
        char c = source[pos];

        /* skip spaces & tabs */
        if (c == ' ' || c == '\t') {
            pos++;
            continue;
        }

        /* newlines */
        if (c == '\n') {
            tokens[count].type = TOK_NEWLINE;
            tokens[count].value[0] = '\n';
            tokens[count].value[1] = '\0';
            tokens[count].line = line;
            count++;
            line++;
            pos++;
            continue;
        }

        /* carriage return (skip, handle \r\n) */
        if (c == '\r') {
            pos++;
            continue;
        }

        /* shebang #!/... (skip entire line) */
        if (c == '#' && pos + 1 < length && source[pos + 1] == '!') {
            tokens[count].type = TOK_HASH_BANG;
            int start = (int)pos;
            while (pos < length && source[pos] != '\n') pos++;
            int len = (int)pos - start;
            safe_copy(tokens[count].value, source + start, len,
                      MAX_TOKEN_LEN);
            tokens[count].line = line;
            count++;
            continue;
        }

        /* comments */
        if (c == '#') {
            /* skip rest of line */
            while (pos < length && source[pos] != '\n') pos++;
            continue;
        }

        /* semicolons */
        if (c == ';') {
            tokens[count].type = TOK_SEMICOLON;
            tokens[count].value[0] = ';';
            tokens[count].value[1] = '\0';
            tokens[count].line = line;
            count++;
            pos++;
            continue;
        }

        /* pipe | */
        if (c == '|') {
            tokens[count].type = TOK_PIPE;
            tokens[count].value[0] = '|';
            tokens[count].value[1] = '\0';
            tokens[count].line = line;
            count++;
            pos++;
            continue;
        }

        /* redirection > >> */
        if (c == '>') {
            if (pos + 1 < length && source[pos + 1] == '>') {
                tokens[count].type = TOK_REDIR_APPEND;
                tokens[count].value[0] = '>';
                tokens[count].value[1] = '>';
                tokens[count].value[2] = '\0';
                tokens[count].line = line;
                count++;
                pos += 2;
            } else {
                tokens[count].type = TOK_REDIR_OUT;
                tokens[count].value[0] = '>';
                tokens[count].value[1] = '\0';
                tokens[count].line = line;
                count++;
                pos++;
            }
            continue;
        }

        /* redirection < */
        if (c == '<') {
            tokens[count].type = TOK_REDIR_IN;
            tokens[count].value[0] = '<';
            tokens[count].value[1] = '\0';
            tokens[count].line = line;
            count++;
            pos++;
            continue;
        }

        /* background & */
        if (c == '&') {
            tokens[count].type = TOK_BACKGROUND;
            tokens[count].value[0] = '&';
            tokens[count].value[1] = '\0';
            tokens[count].line = line;
            count++;
            pos++;
            continue;
        }

        /* backtick ` (command substitution) */
        if (c == '`') {
            pos++;
            int start_bt = (int)pos;
            while (pos < length && source[pos] != '`') pos++;
            int len_bt = (int)pos - start_bt;
            tokens[count].type = TOK_BACKTICK;
            safe_copy(tokens[count].value, source + start_bt, len_bt,
                      MAX_TOKEN_LEN);
            tokens[count].line = line;
            count++;
            if (pos < length) pos++; /* skip closing ` */
            continue;
        }

        /* brackets [ ] */
        if (c == '[') {
            tokens[count].type = TOK_LBRACKET;
            tokens[count].value[0] = '[';
            tokens[count].value[1] = '\0';
            tokens[count].line = line;
            count++;
            pos++;
            continue;
        }
        if (c == ']') {
            tokens[count].type = TOK_RBRACKET;
            tokens[count].value[0] = ']';
            tokens[count].value[1] = '\0';
            tokens[count].line = line;
            count++;
            pos++;
            continue;
        }

        /* braces { } */
        if (c == '{') {
            tokens[count].type = TOK_LBRACE;
            tokens[count].value[0] = '{';
            tokens[count].value[1] = '\0';
            tokens[count].line = line;
            count++;
            pos++;
            continue;
        }
        if (c == '}') {
            tokens[count].type = TOK_RBRACE;
            tokens[count].value[0] = '}';
            tokens[count].value[1] = '\0';
            tokens[count].line = line;
            count++;
            pos++;
            continue;
        }

        /* parentheses ( ) */
        if (c == '(') {
            tokens[count].type = TOK_LPAREN;
            tokens[count].value[0] = '(';
            tokens[count].value[1] = '\0';
            tokens[count].line = line;
            count++;
            pos++;
            continue;
        }
        if (c == ')') {
            tokens[count].type = TOK_RPAREN;
            tokens[count].value[0] = ')';
            tokens[count].value[1] = '\0';
            tokens[count].line = line;
            count++;
            pos++;
            continue;
        }

        /* $((expr)) arithmetic expansion */
        if (c == '$' && pos + 1 < length && source[pos + 1] == '(' &&
            pos + 2 < length && source[pos + 2] == '(') {
            pos += 3; /* skip $(( */
            int start = (int)pos;
            int depth = 1;
            while (pos < length && depth > 0) {
                if (source[pos] == '(' && pos + 1 < length &&
                    source[pos + 1] == '(') {
                    depth++;
                    pos++;
                } else if (source[pos] == ')' && pos + 1 < length &&
                           source[pos + 1] == ')') {
                    depth--;
                    if (depth == 0) break;
                    pos++;
                }
                pos++;
            }
            int len = (int)pos - start;
            tokens[count].type = TOK_ARITH;
            safe_copy(tokens[count].value, source + start, len,
                      MAX_TOKEN_LEN);
            tokens[count].line = line;
            count++;
            /* skip closing )) */
            if (pos < length && source[pos] == ')') pos++;
            if (pos < length && source[pos] == ')') pos++;
            continue;
        }

        /* $() command substitution */
        if (c == '$' && pos + 1 < length && source[pos + 1] == '(' &&
            !(pos + 2 < length && source[pos + 2] == '(')) {
            pos += 2; /* skip $( */
            int start = (int)pos;
            int depth = 1;
            while (pos < length && depth > 0) {
                if (source[pos] == '(') depth++;
                else if (source[pos] == ')') {
                    depth--;
                    if (depth == 0) break;
                }
                pos++;
            }
            int len = (int)pos - start;
            tokens[count].type = TOK_CMD_SUBST_START;
            safe_copy(tokens[count].value, source + start, len,
                      MAX_TOKEN_LEN);
            tokens[count].line = line;
            count++;
            if (pos < length) pos++; /* skip closing ) */
            continue;
        }

        /* variables $VAR, $?, $#, $0-$9 */
        if (c == '$') {
            pos++;
            if (pos < length) {
                char next = source[pos];
                if (next == '?' || next == '#') {
                    tokens[count].type = TOK_VARIABLE;
                    tokens[count].value[0] = next;
                    tokens[count].value[1] = '\0';
                    tokens[count].line = line;
                    count++;
                    pos++;
                } else if (is_digit(next)) {
                    /* $0, $1, ... $9 */
                    tokens[count].type = TOK_VARIABLE;
                    tokens[count].value[0] = next;
                    tokens[count].value[1] = '\0';
                    tokens[count].line = line;
                    count++;
                    pos++;
                } else if (next == '{') {
                    /* ${...} - advanced variable expansion.
                     * Store the whole ${...} content (without braces)
                     * as a TOK_VARIABLE token, and let copy_token_to_argv
                     * reconstruct $varname.  But for ${...} forms we
                     * need to keep the braces so expand() sees ${...}.
                     * Store the inner content in value, but prefix with
                     * a sentinel '{' so we can reconstruct ${...}. */
                    pos++; /* skip { */
                    int start = (int)pos;
                    int depth = 1;
                    while (pos < length && depth > 0) {
                        if (source[pos] == '{') depth++;
                        else if (source[pos] == '}') {
                            depth--;
                            if (depth == 0) break;
                        }
                        pos++;
                    }
                    int elen = (int)pos - start;
                    if (pos < length) pos++; /* skip } */
                    /* Store as TOK_WORD with value = ${...} already
                     * reconstructed, so the parser copies it as-is and
                     * expand() can process it. */
                    tokens[count].type = TOK_WORD;
                    /* Build "${...}" into token value */
                    {
                        int ti = 0;
                        if (ti < MAX_TOKEN_LEN - 1)
                            tokens[count].value[ti++] = '$';
                        if (ti < MAX_TOKEN_LEN - 1)
                            tokens[count].value[ti++] = '{';
                        for (int k = 0;
                             k < elen && ti < MAX_TOKEN_LEN - 1; k++) {
                            tokens[count].value[ti++] = source[start + k];
                        }
                        if (ti < MAX_TOKEN_LEN - 1)
                            tokens[count].value[ti++] = '}';
                        tokens[count].value[ti] = '\0';
                    }
                    tokens[count].line = line;
                    count++;
                } else if (next == '!') {
                    /* $! - last background PID */
                    tokens[count].type = TOK_VARIABLE;
                    tokens[count].value[0] = '!';
                    tokens[count].value[1] = '\0';
                    tokens[count].line = line;
                    count++;
                    pos++;
                } else if (is_alpha(next) || next == '_') {
                    int start = (int)pos;
                    while (pos < length &&
                           (is_alnum(source[pos]) || source[pos] == '_'))
                        pos++;
                    int len = (int)pos - start;
                    tokens[count].type = TOK_VARIABLE;
                    safe_copy(tokens[count].value, source + start, len,
                              MAX_TOKEN_LEN);
                    tokens[count].line = line;
                    count++;
                } else {
                    /* bare $ - treat as word */
                    tokens[count].type = TOK_WORD;
                    tokens[count].value[0] = '$';
                    tokens[count].value[1] = '\0';
                    tokens[count].line = line;
                    count++;
                }
            }
            continue;
        }

        /* double-quoted strings */
        if (c == '"') {
            pos++;
            int start = (int)pos;
            while (pos < length && source[pos] != '"') {
                if (source[pos] == '\\' && pos + 1 < length) pos++;
                pos++;
            }
            int len = (int)pos - start;
            tokens[count].type = TOK_STRING;
            safe_copy(tokens[count].value, source + start, len,
                      MAX_TOKEN_LEN);
            tokens[count].line = line;
            count++;
            if (pos < length) pos++; /* skip closing " */
            continue;
        }

        /* single-quoted strings (no expansion) */
        if (c == '\'') {
            pos++;
            int start = (int)pos;
            while (pos < length && source[pos] != '\'') pos++;
            int len = (int)pos - start;
            tokens[count].type = TOK_STRING;
            safe_copy(tokens[count].value, source + start, len,
                      MAX_TOKEN_LEN);
            tokens[count].line = line;
            count++;
            if (pos < length) pos++; /* skip closing ' */
            continue;
        }

        /* 2> and 2>&1 redirections */
        if (c == '2' && pos + 1 < length && source[pos + 1] == '>') {
            if (pos + 3 < length && source[pos + 2] == '&' &&
                source[pos + 3] == '1') {
                tokens[count].type = TOK_REDIR_ERR_OUT;
                safe_copy(tokens[count].value, "2>&1", 4, MAX_TOKEN_LEN);
                tokens[count].line = line;
                count++;
                pos += 4;
            } else {
                tokens[count].type = TOK_REDIR_ERR;
                safe_copy(tokens[count].value, "2>", 2, MAX_TOKEN_LEN);
                tokens[count].line = line;
                count++;
                pos += 2;
            }
            continue;
        }

        /* words / keywords / assignments */
        if (is_word_char(c) || c == '!' || c == '=') {
            int start = (int)pos;

            /* Scan the full word, including embedded = and ! */
            while (pos < length && (is_word_char(source[pos]) ||
                                    source[pos] == '=' ||
                                    source[pos] == '!')) {
                pos++;
            }
            int len = (int)pos - start;

            /* Safety: if we somehow didn't advance, skip the character
             * to prevent an infinite loop */
            if (len == 0) {
                pos++;
                continue;
            }

            char word[MAX_TOKEN_LEN];
            safe_copy(word, source + start, len, MAX_TOKEN_LEN);

            /* Check for assignment: NAME=VALUE pattern */
            int eq_pos = -1;
            for (int i = 0; i < len; i++) {
                if (word[i] == '=') {
                    /* Only if left side is a valid variable name */
                    int valid = 1;
                    if (i == 0 || !is_alpha(word[0])) valid = 0;
                    for (int j = 1; j < i && valid; j++) {
                        if (!is_alnum(word[j]) && word[j] != '_')
                            valid = 0;
                    }
                    if (valid) {
                        eq_pos = i;
                        break;
                    }
                }
            }

            if (eq_pos > 0) {
                /* Assignment: emit name token, then = token, then value */
                /* Name */
                tokens[count].type = TOK_WORD;
                safe_copy(tokens[count].value, word, eq_pos, MAX_TOKEN_LEN);
                tokens[count].line = line;
                count++;

                /* = */
                if (count < max_tokens - 1) {
                    tokens[count].type = TOK_ASSIGN;
                    tokens[count].value[0] = '=';
                    tokens[count].value[1] = '\0';
                    tokens[count].line = line;
                    count++;
                }

                /* Value (may be empty) */
                if (count < max_tokens - 1 && eq_pos + 1 < len) {
                    tokens[count].type = TOK_WORD;
                    safe_copy(tokens[count].value, word + eq_pos + 1,
                              len - eq_pos - 1, MAX_TOKEN_LEN);
                    tokens[count].line = line;
                    count++;
                } else if (count < max_tokens - 1) {
                    tokens[count].type = TOK_WORD;
                    tokens[count].value[0] = '\0';
                    tokens[count].line = line;
                    count++;
                }
            } else {
                /* Regular word or keyword */
                token_type_t tt = keyword_type(word);
                tokens[count].type = tt;
                safe_copy(tokens[count].value, word, len, MAX_TOKEN_LEN);
                tokens[count].line = line;
                count++;
            }
            continue;
        }

        /* unknown character - skip */
        pos++;
    }

    /* EOF token */
    tokens[count].type = TOK_EOF;
    tokens[count].value[0] = '\0';
    tokens[count].line = line;
    count++;

    KDEBUG("CupidScript lexer: %d tokens from %u bytes", count, length);
    return count;
}
