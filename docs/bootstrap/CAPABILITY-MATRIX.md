# Cupid Toolchain capability matrix

Statuses in this initial matrix mean:

- **Observed**: the implementation is present in source and at least one relevant smoke was executed.
- **Partial**: useful implementation exists, but the required active-source or format coverage is not proven.
- **Missing**: no implementation satisfying the bootstrap contract was found.
- **Required**: the checked active-source audit proves the capability is exercised, but the Cupid implementation does not yet satisfy it.

The capability requirements below are backed by `ACTIVE-SOURCE-AUDIT.md` and `audits/active-build.json`: 659 active language inputs across root, user, and hosted toolchain build roots, 248 stable feature IDs, 458 output transforms, and an explicit i386 ILP32/cdecl/ELF32 contract. Lexical counts are discovery evidence; semantic completion still requires focused compiler/assembler tests.

## CupidC

| Capability | Status | Baseline evidence and gap |
| --- | --- | --- |
| In-OS JIT compilation and execution | Observed | Explicit `/bin/ls.cc` GUI smoke passed; the default `ls` smoke was flaky once before passing unchanged. |
| In-OS AOT executable output | Partial | Writer emits fixed-address ELF32 `ET_EXEC` code/data segments. No systematic executable parity suite was run in this baseline. |
| C and Cupid language surface | Required/Partial | The audit covers 252 C translation units, 255 headers, and 126 Cupid C files. Required C includes structs/unions/enums, callbacks, bit-fields, static assertions, 64-bit integers, float/double, variadics, designated/partial initialization, and full control/expression semantics. Cupid mode additionally exercises sized types, `float4`/`double2`, class/new/del, reg/noreg, `#exe`, native asm blocks, and production browser-scale globals/includes. Current parser support is useful but not conformance-complete. |
| Preprocessing and includes | Required/Partial | Active source uses 2,000+ quoted and 100+ angle includes, forced Doom compatibility headers, predefined/command-line macros, nested conditionals, recursive guarded includes, multiline/function/variadic macros, stringify, paste, GNU comma elision, `__FILE__`/`__LINE__`, and pack/once pragmas. Current CupidC preprocessing has not passed that corpus. |
| Type checking and ABI | Required/Partial | The contract is i386 ILP32, signed plain char, cdecl, 16-byte stack/SIMD alignment, and ELF32 `ET_REL`. Active code requires real 8/16/32/64-bit semantics, float/double calls/returns, aggregates, callbacks, weak/undefined symbols, static/tentative storage, and volatile MMIO/SMP behavior. Cupid mode's existing compact model is insufficient for C mode until these are proven. |
| Diagnostics | Partial | Source locations and fail-fast errors exist, with `CC_MAX_ERRORS` currently one. Required positive/negative diagnostic coverage is not catalogued. |
| Typed AST and linear IR | Missing | Current implementation emits machine code during parsing. |
| Deterministic ELF32 `ET_REL` output | Missing | The shared writer now owns the required object model, but CupidC still emits fixed-address executables and has no semantic lowering into it. |
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
| Intel-style x86 encoder | Observed/Partial | The 26 active assembly inputs contain 1,196 instruction statements, 91 mnemonic families plus two active prefixes, 27 register names, and 107 memory operands. A checked source normalizer derives 162 signatures/182 mode-specific cases; its traceable NASM 2.16.01 oracle manifest decodes and exact-replays in full. A separate manually audited, cohort-linked manifest represents the 208 active C/Cupid C inline-assembly sites with 96 source spellings and 129 checked source-spelling/form rows, retaining width/direction variants and source aliases. Coverage includes mixed 16/32-bit operation, segment/control registers, far transfers, sized operands, segment overrides, ModRM/SIB addressing, x87, and SSE2. The legacy CupidASM parser/emitter has not migrated to that model and still retains its earlier parsing, addressing, and output limitations. |
| Labels, expressions, and relocations | Required/Partial | Active source requires 153 colon labels, dot-local labels, `equ`, `$`/`$$`, label differences, arithmetic/shift/complement expressions, binary-suffix literals, five external PC-relative calls, 33 exports, and both `R_386_32`/`R_386_PC32` object interoperability. Current forward patches do not provide that object/link model. |
| Directives and source composition | Required/Partial | Required forms include `BITS`, `ORG`, `section`, `global`, `extern`, `db/dw/dd/dq`, `times`, `equ`, `%define`, `%include`, and seven reserve spellings. Existing support is partial: global/extern are discarded, expressions are restricted, include resolution is CWD-relative, and raw/relocatable outputs are absent. |
| Flat binary output for boot paths | Missing | Current host boot/trampoline binaries are still produced by NASM; the native writer targets in-OS execution. |
| Deterministic ELF32 `ET_REL` output | Missing | The shared writer now provides standard sections, symbol bindings, and relocations, but CupidASM does not yet lower labels/fixups into that interface. |
| Assemble all active OS assembly | Missing | NASM still owns four files: two exact flat binaries and two ELF32 objects. The other 22 assembly files are embedded CupidASM runtime inputs and remain a regression corpus. |
| Host-runnable assembler and self-build | Missing | CupidASM is linked into the kernel and has no host seed or staged bootstrap gate. |

## CupidDis

| Capability | Status | Baseline evidence and gap |
| --- | --- | --- |
| In-OS instruction decoding | Observed/Partial | The kernel `dis`, `exec -d`, and CupidC JIT adapters now call the same typed 16/32-bit x86 decoder and streaming formatter as hosted CupidDis; the duplicate kernel opcode tables are gone. The DEBUG boot self-test enters through both the preserved raw/JIT `dis_disassemble` adapter and a temporary-file VFS `dis_elf` command path rather than bypassing them. Contracts cover raw 16-bit/32-bit decoding, labels, 16-bit relative wraparound, unknown/invalid progress, and truncated tails across the active x87/SSE2-capable catalogue. A raw request has one mode, so mixed-mode flat images still need range extraction; completion also requires the remainder of the bounded ADR 0005 instruction domain and source annotations. |
| ELF32 executable inspection | Observed/Partial | The shared reader exposes checked static i386 `ET_EXEC` header and program-header views, including sectionless CupidC/CupidASM executables. CupidDis selects executable `PT_LOAD` contents, preserves absolute symbols when sections exist, and rejects malformed ranges/alignments. Dynamic symbol tables/relocations are diagnosed as unsupported until the typed model represents multiple symbol-table domains. Final executable layout remains CupidLD work. |
| ELF32 `ET_REL`, sections, symbols, and relocations | Observed | Hosted and in-process CupidDis reports serialized headers, sections, local/global/weak/undefined, TLS, and unnamed section symbols, and serialized-order relocations from the shared reader, disassembles every executable `PROGBITS` section, and overlays matching `R_386_32`/`R_386_PC32` fields without assigning relocation ownership to raw bytes. View-gated sorted indexes keep label traversal linear, make relocation lookup logarithmic, and never span caller callbacks. Positive, malformed, ordering, overflow, report-shape, exact-diagnostic, and early/late sink-failure contracts cover the typed seam. Native hosted contract objects remain deliberately classified separately from i386 OS objects. |
| DWARF v4 source information | Missing | No line/debug information parser or source annotation is present. |
| Host-runnable object inspector | Observed/Partial | `make -C toolchain all` builds a native `cupiddis` CLI on Windows and Linux. Static ELF input defaults to all implemented views; explicit header/section/symbol/relocation/disassembly views and an address-sorted GNU-`nm`-compatible mode are available. Raw input deliberately requires explicit 16/32-bit mode and base. Buffered stdout failures are detected at flush and produce a processing failure. The executable is still bootstrapped by the host C compiler and lacks dynamic-ELF, raw range/mode-map, and DWARF views. |
| Encoder/decoder parity tests | Observed/Partial | One catalogue now drives CupidDis plus both encoding directions. Checked manifests cover 182 active-ASM mode cases and 129 active-inline source-spelling/form rows; 153 additional focused/current-CupidC vectors exercise model boundaries. Every manifest vector decodes and re-encodes with its fingerprint-scoped form to identical bytes. Separate tests prove GNU's noncanonical `66 F3` string-prefix order decodes and canonicalizes to `F3 66`, and prove valid unsupported group/prefix/x87 spellings remain `UNKNOWN`. Negative and every-byte truncation cases cover illegal prefixes/groups/registers, reserved PSRLW memory encoding, range failures, and transactional output. CupidASM migration and a generated every-form exhaustive corpus remain. |

## Shared object, linker, and bootstrap capabilities

| Capability | Status | Baseline evidence and gap |
| --- | --- | --- |
| Platform-neutral job, arena, buffer, path, source, and diagnostics core | Observed/Partial | One freestanding implementation passes seven hosted public-contract tests and compiles into the i386 kernel through real libc and heap/VFS adapters. The DEBUG boot self-test exercises the public invocation against `/bin/ls.cc`, checked allocation/path/output behavior, VFS commit/cleanup, and missing-input translation. Existing tool frontends have not yet migrated onto the job interface. |
| Shared ELF32 object library | Observed/Partial | One freestanding two-operation module writes deterministic i386 `ET_REL` objects and reads bounded typed static `ET_REL`/`ET_EXEC` views. Public contracts cover named/unnamed sections, byte-aligned and zero-count tables, symbols, weak/common storage, alignments, program headers, sectionless executables, both required relocations, explicit/implicit addends, determinism, rollback, malformed input, and an explicit unsupported diagnostic for dynamic symbol tables; GNU `readelf`, LLVM `ld.lld`, Clang, and the real NASM ISR object passed interoperability oracles. Kernel round-trip and CupidDis adapter self-tests exercise the same interface. Multiple/dynamic symbol-table domains remain. |
| CupidLD | Missing | Host GNU `ld`/`ld.lld` owns layout, symbol resolution, relocations, and linker-script interpretation. |
| CupidObj | Missing | Host `objcopy` owns binary wrapping and raw extraction. |
| Shared x86 instruction model | Observed | One freestanding typed 16/32-bit seam and one private flat catalogue serve encoding and decoding: 546 rows (544 encodable forms plus two decode-only invalid recognizers), 226 canonical mnemonics, 64 registers, and fingerprint `3159218E`. Contracts cover deterministic shortest-form selection, exact-form replay, fields suitable for absolute/PC-relative relocation, mixed addressing, far/system/x87/SSE2 instructions, typed MMX-ready operands/registers, conservative invalid/unknown/truncated decoding, and transactional failures. CupidDis and its kernel self-test now consume the model; CupidC and CupidASM migration remains. |
| Windows and Linux checked seeds | Missing | No checked-in host-runnable Cupid Toolchain executables or provenance manifests exist. |
| Stage 2/stage 3 byte identity | Missing | No deterministic staged bootstrap harness exists. |
| Clean-checkout host independence | Missing | External compiler, NASM, linker, `objcopy`, and `nm` are all still required. |
