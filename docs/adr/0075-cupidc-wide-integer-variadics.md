# Pass wide integers through variadic and unprototyped calls

- Status: Accepted
- Date: 2026-07-22

## Context

ADRs 0054 through 0056 added scalar variadic calls, a target `va_list` cursor, and calls through function types without prototypes. A call instruction retained its actual argument count, but the i386 emitter still inferred argument sizes from declared parameters. Values without declared parameter types were limited to represented four-byte integers and pointers.

ADRs 0065 through 0074 later gave signed and unsigned eight-byte integers one Linear IR handle backed by a private frame snapshot. Passing that handle through the four-byte undeclared-argument path would send the snapshot address instead of its contents. The caller also needed an eight-byte `va_arg` path before the same value could cross a variadic interface in both directions.

## Decision

Every `CALL_DIRECT` and `CALL_INDIRECT` instruction owns a source-ordered slice of its actual post-conversion argument types. `first_argument_type` selects the first entry in the unit's immutable `argument_types` array, and `argument_count` gives the slice length. The unit publishes the array and `argument_type_count`. A zero-argument call points at the current packed cursor. A non-call instruction uses `CTOOL_C_AST_NONE` and keeps an argument count of zero.

The lowerer records types after lvalue conversion, array or function decay, assignment conversion, integer promotion, or another supported default argument promotion has run. The frontend AST needs no duplicate table because each converted call child already retains its final type. The lowerer appends a call's slice after lowering all operands, so an inner call appears before its enclosing call in both instruction and type-slice order. Count-only validation uses separate type-slice state. The count and fill passes must agree before the result is published.

Named arguments still match their declared parameter types. An ellipsis position or a call through a function type without a prototype now accepts a non-atomic signed or unsigned eight-byte integer in addition to the represented four-byte integer and pointer forms. Signedness does not change transport.

Each argument remains one four-byte handle on the Linear IR semantic stack. The i386 emitter reads the actual type slice to size a shared outgoing area. A wide argument occupies eight consecutive bytes, with its low four-byte word followed by its high four-byte word. Direct and indirect calls use the same placement rule, and ESP remains aligned to a sixteen-byte boundary immediately before `CALL`. The saved callee for an indirect call stays below the evaluated argument handles until the outgoing area is ready.

`VARIADIC_ARGUMENT` keeps the requested value type on the instruction. A supported four-byte read advances the target `char *` cursor by four. A signed or unsigned eight-byte integer read copies eight bytes from the old cursor into an instruction-owned snapshot, advances the cursor by eight, and publishes one snapshot handle. A represented enum with an eight-byte compatible integer type follows the same rule.

Atomic cursor operations and atomic argument reads remain unsupported. Floating default promotion to `double`, floating transport and reads, and aggregate transport and reads also remain unsupported. These cases keep focused diagnostics instead of using an incorrect four-byte layout.

`ctool_c_ir_validate_call_slices` validates argument-type slices as one complete packed sequence before the emitter reads them. Missing entries, out-of-range types, gaps, overlaps, call metadata on a non-call instruction, and trailing unowned entries make the IR unit invalid. The validator borrows both units and changes neither one. Lowering and object emission remain transactional on their own malformed-input and resource-limit failures.

## Consequences and evidence

The frontend contracts cover signed and unsigned wide values at direct and indirect variadic calls and at direct and indirect unprototyped calls. They also cover signed, unsigned, and represented wide-enum `va_arg` expressions. Atomic and floating reads retain their focused failures.

The IR proof covers zero-argument, direct, indirect, unprototyped, and nested calls. It checks every packed post-conversion type, the call-owned slice boundaries, wide cursor-result types, deterministic repeat lowering, malformed input, allocation limits, and same-job recovery. Direct validator cases cover a missing array or entry, a gap, an overlap, an out-of-range span or type, a non-call owner, a trailing entry, and successful reuse after every rejection.

The deterministic ELF32 proof executes direct and indirect variadic calls, direct and indirect unprototyped calls, mixed four-byte and eight-byte argument sequences, signed reads, successive wide reads, and a copied wide cursor. A nested caller evaluates one wide variadic call inside another. Decoder and execution checks cover both nested call sites, call alignment, cdecl word order, cursor movement, preserved arguments, cleanup, the full returned value, and restored frame state. Constrained output leaves no partial object, and recovery reproduces the same bytes.

The verified hosted suites pass 51 frontend tests, 37 IR tests, and 31 object tests. The focused and neighboring variadic, unprototyped-call, wide-parameter, and wide-value modes are green.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler, its contracts, and every normal C object. No production object, build transform, boot path, host dependency, or ownership count changes.

Issue #25 remains open for atomic cursor and read semantics, floating and aggregate call forms, production integration, staged self-hosting, and the fixed-point bootstrap.

## Extension: floating scalar transport

ADR 0076 reuses the packed actual-type slices and eight-byte cursor machinery for values that are already `double`. It also adds `va_arg(double)`. The earlier floating gap remains accurate for default `float` promotion, `va_arg(float)`, floating computation and conversion, `long double`, SIMD, and atomic access.
