#include "ansi.h"
#include "string.h"

void ansi_init(terminal_color_state_t *state) {
  state->fg_color = ANSI_DEFAULT_FG;
  state->bg_color = ANSI_DEFAULT_BG;
  state->bold = false;
  state->inverse = false;
  state->g0_alt_charset = false;
  state->param1 = 0;
  state->param2 = 0;
  state->esc_len = 0;
  state->in_escape = false;
  state->in_csi = false;
  state->in_osc = false;
  state->osc_esc = false;
  state->in_charset_select = false;
  memset(state->esc_buf, 0, ANSI_ESC_BUF_SIZE);
}

void ansi_reset(terminal_color_state_t *state) {
  state->fg_color = ANSI_DEFAULT_FG;
  state->bg_color = ANSI_DEFAULT_BG;
  state->bold = false;
  state->inverse = false;
}

uint8_t ansi_get_fg(const terminal_color_state_t *state) {
  uint8_t fg = state->inverse ? state->bg_color : state->fg_color;
  if (state->bold && fg < 8) {
    fg = (uint8_t)(fg + 8);
  }
  return fg;
}

uint8_t ansi_get_bg(const terminal_color_state_t *state) {
  return state->inverse ? state->fg_color : state->bg_color;
}

static const uint8_t ansi_to_vga[8] = {
  0, 4, 2, 6, 1, 5, 3, 7
};

static const uint32_t vga_to_rgb32[16] = {
  0x00141418U, 0x000060A8U, 0x00408040U, 0x004090A8U,
  0x00A04040U, 0x00885088U, 0x00907030U, 0x00C8C8C8U,
  0x00505060U, 0x00B8DDFFU, 0x0090D090U, 0x0090D8E8U,
  0x00FF9090U, 0x00F0C0F0U, 0x00F0E060U, 0x00F8F8F8U
};

uint32_t ansi_vga_to_palette(uint8_t vga_color) {
  if (vga_color > 15) return vga_to_rgb32[7];
  return vga_to_rgb32[vga_color];
}

static uint8_t ansi_rgb_to_vga(int r, int g, int b) {
  int best = 7;
  int best_dist = 0x7fffffff;

  if (r < 0) r = 0;
  if (g < 0) g = 0;
  if (b < 0) b = 0;
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;

  for (int i = 0; i < 16; i++) {
    int pr = (int)((vga_to_rgb32[i] >> 16) & 0xFFU);
    int pg = (int)((vga_to_rgb32[i] >> 8) & 0xFFU);
    int pb = (int)(vga_to_rgb32[i] & 0xFFU);
    int dr = r - pr;
    int dg = g - pg;
    int db = b - pb;
    int dist = dr * dr + dg * dg + db * db;
    if (dist < best_dist) {
      best_dist = dist;
      best = i;
    }
  }

  return (uint8_t)best;
}

static int parse_number(const char *buf, int start, int end) {
  int val = 0;
  bool found = false;
  for (int i = start; i < end; i++) {
    if (buf[i] >= '0' && buf[i] <= '9') {
      val = val * 10 + (buf[i] - '0');
      found = true;
    }
  }
  return found ? val : -1;
}

static int parse_csi_params(const char *buf, int len, int *params, int max_params) {
  int count = 0;
  int start = 0;

  while (start < len && (buf[start] == '?' || buf[start] == '>' ||
                         buf[start] == '=' || buf[start] == '!')) {
    start++;
  }

  for (int i = start; i <= len; i++) {
    if (i == len || buf[i] == ';' || buf[i] == ':') {
      if (count < max_params) {
        params[count++] = parse_number(buf, start, i);
      }
      start = i + 1;
    }
  }

  return count;
}

static int csi_is_private(const char *buf, int len) {
  return (len > 0 && (buf[0] == '?' || buf[0] == '>' ||
                      buf[0] == '=' || buf[0] == '!')) ? 1 : 0;
}

static ansi_result_t process_csi(terminal_color_state_t *state) {
  int len = state->esc_len;
  if (len < 1) return ANSI_RESULT_SKIP;

  char final_char = state->esc_buf[len - 1];
  int body_len = len - 1;
  int params[16];
  int param_count = parse_csi_params(state->esc_buf, body_len, params, 16);
  int private_mode = csi_is_private(state->esc_buf, body_len);

  state->param1 = 0;
  state->param2 = 0;

  if (final_char == 'm') {
    if (param_count == 0 || (param_count == 1 && params[0] == -1)) {
      ansi_reset(state);
      return ANSI_RESULT_SKIP;
    }

    for (int p = 0; p < param_count; p++) {
      int code = params[p];
      if (code < 0) continue;

      if (code == 0) {
        ansi_reset(state);
      } else if (code == 1) {
        state->bold = true;
      } else if (code == 7) {
        state->inverse = true;
      } else if (code == 22) {
        state->bold = false;
      } else if (code == 27) {
        state->inverse = false;
      } else if (code >= 30 && code <= 37) {
        state->fg_color = ansi_to_vga[code - 30];
      } else if (code == 39) {
        state->fg_color = ANSI_DEFAULT_FG;
      } else if (code >= 40 && code <= 47) {
        state->bg_color = ansi_to_vga[code - 40];
      } else if (code == 49) {
        state->bg_color = ANSI_DEFAULT_BG;
      } else if (code >= 90 && code <= 97) {
        state->fg_color = (uint8_t)(ansi_to_vga[code - 90] + 8);
      } else if (code >= 100 && code <= 107) {
        state->bg_color = (uint8_t)(ansi_to_vga[code - 100] + 8);
      } else if ((code == 38 || code == 48) && p + 4 < param_count &&
                 params[p + 1] == 2) {
        uint8_t mapped = ansi_rgb_to_vga(params[p + 2],
                                         params[p + 3],
                                         params[p + 4]);
        if (code == 38) state->fg_color = mapped;
        else state->bg_color = mapped;
        p += 4;
      } else if ((code == 38 || code == 48) && p + 2 < param_count &&
                 params[p + 1] == 5) {
        int color = params[p + 2];
        if (color >= 0) {
          uint8_t mapped = (uint8_t)(color & 15);
          if (code == 38) state->fg_color = mapped;
          else state->bg_color = mapped;
        }
        p += 2;
      }
    }
    return ANSI_RESULT_SKIP;
  }

  if (final_char == 'H' || final_char == 'f') {
    int row = (param_count > 0 && params[0] > 0) ? params[0] : 1;
    int col = (param_count > 1 && params[1] > 0) ? params[1] : 1;
    state->param1 = row - 1;
    state->param2 = col - 1;
    return ANSI_RESULT_CURSOR_POS;
  }

  if (final_char == 'A' || final_char == 'B' || final_char == 'C' ||
      final_char == 'D' || final_char == 'E' || final_char == 'F') {
    int n = (param_count > 0 && params[0] > 0) ? params[0] : 1;
    state->param1 = n;
    state->param2 = 0;
    if (final_char == 'A') return ANSI_RESULT_CURSOR_UP;
    if (final_char == 'B') return ANSI_RESULT_CURSOR_DOWN;
    if (final_char == 'C') return ANSI_RESULT_CURSOR_FORWARD;
    if (final_char == 'D') return ANSI_RESULT_CURSOR_BACK;
    if (final_char == 'E') {
      state->param2 = 1;
      return ANSI_RESULT_CURSOR_DOWN;
    }
    state->param2 = 1;
    return ANSI_RESULT_CURSOR_UP;
  }

  if (final_char == 'G' || final_char == '`') {
    int col = (param_count > 0 && params[0] > 0) ? params[0] : 1;
    state->param1 = col - 1;
    return ANSI_RESULT_CURSOR_COL;
  }

  if (final_char == 'd') {
    int row = (param_count > 0 && params[0] > 0) ? params[0] : 1;
    state->param1 = row - 1;
    return ANSI_RESULT_CURSOR_ROW;
  }

  if (final_char == 'J') {
    int param = (param_count > 0 && params[0] >= 0) ? params[0] : 0;
    state->param1 = param;
    if (param == 2 || param == 3) {
      return ANSI_RESULT_CLEAR;
    }
    return ANSI_RESULT_ERASE_DISPLAY;
  }

  if (final_char == 'K') {
    int param = (param_count > 0 && params[0] >= 0) ? params[0] : 0;
    state->param1 = param;
    return ANSI_RESULT_ERASE_LINE;
  }

  if (final_char == 's') {
    return ANSI_RESULT_SAVE_CURSOR;
  }

  if (final_char == 'u') {
    return ANSI_RESULT_RESTORE_CURSOR;
  }

  if (final_char == 'S' || final_char == 'T') {
    int n = (param_count > 0 && params[0] > 0) ? params[0] : 1;
    state->param1 = n;
    return final_char == 'S' ? ANSI_RESULT_SCROLL_UP : ANSI_RESULT_SCROLL_DOWN;
  }

  if (final_char == 'h' || final_char == 'l') {
    state->param1 = (param_count > 0 && params[0] >= 0) ? params[0] : 0;
    state->param2 = private_mode;
    return final_char == 'h' ? ANSI_RESULT_SET_MODE : ANSI_RESULT_RESET_MODE;
  }

  if (final_char == 'r') {
    int top = (param_count > 0 && params[0] > 0) ? params[0] : 1;
    int bottom = (param_count > 1 && params[1] > 0) ? params[1] : 0;
    state->param1 = top - 1;
    state->param2 = bottom > 0 ? bottom - 1 : -1;
    return ANSI_RESULT_SET_SCROLL_REGION;
  }

  if (final_char == 'L' || final_char == 'M' || final_char == '@' ||
      final_char == 'P' || final_char == 'X') {
    int n = (param_count > 0 && params[0] > 0) ? params[0] : 1;
    state->param1 = n;
    if (final_char == 'L') return ANSI_RESULT_INSERT_LINE;
    if (final_char == 'M') return ANSI_RESULT_DELETE_LINE;
    if (final_char == '@') return ANSI_RESULT_INSERT_CHARS;
    if (final_char == 'P') return ANSI_RESULT_DELETE_CHARS;
    return ANSI_RESULT_ERASE_CHARS;
  }

  if (final_char == 'b') {
    int n = (param_count > 0 && params[0] > 0) ? params[0] : 1;
    state->param1 = n;
    return ANSI_RESULT_REPEAT_CHAR;
  }

  return ANSI_RESULT_SKIP;
}

ansi_result_t ansi_process_char(terminal_color_state_t *state, char c) {
  if (state->in_osc) {
    if ((unsigned char)c == 0x07) {
      state->in_osc = false;
      state->osc_esc = false;
      return ANSI_RESULT_SKIP;
    }
    if (state->osc_esc && c == '\\') {
      state->in_osc = false;
      state->osc_esc = false;
      return ANSI_RESULT_SKIP;
    }
    state->osc_esc = ((unsigned char)c == 0x1B);
    return ANSI_RESULT_SKIP;
  }

  if (!state->in_escape) {
    if ((unsigned char)c == 0x1B) {
      state->in_escape = true;
      state->in_csi = false;
      state->in_charset_select = false;
      state->esc_len = 0;
      memset(state->esc_buf, 0, ANSI_ESC_BUF_SIZE);
      return ANSI_RESULT_SKIP;
    }
    return ANSI_RESULT_PRINT;
  }

  if (!state->in_csi) {
    if (state->in_charset_select) {
      state->g0_alt_charset = (c == '0') ? true : false;
      state->in_escape = false;
      state->in_charset_select = false;
      state->esc_len = 0;
      return ANSI_RESULT_SKIP;
    }
    if (c == '[') {
      state->in_csi = true;
      return ANSI_RESULT_SKIP;
    } else if (c == ']') {
      state->in_escape = false;
      state->in_osc = true;
      state->osc_esc = false;
      return ANSI_RESULT_SKIP;
    } else if (c == '7') {
      state->in_escape = false;
      return ANSI_RESULT_SAVE_CURSOR;
    } else if (c == '8') {
      state->in_escape = false;
      return ANSI_RESULT_RESTORE_CURSOR;
    } else if (c == 'c') {
      state->in_escape = false;
      return ANSI_RESULT_CLEAR;
    } else if (c == '(' || c == ')') {
      state->in_charset_select = true;
      return ANSI_RESULT_SKIP;
    } else {
      state->in_escape = false;
      state->in_csi = false;
      state->in_charset_select = false;
      state->esc_len = 0;
      return ANSI_RESULT_SKIP;
    }
  }

  if (state->esc_len < ANSI_ESC_BUF_SIZE - 1) {
    state->esc_buf[state->esc_len++] = c;
    state->esc_buf[state->esc_len] = '\0';
  } else {
    state->in_escape = false;
    state->in_csi = false;
    state->in_charset_select = false;
    state->esc_len = 0;
    memset(state->esc_buf, 0, ANSI_ESC_BUF_SIZE);
    return ANSI_RESULT_SKIP;
  }

  if ((c >= '@' && c <= '~')) {
    ansi_result_t result = process_csi(state);
    state->in_escape = false;
    state->in_csi = false;
    state->in_charset_select = false;
    state->esc_len = 0;
    memset(state->esc_buf, 0, ANSI_ESC_BUF_SIZE);
    return result;
  }

  return ANSI_RESULT_SKIP;
}
