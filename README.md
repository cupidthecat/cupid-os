# cupid-os

Cupid OS is a 32-bit x86 hobby OS written in Cupid C and Cupid ASM. It has a graphical desktop, window manager, built-in C compiler, assembler, and scripting language. It runs on real hardware and in QEMU. The design draws from TempleOS, OsakaOS, and Unix.

<img src="img/background.png" alt="Desktop" width="700">

<img src="img/freedoom.png" alt="Freedoom" width="700">

<img src="img/web_demo.png" alt="Web Demo" width="700">

<img src="img/fm.png" alt="File manager" width="700">

<img src="img/paint.png" alt="Paint" width="700">

## Current features

- VBE 640x480 32bpp graphics with a window manager, taskbar, and desktop icons
- CupidC, a HolyC-inspired C compiler with JIT and ELF32 AOT output
- Hardware FPU (x87) and SSE/SSE2 with eager FXSAVE context switch
- CupidC float/double scalars, exact unary signs, comparisons, control-flow
  truth, prefix/postfix updates, and float4/double2 SIMD types with SSE intrinsics
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
- 7 GUI themes: Windows95, Pastel Dream, Dark Mode, High Contrast, Retro Amber, Temple, and Vaporwave
- USB 1.1 and 2.0 through UHCI and EHCI host controllers, with HID keyboard and mouse support, hubs up to depth 5, and BBB/SCSI mass storage
- SMP for up to 32 CPUs, with ACPI/MP discovery, per-CPU LAPIC timers, a big kernel lock, reschedule IPIs, and cross-CPU calls
- RTL8139 and E1000 networking with ARP, IPv4 fragmentation and reassembly, ICMP, UDP, client and server TCP, DHCP with static fallback, DNS, BSD-style sockets, and the two-NIC `make test-net` harness
- TLS 1.2 and 1.3 client handshakes with public servers using ChaCha20-Poly1305 or AES-128-GCM, RSA-PKCS1v15 and RSA-PSS verification, ECDSA-P256, X25519 or P-256 ECDHE, X.509 parsing, hostname and time checks, and best-effort chain checks against an embedded Mozilla CA bundle. The current chain policy remains lenient when a root or signature algorithm is unavailable.
- HTTP and HTTPS through `curl` and `wget`; `curl` supports GET, POST, common request flags, and bounded HTTP-to-HTTP redirects, while `wget` supports `-O` and `-q`, derives output names, and reports status
- In-OS `ssh` and `telnet` clients plus an `sshd` server. SSH supports password and keyboard-interactive authentication, PTY shells, remote execution, host-key verification, Curve25519/ChaCha20-Poly1305, and terminal resizing
- A graphical shell browser with HTML5 tokenization and tree building, CSS cascade and specificity, variables and `calc`, web fonts, block and inline layout, HTTP and HTTPS, navigation history, and GET forms
- AC97 audio at 22050 Hz stereo, a 16-slot signed 16-bit mixer, the cycle-accurate LGPL-2.1 Nuked-OPL3 emulator, and an 18-voice MIDI dispatcher with percussion, two-voice patches, pan, and sustain
- DOOM with automatic Freedoom1/2 discovery under `/disk/wads/`, mixer-backed sound effects, MUS-to-MIDI OPL3 music in slot 8, keyboard controls, and persistent saves and `default.cfg` under `/home/doom/`
- Headless build (`make run-headless`): boots straight into shell over COM1/stdio, no VBE. Scriptable through the Python serial/QEMU harnesses in `tools/`.
- PS/2 keyboard and mouse, ATA/IDE disk, RTC, serial, PC speaker drivers
- System clipboard, x86-32 disassembler, BMP / PNG / JPEG image codecs, TrueType font system with bundled Liberation fonts and live `fontswitch`
- Panic backtrace decoded against a kernel symbol table (`addr  function_name+offset` per frame)

## Recent additions

Recent subsystem work is summarized below. Detailed pages live under `wiki/`, and several also have embedded `cupidos-txt/*.CTXT` manuals that Notepad renders in the running OS.

- CupidC initializes x87 and SSE state, saves it with FXSAVE/FXRSTOR during context switches, sets MXCSR defaults, and dumps registers from the `#NM`, `#MF`, and `#XF` handlers. The language supports `float`, `double`, `float4`, and `double2`, along with SSE intrinsics, a 25-operation libm, and x87-backed integer/fractional splitting for `printf` formats `%f`, `%e`, `%g`, and `%.Nf`.
- ISO9660 images mount read-only from any VFS file with `mount foo.iso /iso`. The implementation handles Rock Ridge long names, case-insensitive lookup, and up to four simultaneous mounts.
- Opt-in swap uses four allocation classes (1K, 4K, 16K, and 64K), true LRU eviction, and 1,024 handles over a 16 MB FAT-backed file. Callers use `swap_alloc`, `pin`, and `unpin` explicitly rather than relying on virtual-memory page faults.
- UHCI and EHCI share an IRQ dispatcher and provide enumeration, HID keyboard and mouse support, hubs up to depth 5, and BBB/SCSI mass storage beneath FAT16.
- SMP supports up to 32 CPUs through ACPI/MP discovery and INIT-SIPI-SIPI startup. It uses per-CPU LAPIC timers, IOAPIC routing with the 8259 fully masked, a ticket-based big kernel lock, a shared run queue, and IPIs for rescheduling, cross-CPU calls, and panic broadcasts.
- The TCP/IP stack supports RTL8139 and E1000 devices, ARP, IPv4, ICMP, UDP, a client and server subset of RFC 793 TCP, DHCP with static fallback, DNS with a 16-entry TTL cache, and a 32-slot BSD socket table shared by the shell and CupidC. TCP uses per-socket stop-and-wait retransmission with exponential backoff, advertises the actual receive-buffer space, and collects abandoned half-open connections. IPv4 fragments outgoing packets and keeps four reassembly slots for datagrams up to about 64 KB.
- The in-tree TLS 1.2 and 1.3 client implements ChaCha20-Poly1305 and AES-128-GCM records, X25519 and P-256 ECDHE, ECDSA-P256, RSA-PKCS1v15 and RSA-PSS verification, HKDF, SHA-256, HMAC, ASN.1/DER parsing, and X.509 v3 parsing with hostname, time, and best-effort chain checks against an embedded Mozilla CA bundle. The chain checker is still lenient when it cannot find a root or implement a signature algorithm. A boot self-test runs RFC vectors. `curl`, `wget`, and the shell browser use this implementation for HTTPS.
- `bin/curl.cc` and `bin/wget.cc` are CupidC clients built on the socket and TLS bindings. `curl` supports GET, POST, `-o`, `-i`, `-s`, `-X`, `-d`, and `-H`, with HTTP-to-HTTP redirects capped at five hops. `wget` supports `-O` and `-q`, derives its output filename, and reports the response status and saved byte count.
- `bin/ssh.cc` is an SSH-2 client with Curve25519 key exchange, ChaCha20-Poly1305 transport, Ed25519, RSA-SHA2, and ECDSA-P256 host-key verification, password and keyboard-interactive authentication, PTY shells, and remote execution. `bin/telnet.cc` handles IAC negotiation, TTYPE, NAWS, Ctrl-] commands, and CRLF-safe interactive sessions. `kernel/lang/ssh_io.cc` connects both clients to the GUI terminal and handles hidden passwords, VT/xterm keys, resize events, and ANSI output.
- `bin/browser.cc` drives a browser assembled from `bin/browser/{css,dom,font_face,image,input,js_dom,js_interp,js_lex,js_parse,layout,main,nav,net,paint,parser,render_tree,style,url,url_hash,util,woff,woff2}.cc`. It has an HTML5 tokenizer and tree builder, a CSS lexer with user-agent and author cascades, specificity, variables, `calc`, external stylesheets, `@font-face`, WOFF1 support, WOFF2 fallback handling, a render-tree builder, block and inline formatting, clipping, rounded corners, box shadows, and a painter that walks the render tree. The UI supports HTTP and HTTPS, Ctrl-L for the address bar, Backspace history, link navigation, GET forms, checkboxes, text inputs, and `about:dump`.
- `kernel/gfx/fontsys.cc` registers the bundled Liberation fonts, rasterizes UTF-8 text, stores the default in `/etc/font.conf`, exposes CupidC bindings, and supplies text to the browser and `fontswitch`.
- `kernel/audio/ac97.cc` drives the PCI AC97 codec with a 32-entry BDL ring and IOC refills. `kernel/audio/mixer.cc` provides 16 signed 16-bit slots for PCM and streamed sources. The repository also carries the LGPL-2.1 Nuked-OPL3 emulator, the GPL-2 chocolate-doom MUS-to-MIDI converter, and an 18-voice dispatcher in `kernel/audio/midiopl.cc`. The dispatcher loads GENMIDI patches and handles the percussion bank, two-voice patches, pan, sustain, master-volume re-leveling, and single-pass resampling. `audiotest all` runs the sine, sweep, pan, OPL, and AC97-routed OPL checks.
- The vendored doomgeneric core lives under `kernel/doom/src/` with BSD and GPL-2 components. The platform shim sends `DG_DrawFrame` to the VBE back buffer, connects `DG_GetKey` to the raw-scancode subscriber ring, and implements `DG_SleepMs` and `DG_GetTicksMs` with the PIT. `dglibc` supplies the required heap, string, stdio, formatting, and setjmp routines. Sound effects go straight to the mixer, while music passes from MUS to MIDI, `midiopl`, Nuked-OPL3, and mixer slot 8. The shell command `doom` finds Freedoom WADs under `/disk/wads/`; `doom -iwad <path>` selects another IWAD. Savegames and `default.cfg` live in homefs under `/home/doom/`.
- A two-pass kernel link generates and embeds a `.ksyms` blob. The generator reads private snapshots of the pass-one kernel and CupidDis, rejects malformed symbol output or live input drift, and replaces the generated `.cc` source only after validation. Checked-seed CupidC compiles that source. `kernel_panic` uses `ksym_lookup` and a frame-pointer walk to print `function_name+offset` for each return address. It prints raw addresses if the blob is missing or corrupt.

Built-in CupidC smoke tests exercise each track: `feature12_float`,
`feature13_double` (including runtime unary signs, all six scalar floating
comparisons, signed zero, NaN behavior, a type error, and recovery),
`feature14_simd`, `feature15_libm`, `feature16_asm_fpu`
(float/SIMD/libm), `feature17_iso` (ISO9660), `feature18_swap` (swap),
`feature19_usb` (USB), `feature20_smp` (SMP), `feature21_net` (TCP client:
DNS + connect + HTTP GET), `feature22_net_server` (TCP listen + accept +
echo), `feature23_full_access` (network/kernel binding sanity),
`feature24_widetypes` (CupidC C-compatibility spellings and control-flow
parsing), and `feature25` (nearest-loop continuation, saved-selector cleanup,
and parser-depth rejection and recovery).

[ADR 0189](docs/adr/0189-preserve-floating-values-in-private-cupidc-unary-signs.md)
records the private compiler's typed unary-sign behavior and its guest
recovery proof. The frontier permits the deliberate non-arithmetic operand
diagnostic only once and only inside the completed `feature13_double.cc`
command. Stale and repeated copies still fail the boot. A host oracle compiles
the active emitter functions, checks their instruction bytes, and interprets
those bytes against ordinary values, signed zero, and NaN payloads.

[ADR 0192](docs/adr/0192-compare-floating-scalars-in-private-cupidc.md)
records private scalar comparison behavior. Matching widths use `UCOMISS` or
`UCOMISD`, mixed widths compare as `double`, and explicit parity checks make
only `!=` true for NaN. The feature13 frontier requires ordered, mixed-width,
signed-zero, and unordered results before JIT completion.

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
feature25

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

All code runs in ring 0. There is no privilege separation or virtual-memory isolation. User programs can access hardware and memory directly and call any kernel function. The goal is transparency and learning rather than security.

The design borrows from TempleOS (single address space, built-in compiler, bare metal), Unix (VFS, shell, process model), and OsakaOS (aesthetics).

## Building

The normal image build compiles all 238 checked-in C roots with the verified
CupidC seed. Linux runs that static i386 seed directly; Windows runs it
through WSL. CupidASM assembles every active OS assembly input. CupidLD,
CupidObj, and CupidDis own every OS link, object or binary transformation, and
kernel-symbol read. GCC, Clang, NASM, `objcopy`, and `nm` do not produce a
normal image artifact.

Python 3 and GNU Make orchestrate the build. QEMU is needed only to run the
image or execute emulator tests. The normal Linux build requirements are:

```bash
sudo apt-get install python3 make qemu-system-x86
```

A host C toolchain is needed only for explicit native Toolchain commands and
comparison oracles. Install those optional tools separately:

```bash
sudo apt-get install gcc gcc-multilib binutils nasm
```

On Windows, install GNU Make, Python 3, WSL, and QEMU, then build from
PowerShell or another native Windows shell:

```powershell
choco install make python qemu
wsl --install
```

LLVM is needed only for the native Toolchain contracts and the optional
native user equivalence check. It is not an image or normal user-program code
generator. `llvm-nm` is an optional comparison oracle:

```powershell
choco install llvm
```

`mtools` is no longer required for the normal build; the Makefile uses
`tools/hostbuild.py` to create and update the FAT16 image on both platforms.
The same helper now authors the tracked ISO9660 test fixture, so `mkisofs`,
`genisoimage`, and `xorrisofs` are not build prerequisites.
NASM is also not required; install it only to run the optional
`make nasm-assembly-oracle` comparison suite.
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
| `make test-user-native-windows-equivalence` | Compare all native Windows user objects and executables with the checked seed |
| `make bootstrap-audit` | Regenerate the checked active-source/build-feature inventory |
| `make check-bootstrap-audit` | Reject audit drift or a failing graph contract |
| `make bootstrap-baseline` | Build committed HEAD twice in isolation and record host-toolchain evidence |
| `make bootstrap-host-comparison` | Compare the checked Windows/Linux baseline evidence and write the cross-host record |
| `make check-bootstrap-host-comparison` | Reject stale, failed, or structurally incomparable checked host evidence |
| `make stage-wads` | Copy Freedoom WADs from host into FAT16 partition |
| `make sync-demos` | Copy demos/*.asm into the FAT16 partition |
| `make clean` | Remove object files, keep cupidos.img |
| `make clean-image` | Remove cupidos.img only |
| `make distclean` | Remove everything including cupidos.img |

`make bootstrap-baseline` records tool versions and hashes, runs the host tests plus explicit CupidC/CupidASM GUI smokes, and compares two clean builds artifact by artifact across the root, user, and hosted-toolchain roots. Checked revision `1e079d1` reproduces all 447 artifacts independently on Windows Clang/LLVM and Linux GCC/binutils; `make check-bootstrap-host-comparison` verifies the shared logical cohort and behavior/quality contract without requiring cross-toolchain byte equality. See `docs/bootstrap/BASELINE.md` for the evidence contract. Networking integration remains available through `make test-net-quick` and `make test-net`.

The network tests use only Python's standard library. They give QEMU the
same 512 MiB that Cupid OS identity-maps, drive the headless shell over a
local TCP serial channel, retain QEMU startup diagnostics, and stop any guest
wait when a fatal kernel marker appears. Their Ethernet PCAP reader
correlates complete ARP, DHCP, ICMP, TCP handshake, and bidirectional
teardown exchanges without `pexpect` or Scapy.

### Self-hosting compiler status

The normal image build uses the checked CupidC seed for 238 checked-in
objects and the generated kernel symbol object. The strict non-Doom kernel
and driver frontier covers 155 of those checked-in roots. All normal sources
use `.cc`. The five shared Toolchain roots also belong to the 19-source i386
Linux fixed-point plan. Their native GCC and Clang rules select C explicitly
with `-x c`. A source moves to `.cc` only when its checked recipe, object,
image, and runtime path pass together.

Typed null conversion, external-array address decay, GNU assembly operands,
the per-CPU GS load, port I/O, integer atomics, and the wider shared C path
cover this cohort. The checked seed builds `kernel/gfx/jpeg.cc` and
`kernel/gfx/glyph_raster.cc` from closed four-header snapshots. It also
builds `kernel/cpu/libm.cc` from its exact source, `types.h`, and `libm.h`.
Their 21,120-byte, 11,744-byte, and 16,164-byte objects repeat exactly, and
poisoned-host builds cannot fall back to GCC or Clang.

The same wrapper builds `kernel/core/kernel.cc` from its 63-header recursive
closure, `kernel/cpu/simd.cc` from seven headers, and
`kernel/core/string.cc` from `string.h` and `types.h`. Their checked objects
are 25,920, 8,768, and 14,460 bytes. Poisoning every host compiler and
assembler command leaves all three production recipes on CupidC, and
CupidDis accepts each relocatable object.

The CSPRNG emits RDTSC, CPUID, RDRAND, and SETC through Cupid's x86 model
while preserving EBX. Every object is validated as an i386 ELF32 relocatable
before publication. A valid data-only object may omit `.text`; its remaining
sections and symbols still receive the full bounds checks. The strict kernel
frontier compiles all 155 checked-in sources twice. The complete two-pass
frontier passes against a 445-file snapshot with SHA-256
`e28b1024edc5361d99583f79f65ce43690ebc873f04b568837f57f8af5df5db7`.
Both 155-object passes are byte-identical; each totals 3,717,856 bytes. The
frontier publisher retries a short permission-style directory lock with five
bounded delays. A persistent lock or any other filesystem error leaves the
frontier unpublished. Input discovery also skips hidden paths under the active
include roots. Private compiler staging headers therefore cannot appear as
repository drift when a checked build runs at the same time. The combined
155-root graph carries a byte-fixed baseline JPEG in its ISO runtime fixture.
Strong four-vCPU runs pass with e1000 and RTL8139 networking. They start CPUs
1 through 3, report four of four, seed from RDRAND, pass all 62 crypto,
ASN.1, and X.509 checks, exercise USB storage and audio output, reach the
desktop and terminal, and complete in-OS CupidC execution.
The command gate now requires all 22 `feature15_libm.cc` checks, the exact
zero-failure summary, and `PASS feature15_libm`.

The generated ramfs, homefs, and demo installation tables are emitted as
`.cc` sources and compiled by the checked CupidC seed. The separate `user/`
build uses CupidC for `hello.cc`, `ls.cc`, and `cat.cc`, then CupidLD places
each executable in the fixed external arena. Linux runs the checked i386
Linux seed directly, while Windows runs it through WSL. The normal user build
does not prepare a native compiler or linker. An optional Windows frontier
runs private snapshots of the native hosted drivers and requires all six
outputs to match the checked seed. Before compiling, the user build checks
the kernel and public syscall declarations as one i386 ABI. The checker
captures all six declaration inputs and rechecks their exact bytes before
success. The contract is version 5 with 103 fields in 412 bytes, a 136-byte
directory entry, an 8-byte file status record, and 101 reviewed function
providers. Both execution paths freeze their complete source and control
inputs, validate the resulting ELF files, and publish only complete
artifacts. Poisoned-host tests prove that the normal user build cannot fall
back to GCC, Clang, `ld`, or `cc`. The optional native drivers still need
Clang and its Windows linker, so this is not a native Windows fixed point.
`user/build/` contains local generated outputs and is ignored by Git.

The external-program gate boots `hello`, `ls`, and `cat` from separate
private image copies. Serial
events bind each syscall to the loaded PID and record printable content by
byte count and FNV-1a fingerprint instead of copying caller text into the log.
The checks cover numeric output and root-directory reads. The cat gate also
copies a fixed FAT fixture over `/home/readme.txt`, preserving the program's
normal path and the selected image. Every program must produce a matching
process exit.

ADRs 0115, 0123, and 0127 transferred 29 source-driven roots into the normal
CupidC build. The later transfer added IDT and PIC setup, paging, LAPIC control,
process management, panic handling, and the in-kernel CupidASM and CupidC
adapters. It emits weak ELF symbols and named sections, records `unused`
declarations, preserves typed static null pointers, recognizes known-true
loops as non-fallthrough, lowers comma expressions in source order, and keeps
all 32 bits through represented function-pointer casts. Exact output-only
assembly forms snapshot general registers, ESP, EBP, the caller return slot,
or EFLAGS into one four-byte destination. Those files were renamed to `.cc`
with their ownership transfer. ADR 0124 renames another 111 exclusively
CupidC-owned roots to `.cc`. ADR 0126 completes the 19-source fixed-point
rename and proves the updated plan from the old seed. ADR 0129 moves the
lexer. ADR 0135 moves Nuked OPL3 after the checked seed gains C11
external-inline finalization. ADR 0139 moves JPEG decoding and glyph
rasterization after the floating data and comparison paths reach the checked
seed. ADR 0167 moves the FPU, per-CPU, and SMP roots after their checked
objects and four-vCPU runtime paths pass, leaving four strict checked-in roots
on the host compiler. ADR 0176 moves libm. ADR 0180 moves the kernel entry and
SIMD roots. ADR 0181 moves `kernel/core/string.cc` through its closed
two-header recipe and completes CupidC ownership of the strict checked-in
kernel and driver cohort.

The checked seed represents the three naked IPI entries in
`kernel/smp/smp.cc`. The reschedule and call entries emit exact
`PUSHAL`, direct-call, `POPAL`, and `IRET` sequences with no C frame. The panic
entry emits `CLI`, `HLT`, and a relative jump back to the halt instruction.
Two complete kernel-profile compiles produce the same validated 8,444-byte
ELF32 object with SHA-256
`bd3189b2a1a6d15728c559172f5d6acca0889103428085cec8cc1024742a22d1`.
The production wrapper now owns this root. Its hash differs from the earlier
`.c` proof only because the existing `__FILE__` diagnostic records the new
source name.

The checked seed now finalizes C11 inline meaning across the complete
file-scope declaration set. The ordinary header declaration followed by the
inline definition of `OPL3_Generate4Ch` in
`kernel/audio/nuked_opl3.cc` emits one global function. Two kernel-profile
compiles produce the same validated 40,424-byte ELF32 object with SHA-256
`a3a04ade4029d9333902bb93376fb5eef21f349ee5a1406bd0751cc4cee9f2a1`.
Its only undefined import is `memset`. The exact production recipe freezes
the checked seed, then compiles from a private copy of the source and its
three headers. It rechecks the live four-file closure before publishing the
object. A poisoned-host test prevents fallback to GCC or Clang. An earlier
`static` declaration still keeps a later `extern inline` definition internal.
External-linkage inline declarations without a definition fail during
translation-unit finalization, while pure external inline definitions still
receive a focused unsupported diagnostic from lowering.

CupidC accepts GNU `used` and `__used__` on file-scope objects and functions.
Redeclarations merge the flag into one canonical entity, and the Linear IR
and object boundaries validate it before use. The generated
`kernel/cpu/ksyms_data.cc` source is part of the normal checked CupidC graph.
Its i386-word initializer preserves the current 109,889-byte symbol blob. The
checked wrapper produces a 110,304-byte object with SHA-256
`45e77aff292df2d47ac7b9c2004371fa767ed511df066238a2e3299c9a9d08c2`.

The checked seed retains GNU `noinline` and
`target("general-regs-only")` on canonical file-scope functions.
`noinline` records the request for a future inliner and does not change
current bytes. Each IR function carries the canonical code generation mask,
and emission rejects a mismatch. The target attribute rejects
compiler-generated floating work while allowing explicit source assembly
through its separate contracts.
It also accepts the exact volatile `ldmxcsr %0` form with one
addressable, non-atomic 32-bit integer `m` input. Linear IR evaluates the
object address once, and the shared x86 model emits `0F AE 10` at `[EAX]`.
It also accepts the exact volatile MOVSS round trip in `fpu_boot_smoke()` and
the matching one-way load and store forms. Each form keeps a typed `float`
memory address and requires the `xmm0` clobber. The shared x86 model emits
`F3 0F 10 00` for the load and `F3 0F 11 00` for the store through EAX.
The exact volatile x87 block in `stress_sin()` is also represented. It keeps
one `double` `=m` output and one `double` `m` input, evaluates their addresses
once in output-then-input order, and permits no clobbers. The shared x86 model
emits `FLD`, `FSIN`, and `FSTP` through EAX with balanced x87 depth and no
frame temporary. Two complete builds of
`kernel/cpu/fpu.cc` produce the same validated 6,620-byte object with SHA-256
`14c3ea232b7d4455ceabd561c69293cc5849abae24d9f210aa69d64ed8c8a5cb`.
The normal Make graph now compiles this root through the checked wrapper.
A typed object policy rejects helper calls or floating work before the CR4
write. It requires one `FNINIT` followed by one 32-bit memory `LDMXCSR`.
The four-vCPU runtime gate requires `[fpu] SSE2 enabled`,
`[fpu] boot smoke ok`, and `FPU boot smoke passed`.

The checked seed also represents the two exact EFLAGS restore statements
in `simd_cpu_has_cpuid()`. Each volatile statement takes one 32-bit integer
through `r`, has no output, and requires the `cc` clobber. Linear IR keeps the
input value and clobber metadata, while the shared x86 path emits
`POP EAX`, `PUSH EAX`, and `POPF` with balanced ESP. It also
accepts the unchanged CPUID statement where the `a` input shares EAX with
the `=a` output. The public operand keeps its fixed-register spelling and
names output zero as its match; Linear IR and object emission verify that
relationship, including represented integer types and equal widths, before
loading EAX. A frozen same-width float cannot bypass those checks. The
checked seed now accepts the six remaining packed SSE2 statement shapes in
unchanged `kernel/cpu/simd.cc`. It checks the exact pointer and integer inputs
plus the memory and XMM0 through XMM7 clobbers, then emits the copy, broadcast,
blend, and saturating-add instructions through Cupid's shared x86 model. Two
checked-seed builds produce the same validated 8,768-byte object with SHA-256
`fd280c321b8eb38a90d4f0982d70b8df0364585e3da322eb2c9de722e071f8d4`.
The normal SIMD recipe now compiles through the checked wrapper from an exact
seven-header closure.

The checked seed emits the four exact descriptor-table and segment-register
statements in `kernel/smp/percpu.cc`. The LGDT forms keep their
packed six-byte memory operand and exact AX and memory clobbers. The GS form
keeps its represented 16-bit selector. Cupid's shared x86 model reloads the
data segments directly and uses a relative call-and-RETF trampoline for CS,
so no compiler-local label relocation is needed. Two complete compiles
produce the same 6,760-byte object. The checked production wrapper owns the
root and its frozen recursive header closure.

File-scope GNU basic assembly has a separate CupidC representation.
The frontend owns immutable templates outside function bodies, and Linear IR
keeps their source order. The i386 emitter handles the twelve exact x87/SSE
floating wrappers at the start of unchanged `kernel/cpu/libm.cc`. Cupid's
shared x86 encoder produces 248 text bytes, twelve global function symbols,
and no relocations. The checked seed accepts named operands on function-body GNU
assembly and resolves `%[name]` to the existing numeric operand before Linear
IR. It emits the complete `libm_pow_impl` and `libm_powf_impl` statements.
The double form has five `double` memory operands. The mixed form has a
`float` output, two `float` inputs, and two `double` inputs. Each focused
function has 116 exact text bytes, no relocations, a maximum x87 depth of
three, and balanced depth on return. The checked seed also emits the exact
`sqrtsd %1, %0` statement with one `double` `=x` output and one `double` `x`
input. Its focused function has 65 text bytes and no relocations. The exact
`libm_atan2_impl()` statement is also represented with one `double` `=m`
output, two `double` `m` inputs, and one `memory` clobber. Its focused
function has 53 text bytes and no relocations. The exact
`libm_exp_impl()` statement is represented with the same three operand
types in output, `x`, `log2e` order. Its 71-byte focused function has no
relocations, reaches x87 depth three, and returns to its incoming depth.
The checked seed emits the following aligned `fabs` mask block and the
`fabs` and `fabsf` wrappers. The masks occupy the first 32 bytes of
`.rodata`, with local labels at offsets 0 and 16. The wrappers contain 15 and
14 text bytes and carry one `R_386_32` relocation each to the matching mask.
The checked seed also emits the next eight rounding wrappers. It saves and
restores the x87 control word around `FRNDINT`, selecting down, up,
nearest-even, or toward-zero mode for each double and float pair. The family
adds 384 exact text bytes, has no relocations, reaches x87 depth one, and
balances ESP and x87 depth. It also emits the following `fmod` and `fmodf`
wrappers. Each repeats `FPREM` while status-word C2 is set, using an exact
short backward branch, then discards the divisor and returns the remainder
through XMM0. Both functions contain 35 text bytes, reach x87 depth two,
balance ESP and x87 depth, and need no relocation. The complete source now
reaches the exact aligned `libm_log2e_const` and `libm_ln2_const` block and
the next eight exponent and logarithm wrappers. The data effect contributes
16 `.rodata` bytes at alignment eight and local labels at offsets 0 and 8.
`exp2` and `exp2f` contain 37 text bytes each, `exp` and `expf` contain 45
each, `log2` and `log2f` contain 23 each, and `log` and `logf` contain 27
each. The four natural forms carry one `R_386_32` relocation apiece to the
matching constant. All eight wrappers use Cupid's x86 model, balance ESP
and x87 depth, and reach no deeper than three x87 values. The unchanged
source then reaches the `pow` wrapper at line 846.

The checked seed emits that wrapper and the other 17 remaining cdecl bridges.
The binary `pow`, `hypot`, and `nextafter` pairs and the unary
`asin`, `acos`, `sinh`, `cosh`, `tanh`, and `cbrt` pairs copy their original
argument words, call the matching external implementation, reclaim the
copied words, and move the ST(0) result into XMM0. The four float or double,
unary or binary shapes occupy 558 text bytes and carry exactly 18
`R_386_PC32` relocations with addend `-4`. The decoder checks every stack
access, call, cleanup, result move, and return. Two complete compiles of
unchanged `kernel/cpu/libm.cc` produce the same 16,164-byte ELF32 relocatable
object with SHA-256
`ccfb59839b058020a3cdc30c8e6db7ebac8845215a38ff974b3cbca876574eac`.

The normal `libm.cc` recipe runs the checked compiler wrapper against a
frozen two-header closure. The source keeps the exact pre-transfer bytes,
and a build with every host code-generator command poisoned produces the
locked object. ADR 0176 records the production transfer.

The checked seed emits the exact volatile
`call 1f\n1: popl %0` state read used by the stack-trace helpers in
`kernel/lang/as.cc` and `kernel/lang/cupidc.cc`. The form takes one four-byte
integer `=r` output. It becomes a zero-displacement `CALL` followed by
`POP r32` through Cupid's x86 model, with no relocation or host assembler.
Both complete roots compile reproducibly under the kernel profile and now
produce their normal objects through the checked wrapper.

The checked seed accepts the GNU `Nd` alternative used by the 8259 PIC
helpers. It selects the valid DX branch and emits the exact
`outb %0, %1` and `inb %1, %0` byte forms through Cupid's x86 model. The
complete `kernel/cpu/pic.cc` root compiles to a deterministic 2,408-byte
ELF32 object and the normal Make recipe uses that object.

The checked seed accepts the exact volatile `fnstsw %0`, `fnstcw %0`, and
`stmxcsr %0` forms with one `=m` output. The first two require a modifiable
16-bit integer, while `stmxcsr` requires a modifiable 32-bit integer. Linear
IR evaluates the destination once, and the i386 emitter writes the machine
state directly through that address without an output staging slot. The exact
call-next form above covers the later local-label statement in
`kernel/core/panic.cc` as well. Two complete kernel-profile compiles produce
the same validated 10,212-byte ELF32 object with SHA-256
`84daa51a65d6970ae7a7918b05fe64b7676c39d3309264375e349cf0ae20d428`.
The normal panic object now comes from that checked path.

Function-body GNU assembly may have no operands. Basic statements and
extended statements with an empty output list are implicitly volatile. Exact
sequences of PAUSE, NOP, STI, HLT, CLI, CLD, SFENCE, and FNINIT emit through
the shared x86 model without a temporary frame slot or EBX save. That path
builds the unchanged e1000, desktop, socket, and TCP sources in the normal
image rather than only in the earlier hybrid proof.

The checked seed accepts a modifiable four-byte object or `void` pointer as
the single `=r` output of the exact `mov %%gs:0, %0` per-CPU load. The
frontend and IR keep the pointer type and evaluate its destination once. The
shared x86 model emits `65 A1 00 00 00 00`, then the ordinary output path
stores the snapshot through that destination.

The checked seed accepts independent `r` and `c` inputs for the exact
privileged assembly used by `idt.cc`, `paging.cc`, and `lapic.cc`. The `r`
constraint carries a represented four-byte integer or data pointer, while
`c` carries a represented four-byte integer in ECX. CupidC emits CR0, CR2,
CR3, and CR4 moves plus RDMSR directly into ELF32 objects without a host
assembler. All three roots compile twice to byte-identical, validated
objects, and the normal build now owns them through CupidC.

The checked seed compiles both FXSAVE statements in
`kernel/core/process.cc`. The exact volatile `fxsave (%0)` form accepts one
four-byte object or `void` pointer `r` input and the source's `memory`
clobber. The emitter loads the pointer into EAX and emits `0F AE 00` through
the shared x86 model. Two complete `KERNEL_I386` compiles produce the same
validated 30,216-byte ELF32 object, with one decoded FXSAVE in each process
creation path. The normal process object now comes from this path.

The checked seed compiles every unchanged helper in `kernel/core/ports.h`.
The six scalar helpers retain their 8-bit, 16-bit, or 32-bit accumulator
width and their 16-bit DX port input. The two word-string helpers retain
read/write pointer and count operands, issue `CLD` before `REP INSW` or
`REP OUTSW`, write the advanced values back, and restore ESI or EDI for the
i386 cdecl caller. The frontend accepts the source's single `memory` clobber
on INSW, and each output address or input value is evaluated once. These
forms are present in the checked seed. The normal build
uses them in the production cohort.

The same compiler handles `__atomic_load_n`, `__atomic_store_n`,
`__atomic_exchange_n`, `__atomic_fetch_add`, and `__atomic_fetch_or` for
one-, two-, and four-byte integer objects. It keeps the memory order in typed
AST and IR records, emits ordinary width-correct loads and release stores,
uses memory `XCHG` for exchanges and sequentially consistent stores, and uses
`LOCK XADD` for fetch-add. Fetch-or uses a `LOCK CMPXCHG` retry loop so it can
return the old value without losing a competing update. The loop also
preserves EBX for i386 cdecl. The six `__ATOMIC_*` order macros are reserved
target predefines in every language mode; the five expressions remain
GNU-only. A decoded i386 oracle checks old values, memory updates, wraparound,
narrow signedness, cdecl state, one-time operand evaluation, and forced
contention. Runtime order arguments, pointer and eight-byte atomics, and HLE
flags remain open. The checked seed carries all five operations and compiles
the active EHCI fetch-or path.

The active non-Doom header gate is 155/155 in the checked seed. Under the full
kernel profile, unchanged `kernel/smp/acpi.cc` and `kernel/smp/mp_tables.cc`
emit byte-identical 5,708-byte and 4,156-byte i386 ELF32 objects. The checked
wrapper also compiles the port-I/O users and EHCI's atomic fetch-or
ownership path. Each transferred Make recipe carries its exact recursive
header closure.

Poisoned-host checks cover all 238 checked-in normal CupidC recipes through
the strict and Doom gates. They fail if a CupidC-owned object reaches Clang or
GCC. They pass against the renamed graph. Across the three supported build
roots, the audit records 449 transforms. CupidC participates in 245, Python
participates in all 449, and no normal transform invokes a host C compiler.
The Toolchain root now builds its fourteen `.cc` contracts twice with
stage-two and stage-three CupidC, compares the static i386 executables, and
publishes the cohort together. The publisher accepts only a dedicated
`cupidc-contracts` directory inside the source tree. It validates the target
before work and again before promotion, and an existing destination must
already verify as a complete cohort. Arbitrary directories, source trees,
files, and symbolic links are rejected without modification. The initial,
private, and live contract inventories must match exactly, including
membership and hashes, so additions, removals, and a transient edit copied
before its live source is restored all fail. Normal build and test entry points derive
the cohort from each requested executable, require a named manifest artifact,
and verify the complete artifact inventory, the 45 contract inputs, the
41-file fixed-point source inventory, and the checked seed manifest before
execution. The contract inventory includes the Toolchain Makefile and both
Python modules that build or verify the cohort. A plan with an unknown
link-object key fails validation before the first compiler process starts.
Native contract binaries remain available only through
`make -C toolchain native-oracles`.
The shared runtime formats signed and unsigned `long long` values, padded
64-bit hexadecimal values, and precision-bounded strings. Those forms come
from the unchanged contract diagnostics and are covered by the executable
runtime probe.
The 438-transform root image
graph has no host C or recursive Make transform. Its CupidASM, CupidObj,
CupidLD, and CupidDis commands run from the checked seed. One
Python transform checks the external program syscall ABI and produces no OS
code. One Python transform generates `big.bin`; another packages the frozen
fixture tree as deterministic ISO9660/Rock Ridge bytes.
`test_iso/fixtures.manifest` pins every directory and file without asking Make
to recurse through an unchecked path. Make declares the same portable paths
explicitly, and a checked test prevents that prerequisite list from drifting
away from the manifest. Raw manifest text never enters Make grammar. The graph
records the manifest, fixture root, every declared member, writer, imported
bootstrap helper, and Makefile. ISO authoring does not probe or launch an
external command.
The runner checks the live five-tool cohort again after every command. Make
passes every wildcard-discovered output list through `$(sort ...)` before
generation or link, so Windows and Linux do not inherit different link order
from host locale.
The first direct host comparison matched 426 of 430 kernel artifacts and
traced all four differences to one JPEG object. Host FFmpeg had rewritten the
tracked progressive image differently on Windows and Linux. The repository
stores the accepted sequential baseline bytes. Hostbuild validates and
copies exact SOF0 or SOF1 input, rejects progressive, unsupported, or malformed
frames, and gives the private snapshot to checked CupidObj. The root build no
longer calls FFmpeg, `jpegtran`, `djpeg`, or `cjpeg`. The Linux kernel build
passed in 607.7 seconds, and the Windows root build passed in 341.6 seconds.
All 430 frozen kernel artifacts match byte for byte.

The current raw kernel is 8,510,856 bytes with SHA-256
`5bd12f137dbbbba30bff4d3fe2b95e1727379b2ece1aaffc6a96cb2dc4416d5a`.
The fresh 209,715,200-byte normal image has SHA-256
`5589c5cc151c486a85efaffc3551b37ec4f733ebd57f78c863c5bf6b96c7e23d`.
The kernel bytes match the image from the 2,560-byte boot boundary. This is a
preboot digest; guest filesystem writes intentionally change the image. The
preceding cross-host image passed a private `/bin/ls.cc` JIT boot in 49.8
seconds. Its
35,033-byte log has SHA-256
`74951551108b76fa1e7def8e525dbc50f0a7c62f19d5b47a70e4d1cf9961f21f`.
The CupidC transforms are 238 checked-in normal roots, the generated kernel
symbol table, three generated installation tables, and three example
programs. The renamed graph passes both CupidLD links and CupidObj flattening.
The current clean-image runtime checkpoint
includes the transferred lexer, Nuked OPL3, FPU, per-CPU, and SMP roots in the
complete checked frontier. Four-vCPU GUI runs pass with both supported
NICs and reach SMP
startup, RDRAND, all 62 crypto checks, USB storage, the desktop, terminal,
audio playback, the glyph path, a checked 8-by-8 JPEG decode, and in-OS CupidC
execution at `0x01100000`. A separate smoke
loads the same external ELF program twice at `0x01C00000`; process cleanup
releases the first arena lease before the second load. ADR 0124 records
the first renamed graph's exact hashes, byte counts, timing, and runtime log.
ADR 0126 records the fixed-point naming proof.

The hosted CupidC path carries one-byte, two-byte, and four-byte integers
through target-sized locals, file objects,
members, indexed access, conditions, conversions, assignment, mutation, and
prototyped, variadic, or unprototyped direct and indirect calls. Narrow loads
produce canonical 32-bit values, while stores use the declared byte or word
width. Represented scalar cdecl arguments keep four-byte stack slots, and
callers and callees normalize narrow results. An explicit cast to `void`
evaluates its operand once and discards any represented result.

The hosted path also carries signed and unsigned eight-byte integers through constants, matching conditional arms, fixed direct and indirect call results, object access, declared parameters, and named direct or indirect call arguments. File objects, block statics, fixed automatic objects, pointer dereferences, ordinary members, and indexed elements can be loaded, initialized, assigned, mutated, chained, discarded, and returned. One Linear IR entry names an emitter-owned eight-byte snapshot, so a load is a stable C value rather than a borrowed object address. A declared wide argument occupies eight cdecl stack bytes, and later parameter addresses include its full width. On return, EAX carries the low word and EDX carries the high word. Wide values support addition, subtraction, multiplication, division, remainder, unary plus, unary minus, bitwise complement, left shift, signed or unsigned right shift, AND, OR, XOR, all six signed or unsigned comparisons, logical not, short-circuit logical operators, conditional selection, structured scalar conditions, signed or unsigned switch dispatch, all ten compound assignments, prefix and postfix update, explicit casts to or from represented byte, word, and doubleword integers, and the usual arithmetic conversion from `signed long long` to `unsigned long long`. A wide switch evaluates its condition once, duplicates the private snapshot handle, and compares both words of each case value. Wide mutation evaluates the destination once and performs one semantic load and store. Wide multiplication combines one full low-word product with both cross-word products. Division and remainder run a fixed 64-step restoring loop over unsigned magnitudes, then apply the quotient or dividend sign. Each multiplication, division, or remainder result receives a fresh snapshot. GNU wide enums promote to their compatible signed or unsigned wide type. The complete unchanged `ctool_buffer_put_le64`, `ctool_buffer_patch_le64`, `pp_if_value_truth`, `pp_if_is_negative`, `pp_if_signed_less`, `pp_if_signed_magnitude`, `cfront_constant_apply_binary`, and X25519 `fe_carry` bodies guard those operations. CupidASM's unchanged number parser and unary expression branch guard the arithmetic, while X25519's unchanged `fe_mul_u32` helper guards wide-by-narrow multiplication. Runtime cases that C leaves undefined promise neither a trap nor a result. Signed and unsigned wide integers can also pass through an ellipsis or a call without a prototype.

The checked seed also accepts an explicit non-atomic `double` to
`unsigned long long` cast. The i386 emitter derives an unsigned high word
from `value / 2^32`, subtracts that exact multiple from the original value,
and derives the low word from the remainder. Each 32-bit step splits at
2^31 so the signed SSE truncation instruction stays in range. Decoder-driven
cases cover zero, positive and negative fractions, both sides of 2^32, 2^53
minus one, 2^63, the active `1.8e19` guard, and the largest binary64 value
below 2^64. This lets the seed emit complete unchanged
`kernel/core/string.cc` as a deterministic 14,460-byte object. The normal
recipe freezes the source and its two-header closure, validates the emitted
ELF32 object, and publishes it without a host compiler.

The hosted path carries `float` and `double` values through objects,
initialization, assignment, discard, calls, parameters, results, and returns.
It supports conversion between the two widths, arithmetic at matching or
mixed widths, conditional values, compound arithmetic assignment, default
argument promotion, `va_arg(double)`, and all six comparisons. Matching
`float` and `double` operands compare at their own width, while a mixed pair
compares as `double`. Decimal constants are published as exact IEEE bits.
Static-duration scalar and aggregate leaves accept decimal
floating constants with parentheses and unary signs. Assignment conversion
between `float` and `double` is rounded with integer-only target arithmetic,
and exact binary32 or binary64 bytes reach `.rodata`, `.data`, or `.bss`.
Represented integer-to-floating conversions,
floating-to-signed conversions, floating-to-unsigned byte or word
conversions, the explicit `double` to unsigned-wide cast, and mixed integer
and floating arithmetic use the SSE object path. Unsigned four-byte input
uses an exact split across the sign boundary.
The x87 transport model, SSE conversion oracle, and `UCOMISS` or `UCOMISD`
comparison oracle check rounding, operand order, ordered values, signed zero,
infinities, quiet and signaling NaNs, call alignment, and frame state.
Non-atomic `long double` values use twelve-byte i386 objects and x87 80-bit
memory operations. Automatic values use frame snapshots. Static-duration
scalars, fixed arrays, and complete records may contain long-double leaves.
Implicit initialization zeros the complete object; an explicit leaf accepts
an integer constant expression equal to zero. Each leaf occupies twelve
zero-filled BSS bytes, and atomic leaves fail recursively without following
pointers. The represented slice covers floating-width conversion,
unary plus and minus, all four arithmetic operators, function returns, and
direct or indirect call results. Fixed, ellipsis, and unprototyped arguments
occupy twelve cdecl bytes. `va_arg(long double)` copies the same width and
advances the cursor by twelve bytes. The static i386 runtime checks both
zero-initialization forms, a following four-byte argument, both old-style call
forms, and result transport through x87 `ST0`. Direct floating truth,
hexadecimal and subnormal floating literals, `long double` literals, nonzero
or floating static long-double initializers, comparisons, integer conversions
involving `long double`, conversion to unsigned four-byte integers
or `_Bool`, mixed integer and floating conditional arms, floating increment
and decrement, SIMD values, floating atomics, and over-aligned emission
remain open.

Plain assignment, all ten compound assignments, and prefix or postfix increment and decrement now work for represented non-atomic bit fields in four-byte storage units. Linear IR keeps the selected member and evaluates the record address once. Partial fields preserve neighboring bits, and postfix updates retain the extracted old value through the store so width wrap does not change the result. Narrow unsigned fields promote to signed `int` when their values fit. A volatile 32-bit field uses one read and one direct store. An execution oracle proves that `states[(*index)++].value++` advances its side-effecting index exactly once. Partial volatile mutation, atomic bit-field access, and non-four-byte storage units remain open. The plain-assignment contracts still pin Doom's unchanged `colors[index].r = value` shape.

The hosted path also carries complete fixed-size structures with alignment up to four bytes when their inline object graph has no volatile or atomic subobject. A structure lvalue conversion copies the target bytes into private frame storage, so assignment chains, conditional values, expression initialization, casts to `void`, and returns keep value semantics instead of aliasing the source object. Direct and indirect calls pass structure arguments inline in four-byte-rounded stack areas. A structure result uses a hidden pointer at `[EBP+8]`; explicit parameters start at `[EBP+12]`, and the callee returns the pointer in EAX with `RET 4`.

The shared value path copies nested union storage inside a supported structure and reads a scalar member directly from a returned structure snapshot. A direct four-byte integer literal zero may be cast to a represented function pointer. Represented function pointers may also cast to another function-pointer type or to and from a represented 32-bit integer without changing target bits. Explicit conversions between an object pointer and a signed or unsigned eight-byte integer use the wide snapshot path: widening writes a zero high word, and narrowing keeps the low word. Outside the explicit Doom compatibility profile, object-pointer and function-pointer interchange remains outside this boundary. Function-pointer and wide-integer conversions, top-level union parameters or results, and aggregate members selected from structure rvalues also remain open. Static compatible character and void pointers accept an ordinary string literal hidden behind parentheses or a macro. Pointer qualification accepts the safe `char **` to `char *const *` conversion. It rejects `char **` to `const char **`, which would add a qualifier at an unsafe nested level, and rejects removing the nested `const`.

The exact hosted gate checks every source at its real i386 Linux ABI. It
contains 33 strict C11 roots and two GNU-enabled runtime roots: the 19-source
static tool union, `kernel/lang/as_elf.cc`, the runtime implementation and
probe, and all fourteen Toolchain contracts. Thirty-one strict roots use only
the Toolchain and hosted declaration roots. The assembler ELF adapter and its
contract form a two-root bridge that can also include `/kernel/lang`; no other
hosted source gets that wider search path. The GNU profile is limited to the
runtime implementation and probe. The former 64-bit hosted profiles are empty.
CupidC emits deterministic ELF32 objects, CupidLD links static executables,
and the checked cohort compares every contract across compiler stages. These
profiles use repository headers, an explicit four-byte pointer fact, and no
host system headers.

CupidC emits the repository's i386 Linux runtime and five command closures: CupidC, CupidASM, CupidDis, CupidLD, and CupidObj. CupidASM assembles `_start` and the system-call boundary, while CupidLD links each deterministic static i386 command without unresolved symbols. A sixth executable checks process arguments, heap reuse and release, allocation failures, files and seeks, 32-bit and 64-bit integer formatting, bounded string formatting, formatting errors, working-directory errors, memory comparison, and the remaining checked string functions. The runtime is intentionally narrow, with unbuffered streams and single-threaded heap, stream, and `errno` state.

The native and Cupid-built `cupidc` drivers accept compile-only C11 jobs with
ordered include roots, command-line definitions and undefinitions, forced
inputs, GNU or freestanding mode, and commit-gated output. `-I` enables quoted
and angle lookup, while `--include-angle` enables angle lookup only.
Repeatable `-include` options run in caller order before the primary source.
These path options accept native paths or absolute logical paths under
`--root`. Compilation failures leave an existing output untouched; a
file-adapter write failure can still leave a partial file.

The normal build uses the checked seed's exact Doom-tree preprocessing profile
for 80 source objects. Its explicit `--doom-compat` switch gives the five
audited calls in `i_system.cc` old-style external declarations and
permits eleven audited, bit-preserving conversions between unqualified
function pointers and unqualified four-byte data or `void` pointers in
`m_menu.cc`, `p_saveg.cc`, `p_ceilng.cc`, and `p_plats.cc`. Strict C and plain GNU
mode still reject those implicit conversions, and explicit function/data
casts remain outside Linear IR. An integer-only IEEE evaluator compiles the
unchanged automap table, the sound driver's empty volatile memory barrier
emits no target bytes, one-active-member union initialization compiles
unchanged `info.cc`, and ordinary narrow bit-field promotion compiles unchanged
`i_video.cc`.

The same checked production path owns the three compatibility roots. It
preserves the explicit static string cast in `doom_libc_stubs.cc` and emits the exact
`dg_setjmp` and `dg_longjmp` file-scope block through Cupid's x86 model.
Two checked-seed compiles produce the same 27,992-byte, 14,352-byte, and
10,232-byte objects for each root. All 83 sources use `.cc`.

The wrapper freezes each selected source and the complete 289-file header and
include space for both profiles. Its content-addressed manifest fixes the
three-source and 80-source memberships. It scans the visible Doom tree before
and after every compile. A legacy `.c` file, an unlisted `.cc` file, a missing
root, header membership or byte drift, a symbolic link, or an NTFS junction
fails before publication. The `g_game.cc` object keeps the two
`&array[1]` initializers as
`R_386_32` relocations with addend 4; direct calls still require
`R_386_PC32` addend -4. The root Make graph contains no host C transform.
ADR 0184 records the production transfer.

Private four-CPU boots pass the full frontier with both e1000 and RTL8139.
Each NIC also passes `doom` with no WAD, `doom -iwad /disk/missing.wad`, and
a later CupidC-built `ls` command. The logs contain the no-WAD guidance, the
missing-IWAD error, `[doom] returned to shell`, and no panic marker. This
checkout contains no WAD, so these checks do not claim gameplay, input, game
audio, or save behavior.

The five static i386 Linux tools have a checked bootstrap seed. Its manifest binds the exact binaries, source revision, target ABI, producer lineage, 19-source build plan, and five link orders before execution. The current CupidC seed is the checked bootstrap's 2,528,332-byte stage-three image with SHA-256 `f53989572cd1564a8bf91059552868ee43a1d80905986b58cd97d44949aab3a1`. It comes from revision `af4644177c033eebda164d7893074315439df119` and carries the complete 83-root Doom frontier, current GNU entity metadata, the active x87 and SSE forms, descriptor and segment assembly, naked IPI entries, all `libm.cc` file-scope effects, the exact dglibc jump block, pointer-preserving static address casts, the kernel-entry BSS clear with a nonzero page-aligned stack top, packed SSE2 statements, and explicit `double` to `unsigned long long` conversion. The same seed carries the 587-row shared x86 catalogue through CupidASM and CupidDis.

The harness pins the build plan independently and freezes the verified manifest and binaries. It also copies the exact bytes of all 41 source inputs, including `link.ld`, into a private compiler root. Seed CupidC, CupidASM, and CupidLD build stage two below that root, then the stage-two producer trio repeats the work for stage three. The harness rehashes both the private closure and the live closure before the first stage, after each stage, and after the behavior suite. A live edit that is made and restored during a compile cannot change the bytes consumed by either stage.

The comparison covers all 19 C objects, independently assembled startup objects, and the linked CupidC, CupidASM, CupidDis, CupidLD, and CupidObj images. Every artifact matches byte for byte with host code-generator commands poisoned, including all five checked seed images against stage two. Both stages also agree on each tool's help path, ten successful operations, and six useful failures. The requested output stays empty while those checks run. Both stages, the behavior evidence, and `bootstrap-report.json` appear together only after complete success. Run `make verify-bootstrap-seed` for validation or `make bootstrap-from-seed` for the complete rebuild. The normal Toolchain build then uses those two compiler stages for fourteen contract programs and the runtime probe. It compares all sixteen new objects and fifteen linked executables. Its private contract tree must reproduce the initial 45-file inventory exactly, including the Toolchain Makefile and both Python control modules, and each live check discovers the set again before comparing hashes. The public manifest also records the checked build plan, seed manifest, and complete 41-file fixed-point source inventory. Seed-manifest hashing, decoding, and validation use one captured byte sequence. A replacement during verification cannot pair one digest with another read's build plan. Every run recomputes both source inventories before execution. A host compiler is used only by the explicit native oracle and development targets.

Host Python still coordinates the checked fixed point, and Windows still uses
WSL to execute the static i386 Linux tools. Native Windows and Python-free
fixed points therefore remain open. The proposed 20 percent output-quality
ceiling is also disabled: the project has no approved cohort, metric, oracle
producer, or same-revision oracle artifact. Older Windows and Linux host
`.text` measurements differ by 22.73 percent for the same revision, so
neither is a trustworthy default. Linker capacity checks remain separate
safety gates.

Hosted i386 object emission places ESP on a sixteen-byte boundary immediately before every `CALL`. The emitter derives padding from the function frame, the live Linear IR stack depth, and any outgoing target-sized argument area. Direct and indirect calls use the same rule for prototyped, variadic, unprototyped, nested, structure, and wide cases, with zero, four, eight, or twelve bytes of padding as needed.

Variadic calls and callees follow that same hosted path. The frontend applies lvalue conversion, array and function decay, integer promotion, and `float` to `double` promotion to each ellipsis argument as required. Every call instruction owns a contiguous slice of post-conversion actual argument types in a packed Linear IR array. A shared validator requires one complete ordered partition and rejects gaps, overlaps, invalid types, trailing entries, and metadata on non-call instructions. Named slots use declared parameter types after compatibility checking, while unnamed slots use the packed actual types. The i386 emitter uses the validated slice and actual count for cdecl argument order, slot widths, indirect callee placement, alignment, and caller cleanup. Direct and indirect calls can pass represented four-byte integers and pointers, signed and unsigned eight-byte integers, an existing `double` or `long double`, or a source `float` promoted to `double`. The `long double` path works for fixed, ellipsis, and unprototyped calls. A wide integer or `double` unnamed argument uses two adjacent cdecl words; a `long double` uses three. Arguments occupy increasing addresses in source order, with lower words first. Each argument still has one abstract IR handle, and an indirect callee remains below the argument handles while the emitter prepares the outgoing area.

In GNU C mode, `__builtin_va_list` is a target `char *` cursor. Explicit frontend and IR operations cover `__builtin_va_start`, `__builtin_va_arg`, `__builtin_va_copy`, and `__builtin_va_end`. The emitter starts the cursor after the full width of the final named cdecl argument. A four-byte pointer, integer, or enum read advances the stored cursor by four bytes. A signed or unsigned eight-byte integer, 64-bit enum, or `double` is copied into a fresh private snapshot and advances the cursor by eight bytes. A `long double` read copies twelve bytes and advances the cursor by twelve. Every represented width keeps the i386 cursor on four-byte slot alignment. The execution contract reads a four-byte value immediately after a `long double`, so it checks the twelve-byte cursor movement as well as the copied value. Nested callers also check aligned calls, cleanup, and complete returned values. Atomic, `float`, and aggregate reads remain unsupported. Calling `va_arg` with `float` is invalid C because a variadic `float` must arrive as `double`.

The hosted path accepts zero-parameter definitions written with an empty identifier list and preserves their non-prototype function type. Direct and indirect calls without a prototype apply the default argument promotions to every argument. Each call keeps its actual count and post-conversion type slice in Linear IR, and the i386 emitter accepts represented four-byte integers and pointers, signed or unsigned eight-byte integers, existing `double` or `long double` values, and source `float` values promoted to `double`. Block-scope `struct` and `union` tags now support forward declarations, same-scope completion, ordinary references, nested shadowing, and scope restoration. A record tag declared in a function definition's parameter list stays visible through the outer body and expires when that definition ends. Tag-only declarations accept the represented `typedef`, `extern`, `static`, `auto`, and `register` spellings, or a represented type qualifier, when they introduce a tag, and lower without runtime work. An empty declaration with storage or type qualification cannot merely repeat a visible tag. A `for` initializer may use a visible record type or an anonymous record, but it cannot introduce a named tag or omit the object. Anonymous record definitions cover Doom's block-static `packs` array. Block-scope `extern` object declarations now keep a lexical alias to one canonical linked object. Compatible repeats share identity, incomplete arrays may be completed, visible file-scope `static` objects keep internal linkage, and declarations introduced only inside a block do not leak into ordinary file-scope lookup. Their declarations reserve no frame storage and lower without runtime instructions. Block typedefs follow the same ordinary lexical scope. Each alias keeps a stable type and source-order binding, supports exact same-type repetition and nested shadowing, and lowers as a validated no-op. Record and function aliases work, and spelling a local alias does not change emitted ELF bytes. Block function declarations now keep a lexical alias to one canonical linked function. Plain and `extern` declarations share compatible identity, preserve a visible file-scope `static` function's internal linkage, stay out of file lookup when introduced only inside a block, and lower without storage or runtime instructions. A later file declaration can publish the same external entity, while calls and addresses use the normal linked-function path. Block enums publish lexical enumerator bindings with folded target values. Definitions work in declarations, record members, function-definition parameter lists, and block type names. Function-prefix and expression or initializer ownership records preserve the point where each name becomes visible, including type names in case values, loop headers, variadic reads, aggregate designators, and compound literals. Represented uses become integer IR without storage, symbols, or relocations. This covers the unchanged cursor constants in `kernel/gui/desktop.cc` and REPL limits in `kernel/lang/shell.cc`. The exact Doom profile still parses all of `kernel/doom/src/d_main.cc`, including `forwardmove` and `sidemove` on lines 1336 and 1337. Nonempty identifier-list definitions, block declaration attributes, nested function definitions, atomic variadic access, aggregate arguments without a declared parameter type, and aggregate variadic reads remain unfinished.

The private in-kernel compiler tags loop and switch control frames. `break` selects the innermost frame. `continue` scans outward to the nearest loop and removes every crossed switch selector before it jumps. The parser accepts 128 active control frames and 1,024 active statement calls. It rejects the next entry with `control nesting too deep` or `statement nesting too deep`, then restores the counters during REPL rollback. `/bin/feature25.cc` checks the three loop targets, nested switches, sustained selector cleanup, both accepted limits, the two overflow diagnostics, and a successful evaluation after each failure.

Block-static objects now reach the hosted ELF32 emitter. They use the same `.rodata`, `.data`, or `.bss` policy as file objects and receive deterministic local symbols based on their absolute block-binding indices. A runtime address uses `R_386_32` instead of an EBP-relative frame slot, and the declaration emits no runtime initialization code. This covers initialized, zero-filled, aggregate, string-backed, shadowed, unused, and unreachable block statics.

Fixed automatic arrays and complete structures with alignment up to four bytes support initializer lists. CupidC zero-initializes the full object first, then evaluates explicit integer, pointer, supported structure, or narrow character-array string leaves in source order. Scalar and structure values store through nested array and member paths. A string leaf copies the exact retained bytes with `REP MOVSB`, leaving any unused tail elements zero. Direct designators and omitted subobjects follow the frontend's existing initializer forest. The i386 emitter uses `REP STOSB` for the initial zeroing.

A complete C union may select one active member in an initializer list. A positional clause selects the first eligible named member, while a direct member designator selects that member. The same form works inside arrays, structures, automatic objects, static objects, and block-scope compound literals. Runtime lowering zeros the complete union before storing the selected member; static emission writes that member over zero-filled storage. A second clause is still rejected until CupidC can replace an earlier initializer subtree with C's override semantics.

Block-scope compound literals use the shared initializer walker and one persistent unnamed automatic object per source site. Each evaluation reruns the initializer and yields an lvalue that supports loads, address-taking, indexing, and member access. Aggregate lists are built in a separate frame slot and copied to the persistent object only after all initializer reads finish. This preserves the previous value when an escaped pointer reads the object during reevaluation. Narrow string roots zero and copy directly into the persistent array. The active `(ctool_string_t){literal, size}` call and a focused `(char[]){"Cupid"}` case now pass through the hosted frontend, IR, and object emitter unchanged.

Runtime narrow string expressions now receive deterministic local `.rodata` symbols and `R_386_32` relocations, so pointer initialization, arguments, indexing, and returns use normal array decay. Block-static pointers may also use another block-static object's address in a constant initializer; the local ELF symbol and relocation remain intact. File-scope and other static-duration compound literals, variable-length literals, and the named-aggregate backward-jump alias case remain open under issue #25. Top-level union and Cupid class values, aggregate members selected from structure rvalues, explicit bit-field initializer leaves, volatile or atomic aggregate access, over-aligned structures, Boolean mutation, and broader floating computation or conversion remain open. Static string address arithmetic, integer-routed or otherwise unrepresented address casts, wide strings, literal pooling, atomic and aggregate variadic values, and production integration also remain open. A copied structure may contain union, wide, or floating members because this path moves its complete target representation. The private in-kernel CupidC compiler continues to handle embedded runtime JIT and AOT compilation. See [the bootstrap record](docs/bootstrap/README.md), [ADR 0049](docs/adr/0049-cupidc-structure-values-and-cdecl-abi.md), [ADR 0050](docs/adr/0050-cupidc-sixteen-byte-call-alignment.md), [ADR 0051](docs/adr/0051-cupidc-block-scope-static-object-emission.md), [ADR 0052](docs/adr/0052-cupidc-block-scope-compound-literals.md), [ADR 0053](docs/adr/0053-cupidc-runtime-narrow-strings.md), [ADR 0054](docs/adr/0054-cupidc-scalar-variadic-calls.md), [ADR 0055](docs/adr/0055-cupidc-scalar-variadic-callees.md), [ADR 0056](docs/adr/0056-cupidc-empty-identifier-list-functions.md), [ADR 0057](docs/adr/0057-cupidc-block-scope-record-tags.md), [ADR 0058](docs/adr/0058-cupidc-block-scope-extern-objects.md), [ADR 0059](docs/adr/0059-cupidc-block-scope-typedefs.md), [ADR 0060](docs/adr/0060-cupidc-block-scope-function-declarations.md), [ADR 0061](docs/adr/0061-cupidc-block-scope-enums.md), [ADR 0062](docs/adr/0062-cupidc-nested-block-enum-definitions.md), [ADR 0063](docs/adr/0063-cupidc-bit-field-assignments.md), [ADR 0064](docs/adr/0064-cupidc-bit-field-mutation.md), [ADR 0065](docs/adr/0065-cupidc-wide-integer-returns.md), [ADR 0066](docs/adr/0066-cupidc-wide-integer-object-values.md), [ADR 0067](docs/adr/0067-cupidc-wide-integer-parameters-and-arguments.md), [ADR 0068](docs/adr/0068-cupidc-wide-integer-shifts-and-conversions.md), [ADR 0069](docs/adr/0069-cupidc-wide-integer-comparisons-and-conditions.md), [ADR 0070](docs/adr/0070-cupidc-wide-integer-addition-subtraction-and-unary.md), [ADR 0071](docs/adr/0071-cupidc-wide-integer-switch-dispatch.md), [ADR 0072](docs/adr/0072-cupidc-wide-integer-multiplication.md), [ADR 0073](docs/adr/0073-cupidc-wide-integer-division-and-remainder.md), [ADR 0074](docs/adr/0074-cupidc-wide-integer-mutation.md), [ADR 0075](docs/adr/0075-cupidc-wide-integer-variadics.md), [ADR 0076](docs/adr/0076-cupidc-floating-scalar-transport.md), [ADR 0077](docs/adr/0077-cupidc-float-default-argument-promotion.md), [ADR 0078](docs/adr/0078-private-cupidc-tagged-control-frames.md), and [ADR 0196](docs/adr/0196-transfer-toolchain-contracts-to-cupidc.md).

Here, production integration means replacing the remaining Python and WSL
launch bridge with native Cupid tooling, not recovering a host-C object
graph. The checked-seed path owns all 238 checked-in normal roots and the
generated kernel symbol translation described above. No supported transform
invokes a host C compiler.

[ADR 0079](docs/adr/0079-cupidc-same-kind-floating-arithmetic.md) records the first hosted floating arithmetic boundary. [ADR 0091](docs/adr/0091-cupidc-floating-width-conversions.md) records conversion between `float` and `double`, mixed-width arithmetic and conditional arms, and floating compound assignment.

[ADR 0081](docs/adr/0081-cupidc-self-host-source-frontier.md) records the hermetic Toolchain source and object frontier. [ADR 0082](docs/adr/0082-cupidc-i386-linux-host-abi.md) records the checked adapter declarations. [ADR 0085](docs/adr/0085-static-i386-host-adapter-link-tracer.md) records the earlier static link tracer. [ADR 0086](docs/adr/0086-cupid-built-i386-linux-tools.md) records the repository runtime and the first four static Linux commands. [ADR 0087](docs/adr/0087-cupidc-immediate-pointer-qualification.md) records the nested pointer qualification boundary. [ADR 0088](docs/adr/0088-cupid-built-cupidc-driver.md) records the compiler driver and first generation check. [ADR 0089](docs/adr/0089-cupidc-i386-compiler-fixed-point.md) records the complete i386 Linux compiler fixed point. [ADR 0090](docs/adr/0090-static-i386-toolchain-fixed-point.md) records the five-tool fixed point and its producer lineage. [ADR 0092](docs/adr/0092-checked-i386-linux-bootstrap-seed.md) records the first checked seed, verification boundary, and source-drift guard. [ADR 0097](docs/adr/0097-refresh-the-checked-i386-linux-seed.md) records the first stage-three seed refresh. [ADR 0102](docs/adr/0102-refresh-seed-for-smp-compiler-support.md) records the SMP compiler seed, [ADR 0106](docs/adr/0106-refresh-seed-for-port-io-compiler-support.md) records the port-I/O compiler seed and poisoned-host reproof, [ADR 0107](docs/adr/0107-cupidc-gnu-atomic-fetch-or.md) records compiler-head fetch-or, [ADR 0108](docs/adr/0108-refresh-seed-for-atomic-fetch-or.md) records its checked-seed promotion, [ADR 0110](docs/adr/0110-cupidc-production-cutover.md) records the 40-source production cutover, [ADR 0111](docs/adr/0111-expand-cupidc-production-ownership.md) records the 116-source expansion and memory map, [ADR 0112](docs/adr/0112-check-generated-and-user-cupidc-builds.md) records the generated and external-program handoff, [ADR 0113](docs/adr/0113-expand-the-source-driven-cupidc-frontier.md) records the source-driven compiler frontier, and [ADR 0114](docs/adr/0114-refresh-seed-for-the-source-driven-frontier.md) records its checked-seed promotion.

[ADR 0115](docs/adr/0115-transfer-the-source-driven-roots-to-cupidc.md) records the 20-root `.cc` ownership transfer. [ADR 0116](docs/adr/0116-retain-gnu-used-entities-in-cupidc.md) records compiler-head `used` metadata. [ADR 0117](docs/adr/0117-emit-privileged-register-assembly-in-cupidc.md) records control-register and RDMSR assembly. [ADR 0118](docs/adr/0118-cupidc-call-next-gnu-assembly.md) records the stack-trace call-next boundary. [ADR 0119](docs/adr/0119-cupidc-fxsave-pointer-input-assembly.md) records the FXSAVE pointer-input boundary.

[ADR 0120](docs/adr/0120-use-dx-for-gnu-nd-port-operands.md) records the GNU `Nd` DX fallback and the compiler-head PIC proof. [ADR 0121](docs/adr/0121-cupidc-machine-state-memory-outputs.md) records the exact machine-state memory-output boundary. [ADR 0122](docs/adr/0122-refresh-seed-for-gnu-assembly-frontier.md) records the five-tool seed refresh and poisoned-host reproof.

[ADR 0123](docs/adr/0123-transfer-gnu-assembly-frontier-to-cupidc.md) records the eight-root and generated-symbol production transfer.

[ADR 0124](docs/adr/0124-name-production-cupidc-sources-consistently.md) records the 111-root `.cc` naming transfer. ADR 0126 completes the five shared Toolchain roots. [ADR 0127](docs/adr/0127-lock-the-external-program-syscall-abi.md) records the external syscall contract, [ADR 0130](docs/adr/0130-run-user-cupid-tools-natively-on-windows.md) records the optional native Windows user-tool path, [ADR 0133](docs/adr/0133-freeze-user-abi-inputs-and-isolate-runtime-boots.md) records the ABI snapshot and private guest checks, [ADR 0188](docs/adr/0188-run-the-windows-user-build-from-the-checked-seed.md) makes the checked seed the normal Windows user path, and [ADR 0190](docs/adr/0190-run-root-cupid-tools-from-the-checked-seed.md) moves the root assembler, object, linker, and disassembler commands to the same checked trust unit.

[ADR 0125](docs/adr/0125-represent-decimal-floating-scalars.md) records decimal binary32 and binary64 constants, represented integer conversions, and mixed scalar arithmetic. [ADR 0126](docs/adr/0126-name-fixed-point-sources-consistently.md) records the complete 19-source fixed-point rename and old-seed proof. [ADR 0129](docs/adr/0129-refresh-seed-and-transfer-cupidc-lexer.md) records the promoted seed and the lexer handoff.

[ADR 0128](docs/adr/0128-bound-private-cupidc-statement-depth.md) records the private parser's fail-closed control and statement depth.

[ADR 0131](docs/adr/0131-finalize-c11-external-inline-definitions.md) records C11 external-inline finalization and the unchanged Nuked OPL3 compiler-head proof.

[ADR 0134](docs/adr/0134-refresh-seed-for-shared-x86-and-external-inline.md) records the checked-seed promotion that carries C11 external-inline finalization and shared immediate multiply support.

[ADR 0135](docs/adr/0135-transfer-nuked-opl3-to-cupidc.md) records the Nuked OPL3 production ownership transfer and its image and runtime proof.

[ADR 0136](docs/adr/0136-represent-static-floating-constant-data.md) records exact static `float` and `double` data, signed-zero placement, and direct width conversion.

[ADR 0137](docs/adr/0137-emit-c-floating-comparisons.md) records all six floating comparisons, mixed-width conversion, and IEEE unordered behavior.

[ADR 0138](docs/adr/0138-refresh-seed-for-floating-data-and-comparisons.md) records the checked-seed promotion that carries both floating capabilities.

[ADR 0139](docs/adr/0139-transfer-jpeg-and-glyph-rasterization-to-cupidc.md) records the JPEG and glyph-raster production transfer, closed inputs, deterministic objects, and guest decode proof.

[ADR 0140](docs/adr/0140-expose-ordered-forced-includes.md) records the ordered forced-input driver seam and exact Doom-tree frontier. [ADR 0145](docs/adr/0145-retain-empty-memory-assembly-barriers.md) records the empty compiler memory barrier and the resulting sound-driver object. [ADR 0146](docs/adr/0146-represent-ldmxcsr-memory-inputs.md) records the exact LDMXCSR memory-input boundary. [ADR 0147](docs/adr/0147-evaluate-static-floating-arithmetic.md) records deterministic static floating arithmetic and the resulting automap object. [ADR 0148](docs/adr/0148-represent-movss-float-memory-assembly.md) records the exact MOVSS float-memory boundary. [ADR 0149](docs/adr/0149-gate-doom-implicit-function-declarations.md) records the explicit Doom implicit-call profile. [ADR 0150](docs/adr/0150-represent-x87-sine-memory-assembly.md) records the exact x87 sine memory boundary and completed compiler-head FPU root. [ADR 0151](docs/adr/0151-gate-doom-function-data-pointer-conversions.md) records the profile's function/data pointer rule. [ADR 0152](docs/adr/0152-retain-narrow-bit-field-promotion-provenance.md) records ordinary narrow bit-field promotion. [ADR 0153](docs/adr/0153-represent-union-initializer-lists.md) records one-active-member union initialization. [ADR 0154](docs/adr/0154-represent-x87-round-down-memory-assembly.md) records the exact x87 round-down and control-word boundary. [ADR 0155](docs/adr/0155-represent-task23-file-scope-assembly.md) records the file-scope GNU basic assembly boundary and the Task 23 wrapper proof. [ADR 0156](docs/adr/0156-represent-naked-ipi-wrappers.md) records the exact naked IPI wrapper boundary. [ADR 0157](docs/adr/0157-represent-descriptor-table-segment-assembly.md) records the descriptor-table and segment-register boundary. [ADR 0158](docs/adr/0158-promote-current-toolchain-seed.md) records the clean fixed-point promotion and its post-promotion reproof. [ADR 0160](docs/adr/0160-represent-flags-restore-assembly.md) records the exact EFLAGS restore and `cc` clobber boundary.

[ADR 0159](docs/adr/0159-normalize-gnu-named-assembly-operands.md) records parser-private named GNU assembly operands and canonical numeric validation.

[ADR 0161](docs/adr/0161-represent-x87-double-pow-memory-assembly.md) records the exact double-precision `pow` assembly boundary and the then-named `libm.c` frontier.

[ADR 0162](docs/adr/0162-represent-x87-mixed-width-powf-memory-assembly.md) records the mixed-width `powf` assembly boundary and the following `sqrtsd` frontier.

[ADR 0163](docs/adr/0163-represent-sqrtsd-register-assembly.md) records the exact `sqrtsd` statement and the following `atan2` frontier.

[ADR 0164](docs/adr/0164-represent-x87-atan2-memory-assembly.md) records the exact x87 `atan2` statement and the following `exp` frontier.

[ADR 0165](docs/adr/0165-represent-x87-exp-memory-assembly.md) records the exact x87 exponent statement and the following file-scope mask frontier.

[ADR 0166](docs/adr/0166-represent-fabs-file-scope-assembly.md) records the exact `fabs` mask and wrapper effects and the following `floor` frontier.

[ADR 0167](docs/adr/0167-transfer-fpu-percpu-smp-to-cupidc.md) records the production transfer of the FPU, per-CPU, and SMP roots.

[ADR 0168](docs/adr/0168-represent-fixed-register-input-overlap.md) records compatible fixed-register input and output sharing.

[ADR 0169](docs/adr/0169-represent-libm-rounding-file-scope-assembly.md) records the exact eight-wrapper libm rounding family and the following `fmod` frontier.

[ADR 0170](docs/adr/0170-represent-double-to-unsigned-wide-conversion.md) records the explicit `double` to `unsigned long long` conversion and the resulting complete compiler-head object for the then-named `string.c`.

[ADR 0171](docs/adr/0171-represent-libm-fmod-file-scope-assembly.md) records the exact `fmod` and `fmodf` loops and the following read-only constant frontier.

[ADR 0172](docs/adr/0172-represent-libm-exp-log-file-scope-assembly.md) records the exact exponent/logarithm constants and wrappers and the following `pow` frontier.

[ADR 0173](docs/adr/0173-represent-libm-cdecl-bridges.md) records the final 18 libm cdecl bridges and complete compiler-head object emission for the then-named `libm.c`.

[ADR 0174](docs/adr/0174-promote-libm-capable-toolchain-seed.md) records the poisoned-host seed transition, promoted five-tool set, and post-promotion reproof.

[ADR 0175](docs/adr/0175-represent-kernel-entry-bss-clear-assembly.md) records the exact kernel stack and BSS-clear statement, its entry-only stack contract, and the private compiler-head boot proof.

[ADR 0176](docs/adr/0176-transfer-libm-to-cupidc.md) records the checked production recipe, byte-preserving `.cc` rename, complete frontier, image, and guest libm proof.

[ADR 0178](docs/adr/0178-represent-active-packed-sse2-assembly.md) records the six exact packed SSE2 statement shapes and complete compiler-head SIMD object.

[ADR 0179](docs/adr/0179-promote-bss-and-simd-capable-toolchain-seed.md) records the five-tool seed promotion, direct kernel-entry and SIMD compile proofs, and poisoned-host fixed-point reproof.

[ADR 0180](docs/adr/0180-transfer-kernel-entry-and-simd-to-cupidc.md) records the checked production recipes, byte-preserving `.cc` renames, complete frontier, image, and dual-NIC guest proof.
[ADR 0181](docs/adr/0181-transfer-string-to-cupidc.md) records the final strict-root recipe, byte-preserving `.cc` rename, complete frontier, image, and dual-NIC guest proof.

[ADR 0182](docs/adr/0182-complete-doom-compatibility-object-frontier.md) records pointer-preserving static address casts, exact dglibc jump assembly, and the complete 83-root current-compiler Doom object frontier.

[ADR 0183](docs/adr/0183-promote-doom-capable-toolchain-seed.md) records the five-tool seed promotion, exact checked-seed Doom compatibility objects, and poisoned-host fixed-point reproof.

[ADR 0184](docs/adr/0184-transfer-doom-to-cupidc.md) records the 83-source `.cc` transfer, closed Doom profile inputs, host-free root object graph, image build, and runtime checks.

[ADR 0185](docs/adr/0185-accept-page-aligned-kernel-stack-tops.md) records compiler-head support for a nonzero, page-aligned kernel stack top in the otherwise fixed BSS-clear entry statement.

[ADR 0186](docs/adr/0186-promote-stack-top-capable-toolchain-seed.md) records the five-tool seed promotion, the exact `0x01100000` checked-seed regression, and both poisoned-host fixed-point proofs.

[ADR 0187](docs/adr/0187-expand-kernel-and-relocate-external-elf.md) records the expanded kernel reservation, the stack move to `0x00F00000..0x01100000`, the external ELF move to `0x01C00000`, and the FAT16 move to LBA 20480.

[ADR 0143](docs/adr/0143-share-ordinary-padding-nops.md) records the shared ordinary compiler padding family and its measured disassembly improvement.

[ADR 0144](docs/adr/0144-recognize-exact-clang-prefix-padding.md) records the exact decode-only exception for Clang repeated-prefix padding.

[ADR 0142](docs/adr/0142-freeze-fixed-point-source-closure.md) records the private fixed-point source root, dual closure checks, and complete output publication.

[ADR 0083](docs/adr/0083-shared-x86-conditional-moves.md) records the shared i686 conditional-move family and its exact operand boundary. [ADR 0084](docs/adr/0084-cupidobj-canonical-text-wrapping.md) records canonical embedded text and the byte-exact binary boundary.

### Copying files into the disk image

Cupid OS mounts FAT16 at `/disk` and persistent `homefs` at `/home`.

- `/disk` is the raw FAT16 partition in `cupidos.img`.
- `/home` is `homefs`, serialized into `HOMEFS.SYS` on FAT16.
- On first boot without `HOMEFS.SYS`, `homefs` imports existing FAT16 files.

The FAT16 partition sits at byte offset 10485760 (20480 * 512) inside `cupidos.img`. Use the portable host helper to put files in the FAT16 backend:

```bash
python3 tools/hostbuild.py stage --image cupidos.img --fat-start-lba 20480 myfile.txt:/myfile.txt
```

On Windows, use `python` instead of `python3`. If you prefer `mtools`, point it
at the same offset:

```bash
mcopy -o -i cupidos.img@@10485760 myfile.txt ::/myfile.txt
mdir  -i cupidos.img@@10485760 ::/
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

Press Ctrl+Alt+2 to open the QEMU monitor. `make run` sends serial output to stdout.

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
    util/                  calendar, generated *_programs_gen.cc
  drivers/               hardware drivers: ATA, keyboard, mouse,
                         PIT, RTC, serial, speaker, timer, VGA,
                         PCI, RTL8139, E1000
  bin/                   105 runnable CupidC programs and 22 browser fragments
  demos/                 22 CupidASM demo/include programs
  user/                  example ELF user programs + cupid.h
  wiki/                  documentation (28 Markdown files)
  docs/                  architecture, agent, and bootstrap records
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

The bootloader has two stages and occupies about 4 KB.

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
LBA 5-20479 Kernel binary area
LBA 20480+  FAT16 partition (mounted as /disk)
           homefs persistent container (HOMEFS.SYS), mounted as /home
```

---

## Kernel (kernel/)

### Core

| File | What it does |
|------|-------------|
| `kernel.cc/.h` | kmain() entry, initializes IDT/GDT/PIC/PIT/keyboard/mouse/VBE, starts desktop |
| `idt.cc/.h` | IDT setup, 256 gate descriptors |
| `irq.cc/.h` | IRQ dispatch, handler registration |
| `pic.cc/.h` | 8259 PIC init, IRQ masking, EOI |
| `panic.cc/.h` | Kernel panic with register dump and stack trace |
| `ports.h` | inb/outb/inw/outw port I/O macros |
| `assert.h` | Assert macros |

### Memory

| File | What it does |
|------|-------------|
| `memory.cc/.h` | Physical memory manager, bitmap allocator over 512MB, kernel heap |
| `paging.cc` | Page tables, identity-mapped address space |

The kernel heap uses a bump allocator with a free list. Everything runs at ring 0 in a flat 32-bit identity-mapped address space. The PMM manages 512MB, starts with a 256MB heap, and reserves the 2MB kernel stack at `0x00F00000..0x01100000`.

### Processes

| File | What it does |
|------|-------------|
| `process.cc/.h` | PCB, process list, round-robin scheduler |
| `context_switch.asm` | Saves EBX/ESI/EDI/EBP/EFLAGS, swaps ESP/EIP |

The preemptive scheduler supports up to 32 threads. IRQ0 runs at 200 Hz and provides 5 ms time slices. Process states are READY, RUNNING, BLOCKED, and TERMINATED. Core primitives are `process_create()`, `process_yield()`, `process_exit()`, and `process_kill()`; the quiescent reaper reclaims detached terminated PCBs.

### Filesystem

| File | What it does |
|------|-------------|
| `vfs.cc/.h` | VFS layer: open, read, write, close, seek, stat, readdir |
| `vfs_helpers.cc/.h` | read_all(), write_all(), read_text(), write_text() |
| `ramfs.cc/.h` | In-memory root filesystem, populated at boot with programs/docs/demos |
| `devfs.cc/.h` | /dev entries: null, zero, console, serial, random |
| `fat16.cc/.h` | FAT16: MBR parsing, cluster chains, file read/write/create |
| `fat16_vfs.cc/.h` | FAT16 to VFS adapter |
| `homefs.cc/.h` | Persistent logical filesystem for /home, serialized to HOMEFS.SYS |
| `blockdev.cc/.h` | Block device abstraction |
| `blockcache.cc/.h` | 64-entry LRU sector cache, write-back, flushes periodically |

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
| `graphics.cc/.h` | Pixel, line, rect primitives with clipping |
| `gfx2d.cc/.h` | Gradients (H/V/radial), shadows, dither, alpha blending, file dialogs |
| `gfx2d_effects.cc/.h` | Blur, sharpen, sepia, noise, color manipulation |
| `gfx2d_icons.cc/.h` | Desktop icon registration, hit-testing, drag and drop |
| `gfx2d_assets.cc/.h` | Texture loading and caching |
| `gfx2d_transform.cc/.h` | Scale, rotate, skew, perspective |
| `font_8x8.cc/.h` | 8x8 bitmap font data and renderer |
| `bmp.cc/.h` | BMP codec: 24-bit uncompressed read/write, 32bpp output |

All rendering goes to a RAM back buffer first. `vga_flip()` copies it to the linear framebuffer, and the double buffering prevents tearing.

### GUI

| File | What it does |
|------|-------------|
| `gui.cc/.h` | Window list, z-order, drag, focus, minimize, close (up to 16 windows) |
| `gui_widgets.cc/.h` | Checkboxes, radio buttons, dropdowns, sliders, progress bars |
| `gui_containers.cc/.h` | Panels, tabs, splitters, groups |
| `gui_menus.cc/.h` | Menu bars, dropdown menus, context menus, toolbars, status bars, tooltips |
| `gui_themes.cc/.h` | 7 built-in themes, .theme file load/save |
| `gui_events.cc/.h` | Mouse, keyboard, and window event dispatch |
| `ui.cc/.h` | Higher-level controls on top of the widget layer |

Themes include Windows95, Pastel Dream, Dark Mode, High Contrast, Retro Amber, Temple, and Vaporwave. Theme files can be saved and loaded from disk.

### Desktop

`desktop.cc/.h` handles the desktop shell: animated gradient background, taskbar with clock, icon grid, and the main event loop. On mouse-move it only redraws the cursor, not the whole screen. The background color LUT is recalculated at most every 3-4 animation frames.

### Apps

| File | What it does |
|------|-------------|
| `bin/notepad.cc` | Text editor with menus, scrollbars, clipboard, undo/redo, file open/save |
| `terminal_app.cc/.h` | GUI terminal window: scrolling text buffer, PS/2 input, ANSI color support |
| `ansi.cc/.h` | ANSI escape sequence parser: colors, cursor positioning, screen clear |
| `calendar.cc/.h` | Date/time math, RTC integration, taskbar clock, calendar popup |
| `clipboard.cc/.h` | System clipboard, shared across Notepad and Terminal |
| `ed.cc/.h` | Kernel line editor, separate from the `bin/ed.cc` program |

### Compilers and languages

CupidC (`kernel/lang/cupidc*`) is a compiler for a HolyC-inspired C dialect:

- Single-pass recursive descent compiler
- JIT mode: compile and run .cc files in memory immediately
- AOT mode: compile to ELF32 binaries on disk
- Inline assembly, structs/classes, floats/SIMD, constant expressions, labels/goto, and full ring-0 kernel bindings
- Limits: 1MB code, 8MB data/string storage, 1024 functions, 4096 symbols per unit

CupidASM (`as*.cc`) is an Intel-syntax x86-32 assembler:

- Expanded x86-32 integer/control-flow/system/FPU/SSE/atomic coverage
- JIT and AOT (ELF32) modes
- Directives: `%include`, reserve aliases, `times`, alignment
- Forward references, up to 8192 labels
- Kernel bindings for print, malloc, VFS, graphics calls

CupidScript (`cupidscript*.cc`) is a shell scripting language for `.cup` files:

- Variables, if/else, while, for loops
- Functions with parameters and return values
- Pipes (|), redirects (> and >>), background jobs (&)
- Arrays, string operations
- Calls shell commands and kernel functions directly

CupidDis is the shared x86-32 disassembler and ELF inspector used by the hosted CLI and the kernel `dis` and `exec -d` adapters. Raw input accepts one 16-bit or 32-bit mode, or an ordered mode map for a flat image that changes modes. The hosted form is `cupiddis --raw --mode 16|32 [--mode-at OFFSET:16|32]... --base ADDRESS FILE`. The caller supplies instruction-boundary offsets; CupidDis validates ordering and range bounds but does not infer transitions from bytes. The shared x86 model covers all sixteen i686 conditional moves for 16-bit and 32-bit register or memory sources. It also covers three-operand `IMUL` with same-width register or memory sources, using `69 /r` for a full immediate and `6B /r` when the value fits a sign-extended byte. Ordinary compiler padding includes plain `90`, `66 90`, and word or doubleword `0F 1F /0` register and memory forms. A private 32-bit decoder exception recognizes the five exact Clang forms with two through six leading `66` bytes and the fixed `2E 0F 1F 84 00 00 00 00 00` tail. Other repeated prefixes remain invalid, and CupidASM cannot emit the redundant forms. CupidASM accepts the conditional-move aliases, chooses the shortest valid multiply encoding, and applies the current mode's default width to a memory NOP. Source head has 589 forms and fingerprint `22C336A0`, including 80-bit x87 memory forms for automatic `long double` values. The repository seed retains the earlier 587-form catalogue and rebuilds the current model during the checked fixed point.

### Program execution

| File | What it does |
|------|-------------|
| `exec.cc/.h` | Fixed-address ELF32/CUPD loader: validated segments, staged ELF loading, BSS zeroing, and image/lease lifetime transfer |
| `syscall.cc/.h` | Syscall table passed to ELF programs as a struct of function pointers |

### Shell (kernel/lang/shell.cc)

The shell handles command parsing, pipelines, input/output redirection, background jobs, history with arrow-key navigation, and tab completion. Typing a .cc filename runs it through CupidC JIT. Typing a .asm file runs it through CupidASM JIT. Typing a .cup file runs it through CupidScript.

### Utility libraries

| File | What it does |
|------|-------------|
| `string.cc/.h` | strlen, strcmp, strcpy, strcat, strtok, strstr, sprintf and more |
| `math.cc/.h` | 64-bit integer math, g2d_isqrt(), trig approximations, itoa/atoi |

---

## Drivers (drivers/)

| File | What it does |
|------|-------------|
| `vga.cc/.h` | VBE 640x480x32bpp, double-buffering, vsync via Y_OFFSET page flip |
| `keyboard.cc/.h` | PS/2 keyboard on IRQ1, scancode to ASCII, modifiers, key repeat, circular buffer |
| `mouse.cc/.h` | PS/2 mouse on IRQ12, 3-byte packet parsing, scroll wheel, cursor |
| `pit.cc/.h` | 8254 PIT channel 0 at 200Hz, channel 2 for speaker |
| `timer.cc/.h` | Tick counter, sleep(), multi-channel timer callbacks |
| `speaker.cc/.h` | PC speaker beep via port 0x61 |
| `ata.cc/.h` | ATA/IDE PIO, 28-bit LBA, IDENTIFY, read/write on primary channel |
| `rtc.cc/.h` | Real-time clock from CMOS, BCD to binary, NMI masking |
| `serial.cc/.h` | COM1 at 115200 baud, used for kernel debug output |

---

## Built-in programs (bin/)

RamFS contains 105 top-level CupidC programs that the shell can run directly. It also contains 22 support modules under `bin/browser/*.cc`, which `browser.cc` includes rather than launching as separate programs.

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
| Subsystem smoke tests | feature17_iso (ISO9660), feature18_swap (swap), feature19_usb (USB), feature20_smp (SMP), feature21_net (TCP client), feature22_net_server (TCP server), feature23_full_access, feature24_widetypes, feature25 |
| Networking utilities | arp, curl, cupidfetch, ifconfig, netstat, ping, resolve, ssh, telnet, wget |
| Text/documentation viewers | auto, bible, oracle |
| Test programs | dglibc_test, kbdsub_test, test, test_fpaug, test_print |

---

## Assembly demos (demos/)

RamFS contains 22 CupidASM programs. Run one from the shell with `as <name>.asm`:

hello, loop, fibonacci, factorial, bubblesort, stack, data, math, include_feature, include_helper, jcc_aliases, asm_compat_reserve, reserve_directives, fs_syscalls, syscall_table_demo, syscall_vfs_extended_demo, parity_core, parity_diag, parity_gfx2d, parity_priv, fpu_kernel, simd_blur

---

## User programs (user/)

The `user/` directory has three example ELF32 programs: `hello.cc`, `cat.cc`,
and `ls.cc`. Its `cupid.h` header defines the syscall-table ABI. `make -C user`
first compares that header with the kernel types, syscall table and
initializer, VFS declarations, and socket constants. It then compiles the
sources with CupidC and links them with CupidLD. Linux runs the checked seed
directly, while Windows runs it through WSL.
`make test-user-native-windows-equivalence` builds the optional native hosted
drivers and checks every object and executable against the seed. The host
compiler is needed only for that comparison and the hosted Toolchain. The
current ABI is version 5 with 103 fields and a 412-byte i386 table.
The generated `user/build/` directory is ignored by Git, so rebuild the
programs before staging them into an image.

External executables must be linked for the current
`0x01C00000..0x01E00000` arena. Binaries linked at an earlier fixed base must
be rebuilt.

---

## Memory layout

```
0x007C00            Stage 1 bootloader (512 bytes)
0x007E00            Stage 2 bootloader (2KB)
0x100000            Kernel start (_start)
                    .text, .rodata, .data
                    .bss
0x00F00000-0x01100000 Kernel stack (2MB, grows down; 16-byte guard)
0x01100000-0x01A00000 CupidC JIT/AOT region (1MB code + 8MB data)
0x01A00000-0x01C00000 CupidASM JIT/AOT region (1MB code + 1MB data)
0x01C00000-0x01E00000 External ELF arena (exclusive fixed-address lease)
0xE0000000+         VBE linear framebuffer (address comes from BIOS)
```

---

## Interrupt handling

The IDT has 256 entries. CPU exceptions occupy entries 0 through 31, and remapped PIC IRQs start at 32.

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

1. Add the source and header files to `kernel/` or `drivers/`
2. Add the source file to the object list in the Makefile
3. Run `make`

New CupidC programs go in bin/ and are automatically embedded in RamFS at build time. New assembly demos go in demos/. CupidObj stores these text files with LF line endings even when a checkout uses CRLF. CupidScript files use the .cup extension and can be placed anywhere on the VFS.

---

## Requirements

- Python 3
- GNU Make
- WSL on Windows, used to run the checked static i386 Linux tools
- QEMU (`qemu-system-i386`, runtime/testing only)
- GCC with 32-bit support and its linker on Linux, optional for native
  Toolchain oracles and baseline capture
- LLVM (`clang` and its linker) on Windows, optional for native Toolchain and
  user-program equivalence oracles
- GNU `nm` or `llvm-nm` (optional comparison oracle)
- NASM (optional, for `make nasm-assembly-oracle` parity checks)
- mtools (`mcopy` and `mdir`) is optional for manual FAT16 image inspection and copying
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
