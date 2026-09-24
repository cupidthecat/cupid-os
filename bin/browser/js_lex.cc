/* §7 JavaScript lexer. Reads `src[0..len)` and appends tokens into
 * jtk_*[]. Keywords are looked up by string match against an interned
 * identifier; everything else is a punctuator or operator. Decimal forms
 * and prefixed hexadecimal, binary, and octal integers keep their double
 * value in jtk_num. Numeric separators are checked by the shared scanner.*/

int js_is_digit(int c)  { return c >= '0' && c <= '9'; }
int js_is_alpha(int c)  { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; }
int js_is_alnum(int c)  { return js_is_alpha(c) || js_is_digit(c); }

int js_digit_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

double js_lex_number_value;
int js_lex_number_end;

int js_scan_number(char *src, int n, int start) {
    int i = start;
    double v = 0.0;

    if (src[start] == '0' && start + 1 < n &&
        (src[start + 1] == 'x' || src[start + 1] == 'X' ||
         src[start + 1] == 'b' || src[start + 1] == 'B' ||
         src[start + 1] == 'o' || src[start + 1] == 'O')) {
        int prefix = src[start + 1];
        int base = 16;
        if (prefix == 'b' || prefix == 'B') base = 2;
        else if (prefix == 'o' || prefix == 'O') base = 8;
        i = start + 2;
        int digits = 0;
        int last_was_digit = 0;
        while (i < n) {
            int c = (unsigned char)src[i];
            int digit = js_digit_value(c);
            if (digit >= 0 && digit < base) {
                v = v * (double)base + (double)digit;
                digits = digits + 1;
                last_was_digit = 1;
                i = i + 1;
                continue;
            }
            if (c == '_') {
                int next_digit = i + 1 < n ?
                    js_digit_value((unsigned char)src[i + 1]) : -1;
                if (!last_was_digit || next_digit < 0 || next_digit >= base) {
                    js_set_err("js: invalid numeric separator");
                    return -1;
                }
                last_was_digit = 0;
                i = i + 1;
                continue;
            }
            if (digit >= base) {
                if (base == 2) js_set_err("js: invalid binary digit");
                else if (base == 8) js_set_err("js: invalid octal digit");
                else js_set_err("js: invalid hexadecimal digit");
                return -1;
            }
            break;
        }
        if (digits == 0) {
            if (base == 2) js_set_err("js: expected binary digits");
            else if (base == 8) js_set_err("js: expected octal digits");
            else js_set_err("js: expected hexadecimal digits");
            return -1;
        }
        if (i < n && js_is_alpha((unsigned char)src[i])) {
            js_set_err("js: identifier follows numeric literal");
            return -1;
        }
        js_lex_number_value = v;
        js_lex_number_end = i;
        return 0;
    }

    int integer_digits = 0;
    int last_was_digit = 0;
    while (i < n && src[i] != '.') {
        int c = (unsigned char)src[i];
        if (js_is_digit(c)) {
            v = v * 10.0 + (double)(c - '0');
            integer_digits = integer_digits + 1;
            last_was_digit = 1;
            i = i + 1;
            continue;
        }
        if (c == '_') {
            int next_is_digit = i + 1 < n &&
                                js_is_digit((unsigned char)src[i + 1]);
            if (!last_was_digit || !next_is_digit || src[start] == '0') {
                js_set_err("js: invalid numeric separator");
                return -1;
            }
            last_was_digit = 0;
            i = i + 1;
            continue;
        }
        break;
    }

    int fraction_digits = 0;
    if (i < n && src[i] == '.') {
        i = i + 1;
        last_was_digit = 0;
        double place = 0.1;
        while (i < n) {
            int c = (unsigned char)src[i];
            if (js_is_digit(c)) {
                v = v + (double)(c - '0') * place;
                place = place * 0.1;
                fraction_digits = fraction_digits + 1;
                last_was_digit = 1;
                i = i + 1;
                continue;
            }
            if (c == '_') {
                int next_is_digit = i + 1 < n &&
                                    js_is_digit((unsigned char)src[i + 1]);
                if (!last_was_digit || !next_is_digit) {
                    js_set_err("js: invalid numeric separator");
                    return -1;
                }
                last_was_digit = 0;
                i = i + 1;
                continue;
            }
            break;
        }
    }

    if (integer_digits == 0 && fraction_digits == 0) {
        js_set_err("js: expected decimal digits");
        return -1;
    }

    if (i < n && (src[i] == 'e' || src[i] == 'E')) {
        i = i + 1;
        int exponent_negative = 0;
        if (i < n && (src[i] == '+' || src[i] == '-')) {
            exponent_negative = src[i] == '-';
            i = i + 1;
        }
        int exponent = 0;
        int exponent_digits = 0;
        last_was_digit = 0;
        while (i < n) {
            int c = (unsigned char)src[i];
            if (js_is_digit(c)) {
                if (exponent < 400) {
                    exponent = exponent * 10 + (c - '0');
                    if (exponent > 400) exponent = 400;
                }
                exponent_digits = exponent_digits + 1;
                last_was_digit = 1;
                i = i + 1;
                continue;
            }
            if (c == '_') {
                int next_is_digit = i + 1 < n &&
                                    js_is_digit((unsigned char)src[i + 1]);
                if (!last_was_digit || !next_is_digit) {
                    js_set_err("js: invalid numeric separator");
                    return -1;
                }
                last_was_digit = 0;
                i = i + 1;
                continue;
            }
            break;
        }
        if (exponent_digits == 0) {
            js_set_err("js: expected exponent digits");
            return -1;
        }
        while (exponent > 0) {
            v = exponent_negative ? v / 10.0 : v * 10.0;
            exponent = exponent - 1;
        }
    }

    if (i < n && js_is_alpha((unsigned char)src[i])) {
        js_set_err("js: identifier follows numeric literal");
        return -1;
    }
    js_lex_number_value = v;
    js_lex_number_end = i;
    return 0;
}

int js_str_intern(char *src, int n) {
    /* Look for an existing entry first - cheap dedup of identifiers. */
    int i = 0;
    while (i < js_str_pool_pos) {
        int k = 0;
        while (k < n && i + k < js_str_pool_pos &&
               js_str_pool[i + k] == src[k]) {
            k = k + 1;
        }
        if (k == n && i + n < js_str_pool_pos &&
            js_str_pool[i + n] == 0) {
            return i;
        }
        while (i < js_str_pool_pos && js_str_pool[i] != 0) i = i + 1;
        if (i < js_str_pool_pos) i = i + 1;
    }
    if (n < 0 || n > JS_STR_POOL - js_str_pool_pos - 1) {
        js_set_err("js: string pool full");
        return -1;
    }
    int off = js_str_pool_pos;
    int j = 0;
    while (j < n) { js_str_pool[off + j] = src[j]; j = j + 1; }
    js_str_pool[off + n] = 0;
    js_str_pool_pos = off + n + 1;
    return off;
}

int js_keyword(char *s, int n) {
    if (n == 2 && b_strieq_n(s, "if", 2)) return JS_TOK_KW_IF;
    if (n == 3 && b_strieq_n(s, "var", 3)) return JS_TOK_KW_VAR;
    if (n == 3 && b_strieq_n(s, "let", 3)) return JS_TOK_KW_LET;
    if (n == 3 && b_strieq_n(s, "for", 3)) return JS_TOK_KW_FOR;
    if (n == 3 && b_strieq_n(s, "new", 3)) return JS_TOK_KW_NEW;
    if (n == 4 && b_strieq_n(s, "else", 4)) return JS_TOK_KW_ELSE;
    if (n == 4 && b_strieq_n(s, "true", 4)) return JS_TOK_KW_TRUE;
    if (n == 4 && b_strieq_n(s, "null", 4)) return JS_TOK_KW_NULL;
    if (n == 5 && b_strieq_n(s, "while", 5)) return JS_TOK_KW_WHILE;
    if (n == 5 && b_strieq_n(s, "false", 5)) return JS_TOK_KW_FALSE;
    if (n == 5 && b_strieq_n(s, "break", 5)) return JS_TOK_KW_BREAK;
    if (n == 5 && b_strieq_n(s, "const", 5)) return JS_TOK_KW_CONST;
    if (n == 6 && b_strieq_n(s, "return", 6)) return JS_TOK_KW_RETURN;
    if (n == 6 && b_strieq_n(s, "typeof", 6)) return JS_TOK_KW_TYPEOF;
    if (n == 8 && b_strieq_n(s, "function", 8)) return JS_TOK_KW_FUNCTION;
    if (n == 8 && b_strieq_n(s, "continue", 8)) return JS_TOK_KW_CONTINUE;
    if (n == 9 && b_strieq_n(s, "undefined", 9)) return JS_TOK_KW_UNDEFINED;
    return 0;
}

void js_emit_tok(int kind, double num, int str_off, int str_len, int line) {
    if (jtk_count >= MAX_JS_TOKENS) return;
    int t = jtk_count;
    jtk_kind   [t] = kind;
    jtk_num    [t] = num;
    jtk_str_off[t] = str_off;
    jtk_str_len[t] = str_len;
    jtk_line   [t] = line;
    jtk_count = t + 1;
}

int js_tokenize(char *src, int n) {
    int i = 0;
    int line = 1;
    while (i < n) {
        int c = (unsigned char)src[i];
        /* skip whitespace */
        if (c == '\n') { line = line + 1; i = i + 1; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i = i + 1; continue; }
        /* line comment // ... */
        if (c == '/' && i + 1 < n && src[i+1] == '/') {
            while (i < n && src[i] != '\n') i = i + 1;
            continue;
        }
        /* block comment /* ... */
        if (c == '/' && i + 1 < n && src[i+1] == '*') {
            i = i + 2;
            while (i + 1 < n && !(src[i] == '*' && src[i+1] == '/')) {
                if (src[i] == '\n') line = line + 1;
                i = i + 1;
            }
            if (i + 1 < n) i = i + 2;
            continue;
        }
        /* Decimal, hexadecimal, binary, and octal number literals. */
        if (js_is_digit(c) ||
            (c == '.' && i + 1 < n && js_is_digit(src[i + 1]))) {
            if (js_scan_number(src, n, i) != 0) return -1;
            i = js_lex_number_end;
            js_emit_tok(JS_TOK_NUMBER, js_lex_number_value, -1, 0, line);
            continue;
        }
        /* identifier or keyword */
        if (js_is_alpha(c)) {
            int s = i;
            while (i < n && js_is_alnum(src[i])) i = i + 1;
            int len = i - s;
            int kw = js_keyword(src + s, len);
            if (kw) { js_emit_tok(kw, 0, -1, 0, line); continue; }
            int off = js_str_intern(src + s, len);
            if (off < 0) return -1;
            js_emit_tok(JS_TOK_IDENT, 0, off, len, line);
            continue;
        }
        /* string literal: '...' or "..." (no escapes beyond \\, \n, \t, \", \') */
        if (c == '"' || c == '\'') {
            int q = c;
            i = i + 1;
            int s = i;
            char buf[1024];
            int b = 0;
            while (i < n && src[i] != q && b < 1023) {
                if (src[i] == '\\' && i + 1 < n) {
                    int e = src[i+1];
                    if (e == 'n') buf[b] = '\n';
                    else if (e == 't') buf[b] = '\t';
                    else if (e == 'r') buf[b] = '\r';
                    else if (e == '\\') buf[b] = '\\';
                    else if (e == '\'') buf[b] = '\'';
                    else if (e == '"') buf[b] = '"';
                    else if (e == '0') buf[b] = 0;
                    else buf[b] = (char)e;
                    b = b + 1;
                    i = i + 2;
                    continue;
                }
                buf[b] = src[i];
                b = b + 1;
                i = i + 1;
            }
            if (i < n) i = i + 1;       /* skip closing quote */
            int off = js_str_intern(buf, b);
            if (off < 0) return -1;
            (void)s;
            js_emit_tok(JS_TOK_STRING, 0, off, b, line);
            continue;
        }
        /* punctuators and operators - longest match first */
        int peek = (i + 1 < n) ? (unsigned char)src[i+1] : 0;
        int peek2 = (i + 2 < n) ? (unsigned char)src[i+2] : 0;
        if (c == '=' && peek == '=' && peek2 == '=') {
            js_emit_tok(JS_TOK_EQ_EQ, 0, -1, 0, line); i = i + 3; continue;
        }
        if (c == '!' && peek == '=' && peek2 == '=') {
            js_emit_tok(JS_TOK_NEQ_EQ, 0, -1, 0, line); i = i + 3; continue;
        }
        if (c == '=' && peek == '=') { js_emit_tok(JS_TOK_EQ,  0, -1, 0, line); i = i + 2; continue; }
        if (c == '!' && peek == '=') { js_emit_tok(JS_TOK_NEQ, 0, -1, 0, line); i = i + 2; continue; }
        if (c == '<' && peek == '=') { js_emit_tok(JS_TOK_LE,  0, -1, 0, line); i = i + 2; continue; }
        if (c == '>' && peek == '=') { js_emit_tok(JS_TOK_GE,  0, -1, 0, line); i = i + 2; continue; }
        if (c == '&' && peek == '&') { js_emit_tok(JS_TOK_AND_AND, 0, -1, 0, line); i = i + 2; continue; }
        if (c == '|' && peek == '|') { js_emit_tok(JS_TOK_OR_OR,   0, -1, 0, line); i = i + 2; continue; }
        if (c == '+' && peek == '+') { js_emit_tok(JS_TOK_PLUS_PLUS, 0, -1, 0, line); i = i + 2; continue; }
        if (c == '-' && peek == '-') { js_emit_tok(JS_TOK_MINUS_MINUS, 0, -1, 0, line); i = i + 2; continue; }
        if (c == '+' && peek == '=') { js_emit_tok(JS_TOK_PLUS_EQ,  0, -1, 0, line); i = i + 2; continue; }
        if (c == '-' && peek == '=') { js_emit_tok(JS_TOK_MINUS_EQ, 0, -1, 0, line); i = i + 2; continue; }
        if (c == '*' && peek == '=') { js_emit_tok(JS_TOK_STAR_EQ,  0, -1, 0, line); i = i + 2; continue; }
        if (c == '/' && peek == '=') { js_emit_tok(JS_TOK_SLASH_EQ, 0, -1, 0, line); i = i + 2; continue; }
        if (c == '%' && peek == '=') { js_emit_tok(JS_TOK_PERCENT_EQ, 0, -1, 0, line); i = i + 2; continue; }
        if (c == '{') { js_emit_tok(JS_TOK_LBRACE,  0, -1, 0, line); i = i + 1; continue; }
        if (c == '}') { js_emit_tok(JS_TOK_RBRACE,  0, -1, 0, line); i = i + 1; continue; }
        if (c == '(') { js_emit_tok(JS_TOK_LPAREN,  0, -1, 0, line); i = i + 1; continue; }
        if (c == ')') { js_emit_tok(JS_TOK_RPAREN,  0, -1, 0, line); i = i + 1; continue; }
        if (c == '[') { js_emit_tok(JS_TOK_LBRACK,  0, -1, 0, line); i = i + 1; continue; }
        if (c == ']') { js_emit_tok(JS_TOK_RBRACK,  0, -1, 0, line); i = i + 1; continue; }
        if (c == ';') { js_emit_tok(JS_TOK_SEMI,    0, -1, 0, line); i = i + 1; continue; }
        if (c == ',') { js_emit_tok(JS_TOK_COMMA,   0, -1, 0, line); i = i + 1; continue; }
        if (c == '.') { js_emit_tok(JS_TOK_DOT,     0, -1, 0, line); i = i + 1; continue; }
        if (c == ':') { js_emit_tok(JS_TOK_COLON,   0, -1, 0, line); i = i + 1; continue; }
        if (c == '?') { js_emit_tok(JS_TOK_QUESTION,0, -1, 0, line); i = i + 1; continue; }
        if (c == '=') { js_emit_tok(JS_TOK_ASSIGN,  0, -1, 0, line); i = i + 1; continue; }
        if (c == '+') { js_emit_tok(JS_TOK_PLUS,    0, -1, 0, line); i = i + 1; continue; }
        if (c == '-') { js_emit_tok(JS_TOK_MINUS,   0, -1, 0, line); i = i + 1; continue; }
        if (c == '*') { js_emit_tok(JS_TOK_STAR,    0, -1, 0, line); i = i + 1; continue; }
        if (c == '/') { js_emit_tok(JS_TOK_SLASH,   0, -1, 0, line); i = i + 1; continue; }
        if (c == '%') { js_emit_tok(JS_TOK_PERCENT, 0, -1, 0, line); i = i + 1; continue; }
        if (c == '<') { js_emit_tok(JS_TOK_LT,      0, -1, 0, line); i = i + 1; continue; }
        if (c == '>') { js_emit_tok(JS_TOK_GT,      0, -1, 0, line); i = i + 1; continue; }
        if (c == '!') { js_emit_tok(JS_TOK_NOT,     0, -1, 0, line); i = i + 1; continue; }
        /* Unknown character: keep the Browser's tolerant skip behavior. */
        i = i + 1;
    }
    js_emit_tok(JS_TOK_EOF, 0, -1, 0, line);
    return 0;
}
