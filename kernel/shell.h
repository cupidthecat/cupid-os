#ifndef SHELL_H
#define SHELL_H

#include "types.h"

/* ── Shell output modes ───────────────────────────────────────────── */
typedef enum {
    SHELL_OUTPUT_TEXT,   /* Direct VGA text mode (default) */
    SHELL_OUTPUT_GUI     /* Write to buffer for GUI terminal */
} shell_output_mode_t;

/* ── Per-cell color information ───────────────────────────────────── */
typedef struct {
    uint8_t fg;   /* VGA color index 0-15 */
    uint8_t bg;   /* VGA color index 0-15 */
} shell_color_t;

/* ── GUI-mode buffer dimensions ───────────────────────────────────── */
#define SHELL_COLS 80
#define SHELL_ROWS 50

/* ── Public API ───────────────────────────────────────────────────── */

/* Original text-mode shell loop (blocking) */
void shell_run(void);

/* Set output mode (text or GUI) */
void shell_set_output_mode(shell_output_mode_t mode);

/* Get current output mode */
shell_output_mode_t shell_get_output_mode(void);

/* Get current working directory */
const char *shell_get_cwd(void);

/* GUI mode: get the character buffer */
const char *shell_get_buffer(void);

/* GUI mode: get the color buffer (parallel to char buffer) */
const shell_color_t *shell_get_color_buffer(void);

/* GUI mode: get cursor position */
int shell_get_cursor_x(void);
int shell_get_cursor_y(void);

/* GUI mode: handle a keypress (called from terminal_app) */
void shell_gui_handle_key(uint8_t scancode, char character);

/* GUI mode: set the visible column width (called from terminal_app) */
void shell_set_visible_cols(int cols);

/* GUI mode: direct output to GUI buffer (used by kernel print routing) */
void shell_gui_putchar_ext(char c);
void shell_gui_print_ext(const char *s);
void shell_gui_print_int_ext(uint32_t num);

/* Execute a command line string (used by CupidScript) */
void shell_execute_line(const char *line);

#endif