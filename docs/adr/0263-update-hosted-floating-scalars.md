# ADR 0263: Update hosted floating scalars

## Status

Accepted on 2026-08-11.

## Context

Private CupidC already implemented prefix and postfix `++` and `--` for
`float` and `double`. The hosted frontend still rejected the same valid C
expressions with `floating update is outside this body slice`. Active
`bin/feature13_double.cc` uses these forms for locals, globals, standalone
statements, and a `for` increment, so the hosted gap blocked source parity
instead of representing a speculative language feature.

A postfix update must return the exact old value after storing the new value.
Computing that result by reversing the update would not preserve a negative
zero or a NaN payload. An indirect lvalue must also be evaluated once.

## Decision

Accept prefix and postfix increment and decrement for modifiable, non-atomic
`float` and `double` lvalues. The frontend records the operand width as the
computation type. Atomic floating updates and `long double` updates retain
separate unsupported diagnostics.

Linear IR evaluates the lvalue address once, loads the old value, and adds or
subtracts an exact `1.0` at that value's width. Prefix updates use the existing
value-preserving store. Postfix updates use a new `STORE_OLD_VALUE`
instruction that consumes the destination, old value, and replacement. It
stores the replacement once and returns the original value.

The i386 emitter keeps a `float` as one raw four-byte value. It keeps a
`double` in its existing private eight-byte snapshot. `STORE_OLD_VALUE` copies
the replacement through the ordinary target-width store and leaves the old raw
value or snapshot handle on the semantic stack. The operation does not derive
the old result from the replacement.

## Test evidence

The frontend contract covers all four operator forms at both represented
widths. It retains the established const, rvalue, pointer, aggregate, and
incomplete-target checks, and adds exact failures for atomic floating values
and `long double`.

The Linear IR contract pins the exact-width `1.0` constants, arithmetic, and
prefix or postfix store sequences. A forged computation type fails
transactionally, and the untouched unit lowers successfully afterward.

The deterministic object contract executes local, file, member, indexed, and
indirect lvalue updates. It covers every direction and prefix or postfix
result, evaluates a side-effecting designator once, and checks that postfix
preserves binary32 negative zero plus binary32 and binary64 NaN payloads.
Repeated emission is byte-identical.

A CupidC-compiled, CupidASM-assembled, and CupidLD-linked static i386 runtime
runs under WSL and prints `runtime-ok`. It repeats the local, file, member,
indexed, one-time-evaluation, signed-zero, and NaN cases through the hosted
runtime closure.

The clean frontend, IR, and object entry points pass three focused tests in
46.005 seconds. Neighboring floating arithmetic, wide mutation, and bit-field
mutation selectors also pass in the implementation worktree. The canonical
Toolchain test recipe invokes each new selector exactly once.

## Rejected alternatives

Reconstructing a postfix result by applying the inverse operation was rejected
because floating arithmetic is not reversible and would change signed-zero and
NaN payload behavior.

Evaluating the lvalue again for the store was rejected because C permits a
side-effecting member, pointer, or index designator.

Converting through an integer was rejected because it loses fractions,
infinities, NaNs, and values outside the represented integer range.

Including atomic floating or `long double` updates was rejected for this
slice. Neither has the required load, arithmetic, store, and result contract
at this boundary.

## Consequences

Source-head hosted CupidC now matches private CupidC for ordinary `float` and
`double` updates, including indirect lvalues. The checked seed does not carry
this capability until the next five-tool promotion, so this decision does not
move a production owner.

Atomic floating updates, `long double` updates, SIMD updates, and the remaining
hosted floating gaps stay explicit. No `.c` to `.cc` rename is due, and
`TempleOS/` remains read-only reference material.
