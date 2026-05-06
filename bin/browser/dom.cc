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

/* Returns offset into attr_pool (where strings live) or -1 if attribute absent.
 * `name` is a NUL-terminated literal like "href"; comparison is case-insensitive. */
int dom_attr_get(int node, char *name) {
    if (node < 0 || node >= nodes_count) return -1;
    int first = dom_attrs_first[node];
    int count = dom_attrs_count[node];
    int name_len = b_strlen(name);
    for (int k = 0; k < count; k++) {
        int n_off = dom_ap_name_off[first + k];
        if (n_off < 0) continue;
        char *aname = attr_pool + n_off;
        if (b_strieq_n(aname, name, name_len) && aname[name_len] == 0) {
            return dom_ap_value_off[first + k];
        }
    }
    return -1;
}

/* Like dom_attr_get but returns the string pointer, or NULL if absent. */
char *dom_attr_str(int node, char *name) {
    int off = dom_attr_get(node, name);
    if (off < 0) return 0;
    return attr_pool + off;
}

/* alloc_node: allocate a DOM node of `tag`, attach as last child of `parent`,
 * and copy any attrs from the tokenizer's scratch (ap_*[]) into the permanent
 * dom_ap_*[] pool. tok_idx == -1 means "no attrs" (used for synthetic root and
 * for text nodes which pass attrs separately via n_text_off/n_text_len). */
int alloc_node(int tag, int parent, int tok_idx) {
    if (nodes_count >= MAX_NODES) return -1;
    int idx = nodes_count;
    nodes_count = nodes_count + 1;
    n_tag[idx]         = tag;
    n_parent[idx]      = parent;
    n_first_child[idx] = -1;
    n_next[idx]        = -1;
    n_text_off[idx]    = -1;
    n_text_len[idx]    = 0;
    dom_attrs_first[idx] = 0;
    dom_attrs_count[idx] = 0;
    dom_class_off[idx] = -1;
    dom_id_off[idx]    = -1;

    if (tok_idx >= 0) {
        int t_first = tok_attr_first[tok_idx];
        int t_count = tok_attr_count[tok_idx];
        if (t_count > 0 && dom_ap_count + t_count <= MAX_ATTR_PAIRS) {
            dom_attrs_first[idx] = dom_ap_count;
            dom_attrs_count[idx] = t_count;
            for (int k = 0; k < t_count; k++) {
                int n_off = ap_name_off[t_first + k];
                int v_off = ap_value_off[t_first + k];
                dom_ap_name_off[dom_ap_count]  = n_off;
                dom_ap_value_off[dom_ap_count] = v_off;
                dom_ap_count = dom_ap_count + 1;
                if (n_off >= 0) {
                    char *aname = attr_pool + n_off;
                    if (b_strieq(aname, "class")) {
                        dom_class_off[idx] = v_off;
                    } else if (b_strieq(aname, "id")) {
                        dom_id_off[idx] = v_off;
                    }
                }
            }
        }
    }

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

/* Decode &amp; / &lt; / &gt; / &quot; / &nbsp; / &#NNN; / &#xHH; and the
 * extended §1 named-entity set into out; returns new length. The mapping is
 * lossy by design (no Unicode in the 8x8 ASCII font) but predictable. */
int decode_entities(char *src, int slen, char *out, int omax) {
    static char *ent_names[] = {
        "amp",   "lt",    "gt",    "quot",  "apos",  "nbsp",
        "mdash", "ndash", "hellip","lsquo", "rsquo", "ldquo", "rdquo",
        "middot","bull",  "copy",  "reg",   "trade", "sect",  "para",
        "deg",   "times", "divide","plusmn","frac12","frac14","frac34",
        "larr",  "rarr",  "uarr",  "darr",  "laquo", "raquo",
        "iexcl", "iquest","cent",  "pound", "yen",   "euro",
        0
    };
    static int ent_chars[] = {
        '&',     '<',     '>',     '"',     '\'',    ' ',
        '-',     '-',     '.',     '\'',    '\'',    '"',     '"',
        '*',     '*',     'C',     'R',     'T',     'S',     'P',
        'd',     'x',     '/',     '+',     'h',     'q',     'Q',
        '<',     '>',     '^',     'v',     '<',     '>',
        '!',     '?',     'c',     'L',     'Y',     'E'
    };

    int i = 0;
    int o = 0;
    while (i < slen && o < omax - 1) {
        if (src[i] == '&') {
            int j = i + 1;
            int end = j;
            while (end < slen && src[end] != ';' && end - j < 8) end = end + 1;
            if (end < slen && src[end] == ';') {
                int el = end - j;
                /* numeric */
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
                    if (v == 0xA0) {            /* nbsp */
                        out[o] = ' '; o = o + 1; i = end + 1; continue;
                    }
                    if (v == 0xAD) {            /* soft hyphen — drop */
                        i = end + 1; continue;
                    }
                    /* unsupported codepoint: ASCII placeholder */
                    out[o] = '?'; o = o + 1; i = end + 1; continue;
                }
                /* named */
                int matched = 0;
                int idx = 0;
                while (ent_names[idx]) {
                    char *en = ent_names[idx];
                    int enl = b_strlen(en);
                    if (enl == el && b_strieq_n(src + j, en, enl)) {
                        int ch = ent_chars[idx];
                        if (o < omax - 1) {
                            out[o] = (char)ch; o = o + 1;
                        }
                        matched = 1;
                        break;
                    }
                    idx = idx + 1;
                }
                if (matched) { i = end + 1; continue; }
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
