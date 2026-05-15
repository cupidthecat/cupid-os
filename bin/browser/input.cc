/* Hit testing + Input */

/* Reverse-depth render-tree hit test: returns the deepest render node whose
 * paint rectangle contains (mx, my). Inputs are window-content-relative
 * coordinates (the same frame rt_screen_x/y produces, i.e. they already
 * account for viewport_y() and scroll_y).*/
int rt_hit(int n, int mx, int my) {
    int sx = rt_screen_x(n);
    int sy = rt_screen_y(n);
    if (mx < sx || mx >= sx + rt_w[n] || my < sy || my >= sy + rt_h[n]) return -1;
    /* Walk children deepest/last-first so later painters win on overlap. */
    int sibs[256];
    int nsib = 0;
    int c = rt_first_child[n];
    while (c >= 0 && nsib < 256) { sibs[nsib] = c; nsib = nsib + 1; c = rt_next[c]; }
    for (int k = nsib - 1; k >= 0; k--) {
        int h = rt_hit(sibs[k], mx, my);
        if (h >= 0) return h;
    }
    return n;
}

int hit_box(int mx, int my) {
    if (rt_count == 0) return -1;
    return rt_hit(0, mx, my);
}

/* Inline content is absorbed into RT_LINE_BOX siblings, so the original
 * <a> RT_INLINE node has no rt_w/h and the hit walk never visits it. Atoms
 * carry la_link_idx, so when a click lands on a LINE_BOX, look up which
 * atom column the click falls in.*/
int line_box_link_at(int n, int rel_ax) {
    int first = rt_line_atom_first[n];
    int count = rt_line_atom_count[n];
    for (int k = first; k < first + count; k++) {
        if (la_x[k] < 0) continue;
        if (rel_ax >= la_x[k] && rel_ax < la_x[k] + la_w[k]) {
            return la_link_idx[k];
        }
    }
    return -1;
}

/* If a click on a LINE_BOX lands on a replaced atom (la_text_off encodes
 * a negative RT-node ref), return that node so input/checkbox hits route
 * to the input element instead of stopping at the line_box. -1 if the
 * column is plain text.*/
int line_box_replaced_at(int n, int rel_ax) {
    int first = rt_line_atom_first[n];
    int count = rt_line_atom_count[n];
    for (int k = first; k < first + count; k++) {
        if (la_x[k] < 0) continue;
        if (rel_ax < la_x[k] || rel_ax >= la_x[k] + la_w[k]) continue;
        if (la_text_off[k] < 0) return -la_text_off[k] - 1;
        return -1;
    }
    return -1;
}

/* find the input/button form parent node */
int find_node_for_input(int ii) {
    if (ii < 0 || ii >= inputs_count) return -1;
    return input_node[ii];
}

int find_form_node(int input_node) {
    int p = n_parent[input_node];
    while (p >= 0) {
        if (n_tag[p] == T_FORM) return p;
        p = n_parent[p];
    }
    return -1;
}

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
        focus_mode = FOCUS_PAGE;
        if (fi >= 0 && fi < forms_count) submit_form(form_node[fi]);
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
    /* Ctrl-D = dump render tree + computed style to serial (debug). */
    if (ch == 4) {
        about_dump();
        b_strcpy_n(status_msg, "Dumped render tree to serial", 256);
        return;
    }
    if (sc == 72) { scroll_y = scroll_y - line_h * 2; clamp_scroll(); return; }
    if (sc == 80) { scroll_y = scroll_y + line_h * 2; clamp_scroll(); return; }
    if (sc == 73) { scroll_y = scroll_y - viewport_h() + line_h; clamp_scroll(); return; }
    if (sc == 81) { scroll_y = scroll_y + viewport_h() - line_h; clamp_scroll(); return; }
    if (sc == 71) { scroll_y = 0; return; }
    if (sc == 79) { scroll_y = doc_h; clamp_scroll(); return; }
    if (ch == 27) {
        /* Esc - close the window */
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
        /* Back / forward buttons: 22x20 rects starting at x=6 with a
         * 4 px gap.  Layout matches paint.cc:draw_address_bar.*/
        if (rel_x >= 6 && rel_x < 28) {
            if (hist_pos > 1) go_back();
            return;
        }
        if (rel_x >= 32 && rel_x < 54) {
            if (hist_pos < hist_count) go_forward();
            return;
        }
        focus_mode = FOCUS_ADDR;
        addr_cursor = addr_len;
        return;
    }
    /* viewport */
    if (rel_y >= viewport_y() && rel_y < viewport_y() + viewport_h() &&
        rel_x >= 0 && rel_x < cur_cw - 12) {
        int hit = hit_box(rel_x, rel_y);
        if (hit >= 0) {
            /* Walk parent chain looking for the nearest interactive node:
             * a link, a focusable input, or a submit-capable button/input.*/
            int link = -1;
            int input_idx = -1;
            int submit_form_node = -1;
            /* If hit landed on a LINE_BOX, the click may be on an atom that
             * came from an <a> or a replaced control. line_box covers the
             * full row even though the original RT_INLINE/RT_REPLACED
             * children sit at the same y, so without this redirect a
             * checkbox/input click would stop at the line_box and walk
             * up through the block parent without finding the form
             * control.*/
            if (rt_kind[hit] == RT_LINE_BOX) {
                int rel_ax = rel_x - rt_screen_x(hit);
                int li = line_box_link_at(hit, rel_ax);
                if (li >= 0) link = li;
                if (link < 0) {
                    int repl = line_box_replaced_at(hit, rel_ax);
                    if (repl >= 0) hit = repl;
                }
            }
            int toggle_dom = -1;
            int cur = (link >= 0) ? -1 : hit;
            while (cur >= 0) {
                if (rt_link_idx[cur] >= 0) { link = rt_link_idx[cur]; break; }
                if (rt_input_idx[cur] >= 0) { input_idx = rt_input_idx[cur]; break; }
                int dom = rt_dom[cur];
                if (dom >= 0) {
                    int tag = n_tag[dom];
                    if (tag == T_BUTTON) {
                        char *btyp = dom_attr_str(dom, "type");
                        /* default <button> behavior is submit */
                        if (btyp == 0 || b_strieq(btyp, "submit")) {
                            int fn = find_form_node(dom);
                            if (fn >= 0) { submit_form_node = fn; break; }
                        }
                    } else if (tag == T_INPUT) {
                        char *typ = dom_attr_str(dom, "type");
                        if (typ != 0 &&
                            (b_strieq(typ, "submit") || b_strieq(typ, "image"))) {
                            int fn = find_form_node(dom);
                            if (fn >= 0) { submit_form_node = fn; break; }
                        }
                        if (typ != 0 &&
                            (b_strieq(typ, "checkbox") || b_strieq(typ, "radio"))) {
                            toggle_dom = dom;
                            break;
                        }
                    }
                }
                cur = rt_parent[cur];
            }
            if (toggle_dom >= 0) {
                /* Checkbox toggles in place; radio sets self and clears
                 * other radios sharing the same `name`.*/
                char *typ = dom_attr_str(toggle_dom, "type");
                if (typ && b_strieq(typ, "radio")) {
                    int my_name = dom_attr_get(toggle_dom, "name");
                    if (my_name >= 0) {
                        for (int k = 0; k < nodes_count; k = k + 1) {
                            if (n_tag[k] != T_INPUT) continue;
                            char *t2 = dom_attr_str(k, "type");
                            if (!t2 || !b_strieq(t2, "radio")) continue;
                            int kn = dom_attr_get(k, "name");
                            if (kn == my_name) n_checkbox_state[k] = 0;
                        }
                    }
                    n_checkbox_state[toggle_dom] = 1;
                } else {
                    n_checkbox_state[toggle_dom] =
                        n_checkbox_state[toggle_dom] ? 0 : 1;
                }
                focus_mode = FOCUS_PAGE;
                return;
            }
            if (link >= 0) {
                char *u = attr_pool + link_url_off[link];
                char full[1024];
                compute_url_relative(u, full, URL_MAX);
                focus_mode = FOCUS_PAGE;
                navigate(full);
                return;
            }
            if (input_idx >= 0) {
                focus_mode = FOCUS_INPUT;
                focused_input = input_idx;
                return;
            }
            if (submit_form_node >= 0) {
                focus_mode = FOCUS_PAGE;
                submit_form(submit_form_node);
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
    hover_dom_node = -1;
    if (rel_y >= viewport_y() && rel_y < viewport_y() + viewport_h() &&
        rel_x >= 0 && rel_x < cur_cw - 12) {
        int hit = hit_box(rel_x, rel_y);
        if (hit >= 0) {
            /* Walk to find the deepest DOM-backed rt node and the nearest
             * link ancestor.*/
            int cur = hit;
            while (cur >= 0) {
                if (hover_dom_node < 0 && rt_dom[cur] >= 0) {
                    hover_dom_node = rt_dom[cur];
                }
                if (hover_link < 0 && rt_link_idx[cur] >= 0) {
                    hover_link = rt_link_idx[cur];
                }
                cur = rt_parent[cur];
            }
            if (rt_kind[hit] == RT_LINE_BOX) {
                int li = line_box_link_at(hit, rel_x - rt_screen_x(hit));
                if (li >= 0) hover_link = li;
            }
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

    /* If a rule actually references :hover/:focus, restyle and re-layout
     * when the hovered DOM node changes. Skipped when no dynamic pseudo
     * rule is active to avoid pointless work on every pixel of motion.*/
    if (css_has_dynamic_pseudo && hover_dom_node != prev_hover_dom_node) {
        prev_hover_dom_node = hover_dom_node;
        style_resolve_all();
        run_layout();
    }

    int dz = mouse_scroll();
    if (dz != 0) {
        scroll_y = scroll_y + dz * line_h * 3;
        clamp_scroll();
    }
    prev_buttons = btns;
}
