/* fontswitch.cc - OS-wide TTF font picker.
 *
 * Lists registered TTF faces plus a "8x8 bitmap" sentinel row, lets
 * the user pick one and a pixel size, previews the choice, and on
 * Apply commits via fontsys_set_os_default + writes /etc/font.conf
 * so the choice survives reboot. ESC key (or window close) exits.
 *
 * Models on bin/paint.cc for the gfx2d_app_toolbar / mouse-click loop
 * and bin/gfxgui_test.cc for fullscreen entry. */

int g_face_count = 0;
int g_sel_index  = 0;       /* index into the on-screen list */
int g_sel_size   = 14;
int g_apply_msg_until = 0;  /* tick value; show "Saved!" until then */
int g_status_color = 0x66FF66;

/* Layout constants. */
int LIST_X      = 24;
int LIST_Y      = 56;
int LIST_W      = 592;
int ROW_H       = 28;

int PREVIEW_X   = 24;
int PREVIEW_Y   = 232;
int PREVIEW_W   = 592;
int PREVIEW_H   = 80;

int CTRL_Y      = 332;
int SZ_MINUS_X  = 130;
int SZ_PLUS_X   = 200;
int SZ_BTN_W    = 32;
int SZ_BTN_H    = 28;

int APPLY_X     = 460;
int APPLY_W     = 140;
int APPLY_H     = 28;

int hit(int mx, int my, int x, int y, int w, int h) {
  if (mx < x) return 0;
  if (my < y) return 0;
  if (mx >= x + w) return 0;
  if (my >= y + h) return 0;
  return 1;
}

/* Number of rows shown — TTF faces + 1 bitmap sentinel. */
int total_rows() {
  return g_face_count + 1;
}

/* Map a row index to a face_id; -1 means bitmap sentinel row. */
int row_face_id(int row) {
  if (row < g_face_count) return row;
  return -1;
}

void draw_row(int row, int tick) {
  int y = LIST_Y + row * ROW_H;
  int sel = (row == g_sel_index);
  int bg = sel ? 0x2C5279 : 0x1F1F25;
  int fg = sel ? 0xFFFFFF : 0xD0D0D0;

  gfx2d_rect_fill(LIST_X, y, LIST_W, ROW_H - 2, bg);

  /* Caret/marker on selected row. */
  if (sel) {
    gfx2d_text(LIST_X + 6, y + 8, ">", fg, 1);
  }

  int face = row_face_id(row);
  if (face < 0) {
    gfx2d_text(LIST_X + 24, y + 8, "8x8 bitmap (revert)", fg, 1);
    gfx2d_text(LIST_X + 320, y + 8, "--", fg, 1);
    gfx2d_text(LIST_X + 380, y + 8, "--", fg, 1);
    return;
  }

  gfx2d_text(LIST_X + 24, y + 8, fontsys_face_family(face), fg, 1);

  /* Weight column. */
  int w = fontsys_face_weight(face);
  int wx = LIST_X + 320;
  int hundreds = w / 100;
  int tens = (w / 10) % 10;
  int ones = w % 10;
  char wbuf[6];
  wbuf[0] = (char)(48 + hundreds);
  wbuf[1] = (char)(48 + tens);
  wbuf[2] = (char)(48 + ones);
  wbuf[3] = 0;
  gfx2d_text(wx, y + 8, wbuf, fg, 1);

  /* Italic flag column. */
  int it = fontsys_face_italic(face);
  gfx2d_text(LIST_X + 380, y + 8, it ? "I" : "R", fg, 1);
}

void draw_preview(int tick) {
  gfx2d_rect_fill(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, 0x101216);
  gfx2d_rect(PREVIEW_X, PREVIEW_Y, PREVIEW_W, PREVIEW_H, 0x3A3F49);

  gfx2d_text(PREVIEW_X + 8, PREVIEW_Y + 6, "Preview:", 0x8FB1FF, 1);

  int face = row_face_id(g_sel_index);
  const char *sample = "The quick brown fox jumps over the lazy dog 1234567890";
  int sample_y = PREVIEW_Y + 28;

  /* Temporarily flip the OS default so gfx2d_text honors the previewed
   * face/size, then restore. Single code path: face>=0 routes through
   * the TTF gate inside gfx2d_text, face=-1 falls to the bitmap path. */
  int saved_face = fontsys_get_os_default_face();
  int saved_size = fontsys_get_os_default_size();
  fontsys_set_os_default(face, g_sel_size);
  gfx2d_text(PREVIEW_X + 8, sample_y, sample, 0xE8E8E8, 1);
  fontsys_set_os_default(saved_face, saved_size);
}

void draw_size_controls(int tick) {
  gfx2d_text(LIST_X, CTRL_Y + 8, "size:", 0xC0C0C0, 1);

  /* [-] button */
  gfx2d_rect_fill(SZ_MINUS_X, CTRL_Y, SZ_BTN_W, SZ_BTN_H, 0x2A2A35);
  gfx2d_rect(SZ_MINUS_X, CTRL_Y, SZ_BTN_W, SZ_BTN_H, 0x555560);
  gfx2d_text(SZ_MINUS_X + 12, CTRL_Y + 8, "-", 0xFFFFFF, 1);

  /* size readout */
  char sbuf[8];
  int s = g_sel_size;
  int t = (s / 10) % 10;
  int o = s % 10;
  int p = 0;
  if (s >= 10) { sbuf[p++] = (char)(48 + t); }
  sbuf[p++] = (char)(48 + o);
  sbuf[p] = 0;
  gfx2d_text(SZ_MINUS_X + 40, CTRL_Y + 8, sbuf, 0xFFFFFF, 1);

  /* [+] button */
  gfx2d_rect_fill(SZ_PLUS_X, CTRL_Y, SZ_BTN_W, SZ_BTN_H, 0x2A2A35);
  gfx2d_rect(SZ_PLUS_X, CTRL_Y, SZ_BTN_W, SZ_BTN_H, 0x555560);
  gfx2d_text(SZ_PLUS_X + 12, CTRL_Y + 8, "+", 0xFFFFFF, 1);

  /* Apply button */
  gfx2d_rect_fill(APPLY_X, CTRL_Y, APPLY_W, APPLY_H, 0x246B33);
  gfx2d_rect(APPLY_X, CTRL_Y, APPLY_W, APPLY_H, 0x66BB77);
  gfx2d_text(APPLY_X + 50, CTRL_Y + 8, "Apply", 0xFFFFFF, 1);

  if (tick < g_apply_msg_until) {
    gfx2d_text(LIST_X, CTRL_Y + 44, "Saved /etc/font.conf - active now",
               g_status_color, 1);
  }
}

/* Compose family=...\nsize=N\n into buf. Returns length. */
int build_conf(char *buf, int cap, const char *family, int size_px) {
  int p = 0;
  const char *k1 = "family=";
  int i = 0;
  while (k1[i] && p < cap - 1) buf[p++] = k1[i++];
  i = 0;
  while (family[i] && p < cap - 1) buf[p++] = family[i++];
  if (p < cap - 1) buf[p++] = '\n';

  const char *k2 = "size=";
  i = 0;
  while (k2[i] && p < cap - 1) buf[p++] = k2[i++];
  /* int -> ASCII. size_px is 6..96. */
  if (size_px >= 100 && p < cap - 1) buf[p++] = (char)(48 + (size_px / 100) % 10);
  if (size_px >= 10 && p < cap - 1)  buf[p++] = (char)(48 + (size_px / 10) % 10);
  if (p < cap - 1)                   buf[p++] = (char)(48 + size_px % 10);
  if (p < cap - 1) buf[p++] = '\n';
  buf[p] = 0;
  return p;
}

void apply_selection(int tick) {
  int face = row_face_id(g_sel_index);
  fontsys_set_os_default(face, g_sel_size);

  char buf[160];
  int n;
  if (face < 0) {
    n = build_conf(buf, 160, "__bitmap__", g_sel_size);
  } else {
    n = build_conf(buf, 160, fontsys_face_family(face), g_sel_size);
  }
  vfs_write_text("/etc/font.conf", buf);

  g_apply_msg_until = tick + 90;   /* ~90 frames feedback */
  g_status_color = 0x66FF66;
}

int main() {
  if (!is_gui_mode()) {
    println("[fontswitch] requires GUI mode");
    println("[fontswitch] open the desktop terminal and run again");
    return 1;
  }

  g_face_count = fontsys_face_count();
  if (g_face_count <= 0) {
    println("[fontswitch] fontsys reports no faces - is the kernel built with embedded TTFs?");
    return 1;
  }

  /* Default selection: current OS default, or first row. */
  int cur = fontsys_get_os_default_face();
  if (cur >= 0 && cur < g_face_count) {
    g_sel_index = cur;
  } else {
    g_sel_index = 0;
  }
  int cur_sz = fontsys_get_os_default_size();
  if (cur_sz >= 6 && cur_sz <= 96) g_sel_size = cur_sz;

  gfx2d_fullscreen_enter();

  int tick = 0;
  int prev_buttons = 0;
  int quit = 0;

  while (quit == 0) {
    int mx = mouse_x();
    int my = mouse_y();
    int b = mouse_buttons();
    int left_click = (b & 1) && !(prev_buttons & 1);
    prev_buttons = b;

    /* Background. */
    gfx2d_clear(0x161820);

    int tb = gfx2d_app_toolbar("Font Switch", mx, my, left_click);
    if (tb == 1 || gfx2d_should_quit()) quit = 1;
    if (tb == 2) gfx2d_minimize("Font Switch");

    /* Title strip. */
    gfx2d_text(LIST_X, 28, "Pick a font for the whole OS:", 0xFFFFFF, 1);

    /* Hit-test list rows. */
    if (left_click) {
      int r = 0;
      while (r < total_rows()) {
        int ry = LIST_Y + r * ROW_H;
        if (hit(mx, my, LIST_X, ry, LIST_W, ROW_H - 2)) {
          g_sel_index = r;
        }
        r = r + 1;
      }

      /* size [-] */
      if (hit(mx, my, SZ_MINUS_X, CTRL_Y, SZ_BTN_W, SZ_BTN_H)) {
        if (g_sel_size > 8) g_sel_size = g_sel_size - 1;
      }
      /* size [+] */
      if (hit(mx, my, SZ_PLUS_X, CTRL_Y, SZ_BTN_W, SZ_BTN_H)) {
        if (g_sel_size < 48) g_sel_size = g_sel_size + 1;
      }
      /* Apply */
      if (hit(mx, my, APPLY_X, CTRL_Y, APPLY_W, APPLY_H)) {
        apply_selection(tick);
      }
    }

    /* Draw rows. */
    int r = 0;
    while (r < total_rows()) {
      draw_row(r, tick);
      r = r + 1;
    }

    draw_preview(tick);
    draw_size_controls(tick);

    gfx2d_draw_cursor();
    gfx2d_flip();
    tick = tick + 1;
    yield();
  }

  gfx2d_fullscreen_exit();
  return 0;
}
