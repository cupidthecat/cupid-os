# User Programs

cupid-os supports TempleOS-style user programs — drop a `.cc` file into `/bin/` or `/home/bin/` and the shell discovers it automatically. No kernel recompile, no linking, no build step. Just write the source and type the command name.

---

## How It Works

When you type a command the shell doesn't recognize as a built-in, it searches these locations in order:

| Search order | Path | Type | Storage |
|:---:|------|------|---------|
| 1 | `/bin/<cmd>` | ELF/CUPD binary | ramfs (memory) |
| 2 | `/bin/<cmd>.cc` | CupidC source | ramfs (memory) |
| 3 | `/home/bin/<cmd>` | ELF/CUPD binary | FAT16 (disk) |
| 4 | `/home/bin/<cmd>.cc` | CupidC source | FAT16 (disk) |

CupidC `.cc` files are JIT-compiled and run instantly. ELF binaries are loaded and executed as kernel threads.

Programs in `/bin/` (ramfs) are baked into the OS image at build time. Programs in `/home/bin/` (FAT16 disk) can be created, edited, and deployed at runtime — no reboot needed.

---

## Quick Start

### 1. Write a Program

Create a `.cc` file with a `main()` function. Use `get_args()` to receive command-line arguments and `get_cwd()` to get the current working directory.

```c
// hello.cc - simplest possible user program
void main() {
    println("Hello from a user program!");
}
```

### 2. Deploy It

**Option A: Build-time (ramfs)**

Place the file in the `bin/` directory in the source tree. When the OS boots, it appears at `/bin/hello.cc`. Type `hello` in the shell and it runs.

**Option B: Runtime (disk)**

Use the `ed` editor to create the file on disk:

```
> vmkdir /home/bin
> ed /home/bin/hello.cc
a
void main() {
    println("Hello from a user program!");
}
.
w
q
> hello
Hello from a user program!
```

### 3. Pass Arguments

Arguments are retrieved with `get_args()`, which returns a single string containing everything after the command name:

```c
// greet.cc - program that takes arguments
void main() {
    char *args = (char*)get_args();
    if (strlen(args) == 0) {
        println("Usage: greet <name>");
        return;
    }
    print("Hello, ");
    print(args);
    println("!");
}
```

```
> greet World
Hello, World!
```

---

## Program Structure

Every CupidC user program follows the same pattern:

```c
// 1. Helper functions (optional)
void my_helper() { ... }

// 2. main() entry point (required)
void main() {
    // 3. Get arguments from the shell
    char *args = (char*)get_args();

    // 4. Do work using kernel bindings
    // 5. Print output with print() / println()
}
```

### Rules

- Every program must have a `void main()` function
- No `#include` needed — kernel bindings are pre-registered
- No structs — use `char`, `int`, pointers, and arrays
- Max 64 KB code, 16 KB data per program
- Programs run in ring 0 with full kernel access

---

## Kernel Bindings Reference

These functions are available in every CupidC program without any includes. See the [CupidC Compiler](CupidC-Compiler) page for the full list.

### Shell Integration

| Function | Returns | Description |
|----------|---------|-------------|
| `get_args()` | `char*` | Command-line arguments (everything after the command name) |
| `get_cwd()` | `char*` | Current working directory (e.g. `"/home"`) |

### Console I/O

| Function | Description |
|----------|-------------|
| `print(char* s)` | Print a string |
| `println(char* s)` | Print a string followed by a newline |
| `putchar(char c)` | Print a single character |
| `print_int(int n)` | Print an integer |
| `print_hex(int n)` | Print a hex value |

### File System

| Function | Description |
|----------|-------------|
| `vfs_open(char* path, int flags)` | Open a file (returns fd) |
| `vfs_close(int fd)` | Close a file descriptor |
| `vfs_read(int fd, void* buf, int n)` | Read bytes from a file |
| `vfs_write(int fd, void* buf, int n)` | Write bytes to a file |
| `vfs_stat(char* path, void* buf)` | Get file info (5 bytes: 4 size + 1 type) |
| `vfs_readdir(int fd, void* ent)` | Read next directory entry |
| `vfs_mkdir(char* path)` | Create a directory |
| `vfs_unlink(char* path)` | Delete a file |
| `vfs_rename(char* old, char* new)` | Move/rename a file |

### String & Memory

| Function | Description |
|----------|-------------|
| `strlen(char* s)` | String length |
| `strcmp(char* a, char* b)` | Compare strings (0 = equal) |
| `strncmp(char* a, char* b, int n)` | Compare up to n characters |
| `memset(void* p, int v, int n)` | Fill memory |
| `memcpy(void* dst, void* src, int n)` | Copy memory |
| `kmalloc(int size)` | Allocate heap memory |
| `kfree(void* ptr)` | Free heap memory |

---

## Common Patterns

### Parsing Arguments

Most programs need to split a single argument string into separate tokens:

```c
void main() {
    char *args = (char*)get_args();
    char first[256];
    int ai = 0;
    int fi = 0;

    // Skip leading spaces
    while (args[ai] == ' ') ai = ai + 1;

    // Copy first token
    while (args[ai] && args[ai] != ' ') {
        first[fi] = args[ai];
        fi = fi + 1;
        ai = ai + 1;
    }
    first[fi] = 0;

    // 'first' now contains the first argument
    // args[ai] points to the rest
}
```

### Resolving Relative Paths

Convert a user-supplied path to an absolute VFS path:

```c
void resolve_path(char *out, char *path) {
    int i = 0;
    if (path[0] == '/') {
        while (path[i]) { out[i] = path[i]; i = i + 1; }
        out[i] = 0;
        return;
    }
    char *cwd = (char*)get_cwd();
    int ci = 0;
    while (cwd[ci]) { out[i] = cwd[ci]; i = i + 1; ci = ci + 1; }
    if (i > 1) { out[i] = '/'; i = i + 1; }
    int pi = 0;
    while (path[pi]) { out[i] = path[pi]; i = i + 1; pi = pi + 1; }
    out[i] = 0;
}
```

### Checking File/Directory Type

The `vfs_stat` binding writes into a buffer: 4 bytes for `size` (uint32), then 1 byte for `type`. Type values:

| Value | Meaning |
|:-----:|---------|
| 0 | `VFS_TYPE_FILE` |
| 1 | `VFS_TYPE_DIR` |
| 2 | `VFS_TYPE_DEV` |

```c
char stat_buf[8];
int st = vfs_stat("/home", stat_buf);
if (st >= 0 && stat_buf[4] == 1) {
    println("It's a directory");
}
```

### Reading a File

```c
void main() {
    int fd = vfs_open("/home/notes.txt", 0);
    if (fd < 0) {
        println("Cannot open file");
        return;
    }
    char buf[512];
    int n = vfs_read(fd, buf, 511);
    if (n > 0) {
        buf[n] = 0;
        println(buf);
    }
    vfs_close(fd);
}
```

### Listing a Directory

```c
void main() {
    int fd = vfs_open("/home", 0);
    if (fd < 0) {
        println("Cannot open directory");
        return;
    }
    // vfs_dirent_t is 69 bytes: 64 name + 4 size + 1 type
    char ent[69];
    while (vfs_readdir(fd, ent) > 0) {
        print("  ");
        println(ent);  // name is at offset 0
    }
    vfs_close(fd);
}
```

---

## Included Programs

### `mv` — Move/Rename Files

**Location:** `/bin/mv.cc`

Moves or renames files. If the destination is a directory, the file is moved into it keeping its original name.

```
> mv old.txt new.txt          # rename
> mv report.txt /tmp/         # move into directory
> mv /home/a.txt /home/docs/  # absolute paths
```

**How it works:**
1. Calls `get_args()` to get `<source> <dest>`
2. Parses into two separate path strings
3. Resolves relative paths using `get_cwd()`
4. Checks if dest is a directory via `vfs_stat()` — if so, appends the source filename
5. Calls `vfs_rename()` to perform the move (copy + delete)

---

## Tips

- **Keep files small.** Notepad has a 32 KB buffer. The CupidC compiler has a 64 KB source limit. Write compact code.
- **No verbose comments.** Every byte counts on an embedded OS. Use short comments or none.
- **Test with `cupidc`.** Run `cupidc /bin/yourprog.cc` to JIT-compile and test before deploying.
- **Use `println` for errors.** There's no stderr — just print error messages and `return`.
- **No structs.** Use `char` arrays and byte offsets to work with structured data (see the `vfs_stat` pattern above).
- **Programs in `/home/bin/` persist across reboots** since they're on the FAT16 disk. Programs in `/bin/` are in ramfs and rebuilt from source each boot.

---

## See Also

- [CupidC Compiler](CupidC-Compiler) — full language reference and compiler internals
- [Shell Commands](Shell-Commands) — all built-in commands
- [Filesystem](Filesystem) — VFS, ramfs, FAT16, and directory structure
- [ELF Programs](ELF-Programs) — compiling native C programs with GCC for cupid-os
