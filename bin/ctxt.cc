#pragma once

int CTXT_BODY;
int CTXT_H1;
int CTXT_H2;
int CTXT_H3;
int CTXT_RULE;
int CTXT_CENTER;
int CTXT_COMMENT;
int CTXT_BUTTON;
int CTXT_CODE_HEADER;
int CTXT_CODE;
int CTXT_SPRITE;
int CTXT_TREE;

int CTXT_ACT_OPEN;
int CTXT_ACT_SHELL;
int CTXT_ACT_REPL;
int CTXT_ACT_TREE;

int CTXT_MAX_LINES;
int CTXT_MAX_LINKS;
int CTXT_MAX_TEXT;

int ctxt_line_count;
int ctxt_line_type[1024];
char ctxt_line_text[1024][128];
int ctxt_line_color[1024];
int ctxt_line_bg_color[1024];
int ctxt_line_ref[1024];
int ctxt_line_aux_a[1024];
int ctxt_line_aux_b[1024];
int ctxt_line_tree_mask[1024];

int ctxt_link_count;
char ctxt_link_target[192][128];
int ctxt_link_action[192];
int ctxt_link_ref[192];
int ctxt_link_x[192];
int ctxt_link_y[192];
int ctxt_link_w[192];
int ctxt_link_h[192];

int ctxt_action_count;
int ctxt_action_type[128];
char ctxt_action_payload[128][512];

int ctxt_sprite_count;
char ctxt_sprite_path[64][128];
int ctxt_sprite_w[64];
int ctxt_sprite_h[64];
int ctxt_sprite_action[64];

int ctxt_tree_count;
int ctxt_tree_open[32];

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
int ctxt_col_link_hover;
int ctxt_col_code_bg;
int ctxt_col_code_text;

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

void ctxt_strcat_n(char *dst, char *src, int max) {
  int d = 0;
  int s = 0;
  while (dst[d] && d < max - 1) d = d + 1;
  while (src[s] && d < max - 1) {
    dst[d] = src[s];
    d = d + 1;
    s = s + 1;
  }
  dst[d] = 0;
}

int ctxt_strcmp(char *a, char *b) {
  int i = 0;
  while (a[i] && b[i] && a[i] == b[i]) i = i + 1;
  return a[i] - b[i];
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
  if (c == 47 || c == 46 || c == 95 || c == 45 || c == 58) return 1;
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

int ctxt_trim_copy(char *src, char *dst, int max) {
  int s = 0;
  int e = ctxt_strlen(src);
  int i = 0;
  while (src[s] && ctxt_is_space(src[s])) s = s + 1;
  while (e > s && ctxt_is_space(src[e - 1])) e = e - 1;
  while (s < e && i < max - 1) {
    dst[i] = src[s];
    i = i + 1;
    s = s + 1;
  }
  dst[i] = 0;
  return i;
}

int ctxt_parse_u32(char *src) {
  int i = 0;
  int v = 0;
  while (src[i] && ctxt_is_space(src[i])) i = i + 1;
  while (src[i] >= 48 && src[i] <= 57) {
    v = v * 10 + (src[i] - 48);
    i = i + 1;
  }
  return v;
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
    ctxt_col_link_hover = 0x000B3AA8;
    ctxt_col_code_bg = 0x00E7E0D2;
    ctxt_col_code_text = 0x001A1A1A;
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
    ctxt_col_link_hover = 0x00BDEBFF;
    ctxt_col_code_bg = 0x00242A3B;
    ctxt_col_code_text = 0x00E6EDF7;
  }
}

void ctxt_reset() {
  ctxt_line_count = 0;
  ctxt_link_count = 0;
  ctxt_action_count = 0;
  ctxt_sprite_count = 0;
  ctxt_tree_count = 0;
  ctxt_total_h = 0;
  ctxt_total_w = 0;
  CTXT_BODY = 0;
  CTXT_H1 = 1;
  CTXT_H2 = 2;
  CTXT_H3 = 3;
  CTXT_RULE = 4;
  CTXT_CENTER = 5;
  CTXT_COMMENT = 6;
  CTXT_BUTTON = 7;
  CTXT_CODE_HEADER = 8;
  CTXT_CODE = 9;
  CTXT_SPRITE = 10;
  CTXT_TREE = 11;
  CTXT_ACT_OPEN = 0;
  CTXT_ACT_SHELL = 1;
  CTXT_ACT_REPL = 2;
  CTXT_ACT_TREE = 3;
  CTXT_MAX_LINES = 1024;
  CTXT_MAX_LINKS = 192;
  CTXT_MAX_TEXT = 128;
  ctxt_set_theme(0);
}

int ctxt_line_h(int type) {
  if (type == CTXT_H1) return 20;
  if (type == CTXT_H2) return 16;
  if (type == CTXT_H3) return 12;
  if (type == CTXT_RULE) return 10;
  if (type == CTXT_BUTTON) return 18;
  if (type == CTXT_TREE) return 16;
  if (type == CTXT_CODE_HEADER) return 18;
  if (type == CTXT_CODE) return 12;
  if (type == CTXT_SPRITE) return 12;
  if (type == CTXT_COMMENT) return 0;
  return 10;
}

int ctxt_line_scale(int type) {
  if (type == CTXT_H1) return 2;
  if (type == CTXT_H2) return 2;
  return 1;
}

int ctxt_add_action(int type, char *payload) {
  int idx = ctxt_action_count;
  if (idx >= 128) return -1;
  ctxt_action_type[idx] = type;
  ctxt_strcpy_n(ctxt_action_payload[idx], payload, 512);
  ctxt_action_count = ctxt_action_count + 1;
  return idx;
}

int ctxt_add_sprite(char *path, int w, int h, int action_ref) {
  int idx = ctxt_sprite_count;
  if (idx >= 64) return -1;
  ctxt_strcpy_n(ctxt_sprite_path[idx], path, 128);
  ctxt_sprite_w[idx] = w;
  ctxt_sprite_h[idx] = h;
  ctxt_sprite_action[idx] = action_ref;
  ctxt_sprite_count = ctxt_sprite_count + 1;
  return idx;
}

int ctxt_add_tree(int open) {
  int idx = ctxt_tree_count;
  if (idx >= 32) return -1;
  ctxt_tree_open[idx] = open;
  ctxt_tree_count = ctxt_tree_count + 1;
  return idx;
}

int ctxt_parse_action(char *src, int type_out_ptr, int payload_out_ptr, int payload_max) {
  char *payload = payload_out_ptr;
  int type = CTXT_ACT_OPEN;
  char tmp[512];
  int i = 0;
  while (src[i] && ctxt_is_space(src[i])) i = i + 1;
  ctxt_strcpy_n(tmp, src + i, 512);
  if (ctxt_starts_with(tmp, "open:")) {
    type = CTXT_ACT_OPEN;
    ctxt_trim_copy(tmp + 5, payload, payload_max);
  } else if (ctxt_starts_with(tmp, "shell:")) {
    type = CTXT_ACT_SHELL;
    ctxt_trim_copy(tmp + 6, payload, payload_max);
  } else if (ctxt_starts_with(tmp, "repl:")) {
    type = CTXT_ACT_REPL;
    ctxt_trim_copy(tmp + 5, payload, payload_max);
  } else {
    type = CTXT_ACT_SHELL;
    ctxt_trim_copy(tmp, payload, payload_max);
  }
  int *type_out = type_out_ptr;
  *type_out = type;
  return payload[0] != 0;
}

int ctxt_line_visible(int idx) {
  int mask = ctxt_line_tree_mask[idx];
  int bit = 0;
  while (bit < ctxt_tree_count && bit < 32) {
    if ((mask & (1 << bit)) && !ctxt_tree_open[bit]) return 0;
    bit = bit + 1;
  }
  return 1;
}

void ctxt_refresh_metrics() {
  int i = 0;
  int total_h = 0;
  int max_w = 0;
  while (i < ctxt_line_count) {
    int type = ctxt_line_type[i];
    int lh = ctxt_line_h(type);
    int lw = 0;
    if (ctxt_line_visible(i) && type != CTXT_COMMENT) {
      if (type == CTXT_BUTTON || type == CTXT_TREE) {
        lw = gfx2d_text_width(ctxt_line_text[i], 1) + 20;
      } else if (type == CTXT_CODE_HEADER) {
        lw = gfx2d_text_width(ctxt_line_text[i], 1) + 72;
      } else if (type == CTXT_SPRITE) {
        lw = ctxt_line_aux_a[i] + 8;
        total_h = total_h + ctxt_line_aux_b[i];
      } else {
        lw = gfx2d_text_width(ctxt_line_text[i], ctxt_line_scale(type));
      }
      if (lw > max_w) max_w = lw;
      total_h = total_h + lh;
    }
    i = i + 1;
  }
  ctxt_total_h = total_h;
  ctxt_total_w = max_w + 8;
}

void ctxt_parse(int buf_ptr, int len) {
  char *buf = buf_ptr;
  int i = 0;
  int in_box = 0;
  int cur_box_bg = 0;
  int tree_mask = 0;
  int tree_stack[32];
  int tree_top = 0;
  int code_action = -1;

  ctxt_line_count = 0;
  ctxt_link_count = 0;
  ctxt_action_count = 0;
  ctxt_sprite_count = 0;
  ctxt_tree_count = 0;

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
    int type = CTXT_BODY;
    int ref = -1;
    int aux_a = 0;
    int aux_b = 0;

    ctxt_line_color[n] = 0;
    ctxt_line_bg_color[n] = 0;
    ctxt_line_ref[n] = -1;
    ctxt_line_aux_a[n] = 0;
    ctxt_line_aux_b[n] = 0;
    ctxt_line_tree_mask[n] = tree_mask;

    if (code_action >= 0) {
      if (ctxt_starts_with(line, ">endcode")) {
        code_action = -1;
        continue;
      }
      type = CTXT_CODE;
      ref = code_action;
      ctxt_strcpy_n(ctxt_line_text[n], line, CTXT_MAX_TEXT);
      if (ctxt_action_payload[code_action][0]) ctxt_strcat_n(ctxt_action_payload[code_action], "\n", 512);
      ctxt_strcat_n(ctxt_action_payload[code_action], line, 512);
    } else if (line[0] == 62) {
      if (ctxt_starts_with(line, ">h1 ")) {
        type = CTXT_H1;
        ctxt_strcpy_n(ctxt_line_text[n], line + 4, CTXT_MAX_TEXT);
      } else if (ctxt_starts_with(line, ">h2 ")) {
        type = CTXT_H2;
        ctxt_strcpy_n(ctxt_line_text[n], line + 4, CTXT_MAX_TEXT);
      } else if (ctxt_starts_with(line, ">h3 ")) {
        type = CTXT_H3;
        ctxt_strcpy_n(ctxt_line_text[n], line + 4, CTXT_MAX_TEXT);
      } else if (ctxt_starts_with(line, ">rule")) {
        type = CTXT_RULE;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">center ")) {
        type = CTXT_CENTER;
        ctxt_strcpy_n(ctxt_line_text[n], line + 8, CTXT_MAX_TEXT);
      } else if (ctxt_starts_with(line, ">comment")) {
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">theme light")) {
        ctxt_set_theme(1);
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">theme dark")) {
        ctxt_set_theme(0);
        type = CTXT_COMMENT;
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
          else if (ctxt_starts_with(line, ">style codebg ")) ctxt_col_code_bg = color;
          else if (ctxt_starts_with(line, ">style codetext ")) ctxt_col_code_text = color;
          else if (ctxt_starts_with(line, ">style link ")) ctxt_col_link = color;
          else if (ctxt_starts_with(line, ">style linkhover ")) ctxt_col_link_hover = color;
        }
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">box")) {
        in_box = 1;
        cur_box_bg = ctxt_col_box_bg;
        if (ctxt_strlen(line) >= 11) {
          int color = ctxt_parse_color(line, 5);
          if (color != 0) cur_box_bg = color;
        }
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">endbox")) {
        in_box = 0;
        cur_box_bg = 0;
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">button ")) {
        char label[128];
        char action_src[512];
        char payload[512];
        int action_type = CTXT_ACT_SHELL;
        int p = 8;
        int l = 0;
        int a = 0;
        while (line[p] && !(line[p] == '|' )) {
          if (l < 127) label[l] = line[p];
          l = l + 1;
          p = p + 1;
        }
        if (l > 127) l = 127;
        label[l] = 0;
        if (line[p] == '|') p = p + 1;
        while (line[p]) {
          if (a < 511) action_src[a] = line[p];
          a = a + 1;
          p = p + 1;
        }
        if (a > 511) a = 511;
        action_src[a] = 0;
        ctxt_trim_copy(label, ctxt_line_text[n], CTXT_MAX_TEXT);
        if (ctxt_parse_action(action_src, &action_type, payload, 512)) {
          type = CTXT_BUTTON;
          ref = ctxt_add_action(action_type, payload);
        } else {
          type = CTXT_COMMENT;
          ctxt_line_text[n][0] = 0;
        }
      } else if (ctxt_starts_with(line, ">code")) {
        char label[128];
        char payload[2];
        payload[0] = 0;
        payload[1] = 0;
        ctxt_trim_copy(line + 5, label, 128);
        if (!label[0]) ctxt_strcpy_n(label, "Run CupidC", 128);
        type = CTXT_CODE_HEADER;
        ref = ctxt_add_action(CTXT_ACT_REPL, payload);
        ctxt_strcpy_n(ctxt_line_text[n], label, CTXT_MAX_TEXT);
        code_action = ref;
      } else if (ctxt_starts_with(line, ">tree")) {
        char label[128];
        int open = 0;
        char *arg = line + 5;
        while (*arg && ctxt_is_space(*arg)) arg = arg + 1;
        if (ctxt_starts_with(arg, "open ")) {
          open = 1;
          arg = arg + 5;
        } else if (ctxt_starts_with(arg, "closed ")) {
          arg = arg + 7;
        }
        ctxt_trim_copy(arg, label, 128);
        if (!label[0]) ctxt_strcpy_n(label, "Section", 128);
        type = CTXT_TREE;
        ctxt_strcpy_n(ctxt_line_text[n], label, CTXT_MAX_TEXT);
        ref = ctxt_add_tree(open);
        if (ref >= 0 && tree_top < 32) {
          tree_stack[tree_top] = ref;
          tree_top = tree_top + 1;
          tree_mask = tree_mask | (1 << ref);
        }
      } else if (ctxt_starts_with(line, ">endtree")) {
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
        if (tree_top > 0) {
          tree_top = tree_top - 1;
          tree_mask = tree_mask & ~(1 << tree_stack[tree_top]);
        }
      } else if (ctxt_starts_with(line, ">sprite ")) {
        char sprite_args[256];
        char path[128];
        char size1[32];
        char size2[32];
        char action_src[512];
        char payload[512];
        int action_type = CTXT_ACT_OPEN;
        int action_ref = -1;
        int p = 8;
        int q = 0;
        int s = 0;
        int draw_w = 0;
        int draw_h = 0;
        int cut = 0;
        while (line[p + cut] && line[p + cut] != '|') {
          if (cut < 255) sprite_args[cut] = line[p + cut];
          cut = cut + 1;
        }
        if (cut > 255) cut = 255;
        sprite_args[cut] = 0;
        if (line[p + cut] == '|') {
          int a = 0;
          p = p + cut + 1;
          while (line[p] && a < 511) {
            action_src[a] = line[p];
            a = a + 1;
            p = p + 1;
          }
          action_src[a] = 0;
          if (ctxt_parse_action(action_src, &action_type, payload, 512)) {
            action_ref = ctxt_add_action(action_type, payload);
          }
          p = 0;
        } else {
          p = 8;
        }
        if (sprite_args[0]) {
          p = 0;
          while (sprite_args[p] && ctxt_is_space(sprite_args[p])) p = p + 1;
        }
        while (sprite_args[p] && !ctxt_is_space(sprite_args[p]) && q < 127) {
          path[q] = sprite_args[p];
          q = q + 1;
          p = p + 1;
        }
        path[q] = 0;
        while (sprite_args[p] && ctxt_is_space(sprite_args[p])) p = p + 1;
        while (sprite_args[p] && !ctxt_is_space(sprite_args[p]) && s < 31) {
          size1[s] = sprite_args[p];
          s = s + 1;
          p = p + 1;
        }
        size1[s] = 0;
        while (sprite_args[p] && ctxt_is_space(sprite_args[p])) p = p + 1;
        s = 0;
        while (sprite_args[p] && !ctxt_is_space(sprite_args[p]) && s < 31) {
          size2[s] = sprite_args[p];
          s = s + 1;
          p = p + 1;
        }
        size2[s] = 0;
        draw_w = ctxt_parse_u32(size1);
        draw_h = ctxt_parse_u32(size2);
        if (draw_w <= 0) draw_w = 96;
        if (draw_h <= 0) draw_h = draw_w;
        type = CTXT_SPRITE;
        ref = ctxt_add_sprite(path, draw_w, draw_h, action_ref);
        aux_a = draw_w;
        aux_b = draw_h;
        ctxt_strcpy_n(ctxt_line_text[n], path, CTXT_MAX_TEXT);
      } else {
        ctxt_strcpy_n(ctxt_line_text[n], line, CTXT_MAX_TEXT);
      }
    } else {
      ctxt_strcpy_n(ctxt_line_text[n], line, CTXT_MAX_TEXT);
    }

    ctxt_line_type[n] = type;
    ctxt_line_ref[n] = ref;
    ctxt_line_aux_a[n] = aux_a;
    ctxt_line_aux_b[n] = aux_b;
    ctxt_line_tree_mask[n] = tree_mask;
    ctxt_line_bg_color[n] = in_box ? cur_box_bg : 0;
    ctxt_line_count = ctxt_line_count + 1;
  }

  ctxt_refresh_metrics();
}

int ctxt_content_h() {
  return ctxt_total_h;
}

int ctxt_content_w() { return ctxt_total_w; }

void ctxt_add_link_rect(int x, int y, int w, int h, int action, int ref, char *target) {
  if (ctxt_link_count >= CTXT_MAX_LINKS) return;
  ctxt_link_x[ctxt_link_count] = x;
  ctxt_link_y[ctxt_link_count] = y;
  ctxt_link_w[ctxt_link_count] = w;
  ctxt_link_h[ctxt_link_count] = h;
  ctxt_link_action[ctxt_link_count] = action;
  ctxt_link_ref[ctxt_link_count] = ref;
  ctxt_strcpy_n(ctxt_link_target[ctxt_link_count], target, 128);
  ctxt_link_count = ctxt_link_count + 1;
}

void ctxt_draw_text_links(int base_x, int py, int x, int x2, int y, int y2,
                          int lh, int scale, int fg, char *text) {
  int ch_w = 8 * scale;
  int px = base_x;
  int ti = 0;
  while (text[ti]) {
    if (text[ti] == 91) {
      int li = ti + 1;
      char label[64];
      char target[256];
      char payload[256];
      int action_type = CTXT_ACT_OPEN;
      int lpos = 0;
      int tpos = 0;
      while (text[li] && text[li] != 93 && lpos < 63) {
        label[lpos] = text[li];
        lpos = lpos + 1;
        li = li + 1;
      }
      label[lpos] = 0;
      if (text[li] == 93 && text[li + 1] == 40) {
        li = li + 2;
        while (text[li] && text[li] != 41 && tpos < 255) {
          target[tpos] = text[li];
          tpos = tpos + 1;
          li = li + 1;
        }
        target[tpos] = 0;
        if (text[li] == 41) {
          int ux = px;
          int k = 0;
          while (label[k]) {
            if (px + ch_w > x && px < x2 && py + lh > y && py < y2) {
              gfx2d_char_scaled(px, py, label[k], ctxt_col_link, scale);
            }
            px = px + ch_w;
            k = k + 1;
          }
          if (ctxt_parse_action(target, &action_type, payload, 256)) {
            int uw = lpos * ch_w;
            if (uw > 0) {
              gfx2d_hline(ux, py + lh - 2, uw, ctxt_col_link);
              ctxt_add_link_rect(ux, py, uw, lh, action_type, -1, payload);
            }
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
        int ux = px;
        int k = 0;
        while (k < plen) {
          if (px + ch_w > x && px < x2 && py + lh > y && py < y2) {
            gfx2d_char_scaled(px, py, text[ti + k], ctxt_col_link, scale);
          }
          px = px + ch_w;
          k = k + 1;
        }
        gfx2d_hline(ux, py + lh - 2, plen * ch_w, ctxt_col_link);
        {
          char tok[128];
          k = 0;
          while (k < plen && k < 127) {
            tok[k] = text[ti + k];
            k = k + 1;
          }
          tok[k] = 0;
          ctxt_add_link_rect(ux, py, plen * ch_w, lh, CTXT_ACT_OPEN, -1, tok);
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

void ctxt_draw_sprite(int x, int y, int w, int h, char *path) {
  int img = gfx2d_image_load(path);
  if (img >= 0) {
    gfx2d_image_draw_scaled(img, x, y, w, h);
    gfx2d_image_free(img);
    gfx2d_rect(x, y, w, h, ctxt_col_rule);
  } else {
    gfx2d_rect(x, y, w, h, ctxt_col_rule);
    gfx2d_text(x + 4, y + 4, "(missing sprite)", ctxt_col_rule, 1);
  }
}

void ctxt_render(int x, int y, int w, int h, int sy, int sx) {
  gfx2d_rect_fill(x, y, w, h, ctxt_col_bg);
  int x2 = x + w;
  int y2 = y + h;
  int py = y - sy;
  ctxt_link_count = 0;
  int panel_bg = ctxt_theme_light ? 0x00D8D4CC : 0x003A4152;
  int panel_text = ctxt_theme_light ? ctxt_col_body : ctxt_col_box_text;
  int code_button_bg = ctxt_theme_light ? 0x00D9E7F8 : 0x00303D57;
  int code_button_text = ctxt_col_code_text;

  int i = 0;
  while (i < ctxt_line_count) {
    int type = ctxt_line_type[i];
    int lh = ctxt_line_h(type);

    if (ctxt_line_visible(i) && type != CTXT_COMMENT) {
      if (py + lh >= y && py <= y2) {
        int fg = ctxt_col_body;
        if (type == CTXT_H1) fg = ctxt_col_h1;
        if (type == CTXT_H2) fg = ctxt_col_h2;
        if (type == CTXT_H3) fg = ctxt_col_h3;

        if (ctxt_line_bg_color[i]) {
          gfx2d_rect_fill(x, py, w, lh, ctxt_line_bg_color[i]);
          gfx2d_vline(x, py, lh, ctxt_col_rule);
          fg = ctxt_col_box_text;
        }

        if (type == CTXT_RULE) {
          gfx2d_hline(x + 2, py + (lh / 2), w - 4, ctxt_col_rule);
        } else if (type == CTXT_BUTTON) {
          int bw = gfx2d_text_width(ctxt_line_text[i], 1) + 20;
          gfx2d_panel(x + 2, py, bw, lh - 2, panel_bg);
          gfx2d_text(x + 10, py + 4, ctxt_line_text[i], panel_text, 1);
          if (ctxt_line_ref[i] >= 0 && ctxt_line_ref[i] < ctxt_action_count) {
            ctxt_add_link_rect(x + 2, py, bw, lh - 2,
                               ctxt_action_type[ctxt_line_ref[i]],
                               ctxt_line_ref[i],
                               ctxt_action_payload[ctxt_line_ref[i]]);
          }
        } else if (type == CTXT_TREE) {
          char label[128];
          int bw;
          label[0] = ctxt_tree_open[ctxt_line_ref[i]] ? '-' : '+';
          label[1] = ' ';
          label[2] = 0;
          ctxt_strcat_n(label, ctxt_line_text[i], 128);
          bw = gfx2d_text_width(label, 1) + 12;
          gfx2d_panel(x + 2, py, bw, lh - 1, panel_bg);
          gfx2d_text(x + 8, py + 3, label, panel_text, 1);
          ctxt_add_link_rect(x + 2, py, bw, lh - 1, CTXT_ACT_TREE,
                             ctxt_line_ref[i], label);
        } else if (type == CTXT_CODE_HEADER) {
          int bw = 58;
          gfx2d_rect_fill(x + 2, py, w - 4, lh, ctxt_col_code_bg);
          gfx2d_rect(x + 2, py, w - 4, lh, ctxt_col_rule);
          gfx2d_text(x + 8, py + 4, ctxt_line_text[i], ctxt_col_code_text, 1);
          gfx2d_panel(x + w - bw - 6, py + 1, bw, lh - 2, code_button_bg);
          gfx2d_text(x + w - bw + 10, py + 4, "Run", code_button_text, 1);
          if (ctxt_line_ref[i] >= 0 && ctxt_line_ref[i] < ctxt_action_count) {
            ctxt_add_link_rect(x + 2, py, w - 4, lh,
                               CTXT_ACT_REPL, ctxt_line_ref[i],
                               ctxt_action_payload[ctxt_line_ref[i]]);
            ctxt_add_link_rect(x + w - bw - 6, py + 1, bw, lh - 2,
                               CTXT_ACT_REPL, ctxt_line_ref[i],
                               ctxt_action_payload[ctxt_line_ref[i]]);
          }
        } else if (type == CTXT_CODE) {
          gfx2d_rect_fill(x + 2, py, w - 4, lh, ctxt_col_code_bg);
          gfx2d_rect(x + 2, py, w - 4, lh, ctxt_col_rule);
          gfx2d_text(x + 8, py + 2, ctxt_line_text[i], ctxt_col_code_text, 1);
          if (ctxt_line_ref[i] >= 0 && ctxt_line_ref[i] < ctxt_action_count) {
            ctxt_add_link_rect(x + 2, py, w - 4, lh, CTXT_ACT_REPL,
                               ctxt_line_ref[i],
                               ctxt_action_payload[ctxt_line_ref[i]]);
          }
        } else if (type == CTXT_SPRITE) {
          gfx2d_rect_fill(x + 2, py, ctxt_line_aux_a[i] + 8, ctxt_line_aux_b[i] + 8,
                          ctxt_col_code_bg);
          if (ctxt_line_ref[i] >= 0 && ctxt_line_ref[i] < ctxt_sprite_count) {
            ctxt_draw_sprite(x + 6, py + 4, ctxt_line_aux_a[i], ctxt_line_aux_b[i],
                             ctxt_sprite_path[ctxt_line_ref[i]]);
            if (ctxt_sprite_action[ctxt_line_ref[i]] >= 0 &&
                ctxt_sprite_action[ctxt_line_ref[i]] < ctxt_action_count) {
              int action_ref = ctxt_sprite_action[ctxt_line_ref[i]];
              ctxt_add_link_rect(x + 2, py, ctxt_line_aux_a[i] + 8,
                                 ctxt_line_aux_b[i] + 8,
                                 ctxt_action_type[action_ref], action_ref,
                                 ctxt_action_payload[action_ref]);
            }
          }
        } else {
          int scale = ctxt_line_scale(type);
          int text_x = x + 2 - sx;
          if (type == CTXT_CENTER) {
            int tw = gfx2d_text_width(ctxt_line_text[i], scale);
            text_x = x + ((w - tw) / 2) - sx;
          }
          ctxt_draw_text_links(text_x, py, x, x2, y, y2, lh, scale, fg,
                               ctxt_line_text[i]);
        }
      }
      py = py + lh;
      if (type == CTXT_SPRITE) py = py + ctxt_line_aux_b[i];
    }
    i = i + 1;
  }

  ctxt_refresh_metrics();
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

int ctxt_get_link_action(int idx) {
  if (idx < 0 || idx >= ctxt_link_count) return -1;
  return ctxt_link_action[idx];
}

int ctxt_get_link_ref(int idx) {
  if (idx < 0 || idx >= ctxt_link_count) return -1;
  return ctxt_link_ref[idx];
}

void ctxt_toggle_tree(int idx) {
  if (idx < 0 || idx >= ctxt_tree_count) return;
  ctxt_tree_open[idx] = 1 - ctxt_tree_open[idx];
  ctxt_refresh_metrics();
}
