/**
 * @file style_properties.cc
 * @brief Apply CSS declarations to a computed style.
 */

void cs_apply_property(int cs, int prop, int val_off, int val_len) {
    /* Flat if/return chain: CupidC parser overflows on long else-if chains.
     * Hoist `c` to function scope: CupidC keeps locals in a flat table, so
     * re-declaring `int c;` in multiple sibling branches conflicts.*/
    int c;
    /* Pre-resolve var() so css_value_color / css_value_keyword don't
     * need to know about custom properties. css_value_len handles var()
     * + calc() itself (it needs `cs` for ancestor lookup). When the raw
     * value contains `var(`, substitute into a scratch buffer, intern
     * the result back into css_value_pool, and dispatch with the new
     * offsets. Skipped when no var() so the common case stays free.*/
    int has_var_call = 0;
    for (int sc = 0; sc + 4 <= val_len; sc = sc + 1) {
        if (css_value_pool[val_off + sc] == 'v' && css_value_pool[val_off + sc + 1] == 'a' &&
            css_value_pool[val_off + sc + 2] == 'r' && css_value_pool[val_off + sc + 3] == '(') {
            has_var_call = 1;
            break;
        }
    }
    if (has_var_call && prop != CP_CUSTOM_VAR) {
        char rbuf[1024];
        int rlen = cs_var_substitute(cs, val_off, val_len, rbuf, 1024);
        int new_off = css_intern_value(rbuf, rlen);
        if (new_off >= 0) {
            val_off = new_off;
            val_len = rlen;
        }
    }
    if (prop == CP_COLOR) {
        if (css_value_color(val_off, val_len, &c))
            cs_color[cs] = c;
        return;
    }
    if (prop == CP_BG_COLOR || prop == CP_BG) {
        int gtype = 0;
        int gc1 = 0;
        int gc2 = 0;
        if (css_value_linear_gradient(val_off, val_len, &gtype, &gc1, &gc2)) {
            cs_bg_grad[cs] = gtype;
            cs_bg_grad_c1[cs] = gc1;
            cs_bg_grad_c2[cs] = gc2;
            cs_bg[cs] = gc1;
            return;
        }
        if (css_value_color(val_off, val_len, &c)) {
            cs_bg[cs] = c;
            cs_bg_grad[cs] = BG_GRAD_NONE;
        }
        return;
    }
    if (prop == CP_BOX_SIZING) {
        if (css_value_keyword(val_off, val_len, "border-box"))
            cs_box_sizing[cs] = BOX_SIZING_BORDER;
        else
            cs_box_sizing[cs] = BOX_SIZING_CONTENT;
        return;
    }
    if (prop == CP_FONT_WEIGHT) {
        if (css_value_keyword(val_off, val_len, "bold")) {
            cs_font_w[cs] = 700;
            return;
        }
        if (css_value_keyword(val_off, val_len, "normal")) {
            cs_font_w[cs] = 400;
            return;
        }
        cs_font_w[cs] = css_value_int(val_off, val_len);
        return;
    }
    if (prop == CP_FONT_STYLE) {
        cs_font_i[cs] = css_value_keyword(val_off, val_len, "italic") ? 1 : 0;
        return;
    }
    if (prop == CP_FONT_FAMILY) {
        /* Stash the verbatim CSS value so the comma list / quotes are
         * passed to fontsys_match unchanged at paint time. Also walk
         * the comma list looking for any generic keyword token so the
         * cascade has a fallback even when generic appears LAST (e.g.
         * `font-family: 'TestRanges', sans-serif`). The previous code
         * used css_value_keyword which only matches the whole value.*/
        cs_font_family_off[cs] = val_off;
        cs_font_family_len[cs] = val_len;
        int gi = val_off;
        int gend = val_off + val_len;
        cs_font_generic[cs] = FONTSYS_FAMILY_DEFAULT;
        while (gi < gend) {
            while (gi < gend && (css_value_pool[gi] == ' ' || css_value_pool[gi] == '\t' ||
                                 css_value_pool[gi] == ','))
                gi = gi + 1;
            if (gi >= gend)
                break;
            int t_start = gi;
            if (css_value_pool[gi] == '\'' || css_value_pool[gi] == '"') {
                char q = css_value_pool[gi];
                gi = gi + 1;
                while (gi < gend && css_value_pool[gi] != q)
                    gi = gi + 1;
                if (gi < gend)
                    gi = gi + 1;
            } else {
                while (gi < gend && css_value_pool[gi] != ',')
                    gi = gi + 1;
            }
            int t_end = gi;
            while (t_end > t_start &&
                   (css_value_pool[t_end - 1] == ' ' || css_value_pool[t_end - 1] == '\t'))
                t_end = t_end - 1;
            int t_len = t_end - t_start;
            if (t_len == 5 && b_strieq_n(css_value_pool + t_start, "serif", 5)) {
                cs_font_generic[cs] = FONTSYS_FAMILY_SERIF;
                break;
            }
            if (t_len == 10 && b_strieq_n(css_value_pool + t_start, "sans-serif", 10)) {
                cs_font_generic[cs] = FONTSYS_FAMILY_SANS_SERIF;
                break;
            }
            if (t_len == 9 && b_strieq_n(css_value_pool + t_start, "monospace", 9)) {
                cs_font_generic[cs] = FONTSYS_FAMILY_MONOSPACE;
                break;
            }
            if (t_len == 7 && b_strieq_n(css_value_pool + t_start, "cursive", 7)) {
                cs_font_generic[cs] = FONTSYS_FAMILY_CURSIVE;
                break;
            }
            if (t_len == 7 && b_strieq_n(css_value_pool + t_start, "fantasy", 7)) {
                cs_font_generic[cs] = FONTSYS_FAMILY_FANTASY;
                break;
            }
            if (t_len == 9 && b_strieq_n(css_value_pool + t_start, "system-ui", 9)) {
                cs_font_generic[cs] = FONTSYS_FAMILY_SYSTEM_UI;
                break;
            }
        }
        return;
    }
    if (prop == CP_FONT_SIZE) {
        /* Resolve to px against parent (em/%) and root (rem). cs index ==
         * DOM node index, so parent_cs == n_parent[cs] and the parent has
         * already been cascaded earlier in DOM order.*/
        int parent_px = 16;
        int root_px = 16;
        int parent = n_parent[cs];
        if (parent >= 0 && parent < cs && cs_font_size_px[parent] > 0) {
            parent_px = cs_font_size_px[parent];
        }
        if (cs >= 1 && cs_font_size_px[1] > 0)
            root_px = cs_font_size_px[1];
        int px = parse_length_px(val_off, val_len, parent_px, root_px);
        if (px > 0)
            cs_font_size_px[cs] = px;
        return;
    }
    if (prop == CP_TEXT_ALIGN) {
        if (css_value_keyword(val_off, val_len, "center")) {
            cs_text_align[cs] = TA_CENTER;
            return;
        }
        if (css_value_keyword(val_off, val_len, "right")) {
            cs_text_align[cs] = TA_RIGHT;
            return;
        }
        cs_text_align[cs] = TA_LEFT;
        return;
    }
    if (prop == CP_TEXT_DEC) {
        cs_text_dec[cs] = 0;
        if (css_value_keyword(val_off, val_len, "underline"))
            cs_text_dec[cs] = cs_text_dec[cs] | TD_UNDERLINE;
        if (css_value_keyword(val_off, val_len, "line-through"))
            cs_text_dec[cs] = cs_text_dec[cs] | TD_LINE_THROUGH;
        return;
    }
    if (prop == CP_DISPLAY) {
        if (css_value_keyword(val_off, val_len, "none")) {
            cs_display[cs] = DISP_NONE;
            return;
        }
        if (css_value_keyword(val_off, val_len, "block")) {
            cs_display[cs] = DISP_BLOCK;
            return;
        }
        if (css_value_keyword(val_off, val_len, "inline-block")) {
            cs_display[cs] = DISP_INLINE_BLOCK;
            return;
        }
        if (css_value_keyword(val_off, val_len, "inline-flex")) {
            cs_display[cs] = DISP_INLINE_FLEX;
            return;
        }
        if (css_value_keyword(val_off, val_len, "inline")) {
            cs_display[cs] = DISP_INLINE;
            return;
        }
        if (css_value_keyword(val_off, val_len, "list-item")) {
            cs_display[cs] = DISP_LIST_ITEM;
            return;
        }
        if (css_value_keyword(val_off, val_len, "flex")) {
            cs_display[cs] = DISP_FLEX;
            return;
        }
        if (css_value_keyword(val_off, val_len, "table")) {
            cs_display[cs] = DISP_TABLE;
            return;
        }
        if (css_value_keyword(val_off, val_len, "table-row")) {
            cs_display[cs] = DISP_TABLE_ROW;
            return;
        }
        if (css_value_keyword(val_off, val_len, "table-cell")) {
            cs_display[cs] = DISP_TABLE_CELL;
            return;
        }
        return;
    }
    if (prop == CP_FLEX_DIR) {
        if (css_value_keyword(val_off, val_len, "column"))
            cs_flex_dir[cs] = FLEX_DIR_COLUMN;
        else
            cs_flex_dir[cs] = FLEX_DIR_ROW;
        return;
    }
    if (prop == CP_JUSTIFY) {
        if (css_value_keyword(val_off, val_len, "flex-end"))
            cs_justify[cs] = JUSTIFY_FLEX_END;
        else if (css_value_keyword(val_off, val_len, "center"))
            cs_justify[cs] = JUSTIFY_CENTER;
        else if (css_value_keyword(val_off, val_len, "space-between"))
            cs_justify[cs] = JUSTIFY_SPACE_BETWEEN;
        else if (css_value_keyword(val_off, val_len, "space-around"))
            cs_justify[cs] = JUSTIFY_SPACE_AROUND;
        else if (css_value_keyword(val_off, val_len, "space-evenly"))
            cs_justify[cs] = JUSTIFY_SPACE_EVENLY;
        else
            cs_justify[cs] = JUSTIFY_FLEX_START;
        return;
    }
    if (prop == CP_ALIGN_ITEMS) {
        if (css_value_keyword(val_off, val_len, "flex-start"))
            cs_align_items[cs] = ALIGN_FLEX_START;
        else if (css_value_keyword(val_off, val_len, "flex-end"))
            cs_align_items[cs] = ALIGN_FLEX_END;
        else if (css_value_keyword(val_off, val_len, "center"))
            cs_align_items[cs] = ALIGN_CENTER;
        else if (css_value_keyword(val_off, val_len, "baseline"))
            cs_align_items[cs] = ALIGN_BASELINE;
        else
            cs_align_items[cs] = ALIGN_STRETCH;
        return;
    }
    if (prop == CP_FLEX_GROW) {
        /* Parse number with up to 2 decimal places into hundredths so
         * `flex-grow: 1.5` stores 150. Plain integers come through as
         * 100 * value.*/
        int v = 0;
        int frac = 0;
        int frac_digits = 0;
        int seen_dot = 0;
        int i = val_off;
        int end = val_off + val_len;
        while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
            i = i + 1;
        while (i < end) {
            char c = css_value_pool[i];
            if (c >= '0' && c <= '9') {
                if (seen_dot) {
                    if (frac_digits < 2) {
                        frac = frac * 10 + (c - '0');
                        frac_digits = frac_digits + 1;
                    }
                } else {
                    v = v * 10 + (c - '0');
                }
            } else if (c == '.') {
                seen_dot = 1;
            } else {
                break;
            }
            i = i + 1;
        }
        while (frac_digits < 2) {
            frac = frac * 10;
            frac_digits = frac_digits + 1;
        }
        cs_flex_grow[cs] = v * 100 + frac;
        return;
    }
    if (prop == CP_FLEX_SHRINK) {
        int v = css_value_int(val_off, val_len);
        cs_flex_shrink[cs] = v * 100;
        return;
    }
    if (prop == CP_FLEX_BASIS) {
        if (css_value_keyword(val_off, val_len, "auto")) {
            cs_flex_basis[cs] = -1;
            return;
        }
        cs_flex_basis[cs] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_FLEX) {
        /* Shorthand. Common forms:
         *   flex: <number>            -> grow=N shrink=1 basis=0
         *   flex: <number> <number>   -> grow=A shrink=B basis=0
         *   flex: <number> <length>   -> grow=A shrink=1 basis=L
         *   flex: <a> <b> <length>    -> grow=A shrink=B basis=L
         *   flex: auto                -> grow=1 shrink=1 basis=auto
         *   flex: none                -> grow=0 shrink=0 basis=auto
         * Reference: blink/Source/core/css/parser/CSSPropertyParser.cpp
         * (consumeFlex).*/
        if (css_value_keyword(val_off, val_len, "none")) {
            cs_flex_grow[cs] = 0;
            cs_flex_shrink[cs] = 0;
            cs_flex_basis[cs] = -1;
            return;
        }
        if (css_value_keyword(val_off, val_len, "auto")) {
            cs_flex_grow[cs] = 100;
            cs_flex_shrink[cs] = 100;
            cs_flex_basis[cs] = -1;
            return;
        }
        /* Token walk. Numbers become grow then shrink; a length or `0`
         * with a px-style suffix becomes basis.*/
        int tok_off[3];
        int tok_len[3];
        int tn = 0;
        int i = val_off;
        int end = val_off + val_len;
        while (i < end && tn < 3) {
            while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
                i = i + 1;
            if (i >= end)
                break;
            int s = i;
            while (i < end && css_value_pool[i] != ' ' && css_value_pool[i] != '\t')
                i = i + 1;
            tok_off[tn] = s;
            tok_len[tn] = i - s;
            tn = tn + 1;
        }
        int g = 100; /* default grow if first number missing */
        int sh = 100;
        int basis = 0; /* `flex: 1` => basis: 0 per spec */
        int got_g = 0;
        int got_sh = 0;
        int got_basis = 0;
        int t;
        for (t = 0; t < tn; t = t + 1) {
            char *s = css_value_pool + tok_off[t];
            int sl = tok_len[t];
            int has_unit = 0;
            int k;
            for (k = 0; k < sl; k = k + 1) {
                char c = s[k];
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '%') {
                    has_unit = 1;
                    break;
                }
            }
            if (has_unit && !got_basis) {
                if (b_strieq_n(s, "auto", 4))
                    basis = -1;
                else
                    basis = css_value_len(cs, tok_off[t], sl);
                got_basis = 1;
            } else if (!got_g) {
                int v = 0;
                int frac = 0;
                int seen_dot = 0;
                int frac_d = 0;
                int kk;
                for (kk = 0; kk < sl; kk = kk + 1) {
                    char c = s[kk];
                    if (c >= '0' && c <= '9') {
                        if (seen_dot) {
                            if (frac_d < 2) {
                                frac = frac * 10 + (c - '0');
                                frac_d = frac_d + 1;
                            }
                        } else
                            v = v * 10 + (c - '0');
                    } else if (c == '.')
                        seen_dot = 1;
                    else
                        break;
                }
                while (frac_d < 2) {
                    frac = frac * 10;
                    frac_d = frac_d + 1;
                }
                g = v * 100 + frac;
                got_g = 1;
            } else if (!got_sh) {
                int v = 0;
                int frac = 0;
                int seen_dot = 0;
                int frac_d = 0;
                int kk;
                for (kk = 0; kk < sl; kk = kk + 1) {
                    char c = s[kk];
                    if (c >= '0' && c <= '9') {
                        if (seen_dot) {
                            if (frac_d < 2) {
                                frac = frac * 10 + (c - '0');
                                frac_d = frac_d + 1;
                            }
                        } else
                            v = v * 10 + (c - '0');
                    } else if (c == '.')
                        seen_dot = 1;
                    else
                        break;
                }
                while (frac_d < 2) {
                    frac = frac * 10;
                    frac_d = frac_d + 1;
                }
                sh = v * 100 + frac;
                got_sh = 1;
            }
        }
        cs_flex_grow[cs] = g;
        cs_flex_shrink[cs] = sh;
        cs_flex_basis[cs] = basis;
        return;
    }
    if (prop == CP_GAP) {
        cs_gap[cs] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_BG_IMAGE) {
        /* `background-image: url(href)` or `none`. The href bytes live in
         * css_value_pool already (the parser stashes the value before
         * apply); we record an off/len that points at the inner string,
         * stripping `url(` ... `)` and any single/double quotes.*/
        if (css_value_keyword(val_off, val_len, "none")) {
            cs_bg_img_off[cs] = -1;
            cs_bg_img_len[cs] = 0;
            return;
        }
        int p = val_off;
        int e = val_off + val_len;
        while (p < e && (css_value_pool[p] == ' ' || css_value_pool[p] == '\t'))
            p = p + 1;
        if (p + 4 < e && b_strieq_n(css_value_pool + p, "url(", 4)) {
            p = p + 4;
            int rp = e - 1;
            while (rp > p && (css_value_pool[rp] == ' ' || css_value_pool[rp] == '\t'))
                rp = rp - 1;
            if (rp > p && css_value_pool[rp] == ')')
                rp = rp - 1;
            while (p <= rp && (css_value_pool[p] == ' ' || css_value_pool[p] == '\t'))
                p = p + 1;
            while (rp >= p && (css_value_pool[rp] == ' ' || css_value_pool[rp] == '\t'))
                rp = rp - 1;
            if (p <= rp && (css_value_pool[p] == '"' || css_value_pool[p] == '\''))
                p = p + 1;
            if (rp >= p && (css_value_pool[rp] == '"' || css_value_pool[rp] == '\''))
                rp = rp - 1;
            if (rp >= p) {
                cs_bg_img_off[cs] = p;
                cs_bg_img_len[cs] = rp - p + 1;
                return;
            }
        }
        cs_bg_img_off[cs] = -1;
        cs_bg_img_len[cs] = 0;
        return;
    }
    if (prop == CP_BG_SIZE) {
        if (css_value_keyword(val_off, val_len, "cover")) {
            cs_bg_size_w[cs] = BG_SIZE_COVER;
            cs_bg_size_h[cs] = BG_SIZE_COVER;
            return;
        }
        if (css_value_keyword(val_off, val_len, "contain")) {
            cs_bg_size_w[cs] = BG_SIZE_CONTAIN;
            cs_bg_size_h[cs] = BG_SIZE_CONTAIN;
            return;
        }
        /* One or two length tokens: `Npx` or `Npx Mpx`. `auto` keeps the
         * intrinsic axis.*/
        int p = val_off;
        int e = val_off + val_len;
        int t1_off = -1;
        int t1_len = 0;
        int t2_off = -1;
        int t2_len = 0;
        while (p < e && (css_value_pool[p] == ' ' || css_value_pool[p] == '\t'))
            p = p + 1;
        if (p < e) {
            t1_off = p;
            while (p < e && css_value_pool[p] != ' ' && css_value_pool[p] != '\t')
                p = p + 1;
            t1_len = p - t1_off;
            while (p < e && (css_value_pool[p] == ' ' || css_value_pool[p] == '\t'))
                p = p + 1;
            if (p < e) {
                t2_off = p;
                while (p < e && css_value_pool[p] != ' ' && css_value_pool[p] != '\t')
                    p = p + 1;
                t2_len = p - t2_off;
            }
        }
        if (t1_off >= 0) {
            if (b_strieq_n(css_value_pool + t1_off, "auto", 4) && t1_len == 4)
                cs_bg_size_w[cs] = BG_SIZE_AUTO;
            else
                cs_bg_size_w[cs] = css_value_len(cs, t1_off, t1_len);
        }
        if (t2_off >= 0) {
            if (b_strieq_n(css_value_pool + t2_off, "auto", 4) && t2_len == 4)
                cs_bg_size_h[cs] = BG_SIZE_AUTO;
            else
                cs_bg_size_h[cs] = css_value_len(cs, t2_off, t2_len);
        } else if (t1_off >= 0) {
            /* Single value: per spec, the missing axis is `auto`. */
            cs_bg_size_h[cs] = BG_SIZE_AUTO;
        }
        return;
    }
    if (prop == CP_BG_POSITION) {
        /* Accept keyword pairs (top/bottom/left/right/center) or
         * length pairs. Single value applies to x; y defaults to center.
         * Box dimensions aren't known at apply time; encode keywords as
         * sentinel pixel positions resolved at paint time:
         *   0       -> left / top
         *   -10000  -> center
         *   -20000  -> right / bottom
         * Numeric values pass through unchanged.*/
        int p = val_off;
        int e = val_off + val_len;
        int tok_off[2];
        int tok_len[2];
        int tn = 0;
        while (p < e && tn < 2) {
            while (p < e && (css_value_pool[p] == ' ' || css_value_pool[p] == '\t'))
                p = p + 1;
            if (p >= e)
                break;
            int s = p;
            while (p < e && css_value_pool[p] != ' ' && css_value_pool[p] != '\t')
                p = p + 1;
            tok_off[tn] = s;
            tok_len[tn] = p - s;
            tn = tn + 1;
        }
        int xv = 0;
        int yv = -10000; /* center default for missing y */
        int t;
        for (t = 0; t < tn; t = t + 1) {
            int o = tok_off[t];
            int l = tok_len[t];
            int is_x_kw = -1;
            int is_y_kw = -1;
            if (b_strieq_n(css_value_pool + o, "left", 4) && l == 4)
                is_x_kw = 0;
            else if (b_strieq_n(css_value_pool + o, "right", 5) && l == 5)
                is_x_kw = -20000;
            else if (b_strieq_n(css_value_pool + o, "top", 3) && l == 3)
                is_y_kw = 0;
            else if (b_strieq_n(css_value_pool + o, "bottom", 6) && l == 6)
                is_y_kw = -20000;
            else if (b_strieq_n(css_value_pool + o, "center", 6) && l == 6) {
                /* Apply to whichever axis hasn't been claimed yet. */
                if (t == 0)
                    xv = -10000;
                else
                    yv = -10000;
                continue;
            }
            if (is_x_kw != -1) {
                xv = is_x_kw;
                continue;
            }
            if (is_y_kw != -1) {
                yv = is_y_kw;
                continue;
            }
            /* numeric */
            if (t == 0)
                xv = css_value_len(cs, o, l);
            else
                yv = css_value_len(cs, o, l);
        }
        cs_bg_pos_x[cs] = xv;
        cs_bg_pos_y[cs] = yv;
        return;
    }
    if (prop == CP_BG_REPEAT) {
        if (css_value_keyword(val_off, val_len, "no-repeat"))
            cs_bg_repeat[cs] = BG_REPEAT_NONE;
        else if (css_value_keyword(val_off, val_len, "repeat-x"))
            cs_bg_repeat[cs] = BG_REPEAT_X;
        else if (css_value_keyword(val_off, val_len, "repeat-y"))
            cs_bg_repeat[cs] = BG_REPEAT_Y;
        else
            cs_bg_repeat[cs] = BG_REPEAT_BOTH;
        return;
    }
    if (prop == CP_MARGIN || prop == CP_PADDING) {
        int vals[4];
        int autos[4];
        vals[0] = 0;
        vals[1] = 0;
        vals[2] = 0;
        vals[3] = 0;
        autos[0] = 0;
        autos[1] = 0;
        autos[2] = 0;
        autos[3] = 0;
        int nvals = 0;
        int i = val_off;
        int end = val_off + val_len;
        while (i < end && nvals < 4) {
            while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
                i = i + 1;
            if (i >= end)
                break;
            int v_start = i;
            while (i < end && css_value_pool[i] != ' ' && css_value_pool[i] != '\t')
                i = i + 1;
            int sub_len = i - v_start;
            if (css_value_is_auto(v_start, sub_len)) {
                vals[nvals] = 0;
                autos[nvals] = 1;
            } else {
                vals[nvals] = css_value_len(cs, v_start, sub_len);
                autos[nvals] = 0;
            }
            nvals = nvals + 1;
        }
        /* Expand 1/2/3/4-value shorthand into a 4-element side array
         * (top, right, bottom, left).*/
        int side_v[4];
        int side_a[4];
        if (nvals == 1) {
            side_v[0] = vals[0];
            side_v[1] = vals[0];
            side_v[2] = vals[0];
            side_v[3] = vals[0];
            side_a[0] = autos[0];
            side_a[1] = autos[0];
            side_a[2] = autos[0];
            side_a[3] = autos[0];
        } else if (nvals == 2) {
            side_v[0] = vals[0];
            side_v[1] = vals[1];
            side_v[2] = vals[0];
            side_v[3] = vals[1];
            side_a[0] = autos[0];
            side_a[1] = autos[1];
            side_a[2] = autos[0];
            side_a[3] = autos[1];
        } else if (nvals == 3) {
            side_v[0] = vals[0];
            side_v[1] = vals[1];
            side_v[2] = vals[2];
            side_v[3] = vals[1];
            side_a[0] = autos[0];
            side_a[1] = autos[1];
            side_a[2] = autos[2];
            side_a[3] = autos[1];
        } else {
            side_v[0] = vals[0];
            side_v[1] = vals[1];
            side_v[2] = vals[2];
            side_v[3] = vals[3];
            side_a[0] = autos[0];
            side_a[1] = autos[1];
            side_a[2] = autos[2];
            side_a[3] = autos[3];
        }
        if (prop == CP_MARGIN) {
            cs_margin[cs][0] = side_v[0];
            cs_margin[cs][1] = side_v[1];
            cs_margin[cs][2] = side_v[2];
            cs_margin[cs][3] = side_v[3];
            cs_margin_auto[cs][0] = side_a[0];
            cs_margin_auto[cs][1] = side_a[1];
            cs_margin_auto[cs][2] = side_a[2];
            cs_margin_auto[cs][3] = side_a[3];
        } else {
            cs_padding[cs][0] = side_v[0];
            cs_padding[cs][1] = side_v[1];
            cs_padding[cs][2] = side_v[2];
            cs_padding[cs][3] = side_v[3];
        }
        return;
    }
    if (prop == CP_MARGIN_T) {
        if (css_value_is_auto(val_off, val_len)) {
            cs_margin[cs][0] = 0;
            cs_margin_auto[cs][0] = 1;
        } else {
            cs_margin[cs][0] = css_value_len(cs, val_off, val_len);
            cs_margin_auto[cs][0] = 0;
        }
        return;
    }
    if (prop == CP_MARGIN_R) {
        if (css_value_is_auto(val_off, val_len)) {
            cs_margin[cs][1] = 0;
            cs_margin_auto[cs][1] = 1;
        } else {
            cs_margin[cs][1] = css_value_len(cs, val_off, val_len);
            cs_margin_auto[cs][1] = 0;
        }
        return;
    }
    if (prop == CP_MARGIN_B) {
        if (css_value_is_auto(val_off, val_len)) {
            cs_margin[cs][2] = 0;
            cs_margin_auto[cs][2] = 1;
        } else {
            cs_margin[cs][2] = css_value_len(cs, val_off, val_len);
            cs_margin_auto[cs][2] = 0;
        }
        return;
    }
    if (prop == CP_MARGIN_L) {
        if (css_value_is_auto(val_off, val_len)) {
            cs_margin[cs][3] = 0;
            cs_margin_auto[cs][3] = 1;
        } else {
            cs_margin[cs][3] = css_value_len(cs, val_off, val_len);
            cs_margin_auto[cs][3] = 0;
        }
        return;
    }
    if (prop == CP_PADDING_T) {
        cs_padding[cs][0] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_PADDING_R) {
        cs_padding[cs][1] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_PADDING_B) {
        cs_padding[cs][2] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_PADDING_L) {
        cs_padding[cs][3] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_BORDER || prop == CP_BORDER_T || prop == CP_BORDER_R || prop == CP_BORDER_B ||
        prop == CP_BORDER_L) {
        /* Parse `<width> <style> <color>` shorthand. Width default 1px,
         * style ignored (treat any style as solid), color default keeps
         * existing cs_border_color. Sides: BORDER = all four,
         * BORDER_T/R/B/L = single side.*/
        int width = 1;
        int sides[4];
        sides[0] = (prop == CP_BORDER || prop == CP_BORDER_T) ? 1 : 0;
        sides[1] = (prop == CP_BORDER || prop == CP_BORDER_R) ? 1 : 0;
        sides[2] = (prop == CP_BORDER || prop == CP_BORDER_B) ? 1 : 0;
        sides[3] = (prop == CP_BORDER || prop == CP_BORDER_L) ? 1 : 0;
        int i = val_off;
        int end = val_off + val_len;
        int got_width = 0;
        int got_color = 0;
        while (i < end) {
            while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
                i = i + 1;
            if (i >= end)
                break;
            int t_start = i;
            while (i < end && css_value_pool[i] != ' ' && css_value_pool[i] != '\t')
                i = i + 1;
            int t_len = i - t_start;
            /* numeric? */
            if (!got_width && css_value_pool[t_start] >= '0' && css_value_pool[t_start] <= '9') {
                width = css_value_len(cs, t_start, t_len);
                got_width = 1;
                continue;
            }
            /* keyword styles: recognise dashed/dotted/none; everything
             * else (groove/ridge/double/inset/outset/hidden) collapses
             * to solid which is what we paint by default.*/
            if (css_value_keyword(t_start, t_len, "dashed")) {
                cs_border_style[cs] = BS_DASHED;
                continue;
            }
            if (css_value_keyword(t_start, t_len, "dotted")) {
                cs_border_style[cs] = BS_DOTTED;
                continue;
            }
            if (css_value_keyword(t_start, t_len, "none") ||
                css_value_keyword(t_start, t_len, "hidden")) {
                cs_border_style[cs] = BS_NONE;
                continue;
            }
            if (css_value_keyword(t_start, t_len, "solid") ||
                css_value_keyword(t_start, t_len, "double") ||
                css_value_keyword(t_start, t_len, "groove") ||
                css_value_keyword(t_start, t_len, "ridge") ||
                css_value_keyword(t_start, t_len, "inset") ||
                css_value_keyword(t_start, t_len, "outset")) {
                cs_border_style[cs] = BS_SOLID;
                continue;
            }
            /* color */
            if (!got_color && css_value_color(t_start, t_len, &c)) {
                cs_border_color[cs] = c;
                got_color = 1;
                continue;
            }
        }
        if (sides[0])
            cs_border[cs][0] = width;
        if (sides[1])
            cs_border[cs][1] = width;
        if (sides[2])
            cs_border[cs][2] = width;
        if (sides[3])
            cs_border[cs][3] = width;
        return;
    }
    if (prop == CP_BORDER_COLOR) {
        if (css_value_color(val_off, val_len, &c))
            cs_border_color[cs] = c;
        return;
    }
    if (prop == CP_BORDER_WIDTH) {
        int w = css_value_len(cs, val_off, val_len);
        cs_border[cs][0] = w;
        cs_border[cs][1] = w;
        cs_border[cs][2] = w;
        cs_border[cs][3] = w;
        return;
    }
    if (prop == CP_BORDER_STYLE) {
        if (css_value_keyword(val_off, val_len, "dashed")) {
            cs_border_style[cs] = BS_DASHED;
        } else if (css_value_keyword(val_off, val_len, "dotted")) {
            cs_border_style[cs] = BS_DOTTED;
        } else if (css_value_keyword(val_off, val_len, "none") ||
                   css_value_keyword(val_off, val_len, "hidden")) {
            cs_border_style[cs] = BS_NONE;
        } else {
            cs_border_style[cs] = BS_SOLID;
        }
        /* Presence of any non-none style implies 1px border if no width set. */
        if (cs_border[cs][0] == 0 && cs_border[cs][1] == 0 && cs_border[cs][2] == 0 &&
            cs_border[cs][3] == 0 && cs_border_style[cs] != BS_NONE) {
            cs_border[cs][0] = 1;
            cs_border[cs][1] = 1;
            cs_border[cs][2] = 1;
            cs_border[cs][3] = 1;
        }
        return;
    }
    if (prop == CP_FONT) {
        /* `font` shorthand per CSS 2.1 §15.8:
         *   [<style>||<variant>||<weight>] <size>[/<line-height>] <family>+
         * Pick up italic/bold/normal, the numeric size, and treat
         * everything after the size as the font-family list (verbatim
         * (same convention as CP_FONT_FAMILY).  Within the family
         * tokens we additionally watch for the generic keywords
         * (serif/sans-serif/monospace/...) and stash that into
         * cs_font_generic so fontsys_match can fall back without
         * needing to re-parse the family list.*/
        int i = val_off;
        int end = val_off + val_len;
        int saw_size = 0;
        int family_off = -1;
        int family_len = 0;
        while (i < end) {
            while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
                i = i + 1;
            if (i >= end)
                break;
            int t_start = i;
            while (i < end && css_value_pool[i] != ' ' && css_value_pool[i] != '\t')
                i = i + 1;
            int t_len = i - t_start;
            if (!saw_size && css_value_keyword(t_start, t_len, "italic")) {
                cs_font_i[cs] = 1;
                continue;
            }
            if (!saw_size && css_value_keyword(t_start, t_len, "bold")) {
                cs_font_w[cs] = 700;
                continue;
            }
            if (!saw_size && css_value_keyword(t_start, t_len, "normal")) {
                continue;
            }
            if (!saw_size && css_value_pool[t_start] >= '0' && css_value_pool[t_start] <= '9') {
                int parent_px = 16;
                int root_px = 16;
                int parent = n_parent[cs];
                if (parent >= 0 && parent < cs && cs_font_size_px[parent] > 0) {
                    parent_px = cs_font_size_px[parent];
                }
                if (cs >= 1 && cs_font_size_px[1] > 0)
                    root_px = cs_font_size_px[1];
                int px = parse_length_px(t_start, t_len, parent_px, root_px);
                if (px > 0)
                    cs_font_size_px[cs] = px;
                saw_size = 1;
                continue;
            }
            /* After the size, every remaining token belongs to the
             * font-family list.  Capture the verbatim slice from the
             * first family token to the end of the value.*/
            if (saw_size) {
                if (family_off < 0)
                    family_off = t_start;
                family_len = (val_off + val_len) - family_off;
                /* Pick up the generic keyword if this token is one. */
                if (css_value_keyword(t_start, t_len, "serif"))
                    cs_font_generic[cs] = FONTSYS_FAMILY_SERIF;
                else if (css_value_keyword(t_start, t_len, "sans-serif"))
                    cs_font_generic[cs] = FONTSYS_FAMILY_SANS_SERIF;
                else if (css_value_keyword(t_start, t_len, "monospace"))
                    cs_font_generic[cs] = FONTSYS_FAMILY_MONOSPACE;
                else if (css_value_keyword(t_start, t_len, "cursive"))
                    cs_font_generic[cs] = FONTSYS_FAMILY_CURSIVE;
                else if (css_value_keyword(t_start, t_len, "fantasy"))
                    cs_font_generic[cs] = FONTSYS_FAMILY_FANTASY;
                else if (css_value_keyword(t_start, t_len, "system-ui"))
                    cs_font_generic[cs] = FONTSYS_FAMILY_SYSTEM_UI;
                /* Trim trailing trailing whitespace once at end of loop. */
                while (family_len > 0) {
                    char b = css_value_pool[family_off + family_len - 1];
                    if (b != ' ' && b != '\t')
                        break;
                    family_len = family_len - 1;
                }
                /* Don't `continue;` here. Let the outer loop end naturally
                 * since we've already consumed all remaining bytes via
                 * the family slice.*/
            }
        }
        if (family_off >= 0) {
            cs_font_family_off[cs] = family_off;
            cs_font_family_len[cs] = family_len;
        }
        return;
    }
    if (prop == CP_WIDTH) {
        cs_width[cs] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_HEIGHT) {
        cs_height[cs] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_MAX_WIDTH) {
        if (css_value_keyword(val_off, val_len, "none")) {
            cs_max_width[cs] = -1;
            return;
        }
        cs_max_width[cs] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_MIN_WIDTH) {
        cs_min_width[cs] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_MAX_HEIGHT) {
        if (css_value_keyword(val_off, val_len, "none")) {
            cs_max_height[cs] = -1;
            return;
        }
        cs_max_height[cs] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_MIN_HEIGHT) {
        cs_min_height[cs] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_POSITION) {
        if (css_value_keyword(val_off, val_len, "relative")) {
            cs_position[cs] = POS_RELATIVE;
            return;
        }
        if (css_value_keyword(val_off, val_len, "absolute")) {
            cs_position[cs] = POS_ABSOLUTE;
            return;
        }
        if (css_value_keyword(val_off, val_len, "fixed")) {
            cs_position[cs] = POS_FIXED;
            return;
        }
        cs_position[cs] = POS_STATIC;
        return;
    }
    if (prop == CP_TOP) {
        if (css_value_keyword(val_off, val_len, "auto")) {
            cs_top[cs] = 0;
            cs_pos_set[cs] = cs_pos_set[cs] & ~1;
            return;
        }
        cs_top[cs] = css_value_len(cs, val_off, val_len);
        cs_pos_set[cs] = cs_pos_set[cs] | 1;
        return;
    }
    if (prop == CP_RIGHT) {
        if (css_value_keyword(val_off, val_len, "auto")) {
            cs_right[cs] = 0;
            cs_pos_set[cs] = cs_pos_set[cs] & ~2;
            return;
        }
        cs_right[cs] = css_value_len(cs, val_off, val_len);
        cs_pos_set[cs] = cs_pos_set[cs] | 2;
        return;
    }
    if (prop == CP_BOTTOM) {
        if (css_value_keyword(val_off, val_len, "auto")) {
            cs_bottom[cs] = 0;
            cs_pos_set[cs] = cs_pos_set[cs] & ~4;
            return;
        }
        cs_bottom[cs] = css_value_len(cs, val_off, val_len);
        cs_pos_set[cs] = cs_pos_set[cs] | 4;
        return;
    }
    if (prop == CP_LEFT) {
        if (css_value_keyword(val_off, val_len, "auto")) {
            cs_left[cs] = 0;
            cs_pos_set[cs] = cs_pos_set[cs] & ~8;
            return;
        }
        cs_left[cs] = css_value_len(cs, val_off, val_len);
        cs_pos_set[cs] = cs_pos_set[cs] | 8;
        return;
    }
    if (prop == CP_Z_INDEX) {
        if (css_value_keyword(val_off, val_len, "auto")) {
            cs_z_index[cs] = 0;
            cs_pos_set[cs] = cs_pos_set[cs] & ~16;
            return;
        }
        cs_pos_set[cs] = cs_pos_set[cs] | 16;
        int v = 0;
        int neg = 0;
        int k = val_off;
        if (k < val_off + val_len && css_value_pool[k] == '-') {
            neg = 1;
            k = k + 1;
        }
        while (k < val_off + val_len && css_value_pool[k] >= '0' && css_value_pool[k] <= '9') {
            v = v * 10 + (css_value_pool[k] - '0');
            k = k + 1;
        }
        cs_z_index[cs] = neg ? -v : v;
        return;
    }
    if (prop == CP_FLOAT) {
        if (css_value_keyword(val_off, val_len, "left")) {
            cs_float[cs] = FLOAT_LEFT;
            return;
        }
        if (css_value_keyword(val_off, val_len, "right")) {
            cs_float[cs] = FLOAT_RIGHT;
            return;
        }
        cs_float[cs] = FLOAT_NONE;
        return;
    }
    if (prop == CP_CLEAR) {
        if (css_value_keyword(val_off, val_len, "left")) {
            cs_clear[cs] = CLEAR_LEFT;
            return;
        }
        if (css_value_keyword(val_off, val_len, "right")) {
            cs_clear[cs] = CLEAR_RIGHT;
            return;
        }
        if (css_value_keyword(val_off, val_len, "both")) {
            cs_clear[cs] = CLEAR_BOTH;
            return;
        }
        cs_clear[cs] = CLEAR_NONE;
        return;
    }
    if (prop == CP_WHITE_SPACE) {
        if (css_value_keyword(val_off, val_len, "pre")) {
            cs_white_space[cs] = WS_PRE;
            return;
        }
        if (css_value_keyword(val_off, val_len, "nowrap")) {
            cs_white_space[cs] = WS_NOWRAP;
            return;
        }
        cs_white_space[cs] = WS_NORMAL;
        return;
    }
    if (prop == CP_LIST_STYLE_TYPE) {
        if (css_value_keyword(val_off, val_len, "decimal")) {
            cs_list_style[cs] = LS_DECIMAL;
            return;
        }
        if (css_value_keyword(val_off, val_len, "circle")) {
            cs_list_style[cs] = LS_CIRCLE;
            return;
        }
        if (css_value_keyword(val_off, val_len, "square")) {
            cs_list_style[cs] = LS_SQUARE;
            return;
        }
        if (css_value_keyword(val_off, val_len, "none")) {
            cs_list_style[cs] = LS_NONE;
            return;
        }
        cs_list_style[cs] = LS_DISC;
        return;
    }
    if (prop == CP_VERTICAL_ALIGN) {
        if (css_value_keyword(val_off, val_len, "top")) {
            cs_vertical_align[cs] = VA_TOP;
            return;
        }
        if (css_value_keyword(val_off, val_len, "middle")) {
            cs_vertical_align[cs] = VA_MIDDLE;
            return;
        }
        if (css_value_keyword(val_off, val_len, "bottom")) {
            cs_vertical_align[cs] = VA_BOTTOM;
            return;
        }
        cs_vertical_align[cs] = VA_BASELINE;
        return;
    }
    if (prop == CP_LINE_HEIGHT) {
        if (css_value_keyword(val_off, val_len, "normal")) {
            cs_line_height[cs] = -1;
            cs_line_height_mult[cs] = 0;
            return;
        }
        int v;
        int m;
        if (parse_line_height(val_off, val_len, &v, &m)) {
            cs_line_height[cs] = v;
            cs_line_height_mult[cs] = m;
        }
        return;
    }
    if (prop == CP_BORDER_RADIUS) {
        /* Single value only (uniform corners). Future: 1-4 value parsing
         * matching margin/padding shorthand. Reference Blink
         * core/css/parser/CSSPropertyBorderRadiusUtils.cpp.*/
        int r = css_value_len(cs, val_off, val_len);
        if (r < 0)
            r = 0;
        if (r > 64)
            r = 64; /* clamp; gfx2d_rect_round_fill caps too */
        cs_border_radius[cs] = r;
        return;
    }
    if (prop == CP_OVERFLOW) {
        if (css_value_keyword(val_off, val_len, "hidden") ||
            css_value_keyword(val_off, val_len, "clip") ||
            css_value_keyword(val_off, val_len, "scroll") ||
            css_value_keyword(val_off, val_len, "auto")) {
            cs_overflow[cs] = OVERFLOW_HIDDEN;
            return;
        }
        cs_overflow[cs] = OVERFLOW_VISIBLE;
        return;
    }
    if (prop == CP_BOX_SHADOW) {
        /* Minimal v1: parse first <offset-x> <offset-y> [<color>?]. Skip
         * blur and spread radii (Blink supports them via CSSShadowValue;
         * we don't yet have a Gaussian blur path in gfx2d). "none" clears.
         * Reference: blink/Source/core/css/parser/CSSPropertyParser.cpp
         * (parseShadow).*/
        if (css_value_keyword(val_off, val_len, "none")) {
            cs_shadow_has[cs] = 0;
            return;
        }
        int i = val_off;
        int end = val_off + val_len;
        int dx = 0;
        int dy = 0;
        int col = 0x000000;
        int got_dx = 0;
        int got_dy = 0;
        while (i < end) {
            while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
                i = i + 1;
            if (i >= end)
                break;
            int t_start = i;
            while (i < end && css_value_pool[i] != ' ' && css_value_pool[i] != '\t')
                i = i + 1;
            int t_len = i - t_start;
            if (!got_dx && (css_value_pool[t_start] == '-' || css_value_pool[t_start] == '+' ||
                            (css_value_pool[t_start] >= '0' && css_value_pool[t_start] <= '9'))) {
                dx = css_value_len(cs, t_start, t_len);
                got_dx = 1;
                continue;
            }
            if (got_dx && !got_dy &&
                (css_value_pool[t_start] == '-' || css_value_pool[t_start] == '+' ||
                 (css_value_pool[t_start] >= '0' && css_value_pool[t_start] <= '9'))) {
                dy = css_value_len(cs, t_start, t_len);
                got_dy = 1;
                continue;
            }
            int c2;
            if (css_value_color(t_start, t_len, &c2)) {
                col = c2;
                continue;
            }
            /* Unknown token (blur radius / spread / inset) - ignore. */
        }
        if (got_dx && got_dy) {
            cs_shadow_has[cs] = 1;
            cs_shadow_dx[cs] = dx;
            cs_shadow_dy[cs] = dy;
            cs_shadow_color[cs] = col;
        }
        return;
    }
}
