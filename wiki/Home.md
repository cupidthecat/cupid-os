# cupid-os Wiki

Welcome to the **cupid-os** wiki! cupid-os is a modern, 32-bit operating system written in C and x86 Assembly, combining clean design with nostalgic aesthetics. It runs entirely in ring 0 with no security boundaries — inspired by TempleOS and OsakaOS.

---

## 📖 Pages

| Page | Description |
|------|-------------|
| [Getting Started](Getting-Started) | Build requirements, compiling, booting in QEMU |
| [Architecture](Architecture) | System overview, memory layout, boot sequence, component diagram |
| [Shell Commands](Shell-Commands) | Full reference for all 24 built-in shell commands |
| [CupidScript](CupidScript) | Scripting language guide — variables, loops, functions, examples |
| [Ed Editor](Ed-Editor) | How to use the built-in ed(1) line editor |
| [Desktop Environment](Desktop-Environment) | VGA graphics, window manager, mouse, terminal app |
| [Process Management](Process-Management) | Scheduler, context switching, process API |
| [Filesystem](Filesystem) | In-memory FS, FAT16 driver, disk I/O, block cache |
| [Debugging](Debugging) | Serial console, memory safety, crash testing, assertions |

---

## 🏗️ System at a Glance

```
┌─────────────────────────────────────────────────────┐
│                   cupid-os                          │
├──────────┬──────────┬───────────┬───────────────────┤
│  Desktop │ Terminal │  Notepad  │   User Scripts    │
│  (GUI)   │  (Shell) │  (Editor) │   (.cup files)    │
├──────────┴──────────┴───────────┴───────────────────┤
│              Shell + CupidScript                    │
│   24 commands │ bash-like scripting │ ed editor     │
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
│              FAT16 Filesystem                       │
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

## 🎯 Philosophy

cupid-os embraces complete user empowerment:

- **No security boundaries** — all code runs in ring 0
- **Direct hardware access** — no abstraction hiding the metal
- **Full memory visibility** — no virtual memory restrictions
- **Transparency** — every byte of the system is inspectable

This makes cupid-os ideal for learning how computers really work at the lowest level.

---

## 🔧 Quick Start

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

## 📁 Project Structure

```
cupid-os/
├── boot/
│   └── boot.asm              # Bootloader (real → protected mode)
├── kernel/
│   ├── kernel.c/h             # Main kernel, VGA init, entry point
│   ├── shell.c/h              # Shell with 24 commands
│   ├── cupidscript*.c/h       # CupidScript scripting language
│   ├── ed.c/h                 # Ed line editor
│   ├── process.c/h            # Process scheduler
│   ├── context_switch.asm     # Assembly context switch
│   ├── memory.c/h             # Heap, PMM, canaries
│   ├── fat16.c/h              # FAT16 filesystem
│   ├── gui.c/h                # Window manager
│   ├── desktop.c/h            # Desktop environment
│   ├── graphics.c/h           # Drawing primitives
│   ├── terminal_app.c/h       # GUI terminal
│   ├── notepad.c/h            # Notepad application
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

## 📜 License

cupid-os is released under the **GNU General Public License v3**.
