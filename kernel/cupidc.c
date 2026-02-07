/**
 * cupidc.c — CupidC compiler driver for CupidOS
 *
 * Provides the main entry points for JIT and AOT compilation:
 *   - cupidc_jit(): Compile and execute a .cc file immediately
 *   - cupidc_aot(): Compile a .cc file to an ELF32 binary on disk
 *
 * Initializes kernel function bindings so CupidC programs can call
 * print(), kmalloc(), outb(), inb(), and other kernel APIs directly.
 */

#include "cupidc.h"
#include "kernel.h"
#include "vfs.h"
#include "memory.h"
#include "string.h"
#include "ports.h"
#include "process.h"
#include "exec.h"
#include "shell.h"
#include "calendar.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "../drivers/rtc.h"
#include "blockcache.h"
#include "panic.h"
#include "math.h"
#include "ed.h"
#include "../drivers/keyboard.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Port I/O wrappers for CupidC kernel bindings
 *
 *  The compiler binds calls to outb()/inb() to these wrappers which
 *  match cdecl calling convention with 32-bit args on the stack.
 * ══════════════════════════════════════════════════════════════════════ */

static void cc_outb(uint32_t port, uint32_t value) {
    outb((uint16_t)port, (uint8_t)value);
}

static uint32_t cc_inb(uint32_t port) {
    return (uint32_t)inb((uint16_t)port);
}

/* ── Print a newline ─────────────────────────────────────────────── */
static void cc_println(const char *s) {
    print(s);
    print("\n");
}

/* ── Wrapper for process_yield ───────────────────────────────────── */
static void cc_yield(void) {
    process_yield();
}

/* ── Wrapper for process_exit ────────────────────────────────────── */
static void cc_exit(void) {
    process_exit();
}

/* ── Test counting process for spawn command ─────────────────────── */
static void cc_test_counting_process(void) {
    uint32_t pid = process_get_current_pid();
    for (int i = 0; i < 10; i++) {
        serial_printf("[PROCESS] PID %u count %d\n", pid, i);
        process_yield();
    }
}

/* ── Spawn N test processes, return count actually spawned ────────── */
static uint32_t cc_spawn_test(uint32_t count) {
    if (count > 16) count = 16;
    uint32_t spawned = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t pid = process_create(cc_test_counting_process, "test",
                                      DEFAULT_STACK_SIZE);
        if (pid == 0) break;
        print("Spawned PID ");
        print_int(pid);
        print("\n");
        spawned++;
    }
    return spawned;
}

/* ── RTC accessors (CupidC can't access struct fields) ───────────── */
static int cc_rtc_hour(void) {
    rtc_time_t t; rtc_read_time(&t); return (int)t.hour;
}
static int cc_rtc_minute(void) {
    rtc_time_t t; rtc_read_time(&t); return (int)t.minute;
}
static int cc_rtc_second(void) {
    rtc_time_t t; rtc_read_time(&t); return (int)t.second;
}
static int cc_rtc_day(void) {
    rtc_date_t d; rtc_read_date(&d); return (int)d.day;
}
static int cc_rtc_month(void) {
    rtc_date_t d; rtc_read_date(&d); return (int)d.month;
}
static int cc_rtc_year(void) {
    rtc_date_t d; rtc_read_date(&d); return (int)d.year;
}
static int cc_rtc_weekday(void) {
    rtc_date_t d; rtc_read_date(&d); return (int)d.weekday;
}

/* Format date as "Thursday, February 6, 2026" into static buffer */
static char cc_date_full_buf[48];
static const char *cc_date_full_string(void) {
    rtc_date_t d; rtc_read_date(&d);
    format_date_full(&d, cc_date_full_buf, 48);
    return cc_date_full_buf;
}

/* Format date as "Feb 6, 2026" into static buffer */
static char cc_date_short_buf[20];
static const char *cc_date_short_string(void) {
    rtc_date_t d; rtc_read_date(&d);
    format_date_short(&d, cc_date_short_buf, 20);
    return cc_date_short_buf;
}

/* Format time as "6:32:15 PM" into static buffer */
static char cc_time_buf[20];
static const char *cc_time_string(void) {
    rtc_time_t t; rtc_read_time(&t);
    format_time_12hr_sec(&t, cc_time_buf, 20);
    return cc_time_buf;
}

/* Format time as "6:32 PM" into static buffer */
static char cc_time_short_buf[20];
static const char *cc_time_short_string(void) {
    rtc_time_t t; rtc_read_time(&t);
    format_time_12hr(&t, cc_time_short_buf, 20);
    return cc_time_short_buf;
}

/* ── Mount info accessors (CupidC can't access struct fields) ────── */
static const char *cc_mount_name(int index) {
    const vfs_mount_t *m = vfs_get_mount(index);
    if (m && m->mounted && m->ops) return m->ops->name;
    return NULL;
}

static const char *cc_mount_path(int index) {
    const vfs_mount_t *m = vfs_get_mount(index);
    if (m && m->mounted) return m->path;
    return NULL;
}

/* ── Debug/System wrappers for CupidC ────────────────────────────── */

/* CupidC can't do inline asm, so we provide a wrapper that captures
 * the current EBP/EIP and calls print_stack_trace(). */
static void cc_dump_stack_trace(void) {
    uint32_t ebp, eip;
    __asm__ volatile("movl %%ebp, %0" : "=r"(ebp));
    __asm__ volatile("call 1f\n1: popl %0" : "=r"(eip));
    print_stack_trace(ebp, eip);
}

/* CupidC can't do inline asm — capture and print all CPU registers */
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
    print("  EFLAGS: "); print_hex(eflags_v); print("\n");
}

/* get_cpu_freq returns uint64_t but CupidC only has 32-bit ints */
static uint32_t cc_get_cpu_mhz(void) {
    return (uint32_t)(get_cpu_freq() / 1000000);
}

/* Read a single byte from a given memory address */
static int cc_peek_byte(uint32_t addr) {
    return (int)*((volatile uint8_t *)addr);
}

/* is_gui_mode — wrapper for shell_get_output_mode() */
static uint32_t cc_is_gui_mode(void) {
    return (shell_get_output_mode() == SHELL_OUTPUT_GUI) ? 1 : 0;
}

/* kernel_panic is variadic — provide simple 1-arg wrapper */
static void cc_kernel_panic_msg(const char *msg) {
    kernel_panic("%s", msg);
}

/* Crashtest wrappers — CupidC can't do volatile pointer tricks etc. */
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
        memset(buf, 'A', 32);  /* overflow by 16 bytes */
        kfree(buf);            /* triggers canary detection */
    }
}

static void cc_crashtest_stackoverflow(void) {
    volatile char big[65536];
    big[0] = 'x';
    big[65535] = 'y';
    (void)big;
}

/* Print a byte as 2 hex digits — wrapper with uint32_t arg for CupidC */
static void cc_print_hex_byte(uint32_t val) {
    print_hex_byte((uint8_t)val);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Kernel Bindings Registration
 * ══════════════════════════════════════════════════════════════════════ */

static void cc_register_kernel_bindings(cc_state_t *cc) {
    /* Helper macro to add a kernel function binding */
    #define BIND(name_str, func_ptr, nparams) do {         \
        cc_symbol_t *s = cc_sym_add(cc, name_str,          \
                                    SYM_KERNEL, TYPE_VOID); \
        if (s) {                                            \
            uint32_t addr;                                  \
            memcpy(&addr, &(func_ptr), sizeof(addr));       \
            s->address = addr;                              \
            s->param_count = (nparams);                     \
            s->is_defined = 1;                              \
        }                                                   \
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

    /* Process management — extended */
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

    /* TempleOS-style argument passing: CupidC programs call get_args()
     * to receive command-line arguments set by the shell. */
    const char *(*p_get_args)(void) = shell_get_program_args;
    BIND("get_args", p_get_args, 0);

    /* Timer */
    uint32_t (*p_uptime)(void) = timer_get_uptime_ms;
    BIND("uptime_ms", p_uptime, 0);

    /* RTC — individual field accessors */
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

    /* RTC — formatted string accessors */
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

    /* Memory diagnostics — extended */
    void (*p_detect_leaks)(uint32_t) = detect_memory_leaks;
    BIND("detect_memory_leaks", p_detect_leaks, 1);

    void (*p_heap_check)(void) = heap_check_integrity;
    BIND("heap_check_integrity", p_heap_check, 0);

    uint32_t (*p_pmm_free)(void) = pmm_free_pages;
    BIND("pmm_free_pages", p_pmm_free, 0);

    uint32_t (*p_pmm_total)(void) = pmm_total_pages;
    BIND("pmm_total_pages", p_pmm_total, 0);

    /* Timer — extended */
    uint32_t (*p_timer_freq)(void) = timer_get_frequency;
    BIND("timer_get_frequency", p_timer_freq, 0);

    /* CPU info */
    uint32_t (*p_cpu_mhz)(void) = cc_get_cpu_mhz;
    BIND("get_cpu_mhz", p_cpu_mhz, 0);

    /* Process info — extended */
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

    /* GUI mode query */
    uint32_t (*p_is_gui)(void) = cc_is_gui_mode;
    BIND("is_gui_mode", p_is_gui, 0);

    /* VFS mount count */
    int (*p_vfs_mount_count)(void) = vfs_mount_count;
    BIND("vfs_mount_count", p_vfs_mount_count, 0);

    /* Keyboard input */
    char (*p_getchar)(void) = getchar;
    BIND("getchar", p_getchar, 0);

    /* String operations — extended */
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

    #undef BIND
}

/* ══════════════════════════════════════════════════════════════════════
 *  Source File Reading Helper
 * ══════════════════════════════════════════════════════════════════════ */

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
        if (chunk > 512) chunk = 512;
        int r = vfs_read(fd, source + total, chunk);
        if (r <= 0) break;
        total += (uint32_t)r;
    }
    source[total] = '\0';

    vfs_close(fd);
    return source;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Compiler State Initialization
 * ══════════════════════════════════════════════════════════════════════ */

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
        if (cc->code) kfree(cc->code);
        if (cc->data) kfree(cc->data);
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
    if (cc->code) kfree(cc->code);
    if (cc->data) kfree(cc->data);
    cc->code = NULL;
    cc->data = NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 *  JIT Mode — Compile and Execute
 * ══════════════════════════════════════════════════════════════════════ */

void cupidc_jit(const char *path) {
    serial_printf("[cupidc] JIT compile: %s\n", path);

    /* Read source file */
    char *source = cc_read_source(path);
    if (!source) return;

    /* Heap-allocate compiler state (~24KB — too large for stack) */
    cc_state_t *cc = kmalloc(sizeof(cc_state_t));
    if (!cc) {
        print("CupidC: out of memory for compiler state\n");
        kfree(source);
        return;
    }
    if (cc_init_state(cc, 1) < 0) {
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
        print("CupidC: no main() function found\n");
        kfree(source);
        cc_cleanup_state(cc);
        kfree(cc);
        return;
    }

    serial_printf("[cupidc] Compiled: %u bytes code, %u bytes data\n",
                  cc->code_pos, cc->data_pos);

    /* JIT code/data regions are permanently reserved at boot by pmm_init()
     * so the heap never allocates into them.  Just copy and execute. */

    /* Copy code and data to execution regions */
    memcpy((void *)CC_JIT_CODE_BASE, cc->code, cc->code_pos);
    memcpy((void *)CC_JIT_DATA_BASE, cc->data, cc->data_pos);

    /* Calculate entry point */
    uint32_t entry_addr = CC_JIT_CODE_BASE + cc->entry_offset;
    void (*entry_fn)(void);
    memcpy(&entry_fn, &entry_addr, sizeof(entry_fn));

    serial_printf("[cupidc] Executing at 0x%x\n", entry_addr);

    /* Check stack health before execution */
    stack_guard_check();

    /* Mark program as running (routes GUI keyboard input to program) */
    shell_jit_program_start();

    /* Execute the program directly (JIT — synchronous) */
    entry_fn();

    /* Mark program as finished (routes GUI keyboard input back to shell) */
    shell_jit_program_end();

    /* Check stack health after execution */
    uint32_t usage_after = stack_usage_current();
    uint32_t usage_peak = stack_usage_peak();
    stack_guard_check();

    serial_printf("[cupidc] JIT execution complete (stack: %u bytes used, peak: %u bytes)\n",
                  usage_after, usage_peak);

    /* Warn if stack usage is high */
    if (usage_peak > STACK_SIZE / 2) {
        serial_printf("[cupidc] WARNING: High stack usage detected (%u KB / %u KB)\n",
                      usage_peak / 1024, STACK_SIZE / 1024);
    }

    /* Clean up — do NOT release the JIT region; it stays reserved */
    kfree(source);
    cc_cleanup_state(cc);
    kfree(cc);
}

/* ══════════════════════════════════════════════════════════════════════
 *  AOT Mode — Compile to ELF Binary
 * ══════════════════════════════════════════════════════════════════════ */

void cupidc_aot(const char *src_path, const char *out_path) {
    serial_printf("[cupidc] AOT compile: %s -> %s\n", src_path, out_path);

    /* Read source file */
    char *source = cc_read_source(src_path);
    if (!source) return;

    /* Heap-allocate compiler state (~24KB — too large for stack) */
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
        print("CupidC: no main() function found\n");
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
