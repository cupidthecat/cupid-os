#include "shell.h"
#include "../drivers/rtc.h"
#include "../drivers/serial.h"
#include "assert.h"
#include "blockcache.h"
#include "calendar.h"
#include "cupidc.h"
#include "cupidscript.h"
#include "desktop.h"
#include "exec.h"
#include "fat16.h"
#include "fs.h"
#include "gfx2d.h"
#include "kernel.h"
#include "keyboard.h"
#include "math.h"
#include "memory.h"
#include "notepad.h"
#include "ports.h"
#include "process.h"
#include "string.h"
#include "terminal_ansi.h"
#include "terminal_app.h"
#include "timer.h"
#include "types.h"
#include "vfs.h"

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
static int gui_visible_cols = SHELL_COLS; /* actual visible column width */

/* ── I/O redirection capture buffer ─────────────────────────────── */
#define REDIR_BUF_SIZE 4096
static char *redir_buf = NULL; /* heap-allocated when active */
static int redir_len = 0;
static bool redir_active = false;

/* ── Current working directory ──────────────────────────────────── */
static char shell_cwd[VFS_MAX_PATH] = "/";

/* ── Program argument passing (TempleOS-style) ──────────────────── */
#define SHELL_ARGS_MAX 256
static char shell_program_args[SHELL_ARGS_MAX] = "";

/* ── JIT program input routing (for GUI mode) ───────────────────── */
#define JIT_INPUT_BUFFER_SIZE 256
static char jit_input_buffer[JIT_INPUT_BUFFER_SIZE];
static int jit_input_read_pos = 0;
static int jit_input_write_pos = 0;
static int jit_program_running = 0;
static int jit_program_interrupted = 0;
static int jit_program_killed = 0;
static char jit_program_name[64] = {0};

/* Stack for nested JIT programs (e.g. minimized app + user runs ps) */
#define JIT_STACK_MAX 4
static struct {
  char name[64];
  int  running;
  int  killed;
  void *saved_code;  /* kmalloc'd copy of CC_JIT_CODE_BASE region */
  void *saved_data;  /* kmalloc'd copy of CC_JIT_DATA_BASE region */
} jit_stack[JIT_STACK_MAX];
static int jit_stack_depth = 0;

void shell_set_program_args(const char *args) {
  int i = 0;
  if (args) {
    while (args[i] && i < SHELL_ARGS_MAX - 1) {
      shell_program_args[i] = args[i];
      i++;
    }
  }
  shell_program_args[i] = '\0';
}

const char *shell_get_program_args(void) { return shell_program_args; }

const char *shell_get_cwd(void) { return shell_cwd; }

void shell_set_cwd(const char *path) {
  int i = 0;
  while (path[i] && i < VFS_MAX_PATH - 1) {
    shell_cwd[i] = path[i];
    i++;
  }
  shell_cwd[i] = '\0';
}

/**
 * Resolve a possibly-relative path against the CWD.
 * Result written to `out` (must be VFS_MAX_PATH bytes).
 *   - Absolute paths (starting with '/') are copied as-is.
 *   - Relative paths are joined with CWD.
 *   - ".." and "." components are resolved.
 */
void shell_resolve_path(const char *input, char *out) {
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
  while (input[input_len])
    input_len++;
  while (input_len > 0 &&
         (input[input_len - 1] == ' ' || input[input_len - 1] == '\t'))
    input_len--;

  if (input_len == 0) {
    int i = 0;
    while (shell_cwd[i] && i < VFS_MAX_PATH - 1) {
      out[i] = shell_cwd[i];
      i++;
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
  out[oi++] = '/'; /* Always starts with / */

  const char *p = tmp;
  while (*p) {
    while (*p == '/')
      p++; /* skip slashes */
    if (*p == '\0')
      break;

    /* Find end of component */
    const char *end = p;
    while (*end && *end != '/')
      end++;
    int len = (int)(end - p);

    if (len == 1 && p[0] == '.') {
      /* "." => skip */
    } else if (len == 2 && p[0] == '.' && p[1] == '.') {
      /* ".." => go up one level */
      if (oi > 1) {
        oi--; /* remove trailing slash or char */
        while (oi > 1 && out[oi - 1] != '/')
          oi--;
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
  if (oi > 1 && out[oi - 1] == '/')
    oi--;
  out[oi] = '\0';
}

/* Forward declarations */
static void execute_command(const char *input);
static void shell_gui_putchar(char c);
static void shell_gui_print(const char *s);
static void shell_gui_print_int(uint32_t num);
static int shell_ends_with(const char *str, const char *suffix);

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
    memory_set_output(shell_gui_print, shell_gui_print_int);
    panic_set_output(shell_gui_print, shell_gui_putchar);
    blockcache_set_output(shell_gui_print, shell_gui_print_int);
  } else {
    /* Reset all subsystems to use kernel output */
    fat16_set_output(print, putchar, print_int);
    memory_set_output(print, print_int);
    panic_set_output(print, putchar);
    blockcache_set_output(print, print_int);
  }
}

shell_output_mode_t shell_get_output_mode(void) { return output_mode; }

void shell_set_visible_cols(int cols) {
  if (cols > 0 && cols <= SHELL_COLS) {
    gui_visible_cols = cols;
  }
}

const char *shell_get_buffer(void) { return gui_buffer; }

const shell_color_t *shell_get_color_buffer(void) { return gui_color_buffer; }

int shell_get_cursor_x(void) { return gui_cursor_x; }

int shell_get_cursor_y(void) { return gui_cursor_y; }

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
    /* Escape sequence in progress or color code processed — nothing to display
     */
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

  /* Trigger window redraw immediately */
  terminal_mark_dirty();
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
  if (num == 0) {
    shell_gui_putchar('0');
    return;
  }
  while (num > 0) {
    buf[i++] = (char)((num % 10) + (uint32_t)'0');
    num /= 10;
  }
  while (i > 0) {
    char c = buf[--i];
    shell_gui_putchar(c);
  }
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
    if (num == 0) {
      tmp[i++] = '0';
    } else {
      while (num > 0) {
        tmp[i++] = (char)('0' + (num % 10));
        num /= 10;
      }
    }
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
void shell_gui_putchar_ext(char c) { shell_gui_putchar(c); }

void shell_gui_print_ext(const char *s) { shell_gui_print(s); }

void shell_gui_print_int_ext(uint32_t num) { shell_gui_print_int(num); }

// Scancodes for extended keys we care about
#define SCANCODE_ARROW_UP 0x48
#define SCANCODE_ARROW_DOWN 0x50
#define SCANCODE_ARROW_LEFT 0x4B
#define SCANCODE_ARROW_RIGHT 0x4D

struct shell_command {
  const char *name;
  const char *description;
  void (*func)(const char *);
};

// Forward declarations for commands
static void shell_cupid(const char *args);
static void shell_exec_cmd(const char *args);
static void shell_notepad_cmd(const char *args);
static void shell_terminal_cmd(const char *args);
static void shell_cupidc_cmd(const char *args);
static void shell_ccc_cmd(const char *args);

// List of supported commands
static struct shell_command commands[] = {
    {"cupid", "Run a CupidScript (.cup) file", shell_cupid},
    {"exec", "Run a binary (ELF or CUPD)", shell_exec_cmd},
    {"notepad", "Open Notepad", shell_notepad_cmd},
    {"terminal", "Open a Terminal window", shell_terminal_cmd},
    {"cupidc", "Compile and run CupidC (.cc) file", shell_cupidc_cmd},
    {"ccc", "Compile CupidC to ELF binary", shell_ccc_cmd},
    {0, 0, 0} // Null terminator
};

static char history[HISTORY_SIZE][MAX_INPUT_LEN + 1];
static int history_count = 0;
static int history_next = 0;  // Next insert slot
static int history_view = -1; // -1 when not browsing history

// Minimal strncmp to avoid touching global string utils
static int shell_strncmp(const char *s1, const char *s2, size_t n) {
  while (n > 0 && *s1 && (*s1 == *s2)) {
    s1++;
    s2++;
    n--;
  }
  if (n == 0)
    return 0;
  return (unsigned char)*s1 - (unsigned char)*s2;
}

static void history_record(const char *line) {
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
static const char *history_get_from_newest(int offset) {
  if (offset < 0 || offset >= history_count) {
    return NULL;
  }

  int idx = history_next - 1 - offset;
  while (idx < 0) {
    idx += HISTORY_SIZE;
  }
  return history[idx];
}

int shell_get_history_count(void) { return history_count; }

const char *shell_get_history_entry(int index) {
  return history_get_from_newest(index);
}

static void replace_input(const char *new_text, char *input, int *pos, int *cursor) {
  // Erase current buffer from screen: move cursor to end, then erase all
  while (*cursor < *pos) {
    shell_putchar(input[*cursor]);
    (*cursor)++;
  }
  while (*pos > 0) {
    shell_print("\b \b");
    (*pos)--;
  }
  *cursor = 0;

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
  *cursor = i;
}

static void tab_complete(char *input, int *pos) {
/* ── Helper: re-display prompt + current input after listing matches ── */
#define REDRAW_PROMPT()                                                        \
  do {                                                                         \
    shell_print("\n");                                                         \
    shell_print(shell_cwd);                                                    \
    shell_print("> ");                                                         \
    for (int _k = 0; _k < *pos; _k++)                                          \
      shell_putchar(input[_k]);                                                \
  } while (0)

  /* Find the first space to determine if we're completing
     a command name or an argument. */
  int first_space = -1;
  for (int i = 0; i < *pos; i++) {
    if (input[i] == ' ') {
      first_space = i;
      break;
    }
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
    bool wants_file = strcmp(cmd, "cat") == 0 || strcmp(cmd, "cd") == 0 ||
                      strcmp(cmd, "ls") == 0 || strcmp(cmd, "exec") == 0 ||
                      strcmp(cmd, "ed") == 0 || strcmp(cmd, "cupid") == 0;

    if (!wants_file)
      return;

    /* Find the start of the last argument (after last space) */
    int arg_start = first_space + 1;
    for (int i = *pos - 1; i > first_space; i--) {
      if (input[i] == ' ') {
        arg_start = i + 1;
        break;
      }
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
      if (arg_prefix[i] == '/')
        last_slash = i;
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
    if (fd < 0)
      return;

    char first_match[VFS_MAX_NAME];
    first_match[0] = '\0';
    int match_count = 0;
    bool first_is_dir = false;
    vfs_dirent_t ent;

    while (vfs_readdir(fd, &ent) > 0) {
      if (shell_strncmp(ent.name, name_prefix, (size_t)name_prefix_len) == 0) {
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
          if (shell_strncmp(ent.name, name_prefix, (size_t)name_prefix_len) !=
              0)
            continue;
          if (first) {
            int n = 0;
            while (ent.name[n] && n < VFS_MAX_NAME - 1) {
              common[n] = ent.name[n];
              n++;
            }
            common[n] = '\0';
            common_len = n;
            first = false;
          } else {
            int n = 0;
            while (n < common_len && ent.name[n] && common[n] == ent.name[n])
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
          if (shell_strncmp(ent.name, name_prefix, (size_t)name_prefix_len) !=
              0)
            continue;
          shell_print(ent.name);
          if (ent.type == VFS_TYPE_DIR)
            shell_putchar('/');
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
   *  Matches against both built-in commands[] AND /bin/ programs
   * ──────────────────────────────────────────────────────────────────── */
  size_t prefix_len = (size_t)(*pos);
  char prefix[MAX_INPUT_LEN + 1];
  for (size_t i = 0; i < prefix_len; i++) {
    prefix[i] = input[i];
  }
  prefix[prefix_len] = '\0';

/* Collect all matching names into a small table.
 * 64 entries is plenty for our command set. */
#define TAB_MAX_MATCHES 64
#define TAB_MAX_NAME 64
  static char tab_matches[TAB_MAX_MATCHES][TAB_MAX_NAME];
  int match_count = 0;

  /* 1) Built-in commands */
  for (int i = 0; commands[i].name; i++) {
    if (shell_strncmp(commands[i].name, prefix, prefix_len) == 0) {
      if (match_count < TAB_MAX_MATCHES) {
        int n = 0;
        while (commands[i].name[n] && n < TAB_MAX_NAME - 1) {
          tab_matches[match_count][n] = commands[i].name[n];
          n++;
        }
        tab_matches[match_count][n] = '\0';
        match_count++;
      }
    }
  }

  /* 2) Programs in /bin/ — scan for .cc files and strip extension */
  {
    int bin_fd = vfs_open("/bin", O_RDONLY);
    if (bin_fd >= 0) {
      vfs_dirent_t ent;
      while (vfs_readdir(bin_fd, &ent) > 0 && match_count < TAB_MAX_MATCHES) {
        /* Only match .cc files */
        if (!shell_ends_with(ent.name, ".cc"))
          continue;

        /* Compute name without .cc extension */
        int nlen = 0;
        while (ent.name[nlen])
          nlen++;
        int base_len = nlen - 3; /* strip ".cc" */
        if (base_len <= 0 || base_len >= TAB_MAX_NAME)
          continue;

        char base_name[TAB_MAX_NAME];
        for (int b = 0; b < base_len; b++)
          base_name[b] = ent.name[b];
        base_name[base_len] = '\0';

        /* Check prefix match */
        if (shell_strncmp(base_name, prefix, prefix_len) != 0)
          continue;

        /* Avoid duplicates (if a built-in has the same name) */
        bool dup = false;
        for (int m = 0; m < match_count; m++) {
          if (strcmp(tab_matches[m], base_name) == 0) {
            dup = true;
            break;
          }
        }
        if (dup)
          continue;

        for (int b = 0; b < base_len; b++)
          tab_matches[match_count][b] = base_name[b];
        tab_matches[match_count][base_len] = '\0';
        match_count++;
      }
      vfs_close(bin_fd);
    }
  }

  if (match_count == 1) {
    const char *completion = tab_matches[0] + prefix_len;
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
    /* Compute longest common prefix among all matches */
    int common_len = TAB_MAX_NAME;
    for (int m = 1; m < match_count; m++) {
      int n = 0;
      while (n < common_len && tab_matches[0][n] && tab_matches[m][n] &&
             tab_matches[0][n] == tab_matches[m][n])
        n++;
      common_len = n;
    }

    /* Append common prefix beyond what's typed */
    if (common_len > (int)prefix_len) {
      for (int ci = (int)prefix_len; ci < common_len && *pos < MAX_INPUT_LEN;
           ci++) {
        input[*pos] = tab_matches[0][ci];
        shell_putchar(tab_matches[0][ci]);
        (*pos)++;
      }
      return;
    }

    /* List all matching commands */
    shell_print("\n");
    for (int m = 0; m < match_count; m++) {
      shell_print(tab_matches[m]);
      shell_print("  ");
    }
    REDRAW_PROMPT();
  }

#undef REDRAW_PROMPT
}

/* ── cupid command: run a CupidScript file ── */
static void shell_cupid(const char *args) {
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
    cupidscript_set_output(shell_print, shell_putchar, shell_print_int);
  }

  cupidscript_run_file(filename, script_args);
}

/* ── try_bin_dispatch: check if a resolved path is /bin/<app> and run it ──
 * Returns true if handled, false if not a /bin path. */
static bool try_bin_dispatch(const char *resolved, const char *extra_args) {
  if (resolved[0] != '/' || resolved[1] != 'b' || resolved[2] != 'i' ||
      resolved[3] != 'n' || resolved[4] != '/') {
    return false;
  }

  const char *app = resolved + 5;
  serial_printf("[try_bin_dispatch] resolved='%s' app='%s'\n", resolved, app);
  if (strcmp(app, "terminal") == 0) {
    terminal_launch();
  } else if (strcmp(app, "notepad") == 0) {
    notepad_launch();
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
    /* Check if it's a .cc source file — JIT compile it */
    if (shell_ends_with(resolved, ".cc")) {
      shell_set_program_args(extra_args ? extra_args : "");
      cupidc_jit(resolved);
      return true;
    }
    /* Try as CUPD/ELF binary */
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

static void shell_exec_cmd(const char *args) {
  if (!args || args[0] == '\0') {
    shell_print("Usage: exec <path> [args...]\n");
    return;
  }

  /* Split first word (program path) from remaining arguments */
  char prog[VFS_MAX_PATH];
  int pi = 0;
  int ai = 0;
  while (args[ai] == ' ')
    ai++; /* skip leading spaces */
  while (args[ai] && args[ai] != ' ' && pi < VFS_MAX_PATH - 1) {
    prog[pi++] = args[ai++];
  }
  prog[pi] = '\0';
  while (args[ai] == ' ')
    ai++;                            /* skip spaces before args */
  const char *prog_args = &args[ai]; /* remaining args (may be empty) */

  char rpath[VFS_MAX_PATH];
  shell_resolve_path(prog, rpath);
  serial_printf("[shell_exec_cmd] prog='%s' rpath='%s' args='%s'\n", prog,
                rpath, prog_args);

  /* Set program arguments before dispatch */
  shell_set_program_args(prog_args);

  /* Check for /bin/ built-in dispatch first */
  if (try_bin_dispatch(rpath, prog_args))
    return;

  /* Check if it's a .cc source file — JIT compile it */
  if (shell_ends_with(rpath, ".cc")) {
    cupidc_jit(rpath);
    return;
  }

  int r = exec(rpath, prog);
  if (r < 0) {
    shell_print("exec: failed to load ");
    shell_print(prog);
    shell_print("\n");
  } else {
    shell_print("Started process PID ");
    shell_print_int((uint32_t)r);
    shell_print("\n");
  }
}

static void shell_notepad_cmd(const char *args) {
  (void)args;
  notepad_launch();
}

static void shell_terminal_cmd(const char *args) {
  (void)args;
  terminal_launch();
}

/* ── helper: check if string ends with suffix ── */
static int shell_ends_with(const char *str, const char *suffix) {
  int slen = 0, xlen = 0;
  while (str[slen])
    slen++;
  while (suffix[xlen])
    xlen++;
  if (xlen > slen)
    return 0;
  return strcmp(str + slen - xlen, suffix) == 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  CupidC Compiler Commands
 * ══════════════════════════════════════════════════════════════════════ */

/* cupidc <file.cc> — JIT compile and run */
static void shell_cupidc_cmd(const char *args) {
  if (!args || args[0] == '\0') {
    shell_print("Usage: cupidc <file.cc>\n");
    shell_print("  Compile and run a CupidC source file\n");
    return;
  }
  char rpath[VFS_MAX_PATH];
  shell_resolve_path(args, rpath);
  cupidc_jit(rpath);
}

/* ccc <file.cc> -o <output> — AOT compile to ELF binary */
static void shell_ccc_cmd(const char *args) {
  if (!args || args[0] == '\0') {
    shell_print("Usage: ccc <file.cc> -o <output>\n");
    shell_print("  Compile CupidC source to ELF binary\n");
    return;
  }

  /* Parse: src_file -o out_file */
  char src[VFS_MAX_PATH];
  char out[VFS_MAX_PATH];
  int si = 0, ai = 0;

  /* Extract source file */
  while (args[ai] && args[ai] != ' ' && si < VFS_MAX_PATH - 1) {
    src[si++] = args[ai++];
  }
  src[si] = '\0';

  /* Skip whitespace */
  while (args[ai] == ' ')
    ai++;

  /* Check for -o flag */
  if (args[ai] == '-' && args[ai + 1] == 'o') {
    ai += 2;
    while (args[ai] == ' ')
      ai++;

    int oi = 0;
    while (args[ai] && args[ai] != ' ' && oi < VFS_MAX_PATH - 1) {
      out[oi++] = args[ai++];
    }
    out[oi] = '\0';
  } else {
    /* Default output name: replace .cc with nothing, or append .elf */
    int slen = 0;
    while (src[slen])
      slen++;

    int oi = 0;
    /* Copy up to .cc extension */
    if (slen > 3 && src[slen - 3] == '.' && src[slen - 2] == 'c' &&
        src[slen - 1] == 'c') {
      for (int k = 0; k < slen - 3 && oi < VFS_MAX_PATH - 1; k++) {
        out[oi++] = src[k];
      }
    } else {
      for (int k = 0; k < slen && oi < VFS_MAX_PATH - 1; k++) {
        out[oi++] = src[k];
      }
    }
    out[oi] = '\0';
  }

  /* Resolve paths */
  char rsrc[VFS_MAX_PATH];
  char rout[VFS_MAX_PATH];
  shell_resolve_path(src, rsrc);
  shell_resolve_path(out, rout);

  cupidc_aot(rsrc, rout);
}

/* ── shell_execute_line: public interface for CupidScript ── */
void shell_execute_line(const char *line) {
  if (!line || line[0] == '\0')
    return;
  execute_command(line);
}

// Find and execute a command
static void execute_command(const char *input) {
  // Skip empty input
  if (!input || input[0] == '\0') {
    return;
  }

  /* ── Check for output redirection (> or >>) ─────────────────── */
  char clean_input[MAX_INPUT_LEN + 1];
  char redir_file[VFS_MAX_PATH];
  bool do_redir = false;
  bool do_append = false;
  redir_file[0] = '\0';

  {
    int len = 0;
    while (input[len])
      len++;

    /* Scan for unquoted > */
    bool in_squote = false;
    bool in_dquote = false;
    int rpos = -1;
    for (int k = 0; k < len; k++) {
      if (input[k] == '\'' && !in_dquote)
        in_squote = !in_squote;
      else if (input[k] == '"' && !in_squote)
        in_dquote = !in_dquote;
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
      while (fstart < len && input[fstart] == ' ')
        fstart++;

      /* Extract filename */
      int fi = 0;
      while (fstart < len && input[fstart] != ' ' && fi < VFS_MAX_PATH - 1) {
        redir_file[fi++] = input[fstart++];
      }
      redir_file[fi] = '\0';

      /* Copy everything before > into clean_input, trim trailing space */
      int ci = 0;
      for (int k = 0; k < rpos && k < MAX_INPUT_LEN; k++) {
        clean_input[ci++] = input[k];
      }
      while (ci > 0 && clean_input[ci - 1] == ' ')
        ci--;
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
  const char *args = 0;

  // Split input into command and arguments
  int i = 0;
  while (input[i] && input[i] != ' ') {
    cmd[i] = input[i];
    i++;
  }
  cmd[i] = 0;

  if (input[i] == ' ') {
    args = &input[i + 1];
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
    if (try_bin_dispatch(resolved, args))
      goto redir_done;
  }

  /* ══════════════════════════════════════════════════════════════
   *  TempleOS-style auto-discovery: check /bin/ and /home/bin/
   *  for programs matching the command name.  This lets CupidC
   *  programs be added to the OS without recompiling the kernel.
   *
   *  Search order:
   *    1. /bin/<cmd>        — ELF/CUPD binary (ramfs)
   *    2. /bin/<cmd>.cc     — CupidC source   (ramfs)
   *    3. /home/bin/<cmd>   — ELF/CUPD binary (disk)
   *    4. /home/bin/<cmd>.cc — CupidC source  (disk, persistent)
   * ══════════════════════════════════════════════════════════════ */
  {
    char bin_path[VFS_MAX_PATH];
    vfs_stat_t bin_st;

    /* --- 1. /bin/<cmd> (ELF binary in ramfs) --- */
    {
      int bp = 0;
      const char *pfx = "/bin/";
      while (*pfx)
        bin_path[bp++] = *pfx++;
      int ci = 0;
      while (cmd[ci] && bp < VFS_MAX_PATH - 1)
        bin_path[bp++] = cmd[ci++];
      bin_path[bp] = '\0';
    }
    if (vfs_stat(bin_path, &bin_st) >= 0 && bin_st.type == VFS_TYPE_FILE) {
      /* Check if it's a .cc source (JIT) or binary (exec) */
      if (shell_ends_with(bin_path, ".cc")) {
        shell_set_program_args(args ? args : "");
        cupidc_jit(bin_path);
        goto redir_done;
      } else {
        shell_set_program_args(args ? args : "");
        int r = exec(bin_path, cmd);
        if (r >= 0) {
          shell_print("Started process PID ");
          shell_print_int((uint32_t)r);
          shell_print("\n");
          goto redir_done;
        }
        /* Not an ELF — might be a stub file, continue to try .cc version */
      }
    }

    /* --- 2. /bin/<cmd>.cc (CupidC source in ramfs) --- */
    {
      int bp = 0;
      const char *pfx = "/bin/";
      while (*pfx)
        bin_path[bp++] = *pfx++;
      int ci = 0;
      while (cmd[ci] && bp < VFS_MAX_PATH - 4)
        bin_path[bp++] = cmd[ci++];
      bin_path[bp++] = '.';
      bin_path[bp++] = 'c';
      bin_path[bp++] = 'c';
      bin_path[bp] = '\0';
    }
    if (vfs_stat(bin_path, &bin_st) >= 0 && bin_st.type == VFS_TYPE_FILE) {
      shell_set_program_args(args ? args : "");
      cupidc_jit(bin_path);
      goto redir_done;
    }

    /* --- 3. /home/bin/<cmd> (ELF binary on disk) --- */
    {
      int bp = 0;
      const char *pfx = "/home/bin/";
      while (*pfx)
        bin_path[bp++] = *pfx++;
      int ci = 0;
      while (cmd[ci] && bp < VFS_MAX_PATH - 1)
        bin_path[bp++] = cmd[ci++];
      bin_path[bp] = '\0';
    }
    if (vfs_stat(bin_path, &bin_st) >= 0 && bin_st.type == VFS_TYPE_FILE) {
      shell_set_program_args(args ? args : "");
      if (shell_ends_with(bin_path, ".cc")) {
        cupidc_jit(bin_path);
        goto redir_done;
      } else {
        int r = exec(bin_path, cmd);
        if (r >= 0) {
          shell_print("Started process PID ");
          shell_print_int((uint32_t)r);
          shell_print("\n");
          goto redir_done;
        }
        /* Not a valid binary, continue to try .cc version */
      }
    }

    /* --- 4. /home/bin/<cmd>.cc (CupidC source on disk) --- */
    {
      int bp = 0;
      const char *pfx = "/home/bin/";
      while (*pfx)
        bin_path[bp++] = *pfx++;
      int ci = 0;
      while (cmd[ci] && bp < VFS_MAX_PATH - 4)
        bin_path[bp++] = cmd[ci++];
      bin_path[bp++] = '.';
      bin_path[bp++] = 'c';
      bin_path[bp++] = 'c';
      bin_path[bp] = '\0';
    }
    if (vfs_stat(bin_path, &bin_st) >= 0 && bin_st.type == VFS_TYPE_FILE) {
      shell_set_program_args(args ? args : "");
      cupidc_jit(bin_path);
      goto redir_done;
    }
  }

  /* Handle ./program execution */
  if (cmd[0] == '.' && cmd[1] == '/') {
    char run_path[VFS_MAX_PATH];
    shell_resolve_path(cmd + 2, run_path);
    shell_set_program_args(args ? args : "");

    if (shell_ends_with(run_path, ".cup")) {
      shell_cupid(args ? input + 2 : cmd + 2);
      goto redir_done;
    }
    if (shell_ends_with(run_path, ".cc")) {
      cupidc_jit(run_path);
      goto redir_done;
    }
    /* Try as ELF/CUPD binary */
    vfs_stat_t dot_st;
    if (vfs_stat(run_path, &dot_st) >= 0 && dot_st.type == VFS_TYPE_FILE) {
      int r = exec(run_path, cmd + 2);
      if (r >= 0) {
        shell_print("Started process PID ");
        shell_print_int((uint32_t)r);
        shell_print("\n");
      } else {
        shell_print(run_path);
        shell_print(": exec failed\n");
      }
      goto redir_done;
    }
    shell_print(run_path);
    shell_print(": not found\n");
    goto redir_done;
  }

  /* Handle bare .cup files: script.cup → cupid script.cup */
  if (shell_ends_with(cmd, ".cup")) {
    shell_cupid(input);
    goto redir_done;
  }

  /* Handle bare .cc files: program.cc → cupidc program.cc */
  if (shell_ends_with(cmd, ".cc")) {
    char cc_path[VFS_MAX_PATH];
    shell_resolve_path(cmd, cc_path);
    cupidc_jit(cc_path);
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
  int pos = 0;    // length of input
  int cursor = 0; // cursor position within input

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
        const char *entry = history_get_from_newest(history_view);
        replace_input(entry, input, &pos, &cursor);
      }
      continue;
    } else if (event.character == 0 && event.scancode == SCANCODE_ARROW_DOWN) {
      if (history_count > 0) {
        if (history_view > 0) {
          history_view--;
          const char *entry = history_get_from_newest(history_view);
          replace_input(entry, input, &pos, &cursor);
        } else if (history_view == 0) {
          history_view = -1;
          replace_input("", input, &pos, &cursor);
        }
      }
      continue;
    }

    // Left arrow: move cursor left
    if (event.character == 0 && event.scancode == SCANCODE_ARROW_LEFT) {
      if (cursor > 0) {
        cursor--;
        shell_putchar('\b');
      }
      continue;
    }

    // Right arrow: move cursor right
    if (event.character == 0 && event.scancode == SCANCODE_ARROW_RIGHT) {
      if (cursor < pos) {
        shell_putchar(input[cursor]);
        cursor++;
      }
      continue;
    }

    if (c == '\t') {
      // Move cursor to end before tab completion
      while (cursor < pos) {
        shell_putchar(input[cursor]);
        cursor++;
      }
      tab_complete(input, &pos);
      cursor = pos;
      continue;
    }

    // Ctrl+C: cancel current line
    if (c == 3) {
      shell_print("^C\n");
      pos = 0;
      cursor = 0;
      memset(input, 0, sizeof(input));
      history_view = -1;
      shell_print(shell_cwd);
      shell_print("> ");
      continue;
    }

    if (c == '\n') {
      input[pos] = 0; // Ensure null termination
      shell_putchar('\n');
      history_record(input);
      execute_command(input);
      pos = 0;
      cursor = 0;
      history_view = -1;
      memset(input, 0, sizeof(input)); // Clear buffer
      shell_print(shell_cwd);
      shell_print("> ");
    } else if (c == '\b') {
      if (cursor > 0) {
        // Delete character before cursor
        cursor--;
        // Shift characters left
        for (int i = cursor; i < pos - 1; i++) {
          input[i] = input[i + 1];
        }
        pos--;
        input[pos] = '\0';
        // Move screen cursor back
        shell_putchar('\b');
        // Re-render from cursor to end
        for (int i = cursor; i < pos; i++) {
          shell_putchar(input[i]);
        }
        shell_putchar(' '); // Erase trailing character
        // Move cursor back to correct position
        for (int i = 0; i < pos - cursor + 1; i++) {
          shell_putchar('\b');
        }
      }
      history_view = -1; // Stop browsing when editing
    } else if (c && pos < MAX_INPUT_LEN) {
      if (cursor == pos) {
        // Append at end (fast path)
        input[pos++] = c;
        input[pos] = '\0';
        cursor++;
        shell_putchar(c);
      } else {
        // Insert in middle: shift characters right
        for (int i = pos; i > cursor; i--) {
          input[i] = input[i - 1];
        }
        input[cursor] = c;
        pos++;
        input[pos] = '\0';
        // Print from cursor to end
        for (int i = cursor; i < pos; i++) {
          shell_putchar(input[i]);
        }
        cursor++;
        // Move cursor back to correct position
        for (int i = 0; i < pos - cursor; i++) {
          shell_putchar('\b');
        }
      }
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
static int gui_input_pos = 0;    // length of input
static int gui_input_cursor = 0; // cursor position within input
static int gui_history_view = -1;

/* GUI-mode wrappers (print/putchar go to GUI buffer) */
static void gui_replace_input(const char *new_text) {
  // Move cursor to end first
  while (gui_input_cursor < gui_input_pos) {
    shell_gui_putchar(gui_input[gui_input_cursor]);
    gui_input_cursor++;
  }
  // Erase all
  while (gui_input_pos > 0) {
    shell_gui_putchar('\b');
    shell_gui_putchar(' ');
    shell_gui_putchar('\b');
    gui_input_pos--;
  }
  gui_input_cursor = 0;

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
  gui_input_cursor = i;
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

/* ══════════════════════════════════════════════════════════════════════
 *  JIT Program Input Routing (GUI Mode)
 * ══════════════════════════════════════════════════════════════════════ */

void shell_jit_program_input(char c) {

  /* Check for Ctrl+C (ASCII 3) to interrupt program */
  if (c == 3) {
    jit_program_interrupted = 1;
    return;
  }

  /* Add to circular buffer */
  int next_write = (jit_input_write_pos + 1) % JIT_INPUT_BUFFER_SIZE;
  if (next_write != jit_input_read_pos) {
    jit_input_buffer[jit_input_write_pos] = c;
    jit_input_write_pos = next_write;
  } else {
  }
}

char shell_jit_program_getchar(void) {

  /* Wait for input (blocking with GUI updates) */
  while (jit_input_read_pos == jit_input_write_pos &&
         !jit_program_interrupted) {
    /* Pump GUI to keep display responsive */
    if (shell_get_output_mode() == SHELL_OUTPUT_GUI) {
      terminal_mark_dirty();
      desktop_redraw_cycle();
    }
    __asm__ volatile("hlt");
  }

  /* Check if interrupted */
  if (jit_program_interrupted) {
    return 0; /* Return null to signal interruption */
  }

  /* Read from buffer */
  char c = jit_input_buffer[jit_input_read_pos];
  jit_input_read_pos = (jit_input_read_pos + 1) % JIT_INPUT_BUFFER_SIZE;

  return c;
}

char shell_jit_program_pollchar(void) {
  if (jit_input_read_pos == jit_input_write_pos)
    return 0; /* No key available */
  char c = jit_input_buffer[jit_input_read_pos];
  jit_input_read_pos = (jit_input_read_pos + 1) % JIT_INPUT_BUFFER_SIZE;
  return c;
}

void shell_jit_program_start(const char *name) {
  /* If a JIT program is already loaded, push its state onto the stack */
  if (jit_program_name[0] != '\0' && jit_stack_depth < JIT_STACK_MAX) {
    int d = jit_stack_depth;
    int j = 0;
    while (jit_program_name[j]) {
      jit_stack[d].name[j] = jit_program_name[j];
      j++;
    }
    jit_stack[d].name[j] = '\0';
    jit_stack[d].running = jit_program_running;
    jit_stack[d].killed  = jit_program_killed;

    /* Save the JIT code+data regions so they survive being overwritten */
    jit_stack[d].saved_code = kmalloc(CC_MAX_CODE);
    jit_stack[d].saved_data = kmalloc(CC_MAX_DATA);
    if (jit_stack[d].saved_code)
      memcpy(jit_stack[d].saved_code, (void *)CC_JIT_CODE_BASE, CC_MAX_CODE);
    if (jit_stack[d].saved_data)
      memcpy(jit_stack[d].saved_data, (void *)CC_JIT_DATA_BASE, CC_MAX_DATA);

    jit_stack_depth++;
    serial_printf("[shell] JIT stack push depth=%d saved %s\n", jit_stack_depth, jit_stack[d].name);
  }

  jit_program_running = 1;
  jit_program_interrupted = 0;
  jit_program_killed = 0;
  jit_input_read_pos = 0;
  jit_input_write_pos = 0;

  /* Store the program name for ps listing */
  int i = 0;
  if (name) {
    /* Extract basename: skip path prefix */
    const char *base = name;
    for (const char *p = name; *p; p++) {
      if (*p == '/') base = p + 1;
    }
    /* Copy basename, strip .cc extension */
    while (base[i] && i < 62) {
      if (base[i] == '.' && base[i+1] == 'c' && base[i+2] == 'c' && base[i+3] == '\0')
        break;
      jit_program_name[i] = base[i];
      i++;
    }
  }
  jit_program_name[i] = '\0';
}

void shell_jit_program_end(void) {
  /* Pop previous JIT state if we nested */
  if (jit_stack_depth > 0) {
    jit_stack_depth--;
    int d = jit_stack_depth;
    int j = 0;
    while (jit_stack[d].name[j]) {
      jit_program_name[j] = jit_stack[d].name[j];
      j++;
    }
    jit_program_name[j] = '\0';
    jit_program_running = jit_stack[d].running;
    jit_program_killed  = jit_stack[d].killed;
    jit_program_interrupted = 0;

    /* Restore the saved JIT code+data regions */
    if (jit_stack[d].saved_code) {
      memcpy((void *)CC_JIT_CODE_BASE, jit_stack[d].saved_code, CC_MAX_CODE);
      kfree(jit_stack[d].saved_code);
      jit_stack[d].saved_code = NULL;
    }
    if (jit_stack[d].saved_data) {
      memcpy((void *)CC_JIT_DATA_BASE, jit_stack[d].saved_data, CC_MAX_DATA);
      kfree(jit_stack[d].saved_data);
      jit_stack[d].saved_data = NULL;
    }
    serial_printf("[shell] JIT stack pop depth=%d restored %s\n", jit_stack_depth, jit_program_name);
  } else {
    jit_program_running = 0;
    jit_program_interrupted = 0;
    jit_program_killed = 0;
    jit_program_name[0] = '\0';
  }

  /* Defensive cleanup: fullscreen apps can bypass their own teardown paths. */
  if (gfx2d_fullscreen_active()) {
    gfx2d_fullscreen_exit();
  }
}

void shell_jit_program_suspend(void) {
  jit_program_running = 0;
}

void shell_jit_program_resume(void) {
  jit_program_running = 1;
}

int shell_jit_program_is_running(void) {
  return jit_program_name[0] != '\0' || jit_stack_depth > 0;
}

const char *shell_jit_program_get_name(void) {
  return jit_program_name;
}

void shell_jit_program_kill(void) {
  /* Kill the suspended (minimized) JIT program, not the active one */
  if (jit_stack_depth > 0) {
    jit_stack[jit_stack_depth - 1].killed = 1;
  } else {
    jit_program_killed = 1;
    jit_program_interrupted = 1;
  }
}

void shell_jit_program_kill_at(int index) {
  if (index >= 0 && index < jit_stack_depth) {
    jit_stack[index].killed = 1;
  } else if (index == jit_stack_depth) {
    /* The active (topmost) JIT program */
    jit_program_killed = 1;
    jit_program_interrupted = 1;
  }
}

int shell_jit_program_was_killed(void) {
  return jit_program_killed;
}

int shell_jit_suspended_count(void) {
  return jit_stack_depth;
}

const char *shell_jit_suspended_get_name(int index) {
  if (index >= 0 && index < jit_stack_depth)
    return jit_stack[index].name;
  return "";
}

void shell_gui_handle_key(uint8_t scancode, char character) {
  if (output_mode != SHELL_OUTPUT_GUI)
    return;

  /* If a JIT program is running, route input to it instead of shell */
  if (jit_program_running) {
    /* Only route actual characters, not key releases or non-character keys */
    if (character != 0) {
      shell_jit_program_input(character);
    }
    /* Also route arrow left/right scancodes for JIT programs that need them */
    if (character == 0 && (scancode == SCANCODE_ARROW_LEFT ||
                           scancode == SCANCODE_ARROW_RIGHT)) {
      /* JIT programs currently don't handle raw scancodes, skip */
    }
    return;
  }

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

  /* Left arrow: move cursor left */
  if (character == 0 && scancode == SCANCODE_ARROW_LEFT) {
    if (gui_input_cursor > 0) {
      gui_input_cursor--;
      shell_gui_putchar('\b');
    }
    return;
  }

  /* Right arrow: move cursor right */
  if (character == 0 && scancode == SCANCODE_ARROW_RIGHT) {
    if (gui_input_cursor < gui_input_pos) {
      shell_gui_putchar(gui_input[gui_input_cursor]);
      gui_input_cursor++;
    }
    return;
  }

  /* Tab completion */
  if (character == '\t') {
    /* Move cursor to end before tab completion */
    while (gui_input_cursor < gui_input_pos) {
      shell_gui_putchar(gui_input[gui_input_cursor]);
      gui_input_cursor++;
    }
    tab_complete(gui_input, &gui_input_pos);
    gui_input_cursor = gui_input_pos;
    gui_history_view = -1;
    return;
  }

  /* Ctrl+C: cancel current line */
  if (character == 3) {
    shell_gui_print("^C\n");
    gui_input_pos = 0;
    gui_input_cursor = 0;
    gui_history_view = -1;
    memset(gui_input, 0, sizeof(gui_input));
    shell_gui_print(shell_cwd);
    shell_gui_print("> ");
    return;
  }

  if (character == '\n') {
    gui_input[gui_input_pos] = '\0';
    shell_gui_putchar('\n');

    if (gui_input_pos > 0) {
      history_record(gui_input);
      gui_exec_command(gui_input);
    }

    gui_input_pos = 0;
    gui_input_cursor = 0;
    gui_history_view = -1;
    memset(gui_input, 0, sizeof(gui_input));
    shell_gui_print(shell_cwd);
    shell_gui_print("> ");
  } else if (character == '\b') {
    if (gui_input_cursor > 0) {
      gui_input_cursor--;
      /* Shift characters left */
      for (int i = gui_input_cursor; i < gui_input_pos - 1; i++) {
        gui_input[i] = gui_input[i + 1];
      }
      gui_input_pos--;
      gui_input[gui_input_pos] = '\0';
      /* Move screen cursor back */
      shell_gui_putchar('\b');
      /* Re-render from cursor to end */
      for (int i = gui_input_cursor; i < gui_input_pos; i++) {
        shell_gui_putchar(gui_input[i]);
      }
      shell_gui_putchar(' '); /* Erase trailing char */
      /* Move cursor back to correct position */
      for (int i = 0; i < gui_input_pos - gui_input_cursor + 1; i++) {
        shell_gui_putchar('\b');
      }
    }
    gui_history_view = -1;
  } else if (character && gui_input_pos < MAX_INPUT_LEN) {
    if (gui_input_cursor == gui_input_pos) {
      /* Append at end (fast path) */
      gui_input[gui_input_pos++] = character;
      gui_input[gui_input_pos] = '\0';
      gui_input_cursor++;
      shell_gui_putchar(character);
    } else {
      /* Insert in middle: shift characters right */
      for (int i = gui_input_pos; i > gui_input_cursor; i--) {
        gui_input[i] = gui_input[i - 1];
      }
      gui_input[gui_input_cursor] = character;
      gui_input_pos++;
      gui_input[gui_input_pos] = '\0';
      /* Print from cursor to end */
      for (int i = gui_input_cursor; i < gui_input_pos; i++) {
        shell_gui_putchar(gui_input[i]);
      }
      gui_input_cursor++;
      /* Move cursor back to correct position */
      for (int i = 0; i < gui_input_pos - gui_input_cursor; i++) {
        shell_gui_putchar('\b');
      }
    }
    gui_history_view = -1;
  }

  (void)shell_print_fn;
  (void)shell_putchar_fn;
  (void)shell_print_int_fn;
  (void)shell_gui_print_int;
}
