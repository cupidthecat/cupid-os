# Host dependency inventory

The deterministic active-source audit records three supported build roots:
root `all`, `user:all`, and `toolchain:all`. It evaluates Make conditionals
with `OS=Windows_NT` and `LC_ALL=C` on every host so the checked graph has one
stable shape, then covers the Linux branch with direct build tests.
`audits/active-build.json` owns the current 732-input/450-transform graph. The
language graph contains 30 assembly inputs, 295 headers, and 407 Cupid C
files. No ordinary C translation unit remains in a supported root. The
active-source digest is
`c35fa81b8d869dbd32709df36150c31fa20a2d428f1e7c40c9da8ac5986471d6`.
The 2,652,972-byte audit JSON has SHA-256
`bb99766083c1e973fe96b2bb83585ef23bee36ed3d8ee4be793e781783aae168`,
and the 12,269-byte summary has SHA-256
`1a9330b2c63a17ab907d47aaa9ab8803f19e64e810cde14f1ffa1ede2c6a817b`.
The checked Windows Clang/LLVM and Linux GCC/binutils baselines at
revision `1e079d1` predate the current CupidC ownership and remain historical
oracle evidence.

Active dglibc now consumes the checked seed's `returns_twice` support. This
changes no build owner or host dependency. Native Clang still builds the
optional decoder-driven oracle, while the asset-free QEMU self-test executes
the corrected setjmp, longjmp, quit, and error paths in Cupid OS. ADR 0213
records the seed promotion, and ADR 0214 records active adoption.

The normal root build sends no C object through GCC or Clang. Checked-seed
CupidC owns 245 transforms across the three roots. The normal cohort contains
238 checked-in sources and the generated kernel symbol table; all 239 use
`.cc`. Three generated installation tables and three example programs account
for the other six CupidC transforms. The host C compiler owns no transform in
a supported root. CupidObj participates in 191 transforms, including the
three installation-source generators, the kernel-symbol source generator, and
the normal disk-image template, ISO fixture, and Doom profile manifest. Python
participates in all 450 transforms as the checked-tool launcher and host-side
safety, parity, and publication layer. Root `all` has 441 transforms: 440
artifact transforms with a Cupid tool owner plus the Python-only size verifier,
which emits no OS artifact. No recursive Make
transform remains. One Python runner owns direct Linux execution and WSL
staging on Windows. It also owns the live post-run seed check for root tools,
checked production CupidC, and checked user CupidLD. Each wrapper
supplies the five-tool capture it already froze. Drift detected by the
post-run check prevents publication. The checked user compiler and Toolchain
contract publisher create their own output directories. On POSIX, the compiler
requires `dir_fd`, `O_DIRECTORY`, and `O_NOFOLLOW` support. On Windows, it uses
parent-relative directory handles and rejects reparse points. The pins remain
open through the final resolved-output check. Two Python-only verification or
orchestration transforms remain outside the normal root. The external-program
ABI gate runs a Cupid-built contract and a separate Python oracle. Root `all` runs
CupidASM, CupidObj, CupidLD, and CupidDis from the checked five-tool seed.
Make passes discovered output paths through `$(sort ...)` before generation
and link, so the Windows and Linux branches consume one canonical source
order. ADR 0238
records the disk-image transfer, and ADR 0245 records the publisher-owned
directory boundary. ADR 0246 records the shared invocation boundary. Python
and WSL remain required host-control components.

The normal `all` target uses Host Python for one additional read-only check.
`verify-artifact-sizes` is a direct prerequisite of `cupidos.img` and receives
`$(BOOTSTRAP_SEED_MANIFEST)`. It derives the five seed paths and declared sizes
from that selected manifest, requires the policy to agree, and checks the
five-sector boot image, both kernel ELFs, and the raw kernel. It rejects an
incomplete or expanded policy and unsafe files. A failure prevents image
publication and preserves the existing image. The verifier does not create,
rewrite, or publish an artifact. This adds orchestration, not a code-producing
host dependency. ADR 0267 records the boundary.

The first checked PE boundary built one deterministic imported i386 command
without a host compiler, assembler, linker, import library, or C runtime.
Windows loaded that probe directly and checked its exact stdout, empty stderr,
and exit 37. ADR 0247 records PE32 serialization, ADR 0248 records imports and
the loader probe, and ADR 0258 records its seed carriage.

Source head now goes further. Checked-seed CupidC, CupidASM, and CupidLD build
native CupidASM, CupidC, CupidDis, and CupidObj images with a repository-owned
Windows startup and runtime. Windows runs help plus useful success and failure
paths for all four tools and a direct runtime contract. This remains a
development proof, not a normal-build dependency removal: Windows still uses
WSL for checked producer paths. CupidLD's publication calls and checked native
seed carriage remain open. The normal graph has 450 transforms with the same
artifact owners. ADR 0268 records the four-tool boundary.

Checked-seed CupidLD publishes ELF and PE output with native file operations. It
creates an adjacent candidate with exclusive-create semantics, writes and
closes it, reopens the file, checks its size and contents, then replaces the
destination. This adds no host code generator. On POSIX, CupidLD requests mode
`0777`; the process umask may remove any permission bits. The operation still
depends on a caller-controlled stable directory and does not provide a
destination lock, directory pin, or crash-durability guarantee.

Checked-seed CupidObj now authors the complete tracked ISO fixture through
`iso-fixture`. Python remains the tree freezer, independent renderer, native
path checker, drift checker, and guarded publisher. ADR 0239 records the
source capability, ADR 0240 records seed carriage, and ADR 0241 records the
production handoff.

Checked-seed CupidObj authors the normal Doom profile manifest through
`profile-manifest`. The wrapper derives a bounded snapshot and an independent
Python oracle from one stable capture, runs CupidObj from the exact frozen
seed, requires byte parity, and rechecks the seed, live profile inputs,
candidate, output directory, and existing output. An adjacent no-follow lock
guards publication. Identical bytes retain their timestamp, while changed
bytes publish atomically. Python retains discovery, native-path safety,
freezing, parity, drift checks, locking, and publication. ADR 0242 records the
source boundary, ADR 0243 records seed carriage, and ADR 0244 records the
normal handoff.

CupidASM participates in five production transforms. The fifth assembles
`test_iso/fixtures/big.bin` from `test_iso/big_pattern.asm` through the checked
seed. Python freezes the seed and source, verifies the exact 4,096-byte lane
pattern, and controls atomic publication. It does not author the candidate.
NASM remains an optional oracle for the other production assembly sources.
Its different `$` behavior inside `TIMES` makes this fixture the single
explicit byte-parity exception. ADR 0227 records the boundary.

Checked-seed CupidObj generates the three installation-table sources from the
same ordinal inventories. The production outputs match their pre-transfer
files and the Python oracle, and the public operation fails transactionally on
malformed, duplicate, mixed, or oversized lists. Each Make recipe depends on
`$(CUPIDOBJ_INPUTS)`. `tools/hostbuild.py` is no longer a prerequisite or
recipe owner for these outputs, though it remains their oracle and keeps its
other roles. Python still participates in all 450 transforms because the
checked-seed runner uses it to launch CupidObj. ADRs 0201, 0203, and 0204
record the operation, its first seed promotion, and the ownership transfer.
ADRs 0205 and 0206 record the request-boundary and linked-symbol corrections.
The checked seed, source implementation, and Python oracle reject distinct
inventory paths that map to the same complete wrapped symbol. They preserve
the exact docs and home BMP alias used by the active image. The current seed
has the bounds, ordering, and symbol-domain corrections. This changes no
dependency count.

Checked-seed CupidObj now generates the packed kernel-symbol `.cc` source from
canonical CupidDis text in the normal build. Hostbuild freezes the pass-one
kernel and seed, preserves CupidDis's exact text for CupidObj, and renders the
same source independently as a parity oracle. A failed tool, missing output,
byte mismatch, or changed live input leaves the existing destination intact.
CupidDis owns symbol inspection, CupidObj owns source generation, and
checked-seed CupidC owns compilation. Python still coordinates the operation
and checks its result. ADR 0222 records the command, ADR 0223 records seed
carriage, and ADR 0224 records the recipe transfer.

A current extension inventory finds seventeen tracked `.c` files outside
`TempleOS/`, with none in a supported transform:

- Seven historical copies: `bin/cupidc.c`, `bin/cupidc_lex.c`,
  `bin/cupidc_parse.c`, `bin/fat16.c`, `bin/fat16_vfs.c`, `bin/kernel.c`, and
  `bin/terminal_app.c`.
- Three superseded implementations: `kernel/core/scheduler.c`,
  `kernel/gui/notepad.c`, and `kernel/gui/terminal_ansi.c`.
- One dormant, unlinked runtime draft: `kernel/lang/cupidc_runtime.c`.
- Six deliberate host test or oracle fixtures: `tests/kernel_exec_contract.c`,
  `tests/kernel_process_contract.c`,
  `tests/usb_interrupt_ownership_contract.c`,
  `tests/usb_msc_lifetime_contract.c`,
  `tests/usb_reconciliation_runtime.c`, and
  `toolchain/tests/elf32_oracle.c`.

The audit classifies the first seven as `historical_copy`, the next three as
`superseded`, and the remaining seven as `not_reached`. The stale Make recipe
for `kernel/gui/terminal_ansi.c` has been removed. That source remains
superseded by the linked `kernel/gui/ansi.cc` implementation.

The repository also tracks 406 `.cc` files outside `TempleOS/`. The active
graph reaches 403 of them and four generated `.cc` sources, for 407 active
Cupid C inputs. The generated sources are `kernel/cpu/ksyms_data.cc`,
`kernel/util/bin_programs_gen.cc`, `kernel/util/demos_programs_gen.cc`, and
`kernel/util/docs_programs_gen.cc`. Renaming the dormant runtime draft or the
host fixtures would claim active Cupid ownership they do not have. No active
Cupid-owned `.c` source remains due for a `.cc` rename.

CupidASM's `align` statement adds no host tool to that graph. The shared
assembler computes raw padding from the absolute `ORG` address, records ELF32
section alignment, keeps NOBITS padding out of the file, and respects fixed
region bases. The active FPU demo can state its FXSAVE alignment directly, so
it no longer relies on section ordering or an external assembler. No build
owner or dependency count moves in this increment.

The first direct Windows and Linux comparison matched 426 of 430 kernel
artifacts. The four differences were the JPEG object and the three artifacts
that consumed it. Host FFmpeg had converted the tracked progressive image to
different byte streams on the two systems. The repository stores the
accepted sequential baseline bytes. Hostbuild freezes the source and runs
checked CupidObj `wrap-jpeg`, which accepts sequential SOF0 or SOF1 input and
rejects progressive, unsupported, or malformed frames. Python then checks the
accepted snapshot independently and requires unchanged bytes. This removes
FFmpeg, `jpegtran`, `djpeg`, and `cjpeg` from the root dependency set. The
Linux kernel build passed in 607.7 seconds, and the Windows root build passed
in 341.6 seconds. Their 430 frozen kernel artifacts match byte for byte. A
fresh normal image passed a private `/bin/ls.cc` JIT boot in 49.8 seconds.

Python also rechecks the live manifest and source and controls atomic
publication. It no longer makes the first production acceptance decision.
ADR 0231 records the capability, ADR 0234 records seed carriage, and ADR 0235
records the production transfer.

Private CupidC scalar comparisons change no build owner or host dependency.
The in-kernel emitter now handles all six matching or mixed `float` and
`double` relations with C's unordered behavior. Focused byte tests still use
a host compiler as an optional execution oracle, while the normal kernel
object is produced by checked-seed CupidC and the guest frontier executes the
result.

Private CupidC scalar truth has the same ownership boundary. The in-kernel
emitter now materializes `float` and `double` truth for unary `!`, conditional
selection, and every structured control form. The focused host oracle checks
the active instruction helper, while checked-seed CupidC produces the normal
kernel object and the guest frontier executes each parser path. No host
compiler, assembler, linker, or packaging dependency was added or retired.

The in-kernel symbol boundary has 326 value-returning bindings and 231
verified `void` functions. Its value group contains 208 promoted integers, 40
unsigned words, 25 `float`, 25 `double`, 19 character pointers, and eight
other pointers. Explicit `uint32_t`, `size_t`, and `swap_handle_t` results
publish `TYPE_UINT`; narrow unsigned results retain integer promotion. This
metadata is compiled into the checked-seed-owned kernel object.

The 46 graphics and GUI bindings add no host dependency. All 43
direct bindings target implementations already linked into the kernel. Three
theme accessors expose existing constant objects by address. Checked-seed
CupidC produces the normal kernel object, while the private compiler
uses the completed table for embedded AOT and JIT work. The fixed runtime
stores its disposable outputs in guest RamFS and does not need host storage or
a HomeFS publication step. The affine inverse correction remains ordinary
CupidC-owned kernel source and uses the existing `__udivdi3` runtime helper
with local magnitude, sign, coefficient, and translation range handling. The
test creates its theme, image, font, and surface through guest APIs, then reads
exact font and filtered-surface pixels back through those APIs.
GodSong's settings line and the popup's post-acquisition input marker are
guest serial output. They replace a timed settle and startup-only graphics
diagnostic without adding a host service or changing a build owner.

One optional hosted oracle includes the production transform source and
executes its inverse routine across determinant and overflow boundaries when a
C++ compiler is available. It skips when none is installed. The normal build,
checked-seed object build, and private QEMU gate do not invoke that compiler.

The Browser number work keeps the existing ownership boundary. Its lexer, AST,
and interpreter remain source-wrapped inputs compiled by private in-OS CupidC.
The radix and separator scanner, Unicode-aware primitive conversions and
relations, equality, remainder, pool-backed concatenation, and compound
operations add no host math library or compiler step. Checked interning,
native-function identity, array length growth, explicit index limits, and
range-safe finite formatting stay in the same private runtime. Saved
assignment references and the tagged structure typedef that carries them also
compile in the guest. Checked-seed CupidC builds the parser that adds tagged
typedef bodies, preserves their structure index, checks allocation arithmetic,
and restores committed REPL record definitions; no host parser is introduced.
Private CupidC joins the larger active script from bounded string tokens using
its existing data section. Integer literal and constant-expression checks use
fixed 32-bit arithmetic inside the private compiler. Focused hosted tests
remain optional oracles;
checked-seed CupidC still builds the production lexer and parser objects. The
four-CPU guest contract requires ten useful diagnostics, the 26-field
`browser --selftest` marker, recovery, and clean JIT completion. ADRs 0210 and
0218 record the two number slices, and ADR 0219 records the tagged typedef
support.

Exact private decimal literals keep the same ownership boundary. The
in-kernel lexer uses only fixed-size integer arithmetic and does not call a
host conversion routine or math library. Checked-seed CupidC still produces
the production lexer and parser objects, while hosted payload and diagnostic
tests remain development oracles. This step adds or retires no host compiler,
assembler, linker, or packaging dependency. ADR 0217 records the boundary.

Private floating increment and decrement retire no host dependency. The
in-kernel parser and SSE emitter own local, parameter, global, statement, and
`for` update behavior. A host compiler checks extracted active emitter bytes
as an optional oracle, while checked-seed CupidC builds the production parser
object and the guest frontier executes the result.

Private mixed-width cdecl calls keep the same ownership boundary. The
in-kernel parser now gives represented scalars and pointers four-byte slots
and `double` values eight-byte slots across direct, indirect, and method
calls. Callees use the same widths for parameter addresses, and callers clean
the complete outgoing area. Direct functions and methods with parsed fixed
parameter types convert represented integer, `char`, `float`, and `double`
arguments to the declared slot type before the call. Represented pointer
categories and integer null forms can fill a pointer slot. A represented
object pointer can fill a fixed `int` or `unsigned int` slot as one unchanged
i386 word. Narrow and floating destinations remain rejected. A parsed variadic
tail widens `float` to `double` and promotes `char` to `int`. Function-pointer
calls, kernel bindings, and calls without parameter metadata keep their
source-width slots. A focused host-built runtime remains an optional ABI oracle.
Checked-seed CupidC builds the production parser object, and the four-CPU guest
frontier executes ten mixed-width feature13 calls. No host compiler, assembler,
linker, or packaging dependency was added or retired.

Positive fixed-array bounds, checked count-by-stride multiplication, REPL data
reservation, character promotion, and fresh pointer subscript metadata also
live in the checked-seed-owned parser object. They add no host build step.
Fixed-array typedef fields keep their complete object size and record-element
identity through direct and pointer member access. One shared lvalue walk
handles indexed record members whether the outer record is a named object or
an array element. These parser changes add no host build step.

The i386 runtime contract and all fifteen Toolchain contracts use `.cc`.
The checked seed produces stage-two and stage-three tools, each stage compiles
the full contract set, and CupidLD links the matching static i386
executables. The seventeen newly compiled objects and sixteen executables must
match byte for byte across stages before the runtime probe runs and
publication occurs. The publisher validates a dedicated
`cupidc-contracts` target before work and immediately before promotion. An
existing destination must already verify as a complete cohort. Arbitrary
directories, source trees, files, and symbolic links are rejected without
modification. Exact initial, private, and newly discovered contract
inventories catch added or removed inputs and restored edits that changed a
copied file. The manifest binds a 62-input contract inventory, including the
Windows startup, runtime, direct runtime contract, and `direct.h`, the user syscall ABI contract and its six
declarations, the Toolchain Makefile, the publisher, and the independent
Python ABI oracle. It separately binds the
checked seed and 47-file fixed-point source inventory. Each run derives its
cohort from the requested executable and verifies all artifact hashes and both
live inventories. Hashing, decoding,
schema validation, and build-plan use all consume one captured seed-manifest
byte sequence. Host Python still orchestrates the check. Native contract
binaries are optional
`native-oracles`, so their host compiler and linker do not enter the
supported graph.
The current promoted-seed user frontier passed in 3,291.317 seconds after the
publisher rebuilt and atomically installed a complete 21-artifact cohort.
Stage two and stage three matched. Windows used the existing WSL runner, and
Host Python retained snapshot, launch, and publication duties. The proof did
not add a host compiler, assembler, linker, or binary utility to the supported
path.
The same Cupid-owned runtime now formats the cohort's signed and unsigned
64-bit diagnostics, padded hexadecimal fingerprints, and precision-bounded
string views. These paths do not borrow a host libc formatter.

The ABI verification captures the exact bytes of its six declaration inputs,
compares the reviewed i386 contract, and rechecks every input before success.
The external-program runtime gate gives hello, ls, and cat separate private
copies of the staged image. ADR 0133 records these consistency boundaries.
A fresh build in a unique output directory passed in 10.492 seconds and
reproduced all six hashes from the promoted-seed frontier. Disposable staged
copies returned 0 for hello in 54.546 seconds, ls in 52.637 seconds, and cat in
80.043 seconds. Cat used a 62-byte marker-shaped fixture and passed the
negative serial-event boundary. The source and evidence images remained
unchanged at SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.

Linux runs the checked i386 seed for all six user artifacts. Windows runs the
same seed through WSL. A separate frontier uses private snapshots of the two
native hosted drivers and compares all six outputs with the seed. Clang and
its native linker build those optional drivers, so the comparison path does
not establish a Windows fixed point.

The strict frontier must compile all 155 checked-in sources twice. Every
transferred Make recipe names its exact recursive header closure and common
checked-seed controls. Poisoned-host recipes, strict syntax, focused tests,
and the normal-image gate remain part of the proof. The full 155-root
frontier passes twice against a 445-file frozen snapshot with SHA-256
`99d03de14f544f6a76d21ed147e62018873f1e2e8dfa2f4459830b69314432c2`.
The two object sets are byte-identical; each totals 3,749,796 bytes. The combined 155-root graph
also carries the ISO fixture as an explicit image input and passes the strong
four-vCPU runtime gate with both NICs.
ADRs
0110 and 0111 record the earlier transfers, ADR 0115 records
the first source-driven ownership, ADR 0123 records the latest production
transfer, ADR 0124 records the 111-root naming transfer, ADR 0126 records
the fixed-point rename and old-seed proof, ADR 0129 records the lexer
handoff, ADR 0135 records the Nuked OPL3 transfer, ADR 0139 records the
JPEG and glyph-raster transfer, ADR 0167 records the FPU and SMP transfer,
ADR 0176 records the libm transfer, ADR 0180 records the kernel entry and
SIMD transfer, ADR 0181 records the final strict-root transfer, and ADR 0184
records the 83-root Doom transfer.

The Doom wrapper fixes exact three-source and 80-source allowlists and freezes
the selected source with all 291 `.h` and `.inc` inputs visible through the
two compiler profiles. It recursively checks visible `.c` and `.cc` files
beneath the Doom tree before and after a compile. Its always-checked manifest
detects source removal. A legacy `.c` file, an unlisted `.cc` file, header
membership or byte changes, a symbolic link, or an NTFS junction fails before
publication. An unchanged scan preserves the manifest timestamp. This closes
the host C dependency for normal Doom objects without changing strict C or
ordinary GNU mode.

The combined cohort's four-vCPU GUI proof starts every CPU, forces the CSPRNG
through RDRAND, passes all 62 crypto, ASN.1, and X.509 checks, reaches e1000
traffic, opens the desktop and terminal, and completes embedded CupidC
execution at `0x01100000`. The dual-NIC contract also covers audio, TrueType
glyph use, an exact 8-by-8 JPEG decode, UHCI input reattachment, and six EHCI
storage lifetimes. The private-image gate loads and
reaps the same external ELF program twice at `0x01C00000`, with lease release
between the two runs.

The Doom handoff uses the same four-CPU e1000 and RTL8139 frontier. Earlier
private boots returned from two consecutive missing-IWAD launches. The fixed
frontier now runs normal WAD discovery, an explicit missing path, the
shell-return marker, and a fresh CupidC-built `ls` after Doom recovery. The
stateful frontier also passes after swap keeps one FAT handle open. No host C
tool participates. The checkout has no WAD, so gameplay remains outside this
host-dependency proof.

CupidC represents operand-free GNU assembly statements inside functions and
emits their exact no-operand i386 instructions. The checked seed uses that
capability for e1000, the desktop, sockets, and TCP. It also represents all
eight port-I/O helpers in unchanged `kernel/core/ports.h`. The scalar forms
retain accumulator and port widths. The repeated word-string forms retain
read/write pointer and count operands, write both results back, and restore
ESI or EDI. INSW accepts one `memory` clobber. The 14-source handoff uses this
path in production.

Checked-seed CupidC represents file-scope GNU basic assembly as a
translation-unit effect rather than passing it to GAS. The exact Task 23
fixture emits the twelve opening x87/SSE floating wrappers from the then-named
`libm.c` in
248 text bytes through Cupid's x86 encoder, with twelve global function
symbols and no relocations.
The checked seed resolves named function-body operands without invoking GAS.
It now emits the exact x87 statements in `libm_pow_impl()` and
`libm_powf_impl()`. The double form has five `double` memory operands. The
mixed form has a `float` output, two `float` inputs, and two `double` inputs.
Each 116-byte focused function uses no relocations and returns the x87 stack
to its incoming depth. The active power and exponent paths use `DC E9` for
the intended `x - round(x)` remainder, while the checked seed keeps the
legacy `DC E1` form for compatibility. The checked seed also emits the exact `sqrtsd %1, %0`
statement with a `double` `=x` output and a `double` `x` input. The focused
function has 65 text bytes and no relocations. It also emits the exact x87
statement in `libm_atan2_impl()` with one `double` `=m` output, two `double`
`m` inputs, and one `memory` clobber. That focused function has 53 text bytes
and no relocations. It also emits the exact x87 statement in
`libm_exp_impl()` with one `double` `=m` output, two `double` `m` inputs,
and one `memory` clobber. That focused function has 71 text bytes, no
relocations, and balanced x87 depth. The checked seed also emits the aligned
32-byte `fabs` mask block and the following `fabs` and `fabsf` wrappers. The
mask labels are local `STT_NOTYPE` symbols at `.rodata` offsets 0 and 16.
The wrappers contain 15 and 14 text bytes and carry one `R_386_32`
relocation each. The checked seed also emits the following `floor`, `floorf`,
`ceil`, `ceilf`, `round`, `roundf`, `trunc`, and `truncf` wrappers. The
family saves and restores the x87 control word around `FRNDINT`, selects all
four source rounding modes, occupies 384 text bytes, and has no relocations.
The checked seed also emits the following `fmod` and `fmodf` definitions. Each
35-byte function loops on `FPREM` until status-word C2 clears, discards the
divisor, returns the result through XMM0, and has no relocation. The checked
seed also emits the aligned `libm_log2e_const` and `libm_ln2_const` block
and the following eight exponent/logarithm wrappers. The constants occupy 16
`.rodata` bytes at alignment eight. The wrappers add 264 text bytes and four
`R_386_32` relocations, reach at most x87 depth three, and balance ESP and
x87 depth. The checked seed also emits all 18 remaining cdecl bridges. The
unary and binary float or double shapes copy the original argument words,
call matching external `libm_*_impl` symbols, reclaim the words, and move
ST(0) into XMM0. The family occupies 558 text bytes with 18
`R_386_PC32` relocations. The corrected source is
`kernel/cpu/libm.cc`. Its 43,736 source bytes have SHA-256
`baffe801c7573b8500c60251298a753f60732608d58443178be8ce9ab809ef93`.
The checked wrapper freezes that source with `kernel/core/types.h` and
`kernel/cpu/libm.h`, then emits a 16,164-byte ELF32 relocatable object
with SHA-256
`c0911732361f2e1ea78aa778f834719ba12208cc2d9f0a312455a5e6a38a75b4`.
The normal recipe is CupidC-owned. ADR 0176 records the transfer, and ADR
0209 records the active numerical correction.

The USB lifetime work retires no additional compiler transform, but it
supplies the runtime contract for EHCI and UHCI ownership. Reconciliation
keeps failed work durable, rotates backed-off retries fairly, and reuses
device addresses and block slots after safe release. Hub callbacks report
changes while the core owns teardown, reset, enumeration, acknowledgement,
and edge rereads. Controller-local generations prevent stale cancellation
from retiring a reused interrupt slot, and cancellation waits for callback
and DMA quiescence. The UHCI IRQ path acknowledges only write-clear interrupt
bits, so it cannot erase HCHalted while another CPU proves transfer teardown.
A quarantined address requires a proved reset before companion handoff. Block
references reject saturation, and mass storage restores its online state if
unregister fails. Compiled fixtures and 45 USB tests pass. The 123 GUI gate
unit tests cover detach and reattach expectations and their failure forms.
The live e1000 and RTL8139 runs pass UHCI input reattachment and six EHCI
storage lifetimes. ADR 0109 records these rules.

The checked seed also accepts the exact per-CPU `mov %%gs:0, %0` form with one
modifiable four-byte object or `void` pointer output. The integer atomic slice
handles load, store, exchange, fetch-add, and fetch-or on represented one-,
two-, and four-byte objects. This completes all three `percpu.h` header roots
and lets checked-seed CupidC emit `kernel/smp/acpi.cc`,
`kernel/smp/mp_tables.cc`, and the active EHCI port-change path.

The public frontend now represents decimal `float` and `double` constants as
exact IEEE bits without calling a host floating library. The IR and SSE
emitter cover represented integer-to-floating conversions,
floating-to-signed conversions, floating-to-unsigned conversions through
represented four-byte targets, and mixed represented integer and floating
arithmetic.
The checked seed also covers an explicit non-atomic `double` to
`unsigned long long` cast. Unsigned four-byte input uses an exact split
across the sign boundary. Runtime four-byte output widens binary32 exactly
and uses the same 2^31 split, while unsigned-wide output is decomposed around
2^32. The seed carries the earlier conversion paths, and
`kernel/lang/cupidc_lex.cc` builds through the checked wrapper.
CupidC writes target-width static floating constant data for
scalar and aggregate leaves. Parentheses, unary signs, direct conversion
between `float` and `double`, and signed zero are represented without a host
floating library. The checked seed compiles `kernel/gfx/jpeg.cc` twice to the
same 21,120-byte object with SHA-256
`ccabae9e3b979031079f1ed72189c990f3aee4aa773c6ec742b5ccc263570851`.
Its production recipe freezes four headers, and the guest frontier checks a
byte-fixed baseline decode.
CupidC also compares matching or mixed-width `float` and `double`
operands with all six C operators. `UCOMISS` and `UCOMISD` emission handles
ordered values, signed zero, infinities, quiet NaN, and signaling NaN. The
checked seed compiles `kernel/gfx/glyph_raster.cc` twice to the same
11,744-byte object with SHA-256
`83d2f4cac28abbc5bb8a92020ab7fb57251b1b927b4fdbc40981f29556aa1e80`.
The normal GUI path exercises its glyph output.
The checked seed also evaluates static binary32 and binary64 arithmetic with
integer-only target semantics. Unary signs, addition, subtraction,
multiplication, division, comparisons, casts, scalar truth, short-circuit
logic, conditionals, enumerator constants, and represented signed or unsigned
integer conversion through 64 bits use no host floating operation or math
library. This path emits the unchanged Doom automap object. Automatic
non-atomic `long double` values now use the shared x87 path for object
transport, floating-width conversion, unary plus and minus, and addition,
subtraction, multiplication, and division. Bounded finite normal decimal `L`
tokens round an exact ratio to a 64-bit explicit significand and biased x87
exponent. Their 80-bit loads and stores come from Cupid's x86 model. Direct
and indirect fixed, variadic, and unprototyped arguments use twelve cdecl
bytes. Functions return the value in x87 `ST0`, and direct or indirect callers
store it in a twelve-byte snapshot.
`va_arg(long double)` copies twelve bytes and leaves the cursor at the next
four-byte slot. Static-duration scalars, fixed arrays, and complete records
may contain non-atomic long-double leaves. Implicit initialization zeros the
complete object. Explicit leaves accept a represented integer constant
expression or a bounded decimal `L` literal with parentheses and unary signs.
The emitter writes ten exact value bytes, clears both padding bytes, and
chooses `.bss`, `.data`, or `.rodata` from the payload and qualifiers. Runtime
`float`, `double`, and automatic `long double` values use Cupid-owned zero
comparisons for unary `!`, `&&`, `||`, the controlling operand of `?:`, the
conditions of `if`, `while`, `do`, and `for`, and conversion to `_Bool`. Both
signed zeros are false; finite nonzero values, subnormals, infinities, and NaNs
are true. Runtime casts, assignments, arguments, and returns convert between
`long double` and signed or unsigned integers at 8, 16, 32, and 64 bits.
Integer output restores the x87 control word after truncation. Unsigned 64-bit
corrections temporarily use 64-bit x87 precision, retain the caller's rounding
mode, and restore the complete saved control word. Static initializer
conversion also works for every represented integer kind and for an enum
whose compatible integer type has the represented target layout. The frontend
packs integer magnitudes into exact x87 metadata. For integer destinations
other than `_Bool`, it truncates long-double input toward zero before the
range check. `_Bool` tests the original floating value, so both signed zeros
become false and every represented finite nonzero value becomes true. The
fixture converts `-0.5L` to both targets, producing true for `_Bool` and zero
for an unsigned integer. Integer-valued zero stays a `ZERO` record. Linear IR
validates every static `INTEGER` leaf against the standard target kind and
representation tables. Primitive bases use their canonical target size,
signedness, and alignment. An enum, its unwrapped base, and its compatible type
agree on size, signedness, integer, object, and completeness flags, as well as
alignment. A `QUALIFIED` node copies referenced alignment unless it introduces
`_Atomic`. An atomic introduction at any layer raises alignment to at least
the target atomic alignment. An `ALIGNED` node requires an explicit,
nonzero power-of-two alignment and may lower the referenced alignment.
Static long-double truth, all six comparisons, short-circuit logic,
conditional selection, and conversion to or from binary32 and binary64 use
the same target-only value model. The shared payload rule accepts canonical
x87 zero, subnormal, normal, infinity, and NaN encodings. Binary32 and
binary64 infinities widen with their sign, while every source NaN widens to a
canonical quiet x87 NaN. Narrowing produces target infinity or a canonical
quiet NaN. The folded expressions become final initializer records and add no
runtime IR. Static `+`, `-`, `*`, and `/` use unsigned 128-bit target
arithmetic with nearest-even rounding and gradual underflow. They also become
final initializer records. This work introduces no host floating operation or
math-library dependency.
Hexadecimal floating literals, binary32 and binary64 subnormal literals,
hexadecimal or subnormal long-double literals, decimal ratios beyond the
bounded parser, mixed integer and floating runtime arithmetic or conditionals,
and atomic or long-double updates remain open. Matching or mixed-width floating
conditional arms and the four arithmetic compound assignments retain their
established x87 path. All six matching or mixed long-double comparisons use a
balanced `FUCOMIP` sequence. ADRs 0196, 0199, 0202, and 0229 record the current
long-double, comparison, truth, and literal boundaries. ADR 0250 records
runtime conversion to unsigned four-byte targets, ADR 0251 records static
long-double data, ADR 0253 records runtime conversions between `long double`
and integers, ADR 0254 records static initializer conversion, ADR 0255
records static controls and finite width conversion, and ADR 0256 records
canonical x87 classes and special-value conversion. ADR 0260 records static
long-double arithmetic.

The checked seed and source head have 604 x86 forms, 249 canonical mnemonics,
64 registers, and fingerprint `55A8970F`. The catalogue includes signed x87
`FILD` and `FISTP` memory operands at 16, 32, and 64 bits and canonical
`SETP` and `SETNP` byte predicates. Four forms cover canonical
16-bit and 32-bit SHRD with immediate or fixed CL counts. The forward x87
`FSUB ST(1), ST(0)` form encodes as `DC E9`. ADR 0203 carries `FLDZ` and the
three preceding x87 forms into the trust root, ADR 0208 carries forward stack
subtraction, ADR 0226 records SHRD, ADR 0228 records SHRD's first seed
carriage, ADR 0243 records an earlier seed, ADR 0252 records the x87 integer
forms, ADR 0258 records the preceding seed, ADR 0259 records the parity
predicates, and ADR 0265 records the current promotion.
These counts supersede older source-head references below. The new inspection
coverage changes no production owner and removes no host dependency.

The aggregate proof adds no host dependency. Checked-seed CupidC emits two
24-byte arrays and two 28-byte records into 104 BSS bytes. A separate
415-byte access function has fingerprint `BF01CC71`, eight absolute
relocations, and six symbols, and the hosted i386 runtime checks both initial
zero state and member transport.

Later sections preserve ownership wording that accompanied earlier capability
slices. The current 155-source checked-in cohort, complete `.cc` naming, and
the generated-symbol transfer supersede those snapshots.

ADRs 0113 and 0114 supersede later statements that comma expressions,
represented function-pointer casts, typed static nulls, or every GNU
attribute remain open. The checked seed carries comma sequencing, same-width
function-pointer representation casts, known-true loop reachability,
general-register and EFLAGS snapshots, and canonical `weak`, named `section`,
and `unused` metadata. ADR 0115 moves the first 20 passing roots into
production, and ADR 0123 moves eight more roots plus generated kernel symbols.
ADR 0124 renames the 111 exclusively CupidC-owned roots to `.cc`. ADR 0126
finishes the naming work for the shared Toolchain roots while preserving C
semantics in native recipes. ADR 0176 transfers libm, ADR 0180 transfers
the kernel entry and SIMD roots, and ADR 0181 transfers
`kernel/core/string.cc`. No strict checked-in kernel or driver root remains
host-owned.

The checked seed clears the former language blocker in
`kernel/audio/nuked_opl3.cc`. The frontend finalizes its ordinary declaration
and inline definition as one C11 external definition, and two full compiles
produce the same validated 40,424-byte object. The closed normal recipe,
frontier, image, and dual-NIC runtime gates pass. The wrapper compiles
from a private copy of the source and its three headers, then rejects live
input drift before replacing the object. This retires one host C root
dependency.

CupidC accepts GNU `used` and `__used__` on canonical file-scope
objects and functions. The Linear IR and object boundaries validate the
frozen flag, and the focused object proof reproduces the generated
`section(".ksyms"), used, aligned(4)` declaration. The generated
`kernel/cpu/ksyms_data.cc` now compiles through the normal checked wrapper.
Its packed i386 words preserve the exact 114,851-byte blob. The current
115,264-byte object has SHA-256
`a5eb7e848b156754dc87203e806411ed006694167b5a67dd8233d8ef9f71a65c`.
Checked CupidDis extracts canonical text from a frozen pass-one kernel, and
checked CupidObj serializes the blob. Python freezes the seed, independently
renders the expected bytes, and rejects malformed output, an empty text-symbol
set, i386 address overflow, missing output, parity failure, or live input drift
before atomic publication. This retires the generated root's GCC or Clang
dependency. ADR 0116 records the language boundary, ADR 0123 records the
compiler transfer, and ADR 0224 records the generator transfer.

The checked seed accepts the independent `r` and `c` inputs used by exact
control-register moves and RDMSR. It compiles `kernel/cpu/idt.cc`,
`kernel/mm/paging.cc`, and `kernel/smp/lapic.cc` twice to byte-identical
validated objects of 8,756, 2,336, and 4,184 bytes. The emitter writes the
privileged i386 instructions directly and does not invoke a host assembler.
The normal recipes use those objects, retiring three GCC or Clang
dependencies. ADR 0117 records the capability and its unsupported forms, and
ADR 0123 records the transfer.

The checked seed handles the exact volatile
`call 1f\n1: popl %0` state read in `kernel/lang/as.cc` and
`kernel/lang/cupidc.cc`. It requires one modifiable four-byte integer `=r`
output and emits a zero-displacement call followed by a pop through Cupid's
x86 model. Both roots compile twice to byte-identical validated i386
relocatable objects under the complete kernel profile, and their normal
recipes use those objects. ADR 0118 records the language boundary, and ADR
0123 records the production transfer.

The checked seed compiles both exact volatile `fxsave (%0)` statements in
`kernel/core/process.cc`, retaining one four-byte object or `void` pointer
`r` input and the `memory` clobber. The shared x86 path emits `0F AE 00` at
`[EAX]`. Two full-profile compiles produce the same validated 30,216-byte
object. The normal recipe now uses that object. Native contract binaries
remain host-built. ADR 0119 records the language boundary, and ADR 0123
records the ownership transfer.

The checked seed represents the GNU `Nd` constraint in
`kernel/cpu/pic.cc`. It chooses the DX alternative and emits both active
8-bit port templates without a host assembler. The unchanged root produces
a 2,408-byte object with SHA-256
`c1855a19e0cd285953996344493dcefe916f06d89fed706219718920b4d2ea5d`.
The normal PIC recipe now uses it. ADR 0120 records the capability, and ADR
0123 records the transfer proof.

The checked seed writes the FPU status word, x87 control word, and MXCSR
through the exact `=m` GNU assembly outputs in `kernel/core/panic.cc`. The
frontend, Linear IR, and i386 emitter keep the 16-bit or 32-bit destination
width and evaluate its address once. The checked seed also
supports the source's later exact `call 1f` template. Two complete profile
compiles produce the same validated 10,212-byte object with SHA-256
`84daa51a65d6970ae7a7918b05fe64b7676c39d3309264375e349cf0ae20d428`.
The normal panic recipe now uses this object. ADR 0121 records the language
boundary, and ADR 0123 records the ownership transfer.

Value-preserving bit-field assignment changes compiler capability without moving another output. Four focused functions cover unsigned, signed, full-width, pointer-derived, and indexed stores. The execution oracle checks the stored value, neighboring bits, arguments, and stack state. Both checked compiler stages build the shared frontend, Linear IR, emitter, and normal contract; GCC or Clang builds only the optional native copy. The proof adds no transform beyond the current production cohort and retires no executable, linker, assembler, or object-tool dependency.

Ordinary narrow bit-field promotion serves the production Doom cohort. The
frontend and Linear IR retain and validate the direct member behind an
eight-bit `unsigned int` field's promotion to signed `int`. A 127-byte exact
object and eight decoder-driven executions cover the active shift and mask
forms. The checked seed uses this support to emit
`kernel/doom/src/i_video.cc`; two exact-profile compiles reproduce its
9,288-byte object with SHA-256
`d04e91844763391d4224d14aefce64ece02a95c9a99c604e9ef5b1392974dd20`.

The checked Doom compatibility path retires all 83 normal host C transforms.
CupidC preserves explicit non-atomic pointer casts around static addresses and
emits the active corrected dglibc jump block through Cupid's x86 model.
Repeated checked-seed compiles agree on the 93,332-byte, 17,084-byte, and
10,352-byte compatibility objects, and the normal recipes consume them through
the closed production wrapper. Native VFS rename, checked cache failure
handling, FAT durable publication, HomeFS container ownership and batching,
the repeated exit lifecycle, and production config helpers have asset-free
guest coverage. A staged IWAD is still needed for gameplay, input, audio,
menu-driven save/load, and persistence across reboot. QEMU remains a test
dependency, not a normal build producer.

Eight-byte integer values cross the shared path through full-width constants, matching conditional results, fixed direct and indirect call results, object access, initialization, plain and chained assignment, declared parameters, named arguments, ellipsis and unprototyped call arguments, variadic reads, discard, returns, arithmetic, unary operations, shifts, bitwise operations, comparisons, logical operations, conditions, switch dispatch, and conversion to or from represented integer widths. File objects, block statics, fixed automatic objects, pointer dereferences, ordinary members, and indexed elements use private eight-byte frame snapshots. The i386 emitter restores the low word to EAX and the high word to EDX on return. Calls publish packed post-conversion actual types in emitted instruction order, which gives an open-position wide integer two adjacent stack words and advances a wide variadic cursor by eight bytes. The CupidC-built socket and TCP objects use this production path. Both checked compiler stages build the deterministic result, object, parameter, operation, and call-position contracts; host-built copies are optional oracles.

The floating work first landed without moving production ownership. The shared path copies matching `float` and `double` values through objects, calls, variadic reads, and returns. It now evaluates same-kind unary plus and minus and binary addition, subtraction, multiplication, and division. Every changed x87 result is stored immediately at its C width. A `float` rounds into a fresh four-byte semantic slot, and a `double` receives a fresh private eight-byte snapshot. The exact `libm_tanh_impl` guard pins nested `double` arithmetic with call-produced operands. The execution model checks operand order, immediate spills, selected IEEE patterns, call alignment, and frame state. It does not execute native x87 code. Both checked compiler stages now build the proof, while GCC or Clang provides an optional native oracle.

The static evaluator uses only target-sized integer arithmetic to produce IEEE
binary32 and binary64 bits, so it adds no host floating or math-library
dependency. The checked seed carries this evaluator into the production Doom
automap object. Both checked compiler stages build its contracts. GCC or Clang
builds only the optional native copy.

Typed raw inspection leaves the dependency inventory unchanged. CupidDis accepts borrowed ordered code16, code32, and data ranges. Its hosted CLI exposes `--range-at OFFSET:16|32|data` and retains `--mode-at OFFSET:16|32` for code-only changes. The checked CupidDis executable owns normal kernel-symbol inspection, though that ELF input does not need a raw map. Both checked compiler stages rebuild the hosted CLI, and checked CupidC builds the in-kernel adapter. GCC or Clang builds only the optional native copy.

The self-host source frontier also retires no dependency. Hosted CupidC emits deterministic i386 ELF32 objects for all fourteen files in issue #27's CupidC, CupidASM, and CupidDis cohort. Ten cohort files use the hermetic profile. `kernel/lang/as_elf.cc` is the kernel bridge, and the hosted adapters use Cupid-owned i386 Linux declarations for their runtime interfaces. The profile rejects a missing or non-32-bit pointer fact. The gate also covers complete CupidLD and CupidObj command closures. Adapter checks lock the named undefined imports and every text relocation.

The repository i386 Linux runtime replaces the tracer's test-only providers for complete tool closures. CupidC compiles allocation, file, memory, string, `errno`, working-directory, and diagnostic services. CupidASM supplies startup and system-call wrappers, and CupidLD produces static CupidC, CupidASM, CupidDis, CupidLD, and CupidObj commands. Linux and WSL behavior matches the native sibling commands for real outputs and failure paths.

The five static commands share one complete checked-seed gate. The manifest
binds the exact executables, source revision, target ABI, producer lineage,
19-source build plan, startup, and five link orders. The current seed contains
the stage-three images from revision
`95f5bb6cfd0468bb8852c670ada849cb5bde79a7`. CupidC is 2,666,240 bytes
with SHA-256
`ab83e817e49f6f51a31fb41955d33ca6faa4d2073c975ba3a87999c44eeca7cb`
and carries canonical x87 payloads, runtime and static integer conversion,
static long-double arithmetic, and ordinary `float` and `double` updates.
CupidASM is 449,912 bytes with SHA-256
`0d9647b61bc422e88fbc6f8d846f5041e02deca192efe4cfd62df64910340b26`.
CupidDis is 396,500 bytes with SHA-256
`acb136752d504445ad52abc315532a2427db844bdd5da98e2d2d78380047a73e`.
Both carry the 604-row shared x86 catalogue. CupidDis also carries typed strict
inspection and ADR 0266's immutable first-opcode index. CupidLD is 312,792 bytes with SHA-256
`9561d6f7170472cd6dccd87d4988fdd2b23a138966cbe4940a9ffb062eab481d`
and retains deterministic PE32 imports and the loader probe. CupidObj is 392,688 bytes
with SHA-256
`7137ad601a7c22178112fbf08163b36ff2064807caa99962df97d7ae7ae62f2b`
and carries `profile-manifest` authoring, transactional sequential-JPEG
validation, and pristine disk and ISO fixture construction. The 5,440-byte manifest
has SHA-256
`5b46684d9977287f69a94473acbbf7c5302213ef98f9748482cba768ffca0be8`.
The promotion proof passed in 763.5 seconds. It matches all nineteen C objects,
startup, five tools, and five help, eighteen success, and sixteen failure cases
between stages two and three. Its frozen 43-input
closure has SHA-256
`56e0943f82737a7013994f1a2b78fcbd5b5c762d0f5036aac5a48bfbb3dcbe32`,
and its 17,035-byte report has SHA-256
`810704f6701b4b4627062981e1e969332d4aa5f409d2cdce3d4fcba150518f84`.
An independent poisoned-host reproof passed in 766.9 seconds. All five seed
images match stage two, and stage two matches stage three across the complete
19/1/5 artifact set and 5/18/16 behavior matrix. The Windows loader proof
passes with exit 37. Its 17,032-byte report has SHA-256
`736872f31d853fe5b2b67c25e7ec42a1893655074a1c653112def6d66fdeac87`,
and a separate rehash matches every stage artifact in that report.
ADR 0265 records the promotion.

The harness copies the exact 47-input source closure into a private compiler root. Checked CupidC compiles the stage-two union there, checked CupidASM assembles startup, and checked CupidLD links all five Linux tools. The closure also holds the small Windows probe, native tool runtime and startup, direct Windows runtime contract, and `direct.h`. The stage-two producer trio repeats that work for stage three below the same root. Both the private closure and the live closure are checked before the first stage, after each stage, and after behavior checks. Both stages execute the positive and failure cases for every Linux command, then build matching imported Windows probe, CupidASM, CupidC, CupidDis, CupidObj, and runtime-contract images. On Windows, the harness validates and runs all six PE images before it publishes evidence. Each native tool checks help plus a useful success and failure path. CupidDis also checks exact raw-report parity. The runtime contract checks allocation, named-file output and append behavior, current-directory errors, argument parsing, and useful negative paths. The two stages, behavior evidence, and report are published together only after success. The normal Toolchain target then uses both static Linux stages to build its contract cohort without external code generation. Native contracts and hosted development commands remain explicit host-built oracles; normal OS and Toolchain artifacts do not depend on them.

Two active-source fragments anchor the wide call requirement. `toolchain/tests/cupidc_object_contract.cc::decode_function` passes the signed `long long` branch target to `fprintf`. `toolchain/tests/cupidc_frontend_contract.cc::validate_file_object_finalization_storage_limit` passes three `unsigned long long` byte counts to `fprintf`. The guards cover those call fragments only. The complete `.cc` contract programs now compile in both checked stages, while the focused guards still identify why the capability is required. No active-source guard covers a wide `va_arg` or an unprototyped wide call, so those paths have focused ABI fixture evidence only. The neighboring `variadic-callees`, `old-style-empty-functions`, `wide-returns`, and `floating-transport` modes remain part of the full gate. The `js_push_num` guard covers its declaration and assignment lines only, not the full browser interpreter function.

Cast-to-void support now serves production e1000, desktop, and TCP code. The shared path evaluates the operand once, emits `DISCARD` for a represented integer, object pointer, or function pointer, and leaves a `void` operand off the abstract stack. The complete unchanged `ctool_host_allocate` and `ctool_host_release` helpers guard the focused requirement. A deterministic 52-byte object proves the existing discard and direct-call emission paths. Both checked compiler stages build the focused proof; GCC or Clang remains an optional native oracle.

Automatic aggregate initializer lowering serves the CupidC-built desktop object. CupidC semantically zeros a complete fixed automatic array or structure, then evaluates represented leaves in source order and stores them through direct member and element paths. A supported structure-valued leaf uses the structure copy path. The object emitter preserves EDI and uses `CLD` plus `REP STOSB` for the complete object before explicit stores. Named automatic aggregate declarations still initialize in place; backward-jump reentry with an escaped alias remains open under issue #25. The active `no_name` initializer in `cupidc_pp.cc` and the `{0}` type-node initializer in `cupidc_frontend.cc` retain focused guards. Both checked compiler stages build the contracts; a native host build is optional.

Runtime narrow string lowering serves production e1000 and desktop code. `STRING_LITERAL_ADDRESS` gives normal string expressions local `.rodata` symbols and absolute text relocations. `COPY_STRING` fills named automatic arrays, nested initializer leaves, and block-scope compound literals after their destinations have been zeroed. The unchanged automatic hexadecimal array in `drivers/serial.cc` retains a focused source guard. The normal contract path is Cupid-built; GCC or Clang and the native linker are optional oracles.

Structure values serve the CupidC-built socket and desktop objects. CupidC copies complete supported structures through loads, stores, assignment results, conditional joins, expression initialization, discard, fixed direct and indirect calls, and returns. Instruction-owned frame slots hold snapshots and call results. The i386 call path places structure arguments inline in rounded four-byte spans and uses a hidden return pointer at `EBP + 8`; the callee returns that pointer through EAX and removes its slot with `RET 4`. The checked seed and source head have a 604-form x86 catalogue with 249 mnemonics, 64 registers, and fingerprint `55A8970F`. The catalogue includes canonical `SETP` and `SETNP` byte predicates. Six rows cover signed x87 `FILD` and `FISTP` memory operands at 16, 32, and 64 bits. The four SHRD forms encode canonical SHRD for both widths and count sources. The forward x87 form encodes canonical `FSUB ST(1), ST(0)` as `DC E9`. The four preceding x87 forms are 80-bit `FLD` and `FSTP` memory forms, i686 `FUCOMIP ST0, ST(i)`, and operand-free `FLDZ`. Both source-built contract stages rebuild the 604-form source catalogue. ADR 0203 records the preceding seed, ADR 0207 records the forward-subtraction boundary, ADR 0208 records its promotion, ADR 0226 records SHRD, ADR 0228 records SHRD's first seed carriage, ADR 0252 records the x87 integer forms, ADR 0258 records the preceding checked seed, ADR 0259 records the parity predicates, and ADR 0265 records their current checked-seed carriage. The model covers all sixteen i686 conditional moves in 16-bit and 32-bit widths, the complete 16-bit and 32-bit three-operand immediate `IMUL` family, ordinary `90`, `66 90`, and `0F 1F /0` padding, `RET imm16`, long-double comparison, and x87 zero materialization. A private decoder path accepts only five exact repeated-prefix Clang padding strings and creates no catalogue form. CupidASM accepts canonical and alias conditional-move spellings, chooses `6B /r` only for a signed-byte multiply constant, applies mode-sized defaults to memory NOPs, and rejects invalid operands or prefixes. It cannot request redundant prefixes. CupidDis renders stable canonical names and keeps conservative recovery around malformed bytes. The normal contracts are Cupid-built; GCC or Clang and the native linker provide optional oracle binaries.

The private in-kernel CupidC emitter now sends `continue` in a `do` loop to the condition. The shared hosted path can emit static data and functions with canonical one-byte, two-byte, and four-byte integer values plus 32-bit integer arithmetic, signed and unsigned division and remainder, every integer relation, bitwise AND, OR, and XOR, all four integer unary operators, explicit casts among represented one-byte, two-byte, and four-byte integer types, both shift directions, both short-circuit logical operators, statement-level `if` with optional `else`, pre-test `while`, post-test `do`, `for` with expression or declaration initializers and optional iteration, nearest-loop `break` and `continue`, and multiple returns. It also covers fixed direct and indirect calls with four-byte argument slots and normalized narrow results, represented target-sized scalar locals and target-sized fixed automatic arrays and structures in supported compound statements, including the initializer-list subset, linked file-object loads, direct ordinary record-member loads, four-byte integer bit-field reads, value-preserving plain assignments, compound assignments and prefix or postfix updates for represented non-Boolean byte, word, and doubleword integers, and pointer compound assignments and updates, and discarded nonvoid values in deterministic ELF32 objects. The unchanged `section_map` and `children` arrays and their indexed uses drive automatic object storage. The unchanged `asm_lower`, `x86_class_width`, and `x86_set_memory_width` functions drive signed and unsigned byte and word loads, stores, promotions, conditions, and results. The unchanged `cemit_multiply_overflows`, `cemit_power_of_two`, `cfront_bool_valid`, `asm_branch_fits_i8`, and AES `rotw` helpers drive division, logic, comparisons, shifts, and bitwise OR. The unchanged `size++`, `capacity *= 2u`, and `value /= 10u` statements in `toolchain/ctool.cc` pin four-byte destination-preserving mutation. The complete unchanged `x86_put_u8` body and active decoder byte operations pin narrow mutation. Their 201-byte exact object proof contains four functions, one four-byte BSS object, six symbols, and one `R_386_32` relocation. The separate narrow-mutation object has eight functions in 878 exact text bytes, ten symbols, one byte of BSS, and one absolute relocation. The unchanged CPUID-toggle return statement drives XOR with its mask, comparison, and `bool` conversion. Its surrounding GNU inline assembly and broader statement sequence remain outside this hosted leaf slice. The unchanged memory `align_up` helper drives bitwise complement inside unsigned arithmetic and masking. The complete unchanged `dis_signed_bits` helper drives two comparisons, two conditional branches, three returns, complement, addition, an explicit unsigned-to-signed cast, and negation. Its deterministic object contains one 143-byte local function, 71 decoded instructions, two symbols, no relocations, and branch targets at byte offsets 53 and 111. The complete unchanged `syscall_sleep_ms` helper drives a pre-test loop. Its deterministic object contains one 94-byte local function, 43 decoded instructions, branch targets at byte offsets 92 and 20, and three direct-call relocations at offsets 11, 24, and 80. The unchanged Doom wipe tick loop drives a post-test loop. Its deterministic object contains one 125-byte local function, 59 decoded instructions, branch targets at byte offsets 123 and 6, and two direct-call relocations at offsets 14 and 78. The guarded `url_hash_hex` loop drives a `for` path, while unchanged statements in `cir_validate_initializer_ownership` drive loop control. Their combined deterministic object contains the 107-byte browser function and eight loop-control functions totaling 319 bytes. It has 426 text bytes, ten symbols including the null symbol, exact decoded branch targets, and no relocations. The active `cc_skip_brace_initializer` fragment drives logical not without claiming its complete function. The VGA setter drives a linked store, the timer getter drives an ordinary member at byte offset 8, and the Doom color source drives an eight-bit field at bit offset 16. Bit-field emission also covers signed extraction, a nonzero storage offset, and a full-width field. Local and unresolved external calls use `.rel.text` `R_386_PC32` relocations with addend `-4`; direct object addresses use `R_386_32` with addend zero. Member selection and field extraction do not change the base symbol or relocation addend. At that checkpoint, this work retired no host dependency, and GCC or Clang plus the native linker built the shared modules and contracts. Both checked compiler stages now build the complete contract cohort; native copies are optional oracles. All nine hosted Toolchain source gates parse completely, including `cupidc_ir.cc`, `cupidc_emit.cc`, and `cupidc_frontend.cc`.

The narrow-mutation proof uses the shared decoder as a small test-only i386 execution oracle. Twelve zero and wrap-boundary cases check EAX, the stored byte or word, and poisoned padding in the four-byte argument slot. This adds no emulator or host execution dependency.

The nine-file source count and host-ownership sentence in the preceding
historical summary are superseded. The hermetic frontend gate contains twelve
Toolchain implementation files. The deterministic object gate adds
`kernel/lang/as_elf.cc` and the three hosted adapters, for sixteen sources in
all. Both checked compiler stages build the normal contracts; native copies
are optional oracles. ADR 0081 records the source expansion, and ADR 0196
records the ownership transfer.

| Dependency | Current role | Current requirement | Fixed-point disposition |
| --- | --- | --- | --- |
| GCC with i386/multilib support | Builds optional native Toolchain contracts and commands on Linux | Not required by root `all`, `user:all`, or `toolchain:all`; required only for explicit native oracle and development targets | Retain only as an optional oracle or bootstrap escape hatch |
| Clang with i386 target support | Builds optional native Toolchain contracts and commands on Windows, including the native CupidC and CupidLD user oracle | Not required by root `all`, `user:all`, or `toolchain:all`; required only for explicit native comparison and development targets | Retain only as an optional oracle or bootstrap escape hatch |
| NASM | Optional comparison oracle for the four boot and kernel CupidASM parity tests and the shared ELF32 reader | Not required by root `all`, `user:all`, `toolchain:all`, or baseline preflight; `make nasm-assembly-oracle` uses it when installed. The ISO lane fixture is excluded because NASM freezes `$` across its `TIMES` statement | Retain only as an optional oracle/bootstrap escape hatch |
| Host linker backend (`ld`, `ld.lld`, `lld-link`, or platform equivalent) | No direct i386 OS, user, or normal Toolchain link recipe remains. CupidLD owns those outputs. Checked-seed CupidLD builds deterministic imports and links the small loader probe plus native CupidASM, CupidC, CupidDis, and CupidObj images that Windows runs directly. A host compiler still invokes a native linker for optional oracle and development commands, while standalone ELF linkers remain comparison tools. Canonical Windows LLD links use `/Brepro` so hosted PE timestamps cannot invalidate same-host evidence | Not required by root `all`, `user:all`, `toolchain:all`, or the checked-seed Windows execution proofs; required only by optional native targets | Retain only as an optional oracle or escape hatch while CupidLD's Windows publisher and checked native seed remain open |
| GNU `objcopy` / `llvm-objcopy` | No role in the normal build; tracked legacy/oracle helpers may still invoke it manually, and the checked `6731dd6` evidence fingerprints the then-installed oracle | Not required for root `all`, `user:all`, `toolchain:all`, or new `bootstrap-baseline` captures | Retain only as an optional comparison/maintenance utility; CupidObj owns the production transformations |
| GNU `nm` / `llvm-nm` | Optional comparison oracle for CupidDis's numeric symbol view and historical baseline evidence | Not required by root `all`, `user:all`, `toolchain:all`, or baseline preflight; configured through `NM` only for optional oracle probes/tests | Retain only as an optional comparison/maintenance utility; CupidDis owns production kernel-symbol inspection |
| Hosted C runtime/libc | Backs only the explicit native oracle and development adapters. The normal five-tool build and sixteen-executable contract cohort use Cupid's checked i386 Linux declarations and repository runtime. Native CupidASM, CupidC, CupidDis, and CupidObj use the repository Windows runtime and twelve CupidASM API bridges | Not required by root `all`, `user:all`, `toolchain:all`, or the checked Windows commands; required only by native oracle and development targets | Retain only for optional native oracle and development seams; it must not own normal preprocessing, parsing, type/layout semantics, code generation, object, assembly, link, or inspection behavior |
| GNU Make | Declares the root, user, and toolchain-contract build graphs and invokes tools | Required; the graph uses portable ordinary/stamp targets rather than GNU Make 4.3 grouped-target syntax | May remain as host orchestration; it must invoke Cupid code-producing tools on the normal path |
| Python 3 | Launches the checked seed for all 440 Cupid-owned root artifact transforms plus six external-program compile and link operations; runs the Python-only size verifier; runs the Cupid-built syscall ABI contract and compares its report with an independent oracle; coordinates and verifies kernel-symbol generation; parity-checks accepted JPEG, ISO, and profile-manifest bytes; builds the independent disk-template, ISO, and profile oracles; preserves existing FAT contents and stages files; validates, locks, and atomically publishes outputs; builds fixtures; and drives QEMU tests | Required | May remain for tests and packaging, but removing it from the staged fixed point and checked-tool launch path is the open Python-free bootstrap gate |
| WSL on Windows | Runs the checked static i386 Linux seed for 440 root CupidC, CupidASM, CupidObj, CupidLD, and CupidDis artifact transforms, six external-program compile and link operations, the Cupid-built syscall ABI contract, and the staged Toolchain bootstrap | Required for those paths on Windows; native Linux runs the seed directly | Remove it when a checked native Cupid toolchain or an equivalent Cupid-owned execution path is available |
| Git | Enumerates the tracked audit universe and creates detached baseline worktrees | Required for development/audit workflows, not image production | Retain as source-control orchestration, never as a code-producing dependency |
| `link.ld` and its documented GNU-script subset | Defines kernel memory and section layout; CupidLD parses the exercised `ENTRY`, `SECTIONS`, location-counter, wildcard, alignment, symbol, `COMMON`, and `ASSERT` forms | Required input to both kernel link passes; host-linker interpretation is oracle-only | Keep the script as the source-owned layout contract and deepen CupidLD when the active script needs more semantics |
| `jpegtran`, `djpeg`/`cjpeg`, or FFmpeg | No role in the normal root build. Checked CupidObj validates and wraps the repository's sequential SOF0 or SOF1 JPEG; Python checks accepted bytes independently | Not required by root `all`; progressive, unsupported, and malformed input fails instead of selecting a host converter | Retain only for optional asset maintenance outside the build graph |
| `mkisofs`, `genisoimage`, or `xorrisofs` | No role in the build. A checked manifest fixes the fixture tree, checked CupidObj emits the tracked ECMA-119 and `RRIP_1991A` image, and Python verifies it independently | Not required by root `all`, `sync-iso`, fixture regeneration, or tests | Retain only as an optional interoperability oracle outside the build graph |
| Bash, curl, OpenSSL, xxd, and Unix text tools | Manual CA-bundle refresh and legacy/oracle helper scripts | Not required by root `all`; required for those maintenance paths | Keep only documented maintenance dependencies; Python/Cupid paths own normal-build behavior |
| QEMU `qemu-system-i386` | Boots emulator smoke and integration tests | Required for automated emulator verification, not image production | Retain as a test dependency; real-hardware tests remain complementary |
| Host shell/platform utilities | Launch Make, Python, and tests | Required operational environment, but no reachable transform is owned by an ad-hoc shell recipe | Keep only non-code-producing orchestration requirements |

The hosted pointer slice now serves the transferred e1000, desktop, socket, and TCP objects. Four-byte object pointers cross supported cdecl parameters and results, automatic and linked storage, direct calls, loads, stores, assignment, initialization, qualification, both directions between object pointers and `void *`, null conversion, dereference, address-of, and indirect ordinary members. Structural compatibility admits distinct pointer-to-array graph nodes, removes top-level pointer-object qualifiers during value conversion, and carries array qualification to the element comparison. The unchanged `obj_region_less` helper publishes 50 exact IR instructions. The unchanged `ctool_job_arena` helper reaches pointer inequality, typed null casts, pointer truth testing, pointer-valued conditional selection, and indirect member loading. Complete-object pointer arithmetic covers scaled offsets, compatible pointer difference, normalized subscripts, linked array decay, and pointer mutation. The two unchanged ATA transfer loops pin `buf += 256`, and exact fixtures reproduce their two-byte stride and constant offset. The focused object has nineteen functions in 811 exact text bytes, twenty-one symbols, one sixteen-byte BSS array, and two absolute relocations. Function pointers retain their signatures across the same four-byte scalar paths, including fixed indirect calls. Their object proof has thirteen functions in 513 text bytes, seventeen symbols, nine text relocations, one data relocation, four register-indirect calls, and one direct call. Checked-seed CupidC uses these paths for the four production objects and both checked stages build the focused proofs; GCC or Clang plus the native linker provide only optional oracle copies.

The function-pointer type relation is an arena-backed, memoized worklist instead of a recursive walk. The contract covers repeated callback children, old-style promotions, ignored top-level parameter `const`, `volatile`, and `restrict`, significant `_Atomic` and referent qualifiers, missing parameter storage, and checked scratch rollback. A second object adds a 28-byte local-function address proof with one `R_386_32` relocation to a defined static symbol. Both checked compiler stages build these tests; the host-built copies are optional oracles. Explicit function pointer casts that produce values are still rejected. A cast to `void` only discards the represented value, so it adds no hidden target conversion policy.

Hosted narrow integer values and mutation serve the transferred production objects. One-byte and two-byte loads sign-extend or zero-extend into canonical 32-bit values, compound assignments and updates compute through 32-bit promotion, stores use the declared byte or word width, and `_Bool` conversion tests the full source word. Fixed cdecl calls keep four-byte argument slots and normalize narrow results in both caller and callee paths. The value object proof covers 30 functions, 31 decoded returns, four direct calls, three register-indirect calls, signed and unsigned byte and word loads, exact-width stores, and a two-byte BSS object aligned to two bytes. The mutation proof adds eight functions, 878 exact text bytes, fourteen byte stores, four word stores, and one volatile byte load. Both checked compiler stages build these focused contracts, while checked-seed CupidC owns the production emission. Host-built copies are optional oracles.

Hosted sixteen-byte call alignment serves calls in the transferred production objects. A target-private pass derives the live Linear IR stack depth along reachable control flow. The i386 emitter combines that depth with the fixed frame and outgoing ABI storage, then reserves zero, four, eight, or twelve bytes. A control-flow decoder checks ESP at every reachable direct or indirect call across conditional joins and loop back edges. A symbolic oracle checks three argument values after a twelve-byte padding move. Both checked compiler stages build the focused emitter contract, while checked-seed CupidC emits the production call sites. GCC or Clang builds only the optional native copy.

Hosted scalar variadic callees follow that same boundary. GNU C mode exposes `__builtin_va_list` as a target `char *` cursor, and the frontend, IR, and emitter carry start, argument, copy, and end through represented non-atomic pointers, integers, `double`, and `long double`. Four-byte reads advance the cursor by four bytes. Wide integers and `double` copy eight bytes into one private snapshot and advance by eight. `va_arg(long double)` copies twelve bytes and advances to the following four-byte slot. The static i386 runtime checks that following slot as well as the copied long-double value. The unchanged Doom compatibility header parses under its generated profile. Decoder-driven i386 contracts check copied and original cursors plus fixed, wide, floating, successive, and direct or indirect call positions. Both checked compiler stages build the changed modules and contracts. GCC or Clang builds only optional native copies.

Hosted empty identifier-list definitions and unprototyped calls keep the same ownership boundary. The frontend preserves a non-prototype function type and applies default promotions to every call argument. Linear IR carries the actual count and one packed post-conversion type for each argument. The emitter uses those types for cdecl layout, alignment, and cleanup. Signed and unsigned wide integers, existing `double` values, and source `float` values promoted to `double` occupy two adjacent stack words in direct and indirect calls. An existing `long double` occupies three words. The static i386 runtime executes both the direct and indirect long-double paths. Both checked compiler stages build the shared compiler and focused contracts. GCC or Clang builds only optional native copies.

Block-scope `struct` and `union` tags serve the production desktop object. The frontend owns their lexical identity and completion, including record tags declared in a function definition's parameter list. An empty tag declaration with a represented storage class or type qualifier adds no runtime IR when it introduces a tag; repeating a visible tag without a declarator is rejected. A `for` initializer can use a visible tag or anonymous record for its object but cannot introduce a named tag. Deterministic object evidence covers Doom's anonymous block-static record, its exact literal bytes, the text reference to `packs`, and all three string relocations. The exact Doom profile passes this declaration and parses the complete `d_main.cc` file after the linked-object work in ADR 0058. Both checked compiler stages build the focused proof; checked-seed CupidC owns the desktop use.

Block-scope external objects are part of the Cupid-built contract cohort. The frontend keeps lexical aliases separate from canonical linked entities, and Linear IR lowers each use through `FILE_ADDRESS` without reserving an automatic slot. The exact ELF32 proof has 15 text bytes, three symbols, and one `R_386_32` relocation to one undefined object. Both checked compiler stages build the compiler and contract. GCC or Clang builds only the optional native copy.

Block typedefs are also part of the Cupid-built contract cohort. The frontend keeps each alias in the ordinary lexical namespace with a stable type, while Linear IR validates the declaration without emitting work. The ELF32 proof matches the same function with the underlying type spelled directly, byte for byte. Both checked compiler stages build the compiler and contracts. GCC or Clang builds only the optional native copies.

Block function declarations are part of the Cupid-built contract cohort. The frontend gives each lexical name its visible type and one canonical linked function. Linear IR validates both function types without allocating storage or emitting an instruction for the declaration. The ELF32 proof is byte-identical to equivalent file-scope declarations and contains one undefined function, two `R_386_PC32` call relocations, and one `R_386_32` address relocation. Both checked compiler stages build the compiler and contracts. GCC or Clang builds only the optional native copies.

Block enums now serve the production desktop object. The frontend keeps their lexical tags, ordinary enumerator names, target values, and source activation points across declarations, record members, function-definition parameter lists, and block type names. Linear IR turns represented uses into integer constants without allocating storage, and the ELF32 proof matches direct folded constants byte for byte with no enum symbol or relocation. The cursor and REPL enums remain in their active source files. Checked-seed CupidC builds the focused proof; GCC or Clang can still build the optional native oracle.

Hosted block-static emission now serves the production desktop object. The shared frontend retains constant roots and absolute block-binding identities, the lowerer emits no declaration-time stores, and the object emitter assigns local symbols in `.rodata`, `.data`, or `.bss`. Runtime addresses use `R_386_32`, and block statics never receive automatic frame slots. The exact object proof covers eleven static objects and sixteen relocations, including shadowed, unused, and unreachable declarations. Checked-seed CupidC builds the focused proof; GCC or Clang and the native linker remain optional oracles.

Hosted block-scope compound literals also change capability without moving a production object. The shared frontend owns the initializer forest, and Linear IR retains one unnamed-object identity per source site. The i386 emitter assigns a persistent automatic frame slot to that identity. Aggregate lists use a separate staging slot and one complete-object copy so initializer reads finish before the persistent object changes. Initialization runs at every evaluation before the expression returns the object's address. Checked-seed CupidC now builds the contract; GCC or Clang and the native linker remain optional oracles. No current production object exercises this compound-literal path.

## Resolved output ownership

Counts are output transforms in the checked audit, not textual recipe occurrences. Composite Python transforms list the code-producing utility they invoke as a second owner.

| Tool hand-off | Reachable outputs | Required external behavior |
| --- | ---: | --- |
| Host C compiler | 0 | Native hosted tools and contracts are explicit optional oracles outside every supported root |
| CupidC | 245 owned or participating transforms | The 238-source checked-in normal cohort, generated kernel symbols, three generated installation tables, three example external programs, and the checked Toolchain contract cohort; every published object is validated |
| Cupid-built ABI contract | 1 participating transform | The staged static i386 checker owns the reviewed syscall-table, scalar, constant, record-layout, provider, snapshot, and reread rules; Python independently checks its report and controls publication |
| CupidASM | 5 owned transforms | Three production flat binaries and two production ELF32 `ET_REL` objects. The two boot and kernel flat outputs are byte-identical to the optional NASM oracle; the checked ISO lane is the documented NASM `TIMES` exception. The objects match the oracle's code, symbol, alignment, and relocation semantics |
| NASM | 0 production transforms | Optional active-source and ELF32 interoperability oracle only |
| CupidLD | 5 owned transforms | Two script-driven kernel links plus three fixed-address user executables; owns `R_386_32`/`R_386_PC32`, weak/strong/common/script symbols, absolute COMMON alignment, relocation-aware merge entries, assertions, static ELF32 serialization, explicit unsupported allocated-section diagnostics, and the used `link.ld` subset |
| CupidObj | 191 owned transforms | 173 canonical text-to-ELF wrappers, eight byte-exact binary-to-ELF wrappers, one checked `wrap-jpeg` transform with Python parity and publication checks, final initialized ELF-to-raw conversion, three installation-source generators, one kernel-symbol source generator, one production disk-image template, one production ISO fixture, and one guarded Doom profile manifest. |
| Checked-seed CupidObj disk path | Included in the 191 CupidObj transforms | `disk-template` authors the MBR, boot reserve, kernel lane, FAT16 metadata, pristine FATs, and empty root directory before Python performs mutable image work. |
| Checked-seed CupidObj ISO path | Included in the 191 CupidObj transforms | `iso-fixture` authors the complete deterministic ECMA-119 and Rock Ridge image before Python compares an independent render and publishes under a per-output lock. |
| Checked-seed CupidObj profile path | Included in the 191 CupidObj transforms | `profile-manifest` authors the canonical Doom profile JSON from a frozen `CUPROF1` snapshot before Python checks an independent oracle and publishes under an adjacent no-follow lock. |
| CupidDis | 2 participating transforms | Supplies 4,718 deterministic text-symbol rows for the 114,851-byte panic-backtrace blob, then validates the complete 429-input code cohort on the existing `kernel.bin` transform; the host oracle remains optional |
| Python | 450 transforms | Launches checked Cupid tools and retains host discovery, safety, parity, drift detection, locking, publication, and mutable image work. The Python-only root size verifier emits no OS artifact. Two supplemental verification or orchestration outputs remain Python-only. The user ABI gate combines a Cupid-built contract with an independent Python oracle. The disk image, ISO image, and Doom profile manifest are composite transforms because checked CupidObj authors their deterministic bytes first. |
| Make recursion | 0 transforms | Native hosted CupidASM, CupidObj, CupidLD, and CupidDis targets remain available, but no supported root reaches them recursively |

Checked-seed CupidDis can validate several inputs with
`--require-known FILE [FILE...]`. The command accepts only code streams whose
typed unknown, invalid, and truncated counts are zero. It excludes declared
raw data and non-executable ELF regions, continues after an input failure, and
does not publish rendered text. This adds no root output. It makes CupidDis a
participant in the existing `kernel.bin` transform, bringing its audited
participation to two transforms. The normal kernel path validates all 427
audited root object outputs plus the pass-one and final kernel ELFs in the same
transaction that performs flat extraction. The 9,028-byte LF-only input
manifest lists those 429 unique paths in graph order with SHA-256
`48bdef348f6575881b9808631173e7265abc9ea89dfb84d48de72b3d2304749e`.
Make keeps all 429 paths as direct prerequisites. The first separate gate
froze and rehashed the seed manifest, input manifest, and selected inputs. It
passed in 185.526 seconds with empty streams and exit 0. The current hostbuild
transaction freezes the selected seed manifest and all five artifacts, the
429-entry input manifest and cohort, and the existing `kernel.bin` boundary.
Checked CupidDis validates that private cohort, then checked CupidObj flattens
the frozen final ELF into a private candidate. Hostbuild rechecks live trust
inputs and the output before parent-relative atomic publication. Every failure
preserves the prior raw kernel. The transaction passed with exit 0 in 187.054
seconds and published an 8,946,332-byte `kernel.bin` with SHA-256
`4f5f2591d01bcc4007773844e9bfb8112a16dd17fbd178014cc2056fefaab67d`.
The focused hostbuild suites each passed 31 tests on Windows and in WSL;
platform-specific cases were skipped on the opposite host. Moving private
flatten extraction onto the shared pinned-path helper remains deferred
maintenance. Python remains
the launcher and drift guard, so this adoption removes no orchestration
dependency. ADR 0266's indexed decoder makes the checked 128 KiB throughput
contract pass within 30 seconds without changing instruction selection or
recovery. ADR 0265 records production adoption.

The final audit records 450 transforms across the three supported roots and
441 under root `all`. Its tool participation totals are Python 450, CupidC
245, CupidObj 191, CupidASM five, CupidLD five, and CupidDis two. It retains
the 5/18/16 fixed-point matrix and records strict validation plus flat
extraction together on `kernel.bin`, with all 429 code inputs represented.
`make bootstrap-audit` passed in 64.780 seconds.

The poisoned-host normal `make -j2` passed in 1,057.969 seconds with `CC`,
`CXX`, `CPP`, `HOSTCC`, `HOSTCXX`, `ASM`, `AS`, `LD`, `AR`, `NM`, and
`OBJCOPY` pointed at invalid commands. That historical build ran the separate
strict gate before CupidObj flattened the kernel. It produced `boot.bin`, the pass-one ELF, final
ELF, raw kernel, and disk image with SHA-256 values
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`,
`b21fa8954499a7857ee4b12fa3950fcc08ff3c6a6234c8ae72effc38c51fdc6d`,
`a0b57cd886369762b65d657bb3f2915ada8f30b52102535add89466eaf4f5976`,
`4f5f2591d01bcc4007773844e9bfb8112a16dd17fbd178014cc2056fefaab67d`, and
`4548005bd0aa1a3cffb74620c2309d53c6b291ea2505ed187034bf6b13f1bb37`.
Definitive four-vCPU E1000 and RTL8139 boot frontiers passed from that image
with exits 0 in 794.034 and 758.667 seconds. Both passed SMP, frontier,
framebuffer, AC97, and PC speaker checks. The private-image runs left the
source image unchanged.

The frozen-document poisoned-host rebuild passed in 1,018.548 seconds. Its
current outputs supersede the pre-freeze identities above. The 2,560-byte
`boot.bin` has SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`.
The 9,044,032-byte pass-one ELF has SHA-256
`659bd6485deb4e6a18a1efa0f575eb90f210fe5674e9e1257eeef2a4422ff21e`,
the 9,166,912-byte final ELF has SHA-256
`7caf5ad4bc721f10418c06be7cfd8d9568efc8378e7baf2c2f7a510ec49263a3`,
and the 8,950,860-byte raw kernel has SHA-256
`5f0c0becc1ba66a9d3e2eda15555fec39faedc98e2349ad3ee7b2d08775fe1a7`.
A final poisoned-host `make -j2 all` passed with exit 0 in 1,022.190 seconds.
The exact-size prerequisite accepted all nine artifacts before publishing the
209,715,200-byte image with SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.
The completed boot frontiers above remain pre-freeze runtime evidence. The
final four-vCPU E1000 and RTL8139 frontiers passed from the current image with
the partitioned USB fixture, `--smp 4`, `--cpu max`, SMP and frontier runtime
verification, a private image, and a 300-second phase timeout. E1000 exited 0
in 725.058 seconds with 103,673 changed framebuffer pixels, 29,608,822 AC97
frames at peak 25,600, and 76,784 PC speaker frames at peak 30,710. RTL8139
exited 0 in 725.406 seconds with 106,151 changed pixels, 29,601,879 AC97 frames
at peak 25,600, and 76,719 PC speaker frames at peak 31,501. Both used a 640
by 480 framebuffer, and the image hash remained unchanged.

The normal Make image recipe passes the checked seed manifest to
`tools/hostbuild.py image`. Hostbuild freezes every input and snapshots the
live output, then runs checked CupidObj before it builds an independent Python
template and compares every byte. Python preserves an existing valid FAT
filesystem or starts from the complete template, stages the frozen files,
extends the candidate, and rechecks the seed, inputs, and live output. A
cross-process lock guards the final atomic replacement. ADR 0238 records this
ownership boundary.

`tools/hostbuild.py::_symbols_from_nm` remains the drop-in native-reader seam
for oracle use. The normal Make path calls `_symbol_text_from_seed`, which
freezes the pass-one kernel, runs manifest-checked CupidDis, and preserves its
exact text for checked CupidObj. `embed_jpeg` freezes the JPEG, calls checked
CupidObj `wrap-jpeg` with the original source identity, checks the accepted
bytes through Python, and publishes only after live inputs pass their drift
check. `tools/mksyms.sh` and `tools/embed_jpeg_baseline.sh` are tracked
legacy/oracle duplicates outside the normal Make path.

### Historical native-contract boundary

The paragraph below records the earlier native-only contract boundary. ADR
0196 supersedes its build ownership: the normal contract suite is now
compiled and linked by CupidC and CupidLD, while the host builds it only when
`native-oracles` is requested.

The hosted contract suites use the host C compiler and its native linker backend to bootstrap and exercise the shared core, CupidC preprocessing/declaration/type-layout/IR/object operations, ELF32, x86, CupidDis, CupidASM, CupidObj, CupidLD, and the kernel's buffer-only fixed-image-to-`ET_EXEC` bridge. The ELF32, CupidASM, and CupidLD suites may also use NASM, GNU `readelf`, and standalone GNU/LLVM ELF linkers as optional comparison oracles. They prove that Cupid-written objects and executables are accepted by external consumers, that the Cupid reader accepts Clang-, NASM-, and linker-produced objects, that every active assembly source reaches the required raw, relocatable, or fixed artifact, and that all shared operations fail transactionally; absent oracle tools are skipped. Assembly, inspection, object-transformation, and link semantics plus production assembly/link/object ownership have transferred. The shared hosted CupidC path handles static data and functions, including direct and fixed indirect calls with one-byte, two-byte, or four-byte integer parameters and results, same-kind `float` and `double` parameters and results, plus supported structure parameters and results. It supports target-width integer and four-byte pointer locals, target-sized fixed automatic arrays and structures, including the supported initializer-list subset, linked target-width integer and four-byte pointer file objects, ordinary members, four-byte bit-field reads, plain scalar and structure assignments, compound assignments and prefix or postfix updates for represented non-Boolean byte, word, and doubleword integers, and pointer compound assignments and updates, 32-bit division and remainder, all integer relations and unary operations, bitwise operations, shifts, short-circuit logic, structured selection and loops, 32-bit and wide integer switch dispatch, nearest-target control, direct labels, and `goto`. Narrow loads sign-extend or zero-extend into canonical 32-bit words, mutation computes through 32-bit promotion, exact-width stores use a byte or word lane, and fixed scalar cdecl calls retain four-byte argument slots. Structure calls copy completed arguments into rounded inline spans and use a hidden result pointer when needed. Every supported direct or indirect call aligns ESP to sixteen bytes immediately before `CALL`; target-private depth analysis accounts for the frame, live semantic values, and outgoing storage. Explicit casts to `void` evaluate represented integer, pointer, floating, supported structure, or `void` operands and produce no value. The transferred e1000, desktop, socket, and TCP objects now exercise these paths in the normal build.

The shared preprocessor owns translation phases, macros, conditionals, reproducible predefined macros, dual-location `#line`, includes, once identity, pack metadata, and policy-neutral Cupid `#exe` markers. The declaration operation consumes that tape and publishes the shared type graph, completed layouts, canonical declarations, file object definitions, function-scoped labels, semantic initializer records, and immutable function-body AST. A file definition keeps definition-local type, storage, kind, location, and initializer ownership separate from the canonical first declaration. Explicit and tentative definitions use the same static forest as block-static objects. Repeated tentative declarations coalesce during parsing, then translation-unit finalization applies the merged type and supplies a zero root. Static addresses can name a linked file object or function after address-of, array decay, function decay, or pointer arithmetic with a represented integer constant expression. These remain semantic references whose checked signed target-byte addends are independent of host pointers. Automatic forests retain runtime expressions, while static forests retain zeros, target integers, strings, binding addresses, and direct array or structure lists. Duration-aware freeze validation keeps those storage domains separate and checks every owner, reference, payload, direct selector, and postorder edge. The object operation assigns the static forests to `.rodata`, `.data`, or `.bss`, writes target bytes and symbols, and turns represented addresses and addends into direct-symbol `R_386_32` relocations. The body subset continues to cover scalar and aggregate return, structured control flow, canonical labels and direct `goto`, integer and pointer expressions, conditional values, assignment and updates, calls, casts, and expression designators.

The native and Cupid-built compiler drivers now pass repeatable `-include`
inputs into that preprocessing operation in caller order. This lets the
checked seed reproduce the Doom-tree profile. CupidC retains the sound
driver's empty volatile memory barrier. Its integer-only IEEE evaluator also
folds the unchanged static fixed-point table in `am_map.cc`. The explicit
`--doom-compat` profile also represents the five calls in `i_system.cc` that
appear before a declaration. Strict C and plain GNU mode still reject those
calls. The same profile carries the eleven audited conversions
between unqualified function pointers and unqualified four-byte data or
`void` pointers in `m_menu.cc`, `p_saveg.cc`, `p_ceilng.cc`, and `p_plats.cc`.
Strict C and plain GNU mode still reject the implicit conversions, and their
explicit function/data casts remain outside Linear IR. One-active-member union
initialization also compiles unchanged `info.cc`, and ordinary narrow bit-field
promotion compiles unchanged `i_video.cc`. All 83 normal recipes now use the
checked production wrapper. The exact input manifest and prepublication drift
checks make that ownership fail closed.

Eight-byte integer and exact floating object access use those existing storage identities. A wide `LOAD` copies eight bytes into its own frame snapshot, and `STORE` or `STORE_VALUE` copies from that snapshot to a selected object. This applies to file objects, block statics, fixed automatics, pointer dereferences, ordinary members, and indexed elements. A `float` load keeps its raw four bytes. A `double` load receives its own frame snapshot, and both types pass through compatible stores, fixed calls, discard, and returns. Same-kind floating arithmetic stores each changed result before the next IR instruction. Values already typed as `double` also pass through ellipsis and unprototyped calls, and `va_arg(double)` advances by eight bytes. The normal contract cohort proves these operations with both checked compiler stages; a host compiler builds only the optional native copies.

File definitions and block-static bindings now share one object encoder. It places file objects first, then every block static in absolute binding order, before it emits functions. The same initializer forms, section rules, target bytes, symbol construction, and direct-symbol relocations apply to both storage domains. A block-static initializer can now retain another block-static object's symbol and emit an `R_386_32` relocation to it.

The unchanged FAT16 and active-header contracts still pin layout,
redeclaration, attributes, assertions, and lexical ownership. The checked-seed
C11 standalone sweep passes 161 of 163 active non-Doom headers;
`scheduler.h` and `simd_intrin.h` retain exact C11-profile failures. The
checked seed maps Cupid's sized scalar, Boolean, and vector spellings into the
shared type graph and parses all 29 declarations in unchanged
`simd_intrin.h` under the Cupid profile. `cpu.h` passes through the represented
RDTSC form, the three roots that include `percpu.h` parse through all active
integer atomics, and `ports.h` parses through all eight width-aware helpers.
All nineteen Toolchain source gates parse completely. Each five-number tuple
reports definitions, statements, expressions, block bindings, and
initializers. `cupidc_pp.cc` publishes 143/3,932/25,287/479/286;
`cupidc_ir.cc` publishes 269/7,496/69,333/989/362;
`cupidc_emit.cc` publishes 366/9,234/77,133/1,122/748; and
`cupidc_frontend.cc` publishes 445/17,242/113,778/2,565/1,547. The generated
audit records the current active-source totals and source graph.

Checked stage-two and stage-three CupidC build the shared frontend, emitter,
and normal contract programs. GCC or Clang and a host linker build only the
explicit native oracles and development commands. Open work
includes chained and overriding designators, promoted anonymous-member
designators, repeated union-member overrides, Cupid class lists, broader
runtime values, pointer and eight-byte atomics, computed `goto`, GNU label
addresses, the remaining GNU surface, hexadecimal floating literals, the
remaining `long double` forms, and broader self-hosting. The private kernel
compiler continues to own embedded runtime JIT and AOT compilation.

Checked-seed CupidC accepts exact `fldcw %0` with one addressable, non-atomic
16-bit integer `m` input. GNU semantics make the no-output statement volatile
even without that keyword. Frontend, Linear IR, and object contracts
share this state-memory input seam with `ldmxcsr %0`. ADR 0258 records seed
carriage. This narrows the language gap without removing a host dependency.

Checked-seed `noinline` and `target("general-regs-only")` semantics narrow
that GNU gap without changing the dependency count. The seed also accepts
the exact LDMXCSR memory input at line 28, all three MOVSS
float-memory forms in `fpu_boot_smoke()`, and the exact balanced x87
`fldl`, `fsin`, and `fstpl` block in `stress_sin()`. Two complete
builds of `kernel/cpu/fpu.cc` produce the same validated 6,620-byte object
with SHA-256
`14c3ea232b7d4455ceabd561c69293cc5849abae24d9f210aa69d64ed8c8a5cb`.
The production object contract decodes `fpu_init_cpu()` with Cupid's ELF and
x86 readers. It rejects helper calls and floating work before the CR4 write,
requires one `FNINIT` followed by one 32-bit memory `LDMXCSR`, and rejects
other floating work in that function. A negative fixture replaces the CR4
write with NOPs and must fail before `FNINIT`.

The checked seed also accepts the complete unchanged x87 control-word block in
`str_floor()`, including its exact AX and memory clobbers. The emitter reuses
the consumed input-address slot for the two stack scratch words, restores the
incoming x87 control word, and leaves the pending output address intact. Two
compiles of the extracted active helper produce the same 420-byte object with
SHA-256
`448012fe57ec625c6075e97cf91163b994a0443238c5d6bdf25e4b839763f14e`.
The checked seed emits the later explicit double-to-`uint64_t` casts. Two
complete compiles of unchanged `kernel/core/string.cc` produce the same
14,460-byte object with SHA-256
`d48bb6ea18b7124fbefeaca0d5d5ee8a517db950f21ea88e30ededd6c5c2a577`.

`kernel/cpu/fpu.cc` and `kernel/core/string.cc` have transferred to checked
CupidC. The string recipe freezes the source and its two headers before
validated publication. ADRs 0141, 0146,
0148, 0150, and 0154 record the assembly boundaries. ADR 0170 records the
conversion.

Compiler head now emits the exact operand-free BSS-clear statement at the
start of the external `.text.start` `_start()` body. The statement installs
the fixed stack, loads `_bss_start` and `_kernel_end` through two `R_386_32`
relocations, derives the doubleword count, and clears the range with CLD and
REP STOSD. The following `kmain()` call uses the reset stack residue, and a
return reaches a halt loop rather than the discarded frame. Frontend depth
tracking rejects leading, label-wrapped, and otherwise nested copies before
IR independently checks the outer body relationship.

Two Cupid-built compiler runs emit unchanged `kernel/core/kernel.cc` as the
same 25,920-byte object with SHA-256
`ed42676ad0d7f16b1fb83442ead1b0082781324dca719104922099cee34b5ab0`.
The normal recipe freezes the source and its 63-header recursive closure.
Poisoning `CC` leaves it on the checked wrapper, and CupidDis decodes the
`0x01100000` stack reset, BSS clear, `kmain()` call, and halt loop. ADR 0175
records the boundary, ADR 0179 records seed carriage, ADR 0180 records
production ownership, and ADR 0187 records the active memory-map placement.

The checked seed accepts the exact volatile EFLAGS restore used twice by
`simd_cpu_has_cpuid()`: one 32-bit `r` input, no outputs, and one `cc`
clobber. The shared x86 path emits `POP EAX`, `PUSH EAX`, and `POPF` without
a temporary or relocation. It also accepts the valid fixed EAX
overlap in the following CPUID statement. Its `a` input keeps the original
constraint and names the compatible `=a` output; the emitter consumes the
leaf through EAX immediately before CPUID. Compiler head also emits the six
remaining packed SSE2 statement shapes with their exact ordered inputs and
memory plus XMM0 through XMM7 clobbers. Two unchanged-source compiles produce
the same validated 8,768-byte object with SHA-256
`fd280c321b8eb38a90d4f0982d70b8df0364585e3da322eb2c9de722e071f8d4`.
The normal `kernel/cpu/simd.cc` recipe freezes the source and its seven-header
closure. Poisoning `CC` leaves it on the checked wrapper, and CupidDis decodes
the packed copy, broadcast, blend, and saturating-add paths. ADRs 0160 and
0168 record the earlier boundaries, ADR 0178 records packed SSE2 support, ADR
0179 records complete seed carriage, and ADR 0180 records production
ownership.

The checked seed also emits `kernel/smp/percpu.cc` completely. Its
exact GNU assembly forms load a packed six-byte GDTR, reload the code and
data segments, and write a represented 16-bit selector to GS. Two validated
compiles produce the same 6,760-byte object with SHA-256
`3c2c6f0e00e5edec1ca16cba91e9fc593d1c42e24f4ebd3591e5f574fb0dd772`.
The checked normal wrapper owns the 6,760-byte object and its frozen recursive
closure. The image and four-vCPU dual-NIC runtime gates pass. ADR 0157 records
the language boundary, and ADR 0167 records the production transfer.

The checked seed also represents the three exact naked IPI entries. The two
call wrappers emit without a C frame and retain a
typed direct-call relocation. The panic entry emits its complete halt loop.
The earlier `smp.c` compiler proof produced an 8,444-byte object with SHA-256
`806509a6dd1ac7eb34b7ffcb67a1f8852950663a274145584d0260da76dcba54`.
The checked production root is `kernel/smp/smp.cc`; its 8,444-byte object has
SHA-256
`bd3189b2a1a6d15728c559172f5d6acca0889103428085cec8cc1024742a22d1`.
The existing `__FILE__` diagnostic accounts for the new hash. ADR 0156 records
the language boundary, and ADR 0167 records the production transfer.

The private compiler now bounds the parser work behind that smoke. It accepts
128 active loop-or-switch controls and 1,024 active statement calls, rejects
the next entry before further recursion, and restores both counters after a
failed REPL evaluation. This changes embedded JIT safety without retiring or
adding a host dependency.

The hosted preprocessor contract runs 391 tracked profile executions through
the repository file adapter. This covers 238 root-kernel and Doom C inputs,
three user inputs, 107 Cupid programs, 33 strict hosted i386 Linux roots, four
strict hosted i386 Windows tool roots, one freestanding i386 Windows root, two
strict hosted i386 kernel-bridge roots, and three GNU runtime roots. Only
`kernel/lang/as_elf.cc` and its Toolchain contract receive `/kernel/lang` as
an include root. A separate target materializes and checks four generated
kernel C inputs through the existing generators and first-pass link. The
target profiles use checked repository headers and
`__SIZEOF_POINTER__=4` for the static Linux closure. The two legacy 64-bit
profiles have no roots. Every hosted contract now belongs to the checked i386
cohort, so no hosted unit is deferred and no host C transform remains.

The production-integration item above now includes the checked Toolchain
contract cohort. Checked-seed CupidC owns the 238-source checked-in normal
cohort, generated kernel symbols, and the six generated-install or user
translations. The rebuilt compiler stages own all fifteen contract programs.

The broad inventory below records the original IR contract boundary. Its statements that wide multiplication, division, remainder, mutation, open-position arguments, and wider variadic reads were unsupported are historical. The phrase "eighteenth host-built artifact" names the owner at that old boundary. Both checked compiler stages now build it, while native binaries remain optional oracles. ADRs 0072 through 0075 supersede those gaps.

The hosted layout contract still supplies independent manual ABI oracles for every FAT16 member offset, active Doom bit fields, and representative process, syscall-table, `e1000_rx_desc_t`, and per-CPU layouts. The declaration contract supplies separate source-driven proof for the FAT closure plus namespace, declarator, declaration-legality, target-integer, rollback, scale, and nesting cases. The IR contract is the eighteenth host-built artifact. It covers unchanged active addition, direct calls, automatic locals, the complete `vga_flip_ready` body, the complete `syscall_sleep_ms` loop, the Doom wipe tick loop, the guarded browser `for` loops, guarded nested declarations, guarded active `break` and `continue` statements, the VGA setter, the timer member getter, the Paint coordinate transforms, `cemit_multiply_overflows`, `cemit_power_of_two`, `cfront_bool_valid`, `asm_branch_fits_i8`, `obj_region_less`, `ctool_job_arena`, the AES `rotw` helper, the CPUID-toggle return statement, the memory `align_up` helper, active negation and logical-not fragments, and the Doom color declaration and reads. The multiplication-overflow fixture pins 21 exact IR instructions, including unsigned division. A separate object covers signed and unsigned quotient and remainder in 138 text bytes with five symbols and no relocations. The logical AND fixture pins 23 exact IR instructions and a 143-byte function with five checked branch targets. Each logical OR fixture pins 20 exact IR instructions and a 127-byte function with six checked branch targets. The comparison object adds three 39-byte functions to the active CupidASM helper, for 244 text bytes, five symbols, and no relocations. It covers all signed and unsigned less-than forms. The combined function object has 917 text bytes with ten relocations at refreshed call offsets. The shift fixture pins ten IR instructions. Its 86-byte object covers `SHL`, unsigned `SHR`, signed `SAR`, and `OR` in two functions with three symbols and no relocations. The CPUID-toggle fixture pins 13 IR instructions and one exact 69-byte local function with no relocations. Shared decoding covers `XOR` and the surrounding shift, mask, and comparison. The memory-alignment fixture pins 16 IR instructions and one exact 73-byte local function with no relocations. Shared decoding covers `NOT` with the surrounding addition, subtraction, and mask. The integer-unary fixture pins 16 IR instructions across four functions. Its object has 86 text bytes, five symbols, no relocations, and decoded coverage for negation and normalized logical not. The active pre-test loop fixture pins 14 exact IR instructions and one 94-byte local function. Its false exit lands at byte offset 92, its backward jump lands at byte offset 20, and its three direct-call relocations are at offsets 11, 24, and 80 with addend `-4`. A focused terminal-body pre-test loop pins five instructions with no backward jump. The active post-test Doom fixture pins 21 exact IR instructions and one 125-byte local function. Its false exit lands at byte offset 123, its backward jump lands at byte offset 6, and its two direct-call relocations are at offsets 14 and 78 with addend `-4`. A focused terminal-body post-test loop pins one return with no condition or backward edge. The browser expression-`for` fixture pins 23 exact IR instructions. Two break functions add eight instructions, and six continuation and nesting functions add 47. Their combined object has 426 text bytes across nine functions, ten symbols including the null symbol, fixed branch targets, and no relocations. The declaration-initialized loop pins 17 instructions, the nested-compound function pins 16, the loop-body fixture pins ten across `while`, `do`, and `for`, and the unreachable declaration publishes two. Their object has 238 text bytes across four functions, five symbols including the null symbol, fixed local slots, exact branch targets, and no relocations. Omitted-clause fixtures cover a terminal body and a non-fallthrough infinite loop. The bit-field fixture lowers a volatile `r` read to `FILE_ADDRESS`, `BIT_FIELD_LOAD`, and `RETURN_VALUE`. Its object covers unsigned and signed extraction, storage offsets 0, 4, and 8, a full-width field, 63 text bytes, and three direct-object relocations with addend zero. At that boundary, focused negatives kept 64-bit division, remainder, mutation, and wide shift counts unsupported. Terminal-body negatives reject an unreachable `do` condition, an unreachable `for` iteration, and an unreachable wide declaration. Narrow and atomic fields also remain unsupported, and a valid one-byte packed record with a four-byte declared storage unit receives a feature diagnostic rather than a malformed-input diagnostic. The pointer-value contract adds 50 exact active-source instructions and 61 focused instructions across twelve functions; its exact 266-byte object proof is described above. The combined `ctool_job_arena` and comparison contract adds 27 exact instructions, with twelve more for explicit pointer casts. Its exact object has six functions in 198 text bytes and no relocations. Eight pointer-condition functions publish 62 exact IR instructions and emit 372 exact text bytes with no relocations. A malformed frozen unit that changes `void *` equality into pointer order fails transactionally. Function pointer calls and values add 86 exact IR instructions across thirteen functions. A separate signed wide-parameter fixture adds a five-instruction register-indirect call. The object proof has 513 text bytes, seventeen symbols, nine text relocations, and one data relocation. Its first 234 text bytes are exact, and shared decoding finds four register-indirect calls, one direct call, and thirteen returns. Automatic object storage adds 47 exact IR instructions across five functions. Cast-to-void coverage adds the complete unchanged host allocation pair in 18 IR instructions and one mixed-operand function in 16. Supported structure operands now use the same typed discard after their lvalue snapshot. Its deterministic 52-byte object has three symbols and one direct-call relocation at text offset 43, and repeated emission is byte-identical. The automatic-object proof has 264 exact text bytes, nine symbols, three direct-call relocations, a mixed 12-byte frame with locals at EBP minus 3 and EBP minus 12, and the active `&children[index]` call shape in another 12-byte frame. Narrow indexed loads, stores, compound assignments, and updates now lower with their target width. Boolean mutation and aggregate forms outside the supported structure slice fail transactionally. Scalar variadic coverage includes direct and indirect callers plus a definition that starts, copies, reads, and ends a target cursor. The callee object is deterministic, has no relocations, and contains one positive EBP displacement, `16`, for the first unnamed argument after two named parameters. Its decoder-driven i386 oracle reads a pointer, then reads the same unsigned-long slot through copied and original cursor state. It returns `0x21426384` and preserves every incoming argument word. Wide arguments without a declared parameter type, atomic cursors or reads, wider or aggregate variadic reads, atomic callback loads, and a malformed relational comparison fail transactionally. The structure-value contract also covers bytewise copies, assignment results, rounded three-byte, eight-byte, and twelve-byte arguments, direct and indirect calls, hidden-result returns, deterministic padding, and decoded `RET 4` epilogues. None of these artifacts affects an OS binary or justifies an emulator result. The later wide contracts cover multiplication, division, remainder, and mutation. At that historical boundary, floating scalar values remained open. ADRs 0076 and 0077 now carry same-kind `float` and `double` values through object access, initialization, assignment, fixed calls, discard, returns, default-promoted open call positions, and `va_arg(double)`. At that boundary, deferred automatic initializer forms, aggregate categories outside the supported structure slice, Boolean mutation, narrow bit fields, non-four-byte storage units, partial volatile bit-field mutation, packed storage units that cross the record boundary, atomic access, floating literals, mixed-kind and integer conversions, comparisons, truth testing, conditionals, compound updates, other general value-producing floating conversions, explicit static floating initializers, non-scalar ellipsis transport, wider or aggregate variadic reads, and the remaining ABI surface open. Later entries in this file record the current boundary. A separate decoder proof checks all four call-padding amounts, nested calls, and direct or indirect scalar and structure calls.

ADRs 0079, 0091, 0125, 0136, 0137, 0147, 0196, 0199, and 0202 supersede the
floating gap list in the historical contract paragraph above. The current
floating boundary is the one recorded under **Not host compilation** below.

The wide parameter fixture adds nine functions without retiring a dependency. It covers single and mixed eight-byte parameters, direct and indirect calls, a declared wide parameter before an ellipsis, and a variadic cursor started after a final wide parameter. Its deterministic object has ten symbols and five text relocations. A relocated i386 oracle checks returned values, unchanged argument slots, and restored stack and frame pointers. ADR 0075 extends the same i386 boundary to signed and unsigned wide integers in direct and indirect ellipsis or unprototyped calls. Packed post-conversion types supply the outgoing width, and wide variadic reads consume eight bytes into one snapshot handle. At that checkpoint, GCC or Clang built the focused proof. In the normal build, checked-seed CupidC uses the declared-wide-parameter path for X25519's `fe_carry`, while the socket layer passes its `uint64_t` time value to TLS. Both checked compiler stages now build the complete contract; native host copies are optional oracles.

The wide operation fixture itself retires no dependency. Its relocated i386 oracle runs left and signed or unsigned right shifts at every defined count from 0 through 63, cross-word AND, OR, and XOR, mixed signedness, GNU wide-enum promotion, byte extraction, explicit and implicit represented widening, narrowing, same-width assignment conversion, and high-word Boolean truth. Transactional mutations reject reverse same-rank usual arithmetic conversion and promotion to the wrong enum-compatible type. The complete unchanged `ctool_buffer_put_le64` and `ctool_buffer_patch_le64` bodies lower and emit with three checked external call relocations. Limit failure restores an empty output, and a later operation in the same job reproduces the deterministic object. At that checkpoint, GCC or Clang built this contract path. Both checked compiler stages now build it, and no supported host-owned C transform remains.

The wide comparison fixture supersedes the earlier wide-condition negative inventory. Its 24 functions produce 264 exact IR instructions and 3,341 deterministic text bytes with no relocation. A decoder-driven i386 oracle executes signed, unsigned, and usual-arithmetic comparisons plus logical not, short-circuit AND and OR, selection, and `if`, `while`, `do`, and `for` conditions. It distinguishes low-word order when high words match, tests a signed high-word subtraction that sets overflow, and treats a value with only its high word set as true. Full-body guards and execution cases cover `pp_if_value_truth`, `pp_if_is_negative`, and `pp_if_signed_less`. Malformed metadata and constrained output retain transactional failure and same-job recovery. The arithmetic fixture below closes addition, subtraction, multiplication, and nonlogical unary operations. A separate full-width switch proof has 46 exact IR instructions and a deterministic 504-byte object with no relocations. At that proof boundary, division, remainder, mutation, and values without a declared parameter type were host-built gaps. Both checked compiler stages now build every compiler and contract object; GCC or Clang copies are optional native oracles.

The wide arithmetic fixture adds addition, subtraction, multiplication, unary plus, unary minus, and bitwise complement without moving a production owner. Its 19 functions produce 118 exact IR instructions; the original 83-instruction prefix keeps fingerprint `245E6D8F4F77588E`. The earlier deterministic object has 3,156 text bytes, 26 symbols including the null symbol, no relocations, and fingerprint `B52392EA`. A separate multiplication object has 1,103 text bytes, seven symbols including the null symbol, no relocations, and fingerprint `E357BE84`. Its decoder finds seven `MUL`, fourteen `IMUL`, six returns, and no call or divide. The i386 oracles check carry, borrow, unsigned wrap, defined signed cases, unary identities, multiplication cross terms, mixed and narrow conversions, chained operations, and snapshot stability. Full-body guards bind the unchanged `pp_if_signed_magnitude`, CupidASM number-parser and unary-expression helpers, and X25519 `fe_mul_u32`. Malformed binary, unary, and multiplication metadata, constrained output, and same-job recovery retain transactional behavior. At the ADR 0072 boundary, wide division, remainder, mutation, and values without a declared parameter type were host-built gaps. ADR 0073 closes division and remainder, ADR 0074 closes mutation, and ADR 0075 closes signed and unsigned wide integer arguments in supported ellipsis and unprototyped calls. Both checked compiler stages now build every compiler and contract object; GCC or Clang copies are optional native oracles.

ADRs 0072 through 0075 supersede the earlier broad inventory that lists wide multiplication, division, remainder, mutation, and open call positions as unsupported. ADRs 0076 and 0077 likewise supersede the statement that all floating scalar values are open. Exact `float` and `double` transport, default-promoted open call positions, and `va_arg(double)` are represented. Floating computation and general value-producing conversion, aggregate values, atomic access, and other unrepresented forms remain outside the current ABI slice. Implicit static zero initialization and casts to `void` are represented.

ADR 0073 adds signed and unsigned eight-byte division and remainder without changing output ownership. Linear IR accepts each operation after promotion and the usual arithmetic conversions give both operands and the result one represented wide type. The arithmetic fixture now has 26 functions and 165 exact instructions. Its original 83-instruction prefix retains fingerprint `245E6D8F4F77588E`, and seven slices cover signed and unsigned quotient and remainder, mixed signedness, a widened narrow divisor, and a chained quotient/remainder expression. Invalid conversion or result metadata fails transactionally.

The i386 emitter copies both immutable operand snapshots into a 40-byte transient stack area. A fixed 64-step restoring loop keeps two-word dividend, divisor, quotient, and remainder state, with separate quotient and remainder sign words. Each round shifts the quotient, moves the dividend's top bit into the remainder, performs an unsigned high-word and low-word comparison, subtracts with `SUB` and `SBB` when required, and sets the quotient bit. A carry branch preserves the full comparison before the high-word and low-word checks and joins the shared subtraction block. The sequence uses EAX, ECX, EDX, and scratch memory only. Signed operations divide unsigned magnitudes, apply the XOR of the operand signs to the quotient, and apply the dividend sign to the remainder. The scratch area is released before the result goes to a fresh private snapshot.

The focused ELF32 proof has eleven functions, 4,775 text bytes, fingerprint `55F1A495`, twelve symbols including the null symbol, and no relocations. Its thirteen divide or remainder operations each contain the fixed loop. Shared decoding checks the five loop branches, their local targets, the common `SUB` and `SBB` block, sign handling, and the absence of `CALL`, `DIV`, and `IDIV`. The execution oracle makes 33 defined calls. It covers a zero dividend, `UINT64_MAX / 1`, equal operands, low-word and high-word values, high-bit divisors, all four signed sign combinations, `INT64_MIN / 1`, mixed and narrow conversions, chaining, input reuse, restored stack and frame state, preserved callee-saved registers, unchanged arguments, and stack sentinels. Repeat emission is byte-identical. A 64-byte output limit leaves the caller's output empty, and the next emission in the same job reproduces the 5,452-byte object.

The undefined cases are outside that runtime oracle. C leaves both division and remainder undefined when the divisor is zero. It also leaves `INT64_MIN / -1` and `INT64_MIN % -1` undefined because the quotient is not representable. The wide software loop has no defined result or trap requirement for those inputs. The narrow hardware `DIV` and `IDIV` path happens to raise `#DE`. The IR suite fingerprints the complete 9,313-byte normalized `cfront_constant_apply_binary` body as `CF0E333FEC913171` and checks CupidASM's complete `asm_parse_number` text exactly. The object suite guards the active frontend fragments, then emits the focused eleven-function fixture. These guards bind the focused fixtures to current source requirements. At that historical checkpoint, they did not prove full object emission or transfer CupidC ownership for either active function, and GCC or Clang built the compiler and contracts. Both checked compiler stages now build the complete programs, and no supported host-owned C transform remains.

The bit-field assignment fixture adds 31 exact IR instructions across four functions. Pointer-based functions cover unsigned eight-bit, signed five-bit, and full-width fields, while the indexed function matches Doom's `colors[index].r` shape. The deterministic object keeps a 1,024-byte color array in `.bss` and one absolute text relocation. Six execution cases check truncation, signed extension, neighboring bits, one storage write, and the no-read full-width path. Character-sized, Boolean, atomic, and compact packed forms retain focused diagnostics.

The bit-field mutation fixture expands hosted semantics without retiring another dependency. Exact IR streams cover prefix, postfix, and compound lowering, plus a matrix containing all ten compound operators. The deterministic 1,415-byte object has 20 functions, 21 symbols including the null symbol, and no relocations. A decoder-driven i386 oracle checks signed and unsigned field-width wrap, old postfix values, neighboring-bit preservation, argument and stack integrity, the one-read, one-store volatile 32-bit path, and exactly one index advance in a side-effecting record designator. Partial fields currently need a second complete-unit read for their final merge, so partial volatile mutation remains unsupported. At the original checkpoint, GCC or Clang built this compiler path and no normal OS object used it. Both checked compiler stages now build the path.

Direct label and `goto` coverage adds 73 exact IR instructions across eleven functions after entry-aware lowering removes dead structured prefixes. It includes entry into an infinite loop before `break` and `continue`, plus declaration ownership below a label. The object proof contains a 44-byte forward function, a 76-byte backward function, 38-byte terminal `if` and `while` functions, and a 41-byte function with one four-byte automatic local below its label. It has 237 text bytes, six symbols including the null symbol, no relocations, and nine decoded branch targets. Repeated emission is byte-identical. At its original checkpoint, this was host-built evidence and moved no normal object. Both checked compiler stages now build it as part of the transferred contract cohort.

Hosted switch coverage adds the unchanged `cfront_public_storage` function. It publishes 59 exact IR instructions and emits one exact 272-byte local function with six comparisons, six conditional branches, seven direct jumps, six returns, two symbols including the null symbol, and no relocations. Control and nesting fixtures cover fallthrough, no-default exit, nearest-target `break` and `continue`, cases inside structured statements, direct label entry, and unreachable nested switches. Both checked compiler stages build this evidence; it moves no normal OS object.

The tracked `link.ld` is itself a compatibility contract. It uses `ENTRY`, `SECTIONS`, location-counter assignment, input-section wildcards, `ALIGN`, symbol definitions, `COMMON`, and repeated `ASSERT` statements. Both kernel ELF targets declare it as a prerequisite and pass it to CupidLD.

## Not host compilation

The current wide scalar boundary represents constants, fixed call results,
object access, initialization, assignment and update, declared parameters,
named arguments, supported variadic and unprototyped arguments, variadic
reads, return and discard, integer arithmetic, bitwise operations, shifts,
comparisons, logical operators, conditions, switch dispatch, casts,
same-rank signed-to-unsigned conversion, GNU wide-enum promotion, and
conversion to or from represented integer widths.

ADRs 0076, 0077, 0079, 0136, 0137, and 0147 add exact `float` and `double`
transport, default-promoted open call positions, `va_arg(double)`, runtime
arithmetic, static constant data and arithmetic, and all six matching or
mixed-width comparisons. ADR 0196 adds automatic non-atomic `long double`
transport, floating-width conversion, unary and binary arithmetic,
twelve-byte arguments, returns, call results, `va_arg(long double)`, and
zero-filled static leaves in scalars, fixed arrays, and complete records. ADR
0199 adds all six matching or mixed long-double comparisons. ADR 0202 adds
runtime truth, controlling expressions, and conversion to `_Bool`. ADR 0229
adds bounded finite normal decimal long-double literals. ADR 0250 adds runtime
conversion from `float` and `double` to unsigned four-byte targets. ADR 0251
carries bounded decimal literals into exact static scalar and aggregate data.
ADR 0253 adds runtime conversion between `long double` and every signed or
unsigned i386 integer width. ADR 0254 adds target-only static initializer
conversion for the same widths, `_Bool`, plain `char`, and enums whose
compatible integer types have a represented target layout.
ADR 0255 adds target-only static long-double truth, comparison, short-circuit
logic, conditional selection, and conversion to or from binary32 and
binary64. ADR 0256 accepts canonical x87 zero, subnormal, normal, infinity,
and NaN payloads and adds special-value conversion without host floating
work. ADR 0260 adds integer-only static long-double `+`, `-`, `*`, and `/`
with exact target rounding and no runtime IR.

ADR 0263 adds prefix and postfix update for modifiable non-atomic `float` and
`double` lvalues. Linear IR evaluates the destination once, and the emitter
returns the original payload for postfix forms after storing the replacement.

Runtime mixed integer and floating arithmetic or conditional arms, atomic and
long-double updates, hexadecimal floating literals, binary32 and
binary64 subnormal literals, hexadecimal or subnormal long-double literals,
decimal ratios beyond the bounded parser, aggregate floating values, atomic
access, and other unrepresented forms remain
outside the current ABI slice.

The wide-mutation proof expands shared semantics. Fifteen functions publish 225 exact IR instructions, and 17 emitted functions occupy 4,410 text bytes with fingerprint `4B337038`, 18 symbols including the null symbol, and no relocations. Decoder and execution checks cover all ten compound operators, signed and unsigned prefix or postfix update, postfix snapshot preservation, one-time indexed evaluation, volatile access, cdecl state, rollback, and deterministic recovery. Checked-seed CupidC uses this path for the `+=` and `&=` operations in X25519's `fe_carry`, and both checked stages build the focused contract. GCC or Clang provides only the optional native copy.

- The 107 active `bin/*.cc` roots and 22 `bin/browser/*.cc` fragments are wrapped by CupidObj and installed in the OS filesystem. CupidC compiles them on demand inside Cupid OS.
- The 22 `demos/*.asm` files are likewise embedded by CupidObj and assembled by CupidASM on demand.
- Repository headers and compatibility code replace the host libc/header environment for root compilation (`-nostdlib -nostdinc -ffreestanding`). The checked i386 Linux profiles declare the command-facing ABI, and the repository supplies a matching narrow runtime. The normal OS and Toolchain builds use Cupid tools; only explicit native oracles use the host toolchain.
- The normal hosted contracts link against Cupid's narrow i386 runtime. Optional native oracles use the host C runtime through the core adapter and thin CLI drivers. The shared arena, buffer, path, source, diagnostic, limit, object, instruction, assembly, and inspection behavior is freestanding, and the same CupidASM source is linked into the kernel.
- Optional WAD discovery and test fixtures affect packaged/runtime content, not compiler ownership.

ADR 0261 changes in-kernel graphics ownership only. The desktop, retained
windows, legacy frame path, and fullscreen programs coordinate through a
PID-tagged handoff, and the process reaper releases abandoned render state.
It does not add or retire a host build dependency. Python still drives the
frontier command sequence, and QEMU remains the runtime oracle.

## Removal gate

A code-producing host dependency leaves the normal build only after the Cupid replacement has positive and negative tests, matches required object/ABI/layout behavior, builds its assigned active-source cohort, and passes the relevant OS boot or runtime smoke. The legacy host path remains available as an oracle until fixed-point bootstrap and behavior gates are reliable.
