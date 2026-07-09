# Cupid Toolchain capability matrix

Statuses in this initial matrix mean:

- **Observed**: the implementation is present in source and at least one relevant smoke was executed.
- **Partial**: useful implementation exists, but the required active-source or format coverage is not proven.
- **Missing**: no implementation satisfying the bootstrap contract was found.
- **Audit pending**: the active-source inventory has not yet established an exact requirement set.

## CupidC

| Capability | Status | Baseline evidence and gap |
| --- | --- | --- |
| In-OS JIT compilation and execution | Observed | Explicit `/bin/ls.cc` GUI smoke passed; the default `ls` smoke was flaky once before passing unchanged. |
| In-OS AOT executable output | Partial | Writer emits fixed-address ELF32 `ET_EXEC` code/data segments. No systematic executable parity suite was run in this baseline. |
| C and Cupid language surface | Partial | Lexer/parser contain scalar, pointer, array, function, struct/class, enum, typedef, control-flow, floating/SIMD, inline-assembly, and extension support. Exact C11 semantics and active-source coverage remain audit pending. |
| Preprocessing and includes | Partial | Existing programs use built-in source/include handling. Required macro, conditional, include, and diagnostic compatibility must be inventoried against active C and vendored code. |
| Type checking and ABI | Partial | A compact internal type model and cdecl-oriented generation exist. Integer widths, aggregates, conversions, variadics, function pointers, qualifiers, storage duration, layout, and call/return behavior need source-driven conformance tests. |
| Diagnostics | Partial | Source locations and fail-fast errors exist, with `CC_MAX_ERRORS` currently one. Required positive/negative diagnostic coverage is not catalogued. |
| Typed AST and linear IR | Missing | Current implementation emits machine code during parsing. |
| Deterministic ELF32 `ET_REL` output | Missing | Current output is an executable with program headers and no relocatable section/symbol/relocation model. |
| Host-runnable compiler | Missing | CupidC is linked into the kernel and depends on kernel/VFS/runtime services. |
| Compile CupidC and the remaining toolchain | Missing | No staged self-compilation gate or checked-in seed exists. |
| Optimization and measured quality gate | Missing | No agreed cohort has Cupid/oracle size and runtime comparisons yet. |

## CupidASM

| Capability | Status | Baseline evidence and gap |
| --- | --- | --- |
| In-OS JIT assembly and execution | Observed | `as /demos/hello.asm` GUI-terminal smoke passed. |
| In-OS AOT executable output | Partial | Writer emits fixed-address ELF32 `ET_EXEC`; relocatable output is absent. |
| Intel-style x86 encoder | Partial | A substantial integer, control, system, x87, and SSE-oriented implementation exists. Exact instruction, operand-size, and execution-mode parity for active assembly remains audit pending. |
| Labels and expressions | Partial | Labels, forward patches, `equ`, arithmetic, and memory operands exist; cross-section/external relocation semantics are absent. |
| Directives and source composition | Partial | Text/data selection, data and reserve directives, `times`, and `%include` support exist. Alignment, macros, conditionals, `incbin`, `org`/`bits`, and other repo-used behavior must be measured rather than assumed. |
| Flat binary output for boot paths | Missing | Current host boot/trampoline binaries are still produced by NASM; the native writer targets in-OS execution. |
| Deterministic ELF32 `ET_REL` output | Missing | No standard sections, symbol bindings, or relocations suitable for CupidLD. |
| Assemble all active OS assembly | Missing | NASM still owns the boot loader, ISR, context switch, and SMP trampoline. |
| Host-runnable assembler and self-build | Missing | CupidASM is linked into the kernel and has no host seed or staged bootstrap gate. |

## CupidDis

| Capability | Status | Baseline evidence and gap |
| --- | --- | --- |
| In-OS instruction decoding | Partial | Decodes a useful 32-bit x86 subset and formats instructions/symbols; complete 16/32-bit through-SSE2 coverage is not proven. |
| ELF32 executable inspection | Partial | Reads loadable executable segments and function symbols when section data is available. Robust format/error coverage is not yet catalogued. |
| ELF32 `ET_REL`, sections, symbols, and relocations | Missing | Required object-inspection output and relocation rendering are not implemented as an objdump-class contract. |
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
