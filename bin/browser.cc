//help: Web browser. Renders HTTP and HTTPS pages in a window.
//help: Usage: browser [url]
//help:   Address bar: Ctrl-L to focus, Enter to go.
//help:   Backspace (page focus): back history.
//help:   Arrow keys / mouse wheel: scroll.
//help:   Click link to navigate.

#include "browser/util.cc"
#include "browser/url.cc"
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

/* ---------- HTTP fetch ---------- */

int build_request(char *buf, char *method, char *p, char *h) {
    int q = 0;
    q = b_append(buf, q, method);
    buf[q] = ' '; q = q + 1;
    q = b_append(buf, q, p);
    q = b_append(buf, q, " HTTP/1.0\r\nHost: ");
    q = b_append(buf, q, h);
    q = b_append(buf, q, "\r\nUser-Agent: cupidos-browser/1.0\r\n");
    q = b_append(buf, q, "Accept: text/html,*/*\r\nConnection: close\r\n\r\n");
    return q;
}

/* Fetch into page_buf. Sets page_len. Returns 0 on success, -1 on fail. */
int fetch_url(char *url, char *content_type_out) {
    int redirects = 0;
    page_len = 0;
    content_type_out[0] = 0;
    char work_url[1024];
    b_strcpy_n(work_url, url, URL_MAX);

    while (1) {
        int rc = parse_url(work_url, cur_host, &cur_port, cur_path, &cur_is_https);
        if (rc != 0) {
            b_strcpy_n(status_msg, "bad URL", 256);
            return -1;
        }

        U32 ip = 0;
        if (dns_resolve(cur_host, &ip) != 0) {
            b_strcpy_n(status_msg, "DNS lookup failed: ", 256);
            int sl = b_strlen(status_msg);
            b_strcpy_n(status_msg + sl, cur_host, 256 - sl);
            return -1;
        }

        int fd = socket(SOCK_TCP);
        if (fd < 0) {
            b_strcpy_n(status_msg, "socket() failed", 256);
            return -1;
        }
        if (connect(fd, ip, htons(cur_port)) != 0) {
            b_strcpy_n(status_msg, "connect failed", 256);
            close(fd);
            return -1;
        }
        if (cur_is_https) {
            int hl = 0;
            while (cur_host[hl]) hl = hl + 1;
            if (setsockopt(fd, SOL_TLS, TLS_ENABLE, cur_host, hl + 1) != 0) {
                b_strcpy_n(status_msg, "TLS handshake failed", 256);
                close(fd);
                return -1;
            }
        }

        char req[4096];
        int rlen = build_request(req, "GET", cur_path, cur_host);
        if (send(fd, req, rlen) < 0) {
            b_strcpy_n(status_msg, "send failed", 256);
            close(fd);
            return -1;
        }

        char buf[4096];
        int hdr_state = 0;
        int in_body = 0;
        int status = 0;
        int sl_state = 0;
        int sl_digits = 0;
        char location[1024]; location[0] = 0;
        int loc_len = 0;
        char line[1024]; int line_len = 0;
        page_len = 0;

        while (1) {
            int n = recv(fd, buf, RECV_BUF_SIZE);
            if (n <= 0) break;
            int j = 0;
            if (!in_body) {
                while (j < n) {
                    char b = buf[j];

                    if (sl_state == 0) {
                        if (b == ' ') sl_state = 1;
                    } else if (sl_state == 1) {
                        if (b >= '0' && b <= '9') {
                            sl_digits = sl_digits * 10 + (b - '0');
                        } else {
                            status = sl_digits;
                            sl_state = 2;
                        }
                    }

                    if (b == '\n') {
                        if (line_len > 9 && b_strieq_n(line, "location:", 9)) {
                            int li = 9;
                            while (li < line_len &&
                                   (line[li] == ' ' || line[li] == '\t')) li = li + 1;
                            int lo = 0;
                            while (li < line_len && line[li] != '\r' &&
                                   lo < URL_MAX - 1) {
                                location[lo] = line[li];
                                lo = lo + 1; li = li + 1;
                            }
                            location[lo] = 0;
                            loc_len = lo;
                        } else if (line_len > 13 &&
                                   b_strieq_n(line, "content-type:", 13)) {
                            int li = 13;
                            while (li < line_len &&
                                   (line[li] == ' ' || line[li] == '\t')) li = li + 1;
                            int lo = 0;
                            while (li < line_len && line[li] != '\r' &&
                                   line[li] != ';' && lo < 127) {
                                content_type_out[lo] = line[li];
                                lo = lo + 1; li = li + 1;
                            }
                            content_type_out[lo] = 0;
                        }
                        line_len = 0;
                    } else if (b != '\r' && line_len < HEADER_LINE_MAX - 1) {
                        line[line_len] = b; line_len = line_len + 1;
                    }

                    if (b == '\r' && (hdr_state == 0 || hdr_state == 2)) hdr_state = hdr_state + 1;
                    else if (b == '\n' && (hdr_state == 1 || hdr_state == 3)) hdr_state = hdr_state + 1;
                    else hdr_state = 0;
                    j = j + 1;
                    if (hdr_state == 4) { in_body = 1; break; }
                }
            }
            int skip_body = (status >= 300 && status < 400 &&
                             loc_len > 0 && redirects < 5);
            if (in_body && j < n && !skip_body) {
                int blen = n - j;
                int can = PAGE_BUF_SIZE - 1 - page_len;
                if (blen > can) blen = can;
                if (blen > 0) {
                    int k = 0;
                    while (k < blen) {
                        page_buf[page_len + k] = buf[j + k];
                        k = k + 1;
                    }
                    page_len = page_len + blen;
                }
            }
        }
        close(fd);
        page_buf[page_len] = 0;

        if (status >= 300 && status < 400 && loc_len > 0 && redirects < 5) {
            char new_url[1024];
            if (resolve_redirect(location, cur_host, cur_port, cur_is_https,
                                 new_url, URL_MAX) != 0) {
                b_strcpy_n(status_msg, "bad Location header", 256);
                return -1;
            }
            b_strcpy_n(work_url, new_url, URL_MAX);
            redirects = redirects + 1;
            continue;
        }

        if (status < 200 || status >= 300) {
            b_strcpy_n(status_msg, "HTTP error: ", 256);
            int sl = b_strlen(status_msg);
            b_append_int(status_msg, sl, status);
            status_msg[sl + 4] = 0;
            return -1;
        }
        b_strcpy_n(cur_url, work_url, URL_MAX);
        return 0;
    }
    return -1;
}

/* ---------- Color parser ---------- */

/* hex_digit in browser/url.cc */

int parse_color_named(char *s, int *out) {
    if (b_strieq(s, "black"))   { *out = 0x00000000; return 1; }
    if (b_strieq(s, "white"))   { *out = 0x00FFFFFF; return 1; }
    if (b_strieq(s, "red"))     { *out = 0x00FF0000; return 1; }
    if (b_strieq(s, "green"))   { *out = 0x00008000; return 1; }
    if (b_strieq(s, "blue"))    { *out = 0x000000FF; return 1; }
    if (b_strieq(s, "yellow"))  { *out = 0x00FFFF00; return 1; }
    if (b_strieq(s, "cyan"))    { *out = 0x0000FFFF; return 1; }
    if (b_strieq(s, "magenta")) { *out = 0x00FF00FF; return 1; }
    if (b_strieq(s, "gray"))    { *out = 0x00808080; return 1; }
    if (b_strieq(s, "grey"))    { *out = 0x00808080; return 1; }
    if (b_strieq(s, "orange"))  { *out = 0x00FFA500; return 1; }
    if (b_strieq(s, "purple"))  { *out = 0x00800080; return 1; }
    if (b_strieq(s, "silver"))  { *out = 0x00C0C0C0; return 1; }
    if (b_strieq(s, "navy"))    { *out = 0x00000080; return 1; }
    if (b_strieq(s, "lime"))    { *out = 0x0000FF00; return 1; }
    if (b_strieq(s, "maroon"))  { *out = 0x00800000; return 1; }
    return 0;
}

int parse_color(char *s, int *out) {
    while (s[0] == ' ' || s[0] == '\t') s = s + 1;
    if (s[0] == '#') {
        s = s + 1;
        int n = 0;
        while (hex_digit(s[n]) >= 0) n = n + 1;
        if (n == 6) {
            int r = hex_digit(s[0]) * 16 + hex_digit(s[1]);
            int g = hex_digit(s[2]) * 16 + hex_digit(s[3]);
            int b = hex_digit(s[4]) * 16 + hex_digit(s[5]);
            *out = (r << 16) | (g << 8) | b;
            return 1;
        }
        if (n == 3) {
            int r = hex_digit(s[0]); r = r | (r << 4);
            int g = hex_digit(s[1]); g = g | (g << 4);
            int b = hex_digit(s[2]); b = b | (b << 4);
            *out = (r << 16) | (g << 8) | b;
            return 1;
        }
        return 0;
    }
    return parse_color_named(s, out);
}

/* parse style="color: X; background-color: Y; font-weight: Z" */
void apply_style(char *style, int node_idx) {
    int i = 0;
    char prop[64];
    char val[128];
    while (style[i]) {
        while (style[i] == ' ' || style[i] == ';' || style[i] == '\t') i = i + 1;
        if (!style[i]) break;
        int p = 0;
        while (style[i] && style[i] != ':' && p < 63) {
            prop[p] = style[i]; p = p + 1; i = i + 1;
        }
        prop[p] = 0;
        if (style[i] != ':') break;
        i = i + 1;
        while (style[i] == ' ' || style[i] == '\t') i = i + 1;
        int v = 0;
        while (style[i] && style[i] != ';' && v < 127) {
            val[v] = style[i]; v = v + 1; i = i + 1;
        }
        val[v] = 0;
        /* trim trailing spaces */
        while (v > 0 && (val[v-1] == ' ' || val[v-1] == '\t')) {
            v = v - 1; val[v] = 0;
        }
        if (b_strieq(prop, "color")) {
            int c;
            if (parse_color(val, &c)) n_color[node_idx] = c;
        } else if (b_strieq(prop, "background-color") ||
                   b_strieq(prop, "background")) {
            int c;
            if (parse_color(val, &c)) n_bgcolor[node_idx] = c;
        } else if (b_strieq(prop, "font-weight")) {
            if (b_strieq(val, "bold") || b_strieq(val, "bolder") ||
                b_strieq(val, "700") || b_strieq(val, "800") ||
                b_strieq(val, "900")) {
                /* will be inherited via styling pass */
                n_type[node_idx] = 1; /* repurpose: 1=bold marker for non-input nodes */
            }
        }
    }
}

/* ---------- HTML lex / parse ---------- */

int tag_id(char *name, int len) {
    if (len == 0) return T_OTHER;
    char b[16]; int i = 0;
    while (i < len && i < 15) { b[i] = (char)b_lc(name[i]); i = i + 1; }
    b[i] = 0;
    if (b_streq(b, "html"))   return T_HTML;
    if (b_streq(b, "head"))   return T_HEAD;
    if (b_streq(b, "title"))  return T_TITLE;
    if (b_streq(b, "body"))   return T_BODY;
    if (b_streq(b, "p"))      return T_P;
    if (b_streq(b, "h1"))     return T_H1;
    if (b_streq(b, "h2"))     return T_H2;
    if (b_streq(b, "h3"))     return T_H3;
    if (b_streq(b, "h4"))     return T_H4;
    if (b_streq(b, "h5"))     return T_H5;
    if (b_streq(b, "h6"))     return T_H6;
    if (b_streq(b, "a"))      return T_A;
    if (b_streq(b, "br"))     return T_BR;
    if (b_streq(b, "hr"))     return T_HR;
    if (b_streq(b, "ul"))     return T_UL;
    if (b_streq(b, "ol"))     return T_OL;
    if (b_streq(b, "li"))     return T_LI;
    if (b_streq(b, "pre"))    return T_PRE;
    if (b_streq(b, "code"))   return T_CODE;
    if (b_streq(b, "b"))      return T_B;
    if (b_streq(b, "strong")) return T_STRONG;
    if (b_streq(b, "i"))      return T_I;
    if (b_streq(b, "em"))     return T_EM;
    if (b_streq(b, "span"))   return T_SPAN;
    if (b_streq(b, "div"))    return T_DIV;
    if (b_streq(b, "img"))    return T_IMG;
    if (b_streq(b, "form"))   return T_FORM;
    if (b_streq(b, "input"))  return T_INPUT;
    if (b_streq(b, "button")) return T_BUTTON;
    if (b_streq(b, "font"))   return T_FONT;
    if (b_streq(b, "script")) return T_SCRIPT;
    if (b_streq(b, "style"))  return T_STYLE;
    return T_OTHER;
}

int is_void_tag(int tag) {
    return tag == T_BR || tag == T_HR || tag == T_IMG || tag == T_INPUT;
}

int alloc_node(int tag, int parent) {
    if (nodes_count >= MAX_NODES) return -1;
    int idx = nodes_count;
    nodes_count = nodes_count + 1;
    n_tag[idx]         = tag;
    n_parent[idx]      = parent;
    n_first_child[idx] = -1;
    n_next[idx]        = -1;
    n_text_off[idx]    = -1;
    n_text_len[idx]    = 0;
    n_href[idx]        = -1;
    n_src[idx]         = -1;
    n_color[idx]       = -1;
    n_bgcolor[idx]     = -1;
    n_action[idx]      = -1;
    n_name[idx]        = -1;
    n_value[idx]       = -1;
    n_type[idx]        = -1;
    n_form_idx[idx]    = -1;
    if (parent >= 0) {
        if (n_first_child[parent] == -1) {
            n_first_child[parent] = idx;
        } else {
            int p = n_first_child[parent];
            while (n_next[p] != -1) p = n_next[p];
            n_next[p] = idx;
        }
    }
    return idx;
}

/* Copy a string into attr_pool. Returns offset, -1 if pool exhausted. */
int attr_intern(char *s, int len) {
    if (attr_pool_pos + len + 1 >= ATTR_POOL_SIZE) return -1;
    int off = attr_pool_pos;
    int i = 0;
    while (i < len) { attr_pool[off + i] = s[i]; i = i + 1; }
    attr_pool[off + len] = 0;
    attr_pool_pos = off + len + 1;
    return off;
}

/* Decode &amp; / &lt; / &gt; / &quot; / &nbsp; / &#NNN; in-place into out;
 * returns new length. */
int decode_entities(char *src, int slen, char *out, int omax) {
    int i = 0;
    int o = 0;
    while (i < slen && o < omax - 1) {
        if (src[i] == '&') {
            int j = i + 1;
            int end = j;
            while (end < slen && src[end] != ';' && end - j < 8) end = end + 1;
            if (end < slen && src[end] == ';') {
                int el = end - j;
                if (el == 3 && b_strieq_n(src + j, "amp", 3)) {
                    out[o] = '&'; o = o + 1; i = end + 1; continue;
                }
                if (el == 2 && b_strieq_n(src + j, "lt", 2)) {
                    out[o] = '<'; o = o + 1; i = end + 1; continue;
                }
                if (el == 2 && b_strieq_n(src + j, "gt", 2)) {
                    out[o] = '>'; o = o + 1; i = end + 1; continue;
                }
                if (el == 4 && b_strieq_n(src + j, "quot", 4)) {
                    out[o] = '"'; o = o + 1; i = end + 1; continue;
                }
                if (el == 4 && b_strieq_n(src + j, "nbsp", 4)) {
                    out[o] = ' '; o = o + 1; i = end + 1; continue;
                }
                if (el == 4 && b_strieq_n(src + j, "apos", 4)) {
                    out[o] = '\''; o = o + 1; i = end + 1; continue;
                }
                if (el >= 2 && src[j] == '#') {
                    int v = 0;
                    int k = j + 1;
                    int hex = 0;
                    if (k < end && (src[k] == 'x' || src[k] == 'X')) {
                        hex = 1; k = k + 1;
                    }
                    while (k < end) {
                        if (hex) {
                            int d = hex_digit(src[k]);
                            if (d < 0) { v = -1; break; }
                            v = v * 16 + d;
                        } else {
                            if (src[k] < '0' || src[k] > '9') { v = -1; break; }
                            v = v * 10 + (src[k] - '0');
                        }
                        k = k + 1;
                    }
                    if (v >= 32 && v < 127) {
                        out[o] = (char)v; o = o + 1; i = end + 1; continue;
                    }
                    if (v == 0xA0) {
                        out[o] = ' '; o = o + 1; i = end + 1; continue;
                    }
                    /* unsupported codepoint: skip */
                    i = end + 1; continue;
                }
            }
            /* unrecognized entity: emit literal & */
            out[o] = '&'; o = o + 1; i = i + 1;
        } else {
            out[o] = src[i]; o = o + 1; i = i + 1;
        }
    }
    out[o] = 0;
    return o;
}

/* Skip until matching close tag like </script> (case-insensitive).
 * Returns new index past close tag. */
int skip_to_close(char *html, int n, int i, char *closetag) {
    int cl = b_strlen(closetag);
    while (i < n - cl - 2) {
        if (html[i] == '<' && html[i+1] == '/' &&
            b_strieq_n(html + i + 2, closetag, cl) &&
            (html[i + 2 + cl] == '>' || html[i + 2 + cl] == ' ')) {
            /* advance past tag */
            while (i < n && html[i] != '>') i = i + 1;
            if (i < n) i = i + 1;
            return i;
        }
        i = i + 1;
    }
    return n;
}

/* Build DOM from page_buf into nodes. */
void parse_html(int html_len) {
    nodes_count = 0;
    attr_pool_pos = 1;  /* offset 0 reserved as "none" */
    forms_count = 0;
    inputs_count = 0;
    title_buf[0] = 0;

    /* synthetic root */
    int root = alloc_node(T_ROOT, -1);

    int stack[64];
    int stack_top = 0;
    stack[0] = root;
    int cur_form = -1;

    int i = 0;
    int n = html_len;
    while (i < n) {
        if (page_buf[i] == '<') {
            /* comment? */
            if (i + 3 < n && page_buf[i+1] == '!' &&
                page_buf[i+2] == '-' && page_buf[i+3] == '-') {
                i = i + 4;
                while (i + 2 < n &&
                       !(page_buf[i] == '-' && page_buf[i+1] == '-' &&
                         page_buf[i+2] == '>')) i = i + 1;
                if (i < n) i = i + 3;
                continue;
            }
            /* doctype? */
            if (i + 1 < n && page_buf[i+1] == '!') {
                while (i < n && page_buf[i] != '>') i = i + 1;
                if (i < n) i = i + 1;
                continue;
            }
            int closing = 0;
            int j = i + 1;
            if (j < n && page_buf[j] == '/') { closing = 1; j = j + 1; }
            int name_start = j;
            while (j < n && page_buf[j] != ' ' && page_buf[j] != '\t' &&
                   page_buf[j] != '>' && page_buf[j] != '/' &&
                   page_buf[j] != '\n' && page_buf[j] != '\r') j = j + 1;
            int name_len = j - name_start;
            int tag = tag_id(page_buf + name_start, name_len);

            /* attributes */
            char href_v[1024]; href_v[0] = 0;
            char src_v[1024]; src_v[0] = 0;
            char color_v[64]; color_v[0] = 0;
            char bg_v[64]; bg_v[0] = 0;
            char action_v[1024]; action_v[0] = 0;
            char method_v[16]; method_v[0] = 0;
            char name_v[64]; name_v[0] = 0;
            char value_v[128]; value_v[0] = 0;
            char type_v[16]; type_v[0] = 0;
            char style_v[256]; style_v[0] = 0;

            int self_close = 0;
            if (!closing) {
                while (j < n && page_buf[j] != '>') {
                    while (j < n && (page_buf[j] == ' ' || page_buf[j] == '\t' ||
                                     page_buf[j] == '\n' || page_buf[j] == '\r')) j = j + 1;
                    if (j >= n || page_buf[j] == '>') break;
                    if (page_buf[j] == '/') { self_close = 1; j = j + 1; continue; }
                    int an_s = j;
                    while (j < n && page_buf[j] != '=' && page_buf[j] != ' ' &&
                           page_buf[j] != '\t' && page_buf[j] != '>' &&
                           page_buf[j] != '/' && page_buf[j] != '\n' &&
                           page_buf[j] != '\r') j = j + 1;
                    int an_l = j - an_s;
                    char av[256]; av[0] = 0;
                    int avl = 0;
                    if (j < n && page_buf[j] == '=') {
                        j = j + 1;
                        char quote = 0;
                        if (j < n && (page_buf[j] == '"' || page_buf[j] == '\'')) {
                            quote = page_buf[j];
                            j = j + 1;
                            int as = j;
                            while (j < n && page_buf[j] != quote) j = j + 1;
                            avl = j - as;
                            if (avl > 255) avl = 255;
                            int k = 0;
                            while (k < avl) { av[k] = page_buf[as + k]; k = k + 1; }
                            av[avl] = 0;
                            if (j < n) j = j + 1;
                        } else {
                            int as = j;
                            while (j < n && page_buf[j] != ' ' && page_buf[j] != '\t' &&
                                   page_buf[j] != '>' && page_buf[j] != '\n' &&
                                   page_buf[j] != '\r') j = j + 1;
                            avl = j - as;
                            if (avl > 255) avl = 255;
                            int k = 0;
                            while (k < avl) { av[k] = page_buf[as + k]; k = k + 1; }
                            av[avl] = 0;
                        }
                    }
                    /* match attribute name */
                    if (b_strieq_n(page_buf + an_s, "href", 4) && an_l == 4) {
                        b_strcpy_n(href_v, av, URL_MAX);
                    } else if (b_strieq_n(page_buf + an_s, "src", 3) && an_l == 3) {
                        b_strcpy_n(src_v, av, URL_MAX);
                    } else if (b_strieq_n(page_buf + an_s, "color", 5) && an_l == 5) {
                        b_strcpy_n(color_v, av, 64);
                    } else if (b_strieq_n(page_buf + an_s, "bgcolor", 7) && an_l == 7) {
                        b_strcpy_n(bg_v, av, 64);
                    } else if (b_strieq_n(page_buf + an_s, "action", 6) && an_l == 6) {
                        b_strcpy_n(action_v, av, URL_MAX);
                    } else if (b_strieq_n(page_buf + an_s, "method", 6) && an_l == 6) {
                        b_strcpy_n(method_v, av, 16);
                    } else if (b_strieq_n(page_buf + an_s, "name", 4) && an_l == 4) {
                        b_strcpy_n(name_v, av, 64);
                    } else if (b_strieq_n(page_buf + an_s, "value", 5) && an_l == 5) {
                        b_strcpy_n(value_v, av, 128);
                    } else if (b_strieq_n(page_buf + an_s, "type", 4) && an_l == 4) {
                        b_strcpy_n(type_v, av, 16);
                    } else if (b_strieq_n(page_buf + an_s, "style", 5) && an_l == 5) {
                        b_strcpy_n(style_v, av, 256);
                    }
                }
                if (j < n) j = j + 1;
            } else {
                while (j < n && page_buf[j] != '>') j = j + 1;
                if (j < n) j = j + 1;
            }
            i = j;

            if (tag == T_SCRIPT || tag == T_STYLE) {
                if (!closing) {
                    char *ct = (tag == T_SCRIPT) ? "script" : "style";
                    i = skip_to_close(page_buf, n, i, ct);
                }
                continue;
            }

            if (closing) {
                /* pop until match found */
                int k = stack_top;
                while (k > 0) {
                    if (n_tag[stack[k]] == tag) {
                        stack_top = k - 1;
                        if (tag == T_FORM) cur_form = -1;
                        break;
                    }
                    k = k - 1;
                }
                continue;
            }

            int parent = stack[stack_top];
            int idx = alloc_node(tag, parent);
            if (idx < 0) continue;

            if (href_v[0])  n_href[idx]   = attr_intern(href_v, b_strlen(href_v));
            if (src_v[0])   n_src[idx]    = attr_intern(src_v, b_strlen(src_v));
            if (color_v[0]) {
                int c;
                if (parse_color(color_v, &c)) n_color[idx] = c;
            }
            if (bg_v[0]) {
                int c;
                if (parse_color(bg_v, &c)) n_bgcolor[idx] = c;
            }
            if (action_v[0]) n_action[idx] = attr_intern(action_v, b_strlen(action_v));
            if (name_v[0])   n_name[idx]   = attr_intern(name_v, b_strlen(name_v));
            if (value_v[0])  n_value[idx]  = attr_intern(value_v, b_strlen(value_v));
            if (type_v[0])   n_type[idx]   = attr_intern(type_v, b_strlen(type_v));
            if (style_v[0])  apply_style(style_v, idx);

            if (tag == T_FORM) {
                if (forms_count < MAX_FORMS) {
                    int fi = forms_count;
                    forms_count = forms_count + 1;
                    form_action[fi] = (action_v[0])
                        ? attr_intern(action_v, b_strlen(action_v)) : -1;
                    cur_form = fi;
                }
            } else if (tag == T_INPUT) {
                /* register text input; ignore submit-only */
                int is_text = 1;
                if (type_v[0] && b_strieq(type_v, "submit")) is_text = 0;
                if (type_v[0] && b_strieq(type_v, "button")) is_text = 0;
                if (type_v[0] && b_strieq(type_v, "hidden")) is_text = 0;
                if (is_text && inputs_count < MAX_INPUTS) {
                    int ii = inputs_count;
                    inputs_count = inputs_count + 1;
                    n_form_idx[idx] = ii;
                    input_name_off[ii] = (name_v[0])
                        ? attr_intern(name_v, b_strlen(name_v)) : -1;
                    input_form[ii] = cur_form;
                    /* default value */
                    int k = 0;
                    while (k < 127 && value_v[k]) {
                        input_value[ii * 128 + k] = value_v[k];
                        k = k + 1;
                    }
                    input_value[ii * 128 + k] = 0;
                } else {
                    n_form_idx[idx] = -2; /* submit/button */
                }
            } else if (tag == T_BUTTON) {
                n_form_idx[idx] = (cur_form >= 0) ? -3 : -1;
            }

            if (!is_void_tag(tag) && !self_close) {
                if (stack_top + 1 < 64) {
                    stack_top = stack_top + 1;
                    stack[stack_top] = idx;
                }
            }
        } else {
            /* text node */
            int ts = i;
            while (i < n && page_buf[i] != '<') i = i + 1;
            int tl = i - ts;
            /* check non-whitespace */
            int has = 0;
            int k = 0;
            while (k < tl) {
                if (page_buf[ts + k] != ' ' && page_buf[ts + k] != '\t' &&
                    page_buf[ts + k] != '\n' && page_buf[ts + k] != '\r') {
                    has = 1; break;
                }
                k = k + 1;
            }
            if (has || (stack_top > 0 && n_tag[stack[stack_top]] == T_PRE)) {
                int parent = stack[stack_top];
                int idx = alloc_node(T_TEXT, parent);
                if (idx >= 0) {
                    /* decode entities into attr_pool */
                    char tmp[2048];
                    int tll = tl;
                    if (tll > 2047) tll = 2047;
                    int dl = decode_entities(page_buf + ts, tll, tmp, 2048);
                    n_text_off[idx] = attr_intern(tmp, dl);
                    n_text_len[idx] = dl;
                    /* if parent is title, save title */
                    if (parent >= 0 && n_tag[parent] == T_TITLE && title_buf[0] == 0) {
                        b_strcpy_n(title_buf, tmp, 256);
                    }
                }
            }
        }
    }
}

/* ---------- Layout ---------- */

int viewport_w() {
    int w = cur_cw - 8 - 12;
    if (w < 100) w = 100;
    return w;
}

int parent_color(int idx, int default_c) {
    while (idx >= 0) {
        if (n_color[idx] >= 0) return n_color[idx];
        idx = n_parent[idx];
    }
    return default_c;
}

int parent_bg(int idx, int default_c) {
    while (idx >= 0) {
        if (n_bgcolor[idx] >= 0) return n_bgcolor[idx];
        idx = n_parent[idx];
    }
    return default_c;
}

int parent_bold(int idx) {
    while (idx >= 0) {
        int t = n_tag[idx];
        if (t == T_B || t == T_STRONG || t == T_H1 || t == T_H2 ||
            t == T_H3 || t == T_H4 || t == T_H5 || t == T_H6) return 1;
        idx = n_parent[idx];
    }
    return 0;
}

int parent_link(int idx) {
    while (idx >= 0) {
        if (n_tag[idx] == T_A && n_href[idx] >= 0) return n_href[idx];
        idx = n_parent[idx];
    }
    return -1;
}

/* Layout state */
int  L_x;
int  L_y;
int  L_line_h;
int  L_max_w;
int  L_left_margin;

void emit_box(int kind) {
    if (boxes_count >= MAX_BOXES) return;
    int b = boxes_count;
    boxes_count = boxes_count + 1;
    b_kind[b] = kind;
    b_x[b] = 0; b_y[b] = 0; b_w[b] = 0; b_h[b] = 0;
    b_fg[b] = page_fg; b_bg[b] = -1;
    b_text_off[b] = -1; b_text_len[b] = 0;
    b_link_idx[b] = -1; b_input_idx[b] = -1; b_img_handle[b] = -1;
    b_bold[b] = 0; b_underline[b] = 0;
}

int last_box() { return boxes_count - 1; }

int register_link(int href_off) {
    if (links_count >= MAX_LINKS) return -1;
    int li = links_count;
    links_count = links_count + 1;
    link_url_off[li] = href_off;
    return li;
}

void newline() {
    L_x = L_left_margin;
    L_y = L_y + L_line_h;
    L_line_h = line_h;
}

void layout_text(int node_idx, int text_off, int len, int link_idx,
                 int bold, int fg, int bg) {
    char *text = attr_pool + text_off;
    int i = 0;
    int in_pre = 0;
    int p = n_parent[node_idx];
    while (p >= 0) {
        if (n_tag[p] == T_PRE || n_tag[p] == T_CODE) { in_pre = 1; break; }
        p = n_parent[p];
    }
    while (i < len) {
        /* skip leading whitespace runs (non-PRE) */
        if (!in_pre) {
            while (i < len && (text[i] == ' ' || text[i] == '\t' ||
                               text[i] == '\n' || text[i] == '\r')) i = i + 1;
            if (i >= len) break;
        }

        /* find next word boundary */
        int ws = i;
        if (in_pre) {
            while (i < len && text[i] != '\n') i = i + 1;
        } else {
            while (i < len && text[i] != ' ' && text[i] != '\t' &&
                   text[i] != '\n' && text[i] != '\r') i = i + 1;
        }
        int wl = i - ws;
        if (wl == 0) {
            if (in_pre && i < len && text[i] == '\n') {
                newline();
                i = i + 1;
            }
            continue;
        }

        int wpx = wl * char_w;
        if (L_x + wpx > L_max_w && L_x > L_left_margin) {
            newline();
        }
        if (boxes_count >= MAX_BOXES) return;
        emit_box(BK_TEXT);
        int bi = last_box();
        b_x[bi] = L_x;
        b_y[bi] = L_y;
        b_w[bi] = wpx;
        b_h[bi] = line_h;
        b_text_off[bi] = text_off + ws;
        b_text_len[bi] = wl;
        b_fg[bi] = fg;
        b_bg[bi] = bg;
        b_link_idx[bi] = link_idx;
        b_bold[bi] = bold;
        b_underline[bi] = (link_idx >= 0) ? 1 : 0;

        L_x = L_x + wpx;
        if (in_pre && i < len && text[i] == '\n') {
            newline();
            i = i + 1;
        } else if (!in_pre && i < len) {
            /* add single space if not at end of line */
            if (L_x + char_w <= L_max_w) {
                L_x = L_x + char_w;
            } else {
                newline();
            }
        }
    }
}

void layout_children(int idx) {
    int c = n_first_child[idx];
    while (c >= 0) {
        layout_node(c);
        c = n_next[c];
    }
}

void layout_node(int idx) {
    int t = n_tag[idx];

    /* Block-level tags break first */
    if (t == T_P || t == T_H1 || t == T_H2 || t == T_H3 || t == T_H4 ||
        t == T_H5 || t == T_H6 || t == T_DIV || t == T_LI || t == T_UL ||
        t == T_OL || t == T_PRE || t == T_HR || t == T_FORM ||
        t == T_BR || t == T_BODY) {
        if (L_x > L_left_margin) newline();
        if (t == T_BR) return;
        if (t == T_HR) {
            emit_box(BK_HRULE);
            int bi = last_box();
            b_x[bi] = L_left_margin;
            b_y[bi] = L_y + 4;
            b_w[bi] = L_max_w - L_left_margin;
            b_h[bi] = 1;
            b_fg[bi] = 0x808080;
            L_y = L_y + 10;
            return;
        }
        if (t == T_LI) {
            /* indent + bullet */
            L_x = L_left_margin + 16;
            emit_box(BK_TEXT);
            int bi = last_box();
            b_x[bi] = L_left_margin + 4;
            b_y[bi] = L_y;
            b_w[bi] = char_w;
            b_h[bi] = line_h;
            b_text_off[bi] = -2;     /* sentinel for bullet glyph */
            b_text_len[bi] = 0;
            b_fg[bi] = parent_color(idx, page_fg);
        }
    }

    /* Specific element handling */
    if (t == T_TEXT) {
        if (n_text_off[idx] >= 0) {
            int link = parent_link(idx);
            int link_idx = -1;
            if (link >= 0) link_idx = register_link(link);
            int bold = parent_bold(idx);
            int fg = parent_color(idx, page_fg);
            int bg = parent_bg(idx, -1);
            layout_text(idx, n_text_off[idx], n_text_len[idx],
                        link_idx, bold, fg, bg);
        }
    } else if (t == T_IMG) {
        if (L_x + 80 > L_max_w) newline();
        emit_box(BK_IMG);
        int bi = last_box();
        b_x[bi] = L_x;
        b_y[bi] = L_y;
        b_w[bi] = 80;
        b_h[bi] = 30;
        b_text_off[bi] = (n_src[idx] >= 0) ? n_src[idx] : -1;
        b_fg[bi] = 0x444444;
        b_bg[bi] = 0xE0E0E0;
        L_x = L_x + 80 + char_w;
    } else if (t == T_INPUT) {
        if (n_form_idx[idx] >= 0) {
            int ii = n_form_idx[idx];
            if (L_x + 200 > L_max_w) newline();
            emit_box(BK_INPUT);
            int bi = last_box();
            b_x[bi] = L_x;
            b_y[bi] = L_y;
            b_w[bi] = 200;
            b_h[bi] = line_h + 4;
            b_input_idx[bi] = ii;
            b_fg[bi] = 0x000000;
            b_bg[bi] = 0xFFFFFF;
            if (L_line_h < b_h[bi]) L_line_h = b_h[bi];
            L_x = L_x + 200 + char_w;
        } else {
            /* submit button */
            char *label = (n_value[idx] >= 0)
                ? attr_pool + n_value[idx] : "Submit";
            int ll = b_strlen(label);
            int bw = ll * char_w + 16;
            if (L_x + bw > L_max_w) newline();
            emit_box(BK_BUTTON);
            int bi = last_box();
            b_x[bi] = L_x;
            b_y[bi] = L_y;
            b_w[bi] = bw;
            b_h[bi] = line_h + 4;
            b_text_off[bi] = (n_value[idx] >= 0) ? n_value[idx] : -3;
            b_text_len[bi] = ll;
            b_fg[bi] = 0x000000;
            b_bg[bi] = 0xC0C0C0;
            b_link_idx[bi] = -1;
            b_input_idx[bi] = -2; /* marker: submit */
            b_bold[bi] = 0;
            if (L_line_h < b_h[bi]) L_line_h = b_h[bi];
            L_x = L_x + bw + char_w;
        }
        return;  /* void */
    } else if (t == T_BUTTON) {
        layout_children(idx);  /* button text */
        return;
    }

    if (t == T_TEXT || t == T_BR || t == T_IMG ||
        t == T_INPUT) return;

    layout_children(idx);

    if (t == T_P || t == T_H1 || t == T_H2 || t == T_H3 || t == T_H4 ||
        t == T_H5 || t == T_H6 || t == T_DIV || t == T_LI || t == T_UL ||
        t == T_OL || t == T_PRE || t == T_FORM || t == T_BODY) {
        if (L_x > L_left_margin) newline();
        if (t == T_H1) L_y = L_y + 8;
        else if (t == T_H2) L_y = L_y + 6;
        else if (t == T_P || t == T_DIV) L_y = L_y + 4;
    }
}

void run_layout() {
    boxes_count = 0;
    links_count = 0;
    L_left_margin = 8;
    L_x = L_left_margin;
    L_y = 4;
    L_line_h = line_h;
    L_max_w = viewport_w();
    page_bg = 0xFFFFFF;
    page_fg = 0x000000;
    /* find body */
    int body = -1;
    int i = 0;
    while (i < nodes_count) {
        if (n_tag[i] == T_BODY) { body = i; break; }
        i = i + 1;
    }
    if (body >= 0 && n_bgcolor[body] >= 0) page_bg = n_bgcolor[body];
    if (body >= 0 && n_color[body] >= 0)   page_fg = n_color[body];

    if (body >= 0) {
        layout_children(body);
    } else {
        /* fallback: layout from root */
        layout_children(0);
    }
    if (L_x > L_left_margin) L_y = L_y + L_line_h;
    doc_h = L_y + 8;
}

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
