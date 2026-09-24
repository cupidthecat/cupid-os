# ADR 0137: Emit C floating comparisons

## Status

Accepted on 2026-07-27.

## Context

The shared CupidC path already transports, converts, and computes non-atomic
`float` and `double` values. It still rejected every floating equality or
relational operator. That stopped the unchanged glyph rasterizer at
expressions such as `v < 0.0f`, `(float)i != v`, and `d2 < 0.123f`.

Rewriting those conditions as integer bit tests would change their C
semantics, especially for signed zero and NaN. CupidC needs ordinary C
floating comparisons with target behavior that does not depend on the
bootstrap host.

## Decision

Represent `==`, `!=`, `<`, `<=`, `>`, and `>=` as ordinary binary AST and
Linear IR operations with a signed `int` result. Both operands must reach the
same non-atomic `float` or `double` type. The usual arithmetic conversion
widens a mixed `float` and `double` pair to `double`.

The i386 emitter consumes the two semantic values into XMM0 and XMM1, then
uses `UCOMISS` for binary32 or `UCOMISD` for binary64. An ordered comparison
uses the corresponding unsigned predicate. Unordered results need an
explicit parity branch for equality, inequality, less-than, and
less-than-or-equal. Greater-than and greater-than-or-equal are already false
for unordered inputs with `SETA` and `SETAE`.

The result is a normalized four-byte C `int`. The comparison path consumes
both operands and pushes one result without exposing an XMM register or
snapshot address in public IR.

Keep direct floating truth conversion outside this decision. A floating
comparison can control a statement because its result is an `int`, but a
bare floating value still cannot serve as a condition. `long double`,
atomic floating access, and comparisons that require an unrepresented
eight-byte integer conversion retain focused diagnostics.

## Evidence

Frontend and Linear IR contracts cover all six operators for matching
`float`, matching `double`, and mixed-width operands. The mixed cases prove
that exactly one binary32 operand widens before each binary64 comparison.
Malformed result and operation metadata fail transactionally. Constrained
allocation, repeat lowering, and same-job recovery are also checked.

The deterministic ELF contract emits 1,524 text bytes with fingerprint
`0DC63C53`, 19 symbols, and no relocations. Shared decoding finds the expected
`UCOMISS`, `UCOMISD`, parity branches, and normalized predicates. Its i386
execution oracle covers ordered values, positive and negative zero, both
infinities, quiet NaN, and signaling NaN with either operand unordered. It
also checks cdecl arguments, stack restoration, and callee-saved registers.

The unchanged `kernel/gfx/glyph_raster.c` source compiles twice under the
complete native `KERNEL_I386` profile to identical 11,740-byte i386
relocatable objects with SHA-256
`880777180290245CB62F21AD799218CB9D3C8C3BA6D6449C2BE3352C48934B33`.

## Rejected alternatives

Using x87 status-word extraction was rejected because the existing semantic
stack already has exact binary32 values and binary64 snapshot addresses that
load directly into XMM registers.

Treating every unsigned predicate as sufficient was rejected because
`UCOMISS` and `UCOMISD` set zero, parity, and carry together for unordered
operands. That would make equality and the two lower relations true for NaN.

Lowering `!=` as logical negation of `==` was rejected because it would add a
second public operation and would not remove the need to handle unordered
results at the machine comparison.

Changing the glyph rasterizer was rejected because its comparisons are valid
C and expose a compiler gap.

## Consequences

Compiler head can evaluate all six C comparisons for represented `float` and
`double` operands, including mixed widths and IEEE unordered behavior. This
closes the next known language blocker in the unchanged glyph rasterizer.

The checked bootstrap seed still predates this capability. A seed refresh and
fixed-point proof must happen before the normal build can transfer
`kernel/gfx/glyph_raster.c` to CupidC ownership and rename it to `.cc`.

Direct floating truth, `long double`, atomic floating access, conversion to
unsigned four-byte integers or `_Bool`, hexadecimal and subnormal literals,
floating increment and decrement, and static floating arithmetic remain
separate work.
