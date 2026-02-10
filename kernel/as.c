/**
 * as.c — CupidASM assembler driver for CupidOS
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
#include "../drivers/serial.h"
#include "../drivers/timer.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Read source file from VFS
 * ══════════════════════════════════════════════════════════════════════ */

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

/* ══════════════════════════════════════════════════════════════════════
 *  Assembler State Initialization / Cleanup
 * ══════════════════════════════════════════════════════════════════════ */

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
/* Helper: convert a function pointer to uint32_t address safely */
static uint32_t fn_to_u32(void (*fn)(void)) {
  uint32_t addr;
  memcpy(&addr, &fn, sizeof(addr));
  return addr;
}

/* Macro to bind a kernel function — casts any function type through
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

/* ── Thin wrappers for kernel APIs that are macros or misnamed ──── */
static void as_jit_exit(void) {
  /* For JIT mode, exit just returns — the caller (as_jit) handles cleanup */
}

static void *as_jit_malloc(size_t size) {
  return kmalloc_debug(size, "asm", 0);
}

/* Register kernel functions as pre-defined labels so asm programs can call
 * them directly (e.g. `call print`).  JIT and AOT share most bindings, but
 * `exit` differs: JIT returns to as_jit(), AOT must terminate its process. */
static void as_register_kernel_bindings(as_state_t *as, int jit_mode) {
  /* Console output */
  AS_BIND(as, "print",        print);
  AS_BIND(as, "putchar",      putchar);
  AS_BIND(as, "print_int",    print_int);
  AS_BIND(as, "print_hex",    print_hex);
  AS_BIND(as, "clear_screen", clear_screen);

  /* Memory */
  AS_BIND(as, "kmalloc",      as_jit_malloc);
  AS_BIND(as, "kfree",        kfree);

  /* String ops */
  AS_BIND(as, "strlen",       strlen);
  AS_BIND(as, "strcmp",        strcmp);
  AS_BIND(as, "memset",       memset);
  AS_BIND(as, "memcpy",       memcpy);

  /* VFS */
  AS_BIND(as, "vfs_open",     vfs_open);
  AS_BIND(as, "vfs_close",    vfs_close);
  AS_BIND(as, "vfs_read",     vfs_read);
  AS_BIND(as, "vfs_write",    vfs_write);

  /* Process */
  if (jit_mode) {
    AS_BIND(as, "exit",       as_jit_exit);
  } else {
    AS_BIND(as, "exit",       process_exit);
  }
  AS_BIND(as, "yield",        process_yield);
  AS_BIND(as, "sleep_ms",     timer_sleep_ms);

  /* Timer */
  AS_BIND(as, "uptime_ms",    timer_get_uptime_ms);

  /* Memory stats */
  AS_BIND(as, "memstats",     print_memory_stats);
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
  as_bind_equ(as, "SYS_SHELL_CWD",    116);
  as_bind_equ(as, "SYS_UPTIME_MS",    120);
  as_bind_equ(as, "SYS_EXEC",         124);

  return 0;
}

static void as_cleanup_state(as_state_t *as) {
  if (as->code) kfree(as->code);
  if (as->data) kfree(as->data);
  as->code = NULL;
  as->data = NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 *  JIT Mode — Assemble and Execute
 * ══════════════════════════════════════════════════════════════════════ */

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
  shell_jit_program_start(path);

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

  /* Execute the program directly (JIT — synchronous) */
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

/* ══════════════════════════════════════════════════════════════════════
 *  AOT Mode — Assemble to ELF Binary
 * ══════════════════════════════════════════════════════════════════════ */

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
