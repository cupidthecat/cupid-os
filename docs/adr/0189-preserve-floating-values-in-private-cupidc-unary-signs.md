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

### Review hardening

The first acceptance gate removed every exact copy of the expected type
diagnostic from the full serial log before checking failures. That was too
broad. A stale copy before the feature command or a repeated copy elsewhere
could escape the compiler-error check.

The gate now permits one copy only after the
`[cupidc] JIT compile: /bin/feature13_double.cc` marker. Final-log validation
also requires that copy to sit inside one complete feature13 success match.
Missing context, stale text, and repeated diagnostics all fail with a focused
contract error. The live poll tolerates only a line-bounded trailing prefix
while QEMU is still writing that one diagnostic.

Red tests captured the stale, repeated, out-of-order, partial-write, and
embedded-line cases before the gate changed. The GUI terminal suite now has
90 passing tests. A separate two-test host oracle extracts the active byte
writers and five unary emitter functions from
`kernel/lang/cupidc_parse.cc`, compiles that exact C code, checks both byte
sequences, and interprets the emitted instruction subset. Its raw-bit cases
cover ordinary binary32 and binary64 values, signed zero in both directions,
infinities, quiet and signaling NaNs, and subnormals. The interpreter also
checks stack balance and canaries around the eight-byte scratch slot.

The rebuilt checked-seed root passed in 343 seconds. Its 8,490,736-byte raw
kernel has SHA-256
`00ec20c5aa19221ea89ddaf9e0fbdf98467f051b38d9bebb28931859cb16d9fe`,
and the 209,715,200-byte image has SHA-256
`448bb7eaf581cba55eb5b79d9fc2231cf8f8422bf095748bb1946cf0e981e7b7`.

That image passed the complete four-vCPU e1000 frontier in 242 seconds. The
94,323-byte serial log contains one feature13 compile marker, one permitted
diagnostic, one feature pass, no other CupidC error, and ten JIT completions.
It has SHA-256
`c91ec6438c176d2270d8f52df34d47ad60b3be891c3916d1cf9624b0609427e1`.
The run also completed six USB storage lifetimes, HID reattachment, 75,635
changed pixels, 8,189,087 AC97 frames, and 75,220 PC-speaker frames.

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
