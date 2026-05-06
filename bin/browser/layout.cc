/* ---------- Layout ----------
 *
 * Two pipelines coexist during the Plan-2 transition:
 *
 *   1. Legacy box-list layout (legacy_run_layout, legacy_layout_node, ...) —
 *      walks the DOM directly and emits flat `boxes[]` for paint.cc to draw.
 *      Still drives visible rendering until Task 9 cuts paint over to the
 *      render tree. error_page() in paint.cc also drives this pipeline
 *      directly via legacy_emit_box / legacy_last_box.
 *
 *   2. New render-tree BFC layout (run_layout, layout_block, ...) — walks
 *      the rt_* arrays filled in Task 6 and resolves rt_x/y/w/h plus
 *      rt_content_x/y per node. Currently runs alongside the legacy
 *      pipeline; paint will switch over in Task 9. Inline-formatting-context
 *      (line-box building) is stubbed in this task and filled in Task 8 —
 *      until then, inline content is silently dropped from the new pipeline,
 *      but legacy paint still draws it correctly.
 */

/* ---------- Legacy pipeline (DOM-walking, fills boxes[]) ---------- */

int legacy_viewport_w() {
    int w = cur_cw - 8 - 12;
    if (w < 100) w = 100;
    return w;
}

int legacy_parent_color(int idx, int default_c) {
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

int legacy_parent_bg(int idx, int default_c) {
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

int legacy_parent_bold(int idx) {
    while (idx >= 0) {
        int t = n_tag[idx];
        if (t == T_B || t == T_STRONG || t == T_H1 || t == T_H2 ||
            t == T_H3 || t == T_H4 || t == T_H5 || t == T_H6) return 1;
        idx = n_parent[idx];
    }
    return 0;
}

int legacy_parent_link(int idx) {
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
 * (e.g., submit/button/hidden — those render as a submit button instead).
 * Kept public (no legacy_ prefix) — paint/hit-test in Task 9/10 may reuse. */
int find_input_for_node(int idx) {
    for (int k = 0; k < inputs_count; k = k + 1) {
        if (input_node[k] == idx) return k;
    }
    return -1;
}

/* Layout state (legacy only) */
int  L_x;
int  L_y;
int  L_line_h;
int  L_max_w;
int  L_left_margin;

void legacy_emit_box(int kind) {
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

int legacy_last_box() { return boxes_count - 1; }

int legacy_register_link(int href_off) {
    if (links_count >= MAX_LINKS) return -1;
    int li = links_count;
    links_count = links_count + 1;
    link_url_off[li] = href_off;
    return li;
}

void legacy_newline() {
    L_x = L_left_margin;
    L_y = L_y + L_line_h;
    L_line_h = line_h;
}

void legacy_layout_text(int node_idx, int text_off, int len, int link_idx,
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
                legacy_newline();
                i = i + 1;
            }
            continue;
        }

        int wpx = wl * char_w;
        if (L_x + wpx > L_max_w && L_x > L_left_margin) {
            legacy_newline();
        }
        if (boxes_count >= MAX_BOXES) return;
        legacy_emit_box(BK_TEXT);
        int bi = legacy_last_box();
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
            legacy_newline();
            i = i + 1;
        } else if (!in_pre && i < len) {
            /* add single space if not at end of line */
            if (L_x + char_w <= L_max_w) {
                L_x = L_x + char_w;
            } else {
                legacy_newline();
            }
        }
    }
}

void legacy_layout_children(int idx) {
    int c = n_first_child[idx];
    while (c >= 0) {
        legacy_layout_node(c);
        c = n_next[c];
    }
}

void legacy_layout_node(int idx) {
    int t = n_tag[idx];

    /* Block-level tags break first */
    if (t == T_P || t == T_H1 || t == T_H2 || t == T_H3 || t == T_H4 ||
        t == T_H5 || t == T_H6 || t == T_DIV || t == T_LI || t == T_UL ||
        t == T_OL || t == T_PRE || t == T_HR || t == T_FORM ||
        t == T_BR || t == T_BODY) {
        if (L_x > L_left_margin) legacy_newline();
        if (t == T_BR) return;
        if (t == T_HR) {
            legacy_emit_box(BK_HRULE);
            int bi = legacy_last_box();
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
            legacy_emit_box(BK_TEXT);
            int bi = legacy_last_box();
            b_x[bi] = L_left_margin + 4;
            b_y[bi] = L_y;
            b_w[bi] = char_w;
            b_h[bi] = line_h;
            b_text_off[bi] = -2;     /* sentinel for bullet glyph */
            b_text_len[bi] = 0;
            b_fg[bi] = legacy_parent_color(idx, page_fg);
        }
    }

    /* Specific element handling */
    if (t == T_TEXT) {
        if (n_text_off[idx] >= 0) {
            int link = legacy_parent_link(idx);
            int link_idx = -1;
            if (link >= 0) link_idx = legacy_register_link(link);
            int bold = legacy_parent_bold(idx);
            int fg = legacy_parent_color(idx, page_fg);
            int bg = legacy_parent_bg(idx, -1);
            legacy_layout_text(idx, n_text_off[idx], n_text_len[idx],
                               link_idx, bold, fg, bg);
        }
    } else if (t == T_IMG) {
        if (L_x + 80 > L_max_w) legacy_newline();
        legacy_emit_box(BK_IMG);
        int bi = legacy_last_box();
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
            if (L_x + 200 > L_max_w) legacy_newline();
            legacy_emit_box(BK_INPUT);
            int bi = legacy_last_box();
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
            if (L_x + bw > L_max_w) legacy_newline();
            legacy_emit_box(BK_BUTTON);
            int bi = legacy_last_box();
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
        legacy_layout_children(idx);  /* button text */
        return;
    }

    if (t == T_TEXT || t == T_BR || t == T_IMG ||
        t == T_INPUT) return;

    legacy_layout_children(idx);

    if (t == T_P || t == T_H1 || t == T_H2 || t == T_H3 || t == T_H4 ||
        t == T_H5 || t == T_H6 || t == T_DIV || t == T_LI || t == T_UL ||
        t == T_OL || t == T_PRE || t == T_FORM || t == T_BODY) {
        if (L_x > L_left_margin) legacy_newline();
        if (t == T_H1) L_y = L_y + 8;
        else if (t == T_H2) L_y = L_y + 6;
        else if (t == T_P || t == T_DIV) L_y = L_y + 4;
    }
}

void legacy_run_layout() {
    boxes_count = 0;
    links_count = 0;
    L_left_margin = 8;
    L_x = L_left_margin;
    L_y = 4;
    L_line_h = line_h;
    L_max_w = legacy_viewport_w();
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
        legacy_layout_children(body);
    } else {
        /* fallback: layout from root */
        legacy_layout_children(0);
    }
    if (L_x > L_left_margin) L_y = L_y + L_line_h;
    doc_h = L_y + 8;
}

/* ---------- §4 BFC + IFC layout — fills rt_x/y/w/h/content_x/y for every render node. ---------- */

int rt_padding_l(int n) { return cs_padding[rt_style[n]][3]; }
int rt_padding_r(int n) { return cs_padding[rt_style[n]][1]; }
int rt_padding_t(int n) { return cs_padding[rt_style[n]][0]; }
int rt_padding_b(int n) { return cs_padding[rt_style[n]][2]; }
int rt_border_l (int n) { return cs_border [rt_style[n]][3]; }
int rt_border_r (int n) { return cs_border [rt_style[n]][1]; }
int rt_border_t (int n) { return cs_border [rt_style[n]][0]; }
int rt_border_b (int n) { return cs_border [rt_style[n]][2]; }
int rt_margin_l (int n) { return cs_margin [rt_style[n]][3]; }
int rt_margin_r (int n) { return cs_margin [rt_style[n]][1]; }
int rt_margin_t (int n) { return cs_margin [rt_style[n]][0]; }
int rt_margin_b (int n) { return cs_margin [rt_style[n]][2]; }

int viewport_content_w() {
    /* Window inner width minus 12px scrollbar */
    return cur_cw - 12;
}

/* Forward */
void layout_block(int n, int avail_w);
void flush_inline(int parent, int *atom_pile_first, int *atom_pile_count,
                  int cx, int *cy, int max_w);

/* Stubs filled in by Task 8. While stubbed, inline content is silently
 * dropped from the new pipeline — but legacy paint still drives rendering,
 * so visible output is unchanged. */
void collect_inline_atoms(int n) { (void)n; }
void flush_inline(int parent, int *atom_pile_first, int *atom_pile_count,
                  int cx, int *cy, int max_w) {
    (void)parent; (void)atom_pile_first; (void)atom_pile_count;
    (void)cx; (void)cy; (void)max_w;
}

void layout_block(int n, int avail_w) {
    /* Resolve width */
    int style_w = cs_width[rt_style[n]];
    int w;
    if (style_w >= 0) w = style_w;
    else w = avail_w - rt_margin_l(n) - rt_margin_r(n);
    if (w < 0) w = 0;
    rt_w[n] = w;

    int content_w = w - rt_padding_l(n) - rt_padding_r(n)
                    - rt_border_l(n) - rt_border_r(n);
    if (content_w < 0) content_w = 0;

    int cx = rt_padding_l(n) + rt_border_l(n);
    int cy = rt_padding_t(n) + rt_border_t(n);
    rt_content_x[n] = cx;
    rt_content_y[n] = cy;

    /* Walk children. Inline runs get accumulated and flushed when a block
     * sibling appears or end of children. */
    int pile_first = la_count;
    int pile_count = 0;

    int c = rt_first_child[n];
    while (c >= 0) {
        int kind = rt_kind[c];
        if (kind == RT_LIST_MARKER) {
            /* Markers don't affect block layout flow; placed in padding-left
             * reservation at paint time. */
            c = rt_next[c]; continue;
        }
        if (rt_kind_is_block_level(kind) ||
            (kind == RT_BLOCK)) {
            /* Flush pending inline run */
            if (pile_count > 0) {
                flush_inline(n, &pile_first, &pile_count, cx, &cy, content_w);
            }
            /* Lay out block child */
            int child_avail = content_w;
            layout_block(c, child_avail);
            int child_x = cx + rt_margin_l(c);
            int child_y = cy + rt_margin_t(c);
            rt_x[c] = child_x;
            rt_y[c] = child_y;
            cy = child_y + rt_h[c] + rt_margin_b(c);
        } else {
            /* Inline / text / inline-block / replaced: accumulate as atoms */
            collect_inline_atoms(c);
            pile_count = la_count - pile_first;
        }
        c = rt_next[c];
    }
    if (pile_count > 0) {
        flush_inline(n, &pile_first, &pile_count, cx, &cy, content_w);
    }

    /* Resolve own height */
    int style_h = cs_height[rt_style[n]];
    if (style_h >= 0) {
        rt_h[n] = style_h;
    } else {
        rt_h[n] = cy + rt_padding_b(n) + rt_border_b(n);
    }
}

void run_layout() {
    if (rt_count == 0) return;
    int root = 0;
    /* Root: width = viewport content width minus chrome (address bar,
     * status, scrollbar). viewport_x/viewport_y are defined in paint.cc;
     * cross-TU forward refs are resolved at JIT pass. */
    rt_x[root] = viewport_x();
    rt_y[root] = viewport_y();
    int avail = viewport_content_w();
    layout_block(root, avail);
    doc_h = rt_y[root] + rt_h[root];
}
