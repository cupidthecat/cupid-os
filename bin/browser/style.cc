/* §2 Style resolver + UA stylesheet.
 *   - ua_default_style fills cs_*[cs] with UA defaults for a tag.
 *   - css_value_int / css_value_color / css_value_keyword decode a
 *     (val_off, val_len) slice of css_value_pool into a typed value.
 *   - cs_apply_property writes a single CP_* property onto a ComputedStyle.
 *   - apply_inline_style parses a style="..." attribute body and applies it.
 *   - sel_compound_matches / sel_chain_matches match a CSS selector chain
 *     against a DOM node + ancestor chain (descendant combinator only).
 *   - style_resolve_all walks every DOM node, allocates one ComputedStyle,
 *     and runs UA defaults -> author cascade (specificity, doc-order) ->
 *     inline style -> inheritance from parent. */

void ua_default_style(int tag, int cs) {
    /* Fill cs_*[cs] with sensible UA defaults for `tag`. Inherit-friendly defaults
     * (color, font_*, white_space, text_align, list_style) start at -1 / 0 so the
     * resolver can treat them as "unset, inherit from parent". */
    cs_color[cs] = -1;
    cs_bg[cs] = -1;
    cs_font_w[cs] = 400;
    cs_font_i[cs] = 0;
    cs_font_size_tier[cs] = 1;       /* default 8x8 */
    cs_text_align[cs] = TA_LEFT;
    cs_text_dec[cs] = 0;
    cs_display[cs] = DISP_INLINE;
    cs_margin[cs][0] = 0; cs_margin[cs][1] = 0;
    cs_margin[cs][2] = 0; cs_margin[cs][3] = 0;
    cs_margin_auto[cs][0] = 0; cs_margin_auto[cs][1] = 0;
    cs_margin_auto[cs][2] = 0; cs_margin_auto[cs][3] = 0;
    cs_padding[cs][0] = 0; cs_padding[cs][1] = 0;
    cs_padding[cs][2] = 0; cs_padding[cs][3] = 0;
    cs_border[cs][0] = 0; cs_border[cs][1] = 0;
    cs_border[cs][2] = 0; cs_border[cs][3] = 0;
    cs_border_color[cs] = 0x000000;
    cs_width[cs] = -1;
    cs_height[cs] = -1;
    cs_white_space[cs] = WS_NORMAL;
    cs_list_style[cs] = LS_DISC;
    cs_vertical_align[cs] = VA_BASELINE;
    cs_line_height[cs] = -1;        /* unset; falls back to tier_line_h */
    cs_line_height_mult[cs] = 0;
    cs_font_size_px[cs] = -1;       /* unset; inherits from parent */
    cs_font_family_off[cs] = -1;    /* unset; inherits from parent */
    cs_font_family_len[cs] = 0;
    cs_font_generic[cs] = FONTSYS_FAMILY_DEFAULT;
    cs_max_width[cs]  = -1;
    cs_min_width[cs]  = -1;
    cs_max_height[cs] = -1;
    cs_min_height[cs] = -1;
    cs_border_radius[cs] = 0;
    cs_overflow[cs]      = OVERFLOW_VISIBLE;
    cs_border_style[cs]  = BS_SOLID;
    cs_shadow_has[cs]    = 0;
    cs_shadow_dx[cs]     = 0;
    cs_shadow_dy[cs]     = 0;
    cs_shadow_color[cs]  = 0x000000;
    cs_position[cs] = POS_STATIC;
    cs_top[cs] = 0; cs_right[cs] = 0; cs_bottom[cs] = 0; cs_left[cs] = 0;
    cs_pos_set[cs] = 0;
    cs_z_index[cs] = 0;
    cs_float[cs] = FLOAT_NONE;
    cs_clear[cs] = CLEAR_NONE;
    cs_var_count[cs] = 0;
    cs_box_sizing[cs] = BOX_SIZING_CONTENT;
    cs_bg_grad[cs] = BG_GRAD_NONE;
    cs_bg_grad_c1[cs] = 0;
    cs_bg_grad_c2[cs] = 0;
    cs_flex_dir[cs] = FLEX_DIR_ROW;
    cs_justify[cs] = JUSTIFY_FLEX_START;
    cs_align_items[cs] = ALIGN_STRETCH;
    cs_flex_grow[cs] = 0;
    cs_flex_shrink[cs] = 100;
    cs_flex_basis[cs] = -1;
    cs_gap[cs] = 0;

    /* Per-tag overrides matching spec §2 UA stylesheet. Flat if/return chain
     * (CupidC parser recurses into nested else; long else-if chains overflow
     * its stack). */
    /* Heading + paragraph margins. Top is full spec (0.67-1em); bottom is
     * trimmed so headings sit closer to their following content. After the
     * parent-first-child collapse rule, the visible gap above an h2 that
     * leads a page is 0; the gap below the h2 down to the first paragraph
     * = max(h2.margin_b, p.margin_t) = max(8, 10) = 10. */
    if (tag == T_H1) {
        cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
        cs_font_size_px[cs] = 32;
        cs_margin[cs][0] = 21; cs_margin[cs][2] = 10;
        return;
    }
    if (tag == T_H2) {
        cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
        cs_font_size_px[cs] = 24;
        cs_margin[cs][0] = 20; cs_margin[cs][2] = 8;
        return;
    }
    if (tag == T_H3) {
        cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
        cs_font_size_px[cs] = 19;
        cs_margin[cs][0] = 19; cs_margin[cs][2] = 8;
        return;
    }
    if (tag == T_H4) {
        cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
        cs_font_size_px[cs] = 16;
        cs_margin[cs][0] = 18; cs_margin[cs][2] = 8;
        return;
    }
    if (tag == T_H5) {
        cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
        cs_font_size_px[cs] = 13;
        cs_margin[cs][0] = 16; cs_margin[cs][2] = 6;
        return;
    }
    if (tag == T_H6) {
        cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
        cs_font_size_px[cs] = 11;
        cs_margin[cs][0] = 16; cs_margin[cs][2] = 6;
        return;
    }
    if (tag == T_P) {
        cs_display[cs] = DISP_BLOCK;
        cs_margin[cs][0] = 10; cs_margin[cs][2] = 10;
        return;
    }
    if (tag == T_A) {
        /* Per HTML/CSS UA spec, only `:link` (an <a> with href) gets the
         * default blue/underline. Bare anchors used as targets (`<a>foo</a>`
         * without href) inherit color and have no decoration. cs index ==
         * DOM node index for element entries, so dom_attr_get is valid. */
        if (dom_attr_get(cs, "href") >= 0) {
            cs_color[cs] = 0x0000EE; cs_text_dec[cs] = TD_UNDERLINE;
        }
        return;
    }
    if (tag == T_PRE) {
        cs_display[cs] = DISP_BLOCK; cs_white_space[cs] = WS_PRE;
        cs_margin[cs][0] = 16; cs_margin[cs][2] = 16;
        cs_font_generic[cs] = FONTSYS_FAMILY_MONOSPACE;
        return;
    }
    if (tag == T_CODE) { cs_font_generic[cs] = FONTSYS_FAMILY_MONOSPACE; return; }
    if (tag == T_B || tag == T_STRONG) { cs_font_w[cs] = 700; return; }
    if (tag == T_I || tag == T_EM) {
        cs_font_i[cs] = 1;
        return;
    }
    if (tag == T_UL || tag == T_OL || tag == T_DL) {
        cs_display[cs] = DISP_BLOCK; cs_padding[cs][3] = 40;     /* CSS2 default */
        cs_margin[cs][0] = 16; cs_margin[cs][2] = 16;            /* 1em */
        if (tag == T_OL) cs_list_style[cs] = LS_DECIMAL;
        return;
    }
    if (tag == T_LI) { cs_display[cs] = DISP_LIST_ITEM; return; }
    if (tag == T_TABLE) { cs_display[cs] = DISP_TABLE; return; }
    if (tag == T_THEAD || tag == T_TBODY || tag == T_TFOOT) {
        cs_display[cs] = DISP_TABLE_ROW_GROUP;
        return;
    }
    if (tag == T_TR) { cs_display[cs] = DISP_TABLE_ROW; return; }
    if (tag == T_TD) {
        cs_display[cs] = DISP_TABLE_CELL;
        cs_padding[cs][0] = 2; cs_padding[cs][1] = 2;
        cs_padding[cs][2] = 2; cs_padding[cs][3] = 2;
        cs_border[cs][0] = 1; cs_border[cs][1] = 1;
        cs_border[cs][2] = 1; cs_border[cs][3] = 1;
        cs_border_color[cs] = 0xC0C0C0;
        return;
    }
    if (tag == T_TH) {
        cs_display[cs] = DISP_TABLE_CELL; cs_font_w[cs] = 700;
        cs_text_align[cs] = TA_CENTER;
        cs_padding[cs][0] = 2; cs_padding[cs][1] = 2;
        cs_padding[cs][2] = 2; cs_padding[cs][3] = 2;
        cs_border[cs][0] = 1; cs_border[cs][1] = 1;
        cs_border[cs][2] = 1; cs_border[cs][3] = 1;
        cs_border_color[cs] = 0xC0C0C0;
        return;
    }
    if (tag == T_HR) {
        cs_display[cs] = DISP_BLOCK; cs_height[cs] = 1;
        cs_bg[cs] = 0x808080;
        cs_margin[cs][0] = 8; cs_margin[cs][2] = 8;
        return;
    }
    if (tag == T_BLOCKQUOTE) {
        cs_display[cs] = DISP_BLOCK; cs_padding[cs][3] = 16;
        cs_margin[cs][0] = 8; cs_margin[cs][2] = 8;
        cs_border[cs][3] = 4;
        cs_border_color[cs] = 0xCCCCCC;
        return;
    }
    if (tag == T_DIV) { cs_display[cs] = DISP_BLOCK; return; }
    if (tag == T_HEADER || tag == T_FOOTER || tag == T_NAV ||
        tag == T_SECTION || tag == T_ARTICLE || tag == T_ASIDE ||
        tag == T_MAIN) {
        cs_display[cs] = DISP_BLOCK;
        return;
    }
    if (tag == T_FORM) { cs_display[cs] = DISP_BLOCK; return; }
    if (tag == T_INPUT || tag == T_BUTTON) { cs_display[cs] = DISP_INLINE_BLOCK; return; }
    if (tag == T_IMG) { cs_display[cs] = DISP_INLINE_BLOCK; return; }
    if (tag == T_BODY) {
        cs_display[cs] = DISP_BLOCK;
        /* Real browsers collapse body's top margin with the first child's
         * top margin (spec margin-collapsing through a zero-padding parent).
         * We don't implement parent/first-child collapse yet, so set the
         * top to 0; the visible gap above the first heading is then the
         * heading's own UA margin-top (e.g. h1 21px, h2 20px), matching
         * the post-collapse result a real browser shows. */
        cs_margin[cs][0] = 0; cs_margin[cs][1] = 8;
        cs_margin[cs][2] = 8; cs_margin[cs][3] = 8;
        return;
    }
    if (tag == T_HTML) {
        cs_display[cs] = DISP_BLOCK;
        cs_font_size_px[cs] = 16;       /* root font-size baseline (rem unit) */
        return;
    }
    if (tag == T_ROOT) {
        cs_display[cs] = DISP_BLOCK;
        cs_font_size_px[cs] = 16;
        return;
    }
    if (tag == T_HEAD || tag == T_SCRIPT || tag == T_STYLE ||
        tag == T_NOSCRIPT || tag == T_TITLE) {
        /* <title> never renders as visible body content: even when the
         * parser fails to wrap it in an implicit <head> (e.g. `<!doctype
         * html><title>x</title><body>...`), display:none keeps it out of
         * the layout tree. Title text remains accessible via title_buf. */
        cs_display[cs] = DISP_NONE;
        return;
    }
    if (tag == T_DT) { cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700; return; }
    if (tag == T_DD) { cs_display[cs] = DISP_BLOCK; cs_padding[cs][3] = 24; return; }
    if (tag == T_TEXTAREA) { cs_display[cs] = DISP_INLINE_BLOCK; return; }
    if (tag == T_OPTION) { cs_display[cs] = DISP_BLOCK; return; }
    if (tag == T_CAPTION) { cs_display[cs] = DISP_TABLE_CAPTION; return; }
}

/* Step 5.1: value parsers */

int css_value_int(int off, int len) {
    /* Parse a length with optional 'px' / 'pt' / 'em' / 'vw' / 'vh' / '%'
     * suffix. Tracks the decimal part via fixed-point millipixels so '1.5em'
     * rounds to 24, not 16, before unit conversion. */
    int sign = 1;
    int i = off;
    int end = off + len;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
    if (i < end && css_value_pool[i] == '-') { sign = -1; i = i + 1; }
    int milli = 0;       /* value * 1000 */
    while (i < end && css_value_pool[i] >= '0' && css_value_pool[i] <= '9') {
        milli = milli * 10 + (css_value_pool[i] - '0');
        i = i + 1;
    }
    milli = milli * 1000;
    if (i < end && css_value_pool[i] == '.') {
        i = i + 1;
        int place = 100;
        while (i < end && css_value_pool[i] >= '0' && css_value_pool[i] <= '9') {
            if (place > 0) {
                milli = milli + (css_value_pool[i] - '0') * place;
                place = place / 10;
            }
            i = i + 1;
        }
    }
    if (i < end) {
        int a = css_value_pool[i];
        int b = (i + 1 < end) ? css_value_pool[i+1] : 0;
        int c2 = (i + 2 < end) ? css_value_pool[i+2] : 0;
        if (a == 'r' && b == 'e' && c2 == 'm') { milli = milli * 16; } /* root em */
        else if (a == 'p' && b == 't') { milli = (milli * 4) / 3; }
        else if (a == 'e' && b == 'm') { milli = milli * 16; }   /* 1em = 16px */
        else if (a == 'v' && b == 'w') {
            int vw_base = cur_cw - 12;
            if (vw_base < 0) vw_base = 0;
            milli = (milli * vw_base) / 100;
        }
        else if (a == 'v' && b == 'h') {
            int vh_base = cur_ch - ADDR_H - STATUS_H - 2;
            if (vh_base < 0) vh_base = 0;
            milli = (milli * vh_base) / 100;
        }
        else if (a == '%') {
            int pw_base = cur_cw - 12;
            if (pw_base < 0) pw_base = 0;
            milli = (milli * pw_base) / 100;
        }
        /* px or unknown unit: treat as px (no scaling) */
    }
    return (milli / 1000) * sign;
}

/* Resolve a CSS length value to px in the context of computed-style index
 * `cs`. Differs from css_value_int by:
 *   - em uses cs's own font-size (walking up if still unset),
 *   - rem uses html (cs index 1) font-size,
 *   - % uses viewport content width as a stable proxy for the containing
 *     block width (true containing-block-relative resolution would need
 *     layout-time deferral; the proxy is accurate at the body level).
 * Negative values supported. Returns 0 on parse failure. */
int css_value_len(int cs, int off, int len) {
    /* Resolve var() and calc() before parsing as a length. var() goes
     * first because var() can appear inside calc() (`calc(var(--g) -
     * 4px)`), and the calc() evaluator wants concrete numbers + units. */
    char rbuf[1024];
    int rlen = 0;
    int has_var = 0;
    for (int sc = 0; sc + 4 <= len; sc = sc + 1) {
        if (css_value_pool[off + sc] == 'v' &&
            css_value_pool[off + sc + 1] == 'a' &&
            css_value_pool[off + sc + 2] == 'r' &&
            css_value_pool[off + sc + 3] == '(') { has_var = 1; break; }
    }
    char *src;
    int src_len;
    if (has_var) {
        rlen = cs_var_substitute(cs, off, len, rbuf, 1024);
        src = rbuf;
        src_len = rlen;
    } else {
        src = css_value_pool + off;
        src_len = len;
    }
    /* calc(...) handling. Strip the outer 'calc(' / ')' and pass the
     * inner expression to eval_calc. base_px is the containing-block
     * width proxy used by '%' leaves. */
    int ci = 0;
    while (ci < src_len && (src[ci] == ' ' || src[ci] == '\t')) ci = ci + 1;
    if (ci + 5 <= src_len &&
        src[ci] == 'c' && src[ci+1] == 'a' && src[ci+2] == 'l' &&
        src[ci+3] == 'c' && src[ci+4] == '(') {
        int inner = ci + 5;
        int depth = 1;
        int e = inner;
        while (e < src_len && depth > 0) {
            if (src[e] == '(') depth = depth + 1;
            else if (src[e] == ')') {
                depth = depth - 1;
                if (depth == 0) break;
            }
            e = e + 1;
        }
        int base_px_calc = cur_cw - 12;
        if (base_px_calc < 0) base_px_calc = 0;
        int got = eval_calc(src + inner, e - inner, base_px_calc);
        if (got >= 0) return got;
        return 0;
    }
    int sign = 1;
    int i = 0;
    int end = src_len;
    while (i < end && (src[i] == ' ' || src[i] == '\t')) i = i + 1;
    if (i < end && src[i] == '-') { sign = -1; i = i + 1; }
    if (i >= end) return 0;
    if (src[i] < '0' || src[i] > '9') return 0;
    int int_part = 0;
    while (i < end && src[i] >= '0' && src[i] <= '9') {
        int_part = int_part * 10 + (src[i] - '0');
        i = i + 1;
    }
    int frac = 0;
    if (i < end && src[i] == '.') {
        i = i + 1;
        int digits = 0;
        while (i < end && src[i] >= '0' && src[i] <= '9') {
            if (digits < 2) frac = frac * 10 + (src[i] - '0');
            digits = digits + 1;
            i = i + 1;
        }
        if (digits == 1) frac = frac * 10;
    }
    int v_x100 = int_part * 100 + frac;
    while (i < end && (src[i] == ' ' || src[i] == '\t')) i = i + 1;

    int base_px = (cs >= 0 && cs < cs_count) ? cs_font_size_px[cs] : 16;
    if (base_px <= 0) {
        int p = (cs >= 0) ? n_parent[cs] : -1;
        while (p >= 0 && cs_font_size_px[p] <= 0) p = n_parent[p];
        base_px = (p >= 0 && cs_font_size_px[p] > 0) ? cs_font_size_px[p] : 16;
    }
    int root_px = (cs_count >= 2 && cs_font_size_px[1] > 0) ? cs_font_size_px[1] : 16;

    int result;
    if (i >= end) {
        result = v_x100 / 100;
    } else {
        int a = src[i];
        int b = (i + 1 < end) ? src[i+1] : 0;
        int c2 = (i + 2 < end) ? src[i+2] : 0;
        if (a == 'p' && b == 'x') {
            result = v_x100 / 100;
        } else if (a == 'p' && b == 't') {
            result = (v_x100 * 4) / (3 * 100);
        } else if (a == 'r' && b == 'e' && c2 == 'm') {
            result = (v_x100 * root_px) / 100;
        } else if (a == 'e' && b == 'm') {
            result = (v_x100 * base_px) / 100;
        } else if (a == 'v' && b == 'w') {
            int vw = cur_cw - 12;
            if (vw < 0) vw = 0;
            result = (v_x100 * vw) / 10000;
        } else if (a == 'v' && b == 'h') {
            int vh = cur_ch - ADDR_H - STATUS_H - 2;
            if (vh < 0) vh = 0;
            result = (v_x100 * vh) / 10000;
        } else if (a == '%') {
            int vw = cur_cw - 12;
            if (vw < 0) vw = 0;
            result = (v_x100 * vw) / 10000;
        } else {
            result = v_x100 / 100;
        }
    }
    return result * sign;
}

int css_value_is_auto(int off, int len) {
    int i = off;
    int end = off + len;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i++;
    if (end - i < 4) return 0;
    return (css_value_pool[i]   == 'a' &&
            css_value_pool[i+1] == 'u' &&
            css_value_pool[i+2] == 't' &&
            css_value_pool[i+3] == 'o');
}

int css_value_color(int off, int len, int *out) {
    /* Reuse parse_color (in dom.cc). Accepts #RGB / #RRGGBB / named colors. */
    char tmp[64];
    if (len >= 64) len = 63;
    int k = 0;
    while (k < len) { tmp[k] = css_value_pool[off + k]; k = k + 1; }
    tmp[len] = 0;
    return parse_color(tmp, out);
}

int css_value_keyword(int off, int len, char *kw) {
    int kl = b_strlen(kw);
    if (len < kl) return 0;
    return b_strieq_n(css_value_pool + off, kw, kl);
}

/* Parse linear-gradient(<dir>?, <color>, <color>) into a 2-stop cardinal
 * gradient. Returns 1 on success and writes type (BG_GRAD_HORIZONTAL or
 * BG_GRAD_VERTICAL) plus c1/c2. Direction tokens accepted: "to top",
 * "to right", "to bottom", "to left", "0deg" / "90deg" / "180deg" / "270deg".
 * Default (no direction) is "to bottom" per CSS Images Module §3.1.
 * Reference: blink/Source/core/css/CSSGradientValue.cpp
 * (CSSGradientValue::parseLengthsAndPercents + applyAndScaleStops). */
int css_value_linear_gradient(int off, int len,
                              int *out_type, int *out_c1, int *out_c2) {
    char *s = css_value_pool;
    int p = off;
    int e = off + len;
    while (p < e && (s[p] == ' ' || s[p] == '\t')) p = p + 1;
    int kw_len = 15;       /* "linear-gradient" */
    if (p + kw_len + 1 >= e) return 0;
    if (!b_strieq_n(s + p, "linear-gradient", kw_len)) return 0;
    p = p + kw_len;
    while (p < e && (s[p] == ' ' || s[p] == '\t')) p = p + 1;
    if (p >= e || s[p] != '(') return 0;
    p = p + 1;
    int args_open = p;
    int depth = 1;
    int q = p;
    while (q < e && depth > 0) {
        if (s[q] == '(') depth = depth + 1;
        else if (s[q] == ')') depth = depth - 1;
        if (depth > 0) q = q + 1;
    }
    if (depth != 0) return 0;
    int args_end = q;
    /* Split args on top-level commas. Cap at 8 args. */
    int args_off[8];
    int args_len[8];
    int arg_count = 0;
    int seg_start = args_open;
    int dep = 0;
    int r;
    for (r = args_open; r < args_end; r = r + 1) {
        char ch = s[r];
        if (ch == '(') dep = dep + 1;
        else if (ch == ')') dep = dep - 1;
        if (dep == 0 && ch == ',') {
            if (arg_count < 8) {
                args_off[arg_count] = seg_start;
                args_len[arg_count] = r - seg_start;
                arg_count = arg_count + 1;
            }
            seg_start = r + 1;
        }
    }
    if (arg_count < 8) {
        args_off[arg_count] = seg_start;
        args_len[arg_count] = args_end - seg_start;
        arg_count = arg_count + 1;
    }
    if (arg_count < 2) return 0;
    /* Trim arg 0 for direction detection. */
    int a0_off = args_off[0];
    int a0_len = args_len[0];
    while (a0_len > 0 && (s[a0_off] == ' ' || s[a0_off] == '\t')) {
        a0_off = a0_off + 1; a0_len = a0_len - 1;
    }
    while (a0_len > 0 && (s[a0_off + a0_len - 1] == ' ' ||
                          s[a0_off + a0_len - 1] == '\t')) {
        a0_len = a0_len - 1;
    }
    int direction_idx = -1;        /* 0=top, 1=right, 2=bottom, 3=left */
    int c1_arg = 0;
    int c2_arg = 1;
    if (a0_len >= 5 &&
        (s[a0_off] == 't' || s[a0_off] == 'T') &&
        (s[a0_off + 1] == 'o' || s[a0_off + 1] == 'O') &&
        (s[a0_off + 2] == ' ' || s[a0_off + 2] == '\t')) {
        int o = a0_off + 3;
        int last = a0_off + a0_len;
        while (o < last && (s[o] == ' ' || s[o] == '\t')) o = o + 1;
        int dlen = last - o;
        if (dlen >= 6 && b_strieq_n(s + o, "bottom", 6)) direction_idx = 2;
        else if (dlen >= 5 && b_strieq_n(s + o, "right",  5)) direction_idx = 1;
        else if (dlen >= 4 && b_strieq_n(s + o, "left",   4)) direction_idx = 3;
        else if (dlen >= 3 && b_strieq_n(s + o, "top",    3)) direction_idx = 0;
        if (direction_idx < 0) return 0;
        c1_arg = 1; c2_arg = 2;
    } else {
        /* "<N>deg" form. */
        int v = 0;
        int q2 = 0;
        while (q2 < a0_len && s[a0_off + q2] >= '0' && s[a0_off + q2] <= '9') {
            v = v * 10 + (s[a0_off + q2] - '0');
            q2 = q2 + 1;
        }
        if (q2 > 0 && q2 + 2 < a0_len &&
            b_strieq_n(s + a0_off + q2, "deg", 3)) {
            int deg = v % 360;
            if (deg < 0) deg = deg + 360;
            if      (deg ==   0) direction_idx = 0;
            else if (deg ==  90) direction_idx = 1;
            else if (deg == 180) direction_idx = 2;
            else if (deg == 270) direction_idx = 3;
            else                 direction_idx = 2;     /* nearest cardinal */
            c1_arg = 1; c2_arg = 2;
        } else {
            direction_idx = 2;       /* default "to bottom" */
            c1_arg = 0; c2_arg = 1;
        }
    }
    if (c2_arg >= arg_count) return 0;
    int c1 = 0;
    int c2 = 0;
    if (!css_value_color(args_off[c1_arg], args_len[c1_arg], &c1)) return 0;
    if (!css_value_color(args_off[c2_arg], args_len[c2_arg], &c2)) return 0;
    int type = BG_GRAD_VERTICAL;
    if (direction_idx == 1 || direction_idx == 3) type = BG_GRAD_HORIZONTAL;
    /* gfx2d_gradient_v paints c1 at top -> c2 at bottom; gfx2d_gradient_h
     * paints c1 at left -> c2 at right. "to top" / "to left" reverse the
     * stop order so the named edge ends up holding the second color. */
    if (direction_idx == 0 || direction_idx == 3) {
        int tmp = c1; c1 = c2; c2 = tmp;
    }
    *out_type = type;
    *out_c1 = c1;
    *out_c2 = c2;
    return 1;
}

/* Map computed font-size in px to the kernel's three-tier glyph render set.
 *   tier 0 = SMALL  6x8
 *   tier 1/2 = NORMAL 8x8
 *   tier 3/4 = LARGE  16x16  (8x8 doubled)
 * Buckets:
 *   <= 14 px  -> SMALL    (h5, h6, small/x-small body)
 *   15-18 px  -> NORMAL   (body, h4, medium)
 *   19-26 px  -> LARGE-3  (h2, h3, large/x-large)
 *   >= 27 px  -> LARGE-4  (h1, xx-large): same glyph, taller line box. */
int px_to_tier(int px) {
    if (px <= 0)  return 1;
    if (px <= 14) return 0;
    if (px <= 18) return 1;
    if (px <= 26) return 3;
    return 4;
}

/* Parse a CSS length / size value into px. Handles:
 *   <int>         -> int (treated as px)
 *   <num>px       -> num
 *   <num>em       -> num * parent_px
 *   <num>rem      -> num * root_px
 *   <num>%        -> num/100 * parent_px
 *   keyword font-size set: xx-small/x-small/small/medium/large/x-large/xx-large
 * Returns -1 on parse failure. */
int parse_length_px(int off, int len, int parent_px, int root_px) {
    int i = off;
    int end = off + len;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
    if (i >= end) return -1;
    int klen = end - i;
    if (klen >= 8  && css_value_keyword(i, klen, "xx-small")) return 9;
    if (klen >= 7  && css_value_keyword(i, klen, "x-small"))  return 11;
    if (klen >= 6  && css_value_keyword(i, klen, "medium"))   return 16;
    if (klen >= 7  && css_value_keyword(i, klen, "x-large"))  return 24;
    if (klen >= 8  && css_value_keyword(i, klen, "xx-large")) return 32;
    if (klen >= 5  && css_value_keyword(i, klen, "small"))    return 13;
    if (klen >= 5  && css_value_keyword(i, klen, "large"))    return 19;
    if (css_value_pool[i] < '0' || css_value_pool[i] > '9') return -1;
    int int_part = 0;
    while (i < end && css_value_pool[i] >= '0' && css_value_pool[i] <= '9') {
        int_part = int_part * 10 + (css_value_pool[i] - '0');
        i = i + 1;
    }
    int frac = 0;
    if (i < end && css_value_pool[i] == '.') {
        i = i + 1;
        int digits = 0;
        while (i < end && css_value_pool[i] >= '0' && css_value_pool[i] <= '9') {
            if (digits < 2) frac = frac * 10 + (css_value_pool[i] - '0');
            digits = digits + 1;
            i = i + 1;
        }
        if (digits == 1) frac = frac * 10;
    }
    int v_x100 = int_part * 100 + frac;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
    if (i >= end) return v_x100 / 100;
    if (i + 1 < end && css_value_pool[i] == 'p' && css_value_pool[i+1] == 'x') {
        return v_x100 / 100;
    }
    if (i + 2 < end &&
        css_value_pool[i] == 'r' && css_value_pool[i+1] == 'e' && css_value_pool[i+2] == 'm') {
        return (v_x100 * root_px) / 100;
    }
    if (i + 1 < end && css_value_pool[i] == 'e' && css_value_pool[i+1] == 'm') {
        return (v_x100 * parent_px) / 100;
    }
    if (css_value_pool[i] == '%') {
        return (v_x100 * parent_px) / 10000;
    }
    return v_x100 / 100;
}

/* Parse a `line-height` value into (px_or_mult, is_mult). Returns 1 on success.
 *   "30px" -> value=30,  is_mult=0
 *   "1.8"  -> value=180, is_mult=1     (multiplier x100)
 *   "2"    -> value=200, is_mult=1
 *   "normal"/anything else -> returns 0 (caller leaves sentinel) */
int parse_line_height(int off, int len, int *out_value, int *out_is_mult) {
    int i = off;
    int end = off + len;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
    if (i >= end) return 0;
    if (css_value_pool[i] < '0' || css_value_pool[i] > '9') return 0;
    int int_part = 0;
    while (i < end && css_value_pool[i] >= '0' && css_value_pool[i] <= '9') {
        int_part = int_part * 10 + (css_value_pool[i] - '0');
        i = i + 1;
    }
    int frac = 0;
    if (i < end && css_value_pool[i] == '.') {
        i = i + 1;
        int digits = 0;
        while (i < end && css_value_pool[i] >= '0' && css_value_pool[i] <= '9') {
            if (digits < 2) frac = frac * 10 + (css_value_pool[i] - '0');
            digits = digits + 1;
            i = i + 1;
        }
        if (digits == 1) frac = frac * 10;     /* "1.8" -> 80 */
    }
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
    if (i + 1 < end && css_value_pool[i] == 'p' && css_value_pool[i+1] == 'x') {
        *out_value = int_part;
        *out_is_mult = 0;
        return 1;
    }
    *out_value = int_part * 100 + frac;
    *out_is_mult = 1;
    return 1;
}

/* CSS custom property lookup. Walks the DOM ancestor chain (cs index ==
 * DOM node index for the document tree, see style_resolve_all) until it
 * finds a node whose cs_var_*[] table has a slot whose name matches the
 * `name` byte range. Returns 1 + sets *out_off / *out_len on hit, 0 on
 * miss. Cap the walk at 32 ancestors to bound circular-ref blow-ups
 * (deeply pathological pages); spec doesn't require a depth bound but
 * we'd rather degrade than reboot.
 *
 * Reference: Blink's CSSVariableResolver walks the inheritance chain
 * lazily at computed-style time. Same idea here, except we walk DOM
 * parents directly rather than through a separate inherited map. */
int cs_var_lookup(int cs, char *name, int name_len, int *out_off, int *out_len) {
    int cur = cs;
    int hops = 0;
    while (cur >= 0 && hops < 32) {
        int n = cs_var_count[cur];
        for (int k = 0; k < n; k = k + 1) {
            if (cs_var_name_len[cur][k] != name_len) continue;
            int eq = 1;
            int o = cs_var_name_off[cur][k];
            for (int b = 0; b < name_len; b = b + 1) {
                if (css_value_pool[o + b] != name[b]) { eq = 0; break; }
            }
            if (eq) {
                *out_off = cs_var_val_off[cur][k];
                *out_len = cs_var_val_len[cur][k];
                return 1;
            }
        }
        cur = n_parent[cur];
        hops = hops + 1;
    }
    return 0;
}

/* Substitute every var(--name [, fallback]) inside the value byte range
 * (val_off, val_len) of css_value_pool into `out` (sized `out_cap`).
 * Returns the number of bytes written. Lazy: called by parse_length /
 * parse_color / etc. before they consume the value. var() inside calc()
 * is intentionally NOT supported in v1; eval_calc gets the substituted
 * stream too so common patterns like `width: calc(var(--gutter) - 4px)`
 * work after the substitution pass.
 *
 * Reference: Blink CSSVariableResolver::resolveVariableReference. */
int cs_var_substitute(int cs, int val_off, int val_len, char *out, int out_cap) {
    int op = 0;
    int i = 0;
    while (i < val_len && op < out_cap - 1) {
        /* Detect "var(" */
        if (i + 4 <= val_len &&
            css_value_pool[val_off + i] == 'v' &&
            css_value_pool[val_off + i + 1] == 'a' &&
            css_value_pool[val_off + i + 2] == 'r' &&
            css_value_pool[val_off + i + 3] == '(') {
            int p = i + 4;
            while (p < val_len && (css_value_pool[val_off + p] == ' ' ||
                                   css_value_pool[val_off + p] == '\t')) p = p + 1;
            int name_start = p;
            while (p < val_len && css_value_pool[val_off + p] != ',' &&
                   css_value_pool[val_off + p] != ')') p = p + 1;
            int name_end = p;
            while (name_end > name_start &&
                   (css_value_pool[val_off + name_end - 1] == ' ' ||
                    css_value_pool[val_off + name_end - 1] == '\t')) name_end = name_end - 1;
            int fallback_start = -1;
            int fallback_end = -1;
            if (p < val_len && css_value_pool[val_off + p] == ',') {
                p = p + 1;
                while (p < val_len && (css_value_pool[val_off + p] == ' ' ||
                                       css_value_pool[val_off + p] == '\t')) p = p + 1;
                fallback_start = p;
                int depth = 1;
                while (p < val_len && depth > 0) {
                    if (css_value_pool[val_off + p] == '(') depth = depth + 1;
                    else if (css_value_pool[val_off + p] == ')') {
                        depth = depth - 1;
                        if (depth == 0) break;
                    }
                    p = p + 1;
                }
                fallback_end = p;
                while (fallback_end > fallback_start &&
                       (css_value_pool[val_off + fallback_end - 1] == ' ' ||
                        css_value_pool[val_off + fallback_end - 1] == '\t')) fallback_end = fallback_end - 1;
            }
            if (p < val_len && css_value_pool[val_off + p] == ')') p = p + 1;
            int v_off = -1;
            int v_len = 0;
            int hit = cs_var_lookup(cs, css_value_pool + val_off + name_start,
                                    name_end - name_start, &v_off, &v_len);
            if (hit) {
                /* Recursively substitute in case the var's value also
                 * contains var() references. Bound depth by checking
                 * we don't re-enter the same name; for v1 we simply
                 * cap chained var() expansion at 4 hops via this byte
                 * limit (the resolver caps the ancestor walk too). */
                char tmp[1024];
                int tn = cs_var_substitute(cs, v_off, v_len, tmp, 1024);
                for (int k = 0; k < tn && op < out_cap - 1; k = k + 1) out[op++] = tmp[k];
            } else if (fallback_start >= 0) {
                char tmp2[1024];
                int tn2 = cs_var_substitute(cs, val_off + fallback_start,
                                            fallback_end - fallback_start,
                                            tmp2, 1024);
                for (int k = 0; k < tn2 && op < out_cap - 1; k = k + 1) out[op++] = tmp2[k];
            }
            /* On unresolved with no fallback the substitution emits
             * nothing for this var() — the consumer typically falls
             * back to the property's initial value, which is what
             * Blink's behaviour reduces to. */
            i = p;
            continue;
        }
        out[op++] = css_value_pool[val_off + i];
        i = i + 1;
    }
    out[op] = 0;
    return op;
}

/* CSS calc() recursive-descent evaluator.
 *
 * Tracks a {px, pct, num} accumulator: lengths split into a pixel
 * component and a percent component (Blink CSSCalcValue accumulates
 * the same way at compute time, see CSSCalculationValue.cpp around
 * lines 413-438), plus a unit-less number flag for `calc(2 * 8px)`.
 * Final pixel value = px + (pct/100) * base_px.
 *
 * Grammar:
 *   expr  = mul ( ('+' | '-') mul )*
 *   mul   = term ( ('*' | '/') term )*
 *   term  = '(' expr ')' | '-' term | number unit?
 *
 * Limitations vs spec:
 *   - em / rem leaves not supported (treated as px). Most modern sites
 *     prefer px or % inside calc anyway.
 *   - Mixed length+percent in mul/div not flagged as errors; the result
 *     keeps both buckets which may produce a meaningless value, matching
 *     the intent without enforcing CSSCalcValue's full type matrix.
 */

/* calc() recursive-descent evaluator. State carried in module-level
 * globals (cupidc has limited struct-pointer codegen for nested
 * `s->t[s->p]` patterns and for multi-init declarators inside hot
 * loops, so the simpler globals approach is more reliable). The
 * recursion is bounded by parenthesis depth in source, so concurrent
 * top-level invocations would clobber state — the current call sites
 * never recurse from within eval_calc itself.
 *
 * Reference: blink/Source/core/css/CSSCalculationValue.cpp accumulator
 * pattern — track separate `pixels` and `percent` buckets so
 * `calc(100% - 16px)` stays exact at compute time. */
char *ec_t;
int ec_n;
int ec_p;
int ec_err;

void ec_skip_ws(void) {
    while (ec_p < ec_n && (ec_t[ec_p] == ' ' || ec_t[ec_p] == '\t')) ec_p = ec_p + 1;
}

void ec_eval_expr(int *px, int *pct, int *is_num);

void ec_eval_term(int *px, int *pct, int *is_num) {
    ec_skip_ws();
    if (ec_p >= ec_n) { ec_err = 1; *px = 0; *pct = 0; *is_num = 1; return; }
    if (ec_t[ec_p] == '(') {
        ec_p = ec_p + 1;
        ec_eval_expr(px, pct, is_num);
        ec_skip_ws();
        if (ec_p < ec_n && ec_t[ec_p] == ')') ec_p = ec_p + 1;
        else ec_err = 1;
        return;
    }
    if (ec_t[ec_p] == '+') {
        ec_p = ec_p + 1;
        ec_eval_term(px, pct, is_num);
        return;
    }
    /* Parse number, possibly with a leading '-' (unary minus is handled
     * here rather than as a separate term so `calc(100% - 32px)` parses
     * as 100% MINUS 32px, not 100% MINUS unary-(-32px); ec_eval_expr
     * already swallows the binary '-' before getting here). */
    int sign = 1;
    if (ec_t[ec_p] == '-') { sign = -1; ec_p = ec_p + 1; ec_skip_ws(); }
    int int_part = 0;
    int has_digit = 0;
    while (ec_p < ec_n && ec_t[ec_p] >= '0' && ec_t[ec_p] <= '9') {
        int_part = int_part * 10 + (ec_t[ec_p] - '0');
        has_digit = 1;
        ec_p = ec_p + 1;
    }
    int frac_num = 0;
    int frac_div = 1;
    if (ec_p < ec_n && ec_t[ec_p] == '.') {
        ec_p = ec_p + 1;
        while (ec_p < ec_n && ec_t[ec_p] >= '0' && ec_t[ec_p] <= '9') {
            frac_num = frac_num * 10 + (ec_t[ec_p] - '0');
            frac_div = frac_div * 10;
            has_digit = 1;
            ec_p = ec_p + 1;
        }
    }
    if (!has_digit) { ec_err = 1; *px = 0; *pct = 0; *is_num = 1; return; }
    *is_num = 1;
    *px = sign * int_part;
    *pct = 0;
    if (ec_p < ec_n) {
        char c0 = ec_t[ec_p];
        char c1 = (ec_p + 1 < ec_n) ? ec_t[ec_p + 1] : 0;
        if (c0 == '%') {
            *pct = sign * (int_part * 1000 + (frac_num * 1000) / frac_div);
            *px = 0;
            *is_num = 0;
            ec_p = ec_p + 1;
        } else if ((c0 == 'p' || c0 == 'P') && (c1 == 'x' || c1 == 'X')) {
            *px = sign * int_part;
            *is_num = 0;
            ec_p = ec_p + 2;
        } else if ((c0 == 'p' || c0 == 'P') && (c1 == 't' || c1 == 'T')) {
            int v_thirds = sign * int_part * 4;
            *px = v_thirds / 3;
            *is_num = 0;
            ec_p = ec_p + 2;
        } else if ((c0 == 'e' || c0 == 'E') && (c1 == 'm' || c1 == 'M')) {
            *px = sign * int_part;
            *is_num = 0;
            ec_p = ec_p + 2;
        }
    }
}

void ec_eval_mul(int *px, int *pct, int *is_num) {
    ec_eval_term(px, pct, is_num);
    while (!ec_err) {
        ec_skip_ws();
        if (ec_p >= ec_n) break;
        char op = ec_t[ec_p];
        if (op != '*' && op != '/') break;
        ec_p = ec_p + 1;
        int r_px = 0;
        int r_pct = 0;
        int r_is_num = 1;
        ec_eval_term(&r_px, &r_pct, &r_is_num);
        if (op == '*') {
            if (r_is_num) {
                *px = *px * r_px;
                *pct = *pct * r_px;
            } else if (*is_num) {
                int factor = *px;
                *px = factor * r_px;
                *pct = factor * r_pct;
                *is_num = 0;
            } else {
                ec_err = 1;
            }
        } else {
            int divisor;
            if (r_is_num) divisor = r_px;
            else divisor = 0;
            if (divisor == 0) { ec_err = 1; break; }
            *px = *px / divisor;
            *pct = *pct / divisor;
        }
    }
}

void ec_eval_expr(int *px, int *pct, int *is_num) {
    ec_eval_mul(px, pct, is_num);
    while (!ec_err) {
        ec_skip_ws();
        if (ec_p >= ec_n) break;
        char op = ec_t[ec_p];
        if (op != '+' && op != '-') break;
        ec_p = ec_p + 1;
        int r_px = 0;
        int r_pct = 0;
        int r_is_num = 1;
        ec_eval_mul(&r_px, &r_pct, &r_is_num);
        if (op == '+') {
            *px = *px + r_px;
            *pct = *pct + r_pct;
        } else {
            *px = *px - r_px;
            *pct = *pct - r_pct;
        }
        if (!r_is_num) *is_num = 0;
    }
}

int eval_calc(char *text, int len, int base_px) {
    ec_t = text;
    ec_n = len;
    ec_p = 0;
    ec_err = 0;
    int px = 0;
    int pct = 0;
    int is_num = 1;
    ec_eval_expr(&px, &pct, &is_num);
    if (ec_err) return -1;
    int from_pct = (pct * base_px) / 100000;
    return px + from_pct;
}

/* Step 5.2: single-property apply */

void cs_apply_property(int cs, int prop, int val_off, int val_len) {
    /* Flat if/return chain: CupidC parser overflows on long else-if chains.
     * Hoist `c` to function scope: CupidC keeps locals in a flat table, so
     * re-declaring `int c;` in multiple sibling branches conflicts. */
    int c;
    /* Pre-resolve var() so css_value_color / css_value_keyword don't
     * need to know about custom properties. css_value_len handles var()
     * + calc() itself (it needs `cs` for ancestor lookup). When the raw
     * value contains `var(`, substitute into a scratch buffer, intern
     * the result back into css_value_pool, and dispatch with the new
     * offsets. Skipped when no var() so the common case stays free. */
    int has_var_call = 0;
    for (int sc = 0; sc + 4 <= val_len; sc = sc + 1) {
        if (css_value_pool[val_off + sc] == 'v' &&
            css_value_pool[val_off + sc + 1] == 'a' &&
            css_value_pool[val_off + sc + 2] == 'r' &&
            css_value_pool[val_off + sc + 3] == '(') { has_var_call = 1; break; }
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
        if (css_value_color(val_off, val_len, &c)) cs_color[cs] = c;
        return;
    }
    if (prop == CP_BG_COLOR || prop == CP_BG) {
        int gtype = 0; int gc1 = 0; int gc2 = 0;
        if (css_value_linear_gradient(val_off, val_len,
                                      &gtype, &gc1, &gc2)) {
            cs_bg_grad[cs]    = gtype;
            cs_bg_grad_c1[cs] = gc1;
            cs_bg_grad_c2[cs] = gc2;
            cs_bg[cs]         = gc1;
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
        if (css_value_keyword(val_off, val_len, "bold")) { cs_font_w[cs] = 700; return; }
        if (css_value_keyword(val_off, val_len, "normal")) { cs_font_w[cs] = 400; return; }
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
         * used css_value_keyword which only matches the whole value. */
        cs_font_family_off[cs] = val_off;
        cs_font_family_len[cs] = val_len;
        int gi = val_off;
        int gend = val_off + val_len;
        cs_font_generic[cs] = FONTSYS_FAMILY_DEFAULT;
        while (gi < gend) {
            while (gi < gend && (css_value_pool[gi] == ' ' ||
                                 css_value_pool[gi] == '\t' ||
                                 css_value_pool[gi] == ',')) gi = gi + 1;
            if (gi >= gend) break;
            int t_start = gi;
            if (css_value_pool[gi] == '\'' || css_value_pool[gi] == '"') {
                char q = css_value_pool[gi];
                gi = gi + 1;
                while (gi < gend && css_value_pool[gi] != q) gi = gi + 1;
                if (gi < gend) gi = gi + 1;
            } else {
                while (gi < gend && css_value_pool[gi] != ',') gi = gi + 1;
            }
            int t_end = gi;
            while (t_end > t_start && (css_value_pool[t_end - 1] == ' ' ||
                                       css_value_pool[t_end - 1] == '\t'))
                t_end = t_end - 1;
            int t_len = t_end - t_start;
            if (t_len == 5 && b_strieq_n(css_value_pool + t_start, "serif", 5))
                { cs_font_generic[cs] = FONTSYS_FAMILY_SERIF; break; }
            if (t_len == 10 && b_strieq_n(css_value_pool + t_start, "sans-serif", 10))
                { cs_font_generic[cs] = FONTSYS_FAMILY_SANS_SERIF; break; }
            if (t_len == 9 && b_strieq_n(css_value_pool + t_start, "monospace", 9))
                { cs_font_generic[cs] = FONTSYS_FAMILY_MONOSPACE; break; }
            if (t_len == 7 && b_strieq_n(css_value_pool + t_start, "cursive", 7))
                { cs_font_generic[cs] = FONTSYS_FAMILY_CURSIVE; break; }
            if (t_len == 7 && b_strieq_n(css_value_pool + t_start, "fantasy", 7))
                { cs_font_generic[cs] = FONTSYS_FAMILY_FANTASY; break; }
            if (t_len == 9 && b_strieq_n(css_value_pool + t_start, "system-ui", 9))
                { cs_font_generic[cs] = FONTSYS_FAMILY_SYSTEM_UI; break; }
        }
        return;
    }
    if (prop == CP_FONT_SIZE) {
        /* Resolve to px against parent (em/%) and root (rem). cs index ==
         * DOM node index, so parent_cs == n_parent[cs] and the parent has
         * already been cascaded earlier in DOM order. */
        int parent_px = 16;
        int root_px = 16;
        int parent = n_parent[cs];
        if (parent >= 0 && parent < cs && cs_font_size_px[parent] > 0) {
            parent_px = cs_font_size_px[parent];
        }
        if (cs >= 1 && cs_font_size_px[1] > 0) root_px = cs_font_size_px[1];
        int px = parse_length_px(val_off, val_len, parent_px, root_px);
        if (px > 0) cs_font_size_px[cs] = px;
        return;
    }
    if (prop == CP_TEXT_ALIGN) {
        if (css_value_keyword(val_off, val_len, "center")) { cs_text_align[cs] = TA_CENTER; return; }
        if (css_value_keyword(val_off, val_len, "right")) { cs_text_align[cs] = TA_RIGHT; return; }
        cs_text_align[cs] = TA_LEFT;
        return;
    }
    if (prop == CP_TEXT_DEC) {
        cs_text_dec[cs] = 0;
        if (css_value_keyword(val_off, val_len, "underline"))    cs_text_dec[cs] = cs_text_dec[cs] | TD_UNDERLINE;
        if (css_value_keyword(val_off, val_len, "line-through")) cs_text_dec[cs] = cs_text_dec[cs] | TD_LINE_THROUGH;
        return;
    }
    if (prop == CP_DISPLAY) {
        if (css_value_keyword(val_off, val_len, "none"))         { cs_display[cs] = DISP_NONE; return; }
        if (css_value_keyword(val_off, val_len, "block"))        { cs_display[cs] = DISP_BLOCK; return; }
        if (css_value_keyword(val_off, val_len, "inline-block")) { cs_display[cs] = DISP_INLINE_BLOCK; return; }
        if (css_value_keyword(val_off, val_len, "inline-flex"))  { cs_display[cs] = DISP_INLINE_FLEX; return; }
        if (css_value_keyword(val_off, val_len, "inline"))       { cs_display[cs] = DISP_INLINE; return; }
        if (css_value_keyword(val_off, val_len, "list-item"))    { cs_display[cs] = DISP_LIST_ITEM; return; }
        if (css_value_keyword(val_off, val_len, "flex"))         { cs_display[cs] = DISP_FLEX; return; }
        if (css_value_keyword(val_off, val_len, "table"))        { cs_display[cs] = DISP_TABLE; return; }
        if (css_value_keyword(val_off, val_len, "table-row"))    { cs_display[cs] = DISP_TABLE_ROW; return; }
        if (css_value_keyword(val_off, val_len, "table-cell"))   { cs_display[cs] = DISP_TABLE_CELL; return; }
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
        if (css_value_keyword(val_off, val_len, "flex-end"))      cs_justify[cs] = JUSTIFY_FLEX_END;
        else if (css_value_keyword(val_off, val_len, "center"))   cs_justify[cs] = JUSTIFY_CENTER;
        else if (css_value_keyword(val_off, val_len, "space-between")) cs_justify[cs] = JUSTIFY_SPACE_BETWEEN;
        else if (css_value_keyword(val_off, val_len, "space-around"))  cs_justify[cs] = JUSTIFY_SPACE_AROUND;
        else if (css_value_keyword(val_off, val_len, "space-evenly"))  cs_justify[cs] = JUSTIFY_SPACE_EVENLY;
        else                                                       cs_justify[cs] = JUSTIFY_FLEX_START;
        return;
    }
    if (prop == CP_ALIGN_ITEMS) {
        if (css_value_keyword(val_off, val_len, "flex-start"))    cs_align_items[cs] = ALIGN_FLEX_START;
        else if (css_value_keyword(val_off, val_len, "flex-end")) cs_align_items[cs] = ALIGN_FLEX_END;
        else if (css_value_keyword(val_off, val_len, "center"))   cs_align_items[cs] = ALIGN_CENTER;
        else if (css_value_keyword(val_off, val_len, "baseline")) cs_align_items[cs] = ALIGN_BASELINE;
        else                                                       cs_align_items[cs] = ALIGN_STRETCH;
        return;
    }
    if (prop == CP_FLEX_GROW) {
        /* Parse number with up to 2 decimal places into hundredths so
         * `flex-grow: 1.5` stores 150. Plain integers come through as
         * 100 * value. */
        int v = 0;
        int frac = 0;
        int frac_digits = 0;
        int seen_dot = 0;
        int i = val_off;
        int end = val_off + val_len;
        while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
        while (i < end) {
            char c = css_value_pool[i];
            if (c >= '0' && c <= '9') {
                if (seen_dot) {
                    if (frac_digits < 2) { frac = frac * 10 + (c - '0'); frac_digits = frac_digits + 1; }
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
        while (frac_digits < 2) { frac = frac * 10; frac_digits = frac_digits + 1; }
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
         * (consumeFlex). */
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
         * with a px-style suffix becomes basis. */
        int tok_off[3];
        int tok_len[3];
        int tn = 0;
        int i = val_off;
        int end = val_off + val_len;
        while (i < end && tn < 3) {
            while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
            if (i >= end) break;
            int s = i;
            while (i < end && css_value_pool[i] != ' ' && css_value_pool[i] != '\t') i = i + 1;
            tok_off[tn] = s;
            tok_len[tn] = i - s;
            tn = tn + 1;
        }
        int g = 100;       /* default grow if first number missing */
        int sh = 100;
        int basis = 0;     /* `flex: 1` => basis: 0 per spec */
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
                    has_unit = 1; break;
                }
            }
            if (has_unit && !got_basis) {
                if (b_strieq_n(s, "auto", 4)) basis = -1;
                else                          basis = css_value_len(cs, tok_off[t], sl);
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
                        if (seen_dot) { if (frac_d < 2) { frac = frac * 10 + (c - '0'); frac_d = frac_d + 1; } }
                        else          v = v * 10 + (c - '0');
                    } else if (c == '.') seen_dot = 1;
                    else break;
                }
                while (frac_d < 2) { frac = frac * 10; frac_d = frac_d + 1; }
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
                        if (seen_dot) { if (frac_d < 2) { frac = frac * 10 + (c - '0'); frac_d = frac_d + 1; } }
                        else          v = v * 10 + (c - '0');
                    } else if (c == '.') seen_dot = 1;
                    else break;
                }
                while (frac_d < 2) { frac = frac * 10; frac_d = frac_d + 1; }
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
    if (prop == CP_MARGIN || prop == CP_PADDING) {
        int vals[4];
        int autos[4];
        vals[0] = 0; vals[1] = 0; vals[2] = 0; vals[3] = 0;
        autos[0] = 0; autos[1] = 0; autos[2] = 0; autos[3] = 0;
        int nvals = 0;
        int i = val_off;
        int end = val_off + val_len;
        while (i < end && nvals < 4) {
            while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
            if (i >= end) break;
            int v_start = i;
            while (i < end && css_value_pool[i] != ' ' && css_value_pool[i] != '\t') i = i + 1;
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
         * (top, right, bottom, left). */
        int side_v[4];
        int side_a[4];
        if (nvals == 1) {
            side_v[0] = vals[0]; side_v[1] = vals[0]; side_v[2] = vals[0]; side_v[3] = vals[0];
            side_a[0] = autos[0]; side_a[1] = autos[0]; side_a[2] = autos[0]; side_a[3] = autos[0];
        } else if (nvals == 2) {
            side_v[0] = vals[0]; side_v[1] = vals[1]; side_v[2] = vals[0]; side_v[3] = vals[1];
            side_a[0] = autos[0]; side_a[1] = autos[1]; side_a[2] = autos[0]; side_a[3] = autos[1];
        } else if (nvals == 3) {
            side_v[0] = vals[0]; side_v[1] = vals[1]; side_v[2] = vals[2]; side_v[3] = vals[1];
            side_a[0] = autos[0]; side_a[1] = autos[1]; side_a[2] = autos[2]; side_a[3] = autos[1];
        } else {
            side_v[0] = vals[0]; side_v[1] = vals[1]; side_v[2] = vals[2]; side_v[3] = vals[3];
            side_a[0] = autos[0]; side_a[1] = autos[1]; side_a[2] = autos[2]; side_a[3] = autos[3];
        }
        if (prop == CP_MARGIN) {
            cs_margin[cs][0] = side_v[0]; cs_margin[cs][1] = side_v[1];
            cs_margin[cs][2] = side_v[2]; cs_margin[cs][3] = side_v[3];
            cs_margin_auto[cs][0] = side_a[0]; cs_margin_auto[cs][1] = side_a[1];
            cs_margin_auto[cs][2] = side_a[2]; cs_margin_auto[cs][3] = side_a[3];
        } else {
            cs_padding[cs][0] = side_v[0]; cs_padding[cs][1] = side_v[1];
            cs_padding[cs][2] = side_v[2]; cs_padding[cs][3] = side_v[3];
        }
        return;
    }
    if (prop == CP_MARGIN_T)  {
        if (css_value_is_auto(val_off, val_len)) { cs_margin[cs][0] = 0; cs_margin_auto[cs][0] = 1; }
        else { cs_margin[cs][0] = css_value_len(cs, val_off, val_len); cs_margin_auto[cs][0] = 0; }
        return;
    }
    if (prop == CP_MARGIN_R)  {
        if (css_value_is_auto(val_off, val_len)) { cs_margin[cs][1] = 0; cs_margin_auto[cs][1] = 1; }
        else { cs_margin[cs][1] = css_value_len(cs, val_off, val_len); cs_margin_auto[cs][1] = 0; }
        return;
    }
    if (prop == CP_MARGIN_B)  {
        if (css_value_is_auto(val_off, val_len)) { cs_margin[cs][2] = 0; cs_margin_auto[cs][2] = 1; }
        else { cs_margin[cs][2] = css_value_len(cs, val_off, val_len); cs_margin_auto[cs][2] = 0; }
        return;
    }
    if (prop == CP_MARGIN_L)  {
        if (css_value_is_auto(val_off, val_len)) { cs_margin[cs][3] = 0; cs_margin_auto[cs][3] = 1; }
        else { cs_margin[cs][3] = css_value_len(cs, val_off, val_len); cs_margin_auto[cs][3] = 0; }
        return;
    }
    if (prop == CP_PADDING_T) { cs_padding[cs][0] = css_value_len(cs, val_off, val_len); return; }
    if (prop == CP_PADDING_R) { cs_padding[cs][1] = css_value_len(cs, val_off, val_len); return; }
    if (prop == CP_PADDING_B) { cs_padding[cs][2] = css_value_len(cs, val_off, val_len); return; }
    if (prop == CP_PADDING_L) { cs_padding[cs][3] = css_value_len(cs, val_off, val_len); return; }
    if (prop == CP_BORDER || prop == CP_BORDER_T || prop == CP_BORDER_R ||
        prop == CP_BORDER_B || prop == CP_BORDER_L) {
        /* Parse `<width> <style> <color>` shorthand. Width default 1px,
         * style ignored (treat any style as solid), color default keeps
         * existing cs_border_color. Sides: BORDER = all four,
         * BORDER_T/R/B/L = single side. */
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
            while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
            if (i >= end) break;
            int t_start = i;
            while (i < end && css_value_pool[i] != ' ' && css_value_pool[i] != '\t') i = i + 1;
            int t_len = i - t_start;
            /* numeric? */
            if (!got_width && css_value_pool[t_start] >= '0' &&
                css_value_pool[t_start] <= '9') {
                width = css_value_len(cs, t_start, t_len);
                got_width = 1;
                continue;
            }
            /* keyword styles: recognise dashed/dotted/none; everything
             * else (groove/ridge/double/inset/outset/hidden) collapses
             * to solid which is what we paint by default. */
            if (css_value_keyword(t_start, t_len, "dashed")) {
                cs_border_style[cs] = BS_DASHED; continue;
            }
            if (css_value_keyword(t_start, t_len, "dotted")) {
                cs_border_style[cs] = BS_DOTTED; continue;
            }
            if (css_value_keyword(t_start, t_len, "none") ||
                css_value_keyword(t_start, t_len, "hidden")) {
                cs_border_style[cs] = BS_NONE; continue;
            }
            if (css_value_keyword(t_start, t_len, "solid") ||
                css_value_keyword(t_start, t_len, "double") ||
                css_value_keyword(t_start, t_len, "groove") ||
                css_value_keyword(t_start, t_len, "ridge") ||
                css_value_keyword(t_start, t_len, "inset") ||
                css_value_keyword(t_start, t_len, "outset")) {
                cs_border_style[cs] = BS_SOLID; continue;
            }
            /* color */
            if (!got_color && css_value_color(t_start, t_len, &c)) {
                cs_border_color[cs] = c;
                got_color = 1;
                continue;
            }
        }
        if (sides[0]) cs_border[cs][0] = width;
        if (sides[1]) cs_border[cs][1] = width;
        if (sides[2]) cs_border[cs][2] = width;
        if (sides[3]) cs_border[cs][3] = width;
        return;
    }
    if (prop == CP_BORDER_COLOR) {
        if (css_value_color(val_off, val_len, &c)) cs_border_color[cs] = c;
        return;
    }
    if (prop == CP_BORDER_WIDTH) {
        int w = css_value_len(cs, val_off, val_len);
        cs_border[cs][0] = w; cs_border[cs][1] = w;
        cs_border[cs][2] = w; cs_border[cs][3] = w;
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
        if (cs_border[cs][0] == 0 && cs_border[cs][1] == 0 &&
            cs_border[cs][2] == 0 && cs_border[cs][3] == 0 &&
            cs_border_style[cs] != BS_NONE) {
            cs_border[cs][0] = 1; cs_border[cs][1] = 1;
            cs_border[cs][2] = 1; cs_border[cs][3] = 1;
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
         * needing to re-parse the family list. */
        int i = val_off;
        int end = val_off + val_len;
        int saw_size = 0;
        int family_off = -1;
        int family_len = 0;
        while (i < end) {
            while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
            if (i >= end) break;
            int t_start = i;
            while (i < end && css_value_pool[i] != ' ' && css_value_pool[i] != '\t') i = i + 1;
            int t_len = i - t_start;
            if (!saw_size && css_value_keyword(t_start, t_len, "italic")) {
                cs_font_i[cs] = 1; continue;
            }
            if (!saw_size && css_value_keyword(t_start, t_len, "bold")) {
                cs_font_w[cs] = 700; continue;
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
                if (cs >= 1 && cs_font_size_px[1] > 0) root_px = cs_font_size_px[1];
                int px = parse_length_px(t_start, t_len, parent_px, root_px);
                if (px > 0) cs_font_size_px[cs] = px;
                saw_size = 1;
                continue;
            }
            /* After the size, every remaining token belongs to the
             * font-family list.  Capture the verbatim slice from the
             * first family token to the end of the value. */
            if (saw_size) {
                if (family_off < 0) family_off = t_start;
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
                    if (b != ' ' && b != '\t') break;
                    family_len = family_len - 1;
                }
                /* Don't `continue;` here. Let the outer loop end naturally
                 * since we've already consumed all remaining bytes via
                 * the family slice. */
            }
        }
        if (family_off >= 0) {
            cs_font_family_off[cs] = family_off;
            cs_font_family_len[cs] = family_len;
        }
        return;
    }
    if (prop == CP_WIDTH)  { cs_width[cs]  = css_value_len(cs, val_off, val_len); return; }
    if (prop == CP_HEIGHT) { cs_height[cs] = css_value_len(cs, val_off, val_len); return; }
    if (prop == CP_MAX_WIDTH) {
        if (css_value_keyword(val_off, val_len, "none")) { cs_max_width[cs] = -1; return; }
        cs_max_width[cs]  = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_MIN_WIDTH) {
        cs_min_width[cs]  = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_MAX_HEIGHT) {
        if (css_value_keyword(val_off, val_len, "none")) { cs_max_height[cs] = -1; return; }
        cs_max_height[cs] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_MIN_HEIGHT) {
        cs_min_height[cs] = css_value_len(cs, val_off, val_len);
        return;
    }
    if (prop == CP_POSITION) {
        if (css_value_keyword(val_off, val_len, "relative")) { cs_position[cs] = POS_RELATIVE; return; }
        if (css_value_keyword(val_off, val_len, "absolute")) { cs_position[cs] = POS_ABSOLUTE; return; }
        if (css_value_keyword(val_off, val_len, "fixed"))    { cs_position[cs] = POS_FIXED; return; }
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
        if (k < val_off + val_len && css_value_pool[k] == '-') { neg = 1; k = k + 1; }
        while (k < val_off + val_len &&
               css_value_pool[k] >= '0' && css_value_pool[k] <= '9') {
            v = v * 10 + (css_value_pool[k] - '0');
            k = k + 1;
        }
        cs_z_index[cs] = neg ? -v : v;
        return;
    }
    if (prop == CP_FLOAT) {
        if (css_value_keyword(val_off, val_len, "left"))  { cs_float[cs] = FLOAT_LEFT;  return; }
        if (css_value_keyword(val_off, val_len, "right")) { cs_float[cs] = FLOAT_RIGHT; return; }
        cs_float[cs] = FLOAT_NONE;
        return;
    }
    if (prop == CP_CLEAR) {
        if (css_value_keyword(val_off, val_len, "left"))  { cs_clear[cs] = CLEAR_LEFT;  return; }
        if (css_value_keyword(val_off, val_len, "right")) { cs_clear[cs] = CLEAR_RIGHT; return; }
        if (css_value_keyword(val_off, val_len, "both"))  { cs_clear[cs] = CLEAR_BOTH;  return; }
        cs_clear[cs] = CLEAR_NONE;
        return;
    }
    if (prop == CP_WHITE_SPACE) {
        if (css_value_keyword(val_off, val_len, "pre"))    { cs_white_space[cs] = WS_PRE; return; }
        if (css_value_keyword(val_off, val_len, "nowrap")) { cs_white_space[cs] = WS_NOWRAP; return; }
        cs_white_space[cs] = WS_NORMAL;
        return;
    }
    if (prop == CP_LIST_STYLE_TYPE) {
        if (css_value_keyword(val_off, val_len, "decimal")) { cs_list_style[cs] = LS_DECIMAL; return; }
        if (css_value_keyword(val_off, val_len, "circle"))  { cs_list_style[cs] = LS_CIRCLE; return; }
        if (css_value_keyword(val_off, val_len, "square"))  { cs_list_style[cs] = LS_SQUARE; return; }
        if (css_value_keyword(val_off, val_len, "none"))    { cs_list_style[cs] = LS_NONE; return; }
        cs_list_style[cs] = LS_DISC;
        return;
    }
    if (prop == CP_VERTICAL_ALIGN) {
        if (css_value_keyword(val_off, val_len, "top"))    { cs_vertical_align[cs] = VA_TOP; return; }
        if (css_value_keyword(val_off, val_len, "middle")) { cs_vertical_align[cs] = VA_MIDDLE; return; }
        if (css_value_keyword(val_off, val_len, "bottom")) { cs_vertical_align[cs] = VA_BOTTOM; return; }
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
         * core/css/parser/CSSPropertyBorderRadiusUtils.cpp. */
        int r = css_value_len(cs, val_off, val_len);
        if (r < 0) r = 0;
        if (r > 64) r = 64;     /* clamp; gfx2d_rect_round_fill caps too */
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
         * (parseShadow). */
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
            while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
            if (i >= end) break;
            int t_start = i;
            while (i < end && css_value_pool[i] != ' ' && css_value_pool[i] != '\t') i = i + 1;
            int t_len = i - t_start;
            if (!got_dx && (css_value_pool[t_start] == '-' || css_value_pool[t_start] == '+' ||
                            (css_value_pool[t_start] >= '0' && css_value_pool[t_start] <= '9'))) {
                dx = css_value_len(cs, t_start, t_len);
                got_dx = 1;
                continue;
            }
            if (got_dx && !got_dy && (css_value_pool[t_start] == '-' || css_value_pool[t_start] == '+' ||
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
            cs_shadow_has[cs]   = 1;
            cs_shadow_dx[cs]    = dx;
            cs_shadow_dy[cs]    = dy;
            cs_shadow_color[cs] = col;
        }
        return;
    }
}

/* Step 5.5: inline style="..." parser.
 * Defined before sel_X and style_resolve_all so the latter can call it
 * without forward declarations. */

/* First pass over a `style="..."` attribute: only extracts the
 * custom-property (`--name: value`) declarations into cs_var_*[]. Run
 * BEFORE the regular property cascade so var() calls during regular-
 * rule application see the inline-declared vars. Without this, inline
 * `style="--c: blue"` was parsed in step 3 of style_resolve_all but
 * `color: var(--c, red)` had already been resolved in step 2 against
 * an empty cs_var_*, falling back to `red`. */
void apply_inline_vars(int cs, char *s) {
    int n = b_strlen(s);
    int i = 0;
    while (i < n) {
        while (i < n && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
        if (i >= n) break;
        int p_start = i;
        while (i < n && s[i] != ':' && s[i] != ';') i = i + 1;
        int p_end = i;
        while (p_end > p_start && (s[p_end-1] == ' ' || s[p_end-1] == '\t')) p_end = p_end - 1;
        if (i >= n || s[i] != ':') break;
        i = i + 1;
        while (i < n && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
        int v_start = i;
        while (i < n && s[i] != ';') i = i + 1;
        int v_end = i;
        while (v_end > v_start && (s[v_end-1] == ' ' || s[v_end-1] == '\t')) v_end = v_end - 1;
        if (i < n && s[i] == ';') i = i + 1;
        int p_l = p_end - p_start;
        if (p_l >= 2 && s[p_start] == '-' && s[p_start + 1] == '-') {
            if (cs_var_count[cs] < 8) {
                int name_off = css_intern_value(s + p_start, p_l);
                int val_off  = css_intern_value(s + v_start, v_end - v_start);
                if (name_off >= 0 && val_off >= 0) {
                    int k = cs_var_count[cs];
                    cs_var_name_off[cs][k] = name_off;
                    cs_var_name_len[cs][k] = p_l;
                    cs_var_val_off [cs][k] = val_off;
                    cs_var_val_len [cs][k] = v_end - v_start;
                    cs_var_count   [cs]    = k + 1;
                }
            }
        }
    }
}

void apply_inline_style(int cs, char *s) {
    int n = b_strlen(s);
    int i = 0;
    while (i < n) {
        while (i < n && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
        if (i >= n) break;
        int p_start = i;
        while (i < n && s[i] != ':' && s[i] != ';') i = i + 1;
        int p_end = i;
        while (p_end > p_start && (s[p_end-1] == ' ' || s[p_end-1] == '\t')) p_end = p_end - 1;
        if (i >= n || s[i] != ':') break;
        i = i + 1;
        while (i < n && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
        int v_start = i;
        while (i < n && s[i] != ';') i = i + 1;
        int v_end = i;
        while (v_end > v_start && (s[v_end-1] == ' ' || s[v_end-1] == '\t')) v_end = v_end - 1;
        if (i < n && s[i] == ';') i = i + 1;
        int p_l = p_end - p_start;
        /* Inline custom-property declaration: `style="--foo: bar"`. */
        if (p_l >= 2 && s[p_start] == '-' && s[p_start + 1] == '-') {
            if (cs_var_count[cs] < 8) {
                int name_off = css_intern_value(s + p_start, p_l);
                int val_off  = css_intern_value(s + v_start, v_end - v_start);
                if (name_off >= 0 && val_off >= 0) {
                    int k = cs_var_count[cs];
                    cs_var_name_off[cs][k] = name_off;
                    cs_var_name_len[cs][k] = p_l;
                    cs_var_val_off [cs][k] = val_off;
                    cs_var_val_len [cs][k] = v_end - v_start;
                    cs_var_count   [cs]    = k + 1;
                }
            }
            continue;
        }
        int prop = css_match_property(s + p_start, p_l);
        if (prop) {
            int voff = css_intern_value(s + v_start, v_end - v_start);
            if (voff >= 0) cs_apply_property(cs, prop, voff, v_end - v_start);
        }
    }
}

/* Step 5.3: selector matching */

/* Match an attribute against (op, name_off, val_off). Returns 1 on hit.
 * Shared between css_sel_* and css_not_* compound matchers. */
int attr_op_matches(int node, int a_off, int a_val_off, int a_op) {
    if (a_op == ATTR_OP_NONE) return 1;
    if (a_off < 0) return 0;
    char *aname = attr_pool + a_off;
    int node_attr = dom_attr_get(node, aname);
    if (node_attr < 0) return 0;
    if (a_op == ATTR_OP_PRESENCE) return 1;
    if (a_val_off < 0) return 0;
    char *want = attr_pool + a_val_off;
    int want_len = b_strlen(want);
    char *have = attr_pool + node_attr;
    int have_len = b_strlen(have);
    int i;
    int ws;
    if (a_op == ATTR_OP_EXACT) {
        if (have_len != want_len) return 0;
        return b_strieq_n(have, want, want_len) ? 1 : 0;
    }
    if (a_op == ATTR_OP_WORD) {
        i = 0;
        while (i < have_len) {
            while (i < have_len && (have[i] == ' ' || have[i] == '\t')) i = i + 1;
            ws = i;
            while (i < have_len && have[i] != ' ' && have[i] != '\t') i = i + 1;
            if (i - ws == want_len && b_strieq_n(have + ws, want, want_len)) return 1;
        }
        return 0;
    }
    if (a_op == ATTR_OP_PREFIX) {
        if (want_len == 0 || want_len > have_len) return 0;
        return b_strieq_n(have, want, want_len) ? 1 : 0;
    }
    if (a_op == ATTR_OP_SUFFIX) {
        if (want_len == 0 || want_len > have_len) return 0;
        return b_strieq_n(have + have_len - want_len, want, want_len) ? 1 : 0;
    }
    if (a_op == ATTR_OP_CONTAINS) {
        if (want_len == 0 || want_len > have_len) return 0;
        i = 0;
        while (i + want_len <= have_len) {
            if (b_strieq_n(have + i, want, want_len)) return 1;
            i = i + 1;
        }
        return 0;
    }
    return 0;
}

/* Class-token membership against a stored class= attribute. */
int class_token_match(int node, int c_off) {
    int node_class = dom_class_off[node];
    if (node_class < 0) return 0;
    char *tgt = attr_pool + c_off;
    int tgt_len = b_strlen(tgt);
    if (tgt_len == 0) return 0;
    char *cls = attr_pool + node_class;
    int cls_len = b_strlen(cls);
    int i = 0;
    while (i < cls_len) {
        while (i < cls_len && (cls[i] == ' ' || cls[i] == '\t')) i = i + 1;
        int s = i;
        while (i < cls_len && cls[i] != ' ' && cls[i] != '\t') i = i + 1;
        if (i - s == tgt_len && b_strieq_n(cls + s, tgt, tgt_len)) return 1;
    }
    return 0;
}

/* Match a simple pseudo-class (ones that depend only on the node and
 * tree-position caches). Returns 1 on hit. PSEUDO_NTH_CHILD reads
 * `pseudo_arg` packed as (a<<16)|(b&0xFFFF) with signed semantics. */
int simple_pseudo_matches(int node, int pseudo, int pseudo_arg) {
    int p;
    int idx;
    int a_raw;
    int b_raw;
    int a;
    int b;
    int diff;
    int t;
    int c;
    int last_match;
    if (pseudo == PSEUDO_NONE) return 1;
    if (pseudo == PSEUDO_HOVER) {
        if (hover_dom_node < 0) return 0;
        p = hover_dom_node;
        while (p >= 0) { if (p == node) return 1; p = n_parent[p]; }
        return 0;
    }
    if (pseudo == PSEUDO_FOCUS) {
        if (focused_input < 0 || focused_input >= inputs_count) return 0;
        return (input_node[focused_input] == node) ? 1 : 0;
    }
    if (pseudo == PSEUDO_LINK) {
        if (n_tag[node] != T_A) return 0;
        return (dom_attr_get(node, "href") >= 0) ? 1 : 0;
    }
    if (pseudo == PSEUDO_VISITED) return 0;
    if (pseudo == PSEUDO_FIRST_CHILD) {
        return (n_sibling_idx[node] == 1) ? 1 : 0;
    }
    if (pseudo == PSEUDO_LAST_CHILD) {
        if (n_sibling_idx[node] == 0) return 0;
        return (n_sibling_idx[node] == n_sibling_count[node]) ? 1 : 0;
    }
    if (pseudo == PSEUDO_NTH_CHILD) {
        idx = n_sibling_idx[node];
        if (idx <= 0) return 0;
        /* Sign-extend the 16-bit packed halves. */
        a_raw = (pseudo_arg >> 16) & 0xFFFF;
        b_raw = pseudo_arg & 0xFFFF;
        a = (a_raw & 0x8000) ? (a_raw | -0x10000) : a_raw;
        b = (b_raw & 0x8000) ? (b_raw | -0x10000) : b_raw;
        if (a == 0) return (idx == b) ? 1 : 0;
        diff = idx - b;
        if (a > 0) {
            if (diff < 0) return 0;
            return (diff % a == 0) ? 1 : 0;
        }
        /* a < 0 */
        if (diff > 0) return 0;
        return ((-diff) % (-a) == 0) ? 1 : 0;
    }
    if (pseudo == PSEUDO_ROOT) {
        return (n_tag[node] == T_HTML || n_parent[node] < 0) ? 1 : 0;
    }
    if (pseudo == PSEUDO_FIRST_OF_TYPE) {
        t = n_tag[node];
        p = n_parent[node];
        if (p < 0) return 1;
        c = n_first_child[p];
        while (c >= 0) {
            if (n_tag[c] == t) return (c == node) ? 1 : 0;
            c = n_next[c];
        }
        return 0;
    }
    if (pseudo == PSEUDO_LAST_OF_TYPE) {
        t = n_tag[node];
        p = n_parent[node];
        if (p < 0) return 1;
        last_match = -1;
        c = n_first_child[p];
        while (c >= 0) {
            if (n_tag[c] == t) last_match = c;
            c = n_next[c];
        }
        return (last_match == node) ? 1 : 0;
    }
    if (pseudo == PSEUDO_EMPTY) {
        return (n_first_child[node] < 0) ? 1 : 0;
    }
    return 0;
}

/* Match a single :not(<simple>) inner compound at `not_idx`. */
int not_simple_matches(int not_idx, int node) {
    if (not_idx < 0) return 0;
    int t = css_not_tag[not_idx];
    if (t != 0 && n_tag[node] != t) return 0;
    int c_off = css_not_class_off[not_idx];
    if (c_off >= 0 && !class_token_match(node, c_off)) return 0;
    int id_off = css_not_id_off[not_idx];
    if (id_off >= 0) {
        int node_id = dom_id_off[node];
        if (node_id < 0) return 0;
        if (!b_strieq(attr_pool + node_id, attr_pool + id_off)) return 0;
    }
    if (!attr_op_matches(node,
                         css_not_attr_off[not_idx],
                         css_not_attr_val_off[not_idx],
                         css_not_attr_op[not_idx])) return 0;
    int p = css_not_pseudo[not_idx];
    if (p != 0 && !simple_pseudo_matches(node, p, css_not_pseudo_arg[not_idx])) return 0;
    return 1;
}

int sel_compound_matches(int sel_idx, int node) {
    int t = css_sel_tag[sel_idx];
    if (t != 0 && n_tag[node] != t) return 0;
    int c_off = css_sel_class_off[sel_idx];
    if (c_off >= 0 && !class_token_match(node, c_off)) return 0;
    int id_off = css_sel_id_off[sel_idx];
    int node_id;
    int pseudo;
    int ni;
    int a_op;
    if (id_off >= 0) {
        node_id = dom_id_off[node];
        if (node_id < 0) return 0;
        if (!b_strieq(attr_pool + node_id, attr_pool + id_off)) return 0;
    }
    pseudo = css_sel_pseudo[sel_idx];
    if (pseudo == PSEUDO_NOT) {
        /* :not(X): succeed iff inner does NOT match */
        ni = css_sel_not_idx[sel_idx];
        if (not_simple_matches(ni, node)) return 0;
    }
    if (pseudo != PSEUDO_NONE && pseudo != PSEUDO_NOT) {
        if (!simple_pseudo_matches(node, pseudo, css_sel_pseudo_arg[sel_idx])) return 0;
    }
    a_op = css_sel_attr_op[sel_idx];
    if (a_op != ATTR_OP_NONE) {
        if (!attr_op_matches(node,
                             css_sel_attr_off[sel_idx],
                             css_sel_attr_val_off[sel_idx],
                             a_op)) return 0;
    }
    return 1;
}

/* Inner combinator-walk. Caller has already verified the tail compound;
 * this walks left-to-right combinators from `last-1` down to `sel_first`.
 * CupidC keeps locals in a flat table, so all branch-scoped ints are
 * hoisted to function scope. */
int sel_chain_walk(int sel_first, int last, int node) {
    int cur = node;
    int s = last - 1;
    int comb;
    int p;
    int prev;
    while (s >= sel_first) {
        comb = css_sel_combinator[s + 1];
        if (comb == COMB_SUBSELECTOR) {
            if (!sel_compound_matches(s, cur)) return 0;
            s = s - 1;
            continue;
        }
        if (comb == COMB_CHILD) {
            p = n_parent[cur];
            if (p < 0) return 0;
            if (!sel_compound_matches(s, p)) return 0;
            cur = p;
            s = s - 1;
            continue;
        }
        if (comb == COMB_DESCENDANT) {
            p = n_parent[cur];
            while (p >= 0 && !sel_compound_matches(s, p)) p = n_parent[p];
            if (p < 0) return 0;
            cur = p;
            s = s - 1;
            continue;
        }
        if (comb == COMB_ADJACENT) {
            prev = n_prev_sibling_elt[cur];
            if (prev < 0) return 0;
            if (!sel_compound_matches(s, prev)) return 0;
            cur = prev;
            s = s - 1;
            continue;
        }
        if (comb == COMB_GEN_SIBLING) {
            prev = n_prev_sibling_elt[cur];
            while (prev >= 0 && !sel_compound_matches(s, prev)) prev = n_prev_sibling_elt[prev];
            if (prev < 0) return 0;
            cur = prev;
            s = s - 1;
            continue;
        }
        return 0;
    }
    return 1;
}

/* Match a selector chain against a node. Right-to-left walk modeled on
 * Blink's SelectorChecker::match. Rules with a tail pseudo-element are
 * treated as non-matching against the originating element so they don't
 * pollute its own ComputedStyle (those rules feed the pseudo cascade
 * via sel_chain_matches_pseudo). */
int sel_chain_matches(int sel_first, int sel_count, int node) {
    if (sel_count == 0) return 0;
    int last = sel_first + sel_count - 1;
    if (css_sel_pseudo_elt[last] != PSELT_NONE) return 0;
    if (!sel_compound_matches(last, node)) return 0;
    return sel_chain_walk(sel_first, last, node);
}

/* Pseudo-element variant: the chain MUST end with the requested
 * pseudo-element flag. The originating element is `node`; combinators are
 * still resolved against the real DOM. Used by ::before/::after content
 * cascade and any future pseudo-element styling. */
int sel_chain_matches_pseudo(int sel_first, int sel_count, int node, int wanted_pe) {
    if (sel_count == 0) return 0;
    int last = sel_first + sel_count - 1;
    if (css_sel_pseudo_elt[last] != wanted_pe) return 0;
    if (!sel_compound_matches(last, node)) return 0;
    return sel_chain_walk(sel_first, last, node);
}

/* Decode a CSS string value into out[]. Handles `\HHHHHH` codepoint
 * escapes (1..6 hex digits, optional terminating space) and UTF-8
 * multi-byte sequences in raw text. Codepoints above 127 are folded
 * through map_high_codepoint() to ASCII fallbacks (the bitmap font
 * doesn't carry real Unicode). Returns the decoded byte length, or 0 if
 * the value isn't a quoted string. */
int css_value_string(int off, int len, char *out, int omax) {
    int i = off;
    int end = off + len;
    int o = 0;
    int v;
    int d;
    int hd;
    int hi;
    int b0;
    int cp;
    int adv;
    char c;
    char quote;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
    if (i >= end) return 0;
    quote = css_value_pool[i];
    if (quote != '"' && quote != '\'') return 0;
    i = i + 1;
    while (i < end && o < omax - 1) {
        c = css_value_pool[i];
        if (c == quote) { i = i + 1; break; }
        if (c == '\\' && i + 1 < end) {
            i = i + 1;
            v = 0;
            d = 0;
            while (i < end && d < 6) {
                hd = hex_digit(css_value_pool[i]);
                if (hd < 0) break;
                v = v * 16 + hd;
                d = d + 1;
                i = i + 1;
            }
            if (d == 0) {
                /* `\x` literal escape (drop the backslash, keep the char) */
                out[o] = css_value_pool[i]; o = o + 1;
                i = i + 1;
                continue;
            }
            if (i < end && css_value_pool[i] == ' ') i = i + 1;
            if (v >= 32 && v < 127) {
                out[o] = (char)v; o = o + 1; continue;
            }
            if (v == 0xA0) { out[o] = ' '; o = o + 1; continue; }
            /* Emit raw UTF-8 so TTF cmap renders the real glyph
             * (e.g. \201C -> U+201C left double quote). fontsys.c
             * UTF-8-decodes runs in run_width / draw_run_styled. */
            int wn = emit_utf8_codepoint(v, out + o, omax - 1 - o);
            if (wn > 0) { o = o + wn; continue; }
            out[o] = '?'; o = o + 1; continue;
        }
        b0 = c & 0xFF;
        if (b0 >= 0x80) {
            cp = -1;
            adv = 1;
            if ((b0 & 0xE0) == 0xC0 && i + 1 < end) {
                cp = ((b0 & 0x1F) << 6) | (css_value_pool[i+1] & 0x3F);
                adv = 2;
            }
            if (cp < 0 && (b0 & 0xF0) == 0xE0 && i + 2 < end) {
                cp = ((b0 & 0x0F) << 12) |
                     ((css_value_pool[i+1] & 0x3F) << 6) |
                     (css_value_pool[i+2] & 0x3F);
                adv = 3;
            }
            if (cp >= 0) {
                if (cp == 0xA0) { out[o] = ' '; o = o + 1; }
                else if (map_high_codepoint(cp, &hi)) { out[o] = (char)hi; o = o + 1; }
                else { out[o] = '?'; o = o + 1; }
                i = i + adv;
                continue;
            }
        }
        out[o] = c; o = o + 1; i = i + 1;
    }
    out[o] = 0;
    return o;
}

/* Subroutine of style_resolve_all: resolve ::before / ::after generated
 * content for `node`. Matches every rule with a pseudo-element tail
 * compound, picks the highest-specificity (then doc-order) winner per
 * pseudo-element side, decodes the `content:` string, and saves the
 * result to n_pseudo_before/after_off. Other pseudo-element properties
 * (color, font, etc.) are not yet honored. Only `content`. */
void resolve_pseudo_content(int node) {
    int win_b = -1;
    int score_b = -1;
    int win_a = -1;
    int score_a = -1;
    int sf;
    int sc;
    int tail;
    int pe;
    int score;
    int len;
    int ao;
    char buf[256];
    for (int r = 0; r < css_rule_count; r = r + 1) {
        if (css_rule_prop_id[r] != CP_CONTENT) continue;
        sf = css_rule_sel_first[r];
        sc = css_rule_sel_count[r];
        tail = sf + sc - 1;
        pe = css_sel_pseudo_elt[tail];
        if (pe == PSELT_NONE) continue;
        if (!sel_chain_matches_pseudo(sf, sc, node, pe)) continue;
        score = (css_rule_specificity[r] << 12) | (css_rule_doc_order[r] & 0xFFF);
        if (pe == PSELT_BEFORE) {
            if (score > score_b) { score_b = score; win_b = r; }
            continue;
        }
        if (pe == PSELT_AFTER) {
            if (score > score_a) { score_a = score; win_a = r; }
        }
    }
    if (win_b >= 0) {
        len = css_value_string(css_rule_value_off[win_b], css_rule_value_len[win_b], buf, 256);
        if (len > 0) {
            ao = attr_intern(buf, len);
            if (ao >= 0) {
                n_pseudo_before_off[node] = ao;
                n_pseudo_before_len[node] = len;
            }
        }
    }
    if (win_a >= 0) {
        len = css_value_string(css_rule_value_off[win_a], css_rule_value_len[win_a], buf, 256);
        if (len > 0) {
            ao = attr_intern(buf, len);
            if (ao >= 0) {
                n_pseudo_after_off[node] = ao;
                n_pseudo_after_len[node] = len;
            }
        }
    }
}

/* Step 5.4: style_resolve_all */

void style_resolve_all() {
    cs_count = 0;

    /* Allocate one ComputedStyle per DOM node, in DOM order so parent < child.
     * Index alignment: cs[i] corresponds to node i. */
    for (int n = 0; n < nodes_count; n = n + 1) {
        int cs = cs_count;
        cs_count = cs_count + 1;
        if (cs >= MAX_COMPUTED_STYLES) { cs_count = MAX_COMPUTED_STYLES; break; }

        /* 1. UA defaults */
        ua_default_style(n_tag[n], cs);

        /* 2a. Custom-property cascade FIRST so var() resolution during
         * regular property cascade sees inherited + locally-declared
         * vars. CP_CUSTOM_VAR rules can declare many distinct --names
         * per node, so the winner_rule[] slot dedicated to CP_CUSTOM_VAR
         * is insufficient — walk the rule pool keyed on
         * (name_off, name_len), and apply the highest-scoring winner
         * per name. Reference: Blink resolves custom properties in the
         * cascade with the same priority order as regular properties;
         * we approximate via the shared specificity+doc-order score. */
        for (int r = 0; r < css_rule_count; r = r + 1) {
            if (css_rule_prop_id[r] != CP_CUSTOM_VAR) continue;
            if (css_rule_important[r]) continue;
            int sf2 = css_rule_sel_first[r];
            int sc2 = css_rule_sel_count[r];
            if (!sel_chain_matches(sf2, sc2, n)) continue;
            int n_off = css_rule_var_name_off[r];
            int n_len = css_rule_var_name_len[r];
            int score2 = (css_rule_specificity[r] << 12) |
                         (css_rule_doc_order[r] & 0xFFF);
            /* Find existing slot by name; else allocate a new one. */
            int slot = -1;
            for (int k = 0; k < cs_var_count[cs]; k = k + 1) {
                if (cs_var_name_len[cs][k] != n_len) continue;
                int eq = 1;
                for (int b = 0; b < n_len; b = b + 1) {
                    if (css_value_pool[cs_var_name_off[cs][k] + b] !=
                        css_value_pool[n_off + b]) { eq = 0; break; }
                }
                if (eq) { slot = k; break; }
            }
            if (slot < 0) {
                if (cs_var_count[cs] >= 8) continue;
                slot = cs_var_count[cs];
                cs_var_count[cs] = slot + 1;
                cs_var_name_off[cs][slot] = n_off;
                cs_var_name_len[cs][slot] = n_len;
                cs_var_val_off [cs][slot] = css_rule_value_off[r];
                cs_var_val_len [cs][slot] = css_rule_value_len[r];
                /* Encode score in val_len's high bits? No - use separate.
                 * Re-walk to find current score from val pool would be
                 * nicer; simpler: keep score implicit by always tracking
                 * the higher score on each visit. */
                cs_var_name_off[cs][slot] = n_off;
                cs_var_name_len[cs][slot] = n_len;
                /* score implicit via slot order; we'll re-check on conflict. */
                (void)score2;
            } else {
                /* Replace if score higher. We cheat and replace
                 * unconditionally on later doc-order: rules walk in
                 * source order so later wins on ties, which matches the
                 * doc-order tiebreaker. Specificity ties handled
                 * implicitly via rule walk order; for stricter spec
                 * compliance store an explicit score per slot. */
                cs_var_val_off[cs][slot] = css_rule_value_off[r];
                cs_var_val_len[cs][slot] = css_rule_value_len[r];
            }
        }

        /* 2b. Inline custom-property pre-pass — extract any `--name:`
         * declarations from `style="..."` into cs_var_*[] BEFORE the
         * regular property cascade runs, so var() lookups in regular
         * rules see inline-declared vars. Inline custom props win over
         * matching author-rule custom props on this element because
         * apply_inline_vars appends and our cs_var_lookup returns the
         * first match — appending later overrides. */
        int sty_off = dom_attr_get(n, "style");
        if (sty_off >= 0) {
            apply_inline_vars(cs, attr_pool + sty_off);
        }

        /* 2c. Author rules in two passes: pass 1 (non-important) feeds
         * the normal specificity+doc-order cascade; pass 2 (important)
         * wins over everything from pass 1 and inline style. The score
         * packs (specificity << 12) | doc_order so a higher specificity
         * or later rule wins at equal level. Size matches MAX_CP_ID;
         * CupidC requires a literal here. */
        int winner_rule[80];
        int winner_score[80];
        for (int p = 0; p < MAX_CP_ID; p = p + 1) { winner_rule[p] = -1; winner_score[p] = -1; }
        for (int r = 0; r < css_rule_count; r = r + 1) {
            if (css_rule_important[r]) continue;
            int p = css_rule_prop_id[r];
            if (p < 1 || p >= MAX_CP_ID) continue;
            int sf = css_rule_sel_first[r];
            int sc = css_rule_sel_count[r];
            if (!sel_chain_matches(sf, sc, n)) continue;
            int score = (css_rule_specificity[r] << 12) | (css_rule_doc_order[r] & 0xFFF);
            if (score > winner_score[p]) {
                winner_score[p] = score;
                winner_rule[p] = r;
            }
        }
        for (int p = 1; p < MAX_CP_ID; p = p + 1) {
            int r = winner_rule[p];
            if (r >= 0) {
                cs_apply_property(cs, p,
                                  css_rule_value_off[r], css_rule_value_len[r]);
            }
        }

        /* 3. Inline style="..." attribute (wins over non-important
         * author rules). The pre-pass at 2b already captured --vars;
         * apply_inline_style still re-applies them but that's a no-op
         * since the values are identical. Regular properties get
         * applied here for the first time. */
        if (sty_off >= 0) {
            apply_inline_style(cs, attr_pool + sty_off);
        }

        /* 4. Important author rules: applied last so they override pass 1
         *    and inline style. Specificity + doc-order still resolves ties
         *    among important rules themselves. */
        int imp_rule[80];
        int imp_score[80];
        for (int p = 0; p < MAX_CP_ID; p = p + 1) { imp_rule[p] = -1; imp_score[p] = -1; }
        for (int r = 0; r < css_rule_count; r = r + 1) {
            if (!css_rule_important[r]) continue;
            int p = css_rule_prop_id[r];
            if (p < 1 || p >= MAX_CP_ID) continue;
            int sf = css_rule_sel_first[r];
            int sc = css_rule_sel_count[r];
            if (!sel_chain_matches(sf, sc, n)) continue;
            int score = (css_rule_specificity[r] << 12) | (css_rule_doc_order[r] & 0xFFF);
            if (score > imp_score[p]) {
                imp_score[p] = score;
                imp_rule[p] = r;
            }
        }
        for (int p = 1; p < MAX_CP_ID; p = p + 1) {
            int r = imp_rule[p];
            if (r >= 0) {
                cs_apply_property(cs, p,
                                  css_rule_value_off[r], css_rule_value_len[r]);
            }
        }

        /* 4. Inheritance from parent ComputedStyle for unset inheritable props.
         *    cs index == node index, so parent's cs is at parent's node index. */
        int parent = n_parent[n];
        if (parent >= 0 && parent < cs_count) {
            int pcs = parent;
            if (cs_color[cs] < 0) cs_color[cs] = cs_color[pcs];
            if (cs_text_align[cs] == TA_LEFT && cs_text_align[pcs] != TA_LEFT)
                cs_text_align[cs] = cs_text_align[pcs];
            if (cs_white_space[cs] == WS_NORMAL) cs_white_space[cs] = cs_white_space[pcs];
            if (cs_list_style[cs] == LS_DISC && cs_list_style[pcs] != LS_DISC)
                cs_list_style[cs] = cs_list_style[pcs];
            if (cs_line_height[cs] < 0) {
                cs_line_height[cs] = cs_line_height[pcs];
                cs_line_height_mult[cs] = cs_line_height_mult[pcs];
            }
            if (cs_font_size_px[cs] < 0) cs_font_size_px[cs] = cs_font_size_px[pcs];
            /* Font-family inherits as a unit. cs_font_family_off == -1
             * means "unset on this element" - copy the parent's stash. */
            if (cs_font_family_off[cs] < 0) {
                cs_font_family_off[cs] = cs_font_family_off[pcs];
                cs_font_family_len[cs] = cs_font_family_len[pcs];
            }
            if (cs_font_generic[cs] == FONTSYS_FAMILY_DEFAULT
                && cs_font_generic[pcs] != FONTSYS_FAMILY_DEFAULT) {
                cs_font_generic[cs] = cs_font_generic[pcs];
            }
            /* font-weight / font-style are inherited per CSS spec. Element
             * UA defaults set 400 / 0 explicitly, so we promote to parent's
             * value only when the child still carries the default; child
             * elements that explicitly set normal still override correctly
             * because cascade ran before this inheritance pass. */
            if (cs_font_w[cs] == 400 && cs_font_w[pcs] != 400) cs_font_w[cs] = cs_font_w[pcs];
            if (cs_font_i[cs] == 0   && cs_font_i[pcs] != 0)   cs_font_i[cs] = cs_font_i[pcs];
            /* text-decoration in CSS2.1 doesn't strictly inherit, but its
             * paint effect (underline/strike) propagates from ancestor to
             * all descendants. Treating it as inherit-when-unset is
             * visually equivalent and avoids walking the rt parent chain
             * in emit_text_atoms. */
            if (cs_text_dec[cs] == 0 && cs_text_dec[pcs] != 0) cs_text_dec[cs] = cs_text_dec[pcs];
        } else {
            /* root: ensure color is concrete and font-size has a baseline */
            if (cs_color[cs] < 0) cs_color[cs] = 0x000000;
            if (cs_font_size_px[cs] < 0) cs_font_size_px[cs] = 16;
        }
        /* Derive the kernel-tier from the px-resolved size. layout/paint
         * still consume cs_font_size_tier; cs_font_size_px is the source
         * of truth and what em/rem/% length resolution reads. */
        cs_font_size_tier[cs] = px_to_tier(cs_font_size_px[cs]);

        /* Pseudo-element generated content (::before / ::after). Stored on
         * the originating node; render_tree.cc injects synthetic RT_TEXT
         * children at build time. */
        resolve_pseudo_content(n);
    }
}

/* Optional debug helper. Kept dormant, not called from parse_html. */
void dump_style(int n) {
    serial_printf("[browser] node %d (tag %d): disp=%d color=0x%x bg=0x%x weight=%d size=%d\n",
                  n, n_tag[n], cs_display[n], cs_color[n], cs_bg[n],
                  cs_font_w[n], cs_font_size_tier[n]);
}
