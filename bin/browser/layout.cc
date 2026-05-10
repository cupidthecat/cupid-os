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
 * no `line-height` was specified, fall back to fontsys_line_height so real
 * TTF metrics drive vertical rhythm at non-tier sizes; tier_line_h survives
 * only for the bitmap-only path (no resolved face/size). */
int effective_line_h(int cs, int tier) {
    if (cs < 0 || cs >= cs_count) return tier_line_h(tier);
    int lh = cs_line_height[cs];
    int px = cs_font_size_px[cs];
    int natural = tier_line_h(tier);
    if (px > 0) {
        int face = cs_face_id_for(cs);
        if (face >= 0) {
            int n = fontsys_line_height(face, px);
            if (n > 0) natural = n;
        } else {
            natural = (px * 12) / 10;
        }
        if (natural < tier_line_h(tier)) natural = tier_line_h(tier);
    }
    if (lh < 0) return natural;
    if (cs_line_height_mult[cs]) {
        int v = (natural * lh) / 100;
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
/* Decode one UTF-8 codepoint from `p[0..len-1]`. Returns the byte
 * advance (1..4) and stores the codepoint in *cp. On a malformed lead
 * byte returns 1 with *cp = U+FFFD so callers can keep walking. Returns
 * 0 only when len <= 0. */
int utf8_decode_one(char *p, int len, int *cp) {
    if (len <= 0) { *cp = 0; return 0; }
    unsigned char b0 = (unsigned char)p[0];
    if (b0 < 0x80) { *cp = (int)b0; return 1; }
    if ((b0 & 0xE0) == 0xC0 && len >= 2) {
        unsigned char b1 = (unsigned char)p[1];
        if ((b1 & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        *cp = ((int)(b0 & 0x1F) << 6) | (int)(b1 & 0x3F);
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0 && len >= 3) {
        unsigned char b1 = (unsigned char)p[1];
        unsigned char b2 = (unsigned char)p[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        *cp = ((int)(b0 & 0x0F) << 12) | ((int)(b1 & 0x3F) << 6) | (int)(b2 & 0x3F);
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0 && len >= 4) {
        unsigned char b1 = (unsigned char)p[1];
        unsigned char b2 = (unsigned char)p[2];
        unsigned char b3 = (unsigned char)p[3];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) {
            *cp = 0xFFFD; return 1;
        }
        *cp = ((int)(b0 & 0x07) << 18) | ((int)(b1 & 0x3F) << 12) |
              ((int)(b2 & 0x3F) << 6)  | (int)(b3 & 0x3F);
        return 4;
    }
    *cp = 0xFFFD;
    return 1;
}

/* Resolve the CSS font shorthand on `cs` to a fontsys face_id, optionally
 * filtered by a unicode-range that must cover `cp`. Pass cp == -1 to
 * skip range filtering (back-compat for callers without a codepoint).
 * Webfonts (font_face.cc) take priority over kernel preinstalled faces. */
int cs_face_id_for_cp(int cs, int cp) {
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
    if (fam_len > 0) {
        int wf = font_face_match_cp(fam, fam_len, weight, italic, cp);
        if (wf >= 0) return wf;
    }
    int picked = fontsys_match(fam_len > 0 ? fam : "", gen, weight, italic);
    /* Glyph-level fallback. The CSS family list is exhausted (or no
     * match); fontsys_match returned the generic family's preinstalled
     * face. If that face's cmap doesn't carry the requested codepoint,
     * walk every registered face for one that does — matches Blink's
     * last-resort fallback in
     * blink/Source/platform/fonts/FontFallbackList::fontDataForCharacter.
     * Skipped when cp == -1 (caller has no codepoint, e.g. line-height
     * lookup) since we have nothing to test against. */
    if (cp >= 0 && picked >= 0 && !fontsys_face_has_cp(picked, cp)) {
        int sub = fontsys_find_face_with_cp(cp);
        if (sub >= 0) return sub;
    }
    return picked;
}

/* Range-blind variant for callers that pick one face per atom (line
 * height, list markers, generic measurement). */
int cs_face_id_for(int cs) {
    return cs_face_id_for_cp(cs, -1);
}

int text_slice_w(int off, int len, int tier) {
    return gfx2d_text_width_n(attr_pool + off, len, tier_to_font(tier));
}

/* fontsys-aware width with unicode-range awareness: walk the UTF-8 byte
 * run codepoint by codepoint, resolve the face per cp (so Google Fonts
 * subsets route correctly), and sum hmtx advances. Falls back to the
 * tier path when size_px is unavailable (e.g. bootstrap atoms with no
 * resolved cs). */
int text_slice_w_cs(int cs, int off, int len) {
    if (cs >= 0 && cs < cs_count && cs_font_size_px[cs] > 0) {
        int total = 0;
        int i = 0;
        int got_any = 0;
        while (i < len) {
            int cp;
            int adv = utf8_decode_one(attr_pool + off + i, len - i, &cp);
            if (adv <= 0) break;
            int face = cs_face_id_for_cp(cs, cp);
            if (face >= 0) {
                int aw = fontsys_advance(face, cp, cs_font_size_px[cs]);
                if (aw <= 0) {
                    /* Glyph missing in this face's cmap; fall through to
                     * the tier-bitmap advance so we never return 0 width
                     * (which would collapse adjacent words). */
                    aw = gfx2d_glyph_advance(' ', tier_to_font(cs_font_size_tier[cs]));
                    if (aw <= 0) aw = cs_font_size_px[cs] / 2 + 1;
                }
                total = total + aw;
                got_any = 1;
            } else {
                total = total + tier_char_w(cs_font_size_tier[cs]);
            }
            i = i + adv;
        }
        if (got_any) return total;
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
    /* Pass the full text-dec bitmask (TD_UNDERLINE | TD_LINE_THROUGH) so
     * paint_rt_line_box can stroke both. la_underline was historically a
     * bool but bit 0 = underline / bit 1 = line-through line up cleanly
     * with the cs_text_dec flag layout. */
    int underline = cs_text_dec[cs] & (TD_UNDERLINE | TD_LINE_THROUGH);
    int link_idx = -1;
    /* Walk up looking for a parent with rt_link_idx >= 0. While walking,
     * also pick up the closest INLINE ancestor's background-color so rules
     * like `a[href*="example"] { background: #eef }` paint a per-word fill
     * behind the inline link text (background-color is non-inherited, so
     * the text node's own cs_bg stays -1). Block ancestors paint their
     * own background through paint_rt_box_decoration; stop the walk there
     * to avoid double-painting a block fill behind every word. */
    int p = rt_text_n;
    while (p >= 0) {
        if (rt_link_idx[p] >= 0 && link_idx < 0) link_idx = rt_link_idx[p];
        if (bg < 0 && p != rt_text_n) {
            int kp = rt_kind[p];
            if (kp != RT_INLINE && kp != RT_TEXT) break;
            int cs_p = rt_style[p];
            if (cs_bg[cs_p] >= 0) bg = cs_bg[cs_p];
        }
        if (link_idx >= 0 && bg >= 0) break;
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
                la_cs[la_count] = cs;
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
                    la_cs[la_count] = cs;
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
            la_cs[la_count] = cs;
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
            la_cs[la_count] = cs;
            la_italic[la_count] = italic;
            la_fg[la_count] = fg; la_bg[la_count] = bg;
            la_bold[la_count] = bold; la_underline[la_count] = underline;
            la_link_idx[la_count] = link_idx;
            la_x[la_count] = -1;
            la_count++;
        }
    }
}


/* Shrink-to-fit width for `display: inline-block` with `width: auto`.
 * CSS 2.1 §10.3.9.  Single-direct-text-child case (covers the chip
 * pattern `<span class=pill>pill</span>`); for nested/mixed inline
 * content we leave cs_width = -1 and let layout_block stretch to
 * avail.  Estimate width from text length × half font size. */
void shrink_to_fit_inline_block(int n) {
    int sty = rt_style[n];
    if (cs_width[sty] >= 0) return;
    int fc = rt_first_child[n];
    if (fc < 0) return;
    if (rt_kind[fc] != RT_TEXT) return;
    if (rt_next[fc] >= 0) return;
    int csz = cs_font_size_px[sty];
    int chw = 8;
    if (csz > 0) chw = csz / 2 + 1;
    cs_width[sty] = rt_text_len[fc] * chw;
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
        if (kind == RT_INLINE_BLOCK) {
            shrink_to_fit_inline_block(n);
            layout_block(n, avail);
        }
        int w = (rt_intrinsic_w[n] > 0) ? rt_intrinsic_w[n] : rt_w[n];
        int h = (rt_intrinsic_h[n] > 0) ? rt_intrinsic_h[n] : rt_h[n];
        /* Stamp the resolved size on the node now so hit_test (which
         * runs outside the paint pipeline) can match a click against
         * the checkbox/text input. paint_rt_line_box also sets these,
         * but only at paint time, and clicks happen between paints. */
        if (rt_intrinsic_w[n] > 0) rt_w[n] = w;
        if (rt_intrinsic_h[n] > 0) rt_h[n] = h;
        (void)h;
        if (la_count < MAX_LINE_ATOMS) {
            la_text_off[la_count] = -n - 1;     /* negative encodes "RT node ref"; -n-1 reverses to n */
            la_text_len[la_count] = 0;
            la_w[la_count] = w;
            la_font_tier[la_count] = 0;
            la_size_px[la_count] = 0;
            la_face_id[la_count] = -1;
            la_cs[la_count] = -1;
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
            /* Hard break (PRE). la_x stores the atom's x relative to the
             * line box's left edge (the line box itself sits at cx in
             * the parent block), so the running cursor `x` -- which is
             * absolute-within-parent and starts at cx -- is normalised
             * by subtracting cx before stashing. paint_rt_line_box then
             * does sx_lb + la_x[k] without double-counting padding. */
            la_x[k] = x - cx;
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
        la_x[k] = x - cx;
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
    /* Resolve width. Default `box-sizing: content-box` per CSS 2.1:
     * `width` describes the CONTENT box, so the painted (border)
     * box is `width + padding-l/r + border-l/r`. Without this, a
     * `.card { width:240px; padding:16px; border:1px }` painted at
     * 240 px outer (content squeezed to 206) instead of Chrome's
     * 274 px outer. We don't yet honour `box-sizing: border-box`;
     * authors who set it will have to drop padding from their width
     * until cs_box_sizing exists. */
    int sty = rt_style[n];
    int style_w = cs_width[sty];
    int extra_w = rt_padding_l(n) + rt_padding_r(n)
                + rt_border_l(n)  + rt_border_r(n);
    int content_w;
    if (style_w >= 0) {
        content_w = style_w;
    } else {
        content_w = avail_w - rt_margin_l(n) - rt_margin_r(n) - extra_w;
    }
    /* min/max-width clamps apply to the content box per CSS 2.1 §10.4
     * (content-box sizing). min-width wins on conflict per spec. */
    int max_w_clamp = cs_max_width[sty];
    int min_w_clamp = cs_min_width[sty];
    if (max_w_clamp >= 0 && content_w > max_w_clamp) content_w = max_w_clamp;
    if (min_w_clamp >= 0 && content_w < min_w_clamp) content_w = min_w_clamp;
    if (content_w < 0) content_w = 0;
    int w = content_w + extra_w;
    rt_w[n] = w;

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
     * Floats and clearance break collapse: deferred to B2. */
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
        /* Out-of-flow children are skipped by the in-flow pass;
         * layout_oof handles them later against their containing block.
         * Without this skip, position:fixed and position:absolute push
         * subsequent siblings down because their box still consumes
         * vertical space. Reference: blink/Source/core/rendering/
         * RenderBlockFlow::layoutBlockChildren skips children where
         * isOutOfFlowPositioned() returns true. */
        {
            int c_sty = rt_style[c];
            int c_pos = cs_position[c_sty];
            if (c_pos == POS_ABSOLUTE || c_pos == POS_FIXED) {
                c = rt_next[c]; continue;
            }
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

    /* Resolve own height. Same content-box convention as width: the
     * `height` property describes the content box, and min/max-height
     * also apply to the content box per CSS 2.1 §10.7. */
    int style_h = cs_height[sty];
    int extra_h = rt_padding_t(n) + rt_padding_b(n)
                + rt_border_t(n)  + rt_border_b(n);
    int content_h;
    if (style_h >= 0) {
        content_h = style_h;
    } else {
        content_h = cy - rt_padding_t(n) - rt_border_t(n);
    }
    int max_h_clamp = cs_max_height[sty];
    int min_h_clamp = cs_min_height[sty];
    if (max_h_clamp >= 0 && content_h > max_h_clamp) content_h = max_h_clamp;
    if (min_h_clamp >= 0 && content_h < min_h_clamp) content_h = min_h_clamp;
    if (content_h < 0) content_h = 0;
    rt_h[n] = content_h + extra_h;
}

/* Find the document-space x of node n by summing rt_x along its parent
 * chain. Used by layout_oof to anchor an absolute box against a
 * positioned ancestor whose final rt_x is already filled in. Stops at
 * any oof ancestor (its rt_x is already absolute). */
int rt_doc_x_of(int n) {
    int x = 0;
    int cur = n;
    while (cur >= 0) {
        x = x + rt_x[cur];
        if (rt_is_oof[cur]) break;
        cur = rt_parent[cur];
    }
    return x;
}
int rt_doc_y_of(int n) {
    int y = 0;
    int cur = n;
    while (cur >= 0) {
        y = y + rt_y[cur];
        if (rt_is_oof[cur]) break;
        cur = rt_parent[cur];
    }
    return y;
}

/* Walk parent chain looking for nearest positioned ancestor. If none,
 * the containing block is the initial CB (the document root, RT 0).
 * Returns the RT index. */
int rt_containing_block(int n) {
    int p = rt_parent[n];
    while (p >= 0) {
        int sty = rt_style[p];
        if (cs_position[sty] != POS_STATIC) return p;
        p = rt_parent[p];
    }
    return 0;
}

/* Resolve an `position: absolute | fixed` node against its containing
 * block. Containing block for absolute is the nearest positioned
 * ancestor (or document root); for fixed it is the viewport, anchored
 * at (0,0) with width = viewport content width. Sets rt_x/rt_y to
 * absolute document-space coords and marks rt_is_oof so paint walks
 * the value directly instead of summing the ancestor chain. */
/* Recursive intrinsic-text-width estimate for shrink-to-fit. Walks the
 * subtree, summing text-slice widths within an RT_TEXT, and taking
 * the max across siblings (since horizontal stacking inside an inline
 * run sums; nested block siblings each get their own line). For label-
 * sized abspos boxes (the common case) this is close to the spec
 * shrink-to-fit which is max(min-content, min(preferred, available)).
 * Reference: blink/Source/core/rendering/RenderBox.cpp
 * shrinkToFitWidth + computePreferredLogicalWidths. */
int oof_intrinsic_text_w(int n) {
    int kind = rt_kind[n];
    if (kind == RT_TEXT) {
        int sty = rt_style[n];
        return text_slice_w_cs(sty, rt_text_off[n], rt_text_len[n]);
    }
    int sum = 0;
    int max_block = 0;
    int c = rt_first_child[n];
    while (c >= 0) {
        int ck = rt_kind[c];
        int w = oof_intrinsic_text_w(c);
        if (ck == RT_INLINE || ck == RT_TEXT || ck == RT_INLINE_BLOCK ||
            ck == RT_REPLACED) {
            sum = sum + w;
        } else {
            if (w > max_block) max_block = w;
        }
        c = rt_next[c];
    }
    if (sum > max_block) return sum;
    return max_block;
}

void layout_oof_one(int oof) {
    int sty = rt_style[oof];
    int pos = cs_position[sty];
    if (pos != POS_ABSOLUTE && pos != POS_FIXED) return;

    int cb_x = 0;
    int cb_y = 0;
    int cb_w = 0;
    int cb_h = 0;
    if (pos == POS_FIXED) {
        cb_x = 0; cb_y = 0;
        cb_w = viewport_content_w();
        cb_h = (cur_ch > (ADDR_H + 1) + (STATUS_H + 1))
               ? cur_ch - (ADDR_H + 1) - (STATUS_H + 1) : 0;
    } else {
        int cb = rt_containing_block(oof);
        cb_x = rt_doc_x_of(cb) + rt_padding_l(cb) + rt_border_l(cb);
        cb_y = rt_doc_y_of(cb) + rt_padding_t(cb) + rt_border_t(cb);
        cb_w = rt_w[cb] - rt_padding_l(cb) - rt_padding_r(cb)
                       - rt_border_l(cb)  - rt_border_r(cb);
        cb_h = rt_h[cb] - rt_padding_t(cb) - rt_padding_b(cb)
                       - rt_border_t(cb)  - rt_border_b(cb);
    }
    if (cb_w < 0) cb_w = 0;
    if (cb_h < 0) cb_h = 0;

    /* CSS 2.1 §10.3.7 - width / left / right resolution for absolutely
     * positioned non-replaced elements. Reference:
     * blink/Source/core/rendering/RenderBox.cpp::computePositionedLogicalWidth.
     *
     *   left set, right set, width auto  -> width = cb_w - left - right
     *   left set, right auto, width auto -> shrink-to-fit, x = left
     *   left auto, right set, width auto -> shrink-to-fit, x = cb_w - right - w
     *   left auto, right auto, width auto -> shrink-to-fit at static position
     */
    int has_w = (cs_width[sty] >= 0);
    int has_h = (cs_height[sty] >= 0);
    int leftv = cs_left[sty];
    int rightv = cs_right[sty];
    int topv = cs_top[sty];
    int botv = cs_bottom[sty];
    int w_resolved = -1;
    if (has_w) {
        w_resolved = cs_width[sty];
    } else if (leftv >= 0 && rightv >= 0) {
        w_resolved = cb_w - leftv - rightv;
        if (w_resolved < 0) w_resolved = 0;
    }
    /* When width is auto AND we have at most one of left/right, do a
     * shrink-to-fit pre-measurement: walk the subtree's text content
     * to estimate intrinsic width, then lay out ONCE at that width.
     * (Doing it as a re-layout would orphan the first pass's
     * line-box children on rt_first_child[oof] alongside the new ones,
     * since layout_block is purely additive — paint would then walk
     * both and double-render the text.)
     * Reference: blink/Source/core/rendering/RenderBox.cpp
     * shrinkToFitWidth + computePreferredLogicalWidths. */
    int extra_w_self = rt_padding_l(oof) + rt_padding_r(oof)
                     + rt_border_l(oof)  + rt_border_r(oof);
    if (w_resolved < 0) {
        int intrinsic = oof_intrinsic_text_w(oof);
        if (intrinsic > 0) {
            int shrink_w = intrinsic + extra_w_self;
            if (shrink_w > cb_w) shrink_w = cb_w;
            w_resolved = shrink_w - extra_w_self;
            if (w_resolved < 0) w_resolved = 0;
        }
    }
    int avail = (w_resolved >= 0) ? w_resolved : cb_w;
    layout_block(oof, avail);
    if (w_resolved >= 0) rt_w[oof] = w_resolved + extra_w_self;

    int x = 0;
    int y = 0;
    if (leftv >= 0) {
        x = cb_x + leftv;
    } else if (rightv >= 0) {
        x = cb_x + cb_w - rightv - rt_w[oof];
    } else {
        /* Static-position fallback: use the in-flow rt_x assigned during
         * the first pass. */
        x = rt_doc_x_of(oof);
    }
    if (topv >= 0) {
        y = cb_y + topv;
    } else if (botv >= 0) {
        y = cb_y + cb_h - botv - rt_h[oof];
    } else {
        y = rt_doc_y_of(oof);
    }
    if (has_h && cs_height[sty] >= 0) {
        rt_h[oof] = cs_height[sty]
                  + rt_padding_t(oof) + rt_padding_b(oof)
                  + rt_border_t(oof)  + rt_border_b(oof);
    }
    rt_x[oof] = x;
    rt_y[oof] = y;
    rt_is_oof[oof] = 1;
    rt_is_fixed[oof] = (pos == POS_FIXED) ? 1 : 0;
}

void layout_oof(void) {
    for (int i = 0; i < rt_oof_count; i = i + 1) {
        layout_oof_one(rt_oof_list[i]);
    }
}

void run_layout() {
    if (rt_count == 0) return;
    int root = 0;
    /* Root sits at document origin (0,0). rt_screen_x/y in paint.cc add
     * viewport_x()/viewport_y() at paint time, so seeding root with the
     * viewport origin double-counts the chrome and shoves the first h2
     * ~viewport_y px below where Chrome would draw it. */
    rt_x[root] = 0;
    rt_y[root] = 0;
    int avail = viewport_content_w();
    layout_block(root, avail);
    layout_oof();
    doc_h = rt_y[root] + rt_h[root];
}
