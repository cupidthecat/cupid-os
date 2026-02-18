//icon: "Terminal"
//icon_desc: "CupidOS Terminal"
//icon_x: 10
//icon_y: 10
//icon_type: app
//icon_color: 0x404040

int win;
int scale;
int scroll;
int blink_ms;
int cursor_on;
int last_frame_ms;

int clamp(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void main() {
  win = gui_win_create("Terminal", 80, 60, 560, 320);
  scale = 1;
  scroll = 0;
  blink_ms = uptime_ms();
  cursor_on = 1;
  last_frame_ms = 0;

  if (win == -1) {
    return;
  }

  while (gui_win_is_open(win)) {
    if (!gui_win_can_draw(win)) {
      yield();
      continue;
    }

    int key = gui_win_poll_key(win);
    while (key != -1) {
      int sc = (key >> 8) & 0xFF;
      int ch = key & 0xFF;
      int ctrl = keyboard_ctrl_held();

      if (ctrl && (ch == 43 || ch == 61)) {
        scale = clamp(scale + 1, 1, 3);
      } else if (ctrl && (ch == 45 || ch == 95)) {
        scale = clamp(scale - 1, 1, 3);
      } else if (sc == 73) {
        scroll = clamp(scroll + 5, 0, shell_cursor_y());
      } else if (sc == 81) {
        scroll = clamp(scroll - 5, 0, shell_cursor_y());
      } else {
        scroll = 0;
        cursor_on = 1;
        blink_ms = uptime_ms();
        shell_send_key(sc, ch);
      }

      key = gui_win_poll_key(win);
    }

    {
      int delta = mouse_scroll();
      if (delta != 0) {
        scroll = clamp(scroll - delta, 0, shell_cursor_y());
      }
    }

    {
      int now_frame = uptime_ms();
      if (last_frame_ms != 0 && now_frame - last_frame_ms < 16) {
        yield();
        continue;
      }
      last_frame_ms = now_frame;
    }

    {
      int cx = gui_win_content_x(win);
      int cy = gui_win_content_y(win);
      int cw = gui_win_content_w(win);
      int ch = gui_win_content_h(win);
      int cell = 8 * scale;
      int cols = cw / cell;
      int rows = ch / cell;
      int cursor_row = shell_cursor_y();
      int first_buf_row = cursor_row - rows - scroll + 1;

      if (first_buf_row < 0) {
        first_buf_row = 0;
      }

      /* 2px inner padding so text doesn't touch the window border */
      int pad = 2;
      int inner_w = cw - pad * 2;
      int inner_h = ch - pad * 2;
      int cols_pad = inner_w / cell;
      int rows_pad = inner_h / cell;
      if (cols_pad > cols) cols_pad = cols;
      if (rows_pad > rows) rows_pad = rows;

      gfx2d_rect_fill(cx, cy, cw, ch, 0x001E1E1E);

      int r = 0;
      while (r < rows_pad) {
        int buf_row = first_buf_row + r;
        if (buf_row >= 0 && buf_row < shell_buf_rows()) {
          int c = 0;
          while (c < cols_pad) {
            if (c < shell_buf_cols()) {
              int glyph = shell_buf_char(buf_row, c);
              int col = shell_buf_color(buf_row, c);
              int fg = col & 15;
              int bg = (col >> 4) & 15;
              /* fg=0 (ANSI black = 0x141418) is invisible on dark bg; treat as light gray */
              int dfg = fg;
              if (dfg == 0) dfg = 7;
              int px = cx + pad + c * cell;
              int py = cy + pad + r * cell;

              if (bg != 0) {
                gfx2d_rect_fill(px, py, cell, cell, ansi_color(bg));
              }
              if (glyph != 0 && glyph != 32) {
                gfx2d_char_scaled(px, py, glyph, ansi_color(dfg), scale);
              }
            }
            c = c + 1;
          }
        }
        r = r + 1;
      }

      {
        int vis_cursor_row = cursor_row - first_buf_row;
        if (cursor_on && vis_cursor_row >= 0 && vis_cursor_row < rows_pad) {
          int px = cx + pad + shell_cursor_x() * cell;
          int py = cy + pad + vis_cursor_row * cell;
          gfx2d_rect_fill(px, py, 3, cell, ansi_color(7));
        }
      }
    }

    {
      int now = uptime_ms();
      if (now - blink_ms > 500) {
        cursor_on = 1 - cursor_on;
        blink_ms = now;
      }
    }

    gui_win_draw_frame(win);
    gui_win_flip(win);
    yield();
  }

  gui_win_close(win);
}
