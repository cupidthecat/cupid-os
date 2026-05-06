//help: Web browser. Renders HTTP and HTTPS pages in a window.
//help: Usage: browser [url]
//help:   Address bar: Ctrl-L to focus, Enter to go.
//help:   Backspace (page focus): back history.
//help:   Arrow keys / mouse wheel: scroll.
//help:   Click link to navigate.

#include "browser/util.cc"
#include "browser/url.cc"
#include "browser/net.cc"
#include "browser/dom.cc"
#include "browser/parser.cc"
#include "browser/layout.cc"
#include "browser/_smoke.cc"

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

    PAGE_BUF_SIZE   = 96000,
    HEADER_LINE_MAX = 1024,
    REQ_MAX         = 4096,
    RECV_BUF_SIZE   = 4096,

    MAX_NODES = 1024,
    MAX_BOXES = 2048,
    MAX_LINKS = 256,
    MAX_INPUTS = 16,
    MAX_FORMS  = 8,
    HIST_MAX   = 8,

    ATTR_POOL_SIZE = 16384,

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

char page_buf[96000];
int  page_len;

char ctype_buf[128];

/* DOM nodes (parallel arrays) */
int nodes_count;
int n_tag       [1024];
int n_parent    [1024];
int n_first_child[1024];
int n_next      [1024];
int n_text_off  [1024];   /* into page_buf for TEXT nodes */
int n_text_len  [1024];
int n_href      [1024];   /* attr_pool offset, -1 = none */
int n_src       [1024];
int n_color     [1024];   /* 0xAARRGGBB or -1 */
int n_bgcolor   [1024];
int n_action    [1024];
int n_name      [1024];
int n_value     [1024];
int n_type      [1024];
int n_form_idx  [1024];   /* for input/button: parent form idx */

char attr_pool[16384];
int  attr_pool_pos;

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
int  link_url_off[256];   /* into attr_pool */

/* form inputs */
int  inputs_count;
char input_value[2048];   /* parallel: 128 bytes per input */
int  input_name_off[16];
int  input_form[16];

/* form info */
int  forms_count;
int  form_action[8];     /* attr_pool offset of action url */

/* doc */
int  doc_h;
int  scroll_y;
int  page_bg;
int  page_fg;

/* history */
char hist_url_pool[8192];
int  hist_count;

/* state */
int  focus_mode;     /* 0=page, 1=addr, 2=input */
int  focused_input;  /* idx into inputs */
int  prev_buttons;
int  hover_link;     /* -1 or link idx */

/* current content area dimensions (excluding window chrome) */
int  cur_cw;
int  cur_ch;

/* utility helpers in browser/util.cc */

/* URL parsing in browser/url.cc */

/* HTTP fetch in browser/net.cc */

/* color + style helpers in browser/dom.cc */

/* HTML lex / parse in browser/parser.cc */

/* layout in browser/layout.cc */

/* ---------- Render ---------- */

int viewport_x() { return 0; }
int viewport_y() { return ADDR_H + 1; }
int viewport_h() {
    int h = cur_ch - ADDR_H - STATUS_H - 2;
    if (h < 60) h = 60;
    return h;
}

void draw_text_box(int bi, int sx, int sy) {
    int bx = b_x[bi];
    int by = b_y[bi] - scroll_y;
    int bw = b_w[bi];
    int bh = b_h[bi];
    if (by + bh < 0 || by >= viewport_h()) return;

    if (b_bg[bi] >= 0) {
        gfx2d_rect_fill(sx + bx, sy + by, bw, bh, b_bg[bi]);
    }
    if (b_text_off[bi] == -2) {
        /* bullet */
        gfx2d_circle_fill(sx + bx + 3, sy + by + line_h / 2, 2, b_fg[bi]);
        return;
    }
    if (b_text_off[bi] >= 0 && b_text_len[bi] > 0) {
        char tmp[256];
        int ml = b_text_len[bi];
        if (ml > 255) ml = 255;
        int k = 0;
        char *src = attr_pool + b_text_off[bi];
        while (k < ml) { tmp[k] = src[k]; k = k + 1; }
        tmp[ml] = 0;
        gfx2d_text(sx + bx, sy + by, tmp, b_fg[bi], 0);
        if (b_bold[bi]) {
            gfx2d_text(sx + bx + 1, sy + by, tmp, b_fg[bi], 0);
        }
        if (b_underline[bi]) {
            gfx2d_hline(sx + bx, sy + by + line_h - 1, bw, b_fg[bi]);
        }
    }
}

void draw_input_box(int bi, int sx, int sy) {
    int bx = b_x[bi]; int by = b_y[bi] - scroll_y;
    int bw = b_w[bi]; int bh = b_h[bi];
    if (by + bh < 0 || by >= viewport_h()) return;
    gfx2d_rect_fill(sx + bx, sy + by, bw, bh, b_bg[bi]);
    gfx2d_rect(sx + bx, sy + by, bw, bh, 0x808080);
    int ii = b_input_idx[bi];
    if (ii >= 0 && ii < MAX_INPUTS) {
        char *v = input_value + ii * 128;
        int max_chars = (bw - 4) / char_w;
        char tmp[128];
        int kl = 0;
        while (v[kl] && kl < max_chars && kl < 127) {
            tmp[kl] = v[kl]; kl = kl + 1;
        }
        tmp[kl] = 0;
        gfx2d_text(sx + bx + 2, sy + by + 2, tmp, b_fg[bi], 0);
        if (focus_mode == FOCUS_INPUT && focused_input == ii) {
            int cx = sx + bx + 2 + kl * char_w;
            gfx2d_vline(cx, sy + by + 2, line_h, b_fg[bi]);
        }
    }
}

void draw_button_box(int bi, int sx, int sy) {
    int bx = b_x[bi]; int by = b_y[bi] - scroll_y;
    int bw = b_w[bi]; int bh = b_h[bi];
    if (by + bh < 0 || by >= viewport_h()) return;
    gfx2d_rect_fill(sx + bx, sy + by, bw, bh, b_bg[bi]);
    gfx2d_rect(sx + bx, sy + by, bw, bh, 0x404040);
    if (b_text_off[bi] >= 0 && b_text_len[bi] > 0) {
        char tmp[64];
        int ml = b_text_len[bi];
        if (ml > 63) ml = 63;
        int k = 0;
        char *src = attr_pool + b_text_off[bi];
        while (k < ml) { tmp[k] = src[k]; k = k + 1; }
        tmp[ml] = 0;
        int tx = sx + bx + (bw - ml * char_w) / 2;
        gfx2d_text(tx, sy + by + 3, tmp, b_fg[bi], 0);
    } else {
        gfx2d_text(sx + bx + 4, sy + by + 3, "Submit", b_fg[bi], 0);
    }
}

void draw_image_box(int bi, int sx, int sy) {
    int bx = b_x[bi]; int by = b_y[bi] - scroll_y;
    int bw = b_w[bi]; int bh = b_h[bi];
    if (by + bh < 0 || by >= viewport_h()) return;
    if (b_img_handle[bi] >= 0) {
        gfx2d_image_draw_scaled(b_img_handle[bi], sx + bx, sy + by, bw, bh);
    } else {
        gfx2d_rect_fill(sx + bx, sy + by, bw, bh, b_bg[bi]);
        gfx2d_rect(sx + bx, sy + by, bw, bh, b_fg[bi]);
        gfx2d_text(sx + bx + 4, sy + by + 4, "[img]", b_fg[bi], 0);
    }
}

void draw_address_bar(int sx, int sy, int sw) {
    int bg = (focus_mode == FOCUS_ADDR) ? 0xFFFFE0 : 0xF0F0F0;
    gfx2d_rect_fill(sx, sy, sw, ADDR_H, bg);
    gfx2d_hline(sx, sy + ADDR_H - 1, sw, 0x808080);
    gfx2d_text(sx + 4, sy + 6, "URL:", 0x404040, 0);
    char tmp[128];
    int ml = addr_len;
    if (ml > 127) ml = 127;
    int k = 0;
    while (k < ml) { tmp[k] = addr_buf[k]; k = k + 1; }
    tmp[ml] = 0;
    gfx2d_text(sx + 4 + 4 * char_w + 4, sy + 6, tmp, 0x000000, 0);
    if (focus_mode == FOCUS_ADDR) {
        int cx = sx + 4 + 4 * char_w + 4 + addr_cursor * char_w;
        gfx2d_vline(cx, sy + 4, ADDR_H - 8, 0x000000);
    }
}

void draw_status_bar(int sx, int sy, int sw) {
    gfx2d_rect_fill(sx, sy, sw, STATUS_H, 0xE0E0E0);
    gfx2d_hline(sx, sy, sw, 0x808080);
    char *m = status_msg;
    if (hover_link >= 0 && hover_link < links_count) {
        m = attr_pool + link_url_off[hover_link];
    }
    char tmp[80];
    int k = 0;
    while (m[k] && k < 79) { tmp[k] = m[k]; k = k + 1; }
    tmp[k] = 0;
    gfx2d_text(sx + 4, sy + 4, tmp, 0x202020, 0);
}

void draw_scrollbar(int sx, int sy) {
    int sb_x = sx + cur_cw - 12;
    int sb_y = sy;
    int sb_h = viewport_h();
    gfx2d_rect_fill(sb_x, sb_y, 12, sb_h, 0xE0E0E0);
    if (doc_h <= sb_h) return;
    int thumb_h = (sb_h * sb_h) / doc_h;
    if (thumb_h < 16) thumb_h = 16;
    int max_scroll = doc_h - sb_h;
    int thumb_y = sb_y + ((sb_h - thumb_h) * scroll_y) / max_scroll;
    gfx2d_rect_fill(sb_x + 2, thumb_y, 8, thumb_h, 0x808080);
}

void render() {
    if (gui_win_begin_paint(win) != 0) return;
    /* Drawing inside begin_paint targets the window's offscreen surface
     * which has its own (0,0) origin; do NOT use gui_win_content_x/y
     * here.  Mouse handlers translate screen coords back to surface coords
     * separately. */
    int cx = 0;
    int cy = 0;

    /* background */
    gfx2d_rect_fill(cx, cy, cur_cw, cur_ch, page_bg);

    /* address bar */
    draw_address_bar(cx, cy, cur_cw);

    /* viewport (clipped) */
    int vx = cx + viewport_x();
    int vy = cy + viewport_y();
    gfx2d_clip_set(vx, vy, cur_cw - 12, viewport_h());

    int i = 0;
    while (i < boxes_count) {
        int kind = b_kind[i];
        if (kind == BK_TEXT) draw_text_box(i, vx, vy);
        else if (kind == BK_INPUT) draw_input_box(i, vx, vy);
        else if (kind == BK_BUTTON) draw_button_box(i, vx, vy);
        else if (kind == BK_IMG) draw_image_box(i, vx, vy);
        else if (kind == BK_RECT) {
            int by = b_y[i] - scroll_y;
            if (by + b_h[i] >= 0 && by < viewport_h())
                gfx2d_rect_fill(vx + b_x[i], vy + by, b_w[i], b_h[i], b_fg[i]);
        } else if (kind == BK_HRULE) {
            int by = b_y[i] - scroll_y;
            if (by >= 0 && by < viewport_h())
                gfx2d_hline(vx + b_x[i], vy + by, b_w[i], b_fg[i]);
        }
        i = i + 1;
    }

    gfx2d_clip_clear();

    /* scrollbar */
    draw_scrollbar(cx, cy + viewport_y());

    /* status bar */
    draw_status_bar(cx, cy + cur_ch - STATUS_H, cur_cw);

    gui_win_end_paint(win);
    gui_win_present(win);
}

/* ---------- URL resolution against current page ---------- */

void compute_url_relative(char *rel, char *out, int max) {
    if (b_strieq_n(rel, "http://", 7) || b_strieq_n(rel, "https://", 8)) {
        b_strcpy_n(out, rel, max);
        return;
    }
    /* Build prefix: scheme://host[:port] */
    int p = 0;
    char *prefix = cur_is_https ? "https://" : "http://";
    int i = 0;
    while (prefix[i] && p < max - 1) { out[p] = prefix[i]; p = p + 1; i = i + 1; }
    i = 0;
    while (cur_host[i] && p < max - 1) { out[p] = cur_host[i]; p = p + 1; i = i + 1; }
    int default_port = cur_is_https ? 443 : 80;
    if (cur_port != default_port && p < max - 7) {
        out[p] = ':'; p = p + 1;
        char num[8]; int n = 0; int v = cur_port;
        while (v > 0) { num[n] = '0' + (v % 10); n = n + 1; v = v / 10; }
        while (n > 0 && p < max - 1) { n = n - 1; out[p] = num[n]; p = p + 1; }
    }
    if (rel[0] == '/') {
        i = 0;
        while (rel[i] && p < max - 1) { out[p] = rel[i]; p = p + 1; i = i + 1; }
        out[p] = 0;
        return;
    }
    /* relative: append cur_path's directory + rel */
    int last_slash = -1;
    i = 0;
    while (cur_path[i]) {
        if (cur_path[i] == '/') last_slash = i;
        i = i + 1;
    }
    int j = 0;
    while (j <= last_slash && p < max - 1) {
        out[p] = cur_path[j]; p = p + 1; j = j + 1;
    }
    i = 0;
    while (rel[i] && p < max - 1) { out[p] = rel[i]; p = p + 1; i = i + 1; }
    out[p] = 0;
}

/* ---------- Navigation ---------- */

void navigate(char *u) {
    b_strcpy_n(status_msg, "Loading: ", 256);
    int sl = b_strlen(status_msg);
    b_strcpy_n(status_msg + sl, u, 256 - sl);
    render();

    char ct[128]; ct[0] = 0;
    if (fetch_url(u, ct) != 0) {
        nodes_count = 0;
        boxes_count = 0;
        doc_h = 0;
        scroll_y = 0;
        return;
    }
    /* push history */
    if (hist_count < HIST_MAX) {
        b_strcpy_n(hist_url_pool + hist_count * URL_MAX, cur_url, URL_MAX);
        hist_count = hist_count + 1;
    } else {
        int k = 0;
        while (k < HIST_MAX - 1) {
            int j = 0;
            while (j < URL_MAX) {
                hist_url_pool[k * URL_MAX + j] = hist_url_pool[(k + 1) * URL_MAX + j];
                j = j + 1;
            }
            k = k + 1;
        }
        b_strcpy_n(hist_url_pool + (HIST_MAX - 1) * URL_MAX, cur_url, URL_MAX);
    }
    parse_html(page_len);
    run_layout();
    scroll_y = 0;
    if (title_buf[0]) b_strcpy_n(status_msg, title_buf, 256);
    else              b_strcpy_n(status_msg, cur_url, 256);
    /* update address bar */
    b_strcpy_n(addr_buf, cur_url, URL_MAX);
    addr_len = b_strlen(addr_buf);
    addr_cursor = addr_len;
}

void go_back() {
    if (hist_count <= 1) return;
    hist_count = hist_count - 1;
    char prev[1024];
    b_strcpy_n(prev, hist_url_pool + (hist_count - 1) * URL_MAX, URL_MAX);
    /* re-fetch (don't double-push) */
    hist_count = hist_count - 1;
    navigate(prev);
}

/* ---------- Form submit ---------- */

void submit_form(int form_node_idx) {
    int form_idx = -1;
    int i = 0;
    /* find form_idx */
    int seen = 0;
    int n = 0;
    while (n < nodes_count) {
        if (n_tag[n] == T_FORM) {
            if (n == form_node_idx) { form_idx = seen; break; }
            seen = seen + 1;
        }
        n = n + 1;
    }
    if (form_idx < 0 || form_idx >= forms_count) return;

    char base_url[1024];
    if (form_action[form_idx] >= 0) {
        char *act = attr_pool + form_action[form_idx];
        compute_url_relative(act, base_url, URL_MAX);
    } else {
        b_strcpy_n(base_url, cur_url, URL_MAX);
    }

    char query[1024]; int qp = 0;
    int has = 0;
    int ii = 0;
    while (ii < inputs_count) {
        if (input_form[ii] == form_idx && input_name_off[ii] >= 0) {
            char *nm = attr_pool + input_name_off[ii];
            char *vl = input_value + ii * 128;
            if (qp < 1023) {
                query[qp] = has ? '&' : '?'; qp = qp + 1;
            }
            int j = 0;
            while (nm[j] && qp < 1023) {
                query[qp] = nm[j]; qp = qp + 1; j = j + 1;
            }
            if (qp < 1023) { query[qp] = '='; qp = qp + 1; }
            j = 0;
            while (vl[j] && qp < 1023) {
                char c = vl[j];
                if (c == ' ') { query[qp] = '+'; qp = qp + 1; }
                else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                         c == '.' || c == '~') {
                    query[qp] = c; qp = qp + 1;
                } else if (qp + 3 <= 1023) {
                    char hex[16];
                    hex[0] = '0'; hex[1] = '1'; hex[2] = '2'; hex[3] = '3';
                    hex[4] = '4'; hex[5] = '5'; hex[6] = '6'; hex[7] = '7';
                    hex[8] = '8'; hex[9] = '9'; hex[10] = 'A'; hex[11] = 'B';
                    hex[12] = 'C'; hex[13] = 'D'; hex[14] = 'E'; hex[15] = 'F';
                    int u = (int)c & 0xFF;
                    query[qp] = '%'; qp = qp + 1;
                    query[qp] = hex[(u >> 4) & 0xF]; qp = qp + 1;
                    query[qp] = hex[u & 0xF]; qp = qp + 1;
                }
                j = j + 1;
            }
            has = 1;
        }
        ii = ii + 1;
    }
    query[qp] = 0;

    char full[1024];
    int p = 0;
    int j = 0;
    while (base_url[j] && p < URL_MAX - 1 && base_url[j] != '?') {
        full[p] = base_url[j]; p = p + 1; j = j + 1;
    }
    j = 0;
    while (query[j] && p < URL_MAX - 1) {
        full[p] = query[j]; p = p + 1; j = j + 1;
    }
    full[p] = 0;
    navigate(full);
}

/* ---------- Hit testing ---------- */

int hit_box(int mx, int my) {
    /* mx, my relative to viewport */
    int i = 0;
    int doc_y = my + scroll_y;
    while (i < boxes_count) {
        int by = b_y[i];
        int bx = b_x[i];
        if (mx >= bx && mx < bx + b_w[i] &&
            doc_y >= by && doc_y < by + b_h[i]) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

/* find the input/button form parent node */
int find_node_for_input(int ii) {
    int n = 0;
    while (n < nodes_count) {
        if (n_tag[n] == T_INPUT && n_form_idx[n] == ii) return n;
        n = n + 1;
    }
    return -1;
}

int find_form_node(int input_node) {
    int p = n_parent[input_node];
    while (p >= 0) {
        if (n_tag[p] == T_FORM) return p;
        p = n_parent[p];
    }
    return -1;
}

/* ---------- Input ---------- */

void clamp_scroll() {
    int max = doc_h - viewport_h();
    if (max < 0) max = 0;
    if (scroll_y > max) scroll_y = max;
    if (scroll_y < 0) scroll_y = 0;
}

void handle_address_key(int sc, int ch) {
    if (ch == 13 || ch == 10) {
        focus_mode = FOCUS_PAGE;
        navigate(addr_buf);
        return;
    }
    if (ch == 27) { focus_mode = FOCUS_PAGE; return; }
    if (ch == 8) {
        if (addr_cursor > 0) {
            int k = addr_cursor;
            while (k < addr_len) {
                addr_buf[k - 1] = addr_buf[k];
                k = k + 1;
            }
            addr_len = addr_len - 1;
            addr_cursor = addr_cursor - 1;
            addr_buf[addr_len] = 0;
        }
        return;
    }
    if (sc == 75) {  /* left */
        if (addr_cursor > 0) addr_cursor = addr_cursor - 1;
        return;
    }
    if (sc == 77) {  /* right */
        if (addr_cursor < addr_len) addr_cursor = addr_cursor + 1;
        return;
    }
    if (sc == 71) { addr_cursor = 0; return; }       /* home */
    if (sc == 79) { addr_cursor = addr_len; return; }/* end */
    if (ch >= 32 && ch < 127 && addr_len < URL_MAX - 1) {
        int k = addr_len;
        while (k > addr_cursor) {
            addr_buf[k] = addr_buf[k - 1];
            k = k - 1;
        }
        addr_buf[addr_cursor] = (char)ch;
        addr_len = addr_len + 1;
        addr_cursor = addr_cursor + 1;
        addr_buf[addr_len] = 0;
    }
}

void handle_input_key(int sc, int ch) {
    int ii = focused_input;
    if (ii < 0 || ii >= inputs_count) return;
    char *v = input_value + ii * 128;
    int vl = b_strlen(v);
    if (ch == 13 || ch == 10) {
        /* submit form */
        int fi = input_form[ii];
        /* find the form node */
        int n = 0;
        int seen = 0;
        int form_node = -1;
        while (n < nodes_count) {
            if (n_tag[n] == T_FORM) {
                if (seen == fi) { form_node = n; break; }
                seen = seen + 1;
            }
            n = n + 1;
        }
        focus_mode = FOCUS_PAGE;
        if (form_node >= 0) submit_form(form_node);
        return;
    }
    if (ch == 27) { focus_mode = FOCUS_PAGE; return; }
    if (ch == 8) {
        if (vl > 0) v[vl - 1] = 0;
        return;
    }
    if (ch >= 32 && ch < 127 && vl < 126) {
        v[vl] = (char)ch;
        v[vl + 1] = 0;
    }
}

void handle_page_key(int sc, int ch) {
    if (ch == 8) { go_back(); return; }
    /* Ctrl-L = focus address bar (ASCII 12 = ^L) */
    if (ch == 12) {
        focus_mode = FOCUS_ADDR;
        addr_cursor = addr_len;
        return;
    }
    if (sc == 72) { scroll_y = scroll_y - line_h * 2; clamp_scroll(); return; }
    if (sc == 80) { scroll_y = scroll_y + line_h * 2; clamp_scroll(); return; }
    if (sc == 73) { scroll_y = scroll_y - viewport_h() + line_h; clamp_scroll(); return; }
    if (sc == 81) { scroll_y = scroll_y + viewport_h() - line_h; clamp_scroll(); return; }
    if (sc == 71) { scroll_y = 0; return; }
    if (sc == 79) { scroll_y = doc_h; clamp_scroll(); return; }
    if (ch == 27) {
        /* Esc — close the window */
        gui_win_close(win);
    }
}

void handle_keys() {
    int key = gui_win_poll_key(win);
    while (key != -1) {
        int sc = (key >> 8) & 255;
        int ch = key & 255;
        if (focus_mode == FOCUS_ADDR) handle_address_key(sc, ch);
        else if (focus_mode == FOCUS_INPUT) handle_input_key(sc, ch);
        else handle_page_key(sc, ch);
        key = gui_win_poll_key(win);
    }
}

void handle_left_click(int mx, int my) {
    int cx = gui_win_content_x(win);
    int cy = gui_win_content_y(win);
    int rel_x = mx - cx;
    int rel_y = my - cy;
    /* address bar? */
    if (rel_y >= 0 && rel_y < ADDR_H) {
        focus_mode = FOCUS_ADDR;
        addr_cursor = addr_len;
        return;
    }
    /* viewport */
    if (rel_y >= viewport_y() && rel_y < viewport_y() + viewport_h() &&
        rel_x >= 0 && rel_x < cur_cw - 12) {
        int vmx = rel_x;
        int vmy = rel_y - viewport_y();
        int bi = hit_box(vmx, vmy);
        if (bi >= 0) {
            int kind = b_kind[bi];
            if (kind == BK_TEXT && b_link_idx[bi] >= 0) {
                int li = b_link_idx[bi];
                char *u = attr_pool + link_url_off[li];
                char full[1024];
                compute_url_relative(u, full, URL_MAX);
                focus_mode = FOCUS_PAGE;
                navigate(full);
                return;
            }
            if (kind == BK_INPUT) {
                focus_mode = FOCUS_INPUT;
                focused_input = b_input_idx[bi];
                return;
            }
            if (kind == BK_BUTTON && b_input_idx[bi] == -2) {
                /* submit-style button: find form node */
                int n = 0;
                while (n < nodes_count) {
                    if (n_tag[n] == T_INPUT && n_form_idx[n] == -2) {
                        int fn = find_form_node(n);
                        if (fn >= 0) submit_form(fn);
                        return;
                    }
                    n = n + 1;
                }
                return;
            }
        }
        focus_mode = FOCUS_PAGE;
        return;
    }
    /* scrollbar? */
    if (rel_x >= cur_cw - 12 && rel_x < cur_cw &&
        rel_y >= viewport_y() && rel_y < viewport_y() + viewport_h()) {
        int rel = rel_y - viewport_y();
        int frac = (rel * 100) / viewport_h();
        scroll_y = (doc_h - viewport_h()) * frac / 100;
        clamp_scroll();
        return;
    }
}

void handle_hover(int mx, int my) {
    int cx = gui_win_content_x(win);
    int cy = gui_win_content_y(win);
    int rel_x = mx - cx;
    int rel_y = my - cy;
    hover_link = -1;
    if (rel_y >= viewport_y() && rel_y < viewport_y() + viewport_h() &&
        rel_x >= 0 && rel_x < cur_cw - 12) {
        int vmx = rel_x;
        int vmy = rel_y - viewport_y();
        int bi = hit_box(vmx, vmy);
        if (bi >= 0 && b_kind[bi] == BK_TEXT && b_link_idx[bi] >= 0) {
            hover_link = b_link_idx[bi];
        }
    }
}

void handle_mouse() {
    int mx = mouse_x();
    int my = mouse_y();
    int btns = mouse_buttons();
    int left_click = (btns & 1) && !(prev_buttons & 1);
    if (left_click) handle_left_click(mx, my);
    handle_hover(mx, my);

    int dz = mouse_scroll();
    if (dz != 0) {
        scroll_y = scroll_y + dz * line_h * 3;
        clamp_scroll();
    }
    prev_buttons = btns;
}

/* ---------- Main ---------- */

void error_page(char *msg) {
    nodes_count = 0;
    attr_pool_pos = 1;
    boxes_count = 0;
    L_x = 8; L_y = 20;
    L_line_h = line_h;
    L_max_w = viewport_w();
    L_left_margin = 8;
    page_bg = 0xFFE8E8;
    page_fg = 0x000000;
    /* Lay out the message manually */
    emit_box(BK_TEXT);
    int bi = last_box();
    int off = attr_intern(msg, b_strlen(msg));
    b_x[bi] = 8; b_y[bi] = 16;
    b_w[bi] = b_strlen(msg) * char_w;
    b_h[bi] = line_h;
    b_text_off[bi] = off; b_text_len[bi] = b_strlen(msg);
    b_fg[bi] = 0x800000; b_bg[bi] = -1;
    b_link_idx[bi] = -1; b_input_idx[bi] = -1; b_img_handle[bi] = -1;
    b_bold[bi] = 1; b_underline[bi] = 0;
    doc_h = 40;
}

void main() {
    if (browser_smoke_token() != 0xCAFE) {
        println("browser: smoke token mismatch — include broken");
        return;
    }
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
