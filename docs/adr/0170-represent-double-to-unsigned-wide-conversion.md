# ADR 0170: Represent double to unsigned wide conversion

## Status

Accepted on 2026-07-28.

## Context

The checked seed already emits the x87 round-down statement in
`str_floor()`. Compiler head then reached an explicit cast from `double` to
`uint64_t` at line 190 of unchanged `kernel/core/string.c`. The same
conversion appears twice more in `fmt_f()`.

CupidC could convert floating values to represented signed integers and to
unsigned byte or word integers. It could also carry `unsigned long long`
values, but no runtime conversion joined those two paths. Sending the cast
through a host compiler would leave an active source requirement outside the
Cupid object emitter.

## Decision

Compiler-head CupidC accepts an explicit, non-atomic cast from `double` to
the exact `unsigned long long` type. Linear IR keeps it as one typed
conversion from a binary64 value to an unsigned eight-byte snapshot.
Implicit assignment, `float` input, signed wide or enum output, atomic
operands, and other floating and wide combinations remain outside this
boundary.

The i386 emitter divides the input by 2^32 and truncates that quotient to the
high word. It reconstructs the quotient as an exact `double`, multiplies it
by 2^32, subtracts it from the original value, and truncates the remainder
to the low word. Each unsigned 32-bit truncation splits at 2^31 so
`CVTTSD2SI` never has to represent an out-of-range positive signed result.
The high word remains in EDX, the low word ends in EAX, and the ordinary
wide-value snapshot owns the result.

The conversion is required to match C whenever the truncated integral value
is representable. For finite binary64 input, that interval is `(-1, 2^64)`.
Inputs for which C leaves the conversion undefined promise neither a trap nor
a result.

## Evidence

Frontend contracts retain the explicit cast and reject `float`, atomic
`double`, an atomic target, implicit conversion, and a wide enum target.
Linear IR contracts check the exact source and destination types and
conversion metadata. They reject forged signed-wide and assignment
conversions, preserve the frozen unit after failure, and recover in the same
job. The object boundary repeats both metadata mutations before a successful
recovery.

The shared-decoder object contract checks the SSE2 instruction family and
runs the emitted code in the i386 state oracle. Cases cover zero, positive
and negative fractions, both sides of 2^32, 2^53 minus one, 2^63, the active
`1.8e19` guard, and the largest binary64 value below 2^64. Repeat emission
must be byte-identical.

Unchanged `kernel/core/string.c` now compiles twice to the same 14,460-byte
ELF32 object with SHA-256
`d48bb6ea18b7124fbefeaca0d5d5ee8a517db950f21ea88e30ededd6c5c2a577`.

## Rejected alternatives

Using x87 integer stores was rejected because their signed 64-bit result does
not cover values from 2^63 through the top of the unsigned range.

Returning only the low word was rejected because it would silently narrow a
valid C result.

Changing `fmt_f()` to avoid `uint64_t` was rejected because the active source
is valid C and should drive the compiler requirement.

## Consequences

Compiler head can emit the complete unchanged `kernel/core/string.c`
translation unit. The checked seed does not carry this conversion yet, so
the production recipe remains host-owned and the source keeps its `.c`
suffix. A checked-seed promotion and production handoff are separate steps.

No normal OS object, ABI, image, runtime path, or host-dependency count
changes. `TempleOS/` remains untouched reference material.
