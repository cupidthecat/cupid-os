/* §3 Render tree builder */

int rt_alloc(int kind, int dom, int parent, int style_cs) {
    if (rt_count >= MAX_RT_NODES) return -1;
    int n = rt_count++;
    rt_dom[n] = dom;
    rt_parent[n] = parent;
    rt_first_child[n] = -1;
    rt_next[n] = -1;
    rt_kind[n] = kind;
    rt_style[n] = style_cs;
    rt_text_off[n] = 0;
    rt_text_len[n] = 0;
    /* rt_x/y/w/h must be zeroed even though layout_block writes them
     * for every block child. RT_LIST_MARKER is skipped by the layout
     * walk (markers live in the parent's padding-left and aren't laid
     * out in flow), so without this init the first marker on a fresh
     * page reuses whatever stale offset was at this slot from the
     * previous render. Visible symptom: first li's bullet jumps to
     * the right of its text while the rest of the list looks correct. */
    rt_x[n] = 0;
    rt_y[n] = 0;
    rt_w[n] = 0;
    rt_h[n] = 0;
    rt_intrinsic_w[n] = 0;
    rt_intrinsic_h[n] = 0;
    rt_is_oof[n] = 0;
    rt_is_fixed[n] = 0;
    rt_is_stack[n] = 0;
    rt_link_idx[n] = -1;
    rt_input_idx[n] = -1;
    rt_line_atom_first[n] = 0;
    rt_line_atom_count[n] = 0;
    if (parent >= 0) {
        if (rt_first_child[parent] < 0) {
            rt_first_child[parent] = n;
        } else {
            int sib = rt_first_child[parent];
            while (rt_next[sib] >= 0) sib = rt_next[sib];
            rt_next[sib] = n;
        }
    }
    return n;
}

int rt_kind_for_display(int disp) {
    if (disp == DISP_BLOCK)              return RT_BLOCK;
    if (disp == DISP_INLINE)             return RT_INLINE;
    if (disp == DISP_INLINE_BLOCK)       return RT_INLINE_BLOCK;
    if (disp == DISP_INLINE_FLEX)        return RT_INLINE_BLOCK;
    if (disp == DISP_FLEX)               return RT_BLOCK;
    if (disp == DISP_LIST_ITEM)          return RT_LIST_ITEM;
    if (disp == DISP_TABLE)              return RT_TABLE;
    if (disp == DISP_TABLE_ROW_GROUP)    return RT_TABLE_ROW_GROUP;
    if (disp == DISP_TABLE_ROW)          return RT_TABLE_ROW;
    if (disp == DISP_TABLE_CELL)         return RT_TABLE_CELL;
    if (disp == DISP_TABLE_CAPTION)      return RT_TABLE_CAPTION;
    return RT_INLINE;
}

int rt_kind_is_inline(int kind) {
    return kind == RT_INLINE || kind == RT_INLINE_BLOCK || kind == RT_TEXT ||
           kind == RT_REPLACED;
}

int rt_kind_is_block_level(int kind) {
    return kind == RT_BLOCK || kind == RT_LIST_ITEM ||
           kind == RT_TABLE || kind == RT_TABLE_ROW_GROUP ||
           kind == RT_TABLE_ROW || kind == RT_TABLE_CELL ||
           kind == RT_TABLE_CAPTION;
}

/* Helper: build children of `dom` under render parent `rt_parent_n`, inserting
 * anonymous block wrappers around contiguous inline runs whenever this parent
 * has at least one block-level child. */
void build_rt_children(int dom, int rt_parent_n) {
    /* Pre-scan: do we have a mix? */
    int has_block = 0;
    int c = n_first_child[dom];
    while (c >= 0) {
        if (n_tag[c] != T_TEXT) {
            int cs_c = c;     /* style index = DOM index */
            int disp = cs_display[cs_c];
            if (disp == DISP_NONE) { c = n_next[c]; continue; }
            int rk = rt_kind_for_display(disp);
            if (rt_kind_is_block_level(rk)) { has_block = 1; break; }
        }
        c = n_next[c];
    }

    int anon_block = -1;          /* current anon block wrapper, -1 if none open */
    c = n_first_child[dom];
    while (c >= 0) {
        if (n_tag[c] == T_TEXT) {
            /* Whitespace-only text between blocks is dropped per spec. */
            int ws = 1;
            for (int k = 0; k < n_text_len[c]; k++) {
                char ch = attr_pool[n_text_off[c] + k];
                if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') { ws = 0; break; }
            }
            if (ws && has_block) { c = n_next[c]; continue; }
            int target = rt_parent_n;
            if (has_block) {
                if (anon_block < 0) {
                    /* Anonymous block uses parent's style for inheritance */
                    anon_block = rt_alloc(RT_BLOCK, -1, rt_parent_n, rt_style[rt_parent_n]);
                }
                target = anon_block;
            }
            int rt_t = rt_alloc(RT_TEXT, c, target, c);
            if (rt_t < 0) return;
            rt_text_off[rt_t] = n_text_off[c];
            rt_text_len[rt_t] = n_text_len[c];
            c = n_next[c];
            continue;
        }
        int cs_c = c;
        int disp = cs_display[cs_c];
        if (disp == DISP_NONE) { c = n_next[c]; continue; }
        int rk = rt_kind_for_display(disp);

        if (rt_kind_is_block_level(rk)) {
            anon_block = -1;          /* close any open anon */
            int n = build_rt_subtree(c, rt_parent_n);
            (void)n;
        } else {
            int target = rt_parent_n;
            if (has_block) {
                if (anon_block < 0) {
                    anon_block = rt_alloc(RT_BLOCK, -1, rt_parent_n, rt_style[rt_parent_n]);
                }
                target = anon_block;
            }
            int n = build_rt_subtree(c, target);
            (void)n;
        }
        c = n_next[c];
    }
}

int build_rt_subtree(int dom, int rt_parent_n) {
    int cs_d = dom;       /* style index = DOM index */
    int disp = cs_display[cs_d];
    if (disp == DISP_NONE) return -1;

    /* REPLACED elements: <img>, <input>, <button>, <textarea> */
    int tag = n_tag[dom];
    int is_replaced = (tag == T_IMG || tag == T_INPUT || tag == T_BUTTON ||
                       tag == T_TEXTAREA);

    int kind = rt_kind_for_display(disp);
    if (is_replaced) kind = RT_REPLACED;

    int n = rt_alloc(kind, dom, rt_parent_n, cs_d);
    if (n < 0) return -1;

    /* For <a>: bind the link index, registering a new entry if this href
     * hasn't been seen yet. (Earlier code relied on layout to register
     * links, but no such pass existed - so links_count stayed 0 and every
     * <a> ended up with rt_link_idx == -1, breaking hover and click.) */
    if (tag == T_A) {
        int href_off = dom_attr_get(dom, "href");
        if (href_off >= 0) {
            int found = -1;
            for (int k = 0; k < links_count; k++) {
                if (link_url_off[k] == href_off) { found = k; break; }
            }
            if (found < 0 && links_count < 1024) {
                found = links_count;
                link_url_off[links_count] = href_off;
                links_count = links_count + 1;
            }
            rt_link_idx[n] = found;
        }
    }

    /* For <img>: intrinsic dims from HTML width/height attrs first, then
     * decoded pixel dimensions (n_img_intrinsic_*) once image.cc has
     * fetched the bytes, then 80x60 placeholder if neither is known. */
    if (tag == T_IMG) {
        int w_off = dom_attr_get(dom, "width");
        int h_off = dom_attr_get(dom, "height");
        int dw = -1, dh = -1;
        if (w_off >= 0) {
            int v = 0;
            char *s = attr_pool + w_off;
            for (int k = 0; s[k] >= '0' && s[k] <= '9'; k++) v = v*10 + (s[k]-'0');
            if (v > 0) dw = v;
        }
        if (h_off >= 0) {
            int v = 0;
            char *s = attr_pool + h_off;
            for (int k = 0; s[k] >= '0' && s[k] <= '9'; k++) v = v*10 + (s[k]-'0');
            if (v > 0) dh = v;
        }
        int decoded_w = n_img_intrinsic_w[dom];
        int decoded_h = n_img_intrinsic_h[dom];
        if (dw < 0 && decoded_w > 0) dw = decoded_w;
        if (dh < 0 && decoded_h > 0) dh = decoded_h;
        /* If only one axis specified, scale the other to preserve aspect
         * ratio when we have decoded pixels. */
        if (dw > 0 && dh < 0 && decoded_w > 0 && decoded_h > 0) {
            dh = (decoded_h * dw) / decoded_w;
        }
        if (dh > 0 && dw < 0 && decoded_w > 0 && decoded_h > 0) {
            dw = (decoded_w * dh) / decoded_h;
        }
        rt_intrinsic_w[n] = (dw > 0) ? dw : 80;
        rt_intrinsic_h[n] = (dh > 0) ? dh : 60;
    }

    /* For <input>: bind input index, set intrinsic size. Checkbox/radio
     * are small square boxes (paint draws their own chrome); text-style
     * inputs are wider and shorter. The DOM `type` attr drives both the
     * input-table eligibility (in parser.cc) and intrinsic dimensions. */
    if (tag == T_INPUT) {
        for (int k = 0; k < inputs_count; k++) {
            if (input_node[k] == dom) { rt_input_idx[n] = k; break; }
        }
        char *type_s = dom_attr_str(dom, "type");
        int is_check = 0;
        if (type_s && (b_strieq(type_s, "checkbox") || b_strieq(type_s, "radio"))) is_check = 1;
        if (is_check) {
            /* 16x16 reads as a normal checkbox at 14px body text and
             * lines up close to Chrome's intrinsic (13-16). At the old
             * 14x14 the 1px border on each side left a 12x12 fill that
             * could read as a faint mark. */
            rt_intrinsic_w[n] = 16;
            rt_intrinsic_h[n] = 16;
        } else {
            rt_intrinsic_w[n] = 140;
            rt_intrinsic_h[n] = 18;
        }
    }
    if (tag == T_BUTTON) {
        rt_intrinsic_w[n] = 64;
        rt_intrinsic_h[n] = 18;
    }

    /* For <li>: insert a LIST_MARKER child first */
    if (kind == RT_LIST_ITEM) {
        int marker = rt_alloc(RT_LIST_MARKER, -1, n, cs_d);
        (void)marker;
    }

    /* Replaced elements have no children rendered */
    if (!is_replaced) {
        /* ::before generated content: synthetic RT_TEXT prepended ahead
         * of the element's real children. Inherits the element's cs slot
         * for color/font; spec requires a separate inline box per pseudo
         * but for v1 a flat text node is visually correct for simple
         * `content: "..."` strings. */
        if (n_pseudo_before_off[dom] >= 0) {
            int rt_pe = rt_alloc(RT_TEXT, dom, n, cs_d);
            if (rt_pe >= 0) {
                rt_text_off[rt_pe] = n_pseudo_before_off[dom];
                rt_text_len[rt_pe] = n_pseudo_before_len[dom];
            }
        }
        build_rt_children(dom, n);
        if (n_pseudo_after_off[dom] >= 0) {
            int rt_pe = rt_alloc(RT_TEXT, dom, n, cs_d);
            if (rt_pe >= 0) {
                rt_text_off[rt_pe] = n_pseudo_after_off[dom];
                rt_text_len[rt_pe] = n_pseudo_after_len[dom];
            }
        }
    }
    return n;
}

/* Anonymous-table-ancestor wrapping. For Plan 2, table layout is the same as
 * block fallback (no real grid), so anon-table wrappers cosmetically don't
 * matter - they're just additional block boxes. Skip the wrap for Plan 2;
 * Plan 3 implements proper anon-table-ancestor logic alongside table layout. */
void rt_anon_table_fixup() {
    (void)0;
}

/* Walk freshly-built render tree, append every stacking-context
 * participant to rt_oof_list. Two participant kinds:
 *   - position: absolute | fixed - layout's second pass resolves x/y;
 *     paint draws in z-index order with rt_is_oof set.
 *   - position: relative WITH explicit z-index - stays in flow
 *     (no second layout pass), but z-sorts in paint with siblings.
 * CSS 2.1 §9.9.1: "An element forms a stacking context if it has a
 * position value other than static and a z-index value other than
 * auto." Both kinds get rt_is_stack so the in-flow paint walk skips
 * them. */
void rt_collect_oof(int n) {
    if (n < 0) return;
    int sty = rt_style[n];
    int pos = cs_position[sty];
    int has_z = (cs_pos_set[sty] & 16) != 0;
    int is_oof = (pos == POS_ABSOLUTE || pos == POS_FIXED);
    int is_rel_z = (pos == POS_RELATIVE && has_z);
    if ((is_oof || is_rel_z) && rt_oof_count < 1024) {
        rt_oof_list[rt_oof_count] = n;
        rt_oof_count = rt_oof_count + 1;
        rt_is_stack[n] = 1;
    }
    int c = rt_first_child[n];
    while (c >= 0) { rt_collect_oof(c); c = rt_next[c]; }
}

void build_render_tree() {
    rt_count = 0;
    rt_oof_count = 0;
    la_count = 0;
    links_count = 0;     /* rebuilt as <a> nodes are walked below */
    /* Synthetic RT root mirrors DOM root (DOM index 0 = T_ROOT) */
    int root = rt_alloc(RT_BLOCK, 0, -1, 0);
    if (root < 0) return;
    build_rt_children(0, root);
    rt_anon_table_fixup();
    rt_collect_oof(root);
}

/* Dormant debug helper (kept for future use, not called) */
void dump_rt(int n, int depth) {
    char *kn = "?";
    if (rt_kind[n] == RT_BLOCK)             kn = "BLOCK";
    else if (rt_kind[n] == RT_INLINE)       kn = "INLINE";
    else if (rt_kind[n] == RT_INLINE_BLOCK) kn = "INLINE_BLOCK";
    else if (rt_kind[n] == RT_TEXT)         kn = "TEXT";
    else if (rt_kind[n] == RT_LIST_ITEM)    kn = "LI";
    else if (rt_kind[n] == RT_LIST_MARKER)  kn = "MARKER";
    else if (rt_kind[n] == RT_REPLACED)     kn = "REPLACED";
    else if (rt_kind[n] == RT_TABLE)        kn = "TABLE";
    else if (rt_kind[n] == RT_TABLE_ROW)    kn = "TR";
    else if (rt_kind[n] == RT_TABLE_CELL)   kn = "TD";
    serial_printf("[rt] %d%*s %s dom=%d tag=%d txtlen=%d\n",
                  depth, depth*2, "", kn, rt_dom[n],
                  rt_dom[n] >= 0 ? n_tag[rt_dom[n]] : 0,
                  rt_text_len[n]);
    int c = rt_first_child[n];
    while (c >= 0) { dump_rt(c, depth + 1); c = rt_next[c]; }
}
