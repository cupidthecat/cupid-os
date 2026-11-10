# ADR 0256: Accept canonical static x87 payloads

## Status

Accepted on 2026-08-10.

## Context

Hosted CupidC already represented static `long double` values as one 64-bit
significand and one 16-bit sign and exponent word. Its static evaluator could
compare finite values, test truth, and convert finite values to and from
`float` and `double`. Three validation boundaries rejected every x87 infinity,
NaN, and subnormal payload. Widening a binary32 or binary64 infinity or NaN
therefore failed even though the source value and its target representation
were known.

Static long-double arithmetic also needs a closed representation for overflow,
underflow, division by zero, and invalid operations. Adding arithmetic while
special results remained invalid would create temporary cases that the later
IR or object writer could not carry.

## Decision

Use one canonical x87 class rule in the frontend, Linear IR, and object
emitter. A private header-local decoder owns validation, classification, and
subnormal normalization, so all three boundaries apply the same code. An
exponent of zero requires a clear explicit integer bit. A zero significand
represents signed zero, and a nonzero significand represents a subnormal.
Exponents from 1 through `0x7ffe` require the explicit integer bit. Exponent
`0x7fff` also requires that bit. The significand
`0x8000000000000000` represents infinity at that exponent, and every other
accepted significand represents NaN.

Reject exponent-zero encodings with the explicit integer bit, nonzero
exponents without that bit, and metadata above the low 16 bits. These are
pseudo-denormal, unnormal or pseudo-special encodings, not alternate forms of
the canonical classes.

Normalize an accepted x87 subnormal in the target-only decoder before truth,
comparison, or width conversion. Keep the represented sign and exponent in
the original initializer and object payload.

Widen binary32 and binary64 infinities to the matching signed x87 infinity.
Widen any binary32 or binary64 NaN to the canonical quiet x87 NaN with
significand `0xc000000000000000`. Narrow x87 infinity and NaN through the
existing binary32 and binary64 packer. The packer emits the target infinity or
canonical quiet NaN. Signed zeros and finite conversions keep their existing
bits.

The static evaluator handles truth and comparisons over every accepted class.
Both infinities and NaNs are true. Infinity participates in ordinary ordering.
A NaN comparison is unordered, so only `!=` is true. Folded results become
final initializer records and add no runtime IR.

The object emitter accepts the same payload rule for static data and frozen
floating IR instructions. A twelve-byte object contains the ten x87 value
bytes followed by two zero padding bytes.

## Evidence

The shared static-control fixture widens binary32 and binary64 positive
infinity, negative infinity, and NaN. It checks truth, infinity ordering, all
six unordered relations, and exact narrowing to binary32 and binary64. The
frontend and IR contracts require final initializer records with no runtime
function or instruction.

The frontend contract calls the shared decoder for raw payloads that public C
syntax cannot produce in this slice. The minimum positive and negative
subnormals normalize to exponent `-16445` and significand
`0x8000000000000000`. The largest subnormal normalizes to exponent `-16383`
and significand `0xfffffffffffffffe`. Pseudo-denormal and pseudo-special
payloads fail the same call.

The object contract checks exact x87 words, canonical quiet NaN output, section
and symbol layout, zero padding, deterministic repeated emission, constrained
output rollback, and same-job recovery. Frozen-unit mutations accept a
canonical x87 subnormal and reject pseudo-denormal, unnormal, pseudo-special,
and high-metadata encodings.

The complete hosted object module passes all 109 tests after the three changed
self-host object inventories are relocked to the reviewed source.

## Rejected alternatives

Keeping a finite-only payload validator was rejected. It would force static
arithmetic to invent a separate error for each valid special or underflow
result.

Using the host `long double` layout or host floating operations was rejected.
The bootstrap compiler must keep the i386 target result independent of the
machine that runs the compiler.

Preserving each source NaN payload during width conversion was not selected
for this boundary. A canonical quiet NaN gives the static evaluator and object
tests one stable target result while preserving C unordered behavior.

## Consequences

Compiler-head CupidC can carry every canonical x87 class through static data,
truth, comparison, and floating-width conversion. Static long-double
arithmetic remains a separate step, but it can now produce special and gradual
underflow results without crossing an invalid representation.

ADR 0258 carries this capability in the checked seed. No production source
changes owner, no host dependency is added, and no `.c` to `.cc` rename is
due. `TempleOS/` remains read-only reference material.
