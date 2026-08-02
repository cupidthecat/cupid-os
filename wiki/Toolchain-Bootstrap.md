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

The separate i386 runtime contract is not part of those 19 tool inputs. It
uses `.cc` because CupidC compiles it, CupidLD links it with CupidASM startup
and the repository runtime, and Linux or WSL runs the result. The normal
Toolchain target also owns fourteen `.cc` contract programs. Stage-two and
stage-three CupidC compile them at the checked i386 ABI, CupidLD links each
one against matching stage objects, and the harness requires all sixteen new
objects and fifteen executables to match across stages. It freezes 45
contract inputs and reconstructs that exact inventory under a private source
root. That inventory includes the Toolchain Makefile and both Python modules
that construct or verify the cohort. Newly discovered contract inventories catch additions, removals, and a
transient edit copied before the live file is restored. The public manifest
also binds the checked seed, build plan, and 41-file fixed-point source
inventory. Verify and run reconstruct both inventories before execution.
Hashing, JSON decoding, schema checks, and build-plan use share one captured
seed-manifest byte sequence. Replacing the file during validation cannot mix
facts from separate reads. The
publisher accepts only a dedicated `cupidc-contracts` directory inside the
source tree. It validates that target before work and again before promotion,
and an existing destination must already verify as a complete cohort.
Arbitrary directories, source trees, files, and symbolic links remain
untouched. It publishes all fifteen contracts, five refreshed tools, and a
manifest together. ADR 0195 records the runtime probe rename, and ADR 0196
records the complete transfer.
Every normal Toolchain run derives the cohort from its requested executable,
requires a named manifest artifact, and verifies the target, fixed-point
record, exact filenames, sizes, hashes, and current live input hashes before
execution.
The runtime probe also exercises signed and unsigned `long long` formatting,
sixteen-digit zero-padded hexadecimal output, and precision-bounded strings.
Those forms come from the unchanged Toolchain contract diagnostics.

```sh
make verify-bootstrap-seed
```

This command validates the seed without executing it.

```sh
make bootstrap-from-seed
```

The full command reads all 41 current source inputs once: 19 C sources,
startup, 20 project headers, and `link.ld`. It copies those exact bytes into a
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

This seed makes the hosted static toolchain reproducible from a clean
checkout. `make -C toolchain all` uses it for the normal contract cohort and
does not invoke a host C compiler or native linker. Native contracts remain
available under `native-oracles`, and hosted development commands may still
use a host compiler. Normal OS objects do not.

Host Python still coordinates the fixed point, and Windows still runs the
static i386 Linux tools through WSL. Native Windows and Python-free fixed
points remain open. No output-quality gate is defined. Older Windows and
Linux host `.text` measurements differ by 22.73 percent for the same revision,
so neither measurement can define that gate. Linker capacity checks remain
separate.

The production boot source assembles to an exact 2,560-byte image with SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`.
CupidASM and the optional NASM oracle produce the same bytes for the current
`0x01100000` stack top and LBA 20480 FAT boundary.

Source-head CupidASM also accepts `align POWER_OF_TWO[, FILL_BYTE]`. Raw
output aligns the absolute `ORG` address, ELF32 output carries the required
section alignment, and fixed images include the absolute region base in the
calculation. NOBITS padding consumes memory but no file bytes. The FPU demo
now declares its 16-byte FXSAVE requirement directly. This language change
does not move a build owner or add a host dependency. ADR 0197 records it.

The checked seed includes the complete 83-root Doom compiler frontier,
current GNU entity metadata, the active x87 and SSE memory forms, descriptor
and segment assembly, every represented assembly effect in `libm.cc`, the exact
dglibc jump block, pointer-preserving static address casts, explicit `double`
to `unsigned long long` conversion, exact naked IPI entries, and runtime
floating truth. Its stage-three CupidC image is 2,561,644 bytes with SHA-256
`a4dff3c1c8ae975e9b8278920d36aefe6ad9b28a52503a6d5d4253e04e4a21af`.
It came from revision `efec9c5f89358999a067a4a7c923d06d814d1639`. It also
carries the kernel-entry BSS clear with a nonzero page-aligned stack top and
all packed SSE2 statements in the active SIMD source. CupidASM and CupidDis
carry the 592-row shared x86 catalogue with forward stack subtraction.
CupidDis carries typed raw code and
data ranges, and CupidObj carries installation-source generation. Its
253,724-byte CupidObj image has SHA-256
`f78752dc01daf3d2a9dc9265425f9c60639f438d5dcb91a001cf40d7d241ded5`.
In the latest transition, all 19 C objects, startup, and five tool images
matched between stage two and stage three. Both stages passed five help cases,
ten successful operations, and six useful failures. CupidASM, CupidC, and
CupidDis changed from the preceding seed; CupidLD and CupidObj stayed
byte-identical. A post-promotion rebuild then reproduced all five checked seed
images at stage two and repeated the complete fixed point. ADR 0208 records
the promotion and both proofs.

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
`ed42676ad0d7f16b1fb83442ead1b0082781324dca719104922099cee34b5ab0`.
The normal image built with that object passes the four-CPU frontier gate on
both supported NICs. The production wrapper freezes the source and its
63-header closure, and the normal recipe uses the checked object. ADR 0187
records the coordinated memory-map move.

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

The strict non-Doom portion of the normal image has 156 checked CupidC
transforms: 155 checked-in sources and the generated
`kernel/cpu/ksyms_data.cc` source. All 156 sources use `.cc`. The 83 Doom
roots bring the normal checked-in total to 238. The five shared Toolchain
roots also belong to the 19-source i386 Linux fixed point. Native GCC and
Clang rules select C with `-x c` only for optional oracles. ADR 0124
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
fixed-point table in `kernel/doom/src/am_map.cc` without a host floating
operation. A one-active-member union initializer also emits unchanged
`kernel/doom/src/info.cc`. An explicit `--doom-compat` switch represents the
five calls in `i_system.cc` that precede a declaration and permits the eleven
audited, bit-preserving conversions between unqualified function pointers and
unqualified four-byte data or `void` pointers. Strict C and plain GNU mode
still reject those implicit conversions, and explicit function/data casts
remain outside Linear IR. The checked seed retains member provenance while
narrow `unsigned int` color fields promote to signed `int` in unchanged
`kernel/doom/src/i_video.cc`. It emits all 80 Doom-tree objects.

The production wrapper also emits the three compatibility roots. It keeps the
explicit static string cast in `doom_libc_stubs.cc` and emits the exact
`dg_setjmp` and `dg_longjmp` file-scope block through Cupid's x86 model. A
second checked-seed compile matches the first for all three objects. The
normal graph owns all 83 roots through CupidC, and every source uses `.cc`.
The wrapper fixes exact three-source and 80-source allowlists and freezes the
complete 289-file header space. Its input manifest detects source removal,
while the wrapper recursively checks visible `.c` and `.cc` files and live
bytes before publication. A legacy `.c` file, an unlisted `.cc` file, header
drift, a symbolic link, or an NTFS junction fails closed. The 51,492-byte
`g_game.cc` object keeps two `R_386_32` relocations
with addend 4. Full IWAD gameplay remains a separate runtime gate.

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
compiles all 155 roots twice against a 445-file snapshot with SHA-256
`99d03de14f544f6a76d21ed147e62018873f1e2e8dfa2f4459830b69314432c2`.
Both object sets are byte-identical; each totals 3,749,796 bytes. The combined graph passes the
two-link symbol and memory checks, clean normal and partitioned image builds,
and strong four-vCPU runtime gates with both NICs.

Checked-seed CupidObj generates three installation tables, and checked-seed
CupidC compiles them. CupidC also compiles the three example external ELF
programs. All six use `.cc` source
names. The generated tables keep the kernel profile. `hello.cc`, `ls.cc`, and
`cat.cc` use the closed user profile and CupidLD link. Linux runs the checked
seed directly, while Windows runs it through WSL. Both wrappers freeze their
source and control inputs, validate every ELF result, and replace an artifact
only after the operation succeeds. The default frontier tracks 23 checked-seed
inputs. An explicit 46-input Windows frontier runs private native hosted
CupidC and CupidLD snapshots and requires all six files to match checked-seed
output.

CupidObj now implements the bin, docs, and demos table formats through its
public `install-source` command. It keeps caller order, validates the path
category and extension, rejects duplicates and mixed lists, applies one
overflow-safe 512-path limit across the complete request, and rolls back a
partial result on failure. Mixed home-asset extensions retain their supplied
order in the checked seed, source head, and Python oracle. The active
inventories already satisfy both rules. A Cupid-built
command reproduced all three live tables twice with exact Python-oracle
parity. The normal Make recipes now run that checked command for all three
outputs. `tools/hostbuild.py` is no longer a prerequisite or recipe owner for
them, but it remains the parity oracle. ADR 0204 records the transfer, ADR
0206 records the linked-symbol contract, and ADR 0208 records the current
seed.
The checked seed, source head, and Python oracle also compare the full wrapped
symbol name for every typed entry. Distinct paths that collapse to one symbol
fail before publication. The exact same BMP may remain in both the docs and
home lists, where both entries use the same object. Every normal
installation-table recipe now enforces this guard.

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
`unsigned int` words and records the logical 109,889-byte blob length
separately. It runs private snapshots of the pass-one kernel and CupidDis,
rejects malformed symbol rows, an empty text-symbol set, and live input
drift, then replaces the `.cc` source atomically. The checked compiler
wrapper freezes that source and its header closure before it publishes the
object. The word array ends with three zero pad bytes. The final kernel
consumes 4,561 text symbols and shows no address drift from the pass-one
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
user executables have no unsupported instruction fallback. The remaining
measured gap comes from a legacy native-oracle kernel and Doom corpus, not an
active host-owned build path.

Raw inspection uses an ordered range map with 16-bit code, 32-bit code, and
data kinds. Code ranges enter the shared decoder, while data ranges print as
literal `db` rows. The active SMP trampoline test assembles the unchanged
4,096-byte source and marks `[0x000, 0x01f)` as code16,
`[0x01f, 0x210)` as data, `[0x210, 0x254)` as code32, and
`[0x254, 0x1000)` as data. Two CLI renders
match, and neither data interval produces invented instructions. This changes
no production owner because the normal build uses CupidDis's ELF symbol view.

The shared catalogue
now covers 16-bit and 32-bit three-operand `IMUL` through both `69 /r` and
`6B /r`. It also covers ordinary compiler padding from `66 90` through the
ten-byte `66 2E 0F 1F 84 00 00 00 00 00` form. An independent census found
1,100 such multibyte NOPs and 6,610 padding bytes in 74 native-oracle objects.
Across the 228 i386 kernel objects in that corpus, CupidDis
fallback rows first fall from 6,952 in 77 objects to 3,597 in 68 objects.
A private decoder exception then recognizes 568 exact Clang forms with two
through six leading `66` bytes and the fixed
`2E 0F 1F 84 00 00 00 00 00` tail. The final scan has 1,901 fallback rows
in 36 objects and renders 1,781 NOP rows. Other repeated prefixes remain
invalid, and CupidASM cannot emit the redundant forms. Packed-integer SSE2
is the next largest measured decoder gap. Source head has 592 catalogue rows,
244 canonical mnemonics, and fingerprint `F4420CB4`. The checked seed carries
the same model. Its newest form encodes
canonical `FSUB ST(1), ST(0)` as `DC E9` for corrected exponent range
reduction. The four preceding x87 forms are 80-bit `FLD` and `FSTP`
memory forms, i686
`FUCOMIP ST0, ST(i)`, and operand-free `FLDZ`, used by represented `long
double` values. File-scope and
block-static scalars, fixed arrays, and complete records may contain
implicitly or explicitly zeroed non-atomic long-double leaves. The aggregate
object proof fixes 104 BSS bytes, a 415-byte function with fingerprint
`BF01CC71`, eight absolute relocations, and six symbols.
Both compiler stages in the normal contract cohort rebuild the source
catalogue. ADR 0203 records the checked seed, and ADR 0207 records the new
subtraction form.

The four-vCPU GUI runtime starts every discovered CPU, reaches e1000 or
RTL8139 traffic,
passes all 62 crypto checks, opens the desktop and terminal, and completes
embedded CupidC execution at `0x01100000`. The established e1000 and RTL8139
gates continue to cover audio, input reattachment, and six EHCI storage
lifetimes. Both NIC runs print `[fpu] SSE2 enabled`,
`[fpu] boot smoke ok`, and `FPU boot smoke passed`, then finish
`feature16_asm_fpu.cc`. A private-image smoke loads the same external ELF
program twice at `0x01C00000`; cleanup releases the first arena lease before
the second load. Current private-image runs finish the full e1000 and RTL8139
frontiers in 235.259 and 232.832 seconds. Both reach
`[feature13-call] PASS checks=10`, `[feature15-x87] 7 range checks, 0 failed`,
and `[feature15] 29 checks total, 0 failed` before clean CupidC completion.
They also complete the GodSong interaction.
The gate rejects SMP, storage, crypto, exception, panic, corruption, and
illegal-instruction failure markers. The X.509 checks exercise parser,
hostname, chain state, and embedded-root lookup paths. They are not a full
trust-validation claim.

Across the root and supplemental builds, the current audit assigns 245
transforms to CupidC and none to a host C compiler. Python participates in
all 449 transforms. CupidC's total is 239 normal transforms plus three
generated installation tables and the `hello.cc`, `ls.cc`, and `cat.cc`
programs. The
438-transform root image graph has no host C or recursive Make transform.
Its four CupidASM, 185 CupidObj, two CupidLD, and one CupidDis transforms run
from the manifest-checked five-tool seed. Native hosted commands remain
explicit oracle targets. The runner rechecks the live seed cohort after each
command, and Make passes wildcard-discovered output sources through
`$(sort ...)` before generation or link. Windows and Linux therefore consume
the same root order across host locales.
ADR 0190 records the root handoff, and ADR 0196 records the Toolchain contract
handoff.

The separate private in-kernel CupidC compiler now uses one scalar cdecl
layout for direct, function-pointer, and method calls. Represented scalars and
pointers occupy four-byte slots, while `double` occupies eight. Calls retain
left-to-right evaluation, then arrange complete words at increasing source
addresses. Callees and caller cleanup use the same widths. This repairs guest
JIT and AOT behavior without moving a build owner. ADR 0198 records the
boundary.

The Browser's JavaScript runtime now keeps decimal tokens and numeric AST
nodes in a binary64 lane. Its comparisons and truth conversion use private
CupidC floating operations directly. Division preserves its IEEE result,
remainder by zero produces NaN, and malformed decimal exponents receive a
specific parser diagnostic. The asset-free `browser --selftest` command
drives the real lexer, parser, and interpreter. Its 17 result fields cover
close and large-value order, negative zero and its reciprocal, NaN comparison
and truth, NaN and signed infinity formatting, decimal literals, signed and
uppercase exponents, relational order, division and division assignment by
zero, remainder by zero, the exponent cap, and malformed-exponent rejection.

Those five numeric tables exposed integer-only lowering for fixed floating
array symbols. CupidC now records the declared element type on one-dimensional
global, automatic, block-static, and persistent REPL arrays. `float` and
`double` elements use four-byte or eight-byte storage and indirect SSE loads
and stores. Separate compiler contracts cover scalar assignment conversions
and arithmetic compound assignment. Bounds must be positive, and checked
count-by-stride multiplication rejects an overflowing allocation before
storage is reserved. Fresh subscript metadata also prevents one pointer
expression from inheriting an earlier array stride. Multidimensional floating
arrays, fixed SIMD arrays, floating pointer types, floating pointer dereference,
and floating arrays embedded in structure or class fields remain unsupported.

Direct functions and methods retain parsed fixed parameter types. Known fixed
arguments convert among represented integer, `char`, `float`, and `double`
types before cdecl layout. Represented pointer categories and integer null
forms can fill a pointer slot. Character arithmetic follows integer promotion
and uses the scalar integer conversion path when mixed with floating values.
A parsed variadic tail widens `float` to `double` and promotes `char` to `int`.
Function-pointer calls, kernel bindings, and calls without fixed parameter
metadata retain source-width arguments. ADR 0210 records these compiler
capabilities and the Browser path that requires the fixed `double` tables.

ISO test-fixture packaging no longer hides an external tool behind Python.
`test_iso/fixtures.manifest` pins every directory and file. Make declares the
same seven portable paths explicitly, and a test keeps that prerequisite list
equal to the manifest. Raw manifest text never enters Make grammar, and Make
never recurses through an unchecked fixture path. The graph includes the
manifest, fixture root, all seven members, `tools/hostbuild.py`, its imported
bootstrap helper, and the Makefile. The writer produces the tracked ECMA-119 and
`RRIP_1991A` bytes directly. Its continuation follows the directory stream,
so sequential readers retain every long name. Hostbuild then rechecks the
manifest and tree before atomic publication. Rebuilding the fixture does not
need `mkisofs`, `genisoimage`, or `xorrisofs`. ADR 0191 records the format
boundary and negative cases.

The first direct comparison matched 426 of 430 kernel artifacts. The remaining
four were one JPEG object and the three outputs that consumed it. Host FFmpeg
had rewritten the tracked progressive image differently on Windows and Linux.
The repository stores the accepted sequential baseline bytes. Hostbuild
validates and copies structurally checked SOF0 or SOF1 input, rejects
progressive, unsupported, or malformed marker streams, and gives the exact
private snapshot to
checked CupidObj. FFmpeg, `jpegtran`, `djpeg`, and `cjpeg` are no longer root
dependencies. The Linux kernel build passed in 607.7 seconds, and the Windows
root build passed in 341.6 seconds. All 430 frozen kernel artifacts match byte
for byte. The matching raw kernel is 8,490,228 bytes with SHA-256
`53770a93658e757d25f5aeab9d3e434d4a3be2a1dc3fbe4b19869e5bf9820a06`.
The fresh normal image has SHA-256
`e815d2ef67f114a26181f0e2cbde85f892cdadd487f8d9cbee9715e720800b3e`.
A private `/bin/ls.cc` JIT boot from it passed in 49.8 seconds.
The checked audit uses the canonical Windows Make branch and C locale on
every host. Direct Linux builds test the separate Linux execution branch.

The latest local normal build includes the transferred Toolchain work and
current CupidObj collision checks. The complete Toolchain target passed in
2,863.8 seconds. Its two stages matched 19 C objects, startup, and five tools.
The 18,231-byte contract manifest covers 45 inputs and has SHA-256
`27bcebb78404c8013bc56a3e2a0b9d7400cbfa040053863ed55d0d3131baaf33`.

The normal root build passed in 1,452.910 seconds. Its 8,719,780-byte final ELF
has SHA-256
`5a7a491a39372697accff9b678054b4bf84e2e68ffc3e882c5ef815d570cee06`;
the 8,518,280-byte raw kernel has SHA-256
`ecde61e586fb69bf091e3586c7c0a90d65588a9d7aa22ea6cf7d2f48dc341df3`.
The 209,715,200-byte image has SHA-256
`f488f54c023e6d1f7e9883be1f93f705fbdab4b1de3aab8a2b61b86f3863a085`.
A private copy reached the desktop and terminal and completed `/bin/ls.cc`
through the in-OS CupidC JIT in 54.025 seconds, with no panic marker.

The production Doom runtime proof uses private four-CPU images on e1000 and
RTL8139. Both NICs pass the full frontier, print the no-WAD guidance, recover
from `doom -iwad /disk/missing.wad`, and complete a later CupidC-built `ls`.
The checkout has no WAD, so this evidence stops before gameplay.

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
decoding checks all seventeen x87 instructions, the active `DC E9`
forward-subtract bytes, maximum stack depth three, balanced depth on return,
deterministic output, rollback, and same-job recovery. The old `DC E1` form
remains an explicit compatibility case.

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
source then reaches the aligned file-scope `fabs` mask block.

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

The source then reaches `pow` at line 846.

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

Two exact kernel-profile compiles of corrected `kernel/cpu/libm.cc` now
produce the same valid 16,164-byte ELF32 relocatable object with SHA-256
`c0911732361f2e1ea78aa778f834719ba12208cc2d9f0a312455a5e6a38a75b4`.
General GAS remains unsupported.

The checked seed carries this whole boundary. The normal `libm.cc` recipe now
uses the checked production wrapper with `kernel/core/types.h` and
`kernel/cpu/libm.h` frozen beside the source. The guest gate runs
`/bin/feature15_libm.cc` and requires the seven-case x87 summary, all 29
checks, and `PASS feature15_libm`. ADR 0176 records production ownership, and
ADR 0209 records the numerical correction.
