#include "ansi.h"
#include "string.h"

void ansi_init(terminal_color_state_t *state) {
  state->fg_color = ANSI_DEFAULT_FG;
  state->bg_color = ANSI_DEFAULT_BG;
  state->bold = false;
  state->esc_len = 0;
  state->in_escape = false;
  state->in_csi = false;
  memset(state->esc_buf, 0, ANSI_ESC_BUF_SIZE);
}

void ansi_reset(terminal_color_state_t *state) {
  state->fg_color = ANSI_DEFAULT_FG;
  state->bg_color = ANSI_DEFAULT_BG;
  state->bold = false;
}

uint8_t ansi_get_fg(const terminal_color_state_t *state) {
  uint8_t fg = state->fg_color;
  if (state->bold && fg < 8) {
    fg = (uint8_t)(fg + 8);
  }
  return fg;
}

uint8_t ansi_get_bg(const terminal_color_state_t *state) {
  return state->bg_color;
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

static ansi_result_t process_csi(terminal_color_state_t *state) {
  int len = state->esc_len;
  if (len < 1) return ANSI_RESULT_SKIP;

  char final_char = state->esc_buf[len - 1];

  if (final_char == 'm') {
    int params[8];
    int param_count = 0;
    int start = 0;

    for (int i = 0; i <= len - 1; i++) {
      if (state->esc_buf[i] == ';' || i == len - 1) {
        int end = (i == len - 1) ? i : i;
        if (param_count < 8) {
          params[param_count] = parse_number(state->esc_buf, start, end);
          param_count++;
        }
        start = i + 1;
      }
    }

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
      } else if (code == 22) {
        state->bold = false;
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
        state->bg_color = ansi_to_vga[code - 100];
      }
    }
    return ANSI_RESULT_SKIP;
  }

  if (final_char == 'H' || final_char == 'f') {
    return ANSI_RESULT_HOME;
  }

  if (final_char == 'J') {
    int param = parse_number(state->esc_buf, 0, len - 1);
    if (param == 2 || param == 3) {
      return ANSI_RESULT_CLEAR;
    }
    return ANSI_RESULT_SKIP;
  }

  if (final_char == 'K') {
    return ANSI_RESULT_SKIP;
  }

  return ANSI_RESULT_SKIP;
}

ansi_result_t ansi_process_char(terminal_color_state_t *state, char c) {
  if (!state->in_escape) {
    if ((unsigned char)c == 0x1B) {
      state->in_escape = true;
      state->in_csi = false;
      state->esc_len = 0;
      memset(state->esc_buf, 0, ANSI_ESC_BUF_SIZE);
      return ANSI_RESULT_SKIP;
    }
    return ANSI_RESULT_PRINT;
  }

  if (!state->in_csi) {
    if (c == '[') {
      state->in_csi = true;
      return ANSI_RESULT_SKIP;
    } else {
      state->in_escape = false;
      state->in_csi = false;
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
    state->esc_len = 0;
    memset(state->esc_buf, 0, ANSI_ESC_BUF_SIZE);
    return ANSI_RESULT_SKIP;
  }

  if ((c >= '@' && c <= '~')) {
    ansi_result_t result = process_csi(state);
    state->in_escape = false;
    state->in_csi = false;
    state->esc_len = 0;
    memset(state->esc_buf, 0, ANSI_ESC_BUF_SIZE);
    return result;
  }

  return ANSI_RESULT_SKIP;
}
