/* Globals + entry point */

enum {
    SOCK_TCP   = 2,
    SOL_TLS    = 1,
    TLS_ENABLE = 1,

    WIN_W      = 600,
    WIN_H      = 420,
    WIN_X      = 20,
    WIN_Y      = 30,

    ADDR_H     = 28,
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
    MAX_CSS_RULES = 512,
    MAX_COMPUTED_STYLES = 4096,
    CSS_VALUE_POOL_SIZE = 32768,
    MAX_CSS_NOT_SELS = 256,

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
    T_LINK       = 57,

    /* §1 tokenizer token kinds */
    TK_START = 1,
    TK_END   = 2,
    TK_TEXT  = 3,
    TK_EOF   = 4,

    /* §2 CSS property IDs - internal. Keep <= MAX_CP_ID; the cascade loop in
     * style_resolve_all uses winner_rule[MAX_CP_ID]. */
    CP_COLOR = 1, CP_BG_COLOR, CP_BG, CP_FONT_WEIGHT, CP_FONT_STYLE,
    CP_FONT_SIZE, CP_TEXT_ALIGN, CP_TEXT_DEC, CP_DISPLAY,
    CP_MARGIN, CP_MARGIN_T, CP_MARGIN_R, CP_MARGIN_B, CP_MARGIN_L,
    CP_PADDING, CP_PADDING_T, CP_PADDING_R, CP_PADDING_B, CP_PADDING_L,
    CP_BORDER, CP_BORDER_COLOR, CP_BORDER_T, CP_BORDER_R, CP_BORDER_B, CP_BORDER_L,
    CP_BORDER_WIDTH, CP_BORDER_STYLE, CP_FONT,
    CP_FONT_FAMILY,
    CP_WIDTH, CP_HEIGHT, CP_WHITE_SPACE, CP_LIST_STYLE_TYPE, CP_VERTICAL_ALIGN,
    CP_LINE_HEIGHT,
    CP_MAX_WIDTH, CP_MIN_WIDTH, CP_MAX_HEIGHT, CP_MIN_HEIGHT,
    CP_CONTENT,
    CP_BORDER_RADIUS, CP_BOX_SHADOW, CP_OVERFLOW,
    MAX_CP_ID = 48,

    /* Generic-family keywords. Numeric values mirror the kernel's
     * FONTSYS_FAMILY_* (kernel/gfx/fontsys.h) so cs_font_generic[cs]
     * can be passed straight to fontsys_match. */
    FONTSYS_FAMILY_DEFAULT    = 0,
    FONTSYS_FAMILY_SERIF      = 1,
    FONTSYS_FAMILY_SANS_SERIF = 2,
    FONTSYS_FAMILY_MONOSPACE  = 3,
    FONTSYS_FAMILY_CURSIVE    = 4,
    FONTSYS_FAMILY_FANTASY    = 5,
    FONTSYS_FAMILY_SYSTEM_UI  = 6,

    /* §2 enumerated value tags */
    TA_LEFT = 0, TA_CENTER, TA_RIGHT,
    TD_UNDERLINE = 1, TD_LINE_THROUGH = 2,
    DISP_BLOCK = 1, DISP_INLINE, DISP_INLINE_BLOCK, DISP_LIST_ITEM,
    DISP_TABLE, DISP_TABLE_ROW_GROUP, DISP_TABLE_ROW, DISP_TABLE_CELL,
    DISP_TABLE_CAPTION, DISP_NONE,
    WS_NORMAL = 0, WS_PRE, WS_NOWRAP,
    LS_DISC = 0, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_NONE,
    VA_BASELINE = 0, VA_TOP, VA_MIDDLE, VA_BOTTOM,
    OVERFLOW_VISIBLE = 0, OVERFLOW_HIDDEN,
    BS_SOLID = 0, BS_DASHED = 1, BS_DOTTED = 2, BS_NONE = 3,
    MAX_CSS_SELECTORS = 2048,

    /* §2 selector pseudo-class IDs (stored in css_sel_pseudo[]) */
    PSEUDO_NONE = 0,
    PSEUDO_HOVER = 1,
    PSEUDO_FOCUS = 2,
    PSEUDO_LINK = 3,
    PSEUDO_VISITED = 4,
    PSEUDO_FIRST_CHILD = 5,
    PSEUDO_LAST_CHILD = 6,
    PSEUDO_NTH_CHILD = 7,    /* css_sel_pseudo_arg packs (a<<16)|b for an+b */
    PSEUDO_NOT = 8,           /* css_sel_not_idx points into css_not_* pool */
    PSEUDO_ROOT = 9,
    PSEUDO_FIRST_OF_TYPE = 10,
    PSEUDO_LAST_OF_TYPE = 11,
    PSEUDO_EMPTY = 12,

    /* §2 selector pseudo-element IDs (stored in css_sel_pseudo_elt[]) */
    PSELT_NONE = 0,
    PSELT_BEFORE = 1,
    PSELT_AFTER = 2,

    /* §2 selector combinators */
    COMB_DESCENDANT = 0,
    COMB_CHILD = 1,
    COMB_ADJACENT = 2,        /* + */
    COMB_GEN_SIBLING = 3,     /* ~ */
    COMB_SUBSELECTOR = 4,     /* same-element AND (.a.b, a:hover) */

    /* §2 attribute-selector ops */
    ATTR_OP_NONE = 0,
    ATTR_OP_PRESENCE = 1,     /* [attr] */
    ATTR_OP_EXACT = 2,        /* [attr=v] */
    ATTR_OP_WORD = 3,         /* [attr~=v] */
    ATTR_OP_PREFIX = 4,       /* [attr^=v] */
    ATTR_OP_SUFFIX = 5,       /* [attr$=v] */
    ATTR_OP_CONTAINS = 6,     /* [attr*=v] */

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
    FOCUS_INPUT = 2,

    /* §7 JavaScript engine pools */
    MAX_JS_TOKENS = 8192,
    MAX_JS_NODES  = 8192,
    JS_STR_POOL   = 65536,
    MAX_JS_SCRIPTS = 32,
    MAX_JS_VS     = 1024,   /* value stack depth */
    MAX_JS_BINDINGS = 1024,
    MAX_JS_SCOPES = 256,
    MAX_JS_FUNCS  = 256,
    MAX_JS_OBJS   = 512,
    MAX_JS_PROPS  = 4096,

    /* JS value tags */
    JS_VAL_UNDEF = 0,
    JS_VAL_NULL,
    JS_VAL_BOOL,
    JS_VAL_NUM,
    JS_VAL_STR,
    JS_VAL_OBJ,
    JS_VAL_ARR,
    JS_VAL_FUNC,
    JS_VAL_NATIVE,
    JS_VAL_DOMNODE,
    JS_VAL_STYLE,        /* element.style proxy; dom_idx = element */

    /* JS token kinds */
    JS_TOK_EOF = 0,
    JS_TOK_NUMBER, JS_TOK_STRING, JS_TOK_IDENT,
    JS_TOK_LBRACE, JS_TOK_RBRACE, JS_TOK_LPAREN, JS_TOK_RPAREN,
    JS_TOK_LBRACK, JS_TOK_RBRACK,
    JS_TOK_SEMI, JS_TOK_COMMA, JS_TOK_DOT, JS_TOK_COLON, JS_TOK_QUESTION,
    JS_TOK_ASSIGN, JS_TOK_PLUS, JS_TOK_MINUS, JS_TOK_STAR, JS_TOK_SLASH,
    JS_TOK_PERCENT,
    JS_TOK_PLUS_EQ, JS_TOK_MINUS_EQ, JS_TOK_STAR_EQ, JS_TOK_SLASH_EQ,
    JS_TOK_PLUS_PLUS, JS_TOK_MINUS_MINUS,
    JS_TOK_EQ, JS_TOK_NEQ, JS_TOK_EQ_EQ, JS_TOK_NEQ_EQ,
    JS_TOK_LT, JS_TOK_GT, JS_TOK_LE, JS_TOK_GE,
    JS_TOK_AND_AND, JS_TOK_OR_OR, JS_TOK_NOT,
    JS_TOK_KW_VAR, JS_TOK_KW_LET, JS_TOK_KW_CONST,
    JS_TOK_KW_FUNCTION, JS_TOK_KW_RETURN,
    JS_TOK_KW_IF, JS_TOK_KW_ELSE,
    JS_TOK_KW_WHILE, JS_TOK_KW_FOR,
    JS_TOK_KW_BREAK, JS_TOK_KW_CONTINUE,
    JS_TOK_KW_TRUE, JS_TOK_KW_FALSE,
    JS_TOK_KW_NULL, JS_TOK_KW_UNDEFINED,
    JS_TOK_KW_NEW, JS_TOK_KW_TYPEOF,

    /* JS native function IDs */
    JS_NATIVE_DOC_GET_ELEMENT_BY_ID = 1,
    JS_NATIVE_DOC_CREATE_ELEMENT,
    JS_NATIVE_DOC_QUERY_SELECTOR,
    JS_NATIVE_EL_GET_ATTRIBUTE,
    JS_NATIVE_EL_SET_ATTRIBUTE,
    JS_NATIVE_EL_APPEND_CHILD,
    JS_NATIVE_EL_REMOVE,

    /* JS AST node kinds */
    JS_NODE_NONE = 0,
    JS_NODE_NUM, JS_NODE_STR, JS_NODE_BOOL, JS_NODE_NULL, JS_NODE_UNDEF,
    JS_NODE_IDENT, JS_NODE_ARR_LIT, JS_NODE_OBJ_LIT, JS_NODE_OBJ_PROP,
    JS_NODE_BIN, JS_NODE_UNARY, JS_NODE_ASSIGN,
    JS_NODE_MEMBER, JS_NODE_INDEX, JS_NODE_CALL,
    JS_NODE_COND, JS_NODE_PRE_INC, JS_NODE_POST_INC,
    JS_NODE_EXPR_STMT, JS_NODE_VAR_DECL, JS_NODE_VAR_DECLARATOR,
    JS_NODE_BLOCK, JS_NODE_IF, JS_NODE_WHILE, JS_NODE_FOR,
    JS_NODE_RETURN, JS_NODE_BREAK, JS_NODE_CONTINUE,
    JS_NODE_FUNC_DECL, JS_NODE_FUNC_EXPR, JS_NODE_PROGRAM
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

/* §1 sibling caches populated after DOM build (see populate_sibling_caches
 * in dom.cc). Element-only counts (text nodes ignored), used by selector
 * matcher for :first-child / :last-child / :nth-child / + / ~. */
int n_sibling_idx     [4096];   /* 1-based index among element siblings */
int n_sibling_count   [4096];   /* total element-sibling count of n_parent[i] */
int n_prev_sibling_elt[4096];   /* prev element sibling, -1 if none */
int n_next_sibling_elt[4096];   /* next element sibling, -1 if none */

/* Mutable checkbox/radio state per DOM node. Parser seeds from the initial
 * `checked` HTML attribute; click handling toggles this in place since the
 * DOM has no attr-set helper. paint_rt_replaced reads this instead of
 * re-querying `checked` so a click can be reflected without rebuilding
 * the DOM. */
int n_checkbox_state[4096];

/* §2 generated content for ::before / ::after. Per element, the cascade
 * resolves a single string (decoded from CSS `content:`). render_tree.cc
 * injects a synthetic RT_TEXT child as first/last child of the
 * originating element when these are set. -1 = no generated content. */
int n_pseudo_before_off[4096];
int n_pseudo_before_len[4096];
int n_pseudo_after_off [4096];
int n_pseudo_after_len [4096];

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
int css_rule_sel_first[512];
int css_rule_sel_count[512];
int css_rule_prop_id  [512];
int css_rule_value_off[512];
int css_rule_value_len[512];
int css_rule_specificity[512];
int css_rule_doc_order [512];
int css_rule_important [512];           /* 1 if value ended in !important */

/* Each selector is a chain of "compound selectors". A compound is a
 * (tag, class_off, id_off) triple. Combinators see COMB_* enum. */
int css_sel_count;
int css_sel_tag      [2048];
int css_sel_class_off[2048];
int css_sel_id_off   [2048];
/* Combinator joining this compound to the previous one in a chain.
 * COMB_DESCENDANT (0) is default (whitespace), COMB_CHILD (>),
 * COMB_ADJACENT (+), COMB_GEN_SIBLING (~), COMB_SUBSELECTOR (same element,
 * for chained simple selectors like ".a.b" or "a:hover"). */
int css_sel_combinator[2048];
/* Optional attribute selector on this compound. -1 if absent.
 * css_sel_attr_op uses ATTR_OP_* enum. */
int css_sel_attr_off    [2048];
int css_sel_attr_val_off[2048];
int css_sel_attr_op     [2048];
/* Pseudo-class on this compound (PSEUDO_* enum). */
int css_sel_pseudo      [2048];
/* Pseudo-class argument: for PSEUDO_NTH_CHILD, packed (a<<16)|(b&0xFFFF)
 * with signed semantics; otherwise 0. */
int css_sel_pseudo_arg  [2048];
/* Pseudo-element flag: PSELT_NONE/BEFORE/AFTER. Tail of a compound only. */
int css_sel_pseudo_elt  [2048];
/* For PSEUDO_NOT, index into css_not_* pool of the inner simple compound;
 * -1 if no :not. */
int css_sel_not_idx     [2048];

/* §2 :not(<simple>) inner-compound pool. v1 supports one-deep, single
 * simple compound only (no combinators, no nested :not). */
int css_not_count;
int css_not_tag        [256];
int css_not_class_off  [256];
int css_not_id_off     [256];
int css_not_attr_off   [256];
int css_not_attr_val_off[256];
int css_not_attr_op    [256];
int css_not_pseudo     [256];   /* simple pseudos (:first-child etc.); no :not */
int css_not_pseudo_arg [256];
/* Set true at parse time if any rule references :hover or :focus.
 * Cheap gate: when false, the hover-driven restyle path is skipped. */
int css_has_dynamic_pseudo;
/* DOM node currently hovered (deepest rt_dom hit), -1 if none.
 * prev_hover_dom_node is the previous frame's value used to detect
 * change and trigger restyle. */
int hover_dom_node;
int prev_hover_dom_node;

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
/* line-height: -1 = unset (use tier_line_h fallback). When cs_line_height_mult
 * is 1, the value is a unitless multiplier scaled by 100 (e.g. "1.8" -> 180);
 * when 0, the value is a px length. */
int cs_line_height[4096];
int cs_line_height_mult[4096];
/* font-size in px. -1 = unset (inherit from parent during cascade fill).
 * cs_font_size_tier is derived from this via px_to_tier(px). */
int cs_font_size_px[4096];
/* font-family: stashed in css_value_pool. -1 off = unset (inherit). The
 * string is the verbatim CSS value (comma list, with quotes/spaces).
 * cs_font_generic mirrors a FONTSYS_FAMILY_* keyword. */
int cs_font_family_off[4096];
int cs_font_family_len[4096];
int cs_font_generic   [4096];
/* Sizing clamps. -1 = unset (no clamp). */
int cs_max_width [4096];
int cs_min_width [4096];
int cs_max_height[4096];
int cs_min_height[4096];

/* §2.x Visual-quality additions (Blink BoxPainter.cpp). All default 0/-1. */
int cs_border_radius[4096];   /* px; 0 = sharp corners */
int cs_overflow     [4096];   /* OVERFLOW_VISIBLE | OVERFLOW_HIDDEN */
int cs_border_style [4096];   /* BS_SOLID | BS_DASHED | BS_DOTTED | BS_NONE */
int cs_shadow_has   [4096];   /* 1 if box-shadow declared, else 0 */
int cs_shadow_dx    [4096];   /* px offset, signed */
int cs_shadow_dy    [4096];   /* px offset, signed */
int cs_shadow_color [4096];   /* RGB; black if unspecified */

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
/* fontsys-resolved per-atom face/size/italic. la_size_px <= 0 falls
 * back to the tier-based path in paint. */
int la_size_px  [8192];
int la_face_id  [8192];
int la_italic   [8192];

/* Each LINE_BOX render node references a contiguous atom slice */
int rt_line_atom_first[6144];
int rt_line_atom_count[6144];

/* §1 tokenizer scratch - populated by tokenize(), consumed by tree builder.
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

/* §7 JS engine pools. Reset per page from navigate() (mirrors the
 * attr_pool per-page reset pattern). All sizes are conservative for
 * the small scripts the browser is expected to encounter. */
int  jtk_kind   [8192];     /* MAX_JS_TOKENS */
int  jtk_num    [8192];
int  jtk_str_off[8192];
int  jtk_str_len[8192];
int  jtk_line   [8192];
int  jtk_count;

/* JS AST nodes — parallel arrays. Each node carries up to four int
 * slots; per-kind layout is documented in js_parse.cc / js_interp.cc. */
int  jn_kind   [8192];
int  jn_a      [8192];
int  jn_b      [8192];
int  jn_c      [8192];
int  jn_d      [8192];
int  jn_next   [8192];      /* sibling link inside a list (block stmts, args, params) */
int  jn_count;

/* JS string pool — interns identifiers, string literals, AST string
 * fields. Reset per page. */
char js_str_pool[65536];
int  js_str_pool_pos;

/* Queue of <script> source ranges to run after parse + render-tree
 * build; entries are (attr_pool offset, length). */
int  js_script_off[32];
int  js_script_len[32];
int  js_script_count;

/* Last error message produced by the JS engine; non-empty if a parse
 * or eval error occurred. */
char js_last_error[256];

/* §7 JS interpreter state. The value stack holds intermediate
 * expression results in parallel arrays so each eval pushes a fully
 * tagged value without struct return.  Scope frames + bindings form
 * the lexical environment; lookups walk the parent chain. */
int    jvs_tag    [1024];
double jvs_num    [1024];
int    jvs_str_off[1024];
int    jvs_str_len[1024];
int    jvs_obj_idx[1024];
int    jvs_dom_idx[1024];
int    jvs_native_id[1024];
int    jvs_top;

int    jb_name_off[1024];
int    jb_name_len[1024];
int    jb_tag     [1024];
double jb_num     [1024];
int    jb_str_off [1024];
int    jb_str_len [1024];
int    jb_obj_idx [1024];
int    jb_dom_idx [1024];
int    jb_count;

int    jsc_parent[256];
int    jsc_first [256];     /* first binding index covered by this frame */
int    jsc_count [256];
int    jsc_top;             /* number of allocated frames */
int    jsc_cur;             /* index of the active frame */

/* Statement result codes propagated up the eval tree. */
int    js_ctrl_signal;      /* 0 normal, 1 break, 2 continue, 3 return */

/* Implicit `this` for the currently-being-prepared method call. Set by
 * js_eval_call when the callee comes from MEMBER on a DOMNODE/object.
 * Native function bodies read these instead of an explicit receiver
 * argument. */
int    jsd_this_tag;
int    jsd_this_dom_idx;
int    jsd_this_obj_idx;

/* Set true once dom_dirty becomes 1 in this page; parser.cc reflows
 * the document after queued scripts run. */
int    dom_dirty;

/* §7 JS function records. A function value carries an int handle
 * into these parallel arrays. captured_scope is the scope frame
 * active when the function was defined (closure). */
int    jfn_param_first    [256];
int    jfn_body           [256];
int    jfn_captured_scope [256];
int    jfn_native_id      [256];   /* -1 for user functions, >=0 native */
int    jfn_count;

/* §7 JS object pool. Plain objects and arrays share storage; arrays
 * carry an extra arr_len. Properties hang off jobj_first_prop[] as a
 * singly-linked list through jp_next[]. */
int    jobj_kind     [512];        /* 0=plain, 1=array */
int    jobj_first_prop[512];
int    jobj_arr_len  [512];
int    jobj_count;

int    jp_key_off [4096];
int    jp_key_len [4096];
int    jp_tag     [4096];
double jp_num     [4096];
int    jp_str_off [4096];
int    jp_str_len [4096];
int    jp_obj_idx [4096];
int    jp_dom_idx [4096];
int    jp_next    [4096];
int    jp_count;

/* history */
char hist_url_pool[16384];
int  hist_count;
int  hist_pos;        /* 1-based index of current entry; 0 = empty */
int  nav_no_push;     /* when 1, navigate() skips the push (used by back/fwd) */

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
    hist_pos = 0;
    nav_no_push = 0;
    scroll_y = 0;
    doc_h = 0;
    page_bg = 0xFFFFFF;
    page_fg = 0x000000;
    doc_bg_suppress_body = 0;
    focus_mode = FOCUS_PAGE;
    focused_input = -1;
    prev_buttons = 0;
    hover_link = -1;
    hover_dom_node = -1;
    prev_hover_dom_node = -1;
    css_has_dynamic_pseudo = 0;
    css_not_count = 0;
    jtk_count = 0;
    jn_count = 0;
    js_str_pool_pos = 0;
    js_script_count = 0;
    js_last_error[0] = 0;
    jvs_top = 0;
    jb_count = 0;
    jsc_top = 0;
    jsc_cur = -1;
    js_ctrl_signal = 0;
    jfn_count = 0;
    jobj_count = 0;
    jp_count = 0;
    jsd_this_tag = JS_VAL_UNDEF;
    jsd_this_dom_idx = -1;
    jsd_this_obj_idx = -1;
    dom_dirty = 0;

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

        /* §2.x Webfont async pump (FOUT). After each frame pump one
         * pending @font-face slot; if any slot transitioned to LOADED,
         * line widths shift so re-cascade + rebuild RT + relayout, then
         * the next iteration paints with the new face. Reference:
         * blink/Source/core/css/CSSFontSelector.cpp::fontFaceInvalidated. */
        font_face_pump();
        if (font_face_any_state_changed()) {
            populate_sibling_caches();
            style_resolve_all();
            build_render_tree();
            run_layout();
            clamp_scroll();
            font_face_state_clear();
        }

        yield();
    }
}
