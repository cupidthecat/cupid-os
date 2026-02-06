# cupid-os
A modern, 32-bit operating system written in C and x86 Assembly that combines clean design with nostalgic aesthetics. The project implements core OS functionality while serving as both a learning platform and a foundation for experimental OS concepts. Inspired by systems like TempleOS and OsakaOS, cupid-os aims to provide a transparent, hands-on environment where users can directly interact with hardware.

## Example of cupid-os
![alt text for cupid-os img](img/basic_shell.png)

- Custom bootloader with protected mode transition
- Comprehensive interrupt handling system
- Advanced PS/2 keyboard driver with full US layout support
- High-precision programmable timer system
- VGA Mode 13h graphical desktop with draggable windows
- PS/2 mouse driver with cursor rendering
- Pastel-themed desktop environment with taskbar and icons
- Preemptive multitasking with round-robin process scheduler
- Linux-style Virtual File System with RamFS, DevFS, and FAT16 backends
- Real-time clock display in taskbar with interactive calendar popup

The goal of cupid-os is to create an accessible, well-documented operating system that serves as both a learning platform and a foundation for experimental OS concepts. Drawing inspiration from TempleOS, OsakaOS, and classic game systems, it focuses on combining technical excellence with an engaging user experience.

## Philosophy
cupid-os embraces a philosophy of complete user empowerment and transparency, inspired by TempleOS. Like TempleOS, cupid-os gives users full, unrestricted access to the entire system:

- No security boundaries or privilege levels - all code runs in ring 0
- Direct hardware access from user programs
- Full memory access with no virtual memory restrictions
- Complete visibility into and control over all system internals
- No artificial limitations or "protections" getting in the way

The goal is to create a pure, simple environment where users have complete freedom to explore, experiment, and truly understand how their computer works at the lowest level. While this approach sacrifices security and isolation, it maximizes learning potential and enables direct hardware manipulation that modern OSes restrict.

This design choice reflects our belief that users should be trusted and empowered rather than constrained. For educational and experimental purposes, having full access to bare metal is invaluable.

With that being said cupid-os also will have a mix of influence from mostly Linux and UNIX-like systems, with some visual and other inspiration coming from both templeOS, and OsakaOS. 

## Project Structure

- **boot/**  
  - `boot.asm` – Bootloader that sets up the environment, loads the kernel, and switches to protected mode.

- **kernel/**  
  - `kernel.c` – Main kernel file handling VGA initialization, timer calibration, and overall system startup.
  - `idt.c/h` – IDT setup and gate configuration.
  - `isr.asm` – Assembly routines for common ISR/IRQ stubs.
  - `irq.c/h` – IRQ dispatch and handler installation.
  - `pic.c/h` – PIC (Programmable Interrupt Controller) initialization and masking functions.
  - `math.c/h` – Math utilities including 64-bit division, itoa, and hex printing.
  - `shell.c/h` – A basic shell interface that handles user input and simple commands.
  - `string.c/h` – Basic string manipulation functions.
  - `cpu.h` – CPU utility functions (including `rdtsc` and CPU frequency detection).
  - `kernel.h` – Core kernel definitions and shared globals (e.g., VGA parameters).
  - `panic.c/h` – Enhanced kernel panic handler with register dumps, stack traces, and hex stack dumps.
  - `assert.h` – Kernel assertion macros (`ASSERT`, `ASSERT_MSG`) with file/line reporting.
  - `debug.h` – Debug utility definitions.
  - `blockdev.c/h` – Generic block device abstraction layer.
  - `blockcache.c/h` – 64-entry LRU block cache with write-back policy.
  - `fat16.c/h` – FAT16 filesystem driver with MBR partition table support (read/write).
  - `vfs.c/h` – Virtual File System core: mount table, file descriptor table, path resolution, unified API.
  - `ramfs.c/h` – In-memory filesystem with directory tree, used for root (`/`), `/bin`, `/tmp`.
  - `devfs.c/h` – Device filesystem for `/dev` (null, zero, random, serial).
  - `fat16_vfs.c/h` – FAT16 VFS wrapper, adapts existing FAT16 driver for unified VFS access at `/home`.
  - `exec.c/h` – CUPD program loader: reads executable headers, loads code/data, spawns processes.
  - `ed.c/h` – Ed line editor (Unix ed(1) clone) with regex search, substitution, and undo.
  - `paging.c` – Identity-mapped paging setup (4KB pages, 32MB).
  - `font_8x8.c/h` – 8×8 monospaced bitmap font (ASCII 0–127) for graphical text rendering.
  - `graphics.c/h` – Graphics primitives: pixel, line (Bresenham), rectangle, and text drawing with clipping.
  - `gui.c/h` – Window manager with draggable windows, z-ordering, focus, and close buttons.
  - `desktop.c/h` – Desktop shell with background, taskbar, clickable icons, and main event loop.
  - `terminal_app.c/h` – GUI terminal application interfacing with the shell via character buffer.
  - `process.c/h` – Process management: kernel threads, round-robin scheduler, context switching, process lifecycle.
  - `context_switch.asm` – Pure assembly context switch routine: saves/restores callee-saved registers and ESP across process switches.
  - `cupidscript.h` – CupidScript scripting language header: token types, AST nodes, runtime context, and public API.
  - `cupidscript_lex.c` – CupidScript lexer/tokenizer: breaks script source into tokens (keywords, variables, operators, strings).
  - `cupidscript_parse.c` – CupidScript parser: transforms token stream into an Abstract Syntax Tree (AST).
  - `cupidscript_exec.c` – CupidScript interpreter/executor: walks the AST, executes commands, evaluates tests, manages control flow.
  - `cupidscript_runtime.c` – CupidScript runtime: variable storage, function registry, and `$VAR` expansion engine.
  - `cupidscript_streams.c/h` – Stream system: file descriptor table, pipes, buffer I/O, and terminal stream wrappers.
  - `cupidscript_strings.c` – Advanced bash-like string operations: substring, prefix/suffix removal, replacement, case conversion.
  - `cupidscript_arrays.c/h` – Array and associative array support for CupidScript variables.
  - `cupidscript_jobs.c/h` – Background job management: job table, process state tracking, job control builtins.
  - `terminal_ansi.c/h` – ANSI escape sequence parser: color codes, cursor control, screen clearing for terminal color support.
  - `calendar.c/h` – Calendar math (leap year, days-in-month, Zeller's congruence) and time/date formatting for taskbar clock and calendar popup.

- **drivers/**  
  - `keyboard.c/h` – PS/2 keyboard driver with enhanced key support (arrow keys, delete, and modifiers).
  - `timer.c/h` – Timer functions including sleep/delay, tick counting, and multi-channel support.
  - `pit.c/h` – PIT configuration and frequency setup.
  - `speaker.c/h` – PC speaker driver with tone and beep functionality.
  - `serial.c/h` – COM1 serial port driver with formatted logging and circular log buffer.
  - `ata.c/h` – ATA/IDE PIO-mode disk driver.
  - `vga.c/h` – VGA Mode 13h graphics driver (320×200, 256 colors) with double buffering.
  - `mouse.c/h` – PS/2 mouse driver with IRQ12 handler and cursor rendering.
  - `rtc.c/h` – CMOS Real-Time Clock driver: reads time/date via I/O ports 0x70/0x71, BCD conversion, atomic reads, validation.

- **link.ld** – Linker script defining the kernel image layout.
- **Makefile** – Build configuration that compiles the bootloader, kernel, and drivers into a bootable image.
- **LICENSE** – GNU General Public License v3.

# Features

- **Custom Bootloader & Protected Mode Transition**  
  - Loads at `0x7C00` and sets up a simple boot message.
  - Loads the kernel from disk and switches from 16-bit real mode to 32-bit protected mode.
  - Sets up a Global Descriptor Table (GDT) for proper memory segmentation.

- **Interrupt & Exception Handling**  
  - Comprehensive Interrupt Descriptor Table (IDT) configuration.
  - Exception handlers with detailed error messages.
  - IRQ management with PIC remapping and custom handler registration.
  - A common ISR/IRQ stub that saves processor state before dispatch.

- **PS/2 Keyboard Driver**  
  - Full US keyboard layout support.
  - Interrupt-driven input processing (IRQ1) with scancode-to-ASCII conversion.
  - **Enhanced Key Handling:**  
    - Support for modifier keys (Shift, Ctrl, Alt, Caps Lock) with proper state tracking.
    - Extended key support including arrow keys and the delete key.
    - Circular buffer implementation for key event storage.
    - Configurable key repeat and debouncing via timestamping.

- **Timer & CPU Calibration**  
  - Uses the Intel 8253/8254 Programmable Interval Timer (PIT) for system timing.
  - Provides system tick counters, sleep/delay functions, and multi-channel timer callbacks.
  - **High-Precision Timing:**  
    - Calibrates using the CPU’s Time Stamp Counter (TSC) to measure the CPU frequency.
    - Exposes `get_cpu_freq()` and `get_pit_ticks_per_ms()` for accurate timing calculations.
    
- **VGA Graphics & Desktop Environment** ✨ **NEW**
  - **VGA Mode 13h Driver:** 320×200 resolution, 256-color indexed mode with programmable RGB palette
    - Pastel/soft aesthetic color palette (OsakaOS-inspired)
    - Double buffering via heap-allocated back buffer for flicker-free rendering
    - Direct linear framebuffer access at 0xA0000
  - **Graphics Primitives:** Pixel plotting, Bresenham line drawing, filled/outlined rectangles
    - 8×8 monospaced bitmap font with full ASCII coverage
    - Text rendering with automatic clipping to screen bounds
  - **PS/2 Mouse Driver:** IRQ12-driven with 3-byte packet parsing
    - 8×10 pixel arrow cursor with outline for visibility
    - Save-under buffer for non-destructive cursor rendering
    - Button state tracking (left, right, middle)
  - **Window Manager:** Up to 16 overlapping windows with z-ordering
    - Window creation, destruction, and focus management
    - Draggable title bars with drag offset tracking
    - Close buttons with hit testing
    - Focused/unfocused title bar color differentiation
    - Application-specific redraw callbacks
  - **Desktop Shell:** Complete desktop environment
    - Solid pastel background with desktop icons
    - Taskbar at bottom (20px) with "cupid-os" branding and window buttons
    - **Real-time clock display** (right-aligned) showing time in 12-hour format and short date
    - **Interactive calendar popup** — click clock to open a navigable monthly calendar
      - Shows current month with highlighted today marker
      - Left/right arrow buttons to navigate months
      - Full date and time with seconds display
      - Close by clicking outside or pressing Escape
    - Clickable desktop icons that launch applications
    - Active window highlighting in taskbar
    - Main event loop processing mouse and keyboard input
  - **GUI Terminal Application:** Shell running inside a graphical window
    - Character buffer (80×50) with scrolling support
    - Per-character foreground/background color buffer with ANSI escape sequence processing
    - 16-color VGA palette mapped to Mode 13h for colored text rendering
    - Dark background terminal rendering
    - Cursor underline display
    - Key forwarding from GUI to shell
    - Shell dual output mode (text mode / GUI buffer)

- **PC Speaker Driver**  
  - Implements basic tone and beep functionality.
  - Supports configuring PIT channel 2 to generate square waves for sound output.

- **Process Management & Scheduler** ✨ **NEW**
  - **Deferred Preemptive Multitasking:** Round-robin scheduler with 10ms time slices (100Hz PIT)
    - Up to 32 concurrent kernel threads sharing a flat 32-bit address space
    - All processes run in ring 0 (TempleOS-inspired, no security boundaries)
    - Timer IRQ0 sets a `need_reschedule` flag; actual context switch deferred to safe voluntary points
    - Safe reschedule points: desktop main loop (before HLT), `process_yield()`, idle process loop
    - Avoids stack corruption from context switching inside interrupt handlers
  - **Process Lifecycle:** Create, run, yield, kill, and exit processes
    - Idle process (PID 1) always present, never exits, runs when nothing else is ready
    - Desktop main thread registered as PID 2 via `process_register_current()`
    - GUI terminal spawned as its own process (PID 3)
    - Automatic process cleanup when entry function returns (exit trampoline)
    - Deferred stack freeing — terminated process stacks freed lazily on slot reuse
    - Stack canary (`0xDEADC0DE`) at bottom of each process stack, checked on every context switch
  - **Pure Assembly Context Switch** (`context_switch.asm`)
    - Saves all callee-saved registers (EBP, EDI, ESI, EBX) and EFLAGS onto the current stack
    - Stores resulting ESP into the old process's PCB, switches ESP to new process, jumps to new EIP
    - Resume label (`context_switch_resume`) pops saved registers and does `ret` — returns normally to caller
    - New processes start at their entry function with `process_exit_trampoline` as the return address
    - Process isolation: crashes in one process don't bring down the kernel
  - **Shell Commands:**
    - `ps` — List all processes with PID, state, and name
    - `kill <pid>` — Terminate a process by PID
    - `spawn [n]` — Create test counting processes (default 1, max 16)
    - `yield` — Voluntarily give up CPU to next process
  - **Process API:** `process_create()`, `process_exit()`, `process_yield()`, `process_kill()`, `process_list()`, `process_register_current()`

- **Memory Management Foundations**  
  - Bitmap-backed physical page frame allocator (currently targeting the first 32MB).
  - Identity-mapped paging setup with 4KB pages to keep addresses stable in ring 0.
  - Kernel heap with a bump allocator + free list for small dynamic allocations.

- **ATA Disk I/O System** ✨ **NEW**
  - **ATA/IDE Driver:** PIO mode support for reading/writing disk sectors (28-bit LBA addressing)
  - **Block Device Abstraction:** Generic interface for block storage devices
  - **Block Cache:** 64-entry LRU cache with write-back policy (32KB total)
    - Automatic periodic flush every 5 seconds
    - Manual sync via `sync` command
    - Cache statistics tracking (hit/miss rates)
  - **FAT16 Filesystem:** Full FAT16 support with MBR partition table parsing
    - File operations: open, read, close, write, delete, directory listing
    - `fat16_write_file()` – Create or overwrite files with automatic cluster allocation
    - `fat16_delete_file()` – Remove files and free associated cluster chains
    - Cluster chain management: allocation, following, and freeing
    - FAT entry writes replicated across all FAT copies
    - Root directory support (subdirectories planned)
  - **Performance:** 10-100x speedup via write-back caching

- **Virtual File System (VFS)** ✨ **NEW**
  - **Linux-style VFS layer** with unified API (`vfs_open`, `vfs_read`, `vfs_write`, `vfs_close`)
    - Mount table supporting up to 16 simultaneous mount points
    - File descriptor table with up to 64 open files
    - Path resolution via longest-prefix match against mount table
    - Current working directory with `.` and `..` navigation
  - **Three filesystem backends:**
    - **RamFS** — In-memory filesystem with full directory tree, used for root (`/`), `/bin`, `/tmp`
    - **DevFS** — Device filesystem at `/dev` with null, zero, random, serial pseudo-devices
    - **FAT16 VFS Wrapper** — Adapts existing FAT16 driver for unified access at `/home`
  - **Directory hierarchy:** `/` (ramfs root), `/bin` (programs), `/tmp` (temp), `/home` (disk), `/dev` (devices)
  - **CUPD Program Loader:** Load and execute flat binaries with 20-byte header (magic `0x43555044`)
    - Validates header, allocates memory, loads code+data sections, zeros BSS, spawns process
    - Max 256KB executables, integrated with process scheduler
  - **Notepad VFS integration:** File dialog browses VFS directories with navigation, open/save via VFS API
  - **12 new shell commands:** `cd`, `pwd`, `ls`, `cat`, `mount`, `vls`, `vcat`, `vstat`, `vmkdir`, `vrm`, `vwrite`, `exec`

- **Debugging & Memory Safety** ✨ **NEW**
  - **Serial Port Driver:** COM1 at 115200 baud (8N1), formatted output via `serial_printf()`
    - Multi-level logging system: `KDEBUG`, `KINFO`, `KWARN`, `KERROR` macros
    - In-memory circular log buffer (100 lines) viewable from shell
    - Dual output to serial port and log buffer with timestamps
  - **Enhanced Panic Handler:** Full system state dump on kernel panic
    - Register dump (all GP registers + segment registers)
    - Stack trace via EBP frame chain walking (up to 10 frames)
    - Hex stack dump (128 bytes from ESP)
    - System state (uptime, memory usage)
    - Dual output to VGA screen and serial port
  - **Kernel Assertions:** `ASSERT(cond)` and `ASSERT_MSG(cond, msg, ...)` macros
    - Triggers kernel panic with file:line on failure
    - Compiled out when `DEBUG` is not defined
  - **Heap Canaries:** Front (`0xDEADBEEF`) and back (`0xBEEFDEAD`) guard values on every allocation
    - Automatic corruption detection on `kfree()` with source location reporting
    - Free-memory poisoning (`0xFEFEFEFE`) to catch use-after-free bugs
    - Full heap walk via `memcheck` command
  - **Allocation Tracking:** Records source file, line, timestamp for up to 1024 live allocations
    - Leak detection with configurable age threshold
    - Peak allocation watermark tracking
  - **Enhanced Page Fault Handler:** Reads CR2 for faulting address
    - Detects NULL pointer dereferences (address < 0x1000)
    - Reports access type (read/write), cause (not-present/protection), and CPU mode

- **Ed Line Editor** ✨ **NEW**
  - A faithful implementation of the classic Unix `ed(1)` line editor
  - **Address Forms:** Line numbers, `.` (current), `$` (last), `+`/`-` offsets, `/RE/` and `?RE?` regex search, `'x` marks, `%` (whole file), `addr,addr` ranges
  - **Editing Commands:**
    - `a` (append), `i` (insert), `c` (change) – Enter input mode; end with `.` on a line by itself
    - `d` (delete), `j` (join), `m` (move), `t` (copy/transfer)
    - `s/pattern/replacement/flags` – Substitute with `g` (global), `p` (print), `n` (number) flags
    - `u` (undo) – Single-level undo of the last change
  - **Display Commands:** `p` (print), `n` (number), `l` (list with escapes)
  - **File Commands:**
    - `e` / `E` (edit), `r` (read), `w` (write), `wq` (write & quit)
    - `f` (filename) – Get/set current filename
    - Reads from both in-memory filesystem and FAT16 disk
    - Writes to FAT16 disk (`w`, `wq`, `W` commands persist data via ATA driver)
  - **Search:** `/pattern/` forward, `?pattern?` backward with basic regex (`. * ^ $`)
  - **Global Commands:** `g/RE/cmd` and `v/RE/cmd` (inverse global)
  - **Other:** `=` (print line count), `k` (mark), `H`/`h` (error messages), `q`/`Q` (quit)
  - **Limits:** 1024 lines max, 256 chars per line
  - **Regex:** Supports `.` (any char), `*` (zero or more), `^` (start), `$` (end), literal chars

- **CupidScript Scripting Language** ✨ **ENHANCED**
  - A bash-like scripting language for cupid-os with `.cup` file extension
  - **Three invocation methods:**
    - `cupid script.cup [args]` — Run via the `cupid` shell command
    - `./script.cup [args]` — Run with `./` prefix
    - `script.cup [args]` — Run by typing the filename directly
  - **Variables:**
    - Assignment: `NAME=frank`, `COUNT=42`
    - Expansion: `$NAME`, `$COUNT` — expands inside strings and arguments
    - Special variables: `$?` (last exit status), `$#` (argument count), `$0` (script name), `$1`–`$9` (positional args), `$!` (last background PID)
    - Undefined variables expand to empty string (bash behavior)
  - **Conditionals:**
    - `if [ $X -eq 10 ]; then ... fi`
    - `if [ $NAME = "frank" ]; then ... else ... fi`
    - Test operators: `-eq`, `-ne`, `-lt`, `-gt`, `-le`, `-ge` (numeric); `=`, `!=` (string); `-z`, `-n` (length)
  - **Loops:**
    - `for i in 1 2 3 4 5; do ... done` — iterate over word list
    - `while [ $COUNT -lt 10 ]; do ... done` — conditional loop
    - Safety limit: 10,000 iterations per while loop
  - **Functions:**
    - Definition: `greet() { echo "Hello $1"; }`
    - Call: `greet frank` — arguments accessible via `$1`, `$2`, etc.
    - `return N` — set exit status from a function
  - **Arithmetic:** `$((expr))` expansion with `+`, `-`, `*`, `/`, `%`
  - **Comments:** `# comment` — full line and inline
  - **Shebang:** `#!/bin/cupid` supported (skipped during execution)
  - **Terminal Color Support:** ✨ **NEW**
    - ANSI escape sequence parser: `\e[30-37m`, `\e[40-47m`, `\e[90-97m`, `\e[0m`, `\e[1m`, `\e[2J`, `\e[H`
    - Per-character foreground and background colors in GUI terminal
    - Built-in color functions: `setcolor`, `resetcolor`, `printc`
    - `echo -c <color> <text>` — colored echo shorthand
    - 16-color VGA palette with Mode 13h mapping
  - **I/O Redirection & Pipes:** ✨ **NEW**
    - Pipe operator: `cmd1 | cmd2` — connect stdout to stdin
    - Output redirection: `cmd > file`, `cmd >> file` (append)
    - Input redirection: `cmd < file`
    - Error redirection: `cmd 2> file`, `cmd 2>&1`
    - File descriptor table with up to 16 open descriptors per context
    - Stream types: terminal, buffer (pipes), and VFS file
  - **Command Substitution:** ✨ **NEW**
    - Modern syntax: `result=$(command)`
    - Backtick syntax: `` result=`command` ``
    - Nested substitution support
  - **Background Jobs:** ✨ **NEW**
    - Background execution: `command &`
    - Job table with up to 32 tracked jobs
    - `jobs` / `jobs -l` — list background jobs
    - `$!` — PID of last background process
  - **Arrays:** ✨ **NEW**
    - Regular arrays: up to 256 elements, 32 arrays
    - Associative arrays: `declare -A name`, key-value pairs
    - Array access: `${arr[0]}`, `${arr[@]}`, `${#arr[@]}`
  - **Advanced String Operations:** ✨ **NEW**
    - Length: `${#var}`
    - Substring: `${var:start:len}`
    - Suffix removal: `${var%pattern}`, `${var%%pattern}`
    - Prefix removal: `${var#pattern}`, `${var##pattern}`
    - Replacement: `${var/pattern/replacement}`
    - Case conversion: `${var^^}` (upper), `${var,,}` (lower), `${var^}` (capitalize)
  - **Architecture:** Lexer → Parser → AST → Interpreter pipeline
    - All allocations use `kmalloc`/`kfree` with full cleanup
    - Works in both text mode and GUI terminal
    - Scripts can call any built-in shell command
  - **Limits:** 128 variables, 32 functions, 256-char values, 2048 tokens, 32 arrays, 16 assoc arrays
  - Create scripts with the `ed` editor, save to FAT16 disk, and execute

- **Shell Interface**
  - **Command-line shell** with prompt showing CWD, parsing, history, and tab completion
  - **VFS Commands:** `cd <path>`, `pwd`, `ls [path]`, `cat <file>`, `mount` ✨ **NEW**
  - **Advanced VFS Commands:** `vls`, `vcat`, `vstat`, `vmkdir`, `vrm`, `vwrite` ✨ **NEW**
  - **Program Execution:** `exec <path>` – Load and run CUPD binaries from VFS ✨ **NEW**
  - **Legacy Disk Commands:** `lsdisk`, `catdisk <file>`, `sync`, `cachestats`
  - **Editor:** `ed [file]` – Launch the ed line editor
  - **Scripting:** `cupid <script.cup> [args]` – Run a CupidScript file ✨ **NEW**
  - **Process Commands:**
    - `ps` – List all running processes (PID, state, name)
    - `kill <pid>` – Terminate a process
    - `spawn [n]` – Spawn test processes
    - `yield` – Voluntarily yield CPU
  - **System Commands:** `help`, `clear`, `echo`, `time`, `reboot`, `history`
  - **Debug Commands:** ✨ **NEW**
    - `memdump <hex_addr> [length]` – Hex + ASCII dump of memory region (default 64 bytes, max 512)
    - `memstats` – Show heap and physical memory statistics
    - `memleak [seconds]` – Detect allocations older than threshold (default 60s)
    - `memcheck` – Walk all heap blocks and verify canary integrity
    - `stacktrace` – Print current call stack (EBP chain)
    - `registers` – Dump all general-purpose CPU registers + EFLAGS
    - `sysinfo` – Show uptime, CPU frequency, timer frequency, and memory usage
    - `loglevel [debug|info|warn|error|panic]` – Get/set serial log verbosity
    - `logdump` – Print the in-memory circular log buffer
    - `crashtest <type>` – Test crash handling (`panic`, `nullptr`, `divzero`, `assert`, `overflow`, `stackoverflow`)
  - Command history navigation (arrow up/down) and tab completion for command/file names

- **Utility Libraries**
  - **Math Library:** 64-bit division, integer-to-string conversion, hexadecimal printing
  - **String Library:** strlen, strcmp, strncmp, memcpy, memset
  - **In-memory Filesystem:** Legacy read-only file table for system files (LICENSE.txt, MOTD.txt), now pre-populated into VFS at boot

## Development Roadmap
The development roadmap outlined below represents our current plans and priorities. However, it's important to note that this roadmap is flexible and will evolve based on:

- New requirements discovered during development
- Technical challenges and learning opportunities encountered
- Community feedback and contributions
- Integration needs between different system components
- Performance optimization requirements
- Hardware support requirements
- Testing and debugging needs

As we progress, new phases and tasks may be added, existing ones may be modified, and priorities may shift to ensure we're building the most robust and useful system possible.

### Phase 1 - Core System Infrastructure
1. **Interrupt Handling** (✅ Complete)
   - ✅ Implement IDT (Interrupt Descriptor Table)
   - ✅ Set up basic exception handlers
   - ✅ Handle hardware interrupts
   - ✅ Implement PIC configuration
   - ✅ Add detailed error messages for exceptions
   - ✅ Support for custom interrupt handlers
   - ⭕ Basic boot sequence logging
2. **Keyboard Input** (✅ Complete)
   - ✅ Implement PS/2 keyboard driver
   - ✅ Basic input buffer
   - ✅ Scancode handling
   - ✅ Input event processing
   - ✅ Keyboard state management
   - ✅ Modifier key support (Shift, Caps Lock)
   - ✅ Additional modifier keys (Ctrl, Alt)
   - ✅ Key repeat handling
   - ✅ Function keys support
   - ✅ Extended key support
   - ✅ Key debouncing
   - ✅ Circular buffer implementation
   - ⭕ Arrow keys

3. **Timer Support** (🔄 In Progress)
   - ✅ PIT (Programmable Interval Timer) implementation
   - ✅ Basic system clock
   - ✅ Timer interrupts
   - ✅ System tick counter
   - ✅ Sleep/delay functions
   - ✅ Timer calibration
   - ✅ Multiple timer channels
   - ✅ Variable frequency support
   - X PC Speaker support [WILL BE SUPPORTED LATER]
   - 🔄 High-precision timing modes

4. **Memory Management** (🔄 In Progress)
   - ✅ Physical memory manager
   - ✅ Simple memory allocation/deallocation
   - ✅ Basic paging setup
   - ✅ Heap canaries (front/back guard values)
   - ✅ Free-memory poisoning (use-after-free detection)
   - ✅ Allocation tracking with source location
   - ✅ Heap integrity checking
   - ✅ Memory leak detection
   - ✅ Heap management
   - ✅ Memory statistics tracking
   - ⭕ Memory protection
   - ⭕ Memory mapping
   - ⭕ Virtual memory support

### Phase 2 - Extended Features
5. **Shell Interface** (🔄 In-Progress)
   - ✅ Basic command parsing and prompt display with CWD
   - ✅ 38 built-in commands (system, filesystem, VFS, disk, debug, editor, scripting, process)
   - ✅ Advanced command parsing with argument splitting
   - ✅ Command history (16 entries, arrow key navigation)
   - ✅ Tab completion (commands and filenames)
   - ✅ Debug/memory introspection commands
   - ✅ CupidScript scripting language (`.cup` files with variables, loops, functions)
   - ✅ VFS navigation commands (cd, pwd, ls, cat, mount, vls, vcat, vstat, vmkdir, vrm, vwrite, exec)
   - ✅ I/O redirection (pipes, `>`, `>>`, `<`, `2>`, `2>&1`)
   - ✅ Terminal color support (ANSI escape codes + built-in color functions)
   - ✅ Command substitution (`$()` and backticks)
   - ✅ Background jobs (`&`, `jobs`, `$!`)
   - ✅ Arrays and associative arrays
   - ✅ Advanced string operations (`${var:0:5}`, `${var%pattern}`, `${var^^}`, etc.)

7. **Process Management** (✅ Complete)
   - ✅ Process creation/termination
   - ✅ Basic scheduling (round-robin, 10ms time slices)
   - ✅ Process states (READY, RUNNING, BLOCKED, TERMINATED)
   - ✅ Pure assembly context switching (`context_switch.asm`)
   - ✅ Deferred preemptive scheduling (flag-based, safe reschedule points)
   - ✅ Idle process (PID 1, always present)
   - ✅ Desktop thread registered as PID 2
   - ✅ Terminal spawned as its own process (PID 3)
   - ✅ Stack canary overflow detection
   - ✅ Deferred stack freeing (lazy reaping of terminated processes)
   - ✅ Shell commands: ps, kill, spawn, yield
   - ⭕ Process blocking/sleeping
   - ⭕ Inter-process communication

8. **Basic Device Drivers** (🔄 In Progress)
   - ✅ PS/2 Keyboard
   - ✅ ATA/IDE disk driver (PIO mode)
   - ✅ Serial port (COM1, 115200 baud)
   - ✅ VGA Mode 13h graphics (320×200, 256 colors)
   - ✅ PS/2 mouse driver (IRQ12, cursor rendering)
   - ✅ Real-time clock (CMOS RTC driver with taskbar clock and calendar popup)

9. **Simple Filesystem** (✅ Complete)
   - ✅ VFS layer with mount table, path resolution, unified API
   - ✅ RamFS (in-memory directory tree for root, /bin, /tmp)
   - ✅ DevFS (device files: /dev/null, /dev/zero, /dev/random, /dev/serial)
   - ✅ FAT16 VFS wrapper (persistent disk files at /home)
   - ✅ CUPD program loader (flat binary format with header)
   - ✅ Directory navigation (cd, pwd, ls, cat with CWD support)
   - ✅ Notepad file dialog uses VFS for browsing, opening, and saving
   - ⭕ File permissions
   - ⭕ Per-process file descriptors

6. **Text Editor** (✅ Complete)
   - ✅ Ed line editor (Unix ed(1) clone)
   - ✅ Address parsing (line numbers, `.`, `$`, regex, marks)
   - ✅ Input mode (append, insert, change)
   - ✅ Editing commands (delete, join, move, copy, substitute)
   - ✅ Regex search (forward/backward with basic regex)
   - ✅ Global/inverse-global commands
   - ✅ Single-level undo
   - ✅ File I/O (in-memory fs + FAT16)

### Phase 3 - Advanced Features
10. Custom compiler
11. Advanced memory management
12. Extended device support
13. ~~Multi-process scheduling~~ ✅ Basic round-robin scheduling implemented
14. Custom music program in dedication to Terry A. Davis

## Requirements
- NASM (Netwide Assembler) for bootloader compilation
- GCC (32-bit support required)
- GNU Make
- QEMU for testing (qemu-system-i386)
- dosfstools (for creating FAT16 test disks)
- Linux environment (or equivalent Unix-like system)

## Building
1. Install dependencies (Ubuntu/Debian):
```bash
sudo apt-get install nasm gcc make qemu-system-x86 dosfstools
```

2. Build the OS:
```bash
make
```

3. Run in QEMU (serial output to terminal):
```bash
make run
```

4. Run with a FAT16 hard disk attached:
```bash
make run-disk
```

5. Run with serial output logged to file:
```bash
make run-log
# Serial output saved to debug.log
```

### Creating a FAT16 Test Disk

The `lsdisk` and `catdisk` commands require a FAT16-formatted disk image with an MBR partition table. A raw `mkfs.fat` image **will not work** — the FAT16 driver expects an MBR with a valid partition entry.

```bash
# 1. Create a 10MB blank disk image
dd if=/dev/zero of=test-disk.img bs=1M count=10

# 2. Create an MBR partition table with a single FAT16 partition (type 0x06)
#    This creates one primary partition using all available space
echo -e 'o\nn\np\n1\n\n\nt\n6\nw' | fdisk test-disk.img

# 3. Set up a loop device for the partition (starts at sector 2048)
sudo losetup -o $((2048*512)) --sizelimit $((18432*512)) /dev/loop0 test-disk.img

# 4. Format the partition as FAT16
sudo mkfs.fat -F 16 -n "CUPIDOS" /dev/loop0

# 5. Mount and add test files
sudo mkdir -p /tmp/testdisk
sudo mount /dev/loop0 /tmp/testdisk
echo "Hello from CupidOS disk!" | sudo tee /tmp/testdisk/README.TXT
echo "This is a test file"      | sudo tee /tmp/testdisk/TEST.TXT

# 6. Clean up
sudo umount /tmp/testdisk
sudo losetup -d /dev/loop0
rmdir /tmp/testdisk
```

After this, `make run-disk` will boot CupidOS with the disk attached. Use `lsdisk` to list files and `catdisk <filename>` to read them.

## Project Structure Details
### Bootloader (`boot/boot.asm`)
- Loads at 0x7C00 (BIOS loading point)
- Sets up initial environment
- Loads kernel from disk
- Switches to protected mode
- Jumps to kernel at 0x1000

### Kernel Components
#### Main Kernel (`kernel/kernel.c`)
- Entry point at 0x1000
- Implements basic screen I/O
- VGA text mode driver (initial boot) → switches to Mode 13h graphics
- System initialization
- IDT initialization
- Boots into graphical desktop environment

#### Interrupt System (`kernel/idt.c`, `kernel/isr.asm`)
- Complete IDT setup and management
- Exception handlers with detailed error messages
- Hardware interrupt support (IRQ0-15)
- Programmable Interrupt Controller (PIC) configuration
- Custom interrupt handler registration
- Debug exception handling

#### Input System (`drivers/keyboard.c`, `drivers/keyboard.h`)
- PS/2 keyboard driver with:
  - Full US keyboard layout support
  - Shift and Caps Lock modifiers
  - Key state tracking
  - Interrupt-driven input handling (IRQ1)
  - Debouncing support
  - Extended key support (e.g. right ctrl/alt)
  - Circular buffer for key events
  - Support for special keys (backspace, tab, enter)

#### VGA Graphics Driver (`drivers/vga.c`, `drivers/vga.h`)
- VGA Mode 13h: 320×200 resolution, 256-color indexed mode
- Programmable RGB palette with pastel/soft aesthetic (16 UI colors)
- Double buffering via heap-allocated 64KB back buffer
- Mode switching from text mode via VGA register programming
- Direct linear framebuffer access at physical address 0xA0000

#### PS/2 Mouse Driver (`drivers/mouse.c`, `drivers/mouse.h`)
- IRQ12-driven PS/2 auxiliary device with 3-byte packet protocol
- Sign-extended delta movement with overflow protection
- Cursor position clamped to screen bounds (0-319, 0-199)
- 8×10 pixel arrow cursor bitmap with outline for visibility
- Save-under buffer for non-destructive cursor compositing
- Button state tracking (left, right, middle) with press/release detection

#### Graphics Primitives (`kernel/graphics.c`, `kernel/graphics.h`)
- Pixel plotting with automatic bounds clipping
- Bresenham's line algorithm for arbitrary-angle lines
- Fast horizontal/vertical line drawing via `memset`
- Filled and outlined rectangles
- 8×8 bitmap font text rendering (ASCII 0-127)
- Text width measurement for layout calculations

#### Window Manager (`kernel/gui.c`, `kernel/gui.h`)
- Up to 16 overlapping windows with z-ordered rendering
- Window creation/destruction with unique IDs
- Focus management: clicking raises window to top
- Title bar dragging with drag offset tracking and screen clamping
- Close button hit detection with "X" rendering
- Focused vs unfocused visual differentiation (cyan vs gray title bars)
- Application-specific redraw callbacks for content rendering
- Dirty flag system for efficient partial redraws

#### Desktop Shell (`kernel/desktop.c`, `kernel/desktop.h`)
- Solid pastel background fill (light pink)
- Taskbar (bottom 20px): "cupid-os" branding + window buttons
- Active window highlighting in taskbar
- Up to 16 clickable desktop icons with labels
- Main event loop: mouse → keyboard → redraw → HLT cycle
- Icon click launches applications (e.g., Terminal)
- Taskbar click focuses corresponding window

#### GUI Terminal (`kernel/terminal_app.c`, `kernel/terminal_app.h`)
- Shell running inside a graphical window (280×160 default)
- 80×50 character buffer with scrolling support
- Per-character color buffer: each cell stores foreground and background VGA color index
- ANSI escape sequence processing: color codes parsed and stripped from output, colors applied per-character
- 16-color VGA palette mapped to Mode 13h palette indices for graphical rendering
- Dark background (COLOR_TERM_BG) with colored text (default: light gray)
- Cursor underline rendering at current shell position
- Key forwarding from desktop event loop to shell
- Shell dual output mode: text mode (VGA) or GUI buffer

#### Process Scheduler (`kernel/process.c`, `kernel/process.h`, `kernel/context_switch.asm`)
- **Process Control Block (PCB):** PID, state, saved CPU context (ESP/EIP + callee-saved regs), stack info, name
- **Process Table:** Static array of 32 PCBs indexed by (PID − 1)
- **Round-Robin Scheduler:** Equal 10ms time slices, triggered by PIT IRQ0
  - Deferred preemptive: timer interrupt sets a `need_reschedule` flag (never calls `schedule()` from ISR)
  - Context switch happens at safe voluntary points: desktop main loop, `process_yield()`, idle process
  - Cooperative: `process_yield()` voluntarily gives up CPU
- **Pure Assembly Context Switch** (`context_switch.asm`):
  - `context_switch(old_esp, new_esp, new_eip)` — saves EBP, EDI, ESI, EBX, EFLAGS onto current stack
  - Stores resulting ESP into `*old_esp`, switches ESP to `new_esp`, jumps to `new_eip`
  - `context_switch_resume` — pops saved registers and does `ret`, returning normally to caller
  - New processes start at their entry function; `process_exit_trampoline` is the return address on the stack
- **Idle Process (PID 1):** Always present, runs `STI; HLT` loop, checks reschedule flag each iteration
- **Desktop Thread (PID 2):** Kernel main thread registered via `process_register_current("desktop")`
- **Terminal Process (PID 3):** GUI terminal spawned as its own process via `process_create()`
- **Stack Management:**
  - Canary value (`0xDEADC0DE`) at stack bottom, checked on every context switch
  - Deferred stack freeing: terminated process stacks freed lazily when slot is reused
- **Process API:**
  - `process_create(entry, name, stack_size)` — Spawn new kernel thread
  - `process_exit()` — Terminate current process, defer stack free, reschedule
  - `process_yield()` — Voluntary CPU relinquish (clears reschedule flag first)
  - `process_kill(pid)` — Terminate any process by PID
  - `process_list()` — Print all processes (used by `ps` command)
  - `process_register_current(name)` — Register an already-running thread in the process table

#### Ed Line Editor (`kernel/ed.c`, `kernel/ed.h`)
A faithful implementation of the classic Unix `ed(1)` line editor, operating entirely in-memory:
- **Buffer management:** Up to 1024 lines × 256 chars, dynamically allocated via `kmalloc`/`kfree`
- **Address parsing:** Numeric, `.`, `$`, `+`/`-` offsets, `/RE/` forward search, `?RE?` backward search, `'x` marks
- **Input mode:** Append (`a`), insert (`i`), change (`c`) — terminated by `.` on its own line
- **Editing:** Delete (`d`), join (`j`), move (`m`), transfer/copy (`t`), substitute (`s/pat/repl/flags`)
- **Display:** Print (`p`), numbered print (`n`), escaped list (`l`)
- **File I/O:** Edit (`e`/`E`), read (`r`), write (`w`/`wq`), filename (`f`) — reads from in-memory fs and FAT16
- **Global:** `g/RE/cmd` executes command on matching lines; `v/RE/cmd` on non-matching lines
- **Regex engine:** Basic regex supporting `.` (any), `*` (repeat), `^` (anchor start), `$` (anchor end)
- **Undo:** Single-level full-buffer undo (`u`)
- **Marks:** 26 named marks (`ka` through `kz`, accessed via `'a` through `'z`)

**Usage:**
```
> ed                  # Start with empty buffer
> ed LICENSE.txt      # Open file from in-memory filesystem
> ed README.TXT       # Open file from FAT16 disk
```

**Example session:**
```
> ed
a                     # Append mode
Hello, world!
This is cupid-os.
.                     # End input
1,2p                  # Print lines 1–2
Hello, world!
This is cupid-os.
s/world/CupidOS/     # Substitute
1p                    # Print line 1
Hello, CupidOS!
w test.txt            # Write (reports byte count)
30
q                     # Quit
```

### Memory Layout
- Bootloader: 0x7C00
- Kernel: 0x1000
- Stack: 0x90000
- IDT: Dynamically allocated

## Development
To modify or extend the OS:

1. Bootloader changes:
   - Edit `boot/boot.asm`
   - Modify GDT if adding memory segments
   - Update kernel loading if kernel size changes

2. Kernel changes:
   - Edit `kernel/kernel.c`
   - Update `kernel/link.ld` if changing memory layout
   - Modify Makefile if adding new source files

## Debugging

### Serial Console
All `make run` and `make run-disk` targets include `-serial stdio`, so kernel log messages appear in your terminal automatically. Use `make run-log` to capture serial output to `debug.log` instead.

From the shell, use these commands for runtime debugging:
```
memstats          # Show heap + PMM statistics
memcheck          # Verify all heap canaries are intact
memleak 10        # Find allocations older than 10 seconds
memdump 0xB8000 64  # Hex dump 64 bytes of VGA memory
stacktrace        # Print current call stack
registers         # Dump CPU registers
sysinfo           # Uptime, CPU freq, memory overview
loglevel debug    # Set serial log verbosity (debug/info/warn/error/panic)
logdump           # Print in-memory log buffer
crashtestpanic   # Test panic handler (also: nullptr, divzero, assert, overflow, stackoverflow)
```

### Crash Testing
The `crashtest` command deliberately triggers various faults to verify the panic handler:
- `crashtest panic` – Kernel panic with message
- `crashtest nullptr` – NULL pointer dereference (page fault)
- `crashtest divzero` – Division by zero exception
- `crashtest assert` – Assertion failure
- `crashtest overflow` – Heap buffer overflow (detected by canary on free)
- `crashtest stackoverflow` – 64KB stack allocation (page fault)

Each crash produces a full register dump, stack trace, and hex stack dump on both the VGA screen and serial output.

### QEMU Monitor
```bash
make run
# Press Ctrl+Alt+2 for QEMU monitor
```

### GDB
```bash
# Terminal 1
qemu-system-i386 -s -S -boot a -fda cupidos.img

# Terminal 2
gdb
(gdb) target remote localhost:1234
```

## Contributing
1. Fork the repository
2. Create your feature branch
3. Commit your changes
4. Push to the branch
5. Create a Pull Request

## License
GNU v3

## Recent Updates
- **RTC Clock & Calendar** ✨ **NEW** – CMOS Real-Time Clock driver reading time/date via I/O ports 0x70/0x71 with BCD conversion and atomic reads. Digital clock displayed right-aligned in the taskbar (12-hour format with AM/PM and abbreviated date). Click the clock to open an interactive calendar popup showing a navigable monthly view with current day highlighting, month navigation arrows, and full date/time display. Close by clicking outside or pressing Escape. New files: `drivers/rtc.c/h`, `kernel/calendar.c/h`.
- **Terminal Colors & ANSI Support** – Full ANSI escape sequence parser with 16-color VGA palette. Per-character foreground and background color tracking in the GUI terminal's 80×50 character buffer. Supports `\e[30-37m` / `\e[90-97m` foreground, `\e[40-47m` background, `\e[0m` reset, `\e[1m` bold, `\e[2J` clear, and `\e[H` cursor home. Colors mapped from VGA text-mode indices to Mode 13h palette for graphical rendering. New files: `terminal_ansi.c/h`.
- **CupidScript I/O Redirection & Pipes** – Stream system with file descriptor table (16 fds per context), supporting terminal, buffer, and VFS file streams. Pipe operator (`|`) connects command stdout to next command's stdin. Output redirection (`>`, `>>`) and input redirection (`<`). Error redirection (`2>`, `2>&1`). New files: `cupidscript_streams.c/h`.
- **CupidScript Color Builtins** – Built-in commands for terminal color output: `setcolor <fg> [bg]` sets persistent color, `resetcolor` returns to defaults, `printc <fg> <text>` prints colored text, and `echo -c <color> <text>` for colored echo. All emit ANSI escape codes internally.
- **Command Substitution** – Both `$(command)` and `` `command` `` syntax for capturing command output into variables. Supports nesting.
- **Background Jobs** – `command &` runs in background with job table tracking (up to 32 jobs). `jobs` / `jobs -l` lists active jobs. `$!` expands to PID of last background process.
- **Arrays & Associative Arrays** – Regular arrays (up to 256 elements, 32 arrays) with create, set, get, append operations. Associative arrays via `declare -A name` with key-value storage (128 entries, 16 arrays). New files: `cupidscript_arrays.c/h`.
- **Advanced String Operations** – Bash-style string manipulation in variable expansion: `${#var}` (length), `${var:start:len}` (substring), `${var%pattern}` / `${var%%pattern}` (suffix removal), `${var#pattern}` / `${var##pattern}` (prefix removal), `${var/old/new}` (replacement), `${var^^}` (uppercase), `${var,,}` (lowercase), `${var^}` (capitalize). New file: `cupidscript_strings.c`.
- **Job Control** – Background job tracking integrated with process scheduler. `process_get_state()` API added for querying process state by PID. New files: `cupidscript_jobs.c/h`.
- **Virtual File System (VFS)** – Linux-style VFS layer providing a unified file API across multiple filesystem backends. Three filesystem drivers: RamFS (in-memory directory tree for `/`, `/bin`, `/tmp`), DevFS (pseudo-devices at `/dev/null`, `/dev/zero`, `/dev/random`, `/dev/serial`), and a FAT16 VFS wrapper (persistent user files at `/home`). Hierarchical directory structure with mount points, path resolution via longest-prefix match, file descriptor table (64 fds), and mount table (16 mounts). CUPD program loader reads flat binaries with a 20-byte header (magic `0x43555044`), loads code+data sections, zeros BSS, and spawns processes. Shell now has current working directory tracking (prompt shows CWD), with 14 new VFS commands: `cd`, `pwd`, `ls`, `cat`, `mount`, `vls`, `vcat`, `vstat`, `vmkdir`, `vrm`, `vwrite`, `exec`. Notepad file dialog switched from direct FAT16 calls to VFS API with directory navigation and double-click support. Total shell commands: 38.
- **CupidScript Scripting Language** – Bash-like scripting for cupid-os with variables (`$VAR`, `$?`, `$#`, `$0`–`$9`), if/else conditionals with test operators (`-eq`, `-ne`, `-lt`, `-gt`, `=`, `!=`), for/while loops, user-defined functions with positional arguments and return values, arithmetic expansion (`$((expr))`), and comments. Scripts use `.cup` extension, are created with the `ed` editor, and can be run via `cupid script.cup`, `./script.cup`, or just `script.cup`. Full lexer → parser → AST → interpreter pipeline with proper memory management. Integrates with all existing shell commands and works in both text and GUI terminal modes.
- **Process Management & Scheduler** – Deferred preemptive multitasking with round-robin scheduling (10ms time slices at 100Hz). Up to 32 kernel threads with pure assembly context switching (`context_switch.asm`) that saves/restores callee-saved registers (EBP, EDI, ESI, EBX, EFLAGS). Timer IRQ sets a reschedule flag checked at safe voluntary points (desktop loop, yield, idle) to avoid stack corruption from ISR-level switching. Desktop runs as PID 2, GUI terminal as PID 3. Deferred stack freeing with lazy reaping, stack canary overflow detection, and 4 shell commands (`ps`, `kill`, `spawn`, `yield`).
- **VGA Graphics & Desktop Environment** – Full graphical desktop with VGA Mode 13h (320×200, 256 colors). Pastel color palette, PS/2 mouse driver with cursor, window manager supporting up to 16 draggable overlapping windows with z-ordering, desktop shell with taskbar and clickable icons, and a GUI terminal application running the existing shell in a graphical window. Double-buffered rendering for flicker-free display.
- **FAT16 Write Support** – Files can now be created, overwritten, and deleted on FAT16 disk. Cluster allocation/freeing, FAT chain management, and directory entry creation are fully implemented. Ed's `w`/`wq`/`W` commands persist data through the full ATA write path.
- **Ed Line Editor** – Full Unix ed(1) clone with address parsing, regex search/substitute, input mode, global commands, marks, single-level undo, and file I/O from both in-memory and FAT16 filesystems. Writes to FAT16 disk via ATA driver.
- **Debugging & Memory Safety System** – Serial port driver (COM1, 115200 baud), enhanced panic handler with register/stack dumps, kernel assertions, heap canaries with corruption detection, allocation tracking with leak detection, free-memory poisoning, and 10 new debug shell commands
- **ATA Disk I/O** – PIO-mode ATA driver, block device layer, 64-entry LRU write-back cache, FAT16 filesystem with MBR support (read & write)
- **Shell Enhancements** – 38 commands total, command history with arrow key navigation, tab completion for commands and filenames
- Implemented comprehensive keyboard driver with full modifier key support
- Added function key handling (F1-F12)
- Implemented key repeat functionality with configurable delays
- Added debouncing support for more reliable key input
- Enhanced exception handling with detailed error messages
- Implemented basic PIT timer with system tick counter
- Added initial delay/sleep functionality using timer ticks
