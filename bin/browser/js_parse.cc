/* §7 JavaScript parser. Recursive-descent over the token stream
 * produced by js_lex.cc. AST nodes use the parallel jn_*[] arrays;
 * per-kind a/b/c/d slots are documented below.
 *
 * Per-kind slot layout (a/b/c/d, jn_next is sibling link in lists):
 *   NUM:           a = integer value (F1b promotes to double)
 *   STR / IDENT:   a = js_str_pool offset, b = length
 *   BOOL:          a = 0 or 1
 *   BIN:           a = op token kind, b = left, c = right
 *   UNARY:         a = op token kind, b = operand
 *   ASSIGN:        a = op token kind (= += -= ...), b = lvalue, c = rvalue
 *   MEMBER:        a = object, b/c = ident off/len
 *   INDEX:         a = object, b = index expr
 *   CALL:          a = callee, b = first arg (next via jn_next)
 *   COND:          a = cond, b = then, c = else
 *   PRE_INC/POST_INC: a = op token (PLUS_PLUS / MINUS_MINUS), b = target
 *   VAR_DECL:      a = first declarator (jn_next chain), b = decl kind
 *   VAR_DECLARATOR: a/b = name off/len, c = init expr (-1 if none)
 *   BLOCK:         a = first stmt (jn_next chain)
 *   IF:            a = cond, b = then-stmt, c = else-stmt (-1 if absent)
 *   WHILE:         a = cond, b = body
 *   FOR:           a = init, b = cond, c = step, d = body
 *   RETURN:        a = expr (-1 if void)
 *   FUNC_DECL/_EXPR: a/b = name off/len (-1/0 if anon), c = first param
 *                  (jn_next), d = body block
 *   ARR_LIT:       a = first element (jn_next chain)
 *   OBJ_LIT:       a = first OBJ_PROP (jn_next)
 *   OBJ_PROP:      a/b = key off/len, c = value expr
 *   PROGRAM:       a = first stmt (jn_next)
*/

int jp_pos;

int js_peek_kind() {
    if (jp_pos >= jtk_count) return JS_TOK_EOF;
    return jtk_kind[jp_pos];
}

int js_match(int kind) {
    if (js_peek_kind() != kind) return 0;
    jp_pos = jp_pos + 1;
    return 1;
}

void js_set_err(char *msg) {
    if (js_last_error[0] != 0) return;       /* keep first error */
    int i = 0;
    while (msg[i] && i < 255) { js_last_error[i] = msg[i]; i = i + 1; }
    js_last_error[i] = 0;
}

void js_expect(int kind, char *what) {
    if (!js_match(kind)) js_set_err(what);
}

int js_alloc_node(int kind, int a, int b, int c, int d) {
    if (jn_count >= MAX_JS_NODES) { js_set_err("js: AST pool full"); return -1; }
    int n = jn_count;
    jn_kind[n] = kind;
    jn_a   [n] = a;
    jn_b   [n] = b;
    jn_c   [n] = c;
    jn_d   [n] = d;
    jn_next[n] = -1;
    jn_count = n + 1;
    return n;
}

/* Mutually-recursive js_p_* calls below resolve via CupidC's deferred
 * cross-resolve pass; explicit prototypes upset the parser.*/
int js_p_primary() {
    int k = js_peek_kind();
    if (k == JS_TOK_NUMBER) {
        int v = jtk_num[jp_pos];
        jp_pos = jp_pos + 1;
        return js_alloc_node(JS_NODE_NUM, v, 0, 0, 0);
    }
    if (k == JS_TOK_STRING) {
        int o = jtk_str_off[jp_pos];
        int l = jtk_str_len[jp_pos];
        jp_pos = jp_pos + 1;
        return js_alloc_node(JS_NODE_STR, o, l, 0, 0);
    }
    if (k == JS_TOK_KW_TRUE)  { jp_pos = jp_pos + 1; return js_alloc_node(JS_NODE_BOOL,  1, 0, 0, 0); }
    if (k == JS_TOK_KW_FALSE) { jp_pos = jp_pos + 1; return js_alloc_node(JS_NODE_BOOL,  0, 0, 0, 0); }
    if (k == JS_TOK_KW_NULL)  { jp_pos = jp_pos + 1; return js_alloc_node(JS_NODE_NULL,  0, 0, 0, 0); }
    if (k == JS_TOK_KW_UNDEFINED) {
        jp_pos = jp_pos + 1; return js_alloc_node(JS_NODE_UNDEF, 0, 0, 0, 0);
    }
    if (k == JS_TOK_IDENT) {
        int o = jtk_str_off[jp_pos];
        int l = jtk_str_len[jp_pos];
        jp_pos = jp_pos + 1;
        return js_alloc_node(JS_NODE_IDENT, o, l, 0, 0);
    }
    if (k == JS_TOK_LPAREN) {
        jp_pos = jp_pos + 1;
        int e = js_p_expr();
        js_expect(JS_TOK_RPAREN, "js: expected ')'");
        return e;
    }
    if (k == JS_TOK_LBRACK) {
        /* array literal */
        jp_pos = jp_pos + 1;
        int first = -1;
        int prev = -1;
        while (js_peek_kind() != JS_TOK_RBRACK && js_peek_kind() != JS_TOK_EOF) {
            int e = js_p_assign();
            if (first < 0) first = e; else jn_next[prev] = e;
            prev = e;
            if (!js_match(JS_TOK_COMMA)) break;
        }
        js_expect(JS_TOK_RBRACK, "js: expected ']'");
        return js_alloc_node(JS_NODE_ARR_LIT, first, 0, 0, 0);
    }
    if (k == JS_TOK_LBRACE) {
        /* object literal */
        jp_pos = jp_pos + 1;
        int first = -1;
        int prev = -1;
        while (js_peek_kind() != JS_TOK_RBRACE && js_peek_kind() != JS_TOK_EOF) {
            int kk = js_peek_kind();
            int key_off = -1;
            int key_len = 0;
            if (kk == JS_TOK_IDENT || kk == JS_TOK_STRING) {
                key_off = jtk_str_off[jp_pos];
                key_len = jtk_str_len[jp_pos];
                jp_pos = jp_pos + 1;
            } else {
                js_set_err("js: expected object key");
                break;
            }
            js_expect(JS_TOK_COLON, "js: expected ':'");
            int val = js_p_assign();
            int prop = js_alloc_node(JS_NODE_OBJ_PROP, key_off, key_len, val, 0);
            if (first < 0) first = prop; else jn_next[prev] = prop;
            prev = prop;
            if (!js_match(JS_TOK_COMMA)) break;
        }
        js_expect(JS_TOK_RBRACE, "js: expected '}'");
        return js_alloc_node(JS_NODE_OBJ_LIT, first, 0, 0, 0);
    }
    if (k == JS_TOK_KW_FUNCTION) {
        /* function expression */
        jp_pos = jp_pos + 1;
        int name_off = -1;
        int name_len = 0;
        if (js_peek_kind() == JS_TOK_IDENT) {
            name_off = jtk_str_off[jp_pos];
            name_len = jtk_str_len[jp_pos];
            jp_pos = jp_pos + 1;
        }
        js_expect(JS_TOK_LPAREN, "js: expected '(' after function");
        int first = -1;
        int prev = -1;
        while (js_peek_kind() != JS_TOK_RPAREN && js_peek_kind() != JS_TOK_EOF) {
            if (js_peek_kind() != JS_TOK_IDENT) { js_set_err("js: expected param name"); break; }
            int p = js_alloc_node(JS_NODE_IDENT, jtk_str_off[jp_pos], jtk_str_len[jp_pos], 0, 0);
            jp_pos = jp_pos + 1;
            if (first < 0) first = p; else jn_next[prev] = p;
            prev = p;
            if (!js_match(JS_TOK_COMMA)) break;
        }
        js_expect(JS_TOK_RPAREN, "js: expected ')'");
        int body = js_p_block();
        return js_alloc_node(JS_NODE_FUNC_EXPR, name_off, name_len, first, body);
    }
    js_set_err("js: unexpected token in expression");
    jp_pos = jp_pos + 1;
    return js_alloc_node(JS_NODE_UNDEF, 0, 0, 0, 0);
}

int js_p_postfix() {
    int e = js_p_primary();
    while (1) {
        int k = js_peek_kind();
        if (k == JS_TOK_DOT) {
            jp_pos = jp_pos + 1;
            if (js_peek_kind() != JS_TOK_IDENT) { js_set_err("js: expected ident after '.'"); break; }
            int o = jtk_str_off[jp_pos];
            int l = jtk_str_len[jp_pos];
            jp_pos = jp_pos + 1;
            e = js_alloc_node(JS_NODE_MEMBER, e, o, l, 0);
            continue;
        }
        if (k == JS_TOK_LBRACK) {
            jp_pos = jp_pos + 1;
            int idx = js_p_expr();
            js_expect(JS_TOK_RBRACK, "js: expected ']'");
            e = js_alloc_node(JS_NODE_INDEX, e, idx, 0, 0);
            continue;
        }
        if (k == JS_TOK_LPAREN) {
            jp_pos = jp_pos + 1;
            int first = -1;
            int prev = -1;
            while (js_peek_kind() != JS_TOK_RPAREN && js_peek_kind() != JS_TOK_EOF) {
                int a = js_p_assign();
                if (first < 0) first = a; else jn_next[prev] = a;
                prev = a;
                if (!js_match(JS_TOK_COMMA)) break;
            }
            js_expect(JS_TOK_RPAREN, "js: expected ')'");
            e = js_alloc_node(JS_NODE_CALL, e, first, 0, 0);
            continue;
        }
        if (k == JS_TOK_PLUS_PLUS) {
            jp_pos = jp_pos + 1;
            e = js_alloc_node(JS_NODE_POST_INC, JS_TOK_PLUS_PLUS, e, 0, 0);
            continue;
        }
        if (k == JS_TOK_MINUS_MINUS) {
            jp_pos = jp_pos + 1;
            e = js_alloc_node(JS_NODE_POST_INC, JS_TOK_MINUS_MINUS, e, 0, 0);
            continue;
        }
        break;
    }
    return e;
}

int js_p_unary() {
    int k = js_peek_kind();
    if (k == JS_TOK_NOT || k == JS_TOK_MINUS || k == JS_TOK_PLUS ||
        k == JS_TOK_KW_TYPEOF) {
        jp_pos = jp_pos + 1;
        int operand = js_p_unary();
        return js_alloc_node(JS_NODE_UNARY, k, operand, 0, 0);
    }
    if (k == JS_TOK_PLUS_PLUS || k == JS_TOK_MINUS_MINUS) {
        jp_pos = jp_pos + 1;
        int operand = js_p_unary();
        return js_alloc_node(JS_NODE_PRE_INC, k, operand, 0, 0);
    }
    return js_p_postfix();
}

int js_p_mul() {
    int l = js_p_unary();
    while (1) {
        int k = js_peek_kind();
        if (k != JS_TOK_STAR && k != JS_TOK_SLASH && k != JS_TOK_PERCENT) break;
        jp_pos = jp_pos + 1;
        int r = js_p_unary();
        l = js_alloc_node(JS_NODE_BIN, k, l, r, 0);
    }
    return l;
}

int js_p_add() {
    int l = js_p_mul();
    while (1) {
        int k = js_peek_kind();
        if (k != JS_TOK_PLUS && k != JS_TOK_MINUS) break;
        jp_pos = jp_pos + 1;
        int r = js_p_mul();
        l = js_alloc_node(JS_NODE_BIN, k, l, r, 0);
    }
    return l;
}

int js_p_rel() {
    int l = js_p_add();
    while (1) {
        int k = js_peek_kind();
        if (k != JS_TOK_LT && k != JS_TOK_GT && k != JS_TOK_LE && k != JS_TOK_GE) break;
        jp_pos = jp_pos + 1;
        int r = js_p_add();
        l = js_alloc_node(JS_NODE_BIN, k, l, r, 0);
    }
    return l;
}

int js_p_eq() {
    int l = js_p_rel();
    while (1) {
        int k = js_peek_kind();
        if (k != JS_TOK_EQ && k != JS_TOK_NEQ &&
            k != JS_TOK_EQ_EQ && k != JS_TOK_NEQ_EQ) break;
        jp_pos = jp_pos + 1;
        int r = js_p_rel();
        l = js_alloc_node(JS_NODE_BIN, k, l, r, 0);
    }
    return l;
}

int js_p_logical_and() {
    int l = js_p_eq();
    while (js_peek_kind() == JS_TOK_AND_AND) {
        jp_pos = jp_pos + 1;
        int r = js_p_eq();
        l = js_alloc_node(JS_NODE_BIN, JS_TOK_AND_AND, l, r, 0);
    }
    return l;
}

int js_p_logical_or() {
    int l = js_p_logical_and();
    while (js_peek_kind() == JS_TOK_OR_OR) {
        jp_pos = jp_pos + 1;
        int r = js_p_logical_and();
        l = js_alloc_node(JS_NODE_BIN, JS_TOK_OR_OR, l, r, 0);
    }
    return l;
}

int js_p_cond() {
    int c = js_p_logical_or();
    if (js_peek_kind() == JS_TOK_QUESTION) {
        jp_pos = jp_pos + 1;
        int t = js_p_assign();
        js_expect(JS_TOK_COLON, "js: expected ':' in ?:");
        int e = js_p_assign();
        return js_alloc_node(JS_NODE_COND, c, t, e, 0);
    }
    return c;
}

int js_p_assign() {
    int l = js_p_cond();
    int k = js_peek_kind();
    if (k == JS_TOK_ASSIGN || k == JS_TOK_PLUS_EQ || k == JS_TOK_MINUS_EQ ||
        k == JS_TOK_STAR_EQ || k == JS_TOK_SLASH_EQ) {
        jp_pos = jp_pos + 1;
        int r = js_p_assign();
        return js_alloc_node(JS_NODE_ASSIGN, k, l, r, 0);
    }
    return l;
}

int js_p_expr() {
    return js_p_assign();
}

int js_p_var_decl() {
    int kind_kw = js_peek_kind();
    int kind = 0;
    if (kind_kw == JS_TOK_KW_LET) kind = 1;
    else if (kind_kw == JS_TOK_KW_CONST) kind = 2;
    jp_pos = jp_pos + 1;
    int first = -1;
    int prev = -1;
    while (1) {
        if (js_peek_kind() != JS_TOK_IDENT) { js_set_err("js: expected name in var decl"); break; }
        int o = jtk_str_off[jp_pos];
        int l = jtk_str_len[jp_pos];
        jp_pos = jp_pos + 1;
        int init = -1;
        if (js_match(JS_TOK_ASSIGN)) init = js_p_assign();
        int d = js_alloc_node(JS_NODE_VAR_DECLARATOR, o, l, init, 0);
        if (first < 0) first = d; else jn_next[prev] = d;
        prev = d;
        if (!js_match(JS_TOK_COMMA)) break;
    }
    js_match(JS_TOK_SEMI);     /* optional */
    return js_alloc_node(JS_NODE_VAR_DECL, first, kind, 0, 0);
}

int js_p_block() {
    js_expect(JS_TOK_LBRACE, "js: expected '{'");
    int first = -1;
    int prev = -1;
    while (js_peek_kind() != JS_TOK_RBRACE && js_peek_kind() != JS_TOK_EOF) {
        int s = js_p_stmt();
        if (s < 0) break;
        if (first < 0) first = s; else jn_next[prev] = s;
        prev = s;
    }
    js_expect(JS_TOK_RBRACE, "js: expected '}'");
    return js_alloc_node(JS_NODE_BLOCK, first, 0, 0, 0);
}

int js_p_if() {
    jp_pos = jp_pos + 1;       /* eat 'if' */
    js_expect(JS_TOK_LPAREN, "js: expected '(' after 'if'");
    int cond = js_p_expr();
    js_expect(JS_TOK_RPAREN, "js: expected ')'");
    int t = js_p_stmt();
    int e = -1;
    if (js_match(JS_TOK_KW_ELSE)) e = js_p_stmt();
    return js_alloc_node(JS_NODE_IF, cond, t, e, 0);
}

int js_p_while() {
    jp_pos = jp_pos + 1;
    js_expect(JS_TOK_LPAREN, "js: expected '(' after 'while'");
    int cond = js_p_expr();
    js_expect(JS_TOK_RPAREN, "js: expected ')'");
    int body = js_p_stmt();
    return js_alloc_node(JS_NODE_WHILE, cond, body, 0, 0);
}

int js_p_for() {
    jp_pos = jp_pos + 1;
    js_expect(JS_TOK_LPAREN, "js: expected '(' after 'for'");
    int init = -1;
    int k = js_peek_kind();
    if (k == JS_TOK_SEMI) { jp_pos = jp_pos + 1; }
    else if (k == JS_TOK_KW_VAR || k == JS_TOK_KW_LET || k == JS_TOK_KW_CONST) {
        init = js_p_var_decl();
    } else {
        init = js_alloc_node(JS_NODE_EXPR_STMT, js_p_expr(), 0, 0, 0);
        js_expect(JS_TOK_SEMI, "js: expected ';' after for-init");
    }
    int cond = -1;
    if (js_peek_kind() != JS_TOK_SEMI) cond = js_p_expr();
    js_expect(JS_TOK_SEMI, "js: expected ';' after for-cond");
    int step = -1;
    if (js_peek_kind() != JS_TOK_RPAREN) step = js_p_expr();
    js_expect(JS_TOK_RPAREN, "js: expected ')'");
    int body = js_p_stmt();
    return js_alloc_node(JS_NODE_FOR, init, cond, step, body);
}

int js_p_return() {
    jp_pos = jp_pos + 1;
    int e = -1;
    if (js_peek_kind() != JS_TOK_SEMI && js_peek_kind() != JS_TOK_RBRACE &&
        js_peek_kind() != JS_TOK_EOF) {
        e = js_p_expr();
    }
    js_match(JS_TOK_SEMI);
    return js_alloc_node(JS_NODE_RETURN, e, 0, 0, 0);
}

int js_p_function_decl() {
    jp_pos = jp_pos + 1;       /* eat 'function' */
    int name_off = -1;
    int name_len = 0;
    if (js_peek_kind() == JS_TOK_IDENT) {
        name_off = jtk_str_off[jp_pos];
        name_len = jtk_str_len[jp_pos];
        jp_pos = jp_pos + 1;
    }
    js_expect(JS_TOK_LPAREN, "js: expected '(' after function");
    int first = -1;
    int prev = -1;
    while (js_peek_kind() != JS_TOK_RPAREN && js_peek_kind() != JS_TOK_EOF) {
        if (js_peek_kind() != JS_TOK_IDENT) { js_set_err("js: expected param name"); break; }
        int p = js_alloc_node(JS_NODE_IDENT, jtk_str_off[jp_pos], jtk_str_len[jp_pos], 0, 0);
        jp_pos = jp_pos + 1;
        if (first < 0) first = p; else jn_next[prev] = p;
        prev = p;
        if (!js_match(JS_TOK_COMMA)) break;
    }
    js_expect(JS_TOK_RPAREN, "js: expected ')'");
    int body = js_p_block();
    return js_alloc_node(JS_NODE_FUNC_DECL, name_off, name_len, first, body);
}

int js_p_stmt() {
    int k = js_peek_kind();
    if (k == JS_TOK_LBRACE) return js_p_block();
    if (k == JS_TOK_SEMI) { jp_pos = jp_pos + 1; return js_alloc_node(JS_NODE_BLOCK, -1, 0, 0, 0); }
    if (k == JS_TOK_KW_VAR || k == JS_TOK_KW_LET || k == JS_TOK_KW_CONST) return js_p_var_decl();
    if (k == JS_TOK_KW_IF)       return js_p_if();
    if (k == JS_TOK_KW_WHILE)    return js_p_while();
    if (k == JS_TOK_KW_FOR)      return js_p_for();
    if (k == JS_TOK_KW_RETURN)   return js_p_return();
    if (k == JS_TOK_KW_BREAK)    { jp_pos = jp_pos + 1; js_match(JS_TOK_SEMI);
                                    return js_alloc_node(JS_NODE_BREAK, 0, 0, 0, 0); }
    if (k == JS_TOK_KW_CONTINUE) { jp_pos = jp_pos + 1; js_match(JS_TOK_SEMI);
                                    return js_alloc_node(JS_NODE_CONTINUE, 0, 0, 0, 0); }
    if (k == JS_TOK_KW_FUNCTION) return js_p_function_decl();
    int e = js_p_expr();
    js_match(JS_TOK_SEMI);
    return js_alloc_node(JS_NODE_EXPR_STMT, e, 0, 0, 0);
}

int js_parse() {
    jp_pos = 0;
    int first = -1;
    int prev = -1;
    while (js_peek_kind() != JS_TOK_EOF) {
        int s = js_p_stmt();
        if (s < 0) break;
        if (first < 0) first = s; else jn_next[prev] = s;
        prev = s;
        if (js_last_error[0] != 0) break;
    }
    return js_alloc_node(JS_NODE_PROGRAM, first, 0, 0, 0);
}

/* AST debug dump (serial). Used by F1a tests; later code routes
 * through about:dump.*/
char *js_kind_name(int k) {
    if (k == JS_NODE_NUM)        return "NUM";
    if (k == JS_NODE_STR)        return "STR";
    if (k == JS_NODE_BOOL)       return "BOOL";
    if (k == JS_NODE_NULL)       return "NULL";
    if (k == JS_NODE_UNDEF)      return "UNDEF";
    if (k == JS_NODE_IDENT)      return "IDENT";
    if (k == JS_NODE_BIN)        return "BIN";
    if (k == JS_NODE_UNARY)      return "UNARY";
    if (k == JS_NODE_ASSIGN)     return "ASSIGN";
    if (k == JS_NODE_MEMBER)     return "MEMBER";
    if (k == JS_NODE_INDEX)      return "INDEX";
    if (k == JS_NODE_CALL)       return "CALL";
    if (k == JS_NODE_COND)       return "COND";
    if (k == JS_NODE_PRE_INC)    return "PRE_INC";
    if (k == JS_NODE_POST_INC)   return "POST_INC";
    if (k == JS_NODE_EXPR_STMT)  return "EXPR_STMT";
    if (k == JS_NODE_VAR_DECL)   return "VAR_DECL";
    if (k == JS_NODE_VAR_DECLARATOR) return "DECLR";
    if (k == JS_NODE_BLOCK)      return "BLOCK";
    if (k == JS_NODE_IF)         return "IF";
    if (k == JS_NODE_WHILE)      return "WHILE";
    if (k == JS_NODE_FOR)        return "FOR";
    if (k == JS_NODE_RETURN)     return "RETURN";
    if (k == JS_NODE_BREAK)      return "BREAK";
    if (k == JS_NODE_CONTINUE)   return "CONTINUE";
    if (k == JS_NODE_FUNC_DECL)  return "FUNC_DECL";
    if (k == JS_NODE_FUNC_EXPR)  return "FUNC_EXPR";
    if (k == JS_NODE_ARR_LIT)    return "ARR";
    if (k == JS_NODE_OBJ_LIT)    return "OBJ";
    if (k == JS_NODE_OBJ_PROP)   return "PROP";
    if (k == JS_NODE_PROGRAM)    return "PROGRAM";
    return "?";
}

void js_dump_node(int n, int depth) {
    if (n < 0) return;
    int k = jn_kind[n];
    serial_printf("[js] %d%*s %s a=%d b=%d c=%d d=%d\n",
                  depth, depth * 2, "", js_kind_name(k),
                  jn_a[n], jn_b[n], jn_c[n], jn_d[n]);
    /* Recurse children based on kind. */
    if (k == JS_NODE_PROGRAM || k == JS_NODE_BLOCK || k == JS_NODE_VAR_DECL ||
        k == JS_NODE_ARR_LIT || k == JS_NODE_OBJ_LIT) {
        int c = jn_a[n];
        while (c >= 0) { js_dump_node(c, depth + 1); c = jn_next[c]; }
        return;
    }
    if (k == JS_NODE_BIN || k == JS_NODE_ASSIGN) {
        js_dump_node(jn_b[n], depth + 1);
        js_dump_node(jn_c[n], depth + 1);
        return;
    }
    if (k == JS_NODE_UNARY || k == JS_NODE_PRE_INC || k == JS_NODE_POST_INC) {
        js_dump_node(jn_b[n], depth + 1);
        return;
    }
    if (k == JS_NODE_COND || k == JS_NODE_IF) {
        js_dump_node(jn_a[n], depth + 1);
        js_dump_node(jn_b[n], depth + 1);
        if (jn_c[n] >= 0) js_dump_node(jn_c[n], depth + 1);
        return;
    }
    if (k == JS_NODE_WHILE) {
        js_dump_node(jn_a[n], depth + 1);
        js_dump_node(jn_b[n], depth + 1);
        return;
    }
    if (k == JS_NODE_FOR) {
        js_dump_node(jn_a[n], depth + 1);
        if (jn_b[n] >= 0) js_dump_node(jn_b[n], depth + 1);
        if (jn_c[n] >= 0) js_dump_node(jn_c[n], depth + 1);
        js_dump_node(jn_d[n], depth + 1);
        return;
    }
    if (k == JS_NODE_RETURN) {
        if (jn_a[n] >= 0) js_dump_node(jn_a[n], depth + 1);
        return;
    }
    if (k == JS_NODE_FUNC_DECL || k == JS_NODE_FUNC_EXPR) {
        int p = jn_c[n];
        while (p >= 0) { js_dump_node(p, depth + 1); p = jn_next[p]; }
        js_dump_node(jn_d[n], depth + 1);
        return;
    }
    if (k == JS_NODE_CALL) {
        js_dump_node(jn_a[n], depth + 1);
        int a = jn_b[n];
        while (a >= 0) { js_dump_node(a, depth + 1); a = jn_next[a]; }
        return;
    }
    if (k == JS_NODE_MEMBER || k == JS_NODE_INDEX) {
        js_dump_node(jn_a[n], depth + 1);
        if (k == JS_NODE_INDEX) js_dump_node(jn_b[n], depth + 1);
        return;
    }
    if (k == JS_NODE_OBJ_PROP) {
        js_dump_node(jn_c[n], depth + 1);
        return;
    }
    if (k == JS_NODE_EXPR_STMT) {
        js_dump_node(jn_a[n], depth + 1);
        return;
    }
    if (k == JS_NODE_VAR_DECLARATOR) {
        if (jn_c[n] >= 0) js_dump_node(jn_c[n], depth + 1);
        return;
    }
}

void js_dump_ast(int root) { js_dump_node(root, 0); }

/* Public engine entry. F1a: lex, parse, dump. F1b adds eval. */
void js_reset_per_page() {
    jtk_count = 0;
    jn_count = 0;
    js_str_pool_pos = 0;
    js_script_count = 0;
    js_last_error[0] = 0;
}

void js_queue_script(int off, int len) {
    if (js_script_count >= MAX_JS_SCRIPTS) return;
    js_script_off[js_script_count] = off;
    js_script_len[js_script_count] = len;
    js_script_count = js_script_count + 1;
}

int js_run(char *src, int len) {
    /* Per-script reset of token/AST pools so successive scripts don't
     * stack into a single pool. js_str_pool persists for the page so
     * later scripts can reference earlier literals.*/
    jtk_count = 0;
    int saved_jn = jn_count;
    js_last_error[0] = 0;
    js_ctrl_signal = 0;
    js_tokenize(src, len);
    int root = js_parse();
    if (js_last_error[0] != 0) {
        serial_printf("[js] parse error: %s\n", js_last_error);
        return -1;
    }
    serial_printf("[js] === script (%d tokens, %d nodes) ===\n",
                  jtk_count, jn_count - saved_jn);
    js_exec_program(root);
    if (js_last_error[0] != 0) {
        serial_printf("[js] runtime error: %s\n", js_last_error);
        return -1;
    }
    return 0;
}

void js_run_queued_scripts() {
    int i = 0;
    while (i < js_script_count) {
        char *src = attr_pool + js_script_off[i];
        int len = js_script_len[i];
        js_run(src, len);
        i = i + 1;
    }
}
