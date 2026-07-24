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
The permanently reserved identity-mapped range `[0x00E00000, 0x01000000)` leased exclusively to one ordinary fixed-address ELF process at a time.
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
An eight-byte integer carried through hosted Linear IR as one logical value. The i386 emitter stores its bytes in a private frame snapshot and returns the low word in EAX and the high word in EDX. The current boundary covers constants, matching conditional arms, fixed call results, object access through file, block-static, automatic, pointer, member, and index paths, initialization, plain assignment, all ten compound assignments, prefix and postfix update, declared parameters, named direct or indirect call arguments, signed or unsigned ellipsis and unprototyped call arguments, discard, return, addition, subtraction, multiplication, division, remainder, unary plus, unary minus, bitwise complement, left and signed or unsigned right shifts, AND, OR, XOR, all six comparisons, logical not, short-circuit logical operators, conditional selection, structured scalar conditions, signed or unsigned switch dispatch, conversion to or from represented integer widths, and non-atomic `va_arg` reads. It also covers the standard `signed long long` to `unsigned long long` usual arithmetic conversion and, in GNU mode, promotion of a wide enum to its compatible wide integer type. A switch duplicates the value's snapshot handle and compares the complete eight-byte case value without evaluating the condition again. Mutation evaluates its lvalue address once and returns either the stored snapshot or the reconstructed postfix value. Multiplication combines the low-word product with both cross-word products. Division and remainder use a fixed restoring loop over unsigned magnitudes, then apply the quotient or dividend sign. Each multiplication, division, remainder, or wide variadic-read result receives a fresh snapshot. Shift counts remain represented four-byte integers. Runtime cases that C leaves undefined promise neither a trap nor a result.
_Avoid_: two unrelated 32-bit values, a public IR register pair

**Floating scalar value**:
A non-atomic `float` or `double` carried through hosted Linear IR as one logical value. A `float` keeps its raw four-byte representation. A `double` uses an emitter-owned eight-byte snapshot. Object loads, initialization, plain assignment, discard, fixed arguments and parameters, direct or indirect call results, returns, `double` ellipsis arguments, and non-atomic `va_arg(double)` reads use this path. Explicit casts and assignment conversion work in both directions between the two widths. Unary plus and minus and binary addition, subtraction, multiplication, and division work for same-width or mixed-width operands. Matching floating conditional arms keep their width; mixed arithmetic and conditional arms use `double`. The four arithmetic compound assignments compute at the common width, convert back to the left width, and evaluate the lvalue once. Every changed x87 result is stored at its C width before the next IR instruction. Default argument promotion converts an ellipsis or unprototyped source `float` to `double` through x87 and a fresh snapshot. i386 calls place the final value in four or eight cdecl stack bytes. Floating results cross the ABI in x87 `ST0`; after call cleanup, the caller places a `float` in a four-byte semantic stack slot or a `double` in a private eight-byte frame snapshot. Floating values are not represented truth operands, so IR and emission reject logical or branch metadata that names one. A decoder-driven oracle checks call alignment and models the supported x87 subset; it is not a native x87 execution proof.
_Avoid_: general floating-point support, an exposed snapshot pointer

**Private compiler control frame**:
A tagged loop or switch entry used by the in-kernel CupidC emitter. `break` selects the nearest control frame. `continue` selects the nearest loop and removes the saved selector for each switch crossed on the way.
_Avoid_: loop-only depth stack

**Represented bit-field assignment**:
A plain Cupid C assignment to a non-atomic integer bit field whose declared storage unit is four bytes and fits inside its record. Linear IR retains the graph member, while i386 emission preserves neighboring bits and returns the value represented by the stored field.
_Avoid_: bit-field address, ordinary member store

**Represented bit-field mutation**:
A compound assignment or prefix or postfix update to a represented bit field. Linear IR evaluates the record address once, applies target-width promotion, and keeps the extracted old value when postfix semantics require it. Partial fields are nonvolatile because their current store path needs a second complete-unit read. A volatile 32-bit field uses one read and one direct store.
_Avoid_: bit-field address, reconstructing a postfix value after truncation

**Block-static object**:
A block-scope object with static storage duration. It keeps its absolute frontend block-binding identity, receives a local ELF object symbol, and never consumes an automatic frame slot.
_Avoid_: automatic local, file-scope object

**Block extern alias**:
A lexical block declaration that names a canonical linked object. It owns no storage. Uses retain the canonical file-binding identity even when no ordinary file-scope name is visible.
_Avoid_: automatic local, block-static object

**Block function alias**:
A lexical block declaration that names a canonical linked function. It owns no storage or runtime work. Uses keep the type visible at the declaration and retain the canonical function identity even when no ordinary file-scope name is visible.
_Avoid_: nested function, automatic local

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
The unchanged implementation files that hosted CupidC can preprocess, parse, lower, and emit as deterministic i386 ELF32 objects. The current gate contains all twelve hermetic `HOSTED_TOOLCHAIN_64` units and `kernel/lang/as_elf.c`. The complete static i386 profile adds the hosted adapters, driver, runtime, and runtime contract, for 19 strict C11 units plus the GNU-enabled runtime. Complete CupidC-emitted closures for CupidC, CupidASM, CupidDis, CupidLD, and CupidObj link with CupidASM startup and the hosted i386 Linux runtime, then run real behavior checks on Linux or through WSL. The checked i386 Linux seed consumes this frontier, while production ownership remains a separate boundary.
_Avoid_: self-hosted toolchain, completed source cohort

**Hosted i386 ABI profile**:
The deterministic hosted C request used to compile an i386 Linux tool closure. It searches `/toolchain` for quoted and angle includes and the checked i386 Linux declaration set for angle includes only, defines `__SIZEOF_POINTER__` as four, and leaves `_WIN32` undefined. The CupidC command represents those roots with `-I` and `--include-angle` in caller order. Tool sources use strict C11. The hosted runtime alone enables CupidC's GNU variadic built-ins for formatted diagnostics.
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
The manifest-bound set of static CupidC, CupidASM, CupidDis, CupidLD, and CupidObj executables under `bootstrap/seeds/i386-linux/`. Verification binds their hashes, sizes, ELF properties, target ABI, producer lineage, source revision, and exact 19-source build plan before execution. A bootstrap freezes the verified manifest and binaries, then the seed producer trio builds stage two from a captured 40-input source snapshot. Stage two builds the byte-identical stage three.
_Avoid_: current normal-build toolchain, native Windows seed, unverified binary cache

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
