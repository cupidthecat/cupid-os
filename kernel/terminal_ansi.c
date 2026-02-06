/*
 * terminal_ansi.c - ANSI escape sequence parser for cupid-os
 *
 * Parses standard ANSI escape codes and updates terminal color state.
 * Supports:
 *   \e[0m          - Reset to defaults
 *   \e[30m-37m     - Set foreground (standard colors)
 *   \e[40m-47m     - Set background (standard colors)
 *   \e[90m-97m     - Set bright foreground colors
 *   \e[1m          - Bold/bright
 *   \e[2J          - Clear screen
 *   \e[H           - Cursor home
 *   \e[<row>;<col>H - Cursor position (parsed but not acted on here)
 */

#include "terminal_ansi.h"
#include "string.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Initialization
 * ══════════════════════════════════════════════════════════════════════ */

void ansi_init(terminal_color_state_t *state) {
    state->fg_color  = ANSI_DEFAULT_FG;
    state->bg_color  = ANSI_DEFAULT_BG;
    state->bold      = false;
    state->esc_len   = 0;
    state->in_escape = false;
    state->in_csi    = false;
    memset(state->esc_buf, 0, ANSI_ESC_BUF_SIZE);
}

void ansi_reset(terminal_color_state_t *state) {
    state->fg_color  = ANSI_DEFAULT_FG;
    state->bg_color  = ANSI_DEFAULT_BG;
    state->bold      = false;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Color accessors
 * ══════════════════════════════════════════════════════════════════════ */

uint8_t ansi_get_fg(const terminal_color_state_t *state) {
    uint8_t fg = state->fg_color;
    if (state->bold && fg < 8) {
        fg = (uint8_t)(fg + 8);  /* Bold makes standard colors bright */
    }
    return fg;
}

uint8_t ansi_get_bg(const terminal_color_state_t *state) {
    return state->bg_color;
}

/* ══════════════════════════════════════════════════════════════════════
 *  ANSI-to-VGA color mapping
 *
 *  ANSI 30-37 maps to VGA colors in a specific order:
 *    ANSI 0 (black)   → VGA 0
 *    ANSI 1 (red)     → VGA 4
 *    ANSI 2 (green)   → VGA 2
 *    ANSI 3 (yellow)  → VGA 6 (brown/yellow)
 *    ANSI 4 (blue)    → VGA 1
 *    ANSI 5 (magenta) → VGA 5
 *    ANSI 6 (cyan)    → VGA 3
 *    ANSI 7 (white)   → VGA 7
 * ══════════════════════════════════════════════════════════════════════ */
static const uint8_t ansi_to_vga[8] = {
    0,  /* ANSI black   → VGA black */
    4,  /* ANSI red     → VGA red */
    2,  /* ANSI green   → VGA green */
    6,  /* ANSI yellow  → VGA brown */
    1,  /* ANSI blue    → VGA blue */
    5,  /* ANSI magenta → VGA magenta */
    3,  /* ANSI cyan    → VGA cyan */
    7   /* ANSI white   → VGA light gray */
};

/* ══════════════════════════════════════════════════════════════════════
 *  VGA-to-palette color mapping for Mode 13h GUI terminal
 *
 *  Maps VGA text-mode color indices (0-15) to Mode 13h palette
 *  indices that produce similar colors.
 * ══════════════════════════════════════════════════════════════════════ */
static const uint8_t vga_to_mode13h[16] = {
    0,    /* 0  Black         → palette 0  (black) */
    1,    /* 1  Blue          → palette 1  (dark blue) */
    2,    /* 2  Green         → palette 2  (dark green) */
    3,    /* 3  Cyan          → palette 3  (dark cyan) */
    4,    /* 4  Red           → palette 4  (dark red) */
    5,    /* 5  Magenta       → palette 5  (dark magenta) */
    20,   /* 6  Brown         → palette 20 (brown) */
    7,    /* 7  Light Gray    → palette 7  (light gray) */
    8,    /* 8  Dark Gray     → palette 8  (dark gray) */
    9,    /* 9  Light Blue    → palette 9  */
    10,   /* 10 Light Green   → palette 10 */
    11,   /* 11 Light Cyan    → palette 11 */
    12,   /* 12 Light Red     → palette 12 */
    13,   /* 13 Light Magenta → palette 13 */
    14,   /* 14 Yellow        → palette 14 */
    15    /* 15 White         → palette 15 */
};

uint8_t ansi_vga_to_palette(uint8_t vga_color) {
    if (vga_color > 15) return 7;  /* default to light gray */
    return vga_to_mode13h[vga_color];
}

/* ══════════════════════════════════════════════════════════════════════
 *  Parse a single numeric parameter from the escape buffer
 * ══════════════════════════════════════════════════════════════════════ */

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

/* ══════════════════════════════════════════════════════════════════════
 *  Process a complete CSI sequence: ESC [ ... <final>
 *
 *  The escape buffer contains everything after ESC [ up to and
 *  including the final character.
 * ══════════════════════════════════════════════════════════════════════ */
static ansi_result_t process_csi(terminal_color_state_t *state) {
    int len = state->esc_len;
    if (len < 1) return ANSI_RESULT_SKIP;

    char final_char = state->esc_buf[len - 1];

    /* ── SGR (Select Graphic Rendition): final char 'm' ────────── */
    if (final_char == 'm') {
        /* Parse semicolon-separated parameters */
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

        /* If no parameters, treat as reset (ESC[m == ESC[0m) */
        if (param_count == 0 || (param_count == 1 && params[0] == -1)) {
            ansi_reset(state);
            return ANSI_RESULT_SKIP;
        }

        /* Process each parameter */
        for (int p = 0; p < param_count; p++) {
            int code = params[p];
            if (code < 0) continue;

            if (code == 0) {
                /* Reset */
                ansi_reset(state);
            } else if (code == 1) {
                /* Bold/bright */
                state->bold = true;
            } else if (code == 22) {
                /* Normal intensity (unbold) */
                state->bold = false;
            } else if (code >= 30 && code <= 37) {
                /* Standard foreground */
                state->fg_color = ansi_to_vga[code - 30];
            } else if (code == 39) {
                /* Default foreground */
                state->fg_color = ANSI_DEFAULT_FG;
            } else if (code >= 40 && code <= 47) {
                /* Standard background */
                state->bg_color = ansi_to_vga[code - 40];
            } else if (code == 49) {
                /* Default background */
                state->bg_color = ANSI_DEFAULT_BG;
            } else if (code >= 90 && code <= 97) {
                /* Bright foreground */
                state->fg_color = (uint8_t)(ansi_to_vga[code - 90] + 8);
            } else if (code >= 100 && code <= 107) {
                /* Bright background (use without blink) */
                state->bg_color = ansi_to_vga[code - 100];
            }
        }
        return ANSI_RESULT_SKIP;
    }

    /* ── Cursor Position / Home: final char 'H' or 'f' ─────────── */
    if (final_char == 'H' || final_char == 'f') {
        /* ESC[H  — cursor home */
        if (len == 1) {
            return ANSI_RESULT_HOME;
        }
        /* ESC[row;colH — position (we note it but don't act) */
        return ANSI_RESULT_HOME;
    }

    /* ── Erase Display: ESC[2J ─────────────────────────────────── */
    if (final_char == 'J') {
        int param = parse_number(state->esc_buf, 0, len - 1);
        if (param == 2 || param == 3) {
            return ANSI_RESULT_CLEAR;
        }
        return ANSI_RESULT_SKIP;
    }

    /* ── Erase Line: ESC[K (ignored for now) ───────────────────── */
    if (final_char == 'K') {
        return ANSI_RESULT_SKIP;
    }

    /* Unknown CSI sequence — skip */
    return ANSI_RESULT_SKIP;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Main character processing
 * ══════════════════════════════════════════════════════════════════════ */

ansi_result_t ansi_process_char(terminal_color_state_t *state, char c) {
    /* Currently inside an escape sequence? */
    if (state->in_escape) {
        /* Waiting for '[' to start a CSI sequence? */
        if (!state->in_csi) {
            if (c == '[') {
                /* CSI sequence started — begin collecting params */
                state->in_csi = true;
                state->esc_len = 0;
                return ANSI_RESULT_SKIP;
            } else {
                /* Not a CSI sequence — abort */
                state->in_escape = false;
                state->esc_len = 0;
                return ANSI_RESULT_SKIP;
            }
        }

        /* Inside CSI: buffer the character */
        if (state->esc_len < ANSI_ESC_BUF_SIZE - 1) {
            state->esc_buf[state->esc_len++] = c;
            state->esc_buf[state->esc_len] = '\0';
        }

        /* Look for final character (letter) */
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            /* Complete CSI sequence */
            state->in_escape = false;
            state->in_csi    = false;
            ansi_result_t result = process_csi(state);
            state->esc_len = 0;
            return result;
        }

        /* Still collecting CSI parameters (digits, semicolons) */
        if ((c >= '0' && c <= '9') || c == ';' || c == '?') {
            return ANSI_RESULT_SKIP;
        }

        /* Unexpected character in escape — abort */
        state->in_escape = false;
        state->in_csi    = false;
        state->esc_len = 0;
        return ANSI_RESULT_SKIP;
    }

    /* Start of escape sequence? */
    if (c == '\x1B') {
        state->in_escape = true;
        state->esc_len = 0;
        state->esc_buf[0] = '\0';
        return ANSI_RESULT_SKIP;
    }

    /* Regular character — render it with current colors */
    return ANSI_RESULT_PRINT;
}
