/* Globals + entry point */

enum {
    SOCK_TCP   = 2,
    SOL_TLS    = 1,
    TLS_ENABLE = 1,

    WIN_W      = 600,
    WIN_H      = 420,
    WIN_X      = 20,
    WIN_Y      = 30,

    ADDR_H     = 24,
    STATUS_H   = 18,

    URL_MAX    = 1024,
    HOST_MAX   = 256,
    PATH_MAX_  = 1024,

    PAGE_BUF_SIZE   = 524288,
    HEADER_LINE_MAX = 1024,
    REQ_MAX         = 4096,
    RECV_BUF_SIZE   = 4096,

    MAX_NODES = 4096,
    MAX_LINKS = 1024,
    MAX_INPUTS = 64,
    MAX_FORMS  = 32,
    HIST_MAX   = 16,

    ATTR_POOL_SIZE = 131072,

    /* §1 tokenizer scratch */
    MAX_TOKENS = 16384,
    MAX_ATTR_PAIRS = 8192,

    /* §3 render tree pool */
    MAX_RT_NODES = 6144,
    MAX_LINE_ATOMS = 8192,

    /* §2 style/CSS */
    MAX_CSS_RULES = 256,
    MAX_COMPUTED_STYLES = 4096,
    CSS_VALUE_POOL_SIZE = 32768,

    /* §1 entity table */
    MAX_ENTITY_NAMES = 64,

    /* §4 layout scratch - pending inline atoms before line-box flush */
    MAX_PENDING_INLINE = 1024,

    /* tags */
    T_TEXT   = 0,
    T_HTML   = 1,
    T_HEAD   = 2,
    T_TITLE  = 3,
    T_BODY   = 4,
    T_P      = 5,
    T_H1     = 6,
    T_H2     = 7,
    T_H3     = 8,
    T_H4     = 9,
    T_H5     = 10,
    T_H6     = 11,
    T_A      = 12,
    T_BR     = 13,
    T_HR     = 14,
    T_UL     = 15,
    T_OL     = 16,
    T_LI     = 17,
    T_PRE    = 18,
    T_CODE   = 19,
    T_B      = 20,
    T_STRONG = 21,
    T_I      = 22,
    T_EM     = 23,
    T_SPAN   = 24,
    T_DIV    = 25,
    T_IMG    = 26,
    T_FORM   = 27,
    T_INPUT  = 28,
    T_BUTTON = 29,
    T_FONT   = 30,
    T_SCRIPT = 31,
    T_STYLE  = 32,
    T_OTHER  = 33,
    T_ROOT   = 34,
    T_TEXTAREA = 35,
    T_DT       = 36,
    T_DD       = 37,
    T_DL       = 38,
    T_THEAD    = 39,
    T_TBODY    = 40,
    T_TFOOT    = 41,
    T_CAPTION  = 42,
    T_OPTION   = 43,
    T_HEADER   = 44,
    T_FOOTER   = 45,
    T_NAV      = 46,
    T_SECTION  = 47,
    T_ARTICLE  = 48,
    T_ASIDE    = 49,
    T_MAIN     = 50,
    T_TABLE    = 51,
    T_TR       = 52,
    T_TD       = 53,
    T_TH       = 54,
    T_BLOCKQUOTE = 55,
    T_NOSCRIPT   = 56,

    /* §1 tokenizer token kinds */
    TK_START = 1,
    TK_END   = 2,
    TK_TEXT  = 3,
    TK_EOF   = 4,

    /* §2 CSS property IDs - internal */
    CP_COLOR = 1, CP_BG_COLOR, CP_BG, CP_FONT_WEIGHT, CP_FONT_STYLE,
    CP_FONT_SIZE, CP_TEXT_ALIGN, CP_TEXT_DEC, CP_DISPLAY,
    CP_MARGIN, CP_MARGIN_T, CP_MARGIN_R, CP_MARGIN_B, CP_MARGIN_L,
    CP_PADDING, CP_PADDING_T, CP_PADDING_R, CP_PADDING_B, CP_PADDING_L,
    CP_BORDER, CP_BORDER_COLOR, CP_BORDER_T, CP_BORDER_R, CP_BORDER_B, CP_BORDER_L,
    CP_WIDTH, CP_HEIGHT, CP_WHITE_SPACE, CP_LIST_STYLE_TYPE, CP_VERTICAL_ALIGN,

    /* §2 enumerated value tags */
    TA_LEFT = 0, TA_CENTER, TA_RIGHT,
    TD_UNDERLINE = 1, TD_LINE_THROUGH = 2,
    DISP_BLOCK = 1, DISP_INLINE, DISP_INLINE_BLOCK, DISP_LIST_ITEM,
    DISP_TABLE, DISP_TABLE_ROW_GROUP, DISP_TABLE_ROW, DISP_TABLE_CELL,
    DISP_TABLE_CAPTION, DISP_NONE,
    WS_NORMAL = 0, WS_PRE, WS_NOWRAP,
    LS_DISC = 0, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_NONE,
    VA_BASELINE = 0, VA_TOP, VA_MIDDLE, VA_BOTTOM,
    MAX_CSS_SELECTORS = 1024,

    /* §3 render-tree kinds */
    RT_BLOCK = 1,
    RT_INLINE,
    RT_INLINE_BLOCK,
    RT_LIST_ITEM,
    RT_LIST_MARKER,
    RT_TABLE,
    RT_TABLE_ROW_GROUP,
    RT_TABLE_ROW,
    RT_TABLE_CELL,
    RT_TABLE_CAPTION,
    RT_TEXT,
    RT_REPLACED,        /* <img>, <input>, <button>, <textarea> */
    RT_LINE_BOX,

    /* focus */
    FOCUS_PAGE = 0,
    FOCUS_ADDR = 1,
    FOCUS_INPUT = 2
};

/* Global state */

int  win;
int  font_id;          /* 0=NORMAL 8x8 */
int  line_h;           /* px per line */
int  char_w;           /* px per char */

char cur_url[1024];
char cur_host[256];
int  cur_port;
char cur_path[1024];
int  cur_is_https;

char addr_buf[1024];
int  addr_len;
int  addr_cursor;

char title_buf[256];
char status_msg[256];

char page_buf[524288];
int  page_len;

char ctype_buf[4096];

/* DOM nodes (parallel arrays) */
int nodes_count;
int n_tag       [4096];
int n_parent    [4096];
int n_first_child[4096];
int n_next      [4096];
int n_text_off  [4096];   /* into page_buf for TEXT nodes */
int n_text_len  [4096];

/* §1 DOM attribute pool - each node has a contiguous slice of (name_off, value_off)
 * pairs in dom_ap_*[]. Slice begins at dom_attrs_first[i] and runs
 * dom_attrs_count[i] pairs. The slice is COPIED out of the tokenizer's scratch
 * ap_*[] (which is reused across pages) into the permanent dom_ap_*[] pool so
 * layout/paint can re-query during repaint. Strings (the offsets refer to)
 * stay in attr_pool which is also persistent across the page lifetime. */
int dom_attrs_first[4096];
int dom_attrs_count[4096];
int dom_ap_count;
int dom_ap_name_off [8192];
int dom_ap_value_off[8192];

/* §1 selector cache: id and class strings extracted at DOM-build time.
 * Used by §2 selector matching. -1 if absent. */
int dom_class_off[4096];
int dom_id_off   [4096];

/* node-back-reference for forms/inputs registered by the tree builder.
 * inputs[] / forms[] index -> DOM node index. Lets handlers look up DOM
 * attrs (action, name, value, type) of the originating element. */
int input_node[64];
int form_node [32];

char attr_pool[131072];
int  attr_pool_pos;

/* §2 CSS rule table - populated by css.cc, consumed by style.cc resolver.
 * One rule = one selector chain + one declaration block. Multiple
 * declarations in a single CSS rule produce multiple entries here
 * (one per property), so MAX_CSS_RULES caps property-count, not rule-count. */
int css_rule_count;
int css_rule_sel_first[256];
int css_rule_sel_count[256];
int css_rule_prop_id  [256];
int css_rule_value_off[256];
int css_rule_value_len[256];
int css_rule_specificity[256];
int css_rule_doc_order [256];

/* Each selector is a chain of "compound selectors". A compound is a
 * (tag, class_off, id_off) triple. Descendant combinators are implicit. */
int css_sel_count;
int css_sel_tag      [1024];
int css_sel_class_off[1024];
int css_sel_id_off   [1024];
/* Combinator joining this compound to the previous one in a chain.
 * 0 = descendant (default; whitespace separator).
 * 1 = child (>). The first compound in a chain is always 0. */
int css_sel_combinator[1024];
/* Optional attribute selector on this compound. -1 if none.
 * css_sel_attr_op: 0=none, 1=presence ([attr]), 2=exact ([attr=v]),
 *                  3=word match ([attr~=v]). */
int css_sel_attr_off    [1024];
int css_sel_attr_val_off[1024];
int css_sel_attr_op     [1024];

/* CSS value pool - separate from attr_pool. */
char css_value_pool[32768];
int  css_value_pool_pos;

/* §2 ComputedStyle pool - one entry per render node. */
int cs_count;
int cs_color    [4096];
int cs_bg       [4096];
int cs_font_w   [4096];
int cs_font_i   [4096];
int cs_font_size_tier[4096];
int cs_text_align[4096];
int cs_text_dec [4096];
int cs_display  [4096];
int cs_margin   [4096][4];
int cs_margin_auto[4096][4];   /* 1 if that margin side was set to "auto" */
int cs_padding  [4096][4];
int cs_border   [4096][4];
int cs_border_color[4096];
int cs_width    [4096];
int cs_height   [4096];
int cs_white_space[4096];
int cs_list_style[4096];
int cs_vertical_align[4096];

/* §3 Render tree pool - sized at MAX_RT_NODES (6144 per spec) */
int rt_count;
int rt_dom         [6144];   /* back-pointer to DOM node, -1 for anonymous */
int rt_parent      [6144];
int rt_first_child [6144];
int rt_next        [6144];
int rt_kind        [6144];   /* RT_* */
int rt_style       [6144];   /* index into cs_*[] */
int rt_text_off    [6144];
int rt_text_len    [6144];
int rt_intrinsic_w [6144];
int rt_intrinsic_h [6144];
int rt_link_idx    [6144];   /* link[] index for clickable, -1 otherwise */
int rt_input_idx   [6144];   /* input[] index, -1 otherwise */

/* Geometry filled by §4 layout */
int rt_x[6144];
int rt_y[6144];
int rt_w[6144];
int rt_h[6144];
int rt_content_x[6144];
int rt_content_y[6144];
int rt_baseline[6144];

/* Line-box atom storage - one entry per word/glyph in an inline run.
 * line_box render nodes are LINE_BOX kind with rt_first_child indexing into
 * the atom pool via a separate atom_first/count pair. */
int la_count;
int la_x        [8192];   /* x within line box (cumulative) */
int la_w        [8192];
int la_text_off [8192];   /* into attr_pool */
int la_text_len [8192];
int la_font_tier[8192];
int la_fg       [8192];
int la_bg       [8192];
int la_bold     [8192];
int la_underline[8192];
int la_link_idx [8192];

/* Each LINE_BOX render node references a contiguous atom slice */
int rt_line_atom_first[6144];
int rt_line_atom_count[6144];

/* §1 tokenizer scratch - populated by tokenize(), consumed by tree builder in Task 3.
 * tok_text_len uses bit 0x40000000 as a sentinel: if set, tok_text_off is an
 * attr_pool offset (decoded RCDATA text); otherwise it is a page_buf offset.
 * The tree builder reads this as:
 *   int real_len = tok_text_len[ti] & 0x3FFFFFFF;
 *   int from_attr = (tok_text_len[ti] >> 30) & 1;
 */
int  tok_count;
int  tok_kind     [16384];   /* TK_START, TK_END, TK_TEXT, TK_EOF */
int  tok_tag      [16384];   /* T_* enum, only meaningful for START/END */
int  tok_attr_first[16384];  /* index into ap_* arrays */
int  tok_attr_count[16384];
int  tok_text_off [16384];   /* offset into page_buf or attr_pool (see flag above) */
int  tok_text_len [16384];
int  tok_self_close[16384];

/* attr-pair pool: name and value byte-offsets into attr_pool */
int  ap_count;
int  ap_name_off  [8192];
int  ap_value_off [8192];

/* link table */
int  links_count;
int  link_url_off[1024];   /* into attr_pool */

/* form inputs */
int  inputs_count;
char input_value[8192];   /* parallel: 128 bytes per input */
int  input_name_off[64];
int  input_form[64];

/* form info */
int  forms_count;
int  form_action[32];     /* attr_pool offset of action url */

/* doc */
int  doc_h;
int  scroll_y;
int  page_bg;
int  page_fg;
/* Set by document_bg() when body's bg propagates up to the canvas;
 * paint_rt_node uses it to skip body's own bg paint and avoid the
 * margin-inset double-paint. */
int  doc_bg_suppress_body;

/* history */
char hist_url_pool[16384];
int  hist_count;

/* state */
int  focus_mode;     /* 0=page, 1=addr, 2=input */
int  focused_input;  /* idx into inputs */
int  prev_buttons;
int  hover_link;     /* -1 or link idx */

/* current content area dimensions (excluding window chrome) */
int  cur_cw;
int  cur_ch;

/* Main */

void browser_main() {
    char *raw = (char*)get_args();

    font_id = 0;
    char_w = 8;
    line_h = 12;
    addr_buf[0] = 0; addr_len = 0; addr_cursor = 0;
    title_buf[0] = 0;
    status_msg[0] = 0;
    cur_url[0] = 0;
    nodes_count = 0;
    attr_pool_pos = 1;
    inputs_count = 0;
    forms_count = 0;
    hist_count = 0;
    scroll_y = 0;
    doc_h = 0;
    page_bg = 0xFFFFFF;
    page_fg = 0x000000;
    doc_bg_suppress_body = 0;
    focus_mode = FOCUS_PAGE;
    focused_input = -1;
    prev_buttons = 0;
    hover_link = -1;

    win = gui_win_create("Browser", WIN_X, WIN_Y, WIN_W, WIN_H);
    if (win == -1) {
        println("browser: failed to create window");
        return;
    }
    cur_cw = gui_win_content_w(win);
    cur_ch = gui_win_content_h(win);

    /* Initial URL: argument or default */
    char start[1024];
    start[0] = 0;
    if (raw && raw[0]) {
        int i = 0;
        while (raw[i] == ' ' || raw[i] == '\t') i = i + 1;
        if (raw[i]) {
            int p = 0;
            while (raw[i] && raw[i] != ' ' && raw[i] != '\t' &&
                   p < URL_MAX - 1) {
                start[p] = raw[i]; p = p + 1; i = i + 1;
            }
            start[p] = 0;
        }
    }
    if (!start[0]) {
        b_strcpy_n(start, "http://example.com/", URL_MAX);
    }

    /* If URL has no scheme, prepend http:// */
    if (!b_strieq_n(start, "http://", 7) &&
        !b_strieq_n(start, "https://", 8)) {
        char tmp[1024];
        b_strcpy_n(tmp, "http://", URL_MAX);
        int sl = b_strlen(tmp);
        b_strcpy_n(tmp + sl, start, URL_MAX - sl);
        b_strcpy_n(start, tmp, URL_MAX);
    }

    navigate(start);

    while (gui_win_is_open(win)) {
        if (!gui_win_can_draw(win)) {
            yield();
            continue;
        }
        int new_cw = gui_win_content_w(win);
        int new_ch = gui_win_content_h(win);
        if (new_cw != cur_cw || new_ch != cur_ch) {
            cur_cw = new_cw;
            cur_ch = new_ch;
            run_layout();
            clamp_scroll();
        }
        handle_keys();
        handle_mouse();
        render();
        yield();
    }
}
