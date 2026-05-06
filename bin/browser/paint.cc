/* ---------- Render ---------- */

int viewport_x() { return 0; }
int viewport_y() { return ADDR_H + 1; }
int viewport_h() {
    int h = cur_ch - ADDR_H - STATUS_H - 2;
    if (h < 60) h = 60;
    return h;
}

void draw_text_box(int bi, int sx, int sy) {
    int bx = b_x[bi];
    int by = b_y[bi] - scroll_y;
    int bw = b_w[bi];
    int bh = b_h[bi];
    if (by + bh < 0 || by >= viewport_h()) return;

    if (b_bg[bi] >= 0) {
        gfx2d_rect_fill(sx + bx, sy + by, bw, bh, b_bg[bi]);
    }
    if (b_text_off[bi] == -2) {
        /* bullet */
        gfx2d_circle_fill(sx + bx + 3, sy + by + line_h / 2, 2, b_fg[bi]);
        return;
    }
    if (b_text_off[bi] >= 0 && b_text_len[bi] > 0) {
        char tmp[256];
        int ml = b_text_len[bi];
        if (ml > 255) ml = 255;
        int k = 0;
        char *src = attr_pool + b_text_off[bi];
        while (k < ml) { tmp[k] = src[k]; k = k + 1; }
        tmp[ml] = 0;
        gfx2d_text(sx + bx, sy + by, tmp, b_fg[bi], 0);
        if (b_bold[bi]) {
            gfx2d_text(sx + bx + 1, sy + by, tmp, b_fg[bi], 0);
        }
        if (b_underline[bi]) {
            gfx2d_hline(sx + bx, sy + by + line_h - 1, bw, b_fg[bi]);
        }
    }
}

void draw_input_box(int bi, int sx, int sy) {
    int bx = b_x[bi]; int by = b_y[bi] - scroll_y;
    int bw = b_w[bi]; int bh = b_h[bi];
    if (by + bh < 0 || by >= viewport_h()) return;
    gfx2d_rect_fill(sx + bx, sy + by, bw, bh, b_bg[bi]);
    gfx2d_rect(sx + bx, sy + by, bw, bh, 0x808080);
    int ii = b_input_idx[bi];
    if (ii >= 0 && ii < MAX_INPUTS) {
        char *v = input_value + ii * 128;
        int max_chars = (bw - 4) / char_w;
        char tmp[128];
        int kl = 0;
        while (v[kl] && kl < max_chars && kl < 127) {
            tmp[kl] = v[kl]; kl = kl + 1;
        }
        tmp[kl] = 0;
        gfx2d_text(sx + bx + 2, sy + by + 2, tmp, b_fg[bi], 0);
        if (focus_mode == FOCUS_INPUT && focused_input == ii) {
            int cx = sx + bx + 2 + kl * char_w;
            gfx2d_vline(cx, sy + by + 2, line_h, b_fg[bi]);
        }
    }
}

void draw_button_box(int bi, int sx, int sy) {
    int bx = b_x[bi]; int by = b_y[bi] - scroll_y;
    int bw = b_w[bi]; int bh = b_h[bi];
    if (by + bh < 0 || by >= viewport_h()) return;
    gfx2d_rect_fill(sx + bx, sy + by, bw, bh, b_bg[bi]);
    gfx2d_rect(sx + bx, sy + by, bw, bh, 0x404040);
    if (b_text_off[bi] >= 0 && b_text_len[bi] > 0) {
        char tmp[64];
        int ml = b_text_len[bi];
        if (ml > 63) ml = 63;
        int k = 0;
        char *src = attr_pool + b_text_off[bi];
        while (k < ml) { tmp[k] = src[k]; k = k + 1; }
        tmp[ml] = 0;
        int tx = sx + bx + (bw - ml * char_w) / 2;
        gfx2d_text(tx, sy + by + 3, tmp, b_fg[bi], 0);
    } else {
        gfx2d_text(sx + bx + 4, sy + by + 3, "Submit", b_fg[bi], 0);
    }
}

void draw_image_box(int bi, int sx, int sy) {
    int bx = b_x[bi]; int by = b_y[bi] - scroll_y;
    int bw = b_w[bi]; int bh = b_h[bi];
    if (by + bh < 0 || by >= viewport_h()) return;
    if (b_img_handle[bi] >= 0) {
        gfx2d_image_draw_scaled(b_img_handle[bi], sx + bx, sy + by, bw, bh);
    } else {
        gfx2d_rect_fill(sx + bx, sy + by, bw, bh, b_bg[bi]);
        gfx2d_rect(sx + bx, sy + by, bw, bh, b_fg[bi]);
        gfx2d_text(sx + bx + 4, sy + by + 4, "[img]", b_fg[bi], 0);
    }
}

void draw_address_bar(int sx, int sy, int sw) {
    int bg = (focus_mode == FOCUS_ADDR) ? 0xFFFFE0 : 0xF0F0F0;
    gfx2d_rect_fill(sx, sy, sw, ADDR_H, bg);
    gfx2d_hline(sx, sy + ADDR_H - 1, sw, 0x808080);
    gfx2d_text(sx + 4, sy + 6, "URL:", 0x404040, 0);
    char tmp[128];
    int ml = addr_len;
    if (ml > 127) ml = 127;
    int k = 0;
    while (k < ml) { tmp[k] = addr_buf[k]; k = k + 1; }
    tmp[ml] = 0;
    gfx2d_text(sx + 4 + 4 * char_w + 4, sy + 6, tmp, 0x000000, 0);
    if (focus_mode == FOCUS_ADDR) {
        int cx = sx + 4 + 4 * char_w + 4 + addr_cursor * char_w;
        gfx2d_vline(cx, sy + 4, ADDR_H - 8, 0x000000);
    }
}

void draw_status_bar(int sx, int sy, int sw) {
    gfx2d_rect_fill(sx, sy, sw, STATUS_H, 0xE0E0E0);
    gfx2d_hline(sx, sy, sw, 0x808080);
    char *m = status_msg;
    if (hover_link >= 0 && hover_link < links_count) {
        m = attr_pool + link_url_off[hover_link];
    }
    char tmp[80];
    int k = 0;
    while (m[k] && k < 79) { tmp[k] = m[k]; k = k + 1; }
    tmp[k] = 0;
    gfx2d_text(sx + 4, sy + 4, tmp, 0x202020, 0);
}

void draw_scrollbar(int sx, int sy) {
    int sb_x = sx + cur_cw - 12;
    int sb_y = sy;
    int sb_h = viewport_h();
    gfx2d_rect_fill(sb_x, sb_y, 12, sb_h, 0xE0E0E0);
    if (doc_h <= sb_h) return;
    int thumb_h = (sb_h * sb_h) / doc_h;
    if (thumb_h < 16) thumb_h = 16;
    int max_scroll = doc_h - sb_h;
    int thumb_y = sb_y + ((sb_h - thumb_h) * scroll_y) / max_scroll;
    gfx2d_rect_fill(sb_x + 2, thumb_y, 8, thumb_h, 0x808080);
}

void render() {
    if (gui_win_begin_paint(win) != 0) return;
    /* Drawing inside begin_paint targets the window's offscreen surface
     * which has its own (0,0) origin; do NOT use gui_win_content_x/y
     * here.  Mouse handlers translate screen coords back to surface coords
     * separately. */
    int cx = 0;
    int cy = 0;

    /* background */
    gfx2d_rect_fill(cx, cy, cur_cw, cur_ch, page_bg);

    /* address bar */
    draw_address_bar(cx, cy, cur_cw);

    /* viewport (clipped) */
    int vx = cx + viewport_x();
    int vy = cy + viewport_y();
    gfx2d_clip_set(vx, vy, cur_cw - 12, viewport_h());

    int i = 0;
    while (i < boxes_count) {
        int kind = b_kind[i];
        if (kind == BK_TEXT) draw_text_box(i, vx, vy);
        else if (kind == BK_INPUT) draw_input_box(i, vx, vy);
        else if (kind == BK_BUTTON) draw_button_box(i, vx, vy);
        else if (kind == BK_IMG) draw_image_box(i, vx, vy);
        else if (kind == BK_RECT) {
            int by = b_y[i] - scroll_y;
            if (by + b_h[i] >= 0 && by < viewport_h())
                gfx2d_rect_fill(vx + b_x[i], vy + by, b_w[i], b_h[i], b_fg[i]);
        } else if (kind == BK_HRULE) {
            int by = b_y[i] - scroll_y;
            if (by >= 0 && by < viewport_h())
                gfx2d_hline(vx + b_x[i], vy + by, b_w[i], b_fg[i]);
        }
        i = i + 1;
    }

    gfx2d_clip_clear();

    /* scrollbar */
    draw_scrollbar(cx, cy + viewport_y());

    /* status bar */
    draw_status_bar(cx, cy + cur_ch - STATUS_H, cur_cw);

    gui_win_end_paint(win);
    gui_win_present(win);
}

void error_page(char *msg) {
    nodes_count = 0;
    attr_pool_pos = 1;
    boxes_count = 0;
    L_x = 8; L_y = 20;
    L_line_h = line_h;
    L_max_w = legacy_viewport_w();
    L_left_margin = 8;
    page_bg = 0xFFE8E8;
    page_fg = 0x000000;
    /* Lay out the message manually */
    legacy_emit_box(BK_TEXT);
    int bi = legacy_last_box();
    int off = attr_intern(msg, b_strlen(msg));
    b_x[bi] = 8; b_y[bi] = 16;
    b_w[bi] = b_strlen(msg) * char_w;
    b_h[bi] = line_h;
    b_text_off[bi] = off; b_text_len[bi] = b_strlen(msg);
    b_fg[bi] = 0x800000; b_bg[bi] = -1;
    b_link_idx[bi] = -1; b_input_idx[bi] = -1; b_img_handle[bi] = -1;
    b_bold[bi] = 1; b_underline[bi] = 0;
    doc_h = 40;
}
