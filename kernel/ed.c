/**
 *  outdated - ed was rewritten in cupid c 
 * ed.c - Ed line editor for cupid-os
 *
 * A faithful implementation of the classic Unix ed(1) line editor.
 * Supports the standard POSIX ed command set including:
 *   a, i, c, d, p, n, l, w, q, Q, r, =, s, m, t, j, u, e, f, k, ', H, h
 *   Address forms: n, ., $, +, -, /RE/, ?RE?, 'x, addr,addr
 *
 * Limitations (bare-metal environment):
 *   - Maximum ED_MAX_LINES lines, each up to ED_MAX_LINE_LEN characters
 *   - Reads from in-memory fs and FAT16 disk; writes to FAT16 disk
 *   - Regex limited to basic literal and ^ $ . * support
 */

#include "ed.h"
#include "kernel.h"
#include "string.h"
#include "memory.h"
#include "types.h"
#include "math.h"
#include "fs.h"
#include "fat16.h"
#include "../drivers/keyboard.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Constants
 * ══════════════════════════════════════════════════════════════════════ */
#define ED_MAX_LINES     1024
#define ED_MAX_LINE_LEN  256
#define ED_CMD_BUF_LEN   512
#define ED_FILENAME_LEN  64

/* ══════════════════════════════════════════════════════════════════════
 *  Editor state
 * ══════════════════════════════════════════════════════════════════════ */
static char *ed_lines[ED_MAX_LINES];     /* Array of line pointers      */
static int   ed_nlines;                  /* Total number of lines       */
static int   ed_cur;                     /* Current line (1-based, 0=no lines) */
static int   ed_dirty;                   /* Buffer modified flag        */
static char  ed_filename[ED_FILENAME_LEN]; /* Current filename          */
static int   ed_quit;                    /* Set when we should exit     */
static int   ed_show_errors;             /* H toggle: verbose errors    */
static char  ed_last_error[128];         /* Last error message          */

/* Undo state: single-level undo for the last change */
#define ED_UNDO_MAX_LINES 1024
static char *ed_undo_lines[ED_UNDO_MAX_LINES];
static int   ed_undo_nlines;
static int   ed_undo_cur;
static int   ed_undo_valid;

/* Marks: 'a through 'z */
static int ed_marks[26];

/* Last search/substitution patterns */
static char ed_last_search[ED_CMD_BUF_LEN];
static char ed_last_replace[ED_CMD_BUF_LEN];

/* Output function pointers (can be overridden) */
static void (*ed_print)(const char*) = print;
static void (*ed_putchar)(char) = putchar;
static void (*ed_print_int_fn)(uint32_t) = print_int;

void ed_set_output(void (*print_fn)(const char*), void (*putchar_fn)(char), void (*print_int_fn)(uint32_t)) {
    if (print_fn) ed_print = print_fn;
    if (putchar_fn) ed_putchar = putchar_fn;
    if (print_int_fn) ed_print_int_fn = print_int_fn;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Utility helpers
 * ══════════════════════════════════════════════════════════════════════ */

/* Our own strlen since the kernel one is available */
static size_t ed_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Our own strcpy */
static void ed_strcpy(char *dst, const char *src) {
    while ((*dst++ = *src++)) {}
}

/* Duplicate a string using kmalloc */
static char *ed_strdup(const char *s) {
    size_t len = ed_strlen(s);
    char *p = kmalloc(len + 1);
    if (p) {
        memcpy(p, s, len + 1);
    }
    return p;
}

/* Print an integer (simple) */
static void ed_print_int(int n) {
    if (n < 0) {
        ed_putchar('-');
        n = -n;
    }
    ed_print_int_fn((uint32_t)n);
}

/* Read a full line of input from the keyboard, return length.
 * Handles backspace. Returns the line WITHOUT trailing newline. */
static int ed_readline(char *buf, int maxlen) {
    int pos = 0;
    while (1) {
        char c = (char)getchar();
        if (c == '\n') {
            buf[pos] = '\0';
            ed_putchar('\n');
            return pos;
        } else if (c == '\b') {
            if (pos > 0) {
                pos--;
                ed_print("\b \b");
            }
        } else if (c >= 0x20 && pos < maxlen - 1) {
            buf[pos++] = c;
            ed_putchar(c);
        }
    }
}

/* Set an error message */
static void ed_error(const char *msg) {
    ed_strcpy(ed_last_error, msg);
    ed_print("?\n");
    if (ed_show_errors) {
        ed_print(ed_last_error);
        ed_print("\n");
    }
}

/* Check if character is a digit */
static int ed_isdigit(char c) {
    return c >= '0' && c <= '9';
}

/* Parse an unsigned integer from a string, advancing pointer */
static int ed_parse_int(const char **pp) {
    int val = 0;
    while (ed_isdigit(**pp)) {
        val = val * 10 + (**pp - '0');
        (*pp)++;
    }
    return val;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Undo support (single-level)
 * ══════════════════════════════════════════════════════════════════════ */

static void ed_save_undo(void) {
    /* Free old undo state */
    for (int i = 0; i < ed_undo_nlines; i++) {
        if (ed_undo_lines[i]) kfree(ed_undo_lines[i]);
    }
    /* Copy current buffer */
    ed_undo_nlines = ed_nlines;
    ed_undo_cur = ed_cur;
    for (int i = 0; i < ed_nlines; i++) {
        ed_undo_lines[i] = ed_strdup(ed_lines[i]);
    }
    ed_undo_valid = 1;
}

static void ed_do_undo(void) {
    if (!ed_undo_valid) {
        ed_error("nothing to undo");
        return;
    }
    /* Swap current and undo states */
    int tmp_nlines = ed_nlines;
    int tmp_cur = ed_cur;

    /* Save current into temp */
    char *tmp_lines[ED_MAX_LINES];
    for (int i = 0; i < ed_nlines; i++) {
        tmp_lines[i] = ed_lines[i];
    }

    /* Restore undo into current */
    ed_nlines = ed_undo_nlines;
    ed_cur = ed_undo_cur;
    for (int i = 0; i < ed_undo_nlines; i++) {
        ed_lines[i] = ed_undo_lines[i];
    }

    /* Old current becomes new undo */
    ed_undo_nlines = tmp_nlines;
    ed_undo_cur = tmp_cur;
    for (int i = 0; i < tmp_nlines; i++) {
        ed_undo_lines[i] = tmp_lines[i];
    }

    ed_dirty = 1;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Buffer manipulation
 * ══════════════════════════════════════════════════════════════════════ */

/* Insert a line AFTER position 'after' (0 = insert at beginning).
 * Returns 1 on success, 0 on failure. */
static int ed_insert_line(int after, const char *text) {
    if (ed_nlines >= ED_MAX_LINES) {
        ed_error("buffer full");
        return 0;
    }
    char *dup = ed_strdup(text);
    if (!dup) {
        ed_error("out of memory");
        return 0;
    }
    /* Shift lines down */
    for (int i = ed_nlines; i > after; i--) {
        ed_lines[i] = ed_lines[i - 1];
    }
    ed_lines[after] = dup;
    ed_nlines++;
    /* Update marks */
    for (int m = 0; m < 26; m++) {
        if (ed_marks[m] > after) ed_marks[m]++;
    }
    return 1;
}

/* Delete line at position 'pos' (1-based). */
static void ed_delete_line(int pos) {
    if (pos < 1 || pos > ed_nlines) return;
    int idx = pos - 1;
    if (ed_lines[idx]) kfree(ed_lines[idx]);
    for (int i = idx; i < ed_nlines - 1; i++) {
        ed_lines[i] = ed_lines[i + 1];
    }
    ed_nlines--;
    /* Update marks */
    for (int m = 0; m < 26; m++) {
        if (ed_marks[m] == pos) ed_marks[m] = 0;
        else if (ed_marks[m] > pos) ed_marks[m]--;
    }
}

/* Replace line at position 'pos' (1-based) with new text */
static int ed_replace_line(int pos, const char *text) {
    if (pos < 1 || pos > ed_nlines) return 0;
    int idx = pos - 1;
    char *dup = ed_strdup(text);
    if (!dup) {
        ed_error("out of memory");
        return 0;
    }
    if (ed_lines[idx]) kfree(ed_lines[idx]);
    ed_lines[idx] = dup;
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Basic regex matching (supports . * ^ $ and literal characters)
 * ══════════════════════════════════════════════════════════════════════ */

/* Match pattern at a specific position in text.
 * Returns 1 if matched, 0 otherwise.
 * *match_start and *match_end set to the matched range in text. */
static int ed_regex_match_here(const char *pat, const char *text);
static int ed_regex_match_star(char c, const char *pat, const char *text);

static int ed_regex_match_here(const char *pat, const char *text) {
    if (pat[0] == '\0') return 1;
    if (pat[1] == '*') return ed_regex_match_star(pat[0], pat + 2, text);
    if (pat[0] == '$' && pat[1] == '\0') return (*text == '\0') ? 1 : 0;
    if (*text != '\0' && (pat[0] == '.' || pat[0] == *text))
        return ed_regex_match_here(pat + 1, text + 1);
    return 0;
}

static int ed_regex_match_star(char c, const char *pat, const char *text) {
    /* Try matching zero or more occurrences of c */
    do {
        if (ed_regex_match_here(pat, text)) return 1;
    } while (*text != '\0' && (*text++ == c || c == '.'));
    return 0;
}

/* Search for pattern in text. Returns pointer to start of match, or NULL. */
static const char *ed_regex_search(const char *pat, const char *text) {
    if (pat[0] == '^')
        return ed_regex_match_here(pat + 1, text) ? text : NULL;
    /* Try matching at every position */
    const char *p = text;
    do {
        if (ed_regex_match_here(pat, p)) return p;
    } while (*p++ != '\0');
    return NULL;
}

/* Find end of match starting at pos with pattern */
static const char *ed_regex_match_end(const char *pat, const char *text) {
    /* After matching, find where the match ends */
    if (pat[0] == '^') pat++;
    const char *t = text;
    while (*pat) {
        if (pat[1] == '*') {
            char c = pat[0];
            /* Greedy: advance as far as possible */
            const char *start = t;
            while (*t && (c == '.' || *t == c)) t++;
            /* Now try to match rest from rightmost */
            while (t >= start) {
                if (ed_regex_match_here(pat + 2, t)) return t;
                t--;
            }
            return NULL;
        }
        if (pat[0] == '$' && pat[1] == '\0') return t;
        if (*t == '\0') return NULL;
        if (pat[0] != '.' && pat[0] != *t) return NULL;
        pat++;
        t++;
    }
    return t;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Address parsing
 * ══════════════════════════════════════════════════════════════════════ */

/* Parse a single address from the command string.
 * Returns the resolved line number (1-based), or -1 on error.
 * Advances *pp past the consumed characters.
 * 'set' is set to 1 if an address was actually parsed. */
static int ed_parse_address(const char **pp, int *set) {
    const char *p = *pp;
    int addr = -1;
    *set = 0;

    while (*p == ' ') p++;

    if (ed_isdigit(*p)) {
        addr = ed_parse_int(&p);
        *set = 1;
    } else if (*p == '.') {
        addr = ed_cur;
        p++;
        *set = 1;
    } else if (*p == '$') {
        addr = ed_nlines;
        p++;
        *set = 1;
    } else if (*p == '\'') {
        p++;
        if (*p >= 'a' && *p <= 'z') {
            int m = *p - 'a';
            if (ed_marks[m] == 0) {
                ed_error("undefined mark");
                return -1;
            }
            addr = ed_marks[m];
            p++;
            *set = 1;
        } else {
            ed_error("invalid mark");
            return -1;
        }
    } else if (*p == '/' || *p == '?') {
        char delim = *p;
        p++;
        char pattern[ED_CMD_BUF_LEN];
        int pi = 0;
        while (*p && *p != delim && pi < ED_CMD_BUF_LEN - 1) {
            pattern[pi++] = *p++;
        }
        pattern[pi] = '\0';
        if (*p == delim) p++;

        /* Use last pattern if empty */
        if (pi == 0) {
            if (ed_last_search[0] == '\0') {
                ed_error("no previous pattern");
                return -1;
            }
            ed_strcpy(pattern, ed_last_search);
        } else {
            ed_strcpy(ed_last_search, pattern);
        }

        /* Search forward or backward */
        int found = 0;
        if (delim == '/') {
            /* Forward search from cur+1, wrapping */
            for (int i = 0; i < ed_nlines; i++) {
                int line = ((ed_cur + i) % ed_nlines) + 1;
                if (ed_regex_search(pattern, ed_lines[line - 1])) {
                    addr = line;
                    found = 1;
                    break;
                }
            }
        } else {
            /* Backward search from cur-1, wrapping */
            for (int i = 0; i < ed_nlines; i++) {
                int line = ed_cur - 1 - i;
                while (line < 1) line += ed_nlines;
                if (ed_regex_search(pattern, ed_lines[line - 1])) {
                    addr = line;
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            ed_error("pattern not found");
            return -1;
        }
        *set = 1;
    }

    if (*set == 0) {
        *pp = p;
        return -1;
    }

    /* Handle +/- offsets */
    while (*p == '+' || *p == '-') {
        char op = *p;
        p++;
        int offset = 1;
        if (ed_isdigit(*p)) {
            offset = ed_parse_int(&p);
        }
        if (op == '+') addr += offset;
        else addr -= offset;
    }

    *pp = p;
    return addr;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Input mode (used by a, i, c commands)
 * ══════════════════════════════════════════════════════════════════════ */

/* Read lines from input until a line containing only "." is entered.
 * Insert them after 'after' (1-based index; 0 = before first line).
 * Returns the line number of the last inserted line. */
static int ed_input_mode(int after) {
    char buf[ED_MAX_LINE_LEN];
    int count = 0;
    while (1) {
        ed_readline(buf, ED_MAX_LINE_LEN);
        if (buf[0] == '.' && buf[1] == '\0') break;
        if (!ed_insert_line(after + count, buf)) break;
        count++;
        ed_cur = after + count;
    }
    if (count > 0) ed_dirty = 1;
    return after + count;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Substitution
 * ══════════════════════════════════════════════════════════════════════ */

/* Perform s/pattern/replacement/flags on a single line.
 * Returns 1 if a substitution was made, 0 otherwise. */
static int ed_substitute_line(int linenum, const char *pattern,
                              const char *replacement, int global_flag,
                              int count_target) {
    char *line = ed_lines[linenum - 1];
    char result[ED_MAX_LINE_LEN];
    int rp = 0;
    int subs = 0;
    int match_num = 0;
    const char *p = line;

    while (*p && rp < ED_MAX_LINE_LEN - 1) {
        const char *match = ed_regex_search(pattern, p);
        if (!match || match != p) {
            /* Try to find a match further in the string */
            if (match && match > p) {
                /* Copy characters before match */
                while (p < match && rp < ED_MAX_LINE_LEN - 1) {
                    result[rp++] = *p++;
                }
                continue;
            }
            /* No match at all */
            if (!match) {
                /* Copy rest of line */
                while (*p && rp < ED_MAX_LINE_LEN - 1) {
                    result[rp++] = *p++;
                }
                break;
            }
        }

        match_num++;

        /* Determine if this match should be replaced */
        int do_replace = 0;
        if (global_flag) {
            do_replace = 1;
        } else if (count_target > 0) {
            if (match_num == count_target) do_replace = 1;
        } else {
            if (match_num == 1) do_replace = 1;
        }

        if (do_replace) {
            /* Find end of match */
            const char *mend = ed_regex_match_end(pattern, p);
            if (!mend || mend == p) {
                /* Zero-length match - copy one char and advance */
                if (*p) result[rp++] = *p++;
                continue;
            }

            /* Copy replacement, handling & (matched text) */
            const char *r = replacement;
            while (*r && rp < ED_MAX_LINE_LEN - 1) {
                if (*r == '&') {
                    /* Copy matched text */
                    const char *m = p;
                    while (m < mend && rp < ED_MAX_LINE_LEN - 1) {
                        result[rp++] = *m++;
                    }
                } else if (*r == '\\' && r[1]) {
                    r++;
                    if (*r == 'n') result[rp++] = '\n';
                    else if (*r == 't') result[rp++] = '\t';
                    else result[rp++] = *r;
                } else {
                    result[rp++] = *r;
                }
                r++;
            }
            p = mend;
            subs++;

            if (!global_flag && count_target <= 0) {
                /* Only first match for non-global */
                while (*p && rp < ED_MAX_LINE_LEN - 1) {
                    result[rp++] = *p++;
                }
                break;
            }
        } else {
            /* Don't replace this match, copy the matched text literally */
            const char *mend = ed_regex_match_end(pattern, p);
            if (!mend || mend == p) {
                if (*p) result[rp++] = *p++;
                continue;
            }
            while (p < mend && rp < ED_MAX_LINE_LEN - 1) {
                result[rp++] = *p++;
            }
        }
    }
    result[rp] = '\0';

    if (subs > 0) {
        ed_replace_line(linenum, result);
    }
    return subs;
}

/* Parse and execute s command: s/pat/repl/flags */
static void ed_cmd_substitute(int addr1, int addr2, const char *cmd) {
    if (*cmd != 's') return;
    cmd++;

    char delim = *cmd;
    if (!delim || delim == ' ' || delim == '\n') {
        /* Repeat last substitution */
        if (ed_last_search[0] == '\0') {
            ed_error("no previous substitution");
            return;
        }
        int total = 0;
        ed_save_undo();
        for (int i = addr1; i <= addr2; i++) {
            total += ed_substitute_line(i, ed_last_search, ed_last_replace, 0, 0);
        }
        if (total == 0) {
            ed_error("no match");
        } else {
            ed_dirty = 1;
        }
        return;
    }
    cmd++;

    /* Parse pattern */
    char pattern[ED_CMD_BUF_LEN];
    int pi = 0;
    while (*cmd && *cmd != delim && pi < ED_CMD_BUF_LEN - 1) {
        if (*cmd == '\\' && cmd[1]) {
            pattern[pi++] = *cmd++;
            pattern[pi++] = *cmd++;
        } else {
            pattern[pi++] = *cmd++;
        }
    }
    pattern[pi] = '\0';
    if (*cmd == delim) cmd++;

    /* Parse replacement */
    char replacement[ED_CMD_BUF_LEN];
    int ri = 0;
    while (*cmd && *cmd != delim && ri < ED_CMD_BUF_LEN - 1) {
        if (*cmd == '\\' && cmd[1]) {
            replacement[ri++] = *cmd++;
            replacement[ri++] = *cmd++;
        } else {
            replacement[ri++] = *cmd++;
        }
    }
    replacement[ri] = '\0';
    if (*cmd == delim) cmd++;

    /* Parse flags */
    int global_flag = 0;
    int print_flag = 0;
    int number_flag = 0;
    int count_target = 0;
    while (*cmd) {
        if (*cmd == 'g') global_flag = 1;
        else if (*cmd == 'p') print_flag = 1;
        else if (*cmd == 'n') number_flag = 1;
        else if (ed_isdigit(*cmd)) count_target = count_target * 10 + (*cmd - '0');
        cmd++;
    }

    /* Use last pattern if empty */
    if (pi == 0) {
        if (ed_last_search[0] == '\0') {
            ed_error("no previous pattern");
            return;
        }
        ed_strcpy(pattern, ed_last_search);
    } else {
        ed_strcpy(ed_last_search, pattern);
    }
    ed_strcpy(ed_last_replace, replacement);

    ed_save_undo();
    int total_subs = 0;
    for (int i = addr1; i <= addr2; i++) {
        int n = ed_substitute_line(i, pattern, replacement, global_flag, count_target);
        if (n > 0) {
            total_subs += n;
            ed_cur = i;
        }
    }

    if (total_subs == 0) {
        ed_error("no match");
        return;
    }
    ed_dirty = 1;

    if (print_flag || number_flag) {
        if (number_flag) {
            ed_print_int(ed_cur);
            ed_putchar('\t');
        }
        ed_print(ed_lines[ed_cur - 1]);
        ed_print("\n");
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Line display helpers
 * ══════════════════════════════════════════════════════════════════════ */

/* Print a line with special character escaping (for 'l' command) */
static void ed_print_escaped(const char *s) {
    while (*s) {
        if (*s == '\\') {
            ed_print("\\\\");
        } else if (*s == '\t') {
            ed_print("\\t");
        } else if (*s == '\b') {
            ed_print("\\b");
        } else if ((unsigned char)*s < 0x20 || (unsigned char)*s == 0x7F) {
            ed_print("\\x");
            print_hex_byte((uint8_t)*s);
        } else {
            ed_putchar(*s);
        }
        s++;
    }
    ed_print("$\n");
}

/* ══════════════════════════════════════════════════════════════════════
 *  Load / save buffer
 * ══════════════════════════════════════════════════════════════════════ */

/* Load text data into the buffer starting after 'after' (0 for empty buf).
 * Splits on newlines. Returns byte count. */
static int ed_load_text(const char *data, uint32_t size, int after) {
    int bytes = 0;
    char line[ED_MAX_LINE_LEN];
    int lp = 0;
    int count = 0;

    for (uint32_t i = 0; i < size; i++) {
        if (data[i] == '\n' || data[i] == '\r') {
            line[lp] = '\0';
            if (!ed_insert_line(after + count, line)) break;
            count++;
            bytes += lp + 1;
            lp = 0;
            /* Skip \r\n pairs */
            if (data[i] == '\r' && i + 1 < size && data[i + 1] == '\n') {
                i++;
                bytes++;
            }
        } else if (lp < ED_MAX_LINE_LEN - 1) {
            line[lp++] = data[i];
        }
    }
    /* Handle last line without newline */
    if (lp > 0) {
        line[lp] = '\0';
        if (ed_insert_line(after + count, line)) {
            count++;
            bytes += lp;
        }
    }

    if (count > 0) {
        ed_cur = after + count;
    }
    return bytes;
}

/**
 * ed_write_to_disk - Serialize the editor buffer and write to FAT16 disk
 *
 * @param filename: Target filename (8.3 format)
 * @param from: First line to write (1-based)
 * @param to: Last line to write (1-based)
 * @return Bytes written on success, -1 on error
 */
static int ed_write_to_disk(const char *filename, int from, int to) {
    /* Calculate total size needed */
    int total = 0;
    for (int i = from; i <= to; i++) {
        total += (int)ed_strlen(ed_lines[i - 1]) + 1; /* +1 for '\n' */
    }

    if (total == 0) {
        /* Write an empty file */
        int ret = fat16_write_file(filename, "", 0);
        return (ret < 0) ? -1 : 0;
    }

    /* Allocate a flat buffer */
    char *buf = kmalloc((size_t)total);
    if (!buf) {
        ed_error("out of memory");
        return -1;
    }

    /* Serialize lines into buffer with newlines */
    int pos = 0;
    for (int i = from; i <= to; i++) {
        const char *line = ed_lines[i - 1];
        size_t len = ed_strlen(line);
        memcpy(buf + pos, line, len);
        pos += (int)len;
        buf[pos++] = '\n';
    }

    /* Write to FAT16 */
    int ret = fat16_write_file(filename, buf, (uint32_t)total);
    kfree(buf);

    if (ret < 0) {
        ed_error("write failed");
        return -1;
    }
    return total;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Command execution
 * ══════════════════════════════════════════════════════════════════════ */

static void ed_exec_command(const char *cmdline) {
    const char *p = cmdline;
    int addr1, addr2;
    int has_range = 0;

    while (*p == ' ') p++;

    /* Empty line */
    if (*p == '\0') {
        /* Print next line (like +1p) */
        if (ed_nlines == 0) {
            ed_error("invalid address");
            return;
        }
        if (ed_cur < ed_nlines) {
            ed_cur++;
            ed_print(ed_lines[ed_cur - 1]);
            ed_print("\n");
        } else {
            ed_error("invalid address");
        }
        return;
    }

    /* Determine default addresses based on upcoming command */
    /* First, peek ahead to find the command character */
    const char *peek = p;
    int dummy_set = 0;
    int dummy_addr;
    const char *saved_p = p;

    /* Try to skip past any address specification to find the command */
    dummy_addr = ed_parse_address(&peek, &dummy_set);
    (void)dummy_addr;
    while (*peek == ' ') peek++;
    if (*peek == ',' || *peek == ';') {
        peek++;
        int dummy_set2 = 0;
        ed_parse_address(&peek, &dummy_set2);
        (void)dummy_set2;
    }
    while (*peek == ' ') peek++;
    char upcoming_cmd = *peek;

    /* Reset p */
    p = saved_p;

    /* Set default addresses based on command */
    int def1 = ed_cur;
    int def2 = ed_cur;

    switch (upcoming_cmd) {
        case 'w':
        case 'W':
            def1 = 1; def2 = ed_nlines;
            break;
        case 'g':
        case 'G':
        case 'v':
        case 'V':
            def1 = 1; def2 = ed_nlines;
            break;
        default:
            break;
    }

    /* Parse address range */
    addr1 = def1;
    addr2 = def2;

    if (*p == '%') {
        addr1 = 1;
        addr2 = ed_nlines;
        p++;
        has_range = 1;
    } else if (*p == ',' && *(p + 1) != '\0') {
        addr1 = 1;
        addr2 = ed_nlines;
        p++;
        has_range = 1;
    } else {
        int set1 = 0;
        int a1 = ed_parse_address(&p, &set1);
        if (set1) {
            if (a1 == -1) return; /* error already reported */
            addr1 = a1;
            addr2 = a1;
            has_range = 1;
        }

        while (*p == ' ') p++;

        if (*p == ',' || *p == ';') {
            char sep = *p;
            p++;
            if (sep == ';') {
                ed_cur = addr1;
            }
            int set2 = 0;
            int a2 = ed_parse_address(&p, &set2);
            if (set2) {
                if (a2 == -1) return;
                addr2 = a2;
            } else {
                addr2 = ed_nlines;
            }
        }
    }

    while (*p == ' ') p++;

    /* Validate addresses for commands that need lines */
    char cmd = *p;

    /* Handle the command */
    switch (cmd) {

    /* ── (a)ppend ── */
    case 'a': {
        if (!has_range) addr1 = ed_cur;
        if (ed_nlines == 0) addr1 = 0;
        else if (addr1 < 0 || addr1 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        p++;
        ed_save_undo();
        ed_input_mode(addr1);
        break;
    }

    /* ── (i)nsert ── */
    case 'i': {
        if (!has_range) addr1 = ed_cur;
        if (ed_nlines == 0) addr1 = 0;
        else if (addr1 < 1) {
            addr1 = 1;
        }
        if (addr1 > ed_nlines && ed_nlines > 0) {
            ed_error("invalid address");
            return;
        }
        p++;
        ed_save_undo();
        ed_input_mode(addr1 > 0 ? addr1 - 1 : 0);
        break;
    }

    /* ── (c)hange ── */
    case 'c': {
        if (ed_nlines == 0) {
            /* Change on empty buffer = insert */
            p++;
            ed_save_undo();
            ed_input_mode(0);
            break;
        }
        if (addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        p++;
        ed_save_undo();
        int insert_at = addr1 - 1;
        for (int i = addr2; i >= addr1; i--) {
            ed_delete_line(i);
        }
        ed_input_mode(insert_at);
        break;
    }

    /* ── (d)elete ── */
    case 'd': {
        if (ed_nlines == 0 || addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        p++;
        ed_save_undo();
        for (int i = addr2; i >= addr1; i--) {
            ed_delete_line(i);
        }
        if (ed_nlines == 0) ed_cur = 0;
        else if (addr1 <= ed_nlines) ed_cur = addr1;
        else ed_cur = ed_nlines;
        ed_dirty = 1;
        break;
    }

    /* ── (p)rint ── */
    case 'p': {
        if (ed_nlines == 0) {
            ed_error("invalid address");
            return;
        }
        if (addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        p++;
        for (int i = addr1; i <= addr2; i++) {
            ed_print(ed_lines[i - 1]);
            ed_print("\n");
        }
        ed_cur = addr2;
        break;
    }

    /* ── (n)umber ── */
    case 'n': {
        if (ed_nlines == 0) {
            ed_error("invalid address");
            return;
        }
        if (addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        p++;
        for (int i = addr1; i <= addr2; i++) {
            ed_print_int(i);
            ed_putchar('\t');
            ed_print(ed_lines[i - 1]);
            ed_print("\n");
        }
        ed_cur = addr2;
        break;
    }

    /* ── (l)ist ── */
    case 'l': {
        if (ed_nlines == 0) {
            ed_error("invalid address");
            return;
        }
        if (addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        p++;
        for (int i = addr1; i <= addr2; i++) {
            ed_print_escaped(ed_lines[i - 1]);
        }
        ed_cur = addr2;
        break;
    }

    /* ── (=) print line number ── */
    case '=': {
        if (!has_range) addr2 = ed_nlines;
        ed_print_int(addr2);
        ed_print("\n");
        break;
    }

    /* ── (q)uit / (Q)uit ── */
    case 'q': {
        p++;
        if (ed_dirty) {
            ed_error("warning: buffer modified");
            ed_dirty = 0; /* Allow second q to quit */
            return;
        }
        ed_quit = 1;
        break;
    }
    case 'Q': {
        ed_quit = 1;
        break;
    }

    /* ── (w)rite ── */
    case 'w': {
        p++;
        int do_quit = 0;
        if (*p == 'q') {
            /* wq: write and quit */
            do_quit = 1;
            p++;
        }
        while (*p == ' ') p++;

        /* Filename */
        if (*p) {
            int fi = 0;
            char fname[ED_FILENAME_LEN];
            while (*p && *p != ' ' && fi < ED_FILENAME_LEN - 1) {
                fname[fi++] = *p++;
            }
            fname[fi] = '\0';
            ed_strcpy(ed_filename, fname);
        } else if (ed_filename[0] == '\0') {
            ed_error("no filename");
            return;
        }

        /* Write to disk */
        int bytes = ed_write_to_disk(ed_filename, addr1, addr2);
        if (bytes < 0) return;

        ed_print_int(bytes);
        ed_print("\n");
        ed_dirty = 0;

        if (do_quit) ed_quit = 1;
        break;
    }

    /* ── (r)ead ── */
    case 'r': {
        p++;
        while (*p == ' ') p++;
        if (!*p && ed_filename[0] == '\0') {
            ed_error("no filename");
            return;
        }
        const char *rfile = *p ? p : ed_filename;

        /* Try in-memory filesystem */
        const fs_file_t *f = fs_find(rfile);
        if (!f) {
            ed_error("cannot open file");
            return;
        }
        if (!has_range) addr1 = ed_nlines;
        ed_save_undo();
        int bytes = ed_load_text(f->data, f->size, addr1);
        ed_print_int(bytes);
        ed_print("\n");
        ed_dirty = 1;
        break;
    }

    /* ── (e)dit ── */
    case 'e': {
        p++;
        if (*p == '!') {
            ed_error("shell escape not supported");
            return;
        }
        while (*p == ' ') p++;
        if (ed_dirty) {
            ed_error("warning: buffer modified");
            ed_dirty = 0;
            return;
        }
        /* Clear buffer */
        for (int i = 0; i < ed_nlines; i++) {
            if (ed_lines[i]) kfree(ed_lines[i]);
        }
        ed_nlines = 0;
        ed_cur = 0;

        if (*p) {
            int fi = 0;
            char fname[ED_FILENAME_LEN];
            while (*p && *p != ' ' && fi < ED_FILENAME_LEN - 1) {
                fname[fi++] = *p++;
            }
            fname[fi] = '\0';
            ed_strcpy(ed_filename, fname);
        }

        if (ed_filename[0]) {
            const fs_file_t *f = fs_find(ed_filename);
            if (f) {
                int bytes = ed_load_text(f->data, f->size, 0);
                ed_print_int(bytes);
                ed_print("\n");
            } else {
                ed_error("cannot open file");
            }
        }
        ed_dirty = 0;
        break;
    }

    /* ── (E)dit unconditional ── */
    case 'E': {
        p++;
        while (*p == ' ') p++;
        /* Clear buffer */
        for (int i = 0; i < ed_nlines; i++) {
            if (ed_lines[i]) kfree(ed_lines[i]);
        }
        ed_nlines = 0;
        ed_cur = 0;

        if (*p) {
            int fi = 0;
            char fname[ED_FILENAME_LEN];
            while (*p && *p != ' ' && fi < ED_FILENAME_LEN - 1) {
                fname[fi++] = *p++;
            }
            fname[fi] = '\0';
            ed_strcpy(ed_filename, fname);
        }

        if (ed_filename[0]) {
            const fs_file_t *f = fs_find(ed_filename);
            if (f) {
                int bytes = ed_load_text(f->data, f->size, 0);
                ed_print_int(bytes);
                ed_print("\n");
            } else {
                ed_error("cannot open file");
            }
        }
        ed_dirty = 0;
        break;
    }

    /* ── (f)ilename ── */
    case 'f': {
        p++;
        while (*p == ' ') p++;
        if (*p) {
            int fi = 0;
            while (*p && *p != ' ' && fi < ED_FILENAME_LEN - 1) {
                ed_filename[fi++] = *p++;
            }
            ed_filename[fi] = '\0';
        }
        if (ed_filename[0]) {
            ed_print(ed_filename);
            ed_print("\n");
        } else {
            ed_error("no filename");
        }
        break;
    }

    /* ── (s)ubstitute ── */
    case 's': {
        if (ed_nlines == 0) {
            ed_error("invalid address");
            return;
        }
        if (addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        ed_cmd_substitute(addr1, addr2, p);
        break;
    }

    /* ── (m)ove ── */
    case 'm': {
        if (ed_nlines == 0 || addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        p++;
        int dest_set = 0;
        int dest = ed_parse_address(&p, &dest_set);
        if (!dest_set) {
            ed_error("invalid destination");
            return;
        }
        if (dest >= addr1 && dest <= addr2) {
            ed_error("invalid destination");
            return;
        }
        ed_save_undo();

        /* Collect lines to move */
        int count = addr2 - addr1 + 1;
        char *moved[ED_MAX_LINES];
        for (int i = 0; i < count; i++) {
            moved[i] = ed_lines[addr1 - 1 + i];
        }

        /* Remove from old position */
        for (int i = addr1 - 1; i < ed_nlines - count; i++) {
            ed_lines[i] = ed_lines[i + count];
        }
        ed_nlines -= count;

        /* Adjust destination if it was after the removed range */
        if (dest > addr2) dest -= count;

        /* Insert at destination */
        for (int i = ed_nlines; i > dest; i--) {
            ed_lines[i + count - 1] = ed_lines[i - 1];
        }
        for (int i = 0; i < count; i++) {
            ed_lines[dest + i] = moved[i];
        }
        ed_nlines += count;
        ed_cur = dest + count;
        ed_dirty = 1;
        break;
    }

    /* ── (t)ransfer (copy) ── */
    case 't': {
        if (ed_nlines == 0 || addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        p++;
        int dest_set = 0;
        int dest = ed_parse_address(&p, &dest_set);
        if (!dest_set) {
            ed_error("invalid destination");
            return;
        }
        if (dest < 0 || dest > ed_nlines) {
            ed_error("invalid destination");
            return;
        }
        ed_save_undo();

        int count = addr2 - addr1 + 1;
        for (int i = 0; i < count; i++) {
            if (!ed_insert_line(dest + i, ed_lines[addr1 - 1 + i])) return;
        }
        ed_cur = dest + count;
        ed_dirty = 1;
        break;
    }

    /* ── (j)oin ── */
    case 'j': {
        if (ed_nlines == 0) {
            ed_error("invalid address");
            return;
        }
        if (!has_range) {
            addr1 = ed_cur;
            addr2 = ed_cur + 1;
        }
        if (addr1 < 1 || addr2 > ed_nlines || addr1 >= addr2) {
            ed_error("invalid address");
            return;
        }
        p++;
        ed_save_undo();

        /* Build joined line */
        char joined[ED_MAX_LINE_LEN];
        int jp = 0;
        for (int i = addr1; i <= addr2 && jp < ED_MAX_LINE_LEN - 1; i++) {
            const char *s = ed_lines[i - 1];
            while (*s && jp < ED_MAX_LINE_LEN - 1) {
                joined[jp++] = *s++;
            }
        }
        joined[jp] = '\0';

        /* Replace first line and delete the rest */
        ed_replace_line(addr1, joined);
        for (int i = addr2; i > addr1; i--) {
            ed_delete_line(i);
        }
        ed_cur = addr1;
        ed_dirty = 1;
        break;
    }

    /* ── (k)mark ── */
    case 'k': {
        p++;
        if (*p < 'a' || *p > 'z') {
            ed_error("invalid mark");
            return;
        }
        if (ed_nlines == 0 || addr1 < 1 || addr1 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        ed_marks[*p - 'a'] = addr1;
        break;
    }

    /* ── (u)ndo ── */
    case 'u': {
        ed_do_undo();
        break;
    }

    /* ── (g)lobal ── */
    case 'g': {
        if (ed_nlines == 0) {
            ed_error("invalid address");
            return;
        }
        p++;
        if (!*p) {
            ed_error("invalid command suffix");
            return;
        }
        char gdelim = *p;
        p++;
        char gpattern[ED_CMD_BUF_LEN];
        int gpi = 0;
        while (*p && *p != gdelim && gpi < ED_CMD_BUF_LEN - 1) {
            gpattern[gpi++] = *p++;
        }
        gpattern[gpi] = '\0';
        if (*p == gdelim) p++;

        if (gpi == 0) {
            if (ed_last_search[0] == '\0') {
                ed_error("no previous pattern");
                return;
            }
            ed_strcpy(gpattern, ed_last_search);
        } else {
            ed_strcpy(ed_last_search, gpattern);
        }

        /* Get the command to execute */
        const char *gcmd = p;
        if (!*gcmd) gcmd = "p";

        /* Mark matching lines */
        uint8_t marked[ED_MAX_LINES];
        memset(marked, 0, (size_t)ed_nlines);
        for (int i = addr1; i <= addr2; i++) {
            if (ed_regex_search(gpattern, ed_lines[i - 1])) {
                marked[i - 1] = 1;
            }
        }

        /* Execute command on marked lines */
        for (int i = 0; i < ed_nlines; i++) {
            if (marked[i]) {
                ed_cur = i + 1;
                /* Build a command line with current address */
                char gcmd_buf[ED_CMD_BUF_LEN];
                /* For simple commands like p, d, s/...  */
                int gi = 0;
                /* Write current line number */
                char numstr[12];
                itoa(ed_cur, numstr);
                const char *ns = numstr;
                while (*ns && gi < ED_CMD_BUF_LEN - 1) gcmd_buf[gi++] = *ns++;
                const char *gc = gcmd;
                while (*gc && gi < ED_CMD_BUF_LEN - 1) gcmd_buf[gi++] = *gc++;
                gcmd_buf[gi] = '\0';
                ed_exec_command(gcmd_buf);

                /* Lines may have been deleted - adjust */
                if (ed_nlines < (int)(sizeof(marked))) {
                    /* If lines were deleted, we can't reliably continue */
                }
            }
        }
        break;
    }

    /* ── (v) inverse global ── */
    case 'v': {
        if (ed_nlines == 0) {
            ed_error("invalid address");
            return;
        }
        p++;
        if (!*p) {
            ed_error("invalid command suffix");
            return;
        }
        char vdelim = *p;
        p++;
        char vpattern[ED_CMD_BUF_LEN];
        int vpi = 0;
        while (*p && *p != vdelim && vpi < ED_CMD_BUF_LEN - 1) {
            vpattern[vpi++] = *p++;
        }
        vpattern[vpi] = '\0';
        if (*p == vdelim) p++;

        if (vpi == 0) {
            if (ed_last_search[0] == '\0') {
                ed_error("no previous pattern");
                return;
            }
            ed_strcpy(vpattern, ed_last_search);
        } else {
            ed_strcpy(ed_last_search, vpattern);
        }

        const char *vcmd = p;
        if (!*vcmd) vcmd = "p";

        /* Mark NON-matching lines */
        uint8_t vmarked[ED_MAX_LINES];
        memset(vmarked, 0, (size_t)ed_nlines);
        for (int i = addr1; i <= addr2; i++) {
            if (!ed_regex_search(vpattern, ed_lines[i - 1])) {
                vmarked[i - 1] = 1;
            }
        }

        for (int i = 0; i < ed_nlines; i++) {
            if (vmarked[i]) {
                ed_cur = i + 1;
                char vcmd_buf[ED_CMD_BUF_LEN];
                int vi = 0;
                char numstr[12];
                itoa(ed_cur, numstr);
                const char *ns = numstr;
                while (*ns && vi < ED_CMD_BUF_LEN - 1) vcmd_buf[vi++] = *ns++;
                const char *vc = vcmd;
                while (*vc && vi < ED_CMD_BUF_LEN - 1) vcmd_buf[vi++] = *vc++;
                vcmd_buf[vi] = '\0';
                ed_exec_command(vcmd_buf);
            }
        }
        break;
    }

    /* ── (H)elp toggle / (h)elp ── */
    case 'H': {
        ed_show_errors = !ed_show_errors;
        if (ed_show_errors && ed_last_error[0]) {
            ed_print(ed_last_error);
            ed_print("\n");
        }
        break;
    }
    case 'h': {
        if (ed_last_error[0]) {
            ed_print(ed_last_error);
            ed_print("\n");
        }
        break;
    }

    /* ── (P)rompt toggle - we just accept it ── */
    case 'P': {
        /* In our implementation, prompt is always shown */
        break;
    }

    /* ── Address-only (go to line) ── */
    case '\0': {
        /* Already handled at top */
        break;
    }

    /* ── Newline / +/- ── */
    case '+': {
        p++;
        int offset = 1;
        if (ed_isdigit(*p)) offset = ed_parse_int(&p);
        int target = ed_cur + offset;
        if (target < 1 || target > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        ed_cur = target;
        ed_print(ed_lines[ed_cur - 1]);
        ed_print("\n");
        break;
    }
    case '-': {
        p++;
        int offset = 1;
        if (ed_isdigit(*p)) offset = ed_parse_int(&p);
        int target = ed_cur - offset;
        if (target < 1 || target > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        ed_cur = target;
        ed_print(ed_lines[ed_cur - 1]);
        ed_print("\n");
        break;
    }

    /* ── (W)rite append ── */
    case 'W': {
        p++;
        while (*p == ' ') p++;
        if (*p) {
            int fi = 0;
            while (*p && *p != ' ' && fi < ED_FILENAME_LEN - 1) {
                ed_filename[fi++] = *p++;
            }
            ed_filename[fi] = '\0';
        }
        if (ed_filename[0] == '\0') {
            ed_error("no filename");
            return;
        }

        /* For W (append), read existing file, concatenate, and rewrite.
         * True append isn't possible on FAT16 without more complexity,
         * so we read-modify-write. */
        int existing_size = 0;
        char *existing_data = NULL;

        /* Try reading existing file from disk */
        fat16_file_t *ef = fat16_open(ed_filename);
        if (ef) {
            existing_size = (int)ef->file_size;
            if (existing_size > 0) {
                existing_data = kmalloc((size_t)existing_size);
                if (existing_data) {
                    fat16_read(ef, existing_data, (uint32_t)existing_size);
                }
            }
            fat16_close(ef);
        }

        /* Calculate new data size */
        int new_bytes = 0;
        for (int i = addr1; i <= addr2; i++) {
            new_bytes += (int)ed_strlen(ed_lines[i - 1]) + 1;
        }

        int total = existing_size + new_bytes;
        char *combined = kmalloc((size_t)total);
        if (!combined) {
            if (existing_data) kfree(existing_data);
            ed_error("out of memory");
            return;
        }

        /* Copy existing data */
        if (existing_data && existing_size > 0) {
            memcpy(combined, existing_data, (size_t)existing_size);
            kfree(existing_data);
        }

        /* Append new lines */
        int pos = existing_size;
        for (int i = addr1; i <= addr2; i++) {
            const char *line = ed_lines[i - 1];
            size_t len = ed_strlen(line);
            memcpy(combined + pos, line, len);
            pos += (int)len;
            combined[pos++] = '\n';
        }

        int ret = fat16_write_file(ed_filename, combined, (uint32_t)total);
        kfree(combined);

        if (ret < 0) {
            ed_error("write failed");
            return;
        }

        ed_print_int(new_bytes);
        ed_print("\n");
        ed_dirty = 0;
        break;
    }

    default:
        /* If we have a range and no command, go to that line */
        if (has_range && cmd == '\0') {
            if (addr2 >= 1 && addr2 <= ed_nlines) {
                ed_cur = addr2;
                ed_print(ed_lines[ed_cur - 1]);
                ed_print("\n");
            } else if (ed_nlines == 0) {
                ed_error("invalid address");
            }
        } else {
            ed_error("unknown command");
        }
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Main entry point
 * ══════════════════════════════════════════════════════════════════════ */

void ed_run(const char *filename) {
    /* Initialize state */
    ed_nlines = 0;
    ed_cur = 0;
    ed_dirty = 0;
    ed_quit = 0;
    ed_show_errors = 0;
    ed_last_error[0] = '\0';
    ed_last_search[0] = '\0';
    ed_last_replace[0] = '\0';
    ed_undo_nlines = 0;
    ed_undo_valid = 0;
    ed_filename[0] = '\0';

    memset(ed_marks, 0, sizeof(ed_marks));
    memset(ed_lines, 0, sizeof(ed_lines));
    memset(ed_undo_lines, 0, sizeof(ed_undo_lines));

    /* Load file if specified */
    if (filename && filename[0]) {
        /* Store the filename */
        int fi = 0;
        while (filename[fi] && fi < ED_FILENAME_LEN - 1) {
            ed_filename[fi] = filename[fi];
            fi++;
        }
        ed_filename[fi] = '\0';

        /* Try in-memory filesystem first */
        const fs_file_t *f = fs_find(filename);
        if (f && f->data) {
            int bytes = ed_load_text(f->data, f->size, 0);
            ed_print_int(bytes);
            ed_print("\n");
        } else {
            /* Try FAT16 filesystem */
            fat16_file_t *df = fat16_open(filename);
            if (df) {
                char buf[512];
                char bigbuf[4096];
                int total = 0;
                int bytes_read;
                while ((bytes_read = fat16_read(df, buf, sizeof(buf))) > 0) {
                    for (int i = 0; i < bytes_read && total < 4095; i++) {
                        bigbuf[total++] = buf[i];
                    }
                }
                bigbuf[total] = '\0';
                fat16_close(df);
                int loaded = ed_load_text(bigbuf, (uint32_t)total, 0);
                ed_print_int(loaded);
                ed_print("\n");
            } else {
                ed_print(ed_filename);
                ed_print(": No such file\n");
            }
        }
    }

    /* Main command loop */
    char cmd[ED_CMD_BUF_LEN];
    while (!ed_quit) {
        ed_readline(cmd, ED_CMD_BUF_LEN);
        ed_exec_command(cmd);
    }

    /* Clean up */
    for (int i = 0; i < ed_nlines; i++) {
        if (ed_lines[i]) kfree(ed_lines[i]);
    }
    for (int i = 0; i < ed_undo_nlines; i++) {
        if (ed_undo_lines[i]) kfree(ed_undo_lines[i]);
    }
}
