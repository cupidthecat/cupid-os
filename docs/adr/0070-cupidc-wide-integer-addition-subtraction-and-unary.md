# Lower wide integer addition, subtraction, and unary operations through CupidC

## Context

ADRs 0065 through 0069 let hosted CupidC carry eight-byte integers through objects, calls, shifts, bitwise binary operations, conversions, comparisons, and conditions. Ordinary addition, subtraction, and the nonlogical unary operators still stopped in Linear IR.

Active toolchain source needs this work. `pp_if_signed_magnitude` computes a two's-complement magnitude with `(~bits) + 1ull`. CupidASM's unary expression branch preserves a positive value, subtracts a negative operand from zero, or complements both words. Rewriting either path as source-level word arithmetic would hide a compiler limit in the tools CupidC must eventually build.

## Decision

Hosted CupidC accepts signed and unsigned eight-byte operands for binary addition and subtraction. The operands must already have the same type after the usual arithmetic conversions. The i386 emitter adds the low words with `ADD` and the high words with `ADC`. Subtraction uses `SUB` followed by `SBB`. Each result receives a fresh eight-byte frame snapshot.

Unary plus keeps the existing immutable snapshot handle. Unary minus loads both words, negates the low word, carries the low-word borrow into the high word, and negates the adjusted high word. Bitwise complement applies `NOT` to both words and writes a fresh snapshot. Logical not keeps the two-word truth path from ADR 0069.

The snapshot rule matters for nested expressions. A result cannot overwrite either operand because another use of that operand may still be live. Linear IR continues to count one C value as one stack item and does not expose EDX:EAX or two public word lanes.

Signed operations follow C's existing rule that overflow is undefined. Unsigned addition, subtraction, negation, and complement wrap modulo 2^64. Division, remainder, wide compound assignment, and increment and decrement were separate work at this boundary. ADR 0071 later adds wide switch dispatch, ADR 0072 adds multiplication, ADR 0073 adds division and remainder, and ADR 0074 adds mutation.

Lowering and object emission keep their transaction rules. Malformed operation metadata or a constrained output buffer publishes no partial unit or object, rewinds temporary storage, and leaves the frontend translation unit unchanged.

## Consequences and evidence

The focused arithmetic IR has 14 functions and exactly 83 instructions with fingerprint `245E6D8F4F77588E`. The combined operation object has 3,156 bytes of `.text`, fingerprint `B52392EA`, 26 symbols including the null symbol, and no relocations.

The execution oracle covers low-word carry, high-word borrow, full-width unsigned wrap, signed non-overflowing results, zero and sign-boundary negation, double-negation and double-complement identities, and nested uses that would expose an overwritten operand. It also checks stack restoration, frame restoration, preserved registers, and unchanged arguments.

Full-body source guards bind the preprocessor magnitude helper to its exact implementation. A second guard binds CupidASM's unchanged unary expression branch. The active magnitude helper also executes in the object oracle for positive, negative-one, and signed-boundary inputs.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler, the contracts, and every normal C object. No production transform or host dependency changes owner.

Issue #25 remains open. ADR 0072 later adds wide multiplication, ADR 0073 adds division and remainder, and ADR 0074 adds mutation. Values without declared parameter types, floating values, production integration, staged self-hosting, and the fixed-point bootstrap remain unfinished.
