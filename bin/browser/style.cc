/* ---------- §2 Style resolver + UA stylesheet ----------
 * Plan-2 Task 5:
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

    /* Per-tag overrides matching spec §2 UA stylesheet. Flat if/return chain
     * (CupidC parser recurses into nested else; long else-if chains overflow
     * its stack). */
    if (tag == T_H1) {
        cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
        cs_font_size_tier[cs] = 4;
        cs_margin[cs][0] = 16; cs_margin[cs][2] = 16;
        return;
    }
    if (tag == T_H2) {
        cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
        cs_font_size_tier[cs] = 3;
        cs_margin[cs][0] = 12; cs_margin[cs][2] = 12;
        return;
    }
    if (tag == T_H3) {
        cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
        cs_font_size_tier[cs] = 3;
        cs_margin[cs][0] = 10; cs_margin[cs][2] = 10;
        return;
    }
    if (tag == T_H4) {
        cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
        cs_font_size_tier[cs] = 2;
        cs_margin[cs][0] = 8; cs_margin[cs][2] = 8;
        return;
    }
    if (tag == T_H5 || tag == T_H6) {
        cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
        cs_font_size_tier[cs] = 1;
        cs_margin[cs][0] = 8; cs_margin[cs][2] = 8;
        return;
    }
    if (tag == T_P) {
        cs_display[cs] = DISP_BLOCK;
        cs_margin[cs][0] = 8; cs_margin[cs][2] = 8;
        return;
    }
    if (tag == T_A) {
        cs_color[cs] = 0x0000EE; cs_text_dec[cs] = TD_UNDERLINE;
        return;
    }
    if (tag == T_PRE) {
        cs_display[cs] = DISP_BLOCK; cs_white_space[cs] = WS_PRE;
        return;
    }
    if (tag == T_CODE) { return; }
    if (tag == T_B || tag == T_STRONG) { cs_font_w[cs] = 700; return; }
    if (tag == T_I || tag == T_EM) {
        cs_font_i[cs] = 1; cs_text_dec[cs] = TD_UNDERLINE;
        return;
    }
    if (tag == T_UL || tag == T_OL || tag == T_DL) {
        cs_display[cs] = DISP_BLOCK; cs_padding[cs][3] = 24;
        cs_margin[cs][0] = 8; cs_margin[cs][2] = 8;
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
        return;
    }
    if (tag == T_TH) {
        cs_display[cs] = DISP_TABLE_CELL; cs_font_w[cs] = 700;
        cs_text_align[cs] = TA_CENTER;
        cs_padding[cs][0] = 2; cs_padding[cs][1] = 2;
        cs_padding[cs][2] = 2; cs_padding[cs][3] = 2;
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
    if (tag == T_BODY) { cs_display[cs] = DISP_BLOCK; return; }
    if (tag == T_HTML) { cs_display[cs] = DISP_BLOCK; return; }
    if (tag == T_ROOT) { cs_display[cs] = DISP_BLOCK; return; }
    if (tag == T_HEAD || tag == T_SCRIPT || tag == T_STYLE ||
        tag == T_NOSCRIPT) {
        cs_display[cs] = DISP_NONE;
        return;
    }
    if (tag == T_DT) { cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700; return; }
    if (tag == T_DD) { cs_display[cs] = DISP_BLOCK; cs_padding[cs][3] = 24; return; }
    if (tag == T_TEXTAREA) { cs_display[cs] = DISP_INLINE_BLOCK; return; }
    if (tag == T_OPTION) { cs_display[cs] = DISP_BLOCK; return; }
    if (tag == T_CAPTION) { cs_display[cs] = DISP_TABLE_CAPTION; return; }
}

/* ---------- Step 5.1: value parsers ---------- */

int css_value_int(int off, int len) {
    /* Parses an integer with optional 'px' / 'pt' / 'em' suffix. Returns px. */
    int sign = 1;
    int i = off;
    int end = off + len;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
    if (i < end && css_value_pool[i] == '-') { sign = -1; i = i + 1; }
    int v = 0;
    while (i < end && css_value_pool[i] >= '0' && css_value_pool[i] <= '9') {
        v = v * 10 + (css_value_pool[i] - '0');
        i = i + 1;
    }
    if (i + 1 < end) {
        int a = css_value_pool[i];
        int b = css_value_pool[i+1];
        if (a == 'p' && b == 't') { v = (v * 4) / 3; }
        else if (a == 'e' && b == 'm') { v = v * 8; }   /* 1em = 8px (base font) */
        /* px or unknown unit: treat as px */
    }
    return v * sign;
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

/* ---------- Step 5.2: single-property apply ---------- */

void cs_apply_property(int cs, int prop, int val_off, int val_len) {
    /* Flat if/return chain: CupidC parser overflows on long else-if chains.
     * Hoist `c` to function scope: CupidC keeps locals in a flat table, so
     * re-declaring `int c;` in multiple sibling branches conflicts. */
    int c;
    if (prop == CP_COLOR) {
        if (css_value_color(val_off, val_len, &c)) cs_color[cs] = c;
        return;
    }
    if (prop == CP_BG_COLOR || prop == CP_BG) {
        if (css_value_color(val_off, val_len, &c)) cs_bg[cs] = c;
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
    if (prop == CP_FONT_SIZE) {
        int px = css_value_int(val_off, val_len);
        if (px <= 9) { cs_font_size_tier[cs] = 0; return; }
        if (px <= 11) { cs_font_size_tier[cs] = 1; return; }
        if (px <= 14) { cs_font_size_tier[cs] = 2; return; }
        if (px <= 20) { cs_font_size_tier[cs] = 3; return; }
        cs_font_size_tier[cs] = 4;
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
        if (css_value_keyword(val_off, val_len, "inline"))       { cs_display[cs] = DISP_INLINE; return; }
        if (css_value_keyword(val_off, val_len, "list-item"))    { cs_display[cs] = DISP_LIST_ITEM; return; }
        if (css_value_keyword(val_off, val_len, "table"))        { cs_display[cs] = DISP_TABLE; return; }
        if (css_value_keyword(val_off, val_len, "table-row"))    { cs_display[cs] = DISP_TABLE_ROW; return; }
        if (css_value_keyword(val_off, val_len, "table-cell"))   { cs_display[cs] = DISP_TABLE_CELL; return; }
        return;
    }
    if (prop == CP_MARGIN || prop == CP_PADDING) {
        int vals[4];
        vals[0] = 0; vals[1] = 0; vals[2] = 0; vals[3] = 0;
        int nvals = 0;
        int i = val_off;
        int end = val_off + val_len;
        while (i < end && nvals < 4) {
            while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t')) i = i + 1;
            if (i >= end) break;
            int v_start = i;
            while (i < end && css_value_pool[i] != ' ' && css_value_pool[i] != '\t') i = i + 1;
            vals[nvals] = css_value_int(v_start, i - v_start);
            nvals = nvals + 1;
        }
        int t; int r; int b; int l;
        if (nvals == 1)      { t = vals[0]; r = vals[0]; b = vals[0]; l = vals[0]; }
        else if (nvals == 2) { t = vals[0]; b = vals[0]; r = vals[1]; l = vals[1]; }
        else if (nvals == 3) { t = vals[0]; r = vals[1]; l = vals[1]; b = vals[2]; }
        else                 { t = vals[0]; r = vals[1]; b = vals[2]; l = vals[3]; }
        if (prop == CP_MARGIN) {
            cs_margin[cs][0] = t; cs_margin[cs][1] = r; cs_margin[cs][2] = b; cs_margin[cs][3] = l;
        } else {
            cs_padding[cs][0] = t; cs_padding[cs][1] = r; cs_padding[cs][2] = b; cs_padding[cs][3] = l;
        }
        return;
    }
    if (prop == CP_MARGIN_T)  { cs_margin[cs][0]  = css_value_int(val_off, val_len); return; }
    if (prop == CP_MARGIN_R)  { cs_margin[cs][1]  = css_value_int(val_off, val_len); return; }
    if (prop == CP_MARGIN_B)  { cs_margin[cs][2]  = css_value_int(val_off, val_len); return; }
    if (prop == CP_MARGIN_L)  { cs_margin[cs][3]  = css_value_int(val_off, val_len); return; }
    if (prop == CP_PADDING_T) { cs_padding[cs][0] = css_value_int(val_off, val_len); return; }
    if (prop == CP_PADDING_R) { cs_padding[cs][1] = css_value_int(val_off, val_len); return; }
    if (prop == CP_PADDING_B) { cs_padding[cs][2] = css_value_int(val_off, val_len); return; }
    if (prop == CP_PADDING_L) { cs_padding[cs][3] = css_value_int(val_off, val_len); return; }
    if (prop == CP_BORDER) {
        cs_border[cs][0] = 1; cs_border[cs][1] = 1;
        cs_border[cs][2] = 1; cs_border[cs][3] = 1;
        if (css_value_color(val_off, val_len, &c)) cs_border_color[cs] = c;
        return;
    }
    if (prop == CP_BORDER_COLOR) {
        if (css_value_color(val_off, val_len, &c)) cs_border_color[cs] = c;
        return;
    }
    if (prop == CP_WIDTH)  { cs_width[cs]  = css_value_int(val_off, val_len); return; }
    if (prop == CP_HEIGHT) { cs_height[cs] = css_value_int(val_off, val_len); return; }
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
}

/* ---------- Step 5.5: inline style="..." parser ----------
 * Defined before sel_*/style_resolve_all so the latter can call it without
 * forward declarations. */

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
        int prop = css_match_property(s + p_start, p_end - p_start);
        if (prop) {
            int voff = css_intern_value(s + v_start, v_end - v_start);
            if (voff >= 0) cs_apply_property(cs, prop, voff, v_end - v_start);
        }
    }
}

/* ---------- Step 5.3: selector matching ---------- */

int sel_compound_matches(int sel_idx, int node) {
    int t = css_sel_tag[sel_idx];
    if (t != 0 && n_tag[node] != t) return 0;
    int c_off = css_sel_class_off[sel_idx];
    if (c_off >= 0) {
        int node_class = dom_class_off[node];
        if (node_class < 0) return 0;
        /* class= can have multiple space-separated values; check each */
        char *tgt = attr_pool + c_off;
        int tgt_len = b_strlen(tgt);
        char *cls = attr_pool + node_class;
        int cls_len = b_strlen(cls);
        int i = 0;
        int matched = 0;
        while (i < cls_len && !matched) {
            while (i < cls_len && (cls[i] == ' ' || cls[i] == '\t')) i = i + 1;
            int s = i;
            while (i < cls_len && cls[i] != ' ' && cls[i] != '\t') i = i + 1;
            if (i - s == tgt_len && b_strieq_n(cls + s, tgt, tgt_len)) { matched = 1; }
        }
        if (!matched) return 0;
    }
    int id_off = css_sel_id_off[sel_idx];
    if (id_off >= 0) {
        int node_id = dom_id_off[node];
        if (node_id < 0) return 0;
        if (!b_strieq(attr_pool + node_id, attr_pool + id_off)) return 0;
    }
    return 1;
}

/* Match a selector chain against a node by walking up parents. Last selector
 * matches `node`; earlier selectors must match an ancestor at any depth
 * (descendant combinator). */
int sel_chain_matches(int sel_first, int sel_count, int node) {
    if (sel_count == 0) return 0;
    int last = sel_first + sel_count - 1;
    if (!sel_compound_matches(last, node)) return 0;
    int cur = n_parent[node];
    int s = last - 1;
    while (s >= sel_first) {
        if (cur < 0) return 0;
        if (sel_compound_matches(s, cur)) {
            s = s - 1;
            cur = n_parent[cur];
        } else {
            cur = n_parent[cur];     /* descendant: skip non-matching ancestors */
        }
    }
    return 1;
}

/* ---------- Step 5.4: style_resolve_all ---------- */

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

        /* 2. Author rules in (specificity, doc-order) winning order. Track the
         *    highest-scoring matching rule per property, then apply once. */
        int winner_rule[32];
        int winner_score[32];
        for (int p = 0; p < 32; p = p + 1) { winner_rule[p] = -1; winner_score[p] = -1; }
        for (int r = 0; r < css_rule_count; r = r + 1) {
            int p = css_rule_prop_id[r];
            if (p < 1 || p >= 32) continue;
            int sf = css_rule_sel_first[r];
            int sc = css_rule_sel_count[r];
            if (!sel_chain_matches(sf, sc, n)) continue;
            int score = (css_rule_specificity[r] << 12) | (css_rule_doc_order[r] & 0xFFF);
            if (score > winner_score[p]) {
                winner_score[p] = score;
                winner_rule[p] = r;
            }
        }
        for (int p = 1; p < 32; p = p + 1) {
            int r = winner_rule[p];
            if (r >= 0) {
                cs_apply_property(cs, p,
                                  css_rule_value_off[r], css_rule_value_len[r]);
            }
        }

        /* 3. Inline style="..." attribute (always wins over author rules) */
        int sty_off = dom_attr_get(n, "style");
        if (sty_off >= 0) {
            apply_inline_style(cs, attr_pool + sty_off);
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
        } else {
            /* root: ensure color is concrete */
            if (cs_color[cs] < 0) cs_color[cs] = 0x000000;
        }
    }
}

/* Optional debug helper. Kept dormant — not called from parse_html. */
void dump_style(int n) {
    serial_printf("[browser] node %d (tag %d): disp=%d color=0x%x bg=0x%x weight=%d size=%d\n",
                  n, n_tag[n], cs_display[n], cs_color[n], cs_bg[n],
                  cs_font_w[n], cs_font_size_tier[n]);
}
