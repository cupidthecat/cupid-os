/**
 * cupidc.c - CupidC compiler driver for CupidOS
 *
 * Provides the main entry points for JIT and AOT compilation:
 *   - cupidc_jit(): Compile and execute a .cc file immediately
 *   - cupidc_aot(): Compile a .cc file to an ELF32 binary on disk
 *
 * Initializes kernel function bindings so CupidC programs can call
 * print(), kmalloc(), outb(), inb(), and other kernel APIs directly.
 */

#include "cupidc.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/rtc.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "blockcache.h"
#include "bmp.h"
#include "calendar.h"
#include "desktop.h"
#include "ed.h"
#include "exec.h"
#include "fat16.h"
#include "gfx2d.h"
#include "gfx2d_icons.h"
#include "kernel.h"
#include "math.h"
#include "memory.h"
#include "notepad.h"
#include "panic.h"
#include "ports.h"
#include "process.h"
#include "shell.h"
#include "string.h"
#include "vfs.h"
#include "vfs_helpers.h"

/* Port I/O wrappers for CupidC kernel bindings
 * The compiler binds calls to outb()/inb() to these wrappers which
 * match cdecl calling convention with 32-bit args on the stack.
 * ══════════════════════════════════════════════════════════════════════ */
 */

static void cc_outb(uint32_t port, uint32_t value) {
  outb((uint16_t)port, (uint8_t)value);
}

static uint32_t cc_inb(uint32_t port) { return (uint32_t)inb((uint16_t)port); }

static void cc_println(const char *s) {
  print(s);
  print("\n");
}

typedef __builtin_va_list cc_va_list;
#define cc_va_start(ap, last) __builtin_va_start(ap, last)
#define cc_va_end(ap) __builtin_va_end(ap)
#define cc_va_arg(ap, type) __builtin_va_arg(ap, type)

static void cc_print_signed_i32(int32_t v) {
  if (v < 0) {
    print("-");
    print_int((uint32_t)(0 - v));
  } else {
    print_int((uint32_t)v);
  }
}

static void cc_print_builtin(const char *fmt, ...) {
  if (!fmt)
    return;

  cc_va_list ap;
  cc_va_start(ap, fmt);

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
    if (*fmt == '\0')
      break;

    switch (*fmt) {
    case 'd': {
      int32_t v = cc_va_arg(ap, int32_t);
      cc_print_signed_i32(v);
      break;
    }
    case 'u': {
      uint32_t v = cc_va_arg(ap, uint32_t);
      print_int(v);
      break;
    }
    case 'x':
    case 'X': {
      uint32_t v = cc_va_arg(ap, uint32_t);
      print_hex(v);
      break;
    }
    case 'c': {
      int v = cc_va_arg(ap, int);
      putchar((char)v);
      break;
    }
    case 's': {
      const char *s = cc_va_arg(ap, const char *);
      if (s)
        print(s);
      else
        print("(null)");
      break;
    }
    case 'p': {
      uint32_t v = (uint32_t)cc_va_arg(ap, const void *);
      print_hex(v);
      break;
    }
    case '%':
      print("%");
      break;
    default:
      print("%");
      putchar(*fmt);
      break;
    }
    fmt++;
  }

  cc_va_end(ap);
}

static void cc_printline_builtin(const char *fmt, ...) {
  if (!fmt) {
    print("\n");
    return;
  }

  cc_va_list ap;
  cc_va_start(ap, fmt);

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
    if (*fmt == '\0')
      break;

    switch (*fmt) {
    case 'd': {
      int32_t v = cc_va_arg(ap, int32_t);
      cc_print_signed_i32(v);
      break;
    }
    case 'u': {
      uint32_t v = cc_va_arg(ap, uint32_t);
      print_int(v);
      break;
    }
    case 'x':
    case 'X': {
      uint32_t v = cc_va_arg(ap, uint32_t);
      print_hex(v);
      break;
    }
    case 'c': {
      int v = cc_va_arg(ap, int);
      putchar((char)v);
      break;
    }
    case 's': {
      const char *s = cc_va_arg(ap, const char *);
      if (s)
        print(s);
      else
        print("(null)");
      break;
    }
    case 'p': {
      uint32_t v = (uint32_t)cc_va_arg(ap, const void *);
      print_hex(v);
      break;
    }
    case '%':
      print("%");
      break;
    default:
      print("%");
      putchar(*fmt);
      break;
    }
    fmt++;
  }

  cc_va_end(ap);
  print("\n");
}

static void cc_yield(void) { process_yield(); }

static void cc_exit(void) { process_exit(); }

/* Open a file in GUI notepad from CupidC apps. */
static void cc_notepad_open_file(const char *path) {
  if (!path || path[0] == '\0')
    return;
  notepad_launch_with_file(path, path);
}

static void cc_test_counting_process(void) {
  uint32_t pid = process_get_current_pid();
  for (int i = 0; i < 10; i++) {
    serial_printf("[PROCESS] PID %u count %d\n", pid, i);
    process_yield();
  }
}

static uint32_t cc_spawn_test(uint32_t count) {
  if (count > 16)
    count = 16;
  uint32_t spawned = 0;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t pid =
        process_create(cc_test_counting_process, "test", DEFAULT_STACK_SIZE);
    if (pid == 0)
      break;
    print("Spawned PID ");
    print_int(pid);
    print("\n");
    spawned++;
  }
  return spawned;
}

static int cc_rtc_hour(void) {
  rtc_time_t t;
  rtc_read_time(&t);
  return (int)t.hour;
}
static int cc_rtc_minute(void) {
  rtc_time_t t;
  rtc_read_time(&t);
  return (int)t.minute;
}
static int cc_rtc_second(void) {
  rtc_time_t t;
  rtc_read_time(&t);
  return (int)t.second;
}
static int cc_rtc_day(void) {
  rtc_date_t d;
  rtc_read_date(&d);
  return (int)d.day;
}
static int cc_rtc_month(void) {
  rtc_date_t d;
  rtc_read_date(&d);
  return (int)d.month;
}
static int cc_rtc_year(void) {
  rtc_date_t d;
  rtc_read_date(&d);
  return (int)d.year;
}
static int cc_rtc_weekday(void) {
  rtc_date_t d;
  rtc_read_date(&d);
  return (int)d.weekday;
}

/* Format date as "Thursday, February 6, 2026" into static buffer */
static char cc_date_full_buf[48];
static const char *cc_date_full_string(void) {
  rtc_date_t d;
  rtc_read_date(&d);
  format_date_full(&d, cc_date_full_buf, 48);
  return cc_date_full_buf;
}

/* Format date as "Feb 6, 2026" into static buffer */
static char cc_date_short_buf[20];
static const char *cc_date_short_string(void) {
  rtc_date_t d;
  rtc_read_date(&d);
  format_date_short(&d, cc_date_short_buf, 20);
  return cc_date_short_buf;
}

/* Format time as "6:32:15 PM" into static buffer */
static char cc_time_buf[20];
static const char *cc_time_string(void) {
  rtc_time_t t;
  rtc_read_time(&t);
  format_time_12hr_sec(&t, cc_time_buf, 20);
  return cc_time_buf;
}

/* Format time as "6:32 PM" into static buffer */
static char cc_time_short_buf[20];
static const char *cc_time_short_string(void) {
  rtc_time_t t;
  rtc_read_time(&t);
  format_time_12hr(&t, cc_time_short_buf, 20);
  return cc_time_short_buf;
}

static const char *cc_mount_name(int index) {
  const vfs_mount_t *m = vfs_get_mount(index);
  if (m && m->mounted && m->ops)
    return m->ops->name;
  return NULL;
}

static const char *cc_mount_path(int index) {
  const vfs_mount_t *m = vfs_get_mount(index);
  if (m && m->mounted)
    return m->path;
  return NULL;
}

/* CupidC can't do inline asm, so we provide a wrapper that captures
 * the current EBP/EIP and calls print_stack_trace(). */
static void cc_dump_stack_trace(void) {
  uint32_t ebp, eip;
  __asm__ volatile("movl %%ebp, %0" : "=r"(ebp));
  __asm__ volatile("call 1f\n1: popl %0" : "=r"(eip));
  print_stack_trace(ebp, eip);
}

/* CupidC can't do inline asm - capture and print all CPU registers */
static void cc_dump_registers(void) {
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
  print("  EAX: ");
  print_hex(eax_v);
  print("  EBX: ");
  print_hex(ebx_v);
  print("  ECX: ");
  print_hex(ecx_v);
  print("  EDX: ");
  print_hex(edx_v);
  print("\n");
  print("  ESI: ");
  print_hex(esi_v);
  print("  EDI: ");
  print_hex(edi_v);
  print("  EBP: ");
  print_hex(ebp_v);
  print("  ESP: ");
  print_hex(esp_v);
  print("\n");
  print("  EFLAGS: ");
  print_hex(eflags_v);
  print("\n");
}

/* get_cpu_freq returns uint64_t but CupidC only has 32-bit ints */
static uint32_t cc_get_cpu_mhz(void) {
  return (uint32_t)(get_cpu_freq() / 1000000);
}

/* Read a single byte from a given memory address */
static int cc_peek_byte(uint32_t addr) {
  return (int)*((volatile uint8_t *)addr);
}

/* is_gui_mode - wrapper for shell_get_output_mode() */
static uint32_t cc_is_gui_mode(void) {
  return (shell_get_output_mode() == SHELL_OUTPUT_GUI) ? 1 : 0;
}

/* kernel_panic is variadic - provide simple 1-arg wrapper */
static void cc_kernel_panic_msg(const char *msg) { kernel_panic("%s", msg); }

/* Crashtest wrappers - CupidC can't do volatile pointer tricks etc. */
static void cc_crashtest_nullptr(void) {
  volatile int *p = (volatile int *)0;
  (void)*p;
}

static void cc_crashtest_divzero(void) {
  volatile int a = 1;
  volatile int b = 0;
  volatile int c = a / b;
  (void)c;
}

static void cc_crashtest_overflow(void) {
  char *buf = kmalloc(16);
  if (buf) {
    memset(buf, 'A', 32); /* overflow by 16 bytes */
    kfree(buf);           /* triggers canary detection */
  }
}

static void cc_crashtest_stackoverflow(void) {
  volatile char big[65536];
  big[0] = 'x';
  big[65535] = 'y';
  (void)big;
}

/* Print a byte as 2 hex digits - wrapper with uint32_t arg for CupidC */
static void cc_print_hex_byte(uint32_t val) { print_hex_byte((uint8_t)val); }

/* 16.16 Fixed-Point Math Helpers for CupidC */

static int cc_fp_mul(int a, int b) {
  /* Use 64-bit multiply (no libgcc needed for multiply, only divide) */
  return (int)(((int64_t)a * (int64_t)b) >> 16);
}

static int cc_fp_div(int a, int b) {
  /* 16.16 fixed-point division using 32-bit math only
   * Result = (a << 16) / b, but we can't do 64-bit division
   * Use iterative approach: divide in parts to avoid overflow */
  if (b == 0)
    return 0;

  int sign = 1;
  if (a < 0) {
    a = -a;
    sign = -sign;
  }
  if (b < 0) {
    b = -b;
    sign = -sign;
  }

  /* Integer part: a / b */
  int int_part = a / b;
  int remainder = a % b;

  /* Fractional part: (remainder << 16) / b, done in steps to avoid overflow */
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

static int cc_fp_from_int(int a) { return a << 16; }
static int cc_fp_to_int(int a) { return a >> 16; }
static int cc_fp_frac(int a) { return a & 0xFFFF; }
static int cc_fp_one(void) { return 65536; } /* FP_ONE = 1.0 in 16.16 */

/* Mouse Input Accessors for CupidC */

static int cc_mouse_x(void) { return (int)mouse.x; }
static int cc_mouse_y(void) { return (int)mouse.y; }
static int cc_mouse_buttons(void) { return (int)mouse.buttons; }
static int cc_mouse_scroll(void) {
  int dz = (int)mouse.scroll_z;
  mouse.scroll_z = 0;
  return dz;
}
static int cc_key_shift_held(void) { return keyboard_get_shift() ? 1 : 0; }

/* Kernel Bindings Registration */

static void cc_register_kernel_bindings(cc_state_t *cc) {
/* Helper macro to add a kernel function binding */
#define BIND(name_str, func_ptr, nparams)                                      \
  do {                                                                         \
    cc_symbol_t *s = cc_sym_add(cc, name_str, SYM_KERNEL, TYPE_VOID);          \
    if (s) {                                                                   \
      uint32_t addr;                                                           \
      memcpy(&addr, &(func_ptr), sizeof(addr));                                \
      s->address = addr;                                                       \
      s->param_count = (nparams);                                              \
      s->is_defined = 1;                                                       \
    }                                                                          \
  } while (0)

  /* Console output */
  void (*p_print)(const char *) = print;
  BIND("print", p_print, 1);

  void (*p_println)(const char *) = cc_println;
  BIND("println", p_println, 1);

  void (*p_putchar)(char) = putchar;
  BIND("putchar", p_putchar, 1);

  void (*p_print_int)(uint32_t) = print_int;
  BIND("print_int", p_print_int, 1);

  void (*p_print_hex)(uint32_t) = print_hex;
  BIND("print_hex", p_print_hex, 1);

  void (*p_clear)(void) = clear_screen;
  BIND("clear_screen", p_clear, 0);

  void (*p_serial_printf)(const char *, ...) = serial_printf;
  BIND("serial_printf", p_serial_printf, 1);

  void (*p_cc_print_builtin)(const char *, ...) = cc_print_builtin;
  BIND("__cc_Print", p_cc_print_builtin, 1);

  void (*p_cc_printline_builtin)(const char *, ...) = cc_printline_builtin;
  BIND("__cc_PrintLine", p_cc_printline_builtin, 1);

  /* Memory management */
  /* kmalloc_debug takes (size, file, line) but CupidC programs should
   * just call kmalloc(size).  We bind to a wrapper that fills in
   * a dummy file/line. */
  void *(*p_malloc)(size_t, const char *, uint32_t) = kmalloc_debug;
  BIND("kmalloc", p_malloc, 1);

  void (*p_free)(void *) = kfree;
  BIND("kfree", p_free, 1);

  /* String operations */
  size_t (*p_strlen)(const char *) = strlen;
  BIND("strlen", p_strlen, 1);

  int (*p_strcmp)(const char *, const char *) = strcmp;
  BIND("strcmp", p_strcmp, 2);

  int (*p_strncmp)(const char *, const char *, size_t) = strncmp;
  BIND("strncmp", p_strncmp, 3);

  void *(*p_memset)(void *, int, size_t) = memset;
  BIND("memset", p_memset, 3);

  void *(*p_memcpy)(void *, const void *, size_t) = memcpy;
  BIND("memcpy", p_memcpy, 3);

  /* Port I/O */
  void (*p_outb)(uint32_t, uint32_t) = cc_outb;
  BIND("outb", p_outb, 2);

  uint32_t (*p_inb)(uint32_t) = cc_inb;
  BIND("inb", p_inb, 1);

  /* VFS file operations */
  int (*p_vfs_open)(const char *, uint32_t) = vfs_open;
  BIND("vfs_open", p_vfs_open, 2);

  int (*p_vfs_close)(int) = vfs_close;
  BIND("vfs_close", p_vfs_close, 1);

  int (*p_vfs_read)(int, void *, uint32_t) = vfs_read;
  BIND("vfs_read", p_vfs_read, 3);

  int (*p_vfs_write)(int, const void *, uint32_t) = vfs_write;
  BIND("vfs_write", p_vfs_write, 3);

  int (*p_vfs_seek)(int, int32_t, int) = vfs_seek;
  BIND("vfs_seek", p_vfs_seek, 3);

  int (*p_vfs_stat)(const char *, vfs_stat_t *) = vfs_stat;
  BIND("vfs_stat", p_vfs_stat, 2);

  int (*p_vfs_readdir)(int, vfs_dirent_t *) = vfs_readdir;
  BIND("vfs_readdir", p_vfs_readdir, 2);

  int (*p_vfs_mkdir)(const char *) = vfs_mkdir;
  BIND("vfs_mkdir", p_vfs_mkdir, 1);

  int (*p_vfs_unlink)(const char *) = vfs_unlink;
  BIND("vfs_unlink", p_vfs_unlink, 1);

  int (*p_vfs_rename)(const char *, const char *) = vfs_rename;
  BIND("vfs_rename", p_vfs_rename, 2);

  /* Process management */
  void (*p_yield)(void) = cc_yield;
  BIND("yield", p_yield, 0);

  void (*p_exit)(void) = cc_exit;
  BIND("exit", p_exit, 0);

  /* Program execution */
  int (*p_exec)(const char *, const char *) = exec;
  BIND("exec", p_exec, 2);

  /* Memory diagnostics */
  void (*p_memstats)(void) = print_memory_stats;
  BIND("memstats", p_memstats, 0);

  /* Shell integration */
  const char *(*p_get_cwd)(void) = shell_get_cwd;
  BIND("get_cwd", p_get_cwd, 0);

  void (*p_set_cwd)(const char *) = shell_set_cwd;
  BIND("set_cwd", p_set_cwd, 1);

  void (*p_resolve_path)(const char *, char *) = shell_resolve_path;
  BIND("resolve_path", p_resolve_path, 2);

  int (*p_get_history_count)(void) = shell_get_history_count;
  BIND("get_history_count", p_get_history_count, 0);

  const char *(*p_get_history_entry)(int) = shell_get_history_entry;
  BIND("get_history_entry", p_get_history_entry, 1);

  /* Process management - extended */
  void (*p_process_list)(void) = process_list;
  BIND("process_list", p_process_list, 0);

  void (*p_process_kill)(uint32_t) = process_kill;
  BIND("process_kill", p_process_kill, 1);

  uint32_t (*p_spawn_test)(uint32_t) = cc_spawn_test;
  BIND("spawn_test", p_spawn_test, 1);

  /* Mount info */
  int (*p_mount_count)(void) = vfs_mount_count;
  BIND("mount_count", p_mount_count, 0);

  const char *(*p_mount_name)(int) = cc_mount_name;
  BIND("mount_name", p_mount_name, 1);

  const char *(*p_mount_path)(int) = cc_mount_path;
  BIND("mount_path", p_mount_path, 1);

  uint32_t (*p_storage_total_bytes)(void) = fat16_total_bytes;
  BIND("storage_total_bytes", p_storage_total_bytes, 0);

  uint32_t (*p_storage_free_bytes)(void) = fat16_free_bytes;
  BIND("storage_free_bytes", p_storage_free_bytes, 0);

  /* TempleOS-style argument passing: CupidC programs call get_args()
   * to receive command-line arguments set by the shell. */
  const char *(*p_get_args)(void) = shell_get_program_args;
  BIND("get_args", p_get_args, 0);

  /* Timer */
  uint32_t (*p_uptime)(void) = timer_get_uptime_ms;
  BIND("uptime_ms", p_uptime, 0);

  /* RTC - individual field accessors */
  int (*p_rtc_hour)(void) = cc_rtc_hour;
  BIND("rtc_hour", p_rtc_hour, 0);

  int (*p_rtc_minute)(void) = cc_rtc_minute;
  BIND("rtc_minute", p_rtc_minute, 0);

  int (*p_rtc_second)(void) = cc_rtc_second;
  BIND("rtc_second", p_rtc_second, 0);

  int (*p_rtc_day)(void) = cc_rtc_day;
  BIND("rtc_day", p_rtc_day, 0);

  int (*p_rtc_month)(void) = cc_rtc_month;
  BIND("rtc_month", p_rtc_month, 0);

  int (*p_rtc_year)(void) = cc_rtc_year;
  BIND("rtc_year", p_rtc_year, 0);

  int (*p_rtc_weekday)(void) = cc_rtc_weekday;
  BIND("rtc_weekday", p_rtc_weekday, 0);

  uint32_t (*p_rtc_epoch)(void) = rtc_get_epoch_seconds;
  BIND("rtc_epoch", p_rtc_epoch, 0);

  /* RTC - formatted string accessors */
  const char *(*p_date_full)(void) = cc_date_full_string;
  BIND("date_full_string", p_date_full, 0);

  const char *(*p_date_short)(void) = cc_date_short_string;
  BIND("date_short_string", p_date_short, 0);

  const char *(*p_time_str)(void) = cc_time_string;
  BIND("time_string", p_time_str, 0);

  const char *(*p_time_short)(void) = cc_time_short_string;
  BIND("time_short_string", p_time_short, 0);

  /* Block cache */
  void (*p_blockcache_sync)(void) = blockcache_sync;
  BIND("blockcache_sync", p_blockcache_sync, 0);

  void (*p_blockcache_stats)(void) = blockcache_stats;
  BIND("blockcache_stats", p_blockcache_stats, 0);

  /* Memory diagnostics - extended */
  void (*p_detect_leaks)(uint32_t) = detect_memory_leaks;
  BIND("detect_memory_leaks", p_detect_leaks, 1);

  void (*p_heap_check)(void) = heap_check_integrity;
  BIND("heap_check_integrity", p_heap_check, 0);

  uint32_t (*p_pmm_free)(void) = pmm_free_pages;
  BIND("pmm_free_pages", p_pmm_free, 0);

  uint32_t (*p_pmm_total)(void) = pmm_total_pages;
  BIND("pmm_total_pages", p_pmm_total, 0);

  /* Timer - extended */
  uint32_t (*p_timer_freq)(void) = timer_get_frequency;
  BIND("timer_get_frequency", p_timer_freq, 0);

  /* CPU info */
  uint32_t (*p_cpu_mhz)(void) = cc_get_cpu_mhz;
  BIND("get_cpu_mhz", p_cpu_mhz, 0);

  /* Process info - extended */
  uint32_t (*p_proc_count)(void) = process_get_count;
  BIND("process_get_count", p_proc_count, 0);

  /* Serial log control */
  void (*p_set_log)(int) = (void (*)(int))set_log_level;
  BIND("set_log_level", p_set_log, 1);

  const char *(*p_get_log_name)(void) = get_log_level_name;
  BIND("get_log_level_name", p_get_log_name, 0);

  void (*p_print_log_buf)(void) = print_log_buffer;
  BIND("print_log_buffer", p_print_log_buf, 0);

  /* Debug wrappers (CupidC can't do inline asm) */
  void (*p_dump_stack)(void) = cc_dump_stack_trace;
  BIND("dump_stack_trace", p_dump_stack, 0);

  void (*p_dump_regs)(void) = cc_dump_registers;
  BIND("dump_registers", p_dump_regs, 0);

  /* Memory peek */
  int (*p_peek)(uint32_t) = cc_peek_byte;
  BIND("peek_byte", p_peek, 1);

  /* Hex byte printing */
  void (*p_hex_byte)(uint32_t) = cc_print_hex_byte;
  BIND("print_hex_byte", p_hex_byte, 1);

  /* Crash testing */
  void (*p_panic_msg)(const char *) = cc_kernel_panic_msg;
  BIND("kernel_panic", p_panic_msg, 1);

  void (*p_crash_null)(void) = cc_crashtest_nullptr;
  BIND("crashtest_nullptr", p_crash_null, 0);

  void (*p_crash_div)(void) = cc_crashtest_divzero;
  BIND("crashtest_divzero", p_crash_div, 0);

  void (*p_crash_overflow)(void) = cc_crashtest_overflow;
  BIND("crashtest_overflow", p_crash_overflow, 0);

  void (*p_crash_stack)(void) = cc_crashtest_stackoverflow;
  BIND("crashtest_stackoverflow", p_crash_stack, 0);

  /* Ed line editor */
  void (*p_ed_run)(const char *) = ed_run;
  BIND("ed_run", p_ed_run, 1);

  /* Notepad integration */
  void (*p_notepad_open_file)(const char *) = cc_notepad_open_file;
  BIND("notepad_open_file", p_notepad_open_file, 1);

  /* GUI mode query */
  uint32_t (*p_is_gui)(void) = cc_is_gui_mode;
  BIND("is_gui_mode", p_is_gui, 0);

  /* VFS mount count */
  int (*p_vfs_mount_count)(void) = vfs_mount_count;
  BIND("vfs_mount_count", p_vfs_mount_count, 0);

  /* Keyboard input */
  char (*p_getchar)(void) = getchar;
  BIND("getchar", p_getchar, 0);

  /* Non-blocking keyboard poll */
  char (*p_poll_key)(void) = shell_jit_program_pollchar;
  BIND("poll_key", p_poll_key, 0);

  /* Keyboard modifier state */
  int (*p_key_shift_held)(void) = cc_key_shift_held;
  BIND("key_shift_held", p_key_shift_held, 0);

  /* Mouse input */
  int (*p_mouse_x)(void) = cc_mouse_x;
  BIND("mouse_x", p_mouse_x, 0);

  int (*p_mouse_y)(void) = cc_mouse_y;
  BIND("mouse_y", p_mouse_y, 0);

  int (*p_mouse_buttons)(void) = cc_mouse_buttons;
  BIND("mouse_buttons", p_mouse_buttons, 0);

  int (*p_mouse_scroll)(void) = cc_mouse_scroll;
  BIND("mouse_scroll", p_mouse_scroll, 0);

  /* String operations - extended */

  char *(*p_strcpy)(char *, const char *) = strcpy;
  BIND("strcpy", p_strcpy, 2);

  char *(*p_strncpy)(char *, const char *, size_t) = strncpy;
  BIND("strncpy", p_strncpy, 3);

  char *(*p_strcat)(char *, const char *) = strcat;
  BIND("strcat", p_strcat, 2);

  char *(*p_strchr)(const char *, int) = strchr;
  BIND("strchr", p_strchr, 2);

  char *(*p_strstr)(const char *, const char *) = strstr;
  BIND("strstr", p_strstr, 2);

  int (*p_memcmp)(const void *, const void *, size_t) = memcmp;
  BIND("memcmp", p_memcmp, 3);

  /* gfx2d - 2D graphics library */
  void (*p_gfx2d_init)(void) = gfx2d_init;
  BIND("gfx2d_init", p_gfx2d_init, 0);

  void (*p_gfx2d_clear)(uint32_t) = gfx2d_clear;
  BIND("gfx2d_clear", p_gfx2d_clear, 1);

  void (*p_gfx2d_flip)(void) = gfx2d_flip;
  BIND("gfx2d_flip", p_gfx2d_flip, 0);

  int (*p_gfx2d_width)(void) = gfx2d_width;
  BIND("gfx2d_width", p_gfx2d_width, 0);

  int (*p_gfx2d_height)(void) = gfx2d_height;
  BIND("gfx2d_height", p_gfx2d_height, 0);

  void (*p_gfx2d_pixel)(int, int, uint32_t) = gfx2d_pixel;
  BIND("gfx2d_pixel", p_gfx2d_pixel, 3);

  uint32_t (*p_gfx2d_getpixel)(int, int) = gfx2d_getpixel;
  BIND("gfx2d_getpixel", p_gfx2d_getpixel, 2);

  void (*p_gfx2d_pixel_alpha)(int, int, uint32_t) = gfx2d_pixel_alpha;
  BIND("gfx2d_pixel_alpha", p_gfx2d_pixel_alpha, 3);

  void (*p_gfx2d_line)(int, int, int, int, uint32_t) = gfx2d_line;
  BIND("gfx2d_line", p_gfx2d_line, 5);

  void (*p_gfx2d_hline)(int, int, int, uint32_t) = gfx2d_hline;
  BIND("gfx2d_hline", p_gfx2d_hline, 4);

  void (*p_gfx2d_vline)(int, int, int, uint32_t) = gfx2d_vline;
  BIND("gfx2d_vline", p_gfx2d_vline, 4);

  void (*p_gfx2d_rect)(int, int, int, int, uint32_t) = gfx2d_rect;
  BIND("gfx2d_rect", p_gfx2d_rect, 5);

  void (*p_gfx2d_rect_fill)(int, int, int, int, uint32_t) = gfx2d_rect_fill;
  BIND("gfx2d_rect_fill", p_gfx2d_rect_fill, 5);

  void (*p_gfx2d_rect_round)(int, int, int, int, int, uint32_t) =
      gfx2d_rect_round;
  BIND("gfx2d_rect_round", p_gfx2d_rect_round, 6);

  void (*p_gfx2d_rect_round_fill)(int, int, int, int, int, uint32_t) =
      gfx2d_rect_round_fill;
  BIND("gfx2d_rect_round_fill", p_gfx2d_rect_round_fill, 6);

  void (*p_gfx2d_circle)(int, int, int, uint32_t) = gfx2d_circle;
  BIND("gfx2d_circle", p_gfx2d_circle, 4);

  void (*p_gfx2d_circle_fill)(int, int, int, uint32_t) = gfx2d_circle_fill;
  BIND("gfx2d_circle_fill", p_gfx2d_circle_fill, 4);

  void (*p_gfx2d_ellipse)(int, int, int, int, uint32_t) = gfx2d_ellipse;
  BIND("gfx2d_ellipse", p_gfx2d_ellipse, 5);

  void (*p_gfx2d_ellipse_fill)(int, int, int, int, uint32_t) =
      gfx2d_ellipse_fill;
  BIND("gfx2d_ellipse_fill", p_gfx2d_ellipse_fill, 5);

  void (*p_gfx2d_rect_fill_alpha)(int, int, int, int, uint32_t) =
      gfx2d_rect_fill_alpha;
  BIND("gfx2d_rect_fill_alpha", p_gfx2d_rect_fill_alpha, 5);

  void (*p_gfx2d_gradient_h)(int, int, int, int, uint32_t, uint32_t) =
      gfx2d_gradient_h;
  BIND("gfx2d_gradient_h", p_gfx2d_gradient_h, 6);

  void (*p_gfx2d_gradient_v)(int, int, int, int, uint32_t, uint32_t) =
      gfx2d_gradient_v;
  BIND("gfx2d_gradient_v", p_gfx2d_gradient_v, 6);

    void (*p_gfx2d_gradient_radial)(int, int, int, int, uint32_t, uint32_t) =
      gfx2d_gradient_radial;
    BIND("gfx2d_gradient_radial", p_gfx2d_gradient_radial, 6);

      uint32_t (*p_gfx2d_color_hsv)(int, int, int) = gfx2d_color_hsv;
      BIND("gfx2d_color_hsv", p_gfx2d_color_hsv, 3);

      void (*p_gfx2d_color_picker_draw_sv)(int, int, int, int, int, int, int) =
        gfx2d_color_picker_draw_sv;
      BIND("gfx2d_color_picker_draw_sv", p_gfx2d_color_picker_draw_sv, 7);

      void (*p_gfx2d_color_picker_draw_hue)(int, int, int, int, int) =
        gfx2d_color_picker_draw_hue;
      BIND("gfx2d_color_picker_draw_hue", p_gfx2d_color_picker_draw_hue, 5);

      int (*p_gfx2d_color_picker_pick_hue)(int, int, int, int, int, int) =
        gfx2d_color_picker_pick_hue;
      BIND("gfx2d_color_picker_pick_hue", p_gfx2d_color_picker_pick_hue, 6);

      int (*p_gfx2d_color_picker_pick_sat)(int, int, int, int, int, int) =
        gfx2d_color_picker_pick_sat;
      BIND("gfx2d_color_picker_pick_sat", p_gfx2d_color_picker_pick_sat, 6);

      int (*p_gfx2d_color_picker_pick_val)(int, int, int, int, int, int) =
        gfx2d_color_picker_pick_val;
      BIND("gfx2d_color_picker_pick_val", p_gfx2d_color_picker_pick_val, 6);

  void (*p_gfx2d_shadow)(int, int, int, int, int, uint32_t) = gfx2d_shadow;
  BIND("gfx2d_shadow", p_gfx2d_shadow, 6);

  void (*p_gfx2d_dither_rect)(int, int, int, int, uint32_t, uint32_t, int) =
      gfx2d_dither_rect;
  BIND("gfx2d_dither_rect", p_gfx2d_dither_rect, 7);

  void (*p_gfx2d_scanlines)(int, int, int, int, int) = gfx2d_scanlines;
  BIND("gfx2d_scanlines", p_gfx2d_scanlines, 5);

  void (*p_gfx2d_clip_set)(int, int, int, int) = gfx2d_clip_set;
  BIND("gfx2d_clip_set", p_gfx2d_clip_set, 4);

  void (*p_gfx2d_clip_clear)(void) = gfx2d_clip_clear;
  BIND("gfx2d_clip_clear", p_gfx2d_clip_clear, 0);

  int (*p_gfx2d_sprite_load)(const char *) = gfx2d_sprite_load;
  BIND("gfx2d_sprite_load", p_gfx2d_sprite_load, 1);

  void (*p_gfx2d_sprite_free)(int) = gfx2d_sprite_free;
  BIND("gfx2d_sprite_free", p_gfx2d_sprite_free, 1);

  void (*p_gfx2d_sprite_draw)(int, int, int) = gfx2d_sprite_draw;
  BIND("gfx2d_sprite_draw", p_gfx2d_sprite_draw, 3);

  void (*p_gfx2d_sprite_draw_alpha)(int, int, int) = gfx2d_sprite_draw_alpha;
  BIND("gfx2d_sprite_draw_alpha", p_gfx2d_sprite_draw_alpha, 3);

  void (*p_gfx2d_sprite_draw_scaled)(int, int, int, int, int) =
      gfx2d_sprite_draw_scaled;
  BIND("gfx2d_sprite_draw_scaled", p_gfx2d_sprite_draw_scaled, 5);

  int (*p_gfx2d_sprite_width)(int) = gfx2d_sprite_width;
  BIND("gfx2d_sprite_width", p_gfx2d_sprite_width, 1);

  int (*p_gfx2d_sprite_height)(int) = gfx2d_sprite_height;
  BIND("gfx2d_sprite_height", p_gfx2d_sprite_height, 1);

  void (*p_gfx2d_text)(int, int, const char *, uint32_t, int) = gfx2d_text;
  BIND("gfx2d_text", p_gfx2d_text, 5);

  void (*p_gfx2d_text_shadow)(int, int, const char *, uint32_t, uint32_t, int) =
      gfx2d_text_shadow;
  BIND("gfx2d_text_shadow", p_gfx2d_text_shadow, 6);

  void (*p_gfx2d_text_outline)(int, int, const char *, uint32_t, uint32_t,
                               int) = gfx2d_text_outline;
  BIND("gfx2d_text_outline", p_gfx2d_text_outline, 6);

  void (*p_gfx2d_text_wrap)(int, int, int, const char *, uint32_t, int) =
      gfx2d_text_wrap;
  BIND("gfx2d_text_wrap", p_gfx2d_text_wrap, 6);

  int (*p_gfx2d_text_width)(const char *, int) = gfx2d_text_width;
  BIND("gfx2d_text_width", p_gfx2d_text_width, 2);

  int (*p_gfx2d_text_height)(int) = gfx2d_text_height;
  BIND("gfx2d_text_height", p_gfx2d_text_height, 1);

  void (*p_gfx2d_vignette)(int) = gfx2d_vignette;
  BIND("gfx2d_vignette", p_gfx2d_vignette, 1);

  void (*p_gfx2d_pixelate)(int, int, int, int, int) = gfx2d_pixelate;
  BIND("gfx2d_pixelate", p_gfx2d_pixelate, 5);

  void (*p_gfx2d_invert)(int, int, int, int) = gfx2d_invert;
  BIND("gfx2d_invert", p_gfx2d_invert, 4);

  void (*p_gfx2d_tint)(int, int, int, int, uint32_t, int) = gfx2d_tint;
  BIND("gfx2d_tint", p_gfx2d_tint, 6);

  void (*p_gfx2d_bevel)(int, int, int, int, int) = gfx2d_bevel;
  BIND("gfx2d_bevel", p_gfx2d_bevel, 5);

  void (*p_gfx2d_panel)(int, int, int, int) = gfx2d_panel;
  BIND("gfx2d_panel", p_gfx2d_panel, 4);

  void (*p_gfx2d_titlebar)(int, int, int, int, uint32_t, uint32_t) =
      gfx2d_titlebar;
  BIND("gfx2d_titlebar", p_gfx2d_titlebar, 6);

  void (*p_gfx2d_copper_bars)(int, int, int, int *) = gfx2d_copper_bars;
  BIND("gfx2d_copper_bars", p_gfx2d_copper_bars, 4);

  void (*p_gfx2d_plasma)(int, int, int, int, int) = gfx2d_plasma;
  BIND("gfx2d_plasma", p_gfx2d_plasma, 5);

  void (*p_gfx2d_checkerboard)(int, int, int, int, int, uint32_t, uint32_t) =
      gfx2d_checkerboard;
  BIND("gfx2d_checkerboard", p_gfx2d_checkerboard, 7);

  /* gfx2d - blend modes */
  void (*p_gfx2d_blend_mode)(int) = gfx2d_blend_mode;
  BIND("gfx2d_blend_mode", p_gfx2d_blend_mode, 1);

  /* gfx2d - surfaces */
  int (*p_gfx2d_surface_alloc)(int, int) = gfx2d_surface_alloc;
  BIND("gfx2d_surface_alloc", p_gfx2d_surface_alloc, 2);

  void (*p_gfx2d_surface_free)(int) = gfx2d_surface_free;
  BIND("gfx2d_surface_free", p_gfx2d_surface_free, 1);

  void (*p_gfx2d_surface_fill)(int, uint32_t) = gfx2d_surface_fill;
  BIND("gfx2d_surface_fill", p_gfx2d_surface_fill, 2);

  void (*p_gfx2d_surface_set_active)(int) = gfx2d_surface_set_active;
  BIND("gfx2d_surface_set_active", p_gfx2d_surface_set_active, 1);

  void (*p_gfx2d_surface_unset_active)(void) = gfx2d_surface_unset_active;
  BIND("gfx2d_surface_unset_active", p_gfx2d_surface_unset_active, 0);

  void (*p_gfx2d_surface_blit)(int, int, int) = gfx2d_surface_blit;
  BIND("gfx2d_surface_blit", p_gfx2d_surface_blit, 3);

  void (*p_gfx2d_surface_blit_alpha)(int, int, int, int) =
      gfx2d_surface_blit_alpha;
  BIND("gfx2d_surface_blit_alpha", p_gfx2d_surface_blit_alpha, 4);

    void (*p_gfx2d_surface_blit_scaled)(int, int, int, int, int) =
      gfx2d_surface_blit_scaled;
    BIND("gfx2d_surface_blit_scaled", p_gfx2d_surface_blit_scaled, 5);

  void (*p_gfx2d_capture_screen_to_surface)(int) =
      gfx2d_capture_screen_to_surface;
  BIND("gfx2d_capture_screen_to_surface", p_gfx2d_capture_screen_to_surface, 1);

  /* gfx2d - tweening */
  int (*p_gfx2d_tween_linear)(int, int, int, int) = gfx2d_tween_linear;
  BIND("gfx2d_tween_linear", p_gfx2d_tween_linear, 4);

  int (*p_gfx2d_tween_ease_in_out)(int, int, int, int) =
      gfx2d_tween_ease_in_out;
  BIND("gfx2d_tween_ease_in_out", p_gfx2d_tween_ease_in_out, 4);

  int (*p_gfx2d_tween_bounce)(int, int, int, int) = gfx2d_tween_bounce;
  BIND("gfx2d_tween_bounce", p_gfx2d_tween_bounce, 4);

  int (*p_gfx2d_tween_elastic)(int, int, int, int) = gfx2d_tween_elastic;
  BIND("gfx2d_tween_elastic", p_gfx2d_tween_elastic, 4);

  /* gfx2d - particles */
  int (*p_gfx2d_particles_create)(void) = gfx2d_particles_create;
  BIND("gfx2d_particles_create", p_gfx2d_particles_create, 0);

  void (*p_gfx2d_particles_free)(int) = gfx2d_particles_free;
  BIND("gfx2d_particles_free", p_gfx2d_particles_free, 1);

  void (*p_gfx2d_particle_emit)(int, int, int, int, int, uint32_t, int) =
      gfx2d_particle_emit;
  BIND("gfx2d_particle_emit", p_gfx2d_particle_emit, 7);

  void (*p_gfx2d_particles_update)(int, int) = gfx2d_particles_update;
  BIND("gfx2d_particles_update", p_gfx2d_particles_update, 2);

  void (*p_gfx2d_particles_draw)(int) = gfx2d_particles_draw;
  BIND("gfx2d_particles_draw", p_gfx2d_particles_draw, 1);

  int (*p_gfx2d_particles_alive)(int) = gfx2d_particles_alive;
  BIND("gfx2d_particles_alive", p_gfx2d_particles_alive, 1);

  /* gfx2d - drawing tools */
    void (*p_gfx2d_tri)(int, int, int, int, int, int, uint32_t) = gfx2d_tri;
    BIND("gfx2d_tri", p_gfx2d_tri, 7);

  void (*p_gfx2d_bezier)(int, int, int, int, int, int, uint32_t) = gfx2d_bezier;
  BIND("gfx2d_bezier", p_gfx2d_bezier, 7);

  void (*p_gfx2d_tri_fill)(int, int, int, int, int, int, uint32_t) =
      gfx2d_tri_fill;
  BIND("gfx2d_tri_fill", p_gfx2d_tri_fill, 7);

    void (*p_gfx2d_tri_fill_gradient)(int, int, uint32_t, int, int, uint32_t,
                    int, int, uint32_t) =
      gfx2d_tri_fill_gradient;
    BIND("gfx2d_tri_fill_gradient", p_gfx2d_tri_fill_gradient, 9);

    void (*p_gfx2d_line_thick)(int, int, int, int, int, uint32_t) =
      gfx2d_line_thick;
    BIND("gfx2d_line_thick", p_gfx2d_line_thick, 6);

    void (*p_gfx2d_circle_thick)(int, int, int, int, uint32_t) =
      gfx2d_circle_thick;
    BIND("gfx2d_circle_thick", p_gfx2d_circle_thick, 5);

  void (*p_gfx2d_line_aa)(int, int, int, int, uint32_t) = gfx2d_line_aa;
  BIND("gfx2d_line_aa", p_gfx2d_line_aa, 5);

  void (*p_gfx2d_flood_fill)(int, int, uint32_t) = gfx2d_flood_fill;
  BIND("gfx2d_flood_fill", p_gfx2d_flood_fill, 3);

  /* gfx2d - fullscreen mode */
  void (*p_gfx2d_fullscreen_enter)(void) = gfx2d_fullscreen_enter;
  BIND("gfx2d_fullscreen_enter", p_gfx2d_fullscreen_enter, 0);

  void (*p_gfx2d_fullscreen_exit)(void) = gfx2d_fullscreen_exit;
  BIND("gfx2d_fullscreen_exit", p_gfx2d_fullscreen_exit, 0);

    void (*p_gfx2d_window_reset)(int, int, int, int) = gfx2d_window_reset;
    BIND("gfx2d_window_reset", p_gfx2d_window_reset, 4);

    int (*p_gfx2d_window_frame)(const char *, int, int, int, int) =
      gfx2d_window_frame;
    BIND("gfx2d_window_frame", p_gfx2d_window_frame, 5);

    int (*p_gfx2d_window_x)(void) = gfx2d_window_x;
    BIND("gfx2d_window_x", p_gfx2d_window_x, 0);

    int (*p_gfx2d_window_y)(void) = gfx2d_window_y;
    BIND("gfx2d_window_y", p_gfx2d_window_y, 0);

    int (*p_gfx2d_window_w)(void) = gfx2d_window_w;
    BIND("gfx2d_window_w", p_gfx2d_window_w, 0);

    int (*p_gfx2d_window_h)(void) = gfx2d_window_h;
    BIND("gfx2d_window_h", p_gfx2d_window_h, 0);

    int (*p_gfx2d_window_content_x)(void) = gfx2d_window_content_x;
    BIND("gfx2d_window_content_x", p_gfx2d_window_content_x, 0);

    int (*p_gfx2d_window_content_y)(void) = gfx2d_window_content_y;
    BIND("gfx2d_window_content_y", p_gfx2d_window_content_y, 0);

    int (*p_gfx2d_window_content_w)(void) = gfx2d_window_content_w;
    BIND("gfx2d_window_content_w", p_gfx2d_window_content_w, 0);

    int (*p_gfx2d_window_content_h)(void) = gfx2d_window_content_h;
    BIND("gfx2d_window_content_h", p_gfx2d_window_content_h, 0);

  int (*p_gfx2d_app_toolbar)(const char *, int, int, int) =
      gfx2d_app_toolbar;
  BIND("gfx2d_app_toolbar", p_gfx2d_app_toolbar, 4);

  void (*p_gfx2d_minimize)(const char *) = gfx2d_minimize;
  BIND("gfx2d_minimize", p_gfx2d_minimize, 1);
  int (*p_gfx2d_should_quit)(void) = gfx2d_should_quit;
  BIND("gfx2d_should_quit", p_gfx2d_should_quit, 0);

  void (*p_gfx2d_draw_cursor)(void) = gfx2d_draw_cursor;
  BIND("gfx2d_draw_cursor", p_gfx2d_draw_cursor, 0);

  void (*p_gfx2d_cursor_hide)(void) = gfx2d_cursor_hide;
  BIND("gfx2d_cursor_hide", p_gfx2d_cursor_hide, 0);

  void (*p_desktop_bg_set_mode_anim)(void) = desktop_bg_set_mode_anim;
  BIND("desktop_bg_set_mode_anim", p_desktop_bg_set_mode_anim, 0);

  void (*p_desktop_bg_set_mode_solid)(uint32_t) = desktop_bg_set_mode_solid;
  BIND("desktop_bg_set_mode_solid", p_desktop_bg_set_mode_solid, 1);

  void (*p_desktop_bg_set_mode_gradient)(uint32_t, uint32_t) =
      desktop_bg_set_mode_gradient;
  BIND("desktop_bg_set_mode_gradient", p_desktop_bg_set_mode_gradient, 2);

    void (*p_desktop_bg_set_mode_tiled_pattern)(int, uint32_t, uint32_t) =
      desktop_bg_set_mode_tiled_pattern;
    BIND("desktop_bg_set_mode_tiled_pattern", p_desktop_bg_set_mode_tiled_pattern,
       3);

    int (*p_desktop_bg_set_mode_tiled_bmp)(const char *) =
      desktop_bg_set_mode_tiled_bmp;
    BIND("desktop_bg_set_mode_tiled_bmp", p_desktop_bg_set_mode_tiled_bmp, 1);

  int (*p_desktop_bg_set_mode_bmp)(const char *) = desktop_bg_set_mode_bmp;
  BIND("desktop_bg_set_mode_bmp", p_desktop_bg_set_mode_bmp, 1);

  int (*p_desktop_bg_get_mode)(void) = desktop_bg_get_mode;
  BIND("desktop_bg_get_mode", p_desktop_bg_get_mode, 0);

  uint32_t (*p_desktop_bg_get_solid_color)(void) = desktop_bg_get_solid_color;
  BIND("desktop_bg_get_solid_color", p_desktop_bg_get_solid_color, 0);

  void (*p_desktop_bg_set_anim_theme)(int) = desktop_bg_set_anim_theme;
  BIND("desktop_bg_set_anim_theme", p_desktop_bg_set_anim_theme, 1);

  int (*p_desktop_bg_get_anim_theme)(void) = desktop_bg_get_anim_theme;
  BIND("desktop_bg_get_anim_theme", p_desktop_bg_get_anim_theme, 0);

  int (*p_desktop_bg_get_tiled_pattern)(void) = desktop_bg_get_tiled_pattern;
  BIND("desktop_bg_get_tiled_pattern", p_desktop_bg_get_tiled_pattern, 0);

  int (*p_desktop_bg_get_tiled_use_bmp)(void) = desktop_bg_get_tiled_use_bmp;
  BIND("desktop_bg_get_tiled_use_bmp", p_desktop_bg_get_tiled_use_bmp, 0);

  /* Fixed-point math (16.16) */

  int (*p_fp_mul)(int, int) = cc_fp_mul;
  BIND("fp_mul", p_fp_mul, 2);

  int (*p_fp_div)(int, int) = cc_fp_div;
  BIND("fp_div", p_fp_div, 2);

  int (*p_fp_from_int)(int) = cc_fp_from_int;
  BIND("fp_from_int", p_fp_from_int, 1);

  int (*p_fp_to_int)(int) = cc_fp_to_int;
  BIND("fp_to_int", p_fp_to_int, 1);

  int (*p_fp_frac)(int) = cc_fp_frac;
  BIND("fp_frac", p_fp_frac, 1);

  int (*p_fp_one)(void) = cc_fp_one;
  BIND("FP_ONE", p_fp_one, 0);

  /* BMP image encoding/decoding */

  int (*p_bmp_get_info)(const char *, bmp_info_t *) = bmp_get_info;
  BIND("bmp_get_info", p_bmp_get_info, 2);

  int (*p_bmp_decode)(const char *, uint32_t *, uint32_t) = bmp_decode;
  BIND("bmp_decode", p_bmp_decode, 3);

  int (*p_bmp_encode)(const char *, const uint32_t *,
                      uint32_t, uint32_t) = bmp_encode;
  BIND("bmp_encode", p_bmp_encode, 4);

  int (*p_bmp_decode_to_fb)(const char *, int, int) = bmp_decode_to_fb;
  BIND("bmp_decode_to_fb", p_bmp_decode_to_fb, 3);

  int (*p_bmp_decode_to_surface_fit)(const char *, int, int, int) =
      bmp_decode_to_surface_fit;
  BIND("bmp_decode_to_surface_fit", p_bmp_decode_to_surface_fit, 4);

  /* File dialogs */

  int (*p_file_dlg_open)(const char *, char *, const char *) =
      gfx2d_file_dialog_open;
  BIND("file_dialog_open", p_file_dlg_open, 3);

  int (*p_file_dlg_save)(const char *, const char *, char *, const char *) =
      gfx2d_file_dialog_save;
  BIND("file_dialog_save", p_file_dlg_save, 4);

  /* VFS helpers */

  int (*p_vfs_read_all)(const char *, void *, uint32_t) = vfs_read_all;
  BIND("vfs_read_all", p_vfs_read_all, 3);

  int (*p_vfs_write_all)(const char *, const void *, uint32_t) = vfs_write_all;
  BIND("vfs_write_all", p_vfs_write_all, 3);

  int (*p_vfs_read_text)(const char *, char *, uint32_t) = vfs_read_text;
  BIND("vfs_read_text", p_vfs_read_text, 3);

  int (*p_vfs_write_text)(const char *, const char *) = vfs_write_text;
  BIND("vfs_write_text", p_vfs_write_text, 2);

  int (*p_vfs_copy_file)(const char *, const char *) = vfs_copy_file;
  BIND("vfs_copy_file", p_vfs_copy_file, 2);

  /* String extras */

  char *(*p_strrchr)(const char *, int) = strrchr;
  BIND("strrchr", p_strrchr, 2);

  /* Dialog helpers */

  int (*p_confirm_dlg)(const char *) = gfx2d_confirm_dialog;
  BIND("confirm_dialog", p_confirm_dlg, 1);

  int (*p_input_dlg)(const char *, char *, int) = gfx2d_input_dialog;
  BIND("input_dialog", p_input_dlg, 3);

  void (*p_message_dlg)(const char *) = gfx2d_message_dialog;
  BIND("message_dialog", p_message_dlg, 1);

  int (*p_popup_menu)(int, int, const char **, int) = gfx2d_popup_menu;
  BIND("popup_menu", p_popup_menu, 4);

  /* Desktop icon system */
  int (*p_icon_register)(const char *, const char *, int, int) =
      gfx2d_icon_register;
  BIND("register_desktop_icon", p_icon_register, 4);

  void (*p_icon_set_desc)(int, const char *) = gfx2d_icon_set_desc;
  BIND("set_icon_desc", p_icon_set_desc, 2);

  void (*p_icon_set_type)(int, int) = gfx2d_icon_set_type;
  BIND("set_icon_type", p_icon_set_type, 2);

  void (*p_icon_set_color)(int, uint32_t) = gfx2d_icon_set_color;
  BIND("set_icon_color", p_icon_set_color, 2);

  void (*p_icon_set_drawer)(int, void (*)(int, int)) =
      gfx2d_icon_set_custom_drawer;
  BIND("set_icon_drawer", p_icon_set_drawer, 2);

    void (*p_icon_draw_named)(const char *, int, int, uint32_t) =
      gfx2d_icon_draw_named;
    BIND("gfx2d_icon_draw_named", p_icon_draw_named, 4);

  int (*p_icon_find)(const char *) = gfx2d_icon_find_by_path;
  BIND("get_my_icon_handle", p_icon_find, 1);

  void (*p_icon_set_pos)(int, int, int) = gfx2d_icon_set_pos;
  BIND("set_icon_pos", p_icon_set_pos, 3);

  const char *(*p_icon_get_label)(int) = gfx2d_icon_get_label;
  BIND("get_icon_label", p_icon_get_label, 1);

  const char *(*p_icon_get_path)(int) = gfx2d_icon_get_path;
  BIND("get_icon_path", p_icon_get_path, 1);

  int (*p_icon_at_pos)(int, int) = gfx2d_icon_at_pos;
  BIND("icon_at_pos", p_icon_at_pos, 2);

  int (*p_icon_count)(void) = gfx2d_icon_count;
  BIND("icon_count", p_icon_count, 0);

  void (*p_icons_save)(void) = gfx2d_icons_save;
  BIND("icons_save", p_icons_save, 0);

#undef BIND
}

/* Source File / Preprocessor Helpers */

#define CC_PP_MAX_OUTPUT (512u * 1024u)
#define CC_PP_MAX_MACROS 128
#define CC_PP_MAX_MACRO_VALUE 256
#define CC_PP_MAX_INCLUDE_DEPTH 8
#define CC_PP_MAX_PATH 256
#define CC_PP_MAX_COND_DEPTH 32
#define CC_PP_MAX_EXE_FUNCS 128

typedef struct {
  char name[CC_MAX_IDENT];
  char value[CC_PP_MAX_MACRO_VALUE];
} cc_pp_macro_t;

typedef struct {
  cc_pp_macro_t macros[CC_PP_MAX_MACROS];
  int macro_count;

  int in_block_comment;
  int active;
  int cond_depth;
  int cond_parent[CC_PP_MAX_COND_DEPTH];
  int cond_taken[CC_PP_MAX_COND_DEPTH];

  char *out;
  uint32_t out_len;
  uint32_t out_cap;

  int error;
  const char *error_msg;

  int jit_mode;
  int exe_skip_depth;
  int exe_skip_reported;
  int exe_capture_depth;
  int exe_func_counter;
} cc_pp_state_t;

static int cc_pp_is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int cc_pp_is_alpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int cc_pp_is_alnum(char c) {
  return cc_pp_is_alpha(c) || (c >= '0' && c <= '9');
}

static int cc_pp_word_eq(const char *start, const char *end, const char *word) {
  int i = 0;
  while (start + i < end && word[i]) {
    if (start[i] != word[i])
      return 0;
    i++;
  }
  return (start + i == end) && (word[i] == '\0');
}

static void cc_pp_set_error(cc_pp_state_t *pp, const char *msg) {
  if (pp->error)
    return;
  pp->error = 1;
  pp->error_msg = msg;
}

static void cc_pp_append_char(cc_pp_state_t *pp, char c) {
  if (pp->error)
    return;
  if (pp->out_len + 1u >= pp->out_cap) {
    cc_pp_set_error(pp, "expanded source too large");
    return;
  }
  pp->out[pp->out_len++] = c;
}

static void cc_pp_append_range(cc_pp_state_t *pp, const char *start,
                               const char *end) {
  const char *p = start;
  while (!pp->error && p < end) {
    cc_pp_append_char(pp, *p++);
  }
}

static void cc_pp_append_text(cc_pp_state_t *pp, const char *s) {
  while (!pp->error && *s) {
    cc_pp_append_char(pp, *s++);
  }
}

static void cc_pp_append_uint_dec(cc_pp_state_t *pp, uint32_t v) {
  char buf[16];
  int i = 0;
  if (v == 0) {
    cc_pp_append_char(pp, '0');
    return;
  }
  while (v > 0 && i < (int)(sizeof(buf) - 1)) {
    buf[i++] = (char)('0' + (v % 10u));
    v /= 10u;
  }
  while (i > 0) {
    cc_pp_append_char(pp, buf[--i]);
  }
}

static void cc_pp_update_brace_depth(const char *start, const char *end,
                                     int *depth) {
  const char *p = start;
  int in_str = 0;
  char q = '\0';
  while (p < end) {
    if (!in_str && *p == '/' && (p + 1) < end && p[1] == '/') {
      break;
    }
    if (!in_str && (*p == '"' || *p == '\'')) {
      in_str = 1;
      q = *p;
      p++;
      continue;
    }
    if (in_str) {
      if (*p == '\\' && (p + 1) < end) {
        p += 2;
        continue;
      }
      if (*p == q) {
        in_str = 0;
      }
      p++;
      continue;
    }
    if (*p == '{') {
      (*depth)++;
    } else if (*p == '}') {
      (*depth)--;
    }
    p++;
  }
}

static char *cc_read_source(const char *path) {
  int fd = vfs_open(path, O_RDONLY);
  if (fd < 0) {
    print("CupidC: cannot open ");
    print(path);
    print("\n");
    return NULL;
  }

  /* Get file size via stat */
  vfs_stat_t st;
  if (vfs_stat(path, &st) < 0) {
    vfs_close(fd);
    print("CupidC: cannot stat ");
    print(path);
    print("\n");
    return NULL;
  }

  uint32_t size = st.size;
  if (size == 0 || size > 256 * 1024) {
    vfs_close(fd);
    print("CupidC: file too large or empty\n");
    return NULL;
  }

  char *source = kmalloc(size + 1);
  if (!source) {
    vfs_close(fd);
    print("CupidC: out of memory\n");
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

static int cc_pp_find_macro(cc_pp_state_t *pp, const char *name) {
  int i;
  for (i = 0; i < pp->macro_count; i++) {
    if (strcmp(pp->macros[i].name, name) == 0)
      return i;
  }
  return -1;
}

static void cc_pp_set_macro(cc_pp_state_t *pp, const char *name,
                            const char *value) {
  int idx = cc_pp_find_macro(pp, name);
  if (idx < 0) {
    if (pp->macro_count >= CC_PP_MAX_MACROS) {
      cc_pp_set_error(pp, "too many #define macros");
      return;
    }
    idx = pp->macro_count++;
  }

  strncpy(pp->macros[idx].name, name, CC_MAX_IDENT - 1);
  pp->macros[idx].name[CC_MAX_IDENT - 1] = '\0';
  strncpy(pp->macros[idx].value, value, CC_PP_MAX_MACRO_VALUE - 1);
  pp->macros[idx].value[CC_PP_MAX_MACRO_VALUE - 1] = '\0';
}

static void cc_pp_resolve_include(const char *base_path, const char *inc_path,
                                  char *out_path) {
  if (inc_path[0] == '/') {
    strncpy(out_path, inc_path, CC_PP_MAX_PATH - 1);
    out_path[CC_PP_MAX_PATH - 1] = '\0';
    return;
  }

  const char *slash = strrchr(base_path, '/');
  if (!slash) {
    strncpy(out_path, inc_path, CC_PP_MAX_PATH - 1);
    out_path[CC_PP_MAX_PATH - 1] = '\0';
    return;
  }

  int dir_len = (int)(slash - base_path + 1);
  if (dir_len < 0)
    dir_len = 0;
  if (dir_len > CC_PP_MAX_PATH - 1)
    dir_len = CC_PP_MAX_PATH - 1;

  memcpy(out_path, base_path, (size_t)dir_len);
  int oi = dir_len;
  int ii = 0;
  while (inc_path[ii] && oi < CC_PP_MAX_PATH - 1) {
    out_path[oi++] = inc_path[ii++];
  }
  out_path[oi] = '\0';
}

static void cc_pp_expand_line(cc_pp_state_t *pp, const char *line_start,
                              const char *line_end) {
  const char *p = line_start;
  while (!pp->error && p < line_end) {
    if (pp->in_block_comment) {
      cc_pp_append_char(pp, *p);
      if (*p == '*' && (p + 1) < line_end && p[1] == '/') {
        cc_pp_append_char(pp, '/');
        p += 2;
        pp->in_block_comment = 0;
      } else {
        p++;
      }
      continue;
    }

    if (*p == '/' && (p + 1) < line_end && p[1] == '/') {
      cc_pp_append_range(pp, p, line_end);
      return;
    }

    if (*p == '/' && (p + 1) < line_end && p[1] == '*') {
      cc_pp_append_char(pp, '/');
      cc_pp_append_char(pp, '*');
      p += 2;
      pp->in_block_comment = 1;
      continue;
    }

    if (*p == '"' || *p == '\'') {
      char q = *p;
      cc_pp_append_char(pp, *p++);
      while (!pp->error && p < line_end) {
        char c = *p++;
        cc_pp_append_char(pp, c);
        if (c == '\\' && p < line_end) {
          cc_pp_append_char(pp, *p++);
          continue;
        }
        if (c == q)
          break;
      }
      continue;
    }

    if (cc_pp_is_alpha(*p)) {
      char ident[CC_MAX_IDENT];
      int len = 0;
      const char *id_start = p;
      while (p < line_end && cc_pp_is_alnum(*p)) {
        if (len < CC_MAX_IDENT - 1)
          ident[len++] = *p;
        p++;
      }
      ident[len] = '\0';

      int mi = cc_pp_find_macro(pp, ident);
      if (mi >= 0) {
        const char *val = pp->macros[mi].value;
        while (!pp->error && *val) {
          cc_pp_append_char(pp, *val++);
        }
      } else {
        cc_pp_append_range(pp, id_start, p);
      }
      continue;
    }

    cc_pp_append_char(pp, *p++);
  }
}

static void cc_pp_process_file(cc_pp_state_t *pp, const char *path, int depth);

static void cc_pp_handle_directive(cc_pp_state_t *pp, const char *cur_path,
                                   const char *line_start,
                                   const char *line_end, int depth) {
  const char *p = line_start;
  while (p < line_end && cc_pp_is_space(*p))
    p++;
  if (p >= line_end || *p != '#')
    return;
  p++; /* consume '#' */

  while (p < line_end && cc_pp_is_space(*p))
    p++;

  const char *kw_start = p;
  while (p < line_end && cc_pp_is_alpha(*p))
    p++;
  const char *kw_end = p;

  while (p < line_end && cc_pp_is_space(*p))
    p++;

  if (cc_pp_word_eq(kw_start, kw_end, "include")) {
    if (!pp->active)
      return;
    if (p >= line_end || *p != '"')
      return;
    p++;
    char inc_path[CC_PP_MAX_PATH];
    int pi = 0;
    while (p < line_end && *p != '"' && pi < CC_PP_MAX_PATH - 1) {
      inc_path[pi++] = *p++;
    }
    inc_path[pi] = '\0';
    if (p >= line_end || *p != '"') {
      cc_pp_set_error(pp, "malformed #include");
      return;
    }
    char resolved[CC_PP_MAX_PATH];
    cc_pp_resolve_include(cur_path, inc_path, resolved);
    cc_pp_process_file(pp, resolved, depth + 1);
    return;
  }

  if (cc_pp_word_eq(kw_start, kw_end, "define")) {
    if (!pp->active)
      return;

    if (p >= line_end || !cc_pp_is_alpha(*p)) {
      cc_pp_set_error(pp, "malformed #define");
      return;
    }

    char name[CC_MAX_IDENT];
    int ni = 0;
    while (p < line_end && cc_pp_is_alnum(*p)) {
      if (ni < CC_MAX_IDENT - 1)
        name[ni++] = *p;
      p++;
    }
    name[ni] = '\0';

    if (p < line_end && *p == '(') {
      /* Function-like macros are not part of this phase. */
      return;
    }

    while (p < line_end && cc_pp_is_space(*p))
      p++;

    const char *val_start = p;
    const char *val_end = line_end;
    while (val_end > val_start && cc_pp_is_space(*(val_end - 1)))
      val_end--;

    char value[CC_PP_MAX_MACRO_VALUE];
    int vi = 0;
    while (val_start < val_end && vi < CC_PP_MAX_MACRO_VALUE - 1) {
      value[vi++] = *val_start++;
    }
    value[vi] = '\0';
    cc_pp_set_macro(pp, name, value);
    return;
  }

  if (cc_pp_word_eq(kw_start, kw_end, "ifdef") ||
      cc_pp_word_eq(kw_start, kw_end, "ifndef")) {
    if (pp->cond_depth >= CC_PP_MAX_COND_DEPTH) {
      cc_pp_set_error(pp, "preprocessor nesting too deep");
      return;
    }
    char name[CC_MAX_IDENT];
    int ni = 0;
    while (p < line_end && cc_pp_is_alnum(*p) && ni < CC_MAX_IDENT - 1) {
      name[ni++] = *p++;
    }
    name[ni] = '\0';
    int defined = (ni > 0 && cc_pp_find_macro(pp, name) >= 0) ? 1 : 0;
    int cond_true = cc_pp_word_eq(kw_start, kw_end, "ifdef") ? defined : !defined;

    pp->cond_parent[pp->cond_depth] = pp->active;
    pp->cond_taken[pp->cond_depth] = cond_true ? 1 : 0;
    pp->active = pp->active && cond_true;
    pp->cond_depth++;
    return;
  }

  if (cc_pp_word_eq(kw_start, kw_end, "else")) {
    if (pp->cond_depth <= 0) {
      cc_pp_set_error(pp, "unmatched #else");
      return;
    }
    int idx = pp->cond_depth - 1;
    pp->active = pp->cond_parent[idx] && !pp->cond_taken[idx];
    pp->cond_taken[idx] = 1;
    return;
  }

  if (cc_pp_word_eq(kw_start, kw_end, "endif")) {
    if (pp->cond_depth <= 0) {
      cc_pp_set_error(pp, "unmatched #endif");
      return;
    }
    pp->cond_depth--;
    pp->active = pp->cond_parent[pp->cond_depth];
    return;
  }

  if (cc_pp_word_eq(kw_start, kw_end, "exe")) {
    if (!pp->active)
      return;

    while (p < line_end && cc_pp_is_space(*p))
      p++;
    if (p >= line_end || *p != '{') {
      cc_pp_set_error(pp, "malformed #exe (expected '{')");
      return;
    }

    if (pp->jit_mode) {
      int exe_id;
      int depth_local;
      if (pp->exe_func_counter >= CC_PP_MAX_EXE_FUNCS) {
        cc_pp_set_error(pp, "too many #exe blocks");
        return;
      }
      exe_id = pp->exe_func_counter++;

      cc_pp_append_text(pp, "void __cc_exe_");
      cc_pp_append_uint_dec(pp, (uint32_t)exe_id);
      cc_pp_append_text(pp, "(void) ");
      cc_pp_expand_line(pp, p, line_end);

      depth_local = 0;
      cc_pp_update_brace_depth(p, line_end, &depth_local);
      pp->exe_capture_depth = depth_local;
    } else {
      pp->exe_skip_depth = 1;
      pp->exe_skip_reported = 1;
    }
    return;
  }
}

static void cc_pp_process_file(cc_pp_state_t *pp, const char *path, int depth) {
  if (pp->error)
    return;
  if (depth > CC_PP_MAX_INCLUDE_DEPTH) {
    cc_pp_set_error(pp, "include depth exceeded");
    return;
  }

  char *source = cc_read_source(path);
  if (!source) {
    cc_pp_set_error(pp, "cannot read source/include file");
    return;
  }

  const char *p = source;
  while (!pp->error && *p) {
    const char *line_start = p;
    while (*p && *p != '\n')
      p++;
    const char *line_end = p;

    if (pp->exe_capture_depth > 0) {
      int depth_delta = 0;
      cc_pp_expand_line(pp, line_start, line_end);
      cc_pp_update_brace_depth(line_start, line_end, &depth_delta);
      pp->exe_capture_depth += depth_delta;
      if (*p == '\n') {
        cc_pp_append_char(pp, '\n');
        p++;
      }
      continue;
    }

    if (pp->exe_skip_depth > 0) {
      int depth_delta = 0;
      cc_pp_update_brace_depth(line_start, line_end, &depth_delta);
      pp->exe_skip_depth += depth_delta;

      if (*p == '\n') {
        p++;
      }
      continue;
    }

    const char *s = line_start;
    while (s < line_end && cc_pp_is_space(*s))
      s++;

    if (s < line_end && *s == '#') {
      cc_pp_handle_directive(pp, path, line_start, line_end, depth);
    } else if (pp->active) {
      cc_pp_expand_line(pp, line_start, line_end);
    }

    if (*p == '\n') {
      cc_pp_append_char(pp, '\n');
      p++;
    }
  }

  kfree(source);
}

static char *cc_preprocess_source(const char *path, int jit_mode) {
  cc_pp_state_t pp;
  memset(&pp, 0, sizeof(pp));
  pp.active = 1;
  pp.jit_mode = jit_mode;
  pp.out_cap = CC_PP_MAX_OUTPUT;
  pp.out = kmalloc(pp.out_cap);
  if (!pp.out) {
    print("CupidC: out of memory for preprocessor\n");
    return NULL;
  }

  cc_pp_process_file(&pp, path, 0);

  if (!pp.error && pp.cond_depth != 0) {
    cc_pp_set_error(&pp, "unterminated #ifdef/#ifndef block");
  }

  if (!pp.error) {
    cc_pp_append_char(&pp, '\0');
  }

  if (!pp.error && !jit_mode && pp.exe_skip_reported) {
    print("CupidC: warning: #exe blocks skipped in AOT mode\n");
  }

  if (pp.error) {
    print("CupidC preprocess error");
    if (pp.error_msg) {
      print(": ");
      print(pp.error_msg);
    }
    print("\n");
    kfree(pp.out);
    return NULL;
  }

  return pp.out;
}

/* Compiler State Initialization */

static int cc_init_state(cc_state_t *cc, int jit_mode) {
  memset(cc, 0, sizeof(*cc));

  cc->jit_mode = jit_mode;
  cc->error = 0;
  cc->has_entry = 0;
  cc->patch_count = 0;
  cc->loop_depth = 0;
  cc->local_offset = 0;

  /* Allocate code and data buffers */
  cc->code = kmalloc(CC_MAX_CODE);
  cc->data = kmalloc(CC_MAX_DATA);

  if (!cc->code || !cc->data) {
    if (cc->code)
      kfree(cc->code);
    if (cc->data)
      kfree(cc->data);
    print("CupidC: out of memory for compiler buffers\n");
    return -1;
  }

  memset(cc->code, 0, CC_MAX_CODE);
  memset(cc->data, 0, CC_MAX_DATA);

  cc->code_pos = 0;
  cc->data_pos = 0;

  if (jit_mode) {
    /* JIT: code will be copied to executable region before running */
    cc->code_base = CC_JIT_CODE_BASE;
    cc->data_base = CC_JIT_DATA_BASE;
  } else {
    /* AOT: separate code and data regions so addresses are correct */
    cc->code_base = CC_AOT_CODE_BASE;
    cc->data_base = CC_AOT_DATA_BASE;
  }

  /* Initialize symbol table */
  cc_sym_init(cc);
  cc->struct_count = 0;

  /* Register kernel bindings */
  cc_register_kernel_bindings(cc);

  return 0;
}

static void cc_cleanup_state(cc_state_t *cc) {
  if (cc->code)
    kfree(cc->code);
  if (cc->data)
    kfree(cc->data);
  cc->code = NULL;
  cc->data = NULL;
}

static int cc_name_starts_with(const char *s, const char *prefix) {
  int i = 0;
  while (prefix[i]) {
    if (s[i] != prefix[i])
      return 0;
    i++;
  }
  return 1;
}

/* JIT Mode - Compile and Execute */

int cupidc_jit_status(const char *path) {
  serial_printf("[cupidc] JIT compile: %s\n", path);

  /* Read and preprocess source file */
  char *source = cc_preprocess_source(path, 1);
  if (!source)
    return -1;

  /* Heap-allocate compiler state (~24KB - too large for stack) */
  cc_state_t *cc = kmalloc(sizeof(cc_state_t));
  if (!cc) {
    print("CupidC: out of memory for compiler state\n");
    kfree(source);
    return -1;
  }
  if (cc_init_state(cc, 1) < 0) {
    kfree(cc);
    kfree(source);
    return -1;
  }

  /* Lex + parse + generate code */
  cc_lex_init(cc, source);
  cc_parse_program(cc);

  if (cc->error) {
    print(cc->error_msg);
    kfree(source);
    cc_cleanup_state(cc);
    kfree(cc);
    return -1;
  }

  if (!cc->has_entry) {
    print("CupidC: no entry point found (main or top-level statements)\n");
    kfree(source);
    cc_cleanup_state(cc);
    kfree(cc);
    return -1;
  }

  serial_printf("[cupidc] Compiled: %u bytes code, %u bytes data\n",
                cc->code_pos, cc->data_pos);

  /* Guard: reject programs that exceed JIT region limits */
  if (cc->code_pos > CC_MAX_CODE) {
    serial_printf("[cupidc] ERROR: code size %u exceeds max %u\n",
                  cc->code_pos, (unsigned)CC_MAX_CODE);
    print("CupidC: program too large (code overflow)\n");
    kfree(source);
    cc_cleanup_state(cc);
    kfree(cc);
    return -1;
  }
  if (cc->data_pos > CC_MAX_DATA) {
    serial_printf("[cupidc] ERROR: data size %u exceeds max %u\n",
                  cc->data_pos, (unsigned)CC_MAX_DATA);
    print("CupidC: program too large (data overflow)\n");
    kfree(source);
    cc_cleanup_state(cc);
    kfree(cc);
    return -1;
  }

  /* JIT code/data regions are permanently reserved at boot by pmm_init()
   * so the heap never allocates into them.  Just copy and execute. */

  /* Save the current JIT regions BEFORE overwriting (for nested JIT programs).
   * This must happen before the memcpy so we preserve the previous program. */
  shell_jit_program_start(path);

  /* Copy code and data to execution regions */
  memcpy((void *)CC_JIT_CODE_BASE, cc->code, cc->code_pos);
  memcpy((void *)CC_JIT_DATA_BASE, cc->data, cc->data_pos);

  /* Execute compile-time #exe functions once before normal entry. */
  {
    uint32_t called_offsets[CC_PP_MAX_EXE_FUNCS];
    int called_count = 0;
    int i;
    for (i = 0; i < cc->sym_count; i++) {
      cc_symbol_t *sym = &cc->symbols[i];
      if (sym->kind != SYM_FUNC || !sym->is_defined)
        continue;
      if (!cc_name_starts_with(sym->name, "__cc_exe_"))
        continue;

      /* Dedup duplicate function symbols that share the same offset. */
      {
        int j;
        int seen = 0;
        for (j = 0; j < called_count; j++) {
          if (called_offsets[j] == (uint32_t)sym->offset) {
            seen = 1;
            break;
          }
        }
        if (seen)
          continue;
      }

      {
        uint32_t fn_addr = CC_JIT_CODE_BASE + (uint32_t)sym->offset;
        void (*fn)(void);
        memcpy(&fn, &fn_addr, sizeof(fn));
        fn();
      }

      if (called_count < CC_PP_MAX_EXE_FUNCS) {
        called_offsets[called_count++] = (uint32_t)sym->offset;
      }
    }
  }

  /* Calculate entry point */
  uint32_t entry_addr = CC_JIT_CODE_BASE + cc->entry_offset;
  void (*entry_fn)(void);
  memcpy(&entry_fn, &entry_addr, sizeof(entry_fn));

  serial_printf("[cupidc] Executing at 0x%x\n", entry_addr);

  /* Check stack health before execution */
  stack_guard_check();

  /* Execute the program directly (JIT - synchronous) */
  entry_fn();

  /* Mark program as finished (routes GUI keyboard input back to shell) */
  shell_jit_program_end();

  /* Check stack health after execution */
  uint32_t usage_after = stack_usage_current();
  uint32_t usage_peak = stack_usage_peak();
  stack_guard_check();

  serial_printf("[cupidc] JIT execution complete (stack: %u bytes used, peak: "
                "%u bytes)\n",
                usage_after, usage_peak);

  /* Warn if stack usage is high */
  if (usage_peak > STACK_SIZE / 2) {
    serial_printf(
        "[cupidc] WARNING: High stack usage detected (%u KB / %u KB)\n",
        usage_peak / 1024, STACK_SIZE / 1024);
  }

  /* Clean up - do NOT release the JIT region; it stays reserved */
  kfree(source);
  cc_cleanup_state(cc);
  kfree(cc);
  return 0;
}

void cupidc_jit(const char *path) { (void)cupidc_jit_status(path); }

/* AOT Mode - Compile to ELF Binary */

void cupidc_aot(const char *src_path, const char *out_path) {
  serial_printf("[cupidc] AOT compile: %s -> %s\n", src_path, out_path);

  /* Read and preprocess source file */
  char *source = cc_preprocess_source(src_path, 0);
  if (!source)
    return;

  /* Heap-allocate compiler state (~24KB - too large for stack) */
  cc_state_t *cc = kmalloc(sizeof(cc_state_t));
  if (!cc) {
    print("CupidC: out of memory for compiler state\n");
    kfree(source);
    return;
  }
  if (cc_init_state(cc, 0) < 0) {
    kfree(cc);
    kfree(source);
    return;
  }

  /* Lex + parse + generate code */
  cc_lex_init(cc, source);
  cc_parse_program(cc);

  if (cc->error) {
    print(cc->error_msg);
    kfree(source);
    cc_cleanup_state(cc);
    kfree(cc);
    return;
  }

  if (!cc->has_entry) {
    print("CupidC: no entry point found (main or top-level statements)\n");
    kfree(source);
    cc_cleanup_state(cc);
    kfree(cc);
    return;
  }

  print("Compiled: ");
  print_int(cc->code_pos);
  print(" bytes code, ");
  print_int(cc->data_pos);
  print(" bytes data\n");

  /* Write ELF binary */
  int r = cc_write_elf(cc, out_path);
  if (r < 0) {
    print("CupidC: failed to write output file\n");
  } else {
    print("Written to ");
    print(out_path);
    print("\n");
  }

  kfree(source);
  cc_cleanup_state(cc);
  kfree(cc);
}

void cupidc_dis(const char *src_path, dis_output_fn out_fn) {
  char *source;
  cc_state_t *cc;
  dis_sym_t syms[DIS_MAX_SYMS];
  int i;
  int nsyms = 0;

  if (!src_path || src_path[0] == '\0') {
    if (out_fn)
      out_fn("cupidc dis: invalid source path\n");
    else
      print("cupidc dis: invalid source path\n");
    return;
  }

  source = cc_preprocess_source(src_path, 1);
  if (!source)
    return;

  cc = kmalloc(sizeof(cc_state_t));
  if (!cc) {
    if (out_fn)
      out_fn("CupidC: out of memory for compiler state\n");
    else
      print("CupidC: out of memory for compiler state\n");
    kfree(source);
    return;
  }

  if (cc_init_state(cc, 1) < 0) {
    kfree(cc);
    kfree(source);
    return;
  }

  cc_lex_init(cc, source);
  cc_parse_program(cc);

  if (cc->error) {
    if (out_fn)
      out_fn(cc->error_msg);
    else
      print(cc->error_msg);
    kfree(source);
    cc_cleanup_state(cc);
    kfree(cc);
    return;
  }

  for (i = 0; i < cc->sym_count && nsyms < DIS_MAX_SYMS; i++) {
    const cc_symbol_t *sym = &cc->symbols[i];
    int k;

    if (sym->kind != SYM_FUNC || !sym->is_defined) {
      continue;
    }

    syms[nsyms].addr = CC_JIT_CODE_BASE + (uint32_t)sym->offset;
    for (k = 0; k < (int)sizeof(syms[nsyms].name) - 1; k++) {
      char c = sym->name[k];
      syms[nsyms].name[k] = c;
      if (c == '\0') {
        break;
      }
    }
    syms[nsyms].name[sizeof(syms[nsyms].name) - 1] = '\0';
    nsyms++;
  }

  dis_disassemble(cc->code, cc->code_pos, CC_JIT_CODE_BASE, syms, nsyms, out_fn);

  kfree(source);
  cc_cleanup_state(cc);
  kfree(cc);
}
