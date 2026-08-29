# Architecture

cupid-os is a monolithic, single-address-space, ring-0 operating system for 32-bit x86. The kernel, drivers, shell, and applications all run in the same flat memory space with full hardware access.

The current build graph contains 452 transforms, including 443 under root
`all`. CupidBuild directly coordinates the ISR and context-switch object
publications plus the bootloader and SMP raw-image publications. It also runs
186 direct CupidObj recipes and both normal kernel links, bringing its total to
192 transforms and leaving Python in 260. CupidASM remains the language owner,
and CupidDis remains the
inspection owner for the four guarded assembly publications.

Source-head CupidBuild also carries a typed JPEG publication transaction. It
fits the existing guarded host layer with one asset, one manifest, and six
tools. Paired seed promotion is still required before that source capability
can replace the normal Python coordinator.

The source-head fixed-point closure contains 59 inputs with snapshot SHA-256
`3c3218219472735ba1073e1ca7b1f67ee75bf123fb0be77d2c65e019a6aebdef`.
CupidBuild accepts a promoted v2 manifest whose `source_input_count` is 58 or
59 and rejects 57 or 60. This keeps the active 58-input pair usable while
making a later 59-input promotion consumable. The first promotion attempt
failed closed with `fixed-point provenance differs`, so no invalid seed became
active. The normal JPEG recipe remains Python-owned. ADR 0366 records this
compatibility boundary.

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

The promoted CupidBuild seed owns the checked transactions for the 2,560-byte
boot image and 4,096-byte SMP trampoline. CupidASM writes each private image
and its typed v2 range map. CupidDis requires known instructions, local
targets, and source-resolved edges before publication. The boot map contains
nine local relative, two local far, and one external far edge. The trampoline
map contains four local relative, one local far, and one indirect unprovable
edge. A failed check preserves the previous image, and the map stays private.
ADR 0305 records raw local-target adoption, ADR 0340 records source-resolved
edges, ADR 0356 records seed carriage, and ADR 0357 records direct publication
ownership.

The public in-kernel raw adapter accepts fixed 16-bit code, fixed 32-bit code,
or the same typed code16, code32, and data range records used by the shared
inspector. A caller may require complete known-instruction coverage. CupidDis
then checks its typed summary and returns before printing a listing if selected
code is unknown, invalid, or truncated. The older JIT adapter remains
permissive fixed-32. ADR 0334 records this boundary.

Checked CupidDis can also validate static ELF code anchors. Its explicit
policy checks the kernel entry and every defined function symbol against
decoded starts in file-backed executable load regions. It reports an address
outside code separately from an address in the middle of an instruction. The
normal two-ELF kernel transaction selects this policy together with linked
local-target validation. ADR 0320 records the rule, and ADR 0323 records its
promotion and adoption.

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

The current 156-source production build passes. The broader two-pass frontier
targets 156 sources and 312 checked compilations. Its latest rerun exceeded
2,340 seconds without a compiler diagnostic and remains incomplete.

The normal Toolchain root builds fifteen published `.cc` contracts and the
runtime probe with stage-three and stage-four CupidC. It runs and publishes the
stage-four cohort. Its publisher accepts only a
dedicated `cupidc-contracts` directory inside the source tree. It validates
the target before work and again before promotion, and an existing
destination must already verify as a complete cohort. Arbitrary directories,
source trees, files, and symbolic links remain untouched. Exact initial,
private, and newly discovered contract inventories catch additions, removals,
and restored edits that changed a copied input. Every contract run derives the
cohort from its executable, requires a named manifest artifact, and verifies
all artifact hashes, the source-current 76-input publication set, the checked seed
manifest, and the 59-file candidate source inventory before execution. The
contract inventory includes the small Windows probe, the native Windows tool
runtime and startup, CupidLD publication runtime and bridge, direct contract,
`direct.h`, `windows.h`, hosted `stddef.h`, both PE32 reader headers, the CupidBuild declarations
and Windows startup, the user syscall ABI contract and its six declarations,
the Toolchain Makefile, the publisher, and the independent Python ABI oracle.
One captured seed-manifest byte sequence supplies the digest, decoded data, schema
checks, and build plan. Seventeen objects and sixteen executables must match
across stages before the 22-artifact candidate cohort can be published. Contract runs
use a private copy of the verified cohort. The user ABI check also gives the
Cupid contract and Python oracle one shared six-file snapshot. Linux runs the
published ELF contract. Windows freezes a separate 27-file closure, builds a
private PE with checked CupidC, CupidASM, and CupidLD, and runs it directly.
The Windows path rechecks its source and seed closures and never touches the
Linux publication. For publication, the checked stage-four Linux tools build
the manifest author from its direct inputs. Linux runs a static ELF. Windows
runs a validated native PE built from the same author source, checked Windows
runtime, startup, and exact imports. Its framed `CUPMAN4` request binds the
publication facts and raw stage-three and stage-four bytes for 62 fixed-point pairs: 17 contract
objects, 16 contract executables, 22 bootstrap C objects, one startup object,
and six tool images. The author requires regular, nonempty, byte-identical
streams and hashes both sides independently. It derives the 17 schema-v3
object records, checks executable pairs against their artifact facts, and
derives the fixed-point summary from the exact pair inventories. The request
has no caller `all_equal` field. Python repeats all 62 comparisons after author
acceptance and retains no-follow capture, launch, drift checks, private
staging, rollback, and atomic replacement. A failure preserves the prior
publication. The host-selected checked
seed builds a separate `CUPMAN2` verifier as a static ELF on Linux or a native
PE on Windows. ADR 0302 records the verifier boundary, ADR 0304 records the
author split, ADR 0307 records raw stage-pair evidence, and ADR 0322 records
native Windows author execution.

The latest complete schema v3 `CUPMAN4` publication passed and wrote 22
artifacts from 75 publication inputs and the exact 58-file bootstrap
inventory. The Cupid author and Python oracle agreed on all 62 stage pairs.
Its 29,271-byte manifest has SHA-256
`5fab9706abe6d938e9aa4a355ebbae293fee5404475d3d20d2591d6a9e464011`.
Its final verifier accepted all 22 artifacts.
Both checked Python contract launchers resolve `tools` from this checkout. The
direct contract suite passes 40 tests in 54.623 seconds. The publisher suite
passes 64 tests in 12.144 seconds, and the pinned verifier runner passes 25 tests
in 32.773 seconds with three POSIX-only Windows skips. ADR 0311 records this
host import boundary.

Audit ownership for author generation stops at the 20 direct build inputs. The
historical 70 publication and 50 bootstrap inputs are observations and do not inherit
compiler or assembler ownership from that transform.

The publication records 75 publication inputs, 58 candidate inputs, 22
artifacts, and 62 stage pairs. The active v2 seed carries CupidBuild, which now
owns four guarded assembly publications and runs 186 direct CupidObj recipes.
The stable audit counts cover 748 active language inputs, 452 transforms, 255
features, and 28 unreachable inputs.
CupidC participates in 250 transforms,
CupidObj in 192, CupidASM in nine, CupidLD in nine, CupidDis in nine, and
CupidBuild in 192. Four transforms use Cupid-built semantic contracts. Python
participates in 260, but no transform is Python-only. All 443 transforms under
root `all` have a Cupid participant.

The first attempt at this audit stopped after 65.183 seconds because the test
still locked the old artifact-size recipe. The audit and its test now require
one `$(ARTIFACT_SIZE_CONTRACT)` command. That wrapper captures the Linux policy
manifest and the complete checked Windows seed cohort in one transaction. Its
`CUPSIZE2` request gives the CupidC-built contract the Windows manifest, Linux
parent digest, and six regular-file size and digest observations. The contract
validates the Windows target, provenance, exact inventory, and observed bytes.
The focused modules contain 54 tests. They pass with four platform-specific
skips. At that checkpoint, the source-head artifact contract passed
against all sixteen exact artifacts. Its 3,382-byte policy had SHA-256
`78d1d4cc4b5411cc73523b88166e75fba876b2cd78f1d9c9118b1367fa86ec21`
and covers 38,144,480 bytes across those paths.

| Checkpoint artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 9,605,148 | `e54c2fcefb432bc0cab314411a4dfb0dda377169c613497487f0eb6ec75c4b63` |
| `kernel/kernel.elf` | 9,736,220 | `c4004b2b9b003b8c0174a32d11948a228b50ec57e97d69532c2c514834adc436` |
| `kernel/kernel.bin` | 9,507,804 | `2efdc4df2a71cc6e889acd67f9322bf449692ee046d089762df3575dba90143f` |
| `cupidos.img` | 209,715,200 | `1276de1dc03ed01cbcc90e95e9a4d0b71abd0751bd9c74251ab0ccac2719c9bc` |

That normal build completed the 431-input local-target and code-anchor
scan, accepted the exact policy, and preserved the image's FAT contents while
staging `hello.iso`. A private four-vCPU `max` and E1000 copy brought all CPUs
online and completed the full graphics, audio, and in-OS CupidC frontier. The
framebuffer changed 69,823 pixels; both audio captures were non-silent. Its
147,526-byte log has SHA-256
`252d3ef3796233cd752754c19aaa85a7311010bd75d0d5d57264fd6919584b56`
and no rejected runtime marker.

The preceding source-head cohort used the same pass-one and final ELF sizes
with SHA-256 values
`c694ba70e37e711c6490db2f17fb1869300c26703017cb1bd8adb3b51cce1c60`
and `bbdb974acc12bf57dd95bff4f734ad9ad75dd8d11dfec3267a22df455666f459`.
Its 9,504,760-byte raw kernel had SHA-256
`346063e08f2fef5550e2a40b8edb1ea7cbe2f242a3084f1137a9552ed7baf84a`,
and its disk image had SHA-256
`aa9bc411d48625837b511b32444019f0aa555a48fc5aaa2400c9259dd8607333`.
That image passed a private four-vCPU E1000 frontier smoke covering SMP,
terminal startup, framebuffer output, AC97, and PC speaker audio.

`make bootstrap-audit` and `make check-bootstrap-audit` both pass. The
generated audit records 23 failure groups, six help groups, and 29 success
groups on Linux. It records 12 failure groups, six help groups, and 16 success
groups on Windows.

The exact artifact-size policy covers sixteen paths: four OS artifacts, six
Linux seed executables, and six Windows seed executables. The wrapper passes
the captured Linux manifest to the Cupid contract, validates the captured
Windows manifest and six PE images, and rechecks both trust units before
success. ADR 0305 established and first carried the earlier fourteen-path
closure.
ADRs 0312 and 0318 preserve earlier promotions, ADR 0323 records the preceding
code-anchor pair, and ADR 0336 records the earlier five-tool pair.

The current seed pair uses the Linux and Windows v2 schemas and lists six tool
images, including CupidBuild with a non-producing fixed-point plan role. Both manifests bind revision
`a4eee4c2c4b8f1cbb7ca22fbe7688f5958912e4f`, 58 inputs, and source snapshot
`a2e8e5c97672c2d0bd8ba4f4166860cc9686a1838cefeab8bc46d5b1c9fbe09d`.
The Linux plan has SHA-256
`52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd`;
its 6,602-byte manifest has SHA-256
`f1bee18b9b1506ff5a665e76d57d028702ae7c701c4e9d432ed4b87c68ee258b`.
The Windows native plan has SHA-256
`f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14`;
its 2,852-byte manifest has SHA-256
`917817122a36331a0ec77ba06d6ce40a8eacacc4224d8ed468d8d77272b8b974`
and pairs to the exact Linux manifest bytes. Candidate proof and promoted-seed
self-consumption pass on both platforms, with all six initial images equal to
stage two. CupidBuild publishes the two assembly objects and both raw images
directly, while Python retains the other guarded paths. ADR 0356 records the
active seed refresh, and ADR 0357 records the raw recipe transfer.

The preceding poisoned-host `make -j4 all` checkpoint passed in 684.260 seconds.
All fourteen policy artifacts matched their exact sizes. The 2,960-byte policy
had SHA-256
`b23bdcb3757a7ddc2a49eeef51cad48cdbd6899f0080c75896b67ef0c665da6e`.
Its 39 focused tests passed in 2.739 seconds, with two skips. These figures are
retained as checkpoint history.

| Preceding checkpoint output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/smp_trampoline.bin` | 4,096 | `b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90` |
| `kernel/kernel.elf.pass1` | 9,320,424 | `3f9a1c681fbcfb1aa453e42a9d77ed1069b9a487110c9ec22ac318d278bdd1e6` |
| `kernel/kernel.elf` | 9,447,400 | `92d4e2f890b657c9881eb2184c7f8f9f0e96b18b5b060dbabab17e7ea305b1ce` |
| `kernel/kernel.bin` | 9,224,756 | `4d53e0456d8e63e140f6dcab135765662d12df6e4a83b246409572501f3b4cbd` |
| `cupidos.img` | 209,715,200 | `43409d159d2da70feb20deccda0d79a695c6ab56d87a179fe21a66ab40c5eedd` |

A private four-vCPU `max`/e1000 smoke of that image passed in 64.601
seconds. It printed the direct, named, and typedef callback markers in order,
including `[feature14-callback-typedef] PASS float4=4 calls=1`, followed by
`PASS feature14_simd` and `[cupidc] JIT execution complete`. No reject marker
appeared. The source image was unchanged. The 33,219-byte log has SHA-256
`e39a1905002c2baa483c65eb6e763f4f62907c22f8954873dbb20f4ba5a53e93`.

The pre-documentation artifact gate passed in 651.3 seconds and accepted all
fourteen exact paths. It measured `kernel/kernel.bin` at 9,225,092 bytes. The
pinned contract runner passed 24 tests in 27.752 seconds, and the complete
artifact group passed 45 tests in 2.557 seconds.

The preceding integrated fully poisoned `make -j4 all` first reached the exact-size gate
with three rebuilt kernel outputs. The pass-one ELF measured 9,345,464 bytes,
the final ELF measured 9,472,440 bytes, and the raw kernel measured 9,251,100
bytes. The artifact group passed all 46 tests in 4.160 seconds, with four
expected Windows skips. After those three rows were updated, the repeated
poisoned build passed in 874.531 seconds. All fourteen artifacts matched the
policy, the existing FAT contents were preserved, and `hello.iso` was staged.

| Historical integrated output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/smp_trampoline.bin` | 4,096 | `b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90` |
| `kernel/kernel.elf.pass1` | 9,345,464 | `5dbd2c5acb7b1604cf6daf6f311e88015d0762125c60920da3737d7e10d76f06` |
| `kernel/kernel.elf` | 9,472,440 | `5810ddcb963cfadb4fea3b1343bb38c17ce3f762a48f25615b3feb653f1638e3` |
| `kernel/kernel.bin` | 9,251,100 | `4014b1b2acf34be4dd7483fb8aa9e8a8b0e76eea771c83669571cbf7b66fe0e3` |
| `cupidos.img` | 209,715,200 | `31b25b6881419b1bb8a04b2b3765323b21c5706ac114af1a07b514dcdcd07ea3` |
| `bootstrap/artifact-size-policy.json` | 2,960 | `7b12be6d0dd33f9016ecb4287f5c9414e1da79ffc61e7957aab60cea94850474` |

The integrated strong full private frontier smoke passed in 883.513 seconds
with e1000, four `max` vCPUs, SMP and frontier checks, and the private USB
fixture. The 640-by-480 framebuffer changed 89,630 pixels. AC97 produced
36,877,878 stereo 44.1 kHz frames with a peak of 25,600; the PC speaker produced
76,251 stereo 44.1 kHz frames with a peak of 29,912. The expected direct-call,
named-callback, typedef-callback, global-callback, automatic-callback, and
overall feature14 PASS markers each appeared once and in order. The feature run
then printed a clean JIT completion. The 161,418-byte log has SHA-256
`bc30f5083b96a36362bec5975c0a88437c4f23515de329328bb03d8f6c3e9326`.
The source image was unchanged at SHA-256
`31b25b6881419b1bb8a04b2b3765323b21c5706ac114af1a07b514dcdcd07ea3`.

Fourteen ordinary contract compiles use the worker pool and 900-second
budgets. The pool drains before `cupidc-object` receives an exclusive
1,800-second compile. Runtime compilation and parallel contract linking keep
their 360-second limits. ADR 0282 records this admission policy.
The kernel ELF contract plan explicitly carries `as_elf`, CupidLD, CupidASM,
x86, and ELF32. This matches the native closure and prevents a staged link from
reaching publication with a missing strong definition.
Its v2 manifest also binds stage three and stage four as the compared
fixed-point pair. The verifier checks that exact provenance before directory
promotion.
The preceding full publication gate passed in 4,589.9 seconds. It compared stage-three and
stage-four contract outputs, ran and published stage four, verified 21
artifacts from 65 inputs, and matched all three native Windows user programs
to the checked seed at both object and executable boundaries. The warmed path
passed in 12.2 seconds.
Native contract binaries are optional oracles.

At the promoted-seed checkpoint, stages two and three linked matching native
Windows images for all five hosted tools from Cupid-built objects. CupidASM
supplied the entry and imported API bridges, while CupidLD authored each PE
image and its IAT slots. CupidLD added four publication imports to the shared
twelve. Windows ran help plus a useful success and failure path for each tool.
CupidDis also checked quoted raw-input parity with the Linux tool. These five
images formed the preceding checked Windows execution seed used by
output-bearing production recipes. Most of the Toolchain contract cohort still
runs the Linux seed through WSL. The user ABI, artifact-size, and Toolchain
manifest gates build and run temporary PEs without WSL. The manifest verifier
and author still bind the Linux publication seed.
Source head freezes the
PE execution seed and the Linux plan
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
Linux later passed a 1,294.3-second clean proof, promoted the stage-four seed,
and passed a 1,473.9-second reproof with all five initial seed comparisons
true. Native Windows then passed a 1,253.4-second clean proof, promoted its
stage-four PE32 seed, and passed a 1,061.3-second reproof with all five initial
seed comparisons true. Its 2,118-byte manifest has SHA-256
`ae1d3dfb10604bba419c5936884668d10595f6c671915a4ae5f16706204bb41e`.
Both reproofs reject executable relocations without decoded field owners. ADRs
0280 and 0281 preserve the preceding promotions. ADR 0292 records that
strict-relocation promotion.

The earlier v1 Linux and Windows seeds both bound revision
`a17c9465911da41d59b7ada71733d36c39faa5ea` and exact 50-input snapshot
`46c5335c80d822dd5085ee22077486ea647e5396482d42454847c87e4222aa67`.
The 5,573-byte Linux manifest had SHA-256
`b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b`.
The 2,118-byte Windows manifest had SHA-256
`751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292baed0241967f772eaef`
and named that Linux manifest as its parent. ADR 0336 records these identities.

The first complete run of the intermediate 86-test seed suite took 2,394.660 seconds and reported
failures from stale test data. The tiny source roots were
missing the Windows publication startup and runtime inputs, and the promoted
Windows CupidASM report still expected the older four-byte output. The repaired
report requires six bytes with SHA-256
`95d76dfca4cb4f279611a6ea7a86202898305a4906c6c822c1bfce2ec9ecf06b`.
Six focused source-freeze and PE tests passed in 0.736 seconds. The isolated
fixed-point test passed in 1,187.863 seconds, and that suite passed all 86 tests
in 2,444.917 seconds. After the relocatable-object cases were added, the
ADR 0312 checkpoint passed all 89 tests in 3,145.502 seconds. The complete
source-head module later passed all 92 tests in 2,820.626 seconds. Those v1
seeds carried that snapshot, and the kernel publisher applies the linked
local-target and code-anchor rules before flattening. ADR 0323 records the promotion and
adoption. The current source-head module also covers grouped runtime addresses
and native Windows diagnostic paths; all 99 tests pass in 3,377.405 seconds.

The earlier dual-NIC four-vCPU evidence predates this seed promotion. Those
checks passed through SMP, RDRAND, all 62 crypto checks, USB
storage, audio, TrueType glyphs, a baseline JPEG decode, the desktop, terminal,
and in-OS CupidC. They require `[fpu] SSE2 enabled`,
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
