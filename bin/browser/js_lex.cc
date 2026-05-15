/* §7 JavaScript lexer. Reads `src[0..len)` and appends tokens into
 * jtk_*[]. Keywords are looked up by string match against an interned
 * identifier; everything else is a punctuator or operator. Numbers
 * keep only the decimal integer portion in jtk_num for now (F1a) -
 * fractional parts are recognised but ignored; F1b switches the
 * runtime to doubles.*/

int js_is_digit(int c)  { return c >= '0' && c <= '9'; }
int js_is_alpha(int c)  { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; }
int js_is_alnum(int c)  { return js_is_alpha(c) || js_is_digit(c); }

int js_str_intern(char *src, int n) {
    /* Look for an existing entry first - cheap dedup of identifiers. */
    int i = 0;
    while (i < js_str_pool_pos) {
        int k = 0;
        while (k < n && js_str_pool[i + k] == src[k]) k = k + 1;
        if (k == n && js_str_pool[i + n] == 0) return i;
        while (i < js_str_pool_pos && js_str_pool[i] != 0) i = i + 1;
        if (i < js_str_pool_pos) i = i + 1;
    }
    if (js_str_pool_pos + n + 1 >= JS_STR_POOL) return -1;
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

void js_emit_tok(int kind, int num, int str_off, int str_len, int line) {
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
        /* number */
        if (js_is_digit(c)) {
            int v = 0;
            int s = i;
            while (i < n && js_is_digit(src[i])) {
                v = v * 10 + (src[i] - '0');
                i = i + 1;
            }
            /* fractional - parsed and dropped for now */
            if (i < n && src[i] == '.') {
                i = i + 1;
                while (i < n && js_is_digit(src[i])) i = i + 1;
            }
            (void)s;
            js_emit_tok(JS_TOK_NUMBER, v, -1, 0, line);
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
        /* unknown character - skip silently */
        i = i + 1;
    }
    js_emit_tok(JS_TOK_EOF, 0, -1, 0, line);
    return 0;
}
