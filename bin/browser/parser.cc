/* HTML lex / parse */

/* §1 tokenizer states */
enum {
    ST_DATA = 0,
    ST_TAG_OPEN,
    ST_END_TAG_OPEN,
    ST_TAG_NAME,
    ST_BEFORE_ATTR,
    ST_ATTR_NAME,
    ST_AFTER_ATTR_NAME,
    ST_BEFORE_VALUE,
    ST_VALUE_DQ,
    ST_VALUE_SQ,
    ST_VALUE_UQ,
    ST_SELF_CLOSE,
    ST_MARKUP_DECL,    /* sees "<!" -- decide between Comment and Doctype */
    ST_COMMENT,        /* skip to "-->" */
    ST_DOCTYPE,        /* skip to ">" */
    ST_RAWTEXT,        /* used inside <script>/<style> */
    ST_RCDATA          /* used inside <title>/<textarea> */
};

int emit_token(int kind, int tag, int text_off, int text_len,
               int attr_first, int attr_count, int self_close) {
    if (tok_count >= MAX_TOKENS) return -1;
    tok_kind[tok_count]       = kind;
    tok_tag[tok_count]        = tag;
    tok_text_off[tok_count]   = text_off;
    tok_text_len[tok_count]   = text_len;
    tok_attr_first[tok_count] = attr_first;
    tok_attr_count[tok_count] = attr_count;
    tok_self_close[tok_count] = self_close;
    return tok_count++;
}

void tokenize(int html_len) {
    tok_count = 0;
    ap_count  = 0;
    int state = ST_DATA;
    int i = 0;
    int text_start = 0;
    int tag_start = 0;
    int tag_is_end = 0;
    int cur_tag = 0;            /* T_* during ST_TAG_NAME */
    int cur_attr_first = 0;
    int cur_attr_count = 0;
    int cur_self_close = 0;

    while (i <= html_len) {
        int c = (i < html_len) ? (page_buf[i] & 0xFF) : -1;

        if (state == ST_DATA) {
            if (c == '<') {
                if (i > text_start) {
                    emit_token(TK_TEXT, 0, text_start, i - text_start, 0, 0, 0);
                }
                state = ST_TAG_OPEN;
                tag_is_end = 0;
                cur_tag = 0;
                cur_attr_first = ap_count;
                cur_attr_count = 0;
                cur_self_close = 0;
                i++;
                continue;
            }
            if (c < 0) {
                if (i > text_start) {
                    emit_token(TK_TEXT, 0, text_start, i - text_start, 0, 0, 0);
                }
                emit_token(TK_EOF, 0, 0, 0, 0, 0, 0);
                return;
            }
            i++;
            continue;
        }

        if (state == ST_TAG_OPEN) {
            if (c == '/') { state = ST_END_TAG_OPEN; i++; continue; }
            if (c == '!') { state = ST_MARKUP_DECL; i++; continue; }
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                state = ST_TAG_NAME;
                tag_start = i;
                continue;            /* re-process this byte in TAG_NAME */
            }
            /* anything else: treat '<' as text, fall back */
            text_start = i - 1;
            state = ST_DATA;
            continue;
        }

        if (state == ST_END_TAG_OPEN) {
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                state = ST_TAG_NAME;
                tag_is_end = 1;
                tag_start = i;
                continue;
            }
            /* malformed; skip to '>' */
            while (i < html_len && page_buf[i] != '>') i++;
            if (i < html_len) i++;
            state = ST_DATA;
            text_start = i;
            continue;
        }

        if (state == ST_TAG_NAME) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '/' || c == '>') {
                cur_tag = tag_id(page_buf + tag_start, i - tag_start);
                state = ST_BEFORE_ATTR;
                continue;          /* let BEFORE_ATTR re-handle byte */
            }
            i++;
            continue;
        }

        if (state == ST_BEFORE_ATTR) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }
            if (c == '/') { cur_self_close = 1; i++; continue; }
            if (c == '>') {
                emit_token(tag_is_end ? TK_END : TK_START, cur_tag,
                           0, 0, cur_attr_first, cur_attr_count, cur_self_close);
                if (!tag_is_end) {
                    if (cur_tag == T_SCRIPT || cur_tag == T_STYLE) {
                        state = ST_RAWTEXT;
                    } else if (cur_tag == T_TITLE || cur_tag == T_TEXTAREA) {
                        state = ST_RCDATA;
                    } else {
                        state = ST_DATA;
                    }
                } else {
                    state = ST_DATA;
                }
                i++;
                text_start = i;
                continue;
            }
            /* attribute name starts here */
            state = ST_ATTR_NAME;
            tag_start = i;
            continue;
        }

        if (state == ST_ATTR_NAME) {
            if (c == '=') {
                int name_off = attr_intern(page_buf + tag_start, i - tag_start);
                if (ap_count < MAX_ATTR_PAIRS) {
                    ap_name_off[ap_count]  = name_off;
                    ap_value_off[ap_count] = -1;       /* filled at value end */
                    ap_count++;
                    cur_attr_count++;
                }
                state = ST_BEFORE_VALUE;
                i++; continue;
            }
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                c == '/' || c == '>') {
                /* boolean attribute (no '=') */
                int name_off = attr_intern(page_buf + tag_start, i - tag_start);
                if (ap_count < MAX_ATTR_PAIRS) {
                    ap_name_off[ap_count]  = name_off;
                    ap_value_off[ap_count] = attr_intern("", 0);
                    ap_count++;
                    cur_attr_count++;
                }
                state = ST_BEFORE_ATTR;
                continue;
            }
            i++;
            continue;
        }

        if (state == ST_BEFORE_VALUE) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }
            if (c == '"')  { state = ST_VALUE_DQ; i++; tag_start = i; continue; }
            if (c == '\'') { state = ST_VALUE_SQ; i++; tag_start = i; continue; }
            state = ST_VALUE_UQ; tag_start = i; continue;
        }

        if (state == ST_VALUE_DQ || state == ST_VALUE_SQ) {
            int term = (state == ST_VALUE_DQ) ? '"' : '\'';
            if (c == term) {
                int dec_max = (i - tag_start) + 1;
                /* fast path: no '&' -> intern directly */
                int has_amp = 0;
                for (int k = tag_start; k < i; k++) {
                    if (page_buf[k] == '&') { has_amp = 1; break; }
                }
                int voff;
                if (!has_amp) {
                    voff = attr_intern(page_buf + tag_start, i - tag_start);
                } else {
                    int dec_len = decode_entities(page_buf + tag_start, i - tag_start,
                                                  ctype_buf, dec_max < 4096 ? dec_max : 4096);
                    voff = attr_intern(ctype_buf, dec_len);
                }
                if (ap_count > 0) ap_value_off[ap_count - 1] = voff;
                state = ST_BEFORE_ATTR;
                i++; continue;
            }
            i++; continue;
        }

        if (state == ST_VALUE_UQ) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                c == '>' || c < 0) {
                int has_amp = 0;
                for (int k = tag_start; k < i; k++) {
                    if (page_buf[k] == '&') { has_amp = 1; break; }
                }
                int voff;
                if (!has_amp) {
                    voff = attr_intern(page_buf + tag_start, i - tag_start);
                } else {
                    int dec_max = (i - tag_start) + 1;
                    int dec_len = decode_entities(page_buf + tag_start, i - tag_start,
                                                  ctype_buf, dec_max < 4096 ? dec_max : 4096);
                    voff = attr_intern(ctype_buf, dec_len);
                }
                if (ap_count > 0) ap_value_off[ap_count - 1] = voff;
                state = ST_BEFORE_ATTR;
                continue;
            }
            i++; continue;
        }

        if (state == ST_MARKUP_DECL) {
            /* If next two bytes are "--", it's a comment; otherwise treat as DOCTYPE-like */
            if (i + 1 < html_len && page_buf[i] == '-' && page_buf[i+1] == '-') {
                state = ST_COMMENT;
                i += 2;
                continue;
            }
            state = ST_DOCTYPE;
            continue;
        }

        if (state == ST_COMMENT) {
            /* skip until '-->' */
            while (i + 2 < html_len &&
                   !(page_buf[i] == '-' && page_buf[i+1] == '-' && page_buf[i+2] == '>')) {
                i++;
            }
            if (i + 2 < html_len) i += 3; else i = html_len;
            state = ST_DATA;
            text_start = i;
            continue;
        }

        if (state == ST_DOCTYPE) {
            while (i < html_len && page_buf[i] != '>') i++;
            if (i < html_len) i++;
            state = ST_DATA;
            text_start = i;
            continue;
        }

        if (state == ST_RAWTEXT || state == ST_RCDATA) {
            /* find matching </tag> for the most recently emitted START. */
            int last_start = tok_count - 1;
            while (last_start >= 0 && tok_kind[last_start] != TK_START) last_start--;
            int close_tag = (last_start >= 0) ? tok_tag[last_start] : 0;
            char *close_name = 0;
            int close_name_len = 0;
            if (close_tag == T_SCRIPT)        { close_name = "script";   close_name_len = 6; }
            else if (close_tag == T_STYLE)    { close_name = "style";    close_name_len = 5; }
            else if (close_tag == T_TITLE)    { close_name = "title";    close_name_len = 5; }
            else if (close_tag == T_TEXTAREA) { close_name = "textarea"; close_name_len = 8; }
            else { state = ST_DATA; continue; }

            int t_start = i;
            while (i < html_len) {
                if (page_buf[i] == '<' && i + 1 + close_name_len < html_len &&
                    page_buf[i+1] == '/' &&
                    b_strieq_n(page_buf + i + 2, close_name, close_name_len) &&
                    (page_buf[i + 2 + close_name_len] == '>' ||
                     page_buf[i + 2 + close_name_len] == ' ' ||
                     page_buf[i + 2 + close_name_len] == '\t' ||
                     page_buf[i + 2 + close_name_len] == '\n')) break;
                i++;
            }
            int t_end = i;

            if (t_end > t_start) {
                if (state == ST_RCDATA) {
                    int dec_len = decode_entities(page_buf + t_start, t_end - t_start,
                                                  ctype_buf, 4096);
                    int off = attr_intern(ctype_buf, dec_len);
                    /* Sentinel bit 0x40000000 on tok_text_len marks attr_pool-backed text. */
                    emit_token(TK_TEXT, 0, off,
                               dec_len | 0x40000000, 0, 0, 0);
                } else {
                    emit_token(TK_TEXT, 0, t_start, t_end - t_start, 0, 0, 0);
                }
            }

            /* skip past "</closename" and any whitespace/'>' */
            if (i < html_len) {
                emit_token(TK_END, close_tag, 0, 0, 0, 0, 0);
                i += 2 + close_name_len;
                while (i < html_len && page_buf[i] != '>') i++;
                if (i < html_len) i++;
            }
            state = ST_DATA;
            text_start = i;
            continue;
        }

        /* catch-all fallback: skip to '>' and resume in DATA */
        while (i < html_len && page_buf[i] != '>') i++;
        if (i < html_len) i++;
        state = ST_DATA;
        text_start = i;
    }
}

int tag_id(char *name, int len) {
    if (len == 0) return T_OTHER;
    char b[16]; int i = 0;
    while (i < len && i < 15) { b[i] = (char)b_lc(name[i]); i = i + 1; }
    b[i] = 0;
    if (b_streq(b, "html"))   return T_HTML;
    if (b_streq(b, "head"))   return T_HEAD;
    if (b_streq(b, "title"))  return T_TITLE;
    if (b_streq(b, "body"))   return T_BODY;
    if (b_streq(b, "p"))      return T_P;
    if (b_streq(b, "h1"))     return T_H1;
    if (b_streq(b, "h2"))     return T_H2;
    if (b_streq(b, "h3"))     return T_H3;
    if (b_streq(b, "h4"))     return T_H4;
    if (b_streq(b, "h5"))     return T_H5;
    if (b_streq(b, "h6"))     return T_H6;
    if (b_streq(b, "a"))      return T_A;
    if (b_streq(b, "br"))     return T_BR;
    if (b_streq(b, "hr"))     return T_HR;
    if (b_streq(b, "ul"))     return T_UL;
    if (b_streq(b, "ol"))     return T_OL;
    if (b_streq(b, "li"))     return T_LI;
    if (b_streq(b, "pre"))    return T_PRE;
    if (b_streq(b, "code"))   return T_CODE;
    if (b_streq(b, "b"))      return T_B;
    if (b_streq(b, "strong")) return T_STRONG;
    if (b_streq(b, "i"))      return T_I;
    if (b_streq(b, "em"))     return T_EM;
    if (b_streq(b, "span"))   return T_SPAN;
    if (b_streq(b, "div"))    return T_DIV;
    if (b_streq(b, "img"))    return T_IMG;
    if (b_streq(b, "form"))   return T_FORM;
    if (b_streq(b, "input"))  return T_INPUT;
    if (b_streq(b, "button")) return T_BUTTON;
    if (b_streq(b, "font"))   return T_FONT;
    if (b_streq(b, "script")) return T_SCRIPT;
    if (b_streq(b, "style"))  return T_STYLE;
    if (b_streq(b, "noscript")) return T_NOSCRIPT;
    if (b_streq(b, "textarea")) return T_TEXTAREA;
    if (b_streq(b, "dt"))       return T_DT;
    if (b_streq(b, "dd"))       return T_DD;
    if (b_streq(b, "dl"))       return T_DL;
    if (b_streq(b, "thead"))    return T_THEAD;
    if (b_streq(b, "tbody"))    return T_TBODY;
    if (b_streq(b, "tfoot"))    return T_TFOOT;
    if (b_streq(b, "caption"))  return T_CAPTION;
    if (b_streq(b, "option"))   return T_OPTION;
    if (b_streq(b, "header"))   return T_HEADER;
    if (b_streq(b, "footer"))   return T_FOOTER;
    if (b_streq(b, "nav"))      return T_NAV;
    if (b_streq(b, "section"))  return T_SECTION;
    if (b_streq(b, "article"))  return T_ARTICLE;
    if (b_streq(b, "aside"))    return T_ASIDE;
    if (b_streq(b, "main"))     return T_MAIN;
    if (b_streq(b, "table"))    return T_TABLE;
    if (b_streq(b, "tr"))       return T_TR;
    if (b_streq(b, "td"))       return T_TD;
    if (b_streq(b, "th"))       return T_TH;
    if (b_streq(b, "blockquote")) return T_BLOCKQUOTE;
    return T_OTHER;
}

int is_void_tag(int tag) {
    return tag == T_BR || tag == T_HR || tag == T_IMG || tag == T_INPUT;
}

int is_block_tag(int tag) {
    return tag == T_P || tag == T_DIV || tag == T_H1 || tag == T_H2 ||
           tag == T_H3 || tag == T_H4 || tag == T_H5 || tag == T_H6 ||
           tag == T_UL || tag == T_OL || tag == T_LI || tag == T_PRE ||
           tag == T_BLOCKQUOTE || tag == T_HR || tag == T_TABLE ||
           tag == T_FORM || tag == T_HEADER || tag == T_FOOTER ||
           tag == T_NAV || tag == T_SECTION || tag == T_ARTICLE ||
           tag == T_ASIDE || tag == T_MAIN;
}

int is_list_container(int tag) {
    return tag == T_UL || tag == T_OL || tag == T_DL;
}

/* Build DOM by consuming the token stream produced by tokenize().
 * Tree builder uses an open-elements stack with simplified insertion modes
 * to drive implicit-close rules per spec §1.
 *
 * Note: attr_pool_pos is NOT reset here because tokenize() has already
 * interned attr names/values into attr_pool; resetting would invalidate
 * those offsets. Whole-page reset of attr_pool_pos happens in error_page()
 * and per-navigation in navigate() before tokenize() starts. */
void parse_html(int html_len) {
    tokenize(html_len);
    serial_printf("[browser] tokenize: %d tokens, %d attr-pairs\n",
                  tok_count, ap_count);

    nodes_count = 0;
    dom_ap_count = 0;
    forms_count = 0;
    inputs_count = 0;
    title_buf[0] = 0;

    /* §2 reset CSS state - author rules accumulate per page */
    css_rule_count = 0;
    css_sel_count = 0;
    css_value_pool_pos = 0;
    css_has_dynamic_pseudo = 0;

    /* synthetic root */
    int root = alloc_node(T_ROOT, -1, -1);

    int stack[64];
    int sp = 0;
    stack[sp] = root;
    sp = sp + 1;

    int IM_INITIAL = 0;
    int IM_IN_HEAD = 1;
    int IM_IN_BODY = 2;
    int IM_IN_TABLE = 3;
    int IM_IN_ROW = 4;
    int IM_IN_CELL = 5;
    int IM_IN_CAPTION = 6;
    int mode = IM_INITIAL;

    for (int ti = 0; ti < tok_count; ti = ti + 1) {
        int kind = tok_kind[ti];
        int tag  = tok_tag[ti];

        if (kind == TK_EOF) break;

        if (kind == TK_TEXT) {
            int parent = stack[sp - 1];
            int len_field = tok_text_len[ti];
            int from_attr = (len_field >> 30) & 1;
            int real_len  = len_field & 0x3FFFFFFF;
            int text_off;
            int text_len;
            if (from_attr) {
                /* RCDATA: bytes already in attr_pool, decoded by tokenizer */
                text_off = tok_text_off[ti];
                text_len = real_len;
            } else {
                /* DATA: decode entities into ctype_buf, then intern */
                int dec_max = real_len + 1;
                if (dec_max > 4096) dec_max = 4096;
                int dec_len = decode_entities(page_buf + tok_text_off[ti],
                                              real_len, ctype_buf, dec_max);
                text_off = attr_intern(ctype_buf, dec_len);
                text_len = dec_len;
            }
            if (text_off < 0) continue;
            /* whitespace check */
            int ws_only = 1;
            for (int k = 0; k < text_len; k = k + 1) {
                char c = attr_pool[text_off + k];
                if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                    ws_only = 0; break;
                }
            }
            /* In <table> outside cells, foster-parent non-whitespace text:
             * insert as the previous sibling of the table. Per HTML5 spec
             * §13.2.6.5, stray inline content inside a table doesn't belong
             * inside any cell; it belongs *before* the table in its parent. */
            if (mode == IM_IN_TABLE && !ws_only) {
                int table_idx = -1;
                for (int k = sp - 1; k >= 1; k = k - 1) {
                    if (n_tag[stack[k]] == T_TABLE) { table_idx = stack[k]; break; }
                }
                if (table_idx < 0) continue;
                int table_parent = n_parent[table_idx];
                if (table_parent < 0) continue;
                int fn = alloc_node(T_TEXT, table_parent, -1);
                if (fn < 0) continue;
                n_text_off[fn] = text_off;
                n_text_len[fn] = text_len;
                dom_insert_before(fn, table_idx);
                continue;
            }
            /* skip whitespace-only text nodes outside <pre>/<title> */
            if (ws_only) {
                int parent_tag = (parent >= 0) ? n_tag[parent] : -1;
                if (parent_tag != T_PRE && parent_tag != T_CODE &&
                    parent_tag != T_TITLE) {
                    continue;
                }
            }
            /* preserve title-buf capture: first text inside <title> */
            int parent_tag = (parent >= 0) ? n_tag[parent] : -1;
            if (parent_tag == T_TITLE && title_buf[0] == 0) {
                int copy = text_len;
                if (copy > 255) copy = 255;
                for (int k = 0; k < copy; k = k + 1) {
                    title_buf[k] = attr_pool[text_off + k];
                }
                title_buf[copy] = 0;
            }
            /* §2 author CSS: feed text inside <style> into the rule table
             * and drop the text node - CSS does not render. */
            if (parent_tag == T_STYLE) {
                css_parse_block(attr_pool + text_off, text_len);
                continue;
            }
            /* §7 inline <script>: queue the source for execution after
             * the DOM and render tree are built (DOMContentLoaded-ish
             * semantics). The text node itself is dropped from the DOM
             * via display:none on T_SCRIPT, but we must not emit it as
             * visible text either. */
            if (parent_tag == T_SCRIPT) {
                js_queue_script(text_off, text_len);
                continue;
            }
            int n = alloc_node(T_TEXT, parent, -1);
            if (n < 0) continue;
            n_text_off[n] = text_off;
            n_text_len[n] = text_len;
            continue;
        }

        if (kind == TK_START) {
            /* implicit-close rules */
            if (tag == T_P) {
                /* close any currently-open <p> */
                while (sp > 1 && n_tag[stack[sp - 1]] != T_P) {
                    if (is_block_tag(n_tag[stack[sp - 1]])) break;
                    sp = sp - 1;
                }
                if (sp > 1 && n_tag[stack[sp - 1]] == T_P) sp = sp - 1;
            } else if (is_block_tag(tag)) {
                /* close <p> if open as direct ancestor */
                if (sp > 1 && n_tag[stack[sp - 1]] == T_P) sp = sp - 1;
            } else if (tag == T_LI) {
                while (sp > 1 && n_tag[stack[sp - 1]] != T_LI &&
                       !is_list_container(n_tag[stack[sp - 1]])) sp = sp - 1;
                if (sp > 1 && n_tag[stack[sp - 1]] == T_LI) sp = sp - 1;
            } else if (tag == T_DT || tag == T_DD) {
                while (sp > 1 &&
                       n_tag[stack[sp - 1]] != T_DT &&
                       n_tag[stack[sp - 1]] != T_DD &&
                       n_tag[stack[sp - 1]] != T_DL) sp = sp - 1;
                if (sp > 1 &&
                    (n_tag[stack[sp - 1]] == T_DT ||
                     n_tag[stack[sp - 1]] == T_DD)) sp = sp - 1;
            } else if (tag == T_TR) {
                while (sp > 1 && n_tag[stack[sp - 1]] != T_TR &&
                       n_tag[stack[sp - 1]] != T_TABLE &&
                       n_tag[stack[sp - 1]] != T_THEAD &&
                       n_tag[stack[sp - 1]] != T_TBODY &&
                       n_tag[stack[sp - 1]] != T_TFOOT) sp = sp - 1;
                if (sp > 1 && n_tag[stack[sp - 1]] == T_TR) sp = sp - 1;
            } else if (tag == T_TD || tag == T_TH) {
                while (sp > 1 &&
                       n_tag[stack[sp - 1]] != T_TD &&
                       n_tag[stack[sp - 1]] != T_TH &&
                       n_tag[stack[sp - 1]] != T_TR) sp = sp - 1;
                if (sp > 1 &&
                    (n_tag[stack[sp - 1]] == T_TD ||
                     n_tag[stack[sp - 1]] == T_TH)) sp = sp - 1;
            } else if (tag == T_OPTION) {
                if (sp > 1 && n_tag[stack[sp - 1]] == T_OPTION) sp = sp - 1;
            } else if (tag == T_A) {
                /* close any open <a> (avoid nesting) */
                if (sp > 1 && n_tag[stack[sp - 1]] == T_A) sp = sp - 1;
            } else if (tag == T_BODY) {
                /* close <head> if currently open as direct ancestor */
                if (sp > 1 && n_tag[stack[sp - 1]] == T_HEAD) sp = sp - 1;
            }

            int parent = stack[sp - 1];
            int n = alloc_node(tag, parent, ti);
            if (n < 0) continue;

            /* mode transitions */
            if (tag == T_TABLE)        mode = IM_IN_TABLE;
            else if (tag == T_TR)      mode = IM_IN_ROW;
            else if (tag == T_TD || tag == T_TH) mode = IM_IN_CELL;
            else if (tag == T_CAPTION) mode = IM_IN_CAPTION;
            else if (tag == T_BODY)    mode = IM_IN_BODY;
            else if (tag == T_HEAD)    mode = IM_IN_HEAD;

            /* push unless void or self-closing */
            if (!is_void_tag(tag) && !tok_self_close[ti]) {
                if (sp < 64) {
                    stack[sp] = n;
                    sp = sp + 1;
                }
            }

            /* (link registration is layout's job - see register_link in
             * layout.cc; layout resets links_count and rebuilds during
             * each pass.) */

            /* register forms */
            if (tag == T_FORM && forms_count < MAX_FORMS) {
                int a_off = dom_attr_get(n, "action");
                form_action[forms_count] = a_off;
                form_node[forms_count]   = n;
                forms_count = forms_count + 1;
            }

            /* register inputs (text-like only - submit/button/hidden are not
             * part of the editable inputs[] table, but their DOM node is
             * still kept so layout can render them). */
            if (tag == T_INPUT) {
                char *type_s = dom_attr_str(n, "type");
                int is_text = 1;
                if (type_s) {
                    if (b_strieq(type_s, "submit") ||
                        b_strieq(type_s, "button") ||
                        b_strieq(type_s, "hidden") ||
                        b_strieq(type_s, "reset") ||
                        b_strieq(type_s, "image") ||
                        b_strieq(type_s, "file")  ||
                        b_strieq(type_s, "checkbox") ||
                        b_strieq(type_s, "radio")) is_text = 0;
                }
                if (is_text && inputs_count < MAX_INPUTS) {
                    int ii = inputs_count;
                    int v_off = dom_attr_get(n, "value");
                    int n_off = dom_attr_get(n, "name");
                    if (v_off >= 0) {
                        char *v = attr_pool + v_off;
                        int len = b_strlen(v);
                        if (len > 127) len = 127;
                        for (int k = 0; k < len; k = k + 1) {
                            input_value[ii * 128 + k] = v[k];
                        }
                        input_value[ii * 128 + len] = 0;
                    } else {
                        input_value[ii * 128] = 0;
                    }
                    input_name_off[ii] = n_off;
                    input_form[ii]     = (forms_count > 0) ? forms_count - 1 : -1;
                    input_node[ii]     = n;
                    inputs_count = inputs_count + 1;
                }
            }

            continue;
        }

        if (kind == TK_END) {
            /* close until matching open tag */
            int found = -1;
            for (int k = sp - 1; k >= 1; k = k - 1) {
                if (n_tag[stack[k]] == tag) { found = k; break; }
            }
            if (found < 0) continue;

            /* Simplified adoption-agency: any formatting elements (b/i/em/
             * strong/font/u/s) on the stack ABOVE the matched end tag are
             * still active per spec - the misnest <b><i>x</b> closes b but
             * keeps i open, with subsequent content wrapped in a fresh i.
             * We capture the tags above the match, pop to it, then re-open
             * each captured tag as a new sibling under the new top. Limited
             * to 8 saved tags - deeper formatting nests fall through. */
            int saved[8];
            int n_saved = 0;
            for (int k = found + 1; k < sp && n_saved < 8; k = k + 1) {
                int t = n_tag[stack[k]];
                if (t == T_B || t == T_I || t == T_EM || t == T_STRONG ||
                    t == T_FONT) {
                    saved[n_saved] = t;
                    n_saved = n_saved + 1;
                }
            }
            sp = found;
            for (int k = 0; k < n_saved && sp < 64; k = k + 1) {
                int new_parent = stack[sp - 1];
                int nn = alloc_node(saved[k], new_parent, -1);
                if (nn < 0) break;
                stack[sp] = nn;
                sp = sp + 1;
            }

            /* mode transitions on close */
            if (tag == T_TABLE)        mode = IM_IN_BODY;
            else if (tag == T_TR)      mode = IM_IN_TABLE;
            else if (tag == T_TD || tag == T_TH) mode = IM_IN_ROW;
            else if (tag == T_CAPTION) mode = IM_IN_TABLE;
            continue;
        }
    }

    serial_printf("[browser] css: %d rules, %d sels, %d val-bytes\n",
                  css_rule_count, css_sel_count, css_value_pool_pos);

    style_resolve_all();
    serial_printf("[browser] style: %d computed entries\n", cs_count);

    build_render_tree();
    serial_printf("[browser] rt: %d nodes\n", rt_count);

    /* §7 run queued <script> blocks now that the DOM, computed styles,
     * and render tree exist. js_install_globals() exposes window /
     * document / document.body / document.getElementById. After all
     * scripts run, if any of them set dom_dirty, re-run style + layout
     * once so visible state reflects the mutation (one reflow per task). */
    if (js_script_count > 0) {
        serial_printf("[browser] js: running %d queued scripts\n", js_script_count);
        js_install_globals();
        js_run_queued_scripts();
        if (dom_dirty) {
            style_resolve_all();
            build_render_tree();
            run_layout();
            dom_dirty = 0;
        }
    }
}
