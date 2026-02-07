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

Place the file in the `bin/` directory in the source tree. The Makefile auto-discovers all `bin/*.cc` files, embeds them into the kernel image via `objcopy`, and generates the installation code automatically. When the OS boots, your program appears at `/bin/hello.cc`. Type `hello` in the shell and it runs.

To add a new build-time program:
1. Create `bin/<name>.cc`
2. Run `make`

That's it — no need to edit `kernel.c`, the `Makefile`, or any other file.

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
// 1. Help text (convention: //help: lines at the top)
//help: Short description of the program
//help: Usage: myprogram <args>
//help: More detailed explanation here.

// 2. Helper functions (optional)
void my_helper() { ... }

// 3. main() entry point (required)
void main() {
    // 4. Get arguments from the shell
    char *args = (char*)get_args();

    // 5. Do work using kernel bindings
    // 6. Print output with print() / println()
}
```

### The `//help:` Convention

Every program should start with `//help:` comment lines. These are read by the `help` command:

- **First `//help:` line** — one-line summary, shown in `help` listing
- **Additional `//help:` lines** — detailed usage, shown with `help <command>`

```c
//help: Move or rename files
//help: Usage: mv <source> <dest>
//help: If <dest> is a directory, moves the file into it.
//help: Supports both relative and absolute paths.
//help: Examples:
//help:   mv old.txt new.txt
//help:   mv report.txt /tmp/
```

The `help` command reads the source file itself to extract these lines — source IS the documentation.

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

| `uptime_ms()` | `int` | System uptime in milliseconds |
| `rtc_hour()` | `int` | Current hour (0-23) |
| `rtc_minute()` | `int` | Current minute (0-59) |
| `rtc_second()` | `int` | Current second (0-59) |
| `rtc_day()` | `int` | Current day of month (1-31) |
| `rtc_month()` | `int` | Current month (1-12) |
| `rtc_year()` | `int` | Current year (e.g. 2026) |
| `rtc_weekday()` | `int` | Day of week (0=Sunday, 6=Saturday) |
| `rtc_epoch()` | `int` | Seconds since Unix epoch |
| `date_full_string()` | `char*` | Formatted: "Thursday, February 6, 2026" |
| `date_short_string()` | `char*` | Formatted: "Feb 6, 2026" |
| `time_string()` | `char*` | Formatted: "6:32:15 PM" |
| `time_short_string()` | `char*` | Formatted: "6:32 PM" |

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

### Block Cache

| Function | Description |
|----------|-------------|
| `blockcache_sync()` | Flush all dirty cache blocks to disk |
| `blockcache_stats()` | Print cache hit/miss statistics |

### Debug & Diagnostics

| Function | Description |
|----------|-------------|
| `memstats()` | Print heap and physical memory statistics |
| `detect_memory_leaks(int ms)` | Report allocations older than `ms` milliseconds |
| `heap_check_integrity()` | Walk heap blocks and verify canary values |
| `pmm_free_pages()` | Number of free physical 4 KB pages |
| `pmm_total_pages()` | Total physical 4 KB pages |
| `dump_stack_trace()` | Print current EBP call stack |
| `dump_registers()` | Print all general-purpose CPU registers + EFLAGS |
| `peek_byte(int addr)` | Read one byte from a memory address |
| `print_hex_byte(int val)` | Print a byte as 2 hex digits |
| `get_cpu_mhz()` | CPU frequency in MHz |
| `timer_get_frequency()` | Timer interrupt rate in Hz |
| `process_get_count()` | Number of running processes |

### Serial Log Control

| Function | Description |
|----------|-------------|
| `set_log_level(int level)` | Set log level: 0=debug, 1=info, 2=warn, 3=error, 4=panic |
| `get_log_level_name()` | Current log level as a string |
| `print_log_buffer()` | Print the circular log buffer contents |

### Crash Testing

| Function | Description |
|----------|-------------|
| `kernel_panic(char* msg)` | Trigger a kernel panic |
| `crashtest_nullptr()` | Dereference a NULL pointer |
| `crashtest_divzero()` | Divide by zero |
| `crashtest_overflow()` | Overflow a heap buffer (canary detection) |
| `crashtest_stackoverflow()` | Allocate 64 KB on stack (page fault) |

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

### `help` — List Available Commands

**Location:** `/bin/help.cc`

Lists all programs in `/bin/` and `/home/bin/` with their one-line descriptions (read from `//help:` lines in each source file). Also supports `help <command>` to show detailed help for a specific command.

```
> help
CupidOS Commands
================

Programs (/bin):
  echo        - Print text to the terminal
  clear       - Clear the terminal screen
  help        - List available commands or show help for a command
  mv          - Move or rename files
  ...

Shell built-ins: cd, history, jobs

Type 'help <command>' for detailed help.
```

```
> help mv
Move or rename files
Usage: mv <source> <dest>
If <dest> is a directory, moves the file into it.
Supports both relative and absolute paths.
Examples:
  mv old.txt new.txt
  mv report.txt /tmp/
```

**Bindings used:** `get_args`, `vfs_open`, `vfs_read`, `vfs_readdir`, `vfs_close`, `strlen`, `print`, `println`

### `pwd` — Print Working Directory

**Location:** `/bin/pwd.cc`

Prints the absolute path of the current working directory.

```
> pwd
/home/bin
```

**Bindings used:** `get_cwd`, `println`

### `ls` — List Files and Directories

**Location:** `/bin/ls.cc`

Lists files and directories in the given path (or CWD if no argument). Directories are shown with `[DIR]`, devices with `[DEV]`, and file sizes in bytes for regular files.

```
> ls
[DIR]  bin
[DIR]  dev
[DIR]  home
       README.txt  42 bytes

> ls /bin
       echo.cc  196 bytes
       help.cc  2734 bytes
       ...
```

**How it works:** Uses `resolve_path()` to handle relative paths, then calls `vfs_readdir()` with a raw 69-byte buffer matching the `vfs_dirent_t` layout. Reads the type byte at offset 68 and reconstructs the 32-bit size from bytes at offset 64–67.

**Bindings used:** `get_args`, `resolve_path`, `vfs_open`, `vfs_readdir`, `vfs_close`, `print`, `println`, `print_int`

### `cat` — Display File Contents

**Location:** `/bin/cat.cc`

Prints the contents of a file to the terminal. Output is truncated at 64 KB for safety.

```
> cat README.txt
Welcome to CupidOS!
```

**Bindings used:** `get_args`, `resolve_path`, `strlen`, `vfs_open`, `vfs_read`, `vfs_close`, `print`, `println`, `putchar`

### `history` — Show Command History

**Location:** `/bin/history.cc`

Displays recently executed commands, numbered from oldest to newest.

```
> history
1: ls
2: cd /bin
3: cat echo.cc
4: history
```

**Bindings used:** `get_history_count`, `get_history_entry`, `print`, `print_int`

### `cd` — Change Directory

**Location:** `/bin/cd.cc`

Changes the shell's current working directory. With no arguments, changes to `/`. Supports relative paths, `.` (current), and `..` (parent).

```
> cd /bin
> pwd
/bin
> cd ..
> pwd
/
```

**How it works:** Uses `resolve_path()` to convert the input to an absolute path, then calls `vfs_stat()` with a raw 8-byte buffer to verify the target is a directory (type byte at offset 4 must equal 1). If valid, calls `set_cwd()` to update the shell's working directory.

**Bindings used:** `get_args`, `resolve_path`, `strlen`, `vfs_stat`, `set_cwd`, `print`, `println`

### `ps` — List Processes

**Location:** `/bin/ps.cc`

Displays all active processes with their PID, state, and name.

```
> ps
PID  STATE      NAME
---  ---------  ----------------
 1   READY      idle
 2   RUNNING    shell
Total: 2 process(es)
```

**Bindings used:** `process_list`

### `kill` — Kill a Process

**Location:** `/bin/kill.cc`

Terminates a process by PID. Cannot kill the idle process (PID 1).

```
> kill 3
Killing PID 3...
```

**How it works:** Parses the PID argument as a decimal integer, validates it (non-zero, not PID 1), then calls `process_kill()`.

**Bindings used:** `get_args`, `process_kill`, `print`, `print_int`, `println`

### `spawn` — Spawn Test Processes

**Location:** `/bin/spawn.cc`

Creates 1–16 test counting processes for scheduler testing. Each test process counts to 10 on the serial log with yields between counts.

```
> spawn 3
Spawned PID 3
Spawned PID 4
Spawned PID 5
```

**Bindings used:** `get_args`, `spawn_test`

### `yield` — Yield CPU

**Location:** `/bin/yield.cc`

Voluntarily gives up the CPU to the next ready process.

```
> yield
Yielding CPU...
```

**Bindings used:** `yield`, `println`

### `mount` — Show Mounted Filesystems

**Location:** `/bin/mount.cc`

Lists all mounted filesystems with their type and mount path.

```
> mount
ramfs on /
devfs on /dev
```

**How it works:** Calls `mount_count()` to get the total, then iterates with `mount_name(i)` and `mount_path(i)` to retrieve each entry's filesystem name and path. These wrapper bindings are needed because CupidC cannot access struct fields directly.

**Bindings used:** `mount_count`, `mount_name`, `mount_path`, `print`, `println`

### `echo` — Print Text

**Location:** `/bin/echo.cc`

Prints its arguments followed by a newline.

```
> echo Hello, CupidOS!
Hello, CupidOS!
```

**Bindings used:** `get_args`, `strlen`, `print`

### `clear` — Clear the Screen

**Location:** `/bin/clear.cc`

Clears the terminal by emitting ANSI escape sequences (`ESC[2J` to erase display, `ESC[H` to move cursor home). Works in both VGA text mode and GUI terminal.

```
> clear
```

**Bindings used:** `print`

### `time` — Show System Uptime

**Location:** `/bin/time.cc`

Displays system uptime in seconds and milliseconds using the `uptime_ms()` kernel binding.

```
> time
Uptime: 42.567s (42567 ms)
```

**Bindings used:** `uptime_ms`, `print_int`, `putchar`, `print`, `println`

### `reboot` — Reboot the Machine

**Location:** `/bin/reboot.cc`

Reboots the machine by sending the reset command (`0xFE`) to the keyboard controller at port `0x64`. Uses inline assembly to disable interrupts first.

```
> reboot
Rebooting...
```

**Bindings used:** `println`, `inb`, `outb`, inline asm (`cli`, `hlt`)

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

### `setcolor` — Set Terminal Color

**Location:** `/bin/setcolor.cc`

Sets the terminal foreground and optionally background color using ANSI escape codes.

```
> setcolor 2          # green text
> setcolor 4 7        # red text on white background
```

**How it works:** Parses the color number(s) from the argument string, then emits the appropriate ANSI escape sequence (`ESC[3Xm` for colors 0-7, `ESC[9Xm` for bright colors 8-15). Background colors use `ESC[4Xm`.

**Bindings used:** `get_args`, `strlen`, `putchar`, `println`

### `resetcolor` — Reset Terminal Colors

**Location:** `/bin/resetcolor.cc`

Resets terminal foreground and background colors to their defaults by emitting `ESC[0m`.

```
> resetcolor
```

**Bindings used:** `putchar`, `print`

### `printc` — Print Colored Text

**Location:** `/bin/printc.cc`

Prints text in a specific foreground color, then automatically resets to defaults.

```
> printc 14 Warning: disk nearly full
Warning: disk nearly full
> printc 2 Success!
Success!
```

**How it works:** Parses the color number, emits the ANSI color escape, prints the remaining text, then emits `ESC[0m` to reset.

**Bindings used:** `get_args`, `strlen`, `putchar`, `print`, `println`

### `date` — Show Date and Time

**Location:** `/bin/date.cc`

Displays the current date and time from the hardware Real-Time Clock (RTC). Supports multiple output formats.

```
> date
Friday, February 7, 2026  3:45:12 PM

> date +short
Feb 7, 2026  3:45 PM

> date +epoch
1770418512
```

**How it works:** Uses the RTC formatted string bindings (`date_full_string()`, `time_string()`, etc.) which read the hardware RTC and return pre-formatted strings. The `+epoch` mode uses `rtc_epoch()` which computes Unix timestamp from the RTC values.

**Bindings used:** `get_args`, `strcmp`, `print`, `println`, `print_int`, `date_full_string`, `date_short_string`, `time_string`, `time_short_string`, `rtc_epoch`

### `sync` — Flush Disk Cache

**Location:** `/bin/sync.cc`

Flushes all dirty blocks from the block cache to disk, ensuring data is persisted.

```
> sync
Cache flushed to disk
```

**Bindings used:** `blockcache_sync`, `print`

### `cachestats` — Show Cache Statistics

**Location:** `/bin/cachestats.cc`

Displays block cache hit/miss statistics.

```
> cachestats
```

**Bindings used:** `blockcache_stats`

### `memdump` — Hex Memory Dump

**Location:** `/bin/memdump.cc`

Displays a hex + ASCII dump of a memory region. Accepts a hex address and optional length (default 64, max 512).

```
> memdump 0x100000 32
00100000: 48 65 6C 6C 6F 20 57 6F  Hello Wo
```

**How it works:** Parses hex address and decimal length from arguments, then reads bytes using `peek_byte()` and formats them as a traditional hex dump with ASCII sidebar.

**Bindings used:** `get_args`, `print`, `print_hex`, `print_hex_byte`, `putchar`, `peek_byte`

### `memstats` — Show Memory Statistics

**Location:** `/bin/memstats.cc`

Prints heap and physical memory usage statistics.

```
> memstats
```

**Bindings used:** `memstats`

### `memleak` — Detect Memory Leaks

**Location:** `/bin/memleak.cc`

Reports heap allocations older than a specified threshold. Default is 60 seconds.

```
> memleak
> memleak 30
```

**Bindings used:** `get_args`, `detect_memory_leaks`

### `memcheck` — Check Heap Integrity

**Location:** `/bin/memcheck.cc`

Walks all heap blocks and verifies canary values to detect buffer overflows.

```
> memcheck
Checking heap integrity...
Heap integrity OK
```

**Bindings used:** `print`, `heap_check_integrity`

### `stacktrace` — Show Call Stack

**Location:** `/bin/stacktrace.cc`

Prints the current EBP frame chain with return addresses.

```
> stacktrace
```

**Bindings used:** `dump_stack_trace`

### `registers` — Dump CPU Registers

**Location:** `/bin/registers.cc`

Dumps all general-purpose CPU registers (EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP) and EFLAGS.

```
> registers
CPU Registers:
  EAX: 0x00000001  EBX: 0x00000000  ECX: 0x00000000  EDX: 0x00000000
  ESI: 0x00000000  EDI: 0x00000000  EBP: 0x001FFAA0  ESP: 0x001FFA80
  EFLAGS: 0x00000202
```

**Bindings used:** `dump_registers`

### `sysinfo` — Show System Information

**Location:** `/bin/sysinfo.cc`

Displays uptime, CPU frequency, timer frequency, and detailed memory statistics.

```
> sysinfo
System Information:
  Uptime: 42.567s
  CPU Freq: 2400 MHz
  Timer Freq: 100 Hz
  Memory: 15360 KB free / 16384 KB total
```

**Bindings used:** `uptime_ms`, `get_cpu_mhz`, `timer_get_frequency`, `pmm_free_pages`, `pmm_total_pages`, `print`, `print_int`, `putchar`, `memstats`

### `loglevel` — Get/Set Serial Log Level

**Location:** `/bin/loglevel.cc`

Shows the current serial log level, or sets it to one of: debug, info, warn, error, panic.

```
> loglevel
Current log level: INFO
Usage: loglevel <debug|info|warn|error|panic>

> loglevel debug
Log level set to DEBUG
```

**Bindings used:** `get_args`, `strcmp`, `set_log_level`, `get_log_level_name`, `print`

### `logdump` — Show Recent Log Entries

**Location:** `/bin/logdump.cc`

Prints the contents of the in-memory circular log buffer.

```
> logdump
=== Recent Log Entries ===
[INFO] VFS initialized
[INFO] System Timer Frequency: 100 Hz
...
```

**Bindings used:** `print`, `print_log_buffer`

### `crashtest` — Test Crash Handling

**Location:** `/bin/crashtest.cc`

Triggers various crash scenarios for testing the kernel's error handling.

```
> crashtest panic
> crashtest nullptr
> crashtest divzero
> crashtest overflow
> crashtest stackoverflow
```

| Type | What it does |
|------|-------------|
| `panic` | Trigger a kernel panic with a test message |
| `nullptr` | Dereference a NULL pointer (page fault) |
| `divzero` | Divide by zero (CPU exception) |
| `overflow` | Overflow a heap buffer (canary detection on free) |
| `stackoverflow` | Allocate 64 KB on stack (page fault) |

**Bindings used:** `get_args`, `strcmp`, `print`, `kernel_panic`, `crashtest_nullptr`, `crashtest_divzero`, `crashtest_overflow`, `crashtest_stackoverflow`

---

### `ed` — Line Editor

**Location:** `/bin/ed.cc`

A full POSIX-like `ed(1)` line editor, written entirely in CupidC (~900 lines). Supports 23+ commands including address parsing, regex search, substitution with backreferences, global commands, undo, marks, and file I/O through VFS.

```
> ed myfile.txt
> ed
```

| Feature | Details |
|---------|---------|
| Commands | a, i, c, d, p, n, l, =, q, Q, w, wq, W, r, e, E, f, s, m, t, j, k, u, g, v, H, h, P, +, - |
| Regex | `.` `*` `^` `$` and literals |
| Addresses | numbers, `.`, `$`, `'x` marks, `/RE/`, `?RE?`, `+/-` offsets, `%`, ranges |
| Substitution | `s/pat/repl/flags` with g, p, n, count, `&` backreference |
| Undo | Single-level (full buffer snapshot) |
| Marks | 26 (a–z) |
| Max lines | 1024 |
| Max line length | 256 chars |

**Bindings used:** `get_args`, `print`, `println`, `putchar`, `print_int`, `print_hex_byte`, `getchar`, `kmalloc`, `kfree`, `memset`, `memcpy`, `strlen`, `strcmp`, `strcpy`, `resolve_path`, `vfs_open`, `vfs_read`, `vfs_write`, `vfs_close`

See also: [Ed Editor](Ed-Editor)

---

## Tips

- **Keep files small.** Notepad has a 32 KB buffer. The CupidC compiler has a 256 KB source limit. Write compact code.
- **No verbose comments.** Every byte counts on an embedded OS. Use short comments or none.
- **Test with `cupidc`.** Run `cupidc /bin/yourprog.cc` to JIT-compile and test before deploying.
- **Use `println` for errors.** There's no stderr — just print error messages and `return`.
- **Structs are supported.** Define structs for structured data (max 32 structs, 16 fields each).
- **Programs in `/home/bin/` persist across reboots** since they're on the FAT16 disk. Programs in `/bin/` are in ramfs and rebuilt from source each boot.

---

## See Also

- [CupidC Compiler](CupidC-Compiler) — full language reference and compiler internals
- [Shell Commands](Shell-Commands) — all built-in commands
- [Filesystem](Filesystem) — VFS, ramfs, FAT16, and directory structure
- [ELF Programs](ELF-Programs) — compiling native C programs with GCC for cupid-os
