//icon: "Notepad"
//icon_desc: "CupidC Text Editor"
//icon_x: 200
//icon_y: 250
//icon_type: app
//icon_color: 0xFFFFAA

#include "ctxt.cc"

int win;

int lines[4096];
int line_lens[4096];
int line_count;

int cursor_line;
int cursor_col;
int scroll_y;
int scroll_x;
int font_scale;

int sel_active;
int sel_sl;
int sel_sc;
int sel_el;
int sel_ec;

int undo_lines[4096];
int undo_lens[4096];
int undo_count;
int undo_cl;
int undo_cc;
int undo_avail;

int active_menu;
int cursor_on;
int blink_ms;
int modified;

char filename[256];
char save_path_alt[256];

int is_ctxt;
int render_mode;
int ctxt_sy;
int ctxt_sx;

int prev_buttons;
int drag_sel;
int should_close;
int mouse_lmb_latch;
int sb_dragging;
int sb_drag_off;
int hb_dragging;
int hb_drag_off;

char file_buf[32768];
char clip_buf[4096];

int COL_BG;
int COL_TEXT;
int COL_CURSOR;
int COL_SEL_BG;
int COL_MENUBAR;
int COL_MENU_TEXT;
int COL_MENU_HOV;
int COL_STATUSBAR;
int COL_STATUS_TEXT;
int COL_SCROLLBAR;
int COL_THUMB;

void np_strcpy(char *dst, char *src, int max) {
  int i = 0;
  while (src[i] && i < max - 1) {
    dst[i] = src[i];
    i = i + 1;
  }
  dst[i] = 0;
}

int np_strlen(char *s) {
  int n = 0;
  while (s[n]) n = n + 1;
  return n;
}

int np_tolower(int ch) {
  if (ch >= 65 && ch <= 90) return ch + 32;
  return ch;
}

void np_to_lower_str(char *src, char *dst, int max) {
  int i = 0;
  if (max < 1) return;
  while (src[i] && i < max - 1) {
    dst[i] = np_tolower(src[i]);
    i = i + 1;
  }
  dst[i] = 0;
}

int np_readable_file(char *path) {
  int fd = vfs_open(path, 0);
  if (fd < 0) return 0;
  vfs_close(fd);
  return 1;
}

int np_swap_txt_to_ctxt(char *src, char *dst, int max) {
  int n = np_strlen(src);
  if (n < 5 || max < 6) return 0;
  np_strcpy(dst, src, max);
  if (src[n - 4] == 46 &&
      np_tolower(src[n - 3]) == 't' &&
      np_tolower(src[n - 2]) == 'x' &&
      np_tolower(src[n - 1]) == 't') {
    dst[n - 3] = 'C';
    dst[n - 2] = 'T';
    dst[n - 1] = 'X';
    dst[n] = 'T';
    dst[n + 1] = 0;
    return 1;
  }
  if (src[n - 5] == 46 &&
      np_tolower(src[n - 4]) == 't' &&
      np_tolower(src[n - 3]) == 'x' &&
      np_tolower(src[n - 2]) == 't' &&
      np_tolower(src[n - 1]) == 'x') {
    dst[n - 4] = 'C';
    dst[n - 3] = 'T';
    dst[n - 2] = 'X';
    dst[n - 1] = 'T';
    dst[n] = 0;
    return 1;
  }
  return 0;
}

void np_ext_to_lower(char *src, char *dst, int max) {
  int n = np_strlen(src);
  int dot = -1;
  int i = 0;
  np_strcpy(dst, src, max);
  while (src[i]) {
    if (src[i] == 46) dot = i;
    i = i + 1;
  }
  if (dot < 0) return;
  i = dot + 1;
  while (dst[i]) {
    dst[i] = np_tolower(dst[i]);
    i = i + 1;
  }
  (void)n;
}

void np_path_join(char *base, char *name, char *out, int max) {
  int bi = 0;
  int oi = 0;
  int need_slash = 0;
  if (max < 2) return;
  if (base == 0 || base[0] == 0) {
    np_strcpy(out, name, max);
    return;
  }
  while (base[bi] && oi < max - 1) {
    out[oi] = base[bi];
    oi = oi + 1;
    bi = bi + 1;
  }
  if (oi > 0 && out[oi - 1] != 47) need_slash = 1;
  if (need_slash && oi < max - 1) {
    out[oi] = 47;
    oi = oi + 1;
  }
  out[oi] = 0;
  np_strcpy(out + oi, name, max - oi);
}

int np_try_open_candidate(char *candidate) {
  char alt[256];
  char alt2[256];
  char low[256];
  char low2[256];
  alt[0] = 0;
  alt2[0] = 0;
  low[0] = 0;
  low2[0] = 0;
  serial_printf("[notepad.cc] link try: %s\n", candidate);
  if (np_readable_file(candidate)) {
    serial_printf("[notepad.cc] link open: %s\n", candidate);
    load_file(candidate);
    return 1;
  }
  np_to_lower_str(candidate, low, 256);
  if (low[0]) {
    serial_printf("[notepad.cc] link try lowercase: %s\n", low);
    if (np_readable_file(low)) {
      serial_printf("[notepad.cc] link open lowercase: %s\n", low);
      load_file(low);
      return 1;
    }
  }
  if (np_swap_txt_to_ctxt(candidate, alt, 256)) {
    serial_printf("[notepad.cc] link try fallback: %s\n", alt);
    if (np_readable_file(alt)) {
      serial_printf("[notepad.cc] link open fallback: %s\n", alt);
      load_file(alt);
      return 1;
    }
    np_to_lower_str(alt, low2, 256);
    if (low2[0]) {
      serial_printf("[notepad.cc] link try fallback: %s\n", low2);
      if (np_readable_file(low2)) {
        serial_printf("[notepad.cc] link open fallback: %s\n", low2);
        load_file(low2);
        return 1;
      }
    }
    np_ext_to_lower(alt, alt2, 256);
    if (alt2[0]) {
      serial_printf("[notepad.cc] link try fallback: %s\n", alt2);
      if (np_readable_file(alt2)) {
        serial_printf("[notepad.cc] link open fallback: %s\n", alt2);
        load_file(alt2);
        return 1;
      }
    }
  }
  return 0;
}

int np_open_link_target(char *target) {
  char path[256];
  int i = 0;
  int slash = -1;
  if (target[0] == 0) return 0;

  if (target[0] == 47) {
    if (np_try_open_candidate(target)) return 1;
  } else {
    resolve_link_path(target, path, 256);
    if (path[0] && np_try_open_candidate(path)) return 1;

    np_path_join("/", target, path, 256);
    if (np_try_open_candidate(path)) return 1;

    np_path_join("/home", target, path, 256);
    if (np_try_open_candidate(path)) return 1;

    np_path_join("/cupidos-txt", target, path, 256);
    if (np_try_open_candidate(path)) return 1;
  }

  while (filename[i]) {
    if (filename[i] == 47) slash = i;
    i = i + 1;
  }
  if (slash >= 0 && target[0] != 47) {
    path[0] = 0;
    i = 0;
    while (i <= slash && i < 255) {
      path[i] = filename[i];
      i = i + 1;
    }
    path[i] = 0;
    np_strcpy(path + i, target, 256 - i);
    if (np_try_open_candidate(path)) return 1;
  }

  return 0;
}

void resolve_link_path(char *target, char *out, int max) {
  int i = 0;
  int slash = -1;
  if (max < 2) return;
  if (target[0] == 0) {
    out[0] = 0;
    return;
  }
  if (target[0] == 47 || filename[0] == 0) {
    np_strcpy(out, target, max);
    return;
  }
  while (filename[i]) {
    if (filename[i] == 47) slash = i;
    i = i + 1;
  }
  if (slash < 0) {
    out[0] = 47;
    out[1] = 0;
    np_strcpy(out + 1, target, max - 1);
    return;
  }
  i = 0;
  while (i <= slash && i < max - 1) {
    out[i] = filename[i];
    i = i + 1;
  }
  out[i] = 0;
  np_strcpy(out + i, target, max - i);
}

void init_colors() {
  COL_BG = 0x001E1E1E;
  COL_TEXT = 0x00D4D4D4;
  COL_CURSOR = 0x00FFFFFF;
  COL_SEL_BG = 0x00264F78;
  COL_MENUBAR = 0x002D2D2D;
  COL_MENU_TEXT = 0x00D4D4D4;
  COL_MENU_HOV = 0x00094771;
  COL_STATUSBAR = 0x00007ACC;
  COL_STATUS_TEXT = 0x00FFFFFF;
  COL_SCROLLBAR = 0x003C3C3C;
  COL_THUMB = 0x00686868;
}

void clear_sel() { sel_active = 0; }

int get_cols(int width_px) { return width_px / (8 * font_scale); }

int get_rows(int content_h) {
  int usable = content_h - 12 - 10 - 12;
  if (usable < 8) usable = 8;
  return usable / (8 * font_scale);
}

int max_line_len() {
  int mx = 0;
  int i = 0;
  while (i < line_count) {
    if (line_lens[i] > mx) mx = line_lens[i];
    i = i + 1;
  }
  return mx;
}

void clamp_scroll_state(int ch_h, int cw) {
  int rows = get_rows(ch_h);
  int cols = get_cols(cw - 12);

  if (rows < 1) rows = 1;
  if (cols < 1) cols = 1;

  if (render_mode && is_ctxt) {
    int content = (ctxt_content_h() + 7) / 8;
    if (content < 1) content = 1;
    int max_top = content - rows;
    if (max_top < 0) max_top = 0;
    if (ctxt_sy < 0) ctxt_sy = 0;
    if (ctxt_sy > max_top * 8) ctxt_sy = max_top * 8;
    return;
  }

  {
    int max_top = line_count - rows;
    if (max_top < 0) max_top = 0;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_y > max_top) scroll_y = max_top;
  }

  {
    int max_x = max_line_len() - cols;
    if (max_x < 0) max_x = 0;
    if (scroll_x < 0) scroll_x = 0;
    if (scroll_x > max_x) scroll_x = max_x;
  }
}

void scrollbar_metrics(int ch_h, int cw, int *out_vx, int *out_vy, int *out_vh,
                      int *out_thumb_y, int *out_thumb_h, int *out_max_top) {
  int area_h = ch_h - 12 - 10;
  int vx = 0;
  int vy = 0;
  int vh = 0;
  int content = 1;
  int visible = get_rows(ch_h);
  int top = 0;
  int max_top = 0;
  int thumb_h = 8;
  int thumb_y = 0;

  if (visible < 1) visible = 1;

  vx = cw - 12;
  vy = 12;
  vh = area_h - 12;
  if (vh < 8) vh = 8;

  if (render_mode && is_ctxt) {
    content = (ctxt_content_h() + 7) / 8;
    top = ctxt_sy / 8;
  } else {
    content = line_count;
    top = scroll_y;
  }

  if (content < 1) content = 1;
  max_top = content - visible;
  if (max_top < 0) max_top = 0;
  if (top < 0) top = 0;
  if (top > max_top) top = max_top;

  thumb_h = (vh * visible) / content;
  if (thumb_h < 8) thumb_h = 8;
  if (thumb_h > vh) thumb_h = vh;

  if (max_top > 0 && vh > thumb_h) {
    thumb_y = vy + (top * (vh - thumb_h)) / max_top;
  } else {
    thumb_y = vy;
  }

  if (out_vx) *out_vx = vx;
  if (out_vy) *out_vy = vy;
  if (out_vh) *out_vh = vh;
  if (out_thumb_y) *out_thumb_y = thumb_y;
  if (out_thumb_h) *out_thumb_h = thumb_h;
  if (out_max_top) *out_max_top = max_top;
}

void set_scroll_top(int top_lines, int ch_h, int cw) {
  int rows = get_rows(ch_h);
  int content = 1;
  int max_top;
  if (rows < 1) rows = 1;

  if (render_mode && is_ctxt) content = (ctxt_content_h() + 7) / 8;
  else content = line_count;
  if (content < 1) content = 1;

  max_top = content - rows;
  if (max_top < 0) max_top = 0;

  if (top_lines < 0) top_lines = 0;
  if (top_lines > max_top) top_lines = max_top;

  if (render_mode && is_ctxt) ctxt_sy = top_lines * 8;
  else scroll_y = top_lines;

  clamp_scroll_state(ch_h, cw);
}

void hscrollbar_metrics(int ch_h, int cw, int *out_hx, int *out_hy, int *out_hw,
                       int *out_thumb_x, int *out_thumb_w, int *out_max_left) {
  int hx = 0;
  int hy = 0;
  int hw = 0;
  int content = 1;
  int visible = get_cols(cw - 12);
  int left = 0;
  int max_left = 0;
  int thumb_w = 8;
  int thumb_x = 0;

  if (visible < 1) visible = 1;

  hx = 0;
  hy = ch_h - 10 - 12;
  hw = cw - 12;
  if (hw < 8) hw = 8;

  if (render_mode && is_ctxt) {
    content = (ctxt_content_w() + 7) / 8;
    left = ctxt_sx / 8;
  } else {
    content = max_line_len();
    left = scroll_x;
  }

  if (content < 1) content = 1;
  max_left = content - visible;
  if (max_left < 0) max_left = 0;
  if (left < 0) left = 0;
  if (left > max_left) left = max_left;

  thumb_w = (hw * visible) / content;
  if (thumb_w < 8) thumb_w = 8;
  if (thumb_w > hw) thumb_w = hw;

  if (max_left > 0 && hw > thumb_w) {
    thumb_x = hx + (left * (hw - thumb_w)) / max_left;
  } else {
    thumb_x = hx;
  }

  if (out_hx) *out_hx = hx;
  if (out_hy) *out_hy = hy;
  if (out_hw) *out_hw = hw;
  if (out_thumb_x) *out_thumb_x = thumb_x;
  if (out_thumb_w) *out_thumb_w = thumb_w;
  if (out_max_left) *out_max_left = max_left;
}

void set_scroll_left(int left_cols, int ch_h, int cw) {
  int cols = get_cols(cw - 12);
  int content = 1;
  int max_left;
  if (cols < 1) cols = 1;

  if (render_mode && is_ctxt) content = (ctxt_content_w() + 7) / 8;
  else content = max_line_len();
  if (content < 1) content = 1;

  max_left = content - cols;
  if (max_left < 0) max_left = 0;

  if (left_cols < 0) left_cols = 0;
  if (left_cols > max_left) left_cols = max_left;

  if (render_mode && is_ctxt) ctxt_sx = left_cols * 8;
  else scroll_x = left_cols;

  clamp_scroll_state(ch_h, cw);
}

void ensure_cursor_visible(int rows, int cols) {
  if (cursor_line < scroll_y) scroll_y = cursor_line;
  if (cursor_line >= scroll_y + rows) scroll_y = cursor_line - rows + 1;
  if (cursor_col < scroll_x) scroll_x = cursor_col;
  if (cursor_col >= scroll_x + cols) scroll_x = cursor_col - cols + 1;
  if (scroll_y < 0) scroll_y = 0;
  if (scroll_x < 0) scroll_x = 0;
}

void free_buffer() {
  int i = 0;
  while (i < line_count) {
    if (lines[i] != 0) {
      kfree(lines[i]);
      lines[i] = 0;
    }
    i = i + 1;
  }
  line_count = 0;
}

void init_buffer() {
  int i = 0;
  while (i < 4096) {
    lines[i] = 0;
    line_lens[i] = 0;
    i = i + 1;
  }
  lines[0] = kmalloc(256);
  if (lines[0] != 0) {
    char *l0 = lines[0];
    l0[0] = 0;
  }
  line_lens[0] = 0;
  line_count = 1;
  cursor_line = 0;
  cursor_col = 0;
  scroll_y = 0;
  scroll_x = 0;
  modified = 0;
  filename[0] = 0;
  save_path_alt[0] = 0;
  undo_count = 0;
  undo_avail = 0;
  i = 0;
  while (i < 4096) {
    undo_lines[i] = 0;
    undo_lens[i] = 0;
    i = i + 1;
  }
  clear_sel();
}

void free_undo() {
  int i = 0;
  while (i < undo_count) {
    if (undo_lines[i] != 0) {
      kfree(undo_lines[i]);
      undo_lines[i] = 0;
    }
    i = i + 1;
  }
  undo_count = 0;
  undo_avail = 0;
  undo_cl = 0;
  undo_cc = 0;
}

void save_undo() {
  free_undo();
  int i = 0;
  while (i < line_count && i < 4096) {
    undo_lines[i] = kmalloc(256);
    if (undo_lines[i] != 0) {
      np_strcpy(undo_lines[i], lines[i], 256);
      undo_lens[i] = line_lens[i];
    }
    i = i + 1;
  }
  undo_count = line_count;
  undo_cl = cursor_line;
  undo_cc = cursor_col;
  undo_avail = 1;
}

void do_undo() {
  if (!undo_avail) return;
  free_buffer();
  int i = 0;
  while (i < undo_count) {
    lines[i] = kmalloc(256);
    if (lines[i] != 0) {
      np_strcpy(lines[i], undo_lines[i], 256);
      line_lens[i] = undo_lens[i];
    }
    i = i + 1;
  }
  line_count = undo_count;
  cursor_line = undo_cl;
  cursor_col = undo_cc;
  modified = 1;
  undo_avail = 0;
}

void insert_char(char ch) {
  if (cursor_line < 0 || cursor_line >= line_count) return;
  char *line = lines[cursor_line];
  int len = line_lens[cursor_line];
  if (len >= 254 || cursor_col > len) return;
  int i = len;
  while (i > cursor_col) {
    line[i] = line[i - 1];
    i = i - 1;
  }
  line[cursor_col] = ch;
  line[len + 1] = 0;
  line_lens[cursor_line] = len + 1;
  cursor_col = cursor_col + 1;
  modified = 1;
}

void delete_char_at_cursor() {
  if (cursor_line < 0 || cursor_line >= line_count) return;
  char *line = lines[cursor_line];
  int len = line_lens[cursor_line];

  if (cursor_col < len) {
    int i = cursor_col;
    while (i < len - 1) {
      line[i] = line[i + 1];
      i = i + 1;
    }
    line[len - 1] = 0;
    line_lens[cursor_line] = len - 1;
    modified = 1;
    return;
  }

  if (cursor_line + 1 < line_count) {
    char *next = lines[cursor_line + 1];
    int next_len = line_lens[cursor_line + 1];
    int i = 0;
    while (i < next_len && len + i < 254) {
      line[len + i] = next[i];
      i = i + 1;
    }
    line[len + i] = 0;
    line_lens[cursor_line] = len + i;
    kfree(lines[cursor_line + 1]);

    i = cursor_line + 1;
    while (i < line_count - 1) {
      lines[i] = lines[i + 1];
      line_lens[i] = line_lens[i + 1];
      i = i + 1;
    }
    line_count = line_count - 1;
    modified = 1;
  }
}

void do_backspace() {
  if (cursor_col > 0) {
    cursor_col = cursor_col - 1;
    delete_char_at_cursor();
  } else if (cursor_line > 0) {
    cursor_line = cursor_line - 1;
    cursor_col = line_lens[cursor_line];
    delete_char_at_cursor();
  }
}

void insert_newline() {
  if (line_count >= 4095 || cursor_line < 0 || cursor_line >= line_count) return;
  int i = line_count;
  while (i > cursor_line + 1) {
    lines[i] = lines[i - 1];
    line_lens[i] = line_lens[i - 1];
    i = i - 1;
  }

  lines[cursor_line + 1] = kmalloc(256);
  if (lines[cursor_line + 1] == 0) return;

  char *old_line = lines[cursor_line];
  char *new_line = lines[cursor_line + 1];
  int old_len = line_lens[cursor_line];
  int ni = 0;
  int oi = cursor_col;
  while (oi < old_len) {
    new_line[ni] = old_line[oi];
    ni = ni + 1;
    oi = oi + 1;
  }
  new_line[ni] = 0;
  line_lens[cursor_line + 1] = ni;
  old_line[cursor_col] = 0;
  line_lens[cursor_line] = cursor_col;
  line_count = line_count + 1;
  cursor_line = cursor_line + 1;
  cursor_col = 0;
  modified = 1;
}

void select_all() {
  sel_active = 1;
  sel_sl = 0;
  sel_sc = 0;
  sel_el = line_count - 1;
  sel_ec = line_lens[line_count - 1];
}

void normalize_sel(int *sl, int *sc, int *el, int *ec) {
  *sl = sel_sl;
  *sc = sel_sc;
  *el = sel_el;
  *ec = sel_ec;
  if (*sl > *el || (*sl == *el && *sc > *ec)) {
    int t;
    t = *sl; *sl = *el; *el = t;
    t = *sc; *sc = *ec; *ec = t;
  }
}

void copy_selection() {
  if (!sel_active) return;
  int sl;
  int sc2;
  int el;
  int ec;
  normalize_sel(&sl, &sc2, &el, &ec);
  int ci = 0;
  int li = sl;
  while (li <= el && ci < 4094) {
    char *line = lines[li];
    int len = line_lens[li];
    int from = 0;
    int to = len;
    if (li == sl) from = sc2;
    if (li == el) to = ec;
    int i = from;
    while (i < to && ci < 4094) {
      clip_buf[ci] = line[i];
      ci = ci + 1;
      i = i + 1;
    }
    if (li < el && ci < 4094) {
      clip_buf[ci] = 10;
      ci = ci + 1;
    }
    li = li + 1;
  }
  clip_buf[ci] = 0;
  clipboard_set(clip_buf, ci);
}

void delete_selection() {
  if (!sel_active) return;
  int sl;
  int sc2;
  int el;
  int ec;
  normalize_sel(&sl, &sc2, &el, &ec);

  char *start_line = lines[sl];
  char *end_line = lines[el];
  int end_len = line_lens[el];
  int ni = sc2;
  int ei = ec;
  while (ei <= end_len && ni < 255) {
    start_line[ni] = end_line[ei];
    ni = ni + 1;
    ei = ei + 1;
  }
  start_line[ni] = 0;
  line_lens[sl] = ni;

  int i = sl + 1;
  while (i <= el) {
    if (lines[i] != 0) kfree(lines[i]);
    i = i + 1;
  }

  i = sl + 1;
  while (i + (el - sl) < line_count) {
    lines[i] = lines[i + (el - sl)];
    line_lens[i] = line_lens[i + (el - sl)];
    i = i + 1;
  }
  line_count = line_count - (el - sl);
  cursor_line = sl;
  cursor_col = sc2;
  clear_sel();
  modified = 1;
}

void paste_clipboard() {
  if (sel_active) delete_selection();
  int p = clipboard_get();
  int n = clipboard_len();
  if (p == 0 || n <= 0) return;
  char *cb = p;
  int i = 0;
  while (i < n) {
    char c = cb[i];
    if (c == 10) {
      insert_newline();
    } else if (c >= 32) {
      insert_char(c);
    }
    i = i + 1;
  }
}

int ends_with_ctxt(char *path) {
  int n = np_strlen(path);
  if (n < 5) return 0;
  return path[n - 5] == 46 &&
         np_tolower(path[n - 4]) == 99 &&
         np_tolower(path[n - 3]) == 116 &&
         np_tolower(path[n - 2]) == 120 &&
         np_tolower(path[n - 1]) == 116;
}

void do_new() {
  free_buffer();
  init_buffer();
  filename[0] = 0;
  modified = 0;
  is_ctxt = 0;
  render_mode = 0;
  ctxt_sy = 0;
  ctxt_sx = 0;
}

void parse_ctxt_if_needed() {
  if (!is_ctxt) return;
  int n = vfs_read_text(filename, file_buf, 32767);
  if (n > 0) {
    file_buf[n] = 0;
    ctxt_parse(file_buf, n);
  }
}

void load_file(char *path) {
  int n = vfs_read_text(path, file_buf, 32767);
  if (n < 0) return;
  file_buf[n] = 0;

  free_buffer();
  line_count = 0;
  int i = 0;
  int li = 0;
  while (i <= n && li < 4096) {
    int ls = i;
    while (i < n && file_buf[i] != 10) i = i + 1;
    int le = i;
    if (i < n) i = i + 1;

    lines[li] = kmalloc(256);
    if (lines[li] == 0) break;
    char *dst = lines[li];
    int ll = le - ls;
    if (ll > 254) ll = 254;
    int j = 0;
    while (j < ll) {
      dst[j] = file_buf[ls + j];
      j = j + 1;
    }
    while (ll > 0 && dst[ll - 1] == 13) ll = ll - 1;
    dst[ll] = 0;
    line_lens[li] = ll;
    li = li + 1;
    if (i >= n) break;
  }

  if (li == 0) {
    lines[0] = kmalloc(256);
    if (lines[0] != 0) {
      char *p0 = (char *)lines[0];
      *p0 = 0;
    }
    line_lens[0] = 0;
    li = 1;
  }

  line_count = li;
  cursor_line = 0;
  cursor_col = 0;
  scroll_y = 0;
  scroll_x = 0;
  modified = 0;
  clear_sel();
  np_strcpy(filename, path, 256);
  is_ctxt = ends_with_ctxt(path);
  render_mode = is_ctxt;
  ctxt_sy = 0;
  ctxt_sx = 0;
  parse_ctxt_if_needed();
}

void save_file(char *path) {
  int ci = 0;
  int li = 0;
  while (li < line_count && ci < 32700) {
    char *lp = lines[li];
    int ll = line_lens[li];
    int j = 0;
    while (j < ll && ci < 32700) {
      file_buf[ci] = lp[j];
      ci = ci + 1;
      j = j + 1;
    }
    file_buf[ci] = 10;
    ci = ci + 1;
    li = li + 1;
  }
  file_buf[ci] = 0;
  vfs_write_text(path, file_buf);
  modified = 0;
  parse_ctxt_if_needed();
}

void do_open() {
  char out[256];
  out[0] = 0;
  int ok = file_dialog_open("/", out, "");
  if (ok && out[0]) load_file(out);
}

void do_save() {
  if (filename[0] == 0) {
    char out[256];
    out[0] = 0;
    int ok = file_dialog_save("/", "untitled.txt", out, "");
    if (!ok || !out[0]) return;
    np_strcpy(filename, out, 256);
  }
  if (save_path_alt[0]) save_file(save_path_alt);
  else save_file(filename);
}

void do_save_as() {
  char out[256];
  out[0] = 0;
  int ok = file_dialog_save("/", filename, out, "");
  if (!ok || !out[0]) return;
  np_strcpy(filename, out, 256);
  save_file(filename);
}

void draw_menu(int cx, int cy, int cw) {
  int my = cy;
  gfx2d_rect_fill(cx, my, cw, 12, COL_MENUBAR);
  if (active_menu == 0) gfx2d_rect_fill(cx, my, 36, 12, COL_MENU_HOV);
  gfx2d_text(cx + 4, my + 2, "File", COL_MENU_TEXT, 1);
  if (active_menu == 1) gfx2d_rect_fill(cx + 40, my, 36, 12, COL_MENU_HOV);
  gfx2d_text(cx + 44, my + 2, "Edit", COL_MENU_TEXT, 1);

  if (active_menu == 0) {
    int dx = cx;
    int dy = my + 12;
    gfx2d_rect_fill(dx, dy, 90, 60, COL_MENUBAR);
    gfx2d_rect(dx, dy, 90, 60, COL_THUMB);
    gfx2d_text(dx + 4, dy + 2, "New", COL_MENU_TEXT, 1);
    gfx2d_text(dx + 4, dy + 14, "Open", COL_MENU_TEXT, 1);
    gfx2d_text(dx + 4, dy + 26, "Save", COL_MENU_TEXT, 1);
    gfx2d_text(dx + 4, dy + 38, "Save As", COL_MENU_TEXT, 1);
    gfx2d_text(dx + 4, dy + 50, "Exit", COL_MENU_TEXT, 1);
  }

  if (active_menu == 1) {
    int dx = cx + 40;
    int dy = my + 12;
    gfx2d_rect_fill(dx, dy, 110, 72, COL_MENUBAR);
    gfx2d_rect(dx, dy, 110, 72, COL_THUMB);
    gfx2d_text(dx + 4, dy + 2, "Undo", COL_MENU_TEXT, 1);
    gfx2d_text(dx + 4, dy + 14, "Cut", COL_MENU_TEXT, 1);
    gfx2d_text(dx + 4, dy + 26, "Copy", COL_MENU_TEXT, 1);
    gfx2d_text(dx + 4, dy + 38, "Paste", COL_MENU_TEXT, 1);
    gfx2d_text(dx + 4, dy + 50, "Sel All", COL_MENU_TEXT, 1);
    gfx2d_text(dx + 4, dy + 62, "Render F2", COL_MENU_TEXT, 1);
  }
}

void draw_status(int cx, int cy, int cw, int ch_h) {
  int y = cy + ch_h - 10;
  gfx2d_rect_fill(cx, y, cw, 10, COL_STATUSBAR);
  char buf[64];
  int i = 0;
  int v;
  buf[i] = 'L'; i = i + 1;
  buf[i] = 'n'; i = i + 1;
  buf[i] = ' '; i = i + 1;
  v = cursor_line + 1;
  if (v >= 1000) { buf[i] = 48 + (v / 1000); i = i + 1; v = v % 1000; }
  if (v >= 100) { buf[i] = 48 + (v / 100); i = i + 1; v = v % 100; }
  if (v >= 10) { buf[i] = 48 + (v / 10); i = i + 1; v = v % 10; }
  buf[i] = 48 + v; i = i + 1;
  buf[i] = ' '; i = i + 1;
  buf[i] = 'C'; i = i + 1;
  buf[i] = 'o'; i = i + 1;
  buf[i] = 'l'; i = i + 1;
  buf[i] = ' '; i = i + 1;
  v = cursor_col + 1;
  if (v >= 1000) { buf[i] = 48 + (v / 1000); i = i + 1; v = v % 1000; }
  if (v >= 100) { buf[i] = 48 + (v / 100); i = i + 1; v = v % 100; }
  if (v >= 10) { buf[i] = 48 + (v / 10); i = i + 1; v = v % 10; }
  buf[i] = 48 + v; i = i + 1;
  buf[i] = 0;
  gfx2d_text(cx + 4, y + 1, buf, COL_STATUS_TEXT, 1);
  if (modified) {
    int rx = cx + cw - 18;
    if (rx < cx + 4) rx = cx + 4;
    gfx2d_text(rx, y + 1, "*", COL_STATUS_TEXT, 1);
  }
}

int sel_line_hit(int li) {
  if (!sel_active) return 0;
  int sl;
  int sc2;
  int el;
  int ec;
  normalize_sel(&sl, &sc2, &el, &ec);
  return li >= sl && li <= el;
}

int sel_span_for_line(int li, int len, int *out_sc, int *out_ec) {
  int sl;
  int sc2;
  int el;
  int ec;
  if (!sel_active) return 0;
  normalize_sel(&sl, &sc2, &el, &ec);
  if (li < sl || li > el) return 0;

  int start = 0;
  int end = len;
  if (sl == el) {
    start = sc2;
    end = ec;
  } else if (li == sl) {
    start = sc2;
    end = len;
  } else if (li == el) {
    start = 0;
    end = ec;
  }

  if (start < 0) start = 0;
  if (end < 0) end = 0;
  if (start > len) start = len;
  if (end > len) end = len;
  if (end <= start) return 0;

  *out_sc = start;
  *out_ec = end;
  return 1;
}

void draw_text_area(int cx, int cy, int cw, int ch_h) {
  int area_y = cy + 12;
  int area_h = ch_h - 12 - 10 - 12;
  int area_w = cw - 12;
  int rows = get_rows(ch_h);
  int cols = get_cols(area_w);
  int cell = 8 * font_scale;

  gfx2d_rect_fill(cx, area_y, area_w, area_h, COL_BG);

  if (render_mode && is_ctxt) {
    int ctxt_top_pad = 8;
    int render_y = area_y + ctxt_top_pad;
    int render_h = area_h - ctxt_top_pad;
    if (render_h < 1) render_h = 1;
    ctxt_render(cx, render_y, area_w, render_h, ctxt_sy, ctxt_sx);
    return;
  }

  int r = 0;
  while (r < rows) {
    int li = scroll_y + r;
    if (li >= line_count) break;
    char *lp = lines[li];
    int len = line_lens[li];
    int py = area_y + r * cell;

    {
      int sel_sc2;
      int sel_ec2;
      if (sel_span_for_line(li, len, &sel_sc2, &sel_ec2)) {
        int vis_sc = sel_sc2;
        int vis_ec = sel_ec2;
        if (vis_sc < scroll_x) vis_sc = scroll_x;
        if (vis_ec > scroll_x + cols) vis_ec = scroll_x + cols;
        if (vis_ec > vis_sc) {
          int sx = cx + (vis_sc - scroll_x) * cell;
          int sw = (vis_ec - vis_sc) * cell;
          if (sx < cx) {
            sw = sw - (cx - sx);
            sx = cx;
          }
          if (sx + sw > cx + area_w) sw = (cx + area_w) - sx;
          if (sw > 0) gfx2d_rect_fill(sx, py, sw, cell, COL_SEL_BG);
        }
      }
    }

    int c = scroll_x;
    int px = cx;
    while (c < len && c < scroll_x + cols) {
      char ch2 = lp[c];
      if (ch2 >= 32) gfx2d_char_scaled(px, py, ch2, COL_TEXT, font_scale);
      px = px + cell;
      c = c + 1;
    }

    r = r + 1;
  }

  if (cursor_on && cursor_line >= scroll_y && cursor_line < scroll_y + rows) {
    int pr = cursor_line - scroll_y;
    int pc = cursor_col - scroll_x;
    if (pc >= 0 && pc < cols) {
      int px = cx + pc * cell;
      int py = area_y + pr * cell;
      if (px + 2 <= cx + area_w) {
        gfx2d_rect_fill(px, py, 2, cell, COL_CURSOR);
      }
    }
  }
}

void draw_scrollbars(int cx, int cy, int cw, int ch_h) {
  int area_h = ch_h - 12 - 10;
  int vx = cx + cw - 12;
  int vy = cy + 12;
  int vh = area_h - 12;
  int rel_vx;
  int rel_vy;
  int rel_vh;
  int thumb_y;
  int thumb_h;
  int max_top;
  int rel_hx;
  int rel_hy;
  int rel_hw;
  int thumb_x;
  int thumb_w;
  int max_left;

  scrollbar_metrics(ch_h, cw, &rel_vx, &rel_vy, &rel_vh, &thumb_y, &thumb_h,
                    &max_top);

  vx = cx + rel_vx;
  vy = cy + rel_vy;
  vh = rel_vh;

  gfx2d_rect_fill(vx, vy, 12, vh, COL_SCROLLBAR);
  gfx2d_rect_fill(vx + 2, cy + thumb_y, 8, thumb_h, COL_THUMB);

  int hy = cy + ch_h - 10 - 12;
  hscrollbar_metrics(ch_h, cw, &rel_hx, &rel_hy, &rel_hw, &thumb_x, &thumb_w,
                     &max_left);
  gfx2d_rect_fill(cx + rel_hx, cy + rel_hy, rel_hw, 12, COL_SCROLLBAR);
  gfx2d_rect_fill(cx + thumb_x, cy + rel_hy + 2, thumb_w, 8, COL_THUMB);

  /* Bottom-right corner where vertical and horizontal bars meet */
  gfx2d_rect_fill(cx + cw - 12, cy + rel_hy, 12, 12, COL_SCROLLBAR);
}

void handle_key(int sc, int ch) {
  int ctrl = keyboard_ctrl_held();
  int lo = np_tolower(ch);

  if (sc == 1) {
    active_menu = -1;
    clear_sel();
    return;
  }

  if (ctrl) {
    int k_n = (lo == 'n' || sc == 49);
    int k_o = (lo == 'o' || sc == 24);
    int k_s = (lo == 's' || sc == 31);
    int k_q = (lo == 'q' || sc == 16);
    int k_z = (lo == 'z' || sc == 44);
    int k_x = (lo == 'x' || sc == 45);
    int k_c = (lo == 'c' || sc == 46);
    int k_v = (lo == 'v' || sc == 47);
    int k_a = (lo == 'a' || sc == 30);
    int k_r = (lo == 'r' || sc == 19);

    if (k_r && is_ctxt) {
      render_mode = 1 - render_mode;
      if (render_mode) parse_ctxt_if_needed();
      return;
    }
    if (k_n) { do_new(); return; }
    if (k_o) { do_open(); return; }
    if (k_s) { do_save(); return; }
    if (k_q) { should_close = 1; return; }
    if (k_z) { do_undo(); return; }
    if (k_x) { save_undo(); copy_selection(); delete_selection(); return; }
    if (k_c) { copy_selection(); return; }
    if (k_v) { save_undo(); paste_clipboard(); return; }
    if (k_a) { select_all(); return; }
    if (ch == 43 || ch == 61) { if (font_scale < 3) font_scale = font_scale + 1; return; }
    if (ch == 45) { if (font_scale > 1) font_scale = font_scale - 1; return; }
    return;
  }

  if (render_mode && is_ctxt) {
    if (sc == 60) { render_mode = 0; return; }
    if (sc == 73 || sc == 72) { ctxt_sy = ctxt_sy - 20; if (ctxt_sy < 0) ctxt_sy = 0; return; }
    if (sc == 81 || sc == 80) { ctxt_sy = ctxt_sy + 20; return; }
    return;
  }

  if (sc == 60 && is_ctxt) {
    render_mode = 1 - render_mode;
    if (render_mode) parse_ctxt_if_needed();
    return;
  }

  if (sc == 72) { if (cursor_line > 0) cursor_line = cursor_line - 1; if (cursor_col > line_lens[cursor_line]) cursor_col = line_lens[cursor_line]; clear_sel(); return; }
  if (sc == 80) { if (cursor_line < line_count - 1) cursor_line = cursor_line + 1; if (cursor_col > line_lens[cursor_line]) cursor_col = line_lens[cursor_line]; clear_sel(); return; }
  if (sc == 75) { if (cursor_col > 0) cursor_col = cursor_col - 1; else if (cursor_line > 0) { cursor_line = cursor_line - 1; cursor_col = line_lens[cursor_line]; } clear_sel(); return; }
  if (sc == 77) { if (cursor_col < line_lens[cursor_line]) cursor_col = cursor_col + 1; else if (cursor_line < line_count - 1) { cursor_line = cursor_line + 1; cursor_col = 0; } clear_sel(); return; }
  if (sc == 71) { cursor_col = 0; clear_sel(); return; }
  if (sc == 79) { cursor_col = line_lens[cursor_line]; clear_sel(); return; }
  if (sc == 73) { cursor_line = cursor_line - 20; if (cursor_line < 0) cursor_line = 0; clear_sel(); return; }
  if (sc == 81) { cursor_line = cursor_line + 20; if (cursor_line >= line_count) cursor_line = line_count - 1; clear_sel(); return; }

  if (sc == 14) { save_undo(); if (sel_active) delete_selection(); else do_backspace(); return; }
  if (sc == 83) { save_undo(); if (sel_active) delete_selection(); else delete_char_at_cursor(); return; }
  if (sc == 28) { save_undo(); if (sel_active) delete_selection(); insert_newline(); return; }
  if (sc == 15) { save_undo(); if (sel_active) delete_selection(); insert_char(32); insert_char(32); insert_char(32); insert_char(32); return; }

  if (ch >= 32 && ch < 127) {
    save_undo();
    if (sel_active) delete_selection();
    insert_char(ch);
  }
}

void handle_mouse(int mx, int my, int buttons, int cx, int cy, int cw, int ch_h) {
  int left_clicked = 0;
  int left_held = 0;
  int left_released = 0;

  if (buttons & 1) {
    left_clicked = mouse_lmb_latch ? 0 : 1;
    mouse_lmb_latch = 1;
    left_held = 1;
  } else {
    left_released = mouse_lmb_latch;
    mouse_lmb_latch = 0;
    left_held = 0;
  }
  int menu_y = cy;

  if (left_released) {
    sb_dragging = 0;
    hb_dragging = 0;
  }

  {
    int rel_vx;
    int rel_vy;
    int rel_vh;
    int thumb_y;
    int thumb_h;
    int max_top;
    int vx;
    int vy;
    int vh;

    scrollbar_metrics(ch_h, cw, &rel_vx, &rel_vy, &rel_vh, &thumb_y, &thumb_h,
                      &max_top);
    vx = cx + rel_vx;
    vy = cy + rel_vy;
    vh = rel_vh;
    thumb_y = cy + thumb_y;

    if (left_clicked && mx >= vx && mx < vx + 12 && my >= vy && my < vy + vh) {
      if (my >= thumb_y && my < thumb_y + thumb_h) {
        sb_dragging = 1;
        sb_drag_off = my - thumb_y;
      } else {
        int range = vh - thumb_h;
        int track = my - vy - (thumb_h / 2);
        int top = 0;
        if (track < 0) track = 0;
        if (range > 0 && track > range) track = range;
        if (range > 0 && max_top > 0) top = (track * max_top) / range;
        set_scroll_top(top, ch_h, cw);
      }
      prev_buttons = buttons;
      return;
    }

    if (left_held && sb_dragging) {
      int range = vh - thumb_h;
      int track = my - vy - sb_drag_off;
      int top = 0;
      if (track < 0) track = 0;
      if (range > 0 && track > range) track = range;
      if (range > 0 && max_top > 0) top = (track * max_top) / range;
      set_scroll_top(top, ch_h, cw);
      prev_buttons = buttons;
      return;
    }

    {
      int rel_hx;
      int rel_hy;
      int rel_hw;
      int thumb_x;
      int thumb_w;
      int max_left;
      int hx;
      int hy;
      int hw;

      hscrollbar_metrics(ch_h, cw, &rel_hx, &rel_hy, &rel_hw, &thumb_x, &thumb_w,
                         &max_left);
      hx = cx + rel_hx;
      hy = cy + rel_hy;
      hw = rel_hw;
      thumb_x = cx + thumb_x;

      if (left_clicked && mx >= hx && mx < hx + hw && my >= hy && my < hy + 12) {
        if (mx >= thumb_x && mx < thumb_x + thumb_w) {
          hb_dragging = 1;
          hb_drag_off = mx - thumb_x;
        } else {
          int range = hw - thumb_w;
          int track = mx - hx - (thumb_w / 2);
          int left = 0;
          if (track < 0) track = 0;
          if (range > 0 && track > range) track = range;
          if (range > 0 && max_left > 0) left = (track * max_left) / range;
          set_scroll_left(left, ch_h, cw);
        }
        prev_buttons = buttons;
        return;
      }

      if (left_held && hb_dragging) {
        int range = hw - thumb_w;
        int track = mx - hx - hb_drag_off;
        int left = 0;
        if (track < 0) track = 0;
        if (range > 0 && track > range) track = range;
        if (range > 0 && max_left > 0) left = (track * max_left) / range;
        set_scroll_left(left, ch_h, cw);
        prev_buttons = buttons;
        return;
      }
    }
  }

  if (left_clicked) {
    if (active_menu == 0) {
      int dy = menu_y + 12;
      if (mx >= cx && mx < cx + 90 && my >= dy && my < dy + 60) {
        if (my < dy + 12) do_new();
        else if (my < dy + 24) do_open();
        else if (my < dy + 36) do_save();
        else if (my < dy + 48) do_save_as();
        else should_close = 1;
        active_menu = -1;
        prev_buttons = buttons;
        return;
      }
    } else if (active_menu == 1) {
      int dx = cx + 40;
      int dy = menu_y + 12;
      if (mx >= dx && mx < dx + 110 && my >= dy && my < dy + 72) {
        if (my < dy + 12) do_undo();
        else if (my < dy + 24) { save_undo(); copy_selection(); delete_selection(); }
        else if (my < dy + 36) copy_selection();
        else if (my < dy + 48) { save_undo(); paste_clipboard(); }
        else if (my < dy + 60) select_all();
        else if (is_ctxt) render_mode = 1 - render_mode;
        active_menu = -1;
        prev_buttons = buttons;
        return;
      }
    }

    if (my >= menu_y && my < menu_y + 12) {
      if (mx >= cx && mx < cx + 36) {
        if (active_menu == 0) active_menu = -1;
        else active_menu = 0;
      } else if (mx >= cx + 40 && mx < cx + 76) {
        if (active_menu == 1) active_menu = -1;
        else active_menu = 1;
      } else {
        active_menu = -1;
      }
      prev_buttons = buttons;
      return;
    }

    active_menu = -1;
  }

  if (render_mode && is_ctxt) {
    if (left_clicked) {
      int lidx = ctxt_link_at(mx, my, ctxt_sy, ctxt_sx);
      serial_printf("[notepad.cc] link click mx=%d my=%d idx=%d count=%d\n", mx, my, lidx, ctxt_link_count);
      if (lidx >= 0) {
        char target[256];
        target[0] = 0;
        ctxt_get_link(lidx, target, 256);
        serial_printf("[notepad.cc] link target: %s file=%s\n", target, filename);
        if (!np_open_link_target(target)) {
          serial_printf("[notepad.cc] link unresolved: %s\n", target);
        }
      }
    }
    prev_buttons = buttons;
    return;
  }

  int area_y = cy + 12;
  int cell = 8 * font_scale;
  int rows = get_rows(ch_h);
  int cols = get_cols(cw - 12);
  int in_area = (my >= area_y && my < area_y + rows * cell &&
                 mx >= cx && mx < cx + cols * cell);

  int row = scroll_y;
  int col = scroll_x;
  if (my < area_y) row = scroll_y;
  else if (my >= area_y + rows * cell) row = scroll_y + rows - 1;
  else row = scroll_y + (my - area_y) / cell;

  if (mx < cx) col = scroll_x;
  else if (mx >= cx + cols * cell) col = scroll_x + cols - 1;
  else col = scroll_x + (mx - cx) / cell;

  if (row < 0) row = 0;
  if (row >= line_count) row = line_count - 1;
  if (col < 0) col = 0;
  if (col > line_lens[row]) col = line_lens[row];

  if (left_clicked && in_area) {
    cursor_line = row;
    cursor_col = col;
    sel_active = 0;
    sel_sl = row;
    sel_sc = col;
    sel_el = row;
    sel_ec = col;
    drag_sel = 1;
  } else if (left_held && drag_sel) {
    cursor_line = row;
    cursor_col = col;
    if (row != sel_sl || col != sel_sc) {
      sel_active = 1;
      sel_el = row;
      sel_ec = col;
    } else {
      sel_active = 0;
      sel_el = sel_sl;
      sel_ec = sel_sc;
    }
  }

  if (left_released) {
    drag_sel = 0;
    if (sel_active && sel_sl == sel_el && sel_sc == sel_ec) clear_sel();
  }

  prev_buttons = buttons;
}

void main() {
  serial_printf("[notepad.cc] main start\n");
  init_colors();
  ctxt_reset();

  win = gui_win_create("Notepad", 100, 50, 540, 350);
  if (win == -1) {
    serial_printf("[notepad.cc] gui_win_create failed\n");
    message_dialog("Notepad: failed to create window");
    return;
  }
  serial_printf("[notepad.cc] window created: %d\n", win);

  init_buffer();
  font_scale = 1;
  active_menu = -1;
  cursor_on = 1;
  blink_ms = uptime_ms();
  render_mode = 0;
  is_ctxt = 0;
  ctxt_sy = 0;
  ctxt_sx = 0;
  drag_sel = 0;
  prev_buttons = 0;
  mouse_lmb_latch = 0;
  sb_dragging = 0;
  sb_drag_off = 0;
  hb_dragging = 0;
  hb_drag_off = 0;
  should_close = 0;

  save_path_alt[0] = 0;

  while (gui_win_is_open(win)) {
    if (should_close) {
      gui_win_close(win);
      win = -1;
      break;
    }

    if (!gui_win_can_draw(win)) {
      yield();
      continue;
    }

    int cx = gui_win_content_x(win);
    int cy = gui_win_content_y(win);
    int cw = gui_win_content_w(win);
    int ch_h = gui_win_content_h(win);

    gui_win_draw_frame(win);

    int rows = get_rows(ch_h);
    int cols = get_cols(cw - 12);

    int key = gui_win_poll_key(win);
    while (key != -1) {
      int sc = (key >> 8) & 255;
      int ch = key & 255;
      handle_key(sc, ch);
      key = gui_win_poll_key(win);
    }

    {
      int delta = mouse_scroll();
      if (delta != 0) {
        int use_x = key_shift_held();
        if (render_mode && is_ctxt) {
          if (use_x) {
            ctxt_sx = ctxt_sx - delta * 16;
            if (ctxt_sx < 0) ctxt_sx = 0;
          } else {
            ctxt_sy = ctxt_sy - delta * 8;
            if (ctxt_sy < 0) ctxt_sy = 0;
          }
        } else {
          if (use_x) {
            scroll_x = scroll_x - delta * 2;
            if (scroll_x < 0) scroll_x = 0;
          } else {
            scroll_y = scroll_y - delta;
            if (scroll_y < 0) scroll_y = 0;
          }
        }
      }
    }

    clamp_scroll_state(ch_h, cw);

    handle_mouse(mouse_x(), mouse_y(), mouse_buttons(), cx, cy, cw, ch_h);

    ensure_cursor_visible(rows, cols);
    clamp_scroll_state(ch_h, cw);
    draw_text_area(cx, cy, cw, ch_h);
    draw_scrollbars(cx, cy, cw, ch_h);
    draw_status(cx, cy, cw, ch_h);
    draw_menu(cx, cy, cw);

    if (uptime_ms() - blink_ms > 500) {
      cursor_on = 1 - cursor_on;
      blink_ms = uptime_ms();
    }

    gui_win_flip(win);
    yield();
  }

  free_buffer();
  free_undo();
  if (win != -1) gui_win_close(win);
}
