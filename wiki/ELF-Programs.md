# ELF Programs

CupidOS supports loading and running standard **ELF32 i386** executables compiled with GCC or Clang. Programs run as kernel threads in ring 0 and access kernel services through a **syscall table** — a struct of function pointers passed to the program's `_start()` entry point.

---

## Quick Start

### 1. Write a Program

Create a `.c` file that includes `cupid.h` and implements `_start()`:

```c
/* user/examples/hello.c */
#include "cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);

    print("Hello from an ELF program!\n");
    print("  PID: ");
    print_int(getpid());
    print("\n");

    exit();
}
```

### 2. Compile

```bash
# From the cupid-os root directory:
make -C user

# Or compile manually:
gcc -m32 -fno-pie -nostdlib -static -ffreestanding -O2 \
    -Iuser -c user/examples/hello.c -o hello.o
ld -m elf_i386 -Ttext=0x00400000 --oformat=elf32-i386 \
    -o hello hello.o
```

### 3. Deploy to Disk

Copy the binary onto the FAT16 disk image:

```bash
# Build image (or reuse existing)
make

# Copy programs (FAT root ::/ maps to CupidOS /home)
mcopy -o -i cupidos.img@@2097152 user/build/hello ::/HELLO
mcopy -o -i cupidos.img@@2097152 user/build/ls    ::/LS
mcopy -o -i cupidos.img@@2097152 user/build/cat   ::/CAT

# Verify
mdir -i cupidos.img@@2097152 ::/
```

### 4. Run in CupidOS

```
/home> exec /home/HELLO
Hello from an ELF program!
  PID: 4
```

---

## Architecture

### How ELF Programs Run

```
┌────────────────────┐
│   ELF Binary       │  (on FAT16 disk at /home/HELLO)
│   .text @ 0x400000 │
│   .data / .bss     │
└────────┬───────────┘
         │  exec("/home/HELLO", "hello")
         ▼
┌────────────────────────────────────────────────┐
│  1. Format Detection                           │
│     Read first 4 bytes → 0x7F 'E' 'L' 'F'     │
├────────────────────────────────────────────────┤
│  2. Header Validation                          │
│     ELF32, little-endian, i386, ET_EXEC        │
├────────────────────────────────────────────────┤
│  3. Scan PT_LOAD Segments                      │
│     Calculate vaddr range (min → max)          │
├────────────────────────────────────────────────┤
│  4. Reserve Physical Pages                     │
│     pmm_reserve_region(page_base, page_size)   │
├────────────────────────────────────────────────┤
│  5. Load Segments at Virtual Addresses         │
│     memset(0) entire region, then read each    │
│     segment directly to its p_vaddr            │
├────────────────────────────────────────────────┤
│  6. Create Process                             │
│     process_create_with_arg(entry, name,       │
│         stack_size, &syscall_table)             │
├────────────────────────────────────────────────┤
│  7. Schedule                                   │
│     process_yield() → new process runs         │
└────────────────────────────────────────────────┘
```

### Memory Model

CupidOS uses a **flat 32 MB identity-mapped** address space. ELF programs are loaded directly at the virtual addresses specified in their program headers — no address translation needed.

```
Physical / Virtual Memory (32 MB identity-mapped):
┌─────────────────────────┬──────────────────────┐
│ 0x00000000 - 0x00010000 │ Reserved (64 KB)     │
│ 0x00010000 - 0x00040000 │ Kernel (~192 KB)     │
│ 0x00040000 - 0x00200000 │ Heap + Stacks        │
│ 0x00400000 - ...        │ ELF Program Memory   │
│ ...                     │ ...                  │
│ 0x02000000              │ End of identity map   │
└─────────────────────────┴──────────────────────┘
```

Programs are linked at `0x00400000` by default (well above the kernel and heap). The PMM reserves the pages used by each ELF so they won't be allocated for other purposes. When the process exits, the pages are released back to the PMM.

### Syscall Table

Since CupidOS runs everything in ring 0 (TempleOS-style), there is no privilege boundary. Instead of traditional `int 0x80` syscalls, the kernel passes a **function pointer table** directly to each ELF program. The program calls kernel functions through this table.

```c
void _start(cupid_syscall_table_t *sys) {
    // sys->print("Hello!\n");     ← direct function call
    // sys->vfs_open("/home/f", 0) ← direct VFS access
    // sys->exit();                ← clean process exit
}
```

This design is simple, fast (no mode switches), and gives programs full kernel access.

---

## Compiling Programs

### Compiler Flags

| Flag | Purpose |
|------|---------|
| `-m32` | Generate 32-bit x86 code |
| `-fno-pie` | Disable position-independent executable |
| `-nostdlib` | Don't link the standard C library |
| `-static` | Static linking only (no shared libraries) |
| `-ffreestanding` | Freestanding environment, no hosted features |
| `-O2` | Optimization level 2 |
| `-Wall -Wextra` | Enable warnings |

### Linker Flags

| Flag | Purpose |
|------|---------|
| `-m elf_i386` | Target i386 ELF format |
| `-Ttext=0x00400000` | Set code base address (4 MB) |
| `--oformat=elf32-i386` | Output ELF32 format |

### User Makefile

The provided `user/Makefile` builds all example programs:

```bash
make -C user          # Build all programs
make -C user clean    # Clean build artifacts
```

To add a new program:
1. Create `user/examples/yourprog.c`
2. Add `yourprog` to the `PROGRAMS` list in `user/Makefile`
3. Run `make -C user`

### Program Structure

Every ELF program must:

1. **Include `cupid.h`** — provides types, constants, and wrapper functions
2. **Implement `_start(cupid_syscall_table_t *sys)`** — the entry point
3. **Call `cupid_init(sys)`** — stores the syscall table pointer globally
4. **Call `exit()` when done** — cleans up the process

```c
#include "cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);       // Required: save syscall table

    // ... your code here ...

    exit();                // Required: clean exit
}
```

> ⚠️ **Important:** If you don't call `exit()`, the process will return from `_start()` into undefined memory and likely crash the system.

---

## Syscall Table API Reference

After calling `cupid_init(sys)`, you can use these wrapper functions directly (no `sys->` prefix needed):

### Console Output

| Function | Signature | Description |
|----------|-----------|-------------|
| `print` | `void print(const char *s)` | Print a string to the terminal |
| `putchar` | `void putchar(char c)` | Print a single character |
| `print_int` | `void print_int(uint32_t n)` | Print an unsigned integer |
| `print_hex` | `void print_hex(uint32_t n)` | Print a hex number (0x...) |
| `clear_screen` | `void clear_screen(void)` | Clear the terminal screen |

### Memory Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `malloc` | `void *malloc(size_t size)` | Allocate heap memory |
| `free` | `void free(void *ptr)` | Free allocated memory |

### String Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `strlen` | `size_t strlen(const char *s)` | String length |
| `strcmp` | `int strcmp(const char *a, const char *b)` | Compare strings |
| `strncmp` | `int strncmp(const char *a, const char *b, size_t n)` | Compare N bytes |
| `memset` | `void *memset(void *p, int v, size_t n)` | Fill memory |
| `memcpy` | `void *memcpy(void *d, const void *s, size_t n)` | Copy memory |

### VFS File Operations

| Function | Signature | Description |
|----------|-----------|-------------|
| `open` | `int open(const char *path, uint32_t flags)` | Open a file (returns fd) |
| `close` | `int close(int fd)` | Close a file descriptor |
| `read` | `int read(int fd, void *buf, uint32_t count)` | Read bytes |
| `write` | `int write(int fd, const void *buf, uint32_t count)` | Write bytes |
| `seek` | `int seek(int fd, int32_t off, int whence)` | Seek in file |
| `stat` | `int stat(const char *path, cupid_stat_t *st)` | Get file info |
| `readdir` | `int readdir(int fd, cupid_dirent_t *ent)` | Read directory entry |
| `mkdir` | `int mkdir(const char *path)` | Create a directory |
| `unlink` | `int unlink(const char *path)` | Delete a file |

**Open flags:** `O_RDONLY` (0), `O_WRONLY` (1), `O_RDWR` (2), `O_CREAT` (0x100), `O_TRUNC` (0x200), `O_APPEND` (0x400)

**Seek modes:** `SEEK_SET` (0), `SEEK_CUR` (1), `SEEK_END` (2)

### Process Management

| Function | Signature | Description |
|----------|-----------|-------------|
| `exit` | `void exit(void)` | Terminate current process |
| `yield` | `void yield(void)` | Yield CPU to other processes |
| `getpid` | `uint32_t getpid(void)` | Get current process ID |
| `kill` | `void kill(uint32_t pid)` | Kill a process by PID |
| `sleep_ms` | `void sleep_ms(uint32_t ms)` | Sleep for N milliseconds |

### Shell Integration

| Function | Signature | Description |
|----------|-----------|-------------|
| `shell_execute` | `void shell_execute(const char *line)` | Execute a shell command |
| `shell_get_cwd` | `const char *shell_get_cwd(void)` | Get current working directory |

### Time

| Function | Signature | Description |
|----------|-----------|-------------|
| `uptime_ms` | `uint32_t uptime_ms(void)` | System uptime in milliseconds |

### Program Execution

| Function | Signature | Description |
|----------|-----------|-------------|
| `exec_program` | `int exec_program(const char *path, const char *name)` | Load and run another program |

---

## VFS Structures

These structures are defined in `cupid.h` and match the kernel's VFS types:

```c
/* Directory entry (from readdir) */
typedef struct {
    char     name[64];   /* File/directory name */
    uint32_t size;       /* File size in bytes */
    uint8_t  type;       /* VFS_TYPE_FILE, VFS_TYPE_DIR, or VFS_TYPE_DEV */
} cupid_dirent_t;

/* File status (from stat) */
typedef struct {
    uint32_t size;       /* File size in bytes */
    uint8_t  type;       /* VFS_TYPE_FILE, VFS_TYPE_DIR, or VFS_TYPE_DEV */
} cupid_stat_t;
```

---

## Example Programs

### hello.c — Hello World

```c
#include "cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);

    print("Hello from an ELF program!\n");
    print("  PID: ");
    print_int(getpid());
    print("\n");
    print("  Uptime: ");
    print_int(uptime_ms());
    print(" ms\n");
    print("  CWD: ");
    print(shell_get_cwd());
    print("\n");

    exit();
}
```

### ls.c — Directory Listing

```c
#include "cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);

    const char *path = shell_get_cwd();
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print("ls: cannot open ");
        print(path);
        print("\n");
        exit();
    }

    cupid_dirent_t ent;
    while (readdir(fd, &ent) > 0) {
        if (ent.type == VFS_TYPE_DIR)
            print("[DIR]  ");
        else if (ent.type == VFS_TYPE_DEV)
            print("[DEV]  ");
        else
            print("       ");

        print(ent.name);

        if (ent.type == VFS_TYPE_FILE) {
            print("  (");
            print_int(ent.size);
            print(" B)");
        }
        print("\n");
    }

    close(fd);
    exit();
}
```

### cat.c — Display File Contents

```c
#include "cupid.h"

void _start(cupid_syscall_table_t *sys) {
    cupid_init(sys);

    const char *path = "/home/readme.txt";

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        print("cat: cannot open ");
        print(path);
        print("\n");
        exit();
    }

    char buf[512];
    int n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        print(buf);
    }

    close(fd);
    exit();
}
```

---

## Deploying Programs to Disk

ELF binaries go on the FAT16 partition inside `cupidos.img`:

```bash
# Build the programs
make -C user

# Copy programs into FAT root (::/), which appears as /home in CupidOS
mcopy -o -i cupidos.img@@2097152 user/build/hello ::/HELLO
mcopy -o -i cupidos.img@@2097152 user/build/ls    ::/LS
mcopy -o -i cupidos.img@@2097152 user/build/cat   ::/CAT

# List contents
mdir -i cupidos.img@@2097152 ::/
```

Then in CupidOS:

```
/home> exec /home/HELLO
/home> exec /home/LS
/home> exec /home/CAT
```

---

## Technical Details

### ELF Header Validation

The loader checks all of the following before loading:

| Field | Required Value | Meaning |
|-------|----------------|---------|
| `e_ident[0..3]` | `0x7F 'E' 'L' 'F'` | ELF magic number |
| `e_ident[4]` | `1` (ELF_CLASS_32) | 32-bit ELF |
| `e_ident[5]` | `1` (ELF_DATA_LSB) | Little-endian |
| `e_type` | `2` (ET_EXEC) | Executable file |
| `e_machine` | `3` (EM_386) | Intel i386 |
| `e_phnum` | `> 0`, `≤ 16` | Has program headers |

### Address Constraints

| Constraint | Value | Reason |
|------------|-------|--------|
| Minimum vaddr | `0x00200000` (2 MB) | Stay above kernel/heap |
| Maximum vaddr | `0x02000000` (32 MB) | End of identity map |
| Max total image | 256 KB | `EXEC_MAX_SIZE` limit |
| Link address | `0x00400000` (default) | Convention for user programs |

### Memory Lifecycle

```
exec("/home/HELLO")
  │
  ├─ pmm_reserve_region(page_base, page_size)  ← pages locked
  ├─ load segments to vaddr
  ├─ process_create_with_arg(...)
  ├─ process_set_image(pid, page_base, page_size)
  │
  │  ... program runs ...
  │
  └─ exit()  →  process_exit()
                 │
                 └─ find_free_slot() reaps terminated process
                    └─ pmm_release_region(image_base, image_size)  ← pages freed
```

### BSS Handling

The BSS section (uninitialized global data) is handled implicitly: the loader `memset(0)`s the entire page-aligned region before loading file data, so any gap between `p_filesz` and `p_memsz` in a segment is already zeroed.

---

## Limitations

### Supported

- ✅ ELF32 i386 static executables
- ✅ Multiple PT_LOAD segments (.text, .data, .rodata, .bss)
- ✅ BSS zero-initialization
- ✅ Up to 256 KB per executable
- ✅ Full kernel API access (console, VFS, memory, process, shell)
- ✅ Automatic memory cleanup on process exit

### Not Supported

- ❌ Dynamic linking / shared libraries
- ❌ Position-independent executables (PIE)
- ❌ ELF relocations
- ❌ Thread-local storage (TLS)
- ❌ ELF64 (64-bit)
- ❌ Non-i386 architectures
- ❌ Command-line arguments (programs can't receive argc/argv)
- ❌ Standard C library (no libc — use syscall table wrappers)
- ❌ Multiple ELF programs at the same link address simultaneously

### Constraints

| Constraint | Value |
|------------|-------|
| Max executable size | 256 KB |
| Max program headers | 16 |
| Max concurrent processes | 32 |
| Stack per process | 8 KB (default) |
| Total system memory | 32 MB |
| Disk filename format | 8.3 uppercase (FAT16) |

---

## See Also

- [Filesystem](Filesystem) — VFS, mount points, FAT16 disk I/O
- [Process Management](Process-Management) — Scheduler, context switching
- [Shell Commands](Shell-Commands) — `exec` command reference
- [Architecture](Architecture) — System memory layout
