/* Render */

int viewport_x() { return 0; }
int viewport_y() { return ADDR_H + 1; }
/* Carve out chrome rows: address bar (top, ADDR_H + 1px hairline) and
 * status bar (bottom, STATUS_H + 1px hairline). The extra 1px on each
 * side keeps glyph descenders from the last visible line bleeding
 * across the status-bar fill, which the user reported as the quote
 * line overlapping the bottom chrome on d1_selectors_v2. */
int viewport_h() {
    int h = cur_ch - (ADDR_H + 1) - (STATUS_H + 1);
    if (h < 60) h = 60;
    return h;
}

/* §6 Render-tree paint - single traversal, decoration -> content -> children.
 * Decoration order matches Blink BoxPainter::paintBoxDecorationBackground:
 *   shadow (offset blits) -> background (rounded if border-radius) ->
 *   border (mitered to corners). Reference:
 *   blink/Source/core/paint/BoxPainter.cpp. */

/* Clip stack for `overflow: hidden`. gfx2d_clip_set replaces (no native
 * push/pop), so we mirror the rect history in userland. The viewport-wide
 * clip from render() lives at index 0 and never pops; nested clips push on
 * entry to an OVERFLOW_HIDDEN node and pop on exit. */
int paint_clip_x[16];
int paint_clip_y[16];
int paint_clip_w[16];
int paint_clip_h[16];
int paint_clip_top;

void paint_clip_init(int x, int y, int w, int h) {
    paint_clip_top = 0;
    paint_clip_x[0] = x; paint_clip_y[0] = y;
    paint_clip_w[0] = w; paint_clip_h[0] = h;
    gfx2d_clip_set(x, y, w, h);
}

/* Intersect (x,y,w,h) with the current top of stack and push the result.
 * gfx2d_clip_set is replaced with the intersection. */
void paint_clip_push(int x, int y, int w, int h) {
    if (paint_clip_top + 1 >= 16) return;
    int px = paint_clip_x[paint_clip_top];
    int py = paint_clip_y[paint_clip_top];
    int pw = paint_clip_w[paint_clip_top];
    int ph = paint_clip_h[paint_clip_top];
    int x0 = (x > px) ? x : px;
    int y0 = (y > py) ? y : py;
    int x1 = ((x + w) < (px + pw)) ? (x + w) : (px + pw);
    int y1 = ((y + h) < (py + ph)) ? (y + h) : (py + ph);
    int nw = x1 - x0; if (nw < 0) nw = 0;
    int nh = y1 - y0; if (nh < 0) nh = 0;
    paint_clip_top++;
    paint_clip_x[paint_clip_top] = x0;
    paint_clip_y[paint_clip_top] = y0;
    paint_clip_w[paint_clip_top] = nw;
    paint_clip_h[paint_clip_top] = nh;
    gfx2d_clip_set(x0, y0, nw, nh);
}

void paint_clip_pop(void) {
    if (paint_clip_top <= 0) return;
    paint_clip_top--;
    gfx2d_clip_set(paint_clip_x[paint_clip_top],
                   paint_clip_y[paint_clip_top],
                   paint_clip_w[paint_clip_top],
                   paint_clip_h[paint_clip_top]);
}

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

/* Box decoration (shadow + bg + border). Split out from paint_rt_node so
 * border-radius and box-shadow live in one place; matches the structure of
 * Blink's BoxPainter::paintBoxDecorationBackground. */
void paint_rt_box_decoration(int n, int sx, int sy, int w, int h) {
    int kind = rt_kind[n];
    int cs = rt_style[n];

    int paints_box = (kind == RT_BLOCK || kind == RT_INLINE_BLOCK ||
                      kind == RT_LIST_ITEM || kind == RT_TABLE_CELL ||
                      kind == RT_REPLACED);
    if (!paints_box) return;

    int radius = cs_border_radius[cs];
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    if (radius < 0) radius = 0;

    /* 1. Box-shadow (no blur, no spread). One offset rect underneath the
     * box, rounded to match if a radius is set. */
    if (cs_shadow_has[cs]) {
        int dx = cs_shadow_dx[cs];
        int dy = cs_shadow_dy[cs];
        int sc = cs_shadow_color[cs];
        if (radius > 0) {
            gfx2d_rect_round_fill(sx + dx, sy + dy, w, h, radius, sc);
        } else {
            gfx2d_rect_fill(sx + dx, sy + dy, w, h, sc);
        }
    }

    /* 2. Background. CSS canvas-painting rule: if body's bg propagated up
     * to the document canvas (because html has no bg), the body element
     * itself must NOT paint bg again. Anonymous blocks share the parent's
     * cs but have rt_dom == -1, so the suppression key is the dom tag. */
    int bg = cs_bg[cs];
    int suppress_bg = 0;
    if (doc_bg_suppress_body && rt_dom[n] >= 0 &&
        n_tag[rt_dom[n]] == T_BODY) suppress_bg = 1;
    if (bg >= 0 && !suppress_bg) {
        if (radius > 0) {
            gfx2d_rect_round_fill(sx, sy, w, h, radius, bg);
        } else {
            gfx2d_rect_fill(sx, sy, w, h, bg);
        }
    }

    /* 3. Border (1px). With a non-zero radius the rounded outline replaces
     * the four-sided rect-fill. gfx2d_rect_round draws a single-pixel
     * stroke; we omit per-side gating in the rounded case (uniform radius
     * implies uniform border in v1). */
    int has_border = cs_border[cs][0] || cs_border[cs][1] ||
                     cs_border[cs][2] || cs_border[cs][3];
    if (has_border && cs_border_style[cs] != BS_NONE) {
        int bc = cs_border_color[cs];
        int style = cs_border_style[cs];
        if (radius > 0) {
            /* Rounded outline always solid for now. Dashed/dotted on
             * curved corners would need bresenham + dash-state which
             * is more than v1 needs. */
            gfx2d_rect_round(sx, sy, w, h, radius, bc);
        } else if (style == BS_DASHED || style == BS_DOTTED) {
            /* Dashed/dotted: stroke 1px segments along each side.
             * Spec leaves dash length implementation-defined; Chrome
             * uses ~3*width for dashed, 1*width for dotted. We stroke
             * single-pixel rectangles since border width is already
             * clamped to 1px in our paint. */
            int dash = (style == BS_DASHED) ? 4 : 1;
            int gap  = (style == BS_DASHED) ? 4 : 2;
            int step = dash + gap;
            /* top + bottom */
            int xx;
            if (cs_border[cs][0] || cs_border[cs][2]) {
                xx = 0;
                while (xx < w) {
                    int seg = dash;
                    if (xx + seg > w) seg = w - xx;
                    if (cs_border[cs][0])
                        gfx2d_rect_fill(sx + xx, sy, seg, 1, bc);
                    if (cs_border[cs][2])
                        gfx2d_rect_fill(sx + xx, sy + h - 1, seg, 1, bc);
                    xx = xx + step;
                }
            }
            /* left + right */
            int yy;
            if (cs_border[cs][3] || cs_border[cs][1]) {
                yy = 0;
                while (yy < h) {
                    int seg = dash;
                    if (yy + seg > h) seg = h - yy;
                    if (cs_border[cs][3])
                        gfx2d_rect_fill(sx, sy + yy, 1, seg, bc);
                    if (cs_border[cs][1])
                        gfx2d_rect_fill(sx + w - 1, sy + yy, 1, seg, bc);
                    yy = yy + step;
                }
            }
        } else {
            if (cs_border[cs][0]) gfx2d_rect_fill(sx, sy, w, 1, bc);
            if (cs_border[cs][2]) gfx2d_rect_fill(sx, sy + h - 1, w, 1, bc);
            if (cs_border[cs][3]) gfx2d_rect_fill(sx, sy, 1, h, bc);
            if (cs_border[cs][1]) gfx2d_rect_fill(sx + w - 1, sy, 1, h, bc);
        }
    }
}

/* Foreground content (text / replaced / marker / line box). Matches the
 * Blink foreground paint pass. */
void paint_rt_content(int n, int sx, int sy) {
    int kind = rt_kind[n];
    if (kind == RT_TEXT) {
        paint_rt_text(n, sx, sy);
    } else if (kind == RT_REPLACED) {
        paint_rt_replaced(n, sx, sy);
    } else if (kind == RT_LIST_MARKER) {
        paint_rt_marker(n, sx, sy);
    } else if (kind == RT_LINE_BOX) {
        paint_rt_line_box(n, sx, sy);
    }
}

void paint_rt_node(int n) {
    int sx = rt_screen_x(n);
    int sy = rt_screen_y(n);
    int w  = rt_w[n];
    int h  = rt_h[n];

    int cs = rt_style[n];

    /* Off-screen cull */
    if (sy + h < viewport_y()) return;
    if (sy > viewport_y() + viewport_h()) return;

    paint_rt_box_decoration(n, sx, sy, w, h);
    paint_rt_content(n, sx, sy);

    /* Push a clip rect for overflow:hidden so descendant paints are
     * trimmed to the content area. Match Blink's RenderLayer clip-rect
     * behaviour for non-stacking-context overflow clip: clip to the
     * border box (we don't yet inset by border width). */
    int clip_pushed = 0;
    if (cs_overflow[cs] == OVERFLOW_HIDDEN) {
        paint_clip_push(sx, sy, w, h);
        clip_pushed = 1;
    }

    /* Children. Inline subtrees (RT_INLINE/RT_TEXT/RT_INLINE_BLOCK/
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

    if (clip_pushed) paint_clip_pop();
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
    int size_px = cs_font_size_px[cs];
    int face = cs_face_id_for(cs);
    int bold = cs_font_w[cs] >= 700;
    int italic = cs_font_i[cs];
    if (face >= 0 && size_px > 0) {
        int baseline = sy + fontsys_ascent(face, size_px);
        fontsys_draw_run_styled(face, size_px, sx, baseline, buf, len, fg,
                                bold, italic);
    } else {
        gfx2d_text_n(sx, sy, buf, len, fg, font);
    }
    if (cs_text_dec[cs] & TD_UNDERLINE) {
        int uw = (face >= 0 && size_px > 0)
                 ? fontsys_run_width(face, size_px, buf, len)
                 : gfx2d_text_width_n(buf, len, font);
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
            /* Replaced/inline-block reference. Stash document-space x/y on
             * the rt node so paint_rt_node's rt_screen_x/y land it at the
             * line origin: rt_screen_y sums ancestor rt_y values then adds
             * viewport_y-scroll_y, so rt_y[child] = sy - rt_screen_y(parent)
             * makes rt_screen_y(child) == sy exactly. Width/height come
             * from the intrinsic stash; render_tree.cc sets these for
             * <input>, <img>, <button> and rt_alloc zeroes rt_w/h for
             * everything else, so paint_rt_replaced was drawing 0x0
             * invisible boxes (input row "missing or clipped" bug). */
            int rt_n = -la_text_off[k] - 1;
            rt_x[rt_n] = sx + la_x[k] - rt_screen_x(rt_parent[rt_n]);
            rt_y[rt_n] = sy - rt_screen_y(rt_parent[rt_n]);
            if (rt_intrinsic_w[rt_n] > 0) rt_w[rt_n] = rt_intrinsic_w[rt_n];
            if (rt_intrinsic_h[rt_n] > 0) rt_h[rt_n] = rt_intrinsic_h[rt_n];
            paint_rt_node(rt_n);
            continue;
        }
        int fg = la_fg[k];
        if (fg < 0) fg = 0x000000;
        int bg = la_bg[k];
        int line_h = effective_line_h(rt_style[n], tier);
        if (bg >= 0) gfx2d_rect_fill(ax, ay, la_w[k], line_h, bg);
        char buf[256];
        int len = la_text_len[k];
        if (len > 255) len = 255;
        for (int kk = 0; kk < len; kk++) buf[kk] = attr_pool[la_text_off[k] + kk];
        buf[len] = 0;
        int face = la_face_id[k];
        int size_px = la_size_px[k];
        if (face >= 0 && size_px > 0) {
            /* Place baseline so the glyph sits inside the line box with
             * roughly correct ascent above and descent below. The
             * fontsys metrics are the source of truth here - tier_*
             * fallbacks only cover bootstrap atoms with no real face. */
            int asc = fontsys_ascent(face, size_px);
            int line_full = fontsys_line_height(face, size_px);
            int extra = line_h - line_full;
            if (extra < 0) extra = 0;
            int baseline = sy + extra / 2 + asc;
            fontsys_draw_run_styled(face, size_px, ax, baseline,
                                    buf, len, fg,
                                    la_bold[k], la_italic[k]);
            if (la_underline[k] & TD_UNDERLINE) {
                gfx2d_rect_fill(ax, baseline + 2, la_w[k], 1, fg);
            }
            if (la_underline[k] & TD_LINE_THROUGH) {
                /* Stroke at ~mid-cap height so it crosses lowercase x-line. */
                gfx2d_rect_fill(ax, baseline - asc / 2, la_w[k], 1, fg);
            }
        } else {
            int font = tier_to_font(tier);
            gfx2d_text_n(ax, ay, buf, len, fg, font);
            if (la_bold[k]) {
                gfx2d_text_n(ax + 1, ay, buf, len, fg, font);
                if (tier >= 3) {
                    gfx2d_text_n(ax, ay + 1, buf, len, fg, font);
                    gfx2d_text_n(ax + 1, ay + 1, buf, len, fg, font);
                }
            }
            if (la_underline[k] & TD_UNDERLINE) {
                gfx2d_rect_fill(ax, ay + glyph_h, la_w[k], 1, fg);
            }
            if (la_underline[k] & TD_LINE_THROUGH) {
                gfx2d_rect_fill(ax, ay + glyph_h / 2, la_w[k], 1, fg);
            }
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
        int sty = rt_style[n];
        int border_set = (cs_border[sty][0] | cs_border[sty][1] |
                          cs_border[sty][2] | cs_border[sty][3]) != 0;
        /* paint_rt_box_decoration already painted the author border (if
         * any) outside this call. Inset the white fill by 1px on each
         * side that has a border so we don't paint over the stroke and
         * black-hole the control. Without this, `input[type="checkbox"]
         * { border: 2px solid #c00 }` came back invisible since the white
         * fill covered the red stroke. */
        int inset = border_set ? 1 : 0;
        if (is_check || is_radio) {
            int iw = rt_w[n] - 2 * inset;
            int ih = rt_h[n] - 2 * inset;
            if (iw < 0) iw = 0;
            if (ih < 0) ih = 0;
            gfx2d_rect_fill(sx + inset, sy + inset, iw, ih, 0xFFFFFF);
            if (!border_set) {
                gfx2d_rect_fill(sx, sy, rt_w[n], 1, 0x808080);
                gfx2d_rect_fill(sx, sy + rt_h[n] - 1, rt_w[n], 1, 0x808080);
                gfx2d_rect_fill(sx, sy, 1, rt_h[n], 0x808080);
                gfx2d_rect_fill(sx + rt_w[n] - 1, sy, 1, rt_h[n], 0x808080);
            }
            int checked = (rt_dom[n] >= 0) ? n_checkbox_state[rt_dom[n]] : 0;
            if (checked) {
                if (is_check) {
                    gfx2d_text(sx + 3, sy + 2, "x", 0x000000, 0);
                } else {
                    int dx = sx + rt_w[n] / 2 - 2;
                    int dy = sy + rt_h[n] / 2 - 2;
                    gfx2d_rect_fill(dx, dy, 4, 4, 0x000000);
                }
            }
            return;
        }
        /* text-style input */
        int iw = rt_w[n] - 2 * inset;
        int ih = rt_h[n] - 2 * inset;
        if (iw < 0) iw = 0;
        if (ih < 0) ih = 0;
        gfx2d_rect_fill(sx + inset, sy + inset, iw, ih, 0xFFFFFF);
        if (!border_set) {
            gfx2d_rect_fill(sx, sy, rt_w[n], 1, 0x808080);
            gfx2d_rect_fill(sx, sy + rt_h[n] - 1, rt_w[n], 1, 0x808080);
            gfx2d_rect_fill(sx, sy, 1, rt_h[n], 0x808080);
            gfx2d_rect_fill(sx + rt_w[n] - 1, sy, 1, rt_h[n], 0x808080);
        }
        int ii = rt_input_idx[n];
        int is_focused = (focus_mode == FOCUS_INPUT && ii >= 0 &&
                          ii == focused_input);
        if (is_focused) {
            /* Focus ring outside the existing border so it doesn't fight
             * the author's red border. Single-pixel inset blue. */
            gfx2d_rect_fill(sx + 1, sy + 1, rt_w[n] - 2, 1, 0x0066CC);
            gfx2d_rect_fill(sx + 1, sy + rt_h[n] - 2, rt_w[n] - 2, 1, 0x0066CC);
            gfx2d_rect_fill(sx + 1, sy + 1, 1, rt_h[n] - 2, 0x0066CC);
            gfx2d_rect_fill(sx + rt_w[n] - 2, sy + 1, 1, rt_h[n] - 2, 0x0066CC);
        }
        if (ii >= 0) {
            char *iv = input_value + ii * 128;
            int tx = sx + 3 + inset;
            int ty = sy + 2 + inset;
            gfx2d_text(tx, ty, iv, 0x000000, 0);
            if (is_focused) {
                /* Caret at end of text. Width measured via gfx2d so it
                 * tracks bitmap-font advance. */
                int tw = gfx2d_text_width(iv, 0);
                int cx = tx + tw;
                gfx2d_rect_fill(cx, ty, 1, 10, 0x000000);
            }
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
    if (ls == LS_NONE) return;
    /* `list-style-position: outside` (default): marker sits in the
     * padding-left of the parent <ul>, vertically aligned to the first
     * line box's baseline. paint_rt_marker is called with sx = li
     * border-box left, sy = li border-box top; the first line box is
     * laid out at li padding-top, which is 0 in our UA defaults, so
     * sy IS the first line top. Baseline = sy + ascent (matching the
     * formula in paint_rt_line_box). */
    char *glyph = "\xE2\x80\xA2";
    int glyph_len = 3;
    if (ls == LS_DISC)        glyph = "\xE2\x80\xA2";  /* • */
    else if (ls == LS_CIRCLE) glyph = "\xE2\x97\xA6";  /* ◦ */
    else if (ls == LS_SQUARE) glyph = "\xE2\x96\xA0";  /* ■ */
    char dec_buf[16];
    if (ls == LS_DECIMAL) {
        int idx = 1;
        int sib = rt_first_child[rt_parent[parent]];
        while (sib >= 0 && sib != parent) {
            if (rt_kind[sib] == RT_LIST_ITEM) idx++;
            sib = rt_next[sib];
        }
        int p = b_append_int(dec_buf, 0, idx);
        dec_buf[p] = '.'; dec_buf[p + 1] = 0;
        glyph = dec_buf;
        glyph_len = p + 1;
    }
    int face = cs_face_id_for(cs);
    int size_px = cs_font_size_px[cs] > 0 ? cs_font_size_px[cs] : 14;
    if (face >= 0) {
        int gw = fontsys_run_width(face, size_px, glyph, glyph_len);
        if (gw <= 0) gw = size_px / 2;
        int asc = fontsys_ascent(face, size_px);
        if (asc <= 0) asc = size_px - 2;
        /* Right-align the marker into the gap left of the content edge.
         * The 6px gutter mirrors what real browsers leave between
         * marker glyph and text (Blink: ListMarkerPainter.cpp uses an
         * em-relative offset; 6px is a fixed-pixel approximation that
         * looks right for body-sized fonts). */
        fontsys_draw_run_styled(face, size_px,
                                sx - gw - 6, sy + asc,
                                glyph, glyph_len,
                                (unsigned int)fg, 0, 0);
    } else {
        /* No TTF face: fall back to the 8x8 bitmap '*'. */
        gfx2d_text(sx - 12, sy + 2, "*", fg, 0);
    }
}

void draw_address_bar(int sx, int sy, int sw) {
    int bg = (focus_mode == FOCUS_ADDR) ? 0xFFFFE0 : 0xF0F0F0;
    gfx2d_rect_fill(sx, sy, sw, ADDR_H, bg);
    gfx2d_hline(sx, sy + ADDR_H - 1, sw, 0x808080);

    /* Back/forward buttons. 22x20 each, vertically centered in the
     * 28-tall toolbar with 4 px clear top and bottom; left edge
     * starts at x+6 with a 4 px gap between them.  Click hit-test in
     * input.cc:handle_left_click matches these coordinates. */
    int btn_w = 22;
    int btn_h = 20;
    int btn_y = sy + (ADDR_H - btn_h) / 2;        /* 4 */
    int back_x = sx + 6;
    int fwd_x  = back_x + btn_w + 4;              /* 32 */
    int back_enabled = (hist_pos > 1);
    int fwd_enabled  = (hist_pos < hist_count);
    /* Active fill is darker, disabled fill matches toolbar bg so the
     * button reads as flat.  Stroke contrast keeps disabled arrows
     * readable but muted (was 0xC0C0C0, invisible against 0xF0F0F0). */
    int back_fill = back_enabled ? 0xFFFFFF : 0xE8E8E8;
    int fwd_fill  = fwd_enabled  ? 0xFFFFFF : 0xE8E8E8;
    int back_stroke = back_enabled ? 0x202020 : 0x808080;
    int fwd_stroke  = fwd_enabled  ? 0x202020 : 0x808080;
    int border_color = 0x808080;
    gfx2d_rect_fill(back_x, btn_y, btn_w, btn_h, back_fill);
    gfx2d_rect_fill(fwd_x,  btn_y, btn_w, btn_h, fwd_fill);
    /* Centered glyph: "<" / ">" at NORMAL (8x8). Inset to put the
     * baseline visually centered inside a 20-tall button. */
    int glyph_x_back = back_x + (btn_w - 8) / 2;
    int glyph_x_fwd  = fwd_x  + (btn_w - 8) / 2;
    int glyph_y      = btn_y + (btn_h - 8) / 2;
    gfx2d_text(glyph_x_back, glyph_y, "<", back_stroke, 0);
    gfx2d_text(glyph_x_fwd,  glyph_y, ">", fwd_stroke, 0);
    /* 1 px outlines */
    gfx2d_rect_fill(back_x, btn_y, btn_w, 1, border_color);
    gfx2d_rect_fill(back_x, btn_y + btn_h - 1, btn_w, 1, border_color);
    gfx2d_rect_fill(back_x, btn_y, 1, btn_h, border_color);
    gfx2d_rect_fill(back_x + btn_w - 1, btn_y, 1, btn_h, border_color);
    gfx2d_rect_fill(fwd_x,  btn_y, btn_w, 1, border_color);
    gfx2d_rect_fill(fwd_x,  btn_y + btn_h - 1, btn_w, 1, border_color);
    gfx2d_rect_fill(fwd_x,  btn_y, 1, btn_h, border_color);
    gfx2d_rect_fill(fwd_x + btn_w - 1, btn_y, 1, btn_h, border_color);

    /* URL label + value baseline-aligned with the button text:
     *   text height 8 → top = sy + (ADDR_H - 8)/2 = 10. */
    int label_x = fwd_x + btn_w + 8;              /* 64 */
    int text_y  = sy + (ADDR_H - 8) / 2;
    gfx2d_text(label_x, text_y, "URL:", 0x404040, 0);
    char tmp[128];
    int ml = addr_len;
    if (ml > 127) ml = 127;
    int k = 0;
    while (k < ml) { tmp[k] = addr_buf[k]; k = k + 1; }
    tmp[ml] = 0;
    int field_x = label_x + 4 * char_w + 4;
    gfx2d_text(field_x, text_y, tmp, 0x000000, 0);
    if (focus_mode == FOCUS_ADDR) {
        int cx = field_x + addr_cursor * char_w;
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

    /* viewport (clipped) - paint the render tree into the content area.
     * paint_clip_init seeds the userland clip stack so OVERFLOW_HIDDEN
     * pushes intersect with the viewport rect. */
    int vx = cx + viewport_x();
    int vy = cy + viewport_y();
    paint_clip_init(vx, vy, cur_cw - 12, viewport_h());

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
