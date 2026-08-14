# Toolchain ownership migration matrix

`TempleOS/` is excluded: it is reference material, not a source cohort. Statuses describe ownership, not how much code exists.

Root `all` now runs CupidASM, CupidObj, CupidLD, and CupidDis from a
manifest-bound five-tool seed selected for the host. Linux uses the static
bootstrap seed. Windows uses the native PE32 execution seed for output-bearing
commands. The checked runner freezes the full trust unit
for each command and checks the live cohort again afterward. Checked
production kernel, generated-install, and user CupidC calls plus checked user
CupidLD links use the same runner with the capture their wrapper already
froze. Drift detected by the post-run check rejects the result before
publication. Make passes every wildcard-discovered output source through
`$(sort ...)` before generation or link, giving Windows and Linux the same
root order regardless of host locale.
Native hosted commands remain explicit oracle targets, but none is reachable
from a supported root. The three-root audit has 452 transforms, no recursive
Make transform, 452 Python participants, 246 CupidC participants, one
Cupid-built ABI-contract participant, and no host C transform. `toolchain:all`
uses the checked seed and both rebuilt compiler stages to compile and link
all fifteen `.cc` Toolchain contracts.
CupidObj participates in 192 transforms, including three installation-source
generators, the kernel-symbol source generator, the normal disk-image
template, repository ISO fixture, and Doom profile manifest. Root `all` has
443 transforms: 442 artifact transforms with a Cupid tool owner plus the
Python-only size verifier, which emits no OS artifact. CupidDis participates
in six root transforms: kernel symbols, the SMP trampoline, the bootloader,
the ISR object, the context-switch object, and `kernel.bin`. The `kernel.bin`
transform runs strict validation and flat extraction against one frozen
cohort, with all 431 code inputs represented.
ADR 0190 records the root handoff, and ADR 0196 records the contract handoff.
ADR 0238 records the disk-image transfer, ADR 0241 records the ISO transfer, ADR
0244 records the profile-manifest transfer, ADR 0245 records publisher-owned
output directories, ADR 0246 records the shared invocation boundary, and ADR
0264 records the syscall ABI contract transfer.

The hosted CupidC driver now exposes the shared frontend's Cupid profile as
`--cupid`. Both preprocessing and parsing receive the same language mode.
`--gnu` remains independent, Doom compatibility remains separate, and C11 is
still the default. The public path compiles the unchanged SIMD declaration
surface and matches the Cupid-built driver byte for byte. ADR 0270 records the
driver boundary.

The SMP trampoline now adds strict CupidDis participation to its existing
CupidASM ownership. One hostbuild transaction freezes the selected seed and
source, assembles a private 4 KiB candidate, checks its mixed code/data map,
and publishes atomically only after `--require-known` succeeds. ADR 0271
records that transfer.

The ISR and context-switch objects now receive the same protection at their
own publication boundary. Hostbuild freezes each source and the complete
five-tool seed, validates the private i386 relocatable candidate, requires a
nonempty executable section, and asks CupidDis to decode every executable
byte. Drift or a failed check preserves the previous object. The final kernel
gate remains as a second whole-image check. ADR 0286 records this transfer.

Source head adds three ownership seams without weakening an active source.
Hosted CupidC probes fixed frames larger than 4 KiB one page at a time.
Kernel CupidASM AOT now emits `ET_REL`, reports its selected entry symbol, and
hands final placement to in-kernel CupidLD. Raw CupidASM can also serialize a
source-derived code and data map that hosted CupidDis consumes directly. The
shared checked raw-image transaction now serves both SMP and bootloader callers.
It owns locking, freezing, drift checks, private candidates, and atomic
publication, while callers keep their image and map policies. Its central
eleven-test suite passed in 1.708 seconds. It includes direct mismatch and
live-output drift checks for both callers. Parent-replacement tests exposed a
POSIX candidate leak when private work lived below the output parent. Private
roots now live directly below the stable repository root. Both caller modules
pass all 10 tests on Windows and all 10 through WSL, including parent
replacement with no leaked candidate. The normal boot edge now enters the
guarded publisher through hostbuild with the production manifest and full
checked-seed closure. The promoted Windows execution seed carries both map
options. The guest
AOT smoke passed in 79.661 seconds with a
15,680-byte `ET_REL` object and an 8,536-byte linked ELF. ADRs 0275, 0276, and
0277 record these boundaries.

The public native Windows driver freezes the PE execution seed and Linux plan
seed as separate trust roles and builds through stage four. It compares stages
three and four, runs behavior on both, and publishes transactionally. Under the
old comparison, source-stable Windows and Linux runs stopped safely at
`cupidobj_main` after 821.9 and 883.3 seconds. Neither run published. Later
uncapped runs passed
the final-pair comparisons. Windows matched 20 C objects, two assembly objects,
and five tools in 20 minutes 43 seconds with 5/5/5 behavior cases. Linux
matched 19 C objects, startup, and five tools in 24 minutes 22 seconds with
5/18/16 behavior cases. Both reports bind the same 50-input snapshot, SHA-256
`d8481a39e0d1c7f42779a8c9f5fc5de10d7e5b9bc4df63ce6afe9ddd9c9716da`.
Those reports began from uncommitted source and remain preliminary. Linux then
passed a clean proof in 1,294.3 seconds, promoted the stage-four cohort, and
passed a 1,473.9-second reproof with all five initial seed comparisons true.
Native Windows passed its clean proof in 1,253.4 seconds and promoted the
stage-four cohort. The previous seed comparison was false for CupidASM, CupidC,
and CupidDis and true for CupidLD and CupidObj. Its 1,061.3-second reproof
matched all five initial seed images. The Linux and Windows behavior matrices
are now 5/18/17 and 5/5/6; both reject unmatched executable relocations. ADRs
0278 and 0279 record the driver and added generation. ADRs 0280 and 0281
preserve the preceding promotions, and ADR 0292 records the current promotion.

A preliminary Linux behavior reconstruction matched four direct native PE tools but
found a CupidDis profile difference. Both CupidDis images were 387,584 bytes;
the Linux reconstruction had SHA-256
`ad6147cd426e204756ec8bf52ae85c64fff9ad39b0bc26e5744f3c421be1e9aa`,
and the direct Windows proof had SHA-256
`07cff807224c425d686e32d54dc1ad541f57aaa624f7b736bba0f9ef5001ce6a`.
The reconstructed plan had compiled `cupiddis_main.cc` without `_WIN32=1`.
The Windows profile now covers all five driver mains, with compile-and-link
parity and an audit guard.

The five promoted stage-four PE32 images form the checked Windows execution
seed. Its 2,118-byte manifest has SHA-256
`ae1d3dfb10604bba419c5936884668d10595f6c671915a4ae5f16706204bb41e`.
Windows output-bearing recipes select it directly, while Linux fixed-point,
Toolchain contract, user ABI, and artifact-size paths keep the Linux bootstrap
seed. Native Windows reconstruction pairs the PE execution seed with that
verified Linux plan. Each PE reserves and commits a one MiB stack. Its heap
reserves one MiB and commits 4 KiB. The independent format reader checks these fields, and the
native compiler must compile the unchanged keyboard driver to the same object
as the Linux seed. ADR 0272 records the role split, ADR 0274 records the stack
policy, ADR 0281 records the preceding clean promotion, and ADR 0292 records
the current promotion.

`verify-artifact-sizes` is a direct prerequisite of `cupidos.img`. It receives
`$(BOOTSTRAP_SEED_MANIFEST)`, derives the five seed paths and declared sizes
from that selected manifest, and requires the policy to agree. It also checks
the five-sector boot image, both kernel ELFs, and the raw kernel. Host Python
validates the policy and files but produces no artifact. A failure prevents
image publication and preserves the existing image. ADR 0267 records the
policy.

Checked-seed CupidLD can also serialize one deterministic fixed-layout i386
PE32 console image and its canonical imports. Checked-seed CupidASM and CupidC
provide a small freestanding Windows command, and Windows loads the CupidLD
image directly. Import ordering is bounded by an in-place heap, name imports
stay below the PE32 high-bit boundary, and an independent parser reconstructs
the exact fixed header, stack and heap fields, and `.idata` layout. That format
proof now underpins the checked Windows execution seed without changing
artifact ownership counts. The promoted Linux seed carries the 5/18/17 matrix.
ADR 0247 records the original format boundary, ADR 0248 records imports and
loader execution, ADR 0258 records the preceding seed, ADR 0265 records its
carriage, and ADR 0274 records the current stack commitment.

The same checked-seed CLI publishes ELF and PE images through an adjacent
candidate created with exclusive-create semantics. It writes and closes the
candidate, then reopens the file and checks its size and contents before one
replacement call. On POSIX, CupidLD requests mode `0777`; the process umask may
remove any permission bits. This standalone write path remains outside
production ownership. It requires a caller-controlled
stable directory and has no destination lock or directory pin.

Checked-seed CupidObj also has a bounded `iso-fixture` operation. It matches the
tracked ECMA-119 and `RRIP_1991A` bytes from a manifest and typed input
inventory. The normal recipe now runs that command before Python renders the
same frozen snapshot independently and controls publication. ADR 0239 records
the source capability, ADR 0240 records seed carriage, and ADR 0241 records
production ownership.

CupidObj now has a self-hosted public operation for the bin, docs, and demos
installation-table formats. It validates typed path groups, preserves sorted
caller order, and matches the Python generator across the complete live
inventory. The checked seed now carries the operation, while the normal
recipes invoke it through the checked-seed runner. `tools/hostbuild.py` remains
the parity oracle but no longer owns or supplies these three outputs. Python
still participates in all 452 transforms because it launches the checked
tools. ADRs 0201, 0203, and 0204 record the operation, seed promotion, and
production transfer.

Checked-seed CupidObj also accepts canonical CupidDis symbol text and emits the
exact packed kernel-symbol `.cc` source. The fixed point exercises this
command, useful failures, and recovery, while a real producer-to-consumer
contract protects the text seam. The normal recipe now passes CupidDis's exact
text to CupidObj and keeps the Python renderer as an independent parity oracle.
ADRs 0222, 0223, and 0224 record the capability, seed promotion, and production
transfer.

The normal Make image recipe passes the checked five-tool seed manifest to
`tools/hostbuild.py image`. Checked CupidObj authors the pristine FAT16
template first. Python builds an independent oracle from the frozen bootloader
and kernel, and any byte difference stops publication. Hostbuild preserves a
valid existing FAT filesystem or uses the complete template for a fresh
image, stages the frozen files, extends the candidate, and rechecks the seed,
inputs, and live output. A cross-process lock and atomic replacement protect
the published image. ADR 0238 records the production boundary.

All seventeen tracked `.c` files outside `TempleOS/` are outside supported
transforms. The machine-readable census records seven historical copies, three
superseded implementations, one dormant runtime draft, five native host test
fixtures, and one optional host oracle. No active Cupid-built source needs a
`.cc` rename. Renaming a `bin/*.c` copy would activate it through the wildcard
build inventory, while renaming a host fixture would silently select C++
semantics. The build audit rejects an active tracked `.c` source if the graph
assigns it to CupidC, and it does not treat `.cc` as proof of the reverse
claim. Checked compile or Toolchain contract edges prove 275 active sources.
An exact policy records the other 130 source-text deliveries, all seventeen
residual `.c` paths, and the three unreachable `.cc` paths. A `.cc` rename
still follows a checked build and behavior proof. Active evidence is mandatory
in every audit, including trees without a policy file. A nonproduction audit
accepts policy, a recorded source relation, or an explicit Make exclusion for
an unreachable `.cc`. The complete production graph requires exact policy
coverage, while a partial production view defers that census. The safe
suffix-only rename set is empty. ADR 0284 records the first gate, and ADR 0291
records the independent provenance contract.

Checked-seed CupidC represents GNU `returns_twice` and preserves live operands
across supported direct calls. It rejects marked-function pointer conversion
and any live-prefix site reachable from a returns-twice continuation. The
active 31-byte setjmp form records `ESP + 4`, the stack state after a normal
return. Doom now uses that checked-seed capability in its shell-session exit
envelope. Asset-free lifecycle checks cover direct longjmp, repeated quit,
repeated error, callback order, and cleanup between launches. ADR 0212 records
the compiler boundary, ADR 0213 its promotion, and ADR 0214 active adoption.

The shared CupidASM source path now owns explicit alignment for raw, ELF32,
NOBITS, and fixed-image output. The active FPU demo declares its 16-byte
FXSAVE requirement with `align 16`; it no longer depends on being the first
object in `.data`. This expands the language without changing any source
owner, build transform, or host dependency. ADR 0197 records the decision.

The first Windows and Linux comparison matched 426 of 430 kernel artifacts.
All four differences followed one JPEG object that host FFmpeg had produced
differently from the tracked progressive source. The repository stores
the accepted sequential baseline bytes. Hostbuild freezes the source and runs
checked CupidObj `wrap-jpeg`, which accepts sequential SOF0 or SOF1 input and
rejects progressive, unsupported, or malformed frames. Python checks only the
accepted private snapshot and requires unchanged bytes. The normal path no
longer depends on FFmpeg, `jpegtran`, `djpeg`, or `cjpeg`. The Linux kernel
build passed in 607.7 seconds, and the Windows root build passed in 341.6
seconds. All 430 frozen kernel artifacts match byte for byte. A fresh normal
image passed a private `/bin/ls.cc` JIT boot in 49.8 seconds.

Python rechecks the manifest and source before atomic publication. ADR 0231
records the capability, ADR 0234 records seed carriage, and ADR 0235 records
the production transfer.

The private production compiler now accepts runtime unary plus and minus for
`char`, `int`, `float`, and `double`. Floating negation toggles only the
IEEE-754 sign bit in XMM0, which preserves negative zero and every other
payload bit. `/bin/feature13_double.cc` proves both widths, signed zero, unary
plus, a non-arithmetic operand diagnostic, recovery, and clean JIT
completion. ADR 0189 records the boundary. This capability does not change
build ownership. The runtime gate now exempts that exact diagnostic only once
inside the completed feature command. Stale, repeated, or out-of-context
compiler errors fail. A separate host oracle compiles the active emitter
helpers and interprets their instruction bytes across binary32 and binary64
payloads.

The private compiler also accepts all six scalar floating comparisons.
Matching widths compare directly, and a mixed `float` and `double` pair
compares as `double`. The result is a normalized `int`; explicit parity
handling makes only `!=` true for NaN. A host byte oracle and the
`feature13_double.cc` four-vCPU guest command cover ordered values, mixed
widths, signed zero, and unordered inputs. ADR 0192 records this boundary.
It changes private JIT and AOT behavior, not build ownership.

Private CupidC also materializes floating truth for unary `!`, `if`, `?:`,
`while`, `for`, and `do ... while`. Both signed zero encodings are false.
Finite nonzero values, infinities, and NaNs are true. One exact-byte oracle
checks binary32 and binary64 payloads, and `feature13_double.cc` runs every
truth-consuming parser site in the four-vCPU guest frontier. ADR 0193 records
the boundary. This is another private JIT and AOT capability, so ownership
counts do not change.

The kernel binding table preserves every declared result type. Its 557
registrations split into 326 typed value results and 231 verified `void`
results. The value group contains 208 promoted integers, 41 unsigned words,
25 `float`, 25 `double`, 19 character pointers, and eight other pointers.
Explicit `uint32_t`, `size_t`, and `swap_handle_t` results use the unsigned lane;
`uint8_t` and `uint16_t` retain integer promotion. A complete source-contract
test prevents a non-void declaration from using the untyped macro and checks
the exact Cupid type on every typed entry. This repairs private compiler
semantics without moving a source file or adding a host dependency.

Forty-six of those registrations expose the linked graphics effects,
bitmap-font assets, transforms, GUI initialization, and theme APIs required by
`gfxgui_test.cc`. Three pointer-returning accessors expose existing constant
themes; the other 43 entries call existing functions. The resulting census
passes private AOT compilation for all 107 runnable top-level programs. The
fixed guest frontier runs the graphics test through AOT and private JIT,
then exercises nested ownership through both voluntary exit and remote kill.
The exit leaves a generation-bound request that must reject the reused PID;
the replacement owner's helper kills only the new generation, and a final AOT
graphics run reuses that PID. It rejects an unresolved symbol before later output can
hide it. It requires
theme and BMP setup, an exact custom-font pixel, an isolated blurred-surface
pixel with unchanged screen state, center and off-center transformed-image
pixels, an off-origin rotation and scale point, identity after popping the
transform, frame 240, cleanup, and JIT return. The affine inverse keeps the
full 32.32 determinant and translation arithmetic in checked 64-bit form.
Disposable test artifacts
stay in RamFS. The later GodSong interaction waits for its settings line and
the popup's post-acquisition input marker. It uses no timed settle or
startup-only graphics diagnostic. This changes no normal build owner or host
dependency. ADR 0261 adds the PID-tagged handoff that serializes desktop,
retained, legacy, and fullscreen writers. Process reaping releases abandoned
ownership before PID reuse, and delayed kills capture the target generation.
Fully published graphics resource handles still
lack an owner tag, so abrupt termination can leak a finite pool slot.
ADR 0233 records the binding boundary.

Private CupidC now updates scalar floating lvalues through all four prefix and
postfix forms. The expression parser, statement shortcuts, and `for` increment
parser share one typed helper. Locals, parameters, and globals use direct
storage. Pointer dereferences, indexes, and scalar record fields keep one
evaluated address until the updated XMM0 value is stored. Postfix updates
preserve the exact old payload in XMM2. The guest retains its direct-variable
marker and adds `[feature13-indirect-update] PASS score=41 once=3
zero=0x80000000` for derived lvalues. ADR 0194 records direct updates, and ADR
0273 records derived updates. Build ownership does not change.

Private CupidC callers and callees now share four-byte and eight-byte cdecl
slots. A call evaluates arguments from left to right, then permutes complete
words into source-order stack positions. This supports arbitrary mixtures of
represented scalar and pointer values with `double`, including the implicit
method `self` slot. Later callee offsets and caller cleanup use the same total.
The feature13 guest uses a real mixed-width helper ten times. This changes
private JIT and AOT behavior without moving a build owner. ADR 0198 records
the boundary.

A direct function or method with parsed fixed parameter types now converts
represented integer, `char`, `float`, and `double` arguments to the declared
four-byte or eight-byte slot before the call. Character operands also follow
integer promotion and the scalar integer-to-floating conversion path.
Represented pointer categories and integer null forms can also fill a pointer
slot. A represented object pointer can fill a fixed `int` or `unsigned int`
slot as one unchanged i386 word; narrow and floating destinations remain
rejected. Calls to a parsed variadic function widen a tail `float` to
`double` and promote a tail `char` to `int`. Function-pointer calls, kernel
bindings, and calls without fixed parameter metadata keep their source-width
behavior. ADR 0230 records the object-address convention.

The Browser JavaScript runtime keeps numeric tokens and AST number nodes in a
binary64 lane. It accepts decimal, hexadecimal, binary, and octal literals and
valid separators between digits. Primitive string conversion consumes the
whole input after trimming the ECMAScript whitespace set and covers decimal
exponents, unsigned radix forms, signed `Infinity`, empty text, invalid text,
and `undefined`. Primitive equality, UTF-16 string order, IEEE remainder, `%=`
and string `+=` use the same runtime values. Concatenation can fill the
remaining 64 KiB string pool and reports exhaustion. Assignment records its
binding, member receiver, or computed key once, before the right side runs, and
stores through that identity. Side-effecting member and index tests cover both
new compound operators, while 1,100 plain writes prove stack balance. String
interning reserves a complete slice before publishing runtime state, and a
failed global install blocks queued scripts. Native function IDs survive a
round trip through a user function. Canonical array writes grow the unsigned
`length` lane through index 4,294,967,294. Direct length assignment fails
explicitly, while 4,294,967,295 remains an ordinary property. Finite
formatting covers large plain integers and small scientific values without a
signed 32-bit narrowing. Ten malformed literal families receive specific
diagnostics, and the next valid script proves recovery. The asset-free
`browser --selftest` command reports 26 computed fields before clean CupidC
completion. ADR 0210 records the first binary64 slice; ADR 0218 records the
expanded lane.

That active script exceeds the private compiler's former 1,023-byte joined
string buffer. CupidC now streams adjacent string tokens into one data object
for automatic expressions, file-scope initializers, and persistent REPL
declarations. Each token remains capped at 1,023 decoded bytes; the joined
value can use the remaining 8 MiB data section. Focused errors cover a longer
single token and joined-data exhaustion. This changes private JIT and AOT
behavior without moving a build owner. ADR 0218 records the boundary.

The saved Browser reference is declared with a tagged structure typedef.
Private CupidC now accepts that ordinary form as well as anonymous structure
typedefs. It keeps the structure index through alias chains and pointer aliases
in the normal parser and persistent REPL. One shared field parser gives both
forms the same offsets and completion checks. This expands private source
acceptance without moving a build owner. The same parser now checks positive
array counts, count-by-stride products, cumulative record padding and size,
final record alignment, REPL data capacity, and cumulative local frames before
allocation. Signed constant expressions reject overflow; an unsigned operand
uses represented `uint32_t` wrap. Decimal and hexadecimal integer literals
cover `UINT32_MAX`, hexadecimal literals require at least one digit, and the
suffix counts inside the 95-character token boundary. Persistent REPL
checkpoints include the
complete structure table, so a failed line cannot fill an older forward tag.
Member address expressions now preserve the selected lvalue. The value form
starts at the record object, and the pointer form loads the pointee before it
adds the field offset. Private execution writes through both forms without
changing adjacent fields; an unknown member still fails during compilation.
ADR 0219 records the boundary.

Private typedef declarations now accept several value or pointer aliases and
retain one-dimensional fixed-array shape per alias. Automatic, global,
block-static, record, class, and persistent REPL objects receive the complete
checked allocation, while function and method parameters decay to element
pointers. Array members keep their complete `sizeof` result and record-element
identity through direct and pointer access. One lvalue walk continues from an
indexed array field to a record member, including when the outer record is
itself selected from an array. This expands private source acceptance without
moving a build owner. ADR 0220 records the boundary.

Private four-byte unsigned values now survive objects, pointers, parameters,
calls, enum symbols, unary and conditional expressions, `sizeof`, and scalar
returns. Relations, division, remainder, and right shift use unsigned i386
behavior. `/=`, `%=`, and `>>=` keep the promoted left operand's signedness
and evaluate the destination once. The complete `uint32_t` range converts
correctly to `float` or `double`, including ordinary and method returns.
Values in C's defined interval convert from either floating width through
casts, initialization, assignment, arguments, and returns. Forty kernel
binding results publish this lane from their local `uint32_t`, `size_t`, or
`swap_handle_t` declaration. Browser uses it for the complete ECMAScript array
index and length range. This changes private semantics without moving a build
owner. ADR 0221 records the compiler and Browser boundary, and ADR 0249
records the completed conversions and remainder assignment.

The five Browser tables require typed private CupidC storage and indexed
access. Global, automatic, block-static, and persistent REPL `float` and
`double` arrays now carry their width through one, two, or three dimensions.
Depth-one floating pointers keep their pointee type through address
expressions, returns, function and method array-parameter decay, dereference,
subscripts, direct pointer updates, floating assignment, and floating
increment or decrement. Structure and class objects, object arrays, and object
pointers keep scalar floating fields and one-dimensional fixed floating field
arrays. Unevaluated
`sizeof(array[index])` reports the remaining row size without running the
index. Checked bounds and size arithmetic guard every fixed allocation, and
fresh expression metadata prevents stride leakage. Derived floating updates
retain that metadata, evaluate their pointer, index, or member destination
once, and keep the original raw postfix payload. Floating pointer depth greater
than one, pointer-to-array types, and assignment through a pointer-valued
floating record field remain unsupported.
This changes private JIT and AOT behavior without moving a build owner. ADR
0210 records the first typed-array slice; ADR 0215 records the expanded
floating lvalue boundary; ADR 0273 records derived floating updates.

Private `float4` and `double2` values now support matching direct arithmetic
and fixed arrays with one, two, or three dimensions. Global, automatic,
block-static, and persistent REPL arrays keep checked row or middle-slice
strides until the final 16-byte vector leaf. Unaligned-safe packed loads and
stores handle that leaf. Plain assignment and `+=`, `-=`, `*=`, and `/=`
preserve the vector type, evaluate every destination index once, and allow a
following lane read. Row and vector `sizeof` retain their complete sizes
without evaluating an index.
Direct ADD and MUL retain the written machine operand order, pinned by an exact
byte contract. The existing minimum and maximum intrinsics retain x86's
second-operand NaN and signed-zero behavior. Runtime checks accept either input
payload from a both-NaN ADD or MUL and reject any other result. Incomplete row
assignment receives a focused error. SIMD pointers, record fields, allocation
with `new`, array parameters, row values, and call ABI transport remain
unsupported. This changes private JIT and AOT behavior without moving a build
owner. ADR 0216 records the first fixed-array boundary, and ADR 0257 records
multidimensional row descent.

Private CupidC now converts decimal `float` and `double` tokens with a fixed
1536-bit integer workspace. It rounds the exact decimal ratio once to the
selected IEEE width using nearest-even, and an `f` suffix selects binary32
before that rounding. The path covers subnormals, finite limits, infinity,
and signed zero. Numeric tokens are limited to 95 characters, including their
suffix. Overlong tokens are consumed completely, leave the next delimiter
available, and report a focused error. Parser recovery retains the first lexer
diagnostic instead of replacing it with a later expression error. Hexadecimal
floating and `long double` literals remain open. This changes private JIT and
AOT behavior without moving a build owner. ADR 0217 records the boundary.

Hosted CupidC emits deterministic i386 ELF32 objects for all fifteen regular files in the CupidC, CupidASM, CupidDis, and user-ABI contract cohort, plus the separate hosted runtime probe. Those sources, the 19-source static Linux tool union, six strict Windows roots, the direct Windows runtime contract, and `kernel/lang/as_elf.cc` use 33 ordinary strict Linux roots, six strict Windows roots, one freestanding Windows root, a two-source strict kernel bridge, and three GNU runtime roots. The third GNU root selects the Windows side of the shared runtime. The retired 64-bit profiles have no active roots. The normal build compiles each regular Linux contract twice from the checked seed, compares all seventeen new objects, links all fifteen programs and the Linux runtime probe through CupidLD, and verifies all sixteen ELF32 executables. Publication accepts only a dedicated `cupidc-contracts` directory inside the source tree. The target is checked before work and again before promotion, and an existing destination must already verify as a complete cohort. Arbitrary directories, source trees, files, and symbolic links remain untouched. The initial, private, and newly discovered contract inventories must match exactly, which catches additions, removals, and restored edits that changed a copied input. Every run derives the cohort from its executable, requires a named manifest artifact, and verifies every artifact hash, the current 65-input contract set, the checked seed manifest, and the 50-file fixed-point source inventory before the behavior matrix starts. Those 65 inputs include the small Windows probe, the native Windows tool runtime and startup, CupidLD publication runtime and bridge, direct runtime contract, `direct.h`, `windows.h`, the user syscall ABI contract and its six declarations, the Toolchain Makefile, the publisher, and the independent Python ABI oracle. One captured seed-manifest byte sequence supplies its digest, decoded JSON, schema validation, and checked build plan. The same gate covers complete CupidLD and CupidObj command closures. Native GCC or Clang builds remain available only as explicit development oracles. This supersedes the older nine-file, eleven-file, fourteen-file, host-built, and 64-bit profile descriptions in the historical notes and long-form rows below.

The contract publisher now carries compile budgets in its plans. Fourteen
ordinary contracts compile in the worker pool with 900-second limits. That
pool drains before `cupidc-object` compiles alone with a 1,800-second limit.
Runtime compilation and contract linking keep their earlier scheduling and
limits. This changes build control, not CupidC ownership or output. ADR 0282
records the policy.

The staged `cupidasm-kernel-elf` plan also matches its native Cupid-owned
closure: `as_elf`, CupidLD, CupidASM, x86, and ELF32. The first checked
scheduler run exposed the old omission through an unresolved strong symbol
after the isolated object compile. Transactional failure published nothing,
and a direct plan test now prevents the closure from shrinking.

The v2 contract manifest now verifies the stage-three and stage-four
convergence pair carried by the four-generation bootstrap report. A complete
private rebuild reached this final boundary in 4,480.3 seconds after all
compile, link, comparison, and runtime checks passed. The stale verifier failed
without publication; positive and wrong-pair tests now cover the repaired
record.

The final supported gate passed in 4,589.9 seconds. It compared stage-three and
stage-four contracts, ran and published stage four, verified the 21-artifact
cohort, then proved the promoted native Windows CupidC and CupidLD reproduce
the checked-seed hello, ls, and cat objects and executables. The warmed path
passed in 12.2 seconds. This proves the scheduler, link closure, fixed-point
record, and publication generation without changing ownership counts.

Repository startup and runtime code now take five complete CupidC-emitted command closures across CupidASM and CupidLD. The runtime supplies the checked heap, file, memory, string, `errno`, working-directory, and diagnostic interfaces. Fixed-point and Linux-contract work runs CupidC, CupidASM, CupidDis, CupidLD, and CupidObj on i386 Linux or through WSL. The Linux seed binds the static images to the target ABI, source revision, producer lineage, and complete build plan. Windows output-bearing production work instead runs checked PE copies directly. The harness freezes the verified Linux seed and copies 50 current inputs, including `link.ld`, the small Windows probe, native tool runtime and startup, CupidLD publication runtime and bridge, the direct contract, and hosted declarations, into a private compiler root. The seed producer trio builds the 19-source stage-two Linux union and all five Linux tools below that root. The stage-two trio builds stage three, then the stage-three trio builds stage four. The driver rehashes the private and live source closures and revalidates the live seed at every generation boundary, after behavior, and before publication. Every stage-three C object, Linux startup object, and Linux tool image matches its stage-four counterpart. Both compared stages agree on positive and failure behavior for all five commands. They also produce matching native objects and PE images for all five tools. Windows runs help plus useful success and failure cases for each tool. CupidLD additionally checks exact output, collision handling, failure diagnostics, and candidate cleanup. The direct runtime contract checks allocation, named-file write and append, current-directory errors, and quote and backslash parsing. Stages two through four, behavior evidence, and the report appear together only after the full gate passes. ADR 0086 records the sibling commands, ADR 0088 records the compiler driver, ADR 0089 records the compiler fixed point, ADR 0090 records the five-tool fixed point, ADR 0092 records the checked seed, ADR 0142 records the frozen source closure, ADR 0268 records the shared Windows runtime, ADR 0269 records CupidLD publication, ADR 0272 records production PE execution, and ADR 0279 records the convergence rule. ADR 0280 preserves the preceding Linux seed, and ADR 0292 records the current one.

The runtime formatter also covers the active contract diagnostics that use
signed and unsigned 64-bit integers, sixteen-digit padded hexadecimal values,
and precision-bounded strings. The executable probe checks those forms before
the cohort can be published.

The separate hosted i386 runtime contract uses `.cc` and remains outside the
19-source fixed-point plan because it tests the completed runtime rather than
contributing to a tool image. The normal Toolchain cohort adds all fifteen
renamed contract programs. Stage-three and stage-four CupidC compile them at
the checked i386 ABI, CupidLD links them with the matching stage objects, and
all seventeen new objects and sixteen executables must match across the two
stages before the stage-four cohort is published. The verifier rediscovers the live input membership and checks its
hashes. Every run also verifies the requested manifest artifact and the whole
cohort. Native copies are optional oracles. ADR 0195 records the runtime
probe's naming boundary, and ADR 0196 records the complete contract cohort.
ADR 0174 records that stage-three seed lineage.
ADR 0176 records the libm production transfer that follows that seed promotion.
The seed carries operand-free assembly, the per-CPU pointer output, the active
integer atomics through fetch-or, width-aware port I/O, GNU `used`, the
source-driven assembly frontier, and the complete libm compiler
frontier. ADR 0103 uses that seed
to transfer ACPI and MP-table discovery into the normal build. ADR 0104
transfers e1000, the desktop, the socket layer, and TCP.

The checked seed keeps file-scope GNU basic assembly in a separate
translation-unit effect table and carries its source order through Linear IR.
It emits the twelve exact x87/SSE floating wrapper definitions at the start
of the then-named `kernel/cpu/libm.c` through the shared x86 model. The focused
object has 248 text bytes, twelve global function symbols, and no
relocations. The checked seed resolves named function-body operands before
it publishes the statement. It also emits the exact `libm_pow_impl` and
`libm_powf_impl` programs. The double form uses five `double` operands. The
mixed form uses a `float` output, two `float` inputs, and two `double`
inputs. Both have one memory clobber and balanced x87 depth. Each focused
function has 116 text bytes and no relocations. The checked seed also emits the
exact `sqrtsd %1, %0` statement with a `double` `=x` output and a `double`
`x` input. Its 65-byte focused function has no relocations. The checked seed
also emits the exact x87 `libm_atan2_impl()` statement with one `double` `=m`
output, two `double` `m` inputs, and a `memory` clobber. Its 53-byte focused
function has no relocations. It also emits the exact x87
`libm_exp_impl()` statement with one `double` `=m` output, two `double` `m`
inputs in `x`, `log2e` order, and a `memory` clobber. Its 71-byte focused
function has no relocations and balanced x87 depth. The checked seed also emits
the aligned 32-byte `fabs` mask block and both following wrappers. The mask
labels remain fixed at `.rodata` offsets 0 and 16 even when later read-only C
objects exist. The 15-byte `fabs` and 14-byte `fabsf` functions each carry
one `R_386_32` relocation to the matching local label. The checked seed also
emits the next eight rounding wrappers. The four double and float pairs save
and restore the x87 control word around `FRNDINT`, select down, up,
nearest-even, and toward-zero mode, and add 384 relocation-free text bytes.
The checked seed also emits `fmod` and `fmodf`. Both 35-byte functions repeat
`FPREM` while C2 is set, use a checked short backward branch, and leave ESP
and x87 depth balanced without a relocation. The checked seed also emits the
aligned `libm_log2e_const` and `libm_ln2_const` block and all eight following
exponent/logarithm wrappers. The constants occupy 16 aligned `.rodata`
bytes. The functions add 264 text bytes, and `exp`, `expf`, `log`, and
`logf` each carry one `R_386_32` relocation. The checked seed emits all 18
remaining cdecl bridges: six binary wrappers in the `pow`, `hypot`, and
`nextafter` pairs and twelve unary wrappers in the `asin`, `acos`, `sinh`,
`cosh`, `tanh`, and `cbrt` pairs. Four shared stack shapes copy the original
argument words, call the matching external implementation, reclaim the
copies, and move ST(0) into XMM0. The functions add 558 text bytes and 18
`R_386_PC32` relocations with addend `-4`. Two complete kernel-profile
compiles of the corrected source produce the same 16,164-byte valid
ELF32 object. General GAS remains open.

The production source is now `kernel/cpu/libm.cc`. The normal checked wrapper
owns its transform and freezes an exact closure of that source,
`kernel/core/types.h`, and `kernel/cpu/libm.h`. The 43,736 source bytes have
SHA-256
`baffe801c7573b8500c60251298a753f60732608d58443178be8ce9ab809ef93`,
and the 16,164-byte object has SHA-256
`c0911732361f2e1ea78aa778f834719ba12208cc2d9f0a312455a5e6a38a75b4`.
ADR 0155 records the initial file-scope boundary, ADR 0159 records named
operand normalization, ADRs 0161 through 0165 record the five represented
statements, ADR 0166 records the `fabs` effects, ADR 0169 records the
rounding family, ADR 0171 records the remainder family, ADR 0172 records the
exponent/log family, ADR 0173 records the cdecl bridges, ADR 0174 records
checked-seed carriage, ADR 0176 records production ownership, and ADR 0209
records the seven active range-reduction corrections.

Checked-seed CupidC represents the two exact volatile FXSAVE statements in
`kernel/core/process.cc`. One independent four-byte object or
`void` pointer `r` input reaches the shared x86 encoder as `0F AE 00`, and
the source's `memory` clobber remains attached to the immutable statement.
The complete source compiles twice under `KERNEL_I386` to matching validated
30,216-byte objects. The normal Make graph now builds the root through the
checked kernel wrapper. ADR 0119 records the compiler boundary, and ADR 0123
records the production transfer.

The GNU integer atomic slice represents load, store, exchange, fetch-add, and
fetch-or for one-, two-, and four-byte objects with constant orders. The
frontend, Linear IR, and i386 emitter retain and validate that contract
through ordinary loads and stores, memory `XCHG`, `LOCK XADD`, and a
`LOCK CMPXCHG` retry loop. The current checked seed carries all five
operations and compiles the active EHCI fetch-or path.
All three `percpu.h` roots parse through this checked-seed capability.
Checked-seed CupidC owns 156 checked-in sources in the strict non-Doom root
cohort. The generated kernel-symbol translation brings that strict total to
157 transforms, all using `.cc`. Another 83 checked-in Doom roots bring the
complete normal total to 240 transforms: 239 checked-in sources plus the
generated source. The 19-source i386 Linux fixed point uses the same names,
including the five roots shared with the normal image. Native GCC or Clang
recipes select C explicitly with `-x c` only for optional oracles, while the
checked seed compiles those paths under the fixed target profile.
ADR 0115 added the first 20 source-driven roots, ADR 0123 added eight more
and the generated symbol object, ADR 0124 renamed the next 111 roots, and
ADR 0126 completed the fixed-point naming work. ADR 0129 moved the lexer, ADR
0135 moved Nuked OPL3, ADR 0139 moved JPEG and glyph rasterization, ADR
0167 moved the FPU and SMP roots, ADR 0176 moved libm, ADR 0180 moved the
kernel entry and SIMD roots, and ADR 0181 moved the final strict host root.
The normal symbol generator
runs frozen copies of the pass-one kernel and CupidDis, validates the reader
output, checks the live inputs for drift, and publishes the `.cc` source
atomically. The checked compiler wrapper separately freezes the generated
source and its exact two-header closure before object validation and
publication.

The current checked-in frontier must repeat all 156 compiles. Strict syntax,
recursive Make dependencies, poisoned-host recipes, focused tests, the normal
image, and runtime gates remain part of that proof. The latest completed
two-pass frontier predates the 156th source. It passed twice against a frozen
445-file snapshot with SHA-256
`99d03de14f544f6a76d21ed147e62018873f1e2e8dfa2f4459830b69314432c2`;
both 155-object sets are byte-identical; each totals 3,749,796 bytes. The combined graph passes clean
normal and partitioned image builds plus strong four-vCPU runtime gates with
both NICs. ADR 0101 supersedes
older row text
that lists all atomic access as open or leaves the header gate at 150/154.
ADRs 0107 and 0108 record fetch-or and its seed transition. ADR 0110 records
the 40-source boundary, ADR 0111 records the 116-source expansion, and ADR
0115 records the first 20-source transfer. Across the root and supplemental
graphs, CupidC owns 246 transforms, no supported transform invokes a host C
compiler, and Python participates in 452. The external-program syscall ABI
output has Cupid-built contract and Python participants. The root size verifier
is Python-only and emits no OS artifact. The two Python-only supplemental
outputs aggregate the Toolchain cohort and record its manifest. No recursive Make
transform remains. The root
image has no host C transform and runs every production Cupid tool from the
checked seed.

The public frontend now represents decimal `float` and `double` constants as
exact IEEE bits using bounded integer arithmetic. The Linear IR and SSE
emitter cover represented integer-to-floating conversions,
floating-to-signed conversions, floating-to-unsigned conversions through
represented four-byte targets, and mixed represented integer and floating
arithmetic.
The checked seed also covers the explicit non-atomic `double` to
`unsigned long long` cast needed by `kernel/core/string.cc`. Unsigned
four-byte input uses an exact split across the sign boundary. Runtime
four-byte unsigned output widens binary32 exactly and reuses the split at
2^31, while unsigned-wide output is decomposed around 2^32. The seed carries the
older path, so `kernel/lang/cupidc_lex.cc` belongs to the normal CupidC
cohort. The checked seed also evaluates decimal
static-duration scalar and aggregate leaves with integer-only IEEE binary32
and binary64 arithmetic. It covers unary signs, the four arithmetic
operations, comparisons, casts, scalar truth, short-circuit logic,
conditionals, and represented signed or unsigned integer conversion through
64 bits. It rounds each operation to nearest with ties to even, preserves
signed zero, and writes exact `.rodata`, `.data`, or `.bss` bytes. Compiler
head also emits all six matching or mixed-width runtime comparisons with C's
ordered and unordered behavior. Automatic non-atomic `long double` values now
accept bounded finite normal decimal `L` tokens and cross locals, assignment,
unary plus and minus, addition, subtraction,
multiplication, division, and conversions to or from `float` and `double`
through i386 x87 memory slots. Direct and indirect fixed arguments occupy
twelve cdecl bytes. Variadic and unprototyped calls use the same width.
Functions return a `long double` in x87 `ST0`, and direct or indirect callers
store it in a twelve-byte snapshot. `va_arg(long double)` copies twelve bytes
and leaves the cursor at the following four-byte slot. Static-duration
scalars, fixed arrays, and complete records may contain non-atomic long-double
leaves. Implicit initialization zeros the complete object; explicit leaves
accept represented integer constant expressions or bounded decimal `L`
literals with parentheses and unary signs. The emitter writes exact x87
payloads with zero padding and selects `.bss`, `.data`, or `.rodata`. All six
comparisons accept matching long-double values and mixed `float` or `double`
inputs. The emitter uses a balanced `FUCOMIP` sequence and the existing
unordered-result normalization. Runtime truth and `_Bool` conversion cover
`float`, `double`, and automatic `long double`. Runtime conversions between
`long double` and signed or unsigned integers cover 8, 16, 32, and 64 bits.
Integer output saves and restores the x87 control word around a truncate-mode
`FISTP`. Unsigned 64-bit corrections select 64-bit x87 precision while
preserving the caller's rounding mode, then restore every saved control bit
before the final store. Runtime arithmetic, all six comparisons, and
conditional selection apply the usual arithmetic conversions between `long
double` and every represented value integer or enum. Conditional lowering
converts only the selected arm. Static initializer conversion covers `_Bool`, plain
`char`, every signed or unsigned i386 integer width, and an enum whose
compatible integer type has the represented target layout. Integer magnitudes
pack directly into the 64-bit x87 significand. For integer destinations other
than `_Bool`, long-double input truncates toward zero before its range check.
`_Bool` tests the original floating value. Integer-valued zero keeps `ZERO`
metadata. Linear IR validates each static `INTEGER` leaf against the standard
target kind and representation tables and rejects stray metadata.
Static long-double truth, all six comparisons, short-circuit logic,
conditional selection, and conversion to or from `float` and `double` now
fold through the target representation. Canonical x87 zero, subnormal,
normal, infinity, and NaN payloads cross the frontend, IR, and object
boundaries. Binary32 and binary64 infinities keep their sign when widened,
and NaNs use one canonical quiet payload in either direction. The expressions
publish final initializer records and no runtime IR.
Static long-double `+`, `-`, `*`, and `/` also fold through an unsigned
128-bit target packer. It rounds once to the x87 explicit significand, handles
gradual underflow and canonical special results, and leaves no runtime IR.
Hexadecimal floating literals, binary32 and binary64 subnormal literals,
hexadecimal or subnormal long-double literals, decimal ratios beyond the
bounded parser, other floating-to-wide conversions, integer-lvalue compound
assignment with a floating right operand, and atomic or long-double updates
remain open. ADR 0250 records
runtime unsigned
four-byte output,
ADR 0251 records static long-double data, ADR 0253 records runtime
conversions between `long double` and integers, and ADR 0254 records static
initializer conversion. ADR 0255 records static controls and finite width
conversion. ADR 0256 records canonical x87 payloads and special-value
conversion. ADR 0260 records static long-double arithmetic.
ADR 0288 records runtime integer and long-double usual conversions. ADR 0289
records wide integer conversion and usual arithmetic with `float` and
`double`.
Matching or mixed-width floating conditional arms and
the four arithmetic compound assignments retain their established x87 path.

The scalar and aggregate proofs cover both scopes, mutable and const objects,
positive and negative values, both signed zeros, the largest accepted bounded
literal, exact section placement, symbols, padding, deterministic repeated
emission, malformed metadata, and recovery. The hosted i386 runtime checks
the three target words in every literal payload. The shared conversion proof
adds every represented integer kind, both signed 64-bit endpoints,
`ULLONG_MAX`, and both the `_Bool` and unsigned results of `-0.5L`. It keeps
`sizeof(float) - 4` as a `ZERO` initializer.

Checked-seed CupidObj generates the three installation tables, and checked-seed
CupidC owns their compilation. CupidC also owns the three example external ELF
programs. These six translation units
use `.cc` names. The generated tables retain the strict kernel profile. The
examples use the closed user profile and CupidLD links them at `0x01C00000`.
Linux runs the checked bootstrap seed directly. Windows uses that Linux seed
through WSL for the user ABI contract, then runs the checked native CupidC and
CupidLD executables for output-bearing work.
Their wrappers compile and link immutable input copies, validate every ELF
result, and publish atomically. The 23-input default frontier matches every
locally generated object and executable. An explicit 46-input Windows
frontier also runs private native hosted CupidC and CupidLD snapshots and
requires byte-exact checked-seed output. Poisoned-path tests reject a fallback
to conventional host code generators on the normal path.
`user/build/` is ignored by Git. Separate private-image guest boots check
hello's numeric output and ls reading the root directory. Before cat starts,
its copy receives a marker-shaped FAT fixture at the normal
`/home/readme.txt` path. Each boot requires a PID-matched process exit.

The checked seed represents the six width-aware scalar port helpers and the
two repeated word-string helpers in unchanged `kernel/core/ports.h`,
including their fixed inputs, read/write outputs, and the INSW memory
clobber. The checked-seed C11 standalone-header sweep passes 161 of 163 inputs,
with `scheduler.h` and `simd_intrin.h` kept as exact C11-profile failures.
The checked seed parses all 29 declarations in unchanged `simd_intrin.h` under the
Cupid profile through native sized scalar and vector type specifiers. These helpers serve
the 155-transform strict non-Doom CupidC cohort. ADRs 0105, 0106, 0110
through 0112, and 0115 supersede the assembly, header-frontier, seed, and
earlier production wording in the long frontend, IR, and emitter rows below.
ADR 0181 records the latest production expansion.

Checked-seed CupidC also represents the exact volatile machine-state stores in
`kernel/core/panic.cc`. `fnstsw %0` and `fnstcw %0` take one 16-bit `=m`
output, while `stmxcsr %0` takes one 32-bit output. The address is evaluated
once and the shared x86 model emits the direct memory instruction. The
integrated compiler also handles the later local-label call. Two complete
profile compiles produce the same validated 10,212-byte object. This
capability has frontend, IR, object, self-source, and full-root evidence. The
normal recipe now uses the checked kernel wrapper. ADR 0121 records the
capability, and ADR 0123 records the production transfer.

USB reconciliation keeps failed work queued, rotates backed-off retries
behind ready peers, and reuses addresses and block slots after safe release.
Hub callbacks report change bits while the core owns teardown, reset,
enumeration, acknowledgement, and the reread that catches a new edge. EHCI
and UHCI interrupt slots carry controller-local generations and in-flight
state. Cancellation waits for the matching callback and DMA use to finish.
A UHCI interrupt acknowledges only write-clear status bits and cannot clear
the halt observation used for teardown. A quarantined address requires a
proved reset before companion handoff. Block references reject saturation,
and mass storage restores its online state when unregister fails. Compiled
fixtures and 45 USB tests pass, and all 123 GUI gate unit tests cover
the USB runtime contract and its failure forms. Live e1000 and RTL8139
runs pass UHCI input reattachment and six EHCI storage lifetimes. ADR 0109
records these ownership rules.

The capability narratives below preserve the owner recorded when each slice
landed. Statements that GCC or Clang still builds a normal contract, or that
no normal OS object has moved, describe those earlier checkpoints. The current
ownership summary above and ADR 0196 take precedence: both checked compiler
stages build the normal Toolchain contracts, and native copies are optional
oracles.

Eight-byte integer values now serve the CupidC-built X25519, socket, and TCP objects as well as hosted contracts. Constants, matching conditional arms, fixed direct and indirect call results, object loads, initialization, assignment, mutation, parameters, arguments, discard, return, arithmetic, bitwise operations, comparisons, logical operations, switch dispatch, and represented conversions use one Linear IR handle backed by an emitter-owned eight-byte snapshot. File objects, block statics, fixed automatic objects, pointer dereferences, ordinary members, and indexed elements reach that path. A declared argument receives eight cdecl stack bytes, and the return boundary places the low word in EAX and the high word in EDX. Calls retain packed post-conversion actual types in emitted order. Ellipsis or unprototyped calls pass each signed or unsigned wide integer in adjacent low and high words, while a wide variadic read advances by eight bytes. Multiplication combines the low-word product and two cross-word products. Division and remainder use a fixed 64-step restoring loop. Focused proofs retain the active compiler, assembler, X25519, and wide diagnostic-call guards. Wide variadic reads and unprototyped wide calls remain fixture evidence rather than whole-function ownership. GCC or Clang still builds the compiler contracts; normal root objects use CupidC.

The quotient/remainder object contains eleven functions and thirteen software loops in 4,775 text bytes. It has fingerprint `55F1A495`, twelve symbols including the null symbol, no relocations, and no `CALL`, `DIV`, or `IDIV` instruction. Thirty-three defined execution checks cover unsigned boundaries, all four signed sign combinations, a safe `INT64_MIN` case, conversions, chaining, input reuse, ABI preservation, rollback, and repeat emission. Division by zero and `INT64_MIN / -1` or `% -1` stay within C's undefined-behavior boundary, so CupidC promises neither a result nor a trap for those inputs.

The wide-mutation fixture publishes 15 functions and 225 IR instructions. Its 17-function object has 4,410 text bytes, fingerprint `4B337038`, 18 symbols including the null symbol, and no relocations. Execution checks cover every compound operator, signed and unsigned updates, postfix snapshot preservation, one-time indexed evaluation, volatile access, and cdecl state. The checked-seed X25519 object uses the same path for `fe_carry`; the focused proof and the remaining host-owned C transforms still depend on GCC or Clang.

This work does not move production ownership. The shared frontend, Linear IR, and deterministic i386 emitter move `float` and `double` values through objects, assignment, calls, variadic reads, and returns. Explicit casts and assignment conversion accept every represented signed or unsigned integer through 64 bits as input to either width. Unary plus and minus and binary addition, subtraction, multiplication, and division accept matching or mixed floating widths. Runtime `+`, `-`, `*`, `/`, all six comparisons, and conditional selection also apply the usual arithmetic conversions to every represented value integer or compatible enum. Only the selected conditional arm converts. Inputs through four bytes use SSE. Wide inputs use x87 `FILD`, including the unsigned 2^64 correction, before a binary32 or binary64 store. The four arithmetic compound assignments with a floating lvalue compute at the common width, convert back to the left width, and evaluate their lvalue once. Every changed x87 result is stored immediately at its C width. A `float` rounds into a fresh four-byte slot, while a `double` receives a fresh private eight-byte snapshot. The unchanged `libm_tanh_impl` body pins nested `double` arithmetic, and the following `float` helper slice pins width conversion. The current proof emits a 63-function object with 10,513 text bytes, fingerprint `01725E63`, 64 symbols, and 123 relocations. Its decoder lock includes 27 wide loads, 18 control-word save and restore pairs, and nine unsigned correction branches. Twenty-two wide execution cases cover signed and unsigned endpoints, precision boundaries, all four arithmetic operators, all six predicates, and both conditional directions. The model does not execute native x87 code. Hosted source-head CupidC and the checked Cupid-built driver emit byte-identical objects for a source using the forms. The current checked seed carries the extension through ADR 0292. No active OS source needs the expression shape, so no normal object, transform, dependency, or source suffix moves.

Represented bit-field assignment and mutation expand hosted CupidC capability without moving a production transform. Linear IR retains the selected graph member and evaluates the record address once. The emitter preserves neighboring bits, returns the stored lane for plain or compound assignment and prefix update, and returns the extracted old lane for postfix update. The focused mutation proof covers every compound operator, signed and unsigned width wrap, and volatile 32-bit direct stores. The plain-assignment proof still pins Doom's unchanged indexed color-array shape. No unchanged active expression currently uses bit-field mutation, so this issue #25 proof changes hosted capability without changing an ownership count. GCC or Clang builds the compiler contract; normal root objects use CupidC.

Ordinary bit-field promotion serves `kernel/doom/src/i_video.cc` in production. The frontend keeps the direct member only for a narrow `unsigned int` field that promotes to signed `int`, and Linear IR validates that provenance before allowing the same-rank conversion. The focused exact object and execution checks cover the shift and mask forms used by all nine guarded color reads. Two exact-profile compiles reproduce the 9,288-byte object with SHA-256 `d04e91844763391d4224d14aefce64ece02a95c9a99c604e9ef5b1392974dd20`. All 83 Doom and port roots use the checked production wrapper.

Checked-seed CupidC now covers the three separate Doom compatibility roots
without moving their normal recipes. Pointer-preserving static address casts
compile the unchanged libc-stub initializer. The exact dglibc jump effect
emits `dg_setjmp` and `dg_longjmp` through the shared x86 model. Two
checked-seed compiles produce the same object for each root. Together with the
80-root Doom-tree frontier, the checked compiler covers all 83 roots.
Host-object and link comparison, `.cc` renames, and runtime proof still
precede production transfer.

The shared Linear IR lowers explicit casts to `void` and now serves production uses in e1000, desktop, and TCP. It evaluates the operand once, emits typed `DISCARD` for a represented integer, pointer, structure, or floating scalar, and leaves a `void` operand off the abstract stack. The complete unchanged `ctool_host_allocate` and `ctool_host_release` helpers retain the focused guard. The deterministic proof adds one 52-byte function with three symbols and one direct-call relocation.

Automatic aggregate initializer lowering now serves the CupidC-built desktop object. `ZERO_OBJECT` applies implicit zero to a complete fixed array or structure, then explicit represented leaves run in source order through direct `MEMBER_ADDRESS` and `ELEMENT_ADDRESS` paths. Supported structure-valued leaves use the structure copy path. The i386 proof checks `CLD`, `REP STOSB`, EDI preservation, selected store offsets, four source-ordered calls, and deterministic repeat emission. Active `no_name` and `{0}` declarations remain focused guards. Issue #25 remains open for the deferred aggregate forms.

Structure values now serve the CupidC-built socket and desktop objects. Complete supported structures cross lvalue snapshots, copies, assignment chains, conditionals, expression initialization, discard, fixed direct and indirect calls, and returns. The emitter copies arguments inline in rounded four-byte spans and uses the i386 hidden return pointer at `EBP + 8`; explicit parameters begin at `EBP + 12`, and the callee removes the hidden slot with `RET 4`. Instruction-owned frame temporaries hold snapshots and call results. The normal Cupid-built contracts check this production path; a native runner is optional oracle evidence.

Sixteen-byte call alignment now serves all four e1000, desktop, socket, and TCP objects. The i386 emitter derives each call's padding from the fixed frame, live Linear IR values, and outgoing ABI block, then places ESP on a sixteen-byte boundary before `CALL`. The control-flow decoder covers direct, indirect, nested, branched, looped, scalar, and structure calls with every possible padding amount. A symbolic oracle checks argument values after padding moves. The normal proof is Cupid-built, while the native contract remains an optional oracle.

Hosted scalar variadic callees also leave ownership unchanged. In GNU C mode, `__builtin_va_list` is a target `char *` cursor. The frontend retains start, argument, copy, and end operations. Linear IR carries target-independent cursor operations. The i386 emitter reads represented non-atomic four-byte pointers and four-byte or eight-byte integers. Four-byte values advance the cursor by four. Wide integers advance it by eight and return one snapshot handle. The exact Doom compatibility header now parses. A decoder-driven i386 execution oracle checks one pointer read and two independent reads of the same unsigned-long slot through copied and original cursors. A focused wide fixture covers signed, unsigned, successive, and copied-cursor reads. GCC or Clang still builds this path, and no normal OS C object has moved to it.

Hosted empty identifier-list definitions and unprototyped calls also leave ownership unchanged. The frontend preserves the non-prototype type and applies default promotions to every argument. Linear IR retains the call site's actual count and a packed post-conversion type for each argument. The i386 emitter uses those types to size, align, copy, and clean up direct and indirect cdecl calls. Signed and unsigned wide integers and source `float` values promoted to `double` occupy two adjacent words. GCC or Clang still builds this path, and no normal OS C object has moved to it.

Block-scope record tags now serve local declarations in the CupidC-built desktop object. The frontend gives local `struct` and `union` tags lexical identity, supports incomplete declarations and same-scope completion, and removes names when their scope closes. Record tags from a function definition's parameter list remain visible through its outer body. A tag-only declaration with a represented storage class or type qualifier lowers without runtime instructions when it introduces a tag; repeating a visible tag is rejected. A `for` initializer may use a visible tag or anonymous record for its object but cannot introduce a named tag or omit the object. The normal Cupid-built contracts cover Doom's anonymous block-static `packs` shape, including exact literal data, the text reference to `packs`, and all three string relocations. The exact profile parses the complete `d_main.cc` file after the linked-object work in ADR 0058.

Hosted block-scope external objects give a lexical alias one canonical linked identity. The declaration emits no IR, and its uses follow the existing file-address relocation path. The deterministic object proof has 15 text bytes, three symbols, and one relocation to an undefined object. The exact Doom `d_main.cc` profile parses both local external arrays and completes. The normal build uses checked-seed CupidC for this cohort.

Hosted block typedefs retain each alias in lexical source order, and IR validates it without allocating storage or emitting an instruction. Eight unchanged typedef declarations across the Toolchain contracts pass through this path. A direct-spelling oracle proves that the alias produces identical ELF32 output. The normal Doom build uses checked-seed CupidC.

Hosted block function declarations separate each lexical name and visible type from the canonical linked function, and IR consumes the declaration without storage or instructions. Direct calls and addresses use the existing linked-function path. The object proof matches equivalent file-scope declarations byte for byte. It retains one undefined function symbol, two `R_386_PC32` call relocations, and one `R_386_32` address relocation. All 27 active declarations remain in their original nine files. The normal Doom cohort uses checked-seed CupidC.

Block-static emission now serves the CupidC-built desktop object. The frontend supplies constant initializer roots and absolute block-binding identities, Linear IR retains a runtime `LOCAL_ADDRESS` without emitting declaration stores, and the emitter maps that identity to a local ELF object symbol. File definitions come first, block statics follow in binding order, and functions come afterward. The checked contract covers eleven objects in `.rodata`, `.data`, and `.bss`, with sixteen `R_386_32` relocations and no automatic frame slots. The normal contract gate builds this proof with CupidC; a host-built runner remains an optional oracle.

Hosted block-scope compound literals do not change tool ownership. The frontend gives each source site one unnamed-object identity and initializer root. Linear IR runs the initializer whenever execution reaches the expression, and the emitter reuses one checked persistent frame slot for that identity. Aggregate lists build in a second staging slot and commit after all initializer reads finish. The hosted path emits deterministic ELF32 output for the unchanged preprocessor call, but GCC or Clang still builds the compiler and no normal OS C object has moved. Named automatic aggregate declarations still initialize in place, so backward-jump reentry through an escaped alias remains open under issue #25.

In the IR and emitter rows below, compound-literal storage includes both `COMPOUND_LITERAL_ADDRESS` for the persistent source-site object and `COMPOUND_LITERAL_STAGING_ADDRESS` for an aggregate list's private build area. `COPY_OBJECT` commits staging once initialization is complete. The object proof distinguishes the two aligned regions, checks staged member and array-element stores, then verifies a complete commit and a later read from each persistent object.

The private production compiler tags loop and switch controls. A switch-local `break` removes its saved selector; a `continue` finds the nearest loop and removes each crossed selector. The parser accepts 128 active controls and 1,024 active statement calls, rejects the next entry before unsafe recursion, and recovers for the next REPL evaluation. `/bin/feature25.cc` proves all three loop targets, nested switches, sustained cleanup for both jump kinds, the negative outside-loop diagnostic, both accepted depth boundaries, both overflow diagnostics, and both recovery paths in the in-OS JIT. This hardens private code generation without changing ownership. The shared hosted frontend retains initializer forests, linked addresses, graph-member identities, canonical labels, and typed function bodies. Linear IR now covers active arithmetic, signed and unsigned 32-bit division and remainder, all integer relations, bitwise AND, OR, XOR, and complement, explicit casts among represented one-byte, two-byte, and four-byte integers, same-width casts between four-byte integers and object pointers, explicit casts to `void` for represented scalar and `void` operands, left and right shifts, short-circuit logical AND and logical OR, plain assignment, represented non-Boolean integer and pointer compound assignments and updates, structured selection and loops, 32-bit integer switch dispatch, nearest-control `break`, nearest-loop `continue`, direct labels and `goto`, multiple returns, direct and fixed indirect calls with one-byte, two-byte, or four-byte integer parameters and results, represented target-sized scalar locals and target-sized fixed automatic arrays or structures in supported compounds, including the initializer-list and block-scope compound-literal subsets, linked object loads and stores, ordinary members, and direct four-byte bit-field reads, value-preserving plain and compound assignment, and prefix or postfix updates. Represented object pointers now support equality, unsigned order, scalar truth testing, pointer-valued conditional selection, same-width explicit casts, complete-object arithmetic, normalized subscripts, linked array decay, and pointer mutation. Represented function pointers now retain function signatures through decay, address-of, dereference, storage, equality, null conversion, truth tests, conditional selection, parameters, results, and fixed indirect calls. The unchanged integer mutation statements in `toolchain/ctool.cc`, the complete `x86_put_u8` body and decoder byte mutations in `toolchain/x86.cc`, and both `buf += 256` statements in `drivers/ata.cc` drive those seams. The unchanged `section_map` array in `toolchain/cupidc_emit.cc` and `children` array in `toolchain/cupidc_ir.cc` drive automatic object storage. The complete unchanged `ctool_host_allocate` and `ctool_host_release` helpers drive casts to `void`. The unchanged `asm_lower`, `x86_class_width`, and `x86_set_memory_width` functions drive signed and unsigned byte and word transport. Narrow loads extend into canonical 32-bit words, compound assignments and updates compute through 32-bit promotion, stores use the declared byte or word width, and fixed cdecl calls keep four-byte argument slots while normalizing narrow results. Exact contracts cover integer mutation across byte, word, and doubleword objects, pointer mutation, and represented four-byte bit-field mutation, destination evaluation once, enum and signedness conversions, volatile access, deterministic target bytes, and stable rejections. All twelve hermetic `HOSTED_TOOLCHAIN_64` implementation files parse completely. No normal OS C transform has moved to this path. Issue #25 stays open.

The final two sentences of the preceding paragraph describe an earlier
milestone. `HOSTED_TOOLCHAIN_64` is retired. All twelve implementation roots
now use `HOSTED_I386_LINUX`, and both checked compiler stages build the normal
contracts.

ADRs 0104, 0110, 0111, 0115, 0123, 0127, and 0160 supersede the old ownership
statement. The shared path emits 156 strict non-Doom objects, all 83 Doom
objects, and the generated kernel-symbol object. That is 240 normal
transforms from 239 checked-in `.cc` sources plus the generated source,
including the five Toolchain roots shared with the fixed point. The older
40-source, 116-source, and 136-source counts remain historical milestones.
Issue #25 remains open for the unrepresented language and object categories.

The hosted function-pointer relation now uses checked, memoized job scratch. It covers repeated callback graphs, old-style promotions, top-level parameter qualifiers, malformed parameter storage, and rollback under a small arena. Represented function pointers may cast to another function-pointer type or to and from a represented 32-bit integer without changing the target bits. Object-pointer interchange and narrow or wide integer forms remain unsupported. A 28-byte local-function object pins `R_386_32` against a defined static symbol. The normal gate now builds this proof with CupidC; the native runner is an optional oracle.

The detailed module rows below keep the language boundary carried by the
current checked seed. The refreshed seed represents comma sequencing, typed
static nulls, known-true loop reachability, bounded register and EFLAGS
snapshots, GNU `weak`, `section`, `unused`, and `used` metadata, target-width
static floating data, and all six floating comparisons.
Rows that still quote the earlier 139-to-5 source-name split are historical
snapshots. The complete `.cc` boundary and floating capability stated above
supersede those counts and open lists.
The first 28 source-driven roots passed the normal frontier, image, and
runtime gates under ADRs 0115 and 0123. ADR 0124 renames another 111
production-owned roots and records the passing graph proof for that path
transfer.

Checked-seed CupidC retains GNU `used` and `__used__` on canonical file-scope
objects and functions. Focused frontend, Linear IR, and object contracts
validate the metadata and the exact generated kernel-symbol declaration
shape. The attribute itself does not change object bytes because every
represented definition is already emitted. The generated
`kernel/cpu/ksyms_data.cc`
source stores its 114,851 logical bytes as little-endian `unsigned int` words
and adds one zero byte to complete the last word. The checked wrapper owns
its normal object. ADR 0116 records the language boundary, and ADR 0123
records the production transfer.

Checked-seed CupidC emits the exact volatile
`call 1f\n1: popl %0` state read used by the stack-trace helpers in
`kernel/lang/as.cc` and `kernel/lang/cupidc.cc`. Frontend and Linear IR
contracts pin its one four-byte integer `=r` output. The shared x86 path emits
a zero-displacement call and immediate pop without a relocation, and a
decoder-driven state oracle checks the captured address and balanced stack.
Both roots compile reproducibly under the complete kernel profile. The
normal Make graph builds both through the checked kernel wrapper. ADR 0118
records the language boundary, and ADR 0123 records the production transfer.

Checked-seed CupidC also retains the 8259 PIC's exact GNU `Nd` port
constraint. It selects the DX alternative and emits the active `inb` and
`outb` templates through the shared x86 model. `kernel/cpu/pic.cc` compiles
under the complete kernel profile and belongs to the normal CupidC cohort.
ADR 0120 records the language boundary, and ADR 0123 records the production
transfer.

The long-form rows below preserve the owner recorded when each capability was
landed. The current ownership summary above and the milestone table below take
precedence where a later transfer has overtaken a row.

The frontend, IR, and emitter share one current `long double` boundary.
Automatic non-atomic values support bounded finite normal decimal `L` tokens, floating-width
conversion, unary plus and minus, all four arithmetic operators, twelve-byte
direct and indirect fixed, variadic, and unprototyped arguments, function
returns, direct and indirect call results, `va_arg(long double)`, and all six
matching or mixed floating comparisons.
Static-duration scalars, fixed arrays, and complete records may contain
non-atomic long-double leaves. Implicit initialization zeros the complete
object. Explicit leaves accept represented integer constant expressions or
bounded decimal `L` literals with parentheses and unary signs. Exact payloads
and padding reach `.bss`, `.data`, or `.rodata`. Canonical zero, subnormal,
normal, infinity, and NaN payloads use one validator at every frozen boundary.
Binary32 and binary64 special values convert without host floating work.
Hexadecimal or subnormal long-double literals, decimal ratios beyond the
bounded parser remain open. Static arithmetic uses integer-only 128-bit
intermediates and produces final initializer data. Runtime
conversion between `long double` and every signed or unsigned i386 integer
width uses the shared `FILD` and `FISTP` path. Static initializer conversion
between these integer types and bounded finite `long double` uses exact target
integer bits and x87 payloads without runtime work.
Checked-seed CupidC accepts exact `fldcw %0` with
one addressable, non-atomic 16-bit integer memory input. GNU semantics make
the no-output statement volatile even without that keyword. The
checked seed also owns the older 32-bit `ldmxcsr %0` state input.

The preprocessing row below retains the profile split from before the runtime
probe began using GNU variadic builtins. The current gate has 33 ordinary
strict Linux roots, six strict Windows roots, one freestanding Windows root,
two strict bridge roots, and three GNU runtime roots.

ADR 0202 supersedes any older row below that lists runtime floating truth,
controlling expressions, or conversion to `_Bool` as unsupported. The shared
frontend, Linear IR, and i386 emitter own those operations for non-atomic
`float`, `double`, and automatic `long double` values.

ADR 0260 supersedes older broad rows that list static long-double arithmetic
as open. Checked-seed CupidC folds all four operators into exact initializer
data without runtime IR. ADR 0265 records seed carriage.

The checked seed and source head have 604 x86 forms, 249 canonical mnemonics,
64 registers, and fingerprint `55A8970F`. The catalogue includes signed x87
`FILD` and `FISTP` memory operands at 16, 32, and 64 bits and canonical
`SETP` and `SETNP` byte predicates. Four forms cover canonical
SHRD. The forward x87 row encodes `FSUB ST(1), ST(0)` as `DC E9`. This
supersedes older counts below. ADR 0203 records seed carriage for `FLDZ` and
the three preceding x87 forms, ADR 0208 records forward-subtraction carriage,
and ADR 0226 records SHRD. ADR 0228 records SHRD's first seed carriage, ADR
0243 records an earlier seed, ADR 0252 records the x87 integer forms, ADR
0258 records the preceding seed, ADR 0259 records the parity predicates, and
ADRs 0265 and 0280 record the preceding seeds, and ADR 0292 records the current
stage-four promotion.
ADR 0253 records
the x87 integer forms' first CupidC runtime use.

The promoted seed also passed an earlier user ownership frontier with exit 0
in 3,291.317 seconds. The supported publisher rebuilt a complete 21-artifact
Toolchain cohort, required stage-two and stage-three byte identity, and
published it as one transaction. The user gate then checked schema
`cupid.user-syscall-abi.v1`, version 5, 103 fields, 412 table bytes, and 101
providers before reproducing
hello, ls, and cat from a 23-input closure with SHA-256
`f63919f4b4307278c825ebedf99391e3ec110646042ee397dac3a7ba330435d3`.

| Source or artifact cohort | Owner/path when recorded | Fixed-point owner/path | Status and next proof |
| --- | --- | --- | --- |
| `boot/boot.asm` | CupidASM flat binary | CupidASM plus CupidDis guarded flat binary | Production-owned: hosted CupidASM emits the exact 2,560 bytes, SHA-256 `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`, byte-identical to the optional NASM oracle. It reads the kernel through LBA 20479 and installs the stack top at `0x01100000`. The normal Make rule calls the shared checked raw-image transaction with the production manifest and full seed closure. Hostbuild freezes the source and seed, assembles private image and map candidates, runs strict CupidDis, detects drift, and publishes atomically. A failure preserves the prior image and leaves no public map. ADR 0283 records the cutover. |
| `kernel/cpu/isr.asm` | CupidASM ELF32 `ET_REL` | CupidASM plus CupidDis guarded ELF32 `ET_REL` | Production-owned: common entry preserves the live per-CPU `GS`, defers generic rescheduling through handler completion/EOI, and discards the source CPU's saved selector after a migration-safe suspension point. CupidASM emits a 1,892-byte object whose 417-byte `.text` has SHA-256 `bcf582569c26029d5143ec42f6de24388596c412bca2b4a672608800fe2606e3`, with 41 symbols and eleven `R_386_PC32` relocations matching the optional NASM oracle exactly. Hostbuild validates a private candidate and strict CupidDis covers every executable byte before atomic publication. ADR 0286 records the guard. |
| `kernel/core/context_switch.asm` | CupidASM ELF32 `ET_REL` | CupidASM plus CupidDis guarded ELF32 `ET_REL` | Production-owned: after moving BKL release and interrupt re-enable behind the new-stack handoff, CupidASM emits a 696-byte object whose 73-byte `.text` has SHA-256 `25b78f4c2cbf3dfadc6dc87a9731a097bfd9df0675534d8449c24d890114fbfa`, with three globals, one undefined handoff symbol, and one `R_386_PC32` relocation matching NASM semantics. Hostbuild validates a private candidate and strict CupidDis covers every executable byte before atomic publication. Scheduler handoff, nested deferral, and four-CPU runtime proofs pass. ADR 0286 records the guard. |
| `kernel/smp/smp_trampoline.S` | CupidASM flat binary with strict CupidDis inspection | CupidASM and CupidDis production gate | Production-owned: hosted CupidASM emits an exact 4,096-byte private candidate with SHA-256 `b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90`. CupidDis requires known instructions in code16 `[0x000, 0x01f)` and code32 `[0x210, 0x254)`, while treating the other bytes as data. The shared checked raw-image transaction owns locking, source and seed freezing, drift checks, private candidates, publication-boundary checks, and atomic publication. The caller retains the exact mixed-mode policy. The expanded eleven-test suite passed in 1.708 seconds, including direct mismatch and live-output drift checks for both callers. The optional NASM oracle remains byte-identical, and four CPUs reach online state in the SMP smoke. |
| 22 `demos/*.asm` inputs | CupidObj embeds source; shared in-OS CupidASM assembles on demand | CupidObj embeds source; shared CupidASM owns host and in-OS assembly | Production source embedding has transferred to CupidObj. All 22 unchanged sources assemble deterministically through shared fixed-image mode with implicit externs disabled. The `parity_gfx2d.asm` fixture carries the kernel adapter's `gfx2d_fullscreen_enter` and `gfx2d_fullscreen_exit` exports, so its normal and error paths resolve. The kernel adapter supplies the real 631-definition catalogue and resolves includes through VFS. A private guest assembled `/demos/hello.asm` to a 15,680-byte `ET_REL` object, linked an 8,536-byte two-segment ELF at `0x01A00000`, ran PID 4, and observed a normal exit in 79.661 seconds. |
| Shared `toolchain/ctool*` core, hosted adapter/contract, and `kernel/lang/ctool_kernel*` adapter | Checked-seed CupidC builds `ctool.cc`, `kernel/lang/ctool_kernel.cc`, every normal hosted contract, and the remaining normal module objects; host C is confined to optional native oracles | CupidC builds the shared core and both adapters; checked seeds build the hosted contract | Interface established and tested; hosted and kernel CupidDis/CupidASM plus hosted CupidObj, CupidLD, and CupidC preprocessing, declaration, type/layout, IR, and object operations consume it. Complete CupidC-built CupidASM, CupidDis, CupidLD, and CupidObj closures use the hosted adapter through the repository i386 Linux runtime. The private in-kernel compiler remains a separate JIT/AOT implementation. Linux and native Windows stage-three to stage-four convergence and seed promotion are complete. A Python-free coordinator remains open. |
| Shared `toolchain/elf32.*` object module | Checked-seed CupidC builds the freestanding object, normal hosted contracts, and static i386 CLI; host C builds only optional native oracles | CupidC builds the same module; CupidC, CupidASM, CupidDis, CupidLD, and CupidObj share its typed object seam | Static object semantics are Cupid-owned and interoperability-tested for `ET_REL` plus read-only `ET_EXEC`; CupidASM and CupidObj emit through the shared writer, while CupidObj and CupidLD consume the typed reader. Dynamic/multiple symbol-table domains remain explicitly unsupported |
| Shared `toolchain/x86.*` instruction module | Checked-seed CupidC builds the freestanding object, normal hosted contracts, and static i386 CLI; host C builds only optional native oracles | CupidC builds the same module; CupidC, CupidASM, and CupidDis share its typed semantic seam and private form catalogue | Instruction semantics are Cupid-owned and active-surface-tested. The checked seed and source head carry 604 forms, 249 mnemonics, 64 registers, and fingerprint `55A8970F`. The catalogue includes canonical `SETP` and `SETNP` byte predicates. Six rows cover signed x87 `FILD` and `FISTP` memory operands at 16, 32, and 64 bits. The four SHRD rows cover canonical 16-bit and 32-bit SHRD with register or memory destinations and either immediate or fixed CL counts. Exact encode, decode, replay, mode overrides, truncation, invalid operands, and recovery are checked. Active CupidC objects no longer leave their SHRD sites as fallback data. ADR 0226 records the capability, and ADR 0228 records its seed carriage. ADR 0252 records the x87 integer rows, ADR 0258 records the preceding checked seed, ADR 0259 records the parity predicates, ADRs 0265 and 0280 record preceding checked-seed carriage, and ADR 0292 records the current seed. The forward x87 row encodes canonical `FSUB ST(1), ST(0)` as `DC E9`; exact replay and reversed-operand failures are checked. ADR 0207 records that capability, and ADR 0208 records its promotion. The four preceding x87 additions are 80-bit `FLD` and `FSTP` memory forms, i686 `FUCOMIP ST0, ST(i)`, and operand-free `FLDZ`. ADR 0203 records their promotion. All sixteen i686 conditional moves cover 16-bit and 32-bit destinations with same-width register or memory sources in either mode; fourteen aliases map to stable canonical names. Three-operand `IMUL` covers 16-bit and 32-bit register destinations, same-width register or memory sources, and either a full immediate through `69 /r` or a sign-extended byte through `6B /r`. Ordinary padding covers `90`, `66 90`, and word or doubleword `0F 1F /0` register and memory forms with normal size and segment overrides. A private decoder recognizer accepts only the five measured 32-bit Clang padding strings with two through six leading `66` bytes. It reports an automatic form, creates no catalogue row, and leaves all nearby duplicate-prefix input invalid. Shortest-form selection, exact replay, truncation, prefix rejection, invalid group digits, PAUSE separation, and recovery are checked. Its `RET imm16` form still lets CupidC encode hidden-result cleanup, CupidASM assemble `ret 4` as `C2 04 00`, and CupidDis render those bytes as `ret 0x4`; `ret 65536` is rejected. CupidASM and CupidDis use the full active surface, but CupidASM cannot request redundant prefixes. Hosted CupidC uses the encoder for leaf functions, branches, and direct or register-indirect calls, including PC-relative field metadata, then uses the decoder to verify the result. The private in-kernel compiler has not migrated to the shared x86 module |
| Shared `toolchain/cupidc_pp.*` preprocessing module | CupidC owns the shared freestanding module and every active hosted contract | CupidC builds one shared host/kernel preprocessor and both C/Cupid parsers consume its typed tape | Translation phases, dual presumed and physical locations, macros, C11 conditionals, reproducible predefined macros, `#line`, includes, forced and cache-aware traversal, pack state, typed Cupid `#exe`, diagnostics, and rollback are contracted. The checked audit covers 2,452 direct include operands, 2,199 quoted and 253 angle, with zero macro operands across 701 C-family inputs. Its generated manifest drives 395 tracked profile executions plus four generated roots. The target set has 33 ordinary strict Linux roots, six strict Windows roots, one freestanding Windows root, a two-source strict kernel bridge, and three GNU runtime roots. Only the bridge searches `/kernel/lang`; every request uses checked declarations and a four-byte pointer fact. The audit pins the closure sources, object-contract input, self-host link recipe, Windows probe inputs, and user ABI contract inputs. Twenty-two browser fragments retain their owner, two delivered headers remain non-roots, and no hosted root is deferred. Named GNU variadics, numeric line-marker semantics, broader hosted-runtime/header integration, and deeper kernel integration remain |
| Shared `toolchain/cupidc_type.*` type/layout module | Checked-seed CupidC builds the freestanding module and normal hosted contract; host C builds only the optional native oracle | CupidC builds the shared semantic module; C and Cupid declaration frontends emit its immutable indexed graph before typed-AST/IR lowering | Scalar and aggregate i386 layout semantics live behind one transactional operation with immutable index references, iterative by-value dependency traversal, stable job-owned outputs, and reclaimed scratch. It supports the required scalar identities, pointers, frontend-selected compatible-integer enums, functions as type markers, arrays/flexible members, vectors, records/classes, bit fields, packing/alignment, and qualification wrappers. Independent manual graphs pin every FAT16 offset and active Doom/process/syscall/`e1000_rx_desc_t`/per-CPU ABI shapes. The hosted frontend reproduces the unchanged FAT closure and selects its two unsigned-`int` compatible enum types from source. Layout owns final flexible-member placement; the frontend owns promoted-name eligibility. The active graph uses these layouts through the shared frontend, IR, and emitter across 246 CupidC transforms. ADR 0181 records the latest production handoff. Further private-kernel integration and additional language categories remain open. |
| Shared `toolchain/cupidc_frontend.*` declaration module | Checked-seed CupidC builds the freestanding operation and normal hosted contract. The private kernel compiler remains the embedded runtime JIT and AOT path; host C builds only the optional native oracle. | CupidC builds one shared C/Cupid frontend that feeds typed AST, linear IR, and the object path. | `ctool_c_parse` publishes immutable bindings, object and function definitions, labels, initializer forests, typed statements and expressions, child tables, and completed i386 layouts. Compound-literal expressions own initializer roots and retain one unnamed automatic-object identity per source site. Block `struct` and `union` tags retain lexical identity, including forward declarations, completion, shadowing, scope expiry, and tags introduced by a function definition's parameter list. Tag-only declarations with a represented storage class or type qualifier publish no runtime binding when they introduce a tag; an empty declaration that only names a visible tag is rejected. A `for` initializer may use a visible tag or anonymous record for an object but cannot introduce a named tag. Block `extern` objects keep lexical aliases to canonical linked entities, preserve visible internal linkage, and lower without automatic storage. Block typedefs retain lexical ordinary-name scope and stable graph types, support exact same-type repetition and nested shadowing, and lower without runtime work. Block function declarations keep lexical aliases to canonical linked functions, preserve visible internal linkage, form compatible composite types, and lower without storage or instructions. Block enums retain lexical tag and enumerator scope, evaluated target values, and an instruction-free declaration path. Definitions in record members, function-definition parameter lists, and block type names use function-prefix or expression and initializer activation records to preserve their exact source point. Represented uses become integer IR without storage or an ELF record. GNU C mode gives `__builtin_va_list` the target `char *` type and publishes typed start, argument, copy, and end expressions with checked cursor and final-parameter use. Variadic reads accept represented four-byte pointers, four-byte or eight-byte integers including compatible-width enums, `double`, and non-atomic `long double`. Empty identifier-list definitions retain a non-prototype type, and unprototyped calls apply lvalue conversion, array and function decay, and integer promotion and `float` to `double` promotion as required. Same-kind `float` and `double` values cross objects, assignment, calls, and returns and support unary plus and minus and binary addition, subtraction, multiplication, and division. Source head and the checked seed apply cast and assignment conversion from every represented signed or unsigned integer through 64 bits to `float` or `double`. Runtime arithmetic, all six comparisons, and conditional selection apply the same usual arithmetic conversions; only the selected conditional arm converts. Modifiable non-atomic `float` and `double` lvalues also support prefix and postfix increment and decrement, with one lvalue evaluation. Assignment and explicit casts also convert runtime `float` or `double` values to represented unsigned four-byte targets. Non-atomic automatic `long double` values accept bounded finite normal decimal `L` tokens and add floating-width conversion, arithmetic, calls, returns, variadic reads, and all six matching or mixed floating comparisons. File-scope and block-static scalars, fixed arrays, and complete records accept implicit zero initialization for non-atomic `long double` leaves. An explicit leaf accepts a represented integer constant expression or a bounded decimal `L` literal with parentheses and unary signs. Static initializer conversion covers `_Bool`, plain `char`, signed and unsigned integers at 8, 16, 32, and 64 bits, and an enum whose compatible integer type has the represented target layout. Integer destinations other than `_Bool` truncate toward zero before the range check; `_Bool` tests the original floating value. An integer-valued zero keeps `ZERO` initializer metadata. Canonical x87 zero, subnormal, normal, infinity, and NaN payloads pass the frontend boundary. Binary32 and binary64 infinities keep their sign when widened, and every NaN uses the canonical quiet x87 payload. Non-atomic values at all three represented widths also support unary `!`, `&&`, `||`, the controlling operand of `?:`, the conditions of `if`, `while`, `do`, and `for`, and conversion to `_Bool`. Runtime casts, assignments, arguments, and returns convert between `long double` and signed or unsigned integers at 8, 16, 32, and 64 bits. Runtime arithmetic, all six comparisons, and conditional selection apply the usual arithmetic conversion from every represented value integer or enum to `long double`; only the selected conditional arm is converted. Comma expressions evaluate left to right and retain the last operand. GNU mode publishes immutable assembly records for the active CSPRNG statements, operand-free function statements, the exact per-CPU pointer output, all eight width-aware port-I/O helpers, and the active one-, two-, and four-byte GNU integer atomic operations. Operand-free statements are implicitly volatile and own empty operand slices. A modifiable four-byte object or `void` pointer may use `=r` for `mov %%gs:0, %0`. The port forms retain fixed-register widths, read/write pointer and count operands, and the INSW memory clobber. The checked-seed C11 standalone-header frontier passes 161 of 163 inputs; `scheduler.h` and `simd_intrin.h` retain exact C11-profile failures. The checked seed and source head map `U0`, the signed and unsigned sized integer spellings, `Bool`, `bool`, `float4`, and `double2` into the shared type graph in Cupid mode. C11 keeps those names available to ordinary declarations. The unchanged `simd_intrin.h` then publishes all 29 function bindings under its proper Cupid profile. `cpu.h` passes through the represented RDTSC form, all three `percpu.h` roots pass the integer atomics, and `ports.h` passes through the represented port-I/O forms. All 19 static Toolchain sources, all fifteen transferred contracts, and the exact Doom `d_main.cc` profile parse completely under their current i386 profiles. The checked seed carries this frontend boundary, including the wide integer and ordinary floating extension. ADR 0292 records its current carriage. ADR 0228 records its promotion and the complete Cupid-built contract proof. The active graph contains 246 CupidC transforms with no ordinary C translation unit. All 239 checked-in roots, including the 156 strict non-Doom and 83 Doom roots, plus the generated kernel-symbol translation use `.cc`. ADR 0124 records the naming boundary, ADR 0181 records the latest production transfer, ADR 0199 records long-double comparisons, ADR 0202 records floating truth, ADR 0250 records unsigned four-byte floating conversion, ADR 0251 records static long-double data, ADR 0253 records runtime conversions between `long double` and integers, ADR 0254 records the corresponding static initializer conversions, and ADR 0255 records static control folding and finite floating-width conversion. ADR 0256 records canonical x87 payloads and special-value conversion. Target-only static long-double `+`, `-`, `*`, and `/` round to the x87 representation and publish exact initializer data without runtime work; ADR 0260 records that arithmetic boundary. ADR 0263 records hosted floating updates, ADR 0287 records mixed integer and `float` or `double` conditional arms, ADR 0288 records runtime integer and long-double usual conversions, and ADR 0289 removes the four-byte integer limit for `float` and `double`. Chained and overriding designators, promoted anonymous-member designators, union and Cupid class lists, remaining address forms, nonempty identifier-list definitions, block declaration attributes, nested function definitions, hexadecimal floating literals, binary32 and binary64 subnormal literals, hexadecimal or subnormal long-double literals, decimal ratios beyond the bounded parser, remaining floating-to-wide conversions, integer-lvalue compound assignment with a floating right operand, atomic and long-double updates, computed goto, GNU label addresses, pointer and eight-byte atomics, runtime memory orders, the broader GNU surface, broader lowering, kernel integration, and self-hosting remain. |
| Shared `toolchain/cupidc_ir.*` linear IR module | Checked-seed CupidC builds the freestanding operation and normal hosted contract, then lowers the active graph through it; host C builds only the optional native oracle | CupidC builds the same module, and the shared frontend lowers both C and Cupid function bodies through it. | `ctool_c_lower_ir` publishes immutable typed function slices whose abstract stack distinguishes addresses from scalar and structure values. Absolute frontend identities cover parameters, block bindings, compound literals, runtime string expressions, file bindings, calls, linked functions, and graph members. The represented slice includes one-byte, two-byte, and four-byte integers, same-kind `float` and `double` values, four-byte object and function pointers, supported structures, target-sized automatic arrays and records, initializer lists, narrow string roots and leaves, block-scope compound literals, fixed calls, scalar variadic calls and callees, empty identifier-list definitions, unprototyped calls, assignment and mutation, including represented four-byte bit fields, structured control flow, `switch`, labels, and `goto`. The eight-byte integer path covers constants, matching conditional arms, fixed call results, file and block-static objects, fixed automatic objects, pointer dereferences, members, indexes, initialization, plain and chained assignment, all ten compound assignments, prefix and postfix update, declared parameters, named call arguments, ellipsis and unprototyped call arguments, variadic reads, discard, returns, addition, subtraction, multiplication, division, remainder, unary plus, unary minus, bitwise complement, left and signed or unsigned right shifts, AND, OR, XOR, all six comparisons, logical not, short-circuit logical operators, conditional selection, structured scalar conditions, signed and unsigned switch dispatch, and conversion to or from represented integer widths through one abstract handle. Mutation duplicates the lvalue address and uses one wide snapshot load before the existing operation and store instructions. Switch lowering duplicates the value handle while full-width equality tests select resolved case targets. An `ASSEMBLY` instruction consumes one source-ordered output-then-input operand slice, which may be empty for an operand-free statement. Operand-bearing forms evaluate output addresses before input values. The per-CPU form retains its pointer output, validates a modifiable four-byte pointer with exact `=r`, and evaluates the destination once. Whole-unit validation checks the packed partition, statement ownership, value category, type, constraint, and numeric tie. A lexical prepass validates block-enumerator activation through function prefixes, expression child order, and initializer forests before control-flow lowering. Calls retain their actual argument count after frontend promotion. Each call instruction also retains `first_argument_type`, which indexes the unit's packed, emitted-order `argument_types` slice. Zero-argument calls keep the current packed cursor, and non-call instructions keep the sentinel. `VARIADIC_START` keeps the final named parameter, `VARIADIC_ARGUMENT` keeps the cursor and loaded value types, `VARIADIC_END` consumes the cursor address, and cursor copy uses scalar `STORE`. `STRING_LITERAL_ADDRESS` keeps a runtime expression identity, while `COPY_STRING` keeps a semantic initializer identity and consumes its selected character-array address. Floating `LOAD`, `STORE`, `STORE_VALUE`, `DISCARD`, fixed direct or indirect calls, `VARIADIC_ARGUMENT` for `double`, `RETURN_VALUE`, default argument `CONVERT`, and same-kind arithmetic `UNARY` or `BINARY` instructions keep one semantic handle per C value. A `float` carries four raw bytes, while a `double` names a private eight-byte snapshot. Floating updates evaluate one lvalue address, apply exact-width `1.0`, and use `STORE_OLD_VALUE` to return the original postfix payload. Typed conversions admit runtime `float` and `double` inputs to represented unsigned four-byte targets. Integer-to-`float` or `double` conversions accept every represented signed or unsigned width for casts and assignment conversion. A bounded finite normal decimal `L` token publishes its exact 64-bit significand and biased x87 exponent. A non-atomic automatic `long double` names a twelve-byte snapshot for object access, calls, variadic reads, returns, arithmetic, and all six matching or mixed comparisons. Non-atomic file-scope and block-static scalars, fixed arrays, and complete records lower `long double` leaves when those leaves are implicitly zero, use a represented integer constant expression, or carry a bounded decimal `L` literal with parentheses and unary signs. The initializer record keeps the 64-bit significand and 16-bit sign and exponent. Validation accepts canonical x87 zero, subnormal, normal, infinity, and NaN classes and rejects malformed explicit-bit or misplaced metadata. Every integer initializer leaf must unwrap either to a primitive with a recognized standard integer kind and Cupid's fixed target size and signedness, or to an enum whose compatible type meets that rule. A primitive base must use its canonical target size, signedness, and alignment. Wrapper and base representations must match on size, signedness, integer, object, and completeness flags. An enum's compatible integer kind must be recognized. The enum, its unwrapped base, and its compatible type must agree on those five fields and on alignment. A `QUALIFIED` node copies referenced alignment unless it introduces `_Atomic`. An atomic introduction at any layer raises alignment to at least the target atomic alignment. An `ALIGNED` node requires an explicit, nonzero power-of-two alignment and may lower the referenced alignment. `_Bool` has one payload bit; other kinds use their full target widths. Stored bits must fit that width, clear the high word, and omit expression, string, address, and list metadata. The validator runs during whole-unit initializer ownership checks and block-static declaration lowering. Unary plus and minus and binary addition, subtraction, multiplication, and division require matching non-atomic operand and result types. Truth operations keep the floating input type on logical-not and zero-branch instructions, while conversion to `_Bool` keeps it on `CONVERT`; atomic floating types fail validation before emission. Runtime conversions between `long double` and signed or unsigned 8, 16, 32, or 64-bit integers retain exact source and destination types. A `USUAL_ARITHMETIC` conversion from every represented value integer or enum to `long double`, `float`, or `double` is valid when that is the floating common type for runtime arithmetic, all six comparisons, and conditional selection. A focused 32-function IR fixture locks the wide conversion and operator inventory, selected-arm locality, repeat determinism, malformed metadata rejection, allocation failure, and same-job recovery. Static initializer conversions keep exact integer bits or x87 payloads, including enum identity and destination qualifiers. Integer destinations other than `_Bool` truncate toward zero before the range check; `_Bool` tests the original floating value. The shared fixture proves the order with `-0.5L`, which becomes one for `_Bool` and zero for an unsigned integer. The active graph uses this path across 246 CupidC transforms. ADR 0181 records the latest production handoff, ADR 0199 records long-double comparisons, ADR 0202 records floating truth, ADR 0250 records unsigned four-byte floating conversion, ADR 0251 records static long-double data, ADR 0253 records runtime conversions between `long double` and integers, ADR 0254 records the corresponding static initializer conversions, and ADR 0255 records static control folding and finite floating-width conversion. ADR 0256 records canonical x87 payloads and special-value conversion. Static long-double arithmetic reaches Linear IR as 85 initializer nodes and 80 list edges with no runtime function, instruction, argument-type, or file-assembly record; ADR 0260 records that boundary. ADR 0263 records hosted floating updates. ADR 0288 records runtime integer and long-double usual conversions. ADR 0289 records wide integer conversion and usual arithmetic with `float` and `double`. Hexadecimal floating literals, binary32 and binary64 subnormal literals, hexadecimal or subnormal long-double literals, decimal ratios beyond the bounded parser, other floating-to-wide conversions, integer-lvalue compound assignment with a floating right operand, atomic and long-double updates, unsupported aggregate categories, Boolean mutation, non-four-byte and partial volatile bit-field mutation, non-scalar values without declared parameter types, atomic cursors and reads, aggregate variadic reads, computed `goto`, GNU label addresses, broader GNU assembly forms, broader production integration, and self-hosting remain. |
| Shared `toolchain/cupidc_emit.*` object module | Checked-seed CupidC builds the freestanding operation and normal hosted object contract, then emits the active graph through it; host C builds only the optional native oracle | CupidC builds the same operation and receives static data and function IR from the shared frontend path. | `ctool_c_emit_object` writes deterministic i386 `ET_REL` through the shared ELF32 writer. It owns target-sized automatic and compound-literal frame slots, block-static and file symbols, structure, wide-integer, and `double` snapshots, four-byte floating semantic slots, fixed, scalar variadic, and unprototyped cdecl calls, scalar variadic cursor traversal, sixteen-byte call-site alignment, branches, direct-symbol relocations, represented bit-field read, assignment, compound, prefix, and postfix stores, and checked x86 encoding. An eight-byte constant, call result, lvalue load, widening conversion, variadic read, or wide operation result receives an instruction-owned frame snapshot. The emitter uses `ADD`/`ADC` and `SUB`/`SBB` pairs for two-word arithmetic, combines one `MUL` result with two `IMUL` cross products for multiplication, and uses a fixed 64-step restoring loop for division and remainder. Signed quotient and remainder operations run on unsigned magnitudes before applying C's quotient and remainder sign rules. Unary plus keeps its immutable input handle; multiplication, division, remainder, negation, and complement write fresh snapshots. Represented operand-bearing GNU assembly reserves fixed registers before assigning general outputs, preserves EBX in private frame storage, and snapshots every output before restoring registers. Operand-free statements use no frame slot or EBX traffic. The shared x86 model emits RDTSC, CPUID, RDRAND, SETC, PAUSE, NOP, STI, HLT, CLI, CLD, SFENCE, and FNINIT for the represented templates. It also emits the exact `65 A1 00 00 00 00` absolute GS load for `mov %%gs:0, %0`; pointer outputs are rejected in every other template, including otherwise valid instructions before or after that load. The emitter also carries two-word shifts and bitwise operations, narrows wide values to canonical represented lanes, compares both words, and ORs both words for Boolean conversion or branch conditions. Wide mutation duplicates the single-word address handle, loads and stores through the existing wide snapshot path, and leaves prefix or compound results available to the expression. Wide switch dispatch duplicates the snapshot handle and uses the same complete equality path for each case. Wide stores and declared wide arguments copy eight bytes from a snapshot, and returns restore EDX:EAX. A cursor starts after the full width of the final named cdecl parameter. Represented non-atomic pointers and four-byte integers load one slot and advance the cursor by four. Signed and unsigned wide integer or `double` reads copy eight bytes into one snapshot and advance the cursor by eight. Copy preserves the cursor value, and end has no target state change. The packed actual argument types determine each outgoing ABI width. A wide integer, existing `double`, or source `float` promoted to `double` at an ellipsis or unprototyped call copies eight bytes into adjacent stack positions while the abstract value remains one handle. Default argument promotion uses `FLD` on the four-byte source and `FSTP` into a fresh eight-byte snapshot. Fixed floating results arrive in x87 `ST0`; after call cleanup, the emitter uses `FSTP` to produce a raw four-byte `float` value or an eight-byte `double` snapshot. Same-kind unary minus uses `FCHS`. Addition, subtraction, multiplication, and division load the left operand before the right operand and use the `ST1` operation `ST0` forms. A bounded finite normal decimal long-double constant writes three exact words to a twelve-byte snapshot and loads it with `FLD m80`. A long-double comparison loads right then left, emits `FUCOMIP ST0, ST1`, discards the surviving x87 value, and reuses the normalized predicate path. Truth materialization compares `float` and `double` with SSE zero and automatic `long double` with x87 zero. An explicit parity branch makes NaN true, while signed zeros remain false; the x87 sequence uses `FLDZ`, `FUCOMIP`, and `FSTP` without changing the register-stack depth. Every changed result is immediately stored at its C width, which gives a `float` a fresh four-byte slot and a `double` a fresh snapshot. Prefix floating update returns the stored value, while postfix stores the replacement and returns the old raw value or snapshot. The call metadata drives argument order, indirect callee placement, alignment, and cleanup. Runtime `float` and `double` to unsigned four-byte conversion widens binary32 exactly, splits at 2^31, truncates each defined half through the signed instruction, and restores bit 31 for the upper half. Runtime conversion between `long double` and every signed or unsigned i386 integer width uses `FILD` and truncate-mode `FISTP`, then restores the caller's x87 control word. The integer-to-long-double sequence also serves runtime arithmetic, all six comparisons, and conditional selection for every represented value integer or enum. Wide integer input to `float` or `double` uses 64-bit `FILD`, the unsigned 2^64 correction when needed, and a binary32 or binary64 store. The 63-function conversion object contains 10,513 text bytes with fingerprint `01725E63`, 64 symbols, and 123 relocations. Its decoder lock covers 27 wide loads, 18 control-word save and restore pairs, and nine unsigned correction branches. Twenty-two execution cases cover signed and unsigned endpoints, precision boundaries, arithmetic, every predicate, and both conditional directions. Static conversion leaves reuse the exact integer and x87 data writers and emit no runtime work. Static floating initializer leaves write exact little-endian binary32 or binary64 data and classify positive zero separately from negative zero. Non-atomic file-scope and block-static scalars, fixed arrays, and complete records may contain `long double` leaves initialized from a represented integer constant expression or a bounded decimal `L` literal. The emitter writes ten exact payload bytes, clears both padding bytes, selects `.bss`, `.data`, or `.rodata`, and retains direct-symbol `R_386_32` relocations. Static data and frozen floating instructions accept canonical x87 zero, subnormal, normal, infinity, and NaN payloads through the same validator. Runtime narrow strings receive local `.LCn` objects in `.rodata`; `STRING_LITERAL_ADDRESS` and `COPY_STRING` produce `R_386_32` text relocations, and copies use `CLD` plus `REP MOVSB` while preserving ESI and EDI. The active graph uses these object semantics across 246 CupidC transforms. ADR 0181 records the latest production handoff, ADR 0199 records long-double comparisons, ADR 0202 records floating truth, ADR 0250 records unsigned four-byte floating conversion, ADR 0251 records static long-double data, ADR 0253 records runtime conversions between `long double` and integers, ADR 0254 records the corresponding static initializer conversions, and ADR 0255 records the folded static data proof. ADR 0256 records canonical x87 payloads and special-value conversion. The 80-result static arithmetic proof emits a deterministic 1,540-byte object with exact twelve-byte x87 leaves, zero padding, no `.text`, and no relocation; ADR 0260 records that object boundary. ADR 0263 records hosted floating updates. ADR 0289 records wide integer conversion and usual arithmetic with `float` and `double`. Static-duration compound literals, wide strings, literal pooling, remaining aggregate forms, non-four-byte bit fields, partial volatile bit-field mutation, atomic cursors and reads, hexadecimal floating literals, binary32 and binary64 subnormal literals, hexadecimal or subnormal long-double literals, decimal ratios beyond the bounded parser, remaining floating-to-wide conversions, integer-lvalue compound assignment with a floating right operand, atomic and long-double updates, non-scalar values without declared parameter types, broader GNU assembly forms, integration with the private in-kernel compiler, and a native/Python-free self-hosting path remain open. |
| `kernel/cpu/idt.cc`, `kernel/mm/paging.cc`, and `kernel/smp/lapic.cc` | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. Independent `r` inputs carry four-byte integers or data pointers, while `c` carries a four-byte integer in ECX for the exact CR0, CR2, CR3, and CR4 moves and RDMSR. Double compilation produces validated byte-identical objects of 8,756, 2,336, and 4,184 bytes. The normal image and four-vCPU runtime gate pass. WRMSR, unsupported control-register directions, arbitrary templates, fixed EBX and `q` inputs, and general clobbers remain open. ADR 0117 records the compiler boundary, and ADR 0123 records the production transfer. |
| All 20 `kernel/crypto/*.cc` sources | Checked-seed CupidC through the root Makefile, with Python and WSL orchestration as needed | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. The exact cohort compiles twice to 204,132 byte-identical i386 `ET_REL` bytes, rebuilds with a poisoned host compiler, and links into the complete image. Sixty-two boot checks cover the primitive suite, ASN.1 success and failure, X.509 parsing and names, chain state, embedded-root lookup, and RDRAND-backed CSPRNG startup. The X.509 smoke does not claim full trust validation. Optimization and the Python/WSL execution bridge remain open. |
| `kernel/smp/acpi.cc` and `kernel/smp/mp_tables.cc` | Checked-seed CupidC through the same verified kernel wrapper | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. Their 5,708-byte and 4,156-byte objects are deterministic and validated. A poisoned-host rebuild proves both Make recipes, while the four-vCPU runtime gate covers MP fallback, ACPI MADT discovery, every CPU online, RDRAND, 62 crypto checks, e1000, desktop, terminal, and JIT completion. |
| ATA, keyboard, mouse, PCI, PIT, RTC, RTL8139, speaker, VGA, AC97, syscall, shell, EHCI, and UHCI sources | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. These 14 sources established the earlier 40-source frontier. Their recipes pin exact recursive header closures and checked-seed controls. The strict syntax, deterministic double-compile, poisoned-host rebuild, focused test, full-image, and both QEMU gates pass. The 44 USB tests and 62 GUI gate unit tests pass. Live e1000 and RTL8139 runs complete the command suite, SMP, network, audio, UHCI input reattachment, and six EHCI storage lifetimes. |
| Seventy-one kernel/driver `.cc` sources plus `ctool.cc`, `cupidasm.cc`, `cupiddis.cc`, `elf32.cc`, and `x86.cc` | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. These 76 sources established the 116-source frontier. The five Toolchain roots now use `.cc`; native GCC or Clang recipes select C explicitly with `-x c`. Recursive dependencies and poisoned-host runs cover each recipe. Data-only `ET_REL` output is accepted without `.text` when the rest of the object is valid. |
| Strict non-Doom CupidC naming boundary | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | The 156 checked-in roots and generated symbol source all use `.cc`. The five Toolchain roots shared with the 19-source fixed point keep C semantics through explicit `-x c` in native recipes. ADRs 0115 and 0123 record the first 28 ownership transfers, ADR 0124 records the next 111 renames, ADR 0126 records the fixed-point rename and old-seed proof, ADR 0129 records the lexer transfer, ADR 0135 records the Nuked OPL3 transfer, ADR 0139 records the JPEG and glyph-raster transfer, ADR 0167 records the FPU and SMP transfer, ADR 0176 records the libm transfer, ADR 0180 records the kernel entry and SIMD transfer, ADR 0181 records the string transfer, and ADR 0276 adds CupidLD. The latest complete two-pass frontier covers the preceding 155 roots. The current 156-source production build passes, while a broader two-generation frontier run timed out after 1,204 seconds and remains incomplete. |
| `kernel/core/process.cc` FXSAVE frontier | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. The deterministic 30,216-byte object contains both exact `0F AE 00` FXSAVE instructions and passes the shared relocatable validator. The normal Make recipe and runtime path now use this object. ADR 0119 records the compiler boundary, and ADR 0123 records the transfer. |
| Strict checked-in core/driver/tool C cohort | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | All 156 strict checked-in roots are CupidC-owned. `kernel/core/string.cc` was the final source transfer, and `toolchain/cupidld.cc` later joined the checked kernel link for in-kernel AOT. The checked seed accepts the complete x87 control-word and round-down statement in unchanged `str_floor()`, including its exact AX and memory clobbers. Two compiles of that extracted active helper produce the same 420-byte object with SHA-256 `448012fe57ec625c6075e97cf91163b994a0443238c5d6bdf25e4b839763f14e`. It also accepts the later explicit non-atomic `double` to `uint64_t` casts. Two full compiles of unchanged `kernel/core/string.cc` produce the same 14,460-byte object with SHA-256 `d48bb6ea18b7124fbefeaca0d5d5ee8a517db950f21ea88e30ededd6c5c2a577`. The wrapper freezes its two-header closure and publishes the validated object without a host compiler. ADR 0181 records the transfer, and ADR 0276 records the CupidLD addition. |
| `kernel/core/kernel.cc` | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. The wrapper freezes the 31,174-byte source and its exact 63-header recursive closure. The checked seed emits a 25,920-byte object with SHA-256 `ed42676ad0d7f16b1fb83442ead1b0082781324dca719104922099cee34b5ab0`. CupidDis decodes the `0x01100000` stack reset, linked BSS clear, `kmain()` call, and halt loop. Poisoning `CC` leaves the Make recipe on CupidC. ADR 0175 records the compiler boundary, ADR 0179 records seed carriage, ADR 0180 records production ownership, and ADR 0187 records the active stack placement. |
| `kernel/cpu/libm.cc` | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. The checked wrapper freezes the 43,736-byte source with `kernel/core/types.h` and `kernel/cpu/libm.h`, then emits a deterministic 16,164-byte validated ELF32 object. Seven aligned source mnemonics now emit `DC E9` for the intended range-reduction subtraction without changing the algorithm, stack order, source size, or ABI. A poisoned normal recipe proves that GCC, Clang, NASM, host assemblers, linkers, and binary utilities do not produce the object. The guest smoke runs `/bin/feature15_libm.cc` and requires seven focused x87 checks, all 29 checks, both zero-failure summaries, and `PASS feature15_libm`. General GAS and the wider GNU surface remain open. ADR 0176 records the transfer, and ADR 0209 records the numerical correction. |
| `kernel/cpu/simd.cc` | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. The wrapper freezes the unchanged 13,971-byte source and its exact seven-header recursive closure. The checked seed emits an 8,768-byte object with SHA-256 `fd280c321b8eb38a90d4f0982d70b8df0364585e3da322eb2c9de722e071f8d4`. CupidDis decodes the copy, broadcast, blend, saturating-add, and streaming-store instructions. Poisoning `CC` leaves the Make recipe on CupidC. ADRs 0160 and 0168 record the earlier compiler boundary, ADR 0174 records first seed carriage, ADR 0178 records packed SSE2 support, ADR 0179 records complete source carriage, and ADR 0180 records production ownership. |
| `kernel/smp/percpu.cc` descriptor-table root | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. Its four exact assembly forms load a packed six-byte GDTR, reload DS, ES, SS, and CS, and write a represented 16-bit selector to GS. Two complete compiles produce the same 6,760-byte object with SHA-256 `3c2c6f0e00e5edec1ca16cba91e9fc593d1c42e24f4ebd3591e5f574fb0dd772`. The wrapper freezes the recursive input closure, and the image plus four-vCPU dual-NIC runtime gates pass. ADR 0157 records the compiler boundary, and ADR 0167 records the transfer. |
| `kernel/audio/nuked_opl3.cc` | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. The checked seed recognizes the ordinary header declaration plus inline source definition as one C11 external definition. It also preserves inherited internal linkage and rejects external-linkage inline declarations without a definition. Two complete compiles produce the same validated 40,424-byte object with SHA-256 `a3a04ade4029d9333902bb93376fb5eef21f349ee5a1406bd0751cc4cee9f2a1`, and CupidDis reports a defined global `OPL3_Generate4Ch` with only `memset` undefined. The wrapper compiles from a private copy of the source and its three headers and refuses live input drift. At that checkpoint, the closed recipe, 155-root frontier, image builds, and dual-NIC runtime gates passed. ADR 0134 records the seed promotion, and ADR 0135 records the production transfer. |
| `kernel/gfx/jpeg.cc` and `kernel/gfx/glyph_raster.cc` | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. Their exact four-header closures, deterministic i386 objects, poisoned-host builds, strict frontier proof, image dependency, and guest JPEG and glyph paths pass. JPEG is 21,120 bytes with SHA-256 `ccabae9e3b979031079f1ed72189c990f3aee4aa773c6ec742b5ccc263570851`; glyph rasterization is 11,744 bytes with SHA-256 `83d2f4cac28abbc5bb8a92020ab7fb57251b1b927b4fdbc40981f29556aa1e80`. ADR 0139 records the transfer. |
| `kernel/lang/cupidc*` | Mixed checked-seed CupidC and GCC/Clang objects linked into the kernel | CupidC builds host and in-OS CupidC variants | `cupidc.cc`, `cupidc_lex.cc`, `cupidc_parse.cc`, and `cupidc_string.cc` are production-owned by checked-seed CupidC. The remaining support roots stay hosted. The production emitter tags loop and switch frames, cleans saved selectors for switch-local `break` and crossed-switch `continue`, accepts 128 active controls and 1,024 active statement calls, rejects the next entry before further recursion, and recovers in the same REPL. `/bin/feature25.cc` checks these paths. The shared core, host runtime seam, object writer, and staged self-build remain. |
| `toolchain/cupidasm.*`, `kernel/lang/as.cc`, and `kernel/lang/as_elf.*` | Checked-seed CupidC builds the shared frontend, the `as_elf.cc` kernel bridge, and the `as.cc` kernel adapter; GCC/Clang builds the hosted driver | CupidC builds the same shared frontend and adapters | Assembly semantics live only in `toolchain/cupidasm.*`; the legacy kernel lexer/parser was deleted. Source head and the checked execution seeds let a sectionless `equ` preamble precede the raw source's section claim. They reject a duplicate raw `ORG` or a switch to another raw source section with stable diagnostics before output. ADR 0292 records fixed-point carriage of the corrected `equ` rule. `as.cc` owns the 631 runtime definitions, fixed placement, and VFS/JIT policy. `as_elf` remains a checked buffer-only temporary `ET_EXEC` bridge. CupidC emits the complete static hosted command closure, CupidASM supplies startup, and CupidLD links it. Linux and WSL runs match native raw and ELF32 output plus invalid-source behavior. The normal hosted executable is built by staged CupidC and CupidLD; the host build remains an optional native oracle. |
| `toolchain/cupiddis.*`, its host driver, and `kernel/lang/dis.cc` adapter | Checked-seed CupidC builds the shared implementation and kernel adapter; GCC/Clang builds the native CLI | CupidC builds host and in-OS CupidDis variants | Inspection semantics have migrated to one host-runnable freestanding module; raw/static-`ET_REL`/static-`ET_EXEC` and `nm` views are checked. Raw requests accept one mode or ordered code16, code32, and data ranges. The CLI exposes `--range-at OFFSET:16|32|data` and retains `--mode-at OFFSET:16|32` for code-only maps. The normal SMP recipe freezes a private 4,096-byte CupidASM candidate, decodes its two code intervals with `--require-known`, treats the other 3,997 bytes as data, and publishes only after strict inspection passes. Every executable `ET_REL` relocation must own a compatible decoded four-byte field. The typed report publishes total and unmatched counts, and ordinary rendering shares the same lookup. Both checked production seeds carry this rule, so guarded CupidASM object publication enforces it on Windows and Linux. The `nm` view owns the normal build's kernel-symbol extraction transform; GNU/LLVM `nm` is oracle-only. CupidC emits the complete static hosted inspector closure. Windows production runs the checked native inspector directly, while Linux-contract work may still use WSL. Dynamic ELF, DWARF v4, and typed code/data boundaries inside executable sections remain. |
| 83 Doom port and vendored C files | Checked-seed CupidC through the exact `doom-compat` and `doom-tree` production profiles | Native normal-build CupidC with no Linux-seed execution bridge | Production-owned by CupidC. All 83 roots use `.cc`. The wrapper fixes exact three-source and 80-source allowlists, freezes the selected source and complete 291-file header space, rejects links and NTFS junctions, validates i386 `ET_REL`, rechecks the full source census and live bytes, and publishes atomically. The 69,366-byte input manifest fixes both source sets and every header hash without changing its timestamp on an unchanged scan; its SHA-256 is `47ba35158cac0a7df253a0056235223e62fee24df74701800f88763e588611c2`. The normal publisher derives a bounded `CUPROF1` snapshot and independent Python oracle from one stable capture, runs CupidObj from the exact frozen seed, requires byte parity, and rechecks the seed, live inputs, candidate, output directory, and existing output under an adjacent no-follow lock. CupidObj authors the production bytes while Python retains the host transaction. The `g_game.cc` object keeps its two static subobject relocations with addend 4 and measures 52,004 bytes. Repeated compatibility compiles produce 93,332-byte, 17,084-byte, and 10,352-byte objects. Active dglibc uses corrected returns-twice setjmp. Config and save replacement use HomeFS through native VFS rename, and the guest diagnostic batches its temporary mutations behind one checked container publish. Cache failure isolation, FAT durable publication and live-entry rules, HomeFS ownership, and corrupt-container rejection now sit in the same active path. The normal root has no host C transform. Earlier gates returned from two missing-IWAD launches. The fixed frontier now passes normal discovery, an explicit missing path, the shell-return marker, and a fresh CupidC-built `ls` on both NICs. Separate stateful frontier boots also pass after swap keeps a FAT handle open. Full IWAD gameplay, input, audio, menu-driven save/load, and persistence across reboot remain. ADRs 0184, 0211, 0214, 0242, 0243, and 0244 record the transfer, active corrections, format boundary, seed carriage, and normal publisher. |
| 108 `bin/*.cc` roots and 22 browser `.cc` fragments | CupidObj embeds source; in-OS CupidC compiles on demand | CupidObj embeds source; CupidC remains the language owner and can also be host-run | Production embedding has transferred to CupidObj; 107 top-level programs are runnable and the dedicated feature-13 source is compiled through `ccc` for the external AOT gate. Host-runnable/source self-hosting remains. |
| Three `user/examples/*.cc` programs plus `user/cupid.h` | Cupid-built syscall ABI gate, CupidC to `ET_REL`, then CupidLD fixed-text link; Linux uses the bootstrap seed directly; Windows uses it through WSL for the ABI contract and runs the checked native execution seed for output compilation and linking; root staging remains deliberate | CupidC/CupidLD build and a deliberate image-staging path | Compilation, link, and ABI-rule ownership have transferred. The precompile operation runs a staged CupidC contract that snapshots and rereads the six declarations while pinning version 5, 103 table fields, 101 providers, exported scalar types and constants, and both VFS record layouts. Python compares its report with an independent oracle and rechecks the fixed-point publication inputs. Closed wrappers freeze the sources, header, complete selected tool cohort, and build controls, validate relocatable objects and loader-compatible executables, and publish atomically. The 23-input default frontier repeats all six program artifacts. The optional 46-input native Windows frontier also requires every result to match the checked bootstrap seed. Poisoned-path tests reject conventional host code generators on the normal path. Separate private-image guest boots execute hello, ls, and cat from the staged image. ADR 0264 records the ABI transfer, and ADR 0272 records native execution-seed adoption. |
| Generated C tables (installation tables, CSS tables, and kernel symbol data) | Checked-seed CupidObj generates the three `.cc` installation tables and `kernel/cpu/ksyms_data.cc`; checked-seed CupidC compiles all four; Python generates only the remaining host-owned tables | CupidObj generates production installation and kernel-symbol tables; CupidC compiles them; Python may orchestrate | Installation-table and kernel-symbol generation are production-owned by Cupid tooling. The three inventory recipes use `$(CUPIDOBJ) install-source`; the two-pass kernel recipe captures checked CupidDis text and uses `$(CUPIDOBJ) ksyms-source`. `tools/hostbuild.py` coordinates the latter and remains the byte-parity oracle, but it does not construct any of these four production sources. The checked seed and installation oracle reject complete wrapped-symbol collisions while preserving one exact docs and home BMP alias. The symbol generator packs 114,851 logical bytes into little-endian `unsigned int` words and adds one zero byte to finish the last word. Its checked wrapper freezes the generated source and header closure, validates the i386 relocatable object, and publishes atomically. CSS tables and other generated C remain separately tracked. ADRs 0116 and 0123 record the earlier compiler transfers; ADRs 0201, 0203, and 0204 record installation-source capability, first seed carriage, and production ownership. ADRs 0205 and 0206 record the checked request corrections, and ADRs 0222 through 0224 record the kernel-symbol capability, seed carriage, and transfer. |
| Kernel ELF layout and symbol resolution | CupidLD with `link.ld`, two passes | CupidLD with the used linker-script subset | Production-owned: all 425 pass-one and 426 final objects link with exact oracle section/segment layout and symbol projection, deterministic first-occurrence merge ordering, repeated `ASSERT` support, and no normal-build GNU/LLVM linker invocation |
| Kernel symbol extraction | Checked-seed CupidDis `-n`, checked-seed CupidObj `ksyms-source`, then checked-seed CupidC; Python freezes inputs and checks parity | CupidDis inspection, CupidObj generation, CupidC compilation; Python orchestration if still useful | Production inspection, generation, and compilation ownership have transferred. CupidDis emits canonical text, CupidObj serializes the 114,851-byte logical blob, and the checked wrapper compiles its packed `.cc` translation. Python rejects malformed text, missing output, oracle mismatch, or live input drift before atomic publication. Every shared symbol keeps the same address between passes. The rebuilt image passes the four-vCPU GUI, terminal, audio, and in-OS CupidC runtime gate. ADR 0224 records the generation handoff. |
| Source, documentation, font, image, and other binary wrapping | CupidObj, with Python snapshot, parity, drift, and publication checks for JPEG | CupidObj/shared object library | Production-owned. The 175 source, manual, demo, and vocabulary transforms canonicalize CRLF to LF without changing lone carriage returns. Eight direct binary wrappers stay byte-exact. Names, section policy, symbols, empty inputs, deterministic repeats, failure rollback, and final-form SMP wrapping are contracted. The JPEG path freezes the repository bytes, runs checked CupidObj `wrap-jpeg` first under the original source identity, and accepts only a regular non-symbolic candidate. Python checks the accepted snapshot independently, requires unchanged bytes, rechecks live inputs, and publishes atomically. Progressive, unsupported, or malformed marker streams fail without replacing the old object. Host image converters are not part of the root path. The 800,860-byte JPEG object has SHA-256 `74ab86d88302c90385bb0b858632b0d6c4ac983d6be28c976dd1a3a348204b3e` on both Windows and Linux. ADR 0235 records the transfer. |
| Linked kernel ELF to raw kernel binary | One checked-seed CupidDis and CupidObj hostbuild transaction | Hostbuild validates and flattens one frozen cohort, then publishes the raw kernel atomically | Production-owned: hostbuild freezes the selected seed manifest and five artifacts, the 431-entry input manifest and cohort, and the existing `kernel.bin` boundary. Checked CupidDis validates the private cohort. Checked CupidObj writes physical-address-ordered file-backed `PT_LOAD` bytes with zero-filled gaps and BSS exclusion from the frozen final ELF. Hostbuild rechecks live trust inputs and the output before parent-relative atomic publication. Every failure preserves the prior raw kernel. At the first reviewed transaction checkpoint, the operation passed with exit 0 in 187.054 seconds and published an 8,946,332-byte raw kernel with SHA-256 `4f5f2591d01bcc4007773844e9bfb8112a16dd17fbd178014cc2056fefaab67d`. The earlier poisoned-host `make -j2` passed in 1,057.969 seconds when validation and flattening were separate. Definitive four-vCPU E1000 and RTL8139 boot frontiers passed with exits 0 in 794.034 and 758.667 seconds. Both passed SMP, frontier, framebuffer, AC97, and PC speaker checks without changing the source image. Later rows record the current kernel identities. |
| Disk, FAT, and ISO fixture construction and staging | Checked-seed CupidObj authors the pristine FAT16 template and the complete ISO fixture; Python hostbuild manages mutable disk state and guards both publications | CupidObj owns both deterministic templates; Python orchestrates FAT reuse, tree safety, parity, drift checks, staging, and guarded publication | Production image ownership is shared. The normal disk recipe passes the checked seed manifest to `tools/hostbuild.py image`, which freezes the bootloader, kernel, stage inputs, seed, and live output. Checked `cupidobj disk-template` authors the MBR, boot and kernel reserve, FAT16 boot sector, two pristine FATs, and empty root directory through the byte before cluster 2. Python builds the same template independently and requires exact byte parity. It preserves a valid existing FAT filesystem or uses the complete template for a fresh image, stages the frozen files, extends the candidate, rechecks every frozen input plus the seed and output, and publishes under a cross-process lock with atomic replacement. Checked CupidASM assembles the 4,096-byte spanning ISO file, and Python verifies it. For the enclosing ECMA-119 and `RRIP_1991A` image, hostbuild freezes the checked manifest and typed inventory, runs checked `cupidobj iso-fixture` first, compares the complete result with an independent Python render, rechecks the seed and live inputs, and publishes under a per-output lock. ADR 0239 records the source capability, ADR 0240 records seed carriage, and ADR 0241 records the production transfer. |
| Emulator verification | QEMU plus Python test harnesses | Same, augmented with staged bootstrap and tool parity tests | Retained test dependency; stabilize the observed GUI-terminal flake |
| `kernel/smp/smp.cc` naked IPI root | Checked-seed CupidC through the verified kernel wrapper | Native normal-build CupidC with no host execution bridge | Production-owned by CupidC. The two call wrappers have no C frame and keep one typed `R_386_PC32` relocation; the panic entry is a complete local halt loop. The earlier `.c` compiler proof produced an 8,444-byte object with SHA-256 `806509a6dd1ac7eb34b7ffcb67a1f8852950663a274145584d0260da76dcba54`. The production `.cc` object remains 8,444 bytes and has SHA-256 `bd3189b2a1a6d15728c559172f5d6acca0889103428085cec8cc1024742a22d1`; its existing `__FILE__` diagnostic accounts for the new hash. The wrapper, image, and four-vCPU dual-NIC runtime gates pass. ADR 0156 records the compiler boundary, and ADR 0167 records the transfer. |

The linked-kernel row's next poisoned-host checkpoint completed on
2026-08-13 through the checked native Windows execution seed. Its first
invocation stopped at the 602.5-second command limit; the resumed build
finished in 968.5 seconds, for 1,571.0 seconds of cumulative work. Its
identities superseded the earlier outputs cited in that row when the checkpoint
was recorded. The 2,560-byte
`boot.bin` has SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`.
The 9,056,612-byte pass-one ELF has SHA-256
`e2f63b5cd9c4e2769b9d6bc893ab5cf778951b97aec954ece6cbac0cc429e92a`,
the 9,179,492-byte final ELF has SHA-256
`1bc06263dbf9849e6d2c594b6fb4be2a3f3b673c91f69d23a2d2e639b1f64776`,
and the 8,962,776-byte raw kernel has SHA-256
`3170aa71eafa656b1f6e23c918f1f472860f513c9c5cd0376d7d4f5f8a7d891c`.
The exact-size prerequisite accepted all nine artifacts before publishing the
209,715,200-byte image with SHA-256
`3b5dd6523a90d6ed0543a6ab2464892f3289b876654f9869f88db0901940b91e`.
A four-vCPU RTL8139 frontier passed from this image in 820.7 seconds. Private
CupidC emitted both feature-13 derived-update markers, compiled and loaded the
dedicated external ELF as PID 4, and reported that same PID exiting. The full
SMP, framebuffer, audio, USB detach/replug, and survival checks passed. The
completed dual-NIC checkpoint immediately before this
rebuild used image SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.

The current linked-kernel checkpoint includes in-kernel CupidLD and the
guarded normal boot edge. A poisoned-host normal build passed in 674.693
seconds after CupidDis accepted all 431 production inputs. The pass-one ELF is
9,211,340 bytes with SHA-256
`2a6f5deafb580b30254483179d6dade9ed4ed7b17b39f9368137b1ff14932263`.
The final ELF is 9,334,220 bytes with SHA-256
`bc855462c1f8f42e34d94a974443f7c6e565d60b1913e3b6f33b3e6e375f3ed6`,
and the raw kernel is 9,114,084 bytes with SHA-256
`8b5d73e74538ce11c1fb074f88b3852d690038aa5cb3a8de3ce222e9df88cade`.
The resulting 209,715,200-byte image has SHA-256
`813c9b0c78f795c1ac9fcff59b9c4111a958a07eb1e3943dc7af60c536521110`.
A private four-vCPU QEMU boot reached JIT completion in 49.257 seconds.

A fresh build of the three user programs in a unique output directory passed
in 10.492 seconds and reproduced all six promoted-frontier artifact hashes.
Disposable staged-copy runs returned 0 for hello in 54.546 seconds, ls in
52.637 seconds, and cat in 80.043 seconds. Cat used a 62-byte marker-shaped
fixture and passed the negative serial-event boundary. Both the source and
evidence images retained the current image hash above.

The checked-seed CupidDis row has an enforceable code-quality policy as
well as rendered inspection. `--require-known` validates multiple files with
typed known, unknown, invalid, and truncated counts and excludes bytes that the
caller or ELF metadata classifies as non-code. The normal kernel path applies
that policy and flat extraction to one frozen cohort of all 429 audited root
object outputs plus the pass-one and final kernel ELFs. The 9,076-byte LF-only manifest lists all 431
unique paths in graph order with SHA-256
`4f1936423ae06418fc2f75603c29a91997608fe82f48c323321523aed25a2ab0`.
Make retains every path as a direct prerequisite. The first separate gate
froze and rehashed the preceding 429-path manifest and inputs and passed in 185.526 seconds with
empty streams and exit 0. At the next handoff checkpoint, the transaction also
froze the selected seed manifest and five artifacts plus the existing
`kernel.bin` boundary. Hostbuild ran checked CupidDis and checked CupidObj
against the private cohort, rechecked live trust inputs and output, and
published through parent-relative atomic replacement. Every failure preserved the prior raw kernel. The transaction
passed in 187.054 seconds with exit 0. The Windows and WSL hostbuild suites
each passed 31 tests, with platform-specific skips. Pinned-path private
extraction remains deferred maintenance. ADR 0262 records the boundary. ADR 0266
records the immutable first-opcode decoder index and checked 128 KiB throughput
contract. ADR 0265 records seed carriage and production adoption.

The final audit records 452 transforms across the three supported roots and
443 under root `all`. Its tool participation totals are Python 452, CupidC
246, CupidObj 192, CupidASM five, CupidLD five, and CupidDis six. It retains
  the 5/18/17 fixed-point matrix. `make bootstrap-audit` passed in 69.0
seconds.

The same poisoned-host root build produced a 2,560-byte `boot.bin` with
SHA-256 `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`,
a 9,039,936-byte pass-one ELF with SHA-256
`b21fa8954499a7857ee4b12fa3950fcc08ff3c6a6234c8ae72effc38c51fdc6d`, and a
209,715,200-byte image with SHA-256
`4548005bd0aa1a3cffb74620c2309d53c6b291ea2505ed187034bf6b13f1bb37`.

ADR 0141 records `noinline` and
`target("general-regs-only")` semantics. ADR 0156 adds exact naked IPI
semantics. All three facts survive compatible
function redeclarations and remain visible on the published IR function. The
checked seed carries all three facts, but this capability changes no owner,
recipe, source suffix, object, or normal image yet.

ADRs 0067 through 0075 supersede the older wide limits in the `cupidc_ir` and `cupidc_emit` rows. Shared IR carries a declared wide parameter, each supported operation result, each mixed-width conversion, each scalar condition, each switch control, each mutation value, and each variadic read as one handle. A packed sequence holds every call's post-conversion actual types in emitted call order, while each call's slice preserves source argument order. One shared validator checks that sequence before the emitter reads it. Adversarial contracts cover missing entries, gaps, overlaps, invalid type indices and spans, non-call owners, trailing entries, and recovery. The emitter counts each argument's ABI width in the outgoing area, copies an open-position wide integer into adjacent low and high words, gives multiplication, division, remainder, and other value-producing operations their required snapshots, advances a wide variadic cursor by eight bytes, compares or tests both words, duplicates a snapshot handle for full-width case dispatch, and duplicates a destination address once for mutation. Copied-cursor and nested-call execution cases check snapshot independence, alignment, cleanup, and the returned bits. The checked-seed X25519, socket, and TCP objects use parts of this path. CupidC now builds the corresponding contract in the normal gate; the native runner is only an oracle.

ADRs 0076, 0077, 0079, 0091, 0125, 0136, 0137, 0147, 0196, and 0199
supersede the older floating gaps in the `cupidc_frontend`, `cupidc_ir`, and
`cupidc_emit` rows. Those modules transport exact `float` and `double` values,
perform default `float` promotion, convert between floating widths, represent
decimal constants, convert the supported represented integer widths, evaluate
matching or mixed runtime arithmetic, select matching or mixed floating
conditional arms, store the four arithmetic compound assignments, emit
target-width static floating data, and evaluate all six runtime comparisons
with IEEE unordered behavior. Static initializers use integer-only binary32
and binary64 arithmetic for unary signs, arithmetic, comparisons, casts,
scalar truth, logic, conditional selection, and represented integer
conversions through 64 bits. Automatic non-atomic `long double` values support
bounded finite normal decimal `L` tokens, local load and store, unary plus and minus, addition, subtraction,
multiplication, division, conversions to or from `float` and `double`,
twelve-byte direct and indirect fixed, variadic, and unprototyped arguments,
function returns, direct and indirect call results, and `va_arg(long double)`
through 80-bit x87 memory operands. Matching long-double operands and mixed
`float` or `double` inputs support all six comparisons through `FUCOMIP`, with
the x87 stack balanced after every result. Runtime truth, structured
conditions, and conversion to `_Bool` cover `float`, `double`, and automatic
`long double`. Static-duration scalar, array, and complete-record leaves accept
implicit zero, a represented integer constant expression, or a bounded
decimal `L` literal with parentheses and unary signs. The object keeps exact
x87 payload bytes and padding in `.bss`, `.data`, or `.rodata`. Runtime
conversions between `long double` and every signed or unsigned i386 integer
width are supported. Runtime arithmetic, all six comparisons, and conditional
selection convert every represented value integer or enum to `long double`.
Only the selected conditional arm is converted. Static initializer conversion covers those widths,
`_Bool`, plain `char`, and an enum whose compatible integer type has the
represented target layout. Static truth, all six comparisons, short-circuit
logic, conditional selection, and conversion between represented floating
widths use target-only evaluation and leave no runtime IR. Canonical x87
zero, subnormal, normal, infinity, and NaN payloads cross every frozen
boundary, and width conversion uses deterministic infinity and quiet-NaN
encodings. Static long-double `+`, `-`, `*`, and `/` use the same target-only
model, with nearest-even rounding, gradual underflow, canonical special
values, and no runtime IR.
Hexadecimal floating literals, binary32 and binary64 subnormal literals,
hexadecimal or subnormal long-double literals, decimal ratios beyond the
bounded parser, other floating-to-wide conversions, integer-lvalue compound
assignment with a floating right operand, atomic and long-double updates,
SIMD, over-aligned
emission, production integration, and self-hosting remain open. ADR 0250
records runtime conversion to unsigned four-byte targets. ADR 0251 records
static long-double data. ADR 0253 records runtime long-double and integer
conversion. ADR 0254 records static initializer conversion. ADR 0255 records
static controls and finite width conversion. ADR 0256 records canonical x87
payloads and special-value conversion. ADR 0260 records static long-double
arithmetic. ADR 0287 records the first source-head integer and floating
conditional boundary. ADR 0288 records runtime integer and long-double usual
conversions. ADR 0289 removes the four-byte integer limit for `float` and
`double` conversion and usual arithmetic.

ADR 0263 adds hosted prefix and postfix update for modifiable non-atomic
`float` and `double` lvalues. The shared frontend records the computation
width, Linear IR evaluates the destination once, and the emitter preserves the
old payload for postfix results. ADR 0265 records checked-seed carriage.

ADR 0148 adds the exact volatile MOVSS round trip and its one-way load and
store forms to those three shared modules. The forms keep typed `float`
memory addresses, require the `xmm0` clobber, and emit through the shared x86
model. ADR 0150 adds the exact volatile x87 sine block in `stress_sin()`.
It keeps typed `double` memory addresses, requires no clobbers, and emits a
balanced `FLD`, `FSIN`, and `FSTP` sequence through the same model. ADR 0154
adds the exact x87 round-down block in `str_floor()`. It keeps the same typed
memory operands, requires the exact `ax` plus `memory` clobber set, reuses the
consumed input-address slot for control-word scratch, restores the caller's
x87 control word, and emits every instruction through the shared model.
General floating memory substitution and arbitrary register or x87 clobbers
remain open. The checked seed emits the separate cast and the complete
unchanged `kernel/core/string.cc` object. The normal recipe freezes the
source and its two headers, validates the object, and publishes it without a
host compiler. ADR 0170 records the boundary, ADR 0174 records seed carriage,
and ADR 0181 records production ownership.

ADR 0175 adds the exact BSS-clear statement at the start of `_start()`. The
public record keeps its EAX, ECX, EDI, and memory clobbers, while the object
keeps absolute relocations to the linker-owned BSS bounds. Entry-specific
stack tracking aligns the `kmain()` call after ESP changes. Frontend depth
rejects a label-wrapped reset, and a returning entry reaches a halt loop.
Compiler head and the Cupid-built compiler emit the complete unchanged
`kernel/core/kernel.cc` object. The checked normal recipe freezes its
63-header closure, and the production image boots that object. ADR 0180
records the ownership transfer.

ADR 0157 adds the four exact descriptor-table and segment-register forms
from the former `kernel/smp/percpu.c`. A packed six-byte `m` input supplies LGDT, the
data reload keeps the exact AX and memory clobbers, and a represented 16-bit
`r` input supplies GS. The shared x86 emitter uses a relative
call-and-RETF trampoline for CS, avoiding an absolute local-label
relocation. The production source is now `kernel/smp/percpu.cc`, and the
checked normal wrapper owns its deterministic object.

The guarded disk-image cutover built the complete root with
`OS_IMAGE=build/disk-template-cutover.img` in 672.0 seconds. The fresh
209,715,200-byte image has SHA-256
`8ad90a91103bf48d1e8d1e20b1b3dee48122ed1e4059b3f94cce7d750c262f16`.
A private four-vCPU `/bin/ls.cc` JIT boot passed in 61.9 seconds. The
719-input audit and its reproducibility check passed at that checkpoint with
449 transforms, 255 features, 25 accounted unreachable files, and source digest
`cfb0e1dcd276154a4db5c2747ed092581874a54cd4c9fb379f204e3c10f8253e`.
The later handoff checkpoint reused the image in 616.648 seconds and preserved
its FAT data. The final image has SHA-256
`d1bfab4aed1f2116768ceed3e301fb14ffe2a36418eb4d4ebdf1108097cb2b05`,
and its private four-vCPU JIT boot passed in 66.8 seconds.

## 2026-08-14 integrated ownership result

The active graph remains at 736 language inputs and 452 transforms. CupidDis
now participates in six production checks, and the ISR and context-switch
objects publish only after a checked private assembly and inspection
transaction. The source-suffix audit finds no active CupidC-owned `.c` root,
so this integration performs no rename. The promoted Linux and Windows seeds
now carry wide integer conversion and strict relocation ownership. The latter
is active in both guarded CupidASM object publishers; the former does not move
an additional source owner because no active source needs its new expression
shape.

## Milestone gates

| Milestone | Ownership gate | Current state |
| --- | --- | --- |
| Baseline | Clean, reproducible oracle build and recorded artifact/tool hashes on Windows and Linux | Complete at `1e079d1`: two clean 447-artifact builds match independently on Windows Clang/LLVM and Linux GCC/binutils, all host/CupidC/CupidASM checks pass, and the checked cross-host gate confirms the same logical cohort, source revision, quality fields, disk geometry, and tool capabilities. Host-specific aggregate hashes are `fc3e626f85780e4973b57a010528e4e3e59d72c63c54cc3701e61936555bc960` and `38bd2192e3d973b8c5b03d04ea69ed4397769913931ef0127c8fe8fee0536c0d` |
| Capability audit | Every active source and generated input mapped to required C, ASM, ABI, object, linker, and inspector features | Complete for root `all`, `user:all`, and `toolchain:all`: 736 active inputs, 255 feature IDs, 452 transforms, 25 accounted unreachable source-like files, and a checked drift and coverage gate. Root `all` has 443 transforms. The graph contains 31 assembly files, 296 headers, 409 Cupid C files, and no ordinary C translation unit. The checked graph uses canonical Windows conditionals and the C locale on every host. No supported transform invokes a host C compiler or recursive Make. |
| Assembly migration | All five production assembly sources produced by CupidASM with equivalent bytes/behavior | Complete: the normal graph has five CupidASM-owned and zero NASM-owned transforms; exact/semantic parity, clean poisoned-tool build, ISO lane parity, interrupt/scheduler contracts, UP JIT, and four-CPU boot/runtime gates pass |
| C migration | Every reachable kernel, tool, application, Doom, and vendored C cohort compiles and passes behavior gates with CupidC | In progress: all 239 checked-in normal roots plus the generated kernel-symbol translation compile through the checked seed. Three generated installation tables and three example programs add CupidC ownership outside that cohort. The 15 hosted Toolchain contracts also compile, link, and run through the checked i386 seed. Linux runs that seed directly; Windows uses WSL for these i386 contract programs and checked native PE tools for production. Only host-built native drivers and contract runners remain optional development oracles. All Doom production objects compile, and the no-WAD, explicit missing-IWAD recovery, shell-survival, and dual-NIC frontier checks pass. Full IWAD gameplay, input, audio, and save behavior remain to be checked. |
| Toolchain self-hosting | Checked seeds rebuild host tools; consecutive post-transition generations are byte-identical on Windows and Linux | The static i386 Linux seed rebuilds the 19-source union, fresh startup, and all five Linux tools from a private captured root. The PE execution seed and verified Linux plan rebuild 20 C objects, two assembly objects, and all five native Windows tools. Both bind the same 50-input snapshot, SHA-256 `e76d36ed4edc7679e91ac237135fe476dff6e69946bbffca56077afbf19a47f9`. Linux passed a clean 1,294.3-second candidate proof and a 1,473.9-second promoted-seed reproof with all initial images equal and 5/18/17 behavior. Windows passed a clean 1,253.4-second candidate proof and a 1,061.3-second reproof with all initial images equal and 5/5/6 behavior. Both matrices reject unmatched executable relocations. Linux contract programs on Windows still use WSL, and Python still coordinates both fixed points. ADRs 0278 and 0279 record the driver and convergence rule; ADR 0292 records the current promotion. |
| Normal-build cutover | Make invokes CupidC, CupidASM, CupidLD, CupidObj, and CupidDis without GCC/Clang/NASM/LLVM/binutils | In progress: root `all` has 443 transforms. Its 442 artifact transforms have a Cupid tool owner, with 243 CupidC, five CupidASM, 192 CupidObj, two CupidLD, and six CupidDis participations. The remaining Python-only size verifier emits no OS artifact and blocks `cupidos.img` publication on failure. Across all three supported graphs, CupidC participates in 246 transforms, CupidObj in 192, CupidASM in five, CupidLD in five, CupidDis in six, and Python in all 452. No transform invokes a host C compiler or recursive Make. Windows selects the native checked execution seed for output-bearing tools; Linux keeps the static seed. The two raw boot paths and both production assembly objects receive strict inspection before publication. Kernel AOT links with in-kernel CupidLD. Linux contract paths on Windows still need WSL, and Python retains coordination, safety, parity, drift, locking, and publication work. |

The normal cutover uses one checked invocation for root tools, checked
production CupidC, and checked user CupidLD. Each production wrapper keeps its
source and output transaction but delegates private seed execution and the
live five-image recheck to `run_seed_tool`. ADR 0246 records this
control-plane consolidation.

For the publisher-owned directory boundary, the user compiler opens every
POSIX or Windows component relative to a pinned parent and checks the resolved
output before releasing those pins. Directory preparation therefore stays
inside the approved repository path even if a public pathname changes during
the walk.

The ADR 0243 promotion carries CupidObj `profile-manifest` in the checked
five-tool seed. Host code generators were poisoned while all 19 C objects,
startup, and five tool images matched between stage two and stage three. Both
stages passed five help cases, fifteen successful operations, and thirteen
failures, including SHA-256 padding and repeated-block vectors, unsafe paths,
case collisions, and output preservation. ADR 0244 moves the normal Doom
manifest to CupidObj authorship through a guarded Python host transaction.
ADR 0242 records the source capability, ADR 0243 records seed carriage, and
ADR 0244 records the normal publication boundary.

The ADR 0241 production-handoff build completed in 502.232 seconds. Its
private 209,715,200-byte image has SHA-256
`3f8c84cea61e5e8bfc4e6a5fc09a030a4d6451d258a4ca2ea6486a923d1d08e3`.
The complete private four-vCPU e1000 frontier passed in 496.479 seconds and
reached the exact six-name ISO listing, `PASS feature17_iso`, and CupidC JIT
completion.
