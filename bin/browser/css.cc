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
        int prop_id = css_match_property(s + p_start, p_end - p_start);
        if (prop_id == 0) continue;        /* unknown property - drop */
        int v_off = css_intern_value(s + v_start, v_end - v_start);
        if (v_off < 0) continue;
        css_emit_rule(sel_first, sel_count, prop_id, v_off, v_end - v_start, important);
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
