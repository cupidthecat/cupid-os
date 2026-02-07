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
#include "../drivers/serial.h"

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

    /* TempleOS-style argument passing: CupidC programs call get_args()
     * to receive command-line arguments set by the shell. */
    const char *(*p_get_args)(void) = shell_get_program_args;
    BIND("get_args", p_get_args, 0);

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
    if (size == 0 || size > 64 * 1024) {
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

    /* Reserve memory for execution */
    uint32_t code_pages = (cc->code_pos + 4095) / 4096;
    uint32_t data_pages = (cc->data_pos + 4095) / 4096;
    if (code_pages == 0) code_pages = 1;
    if (data_pages == 0) data_pages = 1;

    pmm_reserve_region(CC_JIT_CODE_BASE, code_pages * 4096);
    pmm_reserve_region(CC_JIT_DATA_BASE, data_pages * 4096);

    /* Copy code and data to execution regions */
    memcpy((void *)CC_JIT_CODE_BASE, cc->code, cc->code_pos);
    memcpy((void *)CC_JIT_DATA_BASE, cc->data, cc->data_pos);

    /* Calculate entry point */
    uint32_t entry_addr = CC_JIT_CODE_BASE + cc->entry_offset;
    void (*entry_fn)(void);
    memcpy(&entry_fn, &entry_addr, sizeof(entry_fn));

    serial_printf("[cupidc] Executing at 0x%x\n", entry_addr);

    /* Execute the program directly (JIT — synchronous) */
    entry_fn();

    /* Clean up */
    pmm_release_region(CC_JIT_CODE_BASE, code_pages * 4096);
    pmm_release_region(CC_JIT_DATA_BASE, data_pages * 4096);
    kfree(source);
    cc_cleanup_state(cc);
    kfree(cc);

    serial_printf("[cupidc] JIT execution complete\n");
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
