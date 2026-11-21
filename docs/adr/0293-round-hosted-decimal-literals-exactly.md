# ADR 0293: Round hosted decimal literals exactly

## Status

Accepted on 2026-08-15.

## Context

Hosted CupidC converted decimal floating constants with a 64-bit numerator,
a 64-bit denominator, and a 128-bit division helper. That path rounded normal
`float` and `double` values correctly when the decimal ratio fit its small
workspace. It rejected longer spellings, every decimal subnormal, underflow,
and overflow before the frontend could publish the target value.

Those failures were compiler limits rather than C or i386 limits. The private
in-kernel CupidC lexer already had a tested integer-only converter for the same
formats. It uses a fixed 1536-bit workspace, forms the decimal ratio exactly,
and rounds once at the requested IEEE width. Keeping a weaker second algorithm
in the hosted frontend made the two CupidC paths disagree on ordinary source.

Hosted `long double` has a separate bounded decimal path. It publishes an x87
explicit significand and exponent and supports only finite normal literals.
Hexadecimal floating constants also remain outside the hosted decimal grammar.
Neither boundary needed to move for this change.

## Decision

Use a private 48-limb unsigned integer workspace for hosted decimal `float`
and `double` conversion. Each limb is 32 bits. The 1536-bit capacity covers a
complete 95-character token at the binary64 subnormal boundary, including the
shift needed for final rounding.

Build the exact decimal numerator and denominator, determine the binary
exponent, then divide after applying the target binary scale. Compare twice
the remainder with the denominator and round to nearest with ties to even.
An `f` or `F` suffix selects binary32 before conversion. An unsuffixed token
selects binary64.

Publish normal values, subnormals, and finite limits as their raw target bits.
Overflow produces infinity. Underflow produces positive zero, and the existing
unary operator path supplies negative zero when the source has a leading
minus. A zero significand remains zero even when its written exponent is far
outside the finite range.

Accept at most 95 characters in a hosted decimal binary32 or binary64 token,
including its suffix. A longer token receives a focused diagnostic. Decimal
exponents saturate internally at 10,000 so extreme overflow and underflow can
be classified without overflowing parser arithmetic or constructing an
unnecessary power of ten.

Keep the existing bounded `long double` converter. Its precision and scale
diagnostics continue to describe the x87 literal boundary. Keep the existing
hexadecimal floating diagnostic at the shared entry point.

The public frontend, Linear IR, and object contracts share one source fixture.
The frontend checks typed initializer bits. Linear IR validates the same frozen
initializer forest and emits no runtime work for it. The object contract checks
the exact little-endian `.rodata` bytes and the retained twelve-byte x87
control value.

## Evidence

The first frontend run failed on
`1.000000059604644775390625f` with
`decimal floating constant exceeds the supported precision`. The converter
changed only after that public failure was captured.

The shared fixture checks both parity directions at binary32 and binary64
halfway points, the minimum subnormal, the minimum normal, the largest finite
value, overflow to infinity, positive and negative underflow zero, an extreme
exponent on zero, and an accepted 95-character token. A 96-character token
fails with its exact diagnostic. The same job then revalidates the earlier
translation unit. A decimal `1.0L` control keeps the x87 path visible, while
the existing hexadecimal and bounded long-double failures retain their former
boundaries.

The three focused frontend, Linear IR, and object tests pass together in
45.070 seconds. The complete hosted frontend suite passes 97 tests in 12.956
seconds. The complete Linear IR suite passes 86 tests in 13.195 seconds. The
focused object test passes in 22.086 seconds and checks byte-identical repeat
emission.

The complete static fixed-point test passes in 857.279 seconds. All 44
Toolchain contract-plan tests pass in 6.310 seconds after their live input
count moves from 65 to 66 for the shared fixture. Final audit regeneration
passes in 73.7 seconds, and a fresh checked comparison passes in 72.7 seconds.
The generated audit records 737 active inputs, 297 headers, and 23 Toolchain
contract files. The bootstrap log records the red-to-green details.

## Rejected alternatives

Calling `strtod`, compiler builtins, or a host math library was rejected. Host
floating conversion is not part of the i386 object contract and would add a
second authority for rounding.

Parsing every token as binary64 and narrowing an `f` literal was rejected.
Two rounding steps can choose a different binary32 value from direct binary32
rounding.

Keeping the 64-bit ratio and adding special cases for subnormals was rejected.
That would leave long decimal spellings unsupported and would duplicate the
hardest rounding logic in another bounded representation.

An allocator-backed arbitrary-precision integer was rejected for this slice.
The accepted token length is fixed, so a checked fixed-size workspace gives a
clear capacity and no new allocation failure path.

Changing active OS source to shorter literal spellings was rejected because it
would hide a compiler limitation rather than improve CupidC.

## Consequences

Source-head hosted CupidC now gives decimal `float` and `double` literals the
same target-width rounding model as private CupidC. Static and runtime source
can use decimal subnormals, finite boundaries, overflow, underflow, and signed
zero without host floating arithmetic.

The checked execution seed predates this source-head change. No active
translation changes owner, and no normal OS artifact, ABI, build dependency,
or source suffix changes here. A later seed promotion must carry the hosted
compiler and contracts before the checked production toolchain can claim this
capability.

Hexadecimal floating constants, hexadecimal or subnormal `long double`
literals, and long-double decimal ratios beyond the existing bounded parser
remain open. `TempleOS/` remains untouched reference material.
