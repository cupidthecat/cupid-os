# cupid-os

32-bit x86 hobby OS written in C and NASM. Has a graphical desktop, window manager, built-in C compiler, assembler, and scripting language. Runs on real hardware or QEMU. Inspired by TempleOS, OsakaOS, and Unix.

![Desktop](img/background.png)
![File manager](img/fm.png)
![Paint](img/paint.png)

## What it has

- VBE 640x480 32bpp graphics with a window manager, taskbar, and desktop icons
- CupidC, a HolyC-inspired C compiler with JIT and ELF32 AOT output
- CupidASM, an Intel-syntax x86-32 assembler with JIT and ELF32 AOT output
- CupidScript, a shell scripting language with pipes, redirects, and job control
- 40+ shell commands with history, tab completion, pipes, and redirects
- VFS with RamFS (root), DevFS (/dev), and FAT16 (/home)
- Preemptive round-robin scheduler, up to 32 kernel threads
- Two-stage bootloader that loads the kernel above 1MB via unreal mode
- GUI apps: Notepad, Terminal with ANSI colors, Paint, Calendar, File Manager
- 64-entry LRU disk block cache with write-back policy
- 6 GUI themes including a Windows 95 style and a dark mode
- PS/2 keyboard and mouse, ATA/IDE disk, RTC, serial, PC speaker drivers
- System clipboard, x86-32 disassembler, BMP image codec

## Philosophy

All code runs in ring 0. No privilege separation, no virtual memory isolation, no artificial restrictions. User programs have direct hardware access, full memory access, and can call any kernel function. The point is transparency and learning, not security. If you want to understand what your computer is actually doing, this is the kind of environment that lets you do that.

The design borrows from TempleOS (single address space, built-in compiler, bare metal), Unix (VFS, shell, process model), and OsakaOS (aesthetics).

## Building

Requires NASM, GCC with 32-bit support, GNU Make, QEMU, and mtools.

```bash
sudo apt-get install nasm gcc gcc-multilib make qemu-system-x86 mtools
```

```bash
make          # builds cupidos.img
make run      # boots in QEMU
make run-log  # boots and writes serial output to debug.log
```

Default image size is 200MB. To change it:

```bash
make HDD_MB=100
```

### Make targets

| Target | What it does |
|--------|-------------|
| `make` | Build the full disk image |
| `make run` | Boot in QEMU with SDL graphics |
| `make run-log` | Boot in QEMU, write serial to debug.log |
| `make sync-demos` | Copy demos/*.asm into the FAT16 partition |
| `make clean` | Remove object files, keep cupidos.img |
| `make clean-image` | Remove cupidos.img only |
| `make distclean` | Remove everything including cupidos.img |

### Copying files into /home

The FAT16 partition sits at byte offset 2097152 (4096 * 512) inside cupidos.img. Use mtools to put files there:

```bash
mcopy -o -i cupidos.img@@2097152 myfile.txt ::/myfile.txt
mdir  -i cupidos.img@@2097152 ::/
```

If you change FAT_START_LBA in the Makefile, recalculate: offset = FAT_START_LBA * 512.

### Debugging

GDB remote debug:
```bash
qemu-system-i386 -s -S -boot c -hda cupidos.img &
gdb
(gdb) target remote localhost:1234
(gdb) break *0x100000
(gdb) continue
```

QEMU monitor is at Ctrl+Alt+2. Serial output comes through stdout on `make run`.

---

## Project layout

```
cupid-os/
  boot/           two-stage BIOS bootloader
  kernel/         kernel source (58 C files, ~47k lines)
  drivers/        hardware drivers (9 C files)
  bin/            built-in CupidC programs (65 .cc files)
  demos/          CupidASM demo programs (19 .asm files)
  user/           example ELF user programs and cupid.h header
  wiki/           documentation (17 Markdown files)
  docs/plans/     design docs
  cupidos-txt/    embedded rich-text docs (.CTXT format)
  img/            screenshots
  link.ld         linker script
  Makefile
```

---

## Bootloader (boot/boot.asm)

Two stages, about 4KB total.

Stage 1 lives in the MBR at 0x7C00. It loads stage 2 (4 sectors from LBA 1) to 0x7E00 using INT 0x13 EDD, then jumps there.

Stage 2 does the real work:
- Enables the A20 gate
- Switches to unreal mode so it can write above 1MB while still in 16-bit real mode
- Probes VBE and sets mode 0x118 (640x480x32bpp linear framebuffer)
- Loads the kernel in 127-sector chunks from LBA 5 to physical address 0x100000
- Sets up 4KB page tables, identity-mapping 0 to 32MB
- Loads the GDT, enables protected mode, jumps to `_start`

Disk layout:
```
LBA 0       MBR / Stage 1
LBA 1-4     Stage 2
LBA 5-4095  Kernel binary (up to ~2MB)
LBA 4096+   FAT16 partition, mounted as /home
```

---

## Kernel (kernel/)

### Core

| File | What it does |
|------|-------------|
| `kernel.c/.h` | kmain() entry, initializes IDT/GDT/PIC/PIT/keyboard/mouse/VBE, starts desktop |
| `idt.c/.h` | IDT setup, 256 gate descriptors |
| `irq.c/.h` | IRQ dispatch, handler registration |
| `pic.c/.h` | 8259 PIC init, IRQ masking, EOI |
| `panic.c/.h` | Kernel panic with register dump and stack trace |
| `ports.h` | inb/outb/inw/outw port I/O macros |
| `assert.h` | Assert macros |

### Memory

| File | What it does |
|------|-------------|
| `memory.c/.h` | Physical memory manager, bitmap allocator over the first 32MB, kernel heap |
| `paging.c` | Page tables, identity-mapped address space |

The kernel heap uses a bump allocator with a free list. Everything runs at ring 0 in a flat 32-bit address space. The stack lives from 0x800000 to 0x880000 (grows down, 512KB).

### Processes

| File | What it does |
|------|-------------|
| `process.c/.h` | PCB, process list, round-robin scheduler |
| `context_switch.asm` | Saves EBX/ESI/EDI/EBP/EFLAGS, swaps ESP/EIP |

Up to 32 threads. Scheduler is preemptive, driven by IRQ0 at 200Hz (5ms slices). Process states: running, ready, blocked, zombie. Primitives: spawn(), exit(), wait().

### Filesystem

| File | What it does |
|------|-------------|
| `vfs.c/.h` | VFS layer: open, read, write, close, seek, stat, readdir |
| `vfs_helpers.c/.h` | read_all(), write_all(), read_text(), write_text() |
| `ramfs.c/.h` | In-memory root filesystem, populated at boot with programs/docs/demos |
| `devfs.c/.h` | /dev entries: null, zero, console, serial, random |
| `fat16.c/.h` | FAT16: MBR parsing, cluster chains, file read/write/create |
| `fat16_vfs.c/.h` | FAT16 to VFS adapter |
| `blockdev.c/.h` | Block device abstraction |
| `blockcache.c/.h` | 64-entry LRU sector cache, write-back, flushes periodically |

Filesystem layout at runtime:
```
/           RamFS, ephemeral, rebuilt each boot
  bin/      built-in CupidC programs
  demos/    CupidASM demo programs
  docs/     documentation
  dev/      DevFS: null, zero, console, serial, random
  home/     FAT16, persistent user data on disk
```

### Graphics

| File | What it does |
|------|-------------|
| `graphics.c/.h` | Pixel, line, rect primitives with clipping |
| `gfx2d.c/.h` | Gradients (H/V/radial), shadows, dither, alpha blending, file dialogs |
| `gfx2d_effects.c/.h` | Blur, sharpen, sepia, noise, color manipulation |
| `gfx2d_icons.c/.h` | Desktop icon registration, hit-testing, drag and drop |
| `gfx2d_assets.c/.h` | Texture loading and caching |
| `gfx2d_transform.c/.h` | Scale, rotate, skew, perspective |
| `font_8x8.c/.h` | 8x8 bitmap font data and renderer |
| `bmp.c/.h` | BMP codec: 24-bit uncompressed read/write, 32bpp output |

All rendering goes to a RAM back buffer first. `vga_flip()` copies it to the linear framebuffer. Double-buffered so there's no tearing.

### GUI

| File | What it does |
|------|-------------|
| `gui.c/.h` | Window list, z-order, drag, focus, minimize, close (up to 16 windows) |
| `gui_widgets.c/.h` | Checkboxes, radio buttons, dropdowns, sliders, progress bars |
| `gui_containers.c/.h` | Panels, tabs, splitters, groups |
| `gui_menus.c/.h` | Menu bars, dropdown menus, context menus, toolbars, status bars, tooltips |
| `gui_themes.c/.h` | 6 built-in themes, .theme file load/save |
| `gui_events.c/.h` | Mouse, keyboard, and window event dispatch |
| `ui.c/.h` | Higher-level controls on top of the widget layer |

Themes include Windows 95, Modern, Dark, and a few others. Theme files can be saved and loaded from disk.

### Desktop

`desktop.c/.h` handles the desktop shell: animated gradient background, taskbar with clock, icon grid, and the main event loop. On mouse-move it only redraws the cursor, not the whole screen. The background color LUT is recalculated at most every 3-4 animation frames.

### Apps

| File | What it does |
|------|-------------|
| `notepad.c/.h` | Text editor with menus, scrollbars, clipboard, undo/redo, file open/save |
| `terminal_app.c/.h` | GUI terminal window: scrolling text buffer, PS/2 input, ANSI color support |
| `terminal_ansi.c/.h` | ANSI escape sequence parser: colors, cursor positioning, screen clear |
| `calendar.c/.h` | Date/time math, RTC integration, taskbar clock, calendar popup |
| `clipboard.c/.h` | System clipboard, shared across Notepad and Terminal |
| `ed.c/.h` | Original line editor in C, superseded by ed.cc |

### Compilers and languages

**CupidC** (`cupidc*.c`) is a HolyC-inspired C dialect:
- Single-pass recursive descent compiler
- JIT mode: compile and run .cc files in memory immediately
- AOT mode: compile to ELF32 binaries on disk
- Inline assembly, structs up to 16 fields, full ring-0 kernel bindings
- Limits: 128KB code, 32KB data, 256 functions, 512 symbols per unit

**CupidASM** (`as*.c`) is an Intel-syntax x86-32 assembler:
- 62 mnemonics, 24 registers (8/16/32-bit)
- JIT and AOT (ELF32) modes
- Directives: INCLUDE, RESERVE, alignment
- Forward references, up to 512 labels
- Kernel bindings for print, malloc, VFS, graphics calls

**CupidScript** (`cupidscript*.c`) is a bash-like scripting language (.cup files):
- Variables, if/else, while, for loops
- Functions with parameters and return values
- Pipes (|), redirects (> and >>), background jobs (&)
- Arrays, string operations
- Calls shell commands and kernel functions directly

**CupidDis** (`dis.c/.h`) is an x86-32 disassembler used for debugging.

### Program execution

| File | What it does |
|------|-------------|
| `exec.c/.h` | ELF32 loader: program headers, BSS zeroing, relocation |
| `syscall.c/.h` | Syscall table passed to ELF programs as a struct of function pointers |

### Shell (kernel/shell.c)

The shell handles command parsing, pipelines, input/output redirection, background jobs, history with arrow-key navigation, and tab completion. Typing a .cc filename runs it through CupidC JIT. Typing a .asm file runs it through CupidASM JIT. Typing a .cup file runs it through CupidScript.

### Utility libraries

| File | What it does |
|------|-------------|
| `string.c/.h` | strlen, strcmp, strcpy, strcat, strtok, strstr, sprintf and more |
| `math.c/.h` | 64-bit integer math, g2d_isqrt(), trig approximations, itoa/atoi |

---

## Drivers (drivers/)

| File | What it does |
|------|-------------|
| `vga.c/.h` | VBE 640x480x32bpp, double-buffering, vsync via Y_OFFSET page flip |
| `keyboard.c/.h` | PS/2 keyboard on IRQ1, scancode to ASCII, modifiers, key repeat, circular buffer |
| `mouse.c/.h` | PS/2 mouse on IRQ12, 3-byte packet parsing, scroll wheel, cursor |
| `pit.c/.h` | 8254 PIT channel 0 at 200Hz, channel 2 for speaker |
| `timer.c/.h` | Tick counter, sleep(), multi-channel timer callbacks |
| `speaker.c/.h` | PC speaker beep via port 0x61 |
| `ata.c/.h` | ATA/IDE PIO, 28-bit LBA, IDENTIFY, read/write on primary channel |
| `rtc.c/.h` | Real-time clock from CMOS, BCD to binary, NMI masking |
| `serial.c/.h` | COM1 at 115200 baud, used for kernel debug output |

---

## Built-in programs (bin/)

65 CupidC programs embedded in RamFS at boot, all directly runnable from the shell:

| Category | Programs |
|----------|---------|
| File ops | cat, cp, find, grep, ls, mkdir, mv, rm, rmdir, touch |
| Text | ed, echo, clear, setcolor, resetcolor |
| System | date, help, history, ps, kill, sysinfo, time, registers, yield |
| Memory | memdump, memstats, memcheck, memleak |
| Debug | logdump, loglevel, stacktrace, crashtest |
| Graphics | gfxdemo, gfxtest, paint, bmptest |
| Shell | cd, pwd, mount, sync, reboot, spawn, cachestats |
| Network | cupidfetch |
| CupidC demos | feature1_types through feature10_repl, cupidc_test1 through cupidc_test5 |
| Build | build.cup |

---

## Assembly demos (demos/)

19 CupidASM programs embedded in RamFS. Run with `as <name>.asm` from the shell:

hello, loop, fibonacci, factorial, bubblesort, stack, data, math, include_feature, include_helper, jcc_aliases, asm_compat_reserve, reserve_directives, fs_syscalls, syscall_table_demo, syscall_vfs_extended_demo, parity_core, parity_diag, parity_gfx2d

---

## User programs (user/)

The user/ directory has example ELF32 programs (hello.c, cat.c, ls.c) and user/cupid.h which defines the syscall table ABI. Use cupid.h when writing programs that get compiled to ELF and loaded by the kernel.

---

## Memory layout

```
0x007C00            Stage 1 bootloader (512 bytes)
0x007E00            Stage 2 bootloader (2KB)
0x100000            Kernel start (_start)
                    .text, .rodata, .data (2MB max)
                    .bss
0x400000            CupidC JIT region (128KB code + 32KB data)
0x500000            CupidASM JIT region (128KB code + 32KB data)
0x800000            Stack guard page
0x800000-0x880000   Kernel stack (512KB, grows down)
0xE0000000+         VBE linear framebuffer (address comes from BIOS)
```

---

## Interrupt handling

256-entry IDT. CPU exceptions are entries 0-31. IRQs start at 32 (PIC remapped).

| IRQ | Source |
|-----|--------|
| IRQ0 (32) | PIT timer at 200Hz, drives scheduler, animation, and clock |
| IRQ1 (33) | PS/2 keyboard |
| IRQ12 (44) | PS/2 mouse |

Exceptions print a register dump and stack trace before halting.

---

## Performance notes

Changes made in the 2026-02-16 optimization pass:

- Added g2d_isqrt() to replace `while(k*k<j) k++` patterns throughout graphics code
- gfx2d_gradient_v now uses g2d_fill32 for row fills instead of a per-pixel loop
- gfx2d_gradient_radial pre-clips the draw bounds and writes directly to the framebuffer pointer
- gfx2d_shadow is now single-pass instead of a blur x width x height triple loop
- vga_clear_screen uses an 8-pixel unrolled store loop
- desktop_redraw_cycle has a cursor-only path that skips full repaints on mouse moves
- Background animation LUT recalculates at most every 3-4 frames
- vga_retrace_timeout reduced from 1,000,000 to 50,000 cycles (was blocking up to 100ms)
- PIT raised from 100Hz to 200Hz, giving 5ms scheduler slices
- Terminal background is drawn once; characters are not rendered twice on colored backgrounds

---

## Adding to the kernel

1. Add .c/.h files to kernel/ or drivers/
2. Add the .c file to the object list in the Makefile
3. Run `make`

New CupidC programs go in bin/ and are automatically embedded in RamFS at build time. New assembly demos go in demos/. CupidScript files use the .cup extension and can be placed anywhere on the VFS.

---

## Requirements

- NASM
- GCC with 32-bit support (gcc-multilib on 64-bit hosts)
- GNU Make
- QEMU (qemu-system-i386)
- mtools (mcopy, mdir) for FAT16 image work
- Linux or similar Unix environment

---

## License

GNU General Public License v3.0

Built in dedication to Terry A. Davis and TempleOS.
