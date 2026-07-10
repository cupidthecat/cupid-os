# cupid-os

32-bit x86 hobby OS written in C and NASM. Has a graphical desktop, window manager, built-in C compiler, assembler, and scripting language. Runs on real hardware or QEMU. Inspired by TempleOS, OsakaOS, and Unix.

<img src="img/background.png" alt="Desktop" width="700">

<img src="img/freedoom.png" alt="Freedoom" width="700">

<img src="img/web_demo.png" alt="Web Demo" width="700">

<img src="img/fm.png" alt="File manager" width="700">

<img src="img/paint.png" alt="Paint" width="700">

## What it has

- VBE 640x480 32bpp graphics with a window manager, taskbar, and desktop icons
- CupidC, a HolyC-inspired C compiler with JIT and ELF32 AOT output
- Hardware FPU (x87) and SSE/SSE2 with eager FXSAVE context switch
- CupidC float/double scalars and float4/double2 SIMD types with SSE intrinsics
- libm: 25 operations (sqrt, sin, cos, tan, atan, atan2, exp, exp2, log, log2, pow, asin, acos, sinh, cosh, tanh, cbrt, hypot, nextafter, fabs, floor, ceil, round, trunc, fmod + f-variants)
- printf %f, %e, %g, %.Nf with x87-backed int/fractional split
- #NM/#MF/#XF FPU exception handlers with MXCSR/FSW/FCW dump
- CupidASM, an Intel-syntax x86-32 assembler with JIT and ELF32 AOT output
- CupidScript, a shell scripting language with pipes, redirects, and job control
- 100+ embedded source-backed shell programs with history, context-aware tab completion, pipes, and redirects
- VFS with RamFS (/), DevFS (/dev), FAT16 (/disk), and persistent homefs (/home)
- Opt-in swap: handle-based disk-backed memory extension with 4 size
  classes (1K/4K/16K/64K), true LRU eviction, up to 1024 handles over
  a 16 MB FAT16 swap file.
- ISO9660 readonly mount with Rock Ridge (SUSP/RRIP) long filenames,
  multi-mount (up to 4), case-insensitive lookup; mount .iso files
  from the VFS via `mount foo.iso /iso`
- Preemptive round-robin scheduler, up to 32 kernel threads
- Process domains in scheduler and `ps` output (kernel/hosted/external)
- Two-stage bootloader that loads the kernel above 1MB via unreal mode
- GUI apps: Notepad, Terminal with ANSI colors, Paint, Calendar, File Manager
- 64-entry LRU disk block cache with write-back policy
- 6 GUI themes: Windows95, Pastel Dream, Dark Mode, High Contrast, Retro Amber, Vaporwave
- **USB 1.1 + 2.0**: UHCI + EHCI host controllers with HID keyboard/mouse, hub class (depth <= 5), and mass storage (BBB + SCSI)
- **SMP up to 32 CPUs**: ACPI/MP discovery, per-CPU LAPIC timer, big kernel lock, IPI-based reschedule and cross-CPU call
- **Networking**: RTL8139 + E1000 drivers, ARP / IPv4 (with fragmentation + reassembly) / ICMP / UDP / TCP (client + server, retransmit, dynamic window), DHCP with static fallback, DNS resolver, BSD-style sockets, integration test harness (`make test-net`) on both NICs
- **TLS 1.2 + 1.3 client**: full handshake against the public Internet, ChaCha20-Poly1305 + AES-128-GCM AEAD, RSA-PKCS1v15 + RSA-PSS verify, ECDSA-P256, X25519 + P-256 ECDHE, X.509 chain validation against an embedded Mozilla CA bundle, hostname matching
- **HTTP + HTTPS clients**: `curl` (GET/POST, `-o`, `-i`, `-s`, `-X`, `-d`, `-H`, follows http->http redirects), `wget` (auto-named output, `-O`, `-q`, status report)
- **Remote terminals**: in-OS `ssh` client, `telnet` client, and `sshd` server. SSH supports password/keyboard-interactive auth, PTY shells, remote exec, host-key verification, Curve25519/ChaCha20-Poly1305, and terminal window-size updates.
- **Browser**: in-shell graphical browser with a real render pipeline (HTML5 tokenizer, tree builder, CSS lexer + cascade/specificity, CSS variables/calc, `@font-face`, external stylesheets, render tree, BFC + IFC layout, paint), HTTP and HTTPS, link navigation, Backspace history, GET form submit
- **Audio stack**: AC97 PCI codec at 22050 Hz stereo with BDL DMA + IRQ refill, 16-slot s16 software mixer (PCM + streaming), Nuked-OPL3 FM emulator (cycle-accurate, LGPL-2.1), 18-voice MIDI dispatcher with GENMIDI patch loader, percussion, 2-voice patches, pan, sustain pedal
- **DOOM**: Freedoom1/2 WADs auto-discovered from `/disk/wads/`, full SFX through the mixer, music via MUS->MIDI->OPL3 (slot 8), keyboard controls, savegames + `default.cfg` persisted to `/home/doom/`
- Headless build (`make run-headless`): boots straight into shell over COM1/stdio, no VBE. Scriptable through the Python serial/QEMU harnesses in `tools/`.
- PS/2 keyboard and mouse, ATA/IDE disk, RTC, serial, PC speaker drivers
- System clipboard, x86-32 disassembler, BMP / PNG / JPEG image codecs, TrueType font system with bundled Liberation fonts and live `fontswitch`
- Panic backtrace decoded against a kernel symbol table (`addr  function_name+offset` per frame)

## Recent additions

Six feature tracks landed on top of the GUI/compiler/shell base, each with a design spec, implementation plan, and subagent-driven tasks with per-task code review. Specs and plans live under `docs/superpowers/specs/` and `docs/superpowers/plans/`; each track also ships a `wiki/*.md` page and an embedded `cupidos-txt/*.CTXT` doc rendered live by notepad.

- **FPU + SSE + float in CupidC**: x87 init, FXSAVE/FXRSTOR on context switch, MXCSR defaults, `#NM`/`#MF`/`#XF` exception handlers with register dump. CupidC gains `float`, `double`, `float4`, and `double2` with SSE intrinsics; 25-operation libm; printf `%f %e %g %.Nf` with x87-backed int/fractional split.
- **ISO9660 read-only mount**: `mount foo.iso /iso` from any file on the VFS; Rock Ridge (SUSP/RRIP) long filenames; case-insensitive lookup; up to 4 simultaneous mounts.
- **Opt-in handle-based swap**: 4 size classes (1K/4K/16K/64K), true LRU eviction, 1024 handles over a 16 MB FAT-backed swap file; explicit `swap_alloc` / `pin` / `unpin` rather than VM page faults.
- **USB 1.1 + 2.0 stack**: UHCI + EHCI controllers sharing an IRQ dispatcher, device enumeration, HID keyboard and mouse, hub class (depth <= 5), and mass storage (BBB + SCSI) layered under FAT16.
- **SMP up to 32 CPUs**: ACPI/MP discovery, INIT-SIPI-SIPI AP bringup, per-CPU LAPIC timers, IOAPIC routing with the 8259 fully masked, ticket-based big kernel lock, shared runqueue, IPI reschedule / cross-CPU call / panic broadcast.
- **TCP/IP networking**: RTL8139 and E1000 drivers, ARP + IPv4 + ICMP + UDP + TCP (RFC 793 subset, client and server), DHCP client with static fallback, DNS resolver with 16-entry TTL cache, and a 32-slot BSD socket table exposed to both the shell and CupidC. TCP includes per-socket retransmit (stop-and-wait, exponential backoff), dynamic advertised window from actual rx-buffer free space, and listen-queue half-open garbage collection. IP supports fragmentation on send and a 4-slot reassembly table on receive (~64 KB datagrams).
- **TLS 1.2 + 1.3 client**: in-tree implementation of TLS records (ChaCha20-Poly1305, AES-128-GCM), handshake (X25519 / P-256 ECDHE, ECDSA-P256, RSA verify with both PKCS1v15 and PSS), HKDF + SHA-256 + HMAC, ASN.1/DER walker, X.509 v3 parser, and chain validation against an embedded Mozilla CA bundle. Self-test boots through RFC test vectors. Used by `curl https://`, `wget https://`, and the in-shell `browser`.
- **HTTP / HTTPS clients**: `bin/curl.cc` and `bin/wget.cc` are CupidC programs against the Phase-5 socket + TLS bindings. curl supports GET and POST, `-o` / `-i` / `-s` / `-X` / `-d` / `-H`, and follows http->http redirects (capped at 5 hops). wget auto-derives the output filename and reports status code + bytes saved.
- **SSH + Telnet**: `bin/ssh.cc` is a CupidC SSH-2 client with Curve25519 key exchange, ChaCha20-Poly1305 transport, host-key verification for Ed25519/RSA-SHA2/ECDSA-P256, password and keyboard-interactive auth, PTY shell, and remote exec. `bin/telnet.cc` handles IAC negotiation, TTYPE, NAWS, Ctrl-] local commands, and CRLF-safe interactive use. `kernel/lang/ssh_io.c` bridges both clients to the GUI terminal with hidden password input, VT/xterm key translation, resize events, and ANSI rendering.
- **Browser**: `bin/browser.cc` is a render-pipeline browser split across `bin/browser/{css,dom,font_face,image,input,js_dom,js_interp,js_lex,js_parse,layout,main,nav,net,paint,parser,render_tree,style,url,url_hash,util,woff,woff2}.cc`. HTML5 tokenizer + tree builder, CSS lexer with UA + author cascade, specificity, variables/calc, `@font-face`, external `<link rel=stylesheet>`, rounded corners, box shadows, overflow clipping, WOFF1 webfont support, WOFF2 fallback handling, render-tree builder, BFC + IFC line-box layout, painter walks the render tree. HTTP and HTTPS, address bar (Ctrl-L), Backspace history, click navigation, GET form submit, checkboxes/text inputs, and `about:dump`.
- **Font system**: `kernel/gfx/fontsys.c` registers bundled Liberation TTFs, rasterizes UTF-8 text, persists the OS default in `/etc/font.conf`, exposes CupidC bindings, and powers browser text plus the `fontswitch` GUI.
- **Audio stack**: PCI AC97 codec at 22050 Hz stereo, 32-entry BDL ring with IOC IRQ refill (`kernel/audio/ac97.c`). 16-slot s16 software mixer with both PCM and streaming-source playback (`kernel/audio/mixer.c`). Nuked-OPL3 FM emulator vendored under LGPL-2.1 (`kernel/audio/nuked_opl3.c`). MUS-to-MIDI converter (chocolate-doom, GPL-2). 18-voice MIDI dispatcher with GENMIDI patch loader, percussion bank, 2-voice patches, pan, sustain pedal, master-volume re-leveling, single-pass resampler (`kernel/audio/midiopl.c`). `audiotest all` exercises sine, sweep, pan, OPL smoke, and AC97-routed OPL.
- **DOOM**: doomgeneric vendored under `kernel/doom/src/` (BSD/GPL-2). Platform shim wires DG_DrawFrame to the VBE backbuffer, DG_GetKey to the raw-scancode keyboard subscriber ring, and DG_SleepMs/DG_GetTicksMs to the PIT. dglibc supplies the libc subset DOOM needs (heap, string, stdio, fmt, setjmp). SFX hooks the mixer directly; music goes MUS lump -> MIDI -> midiopl -> Nuked-OPL3 -> mixer slot 8. Freedoom WADs are auto-discovered from `/disk/wads/`; savegames + `default.cfg` persist to `/home/doom/` (homefs). Run: `doom` (or `doom -iwad <path>`).
- **Kernel symbols + panic backtrace**: two-pass kernel link generates a `.ksyms` blob from `nm` output and embeds it in the kernel. `kernel_panic` decodes return addresses to `function_name+offset` per frame using `ksym_lookup` + a frame-pointer walker. Graceful fallback to raw addresses if the blob is absent or corrupt.

Built-in CupidC smoke tests exercise each track: `feature12_float`, `feature13_double`, `feature14_simd`, `feature15_libm`, `feature16_asm_fpu` (float/SIMD/libm), `feature17_iso` (ISO9660), `feature18_swap` (swap), `feature19_usb` (USB), `feature20_smp` (SMP), `feature21_net` (TCP client: DNS + connect + HTTP GET), `feature22_net_server` (TCP listen + accept + echo), `feature23_full_access` (network/kernel binding sanity), and `feature24_widetypes` (CupidC C-compatibility spellings and control-flow parsing).

## Feature demo quickstart

After `make run`, these shell commands exercise the major subsystems:

```sh
# 1) Filesystems and persistence
mount
ls /
ls /home
mkdir /home/demo
echo hello > /home/demo/hello.txt
cat /home/demo/hello.txt

# 2) Processing and scheduling
ps
time ls /

# 3) Shell features: pipes, redirects, history
ls /bin | grep gfx > /home/demo/gfx.txt
cat /home/demo/gfx.txt
history

# 4) CupidC JIT and language features
feature1_types
feature3_class
feature10_repl
feature11_ternary

# 5) CupidASM demo execution
as /demos/hello.asm
as /demos/syscall_vfs_extended_demo.asm
as /demos/simd_blur.asm        # SSE + SIMD
as /demos/fpu_kernel.asm       # x87 + SSE assembly

# 6) GUI apps and graphics
terminal
notepad
fm
paint
gfxdemo
gfxtest

# 7) Introspection and debugging tools
sysinfo
registers
memstats
stacktrace
logdump

# 8) Audio/speaker demos
godsong
godspeak

# 9) FPU + SSE float, libm, SIMD
feature12_float
feature13_double
feature14_simd
feature15_libm
feature16_asm_fpu

# 10) ISO9660 read-only mount
mount disk.iso /iso
ls /iso
feature17_iso

# 11) Opt-in handle-based swap
feature18_swap

# 12) USB (run under make run-usb for a populated stick)
feature19_usb

# 13) SMP introspection
smp
feature20_smp

# 14) Networking
ifconfig
arp
ping 10.0.2.2
resolve example.com
netstat
feature21_net           # TCP client: DNS + connect + HTTP GET
feature22_net_server    # TCP server: listen + accept + echo
feature23_full_access   # kernel binding sanity checks
cupidfetch              # one-shot HTTP GET
sshd                    # start SSH server on port 22
# host, with make run-ssh: ssh -p 2222 root@127.0.0.1
ssh user@host           # in-OS SSH client
telnet telehack.com     # in-OS Telnet client

# 15) HTTP / HTTPS clients
curl http://example.com/
curl -i https://www.iana.org/
curl -d test=42 -X POST http://httpbin.org/post
wget -O /home/page.html http://example.com/

# 16) Graphical browser (HTTP + HTTPS)
browser http://example.com/
browser https://www.iana.org/
browser about:dump

# 17) Fonts, audio + DOOM
fontswitch                 # choose system TTF/bitmap font
audiotest all              # sine + sweep + pan + OPL smoke + AC97-routed OPL
volume 50                  # mixer master volume
doom                       # auto-finds Freedoom WAD in /disk/wads/
doom -iwad /home/my.wad    # alternate IWAD
```

## Philosophy

All code runs in ring 0. No privilege separation, no virtual memory isolation, no artificial restrictions. User programs have direct hardware access, full memory access, and can call any kernel function. The point is transparency and learning, not security. If you want to understand what your computer is actually doing, this is the kind of environment that lets you do that.

The design borrows from TempleOS (single address space, built-in compiler, bare metal), Unix (VFS, shell, process model), and OsakaOS (aesthetics).

## Building

Linux image builds default to GCC for C compilation and require NASM, GCC with
32-bit support (including its native linker backend), binutils `nm`, Python 3,
and GNU Make. QEMU is required only to run the image or execute emulator tests.
CupidLD and CupidObj perform every OS/user ELF link and object/binary transform;
the GCC driver still links the temporary hosted Cupid tools:

```bash
sudo apt-get install nasm gcc gcc-multilib python3 make qemu-system-x86
```

Native Windows image builds default to Clang for C compilation. Install GNU
Make, Python 3, NASM, and LLVM (`clang` and `llvm-nm`), then build from
PowerShell or another native Windows shell. Install QEMU for runtime/tests.
No standalone LLVM ELF-linker or `objcopy` command produces an OS/user artifact.
Clang still needs its native linker backend to build the temporary hosted Cupid
tools:

```powershell
choco install make python nasm llvm qemu
```

`mtools` is no longer required for the normal build; the Makefile uses
`tools/hostbuild.py` to create and update the FAT16 image on both platforms.
On Windows, QEMU defaults to no host audio so booting does not depend on a
working DirectSound device; use `make QEMU_AUDIODEV=dsound,id=speaker run` to
enable DirectSound.

```bash
make               # builds cupidos.img
make run           # boots in QEMU with SDL graphics
make run-log       # boots and writes serial output to debug.log
make run-headless  # boots straight into shell over stdio (scriptable tests)
make run-usb       # UHCI + EHCI + kbd/mouse + FAT16 USB stick
make run-net       # boots with RTL8139 user-mode networking
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
| `make run-headless` | Boot headless shell over stdio (no VBE), scriptable |
| `make run-usb` | Boot with UHCI + EHCI + a 32 MB FAT16 USB stick |
| `make run-net` | Boot with RTL8139 user-mode networking and host port 8080 forwarded |
| `make run-net-e1000` | Boot with E1000 user-mode networking |
| `make test-net` | Headless networking integration tests (rtl8139 + e1000) |
| `make test-net-quick` | Same as `test-net` for one NIC only |
| `make test` | Run all deterministic host-side unit tests |
| `make bootstrap-audit` | Regenerate the checked active-source/build-feature inventory |
| `make check-bootstrap-audit` | Reject audit drift or a failing graph contract |
| `make bootstrap-baseline` | Build committed HEAD twice in isolation and record host-toolchain evidence |
| `make stage-wads` | Copy Freedoom WADs from host into FAT16 partition |
| `make sync-demos` | Copy demos/*.asm into the FAT16 partition |
| `make clean` | Remove object files, keep cupidos.img |
| `make clean-image` | Remove cupidos.img only |
| `make distclean` | Remove everything including cupidos.img |

`make bootstrap-baseline` records tool versions and hashes, runs the host tests plus explicit CupidC/CupidASM GUI smokes, and compares two clean builds artifact by artifact. See `docs/bootstrap/BASELINE.md` for the evidence contract. Networking integration remains available through `make test-net-quick` and `make test-net`.

### Copying files into the disk image

CupidOS now mounts FAT16 at `/disk` and mounts persistent `homefs` at `/home`.

- `/disk` is the raw FAT16 partition in `cupidos.img`.
- `/home` is `homefs`, serialized into `HOMEFS.SYS` on FAT16.
- On first boot without `HOMEFS.SYS`, `homefs` imports existing FAT16 files.

The FAT16 partition sits at byte offset 8388608 (16384 * 512) inside `cupidos.img`. Use the portable host helper to put files in the FAT16 backend:

```bash
python3 tools/hostbuild.py stage --image cupidos.img --fat-start-lba 16384 myfile.txt:/myfile.txt
```

On Windows, use `python` instead of `python3`. If you prefer `mtools`, point it
at the same offset:

```bash
mcopy -o -i cupidos.img@@8388608 myfile.txt ::/myfile.txt
mdir  -i cupidos.img@@8388608 ::/
```

If you change `FAT_START_LBA` in the Makefile, recalculate: offset =
`FAT_START_LBA * 512`.

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
  boot/                  two-stage BIOS bootloader
  kernel/                kernel source, organised by subsystem:
    audio/                 AC97 driver, mixer, OPL3 synth, MIDI/MUS
    core/                  kmain, panic, process, scheduler,
                           syscall, app_launch, types, string
    cpu/                   IDT/IRQ/PIC, FPU, libm, math, simd, ksyms
    crypto/                AES, ChaCha20, SHA, HMAC, HKDF, RSA,
                           x25519, P-256, ECDSA, ASN.1, X.509
    doom/                  vendored doomgeneric + dglibc shim
    fs/                    VFS, FAT16, ISO9660, ramfs, devfs,
                           homefs, loopdev, blockcache, blockdev
    gfx/                   gfx2d, BMP/PNG/JPEG, font, graphics
    gui/                   gui widgets, desktop, ed, notepad,
                           terminal app, ANSI
    lang/                  CupidC compiler, CupidASM, CupidScript,
                           shell, exec, godspeak, dis
    mm/                    memory, paging, swap, swap_disk
    network/               ARP, IP, ICMP, UDP, TCP, DHCP, DNS,
                           sockets, net_if
    smp/                   SMP, MP tables, LAPIC/IOAPIC, BKL,
                           per-CPU, ACPI, AP trampoline
    tls/                   TLS 1.2/1.3 record + handshake + CA
    usb/                   USB core, UHCI, EHCI, HID, hub, MSC
    util/                  calendar, generated *_programs_gen.c
  drivers/               hardware drivers: ATA, keyboard, mouse,
                         PIT, RTC, serial, speaker, timer, VGA,
                         PCI, RTL8139, E1000
  bin/                   104 CupidC programs + 22 browser fragments
  demos/                 22 CupidASM demo/include programs
  user/                  example ELF user programs + cupid.h
  wiki/                  documentation (28 Markdown files)
  docs/superpowers/      additional project docs
  cupidos-txt/           embedded rich-text docs (.CTXT format)
  img/                   screenshots
  link.ld                linker script
  Makefile
```

All `kernel/<subdir>/` and `drivers/` are on the include path, so
sources use bare `#include "foo.h"` regardless of the file's
location.

---

## Bootloader (boot/boot.asm)

Two stages, about 4KB total.

Stage 1 lives in the MBR at 0x7C00. It loads stage 2 (4 sectors from LBA 1) to 0x7E00 using INT 0x13 EDD, then jumps there.

Stage 2 does the real work:
- Enables the A20 gate
- Switches to unreal mode so it can write above 1MB while still in 16-bit real mode
- Probes VBE and sets mode 0x118 (640x480x32bpp linear framebuffer)
- Loads the kernel in 127-sector chunks from LBA 5 to physical address 0x100000
- Sets up 4KB page tables, identity-mapping 0 to 512MB
- Loads the GDT, enables protected mode, jumps to `_start`

Disk layout:
```
LBA 0       MBR / Stage 1
LBA 1-4     Stage 2
LBA 5-16383 Kernel binary area (up to ~8MB)
LBA 16384+  FAT16 partition (mounted as /disk)
           homefs persistent container (HOMEFS.SYS), mounted as /home
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
| `memory.c/.h` | Physical memory manager, bitmap allocator over 512MB, kernel heap |
| `paging.c` | Page tables, identity-mapped address space |

The kernel heap uses a bump allocator with a free list. Everything runs at ring 0 in a flat 32-bit identity-mapped address space. The PMM manages 512MB, starts with a 256MB heap, and reserves the 2MB kernel stack at 0x00B00000-0x00D00000.

### Processes

| File | What it does |
|------|-------------|
| `process.c/.h` | PCB, process list, round-robin scheduler |
| `context_switch.asm` | Saves EBX/ESI/EDI/EBP/EFLAGS, swaps ESP/EIP |

Up to 32 threads. Scheduler is preemptive, driven by IRQ0 at 200Hz (5ms slices). Process states are READY, RUNNING, BLOCKED, and TERMINATED. Core primitives are `process_create()`, `process_yield()`, `process_exit()`, and `process_kill()`; detached terminated PCBs are reclaimed by the quiescent reaper.

### Filesystem

| File | What it does |
|------|-------------|
| `vfs.c/.h` | VFS layer: open, read, write, close, seek, stat, readdir |
| `vfs_helpers.c/.h` | read_all(), write_all(), read_text(), write_text() |
| `ramfs.c/.h` | In-memory root filesystem, populated at boot with programs/docs/demos |
| `devfs.c/.h` | /dev entries: null, zero, console, serial, random |
| `fat16.c/.h` | FAT16: MBR parsing, cluster chains, file read/write/create |
| `fat16_vfs.c/.h` | FAT16 to VFS adapter |
| `homefs.c/.h` | Persistent logical filesystem for /home, serialized to HOMEFS.SYS |
| `blockdev.c/.h` | Block device abstraction |
| `blockcache.c/.h` | 64-entry LRU sector cache, write-back, flushes periodically |

Filesystem layout at runtime:
```
/           RamFS, ephemeral, rebuilt each boot
  bin/      built-in CupidC programs
  demos/    CupidASM demo programs
  docs/     documentation
  dev/      DevFS: null, zero, console, serial, random
  disk/     FAT16 raw partition view
  home/     homefs persistent user data (backed by HOMEFS.SYS on FAT16)
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

Themes include Windows95, Pastel Dream, Dark Mode, High Contrast, Retro Amber, and Vaporwave. Theme files can be saved and loaded from disk.

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
- Inline assembly, structs/classes, floats/SIMD, constant expressions, labels/goto, and full ring-0 kernel bindings
- Limits: 1MB code, 8MB data/string storage, 1024 functions, 4096 symbols per unit

**CupidASM** (`as*.c`) is an Intel-syntax x86-32 assembler:
- Expanded x86-32 integer/control-flow/system/FPU/SSE/atomic coverage
- JIT and AOT (ELF32) modes
- Directives: `%include`, reserve aliases, `times`, alignment
- Forward references, up to 8192 labels
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
| `exec.c/.h` | Fixed-address ELF32/CUPD loader: validated segments, staged ELF loading, BSS zeroing, and image/lease lifetime transfer |
| `syscall.c/.h` | Syscall table passed to ELF programs as a struct of function pointers |

### Shell (kernel/lang/shell.c)

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

104 top-level CupidC programs are embedded in RamFS at boot, all directly runnable from the shell. Browser support modules under `bin/browser/*.cc` are embedded too, but are included by `browser.cc` rather than launched directly.

| Category | Programs |
|----------|---------|
| Core shell/filesystem | cat, cd, cp, find, grep, head, ls, mkdir, mount, mv, pwd, rm, rmdir, sort, sync, tail, touch, wc |
| Text/console | clear, echo, ed, help, history, printc, resetcolor, setcolor |
| Process/system | date, kill, ps, reboot, spawn, sysinfo, time, yield |
| Introspection/debug | cachestats, crashtest, logdump, loglevel, registers, stacktrace |
| Memory tools | memcheck, memdump, memleak, memstats |
| GUI/graphics apps | bgstudio, bmptest, browser, ctxt, fm, fontswitch, gfxdemo, gfxgui_test, gfxtest, notepad, paint, terminal |
| Audio/speech/media | audiotest, doom, godsong, godspeak, volume |
| CupidC language tests | cupidc_test1-5, feature1_types, feature2_top_level, feature3_class, feature4_forward_calls, feature5_print_builtin, feature6_exe, feature7_new_del, feature8_reg_noreg, feature9_abs_addr, feature10_repl, feature11_ternary |
| FPU/SSE/libm tests | feature12_float, feature13_double, feature14_simd, feature15_libm, feature16_asm_fpu, fp_drill |
| Subsystem smoke tests | feature17_iso (ISO9660), feature18_swap (swap), feature19_usb (USB), feature20_smp (SMP), feature21_net (TCP client), feature22_net_server (TCP server), feature23_full_access, feature24_widetypes |
| Networking utilities | arp, curl, cupidfetch, ifconfig, netstat, ping, resolve, ssh, telnet, wget |
| Text/documentation viewers | auto, bible, oracle |
| Test programs | dglibc_test, kbdsub_test, test, test_fpaug, test_print |

---

## Assembly demos (demos/)

22 CupidASM programs embedded in RamFS. Run with `as <name>.asm` from the shell:

hello, loop, fibonacci, factorial, bubblesort, stack, data, math, include_feature, include_helper, jcc_aliases, asm_compat_reserve, reserve_directives, fs_syscalls, syscall_table_demo, syscall_vfs_extended_demo, parity_core, parity_diag, parity_gfx2d, parity_priv, fpu_kernel, simd_blur

---

## User programs (user/)

The user/ directory has example ELF32 programs (hello.c, cat.c, ls.c) and user/cupid.h which defines the syscall table ABI. Use cupid.h when writing programs that get compiled to ELF and loaded by the kernel.

---

## Memory layout

```
0x007C00            Stage 1 bootloader (512 bytes)
0x007E00            Stage 2 bootloader (2KB)
0x100000            Kernel start (_start)
                    .text, .rodata, .data
                    .bss
0x00B00000-0x00D00000 Kernel stack (2MB, grows down; 16-byte guard)
0x00D00000-0x00F00000 External ELF arena (exclusive fixed-address lease)
0x01000000-0x01900000 CupidC JIT/AOT region (1MB code + 8MB data)
0x01A00000-0x01C00000 CupidASM JIT/AOT region (1MB code + 1MB data)
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
- PIT runs at 200Hz, giving 5ms scheduler slices
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
- Linux: GCC with 32-bit support (gcc-multilib on 64-bit hosts), its native linker backend, and binutils `nm`
- Windows: LLVM (`clang`, its native linker backend, `llvm-nm`)
- Python 3
- GNU Make
- QEMU (`qemu-system-i386`, runtime/testing only)
- mtools (mcopy, mdir) optional for manual FAT16 image inspection/copying
- DOOM WADs (optional): the build picks up `freedoom1.wad` /
  `freedoom2.wad` from `/usr/share/games/doom/` on the build host and
  auto-copies them into `/disk/wads/` inside the image. On Ubuntu/Debian:
  ```
  sudo apt install freedoom
  ```
  Or drop any DOOM-format IWAD (`doom.wad`, `doom2.wad`, ...) into
  `/usr/share/games/doom/` manually before running `make`. If no WADs
  are present the build still succeeds, but the `doom` shell command
  will report no IWAD found.

---

## License

GNU General Public License v3.0

Built in dedication to Terry A. Davis and TempleOS.
