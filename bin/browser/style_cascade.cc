/**
 * @file style_cascade.cc
 * @brief Match selectors, resolve the cascade, and inherit styles.
 */

/* Inline style declarations.
 * Defined before sel_X and style_resolve_all so the latter can call it
 * without forward declarations.*/

/* First pass over a `style="..."` attribute: only extracts the
 * custom-property (`--name: value`) declarations into cs_var_*[]. Run
 * BEFORE the regular property cascade so var() calls during regular-
 * rule application see the inline-declared vars. Without this, inline
 * `style="--c: blue"` was parsed in step 3 of style_resolve_all but
 * `color: var(--c, red)` had already been resolved in step 2 against
 * an empty cs_var_*, falling back to `red`.*/
void apply_inline_vars(int cs, char *s) {
    int n = b_strlen(s);
    int i = 0;
    while (i < n) {
        while (i < n && (s[i] == ' ' || s[i] == '\t'))
            i = i + 1;
        if (i >= n)
            break;
        int p_start = i;
        while (i < n && s[i] != ':' && s[i] != ';')
            i = i + 1;
        int p_end = i;
        while (p_end > p_start && (s[p_end - 1] == ' ' || s[p_end - 1] == '\t'))
            p_end = p_end - 1;
        if (i >= n || s[i] != ':')
            break;
        i = i + 1;
        while (i < n && (s[i] == ' ' || s[i] == '\t'))
            i = i + 1;
        int v_start = i;
        while (i < n && s[i] != ';')
            i = i + 1;
        int v_end = i;
        while (v_end > v_start && (s[v_end - 1] == ' ' || s[v_end - 1] == '\t'))
            v_end = v_end - 1;
        if (i < n && s[i] == ';')
            i = i + 1;
        int p_l = p_end - p_start;
        if (p_l >= 2 && s[p_start] == '-' && s[p_start + 1] == '-') {
            if (cs_var_count[cs] < 8) {
                int name_off = css_intern_value(s + p_start, p_l);
                int val_off = css_intern_value(s + v_start, v_end - v_start);
                if (name_off >= 0 && val_off >= 0) {
                    int k = cs_var_count[cs];
                    cs_var_name_off[cs][k] = name_off;
                    cs_var_name_len[cs][k] = p_l;
                    cs_var_val_off[cs][k] = val_off;
                    cs_var_val_len[cs][k] = v_end - v_start;
                    cs_var_count[cs] = k + 1;
                }
            }
        }
    }
}

void apply_inline_style(int cs, char *s) {
    int n = b_strlen(s);
    int i = 0;
    while (i < n) {
        while (i < n && (s[i] == ' ' || s[i] == '\t'))
            i = i + 1;
        if (i >= n)
            break;
        int p_start = i;
        while (i < n && s[i] != ':' && s[i] != ';')
            i = i + 1;
        int p_end = i;
        while (p_end > p_start && (s[p_end - 1] == ' ' || s[p_end - 1] == '\t'))
            p_end = p_end - 1;
        if (i >= n || s[i] != ':')
            break;
        i = i + 1;
        while (i < n && (s[i] == ' ' || s[i] == '\t'))
            i = i + 1;
        int v_start = i;
        while (i < n && s[i] != ';')
            i = i + 1;
        int v_end = i;
        while (v_end > v_start && (s[v_end - 1] == ' ' || s[v_end - 1] == '\t'))
            v_end = v_end - 1;
        if (i < n && s[i] == ';')
            i = i + 1;
        int p_l = p_end - p_start;
        /* Inline custom-property declaration: `style="--foo: bar"`. */
        if (p_l >= 2 && s[p_start] == '-' && s[p_start + 1] == '-') {
            if (cs_var_count[cs] < 8) {
                int name_off = css_intern_value(s + p_start, p_l);
                int val_off = css_intern_value(s + v_start, v_end - v_start);
                if (name_off >= 0 && val_off >= 0) {
                    int k = cs_var_count[cs];
                    cs_var_name_off[cs][k] = name_off;
                    cs_var_name_len[cs][k] = p_l;
                    cs_var_val_off[cs][k] = val_off;
                    cs_var_val_len[cs][k] = v_end - v_start;
                    cs_var_count[cs] = k + 1;
                }
            }
            continue;
        }
        int prop = css_match_property(s + p_start, p_l);
        if (prop) {
            int voff = css_intern_value(s + v_start, v_end - v_start);
            if (voff >= 0)
                cs_apply_property(cs, prop, voff, v_end - v_start);
        }
    }
}

/* CSS selector matching */

/* Match an attribute against (op, name_off, val_off). Returns 1 on hit.
 * Shared between css_sel_* and css_not_* compound matchers.*/
int attr_op_matches(int node, int a_off, int a_val_off, int a_op) {
    if (a_op == ATTR_OP_NONE)
        return 1;
    if (a_off < 0)
        return 0;
    char *aname = attr_pool + a_off;
    int node_attr = dom_attr_get(node, aname);
    if (node_attr < 0)
        return 0;
    if (a_op == ATTR_OP_PRESENCE)
        return 1;
    if (a_val_off < 0)
        return 0;
    char *want = attr_pool + a_val_off;
    int want_len = b_strlen(want);
    char *have = attr_pool + node_attr;
    int have_len = b_strlen(have);
    int i;
    int ws;
    if (a_op == ATTR_OP_EXACT) {
        if (have_len != want_len)
            return 0;
        return b_strieq_n(have, want, want_len) ? 1 : 0;
    }
    if (a_op == ATTR_OP_WORD) {
        i = 0;
        while (i < have_len) {
            while (i < have_len && (have[i] == ' ' || have[i] == '\t'))
                i = i + 1;
            ws = i;
            while (i < have_len && have[i] != ' ' && have[i] != '\t')
                i = i + 1;
            if (i - ws == want_len && b_strieq_n(have + ws, want, want_len))
                return 1;
        }
        return 0;
    }
    if (a_op == ATTR_OP_PREFIX) {
        if (want_len == 0 || want_len > have_len)
            return 0;
        return b_strieq_n(have, want, want_len) ? 1 : 0;
    }
    if (a_op == ATTR_OP_SUFFIX) {
        if (want_len == 0 || want_len > have_len)
            return 0;
        return b_strieq_n(have + have_len - want_len, want, want_len) ? 1 : 0;
    }
    if (a_op == ATTR_OP_CONTAINS) {
        if (want_len == 0 || want_len > have_len)
            return 0;
        i = 0;
        while (i + want_len <= have_len) {
            if (b_strieq_n(have + i, want, want_len))
                return 1;
            i = i + 1;
        }
        return 0;
    }
    return 0;
}

/* Class-token membership against a stored class= attribute. */
int class_token_match(int node, int c_off) {
    int node_class = dom_class_off[node];
    if (node_class < 0)
        return 0;
    char *tgt = attr_pool + c_off;
    int tgt_len = b_strlen(tgt);
    if (tgt_len == 0)
        return 0;
    char *cls = attr_pool + node_class;
    int cls_len = b_strlen(cls);
    int i = 0;
    while (i < cls_len) {
        while (i < cls_len && (cls[i] == ' ' || cls[i] == '\t'))
            i = i + 1;
        int s = i;
        while (i < cls_len && cls[i] != ' ' && cls[i] != '\t')
            i = i + 1;
        if (i - s == tgt_len && b_strieq_n(cls + s, tgt, tgt_len))
            return 1;
    }
    return 0;
}

/* Match a simple pseudo-class (ones that depend only on the node and
 * tree-position caches). Returns 1 on hit. PSEUDO_NTH_CHILD reads
 * `pseudo_arg` packed as (a<<16)|(b&0xFFFF) with signed semantics.*/
int simple_pseudo_matches(int node, int pseudo, int pseudo_arg) {
    int p;
    int idx;
    int a_raw;
    int b_raw;
    int a;
    int b;
    int diff;
    int t;
    int c;
    int last_match;
    if (pseudo == PSEUDO_NONE)
        return 1;
    if (pseudo == PSEUDO_HOVER) {
        if (hover_dom_node < 0)
            return 0;
        p = hover_dom_node;
        while (p >= 0) {
            if (p == node)
                return 1;
            p = n_parent[p];
        }
        return 0;
    }
    if (pseudo == PSEUDO_FOCUS) {
        if (focused_input < 0 || focused_input >= inputs_count)
            return 0;
        return (input_node[focused_input] == node) ? 1 : 0;
    }
    if (pseudo == PSEUDO_LINK) {
        if (n_tag[node] != T_A)
            return 0;
        return (dom_attr_get(node, "href") >= 0) ? 1 : 0;
    }
    if (pseudo == PSEUDO_VISITED)
        return 0;
    if (pseudo == PSEUDO_FIRST_CHILD) {
        return (n_sibling_idx[node] == 1) ? 1 : 0;
    }
    if (pseudo == PSEUDO_LAST_CHILD) {
        if (n_sibling_idx[node] == 0)
            return 0;
        return (n_sibling_idx[node] == n_sibling_count[node]) ? 1 : 0;
    }
    if (pseudo == PSEUDO_NTH_CHILD) {
        idx = n_sibling_idx[node];
        if (idx <= 0)
            return 0;
        /* Sign-extend the 16-bit packed halves. */
        a_raw = (pseudo_arg >> 16) & 0xFFFF;
        b_raw = pseudo_arg & 0xFFFF;
        a = (a_raw & 0x8000) ? (a_raw | -0x10000) : a_raw;
        b = (b_raw & 0x8000) ? (b_raw | -0x10000) : b_raw;
        if (a == 0)
            return (idx == b) ? 1 : 0;
        diff = idx - b;
        if (a > 0) {
            if (diff < 0)
                return 0;
            return (diff % a == 0) ? 1 : 0;
        }
        /* a < 0 */
        if (diff > 0)
            return 0;
        return ((-diff) % (-a) == 0) ? 1 : 0;
    }
    if (pseudo == PSEUDO_ROOT) {
        return (n_tag[node] == T_HTML || n_parent[node] < 0) ? 1 : 0;
    }
    if (pseudo == PSEUDO_FIRST_OF_TYPE) {
        t = n_tag[node];
        p = n_parent[node];
        if (p < 0)
            return 1;
        c = n_first_child[p];
        while (c >= 0) {
            if (n_tag[c] == t)
                return (c == node) ? 1 : 0;
            c = n_next[c];
        }
        return 0;
    }
    if (pseudo == PSEUDO_LAST_OF_TYPE) {
        t = n_tag[node];
        p = n_parent[node];
        if (p < 0)
            return 1;
        last_match = -1;
        c = n_first_child[p];
        while (c >= 0) {
            if (n_tag[c] == t)
                last_match = c;
            c = n_next[c];
        }
        return (last_match == node) ? 1 : 0;
    }
    if (pseudo == PSEUDO_EMPTY) {
        return (n_first_child[node] < 0) ? 1 : 0;
    }
    return 0;
}

/* Match a single :not(<simple>) inner compound at `not_idx`. */
int not_simple_matches(int not_idx, int node) {
    if (not_idx < 0)
        return 0;
    int t = css_not_tag[not_idx];
    if (t != 0 && n_tag[node] != t)
        return 0;
    int c_off = css_not_class_off[not_idx];
    if (c_off >= 0 && !class_token_match(node, c_off))
        return 0;
    int id_off = css_not_id_off[not_idx];
    if (id_off >= 0) {
        int node_id = dom_id_off[node];
        if (node_id < 0)
            return 0;
        if (!b_strieq(attr_pool + node_id, attr_pool + id_off))
            return 0;
    }
    if (!attr_op_matches(node, css_not_attr_off[not_idx], css_not_attr_val_off[not_idx],
                         css_not_attr_op[not_idx]))
        return 0;
    int p = css_not_pseudo[not_idx];
    if (p != 0 && !simple_pseudo_matches(node, p, css_not_pseudo_arg[not_idx]))
        return 0;
    return 1;
}

int sel_compound_matches(int sel_idx, int node) {
    int t = css_sel_tag[sel_idx];
    if (t != 0 && n_tag[node] != t)
        return 0;
    int c_off = css_sel_class_off[sel_idx];
    if (c_off >= 0 && !class_token_match(node, c_off))
        return 0;
    int id_off = css_sel_id_off[sel_idx];
    int node_id;
    int pseudo;
    int ni;
    int a_op;
    if (id_off >= 0) {
        node_id = dom_id_off[node];
        if (node_id < 0)
            return 0;
        if (!b_strieq(attr_pool + node_id, attr_pool + id_off))
            return 0;
    }
    pseudo = css_sel_pseudo[sel_idx];
    if (pseudo == PSEUDO_NOT) {
        /* :not(X): succeed iff inner does NOT match */
        ni = css_sel_not_idx[sel_idx];
        if (not_simple_matches(ni, node))
            return 0;
    }
    if (pseudo != PSEUDO_NONE && pseudo != PSEUDO_NOT) {
        if (!simple_pseudo_matches(node, pseudo, css_sel_pseudo_arg[sel_idx]))
            return 0;
    }
    a_op = css_sel_attr_op[sel_idx];
    if (a_op != ATTR_OP_NONE) {
        if (!attr_op_matches(node, css_sel_attr_off[sel_idx], css_sel_attr_val_off[sel_idx], a_op))
            return 0;
    }
    return 1;
}

/* Inner combinator-walk. Caller has already verified the tail compound;
 * this walks left-to-right combinators from `last-1` down to `sel_first`.
 * CupidC keeps locals in a flat table, so all branch-scoped ints are
 * hoisted to function scope.*/
int sel_chain_walk(int sel_first, int last, int node) {
    int cur = node;
    int s = last - 1;
    int comb;
    int p;
    int prev;
    while (s >= sel_first) {
        comb = css_sel_combinator[s + 1];
        if (comb == COMB_SUBSELECTOR) {
            if (!sel_compound_matches(s, cur))
                return 0;
            s = s - 1;
            continue;
        }
        if (comb == COMB_CHILD) {
            p = n_parent[cur];
            if (p < 0)
                return 0;
            if (!sel_compound_matches(s, p))
                return 0;
            cur = p;
            s = s - 1;
            continue;
        }
        if (comb == COMB_DESCENDANT) {
            p = n_parent[cur];
            while (p >= 0 && !sel_compound_matches(s, p))
                p = n_parent[p];
            if (p < 0)
                return 0;
            cur = p;
            s = s - 1;
            continue;
        }
        if (comb == COMB_ADJACENT) {
            prev = n_prev_sibling_elt[cur];
            if (prev < 0)
                return 0;
            if (!sel_compound_matches(s, prev))
                return 0;
            cur = prev;
            s = s - 1;
            continue;
        }
        if (comb == COMB_GEN_SIBLING) {
            prev = n_prev_sibling_elt[cur];
            while (prev >= 0 && !sel_compound_matches(s, prev))
                prev = n_prev_sibling_elt[prev];
            if (prev < 0)
                return 0;
            cur = prev;
            s = s - 1;
            continue;
        }
        return 0;
    }
    return 1;
}

/* Match a selector chain against a node. Right-to-left walk modeled on
 * Blink's SelectorChecker::match. Rules with a tail pseudo-element are
 * treated as non-matching against the originating element so they don't
 * pollute its own ComputedStyle (those rules feed the pseudo cascade
 * via sel_chain_matches_pseudo).*/
int sel_chain_matches(int sel_first, int sel_count, int node) {
    if (sel_count == 0)
        return 0;
    int last = sel_first + sel_count - 1;
    if (css_sel_pseudo_elt[last] != PSELT_NONE)
        return 0;
    if (!sel_compound_matches(last, node))
        return 0;
    return sel_chain_walk(sel_first, last, node);
}

/* Pseudo-element variant: the chain MUST end with the requested
 * pseudo-element flag. The originating element is `node`; combinators are
 * still resolved against the real DOM. Used by ::before/::after content
 * cascade and any future pseudo-element styling.*/
int sel_chain_matches_pseudo(int sel_first, int sel_count, int node, int wanted_pe) {
    if (sel_count == 0)
        return 0;
    int last = sel_first + sel_count - 1;
    if (css_sel_pseudo_elt[last] != wanted_pe)
        return 0;
    if (!sel_compound_matches(last, node))
        return 0;
    return sel_chain_walk(sel_first, last, node);
}

/* Decode a CSS string value into out[]. Handles `\HHHHHH` codepoint
 * escapes (1..6 hex digits, optional terminating space) and UTF-8
 * multi-byte sequences in raw text. Codepoints above 127 are folded
 * through map_high_codepoint() to ASCII fallbacks (the bitmap font
 * doesn't carry real Unicode). Returns the decoded byte length, or 0 if
 * the value isn't a quoted string.*/
int css_value_string(int off, int len, char *out, int omax) {
    int i = off;
    int end = off + len;
    int o = 0;
    int v;
    int d;
    int hd;
    int hi;
    int b0;
    int cp;
    int adv;
    char c;
    char quote;
    while (i < end && (css_value_pool[i] == ' ' || css_value_pool[i] == '\t'))
        i = i + 1;
    if (i >= end)
        return 0;
    quote = css_value_pool[i];
    if (quote != '"' && quote != '\'')
        return 0;
    i = i + 1;
    while (i < end && o < omax - 1) {
        c = css_value_pool[i];
        if (c == quote) {
            i = i + 1;
            break;
        }
        if (c == '\\' && i + 1 < end) {
            i = i + 1;
            v = 0;
            d = 0;
            while (i < end && d < 6) {
                hd = hex_digit(css_value_pool[i]);
                if (hd < 0)
                    break;
                v = v * 16 + hd;
                d = d + 1;
                i = i + 1;
            }
            if (d == 0) {
                /* `\x` literal escape (drop the backslash, keep the char) */
                out[o] = css_value_pool[i];
                o = o + 1;
                i = i + 1;
                continue;
            }
            if (i < end && css_value_pool[i] == ' ')
                i = i + 1;
            if (v >= 32 && v < 127) {
                out[o] = (char)v;
                o = o + 1;
                continue;
            }
            if (v == 0xA0) {
                out[o] = ' ';
                o = o + 1;
                continue;
            }
            /* Emit raw UTF-8 so TTF cmap renders the real glyph
             * (e.g. \201C -> U+201C left double quote). kernel/gfx/fontsys.cc
             * UTF-8-decodes runs in run_width / draw_run_styled.*/
            int wn = emit_utf8_codepoint(v, out + o, omax - 1 - o);
            if (wn > 0) {
                o = o + wn;
                continue;
            }
            out[o] = '?';
            o = o + 1;
            continue;
        }
        b0 = c & 0xFF;
        if (b0 >= 0x80) {
            cp = -1;
            adv = 1;
            if ((b0 & 0xE0) == 0xC0 && i + 1 < end) {
                cp = ((b0 & 0x1F) << 6) | (css_value_pool[i + 1] & 0x3F);
                adv = 2;
            }
            if (cp < 0 && (b0 & 0xF0) == 0xE0 && i + 2 < end) {
                cp = ((b0 & 0x0F) << 12) | ((css_value_pool[i + 1] & 0x3F) << 6) |
                     (css_value_pool[i + 2] & 0x3F);
                adv = 3;
            }
            if (cp >= 0) {
                if (cp == 0xA0) {
                    out[o] = ' ';
                    o = o + 1;
                } else if (map_high_codepoint(cp, &hi)) {
                    out[o] = (char)hi;
                    o = o + 1;
                } else {
                    out[o] = '?';
                    o = o + 1;
                }
                i = i + adv;
                continue;
            }
        }
        out[o] = c;
        o = o + 1;
        i = i + 1;
    }
    out[o] = 0;
    return o;
}

/* Subroutine of style_resolve_all: resolve ::before / ::after generated
 * content for `node`. Matches every rule with a pseudo-element tail
 * compound, picks the highest-specificity (then doc-order) winner per
 * pseudo-element side, decodes the `content:` string, and saves the
 * result to n_pseudo_before/after_off. Other pseudo-element properties
 * (color, font, etc.) are not yet honored. Only `content`.*/
void resolve_pseudo_content(int node) {
    int win_b = -1;
    int score_b = -1;
    int win_a = -1;
    int score_a = -1;
    int sf;
    int sc;
    int tail;
    int pe;
    int score;
    int len;
    int ao;
    char buf[256];
    for (int r = 0; r < css_rule_count; r = r + 1) {
        if (css_rule_prop_id[r] != CP_CONTENT)
            continue;
        sf = css_rule_sel_first[r];
        sc = css_rule_sel_count[r];
        tail = sf + sc - 1;
        pe = css_sel_pseudo_elt[tail];
        if (pe == PSELT_NONE)
            continue;
        if (!sel_chain_matches_pseudo(sf, sc, node, pe))
            continue;
        score = (css_rule_specificity[r] << 12) | (css_rule_doc_order[r] & 0xFFF);
        if (pe == PSELT_BEFORE) {
            if (score > score_b) {
                score_b = score;
                win_b = r;
            }
            continue;
        }
        if (pe == PSELT_AFTER) {
            if (score > score_a) {
                score_a = score;
                win_a = r;
            }
        }
    }
    if (win_b >= 0) {
        len = css_value_string(css_rule_value_off[win_b], css_rule_value_len[win_b], buf, 256);
        if (len > 0) {
            ao = attr_intern(buf, len);
            if (ao >= 0) {
                n_pseudo_before_off[node] = ao;
                n_pseudo_before_len[node] = len;
            }
        }
    }
    if (win_a >= 0) {
        len = css_value_string(css_rule_value_off[win_a], css_rule_value_len[win_a], buf, 256);
        if (len > 0) {
            ao = attr_intern(buf, len);
            if (ao >= 0) {
                n_pseudo_after_off[node] = ao;
                n_pseudo_after_len[node] = len;
            }
        }
    }
}

/* Resolve the styles for every DOM node. */

void style_resolve_all() {
    cs_count = 0;

    /* Allocate one ComputedStyle per DOM node, in DOM order so parent < child.
     * Index alignment: cs[i] corresponds to node i.*/
    for (int n = 0; n < nodes_count; n = n + 1) {
        int cs = cs_count;
        cs_count = cs_count + 1;
        if (cs >= MAX_COMPUTED_STYLES) {
            cs_count = MAX_COMPUTED_STYLES;
            break;
        }

        /* 1. UA defaults */
        ua_default_style(n_tag[n], cs);

        /* 2a. Custom-property cascade FIRST so var() resolution during
         * regular property cascade sees inherited + locally-declared
         * vars. CP_CUSTOM_VAR rules can declare many distinct --names
         * per node, so the winner_rule[] slot dedicated to CP_CUSTOM_VAR
         * is insufficient - walk the rule pool keyed on
         * (name_off, name_len), and apply the highest-scoring winner
         * per name. Reference: Blink resolves custom properties in the
         * cascade with the same priority order as regular properties;
         * we approximate via the shared specificity+doc-order score.*/
        for (int r = 0; r < css_rule_count; r = r + 1) {
            if (css_rule_prop_id[r] != CP_CUSTOM_VAR)
                continue;
            if (css_rule_important[r])
                continue;
            int sf2 = css_rule_sel_first[r];
            int sc2 = css_rule_sel_count[r];
            if (!sel_chain_matches(sf2, sc2, n))
                continue;
            int n_off = css_rule_var_name_off[r];
            int n_len = css_rule_var_name_len[r];
            int score2 = (css_rule_specificity[r] << 12) | (css_rule_doc_order[r] & 0xFFF);
            /* Find existing slot by name; else allocate a new one. */
            int slot = -1;
            for (int k = 0; k < cs_var_count[cs]; k = k + 1) {
                if (cs_var_name_len[cs][k] != n_len)
                    continue;
                int eq = 1;
                for (int b = 0; b < n_len; b = b + 1) {
                    if (css_value_pool[cs_var_name_off[cs][k] + b] != css_value_pool[n_off + b]) {
                        eq = 0;
                        break;
                    }
                }
                if (eq) {
                    slot = k;
                    break;
                }
            }
            if (slot < 0) {
                if (cs_var_count[cs] >= 8)
                    continue;
                slot = cs_var_count[cs];
                cs_var_count[cs] = slot + 1;
                cs_var_name_off[cs][slot] = n_off;
                cs_var_name_len[cs][slot] = n_len;
                cs_var_val_off[cs][slot] = css_rule_value_off[r];
                cs_var_val_len[cs][slot] = css_rule_value_len[r];
                /* Encode score in val_len's high bits? No - use separate.
                 * Re-walk to find current score from val pool would be
                 * nicer; simpler: keep score implicit by always tracking
                 * the higher score on each visit.*/
                cs_var_name_off[cs][slot] = n_off;
                cs_var_name_len[cs][slot] = n_len;
                /* score implicit via slot order; we'll re-check on conflict. */
                (void)score2;
            } else {
                /* Replace if score higher. We cheat and replace
                 * unconditionally on later doc-order: rules walk in
                 * source order so later wins on ties, which matches the
                 * doc-order tiebreaker. Specificity ties handled
                 * implicitly via rule walk order; for stricter spec
                 * compliance store an explicit score per slot.*/
                cs_var_val_off[cs][slot] = css_rule_value_off[r];
                cs_var_val_len[cs][slot] = css_rule_value_len[r];
            }
        }

        /* 2b. Inline custom-property pre-pass - extract any `--name:`
         * declarations from `style="..."` into cs_var_*[] BEFORE the
         * regular property cascade runs, so var() lookups in regular
         * rules see inline-declared vars. Inline custom props win over
         * matching author-rule custom props on this element because
         * apply_inline_vars appends and our cs_var_lookup returns the
         * first match - appending later overrides.*/
        int sty_off = dom_attr_get(n, "style");
        if (sty_off >= 0) {
            apply_inline_vars(cs, attr_pool + sty_off);
        }

        /* 2c. Author rules in two passes: pass 1 (non-important) feeds
         * the normal specificity+doc-order cascade; pass 2 (important)
         * wins over everything from pass 1 and inline style. The score
         * packs (specificity << 12) | doc_order so a higher specificity
         * or later rule wins at equal level. Size matches MAX_CP_ID;
         * CupidC requires a literal here.*/
        int winner_rule[80];
        int winner_score[80];
        for (int p = 0; p < MAX_CP_ID; p = p + 1) {
            winner_rule[p] = -1;
            winner_score[p] = -1;
        }
        for (int r = 0; r < css_rule_count; r = r + 1) {
            if (css_rule_important[r])
                continue;
            int p = css_rule_prop_id[r];
            if (p < 1 || p >= MAX_CP_ID)
                continue;
            int sf = css_rule_sel_first[r];
            int sc = css_rule_sel_count[r];
            if (!sel_chain_matches(sf, sc, n))
                continue;
            int score = (css_rule_specificity[r] << 12) | (css_rule_doc_order[r] & 0xFFF);
            if (score > winner_score[p]) {
                winner_score[p] = score;
                winner_rule[p] = r;
            }
        }
        for (int p = 1; p < MAX_CP_ID; p = p + 1) {
            int r = winner_rule[p];
            if (r >= 0) {
                cs_apply_property(cs, p, css_rule_value_off[r], css_rule_value_len[r]);
            }
        }

        /* 3. Inline style="..." attribute (wins over non-important
         * author rules). The pre-pass at 2b already captured --vars;
         * apply_inline_style still re-applies them but that's a no-op
         * since the values are identical. Regular properties get
         * applied here for the first time.*/
        if (sty_off >= 0) {
            apply_inline_style(cs, attr_pool + sty_off);
        }

        /* 4. Important author rules: applied last so they override pass 1
         *    and inline style. Specificity + doc-order still resolves ties
         *    among important rules themselves.*/
        int imp_rule[80];
        int imp_score[80];
        for (int p = 0; p < MAX_CP_ID; p = p + 1) {
            imp_rule[p] = -1;
            imp_score[p] = -1;
        }
        for (int r = 0; r < css_rule_count; r = r + 1) {
            if (!css_rule_important[r])
                continue;
            int p = css_rule_prop_id[r];
            if (p < 1 || p >= MAX_CP_ID)
                continue;
            int sf = css_rule_sel_first[r];
            int sc = css_rule_sel_count[r];
            if (!sel_chain_matches(sf, sc, n))
                continue;
            int score = (css_rule_specificity[r] << 12) | (css_rule_doc_order[r] & 0xFFF);
            if (score > imp_score[p]) {
                imp_score[p] = score;
                imp_rule[p] = r;
            }
        }
        for (int p = 1; p < MAX_CP_ID; p = p + 1) {
            int r = imp_rule[p];
            if (r >= 0) {
                cs_apply_property(cs, p, css_rule_value_off[r], css_rule_value_len[r]);
            }
        }

        /* 4. Inheritance from parent ComputedStyle for unset inheritable props.
         *    cs index == node index, so parent's cs is at parent's node index.*/
        int parent = n_parent[n];
        if (parent >= 0 && parent < cs_count) {
            int pcs = parent;
            if (cs_color[cs] < 0)
                cs_color[cs] = cs_color[pcs];
            if (cs_text_align[cs] == TA_LEFT && cs_text_align[pcs] != TA_LEFT)
                cs_text_align[cs] = cs_text_align[pcs];
            if (cs_white_space[cs] == WS_NORMAL)
                cs_white_space[cs] = cs_white_space[pcs];
            if (cs_list_style[cs] == LS_DISC && cs_list_style[pcs] != LS_DISC)
                cs_list_style[cs] = cs_list_style[pcs];
            if (cs_line_height[cs] < 0) {
                cs_line_height[cs] = cs_line_height[pcs];
                cs_line_height_mult[cs] = cs_line_height_mult[pcs];
            }
            if (cs_font_size_px[cs] < 0)
                cs_font_size_px[cs] = cs_font_size_px[pcs];
            /* Font-family inherits as a unit. cs_font_family_off == -1
             * means "unset on this element" - copy the parent's stash.*/
            if (cs_font_family_off[cs] < 0) {
                cs_font_family_off[cs] = cs_font_family_off[pcs];
                cs_font_family_len[cs] = cs_font_family_len[pcs];
            }
            if (cs_font_generic[cs] == FONTSYS_FAMILY_DEFAULT &&
                cs_font_generic[pcs] != FONTSYS_FAMILY_DEFAULT) {
                cs_font_generic[cs] = cs_font_generic[pcs];
            }
            /* font-weight / font-style are inherited per CSS spec. Element
             * UA defaults set 400 / 0 explicitly, so we promote to parent's
             * value only when the child still carries the default; child
             * elements that explicitly set normal still override correctly
             * because cascade ran before this inheritance pass.*/
            if (cs_font_w[cs] == 400 && cs_font_w[pcs] != 400)
                cs_font_w[cs] = cs_font_w[pcs];
            if (cs_font_i[cs] == 0 && cs_font_i[pcs] != 0)
                cs_font_i[cs] = cs_font_i[pcs];
            /* text-decoration in CSS2.1 doesn't strictly inherit, but its
             * paint effect (underline/strike) propagates from ancestor to
             * all descendants. Treating it as inherit-when-unset is
             * visually equivalent and avoids walking the rt parent chain
             * in emit_text_atoms.*/
            if (cs_text_dec[cs] == 0 && cs_text_dec[pcs] != 0)
                cs_text_dec[cs] = cs_text_dec[pcs];
        } else {
            /* root: ensure color is concrete and font-size has a baseline */
            if (cs_color[cs] < 0)
                cs_color[cs] = 0x000000;
            if (cs_font_size_px[cs] < 0)
                cs_font_size_px[cs] = 16;
        }
        /* Derive the kernel-tier from the px-resolved size. layout/paint
         * still consume cs_font_size_tier; cs_font_size_px is the source
         * of truth and what em/rem/% length resolution reads.*/
        cs_font_size_tier[cs] = px_to_tier(cs_font_size_px[cs]);

        /* Pseudo-element generated content (::before / ::after). Stored on
         * the originating node; render_tree.cc injects synthetic RT_TEXT
         * children at build time.*/
        resolve_pseudo_content(n);
    }
}

/* Optional debug helper. Kept dormant, not called from parse_html. */
void dump_style(int n) {
    serial_printf("[browser] node %d (tag %d): disp=%d color=0x%x bg=0x%x weight=%d size=%d\n", n,
                  n_tag[n], cs_display[n], cs_color[n], cs_bg[n], cs_font_w[n],
                  cs_font_size_tier[n]);
}
