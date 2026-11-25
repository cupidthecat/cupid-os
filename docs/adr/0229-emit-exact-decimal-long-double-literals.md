# ADR 0229: Emit exact decimal long-double literals

## Status

Accepted on 2026-08-04.

## Context

Hosted CupidC already carries automatic non-atomic `long double` values in
the i386 twelve-byte object format. It can load, store, convert, calculate,
compare, test, pass, and return those values, but source code could not create
a nonzero value with an `L`-suffixed decimal token. Tests had to widen a
`double`, which left a real C language gap in the self-hosted compiler.

The existing decimal converter uses bounded unsigned integer arithmetic. It
already produces exact target binary32 and binary64 bits without using the
host floating library. The same ratio is sufficient for a useful normal
x87 range while the parsed numerator and denominator fit in 64 bits.

## Decision

Extend the converter to accept bounded, finite, normal decimal `long double`
tokens. It rounds the exact decimal ratio to nearest with ties to even at a
64-bit explicit significand. The frontend stores that significand in
`integer_bits` and stores the positive token's biased x87 exponent in the new
`floating_high_bits` field. A leading minus remains an ordinary unary
expression, so the literal metadata never duplicates sign handling.

Linear IR preserves both fields on its floating-constant instruction. The
i386 emitter writes the low significand word, high significand word, and
biased exponent into one twelve-byte frame snapshot, then loads the value
with the shared 80-bit `FLD` form. The final two padding bytes are zero. Zero
uses an all-zero representation.

The frozen frontend, IR, and object boundaries reject exponent metadata on
other expression or instruction kinds. They also reject a nonzero
significand with exponent zero, an exponent above the finite normal range,
or a normal value without its explicit integer bit.

## Evidence

Positive contracts cover `1.0L`, `1.0000000000000000001L`, and
`18446744073709551615e0L`. Their target representations are respectively:

| Source | Significand | Biased exponent |
| --- | --- | ---: |
| `1.0L` | `8000000000000000` | `3fff` |
| `1.0000000000000000001L` | `8000000000000001` | `3fff` |
| `18446744073709551615e0L` | `ffffffffffffffff` | `403e` |

Each focused function is 48 text bytes. Their structure fingerprints are
`B6E00F15`, `4E64F77E`, and `4BE471B9`. Decoder checks find the three exact
constant words and one 80-bit load in every function.

The hosted i386 runtime inspects the precise and upper-significand values in
their twelve-byte object form, compares them with neighboring values, and
uses source literals for ordinary values and both signed zeros. A negative
contract rejects a decimal token beyond the current bounded precision, and
forged frontend and IR metadata fail without publishing an object.

## Rejected alternatives

Converting through host `long double` was rejected because its layout and
rounding depend on the build host. Converting through binary64 was rejected
because it would discard the extra x87 significand bits.

Treating a leading minus as part of the literal was rejected because the AST
already represents unary signs consistently at all floating widths.

A rounding-carry fixture was investigated. With the current unsigned 64-bit
numerator and power-of-ten denominator bound, no accepted decimal ratio can
round the maximum 64-bit significand upward. The defensive carry path remains
for a future wider decimal parser, but this commit does not claim an
unreachable test case.

## Consequences

Hosted CupidC source can now create exact normal x87 values with bounded
decimal `L` literals. Hexadecimal literals, subnormal literals, ratios that
exceed the current unsigned 64-bit parser bounds, nonzero or floating static
long-double initializers, and integer conversions involving `long double`
other than `_Bool` remain open.

The checked seed predates this capability. A later fixed-point transition
must carry it before checked-seed behavior tests or production source can
depend on it.
