# ADR 0250: Convert hosted floating values to unsigned words

## Status

Accepted on 2026-08-09.

## Context

Hosted CupidC already converted represented integers to floating values,
floating values to signed or narrow integers, and an explicit `double` to
`unsigned long long`. Static evaluation also handled unsigned integer targets.
Runtime conversion from `float` or `double` to a four-byte unsigned integer
was still rejected by the frontend, Linear IR validator, and object emitter.

That rejection left a normal freestanding C11 scalar conversion outside the
shared self-hosting compiler. The existing double-to-unsigned-wide emitter
already contained a checked helper for one unsigned 32-bit truncation, so the
missing rule did not require a second conversion design.

## Decision

The hosted frontend accepts explicit casts and assignment conversion from a
non-atomic `float` or `double` to every represented non-atomic integer target
whose Cupid width is at most 32 bits. Existing signed and narrow conversion
rules remain unchanged. Runtime `long double` and atomic floating sources stay
outside this boundary.

Linear IR retains the ordinary typed conversion record. Its validator accepts
an unsigned target whose target layout occupies at most four bytes. The object
emitter applies its four-byte unsigned path to `unsigned int`, i386
`unsigned long`, compatible unsigned-layout enums, and typedef aliases.

The emitter widens a binary32 source to binary64 with `CVTSS2SD`. This widening
is exact. It then calls the existing unsigned-word helper used by the
double-to-unsigned-wide conversion. Values below 2^31 use `CVTTSD2SI`
directly. Values in the upper half subtract an exact binary64 2^31, truncate,
and restore bit 31. The result is pushed as one four-byte Linear IR value.

The language guarantee follows C: a conversion is defined only when the
integer part can be represented by the target. NaNs, infinities, and values
outside that interval receive no promised result.

## Evidence

Frontend and Linear IR contracts distinguish assignment conversion from an
explicit cast for both source widths. Atomic and long-double cases fail with
the existing expression diagnostic. Forged conversion metadata fails without
publishing a partial IR unit, and the same job recovers.

The object contract executes assignment and cast functions for both widths.
Its vectors include `-0.75`, zero, 2^31, the greatest binary32 value below
2^32, and the greatest binary64 value below 2^32. It also checks the shared
decoder's conversion instructions and deterministic repeated emission.

The three first red selectors failed in the frontend, IR, and emitter. The
same selectors passed after implementation in 39.173 seconds. The broader
floating selection passed all 18 tests in 40.126 seconds, and the related
pointer-expression contract passed in 9.485 seconds.

## Rejected alternatives

Sending an unsigned four-byte result through direct signed truncation would
mis-handle the complete interval from 2^31 through 2^32. The checked split is
already the authority for that target word.

Converting binary32 through host floating arithmetic would add a host
dependency and a second rounding surface. `CVTSS2SD` preserves every binary32
value exactly on the target.

Treating the feature as `unsigned int` syntax alone would reject i386
`unsigned long`, typedefs, and compatible enums that share the same target
representation. The validators use semantic width and layout instead.

## Consequences

Hosted CupidC now carries runtime `float` and `double` to unsigned four-byte
conversion through parsing, typed Linear IR, deterministic ELF32 emission,
and execution. The existing narrow, signed, Boolean, and explicit
double-to-unsigned-wide paths remain intact.

Issue #25 remains open. Static long-double computation and width conversion,
integer conversions involving `long double`, aggregate ABI work, and other
freestanding C11 gaps remain. No normal source changes build owner in this
step, so no `.c` to `.cc` rename is due. `TempleOS/` remains untouched
reference material.
