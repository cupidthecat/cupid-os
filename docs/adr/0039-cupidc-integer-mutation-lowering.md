# Lower integer compound assignments and updates through CupidC IR

## Context

The shared CupidC frontend already represents every compound assignment and each prefix or postfix update. A mutation node keeps the raw destination designator, its stored and result type, and the arithmetic computation type. This is enough to preserve C semantics, but the hosted IR previously stopped at these nodes.

Active toolchain source needs the missing path. `toolchain/ctool.c` contains `size++`, `capacity *= 2u`, and `value /= 10u`. Rewriting those statements as plain assignments would hide a compiler requirement and make ordinary C harder to read.

Compound assignment must evaluate its destination once. It must also retain integer promotion, the usual arithmetic conversion when the operands require it, and assignment conversion back to the destination type. Prefix update produces the stored value. Postfix update stores the new value but produces the earlier value. A volatile destination must be loaded and stored once. Atomic mutation needs a separate ordering and target-instruction contract.

## Decision

When this decision was accepted, `ctool_c_lower_ir` accepted compound assignment and update when the destination, result, computation, and right operand fit the represented 32-bit integer slice. It accepted all ten C compound operators and all four prefix or postfix increment and decrement forms. ADR 0046 later applies the same promoted computation to represented, non-Boolean byte and word objects.

A new `DUPLICATE_ADDRESS` instruction duplicates the typed object address on the abstract stack. Compound assignment then loads one copy, applies integer promotion, applies the usual arithmetic conversion when the computation type differs, lowers the right operand, emits the ordinary binary instruction, converts the result back to the destination type, and stores through the saved address with `STORE_VALUE`. The raw destination designator is lowered once.

The left conversion may need two IR instructions. A signed enum first converts to its compatible signed integer type with `INTEGER_PROMOTION`, then converts to the unsigned common type with `USUAL_ARITHMETIC`. A signed `int` combined with an unsigned `int` needs only the second conversion. Shift assignment requires its computation type to equal the promoted left type.

Update lowering uses the same saved address and promotion rule. It combines the loaded value with a computation-typed constant one, converts the result back to the destination type, and stores it once. Prefix update leaves that stored value on the stack. Postfix update converts the stored result back to the computation type, applies the inverse operation with one, and converts the recovered prior value to the expression type. This does not load the object again. The rule is exact for represented unsigned arithmetic and for defined signed executions. Signed overflow remains governed by C's existing undefined-behavior rule.

Qualified volatile destinations keep their qualified type on the address path and their unqualified type on the value path. They emit one load and one store. Atomic destinations receive the existing unsupported-type diagnostic rather than an ordinary `MOV` sequence with unstated memory ordering.

The i386 emitter gives `DUPLICATE_ADDRESS` the same machine sequence as value duplication: `POP EAX`, `PUSH EAX`, and `PUSH EAX`. The separate public IR kind preserves the address/value distinction even though both represented values occupy one 32-bit machine word.

This decision did not add pointer arithmetic, bit-field writes, narrow or wide integer mutation, floating or aggregate mutation, or atomic ordering. ADR 0042 later adds pointer mutation, and ADR 0046 adds non-Boolean byte and word integer mutation. The hosted scope remains unchanged. The host C compiler still produces the normal root and user C objects. The private in-kernel CupidC compiler remains the embedded runtime JIT and AOT path.

## Consequences and evidence

The update contract covers prefix increment, prefix decrement, postfix increment, and postfix decrement in 32 exact IR instructions. The general compound contract covers all ten operators plus signed division and signed right shift in 102 exact instructions. Conversion contracts cover enum promotion, volatile access, mixed signed and unsigned operands, and a signed enum that needs both promotion and usual arithmetic conversion.

Active-source guards pin the three unchanged `ctool.c` statements. The original atomic, pointer, narrow, wide, and bit-field fixtures retained precise unsupported diagnostics. Later pointer and narrow decisions replaced those two negative cases with positive contracts while keeping Boolean, atomic, wide, and bit-field boundaries explicit. Mutated frozen units reject a missing update operator and an out-of-range computation type. Every failure leaves the public result empty, rewinds operation allocations, and preserves the input translation unit.

The deterministic mutation object contains four functions of 35, 45, 78, and 43 bytes. Its 201-byte text section covers prefix update, postfix update, multiplication assignment, logical right-shift assignment, and a file-object update. It has six symbols, one four-byte BSS object, and one `R_386_32` relocation at text offset 162. Repeated emission is byte-identical. CupidDis decodes the complete text and finds three additions, two subtractions, one multiplication, one logical right shift, and four returns.

The first update and compound contracts reached the public unsupported-expression diagnostic. The first object contract reached the emitter's internal failure for the new address-duplication instruction. A later mixed signedness contract showed that treating every left-side conversion as integer promotion was wrong. Splitting the promotion and usual arithmetic steps fixed that semantic record and also covers a signed enum before unsigned computation.

No active source was weakened or rewritten. This change transfers no production build ownership, removes no host dependency, changes no kernel artifact, and makes no boot claim.

Issue #25 remains open. Boolean and bit-field mutation, 64-bit integers, floating and aggregate values, atomic ordering, broader address forms and calls, production integration, staged self-hosting, and the final fixed-point bootstrap remain.
