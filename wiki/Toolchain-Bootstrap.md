# Toolchain Bootstrap

Cupid OS carries a checked static i386 Linux seed for its five hosted tools:
CupidC, CupidASM, CupidDis, CupidLD, and CupidObj. The seed starts a complete
toolchain rebuild without GCC, Clang, NASM, a host linker, `nm`, or `objcopy`.

The seed lives under `bootstrap/seeds/i386-linux/`. Its manifest records each
file's size and SHA-256 value, the static i386 Linux ABI, entry point, source
revision, producer lineage, all 19 C sources, startup, include arguments, and
the exact link order for every tool. Verification pins every source name,
path, position, and GNU-mode flag rather than trusting a manifest-supplied
digest alone. It also rejects unknown fields, dynamic images, an interpreter,
duplicate JSON keys, numeric and Boolean type substitutions, writable
executable load segments, entry points outside executable file bytes, unlisted
ELF files, and unexpected target metadata.

```sh
make verify-bootstrap-seed
```

This command validates the seed without executing it.

```sh
make bootstrap-from-seed
```

The full command captures the hashes of all 40 current source inputs: 19 C
sources, startup, 19 project headers, and `link.ld`. Checked CupidC compiles
stage two, checked CupidASM assembles its startup, and checked CupidLD links all
five tools. The stage-two producer trio repeats the build for stage three.

The gate compares all 19 C objects, both startup objects, and all five linked
images. It also runs five help checks, ten successful operations, and six
failure cases across compilation, assembly, disassembly, symbol inspection,
linking, wrapping, and flattening. A source edit during either stage stops the
build instead of publishing mixed evidence.

Before execution, the harness reads the manifest and each seed binary once. It
verifies those captured bytes, keeps the manifest hash, and runs private copies
of the five binaries. A later replacement of a checked-in file cannot change
that run.

The default output is `build/bootstrap/checked-seed/`. It contains both stages,
the behavior fixtures, and `bootstrap-report.json`. The report keeps the
historical seed source revision separate from the current source snapshot.

Linux runs private copies of the static tools directly. Windows stages each
copy in a mode-0700 WSL directory created by `mktemp`. Native Windows seed
executables are not available yet.

This seed makes the hosted static toolchain reproducible from a clean checkout.
It does not complete the normal OS build migration. Native contract runners,
hosted development commands, and the remaining normal C objects still use a
host C compiler.

The production boot source assembles to an exact 2,560-byte image with SHA-256
`b3e3f6f2897cd5980394e4d1a0e2f94bf6ac6d7ae9aafa5d6de1fc326a5b3442`.
CupidASM and the optional NASM oracle produce the same bytes for the current
`0x00E00000` boot-stack layout.

The checked seed includes the active CSPRNG assembly, operand-free
function assembly, per-CPU pointer output, integer atomics through fetch-or, and
width-aware port I/O. Its stage-three CupidC image is 1,950,556 bytes with
SHA-256
`f4d49d8b870868ccd57aed94eaf7565404ceb10732c79c868e65f9beca5371c8`.
It came from stage three of the checked bootstrap at revision
`10d2412ece22968e03dbe22b048c3d92f210f2ba`, not from the native compiler
candidate. With host compiler and linker commands poisoned, all five seed
images match stage two. All 19 stage-two C objects, startup, and five images
then match stage three, and both stages pass all 21 tool behavior cases.

The refreshed seed represents operand-free GNU assembly statements inside
functions and emits exact PAUSE, NOP, STI, HLT, CLI, CLD, SFENCE, and FNINIT
sequences. The normal build uses that path for e1000, the desktop shell,
the socket layer, and TCP. The earlier detached hybrid linked the same four
objects through both CupidLD passes and booted them under QEMU before the
ownership hand-off.

Compiler head also handles the exact per-CPU pointer output
`mov %%gs:0, %0` with one four-byte `=r` object or `void` pointer. The frontend
and IR preserve its pointer type and evaluate the destination once. The x86
model emits `65 A1 00 00 00 00`.

The checked compiler's atomic slice handles integer load, store, exchange,
and fetch-add builtins with constant orders. Its i386 path selects ordinary
loads and release stores, memory `XCHG`, and `LOCK XADD`. That brings
unchanged `acpi.c` and `mp_tables.c` through deterministic i386 ELF32 object
emission. The normal Make graph owns both through the checked seed. A
four-vCPU image boots every discovered CPU and completes the normal e1000,
desktop, terminal, and CupidC runtime smoke.

Compiler head adds `__atomic_fetch_or` at the same one-, two-, and four-byte
integer widths. It emits a `LOCK CMPXCHG` retry loop because `LOCK OR` cannot
return the old value. Exact byte and execution checks cover a competing
update, signed narrow results, guard bytes, one-time operand evaluation, and
callee-saved EBX. The checked stage-three seed carries this operation and
compiles the active EHCI path.

Compiler head and the checked seed parse all eight helpers in unchanged
`kernel/core/ports.h`. It retains the 8-, 16-, and 32-bit accumulator lanes,
the 16-bit DX port, the read/write ESI or EDI buffer, the read/write ECX
count, and the INSW memory clobber. Scalar port I/O and the CLD plus REP word
forms emit through the shared x86 model. This brings the active non-Doom
header gate to 154/154 at compiler head.

The normal image has a 40-source checked CupidC production boundary. It
contains all 20 crypto units, ACPI and MP-table discovery, e1000, the desktop
shell, the socket layer, TCP, ATA, keyboard, mouse, PCI, PIT, RTC, RTL8139,
speaker, VGA, AC'97, the system-call path, the shell, EHCI, and UHCI. The
strict frontier compiles each approved source twice and accepts 675,340
byte-identical i386 ELF32 bytes. It freezes 328 inputs with SHA-256
`3dedac2c0a5733f531871b6bc83ebb427b92e6dfa448edc93a7804ec28025032`.
Forced Make runs with the host compiler command poisoned cover every
production wrapper recipe. Each recipe lists its exact recursive header
closure.

The QEMU runtime contract passes on four vCPUs with both e1000 and
RTL8139. Each run proves ACPI and MP discovery, every secondary CPU online,
RDRAND, the 62 crypto checks, keyboard and mouse detach and reattach, ATA
storage, AC'97 and PC speaker audio, six EHCI storage lifetimes, a
zero-padded RTC timestamp, and DHCP traffic through the selected NIC. The
gate rejects SMP, storage, crypto, exception, panic, corruption, and
illegal-instruction failure markers. The X.509 checks exercise parser,
hostname, chain state, and embedded-root lookup paths. They are not a full
trust-validation claim.

The current audit assigns 40 transforms to CupidC, 257 C transforms to the
host compiler, 49 transforms to host Python, and 205 root or user objects to
host-built C.
