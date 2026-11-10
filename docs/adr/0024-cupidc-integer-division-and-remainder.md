# Lower 32-bit integer division and remainder through CupidC IR

## Context

The hosted CupidC path reaches this unchanged overflow helper in `toolchain/cupidc_emit.c`:

```c
static ctool_bool cemit_multiply_overflows(ctool_u32 left,
                                            ctool_u32 right) {
  return left != 0u && right > 0xffffffffu / left ? CTOOL_TRUE
                                                  : CTOOL_FALSE;
}
```

The frontend already types its unsigned division correctly, and ADR 0023 already lowers the surrounding comparison, conditional expression, and short-circuit logical AND. `ctool_c_lower_ir` still stopped at `/`, which kept this part of CupidC's own emitter outside the shared object path. The same i386 operation also provides the C remainder operator, so both results belong in one capability decision.

## Decision

The existing `CTOOL_C_IR_INSTRUCTION_BINARY` record carries `CTOOL_C_EXPRESSION_OPERATOR_DIVIDE` and `CTOOL_C_EXPRESSION_OPERATOR_REMAINDER` for represented four-byte integers. The semantic operation and converted input type stay in IR. Register selection and signed instruction choice remain private to the i386 emitter.

Emission pops the right operand into ECX and the left operand into EAX. Signed operands use `CDQ` followed by `IDIV ECX`. Unsigned operands clear EDX with `XOR EDX, EDX` and then use `DIV ECX`. Division pushes the quotient from EAX. Remainder pushes the remainder from EDX. This follows the existing abstract-stack convention and does not change the public IR shape.

The emitter adds no divide-by-zero check and no signed overflow check for the minimum signed integer divided by negative one. C leaves both cases undefined, and the target instruction's fault behavior is a valid result of executing them. Adding compiler-defined recovery would create semantics that the language does not promise.

A separate quotient or remainder instruction was rejected because `BINARY` already retains the exact C operator and input type needed by code generation. Host runtime helpers were also rejected because i386 provides the required 32-bit operations directly and a helper call would add an unnecessary ABI dependency.

## Consequences and evidence

The source guard pins the complete unchanged `cemit_multiply_overflows` helper. It lowers to 21 instructions with a maximum abstract-stack depth of three. The division remains an unsigned semantic `BINARY` operation inside the short-circuit right branch.

The object contract covers four independent functions: signed quotient, signed remainder, unsigned quotient, and unsigned remainder. The signed functions are 34 bytes each and decode through `CDQ` and `IDIV`. The unsigned functions are 35 bytes each and decode through `XOR EDX, EDX` and `DIV`. Their object has 138 text bytes, five symbols, no relocations, and byte-identical repeat emission.

Separate 64-bit division and remainder fixtures receive the unsupported-type diagnostic. Left shift now serves as the unsupported four-byte binary-operation boundary, so successful division cannot hide a generic acceptance bug. Failure still publishes no partial IR or object and preserves rollback and same-job recovery.

This is hosted bootstrap evidence only. GCC or Clang still builds the shared frontend, IR, emitter, x86, and ELF32 modules and their contracts. The host C compiler still produces the normal root and user C objects. The private in-kernel CupidC path remains the embedded runtime JIT and AOT path. No production artifact, ABI owner, build transform, host dependency, boot path, or runtime behavior changes here.

Later decisions close several frontiers that were open here. ADRs 0063 and 0064 add assignment and mutation for represented bit fields in four-byte storage units. Issue #25 remains open for non-four-byte field storage, partial volatile mutation, atomic ordering, broader values, production integration, and staged self-hosting.
