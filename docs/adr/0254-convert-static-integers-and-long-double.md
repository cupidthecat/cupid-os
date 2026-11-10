# ADR 0254: Convert static integers and long double

## Status

Accepted on 2026-08-09.

## Context

Hosted CupidC could emit a bounded finite decimal `long double` literal as
exact i386 x87 data. It also converted integers and `long double` at runtime.
Static initialization still rejected every nonzero integer source and every
long-double-to-integer conversion. The gap applied to file objects,
block-static objects, and scalar leaves inside supported aggregates.

The constant evaluator must use Cupid's target representation. A host
`long double` is not a portable oracle because Windows and Linux hosts use
different formats and evaluation rules.

## Decision

The frontend accepts non-atomic static initializer conversion between
bounded finite `long double` values and every represented value integer.
The integer set is `_Bool`, plain `char`, the signed and unsigned 8, 16, 32,
and 64-bit types, and an enum whose compatible integer type has one of those
target layouts. The initializer keeps its exact destination type and
qualifiers.

Integer-to-long-double conversion packs the target x87 payload directly.
For a nonzero integer with magnitude `M`, let `w` be the number of significant
bits in `M`. The 64-bit explicit significand is `M << (64 - w)`. The biased
exponent is `0x3fff + w - 1`, and a negative source adds sign bit `0x8000`.
Every represented integer is exact because the x87 significand has 64 bits.
An integer-valued zero keeps a `CTOOL_C_INITIALIZER_ZERO` record instead of
being republished as a floating record.

Long-double-to-integer conversion first validates and decodes the existing
ten-byte x87 payload. For a finite nonzero payload, let `E` be the unbiased
exponent and `S` the 64-bit explicit significand. If `E` is negative, the
integral magnitude is zero. Otherwise the evaluator uses
`scale = E - 63`. A nonnegative scale produces `S << scale` after an overflow
check. A negative scale produces `S >> -scale`. This discards the fractional
part toward zero without host floating arithmetic.

For integer destinations other than `_Bool`, range checks run after
truncation. A signed `N`-bit destination accepts magnitudes through
`2^(N - 1) - 1` for positive values and through `2^(N - 1)` for negative
values. An unsigned destination accepts values through `2^N - 1`. A negative
source is valid only when truncation produced zero, so `-0.5L` converts to
unsigned zero while `-1.0L` is out of range. `_Bool` does not use the numeric
truncation path. It tests the original floating value: both signed zeros
become zero, and every represented finite nonzero value becomes one.

Linear IR now validates every published `CTOOL_C_INITIALIZER_INTEGER` leaf.
It unwraps the declared type to its base. Both wrapper and base must be
represented value integers with matching size, signedness, integer, object,
and completeness flags. A primitive base must have a recognized standard
integer kind with Cupid's canonical target size, signedness, and alignment.
An enum's compatible type must also have a recognized standard integer kind.
The enum, its unwrapped base, and its compatible type must agree on size,
signedness, integer, object, and completeness flags, as well as alignment.

A `QUALIFIED` node copies the referenced alignment unless it introduces
`_Atomic`. An atomic introduction raises the result to at least the target
atomic alignment. An `ALIGNED` node requires an explicit, nonzero power-of-two
alignment and may lower the referenced alignment. If that node also introduces
`_Atomic`, the atomic minimum still applies. `_Bool` has one payload bit;
every other accepted integer uses the full width fixed by its primitive or
enum compatible kind. Integer leaves must clear bits above that width and the
floating high word. They may not carry expression, string, address, or list
metadata. The same validator runs during whole-unit initializer ownership
checks and block-static declaration lowering, so a forged unit fails before
object emission.

Static long-double arithmetic, comparison, truth and logical operators,
conditional selection, and conversion between floating widths remain
unsupported. Hexadecimal and subnormal long-double constants, decimal ratios
beyond the bounded parser, and atomic static conversions also remain outside
this boundary. An explicit conversion to `_Bool` is supported; general
static long-double truth is not.

## Evidence

One shared fixture drives the frontend, Linear IR, and object contracts. It
covers both conversion directions for every represented integer kind, signed
and unsigned enums, const aggregate leaves at file scope, and two const block
statics. Its endpoint cases include `LLONG_MIN`, `LLONG_MAX`, `ULLONG_MAX`,
`2^63` to unsigned 64-bit, both signed zeros, and two conversions of `-0.5L`.
The negative fractional value becomes one for `_Bool` but zero for an unsigned
integer, which proves that Boolean truth is tested before numeric truncation.

The frontend oracle checks initializer kind, exact type and qualifiers,
integer bits, x87 significand and high word, aggregate shape, block-static
ownership, and recovery after each diagnostic. Range failures cover both
sides of a signed narrow type, an unsigned narrow type, both signed 64-bit
limits, negative unsigned input, and an exact `2^64` binary64 result built by
multiplication. Existing failures continue to hold the static arithmetic,
comparison, truth, conditional, floating-width, literal, and atomic
boundaries.

The first focused frontend run stopped at the old diagnostic that allowed
only a zero integer source. Later fixture corrections assigned each block
static to its declaration statement, removed a negative case for a qualified
integer rvalue that was valid C, and replaced an unreachable unsigned overflow
literal with a reachable lower-bound case. A later overflow check could not use
an oversized decimal token because the bounded literal parser rejects it
first. Multiplying two supported exact binary64 operands reaches the positive
64-bit overflow path instead. The implementation also preserved the
established `ZERO` initializer kind before the outer long-double assignment
conversion.

The first focused Linear IR hardening run exposed an accepted forged integer
initializer whose destination type was `double`. The first validator closed
that case, but an independent review found a deeper hole: a forged primitive
could claim integer layout while naming a noninteger kind, and forged enum
metadata could name the wrong compatible kind or representation. The hardened
validator now checks the target kind and representation rules above. A later
review found that the first hardening pass did not verify alignment through
qualified, aligned, atomic, and enum wrappers. The final validator applies the
alignment rules above. Focused contracts cover explicit lowered alignment,
explicit raised alignment, and atomic strengthening.

Object tests use the same fixture to check exact little-endian x87 and integer
bytes, zero x87 padding, section and symbol placement, deterministic repeated
emission, forged metadata rejection, and same-job recovery. Against the final
38-node, 34-edge fixture, the focused frontend selector passed one test in
8.634 seconds of runner time. The focused Linear IR selector passed one test
in 10.789 seconds of runner time. The focused object selector passed one test
in 17.985 seconds of runner time.

The final complete frontend module passed all 95 tests in 11.283 seconds, the
Linear IR module passed all 83 tests in 11.891 seconds, and the object module
passed all 109 tests in 940.772 seconds. Active-source audit regeneration and
its stale check passed in 60.616 and 60.939 seconds. The final digest is
`d155b419543faec5944ce066c1a29bdc614fe11d05f21871e9b246450d3b9e45`.

The complete Toolchain proof passed in 2,916.145 seconds. Checked-seed
bootstrap completed, stage two and stage three produced matching objects and
executables, the hosted runtime contract passed, the frozen inputs remained
unchanged, and all 20 artifacts were published and verified. The normal OS
build passed in 1,541.610 seconds and produced the kernel and disk image with
CupidASM, CupidC, CupidLD, and CupidObj.

A four-CPU e1000 boot smoke passed in 67.357 seconds. It reached all four CPUs,
the FPU and 62-check TLS milestones, network initialization, and the ordered
`feature13_double.cc` compile, PASS, and JIT completion markers. The 34,907-byte
log has SHA-256
`9dbec1c15604ce90e3760002ab2c1e23c642a51b398e9b1a28db2a26f99bbcab`.

## Rejected alternatives

Using the host's `long double` representation was rejected because it would
make constant data depend on the bootstrap host.

Converting through binary64 was rejected because binary64 cannot represent
every 64-bit integer exactly.

Checking the floating value against the destination range before truncation
was rejected because C tests representability after discarding the fractional
part. The defined unsigned interval therefore includes finite values between
negative one and zero.

Keeping integer initializer validation only in the frontend was rejected
because Linear IR and object emission accept frozen translation units at
public boundaries.

## Consequences

Compiler-head CupidC can now build exact static data for integer and
long-double conversions without host floating behavior or runtime work. ADR
0258 carries this capability in the checked seed.

No production source changes owner, and no `.c` to `.cc` rename is due.
Issue #25 remains open for static long-double arithmetic, comparison, truth and
logical operators, conditional selection, floating-width conversion, atomics,
other C11 gaps, and staged self-hosting. `TempleOS/` remains untouched
reference material.
