# cupid-os Wiki

Welcome to the **cupid-os** wiki! cupid-os is a modern, 32-bit operating system written in C and x86 Assembly, combining clean design with nostalgic aesthetics. It runs entirely in ring 0 with no security boundaries — inspired by TempleOS and OsakaOS.

---

## Pages

| Page | Description |
|------|-------------|
| [Getting Started](Getting-Started) | Build requirements, compiling, booting in QEMU |
| [Architecture](Architecture) | System overview, memory layout, boot sequence, component diagram |
| [Shell Commands](Shell-Commands) | Full reference for all 24 built-in shell commands |
| [CupidScript](CupidScript) | Scripting language guide — variables, loops, functions, examples |
| [CupidC Compiler](CupidC-Compiler) | HolyC-inspired C compiler — JIT/AOT, inline assembly, kernel bindings |
| [Ed Editor](Ed-Editor) | How to use the built-in ed(1) line editor |
| [Desktop Environment](Desktop-Environment) | VGA graphics, window manager, mouse, terminal app |
| [Process Management](Process-Management) | Scheduler, context switching, process API |
| [Filesystem](Filesystem) | VFS, RamFS, DevFS, FAT16, disk I/O, program loader |
| [ELF Programs](ELF-Programs) | Compiling, loading, and running ELF32 executables, syscall table API |
| [Debugging](Debugging) | Serial console, memory safety, crash testing, assertions |

---

## System at a Glance

```
┌─────────────────────────────────────────────────────┐
│                   cupid-os                          │
├──────────┬──────────┬───────────┬───────────────────┤
│  Desktop │ Terminal │  Notepad  │   User Scripts    │
│  (GUI)   │  (Shell) │  (Editor) │   (.cup files)    │
├──────────┴──────────┴───────────┴───────────────────┤
│              Shell + CupidScript + CupidC           │
│   40+ commands │ bash-like scripting │ C compiler  │
│   colors │ pipes │ redirects │ jobs │ JIT + AOT   │
├─────────────────────────────────────────────────────┤
│       Virtual File System (VFS)                      │
│   RamFS (/) │ DevFS (/dev) │ FAT16 (/home)         │
├─────────────────────────────────────────────────────┤
│              Process Scheduler                      │
│   Round-robin │ 10ms slices │ 32 kernel threads     │
├─────────────────────────────────────────────────────┤
│              Window Manager (GUI)                   │
│   16 windows │ z-order │ drag │ focus │ taskbar     │
├──────────┬──────────┬───────────┬───────────────────┤
│ Keyboard │  Mouse   │   VGA     │    Serial         │
│  (IRQ1)  │ (IRQ12)  │ Mode 13h  │   (COM1)          │
├──────────┴──────────┴───────────┴───────────────────┤
│              FAT16 + Block Cache + ATA               │
│   Block cache │ ATA/IDE PIO │ MBR partitions        │
├─────────────────────────────────────────────────────┤
│              Memory Management                      │
│   PMM bitmap │ Paging │ Heap + canaries │ Tracking  │
├─────────────────────────────────────────────────────┤
│              IDT / IRQ / PIC / PIT                  │
│   Interrupts │ Exceptions │ Timer (100Hz)           │
├─────────────────────────────────────────────────────┤
│              Bootloader (boot.asm)                  │
│   Real mode → Protected mode │ GDT │ Load kernel   │
└─────────────────────────────────────────────────────┘
```

---

## Philosophy

cupid-os embraces complete user empowerment:

- **No security boundaries** — all code runs in ring 0
- **Direct hardware access** — no abstraction hiding the metal
- **Full memory visibility** — no virtual memory restrictions
- **Transparency** — every byte of the system is inspectable

This makes cupid-os ideal for learning how computers really work at the lowest level.

---

## Quick Start

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install nasm gcc make qemu-system-x86 dosfstools

# Build
make

# Run (with serial output)
make run

# Run with FAT16 disk
make run-disk
```

---

## Project Structure

```
cupid-os/
├── boot/
│   └── boot.asm              # Bootloader (real → protected mode)
├── kernel/
│   ├── kernel.c/h             # Main kernel, VGA init, entry point
│   ├── shell.c/h              # Shell with 38 commands + CWD
│   ├── vfs.c/h                # Virtual File System core
│   ├── ramfs.c/h              # In-memory filesystem (RamFS)
│   ├── devfs.c/h              # Device filesystem (DevFS)
│   ├── fat16_vfs.c/h          # FAT16 VFS wrapper
│   ├── exec.c/h               # CUPD program loader
│   ├── cupidscript*.c/h       # CupidScript scripting language
│   ├── cupidc.h/c             # CupidC compiler (JIT/AOT driver)
│   ├── cupidc_lex.c           # CupidC lexer (tokenizer)
│   ├── cupidc_parse.c         # CupidC parser + x86 code gen
│   ├── cupidc_elf.c           # CupidC ELF32 binary writer
│   ├── cupidscript_streams.c/h # Stream system (pipes, fd table)
│   ├── cupidscript_strings.c  # Advanced string operations
│   ├── cupidscript_arrays.c/h # Arrays & associative arrays
│   ├── cupidscript_jobs.c/h   # Background job management
│   ├── terminal_ansi.c/h      # ANSI escape sequence parser
│   ├── ed.c/h                 # Ed line editor
│   ├── process.c/h            # Process scheduler
│   ├── context_switch.asm     # Assembly context switch
│   ├── memory.c/h             # Heap, PMM, canaries
│   ├── fat16.c/h              # FAT16 filesystem driver
│   ├── gui.c/h                # Window manager
│   ├── desktop.c/h            # Desktop environment
│   ├── graphics.c/h           # Drawing primitives
│   ├── terminal_app.c/h       # GUI terminal
│   ├── notepad.c/h            # Notepad application (VFS file dialog)
│   └── ...                    # IDT, IRQ, PIC, panic, etc.
├── drivers/
│   ├── keyboard.c/h           # PS/2 keyboard (IRQ1)
│   ├── mouse.c/h              # PS/2 mouse (IRQ12)
│   ├── vga.c/h                # VGA Mode 13h
│   ├── ata.c/h                # ATA/IDE disk
│   ├── serial.c/h             # COM1 serial port
│   ├── timer.c/h + pit.c/h    # PIT timer
│   └── speaker.c/h            # PC speaker
├── docs/plans/                # Design documents
├── link.ld                    # Linker script
├── Makefile                   # Build system
└── LICENSE                    # GPLv3
```

---

## License

cupid-os is released under the **GNU General Public License v3**.
