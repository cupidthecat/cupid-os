/* §7 JavaScript tree-walking interpreter. F1b scope:
 *   - primitives: number (double), string, bool, null, undefined
 *   - operators: + - * / %, == != === !==, < <= > >=, && || !
 *               assignment + compound, prefix/postfix ++/--
 *   - control flow: if/else, while, for(init;cond;step), break/continue
 *   - var declarations (let/const treated as var; const-write check
 *     deferred until F1c gives us the binding kind tag at lookup)
 *   - console.log builtin: stringify args, route to serial_printf and
 *     status_msg
 * Functions, objects, arrays land in F1c / F1d. */

/* ----- value stack helpers ----- */
void js_push_undef() {
    if (jvs_top >= MAX_JS_VS) return;
    int t = jvs_top;
    jvs_tag[t] = JS_VAL_UNDEF; jvs_num[t] = 0.0;
    jvs_str_off[t] = -1; jvs_str_len[t] = 0;
    jvs_obj_idx[t] = -1; jvs_dom_idx[t] = -1; jvs_native_id[t] = 0;
    jvs_top = t + 1;
}
void js_push_null() {
    js_push_undef();
    jvs_tag[jvs_top - 1] = JS_VAL_NULL;
}
void js_push_num(double v) {
    js_push_undef();
    int t = jvs_top - 1;
    jvs_tag[t] = JS_VAL_NUM; jvs_num[t] = v;
}
void js_push_bool(int b) {
    js_push_undef();
    int t = jvs_top - 1;
    jvs_tag[t] = JS_VAL_BOOL; jvs_num[t] = b ? 1.0 : 0.0;
}
void js_push_str(int off, int len) {
    js_push_undef();
    int t = jvs_top - 1;
    jvs_tag[t] = JS_VAL_STR; jvs_str_off[t] = off; jvs_str_len[t] = len;
}

void js_pop() { if (jvs_top > 0) jvs_top = jvs_top - 1; }

void js_copy_top_from(int src) {
    if (jvs_top >= MAX_JS_VS || src < 0 || src >= jvs_top) return;
    int t = jvs_top;
    jvs_tag[t]      = jvs_tag[src];
    jvs_num[t]      = jvs_num[src];
    jvs_str_off[t]  = jvs_str_off[src];
    jvs_str_len[t]  = jvs_str_len[src];
    jvs_obj_idx[t]  = jvs_obj_idx[src];
    jvs_dom_idx[t]  = jvs_dom_idx[src];
    jvs_native_id[t]= jvs_native_id[src];
    jvs_top = t + 1;
}

/* ----- coercion ----- */
double js_to_number_at(int idx) {
    int t = jvs_tag[idx];
    if (t == JS_VAL_NUM)  return jvs_num[idx];
    if (t == JS_VAL_BOOL) return jvs_num[idx];
    if (t == JS_VAL_NULL) return 0.0;
    if (t == JS_VAL_STR) {
        char *s = js_str_pool + jvs_str_off[idx];
        int sign = 1;
        int i = 0;
        while (s[i] == ' ' || s[i] == '\t') i = i + 1;
        if (s[i] == '-') { sign = -1; i = i + 1; }
        else if (s[i] == '+') { i = i + 1; }
        double v = 0.0;
        int saw = 0;
        while (s[i] >= '0' && s[i] <= '9') {
            v = v * 10.0 + (double)(s[i] - '0');
            i = i + 1; saw = 1;
        }
        if (s[i] == '.') {
            i = i + 1;
            double frac = 0.1;
            while (s[i] >= '0' && s[i] <= '9') {
                v = v + frac * (double)(s[i] - '0');
                frac = frac * 0.1;
                i = i + 1; saw = 1;
            }
        }
        if (!saw) return 0.0;
        return sign * v;
    }
    return 0.0;
}

int js_to_bool_at(int idx) {
    int t = jvs_tag[idx];
    if (t == JS_VAL_UNDEF || t == JS_VAL_NULL) return 0;
    if (t == JS_VAL_BOOL) return jvs_num[idx] != 0.0;
    if (t == JS_VAL_NUM) return jvs_num[idx] != 0.0;
    if (t == JS_VAL_STR) return jvs_str_len[idx] > 0;
    return 1;       /* objects/funcs always truthy */
}

/* Format a double into buf without %f. Integer fast-path for "small"
 * whole numbers (most arithmetic results). */
int js_format_num(double v, char *buf, int max) {
    int b = 0;
    if (v < 0.0) { if (b < max - 1) buf[b] = '-'; b = b + 1; v = -v; }
    /* integer part */
    int int_part = (int)v;
    double frac = v - (double)int_part;
    /* digits */
    char tmp[32];
    int tn = 0;
    if (int_part == 0) { tmp[tn] = '0'; tn = tn + 1; }
    while (int_part > 0 && tn < 31) {
        tmp[tn] = '0' + (int_part % 10);
        int_part = int_part / 10;
        tn = tn + 1;
    }
    while (tn > 0 && b < max - 1) { tn = tn - 1; buf[b] = tmp[tn]; b = b + 1; }
    /* fractional - up to 6 digits, trimmed */
    if (frac > 0.0000001) {
        if (b < max - 1) { buf[b] = '.'; b = b + 1; }
        int digits = 0;
        while (digits < 6 && frac > 0.0000001 && b < max - 1) {
            frac = frac * 10.0;
            int d = (int)frac;
            if (d > 9) d = 9;
            frac = frac - (double)d;
            buf[b] = '0' + d;
            b = b + 1;
            digits = digits + 1;
        }
    }
    if (b < max) buf[b] = 0; else buf[max - 1] = 0;
    return b;
}

int js_to_string_at(int idx, char *buf, int max) {
    int t = jvs_tag[idx];
    if (t == JS_VAL_UNDEF) {
        char *s = "undefined"; int i = 0;
        while (s[i] && i < max - 1) { buf[i] = s[i]; i = i + 1; }
        buf[i] = 0; return i;
    }
    if (t == JS_VAL_NULL) {
        char *s = "null"; int i = 0;
        while (s[i] && i < max - 1) { buf[i] = s[i]; i = i + 1; }
        buf[i] = 0; return i;
    }
    if (t == JS_VAL_BOOL) {
        char *s = (jvs_num[idx] != 0.0) ? "true" : "false";
        int i = 0;
        while (s[i] && i < max - 1) { buf[i] = s[i]; i = i + 1; }
        buf[i] = 0; return i;
    }
    if (t == JS_VAL_NUM) return js_format_num(jvs_num[idx], buf, max);
    if (t == JS_VAL_STR) {
        int i = 0;
        int n = jvs_str_len[idx];
        char *s = js_str_pool + jvs_str_off[idx];
        while (i < n && i < max - 1) { buf[i] = s[i]; i = i + 1; }
        buf[i] = 0; return i;
    }
    char *s = "[object]"; int i = 0;
    while (s[i] && i < max - 1) { buf[i] = s[i]; i = i + 1; }
    buf[i] = 0; return i;
}

int js_eq_at(int a, int b) {
    /* loose equality - just a thin pass for numbers, strings, bools.
     * Object identity is deferred until F1d. */
    int ta = jvs_tag[a]; int tb = jvs_tag[b];
    if (ta == JS_VAL_NULL && tb == JS_VAL_UNDEF) return 1;
    if (ta == JS_VAL_UNDEF && tb == JS_VAL_NULL) return 1;
    if (ta == JS_VAL_NUM || tb == JS_VAL_NUM) {
        return js_to_number_at(a) == js_to_number_at(b);
    }
    if (ta == JS_VAL_STR && tb == JS_VAL_STR) {
        if (jvs_str_len[a] != jvs_str_len[b]) return 0;
        char *sa = js_str_pool + jvs_str_off[a];
        char *sb = js_str_pool + jvs_str_off[b];
        int n = jvs_str_len[a];
        for (int i = 0; i < n; i++) if (sa[i] != sb[i]) return 0;
        return 1;
    }
    if (ta == JS_VAL_BOOL && tb == JS_VAL_BOOL) return jvs_num[a] == jvs_num[b];
    return ta == tb;
}

int js_strict_eq_at(int a, int b) {
    if (jvs_tag[a] != jvs_tag[b]) return 0;
    return js_eq_at(a, b);
}

/* ----- scope / bindings ----- */
int js_scope_enter(int parent) {
    if (jsc_top >= MAX_JS_SCOPES) { js_set_err("js: scope overflow"); return -1; }
    int s = jsc_top;
    jsc_parent[s] = parent;
    jsc_first[s]  = jb_count;
    jsc_count[s]  = 0;
    jsc_top = s + 1;
    return s;
}

int js_binding_alloc(int scope, int name_off, int name_len) {
    if (jb_count >= MAX_JS_BINDINGS) { js_set_err("js: bindings overflow"); return -1; }
    int b = jb_count;
    jb_name_off[b] = name_off;
    jb_name_len[b] = name_len;
    jb_tag[b]      = JS_VAL_UNDEF;
    jb_num[b]      = 0.0;
    jb_str_off[b]  = -1;
    jb_str_len[b]  = 0;
    jb_obj_idx[b]  = -1;
    jb_dom_idx[b]  = -1;
    jb_count = b + 1;
    jsc_count[scope] = jsc_count[scope] + 1;
    return b;
}

int js_str_eq(int off1, int len1, int off2, int len2) {
    if (len1 != len2) return 0;
    char *s1 = js_str_pool + off1;
    char *s2 = js_str_pool + off2;
    for (int i = 0; i < len1; i++) if (s1[i] != s2[i]) return 0;
    return 1;
}

int js_lookup_binding(int scope, int name_off, int name_len) {
    int s = scope;
    while (s >= 0) {
        int first = jsc_first[s];
        int n = jsc_count[s];
        for (int i = 0; i < n; i++) {
            int b = first + i;
            if (js_str_eq(jb_name_off[b], jb_name_len[b], name_off, name_len)) {
                return b;
            }
        }
        s = jsc_parent[s];
    }
    return -1;
}

void js_binding_set_from_top(int b) {
    int t = jvs_top - 1;
    jb_tag[b]      = jvs_tag[t];
    jb_num[b]      = jvs_num[t];
    jb_str_off[b]  = jvs_str_off[t];
    jb_str_len[b]  = jvs_str_len[t];
    jb_obj_idx[b]  = jvs_obj_idx[t];
    jb_dom_idx[b]  = jvs_dom_idx[t];
}

void js_push_from_binding(int b) {
    js_push_undef();
    int t = jvs_top - 1;
    jvs_tag[t]     = jb_tag[b];
    jvs_num[t]     = jb_num[b];
    jvs_str_off[t] = jb_str_off[b];
    jvs_str_len[t] = jb_str_len[b];
    jvs_obj_idx[t] = jb_obj_idx[b];
    jvs_dom_idx[t] = jb_dom_idx[b];
}

/* ----- console builtin ----- */
void js_console_log_top_n(int argc) {
    /* args sit on the value stack at [top-argc .. top-1]. */
    char line[512];
    int p = 0;
    for (int i = 0; i < argc; i++) {
        int idx = jvs_top - argc + i;
        if (i > 0 && p < 511) { line[p] = ' '; p = p + 1; }
        char tmp[256];
        int tl = js_to_string_at(idx, tmp, 256);
        for (int k = 0; k < tl && p < 511; k++) { line[p] = tmp[k]; p = p + 1; }
    }
    line[p] = 0;
    serial_printf("[js] %s\n", line);
    /* status bar mirror so the user sees it without serial */
    int s = 0;
    char *prefix = "[js] ";
    while (prefix[s] && s < 5) { status_msg[s] = prefix[s]; s = s + 1; }
    int k = 0;
    while (k < p && s < 255) { status_msg[s] = line[k]; s = s + 1; k = k + 1; }
    status_msg[s] = 0;
}

/* ----- expression eval ----- */
void js_eval_expr(int node);
void js_eval_stmt(int node);

int js_alloc_object(int kind) {
    if (jobj_count >= MAX_JS_OBJS) { js_set_err("js: object pool full"); return -1; }
    int o = jobj_count;
    jobj_kind[o] = kind;
    jobj_first_prop[o] = -1;
    jobj_arr_len[o] = 0;
    jobj_count = o + 1;
    return o;
}

void js_push_obj(int obj_idx) {
    js_push_undef();
    int t = jvs_top - 1;
    jvs_tag[t] = JS_VAL_OBJ;
    jvs_obj_idx[t] = obj_idx;
}
void js_push_arr(int obj_idx) {
    js_push_undef();
    int t = jvs_top - 1;
    jvs_tag[t] = JS_VAL_ARR;
    jvs_obj_idx[t] = obj_idx;
}

/* Find property `key` on object `obj`. Returns property index or -1. */
int js_obj_find_prop(int obj, int key_off, int key_len) {
    int p = jobj_first_prop[obj];
    while (p >= 0) {
        if (js_str_eq(jp_key_off[p], jp_key_len[p], key_off, key_len)) return p;
        p = jp_next[p];
    }
    return -1;
}

int js_obj_set_prop_from_top(int obj, int key_off, int key_len) {
    int p = js_obj_find_prop(obj, key_off, key_len);
    if (p < 0) {
        if (jp_count >= MAX_JS_PROPS) { js_set_err("js: prop pool full"); return -1; }
        p = jp_count;
        jp_key_off[p] = key_off;
        jp_key_len[p] = key_len;
        jp_next[p] = jobj_first_prop[obj];
        jobj_first_prop[obj] = p;
        jp_count = p + 1;
    }
    int t = jvs_top - 1;
    jp_tag    [p] = jvs_tag[t];
    jp_num    [p] = jvs_num[t];
    jp_str_off[p] = jvs_str_off[t];
    jp_str_len[p] = jvs_str_len[t];
    jp_obj_idx[p] = jvs_obj_idx[t];
    jp_dom_idx[p] = jvs_dom_idx[t];
    return p;
}

void js_push_from_prop(int p) {
    js_push_undef();
    int t = jvs_top - 1;
    jvs_tag[t]     = jp_tag[p];
    jvs_num[t]     = jp_num[p];
    jvs_str_off[t] = jp_str_off[p];
    jvs_str_len[t] = jp_str_len[p];
    jvs_obj_idx[t] = jp_obj_idx[p];
    jvs_dom_idx[t] = jp_dom_idx[p];
}

/* For [obj][index]: convert TOS to a string key; returns interned offset. */
int js_index_top_to_key(int *out_off, int *out_len) {
    int t = jvs_top - 1;
    if (jvs_tag[t] == JS_VAL_STR) {
        *out_off = jvs_str_off[t];
        *out_len = jvs_str_len[t];
        return 0;
    }
    char buf[64];
    int n = js_to_string_at(t, buf, 64);
    int off = js_str_intern(buf, n);
    *out_off = off;
    *out_len = n;
    return 0;
}

int js_alloc_function(int param_first, int body, int captured_scope) {
    if (jfn_count >= MAX_JS_FUNCS) { js_set_err("js: function pool full"); return -1; }
    int f = jfn_count;
    jfn_param_first[f]    = param_first;
    jfn_body[f]           = body;
    jfn_captured_scope[f] = captured_scope;
    jfn_native_id[f]      = -1;
    jfn_count = f + 1;
    return f;
}

void js_push_func(int fn_idx) {
    js_push_undef();
    int t = jvs_top - 1;
    jvs_tag[t] = JS_VAL_FUNC;
    jvs_obj_idx[t] = fn_idx;
}

void js_call_user_function(int fn_idx, int argc) {
    /* args sit at [jvs_top-argc .. jvs_top-1]. Build a fresh scope
     * frame parented to the function's captured scope (closure), bind
     * each param, run the body, restore caller scope. */
    int saved_scope = jsc_cur;
    int new_scope = js_scope_enter(jfn_captured_scope[fn_idx]);
    if (new_scope < 0) { jvs_top = jvs_top - argc; js_push_undef(); return; }
    jsc_cur = new_scope;
    /* bind params */
    int p = jfn_param_first[fn_idx];
    int i = 0;
    while (p >= 0) {
        int o = jn_a[p]; int l = jn_b[p];
        int b = js_binding_alloc(new_scope, o, l);
        if (i < argc && b >= 0) {
            int src = jvs_top - argc + i;
            jb_tag[b]      = jvs_tag[src];
            jb_num[b]      = jvs_num[src];
            jb_str_off[b]  = jvs_str_off[src];
            jb_str_len[b]  = jvs_str_len[src];
            jb_obj_idx[b]  = jvs_obj_idx[src];
            jb_dom_idx[b]  = jvs_dom_idx[src];
        }
        p = jn_next[p];
        i = i + 1;
    }
    /* drop args */
    jvs_top = jvs_top - argc;
    /* execute body */
    int saved_signal = js_ctrl_signal;
    js_ctrl_signal = 0;
    int saved_vs_top = jvs_top;
    js_eval_stmt(jfn_body[fn_idx]);
    /* if RETURN signaled, top of stack already holds the return value. */
    if (js_ctrl_signal != 3) {
        /* fell off end - push undefined */
        if (jvs_top == saved_vs_top) js_push_undef();
        else {
            /* stray values on stack from expr stmts - drop them */
            while (jvs_top > saved_vs_top + 1) js_pop();
        }
    }
    js_ctrl_signal = saved_signal;
    jsc_cur = saved_scope;
}

void js_eval_call(int node) {
    int callee = jn_a[node];
    /* Special-case console.log syntactically (F1b legacy) until a
     * proper object/property infra exists in F1d. */
    int handled_console_log = 0;
    if (callee >= 0 && jn_kind[callee] == JS_NODE_MEMBER) {
        int obj = jn_a[callee];
        int koff = jn_b[callee];
        int klen = jn_c[callee];
        if (obj >= 0 && jn_kind[obj] == JS_NODE_IDENT) {
            int ioff = jn_a[obj]; int ilen = jn_b[obj];
            if (ilen == 7 && klen == 3 &&
                js_str_eq(ioff, ilen,
                          js_str_intern("console", 7), 7) &&
                js_str_eq(koff, klen,
                          js_str_intern("log", 3), 3)) {
                handled_console_log = 1;
            }
        }
    }
    /* Evaluate callee (unless console.log shortcut) and args. */
    int saved = jvs_top;
    if (!handled_console_log) js_eval_expr(callee);
    int callee_top = jvs_top - 1;
    int argc = 0;
    int arg = jn_b[node];
    while (arg >= 0) {
        js_eval_expr(arg);
        arg = jn_next[arg];
        argc = argc + 1;
    }
    if (handled_console_log) {
        js_console_log_top_n(argc);
        jvs_top = saved;
        js_push_undef();
        return;
    }
    /* Inspect callee value */
    int ctag = jvs_tag[callee_top];
    if (ctag == JS_VAL_FUNC) {
        int fn_idx = jvs_obj_idx[callee_top];
        /* Drop callee from the stack so args become contiguous at top. */
        for (int k = 0; k < argc; k++) {
            int dst = callee_top + k;
            int src = callee_top + 1 + k;
            jvs_tag[dst]     = jvs_tag[src];
            jvs_num[dst]     = jvs_num[src];
            jvs_str_off[dst] = jvs_str_off[src];
            jvs_str_len[dst] = jvs_str_len[src];
            jvs_obj_idx[dst] = jvs_obj_idx[src];
            jvs_dom_idx[dst] = jvs_dom_idx[src];
        }
        jvs_top = callee_top + argc;
        js_call_user_function(fn_idx, argc);
        return;
    }
    jvs_top = saved;
    js_set_err("js: callee is not a function");
    js_push_undef();
}

void js_assign_to_target(int target_node) {
    /* Top of stack holds the rvalue. */
    if (target_node < 0) { js_pop(); return; }
    int kind = jn_kind[target_node];
    if (kind == JS_NODE_IDENT) {
        int o = jn_a[target_node]; int l = jn_b[target_node];
        int b = js_lookup_binding(jsc_cur, o, l);
        if (b < 0) {
            /* Implicit global - create at root scope. */
            b = js_binding_alloc(0, o, l);
        }
        if (b >= 0) js_binding_set_from_top(b);
        return;
    }
    if (kind == JS_NODE_MEMBER) {
        /* Save rvalue, evaluate object, restore rvalue on top. */
        int rv_pos = jvs_top - 1;
        js_eval_expr(jn_a[target_node]);
        int obj_top = jvs_top - 1;
        int koff = jn_b[target_node]; int klen = jn_c[target_node];
        int tag = jvs_tag[obj_top];
        int oi  = jvs_obj_idx[obj_top];
        if (tag == JS_VAL_OBJ || tag == JS_VAL_ARR) {
            /* Move rvalue to top of stack (it's at rv_pos, currently
             * shadowed by the object). Swap so the rvalue sits at top. */
            int srct = jvs_tag[rv_pos];
            double srcn = jvs_num[rv_pos];
            int srcs_o = jvs_str_off[rv_pos];
            int srcs_l = jvs_str_len[rv_pos];
            int srco   = jvs_obj_idx[rv_pos];
            int srcd   = jvs_dom_idx[rv_pos];
            /* Top now is obj; we want rvalue on top so set_prop_from_top sees it. */
            jvs_tag[obj_top]     = srct;
            jvs_num[obj_top]     = srcn;
            jvs_str_off[obj_top] = srcs_o;
            jvs_str_len[obj_top] = srcs_l;
            jvs_obj_idx[obj_top] = srco;
            jvs_dom_idx[obj_top] = srcd;
            js_obj_set_prop_from_top(oi, koff, klen);
            jvs_top = rv_pos;        /* leave rvalue at original slot */
            jvs_tag[rv_pos]     = srct;
            jvs_num[rv_pos]     = srcn;
            jvs_str_off[rv_pos] = srcs_o;
            jvs_str_len[rv_pos] = srcs_l;
            jvs_obj_idx[rv_pos] = srco;
            jvs_dom_idx[rv_pos] = srcd;
            jvs_top = rv_pos + 1;
            return;
        }
        jvs_top = rv_pos + 1;
        js_pop();
        return;
    }
    if (kind == JS_NODE_INDEX) {
        int rv_pos = jvs_top - 1;
        js_eval_expr(jn_a[target_node]);
        int obj_top = jvs_top - 1;
        int otag = jvs_tag[obj_top];
        int oi   = jvs_obj_idx[obj_top];
        js_eval_expr(jn_b[target_node]);
        int koff; int klen;
        js_index_top_to_key(&koff, &klen);
        jvs_top = obj_top + 1;       /* drop key, leave object on top */
        if (otag == JS_VAL_OBJ || otag == JS_VAL_ARR) {
            int srct = jvs_tag[rv_pos];
            double srcn = jvs_num[rv_pos];
            int srcs_o = jvs_str_off[rv_pos];
            int srcs_l = jvs_str_len[rv_pos];
            int srco   = jvs_obj_idx[rv_pos];
            int srcd   = jvs_dom_idx[rv_pos];
            jvs_tag[obj_top]     = srct;
            jvs_num[obj_top]     = srcn;
            jvs_str_off[obj_top] = srcs_o;
            jvs_str_len[obj_top] = srcs_l;
            jvs_obj_idx[obj_top] = srco;
            jvs_dom_idx[obj_top] = srcd;
            js_obj_set_prop_from_top(oi, koff, klen);
            jvs_top = rv_pos + 1;
            jvs_tag[rv_pos]     = srct;
            jvs_num[rv_pos]     = srcn;
            jvs_str_off[rv_pos] = srcs_o;
            jvs_str_len[rv_pos] = srcs_l;
            jvs_obj_idx[rv_pos] = srco;
            jvs_dom_idx[rv_pos] = srcd;
            return;
        }
        jvs_top = rv_pos + 1;
        js_pop();
        return;
    }
    js_set_err("js: assignment target unsupported");
}

void js_eval_assign(int node) {
    int op = jn_a[node];
    int lhs = jn_b[node];
    int rhs = jn_c[node];
    if (op == JS_TOK_ASSIGN) {
        js_eval_expr(rhs);
        /* duplicate top so assignment leaves the value on the stack */
        js_copy_top_from(jvs_top - 1);
        js_assign_to_target(lhs);
        return;
    }
    /* compound assignment: load lhs, op with rhs, store, leave value */
    js_eval_expr(lhs);
    js_eval_expr(rhs);
    int a = jvs_top - 2;
    int b = jvs_top - 1;
    double na = js_to_number_at(a);
    double nb = js_to_number_at(b);
    double r = na;
    if (op == JS_TOK_PLUS_EQ) r = na + nb;
    else if (op == JS_TOK_MINUS_EQ) r = na - nb;
    else if (op == JS_TOK_STAR_EQ)  r = na * nb;
    else if (op == JS_TOK_SLASH_EQ) r = (nb != 0.0) ? (na / nb) : 0.0;
    jvs_top = a;
    js_push_num(r);
    js_copy_top_from(jvs_top - 1);
    js_assign_to_target(lhs);
}

void js_eval_bin(int node) {
    int op = jn_a[node];
    int l = jn_b[node];
    int r = jn_c[node];
    /* Short-circuit operators evaluate one side first. */
    if (op == JS_TOK_AND_AND) {
        js_eval_expr(l);
        if (!js_to_bool_at(jvs_top - 1)) return;
        js_pop();
        js_eval_expr(r);
        return;
    }
    if (op == JS_TOK_OR_OR) {
        js_eval_expr(l);
        if (js_to_bool_at(jvs_top - 1)) return;
        js_pop();
        js_eval_expr(r);
        return;
    }
    js_eval_expr(l);
    js_eval_expr(r);
    int a = jvs_top - 2;
    int b = jvs_top - 1;
    /* + with any string -> string concat */
    if (op == JS_TOK_PLUS && (jvs_tag[a] == JS_VAL_STR || jvs_tag[b] == JS_VAL_STR)) {
        char la[256]; char lb[256];
        int al = js_to_string_at(a, la, 256);
        int bl = js_to_string_at(b, lb, 256);
        char joined[512];
        int p = 0;
        for (int i = 0; i < al && p < 511; i++) { joined[p] = la[i]; p = p + 1; }
        for (int i = 0; i < bl && p < 511; i++) { joined[p] = lb[i]; p = p + 1; }
        joined[p] = 0;
        int off = js_str_intern(joined, p);
        jvs_top = a;
        js_push_str(off, p);
        return;
    }
    if (op == JS_TOK_EQ_EQ || op == JS_TOK_EQ) {
        int eq = (op == JS_TOK_EQ_EQ) ? js_strict_eq_at(a, b) : js_eq_at(a, b);
        jvs_top = a; js_push_bool(eq); return;
    }
    if (op == JS_TOK_NEQ_EQ || op == JS_TOK_NEQ) {
        int eq = (op == JS_TOK_NEQ_EQ) ? js_strict_eq_at(a, b) : js_eq_at(a, b);
        jvs_top = a; js_push_bool(!eq); return;
    }
    double na = js_to_number_at(a);
    double nb = js_to_number_at(b);
    double v = 0.0;
    int as_bool = 0;
    int bv = 0;
    if (op == JS_TOK_PLUS)        v = na + nb;
    else if (op == JS_TOK_MINUS)  v = na - nb;
    else if (op == JS_TOK_STAR)   v = na * nb;
    else if (op == JS_TOK_SLASH)  v = (nb != 0.0) ? (na / nb) : 0.0;
    else if (op == JS_TOK_PERCENT) v = (nb != 0.0) ? (na - ((double)((int)(na / nb))) * nb) : 0.0;
    else if (op == JS_TOK_LT) { as_bool = 1; bv = na <  nb; }
    else if (op == JS_TOK_GT) { as_bool = 1; bv = na >  nb; }
    else if (op == JS_TOK_LE) { as_bool = 1; bv = na <= nb; }
    else if (op == JS_TOK_GE) { as_bool = 1; bv = na >= nb; }
    jvs_top = a;
    if (as_bool) js_push_bool(bv); else js_push_num(v);
}

void js_eval_unary(int node) {
    int op = jn_a[node];
    int operand = jn_b[node];
    js_eval_expr(operand);
    int t = jvs_top - 1;
    if (op == JS_TOK_NOT) {
        int b = !js_to_bool_at(t);
        jvs_top = t; js_push_bool(b); return;
    }
    if (op == JS_TOK_MINUS) {
        double v = -js_to_number_at(t);
        jvs_top = t; js_push_num(v); return;
    }
    if (op == JS_TOK_PLUS) {
        double v = js_to_number_at(t);
        jvs_top = t; js_push_num(v); return;
    }
    if (op == JS_TOK_KW_TYPEOF) {
        char *s = "undefined";
        int tag = jvs_tag[t];
        if (tag == JS_VAL_NUM)  s = "number";
        else if (tag == JS_VAL_STR)  s = "string";
        else if (tag == JS_VAL_BOOL) s = "boolean";
        else if (tag == JS_VAL_NULL) s = "object";
        else if (tag == JS_VAL_FUNC || tag == JS_VAL_NATIVE) s = "function";
        else if (tag == JS_VAL_OBJ || tag == JS_VAL_ARR || tag == JS_VAL_DOMNODE) s = "object";
        int sl = 0; while (s[sl]) sl = sl + 1;
        int off = js_str_intern(s, sl);
        jvs_top = t; js_push_str(off, sl); return;
    }
}

void js_eval_inc(int node, int post) {
    int op = jn_a[node];
    int operand = jn_b[node];
    if (jn_kind[operand] != JS_NODE_IDENT) {
        js_set_err("js: ++/-- target must be identifier");
        js_push_undef();
        return;
    }
    int o = jn_a[operand]; int l = jn_b[operand];
    int b = js_lookup_binding(jsc_cur, o, l);
    if (b < 0) { js_set_err("js: undefined variable in ++/--"); js_push_undef(); return; }
    double cur = (jb_tag[b] == JS_VAL_NUM) ? jb_num[b]
               : (jb_tag[b] == JS_VAL_BOOL ? jb_num[b] : 0.0);
    double next = (op == JS_TOK_PLUS_PLUS) ? cur + 1.0 : cur - 1.0;
    if (post) js_push_num(cur); else js_push_num(next);
    jb_tag[b] = JS_VAL_NUM;
    jb_num[b] = next;
}

void js_eval_expr(int node) {
    if (node < 0) { js_push_undef(); return; }
    if (js_last_error[0] != 0) { js_push_undef(); return; }
    int k = jn_kind[node];
    if (k == JS_NODE_NUM)   { js_push_num((double)jn_a[node]); return; }
    if (k == JS_NODE_STR)   { js_push_str(jn_a[node], jn_b[node]); return; }
    if (k == JS_NODE_BOOL)  { js_push_bool(jn_a[node]); return; }
    if (k == JS_NODE_NULL)  { js_push_null(); return; }
    if (k == JS_NODE_UNDEF) { js_push_undef(); return; }
    if (k == JS_NODE_IDENT) {
        int o = jn_a[node]; int l = jn_b[node];
        int b = js_lookup_binding(jsc_cur, o, l);
        if (b < 0) { js_push_undef(); return; }
        js_push_from_binding(b); return;
    }
    if (k == JS_NODE_BIN)        { js_eval_bin(node); return; }
    if (k == JS_NODE_UNARY)      { js_eval_unary(node); return; }
    if (k == JS_NODE_ASSIGN)     { js_eval_assign(node); return; }
    if (k == JS_NODE_PRE_INC)    { js_eval_inc(node, 0); return; }
    if (k == JS_NODE_POST_INC)   { js_eval_inc(node, 1); return; }
    if (k == JS_NODE_COND) {
        js_eval_expr(jn_a[node]);
        int b = js_to_bool_at(jvs_top - 1);
        js_pop();
        if (b) js_eval_expr(jn_b[node]); else js_eval_expr(jn_c[node]);
        return;
    }
    if (k == JS_NODE_CALL) { js_eval_call(node); return; }
    if (k == JS_NODE_FUNC_EXPR) {
        int fn = js_alloc_function(jn_c[node], jn_d[node], jsc_cur);
        if (fn < 0) { js_push_undef(); return; }
        js_push_func(fn);
        return;
    }
    if (k == JS_NODE_ARR_LIT) {
        int o = js_alloc_object(1);
        if (o < 0) { js_push_undef(); return; }
        int e = jn_a[node];
        int i = 0;
        char keybuf[16];
        while (e >= 0) {
            js_eval_expr(e);
            int kn = js_format_num((double)i, keybuf, 16);
            int koff = js_str_intern(keybuf, kn);
            js_obj_set_prop_from_top(o, koff, kn);
            js_pop();
            i = i + 1;
            e = jn_next[e];
        }
        jobj_arr_len[o] = i;
        js_push_arr(o);
        return;
    }
    if (k == JS_NODE_OBJ_LIT) {
        int o = js_alloc_object(0);
        if (o < 0) { js_push_undef(); return; }
        int prop = jn_a[node];
        while (prop >= 0) {
            int koff = jn_a[prop];
            int klen = jn_b[prop];
            int val = jn_c[prop];
            js_eval_expr(val);
            js_obj_set_prop_from_top(o, koff, klen);
            js_pop();
            prop = jn_next[prop];
        }
        js_push_obj(o);
        return;
    }
    if (k == JS_NODE_MEMBER) {
        js_eval_expr(jn_a[node]);
        int t = jvs_top - 1;
        int koff = jn_b[node]; int klen = jn_c[node];
        int tag = jvs_tag[t];
        /* arrays expose .length */
        if (tag == JS_VAL_ARR && klen == 6 &&
            js_str_eq(koff, klen, js_str_intern("length", 6), 6)) {
            double n = (double)jobj_arr_len[jvs_obj_idx[t]];
            jvs_top = t;
            js_push_num(n);
            return;
        }
        if (tag == JS_VAL_OBJ || tag == JS_VAL_ARR) {
            int p = js_obj_find_prop(jvs_obj_idx[t], koff, klen);
            jvs_top = t;
            if (p >= 0) js_push_from_prop(p); else js_push_undef();
            return;
        }
        jvs_top = t;
        js_push_undef();
        return;
    }
    if (k == JS_NODE_INDEX) {
        js_eval_expr(jn_a[node]);
        int obj_top = jvs_top - 1;
        js_eval_expr(jn_b[node]);
        int koff; int klen;
        js_index_top_to_key(&koff, &klen);
        int tag = jvs_tag[obj_top];
        int oi = jvs_obj_idx[obj_top];
        jvs_top = obj_top;
        if (tag == JS_VAL_OBJ || tag == JS_VAL_ARR) {
            if (tag == JS_VAL_ARR && klen == 6 &&
                js_str_eq(koff, klen, js_str_intern("length", 6), 6)) {
                js_push_num((double)jobj_arr_len[oi]);
                return;
            }
            int p = js_obj_find_prop(oi, koff, klen);
            if (p >= 0) js_push_from_prop(p); else js_push_undef();
            return;
        }
        js_push_undef();
        return;
    }
    js_set_err("js: unsupported expression");
    js_push_undef();
}

/* ----- statements ----- */
void js_exec_block(int block_node) {
    if (block_node < 0) return;
    int s = jn_a[block_node];
    while (s >= 0 && js_ctrl_signal == 0 && js_last_error[0] == 0) {
        js_eval_stmt(s);
        s = jn_next[s];
    }
}

void js_eval_stmt(int node) {
    if (node < 0) return;
    if (js_last_error[0] != 0) return;
    int k = jn_kind[node];
    if (k == JS_NODE_BLOCK) { js_exec_block(node); return; }
    if (k == JS_NODE_EXPR_STMT) {
        js_eval_expr(jn_a[node]);
        js_pop();
        return;
    }
    if (k == JS_NODE_VAR_DECL) {
        int d = jn_a[node];
        while (d >= 0) {
            int o = jn_a[d]; int l = jn_b[d]; int init = jn_c[d];
            int b = js_lookup_binding(jsc_cur, o, l);
            if (b < 0) b = js_binding_alloc(jsc_cur, o, l);
            if (init >= 0 && b >= 0) {
                js_eval_expr(init);
                js_binding_set_from_top(b);
                js_pop();
            }
            d = jn_next[d];
        }
        return;
    }
    if (k == JS_NODE_IF) {
        js_eval_expr(jn_a[node]);
        int b = js_to_bool_at(jvs_top - 1);
        js_pop();
        if (b) js_eval_stmt(jn_b[node]);
        else if (jn_c[node] >= 0) js_eval_stmt(jn_c[node]);
        return;
    }
    if (k == JS_NODE_WHILE) {
        int guard = 0;
        while (guard < 100000) {
            js_eval_expr(jn_a[node]);
            int b = js_to_bool_at(jvs_top - 1);
            js_pop();
            if (!b) break;
            js_eval_stmt(jn_b[node]);
            if (js_ctrl_signal == 1) { js_ctrl_signal = 0; break; }
            if (js_ctrl_signal == 2) { js_ctrl_signal = 0; }
            if (js_ctrl_signal == 3) break;
            if (js_last_error[0] != 0) break;
            guard = guard + 1;
        }
        if (guard >= 100000) js_set_err("js: while loop iteration cap reached");
        return;
    }
    if (k == JS_NODE_FOR) {
        if (jn_a[node] >= 0) js_eval_stmt(jn_a[node]);
        int guard = 0;
        while (guard < 100000) {
            int b = 1;
            if (jn_b[node] >= 0) {
                js_eval_expr(jn_b[node]);
                b = js_to_bool_at(jvs_top - 1);
                js_pop();
            }
            if (!b) break;
            js_eval_stmt(jn_d[node]);
            if (js_ctrl_signal == 1) { js_ctrl_signal = 0; break; }
            if (js_ctrl_signal == 2) { js_ctrl_signal = 0; }
            if (js_ctrl_signal == 3) break;
            if (js_last_error[0] != 0) break;
            if (jn_c[node] >= 0) {
                js_eval_expr(jn_c[node]);
                js_pop();
            }
            guard = guard + 1;
        }
        if (guard >= 100000) js_set_err("js: for loop iteration cap reached");
        return;
    }
    if (k == JS_NODE_BREAK)    { js_ctrl_signal = 1; return; }
    if (k == JS_NODE_CONTINUE) { js_ctrl_signal = 2; return; }
    if (k == JS_NODE_RETURN) {
        if (jn_a[node] >= 0) js_eval_expr(jn_a[node]); else js_push_undef();
        js_ctrl_signal = 3;
        return;
    }
    if (k == JS_NODE_FUNC_DECL) {
        int fn = js_alloc_function(jn_c[node], jn_d[node], jsc_cur);
        if (fn < 0) return;
        int o = jn_a[node]; int l = jn_b[node];
        if (o >= 0 && l > 0) {
            int b = js_lookup_binding(jsc_cur, o, l);
            if (b < 0) b = js_binding_alloc(jsc_cur, o, l);
            if (b >= 0) {
                jb_tag[b]      = JS_VAL_FUNC;
                jb_num[b]      = 0.0;
                jb_obj_idx[b]  = fn;
            }
        }
        return;
    }
}

void js_exec_program(int root) {
    if (root < 0) return;
    /* Allocate (or re-use) the root scope. */
    if (jsc_top == 0) jsc_cur = js_scope_enter(-1);
    int s = jn_a[root];
    while (s >= 0 && js_last_error[0] == 0) {
        js_eval_stmt(s);
        if (js_ctrl_signal == 3) break;        /* top-level return is rare but safe */
        s = jn_next[s];
    }
}
