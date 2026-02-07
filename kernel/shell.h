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

/* Set current working directory (used by CupidC cd command) */
void shell_set_cwd(const char *path);

/* Resolve a relative path against CWD into an absolute path */
void shell_resolve_path(const char *input, char *out);

/* History access for CupidC programs */
int shell_get_history_count(void);
const char *shell_get_history_entry(int index);

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

/* ── JIT program input routing (for GUI mode) ─────────────────────── */

/**
 * Check if a JIT program is currently running and consuming input.
 * Returns 1 if a program is running, 0 if shell is active.
 */
int shell_jit_program_is_running(void);

/**
 * Deliver a character to the running JIT program's input buffer.
 * Called by terminal when a program is running.
 */
void shell_jit_program_input(char c);

/**
 * Read a character from the JIT program input buffer (blocking).
 * Called by getchar() when running in GUI mode.
 * Returns the character, or 0 if interrupted.
 */
char shell_jit_program_getchar(void);

/**
 * Mark that a JIT program is about to start execution.
 * Switches keyboard input routing to the program.
 */
void shell_jit_program_start(void);

/**
 * Mark that a JIT program has finished execution.
 * Switches keyboard input routing back to the shell.
 */
void shell_jit_program_end(void);

/* GUI mode: direct output to GUI buffer (used by kernel print routing) */
void shell_gui_putchar_ext(char c);
void shell_gui_print_ext(const char *s);
void shell_gui_print_int_ext(uint32_t num);

/* Execute a command line string (used by CupidScript) */
void shell_execute_line(const char *line);

/* ── Program argument passing (TempleOS-style) ────────────────────── */

/**
 * Set the arguments for the next CupidC program invocation.
 * Called by the shell before JIT-compiling or exec'ing a /bin program.
 * The CupidC program retrieves these via get_args().
 */
void shell_set_program_args(const char *args);

/**
 * Get the current program arguments string.
 * CupidC programs call this as get_args() to receive their CLI args.
 */
const char *shell_get_program_args(void);

#endif