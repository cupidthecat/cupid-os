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
The permanently reserved identity-mapped range `[0x00D00000, 0x00F00000)` leased exclusively to one ordinary fixed-address ELF process at a time.
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
The typed, target-neutral instruction sequence between CupidC's function-body AST and machine-code emission. Its stack entries distinguish object addresses from scalar and structure values. Branch targets stay relative to one function, while parameters, represented automatic objects, compound-literal objects, runtime string literals, block-static objects, linked file objects, and linked function references retain their absolute frontend identities. Machine addresses, section offsets, symbol-table indices, frame offsets, literal symbols, and structure snapshot storage remain private to target emission.
_Avoid_: AST, x86 bytecode, machine code

**Block-static object**:
A block-scope object with static storage duration. It keeps its absolute frontend block-binding identity, receives a local ELF object symbol, and never consumes an automatic frame slot.
_Avoid_: automatic local, file-scope object

**Block extern alias**:
A lexical block declaration that names a canonical linked object. It owns no storage. Uses retain the canonical file-binding identity even when no ordinary file-scope name is visible.
_Avoid_: automatic local, block-static object

**Block typedef**:
A type alias whose name lives in one C block scope. It keeps a stable frontend type identity, shares the ordinary identifier namespace, and owns no runtime storage.
_Avoid_: file typedef, block object

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

**Aligned call site**:
An i386 call instruction emitted with ESP on a sixteen-byte boundary before the CPU pushes the return address. Hosted CupidC derives the required padding from the fixed frame, live Linear IR stack depth, and outgoing ABI storage.
_Avoid_: sixteen-byte function frame, aligned callee entry

**Variadic call site**:
A call through a prototyped function type whose final parameter is an ellipsis. Linear IR keeps the named parameter count in the function type and the actual argument count on the call instruction. Hosted i386 emission currently transports only represented four-byte scalar ellipsis arguments.
_Avoid_: unprototyped call, variadic macro

**Unprototyped call site**:
A call through a function type that does not declare parameter types. The frontend applies default argument promotions to every argument, and Linear IR keeps the actual argument count on the call instruction. Hosted i386 emission currently transports represented four-byte scalar arguments at this boundary.
_Avoid_: variadic call, call with zero parameters

**Variadic cursor**:
The target `char *` value used by hosted CupidC to traverse unnamed i386 cdecl arguments. `va_start` points it just past the final named argument, each supported non-atomic `va_arg` advances it by one four-byte slot, `va_copy` copies it, and `va_end` consumes its evaluated address without changing stored state.
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
The Cupid Toolchain object and binary transformation utility.
_Avoid_: objcopy

**CupidDis**:
The Cupid Toolchain disassembler and binary inspector.
_Avoid_: Cupid disassembler when naming the tool

### Bootstrap

**Self-hosting**:
The state in which Cupid Toolchain source is built by the Cupid Toolchain itself.
_Avoid_: merely building Cupid OS with Cupid tools

**Bootstrap seed**:
A checked-in Cupid Toolchain executable that starts a bootstrap without an external code-generation toolchain.
_Avoid_: oracle toolchain

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
