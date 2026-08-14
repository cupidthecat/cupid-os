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
0x00F00000 ├──────────────────────────┤
           │ Kernel stack             │ 2MB, grows down
0x01100000 ├──────────────────────────┤
           │ CupidC JIT/AOT           │ 1MB code + 8MB data
0x01A00000 ├──────────────────────────┤
           │ CupidASM JIT/AOT         │ 1MB code + 1MB data
0x01C00000 ├──────────────────────────┤
           │ External ELF arena       │ 2MB, permanent reservation/exclusive lease
0x01E00000 ├──────────────────────────┤
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
└── util/       calendar, generated *_programs_gen.cc

drivers/        ATA, keyboard, mouse, PIT, RTC, serial, speaker,
                timer, VGA, PCI, RTL8139, E1000
```

The normal CupidC image cohort has 239 checked-in roots and one generated
symbol root. The strict non-Doom kernel and driver frontier covers 156 of
those checked-in roots. All normal sources use `.cc`. Five shared Toolchain
roots also belong to the 19-source i386 Linux fixed point, and their native
GCC or Clang rules select C with `-x c`. ADRs 0124 and 0126 record the first
two naming steps, ADR 0129 records the lexer transfer, ADR 0135 records the
Nuked OPL3 transfer, ADR 0139 records the JPEG and glyph-raster transfer, ADR
0167 records the FPU and SMP transfer, ADR 0176 records the libm transfer,
ADR 0180 records the kernel entry and SIMD transfer, and ADR 0181 records the
string transfer. The checked wrappers own `kernel/core/kernel.cc`,
`kernel/cpu/simd.cc`, and `kernel/core/string.cc`. Their deterministic
objects are 25,920, 8,768, and 14,460 bytes. The latest complete two-pass
frontier predates the 156th source. Its 155 roots pass twice against a frozen
445-file snapshot; both object sets are
byte-identical and total 3,749,796 bytes. The combined graph keeps the ISO
runtime fixture as an explicit image input. No strict checked-in kernel or
driver root still uses the host compiler.

The current 156-source production build passes. A broader two-generation
frontier run timed out after 1,204 seconds and remains incomplete.

The normal Toolchain root builds fifteen `.cc` contracts and the runtime
probe with stage-two and stage-three CupidC. Its publisher accepts only a
dedicated `cupidc-contracts` directory inside the source tree. It validates
the target before work and again before promotion, and an existing
destination must already verify as a complete cohort. Arbitrary directories,
source trees, files, and symbolic links remain untouched. Exact initial,
private, and newly discovered contract inventories catch additions, removals,
and restored edits that changed a copied input. Every contract run derives the
cohort from its executable, requires a named manifest artifact, and verifies
all artifact hashes, the current 65-input contract set, the checked seed
manifest, and the 50-file fixed-point source inventory before execution. The
contract inventory includes the small Windows probe, the native Windows tool
runtime and startup, CupidLD publication runtime and bridge, direct contract,
`direct.h`, `windows.h`, the user syscall ABI contract and its six declarations,
the Toolchain Makefile, the publisher, and the independent Python ABI oracle. One
captured seed-manifest byte sequence supplies the digest, decoded data, schema
checks, and build plan. Seventeen objects and sixteen executables must match
across stages before the 21-artifact cohort can be published. Contract runs
use a private copy of the verified cohort. The user ABI check also gives the
Cupid contract and Python oracle one shared six-file snapshot, then rechecks
the live publication and sources before success.
Native contract binaries are optional oracles.

At the promoted-seed checkpoint, stages two and three linked matching native
Windows images for all five hosted tools from Cupid-built objects. CupidASM
supplied the entry and imported API bridges, while CupidLD authored each PE
image and its IAT slots. CupidLD added four publication imports to the shared
twelve. Windows ran help plus a useful success and failure path for each tool.
CupidDis also checked quoted raw-input parity with the Linux tool. These five
images form the checked Windows
execution seed used by output-bearing production recipes. Toolchain contracts,
the user ABI contract, and artifact-size policy still run the Linux seed
through WSL. Source head freezes the PE execution seed and the Linux plan
manifest separately, then reconstructs native Windows stages two through four.
Stages two and three are transition generations; stages three and four are the
convergence pair. The former stage-two to stage-three comparison stopped safely
at `cupidobj_main` after 821.9 seconds on Windows and 883.3 seconds on Linux.
New stack-probe code generation changed compiler-produced objects. Later
uncapped proofs passed the final-pair gates. Windows matched 20 C objects, two
assembly objects, and five tools in 20 minutes 43 seconds with 5/5/5 behavior
cases. Linux matched 19 C objects, startup, and five tools in 24 minutes 22
seconds with 5/18/16 behavior cases. Both reports bind the same 50-input
snapshot, SHA-256
`d8481a39e0d1c7f42779a8c9f5fc5de10d7e5b9bc4df63ce6afe9ddd9c9716da`.
Those reports remain preliminary because they began from uncommitted source.
Linux later passed a 1,383.775-second clean proof, promoted the stage-four
seed, and passed a 1,411.998-second reproof with all five initial seed
comparisons true. The clean native Windows proof is next. ADR 0280 records the
Linux promotion.

Strong four-vCPU runtime checks pass with both NICs through SMP, RDRAND, all
62 crypto checks, USB storage, audio, TrueType glyphs, a baseline JPEG decode,
the desktop, terminal, and in-OS CupidC. They require `[fpu] SSE2 enabled`,
`[fpu] boot smoke ok`, and `FPU boot smoke passed`. A typed production-object
policy independently checks CR4, `FNINIT`, and `LDMXCSR` ordering.

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
| Kernel entry | `kernel/core/kernel.cc`, `kernel/core/kernel.h` | Fixed stack, linked BSS clear, non-returning entry handoff, VGA, initialization, and main print functions |
| IDT | `idt.cc/h` | Interrupt descriptor table setup |
| ISR/IRQ | `isr.asm`, `irq.cc/h` | Interrupt/exception dispatching |
| PIC | `pic.cc/h` | Programmable interrupt controller |
| Memory | `memory.cc/h`, `paging.cc` | PMM, heap, paging, canaries, leak detection |
| VFS | `vfs.cc/h` | Virtual filesystem, mount table, path resolution |
| Panic | `panic.cc/h`, `assert.h` | Crash handler, assertions |
| Strings | `string.cc/h` | `strlen`, `strcmp`, `memcpy`, `memset` |
| Math | `math.cc/h` | 64-bit division, `itoa`, hex printing |

### Drivers
| Driver | Files | IRQ | Purpose |
|--------|-------|-----|---------|
| Keyboard | `keyboard.cc/h` | IRQ1 | PS/2 input with modifiers |
| Mouse | `mouse.cc/h` | IRQ12 | PS/2 mouse with cursor |
| Timer | `timer.cc/h`, `pit.cc/h` | IRQ0 | 200Hz PIT, uptime, sleep |
| VGA | `vga.cc/h` | - | VBE 640x480 32bpp, double buffering |
| ATA | `ata.cc/h` | - | PIO disk read/write |
| Serial | `serial.cc/h` | - | COM1 logging |
| Speaker | `speaker.cc/h` | - | PC speaker tones |
| RTC | `rtc.cc/h` | - | CMOS real-time clock |

### Subsystems
| Subsystem | Files | Purpose |
|-----------|-------|---------|
| Shell | `shell.cc/h` | interactive shell with CWD, REPL fallback, completion, pipes/redirects |
| CupidScript | `cupidscript*.cc/h` | Bash-like scripting language |
| Ed Editor | `ed.cc/h` | Unix ed(1) line editor |
| VFS | `vfs.cc/h` | Virtual File System with mount table and path resolution |
| RamFS | `ramfs.cc/h` | In-memory filesystem (root, /bin, /tmp) |
| DevFS | `devfs.cc/h` | Device filesystem (/dev/null, zero, random, serial) |
| FAT16 VFS | `fat16_vfs.cc/h` | FAT16 VFS wrapper for /disk |
| homefs | `homefs.cc/h` | persistent `/home` image stored in `/disk/HOMEFS.SYS` |
| FAT16 | `fat16.cc/h`, `blockdev.cc/h`, `blockcache.cc/h` | FAT16 driver with block cache |
| In-Memory FS | `fs.cc/h` | Legacy read-only system file table |
| Exec | `exec.cc/h` | CUPD program loader |
| Process Mgr | `process.cc/h`, `context_switch.asm` | Scheduler, context switching |
| GUI | `gui.cc/h`, `desktop.cc/h`, `graphics.cc/h`, `font_8x8.cc/h` | Window manager, desktop |
| Terminal | `terminal_app.cc/h` | GUI terminal application |
| Notepad | `bin/notepad.cc` | Text editor application (VFS file dialog) |
| Clipboard | `clipboard.cc/h` | System clipboard |
| Calendar | `calendar.cc/h` | Calendar math, time/date formatting, popup state |

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
