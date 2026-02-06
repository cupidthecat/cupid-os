/*
 * terminal_ansi.h - ANSI escape sequence parser for cupid-os
 *
 * Parses ANSI escape codes (colors, cursor control, clearing)
 * and maintains terminal color state for both VGA text and GUI rendering.
 */
#ifndef TERMINAL_ANSI_H
#define TERMINAL_ANSI_H

#include "types.h"

/* ══════════════════════════════════════════════════════════════════════
 *  VGA color constants (0-15)
 * ══════════════════════════════════════════════════════════════════════ */
#define ANSI_COLOR_BLACK        0
#define ANSI_COLOR_BLUE         1
#define ANSI_COLOR_GREEN        2
#define ANSI_COLOR_CYAN         3
#define ANSI_COLOR_RED          4
#define ANSI_COLOR_MAGENTA      5
#define ANSI_COLOR_BROWN        6
#define ANSI_COLOR_LIGHT_GRAY   7
#define ANSI_COLOR_DARK_GRAY    8
#define ANSI_COLOR_LIGHT_BLUE   9
#define ANSI_COLOR_LIGHT_GREEN  10
#define ANSI_COLOR_LIGHT_CYAN   11
#define ANSI_COLOR_LIGHT_RED    12
#define ANSI_COLOR_LIGHT_MAGENTA 13
#define ANSI_COLOR_YELLOW       14
#define ANSI_COLOR_WHITE        15

/* Default colors */
#define ANSI_DEFAULT_FG  ANSI_COLOR_LIGHT_GRAY
#define ANSI_DEFAULT_BG  ANSI_COLOR_BLACK

/* ══════════════════════════════════════════════════════════════════════
 *  Terminal color state
 * ══════════════════════════════════════════════════════════════════════ */

#define ANSI_ESC_BUF_SIZE 32

typedef struct {
    uint8_t fg_color;               /* Current foreground (0-15) */
    uint8_t bg_color;               /* Current background (0-7) */
    bool    bold;                   /* Bold mode active (adds 8 to fg) */
    char    esc_buf[ANSI_ESC_BUF_SIZE]; /* Partial escape sequence buffer */
    int     esc_len;                /* Length of partial sequence */
    bool    in_escape;              /* Currently parsing an escape seq */
    bool    in_csi;                 /* Past '[', collecting CSI params */
} terminal_color_state_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Parser result — what to do with each character
 * ══════════════════════════════════════════════════════════════════════ */
typedef enum {
    ANSI_RESULT_PRINT,    /* Render this character normally */
    ANSI_RESULT_SKIP,     /* Part of escape sequence — don't render */
    ANSI_RESULT_CLEAR,    /* Clear screen command was received */
    ANSI_RESULT_HOME      /* Move cursor to home position */
} ansi_result_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════════ */

/* Initialize color state to defaults */
void ansi_init(terminal_color_state_t *state);

/* Process a single character through the ANSI parser.
 * Returns ANSI_RESULT_PRINT if the character should be rendered,
 * ANSI_RESULT_SKIP if it's part of an escape sequence, etc.
 * After returning PRINT, state->fg_color and state->bg_color
 * reflect the current active colors. */
ansi_result_t ansi_process_char(terminal_color_state_t *state, char c);

/* Get the effective foreground color (accounting for bold) */
uint8_t ansi_get_fg(const terminal_color_state_t *state);

/* Get the effective background color */
uint8_t ansi_get_bg(const terminal_color_state_t *state);

/* Reset color state to defaults */
void ansi_reset(terminal_color_state_t *state);

/* Map a VGA color index (0-15) to a Mode 13h palette color index.
 * Used by the GUI terminal to convert VGA text-mode color codes
 * to the 256-color palette used in Mode 13h. */
uint8_t ansi_vga_to_palette(uint8_t vga_color);

#endif /* TERMINAL_ANSI_H */
