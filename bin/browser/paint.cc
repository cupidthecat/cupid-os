/* Render */

int viewport_x() { return 0; }
int viewport_y() { return ADDR_H + 1; }
int viewport_h() {
    int h = cur_ch - ADDR_H - STATUS_H - 2;
    if (h < 60) h = 60;
    return h;
}

/* §6 Render-tree paint - single traversal, bg -> border -> content order */

int rt_screen_x(int n) {
    /* Walk parent chain summing x offsets; viewport_x() is the page origin. */
    int x = 0;
    int cur = n;
    while (cur >= 0) {
        x += rt_x[cur];
        cur = rt_parent[cur];
    }
    return x + viewport_x();
}

int rt_screen_y(int n) {
    int y = 0;
    int cur = n;
    while (cur >= 0) {
        y += rt_y[cur];
        cur = rt_parent[cur];
    }
    return y + viewport_y() - scroll_y;
}

void paint_rt_node(int n) {
    int sx = rt_screen_x(n);
    int sy = rt_screen_y(n);
    int w  = rt_w[n];
    int h  = rt_h[n];

    int kind = rt_kind[n];
    int cs = rt_style[n];

    /* Off-screen cull */
    if (sy + h < viewport_y()) return;
    if (sy > viewport_y() + viewport_h()) return;

    /* 1. Background. CSS canvas-painting rule: if body's bg propagated up
     * to the document canvas (because html has no bg), the body element
     * itself must NOT paint bg again. Anonymous blocks share the parent's
     * cs but have rt_dom == -1, so the suppression key is the dom tag. */
    int bg = cs_bg[cs];
    int suppress_bg = 0;
    if (doc_bg_suppress_body && rt_dom[n] >= 0 &&
        n_tag[rt_dom[n]] == T_BODY) suppress_bg = 1;
    if (bg >= 0 && !suppress_bg &&
        (kind == RT_BLOCK || kind == RT_INLINE_BLOCK ||
         kind == RT_LIST_ITEM || kind == RT_TABLE_CELL)) {
        gfx2d_rect_fill(sx, sy, w, h, bg);
    }

    /* 2. Border (1px) */
    if (cs_border[cs][0] || cs_border[cs][1] || cs_border[cs][2] || cs_border[cs][3]) {
        int bc = cs_border_color[cs];
        if (cs_border[cs][0]) gfx2d_rect_fill(sx, sy, w, 1, bc);
        if (cs_border[cs][2]) gfx2d_rect_fill(sx, sy + h - 1, w, 1, bc);
        if (cs_border[cs][3]) gfx2d_rect_fill(sx, sy, 1, h, bc);
        if (cs_border[cs][1]) gfx2d_rect_fill(sx + w - 1, sy, 1, h, bc);
    }

    /* 3. Content */
    if (kind == RT_TEXT) {
        paint_rt_text(n, sx, sy);
    } else if (kind == RT_REPLACED) {
        paint_rt_replaced(n, sx, sy);
    } else if (kind == RT_LIST_MARKER) {
        paint_rt_marker(n, sx, sy);
    } else if (kind == RT_LINE_BOX) {
        paint_rt_line_box(n, sx, sy);
    }

    /* 4. Children. Inline subtrees (RT_INLINE/RT_TEXT/RT_INLINE_BLOCK/
     * RT_REPLACED) were absorbed into RT_LINE_BOX siblings during
     * flush_inline; LINE_BOX paint walks the atom slice and re-enters
     * replaced/inline-block via la_text_off < 0. Skip them here to avoid
     * double-paint. */
    int c = rt_first_child[n];
    while (c >= 0) {
        int ck = rt_kind[c];
        if (ck == RT_INLINE || ck == RT_TEXT ||
            ck == RT_INLINE_BLOCK || ck == RT_REPLACED) {
            c = rt_next[c]; continue;
        }
        paint_rt_node(c);
        c = rt_next[c];
    }
}

void paint_rt_text(int n, int sx, int sy) {
    /* RT_TEXT outside a LINE_BOX (rare - orphan text node not flushed): just draw. */
    int cs = rt_style[n];
    int tier = cs_font_size_tier[cs];
    int font = tier_to_font(tier);
    int fg = cs_color[cs];
    if (fg < 0) fg = 0x000000;
    char buf[256];
    int len = rt_text_len[n];
    if (len > 255) len = 255;
    for (int k = 0; k < len; k++) buf[k] = attr_pool[rt_text_off[n] + k];
    buf[len] = 0;
    gfx2d_text_n(sx, sy, buf, len, fg, font);
    if (cs_text_dec[cs] & TD_UNDERLINE) {
        int uw = gfx2d_text_width_n(buf, len, font);
        gfx2d_rect_fill(sx, sy + effective_line_h(cs, tier) - 2, uw, 1, fg);
    }
}

void paint_rt_line_box(int n, int sx, int sy) {
    int first = rt_line_atom_first[n];
    int count = rt_line_atom_count[n];
    for (int k = first; k < first + count; k++) {
        if (la_x[k] < 0) continue;       /* sentinel atom - break point not painted */
        int ax = sx + la_x[k];
        int tier = la_font_tier[k];
        /* Center glyph vertically in the line box: line_h is taller than the
         * glyph (1.5x), so put the glyph on the visual midline rather than
         * jammed against the top. Bias one px upward so the cap height feels
         * baseline-anchored. */
        int glyph_h = (tier >= 3) ? 16 : 8;
        int ay = sy + (rt_h[n] - glyph_h) / 2;
        if (la_text_off[k] < 0) {
            /* Replaced/inline-block reference */
            int rt_n = -la_text_off[k] - 1;
            rt_x[rt_n] = la_x[k];
            rt_y[rt_n] = sy - rt_screen_y(rt_parent[rt_n]) + viewport_y() - scroll_y;
            paint_rt_node(rt_n);
            continue;
        }
        int fg = la_fg[k];
        if (fg < 0) fg = 0x000000;
        int bg = la_bg[k];
        if (bg >= 0) gfx2d_rect_fill(ax, ay, la_w[k], effective_line_h(rt_style[n], tier), bg);
        char buf[256];
        int len = la_text_len[k];
        if (len > 255) len = 255;
        for (int kk = 0; kk < len; kk++) buf[kk] = attr_pool[la_text_off[k] + kk];
        buf[len] = 0;
        int font = tier_to_font(tier);
        gfx2d_text_n(ax, ay, buf, len, fg, font);
        if (la_bold[k]) {
            /* Bold: re-draw shifted to thicken strokes. LARGE-tier headings
             * also strike vertically so h1/h2 read as visibly heavier than
             * body bold. */
            gfx2d_text_n(ax + 1, ay, buf, len, fg, font);
            if (tier >= 3) {
                gfx2d_text_n(ax, ay + 1, buf, len, fg, font);
                gfx2d_text_n(ax + 1, ay + 1, buf, len, fg, font);
            }
        }
        if (la_underline[k]) {
            /* Place underline just below the glyph rather than at the bottom
             * of the line box, so it tracks the text instead of floating in
             * the inter-line gap. */
            gfx2d_rect_fill(ax, ay + glyph_h, la_w[k], 1, fg);
        }
    }
}

void paint_rt_replaced(int n, int sx, int sy) {
    int tag = (rt_dom[n] >= 0) ? n_tag[rt_dom[n]] : 0;
    if (tag == T_IMG) {
        /* Plan-2: placeholder. Plan 3 fetches and decodes. */
        gfx2d_rect_fill(sx, sy, rt_w[n], rt_h[n], 0xE0E0E0);
        gfx2d_rect_fill(sx, sy, rt_w[n], 1, 0x808080);
        gfx2d_rect_fill(sx, sy + rt_h[n] - 1, rt_w[n], 1, 0x808080);
        gfx2d_rect_fill(sx, sy, 1, rt_h[n], 0x808080);
        gfx2d_rect_fill(sx + rt_w[n] - 1, sy, 1, rt_h[n], 0x808080);
        gfx2d_text(sx + 4, sy + 4, "[img]", 0x404040, 0);
        return;
    }
    if (tag == T_INPUT) {
        char *type_s = dom_attr_str(rt_dom[n], "type");
        int is_check = 0;
        int is_radio = 0;
        if (type_s) {
            if (b_strieq(type_s, "checkbox")) is_check = 1;
            if (!is_check && b_strieq(type_s, "radio")) is_radio = 1;
        }
        if (is_check || is_radio) {
            /* Small square box. CSS-author border (cs_border) is painted
             * by the generic decoration pass before this; here we draw
             * the inner control face plus the check glyph. */
            int sty = rt_style[n];
            int border_set = (cs_border[sty][0] | cs_border[sty][1] |
                              cs_border[sty][2] | cs_border[sty][3]) != 0;
            gfx2d_rect_fill(sx, sy, rt_w[n], rt_h[n], 0xFFFFFF);
            if (!border_set) {
                gfx2d_rect_fill(sx, sy, rt_w[n], 1, 0x808080);
                gfx2d_rect_fill(sx, sy + rt_h[n] - 1, rt_w[n], 1, 0x808080);
                gfx2d_rect_fill(sx, sy, 1, rt_h[n], 0x808080);
                gfx2d_rect_fill(sx + rt_w[n] - 1, sy, 1, rt_h[n], 0x808080);
            }
            char *checked = dom_attr_str(rt_dom[n], "checked");
            if (checked) {
                if (is_check) {
                    gfx2d_text(sx + 3, sy + 2, "x", 0x000000, 0);
                } else {
                    /* radio dot */
                    int dx = sx + rt_w[n] / 2 - 2;
                    int dy = sy + rt_h[n] / 2 - 2;
                    gfx2d_rect_fill(dx, dy, 4, 4, 0x000000);
                }
            }
            return;
        }
        /* text-style input */
        gfx2d_rect_fill(sx, sy, rt_w[n], rt_h[n], 0xFFFFFF);
        gfx2d_rect_fill(sx, sy, rt_w[n], 1, 0x808080);
        gfx2d_rect_fill(sx, sy + rt_h[n] - 1, rt_w[n], 1, 0x808080);
        gfx2d_rect_fill(sx, sy, 1, rt_h[n], 0x808080);
        gfx2d_rect_fill(sx + rt_w[n] - 1, sy, 1, rt_h[n], 0x808080);
        int ii = rt_input_idx[n];
        if (ii >= 0) {
            char *iv = input_value + ii * 128;
            gfx2d_text(sx + 2, sy + 2, iv, 0x000000, 0);
        }
        return;
    }
    if (tag == T_BUTTON) {
        gfx2d_rect_fill(sx, sy, rt_w[n], rt_h[n], 0xC0C0C0);
        /* button label: walk to first text child */
        int c = rt_first_child[n];
        while (c >= 0 && rt_kind[c] != RT_TEXT) {
            c = rt_first_child[c] >= 0 ? rt_first_child[c] : rt_next[c];
        }
        if (c >= 0) {
            char buf[64];
            int len = rt_text_len[c];
            if (len > 63) len = 63;
            for (int k = 0; k < len; k++) buf[k] = attr_pool[rt_text_off[c] + k];
            buf[len] = 0;
            gfx2d_text(sx + 4, sy + 2, buf, 0x000000, 0);
        }
        return;
    }
}

void paint_rt_marker(int n, int sx, int sy) {
    int parent = rt_parent[n];
    int cs = rt_style[parent];
    int ls = cs_list_style[cs];
    int fg = cs_color[cs] >= 0 ? cs_color[cs] : 0x000000;
    char *glyph = "*";
    if (ls == LS_DISC)        glyph = "*";
    else if (ls == LS_CIRCLE) glyph = "o";
    else if (ls == LS_SQUARE) glyph = "#";
    else if (ls == LS_NONE)   return;
    else if (ls == LS_DECIMAL) {
        /* Compute index among LIST_ITEM siblings */
        int idx = 1;
        int sib = rt_first_child[rt_parent[parent]];
        while (sib >= 0 && sib != parent) {
            if (rt_kind[sib] == RT_LIST_ITEM) idx++;
            sib = rt_next[sib];
        }
        char buf[16];
        int p = b_append_int(buf, 0, idx);
        buf[p] = '.'; buf[p + 1] = 0;
        gfx2d_text(sx - 16, sy + 2, buf, fg, 0);
        return;
    }
    gfx2d_text(sx - 12, sy + 2, glyph, fg, 0);
}

void draw_address_bar(int sx, int sy, int sw) {
    int bg = (focus_mode == FOCUS_ADDR) ? 0xFFFFE0 : 0xF0F0F0;
    gfx2d_rect_fill(sx, sy, sw, ADDR_H, bg);
    gfx2d_hline(sx, sy + ADDR_H - 1, sw, 0x808080);
    gfx2d_text(sx + 4, sy + 6, "URL:", 0x404040, 0);
    char tmp[128];
    int ml = addr_len;
    if (ml > 127) ml = 127;
    int k = 0;
    while (k < ml) { tmp[k] = addr_buf[k]; k = k + 1; }
    tmp[ml] = 0;
    gfx2d_text(sx + 4 + 4 * char_w + 4, sy + 6, tmp, 0x000000, 0);
    if (focus_mode == FOCUS_ADDR) {
        int cx = sx + 4 + 4 * char_w + 4 + addr_cursor * char_w;
        gfx2d_vline(cx, sy + 4, ADDR_H - 8, 0x000000);
    }
}

void draw_status_bar(int sx, int sy, int sw) {
    gfx2d_rect_fill(sx, sy, sw, STATUS_H, 0xE0E0E0);
    gfx2d_hline(sx, sy, sw, 0x808080);
    char *m = status_msg;
    if (hover_link >= 0 && hover_link < links_count) {
        m = attr_pool + link_url_off[hover_link];
    }
    char tmp[80];
    int k = 0;
    while (m[k] && k < 79) { tmp[k] = m[k]; k = k + 1; }
    tmp[k] = 0;
    gfx2d_text(sx + 4, sy + 4, tmp, 0x202020, 0);
}

void draw_scrollbar(int sx, int sy) {
    int sb_x = sx + cur_cw - 12;
    int sb_y = sy;
    int sb_h = viewport_h();
    gfx2d_rect_fill(sb_x, sb_y, 12, sb_h, 0xE0E0E0);
    if (doc_h <= sb_h) return;
    int thumb_h = (sb_h * sb_h) / doc_h;
    if (thumb_h < 16) thumb_h = 16;
    int max_scroll = doc_h - sb_h;
    int thumb_y = sb_y + ((sb_h - thumb_h) * scroll_y) / max_scroll;
    gfx2d_rect_fill(sb_x + 2, thumb_y, 8, thumb_h, 0x808080);
}

/* Document background per CSS painting model: the canvas takes the html
 * background-color; if html has no background, body's background-color
 * propagates up. Falls back to page_bg (white). cs index == DOM node
 * index, so we scan for T_HTML / T_BODY by tag.
 *
 * Side effect: when body's bg is consumed by the canvas (html had none),
 * set doc_bg_suppress_body so paint_rt_node skips body's own bg paint -
 * otherwise body would paint a second, smaller (margin-inset) rect of
 * the same color, which is wrong if html bg is later set or if the body
 * has a non-default border. */
int document_bg() {
    int html_bg = -1;
    int body_bg = -1;
    for (int n = 0; n < nodes_count; n++) {
        int tag = n_tag[n];
        if (tag == T_HTML && html_bg < 0) html_bg = cs_bg[n];
        else if (tag == T_BODY && body_bg < 0) body_bg = cs_bg[n];
    }
    doc_bg_suppress_body = 0;
    if (html_bg >= 0) return html_bg;
    if (body_bg >= 0) { doc_bg_suppress_body = 1; return body_bg; }
    return page_bg;
}

void render() {
    if (gui_win_begin_paint(win) != 0) return;
    /* Drawing inside begin_paint targets the window's offscreen surface
     * which has its own (0,0) origin; do NOT use gui_win_content_x/y
     * here.  Mouse handlers translate screen coords back to surface coords
     * separately. */
    int cx = 0;
    int cy = 0;

    /* Surface background (covers everything before chrome paints over) */
    gfx2d_rect_fill(cx, cy, cur_cw, cur_ch, page_bg);

    /* Document background: html bg, then body bg, then white. Filled across
     * the full web viewport so a centered body still sits over its
     * propagated page color (CSS canvas-painting rule). */
    int doc_color = document_bg();
    gfx2d_rect_fill(cx + viewport_x(), cy + viewport_y(),
                    cur_cw, viewport_h(), doc_color);

    /* address bar */
    draw_address_bar(cx, cy, cur_cw);

    /* viewport (clipped) - paint the render tree into the content area */
    int vx = cx + viewport_x();
    int vy = cy + viewport_y();
    gfx2d_clip_set(vx, vy, cur_cw - 12, viewport_h());

    if (rt_count > 0) paint_rt_node(0);

    gfx2d_clip_clear();

    /* scrollbar */
    draw_scrollbar(cx, cy + viewport_y());

    /* status bar */
    draw_status_bar(cx, cy + cur_ch - STATUS_H, cur_cw);

    gui_win_end_paint(win);
    gui_win_present(win);
}

void error_page(char *msg) {
    /* Reset all pipeline state and draw the error inline. */
    nodes_count = 0;
    attr_pool_pos = 1;
    rt_count = 0;
    cs_count = 0;
    la_count = 0;
    page_bg = 0xFFE8E8;
    page_fg = 0x000000;
    doc_h = 40;
    if (gui_win_begin_paint(win) != 0) return;
    gfx2d_rect_fill(0, 0, cur_cw, cur_ch, page_bg);
    draw_address_bar(0, 0, cur_cw);
    int vx = viewport_x();
    int vy = viewport_y();
    gfx2d_text(vx + 8, vy + 16, msg, 0x800000, 0);
    draw_scrollbar(0, viewport_y());
    draw_status_bar(0, cur_ch - STATUS_H, cur_cw);
    gui_win_end_paint(win);
    gui_win_present(win);
}
