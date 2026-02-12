/**
 * syscall.h — Kernel syscall table for CupidOS ELF programs
 *
 * Provides a function-pointer table that the ELF loader passes to
 * user programs.  Since CupidOS runs everything in ring 0 with a
 * flat address space (TempleOS-style), there is no privilege boundary
 * — the table simply gives ELF programs clean access to kernel APIs
 * without needing to know fixed addresses.
 *
 * Usage from an ELF program:
 *   void _start(cupid_syscall_table_t *sys) {
 *       sys->print("Hello from ELF!\n");
 *       sys->exit();
 *   }
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "vfs.h"

/* ── Syscall table version (bump when adding fields) ──────────────── */
#define CUPID_SYSCALL_VERSION 1

/* ── Syscall table structure ──────────────────────────────────────── */
typedef struct cupid_syscall_table {
  /* ── Version / identification ─────────────────────────────────── */
  uint32_t version;    /* CUPID_SYSCALL_VERSION             */
  uint32_t table_size; /* sizeof(cupid_syscall_table_t)     */

  /* ── Console output ───────────────────────────────────────────── */
  void (*print)(const char *str);
  void (*putchar)(char c);
  void (*print_int)(uint32_t num);
  void (*print_hex)(uint32_t num);
  void (*clear_screen)(void);

  /* ── Memory management ────────────────────────────────────────── */
  void *(*malloc)(size_t size);
  void (*free)(void *ptr);

  /* ── String operations ────────────────────────────────────────── */
  size_t (*strlen)(const char *s);
  int (*strcmp)(const char *a, const char *b);
  int (*strncmp)(const char *a, const char *b, size_t n);
  void *(*memset)(void *ptr, int value, size_t num);
  void *(*memcpy)(void *dest, const void *src, size_t n);

  /* ── VFS file operations ──────────────────────────────────────── */
  int (*vfs_open)(const char *path, uint32_t flags);
  int (*vfs_close)(int fd);
  int (*vfs_read)(int fd, void *buffer, uint32_t count);
  int (*vfs_write)(int fd, const void *buffer, uint32_t count);
  int (*vfs_seek)(int fd, int32_t offset, int whence);
  int (*vfs_stat)(const char *path, vfs_stat_t *st);
  int (*vfs_readdir)(int fd, vfs_dirent_t *dirent);
  int (*vfs_mkdir)(const char *path);
  int (*vfs_unlink)(const char *path);

  /* ── Process management ───────────────────────────────────────── */
  void (*exit)(void);
  void (*yield)(void);
  uint32_t (*getpid)(void);
  void (*kill)(uint32_t pid);
  void (*sleep_ms)(uint32_t ms);

  /* ── Shell integration ────────────────────────────────────────── */
  void (*shell_execute)(const char *line);
  const char *(*shell_get_cwd)(void);

  /* ── Time ─────────────────────────────────────────────────────── */
  uint32_t (*uptime_ms)(void);

  /* ── Program execution ────────────────────────────────────────── */
  int (*exec)(const char *path, const char *name);

  /* ── Argument retrieval ───────────────────────────────────────── */
  const char *(*get_args)(void);

} cupid_syscall_table_t;

/* ── Kernel-side API ──────────────────────────────────────────────── */

/**
 * syscall_init — Initialize the global syscall table.
 * Must be called during kernel boot after all subsystems are ready.
 */
void syscall_init(void);

/**
 * syscall_get_table — Get a pointer to the global syscall table.
 * The ELF loader passes this to user programs.
 */
cupid_syscall_table_t *syscall_get_table(void);

#endif /* SYSCALL_H */
