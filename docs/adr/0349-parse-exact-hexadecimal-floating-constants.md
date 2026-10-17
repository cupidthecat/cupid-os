# ADR 0349: Parse exact hexadecimal floating constants

## Status

Accepted on 2026-08-25.

## Context

Hosted CupidC accepted exact decimal floating constants but rejected C99
hexadecimal forms such as `0x1.8p+1`. Those forms are common in numeric and
vendored C because the written significand maps directly to a binary value.
Rejecting them remained a real language gap even though the active normal
build did not currently depend on one.

## Decision

Parse bounded `0x` or `0X` significands with an optional point, a required
binary `p` exponent, and the standard `f`, `F`, `l`, or `L` suffix. Convert the
value with target-only integer arithmetic. Do not call host floating-point
parsers or use host floating values as a production oracle.

Round once to nearest with ties to even for binary32, binary64, and the i386
x87 80-bit payload. Preserve exact normal and subnormal values, signed zero,
the largest finite values, overflow to the represented infinity, and
underflow to zero. Reject missing digits, missing exponent fields, repeated
points, invalid digits or suffixes, and tokens beyond the existing bounded
conversion capacity.

Carry the exact payload through the shared frontend, Linear IR, and ELF32
object emitter.

## Evidence

One shared fixture covers ordinary values, halfway cases on both sides of a
tie, minimum subnormals, normal boundaries, finite maxima, overflow,
underflow, suffixes, and signed zero for all three target formats. The
frontend focused test passes, the complete IR suite passes 86 tests, and the
focused object contract passes. Malformed cases fail with specific diagnostics
and a later compilation recovers.

The larger frontend and object lanes remain part of the consolidated
toolchain gate because concurrent source changes alter their exact audit
inventories.

## Consequences

CupidC can represent C99 hexadecimal floating constants without borrowing the
host's floating-point semantics. This expands the accepted C source language
but moves no production object and does not promote a compiler seed.

All active CupidC-owned translation units already use `.cc`; this capability
does not make any remaining `.c` file eligible for a suffix-only rename.
