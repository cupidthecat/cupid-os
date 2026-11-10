# ADR 0091: Convert floating widths in hosted CupidC

## Status

Accepted on 2026-07-23.

## Context

Hosted CupidC could move `float` and `double` values and evaluate arithmetic
when both operands had the same type. Ordinary C source still stopped at a
change in width. This blocked fixed-parameter calls, returns, mixed arithmetic,
conditional expressions, and compound assignment.

The unchanged `libm_tanhf_impl` and the following `float` helpers in
`kernel/cpu/libm.c` use these rules. Rewriting those functions around the
compiler would move a toolchain limit into active OS source.

## Decision

The shared frontend accepts explicit casts and assignment conversion between
non-atomic `float` and `double`. This covers initialization, plain assignment,
fixed arguments, and returns because those paths already use assignment
conversion.

Mixed `float` and `double` operands for addition, subtraction, multiplication,
and division use `double` as their common type. A conditional expression with
matching floating arms keeps that width. One arm of each width converts the
`float` arm to `double`. Its condition must remain a represented integer or
pointer.

`+=`, `-=`, `*=`, and `/=` now work on represented floating lvalues. The
computation uses the common floating width, then assignment conversion stores
the result at the left operand's width. The lvalue address is evaluated once.

Linear IR records each width change as `CONVERT`. Explicit casts use
`CTOOL_C_CONVERSION_NONE`, stored-value conversions use
`CTOOL_C_CONVERSION_ASSIGNMENT`, and mixed expressions use
`CTOOL_C_CONVERSION_USUAL_ARITHMETIC`. The existing default argument promotion
continues to use `CTOOL_C_CONVERSION_FLOAT_PROMOTION`. Validation rejects a
reverse promotion and any conversion kind that does not fit its source and
target widths.

The i386 emitter loads the source with width-specific x87 `FLD`, removes its
old semantic handle, and stores the result with width-specific `FSTP`. A
`float` result occupies a fresh four-byte slot. A `double` result receives a
fresh private eight-byte snapshot. No live x87 value crosses the next Linear
IR instruction.

This boundary does not include conversion between integers and floating
values, floating comparisons or truth, a floating controlling expression,
increment or decrement, `long double`, atomic floating access, floating
literals, explicit static floating initializers, SIMD, or over-aligned
emission.

## Alternatives considered

Keeping mixed expressions at the left operand's width would violate the usual
arithmetic conversions and would make operand order change the result type.

Computing a compound assignment entirely at the left width would skip the
common-type calculation required before the final store.

Leaving a converted result in x87 would make later instructions depend on
hidden x87 stack state. Immediate target-width storage keeps the existing
snapshot model intact.

Rewriting `libm_tanhf_impl` or its neighboring helpers would preserve a
compiler gap instead of extending CupidC for active source.

## Consequences

The frontend, IR, and object contracts each expose a
`floating-conversions` selector. Positive cases cover casts, initialization,
assignment, fixed calls, returns, all four mixed arithmetic operators,
matching and mixed conditional arms, integer and pointer conditions,
same-width and mixed-width compound assignment, and one-time lvalue
evaluation. Same-width casts and initialization-only conversions keep those
paths independent from assignment coverage.

Useful negatives retain the open boundary for integer and floating
conversion, `long double`, atomic values, comparisons, floating conditions,
mixed scalar categories, and floating update. Atomic conditional arms are
rejected before lvalue conversion can erase their qualifiers.

The deterministic 41-function object contains 6,582 text bytes with
fingerprint `0EB80E4E`, 42 symbols, and 89 relocations. Its decoder checks
32-bit and 64-bit x87 loads and stores, arithmetic operations, and aligned
calls. The execution model covers signed zero, subnormals, infinity, NaN class
preservation, round-to-even narrowing, same-width casts, initialization-only
width changes, mixed arithmetic, matching and mixed conditional selection,
compound stores, and preserved frame state across 46 executed cases.

The complete unchanged `libm_tanhf_impl` through the final `float` helper is
guarded as a 162-byte normalized active-source slice with FNV fingerprint
`03678F60DF3C62E7`.

This capability changes no production build owner. Issue #25 remains open for
the rest of C11 and GNU compatibility, floating comparisons and truth,
integer and floating conversion, remaining floating forms, production
integration, and normal-build C ownership.
