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
