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
hosted development commands, and 93 normal C root objects still use a host C
compiler.

The production boot source assembles to an exact 2,560-byte image with SHA-256
`9545d6a2f44404af85bb3fd568f1b2d7215b7cd1af2933f7ae5a877353dc95fc`.
CupidASM and the optional NASM oracle produce the same bytes for the current
`0x00F00000` boot-stack layout.

The checked seed includes decimal floating scalars, the active CSPRNG
assembly, operand-free function assembly, per-CPU pointer output, integer
atomics through fetch-or, and width-aware port I/O. Its stage-three CupidC
image is 2,080,288 bytes with SHA-256
`e4eb5b0846a580bb5a2826c97ce646eedec1a077581cb6e87dada6845806761b`.
It came from stage three of the checked bootstrap at revision
`fe3bdfe451d7e019a052c7c8ba53f1f9f3f1fb3d`. It also carries GNU `used`,
privileged-register inputs, FXSAVE, call-next capture, GNU `Nd`, and
machine-state memory outputs. With host
code-generator commands poisoned, all five seed images match stage two. All
19 stage-two C objects, startup, and five images then match stage three, and
both stages pass all 21 tool behavior cases.

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

Compiler head handles the independent `r` and `c` inputs used by
`kernel/cpu/idt.cc`, `kernel/mm/paging.cc`, and `kernel/smp/lapic.cc`.
Exact CR0, CR2, CR3, and CR4 moves and RDMSR emit directly into deterministic
i386 ELF32 objects. The three double-compiled objects are 8,756, 2,336, and
4,184 bytes and pass the shared validator. Focused frontend, Linear IR,
object, and decoder contracts cover the supported forms and their failures
without executing privileged instructions. The normal recipes now compile
all three roots with the checked seed.

Compiler head handles the exact volatile `fxsave (%0)` form used twice in
`kernel/core/process.cc`. Its independent `r` input must be a
four-byte object or `void` pointer, and the statement must retain its
`memory` clobber. The emitter places the pointer in EAX and asks the shared
x86 model for `0F AE 00`. Two full-profile compiles produce the same
validated 30,216-byte object, with FXSAVE at text offsets `0x1967` and
`0x4d7c`. The normal recipe now compiles this root with the checked seed.

The checked compiler's atomic slice handles integer load, store, exchange,
and fetch-add builtins with constant orders. Its i386 path selects ordinary
loads and release stores, memory `XCHG`, and `LOCK XADD`. That brings
unchanged `acpi.cc` and `mp_tables.cc` through i386 ELF32 object emission.
The normal Make graph owns both through the checked seed. Their renamed paths
must pass the final four-vCPU e1000, desktop, terminal, and CupidC runtime
smoke.

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

The normal image has 146 checked CupidC C transforms: 145 checked-in sources
and the generated `kernel/cpu/ksyms_data.cc` source. All 146 sources use
`.cc`. The five shared Toolchain roots also belong to the 19-source i386
Linux fixed point. Native GCC and Clang rules select C with `-x c`. ADR 0124
records the first 111-root transfer, ADR 0126 records the complete
fixed-point rename and old-seed proof, and ADR 0129 records the lexer
transfer. Nine strict checked-in roots remain host-owned.

Compiler head now resolves the C11 inline declaration set in unchanged
`kernel/audio/nuked_opl3.c`. The ordinary declaration in its header means
the later inline body provides a global `OPL3_Generate4Ch` definition. Two
kernel-profile compiles produce the same validated 40,424-byte object with
SHA-256
`a3a04ade4029d9333902bb93376fb5eef21f349ee5a1406bd0751cc4cee9f2a1`.
Prior `static` linkage remains internal, and an external-linkage inline
declaration now requires a definition in the same translation unit.
The checked seed predates this rule, so the normal recipe stays host-owned
and the source stays `.c` until seed promotion and the production gates pass.

The strict frontier must compile each of its 145 approved sources twice.
Forced Make runs with the host compiler command poisoned cover every
production wrapper recipe, and each recipe lists its exact recursive header
closure. A valid data-only object can omit `.text` when its other sections
and symbols pass validation. The complete frontier now compiles all 145 roots
twice against a 433-file snapshot. Both object sets are byte-identical and
total 3,545,724 bytes. The combined graph passes the two-link symbol and
memory checks, a clean normal image build, and the strong four-vCPU runtime
gate.

The checked seed compiles three generated installation tables. CupidC also
compiles the three example external ELF programs. All six use `.cc` source
names. The generated tables keep the kernel profile. `hello.cc`, `ls.cc`, and
`cat.cc` use the closed user profile and CupidLD link. Linux runs the checked
seed directly. Windows prepares native hosted CupidC and CupidLD drivers and
runs private PE snapshots without WSL. Both wrappers freeze their source and
control inputs, validate every ELF result, and replace an artifact only after
the operation succeeds. The Windows frontier also requires all six files to
match checked-seed output.

Before compilation, the user ABI operation captures the exact bytes of its
six kernel and public declarations. It compares the reviewed i386 layout and
rechecks every input before reporting success.

The external-program runtime gate boots the validated hello, ls, and cat
executables separately through the ordinary loader. Each QEMU process gets a
private copy of the same staged image. The cat copy also receives a fixed FAT
fixture at `/home/readme.txt`. Serial events carry the running PID. Print
events record a byte count and FNV-1a fingerprint instead of caller text,
which keeps newline and marker-shaped file contents inside one event. The
checks cover hello's numeric writes, ls reading the shell root, cat reading
the fixture, and a PID-matched exit from each program. Kernel and JIT printing
remain on their existing path. ADR 0133 records the ABI and image boundaries.

The refreshed checked seed emits weak symbols and arbitrary compatible named
sections, records `unused` and `used` declarations, preserves typed static
null pointers, treats known-true loops as non-fallthrough, and lowers comma
expressions in source order. It also keeps all target bits through represented
function-pointer casts and supports bounded output-only register and EFLAGS
snapshots. The checked production wrapper now compiles the generated
`kernel/cpu/ksyms_data.cc` root. The generator writes little-endian
`unsigned int` words and records the logical 104,447-byte blob length
separately. It runs private snapshots of the pass-one kernel and CupidDis,
rejects malformed symbol rows, an empty text-symbol set, and live input
drift, then replaces the `.cc` source atomically. The checked compiler
wrapper freezes that source and its header closure before it publishes the
object. The word array ends with one zero pad byte. The final kernel
consumes 4,351 text symbols and shows no address drift from the pass-one
kernel.

Compiler head also emits the exact volatile
`call 1f\n1: popl %0` address capture used by the stack-trace helpers in
`kernel/lang/as.cc` and `kernel/lang/cupidc.cc`. The instruction pair is a
zero-displacement `CALL` followed by `POP r32`, with no relocation. Both
roots compile twice to matching validated i386 ELF32 objects under the
complete kernel profile. The `as.cc` object is 148,056 bytes with SHA-256
`f05ffb741a81403f3bfb86358b3f96011b2ddef65c87e291f582c1d77b0cedfd`.
The `cupidc.cc` object is 288,180 bytes with SHA-256
`4e8501e628a770b346bbe16e23d9549c4320f1f01f0ddcb9309b907a8c898046`.
Their normal recipes now use this checked-seed capability. Other call
templates and general inline-assembly labels remain unsupported.

Compiler head now also accepts the GNU `Nd` port alternative in
`kernel/cpu/pic.cc`. It selects the valid DX branch and emits both unchanged
8-bit PIC templates through Cupid's x86 model. The normal recipe compiles the
root with the checked seed.

Compiler head emits the three machine-state memory outputs in
`kernel/core/panic.cc`. The exact volatile `fnstsw %0` and `fnstcw %0` forms
require a 16-bit `=m` destination, and `stmxcsr %0` requires a 32-bit
destination. Linear IR evaluates each lvalue once, and the i386 emitter writes
through that address with the shared x86 model. The exact call-next support
above also handles the later local-label statement in the unchanged panic
source. Two full kernel-profile compiles produce the same validated
10,212-byte ELF32 object. The normal recipe now uses the checked seed.

CupidDis accepts every one of the 428 active i386 ELF objects, including all
current symbols and relocations. Cupid-built objects, checked tool images, and
user executables have no unsupported instruction fallback. The remaining gap
is concentrated in host-built kernel and Doom objects. The shared catalogue
now covers 16-bit and 32-bit three-operand `IMUL` through both `69 /r` and
`6B /r`. A focused LLVM census found 27 real instances in four host-built
objects, while CupidDis fallback rows in that sample fell from 540 to 495.
Padding NOPs and packed-integer SSE2 remain the largest measured decoder gaps.

The four-vCPU GUI runtime starts every discovered CPU, reaches e1000 traffic,
passes all 62 crypto checks, opens the desktop and terminal, and completes
embedded CupidC execution at `0x01100000`. The established e1000 and RTL8139
gates continue to cover audio, input reattachment, and six EHCI storage
lifetimes. A private-image smoke loads the same external ELF program twice at
`0x00F00000`; cleanup releases the first arena lease before the second load.
The gate rejects SMP, storage, crypto, exception, panic, corruption, and
illegal-instruction failure markers. The X.509 checks exercise parser,
hostname, chain state, and embedded-root lookup paths. They are not a full
trust-validation claim.

Across the root and supplemental builds, the current audit assigns 152
transforms to CupidC, 145 to the host C compiler, 165 to host Python, and five
to Make.
CupidC's total is the 146 normal transforms plus three generated installation
tables and the `hello.cc`, `ls.cc`, and `cat.cc` programs. The host compiler
still produces 93 root objects and still builds the native user drivers.
The fifth Make transform prepares those drivers.
The checked audit uses the canonical Windows Make branch and C locale on
every host. Direct Linux builds test the separate Linux execution branch.
