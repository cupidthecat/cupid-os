#include "shell.h"
#include "rtc.h"
#include "serial.h"
#include "app_launch.h"
#include "assert.h"
#include "blockcache.h"
#include "calendar.h"
#include "as.h"
#include "dis.h"
#include "cupidc.h"
#include "cupidscript.h"
#include "desktop.h"
#include "exec.h"
#include "fat16.h"
#include "fs.h"
#include "gfx2d.h"
#include "gui_themes.h"
#include "kernel.h"
#include "keyboard.h"
#include "math.h"
#include "memory.h"
#include "ports.h"
#include "process.h"
#include "swap.h"
#include "usb.h"
#include "pci.h"
#include "smp.h"
#include "percpu.h"
#include "bkl.h"
#include "terminal_app.h"
#include "string.h"
#include "gui.h"
#include "ansi.h"
#include "timer.h"
#include "types.h"
#include "vfs.h"
#include "net_if.h"
#include "arp.h"
#include "dns.h"
#include "icmp.h"
#include "ip.h"
#include "socket.h"
#include "sshd.h"

#define MAX_INPUT_LEN 80
#define HISTORY_SIZE 16
#define SHELL_REPL_PENDING_MAX (64 * 1024)

/*
 *  GUI output mode support
*/
static shell_output_mode_t output_mode = SHELL_OUTPUT_TEXT;
static char gui_buffer[SHELL_ROWS * SHELL_COLS];
static shell_color_t gui_color_buffer[SHELL_ROWS * SHELL_COLS];
static char gui_saved_buffer[SHELL_ROWS * SHELL_COLS];
static shell_color_t gui_saved_color_buffer[SHELL_ROWS * SHELL_COLS];
static terminal_color_state_t shell_ansi_state;
static int gui_cursor_x = 0;
static int gui_cursor_y = 0;
static int gui_saved_cursor_x = 0;
static int gui_saved_cursor_y = 0;
static int gui_last_was_cr = 0;
static int gui_visible_cols = SHELL_COLS; /* actual visible column width */
static int gui_visible_rows = 25;
static int gui_scroll_top = 0;
static int gui_scroll_bottom = 24;
static int gui_alt_screen = 0;
static int gui_saved_main_cursor_x = 0;
static int gui_saved_main_cursor_y = 0;
static int gui_terminal_cursor_visible = 1;
static int gui_app_cursor_keys = 0;
static int gui_wrap_enabled = 1;
static int gui_origin_mode = 0;
static int gui_utf8_remaining = 0;
static uint32_t gui_utf8_codepoint = 0;
static char gui_last_print_char = ' ';
static int gui_input_origin_x = 0;
static int gui_input_origin_y = 0;

/* I/O redirection capture buffer */
#define REDIR_BUF_SIZE 4096
static char *redir_buf = NULL; /* heap-allocated when active */
static int redir_len = 0;
static bool redir_active = false;

/* Current working directory */
static char shell_cwd[VFS_MAX_PATH] = "/";

/* Program argument passing (TempleOS-style) */
#define SHELL_ARGS_MAX 256
static char shell_program_args[SHELL_ARGS_MAX] = "";

/* JIT program input routing (for GUI mode) */
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
  char     name[64];
  int      running;
  int      killed;
  void    *saved_code;
  void    *saved_data;
  uint32_t code_address;
  uint32_t code_bytes;
  uint32_t data_address;
  uint32_t data_bytes;
  uint32_t owner_pid;   /* PID that was blocked when this entry was pushed */
} jit_stack[JIT_STACK_MAX];
static int      jit_stack_depth = 0;
static uint32_t jit_owner_pid   = 0;
static uint32_t jit_code_address = 0;
static uint32_t jit_code_bytes = 0;
static uint32_t jit_data_address = 0;
static uint32_t jit_data_bytes = 0;
static int jit_regions_active = 0;
static char shell_repl_pending[SHELL_REPL_PENDING_MAX];
static uint32_t shell_repl_pending_len = 0;
static int shell_repl_brace_depth = 0;
static char gui_repl_pending[SHELL_REPL_PENDING_MAX];
static uint32_t gui_repl_pending_len = 0;
static int gui_repl_brace_depth = 0;

static shell_output_sink_t process_output_sinks[MAX_PROCESSES + 1];
static void *process_output_sink_ctx[MAX_PROCESSES + 1];

static void shell_putchar(char c);
static void shell_print(const char *s);
static void shell_gui_putchar(char c);
static void shell_gui_print(const char *s);
static void gui_exec_command(const char *input);

static int gui_ssh_debug = 1;

static void shell_jit_clear_active_regions(void) {
  jit_code_address = 0u;
  jit_code_bytes = 0u;
  jit_data_address = 0u;
  jit_data_bytes = 0u;
  jit_regions_active = 0;
}

static int shell_ssh_debug_active(void) {
  return gui_ssh_debug && jit_program_running &&
         strcmp(jit_program_name, "ssh") == 0;
}

static int shell_jit_input_queued(void) {
  if (jit_input_write_pos >= jit_input_read_pos)
    return jit_input_write_pos - jit_input_read_pos;
  return JIT_INPUT_BUFFER_SIZE - jit_input_read_pos + jit_input_write_pos;
}

void shell_set_process_output_sink(uint32_t pid,
                                   shell_output_sink_t sink,
                                   void *ctx) {
  if (pid > MAX_PROCESSES)
    return;
  process_output_sinks[pid] = sink;
  process_output_sink_ctx[pid] = ctx;
}

void shell_clear_process_output_sink(uint32_t pid) {
  if (pid > MAX_PROCESSES)
    return;
  process_output_sinks[pid] = NULL;
  process_output_sink_ctx[pid] = NULL;
}

int shell_output_write_current(const char *buf, uint32_t len) {
  uint32_t pid;
  if (!process_is_active()) {
    if (!process_output_sinks[0])
      return 0;
    process_output_sinks[0](buf, len, process_output_sink_ctx[0]);
    return 1;
  }

  pid = process_get_current_pid();
  if (pid <= MAX_PROCESSES && process_output_sinks[pid]) {
    process_output_sinks[pid](buf, len, process_output_sink_ctx[pid]);
    return 1;
  }

  if (!process_output_sinks[0])
    return 0;
  process_output_sinks[0](buf, len, process_output_sink_ctx[0]);
  return 1;
}

int shell_output_putchar_current(char c) {
  return shell_output_write_current(&c, 1u);
}

static void shell_debug_dump_bytes(const char *tag, const char *buf, int len) {
  if (!shell_ssh_debug_active())
    return;
  serial_printf("[ssh-input] %s len=%d bytes=", tag, len);
  for (int i = 0; i < len; i++) {
    unsigned char b = (unsigned char)buf[i];
    if (b == 0) serial_printf("<NUL>");
    else if (b == 7) serial_printf("<BEL>");
    else if (b == 8) serial_printf("<BS>");
    else if (b == 9) serial_printf("<TAB>");
    else if (b == 10) serial_printf("<LF>");
    else if (b == 13) serial_printf("<CR>");
    else if (b == 27) serial_printf("<ESC>");
    else if (b >= 32 && b <= 126) {
      char one[2];
      one[0] = (char)b;
      one[1] = '\0';
      serial_printf("%s", one);
    } else {
      serial_printf("<0x%x>", b);
    }
  }
  serial_printf("\n");
}

static void shell_print_u32_padded(uint32_t value, int width) {
  char buf[16];
  int pos = 0;

  do {
    buf[pos++] = (char)('0' + (value % 10u));
    value /= 10u;
  } while (value && pos < (int)sizeof(buf));

  while (pos < width && pos < (int)sizeof(buf))
    buf[pos++] = '0';
  while (pos-- > 0)
    shell_putchar(buf[pos]);
}

static void shell_gui_print_u32_padded(uint32_t value, int width) {
  char buf[16];
  int pos = 0;

  do {
    buf[pos++] = (char)('0' + (value % 10u));
    value /= 10u;
  } while (value && pos < (int)sizeof(buf));

  while (pos < width && pos < (int)sizeof(buf))
    buf[pos++] = '0';
  while (pos-- > 0)
    shell_gui_putchar(buf[pos]);
}

static void shell_print_repl_result_prefix(void) {
  int32_t value = 0;
  int has_value = 0;
  uint32_t elapsed_ms = 0;

  if (!repl_consume_prompt_result(&value, &has_value, &elapsed_ms))
    return;

  print_int(elapsed_ms / 1000u);
  shell_putchar('.');
  shell_print_u32_padded(elapsed_ms % 1000u, 3);
  shell_print("s");
  if (has_value) {
    shell_print(" ans=0x");
    print_hex((uint32_t)value);
    shell_putchar('=');
    if (value < 0) {
      shell_putchar('-');
      print_int((uint32_t)(-(int64_t)value));
    } else {
      print_int((uint32_t)value);
    }
  }
  shell_putchar('\n');
}

static void shell_gui_print_repl_result_prefix(void) {
  int32_t value = 0;
  int has_value = 0;
  uint32_t elapsed_ms = 0;

  if (!repl_consume_prompt_result(&value, &has_value, &elapsed_ms))
    return;

  print_int(elapsed_ms / 1000u);
  shell_gui_putchar('.');
  shell_gui_print_u32_padded(elapsed_ms % 1000u, 3);
  shell_gui_print("s");
  if (has_value) {
    shell_gui_print(" ans=0x");
    print_hex((uint32_t)value);
    shell_gui_putchar('=');
    if (value < 0) {
      shell_gui_putchar('-');
      print_int((uint32_t)(-(int64_t)value));
    } else {
      print_int((uint32_t)value);
    }
  }
  shell_gui_putchar('\n');
}

static void shell_print_prompt(void) {
  shell_print_repl_result_prefix();
  if (shell_repl_brace_depth > 0) {
    shell_print("..> ");
  } else {
    shell_print(shell_cwd);
    shell_print("> ");
  }
}

static void shell_gui_print_prompt(void) {
  shell_gui_print_repl_result_prefix();
  if (gui_repl_brace_depth > 0) {
    shell_gui_print("..> ");
  } else {
    shell_gui_print(shell_cwd);
    shell_gui_print("> ");
  }
  gui_input_origin_x = gui_cursor_x;
  gui_input_origin_y = gui_cursor_y;
}

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
static void execute_command_inner(const char *input, int try_repl);
static void shell_gui_putchar(char c);
static void shell_gui_print(const char *s);
static void shell_gui_print_int(uint32_t num);
static int shell_ends_with(const char *str, const char *suffix);

static void shell_mark_terminal_dirty(void) {
  int n = gui_window_count();
  for (int i = 0; i < n; i++) {
    window_t *w = gui_get_window_by_index(i);
    if (!w)
      continue;
    if (strcmp(w->title, "Terminal") == 0) {
      w->flags |= WINDOW_FLAG_DIRTY;
    }
  }
}

void shell_set_output_mode(shell_output_mode_t mode) {
  if (output_mode == mode) {
    return;
  }

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
    gui_saved_cursor_x = 0;
    gui_saved_cursor_y = 0;
    gui_last_was_cr = 0;
    gui_scroll_top = 0;
    gui_scroll_bottom = gui_visible_rows - 1;
    gui_alt_screen = 0;
    gui_terminal_cursor_visible = 1;
    gui_app_cursor_keys = 0;
    gui_wrap_enabled = 1;
    gui_origin_mode = 0;
    gui_utf8_remaining = 0;
    gui_utf8_codepoint = 0;
    gui_last_print_char = ' ';
    gui_repl_pending_len = 0;
    gui_repl_pending[0] = '\0';
    gui_repl_brace_depth = 0;
    shell_gui_print("cupid-os shell\n");
    shell_gui_print_prompt();
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
    if (gui_cursor_x >= gui_visible_cols)
      gui_cursor_x = gui_visible_cols - 1;
  }
}

void shell_set_visible_rows(int rows) {
  if (rows > 0 && rows <= SHELL_ROWS) {
    gui_visible_rows = rows;
    if (gui_scroll_bottom < gui_scroll_top || gui_scroll_bottom >= gui_visible_rows) {
      gui_scroll_top = 0;
      gui_scroll_bottom = gui_visible_rows - 1;
    }
    if (gui_alt_screen && gui_cursor_y >= gui_visible_rows)
      gui_cursor_y = gui_visible_rows - 1;
  }
}

const char *shell_get_buffer(void) { return gui_buffer; }

const shell_color_t *shell_get_color_buffer(void) { return gui_color_buffer; }

int shell_get_cursor_x(void) { return gui_cursor_x; }

int shell_get_cursor_y(void) { return gui_cursor_y; }

int shell_get_terminal_cursor_visible(void) { return gui_terminal_cursor_visible; }

int shell_get_terminal_alt_screen(void) { return gui_alt_screen; }

int shell_get_terminal_app_cursor_keys(void) { return gui_app_cursor_keys; }

static void shell_clamp_cursor(void) {
  int max_row = gui_alt_screen ? gui_visible_rows : SHELL_ROWS;
  if (max_row < 1) max_row = 1;
  if (gui_cursor_x < 0) gui_cursor_x = 0;
  if (gui_cursor_y < 0) gui_cursor_y = 0;
  if (gui_cursor_x >= gui_visible_cols) gui_cursor_x = gui_visible_cols - 1;
  if (gui_cursor_x >= SHELL_COLS) gui_cursor_x = SHELL_COLS - 1;
  if (gui_cursor_y >= max_row) gui_cursor_y = max_row - 1;
}

static void shell_clear_row_raw(int row) {
  if (row < 0 || row >= SHELL_ROWS)
    return;
  memset(gui_buffer + row * SHELL_COLS, 0, (size_t)SHELL_COLS);
  for (int col = 0; col < SHELL_COLS; col++) {
    int idx = row * SHELL_COLS + col;
    gui_color_buffer[idx].fg = ANSI_DEFAULT_FG;
    gui_color_buffer[idx].bg = ANSI_DEFAULT_BG;
  }
}

static void shell_scroll_rows_up(int top, int bottom, int n) {
  if (top < 0) top = 0;
  if (bottom >= SHELL_ROWS) bottom = SHELL_ROWS - 1;
  if (top > bottom || n <= 0) return;
  int height = bottom - top + 1;
  if (n > height) n = height;
  if (n < height) {
    int cells = (height - n) * SHELL_COLS;
    for (int i = 0; i < cells; i++) {
      gui_buffer[top * SHELL_COLS + i] =
          gui_buffer[(top + n) * SHELL_COLS + i];
      gui_color_buffer[top * SHELL_COLS + i] =
          gui_color_buffer[(top + n) * SHELL_COLS + i];
    }
  }
  for (int row = bottom - n + 1; row <= bottom; row++) {
    shell_clear_row_raw(row);
  }
}

static void shell_scroll_rows_down(int top, int bottom, int n) {
  if (top < 0) top = 0;
  if (bottom >= SHELL_ROWS) bottom = SHELL_ROWS - 1;
  if (top > bottom || n <= 0) return;
  int height = bottom - top + 1;
  if (n > height) n = height;
  if (n < height) {
    int cells = (height - n) * SHELL_COLS;
    for (int i = cells - 1; i >= 0; i--) {
      gui_buffer[(top + n) * SHELL_COLS + i] =
          gui_buffer[top * SHELL_COLS + i];
      gui_color_buffer[(top + n) * SHELL_COLS + i] =
          gui_color_buffer[top * SHELL_COLS + i];
    }
  }
  for (int row = top; row < top + n; row++) {
    shell_clear_row_raw(row);
  }
}

/* Scroll both char and color buffers up one line */
static void shell_gui_scroll(void) {
  if (gui_alt_screen) {
    shell_scroll_rows_up(gui_scroll_top, gui_scroll_bottom, 1);
    gui_cursor_y = gui_scroll_bottom;
    return;
  }
  shell_scroll_rows_up(0, SHELL_ROWS - 1, 1);
  gui_cursor_y = SHELL_ROWS - 1;
}

/* Store current ANSI color into a cell's color entry */
static void shell_set_cell_color(int idx) {
  gui_color_buffer[idx].fg = ansi_get_fg(&shell_ansi_state);
  gui_color_buffer[idx].bg = ansi_get_bg(&shell_ansi_state);
}

static void shell_clear_cell_current(int row, int col) {
  if (row < 0 || row >= SHELL_ROWS || col < 0 || col >= SHELL_COLS)
    return;
  int idx = row * SHELL_COLS + col;
  gui_buffer[idx] = 0;
  shell_set_cell_color(idx);
}

static void shell_clear_line_range_current(int row, int first_col, int last_col) {
  if (row < 0 || row >= SHELL_ROWS)
    return;
  if (first_col < 0) first_col = 0;
  if (last_col >= SHELL_COLS) last_col = SHELL_COLS - 1;
  for (int col = first_col; col <= last_col; col++) {
    shell_clear_cell_current(row, col);
  }
}

static void shell_clear_rows_current(int first_row, int last_row) {
  if (first_row < 0) first_row = 0;
  if (last_row >= SHELL_ROWS) last_row = SHELL_ROWS - 1;
  for (int row = first_row; row <= last_row; row++) {
    shell_clear_line_range_current(row, 0, SHELL_COLS - 1);
  }
}

static void shell_enter_alt_screen(void) {
  if (gui_alt_screen)
    return;

  memcpy(gui_saved_buffer, gui_buffer, sizeof(gui_buffer));
  memcpy(gui_saved_color_buffer, gui_color_buffer, sizeof(gui_color_buffer));
  gui_saved_main_cursor_x = gui_cursor_x;
  gui_saved_main_cursor_y = gui_cursor_y;

  memset(gui_buffer, 0, sizeof(gui_buffer));
  for (int i = 0; i < SHELL_ROWS * SHELL_COLS; i++) {
    gui_color_buffer[i].fg = ANSI_DEFAULT_FG;
    gui_color_buffer[i].bg = ANSI_DEFAULT_BG;
  }

  gui_alt_screen = 1;
  gui_cursor_x = 0;
  gui_cursor_y = 0;
  gui_saved_cursor_x = 0;
  gui_saved_cursor_y = 0;
  gui_scroll_top = 0;
  gui_scroll_bottom = gui_visible_rows - 1;
  gui_last_was_cr = 0;
}

static void shell_leave_alt_screen(void) {
  if (!gui_alt_screen)
    return;

  memcpy(gui_buffer, gui_saved_buffer, sizeof(gui_buffer));
  memcpy(gui_color_buffer, gui_saved_color_buffer, sizeof(gui_color_buffer));
  gui_alt_screen = 0;
  gui_cursor_x = gui_saved_main_cursor_x;
  gui_cursor_y = gui_saved_main_cursor_y;
  gui_scroll_top = 0;
  gui_scroll_bottom = gui_visible_rows - 1;
  gui_terminal_cursor_visible = 1;
  gui_app_cursor_keys = 0;
  gui_wrap_enabled = 1;
  gui_origin_mode = 0;
  gui_last_was_cr = 0;
  shell_clamp_cursor();
}

static int shell_region_bottom(void) {
  if (gui_alt_screen) return gui_scroll_bottom;
  return SHELL_ROWS - 1;
}

static void shell_linefeed(void) {
  int bottom = shell_region_bottom();
  if (gui_cursor_y >= bottom) {
    if (gui_alt_screen) {
      shell_scroll_rows_up(gui_scroll_top, gui_scroll_bottom, 1);
      gui_cursor_y = bottom;
    } else {
      shell_gui_scroll();
    }
  } else {
    gui_cursor_y++;
  }
}

static void shell_insert_lines(int n) {
  if (n <= 0) return;
  if (gui_cursor_y < gui_scroll_top || gui_cursor_y > gui_scroll_bottom)
    return;
  shell_scroll_rows_down(gui_cursor_y, gui_scroll_bottom, n);
}

static void shell_delete_lines(int n) {
  if (n <= 0) return;
  if (gui_cursor_y < gui_scroll_top || gui_cursor_y > gui_scroll_bottom)
    return;
  shell_scroll_rows_up(gui_cursor_y, gui_scroll_bottom, n);
}

static void shell_insert_chars(int n) {
  if (n <= 0 || gui_cursor_y < 0 || gui_cursor_y >= SHELL_ROWS)
    return;
  int row = gui_cursor_y;
  if (n > gui_visible_cols - gui_cursor_x)
    n = gui_visible_cols - gui_cursor_x;
  for (int col = gui_visible_cols - 1; col >= gui_cursor_x + n; col--) {
    int dst = row * SHELL_COLS + col;
    int src = row * SHELL_COLS + col - n;
    gui_buffer[dst] = gui_buffer[src];
    gui_color_buffer[dst] = gui_color_buffer[src];
  }
  shell_clear_line_range_current(row, gui_cursor_x, gui_cursor_x + n - 1);
}

static void shell_delete_chars(int n) {
  if (n <= 0 || gui_cursor_y < 0 || gui_cursor_y >= SHELL_ROWS)
    return;
  int row = gui_cursor_y;
  if (n > gui_visible_cols - gui_cursor_x)
    n = gui_visible_cols - gui_cursor_x;
  for (int col = gui_cursor_x; col + n < gui_visible_cols; col++) {
    int dst = row * SHELL_COLS + col;
    int src = row * SHELL_COLS + col + n;
    gui_buffer[dst] = gui_buffer[src];
    gui_color_buffer[dst] = gui_color_buffer[src];
  }
  shell_clear_line_range_current(row, gui_visible_cols - n, gui_visible_cols - 1);
}

static void shell_set_terminal_mode(int mode, int private_mode, int enabled) {
  if (private_mode) {
    if (mode == 25) {
      gui_terminal_cursor_visible = enabled ? 1 : 0;
    } else if (mode == 1) {
      gui_app_cursor_keys = enabled ? 1 : 0;
    } else if (mode == 7) {
      gui_wrap_enabled = enabled ? 1 : 0;
    } else if (mode == 6) {
      gui_origin_mode = enabled ? 1 : 0;
      gui_cursor_x = 0;
      gui_cursor_y = gui_origin_mode ? gui_scroll_top : 0;
      shell_clamp_cursor();
    } else if (mode == 47 || mode == 1047 || mode == 1049) {
      if (enabled) shell_enter_alt_screen();
      else shell_leave_alt_screen();
    }
  }
}

static char shell_map_unicode_to_cell(uint32_t cp) {
  if (cp >= 0x2500U && cp <= 0x257FU) return '+';
  if (cp >= 0x2580U && cp <= 0x259FU) return '#';
  if (cp == 0x2190U) return '<';
  if (cp == 0x2191U) return '^';
  if (cp == 0x2192U) return '>';
  if (cp == 0x2193U) return 'v';
  if (cp == 0x2022U || cp == 0x25CFU || cp == 0x25CBU) return '*';
  if (cp == 0x2013U || cp == 0x2014U || cp == 0x2212U) return '-';
  if (cp == 0x00B0U) return 'o';
  if (cp == 0x2713U || cp == 0x2714U) return '*';
  if (cp >= 32U && cp <= 126U) return (char)cp;
  return '?';
}

static char shell_map_acs_to_cell(char c) {
  switch (c) {
  case 'j': case 'k': case 'l': case 'm':
  case 'n': case 't': case 'u': case 'v': case 'w':
    return '+';
  case 'q':
    return '-';
  case 'x':
    return '|';
  case 'a':
    return '#';
  case '`':
    return '+';
  case 'f':
    return '\'';
  case 'g':
    return '#';
  case 'o':
    return '~';
  case 's':
    return '_';
  case '~':
    return 'o';
  default:
    return c;
  }
}

static int shell_utf8_next_cell(unsigned char byte, char *out) {
  if (gui_utf8_remaining > 0) {
    if ((byte & 0xC0U) != 0x80U) {
      gui_utf8_remaining = 0;
      gui_utf8_codepoint = 0;
      *out = '?';
      return 1;
    }
    gui_utf8_codepoint = (gui_utf8_codepoint << 6) | (uint32_t)(byte & 0x3FU);
    gui_utf8_remaining--;
    if (gui_utf8_remaining == 0) {
      *out = shell_map_unicode_to_cell(gui_utf8_codepoint);
      gui_utf8_codepoint = 0;
      return 1;
    }
    return 0;
  }

  if (byte < 0x80U) {
    *out = (char)byte;
    return 1;
  }
  if ((byte & 0xE0U) == 0xC0U) {
    gui_utf8_codepoint = (uint32_t)(byte & 0x1FU);
    gui_utf8_remaining = 1;
    return 0;
  }
  if ((byte & 0xF0U) == 0xE0U) {
    gui_utf8_codepoint = (uint32_t)(byte & 0x0FU);
    gui_utf8_remaining = 2;
    return 0;
  }
  if ((byte & 0xF8U) == 0xF0U) {
    gui_utf8_codepoint = (uint32_t)(byte & 0x07U);
    gui_utf8_remaining = 3;
    return 0;
  }

  *out = '?';
  return 1;
}

/* Put a character into the GUI buffer with ANSI escape processing */
static void shell_gui_putchar(char c) {

  /* Feed character through ANSI parser first */
  ansi_result_t result = ansi_process_char(&shell_ansi_state, c);
  if (shell_ssh_debug_active() && result != ANSI_RESULT_SKIP &&
      result != ANSI_RESULT_PRINT) {
    serial_printf("[ssh-render] ansi result=%d byte=0x%x p1=%d p2=%d cursor=%d,%d scroll=%d-%d alt=%d origin=%d wrap=%d app_cursor=%d\n",
                  result, (unsigned char)c, shell_ansi_state.param1,
                  shell_ansi_state.param2, gui_cursor_x, gui_cursor_y,
                  gui_scroll_top, gui_scroll_bottom, gui_alt_screen,
                  gui_origin_mode, gui_wrap_enabled, gui_app_cursor_keys);
  }

  switch (result) {
  case ANSI_RESULT_SKIP:
    /* Escape sequence in progress or color code processed - nothing to display
*/
    return;

  case ANSI_RESULT_CLEAR:
    /* ESC[2J - clear entire screen */
    memset(gui_buffer, 0, sizeof(gui_buffer));
    for (int i = 0; i < SHELL_ROWS * SHELL_COLS; i++) {
      gui_color_buffer[i].fg = ANSI_DEFAULT_FG;
      gui_color_buffer[i].bg = ANSI_DEFAULT_BG;
    }
    gui_cursor_x = 0;
    gui_cursor_y = 0;
    return;

  case ANSI_RESULT_HOME:
    /* ESC[H - cursor home */
    gui_cursor_x = 0;
    gui_cursor_y = 0;
    return;

  case ANSI_RESULT_CURSOR_POS:
    gui_cursor_y = shell_ansi_state.param1 +
                   (gui_origin_mode ? gui_scroll_top : 0);
    gui_cursor_x = shell_ansi_state.param2;
    shell_clamp_cursor();
    return;

  case ANSI_RESULT_CURSOR_UP:
    gui_cursor_y -= shell_ansi_state.param1;
    if (shell_ansi_state.param2 == 1) gui_cursor_x = 0;
    shell_clamp_cursor();
    return;

  case ANSI_RESULT_CURSOR_DOWN:
    gui_cursor_y += shell_ansi_state.param1;
    if (shell_ansi_state.param2 == 1) gui_cursor_x = 0;
    shell_clamp_cursor();
    return;

  case ANSI_RESULT_CURSOR_FORWARD:
    gui_cursor_x += shell_ansi_state.param1;
    shell_clamp_cursor();
    return;

  case ANSI_RESULT_CURSOR_BACK:
    gui_cursor_x -= shell_ansi_state.param1;
    shell_clamp_cursor();
    return;

  case ANSI_RESULT_CURSOR_COL:
    gui_cursor_x = shell_ansi_state.param1;
    shell_clamp_cursor();
    return;

  case ANSI_RESULT_ERASE_LINE:
    if (shell_ansi_state.param1 == 1) {
      shell_clear_line_range_current(gui_cursor_y, 0, gui_cursor_x);
    } else if (shell_ansi_state.param1 == 2) {
      shell_clear_line_range_current(gui_cursor_y, 0, SHELL_COLS - 1);
    } else {
      shell_clear_line_range_current(gui_cursor_y, gui_cursor_x, SHELL_COLS - 1);
    }
    return;

  case ANSI_RESULT_ERASE_DISPLAY:
    if (shell_ansi_state.param1 == 1) {
      shell_clear_rows_current(0, gui_cursor_y - 1);
      shell_clear_line_range_current(gui_cursor_y, 0, gui_cursor_x);
    } else {
      shell_clear_line_range_current(gui_cursor_y, gui_cursor_x, SHELL_COLS - 1);
      shell_clear_rows_current(gui_cursor_y + 1, SHELL_ROWS - 1);
    }
    return;

  case ANSI_RESULT_SAVE_CURSOR:
    gui_saved_cursor_x = gui_cursor_x;
    gui_saved_cursor_y = gui_cursor_y;
    return;

  case ANSI_RESULT_RESTORE_CURSOR:
    gui_cursor_x = gui_saved_cursor_x;
    gui_cursor_y = gui_saved_cursor_y;
    shell_clamp_cursor();
    return;

  case ANSI_RESULT_SCROLL_UP:
    if (gui_alt_screen)
      shell_scroll_rows_up(gui_scroll_top, gui_scroll_bottom,
                           shell_ansi_state.param1);
    else
      shell_scroll_rows_up(0, SHELL_ROWS - 1, shell_ansi_state.param1);
    return;

  case ANSI_RESULT_SCROLL_DOWN:
    if (gui_alt_screen)
      shell_scroll_rows_down(gui_scroll_top, gui_scroll_bottom,
                             shell_ansi_state.param1);
    else
      shell_scroll_rows_down(0, SHELL_ROWS - 1, shell_ansi_state.param1);
    return;

  case ANSI_RESULT_SET_MODE:
    shell_set_terminal_mode(shell_ansi_state.param1, shell_ansi_state.param2, 1);
    return;

  case ANSI_RESULT_RESET_MODE:
    shell_set_terminal_mode(shell_ansi_state.param1, shell_ansi_state.param2, 0);
    return;

  case ANSI_RESULT_SET_SCROLL_REGION:
    gui_scroll_top = shell_ansi_state.param1;
    gui_scroll_bottom = shell_ansi_state.param2 >= 0
                            ? shell_ansi_state.param2
                            : gui_visible_rows - 1;
    if (gui_scroll_top < 0) gui_scroll_top = 0;
    if (gui_scroll_bottom >= gui_visible_rows)
      gui_scroll_bottom = gui_visible_rows - 1;
    if (gui_scroll_top >= gui_scroll_bottom) {
      gui_scroll_top = 0;
      gui_scroll_bottom = gui_visible_rows - 1;
    }
    gui_cursor_x = 0;
    gui_cursor_y = gui_origin_mode ? gui_scroll_top : 0;
    return;

  case ANSI_RESULT_CURSOR_ROW:
    gui_cursor_y = shell_ansi_state.param1 +
                   (gui_origin_mode ? gui_scroll_top : 0);
    shell_clamp_cursor();
    return;

  case ANSI_RESULT_INSERT_LINE:
    shell_insert_lines(shell_ansi_state.param1);
    return;

  case ANSI_RESULT_DELETE_LINE:
    shell_delete_lines(shell_ansi_state.param1);
    return;

  case ANSI_RESULT_INSERT_CHARS:
    shell_insert_chars(shell_ansi_state.param1);
    return;

  case ANSI_RESULT_DELETE_CHARS:
    shell_delete_chars(shell_ansi_state.param1);
    return;

  case ANSI_RESULT_ERASE_CHARS:
    shell_clear_line_range_current(gui_cursor_y, gui_cursor_x,
                                   gui_cursor_x + shell_ansi_state.param1 - 1);
    return;

  case ANSI_RESULT_REPEAT_CHAR:
    for (int i = 0; i < shell_ansi_state.param1; i++) {
      shell_gui_putchar(gui_last_print_char);
    }
    return;

  case ANSI_RESULT_PRINT:
    break; /* fall through to normal character handling */
  }

  if ((unsigned char)c >= 0x80U || gui_utf8_remaining > 0) {
    char mapped;
    if (!shell_utf8_next_cell((unsigned char)c, &mapped))
      return;
    c = mapped;
  } else if (shell_ansi_state.g0_alt_charset && c >= 32 && c <= 126) {
    c = shell_map_acs_to_cell(c);
  }

  /* Normal character handling */
  if (c == '\n') {
    if (shell_ssh_debug_active()) {
      serial_printf("[ssh-render] LF cursor_before=%d,%d last_cr=%d\n",
                    gui_cursor_x, gui_cursor_y, gui_last_was_cr);
    }
    if (!gui_last_was_cr) {
      /* Local shell output uses bare LF as newline, so keep that behavior.
       * Remote PTYs send CRLF; in that case CR already moved to column 0 and
       * clearing here would erase the line that was just printed.*/
      for (int i = gui_cursor_x; i < SHELL_COLS; i++) {
        int idx = gui_cursor_y * SHELL_COLS + i;
        gui_buffer[idx] = 0;
        gui_color_buffer[idx].fg = ANSI_DEFAULT_FG;
        gui_color_buffer[idx].bg = ANSI_DEFAULT_BG;
      }
      gui_cursor_x = 0;
    }
    shell_linefeed();
    gui_last_was_cr = 0;
  } else if (c == '\r') {
    if (shell_ssh_debug_active()) {
      serial_printf("[ssh-render] CR cursor_before=%d,%d\n",
                    gui_cursor_x, gui_cursor_y);
    }
    /* Carriage return: cursor to col 0. Standard CRLF handling so HTTP
     * bodies (e.g. from /bin/curl.cc) don't render \r as a CP437 glyph
     * (♪ at byte 0x0D) on each header line.*/
    gui_cursor_x = 0;
    gui_last_was_cr = 1;
  } else if (c == '\b') {
    if (gui_cursor_x > 0) {
      gui_cursor_x--;
    }
    gui_last_was_cr = 0;
  } else if ((unsigned char)c < 32 && c != '\t') {
    /* Drop other control bytes - never render as glyphs.
     * (Order matters: \b/\n/\r/\t are handled above.)*/
    gui_last_was_cr = 0;
    return;
  } else if (c == '\t') {
    /* Tab = 4 spaces */
    for (int t = 0; t < 4 && gui_cursor_x < gui_visible_cols; t++) {
      int idx = gui_cursor_y * SHELL_COLS + gui_cursor_x;
      gui_buffer[idx] = ' ';
      shell_set_cell_color(idx);
      gui_cursor_x++;
    }
    gui_last_was_cr = 0;
  } else {
    if (gui_cursor_x < gui_visible_cols) {
      int idx = gui_cursor_y * SHELL_COLS + gui_cursor_x;
      gui_buffer[idx] = c;
      shell_set_cell_color(idx);
      gui_cursor_x++;
      gui_last_print_char = c;
    }
    gui_last_was_cr = 0;
  }

  /* Wrap at visible column width */
  if (gui_cursor_x >= gui_visible_cols) {
    if (!gui_wrap_enabled) {
      gui_cursor_x = gui_visible_cols - 1;
    } else {
      gui_cursor_x = 0;
      shell_linefeed();
    }
  }

  /* Scroll */
  if (!gui_alt_screen && gui_cursor_y >= SHELL_ROWS) {
    shell_gui_scroll();
  }

  if (output_mode == SHELL_OUTPUT_GUI)
    shell_mark_terminal_dirty();
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

/*
 *  Output wrappers that route to GUI or text mode
*/
static void shell_print(const char *s) {
  if (s && shell_output_write_current(s, (uint32_t)strlen(s)))
    return;
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
  if (shell_output_putchar_current(c))
    return;
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
  uint32_t sink_pid = process_is_active() ? process_get_current_pid() : 0u;
  if (sink_pid <= MAX_PROCESSES && process_output_sinks[sink_pid]) {
    char tmp[16];
    int i = 0;
    if (num == 0) {
      shell_output_putchar_current('0');
      return;
    }
    while (num > 0 && i < (int)sizeof(tmp)) {
      tmp[i++] = (char)('0' + (num % 10u));
      num /= 10u;
    }
    while (i > 0)
      shell_output_putchar_current(tmp[--i]);
    return;
  }
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
void shell_gui_putchar_ext(char c) {
  if (shell_output_putchar_current(c))
    return;
  shell_gui_putchar(c);
}

void shell_gui_print_ext(const char *s) {
  if (s && shell_output_write_current(s, (uint32_t)strlen(s)))
    return;
  shell_gui_print(s);
}

void shell_gui_print_int_ext(uint32_t num) {
  if (shell_output_write_current("", 0u)) {
    char tmp[16];
    int i = 0;
    if (num == 0) {
      shell_output_putchar_current('0');
      return;
    }
    while (num > 0 && i < (int)sizeof(tmp)) {
      tmp[i++] = (char)('0' + (num % 10u));
      num /= 10u;
    }
    while (i > 0)
      shell_output_putchar_current(tmp[--i]);
    return;
  }
  shell_gui_print_int(num);
}

// Scancodes for extended keys we care about
#define SCANCODE_ARROW_UP 0x48
#define SCANCODE_ARROW_DOWN 0x50
#define SCANCODE_ARROW_LEFT 0x4B
#define SCANCODE_ARROW_RIGHT 0x4D
#define SCANCODE_F7 0x41

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
static void shell_ps_cmd(const char *args);
static void shell_cupidc_cmd(const char *args);
static void shell_cc_cmd(const char *args);
static void shell_ccc_cmd(const char *args);
static void shell_reset_cmd(const char *args);
static void shell_asm_cmd(const char *args);
static void shell_cupidasm_cmd(const char *args);
static void shell_dis_cmd(const char *args);
static void shell_theme_cmd(const char *args);
static void shell_temple_cmd(const char *args);
static void shell_adam_cmd(const char *args);
static void shell_mount_cmd(const char *args);
static void shell_umount_cmd(const char *args);
static void shell_swapinit_cmd(const char *args);
static void shell_swapstats_cmd(const char *args);
static void shell_usb_cmd(const char *args);
static void shell_pci_cmd(const char *args);
static void shell_smp_cmd(const char *args);
static void shell_ifconfig_cmd(const char *args);
static void shell_ping_cmd    (const char *args);
static void shell_netstat_cmd (const char *args);
static void shell_arp_cmd     (const char *args);
static void shell_resolve_cmd (const char *args);
static void shell_doom_cmd    (const char *args);
static void shell_sshd_cmd    (const char *args);

// List of supported commands
static struct shell_command commands[] = {
    {"cupid", "Run a CupidScript (.cup) file", shell_cupid},
    {"exec", "Run a binary (ELF or CUPD)", shell_exec_cmd},
    {"notepad", "Open Notepad", shell_notepad_cmd},
    {"terminal", "Open a Terminal window", shell_terminal_cmd},
    {"ps", "List running processes", shell_ps_cmd},
    {"cc", "Interactive CupidC REPL (or run a .cc file)", shell_cc_cmd},
    {"cupidc", "Compile and run CupidC (.cc) file", shell_cupidc_cmd},
    {"ccc", "Compile CupidC to ELF binary", shell_ccc_cmd},
    {"reset", "Reset the CupidC REPL state", shell_reset_cmd},
    {"as", "Assemble and run .asm file", shell_asm_cmd},
    {"cupidasm", "Assemble .asm to ELF binary", shell_cupidasm_cmd},
    {"dis", "Disassemble ELF binary or .cc file", shell_dis_cmd},
    {"theme", "Set GUI theme (win95|pastel|dark|contrast|amber|vapor|temple)", shell_theme_cmd},
    {"temple", "Invoke TempleOS look: red/yellow/white on black", shell_temple_cmd},
    {"adam", "TempleOS-style task tree under Adam (PID 1)", shell_adam_cmd},
    {"holyc", "Alias for cc: interactive HolyC-style REPL", shell_cc_cmd},
    {"mount", "Mount a filesystem: mount <src> <target> [<type>]", shell_mount_cmd},
    {"umount", "Unmount a filesystem: umount <target>", shell_umount_cmd},
    {"swapinit",  "swapinit [pool_kb] - init opt-in swap", shell_swapinit_cmd},
    {"swapstats", "swapstats - print swap usage",           shell_swapstats_cmd},
    {"usb", "List USB devices (usb | usb hubs | usb hc)", shell_usb_cmd},
    {"pci", "List PCI devices (bus:dev.fn vid:did class irq)", shell_pci_cmd},
    {"smp", "List CPUs (smp | smp info)", shell_smp_cmd},
    {"ifconfig", "Show or set network interface", shell_ifconfig_cmd},
    {"ping",     "Send ICMP echo (ping <host> [count])", shell_ping_cmd},
    {"netstat",  "List sockets", shell_netstat_cmd},
    {"arp",      "Dump ARP cache", shell_arp_cmd},
    {"resolve",  "DNS resolve (resolve <host>)", shell_resolve_cmd},
    {"sshd",     "SSH server (sshd [start|stop|status|passwd])", shell_sshd_cmd},
    {"doom",     "Run DOOM (doom [-iwad <path>])", shell_doom_cmd},
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

#define TAB_MAX_MATCHES 64
#define TAB_MAX_NAME VFS_MAX_NAME

static char tab_matches[TAB_MAX_MATCHES][TAB_MAX_NAME];
static char tab_suffix[TAB_MAX_MATCHES];

static int tab_is_blank(char c) {
  return c == ' ' || c == '\t';
}

static void tab_copy_range(char *out, int out_cap, const char *in,
                           int start, int end) {
  int n = 0;
  while (start < end && n < out_cap - 1) {
    out[n++] = in[start++];
  }
  out[n] = '\0';
}

static int tab_match_exists(const char *name, char suffix, int count) {
  for (int i = 0; i < count; i++) {
    if (tab_suffix[i] == suffix && strcmp(tab_matches[i], name) == 0)
      return 1;
  }
  return 0;
}

static void tab_add_match(const char *name, char suffix, int *count) {
  int n = 0;
  if (*count >= TAB_MAX_MATCHES)
    return;
  if (tab_match_exists(name, suffix, *count))
    return;
  while (name[n] && n < TAB_MAX_NAME - 1) {
    tab_matches[*count][n] = name[n];
    n++;
  }
  tab_matches[*count][n] = '\0';
  tab_suffix[*count] = suffix;
  (*count)++;
}

static int tab_common_prefix_len(int count) {
  int common = TAB_MAX_NAME - 1;
  if (count <= 0)
    return 0;
  for (int m = 1; m < count; m++) {
    int n = 0;
    while (n < common && tab_matches[0][n] && tab_matches[m][n] &&
           tab_matches[0][n] == tab_matches[m][n])
      n++;
    common = n;
  }
  if (count == 1) {
    common = 0;
    while (tab_matches[0][common] && common < TAB_MAX_NAME - 1)
      common++;
  }
  return common;
}

static void tab_redraw_prompt(char *input, int pos) {
  shell_putchar('\n');
  if (output_mode == SHELL_OUTPUT_GUI)
    shell_gui_print_prompt();
  else
    shell_print_prompt();
  for (int i = 0; i < pos; i++)
    shell_putchar(input[i]);
}

static void tab_list_matches(char *input, int pos, int count) {
  shell_putchar('\n');
  for (int i = 0; i < count; i++) {
    shell_print(tab_matches[i]);
    if (tab_suffix[i])
      shell_putchar(tab_suffix[i]);
    shell_print("  ");
  }
  tab_redraw_prompt(input, pos);
}

static void tab_append_char(char *input, int *pos, char c) {
  if (*pos >= MAX_INPUT_LEN)
    return;
  input[*pos] = c;
  shell_putchar(c);
  (*pos)++;
  input[*pos] = '\0';
}

static int tab_apply_matches(char *input, int *pos, int token_prefix_len,
                             int match_count, int add_space_for_single) {
  int common_len;

  if (match_count <= 0)
    return 0;

  if (match_count == 1) {
    int i = token_prefix_len;
    while (tab_matches[0][i] && *pos < MAX_INPUT_LEN) {
      input[*pos] = tab_matches[0][i];
      shell_putchar(tab_matches[0][i]);
      (*pos)++;
      i++;
    }
    input[*pos] = '\0';
    if (tab_suffix[0])
      tab_append_char(input, pos, tab_suffix[0]);
    else if (add_space_for_single)
      tab_append_char(input, pos, ' ');
    return 1;
  }

  common_len = tab_common_prefix_len(match_count);
  if (common_len > token_prefix_len) {
    for (int i = token_prefix_len; i < common_len && *pos < MAX_INPUT_LEN; i++) {
      input[*pos] = tab_matches[0][i];
      shell_putchar(tab_matches[0][i]);
      (*pos)++;
    }
    input[*pos] = '\0';
    return 1;
  }

  tab_list_matches(input, *pos, match_count);
  return 1;
}

static int tab_file_allowed(const char *cmd, const char *prev_token,
                            const vfs_dirent_t *ent) {
  if (ent->type == VFS_TYPE_DIR)
    return 1;
  if (strcmp(cmd, "cd") == 0)
    return 0;
  if ((strcmp(cmd, "as") == 0 || strcmp(cmd, "cupidasm") == 0) &&
      strcmp(prev_token, "-o") != 0)
    return shell_ends_with(ent->name, ".asm");
  if ((strcmp(cmd, "cc") == 0 || strcmp(cmd, "cupidc") == 0 ||
       strcmp(cmd, "ccc") == 0) && strcmp(prev_token, "-o") != 0)
    return shell_ends_with(ent->name, ".cc");
  if (strcmp(cmd, "cupid") == 0)
    return shell_ends_with(ent->name, ".cup");
  return 1;
}

static int tab_complete_words(char *input, int *pos, int token_start,
                              const char *token, const char **words) {
  int count = 0;
  int prefix_len = (int)strlen(token);
  for (int i = 0; words[i]; i++) {
    if (shell_strncmp(words[i], token, (size_t)prefix_len) == 0)
      tab_add_match(words[i], ' ', &count);
  }
  return tab_apply_matches(input, pos, *pos - token_start, count, 1);
}

static int tab_complete_command_words(char *input, int *pos, int token_start,
                                      const char *cmd, const char *prev_token,
                                      const char *token, int arg_index) {
  static const char *sshd_words[] = {"start", "stop", "status", "passwd", NULL};
  static const char *theme_words[] = {"win95", "pastel", "dark", "contrast",
                                      "amber", "vapor", "temple", NULL};
  static const char *usb_words[] = {"hubs", "hc", NULL};
  static const char *smp_words[] = {"info", NULL};
  static const char *mount_words[] = {"fat16", "ramfs", "devfs", "iso9660", NULL};
  static const char *asm_words[] = {"-o", NULL};

  if ((strcmp(cmd, "as") == 0 || strcmp(cmd, "cupidasm") == 0 ||
       strcmp(cmd, "ccc") == 0) && token[0] == '-')
    return tab_complete_words(input, pos, token_start, token, asm_words);
  if (strcmp(prev_token, "-o") == 0)
    return 0;
  if (arg_index == 0 && strcmp(cmd, "sshd") == 0)
    return tab_complete_words(input, pos, token_start, token, sshd_words);
  if (arg_index == 0 && strcmp(cmd, "theme") == 0)
    return tab_complete_words(input, pos, token_start, token, theme_words);
  if (arg_index == 0 && strcmp(cmd, "usb") == 0)
    return tab_complete_words(input, pos, token_start, token, usb_words);
  if (arg_index == 0 && strcmp(cmd, "smp") == 0)
    return tab_complete_words(input, pos, token_start, token, smp_words);
  if (arg_index == 2 && strcmp(cmd, "mount") == 0)
    return tab_complete_words(input, pos, token_start, token, mount_words);
  return 0;
}

static void tab_resolve_dir_token(const char *token, int token_len,
                                  char *dir_path, char *name_prefix,
                                  int *name_prefix_len) {
  char dir_token[VFS_MAX_PATH];
  int last_slash = -1;

  for (int i = 0; i < token_len; i++) {
    if (token[i] == '/')
      last_slash = i;
  }

  if (last_slash >= 0) {
    if (last_slash == 0) {
      dir_token[0] = '/';
      dir_token[1] = '\0';
    } else {
      tab_copy_range(dir_token, VFS_MAX_PATH, token, 0, last_slash);
    }
    *name_prefix_len = token_len - last_slash - 1;
    tab_copy_range(name_prefix, VFS_MAX_NAME, token, last_slash + 1,
                   token_len);
  } else {
    dir_token[0] = '.';
    dir_token[1] = '\0';
    *name_prefix_len = token_len;
    tab_copy_range(name_prefix, VFS_MAX_NAME, token, 0, token_len);
  }

  shell_resolve_path(dir_token, dir_path);
}

static int tab_complete_path(char *input, int *pos, int token_start,
                             const char *cmd, const char *prev_token,
                             int command_position) {
  char token[VFS_MAX_PATH];
  char dir_path[VFS_MAX_PATH];
  char name_prefix[VFS_MAX_NAME];
  int name_prefix_len = 0;
  int token_len = *pos - token_start;
  int fd;
  int count = 0;
  vfs_dirent_t ent;

  if (token_len >= VFS_MAX_PATH)
    token_len = VFS_MAX_PATH - 1;
  tab_copy_range(token, VFS_MAX_PATH, input, token_start, token_start + token_len);
  tab_resolve_dir_token(token, token_len, dir_path, name_prefix,
                        &name_prefix_len);

  fd = vfs_open(dir_path, O_RDONLY);
  if (fd < 0)
    return 0;

  while (vfs_readdir(fd, &ent) > 0 && count < TAB_MAX_MATCHES) {
    if (shell_strncmp(ent.name, name_prefix, (size_t)name_prefix_len) != 0)
      continue;
    if (!command_position && !tab_file_allowed(cmd, prev_token, &ent))
      continue;
    tab_add_match(ent.name, ent.type == VFS_TYPE_DIR ? '/' : ' ', &count);
  }
  vfs_close(fd);

  return tab_apply_matches(input, pos, name_prefix_len, count, 0);
}

static void tab_add_bin_commands(const char *dir, const char *prefix,
                                 int prefix_len, int *count) {
  int fd = vfs_open(dir, O_RDONLY);
  vfs_dirent_t ent;
  if (fd < 0)
    return;

  while (vfs_readdir(fd, &ent) > 0 && *count < TAB_MAX_MATCHES) {
    char base[TAB_MAX_NAME];
    int n = 0;
    int base_len;

    if (ent.type != VFS_TYPE_FILE)
      continue;
    while (ent.name[n] && n < TAB_MAX_NAME - 1) {
      base[n] = ent.name[n];
      n++;
    }
    base[n] = '\0';
    base_len = n;
    if (shell_ends_with(base, ".cc")) {
      base_len -= 3;
      base[base_len] = '\0';
    }
    if (base_len <= 0)
      continue;
    if (shell_strncmp(base, prefix, (size_t)prefix_len) == 0)
      tab_add_match(base, ' ', count);
  }
  vfs_close(fd);
}

static void tab_complete_command(char *input, int *pos, int token_start) {
  char prefix[MAX_INPUT_LEN + 1];
  int prefix_len = *pos - token_start;
  int count = 0;

  if (prefix_len >= MAX_INPUT_LEN)
    prefix_len = MAX_INPUT_LEN;
  tab_copy_range(prefix, sizeof(prefix), input, token_start,
                 token_start + prefix_len);

  for (int i = 0; commands[i].name; i++) {
    if (shell_strncmp(commands[i].name, prefix, (size_t)prefix_len) == 0)
      tab_add_match(commands[i].name, ' ', &count);
  }

  tab_add_bin_commands("/bin", prefix, prefix_len, &count);
  tab_add_bin_commands("/home/bin", prefix, prefix_len, &count);

  if (count > 0)
    tab_apply_matches(input, pos, prefix_len, count, 1);
  else
    tab_complete_path(input, pos, token_start, "", "", 1);
}

static void tab_complete(char *input, int *pos) {
  int cmd_start = 0;
  int cmd_end;
  int token_start;
  int arg_index = -1;
  char cmd[MAX_INPUT_LEN + 1];
  char token[VFS_MAX_PATH];
  char prev_token[VFS_MAX_PATH];

  input[*pos] = '\0';
  while (cmd_start < *pos && tab_is_blank(input[cmd_start]))
    cmd_start++;

  token_start = *pos;
  while (token_start > cmd_start && !tab_is_blank(input[token_start - 1]))
    token_start--;

  cmd_end = cmd_start;
  while (cmd_end < *pos && !tab_is_blank(input[cmd_end]))
    cmd_end++;

  if (token_start <= cmd_start && *pos <= cmd_end) {
    tab_complete_command(input, pos, token_start);
    return;
  }

  tab_copy_range(cmd, sizeof(cmd), input, cmd_start, cmd_end);
  tab_copy_range(token, sizeof(token), input, token_start, *pos);
  prev_token[0] = '\0';

  {
    int scan = cmd_end;
    int last_start = -1;
    int last_end = -1;
    while (scan < token_start) {
      while (scan < token_start && tab_is_blank(input[scan]))
        scan++;
      if (scan >= token_start)
        break;
      last_start = scan;
      while (scan < token_start && !tab_is_blank(input[scan]))
        scan++;
      last_end = scan;
      arg_index++;
    }
    if (last_start >= 0)
      tab_copy_range(prev_token, sizeof(prev_token), input, last_start, last_end);
    else
      arg_index = 0;
  }

  if (tab_complete_command_words(input, pos, token_start, cmd, prev_token,
                                 token, arg_index))
    return;

  tab_complete_path(input, pos, token_start, cmd, prev_token, 0);
}

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

/* ── try_bin_dispatch: check if a resolved path is /bin/<app> and run it ─ * Returns true if handled, false if not a /bin path. */
static bool try_bin_dispatch(const char *resolved, const char *extra_args) {
  if (resolved[0] != '/' || resolved[1] != 'b' || resolved[2] != 'i' ||
      resolved[3] != 'n' || resolved[4] != '/') {
    return false;
  }

  const char *app = resolved + 5;
  serial_printf("[try_bin_dispatch] resolved='%s' app='%s'\n", resolved, app);
  {
    char app_name[VFS_MAX_NAME];
    int i = 0;
    while (app[i] && app[i] != '.' && i < VFS_MAX_NAME - 1) {
      app_name[i] = app[i];
      i++;
    }
    app_name[i] = '\0';
    if (app_launch_by_name(app_name, extra_args) ||
        app_launch_by_path(resolved, extra_args)) {
      return true;
    }
  }

  if (strcmp(app, "cupid") == 0) {
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
    /* Check if it's a .cc source file - JIT compile it */
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

  /* exec -d <file> : disassemble instead of execute */
  if (args[0] == '-' && args[1] == 'd' && args[2] == ' ') {
    char rpath[VFS_MAX_PATH];
    const char *file = args + 3;
    shell_resolve_path(file, rpath);
    if (shell_ends_with(rpath, ".cc")) {
      cupidc_dis(rpath, shell_print);
    } else {
      (void)dis_elf(rpath, shell_print);
    }
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

  /* Check if it's a .cc source file - JIT compile it */
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
  (void)app_launch_by_name("notepad", args);
}

static void shell_terminal_cmd(const char *args) {
  (void)app_launch_by_name("terminal", args);
}

static void shell_ps_cmd(const char *args) {
  (void)args;
  process_list();
}

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

static int shell_readline_cc_repl(char *buf, int max_len) {
  int pos = 0;
  for (;;) {
    char c = getchar();

    if (c == 4) { /* Ctrl+D */
      if (pos == 0) {
        return -1;
      }
      continue;
    }

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      buf[pos] = '\0';
      shell_putchar('\n');
      return pos;
    }

    if (c == '\b') {
      if (pos > 0) {
        pos--;
        shell_print("\b \b");
      }
      continue;
    }

    if (c && pos < max_len - 1) {
      buf[pos++] = c;
      shell_putchar(c);
    }
  }
}

static int shell_write_text_file(const char *path, const char *text) {
  int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0)
    return -1;

  uint32_t len = (uint32_t)strlen(text);
  uint32_t written = 0;
  while (written < len) {
    int w = vfs_write(fd, text + written, len - written);
    if (w <= 0) {
      vfs_close(fd);
      return -1;
    }
    written += (uint32_t)w;
  }

  vfs_close(fd);
  return 0;
}

static int shell_cc_line_brace_delta(const char *line) {
  int delta = 0;
  int in_string = 0;
  int in_char = 0;
  int escape = 0;

  for (int i = 0; line[i]; i++) {
    char c = line[i];

    if (escape) {
      escape = 0;
      continue;
    }

    if (c == '\\') {
      escape = 1;
      continue;
    }

    if (!in_char && c == '"') {
      in_string = !in_string;
      continue;
    }

    if (!in_string && c == '\'') {
      in_char = !in_char;
      continue;
    }

    if (in_string || in_char)
      continue;

    if (c == '{')
      delta++;
    else if (c == '}')
      delta--;
  }

  return delta;
}

static int shell_repl_stage_line(char *pending_buf, uint32_t *pending_len,
                                 int *brace_depth, const char *line,
                                 char *full_buf, uint32_t full_buf_size) {
  uint32_t line_len = (uint32_t)strlen(line);

  if (line_len == 0 && *pending_len == 0)
    return 0;

  if (*pending_len + line_len + 2u >= SHELL_REPL_PENDING_MAX) {
    shell_print("shell: REPL input too large\n");
    *pending_len = 0;
    pending_buf[0] = '\0';
    *brace_depth = 0;
    return -1;
  }

  memcpy(pending_buf + *pending_len, line, line_len);
  *pending_len += line_len;
  pending_buf[(*pending_len)++] = '\n';
  pending_buf[*pending_len] = '\0';

  *brace_depth += shell_cc_line_brace_delta(line);
  if (*brace_depth < 0)
    *brace_depth = 0;
  if (*brace_depth > 0)
    return 0;

  if (*pending_len + 1u > full_buf_size) {
    shell_print("shell: REPL input too large\n");
    *pending_len = 0;
    pending_buf[0] = '\0';
    *brace_depth = 0;
    return -1;
  }

  memcpy(full_buf, pending_buf, *pending_len);
  full_buf[*pending_len] = '\0';
  *pending_len = 0;
  pending_buf[0] = '\0';
  *brace_depth = 0;
  return 1;
}

static void shell_cc_repl(void) {
  enum { CC_REPL_LINE_MAX = 512, CC_REPL_SRC_MAX = 64 * 1024 };
  const char *tmp_path = "/cc_repl_tmp.cc";
  char *session_src = kmalloc(CC_REPL_SRC_MAX);
  char *pending_src = kmalloc(CC_REPL_SRC_MAX);
  char *candidate_src = kmalloc(CC_REPL_SRC_MAX);
  char line[CC_REPL_LINE_MAX];
  uint32_t src_len = 0;
  uint32_t pending_len = 0;
  int brace_depth = 0;

  if (!session_src || !pending_src || !candidate_src) {
    shell_print("cc: out of memory\n");
    if (session_src)
      kfree(session_src);
    if (pending_src)
      kfree(pending_src);
    if (candidate_src)
      kfree(candidate_src);
    return;
  }
  session_src[0] = '\0';
  pending_src[0] = '\0';
  candidate_src[0] = '\0';

  shell_print("CupidC v2 - Enter runs when blocks are complete. Ctrl+D to exit.\n");

  for (;;) {
    shell_print(brace_depth > 0 ? "..> " : "cc> ");
    int n = shell_readline_cc_repl(line, CC_REPL_LINE_MAX);
    if (n < 0) {
      shell_print("\n");
      break;
    }

    if (n == 0) {
      continue;
    }

    if (strcmp(line, ".exit") == 0 || strcmp(line, "exit") == 0) {
      break;
    }

    /* Stage line in pending snippet. */
    {
      uint32_t add_len = (uint32_t)n;
      if (pending_len + add_len + 2u >= CC_REPL_SRC_MAX) {
        shell_print("cc: session too large\n");
        continue;
      }
      memcpy(pending_src + pending_len, line, add_len);
      pending_len += add_len;
      pending_src[pending_len++] = '\n';
      pending_src[pending_len] = '\0';
    }

    brace_depth += shell_cc_line_brace_delta(line);
    if (brace_depth < 0)
      brace_depth = 0;

    if (brace_depth > 0) {
      continue;
    }

    /* Build candidate = committed session + pending snippet. */
    if (src_len + pending_len + 1u >= CC_REPL_SRC_MAX) {
      shell_print("cc: session too large\n");
      pending_len = 0;
      pending_src[0] = '\0';
      brace_depth = 0;
      continue;
    }
    memcpy(candidate_src, session_src, src_len);
    memcpy(candidate_src + src_len, pending_src, pending_len);
    candidate_src[src_len + pending_len] = '\0';

    if (shell_write_text_file(tmp_path, candidate_src) < 0) {
      shell_print("cc: failed to write temp source\n");
      continue;
    }

    if (cupidc_jit_status(tmp_path) == 0) {
      memcpy(session_src + src_len, pending_src, pending_len);
      src_len += pending_len;
      session_src[src_len] = '\0';
    }

    pending_len = 0;
    pending_src[0] = '\0';
    brace_depth = 0;
  }

  kfree(session_src);
  kfree(pending_src);
  kfree(candidate_src);
}

/*  *  CupidC Compiler Commands
 **/

/* cupidc <file.cc> - JIT compile and run */
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

/* cc [file.cc] - interactive CupidC REPL or file JIT */
static void shell_cc_cmd(const char *args) {
  if (!args || args[0] == '\0') {
    shell_cc_repl();
    return;
  }

  if (args[0] == '-' && args[1] == 'd' && args[2] == ' ') {
    char rpath[VFS_MAX_PATH];
    const char *file = args + 3;
    shell_resolve_path(file, rpath);
    cupidc_dis(rpath, shell_print);
    return;
  }

  /* cc <file.cc> behaves like cupidc <file.cc> */
  char rpath[VFS_MAX_PATH];
  shell_resolve_path(args, rpath);
  cupidc_jit(rpath);
}

static void shell_reset_cmd(const char *args) {
  (void)args;
  repl_reset();
  shell_print("REPL state reset\n");
}

/* ccc <file.cc> -o <output> - AOT compile to ELF binary */
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

/*
 *  CupidASM Assembler Commands
*/

/* as <file.asm> - JIT assemble and run */
static void shell_asm_cmd(const char *args) {
  if (!args || args[0] == '\0') {
    shell_print("Usage: as <file.asm>\n");
    shell_print("  Assemble and run an assembly source file\n");
    shell_print("  as -o <output> <file.asm>  - assemble to ELF binary\n");
    return;
  }

  /* Check for -o flag (AOT mode) */
  if (args[0] == '-' && args[1] == 'o') {
    int ai = 2;
    while (args[ai] == ' ') ai++;

    /* Parse output filename */
    char out[VFS_MAX_PATH];
    int oi = 0;
    while (args[ai] && args[ai] != ' ' && oi < VFS_MAX_PATH - 1) {
      out[oi++] = args[ai++];
    }
    out[oi] = '\0';

    /* Skip whitespace */
    while (args[ai] == ' ') ai++;

    /* Parse source filename */
    char src[VFS_MAX_PATH];
    int si = 0;
    while (args[ai] && args[ai] != ' ' && si < VFS_MAX_PATH - 1) {
      src[si++] = args[ai++];
    }
    src[si] = '\0';

    if (src[0] == '\0' || out[0] == '\0') {
      shell_print("Usage: as -o <output> <file.asm>\n");
      return;
    }

    char rsrc[VFS_MAX_PATH];
    char rout[VFS_MAX_PATH];
    shell_resolve_path(src, rsrc);
    shell_resolve_path(out, rout);
    as_aot(rsrc, rout);
    return;
  }

  char rpath[VFS_MAX_PATH];
  shell_resolve_path(args, rpath);
  as_jit(rpath);
}

/* cupidasm <file.asm> -o <output> - AOT assemble to ELF binary */
static void shell_cupidasm_cmd(const char *args) {
  if (!args || args[0] == '\0') {
    shell_print("Usage: cupidasm <file.asm> [-o <output>]\n");
    shell_print("   or: cupidasm -o <output> <file.asm>\n");
    shell_print("  Assemble source to ELF binary\n");
    return;
  }

  /* Parse up to 3 whitespace-delimited tokens. */
  char tok0[VFS_MAX_PATH];
  char tok1[VFS_MAX_PATH];
  char tok2[VFS_MAX_PATH];
  char src[VFS_MAX_PATH];
  char out[VFS_MAX_PATH];
  int ai = 0;

  tok0[0] = '\0';
  tok1[0] = '\0';
  tok2[0] = '\0';
  src[0] = '\0';
  out[0] = '\0';

  while (args[ai] == ' ') ai++;

  int i = 0;
  while (args[ai] && args[ai] != ' ' && i < VFS_MAX_PATH - 1) {
    tok0[i++] = args[ai++];
  }
  tok0[i] = '\0';

  while (args[ai] == ' ') ai++;

  i = 0;
  while (args[ai] && args[ai] != ' ' && i < VFS_MAX_PATH - 1) {
    tok1[i++] = args[ai++];
  }
  tok1[i] = '\0';

  while (args[ai] == ' ') ai++;

  i = 0;
  while (args[ai] && args[ai] != ' ' && i < VFS_MAX_PATH - 1) {
    tok2[i++] = args[ai++];
  }
  tok2[i] = '\0';

  while (args[ai] == ' ') ai++;
  if (args[ai] != '\0' || tok0[0] == '\0') {
    shell_print("Usage: cupidasm <file.asm> [-o <output>]\n");
    shell_print("   or: cupidasm -o <output> <file.asm>\n");
    return;
  }

  /* Accept both orders:
   *   cupidasm <src> -o <out>
   *   cupidasm -o <out> <src>
   * plus:
   *   cupidasm <src>         (default output derived from source)
*/
  if (tok1[0] == '\0') {
    int slen = 0, oi = 0;
    while (tok0[slen]) slen++;
    if (slen > 4 && tok0[slen - 4] == '.' && tok0[slen - 3] == 'a' &&
        tok0[slen - 2] == 's' && tok0[slen - 1] == 'm') {
      for (int k = 0; k < slen - 4 && oi < VFS_MAX_PATH - 1; k++) {
        out[oi++] = tok0[k];
      }
    } else {
      for (int k = 0; k < slen && oi < VFS_MAX_PATH - 1; k++) {
        out[oi++] = tok0[k];
      }
    }
    out[oi] = '\0';
    strcpy(src, tok0);
  } else if (strcmp(tok0, "-o") == 0 && tok1[0] && tok2[0]) {
    strcpy(out, tok1);
    strcpy(src, tok2);
  } else if (strcmp(tok1, "-o") == 0 && tok2[0]) {
    strcpy(src, tok0);
    strcpy(out, tok2);
  } else {
    shell_print("Usage: cupidasm <file.asm> [-o <output>]\n");
    shell_print("   or: cupidasm -o <output> <file.asm>\n");
    return;
  }

  /* Resolve paths */
  char rsrc[VFS_MAX_PATH];
  char rout[VFS_MAX_PATH];
  shell_resolve_path(src, rsrc);
  shell_resolve_path(out, rout);

  as_aot(rsrc, rout);
}

static void shell_dis_cmd(const char *args) {
  char path[VFS_MAX_PATH];
  char rpath[VFS_MAX_PATH];
  int ai = 0;
  int pi = 0;
  int len;

  if (!args || args[0] == '\0') {
    shell_print("Usage: dis <file.elf|file.cc>\n");
    return;
  }

  while (args[ai] == ' ')
    ai++;
  while (args[ai] && args[ai] != ' ' && pi < VFS_MAX_PATH - 1) {
    path[pi++] = args[ai++];
  }
  path[pi] = '\0';

  if (path[0] == '\0') {
    shell_print("Usage: dis <file.elf|file.cc>\n");
    return;
  }

  shell_resolve_path(path, rpath);
  len = (int)strlen(rpath);
  if (len >= 3 && rpath[len - 3] == '.' && rpath[len - 2] == 'c' &&
      rpath[len - 1] == 'c') {
    cupidc_dis(rpath, shell_print);
  } else {
    (void)dis_elf(rpath, shell_print);
  }
}

static void shell_theme_cmd(const char *args) {
  const ui_theme_t *t = NULL;
  if (!args || !args[0]) {
    shell_print("Themes: win95 pastel dark contrast amber vapor temple\n");
    return;
  }
  while (*args == ' ') args++;
  if (strcmp(args, "win95") == 0)         t = &UI_THEME_WINDOWS95;
  else if (strcmp(args, "pastel") == 0)   t = &UI_THEME_PASTEL_DREAM;
  else if (strcmp(args, "dark") == 0)     t = &UI_THEME_DARK_MODE;
  else if (strcmp(args, "contrast") == 0) t = &UI_THEME_HIGH_CONTRAST;
  else if (strcmp(args, "amber") == 0)    t = &UI_THEME_RETRO_AMBER;
  else if (strcmp(args, "vapor") == 0)    t = &UI_THEME_VAPORWAVE;
  else if (strcmp(args, "temple") == 0)   t = &UI_THEME_TEMPLE;
  if (!t) {
    shell_print("theme: unknown name\n");
    return;
  }
  ui_theme_set(t);
  shell_print("theme set. redraw windows to see it.\n");
}

static void shell_temple_cmd(const char *args) {
  (void)args;
  ui_theme_set(&UI_THEME_TEMPLE);
  shell_print("+++ In nomine Patris +++\n");
  shell_print("TempleOS theme active. HolyC lives on.\n");
}

static void shell_adam_cmd(const char *args) {
  (void)args;
  process_list_adam();
}

/*
 *  mount [<src> <target> [<type>]]
 *
 *  With no args: list currently mounted filesystems.
 *  With args:    mount <src> at <target>, optionally with <type>.
 *                If <type> is omitted and <src> ends in ".iso"
 *                (case-insensitive), default to "iso9660".
*/
static void shell_mount_cmd(const char *args) {
  /* Skip leading whitespace (treat NULL as empty). */
  const char *p = args ? args : "";
  while (*p == ' ' || *p == '\t')
    p++;

  /* No args: list mounts (mirrors /bin/mount.cc behaviour). */
  if (*p == '\0') {
    int count = vfs_mount_count();
    if (count <= 0) {
      shell_print("No filesystems mounted.\n");
      return;
    }
    for (int i = 0; i < count; i++) {
      const vfs_mount_t *m = vfs_get_mount(i);
      if (!m)
        continue;
      const char *name = (m->ops && m->ops->name) ? m->ops->name : "?";
      shell_print(name);
      shell_print(" on ");
      shell_print(m->path);
      shell_print("\n");
    }
    return;
  }

  /* Parse: <src> <target> [<type>] */
  char src[VFS_MAX_PATH];
  char target[VFS_MAX_PATH];
  char type_buf[32];
  int si = 0, ti = 0, yi = 0;

  while (*p && *p != ' ' && *p != '\t' && si < (int)sizeof(src) - 1) {
    src[si++] = *p++;
  }
  src[si] = '\0';

  while (*p == ' ' || *p == '\t')
    p++;

  while (*p && *p != ' ' && *p != '\t' && ti < (int)sizeof(target) - 1) {
    target[ti++] = *p++;
  }
  target[ti] = '\0';

  while (*p == ' ' || *p == '\t')
    p++;

  while (*p && *p != ' ' && *p != '\t' && yi < (int)sizeof(type_buf) - 1) {
    type_buf[yi++] = *p++;
  }
  type_buf[yi] = '\0';

  if (src[0] == '\0' || target[0] == '\0') {
    shell_print("usage: mount <src> <target> [<type>]\n");
    return;
  }

  const char *type = type_buf[0] ? type_buf : NULL;

  /* Auto-detect .iso extension if fstype was not specified. */
  if (!type || type[0] == '\0') {
    size_t sn = strlen(src);
    if (sn >= 4 &&
        (src[sn - 4] == '.') &&
        (src[sn - 3] == 'i' || src[sn - 3] == 'I') &&
        (src[sn - 2] == 's' || src[sn - 2] == 'S') &&
        (src[sn - 1] == 'o' || src[sn - 1] == 'O')) {
      type = "iso9660";
    }
  }

  if (!type || type[0] == '\0') {
    shell_print("mount: unable to infer fstype; specify explicitly\n");
    return;
  }

  int rc = vfs_mount(src, target, type);
  if (rc == 0) {
    shell_print("mount: ok\n");
  } else {
    shell_print("mount: failed (");
    /* rc may be negative; shell_print_int takes uint32_t, so just print
     * the raw unsigned representation which is sufficient for diagnostics.*/
    shell_print_int((uint32_t)rc);
    shell_print(")\n");
  }
}

/*
 *  umount <target>
*/
static void shell_umount_cmd(const char *args) {
  const char *p = args ? args : "";
  while (*p == ' ' || *p == '\t')
    p++;
  if (!*p) {
    shell_print("usage: umount <target>\n");
    return;
  }

  /* Extract target (single word). */
  char target[VFS_MAX_PATH];
  int ti = 0;
  while (*p && *p != ' ' && *p != '\t' && ti < (int)sizeof(target) - 1) {
    target[ti++] = *p++;
  }
  target[ti] = '\0';

  int rc = vfs_umount(target);
  if (rc == 0) {
    shell_print("umount: ok\n");
  } else {
    shell_print("umount: failed (");
    shell_print_int((uint32_t)rc);
    shell_print(")\n");
  }
}

/*
 *  swapinit [pool_kb]
*/
static uint32_t swap_shell_parse_uint_or(const char *s, uint32_t dflt) {
  if (!s) return dflt;
  while (*s == ' ' || *s == '\t') s++;
  if (!*s) return dflt;
  uint32_t v = 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10u + (uint32_t)(*s - '0');
    s++;
  }
  return v;
}

static void shell_swapinit_cmd(const char *args) {
  uint32_t pool_kb = swap_shell_parse_uint_or(args, 4096);   /* default 4 MB */
  uint32_t pool_bytes = pool_kb * 1024u;
  int rc = swap_init("/disk/swap.bin", pool_bytes);
  if (rc == 0) {
    shell_print("swapinit: ok\n");
  } else {
    shell_print("swapinit: failed rc=");
    shell_print_int((uint32_t)(rc < 0 ? -rc : rc));
    shell_print("\n");
  }
}

static void shell_swapstats_cmd(const char *args) {
    (void)args;
    swap_stats_t s;
    swap_stats(&s);
    shell_print("handles in use: ");
    shell_print_int(s.handles_in_use);
    shell_print(" / ");
    shell_print_int(s.handles_total);
    shell_print("\npinned:         ");
    shell_print_int(s.pinned_count);
    shell_print("\nevictions:      ");
    shell_print_int(s.evictions);
    shell_print("\nclass 0 (1K):   disk ");
    shell_print_int(s.disk_alloc[0]); shell_print("/"); shell_print_int(s.disk_cap[0]);
    shell_print("  ram ");
    shell_print_int(s.ram_alloc[0]); shell_print("/"); shell_print_int(s.ram_cap[0]);
    shell_print("\nclass 1 (4K):   disk ");
    shell_print_int(s.disk_alloc[1]); shell_print("/"); shell_print_int(s.disk_cap[1]);
    shell_print("  ram ");
    shell_print_int(s.ram_alloc[1]); shell_print("/"); shell_print_int(s.ram_cap[1]);
    shell_print("\nclass 2 (16K):  disk ");
    shell_print_int(s.disk_alloc[2]); shell_print("/"); shell_print_int(s.disk_cap[2]);
    shell_print("  ram ");
    shell_print_int(s.ram_alloc[2]); shell_print("/"); shell_print_int(s.ram_cap[2]);
    shell_print("\nclass 3 (64K):  disk ");
    shell_print_int(s.disk_alloc[3]); shell_print("/"); shell_print_int(s.disk_cap[3]);
    shell_print("  ram ");
    shell_print_int(s.ram_alloc[3]); shell_print("/"); shell_print_int(s.ram_cap[3]);
    shell_print("\n");
}

static const char *usb_class_name(uint8_t cls, uint8_t sub, uint8_t proto) {
    switch (cls) {
    case 0x00: return "(per-interface)";
    case 0x01: return "Audio";
    case 0x02: return "CDC/Communications";
    case 0x03:
        if (sub == 0x01 && proto == 0x01) return "HID Keyboard";
        if (sub == 0x01 && proto == 0x02) return "HID Mouse";
        return "HID";
    case 0x05: return "Physical";
    case 0x06: return "Image";
    case 0x07: return "Printer";
    case 0x08: return "Mass Storage";
    case 0x09: return "Hub";
    case 0x0A: return "CDC Data";
    case 0x0B: return "Smart Card";
    case 0x0D: return "Content Security";
    case 0x0E: return "Video";
    case 0x0F: return "Personal Healthcare";
    case 0x10: return "Audio/Video";
    case 0xDC: return "Diagnostic";
    case 0xE0: return "Wireless Controller";
    case 0xEF: return "Miscellaneous";
    case 0xFE: return "Application-specific";
    case 0xFF: return "Vendor-specific";
    default:   return "unknown";
    }
}

static const char *usb_speed_name(uint8_t s) {
    return (s == 1) ? "LS" : (s == 2) ? "FS" : (s == 3) ? "HS" : "?";
}

static void shell_usb_cmd(const char *args) {
    /* Skip whitespace */
    while (args && *args == ' ') args++;

    if (!args || *args == 0) {
        /* Default: list devices */
        int any = 0;
        for (int i = 0; ; i++) {
            usb_device_t *d = usb_get_device(i);
            if (!d) break;
            any = 1;
            shell_print("[");
            shell_print_int(d->address);
            shell_print("] ");
            shell_print(usb_speed_name(d->speed));
            shell_print(" vid=");
            shell_print_int((uint32_t)d->vendor_id);
            shell_print(" pid=");
            shell_print_int((uint32_t)d->product_id);
            shell_print(" ");
            shell_print(usb_class_name(d->class_code, d->subclass, d->protocol));
            if (d->driver && d->driver->name) {
                shell_print(" [");
                shell_print(d->driver->name);
                shell_print("]");
            }
            if (d->parent_hub) {
                shell_print(" parent=");
                shell_print_int(d->parent_hub->address);
                shell_print(":");
                shell_print_int(d->parent_port);
            }
            shell_print("\n");
        }
        if (!any) shell_print("no USB devices\n");
        return;
    }

    if (args[0] == 'h' && args[1] == 'u' && args[2] == 'b' && args[3] == 's') {
        int any = 0;
        for (int i = 0; ; i++) {
            usb_device_t *d = usb_get_device(i);
            if (!d) break;
            if (d->class_code != 0x09) continue;
            any = 1;
            for (uint8_t p = 0; p < d->hub_depth; p++) shell_print("  ");
            shell_print("hub addr=");
            shell_print_int(d->address);
            shell_print(" depth=");
            shell_print_int(d->hub_depth);
            shell_print("\n");
        }
        if (!any) shell_print("no hubs\n");
        return;
    }

    if (args[0] == 'h' && args[1] == 'c') {
        shell_print("(see boot log for HC init messages)\n");
        return;
    }

    shell_print("usage: usb | usb hubs | usb hc\n");
}

static void shell_pci_cmd(const char *args) {
    (void)args;
    int n = pci_device_count();
    if (n == 0) { shell_print("no PCI devices\n"); return; }
    for (int i = 0; i < n; i++) {
        pci_device_t *p = pci_get_device(i);
        if (!p) break;
        shell_print("[");
        shell_print_int((uint32_t)i);
        shell_print("] ");
        shell_print_int(p->bus);
        shell_print(":");
        shell_print_int(p->device);
        shell_print(".");
        shell_print_int(p->function);
        shell_print(" vid=");
        shell_print_int(p->vendor_id);
        shell_print(" did=");
        shell_print_int(p->device_id);
        shell_print(" class=");
        shell_print_int(p->class_code);
        shell_print(":");
        shell_print_int(p->subclass);
        shell_print(":");
        shell_print_int(p->prog_if);
        shell_print(" irq=");
        shell_print_int(p->irq_line);
        /* USB controller hint: class 0x0C subclass 0x03 */
        if (p->class_code == 0x0C && p->subclass == 0x03) {
            shell_print(p->prog_if == 0x00 ? " [UHCI]"
                      : p->prog_if == 0x10 ? " [OHCI unsupported]"
                      : p->prog_if == 0x20 ? " [EHCI]"
                      : p->prog_if == 0x30 ? " [xHCI unsupported]"
                      : " [USB ?]");
        }
        shell_print("\n");
    }
}

static void shell_smp_cmd(const char *args) {
    while (args && *args == ' ') args++;

    if (args && args[0] == 'i' && args[1] == 'n' && args[2] == 'f' && args[3] == 'o') {
        shell_print("bkl: ");
        shell_print(bkl_held_by_this_cpu() ? "held\n" : "free\n");
        shell_print("cpus: ");
        shell_print_int((uint32_t)smp_cpu_count());
        shell_print("\nme: ");
        shell_print_int((uint32_t)smp_current_cpu());
        shell_print("\n");
        return;
    }

    int n = smp_cpu_count();
    for (int i = 0; i < n; i++) {
        shell_print("[");
        shell_print_int((uint32_t)i);
        shell_print("] apic=");
        shell_print_int(cpus[i].apic_id);
        shell_print(" online=");
        shell_print_int(cpus[i].online);
        shell_print(" preempts=");
        shell_print_int((uint32_t)cpus[i].preempt_count);
        shell_print(" current_pid=");
        shell_print_int(cpus[i].current_pid);
        shell_print("\n");
    }
}

static void shell_ifconfig_cmd(const char *args) {
    (void)args;
    net_if_t *nif = net_if_primary();
    if (!nif) { shell_print("no NIC\n"); return; }
    shell_print(nif->name);
    shell_print(" mac=");
    for (int i = 0; i < 6; i++) {
        shell_print_int(nif->mac[i]);
        if (i < 5) shell_print(":");
    }
    shell_print("\n ip=");
    uint8_t *p = (uint8_t*)&nif->ipv4_addr;
    shell_print_int(p[0]); shell_print(".");
    shell_print_int(p[1]); shell_print(".");
    shell_print_int(p[2]); shell_print(".");
    shell_print_int(p[3]);
    shell_print(" rx="); shell_print_int((uint32_t)nif->rx_packets);
    shell_print(" tx="); shell_print_int((uint32_t)nif->tx_packets);
    shell_print("\n");
}

static void shell_ping_cmd(const char *args) {
    char host[64];
    int hi = 0;
    int count = 4;
    uint32_t ip = 0;
    uint16_t id;
    int sent = 0, recvd = 0;
    int i;

    while (args && *args == ' ') args++;
    if (!args || !*args) { shell_print("usage: ping <host> [count]\n"); return; }

    while (*args && *args != ' ' && hi < 63) host[hi++] = *args++;
    host[hi] = 0;
    while (*args == ' ') args++;
    if (*args) {
        int v = 0;
        while (*args >= '0' && *args <= '9') { v = v * 10 + (*args - '0'); args++; }
        if (v > 0 && v <= 64) count = v;
    }

    if (ip_parse(host, &ip) != 0 && dns_resolve(host, &ip) != 0) {
        shell_print("resolve failed\n"); return;
    }
    {
        uint8_t *p = (uint8_t*)&ip;
        shell_print("PING "); shell_print(host); shell_print(" (");
        shell_print_int(p[0]); shell_print(".");
        shell_print_int(p[1]); shell_print(".");
        shell_print_int(p[2]); shell_print(".");
        shell_print_int(p[3]); shell_print(")\n");
    }

    id = (uint16_t)(timer_get_uptime_ms() & 0xFFFFu);
    for (i = 0; i < count; i++) {
        uint16_t seq = (uint16_t)(i + 1);
        int rtt;
        if (icmp_send_echo(ip, id, seq, 32u) < 0) {
            shell_print("send failed\n"); continue;
        }
        sent++;
        rtt = icmp_wait_reply(ip, id, seq, 3000u);
        if (rtt >= 0) {
            recvd++;
            shell_print("seq="); shell_print_int((uint32_t)seq);
            shell_print(" rtt_ms="); shell_print_int((uint32_t)rtt);
            shell_print("\n");
        } else {
            shell_print("seq="); shell_print_int((uint32_t)seq);
            shell_print(" timeout\n");
        }
        if (i < count - 1) {
            /* 1 second between pings, via TSC (works with IF=0). */
            int k;
            for (k = 0; k < 1000; k++) timer_delay_us(1000u);
        }
    }
    shell_print("sent="); shell_print_int((uint32_t)sent);
    shell_print(" recv="); shell_print_int((uint32_t)recvd);
    shell_print("\n");
}

static void shell_netstat_cmd(const char *args) {
    (void)args;
    for (int i = 0; i < SOCKET_MAX; i++) {
        if (!sockets[i].in_use) continue;
        shell_print("["); shell_print_int((uint32_t)i); shell_print("] ");
        shell_print(sockets[i].type == SOCK_TYPE_UDP ? "UDP" :
                    sockets[i].type == SOCK_TYPE_TCP ? "TCP" : "UNK");
        shell_print(" state="); shell_print_int((uint32_t)sockets[i].tcp_state);
        shell_print(" lport="); shell_print_int(sockets[i].local_port);
        shell_print(" rport="); shell_print_int(sockets[i].remote_port);
        shell_print("\n");
    }
}

static void shell_arp_cmd(const char *args) {
    uint32_t ips[16];
    uint8_t macs[16][6];
    int n, i, j;
    (void)args;
    n = arp_get_entries(ips, macs, 16);
    if (n == 0) { shell_print("(empty)\n"); return; }
    for (i = 0; i < n; i++) {
        uint8_t *p = (uint8_t*)&ips[i];
        shell_print_int(p[0]); shell_print(".");
        shell_print_int(p[1]); shell_print(".");
        shell_print_int(p[2]); shell_print(".");
        shell_print_int(p[3]);
        shell_print(" -> ");
        for (j = 0; j < 6; j++) {
            shell_print_int((uint32_t)macs[i][j]);
            if (j < 5) shell_print(":");
        }
        shell_print("\n");
    }
}

static void shell_resolve_cmd(const char *args) {
    while (args && *args == ' ') args++;
    if (!args || !*args) { shell_print("usage: resolve <host>\n"); return; }
    uint32_t ip = 0;
    if (ip_parse(args, &ip) != 0 && dns_resolve(args, &ip) != 0) {
        shell_print("resolve failed\n"); return;
    }
    uint8_t *p = (uint8_t*)&ip;
    shell_print_int(p[0]); shell_print(".");
    shell_print_int(p[1]); shell_print(".");
    shell_print_int(p[2]); shell_print(".");
    shell_print_int(p[3]); shell_print("\n");
}

static int shell_parse_u16_arg(const char *s, uint16_t *out) {
    uint32_t v = 0;
    if (!s || !*s) return -1;
    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (uint32_t)(*s - '0');
        if (v > 65535u) return -1;
        s++;
    }
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '\0' || v == 0u) return -1;
    *out = (uint16_t)v;
    return 0;
}

static void shell_sshd_cmd(const char *args) {
    if (!args || !*args || strcmp(args, "start") == 0) {
        if (sshd_start(22u) == 0) {
            shell_print("sshd: listening on port 22\n");
        } else {
            shell_print("sshd: start failed\n");
        }
        return;
    }
    if (strcmp(args, "stop") == 0) {
        sshd_stop();
        shell_print("sshd: stopped\n");
        return;
    }
    if (strcmp(args, "status") == 0) {
        shell_print("sshd: ");
        if (sshd_is_running()) {
            shell_print("running port ");
            shell_print_int((uint32_t)sshd_port());
            shell_print("\n");
        } else {
            shell_print("stopped\n");
        }
        return;
    }
    if (strncmp(args, "start ", 6u) == 0) {
        uint16_t port;
        if (shell_parse_u16_arg(args + 6, &port) != 0) {
            shell_print("usage: sshd start [port]\n");
            return;
        }
        if (sshd_start(port) == 0) {
            shell_print("sshd: listening on port ");
            shell_print_int((uint32_t)port);
            shell_print("\n");
        } else {
            shell_print("sshd: start failed\n");
        }
        return;
    }
    if (strncmp(args, "passwd ", 7u) == 0) {
        const char *pw = args + 7;
        if (!*pw) {
            shell_print("usage: sshd passwd <password>\n");
            return;
        }
        if (sshd_set_password(pw) == 0) {
            shell_print("sshd: password updated for root\n");
        } else {
            shell_print("sshd: password update failed\n");
        }
        return;
    }
    shell_print("usage: sshd [start [port]|stop|status|passwd <password>]\n");
}

/* doom shell builtin - calls into the platform shim's doom_main(). */
extern int doom_main(int argc, char **argv);

#define DOOM_ARGV_MAX 16
#define DOOM_ARG_BUF  256

static void shell_doom_cmd(const char *args) {
    static char doom_arg_buf[DOOM_ARG_BUF];
    static char doom_argv0[] = "doom";
    static char *doom_argv[DOOM_ARGV_MAX];
    int doom_argc = 0;
    char *p;

    /* argv[0] = "doom" */
    doom_argv[doom_argc++] = doom_argv0;

    /* Tokenise the args string */
    if (args && *args) {
        /* Copy into mutable buffer so we can place NUL terminators */
        int i = 0;
        while (args[i] && i < DOOM_ARG_BUF - 1) {
            doom_arg_buf[i] = args[i];
            i++;
        }
        doom_arg_buf[i] = '\0';

        p = doom_arg_buf;
        while (*p && doom_argc < DOOM_ARGV_MAX - 1) {
            /* Skip whitespace */
            while (*p == ' ' || *p == '\t') { p++; }
            if (!*p) { break; }
            doom_argv[doom_argc++] = p;
            /* Advance to end of token */
            while (*p && *p != ' ' && *p != '\t') { p++; }
            if (*p) { *p++ = '\0'; }
        }
    }

    doom_main(doom_argc, doom_argv);
}

void shell_execute_line(const char *line) {
  if (!line || line[0] == '\0')
    return;
  execute_command_inner(line, 0);
}

static int shell_program_path_exists(const char *path) {
  vfs_stat_t st;
  return (vfs_stat(path, &st) >= 0 && st.type == VFS_TYPE_FILE) ? 1 : 0;
}

static void shell_make_bin_candidate(char *out, const char *prefix,
                                     const char *cmd, const char *suffix) {
  int oi = 0;
  int ci = 0;
  int si = 0;

  while (prefix[oi] && oi < VFS_MAX_PATH - 1) {
    out[oi] = prefix[oi];
    oi++;
  }
  while (cmd[ci] && oi < VFS_MAX_PATH - 1) {
    out[oi++] = cmd[ci++];
  }
  while (suffix && suffix[si] && oi < VFS_MAX_PATH - 1) {
    out[oi++] = suffix[si++];
  }
  out[oi] = '\0';
}

static int shell_should_try_repl(const char *input) {
  char cmd[MAX_INPUT_LEN];
  char path[VFS_MAX_PATH];
  int i = 0;

  while (input[i] && input[i] != ' ' && input[i] != '\t' &&
         i < MAX_INPUT_LEN - 1) {
    cmd[i] = input[i];
    i++;
  }
  cmd[i] = '\0';
  if (cmd[0] == '\0')
    return 0;

  for (int j = 0; commands[j].name; j++) {
    if (strcmp(cmd, commands[j].name) == 0)
      return 0;
  }

  shell_resolve_path(cmd, path);
  if (shell_program_path_exists(path))
    return 0;

  shell_make_bin_candidate(path, "/bin/", cmd, "");
  if (shell_program_path_exists(path))
    return 0;
  shell_make_bin_candidate(path, "/bin/", cmd, ".cc");
  if (shell_program_path_exists(path))
    return 0;
  shell_make_bin_candidate(path, "/home/bin/", cmd, "");
  if (shell_program_path_exists(path))
    return 0;
  shell_make_bin_candidate(path, "/home/bin/", cmd, ".cc");
  if (shell_program_path_exists(path))
    return 0;

  return 1;
}

// Find and execute a command
static void execute_command(const char *input) {
  execute_command_inner(input, 1);
}

static void execute_command_inner(const char *input, int try_repl) {
  char normalized_input[SHELL_REPL_PENDING_MAX];
  int ni = 0;
  int start = 0;
  int end = 0;

  // Skip empty input
  if (!input || input[0] == '\0') {
    return;
  }

  while (input[start] == ' ' || input[start] == '\t' || input[start] == '\n' ||
         input[start] == '\r') {
    start++;
  }

  end = start;
  while (input[end]) {
    end++;
  }
  while (end > start &&
         (input[end - 1] == ' ' || input[end - 1] == '\t' ||
          input[end - 1] == '\n' || input[end - 1] == '\r')) {
    end--;
  }

  while (start < end && ni < SHELL_REPL_PENDING_MAX - 1) {
    normalized_input[ni++] = input[start++];
  }
  normalized_input[ni] = '\0';
  input = normalized_input;

  if (input[0] == '\0') {
    return;
  }

  /* Check for output redirection (> or >>) */
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

  if (try_repl && shell_should_try_repl(input) && repl_eval(input) == 0)
    goto redir_done;

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

  if (app_launch_by_name(cmd, args))
    goto redir_done;

  /* Handle /bin/<app> execution - resolve the path first */
  {
    char resolved[VFS_MAX_PATH];
    shell_resolve_path(cmd, resolved);
    if (try_bin_dispatch(resolved, args))
      goto redir_done;
  }

  /*
   *  TempleOS-style auto-discovery: check /bin/ and /home/bin/
   *  for programs matching the command name.  This lets CupidC
   *  programs be added to the OS without recompiling the kernel.
   *
   *  Search order:
   *    1. /bin/<cmd>        - ELF/CUPD binary (ramfs)
   *    2. /bin/<cmd>.cc     - CupidC source   (ramfs)
   *    3. /home/bin/<cmd>   - ELF/CUPD binary (disk)
   *    4. /home/bin/<cmd>.cc - CupidC source  (disk, persistent)
*/
  {
    char bin_path[VFS_MAX_PATH];
    vfs_stat_t bin_st;

    /* 1. /bin/<cmd> (ELF binary in ramfs) */
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
        /* Not an ELF - might be a stub file, continue to try .cc version */
      }
    }

    /* /bin/<cmd>.cc (CupidC source in ramfs) */
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

    /* /home/bin/<cmd> (ELF binary on disk) */
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

    /* /home/bin/<cmd>.cc (CupidC source on disk) */
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
    if (shell_ends_with(run_path, ".asm")) {
      as_jit(run_path);
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

  /* Handle bare .cup files: script.cup -> cupid script.cup */
  if (shell_ends_with(cmd, ".cup")) {
    shell_cupid(input);
    goto redir_done;
  }

  /* Handle bare .cc files: program.cc -> cupidc program.cc */
  if (shell_ends_with(cmd, ".cc")) {
    char cc_path[VFS_MAX_PATH];
    shell_resolve_path(cmd, cc_path);
    cupidc_jit(cc_path);
    goto redir_done;
  }

  /* Handle bare .asm files: program.asm -> as program.asm */
  if (shell_ends_with(cmd, ".asm")) {
    char asm_path[VFS_MAX_PATH];
    shell_resolve_path(cmd, asm_path);
    as_jit(asm_path);
    goto redir_done;
  }

  // Handle unknown command
  shell_print("Unknown command: ");
  shell_print(cmd);
  shell_print("\n");

redir_done:
  /* Flush redirect buffer to file if active */
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
  char full_input[SHELL_REPL_PENDING_MAX];
  int pos = 0;    // length of input
  int cursor = 0; // cursor position within input

  repl_init();
  shell_print("cupid-os shell\n");
  shell_repl_pending_len = 0;
  shell_repl_pending[0] = '\0';
  shell_repl_brace_depth = 0;
  shell_print_prompt();

  while (1) {
    key_event_t event;
    if (!keyboard_read_event(&event)) {
      process_yield();
      continue;
    }
    char c = event.character;

    // History navigation with up/down arrows
    if (event.character == 0 && event.scancode == SCANCODE_F7) {
      shell_putchar('\n');
      execute_command("godspeak");
      pos = 0;
      cursor = 0;
      history_view = -1;
      memset(input, 0, sizeof(input));
      shell_print_prompt();
      continue;
    }

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
      shell_repl_pending_len = 0;
      shell_repl_pending[0] = '\0';
      shell_repl_brace_depth = 0;
      shell_print_prompt();
      continue;
    }

    if (c == '\n') {
      input[pos] = 0; // Ensure null termination
      shell_putchar('\n');
      {
        int repl_ready = shell_repl_stage_line(
            shell_repl_pending, &shell_repl_pending_len, &shell_repl_brace_depth,
            input, full_input, sizeof(full_input));
        if (repl_ready > 0) {
          history_record(full_input);
          execute_command(full_input);
        }
      }
      pos = 0;
      cursor = 0;
      history_view = -1;
      memset(input, 0, sizeof(input)); // Clear buffer
      shell_print_prompt();
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

/*
 *  GUI-mode shell key handler
 *
 *  Called from terminal_app.c when a key is pressed while the
 *  terminal window is focused.  Mirrors the text-mode shell_run()
 *  logic but writes to the GUI buffer instead of VGA text memory.
*/

static char gui_input[MAX_INPUT_LEN + 1];
static int gui_input_pos = 0;    // length of input
static int gui_input_cursor = 0; // cursor position within input
static int gui_history_view = -1;
static char gui_pending_command[SHELL_REPL_PENDING_MAX];
static int gui_pending_command_state = 0; /* 0 idle, 1 queued, 2 running */

static void shell_gui_queue_command(const char *input) {
  int i = 0;

  if (!input)
    return;
  if (gui_pending_command_state != 0) {
    shell_gui_print("terminal: command already running\n");
    return;
  }

  while (input[i] && i < SHELL_REPL_PENDING_MAX - 1) {
    gui_pending_command[i] = input[i];
    i++;
  }
  gui_pending_command[i] = '\0';
  gui_pending_command_state = 1;
}

void shell_gui_insert_text(const char *text) {
  if (!text || output_mode != SHELL_OUTPUT_GUI)
    return;
  while (*text) {
    shell_gui_putchar(*text);
    text++;
  }
}

void shell_gui_reset_input(void) {
  gui_input_pos = 0;
  gui_input_cursor = 0;
  gui_history_view = -1;
  gui_input[0] = '\0';
  gui_repl_pending_len = 0;
  gui_repl_pending[0] = '\0';
  gui_repl_brace_depth = 0;
}

static void gui_seek_input_cursor(int offset) {
  int cols = gui_visible_cols;
  int x;
  int y;

  if (cols <= 0 || cols > SHELL_COLS)
    cols = SHELL_COLS;
  if (offset < 0)
    offset = 0;

  x = gui_input_origin_x + offset;
  y = gui_input_origin_y;
  while (x >= cols) {
    x -= cols;
    y++;
  }

  gui_cursor_x = x;
  gui_cursor_y = y;
  shell_clamp_cursor();
}

static void gui_clear_input_display(int len) {
  int cols = gui_visible_cols;
  int row = gui_input_origin_y;
  int col = gui_input_origin_x;

  if (cols <= 0 || cols > SHELL_COLS)
    cols = SHELL_COLS;
  if (len <= 0)
    return;
  while (col >= cols) {
    col -= cols;
    row++;
  }

  while (len > 0 && row < SHELL_ROWS) {
    int count = cols - col;
    if (count <= 0)
      break;
    if (count > len)
      count = len;
    shell_clear_line_range_current(row, col, col + count - 1);
    len -= count;
    row++;
    col = 0;
  }
}

void shell_gui_execute_line(const char *line) {
  char gui_full_input[SHELL_REPL_PENDING_MAX];

  if (output_mode != SHELL_OUTPUT_GUI || !line)
    return;

  shell_gui_insert_text(line);
  shell_gui_putchar('\n');

  if (line[0] || gui_repl_pending_len > 0) {
    int repl_ready =
        shell_repl_stage_line(gui_repl_pending, &gui_repl_pending_len,
                              &gui_repl_brace_depth, line, gui_full_input,
                              sizeof(gui_full_input));
    if (repl_ready > 0) {
      history_record(gui_full_input);
      shell_gui_queue_command(gui_full_input);
    }
  }

  shell_gui_reset_input();
  if (gui_pending_command_state == 0)
    shell_gui_print_prompt();
}

/* GUI-mode wrappers (print/putchar go to GUI buffer) */
static void gui_replace_input(const char *new_text) {
  int old_len = gui_input_pos;
  int i = 0;

  gui_seek_input_cursor(0);
  gui_clear_input_display(old_len);

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
  gui_seek_input_cursor(gui_input_cursor);
  shell_mark_terminal_dirty();
}

/* Temporarily redirect print/putchar to GUI buffer, execute command,
 * then restore.  We achieve this by using a wrapper approach:
 * shell commands call print()/putchar() which go to VGA text.
 * In GUI mode we need them to go to the GUI buffer.
 * The simplest approach: hook print/putchar via function pointers.*/

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
   * the VGA write functions.*/

  /* Execute through the existing command processor */
  execute_command(input);
}

int shell_gui_run_pending_command(void) {
  if (gui_pending_command_state != 1)
    return 0;

  gui_pending_command_state = 2;

  gui_exec_command(gui_pending_command);

  gui_pending_command[0] = '\0';
  gui_pending_command_state = 0;
  shell_gui_print_prompt();
  return 1;
}

/*
 *  JIT Program Input Routing (GUI Mode)
*/

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
    if (shell_ssh_debug_active()) {
      serial_printf("[ssh-input] JIT input drop byte=0x%x queued=%d cap=%d\n",
                    (unsigned char)c, shell_jit_input_queued(),
                    JIT_INPUT_BUFFER_SIZE);
    }
  }
}

static void shell_jit_program_input_seq(const char *seq) {
  while (*seq) {
    shell_jit_program_input(*seq);
    seq++;
  }
}

static void shell_jit_program_input_seq_debug(const char *name,
                                              const char *seq) {
  int len = 0;
  while (seq[len])
    len++;
  if (shell_ssh_debug_active()) {
    serial_printf("[ssh-input] gui special %s queued_before=%d app_cursor=%d\n",
                  name, shell_jit_input_queued(), gui_app_cursor_keys);
    shell_debug_dump_bytes("gui queued special", seq, len);
  }
  shell_jit_program_input_seq(seq);
}

char shell_jit_program_getchar(void) {

  /* Wait for input. Desktop process owns GUI redraw/input dispatch. */
  while (jit_input_read_pos == jit_input_write_pos &&
         !jit_program_interrupted) {
    process_yield();
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

int shell_jit_program_start_regions(const char *name,
                                    uint32_t code_address,
                                    uint32_t code_bytes,
                                    uint32_t data_address,
                                    uint32_t data_bytes) {
  uint32_t code_end;
  uint32_t data_end;
  if (code_address == 0u || code_bytes == 0u || data_address == 0u ||
      data_bytes == 0u || code_address > 0xffffffffu - code_bytes ||
      data_address > 0xffffffffu - data_bytes) {
    serial_printf("[shell] JIT start rejected invalid memory regions\n");
    return 0;
  }
  code_end = code_address + code_bytes;
  data_end = data_address + data_bytes;
  if (code_address < data_end && data_address < code_end) {
    serial_printf("[shell] JIT start rejected overlapping memory regions\n");
    return 0;
  }

  /* If a JIT program is already loaded, push its state onto the stack */
  if (jit_regions_active) {
    if (jit_stack_depth >= JIT_STACK_MAX) {
      serial_printf("[shell] JIT stack push failed: maximum depth reached\n");
      return 0;
    }
    int d = jit_stack_depth;
    int j = 0;
    while (jit_program_name[j]) {
      jit_stack[d].name[j] = jit_program_name[j];
      j++;
    }
    jit_stack[d].name[j] = '\0';
    jit_stack[d].running = jit_program_running;
    jit_stack[d].killed  = jit_program_killed;

    /* Save the current tool's regions so cross-tool nesting is lossless. */
    jit_stack[d].code_address = jit_code_address;
    jit_stack[d].code_bytes = jit_code_bytes;
    jit_stack[d].data_address = jit_data_address;
    jit_stack[d].data_bytes = jit_data_bytes;
    jit_stack[d].saved_code = kmalloc(jit_code_bytes);
    jit_stack[d].saved_data = kmalloc(jit_data_bytes);
    if (!jit_stack[d].saved_code || !jit_stack[d].saved_data) {
      if (jit_stack[d].saved_code) {
        kfree(jit_stack[d].saved_code);
        jit_stack[d].saved_code = NULL;
      }
      if (jit_stack[d].saved_data) {
        kfree(jit_stack[d].saved_data);
        jit_stack[d].saved_data = NULL;
      }
      serial_printf("[shell] JIT stack push failed: insufficient memory to snapshot %s\n",
                    jit_program_name);
      return 0;
    }
    memcpy(jit_stack[d].saved_code, (void *)jit_code_address,
           jit_code_bytes);
    memcpy(jit_stack[d].saved_data, (void *)jit_data_address,
           jit_data_bytes);

    /* Block the process that owns the JIT region so the scheduler
     * won't dispatch it while a different program's code is loaded
     * there.  It will be unblocked when the pop restores its code.*/
    jit_stack[d].owner_pid = jit_owner_pid;
    if (jit_owner_pid != 0) {
      process_block(jit_owner_pid);
    }

    jit_stack_depth++;
    serial_printf("[shell] JIT stack push depth=%d saved %s (blocked PID %u)\n",
                  jit_stack_depth, jit_stack[d].name, jit_owner_pid);
  }

  /* Current process now owns the JIT region */
  jit_owner_pid = process_get_current_pid();
  jit_code_address = code_address;
  jit_code_bytes = code_bytes;
  jit_data_address = data_address;
  jit_data_bytes = data_bytes;
  jit_regions_active = 1;

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
  return 1;
}

int shell_jit_program_start(const char *name) {
  return shell_jit_program_start_regions(
      name, CC_JIT_CODE_BASE, CC_MAX_CODE, CC_JIT_DATA_BASE, CC_MAX_DATA);
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
      memcpy((void *)jit_stack[d].code_address, jit_stack[d].saved_code,
             jit_stack[d].code_bytes);
      kfree(jit_stack[d].saved_code);
      jit_stack[d].saved_code = NULL;
    }
    if (jit_stack[d].saved_data) {
      memcpy((void *)jit_stack[d].data_address, jit_stack[d].saved_data,
             jit_stack[d].data_bytes);
      kfree(jit_stack[d].saved_data);
      jit_stack[d].saved_data = NULL;
    }

    jit_code_address = jit_stack[d].code_address;
    jit_code_bytes = jit_stack[d].code_bytes;
    jit_data_address = jit_stack[d].data_address;
    jit_data_bytes = jit_stack[d].data_bytes;
    jit_regions_active = 1;

    /* The previous owner's code is now back in the JIT region - safe
     * to let the scheduler dispatch it again.*/
    uint32_t prev_owner = jit_stack[d].owner_pid;
    jit_owner_pid = prev_owner;
    if (prev_owner != 0) {
      process_unblock(prev_owner);
    }

    serial_printf("[shell] JIT stack pop depth=%d restored %s (unblocked PID %u)\n",
                  jit_stack_depth, jit_program_name, prev_owner);
  } else {
    jit_owner_pid = 0;
    shell_jit_clear_active_regions();
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
  if (!jit_program_running)
    return 0;
  if (jit_program_name[0] == '\0')
    return 0;
  if (strcmp(jit_program_name, "terminal") == 0)
    return 0;
  return 1;
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
    uint32_t owner_pid = jit_stack[index].owner_pid;

    if (jit_stack[index].saved_code) {
      kfree(jit_stack[index].saved_code);
      jit_stack[index].saved_code = NULL;
    }
    if (jit_stack[index].saved_data) {
      kfree(jit_stack[index].saved_data);
      jit_stack[index].saved_data = NULL;
    }

    for (int i = index; i < jit_stack_depth - 1; i++) {
      jit_stack[i] = jit_stack[i + 1];
    }
    jit_stack_depth--;

    if (owner_pid != 0) {
      process_kill(owner_pid);
    }
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

void shell_jit_discard_by_owner(uint32_t pid) {
  if (pid == 0)
    return;

  for (int i = 0; i < jit_stack_depth;) {
    if (jit_stack[i].owner_pid == pid) {
      if (jit_stack[i].saved_code) {
        kfree(jit_stack[i].saved_code);
        jit_stack[i].saved_code = NULL;
      }
      if (jit_stack[i].saved_data) {
        kfree(jit_stack[i].saved_data);
        jit_stack[i].saved_data = NULL;
      }

      for (int j = i; j < jit_stack_depth - 1; j++) {
        jit_stack[j] = jit_stack[j + 1];
      }
      jit_stack_depth--;
      continue;
    }
    i++;
  }

  if (jit_owner_pid == pid) {
    jit_owner_pid = 0;
    shell_jit_clear_active_regions();
    jit_program_running = 0;
    jit_program_killed = 1;
    jit_program_interrupted = 1;
    jit_program_name[0] = '\0';
  }
}

void shell_gui_handle_key(uint8_t scancode, char character) {
  static char gui_full_input[SHELL_REPL_PENDING_MAX];

  if (output_mode != SHELL_OUTPUT_GUI)
    return;

  window_t *focused = gui_get_focused_window();
  int terminal_focused =
      (focused && strcmp(focused->title, "Terminal") == 0) ? 1 : 0;

  /* If a JIT program is running in the focused terminal, route input to it
   * instead of treating password/shell data as new shell commands.*/
  if (jit_program_running && strcmp(jit_program_name, "terminal") != 0 &&
      terminal_focused) {
    if (shell_ssh_debug_active()) {
      serial_printf("[ssh-input] gui key sc=0x%x ch=0x%x focused=%d queued=%d app_cursor=%d alt=%d origin=%d wrap=%d\n",
                    scancode, (unsigned char)character, terminal_focused,
                    shell_jit_input_queued(), gui_app_cursor_keys,
                    gui_alt_screen, gui_origin_mode, gui_wrap_enabled);
    }
    if (character != 0) {
      if (shell_ssh_debug_active()) {
        char one[1];
        one[0] = character;
        shell_debug_dump_bytes("gui queued char", one, 1);
      }
      shell_jit_program_input(character);
      return;
    }
    if (scancode == SCANCODE_ARROW_UP) {
      shell_jit_program_input_seq_debug("up",
                                        gui_app_cursor_keys ? "\033OA" : "\033[A");
    } else if (scancode == SCANCODE_ARROW_DOWN) {
      shell_jit_program_input_seq_debug("down",
                                        gui_app_cursor_keys ? "\033OB" : "\033[B");
    } else if (scancode == SCANCODE_ARROW_RIGHT) {
      shell_jit_program_input_seq_debug("right",
                                        gui_app_cursor_keys ? "\033OC" : "\033[C");
    } else if (scancode == SCANCODE_ARROW_LEFT) {
      shell_jit_program_input_seq_debug("left",
                                        gui_app_cursor_keys ? "\033OD" : "\033[D");
    } else if (scancode == 0x47) {
      shell_jit_program_input_seq_debug("home",
                                        gui_app_cursor_keys ? "\033OH" : "\033[H");
    } else if (scancode == 0x4F) {
      shell_jit_program_input_seq_debug("end",
                                        gui_app_cursor_keys ? "\033OF" : "\033[F");
    } else if (scancode == 0x49) {
      shell_jit_program_input_seq_debug("page-up", "\033[5~");
    } else if (scancode == 0x51) {
      shell_jit_program_input_seq_debug("page-down", "\033[6~");
    } else if (scancode == 0x53) {
      shell_jit_program_input_seq_debug("delete", "\033[3~");
    } else if (shell_ssh_debug_active()) {
      serial_printf("[ssh-input] gui key unmapped sc=0x%x ch=0x%x\n",
                    scancode, (unsigned char)character);
    }
    return;
  }

  /* History navigation */
  if (character == 0 && scancode == SCANCODE_F7) {
    shell_gui_putchar('\n');
    execute_command("godspeak");
    gui_input_pos = 0;
    gui_input_cursor = 0;
    gui_history_view = -1;
    memset(gui_input, 0, sizeof(gui_input));
    shell_gui_print_prompt();
    return;
  }

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
    gui_repl_pending_len = 0;
    gui_repl_pending[0] = '\0';
    gui_repl_brace_depth = 0;
    shell_gui_print_prompt();
    return;
  }

  if (character == '\n') {
    int queued_command = 0;
    gui_input[gui_input_pos] = '\0';
    shell_gui_putchar('\n');

    if (gui_input_pos > 0 || gui_repl_pending_len > 0) {
      int repl_ready =
          shell_repl_stage_line(gui_repl_pending, &gui_repl_pending_len,
                                &gui_repl_brace_depth, gui_input, gui_full_input,
                                sizeof(gui_full_input));
      if (repl_ready > 0) {
        history_record(gui_full_input);
        shell_gui_queue_command(gui_full_input);
        queued_command = 1;
      }
    }

    gui_input_pos = 0;
    gui_input_cursor = 0;
    gui_history_view = -1;
    memset(gui_input, 0, sizeof(gui_input));
    if (!queued_command)
      shell_gui_print_prompt();
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
