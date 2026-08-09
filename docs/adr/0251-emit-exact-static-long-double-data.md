# ADR 0251: Emit exact static long-double data

## Status

Accepted on 2026-08-09.

## Context

Hosted CupidC already parsed bounded finite normal decimal `long double`
literals and carried automatic values through the i386 x87 path. A static
`long double` leaf could only use implicit zero or an integer constant
expression equal to zero. A source literal such as `1.0L` still failed before
Linear IR, even though the compiler already owned its exact target bits.

This gap affected file-scope and block-static scalars as well as leaves inside
fixed arrays and complete records. Routing those values through host
`long double` would have made object bytes depend on the build platform.

## Decision

The semantic initializer record carries `floating_high_bits` beside its
existing 64-bit `integer_bits`. A static long-double leaf uses `integer_bits`
for the explicit significand and the low 16 bits of `floating_high_bits` for
the x87 sign and biased exponent. Every non-floating initializer and every
binary32 or binary64 initializer must keep the high field zero.

File-scope and block-static scalars, fixed arrays, and complete records accept
a bounded finite normal decimal `L` literal, parentheses, and unary plus or
minus. Positive and negative zero use the same path. Static arithmetic,
comparison, truth, conditional selection, floating-width conversion, and
nonzero integer initialization remain outside this slice.

Frontend freeze, Linear IR validation, and object emission each check their
own input boundary. High bits above the low 16 are invalid. Exponent zero
requires a zero significand. A finite nonzero value requires an exponent from
1 through 0x7ffe and the explicit integer bit. Exponent 0x7fff is reserved and
rejected. Keeping these checks at all three public boundaries lets malformed
or forged units fail before they publish partial output.

The i386 emitter writes the 64-bit significand at the leaf offset, the 16-bit
sign and exponent at offset eight, and two zero padding bytes. An all-zero
payload uses `.bss`. A mutable nonzero payload, including negative zero, uses
`.data`; a const nonzero payload uses `.rodata`. The ordinary symbol and
relocation policy remains unchanged.

## Evidence

The exact twelve-byte payloads include:

| Source | Significand | Sign and exponent |
| --- | --- | ---: |
| `1.0L` | `8000000000000000` | `3fff` |
| `1.0000000000000000001L` | `8000000000000001` | `3fff` |
| `18446744073709551615e0L` | `ffffffffffffffff` | `403e` |
| `-1.0L` | `8000000000000000` | `bfff` |
| `-0.0L` | `0000000000000000` | `8000` |

Frontend and Linear IR contracts cover scalar and aggregate initializers at
both scopes. Negative cases cover static computation, width and integer
conversion, atomic leaves, every malformed payload class, rollback, and
same-job recovery. Object contracts pin section placement, symbols, exact
bytes, zero padding, the absence of data relocations, deterministic repeated
emission, forged metadata rejection, and recovery. The Cupid-built hosted
i386 runtime reads all three words from scalar, array, and record payloads.

The first frontend run rejected `1.0L` at
`/long-double-locals.c:7:49` with `CTB000007`. The first object run exposed
the expected symbol inventory change from 19 to 25. After implementation,
the two focused frontend tests passed in 8.957 seconds, the two Linear IR
tests passed in 10.925 seconds, the two object tests passed in 18.799 seconds,
and the Cupid-built runtime test passed in 25.971 seconds.

## Rejected alternatives

Using the host's `long double` value or memory layout was rejected because
Windows and Linux hosts do not provide one stable i386 x87 representation.

Passing a static value through binary64 was rejected because it would discard
the extra x87 significand bits. The `1.0000000000000000001L` case catches
that loss.

Keeping validation only in the frontend was rejected because Linear IR and
the emitter are public boundaries that accept frozen or forged units in their
own contracts.

## Consequences

Compiler-head CupidC now emits deterministic nonzero and signed-zero static
long-double data without a host floating library. The capability applies to
scalars and aggregate leaves at file and block scope. It does not change a
normal source owner, so no `.c` to `.cc` rename is due.

Issue #25 remains open. Static long-double computation and conversion,
integer conversions involving `long double`, hexadecimal and subnormal
literals, atomics, aggregate ABI work, and other freestanding C11 gaps remain.
The checked seed predates this capability and needs a later verified seed
transition before normal production source can depend on it. `TempleOS/`
remains untouched reference material.
