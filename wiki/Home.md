# cupid-os Wiki

**cupid-os** is a 32-bit operating system written in C and x86 assembly. Its ring-0 execution model and desktop draw on TempleOS and OsakaOS. The system does not isolate programs from the kernel.

---

## Pages

| Page | Description |
|------|-------------|
| [Getting Started](Getting-Started) | Build requirements, compiling, booting in QEMU |
| [Real Hardware](Real-Hardware) | Flashing to USB/disk, BIOS/CSM setup, booting on physical PCs |
| [Architecture](Architecture) | System overview, memory layout, boot sequence, component diagram |
| [Shell Commands](Shell-Commands) | Full reference for built-ins and auto-discovered `/bin` / `/home/bin` commands |
| [CupidScript](CupidScript) | Scripting language guide - variables, loops, functions, examples |
| [CupidC Compiler](CupidC-Compiler) | HolyC-inspired C compiler - JIT/AOT, inline assembly, kernel bindings |
| [Toolchain Bootstrap](Toolchain-Bootstrap) | Checked Cupid tools, staged rebuild, provenance, and fixed-point verification |
| [CupidDoc CTXT](CupidDoc-CTXT) | TempleOS-inspired executable `.ctxt` documents in Notepad, including clickable code, links, trees, and sprite widgets |
| [CupidASM Assembler](CupidASM-Assembler) | x86-32 assembler - Intel syntax, JIT/AOT, include files, kernel bindings |
| [CupidC 2D Graphics Library](CupidC-2D-Graphics-Library) | API reference for the software-rendered 2D graphics library |
| [User Programs](User-Programs) | Writing and deploying CupidC programs in /bin/ and /home/bin/ |
| [Ed Editor](Ed-Editor) | How to use the built-in ed(1) line editor |
| [Desktop Environment](Desktop-Environment) | VBE 640x480 32bpp graphics, window manager, mouse, terminal app |
| [Process Management](Process-Management) | Scheduler, context switching, process API |
| [Filesystem](Filesystem) | VFS, RamFS, DevFS, FAT16, disk I/O, program loader |
| [Disk Setup](Disk-Setup) | Creating and formatting FAT16 disk images for QEMU |
| [ELF Programs](ELF-Programs) | Compiling, loading, and running ELF32 executables, syscall table API |
| [Debugging](Debugging) | Serial console, memory safety, crash testing, assertions |
| [USB](USB) | UHCI + EHCI host controllers, HID keyboard/mouse, hub class, mass storage (BBB + SCSI) |
| [SMP](SMP) | SMP Tier 2: ACPI/MP discovery, per-CPU LAPIC timer, big kernel lock, IPI reschedule and cross-CPU call |
| [Networking](Networking) | RTL8139 + E1000 drivers, TCP/UDP/ICMP/ARP/DHCP/DNS, BSD sockets, HTTP/HTTPS, SSH/Telnet |

---

## Toolchain bootstrap

Retained original file-request binding groups share parent resolution while
keeping fresh file metadata checks and complete caller root and payload custody.
Both repository replays pass 210 new cases and all 3,228 preceding outcomes,
with 95 strict checks, 22 whole object pairs and one hundred rejection controls.
Complete input ownership remains unqualified on the Cupid-built UNC caller.

[The component record](https://github.com/cupidthecat/cupid-os/blob/bootstrap/cupid-self-hosting/docs/bootstrap/NATIVE-FILE-REQUEST-GROUP.md)
carries the held patch, portable receipts and preserved failures.

The held path observation group captures ordinary file, directory and first-absence
observations with complete validation before and after the group. Both complete repository
replays pass 168 new cases and all 3,060 preceding outcomes, with 91 strict
checks and 21 whole object pairs. All sixty altered-evidence controls reject. Complete
native input ownership remains unqualified on Windows UNC paths; normal image
publication still uses Python.

[The component record](https://github.com/cupidthecat/cupid-os/blob/bootstrap/cupid-self-hosting/docs/bootstrap/NATIVE-PATH-OBSERVATION-GROUP.md)
contains the held patch, complete receipts and constructor query diagnosis.

The held retained-file group API passes both complete repository replays,
3,060 API outcomes and 87 strict checks. Group capture preserves per-file
parent, metadata and payload checks, with complete observer validation at both
boundaries. Callback bytes remain provisional until success. The fresh complete
input owner still exceeds sixty seconds on both Windows UNC callers and remains
outside acceptance. [The group record](https://github.com/cupidthecat/cupid-os/blob/c2eabb95a088f938a66ec779fc4e94b1ce04844d/docs/bootstrap/NATIVE-RETAINED-STREAM-GROUP.md) includes complete evidence and
replay instructions at `c2eabb95`.

Cupid OS builds its active source with checked Cupid tools. The normal graph
contains 449 CupidBuild actions and three Python coordinators across 452
transforms. The installed `1d852023` seed pair carries the shared hosted heap
after paired cold builds, complete product comparisons and strict boots.
Normal-root build, seed-reader/release-operation and strict boot checks also
pass. The held optional source-file owner passes both complete repository
replays, preserving 382 new contracts and that packet's 1,742 API outcomes.
The held shared-selection packet passes both complete repository replays,
preserving all 2,916 current API outcomes and 83 strict checks. Temporary
selectors reject publication borrowing; complete retained observers own
custody. Full input-owner acceptance remains open after the final Windows
UNC phase timeouts.
The current 80,111-byte manual covers held file, image-stage, source
selection, stream, path observation and original file-request groups after
paired and normal-root acceptance. All 429 whole object pairs and every byte
of the complete image agree. All six strict SMP boots, sixteen exact artifact
gates per build and seventeen root policy methods pass. The raw kernel has
9,309,356 bytes. Only the raw-kernel exact policy row changes. Held file-request replay keeps `29929d49`
and path-group replay `cf52cd12`, each with its original manual and policy.
Complete native input ownership and image publication remain open.
[The bootstrap record](Toolchain-Bootstrap) contains complete receipts and
remaining work. TempleOS remains read-only and excluded from progress counts.

## System at a Glance

```
┌─────────────────────────────────────────────────────┐
│                   cupid-os                          │
├──────────┬──────────┬───────────┬───────────────────┤
│  Desktop │ Terminal │  Notepad  │   User Scripts    │
│  (GUI)   │  (Shell) │  (Editor) │   (.cup files)    │
├──────────┴──────────┴───────────┴───────────────────┤
│        Shell + Terminal + CupidScript + Tools       │
│ 100+ cmds │ pipes │ redirects │ jobs │ SSH/Telnet    │
│  completion │ ANSI/xterm │ browser │ source cmds     │
├─────────────────────────────────────────────────────┤
│              CupidC + CupidASM                      │
│  C-like JIT/AOT │ x86 asm JIT/AOT │ kernel binds     │
├─────────────────────────────────────────────────────┤
│       Virtual File System (VFS)                     │
│ RamFS (/) │ DevFS (/dev) │ FAT16 (/disk) │ homefs    │
├─────────────────────────────────────────────────────┤
│       Networking + TLS                              │
│ RTL8139/E1000 │ TCP/UDP/DNS │ HTTPS │ sshd │ browser │
├─────────────────────────────────────────────────────┤
│              Process Scheduler                      │
│   Round-robin │ 5ms ticks │ 32 kernel threads       │
├─────────────────────────────────────────────────────┤
│              Window Manager (GUI)                   │
│   16 windows │ z-order │ drag │ focus │ taskbar     │
├──────────┬──────────┬───────────┬───────────────────┤
│ Keyboard │  Mouse   │   VBE     │    Serial         │
│  (IRQ1)  │ (IRQ12)  │640x480    │   (COM1)          │
├──────────┴──────────┴───────────┴───────────────────┤
│       Storage + Media                               │
│ FAT16 │ ISO9660 │ block cache │ USB MSC │ ATA PIO    │
├─────────────────────────────────────────────────────┤
│              Memory Management                      │
│   PMM bitmap │ Paging │ Heap + canaries │ Tracking  │
├─────────────────────────────────────────────────────┤
│              IDT / IRQ / PIC / PIT                  │
│   Interrupts │ Exceptions │ Timer (200Hz)           │
├─────────────────────────────────────────────────────┤
│              Bootloader (boot.asm)                  │
│   Real mode -> Protected mode │ GDT │ Load kernel   │
└─────────────────────────────────────────────────────┘
```

---

## Philosophy

cupid-os deliberately exposes the machine to programs:

- All code runs in ring 0 without a security boundary between applications and the kernel.
- Programs can access hardware directly.
- Programs share the flat address space and can inspect kernel memory.
- The source includes the operating system, desktop, languages, and build tools.

This model is useful for operating-system experiments and low-level study, but it is not safe for untrusted code.

---

## Quick Start

The repository includes a checked static i386 Linux seed and a checked native
PE32 Windows execution seed. Linux runs the static seed directly. Native
Windows builds run output-bearing CupidC, CupidASM, CupidObj, CupidLD, and
CupidDis recipes from the PE32 seed. WSL still runs the Linux seed for
fixed-point reconstruction and the complete published Toolchain contract
cohort. The user ABI, artifact-size, and Toolchain manifest contracts run as
checked native PE files. The last two still inspect Linux publication
provenance.

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install python3 make qemu-system-x86

# Build
make

# Run (with serial output)
make run
```

On Windows, install GNU Make, Python 3, WSL with a Linux distribution, and
QEMU. GCC, Clang, binutils, and NASM are needed only for explicit native
development builds and comparison oracles.

---

## Project Structure

```
cupid-os/
├── boot/
│   └── boot.asm              # Bootloader (real -> protected mode)
├── kernel/
│   ├── core/                  # kmain, panic, process, scheduler, syscall
│   ├── cpu/                   # IDT/IRQ/PIC, FPU/SSE, libm, ksyms
│   ├── fs/                    # VFS, FAT16, ISO9660, ramfs, devfs, homefs
│   ├── gfx/                   # gfx2d, BMP/PNG/JPEG, fontsys, TTF
│   ├── gui/                   # window manager, desktop, terminal, ANSI
│   ├── lang/                  # shell, CupidC, CupidASM, CupidScript, ssh_io
│   ├── network/               # ARP/IP/ICMP/UDP/TCP/DHCP/DNS/sockets/sshd
│   ├── tls/                   # TLS records, handshake, CA bundle
│   ├── crypto/                # AES/ChaCha/RSA/P-256/X25519/X.509 helpers
│   ├── audio/                 # AC97, mixer, OPL3, MIDI/MUS
│   ├── doom/                  # doomgeneric port and platform shim
│   ├── mm/                    # heap, paging, swap
│   ├── smp/                   # AP bringup, LAPIC/IOAPIC, BKL
│   └── usb/                   # UHCI/EHCI, HID, hubs, mass storage
├── drivers/
│   ├── keyboard.cc/h           # PS/2 keyboard (IRQ1)
│   ├── mouse.cc/h              # PS/2 mouse (IRQ12)
│   ├── vga.cc/h                # VBE 640x480 32bpp
│   ├── ata.cc/h                # ATA/IDE disk
│   ├── serial.cc/h            # COM1 serial port
│   ├── timer.cc/h + pit.cc/h   # PIT timer
│   ├── rtl8139.cc/h            # Realtek NIC
│   ├── e1000.cc/h              # Intel NIC
│   └── speaker.cc/h            # PC speaker
├── bin/
│   ├── browser.cc + browser/  # render-pipeline browser
│   ├── ssh.cc, telnet.cc      # remote terminal clients
│   └── *.cc                   # source-backed shell commands
├── docs/plans/                # Design documents
├── link.ld                    # Linker script
├── Makefile                   # Build system
└── LICENSE                    # GPLv3
```

---

## License

cupid-os is released under the **GNU General Public License v3**.
