# Dispatch wide integer switches through CupidC

## Context

ADR 0038 added hosted switch lowering for promoted four-byte integers. ADRs 0065 through 0070 later gave signed and unsigned eight-byte integers one Linear IR handle backed by a private frame snapshot. A switch whose promoted condition remained eight bytes still stopped with the unsupported-type diagnostic.

The frontend already did the C work needed for a wider condition. It promotes the controlling expression, converts each folded case constant to that type, and checks duplicates after conversion. The missing boundary was in Linear IR and object emission. This increment does not have a complete active-source function that depends on a wide switch, so it removes a language blocker for issue #25 without claiming production ownership.

## Decision

Hosted CupidC accepts a switch condition when its promoted type is either a represented four-byte integer or a signed or unsigned eight-byte integer. Case values must have the same type as the condition. Unpromoted byte and word conditions remain malformed input to Linear IR because the frontend must apply integer promotion first.

The condition is evaluated once. `DUPLICATE_VALUE` copies its logical stack entry before each equality test. For a wide value, that entry is the 32-bit handle of an immutable eight-byte snapshot, not either data word. The existing wide equality path compares both words. Matching and default paths discard the saved handle before they jump to the resolved case body.

Object validation now treats value duplication and address duplication separately. `DUPLICATE_VALUE` accepts represented scalar values and wide integer snapshots. `DUPLICATE_ADDRESS` keeps its prior represented-scalar or complete-record object rule. Both instructions still emit the same handle duplication sequence, so this change adds no public word lanes, ABI state, relocation, or helper call.

Unreachable dispatch remains validation-only. The lowerer checks the condition and case types but publishes no comparison chain when the switch has no reachable source entry. Malformed type relations, unpromoted controls, and constrained output keep the existing transactional failure and recovery rules.

## Consequences and evidence

The focused IR proof has three functions and 46 instructions with fingerprint `CA2D36687BA73C9A`. The signed and unsigned functions each use 22 instructions and reach a maximum abstract stack depth of three. Each condition has one parameter address and one load, followed by two full-width case comparisons. An unreachable function publishes only its return path while still validating a `UINT64_MAX` case.

The deterministic ELF32 proof contains two 252-byte functions, for 504 bytes of `.text` with fingerprint `DBC82148`. It has three symbols including the null symbol and no relocations. The execution oracle covers signed positive and negative cases, unsigned positive and high-bit cases, misses that differ only in the low or high word, and both defaults. It also checks the stack, frame pointer, preserved registers, return sentinel, and unchanged two-word argument.

Negative contracts pass malformed translation units through the production lowerer. They reject a case whose type no longer matches the condition and an unpromoted narrow condition. A 64-byte object limit leaves the output empty. Repeated lowering and emission reproduce the same fingerprints, and a failed operation does not prevent the same job from succeeding afterward.

The first red run stopped at `/wide-switch.c:2:11` with the existing unsupported-type diagnostic. Extending the two integer predicates let the already-implemented switch and wide-equality paths meet without changing frontend semantics or the machine representation.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler, its contracts, and every normal C object. No production transform or host dependency changes owner.

Issue #25 remains open. ADR 0072 later adds wide multiplication, and ADR 0073 adds division and remainder. Mutation, values without declared parameter types, floating values, production integration, staged self-hosting, and the fixed-point bootstrap remain unfinished.
