# Host dependency inventory

The deterministic active-source audit records three supported build roots: root `all`, `user:all`, and `toolchain:all`. It evaluates Make conditionals with `OS=Windows_NT` and `LC_ALL=C` on every host so the checked graph has one stable shape, then covers the Linux branch with direct build tests. `audits/active-build.json` owns the current 698-input/504-transform graph, and its 432-path manifest covers all 425 final-link objects. The language graph contains 102 C translation units, 270 headers, and 299 Cupid C files; the prioritized toolchain-source cohort contains 69 files. The checked Windows Clang/LLVM and Linux GCC/binutils baselines both reproduce the complete 447-artifact three-root cohort at revision `1e079d1`; `baselines/windows-linux.json` verifies their common logical cohort and required behavior while treating cross-toolchain byte equality as observational. That evidence predates the CupidC preprocessing, declaration, type/layout, IR, and object contracts. The hosted root declares 25 artifacts, including six Cupid-built i386 executables, so its complete three-root cohort would contain 460 logical artifacts when recaptured from one committed revision.

The normal root build no longer sends every C object through GCC or Clang.
Checked-seed CupidC owns 158 C transforms across the three roots. The normal
cohort contains 151 checked-in sources and the generated kernel symbol table.
All 152 normal sources use `.cc`. The 19-source i386 Linux fixed point uses
the same naming, including its five roots shared with the normal image.
Native GCC or Clang recipes select C explicitly with `-x c`; this avoids
silently changing the language while the checked seed consumes those roots.
Three generated installation tables and three example programs account for
the other six transforms. The host C compiler still owns 139 transforms,
including 87 root objects. Python owns 173 transforms, including the 158
CupidC launches, one external-program syscall ABI verification, and two ISO
fixture operations. Make owns five transforms. The
fifth prepares the native hosted CupidC and CupidLD drivers for `user:all` on
Windows.

The ABI verification captures the exact bytes of its six declaration inputs,
compares the reviewed i386 contract, and rechecks every input before success.
The external-program runtime gate gives hello, ls, and cat separate private
copies of the staged image. ADR 0133 records these consistency boundaries.

Linux runs the checked i386 seed for all six user artifacts. Windows runs
private snapshots of the two native hosted drivers. A separate frontier
compares all six Windows outputs with the seed. That user path no longer
needs WSL after the drivers exist. The root and generated-table checked-seed
paths still use WSL on Windows. Clang and its native linker still build the
Windows drivers, so the native user path does not establish a Windows fixed
point.

The strict frontier must compile all 151 checked-in sources twice. Every
transferred Make recipe names its exact recursive header closure and common
checked-seed controls. Poisoned-host recipes, strict syntax, focused tests,
and the normal-image gate remain part of the proof. The full 151-root
frontier passes twice against a 439-file frozen snapshot with SHA-256
`dd61ee8ece6a26282f7ae2d5f252f53c109827bf3e7a3365a00cc5a6e8d59a8a`.
The two object sets are byte-identical and total 3,643,676 bytes. The combined 151-root graph
also carries the ISO fixture as an explicit image input and passes the strong
four-vCPU runtime gate with both NICs.
ADRs
0110 and 0111 record the earlier transfers, ADR 0115 records
the first source-driven ownership, ADR 0123 records the latest production
transfer, ADR 0124 records the 111-root naming transfer, ADR 0126 records
the fixed-point rename and old-seed proof, ADR 0129 records the lexer
handoff, ADR 0135 records the Nuked OPL3 transfer, ADR 0139 records the
JPEG and glyph-raster transfer, and ADR 0160 records the FPU and SMP transfer.

The combined cohort's four-vCPU GUI proof starts every CPU, forces the CSPRNG
through RDRAND, passes all 62 crypto, ASN.1, and X.509 checks, reaches e1000
traffic, opens the desktop and terminal, and completes embedded CupidC
execution at `0x01100000`. The dual-NIC contract also covers audio, TrueType
glyph use, an exact 8-by-8 JPEG decode, UHCI input reattachment, and six EHCI
storage lifetimes. The private-image gate loads and
reaps the same external ELF program twice at `0x00F00000`, with lease release
between the two runs.

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
fixture emits the twelve opening x87/SSE floating wrappers from `libm.c` in
248 text bytes through Cupid's x86 encoder, with twelve global function
symbols and no relocations.
Compiler head resolves named function-body operands without invoking GAS.
It now emits the exact x87 statements in `libm_pow_impl()` and
`libm_powf_impl()`. The double form has five `double` memory operands. The
mixed form has a `float` output, two `float` inputs, and two `double` inputs.
Each 116-byte focused function uses no relocations and returns the x87 stack
to its incoming depth. Compiler head also emits the exact `sqrtsd %1, %0`
statement with a `double` `=x` output and a `double` `x` input. The focused
function has 65 text bytes and no relocations. It also emits the exact x87
statement in `libm_atan2_impl()` with one `double` `=m` output, two `double`
`m` inputs, and one `memory` clobber. That focused function has 53 text bytes
and no relocations. It also emits the exact x87 statement in
`libm_exp_impl()` with one `double` `=m` output, two `double` `m` inputs,
and one `memory` clobber. That focused function has 71 text bytes, no
relocations, and balanced x87 depth. The complete unchanged source now
reaches the aligned file-scope `fabs` mask block at line 242. The checked
seed predates named operands and all five statement forms, so GCC or Clang
continues to own the normal `libm.c` transform. No dependency or production
ownership count changes.

The USB lifetime work retires no additional compiler transform, but it
supplies the runtime contract for EHCI and UHCI ownership. Reconciliation
keeps failed work durable, rotates backed-off retries fairly, and reuses
device addresses and block slots after safe release. Hub callbacks report
changes while the core owns teardown, reset, enumeration, acknowledgement,
and edge rereads. Controller-local generations prevent stale cancellation
from retiring a reused interrupt slot, and cancellation waits for callback
and DMA quiescence. A quarantined address requires a proved reset before
companion handoff. Block references reject saturation, and mass storage
restores its online state if unregister fails. Compiled fixtures and 44 USB
tests pass. The 62 GUI gate
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
floating-to-signed conversions, floating-to-unsigned byte or word
conversions, and mixed represented integer and floating arithmetic.
Unsigned four-byte input uses an exact split across the sign boundary. The
refreshed seed carries that path, and `kernel/lang/cupidc_lex.cc` now builds
through the checked wrapper.
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
library. This path emits the unchanged Doom automap object. Hexadecimal
floating literals, `long double`, runtime conversion to unsigned four-byte
integers or `_Bool`, runtime floating truth, mixed wide and floating runtime
arithmetic or conditionals, and floating increment and decrement remain
host-bound. Matching or mixed-width floating
conditional arms and the four arithmetic compound assignments retain their
established x87 path. Older detailed rows that list every floating literal,
static floating initializer or arithmetic, comparison, or integer conversion as open are
superseded by this boundary.

Later sections preserve ownership wording that accompanied earlier capability
slices. The current 151-source checked-in cohort, complete `.cc` naming, and
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
semantics in native recipes. Four strict checked-in roots remain host-owned.

The checked seed clears the former language blocker in
`kernel/audio/nuked_opl3.cc`. The frontend finalizes its ordinary declaration
and inline definition as one C11 external definition, and two full compiles
produce the same validated 40,424-byte object. The closed normal recipe,
frontier, image, and dual-NIC runtime gates now pass. The wrapper compiles
from a private copy of the source and its three headers, then rejects live
input drift before replacing the object. This retires one host C root
dependency.

CupidC accepts GNU `used` and `__used__` on canonical file-scope
objects and functions. The Linear IR and object boundaries validate the
frozen flag, and the focused object proof reproduces the generated
`section(".ksyms"), used, aligned(4)` declaration. The generated
`kernel/cpu/ksyms_data.cc` now compiles through the normal checked wrapper.
Its packed i386 words preserve the exact 105,505-byte blob. The current
105,920-byte object has
SHA-256
`4a343b54571ed94324ce09e3ba48859ecdb36497e4e284b5f7996c81ed260131`.
Python still serializes the blob, but it runs a frozen CupidDis image against
a frozen pass-one kernel. It rejects malformed output, an empty text-symbol
set, i386 address overflow, and live input drift before atomic publication.
This retires the generated root's GCC or Clang dependency. ADR 0116 records
the language boundary, and ADR 0123 records the production transfer.

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

Value-preserving bit-field assignment changes compiler capability without moving another output. Four focused functions cover unsigned, signed, full-width, pointer-derived, and indexed stores. The execution oracle checks the stored value, neighboring bits, arguments, and stack state. GCC or Clang still builds the shared frontend, Linear IR, emitter, and contracts. The proof adds no transform beyond the current production cohort and retires no executable, linker, assembler, or object-tool dependency.

Ordinary narrow bit-field promotion also changes compiler capability without moving an output. The frontend and Linear IR now retain and validate the direct member behind an eight-bit `unsigned int` field's promotion to signed `int`. A 127-byte exact object and eight decoder-driven executions cover the active shift and mask forms. The checked seed uses this support to emit unchanged `kernel/doom/src/i_video.c`; two exact-profile compiles reproduce its 9,312-byte object with SHA-256 `8e9fcb59120cac9e8237a8243003fe1696a7841096aca7af360c89fec173336f`. Every Doom recipe remains unchanged. GCC or Clang still builds the compiler and this proof, so no host dependency is retired.

Eight-byte integer values cross the shared path through full-width constants, matching conditional results, fixed direct and indirect call results, object access, initialization, plain and chained assignment, declared parameters, named arguments, ellipsis and unprototyped call arguments, variadic reads, discard, returns, arithmetic, unary operations, shifts, bitwise operations, comparisons, logical operations, conditions, switch dispatch, and conversion to or from represented integer widths. File objects, block statics, fixed automatic objects, pointer dereferences, ordinary members, and indexed elements use private eight-byte frame snapshots. The i386 emitter restores the low word to EAX and the high word to EDX on return. Calls publish packed post-conversion actual types in emitted instruction order, which gives an open-position wide integer two adjacent stack words and advances a wide variadic cursor by eight bytes. The CupidC-built socket and TCP objects now use this production path. The deterministic result, object, parameter, operation, and call-position contracts remain host-built.

The floating work does not move production ownership. The shared path copies matching `float` and `double` values through objects, calls, variadic reads, and returns. It now evaluates same-kind unary plus and minus and binary addition, subtraction, multiplication, and division. Every changed x87 result is stored immediately at its C width. A `float` rounds into a fresh four-byte semantic slot, and a `double` receives a fresh private eight-byte snapshot. The exact `libm_tanh_impl` guard pins nested `double` arithmetic with call-produced operands. The execution model checks operand order, immediate spills, selected IEEE patterns, call alignment, and frame state. It does not execute native x87 code. GCC or Clang still builds the compiler and proof, so the dependency table and ownership counts do not change.

The static evaluator also leaves the dependency inventory unchanged. It uses
only target-sized integer arithmetic to produce IEEE binary32 and binary64
bits, so it adds no host floating or math-library dependency. The checked
seed carries this evaluator, while the Doom cohort remains host-owned.
GCC or Clang still builds the native compiler and its contracts.

Mixed-mode raw inspection also leaves the dependency inventory unchanged. CupidDis now accepts borrowed ordered 16/32-bit ranges and its hosted CLI exposes `--mode-at OFFSET:16|32`. The existing CupidDis executable still owns the normal kernel-symbol inspection transform, but that transform uses ELF input and does not need a raw map. GCC or Clang still builds the hosted CLI and the in-kernel adapter, so no output changes owner and no host tool is retired.

The self-host source frontier also retires no dependency. Hosted CupidC emits deterministic i386 ELF32 objects for all fourteen files in issue #27's CupidC, CupidASM, and CupidDis cohort. Ten cohort files use the hermetic profile. `kernel/lang/as_elf.cc` is the kernel bridge, and the hosted adapters use Cupid-owned i386 Linux declarations for their runtime interfaces. The profile rejects a missing or non-32-bit pointer fact. The gate also covers complete CupidLD and CupidObj command closures. Adapter checks lock the named undefined imports and every text relocation.

The repository i386 Linux runtime replaces the tracer's test-only providers for complete tool closures. CupidC compiles allocation, file, memory, string, `errno`, working-directory, and diagnostic services. CupidASM supplies startup and system-call wrappers, and CupidLD produces static CupidC, CupidASM, CupidDis, CupidLD, and CupidObj commands. Linux and WSL behavior matches the native sibling commands for real outputs and failure paths.

The five static commands now share one complete checked-seed gate. The manifest binds the exact executables, source revision, target ABI, producer lineage, 19-source build plan, startup, and five link orders. The current seed contains the checked bootstrap's stage-three images at revision `c00b3494014ca0a5f41143caa7e713e46b2ad3ec`. CupidC, CupidASM, and CupidDis changed from the preceding seed, while CupidLD and CupidObj remain byte-identical. The 2,320,544-byte CupidC image has SHA-256 `fe4e99837053332e32624208bfceddc60e2be9cdcea5bdacb5b174e6b432cdbb`.

The harness copies the exact 40-input source closure into a private compiler root. Checked CupidC compiles the stage-two union there, checked CupidASM assembles startup, and checked CupidLD links all five tools. The stage-two producer trio repeats that work for stage three below the same root. Both the private closure and the live closure are checked before the first stage, after each stage, and after behavior checks. Every seed image matches stage two, every C object, startup object, and linked image matches across the stages, and both stages execute positive and failure cases for every command. The two stages, behavior evidence, and report are published together only after success. This tighter source and publication boundary does not retire another host dependency. A clean checkout can rebuild the static i386 Linux Toolchain without external code generation. The native contracts, hosted development commands, and remaining normal OS C objects remain host-owned.

Two active-source fragments anchor the wide call requirement. `toolchain/tests/cupidc_object_contract.c::decode_function` passes the signed `long long` branch target to `fprintf`. `toolchain/tests/cupidc_frontend_contract.c::validate_file_object_finalization_storage_limit` passes three `unsigned long long` byte counts to `fprintf`. The guards cover those call fragments only. They do not establish whole-function CupidC ownership. No active-source guard covers a wide `va_arg` or an unprototyped wide call, so those paths have focused ABI fixture evidence only. Current public modules contain 77 frontend tests, 65 IR tests, and 83 object tests. The neighboring `variadic-callees`, `old-style-empty-functions`, `wide-returns`, and `floating-transport` modes remain part of the full gate. The `js_push_num` guard covers its declaration and assignment lines only, not the full browser interpreter function.

Cast-to-void support now serves production e1000, desktop, and TCP code. The shared path evaluates the operand once, emits `DISCARD` for a represented integer, object pointer, or function pointer, and leaves a `void` operand off the abstract stack. The complete unchanged `ctool_host_allocate` and `ctool_host_release` helpers guard the focused requirement. A deterministic 52-byte object proves the existing discard and direct-call emission paths. GCC or Clang and the native host linker still build the focused proof, while checked-seed CupidC emits the production uses.

Automatic aggregate initializer lowering serves the CupidC-built desktop object. CupidC semantically zeros a complete fixed automatic array or structure, then evaluates represented leaves in source order and stores them through direct member and element paths. A supported structure-valued leaf uses the structure copy path. The object emitter preserves EDI and uses `CLD` plus `REP STOSB` for the complete object before explicit stores. Named automatic aggregate declarations still initialize in place; backward-jump reentry with an escaped alias remains open under issue #25. The active `no_name` initializer in `cupidc_pp.cc` and the `{0}` type-node initializer in `cupidc_frontend.cc` retain focused guards. GCC or Clang and the native host linker still build those contracts, while the host C compiler owns 139 transforms recorded below.

Runtime narrow string lowering serves production e1000 and desktop code. `STRING_LITERAL_ADDRESS` gives normal string expressions local `.rodata` symbols and absolute text relocations. `COPY_STRING` fills named automatic arrays, nested initializer leaves, and block-scope compound literals after their destinations have been zeroed. The unchanged automatic hexadecimal array in `drivers/serial.cc` retains a focused source guard. GCC or Clang and the native host linker still build those contracts, and the host C compiler owns 139 recorded transforms.

Structure values serve the CupidC-built socket and desktop objects. CupidC copies complete supported structures through loads, stores, assignment results, conditional joins, expression initialization, discard, fixed direct and indirect calls, and returns. Instruction-owned frame slots hold snapshots and call results. The i386 call path places structure arguments inline in rounded four-byte spans and uses a hidden return pointer at `EBP + 8`; the callee returns that pointer through EAX and removes its slot with `RET 4`. The shared x86 catalogue has 587 forms, 242 mnemonics, 64 registers, and fingerprint `68E281CB`, and the checked seed carries that full model. It covers all sixteen i686 conditional moves in 16-bit and 32-bit widths, the complete 16-bit and 32-bit three-operand immediate `IMUL` family, ordinary `90`, `66 90`, and `0F 1F /0` padding, and `RET imm16`. A private decoder path accepts only five exact repeated-prefix Clang padding strings and creates no catalogue form. CupidASM accepts canonical and alias conditional-move spellings, chooses `6B /r` only for a signed-byte multiply constant, applies mode-sized defaults to memory NOPs, and rejects invalid operands or prefixes. It cannot request redundant prefixes. CupidDis renders stable canonical names and keeps conservative recovery around malformed bytes. GCC or Clang and the native host linker still build these contracts. The host compiler remains responsible for 139 transforms, so this change retires no dependency.

The private in-kernel CupidC emitter now sends `continue` in a `do` loop to the condition. The shared hosted path can emit static data and functions with canonical one-byte, two-byte, and four-byte integer values plus 32-bit integer arithmetic, signed and unsigned division and remainder, every integer relation, bitwise AND, OR, and XOR, all four integer unary operators, explicit casts among represented one-byte, two-byte, and four-byte integer types, both shift directions, both short-circuit logical operators, statement-level `if` with optional `else`, pre-test `while`, post-test `do`, `for` with expression or declaration initializers and optional iteration, nearest-loop `break` and `continue`, and multiple returns. It also covers fixed direct and indirect calls with four-byte argument slots and normalized narrow results, represented target-sized scalar locals and target-sized fixed automatic arrays and structures in supported compound statements, including the initializer-list subset, linked file-object loads, direct ordinary record-member loads, four-byte integer bit-field reads, value-preserving plain assignments, compound assignments and prefix or postfix updates for represented non-Boolean byte, word, and doubleword integers, and pointer compound assignments and updates, and discarded nonvoid values in deterministic ELF32 objects. The unchanged `section_map` and `children` arrays and their indexed uses drive automatic object storage. The unchanged `asm_lower`, `x86_class_width`, and `x86_set_memory_width` functions drive signed and unsigned byte and word loads, stores, promotions, conditions, and results. The unchanged `cemit_multiply_overflows`, `cemit_power_of_two`, `cfront_bool_valid`, `asm_branch_fits_i8`, and AES `rotw` helpers drive division, logic, comparisons, shifts, and bitwise OR. The unchanged `size++`, `capacity *= 2u`, and `value /= 10u` statements in `toolchain/ctool.cc` pin four-byte destination-preserving mutation. The complete unchanged `x86_put_u8` body and active decoder byte operations pin narrow mutation. Their 201-byte exact object proof contains four functions, one four-byte BSS object, six symbols, and one `R_386_32` relocation. The separate narrow-mutation object has eight functions in 878 exact text bytes, ten symbols, one byte of BSS, and one absolute relocation. The unchanged CPUID-toggle return statement drives XOR with its mask, comparison, and `bool` conversion. Its surrounding GNU inline assembly and broader statement sequence remain outside this hosted leaf slice. The unchanged memory `align_up` helper drives bitwise complement inside unsigned arithmetic and masking. The complete unchanged `dis_signed_bits` helper drives two comparisons, two conditional branches, three returns, complement, addition, an explicit unsigned-to-signed cast, and negation. Its deterministic object contains one 143-byte local function, 71 decoded instructions, two symbols, no relocations, and branch targets at byte offsets 53 and 111. The complete unchanged `syscall_sleep_ms` helper drives a pre-test loop. Its deterministic object contains one 94-byte local function, 43 decoded instructions, branch targets at byte offsets 92 and 20, and three direct-call relocations at offsets 11, 24, and 80. The unchanged Doom wipe tick loop drives a post-test loop. Its deterministic object contains one 125-byte local function, 59 decoded instructions, branch targets at byte offsets 123 and 6, and two direct-call relocations at offsets 14 and 78. The guarded `url_hash_hex` loop drives a `for` path, while unchanged statements in `cir_validate_initializer_ownership` drive loop control. Their combined deterministic object contains the 107-byte browser function and eight loop-control functions totaling 319 bytes. It has 426 text bytes, ten symbols including the null symbol, exact decoded branch targets, and no relocations. The active `cc_skip_brace_initializer` fragment drives logical not without claiming its complete function. The VGA setter drives a linked store, the timer getter drives an ordinary member at byte offset 8, and the Doom color source drives an eight-bit field at bit offset 16. Bit-field emission also covers signed extraction, a nonzero storage offset, and a full-width field. Local and unresolved external calls use `.rel.text` `R_386_PC32` relocations with addend `-4`; direct object addresses use `R_386_32` with addend zero. Member selection and field extraction do not change the base symbol or relocation addend. This work retires no host dependency. GCC or Clang and the native host linker still build the shared modules and contracts. All nine hosted Toolchain source gates parse completely, including `cupidc_ir.cc`, `cupidc_emit.cc`, and `cupidc_frontend.cc`.

The narrow-mutation proof uses the shared decoder as a small test-only i386 execution oracle. Twelve zero and wrap-boundary cases check EAX, the stored byte or word, and poisoned padding in the four-byte argument slot. This adds no emulator or host execution dependency.

The nine-file source count in the preceding historical summary is superseded by ADR 0081. The hermetic frontend gate contains twelve Toolchain implementation files. The deterministic object gate adds `kernel/lang/as_elf.cc` and the three hosted adapters, for sixteen sources in all.

| Dependency | Current role | Current requirement | Fixed-point disposition |
| --- | --- | --- | --- |
| GCC with i386/multilib support | Compiles the remaining root C objects on Linux and builds the native hosted core, CupidC preprocessing/declaration/type-layout/IR/object operations, ELF32, x86, CupidDis, CupidASM, CupidObj, and CupidLD contracts/CLIs | Required on Linux for the remaining root and hosted contract builds; the checked seed owns 151 checked-in normal roots, generated kernel symbols, three generated tables, and all three user programs | Remove it from the remaining code-producing normal path; retain it only as an optional oracle or bootstrap escape hatch |
| Clang with i386 target support | Compiles the remaining root C objects on Windows and builds the same native hosted contracts and commands | Required on Windows for those remaining root and hosted builds, including the native CupidC and CupidLD drivers used by `user:all`; the prepared user path itself does not call Clang | Remove it from the remaining code-producing normal and contract paths; retain it only as an optional oracle or bootstrap escape hatch |
| NASM | Optional comparison oracle for the four active-source CupidASM parity tests and the shared ELF32 reader | Not required by root `all`, `user:all`, `toolchain:all`, or baseline preflight; `make nasm-assembly-oracle` uses it when installed | Retain only as an optional oracle/bootstrap escape hatch |
| Host linker backend (`ld`, `ld.lld`, `lld-link`, or platform equivalent) | No direct i386 OS/user link recipe remains; CupidLD owns those five outputs. The host C compiler still invokes a native linker backend to bootstrap the Cupid contract and CLI executables, and standalone ELF linkers remain optional comparison oracles. Canonical Windows LLD links use `/Brepro` so hosted PE timestamps cannot invalidate same-host evidence | Required transitively wherever hosted Cupid tools are rebuilt, including root `all`, `user:all` on Windows, and `toolchain:all`; not an owner of an OS/user ELF transform | Remove from the normal bootstrap after checked Cupid-built seeds/self-hosting exist; retain standalone ELF linkers only as optional oracles/escape hatches |
| GNU `objcopy` / `llvm-objcopy` | No role in the normal build; tracked legacy/oracle helpers may still invoke it manually, and the checked `6731dd6` evidence fingerprints the then-installed oracle | Not required for root `all`, `user:all`, `toolchain:all`, or new `bootstrap-baseline` captures | Retain only as an optional comparison/maintenance utility; CupidObj owns the production transformations |
| GNU `nm` / `llvm-nm` | Optional comparison oracle for CupidDis's numeric symbol view and historical baseline evidence | Not required by root `all`, `user:all`, `toolchain:all`, or baseline preflight; configured through `NM` only for optional oracle probes/tests | Retain only as an optional comparison/maintenance utility; CupidDis owns production kernel-symbol inspection |
| Hosted C runtime/libc | Backs the native hosted adapter's allocation, whole-file, and diagnostic seams plus the CupidC preprocessing, declaration, type/layout, IR, and object contracts and the native CupidC, CupidDis, CupidASM, CupidObj, and CupidLD command drivers. Cupid owns checked i386 Linux declarations and a matching narrow runtime for static Cupid-built commands | Native libc remains required by the temporary native oracle, contracts, and hosted production commands. The repository runtime is sufficient for the five generated Linux i386 commands but is not a general libc or a Windows runtime | Retain a platform runtime seam; it must not own preprocessing, parsing, type/layout semantics, code generation, object, assembly, link, or inspection semantics |
| GNU Make | Declares the root, user, and toolchain-contract build graphs and invokes tools | Required; the graph uses portable ordinary/stamp targets rather than GNU Make 4.3 grouped-target syntax | May remain as host orchestration; it must invoke Cupid code-producing tools on the normal path |
| Python 3 | Generates embedded-source/symbol tables; creates, stages, and cleans images; builds fixtures; transforms JPEG data; drives QEMU network tests with standard-library sockets; parses and correlates Ethernet PCAP captures; verifies the external-program syscall ABI; launches checked-seed CupidC for 152 normal production objects, and launches native CupidC for the Windows user cohort | Required | May remain for orchestration, verification, test control, and image packaging; code/object/link behavior stays behind Cupid tools |
| WSL on Windows | Runs the checked static i386 Linux CupidC seed for 152 normal-build production objects, three generated installation tables, and the staged Toolchain bootstrap | Required for those paths on Windows; the native Windows user build no longer uses it, and native Linux runs the seed directly | Remove from the remaining Windows build when a checked native CupidC or an equivalent Cupid-owned execution path is available |
| Git | Enumerates the tracked audit universe and creates detached baseline worktrees | Required for development/audit workflows, not image production | Retain as source-control orchestration, never as a code-producing dependency |
| `link.ld` and its documented GNU-script subset | Defines kernel memory and section layout; CupidLD parses the exercised `ENTRY`, `SECTIONS`, location-counter, wildcard, alignment, symbol, `COMMON`, and `ASSERT` forms | Required input to both kernel link passes; host-linker interpretation is oracle-only | Keep the script as the source-owned layout contract and deepen CupidLD when the active script needs more semantics |
| `jpegtran`, `djpeg`/`cjpeg`, or FFmpeg | Optional JPEG normalization selected by `tools/hostbuild.py`; availability changes embedded bytes | At least one converter is preferred; raw-copy fallback exists | Keep as optional asset preprocessing or replace with a deterministic checked policy; fingerprint the selected path |
| `mkisofs`, `genisoimage`, or `xorrisofs` | Builds the test ISO for explicit ISO targets | Required only when regenerating the ISO fixture | Retain as test-fixture tooling or replace with a deterministic Python implementation |
| Bash, curl, OpenSSL, xxd, and Unix text tools | Manual CA-bundle refresh and legacy/oracle helper scripts | Not required by root `all`; required for those maintenance paths | Keep only documented maintenance dependencies; Python/Cupid paths own normal-build behavior |
| QEMU `qemu-system-i386` | Boots emulator smoke and integration tests | Required for automated emulator verification, not image production | Retain as a test dependency; real-hardware tests remain complementary |
| Host shell/platform utilities | Launch Make, Python, and tests | Required operational environment, but no reachable transform is owned by an ad-hoc shell recipe | Keep only non-code-producing orchestration requirements |

The hosted pointer slice now serves the transferred e1000, desktop, socket, and TCP objects. Four-byte object pointers cross supported cdecl parameters and results, automatic and linked storage, direct calls, loads, stores, assignment, initialization, qualification, both directions between object pointers and `void *`, null conversion, dereference, address-of, and indirect ordinary members. Structural compatibility admits distinct pointer-to-array graph nodes, removes top-level pointer-object qualifiers during value conversion, and carries array qualification to the element comparison. The unchanged `obj_region_less` helper publishes 50 exact IR instructions. The unchanged `ctool_job_arena` helper reaches pointer inequality, typed null casts, pointer truth testing, pointer-valued conditional selection, and indirect member loading. Complete-object pointer arithmetic covers scaled offsets, compatible pointer difference, normalized subscripts, linked array decay, and pointer mutation. The two unchanged ATA transfer loops pin `buf += 256`, and exact fixtures reproduce their two-byte stride and constant offset. The focused object has nineteen functions in 811 exact text bytes, twenty-one symbols, one sixteen-byte BSS array, and two absolute relocations. Function pointers retain their signatures across the same four-byte scalar paths, including fixed indirect calls. Their object proof has thirteen functions in 513 text bytes, seventeen symbols, nine text relocations, one data relocation, four register-indirect calls, and one direct call. GCC or Clang and the native host linker still build the focused proofs; checked-seed CupidC uses these paths for the four production objects.

The function-pointer type relation is now an arena-backed, memoized worklist instead of a recursive walk. The contract covers repeated callback children, old-style promotions, ignored top-level parameter `const`, `volatile`, and `restrict`, significant `_Atomic` and referent qualifiers, missing parameter storage, and checked scratch rollback. A second object adds a 28-byte local-function address proof with one `R_386_32` relocation to a defined static symbol. These tests remain host-built. Explicit function pointer casts that produce values are still rejected. A cast to `void` only discards the represented value, so it adds no hidden target conversion policy.

Hosted narrow integer values and mutation now serve the transferred production objects. One-byte and two-byte loads sign-extend or zero-extend into canonical 32-bit values, compound assignments and updates compute through 32-bit promotion, stores use the declared byte or word width, and `_Bool` conversion tests the full source word. Fixed cdecl calls keep four-byte argument slots and normalize narrow results in both caller and callee paths. The value object proof covers 30 functions, 31 decoded returns, four direct calls, three register-indirect calls, signed and unsigned byte and word loads, exact-width stores, and a two-byte BSS object aligned to two bytes. The mutation proof adds eight functions, 878 exact text bytes, fourteen byte stores, four word stores, and one volatile byte load. These focused contracts remain host-built, while checked-seed CupidC owns the production emission.

Hosted sixteen-byte call alignment now serves calls in the transferred production objects. A target-private pass derives the live Linear IR stack depth along reachable control flow. The i386 emitter combines that depth with the fixed frame and outgoing ABI storage, then reserves zero, four, eight, or twelve bytes. A control-flow decoder checks ESP at every reachable direct or indirect call across conditional joins and loop back edges. A symbolic oracle checks three argument values after a twelve-byte padding move. GCC or Clang still builds the focused emitter contract, while checked-seed CupidC emits the production call sites.

Hosted scalar variadic callees follow that same boundary. GNU C mode exposes `__builtin_va_list` as a target `char *` cursor, and the frontend, IR, and emitter carry start, argument, copy, and end through represented non-atomic four-byte pointers and four-byte or eight-byte integers. Four-byte reads advance the cursor by four bytes. Wide reads copy eight bytes into one private snapshot and advance the cursor by eight. The unchanged Doom compatibility header parses under its generated profile. A decoder-driven i386 execution oracle checks a pointer read and independent reads of the same unsigned-long slot through copied and original cursors. A focused wide fixture checks signed, unsigned, successive, and copied-cursor reads. GCC or Clang still builds every changed module, so the dependency count and normal OS build ownership do not move.

Hosted empty identifier-list definitions and unprototyped calls keep the same ownership boundary. The frontend preserves a non-prototype function type and applies default promotions to every call argument. Linear IR carries the actual count and one packed post-conversion type for each argument. The emitter uses those types for cdecl layout, alignment, and cleanup. Signed and unsigned wide integers, existing `double` values, and source `float` values promoted to `double` occupy two adjacent stack words in direct and indirect calls. GCC or Clang still builds the shared compiler and all three focused contracts, so the host dependency count and normal OS build ownership remain unchanged.

Block-scope `struct` and `union` tags now serve the production desktop object. The frontend owns their lexical identity and completion, including record tags declared in a function definition's parameter list. An empty tag declaration with a represented storage class or type qualifier adds no runtime IR when it introduces a tag; repeating a visible tag without a declarator is rejected. A `for` initializer can use a visible tag or anonymous record for its object but cannot introduce a named tag. Deterministic object evidence covers Doom's anonymous block-static record, its exact literal bytes, the text reference to `packs`, and all three string relocations. The exact Doom profile passes this declaration and now parses the complete `d_main.c` file after the linked-object work in ADR 0058. The focused proof remains host-built; checked-seed CupidC owns the desktop use.

Block-scope external objects are also host-built capability. The frontend keeps lexical aliases separate from canonical linked entities, and Linear IR lowers each use through `FILE_ADDRESS` without reserving an automatic slot. The exact ELF32 proof has 15 text bytes, three symbols, and one `R_386_32` relocation to one undefined object. GCC or Clang still builds the compiler and contract. No normal Cupid OS object changed owner, and no host dependency was retired.

Block typedefs remain host-built capability as well. The frontend keeps each alias in the ordinary lexical namespace with a stable type, while Linear IR validates the declaration without emitting work. The ELF32 proof matches the same function with the underlying type spelled directly, byte for byte. GCC or Clang still builds the compiler and contracts, so no normal Cupid OS object changes owner and no host dependency is retired.

Block function declarations remain a host-built capability too. The frontend gives each lexical name its visible type and one canonical linked function. Linear IR validates both function types without allocating storage or emitting an instruction for the declaration. The ELF32 proof is byte-identical to equivalent file-scope declarations and contains one undefined function, two `R_386_PC32` call relocations, and one `R_386_32` address relocation. GCC or Clang still builds the compiler and contracts, so no normal Cupid OS object changes owner and no host dependency is retired.

Block enums now serve the production desktop object. The frontend keeps their lexical tags, ordinary enumerator names, target values, and source activation points across declarations, record members, function-definition parameter lists, and block type names. Linear IR turns represented uses into integer constants without allocating storage, and the ELF32 proof matches direct folded constants byte for byte with no enum symbol or relocation. The cursor and REPL enums remain in their active source files. GCC or Clang still builds the focused proof; checked-seed CupidC owns the desktop use.

Hosted block-static emission now serves the production desktop object. The shared frontend retains constant roots and absolute block-binding identities, the lowerer emits no declaration-time stores, and the object emitter assigns local symbols in `.rodata`, `.data`, or `.bss`. Runtime addresses use `R_386_32`, and block statics never receive automatic frame slots. The exact object proof covers eleven static objects and sixteen relocations, including shadowed, unused, and unreachable declarations. GCC or Clang and the native linker still build the focused proof; checked-seed CupidC owns the desktop block statics.

Hosted block-scope compound literals also change capability without retiring a dependency. The shared frontend owns the initializer forest, and Linear IR retains one unnamed-object identity per source site. The i386 emitter assigns a persistent automatic frame slot to that identity. Aggregate lists use a separate staging slot and one complete-object copy so initializer reads finish before the persistent object changes. Initialization runs at every evaluation before the expression returns the object's address. GCC or Clang and the native host linker still build the compiler and contracts. No current production object exercises this compound-literal path.

## Resolved output ownership

Counts are output transforms in the checked audit, not textual recipe occurrences. Composite Python transforms list the code-producing utility they invoke as a second owner.

| Tool hand-off | Reachable outputs | Required external behavior |
| --- | ---: | --- |
| Host C compiler | 139 | 87 i386 root objects, 33 native hosted core/CupidC-preprocessing/declaration/type-layout/IR/object/ELF32/x86/CupidDis/CupidASM/CupidObj/CupidLD/kernel-bridge objects, and 19 native contract/CLI executables; these builds remain temporary bootstrap evidence even though preprocessing, declaration, type/layout, scalar and structure-value code, fixed direct and indirect cdecl calls, file and block-static object emission, assembly, inspection, object-transformation, and link semantics have shared Cupid-owned implementations |
| CupidC | 158 owned transforms | The 151-source checked-in normal cohort, generated kernel symbols, three generated installation tables, and three example external programs; Python verifies and launches the checked seed for Linux and non-user Windows paths, while Windows user builds run a private native driver snapshot; each object is validated before publication |
| CupidASM | 4 owned transforms | Two production flat binaries and two production ELF32 `ET_REL` objects; the raw outputs are byte-identical to the optional NASM oracle and the objects match its code, symbol, alignment, and relocation semantics |
| NASM | 0 production transforms | Optional active-source and ELF32 interoperability oracle only |
| CupidLD | 5 owned transforms | Two script-driven kernel links plus three fixed-address user executables; owns `R_386_32`/`R_386_PC32`, weak/strong/common/script symbols, absolute COMMON alignment, relocation-aware merge entries, assertions, static ELF32 serialization, explicit unsupported allocated-section diagnostics, and the used `link.ld` subset |
| CupidObj | 182 owned transforms | 172 canonical text-to-ELF wrappers, eight byte-exact binary-to-ELF wrappers, one Python-assisted JPEG wrapper, and final initialized ELF-to-raw conversion |
| CupidDis | 1 composite transform | Supplies deterministic numeric symbols to `_symbols_from_nm`; the current consumer cohort contains 4,392 text symbols and a 105,505-byte panic-backtrace blob; the host oracle remains optional |
| Python | 173 transforms | The 158 CupidC launches plus fourteen generation, inspection, link, and orchestration transforms and one external-program syscall ABI verification; Windows uses native CupidC for three user launches, and symbol generation still uses Python after CupidDis inspection |
| Make recursion | 5 transforms | Builds the hosted CupidASM, CupidObj, CupidLD, and CupidDis executables from the root and prepares native CupidC plus CupidLD for the Windows user build before production transforms consume them |

`tools/hostbuild.py::_symbols_from_nm` remains the drop-in numeric-reader subprocess seam, but the normal Make path passes `$(CUPIDDIS)` and no longer defines or invokes `$(NM)`. `embed_jpeg` performs optional image preprocessing, then calls CupidObj once with the original source identity; the former temporary-name wrapper plus three-symbol rewrite pass was removed. `tools/mksyms.sh` and `tools/embed_jpeg_baseline.sh` are tracked legacy/oracle duplicates outside the normal Make path.

The hosted contract suites use the host C compiler and its native linker backend to bootstrap and exercise the shared core, CupidC preprocessing/declaration/type-layout/IR/object operations, ELF32, x86, CupidDis, CupidASM, CupidObj, CupidLD, and the kernel's buffer-only fixed-image-to-`ET_EXEC` bridge. The ELF32, CupidASM, and CupidLD suites may also use NASM, GNU `readelf`, and standalone GNU/LLVM ELF linkers as optional comparison oracles. They prove that Cupid-written objects and executables are accepted by external consumers, that the Cupid reader accepts Clang-, NASM-, and linker-produced objects, that every active assembly source reaches the required raw, relocatable, or fixed artifact, and that all shared operations fail transactionally; absent oracle tools are skipped. Assembly, inspection, object-transformation, and link semantics plus production assembly/link/object ownership have transferred. The shared hosted CupidC path handles static data and functions, including direct and fixed indirect calls with one-byte, two-byte, or four-byte integer parameters and results, same-kind `float` and `double` parameters and results, plus supported structure parameters and results. It supports target-width integer and four-byte pointer locals, target-sized fixed automatic arrays and structures, including the supported initializer-list subset, linked target-width integer and four-byte pointer file objects, ordinary members, four-byte bit-field reads, plain scalar and structure assignments, compound assignments and prefix or postfix updates for represented non-Boolean byte, word, and doubleword integers, and pointer compound assignments and updates, 32-bit division and remainder, all integer relations and unary operations, bitwise operations, shifts, short-circuit logic, structured selection and loops, 32-bit and wide integer switch dispatch, nearest-target control, direct labels, and `goto`. Narrow loads sign-extend or zero-extend into canonical 32-bit words, mutation computes through 32-bit promotion, exact-width stores use a byte or word lane, and fixed scalar cdecl calls retain four-byte argument slots. Structure calls copy completed arguments into rounded inline spans and use a hidden result pointer when needed. Every supported direct or indirect call aligns ESP to sixteen bytes immediately before `CALL`; target-private depth analysis accounts for the frame, live semantic values, and outgoing storage. Explicit casts to `void` evaluate represented integer, pointer, floating, supported structure, or `void` operands and produce no value. The transferred e1000, desktop, socket, and TCP objects now exercise these paths in the normal build.

The shared preprocessor owns translation phases, macros, conditionals, reproducible predefined macros, dual-location `#line`, includes, once identity, pack metadata, and policy-neutral Cupid `#exe` markers. The declaration operation consumes that tape and publishes the shared type graph, completed layouts, canonical declarations, file object definitions, function-scoped labels, semantic initializer records, and immutable function-body AST. A file definition keeps definition-local type, storage, kind, location, and initializer ownership separate from the canonical first declaration. Explicit and tentative definitions use the same static forest as block-static objects. Repeated tentative declarations coalesce during parsing, then translation-unit finalization applies the merged type and supplies a zero root. Static addresses can name a linked file object or function after address-of, array decay, function decay, or pointer arithmetic with a represented integer constant expression. These remain semantic references whose checked signed target-byte addends are independent of host pointers. Automatic forests retain runtime expressions, while static forests retain zeros, target integers, strings, binding addresses, and direct array or structure lists. Duration-aware freeze validation keeps those storage domains separate and checks every owner, reference, payload, direct selector, and postorder edge. The object operation assigns the static forests to `.rodata`, `.data`, or `.bss`, writes target bytes and symbols, and turns represented addresses and addends into direct-symbol `R_386_32` relocations. The body subset continues to cover scalar and aggregate return, structured control flow, canonical labels and direct `goto`, integer and pointer expressions, conditional values, assignment and updates, calls, casts, and expression designators.

The native and Cupid-built compiler drivers now pass repeatable `-include`
inputs into that preprocessing operation in caller order. This lets the
checked seed reproduce the Doom-tree profile. CupidC retains the sound
driver's empty volatile memory barrier. Its integer-only IEEE evaluator also
folds the unchanged static fixed-point table in `am_map.c`. The explicit
`--doom-compat` profile also represents the five calls in `i_system.c` that
appear before a declaration. Strict C and plain GNU mode still reject those
calls. The same profile carries the eleven audited conversions
between unqualified function pointers and unqualified four-byte data or
`void` pointers in `m_menu.c`, `p_saveg.c`, `p_ceilng.c`, and `p_plats.c`.
Strict C and plain GNU mode still reject the implicit conversions, and their
explicit function/data casts remain outside Linear IR. One-active-member union
initialization also compiles unchanged `info.c`, and ordinary narrow bit-field
promotion compiles unchanged `i_video.c`. The checked seed emits all 80
objects, but all 83 Doom and port roots still use host recipes. The three
compatibility roots, object comparison and validation, and runtime proof must
pass before ownership moves. This Doom work itself changes neither the 87
host-built root objects nor the 139 host C transforms.

Eight-byte integer and exact floating object access use those existing storage identities. A wide `LOAD` copies eight bytes into its own frame snapshot, and `STORE` or `STORE_VALUE` copies from that snapshot to a selected object. This applies to file objects, block statics, fixed automatics, pointer dereferences, ordinary members, and indexed elements. A `float` load keeps its raw four bytes. A `double` load receives its own frame snapshot, and both types pass through compatible stores, fixed calls, discard, and returns. Same-kind floating arithmetic stores each changed result before the next IR instruction. Values already typed as `double` also pass through ellipsis and unprototyped calls, and `va_arg(double)` advances by eight bytes. The host compiler still builds the operation's native contracts and the 139 active host C transforms that have not moved.

File definitions and block-static bindings now share one object encoder. It places file objects first, then every block static in absolute binding order, before it emits functions. The same initializer forms, section rules, target bytes, symbol construction, and direct-symbol relocations apply to both storage domains. Static initializer addresses based on another block static remain a frontend boundary.

The unchanged FAT16 and active-header contracts still pin layout, redeclaration, attribute, assertion, and lexical ownership. The checked seed passes the active 155/155 non-Doom header sweep. `cpu.h` passes through the represented RDTSC form, the three roots that include `percpu.h` parse through all active integer atomics, and `ports.h` parses through all eight width-aware helpers. All twelve Toolchain source gates parse completely. Each five-number tuple reports definitions, statements, expressions, block bindings, and initializers. `cupidc_pp.cc` publishes 143/3,932/25,287/479/286. `cupidc_ir.cc` publishes 240/6,938/64,075/912/333. `cupidc_emit.cc` publishes 272/6,794/58,493/845/451, while `cupidc_frontend.cc` publishes 385/15,526/102,378/2,328/1,440. The generated audit records the current active-source totals and source graph.

These hosted semantics do not retire a host dependency. GCC or Clang still builds the shared frontend, emitter, and contracts, the host linker still links the hosted tools, and the host C compiler still owns 87 normal OS root objects and 139 active transforms, while the private kernel compiler owns embedded runtime JIT and AOT compilation. The open host-bound work includes chained and overriding designators, promoted anonymous-member designators, repeated union-member overrides, Cupid class lists, static member-address constants, explicit address casts, broader runtime values and addresses, deferred automatic initializer forms, aggregate categories outside the supported structure slice, Boolean mutation, character-sized bit-field storage, non-four-byte storage units, partial volatile bit-field mutation, pointer and eight-byte atomics, computed `goto`, GNU label addresses, the remaining GNU surface, hexadecimal floating literals, `long double`, unrepresented runtime floating and integer conversions, runtime floating truth and controlling expressions, runtime mixed wide and floating arithmetic or conditional arms, floating increment and decrement, broader local and function code generation, whole-unit emission, and production integration. The private compiler's tagged loop and switch frames change production JIT output and pass the expanded in-OS `feature25` smoke. It transfers no build ownership.

Checked-seed `noinline` and `target("general-regs-only")` semantics narrow
that GNU gap without changing the dependency count. The seed also accepts
the exact LDMXCSR memory input at line 28, all three MOVSS
float-memory forms in `fpu_boot_smoke()`, and the exact balanced x87
`fldl`, `fsin`, and `fstpl` block in `stress_sin()`. Two complete
builds of `kernel/cpu/fpu.cc` produce the same validated 6,620-byte object
with SHA-256
`14c3ea232b7d4455ceabd561c69293cc5849abae24d9f210aa69d64ed8c8a5cb`.

The checked seed also accepts the complete unchanged x87 control-word block in
`str_floor()`, including its exact AX and memory clobbers. The emitter reuses
the consumed input-address slot for the two stack scratch words, restores the
incoming x87 control word, and leaves the pending output address intact. Two
compiles of the extracted active helper produce the same 420-byte object with
SHA-256
`448012fe57ec625c6075e97cf91163b994a0443238c5d6bdf25e4b839763f14e`.
The full unchanged `kernel/core/string.c` source now stops at its independent
double-to-`uint64_t` cast on line 190.

`kernel/cpu/fpu.cc` has transferred to checked CupidC.
`kernel/core/string.c` keeps its `.c` name until the complete translation
unit and production gate pass. ADRs 0141, 0146, 0148, 0150, and 0154 record
the boundaries.

The checked seed also emits `kernel/smp/percpu.cc` completely. Its
exact GNU assembly forms load a packed six-byte GDTR, reload the code and
data segments, and write a represented 16-bit selector to GS. Two validated
compiles produce the same 6,760-byte object with SHA-256
`3c2c6f0e00e5edec1ca16cba91e9fc593d1c42e24f4ebd3591e5f574fb0dd772`.
The checked normal wrapper owns the 6,760-byte object and its frozen recursive
closure. The image and four-vCPU dual-NIC runtime gates pass. ADR 0157 records
the language boundary, and ADR 0160 records the production transfer.

The checked seed also represents the three exact naked IPI entries. The two
call wrappers emit without a C frame and retain a
typed direct-call relocation. The panic entry emits its complete halt loop.
The earlier `smp.c` compiler proof produced an 8,444-byte object with SHA-256
`806509a6dd1ac7eb34b7ffcb67a1f8852950663a274145584d0260da76dcba54`.
The checked production root is `kernel/smp/smp.cc`; its 8,444-byte object has
SHA-256
`bd3189b2a1a6d15728c559172f5d6acca0889103428085cec8cc1024742a22d1`.
The existing `__FILE__` diagnostic accounts for the new hash. ADR 0156 records
the language boundary, and ADR 0160 records the production transfer.

The private compiler now bounds the parser work behind that smoke. It accepts
128 active loop-or-switch controls and 1,024 active statement calls, rejects
the next entry before further recursion, and restores both counters after a
failed REPL evaluation. This changes embedded JIT safety without retiring or
adding a host dependency.

The hosted preprocessor contract runs 379 tracked profile executions through
the repository file adapter. This covers 238 root-kernel and Doom C inputs,
three user inputs, 105 Cupid programs, twelve shared hosted Toolchain units,
the hosted `as_elf` bridge, 19 strict static i386 tool sources, and the
GNU-enabled runtime. A separate target materializes and checks four generated
kernel C inputs through the existing generators and first-pass link. The
64-bit profiles describe the Windows AMD64 and Linux x86_64 bootstrap
processes. The target profiles use checked repository headers and
`__SIZEOF_POINTER__=4` for the static Linux closure. This is compatibility
evidence, not host-compiler retirement: the host compiler still compiles 87
normal root objects plus all native contracts and tools. Twenty native hosted
C transforms are explicit deferrals for external system headers or runtime
services; no hermetic hosted unit remains deferred.

The production-integration item above refers to the 139 remaining host-C
transforms. Checked-seed CupidC owns the 151-source checked-in normal cohort,
generated kernel symbols, and the six generated-install or user translations.

The broad inventory below records the original IR contract boundary. Its statements that wide multiplication, division, remainder, mutation, open-position arguments, and wider variadic reads were unsupported are historical. ADRs 0072 through 0075 supersede those gaps.

The hosted layout contract still supplies independent manual ABI oracles for every FAT16 member offset, active Doom bit fields, and representative process, syscall-table, `e1000_rx_desc_t`, and per-CPU layouts. The declaration contract supplies separate source-driven proof for the FAT closure plus namespace, declarator, declaration-legality, target-integer, rollback, scale, and nesting cases. The IR contract is the eighteenth host-built artifact. It covers unchanged active addition, direct calls, automatic locals, the complete `vga_flip_ready` body, the complete `syscall_sleep_ms` loop, the Doom wipe tick loop, the guarded browser `for` loops, guarded nested declarations, guarded active `break` and `continue` statements, the VGA setter, the timer member getter, the Paint coordinate transforms, `cemit_multiply_overflows`, `cemit_power_of_two`, `cfront_bool_valid`, `asm_branch_fits_i8`, `obj_region_less`, `ctool_job_arena`, the AES `rotw` helper, the CPUID-toggle return statement, the memory `align_up` helper, active negation and logical-not fragments, and the Doom color declaration and reads. The multiplication-overflow fixture pins 21 exact IR instructions, including unsigned division. A separate object covers signed and unsigned quotient and remainder in 138 text bytes with five symbols and no relocations. The logical AND fixture pins 23 exact IR instructions and a 143-byte function with five checked branch targets. Each logical OR fixture pins 20 exact IR instructions and a 127-byte function with six checked branch targets. The comparison object adds three 39-byte functions to the active CupidASM helper, for 244 text bytes, five symbols, and no relocations. It covers all signed and unsigned less-than forms. The combined function object has 917 text bytes with ten relocations at refreshed call offsets. The shift fixture pins ten IR instructions. Its 86-byte object covers `SHL`, unsigned `SHR`, signed `SAR`, and `OR` in two functions with three symbols and no relocations. The CPUID-toggle fixture pins 13 IR instructions and one exact 69-byte local function with no relocations. Shared decoding covers `XOR` and the surrounding shift, mask, and comparison. The memory-alignment fixture pins 16 IR instructions and one exact 73-byte local function with no relocations. Shared decoding covers `NOT` with the surrounding addition, subtraction, and mask. The integer-unary fixture pins 16 IR instructions across four functions. Its object has 86 text bytes, five symbols, no relocations, and decoded coverage for negation and normalized logical not. The active pre-test loop fixture pins 14 exact IR instructions and one 94-byte local function. Its false exit lands at byte offset 92, its backward jump lands at byte offset 20, and its three direct-call relocations are at offsets 11, 24, and 80 with addend `-4`. A focused terminal-body pre-test loop pins five instructions with no backward jump. The active post-test Doom fixture pins 21 exact IR instructions and one 125-byte local function. Its false exit lands at byte offset 123, its backward jump lands at byte offset 6, and its two direct-call relocations are at offsets 14 and 78 with addend `-4`. A focused terminal-body post-test loop pins one return with no condition or backward edge. The browser expression-`for` fixture pins 23 exact IR instructions. Two break functions add eight instructions, and six continuation and nesting functions add 47. Their combined object has 426 text bytes across nine functions, ten symbols including the null symbol, fixed branch targets, and no relocations. The declaration-initialized loop pins 17 instructions, the nested-compound function pins 16, the loop-body fixture pins ten across `while`, `do`, and `for`, and the unreachable declaration publishes two. Their object has 238 text bytes across four functions, five symbols including the null symbol, fixed local slots, exact branch targets, and no relocations. Omitted-clause fixtures cover a terminal body and a non-fallthrough infinite loop. The bit-field fixture lowers a volatile `r` read to `FILE_ADDRESS`, `BIT_FIELD_LOAD`, and `RETURN_VALUE`. Its object covers unsigned and signed extraction, storage offsets 0, 4, and 8, a full-width field, 63 text bytes, and three direct-object relocations with addend zero. At that boundary, focused negatives kept 64-bit division, remainder, mutation, and wide shift counts unsupported. Terminal-body negatives reject an unreachable `do` condition, an unreachable `for` iteration, and an unreachable wide declaration. Narrow and atomic fields also remain unsupported, and a valid one-byte packed record with a four-byte declared storage unit receives a feature diagnostic rather than a malformed-input diagnostic. The pointer-value contract adds 50 exact active-source instructions and 61 focused instructions across twelve functions; its exact 266-byte object proof is described above. The combined `ctool_job_arena` and comparison contract adds 27 exact instructions, with twelve more for explicit pointer casts. Its exact object has six functions in 198 text bytes and no relocations. Eight pointer-condition functions publish 62 exact IR instructions and emit 372 exact text bytes with no relocations. A malformed frozen unit that changes `void *` equality into pointer order fails transactionally. Function pointer calls and values add 86 exact IR instructions across thirteen functions. A separate signed wide-parameter fixture adds a five-instruction register-indirect call. The object proof has 513 text bytes, seventeen symbols, nine text relocations, and one data relocation. Its first 234 text bytes are exact, and shared decoding finds four register-indirect calls, one direct call, and thirteen returns. Automatic object storage adds 47 exact IR instructions across five functions. Cast-to-void coverage adds the complete unchanged host allocation pair in 18 IR instructions and one mixed-operand function in 16. Supported structure operands now use the same typed discard after their lvalue snapshot. Its deterministic 52-byte object has three symbols and one direct-call relocation at text offset 43, and repeated emission is byte-identical. The automatic-object proof has 264 exact text bytes, nine symbols, three direct-call relocations, a mixed 12-byte frame with locals at EBP minus 3 and EBP minus 12, and the active `&children[index]` call shape in another 12-byte frame. Narrow indexed loads, stores, compound assignments, and updates now lower with their target width. Boolean mutation and aggregate forms outside the supported structure slice fail transactionally. Scalar variadic coverage includes direct and indirect callers plus a definition that starts, copies, reads, and ends a target cursor. The callee object is deterministic, has no relocations, and contains one positive EBP displacement, `16`, for the first unnamed argument after two named parameters. Its decoder-driven i386 oracle reads a pointer, then reads the same unsigned-long slot through copied and original cursor state. It returns `0x21426384` and preserves every incoming argument word. Wide arguments without a declared parameter type, atomic cursors or reads, wider or aggregate variadic reads, atomic callback loads, and a malformed relational comparison fail transactionally. The structure-value contract also covers bytewise copies, assignment results, rounded three-byte, eight-byte, and twelve-byte arguments, direct and indirect calls, hidden-result returns, deterministic padding, and decoded `RET 4` epilogues. None of these artifacts affects an OS binary or justifies an emulator result. The later wide contracts cover multiplication, division, remainder, and mutation. At that historical boundary, floating scalar values remained open. ADRs 0076 and 0077 now carry same-kind `float` and `double` values through object access, initialization, assignment, fixed calls, discard, returns, default-promoted open call positions, and `va_arg(double)`. Deferred automatic initializer forms, aggregate categories outside the supported structure slice, Boolean mutation, narrow bit fields, non-four-byte storage units, and partial volatile bit-field mutation, packed storage units that cross the record boundary, atomic access, floating literals, mixed-kind and integer conversions, comparisons, truth testing, conditionals, compound updates, and other general value-producing floating conversions, explicit static floating initializers, non-scalar ellipsis transport, wider or aggregate variadic reads, and the remaining ABI surface also stay open. A separate decoder proof checks all four call-padding amounts, nested calls, and direct or indirect scalar and structure calls.

ADRs 0079, 0091, 0125, 0136, 0137, and 0147 supersede the floating gap list in
the historical contract paragraph above. The current floating boundary is
the one recorded under **Not host compilation** below.

The wide parameter fixture adds nine functions without retiring a dependency. It covers single and mixed eight-byte parameters, direct and indirect calls, a declared wide parameter before an ellipsis, and a variadic cursor started after a final wide parameter. Its deterministic object has ten symbols and five text relocations. A relocated i386 oracle checks returned values, unchanged argument slots, and restored stack and frame pointers. ADR 0075 extends the same i386 boundary to signed and unsigned wide integers in direct and indirect ellipsis or unprototyped calls. Packed post-conversion types supply the outgoing width, and wide variadic reads consume eight bytes into one snapshot handle. GCC or Clang still builds the focused proof. In the normal build, checked-seed CupidC uses the declared-wide-parameter path for X25519's `fe_carry`, while the socket layer passes its `uint64_t` time value to TLS. Those production callers do not retire the remaining host-built compiler and contract work.

The wide operation fixture itself retires no dependency. Its relocated i386 oracle runs left and signed or unsigned right shifts at every defined count from 0 through 63, cross-word AND, OR, and XOR, mixed signedness, GNU wide-enum promotion, byte extraction, explicit and implicit represented widening, narrowing, same-width assignment conversion, and high-word Boolean truth. Transactional mutations reject reverse same-rank usual arithmetic conversion and promotion to the wrong enum-compatible type. The complete unchanged `ctool_buffer_put_le64` and `ctool_buffer_patch_le64` bodies lower and emit with three checked external call relocations. Limit failure restores an empty output, and a later operation in the same job reproduces the deterministic object. GCC or Clang still builds this contract path and the remaining host-owned C transforms.

The wide comparison fixture supersedes the earlier wide-condition negative inventory. Its 24 functions produce 264 exact IR instructions and 3,341 deterministic text bytes with no relocation. A decoder-driven i386 oracle executes signed, unsigned, and usual-arithmetic comparisons plus logical not, short-circuit AND and OR, selection, and `if`, `while`, `do`, and `for` conditions. It distinguishes low-word order when high words match, tests a signed high-word subtraction that sets overflow, and treats a value with only its high word set as true. Full-body guards and execution cases cover `pp_if_value_truth`, `pp_if_is_negative`, and `pp_if_signed_less`. Malformed metadata and constrained output retain transactional failure and same-job recovery. The arithmetic fixture below closes addition, subtraction, multiplication, and nonlogical unary operations. A separate full-width switch proof has 46 exact IR instructions and a deterministic 504-byte object with no relocations. At that proof boundary, division, remainder, mutation, and values without a declared parameter type were host-built gaps. GCC or Clang still builds every compiler and contract object.

The wide arithmetic fixture adds addition, subtraction, multiplication, unary plus, unary minus, and bitwise complement without moving a production owner. Its 19 functions produce 118 exact IR instructions; the original 83-instruction prefix keeps fingerprint `245E6D8F4F77588E`. The earlier deterministic object has 3,156 text bytes, 26 symbols including the null symbol, no relocations, and fingerprint `B52392EA`. A separate multiplication object has 1,103 text bytes, seven symbols including the null symbol, no relocations, and fingerprint `E357BE84`. Its decoder finds seven `MUL`, fourteen `IMUL`, six returns, and no call or divide. The i386 oracles check carry, borrow, unsigned wrap, defined signed cases, unary identities, multiplication cross terms, mixed and narrow conversions, chained operations, and snapshot stability. Full-body guards bind the unchanged `pp_if_signed_magnitude`, CupidASM number-parser and unary-expression helpers, and X25519 `fe_mul_u32`. Malformed binary, unary, and multiplication metadata, constrained output, and same-job recovery retain transactional behavior. At the ADR 0072 boundary, wide division, remainder, mutation, and values without a declared parameter type were host-built gaps. ADR 0073 closes division and remainder, ADR 0074 closes mutation, and ADR 0075 closes signed and unsigned wide integer arguments in supported ellipsis and unprototyped calls. GCC or Clang still builds every compiler and contract object.

ADRs 0072 through 0075 supersede the earlier broad inventory that lists wide multiplication, division, remainder, mutation, and open call positions as unsupported. ADRs 0076 and 0077 likewise supersede the statement that all floating scalar values are open. Exact `float` and `double` transport, default-promoted open call positions, and `va_arg(double)` are represented. Floating computation and general value-producing conversion, aggregate values, atomic access, and other unrepresented forms remain outside the current ABI slice. Implicit static zero initialization and casts to `void` are represented.

ADR 0073 adds signed and unsigned eight-byte division and remainder without changing output ownership. Linear IR accepts each operation after promotion and the usual arithmetic conversions give both operands and the result one represented wide type. The arithmetic fixture now has 26 functions and 165 exact instructions. Its original 83-instruction prefix retains fingerprint `245E6D8F4F77588E`, and seven slices cover signed and unsigned quotient and remainder, mixed signedness, a widened narrow divisor, and a chained quotient/remainder expression. Invalid conversion or result metadata fails transactionally.

The i386 emitter copies both immutable operand snapshots into a 40-byte transient stack area. A fixed 64-step restoring loop keeps two-word dividend, divisor, quotient, and remainder state, with separate quotient and remainder sign words. Each round shifts the quotient, moves the dividend's top bit into the remainder, performs an unsigned high-word and low-word comparison, subtracts with `SUB` and `SBB` when required, and sets the quotient bit. A carry branch preserves the full comparison before the high-word and low-word checks and joins the shared subtraction block. The sequence uses EAX, ECX, EDX, and scratch memory only. Signed operations divide unsigned magnitudes, apply the XOR of the operand signs to the quotient, and apply the dividend sign to the remainder. The scratch area is released before the result goes to a fresh private snapshot.

The focused ELF32 proof has eleven functions, 4,775 text bytes, fingerprint `55F1A495`, twelve symbols including the null symbol, and no relocations. Its thirteen divide or remainder operations each contain the fixed loop. Shared decoding checks the five loop branches, their local targets, the common `SUB` and `SBB` block, sign handling, and the absence of `CALL`, `DIV`, and `IDIV`. The execution oracle makes 33 defined calls. It covers a zero dividend, `UINT64_MAX / 1`, equal operands, low-word and high-word values, high-bit divisors, all four signed sign combinations, `INT64_MIN / 1`, mixed and narrow conversions, chaining, input reuse, restored stack and frame state, preserved callee-saved registers, unchanged arguments, and stack sentinels. Repeat emission is byte-identical. A 64-byte output limit leaves the caller's output empty, and the next emission in the same job reproduces the 5,452-byte object.

The undefined cases are outside that runtime oracle. C leaves both division and remainder undefined when the divisor is zero. It also leaves `INT64_MIN / -1` and `INT64_MIN % -1` undefined because the quotient is not representable. The wide software loop has no defined result or trap requirement for those inputs. The narrow hardware `DIV` and `IDIV` path happens to raise `#DE`. The IR suite fingerprints the complete 9,313-byte normalized `cfront_constant_apply_binary` body as `CF0E333FEC913171` and checks CupidASM's complete `asm_parse_number` text exactly. The object suite guards the active frontend fragments, then emits the focused eleven-function fixture. These guards bind the focused fixtures to current source requirements. They do not prove full object emission or transfer CupidC ownership for either active function. GCC or Clang still builds the compiler, contracts, and the remaining host-owned C transforms, so this slice retires no additional dependency.

The bit-field assignment fixture adds 31 exact IR instructions across four functions. Pointer-based functions cover unsigned eight-bit, signed five-bit, and full-width fields, while the indexed function matches Doom's `colors[index].r` shape. The deterministic object keeps a 1,024-byte color array in `.bss` and one absolute text relocation. Six execution cases check truncation, signed extension, neighboring bits, one storage write, and the no-read full-width path. Character-sized, Boolean, atomic, and compact packed forms retain focused diagnostics.

The bit-field mutation fixture expands hosted semantics without retiring another dependency. Exact IR streams cover prefix, postfix, and compound lowering, plus a matrix containing all ten compound operators. The deterministic 1,415-byte object has 20 functions, 21 symbols including the null symbol, and no relocations. A decoder-driven i386 oracle checks signed and unsigned field-width wrap, old postfix values, neighboring-bit preservation, argument and stack integrity, the one-read, one-store volatile 32-bit path, and exactly one index advance in a side-effecting record designator. Partial fields currently need a second complete-unit read for their final merge, so partial volatile mutation remains unsupported. GCC or Clang still builds this compiler path, and no normal OS C object uses it.

Direct label and `goto` coverage adds 73 exact IR instructions across eleven functions after entry-aware lowering removes dead structured prefixes. It includes entry into an infinite loop before `break` and `continue`, plus declaration ownership below a label. The object proof contains a 44-byte forward function, a 76-byte backward function, 38-byte terminal `if` and `while` functions, and a 41-byte function with one four-byte automatic local below its label. It has 237 text bytes, six symbols including the null symbol, no relocations, and nine decoded branch targets. Repeated emission is byte-identical. This remains host-built evidence and does not move a normal C object to CupidC.

Hosted switch coverage adds the unchanged `cfront_public_storage` function. It publishes 59 exact IR instructions and emits one exact 272-byte local function with six comparisons, six conditional branches, seven direct jumps, six returns, two symbols including the null symbol, and no relocations. Control and nesting fixtures cover fallthrough, no-default exit, nearest-target `break` and `continue`, cases inside structured statements, direct label entry, and unreachable nested switches. This also remains host-built evidence and moves no normal C object to CupidC.

The tracked `link.ld` is itself a compatibility contract. It uses `ENTRY`, `SECTIONS`, location-counter assignment, input-section wildcards, `ALIGN`, symbol definitions, `COMMON`, and repeated `ASSERT` statements. Both kernel ELF targets declare it as a prerequisite and pass it to CupidLD.

## Not host compilation

ADRs 0065 through 0075 supersede older broad references to unsupported wide scalar values in the detailed evidence above. Constants, fixed call results, object access, initialization, plain assignment, all ten compound assignments, prefix and postfix update, declared parameters, named call arguments, signed and unsigned wide integer arguments in supported ellipsis and unprototyped calls, variadic reads, discard, return, addition, subtraction, multiplication, division, remainder, unary plus, unary minus, bitwise complement, shifts, AND, OR, XOR, comparisons, logical operators, conditions, signed and unsigned switch dispatch, explicit represented-to-wide casts, same-rank signed-to-unsigned conversion, GNU wide-enum promotion, and conversion to or from represented integer widths are represented. ADRs 0076, 0077, 0079, 0136, 0137, and 0147 add exact `float` and `double` transport, default-promoted open call positions, `va_arg(double)`, unary and binary runtime arithmetic, static constant data and arithmetic, and all six matching or mixed-width comparisons. Runtime floating truth and controlling expressions, runtime mixed wide and floating arithmetic or conditional arms, floating increment and decrement, hexadecimal floating literals, `long double`, aggregate floating values, atomic access, and other unrepresented forms remain outside the current ABI slice. Broader production ownership also remains open.

The wide-mutation proof expands shared semantics without retiring a host dependency. Fifteen functions publish 225 exact IR instructions, and 17 emitted functions occupy 4,410 text bytes with fingerprint `4B337038`, 18 symbols including the null symbol, and no relocations. Decoder and execution checks cover all ten compound operators, signed and unsigned prefix or postfix update, postfix snapshot preservation, one-time indexed evaluation, volatile access, cdecl state, rollback, and deterministic recovery. Checked-seed CupidC now uses this path for the `+=` and `&=` operations in X25519's `fe_carry`. GCC or Clang still builds the focused proof and the remaining host-owned C transforms.

- The 105 active `bin/*.cc` roots and 22 `bin/browser/*.cc` fragments are wrapped by CupidObj and installed in the OS filesystem. CupidC compiles them on demand inside Cupid OS.
- The 22 `demos/*.asm` files are likewise embedded by CupidObj and assembled by CupidASM on demand.
- Repository headers and compatibility code replace the host libc/header environment for root compilation (`-nostdlib -nostdinc -ffreestanding`). The checked i386 Linux profiles declare the command-facing ABI, and the repository supplies a matching narrow runtime. The normal OS build and native contract runners still use the host toolchain.
- The hosted contracts intentionally use the host C runtime only through the core adapter and thin CLI drivers. The shared arena, buffer, path, source, diagnostic, limit, object, instruction, assembly, and inspection behavior is freestanding, and the same CupidASM source is linked into the kernel.
- Optional WAD discovery and test fixtures affect packaged/runtime content, not compiler ownership.

## Removal gate

A code-producing host dependency leaves the normal build only after the Cupid replacement has positive and negative tests, matches required object/ABI/layout behavior, builds its assigned active-source cohort, and passes the relevant OS boot or runtime smoke. The legacy host path remains available as an oracle until fixed-point bootstrap and behavior gates are reliable.
