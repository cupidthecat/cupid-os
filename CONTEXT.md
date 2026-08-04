# Cupid OS

Cupid OS is an operating-system project pursuing a self-hosting toolchain. This glossary fixes the project-specific language used to discuss the system and its bootstrap.

## Language

### System and source

**Cupid OS**:
The operating system produced and developed by this repository.
_Avoid_: CupidOS, cupid-os (except as a repository or package identifier)

**Active source**:
Cupid OS source that participates in a supported build or ships as part of the system.
_Avoid_: all checked-in source

**Supported build root**:
An accepted build entry point whose reachable inputs and outputs are part of Cupid OS delivery or verification. The normal root image build, separate user-program build, and hosted Cupid Toolchain contract build are current supported build roots; arbitrary Make targets are not automatically supported roots.
_Avoid_: every build target, only the default target

**Source cohort**:
A related group of active sources migrated and verified under one tool-ownership and behavior gate.
_Avoid_: directory (a cohort may cross directories), individual file count

**Toolchain job**:
An owned, bounded lifetime for deterministic Cupid Toolchain arena, buffer, logical-path, source, and diagnostic state.
_Avoid_: global compiler state, platform context

**Platform adapter**:
The narrow allocator, whole-file, and text-output capabilities that connect the shared Cupid Toolchain core to a hosted runtime or the Cupid OS kernel.
_Avoid_: tool backend, giant platform vtable

**External executable arena**:
The permanently reserved identity-mapped range `[0x01C00000, 0x01E00000)` leased exclusively to one ordinary fixed-address ELF process at a time.
_Avoid_: dynamically allocated user memory, CupidC region, CupidASM region

**TempleOS reference tree**:
The checked-in TempleOS source consulted for design understanding but excluded from Cupid OS source, builds, and progress measures.
_Avoid_: vendored Cupid OS source, Cupid OS source

### Languages and tools

**Cupid Toolchain**:
The first-party family of tools that builds and inspects Cupid OS artifacts.
_Avoid_: host toolchain

**Cupid C**:
The C-family language native to Cupid OS.
_Avoid_: CupidC when referring to the language

**CupidC**:
The compiler for C and Cupid C source.
_Avoid_: Cupid C when referring to the compiler

**Linear IR**:
The typed, target-neutral instruction sequence between CupidC's function-body AST and machine-code emission. Its stack entries distinguish object addresses from scalar and structure values. Branch targets stay relative to one function, while parameters, represented automatic objects, compound-literal objects, runtime string literals, block-static objects, linked file objects, and linked function references retain their absolute frontend identities. Machine addresses, section offsets, symbol-table indices, frame offsets, literal symbols, and value snapshot storage remain private to target emission.
_Avoid_: AST, x86 bytecode, machine code

**Wide integer value**:
An eight-byte integer carried through hosted Linear IR as one logical value. The i386 emitter stores its bytes in a private frame snapshot and returns the low word in EAX and the high word in EDX. The current boundary covers constants, matching conditional arms, fixed call results, object access through file, block-static, automatic, pointer, member, and index paths, initialization, plain assignment, all ten compound assignments, prefix and postfix update, declared parameters, named direct or indirect call arguments, signed or unsigned ellipsis and unprototyped call arguments, discard, return, addition, subtraction, multiplication, division, remainder, unary plus, unary minus, bitwise complement, left and signed or unsigned right shifts, AND, OR, XOR, all six comparisons, logical not, short-circuit logical operators, conditional selection, structured scalar conditions, signed or unsigned switch dispatch, conversion to or from represented integer widths, explicit non-atomic `double` to `unsigned long long` conversion, and non-atomic `va_arg` reads. It also covers the standard `signed long long` to `unsigned long long` usual arithmetic conversion and, in GNU mode, promotion of a wide enum to its compatible wide integer type. A switch duplicates the value's snapshot handle and compares the complete eight-byte case value without evaluating the condition again. Mutation evaluates its lvalue address once and returns either the stored snapshot or the reconstructed postfix value. Multiplication combines the low-word product with both cross-word products. Division and remainder use a fixed restoring loop over unsigned magnitudes, then apply the quotient or dividend sign. Each multiplication, division, remainder, or wide variadic-read result receives a fresh snapshot. Shift counts remain represented four-byte integers. Runtime cases that C leaves undefined promise neither a trap nor a result.
_Avoid_: two unrelated 32-bit values, a public IR register pair

**Floating scalar value**:
A non-atomic `float` or `double` carried through hosted Linear IR as one logical value. A `float` keeps its raw four-byte representation. A `double` uses an emitter-owned eight-byte snapshot. Object loads, initialization, plain assignment, discard, fixed arguments and parameters, direct or indirect call results, returns, `double` ellipsis arguments, and non-atomic `va_arg(double)` reads use this path. A static-duration scalar evaluator uses integer-only IEEE binary32 and binary64 arithmetic for unary signs, addition, subtraction, multiplication, division, comparisons, casts, scalar truth, short-circuit logic, and conditional selection. It rounds each result to nearest with ties to even at the C expression width, covers represented signed and unsigned integers through 64 bits, preserves signed zero, and places the final target bits in `.rodata`, `.data`, or `.bss` through the ordinary static object policy. Explicit casts and assignment conversion work in both directions between the two floating widths. Unary plus and minus and binary addition, subtraction, multiplication, and division work for same-width or mixed-width runtime operands. All six equality and relational operators accept matching or mixed widths and produce a normalized signed `int`. Their `UCOMISS` or `UCOMISD` emission treats every unordered relation as false except `!=`. Matching floating conditional arms keep their width; mixed arithmetic and conditional arms use `double`. The four arithmetic compound assignments compute at the common width, convert back to the left width, and evaluate the lvalue once. Every changed x87 result is stored at its C width before the next IR instruction. Default argument promotion converts an ellipsis or unprototyped source `float` to `double` through x87 and a fresh snapshot. i386 calls place the final value in four or eight cdecl stack bytes. Floating results cross the ABI in x87 `ST0`; after call cleanup, the caller places a `float` in a four-byte semantic stack slot or a `double` in a private eight-byte frame snapshot. Runtime truth testing accepts non-atomic `float` and `double` for unary `!`, `&&`, `||`, the controlling operand of `?:`, and the conditions of `if`, `while`, `do`, and `for`. `UCOMISS` or `UCOMISD` compares the value with positive zero and publishes a normalized 0 or 1. Both signed zeros are false; finite nonzero values, subnormals, infinities, and NaNs are true. Explicit casts and assignment conversion to `_Bool` use the same rule. Decoder-driven oracles check comparison behavior and call alignment; the arithmetic oracle models its supported x87 subset rather than executing native x87.
_Avoid_: general floating-point support, an exposed snapshot pointer

**Automatic long-double value**:
A non-atomic `long double` carried through hosted Linear IR in its twelve-byte i386 object representation. Automatic objects use frame snapshots. File-scope and block-static scalars, fixed arrays, and complete records may contain long-double leaves. Implicit initialization zeros the complete object, while an explicit long-double leaf accepts an integer constant expression equal to zero. Each leaf occupies twelve zero-filled BSS bytes, and recursive validation rejects atomic leaves without following pointers. The emitter uses x87 80-bit memory loads and stores for object access, assignment, floating-width conversion, unary plus and minus, addition, subtraction, multiplication, and division. All six comparisons accept two non-atomic long-double values or a long-double value paired with `float` or `double`. Mixed inputs convert to `long double`. The emitter loads right then left, uses `FUCOMIP ST0, ST1`, discards the surviving x87 value, and normalizes a signed `int` result. Signed zeros compare equal, and an unordered input makes only `!=` true. Direct and indirect fixed, ellipsis, and unprototyped arguments use three cdecl words. Functions return a `long double` in x87 `ST0`, and direct or indirect callers store it in a twelve-byte snapshot. `va_arg(long double)` copies twelve bytes, advances the cursor by twelve, and leaves the next four-byte argument at the correct address. Truth testing and `_Bool` conversion compare the value with x87 zero through a balanced `FLDZ`, `FUCOMIP`, and `FSTP` sequence. Both signed zeros are false; finite nonzero values, subnormals, infinities, and NaNs are true. Long-double literals, nonzero and floating static initializers, and integer conversions involving `long double` other than conversion to `_Bool` remain outside this boundary.
_Avoid_: host `long double` layout, general long-double support

**Represented double-to-unsigned-wide conversion**:
An explicit non-atomic cast from `double` to `unsigned long long`. Linear IR keeps one typed conversion, and the i386 emitter decomposes the truncated result into exact high and low 32-bit words before publishing a wide-value snapshot. For finite binary64 input, the defined interval is `(-1, 2^64)`. Values for which C leaves the conversion undefined have no promised result.
_Avoid_: implicit assignment, float input, signed wide or enum output, host floating conversion

**Private compiler control frame**:
A tagged loop or switch entry used by the in-kernel CupidC emitter. `break` selects the nearest control frame. `continue` selects the nearest loop and removes the saved selector for each switch crossed on the way. The parser accepts 128 active control frames and fails before entering a 129th.
_Avoid_: loop-only depth stack, silent capacity exhaustion

**Private compiler statement depth**:
The number of active recursive statement-parser calls in the in-kernel CupidC compiler. The parser accepts 1,024 active calls and rejects the next call before it recurses. A failed REPL evaluation restores this count with the other committed parser state.
_Avoid_: relying on the terminal task's native stack as a language limit

**Private CupidC scalar cdecl slot**:
A four-byte or eight-byte argument and parameter slot used by the in-kernel
JIT and AOT compiler. Integers, pointers, function pointers, `float`, and the
implicit method `self` occupy four bytes. A `double` occupies eight bytes with
its low word at the lower address. Calls evaluate arguments from left to right,
then permute complete words into source order before the call. Callees advance
later parameter offsets by the same slot widths, and callers reclaim the full
outgoing area. A direct function or method call with parsed fixed parameter
types converts `int` or `char` to either floating width, converts between the
two floating widths, and truncates a floating value for an `int` or `char`
slot. Represented pointer categories and integer null forms can fill a pointer
slot. A parsed variadic tail widens `float` to `double` and promotes `char` to
`int`. Function-pointer calls, kernel bindings, and calls without fixed
parameter metadata keep their source width.
_Avoid_: reversing source evaluation, four bytes for every parameter, splitting a double into unrelated arguments

**Represented bit-field assignment**:
A plain Cupid C assignment to a non-atomic integer bit field whose declared storage unit is four bytes and fits inside its record. Linear IR retains the graph member, while i386 emission preserves neighboring bits and returns the value represented by the stored field.
_Avoid_: bit-field address, ordinary member store

**Represented bit-field mutation**:
A compound assignment or prefix or postfix update to a represented bit field. Linear IR evaluates the record address once, applies target-width promotion, and keeps the extracted old value when postfix semantics require it. Partial fields are nonvolatile because their current store path needs a second complete-unit read. A volatile 32-bit field uses one read and one direct store.
_Avoid_: bit-field address, reconstructing a postfix value after truncation

**Represented ordinary bit-field promotion**:
The integer promotion of a narrow `unsigned int` bit-field read in an ordinary expression. On Cupid i386, every value of a field narrower than 32 bits fits signed `int`. The frozen expression and Linear IR keep the direct graph-member reference so this field-specific same-rank conversion cannot be mistaken for a general unsigned-to-signed promotion.
_Avoid_: generic same-rank signedness conversion, bit-field mutation

**Block-static object**:
A block-scope object with static storage duration. It keeps its absolute frontend block-binding identity, receives a local ELF object symbol, and never consumes an automatic frame slot.
_Avoid_: automatic local, file-scope object

**Block extern alias**:
A lexical block declaration that names a canonical linked object. It owns no storage. Uses retain the canonical file-binding identity even when no ordinary file-scope name is visible.
_Avoid_: automatic local, block-static object

**Block function alias**:
A lexical block declaration that names a canonical linked function. It owns no storage or runtime work. Uses keep the type visible at the declaration and retain the canonical function identity even when no ordinary file-scope name is visible.
_Avoid_: nested function, automatic local

**Implicit function alias**:
A block function alias introduced only by the explicit Doom compatibility profile when a bare undeclared identifier is called. It owns the identifier expression that triggered the old-style `extern int name()` declaration and keeps one canonical external function identity across blocks. Calls parsed before a later prototype retain default argument promotions.
_Avoid_: general undeclared identifier support, forward declaration

**Doom compatibility pointer conversion**:
A conversion enabled only by the explicit Doom compatibility profile between one unqualified i386 function pointer and one unqualified i386 data or `void` pointer. CupidC marks the conversion in assignments, automatic initializers, fixed call arguments, returns, and explicit casts. Linear IR verifies both four-byte representations, and i386 emission keeps the bits unchanged.
_Avoid_: general function-pointer compatibility, GNU pointer conversion

**Pointer-preserving static address cast**:
An explicit cast between two non-atomic pointer types around a static string or linked binding address. The initializer keeps the original string bytes, binding identity, and target-byte addend. A cast through an integer is not pointer-preserving and remains outside this term.
_Avoid_: runtime pointer cast, function/data compatibility conversion, integer detour

**External inline definition**:
A file-scope function definition whose external declaration set contains `inline` but does not consist entirely of `inline` declarations without `extern`. A prior or later ordinary declaration and an `extern inline` definition with effective external linkage both provide the external definition. An earlier `static` declaration keeps the function internal, even when its definition is spelled `extern inline`. CupidC records the translation-unit result on the canonical binding while the definition keeps its exact source spelling. Any external-linkage function declared `inline` must have a definition in the same translation unit.
_Avoid_: pure external inline definition, static inline function

**Block typedef**:
A type alias whose name lives in one C block scope. It keeps a stable frontend type identity, shares the ordinary identifier namespace, and owns no runtime storage.
_Avoid_: file typedef, block object

**Private fixed-array typedef**:
A private JIT, AOT, or persistent REPL alias that retains one positive element count as part of its type shape. Automatic, global, block-static, record-field, and class-field objects allocate the complete checked size. A record or class array member retains its complete object size and record-element identity through direct or pointer access, including indexed assignment inside another record array. Function and method parameters decay to an element pointer, while `sizeof` keeps the complete type or object size.
_Avoid_: scalar alias, multidimensional array typedef, pointer to array

**Private unsigned word**:
A four-byte unsigned value retained by the in-kernel compiler through objects, pointers, calls, kernel binding results, enum symbols, unary and conditional expressions, `sizeof`, usual arithmetic conversion, scalar returns, and conversion to `float` or `double`. Relations, division, remainder, and right shift use unsigned i386 behavior. The Browser array-length lane uses this type through the ECMAScript maximum length.
_Avoid_: signed int bits, wide integer value, floating-to-unsigned conversion

**Block enumerator**:
An enum constant whose ordinary identifier lives in one C block scope. Its frontend binding keeps the evaluated target value and type but owns no storage, address, symbol, relocation, or runtime declaration work.
_Avoid_: local constant object, file enumerator

**Block enumerator activation**:
The lexical source point where a block enumerator becomes available to ordinary-name lookup. A declaration or function prefix can own the point directly. A type-name definition uses an expression or initializer owner so IR can validate source order independently of runtime control flow.
_Avoid_: runtime evaluation point, local-object lifetime

**Block-scope record tag**:
A `struct` or `union` name whose identity lives in one C block scope. A declaration may leave the type incomplete, a later definition in the same scope may complete it, and a nested tag may hide it until that nested block ends. A tag declared in a function definition's parameter list shares the outer body scope and expires when the definition ends.
_Avoid_: file tag, block object

**Union initializer selection**:
The single direct member chosen by a represented C union initializer list. A positional clause selects the first eligible named member, while a direct member designator selects that member. The initializer forest keeps one edge for the choice. Runtime lowering zeros the complete union before storing the selected member, and static emission writes the member over zero-filled target storage.
_Avoid_: structure member sequence, multiple active members

**Compound-literal object**:
An unnamed object created by a C compound literal. At block scope, one absolute expression identity names the source site's persistent automatic frame slot. Its initializer runs whenever execution reaches the expression, and the expression is an lvalue naming that object. Aggregate list initialization uses a separate emitter-private staging slot, then replaces the persistent object after every initializer read has finished. A narrow string initializer copies immutable literal bytes directly after zeroing the persistent array.
_Avoid_: temporary structure value, hidden block binding

**Runtime string literal**:
The immutable narrow bytes retained by a string expression evaluated inside a function. Linear IR keeps the expression identity, while the i386 emitter owns its local `.rodata` symbol and relocation. An automatic character array initialized from those bytes is a separate destination object.
_Avoid_: host string pointer, automatic string array

**Structure value**:
A complete Cupid C `struct` carried by value through Linear IR. One abstract stack entry represents an emitter-owned snapshot of the target bytes, not an address that aliases the source object.
_Avoid_: aggregate scalar, borrowed object address

**Hosted source frontier**:
The unchanged implementation and contract files that hosted CupidC can preprocess, parse, lower, and emit as deterministic i386 ELF32 objects. The current target-profile gate contains 33 strict C11 roots and two GNU-enabled runtime roots. `HOSTED_I386_LINUX` owns 31 ordinary requests, while `HOSTED_I386_KERNEL_BRIDGE` owns the two requests that may include `/kernel/lang`. The GNU profile owns the runtime implementation and its behavior probe. Together they cover the complete 19-source static tool union, `kernel/lang/as_elf.cc`, and all fourteen Toolchain contracts. The retired `HOSTED_TOOLCHAIN_64` and `HOSTED_KERNEL_BRIDGE_64` names have no active roots. Non-atomic `long double` uses twelve-byte objects for arithmetic, floating-width conversion, all six matching or mixed floating comparisons, fixed and unprototyped arguments, variadic calls and reads, function returns, and direct or indirect call results. Static-duration scalars and long-double leaves inside fixed arrays or complete records may use implicit or integer-constant zero initialization. Long-double literals, nonzero and floating static initializers, and integer conversions involving `long double` remain open. The checked cohort requires byte identity for sixteen newly compiled objects and fifteen linked executables. Complete CupidC-emitted closures for CupidC, CupidASM, CupidDis, CupidLD, and CupidObj link with CupidASM startup and the hosted i386 Linux runtime, then run real behavior checks on Linux or through WSL. Its initial, frozen, and newly discovered contract inventories must match exactly. Publication accepts only a dedicated verified `cupidc-contracts` destination, and every contract run verifies the named artifact, complete cohort, current 45-input contract set, checked seed manifest, and 41-file fixed-point source inventory before execution. The 45 inputs include the Toolchain Makefile and both Python modules that construct or verify the cohort, so restored timestamps cannot hide control-plane drift. Manifest hashing, parsing, and validation use one captured byte sequence so a concurrent replacement cannot mix provenance from different reads. Link plans also reject an unknown object key before the first compiler process starts.
_Avoid_: self-hosted toolchain, completed source cohort

**Production crypto cohort**:
All 20 `kernel/crypto` translation units built by checked-seed CupidC in the normal image build. Each deterministic i386 ELF32 object passes the shared validator before publication. The boot gate runs 62 crypto, ASN.1, and X.509 checks, walks the incomplete external CA-array path, and initializes the CSPRNG through its represented CPU assembly.
_Avoid_: compiler-head frontier, partial crypto cohort

**Production CupidC kernel cohort**:
The 155 checked-in normal-build translation units owned by checked-seed CupidC, plus the generated kernel symbol-table translation unit. All 156 sources use `.cc`. The five shared Toolchain roots are also part of the 19-source i386 Linux fixed-point plan; their native GCC and Clang rules select C explicitly with `-x c`. The symbol build freezes the pass-one kernel and five-tool seed, runs checked CupidDis to capture canonical symbol text, and runs checked CupidObj to generate the packed source. Python independently renders the expected bytes, rejects malformed text, missing symbols, output mismatch, or live input drift, and publishes only a regular complete file. The checked compiler wrapper freezes that source and its two-header closure before it validates and publishes the data-only object. It also freezes the exact source-driven closures for the kernel entry, SIMD services, the core string implementation, Nuked OPL3, JPEG decoding, glyph rasterization, libm, FPU state, per-CPU setup, and SMP bring-up. ADR 0124 records the first 111-root naming transfer, ADR 0126 completes the fixed-point naming boundary, ADR 0129 transfers the in-kernel CupidC lexer, ADR 0135 transfers Nuked OPL3, ADR 0139 transfers JPEG and glyph rasterization, ADR 0167 transfers the FPU and SMP roots, ADR 0176 transfers libm, ADR 0180 transfers the kernel entry and SIMD roots, ADR 0181 transfers the final strict host root, and ADR 0224 transfers kernel-symbol source generation to CupidObj. The strict frontier compiles all 155 checked-in sources twice before atomic publication, and poisoned-host rebuilds and exact recursive header closures cover every recipe. Its input inventory skips hidden paths under the active include roots, which keeps private compiler staging headers out of the repository snapshot during concurrent builds. Its final directory promotion retries only short permission-style locks; a persistent lock or any other filesystem error fails without publishing a partial frontier. A data-only relocatable object is valid without `.text` when its sections and symbols pass the remaining ELF checks. The full frontier covers a 445-file snapshot with SHA-256 `99d03de14f544f6a76d21ed147e62018873f1e2e8dfa2f4459830b69314432c2`; both 155-object passes are byte-identical; each totals 3,749,796 bytes. The combined graph includes the ISO runtime fixture as an image input. Strong four-vCPU checks cover both supported NICs, all three FPU milestones, the promoted SMP, libm, and string paths, RDRAND, all 62 crypto checks, USB storage, desktop and terminal startup, audio output, TrueType glyph use, an exact baseline JPEG decode, and in-OS CupidC execution. The strict checked-in kernel and driver cohort has no host-compiled root.
_Avoid_: all kernel C, compiler-head frontier, checked seed alone

**Production Doom cohort**:
The 83 `.cc` Doom and Cupid platform translation units built by checked-seed CupidC in the normal image. Three sources use the exact `DOOM_COMPAT_I386` profile; the sound adapter and 79 Doom-tree sources use `DOOM_TREE_I386`. The checked wrapper freezes the selected source and all 290 `.h` and `.inc` inputs visible through the profiles' 20 include roots. It recursively scans visible `.c` and `.cc` files beneath `kernel/doom` before and after compilation. An always-checked manifest fixes both source memberships and every header hash without changing its timestamp when the content is unchanged. A legacy `.c` file, an unlisted `.cc` file, a missing root, added or removed headers, byte drift, symbolic links, and NTFS junctions fail before object publication. The active dglibc source is 67,155 bytes and produces a 93,332-byte object with SHA-256 `e2496b01c93a7858a0c035b53aea0ad834d95d2be3f7ae49574d1759ebec34d6`. The 69,366-byte closed profile manifest has SHA-256 `e77c8a0dc238b1a6f2257f273cf3367dba930c914e6a5806adf058621bbff4a4`. Asset-free runtime checks cover active nonlocal exit, repeated quit and error cleanup, production config helpers with test-only files, native rename and copy boundaries, block-cache failure handling, RamFS limits, FAT collision, read, handle, busy-replacement, and 8.3 behavior, HomeFS ownership, depth, and batched publication, no-WAD recovery, shell survival, and the full stateful four-CPU frontier on e1000 and RTL8139. Gameplay remains a separate IWAD-backed boundary. ADR 0184 records ownership, ADR 0211 the storage bridge, and ADR 0214 the shell-session lifecycle.
_Avoid_: compiler-head Doom frontier, claiming WAD runtime behavior from asset-free tests, host-built Doom cohort

**Production generated-install cohort**:
The ramfs program table, homefs document table, and CupidASM demo table. Checked-seed CupidObj generates all three `.cc` sources from Make's ordinal inventories, and checked-seed CupidC compiles them under the fixed kernel profile. Each generation recipe depends on the checked runner, seed manifest, and all five seed images. The compiler wrapper freezes the generated source and complete header union, validates each relocatable object, and publishes it atomically. `tools/hostbuild.py` remains the parity oracle but no longer owns these production sources. ADR 0204 records the generation transfer.
_Avoid_: every generated C file, Python-free generation, kernel source cohort

**Production checked-seed tool cohort**:
The root image transforms owned by CupidASM, CupidObj, CupidLD, and CupidDis. The normal graph verifies and freezes the manifest-bound five-tool seed before each command, then checks the live trust unit again after the command. It contains five CupidASM transforms, 186 CupidObj transforms, two CupidLD links, and one CupidDis inspection. The fifth CupidASM transform assembles the ISO spanning fixture from checked-in CupidASM source. Python verifies its exact byte lane and controls publication. The CupidObj total includes the three installation-source generators and the kernel-symbol source generator. Python supplies orchestration and parity checks, and Windows uses WSL, but no native hosted Cupid executable is reachable from root `all`. Make applies `$(sort ...)` to every wildcard-discovered output list, so generators and links receive the same order under Windows and Linux host locales. The repository stores its runtime JPEG as a sequential SOF0 frame. Hostbuild validates and copies exact SOF0 or SOF1 bytes, rejects progressive, unsupported, or malformed frames, and gives the private snapshot to checked CupidObj. FFmpeg, `jpegtran`, `djpeg`, and `cjpeg` do not participate. The first cross-host comparison matched 426 of 430 kernel artifacts and traced all four remaining differences to the old host JPEG conversion. After the replacement, a 607.7-second Linux kernel build and a 341.6-second Windows root build produced all 430 frozen kernel artifacts byte for byte. A fresh normal image then passed a private `/bin/ls.cc` JIT boot in 49.8 seconds. Native commands remain explicit development and oracle targets. ADR 0190 records the root tool handoff, ADR 0204 records installation-source ownership, ADR 0224 records kernel-symbol source ownership, and ADR 0227 records the fixture transfer.
_Avoid_: native fixed point, Python-free build, hosted Toolchain contract cohort

**Repository ISO fixture author**:
Checked CupidASM authors the 4,096-byte spanning lane from `test_iso/big_pattern.asm`; hostbuild checks the exact candidate and publishes it atomically. The deterministic ECMA-119 and `RRIP_1991A` writer in `tools/hostbuild.py` authors the complete image. `test_iso/fixtures.manifest` fixes the repository fixture membership before the writer freezes the regular-file tree. Make declares the same seven portable paths explicitly, and a checked test prevents that safe prerequisite list from drifting away from the manifest. The writer emits a primary volume descriptor, both path-table byte orders, identifier-sorted block-bounded directories, a forward SUSP continuation, fixed UTC metadata, and contiguous file extents, then rechecks the manifest and tree before atomic publication. Rock Ridge `NM` records retain guest names drawn from the portable letter, digit, dot, underscore, and dash alphabet, capped at 127 bytes. `PX` and `TF` records carry fixed read-only metadata for other readers; Cupid OS ignores them. Undeclared or missing paths, more than eight directory levels, symbolic links, NTFS junctions and other reparse points, hard-linked outputs, special files, case-only name collisions, unsafe output paths, and live manifest, input, or output drift fail without replacing an existing image. The tracked 61,440-byte fixture rebuilds without `mkisofs`, `genisoimage`, or `xorrisofs`. ADR 0191 records the image boundary, and ADR 0227 records the lane fixture boundary.
_Avoid_: general optical-disc mastering, bootable ISO, Joliet author, guest ISO reader

**Production external-program cohort**:
The `hello.cc`, `ls.cc`, and `cat.cc` examples compiled by CupidC and linked by CupidLD. Linux runs the checked i386 Linux seed directly, while Windows runs it through WSL. The normal build does not prepare native hosted tools. An explicit Windows oracle runs private CupidC and CupidLD snapshots and requires all six outputs to match the checked seed byte for byte. Those optional drivers still depend on a host C compiler and Windows linker, so this is not a native Windows fixed point. Before compilation, the user build captures the exact bytes of the six kernel and public declarations that define the shared i386 syscall contract, compares the reviewed layout, and rechecks every input before success. The reviewed contract is version 5 with 103 fields in 412 bytes, a 136-byte directory entry, an 8-byte file status record, and 101 pinned function providers. The build fixes the freestanding user profile, `_start`, and the `[0x01C00000, 0x01E00000)` arena, then validates the same ELF program-header rules enforced by the kernel loader. `user/build/` contains ignored local outputs. The guest gate boots each program from a separate private copy of the same staged image, binds syscall evidence to the loaded PID, checks output by byte count and FNV-1a fingerprint, copies the hostile cat fixture over the normal `/home/readme.txt` path in the cat copy, and requires the same PID to exit cleanly. ADR 0127 records the ABI gate and corrected VFS record layout. ADR 0130 records the optional native Windows driver path. ADR 0133 records the ABI snapshot and private guest checks. ADR 0187 records the current arena and its coordinated memory-map move. ADR 0188 records the checked-seed default on Windows.
_Avoid_: every external program, hosted GCC examples, user-mode isolation

**Hosted i386 ABI profile**:
The deterministic hosted C request used to compile an i386 Linux tool closure. It searches `/toolchain` for quoted and angle includes and the checked i386 Linux declaration set for angle includes only, defines `__SIZEOF_POINTER__` as four, and leaves `_WIN32` undefined. Only `kernel/lang/as_elf.cc` and `toolchain/tests/cupidasm_kernel_elf_contract.cc` also search `/kernel/lang`; the other 31 strict roots cannot see that directory. The CupidC command represents those roots with `-I` and `--include-angle` in caller order. Repeatable `-include` options represent preprocessing inputs that run in order before the primary source. Tool sources use strict C11. The hosted runtime implementation and behavior probe enable CupidC's GNU variadic built-ins.
_Avoid_: `HOSTED_TOOLCHAIN_64`, vendored libc, host system headers

**Hosted i386 Linux runtime**:
The repository-owned startup and narrow C service layer for static Cupid-built i386 Linux commands. CupidASM supplies process entry and `int 0x80` system-call wrappers. CupidC supplies allocation, unbuffered files, standard streams, fixed-width integer declarations, memory and string functions, `errno`, `getcwd`, formatted diagnostics, and the checked `printf`, `puts`, `snprintf`, `fputc`, and `fputs` surface. Integer formatting covers `int`, `long`, and `long long` decimal and hexadecimal values, including zero-padded widths. String formatting accepts a fixed or argument-supplied precision. A CupidC-built `.cc` runtime contract checks the heap, files, errors, arguments, formatting, memory, and string behavior under Linux or WSL. It is a separate behavior probe and does not enter the 19-source fixed-point plan.
_Avoid_: general libc, Windows runtime, test-only import providers

**Hosted Toolchain contract cohort**:
The fourteen `.cc` Toolchain contract programs and the separate hosted runtime contract built as static i386 Linux executables by stage-two and stage-three CupidC. The checked cohort snapshots its exact source and declaration membership, reproduces that inventory under a private root, compares each executable across compiler stages, runs the runtime behavior probe, and publishes those fifteen contracts with five refreshed tools and a manifest. Live inventory discovery catches additions, removals, and restored edits that changed a private copy. The output must be a dedicated `cupidc-contracts` directory inside the source tree; an existing destination must already verify, and arbitrary directories, source trees, files, or symbolic links remain untouched. A run derives the cohort from its requested executable and verifies the named artifact, all recorded hashes, and current inputs first. `toolchain:all` owns this path. Native GCC or Clang builds are optional oracles under `native-oracles`, not normal build inputs.
_Avoid_: checked seed, native contract suite, 19-source tool fixed point

**CupidC compiler generation**:
A compiler process compiling unchanged source from its complete implementation. Generation zero is the native host-built CupidC driver, and generation one is the first static Cupid-built driver. Generation one builds all eleven C objects and links stage two. Stage two repeats the same work for stage three. The static i386 Linux fixed-point gate requires every stage-two and stage-three C object, both startup objects, and all three linked compiler images to match byte for byte.
_Avoid_: checked seed, complete self-hosting

**Static i386 Toolchain fixed point**:
A stage boundary where one generation of CupidC, CupidASM, and CupidLD builds complete stage-two images for CupidC, CupidASM, CupidDis, CupidLD, and CupidObj, then the stage-two producer trio repeats that build for stage three. The gate compares all 19 C objects, the independently assembled startup objects, and all five linked images across the two stages. Each stage also executes the five tools through real success and failure cases. The same fixed-point relationship is checked from the repository seed on Linux or through WSL. It is not a native Windows proof or normal-build ownership transfer.
_Avoid_: fresh-checkout bootstrap, native Windows fixed point, production cutover

**Host adapter link tracer**:
A static i386 executable used to check the boundary between CupidC objects, CupidASM startup code, and CupidLD. Its `_start` calls `ctool_host_adapter_init`, checks the resulting `{data, size}` fields, and exits through Linux `int 0x80`. Link-only providers resolve the adapter's file and allocation imports but do not implement a usable runtime.
_Avoid_: hosted Cupid tool, C runtime, bootstrap stage

**Immediate pointer qualification conversion**:
A representation-preserving C conversion that adds qualifiers to the object directly referenced by a pointer. CupidC accepts `char **` as `char *const *` because the intermediate pointer becomes read-only through the destination. It still rejects qualifier removal and unsafe deeper changes such as `char **` to `const char **`.
_Avoid_: pointer cast, ignoring nested qualifiers

**Typed null conversion**:
A representation-preserving conversion from a proved integer zero constant, directly or through an explicit cast to unqualified `void *`, to the destination object-pointer type. The frontend marks that proof on the conversion node. Linear IR rejects missing, misplaced, and runtime-pointer provenance before i386 emission keeps the same four-byte zero representation.
_Avoid_: general void pointer conversion, pointer reinterpretation

**Addressable unspecified-bound array**:
An incomplete external array whose element type is complete and has a nonzero target size. CupidC may take the linked object's address and apply ordinary array-to-pointer decay, but it does not treat the array itself as a complete value or storage object.
_Avoid_: fixed array, flexible array member, variable-length array

**Represented GNU assembly statement**:
A GNU-mode CupidC statement whose immutable frontend record owns a decoded template and a packed operand slice. Extended statements accept one to four typed integer register outputs, single-digit matching inputs, and the CSPRNG's RDTSC, CPUID, RDRAND, SETC, and tied NOP forms. The port-I/O subset adds width-preserving `a` and `d` inputs, 8-bit, 16-bit, or 32-bit `=a` outputs, and modifiable `+c`, `+S`, and `+D` operands. The checked seed retains the exact GNU `Nd` port constraint and selects its valid DX alternative for the active `inb` and `outb` templates. It accepts one `memory` clobber where the source requests it. One `=r` output may instead be a modifiable four-byte object or `void` pointer for the exact `mov %%gs:0, %0` per-CPU load. A separate output-only subset snapshots a 32-bit general register, ESP, EBP, `4(%ebp)`, or EFLAGS into one four-byte object; the EFLAGS form may finish with `cli`. Exact volatile `FNSTSW`, `FNSTCW`, and `STMXCSR` forms write one correctly sized integer lvalue through `=m`. The checked seed accepts an independent four-byte integer or data-pointer `r` input and an independent four-byte integer `c` input for exact CR0, CR2, CR3, and CR4 moves and RDMSR. The exact volatile `fxsave (%0)` form narrows its `r` input to a four-byte object or `void` pointer and retains one `memory` clobber. CupidC also captures the next instruction's address through the exact volatile `call 1f\n1: popl %0` form and one four-byte integer `=r` output. That form emits a zero-displacement call and immediate pop without a relocation. Function-body basic statements and extended statements with an empty output list own no operands and are implicitly volatile. An exact empty volatile extended template with one `memory` clobber and no operands remains an IR ordering point and emits no target bytes. Linear IR evaluates output addresses before input values, once each and in source order. The emitter preserves EBX and restores ESI or EDI after repeated string I/O. The checked seed also accepts the unchanged CPUID `a` input sharing the compatible `=a` output. Normal-build ownership remains a separate boundary.
The checked seed also accepts the exact volatile `ldmxcsr %0` form with one addressable, non-atomic 32-bit integer `m` input. Linear IR retains the operand address, and the shared x86 model emits `0F AE 10` through EAX.
The checked seed accepts the exact volatile MOVSS float-memory round trip used by `fpu_boot_smoke()`, plus its one-way load and store forms. Each form requires the `xmm0` clobber. Linear IR evaluates each object address once, and the shared x86 model emits `F3 0F 10 00` or `F3 0F 11 00` through EAX. It also accepts the exact volatile `fldl`, `fsin`, and `fstpl` block in `stress_sin()`. The normal build compiles `kernel/cpu/fpu.cc` through the frozen checked-seed wrapper. A typed production-object policy rejects helper calls and floating work before the CR4 write, requires `FNINIT` before a 32-bit memory `LDMXCSR`, and rejects any other floating work in `fpu_init_cpu()`. The four-vCPU runtime gate requires `[fpu] SSE2 enabled`, `[fpu] boot smoke ok`, and `FPU boot smoke passed`.
_Avoid_: general GNU assembly support, host-assembler escape

**Named GNU assembly operand**:
An optional C-identifier label in brackets before an extended-assembly output or input constraint. Checked-seed CupidC collects the complete output-then-input namespace before parsing ordinary operands, requires every label to be unique, and permits a declared label to remain unused. It rewrites each unescaped `%[identifier]` template reference to the corresponding numeric operand index. A `%%` pair remains literal and does not begin a named reference. Operand names stay parser-private: the frontend, Linear IR, and emitter continue to receive the existing canonical numeric template and packed operand slice. The normal numeric path therefore applies the same lvalue, addressability, atomic, bit-field, width, type, constraint, fixed-register, and template checks after normalization. Malformed, duplicate, and unknown names fail transactionally. Named matching constraints remain outside this term.
_Avoid_: public operand-name metadata, rewriting active source to numeric operands, treating `%%[name]` as substitution, general template substitution

**Represented GNU x87 double-power memory assembly**:
The exact volatile statement in `libm_pow_impl()` that consumes one modifiable `double` `=m` output, four addressable `double` `m` inputs, and one `memory` clobber. Checked-seed CupidC resolves its named operands to numeric indexes, evaluates all five addresses once in source order, and emits the complete `FYL2X`, `FRNDINT`, `F2XM1`, and `FSCALE` sequence with balanced x87 depth. The active source uses the corrected `DC E9` forward subtraction for `x - round(x)`. The checked seed retains the legacy `DC E1` reverse-subtraction form for source compatibility. ADRs 0207 through 0209 record the capability, seed carriage, and source correction.
_Avoid_: treating the mixed-width `libm_powf_impl()` form as all-double operands, arbitrary x87 stack programs, host-assembler escape

**Represented GNU x87 mixed-width float-power memory assembly**:
The exact volatile statement in `libm_powf_impl()` that consumes one modifiable `float` `=m` output, two addressable `float` `m` inputs, two addressable `double` `m` inputs, and one `memory` clobber. Checked-seed CupidC resolves its named operands to numeric indexes and evaluates all five addresses once in source order. The emitter shares the power sequence while selecting 32-bit loads and store for the float values and 64-bit loads for the constants.
_Avoid_: treating every operand as `double`, arbitrary x87 stack programs, host-assembler escape

**Represented GNU SSE2 square-root register assembly**:
The exact volatile `sqrtsd %1, %0` statement in `libm_sqrt_impl()` with one modifiable, non-atomic `double` `=x` output, one non-atomic `double` `x` input, and no clobbers. Linear IR evaluates the output address before the input value. The i386 emitter loads the value into XMM0, applies `SQRTSD XMM0, XMM0`, and stores through the saved output address. The focused function has 65 text bytes and no relocations.
_Avoid_: general `x` constraints, arbitrary XMM register allocation, host-assembler escape

**Represented GNU x87 atan2 memory assembly**:
The exact volatile statement in `libm_atan2_impl()` with one modifiable, non-atomic `double` `=m` output, two addressable, non-atomic `double` `m` inputs in `y`, `x` order, and one `memory` clobber. Named operands normalize before Linear IR freezes the statement. Linear IR evaluates the output, `y`, and `x` addresses once in source order. The i386 emitter uses Cupid's shared x86 model for both loads, `FPATAN`, and the final store. The focused function has 53 text bytes and no relocations.
_Avoid_: general x87 programs, reordered address evaluation, host-assembler escape

**Represented GNU x87 exponent memory assembly**:
The exact volatile statement in `libm_exp_impl()` with one modifiable, non-atomic `double` `=m` output, two addressable, non-atomic `double` `m` inputs in `x`, `log2e` order, and one `memory` clobber. Linear IR evaluates all three addresses once in source order. The i386 emitter runs the complete `exp2(x * log2(e))` pipeline through Cupid's shared x86 model. The focused function has 71 text bytes, no relocations, maximum x87 depth three, and balanced depth on return.

Compiler head and the checked seed distinguish both aligned GNU spellings of the exponent range subtraction. Legacy `fsub %st, %st(1)` retains GNU's `DC E1` reverse-subtract meaning. Corrected `fsubr %st, %st(1)` emits canonical `FSUB ST(1), ST(0)` as `DC E9`, which computes `x - round(x)`. The checked seed and source head have 596 forms and fingerprint `DA15E97F`; the four SHRD rows cover canonical 16-bit and 32-bit SHRD with immediate or fixed CL counts. Active `libm.cc` uses the corrected spelling at all seven range-reduction sites. ADRs 0207 through 0209 record the exponent diagnosis, seed carriage, and runtime-tested source correction. ADR 0226 records SHRD, and ADR 0228 records its seed carriage.
_Avoid_: general x87 programs, host-assembler escape, changing the active libm algorithm

**Represented GNU fabs file-scope assembly**:
The exact aligned `fabs` mask block and following `fabs` and `fabsf` wrappers in `kernel/cpu/libm.cc`. The mask effect owns the first 32 bytes of `.rodata` at alignment 16 and defines local `STT_NOTYPE` labels at offsets 0 and 16. The wrappers retain their source prototypes, emit through Cupid's shared x86 model, and carry one `R_386_32` relocation each to the matching mask.
_Avoid_: passing file-scope data to GAS, moving the masks behind ordinary read-only objects, converting assembly labels into C declarations

**Represented GNU libm rounding file-scope assembly**:
The exact `floor`, `floorf`, `ceil`, `ceilf`, `round`, `roundf`, `trunc`, and `truncf` definitions in `kernel/cpu/libm.cc`. The checked seed saves the incoming x87 control word, clears its rounding field with `0xf3ff`, installs `RC=01`, `RC=10`, `RC=00`, or `RC=11`, runs `FRNDINT`, and restores the original word. The nearest-even pair omits the OR instruction. Each function uses the source scalar width, returns through XMM0, reaches x87 depth one, balances ESP and x87 depth, and emits through Cupid's shared x86 model. The eight wrappers add 384 text bytes and no relocations.
_Avoid_: general GAS input, changing the active rounding mode, leaving the patched x87 control word active

**Represented GNU libm remainder file-scope assembly**:
The exact `fmod` and `fmodf` definitions in `kernel/cpu/libm.cc`. The checked seed loads `y` below `x`, repeats `FPREM` while status-word C2 remains set, discards ST(1), and returns the remaining value through XMM0 at the source width. The loop uses `FNSTSW AX`, `TEST AX, 0x0400`, and a checked rel8 `JNE` with displacement `-10`, all through Cupid's shared x86 model. Each 35-byte wrapper reaches x87 depth two, balances ESP and x87 depth, and has no relocation.
_Avoid_: one-shot `FPREM`, raw opcode append, near-branch substitution, host-assembler escape

**Represented GNU libm exponent/logarithm file-scope assembly**:
The exact aligned `libm_log2e_const` and `libm_ln2_const` block followed by `exp2`, `exp2f`, `exp`, `expf`, `log2`, `log2f`, `log`, and `logf` in `kernel/cpu/libm.cc`. The block owns 16 bytes of `.rodata` at alignment eight and defines two local `STT_NOTYPE` labels at offsets 0 and 8. The exponent functions share the source `FRNDINT`, `F2XM1`, and `FSCALE` pipeline. The logarithm functions use `FYL2X` with either `FLD1` or the stored `ln(2)` value. The four natural forms carry checked `R_386_32` relocations to the matching constant. The family emits 264 text bytes, reaches at most x87 depth three, and balances ESP and x87 depth.
_Avoid_: host floating-point constants, general GAS input, duplicating the exponent sequence, changing the active libm algorithm

**Represented GNU libm cdecl bridge assembly**:
The exact final 18 file-scope wrappers in `kernel/cpu/libm.cc`: the binary `pow`, `hypot`, and `nextafter` pairs plus the unary `asin`, `acos`, `sinh`, `cosh`, `tanh`, and `cbrt` pairs. Each wrapper copies its original cdecl argument words, calls the matching external `libm_*_impl` function through one `R_386_PC32` relocation with addend `-4`, reclaims the copied arguments, and moves the ST(0) result into XMM0 at the source width. The checked seed validates the wrapper and callee prototypes and emits all four one- or two-argument float or double stack shapes through Cupid's shared x86 model. The family owns 558 text bytes and 18 relocations. It completes deterministic checked-seed object emission for `kernel/cpu/libm.cc`.
_Avoid_: tail jump that changes the callee stack layout, raw opcode append, missing callee type validation, host-assembler escape

**Represented GNU dglibc jump assembly**:
The exact combined `dg_setjmp` and `dg_longjmp` file-scope effect in `kernel/doom/dglibc.cc`. Active dglibc uses the corrected form: setjmp is `returns_twice`, longjmp is `noreturn`, and the 31-byte setjmp body saves the caller's post-return `ESP + 4`. The checked seed also recognizes the historical 27-byte compatibility form. Both forms use Cupid's shared x86 model, have no relocation, preserve the rule that a zero jump value becomes one, and jump through the saved return address. ADR 0213 records seed promotion, and ADR 0214 records active adoption.

**Returns-twice call boundary**:
A direct CupidC call to a function marked with GNU `returns_twice` or `__returns_twice__`. CupidC merges the attribute across compatible declarations. A marked function must be called directly; conversion to a function pointer is rejected. Supported calls use four-byte cdecl arguments and may return void or any nonaggregate type. The emitter uses the validated Linear IR stack depth to copy every live four-byte operand below the arguments into call-owned frame slots. It restores those words after caller cleanup and before publishing the call result. A live-prefix call is rejected if any returns-twice continuation can reach it again, while a call with no live prefix may repeat. Aggregate, wide-integer, and wider-than-four-byte floating arguments and aggregate results fail with a specific unsupported diagnostic. The checked seed carries this boundary, and active dglibc uses it for the Doom shell-session envelope.
_Avoid_: indirect marked call, marked-function pointer conversion, outgoing-area argument, aggregate result, live-prefix checkpoint reentry, treating the hosted oracle as guest evidence, IWAD runtime claim

**Doom shell session**:
One call to `doom_main` inside the long-lived Cupid shell process. It owns one nonlocal-exit envelope, one exit-callback list, the recursive-error guard, config-string allocations, and the initial-value snapshot used to reset registered defaults. Landing after `I_Quit` or `I_Error` releases that session before control returns to the shell. ADR 0214 records the boundary.
_Avoid_: operating-system process, retaining callbacks between launches, resetting an active callback walk

**HomeFS replacement transaction**:
The ordered update used for Doom config, save slots, and the `HOMEFS.SYS` container. A caller writes and closes a temporary file, native same-mount rename publishes it, and FAT16 writes and syncs a new cluster chain before it publishes the directory entry. A failed directory sync restores the old sector when possible. Replacement frees the old chain only after the new entry is durable; deletion makes the removed entry durable before it frees that chain. Cache writeback errors travel back to the caller, and a failed read fills scratch storage before it can change a cache entry's identity. A failed cleanup may leak clusters, but it must not remove the newly published file. While HomeFS is mounted, a private FAT reservation blocks raw writes, deletion, and write-capable `/disk/HOMEFS.SYS` aliases. ADR 0211 records the boundary.
_Avoid_: copy-and-unlink rename, deleting the prior file first, mutating an evicted cache entry before a successful read, two live owners, claiming power-cut proof

**HomeFS mutation batch**:
A nestable group of related HomeFS mutations. Operations update the live tree and mark it dirty, but only the outermost `homefs_batch_end` publishes `HOMEFS.SYS` and reports the durable result. An unmatched end fails, depth overflow is rejected, and unmount remains busy while a batch is open. The guest dglibc diagnostic uses one batch so its many filesystem probes produce one final container replacement instead of rewriting the whole tree after each probe.
_Avoid_: transaction rollback for the in-memory tree, hiding the outer publication error, unmounting an open batch

**Represented GNU x87 round-down memory assembly**:
The exact volatile statement in `str_floor()` that loads one `double`, saves the x87 control word below ESP, selects round toward negative infinity, executes `frndint`, restores the saved word, and stores the result. It requires one modifiable `double` `=m` output, one addressable `double` `m` input, and the exact `ax` plus `memory` clobber set. Linear IR evaluates the output address before the input address. The emitter reuses the consumed input-address slot for the two control-word values without touching the pending output address. The checked seed emits the complete helper and the later double-to-`uint64_t` casts, so unchanged `kernel/core/string.cc` compiles completely and deterministically through its production recipe.
_Avoid_: general AX clobber, arbitrary x87 control-word template, frame scratch that changes the active offsets

**Represented GNU descriptor-table assembly**:
The four exact volatile assembly forms in `kernel/smp/percpu.cc` that load a packed six-byte GDTR, reload DS, ES, SS, and CS, and write a represented 16-bit selector to GS. The LGDT forms require one addressable, non-atomic, complete six-byte `m` input and the exact `ax` plus `memory` clobbers. The standalone code-segment reload requires its `memory` clobber, while the GS form requires one 16-bit `r` input and no clobbers. Linear IR retains the GDTR address or selector value. The emitter uses shared x86 encodings and replaces absolute local-label control transfers with a relative `CALL`, `JMP`, and `RETF` trampoline that leaves no relocation. The checked seed carries all four forms, and the normal build uses that path for the per-CPU root.
_Avoid_: general segment assembly, scalar GDTR substitute, absolute compiler-local label relocation

**Represented GNU flags-restore assembly**:
The exact volatile `pushl %0`, `popfl` statement used twice by `simd_cpu_has_cpuid()`. It requires one non-atomic 32-bit integer `r` input, no outputs, and exactly one `cc` clobber. Linear IR retains the input value and validates the frozen clobber metadata. The emitter consumes the value through EAX, pushes it back, and emits POPF through the shared x86 model without a frame temporary or relocation. The checked seed carries this form, and the normal build uses it in `kernel/cpu/simd.cc`.
_Avoid_: general CC clobber, arbitrary EFLAGS template, dropping a real flags effect

**Represented GNU packed SSE2 assembly**:
The six exact volatile statement shapes used by `simd_memcpy()`, `simd_memset32()`, `simd_blend_row()`, and `simd_add_rows()`. Checked-seed CupidC requires zero outputs, the source's ordered pointer or 32-bit integer `r` inputs, and each template's exact memory plus XMM0 through XMM7 clobber set. Linear IR evaluates the inputs once in source order. The i386 emitter consumes them through EAX, EDX, and the existing evaluation stack, then emits every packed operation through Cupid's shared x86 model. Two complete checked-seed builds of unchanged `kernel/cpu/simd.cc` produce the same validated 8,768-byte object. The normal recipe uses that checked path.
_Avoid_: general XMM allocation, arbitrary SSE template, host-assembler escape, changing the active SIMD source

**Represented GNU kernel-entry BSS-clear assembly**:
The exact operand-free volatile statement that is the direct first child of the external, prototyped `void _start(void)` body in `.text.start`. It installs the fixed kernel stack and clears the linked BSS range. The statement requires the exact EAX, ECX, EDI, and memory clobbers plus visible external object declarations for `_bss_start` and `_kernel_end`. The function cannot have a compiler-managed frame. Its nonzero stack top is written as one through eight hexadecimal digits and must be aligned to 4 KiB. The active source installs `0x01100000`. The emitter loads both symbols through `R_386_32` relocations, derives the doubleword count, clears EAX, and emits CLD plus REP STOSD through the shared x86 model. The following `kmain()` call uses stack-base residue zero. If it returns, `_start` enters an interrupt-disabled halt loop. Checked-seed CupidC emits unchanged `kernel/core/kernel.cc` completely as a deterministic 25,920-byte object. The normal production recipe uses that checked path.
_Avoid_: fixed numeric BSS bounds, missing clobbers, label-wrapped, nested, or reordered entry reset, compiler frame after ESP replacement, host-assembler escape

**Represented GNU fixed-register overlap**:
A GNU assembly input whose fixed register is also assigned to one compatible write-only output in the same statement. The input keeps its source constraint, while `matching_output` names the shared output slot. Frontend and Linear IR require the same fixed register, equal widths, and represented integer types. They reject a second input tied to that output, an independently colliding fixed input, or frozen same-width pointer, floating, and aggregate substitutions. The emitter repeats the integer and width checks before it loads the input into the selected output register. The checked seed uses this rule for the unchanged CPUID `=a` output and `a` leaf input in `kernel/cpu/simd.cc`; numeric matching constraints keep their existing representation.
_Avoid_: independent duplicate register, rewriting valid source to a numeric tie, read/write or early-clobber output

**File-scope GNU assembly effect**:
A GNU-mode CupidC translation-unit effect that owns one immutable basic assembly template outside every function. The frontend keeps these effects in a table separate from statement assembly, and Linear IR preserves their source order without pretending that they are function-body instructions. The checked seed emits all file-scope effects in `kernel/cpu/libm.cc`: the opening twelve x87/SSE wrappers, `fabs` data and wrappers, eight rounding functions, two remainder functions, the exponent/logarithm constant block and eight wrappers, and the final 18 cdecl bridge wrappers. It also emits the exact combined dglibc jump effect. Represented functions are prologue-free `STT_FUNC` symbols encoded through Cupid's shared x86 model. Arbitrary GAS text, operands, labels, directives, unrecognized data definitions, and a host-assembler escape remain outside this term.
_Avoid_: GNU assembly statement, general GAS input, hidden host assembler

**Represented GNU entity attribute**:
A GNU-mode `weak`, `section`, `unused`, `used`, `noinline`, `target("general-regs-only")`, or `naked` fact attached to the canonical file-scope object or function after compatible redeclarations merge. `weak` selects ELF weak binding. `section` owns one decoded nonreserved ELF section name and directs compatible object or function definitions there. `unused` records that a declaration may lack an ordinary use. `used` records that a definition must remain in object output. `noinline` preserves a function request for later inlining policy. `target("general-regs-only")` rejects compiler-generated floating work in that function while leaving explicit source assembly under its own contract. A represented naked function has type `void (void)` and owns exactly one complete IPI assembly statement: `pushal`, a canonical direct C-function call, `popal`, and `iret`, or the `cli`, `hlt`, and backward-jump panic loop. Its object path emits no C prologue, epilogue, local reservation, or synthetic return. Each IR function retains its canonical code generation mask, and emission rejects a mismatch. Every represented definition currently reaches ELF32 output and CupidC has no inliner, so `used` and `noinline` affect metadata but not bytes. Invalid placement, conflicting section names, malformed arguments, forged frozen metadata, and GNU-disabled use fail transactionally. The checked seed carries all seven facts.
_Avoid_: skipped attribute text, opaque target strings, every GNU attribute, hardcoded active section names, arbitrary naked C bodies

**Represented GNU integer atomic**:
A GNU-mode CupidC load, store, exchange, fetch-add, or fetch-or on a complete one-, two-, or four-byte integer object. Its public AST and Linear IR keep the object width, evaluated pointer type, and constant memory order. The i386 emitter uses ordinary loads and release stores, memory `XCHG` for exchanges and sequentially consistent stores, `LOCK XADD` for fetch-add, and a `LOCK CMPXCHG` retry loop for fetch-or. The retry loop returns the old value, retains competing updates, and preserves EBX. The checked seed carries all five operations. It compiles the active EHCI fetch-or path as well as the SMP byte flags and 32-bit counters. The normal build uses the seeded path for `acpi.cc` and `mp_tables.cc`; the renamed graph passes the four-vCPU gate with all four discovered processors online.
_Avoid_: volatile substitute, general atomic library, runtime memory order

**Aligned call site**:
An i386 call instruction emitted with ESP on a sixteen-byte boundary before the CPU pushes the return address. Hosted CupidC derives the required padding from the fixed frame, live Linear IR stack depth, and outgoing ABI storage.
_Avoid_: sixteen-byte function frame, aligned callee entry

**Variadic call site**:
A call through a prototyped function type whose final parameter is an ellipsis. Linear IR keeps the named parameter count in the function type. Each call instruction owns its actual argument count and a source-ordered slice of the actual post-conversion types. Hosted i386 emission transports represented four-byte integer and pointer values, signed or unsigned eight-byte integers, existing `double` or `long double` values, and source `float` values after default promotion to `double`. An eight-byte argument occupies adjacent low and high four-byte words in the sixteen-byte-aligned outgoing area; a `long double` occupies three words.
_Avoid_: unprototyped call, variadic macro

**Unprototyped call site**:
A call through a function type that does not declare parameter types. The frontend applies default argument promotions to every argument. Linear IR keeps the actual count and post-conversion type slice on the call instruction. Hosted i386 emission transports represented four-byte integers and pointers, signed or unsigned eight-byte integers, existing `double` or `long double` values, and source `float` values after promotion to `double`. A `long double` uses three adjacent cdecl words in both direct and indirect calls.
_Avoid_: variadic call, call with zero parameters

**Variadic cursor**:
The target `char *` value used by hosted CupidC to traverse unnamed i386 cdecl arguments. `va_start` points it just past the final named argument. A supported non-atomic four-byte integer or pointer `va_arg` advances it by four. A signed or unsigned eight-byte integer, represented wide enum, or `double` read advances it by eight and returns an instruction-owned snapshot. A `long double` read copies twelve bytes into its snapshot and advances by twelve, so the next four-byte value stays aligned. `va_arg(float)` is invalid because an unnamed `float` arrives as `double`. `va_copy` copies the cursor, and `va_end` consumes its evaluated address without changing stored state.
_Avoid_: host `va_list`, argument array

**C mode**:
The CupidC language mode for freestanding C source.
_Avoid_: Cupid mode

**Doom compatibility profile**:
The explicit compiler profile for source requirements audited in the vendored Doom cohort. It adds old-style implicit function declarations and marked i386 function/data pointer conversions to the exact Doom preprocessing and GNU-extension profile. It does not change ordinary C or plain GNU mode. Production uses the profile for three compatibility roots and the matching Doom-tree profile for the other 80 roots.
_Avoid_: GNU mode, ordinary C mode

**Cupid mode**:
The CupidC language mode for Cupid C source and its native extensions. Its
shared declaration frontend treats `U0` as `void`; `U8`, `U16`, `U32`, and
`U64` as unsigned target integers; `I8`, `I16`, `I32`, and `I64` as signed
target integers; `Bool` and `bool` as signed `int`; and `float4` or `double2`
as 16-byte vectors. The strict C11 profile leaves those spellings available
as ordinary identifiers and typedef names. This distinction lets unchanged
`kernel/cpu/simd_intrin.h` publish all 29 bindings under its proper profile.
The
private in-kernel JIT and AOT compiler keeps integer expressions in EAX and
scalar floating expressions in XMM registers. Runtime unary plus preserves an
arithmetic scalar. Runtime unary minus uses integer `NEG` for `char` and
`int`, or toggles only the IEEE-754 sign bit for `float` and `double`.
All six scalar floating comparisons return a normalized `int`. Matching
widths compare directly, while a mixed `float` and `double` pair compares as
`double`. Explicit parity handling keeps every unordered relation false
except `!=`. Scalar floating truth is also normalized in EAX before `!`,
`if`, `?:`, `while`, `for`, or `do` tests it. Positive and negative zero are
false. Every nonzero value, including infinity and NaN, is true. Void
expressions, structures by value, and SIMD vectors are not scalar truth
operands. Prefix and postfix `++` and `--` add or subtract an exact one from
scalar `float` and `double` variables. Postfix results preserve the old
floating payload while the updated value is stored.
Direct calls and both method-call forms use one mixed-width cdecl layout.
Four-byte scalar and pointer values may appear beside eight-byte `double`
values in any order. The implicit method `self` is the first four-byte slot.
Argument expressions keep left-to-right evaluation, while their complete
stack words are permuted into source order before the call. Callees use the
same widths for parameter offsets, and caller cleanup uses their exact sum.
Non-arithmetic operands receive a specific diagnostic, and a rejected REPL
expression does not poison the next compilation. The frontier accepts that
diagnostic only once, inside the completed `feature13_double.cc` command
slice. A stale, repeated, or out-of-context compiler error remains fatal. A
host oracle compiles the active emitter helpers and interprets their exact
bytes against binary32 and binary64 payloads. The kernel bridge publishes the
declared result type of all 510 bindings: 319 return a value and 191 return
`void`. The value group has 205 promoted integer, 40 unsigned-word, 25
`float`, 25 `double`, 19 character-pointer, and five other pointer results.
ADR 0189 records unary signs, ADR 0192 records scalar comparisons, ADR 0193
records scalar truth and binding-result metadata, and ADR 0221 records the
unsigned result split. ADR 0194 records floating variable updates. ADR 0198
records mixed-width cdecl calls.
_Avoid_: C mode, HolyC mode

**Private CupidC floating lvalue**:
A typed `float` or `double` object reached through a fixed array, pointer, or
record field. Global, automatic, block-static, and persistent REPL arrays keep
their scalar width through one, two, or three dimensions. Each subscript uses
the remaining row stride, and `sizeof(array[index])` reports that row without
evaluating the index. Depth-one floating pointers keep their pointee type
through declarations, returns, address expressions, parameter decay,
dereference, subscripting, assignment, and arithmetic compound assignment.
Direct pointer updates use the pointee width. Structure and class objects,
their arrays, and their pointers retain scalar floating fields and
one-dimensional fixed floating field arrays. Every bound and allocation is
checked before storage is reserved. Floating pointer depth greater than one,
indirect floating `++` and `--`, pointer-to-array types, and
assignment through a pointer-valued floating record field remain outside this
boundary. ADR 0210 records the first array slice; ADR 0215 records the broader
lvalue model.
_Avoid_: complete multi-level floating pointer support, indirect floating update

**Private CupidC decimal literal**:
A decimal `float` or `double` token converted with a 1536-bit integer
workspace. The converter forms an exact decimal ratio and rounds it once to
binary32 or binary64 using nearest-even. An `f` or `F` suffix selects binary32
before rounding. Decimal subnormals, finite limits, overflow to infinity,
underflow to zero, and a following unary sign keep their IEEE payloads. A
numeric token may contain at most 95 characters, including its suffix. The
lexer consumes the whole rejected token, leaves the next delimiter available,
and reports a focused length or exponent error. Parser recovery keeps that
first public diagnostic. Hexadecimal floating and `long double` literals are
outside this boundary. ADR 0217 records the model.
_Avoid_: host `strtod`, binary64-first conversion of an `f` literal

**Private CupidC SIMD value**:
A `float4` or `double2` value carried through direct packed arithmetic or a
one-dimensional fixed array. Matching vectors support `+`, `-`, `*`, and `/`.
Global, automatic, block-static, and persistent REPL arrays use 16-byte
elements, unaligned-safe loads and stores, plain assignment, the four
arithmetic compound assignments, lane reads, checked bounds, and `sizeof`.
Direct arithmetic keeps the written left value in the machine destination.
The SSE minimum and maximum intrinsics retain the second-operand rules for NaN
and signed zero. A both-NaN ADD or MUL result may carry either input payload,
depending on the processor or emulator. SIMD pointers, multidimensional
arrays, record fields, allocation with `new`, and function-call ABI transport
remain outside this boundary. ADR 0216 records the model.
_Avoid_: untyped SIMD storage, reordered packed operands

**Browser JavaScript number lane**:
The numeric path shared by the Browser's JavaScript lexer, AST, and
interpreter. Decimal, hexadecimal, binary, and octal tokens enter a dedicated
`double` field and reach runtime without narrowing through an integer node.
Valid separators may appear between digits. The lexer rejects empty radix
bodies, invalid radix digits, misplaced separators, and identifier suffixes.
Primitive conversion decodes UTF-8 while trimming the ECMAScript whitespace
set, consumes the complete string, accepts decimal exponents, radices without
a sign, and signed `Infinity`, and returns NaN for invalid text or `undefined`.
Equality handles same-type primitives, the null and undefined pair,
Boolean-to-number conversion, and number/string conversion. Two strings
compare by UTF-16 code unit. Remainder and `%=` use `fmod`. String `+` and
`+=` append into the remaining 64 KiB string pool and report exhaustion.
Assignment resolves its binding, member receiver, or computed key once before
the right side runs, then stores through that saved identity. Each binding
records its owning scope; declarations search only their current scope, while
expression lookup may walk through parents. Checked value pushes restore an
expression's entry depth after overflow. Every string interning path reserves
the complete slice before it publishes a token, binding, property, or value;
failed global installation blocks queued scripts. Native function IDs travel
with their tags through stack, binding, property, and return lanes. Canonical
array-index writes grow an unsigned length lane through index 4,294,967,294.
The non-index key 4,294,967,295 remains an ordinary property, and direct
`length` assignment is rejected.
Finite formatting avoids out-of-range integer casts and covers plain values
below `1e21` plus scientific notation below `1e-6` or at least `1e21`. The
26-field asset-free self-test also requires ten useful syntax failures, a
600-byte concatenation, transactional pool exhaustion, side-effecting compound
targets, nested-call scope ownership, native round trips, array limits, full
and near-full value-stack failures, a 1,100-write balance check, and same-run
recovery.
ADR 0210 records the first binary64 slice; ADR 0218 records this boundary.
_Avoid_: complete ECMAScript coercion, object-to-primitive conversion, shortest decimal formatting

**Private CupidC adjacent string literal**:
One or more neighboring C string tokens emitted as one null-terminated data
object. Each decoded token may contain at most 1,023 bytes. The combined value
can use the remaining 8 MiB private data section and works in automatic
expressions, file-scope initializers, and persistent REPL declarations. The
lexer consumes a rejected overlong token before reporting it, while the parser
reports joined-data exhaustion without publishing a truncated string. ADR
0218 records the model.
_Avoid_: silently truncated C strings, one fixed buffer for the joined value

**Private CupidC tagged structure typedef**:
A private JIT, AOT, or persistent REPL declaration that gives one structure
both a tag and an ordinary typedef name. The typedef table keeps the structure
index beside the coarse value category, so alias chains and pointer aliases
retain the field layout used by `.` and `->`. Tagged and anonymous forms share
one field-layout parser. ADR 0219 records the model.
_Avoid_: dropping the structure index, rewriting a tagged typedef as anonymous

**Private CupidC record-field address**:
The address produced for a represented record member in private JIT or AOT
source. `&record.field` starts from the record object, while
`&pointer->field` first loads the pointed-to record. Both forms add the
declared field offset and leave the selected address in EAX instead of loading
the field value. ADR 0219 records the model.
_Avoid_: the address of the pointer variable, a field value used as an address

**Private CupidC checked allocation**:
The common size boundary for private fixed arrays, records, globals, persistent
REPL objects, and automatic frames. Array counts and strides must be positive,
and their product may not exceed `0x7ffffffc`. Record padding, each field, and
the final four-byte allocation alignment stay within that ceiling. Automatic
objects accumulate through one alignment-aware helper capped at `0x7ffffff0`,
leaving room for the function's final 16-byte frame rounding. Signed constant
expression arithmetic rejects overflow before evaluation; expressions with an
unsigned operand wrap modulo `2^32`. Decimal and hexadecimal integer literals
span the represented `uint32_t` range, and their suffix counts toward the
95-character token boundary. ADR 0219 records the model.
_Avoid_: checking only a wrapped product, direct local-offset subtraction

**Private CupidC REPL record checkpoint**:
A copy of every committed private structure definition held by the persistent
REPL. Rollback restores the records and the table count, so a failed line
cannot complete an older forward tag or expose rejected fields to later input.
ADR 0219 records the model.
_Avoid_: restoring only `struct_count`, fields from rejected REPL source

**Cupid ASM**:
The assembly language native to Cupid OS.
_Avoid_: CupidASM when referring to the language, NASM syntax

**CupidASM**:
The assembler for Cupid ASM source.
_Avoid_: Cupid ASM when referring to the assembler

**Cupid ASM alignment statement**:
The `align POWER_OF_TWO[, FILL_BYTE]` statement. In raw output it aligns the
absolute `ORG` address. In ELF32 objects it aligns the current section offset
and raises that section's `sh_addralign`. In fixed images it aligns the
absolute region placement and each later statement. The optional fill is one
byte and defaults to zero. A NOBITS section may only use zero fill because its
padding occupies memory without occupying the file.
_Avoid_: loader-provided alignment, placing an object first as an alignment guarantee, NASM escape

**CupidLD**:
The Cupid Toolchain linker.
_Avoid_: host linker

**CupidObj**:
The Cupid Toolchain object and binary transformation utility. `wrap` keeps
binary input unchanged, while `wrap-text` converts CRLF pairs to LF before it
builds an ELF32 object. A lone carriage return remains part of the input. Its
typed `install-source` operation emits the bin, docs, or demos installation
table from a validated repository path inventory. The checked seed rejects
more than 512 paths across all request lists before mode dispatch, accumulates that
total without overflow, and preserves caller order across mixed home-asset
extensions. The Python oracle follows the same order. The command is
self-hosted and byte-compatible with that oracle. The normal Make recipes
invoke the checked-seed command and depend on the complete CupidObj trust
inputs. ADR 0204 records the production transfer, and ADR 0205 records the
corrected seed.
The checked seed also compares every complete emitted binary symbol before it
writes a table. Distinct paths that normalize to one symbol fail, while the
exact same BMP path may appear once in the docs list and once in the home list
because both entries use one wrapped object. The Python oracle enforces the
same rule. ADR 0206 records its promotion.
_Avoid_: objcopy

**Installation source table**:
One generated `.cc` file that installs an auto-discovered source or asset
cohort into the boot filesystem. Checked-seed CupidObj emits the byte-exact
bin, docs, and demos formats. Checked-seed CupidC compiles the result.
_Avoid_: checked-in file list, embedded source object

**Canonical text wrap**:
The CupidObj transform used for source, manuals, demos, and vocabulary data.
It makes the embedded bytes independent of a host checkout's line endings
without changing binary assets.
_Avoid_: source formatting, binary wrapping

**CupidDis**:
The Cupid Toolchain disassembler and binary inspector.
_Avoid_: Cupid disassembler when naming the tool

**Conditional move family**:
The sixteen i686 `CMOVcc` operations represented by one shared x86 encoding and decoding rule. A canonical mnemonic names each condition, while conventional alternative spellings remain aliases.
_Avoid_: conditional jump, `SETcc`, separate assembler and disassembler definitions

**Immediate multiply family**:
The three-operand `IMUL` operation represented by the shared `69 /r` full-immediate and `6B /r` sign-extended-immediate encodings. Its destination is a 16-bit or 32-bit register, its source is a same-width register or memory operand, and the encoder chooses the shorter form only when the value fits a signed byte.
_Avoid_: one-operand multiply, two-operand `IMUL`, decoder-only instruction support

**Padding NOP family**:
The ordinary compiler alignment instructions represented by the single-byte `90`, operand-size-overridden `66 90`, and `0F 1F /0` register or memory encodings. Their operand and address sizes follow the normal 16-bit and 32-bit mode rules.
_Avoid_: PAUSE, arbitrary repeated legacy prefixes, treating every `0F 1F /r` group digit as NOP

**Clang repeated-prefix padding**:
Five exact 32-bit decode-only alignment NOPs with two through six leading `66` bytes followed by `2E 0F 1F 84 00 00 00 00 00`. They preserve the general rule that other repeated legacy prefixes are invalid and have no encodable form.
_Avoid_: a general repeated-prefix grammar, CupidASM output, a catalogue form

**Raw range map**:
An ordered set of borrowed byte ranges that classifies one flat image as 16-bit code, 32-bit code, or literal data. The first range starts at offset zero, and later starts increase within the source. CupidDis decodes code ranges and renders data ranges as `db` rows without entering the x86 decoder. The caller places code transitions at instruction boundaries.
_Avoid_: raw mode map, automatic code or mode detection, one kind per retained instruction

### Bootstrap

**Self-hosting**:
The state in which Cupid Toolchain source is built by the Cupid Toolchain itself.
_Avoid_: merely building Cupid OS with Cupid tools

**Bootstrap seed**:
A checked-in Cupid Toolchain executable that starts a bootstrap without an external code-generation toolchain.
_Avoid_: oracle toolchain

**Checked i386 Linux bootstrap seed**:
The manifest-bound set of static CupidC, CupidASM, CupidDis, CupidLD, and CupidObj executables under `bootstrap/seeds/i386-linux/`. Verification binds their hashes, sizes, ELF properties, target ABI, producer lineage, source revision, and exact 19-source build plan before execution. The current seed comes from revision `bd64a39d1b419df3fb3182c33869084f4bc09c2c`. CupidC is 2,578,244 bytes with SHA-256 `b652adc07442df04fa577fb7987598619cb573c5d932d639288ddddc939f622f`. CupidASM is 445,616 bytes with SHA-256 `1dc9061912f127d231d320940ba781781af663bde83852a613910394709ecc76`; CupidDis is 379,648 bytes with SHA-256 `a45fc4c57afd3bb02980e514d58c11588ba3a8bfa2f05ca348fe465cfdaf9749`. CupidLD and CupidObj remain byte-identical. The 5,440-byte manifest has SHA-256 `7e7da98d2adddbf59fbd7c4da7af7375e08c10147b8c802a2d4a816161f647ea`. The seed carries Cupid's sized scalar, Boolean, and vector type spellings plus the 596-row SHRD-capable x86 catalogue. Its post-promotion reproof matches all five seed images to stage two, then matches all nineteen C objects, startup, five tools, and the 5/11/7 behavior matrix between stages two and three. The frozen 41-input digest is `206a8124bbbc084153827308581131945aa62272e025edfcd33db910026363b5`. ADR 0228 records this promotion.
_Avoid_: current normal-build toolchain, native Windows seed, unverified binary cache

**Frozen fixed-point source closure**:
The exact 41 source inputs copied into one private compiler root before a checked-seed bootstrap runs. Both stages and their behavior checks consume that root. The harness rehashes the private and live closures at each boundary, then publishes the two stages, behavior evidence, and report as one complete directory only after every gate passes.
_Avoid_: live source root, source hash alone, public staging directory

**Bootstrap stage**:
One toolchain generation produced by the preceding generation during a bootstrap.
_Avoid_: build phase

**Fixed point**:
The bootstrap state in which consecutive toolchain generations are identical.
_Avoid_: successful compile

**Normal build**:
The supported path that builds Cupid OS with the Cupid Toolchain as its code-producing toolchain.
_Avoid_: oracle build, host build

**Oracle build**:
An optional comparison build used to establish external reference behavior or output.
_Avoid_: normal build

**Host toolchain**:
External compilers, assemblers, linkers, and binary utilities confined to explicit development and oracle paths. They do not produce normal Cupid OS or Toolchain artifacts. Host Python still coordinates the checked build, and Windows still uses WSL to execute static i386 Linux tools, so native Windows and Python-free fixed points remain open.
_Avoid_: build orchestrator
