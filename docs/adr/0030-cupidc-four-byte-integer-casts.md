# Lower four-byte integer casts through CupidC IR

## Context

The shared frontend retains explicit casts as typed `CAST` nodes. Active source needs that distinction in `dis_signed_bits` from `toolchain/cupiddis.c`:

```c
return -(ctool_i32)((~value) + 1u);
```

The hosted IR already lowers the complement, addition, and negation in this expression. It stopped at the cast from `ctool_u32` to `ctool_i32`. Rewriting the source would hide a normal C operation that CupidC must support. The complete function still needs general selection and multiple returns, so this increment isolates its final return expression.

## Decision

The hosted IR accepts an explicit cast when the source and destination are represented four-byte integer types. It checks that boundary before lowering the operand, lowers the operand once, then records a `CONVERT` instruction with the source type in `input_type`, the destination type in `type`, and `CTOOL_C_CONVERSION_NONE` in `conversion`.

The `NONE` tag distinguishes an explicit source cast from qualification, integer-promotion, usual-arithmetic, and assignment conversions inserted by the frontend. Erasing the cast during lowering was rejected because the following negation must see the destination type. Labeling it as an assignment conversion was rejected because that would misrepresent the source program.

The i386 emitter validates the same represented integer boundary and emits no instruction for the cast. Both values already occupy one 32-bit stack slot. Changing signedness changes how later operations interpret those bits, not the bits themselves. Truncation, sign extension, and zero extension would be redundant or incorrect for this same-width case.

Narrow, wide, floating, pointer, and `void` casts remain outside this slice.

## Consequences and evidence

The IR contract composes its fixture from the exact active expression and a reverse signed-to-unsigned cast. The two functions lower to 12 instructions. The first contains complement, addition, the explicit conversion, negation, and return. The reverse cast proves that the rule is not specific to one signedness direction.

A copied cast node carrying an assignment-conversion tag receives the invalid-unit diagnostic without changing the frozen frontend unit. Separate narrow and wide cast fixtures receive the unsupported-type diagnostic. A cast of a void-returning direct call to `void` receives the same public unsupported-type diagnostic without trying to pop a value that the call does not produce.

The deterministic ELF32 contract emits both functions in 52 text bytes. Their sizes are 35 and 17 bytes. The object has three symbols, no relocations, and decoded coverage for `NOT`, `ADD`, and `NEG`. The cast itself emits no target instruction. Repeated emission is byte-identical and leaves the frontend unit unchanged.

This is hosted bootstrap evidence. The private in-kernel compiler still produces every normal OS C object. No production artifact, build owner, host dependency, boot path, or runtime ABI changed.

Issue #25 remains open. General control flow, multiple returns, the other cast families, broader object and ABI work, production integration, and staged self-hosting still remain.
