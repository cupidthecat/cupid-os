# ADR 0189: Preserve floating values in private CupidC unary signs

## Status

Accepted on 2026-07-30.

## Context

The private in-kernel CupidC compiler parsed unary minus through the integer
path. It always emitted `NEG EAX` and reported an `int` result, even when the
operand was a `float` or `double` held in `XMM0`. Unary plus was not accepted
by that parser branch.

This made ordinary Cupid mode source such as `-1.5` produce an unrelated
integer value. The embedded `feature13_double.cc` test avoided the gap by
writing `0.0 - 5.5`. Keeping that workaround would leave the compiler weaker
than the active source language.

Floating negation also has an exact IEEE-754 meaning. Subtraction from zero
does not preserve every NaN payload and does not express the required
signed-zero bit operation as directly as changing the sign bit.

## Decision

Private CupidC accepts unary plus and unary minus for `char`, `int`, `float`,
and `double` operands.

Integer operands continue through `EAX`. Unary minus emits `NEG EAX`, while
unary plus emits no target instruction. A `char` result receives the integer
promotion already used by the private expression path.

Floating operands remain in `XMM0`. Unary plus leaves the value unchanged.
Unary minus spills the scalar to an eight-byte stack slot, toggles the
IEEE-754 sign word with `XOR`, reloads the original width, and restores
`ESP`. The binary32 path changes bit 31 of the first word. The binary64 path
changes bit 31 of the second word. Every other payload bit stays intact.

Any other operand type fails with
`unary sign requires an arithmetic scalar operand`. A failed REPL evaluation
must leave the compiler ready for the next expression.

## Evidence

The focused guest test covers a negative `float`, a negative `double`,
negative binary32 zero, unary plus on a `double`, rejection of a string
operand, and a successful expression after that rejection. It reports:

```text
[feature13-unary] PASS float=-15 double=-9 zero=0x80000000 plus=9 reject=1 recovery=1
PASS feature13_double
[cupidc] JIT execution complete
```

The private compiler also prints the expected diagnostic for the rejected
operand. The GUI acceptance sequence now requires all of those markers and
rejects either unary-test failure marker.

Thirty kernel compile tests and 80 GUI terminal contract tests pass. The
normal image build also passes. The resulting `kernel/lang/cupidc_parse.o` is
292,728 bytes with SHA-256
`08a0d87c531e033b29adc77bdec5c63b75a18beac5c36b0fc154989a532e151c`.
The 8,488,252-byte kernel has SHA-256
`aa522e896d55656efc041b1395979474859f61dd3b2088d840325c5a3183212a`.

The complete four-vCPU e1000 frontier passes on a private copy of the clean
image. It covers all ten guest commands, six USB storage lifetimes, HID
reattachment, 69,548 changed pixels, 8,244,917 AC97 frames, and 72,034
PC-speaker frames. Its 79,254-byte serial log has SHA-256
`c4242b3b9ec17e0354c5d256a13de99ca3611d6a5f5c5e0f00ef55f1c48047d3`.

## Rejected alternatives

Keeping source-level `0.0 - value` workarounds was rejected because active
Cupid source should drive the compiler capability.

Sending floating operands through integer `NEG` was rejected because their
value is not in `EAX`.

Implementing negation as floating subtraction was rejected because toggling
the sign bit preserves signed zero, infinities, and NaN payload bits exactly.

## Consequences

Cupid mode source can use normal scalar unary signs without changing its
spelling for the private JIT or AOT compiler. The hosted self-hosting
frontend already represents these operators, so this closes one behavioral
gap between the two compiler paths.

Pointer, aggregate, vector, and other non-arithmetic operands remain invalid.
Runtime floating truth, increment, decrement, and the other documented Cupid
mode gaps are unchanged.
