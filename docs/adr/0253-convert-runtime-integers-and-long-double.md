# ADR 0253: Convert between runtime integers and long double

## Status

Accepted on 2026-08-09.

## Context

Hosted CupidC could transport, calculate, compare, and return twelve-byte
i386 `long double` values. It could not convert a runtime integer to or from
that type. Source casts, assignments, argument conversions, and returns
stopped in the frontend even though the shared x86 model already had the
required signed x87 integer memory forms.

The conversion must cover every i386 C integer width without routing through
host floating-point behavior. It must obey C's truncation-toward-zero rule
without leaving any change to the x87 precision, rounding, or exception
controls seen by surrounding code.

## Decision

The frontend and Linear IR accept non-atomic runtime conversions between
`long double` and signed or unsigned integer values at 8, 16, 32, and 64
bits. Explicit casts use `CTOOL_C_CONVERSION_NONE`. Initializers,
assignments, arguments, and returns use
`CTOOL_C_CONVERSION_ASSIGNMENT`. Conversion from `long double` to
`_Bool` keeps the existing floating truth path.

Integer input is presented to x87 as a signed 64-bit memory value. Values
through 32 bits are sign-extended or zero-extended first. Signed 64-bit values
use their existing two-word snapshot. An unsigned 64-bit value with its high
bit set is loaded as signed and corrected by adding exact `2^64`. The
emitter constructs that correction from signed `2^62` and two exact x87
doublings. It saves the caller's control word, selects 64-bit x87 precision
for this correction, and restores the saved word before storing the final
`long double`. The rounding-control bits are left alone because the
correction is exact at that precision.

Floating-to-integer conversion loads the source value into x87 and reserves
twelve scratch bytes. The emitter saves the x87 control word, selects
round-toward-zero, stores a signed 64-bit result with `FISTP`, and restores
the saved control word before publishing the value. Narrow results use the
existing target-width canonicalizer.

The unsigned 64-bit path compares the source with exact `2^63`. Values in
the upper half have `2^63` subtracted before the signed store, then receive
the high result bit. The comparison and subtraction use the same temporary
64-bit precision scope, which ends before the truncate-mode store saves its
own copy of the caller's control word. C leaves NaN, infinity, and
out-of-range conversions undefined, so the compiler does not define a
separate result for them.

CupidC now accepts the exact GNU assembly input `fldcw %0` with one
addressable non-atomic 16-bit integer `m` operand. Because it has no output,
GNU semantics mark it volatile even when the source omits the `volatile`
keyword. The frontend, Linear IR, and emitter share the same state-memory
input boundary with the existing
32-bit `ldmxcsr %0` form. This gives runtime contracts a supported way to
select a test control word and gives OS code a checked control-state input
form. Other `FLDCW` templates remain unsupported.

Every published twelve-byte automatic `long double` snapshot clears its
two padding bytes before the ten-byte x87 store. This keeps runtime
conversion results deterministic when a frame slot is reused.

Static long-double computation and integer conversion remain a separate
constant-evaluation boundary.

## Evidence

The public frontend and Linear IR conversion contracts cover signed casts
and unsigned assignments at 8, 16, 32, and 64 bits in both directions.
Atomic sources and destinations remain rejected. Forged
qualification and usual-arithmetic conversions fail at the IR and object
boundaries without publishing partial output.

The Cupid-built hosted i386 runtime checks exact x87 payloads for each
integer width, both signed 64-bit endpoints, truncation on both sides of zero,
and unsigned values on both sides of `2^63` through `UINT64_MAX`. Signed and
unsigned-wide enum values run through the same path in both directions.
Helper calls exercise the argument and return ABI. A 12-case control-word matrix
crosses the three valid x87 precision modes with nearest, down, up, and chop.
It checks upper and lower unsigned 64-bit conversions in both directions and
compares the complete control word after every conversion. A status-word
check at exactly `2^63` also distinguishes the upper-half branch from a
signed `FISTP` overflow alias.

Frontend, Linear IR, and object contracts cover `fldcw %0` alongside
`ldmxcsr %0`. They require the exact memory-input shape, preserve
qualified lvalues and one-time address evaluation, reject wrong widths,
register objects, bit fields, atomics, clobbers, outputs, and forged metadata,
and prove deterministic recovery. The object contract decodes the exact
16-bit `D9 /5` memory instruction.

The first frontend run stopped at the old long-double cast diagnostic. The
first Linear IR run rejected the same value type. The first runtime build
reached object emission and failed there. After emission was added, the
runtime initially exposed stale padding in an automatic twelve-byte
snapshot. Clearing the padding before `FSTP m80` made the result independent
of frame layout. A later control-word review found that the unsigned 64-bit
correction still used the caller's precision setting. The expanded runtime
matrix reproduced the fault at reduced precision. Scoping the exact
correction to 64-bit x87 precision fixed it without changing the caller's
rounding mode or saved control word. The first `fldcw %0` probe then stopped
at the old 32-bit-only assembly input rule; generalizing the state-memory
input seam fixed both the frontend and Linear IR failures.

## Rejected alternatives

Converting through binary64 was rejected because it would lose valid
long-double precision and could not represent every 64-bit integer exactly.

Using `FRNDINT` before an integer store was rejected because it still
requires a defined store path and does not remove the need to restore the
caller's rounding mode.

Leaving the x87 control word in truncate mode was rejected because it would
change later floating results in the same function.

Running the unsigned correction under the caller's x87 precision was
rejected because 24-bit or 53-bit precision rounds values such as
`UINT64_MAX` before the final 80-bit store. Rebuilding a default control word
was also rejected because it would discard caller-selected rounding,
exception masks, and reserved control state.

Defining NaN or out-of-range integer results was rejected because C does not
specify those conversions and the operating system has no separate policy
for them.

## Consequences

Compiler-head CupidC now owns runtime integer conversion for automatic
`long double` values across the complete i386 integer width set. The
checked seed predates this capability, so a later verified seed promotion is
still required before normal production source can depend on it.

No production source changes owner, and no `.c` to `.cc` rename is due.
Issue #25 remains open for static long-double computation and conversion,
aggregate ABI work, other C11 gaps, and staged self-hosting. `TempleOS/`
remains untouched reference material.
