# Cupid Toolchain bootstrap

This directory records Cupid OS's move from a host-produced bootstrap to the
current checked-seed build and the remaining work toward a native,
Python-free fixed point. [GitHub issue #13](https://github.com/cupidthecat/cupid-os/issues/13)
is the implementation map.

CupidASM now accepts `align POWER_OF_TWO[, FILL_BYTE]` in the shared source
path. Raw output aligns `ORG + output offset`. ELF32 output aligns the current
section offset, updates `sh_addralign`, and keeps later labels and relocations
at their padded offsets. Fixed images align absolute region addresses, even
when a caller supplies an unaligned base. The optional fill defaults to zero;
NOBITS padding is logical only and rejects a nonzero fill. The FPU demo uses
this statement for its 16-byte FXSAVE area instead of relying on its position
at the front of `.data`. ADR 0197 records the boundary.

The private in-kernel CupidC compiler now gives runtime unary signs a typed
scalar path. Unary plus preserves `char`, `int`, `float`, or `double`
operands, with integer promotion where required. Unary minus emits `NEG` for
the integer path. For `float` and `double`, it spills XMM0, toggles only the
IEEE-754 sign word, reloads the original width, and restores the stack.
`feature13_double.cc` checks negative values at both widths, the exact
binary32 negative-zero payload, unary plus, rejection of a string operand,
and same-job recovery. The GUI command frontier requires that evidence before
JIT completion. Its one expected compiler diagnostic is scoped to that
completed command. An earlier copy, a second copy, or the same text outside
the feature run fails the gate. A host oracle compiles the emitter helpers
directly from `kernel/lang/cupidc_parse.cc`, checks their exact instruction
bytes, and interprets them against binary32 and binary64 payloads. ADR 0189
records this private-compiler decision.

The same private compiler now accepts all six scalar floating comparisons.
Matching widths use `UCOMISS` or `UCOMISD`; a mixed `float` and `double` pair
compares as `double`. Explicit parity checks make every unordered relation
false except `!=`. The result is a normalized `int` in EAX. A compiled host
oracle checks exact instruction bytes and interprets ordered values, signed
zero, subnormals, infinities, quiet NaNs, and signaling NaNs.
`feature13_double.cc` proves the behavior inside the complete four-vCPU
frontier. ADR 0192 records the decision.

Private CupidC now converts a scalar floating value to C truth before every
truth-consuming control path. Unary `!`, `if`, `?:`, `while`, `for`, and
`do ... while` share one lowering helper. It compares XMM0 with zero, treats
both signed zero encodings as false, and treats every nonzero value, infinity,
and NaN as true. Void expressions, structures by value, and SIMD vectors fail
with `truth test requires a scalar operand`. The exact-byte oracle covers
binary32 and binary64 zero, subnormal, finite, infinite, quiet-NaN, and
signaling-NaN payloads. The guest feature covers all six parser sites. ADR
0193 records this private-compiler boundary.

The private compiler's 510 kernel bindings now publish the same result type
as their local function-pointer declarations. The table contains 244 integer,
25 `float`, 25 `double`, 19 character-pointer, five other-pointer, and 192
`void` results. `BIND` is reserved for the `void` group, while `BIND_T`
records every value result. A source-contract test parses the complete table,
checks its exact size, and rejects an untyped non-void fixture. This prevents
a returned control value from being mistaken for a `void` expression.

Private CupidC also supports prefix and postfix `++` and `--` on scalar
`float` and `double` variables. One typed helper serves expression updates,
statement shortcuts, and `for` increments. It converts integer one into XMM1
at the variable's width, updates XMM0, and stores through the local,
parameter, or global path. A postfix expression holds the old payload in
XMM2 until the store is complete. Arrays, structures, function pointers, and
SIMD vectors fail with `increment or decrement requires a scalar variable`.
ADR 0194 records this private-compiler boundary.

Private CupidC now uses one cdecl slot model for callers, callees, and both
method-call parser paths. Integers, pointers, function pointers, `float`, and
implicit method `self` use four bytes. A `double` uses eight bytes. Arguments
are still evaluated from left to right; a shared word permutation then places
their complete slots at increasing addresses in source order without swapping
the two words inside a `double`. Callees advance parameter offsets by the same
widths, and callers reclaim the exact total. `feature13_double.cc` replaces
its copied tolerance expressions with a real `double, double, double, int`
helper used by ten checks. ADR 0198 records this private-compiler boundary.

Hosted CupidC now carries signed and unsigned eight-byte integer values through constants, matching conditional arms, fixed direct and indirect call results, object access, declared parameters, named call arguments, ellipsis arguments, and calls through function types without prototypes. File objects, block statics, fixed automatic objects, pointer dereferences, ordinary members, and indexed elements can be initialized, loaded, assigned, mutated, chained, discarded, and returned. One Linear IR entry names an emitter-owned eight-byte frame snapshot. A declared or undeclared wide argument occupies eight cdecl stack bytes. A supported wide `va_arg` read produces an instruction-owned snapshot and advances the cursor by eight. Return restores the low word to EAX and the high word to EDX.

Wide values support addition, subtraction, multiplication, division, remainder, unary plus, unary minus, bitwise complement, shifts, AND, OR, XOR, comparisons, logical operators, conditional selection, structured scalar conditions, signed or unsigned switch dispatch, all ten compound assignments, prefix and postfix update, and conversion to or from represented integer widths. Switch lowering evaluates the condition once and duplicates its snapshot handle before each full-width case comparison. Mutation evaluates its destination once and keeps one semantic load and store. Multiplication combines one full low-word product with both cross-word products. Division and remainder run a fixed 64-step restoring loop over unsigned magnitudes, then apply the quotient or dividend sign. Each multiplication, division, remainder, or wide variadic-read result receives a fresh snapshot. The unchanged `ctool_buffer_put_le64`, `ctool_buffer_patch_le64`, `pp_if_value_truth`, `pp_if_is_negative`, `pp_if_signed_less`, `pp_if_signed_magnitude`, `cfront_constant_apply_binary`, and X25519 `fe_carry` bodies guard the broader operation set. CupidASM's unchanged number parser and unary expression branch guard the arithmetic, while X25519's unchanged `fe_mul_u32` helper guards wide-by-narrow multiplication. ADRs 0065 through 0075 record these boundaries. Runtime cases that C leaves undefined promise neither a trap nor a result.

Hosted CupidC carries `float` and `double` values through object access, automatic initialization, plain assignment, discard, fixed direct or indirect calls, parameters, call results, and returns. Explicit casts and assignment conversion work in both directions between the two widths. Unary plus and minus and binary addition, subtraction, multiplication, and division accept matching or mixed floating operands. Matching floating conditional arms keep their width; mixed arithmetic and conditional arms use `double`. The four arithmetic compound assignments compute at the common width, convert back to the left width, and evaluate their lvalue once. Every changed x87 result is immediately stored at its C width. A `float` rounds into a fresh four-byte semantic slot, while a `double` receives a fresh private eight-byte snapshot. The unchanged `libm_tanh_impl` body pins nested arithmetic with call-produced `double` values, and the complete following `float` helper slice pins the width conversions. The path also promotes `float` to `double` at ellipsis and unprototyped call positions. Calls use four-byte or eight-byte cdecl slots, floating returns use x87 `ST0`, and `va_arg(double)` advances by eight bytes.

Decimal `float` and `double` constants carry exact IEEE bits from the frontend
into linear IR. The integer-only literal parser rounds once to nearest with
ties to even. Static-duration scalar and aggregate leaves use a separate
integer-only IEEE evaluator for unary signs, addition, subtraction,
multiplication, division, all six comparisons, casts, scalar truth,
short-circuit logic, and conditional selection. It rounds after each semantic
operation at the expression's binary32 or binary64 width, converts represented
signed and unsigned integers through 64 bits, preserves signed zero, and uses
the normal `.rodata`, `.data`, or `.bss` placement policy. SSE emission covers
represented runtime integer-to-floating conversions, floating-to-signed
conversions, floating-to-unsigned byte or word conversions, an explicit
non-atomic `double` to `unsigned long long` cast, mixed represented integer
and floating arithmetic, and all six comparisons. Matching widths use
`UCOMISS` or `UCOMISD`; a mixed pair widens to `double`. Explicit parity
handling makes every unordered relation false except `!=`. The unsigned
four-byte input path stays exact across `0x80000000` by converting its upper
31 bits and low bit separately. The unsigned-wide output path splits the
truncated value into high and low words around exact powers of two.

Non-atomic automatic `long double` objects use twelve-byte storage and x87
80-bit memory forms. They support conversion among the three floating widths,
unary plus and minus, and addition, subtraction, multiplication, and division.
Direct and indirect fixed, variadic, and unprototyped arguments occupy twelve
cdecl bytes. Functions return a `long double` in x87 `ST0`, and direct or
indirect callers spill the result into a twelve-byte snapshot.
`va_arg(long double)` copies twelve bytes and leaves the cursor at the
following four-byte slot. Static-duration scalars, fixed arrays, and complete
records may contain non-atomic long-double leaves. Implicit initialization
zeros the complete object; explicit leaves accept zero-valued integer constant
expressions and occupy twelve BSS bytes apiece. Atomic leaves fail recursively
without following pointers. All six comparisons accept matching long-double
operands or a long-double value paired with `float` or `double`. The i386
emitter loads right then left, compares with `FUCOMIP ST0, ST1`, and removes
the surviving x87 value. Its existing parity path makes only `!=` true for an
unordered input. Runtime `float`, `double`, and automatic `long double` values
also serve unary `!`, `&&`, `||`, the controlling operand of `?:`, the
conditions of `if`, `while`, `do`, and `for`, and conversion to `_Bool`. Both signed zeros are false;
finite nonzero values, subnormals, infinities, and NaNs are true. Hexadecimal
floating literals, `long double` literals, nonzero or floating static
long-double initializers, integer conversions involving `long double` other
than `_Bool`, runtime conversion to unsigned four-byte integers, other
floating-to-wide conversions, increment and decrement, SIMD, floating atomics,
and over-aligned floating objects remain open. ADR 0202 records the truth
boundary.

The static aggregate proof uses two 24-byte arrays and two 28-byte records.
They occupy 104 BSS bytes in a separate object whose 415-byte access function
has fingerprint `BF01CC71`, eight absolute relocations, and six symbols. The
hosted i386 runtime proves every leaf and marker starts at zero, then moves
1.5 through both file-scope and block-static members. `sizeof(float) - 4`
provides a zero integer constant-expression case; a true `1.0L` initializer
still fails with a focused diagnostic.

Decoder-driven oracles check width conversion, operand order, selected IEEE
patterns, quiet and signaling NaNs, unsigned boundary values, call alignment,
frame state, and the exact 80-bit load/store inventory. The static i386
runtime executes both call-result paths, both unprototyped argument paths, and
the variadic cursor transition. Exact section-byte contracts check static
initialization without executing target code. ADR 0076 records transport, ADR
0077 records default argument promotion, ADR 0079 records the first arithmetic
boundary, ADR 0091 records width conversion and mixed expressions, ADR 0125
records decimal scalar constants and integer conversion, ADR 0136 records
static floating constant data, ADR 0137 records comparisons, ADR 0147 records
static arithmetic, ADR 0170 records the unsigned-wide cast, and ADR 0196
records the automatic `long double` slice.

The self-host source frontier first closed five requirements from unchanged Toolchain code. Supported structure snapshots retain nested union bytes, and a scalar member can be loaded from a returned structure snapshot. A direct four-byte literal zero can form a represented null function pointer. An object pointer can convert to a signed or unsigned eight-byte integer with a zero high word, and conversion back keeps the low word. Compatible static character and void pointers accept an ordinary string literal through parentheses and macro expansion. At that boundary, top-level union values, aggregate members from structure rvalues, nonzero function-pointer casts, function-pointer and wide-integer conversions, and arithmetic or explicit casts on static string addresses remained open. ADR 0081 records that earlier language boundary.

The refreshed checked seed keeps represented function-pointer bits through a
cast to another function-pointer type or to and from a represented 32-bit
integer.
Object-pointer interchange and narrower or wider integer forms still fail
with a feature diagnostic. ADR 0113 records the current boundary.

The exact frontend gate checks the whole hosted source frontier at its real
i386 Linux ABI. It has 33 strict C11 roots and two GNU runtime roots.
`HOSTED_I386_LINUX` owns the 31 ordinary strict roots, which search only the
Toolchain tree and the angle-only hosted declarations. The GNU profile owns
the runtime implementation and its behavior probe.
`HOSTED_I386_KERNEL_BRIDGE` owns `kernel/lang/as_elf.cc` and its Toolchain
contract, the two roots that may also include `/kernel/lang`. The full set
contains the 19-source static tool union, runtime implementation and probe,
and all fourteen Toolchain contracts. The retired `HOSTED_TOOLCHAIN_64` and
`HOSTED_KERNEL_BRIDGE_64` profiles have no active roots.
Stage-two and stage-three CupidC compile every contract, CupidLD links each
static executable, and the harness rejects a byte difference in any of the
sixteen new objects or fifteen executables. The publisher validates a
dedicated `cupidc-contracts` target before work and again before promotion.
An existing destination must already verify as a complete cohort. Arbitrary
directories, source trees, files, and symbolic links are rejected without
modification. The initial contract snapshot, private copy, and newly discovered
live contract inventory must match in membership and hashes. This catches additions,
removals, and a transient edit copied before its live source is restored.
Every run derives the cohort from its requested executable, requires a named
manifest artifact, and verifies all artifact hashes, the live 45-input
contract set, the checked seed manifest, and the 41-file fixed-point source
inventory before execution. The contract set includes the Toolchain Makefile
and both Python modules that construct or verify the cohort. The seed manifest is read once for hashing,
decoding, schema validation, and build-plan use. A concurrent replacement
cannot mix those facts across reads.
The five-tool fixed point still contains 19 C sources because test programs
do not contribute to a tool image. ADRs 0082 through 0186 record the earlier
compiler, fixed-point, and seed boundaries. ADR 0195 records the runtime
contract rename, and ADR 0196 records the full contract transfer.

Host Python still coordinates the fixed point, and Windows still uses WSL to
execute the static i386 Linux tools. Native Windows and Python-free fixed
points remain open. The proposed 20 percent output-quality gate is disabled
because no approved cohort, metric, oracle producer, or same-revision oracle
artifact exists. Older Windows and Linux host `.text` measurements differ by
22.73 percent for the same revision, so neither is a safe default. Linker
capacity checks remain independent safety gates.

The repository now provides the next boundary as working code. CupidASM assembles i386 Linux startup and system-call wrappers. CupidC compiles a narrow runtime with a reusable heap, unbuffered files, standard streams, fixed-width integers, memory and string functions, `errno`, `getcwd`, and formatted diagnostics. Its checked surface now includes `printf`, `puts`, `snprintf`, `fputc`, `fputs`, `memmove`, and `strstr`. The formatter covers the 32-bit and 64-bit integer forms and bounded strings used by the unchanged Toolchain contracts. CupidLD joins that runtime with complete CupidC-emitted closures for CupidC, CupidASM, CupidDis, CupidLD, and CupidObj. The compiler driver handles compile-only C11 jobs, definitions, undefinitions, forced inputs, GNU and freestanding modes, and commit-gated compiler output. Ordered `-I` roots accept quoted and angle includes, while `--include-angle` roots accept angle includes only. Repeatable `-include` options run before the primary source in caller order. The resulting static commands run real positive and failure fixtures on Linux or through WSL. ADR 0086 records the runtime and sibling commands. ADR 0088 records the compiler driver and first compiler generation. ADR 0140 records the forced-input command boundary, ADR 0145 records the empty memory-barrier boundary, and ADR 0196 records the contract cohort.

The unchanged CupidLD command also exposed a valid `char **` to `char *const *` call that CupidC had rejected. The frontend now recognizes that immediate qualification addition, and Linear IR keeps it as a representation-preserving conversion. Qualifier removal and the unsafe `char **` to `const char **` conversion still fail. ADR 0087 records the type boundary.

In the older detailed sections below, open references to general floating
computation, comparison, truth testing, controlling expressions, or `_Bool`
conversion are superseded by ADRs 0079 through 0202. The limits in the current
summary remain open.

Each direct or indirect call instruction owns a packed, source-ordered slice of every actual post-conversion argument type. The shared IR validator requires those slices to form one complete partition in call-instruction order, rejects metadata on non-call instructions, and checks every packed type index. The i386 emitter uses the same validator before it reads a slice. A signed or unsigned eight-byte integer, an existing `double`, or a source `float` promoted to `double` at an ellipsis or unprototyped position occupies eight bytes in the shared outgoing area. An existing `long double` occupies twelve bytes at a fixed, ellipsis, or unprototyped position. ESP remains aligned to a sixteen-byte boundary before `CALL`. Four-byte integer and pointer transport is unchanged. Atomic and aggregate values remain outside the undeclared-parameter and variadic-read boundaries. ADRs 0075 through 0077 record the IR metadata, ABI rules, and floating promotion.

ADR 0196 extends the same checked argument metadata to represented automatic
`long double` values. A direct or indirect fixed, ellipsis, or unprototyped
argument occupies twelve bytes in the outgoing area. A function result crosses
the ABI in x87 `ST0`, then a direct or indirect caller stores it in a private
twelve-byte snapshot. A corresponding `va_arg(long double)` read copies twelve
bytes and advances the cursor to the next four-byte slot.

The verified hosted suites cover the complete frontend, Linear IR, and object surfaces, with each final count recorded in the chronological log. Focused contracts cover direct and indirect variadic and unprototyped calls, wide and floating values, all six floating comparisons, one-active-member union initializers, canonical function code generation attributes, Doom compatibility conversions, operand-bearing and operand-free assembly, empty memory barriers, pointer output, port I/O, privileged registers, FXSAVE, LDMXCSR, MOVSS, x87 sine memory, descriptor-table and segment transitions, call-next, GNU `Nd`, machine-state memory, the self-host source frontier, deterministic output, malformed metadata, constrained storage, and same-job recovery. Decoder and execution oracles check call alignment, x87 and cdecl stack balance, word order, arithmetic, width conversion, comparisons, structure snapshots, pointer bits, register preservation, cursor movement, preserved arguments, and restored frame state. The adapter gate fixes each function count, text size, object size, and text fingerprint. The tool link gate emits every closure object twice, repeats five command links and the runtime-contract link, and checks rollback and recovery. Public execution covers compilation, assembly, disassembly, linking, object wrapping, include resolution, mixed raw decode modes, missing files, runtime success paths, and useful failures.

The i386 Linux adapter objects are `ctool_host.cc` at 11 functions, 5,522 text bytes, 6,944 object bytes, fingerprint `28739C3F`, 25 symbols, and 38 relocations; `cupidasm_main.cc` at 13 functions, 9,455 text bytes, 12,384 object bytes, fingerprint `561BBC22`, 56 symbols, and 88 relocations; and `cupiddis_main.cc` at 13 functions, 13,816 text bytes, 17,420 object bytes, fingerprint `E33C130C`, 67 symbols, and 106 relocations. Their exact undefined import counts are 10, 31, and 31. Every relocation targets `.text` and has the checked `R_386_PC32/-4` or `R_386_32/0` shape. An independent `gcc -m32 -nostdinc` syntax pass accepts all three unchanged sources against the declarations.

The `ctool_host.cc` tracer applies 45 relocations, resolves 24 symbols, and leaves no undefined symbol in its static executable. Omitting the errno provider produces the exact CupidLD undefined-symbol failure with empty output and a zero result. The same job then links the original bytes again. Linux and WSL hosts with static i386 support run the tracer with exit status zero.

The current checked artifacts are CupidASM at 445,616 bytes, CupidDis at 379,648 bytes, CupidLD at 266,672 bytes, CupidObj at 253,724 bytes, and CupidC at 2,561,644 bytes. Verification checks every hash, size, static ELF property, target ABI, producer lineage, source revision, and build-plan field before execution. The 19-source plan uses `.cc` throughout and has SHA-256 `59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`. Native GCC and Clang recipes select C with `-x c`.

The promotion transition completes that plan through stage two and stage
three. All 19 C object pairs, the startup objects, and all five tool images
match byte for byte with host code-generator commands poisoned. Every
stage-two image matches stage three. CupidASM, CupidC, and CupidDis differ
from the preceding seed, while CupidLD and CupidObj remain byte-identical.
Both stages
agree on five help paths, ten successful operations, and six useful failures
across compilation, assembly, disassembly, symbol inspection, linking,
wrapping, and flattening. The 2,561,644-byte CupidC image carries the complete
83-root Doom frontier,
current GNU entity metadata, x87 and SSE forms, descriptor and segment
assembly, exact naked IPI entries, all file-scope effects in `libm.cc`, the
exact dglibc jump block, pointer-preserving static address casts, the
kernel-entry BSS clear with a nonzero page-aligned stack top, packed SSE2
statements, explicit `double` to `unsigned long long` conversion, and runtime
floating truth. Its
SHA-256 is
`a4dff3c1c8ae975e9b8278920d36aefe6ad9b28a52503a6d5d4253e04e4a21af`,
and its source revision is
`efec9c5f89358999a067a4a7c923d06d814d1639`. CupidASM and CupidDis carry the
592-row shared x86 catalogue with forward stack subtraction. CupidDis carries
typed raw code and data ranges,
and the 253,724-byte CupidObj image carries the complete installation-source
bounds, ordering, and linked-symbol contract. Its SHA-256 is
`f78752dc01daf3d2a9dc9265425f9c60639f438d5dcb91a001cf40d7d241ded5`.
[ADR 0208](../adr/0208-promote-forward-x87-subtraction.md) records the
current transition and promotion. The post-promotion rebuild reproduced all
five checked seed images at stage two and repeated the complete fixed point in
652.3 seconds. Its 15,053-byte report has SHA-256
`c8f52bb27b1be7a4e0a29c0353642d9ef13589013c39df839088da031473d810`.
The complete checked-seed module passes all 38 tests in 712.167 seconds.
`make verify-bootstrap-seed` checks the current inputs without running them.
`make bootstrap-from-seed` performs the complete staged build, while
`make test-toolchain-fixed-point` retains the native-generation oracle.
`make -C toolchain all` builds the checked i386 contract cohort without a
host C compiler. The complete 20-artifact cohort passes in 2,863.8 seconds,
with stage two and stage three matching byte for byte. Its 18,231-byte
manifest covers 45 inputs and has SHA-256
`27bcebb78404c8013bc56a3e2a0b9d7400cbfa040053863ed55d0d3131baaf33`.
GCC or Clang is used only by the explicit `native-oracles` and hosted
development targets.
ADR 0184 moves the 83 Doom roots out of host ownership.

The checked-seed bootstrap copies the exact bytes of its 41 source inputs into
a private compiler root before it starts either stage. CupidC receives that
root through `--root`, and both stage directories and the behavior workspace
stay below it. The harness rehashes the private closure and the live closure
before stage two, after each stage, and after behavior checks. A temporary live
edit that is restored during a compile cannot affect the captured input.
Stage two, stage three, behavior evidence, and the report are published as one
complete directory only after every gate succeeds. A failed run leaves an
absent or empty output unchanged, and a nonempty output is rejected without
modification. ADR 0142 records this trust boundary.

The strict non-Doom root cohort gives checked-seed CupidC ownership of 155
checked-in sources and the generated kernel symbol source. All 156 sources
use `.cc`. The 83 Doom roots bring the normal checked-in total to 238, also
with no ordinary C translation unit. The five shared Toolchain roots belong
to the 19-source i386 Linux fixed point, and native GCC or Clang rules select
C with `-x c` only for optional oracles. ADR 0124 records the first 111-root
transfer, and ADR 0126 records the complete fixed-point rename and old-seed
proof. ADR 0129 records the seed promotion and lexer transfer. ADR 0135
records the Nuked OPL3 transfer, ADR 0139 records the JPEG and glyph-raster
transfer, ADR 0167 records the FPU, per-CPU, and SMP transfer, ADR 0176
records the libm transfer, ADR 0180 records the kernel entry and SIMD
transfer, and ADR 0181 records the final strict-root transfer.

The checked seed now finalizes C11 inline meaning from the complete file-scope
declaration set. The ordinary declaration in `kernel/audio/nuked_opl3.h` and
the inline definition in `kernel/audio/nuked_opl3.cc` provide one external
function. Two complete kernel-profile compiles produce the same validated
40,424-byte object with SHA-256
`a3a04ade4029d9333902bb93376fb5eef21f349ee5a1406bd0751cc4cee9f2a1`.
The object defines `OPL3_Generate4Ch` and imports only `memset`. Its production
recipe freezes the source, three headers, wrapper, and checked seed inputs.
Poisoned-host coverage rejects fallback to GCC or Clang. An earlier `static`
declaration keeps a later `extern inline` definition internal. An
external-linkage inline declaration without a definition fails during
translation-unit finalization. ADR 0131 records the language boundary, ADR
0134 records the seed promotion, and ADR 0135 records the production transfer.

The wrapper freezes and verifies the seed before each compile and publishes
only a validated i386 ELF32 object. A valid data-only object no longer needs a
`.text` section, but its section and symbol ranges must still pass the shared
validator. The production frontier covers 155 approved sources, and every
Make recipe names its recursive header closure and common checked controls.
Forced rebuilds poison the host compiler. The complete frontier now compiles
all 155 roots twice against a 445-file snapshot with SHA-256
`4b4dbd802d8faf0cdf9bc1b2749ab7cddf4c4635dafdea4ac171c37a96449a92`.
Both object passes are byte-identical; each totals 3,721,392 bytes. The combined
frontier retries only short permission-style directory locks with five
bounded delays; persistent locks and other filesystem errors publish nothing.
Its input inventory skips hidden paths under active include roots, so a
concurrent checked compile cannot add private staging headers to the frozen
repository snapshot.
The kernel-entry recipe freezes 63 recursively included headers, and the SIMD
recipe freezes seven. Poisoning `CC` leaves both recipes on the checked
wrapper. Their validated objects are 25,920 and 8,768 bytes, respectively,
and CupidDis accepts both as i386 ELF32 relocatables.
The 155-root graph passes the two-link symbol and memory checks plus clean
normal and partitioned image builds. Strong four-vCPU runtime checks pass in
235.259 seconds with e1000 and 232.832 seconds with RTL8139. Both runs cover
the promoted FPU and SMP paths, RDRAND, all 62
crypto checks, USB storage, the desktop, terminal, audio output, TrueType glyph
use, an exact baseline JPEG decode, the 29-check libm guest probe with seven
focused x87 range-reduction cases, and in-OS CupidC execution.

The pass-one kernel feeds CupidDis symbol output into
`kernel/cpu/ksyms_data.cc`. The generator stores the byte-exact symbol blob
as little-endian i386 words, then records its logical length separately. This
encoding replaced an earlier 638,361-byte source. The current source is
352,269 bytes with SHA-256
`3e0c8730304b4db6e5636ee6358a6e3bf2673434044604f3340224e608c81f86`.
Generation uses
private byte snapshots of the pass-one kernel and CupidDis. It rejects
malformed rows, missing text symbols, i386 address overflow, and live input
drift before it atomically replaces the source. The compiler wrapper freezes
that source and its complete header closure, gives this generated root a
separate 600-second ceiling, validates the relocatable object, rejects input
drift, and publishes atomically.

Three generated installation tables have also left the host C compiler. The
checked CupidObj seed generates the ramfs program table, homefs document
table, and CupidASM demo table as `.cc` sources. Checked CupidC compiles them.
A separate closed wrapper compiles `hello.cc`, `ls.cc`, and `cat.cc` under the
fixed user profile, and a CupidLD wrapper links them into the two-MiB external
arena.
Linux runs the checked seed directly, while Windows runs it through WSL. The
normal user build does not prepare native hosted drivers. Before compilation,
a separate verifier compares the public header with the kernel types, syscall
table and initializer, VFS declarations, and socket constants. It captures the
exact bytes of those six declarations and rechecks them before success. It
pins version 5, all 103 table fields and 101 function providers, the 412-byte
table, and the shared i386 record layouts. Both wrappers freeze their source
and control inputs, validate ELF output, and replace an artifact only after
the full operation succeeds. The 23-input default frontier repeats all six
builds from the seed. An explicit 46-input Windows frontier also runs private
native CupidC and CupidLD snapshots and requires every output to match. A
poisoned-path test rules out GCC, Clang, `ld`, and `cc` on the normal path.
The local `user/build/` directory is generated and ignored by Git. ADR 0127
records the ABI correction and gate. ADR 0130 records the optional native
Windows path. ADR 0188 records the checked-seed default.

CupidObj generates those three installation-table sources through one typed
public operation and the `install-source` CLI. The bin, docs, and demos modes
validate their own path categories, reject duplicate or mixed lists, cap the
combined inventory at 512 paths with overflow-safe accumulation, preserve
caller order across mixed home-asset extensions, and rewind partial output on
failure. The checked seed, source head, and Python oracle carry the limit and
ordering corrections. The active inventories already fit the limit and retain
byte-identical order.
The checked seed, source head, and Python oracle also compare the complete
wrapped symbol domain. They reject distinct paths that normalize to one linked
symbol,
including the bin and browser prefix overlap. One exact BMP path may remain in
both the docs and home lists because both entries use the same object. Every
normal installation-table recipe now enforces this guard.
The normal Make recipes invoke the checked command and depend on
`$(CUPIDOBJ_INPUTS)`.
`tools/hostbuild.py` remains the parity oracle but is no longer a prerequisite
or recipe owner for these outputs. GNU Make's ordinal inventories reproduce
the pre-transfer files and the oracle exactly:

| Table | Bytes | SHA-256 |
| --- | ---: | --- |
| bin | 46,335 | `c79edeeaf909d6c204690acd31dd56ca91be4f65ed148fa8e5e9768ac8dc1d8f` |
| docs | 9,794 | `cff3fc8943d4b1999869653b14a882d21a463471452e429b2d742d47107b13fc` |
| demos | 12,845 | `0d1f7ee032b13abbbe1767d75fe32c6f1ffa8b7014db44ae35c9d4c47ebb8305` |

The private five-tool bootstrap reached a fixed point with the 253,724-byte
CupidObj image. ADR 0201 records the operation, ADR 0204 records production
ownership, ADR 0206 records the linked-symbol contract, and ADR 0208 records
the current seed.

The active-source audit classifies all three recipes as
`generate_install_source` with `cupid_object` and `host_python`. Its exact
delivery guard checks each target, mode-specific recipe, inventory markers,
and the complete checked-seed input set. Its focused positive and negative
coverage passes in 0.222 seconds, including substituted inventories and shell
text that only resembles a command. The full CupidC production module passes
all 39 tests in 25.698 seconds, and the 195-input generated frontier passes in
33.0 seconds with digest
`3819e76ac204f4b2203abe4f34539986bc3a3f68e3a60403aa09ce29f911d799`.
The full 68-test build-graph audit passes in 613.397 seconds. A normal root
build passes in 1,452.910 seconds. It produces an 8,719,780-byte kernel ELF
with SHA-256
`5a7a491a39372697accff9b678054b4bf84e2e68ffc3e882c5ef815d570cee06`
and an 8,518,280-byte raw kernel with SHA-256
`ecde61e586fb69bf091e3586c7c0a90d65588a9d7aa22ea6cf7d2f48dc341df3`.
The resulting 209,715,200-byte image has SHA-256
`f488f54c023e6d1f7e9883be1f93f705fbdab4b1de3aab8a2b61b86f3863a085`.
A private-image `/bin/ls.cc` JIT smoke passes in 54.025 seconds. Its
27,839-byte serial log has SHA-256
`631670b29e91ffe195e343a3cb957e995776b9860efb441f51ffdee4d443d55f`
and contains no panic marker.

The external syscall table records `print`, `print_int`, and `exit` events
with the running PID before using the normal console or process path. A print
event carries only its byte count and FNV-1a fingerprint, so newline or
marker-shaped caller text cannot create a second serial event. Kernel and JIT
callers still use their existing paths. Separate hello, ls, and cat boots
use private copies of one staged image and check numeric output and a
root-directory read. The cat boot also copies a fixed FAT fixture over
`/home/readme.txt` before launch, so the program keeps its normal HomeFS path
and the selected image stays unchanged. Every boot requires PID-matched
process completion.

The larger unoptimized objects need more kernel address space. The kernel
ceiling is `0x00F00000`, and the two-MiB stack occupies
`[0x00F00000, 0x01100000)`. CupidC keeps nine MiB at
`[0x01100000, 0x01A00000)`, and CupidASM keeps two MiB at
`[0x01A00000, 0x01C00000)`. The two-MiB external ELF lease now follows at
`[0x01C00000, 0x01E00000)`. FAT16 starts at LBA 20480, so the BIOS loader
may read the kernel through LBA 20479. No active runtime arena was reduced.

The renamed graph starts every discovered CPU on the four-vCPU GUI gate,
seeds the CSPRNG through RDRAND, passes all 62 crypto, ASN.1, and X.509
checks, reaches the desktop and terminal, initializes e1000, and completes
in-OS CupidC execution at `0x01100000`. The dual-NIC, audio, input
reattachment, and six EHCI lifetime gates remain part of the contract. An
isolated image gate also
loads the same external ELF program twice at `0x01C00000` and releases the
first lease before reuse.

Python still launches the compiler. Windows uses WSL for the root,
generated-table, and external-program checked-seed paths. The optional native
user drivers remain host-built until CupidC gains a Windows runtime and PE or
COFF output. ADR 0110 records the earlier 40-source handoff. ADR 0111 records
the 116-source expansion, data-only object rule, and memory map.
ADR 0112 records the generated-table and external-program handoff. ADR 0113
records the compiler-head source frontier. ADR 0115 records its first
production transfer and source rename. ADR 0123 records the eight-root and
generated-symbol transfer. ADR 0124 records the 111-root naming transfer and
the five deferred shared roots. ADR 0125 records decimal floating scalars,
ADR 0126 records the complete fixed-point rename, ADR 0130 records the
optional native Windows user path, and ADR 0188 records the checked-seed
default. ADR 0133 records the live ABI input check and private guest images.

The checked seed accepts GNU `used` and `__used__` on file-scope objects and
functions. Compatible redeclarations merge the flag into the canonical
entity, and the Linear IR and object boundaries validate the frozen metadata.
The current emitter already writes every represented definition, so the
attribute does not change ELF32 bytes. A hermetic object contract reproduces
the generated kernel-symbol declaration with `section(".ksyms")`, `used`, and
four-byte alignment. The normal build now compiles
`kernel/cpu/ksyms_data.cc` through the checked wrapper. The current generated
symbol blob contains 109,889 meaningful bytes followed by three zero padding
bytes. Its 110,304-byte ELF32 object has SHA-256
`45e77aff292df2d47ac7b9c2004371fa767ed511df066238a2e3299c9a9d08c2`.
ADR 0116 records the language boundary, ADR 0122 records the seed refresh,
and ADR 0123 records the production transfer.

The checked seed accepts the exact volatile
`call 1f\n1: popl %0` statement in the stack-trace helpers. It requires one
modifiable four-byte integer `=r` output and no inputs or clobbers. Linear IR
evaluates the destination once. The emitter uses the shared x86 model to
write `E8 00 00 00 00` followed by `POP r32`, so the captured value is the
address of the pop and ESP is restored without a relocation.

The `kernel/lang/as.cc` and `kernel/lang/cupidc.cc` roots now compile
twice under the complete kernel profile to byte-identical validated i386
ELF32 objects. Their sizes are 148,056 and 288,180 bytes. Their normal object
recipes use this checked path. ADR 0118 records the language boundary, and
ADR 0123 records the production handoff.

The checked seed accepts operand-free GNU assembly in function bodies. Basic
statements and extended statements with an empty output list own an empty
operand slice and are implicitly volatile. The i386 emitter handles exact
sequences of PAUSE, NOP, STI, HLT, CLI, CLD, SFENCE, and FNINIT without a
temporary frame slot or EBX traffic. The refreshed checked seed compiles the
unchanged `e1000.cc`, `desktop.cc`, `socket.cc`, and `tcp.cc` sources in the
normal build. ADR 0099 records the language boundary, ADR 0102 records the
seed transition, and ADR 0104 records the production hand-off. The earlier
detached hybrid build proved the same four objects through both CupidLD
passes, CupidObj, and a GUI boot before the Make graph changed owner.

The checked seed retains an exact empty volatile extended template with one
`memory` clobber and no operands. Linear IR keeps the statement as an ordering
point, while the i386 emitter writes no target bytes for it. This compiles the
unchanged Doom sound driver through its production CupidC recipe. ADR 0145
records the language boundary, and ADR 0184 records the ownership transfer.

The checked seed accepts the exact width-aware port-I/O assembly in
unchanged `kernel/core/ports.h`. The six scalar helpers use AL, AX, or EAX
with DX. The word-string helpers retain modifiable EDI or ESI pointers and an
ECX count, write both final values back, and restore the callee-saved string
register. INSW accepts the source's one `memory` clobber. The exact instruction
sequences are `EC`, `EE`, `66 ED`, `66 EF`, `ED`, `EF`, `FC F3 66 6D`, and
`FC F3 66 6F`. ADR 0105 records the represented boundary, ADR 0106 records
the checked-seed refresh, and ADR 0110 records its use by the 14-source
production handoff.

The checked seed accepts the exact GNU `Nd` constraint and byte templates in
`kernel/cpu/pic.cc`. It chooses the valid `d` alternative, loads
the 16-bit port into DX, and emits `EE` for `outb %0, %1` or `EC` for
`inb %1, %0`. The focused frontend, IR, and object contracts cover malformed
constraints, invalid types, duplicate EDX ownership, forged metadata,
partial templates, deterministic output, rollback, and recovery. The full
kernel profile produces a 2,408-byte object with SHA-256
`c1855a19e0cd285953996344493dcefe916f06d89fed706219718920b4d2ea5d`.
The normal recipe now uses this object. ADR 0120 records the language
boundary, and ADR 0123 records the production handoff.

The next compiler-head slice accepts one modifiable four-byte object or
`void` pointer as the `=r` output of the exact per-CPU template
`mov %%gs:0, %0`. The frontend and IR retain its pointer type and evaluate the
destination once. The emitter assigns EAX and produces the six-byte absolute
GS load through the shared x86 model. ADR 0100 records this boundary.

The checked seed represents the privileged-register statements in
`kernel/cpu/idt.cc`, `kernel/mm/paging.cc`, and
`kernel/smp/lapic.cc`. Independent `r` inputs accept represented four-byte
integers and data pointers. Independent `c` inputs accept represented
four-byte integers and reserve ECX. Exact CR0, CR2, CR3, and CR4 moves and
RDMSR emit without a host assembler or an EBX scratch slot. Frontend, Linear
IR, and object contracts reject unsupported widths, types, fixed-register
collisions, directions, and clobbers without publishing partial state. The
three strict roots compile twice to byte-identical validated objects, and
their normal recipes use those objects. ADR 0117 records the language and
object evidence. ADR 0123 records the production handoff.

The checked seed accepts the independent `r` pointer input used by both
FXSAVE statements in `kernel/core/process.cc`. The exact volatile
template `fxsave (%0)` requires one four-byte object or `void` pointer and one
`memory` clobber. Linear IR consumes the pointer once, and the emitter uses
the shared x86 model to produce `0F AE 00` at `[EAX]` without a temporary
frame slot. A two-function fixture produces a deterministic 396-byte ELF32
object with 40 bytes of text and no relocations. The complete process source
also compiles twice under the fixed kernel profile to matching validated
30,216-byte objects. The normal process recipe uses this path. ADR 0119
records the language and object boundary, and ADR 0123 records the handoff.

The checked seed carries the SMP integer atomics that follow that load.
`__atomic_load_n`, `__atomic_store_n`, `__atomic_exchange_n`, and
`__atomic_fetch_add` accept represented one-, two-, and four-byte integer
objects with checked constant memory orders. It also accepts
`__atomic_fetch_or` at those widths. Ordinary loads and release stores use
width-correct `MOV`; sequentially consistent stores and exchanges use memory
`XCHG`; fetch-add uses `LOCK XADD`; fetch-or uses a `LOCK CMPXCHG` retry loop
that returns the old value and preserves EBX. The six order macros are
reserved target predefines in every language mode; the expressions remain
GNU-only. A decoded i386 oracle checks results, memory updates, narrow
signedness, wraparound, cdecl state, one-time operand evaluation, and a
forced competing update during fetch-or. Runtime order arguments, pointer
atomics, HLE flags, and eight-byte atomics remain open. The checked seed
carries all five operations and compiles the active EHCI fetch-or path. ADR
0107 records the language and emitter boundary; ADR 0108 records its staged
seed promotion.

USB reconciliation keeps work durable when enumeration fails or a
callback fills the queue while the poller is handling another item. One poll
attempt visits each item at most once, rotates deferred work behind its
peers, and backs retries off from 10 milliseconds to 1 second. Device
addresses from 1 through 127 and vacant block-device slots are reusable.
Failures after address assignment quarantine that address until reset,
disconnect, or stale hub work proves it safe to release.
The core marks the next root reset as mandatory while a quarantine exists.
EHCI only bypasses reset for low-speed K-state with no quarantined address;
J-state can still identify a high-speed-capable device. The controller checks
reset assertion, reset clearing, and companion ownership before reporting a
handoff that releases a quarantine.

Hub callbacks only queue observed changes. The core owns child teardown,
reset, enumeration, change acknowledgement, and the reread that catches a
new edge arriving during acknowledgement. EHCI and UHCI interrupt
registrations use controller-local generations and in-flight state.
Cancellation waits for the exact generation's callback and DMA access to
finish, while callbacks run outside the controller lock. Compiled C fixtures
exercise reconciliation, address reuse, block-slot reuse, callback reentry,
generation cancellation, and DMA quiescence. Block references reject
saturation without wrapping. A failed mass-storage unregister restores the
attached online state for another removal attempt. All 44 USB tests and all
62 GUI gate unit tests pass. The e1000 and RTL8139 runtime gates both pass
UHCI input
reattachment and six EHCI storage lifetimes. ADR 0109 records these lifetime
and ownership rules.

The non-Doom header gate is now 155/155 in the checked seed. Checked-seed CupidC
still owns unchanged `kernel/smp/acpi.cc` and
`kernel/smp/mp_tables.cc` in the normal build. A four-vCPU QEMU run discovers
and starts every CPU, initializes e1000, passes all 62 crypto, ASN.1, and
X.509 checks, reaches the desktop and terminal, and completes `/bin/ls.cc`.
The optional runtime contract checks those markers together and rejects the
known SMP, storage, crypto, exception, panic, corruption, and
illegal-instruction failures. ADR 0101 records the atomic boundary, ADR 0102
records the seed transition, and ADR 0103 records the production cutover.

The first unoptimized CupidC cohort crossed the original stack boundary and
established the fixed stack and external arenas. The 116-source expansion
moves that layout upward by one MiB as described above. ADR 0093 records the
original ownership and memory-map decision, ADR 0098 records the complete
crypto cutover, ADR 0111 records the expansion, and ADR 0115 records the
current boundary.

The wide arithmetic proof now has 26 functions and 165 exact IR instructions. Its original 83-instruction prefix keeps fingerprint `245E6D8F4F77588E`. Five multiplication slices cover signed, unsigned, mixed-sign, narrow-to-wide, and chained products, while seven later slices cover signed and unsigned quotient and remainder, mixed signedness, a widened narrow divisor, and chaining. The earlier combined operation object still contains 3,156 text bytes with fingerprint `B52392EA`, 26 symbols including the null symbol, and no relocations. A separate multiplication object contains 1,103 text bytes with fingerprint `E357BE84`, seven symbols including the null symbol, and no relocations. Its decoder finds seven one-operand `MUL`, fourteen two-operand `IMUL`, six returns, and no call or divide. The multiplication oracle covers zero, identity, low-word carry, cross-word contribution, high-bit wrap, defined signed cases, mixed signedness, narrow conversion, fresh snapshots, restored stack and frame state, preserved registers, and unchanged arguments. Malformed multiplication metadata and constrained output fail transactionally and recover in the same job.

The wide-mutation fixture publishes 15 functions and 225 exact IR instructions. Its deterministic object contains 17 functions in 4,410 text bytes, has fingerprint `4B337038`, publishes 18 symbols including the null symbol, and has no relocations. Decoder and execution checks cover all ten compound operators, signed and unsigned prefix or postfix update, mixed and narrow conversion, postfix snapshot preservation, one-time indexed evaluation, volatile access, and cdecl state. Malformed metadata, atomic wide mutation, and constrained output fail without publishing partial work, and the same job reproduces the complete object.

The division and remainder object contains eleven functions, 4,775 text bytes with fingerprint `55F1A495`, twelve symbols including the null symbol, and no relocations. Thirteen operation loops each carry a fixed branch shape through unsigned high- and low-word comparison, shared subtraction, and a repeat edge. Thirty-three defined execution checks cover all four signed sign combinations, unsigned low- and high-word operands, high-bit divisors, mixed and narrow conversions, chaining, snapshot reuse, stack restoration, preserved registers, and unchanged arguments. Invalid quotient/remainder metadata, constrained output, and same-job recovery stay transactional. Undefined runtime inputs are outside the oracle. This proof changes no production owner.

The wide switch proof covers signed and unsigned controlling values, exact full-width cases, and misses that differ in only one word. Its deterministic object contains two 252-byte functions, 504 text bytes with fingerprint `DBC82148`, three symbols including the null symbol, and no relocations. The execution oracle checks the selected return, stack and frame state, preserved registers, and unchanged two-word argument. A mismatched case type, an unpromoted narrow condition, and constrained output fail transactionally. The same job can emit the exact object afterward. This proof changes no production owner.

Hosted CupidC now lowers plain assignment, all ten compound assignments, and prefix or postfix increment and decrement for represented non-atomic bit fields in four-byte storage units. Linear IR retains the graph member and evaluates the record address once. `BIT_FIELD_STORE_VALUE` returns the stored lane for assignment, compound assignment, and prefix update. `BIT_FIELD_STORE_OLD_VALUE` carries the extracted value through a postfix store. Partial fields preserve neighboring bits; a volatile 32-bit field uses one read and one direct store. Partial volatile mutation and other storage sizes remain open. ADR 0063 records plain assignment, and ADR 0064 records mutation.

Hosted CupidC carries complete fixed-size structure values through lvalue conversion, automatic expression initialization, plain and chained assignment, conditional expressions, casts to `void`, fixed direct and indirect calls, and return. One Linear IR stack entry represents an emitter-owned snapshot of the complete target bytes. `LOAD` creates that snapshot, `STORE` copies it without a result, and `STORE_VALUE` preserves it after the copy. Ordinary locals keep their binding-ordered frame slots; structure loads and structure-result calls receive private frame slots in instruction order. Supported structures have target alignment no greater than four bytes and no volatile or atomic subobject. Their copied graph may contain a union, and a scalar member can be loaded from an owned structure-result snapshot. A union used directly as a parameter or result and an aggregate member selected from a structure rvalue remain open. ADR 0049 records the value model and i386 ABI, and ADR 0081 records the nested-union and rvalue-member boundary.

The i386 call path places each structure argument inline and rounds its stack area up to four bytes. Callers zero the outgoing area before filling scalar slots and copying structure bytes, so a three-byte structure has one deterministic padding byte. A structure result uses a hidden destination pointer at `[EBP+8]`; explicit parameters start at `[EBP+12]`. The callee copies the result, returns the hidden pointer in EAX, and removes that word with `RET 4`. Structure copies preserve ESI and EDI around `CLD` and `REP MOVSB`. The deterministic proof has 928 `.text` bytes, 13 symbols, four `R_386_PC32` relocations, and FNV fingerprint `31D58B50`. The shared x86 contract, CupidASM, and CupidDis agree that `C2 04 00` is `ret 0x4`; CupidASM rejects `ret 65536`.

Hosted i386 object emission aligns ESP to sixteen bytes immediately before every direct or indirect `CALL`. A target-private control-flow pass derives the live semantic stack depth at each reachable instruction without changing public Linear IR. The emitter combines that depth with the fixed frame and any outgoing structure or wide argument area, then reserves zero, four, eight, or twelve padding bytes. Scalar calls shift completed argument words into the padded area, while structure and wide calls copy each value into its target-sized slot. The focused proof covers all four padding amounts, nested evaluation, a conditional join, a loop back edge, direct and indirect calls, structure arguments, wide arguments, and hidden structure results. Its control-flow decoder checks every reachable call, while execution or symbolic oracles verify argument values. ADR 0050 records the alignment rule, and ADR 0067 records wide slots.

Block-static objects now reach deterministic hosted ELF32 output. The lowerer validates each constant initializer and publishes no runtime initialization instructions. The emitter gives every object a local `.LBS<absolute-block-binding-index>.<source-name>` symbol, uses the same `.rodata`, `.data`, or `.bss` rules as file objects, and emits runtime addresses through `R_386_32`. Shadowed names remain distinct, unused and unreachable objects still receive storage, and no block static consumes an EBP-relative frame slot. ADR 0051 records this boundary.

Hosted CupidC lowers automatic initializer lists for complete fixed arrays, structures, and one-active-member unions in the ADR 0044 frame boundary. `ZERO_OBJECT` semantically initializes the complete object once. Explicit represented leaves then run in source order and store through checked `MEMBER_ADDRESS` and `ELEMENT_ADDRESS` paths, including nested direct designators. A union list owns one edge: its positional clause selects the first eligible member, or a direct `.member` designator selects that member. Supported structure and eight-byte integer leaves use byte-copy `STORE`, while a narrow character-array string leaf uses `COPY_STRING` to copy the exact frontend-retained bytes after the enclosing object has been zeroed. The i386 emitter preserves EDI, issues `CLD`, and uses `REP STOSB` for zeroing and `REP MOVSB` for copies. Unchanged declarations in `toolchain/cupidc_pp.cc`, `toolchain/cupidc_frontend.cc`, `drivers/serial.cc`, and Doom's `info.cc` guard the active initializer shapes. Repeated union-member overrides, explicit bit-field initializer leaves, volatile or atomic aggregate subobjects, and floating scalar expression leaves remain deferred. ADR 0048 records the original list design, ADR 0053 records runtime narrow strings, ADR 0066 records wide leaves, and ADR 0153 records union selection.

Block-scope compound literals use the same initializer paths. The frontend gives each source site one absolute expression identity and lets that expression own its initializer root. Linear IR initializes the object at each evaluation and returns its address as an lvalue. The i386 emitter reuses one persistent frame slot for the source site. Aggregate lists use a second staging slot and commit the complete object only after every initializer read has finished. A narrow string root zeros its persistent character array and copies the retained literal bytes directly. The exact `(ctool_string_t){literal, size}` call in `toolchain/cupidc_pp.cc` and the focused `(char[]){"Cupid"}` case now parse, lower, and emit without handwritten temporaries. The audit records 40 compound-literal occurrences across four active files. Static-duration literals, variable-length literal objects, and the related named-aggregate backward-jump alias case remain deferred. ADR 0052 records object identity and lifetime, while ADR 0053 extends the runtime initializer boundary.

Ordinary narrow string expressions now cross hosted IR through `STRING_LITERAL_ADDRESS`. The instruction retains the absolute frontend expression identity. The i386 emitter gives each use a local `.LCn` object in `.rodata` and emits an `R_386_32` text relocation with addend zero. This covers array decay in pointer initializers, arguments, indexing, and returns without assigning a host address to the frozen translation unit. Literal pooling and wide strings remain outside this slice.

Hosted CupidC lowers explicit casts to `void` after evaluating the operand once. A represented integer, object pointer, function pointer, supported structure, or floating scalar produces one typed `DISCARD`. A `void` operand leaves the abstract stack unchanged, so the cast publishes no discard instruction. The complete unchanged `ctool_host_allocate` and `ctool_host_release` helpers pin the active `(void)context` and `(void)bytes` uses. Their two functions publish 18 IR instructions, including three discards and two direct calls. A focused 52-byte object has three symbols and one `R_386_PC32` relocation to `sink` at text offset 43 with addend `-4`. An eight-byte integer constant, supported call result, or lvalue can also be discarded. A transported `double` call result or lvalue follows the same rule. A wide integer or `double` lvalue is read into its private snapshot before the handle is removed. Unions, Cupid classes, atomic operands, and floating expressions that require unsupported computation still fail before discard. Represented function pointers may cast to another function-pointer type or to and from a represented 32-bit integer. Object-pointer interchange and narrower or wider integer forms remain unsupported. ADR 0047 records the scalar discard rule, ADR 0049 extends it to structures, ADRs 0065 and 0066 extend it to supported wide values and lvalues, ADR 0076 adds floating transport, and ADR 0113 records the represented function-pointer casts.

The shared frontend carries compatible structure and union values through plain assignment, return, automatic expression initialization, fixed arguments, and matching-record conditional expressions. It retains automatic, block-static, and file-scope array, structure, or one-active-member union initializer lists with direct C11 member and array designators. Selectors stay in source order, positional initialization resumes after the selected subobject, and a direct unknown-bound array uses its greatest selected index plus one. A positional union clause selects the first eligible member; a direct member designator may select another member. Brace-elided children return a following designator to the nearest explicit list. The audit finds 646 direct active-source designators across 19 files. The contract includes the sparse 134-byte ELF table from the CupidASM kernel object test, all seven 35-member definitions in unchanged `kernel/gui/gui_themes.cc`, and the active-member shape in unchanged Doom `info.cc`. Chained designators, names promoted through anonymous members, duplicate overrides, multiple union-member clauses, and Cupid class lists remain deferred.

The production in-kernel CupidC emitter keeps tagged loop and switch control frames. `break` selects the nearest frame and removes a saved switch selector before leaving that switch. `continue` scans outward to the nearest loop, removes every crossed switch selector, and then uses the loop's established target. `while` reaches its condition, `do` reaches its patched condition trampoline, and `for` reaches its iteration expression. The parser accepts 128 active control frames and 1,024 active statement calls. The next entry fails before further recursion with `control nesting too deep` or `statement nesting too deep`. REPL rollback restores both counters. The statement dispatcher has a four-byte checked CupidC frame; its token-heavy nonrecursive work no longer stays live while a nested statement is parsed. `/bin/feature25.cc` runs all three loop forms, nested switches, a nearest-inner-loop case, 600,000-iteration cleanup paths for both jump kinds, both accepted depth boundaries, useful overflow failures, and a fresh evaluation after each failure. The original marker remains `[feature25] PASS do=1 for=1 while=1 stack=1 reject=1 nearest=1`. The added marker is `[feature25-depth] PASS control=1 overflow=1 recovery=1 statement=1 statement-overflow=1 statement-recovery=1`. ADR 0078 records the control semantics, and ADR 0128 records the parser-stack hardening.

The shared frontend independently publishes typed C11 control statements. `break` and `continue` remain targetless there. Hosted IR lowering binds `break` to the nearest loop or switch and binds `continue` to the nearest loop. A `while` continuation reaches its condition, a `do` continuation reaches its post-test condition, and a `for` continuation reaches its iteration expression when present or its condition otherwise. A switch between `continue` and its loop does not become a continuation target.

Unchanged `break` and `continue` statements in `cir_validate_initializer_ownership` in `toolchain/cupidc_ir.cc` guard the active requirement. Two break functions have eight exact IR instructions, including an unconditional `do` break that skips the condition. Six continuation and nesting functions add 47 instructions and check each loop form, a `for` loop without an iteration expression, and nearest-loop binding. Private patch tags resolve deferred `do` and `for` targets during lowering and do not appear in published IR.

Block bindings, compound-literal expressions, and file object definitions own one job-owned semantic initializer forest. Automatic scalars and whole records use `EXPRESSION` roots. Automatic aggregate lists use `LIST` roots with runtime `EXPRESSION` leaves, while character arrays use `STRING`. Supported static objects use `ZERO`, target-converted `INTEGER`, `STRING`, string `ADDRESS`, an `ADDRESS` of a linked file object or function, or recursive `LIST` records. An explicit static null pointer constant uses a destination-typed `ZERO` record, including when it is a child of an array or structure list. A binding address names a linked file object or function and may carry a checked signed i386 target-byte addend. Pointer addition, subtraction, and subscripts derive that addend from the existing integer constant-expression value and the target referent layout. List edges name explicitly initialized direct array elements or structure members, nested roots are postorder, and omitted subobjects remain implicit zero. A direct unknown-bound array is completed on a private object type, leaving a shared incomplete typedef unchanged. Freeze derives storage duration from the owning definition, binding, or compound expression, accepts explicit `ZERO` children only in static forests, and rejects runtime leaves there. The forest itself does not serialize a target image. The object emitter consumes static roots owned by file definitions and block-static bindings, while automatic roots lower into runtime stores.

File-scope object definitions live in a table separate from canonical bindings. The binding keeps first-declaration facts, while the definition keeps its type, storage, source location, explicit or tentative kind, and initializer root. Repeated tentative declarations coalesce as they are parsed. Translation-unit finalization applies the merged binding type and supplies a zero root. An incomplete external array becomes a one-element array, while incomplete internal arrays and records fail precisely. The focused contract publishes 29 definitions, 39 initializer records, and nine list edges. It covers repeated and superseded tentative declarations, object addresses, array and function decay, array-element and one-past addresses, pointer subtraction, integer-first addition, `&numbers[1] + -1`, nonzero qualification and object-to-`void` conversions, both sides of the signed i386 addend boundary, unevaluated pointer arithmetic inside `sizeof`, unresolved external references, and mixed address leaves without inventing host pointers or ELF relocation records. Unchanged `kernel/fs/ramfs.cc` proves the string and all 11 function addresses in its operations table.

The hosted object path accepts represented file and block-static data plus a growing function subset in one deterministic i386 ELF32 relocatable object. Public `ctool_c_lower_ir` publishes a contiguous typed instruction slice for each supported function, with function-relative branches, absolute frontend identities, and retained source locations. The current ABI slice covers prototyped cdecl functions plus zero-parameter definitions written with an empty identifier list. Results may be `void`, represented integer or pointer scalars, same-kind `float` or `double` values, supported eight-byte integers, or supported structures. Declared parameters may use those same represented values or supported structures. Fixed calls, variadic calls, and direct or indirect calls without a prototype reach the same path. Values without declared parameter types may be represented four-byte integers or pointers, signed or unsigned eight-byte integers, or values already typed as `double`. The transport set also includes same-kind `float` and `double`; it does not yet include floating computation or value-producing conversion. Default promotions make an undeclared narrow integer a four-byte value before lowering. An eight-byte integer crosses the ADR 0065 result path, ADR 0066 object path, ADR 0067 parameter path, ADR 0068 operation path, ADR 0072 multiplication path, and ADR 0075 variadic path through one private snapshot handle. These paths include the same-rank signed-to-unsigned usual arithmetic conversion and GNU wide-enum promotion to its exact compatible type. A structure also occupies one abstract IR value while the emitter owns its target-sized snapshot and ABI area.

The path lowers parameter, automatic-local, block-static, and linked file-object loads; structure snapshots and copies; object-pointer dereference and address-of; function decay, address-of, and dereference; direct ordinary members reached through file objects or object pointers; direct reads from four-byte integer bit fields; structurally compatible pointer conversions and null-pointer conversions; represented integer promotions and conversions; explicit casts among represented one-byte, two-byte, and four-byte integers plus same-width casts between four-byte integers and object pointers; constants and enumerator identifiers; all four 32-bit integer unary operators; addition, subtraction, multiplication, signed and unsigned division and remainder, bitwise AND, OR, and XOR, and 32-bit left and right shifts; integer and pointer equality and inequality, plus all four signed or unsigned object-pointer relational comparisons; short-circuit logical AND and logical OR; scalar and matching-structure conditional expressions; statement-level `if` with optional `else`; pre-test `while`, post-test `do`, and `for` loops; 32-bit integer `switch`, `case`, and `default`; nearest-control `break`; nearest-loop `continue`; direct identifier labels and `goto`; multiple returns; represented declarations in supported compound statements and `for` initializers; direct and indirect calls; initializer stores; value-preserving plain assignment; all ten compound assignments plus prefix and postfix increment and decrement for represented non-Boolean one-byte, two-byte, four-byte, and eight-byte integers; discarded nonvoid values; explicit casts to `void` for represented scalar, structure, and `void` operands; and value or void return.

Direct `goto` uses the frontend's canonical function-scope label table. A fixed-point pass marks only labels reached from the function entry, so a dead jump after a return cannot revive its target. Forward jumps use a private patch tag that is cleared before IR is published, while backward jumps receive their target immediately. The direct contracts cover forward and backward jumps, cycles, nested compound and `if` targets, loop exit, entry before `break` and `continue` in an otherwise unreachable infinite loop, a terminal `do` body, and a declaration below a label. Eleven functions publish 73 exact instructions after entry-aware lowering removes dead structured prefixes. If a dead structured prefix still points at the end of a function that cannot fall through, lowering adds an unreachable typed return block so the target stays inside the function. The deterministic object proof contains 237 text bytes in five functions, with decoded branch targets for ordinary jumps, terminal `if` and `while` entries, and a label above a four-byte automatic local. It has no relocations. The unchanged `goto done` cleanup path in `toolchain/cupidld.cc` pins the active requirement.

Hosted switch lowering evaluates its promoted 32-bit condition once. `DUPLICATE_VALUE` preserves that value while a source-ordered equality chain selects a case target. Matching and unmatched paths discard the saved value before they jump to a case, default, or exit. Dispatch discovery follows cases inside compounds, `if` arms, loops, and identifier labels, but stops at a nested switch. Entry-aware lowering validates dead prefixes without publishing their instructions, keeps inner cases from reviving an unreachable nested switch, and still permits direct `goto` into an ordinary label inside a case body. An unused identifier label does not revive a prefix merely because a reachable case follows it in the same block. Positive fixtures place cases inside `while`, `do`, and `for` bodies. Canonical signed constants include negative cases such as `-1`. The unchanged `cfront_public_storage` function publishes 59 exact IR instructions. Its exact 272-byte local object has six comparisons, six conditional branches, seven direct jumps, six returns, two symbols including the null symbol, and no relocations. ADR 0038 records the design and limits.

Represented integer mutation evaluates its destination once. `DUPLICATE_ADDRESS` keeps the address for the final store while the loaded value passes through integer promotion, usual arithmetic conversion where required, the selected operation, and assignment conversion. This covers `*=`, `/=`, `%=`, `+=`, `-=`, `<<=`, `>>=`, `&=`, `^=`, and `|=` plus all four prefix and postfix updates for supported byte, word, doubleword, and eight-byte integer objects. Prefix forms return the stored value. Postfix forms reconstruct the prior canonical value after the store without loading the object again. Qualified volatile objects keep one semantic load and one store. Boolean, atomic, floating, and aggregate mutation remain outside this ordinary-object slice. ADR 0039 records the original four-byte contract, ADR 0046 extends it to non-Boolean byte and word objects, and ADR 0074 extends the snapshot path to wide integers.

Represented bit-field mutation keeps the complete record address because a field has no C address. A partial field is read once for the computation and again for the final read-modify-write merge. Postfix forms retain the first extracted value instead of reconstructing it after width truncation. Narrow `unsigned int` fields use their target width when deciding whether to promote to signed `int`. The 1,415-byte object proof covers 20 functions, all ten compound operators, signed and unsigned prefix or postfix wraparound, neighboring-bit preservation, and volatile 32-bit direct stores, plus one indexed postfix case that advances its side-effecting index exactly once. It has 21 symbols including the null symbol and no relocations. No unchanged active expression currently uses bit-field mutation; this issue #25 proof advances the hosted language path without moving production ownership. Character-sized, Boolean, atomic, compact packed, and partial volatile forms keep focused diagnostics. ADR 0064 records this boundary.

Ordinary narrow bit-field reads keep a different kind of information. When an `unsigned int` field narrower than 32 bits promotes to signed `int`, the frontend places its direct member index on that one conversion. Linear IR verifies the member-load chain and matching graph and layout widths before accepting the same-rank signedness change. Generic conversions still carry no member index and keep the earlier rejection. The active requirement is all nine color-channel reads in unchanged `kernel/doom/src/i_video.cc`. A focused 14-instruction IR fixture and 127-byte object cover signed right shift, masking, and variable left shift. Four forged metadata cases fail transactionally and recover in the same job. ADR 0152 records this boundary.

Represented object pointers now cross the hosted address and value boundary without losing their C meaning. `DEREFERENCE` turns one pointer value into the referenced object address, while `ADDRESS_OF` performs the inverse transition. Both are semantic IR instructions and emit no machine instruction because each represented form is one i386 word. Pointer parameters, results, locals, linked objects, loads, stores, direct arguments, plain assignment, automatic initialization, compatible pointer conversions, and null-pointer conversion now reach deterministic object emission. Value matching removes qualifiers from the pointer object but keeps referent qualifiers, including the C rule that moves array qualification to its elements. A focused initializer carries distinct compatible qualified-array referents through the emitter's load and store checks. The unchanged `obj_region_less` helper in `toolchain/cupidobj.cc` supplies the active pointer and indirect-member requirement. ADR 0040 records that address and value boundary.

Object-pointer comparisons and truth tests now use the same represented scalar path. Equality accepts the pointer types already normalized by the frontend, relational comparisons keep compatible object-pointer operands, and the i386 emitter selects unsigned predicates. Pointer values can drive `!`, `&&`, `||`, conditional selection, `if`, `while`, `do`, and `for`. Explicit same-width casts now carry all 32 bits between represented integers and object pointers, or between represented object-pointer types, without emitting an instruction. Pointer-valued conditional arms normalize to the frontend's composite result type at the join. The condition contract pins all 62 public IR records. Relational validation requires object referents while equality retains frontend-normalized `void *` pairs. A malformed frozen unit that changes `void *` equality into pointer order fails transactionally. The unchanged `ctool_job_arena` helper in `toolchain/ctool.cc` pins the typed null cast, inequality, pointer condition, pointer-valued conditional, and indirect member load in one active expression. Atomic pointer access and casts between pointers and narrow integers remain open. ADR 0041 records the comparison and condition step.

Represented pointer arithmetic uses target layout instead of byte-based integer arithmetic. `POINTER_BINARY` scales 32-bit integer offsets by the complete pointed-to object size, while compatible pointer subtraction divides the address difference by that size and returns signed `int`. Frontend-normalized `pointer[index]` and `index[pointer]` use the same addition and dereference path. `ARRAY_TO_POINTER` records linked array decay without emitting a machine instruction and carries array qualification to the element pointer. Pointer `+=`, `-=`, `++`, and `--` evaluate their destination once; volatile pointer objects receive one load and one store. The unchanged ATA read and write loops in `drivers/ata.cc` pin `buf += 256` as an active requirement. Atomic pointer mutation, wide offsets, union and Cupid class values, deferred initializer leaves, and broader production integration remain open. ADR 0042 records the design.

Represented function pointers use the same four-byte scalar storage and value paths without losing their signatures. `FUNCTION_ADDRESS` names a linked function, `FUNCTION_TO_POINTER` records decay, and `CALL_INDIRECT` retains the prototype used for ABI checks. Structural compatibility ignores top-level `const`, `volatile`, and `restrict` on parameters while retaining `_Atomic` and referent qualifiers. A checked worklist remembers compared type pairs, returns all scratch storage to the job arena, and handles repeated callback graphs without recursive path growth. The emitter evaluates the callee before its arguments, reorders completed arguments into cdecl memory order, calls through EAX, and removes the saved callee with the caller-owned argument storage. Function pointers can cross fixed parameters and results, automatic and linked storage, static and automatic initialization, assignment, direct arguments, equality, null conversion, truth tests, and conditional selection. Casts may change a represented function-pointer signature or move the same 32 bits to or from a represented 32-bit integer. The unchanged `body(&invocation, user_data)` call in `toolchain/ctool.cc` and the unchanged CupidLD section-selector call pin the active requirement. The contract publishes 86 exact IR instructions across 13 functions. A separate signed wide-parameter fixture adds a five-instruction register-indirect call. Its aligned deterministic object contains 13 functions, 513 text bytes, 17 symbols, nine text relocations, and one data relocation. Four calls are register-indirect, one is direct, and the first 234 text bytes are exact. A separate 28-byte object pins one absolute relocation to a defined static function. Indirect variadic and unprototyped calls now carry signed or unsigned eight-byte integer arguments, existing `double` values, and source `float` values promoted to `double` through the same saved-callee and aligned outgoing-area path. Fixed indirect calls carry same-kind `float` and `double` arguments and results. Aggregate ellipsis transport, object-pointer and function-pointer interchange, function-pointer casts involving narrow or wide integers, atomic callback access, floating computation, union, and Cupid class call forms remain open. ADR 0043 records function pointer values and calls, ADR 0047 records discard casts, ADR 0049 extends fixed indirect calls to structure parameters and results, ADR 0050 records call alignment, ADR 0054 adds scalar variadic calls, ADR 0055 adds scalar variadic callees, ADR 0075 adds wide integer variadic transport, ADR 0076 adds floating scalar transport, ADR 0077 adds default `float` promotion, and ADR 0113 records represented function-pointer casts.

Referenced fixed arrays and structures receive target-sized EBP-relative storage in the hosted emitter. `LOCAL_ADDRESS` keeps the absolute block-binding identity in public IR, while the emitter assigns offsets in binding order, honors target alignment up to four bytes, and rounds the final frame to four bytes. The unchanged `section_map` array in `cupidc_emit.cc` and active `&children[index]` call shape in `cupidc_ir.cc` pin the storage requirement. Five focused functions publish 47 instructions and 264 exact text bytes, including a mixed 12-byte frame with addresses at EBP minus 3 and EBP minus 12. Its three call relocations are at offsets 145, 201, and 255. ADR 0048 adds the represented initializer-list subset described above. ADR 0049 allocates instruction-owned structure snapshots after those source objects. Other initializer leaves, top-level union and Cupid class values, and alignment above four bytes remain open. Oversized frames retain a checked limit failure. ADR 0044 records the source-object storage decision.

Represented one-byte and two-byte integers cross loads, exact-width stores, promotions, explicit and implicit conversions, plain assignment, compound assignment, prefix and postfix updates, automatic and linked storage, ordinary members, indexed elements, scalar conditions, fixed and variadic direct or indirect calls, and function results. The abstract stack keeps a canonical 32-bit word, with signed values sign-extended, unsigned values zero-extended, and `_Bool` normalized to zero or one. Each promoted narrow cdecl argument slot is four bytes. Callers and callees canonicalize narrow result lanes at the ABI boundary. The unchanged `asm_lower`, `x86_class_width`, and `x86_set_memory_width` functions pin the value requirement. The complete unchanged `x86_put_u8` body pins one-byte update, and active decoder paths also require one-byte counters and prefix `|=`. Boolean mutation, narrow and atomic bit fields, pointer and eight-byte atomics, compare-exchange, integer and floating conversion, floating comparison and truth, union and Cupid class values, aggregate ellipsis transport, and aggregate variadic reads remain open. ADR 0045 records narrow values, ADR 0046 records narrow mutation, ADR 0049 records inline structure arguments, ADR 0050 records call alignment, ADR 0054 records scalar variadic calls, ADR 0055 records scalar variadic callees, ADR 0075 records wide integer variadic calls and reads, ADRs 0076, 0077, 0079, and 0091 record the hosted floating path, ADR 0101 records the first integer atomic builtins, and ADRs 0065 through 0074 record the underlying wide value path.

For a variadic call, the frontend applies lvalue conversion, array and function decay, integer promotion, and `float` to `double` promotion to each ellipsis argument as required. It applies the same default promotions to every argument at an unprototyped call site. Each public call instruction retains the actual count and indexes a packed slice of every actual post-conversion type. IR and i386 emission use both records for stack effects, argument size and order, indirect callee placement, sixteen-byte padding, and caller cleanup. Signed and unsigned eight-byte values, existing `double` values, and source `float` values promoted to `double` use full-width slots at either undeclared parameter boundary.

GNU C mode now represents `__builtin_va_list` as Cupid's target `char *` cursor. The frontend publishes explicit start, argument, copy, and end expressions. Linear IR keeps start, argument, and end operations; scalar copy uses the existing store. The i386 emitter initializes the cursor just after the full width of the final named cdecl argument and reads through the old cursor. A non-atomic pointer or four-byte signed or unsigned `int`, `long`, or enum read advances by four. A non-atomic signed or unsigned eight-byte integer, represented wide enum, or `double` advances by eight and returns an instruction-owned snapshot. ADR 0055 records the cursor and ABI decisions, ADR 0062 extends the represented read types to enums, ADR 0067 covers a final named wide parameter, ADR 0075 adds wide integer reads, and ADR 0076 adds `va_arg(double)`. A request for `float` is diagnosed as invalid C because an unnamed `float` arrives as `double`.

An empty identifier-list definition now keeps its non-prototype function type while declaring zero parameters. Calls through a function type without a prototype apply default promotions to every argument and retain their actual count and post-conversion type slice through Linear IR and i386 emission. Represented four-byte integers and pointers, signed or unsigned eight-byte integers, existing `double` values, and source `float` values promoted to `double` cross this boundary. ADR 0056 records the function form, ADR 0075 records wide argument transport, ADR 0076 records floating transport, and ADR 0077 records default argument promotion.

Block-scope `struct` and `union` tags now follow lexical C scope. The frontend supports forward declarations, same-scope completion, ordinary references, nested shadowing, and restoration after scope exit. A record tag declared in a function definition's parameter list shares the outer body scope and expires when the definition ends. Tag-only declarations may use the represented `typedef`, `extern`, `static`, `auto`, or `register` spelling, or a represented type qualifier, when they introduce a tag. They remain in the statement stream with no block bindings, and IR treats them as checked no-ops. An empty declaration with storage or type qualification that only names a visible tag is rejected. A `for` initializer may use a visible record type or an anonymous record definition for its object, but it cannot introduce a named tag or omit the object. Anonymous record definitions work when a declarator owns the type, including Doom's block-static `packs` array.

Block-scope `extern` objects keep a lexical alias to one canonical linked object. Compatible repeats share identity, incomplete arrays may be completed, visible file-scope `static` objects keep internal linkage, and a block-only name stays out of ordinary file-scope lookup. The declaration creates no automatic storage and lowers without runtime work. Block typedefs retain their lexical ordinary-name scope, stable graph type, source order, and dual location. Exact same-type repeats are accepted, nested aliases and parameter shadows restore at scope exit, and scalar, record, function, incomplete, and `void` aliases use the normal declarator path. IR consumes each alias as a validated no-op, and object emission is byte-identical to spelling the underlying type directly.

Block function declarations now separate lexical type and visibility from linked identity. Plain and `extern` declarations point to one canonical function, and a visible prior declaration contributes to the later alias's composite type. A declaration from an expired sibling scope does not change the type seen in a later block. A visible file-scope `static` function keeps internal linkage, while a function first introduced in a block stays hidden from ordinary file lookup until a later file declaration publishes it. The declaration adds no storage or runtime IR. Direct calls and function addresses use the canonical symbol with the lexical type. The exact Doom profile still parses all of `kernel/doom/src/d_main.cc`, including the `forwardmove` and `sidemove` declarations on lines 1336 and 1337. Active-source guards also pin 27 block function declarations across nine files.

Block enums use that same lexical binding stream. Each enumerator retains its folded target value and final identifier type. A declaration owns declaration-position and record-member definitions, while a function definition owns a parameter-list prefix. Definitions in block type names attach their enumerators to the expression or initializer where they become visible. Linear IR validates those events in source order before it lowers runtime control flow. This covers `sizeof`, alignment queries, casts, compound literals, `__builtin_offsetof`, case values, loop headers, variadic reads, and aggregate designators. Nested tags and constants still shadow and restore in their C scopes, including an anonymous enum in a `for` declaration. Represented uses lower to `INTEGER` with no frame slot, symbol, relocation, or declaration instruction. File, block, and function-parameter enumerators can also feed static floating arithmetic, comparisons, truth, and conditionals. The active cursor enum in `kernel/gui/desktop.cc` and REPL enum in `kernel/lang/shell.cc` remain unchanged. Block declaration attributes, nested function definitions, nonempty identifier lists, and non-scalar arguments without declared parameter types remain open. ADR 0057 records the record-tag model, ADR 0058 records linked block objects, ADR 0059 records block typedefs, ADR 0060 records linked block functions, ADR 0061 records declaration-position block enumerators, ADR 0062 records nested definitions and lexical activation, and ADR 0147 records static floating use.

The narrow mutation IR matrix covers all ten compound operators, signed and unsigned byte and word updates, one volatile byte update, and a nested byte member. It requires 19 narrow address duplications, 25 narrow loads, 26 promotions, 23 narrowing assignment conversions, 20 exact-width stores, and one volatile load. The deterministic object proof has eight functions in 878 exact text bytes, ten symbols, one byte of BSS, and one `R_386_32` relocation. Shared decoding checks fourteen byte stores, four word stores, promoted multiplication, signed and unsigned division, and shifts. A decoder-driven execution oracle runs twelve signed and unsigned prefix or postfix cases at zero and wrap boundaries. It checks EAX, the stored byte or word, and poisoned padding in the four-byte argument slot. Signed narrowing keeps the low AL or AX lane and sign-extends it, giving a deterministic two's-complement result. Boolean mutation and malformed promoted-type metadata have transactional negative coverage.

Unchanged active source drives the supported subset. `add2` in `bin/cupidc_test3.cc` pins two parameter loads, one typed `ADD`, and a scalar return. `asm_lower` in `toolchain/cupidasm.cc` pins signed-byte parameters, loads, conditions, casts, and returns. `x86_class_width` and `x86_set_memory_width` in `toolchain/x86.cc` pin signed and unsigned byte and word parameters, member storage, promotions, and results. `cemit_multiply_overflows` in `toolchain/cupidc_emit.cc` pins unsigned division in the short-circuit right operand. `cemit_power_of_two` in the same file pins inequality, bitwise AND, equality, short-circuit logical AND, and the surrounding conditional expression. `cfront_bool_valid` in `toolchain/cupidc_frontend.cc` pins short-circuit logical OR over two equality tests. `cfront_public_storage` in that file pins switch dispatch, enum constants, shared case and default targets, and enum returns. `asm_branch_fits_i8` in `toolchain/cupidasm.cc` pins unsigned less-than-or-equal inside logical OR and conditional selection. The unchanged `obj_region_less` helper in `toolchain/cupidobj.cc` pins object-pointer parameters, repeated dereference, indirect member addresses, and pointer loads. The unchanged `ctool_job_arena` helper in `toolchain/ctool.cc` pins object-pointer inequality, a typed null cast, scalar truth testing, pointer-valued conditional selection, and an indirect member load. The unchanged `rotw` helper in `kernel/crypto/aes.cc` pins left shift, unsigned right shift, and bitwise OR while retaining the independently promoted signed `int` shift counts. The unchanged CPUID-toggle return statement in `kernel/cpu/simd.cc` pins bitwise XOR inside its shift, mask, comparison, and conversion context. Its surrounding GNU inline assembly and broader statement sequence remain outside this hosted leaf slice. The unchanged `align_up` helper in `kernel/mm/memory.cc` pins bitwise complement inside its existing unsigned arithmetic and mask. The complete unchanged `dis_signed_bits` function now pins unsigned less-than-or-equal and equality conditions, two conditional branches, three returns, complement, addition, an explicit unsigned-to-signed cast, and negation. Its exact IR contains 27 instructions and reaches an abstract stack depth of two. The complete unchanged `syscall_sleep_ms` helper in `kernel/core/syscall.cc` pins condition reevaluation, the false exit, and the backward loop edge around `process_yield`. Its exact IR contains 14 instructions and reaches an abstract stack depth of two. A focused terminal-body `while` pins the five-instruction path with no backward jump. The unchanged inner tick loop in Doom's `D_Display` function pins body-first execution, condition evaluation after the body, and the backward edge to the body. Its focused IR contains 21 instructions and reaches an abstract stack depth of three. A terminal-body `do` lowers to one return while still validating its unreachable condition. The guarded `url_hash_hex` loop in `bin/browser/url_hash.cc` pins an expression initializer, signed condition, body, assignment iteration, and backward edge. Its focused IR contains 23 instructions and reaches an abstract stack depth of three. Omitted-clause fixtures cover a terminal body and an infinite loop that cannot fall through. The logical-not result in `cc_skip_brace_initializer` remains guarded separately; its broader expressions and control flow still block the complete function. The Paint coordinate functions pin subtraction, multiplication, and addition over linked objects. `vga_flip_ready` covers an automatic initializer call, a linked load, unsigned comparison, conversion to `bool`, and return. `vga_set_vsync_wait` covers a linked assignment whose result is discarded. `timer_get_frequency` keeps the `timer_state` binding and `frequency` graph member until the emitter applies byte offset 8. The Doom guard pins all four color fields in `kernel/doom/src/i_video.h` and all nine red, green, and blue ordinary-expression reads in `kernel/doom/src/i_video.cc`. Its focused IR fixtures retain both the field load and the member-specific promotion. A returned assignment chain proves that each destination is evaluated once and that the stored value survives both stores.

Supported automatic declarations name complete represented scalar objects or fixed array and structure objects with target alignment up to four bytes. Their storage spelling may be absent, `auto`, or `register`, and fixed aggregates may use the supported list initializers. A supported static declaration instead requires a complete nonzero object and a represented constant-data initializer root; it emits storage but no runtime initializer instructions. Both forms may appear in a supported compound statement or as a `for` initializer. A private source-order scan establishes the complete block-binding range for each function before lowering, including declarations below a label. The current statement set contains return, expression, empty, compound, `if` with optional `else`, pre-test `while`, post-test `do`, `for` with optional expression or declaration control clauses, `switch`, `case`, `default`, nearest-control `break`, nearest-loop `continue`, direct labels, and `goto`. A `while` evaluates its condition before each possible iteration. A `do` reaches its body first and evaluates its condition before a possible backward edge. A `for` evaluates its initializer once, then its condition, body, and iteration in C source order. A switch evaluates its promoted condition once, compares cases in source order, and jumps directly to a matching case, the default, or its exit. An omitted loop condition has no false exit, but a reachable `break` can still make the loop fall through. A `continue` reaches the condition for `while` and `do`; it reaches the iteration expression for `for` when one is present and the condition otherwise. A terminal loop body emits no unreachable work or backward edge unless reachable loop control requires the condition or iteration. Skipped conditions, iterations, declarations, labels, and jumps are still checked against the supported-language boundary. Count-only declaration validation advances the same ownership cursor without publishing initializer instructions or changing live label targets. A sequence stops publishing instructions after a terminal statement unless a later subtree contains a reachable label. Instructions serialized before that label are bypassed by the incoming jump. The fixed-point result supplies the final fallthrough decision for every function. A void path that reaches the end receives an implicit return, while a nonvoid path that can fall through remains unsupported. A rewound owner map rejects aliased roots, dangling list edges, and unowned initializer records before lowering.

All call operands are evaluated in source order. Scalar-only calls retain their four-byte slot reversal. A structure-aware call reserves one outgoing ABI block, zeroes it, and fills scalar and structure parameters in declaration order; an indirect call also keeps its callee below the evaluated arguments until the emitter loads it into EAX. Structure arguments occupy their target size rounded up to four bytes. A structure result adds a hidden first word that the callee removes with `RET 4`. Immediately before `CALL`, ESP is aligned to sixteen bytes. The emitter derives zero, four, eight, or twelve bytes of padding from the fixed frame, live semantic values, and outgoing storage. Referenced automatic scalars and fixed arrays or records receive target-sized EBP-relative slots, while structure snapshots receive private slots after the source objects. A block-static address uses its local symbol and an `R_386_32` relocation, never a frame slot. Direct calls use `.rel.text` `R_386_PC32` relocations with addend `-4`. Linked object and function addresses use `R_386_32` with addend zero. Direct jumps use no relocation. `MEMBER_ADDRESS` applies an ordinary member byte offset after relocation. `BIT_FIELD_LOAD` applies the storage-unit byte offset, bit offset, width, and signedness during target emission without changing the base symbol or relocation addend. `BIT_FIELD_STORE_VALUE` uses the same member metadata to replace one field and preserve the value represented by the stored lane. Static inline definitions and external definitions from mixed inline declaration sets are accepted. Pure external inline definitions stop at a focused lowering boundary.

Exact object contracts retain the complete 61-byte VGA load function, the 20-byte timer getter, both 60-byte Paint functions, the 28-byte unsigned multiplication fixture, and the 27-byte and 37-byte assignment fixtures. A separate 138-byte object pins signed and unsigned quotient and remainder functions, with no relocations. Signed operations use `CDQ` and `IDIV`; unsigned operations clear EDX and use `DIV`. The comparison object contains the 127-byte active CupidASM helper and three 39-byte focused functions. Its 244 text bytes cover signed less-than, signed less-than-or-equal, unsigned less-than, and unsigned less-than-or-equal with no relocations. The combined function object appends the exact 143-byte `cemit_power_of_two` function and the 127-byte `cfront_bool_valid` function, bringing its aligned text to 917 bytes. The decoder checks five branch targets in the logical AND helper and six in each logical OR helper. A separate object-pointer contract has six functions in 198 exact text bytes for inequality, equality, unsigned order, and explicit integer/object-pointer casts. The pointer-condition contract has eight functions in 372 exact text bytes for logical not, short-circuit logic, conditional selection, and every supported statement condition. Both objects have no relocations, repeat byte for byte, and decode to the expected compare, predicate, test, branch, and return instructions. The pointer-arithmetic object adds nineteen functions in 811 exact text bytes. It has twenty-one symbols, one sixteen-byte BSS array, two exact absolute relocations, complete-object strides of one, two, four, and twelve bytes, and byte-identical repeated emission.

A separate 86-byte shift object contains the exact 53-byte `rotw` helper and a 33-byte signed right-shift fixture. It has three symbols, no relocations, and decoded coverage for `SHL`, `SHR`, `SAR`, and `OR`. The CPUID-toggle expression has its own exact 69-byte local function, two symbols including the null symbol, no relocations, and decoded coverage for `XOR` with the surrounding shift, mask, and comparison. The memory-alignment contract adds one exact 73-byte local function, two symbols including the null symbol, no relocations, and decoded coverage for `ADD`, `SUB`, `NOT`, and `AND`. The integer-unary contract adds four functions totaling 86 text bytes, five symbols, no relocations, and decoded coverage for `NEG`, `TEST`, `SETE`, and `MOVZX`; unary plus needs no target instruction. The integer-cast contract adds two functions totaling 52 text bytes, with sizes of 35 and 17 bytes. It has three symbols, no relocations, and decoded coverage for `NOT`, `ADD`, and `NEG`; the same-width casts need no target instruction.

The complete signed-bit helper adds one exact 143-byte local function with 71 decoded instructions. Its two conditional branches land at byte offsets 53 and 111. The object has two symbols including the null symbol, no relocations, and repeats byte for byte. The complete sleep helper adds one exact 94-byte local function with 43 decoded instructions. Its false branch lands at byte offset 92, its backward jump lands at byte offset 20, and its three direct-call relocations are at offsets 11, 24, and 80. The focused Doom loop adds one exact 125-byte local function with 59 decoded instructions. Its false exit lands at byte offset 123, its backward jump lands at byte offset 6, and its two direct-call relocations are at offsets 14 and 78. The combined loop object contains the 107-byte browser function and eight loop-control functions totaling 319 bytes. Its 426 text bytes have ten symbols including the null symbol, no relocations, and exact branch targets for `break`, all three continuation points, and nested nearest-loop binding. A separate declaration object contains an 87-byte declaration-initialized loop, an 80-byte nested-compound function, an 11-byte function whose declaration follows an unconditional return, and a 60-byte loop-body function. Its 238 text bytes have five symbols including the null symbol, no relocations, fixed local slots, exact branch targets, and byte-identical repeat emission. The direct-jump object adds a 44-byte forward function, a 76-byte backward function, two 38-byte terminal structured functions, and a 41-byte label-entry declaration function. Its 237 text bytes have six symbols including the null symbol, no relocations, and exact decoded targets. The declaration function lands at byte offset 11 and uses one four-byte local slot. Repeat emission is byte-identical and preserves the frozen input.

The separate bit-field load object adds three functions totaling 63 text bytes. It covers an unsigned eight-bit field, a signed five-bit field at storage byte offset 4, and a full-width field at byte offset 8. Their three direct-object relocations remain at offsets 4, 25, and 49 with addend zero, and repeated emission is byte-identical. The ordinary-promotion object adds `shift_red`, `mask_green`, and `shift_blue` in 127 exact text bytes. Its four symbols have no relocations. Eight decoder-driven executions check signed right shift, masks, variable shifts, unchanged storage and arguments, canaries, and restored cdecl state. A 64-byte output limit fails transactionally, and recovery reproduces the object. The bit-field assignment contract adds four functions and 31 exact IR instructions. Its indexed Doom-shaped function places a 1,024-byte color array in `.bss` and uses one `R_386_32` relocation. A decoder-driven i386 oracle runs six pointer-based cases and checks truncation, signed extension, neighboring bits, one complete-unit store, unchanged arguments, restored stack state, and a full-width store with no old-unit read. Consecutive emissions are byte-identical. Focused negatives distinguish unsupported character-sized, Boolean, atomic, and compact packed fields. Malformed graph and layout widths fail transactionally.

The wide comparison and condition contract covers all six signed and unsigned comparisons, mixed signedness, logical not, short-circuit logical operators, conditional selection, and scalar conditions in `if`, `while`, `do`, and `for`. Its 24 functions lower to 264 exact IR instructions with fingerprint `9EE1D330DE86EDBB`. The deterministic object has 3,341 text bytes with fingerprint `16626CE1`, 25 symbols including the null symbol, and no relocations. A decoder-driven i386 oracle exercises high-word-only truth, equal-high low-word ordering, and signed overflow-aware ordering while checking the cdecl frame and callee-saved registers. Full-body guards keep `pp_if_value_truth`, `pp_if_is_negative`, and `pp_if_signed_less` tied to the active source requirement. Eight-byte shift counts remain a focused boundary. Malformed comparison metadata and output limits fail transactionally. Other unsupported bodies and call shapes, malformed pointer, structure, loop-control, direct-jump, switch, and variadic records, and pure external inline definitions still leave output empty and rewind operation storage.

Wide shifts, AND, OR, XOR, explicit represented-to-wide casts, same-rank signed-to-unsigned conversion, GNU wide-enum promotion, conversion across represented integer widths, and object-pointer conversion to and from signed or unsigned eight-byte integers retain their positive contracts. Eight-byte constants, matching conditional arms, fixed call results, discard, and returns use the ADR 0065 path. Object and function pointer values, address-of, dereference, indirect ordinary members, object-pointer arithmetic, normalized subscripts, linked array decay, pointer mutation, narrow integer mutation, structure copies including nested union storage, scalar members of structure rvalues, structure returns, fixed direct or indirect structure calls, four-byte, wide-integer, and floating variadic calls and callees, floating width conversion, mixed floating expressions, floating compound assignment, sixteen-byte call sites, and the first one-, two-, and four-byte integer atomic builtins are represented. Boolean mutation, pointer and eight-byte atomics, compare-exchange, atomic bit fields and aggregates, character-sized bit fields, non-four-byte storage units, packed storage units that cross the record boundary, partial volatile bit-field mutation, explicit bit-field initializer leaves, integer and floating conversion, floating comparison and truth, aggregate ellipsis arguments, aggregate variadic reads, top-level union and Cupid class values, aggregate members of structure rvalues, and broader production integration remain open.

ADR 0066 adds eight-byte object values to the represented path above. `FILE_ADDRESS`, `LOCAL_ADDRESS`, pointer dereference, `MEMBER_ADDRESS`, and indexed pointer arithmetic can feed a wide `LOAD`. Automatic expression initialization and aggregate leaves use `STORE`, while plain or chained assignment uses `STORE_VALUE`. The emitter copies eight bytes with `CLD` and `REP MOVSB`, preserving one snapshot handle as the assignment result. The IR proof covers eleven exact function streams and fourteen wide loads. Its deterministic object has 16 data bytes, 879 text bytes with fingerprint `2448A1CD`, fourteen symbols, and two exact `R_386_32` relocations. The execution oracle runs the relocated active `get_cpu_freq` path, the block static path, and plain and chained pointer assignments. The active source remains unchanged. Atomic wide loads and stores stay rejected.

Active Doom declarations require the same array-address form at `kernel/doom/src/g_game.cc` for `mousearray` and `joyarray`, and at `kernel/doom/src/tables.cc` for `finecosine`. The focused contract mirrors the constant-expression subscript used by `finecosine`. The forced `kernel/doom/dglibc_compat.h` header parses with its builtin cursor alias, and the empty identifier-list definition of `doomgeneric_Tick()` now passes. The pinned exact-profile parse of `d_main.cc` accepts the anonymous block-static `packs` record and both local external arrays, then completes the file. The command driver can reproduce that profile with ordered `-include` inputs. CupidC retains the sound driver's empty volatile memory barrier in Linear IR and emits no instruction bytes for it. Its static scalar evaluator also compiles the unchanged fixed-point table in `kernel/doom/src/am_map.cc`.

ADR 0149 adds a separate Doom compatibility switch for old C implicit function declarations. An undeclared direct call creates a block-scoped `int()` declaration linked to one canonical external function. Calls made before a later prototype keep default argument promotions, while later calls use the refined prototype. ADR 0151 uses that explicit profile for eleven bit-preserving conversions between unqualified function pointers and unqualified four-byte data or `void` pointers. The frontend and Linear IR check the rule independently; strict C and ordinary GNU mode still reject it. The affected pointer sites are in `m_menu.cc`, `p_saveg.cc`, `p_ceilng.cc`, and `p_plats.cc`. ADR 0153 adds one-active-member union initialization, which compiles unchanged `kernel/doom/src/info.cc`. ADR 0152 retains direct member identity when a narrow `unsigned int` bit field promotes to signed `int`; unchanged `kernel/doom/src/i_video.cc` emits a 9,312-byte object with SHA-256 `8e9fcb59120cac9e8237a8243003fe1696a7841096aca7af360c89fec173336f`. The checked seed owns all 80 Doom-tree sources and the three compatibility roots in the normal image.

ADR 0182 completes the separate three-root `DOOM_COMPAT_I386` frontier.
Explicit non-atomic pointer-to-pointer casts retain a static
string or binding address, while a cast through an integer remains rejected.
The exact dglibc file-scope effect emits 27-byte `dg_setjmp` and 38-byte
`dg_longjmp` functions through Cupid's x86 model with no relocation. A
checked-seed compiler produces the same 27,992-byte dglibc, 14,352-byte
libc-stub, and 10,232-byte platform objects on two runs. ADR 0183 records the
five-tool seed promotion. ADR 0184 moves all 83 normal recipes and source
names to CupidC and `.cc`.

The Doom production wrapper has exact three-source and 80-source allowlists.
It freezes the selected source and all 289 `.h` and `.inc` inputs visible
through the profiles' 20 include roots. The 68,850-byte input manifest has
SHA-256
`259d7994ba929d6740528eba117bf9586c713a35e9d3edd0b4fae8b82219d87c`.
The wrapper recursively checks visible `.c` and `.cc` files beneath the Doom
tree before and after each compile. It rejects a legacy `.c` file, an
unlisted `.cc` file, a missing root, header membership or byte changes,
symbolic links, and NTFS junctions. An unchanged scan keeps the manifest
timestamp, so the closed input check does not force all 83 objects to rebuild.

The production object validator accepts signed `R_386_32` addends used to
select static subobjects. This keeps `&mousearray[1]` and `&joyarray[1]` in
unchanged `g_game.cc`; its 51,492-byte object has SHA-256
`c9da48e696eb521441e8bee0a2b69bfdd691db57b7fbbda42450d208e78d9034`.
Both `.data` relocations carry addend 4. `R_386_PC32` still requires addend
-4.

Private four-CPU e1000 and RTL8139 boots pass the complete runtime frontier.
Separate command gates run the no-WAD path, recover from an explicit missing
IWAD, and then complete CupidC-built `ls` on both NICs. The repository has no
WAD, so gameplay, game input, game audio, and save behavior remain open.

The block-static object proof emits eleven exact local symbols, from `.LBS0.hex` through `.LBS10.unused`. Its sections contain 21 bytes of read-only data, 56 bytes of initialized writable data, and 4 bytes of zero-filled storage. Ten text, one read-only-data, and five data relocations are all direct `R_386_32` references with addend zero. The fixture covers shadowed names, unused and unreachable objects, aggregate and string initializers, linked and unresolved addresses, runtime reads and writes, and an unused eight-byte image. A referenced eight-byte block static now lowers through the wide snapshot path. Missing, out-of-range, mistyped, runtime-initialized, and constrained-output cases still fail transactionally. The unchanged `dis_hex_fixed` helper in `toolchain/cupiddis.cc` pins the active constant character array.

All twelve shared hosted Toolchain implementation files parse completely. Each tuple reports definitions, statements, expressions, block bindings, and initializers: `ctool.cc` 65/1,012/5,981/133/33, `cupidasm.cc` 81/2,935/19,252/326/186, `cupidc_emit.cc` 353/8,554/72,476/1,045/710, `cupidc_frontend.cc` 422/16,503/109,174/2,480/1,509, `cupidc_ir.cc` 262/7,250/67,490/953/354, `cupidc_pp.cc` 143/3,932/25,287/479/286, `cupidc_type.cc` 31/737/5,487/85/43, `cupiddis.cc` 68/1,553/10,065/154/118, `cupidld.cc` 66/2,064/13,347/267/146, `cupidobj.cc` 34/1,012/7,202/117/58, `elf32.cc` 37/1,219/9,457/143/70, and `x86.cc` 60/1,760/11,855/180/16,702. The generated audit records the current lexical totals and source graph. They now belong to the actual i386 Linux profile and feed both the five-tool fixed point and the Cupid-built contract cohort.

The shared frontend treats C11 `<:` and `:>` spellings as canonical brackets across array declarators, subscripts, and the explicit unsupported `__builtin_offsetof` array-designator seam while leaving the immutable preprocessing tape's original token spelling untouched. Strict-C contracts cover mixed and full digraph forms plus malformed and non-pointer subscripts. Compound/update diagnostics distinguish valid but deferred floating `*=`, `/=`, `+=`, `-=`, and updates from invalid floating remainder, shift, bitwise, or aggregate compound/update operands. Compatible aggregate plain assignment is represented without weakening those constraints.

ADR 0153 supersedes positional-union limits preserved in the older detailed
frontend paragraphs below. A union initializer list may now select one direct
member, positionally or by name. Repeated active-member overrides and Cupid
class initializer lists remain open.

The current normal image is compiler-owned by CupidC. A platform-neutral Cupid
Toolchain foundation, a typed transactional CupidC preprocessing tape, a
shared declaration and function-body frontend, typed linear IR, deterministic
object emission, an immutable indexed i386 type/layout operation, a shared
ELF32 module, and a shared typed 16/32-bit x86 instruction model serve the
active kernel, driver, and Doom sources. Explicit native oracle contracts and
development commands still use GCC or Clang.

The preprocessing module owns translation-phase tokenization, ordered
object, function, and variadic macros, C11 conditionals and predefined macros,
`#line` locations, direct and macro-expanded includes, forced inputs,
guarded traversal, canonical once identity, pack metadata, and typed Cupid
`#exe` markers. Checked manifests classify all 2,392 include operands as
2,158 direct quoted plus 234 direct angle forms with zero macro operands
across 686 active C-family inputs. The generated manifest drives 381 tracked
profile runs under ten profiles plus four generated kernel roots. The
profile counts are 155 kernel, three Doom compatibility, 80 Doom tree, three
user, 105 Cupid programs, 31 strict hosted i386 Linux, two hosted i386 kernel
bridge, and two GNU hosted i386 Linux runtime roots. Both 64-bit hosted profiles
now have zero roots.

The `toolchain:all` target bootstraps both checked compiler stages, builds
fourteen Toolchain contracts and the runtime probe as static i386 ELF files,
compares the two generations, and publishes the contracts with five refreshed
tools and a manifest. The audit also keeps 22 browser fragments under
`bin/browser.cc` and two delivered headers without an invented standalone
context. No hosted translation unit is deferred. Native Windows self-hosting
and a Python-free driver remain open.

The hosted `ctool_c_layout_types` contract fixes scalar, pointer, array, enum, vector, function-marker, aligned-wrapper, qualified-wrapper, struct, union, class, bit-field, flexible-array, packed, and explicit-alignment representation to the Cupid i386 target. Enum size, alignment, and signedness copy a frontend-selected compatible integer type. The independent manual active-source layout fixtures select signed `int` and are `4/4`; the declaration frontend now selects compatible types from source, including unsigned `int` for both nonnegative enums in the FAT16 closure. Positive contracts include a direct atomic pointer at `4/4`, a synthetic signed-`long long`-compatible atomic enum at `8/8`, and an aligned incomplete array retained as `0/16` until a compatible declaration supplies its bound. `QUALIFIED` represents `const`/`volatile`/`restrict`/`_Atomic` use of an existing semantic type without cloning its representation or record slice; a pointer to qualified `T` remains distinct from a qualified pointer to `T`. Non-atomic qualification preserves layout. Aligned wrappers carry an exact effective typedef/type-attribute alignment that may lower or raise natural alignment; explicit record alignment only raises the computed record result. Atomic identity propagates through both wrappers, but alignment follows source order: introducing `_Atomic` applies the cached target minimum, a later exact alignment may lower it, and later non-atomic qualification preserves it. Atomic aggregates remain unsupported. Layout enforces a flexible array's final structure position, while the declaration frontend enforces named-member eligibility, including names promoted through anonymous records. The operation resolves immutable index graphs with an iterative strong-edge walk, caches `_Bool`, active-atomic, and atomic-minimum facts once per type, preserves presumed/physical semantic locations, reclaims traversal scratch, and returns stable job-owned layouts transactionally in `O(types + members)`. A 4,096-wrapper/4,096-bit-field regression closes the former repeated-unwrapping path. Manual typed graphs pin all 54 FAT16 member offsets plus active Doom, process, syscall-table, `e1000_rx_desc_t`, and per-CPU ABI shapes.

The hosted `ctool_c_parse` operation consumes the ADR 0012 tape directly and publishes the ADR 0013 graph and completed layouts together with canonical job-owned file bindings, tags, normalized function-parameter types, definition-local parameter object types, source-ordered block bindings, function-scoped labels, semantic object initializers, first-declaration storage/provenance, effective C linkage, names, dual locations, and immutable postorder function-definition, statement, expression, and child tables. Definition records point at the canonical entity while retaining their exact declared type, storage, `inline`, body, and label slice. Each definition parameter retains its source storage and adjusted object type, including top-level qualification, while the parallel function-type entry stays unqualified for compatibility. The body grammar owns compound, declaration, expression, return, typed `if`/`else`, counted `for`, typed `while` and `do`, typed `switch`/`case`/`default`, identifier labels, direct `goto`, `break`, and `continue` statements; null expression statements with `CTOOL_C_AST_NONE`; typed `IF` nodes with a converted scalar condition, required body, optional `else_body`, nearest-unmatched-`if` association, and postorder bodies; typed `FOR` nodes whose initializer is `CTOOL_C_AST_NONE` when omitted and otherwise names a present expression or declaration statement, plus optional converted scalar conditions, optional converted iterations, and required bodies; typed `WHILE` and `DO` nodes with converted scalar conditions and required postorder bodies; typed `SWITCH` nodes with promoted integer conditions and required postorder bodies; folded, converted `CASE` constants; per-switch default and duplicate-value tracking; targetless `break` and `continue` leaves; canonical label identities shared by `LABEL` and `GOTO` nodes; complete automatic objects with none/`auto`/`register` storage and optional scalar, whole-record, character-array, or recursive array and structure initializers; represented block-scope static objects with implicit zero, target integer or floating constants, narrow character-array strings, direct narrow-string addresses, or recursive array and structure lists; block-scope external objects whose lexical aliases name canonical linked entities; file-binding, parameter, and block-binding references; decoded owned ordinary strings; target-typed integer and ordinary narrow character constants; explicit scalar/void casts; address/dereference and direct or anonymously promoted `.`/`->` member designators; the implemented integer operator ladder; pointer addition/subtraction and normalized subscripting; right-associative simple/compound assignment; prefix/postfix updates; prototyped and unprototyped calls; empty identifier-list definitions; and scalar `return` conversion. A `DO` body and its expressions precede its condition and loop node. A label owns its statement body in postorder, while a `goto` label reference is a semantic cross-reference that may point forward or backward. Each public block binding indexes the semantic initializer forest, whose direct-subobject edges live in a parallel immutable table. Uninitialized automatic objects use `CTOOL_C_AST_NONE`; supported static objects always retain a root record, including implicit zero initialization, while omitted aggregate subobjects remain implicit zero. A provisional binding at the future stable index makes the declared name visible through its own initializer at C's point of declaration, while later comma declarators remain invisible until declared. Automatic expressions reuse the shared assignment conversion without applying assignment's modifiable-lvalue requirement. Static integer records use the target integer evaluator and conversion. Static floating records keep target-width IEEE bits after integer-only binary32 or binary64 evaluation of represented arithmetic, comparisons, casts, truth, logic, and conditional selection. String records retain effective copied bytes plus the completed destination type. Direct string-address records own their decoded bytes and a zero addend. List records own direct-subobject edge slices, with every child root preceding its parent. Recursive automatic and static arrays and structures accept explicit braces, trailing commas, and brace elision within the represented forms. Automatic leaves retain runtime expressions; static lists still require constant-data leaves. Empty and excess lists fail precisely. Direct member and array designators select one immediate subobject in source order; positional clauses resume after the selection, and brace-elided children leave a following designator for the nearest explicit list. Chained selectors, promoted anonymous members, duplicate overrides, and positional union or Cupid class lists remain explicit boundaries. Explicit nodes retain lvalue/array/function/qualification, integer-promotion, usual-arithmetic, and assignment conversions. Compound assignments and updates retain one raw designator child plus distinct stored/result and computation types, so later lowering cannot duplicate side effects or lose postfix semantics. Each member AST node refers to one direct ADR 0013 graph member; promoted anonymous-record names publish an ordered chain of direct member hops rather than a flattened pseudo-member. Record qualification and register addressability provenance follow that chain, array decay retains element qualification, and narrow unsigned-`int` bit-fields promote according to their target width. Cupid i386 ranks and widths drive the body independently of the bootstrap host, including the ILP32 `long + unsigned int -> unsigned long` rule and signed-`int` pointer difference. The integer-constant-expression path accepts ordinary narrow character constants and integer-target casts over its represented operands, which covers the active `case (ctool_u8)'x'` shape. Out-of-range signed casts use Cupid's documented two's-complement target result. Static scalar floating expressions use the typed evaluator instead of the integer-constant-expression engine. Ordinary runtime expressions are typed but not evaluated by the integer-constant-expression engine, so divide by zero, signed overflow, and overshift source remains represented for later lowering rather than receiving declaration-time folding diagnostics. Non-VLA `sizeof`, alignment queries, and `__builtin_offsetof` are the deliberate exception: typed operands/member paths are checked against the current target graph and folded to unsigned 32-bit target constants. Unevaluated expression records and decoded-literal arena scratch are rewound before publication, so assertions add no entity, member, statement, expression, or unreachable string storage. Declaration statements index ordered public block-binding slices, while block-binding expressions refer to those stable entries. Lexical lookup walks inner block bindings, definition parameters, then file bindings; static integer evaluation honors the same shadowing before accepting a file-scope enumerator. Nested blocks may shadow parameters, file objects, outer locals, and typedefs, scope exit restores the hidden declaration, and the function body's outer compound correctly shares its parameter scope. One C11 loop scope covers a declaration initializer, condition, iteration, and body before expiring, while the block-item/statement split rejects a declaration used directly as a selection or loop body. Every retained child precedes its parent for direct later lowering.

The unchanged `/kernel/fs/fat16.h` closure still reproduces every FAT layout oracle. Exact additional contracts parse unchanged `kernel.h`, `irq.h`, `cupidscript.h`, and `shell.h` and merge representative duplicate prototypes and typedefs once at the first declaration. GNU `packed`, `aligned`, and `noreturn` lists retain their semantic destinations, and compatibility keeps stronger alignment and `noreturn`. File- and record-scope `_Static_assert` use target integer evaluation, including conditional selection and fault suppression in unevaluated arms. Active-source fragments prove all 26 tracked assertions across `memory.h`, `percpu.h`, `exec.cc`, `process.cc`, and `syscall.cc`.

The [audit-derived active-source gate](./ACTIVE-SOURCE-AUDIT.md) is 155/155
general non-Doom headers at compiler head. The graph contains 717 active
language inputs: 27 assembly files, 289 headers, and 401 Cupid C files. No
ordinary C translation unit remains in the supported roots. It records 254
feature IDs, 449 transforms, and 25 accounted unreachable files. The preprocessor
inventory covers 686 files and 2,392 include occurrences, split into 2,158
quoted and 234 angle forms.

The active-source digest is
`4cc621b69736f3b9f4c22565a8f4ec026bb775bb311254a6c7f9b1b1dd5f7265`.
The 2,546,938-byte audit JSON has SHA-256
`fbd3aabb36e73aea1ee332e7c7413614b6b52bd0ffdec090e9cdcfc5691bb22e`,
and the 12,136-byte summary has SHA-256
`956a34695080089d697307c2c672966501f5ccebf8a5d44a5f8c331022d8447c`.

Across the three supported roots, CupidC participates in 245 transforms and
CupidObj participates in 185 transforms. Python participates in all 449
because it runs the checked-seed commands. No transform invokes a host C
compiler, and no recursive Make transform remains. The 438-transform root
image graph runs
CupidASM, CupidObj, CupidLD, and CupidDis from the checked seed. The audit
evaluates Make conditionals with the canonical Windows branch and C locale on
every host; direct Linux builds cover the Linux execution branch. The public
runner rechecks the live five-tool trust unit after every command.
Output-bearing wildcard lists pass through Make's `$(sort ...)` before
generators or link order consume them. ADR 0190 records the root tool handoff,
and ADR 0196 removes host C from the normal Toolchain root.

The two ISO fixture transforms are now explicit. `gen-big` owns only
`test_iso/fixtures/big.bin`. A separate `package_iso9660_image` transform
checks the exact membership in `test_iso/fixtures.manifest`, freezes that
tree, and writes the tracked ECMA-119 image with fixed `RRIP_1991A` metadata
and a continuation placed after the directory stream.
Make declares the same seven portable paths explicitly instead of expanding
raw manifest text or recursively walking a possible link. A test locks that
prerequisite list to the manifest.
Hostbuild checks the manifest and tree again before atomic publication and
preserves an identical output. The normal graph no longer probes for
`mkisofs`, `genisoimage`, or `xorrisofs`. ADR 0191 records the format and
publication contract. The settled image passes the complete four-vCPU e1000
frontier, including the exact six-name ISO directory check and the existing
read, JPEG, mount-lifetime, graphics, audio, network, SMP, and USB checks.

An initial Windows and Linux comparison matched 426 of 430 kernel artifacts.
The only differing input object wrapped a progressive JPEG that host FFmpeg
had rewritten differently on the two systems. The repository stores the
equivalent sequential baseline bytes. Hostbuild accepts structurally checked
SOF0 or SOF1 input, copies it exactly, and rejects progressive, unsupported,
or malformed marker streams before checked CupidObj wraps the private
snapshot. The root
build no longer calls FFmpeg, `jpegtran`, `djpeg`, or `cjpeg`. The Linux
kernel build passed in 607.7 seconds, and the Windows root build passed in
341.6 seconds. All 430 frozen kernel artifacts match byte for byte.

The matching `kernel/kernel.bin` is 8,490,228 bytes with SHA-256
`53770a93658e757d25f5aeab9d3e434d4a3be2a1dc3fbe4b19869e5bf9820a06`.
The fresh normal image is 209,715,200 bytes with SHA-256
`e815d2ef67f114a26181f0e2cbde85f892cdadd487f8d9cbee9715e720800b3e`.
A private `/bin/ls.cc` JIT boot from it passed in 49.8 seconds. ADR 0190
records the complete artifact table, log identity, and layout headroom.

The canonical active-source digest for this graph is
`7caa739641b278914bdabea9686992a14b5f8ab22acac5f5a27a1884cd26b566`.

External-inline policy now follows translation-unit finalization described by [ADR 0131](../adr/0131-finalize-c11-external-inline-definitions.md). The frontend recognizes external definitions across compatible declaration sets, preserves inherited internal linkage, and rejects an external-linkage inline declaration without a definition. Iterative memoized type relations normalize C qualifier spellings while retaining atomic parameter identity, distinguish strict typedef identity from compatibility, apply old-style/default-promotion rules, accept a 512-level derived pointer graph, and construct symbol-local immutable array/function composite types without corrupting shared typedefs. Transactional tests cover precise conflicts, lexical-scope duplicates and expiry, automatic and static initializer forests, explicit and tentative file definitions, binding addresses, scalar and aggregate return or assignment legality, recursive aggregate modifiability, pointer arithmetic and comparison constraints, conditional association and conversions, loop and switch constraints, direct jumps and label scope, compound/update constraints, malformed literals, unsupported local storage forms, ownership, deep syntax, constrained output, rollback, and recovery. Runtime expression values carry private integer-constant-expression form and value metadata. A represented zero expression, or that expression cast to non-atomic `void *`, becomes a null pointer constant. Comparisons, conditionals, returns, calls, assignments, and automatic initializers publish a destination-typed `CTOOL_C_CONVERSION_NULL_POINTER`; static explicit nulls publish `ZERO` records and discard their temporary expression AST. Comma expressions now evaluate left to right and retain the last operand, and known-true loops preserve non-fallthrough reachability. GNU `weak`, `section`, and `unused` attributes publish canonical entity metadata; exact output-only assembly can snapshot represented i386 register and EFLAGS state. The constant and body expression grammars remain intentionally partial, and namespace and member lookup remain linear. Chained designated paths, promoted anonymous members, duplicate overrides, positional union or Cupid class lists, static member-address constants outside the block-static symbol path, integer-routed and other unrepresented address casts, automatic bases, runtime offsets and subscripts, block declaration attributes, nested function definitions, computed goto and GNU label addresses, broader GNU assembly forms, hexadecimal and subnormal floating constants, long-double literals, nonzero or floating static long-double initializers, integer conversions involving long double other than `_Bool`, remaining integer and floating conversions, nonempty identifier-list definitions, non-scalar arguments without declared parameter types, aggregate variadic reads, block assertions, variable-length arrays and runtime `sizeof`, the remaining GNU attributes, complete Cupid extensions, complete AST and IR coverage, broader function code generation, full translation-unit emission, and production integration remain later work. The shared hosted path owns the 155-source strict non-Doom cohort, all 83 Doom roots, the generated kernel symbol translation, and the six checked generated-install or user translations; the private kernel compiler remains the embedded runtime JIT and AOT path. ADR 0196 adds block-static address initializers, earlier static `const` integer reuse, automatic `long double` transport, and zero-filled static long-double objects without claiming the broader forms. ADR 0199 adds non-atomic long-double comparisons. ADR 0202 adds floating truth, controlling operands, and conversion to `_Bool` at all three represented widths.

The latest local normal build completed in 1,452.910 seconds. Its
8,609,188-byte
pass-one ELF has SHA-256
`384d69a9f1ebe5bbf228791af5519086a73767d2aafc24efbcf22fa696a9354b`;
the 8,719,780-byte final ELF has SHA-256
`5a7a491a39372697accff9b678054b4bf84e2e68ffc3e882c5ef815d570cee06`.
CupidObj flattened it to an 8,518,280-byte kernel with SHA-256
`ecde61e586fb69bf091e3586c7c0a90d65588a9d7aa22ea6cf7d2f48dc341df3`.
The fresh 209,715,200-byte image has SHA-256
`f488f54c023e6d1f7e9883be1f93f705fbdab4b1de3aab8a2b61b86f3863a085`,
and its bytes from offset 2,560 match the complete raw kernel. A private copy
completed `/bin/ls.cc` through the in-OS CupidC JIT in 54.025 seconds without
a panic marker.

The earlier static `const` integer rule is a narrow Cupid C extension rather
than an ISO C integer constant expression. It preserves the unchanged
`atomic_oracle_execute` address tables, which strict GCC and Clang already
fold, without making mutable or indirect object reads compile-time values.

ADR 0116 extends the entity-attribute list above with GNU `used` and
`__used__`. The metadata is canonical and validated, and the checked seed now
uses it for the production symbol-source recipe. ADR 0119 adds the exact
FXSAVE pointer input used in `process.cc`. That translation unit now passes
the complete checked profile and produces its normal object through CupidC.

ADR 0141 adds compiler-head semantics for GNU `noinline` and
`target("general-regs-only")`. Compatible declarations merge each fact into
one canonical function. `noinline` preserves the request for a future
inliner. Each IR function retains the canonical code generation mask. The
target form rejects compiler-generated floating work in Linear IR, and
object placement checks both the mask and invariant again. Explicit
assembly remains under its own contract, so an exact `FNINIT` statement is
valid. At that boundary, unchanged `kernel/cpu/fpu.c` passed its target
attribute and stopped at the independent `"m"(mxcsr)` input to `ldmxcsr` on
line 28. No checked seed or production source changed in that increment.

ADR 0146 advances that exact FPU frontier. The checked seed accepts the volatile
`ldmxcsr %0` form with one addressable, non-atomic 32-bit integer `m` input
and no outputs or clobbers. Linear IR evaluates the address once. The i386
emitter loads it into EAX and emits `0F AE 10` through the shared x86 model.
The deterministic two-function contract object is 400 bytes with 40 bytes of
text and no relocations.

ADR 0148 carries the unchanged source through that MOVSS round trip and the
matching one-way load and store. Each exact volatile form keeps one or two
typed `float` addresses and requires the `xmm0` clobber. Linear IR evaluates
each address once in source order. The shared x86 path emits
`F3 0F 10 00` and `F3 0F 11 00` through EAX. The deterministic three-function
contract object is 464 bytes with 79 bytes of text and no relocations.
ADR 0150 carries the next exact volatile block in `stress_sin()`. Its
modifiable `double` `=m` output and addressable `double` `m` input retain
typed addresses and permit no clobbers. Linear IR evaluates the output
address before the input address, once each. The shared x86 path emits
`FLD qword [EAX]`, `FSIN`, and `FSTP qword [EAX]` with balanced x87 depth
and no frame temporary. The deterministic two-function contract object is
440 bytes with 70 bytes of text and no relocations. Two full compiler-head
builds of unchanged `kernel/cpu/fpu.c` produce the same validated 6,620-byte
object. At the ADR 0150 boundary, production ownership had not moved. The
later transfer renamed the source to `kernel/cpu/fpu.cc` and placed its
unchanged 6,620-byte object under the checked normal wrapper.
The production contract also decodes the exact `fpu_init_cpu()` symbol. It
rejects helper calls and floating work before the CR4 write, requires one
`FNINIT` followed by one 32-bit memory `LDMXCSR`, and rejects any other
floating work in the function. Its negative object replaces the CR4 write
with three NOP instructions and fails before `FNINIT`.

ADR 0160 adds the exact volatile flags-restore form used twice by
`simd_cpu_has_cpuid()`. It takes one non-atomic 32-bit integer through `r`,
has no outputs, and requires exactly one `cc` clobber. The frontend and
Linear IR keep that clobber as public metadata. The emitter consumes the
evaluated value through EAX, pushes it back, and emits POPF through Cupid's
shared x86 model. ESP remains balanced, and the path needs no temporary or
relocation.

ADR 0168 adds compatible fixed-register input sharing with a write-only
output. The unchanged CPUID statement keeps `=a` for its EAX output and `a`
for its leaf input. CupidC records output zero in the input's
`matching_output` field without
replacing the source constraint. Frontend and Linear IR require the same
fixed register, represented integer types, and equal widths. Frozen
same-width pointer, floating, and aggregate inputs fail without publishing
IR or an object. The emitter repeats those checks, loads the evaluated leaf
into EAX immediately before CPUID, then snapshots all four outputs through
the existing EBX-preserving path. Numeric ties keep their existing behavior.
The checked seed now carries unchanged `kernel/cpu/simd.cc` past that overlap
and through all six packed SSE2 statement shapes. The frontend and Linear IR
lock the exact ordered pointer and 32-bit integer inputs plus each memory and
XMM0 through XMM7 clobber set. Emission uses Cupid's shared x86 model for the
copy, broadcast, blend, and saturating-add paths. Two checked-seed builds produce
the same validated 8,768-byte object with SHA-256
`fd280c321b8eb38a90d4f0982d70b8df0364585e3da322eb2c9de722e071f8d4`.
The normal recipe freezes the source and its seven-header closure before the
checked wrapper validates and publishes the object. ADR 0178 records the
packed SSE2 boundary, ADR 0179 records seed carriage, and ADR 0180 records
production ownership.

ADR 0154 represents the complete unchanged x87 round-down statement in
`str_floor()`. It requires one modifiable `double` `=m` output, one
addressable `double` `m` input, and the exact `ax` plus `memory` clobber set.
Linear IR evaluates the output address before the input address, once each.
After loading the input, the emitter reuses its consumed address slot below
ESP for the saved and temporary control words. The pending output address at
`[ESP]` remains intact. The 44-byte direct sequence saves the caller's x87
control word, selects round toward negative infinity, executes `FRNDINT`,
restores the original word, and stores the result with balanced x87 depth.
The two 71-byte fixture functions form a 524-byte object with 142 text bytes
and no relocations.

The exact unchanged `str_floor()` definition compiles twice to the same
420-byte object with SHA-256
`448012fe57ec625c6075e97cf91163b994a0443238c5d6bdf25e4b839763f14e`.
The checked seed also accepts the later explicit non-atomic `double` to
`uint64_t` casts. It divides by 2^32 to obtain the high word, reconstructs
that multiple exactly, subtracts it, and truncates the remainder to the low
word. The shared-decoder oracle covers the active range through the largest
binary64 value below 2^64. Two complete compiles of unchanged
`kernel/core/string.cc` produce the same 14,460-byte object with SHA-256
`d48bb6ea18b7124fbefeaca0d5d5ee8a517db950f21ea88e30ededd6c5c2a577`.
The normal recipe freezes `string.cc`, `string.h`, and `types.h`, then uses
the checked wrapper to validate and publish the object. A poisoned-host
rebuild reaches the same object without GCC, Clang, or a host assembler.
ADR 0170 records the conversion boundary, and ADR 0181 records production
ownership.

ADR 0175 represents the exact operand-free volatile statement that begins
the external, prototyped `void _start(void)` body in `.text.start`. It
requires the EAX, ECX, EDI, and memory clobbers plus visible external object
declarations for `_bss_start` and `_kernel_end`. Frontend statement depth and
Linear IR body identity reject a leading statement or a reset hidden by a
label or another nested body.

The checked seed reads the stack top from the exact statement. It accepts one
through eight hexadecimal digits, rejects zero or a value that is not aligned
to 4 KiB, and emits the parsed `imm32`. The function still cannot have a
compiler-managed frame. It copies ESP to EBP, loads both linker symbols,
derives the doubleword count, clears EAX, then emits CLD and REP STOSD through
the shared x86 model. Its following `kmain()` call uses stack-base residue
zero, and a returning `kmain()` reaches an interrupt-disabled halt loop. The
active source installs `0x01100000`, the top of the fixed two-MiB stack.

The exact fixture has 42 text bytes and three relocations. Its 27-byte
assembly body has `R_386_32` relocations at offsets 11 and 16. The
`R_386_PC32` call relocation is at offset 31. Two runs of the Cupid-built
compiler emit unchanged `kernel/core/kernel.cc` as the same 25,920-byte
object with SHA-256
`ed42676ad0d7f16b1fb83442ead1b0082781324dca719104922099cee34b5ab0`.
The normal recipe freezes the source and its 63-header recursive closure.
Poisoning `CC` leaves the recipe on the checked wrapper, and CupidDis decodes
the stack reset, linked BSS clear, `kmain()` call, and halt loop. ADR 0180
records production ownership. ADR 0185 records the variable, page-aligned
stack-top boundary, ADR 0186 records its checked-seed promotion, and ADR 0187
records the coordinated memory-map move.

ADR 0157 carries the four descriptor-table and segment-register assembly
forms in unchanged `kernel/smp/percpu.c`. The LGDT forms require one
addressable, non-atomic, complete six-byte `m` input and the exact `ax` plus
`memory` clobbers. The code-segment reload keeps its `memory` clobber. The GS
form requires one represented 16-bit `r` input. Linear IR lowers the packed
GDTR as an address and the selector as a two-byte value.

The shared x86 emitter writes the 48-bit LGDT operand, the AX immediate, and
the DS, ES, SS, and GS moves. Code-segment reloads use a relative
call-and-RETF trampoline instead of an absolute compiler-local label. The
fixture object is 528 bytes with 117 text bytes, five sections, five symbols,
and no relocations. Two complete compiler-head compiles of unchanged
`kernel/smp/percpu.c` produce the same 6,760-byte object with SHA-256
`3c2c6f0e00e5edec1ca16cba91e9fc593d1c42e24f4ebd3591e5f574fb0dd772`.
ADR 0157 recorded that compiler boundary against the `.c` source. The
production source is now `kernel/smp/percpu.cc`, and its object belongs to the
checked normal wrapper.

ADR 0155 gives file-scope GNU basic assembly its own frontend and Linear IR
tables. The checked seed emits the twelve exact x87/SSE floating wrappers at
the start of the then-named `kernel/cpu/libm.c` as source-ordered,
prologue-free global functions. The shared x86 encoder produces all 248 text
bytes, and the object
has no relocations. The checked seed accepts `[identifier]` labels on statement
operands and resolves `%[identifier]` to the existing numeric index before
public metadata freezes. The same lvalue, atomic, type, and constraint checks
apply to named and numeric operands, and `%%` remains escaped text.

The checked seed represents the complete x87 statements in `libm_pow_impl()` and
`libm_powf_impl()`. The double form requires one modifiable `double` output
and four addressable `double` inputs. The mixed form requires one modifiable
`float` output, two addressable `float` inputs, and two addressable `double`
inputs. Both require one memory clobber. Linear IR evaluates each statement's
five addresses once in source order. Each focused emitter proof contains 116
exact text bytes, no relocations, the corrected `DC E9` forward-subtract
encoding, and balanced x87 depth. The checked seed also retains the legacy
`DC E1` reverse subtraction for source compatibility. Those
blocks exposed the following
`sqrtsd` statement in `libm_sqrt_impl()`.

The checked seed represents that exact volatile square-root statement. It
requires one modifiable, non-atomic `double` `=x` output, one non-atomic
`double` `x` input, and no clobbers. Linear IR evaluates the output address
before the input value. The emitter uses XMM0 internally for `MOVSD`,
`SQRTSD`, and the final `MOVSD` store. The 65-byte focused function has no
relocations.

The checked seed also represents the exact volatile x87 statement in
`libm_atan2_impl()`. It requires one modifiable, non-atomic `double` `=m`
output, two addressable, non-atomic `double` `m` inputs in `y`, `x` order,
and one `memory` clobber. Linear IR evaluates the three addresses once in
source order. The 53-byte focused function has no relocations, and its
15-byte statement sequence comes entirely from the shared x86 model. The
full source then proceeds to the x87 exponent statement in
`libm_exp_impl()`.

The checked seed also represents that exact volatile exponent statement. It
requires one modifiable, non-atomic `double` `=m` output, two addressable,
non-atomic `double` `m` inputs in `x`, `log2e` order, and one `memory`
clobber. Linear IR evaluates all three addresses once in source order. The
71-byte focused function has no relocations, reaches x87 depth three, and
returns to its incoming depth.

The checked seed represents the following aligned mask block and the exact
`fabs` and `fabsf` wrappers. The mask effect reserves the first 32 bytes of
`.rodata` at alignment 16 and defines local `STT_NOTYPE` labels at offsets 0
and 16. Later read-only C objects follow the masks. The wrappers contain 15
and 14 text bytes and carry one `R_386_32` relocation each to the matching
mask.

The checked seed also represents the next eight file-scope rounding wrappers:
`floor`, `floorf`, `ceil`, `ceilf`, `round`, `roundf`, `trunc`, and
`truncf`. Each saves the caller's x87 control word, clears its rounding
field, selects the source mode, applies `FRNDINT`, and restores the original
word. The four pairs select down, up, nearest-even, and toward-zero mode. The
nearest-even pair emits no OR instruction. The family occupies 384 exact
text bytes, uses no relocations, reaches x87 depth one, and balances ESP and
x87 depth.

The checked seed also represents the exact `fmod` and `fmodf` definitions.
Each loads `y` below `x`, repeats `FPREM` while x87 status-word C2 is set,
and uses a checked short `JNE` with displacement `-10`. After convergence it
discards ST(1), returns the remainder through XMM0 at the source width, and
restores ESP and x87 depth. Both functions contain 35 exact text bytes and
no relocation.

The checked seed represents the aligned `libm_log2e_const` and
`libm_ln2_const` block and the following `exp2`, `exp2f`, `exp`, `expf`,
`log2`, `log2f`, `log`, and `logf` wrappers. The two local constants occupy
16 `.rodata` bytes at alignment eight. The wrappers add 264 text bytes. The
four natural forms have one `R_386_32` relocation each, while the base-two
forms need none. Decoder contracts check every instruction, each operand,
x87 depth, and ESP balance. The full source then proceeds to `pow` at line
846.

The checked seed also represents `pow`, `powf`, `asin`, `asinf`, `acos`,
`acosf`, `sinh`, `sinhf`, `cosh`, `coshf`, `tanh`, `tanhf`, `cbrt`,
`cbrtf`, `hypot`, `hypotf`, `nextafter`, and `nextafterf`. Each wrapper
copies its one or two original cdecl arguments, calls the matching external
`libm_*_impl` function, reclaims the copied words, and moves the ST(0)
result into XMM0. Shared emission covers four stack shapes and adds 558 text
bytes with 18 `R_386_PC32` relocations. Each relocation has a known `-4`
addend. The decoder checks the argument copies, call, cleanup, result
bridge, return, ESP balance, and x87 balance.

Two exact kernel-profile compiles of the corrected
`kernel/cpu/libm.cc` produce the same valid 16,164-byte ELF32 relocatable
object with SHA-256
`c0911732361f2e1ea78aa778f834719ba12208cc2d9f0a312455a5e6a38a75b4`.
General file-scope GAS still fails at the CupidC boundary.

Production now uses `kernel/cpu/libm.cc`. The checked wrapper freezes its
43,736 bytes with `kernel/core/types.h` and `kernel/cpu/libm.h`. The source
has SHA-256
`baffe801c7573b8500c60251298a753f60732608d58443178be8ce9ab809ef93`.
Seven aligned GNU mnemonics now emit `DC E9` without changing the algorithm,
stack order, source size, or ABI. The runtime gate launches
`/bin/feature15_libm.cc` and requires the seven-case x87 summary, all 29
checks, and `PASS feature15_libm`. ADR 0159 records the naming boundary. ADRs 0161
through 0165 record the five statement blocks. ADR 0166 records `fabs`, ADR
0169 records rounding, ADR 0171 records remainder, ADR 0172 records
exponent/log, ADR 0173 records the final cdecl bridges, ADR 0174 records
checked-seed carriage, ADR 0176 records production ownership, and ADR 0209
records the active range-reduction correction.

ADR 0156 represents the naked interrupt entries in unchanged
`kernel/smp/smp.c`. A naked function must have type `void (void)` and contain
one complete assembly statement. The reschedule and call wrappers accept
exact `pushal`, a direct canonical C-function call, `popal`, and `iret`
sequences. The panic wrapper accepts exact `cli`, `hlt`, and a relative jump
back to the halt instruction. The i386 emitter omits every compiler-managed
frame and return instruction. Cupid's shared x86 model emits each eight-byte
call wrapper with one `R_386_PC32` relocation and the seven-byte panic loop
without a relocation. Two complete compiler-head builds produce the same
validated 8,444-byte object with SHA-256
`806509a6dd1ac7eb34b7ffcb67a1f8852950663a274145584d0260da76dcba54`.
That hash records the earlier `.c` path. The checked production source is now
`kernel/smp/smp.cc`; its 8,444-byte object has SHA-256
`bd3189b2a1a6d15728c559172f5d6acca0889103428085cec8cc1024742a22d1`.
The difference comes from the existing `__FILE__` diagnostic. The wrapper,
image, and four-vCPU dual-NIC runtime gates pass.

ADR 0121 adds the three machine-state memory outputs used by the FPU panic
path. Exact volatile `fnstsw %0` and `fnstcw %0` templates accept one
modifiable 16-bit `=m` output, while `stmxcsr %0` accepts one modifiable
32-bit output. Linear IR evaluates the lvalue once. The i386 emitter consumes
that address directly and encodes the instruction through the shared x86
model, without a register-output staging area. Other `=m` templates remain
unsupported. `kernel/core/panic.cc` uses these statements, and the checked
compiler also supports its later exact call-next template.
Two complete kernel-profile compiles produce the same validated 10,212-byte
ELF32 object with SHA-256
`84daa51a65d6970ae7a7918b05fe64b7676c39d3309264375e349cf0ae20d428`.
The checked seed carries this capability, and the normal panic recipe uses it.

The body operator ladder uses checked value/operator vectors and an iterative precedence reducer. A recursive wrapper per precedence tier was rejected after the established 256-deep nested-call contract exhausted the default Windows host stack before reaching its transactional syntax-limit diagnostic. The iterative reducer keeps binary chains bounded by job storage while calls, parentheses, unary nesting, and assignment continue to consume the explicit 256-level budget. A 4,096-operator flat addition oracle publishes 8,193 left-associated postorder expressions and 8,192 child references; a 256-byte output ceiling on the same source proves one limit diagnostic, complete arena/scratch rollback, tape and prior-result preservation, and same-job recovery. High-bit ordinary character bytes sign-extend into signed target `int`; compatible enum/integer assignment retains an explicit destination conversion; and freeze requires every assignment's `computation_type` to equal its result type. Valid decimal normal `float` and `double` constants now publish exact bits. Hexadecimal, subnormal, and `long double` constants and floating logical operands retain distinct unsupported diagnostics.

The call subset accepts fixed prototypes, direct or indirect variadic prototypes, and function types without prototypes. It applies the shared scalar or compatible aggregate assignment conversion to named parameters. Ellipsis arguments and every argument without a declared parameter type receive lvalue conversion, array and function decay, integer promotion, and `float` to `double` promotion as required before IR accepts four-byte integers and pointers, signed or unsigned eight-byte integers, `double`, or automatic `long double`. Aggregate transport at those call boundaries remains open. Lvalue conversion removes top-level `const`, `volatile`, and `_Atomic` from the result while retaining the qualified source child, and nested calls consume the shared 256-level syntax budget instead of recursing without a host-stack bound.

[CupidDis](../adr/0008-typed-cupiddis-inspection-report.md) is fully shared between its native CLI and kernel adapters. Raw input accepts one explicit 16-bit or 32-bit mode or an ordered borrowed range map whose kinds are code16, code32, and data. The hosted CLI spells typed transitions as `--range-at OFFSET:16|32|data` and keeps `--mode-at OFFSET:16|32` for code-only maps. Code ranges use the shared decoder. Data ranges produce bounded `db` rows without entering it. A public integration test assembles the active SMP trampoline and checks code in `[0x000, 0x01f)` and `[0x210, 0x254)`, with data in `[0x01f, 0x210)` and `[0x254, 0x1000)`. ADR 0080 records the original mode map, and ADR 0200 records its typed extension. The shared x86 catalogue carries all sixteen i686 `CMOVcc` conditions in 16-bit and 32-bit widths, with same-width register or memory sources. CupidASM accepts fourteen conventional alias spellings, and CupidDis always renders the canonical condition. The catalogue also carries the complete 16-bit and 32-bit three-operand `IMUL` family. It uses `69 /r` for a full immediate and `6B /r` for a sign-extended byte, with register or memory sources in either mode. Ordinary compiler padding now shares that authority: plain `90`, `66 90`, and word or doubleword `0F 1F /0` register and memory forms encode and decode under the usual operand-size, address-size, and segment rules. A private 32-bit recognizer accepts only the five measured Clang padding strings with two through six `66` prefixes and the exact `2E 0F 1F 84 00 00 00 00 00` tail. The decoded form is automatic, so CupidASM and the encoder cannot request redundant prefixes. Other duplicate prefixes remain invalid. The checked seed carries all 592 rows, 244 canonical mnemonics, 64 registers, and fingerprint `F4420CB4`; the private recognizer does not change the catalogue. ADR 0083 records the conditional-move boundary, ADR 0132 records immediate multiply, ADR 0143 records ordinary padding NOPs, ADR 0144 records the exact Clang exception, and ADR 0208 records the current catalogue's seed carriage. One freestanding CupidASM implementation produces raw, ELF32 relocatable, and fixed-image artifacts for both its hosted CLI and the in-kernel JIT/AOT commands; it owns all four production assembly transforms as well as runtime demo assembly. CupidObj is the code-producing owner for 185 normal-build outputs: 172 canonical text wraps, eight byte-exact binary wraps, one Python-assisted JPEG wrapper, the flat kernel image, and three installation-source generators. ADR 0084 records the text and binary boundary, and ADR 0204 records the source-generation transfer. CupidLD owns the two-pass kernel link and all three separate user-program links. No standalone host assembler, ELF linker, `objcopy`, or symbol-reader command produces an OS or user artifact. CupidDis owns the normal two-pass kernel's symbol extraction through its deterministic `-n` view; the checked pass-one kernel produces 4,561 consumed text symbols and a 109,889-byte blob. Python still serializes the generated Cupid C blob. Root `all` runs all four production Cupid commands from the checked seed. The host C compiler and native linker remain confined to explicit native oracle and development commands. The checked static i386 CupidC command participates in all 245 active C transforms through the Python/WSL wrapper. It also executes the three user translations, while checked CupidLD executes their three links. The optional native Windows drivers remain byte-exact oracles and still depend on Clang and its Windows linker. NASM and GNU/LLVM `nm` remain optional oracle tooling only. ADR 0190 records the root handoff. Checked revision `1e079d1` independently reproduces the 447-artifact root/user/toolchain cohort on Windows Clang/LLVM and Linux GCC/binutils; it predates the hosted preprocessor and active-corpus contracts.

The trampoline intervals above are half-open: code occupies
`[0x000, 0x01f)` and `[0x210, 0x254)`, while data occupies
`[0x01f, 0x210)` and `[0x254, 0x1000)`.

Source head and the checked seed carry 592 rows, 244 canonical mnemonics, and
fingerprint `F4420CB4`. The newest row is canonical `FSUB ST(1), ST(0)`,
encoded as `DC E9`, for the corrected GNU `fsubr %st, %st(1)` exponent range
subtraction. The four forms added in the preceding seed are the
80-bit x87 `FLD` and `FSTP` memory forms, the i686 `FUCOMIP ST0, ST(i)`
register form, and operand-free `FLDZ`. Both checked stages rebuild that
catalogue before compiling the Toolchain contract cohort. ADR 0203 records
the preceding seed promotion, ADR 0207 records the forward-subtraction
boundary, and ADR 0208 records its seed carriage.

ADR 0196 supersedes that paragraph's hosted-contract ownership sentence. The
normal Toolchain contracts are now built by the checked i386 CupidC and
CupidLD path. GCC or Clang and a native linker are used only when an explicit
native oracle or development command is requested.

A block type name or record member may either reuse a visible enum tag or define a new one. New enumerators keep their exact lexical activation point through ADR 0062 ownership records.

## Records

- `LOG.md` is the chronological bootstrapping log. Add an entry for every completed implementation step, failed approach, user answer, important decision, and test run.
- `HOST-DEPENDENCIES.md` records every external build dependency and whether it belongs in the final normal build.
- `CAPABILITY-MATRIX.md` records implemented and missing CupidC, CupidASM, CupidDis, object, linker, and bootstrap capabilities.
- `MIGRATION-MATRIX.md` records which tool owns each source and artifact cohort today and at the self-hosting fixed point.
- `BASELINE.md` documents the reproducible oracle-build interface and evidence format.
- `ACTIVE-SOURCE-AUDIT.md` is the generated human summary of the root OS image, separate user-program, and hosted toolchain build roots, including ownership, source features, ABI requirements, unreachable files, and source-driven priorities.
- `audits/active-build.json` is the deterministic machine-readable companion. Regenerate it with `make bootstrap-audit`; `make test` and `make check-bootstrap-audit` reject drift or a failing audit contract.
- `../adr/` records stable architectural decisions; `../../CONTEXT.md` defines project vocabulary.

## Update contract

Every toolchain implementation commit must update the affected records here and include relevant positive and negative tests. Claims in these files must distinguish source inspection from executed verification. The `TempleOS/` reference tree is excluded from all progress metrics. Generated objects, images, and logs are excluded from source-migration counts and ordinary commits unless they are intentional bootstrap inputs such as checked seeds; their hashes, ownership, layout, and runtime behavior remain required acceptance evidence.

Progress means transferring ownership without reducing Cupid OS behavior:

1. A Cupid tool gains the real feature required by an active source cohort.
2. Tests prove successful behavior and useful failures.
3. The cohort moves from the legacy host/oracle path to the Cupid path.
4. The OS build and applicable boot smoke tests remain green.
5. Host dependencies are removed from the normal build only after the replacement path is proven.
