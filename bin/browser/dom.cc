/* DOM nodes + attribute pool + color + entities */

/* CSS Color Module Level 4 named colors. Matches Blink's
 * platform/graphics/Color::parseHexColor and named-color table
 * (third_party/blink/renderer/platform/graphics/color.cc). Stripped
 * to the most-used subset for browser footprint; ~80 colors covers
 * every name commonly seen on real sites including web-safe palette
 * (red/blue/green), light/dark variants, and design-system favorites
 * (tomato, coral, teal, salmon, ...). */
int parse_color_named(char *s, int *out) {
    /* Trim trailing whitespace / common terminators so the case-insensitive
     * compare hits even when the substituted value has extra spaces. */
    if (b_strieq(s, "transparent")) { *out = 0x00000000; return 1; }
    if (b_strieq(s, "currentcolor")) { *out = 0x00000000; return 1; }

    if (b_strieq(s, "black"))            { *out = 0x000000; return 1; }
    if (b_strieq(s, "silver"))           { *out = 0xC0C0C0; return 1; }
    if (b_strieq(s, "gray"))             { *out = 0x808080; return 1; }
    if (b_strieq(s, "grey"))             { *out = 0x808080; return 1; }
    if (b_strieq(s, "white"))            { *out = 0xFFFFFF; return 1; }
    if (b_strieq(s, "maroon"))           { *out = 0x800000; return 1; }
    if (b_strieq(s, "red"))              { *out = 0xFF0000; return 1; }
    if (b_strieq(s, "purple"))           { *out = 0x800080; return 1; }
    if (b_strieq(s, "fuchsia"))          { *out = 0xFF00FF; return 1; }
    if (b_strieq(s, "magenta"))          { *out = 0xFF00FF; return 1; }
    if (b_strieq(s, "green"))            { *out = 0x008000; return 1; }
    if (b_strieq(s, "lime"))             { *out = 0x00FF00; return 1; }
    if (b_strieq(s, "olive"))            { *out = 0x808000; return 1; }
    if (b_strieq(s, "yellow"))           { *out = 0xFFFF00; return 1; }
    if (b_strieq(s, "navy"))             { *out = 0x000080; return 1; }
    if (b_strieq(s, "blue"))             { *out = 0x0000FF; return 1; }
    if (b_strieq(s, "teal"))             { *out = 0x008080; return 1; }
    if (b_strieq(s, "aqua"))             { *out = 0x00FFFF; return 1; }
    if (b_strieq(s, "cyan"))             { *out = 0x00FFFF; return 1; }
    if (b_strieq(s, "orange"))           { *out = 0xFFA500; return 1; }
    if (b_strieq(s, "aliceblue"))        { *out = 0xF0F8FF; return 1; }
    if (b_strieq(s, "antiquewhite"))     { *out = 0xFAEBD7; return 1; }
    if (b_strieq(s, "aquamarine"))       { *out = 0x7FFFD4; return 1; }
    if (b_strieq(s, "azure"))            { *out = 0xF0FFFF; return 1; }
    if (b_strieq(s, "beige"))            { *out = 0xF5F5DC; return 1; }
    if (b_strieq(s, "bisque"))           { *out = 0xFFE4C4; return 1; }
    if (b_strieq(s, "blanchedalmond"))   { *out = 0xFFEBCD; return 1; }
    if (b_strieq(s, "blueviolet"))       { *out = 0x8A2BE2; return 1; }
    if (b_strieq(s, "brown"))            { *out = 0xA52A2A; return 1; }
    if (b_strieq(s, "burlywood"))        { *out = 0xDEB887; return 1; }
    if (b_strieq(s, "cadetblue"))        { *out = 0x5F9EA0; return 1; }
    if (b_strieq(s, "chartreuse"))       { *out = 0x7FFF00; return 1; }
    if (b_strieq(s, "chocolate"))        { *out = 0xD2691E; return 1; }
    if (b_strieq(s, "coral"))            { *out = 0xFF7F50; return 1; }
    if (b_strieq(s, "cornflowerblue"))   { *out = 0x6495ED; return 1; }
    if (b_strieq(s, "cornsilk"))         { *out = 0xFFF8DC; return 1; }
    if (b_strieq(s, "crimson"))          { *out = 0xDC143C; return 1; }
    if (b_strieq(s, "darkblue"))         { *out = 0x00008B; return 1; }
    if (b_strieq(s, "darkcyan"))         { *out = 0x008B8B; return 1; }
    if (b_strieq(s, "darkgoldenrod"))    { *out = 0xB8860B; return 1; }
    if (b_strieq(s, "darkgray"))         { *out = 0xA9A9A9; return 1; }
    if (b_strieq(s, "darkgrey"))         { *out = 0xA9A9A9; return 1; }
    if (b_strieq(s, "darkgreen"))        { *out = 0x006400; return 1; }
    if (b_strieq(s, "darkkhaki"))        { *out = 0xBDB76B; return 1; }
    if (b_strieq(s, "darkmagenta"))      { *out = 0x8B008B; return 1; }
    if (b_strieq(s, "darkolivegreen"))   { *out = 0x556B2F; return 1; }
    if (b_strieq(s, "darkorange"))       { *out = 0xFF8C00; return 1; }
    if (b_strieq(s, "darkorchid"))       { *out = 0x9932CC; return 1; }
    if (b_strieq(s, "darkred"))          { *out = 0x8B0000; return 1; }
    if (b_strieq(s, "darksalmon"))       { *out = 0xE9967A; return 1; }
    if (b_strieq(s, "darkseagreen"))     { *out = 0x8FBC8F; return 1; }
    if (b_strieq(s, "darkslateblue"))    { *out = 0x483D8B; return 1; }
    if (b_strieq(s, "darkslategray"))    { *out = 0x2F4F4F; return 1; }
    if (b_strieq(s, "darkslategrey"))    { *out = 0x2F4F4F; return 1; }
    if (b_strieq(s, "darkturquoise"))    { *out = 0x00CED1; return 1; }
    if (b_strieq(s, "darkviolet"))       { *out = 0x9400D3; return 1; }
    if (b_strieq(s, "deeppink"))         { *out = 0xFF1493; return 1; }
    if (b_strieq(s, "deepskyblue"))      { *out = 0x00BFFF; return 1; }
    if (b_strieq(s, "dimgray"))          { *out = 0x696969; return 1; }
    if (b_strieq(s, "dimgrey"))          { *out = 0x696969; return 1; }
    if (b_strieq(s, "dodgerblue"))       { *out = 0x1E90FF; return 1; }
    if (b_strieq(s, "firebrick"))        { *out = 0xB22222; return 1; }
    if (b_strieq(s, "floralwhite"))      { *out = 0xFFFAF0; return 1; }
    if (b_strieq(s, "forestgreen"))      { *out = 0x228B22; return 1; }
    if (b_strieq(s, "gainsboro"))        { *out = 0xDCDCDC; return 1; }
    if (b_strieq(s, "ghostwhite"))       { *out = 0xF8F8FF; return 1; }
    if (b_strieq(s, "gold"))             { *out = 0xFFD700; return 1; }
    if (b_strieq(s, "goldenrod"))        { *out = 0xDAA520; return 1; }
    if (b_strieq(s, "greenyellow"))      { *out = 0xADFF2F; return 1; }
    if (b_strieq(s, "honeydew"))         { *out = 0xF0FFF0; return 1; }
    if (b_strieq(s, "hotpink"))          { *out = 0xFF69B4; return 1; }
    if (b_strieq(s, "indianred"))        { *out = 0xCD5C5C; return 1; }
    if (b_strieq(s, "indigo"))           { *out = 0x4B0082; return 1; }
    if (b_strieq(s, "ivory"))            { *out = 0xFFFFF0; return 1; }
    if (b_strieq(s, "khaki"))            { *out = 0xF0E68C; return 1; }
    if (b_strieq(s, "lavender"))         { *out = 0xE6E6FA; return 1; }
    if (b_strieq(s, "lavenderblush"))    { *out = 0xFFF0F5; return 1; }
    if (b_strieq(s, "lawngreen"))        { *out = 0x7CFC00; return 1; }
    if (b_strieq(s, "lemonchiffon"))     { *out = 0xFFFACD; return 1; }
    if (b_strieq(s, "lightblue"))        { *out = 0xADD8E6; return 1; }
    if (b_strieq(s, "lightcoral"))       { *out = 0xF08080; return 1; }
    if (b_strieq(s, "lightcyan"))        { *out = 0xE0FFFF; return 1; }
    if (b_strieq(s, "lightgoldenrodyellow")) { *out = 0xFAFAD2; return 1; }
    if (b_strieq(s, "lightgray"))        { *out = 0xD3D3D3; return 1; }
    if (b_strieq(s, "lightgrey"))        { *out = 0xD3D3D3; return 1; }
    if (b_strieq(s, "lightgreen"))       { *out = 0x90EE90; return 1; }
    if (b_strieq(s, "lightpink"))        { *out = 0xFFB6C1; return 1; }
    if (b_strieq(s, "lightsalmon"))      { *out = 0xFFA07A; return 1; }
    if (b_strieq(s, "lightseagreen"))    { *out = 0x20B2AA; return 1; }
    if (b_strieq(s, "lightskyblue"))     { *out = 0x87CEFA; return 1; }
    if (b_strieq(s, "lightslategray"))   { *out = 0x778899; return 1; }
    if (b_strieq(s, "lightslategrey"))   { *out = 0x778899; return 1; }
    if (b_strieq(s, "lightsteelblue"))   { *out = 0xB0C4DE; return 1; }
    if (b_strieq(s, "lightyellow"))      { *out = 0xFFFFE0; return 1; }
    if (b_strieq(s, "limegreen"))        { *out = 0x32CD32; return 1; }
    if (b_strieq(s, "linen"))            { *out = 0xFAF0E6; return 1; }
    if (b_strieq(s, "mediumaquamarine")) { *out = 0x66CDAA; return 1; }
    if (b_strieq(s, "mediumblue"))       { *out = 0x0000CD; return 1; }
    if (b_strieq(s, "mediumorchid"))     { *out = 0xBA55D3; return 1; }
    if (b_strieq(s, "mediumpurple"))     { *out = 0x9370DB; return 1; }
    if (b_strieq(s, "mediumseagreen"))   { *out = 0x3CB371; return 1; }
    if (b_strieq(s, "mediumslateblue"))  { *out = 0x7B68EE; return 1; }
    if (b_strieq(s, "mediumspringgreen")){ *out = 0x00FA9A; return 1; }
    if (b_strieq(s, "mediumturquoise"))  { *out = 0x48D1CC; return 1; }
    if (b_strieq(s, "mediumvioletred"))  { *out = 0xC71585; return 1; }
    if (b_strieq(s, "midnightblue"))     { *out = 0x191970; return 1; }
    if (b_strieq(s, "mintcream"))        { *out = 0xF5FFFA; return 1; }
    if (b_strieq(s, "mistyrose"))        { *out = 0xFFE4E1; return 1; }
    if (b_strieq(s, "moccasin"))         { *out = 0xFFE4B5; return 1; }
    if (b_strieq(s, "navajowhite"))      { *out = 0xFFDEAD; return 1; }
    if (b_strieq(s, "oldlace"))          { *out = 0xFDF5E6; return 1; }
    if (b_strieq(s, "olivedrab"))        { *out = 0x6B8E23; return 1; }
    if (b_strieq(s, "orangered"))        { *out = 0xFF4500; return 1; }
    if (b_strieq(s, "orchid"))           { *out = 0xDA70D6; return 1; }
    if (b_strieq(s, "palegoldenrod"))    { *out = 0xEEE8AA; return 1; }
    if (b_strieq(s, "palegreen"))        { *out = 0x98FB98; return 1; }
    if (b_strieq(s, "paleturquoise"))    { *out = 0xAFEEEE; return 1; }
    if (b_strieq(s, "palevioletred"))    { *out = 0xDB7093; return 1; }
    if (b_strieq(s, "papayawhip"))       { *out = 0xFFEFD5; return 1; }
    if (b_strieq(s, "peachpuff"))        { *out = 0xFFDAB9; return 1; }
    if (b_strieq(s, "peru"))             { *out = 0xCD853F; return 1; }
    if (b_strieq(s, "pink"))             { *out = 0xFFC0CB; return 1; }
    if (b_strieq(s, "plum"))             { *out = 0xDDA0DD; return 1; }
    if (b_strieq(s, "powderblue"))       { *out = 0xB0E0E6; return 1; }
    if (b_strieq(s, "rebeccapurple"))    { *out = 0x663399; return 1; }
    if (b_strieq(s, "rosybrown"))        { *out = 0xBC8F8F; return 1; }
    if (b_strieq(s, "royalblue"))        { *out = 0x4169E1; return 1; }
    if (b_strieq(s, "saddlebrown"))      { *out = 0x8B4513; return 1; }
    if (b_strieq(s, "salmon"))           { *out = 0xFA8072; return 1; }
    if (b_strieq(s, "sandybrown"))       { *out = 0xF4A460; return 1; }
    if (b_strieq(s, "seagreen"))         { *out = 0x2E8B57; return 1; }
    if (b_strieq(s, "seashell"))         { *out = 0xFFF5EE; return 1; }
    if (b_strieq(s, "sienna"))           { *out = 0xA0522D; return 1; }
    if (b_strieq(s, "skyblue"))          { *out = 0x87CEEB; return 1; }
    if (b_strieq(s, "slateblue"))        { *out = 0x6A5ACD; return 1; }
    if (b_strieq(s, "slategray"))        { *out = 0x708090; return 1; }
    if (b_strieq(s, "slategrey"))        { *out = 0x708090; return 1; }
    if (b_strieq(s, "snow"))             { *out = 0xFFFAFA; return 1; }
    if (b_strieq(s, "springgreen"))      { *out = 0x00FF7F; return 1; }
    if (b_strieq(s, "steelblue"))        { *out = 0x4682B4; return 1; }
    if (b_strieq(s, "tan"))              { *out = 0xD2B48C; return 1; }
    if (b_strieq(s, "thistle"))          { *out = 0xD8BFD8; return 1; }
    if (b_strieq(s, "tomato"))           { *out = 0xFF6347; return 1; }
    if (b_strieq(s, "turquoise"))        { *out = 0x40E0D0; return 1; }
    if (b_strieq(s, "violet"))           { *out = 0xEE82EE; return 1; }
    if (b_strieq(s, "wheat"))            { *out = 0xF5DEB3; return 1; }
    if (b_strieq(s, "whitesmoke"))       { *out = 0xF5F5F5; return 1; }
    if (b_strieq(s, "yellowgreen"))      { *out = 0x9ACD32; return 1; }
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
    /* rgb(r,g,b) and rgba(r,g,b,a). Alpha component is parsed-and-dropped. */
    if ((s[0] == 'r' || s[0] == 'R') &&
        (s[1] == 'g' || s[1] == 'G') &&
        (s[2] == 'b' || s[2] == 'B')) {
        int i = 3;
        if (s[i] == 'a' || s[i] == 'A') i = i + 1;
        while (s[i] == ' ' || s[i] == '\t') i = i + 1;
        if (s[i] != '(') return 0;
        i = i + 1;
        int rgb[3];
        rgb[0] = 0; rgb[1] = 0; rgb[2] = 0;
        for (int k = 0; k < 3; k++) {
            while (s[i] == ' ' || s[i] == '\t' || s[i] == ',') i = i + 1;
            int v = 0;
            int saw_digit = 0;
            while (s[i] >= '0' && s[i] <= '9') {
                v = v * 10 + (s[i] - '0');
                i = i + 1;
                saw_digit = 1;
            }
            if (!saw_digit) return 0;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            rgb[k] = v;
            while (s[i] == ' ' || s[i] == '\t') i = i + 1;
        }
        *out = (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];
        return 1;
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

/* Move `new_node` to become the immediate previous sibling of `before` in
 * the DOM tree. Detaches new_node from its current parent's child list (if
 * any) and splices it in in front of `before`. Used for foster-parenting:
 * stray text inside <table> outside cells must be inserted before the
 * table per HTML spec §13.2.6.5. */
void dom_insert_before(int new_node, int before) {
    if (new_node < 0 || before < 0) return;
    int new_parent = n_parent[before];
    if (new_parent < 0) return;
    /* Detach new_node from its current parent. */
    int op = n_parent[new_node];
    if (op >= 0) {
        if (n_first_child[op] == new_node) {
            n_first_child[op] = n_next[new_node];
        } else {
            int p = n_first_child[op];
            while (p >= 0 && n_next[p] != new_node) p = n_next[p];
            if (p >= 0) n_next[p] = n_next[new_node];
        }
    }
    /* Splice in before `before`. */
    n_parent[new_node] = new_parent;
    n_next[new_node] = before;
    if (n_first_child[new_parent] == before) {
        n_first_child[new_parent] = new_node;
    } else {
        int p = n_first_child[new_parent];
        while (p >= 0 && n_next[p] != before) p = n_next[p];
        if (p >= 0) n_next[p] = new_node;
    }
}

/* Populate per-DOM-node sibling caches consumed by the §2 selector matcher.
 * Called once after parse_html finishes building the DOM and before
 * style_resolve_all runs. Element-only: text nodes (T_TEXT) are skipped so
 * that ":first-child", ":nth-child", "+", "~" behave per CSS spec. */
void populate_sibling_caches() {
    /* Defaults: no parent / no element siblings. */
    for (int i = 0; i < nodes_count; i = i + 1) {
        n_sibling_idx     [i] = 0;
        n_sibling_count   [i] = 0;
        n_prev_sibling_elt[i] = -1;
        n_next_sibling_elt[i] = -1;
    }
    /* For each node that has children, walk once to assign 1-based element
     * indices and prev/next element-sibling links. */
    for (int p = 0; p < nodes_count; p = p + 1) {
        int prev_elt = -1;
        int idx = 0;
        int c = n_first_child[p];
        while (c >= 0) {
            if (n_tag[c] != T_TEXT) {
                idx = idx + 1;
                n_sibling_idx[c] = idx;
                n_prev_sibling_elt[c] = prev_elt;
                if (prev_elt >= 0) n_next_sibling_elt[prev_elt] = c;
                prev_elt = c;
            }
            c = n_next[c];
        }
        /* Stamp total element-sibling count on every element child of p. */
        c = n_first_child[p];
        while (c >= 0) {
            if (n_tag[c] != T_TEXT) n_sibling_count[c] = idx;
            c = n_next[c];
        }
    }
}

/* Look up a named HTML entity by (name, nlen). On match, writes the ASCII
 * approximation to *out_ch and returns 1. The mapping is lossy (no Unicode
 * in the 8x8 ASCII font) but predictable. */
int match_named_entity(char *name, int nlen, int *out_ch) {
    if (nlen == 2) {
        if (b_strieq_n(name, "lt", 2)) { *out_ch = '<'; return 1; }
        if (b_strieq_n(name, "gt", 2)) { *out_ch = '>'; return 1; }
        return 0;
    }
    if (nlen == 3) {
        if (b_strieq_n(name, "amp", 3)) { *out_ch = '&'; return 1; }
        if (b_strieq_n(name, "deg", 3)) { *out_ch = 'd'; return 1; }
        if (b_strieq_n(name, "reg", 3)) { *out_ch = 'R'; return 1; }
        if (b_strieq_n(name, "yen", 3)) { *out_ch = 'Y'; return 1; }
        return 0;
    }
    if (nlen == 4) {
        if (b_strieq_n(name, "quot", 4)) { *out_ch = '"';  return 1; }
        if (b_strieq_n(name, "apos", 4)) { *out_ch = '\''; return 1; }
        if (b_strieq_n(name, "nbsp", 4)) { *out_ch = ' ';  return 1; }
        if (b_strieq_n(name, "bull", 4)) { *out_ch = '*';  return 1; }
        if (b_strieq_n(name, "copy", 4)) { *out_ch = 'C';  return 1; }
        if (b_strieq_n(name, "sect", 4)) { *out_ch = 'S';  return 1; }
        if (b_strieq_n(name, "para", 4)) { *out_ch = 'P';  return 1; }
        if (b_strieq_n(name, "larr", 4)) { *out_ch = '<';  return 1; }
        if (b_strieq_n(name, "rarr", 4)) { *out_ch = '>';  return 1; }
        if (b_strieq_n(name, "uarr", 4)) { *out_ch = '^';  return 1; }
        if (b_strieq_n(name, "darr", 4)) { *out_ch = 'v';  return 1; }
        if (b_strieq_n(name, "cent", 4)) { *out_ch = 'c';  return 1; }
        if (b_strieq_n(name, "euro", 4)) { *out_ch = 'E';  return 1; }
        if (b_strieq_n(name, "Auml", 4)) { *out_ch = 'A';  return 1; }
        if (b_strieq_n(name, "auml", 4)) { *out_ch = 'a';  return 1; }
        if (b_strieq_n(name, "Ouml", 4)) { *out_ch = 'O';  return 1; }
        if (b_strieq_n(name, "ouml", 4)) { *out_ch = 'o';  return 1; }
        if (b_strieq_n(name, "Uuml", 4)) { *out_ch = 'U';  return 1; }
        if (b_strieq_n(name, "uuml", 4)) { *out_ch = 'u';  return 1; }
        if (b_strieq_n(name, "circ", 4)) { *out_ch = '^';  return 1; }
        if (b_strieq_n(name, "tilde",4)) { *out_ch = '~';  return 1; }
        return 0;
    }
    if (nlen == 5) {
        if (b_strieq_n(name, "mdash", 5)) { *out_ch = '-'; return 1; }
        if (b_strieq_n(name, "ndash", 5)) { *out_ch = '-'; return 1; }
        if (b_strieq_n(name, "lsquo", 5)) { *out_ch = '\''; return 1; }
        if (b_strieq_n(name, "rsquo", 5)) { *out_ch = '\''; return 1; }
        if (b_strieq_n(name, "ldquo", 5)) { *out_ch = '"'; return 1; }
        if (b_strieq_n(name, "rdquo", 5)) { *out_ch = '"'; return 1; }
        if (b_strieq_n(name, "times", 5)) { *out_ch = 'x'; return 1; }
        if (b_strieq_n(name, "trade", 5)) { *out_ch = 'T'; return 1; }
        if (b_strieq_n(name, "laquo", 5)) { *out_ch = '<'; return 1; }
        if (b_strieq_n(name, "raquo", 5)) { *out_ch = '>'; return 1; }
        if (b_strieq_n(name, "iexcl", 5)) { *out_ch = '!'; return 1; }
        if (b_strieq_n(name, "pound", 5)) { *out_ch = 'L'; return 1; }
        if (b_strieq_n(name, "radic", 5)) { *out_ch = 'v'; return 1; }
        if (b_strieq_n(name, "infin", 5)) { *out_ch = '8'; return 1; }
        if (b_strieq_n(name, "alpha", 5)) { *out_ch = 'a'; return 1; }
        if (b_strieq_n(name, "Alpha", 5)) { *out_ch = 'A'; return 1; }
        if (b_strieq_n(name, "Sigma", 5)) { *out_ch = 'E'; return 1; }
        if (b_strieq_n(name, "Omega", 5)) { *out_ch = 'O'; return 1; }
        return 0;
    }
    if (nlen == 6) {
        if (b_strieq_n(name, "hellip", 6)) { *out_ch = '.'; return 1; }
        if (b_strieq_n(name, "middot", 6)) { *out_ch = '*'; return 1; }
        if (b_strieq_n(name, "divide", 6)) { *out_ch = '/'; return 1; }
        if (b_strieq_n(name, "plusmn", 6)) { *out_ch = '+'; return 1; }
        if (b_strieq_n(name, "frac12", 6)) { *out_ch = 'h'; return 1; }
        if (b_strieq_n(name, "frac14", 6)) { *out_ch = 'q'; return 1; }
        if (b_strieq_n(name, "frac34", 6)) { *out_ch = 'Q'; return 1; }
        if (b_strieq_n(name, "iquest", 6)) { *out_ch = '?'; return 1; }
        if (b_strieq_n(name, "lambda", 6)) { *out_ch = 'L'; return 1; }
        if (b_strieq_n(name, "permil", 6)) { *out_ch = '%'; return 1; }
        if (b_strieq_n(name, "dagger", 6)) { *out_ch = '+'; return 1; }
        if (b_strieq_n(name, "Dagger", 6)) { *out_ch = '+'; return 1; }
        if (b_strieq_n(name, "lsaquo", 6)) { *out_ch = '<'; return 1; }
        if (b_strieq_n(name, "rsaquo", 6)) { *out_ch = '>'; return 1; }
        if (b_strieq_n(name, "exist",  5)) { *out_ch = 'E'; return 1; }
        if (b_strieq_n(name, "forall", 6)) { *out_ch = 'A'; return 1; }
        return 0;
    }
    return 0;
}

/* Emit `cp` as 1..4 UTF-8 bytes into out (up to omax bytes). Returns byte
 * count written, or 0 if cp is outside U+0000..U+10FFFF. The CSS value
 * decoder and entity decoder both feed this so text in attr_pool stays
 * UTF-8 end-to-end and fontsys.c can look up real cmap glyphs (e.g.
 * U+2022 bullet, U+201C/D curly quotes) instead of folding to ASCII. */
int emit_utf8_codepoint(int cp, char *out, int omax) {
    if (cp < 0) return 0;
    if (cp < 0x80) {
        if (omax < 1) return 0;
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        if (omax < 2) return 0;
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        if (omax < 3) return 0;
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp < 0x110000) {
        if (omax < 4) return 0;
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/* Map a high-codepoint numeric entity to a reasonable ASCII fallback so
 * common Unicode text doesn't render as '?'. Returns 1 if mapped. */
int map_high_codepoint(int v, int *out_ch) {
    if (v == 0x00A9) { *out_ch = 'C'; return 1; }   /* © */
    if (v == 0x00AE) { *out_ch = 'R'; return 1; }   /* ® */
    if (v == 0x00B0) { *out_ch = 'd'; return 1; }   /* ° */
    if (v == 0x00B7) { *out_ch = '.'; return 1; }   /* · -> period (closer to middle dot than '*') */
    if (v == 0x2013) { *out_ch = '-'; return 1; }   /* – */
    if (v == 0x2014) { *out_ch = '-'; return 1; }   /* U+2014 em dash */
    if (v == 0x2018) { *out_ch = '\''; return 1; }  /* ‘ */
    if (v == 0x2019) { *out_ch = '\''; return 1; }  /* ’ */
    if (v == 0x201C) { *out_ch = '"'; return 1; }   /* “ */
    if (v == 0x201D) { *out_ch = '"'; return 1; }   /* ” */
    if (v == 0x2022) { *out_ch = '*'; return 1; }   /* • */
    if (v == 0x2026) { *out_ch = '.'; return 1; }   /* … */
    if (v == 0x2122) { *out_ch = 'T'; return 1; }   /* ™ */
    if (v == 0x2190) { *out_ch = '<'; return 1; }   /* ← */
    if (v == 0x2192) { *out_ch = '>'; return 1; }   /* → */
    if (v == 0x2194) { *out_ch = '-'; return 1; }   /* ↔ */
    if (v == 0x21D2) { *out_ch = '>'; return 1; }   /* ⇒ */
    if (v == 0x221E) { *out_ch = '8'; return 1; }   /* ∞ */
    if (v == 0x2264) { *out_ch = '<'; return 1; }   /* ≤ */
    if (v == 0x2265) { *out_ch = '>'; return 1; }   /* ≥ */
    if (v == 0x2260) { *out_ch = '#'; return 1; }   /* ≠ */
    return 0;
}

/* Decode &amp; / &lt; / &gt; / &quot; / &nbsp; / &#NNN; / &#xHH; and the
 * extended §1 named-entity set into out; returns new length. */
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
                    if (v == 0xAD) {            /* soft hyphen - drop */
                        i = end + 1; continue;
                    }
                    /* Emit raw UTF-8 so TTF cmap can look up the real glyph.
                     * fontsys.c decodes UTF-8 in run_width / draw_run_styled. */
                    int wn = emit_utf8_codepoint(v, out + o, omax - 1 - o);
                    if (wn > 0) { o = o + wn; i = end + 1; continue; }
                    out[o] = '?'; o = o + 1; i = end + 1; continue;
                }
                /* named */
                int ch;
                if (match_named_entity(src + j, el, &ch)) {
                    if (o < omax - 1) { out[o] = (char)ch; o = o + 1; }
                    i = end + 1; continue;
                }
            }
            /* unrecognized entity: emit literal & */
            out[o] = '&'; o = o + 1; i = i + 1;
        } else {
            /* Decode UTF-8 multi-byte sequences in the source so that
             * characters typed directly (e.g. middle-dot, en-dash, smart
             * quotes, ellipsis) get folded to
             * the bitmap-font ASCII fallback through map_high_codepoint
             * instead of bleeding into the framebuffer as garbage glyph
             * bytes. ASCII (0..0x7F) passes through unchanged. */
            int b0 = src[i] & 0xFF;
            if (b0 >= 0x80) {
                int cp = -1;
                int adv = 1;
                if ((b0 & 0xE0) == 0xC0 && i + 1 < slen) {
                    cp = ((b0 & 0x1F) << 6) | (src[i+1] & 0x3F);
                    adv = 2;
                } else if ((b0 & 0xF0) == 0xE0 && i + 2 < slen) {
                    cp = ((b0 & 0x0F) << 12) |
                         ((src[i+1] & 0x3F) << 6) |
                         (src[i+2] & 0x3F);
                    adv = 3;
                } else if ((b0 & 0xF8) == 0xF0 && i + 3 < slen) {
                    cp = ((b0 & 0x07) << 18) |
                         ((src[i+1] & 0x3F) << 12) |
                         ((src[i+2] & 0x3F) << 6) |
                         (src[i+3] & 0x3F);
                    adv = 4;
                }
                if (cp >= 0) {
                    if (cp == 0xA0) { out[o] = ' '; o = o + 1; }
                    else if (cp == 0xAD) { /* soft hyphen drops */ }
                    else {
                        int wn = emit_utf8_codepoint(cp, out + o, omax - 1 - o);
                        if (wn > 0) o = o + wn;
                        else { out[o] = '?'; o = o + 1; }
                    }
                    i = i + adv;
                    continue;
                }
            }
            out[o] = src[i]; o = o + 1; i = i + 1;
        }
    }
    out[o] = 0;
    return o;
}
