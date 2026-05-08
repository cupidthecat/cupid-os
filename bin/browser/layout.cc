/* Layout.
 *
 * Render-tree BFC + IFC layout. Walks the rt_* arrays filled in Task 6 and
 * resolves rt_x/y/w/h plus rt_content_x/y per node. Inline content goes
 * through line-box layout (atoms + flush_inline + LINE_BOX nodes). Paint
 * and hit-test consume rt_* directly (no intermediate flat box list).
 */

/* Linear scan: which inputs[] index, if any, was registered for this
 * DOM node? -1 if this <input> is not in the editable inputs[] table
 * (e.g., submit/button/hidden - those render as a submit button instead). */
int find_input_for_node(int idx) {
    for (int k = 0; k < inputs_count; k = k + 1) {
        if (input_node[k] == idx) return k;
    }
    return -1;
}

/* §4 BFC + IFC layout: fills rt_x/y/w/h/content_x/y for every render node. */

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

/* §4 IFC - line-box layout. Walks an inline subtree depth-first, splits text
 * into atoms by whitespace (or by \n / per-char per `white-space`), tracks
 * (x, line_top, line_h), and emits LINE_BOX render nodes whose
 * rt_line_atom_first/count reference the atom slice. */

/* Tier -> real on-screen font selection. The kernel exposes three sizes:
 *   GFX2D_FONT_SMALL  (0) = 6x8,
 *   GFX2D_FONT_NORMAL (1) = 8x8,
 *   GFX2D_FONT_LARGE  (2) = 16x16 (font_8x8 scaled 2x).
 * tier_line_h is the line-box height (independent of glyph advance);
 * actual horizontal widths come from gfx2d_text_width_n which sums real
 * per-glyph advances. */
int tier_to_font(int tier) {
    if (tier == 0) return 0;             /* SMALL */
    if (tier <= 2) return 1;             /* NORMAL */
    return 2;                            /* LARGE */
}
int tier_char_w(int tier) {
    /* Fallback fixed advance for callers that don't have the bytes (e.g.
     * estimating "word" widths in tests). Real layout & paint use
     * gfx2d_text_width_n / gfx2d_glyph_advance directly. */
    if (tier == 0) return 6;
    if (tier <= 2) return 8;
    return 16;
}
int tier_line_h(int tier) {
    /* ~1.5x glyph height for readable line spacing. */
    if (tier == 0) return 12;
    if (tier == 1) return 18;          /* body 8x8 with airy spacing */
    if (tier == 2) return 20;
    if (tier == 3) return 26;
    return 32;                         /* tier 4 (h1) gets extra room */
}

/* Resolve effective line height for a node given its computed style and the
 * tier in play (atoms can carry different tiers from the parent block). When
 * no `line-height` was specified, fall back to tier_line_h. */
int effective_line_h(int cs, int tier) {
    if (cs < 0 || cs >= cs_count) return tier_line_h(tier);
    int lh = cs_line_height[cs];
    if (lh < 0) return tier_line_h(tier);
    if (cs_line_height_mult[cs]) {
        int base = tier_line_h(tier);
        int v = (base * lh) / 100;
        if (v < 1) v = 1;
        return v;
    }
    return lh;
}

int text_slice_w(int off, int len, int tier) {
    return gfx2d_text_width_n(attr_pool + off, len, tier_to_font(tier));
}

/* Walks the inline subtree rooted at `n` (which is RT_INLINE / RT_TEXT /
 * RT_INLINE_BLOCK / RT_REPLACED). Appends one or more entries to la_*[].
 * White-space behaviour from cs_white_space[rt_style[n]]: NORMAL collapses
 * runs of \s to single space, breaks anywhere; PRE preserves all whitespace,
 * splits on \n; NOWRAP collapses but never breaks. */
void emit_text_atoms(int rt_text_n, int parent_rt) {
    (void)parent_rt;
    int cs = rt_style[rt_text_n];
    int ws = cs_white_space[cs];
    int tier = cs_font_size_tier[cs];
    int fg = cs_color[cs];
    int bg = cs_bg[cs];
    int bold = cs_font_w[cs] >= 700;
    int underline = (cs_text_dec[cs] & TD_UNDERLINE) ? 1 : 0;
    int link_idx = -1;
    /* Walk up looking for a parent with rt_link_idx >= 0 */
    int p = rt_text_n;
    while (p >= 0) {
        if (rt_link_idx[p] >= 0) { link_idx = rt_link_idx[p]; break; }
        p = rt_parent[p];
    }

    int off = rt_text_off[rt_text_n];
    int len = rt_text_len[rt_text_n];
    int i = 0;

    if (ws == WS_PRE) {
        /* Split on \n; each line is one atom. */
        int s = 0;
        while (s < len) {
            int e = s;
            while (e < len && attr_pool[off + e] != '\n') e++;
            int run_len = e - s;
            if (la_count < MAX_LINE_ATOMS) {
                la_text_off[la_count] = off + s;
                la_text_len[la_count] = run_len;
                la_w[la_count] = text_slice_w(off + s, run_len, tier);
                la_font_tier[la_count] = tier;
                la_fg[la_count] = fg; la_bg[la_count] = bg;
                la_bold[la_count] = bold; la_underline[la_count] = underline;
                la_link_idx[la_count] = link_idx;
                la_x[la_count] = -1;     /* filled at flush time */
                la_count++;
            }
            if (e < len) {
                /* Sentinel \n atom: text_len = 0 means hard break */
                if (la_count < MAX_LINE_ATOMS) {
                    la_text_off[la_count] = off + e;
                    la_text_len[la_count] = 0;
                    la_w[la_count] = 0;
                    la_font_tier[la_count] = tier;
                    la_fg[la_count] = fg; la_bg[la_count] = bg;
                    la_bold[la_count] = bold; la_underline[la_count] = underline;
                    la_link_idx[la_count] = link_idx;
                    la_x[la_count] = -2;    /* hard break sentinel */
                    la_count++;
                }
                s = e + 1;
            } else {
                s = e;
            }
        }
        return;
    }

    /* WS_NORMAL or WS_NOWRAP: collapse whitespace to single spaces, split into
     * word atoms. */
    while (i < len) {
        /* skip leading whitespace, emit a single space atom if any (only between
         * word atoms - leading at start of run handled by line layout). */
        int saw_ws = 0;
        while (i < len) {
            char c = attr_pool[off + i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; saw_ws = 1; }
            else break;
        }
        if (i >= len) break;
        if (saw_ws && la_count > 0 && la_count < MAX_LINE_ATOMS) {
            /* Inter-word space atom: text_len=1, text_off pointing at a literal " ". */
            la_text_off[la_count] = attr_intern(" ", 1);
            la_text_len[la_count] = 1;
            la_w[la_count] = gfx2d_glyph_advance(' ', tier_to_font(tier));
            la_font_tier[la_count] = tier;
            la_fg[la_count] = fg; la_bg[la_count] = bg;
            la_bold[la_count] = bold; la_underline[la_count] = underline;
            la_link_idx[la_count] = link_idx;
            la_x[la_count] = (ws == WS_NOWRAP) ? -1 : -3;  /* -3 = soft break point */
            la_count++;
        }
        /* word atom */
        int s = i;
        while (i < len) {
            char c = attr_pool[off + i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
            i++;
        }
        int wlen = i - s;
        if (wlen > 0 && la_count < MAX_LINE_ATOMS) {
            la_text_off[la_count] = off + s;
            la_text_len[la_count] = wlen;
            la_w[la_count] = text_slice_w(off + s, wlen, tier);
            la_font_tier[la_count] = tier;
            la_fg[la_count] = fg; la_bg[la_count] = bg;
            la_bold[la_count] = bold; la_underline[la_count] = underline;
            la_link_idx[la_count] = link_idx;
            la_x[la_count] = -1;
            la_count++;
        }
    }
}

void collect_inline_atoms(int n) {
    int kind = rt_kind[n];
    if (kind == RT_TEXT) {
        emit_text_atoms(n, rt_parent[n]);
        return;
    }
    if (kind == RT_INLINE) {
        int c = rt_first_child[n];
        while (c >= 0) {
            collect_inline_atoms(c);
            c = rt_next[c];
        }
        return;
    }
    if (kind == RT_INLINE_BLOCK || kind == RT_REPLACED) {
        /* For replaced/inline-block: lay out as a mini block, then atom carries
         * the resulting w/h. */
        int avail = viewport_content_w();
        if (kind == RT_INLINE_BLOCK) layout_block(n, avail);
        int w = (rt_intrinsic_w[n] > 0) ? rt_intrinsic_w[n] : rt_w[n];
        int h = (rt_intrinsic_h[n] > 0) ? rt_intrinsic_h[n] : rt_h[n];
        (void)h;
        if (la_count < MAX_LINE_ATOMS) {
            la_text_off[la_count] = -n - 1;     /* negative encodes "RT node ref"; -n-1 reverses to n */
            la_text_len[la_count] = 0;
            la_w[la_count] = w;
            la_font_tier[la_count] = 0;
            la_fg[la_count] = 0; la_bg[la_count] = 0;
            la_bold[la_count] = 0; la_underline[la_count] = 0;
            la_link_idx[la_count] = rt_link_idx[n];
            la_x[la_count] = -1;
            la_count++;
        }
        return;
    }
}

void flush_inline(int parent, int *atom_pile_first, int *atom_pile_count,
                  int cx, int *cy, int max_w) {
    int first = *atom_pile_first;
    int total = *atom_pile_count;
    if (total == 0) return;

    int line_start_atom = first;
    int x = cx;
    int line_h = 0;
    int line_top = *cy;

    for (int k = first; k < first + total; k++) {
        int aw = la_w[k];
        int tier = la_font_tier[k];
        int sentinel = la_x[k];

        if (sentinel == -2) {
            /* Hard break (PRE) */
            la_x[k] = x;
            int lb = rt_alloc(RT_LINE_BOX, -1, parent, rt_style[parent]);
            if (lb >= 0) {
                rt_line_atom_first[lb] = line_start_atom;
                rt_line_atom_count[lb] = (k + 1) - line_start_atom;
                rt_x[lb] = cx;
                rt_y[lb] = line_top;
                rt_w[lb] = max_w;
                int lh = line_h ? line_h : effective_line_h(rt_style[parent], tier);
                rt_h[lb] = lh;
            }
            line_top += line_h ? line_h : effective_line_h(rt_style[parent], tier);
            x = cx;
            line_h = 0;
            line_start_atom = k + 1;
            continue;
        }

        /* Wrap if this atom doesn't fit and we have something on the line */
        if (sentinel == -3 && x + aw > cx + max_w && x > cx) {
            int lb = rt_alloc(RT_LINE_BOX, -1, parent, rt_style[parent]);
            if (lb >= 0) {
                rt_line_atom_first[lb] = line_start_atom;
                rt_line_atom_count[lb] = k - line_start_atom;
                rt_x[lb] = cx;
                rt_y[lb] = line_top;
                rt_w[lb] = max_w;
                rt_h[lb] = line_h;
            }
            line_top += line_h;
            x = cx;
            line_h = 0;
            line_start_atom = k + 1;        /* drop the trailing space atom */
            continue;
        }
        if (x + aw > cx + max_w && x > cx && sentinel != -3) {
            /* Mid-word wrap not allowed in v1 - but we accept overflow rather
             * than truncate */
        }
        la_x[k] = x;
        x += aw;
        int lh = effective_line_h(rt_style[parent], tier);
        if (lh > line_h) line_h = lh;
    }

    /* Flush remaining */
    if (line_start_atom < first + total) {
        int lb = rt_alloc(RT_LINE_BOX, -1, parent, rt_style[parent]);
        if (lb >= 0) {
            rt_line_atom_first[lb] = line_start_atom;
            rt_line_atom_count[lb] = (first + total) - line_start_atom;
            rt_x[lb] = cx;
            rt_y[lb] = line_top;
            rt_w[lb] = max_w;
            rt_h[lb] = line_h ? line_h : 12;
        }
        line_top += line_h ? line_h : 12;
    }

    *cy = line_top;
    *atom_pile_first = la_count;
    *atom_pile_count = 0;
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
     * sibling appears or end of children.
     *
     * Vertical margin collapsing between block siblings: the gap between
     * two adjacent in-flow blocks is max(prev.margin_b, next.margin_t),
     * not the additive sum. We track pending_bottom as the unspent
     * bottom margin from the previous block; the next block's effective
     * top margin is max(pending_bottom, margin_t). Inline content
     * between blocks (an inline pile flush) breaks the collapse chain
     * by resetting pending_bottom. */
    int pile_first = la_count;
    int pile_count = 0;
    int pending_bottom = 0;

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
            /* Flush pending inline run; inline content breaks the
             * margin-collapse chain. */
            if (pile_count > 0) {
                flush_inline(n, &pile_first, &pile_count, cx, &cy, content_w);
                pending_bottom = 0;
            }
            /* Lay out block child */
            int child_avail = content_w;
            layout_block(c, child_avail);
            int ml = rt_margin_l(c);
            int mr = rt_margin_r(c);
            int auto_l = cs_margin_auto[rt_style[c]][3];
            int auto_r = cs_margin_auto[rt_style[c]][1];
            int leftover = content_w - rt_w[c];
            /* Clamp: a box wider than its parent's content area must not
             * gain negative auto margins (they would push it off-screen). */
            if (leftover < 0) leftover = 0;
            if (leftover > 0 && (auto_l || auto_r)) {
                if (auto_l && auto_r) {
                    ml = leftover / 2;
                    mr = leftover - ml;
                } else if (auto_l) {
                    int rem = leftover - mr;
                    ml = rem > 0 ? rem : 0;
                } else {
                    int rem = leftover - ml;
                    mr = rem > 0 ? rem : 0;
                }
            }
            (void)mr;
            int top = rt_margin_t(c);
            int collapsed = (top > pending_bottom) ? top : pending_bottom;
            cy = cy + collapsed;
            int child_x = cx + ml;
            /* Clamp child_x to the content origin so an overflowed box
             * (rt_w > content_w with explicit width) doesn't get pushed
             * left of the parent's content area by negative ml from
             * earlier auto fallbacks. */
            if (child_x < cx) child_x = cx;
            rt_x[c] = child_x;
            rt_y[c] = cy;
            cy = cy + rt_h[c];
            pending_bottom = rt_margin_b(c);
        } else {
            /* Inline / text / inline-block / replaced: accumulate as atoms */
            collect_inline_atoms(c);
            pile_count = la_count - pile_first;
        }
        c = rt_next[c];
    }
    if (pile_count > 0) {
        flush_inline(n, &pile_first, &pile_count, cx, &cy, content_w);
        pending_bottom = 0;
    }
    /* Trailing block child's bottom margin counts toward parent height
     * (parent-last-child collapsing is deferred). */
    cy = cy + pending_bottom;

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
