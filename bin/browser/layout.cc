/* ---------- Layout ---------- */

int viewport_w() {
    int w = cur_cw - 8 - 12;
    if (w < 100) w = 100;
    return w;
}

int parent_color(int idx, int default_c) {
    while (idx >= 0) {
        int off = dom_attr_get(idx, "color");
        if (off >= 0) {
            int c;
            if (parse_color(attr_pool + off, &c)) return c;
        }
        idx = n_parent[idx];
    }
    return default_c;
}

int parent_bg(int idx, int default_c) {
    while (idx >= 0) {
        int off = dom_attr_get(idx, "bgcolor");
        if (off >= 0) {
            int c;
            if (parse_color(attr_pool + off, &c)) return c;
        }
        idx = n_parent[idx];
    }
    return default_c;
}

int parent_bold(int idx) {
    while (idx >= 0) {
        int t = n_tag[idx];
        if (t == T_B || t == T_STRONG || t == T_H1 || t == T_H2 ||
            t == T_H3 || t == T_H4 || t == T_H5 || t == T_H6) return 1;
        idx = n_parent[idx];
    }
    return 0;
}

int parent_link(int idx) {
    while (idx >= 0) {
        if (n_tag[idx] == T_A) {
            int off = dom_attr_get(idx, "href");
            if (off >= 0) return off;
        }
        idx = n_parent[idx];
    }
    return -1;
}

/* Linear scan: which inputs[] index, if any, was registered for this
 * DOM node? -1 if this <input> is not in the editable inputs[] table
 * (e.g., submit/button/hidden — those render as a submit button instead). */
int find_input_for_node(int idx) {
    for (int k = 0; k < inputs_count; k = k + 1) {
        if (input_node[k] == idx) return k;
    }
    return -1;
}

/* Layout state */
int  L_x;
int  L_y;
int  L_line_h;
int  L_max_w;
int  L_left_margin;

void emit_box(int kind) {
    if (boxes_count >= MAX_BOXES) return;
    int b = boxes_count;
    boxes_count = boxes_count + 1;
    b_kind[b] = kind;
    b_x[b] = 0; b_y[b] = 0; b_w[b] = 0; b_h[b] = 0;
    b_fg[b] = page_fg; b_bg[b] = -1;
    b_text_off[b] = -1; b_text_len[b] = 0;
    b_link_idx[b] = -1; b_input_idx[b] = -1; b_img_handle[b] = -1;
    b_bold[b] = 0; b_underline[b] = 0;
}

int last_box() { return boxes_count - 1; }

int register_link(int href_off) {
    if (links_count >= MAX_LINKS) return -1;
    int li = links_count;
    links_count = links_count + 1;
    link_url_off[li] = href_off;
    return li;
}

void newline() {
    L_x = L_left_margin;
    L_y = L_y + L_line_h;
    L_line_h = line_h;
}

void layout_text(int node_idx, int text_off, int len, int link_idx,
                 int bold, int fg, int bg) {
    char *text = attr_pool + text_off;
    int i = 0;
    int in_pre = 0;
    int p = n_parent[node_idx];
    while (p >= 0) {
        if (n_tag[p] == T_PRE || n_tag[p] == T_CODE) { in_pre = 1; break; }
        p = n_parent[p];
    }
    while (i < len) {
        /* skip leading whitespace runs (non-PRE) */
        if (!in_pre) {
            while (i < len && (text[i] == ' ' || text[i] == '\t' ||
                               text[i] == '\n' || text[i] == '\r')) i = i + 1;
            if (i >= len) break;
        }

        /* find next word boundary */
        int ws = i;
        if (in_pre) {
            while (i < len && text[i] != '\n') i = i + 1;
        } else {
            while (i < len && text[i] != ' ' && text[i] != '\t' &&
                   text[i] != '\n' && text[i] != '\r') i = i + 1;
        }
        int wl = i - ws;
        if (wl == 0) {
            if (in_pre && i < len && text[i] == '\n') {
                newline();
                i = i + 1;
            }
            continue;
        }

        int wpx = wl * char_w;
        if (L_x + wpx > L_max_w && L_x > L_left_margin) {
            newline();
        }
        if (boxes_count >= MAX_BOXES) return;
        emit_box(BK_TEXT);
        int bi = last_box();
        b_x[bi] = L_x;
        b_y[bi] = L_y;
        b_w[bi] = wpx;
        b_h[bi] = line_h;
        b_text_off[bi] = text_off + ws;
        b_text_len[bi] = wl;
        b_fg[bi] = fg;
        b_bg[bi] = bg;
        b_link_idx[bi] = link_idx;
        b_bold[bi] = bold;
        b_underline[bi] = (link_idx >= 0) ? 1 : 0;

        L_x = L_x + wpx;
        if (in_pre && i < len && text[i] == '\n') {
            newline();
            i = i + 1;
        } else if (!in_pre && i < len) {
            /* add single space if not at end of line */
            if (L_x + char_w <= L_max_w) {
                L_x = L_x + char_w;
            } else {
                newline();
            }
        }
    }
}

void layout_children(int idx) {
    int c = n_first_child[idx];
    while (c >= 0) {
        layout_node(c);
        c = n_next[c];
    }
}

void layout_node(int idx) {
    int t = n_tag[idx];

    /* Block-level tags break first */
    if (t == T_P || t == T_H1 || t == T_H2 || t == T_H3 || t == T_H4 ||
        t == T_H5 || t == T_H6 || t == T_DIV || t == T_LI || t == T_UL ||
        t == T_OL || t == T_PRE || t == T_HR || t == T_FORM ||
        t == T_BR || t == T_BODY) {
        if (L_x > L_left_margin) newline();
        if (t == T_BR) return;
        if (t == T_HR) {
            emit_box(BK_HRULE);
            int bi = last_box();
            b_x[bi] = L_left_margin;
            b_y[bi] = L_y + 4;
            b_w[bi] = L_max_w - L_left_margin;
            b_h[bi] = 1;
            b_fg[bi] = 0x808080;
            L_y = L_y + 10;
            return;
        }
        if (t == T_LI) {
            /* indent + bullet */
            L_x = L_left_margin + 16;
            emit_box(BK_TEXT);
            int bi = last_box();
            b_x[bi] = L_left_margin + 4;
            b_y[bi] = L_y;
            b_w[bi] = char_w;
            b_h[bi] = line_h;
            b_text_off[bi] = -2;     /* sentinel for bullet glyph */
            b_text_len[bi] = 0;
            b_fg[bi] = parent_color(idx, page_fg);
        }
    }

    /* Specific element handling */
    if (t == T_TEXT) {
        if (n_text_off[idx] >= 0) {
            int link = parent_link(idx);
            int link_idx = -1;
            if (link >= 0) link_idx = register_link(link);
            int bold = parent_bold(idx);
            int fg = parent_color(idx, page_fg);
            int bg = parent_bg(idx, -1);
            layout_text(idx, n_text_off[idx], n_text_len[idx],
                        link_idx, bold, fg, bg);
        }
    } else if (t == T_IMG) {
        if (L_x + 80 > L_max_w) newline();
        emit_box(BK_IMG);
        int bi = last_box();
        b_x[bi] = L_x;
        b_y[bi] = L_y;
        b_w[bi] = 80;
        b_h[bi] = 30;
        int src_off = dom_attr_get(idx, "src");
        b_text_off[bi] = src_off;
        b_fg[bi] = 0x444444;
        b_bg[bi] = 0xE0E0E0;
        L_x = L_x + 80 + char_w;
    } else if (t == T_INPUT) {
        int ii = find_input_for_node(idx);
        if (ii >= 0) {
            if (L_x + 200 > L_max_w) newline();
            emit_box(BK_INPUT);
            int bi = last_box();
            b_x[bi] = L_x;
            b_y[bi] = L_y;
            b_w[bi] = 200;
            b_h[bi] = line_h + 4;
            b_input_idx[bi] = ii;
            b_fg[bi] = 0x000000;
            b_bg[bi] = 0xFFFFFF;
            if (L_line_h < b_h[bi]) L_line_h = b_h[bi];
            L_x = L_x + 200 + char_w;
        } else {
            /* submit / button / etc. — render as a submit button */
            int v_off = dom_attr_get(idx, "value");
            char *label = (v_off >= 0) ? attr_pool + v_off : "Submit";
            int ll = b_strlen(label);
            int bw = ll * char_w + 16;
            if (L_x + bw > L_max_w) newline();
            emit_box(BK_BUTTON);
            int bi = last_box();
            b_x[bi] = L_x;
            b_y[bi] = L_y;
            b_w[bi] = bw;
            b_h[bi] = line_h + 4;
            b_text_off[bi] = (v_off >= 0) ? v_off : -3;
            b_text_len[bi] = ll;
            b_fg[bi] = 0x000000;
            b_bg[bi] = 0xC0C0C0;
            b_link_idx[bi] = -1;
            b_input_idx[bi] = -2; /* marker: submit */
            b_bold[bi] = 0;
            if (L_line_h < b_h[bi]) L_line_h = b_h[bi];
            L_x = L_x + bw + char_w;
        }
        return;  /* void */
    } else if (t == T_BUTTON) {
        layout_children(idx);  /* button text */
        return;
    }

    if (t == T_TEXT || t == T_BR || t == T_IMG ||
        t == T_INPUT) return;

    layout_children(idx);

    if (t == T_P || t == T_H1 || t == T_H2 || t == T_H3 || t == T_H4 ||
        t == T_H5 || t == T_H6 || t == T_DIV || t == T_LI || t == T_UL ||
        t == T_OL || t == T_PRE || t == T_FORM || t == T_BODY) {
        if (L_x > L_left_margin) newline();
        if (t == T_H1) L_y = L_y + 8;
        else if (t == T_H2) L_y = L_y + 6;
        else if (t == T_P || t == T_DIV) L_y = L_y + 4;
    }
}

void run_layout() {
    boxes_count = 0;
    links_count = 0;
    L_left_margin = 8;
    L_x = L_left_margin;
    L_y = 4;
    L_line_h = line_h;
    L_max_w = viewport_w();
    page_bg = 0xFFFFFF;
    page_fg = 0x000000;
    /* find body */
    int body = -1;
    int i = 0;
    while (i < nodes_count) {
        if (n_tag[i] == T_BODY) { body = i; break; }
        i = i + 1;
    }
    if (body >= 0) {
        int bg_off = dom_attr_get(body, "bgcolor");
        if (bg_off >= 0) {
            int c;
            if (parse_color(attr_pool + bg_off, &c)) page_bg = c;
        }
        int fg_off = dom_attr_get(body, "color");
        if (fg_off >= 0) {
            int c;
            if (parse_color(attr_pool + fg_off, &c)) page_fg = c;
        }
    }

    if (body >= 0) {
        layout_children(body);
    } else {
        /* fallback: layout from root */
        layout_children(0);
    }
    if (L_x > L_left_margin) L_y = L_y + L_line_h;
    doc_h = L_y + 8;
}
