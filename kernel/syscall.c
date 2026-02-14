/**
 * syscall.c — Kernel syscall table implementation for CupidOS
 *
 * Populates the global function-pointer table that is passed to ELF
 * programs at launch.  Each slot in the table points to the real
 * kernel function so user programs can call kernel services without
 * linking against the kernel binary.
 */

#include "syscall.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"
#include "exec.h"
#include "kernel.h"
#include "memory.h"
#include "process.h"
#include "shell.h"
#include "string.h"
#include "vfs.h"
#include "vfs_helpers.h"


/* ── Wrappers for functions that need adaptation ──────────────────── */

/**
 * Wrapper for kmalloc — strips the debug file/line tracking since
 * user programs don't have kernel source paths.
 */
static void *syscall_malloc(size_t size) {
  return kmalloc_debug(size, "elf_user", 0);
}

/**
 * Wrapper for process_exit — matches the void(*)(void) signature.
 */
static void syscall_exit(void) { process_exit(); }

/**
 * Wrapper for process_yield.
 */
static void syscall_yield(void) { process_yield(); }

/**
 * Wrapper for process_get_current_pid.
 */
static uint32_t syscall_getpid(void) { return process_get_current_pid(); }

/**
 * Wrapper for process_kill.
 */
static void syscall_kill(uint32_t pid) { process_kill(pid); }

/**
 * Simple busy-wait sleep using uptime.
 */
static void syscall_sleep_ms(uint32_t ms) {
  uint32_t start = timer_get_uptime_ms();
  while ((timer_get_uptime_ms() - start) < ms) {
    process_yield();
  }
}

/**
 * Wrapper for shell_execute_line.
 */
static void syscall_shell_execute(const char *line) {
  shell_execute_line(line);
}

/**
 * Wrapper for exec.
 */
static int syscall_exec(const char *path, const char *name) {
  return exec(path, name);
}

/* ── Global syscall table ─────────────────────────────────────────── */
static cupid_syscall_table_t syscall_table;

void syscall_init(void) {
  memset(&syscall_table, 0, sizeof(syscall_table));

  /* Version / identification */
  syscall_table.version = CUPID_SYSCALL_VERSION;
  syscall_table.table_size = (uint32_t)sizeof(cupid_syscall_table_t);

  /* Console output */
  syscall_table.print = print;
  syscall_table.putchar = putchar;
  syscall_table.print_int = print_int;
  syscall_table.print_hex = print_hex;
  syscall_table.clear_screen = clear_screen;

  /* Memory management */
  syscall_table.malloc = syscall_malloc;
  syscall_table.free = kfree;

  /* String operations */
  syscall_table.strlen = strlen;
  syscall_table.strcmp = strcmp;
  syscall_table.strncmp = strncmp;
  syscall_table.memset = memset;
  syscall_table.memcpy = memcpy;

  /* VFS file operations */
  syscall_table.vfs_open = vfs_open;
  syscall_table.vfs_close = vfs_close;
  syscall_table.vfs_read = vfs_read;
  syscall_table.vfs_write = vfs_write;
  syscall_table.vfs_seek = vfs_seek;
  syscall_table.vfs_stat = vfs_stat;
  syscall_table.vfs_readdir = vfs_readdir;
  syscall_table.vfs_mkdir = vfs_mkdir;
  syscall_table.vfs_unlink = vfs_unlink;
  syscall_table.vfs_rename = vfs_rename;
  syscall_table.vfs_copy_file = vfs_copy_file;
  syscall_table.vfs_read_all = vfs_read_all;
  syscall_table.vfs_write_all = vfs_write_all;
  syscall_table.vfs_read_text = vfs_read_text;
  syscall_table.vfs_write_text = vfs_write_text;

  /* Process management */
  syscall_table.exit = syscall_exit;
  syscall_table.yield = syscall_yield;
  syscall_table.getpid = syscall_getpid;
  syscall_table.kill = syscall_kill;
  syscall_table.sleep_ms = syscall_sleep_ms;

  /* Shell integration */
  syscall_table.shell_execute = syscall_shell_execute;
  syscall_table.shell_get_cwd = shell_get_cwd;

  /* Time */
  syscall_table.uptime_ms = timer_get_uptime_ms;

  /* Program execution */
  syscall_table.exec = syscall_exec;

  /* Diagnostics */
  syscall_table.memstats = print_memory_stats;

  serial_printf("[SYSCALL] Syscall table initialized (v%u, %u bytes)\n",
                syscall_table.version, syscall_table.table_size);
}

cupid_syscall_table_t *syscall_get_table(void) { return &syscall_table; }
