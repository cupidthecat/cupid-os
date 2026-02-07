/**
 * cupid.h — CupidOS User-Space API Header
 *
 * Include this header in ELF programs compiled for CupidOS.
 * It provides access to all kernel services through the syscall
 * table that the kernel passes to your _start() function.
 *
 * ── Quick Start ──────────────────────────────────────────────────
 *
 *   #include "cupid.h"
 *
 *   void _start(cupid_syscall_table_t *sys) {
 *       cupid_init(sys);
 *       print("Hello from ELF!\n");
 *       exit();
 *   }
 *
 * ── Compiling ────────────────────────────────────────────────────
 *
 *   gcc -m32 -fno-pie -nostdlib -static -ffreestanding -O2 \
 *       -I/path/to/cupid-os/user -c hello.c -o hello.o
 *   ld -m elf_i386 -Ttext=0x00400000 --oformat=elf32-i386 \
 *       -o hello hello.o
 *
 * ── Running in CupidOS ──────────────────────────────────────────
 *
 *   exec /home/hello
 *
 * ─────────────────────────────────────────────────────────────────
 */

#ifndef CUPID_H
#define CUPID_H

/* ── Base types (no libc available) ───────────────────────────────── */
#ifndef NULL
#define NULL ((void *)0)
#endif

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;
typedef unsigned long      size_t;

/* ── VFS constants ────────────────────────────────────────────────── */
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0100
#define O_TRUNC    0x0200
#define O_APPEND   0x0400

#define SEEK_SET   0
#define SEEK_CUR   1
#define SEEK_END   2

#define VFS_TYPE_FILE   0
#define VFS_TYPE_DIR    1
#define VFS_TYPE_DEV    2

#define VFS_MAX_NAME    64
#define VFS_MAX_PATH    128

/* ── VFS structures (must match kernel definitions) ───────────────── */
typedef struct {
    char     name[VFS_MAX_NAME];
    uint32_t size;
    uint8_t  type;
} cupid_dirent_t;

typedef struct {
    uint32_t size;
    uint8_t  type;
} cupid_stat_t;

/* ── Syscall table (passed to _start) ─────────────────────────────── */
typedef struct cupid_syscall_table {
    /* Version / identification */
    uint32_t version;
    uint32_t table_size;

    /* Console output */
    void (*print)(const char *str);
    void (*putchar)(char c);
    void (*print_int)(uint32_t num);
    void (*print_hex)(uint32_t num);
    void (*clear_screen)(void);

    /* Memory management */
    void *(*malloc)(size_t size);
    void  (*free)(void *ptr);

    /* String operations */
    size_t (*strlen)(const char *s);
    int    (*strcmp)(const char *a, const char *b);
    int    (*strncmp)(const char *a, const char *b, size_t n);
    void  *(*memset)(void *ptr, int value, size_t num);
    void  *(*memcpy)(void *dest, const void *src, size_t n);

    /* VFS file operations */
    int (*vfs_open)(const char *path, uint32_t flags);
    int (*vfs_close)(int fd);
    int (*vfs_read)(int fd, void *buffer, uint32_t count);
    int (*vfs_write)(int fd, const void *buffer, uint32_t count);
    int (*vfs_seek)(int fd, int32_t offset, int whence);
    int (*vfs_stat)(const char *path, cupid_stat_t *st);
    int (*vfs_readdir)(int fd, cupid_dirent_t *dirent);
    int (*vfs_mkdir)(const char *path);
    int (*vfs_unlink)(const char *path);

    /* Process management */
    void     (*exit)(void);
    void     (*yield)(void);
    uint32_t (*getpid)(void);
    void     (*kill)(uint32_t pid);
    void     (*sleep_ms)(uint32_t ms);

    /* Shell integration */
    void (*shell_execute)(const char *line);
    const char *(*shell_get_cwd)(void);

    /* Time */
    uint32_t (*uptime_ms)(void);

    /* Program execution */
    int (*exec)(const char *path, const char *name);
} cupid_syscall_table_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Global syscall table pointer — set by cupid_init()
 * ══════════════════════════════════════════════════════════════════════ */
static cupid_syscall_table_t *__sys = NULL;

/**
 * cupid_init — Initialize the userspace API.
 * Call this at the start of _start() with the table pointer.
 */
static inline void cupid_init(cupid_syscall_table_t *sys) {
    __sys = sys;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Convenience wrappers — call these after cupid_init()
 * ══════════════════════════════════════════════════════════════════════ */

/* ── Console I/O ──────────────────────────────────────────────────── */
static inline void print(const char *s)     { __sys->print(s); }
static inline void putchar(char c)          { __sys->putchar(c); }
static inline void print_int(uint32_t n)    { __sys->print_int(n); }
static inline void print_hex(uint32_t n)    { __sys->print_hex(n); }
static inline void clear_screen(void)       { __sys->clear_screen(); }

/* ── Memory ───────────────────────────────────────────────────────── */
static inline void *malloc(size_t sz)       { return __sys->malloc(sz); }
static inline void  free(void *p)           { __sys->free(p); }

/* ── Strings ──────────────────────────────────────────────────────── */
static inline size_t strlen(const char *s)  { return __sys->strlen(s); }
static inline int strcmp(const char *a, const char *b)
    { return __sys->strcmp(a, b); }
static inline int strncmp(const char *a, const char *b, size_t n)
    { return __sys->strncmp(a, b, n); }
static inline void *memset(void *p, int v, size_t n)
    { return __sys->memset(p, v, n); }
static inline void *memcpy(void *d, const void *s, size_t n)
    { return __sys->memcpy(d, s, n); }

/* ── VFS ──────────────────────────────────────────────────────────── */
static inline int open(const char *path, uint32_t flags)
    { return __sys->vfs_open(path, flags); }
static inline int close(int fd)
    { return __sys->vfs_close(fd); }
static inline int read(int fd, void *buf, uint32_t count)
    { return __sys->vfs_read(fd, buf, count); }
static inline int write(int fd, const void *buf, uint32_t count)
    { return __sys->vfs_write(fd, buf, count); }
static inline int seek(int fd, int32_t off, int whence)
    { return __sys->vfs_seek(fd, off, whence); }
static inline int stat(const char *path, cupid_stat_t *st)
    { return __sys->vfs_stat(path, st); }
static inline int readdir(int fd, cupid_dirent_t *ent)
    { return __sys->vfs_readdir(fd, ent); }
static inline int mkdir(const char *path)
    { return __sys->vfs_mkdir(path); }
static inline int unlink(const char *path)
    { return __sys->vfs_unlink(path); }

/* ── Process ──────────────────────────────────────────────────────── */
static inline void exit(void)               { __sys->exit(); }
static inline void yield(void)              { __sys->yield(); }
static inline uint32_t getpid(void)         { return __sys->getpid(); }
static inline void kill(uint32_t pid)       { __sys->kill(pid); }
static inline void sleep_ms(uint32_t ms)    { __sys->sleep_ms(ms); }

/* ── Shell ────────────────────────────────────────────────────────── */
static inline void shell_execute(const char *line)
    { __sys->shell_execute(line); }
static inline const char *shell_get_cwd(void)
    { return __sys->shell_get_cwd(); }

/* ── Time ─────────────────────────────────────────────────────────── */
static inline uint32_t uptime_ms(void)      { return __sys->uptime_ms(); }

/* ── Program execution ────────────────────────────────────────────── */
static inline int exec_program(const char *path, const char *name)
    { return __sys->exec(path, name); }

#endif /* CUPID_H */
