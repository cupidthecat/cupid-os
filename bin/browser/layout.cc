/* Layout.
 *
 * Render-tree BFC + IFC layout. Walks the rt_* arrays and resolves
 * rt_x/y/w/h plus rt_content_x/y per node. Inline content goes
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

/* §4.x Line-break punctuation rules adapted from
 * blink/Source/core/rendering/break_lines.cpp. Returns 1 if it is OK to
 * break the line BEFORE the given character (i.e. the char can sit at the
 * start of a fresh line). Most ASCII letters/digits are breakable; the
 * NO_BREAK_BEFORE set is the closing punctuation that should hug the end
 * of the previous line. */
int can_break_before(char c) {
    if (c == '.' || c == ',' || c == ';' || c == ':' ||
        c == '!' || c == '?' ||
        c == ')' || c == ']' || c == '}' ||
        c == '\'' || c == '"') return 0;
    return 1;
}

/* Returns 1 if it is OK to break AFTER `c`. Opening punctuation never gets
 * stranded at end-of-line. */
int can_break_after(char c) {
    if (c == '(' || c == '[' || c == '{' ||
        c == '$' || c == '#' || c == '@') return 0;
    return 1;
}

/* Resolve the CSS font shorthand on `cs` to a fontsys face_id. Copies
 * the verbatim font-family value out of css_value_pool into a local
 * buffer (NUL-terminated) so it can be passed to fontsys_match.   */
int cs_face_id_for(int cs) {
    char fam[200];
    int  fam_len = 0;
    int  off = cs_font_family_off[cs];
    int  len = cs_font_family_len[cs];
    if (off >= 0 && len > 0) {
        if (len > 199) len = 199;
        for (int i = 0; i < len; i = i + 1) fam[i] = css_value_pool[off + i];
        fam[len] = 0;
        fam_len = len;
    } else {
        fam[0] = 0;
    }
    int gen = cs_font_generic[cs];
    int weight = cs_font_w[cs];
    int italic = cs_font_i[cs];
    /* §2.x Webfonts (font_face.cc) take priority over the kernel's
     * preinstalled faces. font_face_match scans every comma-separated
     * family name; bare fallback path goes through fontsys_match. */
    if (fam_len > 0) {
        int wf = font_face_match(fam, fam_len, weight, italic);
        if (wf >= 0) return wf;
    }
    return fontsys_match(fam_len > 0 ? fam : "", gen, weight, italic);
}

int text_slice_w(int off, int len, int tier) {
    return gfx2d_text_width_n(attr_pool + off, len, tier_to_font(tier));
}

/* fontsys-aware width: picks the real face/size for `cs` and asks
 * fontsys for the run width. Falls back to the tier path when size_px
 * is unavailable (e.g. bootstrap atoms with no resolved cs). */
int text_slice_w_cs(int cs, int off, int len) {
    if (cs >= 0 && cs < cs_count && cs_font_size_px[cs] > 0) {
        int face = cs_face_id_for(cs);
        if (face >= 0) {
            return fontsys_run_width(face, cs_font_size_px[cs],
                                     attr_pool + off, len);
        }
    }
    return gfx2d_text_width_n(attr_pool + off, len,
                              tier_to_font(cs_font_size_tier[cs]));
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
    int italic = cs_font_i[cs];
    int size_px = cs_font_size_px[cs];
    int face_id = cs_face_id_for(cs);
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
                int rw = text_slice_w_cs(cs, off + s, run_len);
                if (bold && face_id >= 0 && size_px > 0) rw += run_len;
                la_w[la_count] = rw;
                la_font_tier[la_count] = tier;
                la_size_px[la_count] = size_px;
                la_face_id[la_count] = face_id;
                la_italic[la_count] = italic;
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
                    la_size_px[la_count] = size_px;
                    la_face_id[la_count] = face_id;
                    la_italic[la_count] = italic;
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
     * word atoms. Soft-break sentinels respect Blink's break_lines.cpp
     * punctuation rules so commas and closing brackets don't get stranded
     * at the start of a wrapped line. */
    char prev_last_char = 0;
    while (i < len) {
        /* skip leading whitespace, emit a single space atom if any (only between
         * word atoms - leading at start of run handled by line layout). The
         * space atom is emitted BEFORE checking for end-of-text so trailing
         * whitespace is preserved and connects to the next inline atom run
         * (e.g. "Visit " followed by <a>More...</a> keeps its inter-element
         * space). */
        int saw_ws = 0;
        while (i < len) {
            char c = attr_pool[off + i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; saw_ws = 1; }
            else break;
        }
        if (saw_ws && la_count > 0 && la_count < MAX_LINE_ATOMS) {
            /* Inter-word space atom: text_len=1, text_off pointing at a literal " ". */
            la_text_off[la_count] = attr_intern(" ", 1);
            la_text_len[la_count] = 1;
            /* Space advance: must match the surrounding word atoms' face/size
             * exactly, otherwise the visible gap between words drifts off the
             * font's natural inter-word width and pages like example.com
             * collapse to "ExampleDomain". Use fontsys_advance directly (no
             * raster path - hmtx lookup) on the same face_id/size_px as the
             * word atoms. Fallbacks: gfx2d_glyph_advance on bitmap path, then
             * a 1/4-em estimate so the space is never zero. */
            int sp_w = 0;
            if (face_id >= 0 && size_px > 0) {
                sp_w = fontsys_advance(face_id, ' ', size_px);
            }
            if (sp_w <= 0) sp_w = gfx2d_glyph_advance(' ', tier_to_font(tier));
            if (sp_w <= 0) sp_w = (size_px > 0) ? (size_px / 4 + 1) : 4;
            /* Bold pen advances +1 for the space too, match it. */
            if (bold && face_id >= 0 && size_px > 0) sp_w += 1;
            la_w[la_count] = sp_w;
            la_font_tier[la_count] = tier;
            la_size_px[la_count] = size_px;
            la_face_id[la_count] = face_id;
            la_italic[la_count] = italic;
            la_fg[la_count] = fg; la_bg[la_count] = bg;
            la_bold[la_count] = bold; la_underline[la_count] = underline;
            la_link_idx[la_count] = link_idx;
            /* Soft-break point unless surrounding punctuation forbids it. */
            int breakable = (ws != WS_NOWRAP);
            if (breakable && prev_last_char != 0 && !can_break_after(prev_last_char))
                breakable = 0;
            if (breakable && i < len && !can_break_before(attr_pool[off + i]))
                breakable = 0;
            la_x[la_count] = breakable ? -3 : -1;
            la_count++;
        }
        if (i >= len) break;
        /* word atom */
        int s = i;
        while (i < len) {
            char c = attr_pool[off + i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
            i++;
        }
        int wlen = i - s;
        if (wlen > 0) prev_last_char = attr_pool[off + i - 1];
        if (wlen > 0 && la_count < MAX_LINE_ATOMS) {
            la_text_off[la_count] = off + s;
            la_text_len[la_count] = wlen;
            int ww = text_slice_w_cs(cs, off + s, wlen);
            /* Synthetic bold paints each glyph twice with a +1 horizontal
             * smear and advances the pen by +1 per char (fontsys.c
             * fontsys_draw_run_styled). The measured width must include
             * that extra or the bold run overflows into the trailing
             * space atom and "Example Domain" collapses to "ExampleDomain"
             * for h1/strong/b. */
            if (bold && face_id >= 0 && size_px > 0) ww += wlen;
            la_w[la_count] = ww;
            la_font_tier[la_count] = tier;
            la_size_px[la_count] = size_px;
            la_face_id[la_count] = face_id;
            la_italic[la_count] = italic;
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
            la_size_px[la_count] = 0;
            la_face_id[la_count] = -1;
            la_italic[la_count] = 0;
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

        /* Wrap-on-overflow: if this atom would push the line past max_w and
         * the line has something on it, close the line. For a -3 (soft break
         * point / space) atom we drop it; for a word atom we place it as the
         * first atom of the new line. We never wrap mid-word, so a single
         * word longer than max_w still overflows on its own line. */
        int overflow = (x + aw > cx + max_w && x > cx);
        if (overflow) {
            int lh_wrap = line_h ? line_h : effective_line_h(rt_style[parent], tier);
            int lb = rt_alloc(RT_LINE_BOX, -1, parent, rt_style[parent]);
            if (lb >= 0) {
                rt_line_atom_first[lb] = line_start_atom;
                rt_line_atom_count[lb] = k - line_start_atom;
                rt_x[lb] = cx;
                rt_y[lb] = line_top;
                rt_w[lb] = max_w;
                rt_h[lb] = lh_wrap;
            }
            line_top += lh_wrap;
            x = cx;
            line_h = 0;
            if (sentinel == -3) {
                line_start_atom = k + 1;    /* drop the trailing space atom */
                continue;
            }
            line_start_atom = k;            /* word starts the new line */
        }
        la_x[k] = x;
        x += aw;
        int lh = effective_line_h(rt_style[parent], tier);
        /* Replaced/inline-block atoms (la_text_off encoded as a negative
         * RT-node ref) bring their own height. The line must grow to
         * fit, otherwise the next line overlaps the bottom of an input
         * or image. */
        if (la_text_off[k] < 0) {
            int rt_n = -la_text_off[k] - 1;
            int repl_h = rt_intrinsic_h[rt_n];
            if (repl_h <= 0) repl_h = rt_h[rt_n];
            if (repl_h > lh) lh = repl_h;
        }
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
    int sty = rt_style[n];
    int style_w = cs_width[sty];
    int w;
    if (style_w >= 0) w = style_w;
    else w = avail_w - rt_margin_l(n) - rt_margin_r(n);
    if (w < 0) w = 0;
    /* min/max-width clamps. min-width wins over max-width per CSS spec. */
    int max_w_clamp = cs_max_width[sty];
    int min_w_clamp = cs_min_width[sty];
    if (max_w_clamp >= 0 && w > max_w_clamp) w = max_w_clamp;
    if (min_w_clamp >= 0 && w < min_w_clamp) w = min_w_clamp;
    rt_w[n] = w;

    int content_w = w - rt_padding_l(n) - rt_padding_r(n)
                    - rt_border_l(n) - rt_border_r(n);
    if (content_w < 0) content_w = 0;

    int cx = rt_padding_l(n) + rt_border_l(n);
    int cy = rt_padding_t(n) + rt_border_t(n);
    rt_content_x[n] = cx;
    rt_content_y[n] = cy;

    /* Table-row: lay out cells horizontally, equal-share width, equalize
     * cell heights to the row's max so borders line up. Anonymous table
     * row groups (tbody) flow normally and let each tr handle its own
     * cells. Real table layout (col widths from intrinsic sizing) is a
     * future stage; this is a "good enough" pass for hand-written tables. */
    if (rt_kind[n] == RT_TABLE_ROW) {
        int ncells = 0;
        int c0 = rt_first_child[n];
        while (c0 >= 0) {
            if (rt_kind[c0] == RT_TABLE_CELL) ncells = ncells + 1;
            c0 = rt_next[c0];
        }
        if (ncells > 0) {
            int cell_w = content_w / ncells;
            if (cell_w < 1) cell_w = 1;
            int x = cx;
            int max_h = 0;
            int c = rt_first_child[n];
            while (c >= 0) {
                if (rt_kind[c] == RT_TABLE_CELL) {
                    layout_block(c, cell_w);
                    rt_x[c] = x;
                    rt_y[c] = cy;
                    if (rt_h[c] > max_h) max_h = rt_h[c];
                    x = x + rt_w[c];
                }
                c = rt_next[c];
            }
            /* Equalize cell heights to row height */
            c = rt_first_child[n];
            while (c >= 0) {
                if (rt_kind[c] == RT_TABLE_CELL) rt_h[c] = max_h;
                c = rt_next[c];
            }
            cy = cy + max_h;
            int style_h = cs_height[sty];
            rt_h[n] = (style_h >= 0) ? style_h
                                     : (cy + rt_padding_b(n) + rt_border_b(n));
            return;
        }
    }

    /* Walk children. Inline runs get accumulated and flushed when a block
     * sibling appears or end of children.
     *
     * CSS2.1 §8.3.1 vertical margin collapsing. Adjacent in-flow margins
     * combine into a "strut" (positive_max, negative_min); the visible
     * gap is positive_max + negative_min so a -10 followed by +20 yields
     * 10, and a +6 followed by +12 yields 12. Cases handled in this pass:
     *   - adjacent siblings (current block's margin-t with prior block's
     *     margin-b);
     *   - parent + first in-flow child (zero padding-t/border-t);
     *   - parent + last in-flow child (zero padding-b/border-b, height auto);
     *   - self-collapsing block: a block with no in-flow content and zero
     *     padding/border combines its own top + bottom margins together.
     * Floats and clearance break collapse — deferred to B2. */
    int pile_first = la_count;
    int pile_count = 0;
    int pend_pos = 0;
    int pend_neg = 0;
    /* If true, the next block child's margin-top collapses through the
     * parent's top edge instead of advancing cy. Cleared after first block
     * runs OR after any inline pile flushes (inline content breaks chain). */
    int collapse_first_top = (rt_padding_t(n) == 0 && rt_border_t(n) == 0);
    /* Track top-edge collapsed strut when collapse_first_top swallows a
     * child's margin-t. Currently observed by parent-first-child only;
     * full propagation to grandparent (so parent's outer margin-top wins
     * over child's) is deferred. */
    (void)collapse_first_top;
    /* For parent-last-child collapse: remember if the last in-flow child
     * was a block, and what its bottom margin contribution was. Then
     * after the loop, decide whether to add it to cy or skip it. */
    int last_was_block = 0;
    int collapse_last_allowed = (rt_padding_b(n) == 0 && rt_border_b(n) == 0
                                  && cs_height[sty] < 0);
    int saved_pos = 0;
    int saved_neg = 0;

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
                pend_pos = 0; pend_neg = 0;
                collapse_first_top = 0;
                last_was_block = 0;
            }
            /* Lay out block child */
            int cy_before = cy;
            int child_avail = content_w;
            layout_block(c, child_avail);
            int ml = rt_margin_l(c);
            int mr = rt_margin_r(c);
            int auto_l = cs_margin_auto[rt_style[c]][3];
            int auto_r = cs_margin_auto[rt_style[c]][1];
            int leftover = content_w - rt_w[c];
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
            (void)cy_before;
            int top = rt_margin_t(c);
            if (collapse_first_top) top = 0;
            /* Combine pending strut with this child's margin-top. */
            int t_pos = pend_pos;
            int t_neg = pend_neg;
            if (top > t_pos) t_pos = top;
            if (top < t_neg) t_neg = top;
            cy = cy + t_pos + t_neg;
            collapse_first_top = 0;
            int child_x = cx + ml;
            if (child_x < cx) child_x = cx;
            rt_x[c] = child_x;
            rt_y[c] = cy;
            cy = cy + rt_h[c];
            /* Detect self-collapsing block: child took zero space (no
             * in-flow content) AND has no padding/border. Its margins
             * top+bottom collapse together as a single strut. */
            int c_sty = rt_style[c];
            int self_collapsing =
                (rt_h[c] == 0) &&
                (cs_padding[c_sty][0] == 0) && (cs_padding[c_sty][2] == 0) &&
                (cs_border [c_sty][0] == 0) && (cs_border [c_sty][2] == 0) &&
                (cs_height [c_sty] < 0 || cs_height[c_sty] == 0);
            if (self_collapsing) {
                /* Combine bottom margin into the same strut as the top
                 * we already folded in: undo the cy advance for the top
                 * and re-form a combined strut spanning [t_pos,t_neg] +
                 * margin-b. */
                cy = cy - (t_pos + t_neg);
                int bot = rt_margin_b(c);
                if (bot > t_pos) t_pos = bot;
                if (bot < t_neg) t_neg = bot;
                pend_pos = t_pos;
                pend_neg = t_neg;
            } else {
                int bot = rt_margin_b(c);
                pend_pos = (bot > 0) ? bot : 0;
                pend_neg = (bot < 0) ? bot : 0;
            }
            saved_pos = pend_pos;
            saved_neg = pend_neg;
            last_was_block = 1;
        } else {
            /* Inline / text / inline-block / replaced: accumulate as atoms */
            collect_inline_atoms(c);
            pile_count = la_count - pile_first;
        }
        c = rt_next[c];
    }
    if (pile_count > 0) {
        flush_inline(n, &pile_first, &pile_count, cx, &cy, content_w);
        pend_pos = 0; pend_neg = 0;
        last_was_block = 0;
    }
    /* Parent-last-child margin collapse: when the last in-flow block child's
     * bottom margin would be flush against the parent's bottom content edge
     * AND the parent has no padding-b/border-b AND height is auto, that
     * margin collapses through to the parent's outer bottom margin (i.e.,
     * does not contribute to rt_h[n]). v1 heuristic: just suppress the
     * margin from cy (full propagation up to grandparent's adjacent strut
     * is a B2/B3 follow-up). */
    if (!(last_was_block && collapse_last_allowed)) {
        cy = cy + pend_pos + pend_neg;
    } else {
        /* swallowed by parent's bottom edge */
        (void)saved_pos; (void)saved_neg;
    }

    /* Resolve own height */
    int style_h = cs_height[sty];
    if (style_h >= 0) {
        rt_h[n] = style_h;
    } else {
        rt_h[n] = cy + rt_padding_b(n) + rt_border_b(n);
    }
    /* min/max-height clamps */
    int max_h_clamp = cs_max_height[sty];
    int min_h_clamp = cs_min_height[sty];
    if (max_h_clamp >= 0 && rt_h[n] > max_h_clamp) rt_h[n] = max_h_clamp;
    if (min_h_clamp >= 0 && rt_h[n] < min_h_clamp) rt_h[n] = min_h_clamp;
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
