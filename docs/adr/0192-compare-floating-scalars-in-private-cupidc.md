# ADR 0192: Compare floating scalars in private CupidC

## Status

Accepted on 2026-07-30.

## Context

The private in-kernel CupidC compiler could add, subtract, multiply, and
divide scalar floating values, but it rejected all six comparison operators.
This left active Cupid mode source without ordinary `float` and `double`
relations. The browser interpreter worked around the gap by scaling
differences into integers, which loses information for small values.

The hosted self-hosting compiler already defines the required behavior.
Matching widths compare directly, a mixed pair compares as `double`, and a
comparison returns a normalized `int`. C also requires special handling for
NaN. Every ordered relation must be false when either operand is unordered,
except `!=`, which must be true.

## Decision

Private CupidC accepts `==`, `!=`, `<`, `>`, `<=`, and `>=` when either
operand is a scalar `float` or `double` and the other operand is an arithmetic
scalar.

The existing promotion path converts an integer operand to the floating width
of its peer and widens a mixed `float` and `double` pair to `double`. The
emitter keeps the left operand in XMM1 and the right operand in XMM0, then
uses `UCOMISS` or `UCOMISD`.

The result is normalized in EAX. Equality, less-than, and less-than-or-equal
combine their condition code with `setnp`, so an unordered value cannot look
ordered. Inequality combines `setne` with `setp`, so NaN remains unequal to
every value, including itself. Greater-than and greater-than-or-equal already
exclude unordered inputs through their carry conditions.

Pointers, aggregates, function pointers, and SIMD vectors are not arithmetic
scalar operands. Combining one of them with a floating operator fails with
`floating operator requires arithmetic scalar operands`.

## Evidence

The focused host oracle extracts the active comparison emitter from
`kernel/lang/cupidc_parse.cc`, compiles that exact code, and checks all twelve
binary32 and binary64 instruction sequences. Its small flag interpreter
covers ordered values, signed zero, subnormals, infinities, quiet NaNs, and
signaling NaNs. A separate compiled type oracle checks the four accepted
arithmetic types and rejects all nine non-arithmetic types. All three focused
tests pass.

The embedded `feature13_double.cc` test exercises all six operators, mixed
widths, signed zero, and a runtime NaN. It reports:

```text
[feature13-compare] PASS ordered=6 mixed=4 zero=2 unordered=6
PASS feature13_double
[cupidc] JIT execution complete
```

The GUI contract suite contains 92 passing tests and requires that exact
marker. Thirty checked-seed kernel compiler tests also pass.

A four-job Windows normal build completed in 540.4 seconds. It produced:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/lang/cupidc_parse.o` | 296,112 | `243aae2362739af67295c8020341141956936079934b9127f024f03afe739c0e` |
| `kernel/kernel.elf.pass1` | 8,588,100 | `9470498b0a68c511eda649fc49a70542df28e73f4741499ea9bebf2cb08cbc8e` |
| `kernel/kernel.elf` | 8,698,692 | `7411b3963e8495396c11e7219cbc285d4baa65951824c6f10f225d0790e64e01` |
| `kernel/kernel.bin` | 8,497,624 | `6c740e19dac92c02d9d6e44e60fa69e2a511184c97bce34cc7fa4e0cb1ef602b` |

The complete four-vCPU e1000 frontier passed in 233.8 seconds on a private
image copy. It completed all ten guest commands, six USB storage lifetimes,
HID reattachment, 69,549 changed pixels, 8,248,083 AC97 frames, and 77,857
PC-speaker frames.

## Rejected alternatives

Keeping the integer-scaling comparison helpers was rejected because they
discard floating precision and make active source carry a compiler
workaround.

Using `COMISS` or `COMISD` without an explicit unordered rule was rejected
because NaN would make equality, less-than, and less-than-or-equal produce
incorrect results.

Converting pointers or vectors through the integer-to-floating path was
rejected because those are not arithmetic operands in C or Cupid C.

## Consequences

Private JIT and AOT programs can use normal scalar floating comparisons. This
closes the comparison gap between the private compiler and the hosted
self-hosting path without changing build ownership.

Runtime floating truth, floating increment and decrement, SIMD comparison
operators outside the existing intrinsics, and the other documented Cupid
mode gaps remain open.
