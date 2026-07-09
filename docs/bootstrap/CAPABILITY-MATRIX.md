# Cupid Toolchain capability matrix

Statuses in this initial matrix mean:

- **Observed**: the implementation is present in source and at least one relevant smoke was executed.
- **Partial**: useful implementation exists, but the required active-source or format coverage is not proven.
- **Missing**: no implementation satisfying the bootstrap contract was found.
- **Required**: the checked active-source audit proves the capability is exercised, but the Cupid implementation does not yet satisfy it.

The capability requirements below are backed by `ACTIVE-SOURCE-AUDIT.md` and `audits/active-build.json`: 642 active language inputs across root and user build roots, 248 stable feature IDs, 437 output transforms, and an explicit i386 ILP32/cdecl/ELF32 contract. Lexical counts are discovery evidence; semantic completion still requires focused compiler/assembler tests.

## CupidC

| Capability | Status | Baseline evidence and gap |
| --- | --- | --- |
| In-OS JIT compilation and execution | Observed | Explicit `/bin/ls.cc` GUI smoke passed; the default `ls` smoke was flaky once before passing unchanged. |
| In-OS AOT executable output | Partial | Writer emits fixed-address ELF32 `ET_EXEC` code/data segments. No systematic executable parity suite was run in this baseline. |
| C and Cupid language surface | Required/Partial | The audit covers 241 C translation units, 249 headers, and 126 Cupid C files. Required C includes structs/unions/enums, callbacks, bit-fields, static assertions, 64-bit integers, float/double, variadics, designated/partial initialization, and full control/expression semantics. Cupid mode additionally exercises sized types, `float4`/`double2`, class/new/del, reg/noreg, `#exe`, native asm blocks, and production browser-scale globals/includes. Current parser support is useful but not conformance-complete. |
| Preprocessing and includes | Required/Partial | Active source uses 2,000+ quoted and 100+ angle includes, forced Doom compatibility headers, predefined/command-line macros, nested conditionals, recursive guarded includes, multiline/function/variadic macros, stringify, paste, GNU comma elision, `__FILE__`/`__LINE__`, and pack/once pragmas. Current CupidC preprocessing has not passed that corpus. |
| Type checking and ABI | Required/Partial | The contract is i386 ILP32, signed plain char, cdecl, 16-byte stack/SIMD alignment, and ELF32 `ET_REL`. Active code requires real 8/16/32/64-bit semantics, float/double calls/returns, aggregates, callbacks, weak/undefined symbols, static/tentative storage, and volatile MMIO/SMP behavior. Cupid mode's existing compact model is insufficient for C mode until these are proven. |
| Diagnostics | Partial | Source locations and fail-fast errors exist, with `CC_MAX_ERRORS` currently one. Required positive/negative diagnostic coverage is not catalogued. |
| Typed AST and linear IR | Missing | Current implementation emits machine code during parsing. |
| Deterministic ELF32 `ET_REL` output | Missing | Current output is an executable with program headers and no relocatable section/symbol/relocation model. |
| Host-runnable compiler | Missing | CupidC is linked into the kernel and depends on kernel/VFS/runtime services. |
| Compile CupidC and the remaining toolchain | Missing | No staged self-compilation gate or checked-in seed exists. |
| GNU platform compatibility | Required/Partial | Active C has 199 GNU basic/extended asm sites across 33 files and 72 attribute sites across 31 files (`packed`, alignment, section, weak, noreturn, noinline, used/unused, naked). These are platform requirements, not optional source simplifications. |
| Doom compatibility mode | Required/Missing | The 83-file Doom/port C cohort needs old/no-prototype declarations, five implicit calls, permissive callback/object-pointer conversions, compatibility headers, and relaxed diagnostics without weakening strict C mode. |
| Optimization and measured quality gate | Missing | The oracle records size evidence, but no Cupid-built trusted cohort has the agreed size/runtime comparison yet. |

## CupidASM

| Capability | Status | Baseline evidence and gap |
| --- | --- | --- |
| In-OS JIT assembly and execution | Observed | `as /demos/hello.asm` GUI-terminal smoke passed. |
| In-OS AOT executable output | Partial | Writer emits fixed-address ELF32 `ET_EXEC`; relocatable output is absent. |
| Intel-style x86 encoder | Required/Partial | The 26 active assembly inputs contain 1,196 instruction statements, 91 mnemonics plus two active prefixes, 27 register names, and 99 memory operands. Required forms include mixed 16/32-bit operation, segment/control registers, far transfers, sized operands, segment overrides, and full ModRM/SIB indexing. Current parsing records index fields that emission does not consume and lacks several required modes/registers. |
| Labels, expressions, and relocations | Required/Partial | Active source requires 153 colon labels, dot-local labels, `equ`, `$`/`$$`, label differences, arithmetic/shift/complement expressions, binary-suffix literals, five external PC-relative calls, 33 exports, and both `R_386_32`/`R_386_PC32` object interoperability. Current forward patches do not provide that object/link model. |
| Directives and source composition | Required/Partial | Required forms include `BITS`, `ORG`, `section`, `global`, `extern`, `db/dw/dd/dq`, `times`, `equ`, `%define`, `%include`, and seven reserve spellings. Existing support is partial: global/extern are discarded, expressions are restricted, include resolution is CWD-relative, and raw/relocatable outputs are absent. |
| Flat binary output for boot paths | Missing | Current host boot/trampoline binaries are still produced by NASM; the native writer targets in-OS execution. |
| Deterministic ELF32 `ET_REL` output | Missing | No standard sections, symbol bindings, or relocations suitable for CupidLD. |
| Assemble all active OS assembly | Missing | NASM still owns four files: two exact flat binaries and two ELF32 objects. The other 22 assembly files are embedded CupidASM runtime inputs and remain a regression corpus. |
| Host-runnable assembler and self-build | Missing | CupidASM is linked into the kernel and has no host seed or staged bootstrap gate. |

## CupidDis

| Capability | Status | Baseline evidence and gap |
| --- | --- | --- |
| In-OS instruction decoding | Required/Partial | Active ASM plus C inline assembly proves a mixed 16/32-bit domain through x87/SSE2, prefixes, segments/control registers, far/system/atomic instructions, and SIB addressing. CupidDis currently skips several prefixes semantically and lacks 16-bit mode and broad system/SIMD coverage. |
| ELF32 executable inspection | Partial | Reads loadable executable segments and function symbols when section data is available. Robust format/error coverage is not yet catalogued. |
| ELF32 `ET_REL`, sections, symbols, and relocations | Required/Missing | ISR/context-switch objects and 241 compiled C outputs require sections, local/global/weak/undefined symbols, alignment through 64 bytes, and `R_386_32`/`R_386_PC32`. `dis_elf` currently requires a loadable segment and therefore cannot inspect these objects. |
| DWARF v4 source information | Missing | No line/debug information parser or source annotation is present. |
| Host-runnable object inspector | Missing | CupidDis is linked into the kernel and depends on VFS/runtime services. |
| Encoder/decoder parity tests | Missing | No shared instruction model or exhaustive assembler/disassembler round-trip suite exists. |

## Shared object, linker, and bootstrap capabilities

| Capability | Status | Baseline evidence and gap |
| --- | --- | --- |
| Shared ELF32 object library | Missing | CupidC, CupidASM, and CupidDis have separate minimal executable-format handling. |
| CupidLD | Missing | Host GNU `ld`/`ld.lld` owns layout, symbol resolution, relocations, and linker-script interpretation. |
| CupidObj | Missing | Host `objcopy` owns binary wrapping and raw extraction. |
| Shared x86 instruction model | Missing | Assembler and disassembler tables/logic are independent. |
| Windows and Linux checked seeds | Missing | No checked-in host-runnable Cupid Toolchain executables or provenance manifests exist. |
| Stage 2/stage 3 byte identity | Missing | No deterministic staged bootstrap harness exists. |
| Clean-checkout host independence | Missing | External compiler, NASM, linker, `objcopy`, and `nm` are all still required. |
