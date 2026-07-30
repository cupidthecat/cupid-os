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
The permanently reserved identity-mapped range `[0x00F00000, 0x01100000)` leased exclusively to one ordinary fixed-address ELF process at a time.
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
A non-atomic `float` or `double` carried through hosted Linear IR as one logical value. A `float` keeps its raw four-byte representation. A `double` uses an emitter-owned eight-byte snapshot. Object loads, initialization, plain assignment, discard, fixed arguments and parameters, direct or indirect call results, returns, `double` ellipsis arguments, and non-atomic `va_arg(double)` reads use this path. A static-duration scalar evaluator uses integer-only IEEE binary32 and binary64 arithmetic for unary signs, addition, subtraction, multiplication, division, comparisons, casts, scalar truth, short-circuit logic, and conditional selection. It rounds each result to nearest with ties to even at the C expression width, covers represented signed and unsigned integers through 64 bits, preserves signed zero, and places the final target bits in `.rodata`, `.data`, or `.bss` through the ordinary static object policy. Explicit casts and assignment conversion work in both directions between the two floating widths. Unary plus and minus and binary addition, subtraction, multiplication, and division work for same-width or mixed-width runtime operands. All six equality and relational operators accept matching or mixed widths and produce a normalized signed `int`. Their `UCOMISS` or `UCOMISD` emission treats every unordered relation as false except `!=`. Matching floating conditional arms keep their width; mixed arithmetic and conditional arms use `double`. The four arithmetic compound assignments compute at the common width, convert back to the left width, and evaluate the lvalue once. Every changed x87 result is stored at its C width before the next IR instruction. Default argument promotion converts an ellipsis or unprototyped source `float` to `double` through x87 and a fresh snapshot. i386 calls place the final value in four or eight cdecl stack bytes. Floating results cross the ABI in x87 `ST0`; after call cleanup, the caller places a `float` in a four-byte semantic stack slot or a `double` in a private eight-byte frame snapshot. Runtime floating values are not represented truth operands, so IR and emission reject logical or branch metadata that names one. A comparison result is an ordinary `int` and can control a statement. Decoder-driven oracles check comparison behavior and call alignment; the arithmetic oracle models its supported x87 subset rather than executing native x87.
_Avoid_: general floating-point support, an exposed snapshot pointer

**Represented double-to-unsigned-wide conversion**:
An explicit non-atomic cast from `double` to `unsigned long long`. Linear IR keeps one typed conversion, and the i386 emitter decomposes the truncated result into exact high and low 32-bit words before publishing a wide-value snapshot. For finite binary64 input, the defined interval is `(-1, 2^64)`. Values for which C leaves the conversion undefined have no promised result.
_Avoid_: implicit assignment, float input, signed wide or enum output, host floating conversion

**Private compiler control frame**:
A tagged loop or switch entry used by the in-kernel CupidC emitter. `break` selects the nearest control frame. `continue` selects the nearest loop and removes the saved selector for each switch crossed on the way. The parser accepts 128 active control frames and fails before entering a 129th.
_Avoid_: loop-only depth stack, silent capacity exhaustion

**Private compiler statement depth**:
The number of active recursive statement-parser calls in the in-kernel CupidC compiler. The parser accepts 1,024 active calls and rejects the next call before it recurses. A failed REPL evaluation restores this count with the other committed parser state.
_Avoid_: relying on the terminal task's native stack as a language limit

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
The unchanged implementation files that hosted CupidC can preprocess, parse, lower, and emit as deterministic i386 ELF32 objects. The current gate contains all twelve hermetic `HOSTED_TOOLCHAIN_64` units and `kernel/lang/as_elf.cc`. The complete static i386 profile adds the hosted adapters, driver, runtime, and runtime contract, for 19 strict C11 units plus the GNU-enabled runtime. Complete CupidC-emitted closures for CupidC, CupidASM, CupidDis, CupidLD, and CupidObj link with CupidASM startup and the hosted i386 Linux runtime, then run real behavior checks on Linux or through WSL. The checked i386 Linux seed consumes this frontier, while production ownership remains a separate boundary.
_Avoid_: self-hosted toolchain, completed source cohort

**Production crypto cohort**:
All 20 `kernel/crypto` translation units built by checked-seed CupidC in the normal image build. Each deterministic i386 ELF32 object passes the shared validator before publication. The boot gate runs 62 crypto, ASN.1, and X.509 checks, walks the incomplete external CA-array path, and initializes the CSPRNG through its represented CPU assembly.
_Avoid_: compiler-head frontier, partial crypto cohort

**Production CupidC kernel cohort**:
The 155 checked-in normal-build translation units owned by checked-seed CupidC, plus the generated kernel symbol-table translation unit. All 156 sources use `.cc`. The five shared Toolchain roots are also part of the 19-source i386 Linux fixed-point plan; their native GCC and Clang rules select C explicitly with `-x c`. The symbol generator runs private snapshots of the pass-one kernel and CupidDis, validates the complete symbol view, rejects live input drift, and publishes the source atomically. The checked compiler wrapper freezes that source and its two-header closure before it validates and publishes the data-only object. It also freezes the exact source-driven closures for the kernel entry, SIMD services, the core string implementation, Nuked OPL3, JPEG decoding, glyph rasterization, libm, FPU state, per-CPU setup, and SMP bring-up. ADR 0124 records the first 111-root naming transfer, ADR 0126 completes the fixed-point naming boundary, ADR 0129 transfers the in-kernel CupidC lexer, ADR 0135 transfers Nuked OPL3, ADR 0139 transfers JPEG and glyph rasterization, ADR 0167 transfers the FPU and SMP roots, ADR 0176 transfers libm, ADR 0180 transfers the kernel entry and SIMD roots, and ADR 0181 transfers the final strict host root. The strict frontier compiles all 155 checked-in sources twice before atomic publication, and poisoned-host rebuilds and exact recursive header closures cover every recipe. Its input inventory skips hidden paths under the active include roots, which keeps private compiler staging headers out of the repository snapshot during concurrent builds. Its final directory promotion retries only short permission-style locks; a persistent lock or any other filesystem error fails without publishing a partial frontier. A data-only relocatable object is valid without `.text` when its sections and symbols pass the remaining ELF checks. The full frontier covers a 444-file snapshot with SHA-256 `bfa1e7210193b95df3c357a6c893078c86a74afa33e1cb2baa1cafc0173efab6`; both 155-object passes are byte-identical; each totals 3,708,988 bytes. The combined graph includes the ISO runtime fixture as an image input. Strong four-vCPU checks cover both supported NICs, all three FPU milestones, the promoted SMP, libm, and string paths, RDRAND, all 62 crypto checks, USB storage, desktop and terminal startup, audio output, TrueType glyph use, an exact baseline JPEG decode, and in-OS CupidC execution. The strict checked-in kernel and driver cohort has no host-compiled root.
_Avoid_: all kernel C, compiler-head frontier, checked seed alone

**Production generated-install cohort**:
The ramfs program table, homefs document table, and CupidASM demo table generated as `.cc` source and compiled by the checked CupidC seed under the fixed kernel profile. Their closed wrapper freezes the generator inputs, complete header union, seed manifest, and seed images, validates each relocatable object, and publishes it atomically.
_Avoid_: every generated C file, Python-free generation, kernel source cohort

**Production external-program cohort**:
The `hello.cc`, `ls.cc`, and `cat.cc` examples compiled by CupidC and linked by CupidLD. Linux runs the checked i386 Linux seed directly. Windows builds and runs native hosted CupidC and CupidLD drivers in private snapshots. A separate frontier requires all six Windows outputs to match the checked seed byte for byte. The native drivers still depend on a host C compiler and Windows linker, so this is not a native Windows fixed point. Before compilation, the user build captures the exact bytes of the six kernel and public declarations that define the shared i386 syscall contract, compares the reviewed layout, and rechecks every input before success. The reviewed contract is version 5 with 103 fields in 412 bytes, a 136-byte directory entry, an 8-byte file status record, and 101 pinned function providers. The build fixes the freestanding user profile, `_start`, and the `[0x00F00000, 0x01100000)` arena, then validates the same ELF program-header rules enforced by the kernel loader. `user/build/` contains ignored local outputs. The guest gate boots each program from a separate private copy of the same staged image, binds syscall evidence to the loaded PID, checks output by byte count and FNV-1a fingerprint, copies the hostile cat fixture over the normal `/home/readme.txt` path in the cat copy, and requires the same PID to exit cleanly. ADR 0127 records the ABI gate and corrected VFS record layout. ADR 0130 records the native Windows driver handoff. ADR 0133 records the ABI snapshot and private guest checks.
_Avoid_: every external program, hosted GCC examples, user-mode isolation

**Hosted i386 ABI profile**:
The deterministic hosted C request used to compile an i386 Linux tool closure. It searches `/toolchain` for quoted and angle includes and the checked i386 Linux declaration set for angle includes only, defines `__SIZEOF_POINTER__` as four, and leaves `_WIN32` undefined. The CupidC command represents those roots with `-I` and `--include-angle` in caller order. Repeatable `-include` options represent preprocessing inputs that run in order before the primary source. Tool sources use strict C11. The hosted runtime alone enables CupidC's GNU variadic built-ins for formatted diagnostics.
_Avoid_: `HOSTED_TOOLCHAIN_64`, vendored libc, host system headers

**Hosted i386 Linux runtime**:
The repository-owned startup and narrow C service layer for static Cupid-built i386 Linux commands. CupidASM supplies process entry and `int 0x80` system-call wrappers. CupidC supplies allocation, unbuffered files, standard streams, memory and string functions, `errno`, `getcwd`, and formatted diagnostics through the checked hosted declarations. A CupidC-built runtime contract checks the heap, files, errors, arguments, memory, and string surface under Linux or WSL.
_Avoid_: general libc, Windows runtime, test-only import providers

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
The exact volatile statement in `libm_pow_impl()` that consumes one modifiable `double` `=m` output, four addressable `double` `m` inputs, and one `memory` clobber. Checked-seed CupidC resolves its named operands to numeric indexes, evaluates all five addresses once in source order, and emits the complete `FYL2X`, `FRNDINT`, `F2XM1`, and `FSCALE` sequence with balanced x87 depth. The reverse subtraction uses the shared model's canonical `DC E1` encoding for `FSUBR ST(1), ST(0)`.
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
The exact final 18 file-scope wrappers in `kernel/cpu/libm.cc`: the binary `pow`, `hypot`, and `nextafter` pairs plus the unary `asin`, `acos`, `sinh`, `cosh`, `tanh`, and `cbrt` pairs. Each wrapper copies its original cdecl argument words, calls the matching external `libm_*_impl` function through one `R_386_PC32` relocation with addend `-4`, reclaims the copied arguments, and moves the ST(0) result into XMM0 at the source width. The checked seed validates the wrapper and callee prototypes and emits all four one- or two-argument float or double stack shapes through Cupid's shared x86 model. The family owns 558 text bytes and 18 relocations. It completes deterministic checked-seed object emission for unchanged `kernel/cpu/libm.cc`.
_Avoid_: tail jump that changes the callee stack layout, raw opcode append, missing callee type validation, host-assembler escape

**Represented GNU dglibc jump assembly**:
The exact combined `dg_setjmp` and `dg_longjmp` file-scope effect in `kernel/doom/dglibc.c`. Checked-seed CupidC validates the two external prototypes, rejects competing C definitions, and emits 27-byte and 38-byte prologue-free global functions through Cupid's shared x86 model. The pair has no relocation. It saves or restores EBX, ESI, EDI, EBP, ESP, and the return address, preserves the source rule that a zero long-jump value becomes one, and jumps through the saved address.
_Avoid_: general GAS input, raw opcode append, C rewrite, unchecked jump ABI

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
The exact operand-free volatile statement that is the direct first child of the external, prototyped `void _start(void)` body in `.text.start`. It installs the fixed kernel stack and clears the linked BSS range. The statement requires the exact EAX, ECX, EDI, and memory clobbers plus visible external object declarations for `_bss_start` and `_kernel_end`. The function cannot have a compiler-managed frame. The emitter loads both symbols through `R_386_32` relocations, derives the doubleword count, clears EAX, and emits CLD plus REP STOSD through the shared x86 model. The following `kmain()` call uses stack-base residue zero. If it returns, `_start` enters an interrupt-disabled halt loop. Checked-seed CupidC emits unchanged `kernel/core/kernel.cc` completely as a deterministic 25,920-byte object. The normal production recipe uses that checked path.
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
A call through a prototyped function type whose final parameter is an ellipsis. Linear IR keeps the named parameter count in the function type. Each call instruction owns its actual argument count and a source-ordered slice of the actual post-conversion types. Hosted i386 emission transports represented four-byte integer and pointer values, signed or unsigned eight-byte integers, existing `double` values, and source `float` values after default promotion to `double`. An eight-byte argument occupies adjacent low and high four-byte words in the sixteen-byte-aligned outgoing area.
_Avoid_: unprototyped call, variadic macro

**Unprototyped call site**:
A call through a function type that does not declare parameter types. The frontend applies default argument promotions to every argument. Linear IR keeps the actual count and post-conversion type slice on the call instruction. Hosted i386 emission transports represented four-byte integers and pointers, signed or unsigned eight-byte integers, existing `double` values, and source `float` values after promotion to `double`.
_Avoid_: variadic call, call with zero parameters

**Variadic cursor**:
The target `char *` value used by hosted CupidC to traverse unnamed i386 cdecl arguments. `va_start` points it just past the final named argument. A supported non-atomic four-byte integer or pointer `va_arg` advances it by four, while a signed or unsigned eight-byte integer, represented wide enum, or `double` read advances it by eight and returns an instruction-owned snapshot. `va_arg(float)` is invalid because an unnamed `float` arrives as `double`. `va_copy` copies the cursor, and `va_end` consumes its evaluated address without changing stored state.
_Avoid_: host `va_list`, argument array

**C mode**:
The CupidC language mode for freestanding C source.
_Avoid_: Cupid mode

**Doom compatibility profile**:
The explicit compiler profile for source requirements audited in the vendored Doom cohort. It currently adds old-style implicit function declarations and marked i386 function/data pointer conversions to the exact Doom preprocessing and GNU-extension profile. It does not change ordinary C or plain GNU mode.
_Avoid_: GNU mode, ordinary C mode

**Cupid mode**:
The CupidC language mode for Cupid C source and its native extensions.
_Avoid_: C mode, HolyC mode

**Cupid ASM**:
The assembly language native to Cupid OS.
_Avoid_: CupidASM when referring to the language, NASM syntax

**CupidASM**:
The assembler for Cupid ASM source.
_Avoid_: Cupid ASM when referring to the assembler

**CupidLD**:
The Cupid Toolchain linker.
_Avoid_: host linker

**CupidObj**:
The Cupid Toolchain object and binary transformation utility. `wrap` keeps
binary input unchanged, while `wrap-text` converts CRLF pairs to LF before it
builds an ELF32 object. A lone carriage return remains part of the input.
_Avoid_: objcopy

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

**Raw mode map**:
An ordered set of borrowed byte ranges that assigns 16-bit or 32-bit x86 decoding to one flat image. The first range starts at offset zero. Later offsets increase within the source, and the caller places each transition at an instruction boundary.
_Avoid_: automatic mode detection, one mode per retained instruction

### Bootstrap

**Self-hosting**:
The state in which Cupid Toolchain source is built by the Cupid Toolchain itself.
_Avoid_: merely building Cupid OS with Cupid tools

**Bootstrap seed**:
A checked-in Cupid Toolchain executable that starts a bootstrap without an external code-generation toolchain.
_Avoid_: oracle toolchain

**Checked i386 Linux bootstrap seed**:
The manifest-bound set of static CupidC, CupidASM, CupidDis, CupidLD, and CupidObj executables under `bootstrap/seeds/i386-linux/`. Verification binds their hashes, sizes, ELF properties, target ABI, producer lineage, source revision, and exact 19-source build plan before execution. The current seed is the stage-three output of a checked-seed bootstrap at revision `7609793ea594a8e024474509e5faacaf1d6c76ea`. Its CupidC image is 2,524,088 bytes with SHA-256 `d05b48f14c5c57930c151f4d7099d686066c6cface01305c7d2c0261b660970d`. It carries the complete 83-root Doom compiler frontier, including pointer-preserving static address casts and the exact dglibc jump effect, as well as the current GNU assembly and entity metadata and the active source-driven x87, SSE, descriptor, naked-entry, libm file-scope, kernel-entry BSS-clear, packed SSE2, and double-to-unsigned-wide capabilities. CupidASM and CupidDis carry the 587-row shared x86 catalogue. With all normal host code-generator commands poisoned, all five seed images match stage two; all 19 stage-two C objects, startup, and five images then match stage three. Both stages pass all 21 tool behavior cases over the 40-input snapshot `1199072a4415195a83e45c6469c79e066d445d96a884d6b0b9235cc09f035986`. The recorded seed revision remains separate from a later live source snapshot.
_Avoid_: current normal-build toolchain, native Windows seed, unverified binary cache

**Frozen fixed-point source closure**:
The exact 40 source inputs copied into one private compiler root before a checked-seed bootstrap runs. Both stages and their behavior checks consume that root. The harness rehashes the private and live closures at each boundary, then publishes the two stages, behavior evidence, and report as one complete directory only after every gate passes.
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
External compilers, assemblers, linkers, and binary utilities that currently produce or inspect Cupid OS artifacts and must be displaced from the normal build. At the fixed point they may remain only in an optional bootstrap or oracle path.
_Avoid_: build orchestrator
