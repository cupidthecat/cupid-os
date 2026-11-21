# Lower object-pointer comparisons and conditions through CupidC

## Context

The hosted CupidC object path could carry four-byte object-pointer values, but it still stopped on the unchanged `ctool_job_arena` helper in `toolchain/ctool.c`:

```c
ctool_arena_t *ctool_job_arena(ctool_job_t *job) {
  return job != (ctool_job_t *)0 ? job->arena : (ctool_arena_t *)0;
}
```

This one expression needs a typed null cast, pointer inequality, pointer truth testing, a pointer-valued conditional expression, and an indirect member load. Rewriting it as integer arithmetic would hide the C semantics that the shared frontend already records.

## Decision

`ctool_c_lower_ir` accepts represented four-byte object pointers as scalar operands where C uses truth values. Unary logical not, short-circuit logical AND and OR, conditional-expression conditions, and the conditions of `if`, `while`, `do`, and `for` all retain the pointer input type on `BRANCH_ZERO`. Logical and comparison results remain plain signed `int`.

Pointer equality and relational expressions keep using `CTOOL_C_IR_INSTRUCTION_BINARY`. The IR checks compatible referents after removing the immediate referent qualifiers that do not affect comparison compatibility. Equality retains the frontend's permitted object-pointer and `void *` pairing. Relational validation also requires both referents to be object types, so a frozen unit cannot reinterpret valid `void *` equality as pointer order. The frontend continues to own null-pointer conversion, and the i386 emitter uses unsigned predicates for valid pointer order. Equality and inequality use the existing sign-independent predicates.

An explicit cast may now cross any represented four-byte integer or object-pointer pair. The conversion keeps its source and destination type identities in IR and emits no machine instruction because every supported value is already one i386 word. This covers integer to pointer, pointer to integer, and object-pointer to object-pointer casts. Function pointers, wider integers, and non-four-byte values still fail closed.

A pointer-valued conditional expression may receive compatible arm types with different graph identities. Lowering validates each arm against the frontend's composite result type, then records that composite type at the abstract-stack join. No target conversion is needed because both arms have the same four-byte representation.

Atomic pointer objects remain unsupported. Reading one still needs an explicit ordering and target-instruction contract. Pointer arithmetic, subscripts, pointer mutation, decay, and indirect calls also remain outside this decision.

Adding pointer-specific comparison or branch instructions was rejected. The existing `BINARY`, `UNARY`, and `BRANCH_ZERO` records already carry the input type and operation needed to preserve the semantics. Converting every pointer to an integer before lowering was also rejected because it would erase useful type checks from the public IR.

## Consequences and evidence

The active helper and two focused comparison functions publish 27 exact IR instructions. Three explicit cast functions add twelve exact instructions for pointer to integer, object pointer to `void *`, and `void *` to object pointer. The deterministic comparison object has six functions, 198 text bytes, seven symbols including the null symbol, and no relocations. Shared x86 decoding finds the expected `CMP`, `SETE`, `SETNE`, `SETB`, and return instructions.

Eight focused condition functions exercise logical not, both short-circuit operators, conditional selection, and every supported statement condition. Their 62 exact IR instructions pin all function slices, types, conversions, values, branch targets, and source locations. Their object has 372 exact text bytes, nine symbols including the null symbol, no relocations, eleven decoded `TEST` instructions, one `SETE`, and nine returns. Repeated emission is byte-identical for both objects.

Negative contracts reject a frozen `void *` equality expression changed to pointer order as malformed input. They keep pointer arithmetic, casts involving a wide integer, and atomic pointer loads at the unsupported-type boundary. Each failure preserves the frozen translation unit, publishes no partial IR, rewinds temporary allocations, and leaves the job usable.

This is hosted bootstrap evidence. GCC or Clang still builds the shared frontend, IR, emitter, x86, ELF32, and contract modules. No normal Cupid OS C object uses this path yet, so this decision transfers no production ownership and carries no boot claim.
