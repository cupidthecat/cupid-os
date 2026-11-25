/**
 * @file style.cc
 * @brief Browser style defaults and CSS value parsing.
 */

void ua_default_style(int tag, int cs) {
    /* Fill cs_*[cs] with sensible UA defaults for `tag`. Inherit-friendly defaults
     * (color, font_*, white_space, text_align, list_style) start at -1 / 0 so the
     * resolver can treat them as "unset, inherit from parent".*/
    cs_color[cs] = -1;
    cs_bg[cs] = -1;
    cs_font_w[cs] = 400;
    cs_font_i[cs] = 0;
    cs_font_size_tier[cs] = 1; /* default 8x8 */
    cs_text_align[cs] = TA_LEFT;
    cs_text_dec[cs] = 0;
    cs_display[cs] = DISP_INLINE;
    cs_margin[cs][0] = 0;
    cs_margin[cs][1] = 0;
    cs_margin[cs][2] = 0;
    cs_margin[cs][3] = 0;
    cs_margin_auto[cs][0] = 0;
    cs_margin_auto[cs][1] = 0;
    cs_margin_auto[cs][2] = 0;
    cs_margin_auto[cs][3] = 0;
    cs_padding[cs][0] = 0;
    cs_padding[cs][1] = 0;
    cs_padding[cs][2] = 0;
    cs_padding[cs][3] = 0;
    cs_border[cs][0] = 0;
    cs_border[cs][1] = 0;
    cs_border[cs][2] = 0;
    cs_border[cs][3] = 0;
    cs_border_color[cs] = 0x000000;
    cs_width[cs] = -1;
    cs_height[cs] = -1;
    cs_white_space[cs] = WS_NORMAL;
    cs_list_style[cs] = LS_DISC;
    cs_vertical_align[cs] = VA_BASELINE;
    cs_line_height[cs] = -1; /* unset; falls back to tier_line_h */
    cs_line_height_mult[cs] = 0;
    cs_font_size_px[cs] = -1;    /* unset; inherits from parent */
    cs_font_family_off[cs] = -1; /* unset; inherits from parent */
    cs_font_family_len[cs] = 0;
    cs_font_generic[cs] = FONTSYS_FAMILY_DEFAULT;
    cs_max_width[cs] = -1;
    cs_min_width[cs] = -1;
    cs_max_height[cs] = -1;
    cs_min_height[cs] = -1;
    cs_border_radius[cs] = 0;
    cs_overflow[cs] = OVERFLOW_VISIBLE;
    cs_border_style[cs] = BS_SOLID;
    cs_shadow_has[cs] = 0;
    cs_shadow_dx[cs] = 0;
    cs_shadow_dy[cs] = 0;
    cs_shadow_color[cs] = 0x000000;
    cs_position[cs] = POS_STATIC;
    cs_top[cs] = 0;
    cs_right[cs] = 0;
    cs_bottom[cs] = 0;
    cs_left[cs] = 0;
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
    cs_bg_img_off[cs] = -1;
    cs_bg_img_len[cs] = 0;
    cs_bg_handle[cs] = -1;
    cs_bg_intrinsic_w[cs] = 0;
    cs_bg_intrinsic_h[cs] = 0;
    cs_bg_size_w[cs] = BG_SIZE_AUTO;
    cs_bg_size_h[cs] = BG_SIZE_AUTO;
    cs_bg_pos_x[cs] = 0;
    cs_bg_pos_y[cs] = 0;
    cs_bg_repeat[cs] = BG_REPEAT_BOTH;

    /* Per-tag overrides matching spec §2 UA stylesheet. Flat if/return chain
     * (CupidC parser recurses into nested else; long else-if chains overflow
     * its stack).*/
    /* Heading + paragraph margins. Top is full spec (0.67-1em); bottom is
     * trimmed so headings sit closer to their following content. After the
     * parent-first-child collapse rule, the visible gap above an h2 that
     * leads a page is 0; the gap below the h2 down to the first paragraph
     * = max(h2.margin_b, p.margin_t) = max(8, 10) = 10.*/
    if (tag == T_H1) {
        cs_display[cs] = DISP_BLOCK;
        cs_font_w[cs] = 700;
        cs_font_size_px[cs] = 32;
        cs_margin[cs][0] = 21;
        cs_margin[cs][2] = 10;
        return;
    }
    if (tag == T_H2) {
        cs_display[cs] = DISP_BLOCK;
        cs_font_w[cs] = 700;
        cs_font_size_px[cs] = 24;
        cs_margin[cs][0] = 20;
        cs_margin[cs][2] = 8;
        return;
    }
    if (tag == T_H3) {
        cs_display[cs] = DISP_BLOCK;
        cs_font_w[cs] = 700;
        cs_font_size_px[cs] = 19;
        cs_margin[cs][0] = 19;
        cs_margin[cs][2] = 8;
        return;
    }
    if (tag == T_H4) {
        cs_display[cs] = DISP_BLOCK;
        cs_font_w[cs] = 700;
        cs_font_size_px[cs] = 16;
        cs_margin[cs][0] = 18;
        cs_margin[cs][2] = 8;
        return;
    }
    if (tag == T_H5) {
        cs_display[cs] = DISP_BLOCK;
        cs_font_w[cs] = 700;
        cs_font_size_px[cs] = 13;
        cs_margin[cs][0] = 16;
        cs_margin[cs][2] = 6;
        return;
    }
    if (tag == T_H6) {
        cs_display[cs] = DISP_BLOCK;
        cs_font_w[cs] = 700;
        cs_font_size_px[cs] = 11;
        cs_margin[cs][0] = 16;
        cs_margin[cs][2] = 6;
        return;
    }
    if (tag == T_P) {
        cs_display[cs] = DISP_BLOCK;
        cs_margin[cs][0] = 10;
        cs_margin[cs][2] = 10;
        return;
    }
    if (tag == T_A) {
        /* Per HTML/CSS UA spec, only `:link` (an <a> with href) gets the
         * default blue/underline. Bare anchors used as targets (`<a>foo</a>`
         * without href) inherit color and have no decoration. cs index ==
         * DOM node index for element entries, so dom_attr_get is valid.*/
        if (dom_attr_get(cs, "href") >= 0) {
            cs_color[cs] = 0x0000EE;
            cs_text_dec[cs] = TD_UNDERLINE;
        }
        return;
    }
    if (tag == T_PRE) {
        cs_display[cs] = DISP_BLOCK;
        cs_white_space[cs] = WS_PRE;
        cs_margin[cs][0] = 16;
        cs_margin[cs][2] = 16;
        cs_font_generic[cs] = FONTSYS_FAMILY_MONOSPACE;
        return;
    }
    if (tag == T_CODE) {
        cs_font_generic[cs] = FONTSYS_FAMILY_MONOSPACE;
        return;
    }
    if (tag == T_B || tag == T_STRONG) {
        cs_font_w[cs] = 700;
        return;
    }
    if (tag == T_I || tag == T_EM) {
        cs_font_i[cs] = 1;
        return;
    }
    if (tag == T_UL || tag == T_OL || tag == T_DL) {
        cs_display[cs] = DISP_BLOCK;
        cs_padding[cs][3] = 40; /* CSS2 default */
        cs_margin[cs][0] = 16;
        cs_margin[cs][2] = 16; /* 1em */
        if (tag == T_OL)
            cs_list_style[cs] = LS_DECIMAL;
        return;
    }
    if (tag == T_LI) {
        cs_display[cs] = DISP_LIST_ITEM;
        return;
    }
    if (tag == T_TABLE) {
        cs_display[cs] = DISP_TABLE;
        return;
    }
    if (tag == T_THEAD || tag == T_TBODY || tag == T_TFOOT) {
        cs_display[cs] = DISP_TABLE_ROW_GROUP;
        return;
    }
    if (tag == T_TR) {
        cs_display[cs] = DISP_TABLE_ROW;
        return;
    }
    if (tag == T_TD) {
        cs_display[cs] = DISP_TABLE_CELL;
        cs_padding[cs][0] = 2;
        cs_padding[cs][1] = 2;
        cs_padding[cs][2] = 2;
        cs_padding[cs][3] = 2;
        cs_border[cs][0] = 1;
        cs_border[cs][1] = 1;
        cs_border[cs][2] = 1;
        cs_border[cs][3] = 1;
        cs_border_color[cs] = 0xC0C0C0;
        return;
    }
    if (tag == T_TH) {
        cs_display[cs] = DISP_TABLE_CELL;
        cs_font_w[cs] = 700;
        cs_text_align[cs] = TA_CENTER;
        cs_padding[cs][0] = 2;
        cs_padding[cs][1] = 2;
        cs_padding[cs][2] = 2;
        cs_padding[cs][3] = 2;
        cs_border[cs][0] = 1;
        cs_border[cs][1] = 1;
        cs_border[cs][2] = 1;
        cs_border[cs][3] = 1;
        cs_border_color[cs] = 0xC0C0C0;
        return;
    }
    if (tag == T_HR) {
        cs_display[cs] = DISP_BLOCK;
        cs_height[cs] = 1;
        cs_bg[cs] = 0x808080;
        cs_margin[cs][0] = 8;
        cs_margin[cs][2] = 8;
        return;
    }
    if (tag == T_BLOCKQUOTE) {
        cs_display[cs] = DISP_BLOCK;
        cs_padding[cs][3] = 16;
        cs_margin[cs][0] = 8;
        cs_margin[cs][2] = 8;
        cs_border[cs][3] = 4;
        cs_border_color[cs] = 0xCCCCCC;
        return;
    }
    if (tag == T_DIV) {
        cs_display[cs] = DISP_BLOCK;
        return;
    }
    if (tag == T_HEADER || tag == T_FOOTER || tag == T_NAV || tag == T_SECTION ||
        tag == T_ARTICLE || tag == T_ASIDE || tag == T_MAIN) {
        cs_display[cs] = DISP_BLOCK;
        return;
    }
    if (tag == T_FORM) {
        cs_display[cs] = DISP_BLOCK;
        return;
    }
    if (tag == T_INPUT || tag == T_BUTTON) {
        cs_display[cs] = DISP_INLINE_BLOCK;
        return;
    }
    if (tag == T_IMG) {
        cs_display[cs] = DISP_INLINE_BLOCK;
        return;
    }
    if (tag == T_BODY) {
        cs_display[cs] = DISP_BLOCK;
        /* Real browsers collapse body's top margin with the first child's
         * top margin (spec margin-collapsing through a zero-padding parent).
         * We don't implement parent/first-child collapse yet, so set the
         * top to 0; the visible gap above the first heading is then the
         * heading's own UA margin-top (e.g. h1 21px, h2 20px), matching
         * the post-collapse result a real browser shows.*/
        cs_margin[cs][0] = 0;
        cs_margin[cs][1] = 8;
        cs_margin[cs][2] = 8;
        cs_margin[cs][3] = 8;
        return;
    }
    if (tag == T_HTML) {
        cs_display[cs] = DISP_BLOCK;
        cs_font_size_px[cs] = 16; /* root font-size baseline (rem unit) */
        return;
    }
    if (tag == T_ROOT) {
        cs_display[cs] = DISP_BLOCK;
        cs_font_size_px[cs] = 16;
        return;
    }
    if (tag == T_HEAD || tag == T_SCRIPT || tag == T_STYLE || tag == T_NOSCRIPT || tag == T_TITLE) {
        /* <title> never renders as visible body content: even when the
         * parser fails to wrap it in an implicit <head> (e.g. `<!doctype
         * html><title>x</title><body>...`), display:none keeps it out of
         * the layout tree. Title text remains accessible via title_buf.*/
        cs_display[cs] = DISP_NONE;
        return;
    }
    if (tag == T_DT) {
        cs_display[cs] = DISP_BLOCK;
        cs_font_w[cs] = 700;
        return;
    }
    if (tag == T_DD) {
        cs_display[cs] = DISP_BLOCK;
        cs_padding[cs][3] = 24;
        return;
    }
    if (tag == T_TEXTAREA) {
        cs_display[cs] = DISP_INLINE_BLOCK;
        return;
    }
    if (tag == T_OPTION) {
        cs_display[cs] = DISP_BLOCK;
        return;
    }
    if (tag == T_CAPTION) {
        cs_display[cs] = DISP_TABLE_CAPTION;
        return;
    }
}

/* CSS value parsing */

int css_value_int(int off, int len) {
    /* Parse a length with optional 'px' / 'pt' / 'em' / 'vw' / 'vh' / '%'
     * suffix. Tracks the decimal part via fixed-point millipixels so '1.5em'
     * rounds to 24, not 16, before unit conversion.*/
    int sign = 1;
    int i = off;
    int end = off + len;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
        i = i + 1;
    if (i < end && css_value_pool[i] == '-') {
        sign = -1;
        i = i + 1;
    }
    int milli = 0; /* value * 1000 */
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
        int b = (i + 1 < end) ? css_value_pool[i + 1] : 0;
        int c2 = (i + 2 < end) ? css_value_pool[i + 2] : 0;
        if (a == 'r' && b == 'e' && c2 == 'm') {
            milli = milli * 16;
        } /* root em */
        else if (a == 'p' && b == 't') {
            milli = (milli * 4) / 3;
        } else if (a == 'e' && b == 'm') {
            milli = milli * 16;
        } /* 1em = 16px */
        else if (a == 'v' && b == 'w') {
            int vw_base = cur_cw - 12;
            if (vw_base < 0)
                vw_base = 0;
            milli = (milli * vw_base) / 100;
        } else if (a == 'v' && b == 'h') {
            int vh_base = cur_ch - ADDR_H - STATUS_H - 2;
            if (vh_base < 0)
                vh_base = 0;
            milli = (milli * vh_base) / 100;
        } else if (a == '%') {
            int pw_base = cur_cw - 12;
            if (pw_base < 0)
                pw_base = 0;
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
 * Negative values supported. Returns 0 on parse failure.*/
int css_value_len(int cs, int off, int len) {
    /* Resolve var() and calc() before parsing as a length. var() goes
     * first because var() can appear inside calc() (`calc(var(--g) -
     * 4px)`), and the calc() evaluator wants concrete numbers + units.*/
    char rbuf[1024];
    int rlen = 0;
    int has_var = 0;
    for (int sc = 0; sc + 4 <= len; sc = sc + 1) {
        if (css_value_pool[off + sc] == 'v' && css_value_pool[off + sc + 1] == 'a' &&
            css_value_pool[off + sc + 2] == 'r' && css_value_pool[off + sc + 3] == '(') {
            has_var = 1;
            break;
        }
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
     * width proxy used by '%' leaves.*/
    int ci = 0;
    while (ci < src_len && (src[ci] == ' ' || src[ci] == '\t'))
        ci = ci + 1;
    if (ci + 5 <= src_len && src[ci] == 'c' && src[ci + 1] == 'a' && src[ci + 2] == 'l' &&
        src[ci + 3] == 'c' && src[ci + 4] == '(') {
        int inner = ci + 5;
        int depth = 1;
        int e = inner;
        while (e < src_len && depth > 0) {
            if (src[e] == '(')
                depth = depth + 1;
            else if (src[e] == ')') {
                depth = depth - 1;
                if (depth == 0)
                    break;
            }
            e = e + 1;
        }
        int base_px_calc = cur_cw - 12;
        if (base_px_calc < 0)
            base_px_calc = 0;
        int got = eval_calc(src + inner, e - inner, base_px_calc);
        if (got >= 0)
            return got;
        return 0;
    }
    int sign = 1;
    int i = 0;
    int end = src_len;
    while (i < end && (src[i] == ' ' || src[i] == '\t'))
        i = i + 1;
    if (i < end && src[i] == '-') {
        sign = -1;
        i = i + 1;
    }
    if (i >= end)
        return 0;
    if (src[i] < '0' || src[i] > '9')
        return 0;
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
            if (digits < 2)
                frac = frac * 10 + (src[i] - '0');
            digits = digits + 1;
            i = i + 1;
        }
        if (digits == 1)
            frac = frac * 10;
    }
    int v_x100 = int_part * 100 + frac;
    while (i < end && (src[i] == ' ' || src[i] == '\t'))
        i = i + 1;

    int base_px = (cs >= 0 && cs < cs_count) ? cs_font_size_px[cs] : 16;
    if (base_px <= 0) {
        int p = (cs >= 0) ? n_parent[cs] : -1;
        while (p >= 0 && cs_font_size_px[p] <= 0)
            p = n_parent[p];
        base_px = (p >= 0 && cs_font_size_px[p] > 0) ? cs_font_size_px[p] : 16;
    }
    int root_px = (cs_count >= 2 && cs_font_size_px[1] > 0) ? cs_font_size_px[1] : 16;

    int result;
    if (i >= end) {
        result = v_x100 / 100;
    } else {
        int a = src[i];
        int b = (i + 1 < end) ? src[i + 1] : 0;
        int c2 = (i + 2 < end) ? src[i + 2] : 0;
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
            if (vw < 0)
                vw = 0;
            result = (v_x100 * vw) / 10000;
        } else if (a == 'v' && b == 'h') {
            int vh = cur_ch - ADDR_H - STATUS_H - 2;
            if (vh < 0)
                vh = 0;
            result = (v_x100 * vh) / 10000;
        } else if (a == '%') {
            int vw = cur_cw - 12;
            if (vw < 0)
                vw = 0;
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
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
        i++;
    if (end - i < 4)
        return 0;
    return (css_value_pool[i] == 'a' && css_value_pool[i + 1] == 'u' &&
            css_value_pool[i + 2] == 't' && css_value_pool[i + 3] == 'o');
}

int css_value_color(int off, int len, int *out) {
    /* Reuse parse_color (in dom.cc). Accepts #RGB / #RRGGBB / named colors. */
    char tmp[64];
    if (len >= 64)
        len = 63;
    int k = 0;
    while (k < len) {
        tmp[k] = css_value_pool[off + k];
        k = k + 1;
    }
    tmp[len] = 0;
    return parse_color(tmp, out);
}

int css_value_keyword(int off, int len, char *kw) {
    int kl = b_strlen(kw);
    if (len < kl)
        return 0;
    return b_strieq_n(css_value_pool + off, kw, kl);
}

/* Parse linear-gradient(<dir>?, <color>, <color>) into a 2-stop cardinal
 * gradient. Returns 1 on success and writes type (BG_GRAD_HORIZONTAL or
 * BG_GRAD_VERTICAL) plus c1/c2. Direction tokens accepted: "to top",
 * "to right", "to bottom", "to left", "0deg" / "90deg" / "180deg" / "270deg".
 * Default (no direction) is "to bottom" per CSS Images Module §3.1.
 * Reference: blink/Source/core/css/CSSGradientValue.cpp
 * (CSSGradientValue::parseLengthsAndPercents + applyAndScaleStops).*/
int css_value_linear_gradient(int off, int len, int *out_type, int *out_c1, int *out_c2) {
    char *s = css_value_pool;
    int p = off;
    int e = off + len;
    while (p < e && (s[p] == ' ' || s[p] == '\t'))
        p = p + 1;
    int kw_len = 15; /* "linear-gradient" */
    if (p + kw_len + 1 >= e)
        return 0;
    if (!b_strieq_n(s + p, "linear-gradient", kw_len))
        return 0;
    p = p + kw_len;
    while (p < e && (s[p] == ' ' || s[p] == '\t'))
        p = p + 1;
    if (p >= e || s[p] != '(')
        return 0;
    p = p + 1;
    int args_open = p;
    int depth = 1;
    int q = p;
    while (q < e && depth > 0) {
        if (s[q] == '(')
            depth = depth + 1;
        else if (s[q] == ')')
            depth = depth - 1;
        if (depth > 0)
            q = q + 1;
    }
    if (depth != 0)
        return 0;
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
        if (ch == '(')
            dep = dep + 1;
        else if (ch == ')')
            dep = dep - 1;
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
    if (arg_count < 2)
        return 0;
    /* Trim arg 0 for direction detection. */
    int a0_off = args_off[0];
    int a0_len = args_len[0];
    while (a0_len > 0 && (s[a0_off] == ' ' || s[a0_off] == '\t')) {
        a0_off = a0_off + 1;
        a0_len = a0_len - 1;
    }
    while (a0_len > 0 && (s[a0_off + a0_len - 1] == ' ' || s[a0_off + a0_len - 1] == '\t')) {
        a0_len = a0_len - 1;
    }
    int direction_idx = -1; /* 0=top, 1=right, 2=bottom, 3=left */
    int c1_arg = 0;
    int c2_arg = 1;
    if (a0_len >= 5 && (s[a0_off] == 't' || s[a0_off] == 'T') &&
        (s[a0_off + 1] == 'o' || s[a0_off + 1] == 'O') &&
        (s[a0_off + 2] == ' ' || s[a0_off + 2] == '\t')) {
        int o = a0_off + 3;
        int last = a0_off + a0_len;
        while (o < last && (s[o] == ' ' || s[o] == '\t'))
            o = o + 1;
        int dlen = last - o;
        if (dlen >= 6 && b_strieq_n(s + o, "bottom", 6))
            direction_idx = 2;
        else if (dlen >= 5 && b_strieq_n(s + o, "right", 5))
            direction_idx = 1;
        else if (dlen >= 4 && b_strieq_n(s + o, "left", 4))
            direction_idx = 3;
        else if (dlen >= 3 && b_strieq_n(s + o, "top", 3))
            direction_idx = 0;
        if (direction_idx < 0)
            return 0;
        c1_arg = 1;
        c2_arg = 2;
    } else {
        /* "<N>deg" form. */
        int v = 0;
        int q2 = 0;
        while (q2 < a0_len && s[a0_off + q2] >= '0' && s[a0_off + q2] <= '9') {
            v = v * 10 + (s[a0_off + q2] - '0');
            q2 = q2 + 1;
        }
        if (q2 > 0 && q2 + 2 < a0_len && b_strieq_n(s + a0_off + q2, "deg", 3)) {
            int deg = v % 360;
            if (deg < 0)
                deg = deg + 360;
            if (deg == 0)
                direction_idx = 0;
            else if (deg == 90)
                direction_idx = 1;
            else if (deg == 180)
                direction_idx = 2;
            else if (deg == 270)
                direction_idx = 3;
            else
                direction_idx = 2; /* nearest cardinal */
            c1_arg = 1;
            c2_arg = 2;
        } else {
            direction_idx = 2; /* default "to bottom" */
            c1_arg = 0;
            c2_arg = 1;
        }
    }
    if (c2_arg >= arg_count)
        return 0;
    int c1 = 0;
    int c2 = 0;
    if (!css_value_color(args_off[c1_arg], args_len[c1_arg], &c1))
        return 0;
    if (!css_value_color(args_off[c2_arg], args_len[c2_arg], &c2))
        return 0;
    int type = BG_GRAD_VERTICAL;
    if (direction_idx == 1 || direction_idx == 3)
        type = BG_GRAD_HORIZONTAL;
    /* gfx2d_gradient_v paints c1 at top -> c2 at bottom; gfx2d_gradient_h
     * paints c1 at left -> c2 at right. "to top" / "to left" reverse the
     * stop order so the named edge ends up holding the second color.*/
    if (direction_idx == 0 || direction_idx == 3) {
        int tmp = c1;
        c1 = c2;
        c2 = tmp;
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
 *   >= 27 px  -> LARGE-4  (h1, xx-large): same glyph, taller line box.*/
int px_to_tier(int px) {
    if (px <= 0)
        return 1;
    if (px <= 14)
        return 0;
    if (px <= 18)
        return 1;
    if (px <= 26)
        return 3;
    return 4;
}

/* Parse a CSS length / size value into px. Handles:
 *   <int>         -> int (treated as px)
 *   <num>px       -> num
 *   <num>em       -> num * parent_px
 *   <num>rem      -> num * root_px
 *   <num>%        -> num/100 * parent_px
 *   keyword font-size set: xx-small/x-small/small/medium/large/x-large/xx-large
 * Returns -1 on parse failure.*/
int parse_length_px(int off, int len, int parent_px, int root_px) {
    int i = off;
    int end = off + len;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
        i = i + 1;
    if (i >= end)
        return -1;
    int klen = end - i;
    if (klen >= 8 && css_value_keyword(i, klen, "xx-small"))
        return 9;
    if (klen >= 7 && css_value_keyword(i, klen, "x-small"))
        return 11;
    if (klen >= 6 && css_value_keyword(i, klen, "medium"))
        return 16;
    if (klen >= 7 && css_value_keyword(i, klen, "x-large"))
        return 24;
    if (klen >= 8 && css_value_keyword(i, klen, "xx-large"))
        return 32;
    if (klen >= 5 && css_value_keyword(i, klen, "small"))
        return 13;
    if (klen >= 5 && css_value_keyword(i, klen, "large"))
        return 19;
    if (css_value_pool[i] < '0' || css_value_pool[i] > '9')
        return -1;
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
            if (digits < 2)
                frac = frac * 10 + (css_value_pool[i] - '0');
            digits = digits + 1;
            i = i + 1;
        }
        if (digits == 1)
            frac = frac * 10;
    }
    int v_x100 = int_part * 100 + frac;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
        i = i + 1;
    if (i >= end)
        return v_x100 / 100;
    if (i + 1 < end && css_value_pool[i] == 'p' && css_value_pool[i + 1] == 'x') {
        return v_x100 / 100;
    }
    if (i + 2 < end && css_value_pool[i] == 'r' && css_value_pool[i + 1] == 'e' &&
        css_value_pool[i + 2] == 'm') {
        return (v_x100 * root_px) / 100;
    }
    if (i + 1 < end && css_value_pool[i] == 'e' && css_value_pool[i + 1] == 'm') {
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
 *   "normal"/anything else -> returns 0 (caller leaves sentinel)*/
int parse_line_height(int off, int len, int *out_value, int *out_is_mult) {
    int i = off;
    int end = off + len;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
        i = i + 1;
    if (i >= end)
        return 0;
    if (css_value_pool[i] < '0' || css_value_pool[i] > '9')
        return 0;
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
            if (digits < 2)
                frac = frac * 10 + (css_value_pool[i] - '0');
            digits = digits + 1;
            i = i + 1;
        }
        if (digits == 1)
            frac = frac * 10; /* "1.8" -> 80 */
    }
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
        i = i + 1;
    if (i + 1 < end && css_value_pool[i] == 'p' && css_value_pool[i + 1] == 'x') {
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
 * parents directly rather than through a separate inherited map.*/
int cs_var_lookup(int cs, char *name, int name_len, int *out_off, int *out_len) {
    int cur = cs;
    int hops = 0;
    while (cur >= 0 && hops < 32) {
        int n = cs_var_count[cur];
        for (int k = 0; k < n; k = k + 1) {
            if (cs_var_name_len[cur][k] != name_len)
                continue;
            int eq = 1;
            int o = cs_var_name_off[cur][k];
            for (int b = 0; b < name_len; b = b + 1) {
                if (css_value_pool[o + b] != name[b]) {
                    eq = 0;
                    break;
                }
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
 * Reference: Blink CSSVariableResolver::resolveVariableReference.*/
int cs_var_substitute(int cs, int val_off, int val_len, char *out, int out_cap) {
    int op = 0;
    int i = 0;
    while (i < val_len && op < out_cap - 1) {
        /* Detect "var(" */
        if (i + 4 <= val_len && css_value_pool[val_off + i] == 'v' &&
            css_value_pool[val_off + i + 1] == 'a' && css_value_pool[val_off + i + 2] == 'r' &&
            css_value_pool[val_off + i + 3] == '(') {
            int p = i + 4;
            while (p < val_len &&
                   (css_value_pool[val_off + p] == ' ' || css_value_pool[val_off + p] == '\t'))
                p = p + 1;
            int name_start = p;
            while (p < val_len && css_value_pool[val_off + p] != ',' &&
                   css_value_pool[val_off + p] != ')')
                p = p + 1;
            int name_end = p;
            while (name_end > name_start && (css_value_pool[val_off + name_end - 1] == ' ' ||
                                             css_value_pool[val_off + name_end - 1] == '\t'))
                name_end = name_end - 1;
            int fallback_start = -1;
            int fallback_end = -1;
            if (p < val_len && css_value_pool[val_off + p] == ',') {
                p = p + 1;
                while (p < val_len &&
                       (css_value_pool[val_off + p] == ' ' || css_value_pool[val_off + p] == '\t'))
                    p = p + 1;
                fallback_start = p;
                int depth = 1;
                while (p < val_len && depth > 0) {
                    if (css_value_pool[val_off + p] == '(')
                        depth = depth + 1;
                    else if (css_value_pool[val_off + p] == ')') {
                        depth = depth - 1;
                        if (depth == 0)
                            break;
                    }
                    p = p + 1;
                }
                fallback_end = p;
                while (fallback_end > fallback_start &&
                       (css_value_pool[val_off + fallback_end - 1] == ' ' ||
                        css_value_pool[val_off + fallback_end - 1] == '\t'))
                    fallback_end = fallback_end - 1;
            }
            if (p < val_len && css_value_pool[val_off + p] == ')')
                p = p + 1;
            int v_off = -1;
            int v_len = 0;
            int hit = cs_var_lookup(cs, css_value_pool + val_off + name_start,
                                    name_end - name_start, &v_off, &v_len);
            if (hit) {
                /* Recursively substitute in case the var's value also
                 * contains var() references. Bound depth by checking
                 * we don't re-enter the same name; for v1 we simply
                 * cap chained var() expansion at 4 hops via this byte
                 * limit (the resolver caps the ancestor walk too).*/
                char tmp[1024];
                int tn = cs_var_substitute(cs, v_off, v_len, tmp, 1024);
                for (int k = 0; k < tn && op < out_cap - 1; k = k + 1)
                    out[op++] = tmp[k];
            } else if (fallback_start >= 0) {
                char tmp2[1024];
                int tn2 = cs_var_substitute(cs, val_off + fallback_start,
                                            fallback_end - fallback_start, tmp2, 1024);
                for (int k = 0; k < tn2 && op < out_cap - 1; k = k + 1)
                    out[op++] = tmp2[k];
            }
            /* On unresolved with no fallback the substitution emits
             * nothing for this var() - the consumer typically falls
             * back to the property's initial value, which is what
             * Blink's behaviour reduces to.*/
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
 * top-level invocations would clobber state - the current call sites
 * never recurse from within eval_calc itself.
 *
 * Reference: blink/Source/core/css/CSSCalculationValue.cpp accumulator
 * pattern - track separate `pixels` and `percent` buckets so
 * `calc(100% - 16px)` stays exact at compute time.*/
char *ec_t;
int ec_n;
int ec_p;
int ec_err;

void ec_skip_ws(void) {
    while (ec_p < ec_n && (ec_t[ec_p] == ' ' || ec_t[ec_p] == '\t'))
        ec_p = ec_p + 1;
}

void ec_eval_expr(int *px, int *pct, int *is_num);

void ec_eval_term(int *px, int *pct, int *is_num) {
    ec_skip_ws();
    if (ec_p >= ec_n) {
        ec_err = 1;
        *px = 0;
        *pct = 0;
        *is_num = 1;
        return;
    }
    if (ec_t[ec_p] == '(') {
        ec_p = ec_p + 1;
        ec_eval_expr(px, pct, is_num);
        ec_skip_ws();
        if (ec_p < ec_n && ec_t[ec_p] == ')')
            ec_p = ec_p + 1;
        else
            ec_err = 1;
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
     * already swallows the binary '-' before getting here).*/
    int sign = 1;
    if (ec_t[ec_p] == '-') {
        sign = -1;
        ec_p = ec_p + 1;
        ec_skip_ws();
    }
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
    if (!has_digit) {
        ec_err = 1;
        *px = 0;
        *pct = 0;
        *is_num = 1;
        return;
    }
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
        if (ec_p >= ec_n)
            break;
        char op = ec_t[ec_p];
        if (op != '*' && op != '/')
            break;
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
            if (r_is_num)
                divisor = r_px;
            else
                divisor = 0;
            if (divisor == 0) {
                ec_err = 1;
                break;
            }
            *px = *px / divisor;
            *pct = *pct / divisor;
        }
    }
}

void ec_eval_expr(int *px, int *pct, int *is_num) {
    ec_eval_mul(px, pct, is_num);
    while (!ec_err) {
        ec_skip_ws();
        if (ec_p >= ec_n)
            break;
        char op = ec_t[ec_p];
        if (op != '+' && op != '-')
            break;
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
        if (!r_is_num)
            *is_num = 0;
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
    if (ec_err)
        return -1;
    int from_pct = (pct * base_px) / 100000;
    return px + from_pct;
}
