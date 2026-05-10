/**
 * as.c - CupidASM assembler driver for CupidOS
 *
 * Provides the main entry points for JIT and AOT assembly:
 *   - as_jit(): Assemble and execute a .asm file immediately
 *   - as_aot(): Assemble a .asm file to an ELF32 binary on disk
 */

#include "as.h"
#include "exec.h"
#include "kernel.h"
#include "memory.h"
#include "process.h"
#include "shell.h"
#include "string.h"
#include "syscall.h"
#include "vfs.h"
#include "vfs_helpers.h"
#include "ports.h"
#include "calendar.h"
#include "desktop.h"
#include "math.h"
#include "libm.h"
#include "panic.h"
#include "blockcache.h"
#include "bmp.h"
#include "ed.h"
#include "gfx2d.h"
#include "gfx2d_icons.h"
#include "rtc.h"
#include "mouse.h"
#include "keyboard.h"
#include "serial.h"
#include "timer.h"
#include "ata.h"
#include "pit.h"
#include "speaker.h"
#include "net_if.h"
#include "arp.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "dhcp.h"
#include "dns.h"
#include "socket.h"
#include "blockdev.h"
#include "pci.h"
#include "lapic.h"
#include "bkl.h"
#include "audio/ac97.h"
#include "audio/midiopl.h"
#include "audio/mixer.h"
#include "audio/opl_smoke.h"
#include "notepad.h"
#include "gui.h"
#include "swap.h"
#include "smp.h"
#include "percpu.h"
#include "usb.h"
#include "clipboard.h"
#include "fontsys.h"
#include "ansi.h"
#include "png.h"
#include "jpeg.h"
#include "fat16.h"
#include "deflate.h"
#include "doom/dglibc.h"
#include "graphics.h"
#include "gfx2d_assets.h"
#include "gfx2d_transform.h"
#include "cupidc.h"

/* Read source file from VFS */

static char *as_read_source(const char *path) {
  int fd = vfs_open(path, O_RDONLY);
  if (fd < 0) {
    print("asm: cannot open ");
    print(path);
    print("\n");
    return NULL;
  }

  /* Get file size via stat */
  vfs_stat_t st;
  if (vfs_stat(path, &st) < 0) {
    vfs_close(fd);
    print("asm: cannot stat ");
    print(path);
    print("\n");
    return NULL;
  }

  uint32_t size = st.size;
  if (size == 0 || size > 256 * 1024) {
    vfs_close(fd);
    print("asm: file too large or empty\n");
    return NULL;
  }

  char *source = kmalloc(size + 1);
  if (!source) {
    vfs_close(fd);
    print("asm: out of memory\n");
    return NULL;
  }

  uint32_t total = 0;
  while (total < size) {
    uint32_t chunk = size - total;
    if (chunk > 512)
      chunk = 512;
    int r = vfs_read(fd, source + total, chunk);
    if (r <= 0)
      break;
    total += (uint32_t)r;
  }
  source[total] = '\0';

  vfs_close(fd);
  return source;
}

/* Assembler State Initialization / Cleanup */

/* Register an equ constant in the label table */
static void as_bind_equ(as_state_t *as, const char *name, uint32_t value) {
  if (as->label_count >= AS_MAX_LABELS) return;
  as_label_t *lbl = &as->labels[as->label_count++];
  int i = 0;
  while (name[i] && i < AS_MAX_IDENT - 1) {
    lbl->name[i] = name[i];
    i++;
  }
  lbl->name[i] = '\0';
  lbl->address = value;
  lbl->defined = 1;
  lbl->is_equ  = 1;
}

/* Register a kernel symbol as a pre-defined label with an absolute address.
 * Used in JIT mode so asm programs can `call print`, etc. */
/* convert a function pointer to uint32_t address safely */
static uint32_t fn_to_u32(void (*fn)(void)) {
  uint32_t addr;
  memcpy(&addr, &fn, sizeof(addr));
  return addr;
}

/* Macro to bind a kernel function - casts any function type through
 * void(*)(void) so we only extract the address, never call through it. */
#define AS_BIND(as, name, fn) \
  as_bind((as), (name), fn_to_u32((void(*)(void))(fn)))

static void as_bind(as_state_t *as, const char *name, uint32_t addr) {
  if (as->label_count >= AS_MAX_LABELS) return;
  as_label_t *lbl = &as->labels[as->label_count++];
  int i = 0;
  while (name[i] && i < AS_MAX_IDENT - 1) {
    lbl->name[i] = name[i];
    i++;
  }
  lbl->name[i] = '\0';
  lbl->address = addr;
  lbl->defined = 1;
  lbl->is_equ  = 0;
}

static void as_jit_exit(void) {
  /* For JIT mode, exit just returns - the caller (as_jit) handles cleanup */
}

static void *as_jit_malloc(size_t size) {
  return kmalloc_debug(size, "asm", 0);
}

static void as_println(const char *s) {
  if (s) print(s);
  print("\n");
}

static void as_outb(uint32_t port, uint32_t value) {
  outb((uint16_t)port, (uint8_t)value);
}

static uint32_t as_inb(uint32_t port) {
  return (uint32_t)inb((uint16_t)port);
}

static const char *as_mount_name(int index) {
  const vfs_mount_t *m = vfs_get_mount(index);
  if (m && m->mounted && m->ops) return m->ops->name;
  return NULL;
}

static const char *as_mount_path(int index) {
  const vfs_mount_t *m = vfs_get_mount(index);
  if (m && m->mounted) return m->path;
  return NULL;
}

static int as_rtc_hour(void) {
  rtc_time_t t;
  rtc_read_time(&t);
  return (int)t.hour;
}

static int as_rtc_minute(void) {
  rtc_time_t t;
  rtc_read_time(&t);
  return (int)t.minute;
}

static int as_rtc_second(void) {
  rtc_time_t t;
  rtc_read_time(&t);
  return (int)t.second;
}

static int as_rtc_day(void) {
  rtc_date_t d;
  rtc_read_date(&d);
  return (int)d.day;
}

static int as_rtc_month(void) {
  rtc_date_t d;
  rtc_read_date(&d);
  return (int)d.month;
}

static int as_rtc_year(void) {
  rtc_date_t d;
  rtc_read_date(&d);
  return (int)d.year;
}

static int as_rtc_weekday(void) {
  rtc_date_t d;
  rtc_read_date(&d);
  return (int)d.weekday;
}

static char as_date_full_buf[48];
static const char *as_date_full_string(void) {
  rtc_date_t d;
  rtc_read_date(&d);
  format_date_full(&d, as_date_full_buf, 48);
  return as_date_full_buf;
}

static char as_date_short_buf[20];
static const char *as_date_short_string(void) {
  rtc_date_t d;
  rtc_read_date(&d);
  format_date_short(&d, as_date_short_buf, 20);
  return as_date_short_buf;
}

static char as_time_buf[20];
static const char *as_time_string(void) {
  rtc_time_t t;
  rtc_read_time(&t);
  format_time_12hr_sec(&t, as_time_buf, 20);
  return as_time_buf;
}

static char as_time_short_buf[20];
static const char *as_time_short_string(void) {
  rtc_time_t t;
  rtc_read_time(&t);
  format_time_12hr(&t, as_time_short_buf, 20);
  return as_time_short_buf;
}

static uint32_t as_get_cpu_mhz(void) {
  return (uint32_t)(get_cpu_freq() / 1000000);
}

static int as_peek_byte(uint32_t addr) {
  return (int)*((volatile uint8_t *)addr);
}

static uint32_t as_is_gui_mode(void) {
  return (shell_get_output_mode() == SHELL_OUTPUT_GUI) ? 1u : 0u;
}

static void as_kernel_panic_msg(const char *msg) {
  kernel_panic("%s", msg ? msg : "asm panic");
}

static int as_mouse_x(void) { return (int)mouse.x; }
static int as_mouse_y(void) { return (int)mouse.y; }
static int as_mouse_buttons(void) { return (int)mouse.buttons; }
static int as_mouse_scroll(void) {
  int dz = (int)mouse.scroll_z;
  mouse.scroll_z = 0;
  return dz;
}

static int as_key_shift_held(void) {
  return keyboard_get_shift() ? 1 : 0;
}

typedef __builtin_va_list as_va_list;
#define as_va_start(ap, last) __builtin_va_start(ap, last)
#define as_va_end(ap) __builtin_va_end(ap)
#define as_va_arg(ap, type) __builtin_va_arg(ap, type)

static void as_print_signed_i32(int32_t v) {
  if (v < 0) {
    print("-");
    print_int((uint32_t)(0 - v));
  } else {
    print_int((uint32_t)v);
  }
}

static void as_print_builtin(const char *fmt, ...) {
  if (!fmt) return;
  as_va_list ap;
  as_va_start(ap, fmt);
  while (*fmt) {
    if (*fmt != '%') {
      char ch[2];
      ch[0] = *fmt;
      ch[1] = '\0';
      print(ch);
      fmt++;
      continue;
    }
    fmt++;
    if (*fmt == '\0') break;
    switch (*fmt) {
    case 'd': as_print_signed_i32(as_va_arg(ap, int32_t)); break;
    case 'u': print_int(as_va_arg(ap, uint32_t)); break;
    case 'x':
    case 'X': print_hex(as_va_arg(ap, uint32_t)); break;
    case 'c': putchar((char)as_va_arg(ap, int)); break;
    case 's': {
      const char *s = as_va_arg(ap, const char *);
      print(s ? s : "(null)");
      break;
    }
    case 'p': print_hex((uint32_t)as_va_arg(ap, const void *)); break;
    case '%': print("%"); break;
    default:
      print("%");
      putchar(*fmt);
      break;
    }
    fmt++;
  }
  as_va_end(ap);
}

static void as_printline_builtin(const char *fmt, ...) {
  if (!fmt) {
    print("\n");
    return;
  }
  as_va_list ap;
  as_va_start(ap, fmt);
  while (*fmt) {
    if (*fmt != '%') {
      char ch[2];
      ch[0] = *fmt;
      ch[1] = '\0';
      print(ch);
      fmt++;
      continue;
    }
    fmt++;
    if (*fmt == '\0') break;
    switch (*fmt) {
    case 'd': as_print_signed_i32(as_va_arg(ap, int32_t)); break;
    case 'u': print_int(as_va_arg(ap, uint32_t)); break;
    case 'x':
    case 'X': print_hex(as_va_arg(ap, uint32_t)); break;
    case 'c': putchar((char)as_va_arg(ap, int)); break;
    case 's': {
      const char *s = as_va_arg(ap, const char *);
      print(s ? s : "(null)");
      break;
    }
    case 'p': print_hex((uint32_t)as_va_arg(ap, const void *)); break;
    case '%': print("%"); break;
    default:
      print("%");
      putchar(*fmt);
      break;
    }
    fmt++;
  }
  as_va_end(ap);
  print("\n");
}

static void as_test_counting_process(void) {
  uint32_t pid = process_get_current_pid();
  for (int i = 0; i < 10; i++) {
    serial_printf("[PROCESS] PID %u count %d\n", pid, i);
    process_yield();
  }
}

static uint32_t as_spawn_test(uint32_t count) {
  if (count > 16) count = 16;
  uint32_t spawned = 0;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t pid = process_create(as_test_counting_process, "test", DEFAULT_STACK_SIZE);
    if (pid == 0) break;
    print("Spawned PID ");
    print_int(pid);
    print("\n");
    spawned++;
  }
  return spawned;
}

static void as_notepad_open_file(const char *path) {
  if (!path || path[0] == '\0') return;
  desktop_notepad_launch_with_file(path, path);
}

static void as_dump_stack_trace(void) {
  uint32_t ebp, eip;
  __asm__ volatile("movl %%ebp, %0" : "=r"(ebp));
  __asm__ volatile("call 1f\n1: popl %0" : "=r"(eip));
  print_stack_trace(ebp, eip);
}

static void as_dump_registers(void) {
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
  print("CPU Registers:\n");
  print("  EAX: "); print_hex(eax_v);
  print("  EBX: "); print_hex(ebx_v);
  print("  ECX: "); print_hex(ecx_v);
  print("  EDX: "); print_hex(edx_v);
  print("\n");
  print("  ESI: "); print_hex(esi_v);
  print("  EDI: "); print_hex(edi_v);
  print("  EBP: "); print_hex(ebp_v);
  print("  ESP: "); print_hex(esp_v);
  print("\n");
  print("  EFLAGS: "); print_hex(eflags_v);
  print("\n");
}

static void as_crashtest_nullptr(void) {
  volatile int *p = (volatile int *)0;
  (void)*p;
}

static void as_crashtest_divzero(void) {
  volatile int a = 1;
  volatile int b = 0;
  volatile int c = a / b;
  (void)c;
}

static void as_crashtest_overflow(void) {
  char *buf = kmalloc(16);
  if (buf) {
    memset(buf, 'A', 32);
    kfree(buf);
  }
}

static void as_crashtest_stackoverflow(void) {
  volatile char big[65536];
  big[0] = 'x';
  big[65535] = 'y';
  (void)big;
}

static void as_print_hex_byte_u32(uint32_t val) {
  print_hex_byte((uint8_t)val);
}

static int as_fp_mul(int a, int b) {
  return (int)(((int64_t)a * (int64_t)b) >> 16);
}

static int as_fp_div(int a, int b) {
  if (b == 0) return 0;
  int sign = 1;
  if (a < 0) { a = -a; sign = -sign; }
  if (b < 0) { b = -b; sign = -sign; }
  int int_part = a / b;
  int remainder = a % b;
  int frac = 0;
  for (int i = 15; i >= 0; i--) {
    remainder <<= 1;
    frac <<= 1;
    if (remainder >= b) {
      remainder -= b;
      frac |= 1;
    }
  }
  int result = (int_part << 16) | frac;
  return sign < 0 ? -result : result;
}

static int as_fp_from_int(int a) { return a << 16; }
static int as_fp_to_int(int a) { return a >> 16; }
static int as_fp_frac(int a) { return a & 0xFFFF; }
static int as_fp_one(void) { return 65536; }

/*  Net/HW wrappers for CupidASM  */
static uint32_t as_net_get_ip(void) {
  net_if_t *n = net_if_primary();
  return n ? n->ipv4_addr : 0u;
}
static uint32_t as_net_get_gateway(void) {
  net_if_t *n = net_if_primary();
  return n ? n->ipv4_gateway : 0u;
}
static uint32_t as_net_get_dns(void) {
  net_if_t *n = net_if_primary();
  return n ? n->ipv4_dns : 0u;
}
static uint32_t as_net_get_mask(void) {
  net_if_t *n = net_if_primary();
  return n ? n->ipv4_mask : 0u;
}
static void as_net_get_mac(uint8_t *out) {
  net_if_t *n = net_if_primary();
  int i;
  if (!out) return;
  if (!n) { for (i = 0; i < 6; i++) out[i] = 0u; return; }
  for (i = 0; i < 6; i++) out[i] = n->mac[i];
}
static uint32_t as_net_link_up(void) {
  net_if_t *n = net_if_primary();
  return (n && n->link_up) ? 1u : 0u;
}
static uint32_t as_net_rx_packets(void) {
  net_if_t *n = net_if_primary();
  return n ? (uint32_t)n->rx_packets : 0u;
}
static uint32_t as_net_tx_packets(void) {
  net_if_t *n = net_if_primary();
  return n ? (uint32_t)n->tx_packets : 0u;
}
static uint32_t as_net_rx_drops(void) {
  net_if_t *n = net_if_primary();
  return n ? (uint32_t)n->rx_drops : 0u;
}
static uint32_t as_net_tx_errors(void) {
  net_if_t *n = net_if_primary();
  return n ? (uint32_t)n->tx_errors : 0u;
}

/*  Clipboard / notepad / keyboard / ansi / pci wrappers (mirror cupidc)  */
extern char cc_notepad_open_path[256];
extern char cc_notepad_save_path[256];

static void as_clipboard_set(int ptr, int len) {
  clipboard_copy((const char *)(uint32_t)ptr, len);
}
static int as_clipboard_get(void) {
  const char *d = clipboard_get_data();
  return (int)(uint32_t)d;
}
static int as_clipboard_len(void) { return clipboard_get_length(); }

static void as_notepad_get_open_path(int out_ptr, int out_save_ptr) {
  char *out      = (char *)(uint32_t)out_ptr;
  char *out_save = (char *)(uint32_t)out_save_ptr;
  int i;
  if (out) {
    for (i = 0; i < 255 && cc_notepad_open_path[i]; i++) out[i] = cc_notepad_open_path[i];
    out[i] = '\0';
  }
  if (out_save) {
    for (i = 0; i < 255 && cc_notepad_save_path[i]; i++) out_save[i] = cc_notepad_save_path[i];
    out_save[i] = '\0';
  }
}

static int as_keyboard_ctrl_held(void) { return keyboard_get_ctrl() ? 1 : 0; }

static uint32_t as_ansi_color(int idx) {
  if (idx < 0 || idx > 15) idx = ANSI_DEFAULT_FG;
  return ansi_vga_to_palette((uint8_t)idx);
}

static uint32_t as_pci_bar_is_mmio(int idx, int bar) {
  pci_device_t *d = pci_get_device(idx);
  if (!d || bar < 0 || bar >= 6) return 0u;
  return (d->bars[bar] & 0x1u) ? 0u : 1u;
}
static void as_pci_enable_bus_master_idx(int idx) {
  pci_device_t *d = pci_get_device(idx);
  if (d) pci_enable_bus_master(d);
}

/*  GUI window wrappers  */
static int as_gui_win_create(const char *title, int x, int y, int w, int h) {
  return gui_create_window((int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h, title);
}
static void as_gui_win_close(int win_id) { (void)gui_destroy_window(win_id); }
static int as_gui_win_is_open(int win_id) {
  return gui_get_window(win_id) ? 1 : 0;
}
static int as_gui_win_content_x(int win_id) {
  window_t *win = gui_get_window(win_id);
  return win ? (int)win->x + 1 : 0;
}
static int as_gui_win_content_y(int win_id) {
  window_t *win = gui_get_window(win_id);
  return win ? (int)win->y + TITLEBAR_H + WINDOW_CONTENT_TOP_PAD : 0;
}
static int as_gui_win_content_w(int win_id) {
  window_t *win = gui_get_window(win_id);
  return win ? (int)win->width - 2 : 0;
}
static int as_gui_win_content_h(int win_id) {
  window_t *win = gui_get_window(win_id);
  return win ? (int)win->height - TITLEBAR_H - WINDOW_CONTENT_BORDER : 0;
}
static int as_gui_win_poll_key(int win_id) {
  window_t *win = gui_get_window(win_id);
  if (!win) return -1;
  if (win->key_head == win->key_tail) return -1;
  int key = win->key_queue[win->key_head];
  win->key_head = (win->key_head + 1) % GUI_KEY_QUEUE_SIZE;
  return key;
}
static int  as_gui_win_begin_paint(int wid) { return gui_begin_window_paint(wid); }
static int  as_gui_win_end_paint(int wid)   { return gui_end_window_paint(wid); }
static int  as_gui_win_invalidate(int wid)  { return gui_invalidate_window(wid); }
static int  as_gui_win_invalidate_rect(int wid, int x, int y, int w, int h) {
  return gui_invalidate_window_rect(wid, x, y, w, h);
}
static int  as_gui_win_present(int wid) {
  if (!gui_get_window(wid)) return GUI_ERR_INVALID_ID;
  if (gui_is_minimized(wid)) return GUI_OK;
  return gui_present_windows();
}
static void as_gui_win_flip(int wid) {
  if (!gui_get_window(wid)) return;
  (void)gui_cache_window_content(wid);
  (void)gui_invalidate_window(wid);
  (void)gui_present_windows();
}
static int as_gui_win_can_draw(int wid) {
  window_t *focused = gui_get_focused_window();
  window_t *win = gui_get_window(wid);
  if (!win || gui_is_minimized(wid)) return 0;
  if (focused && (int)focused->id == wid) return 1;
  return (win->flags & WINDOW_FLAG_DIRTY) ? 1 : 0;
}
static int as_gui_win_focus(int wid) { return gui_set_focus(wid); }
static int as_gui_win_draw_frame(int wid) {
  window_t *focused = gui_get_focused_window();
  if (!focused) return -1;
  if ((int)focused->id != wid) return 0;
  mouse_restore_under_cursor();
  return gui_draw_window(wid);
}

/*  Shell buffer wrappers  */
static int as_shell_buf_rows(void) { return SHELL_ROWS; }
static int as_shell_buf_cols(void) { return SHELL_COLS; }
static int as_shell_buf_char(int row, int col) {
  if (row < 0 || row >= SHELL_ROWS || col < 0 || col >= SHELL_COLS) return 0;
  const char *buf = shell_get_buffer();
  return (int)(unsigned char)buf[row * SHELL_COLS + col];
}
static int as_shell_buf_color(int row, int col) {
  if (row < 0 || row >= SHELL_ROWS || col < 0 || col >= SHELL_COLS) return 0x07;
  const shell_color_t *colors = shell_get_color_buffer();
  const shell_color_t cell = colors[row * SHELL_COLS + col];
  return (int)(cell.fg | (uint8_t)(cell.bg << 4));
}
static int  as_shell_cursor_x(void) { return shell_get_cursor_x(); }
static int  as_shell_cursor_y(void) { return shell_get_cursor_y(); }
static void as_shell_send_key(int scancode, int ch) {
  shell_gui_handle_key((uint8_t)scancode, (char)ch);
}

/*  gfx2d text helper wrappers (1bpp glyph routes via graphics.h)  */
static void as_gfx2d_char(int x, int y, int ch, int color) {
  gfx_draw_char((int16_t)x, (int16_t)y, (char)ch, (uint32_t)color);
}
static void as_gfx2d_char_scaled(int x, int y, int ch, int color, int scale) {
  if (scale < 1) scale = 1;
  if (scale > 3) scale = 3;
  gfx_draw_char_scaled((int16_t)x, (int16_t)y, (char)ch, (uint32_t)color, scale);
}
static void as_gfx2d_text_simple(int x, int y, const char *str, int color) {
  gfx_draw_text((int16_t)x, (int16_t)y, str, (uint32_t)color);
}

static int as_blkdev_read(int idx, uint32_t lba, uint32_t count, void *buf) {
  block_device_t *d = blkdev_get(idx);
  if (!d) return -1;
  return blkdev_read(d, lba, count, buf);
}
static int as_blkdev_write(int idx, uint32_t lba, uint32_t count, const void *buf) {
  block_device_t *d = blkdev_get(idx);
  if (!d) return -1;
  return blkdev_write(d, lba, count, buf);
}
static uint32_t as_pci_vendor_idx(int idx) {
  pci_device_t *d = pci_get_device(idx);
  return d ? d->vendor_id : 0u;
}
static uint32_t as_pci_device_id_idx(int idx) {
  pci_device_t *d = pci_get_device(idx);
  return d ? d->device_id : 0u;
}
static uint32_t as_pci_class_idx(int idx) {
  pci_device_t *d = pci_get_device(idx);
  if (!d) return 0u;
  return ((uint32_t)d->class_code << 16) |
         ((uint32_t)d->subclass   <<  8) |
          (uint32_t)d->prog_if;
}
static uint32_t as_pci_irq_idx(int idx) {
  pci_device_t *d = pci_get_device(idx);
  return d ? (uint32_t)d->irq_line : 0u;
}
static uint32_t as_pci_bar_idx(int idx, int bar) {
  pci_device_t *d = pci_get_device(idx);
  if (!d || bar < 0 || bar >= 6) return 0u;
  return d->bars[bar];
}

/* Register kernel functions as pre-defined labels so asm programs can call
 * them directly (e.g. `call print`).  JIT and AOT share most bindings, but
 * `exit` differs: JIT returns to as_jit(), AOT must terminate its process. */
static void as_register_kernel_bindings(as_state_t *as, int jit_mode) {
  /* Console output */
  AS_BIND(as, "print",        print);
  AS_BIND(as, "println",      as_println);
  AS_BIND(as, "putchar",      putchar);
  AS_BIND(as, "print_int",    print_int);
  AS_BIND(as, "print_hex",    print_hex);
  AS_BIND(as, "clear_screen", clear_screen);
  AS_BIND(as, "serial_printf", serial_printf);
  AS_BIND(as, "__cc_Print",    as_print_builtin);
  AS_BIND(as, "__cc_PrintLine",as_printline_builtin);

  /* Memory */
  AS_BIND(as, "kmalloc",      as_jit_malloc);
  AS_BIND(as, "kfree",        kfree);
  AS_BIND(as, "malloc",       as_jit_malloc);
  AS_BIND(as, "free",         kfree);

  /* String ops */
  AS_BIND(as, "strlen",       strlen);
  AS_BIND(as, "strcmp",        strcmp);
  AS_BIND(as, "strncmp",      strncmp);
  AS_BIND(as, "strcpy",       strcpy);
  AS_BIND(as, "strncpy",      strncpy);
  AS_BIND(as, "strchr",       strchr);
  AS_BIND(as, "strcat",       strcat);
  AS_BIND(as, "strrchr",      strrchr);
  AS_BIND(as, "strstr",       strstr);
  AS_BIND(as, "memcmp",       memcmp);
  AS_BIND(as, "memset",       memset);
  AS_BIND(as, "memcpy",       memcpy);

  /* Port I/O */
  AS_BIND(as, "outb",         as_outb);
  AS_BIND(as, "inb",          as_inb);

  /* VFS */
  AS_BIND(as, "vfs_open",     vfs_open);
  AS_BIND(as, "vfs_close",    vfs_close);
  AS_BIND(as, "vfs_read",     vfs_read);
  AS_BIND(as, "vfs_write",    vfs_write);
  AS_BIND(as, "vfs_seek",     vfs_seek);
  AS_BIND(as, "vfs_stat",     vfs_stat);
  AS_BIND(as, "vfs_readdir",  vfs_readdir);
  AS_BIND(as, "vfs_mkdir",    vfs_mkdir);
  AS_BIND(as, "vfs_unlink",   vfs_unlink);
  AS_BIND(as, "vfs_rename",   vfs_rename);
  AS_BIND(as, "vfs_copy_file", vfs_copy_file);
  AS_BIND(as, "vfs_read_all",  vfs_read_all);
  AS_BIND(as, "vfs_write_all", vfs_write_all);
  AS_BIND(as, "vfs_read_text", vfs_read_text);
  AS_BIND(as, "vfs_write_text",vfs_write_text);

  /* Process */
  if (jit_mode) {
    AS_BIND(as, "exit",       as_jit_exit);
  } else {
    AS_BIND(as, "exit",       process_exit);
  }
  AS_BIND(as, "yield",        process_yield);
  AS_BIND(as, "getpid",       process_get_current_pid);
  AS_BIND(as, "kill",         process_kill);
  AS_BIND(as, "sleep_ms",     timer_sleep_ms);

  /* Shell + exec */
  AS_BIND(as, "shell_execute", shell_execute_line);
  AS_BIND(as, "shell_get_cwd", shell_get_cwd);
  AS_BIND(as, "get_cwd",       shell_get_cwd);
  AS_BIND(as, "set_cwd",       shell_set_cwd);
  AS_BIND(as, "resolve_path",  shell_resolve_path);
  AS_BIND(as, "get_history_count", shell_get_history_count);
  AS_BIND(as, "get_history_entry", shell_get_history_entry);
  AS_BIND(as, "get_args",      shell_get_program_args);
  AS_BIND(as, "getchar",       shell_jit_program_getchar);
  AS_BIND(as, "poll_key",      shell_jit_program_pollchar);
  AS_BIND(as, "syscall_get_table", syscall_get_table);
  AS_BIND(as, "exec",          exec);

  /* Timer */
  AS_BIND(as, "uptime_ms",    timer_get_uptime_ms);
  AS_BIND(as, "timer_get_frequency", timer_get_frequency);
  AS_BIND(as, "get_cpu_mhz",  as_get_cpu_mhz);

  /* Memory stats */
  AS_BIND(as, "memstats",     print_memory_stats);
  AS_BIND(as, "peek_byte",    as_peek_byte);
  AS_BIND(as, "is_gui_mode",  as_is_gui_mode);
  AS_BIND(as, "kernel_panic", as_kernel_panic_msg);
  AS_BIND(as, "print_hex_byte", as_print_hex_byte_u32);
  AS_BIND(as, "rtc_epoch",    rtc_get_epoch_seconds);
  AS_BIND(as, "mouse_x",      as_mouse_x);
  AS_BIND(as, "mouse_y",      as_mouse_y);
  AS_BIND(as, "mouse_buttons",as_mouse_buttons);
  AS_BIND(as, "mouse_scroll", as_mouse_scroll);
  AS_BIND(as, "key_shift_held", as_key_shift_held);
  AS_BIND(as, "spawn_test",   as_spawn_test);
  AS_BIND(as, "blockcache_sync", blockcache_sync);
  AS_BIND(as, "blockcache_stats", blockcache_stats);
  AS_BIND(as, "detect_memory_leaks", detect_memory_leaks);
  AS_BIND(as, "heap_check_integrity", heap_check_integrity);
  AS_BIND(as, "pmm_free_pages", pmm_free_pages);
  AS_BIND(as, "pmm_total_pages", pmm_total_pages);
  AS_BIND(as, "set_log_level", set_log_level);
  AS_BIND(as, "get_log_level_name", get_log_level_name);
  AS_BIND(as, "print_log_buffer", print_log_buffer);
  AS_BIND(as, "dump_stack_trace", as_dump_stack_trace);
  AS_BIND(as, "dump_registers", as_dump_registers);
  AS_BIND(as, "crashtest_nullptr", as_crashtest_nullptr);
  AS_BIND(as, "crashtest_divzero", as_crashtest_divzero);
  AS_BIND(as, "crashtest_overflow", as_crashtest_overflow);
  AS_BIND(as, "crashtest_stackoverflow", as_crashtest_stackoverflow);
  AS_BIND(as, "ed_run", ed_run);
  AS_BIND(as, "notepad_open_file", as_notepad_open_file);

  /* Process / mount helpers parity with CupidC */
  AS_BIND(as, "process_list", process_list);
  AS_BIND(as, "process_count", process_get_count);
  AS_BIND(as, "process_kill", process_kill);
  AS_BIND(as, "process_get_count", process_get_count);
  AS_BIND(as, "mount_count",  vfs_mount_count);
  AS_BIND(as, "vfs_mount_count", vfs_mount_count);
  AS_BIND(as, "mount_name",   as_mount_name);
  AS_BIND(as, "mount_path",   as_mount_path);

  /* RTC / date/time helpers parity with CupidC */
  AS_BIND(as, "rtc_hour",     as_rtc_hour);
  AS_BIND(as, "rtc_minute",   as_rtc_minute);
  AS_BIND(as, "rtc_second",   as_rtc_second);
  AS_BIND(as, "rtc_day",      as_rtc_day);
  AS_BIND(as, "rtc_month",    as_rtc_month);
  AS_BIND(as, "rtc_year",     as_rtc_year);
  AS_BIND(as, "rtc_weekday",  as_rtc_weekday);
  AS_BIND(as, "date_full_string",  as_date_full_string);
  AS_BIND(as, "date_short_string", as_date_short_string);
  AS_BIND(as, "time_string",       as_time_string);
  AS_BIND(as, "time_short_string", as_time_short_string);

  /* Fixed-point math parity */
  AS_BIND(as, "fp_mul", as_fp_mul);
  AS_BIND(as, "fp_div", as_fp_div);
  AS_BIND(as, "fp_from_int", as_fp_from_int);
  AS_BIND(as, "fp_to_int", as_fp_to_int);
  AS_BIND(as, "fp_frac", as_fp_frac);
  AS_BIND(as, "FP_ONE", as_fp_one);

  /* BMP helpers parity */
  AS_BIND(as, "bmp_get_info", bmp_get_info);
  AS_BIND(as, "bmp_decode", bmp_decode);
  AS_BIND(as, "bmp_encode", bmp_encode);
  AS_BIND(as, "bmp_decode_to_fb", bmp_decode_to_fb);

  /* Dialog helpers parity */
  AS_BIND(as, "file_dialog_open", gfx2d_file_dialog_open);
  AS_BIND(as, "file_dialog_save", gfx2d_file_dialog_save);
  AS_BIND(as, "confirm_dialog", gfx2d_confirm_dialog);
  AS_BIND(as, "input_dialog", gfx2d_input_dialog);
  AS_BIND(as, "message_dialog", gfx2d_message_dialog);
  AS_BIND(as, "popup_menu", gfx2d_popup_menu);

  /* Desktop icon system parity */
  AS_BIND(as, "register_desktop_icon", gfx2d_icon_register);
  AS_BIND(as, "set_icon_desc", gfx2d_icon_set_desc);
  AS_BIND(as, "set_icon_type", gfx2d_icon_set_type);
  AS_BIND(as, "set_icon_color", gfx2d_icon_set_color);
  AS_BIND(as, "set_icon_drawer", gfx2d_icon_set_custom_drawer);
  AS_BIND(as, "gfx2d_icon_draw_named", gfx2d_icon_draw_named);
  AS_BIND(as, "get_my_icon_handle", gfx2d_icon_find_by_path);
  AS_BIND(as, "set_icon_pos", gfx2d_icon_set_pos);
  AS_BIND(as, "get_icon_label", gfx2d_icon_get_label);
  AS_BIND(as, "get_icon_path", gfx2d_icon_get_path);
  AS_BIND(as, "icon_at_pos", gfx2d_icon_at_pos);
  AS_BIND(as, "icon_count", gfx2d_icon_count);
  AS_BIND(as, "icons_save", gfx2d_icons_save);

  /* gfx2d parity */
  AS_BIND(as, "gfx2d_init", gfx2d_init);
  AS_BIND(as, "gfx2d_clear", gfx2d_clear);
  AS_BIND(as, "gfx2d_flip", gfx2d_flip);
  AS_BIND(as, "gfx2d_width", gfx2d_width);
  AS_BIND(as, "gfx2d_height", gfx2d_height);
  AS_BIND(as, "gfx2d_pixel", gfx2d_pixel);
  AS_BIND(as, "gfx2d_getpixel", gfx2d_getpixel);
  AS_BIND(as, "gfx2d_pixel_alpha", gfx2d_pixel_alpha);
  AS_BIND(as, "gfx2d_line", gfx2d_line);
  AS_BIND(as, "gfx2d_hline", gfx2d_hline);
  AS_BIND(as, "gfx2d_vline", gfx2d_vline);
  AS_BIND(as, "gfx2d_rect", gfx2d_rect);
  AS_BIND(as, "gfx2d_rect_fill", gfx2d_rect_fill);
  AS_BIND(as, "gfx2d_rect_round", gfx2d_rect_round);
  AS_BIND(as, "gfx2d_rect_round_fill", gfx2d_rect_round_fill);
  AS_BIND(as, "gfx2d_circle", gfx2d_circle);
  AS_BIND(as, "gfx2d_circle_fill", gfx2d_circle_fill);
  AS_BIND(as, "gfx2d_ellipse", gfx2d_ellipse);
  AS_BIND(as, "gfx2d_ellipse_fill", gfx2d_ellipse_fill);
  AS_BIND(as, "gfx2d_rect_fill_alpha", gfx2d_rect_fill_alpha);
  AS_BIND(as, "gfx2d_gradient_h", gfx2d_gradient_h);
  AS_BIND(as, "gfx2d_gradient_v", gfx2d_gradient_v);
  AS_BIND(as, "gfx2d_color_hsv", gfx2d_color_hsv);
  AS_BIND(as, "gfx2d_color_picker_draw_sv", gfx2d_color_picker_draw_sv);
  AS_BIND(as, "gfx2d_color_picker_draw_hue", gfx2d_color_picker_draw_hue);
  AS_BIND(as, "gfx2d_color_picker_pick_hue", gfx2d_color_picker_pick_hue);
  AS_BIND(as, "gfx2d_color_picker_pick_sat", gfx2d_color_picker_pick_sat);
  AS_BIND(as, "gfx2d_color_picker_pick_val", gfx2d_color_picker_pick_val);
  AS_BIND(as, "gfx2d_shadow", gfx2d_shadow);
  AS_BIND(as, "gfx2d_dither_rect", gfx2d_dither_rect);
  AS_BIND(as, "gfx2d_scanlines", gfx2d_scanlines);
  AS_BIND(as, "gfx2d_clip_set", gfx2d_clip_set);
  AS_BIND(as, "gfx2d_clip_clear", gfx2d_clip_clear);
  AS_BIND(as, "gfx2d_sprite_load", gfx2d_sprite_load);
  AS_BIND(as, "gfx2d_sprite_free", gfx2d_sprite_free);
  AS_BIND(as, "gfx2d_sprite_draw", gfx2d_sprite_draw);
  AS_BIND(as, "gfx2d_sprite_draw_alpha", gfx2d_sprite_draw_alpha);
  AS_BIND(as, "gfx2d_sprite_draw_scaled", gfx2d_sprite_draw_scaled);
  AS_BIND(as, "gfx2d_sprite_width", gfx2d_sprite_width);
  AS_BIND(as, "gfx2d_sprite_height", gfx2d_sprite_height);
  AS_BIND(as, "gfx2d_text", gfx2d_text);
  AS_BIND(as, "gfx2d_text_shadow", gfx2d_text_shadow);
  AS_BIND(as, "gfx2d_text_outline", gfx2d_text_outline);
  AS_BIND(as, "gfx2d_text_wrap", gfx2d_text_wrap);
  AS_BIND(as, "gfx2d_text_width", gfx2d_text_width);
  AS_BIND(as, "gfx2d_text_height", gfx2d_text_height);
  AS_BIND(as, "gfx2d_vignette", gfx2d_vignette);
  AS_BIND(as, "gfx2d_pixelate", gfx2d_pixelate);
  AS_BIND(as, "gfx2d_invert", gfx2d_invert);
  AS_BIND(as, "gfx2d_tint", gfx2d_tint);
  AS_BIND(as, "gfx2d_bevel", gfx2d_bevel);
  AS_BIND(as, "gfx2d_panel", gfx2d_panel);
  AS_BIND(as, "gfx2d_titlebar", gfx2d_titlebar);
  AS_BIND(as, "gfx2d_copper_bars", gfx2d_copper_bars);
  AS_BIND(as, "gfx2d_plasma", gfx2d_plasma);
  AS_BIND(as, "gfx2d_checkerboard", gfx2d_checkerboard);
  AS_BIND(as, "gfx2d_blend_mode", gfx2d_blend_mode);
  AS_BIND(as, "gfx2d_surface_alloc", gfx2d_surface_alloc);
  AS_BIND(as, "gfx2d_surface_free", gfx2d_surface_free);
  AS_BIND(as, "gfx2d_surface_fill", gfx2d_surface_fill);
  AS_BIND(as, "gfx2d_surface_set_active", gfx2d_surface_set_active);
  AS_BIND(as, "gfx2d_surface_unset_active", gfx2d_surface_unset_active);
  AS_BIND(as, "gfx2d_surface_blit", gfx2d_surface_blit);
  AS_BIND(as, "gfx2d_surface_blit_alpha", gfx2d_surface_blit_alpha);
  AS_BIND(as, "gfx2d_surface_blit_scaled", gfx2d_surface_blit_scaled);
  AS_BIND(as, "gfx2d_tween_linear", gfx2d_tween_linear);
  AS_BIND(as, "gfx2d_tween_ease_in_out", gfx2d_tween_ease_in_out);
  AS_BIND(as, "gfx2d_tween_bounce", gfx2d_tween_bounce);
  AS_BIND(as, "gfx2d_tween_elastic", gfx2d_tween_elastic);
  AS_BIND(as, "gfx2d_particles_create", gfx2d_particles_create);
  AS_BIND(as, "gfx2d_particles_free", gfx2d_particles_free);
  AS_BIND(as, "gfx2d_particle_emit", gfx2d_particle_emit);
  AS_BIND(as, "gfx2d_particles_update", gfx2d_particles_update);
  AS_BIND(as, "gfx2d_particles_draw", gfx2d_particles_draw);
  AS_BIND(as, "gfx2d_particles_alive", gfx2d_particles_alive);
  AS_BIND(as, "gfx2d_bezier", gfx2d_bezier);
  AS_BIND(as, "gfx2d_tri_fill", gfx2d_tri_fill);
  AS_BIND(as, "gfx2d_line_aa", gfx2d_line_aa);
  AS_BIND(as, "gfx2d_flood_fill", gfx2d_flood_fill);
  AS_BIND(as, "gfx2d_fullscreen_enter", gfx2d_fullscreen_enter);
  AS_BIND(as, "gfx2d_fullscreen_exit", gfx2d_fullscreen_exit);
  AS_BIND(as, "gfx2d_window_reset", gfx2d_window_reset);
  AS_BIND(as, "gfx2d_window_frame", gfx2d_window_frame);
  AS_BIND(as, "gfx2d_window_x", gfx2d_window_x);
  AS_BIND(as, "gfx2d_window_y", gfx2d_window_y);
  AS_BIND(as, "gfx2d_window_w", gfx2d_window_w);
  AS_BIND(as, "gfx2d_window_h", gfx2d_window_h);
  AS_BIND(as, "gfx2d_window_content_x", gfx2d_window_content_x);
  AS_BIND(as, "gfx2d_window_content_y", gfx2d_window_content_y);
  AS_BIND(as, "gfx2d_window_content_w", gfx2d_window_content_w);
  AS_BIND(as, "gfx2d_window_content_h", gfx2d_window_content_h);
  AS_BIND(as, "gfx2d_app_toolbar", gfx2d_app_toolbar);
  AS_BIND(as, "gfx2d_minimize", gfx2d_minimize);
  AS_BIND(as, "gfx2d_should_quit", gfx2d_should_quit);
  AS_BIND(as, "gfx2d_draw_cursor", gfx2d_draw_cursor);
  AS_BIND(as, "gfx2d_cursor_hide", gfx2d_cursor_hide);
  AS_BIND(as, "desktop_bg_set_mode_anim", desktop_bg_set_mode_anim);
  AS_BIND(as, "desktop_bg_set_mode_solid", desktop_bg_set_mode_solid);
  AS_BIND(as, "desktop_bg_set_mode_gradient", desktop_bg_set_mode_gradient);
  AS_BIND(as, "desktop_bg_set_mode_tiled_pattern", desktop_bg_set_mode_tiled_pattern);
  AS_BIND(as, "desktop_bg_set_mode_tiled_bmp", desktop_bg_set_mode_tiled_bmp);
  AS_BIND(as, "desktop_bg_set_mode_bmp", desktop_bg_set_mode_bmp);
  AS_BIND(as, "desktop_bg_get_mode", desktop_bg_get_mode);
  AS_BIND(as, "desktop_bg_get_solid_color", desktop_bg_get_solid_color);
  AS_BIND(as, "desktop_bg_set_anim_theme", desktop_bg_set_anim_theme);
  AS_BIND(as, "desktop_bg_get_anim_theme", desktop_bg_get_anim_theme);
  AS_BIND(as, "desktop_bg_get_tiled_pattern", desktop_bg_get_tiled_pattern);
  AS_BIND(as, "desktop_bg_get_tiled_use_bmp", desktop_bg_get_tiled_use_bmp);

  /*  Full networking stack (parity with CupidC)  */
  AS_BIND(as, "net_get_ip",          as_net_get_ip);
  AS_BIND(as, "net_get_gateway",     as_net_get_gateway);
  AS_BIND(as, "net_get_dns",         as_net_get_dns);
  AS_BIND(as, "net_get_mask",        as_net_get_mask);
  AS_BIND(as, "net_get_mac",         as_net_get_mac);
  AS_BIND(as, "net_link_up",         as_net_link_up);
  AS_BIND(as, "net_rx_packets",      as_net_rx_packets);
  AS_BIND(as, "net_tx_packets",      as_net_tx_packets);
  AS_BIND(as, "ip_parse",            ip_parse);
  AS_BIND(as, "ipv4_send",           ipv4_send);
  AS_BIND(as, "arp_resolve",         arp_resolve);
  AS_BIND(as, "arp_dump",            arp_dump);
  AS_BIND(as, "arp_get_entries",     arp_get_entries);
  AS_BIND(as, "icmp_send_echo",      icmp_send_echo);
  AS_BIND(as, "icmp_wait_reply",     icmp_wait_reply);
  AS_BIND(as, "udp_send_raw",        udp_send_raw);
  AS_BIND(as, "dns_resolve",         dns_resolve);
  AS_BIND(as, "htons",               htons);
  AS_BIND(as, "htonl",               htonl);
  AS_BIND(as, "ntohs",               htons);   /* same byte-swap on LE */
  AS_BIND(as, "ntohl",               htonl);
  AS_BIND(as, "socket",              socket_create);
  AS_BIND(as, "bind",                socket_bind);
  AS_BIND(as, "listen",              socket_listen);
  AS_BIND(as, "accept",              socket_accept);
  AS_BIND(as, "connect",             socket_connect);
  AS_BIND(as, "send",                socket_send);
  AS_BIND(as, "recv",                socket_recv);
  AS_BIND(as, "sendto",              socket_sendto);
  AS_BIND(as, "recvfrom",            socket_recvfrom);
  AS_BIND(as, "close",               socket_close);

  as_bind_equ(as, "IP_PROTO_ICMP",   IP_PROTO_ICMP);
  as_bind_equ(as, "IP_PROTO_UDP",    IP_PROTO_UDP);
  as_bind_equ(as, "IP_PROTO_TCP",    IP_PROTO_TCP);
  as_bind_equ(as, "SOCK_TYPE_UDP",   SOCK_TYPE_UDP);
  as_bind_equ(as, "SOCK_TYPE_TCP",   SOCK_TYPE_TCP);

  /*  Block devices  */
  AS_BIND(as, "blkdev_count",        blkdev_count);
  AS_BIND(as, "blkdev_read",         as_blkdev_read);
  AS_BIND(as, "blkdev_write",        as_blkdev_write);
  AS_BIND(as, "ata_read_sectors",    ata_read_sectors);
  AS_BIND(as, "ata_write_sectors",   ata_write_sectors);

  /*  Serial / keyboard direct  */
  AS_BIND(as, "serial_read_char",    serial_read_char);
  AS_BIND(as, "serial_write_char",   serial_write_char);
  AS_BIND(as, "serial_write_string", serial_write_string);
  AS_BIND(as, "serial_has_rx",       serial_has_rx);
  AS_BIND(as, "keyboard_read_event", keyboard_read_event);
  AS_BIND(as, "keyboard_inject_scancode", keyboard_inject_scancode);
  AS_BIND(as, "keyboard_get_shift",  keyboard_get_shift);
  AS_BIND(as, "keyboard_get_ctrl",   keyboard_get_ctrl);
  AS_BIND(as, "keyboard_get_alt",    keyboard_get_alt);
  AS_BIND(as, "keyboard_get_caps_lock", keyboard_get_caps_lock);
  AS_BIND(as, "keyboard_test_sub_start",   keyboard_test_sub_start);
  AS_BIND(as, "keyboard_test_sub_stop",    keyboard_test_sub_stop);
  AS_BIND(as, "keyboard_test_sub_calls",   keyboard_test_sub_calls);
  AS_BIND(as, "keyboard_test_sub_last_sc", keyboard_test_sub_last_sc);
  AS_BIND(as, "keyboard_test_sub_last_pressed", keyboard_test_sub_last_pressed);

  /*  Speaker / PIT  */
  AS_BIND(as, "pc_speaker_on",       pc_speaker_on);
  AS_BIND(as, "pc_speaker_off",      pc_speaker_off);
  AS_BIND(as, "pit_set_frequency",   pit_set_frequency);
  AS_BIND(as, "timer_delay_us",      timer_delay_us);

  /*  PCI introspection  */
  AS_BIND(as, "pci_device_count",    pci_device_count);
  AS_BIND(as, "pci_get_vendor",      as_pci_vendor_idx);
  AS_BIND(as, "pci_get_device_id",   as_pci_device_id_idx);
  AS_BIND(as, "pci_get_class",       as_pci_class_idx);
  AS_BIND(as, "pci_get_irq",         as_pci_irq_idx);
  AS_BIND(as, "pci_get_bar",         as_pci_bar_idx);

  /*  SMP / LAPIC / BKL / paging  */
  AS_BIND(as, "lapic_get_id",        lapic_get_id);
  AS_BIND(as, "lapic_eoi",           lapic_eoi);
  AS_BIND(as, "bkl_lock",            bkl_lock);
  AS_BIND(as, "bkl_unlock",          bkl_unlock);
  AS_BIND(as, "paging_map_mmio",     paging_map_mmio);
  AS_BIND(as, "pmm_alloc_page",      pmm_alloc_page);
  AS_BIND(as, "pmm_free_page",       pmm_free_page);

  /*  Audio: AC97 driver, MIDI/OPL3 synth, PCM mixer  */
  AS_BIND(as, "ac97_init",                ac97_init);
  AS_BIND(as, "ac97_start",               ac97_start);
  AS_BIND(as, "ac97_stop",                ac97_stop);
  AS_BIND(as, "ac97_set_master_volume",   ac97_set_master_volume);
  AS_BIND(as, "ac97_tsc_sleep_ms",        ac97_tsc_sleep_ms);
  AS_BIND(as, "ac97_is_present_int",      ac97_is_present_int);
  AS_BIND(as, "ac97_smoke_sine",          ac97_smoke_sine);
  AS_BIND(as, "ac97_smoke_sweep",         ac97_smoke_sweep);
  AS_BIND(as, "ac97_smoke_pan",           ac97_smoke_pan);
  AS_BIND(as, "audiotest_all",            audiotest_all);
  AS_BIND(as, "opl_smoke",                opl_smoke);
  AS_BIND(as, "midiopl_init",             midiopl_init);
  AS_BIND(as, "midiopl_reset",            midiopl_reset);
  AS_BIND(as, "midiopl_feed",             midiopl_feed);
  AS_BIND(as, "midiopl_render",           midiopl_render);
  AS_BIND(as, "midiopl_set_volume",       midiopl_set_volume);
  AS_BIND(as, "mixer_init",               mixer_init);
  AS_BIND(as, "mixer_play",               mixer_play);
  AS_BIND(as, "mixer_stop",               mixer_stop);
  AS_BIND(as, "mixer_active",             mixer_active);
  AS_BIND(as, "mixer_set_volume",         mixer_set_volume);
  AS_BIND(as, "mixer_fill",               mixer_fill);

  /*  AC97 PCM channel + getters (parity additions)  */
  AS_BIND(as, "ac97_set_pcm_volume",     ac97_set_pcm_volume);
  AS_BIND(as, "ac97_get_master_volume",  ac97_get_master_volume);
  AS_BIND(as, "ac97_get_pcm_volume",     ac97_get_pcm_volume);

  /*  Socket polling + TLS upgrade (parity additions)  */
  AS_BIND(as, "sock_avail",          socket_avail);
  AS_BIND(as, "sock_state",          socket_state);
  AS_BIND(as, "setsockopt",          socket_setsockopt);

  /*  Net interface stats (parity)  */
  AS_BIND(as, "net_rx_drops",        as_net_rx_drops);
  AS_BIND(as, "net_tx_errors",       as_net_tx_errors);

  /*  Image codecs (parity)  */
  AS_BIND(as, "png_decode_mem",            png_decode_mem);
  AS_BIND(as, "jpeg_decode_mem",           jpeg_decode_mem);
  AS_BIND(as, "bmp_decode_to_surface_fit", bmp_decode_to_surface_fit);

  /*  Storage / FS  */
  AS_BIND(as, "storage_total_bytes", fat16_total_bytes);
  AS_BIND(as, "storage_free_bytes",  fat16_free_bytes);

  /*  Swap / SMP / USB  */
  AS_BIND(as, "swap_init",           swap_init);
  AS_BIND(as, "swap_kmalloc",        swap_kmalloc);
  AS_BIND(as, "swap_pin",            swap_pin);
  AS_BIND(as, "swap_unpin",          swap_unpin);
  AS_BIND(as, "swap_free",           swap_free);
  AS_BIND(as, "smp_cpu_count",       smp_cpu_count);
  AS_BIND(as, "smp_current_cpu",     smp_current_cpu);
  AS_BIND(as, "smp_atomic_inc",      smp_atomic_inc);
  AS_BIND(as, "usb_device_count",    usb_device_count);
  AS_BIND(as, "usb_device_class",    usb_device_class);

  /*  Clipboard / notepad / ansi / keyboard ctrl  */
  AS_BIND(as, "clipboard_set",       as_clipboard_set);
  AS_BIND(as, "clipboard_get",       as_clipboard_get);
  AS_BIND(as, "clipboard_len",       as_clipboard_len);
  AS_BIND(as, "notepad_get_open_path", as_notepad_get_open_path);
  AS_BIND(as, "ansi_color",          as_ansi_color);
  AS_BIND(as, "keyboard_ctrl_held",  as_keyboard_ctrl_held);

  /*  PCI extra  */
  AS_BIND(as, "pci_bar_is_mmio",       as_pci_bar_is_mmio);
  AS_BIND(as, "pci_enable_bus_master", as_pci_enable_bus_master_idx);

  /*  Fontsys + REPL  */
  AS_BIND(as, "fontsys_draw_run_styled", fontsys_draw_run_styled);
  AS_BIND(as, "fontsys_set_os_default",  fontsys_set_os_default);
  AS_BIND(as, "repl_eval",                  repl_eval);
  AS_BIND(as, "repl_consume_prompt_result", repl_consume_prompt_result);

  /*  Shell extra  */
  AS_BIND(as, "shell_buf_rows",        as_shell_buf_rows);
  AS_BIND(as, "shell_buf_cols",        as_shell_buf_cols);
  AS_BIND(as, "shell_buf_char",        as_shell_buf_char);
  AS_BIND(as, "shell_buf_color",       as_shell_buf_color);
  AS_BIND(as, "shell_cursor_x",        as_shell_cursor_x);
  AS_BIND(as, "shell_cursor_y",        as_shell_cursor_y);
  AS_BIND(as, "shell_send_key",        as_shell_send_key);
  AS_BIND(as, "shell_execute_line",    shell_execute_line);
  AS_BIND(as, "shell_gui_execute_line",shell_gui_execute_line);
  AS_BIND(as, "shell_gui_reset_input", shell_gui_reset_input);
  AS_BIND(as, "shell_jit_program_resume",  shell_jit_program_resume);
  AS_BIND(as, "shell_jit_program_suspend", shell_jit_program_suspend);
  AS_BIND(as, "shell_set_output_mode", shell_set_output_mode);

  /*  GUI window API  */
  AS_BIND(as, "gui_win_create",          as_gui_win_create);
  AS_BIND(as, "gui_win_close",           as_gui_win_close);
  AS_BIND(as, "gui_win_is_open",         as_gui_win_is_open);
  AS_BIND(as, "gui_win_content_x",       as_gui_win_content_x);
  AS_BIND(as, "gui_win_content_y",       as_gui_win_content_y);
  AS_BIND(as, "gui_win_content_w",       as_gui_win_content_w);
  AS_BIND(as, "gui_win_content_h",       as_gui_win_content_h);
  AS_BIND(as, "gui_win_poll_key",        as_gui_win_poll_key);
  AS_BIND(as, "gui_win_begin_paint",     as_gui_win_begin_paint);
  AS_BIND(as, "gui_win_end_paint",       as_gui_win_end_paint);
  AS_BIND(as, "gui_win_invalidate",      as_gui_win_invalidate);
  AS_BIND(as, "gui_win_invalidate_rect", as_gui_win_invalidate_rect);
  AS_BIND(as, "gui_win_present",         as_gui_win_present);
  AS_BIND(as, "gui_win_flip",            as_gui_win_flip);
  AS_BIND(as, "gui_win_can_draw",        as_gui_win_can_draw);
  AS_BIND(as, "gui_win_focus",           as_gui_win_focus);
  AS_BIND(as, "gui_win_draw_frame",      as_gui_win_draw_frame);

  /*  libm parity (callers marshal float / double args & FPU return).
   *  Float ABI: float on stack (4 B) returns in st0; double on stack (8 B)
   *  returns in st0. Caller must fstp result. */
  AS_BIND(as, "sqrt",      sqrt);   AS_BIND(as, "sqrtf",      sqrtf);
  AS_BIND(as, "sin",       sin);    AS_BIND(as, "sinf",       sinf);
  AS_BIND(as, "cos",       cos);    AS_BIND(as, "cosf",       cosf);
  AS_BIND(as, "tan",       tan);    AS_BIND(as, "tanf",       tanf);
  AS_BIND(as, "asin",      asin);   AS_BIND(as, "asinf",      asinf);
  AS_BIND(as, "acos",      acos);   AS_BIND(as, "acosf",      acosf);
  AS_BIND(as, "atan",      atan);   AS_BIND(as, "atanf",      atanf);
  AS_BIND(as, "atan2",     atan2);  AS_BIND(as, "atan2f",     atan2f);
  AS_BIND(as, "sinh",      sinh);   AS_BIND(as, "sinhf",      sinhf);
  AS_BIND(as, "cosh",      cosh);   AS_BIND(as, "coshf",      coshf);
  AS_BIND(as, "tanh",      tanh);   AS_BIND(as, "tanhf",      tanhf);
  AS_BIND(as, "exp",       exp);    AS_BIND(as, "expf",       expf);
  AS_BIND(as, "exp2",      exp2);   AS_BIND(as, "exp2f",      exp2f);
  AS_BIND(as, "log",       log);    AS_BIND(as, "logf",       logf);
  AS_BIND(as, "log2",      log2);   AS_BIND(as, "log2f",      log2f);
  AS_BIND(as, "pow",       pow);    AS_BIND(as, "powf",       powf);
  AS_BIND(as, "fabs",      fabs);   AS_BIND(as, "fabsf",      fabsf);
  AS_BIND(as, "ceil",      ceil);   AS_BIND(as, "ceilf",      ceilf);
  AS_BIND(as, "floor",     floor);  AS_BIND(as, "floorf",     floorf);
  AS_BIND(as, "round",     round);  AS_BIND(as, "roundf",     roundf);
  AS_BIND(as, "trunc",     trunc);  AS_BIND(as, "truncf",     truncf);
  AS_BIND(as, "fmod",      fmod);   AS_BIND(as, "fmodf",      fmodf);
  AS_BIND(as, "cbrt",      cbrt);   AS_BIND(as, "cbrtf",      cbrtf);
  AS_BIND(as, "hypot",     hypot);  AS_BIND(as, "hypotf",     hypotf);
  AS_BIND(as, "nextafter", nextafter); AS_BIND(as, "nextafterf", nextafterf);

  /*  Compression / fontsys / doom-test (integer-ABI parity)  */
  AS_BIND(as, "kdeflate_raw",            kdeflate_raw);
  AS_BIND(as, "dglibc_test_main",        dglibc_test_main);
  AS_BIND(as, "fontsys_advance",         fontsys_advance);
  AS_BIND(as, "fontsys_ascent",          fontsys_ascent);
  AS_BIND(as, "fontsys_face_count",      fontsys_face_count);
  AS_BIND(as, "fontsys_face_family",     fontsys_face_family);
  AS_BIND(as, "fontsys_face_has_cp",     fontsys_face_has_cp);
  AS_BIND(as, "fontsys_face_italic",     fontsys_face_italic);
  AS_BIND(as, "fontsys_face_weight",     fontsys_face_weight);
  AS_BIND(as, "fontsys_find_face_with_cp", fontsys_find_face_with_cp);
  AS_BIND(as, "fontsys_get_os_default_face", fontsys_get_os_default_face);
  AS_BIND(as, "fontsys_get_os_default_size", fontsys_get_os_default_size);
  AS_BIND(as, "fontsys_italic_extra",    fontsys_italic_extra);
  AS_BIND(as, "fontsys_line_height",     fontsys_line_height);
  AS_BIND(as, "fontsys_match",           fontsys_match);
  AS_BIND(as, "fontsys_register_blob",   fontsys_register_blob);
  AS_BIND(as, "fontsys_register_file",   fontsys_register_file);
  AS_BIND(as, "fontsys_run_width",       fontsys_run_width);
  AS_BIND(as, "fontsys_unregister",      fontsys_unregister);

  /*  gfx2d additions (parity)  */
  AS_BIND(as, "gfx2d_image_load",          gfx2d_image_load);
  AS_BIND(as, "gfx2d_image_load_mem",      gfx2d_image_load_mem);
  AS_BIND(as, "gfx2d_image_free",          gfx2d_image_free);
  AS_BIND(as, "gfx2d_image_draw",          gfx2d_image_draw);
  AS_BIND(as, "gfx2d_image_draw_region",   gfx2d_image_draw_region);
  AS_BIND(as, "gfx2d_image_draw_scaled",   gfx2d_image_draw_scaled);
  AS_BIND(as, "gfx2d_image_draw_transformed", gfx2d_image_draw_transformed);
  AS_BIND(as, "gfx2d_image_get_pixel",     gfx2d_image_get_pixel);
  AS_BIND(as, "gfx2d_image_width",         gfx2d_image_width);
  AS_BIND(as, "gfx2d_image_height",        gfx2d_image_height);
  AS_BIND(as, "gfx2d_char",                as_gfx2d_char);
  AS_BIND(as, "gfx2d_char_scaled",         as_gfx2d_char_scaled);
  AS_BIND(as, "gfx2d_text_n",              gfx2d_text_n);
  AS_BIND(as, "gfx2d_text_simple",         as_gfx2d_text_simple);
  AS_BIND(as, "gfx2d_text_width_n",        gfx2d_text_width_n);
  AS_BIND(as, "gfx2d_circle_thick",        gfx2d_circle_thick);
  AS_BIND(as, "gfx2d_line_thick",          gfx2d_line_thick);
  AS_BIND(as, "gfx2d_tri",                 gfx2d_tri);
  AS_BIND(as, "gfx2d_tri_fill_gradient",   gfx2d_tri_fill_gradient);
  AS_BIND(as, "gfx2d_gradient_h_round",    gfx2d_gradient_h_round);
  AS_BIND(as, "gfx2d_gradient_v_round",    gfx2d_gradient_v_round);
  AS_BIND(as, "gfx2d_gradient_radial",     gfx2d_gradient_radial);
  AS_BIND(as, "gfx2d_glyph_advance",       gfx2d_glyph_advance);
  AS_BIND(as, "gfx2d_capture_screen_to_surface", gfx2d_capture_screen_to_surface);

  /*  Low-level I/O for drivers  */
  AS_BIND(as, "outb",                outb);
  AS_BIND(as, "inb",                 inb);
}

static int as_init_state(as_state_t *as, int jit_mode) {
  memset(as, 0, sizeof(*as));

  as->jit_mode = jit_mode;
  as->error = 0;
  as->has_entry = 0;
  as->patch_count = 0;
  as->label_count = 0;
  as->current_section = 0;
  as->include_depth = 0;

  /* Allocate code and data buffers */
  as->code = kmalloc(AS_MAX_CODE);
  as->data = kmalloc(AS_MAX_DATA);

  if (!as->code || !as->data) {
    if (as->code) kfree(as->code);
    if (as->data) kfree(as->data);
    print("asm: out of memory for assembler buffers\n");
    return -1;
  }

  memset(as->code, 0, AS_MAX_CODE);
  memset(as->data, 0, AS_MAX_DATA);

  as->code_pos = 0;
  as->data_pos = 0;

  if (jit_mode) {
    as->code_base = AS_JIT_CODE_BASE;
    as->data_base = AS_JIT_DATA_BASE;
  } else {
    as->code_base = AS_AOT_CODE_BASE;
    as->data_base = AS_AOT_DATA_BASE;
  }

  /* Register pre-defined kernel symbols for both JIT and AOT assembly. */
  as_register_kernel_bindings(as, jit_mode);

  /* Register syscall table offsets as equ constants.
   * These match cupid_syscall_table_t field offsets so AOT programs
   * can do:  call [ebx + SYS_PRINT]  where ebx = syscall table ptr.
   * JIT programs can also use them for portability. */
  as_bind_equ(as, "SYS_VERSION",      0);
  as_bind_equ(as, "SYS_TABLE_SIZE",   4);
  as_bind_equ(as, "SYS_SIZE",         4);
  as_bind_equ(as, "SYS_PRINT",        8);
  as_bind_equ(as, "SYS_PUTCHAR",      12);
  as_bind_equ(as, "SYS_PRINT_INT",    16);
  as_bind_equ(as, "SYS_PRINT_HEX",    20);
  as_bind_equ(as, "SYS_CLEAR_SCREEN", 24);
  as_bind_equ(as, "SYS_MALLOC",       28);
  as_bind_equ(as, "SYS_FREE",         32);
  as_bind_equ(as, "SYS_STRLEN",       36);
  as_bind_equ(as, "SYS_STRCMP",        40);
  as_bind_equ(as, "SYS_STRNCMP",      44);
  as_bind_equ(as, "SYS_MEMSET",       48);
  as_bind_equ(as, "SYS_MEMCPY",       52);
  as_bind_equ(as, "SYS_VFS_OPEN",     56);
  as_bind_equ(as, "SYS_VFS_CLOSE",    60);
  as_bind_equ(as, "SYS_VFS_READ",     64);
  as_bind_equ(as, "SYS_VFS_WRITE",    68);
  as_bind_equ(as, "SYS_VFS_SEEK",     72);
  as_bind_equ(as, "SYS_VFS_STAT",     76);
  as_bind_equ(as, "SYS_VFS_READDIR",  80);
  as_bind_equ(as, "SYS_VFS_MKDIR",    84);
  as_bind_equ(as, "SYS_VFS_UNLINK",   88);
  as_bind_equ(as, "SYS_EXIT",         92);
  as_bind_equ(as, "SYS_YIELD",        96);
  as_bind_equ(as, "SYS_GETPID",       100);
  as_bind_equ(as, "SYS_KILL",         104);
  as_bind_equ(as, "SYS_SLEEP_MS",     108);
  as_bind_equ(as, "SYS_SHELL_EXEC",   112);
  as_bind_equ(as, "SYS_SHELL_EXEC_LINE", 112);
  as_bind_equ(as, "SYS_SHELL_EXECUTE",112);
  as_bind_equ(as, "SYS_SHELL_CWD",    116);
  as_bind_equ(as, "SYS_SHELL_GET_CWD",116);
  as_bind_equ(as, "SYS_UPTIME_MS",    120);
  as_bind_equ(as, "SYS_EXEC",         124);
  as_bind_equ(as, "SYS_VFS_RENAME",   128);
  as_bind_equ(as, "SYS_VFS_COPY_FILE",132);
  as_bind_equ(as, "SYS_VFS_COPY",     132);
  as_bind_equ(as, "SYS_VFS_READ_ALL", 136);
  as_bind_equ(as, "SYS_VFS_WRITE_ALL",140);
  as_bind_equ(as, "SYS_VFS_READ_TEXT",144);
  as_bind_equ(as, "SYS_VFS_WRITE_TEXT",148);
  as_bind_equ(as, "SYS_MEMSTATS",     152);

  /*  Syscall table offsets (v3)  */
  as_bind_equ(as, "SYS_NET_GET_IP",        156);
  as_bind_equ(as, "SYS_NET_GET_GATEWAY",   160);
  as_bind_equ(as, "SYS_NET_GET_DNS",       164);
  as_bind_equ(as, "SYS_NET_GET_MASK",      168);
  as_bind_equ(as, "SYS_NET_GET_MAC",       172);
  as_bind_equ(as, "SYS_NET_LINK_UP",       176);
  as_bind_equ(as, "SYS_NET_RX_PACKETS",    180);
  as_bind_equ(as, "SYS_NET_TX_PACKETS",    184);
  as_bind_equ(as, "SYS_NET_RX_DROPS",      188);
  as_bind_equ(as, "SYS_NET_TX_ERRORS",     192);
  as_bind_equ(as, "SYS_IP_PARSE",          196);
  as_bind_equ(as, "SYS_IPV4_SEND",         200);
  as_bind_equ(as, "SYS_ARP_RESOLVE",       204);
  as_bind_equ(as, "SYS_ARP_DUMP",          208);
  as_bind_equ(as, "SYS_ARP_GET_ENTRIES",   212);
  as_bind_equ(as, "SYS_ICMP_SEND_ECHO",    216);
  as_bind_equ(as, "SYS_ICMP_WAIT_REPLY",   220);
  as_bind_equ(as, "SYS_UDP_SEND_RAW",      224);
  as_bind_equ(as, "SYS_DNS_RESOLVE",       228);
  as_bind_equ(as, "SYS_HTONS",             232);
  as_bind_equ(as, "SYS_HTONL",             236);
  as_bind_equ(as, "SYS_NTOHS",             240);
  as_bind_equ(as, "SYS_NTOHL",             244);
  as_bind_equ(as, "SYS_SOCKET",            248);
  as_bind_equ(as, "SYS_BIND",              252);
  as_bind_equ(as, "SYS_LISTEN",            256);
  as_bind_equ(as, "SYS_ACCEPT",            260);
  as_bind_equ(as, "SYS_CONNECT",           264);
  as_bind_equ(as, "SYS_SEND",              268);
  as_bind_equ(as, "SYS_RECV",              272);
  as_bind_equ(as, "SYS_SENDTO",            276);
  as_bind_equ(as, "SYS_RECVFROM",          280);
  as_bind_equ(as, "SYS_CLOSE",             284);
  as_bind_equ(as, "SYS_BLKDEV_COUNT",      288);
  as_bind_equ(as, "SYS_BLKDEV_READ",       292);
  as_bind_equ(as, "SYS_BLKDEV_WRITE",      296);
  as_bind_equ(as, "SYS_ATA_READ_SECTORS",  300);
  as_bind_equ(as, "SYS_ATA_WRITE_SECTORS", 304);
  as_bind_equ(as, "SYS_SERIAL_READ_CHAR",  308);
  as_bind_equ(as, "SYS_SERIAL_WRITE_CHAR", 312);
  as_bind_equ(as, "SYS_SERIAL_WRITE_STRING", 316);
  as_bind_equ(as, "SYS_SERIAL_HAS_RX",     320);
  as_bind_equ(as, "SYS_PC_SPEAKER_ON",     324);
  as_bind_equ(as, "SYS_PC_SPEAKER_OFF",    328);
  as_bind_equ(as, "SYS_PIT_SET_FREQUENCY", 332);
  as_bind_equ(as, "SYS_TIMER_DELAY_US",    336);
  as_bind_equ(as, "SYS_PCI_DEVICE_COUNT",  340);
  as_bind_equ(as, "SYS_PCI_GET_VENDOR",    344);
  as_bind_equ(as, "SYS_PCI_GET_DEVICE_ID", 348);
  as_bind_equ(as, "SYS_PCI_GET_CLASS",     352);
  as_bind_equ(as, "SYS_PCI_GET_IRQ",       356);
  as_bind_equ(as, "SYS_PCI_GET_BAR",       360);
  as_bind_equ(as, "SYS_LAPIC_GET_ID",      364);
  as_bind_equ(as, "SYS_LAPIC_EOI",         368);
  as_bind_equ(as, "SYS_BKL_LOCK",          372);
  as_bind_equ(as, "SYS_BKL_UNLOCK",        376);
  as_bind_equ(as, "SYS_PAGING_MAP_MMIO",   380);
  as_bind_equ(as, "SYS_PMM_ALLOC_PAGE",    384);
  as_bind_equ(as, "SYS_PMM_FREE_PAGE",     388);
  as_bind_equ(as, "SYS_OUTB",              392);
  as_bind_equ(as, "SYS_INB",               396);

  /* Protocol / socket-type constants for AOT programs */
  as_bind_equ(as, "IP_PROTO_ICMP",   IP_PROTO_ICMP);
  as_bind_equ(as, "IP_PROTO_UDP",    IP_PROTO_UDP);
  as_bind_equ(as, "IP_PROTO_TCP",    IP_PROTO_TCP);
  as_bind_equ(as, "SOCK_TYPE_UDP",   SOCK_TYPE_UDP);
  as_bind_equ(as, "SOCK_TYPE_TCP",   SOCK_TYPE_TCP);

  /* Useful VFS/open constants for asm programs */
  as_bind_equ(as, "O_RDONLY",         O_RDONLY);
  as_bind_equ(as, "O_WRONLY",         O_WRONLY);
  as_bind_equ(as, "O_RDWR",           O_RDWR);
  as_bind_equ(as, "O_CREAT",          O_CREAT);
  as_bind_equ(as, "O_TRUNC",          O_TRUNC);
  as_bind_equ(as, "O_APPEND",         O_APPEND);

  as_bind_equ(as, "SEEK_SET",         SEEK_SET);
  as_bind_equ(as, "SEEK_CUR",         SEEK_CUR);
  as_bind_equ(as, "SEEK_END",         SEEK_END);

  as_bind_equ(as, "VFS_TYPE_FILE",    VFS_TYPE_FILE);
  as_bind_equ(as, "VFS_TYPE_DIR",     VFS_TYPE_DIR);
  as_bind_equ(as, "VFS_TYPE_DEV",     VFS_TYPE_DEV);

  as_bind_equ(as, "VFS_OK",           VFS_OK);
  as_bind_equ(as, "VFS_ENOENT",       (uint32_t)VFS_ENOENT);
  as_bind_equ(as, "VFS_EACCES",       (uint32_t)VFS_EACCES);
  as_bind_equ(as, "VFS_EEXIST",       (uint32_t)VFS_EEXIST);
  as_bind_equ(as, "VFS_ENOTDIR",      (uint32_t)VFS_ENOTDIR);
  as_bind_equ(as, "VFS_EISDIR",       (uint32_t)VFS_EISDIR);
  as_bind_equ(as, "VFS_EINVAL",       (uint32_t)VFS_EINVAL);
  as_bind_equ(as, "VFS_EMFILE",       (uint32_t)VFS_EMFILE);
  as_bind_equ(as, "VFS_ENOSPC",       (uint32_t)VFS_ENOSPC);
  as_bind_equ(as, "VFS_EIO",          (uint32_t)VFS_EIO);
  as_bind_equ(as, "VFS_ENOSYS",       (uint32_t)VFS_ENOSYS);

  return 0;
}

static void as_cleanup_state(as_state_t *as) {
  if (as->code) kfree(as->code);
  if (as->data) kfree(as->data);
  as->code = NULL;
  as->data = NULL;
}

/* JIT Mode - Assemble and Execute */

void as_jit(const char *path) {
  serial_printf("[asm] JIT assemble: %s\n", path);

  /* Read source file */
  char *source = as_read_source(path);
  if (!source)
    return;

  /* Heap-allocate assembler state (too large for stack) */
  as_state_t *as = kmalloc(sizeof(as_state_t));
  if (!as) {
    print("asm: out of memory for assembler state\n");
    kfree(source);
    return;
  }
  if (as_init_state(as, 1) < 0) {
    kfree(as);
    kfree(source);
    return;
  }

  /* Lex + parse + encode */
  as_lex_init(as, source);
  as_parse_program(as);

  if (as->error) {
    print(as->error_msg);
    kfree(source);
    as_cleanup_state(as);
    kfree(as);
    return;
  }

  if (!as->has_entry) {
    print("asm: no main: or _start: label found\n");
    kfree(source);
    as_cleanup_state(as);
    kfree(as);
    return;
  }

  serial_printf("[asm] Assembled: %u bytes code, %u bytes data\n",
                as->code_pos, as->data_pos);

  /* Guard: reject programs that exceed JIT region limits */
  if (as->code_pos > AS_MAX_CODE) {
    serial_printf("[asm] ERROR: code size %u exceeds max %u\n",
                  as->code_pos, (unsigned)AS_MAX_CODE);
    print("asm: program too large (code overflow)\n");
    kfree(source);
    as_cleanup_state(as);
    kfree(as);
    return;
  }
  if (as->data_pos > AS_MAX_DATA) {
    serial_printf("[asm] ERROR: data size %u exceeds max %u\n",
                  as->data_pos, (unsigned)AS_MAX_DATA);
    print("asm: program too large (data overflow)\n");
    kfree(source);
    as_cleanup_state(as);
    kfree(as);
    return;
  }

  /* Mark JIT program as running */
  if (!shell_jit_program_start(path)) {
    print("asm: cannot launch nested JIT program (snapshot failed)\n");
    kfree(source);
    as_cleanup_state(as);
    kfree(as);
    return;
  }

  /* Copy code and data to execution regions */
  memcpy((void *)AS_JIT_CODE_BASE, as->code, as->code_pos);
  memcpy((void *)AS_JIT_DATA_BASE, as->data, as->data_pos);

  /* Calculate entry point */
  uint32_t entry_addr = AS_JIT_CODE_BASE + as->entry_offset;
  void (*entry_fn)(void);
  memcpy(&entry_fn, &entry_addr, sizeof(entry_fn));

  serial_printf("[asm] Executing at 0x%x\n", entry_addr);

  /* Check stack health before execution */
  stack_guard_check();

  /* Execute the program directly (JIT - synchronous) */
  entry_fn();

  /* Mark program as finished */
  shell_jit_program_end();

  /* Check stack health after execution */
  stack_guard_check();

  serial_printf("[asm] JIT execution complete\n");

  /* Clean up */
  kfree(source);
  as_cleanup_state(as);
  kfree(as);
}

/* AOT Mode - Assemble to ELF Binary */

void as_aot(const char *src_path, const char *out_path) {
  serial_printf("[asm] AOT assemble: %s -> %s\n", src_path, out_path);

  /* Read source file */
  char *source = as_read_source(src_path);
  if (!source)
    return;

  /* Heap-allocate assembler state */
  as_state_t *as = kmalloc(sizeof(as_state_t));
  if (!as) {
    print("asm: out of memory for assembler state\n");
    kfree(source);
    return;
  }
  if (as_init_state(as, 0) < 0) {
    kfree(as);
    kfree(source);
    return;
  }

  /* Lex + parse + encode */
  as_lex_init(as, source);
  as_parse_program(as);

  if (as->error) {
    print(as->error_msg);
    kfree(source);
    as_cleanup_state(as);
    kfree(as);
    return;
  }

  if (!as->has_entry) {
    print("asm: no main: or _start: label found\n");
    kfree(source);
    as_cleanup_state(as);
    kfree(as);
    return;
  }

  print("Assembled: ");
  print_int(as->code_pos);
  print(" bytes code, ");
  print_int(as->data_pos);
  print(" bytes data\n");

  /* Write ELF binary */
  int r = as_write_elf(as, out_path);
  if (r < 0) {
    print("asm: failed to write output file\n");
  } else {
    print("Written to ");
    print(out_path);
    print("\n");
  }

  kfree(source);
  as_cleanup_state(as);
  kfree(as);
}
