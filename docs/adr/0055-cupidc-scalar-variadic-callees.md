# Lower scalar variadic callees through hosted CupidC

## Context

ADR 0054 gave hosted CupidC the caller side of scalar variadic calls, but a variadic definition could only use its named parameters. The exact Doom profile therefore stopped in the forced `kernel/doom/dglibc_compat.h` header at `typedef __builtin_va_list va_list;`. The active tree also uses `va_start`, `va_arg`, `va_copy`, and `va_end` through that header and in kernel logging code.

Cupid's current i386 ABI already fixes the useful first representation. Arguments are passed in cdecl stack slots, represented integers and pointers are four bytes, and the emitter knows the complete span of every named parameter. The compiler can expose that target rule directly without changing any active C source.

## Decision

In GNU C mode, `__builtin_va_list` is CupidC's target `char *` cursor. This is a structural C type rather than a nominal typedef, so aliases such as Doom's `va_list` keep ordinary C compatibility. Strict C mode rejects the builtin spelling with a focused extension diagnostic.

The frontend publishes four explicit expression kinds for `__builtin_va_start`, `__builtin_va_arg`, `__builtin_va_copy`, and `__builtin_va_end`. The destination cursor used by start, argument retrieval, copy, or end must be a modifiable `__builtin_va_list` lvalue. The source of a copy may be any `__builtin_va_list` lvalue. `va_start` is valid only inside a variadic definition, and its second operand must be that definition's final named parameter.

Linear IR keeps target-independent cursor operations for start, argument retrieval, and end. Copy uses the existing scalar `STORE`, because a Cupid i386 cursor is one pointer value. `VARIADIC_START` retains the absolute identity of the final named parameter. `VARIADIC_ARGUMENT` retains both the cursor-object type and the value type it loads. `VARIADIC_END` consumes the evaluated cursor address without producing a value.

The i386 emitter initializes a cursor to the first byte after the final named cdecl argument. It reuses the parameter-layout operation already tested by ordinary parameter access. As a result, it inherits that operation's rounded structure spans and hidden structure-result handling; this increment does not claim a separate cross-case oracle for those layouts. Each supported `va_arg` reads the current four-byte slot and advances the stored cursor by four. `va_copy` copies the cursor value. `va_end` evaluates its operand and otherwise emits no state change.

This slice accepts non-atomic pointers and the non-atomic four-byte `int`, `unsigned int`, `long`, and `unsigned long` types. Atomic cursors and atomic argument reads receive focused unsupported diagnostics. `double`, wider integers, and aggregate reads remain unsupported as well. Floating default promotion and non-scalar ellipsis transport also remain open on the caller side. No source is rewritten to hide those missing ABI paths.

Small i386 GCC and Clang probes both produced the same cursor model: `va_start` takes the address of the first unnamed stack slot, `va_copy` copies one pointer, `va_arg` advances by four before loading through the old pointer, and `va_end` leaves the stored cursor unchanged. CupidC follows that observed ABI while keeping the operations explicit in its public AST and IR.

## Consequences and evidence

The frontend contract covers the unchanged Doom compatibility header under the generated `DOOM_TREE_I386` profile. It checks the target `char *` alias, all four builtin expression records, every supported integer kind, a pointer read, a const copy source, and exact source locations. Negative cases cover strict mode, a nonvariadic function, the wrong final parameter, a non-cursor operand, a const destination, a non-lvalue copy source, atomic cursors and reads, a floating read, and the public nesting limit. The nesting case also checks rollback and same-job recovery.

The same exact profile now preprocesses the forced header and reaches `kernel/doom/src/d_main.c:405`. Parsing stops there on the existing old-style `void doomgeneric_Tick()` definition. This advances the measured Doom frontier without claiming that a complete Doom translation unit compiles.

The IR contract pins the start, scalar copy, argument, and end sequence, its maximum abstract stack depth, and transactional rejection of malformed start and argument records. Its frozen-unit negatives include a mismatched final-parameter type and noncanonical parameter metadata. The object contract emits twice and requires byte-identical ELF32 output with no relocations. Shared decoding finds exactly one positive EBP displacement, `16`, for the first unnamed argument after two named parameters. A decoder-driven i386 execution oracle reads a pointer, then reads the same unsigned-long slot independently through copied and original cursors. It returns `0x21426384` and leaves every incoming argument word unchanged.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler, and the normal Cupid OS build still uses the host C compiler for its C objects. No production owner, image ABI, or host dependency changes in this decision.

Issue #25 remains open. Old-style definitions, floating and wider runtime values, aggregate variadic values, the rest of the C and GNU surface, production integration, staged self-hosting, and the fixed-point bootstrap still remain.
