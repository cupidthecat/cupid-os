# ADR 0249: Complete private unsigned-word conversion and remainder assignment

## Status

Accepted on 2026-08-09.

## Context

ADR 0221 gave the private in-kernel compiler a distinct unsigned 32-bit type.
Objects, calls, comparisons, division, remainder, shifts, and conversion from
an unsigned word to `float` or `double` kept the correct i386 behavior. Two
ordinary C operations were still missing. A floating value could not convert
back to an unsigned word, and the lexer did not recognize `%=`.

These gaps affected the private JIT, AOT, and persistent REPL compiler. They
did not justify rewriting a Cupid program around the compiler. The compiler
needed the normal scalar conversions and mutation syntax.

## Decision

The private compiler converts a non-atomic scalar `float` or `double` to
`uint32_t` for explicit casts, initialization, plain assignment, fixed call
arguments, and returns. The conversion is defined for source values in
`(-1, 2^32)`, as required by C.

One emitter serves both floating widths. It builds the exact scalar threshold
2^31, compares the source with that threshold, and uses signed truncation for
the lower half. The upper path subtracts 2^31, truncates the remainder, and
restores bit 31. The float path stays in binary32. The double path stays in
binary64. Values outside C's defined interval do not receive a language
guarantee.

Direct floating literals used by global and persistent REPL unsigned objects
use the same split in the compiler's constant path. An `f` suffix retains its
rounded binary32 value before conversion. An undefined input is never passed
through an undefined host floating-to-integer cast.

The lexer also publishes a distinct `CC_TOK_PERCENTEQ` token. Direct and
indirect assignment paths use the existing saved-designator model, so the
left side is evaluated once. Signed operands execute `CDQ` and `IDIV`; the
remainder is taken from EDX. Unsigned operands clear EDX and execute `DIV`.
Locals, globals, pointer targets, array elements, and direct or pointer record
fields share that rule.

Floating `%=` remains invalid because C remainder requires integer operands.
The diagnostic names that requirement, and a rejected compilation leaves the
next compilation usable. Floating compound assignment to an unsigned word,
floating-to-pointer conversion, and vector-to-word conversion keep focused
diagnostics instead of consuming stale register state.

## Evidence

The runtime contract covers negative fractions, zero, both sides of 2^31,
the largest defined binary32 value below 2^32, and the largest defined
binary64 value below 2^32. It exercises casts, global, local, and block-static
initialization, direct and indirect stores, array and record fields, fixed
arguments, and returns.

The remainder contract covers signed negative dividends, unsigned high-bit
values, direct and indirect objects, array and record designators, one-time
index and right-side evaluation, and a member base that changes while the
right side runs. An extracted emitter oracle checks the exact signed and
unsigned division byte sequences.

The focused red run failed all three remainder tests before the token and
emitter existed. The focused green run passed all three. The eight private
CupidC modules then passed 149 tests in 17.775 seconds. The separate kernel
compile contract had already passed all 34 tests with the floating conversion
change present.

The embedded `feature13_double.cc` program checks four conversion values on
both sides of 2^31, signed and unsigned remainder assignment, and a
side-effecting array index that must run once. The boot frontier now requires:

```text
[feature13-unsigned] PASS conversions=4 remainders=2 once=1
```

The complete GUI terminal smoke module passed 116 host-side contract tests.
A focused four-vCPU e1000 boot then compiled and ran the unchanged guest
source through private CupidC. It observed the required marker, the program
pass line, and clean JIT completion in 66.7 seconds.

## Rejected alternatives

Using a host cast for static literals would make the result depend on host
integer width and on undefined host behavior outside the represented range.
The bounded split keeps the private compiler's target rule explicit.

Clamping, saturating, or defining wraparound outside `(-1, 2^32)` would invent
a Cupid extension where C deliberately leaves the result undefined.

Rewriting `%=` as a second load and ordinary `%` expression would evaluate a
pointer, subscript, or member base more than once. The saved-designator path
preserves compound-assignment semantics.

## Consequences

Private CupidC now supports both directions between its unsigned word and
`float` or `double`, within C's defined conversion ranges. `%=` joins the other
represented integer compound assignments without weakening signedness or
lvalue identity.

Issue #31 remains open. Complete behavior gates for every embedded program
and other private Cupid language forms are still unfinished. This step changes
no build owner, so no `.c` to `.cc` rename is due. `TempleOS/` remains
untouched reference material.
