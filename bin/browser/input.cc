/* ---------- Hit testing + Input ---------- */

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
