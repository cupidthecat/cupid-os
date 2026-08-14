# ADR 0289: Convert wide integers to float and double

## Status

Accepted on 2026-08-14.

## Context

CupidC already carried signed and unsigned 64-bit integers as complete values.
Its i386 emitter could also convert those values to `float` and `double`. The
conversion loads a 64-bit integer with x87 `FILD`; unsigned inputs at or above
2^63 receive the required 2^64 correction before the result is stored at its C
width.

Three validation gates hid that implementation. Explicit casts, assignment
conversion, and the usual arithmetic conversions admitted integer inputs only
through four bytes. As a result, ordinary source such as `(double)wide`,
`wide + 1.0`, and `flag ? wide : 1.0f` failed before emission.

The restriction did not describe a target or language limit. Narrowing source
values to make the existing validators happy would have discarded information
and weakened the compiler's self-hosting surface.

## Decision

Non-atomic signed and unsigned integers through 64 bits may convert to `float`
or `double` through an explicit cast or assignment conversion. Assignment
conversion covers initialization, plain assignment, return, and a fixed
argument whose declared parameter type is floating.

Runtime `+`, `-`, `*`, `/`, all six comparisons, and conditional selection
apply the usual arithmetic conversions when one operand is `float` or `double`
and the other is any represented value integer or compatible enum. Both
operand orders and both conditional-arm orders follow the same rule. A
`float` operand gives a `float` common type; a `double` operand gives a
`double` common type.

Conditional lowering remains lazy. Only the selected integer arm is converted.
Atomic operands continue to fail with the existing focused diagnostics.

Linear IR and object validation accept the same represented integer set as the
frontend. The emitter keeps the established SSE conversion for inputs through
four bytes. It uses the existing x87 64-bit path for a wide input, including
the unsigned correction and control-word restoration, then stores the result
at binary32 or binary64 width. No runtime helper, host floating operation,
object-format change, relocation, or ABI rule is added.

Integer-lvalue compound assignment with a floating right operand remains a
separate language boundary. This decision also does not add floating-to-wide
conversion beyond the existing explicit `double` to `unsigned long long`
case, nor does it add atomic floating conversion or update.

## Evidence

The frontend contract first failed on a signed 64-bit cast, then on a wide
return conversion after the cast gate was removed. The Linear IR run next
failed at its integer-to-floating validator, and the first object run failed at
the matching emitter validator. Each gate changed only after its failure was
captured.

The final frontend fixture contains 32 functions. For every combination of
`float` or `double` with signed or unsigned `long long`, it checks an explicit
cast, assignment conversion, all four arithmetic operators, all six
comparisons, and both conditional-arm orders. Atomic cast, assignment,
arithmetic, comparison, and conditional cases retain useful negative
diagnostics.

The Linear IR fixture checks the same 32 functions, their exact conversion and
operator inventory, branch-local conditional conversion, repeat determinism,
malformed conversion metadata, constrained allocation, and same-job recovery.

The object conversion fixture grows from 45 to 63 functions. It emits 10,513
text bytes with fingerprint `01725E63`, 64 symbols, and 123 relocations. Its
decoder lock includes 27 64-bit `FILD` inputs, 18 control-word save and restore
pairs, nine unsigned correction branches, and the expected floating arithmetic
operations. Twenty-two execution cases cover `LLONG_MIN`, `ULLONG_MAX`,
binary32 and binary64 precision boundaries, every arithmetic operator, all six
predicates, and both conditional directions.

A capability-specific object negative forges `FLOAT_PROMOTION` on an actual
wide assignment conversion. The public object operation rejects the malformed
unit without publishing bytes, preserves the input, and recovers on the same
job.

Source-head hosted CupidC and the checked Cupid-built driver also compile a
wide cast, arithmetic expression, comparison, and conditional. Their ELF32
objects match byte for byte. That production-parity check passed in 33.909
seconds.

After the reviewed source and object locks were refreshed, the complete
frontend, Linear IR, and object sweep passed all 298 tests in 1,319.854
seconds. The capability-specific object negative was added after that sweep;
the affected object and audit-inventory tests pass together. The
production-freeze and checked Toolchain contract cohort passed 47 tests in
10.838 seconds. Audit regeneration and a fresh checked comparison also pass.

## Rejected alternatives

Narrowing a wide operand to `int` or `unsigned int` in source was rejected
because it changes the value before conversion.

Requiring explicit casts for arithmetic and conditionals was rejected. These
operators use C's usual arithmetic conversions, and the compiler already owns
the necessary target conversion.

Converting through a host `double` was rejected. Host floating behavior is not
part of the i386 object contract, while the existing x87 sequence is target
owned and decoder checked.

Converting both conditional arms before branching was rejected because C
evaluates only the selected arm.

## Consequences

Source-head CupidC has one runtime integer-to-`float` or `double` rule for all
represented integer widths. The frontend, Linear IR, and object emitter agree
on that set, and the old four-byte validator boundary is gone.

The checked execution seed predates this source-head extension. No active
translation root currently requires the new expression shape, so this change
moves no production owner and changes no published OS object, artifact, ABI,
dependency, or source suffix. A later seed convergence must carry the compiler
and contract changes before the checked toolchain can claim the capability.

The documentation payload changes when this commit is integrated. The final
combined build must remeasure the artifact-size policy and run the normal QEMU
gate; a standalone OS rebuild would not provide the combined evidence.
