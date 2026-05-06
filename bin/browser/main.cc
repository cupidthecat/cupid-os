/* ---------- Globals + entry point ---------- */

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
    MAX_BOXES = 2048,
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

    /* §2 style/CSS */
    MAX_CSS_RULES = 256,
    MAX_COMPUTED_STYLES = 4096,
    CSS_VALUE_POOL_SIZE = 32768,

    /* §1 entity table */
    MAX_ENTITY_NAMES = 64,

    /* §4 layout scratch — pending inline atoms before line-box flush */
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

    /* §1 tokenizer token kinds */
    TK_START = 1,
    TK_END   = 2,
    TK_TEXT  = 3,
    TK_EOF   = 4,

    /* box kinds */
    BK_TEXT  = 0,
    BK_RECT  = 1,
    BK_IMG   = 2,
    BK_INPUT = 3,
    BK_HRULE = 4,
    BK_BUTTON = 5,

    /* focus */
    FOCUS_PAGE = 0,
    FOCUS_ADDR = 1,
    FOCUS_INPUT = 2
};

/* ---------- Global state ---------- */

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

/* §1 DOM attribute pool — each node has a contiguous slice of (name_off, value_off)
 * pairs in dom_ap_*[]. Slice begins at dom_attrs_first[i] and runs
 * dom_attrs_count[i] pairs. The slice is COPIED out of the tokenizer's scratch
 * ap_*[] (which is reused across pages) into the permanent dom_ap_*[] pool so
 * layout/paint can re-query during repaint. Strings (the offsets refer to)
 * stay in attr_pool which is also persistent across the page lifetime. */
int dom_attrs_first[MAX_NODES];
int dom_attrs_count[MAX_NODES];
int dom_ap_count;
int dom_ap_name_off [MAX_ATTR_PAIRS];
int dom_ap_value_off[MAX_ATTR_PAIRS];

/* §1 selector cache: id and class strings extracted at DOM-build time.
 * Used by §2 selector matching. -1 if absent. */
int dom_class_off[MAX_NODES];
int dom_id_off   [MAX_NODES];

/* node-back-reference for forms/inputs registered by the tree builder.
 * inputs[] / forms[] index → DOM node index. Lets handlers look up DOM
 * attrs (action, name, value, type) of the originating element. */
int input_node[MAX_INPUTS];
int form_node [MAX_FORMS];

char attr_pool[131072];
int  attr_pool_pos;

/* §1 tokenizer scratch — populated by tokenize(), consumed by tree builder in Task 3.
 * tok_text_len uses bit 0x40000000 as a sentinel: if set, tok_text_off is an
 * attr_pool offset (decoded RCDATA text); otherwise it is a page_buf offset.
 * The tree builder reads this as:
 *   int real_len = tok_text_len[ti] & 0x3FFFFFFF;
 *   int from_attr = (tok_text_len[ti] >> 30) & 1;
 */
int  tok_count;
int  tok_kind     [MAX_TOKENS];   /* TK_START, TK_END, TK_TEXT, TK_EOF */
int  tok_tag      [MAX_TOKENS];   /* T_* enum, only meaningful for START/END */
int  tok_attr_first[MAX_TOKENS];  /* index into ap_* arrays */
int  tok_attr_count[MAX_TOKENS];
int  tok_text_off [MAX_TOKENS];   /* offset into page_buf or attr_pool (see flag above) */
int  tok_text_len [MAX_TOKENS];
int  tok_self_close[MAX_TOKENS];

/* attr-pair pool: name and value byte-offsets into attr_pool */
int  ap_count;
int  ap_name_off  [MAX_ATTR_PAIRS];
int  ap_value_off [MAX_ATTR_PAIRS];

/* layout boxes */
int boxes_count;
int b_kind      [2048];
int b_x         [2048];
int b_y         [2048];
int b_w         [2048];
int b_h         [2048];
int b_fg        [2048];
int b_bg        [2048];
int b_text_off  [2048];
int b_text_len  [2048];
int b_link_idx  [2048];   /* -1 if none */
int b_input_idx [2048];
int b_img_handle[2048];
int b_bold      [2048];
int b_underline [2048];

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

/* ---------- Main ---------- */

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
    boxes_count = 0;
    inputs_count = 0;
    forms_count = 0;
    hist_count = 0;
    scroll_y = 0;
    doc_h = 0;
    page_bg = 0xFFFFFF;
    page_fg = 0x000000;
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
