/*
 * ansi.h - ANSI escape sequence parser for cupid-os
 */
#ifndef ANSI_H
#define ANSI_H

#include "types.h"

#define ANSI_COLOR_BLACK 0
#define ANSI_COLOR_BLUE 1
#define ANSI_COLOR_GREEN 2
#define ANSI_COLOR_CYAN 3
#define ANSI_COLOR_RED 4
#define ANSI_COLOR_MAGENTA 5
#define ANSI_COLOR_BROWN 6
#define ANSI_COLOR_LIGHT_GRAY 7
#define ANSI_COLOR_DARK_GRAY 8
#define ANSI_COLOR_LIGHT_BLUE 9
#define ANSI_COLOR_LIGHT_GREEN 10
#define ANSI_COLOR_LIGHT_CYAN 11
#define ANSI_COLOR_LIGHT_RED 12
#define ANSI_COLOR_LIGHT_MAGENTA 13
#define ANSI_COLOR_YELLOW 14
#define ANSI_COLOR_WHITE 15

#define ANSI_DEFAULT_FG ANSI_COLOR_LIGHT_GRAY
#define ANSI_DEFAULT_BG ANSI_COLOR_BLACK

#define ANSI_ESC_BUF_SIZE 32

typedef struct {
  uint8_t fg_color;
  uint8_t bg_color;
  bool bold;
  char esc_buf[ANSI_ESC_BUF_SIZE];
  int esc_len;
  bool in_escape;
  bool in_csi;
} terminal_color_state_t;

typedef enum {
  ANSI_RESULT_PRINT,
  ANSI_RESULT_SKIP,
  ANSI_RESULT_CLEAR,
  ANSI_RESULT_HOME
} ansi_result_t;

void ansi_init(terminal_color_state_t *state);
ansi_result_t ansi_process_char(terminal_color_state_t *state, char c);
uint8_t ansi_get_fg(const terminal_color_state_t *state);
uint8_t ansi_get_bg(const terminal_color_state_t *state);
void ansi_reset(terminal_color_state_t *state);
uint32_t ansi_vga_to_palette(uint8_t vga_color);

#endif
