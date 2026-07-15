# Lower CupidC leaf functions through typed linear IR

## Context

ADR 0003 calls for a shared linear IR between the typed CupidC frontend and machine-code emission. ADR 0014 publishes the typed function AST, ADR 0015 owns object construction, and ADR 0007 owns x86 encoding. The object path previously stopped whenever a translation unit contained a function definition.

Active source sets the first useful boundary. The unchanged `cemit_add_overflows` helper in `toolchain/cupidc_emit.c` has two 32-bit parameters, an unsigned subtraction and comparison, conditional selection, and a scalar return. Lowering that helper exercises real Toolchain source without rewriting it around a compiler limitation.

## Decision

CupidC exposes one public `ctool_c_lower_ir` operation. It borrows a frozen `ctool_c_translation_unit_t` and publishes immutable function and instruction arrays in the Toolchain job arena. Each function owns one contiguous instruction slice, its exact maximum abstract-stack depth, its binding and declared type, and both presumed and physical source locations. Instructions retain their result and input types, semantic operation or conversion, and source locations. Parameter-address instructions name the frontend's absolute parameter identity. Branches name an instruction relative to the current function.

The abstract stack keeps object addresses distinct from values. This preserves parameter object identity through lvalue conversion and prevents later code generation from treating every integer-shaped item as interchangeable. Each function begins and ends with an empty stack. Both incoming paths at a join must have the same address/value stack shape.

The first ABI slice accepts fixed, nonvariadic cdecl functions with `void` or 32-bit integer results and 32-bit integer parameters. A supported body is either an empty `void` compound statement or a compound statement containing one return. The expression slice covers parameter addresses and loads, 32-bit integer constants, represented implicit integer conversions that preserve the 32-bit value representation, addition, subtraction, signed or unsigned greater-than comparison, and 32-bit conditional selection. It emits value and void returns. An external-linkage definition that retains `inline` stops at a dedicated external-inline finalization diagnostic. Static inline definitions and a non-inline definition preceded by a compatible inline declaration can lower without crossing that deferred policy boundary.

Lowering runs once to count exact records and once to fill them. Failure zeros the public result, rewinds every allocation made by the operation, and keeps its structured diagnostic in the job. The diagnostic distinguishes invalid frozen input, unsupported types, statements, expressions, conversions, or ABI shapes, resource limits, and internal failures.

`ctool_c_emit_object` lowers the functions before it lays out the object. Nonempty `.text` precedes `.rodata`, `.data`, and `.bss`. Defined functions keep source order and receive local or global `STT_FUNC` symbols with exact section-relative offsets and sizes. Function alignment uses x86 NOP bytes.

Every machine instruction goes through `ctool_x86_encode`. The current cdecl frame uses `EBP`, reads parameter `n` at `[EBP + 8 + 4*n]`, and returns scalar values in `EAX`. Signed comparison uses `SETG`; unsigned comparison uses `SETA`. Conditional IR branches use fixed-width relative fields from the shared encoder and are patched to instruction offsets within the same function. They do not need ELF relocations.

Direct AST-to-x86 lowering was rejected because it would recreate the parser/backend coupling that ADR 0003 removes. X86-shaped IR was rejected because register and encoding facts belong to ADR 0007. A block-parameter or SSA graph may become useful for optimization later, but it would add predecessor and join machinery before active source requires it. The typed address/value stack is enough for this slice and keeps the public records straightforward to build with CupidC. Rewriting `cemit_add_overflows` into weaker source was not considered an acceptable workaround.

## Consequences and evidence

The IR contract extracts the exact unchanged helper body and pins its 12 instructions, three-entry maximum stack depth, parameter identities, types, conversions, branch targets, and source locations. It also pins the unchanged `add2` function in `bin/cupidc_test3.cc` as two parameter loads followed by `ADD` and a scalar return. Separate cases cover a constant return, an empty `void` return, a signed-to-unsigned assignment conversion, a static inline definition, and a non-inline definition after an inline declaration. Negative cases cover a malformed body, an unsupported `if` statement, unsupported multiplication, an explicit cast, plain and `extern` external-inline definitions, a 64-bit ABI, a constrained arena, rollback, and same-job recovery.

The object contract emits static data and represented functions in one object. It reads the object through `ctool_elf32_read`, decodes `.text` through `ctool_x86_decode`, and checks every machine byte as well as each mnemonic. Those byte oracles pin the EBP frame, EAX result, constants, parameter displacements 8 and 12, operand order, addition, and both signed and unsigned predicates. The contract also resolves both relative branch targets to the intended conditional arms. It checks section flags and alignment, function bindings and sizes, unchanged data bytes, byte-identical repeat output, external-inline and unsupported-body rollback, and output limits.

This transfers no production build ownership. GCC or Clang still builds the hosted modules and contracts, and the private in-kernel CupidC frontend and backend still produce every normal OS C object. No OS artifact or runtime ABI changed, so this increment has no boot claim.

This first decision left external-inline finalization, explicit casts, calls and `R_386_PC32`, local objects and stack allocation, assignments, general blocks and control statements, labels, pointers, 64-bit integers, floating and aggregate values, broader ABI work, production integration, and self-hosting open. Later extensions close only the parts they name. Issue #25 stays open until the remaining work can carry unchanged active Toolchain functions and then normal OS C cohorts.

## Extension

ADR 0017 extends this interface with fixed direct calls, source-order argument evaluation, cdecl stack-slot placement, and local or external `R_386_PC32` relocations. The open call work above records the boundary when this first leaf decision was made. Indirect and variadic calls, 16-byte call-site alignment, and wider, floating, or aggregate call forms remain open.

The active `add2` function later extended the existing `BINARY` instruction and emitter path with 32-bit integer addition. No public record changed. The emitter pops the right operand into ECX and the left operand into EAX, emits `ADD EAX, ECX` through the shared x86 encoder, and pushes the result for the following return. Multiplication remains a tested unsupported boundary.

ADR 0018 adds absolute block-binding identities, automatic-local addresses, initializer stores, and deterministic fixed EBP slots. It does not make the wider statement, assignment, pointer, or ABI surface complete.

ADR 0019 adds absolute linked file-binding identities through `FILE_ADDRESS`, reuses `LOAD` for four-byte integer objects, and extends `BINARY` with signed or unsigned greater-than-or-equal. The emitter maps direct object addresses to text `R_386_32` relocations without exposing x86 or ELF details in the IR.
