/* ---------- §2 Style resolver + UA stylesheet ----------
 * Plan-2 split:
 *   - This file (Plan-2 skeleton) declares the UA stylesheet helper and a
 *     stub style_resolve_all().
 *   - Task 5 fills style_resolve_all to produce a ComputedStyle per DOM node,
 *     applying UA + author cascade. */

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

    /* Per-tag overrides matching spec §2 UA stylesheet */
    if (tag == T_H1) { cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
                       cs_font_size_tier[cs] = 4;
                       cs_margin[cs][0] = 16; cs_margin[cs][2] = 16; }
    else if (tag == T_H2) { cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
                       cs_font_size_tier[cs] = 3;
                       cs_margin[cs][0] = 12; cs_margin[cs][2] = 12; }
    else if (tag == T_H3) { cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
                       cs_font_size_tier[cs] = 3;
                       cs_margin[cs][0] = 10; cs_margin[cs][2] = 10; }
    else if (tag == T_H4) { cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
                       cs_font_size_tier[cs] = 2;
                       cs_margin[cs][0] = 8; cs_margin[cs][2] = 8; }
    else if (tag == T_H5 || tag == T_H6) {
                       cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700;
                       cs_font_size_tier[cs] = 1;
                       cs_margin[cs][0] = 8; cs_margin[cs][2] = 8; }
    else if (tag == T_P) { cs_display[cs] = DISP_BLOCK;
                       cs_margin[cs][0] = 8; cs_margin[cs][2] = 8; }
    else if (tag == T_A) { cs_color[cs] = 0x0000EE; cs_text_dec[cs] = TD_UNDERLINE; }
    else if (tag == T_PRE) { cs_display[cs] = DISP_BLOCK;
                       cs_white_space[cs] = WS_PRE; }
    else if (tag == T_CODE) { /* inline; monospace approximated by leaving 8x8 */ }
    else if (tag == T_B || tag == T_STRONG) { cs_font_w[cs] = 700; }
    else if (tag == T_I || tag == T_EM) { cs_font_i[cs] = 1; cs_text_dec[cs] = TD_UNDERLINE; }
    else if (tag == T_UL || tag == T_OL || tag == T_DL) {
                       cs_display[cs] = DISP_BLOCK; cs_padding[cs][3] = 24;
                       cs_margin[cs][0] = 8; cs_margin[cs][2] = 8;
                       if (tag == T_OL) cs_list_style[cs] = LS_DECIMAL; }
    else if (tag == T_LI) { cs_display[cs] = DISP_LIST_ITEM; }
    else if (tag == T_TABLE) { cs_display[cs] = DISP_TABLE; }
    else if (tag == T_THEAD || tag == T_TBODY || tag == T_TFOOT) {
                       cs_display[cs] = DISP_TABLE_ROW_GROUP; }
    else if (tag == T_TR) { cs_display[cs] = DISP_TABLE_ROW; }
    else if (tag == T_TD) { cs_display[cs] = DISP_TABLE_CELL; cs_padding[cs][0] = 2;
                       cs_padding[cs][1] = 2; cs_padding[cs][2] = 2; cs_padding[cs][3] = 2; }
    else if (tag == T_TH) { cs_display[cs] = DISP_TABLE_CELL; cs_font_w[cs] = 700;
                       cs_text_align[cs] = TA_CENTER;
                       cs_padding[cs][0] = 2; cs_padding[cs][1] = 2;
                       cs_padding[cs][2] = 2; cs_padding[cs][3] = 2; }
    else if (tag == T_HR) { cs_display[cs] = DISP_BLOCK; cs_height[cs] = 1;
                       cs_bg[cs] = 0x808080;
                       cs_margin[cs][0] = 8; cs_margin[cs][2] = 8; }
    else if (tag == T_BLOCKQUOTE) { cs_display[cs] = DISP_BLOCK; cs_padding[cs][3] = 16;
                       cs_margin[cs][0] = 8; cs_margin[cs][2] = 8; }
    else if (tag == T_DIV) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_HEADER || tag == T_FOOTER || tag == T_NAV ||
             tag == T_SECTION || tag == T_ARTICLE || tag == T_ASIDE ||
             tag == T_MAIN) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_FORM) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_INPUT || tag == T_BUTTON) { cs_display[cs] = DISP_INLINE_BLOCK; }
    else if (tag == T_IMG) { cs_display[cs] = DISP_INLINE_BLOCK; }
    else if (tag == T_BODY) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_HTML) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_ROOT) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_HEAD || tag == T_SCRIPT || tag == T_STYLE ||
             tag == T_NOSCRIPT) { cs_display[cs] = DISP_NONE; }
    else if (tag == T_DT) { cs_display[cs] = DISP_BLOCK; cs_font_w[cs] = 700; }
    else if (tag == T_DD) { cs_display[cs] = DISP_BLOCK; cs_padding[cs][3] = 24; }
    else if (tag == T_TEXTAREA) { cs_display[cs] = DISP_INLINE_BLOCK; }
    else if (tag == T_OPTION) { cs_display[cs] = DISP_BLOCK; }
    else if (tag == T_CAPTION) { cs_display[cs] = DISP_TABLE_CAPTION; }
}

void style_resolve_all() {
    /* Stub. Task 5 fills this. */
    cs_count = 0;
}
