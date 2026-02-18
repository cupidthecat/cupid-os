#pragma once

int CTXT_BODY;
int CTXT_H1;
int CTXT_H2;
int CTXT_H3;
int CTXT_RULE;
int CTXT_CENTER;
int CTXT_BOX;
int CTXT_COMMENT;

int CTXT_MAX_LINES;
int CTXT_MAX_LINKS;
int CTXT_MAX_TEXT;

int ctxt_line_count;
int ctxt_line_type[1024];
char ctxt_line_text[1024][128];
int ctxt_line_color[1024];
int ctxt_line_bg_color[1024];
int ctxt_in_box[1024];
int ctxt_box_bg[1024];

int ctxt_link_count;
char ctxt_link_target[128][128];
int ctxt_link_x[128];
int ctxt_link_y[128];
int ctxt_link_w[128];
int ctxt_link_h[128];

int ctxt_theme_light;
int ctxt_total_h;
int ctxt_total_w;

int ctxt_col_bg;
int ctxt_col_h1;
int ctxt_col_h2;
int ctxt_col_h3;
int ctxt_col_body;
int ctxt_col_rule;
int ctxt_col_box_bg;
int ctxt_col_box_text;
int ctxt_col_link;

int ctxt_strlen(char *s) {
  int n = 0;
  while (s[n]) n = n + 1;
  return n;
}

void ctxt_strcpy_n(char *dst, char *src, int max) {
  int i = 0;
  while (src[i] && i < max - 1) {
    dst[i] = src[i];
    i = i + 1;
  }
  dst[i] = 0;
}

int ctxt_starts_with(char *line, char *prefix) {
  int i = 0;
  while (prefix[i]) {
    if (line[i] != prefix[i]) return 0;
    i = i + 1;
  }
  return 1;
}

int ctxt_hexdig(char c) {
  if (c >= 48 && c <= 57) return c - 48;
  if (c >= 65 && c <= 70) return c - 55;
  if (c >= 97 && c <= 102) return c - 87;
  return 0;
}

int ctxt_is_space(char c) {
  return c == 32 || c == 9 || c == 13 || c == 10;
}

int ctxt_tolower(int ch) {
  if (ch >= 65 && ch <= 90) return ch + 32;
  return ch;
}

int ctxt_is_path_char(char c) {
  if (c >= 48 && c <= 57) return 1;
  if (c >= 65 && c <= 90) return 1;
  if (c >= 97 && c <= 122) return 1;
  if (c == 47 || c == 46 || c == 95 || c == 45) return 1;
  return 0;
}

int ctxt_has_known_ext(char *s, int start, int end) {
  int dot = -1;
  int i = start;
  while (i < end) {
    if (s[i] == 46) dot = i;
    i = i + 1;
  }
  if (dot <= start || dot >= end - 1) return 0;

  int elen = end - dot - 1;
  int a = 0;
  int b = 0;
  int c = 0;
  int d = 0;

  if (elen >= 1) a = ctxt_tolower(s[dot + 1]);
  if (elen >= 2) b = ctxt_tolower(s[dot + 2]);
  if (elen >= 3) c = ctxt_tolower(s[dot + 3]);
  if (elen >= 4) d = ctxt_tolower(s[dot + 4]);

  if (elen == 1 && a == 'c') return 1;
  if (elen == 1 && a == 'h') return 1;
  if (elen == 2 && a == 'c' && b == 'c') return 1;
  if (elen == 2 && a == 'm' && b == 'd') return 1;
  if (elen == 3 && a == 't' && b == 'x' && c == 't') return 1;
  if (elen == 3 && a == 'c' && b == 'u' && c == 'p') return 1;
  if (elen == 3 && a == 'a' && b == 's' && c == 'm') return 1;
  if (elen == 3 && a == 'e' && b == 'l' && c == 'f') return 1;
  if (elen == 4 && a == 'c' && b == 't' && c == 'x' && d == 't') return 1;
  return 0;
}

int ctxt_is_bare_link_token(char *s, int start, int end) {
  int has_slash = 0;
  int has_dot = 0;
  int i = start;
  if (end <= start) return 0;
  while (i < end) {
    if (s[i] == 47) has_slash = 1;
    if (s[i] == 46) has_dot = 1;
    i = i + 1;
  }
  if (!has_dot) return 0;
  if (has_slash) return 1;
  return ctxt_has_known_ext(s, start, end);
}

int ctxt_parse_color(char *buf, int i) {
  if (buf[i] != 35) return 0;
  i = i + 1;
  int r = ctxt_hexdig(buf[i]) * 16 + ctxt_hexdig(buf[i + 1]);
  int g = ctxt_hexdig(buf[i + 2]) * 16 + ctxt_hexdig(buf[i + 3]);
  int b = ctxt_hexdig(buf[i + 4]) * 16 + ctxt_hexdig(buf[i + 5]);
  return (r * 65536) + (g * 256) + b;
}

void ctxt_set_theme(int light) {
  ctxt_theme_light = light;
  if (light) {
    ctxt_col_bg = 0x00F7F4EC;
    ctxt_col_h1 = 0x00B03060;
    ctxt_col_h2 = 0x002B4FA8;
    ctxt_col_h3 = 0x009A5A00;
    ctxt_col_body = 0x001A1A1A;
    ctxt_col_rule = 0x00B8B1A3;
    ctxt_col_box_bg = 0x00EEE7D8;
    ctxt_col_box_text = 0x00223355;
    ctxt_col_link = 0x001D4ED8;
  } else {
    ctxt_col_bg = 0x001E1E2E;
    ctxt_col_h1 = 0x00F38BA8;
    ctxt_col_h2 = 0x0089B4FA;
    ctxt_col_h3 = 0x00FAB387;
    ctxt_col_body = 0x00CDD6F4;
    ctxt_col_rule = 0x00585B70;
    ctxt_col_box_bg = 0x00313244;
    ctxt_col_box_text = 0x00CDD6F4;
    ctxt_col_link = 0x0089DCEB;
  }
}

void ctxt_reset() {
  ctxt_line_count = 0;
  ctxt_link_count = 0;
  ctxt_total_h = 0;
  ctxt_total_w = 0;
  CTXT_BODY = 0;
  CTXT_H1 = 1;
  CTXT_H2 = 2;
  CTXT_H3 = 3;
  CTXT_RULE = 4;
  CTXT_CENTER = 5;
  CTXT_BOX = 6;
  CTXT_COMMENT = 7;
  CTXT_MAX_LINES = 1024;
  CTXT_MAX_LINKS = 128;
  CTXT_MAX_TEXT = 128;
  ctxt_set_theme(0);
}

int ctxt_line_h(int type) {
  if (type == CTXT_H1) return 20;
  if (type == CTXT_H2) return 16;
  if (type == CTXT_H3) return 12;
  if (type == CTXT_RULE) return 10;
  if (type == CTXT_COMMENT) return 0;
  return 10;
}

int ctxt_line_scale(int type) {
  if (type == CTXT_H1) return 2;
  if (type == CTXT_H2) return 2;
  return 1;
}

void ctxt_parse(int buf_ptr, int len) {
  char *buf = buf_ptr;
  int i = 0;
  int in_box = 0;
  int cur_box_bg = 0;
  ctxt_line_count = 0;
  ctxt_link_count = 0;

  while (i < len && ctxt_line_count < CTXT_MAX_LINES) {
    int ls = i;
    while (i < len && buf[i] != 10) i = i + 1;
    int le = i;
    if (i < len) i = i + 1;

    char line[256];
    int ll = le - ls;
    if (ll > 255) ll = 255;
    int j = 0;
    while (j < ll) {
      line[j] = buf[ls + j];
      j = j + 1;
    }
    line[ll] = 0;
    if (ll > 0 && line[ll - 1] == 13) {
      ll = ll - 1;
      line[ll] = 0;
    }

    int n = ctxt_line_count;
    ctxt_line_type[n] = CTXT_BODY;
    ctxt_line_color[n] = 0;
    ctxt_line_bg_color[n] = 0;
    ctxt_in_box[n] = in_box;
    ctxt_box_bg[n] = cur_box_bg;

    if (line[0] == 62) {
      if (ctxt_starts_with(line, ">h1 ")) {
        ctxt_line_type[n] = CTXT_H1;
        ctxt_strcpy_n(ctxt_line_text[n], line + 4, CTXT_MAX_TEXT);
      } else if (ctxt_starts_with(line, ">h2 ")) {
        ctxt_line_type[n] = CTXT_H2;
        ctxt_strcpy_n(ctxt_line_text[n], line + 4, CTXT_MAX_TEXT);
      } else if (ctxt_starts_with(line, ">h3 ")) {
        ctxt_line_type[n] = CTXT_H3;
        ctxt_strcpy_n(ctxt_line_text[n], line + 4, CTXT_MAX_TEXT);
      } else if (ctxt_starts_with(line, ">rule")) {
        ctxt_line_type[n] = CTXT_RULE;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">center ")) {
        ctxt_line_type[n] = CTXT_CENTER;
        ctxt_strcpy_n(ctxt_line_text[n], line + 8, CTXT_MAX_TEXT);
      } else if (ctxt_starts_with(line, ">comment")) {
        ctxt_line_type[n] = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">theme light")) {
        ctxt_set_theme(1);
        ctxt_line_type[n] = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">theme dark")) {
        ctxt_set_theme(0);
        ctxt_line_type[n] = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">style ")) {
        int hash_i = 0;
        int color = 0;
        while (line[hash_i] && line[hash_i] != 35) hash_i = hash_i + 1;
        if (line[hash_i] == 35) color = ctxt_parse_color(line, hash_i);
        if (color != 0) {
          if (ctxt_starts_with(line, ">style bg ")) ctxt_col_bg = color;
          else if (ctxt_starts_with(line, ">style body ")) ctxt_col_body = color;
          else if (ctxt_starts_with(line, ">style h1 ")) ctxt_col_h1 = color;
          else if (ctxt_starts_with(line, ">style h2 ")) ctxt_col_h2 = color;
          else if (ctxt_starts_with(line, ">style h3 ")) ctxt_col_h3 = color;
          else if (ctxt_starts_with(line, ">style rule ")) ctxt_col_rule = color;
          else if (ctxt_starts_with(line, ">style box ")) ctxt_col_box_bg = color;
          else if (ctxt_starts_with(line, ">style boxtext ")) ctxt_col_box_text = color;
          else if (ctxt_starts_with(line, ">style link ")) ctxt_col_link = color;
        }
        ctxt_line_type[n] = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">box")) {
        in_box = 1;
        cur_box_bg = ctxt_col_box_bg;
        if (ctxt_strlen(line) >= 11) {
          int color = ctxt_parse_color(line, 5);
          if (color != 0) cur_box_bg = color;
        }
        ctxt_line_type[n] = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">endbox")) {
        in_box = 0;
        cur_box_bg = 0;
        ctxt_line_type[n] = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else {
        ctxt_strcpy_n(ctxt_line_text[n], line, CTXT_MAX_TEXT);
      }
    } else {
      ctxt_strcpy_n(ctxt_line_text[n], line, CTXT_MAX_TEXT);
    }

    ctxt_in_box[n] = in_box;
    ctxt_box_bg[n] = cur_box_bg;
    ctxt_line_count = ctxt_line_count + 1;
  }
}

int ctxt_content_h() {
  int total = 0;
  int i = 0;
  while (i < ctxt_line_count) {
    total = total + ctxt_line_h(ctxt_line_type[i]);
    i = i + 1;
  }
  return total;
}

int ctxt_content_w() { return ctxt_total_w; }

void ctxt_render(int x, int y, int w, int h, int sy, int sx) {
  gfx2d_rect_fill(x, y, w, h, ctxt_col_bg);
  int x2 = x + w;
  int y2 = y + h;

  int py = y - sy;
  int max_w = 0;
  ctxt_link_count = 0;
  int i = 0;

  while (i < ctxt_line_count) {
    int type = ctxt_line_type[i];
    int lh = ctxt_line_h(type);

    if (type != CTXT_COMMENT && py >= y && py + lh <= y + h) {
      int fg = ctxt_col_body;
      if (type == CTXT_H1) fg = ctxt_col_h1;
      if (type == CTXT_H2) fg = ctxt_col_h2;
      if (type == CTXT_H3) fg = ctxt_col_h3;
      if (type == CTXT_CENTER) fg = ctxt_col_body;
      if (ctxt_in_box[i]) {
        int bg = ctxt_col_box_bg;
        if (ctxt_box_bg[i]) bg = ctxt_box_bg[i];
        gfx2d_rect_fill(x, py, w, lh, bg);
        fg = ctxt_col_box_text;
      }

      if (type == CTXT_RULE) {
        gfx2d_hline(x + 2, py + (lh / 2), w - 4, ctxt_col_rule);
      } else {
        char *text = ctxt_line_text[i];
        int scale = ctxt_line_scale(type);
        int ch_w = 8 * scale;
        int text_x = x + 2 - sx;
        int len = ctxt_strlen(text);
        int logical_w = len * ch_w;
        if (logical_w > max_w) max_w = logical_w;
        if (type == CTXT_CENTER) {
          int tw = len * ch_w;
          text_x = x + ((w - tw) / 2) - sx;
        }
        int px = text_x;
        int ti = 0;
        while (text[ti]) {
          if (text[ti] == 91) {
            int li = ti + 1;
            char label[64];
            char target[256];
            int lpos = 0;
            int tpos = 0;
            while (text[li] && text[li] != 93 && lpos < 63) {
              label[lpos] = text[li];
              lpos = lpos + 1;
              li = li + 1;
            }
            label[lpos] = 0;
            if (text[li] == 93) {
              int li2 = li + 1;
              while (text[li2] && ctxt_is_space(text[li2])) li2 = li2 + 1;
              if (text[li2] != 40) {
                li = -1;
              } else {
                li = li2 + 1;
              }
            } else {
              li = -1;
            }
            if (li >= 0) {
              while (text[li] && text[li] != 41 && tpos < 255) {
                target[tpos] = text[li];
                tpos = tpos + 1;
                li = li + 1;
              }
              target[tpos] = 0;
              if (text[li] == 41) {
                int ts = 0;
                int te = tpos;
                while (ts < te && ctxt_is_space(target[ts])) ts = ts + 1;
                while (te > ts && ctxt_is_space(target[te - 1])) te = te - 1;
                if (ts > 0 || te < tpos) {
                  int t2 = 0;
                  while (ts + t2 < te) {
                    target[t2] = target[ts + t2];
                    t2 = t2 + 1;
                  }
                  target[t2] = 0;
                  tpos = t2;
                }
                int k = 0;
                int ux = px;
                while (label[k]) {
                  if (px + ch_w > x && px < x2 && py + lh > y && py < y2) {
                    gfx2d_char_scaled(px, py, label[k], ctxt_col_link, scale);
                  }
                  px = px + ch_w;
                  k = k + 1;
                }
                {
                  int uw = lpos * ch_w;
                  if (ux < x) {
                    uw = uw - (x - ux);
                    ux = x;
                  }
                  if (ux + uw > x2) uw = x2 - ux;
                  if (uw > 0 && py + lh - 2 >= y && py + lh - 2 < y2) {
                    gfx2d_hline(ux, py + lh - 2, uw, ctxt_col_link);
                  }
                }
                if (ctxt_link_count < CTXT_MAX_LINKS) {
                  int lx = px - (lpos * ch_w);
                  int ly = py;
                  int lw = lpos * ch_w;
                  int lh2 = lh;
                  if (lx < x) {
                    lw = lw - (x - lx);
                    lx = x;
                  }
                  if (ly < y) {
                    lh2 = lh2 - (y - ly);
                    ly = y;
                  }
                  if (lx + lw > x2) lw = x2 - lx;
                  if (ly + lh2 > y2) lh2 = y2 - ly;
                  if (lw < 0) lw = 0;
                  if (lh2 < 0) lh2 = 0;
                  ctxt_link_x[ctxt_link_count] = lx;
                  ctxt_link_y[ctxt_link_count] = ly;
                  ctxt_link_w[ctxt_link_count] = lw;
                  ctxt_link_h[ctxt_link_count] = lh2;
                  ctxt_strcpy_n(ctxt_link_target[ctxt_link_count], target, 128);
                  ctxt_link_count = ctxt_link_count + 1;
                }
                ti = li + 1;
                continue;
              }
            }
          }
          if (ctxt_is_path_char(text[ti]) &&
              (ti == 0 || !ctxt_is_path_char(text[ti - 1]))) {
            int li = ti;
            int plen = 0;
            while (text[li] && ctxt_is_path_char(text[li]) && plen < 127) {
              li = li + 1;
              plen = plen + 1;
            }
            if (plen >= 3 && ctxt_is_bare_link_token(text, ti, li)) {
              int k = 0;
              int ux = px;
              while (k < plen) {
                if (px + ch_w > x && px < x2 && py + lh > y && py < y2) {
                  gfx2d_char_scaled(px, py, text[ti + k], ctxt_col_link, scale);
                }
                px = px + ch_w;
                k = k + 1;
              }
              {
                int uw = plen * ch_w;
                if (ux < x) {
                  uw = uw - (x - ux);
                  ux = x;
                }
                if (ux + uw > x2) uw = x2 - ux;
                if (uw > 0 && py + lh - 2 >= y && py + lh - 2 < y2) {
                  gfx2d_hline(ux, py + lh - 2, uw, ctxt_col_link);
                }
              }
              if (ctxt_link_count < CTXT_MAX_LINKS) {
                int lx = px - (plen * ch_w);
                int ly = py;
                int lw = plen * ch_w;
                int lh2 = lh;
                if (lx < x) {
                  lw = lw - (x - lx);
                  lx = x;
                }
                if (ly < y) {
                  lh2 = lh2 - (y - ly);
                  ly = y;
                }
                if (lx + lw > x2) lw = x2 - lx;
                if (ly + lh2 > y2) lh2 = y2 - ly;
                if (lw < 0) lw = 0;
                if (lh2 < 0) lh2 = 0;
                ctxt_link_x[ctxt_link_count] = lx;
                ctxt_link_y[ctxt_link_count] = ly;
                ctxt_link_w[ctxt_link_count] = lw;
                ctxt_link_h[ctxt_link_count] = lh2;
                {
                  int s = 0;
                  while (s < plen && s < 127) {
                    ctxt_link_target[ctxt_link_count][s] = text[ti + s];
                    s = s + 1;
                  }
                  ctxt_link_target[ctxt_link_count][s] = 0;
                }
                ctxt_link_count = ctxt_link_count + 1;
              }
              ti = li;
              continue;
            }
          }
          if (px + ch_w > x && px < x2 && py + lh > y && py < y2) {
            gfx2d_char_scaled(px, py, text[ti], fg, scale);
          }
          px = px + ch_w;
          ti = ti + 1;
        }
      }
    }

    py = py + lh;
    i = i + 1;
  }

  ctxt_total_h = py - (y - sy);
  ctxt_total_w = max_w + 8;
}

int ctxt_link_at(int mx, int my, int sy, int sx) {
  (void)sy;
  (void)sx;
  int i = 0;
  while (i < ctxt_link_count) {
    if (mx >= ctxt_link_x[i] && mx < ctxt_link_x[i] + ctxt_link_w[i] &&
        my >= ctxt_link_y[i] && my < ctxt_link_y[i] + ctxt_link_h[i]) {
      return i;
    }
    i = i + 1;
  }
  return -1;
}

void ctxt_get_link(int idx, int out_ptr, int size) {
  if (idx < 0 || idx >= ctxt_link_count) return;
  char *out = out_ptr;
  ctxt_strcpy_n(out, ctxt_link_target[idx], size);
}
