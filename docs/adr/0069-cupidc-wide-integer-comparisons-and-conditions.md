# Lower wide integer comparisons and conditions through CupidC

## Context

ADRs 0065 through 0068 let hosted CupidC carry eight-byte integers through objects, calls, shifts, bitwise operations, and conversions. Ordinary control flow still stopped when one of those values became a condition or a comparison operand.

The shared preprocessor has unchanged helpers that expose the missing semantics. `pp_if_value_truth` tests all 64 bits, `pp_if_is_negative` reads the sign bit, and `pp_if_signed_less` compares signed eight-byte values. Rewriting those helpers into pairs of source-level words would move a compiler limit into active source and make later self-hosting harder.

## Decision

Hosted CupidC accepts signed and unsigned eight-byte operands for equality and relational comparisons. The result remains the target signed `int` type, so the existing Boolean and branch path consumes it without a new public IR instruction.

Equality compares both words. Relational comparisons inspect the high word first. Signed comparisons use a signed predicate for the high word, while unsigned comparisons use an unsigned predicate. When the high words are equal, both forms compare the low words as unsigned values. This covers `<`, `<=`, `>`, and `>=` without treating the low word as an independent signed integer.

An eight-byte scalar can also be used directly by logical not, logical AND, logical OR, a conditional expression, `if`, `while`, `do`, or `for`. Truth means that either word is nonzero. Logical AND and logical OR keep the existing short-circuit control flow, including source-order evaluation and skipped side effects. A conditional expression still publishes only the selected arm.

The emitter reads wide operands from their private frame snapshots. A comparison consumes two snapshot handles and pushes one normalized four-byte result. A direct condition or logical-not operation ORs the low and high words before testing zero. Branch joins, stack depth, and the EDX:EAX return boundary therefore keep the value model established by ADR 0065.

Switch dispatch was separate at this boundary because it needed a wide duplicate operation and its own malformed-IR and control-flow coverage. ADR 0071 later adds it. ADRs 0070, 0072, and 0073 fill the arithmetic path, and ADR 0074 adds mutation.

Lowering and object emission keep their transaction rules. Unsupported or malformed input publishes no partial unit or object, restores operation storage, and leaves the frontend translation unit unchanged.

## Consequences and evidence

The focused IR and object contracts cover all six comparison operators, signed and unsigned ordering, values that differ only in the low or high word, sign-boundary cases, high-word-only truth, logical not, short-circuit logical operators, conditional selection, and structured conditions. The execution oracle also checks restored stack and frame state, preserved callee-saved registers, unchanged arguments, repeat-object determinism, constrained-output rollback, and recovery in the same job.

Full-body source guards keep the active preprocessor helpers tied to the requirement. This capability removes their comparison and condition blocker, but it does not claim that the whole preprocessor is Cupid-built yet. Other missing arithmetic and hosted-runtime seams still prevent that ownership transfer.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler, the contracts, and every normal C object. No production transform or host dependency changes owner.

Issue #25 remains open. ADR 0070 later adds wide addition, subtraction, unary plus, unary minus, and bitwise complement. ADR 0071 reuses wide equality for full-width switch dispatch, ADR 0072 adds multiplication, ADR 0073 adds division and remainder, and ADR 0074 adds mutation. Values without declared parameter types, floating values, production integration, staged self-hosting, and the fixed-point bootstrap remain unfinished.
