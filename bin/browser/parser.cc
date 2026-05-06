/* ---------- HTML lex / parse ---------- */

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
    return T_OTHER;
}

int is_void_tag(int tag) {
    return tag == T_BR || tag == T_HR || tag == T_IMG || tag == T_INPUT;
}

/* Skip until matching close tag like </script> (case-insensitive).
 * Returns new index past close tag. */
int skip_to_close(char *html, int n, int i, char *closetag) {
    int cl = b_strlen(closetag);
    while (i < n - cl - 2) {
        if (html[i] == '<' && html[i+1] == '/' &&
            b_strieq_n(html + i + 2, closetag, cl) &&
            (html[i + 2 + cl] == '>' || html[i + 2 + cl] == ' ')) {
            /* advance past tag */
            while (i < n && html[i] != '>') i = i + 1;
            if (i < n) i = i + 1;
            return i;
        }
        i = i + 1;
    }
    return n;
}

/* Build DOM from page_buf into nodes. */
void parse_html(int html_len) {
    nodes_count = 0;
    attr_pool_pos = 1;  /* offset 0 reserved as "none" */
    forms_count = 0;
    inputs_count = 0;
    title_buf[0] = 0;

    /* synthetic root */
    int root = alloc_node(T_ROOT, -1);

    int stack[64];
    int stack_top = 0;
    stack[0] = root;
    int cur_form = -1;

    int i = 0;
    int n = html_len;
    while (i < n) {
        if (page_buf[i] == '<') {
            /* comment? */
            if (i + 3 < n && page_buf[i+1] == '!' &&
                page_buf[i+2] == '-' && page_buf[i+3] == '-') {
                i = i + 4;
                while (i + 2 < n &&
                       !(page_buf[i] == '-' && page_buf[i+1] == '-' &&
                         page_buf[i+2] == '>')) i = i + 1;
                if (i < n) i = i + 3;
                continue;
            }
            /* doctype? */
            if (i + 1 < n && page_buf[i+1] == '!') {
                while (i < n && page_buf[i] != '>') i = i + 1;
                if (i < n) i = i + 1;
                continue;
            }
            int closing = 0;
            int j = i + 1;
            if (j < n && page_buf[j] == '/') { closing = 1; j = j + 1; }
            int name_start = j;
            while (j < n && page_buf[j] != ' ' && page_buf[j] != '\t' &&
                   page_buf[j] != '>' && page_buf[j] != '/' &&
                   page_buf[j] != '\n' && page_buf[j] != '\r') j = j + 1;
            int name_len = j - name_start;
            int tag = tag_id(page_buf + name_start, name_len);

            /* attributes */
            char href_v[1024]; href_v[0] = 0;
            char src_v[1024]; src_v[0] = 0;
            char color_v[64]; color_v[0] = 0;
            char bg_v[64]; bg_v[0] = 0;
            char action_v[1024]; action_v[0] = 0;
            char method_v[16]; method_v[0] = 0;
            char name_v[64]; name_v[0] = 0;
            char value_v[128]; value_v[0] = 0;
            char type_v[16]; type_v[0] = 0;
            char style_v[256]; style_v[0] = 0;

            int self_close = 0;
            if (!closing) {
                while (j < n && page_buf[j] != '>') {
                    while (j < n && (page_buf[j] == ' ' || page_buf[j] == '\t' ||
                                     page_buf[j] == '\n' || page_buf[j] == '\r')) j = j + 1;
                    if (j >= n || page_buf[j] == '>') break;
                    if (page_buf[j] == '/') { self_close = 1; j = j + 1; continue; }
                    int an_s = j;
                    while (j < n && page_buf[j] != '=' && page_buf[j] != ' ' &&
                           page_buf[j] != '\t' && page_buf[j] != '>' &&
                           page_buf[j] != '/' && page_buf[j] != '\n' &&
                           page_buf[j] != '\r') j = j + 1;
                    int an_l = j - an_s;
                    char av[256]; av[0] = 0;
                    int avl = 0;
                    if (j < n && page_buf[j] == '=') {
                        j = j + 1;
                        char quote = 0;
                        if (j < n && (page_buf[j] == '"' || page_buf[j] == '\'')) {
                            quote = page_buf[j];
                            j = j + 1;
                            int as = j;
                            while (j < n && page_buf[j] != quote) j = j + 1;
                            avl = j - as;
                            if (avl > 255) avl = 255;
                            int k = 0;
                            while (k < avl) { av[k] = page_buf[as + k]; k = k + 1; }
                            av[avl] = 0;
                            if (j < n) j = j + 1;
                        } else {
                            int as = j;
                            while (j < n && page_buf[j] != ' ' && page_buf[j] != '\t' &&
                                   page_buf[j] != '>' && page_buf[j] != '\n' &&
                                   page_buf[j] != '\r') j = j + 1;
                            avl = j - as;
                            if (avl > 255) avl = 255;
                            int k = 0;
                            while (k < avl) { av[k] = page_buf[as + k]; k = k + 1; }
                            av[avl] = 0;
                        }
                    }
                    /* match attribute name */
                    if (b_strieq_n(page_buf + an_s, "href", 4) && an_l == 4) {
                        b_strcpy_n(href_v, av, URL_MAX);
                    } else if (b_strieq_n(page_buf + an_s, "src", 3) && an_l == 3) {
                        b_strcpy_n(src_v, av, URL_MAX);
                    } else if (b_strieq_n(page_buf + an_s, "color", 5) && an_l == 5) {
                        b_strcpy_n(color_v, av, 64);
                    } else if (b_strieq_n(page_buf + an_s, "bgcolor", 7) && an_l == 7) {
                        b_strcpy_n(bg_v, av, 64);
                    } else if (b_strieq_n(page_buf + an_s, "action", 6) && an_l == 6) {
                        b_strcpy_n(action_v, av, URL_MAX);
                    } else if (b_strieq_n(page_buf + an_s, "method", 6) && an_l == 6) {
                        b_strcpy_n(method_v, av, 16);
                    } else if (b_strieq_n(page_buf + an_s, "name", 4) && an_l == 4) {
                        b_strcpy_n(name_v, av, 64);
                    } else if (b_strieq_n(page_buf + an_s, "value", 5) && an_l == 5) {
                        b_strcpy_n(value_v, av, 128);
                    } else if (b_strieq_n(page_buf + an_s, "type", 4) && an_l == 4) {
                        b_strcpy_n(type_v, av, 16);
                    } else if (b_strieq_n(page_buf + an_s, "style", 5) && an_l == 5) {
                        b_strcpy_n(style_v, av, 256);
                    }
                }
                if (j < n) j = j + 1;
            } else {
                while (j < n && page_buf[j] != '>') j = j + 1;
                if (j < n) j = j + 1;
            }
            i = j;

            if (tag == T_SCRIPT || tag == T_STYLE) {
                if (!closing) {
                    char *ct = (tag == T_SCRIPT) ? "script" : "style";
                    i = skip_to_close(page_buf, n, i, ct);
                }
                continue;
            }

            if (closing) {
                /* pop until match found */
                int k = stack_top;
                while (k > 0) {
                    if (n_tag[stack[k]] == tag) {
                        stack_top = k - 1;
                        if (tag == T_FORM) cur_form = -1;
                        break;
                    }
                    k = k - 1;
                }
                continue;
            }

            int parent = stack[stack_top];
            int idx = alloc_node(tag, parent);
            if (idx < 0) continue;

            if (href_v[0])  n_href[idx]   = attr_intern(href_v, b_strlen(href_v));
            if (src_v[0])   n_src[idx]    = attr_intern(src_v, b_strlen(src_v));
            if (color_v[0]) {
                int c;
                if (parse_color(color_v, &c)) n_color[idx] = c;
            }
            if (bg_v[0]) {
                int c;
                if (parse_color(bg_v, &c)) n_bgcolor[idx] = c;
            }
            if (action_v[0]) n_action[idx] = attr_intern(action_v, b_strlen(action_v));
            if (name_v[0])   n_name[idx]   = attr_intern(name_v, b_strlen(name_v));
            if (value_v[0])  n_value[idx]  = attr_intern(value_v, b_strlen(value_v));
            if (type_v[0])   n_type[idx]   = attr_intern(type_v, b_strlen(type_v));
            if (style_v[0])  apply_style(style_v, idx);

            if (tag == T_FORM) {
                if (forms_count < MAX_FORMS) {
                    int fi = forms_count;
                    forms_count = forms_count + 1;
                    form_action[fi] = (action_v[0])
                        ? attr_intern(action_v, b_strlen(action_v)) : -1;
                    cur_form = fi;
                }
            } else if (tag == T_INPUT) {
                /* register text input; ignore submit-only */
                int is_text = 1;
                if (type_v[0] && b_strieq(type_v, "submit")) is_text = 0;
                if (type_v[0] && b_strieq(type_v, "button")) is_text = 0;
                if (type_v[0] && b_strieq(type_v, "hidden")) is_text = 0;
                if (is_text && inputs_count < MAX_INPUTS) {
                    int ii = inputs_count;
                    inputs_count = inputs_count + 1;
                    n_form_idx[idx] = ii;
                    input_name_off[ii] = (name_v[0])
                        ? attr_intern(name_v, b_strlen(name_v)) : -1;
                    input_form[ii] = cur_form;
                    /* default value */
                    int k = 0;
                    while (k < 127 && value_v[k]) {
                        input_value[ii * 128 + k] = value_v[k];
                        k = k + 1;
                    }
                    input_value[ii * 128 + k] = 0;
                } else {
                    n_form_idx[idx] = -2; /* submit/button */
                }
            } else if (tag == T_BUTTON) {
                n_form_idx[idx] = (cur_form >= 0) ? -3 : -1;
            }

            if (!is_void_tag(tag) && !self_close) {
                if (stack_top + 1 < 64) {
                    stack_top = stack_top + 1;
                    stack[stack_top] = idx;
                }
            }
        } else {
            /* text node */
            int ts = i;
            while (i < n && page_buf[i] != '<') i = i + 1;
            int tl = i - ts;
            /* check non-whitespace */
            int has = 0;
            int k = 0;
            while (k < tl) {
                if (page_buf[ts + k] != ' ' && page_buf[ts + k] != '\t' &&
                    page_buf[ts + k] != '\n' && page_buf[ts + k] != '\r') {
                    has = 1; break;
                }
                k = k + 1;
            }
            if (has || (stack_top > 0 && n_tag[stack[stack_top]] == T_PRE)) {
                int parent = stack[stack_top];
                int idx = alloc_node(T_TEXT, parent);
                if (idx >= 0) {
                    /* decode entities into attr_pool */
                    char tmp[2048];
                    int tll = tl;
                    if (tll > 2047) tll = 2047;
                    int dl = decode_entities(page_buf + ts, tll, tmp, 2048);
                    n_text_off[idx] = attr_intern(tmp, dl);
                    n_text_len[idx] = dl;
                    /* if parent is title, save title */
                    if (parent >= 0 && n_tag[parent] == T_TITLE && title_buf[0] == 0) {
                        b_strcpy_n(title_buf, tmp, 256);
                    }
                }
            }
        }
    }
}
