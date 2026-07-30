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

The full command reads all 40 current source inputs once: 19 C sources,
startup, 19 project headers, and `link.ld`. It copies those exact bytes into a
private compiler root. Checked CupidC compiles stage two below that root,
checked CupidASM assembles its startup, and checked CupidLD links all five
tools. The stage-two producer trio repeats the build for stage three below the
same root.

The gate compares all 19 C objects, both startup objects, and all five linked
images. It also runs five help checks, ten successful operations, and six
failure cases across compilation, assembly, disassembly, symbol inspection,
linking, wrapping, and flattening. The harness rehashes both the private
closure and the live closure before stage two, after each stage, and after the
behavior suite. A live edit that is made and restored during a compile cannot
change the captured compiler input.

Before execution, the harness reads the manifest and each seed binary once. It
verifies those captured bytes, keeps the manifest hash, and runs private copies
of the five binaries. A later replacement of a checked-in file cannot change
that run.

The default output is `build/bootstrap/checked-seed/`. It must be absent or
empty at the start. The harness keeps both stages, behavior fixtures, and the
report private until every check succeeds, then publishes them as one complete
directory. A nonempty output is rejected without modification. The report
keeps the historical seed source revision separate from the captured source
snapshot. ADR 0142 records this source and publication boundary.

Linux runs private copies of the static tools directly. Windows stages each
copy in a mode-0700 WSL directory created by `mktemp`. Native Windows seed
executables are not available yet.

This seed makes the hosted static toolchain reproducible from a clean checkout.
It does not complete the normal OS build migration. Native contract runners,
hosted development commands, and 83 normal C root objects still use a host C
compiler.

The production boot source assembles to an exact 2,560-byte image with SHA-256
`9545d6a2f44404af85bb3fd568f1b2d7215b7cd1af2933f7ae5a877353dc95fc`.
CupidASM and the optional NASM oracle produce the same bytes for the current
`0x00F00000` boot-stack layout.

The checked seed includes the complete 83-root Doom compiler frontier,
current GNU entity metadata, the active x87 and SSE memory forms, descriptor
and segment assembly, every unchanged assembly effect in `libm.cc`, the exact
dglibc jump block, pointer-preserving static address casts, explicit `double`
to `unsigned long long` conversion, and exact naked IPI entries. Its
stage-three CupidC image is 2,528,332 bytes with SHA-256
`f53989572cd1564a8bf91059552868ee43a1d80905986b58cd97d44949aab3a1`.
It came from stage three of the checked bootstrap at revision
`af4644177c033eebda164d7893074315439df119`. It also carries the kernel-entry
BSS clear with a nonzero page-aligned stack top and all packed SSE2 statements
in the active SIMD source. CupidASM
and CupidDis carry the 587-row shared x86 catalogue. With host code-generator commands
poisoned, all five seed images match stage two. All 19 stage-two C objects,
startup, and five images then match stage three, and both stages pass all 21
tool behavior cases. ADR 0186 records this promotion and its post-promotion
reproof.

The refreshed seed represents operand-free GNU assembly statements inside
functions and emits exact PAUSE, NOP, STI, HLT, CLI, CLD, SFENCE, and FNINIT
sequences. The normal build uses that path for e1000, the desktop shell,
the socket layer, and TCP. The earlier detached hybrid linked the same four
objects through both CupidLD passes and booted them under QEMU before the
ownership hand-off.

The checked seed handles the exact per-CPU pointer output
`mov %%gs:0, %0` with one four-byte `=r` object or `void` pointer. The frontend
and IR preserve its pointer type and evaluate the destination once. The x86
model emits `65 A1 00 00 00 00`.

The checked seed handles the independent `r` and `c` inputs used by
`kernel/cpu/idt.cc`, `kernel/mm/paging.cc`, and `kernel/smp/lapic.cc`.
Exact CR0, CR2, CR3, and CR4 moves and RDMSR emit directly into deterministic
i386 ELF32 objects. The three double-compiled objects are 8,756, 2,336, and
4,184 bytes and pass the shared validator. Focused frontend, Linear IR,
object, and decoder contracts cover the supported forms and their failures
without executing privileged instructions. The normal recipes now compile
all three roots with the checked seed.

The checked seed handles the exact volatile `fxsave (%0)` form used twice in
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

The checked seed carries `__atomic_fetch_or` at the same one-, two-, and four-byte
integer widths. It emits a `LOCK CMPXCHG` retry loop because `LOCK OR` cannot
return the old value. Exact byte and execution checks cover a competing
update, signed narrow results, guard bytes, one-time operand evaluation, and
callee-saved EBX. The checked stage-three seed carries this operation and
compiles the active EHCI path.

The checked seed parses all eight helpers in unchanged
`kernel/core/ports.h`. It retains the 8-, 16-, and 32-bit accumulator lanes,
the 16-bit DX port, the read/write ESI or EDI buffer, the read/write ECX
count, and the INSW memory clobber. Scalar port I/O and the CLD plus REP word
forms emit through the shared x86 model. This brings the active non-Doom
header gate to 155/155 in the checked seed.

The checked seed retains GNU `noinline` and the exact
`target("general-regs-only")` option on compatible file-scope function
declarations. Each IR function carries the canonical code generation mask.
Linear IR rejects compiler-generated floating work in a
general-register-only function, and the emitter repeats the mask and
frozen-metadata checks. Explicit source assembly remains under its own
contract. It also keeps the exact volatile `ldmxcsr %0` memory
input as one address-valued 32-bit integer lvalue and emits `0F AE 10`
through the shared x86 model. It also keeps the exact MOVSS float-memory
round trip in `fpu_boot_smoke()` and the matching one-way load and store.
Each form requires the `xmm0` clobber, evaluates each object address once,
and emits `F3 0F 10 00` or `F3 0F 11 00` through EAX. The unchanged
`stress_sin()` x87 statement is also represented. It evaluates one `double`
output address before one `double` input address, permits no clobbers, and
emits balanced `FLD`, `FSIN`, and `FSTP` instructions with no frame
temporary. Two complete builds of `kernel/cpu/fpu.cc` produce the same
validated 6,620-byte object with SHA-256
`14c3ea232b7d4455ceabd561c69293cc5849abae24d9f210aa69d64ed8c8a5cb`.
The normal build owns the root through the checked wrapper. A typed policy
decodes `fpu_init_cpu()`, rejects helper calls and floating work before the
CR4 write, and requires `FNINIT` before one 32-bit memory `LDMXCSR`.

The checked seed represents the exact volatile EFLAGS restore in
`simd_cpu_has_cpuid()`. One 32-bit `r` input and one `cc` clobber reach
Linear IR as checked public metadata. The emitter produces `POP EAX`,
`PUSH EAX`, and `POPF` through the shared x86 model, leaving ESP balanced.
Both unchanged restore statements pass.

It accepts the CPUID statement's fixed EAX input/output overlap.
The `a` input keeps its original spelling and points to the compatible
write-only `=a` output in the public operand record. Linear IR checks that
tie, including represented integer types and equal widths. Emission repeats
the check and loads EAX immediately before CPUID. A frozen same-width
non-integer substitute fails transactionally. The checked seed emits the six
packed SSE2 statements in `kernel/cpu/simd.cc`. It checks their ordered pointer
and 32-bit integer inputs, exact memory and XMM0 through XMM7 clobbers, and
uses Cupid's shared x86 model for every packed instruction. The production
wrapper freezes the source and its seven-header closure. Two checked-seed
builds produce the same validated 8,768-byte object with SHA-256
`fd280c321b8eb38a90d4f0982d70b8df0364585e3da322eb2c9de722e071f8d4`.
The normal SIMD recipe now uses this checked object.

The checked seed represents the complete x87 round-down statement
in unchanged `str_floor()`. It requires one modifiable `double` output, one
addressable `double` input, and the exact `ax` plus `memory` clobber set.
After loading the input, emission reuses its consumed address slot below ESP
for the saved and temporary control words. The pending output address stays
intact. The sequence selects round toward negative infinity for `FRNDINT`,
then restores the incoming x87 control word before storing the result.

Two exact compiles of the extracted active helper produce the same 420-byte
ELF32 object with SHA-256
`448012fe57ec625c6075e97cf91163b994a0443238c5d6bdf25e4b839763f14e`.
It also emits the later explicit double-to-`uint64_t` casts. It
splits the result around 2^32 and uses a 2^31-safe truncation for each word.
The decoder-driven oracle covers positive and negative fractions and the
active range through the largest binary64 value below 2^64. Full unchanged
`kernel/core/string.cc` compiles
twice to the same 14,460-byte object with SHA-256
`d48bb6ea18b7124fbefeaca0d5d5ee8a517db950f21ea88e30ededd6c5c2a577`.
The production wrapper freezes the source and its two headers, validates the
ELF32 object, and publishes it without a host compiler.

Compiler head now represents the exact operand-free BSS-clear statement only
at the direct start of the external `.text.start` `_start()` definition. It
keeps the EAX, ECX, EDI, and memory clobbers, requires visible object
declarations for `_bss_start` and `_kernel_end`, and rejects a
compiler-managed frame. Frontend depth rejects leading, label-wrapped, and
otherwise nested copies. The emitter installs the fixed stack, writes two
`R_386_32` symbol relocations, derives the doubleword count, and clears the
linked range with CLD and REP STOSD.

The following `kmain()` call uses the reset stack residue and adds no stale
padding. If it returns, the entry disables interrupts and remains in a halt
loop. The 42-byte fixture has three relocations and a 27-byte assembly body.

Two Cupid-built compiler runs emit `kernel/core/kernel.cc` as the
same 25,920-byte object with SHA-256
`d44d06949d48ead865d0d8c1bdd3b76a67b429e0b7a369318ec4fbe8d9f44ed7`.
A private hybrid image with that object passes the GUI terminal smoke. The
production wrapper freezes the source and its 63-header closure, and the
normal recipe now uses the checked object.

The checked seed retains GNU `naked` and `__naked__` for the exact IPI
entries. A naked definition must be
`void (void)` and contain one complete wrapper or panic-loop assembly
statement. The emitter adds no C frame or return instruction. Its direct call
uses one typed `R_386_PC32` relocation, and the panic loop uses a local
relative jump. Two exact-profile compiles reproduce an 8,444-byte object with
SHA-256
`806509a6dd1ac7eb34b7ffcb67a1f8852950663a274145584d0260da76dcba54`.
The earlier `smp.c` proof produced an 8,444-byte object with SHA-256
`806509a6dd1ac7eb34b7ffcb67a1f8852950663a274145584d0260da76dcba54`.
The production `kernel/smp/smp.cc` object remains 8,444 bytes and has SHA-256
`bd3189b2a1a6d15728c559172f5d6acca0889103428085cec8cc1024742a22d1`.
The existing `__FILE__` diagnostic accounts for the difference.

The normal image has 156 checked CupidC C transforms: 155 checked-in sources
and the generated `kernel/cpu/ksyms_data.cc` source. All 156 sources use
`.cc`. The five shared Toolchain roots also belong to the 19-source i386
Linux fixed point. Native GCC and Clang rules select C with `-x c`. ADR 0124
records the first 111-root transfer, ADR 0126 records the complete
fixed-point rename and old-seed proof, ADR 0129 records the lexer transfer,
ADR 0135 records the Nuked OPL3 transfer, ADR 0139 records the JPEG and
glyph-raster transfer, ADR 0167 records the FPU and SMP transfer, ADR 0176
records the libm transfer, ADR 0180 records the kernel entry and SIMD
transfer, and ADR 0181 records the string transfer. No strict checked-in
kernel or driver root remains host-owned.

The checked seed accepts ordered `-include` inputs through both the native
and Cupid-built driver. That command reproduces both complete audited Doom
preprocessing profiles without editing vendored source. It also
retains the sound driver's empty volatile memory barrier without emitting an
instruction. An integer-only IEEE evaluator folds the unchanged static
fixed-point table in `kernel/doom/src/am_map.c` without a host floating
operation. A one-active-member union initializer also emits unchanged
`kernel/doom/src/info.c`. An explicit `--doom-compat` switch represents the
five calls in `i_system.c` that precede a declaration and permits the eleven
audited, bit-preserving conversions between unqualified function pointers and
unqualified four-byte data or `void` pointers. Strict C and plain GNU mode
still reject those implicit conversions, and explicit function/data casts
remain outside Linear IR. The checked seed retains member provenance while
narrow `unsigned int` color fields promote to signed `int` in unchanged
`kernel/doom/src/i_video.c`. It now emits all 80 Doom-tree objects. The
checked seed carries this complete tree frontier.

The seed also emits the three separate compatibility roots. It keeps the
explicit static string cast in `doom_libc_stubs.c` and emits the exact
`dg_setjmp` and `dg_longjmp` file-scope block through Cupid's x86 model. A
second checked-seed compile matches the first for all three objects. The
checked compiler frontier is 83/83, but no Doom recipe moves until the host
and Cupid link boundaries compare, the owned roots move to `.cc`, and runtime
proof passes.

The checked seed resolves the C11 inline declaration set in
`kernel/audio/nuked_opl3.cc`. The ordinary declaration in its header means
the later inline body provides a global `OPL3_Generate4Ch` definition. Two
kernel-profile compiles produce the same validated 40,424-byte object with
SHA-256
`a3a04ade4029d9333902bb93376fb5eef21f349ee5a1406bd0751cc4cee9f2a1`.
The object imports only `memset`. Prior `static` linkage remains internal, and
an external-linkage inline declaration requires a definition in the same
translation unit. The closed production recipe, frontier, image builds, and
dual-NIC runtime gates pass. The wrapper compiles from a private copy of the
source and its three headers, then rejects live input drift before it replaces
the object.

The strict frontier must compile each of its 155 approved sources twice.
Forced Make runs with the host compiler command poisoned cover every
production wrapper recipe, and each recipe lists its exact recursive header
closure. A valid data-only object can omit `.text` when its other sections
and symbols pass validation. Final frontier publication retries only short
permission-style directory locks with five bounded delays. A persistent lock
or any other filesystem error publishes nothing. Input discovery skips hidden
paths under active include roots, so private compiler staging headers from a
concurrent build do not enter the repository snapshot. The complete frontier
compiles all 155 roots twice against a 444-file snapshot with SHA-256
`bfa1e7210193b95df3c357a6c893078c86a74afa33e1cb2baa1cafc0173efab6`.
Both object sets are byte-identical; each totals 3,708,988 bytes. The combined graph passes the
two-link symbol and memory checks, clean normal and partitioned image builds,
and strong four-vCPU runtime gates with both NICs.

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
`unsigned int` words and records the logical 106,259-byte blob length
separately. It runs private snapshots of the pass-one kernel and CupidDis,
rejects malformed symbol rows, an empty text-symbol set, and live input
drift, then replaces the `.cc` source atomically. The checked compiler
wrapper freezes that source and its header closure before it publishes the
object. The word array ends with one zero pad byte. The final kernel
consumes 4,420 text symbols and shows no address drift from the pass-one
kernel.

The checked seed emits the exact volatile
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

The checked seed also accepts the GNU `Nd` port alternative in
`kernel/cpu/pic.cc`. It selects the valid DX branch and emits both unchanged
8-bit PIC templates through Cupid's x86 model. The normal recipe compiles the
root with the checked seed.

The checked seed emits the three machine-state memory outputs in
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
`6B /r`. It also covers ordinary compiler padding from `66 90` through the
ten-byte `66 2E 0F 1F 84 00 00 00 00 00` form. An independent census found
1,100 such multibyte NOPs and 6,610 padding bytes in 74 host-built objects.
Across the 228 i386 kernel objects available to the current audit, CupidDis
fallback rows first fall from 6,952 in 77 objects to 3,597 in 68 objects.
A private decoder exception then recognizes 568 exact Clang forms with two
through six leading `66` bytes and the fixed
`2E 0F 1F 84 00 00 00 00 00` tail. The final scan has 1,901 fallback rows
in 36 objects and renders 1,781 NOP rows. Other repeated prefixes remain
invalid, and CupidASM cannot emit the redundant forms. Packed-integer SSE2
is the next largest measured decoder gap. Source head has 587 catalogue rows
and fingerprint `68E281CB`; the private exception does not change either
value. The checked seed carries the same 587-row catalogue.

The four-vCPU GUI runtime starts every discovered CPU, reaches e1000 or
RTL8139 traffic,
passes all 62 crypto checks, opens the desktop and terminal, and completes
embedded CupidC execution at `0x01100000`. The established e1000 and RTL8139
gates continue to cover audio, input reattachment, and six EHCI storage
lifetimes. Both NIC runs print `[fpu] SSE2 enabled`,
`[fpu] boot smoke ok`, and `FPU boot smoke passed`, then finish
`feature16_asm_fpu.cc`. A private-image smoke loads the same external ELF
program twice at `0x00F00000`; cleanup releases the first arena lease before
the second load.
The gate rejects SMP, storage, crypto, exception, panic, corruption, and
illegal-instruction failure markers. The X.509 checks exercise parser,
hostname, chain state, and embedded-root lookup paths. They are not a full
trust-validation claim.

Across the root and supplemental builds, the current audit assigns 162
transforms to CupidC, 135 to the host C compiler, 177 to host Python, and five
to Make.
CupidC's total is the 156 normal transforms plus three generated installation
tables and the `hello.cc`, `ls.cc`, and `cat.cc` programs. The host compiler
still produces 83 root objects and still builds the native user drivers.
The fifth Make transform prepares those drivers. Two Python transforms keep
the ISO runtime fixture in the normal image dependency graph.
The checked audit uses the canonical Windows Make branch and C locale on
every host. Direct Linux builds test the separate Linux execution branch.

### GNU named assembly operands

Checked-seed CupidC accepts optional `[identifier]` labels on GNU extended
assembly outputs and inputs. It collects the complete operand namespace and
normalizes each unescaped `%[identifier]` reference to the existing numeric
operand index before public frontend metadata freezes. Escaped `%%` pairs
remain literal. Linear IR and the i386 emitter therefore keep their numeric
contracts and apply the same validation to named and numeric source.

### x87 power statements

The checked seed accepts the complete volatile assembly statements in
`libm_pow_impl()` and `libm_powf_impl()`. The double form requires one
modifiable `double` output and four addressable `double` inputs. The mixed
form requires one modifiable `float` output, two addressable `float` inputs,
and two addressable `double` inputs. Both require one memory clobber and no
other clobber. Linear IR evaluates each statement's five addresses once in
source order.

Each focused function has 116 exact text bytes and no relocations. Shared
decoding checks all seventeen x87 instructions, the canonical `DC E1`
reverse-subtract bytes, maximum stack depth three, balanced depth on return,
deterministic output, rollback, and same-job recovery.

### SSE2 square-root statement

The checked seed accepts the exact volatile `sqrtsd %1, %0` statement in
`libm_sqrt_impl()`. It requires one modifiable, non-atomic `double` `=x`
output, one non-atomic `double` `x` input, and no clobbers. Linear IR
evaluates the output address before the input value.

The emitter loads the input into XMM0 with `MOVSD`, applies
`SQRTSD XMM0, XMM0`, and stores the result through the saved output address.
The focused function has 65 text bytes and no relocations. Contracts cover
the exact bytes, forged metadata, useful operand diagnostics, deterministic
output, unreachable validation, rollback, and same-job recovery.

### x87 atan2 memory statement

The checked seed accepts the exact volatile statement in `libm_atan2_impl()`.
It requires one modifiable, non-atomic `double` `=m` output, two addressable,
non-atomic `double` `m` inputs in `y`, `x` order, and one `memory` clobber.
The named spelling normalizes to the same frozen metadata as the numeric
form. Linear IR evaluates all three addresses once in source order.

The 53-byte focused function has no relocations. The direct 15-byte sequence
loads `y`, loads `x`, applies `FPATAN`, and stores through the saved output
address with balanced x87 depth. Contracts cover shared decoding, forged
metadata, operand diagnostics, deterministic output, unreachable validation,
rollback, and same-job recovery.

### x87 exponent memory statement

The checked seed accepts the exact volatile statement in `libm_exp_impl()`. It
requires one modifiable, non-atomic `double` `=m` output, two addressable,
non-atomic `double` `m` inputs in `x`, `log2e` order, and one `memory`
clobber. The named spelling and normalized numeric form use the same
validation. Linear IR evaluates all three addresses once in source order.

The 71-byte focused function has no relocations. Its direct 33-byte sequence
computes `exp2(x * log2(e))`, reaches x87 depth three, and returns to the
incoming depth before storing through the saved output address. Contracts
cover shared decoding, forged metadata, operand diagnostics, deterministic
output, unreachable validation, rollback, and same-job recovery. The
unchanged source then reaches the aligned file-scope `fabs` mask block.

### fabs file-scope masks and wrappers

The checked seed accepts the exact mask block followed by `fabs` and `fabsf`.
The mask effect reserves the first 32 bytes of `.rodata` at alignment 16 and
defines local `STT_NOTYPE` symbols at offsets 0 and 16. It is placed before
ordinary and block-static objects, so later read-only C data starts at offset
32 or later.

The wrappers retain their source prototypes and global function symbols.
`fabs` contains 15 text bytes and an `R_386_32` relocation at function offset
10 to `fabs_mask_d`. `fabsf` contains 14 bytes and the same relocation type at
function offset 9 to `fabs_mask_s`. Both use Cupid's shared x86 model.
Contracts cover exact bytes, symbols, relocations, mixed read-only data,
source ordering, forged metadata, deterministic output, rollback, and
same-job recovery.

### libm file-scope rounding wrappers

The checked seed accepts the exact `floor`, `floorf`, `ceil`, `ceilf`, `round`,
`roundf`, `trunc`, and `truncf` definitions. Each wrapper loads its scalar
argument, saves the x87 control word, clears the RC field with `0xf3ff`, and
installs the source mode before `FRNDINT`. It restores the original control
word before returning the result through XMM0. The nearest-even pair uses
`RC=00` and omits the OR instruction.

The eight functions add 384 text bytes and no relocations. Exact symbol
offsets and sizes are checked, as are every decoded instruction and operand,
the float and double widths, the four control modes, balanced ESP, and
balanced x87 depth. Negative cases alter the control mask and the `floor`
prototype, then check output rollback and same-job recovery.

At this boundary the unchanged source reaches `fmod` at line 465. Named
matching constraints, operand modifiers, and general XMM or x87 constraints
remain separate work. The then-host-owned `libm.c` recipe remained unchanged
at that compiler boundary.

### libm file-scope remainder wrappers

The checked seed accepts the exact `fmod` and `fmodf` definitions. Each wrapper
loads `y` and then `x`, leaving the dividend in ST(0) and divisor in ST(1).
It repeats `FPREM` while C2 in the x87 status word remains set. `FNSTSW AX`
and `TEST AX, 0x0400` feed a rel8 `JNE` with displacement `-10` back to the
reduction instruction.

After convergence, `FSTP ST(1)` removes the divisor without losing the
remainder. The wrapper stores the result at the source width, moves it to
XMM0, restores ESP, and returns. Both functions contain 35 text bytes. The
pair adds 70 bytes to the fixture, for a total of 702, and has no
relocations. Each reaches x87 depth two and returns to its incoming depth.

The decoder checks every instruction and operand, including the C2 mask and
short branch target. Negative cases alter the mask and give `fmod` the float
prototype, then check output rollback and same-job recovery.

That remainder slice ended at the aligned `libm_log2e_const` and
`libm_ln2_const` block on line 544. The next section records the later
checked-seed ownership of that exact data and its wrappers.

### libm file-scope exponent and logarithm wrappers

The checked seed accepts that exact constant block and the following `exp2`,
`exp2f`, `exp`, `expf`, `log2`, `log2f`, `log`, and `logf` definitions. The
two local `STT_NOTYPE` symbols occupy offsets 0 and 8 in a 16-byte `.rodata`
section with alignment eight.

The exponent wrappers share the source `FRNDINT`, `F2XM1`, and `FSCALE`
sequence. The natural pair loads `libm_log2e_const`, multiplies, and then
uses that sequence. The logarithm wrappers use `FYL2X`; the base-two pair
loads one, and the natural pair loads `libm_ln2_const`.

The eight functions occupy 264 text bytes. `exp2` and `exp2f` are 37 bytes
each, `exp` and `expf` are 45 each, `log2` and `log2f` are 23 each, and
`log` and `logf` are 27 each. The natural forms contribute four
`R_386_32` relocations. Decoder checks cover every instruction and operand,
x87 depth up to three, and balanced ESP and x87 state.

Negative contracts change one constant bit, remove or move the data block,
duplicate it, collide a label with a C declaration, forge metadata, and give
`exp` the float prototype. Each failure rolls back cleanly, and the same job
can emit the valid object afterward.

The unchanged source then reaches `pow` at line 846.

### libm cdecl bridge wrappers

The checked seed accepts `pow`, `powf`, `asin`, `asinf`, `acos`, `acosf`,
`sinh`, `sinhf`, `cosh`, `coshf`, `tanh`, `tanhf`, `cbrt`, `cbrtf`,
`hypot`, `hypotf`, `nextafter`, and `nextafterf`. Each exact file-scope
template copies the original cdecl words, calls a matching external
`libm_*_impl` function, reclaims the copied words, stores the ST(0) result,
and moves it into XMM0.

Four shared stack shapes cover unary and binary float and double functions.
The family occupies 558 text bytes. Its 18 direct calls each use one
`R_386_PC32` relocation with known addend `-4`. Decoder contracts check all
push displacements and widths, the call fields, cleanup, result bridge,
return, ESP balance, and x87 balance.

Negative contracts change a template, remove a callee, change a wrapper or
callee prototype, forge metadata, and exhaust the output limit. Each failure
rolls back, and the same job can emit the valid object afterward.

Two exact kernel-profile compiles of byte-unchanged `kernel/cpu/libm.cc` now
produce the same valid 16,164-byte ELF32 relocatable object with SHA-256
`ccfb59839b058020a3cdc30c8e6db7ebac8845215a38ff974b3cbca876574eac`.
General GAS remains unsupported.

The checked seed carries this whole boundary. The normal `libm.cc` recipe now
uses the checked production wrapper with `kernel/core/types.h` and
`kernel/cpu/libm.h` frozen beside the source. The guest gate runs
`/bin/feature15_libm.cc` and requires all 22 checks plus
`PASS feature15_libm`. ADR 0176 records production ownership.
