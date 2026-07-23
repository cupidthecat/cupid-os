# Architecture

cupid-os is a monolithic, single-address-space, ring-0 operating system for 32-bit x86. The kernel, drivers, shell, and applications all run in the same flat memory space with full hardware access.

---

## Boot Sequence

```
BIOS loads boot.asm at 0x7C00 (real mode, 16-bit)
    │
    ├── Set up stack at 0x7C00
    ├── Read kernel chunks through the 0x10000 real-mode bounce buffer
    ├── Copy each chunk to its final 0x00100000+ address with unreal mode
    ├── Set 640x480x32bpp via Bochs VBE I/O ports (0x01CE/0x01CF)
    ├── Read VBE LFB address from PCI BAR0 -> store at 0x0500
    ├── Set up GDT (flat model: code + data segments)
    ├── Switch to protected mode (CR0 bit 0)
    └── Far jump to kernel entry at 0x00100000
            │
            ├── idt_init()          - Interrupt Descriptor Table
            ├── pic_init()          - PIC remapping (IRQ0->32, IRQ8->40)
            ├── irq_init()          - IRQ handler registration
            ├── pmm_init()          - Physical memory manager
            ├── paging_init()       - Identity-mapped 4KB pages (512MB)
            │                         + maps VBE LFB region from 0x0500
            ├── heap_init()         - Kernel heap (256MB initial arena) with canaries
            ├── keyboard_init()     - PS/2 keyboard (IRQ1)
            ├── pit_init()          - PIT at 200Hz (IRQ0)
            ├── serial_init()       - COM1 at 115200 baud
            ├── rtc_init()          - CMOS Real-Time Clock
            ├── fat16_init()        - FAT16 filesystem + block cache
            ├── fs_init()           - In-memory filesystem
            ├── vfs_init()          - Virtual File System
            │   ├── Register ramfs, devfs, fat16 filesystem types
            │   ├── Mount ramfs at /
            │   ├── Create /bin, /tmp, /home directories
            │   ├── Mount devfs at /dev (null, zero, random, serial)
            │   ├── Mount fat16 at /disk (ATA disk)
            │   ├── Mount persistent homefs at /home
            │   └── Pre-populate /LICENSE.txt, /MOTD.txt
            ├── process_init()      - Process table, idle process (PID 1)
            ├── Register desktop as PID 2
            ├── vga_init_vbe()      - Init 640x480 32bpp framebuffer
            ├── mouse_init()        - PS/2 mouse (IRQ12)
            └── desktop_run()       - Main event loop
```

---

## Memory Layout

```
0x00000000 ┌──────────────────────────┐
           │ Low BIOS/boot data       │ IVT, BDA, boot scratch
0x000A0000 ├──────────────────────────┤
           │ VGA/BIOS hole            │ reserved
0x00100000 ├──────────────────────────┤
           │ Kernel image             │ .text/.rodata/.data/.bss
           │                          │ extends to linker _kernel_end
0x00C00000 ├──────────────────────────┤
           │ Kernel stack             │ 2MB, grows down
0x00E00000 ├──────────────────────────┤
           │ External ELF arena       │ 2MB, permanent reservation/exclusive lease
0x01000000 ├──────────────────────────┤
           │ CupidC JIT/AOT           │ 1MB code + 8MB data
0x01900000 ├──────────────────────────┤
           │ Reserved gap             │
0x01A00000 ├──────────────────────────┤
           │ CupidASM JIT/AOT         │ 1MB code + 1MB data
0x01C00000 ├──────────────────────────┤
           │ Heap/pages/process stacks│ PMM + kmalloc arena
0x20000000 ├──────────────────────────┤
           │ End of managed memory    │ 512MB total
           └──────────────────────────┘
           ·
           · (unmapped gap)
           ·
0xFD000000 ┌──────────────────────────┐
           │ VBE Linear Framebuffer   │ ← 640x480x4 = 1.2MB
           │ (identity-mapped by      │   PCI BAR0, QEMU default
           │  paging_init)            │
0xFD140000 └──────────────────────────┘
```

---

## Source-tree layout

The kernel source is organised into subsystem subdirectories. Every
subdir is on the include path (`-I./kernel/<subdir>`), so sources
use bare `#include "foo.h"` regardless of where the header lives.

```
kernel/
├── audio/      AC97 driver, mixer, OPL3, MIDI/MUS
├── core/       kmain, panic, process, scheduler, syscall,
│               app_launch, types, debug, ports, string
├── cpu/        IDT, IRQ, PIC, FPU, libm, math, simd, ksyms
├── crypto/     AES, ChaCha20, SHA, HMAC, HKDF, RSA, x25519,
│               P-256, ECDSA, ASN.1, X.509, csprng
├── doom/       vendored doomgeneric + dglibc shim
├── fs/         VFS, FAT16, ISO9660, ramfs, devfs, homefs,
│               loopdev, blockcache, blockdev
├── gfx/        gfx2d, BMP/PNG/JPEG, font, graphics
├── gui/        gui widgets, desktop, ed, notepad, terminal_app,
│               ANSI, clipboard, ui
├── lang/       CupidC compiler, CupidASM, CupidScript, shell,
│               exec, godspeak, dis
├── mm/         memory, paging, swap, swap_disk
├── network/    ARP, IP, ICMP, UDP, TCP, DHCP, DNS, sockets,
│               net_if
├── smp/        SMP, MP tables, LAPIC, IOAPIC, BKL, per-CPU,
│               ACPI, AP trampoline
├── tls/        TLS 1.2 / 1.3 record + handshake + CA bundle
├── usb/        USB core, UHCI, EHCI, HID, hub, MSC
└── util/       calendar, generated *_programs_gen.c

drivers/        ATA, keyboard, mouse, PIT, RTC, serial, speaker,
                timer, VGA, PCI, RTL8139, E1000
```

The module dependencies run from top to bottom and contain no cycles:

```
gui      → gfx, lang, fs, mm, core
lang     → fs, mm, core, cpu
fs       → mm, core, drivers (ATA), crypto (csprng for /dev/random)
network  → core, drivers (NICs)
tls      → network, crypto, core
audio    → drivers (PCI), core
crypto   → core (types, string only)
smp      → core, cpu, mm, drivers (PIC, PIT)
mm       → core, cpu
cpu      → core
drivers  → core
core     → (nothing)
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
| Timer | `timer.c/h`, `pit.c/h` | IRQ0 | 200Hz PIT, uptime, sleep |
| VGA | `vga.c/h` | - | VBE 640x480 32bpp, double buffering |
| ATA | `ata.c/h` | - | PIO disk read/write |
| Serial | `serial.c/h` | - | COM1 logging |
| Speaker | `speaker.c/h` | - | PC speaker tones |
| RTC | `rtc.c/h` | - | CMOS real-time clock |

### Subsystems
| Subsystem | Files | Purpose |
|-----------|-------|---------|
| Shell | `shell.c/h` | interactive shell with CWD, REPL fallback, completion, pipes/redirects |
| CupidScript | `cupidscript*.c/h` | Bash-like scripting language |
| Ed Editor | `ed.c/h` | Unix ed(1) line editor |
| VFS | `vfs.c/h` | Virtual File System with mount table and path resolution |
| RamFS | `ramfs.c/h` | In-memory filesystem (root, /bin, /tmp) |
| DevFS | `devfs.c/h` | Device filesystem (/dev/null, zero, random, serial) |
| FAT16 VFS | `fat16_vfs.c/h` | FAT16 VFS wrapper for /disk |
| homefs | `homefs.c/h` | persistent `/home` image stored in `/disk/HOMEFS.SYS` |
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
| IRQ0 | 32 | `timer_callback` | PIT timer tick (200Hz), scheduler flag |
| IRQ1 | 33 | `keyboard_handler` | PS/2 keyboard input |
| IRQ12 | 44 | `mouse_handler` | PS/2 mouse input |
| - | 0 | `division_error` | Divide by zero exception |
| - | 6 | `invalid_opcode` | Invalid opcode exception |
| - | 13 | `general_protection` | GPF |
| - | 14 | `page_fault` | Page fault (with CR2 reporting) |

---

## Execution Model

cupid-os uses **deferred preemptive multitasking**:

1. **PIT IRQ0** fires every 5ms -> sets `need_reschedule` flag
2. Flag is checked at **safe voluntary points** only:
   - Desktop main loop (before `HLT`)
   - `process_yield()` calls
   - Idle process loop
3. Context switch happens via pure assembly `context_switch()`:
   - Save EBP, EDI, ESI, EBX, EFLAGS on current stack
   - Store ESP into old process PCB
   - Load new process ESP and jump to new EIP

Deferring the switch keeps context changes out of interrupt handlers, where a switch could corrupt the active stack.

---

## CupidScript Execution Pipeline

```
.cup file on disk
      │
      ▼
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│    Lexer     │ ──▶│    Parser    │ ──▶│  Interpreter │
│ (tokenize)   │     │ (build AST)  │     │ (execute AST)│
└──────────────┘     └──────────────┘     └──────┬───────┘
                                                 │
                           ┌─────────────────────┤
                           ▼                     ▼
                    ┌──────────────┐     ┌──────────────┐
                    │   Runtime    │     │    Shell     │
                    │ (variables,  │     │ (execute_    │
                    │  functions)  │     │  command())  │
                    └──────────────┘     └──────────────┘
```

---

## See Also

- [Getting Started](Getting-Started) - Build and run
- [Process Management](Process-Management) - Scheduler details
- [Filesystem](Filesystem) - Disk I/O architecture
- [Debugging](Debugging) - Memory safety and crash testing
