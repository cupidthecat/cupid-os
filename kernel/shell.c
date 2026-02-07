#include "kernel.h"
#include "string.h"
#include "keyboard.h"
#include "timer.h"
#include "ports.h"
#include "types.h"
#include "fs.h"
#include "shell.h"
#include "blockcache.h"
#include "fat16.h"
#include "memory.h"
#include "math.h"
#include "panic.h"
#include "assert.h"
#include "../drivers/serial.h"
#include "ed.h"
#include "process.h"
#include "cupidscript.h"
#include "vfs.h"
#include "exec.h"
#include "terminal_app.h"
#include "notepad.h"
#include "terminal_ansi.h"
#include "calendar.h"
#include "../drivers/rtc.h"

#define MAX_INPUT_LEN 80
#define HISTORY_SIZE 16

/* ══════════════════════════════════════════════════════════════════════
 *  GUI output mode support
 * ══════════════════════════════════════════════════════════════════════ */
static shell_output_mode_t output_mode = SHELL_OUTPUT_TEXT;
static char gui_buffer[SHELL_ROWS * SHELL_COLS];
static shell_color_t gui_color_buffer[SHELL_ROWS * SHELL_COLS];
static terminal_color_state_t shell_ansi_state;
static int gui_cursor_x = 0;
static int gui_cursor_y = 0;
static int gui_visible_cols = SHELL_COLS;  /* actual visible column width */

/* ── I/O redirection capture buffer ─────────────────────────────── */
#define REDIR_BUF_SIZE 4096
static char  *redir_buf  = NULL;    /* heap-allocated when active */
static int    redir_len  = 0;
static bool   redir_active = false;

/* ── Current working directory ──────────────────────────────────── */
static char shell_cwd[VFS_MAX_PATH] = "/";

const char *shell_get_cwd(void) {
    return shell_cwd;
}

/**
 * Resolve a possibly-relative path against the CWD.
 * Result written to `out` (must be VFS_MAX_PATH bytes).
 *   - Absolute paths (starting with '/') are copied as-is.
 *   - Relative paths are joined with CWD.
 *   - ".." and "." components are resolved.
 */
static void shell_resolve_path(const char *input, char *out) {
    char tmp[VFS_MAX_PATH];
    int ti = 0;

    if (!input || input[0] == '\0') {
        /* No input => CWD */
        int i = 0;
        while (shell_cwd[i] && i < VFS_MAX_PATH - 1) {
            out[i] = shell_cwd[i];
            i++;
        }
        out[i] = '\0';
        return;
    }

    /* Find length, stripping trailing whitespace */
    int input_len = 0;
    while (input[input_len]) input_len++;
    while (input_len > 0 && (input[input_len - 1] == ' ' ||
                              input[input_len - 1] == '\t'))
        input_len--;

    if (input_len == 0) {
        int i = 0;
        while (shell_cwd[i] && i < VFS_MAX_PATH - 1) {
            out[i] = shell_cwd[i]; i++;
        }
        out[i] = '\0';
        return;
    }

    if (input[0] == '/') {
        /* Absolute path */
        int i = 0;
        while (i < input_len && i < VFS_MAX_PATH - 1) {
            tmp[i] = input[i];
            i++;
        }
        tmp[i] = '\0';
        ti = i;
    } else {
        /* Relative path: prepend CWD */
        int i = 0;
        while (shell_cwd[i] && ti < VFS_MAX_PATH - 1) {
            tmp[ti++] = shell_cwd[i++];
        }
        if (ti > 0 && tmp[ti - 1] != '/' && ti < VFS_MAX_PATH - 1) {
            tmp[ti++] = '/';
        }
        i = 0;
        while (i < input_len && ti < VFS_MAX_PATH - 1) {
            tmp[ti++] = input[i++];
        }
        tmp[ti] = '\0';
    }

    /* Now normalize: split on '/' and resolve . and .. */
    /* Use `out` as a stack of path components */
    int oi = 0;
    out[oi++] = '/';  /* Always starts with / */

    const char *p = tmp;
    while (*p) {
        while (*p == '/') p++;  /* skip slashes */
        if (*p == '\0') break;

        /* Find end of component */
        const char *end = p;
        while (*end && *end != '/') end++;
        int len = (int)(end - p);

        if (len == 1 && p[0] == '.') {
            /* "." => skip */
        } else if (len == 2 && p[0] == '.' && p[1] == '.') {
            /* ".." => go up one level */
            if (oi > 1) {
                oi--;  /* remove trailing slash or char */
                while (oi > 1 && out[oi - 1] != '/') oi--;
            }
        } else {
            /* Normal component */
            if (oi > 1 && out[oi - 1] != '/') {
                out[oi++] = '/';
            }
            for (int j = 0; j < len && oi < VFS_MAX_PATH - 1; j++) {
                out[oi++] = p[j];
            }
        }
        p = end;
    }

    /* Remove trailing slash (unless root) */
    if (oi > 1 && out[oi - 1] == '/') oi--;
    out[oi] = '\0';
}

/* Forward declarations */
static void execute_command(const char *input);
static void shell_gui_putchar(char c);
static void shell_gui_print(const char *s);
static void shell_gui_print_int(uint32_t num);

void shell_set_output_mode(shell_output_mode_t mode) {
    output_mode = mode;
    if (mode == SHELL_OUTPUT_GUI) {
        memset(gui_buffer, 0, sizeof(gui_buffer));
        /* Initialize color buffer to default: light gray on black */
        for (int i = 0; i < SHELL_ROWS * SHELL_COLS; i++) {
            gui_color_buffer[i].fg = ANSI_DEFAULT_FG;
            gui_color_buffer[i].bg = ANSI_DEFAULT_BG;
        }
        ansi_init(&shell_ansi_state);
        gui_cursor_x = 0;
        gui_cursor_y = 0;
        /* Print initial prompt into GUI buffer */
        const char *prompt = "cupid-os shell\n";
        while (*prompt) {
            shell_gui_putchar(*prompt);
            prompt++;
        }
        {
            const char *cp = shell_cwd;
            while (*cp) {
                shell_gui_putchar(*cp);
                cp++;
            }
        }
        prompt = "> ";
        while (*prompt) {
            shell_gui_putchar(*prompt);
            prompt++;
        }
        /* Configure all subsystems to use GUI output */
        fat16_set_output(shell_gui_print, shell_gui_putchar, shell_gui_print_int);
        ed_set_output(shell_gui_print, shell_gui_putchar, shell_gui_print_int);
        memory_set_output(shell_gui_print, shell_gui_print_int);
        panic_set_output(shell_gui_print, shell_gui_putchar);
        blockcache_set_output(shell_gui_print, shell_gui_print_int);
    } else {
        /* Reset all subsystems to use kernel output */
        fat16_set_output(print, putchar, print_int);
        ed_set_output(print, putchar, print_int);
        memory_set_output(print, print_int);
        panic_set_output(print, putchar);
        blockcache_set_output(print, print_int);
    }
}

shell_output_mode_t shell_get_output_mode(void) {
    return output_mode;
}

void shell_set_visible_cols(int cols) {
    if (cols > 0 && cols <= SHELL_COLS) {
        gui_visible_cols = cols;
    }
}

const char *shell_get_buffer(void) {
    return gui_buffer;
}

const shell_color_t *shell_get_color_buffer(void) {
    return gui_color_buffer;
}

int shell_get_cursor_x(void) {
    return gui_cursor_x;
}

int shell_get_cursor_y(void) {
    return gui_cursor_y;
}

/* Scroll both char and color buffers up one line */
static void shell_gui_scroll(void) {
    memcpy(gui_buffer, gui_buffer + SHELL_COLS,
           (size_t)((SHELL_ROWS - 1) * SHELL_COLS));
    memset(gui_buffer + (SHELL_ROWS - 1) * SHELL_COLS, 0, (size_t)SHELL_COLS);

    memcpy(gui_color_buffer, gui_color_buffer + SHELL_COLS,
           sizeof(shell_color_t) * (size_t)(SHELL_ROWS - 1) * (size_t)SHELL_COLS);
    /* Clear last row colors to default */
    for (int i = 0; i < SHELL_COLS; i++) {
        gui_color_buffer[(SHELL_ROWS - 1) * SHELL_COLS + i].fg = ANSI_DEFAULT_FG;
        gui_color_buffer[(SHELL_ROWS - 1) * SHELL_COLS + i].bg = ANSI_DEFAULT_BG;
    }
    gui_cursor_y = SHELL_ROWS - 1;
}

/* Store current ANSI color into a cell's color entry */
static void shell_set_cell_color(int idx) {
    gui_color_buffer[idx].fg = ansi_get_fg(&shell_ansi_state);
    gui_color_buffer[idx].bg = ansi_get_bg(&shell_ansi_state);
}

/* Put a character into the GUI buffer with ANSI escape processing */
static void shell_gui_putchar(char c) {
    /* Feed character through ANSI parser first */
    ansi_result_t result = ansi_process_char(&shell_ansi_state, c);

    switch (result) {
    case ANSI_RESULT_SKIP:
        /* Escape sequence in progress or color code processed — nothing to display */
        return;

    case ANSI_RESULT_CLEAR:
        /* ESC[2J — clear entire screen */
        memset(gui_buffer, 0, sizeof(gui_buffer));
        for (int i = 0; i < SHELL_ROWS * SHELL_COLS; i++) {
            gui_color_buffer[i].fg = ANSI_DEFAULT_FG;
            gui_color_buffer[i].bg = ANSI_DEFAULT_BG;
        }
        gui_cursor_x = 0;
        gui_cursor_y = 0;
        return;

    case ANSI_RESULT_HOME:
        /* ESC[H — cursor home */
        gui_cursor_x = 0;
        gui_cursor_y = 0;
        return;

    case ANSI_RESULT_PRINT:
        break; /* fall through to normal character handling */
    }

    /* Normal character handling */
    if (c == '\n') {
        /* Clear remainder of current line */
        for (int i = gui_cursor_x; i < SHELL_COLS; i++) {
            int idx = gui_cursor_y * SHELL_COLS + i;
            gui_buffer[idx] = 0;
            gui_color_buffer[idx].fg = ANSI_DEFAULT_FG;
            gui_color_buffer[idx].bg = ANSI_DEFAULT_BG;
        }
        gui_cursor_x = 0;
        gui_cursor_y++;
    } else if (c == '\b') {
        if (gui_cursor_x > 0) {
            gui_cursor_x--;
            int idx = gui_cursor_y * SHELL_COLS + gui_cursor_x;
            gui_buffer[idx] = ' ';
            shell_set_cell_color(idx);
        }
    } else if (c == '\t') {
        /* Tab = 4 spaces */
        for (int t = 0; t < 4 && gui_cursor_x < gui_visible_cols; t++) {
            int idx = gui_cursor_y * SHELL_COLS + gui_cursor_x;
            gui_buffer[idx] = ' ';
            shell_set_cell_color(idx);
            gui_cursor_x++;
        }
    } else {
        if (gui_cursor_x < gui_visible_cols) {
            int idx = gui_cursor_y * SHELL_COLS + gui_cursor_x;
            gui_buffer[idx] = c;
            shell_set_cell_color(idx);
            gui_cursor_x++;
        }
    }

    /* Wrap at visible column width */
    if (gui_cursor_x >= gui_visible_cols) {
        gui_cursor_x = 0;
        gui_cursor_y++;
    }

    /* Scroll */
    if (gui_cursor_y >= SHELL_ROWS) {
        shell_gui_scroll();
    }
}

/* Print a string into the GUI buffer */
static void shell_gui_print(const char *s) {
    while (*s) {
        shell_gui_putchar(*s);
        s++;
    }
}

/* Print an integer into the GUI buffer */
static void shell_gui_print_int(uint32_t num) {
    char buf[12];
    int i = 0;
    if (num == 0) { shell_gui_putchar('0'); return; }
    while (num > 0) {
        buf[i++] = (char)((num % 10) + (uint32_t)'0');
        num /= 10;
    }
    while (i > 0) shell_gui_putchar(buf[--i]);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Output wrappers that route to GUI or text mode
 * ══════════════════════════════════════════════════════════════════════ */
static void shell_print(const char *s) {
    if (redir_active && redir_buf) {
        while (*s && redir_len < REDIR_BUF_SIZE - 1) {
            redir_buf[redir_len++] = *s++;
        }
        return;
    }
    if (output_mode == SHELL_OUTPUT_GUI) {
        shell_gui_print(s);
    } else {
        print(s);
    }
}

static void shell_putchar(char c) {
    if (redir_active && redir_buf) {
        if (redir_len < REDIR_BUF_SIZE - 1) {
            redir_buf[redir_len++] = c;
        }
        return;
    }
    if (output_mode == SHELL_OUTPUT_GUI) {
        shell_gui_putchar(c);
    } else {
        putchar(c);
    }
}

static void shell_print_int(uint32_t num) {
    if (redir_active && redir_buf) {
        char tmp[12];
        int i = 0;
        if (num == 0) { tmp[i++] = '0'; }
        else { while (num > 0) { tmp[i++] = (char)('0' + (num % 10)); num /= 10; } }
        while (i > 0 && redir_len < REDIR_BUF_SIZE - 1) {
            redir_buf[redir_len++] = tmp[--i];
        }
        return;
    }
    if (output_mode == SHELL_OUTPUT_GUI) {
        shell_gui_print_int(num);
    } else {
        print_int(num);
    }
}

/* External-linkage wrappers for kernel.c routing */
void shell_gui_putchar_ext(char c) {
    shell_gui_putchar(c);
}

void shell_gui_print_ext(const char *s) {
    shell_gui_print(s);
}

void shell_gui_print_int_ext(uint32_t num) {
    shell_gui_print_int(num);
}

// Scancodes for extended keys we care about
#define SCANCODE_ARROW_UP    0x48
#define SCANCODE_ARROW_DOWN  0x50
#define SCANCODE_ARROW_LEFT  0x4B
#define SCANCODE_ARROW_RIGHT 0x4D

struct shell_command {
    const char* name;
    const char* description;
    void (*func)(const char*);
};

// Forward declarations for commands
static void shell_help(const char* args);
static void shell_clear(const char* args);
static void shell_echo(const char* args);
static void shell_time_cmd(const char* args);
static void shell_reboot_cmd(const char* args);
static void shell_history_cmd(const char* args);
static void shell_ls(const char* args);
static void shell_cat(const char* args);
static void shell_sync(const char* args);
static void shell_cachestats(const char* args);
static void shell_lsdisk(const char* args);
static void shell_catdisk(const char* args);
static void shell_memdump(const char* args);
static void shell_memstats(const char* args);
static void shell_memleak(const char* args);
static void shell_memcheck(const char* args);
static void shell_stacktrace(const char* args);
static void shell_registers(const char* args);
static void shell_sysinfo(const char* args);
static void shell_loglevel(const char* args);
static void shell_logdump(const char* args);
static void shell_crashtest(const char* args);
static void shell_ed(const char* args);
static void shell_ps(const char* args);
static void shell_kill_cmd(const char* args);
static void shell_spawn(const char* args);
static void shell_yield_cmd(const char* args);
static void shell_cupid(const char* args);
static void shell_cd(const char* args);
static void shell_pwd(const char* args);
static void shell_vls(const char* args);
static void shell_vcat(const char* args);
static void shell_vmount(const char* args);
static void shell_vstat(const char* args);
static void shell_vmkdir(const char* args);
static void shell_vrm(const char* args);
static void shell_vwrite(const char* args);
static void shell_exec_cmd(const char* args);
static void shell_notepad_cmd(const char* args);
static void shell_terminal_cmd(const char* args);
static void shell_setcolor_cmd(const char* args);
static void shell_resetcolor_cmd(const char* args);
static void shell_printc_cmd(const char* args);
static void shell_cupidfetch(const char* args);
static void shell_date(const char* args);

// List of supported commands
static struct shell_command commands[] = {
    {"help", "Show available commands", shell_help},
    {"clear", "Clear the screen", shell_clear},
    {"echo", "Echo text back", shell_echo},
    {"time", "Show system uptime", shell_time_cmd},
    {"reboot", "Reboot the machine", shell_reboot_cmd},
    {"history", "Show recent commands", shell_history_cmd},
    {"cd", "Change directory", shell_cd},
    {"pwd", "Print working directory", shell_pwd},
    {"ls", "List files in current directory", shell_ls},
    {"cat", "Show file contents", shell_cat},
    {"sync", "Flush disk cache to disk", shell_sync},
    {"cachestats", "Show cache statistics", shell_cachestats},
    {"lsdisk", "List files on disk", shell_lsdisk},
    {"catdisk", "Show file from disk", shell_catdisk},
    {"memdump", "Dump memory region (hex addr len)", shell_memdump},
    {"memstats", "Show memory statistics", shell_memstats},
    {"memleak", "Detect memory leaks", shell_memleak},
    {"memcheck", "Check heap integrity", shell_memcheck},
    {"stacktrace", "Show call stack", shell_stacktrace},
    {"registers", "Dump CPU registers", shell_registers},
    {"sysinfo", "Show system information", shell_sysinfo},
    {"loglevel", "Set serial log level", shell_loglevel},
    {"logdump", "Show recent log entries", shell_logdump},
    {"crashtest", "Test crash handling", shell_crashtest},
    {"ed", "Ed line editor", shell_ed},
    {"cupid", "Run a CupidScript (.cup) file", shell_cupid},
    {"ps", "List all processes", shell_ps},
    {"kill", "Kill a process by PID", shell_kill_cmd},
    {"spawn", "Spawn test processes", shell_spawn},
    {"yield", "Yield CPU to next process", shell_yield_cmd},
    {"vls", "List files/dirs (VFS path)", shell_vls},
    {"vcat", "Show file contents (VFS path)", shell_vcat},
    {"mount", "Show mounted filesystems", shell_vmount},
    {"vstat", "Show file/dir info (VFS path)", shell_vstat},
    {"vmkdir", "Create directory (VFS path)", shell_vmkdir},
    {"vrm", "Delete file (VFS path)", shell_vrm},
    {"vwrite", "Write text to file (VFS path)", shell_vwrite},
    {"exec", "Run a binary (ELF or CUPD)", shell_exec_cmd},
    {"notepad", "Open Notepad", shell_notepad_cmd},
    {"terminal", "Open a Terminal window", shell_terminal_cmd},
    {"setcolor", "Set terminal color (fg [bg])", shell_setcolor_cmd},
    {"resetcolor", "Reset terminal colors", shell_resetcolor_cmd},
    {"printc", "Print colored text (fg text)", shell_printc_cmd},
    {"date", "Show current date and time", shell_date},
    {"cupidfetch", "Show system info with ASCII art", shell_cupidfetch},
    {0, 0, 0} // Null terminator
};

static char history[HISTORY_SIZE][MAX_INPUT_LEN + 1];
static int history_count = 0;
static int history_next = 0;     // Next insert slot
static int history_view = -1;    // -1 when not browsing history

// Minimal strncmp to avoid touching global string utils
static int shell_strncmp(const char* s1, const char* s2, size_t n) {
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

static void history_record(const char* line) {
    if (!line || line[0] == '\0') {
        return;
    }

    size_t len = strlen(line);
    if (len > MAX_INPUT_LEN) {
        len = MAX_INPUT_LEN;
    }

    for (size_t i = 0; i < len; i++) {
        history[history_next][i] = line[i];
    }
    history[history_next][len] = '\0';

    history_next = (history_next + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) {
        history_count++;
    }
}

// Fetch entry offset from newest (0 = most recent)
static const char* history_get_from_newest(int offset) {
    if (offset < 0 || offset >= history_count) {
        return NULL;
    }

    int idx = history_next - 1 - offset;
    while (idx < 0) {
        idx += HISTORY_SIZE;
    }
    return history[idx];
}

static void replace_input(const char* new_text, char* input, int* pos) {
    // Erase current buffer from screen
    while (*pos > 0) {
        shell_print("\b \b");
        (*pos)--;
    }

    // Load new text
    int i = 0;
    if (new_text) {
        while (new_text[i] && i < MAX_INPUT_LEN) {
            input[i] = new_text[i];
            shell_putchar(new_text[i]);
            i++;
        }
    }
    input[i] = '\0';
    *pos = i;
}

static void tab_complete(char* input, int* pos) {
    /* ── Helper: re-display prompt + current input after listing matches ── */
    #define REDRAW_PROMPT() do {                           \
        shell_print("\n");                                 \
        shell_print(shell_cwd);                            \
        shell_print("> ");                                 \
        for (int _k = 0; _k < *pos; _k++)                 \
            shell_putchar(input[_k]);                      \
    } while (0)

    /* Find the first space to determine if we're completing
       a command name or an argument. */
    int first_space = -1;
    for (int i = 0; i < *pos; i++) {
        if (input[i] == ' ') { first_space = i; break; }
    }

    /* ────────────────────────────────────────────────────────────────────
     *  ARGUMENT / FILENAME COMPLETION
     * ──────────────────────────────────────────────────────────────────── */
    if (first_space >= 0) {
        /* Extract the command name */
        char cmd[MAX_INPUT_LEN + 1];
        for (int i = 0; i < first_space && i < MAX_INPUT_LEN; i++)
            cmd[i] = input[i];
        cmd[first_space] = '\0';

        /* Only complete filenames for commands that take paths */
        bool wants_file =
            strcmp(cmd, "cat") == 0   || strcmp(cmd, "cd") == 0    ||
            strcmp(cmd, "ls") == 0    || strcmp(cmd, "vls") == 0   ||
            strcmp(cmd, "vcat") == 0  || strcmp(cmd, "vstat") == 0 ||
            strcmp(cmd, "vmkdir") == 0|| strcmp(cmd, "vrm") == 0   ||
            strcmp(cmd, "vwrite") == 0|| strcmp(cmd, "exec") == 0  ||
            strcmp(cmd, "ed") == 0    || strcmp(cmd, "cupid") == 0 ||
            strcmp(cmd, "catdisk") == 0;

        if (!wants_file) return;

        /* Find the start of the last argument (after last space) */
        int arg_start = first_space + 1;
        for (int i = *pos - 1; i > first_space; i--) {
            if (input[i] == ' ') { arg_start = i + 1; break; }
        }

        /* Extract the partial argument typed so far */
        int arg_len = *pos - arg_start;
        char arg_prefix[VFS_MAX_PATH];
        for (int i = 0; i < arg_len && i < VFS_MAX_PATH - 1; i++)
            arg_prefix[i] = input[arg_start + i];
        arg_prefix[arg_len] = '\0';

        /* Split arg_prefix into directory part and name prefix.
         * E.g. "/home/HEL" → dir="/home", name_prefix="HEL"
         * E.g. "HEL"       → dir=CWD,     name_prefix="HEL"
         * E.g. "/dev/"      → dir="/dev",  name_prefix=""
         */
        char dir_path[VFS_MAX_PATH];
        char name_prefix[VFS_MAX_NAME];
        int name_prefix_len = 0;

        /* Find last slash in arg_prefix */
        int last_slash = -1;
        for (int i = 0; i < arg_len; i++) {
            if (arg_prefix[i] == '/') last_slash = i;
        }

        if (last_slash >= 0) {
            /* Has a slash — directory part is everything up to & including slash */
            if (last_slash == 0) {
                dir_path[0] = '/';
                dir_path[1] = '\0';
            } else {
                for (int i = 0; i < last_slash && i < VFS_MAX_PATH - 1; i++)
                    dir_path[i] = arg_prefix[i];
                dir_path[last_slash] = '\0';
            }
            /* Name prefix is everything after the last slash */
            name_prefix_len = arg_len - last_slash - 1;
            for (int i = 0; i < name_prefix_len && i < VFS_MAX_NAME - 1; i++)
                name_prefix[i] = arg_prefix[last_slash + 1 + i];
            name_prefix[name_prefix_len] = '\0';
        } else {
            /* No slash — use CWD as directory, whole arg is the name prefix */
            shell_resolve_path(".", dir_path);
            name_prefix_len = arg_len;
            for (int i = 0; i < arg_len && i < VFS_MAX_NAME - 1; i++)
                name_prefix[i] = arg_prefix[i];
            name_prefix[name_prefix_len] = '\0';
        }

        /* Open the directory and collect matches */
        int fd = vfs_open(dir_path, O_RDONLY);
        if (fd < 0) return;

        char first_match[VFS_MAX_NAME];
        first_match[0] = '\0';
        int match_count = 0;
        bool first_is_dir = false;
        vfs_dirent_t ent;

        while (vfs_readdir(fd, &ent) > 0) {
            if (shell_strncmp(ent.name, name_prefix,
                              (size_t)name_prefix_len) == 0) {
                if (match_count == 0) {
                    int n = 0;
                    while (ent.name[n] && n < VFS_MAX_NAME - 1) {
                        first_match[n] = ent.name[n];
                        n++;
                    }
                    first_match[n] = '\0';
                    first_is_dir = (ent.type == VFS_TYPE_DIR);
                }
                match_count++;
            }
        }
        vfs_close(fd);

        if (match_count == 1) {
            /* Single match — append the rest of the name */
            const char *rest = first_match + name_prefix_len;
            while (*rest && *pos < MAX_INPUT_LEN) {
                input[*pos] = *rest;
                shell_putchar(*rest);
                (*pos)++;
                rest++;
            }
            /* Append / for directories, space for files */
            if (first_is_dir && *pos < MAX_INPUT_LEN) {
                input[*pos] = '/';
                shell_putchar('/');
                (*pos)++;
            } else if (!first_is_dir && *pos < MAX_INPUT_LEN) {
                input[*pos] = ' ';
                shell_putchar(' ');
                (*pos)++;
            }
        } else if (match_count > 1) {
            /* Multiple matches — find common prefix, then list all */
            /* First, compute the longest common prefix among matches */
            /* (Re-scan directory for this) */
            int common_len = VFS_MAX_NAME;
            fd = vfs_open(dir_path, O_RDONLY);
            if (fd >= 0) {
                bool first = true;
                char common[VFS_MAX_NAME];
                common[0] = '\0';
                while (vfs_readdir(fd, &ent) > 0) {
                    if (shell_strncmp(ent.name, name_prefix,
                                      (size_t)name_prefix_len) != 0)
                        continue;
                    if (first) {
                        int n = 0;
                        while (ent.name[n] && n < VFS_MAX_NAME - 1) {
                            common[n] = ent.name[n]; n++;
                        }
                        common[n] = '\0';
                        common_len = n;
                        first = false;
                    } else {
                        int n = 0;
                        while (n < common_len && ent.name[n] &&
                               common[n] == ent.name[n])
                            n++;
                        common_len = n;
                        common[common_len] = '\0';
                    }
                }
                vfs_close(fd);

                /* Append common prefix beyond what's already typed */
                if (common_len > name_prefix_len) {
                    for (int ci = name_prefix_len;
                         ci < common_len && *pos < MAX_INPUT_LEN; ci++) {
                        input[*pos] = common[ci];
                        shell_putchar(common[ci]);
                        (*pos)++;
                    }
                    /* Don't list matches yet — user can press Tab again */
                    return;
                }
            }

            /* List all matches */
            shell_print("\n");
            fd = vfs_open(dir_path, O_RDONLY);
            if (fd >= 0) {
                while (vfs_readdir(fd, &ent) > 0) {
                    if (shell_strncmp(ent.name, name_prefix,
                                      (size_t)name_prefix_len) != 0)
                        continue;
                    shell_print(ent.name);
                    if (ent.type == VFS_TYPE_DIR) shell_putchar('/');
                    shell_print("  ");
                }
                vfs_close(fd);
            }
            REDRAW_PROMPT();
        }
        /* match_count == 0: no matches, do nothing */
        return;
    }

    /* ────────────────────────────────────────────────────────────────────
     *  COMMAND NAME COMPLETION
     * ──────────────────────────────────────────────────────────────────── */
    size_t prefix_len = (size_t)(*pos);
    char prefix[MAX_INPUT_LEN + 1];
    for (size_t i = 0; i < prefix_len; i++) {
        prefix[i] = input[i];
    }
    prefix[prefix_len] = '\0';

    const char* first_match_cmd = NULL;
    int match_count = 0;

    for (int i = 0; commands[i].name; i++) {
        if (shell_strncmp(commands[i].name, prefix, prefix_len) == 0) {
            if (match_count == 0) {
                first_match_cmd = commands[i].name;
            }
            match_count++;
        }
    }

    if (match_count == 1 && first_match_cmd) {
        const char* completion = first_match_cmd + prefix_len;
        while (*completion && *pos < MAX_INPUT_LEN) {
            input[*pos] = *completion;
            shell_putchar(*completion);
            (*pos)++;
            completion++;
        }
        /* Add trailing space after command */
        if (*pos < MAX_INPUT_LEN) {
            input[*pos] = ' ';
            shell_putchar(' ');
            (*pos)++;
        }
    } else if (match_count > 1) {
        /* Compute longest common prefix among matching commands */
        const char *common_cmd = NULL;
        int common_cmd_len = 0;
        for (int i = 0; commands[i].name; i++) {
            if (shell_strncmp(commands[i].name, prefix, prefix_len) != 0)
                continue;
            if (!common_cmd) {
                common_cmd = commands[i].name;
                common_cmd_len = 0;
                while (common_cmd[common_cmd_len]) common_cmd_len++;
            } else {
                int n = 0;
                while (n < common_cmd_len &&
                       commands[i].name[n] &&
                       common_cmd[n] == commands[i].name[n])
                    n++;
                common_cmd_len = n;
            }
        }

        /* Append common prefix beyond what's typed */
        if (common_cmd && common_cmd_len > (int)prefix_len) {
            for (int ci = (int)prefix_len;
                 ci < common_cmd_len && *pos < MAX_INPUT_LEN; ci++) {
                input[*pos] = common_cmd[ci];
                shell_putchar(common_cmd[ci]);
                (*pos)++;
            }
            return;
        }

        /* List all matching commands */
        shell_print("\n");
        for (int i = 0; commands[i].name; i++) {
            if (shell_strncmp(commands[i].name, prefix, prefix_len) == 0) {
                shell_print(commands[i].name);
                shell_print("  ");
            }
        }
        REDRAW_PROMPT();
    }

    #undef REDRAW_PROMPT
}

// Echo command implementation
static void shell_echo(const char* args) {
    if (args) {
        shell_print(args);
    }
    shell_print("\n");  // Ensure we always print a newline
}

/* ══════════════════════════════════════════════════════════════════════
 *  Color commands – emit ANSI escape sequences through the shell output
 * ══════════════════════════════════════════════════════════════════════ */
static int shell_parse_int(const char *s) {
    int n = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + (*s - '0');
        s++;
    }
    return neg ? -n : n;
}

static void shell_setcolor_cmd(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: setcolor <fg 0-15> [bg 0-7]\n");
        return;
    }

    /* Parse fg */
    const char *p = args;
    while (*p == ' ') p++;
    int fg = shell_parse_int(p);

    /* Skip past fg number */
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    int bg = -1;
    if (*p) {
        bg = shell_parse_int(p);
    }

    /* Build ANSI escape sequence */
    char buf[32];
    int i = 0;
    buf[i++] = '\x1B';
    buf[i++] = '[';
    if (fg >= 8) {
        buf[i++] = '9';
        buf[i++] = (char)('0' + (fg - 8));
    } else {
        buf[i++] = '3';
        buf[i++] = (char)('0' + fg);
    }
    if (bg >= 0) {
        buf[i++] = ';';
        buf[i++] = '4';
        buf[i++] = (char)('0' + (bg & 7));
    }
    buf[i++] = 'm';
    buf[i] = '\0';

    shell_print(buf);
}

static void shell_resetcolor_cmd(const char* args) {
    (void)args;
    shell_print("\x1B[0m");
}

static void shell_printc_cmd(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: printc <fg 0-15> <text>\n");
        return;
    }

    /* Parse fg color */
    const char *p = args;
    while (*p == ' ') p++;
    int color = shell_parse_int(p);

    /* Skip past color number to get text */
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    if (*p == '\0') {
        shell_print("Usage: printc <fg 0-15> <text>\n");
        return;
    }

    /* Emit color */
    char ansi_buf[16];
    int i = 0;
    ansi_buf[i++] = '\x1B';
    ansi_buf[i++] = '[';
    if (color >= 8) {
        ansi_buf[i++] = '9';
        ansi_buf[i++] = (char)('0' + (color - 8));
    } else {
        ansi_buf[i++] = '3';
        ansi_buf[i++] = (char)('0' + color);
    }
    ansi_buf[i++] = 'm';
    ansi_buf[i] = '\0';
    shell_print(ansi_buf);

    /* Print text */
    shell_print(p);
    shell_print("\n");

    /* Reset */
    shell_print("\x1B[0m");
}

static void shell_help(const char* args) {
    (void)args;
    shell_print("Available commands:\n");
    for (int i = 0; commands[i].name; i++) {
        shell_print("  ");
        shell_print(commands[i].name);
        shell_print(" - ");
        shell_print(commands[i].description);
        shell_print("\n");
    }
}

static void shell_clear(const char* args) {
    (void)args;
    if (output_mode == SHELL_OUTPUT_GUI) {
        /* Clear GUI buffer and color buffer */
        memset(gui_buffer, 0, sizeof(gui_buffer));
        for (int i = 0; i < SHELL_ROWS * SHELL_COLS; i++) {
            gui_color_buffer[i].fg = ANSI_DEFAULT_FG;
            gui_color_buffer[i].bg = ANSI_DEFAULT_BG;
        }
        gui_cursor_x = 0;
        gui_cursor_y = 0;
        /* Reset ANSI color state so subsequent output uses defaults */
        ansi_reset(&shell_ansi_state);
    } else {
        clear_screen();
    }
}

static void shell_time_cmd(const char* args) {
    (void)args;
    uint32_t ms = timer_get_uptime_ms();
    uint32_t seconds = ms / 1000;
    uint32_t remainder = ms % 1000;

    shell_print("Uptime: ");
    shell_print_int(seconds);
    shell_putchar('.');
    // Print zero-padded remainder as fractional seconds
    shell_putchar((char)('0' + (remainder / 100)));
    shell_putchar((char)('0' + ((remainder / 10) % 10)));
    shell_putchar((char)('0' + (remainder % 10)));
    shell_print("s (");
    shell_print_int(ms);
    shell_print(" ms)\n");
}

static void shell_reboot_cmd(const char* args) {
    (void)args;
    shell_print("Rebooting...\n");
    __asm__ volatile("cli");
    while (inb(0x64) & 0x02) {
        // Wait for controller ready
    }
    outb(0x64, 0xFE);
    while (1) {
        __asm__ volatile("hlt");
    }
}

static void shell_history_cmd(const char* args) {
    (void)args;
    if (history_count == 0) {
        shell_print("No history yet.\n");
        return;
    }

    int start = history_count - 1;
    for (int i = start; i >= 0; i--) {
        const char* entry = history_get_from_newest(start - i);
        shell_print_int((uint32_t)(history_count - i));
        shell_print(": ");
        shell_print(entry ? entry : "");
        shell_print("\n");
    }
}

static void shell_ls(const char* args) {
    char path[VFS_MAX_PATH];
    shell_resolve_path(args, path);

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        shell_print("ls: cannot open ");
        shell_print(path);
        shell_print("\n");
        return;
    }

    vfs_dirent_t ent;
    int count = 0;
    while (vfs_readdir(fd, &ent) > 0) {
        if (ent.type == VFS_TYPE_DIR) {
            shell_print("[DIR]  ");
        } else if (ent.type == VFS_TYPE_DEV) {
            shell_print("[DEV]  ");
        } else {
            shell_print("       ");
        }
        shell_print(ent.name);
        if (ent.type == VFS_TYPE_FILE) {
            shell_print("  ");
            shell_print_int(ent.size);
            shell_print(" bytes");
        }
        shell_print("\n");
        count++;
    }

    vfs_close(fd);

    if (count == 0) {
        shell_print("(empty directory)\n");
    }
}

static void shell_cat(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: cat <filename>\n");
        return;
    }

    char path[VFS_MAX_PATH];
    shell_resolve_path(args, path);

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        shell_print("cat: file not found: ");
        shell_print(args);
        shell_print("\n");
        return;
    }

    char buf[256];
    int r;
    uint32_t total = 0;
    while ((r = vfs_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < r; i++) {
            shell_putchar(buf[i]);
        }
        total += (uint32_t)r;
        if (total > 65536u) {
            shell_print("\n[cat: output truncated at 64KB]\n");
            break;
        }
    }
    shell_print("\n");
    vfs_close(fd);
}

static void shell_sync(const char* args) {
    (void)args;
    blockcache_sync();
    shell_print("Cache flushed to disk\n");
}

static void shell_cachestats(const char* args) {
    (void)args;
    blockcache_stats();
}

static void shell_lsdisk(const char* args) {
    (void)args;
    fat16_list_root();
}

static void shell_catdisk(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: catdisk <filename>\n");
        return;
    }

    fat16_file_t* file = fat16_open(args);
    if (!file) {
        shell_print("File not found: ");
        shell_print(args);
        shell_print("\n");
        return;
    }

    char buffer[512];
    int bytes_read;
    while ((bytes_read = fat16_read(file, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            shell_putchar(buffer[i]);
        }
    }

    fat16_close(file);
    shell_print("\n");
}

/* ══════════════════════════════════════════════════════════════════════
 *  Debugging & Memory Safety Shell Commands
 * ══════════════════════════════════════════════════════════════════════ */

/* ── simple hex-string-to-uint32 parser ──────────────────────────── */
static uint32_t parse_hex(const char* s) {
    uint32_t val = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s;
        uint32_t d;
        if (c >= '0' && c <= '9') d = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a') + 10;
        else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A') + 10;
        else break;
        val = (val << 4) | d;
        s++;
    }
    return val;
}

static uint32_t parse_dec(const char* s) {
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (uint32_t)(*s - '0');
        s++;
    }
    return val;
}

/* skip whitespace, return pointer to next token */
static const char* skip_ws(const char* s) {
    while (*s == ' ') s++;
    return s;
}

/* skip non-whitespace */
static const char* skip_nws(const char* s) {
    while (*s && *s != ' ') s++;
    return s;
}

/* ── memdump <addr> <len> ── */
static void shell_memdump(const char* args) {
    if (!args || !args[0]) {
        shell_print("Usage: memdump <hex_addr> <length>\n");
        return;
    }
    const char* p = skip_ws(args);
    uint32_t addr = parse_hex(p);
    p = skip_ws(skip_nws(p));
    uint32_t len = 64;
    if (*p) len = parse_dec(p);
    if (len > 512) len = 512;

    for (uint32_t i = 0; i < len; i += 16) {
        print_hex(addr + i);
        shell_print(": ");
        for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
            print_hex_byte(*((uint8_t*)(addr + i + j)));
            shell_putchar(' ');
        }
        shell_print(" ");
        for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
            uint8_t b = *((uint8_t*)(addr + i + j));
            shell_putchar((b >= 0x20 && b < 0x7F) ? (char)b : '.');
        }
        shell_print("\n");
    }
}

/* ── memstats ── */
static void shell_memstats(const char* args) {
    (void)args;
    print_memory_stats();
}

/* ── memleak [threshold_sec] ── */
static void shell_memleak(const char* args) {
    uint32_t threshold = 60000; /* default 60s */
    if (args && args[0]) {
        threshold = parse_dec(args) * 1000;
        if (threshold == 0) threshold = 60000;
    }
    detect_memory_leaks(threshold);
}

/* ── memcheck ── */
static void shell_memcheck(const char* args) {
    (void)args;
    shell_print("Checking heap integrity...\n");
    heap_check_integrity();
    shell_print("Heap integrity OK\n");
}

/* ── stacktrace ── */
static void shell_stacktrace(const char* args) {
    (void)args;
    uint32_t ebp, eip;
    __asm__ volatile("movl %%ebp, %0" : "=r"(ebp));
    __asm__ volatile("call 1f\n1: popl %0" : "=r"(eip));
    print_stack_trace(ebp, eip);
}

/* ── registers ── */
static void shell_registers(const char* args) {
    (void)args;
    uint32_t eax_v, ebx_v, ecx_v, edx_v, esi_v, edi_v, ebp_v, esp_v, eflags_v;
    __asm__ volatile("movl %%eax, %0" : "=r"(eax_v));
    __asm__ volatile("movl %%ebx, %0" : "=r"(ebx_v));
    __asm__ volatile("movl %%ecx, %0" : "=r"(ecx_v));
    __asm__ volatile("movl %%edx, %0" : "=r"(edx_v));
    __asm__ volatile("movl %%esi, %0" : "=r"(esi_v));
    __asm__ volatile("movl %%edi, %0" : "=r"(edi_v));
    __asm__ volatile("movl %%ebp, %0" : "=r"(ebp_v));
    __asm__ volatile("movl %%esp, %0" : "=r"(esp_v));
    __asm__ volatile("pushfl; popl %0" : "=r"(eflags_v));

    shell_print("CPU Registers:\n");
    shell_print("  EAX: "); print_hex(eax_v);
    shell_print("  EBX: "); print_hex(ebx_v);
    shell_print("  ECX: "); print_hex(ecx_v);
    shell_print("  EDX: "); print_hex(edx_v);
    shell_print("\n");
    shell_print("  ESI: "); print_hex(esi_v);
    shell_print("  EDI: "); print_hex(edi_v);
    shell_print("  EBP: "); print_hex(ebp_v);
    shell_print("  ESP: "); print_hex(esp_v);
    shell_print("\n");
    shell_print("  EFLAGS: "); print_hex(eflags_v); print("\n");
}

/* ── sysinfo ── */
static void shell_sysinfo(const char* args) {
    (void)args;
    uint32_t ms = timer_get_uptime_ms();
    shell_print("System Information:\n");
    shell_print("  Uptime: "); shell_print_int(ms / 1000); shell_putchar('.');
    shell_print_int(ms % 1000); shell_print("s\n");
    shell_print("  CPU Freq: "); shell_print_int((uint32_t)(get_cpu_freq() / 1000000));
    shell_print(" MHz\n");
    shell_print("  Timer Freq: "); shell_print_int(timer_get_frequency()); shell_print(" Hz\n");

    uint32_t free_pg  = pmm_free_pages();
    uint32_t total_pg = pmm_total_pages();
    shell_print("  Memory: "); shell_print_int(free_pg * 4); shell_print(" KB free / ");
    shell_print_int(total_pg * 4); shell_print(" KB total\n");

    print_memory_stats();
}

/* Helper: write a decimal number into buf, return chars written */
static int cf_itoa(uint32_t val, char *buf, int bufsize) {
    if (bufsize <= 0) return 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[12];
    int i = 0;
    while (val > 0 && i < 11) {
        tmp[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    int j = 0;
    while (i > 0 && j < bufsize - 1) buf[j++] = tmp[--i];
    buf[j] = '\0';
    return j;
}

/* ══════════════════════════════════════════════════════════════════════
 *  date — show current date and time from the RTC
 * ══════════════════════════════════════════════════════════════════════ */
static void shell_date(const char* args) {
    rtc_date_t date;
    rtc_time_t time;
    rtc_read_date(&date);
    rtc_read_time(&time);

    if (args && args[0]) {
        /* date +epoch  — print seconds since Unix epoch */
        if (strcmp(args, "+epoch") == 0) {
            char ebuf[16];
            cf_itoa(rtc_get_epoch_seconds(), ebuf, 16);
            shell_print(ebuf);
            shell_putchar('\n');
            return;
        }
        /* date +short  — "Feb 6, 2026  6:32 PM" */
        if (strcmp(args, "+short") == 0) {
            char dbuf[20];
            char tbuf[20];
            format_date_short(&date, dbuf, 20);
            format_time_12hr(&time, tbuf, 20);
            shell_print(dbuf);
            shell_print("  ");
            shell_print(tbuf);
            shell_putchar('\n');
            return;
        }
    }

    /* Default: full date and time */
    char datebuf[48];
    char timebuf[20];
    format_date_full(&date, datebuf, 48);
    format_time_12hr_sec(&time, timebuf, 20);
    shell_print(datebuf);
    shell_print("  ");
    shell_print(timebuf);
    shell_putchar('\n');
}

/* ══════════════════════════════════════════════════════════════════════
 *  cupidfetch — neofetch-style system information display
 *
 *  Shows an ASCII art cat mascot alongside colored system details.
 * ══════════════════════════════════════════════════════════════════════ */

static void shell_cupidfetch(const char* args) {
    (void)args;

    /* ── ANSI color codes ─────────────────────────────────────── */
    const char *c_cat   = "\x1B[95m";   /* bright magenta */
    const char *c_hdr   = "\x1B[93m";   /* bright yellow  */
    const char *c_label = "\x1B[96m";   /* bright cyan    */
    const char *c_val   = "\x1B[97m";   /* bright white   */
    const char *c_rst   = "\x1B[0m";    /* reset          */

    /* ── Compact ASCII cat (fits in ~34 cols) ─────────────────── */
    /* Stacked layout: cat on top, info below.
     * GUI terminal is only ~38 cols wide (310px / 8px font). */
    shell_print(c_cat);
    shell_print("   /\\_/\\   \n");
    shell_print("  ( o.o )  ");
    shell_print(c_hdr);
    shell_print("cupid-os\n");
    shell_print(c_cat);
    shell_print("   > ^ <   ");
    shell_print(c_hdr);
    shell_print("-----------\n");
    shell_print(c_cat);
    shell_print("  /|   |\\  \n");
    shell_print(" (_|   |_) \n");
    shell_print(c_rst);

    /* ── Gather system information ─────────────────────────────── */
    uint32_t ms       = timer_get_uptime_ms();
    uint32_t cpu_mhz  = (uint32_t)(get_cpu_freq() / 1000000);
    uint32_t free_pg  = pmm_free_pages();
    uint32_t total_pg = pmm_total_pages();
    uint32_t free_kb  = free_pg * 4;
    uint32_t total_kb = total_pg * 4;
    uint32_t used_kb  = total_kb - free_kb;
    uint32_t mem_pct  = total_kb ? (used_kb * 100) / total_kb : 0;
    uint32_t procs    = process_get_count();
    int      mounts   = vfs_mount_count();
    int      gui_mode = (shell_get_output_mode() == SHELL_OUTPUT_GUI);

    /* Uptime components */
    uint32_t secs = ms / 1000;
    uint32_t mins = secs / 60;  secs %= 60;
    uint32_t hrs  = mins / 60;  mins %= 60;
    uint32_t days = hrs  / 24;  hrs  %= 24;
    (void)secs;

    /* Convert memory to MiB */
    uint32_t used_mib  = used_kb  / 1024;
    uint32_t total_mib = total_kb / 1024;

    char nbuf[16];

    /* ── Info lines (each ≤ 36 chars) ─────────────────────────── */

    /* OS */
    shell_print(c_label);  shell_print("OS: ");
    shell_print(c_val);    shell_print("cupid-os x86\n");

    /* Kernel */
    shell_print(c_label);  shell_print("Kernel: ");
    shell_print(c_val);    shell_print("1.0.0\n");

    /* Uptime */
    shell_print(c_label);  shell_print("Uptime: ");
    shell_print(c_val);
    if (days > 0) {
        cf_itoa(days, nbuf, 16);  shell_print(nbuf);
        shell_print("d ");
    }
    if (hrs > 0 || days > 0) {
        cf_itoa(hrs, nbuf, 16);   shell_print(nbuf);
        shell_print("h ");
    }
    cf_itoa(mins, nbuf, 16);     shell_print(nbuf);
    shell_print("m\n");

    /* Shell */
    shell_print(c_label);  shell_print("Shell: ");
    shell_print(c_val);    shell_print("cupid shell\n");

    /* Display */
    shell_print(c_label);  shell_print("Display: ");
    shell_print(c_val);
    if (gui_mode) {
        shell_print("320x200 256c\n");
    } else {
        shell_print("80x25 16c\n");
    }

    /* Terminal */
    shell_print(c_label);  shell_print("Term: ");
    shell_print(c_val);
    shell_print(gui_mode ? "GUI\n" : "VGA Text\n");

    /* CPU */
    shell_print(c_label);  shell_print("CPU: ");
    shell_print(c_val);    shell_print("x86 @ ");
    cf_itoa(cpu_mhz, nbuf, 16); shell_print(nbuf);
    shell_print(" MHz\n");

    /* Memory */
    shell_print(c_label);  shell_print("Mem: ");
    shell_print(c_val);
    cf_itoa(used_mib, nbuf, 16);  shell_print(nbuf);
    shell_print("/");
    cf_itoa(total_mib, nbuf, 16); shell_print(nbuf);
    shell_print(" MiB (");
    cf_itoa(mem_pct, nbuf, 16);   shell_print(nbuf);
    shell_print("%)\n");

    /* Processes */
    shell_print(c_label);  shell_print("Procs: ");
    shell_print(c_val);
    cf_itoa(procs, nbuf, 16);    shell_print(nbuf);
    shell_print(" running\n");

    /* Mounts */
    shell_print(c_label);  shell_print("Mounts: ");
    shell_print(c_val);
    cf_itoa((uint32_t)mounts, nbuf, 16); shell_print(nbuf);
    shell_print(" fs\n");

    /* Date */
    {
        rtc_date_t cfdate;
        rtc_time_t cftime;
        rtc_read_date(&cfdate);
        rtc_read_time(&cftime);
        char datebuf[40];
        char timebuf[20];
        format_date_full(&cfdate, datebuf, 40);
        format_time_12hr(&cftime, timebuf, 20);

        shell_print(c_label);  shell_print("Date: ");
        shell_print(c_val);    shell_print(datebuf);
        shell_putchar('\n');

        shell_print(c_label);  shell_print("Time: ");
        shell_print(c_val);    shell_print(timebuf);
        shell_putchar('\n');
    }

    /* ── Color palette bars ───────────────────────────────────── */
    /* Standard colors (2-char blocks to fit) */
    for (int c = 0; c < 8; c++) {
        char esc[8];
        esc[0] = '\x1B'; esc[1] = '[';
        esc[2] = '4'; esc[3] = (char)('0' + c);
        esc[4] = 'm'; esc[5] = '\0';
        shell_print(esc);
        shell_print("    ");
    }
    shell_print(c_rst);
    shell_putchar('\n');

    for (int c = 0; c < 8; c++) {
        char esc[12];
        esc[0] = '\x1B'; esc[1] = '[';
        esc[2] = '1'; esc[3] = '0';
        esc[4] = (char)('0' + c); esc[5] = 'm';
        esc[6] = '\0';
        shell_print(esc);
        shell_print("    ");
    }
    shell_print(c_rst);
    shell_putchar('\n');
}

/* ── loglevel [debug|info|warn|error|panic] ── */
static void shell_loglevel(const char* args) {
    if (!args || !args[0]) {
        shell_print("Current log level: ");
        shell_print(get_log_level_name());
        shell_print("\nUsage: loglevel <debug|info|warn|error|panic>\n");
        return;
    }
    if (strcmp(args, "debug") == 0)      set_log_level(LOG_DEBUG);
    else if (strcmp(args, "info") == 0)  set_log_level(LOG_INFO);
    else if (strcmp(args, "warn") == 0)  set_log_level(LOG_WARN);
    else if (strcmp(args, "error") == 0) set_log_level(LOG_ERROR);
    else if (strcmp(args, "panic") == 0) set_log_level(LOG_PANIC);
    else {
        shell_print("Unknown level: ");
        shell_print(args);
        shell_print("\n");
        return;
    }
    shell_print("Log level set to ");
    shell_print(get_log_level_name());
    shell_print("\n");
    KINFO("Log level changed to %s", get_log_level_name());
}

/* ── logdump ── */
static void shell_logdump(const char* args) {
    (void)args;
    shell_print("=== Recent Log Entries ===\n");
    print_log_buffer();
}

/* ── crashtest <type> ── */
static void shell_crashtest(const char* args) {
    if (!args || !args[0]) {
        shell_print("Usage: crashtest <type>\n");
        shell_print("  Types: panic, nullptr, divzero, assert, overflow, stackoverflow\n");
        return;
    }
    if (strcmp(args, "panic") == 0) {
        kernel_panic("Test panic from shell");
    } else if (strcmp(args, "nullptr") == 0) {
        shell_print("Dereferencing NULL pointer...\n");
        volatile int* p = (volatile int*)0;
        (void)*p;
    } else if (strcmp(args, "divzero") == 0) {
        shell_print("Dividing by zero...\n");
        volatile int a = 1;
        volatile int b = 0;
        volatile int c = a / b;
        (void)c;
    } else if (strcmp(args, "assert") == 0) {
        ASSERT_MSG(1 == 2, "deliberate assertion failure from crashtest");
    } else if (strcmp(args, "overflow") == 0) {
        shell_print("Allocating and overflowing buffer...\n");
        char* buf = kmalloc(16);
        if (buf) {
            memset(buf, 'A', 32);   /* overflow by 16 bytes */
            kfree(buf);             /* should detect destroyed canary */
        }
    } else if (strcmp(args, "stackoverflow") == 0) {
        shell_print("Triggering stack overflow...\n");
        volatile char big[65536];
        big[0] = 'x';
        big[65535] = 'y';
        (void)big;
    } else {
        shell_print("Unknown crash test: ");
        shell_print(args);
        shell_print("\n");
    }
}

/* ── ed line editor ── */
static void shell_ed(const char* args) {
    ed_run(args);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Process management shell commands
 * ══════════════════════════════════════════════════════════════════════ */

/* ps - list all processes */
static void shell_ps(const char* args) {
    (void)args;
    process_list();
}

/* kill <pid> - terminate a process */
static void shell_kill_cmd(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: kill <pid>\n");
        return;
    }

    /* Parse PID from args */
    uint32_t pid = 0;
    const char *p = args;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (uint32_t)(*p - '0');
        p++;
    }

    if (pid == 0) {
        shell_print("Invalid PID\n");
        return;
    }
    if (pid == 1) {
        shell_print("Cannot kill idle process (PID 1)\n");
        return;
    }

    shell_print("Killing PID ");
    shell_print_int(pid);
    shell_print("...\n");
    process_kill(pid);
}

/* ── Test process for spawn command ── */
static void test_counting_process(void) {
    uint32_t pid = process_get_current_pid();
    for (int i = 0; i < 10; i++) {
        serial_printf("[PROCESS] PID %u count %d\n", pid, i);
        process_yield();
    }
    /* process_exit called automatically via trampoline */
}

/* spawn [n] - create test processes (default 1) */
static void shell_spawn(const char* args) {
    uint32_t count = 1;

    if (args && args[0] >= '1' && args[0] <= '9') {
        count = 0;
        const char *p = args;
        while (*p >= '0' && *p <= '9') {
            count = count * 10 + (uint32_t)(*p - '0');
            p++;
        }
    }

    if (count > 16) count = 16;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t pid = process_create(test_counting_process, "test", DEFAULT_STACK_SIZE);
        if (pid == 0) {
            shell_print("Failed to create process\n");
            break;
        }
        shell_print("Spawned PID ");
        shell_print_int(pid);
        shell_print("\n");
    }
}

/* yield - voluntarily give up CPU */
static void shell_yield_cmd(const char* args) {
    (void)args;
    shell_print("Yielding CPU...\n");
    process_yield();
}

/* ── cupid command: run a CupidScript file ── */
static void shell_cupid(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: cupid <script.cup> [args...]\n");
        return;
    }

    /* Split filename from arguments */
    char filename[MAX_INPUT_LEN];
    const char *script_args = NULL;
    int i = 0;
    while (args[i] && args[i] != ' ' && i < MAX_INPUT_LEN - 1) {
        filename[i] = args[i];
        i++;
    }
    filename[i] = '\0';
    if (args[i] == ' ') {
        script_args = &args[i + 1];
    }

    /* Set up CupidScript output routing */
    if (output_mode == SHELL_OUTPUT_GUI) {
        cupidscript_set_output(shell_gui_print, shell_gui_putchar,
                               shell_gui_print_int);
    } else {
        cupidscript_set_output(shell_print, shell_putchar,
                               shell_print_int);
    }

    cupidscript_run_file(filename, script_args);
}

/* ══════════════════════════════════════════════════════════════════════
 *  VFS Shell Commands
 * ══════════════════════════════════════════════════════════════════════ */

static void shell_cd(const char* args) {
    char path[VFS_MAX_PATH];
    shell_resolve_path(args, path);

    /* Verify the target is a directory */
    vfs_stat_t st;
    int r = vfs_stat(path, &st);
    if (r < 0) {
        shell_print("cd: no such directory: ");
        shell_print(args ? args : "");
        shell_print("\n");
        return;
    }
    if (st.type != VFS_TYPE_DIR) {
        shell_print("cd: not a directory: ");
        shell_print(args ? args : "");
        shell_print("\n");
        return;
    }

    /* Update CWD */
    int i = 0;
    while (path[i] && i < VFS_MAX_PATH - 1) {
        shell_cwd[i] = path[i];
        i++;
    }
    shell_cwd[i] = '\0';
}

static void shell_pwd(const char* args) {
    (void)args;
    shell_print(shell_cwd);
    shell_print("\n");
}

static void shell_vls(const char* args) {
    char path[VFS_MAX_PATH];
    shell_resolve_path(args, path);

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        shell_print("vls: cannot open ");
        shell_print(path);
        shell_print("\n");
        return;
    }

    vfs_dirent_t ent;
    int count = 0;
    while (vfs_readdir(fd, &ent) > 0) {
        if (ent.type == VFS_TYPE_DIR) {
            shell_print("[DIR]  ");
        } else if (ent.type == VFS_TYPE_DEV) {
            shell_print("[DEV]  ");
        } else {
            shell_print("       ");
        }
        shell_print(ent.name);
        if (ent.type == VFS_TYPE_FILE) {
            shell_print("  ");
            shell_print_int(ent.size);
            shell_print(" bytes");
        }
        shell_print("\n");
        count++;
    }

    vfs_close(fd);

    if (count == 0) {
        shell_print("(empty directory)\n");
    }
}

static void shell_vcat(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: vcat <path>\n");
        return;
    }

    char rpath[VFS_MAX_PATH];
    shell_resolve_path(args, rpath);
    int fd = vfs_open(rpath, O_RDONLY);
    if (fd < 0) {
        shell_print("vcat: cannot open ");
        shell_print(args);
        shell_print("\n");
        return;
    }

    char buf[256];
    int r;
    uint32_t total = 0;
    while ((r = vfs_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < r; i++) {
            shell_putchar(buf[i]);
        }
        total += (uint32_t)r;
        if (total > 65536u) {
            shell_print("\n[vcat: output truncated at 64KB]\n");
            break;
        }
    }
    shell_print("\n");
    vfs_close(fd);
}

static void shell_vmount(const char* args) {
    (void)args;
    int count = vfs_mount_count();
    if (count == 0) {
        shell_print("No filesystems mounted.\n");
        return;
    }
    for (int i = 0; i < count; i++) {
        const vfs_mount_t *m = vfs_get_mount(i);
        if (m && m->mounted) {
            shell_print(m->ops->name);
            shell_print(" on ");
            shell_print(m->path);
            shell_print("\n");
        }
    }
}

static void shell_vstat(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: vstat <path>\n");
        return;
    }

    char rpath[VFS_MAX_PATH];
    shell_resolve_path(args, rpath);
    vfs_stat_t st;
    int r = vfs_stat(rpath, &st);
    if (r < 0) {
        shell_print("vstat: not found: ");
        shell_print(args);
        shell_print("\n");
        return;
    }

    shell_print("Path: ");
    shell_print(args);
    shell_print("\nType: ");
    if (st.type == VFS_TYPE_DIR) shell_print("directory");
    else if (st.type == VFS_TYPE_DEV) shell_print("device");
    else shell_print("file");
    shell_print("\nSize: ");
    shell_print_int(st.size);
    shell_print(" bytes\n");
}

static void shell_vmkdir(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: vmkdir <path>\n");
        return;
    }

    char rpath[VFS_MAX_PATH];
    shell_resolve_path(args, rpath);
    int r = vfs_mkdir(rpath);
    if (r < 0) {
        shell_print("vmkdir: failed to create ");
        shell_print(args);
        shell_print("\n");
    }
}

static void shell_vrm(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: vrm <path>\n");
        return;
    }

    char rpath[VFS_MAX_PATH];
    shell_resolve_path(args, rpath);
    int r = vfs_unlink(rpath);
    if (r < 0) {
        shell_print("vrm: failed to remove ");
        shell_print(args);
        shell_print("\n");
    }
}

static void shell_vwrite(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: vwrite <path> <text>\n");
        return;
    }

    /* Split path from content */
    char rawpath[VFS_MAX_PATH];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < VFS_MAX_PATH - 1) {
        rawpath[i] = args[i];
        i++;
    }
    rawpath[i] = '\0';

    const char *text = "";
    if (args[i] == ' ') {
        text = &args[i + 1];
    }

    char path[VFS_MAX_PATH];
    shell_resolve_path(rawpath, path);
    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        shell_print("vwrite: cannot open ");
        shell_print(path);
        shell_print("\n");
        return;
    }

    uint32_t len = (uint32_t)strlen(text);
    int r = vfs_write(fd, text, len);
    vfs_close(fd);

    if (r < 0) {
        shell_print("vwrite: write failed\n");
    } else {
        shell_print("Wrote ");
        shell_print_int((uint32_t)r);
        shell_print(" bytes to ");
        shell_print(path);
        shell_print("\n");
    }
}

/* ── try_bin_dispatch: check if a resolved path is /bin/<app> and run it ──
 * Returns true if handled, false if not a /bin path. */
static bool try_bin_dispatch(const char *resolved, const char *extra_args) {
    if (resolved[0] != '/' || resolved[1] != 'b' ||
        resolved[2] != 'i' || resolved[3] != 'n' ||
        resolved[4] != '/') {
        return false;
    }

    const char *app = resolved + 5;
    serial_printf("[try_bin_dispatch] resolved='%s' app='%s'\n", resolved, app);
    if (strcmp(app, "terminal") == 0) {
        terminal_launch();
    } else if (strcmp(app, "notepad") == 0) {
        notepad_launch();
    } else if (strcmp(app, "ed") == 0) {
        shell_ed(extra_args);
    } else if (strcmp(app, "cupid") == 0) {
        shell_cupid(extra_args);
    } else if (strcmp(app, "shell") == 0) {
        shell_print("Shell is already running.\n");
    } else {
        /* Try it as a regular command name */
        for (int j = 0; commands[j].name; j++) {
            if (strcmp(app, commands[j].name) == 0) {
                commands[j].func(extra_args);
                return true;
            }
        }
        /* Try as CUPD binary on disk */
        int r = exec(resolved, app);
        if (r >= 0) {
            shell_print("Started process PID ");
            shell_print_int((uint32_t)r);
            shell_print("\n");
        } else {
            shell_print("Unknown binary: /bin/");
            shell_print(app);
            shell_print("\n");
        }
    }
    return true;
}

static void shell_exec_cmd(const char* args) {
    if (!args || args[0] == '\0') {
        shell_print("Usage: exec <path>\n");
        return;
    }

    char rpath[VFS_MAX_PATH];
    shell_resolve_path(args, rpath);
    serial_printf("[shell_exec_cmd] args='%s' rpath='%s'\n", args, rpath);

    /* Check for /bin/ built-in dispatch first */
    if (try_bin_dispatch(rpath, NULL)) return;

    int r = exec(rpath, args);
    if (r < 0) {
        shell_print("exec: failed to load ");
        shell_print(args);
        shell_print("\n");
    } else {
        shell_print("Started process PID ");
        shell_print_int((uint32_t)r);
        shell_print("\n");
    }
}

static void shell_notepad_cmd(const char* args) {
    (void)args;
    notepad_launch();
}

static void shell_terminal_cmd(const char* args) {
    (void)args;
    terminal_launch();
}

/* ── helper: check if string ends with suffix ── */
static int shell_ends_with(const char *str, const char *suffix) {
    int slen = 0, xlen = 0;
    while (str[slen]) slen++;
    while (suffix[xlen]) xlen++;
    if (xlen > slen) return 0;
    return strcmp(str + slen - xlen, suffix) == 0;
}

/* ── shell_execute_line: public interface for CupidScript ── */
void shell_execute_line(const char *line) {
    if (!line || line[0] == '\0') return;
    execute_command(line);
}

// Find and execute a command
static void execute_command(const char* input) {
    // Skip empty input
    if (!input || input[0] == '\0') {
        return;
    }

    /* ── Check for output redirection (> or >>) ─────────────────── */
    char clean_input[MAX_INPUT_LEN + 1];
    char redir_file[VFS_MAX_PATH];
    bool do_redir   = false;
    bool do_append  = false;
    redir_file[0] = '\0';

    {
        int len = 0;
        while (input[len]) len++;

        /* Scan for unquoted > */
        bool in_squote = false;
        bool in_dquote = false;
        int rpos = -1;
        for (int k = 0; k < len; k++) {
            if (input[k] == '\'' && !in_dquote) in_squote = !in_squote;
            else if (input[k] == '"' && !in_squote) in_dquote = !in_dquote;
            else if (input[k] == '>' && !in_squote && !in_dquote) {
                rpos = k;
                break;
            }
        }

        if (rpos >= 0) {
            do_redir = true;
            int fstart = rpos + 1;
            if (fstart < len && input[fstart] == '>') {
                do_append = true;
                fstart++;
            }
            /* Skip spaces after > / >> */
            while (fstart < len && input[fstart] == ' ') fstart++;

            /* Extract filename */
            int fi = 0;
            while (fstart < len && input[fstart] != ' ' &&
                   fi < VFS_MAX_PATH - 1) {
                redir_file[fi++] = input[fstart++];
            }
            redir_file[fi] = '\0';

            /* Copy everything before > into clean_input, trim trailing space */
            int ci = 0;
            for (int k = 0; k < rpos && k < MAX_INPUT_LEN; k++) {
                clean_input[ci++] = input[k];
            }
            while (ci > 0 && clean_input[ci - 1] == ' ') ci--;
            clean_input[ci] = '\0';

            input = clean_input;
        }
    }

    /* If redirecting, set up the capture buffer */
    if (do_redir && redir_file[0]) {
        redir_buf = kmalloc(REDIR_BUF_SIZE);
        if (redir_buf) {
            redir_len = 0;
            redir_active = true;
        } else {
            shell_print("shell: out of memory for redirect\n");
            do_redir = false;
        }
    }

    char cmd[MAX_INPUT_LEN];
    const char* args = 0;
    
    // Split input into command and arguments
    int i = 0;
    while (input[i] && input[i] != ' ') {
        cmd[i] = input[i];
        i++;
    }
    cmd[i] = 0;
    
    if (input[i] == ' ') {
        args = &input[i+1];
    }

    // Find and execute the command
    for (int j = 0; commands[j].name; j++) {
        if (strcmp(cmd, commands[j].name) == 0) {
            commands[j].func(args);
            goto redir_done;
        }
    }

    /* Handle /bin/<app> execution — resolve the path first */
    {
        char resolved[VFS_MAX_PATH];
        shell_resolve_path(cmd, resolved);
        if (try_bin_dispatch(resolved, args)) goto redir_done;
    }

    /* Handle ./script.cup execution */
    if (cmd[0] == '.' && cmd[1] == '/') {
        const char *script_path = cmd + 2;
        if (shell_ends_with(script_path, ".cup")) {
            shell_cupid(args ? input + 2 : script_path);
            goto redir_done;
        }
    }

    /* Handle bare .cup files: script.cup → cupid script.cup */
    if (shell_ends_with(cmd, ".cup")) {
        shell_cupid(input);
        goto redir_done;
    }

    // Handle unknown command
    shell_print("Unknown command: ");
    shell_print(cmd);
    shell_print("\n");

redir_done:
    /* ── Flush redirect buffer to file if active ─────────────────── */
    if (redir_active && redir_buf) {
        redir_active = false;
        redir_buf[redir_len] = '\0';

        /* Resolve the redirect file path */
        char rpath[VFS_MAX_PATH];
        shell_resolve_path(redir_file, rpath);

        uint32_t flags = O_WRONLY | O_CREAT;
        flags |= do_append ? O_APPEND : O_TRUNC;
        int fd = vfs_open(rpath, flags);
        if (fd < 0) {
            /* Restore normal output before printing error */
            kfree(redir_buf);
            redir_buf = NULL;
            redir_len = 0;
            shell_print("shell: cannot open ");
            shell_print(rpath);
            shell_print("\n");
            return;
        }

        if (redir_len > 0) {
            vfs_write(fd, redir_buf, (uint32_t)redir_len);
        }
        vfs_close(fd);

        kfree(redir_buf);
        redir_buf = NULL;
        redir_len = 0;
    }
}

// Main shell loop
void shell_run(void) {
    char input[MAX_INPUT_LEN + 1];
    int pos = 0;
    
    shell_print("cupid-os shell\n");
    shell_print(shell_cwd);
    shell_print("> ");
    
    while (1) {
        key_event_t event;
        keyboard_read_event(&event);
        char c = event.character;

        // History navigation with up/down arrows
        if (event.character == 0 && event.scancode == SCANCODE_ARROW_UP) {
            if (history_count > 0 && history_view < history_count - 1) {
                history_view++;
                const char* entry = history_get_from_newest(history_view);
                replace_input(entry, input, &pos);
            }
            continue;
        } else if (event.character == 0 && event.scancode == SCANCODE_ARROW_DOWN) {
            if (history_count > 0) {
                if (history_view > 0) {
                    history_view--;
                    const char* entry = history_get_from_newest(history_view);
                    replace_input(entry, input, &pos);
                } else if (history_view == 0) {
                    history_view = -1;
                    replace_input("", input, &pos);
                }
            }
            continue;
        }

        if (c == '\t') {
            tab_complete(input, &pos);
            continue;
        }

        if (c == '\n') {
            input[pos] = 0;  // Ensure null termination
            shell_putchar('\n');
            history_record(input);
            execute_command(input);
            pos = 0;
            history_view = -1;
            memset(input, 0, sizeof(input)); // Clear buffer
            shell_print(shell_cwd);
            shell_print("> ");
        }
        else if (c == '\b') {
            if (pos > 0) {
                pos--;
                shell_print("\b \b");  // Erase from screen
                input[pos] = '\0'; // Remove from buffer
            }
            history_view = -1; // Stop browsing when editing
        }
        else if (c && pos < MAX_INPUT_LEN) {
            input[pos++] = c;
            shell_putchar(c);
            history_view = -1; // Stop browsing when typing
        }
    }
} 

/* ══════════════════════════════════════════════════════════════════════
 *  GUI-mode shell key handler
 *
 *  Called from terminal_app.c when a key is pressed while the
 *  terminal window is focused.  Mirrors the text-mode shell_run()
 *  logic but writes to the GUI buffer instead of VGA text memory.
 * ══════════════════════════════════════════════════════════════════════ */

static char gui_input[MAX_INPUT_LEN + 1];
static int  gui_input_pos = 0;
static int  gui_history_view = -1;

/* GUI-mode wrappers (print/putchar go to GUI buffer) */
static void gui_replace_input(const char *new_text) {
    while (gui_input_pos > 0) {
        shell_gui_putchar('\b');
        gui_input_pos--;
    }
    int i = 0;
    if (new_text) {
        while (new_text[i] && i < MAX_INPUT_LEN) {
            gui_input[i] = new_text[i];
            shell_gui_putchar(new_text[i]);
            i++;
        }
    }
    gui_input[i] = '\0';
    gui_input_pos = i;
}

/* Temporarily redirect print/putchar to GUI buffer, execute command,
 * then restore.  We achieve this by using a wrapper approach:
 * shell commands call print()/putchar() which go to VGA text.
 * In GUI mode we need them to go to the GUI buffer.
 * The simplest approach: hook print/putchar via function pointers. */

/* Global function pointers for output redirection */
static void (*shell_print_fn)(const char *) = NULL;
static void (*shell_putchar_fn)(char) = NULL;
static void (*shell_print_int_fn)(uint32_t) = NULL;

static void gui_exec_command(const char *input) {
    /* Temporarily redirect kernel print/putchar to GUI buffer.
     * Since commands call print() and putchar() from kernel.c,
     * we can't easily redirect those.  Instead, we use a simpler
     * approach: the GUI buffer captures the output by hooking
     * into the output path.
     *
     * For now, we just execute the command normally.  The output
     * will go to both VGA text (invisible since we're in Mode 13h)
     * and the serial port.  To capture it in the GUI buffer, we
     * intercept at a higher level by buffering the output ourselves.
     *
     * Simple solution: copy what print/putchar write into our buffer
     * by calling shell_gui_print/shell_gui_putchar for each character
     * that would be printed.  We do this by overriding print temporarily.
     */

    /* Since commands use print() from kernel.c and we can't easily
     * redirect that, we handle only built-in shell commands that we
     * control.  For a complete solution, we'd need to make print()
     * check the output mode, but that changes kernel.c's interface.
     *
     * Pragmatic approach: execute_command calls print()/putchar() which
     * still write to 0xB8000 (harmless in Mode 13h - it's not visible).
     * We also echo the output to the GUI buffer by temporarily swapping
     * the VGA write functions. */

    /* Execute through the existing command processor */
    execute_command(input);
}

void shell_gui_handle_key(uint8_t scancode, char character) {
    if (output_mode != SHELL_OUTPUT_GUI) return;

    /* History navigation */
    if (character == 0 && scancode == SCANCODE_ARROW_UP) {
        if (history_count > 0 && gui_history_view < history_count - 1) {
            gui_history_view++;
            const char *entry = history_get_from_newest(gui_history_view);
            gui_replace_input(entry);
        }
        return;
    }
    if (character == 0 && scancode == SCANCODE_ARROW_DOWN) {
        if (history_count > 0) {
            if (gui_history_view > 0) {
                gui_history_view--;
                const char *entry = history_get_from_newest(gui_history_view);
                gui_replace_input(entry);
            } else if (gui_history_view == 0) {
                gui_history_view = -1;
                gui_replace_input("");
            }
        }
        return;
    }

    /* Tab completion */
    if (character == '\t') {
        tab_complete(gui_input, &gui_input_pos);
        gui_history_view = -1;
        return;
    }

    if (character == '\n') {
        gui_input[gui_input_pos] = '\0';
        shell_gui_putchar('\n');

        if (gui_input_pos > 0) {
            history_record(gui_input);
            /* Execute command - output goes to VGA text memory (hidden)
             * AND we mirror it by patching print/putchar temporarily */
            gui_exec_command(gui_input);
        }

        gui_input_pos = 0;
        gui_history_view = -1;
        memset(gui_input, 0, sizeof(gui_input));
        shell_gui_print(shell_cwd);
        shell_gui_print("> ");
    }
    else if (character == '\b') {
        if (gui_input_pos > 0) {
            gui_input_pos--;
            shell_gui_print("\b \b");
            gui_input[gui_input_pos] = '\0';
        }
        gui_history_view = -1;
    }
    else if (character && gui_input_pos < MAX_INPUT_LEN) {
        gui_input[gui_input_pos++] = character;
        shell_gui_putchar(character);
        gui_history_view = -1;
    }

    (void)shell_print_fn;
    (void)shell_putchar_fn;
    (void)shell_print_int_fn;
    (void)shell_gui_print_int;
}
