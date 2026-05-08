/* §2 CSS lexer + selector parser + rule extractor.
 * css_parse_block(text, len) appends to css_rule_*[] every rule it sees.
 * Selectors supported: tag, .class, #id, comma list, descendant (whitespace).
 * Unsupported (>, +, ~, [attr=...], :pseudo, *) parse-skip silently.
 *
 * Pools (declared in main.cc):
 *   css_rule_*    - flattened (selector-chain, property, value) tuples
 *   css_sel_*     - compound-selector chain storage
 *   css_value_pool - raw CSS value bytes, NUL-terminated
 *
 * Caps from main.cc enums:
 *   MAX_CSS_RULES, MAX_CSS_SELECTORS, CSS_VALUE_POOL_SIZE.
 * Overflow drops further rules/selectors per spec §2. */

int css_intern_value(char *src, int len) {
    if (css_value_pool_pos + len + 1 >= CSS_VALUE_POOL_SIZE) return -1;
    int off = css_value_pool_pos;
    for (int k = 0; k < len; k++) css_value_pool[off + k] = src[k];
    css_value_pool[off + len] = 0;
    css_value_pool_pos += len + 1;
    return off;
}

int css_skip_ws(char *s, int n, int i) {
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { i++; continue; }
        if (c == '/' && i + 1 < n && s[i+1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i+1] == '/')) i++;
            if (i + 1 < n) i += 2;
            continue;
        }
        break;
    }
    return i;
}

/* parse one compound selector; returns end index. Fills out_tag, out_class_off,
 * out_id_off. If the compound is unsupported, sets *unsupp = 1. */
int css_parse_compound(char *s, int n, int i,
                       int *out_tag, int *out_class_off, int *out_id_off,
                       int *unsupp) {
    *out_tag = 0;
    *out_class_off = -1;
    *out_id_off = -1;
    *unsupp = 0;
    int started = 0;
    while (i < n) {
        char c = s[i];
        if (c == '*') {
            /* universal selector - supported by leaving tag=0 (any). */
            i++; started = 1; continue;
        }
        if (c == '#') {
            i++;
            int s_start = i;
            while (i < n && (((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
                              (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '-'))) i++;
            *out_id_off = attr_intern(s + s_start, i - s_start);
            started = 1; continue;
        }
        if (c == '.') {
            i++;
            int s_start = i;
            while (i < n && (((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
                              (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '-'))) i++;
            *out_class_off = attr_intern(s + s_start, i - s_start);
            started = 1; continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            int s_start = i;
            while (i < n && (((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
                              (s[i] >= '0' && s[i] <= '9')))) i++;
            *out_tag = tag_id(s + s_start, i - s_start);
            started = 1; continue;
        }
        if (c == '[' || c == ':' || c == '>' || c == '+' || c == '~') {
            *unsupp = 1;
            /* skip to next whitespace, comma, or { */
            while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != ',' && s[i] != '{') i++;
            return i;
        }
        break;
    }
    if (!started) *unsupp = 1;
    return i;
}

/* Parse one selector-chain, append compounds to css_sel_*. Returns chain start
 * (and writes count). Sets *unsupported=1 if any compound unsupported. */
int css_parse_selector_chain(char *s, int n, int i, int *chain_count, int *unsupported) {
    int first = css_sel_count;
    int count = 0;
    *unsupported = 0;
    while (i < n) {
        i = css_skip_ws(s, n, i);
        if (i >= n) break;
        char c = s[i];
        if (c == ',' || c == '{') break;
        int t;
        int c_off;
        int id_off;
        int unsupp;
        int j = css_parse_compound(s, n, i, &t, &c_off, &id_off, &unsupp);
        if (unsupp) *unsupported = 1;
        if (j == i) break;            /* no progress */
        if (css_sel_count < MAX_CSS_SELECTORS) {
            css_sel_tag[css_sel_count] = t;
            css_sel_class_off[css_sel_count] = c_off;
            css_sel_id_off[css_sel_count] = id_off;
            css_sel_count++;
            count++;
        }
        i = j;
    }
    *chain_count = count;
    if (count == 0) {
        css_sel_count = first;       /* roll back */
        *unsupported = 1;
    }
    return i;
}

int css_compute_specificity(int sel_first, int sel_count) {
    int id_c = 0;
    int cls_c = 0;
    int tag_c = 0;
    for (int k = 0; k < sel_count; k++) {
        if (css_sel_id_off   [sel_first + k] >= 0) id_c++;
        if (css_sel_class_off[sel_first + k] >= 0) cls_c++;
        if (css_sel_tag      [sel_first + k] != 0) tag_c++;
    }
    int s = (id_c << 16) | (cls_c << 8) | tag_c;
    if (s > 0xFFFFFF) s = 0xFFFFFF;
    return s;
}

int css_match_property(char *s, int len) {
    if (len == 5  && b_strieq_n(s, "color", 5))                   return CP_COLOR;
    if (len == 16 && b_strieq_n(s, "background-color", 16))       return CP_BG_COLOR;
    if (len == 10 && b_strieq_n(s, "background", 10))             return CP_BG;
    if (len == 11 && b_strieq_n(s, "font-weight", 11))            return CP_FONT_WEIGHT;
    if (len == 10 && b_strieq_n(s, "font-style", 10))             return CP_FONT_STYLE;
    if (len == 9  && b_strieq_n(s, "font-size", 9))               return CP_FONT_SIZE;
    if (len == 10 && b_strieq_n(s, "text-align", 10))             return CP_TEXT_ALIGN;
    if (len == 15 && b_strieq_n(s, "text-decoration", 15))        return CP_TEXT_DEC;
    if (len == 7  && b_strieq_n(s, "display", 7))                 return CP_DISPLAY;
    if (len == 6  && b_strieq_n(s, "margin", 6))                  return CP_MARGIN;
    if (len == 10 && b_strieq_n(s, "margin-top", 10))             return CP_MARGIN_T;
    if (len == 12 && b_strieq_n(s, "margin-right", 12))           return CP_MARGIN_R;
    if (len == 13 && b_strieq_n(s, "margin-bottom", 13))          return CP_MARGIN_B;
    if (len == 11 && b_strieq_n(s, "margin-left", 11))            return CP_MARGIN_L;
    if (len == 7  && b_strieq_n(s, "padding", 7))                 return CP_PADDING;
    if (len == 11 && b_strieq_n(s, "padding-top", 11))            return CP_PADDING_T;
    if (len == 13 && b_strieq_n(s, "padding-right", 13))          return CP_PADDING_R;
    if (len == 14 && b_strieq_n(s, "padding-bottom", 14))         return CP_PADDING_B;
    if (len == 12 && b_strieq_n(s, "padding-left", 12))           return CP_PADDING_L;
    if (len == 6  && b_strieq_n(s, "border", 6))                  return CP_BORDER;
    if (len == 12 && b_strieq_n(s, "border-color", 12))           return CP_BORDER_COLOR;
    if (len == 5  && b_strieq_n(s, "width", 5))                   return CP_WIDTH;
    if (len == 6  && b_strieq_n(s, "height", 6))                  return CP_HEIGHT;
    if (len == 11 && b_strieq_n(s, "white-space", 11))            return CP_WHITE_SPACE;
    if (len == 16 && b_strieq_n(s, "list-style-type", 16))        return CP_LIST_STYLE_TYPE;
    if (len == 14 && b_strieq_n(s, "vertical-align", 14))         return CP_VERTICAL_ALIGN;
    return 0;
}

void css_emit_rule(int sel_first, int sel_count, int prop_id, int val_off, int val_len) {
    if (css_rule_count >= MAX_CSS_RULES) return;
    int r = css_rule_count++;
    css_rule_sel_first[r] = sel_first;
    css_rule_sel_count[r] = sel_count;
    css_rule_prop_id[r] = prop_id;
    css_rule_value_off[r] = val_off;
    css_rule_value_len[r] = val_len;
    css_rule_specificity[r] = css_compute_specificity(sel_first, sel_count);
    css_rule_doc_order[r] = r;
}

/* Walk a declaration block { prop: value; prop: value; ... }. Caller has positioned
 * at '{'. Returns position after '}'. */
int css_parse_decls(char *s, int n, int i,
                    int sel_first, int sel_count) {
    if (i >= n || s[i] != '{') return i;
    i++;
    while (i < n) {
        i = css_skip_ws(s, n, i);
        if (i < n && s[i] == '}') { i++; break; }
        int p_start = i;
        while (i < n && s[i] != ':' && s[i] != ';' && s[i] != '}') i++;
        int p_end = i;
        if (i >= n || s[i] != ':') {
            /* malformed - skip to ';' or '}' */
            while (i < n && s[i] != ';' && s[i] != '}') i++;
            if (i < n && s[i] == ';') i++;
            continue;
        }
        i++;
        i = css_skip_ws(s, n, i);
        int v_start = i;
        while (i < n && s[i] != ';' && s[i] != '}') i++;
        int v_end = i;
        /* trim trailing whitespace */
        while (v_end > v_start && (s[v_end-1] == ' ' || s[v_end-1] == '\t')) v_end--;
        if (i < n && s[i] == ';') i++;

        /* trim trailing whitespace from prop name */
        while (p_end > p_start && (s[p_end-1] == ' ' || s[p_end-1] == '\t')) p_end--;
        int prop_id = css_match_property(s + p_start, p_end - p_start);
        if (prop_id == 0) continue;        /* unknown property - drop */
        int v_off = css_intern_value(s + v_start, v_end - v_start);
        if (v_off < 0) continue;
        css_emit_rule(sel_first, sel_count, prop_id, v_off, v_end - v_start);
    }
    return i;
}

void css_parse_block(char *text, int len) {
    int i = 0;
    while (i < len) {
        i = css_skip_ws(text, len, i);
        if (i >= len) break;
        /* Possibly multiple selectors separated by ',' */
        int chain_starts[16];
        int chain_counts[16];
        int chain_n = 0;
        int unsup = 0;
        while (i < len && chain_n < 16) {
            int sf = css_sel_count;
            int cc;
            int u;
            i = css_parse_selector_chain(text, len, i, &cc, &u);
            chain_starts[chain_n] = sf;
            chain_counts[chain_n] = cc;
            if (u || cc == 0) unsup = 1;
            chain_n++;
            i = css_skip_ws(text, len, i);
            if (i < len && text[i] == ',') { i++; continue; }
            break;
        }
        i = css_skip_ws(text, len, i);
        if (i >= len) break;
        if (text[i] != '{') {
            /* malformed @-rule or unsupported - skip to next ';' or '}' */
            while (i < len && text[i] != '{' && text[i] != '}') i++;
            if (i < len && text[i] == '{') {
                /* skip block */
                int depth = 1;
                i++;
                while (i < len && depth > 0) {
                    if (text[i] == '{') depth++;
                    else if (text[i] == '}') depth--;
                    i++;
                }
            } else if (i < len) i++;
            continue;
        }
        if (unsup) {
            /* skip block */
            int depth = 1;
            i++;
            while (i < len && depth > 0) {
                if (text[i] == '{') depth++;
                else if (text[i] == '}') depth--;
                i++;
            }
            continue;
        }
        /* Apply decls to each selector chain */
        int decl_start = i;
        for (int k = 0; k < chain_n; k++) {
            i = css_parse_decls(text, len, decl_start, chain_starts[k], chain_counts[k]);
        }
    }
}
