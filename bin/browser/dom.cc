/* ---------- DOM nodes + attribute pool + color + entities ---------- */

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
