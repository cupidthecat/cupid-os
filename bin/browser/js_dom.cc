/* §7 JS DOM bindings. Phase 2a deliverables:
 *   - window / document globals
 *   - document.body          (DOMNODE)
 *   - document.getElementById(id)         -> DOMNODE
 *   - element.textContent / innerText     (read)
 *   - element.tagName                     (read)
 *
 * DOMNODE values carry an int dom_idx in jvs_dom_idx. Property reads
 * on a DOMNODE go through jsd_dom_member_get; method calls (e.g.
 * getElementById) are surfaced as NATIVE function values so the
 * existing CALL machinery stays uniform.
 *
 * F2b adds setters (textContent / innerHTML), F2c adds createElement
 * and tree mutation, F2d adds attribute / style proxies, F2e adds
 * querySelector + location.href.
 */

void js_push_domnode(int dom_idx) {
    js_push_undef();
    int t = jvs_top - 1;
    jvs_tag[t] = JS_VAL_DOMNODE;
    jvs_dom_idx[t] = dom_idx;
}

void js_push_native(int native_id) {
    js_push_undef();
    int t = jvs_top - 1;
    jvs_tag[t] = JS_VAL_NATIVE;
    jvs_native_id[t] = native_id;
}

/* Find the first DOM node whose tag matches; -1 if none. Used by
 * jsd_get_body() and similar lazy lookups. */
int jsd_find_first_by_tag(int tag) {
    for (int n = 0; n < nodes_count; n++) {
        if (n_tag[n] == tag) return n;
    }
    return -1;
}

int jsd_get_body() { return jsd_find_first_by_tag(T_BODY); }

/* Walk the subtree rooted at `n` and append the concatenated text of
 * all descendant text nodes into buf (max bytes). Whitespace inside
 * text nodes is preserved per the (already-tokenized) attr_pool
 * content. */
int jsd_collect_text(int n, char *buf, int max) {
    int p = 0;
    int stack[256];
    int sp = 0;
    stack[sp] = n; sp = sp + 1;
    while (sp > 0) {
        sp = sp - 1;
        int cur = stack[sp];
        if (n_tag[cur] == T_TEXT) {
            int off = n_text_off[cur];
            int ln = n_text_len[cur];
            for (int i = 0; i < ln && p < max - 1; i++) {
                buf[p] = attr_pool[off + i];
                p = p + 1;
            }
        }
        /* Push children in reverse so iteration matches DOM order. */
        int kids[64]; int kn = 0;
        int c = n_first_child[cur];
        while (c >= 0 && kn < 64) { kids[kn] = c; kn = kn + 1; c = n_next[c]; }
        for (int i = kn - 1; i >= 0; i--) {
            if (sp < 256) { stack[sp] = kids[i]; sp = sp + 1; }
        }
    }
    buf[p] = 0;
    return p;
}

/* Copy a tag enum back to its name (lower-case ASCII) into buf. Used
 * by element.tagName. */
int jsd_tag_name(int tag, char *buf, int max) {
    char *s = "?";
    if (tag == T_HTML) s = "HTML";
    else if (tag == T_HEAD) s = "HEAD";
    else if (tag == T_BODY) s = "BODY";
    else if (tag == T_TITLE) s = "TITLE";
    else if (tag == T_DIV) s = "DIV";
    else if (tag == T_P) s = "P";
    else if (tag == T_SPAN) s = "SPAN";
    else if (tag == T_A) s = "A";
    else if (tag == T_IMG) s = "IMG";
    else if (tag == T_H1) s = "H1";
    else if (tag == T_H2) s = "H2";
    else if (tag == T_H3) s = "H3";
    else if (tag == T_H4) s = "H4";
    else if (tag == T_H5) s = "H5";
    else if (tag == T_H6) s = "H6";
    else if (tag == T_UL) s = "UL";
    else if (tag == T_OL) s = "OL";
    else if (tag == T_LI) s = "LI";
    else if (tag == T_TABLE) s = "TABLE";
    else if (tag == T_TR) s = "TR";
    else if (tag == T_TD) s = "TD";
    else if (tag == T_TH) s = "TH";
    else if (tag == T_FORM) s = "FORM";
    else if (tag == T_INPUT) s = "INPUT";
    else if (tag == T_BUTTON) s = "BUTTON";
    else if (tag == T_TEXT) s = "#text";
    int i = 0;
    while (s[i] && i < max - 1) { buf[i] = s[i]; i = i + 1; }
    buf[i] = 0;
    return i;
}

/* Read a property of a DOMNODE; pushes the resulting value. */
void jsd_dom_member_get(int dom_idx, int koff, int klen) {
    if (dom_idx < 0) { js_push_undef(); return; }
    char *name = js_str_pool + koff;
    if (klen == 11 && b_strieq_n(name, "textContent", 11)) {
        char buf[1024];
        int n = jsd_collect_text(dom_idx, buf, 1024);
        int off = js_str_intern(buf, n);
        js_push_str(off, n);
        return;
    }
    if (klen == 9 && b_strieq_n(name, "innerText", 9)) {
        char buf[1024];
        int n = jsd_collect_text(dom_idx, buf, 1024);
        int off = js_str_intern(buf, n);
        js_push_str(off, n);
        return;
    }
    if (klen == 7 && b_strieq_n(name, "tagName", 7)) {
        char buf[32];
        int n = jsd_tag_name(n_tag[dom_idx], buf, 32);
        int off = js_str_intern(buf, n);
        js_push_str(off, n);
        return;
    }
    if (klen == 2 && b_strieq_n(name, "id", 2)) {
        int idoff = dom_id_off[dom_idx];
        if (idoff < 0) { js_push_str(js_str_intern("", 0), 0); return; }
        char *idv = attr_pool + idoff;
        int idlen = b_strlen(idv);
        int o = js_str_intern(idv, idlen);
        js_push_str(o, idlen);
        return;
    }
    if (klen == 9 && b_strieq_n(name, "className", 9)) {
        int co = dom_class_off[dom_idx];
        if (co < 0) { js_push_str(js_str_intern("", 0), 0); return; }
        char *cv = attr_pool + co;
        int cl = b_strlen(cv);
        int o = js_str_intern(cv, cl);
        js_push_str(o, cl);
        return;
    }
    if (klen == 10 && b_strieq_n(name, "parentNode", 10)) {
        int p = n_parent[dom_idx];
        if (p < 0) js_push_null(); else js_push_domnode(p);
        return;
    }
    if (klen == 10 && b_strieq_n(name, "firstChild", 10)) {
        int c = n_first_child[dom_idx];
        if (c < 0) js_push_null(); else js_push_domnode(c);
        return;
    }
    if (klen == 11 && b_strieq_n(name, "nextSibling", 11)) {
        int s = n_next[dom_idx];
        if (s < 0) js_push_null(); else js_push_domnode(s);
        return;
    }
    /* Method callees - return a NATIVE value so eval_call dispatches. */
    if (klen == 12 && b_strieq_n(name, "getAttribute", 12)) {
        js_push_native(JS_NATIVE_EL_GET_ATTRIBUTE); return;
    }
    if (klen == 12 && b_strieq_n(name, "setAttribute", 12)) {
        js_push_native(JS_NATIVE_EL_SET_ATTRIBUTE); return;
    }
    if (klen == 11 && b_strieq_n(name, "appendChild", 11)) {
        js_push_native(JS_NATIVE_EL_APPEND_CHILD); return;
    }
    if (klen == 6 && b_strieq_n(name, "remove", 6)) {
        js_push_native(JS_NATIVE_EL_REMOVE); return;
    }
    js_push_undef();
}

/* Mark all of dom_idx's text-node children as inert (T_OTHER + no
 * text) so a subsequent textContent set / appendChild starts clean.
 * Pool slots are never freed; we just unlink from the parent's child
 * list. Used by jsd_dom_set_text_content. */
void jsd_clear_children(int dom_idx) {
    n_first_child[dom_idx] = -1;
}

/* Allocate a fresh T_TEXT node, intern the string into attr_pool,
 * attach as the only child of parent. Returns the new node index or
 * -1 on failure. */
int jsd_make_text_child(int parent, char *s, int len) {
    int n = alloc_node(T_TEXT, parent, -1);
    if (n < 0) return -1;
    int off = attr_intern(s, len);
    if (off < 0) return -1;
    n_text_off[n] = off;
    n_text_len[n] = len;
    return n;
}

/* Write a property on a DOMNODE; reads top-of-stack as the rvalue.
 * F2b implements textContent / innerText. F2d adds attribute / style. */
void jsd_dom_member_set(int dom_idx, int koff, int klen) {
    if (dom_idx < 0) return;
    char *name = js_str_pool + koff;
    int t = jvs_top - 1;
    if ((klen == 11 && b_strieq_n(name, "textContent", 11)) ||
        (klen == 9  && b_strieq_n(name, "innerText", 9))) {
        char buf[1024];
        int n = js_to_string_at(t, buf, 1024);
        jsd_clear_children(dom_idx);
        jsd_make_text_child(dom_idx, buf, n);
        dom_dirty = 1;
        return;
    }
    if (klen == 9 && b_strieq_n(name, "innerHTML", 9)) {
        /* F2b minimal: treat innerHTML like textContent (no tag
         * parsing). A real innerHTML re-enters the HTML tokenizer/
         * tree-builder against a sub-tree; the existing parser uses
         * file-scope globals so a recursive entry would corrupt the
         * outer parse. Deferred until the parser is refactored. */
        char buf[1024];
        int n = js_to_string_at(t, buf, 1024);
        jsd_clear_children(dom_idx);
        jsd_make_text_child(dom_idx, buf, n);
        dom_dirty = 1;
        return;
    }
    /* Unknown property - silently ignored for now. */
}

/* document.getElementById(s) - walks DOM, matches dom_id_off entries. */
void jsd_doc_get_element_by_id(int argc) {
    if (argc < 1) { js_push_null(); return; }
    int t = jvs_top - argc;
    char want[128];
    int wn = js_to_string_at(t, want, 128);
    int found = -1;
    for (int n = 0; n < nodes_count; n++) {
        int idoff = dom_id_off[n];
        if (idoff < 0) continue;
        char *idv = attr_pool + idoff;
        if (b_strieq_n(idv, want, wn) && idv[wn] == 0) { found = n; break; }
    }
    if (found < 0) js_push_null(); else js_push_domnode(found);
}

/* Native dispatch entry point invoked by eval_call. The native
 * function id is known; argc args sit at [jvs_top-argc .. jvs_top-1]. */
void js_native_call(int native_id, int argc) {
    int saved = jvs_top - argc;
    int receiver_tag = jsd_this_tag;
    int receiver_dom = jsd_this_dom_idx;
    if (native_id == JS_NATIVE_DOC_GET_ELEMENT_BY_ID) {
        jsd_doc_get_element_by_id(argc);
        /* Result on top; drop args. */
        int rt = jvs_top - 1;
        int new_tag = jvs_tag[rt];
        int new_dom = jvs_dom_idx[rt];
        int new_off = jvs_str_off[rt];
        int new_len = jvs_str_len[rt];
        double new_num = jvs_num[rt];
        int new_obj = jvs_obj_idx[rt];
        jvs_top = saved;
        js_push_undef();
        int t = jvs_top - 1;
        jvs_tag[t]     = new_tag;
        jvs_dom_idx[t] = new_dom;
        jvs_num[t]     = new_num;
        jvs_str_off[t] = new_off;
        jvs_str_len[t] = new_len;
        jvs_obj_idx[t] = new_obj;
        return;
    }
    /* unknown native - drop args, push undefined */
    (void)receiver_tag; (void)receiver_dom;
    jvs_top = saved;
    js_push_undef();
}

/* Build the document/window globals at the start of script execution.
 * Called from parser.cc after the render tree is built and just before
 * running queued scripts so document.body has a valid DOM index. */
void js_install_globals() {
    /* Ensure root scope exists. */
    if (jsc_top == 0) jsc_cur = js_scope_enter(-1);

    /* document = { body: <body-domnode>, getElementById: <native> } */
    int doc = js_alloc_object(0);
    if (doc >= 0) {
        int body = jsd_get_body();
        if (body >= 0) {
            js_push_domnode(body);
            int koff = js_str_intern("body", 4);
            js_obj_set_prop_from_top(doc, koff, 4);
            js_pop();
        }
        js_push_native(JS_NATIVE_DOC_GET_ELEMENT_BY_ID);
        int koff = js_str_intern("getElementById", 14);
        js_obj_set_prop_from_top(doc, koff, 14);
        js_pop();

        int b = js_lookup_binding(0, js_str_intern("document", 8), 8);
        if (b < 0) b = js_binding_alloc(0, js_str_intern("document", 8), 8);
        if (b >= 0) {
            jb_tag[b]     = JS_VAL_OBJ;
            jb_obj_idx[b] = doc;
        }
    }

    /* window: a plain object alias to a global namespace - empty for now. */
    int win_obj = js_alloc_object(0);
    if (win_obj >= 0) {
        int b = js_lookup_binding(0, js_str_intern("window", 6), 6);
        if (b < 0) b = js_binding_alloc(0, js_str_intern("window", 6), 6);
        if (b >= 0) {
            jb_tag[b]     = JS_VAL_OBJ;
            jb_obj_idx[b] = win_obj;
        }
    }
}
