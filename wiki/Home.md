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

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install gcc gcc-multilib binutils python3 make qemu-system-x86

# Build
make

# Run (with serial output)
make run
```

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
│   ├── keyboard.c/h           # PS/2 keyboard (IRQ1)
│   ├── mouse.c/h              # PS/2 mouse (IRQ12)
│   ├── vga.c/h                # VBE 640x480 32bpp
│   ├── ata.c/h                # ATA/IDE disk
│   ├── serial.c/h             # COM1 serial port
│   ├── timer.c/h + pit.c/h    # PIT timer
│   ├── rtl8139.c/h            # Realtek NIC
│   ├── e1000.c/h              # Intel NIC
│   └── speaker.c/h            # PC speaker
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
