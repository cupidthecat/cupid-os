# Architecture

cupid-os is a monolithic, single-address-space, ring-0 operating system for 32-bit x86. Every component — kernel, drivers, shell, applications — runs in the same flat memory space with full hardware access.

---

## Boot Sequence

```
BIOS loads boot.asm at 0x7C00 (real mode, 16-bit)
    │
    ├── Set up stack at 0x7C00
    ├── Load kernel from disk (sectors 1+) to 0x10000
    ├── Set 640×480×32bpp via Bochs VBE I/O ports (0x01CE/0x01CF)
    ├── Read VBE LFB address from PCI BAR0 → store at 0x0500
    ├── Set up GDT (flat model: code + data segments)
    ├── Switch to protected mode (CR0 bit 0)
    └── Far jump to kernel entry at 0x10000
            │
            ├── idt_init()          — Interrupt Descriptor Table
            ├── pic_init()          — PIC remapping (IRQ0→32, IRQ8→40)
            ├── irq_init()          — IRQ handler registration
            ├── pmm_init()          — Physical memory manager
            ├── paging_init()       — Identity-mapped 4KB pages (32MB)
            │                         + maps VBE LFB region from 0x0500
            ├── heap_init()         — Kernel heap (2MB) with canaries
            ├── keyboard_init()     — PS/2 keyboard (IRQ1)
            ├── pit_init()          — PIT at 100Hz (IRQ0)
            ├── serial_init()       — COM1 at 115200 baud
            ├── rtc_init()          — CMOS Real-Time Clock
            ├── fat16_init()        — FAT16 filesystem + block cache
            ├── fs_init()           — In-memory filesystem
            ├── vfs_init()          — Virtual File System
            │   ├── Register ramfs, devfs, fat16 filesystem types
            │   ├── Mount ramfs at /
            │   ├── Create /bin, /tmp, /home directories
            │   ├── Mount devfs at /dev (null, zero, random, serial)
            │   ├── Mount fat16 at /home (ATA disk)
            │   └── Pre-populate /LICENSE.txt, /MOTD.txt
            ├── process_init()      — Process table, idle process (PID 1)
            ├── Register desktop as PID 2
            ├── vga_init_vbe()      — Init 640×480 32bpp framebuffer
            ├── mouse_init()        — PS/2 mouse (IRQ12)
            └── desktop_run()       — Main event loop
```

---

## Memory Layout

```
0x00000000 ┌──────────────────────────┐
           │ Interrupt Vector Table    │ (not used — we use IDT)
0x00000500 ├──────────────────────────┤
           │ VBE LFB address          │ ← Written by bootloader
0x00001000 ├──────────────────────────┤
           │ Kernel Code + Data       │ ← Loaded by bootloader
           │ (.text, .data, .bss)     │ ← starts at 0x10000
           ├──────────────────────────┤
           │ Boot Stack               │ ← 0x7C00 (real mode)
0x00110000 ├──────────────────────────┤
           │ Stack Guard Region       │ ← 512KB kernel stack
0x00190000 ├──────────────────────────┤
           │ Kernel Heap              │ ← kmalloc/kfree, 2MB
           │ (canary-protected)       │ ← includes VBE back buffer
           ├──────────────────────────┤
           │ Process Stacks           │ ← kmalloc'd, 4KB+ each
           │ (canary at bottom)       │
0x000A0000 ├──────────────────────────┤
           │ VGA Text Memory          │ ← Text mode (legacy)
0x00100000 ├──────────────────────────┤
           │ Extended Memory          │ ← PMM bitmap manages this
           │ (pages allocated here)   │
0x02000000 ├──────────────────────────┤
           │ End of managed memory    │ (32MB total)
           └──────────────────────────┘
           ·
           · (unmapped gap)
           ·
0xFD000000 ┌──────────────────────────┐
           │ VBE Linear Framebuffer   │ ← 640×480×4 = 1.2MB
           │ (identity-mapped by      │   PCI BAR0, QEMU default
           │  paging_init)            │
0xFD140000 └──────────────────────────┘
```

---

## Component Architecture

### Kernel Core
| Component | Files | Purpose |
|-----------|-------|---------|
| Kernel entry | `kernel.c/h` | VGA, init sequence, main print functions |
| IDT | `idt.c/h` | Interrupt descriptor table setup |
| ISR/IRQ | `isr.asm`, `irq.c/h` | Interrupt/exception dispatching |
| PIC | `pic.c/h` | Programmable interrupt controller |
| Memory | `memory.c/h`, `paging.c` | PMM, heap, paging, canaries, leak detection |
| VFS | `vfs.c/h` | Virtual filesystem, mount table, path resolution |
| Panic | `panic.c/h`, `assert.h` | Crash handler, assertions |
| Strings | `string.c/h` | `strlen`, `strcmp`, `memcpy`, `memset` |
| Math | `math.c/h` | 64-bit division, `itoa`, hex printing |

### Drivers
| Driver | Files | IRQ | Purpose |
|--------|-------|-----|---------|
| Keyboard | `keyboard.c/h` | IRQ1 | PS/2 input with modifiers |
| Mouse | `mouse.c/h` | IRQ12 | PS/2 mouse with cursor |
| Timer | `timer.c/h`, `pit.c/h` | IRQ0 | 100Hz PIT, uptime, sleep |
| VGA | `vga.c/h` | — | VBE 640×480 32bpp, double buffering |
| ATA | `ata.c/h` | — | PIO disk read/write |
| Serial | `serial.c/h` | — | COM1 logging |
| Speaker | `speaker.c/h` | — | PC speaker tones |
| RTC | `rtc.c/h` | — | CMOS real-time clock |

### Subsystems
| Subsystem | Files | Purpose |
|-----------|-------|---------|
| Shell | `shell.c/h` | 38-command interactive shell with CWD support |
| CupidScript | `cupidscript*.c/h` | Bash-like scripting language |
| Ed Editor | `ed.c/h` | Unix ed(1) line editor |
| VFS | `vfs.c/h` | Virtual File System with mount table and path resolution |
| RamFS | `ramfs.c/h` | In-memory filesystem (root, /bin, /tmp) |
| DevFS | `devfs.c/h` | Device filesystem (/dev/null, zero, random, serial) |
| FAT16 VFS | `fat16_vfs.c/h` | FAT16 VFS wrapper for /home |
| FAT16 | `fat16.c/h`, `blockdev.c/h`, `blockcache.c/h` | FAT16 driver with block cache |
| In-Memory FS | `fs.c/h` | Legacy read-only system file table |
| Exec | `exec.c/h` | CUPD program loader |
| Process Mgr | `process.c/h`, `context_switch.asm` | Scheduler, context switching |
| GUI | `gui.c/h`, `desktop.c/h`, `graphics.c/h`, `font_8x8.c/h` | Window manager, desktop |
| Terminal | `terminal_app.c/h` | GUI terminal application |
| Notepad | `notepad.c/h` | Text editor application (VFS file dialog) |
| Clipboard | `clipboard.c/h` | System clipboard |
| Calendar | `calendar.c/h` | Calendar math, time/date formatting, popup state |

---

## Interrupt Map

| IRQ | Vector | Handler | Purpose |
|-----|--------|---------|---------|
| IRQ0 | 32 | `timer_callback` | PIT timer tick (100Hz), scheduler flag |
| IRQ1 | 33 | `keyboard_handler` | PS/2 keyboard input |
| IRQ12 | 44 | `mouse_handler` | PS/2 mouse input |
| — | 0 | `division_error` | Divide by zero exception |
| — | 6 | `invalid_opcode` | Invalid opcode exception |
| — | 13 | `general_protection` | GPF |
| — | 14 | `page_fault` | Page fault (with CR2 reporting) |

---

## Execution Model

cupid-os uses **deferred preemptive multitasking**:

1. **PIT IRQ0** fires every 10ms → sets `need_reschedule` flag
2. Flag is checked at **safe voluntary points** only:
   - Desktop main loop (before `HLT`)
   - `process_yield()` calls
   - Idle process loop
3. Context switch happens via pure assembly `context_switch()`:
   - Save EBP, EDI, ESI, EBX, EFLAGS on current stack
   - Store ESP into old process PCB
   - Load new process ESP and jump to new EIP

This avoids the complexity and stack corruption risks of switching inside interrupt handlers.

---

## CupidScript Execution Pipeline

```
.cup file on disk
      │
      ▼
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│    Lexer     │────▶│    Parser    │────▶│  Interpreter │
│ (tokenize)   │     │ (build AST)  │     │ (execute AST)│
└──────────────┘     └──────────────┘     └──────┬───────┘
                                                  │
                           ┌──────────────────────┤
                           ▼                      ▼
                    ┌──────────────┐     ┌──────────────┐
                    │   Runtime    │     │    Shell     │
                    │ (variables,  │     │ (execute_    │
                    │  functions)  │     │  command())  │
                    └──────────────┘     └──────────────┘
```

---

## See Also

- [Getting Started](Getting-Started) — Build and run
- [Process Management](Process-Management) — Scheduler details
- [Filesystem](Filesystem) — Disk I/O architecture
- [Debugging](Debugging) — Memory safety and crash testing
