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

/* Identifier-character classifier. CSS identifiers admit ASCII alnum,
 * '-', '_'. Used by class, id, attr name, attr value (unquoted) parsers. */
int css_is_ident_ch(char c) {
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '-' || c == '_') return 1;
    return 0;
}

/* Parse `an+b` or `even`/`odd`/single-int into (a, b). Returns 1 on success.
 * Accepts: "2n+1", "2n", "n+3", "-n+5", "3", "even", "odd". */
int css_parse_nth_arg(char *s, int n, int i, int end, int *out_a, int *out_b) {
    while (i < end && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
    if (i + 4 <= end && b_strieq_n(s + i, "even", 4)) { *out_a = 2; *out_b = 0; return 1; }
    if (i + 3 <= end && b_strieq_n(s + i, "odd",  3)) { *out_a = 2; *out_b = 1; return 1; }
    int a = 0;
    int b = 0;
    int saw_n = 0;
    int sign = 1;
    /* parse "[sign?] [digits?] n? [sign? digits]?" */
    if (i < end && (s[i] == '+' || s[i] == '-')) {
        if (s[i] == '-') sign = -1;
        i = i + 1;
    }
    int has_a_digits = 0;
    int a_part = 0;
    while (i < end && s[i] >= '0' && s[i] <= '9') {
        a_part = a_part * 10 + (s[i] - '0');
        has_a_digits = 1;
        i = i + 1;
    }
    if (i < end && (s[i] == 'n' || s[i] == 'N')) {
        saw_n = 1;
        a = has_a_digits ? a_part : 1;
        a = a * sign;
        i = i + 1;
        while (i < end && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
        int b_sign = 1;
        if (i < end && (s[i] == '+' || s[i] == '-')) {
            if (s[i] == '-') b_sign = -1;
            i = i + 1;
            while (i < end && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
            int bb = 0;
            int has_b = 0;
            while (i < end && s[i] >= '0' && s[i] <= '9') {
                bb = bb * 10 + (s[i] - '0');
                has_b = 1;
                i = i + 1;
            }
            if (!has_b) return 0;
            b = b_sign * bb;
        }
        *out_a = a; *out_b = b;
        return 1;
    }
    /* No 'n': treat as fixed b only */
    if (!has_a_digits) return 0;
    *out_a = 0;
    *out_b = sign * a_part;
    return 1;
}

/* Intern a :not() inner simple compound into the css_not_* pool.
 * Returns the pool index, or -1 if pool full or compound unsupported.
 * We only allow tag/.class/#id/[attr]/:simple-pseudo here; nested :not
 * and combinators are rejected. */
int css_intern_not(char *s, int n, int v_start, int v_end) {
    if (css_not_count >= MAX_CSS_NOT_SELS) return -1;
    int slot = css_not_count;
    css_not_tag        [slot] = 0;
    css_not_class_off  [slot] = -1;
    css_not_id_off     [slot] = -1;
    css_not_attr_off   [slot] = -1;
    css_not_attr_val_off[slot] = -1;
    css_not_attr_op    [slot] = 0;
    css_not_pseudo     [slot] = 0;
    css_not_pseudo_arg [slot] = 0;
    int i = v_start;
    int got = 0;
    while (i < v_end) {
        while (i < v_end && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
        if (i >= v_end) break;
        char c = s[i];
        if (c == '*') { i = i + 1; got = 1; continue; }
        if (c == '#') {
            i = i + 1;
            int ss = i;
            while (i < v_end && css_is_ident_ch(s[i])) i = i + 1;
            css_not_id_off[slot] = attr_intern(s + ss, i - ss);
            got = 1; continue;
        }
        if (c == '.') {
            i = i + 1;
            int ss = i;
            while (i < v_end && css_is_ident_ch(s[i])) i = i + 1;
            css_not_class_off[slot] = attr_intern(s + ss, i - ss);
            got = 1; continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            int ss = i;
            while (i < v_end && css_is_ident_ch(s[i])) i = i + 1;
            css_not_tag[slot] = tag_id(s + ss, i - ss);
            got = 1; continue;
        }
        if (c == '[') {
            i = i + 1;
            while (i < v_end && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
            int ns = i;
            while (i < v_end && css_is_ident_ch(s[i])) i = i + 1;
            if (i == ns) return -1;
            css_not_attr_off[slot] = attr_intern(s + ns, i - ns);
            while (i < v_end && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
            int op = 1;
            int op_done = 0;
            if (i + 1 < v_end && s[i] == '~' && s[i+1] == '=') { op = 3; i = i + 2; op_done = 1; }
            if (!op_done && i + 1 < v_end && s[i] == '^' && s[i+1] == '=') { op = 4; i = i + 2; op_done = 1; }
            if (!op_done && i + 1 < v_end && s[i] == '$' && s[i+1] == '=') { op = 5; i = i + 2; op_done = 1; }
            if (!op_done && i + 1 < v_end && s[i] == '*' && s[i+1] == '=') { op = 6; i = i + 2; op_done = 1; }
            if (!op_done && i < v_end && s[i] == '=') { op = 2; i = i + 1; op_done = 1; }
            (void)op_done;
            if (op != 1) {
                while (i < v_end && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
                int quote = 0;
                if (i < v_end && (s[i] == '"' || s[i] == '\'')) { quote = s[i]; i = i + 1; }
                int vs = i;
                if (quote) { while (i < v_end && s[i] != quote) i = i + 1; }
                else       { while (i < v_end && s[i] != ']' && s[i] != ' ' && s[i] != '\t') i = i + 1; }
                css_not_attr_val_off[slot] = attr_intern(s + vs, i - vs);
                if (quote && i < v_end && s[i] == quote) i = i + 1;
            }
            css_not_attr_op[slot] = op;
            while (i < v_end && s[i] != ']') i = i + 1;
            if (i < v_end && s[i] == ']') i = i + 1;
            got = 1; continue;
        }
        if (c == ':') {
            i = i + 1;
            int ps = i;
            while (i < v_end && (css_is_ident_ch(s[i]) || s[i] == '-')) i = i + 1;
            int pl = i - ps;
            int pseudo = 0;
            if (pl == 5  && b_strieq_n(s + ps, "hover",        5)) pseudo = 1;
            if (pseudo == 0 && pl == 5  && b_strieq_n(s + ps, "focus",        5)) pseudo = 2;
            if (pseudo == 0 && pl == 4  && b_strieq_n(s + ps, "link",         4)) pseudo = 3;
            if (pseudo == 0 && pl == 7  && b_strieq_n(s + ps, "visited",      7)) pseudo = 4;
            if (pseudo == 0 && pl == 11 && b_strieq_n(s + ps, "first-child", 11)) pseudo = 5;
            if (pseudo == 0 && pl == 10 && b_strieq_n(s + ps, "last-child",  10)) pseudo = 6;
            if (pseudo == 0 && pl == 5  && b_strieq_n(s + ps, "empty",        5)) pseudo = 12;
            if (pseudo == 0) return -1;     /* nested :not / :nth-child unsupported in v1 */
            css_not_pseudo[slot] = pseudo;
            got = 1; continue;
        }
        return -1;
    }
    if (!got) return -1;
    css_not_count = css_not_count + 1;
    return slot;
}

/* parse one compound selector; returns end index. Fills out_tag, out_class_off,
 * out_id_off, and the optional attribute selector triple plus pseudo-class +
 * pseudo-element + :not index. If the compound is unsupported, sets *unsupp = 1. */
int css_parse_compound(char *s, int n, int i,
                       int *out_tag, int *out_class_off, int *out_id_off,
                       int *out_attr_off, int *out_attr_val_off, int *out_attr_op,
                       int *out_pseudo, int *out_pseudo_arg,
                       int *out_pseudo_elt, int *out_not_idx,
                       int *unsupp) {
    *out_tag = 0;
    *out_class_off = -1;
    *out_id_off = -1;
    *out_attr_off = -1;
    *out_attr_val_off = -1;
    *out_attr_op = 0;
    *out_pseudo = 0;
    *out_pseudo_arg = 0;
    *out_pseudo_elt = 0;
    *out_not_idx = -1;
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
            while (i < n && css_is_ident_ch(s[i])) i++;
            *out_id_off = attr_intern(s + s_start, i - s_start);
            started = 1; continue;
        }
        if (c == '.') {
            i++;
            int s_start = i;
            while (i < n && css_is_ident_ch(s[i])) i++;
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
        if (c == '[') {
            /* [attr], [attr=v], [attr~=v], [attr^=v], [attr$=v], [attr*=v] */
            i++;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            int n_start = i;
            while (i < n && css_is_ident_ch(s[i])) i++;
            int n_end = i;
            if (n_end == n_start) { *unsupp = 1; return i; }
            *out_attr_off = attr_intern(s + n_start, n_end - n_start);
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            int op = 1;        /* presence by default */
            int op_set = 0;
            if (i + 1 < n && s[i] == '~' && s[i+1] == '=') { op = 3; i += 2; op_set = 1; }
            if (!op_set && i + 1 < n && s[i] == '^' && s[i+1] == '=') { op = 4; i += 2; op_set = 1; }
            if (!op_set && i + 1 < n && s[i] == '$' && s[i+1] == '=') { op = 5; i += 2; op_set = 1; }
            if (!op_set && i + 1 < n && s[i] == '*' && s[i+1] == '=') { op = 6; i += 2; op_set = 1; }
            if (!op_set && i < n && s[i] == '=') { op = 2; i++; op_set = 1; }
            (void)op_set;
            if (op != 1) {
                while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
                int quote = 0;
                if (i < n && (s[i] == '"' || s[i] == '\'')) { quote = s[i]; i++; }
                int v_start = i;
                if (quote) {
                    while (i < n && s[i] != quote) i++;
                } else {
                    while (i < n && s[i] != ']' && s[i] != ' ' && s[i] != '\t') i++;
                }
                int v_end = i;
                *out_attr_val_off = attr_intern(s + v_start, v_end - v_start);
                if (quote && i < n && s[i] == quote) i++;
            }
            *out_attr_op = op;
            while (i < n && s[i] != ']') i++;
            if (i < n && s[i] == ']') i++;
            started = 1; continue;
        }
        if (c == ':') {
            /* `::` introduces a pseudo-element; `:` a pseudo-class. */
            i++;
            int double_colon = 0;
            if (i < n && s[i] == ':') { double_colon = 1; i++; }
            int p_start = i;
            while (i < n &&
                   ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
                    s[i] == '-')) i++;
            int p_len = i - p_start;
            if (double_colon) {
                int pe = 0;
                if (p_len == 6 && b_strieq_n(s + p_start, "before", 6)) pe = 1;
                if (pe == 0 && p_len == 5 && b_strieq_n(s + p_start, "after", 5)) pe = 2;
                if (pe == 0) { *unsupp = 1; return i; }
                *out_pseudo_elt = pe;
                started = 1;
                continue;
            }
            /* single colon - check for legacy ::before/::after written as :before/:after */
            if (p_len == 6 && b_strieq_n(s + p_start, "before", 6)) {
                *out_pseudo_elt = 1; started = 1; continue;
            }
            if (p_len == 5 && b_strieq_n(s + p_start, "after", 5)) {
                *out_pseudo_elt = 2; started = 1; continue;
            }
            int pseudo = 0;
            int has_arg = 0;
            /* Flat sequential lookups instead of one big else-if chain
             * (CupidC's parser stack overflows on long else-if). */
            if (p_len == 5  && b_strieq_n(s + p_start, "hover",        5)) pseudo = 1;
            if (pseudo == 0 && p_len == 5  && b_strieq_n(s + p_start, "focus",        5)) pseudo = 2;
            if (pseudo == 0 && p_len == 4  && b_strieq_n(s + p_start, "link",         4)) pseudo = 3;
            if (pseudo == 0 && p_len == 7  && b_strieq_n(s + p_start, "visited",      7)) pseudo = 4;
            if (pseudo == 0 && p_len == 11 && b_strieq_n(s + p_start, "first-child", 11)) pseudo = 5;
            if (pseudo == 0 && p_len == 10 && b_strieq_n(s + p_start, "last-child",  10)) pseudo = 6;
            if (pseudo == 0 && p_len == 9  && b_strieq_n(s + p_start, "nth-child",    9)) { pseudo = 7; has_arg = 1; }
            if (pseudo == 0 && p_len == 3  && b_strieq_n(s + p_start, "not",          3)) { pseudo = 8; has_arg = 1; }
            if (pseudo == 0 && p_len == 4  && b_strieq_n(s + p_start, "root",         4)) pseudo = 9;
            if (pseudo == 0 && p_len == 13 && b_strieq_n(s + p_start, "first-of-type",13)) pseudo = 10;
            if (pseudo == 0 && p_len == 12 && b_strieq_n(s + p_start, "last-of-type", 12)) pseudo = 11;
            if (pseudo == 0 && p_len == 5  && b_strieq_n(s + p_start, "empty",        5)) pseudo = 12;
            if (pseudo == 0) {
                *unsupp = 1;
                while (i < n && s[i] != ' ' && s[i] != '\t' &&
                       s[i] != ',' && s[i] != '{') i++;
                return i;
            }
            if (has_arg) {
                if (i >= n || s[i] != '(') { *unsupp = 1; return i; }
                i++;
                int arg_start = i;
                int depth = 1;
                while (i < n && depth > 0) {
                    if (s[i] == '(') depth = depth + 1;
                    else if (s[i] == ')') { depth = depth - 1; if (depth == 0) break; }
                    i = i + 1;
                }
                int arg_end = i;
                if (i < n && s[i] == ')') i = i + 1;
                if (pseudo == 7) {
                    int a;
                    int b;
                    if (!css_parse_nth_arg(s, n, arg_start, arg_end, &a, &b)) {
                        *unsupp = 1; return i;
                    }
                    *out_pseudo_arg = ((a & 0xFFFF) << 16) | (b & 0xFFFF);
                } else {
                    /* :not(<simple>) */
                    int idx = css_intern_not(s, n, arg_start, arg_end);
                    if (idx < 0) { *unsupp = 1; return i; }
                    *out_not_idx = idx;
                }
            }
            *out_pseudo = pseudo;
            if (pseudo == 1 || pseudo == 2) css_has_dynamic_pseudo = 1;
            started = 1;
            continue;
        }
        break;
    }
    if (!started) *unsupp = 1;
    return i;
}

/* Parse one selector-chain, append compounds to css_sel_*. Returns chain start
 * (and writes count). Sets *unsupported=1 if any compound unsupported.
 * Tracks combinator between compounds: COMB_DESCENDANT default; `>` `+` `~`
 * bump the next compound's combinator. */
int css_parse_selector_chain(char *s, int n, int i, int *chain_count, int *unsupported) {
    int first = css_sel_count;
    int count = 0;
    int next_comb = COMB_DESCENDANT;            /* combinator for the NEXT compound */
    int saw_ws = 0;                              /* whitespace seen since last compound */
    *unsupported = 0;
    while (i < n) {
        /* Track whitespace between tokens to decide between subselector
         * (no whitespace = same compound, AND) and descendant (whitespace =
         * separate compound). */
        int ws_start = i;
        i = css_skip_ws(s, n, i);
        if (i > ws_start) saw_ws = 1;
        if (i >= n) break;
        char c = s[i];
        if (c == ',' || c == '{') break;
        if (c == '>') { next_comb = COMB_CHILD;       i = i + 1; saw_ws = 0; continue; }
        if (c == '+') { next_comb = COMB_ADJACENT;    i = i + 1; saw_ws = 0; continue; }
        if (c == '~') { next_comb = COMB_GEN_SIBLING; i = i + 1; saw_ws = 0; continue; }
        int t;
        int c_off;
        int id_off;
        int a_off;
        int a_val_off;
        int a_op;
        int pseudo;
        int pseudo_arg;
        int pseudo_elt;
        int not_idx;
        int unsupp;
        int j = css_parse_compound(s, n, i, &t, &c_off, &id_off,
                                   &a_off, &a_val_off, &a_op,
                                   &pseudo, &pseudo_arg, &pseudo_elt, &not_idx,
                                   &unsupp);
        if (unsupp) *unsupported = 1;
        if (j == i) break;            /* no progress */
        /* If next_comb is still descendant AND no whitespace seen since the
         * previous compound, this is a subselector chain (e.g. ".a.b" or
         * "a:hover") rather than a descendant relation. The first compound
         * in a chain (count == 0) always uses COMB_DESCENDANT regardless. */
        int comb = next_comb;
        if (count > 0 && comb == COMB_DESCENDANT && !saw_ws) comb = COMB_SUBSELECTOR;
        if (count == 0) comb = COMB_DESCENDANT;
        if (css_sel_count < MAX_CSS_SELECTORS) {
            css_sel_tag[css_sel_count] = t;
            css_sel_class_off[css_sel_count] = c_off;
            css_sel_id_off[css_sel_count] = id_off;
            css_sel_combinator[css_sel_count] = comb;
            css_sel_attr_off[css_sel_count] = a_off;
            css_sel_attr_val_off[css_sel_count] = a_val_off;
            css_sel_attr_op[css_sel_count] = a_op;
            css_sel_pseudo[css_sel_count] = pseudo;
            css_sel_pseudo_arg[css_sel_count] = pseudo_arg;
            css_sel_pseudo_elt[css_sel_count] = pseudo_elt;
            css_sel_not_idx[css_sel_count] = not_idx;
            css_sel_count++;
            count++;
        }
        next_comb = COMB_DESCENDANT;
        saw_ws = 0;
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
        if (css_sel_attr_op  [sel_first + k] != 0) cls_c++;  /* attr counts as class */
        int pseudo = css_sel_pseudo[sel_first + k];
        if (pseudo != 0) cls_c++;                             /* pseudo-class = class */
        /* :not(X) contributes the inner compound's specificity (Selectors-3). */
        if (pseudo == PSEUDO_NOT) {
            int ni = css_sel_not_idx[sel_first + k];
            if (ni >= 0) {
                if (css_not_id_off   [ni] >= 0) id_c++;
                if (css_not_class_off[ni] >= 0) cls_c++;
                if (css_not_attr_op  [ni] != 0) cls_c++;
                if (css_not_pseudo   [ni] != 0) cls_c++;
                if (css_not_tag      [ni] != 0) tag_c++;
                /* The :not pseudo itself does not add specificity beyond
                 * its inner; subtract the "pseudo counts as class" we
                 * already added above for it. */
                cls_c--;
            }
        }
        if (css_sel_pseudo_elt[sel_first + k] != 0) tag_c++;  /* pseudo-element = tag */
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
    if (len == 11 && b_strieq_n(s, "font-family", 11))            return CP_FONT_FAMILY;
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
    if (len == 12 && b_strieq_n(s, "border-width", 12))           return CP_BORDER_WIDTH;
    if (len == 12 && b_strieq_n(s, "border-style", 12))           return CP_BORDER_STYLE;
    if (len == 10 && b_strieq_n(s, "border-top", 10))             return CP_BORDER_T;
    if (len == 12 && b_strieq_n(s, "border-right", 12))           return CP_BORDER_R;
    if (len == 13 && b_strieq_n(s, "border-bottom", 13))          return CP_BORDER_B;
    if (len == 11 && b_strieq_n(s, "border-left", 11))            return CP_BORDER_L;
    if (len == 4  && b_strieq_n(s, "font", 4))                    return CP_FONT;
    if (len == 5  && b_strieq_n(s, "width", 5))                   return CP_WIDTH;
    if (len == 6  && b_strieq_n(s, "height", 6))                  return CP_HEIGHT;
    if (len == 11 && b_strieq_n(s, "white-space", 11))            return CP_WHITE_SPACE;
    if (len == 16 && b_strieq_n(s, "list-style-type", 16))        return CP_LIST_STYLE_TYPE;
    if (len == 14 && b_strieq_n(s, "vertical-align", 14))         return CP_VERTICAL_ALIGN;
    if (len == 11 && b_strieq_n(s, "line-height", 11))            return CP_LINE_HEIGHT;
    if (len == 9  && b_strieq_n(s, "max-width", 9))               return CP_MAX_WIDTH;
    if (len == 9  && b_strieq_n(s, "min-width", 9))               return CP_MIN_WIDTH;
    if (len == 10 && b_strieq_n(s, "max-height", 10))             return CP_MAX_HEIGHT;
    if (len == 10 && b_strieq_n(s, "min-height", 10))             return CP_MIN_HEIGHT;
    if (len == 7  && b_strieq_n(s, "content",   7))               return CP_CONTENT;
    if (len == 13 && b_strieq_n(s, "border-radius", 13))          return CP_BORDER_RADIUS;
    if (len == 10 && b_strieq_n(s, "box-shadow",    10))          return CP_BOX_SHADOW;
    if (len == 8  && b_strieq_n(s, "overflow",       8))          return CP_OVERFLOW;
    if (len == 6  && b_strieq_n(s, "cursor",         6))          return CP_CURSOR;
    if (len == 7  && b_strieq_n(s, "opacity",        7))          return CP_OPACITY;
    if (len == 16 && b_strieq_n(s, "border-collapse",16))         return CP_BORDER_COLLAPSE;
    if (len == 10 && b_strieq_n(s, "box-sizing",    10))          return CP_BOX_SIZING;
    if (len == 14 && b_strieq_n(s, "text-transform",14))          return CP_TEXT_TRANSFORM;
    if (len == 11 && b_strieq_n(s, "text-indent",   11))          return CP_TEXT_INDENT;
    if (len == 14 && b_strieq_n(s, "letter-spacing",14))          return CP_LETTER_SPACING;
    if (len == 12 && b_strieq_n(s, "word-spacing", 12))           return CP_WORD_SPACING;
    if (len == 9  && b_strieq_n(s, "word-wrap",     9))           return CP_WORD_WRAP;
    if (len == 13 && b_strieq_n(s, "overflow-wrap",13))           return CP_WORD_WRAP;  /* alias */
    if (len == 7  && b_strieq_n(s, "outline",       7))           return CP_OUTLINE;
    if (len == 13 && b_strieq_n(s, "outline-color",13))           return CP_OUTLINE_COLOR;
    if (len == 13 && b_strieq_n(s, "outline-width",13))           return CP_OUTLINE_WIDTH;
    if (len == 13 && b_strieq_n(s, "outline-style",13))           return CP_OUTLINE_STYLE;
    if (len == 8  && b_strieq_n(s, "position",       8))          return CP_POSITION;
    if (len == 3  && b_strieq_n(s, "top",            3))          return CP_TOP;
    if (len == 5  && b_strieq_n(s, "right",          5))          return CP_RIGHT;
    if (len == 6  && b_strieq_n(s, "bottom",         6))          return CP_BOTTOM;
    if (len == 4  && b_strieq_n(s, "left",           4))          return CP_LEFT;
    if (len == 7  && b_strieq_n(s, "z-index",        7))          return CP_Z_INDEX;
    if (len == 5  && b_strieq_n(s, "float",          5))          return CP_FLOAT;
    if (len == 5  && b_strieq_n(s, "clear",          5))          return CP_CLEAR;
    if (len == 14 && b_strieq_n(s, "flex-direction", 14))         return CP_FLEX_DIR;
    if (len == 15 && b_strieq_n(s, "justify-content", 15))        return CP_JUSTIFY;
    if (len == 11 && b_strieq_n(s, "align-items",    11))         return CP_ALIGN_ITEMS;
    if (len == 9  && b_strieq_n(s, "flex-grow",      9))          return CP_FLEX_GROW;
    if (len == 11 && b_strieq_n(s, "flex-shrink",    11))         return CP_FLEX_SHRINK;
    if (len == 10 && b_strieq_n(s, "flex-basis",     10))         return CP_FLEX_BASIS;
    if (len == 4  && b_strieq_n(s, "flex",           4))          return CP_FLEX;
    if (len == 3  && b_strieq_n(s, "gap",            3))          return CP_GAP;
    return 0;
}

void css_emit_rule(int sel_first, int sel_count, int prop_id, int val_off, int val_len, int important) {
    if (css_rule_count >= MAX_CSS_RULES) return;
    int r = css_rule_count++;
    css_rule_sel_first[r] = sel_first;
    css_rule_sel_count[r] = sel_count;
    css_rule_prop_id[r] = prop_id;
    css_rule_value_off[r] = val_off;
    css_rule_value_len[r] = val_len;
    css_rule_specificity[r] = css_compute_specificity(sel_first, sel_count);
    css_rule_doc_order[r] = r;
    css_rule_important[r] = important;
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

        /* Detect and strip a trailing `!important` (case-insensitive) from
         * the value bytes. Pattern: optional ws, '!', optional ws, "important". */
        int important = 0;
        int v_scan = v_end;
        while (v_scan > v_start && (s[v_scan-1] == ' ' || s[v_scan-1] == '\t')) v_scan--;
        if (v_scan - v_start >= 9) {
            int kstart = v_scan - 9;
            if (b_strieq_n(s + kstart, "important", 9)) {
                int bang = kstart;
                while (bang > v_start && (s[bang-1] == ' ' || s[bang-1] == '\t')) bang--;
                if (bang > v_start && s[bang-1] == '!') {
                    important = 1;
                    v_end = bang - 1;
                    while (v_end > v_start && (s[v_end-1] == ' ' || s[v_end-1] == '\t')) v_end--;
                }
            }
        }

        /* trim trailing whitespace from prop name */
        while (p_end > p_start && (s[p_end-1] == ' ' || s[p_end-1] == '\t')) p_end--;
        int p_l = p_end - p_start;
        /* Custom property `--name: <raw value>;`. Routed through the same
         * rule pool but tagged CP_CUSTOM_VAR; the name is interned into
         * css_value_pool and stashed on css_rule_var_name_off/len so the
         * cascade can apply it to cs_var_*[]. Reference: Blink later
         * versions store the raw token sequence on a CSSCustomProperty
         * value; we keep the raw bytes for lazy var() expansion. */
        if (p_l >= 2 && s[p_start] == '-' && s[p_start + 1] == '-') {
            int name_off = css_intern_value(s + p_start, p_l);
            int v_off2 = css_intern_value(s + v_start, v_end - v_start);
            if (name_off < 0 || v_off2 < 0) continue;
            int r = css_rule_count;
            css_emit_rule(sel_first, sel_count, CP_CUSTOM_VAR,
                          v_off2, v_end - v_start, important);
            if (css_rule_count > r) {
                css_rule_var_name_off[r] = name_off;
                css_rule_var_name_len[r] = p_l;
            }
            continue;
        }
        int prop_id = css_match_property(s + p_start, p_l);
        if (prop_id == 0) continue;        /* unknown property - drop */
        int v_off = css_intern_value(s + v_start, v_end - v_start);
        if (v_off < 0) continue;
        css_emit_rule(sel_first, sel_count, prop_id, v_off, v_end - v_start, important);
    }
    return i;
}

/* §2.x At-rule dispatcher. Fires when a top-level token begins with '@'.
 * Handles @font-face by parsing its descriptors and registering a
 * webfont rule with the font_face module (font_face.cc). Other at-rules
 * (@media, @import, @keyframes, ...) skip to the matching '}' so
 * following selectors keep parsing.
 *
 * Reference: Blink core/css/parser/CSSAtRuleID.cpp dispatch table and
 * core/css/parser/AtRuleDescriptorParser.cpp for @font-face descriptor
 * extraction. */
int css_at_skip_block(char *text, int len, int i) {
    /* Position is just past '@'. Walk to '{' or ';' (some at-rules end on ';',
     * e.g. @import, @charset). On ';' we're done; on '{' skip the balanced
     * block. */
    while (i < len && text[i] != '{' && text[i] != ';') i++;
    if (i >= len) return i;
    if (text[i] == ';') return i + 1;
    int depth = 1;
    i++;
    while (i < len && depth > 0) {
        if (text[i] == '{') depth++;
        else if (text[i] == '}') depth--;
        i++;
    }
    return i;
}

/* Extract one CSS url("..."), url('...') or url(...). Returns 1 on success
 * with (out_off, out_len) set to the URL text bytes and `*after` advanced
 * past the closing ')'. */
int css_at_parse_url(char *s, int len, int i, int *out_off, int *out_len, int *after) {
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
    if (i + 4 > len) return 0;
    if (!(s[i] == 'u' || s[i] == 'U')) return 0;
    if (!b_strieq_n(s + i, "url", 3)) return 0;
    i = i + 3;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
    if (i >= len || s[i] != '(') return 0;
    i = i + 1;
    while (i < len && (s[i] == ' ' || s[i] == '\t')) i = i + 1;
    char quote = 0;
    if (i < len && (s[i] == '"' || s[i] == '\'')) { quote = s[i]; i = i + 1; }
    int u_start = i;
    if (quote) {
        while (i < len && s[i] != quote) i = i + 1;
    } else {
        while (i < len && s[i] != ')' && s[i] != ' ' && s[i] != '\t') i = i + 1;
    }
    int u_end = i;
    if (quote && i < len) i = i + 1;
    while (i < len && s[i] != ')') i = i + 1;
    if (i < len) i = i + 1;
    *out_off = u_start;
    *out_len = u_end - u_start;
    *after = i;
    return 1;
}

/* Parse one hex token of the unicode-range descriptor at `s..e` and emit
 * its (lo, hi) pair via *out_lo/*out_hi. Grammar (W3C css-fonts §4.5):
 *   U+XXXX            (single)
 *   U+XXXX-YYYY       (range)
 *   U+XX??            (wildcard; ? expands to 0 for lo, F for hi)
 * Returns 1 on success, 0 if the token is malformed. */
int ff_parse_ur_token(char *text, int s, int e, int *out_lo, int *out_hi) {
    while (s < e && (text[s] == ' ' || text[s] == '\t')) s = s + 1;
    if (s + 2 > e) return 0;
    if (text[s] != 'U' && text[s] != 'u') return 0;
    if (text[s+1] != '+') return 0;
    s = s + 2;
    int lo = 0;
    int hi = 0;
    int seen = 0;
    int has_q = 0;
    while (s < e) {
        char c = text[s];
        int dig = -1;
        if (c >= '0' && c <= '9') dig = c - '0';
        else if (c >= 'a' && c <= 'f') dig = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') dig = c - 'A' + 10;
        else if (c == '?') { dig = 0 - 2; has_q = 1; }
        else break;
        if (dig == 0 - 2) {
            lo = (lo << 4);
            hi = (hi << 4) | 0xF;
        } else {
            lo = (lo << 4) | dig;
            hi = (hi << 4) | dig;
        }
        seen = seen + 1;
        s = s + 1;
    }
    if (seen == 0) return 0;
    if (s < e && text[s] == '-' && !has_q) {
        s = s + 1;
        hi = 0;
        int seen2 = 0;
        while (s < e) {
            char c = text[s];
            int dig = -1;
            if (c >= '0' && c <= '9') dig = c - '0';
            else if (c >= 'a' && c <= 'f') dig = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') dig = c - 'A' + 10;
            else break;
            hi = (hi << 4) | dig;
            seen2 = seen2 + 1;
            s = s + 1;
        }
        if (seen2 == 0) return 0;
    }
    *out_lo = lo;
    *out_hi = hi;
    return 1;
}

/* Parse the unicode-range descriptor value into up to `cap` (lo,hi)
 * pairs, stored in r_lo[]/r_hi[]. Returns the number of ranges parsed
 * (0..cap). On overflow returns cap and silently drops further tokens. */
int ff_parse_unicode_range(char *text, int v_start, int v_end,
                           int *r_lo, int *r_hi, int cap) {
    int n = 0;
    int i = v_start;
    while (i < v_end && n < cap) {
        while (i < v_end && (text[i] == ' ' || text[i] == '\t' ||
                             text[i] == ',' || text[i] == '\n' ||
                             text[i] == '\r')) i = i + 1;
        if (i >= v_end) break;
        int t_start = i;
        while (i < v_end && text[i] != ',') i = i + 1;
        int lo = 0;
        int hi = 0;
        if (ff_parse_ur_token(text, t_start, i, &lo, &hi)) {
            r_lo[n] = lo;
            r_hi[n] = hi;
            n = n + 1;
        }
    }
    return n;
}

/* Score a `format(...)` token. Higher is better. Unknown returns 0
 * (we'd skip it). Bare-url (no format token) is scored 1 by the caller.
 * truetype/otf/ttf/opentype = 4, woff2 = 3, woff = 2. WOFF2 outranks
 * WOFF in modern stylesheets (smaller payload), but WOFF1 is more
 * likely to decode without our brotli dep so we bias slightly toward
 * woff2 anyway and let woff1_unwrap fallback handle the rest. */
int ff_format_score(char *text, int f_start, int f_len) {
    if (f_len == 8 && b_strieq_n(text + f_start, "truetype", 8)) return 4;
    if (f_len == 8 && b_strieq_n(text + f_start, "opentype", 8)) return 4;
    if (f_len == 3 && b_strieq_n(text + f_start, "ttf", 3)) return 4;
    if (f_len == 3 && b_strieq_n(text + f_start, "otf", 3)) return 4;
    if (f_len == 5 && b_strieq_n(text + f_start, "woff2", 5)) return 3;
    if (f_len == 4 && b_strieq_n(text + f_start, "woff", 4)) return 2;
    return 0;
}

/* Parse `@font-face { ... }` and register a webfont with font_face.cc.
 * `i` points just past "@font-face". Returns the position after the
 * closing '}'. Recognised descriptors:
 *   font-family:    <ident-or-string>
 *   src:            url("...") [format("...")] [, ...]
 *   font-weight:    <int>|normal|bold (default 400)
 *   font-style:     italic|normal     (default normal)
 *   unicode-range:  U+XXXX[-YYYY] [, U+...]   (default: covers all CPs)
 *
 * The src list is walked end-to-end. Each url(...) entry is scored by
 * its format token (truetype/otf=4, woff2=3, woff=2, bare=1). The top
 * 3 by score (ties broken by source order) become the slot's fallback
 * chain; the pump tries them in order until one decodes.
 *
 * WOFF1 is decompressed via woff.cc + kdeflate_raw. WOFF2 is currently
 * stubbed (woff2.cc returns NULL); the pump falls through to the next
 * URL. local() tokens are silently skipped (no system-font lookup
 * surface). */
int css_at_font_face(char *text, int len, int i) {
    while (i < len && (text[i] == ' ' || text[i] == '\t' ||
                       text[i] == '\n' || text[i] == '\r')) i = i + 1;
    if (i >= len || text[i] != '{') return css_at_skip_block(text, len, i);
    i = i + 1;

    int family_off = -1; int family_len = 0;
    int src_off    = -1; int src_len    = 0;
    int ur_off     = -1; int ur_len     = 0;
    int weight = 400;
    int italic = 0;

    while (i < len) {
        i = css_skip_ws(text, len, i);
        if (i < len && text[i] == '}') { i = i + 1; break; }
        int p_start = i;
        while (i < len && text[i] != ':' && text[i] != ';' && text[i] != '}') i = i + 1;
        int p_end = i;
        if (i >= len || text[i] != ':') {
            while (i < len && text[i] != ';' && text[i] != '}') i = i + 1;
            if (i < len && text[i] == ';') i = i + 1;
            continue;
        }
        i = i + 1;
        i = css_skip_ws(text, len, i);
        int v_start = i;
        while (i < len && text[i] != ';' && text[i] != '}') i = i + 1;
        int v_end = i;
        while (v_end > v_start && (text[v_end-1] == ' ' || text[v_end-1] == '\t')) v_end = v_end - 1;
        while (p_end > p_start && (text[p_end-1] == ' ' || text[p_end-1] == '\t')) p_end = p_end - 1;
        if (i < len && text[i] == ';') i = i + 1;

        int p_l = p_end - p_start;
        int v_l = v_end - v_start;
        if (p_l == 11 && b_strieq_n(text + p_start, "font-family", 11)) {
            family_off = v_start; family_len = v_l;
        } else if (p_l == 3 && b_strieq_n(text + p_start, "src", 3)) {
            src_off = v_start; src_len = v_l;
        } else if (p_l == 13 && b_strieq_n(text + p_start, "unicode-range", 13)) {
            ur_off = v_start; ur_len = v_l;
        } else if (p_l == 11 && b_strieq_n(text + p_start, "font-weight", 11)) {
            if (b_strieq_n(text + v_start, "bold",   v_l < 4 ? v_l : 4) && v_l == 4)   weight = 700;
            else if (b_strieq_n(text + v_start, "normal", v_l < 6 ? v_l : 6) && v_l == 6) weight = 400;
            else {
                int w = 0;
                int has = 0;
                int k = v_start;
                while (k < v_end && text[k] >= '0' && text[k] <= '9') {
                    w = w * 10 + (text[k] - '0'); has = 1; k = k + 1;
                }
                if (has) weight = w;
            }
        } else if (p_l == 10 && b_strieq_n(text + p_start, "font-style", 10)) {
            if (v_l >= 6 && b_strieq_n(text + v_start, "italic", 6)) italic = 1;
            else italic = 0;
        }
    }

    if (family_off < 0 || src_off < 0) return i;

    /* Walk the src list, collecting up to 3 candidates ranked by format
     * score. Each url(...) entry may be followed by an optional
     * format("...") token; bare url() scores 1. */
    int cand_score[3]; cand_score[0] = -1; cand_score[1] = -1; cand_score[2] = -1;
    int cand_off  [3];
    int cand_len  [3];
    int cand_order[3]; cand_order[0] = 0; cand_order[1] = 0; cand_order[2] = 0;
    int order_seq = 0;

    int s = src_off;
    int s_end = src_off + src_len;
    while (s < s_end) {
        while (s < s_end && (text[s] == ' ' || text[s] == '\t' ||
                              text[s] == ',' || text[s] == '\n' || text[s] == '\r')) s = s + 1;
        if (s >= s_end) break;
        /* Skip local(...) tokens entirely. */
        if (s + 6 <= s_end && b_strieq_n(text + s, "local(", 6)) {
            int depth = 1;
            s = s + 6;
            while (s < s_end && depth > 0) {
                if (text[s] == '(') depth = depth + 1;
                else if (text[s] == ')') depth = depth - 1;
                s = s + 1;
            }
            while (s < s_end && text[s] != ',') s = s + 1;
            continue;
        }
        int u_off; int u_len; int u_after;
        if (!css_at_parse_url(text, s_end, s, &u_off, &u_len, &u_after)) {
            while (s < s_end && text[s] != ',') s = s + 1;
            continue;
        }
        s = u_after;
        /* Optional format("..."). */
        int s_save = s;
        while (s < s_end && (text[s] == ' ' || text[s] == '\t')) s = s + 1;
        int score = 1;     /* bare url() */
        if (s + 7 <= s_end && b_strieq_n(text + s, "format(", 7)) {
            s = s + 7;
            while (s < s_end && (text[s] == ' ' || text[s] == '\t')) s = s + 1;
            char fq = 0;
            if (s < s_end && (text[s] == '"' || text[s] == '\'')) { fq = text[s]; s = s + 1; }
            int f_start = s;
            if (fq) {
                while (s < s_end && text[s] != fq) s = s + 1;
            } else {
                while (s < s_end && text[s] != ')' && text[s] != ' ') s = s + 1;
            }
            int f_len = s - f_start;
            if (fq && s < s_end) s = s + 1;
            while (s < s_end && text[s] != ')') s = s + 1;
            if (s < s_end) s = s + 1;
            score = ff_format_score(text, f_start, f_len);
            if (score == 0) {
                /* Unknown format token (eg. svg, eot) — skip. */
                while (s < s_end && text[s] != ',') s = s + 1;
                continue;
            }
        } else {
            s = s_save;
        }
        if (u_len <= 0) {
            while (s < s_end && text[s] != ',') s = s + 1;
            continue;
        }
        /* Insert into candidate table by score (higher wins; ties keep
         * source order via order_seq). Only the worst slot is replaced. */
        order_seq = order_seq + 1;
        int worst = 0;
        for (int j = 1; j < 3; j = j + 1) {
            if (cand_score[j] < cand_score[worst]) worst = j;
            else if (cand_score[j] == cand_score[worst] &&
                     cand_order[j] > cand_order[worst]) worst = j;
        }
        if (score > cand_score[worst] ||
            (score == cand_score[worst] && order_seq < cand_order[worst])) {
            cand_score[worst] = score;
            cand_off  [worst] = u_off;
            cand_len  [worst] = u_len;
            cand_order[worst] = order_seq;
        }
        while (s < s_end && text[s] != ',') s = s + 1;
    }

    /* Sort candidates by score desc, then source order asc. n is small,
     * so a 3-way insertion sort works fine. */
    for (int a = 0; a < 3; a = a + 1) {
        for (int b = a + 1; b < 3; b = b + 1) {
            int swap = 0;
            if (cand_score[b] > cand_score[a]) swap = 1;
            else if (cand_score[b] == cand_score[a] &&
                     cand_order[b] < cand_order[a] && cand_score[b] > 0) swap = 1;
            if (swap) {
                int ts = cand_score[a]; cand_score[a] = cand_score[b]; cand_score[b] = ts;
                int to = cand_off  [a]; cand_off  [a] = cand_off  [b]; cand_off  [b] = to;
                int tl = cand_len  [a]; cand_len  [a] = cand_len  [b]; cand_len  [b] = tl;
                int tr = cand_order[a]; cand_order[a] = cand_order[b]; cand_order[b] = tr;
            }
        }
    }

    /* Parse unicode-range descriptor (if any). */
    int r_lo[8];
    int r_hi[8];
    int r_n = 0;
    if (ur_off >= 0 && ur_len > 0) {
        r_n = ff_parse_unicode_range(text, ur_off, ur_off + ur_len, r_lo, r_hi, 8);
    }

    /* Need at least one valid candidate. */
    if (cand_score[0] <= 0) return i;

    char *u0 = (cand_score[0] > 0) ? (text + cand_off[0]) : (char*)0;
    int   l0 = (cand_score[0] > 0) ? cand_len[0] : 0;
    char *u1 = (cand_score[1] > 0) ? (text + cand_off[1]) : (char*)0;
    int   l1 = (cand_score[1] > 0) ? cand_len[1] : 0;
    char *u2 = (cand_score[2] > 0) ? (text + cand_off[2]) : (char*)0;
    int   l2 = (cand_score[2] > 0) ? cand_len[2] : 0;

    font_face_add_rule_n(text + family_off, family_len,
                         u0, l0, u1, l1, u2, l2,
                         weight, italic,
                         r_lo, r_hi, r_n);
    return i;
}

void css_parse_block(char *text, int len) {
    int i = 0;
    while (i < len) {
        i = css_skip_ws(text, len, i);
        if (i >= len) break;
        /* §2.x At-rule? Branch out before normal selector-rule path. */
        if (text[i] == '@') {
            int at_start = i + 1;
            /* Recognise "@font-face" by case-insensitive prefix; commit 3
             * will fill in css_at_font_face. For now: skip, so any inline
             * @media wrappers etc. don't poison subsequent selectors. */
            if (at_start + 10 <= len &&
                b_strieq_n(text + at_start, "font-face", 9) &&
                (text[at_start + 9] == ' ' || text[at_start + 9] == '\t' ||
                 text[at_start + 9] == '{' || text[at_start + 9] == '\n' ||
                 text[at_start + 9] == '\r')) {
                i = css_at_font_face(text, len, at_start + 9);
                continue;
            }
            i = css_at_skip_block(text, len, at_start);
            continue;
        }
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
