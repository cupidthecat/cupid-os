# Evaluate same-kind floating arithmetic in hosted CupidC

- Status: Accepted
- Date: 2026-07-22

## Context

Hosted CupidC can already move `float` and `double` values through objects, calls, variadic reads, and returns. It can also promote a source `float` to `double` at an ellipsis or unprototyped call position. Arithmetic still stops before Linear IR.

Active source needs a useful arithmetic boundary. The unchanged `libm_tanh_impl` in `kernel/cpu/libm.c` calls `libm_exp_impl` twice, then evaluates `(e1 - e2) / (e1 + e2)` with `double` operands. Rewriting that function around a compiler gap would weaken the source and leave the same requirement elsewhere in the OS.

## Decision

The shared frontend accepts unary plus and minus and binary addition, subtraction, multiplication, and division when both operands have the same represented floating kind. The supported kinds are non-atomic `float` and non-atomic `double`. Lvalue conversion happens before the operation. A `float` expression keeps type `float`; a `double` expression keeps type `double`.

Linear IR records the ordinary typed `UNARY` and `BINARY` instructions. It accepts them only when the operand and result types match exactly and the operation belongs to this boundary. Malformed floating operator records fail transactionally. The existing same-kind usual-arithmetic conversion record is a typed no-op and cannot change the width or kind.

The i386 emitter loads each operand into x87, evaluates `FCHS`, `FADDP`, `FSUBP`, `FMULP`, or `FDIVP`, and immediately stores every changed result at its C type's width. A `float` result is rounded into a fresh four-byte semantic stack slot. A `double` result is rounded into a fresh private eight-byte snapshot, after which the emitter publishes the usual one-word snapshot handle. No live x87 value crosses another Linear IR instruction. Unary plus can keep the existing immutable represented value because it changes neither the value nor its type.

Subtraction and division load the left operand before the right operand, then use the `ST1` operation `ST0` forms. This preserves source order for the two noncommutative operators.

This boundary does not include mixed `float` and `double` arithmetic, integer and floating arithmetic conversions, `long double`, atomic values, floating literals, comparisons, truth operations, conditional expressions, remainder, bitwise operators, compound assignment, increment or decrement, fixed-parameter conversion, explicit floating casts, or explicit static floating initializers.

Storage and output failures retain the existing transaction contract. Failed lowering or emission publishes no partial result, preserves the borrowed translation unit, rewinds temporary state, and allows the same job to recover.

## Rejected alternatives

Keeping intermediate values live in x87 until a later use would make unrelated Linear IR instructions depend on x87 stack depth. Immediate stores keep value lifetime explicit and match the existing call-result design.

Converting every `float` operation to `double` would change the language type and the required rounding point. The supported path stores each `float` result at four-byte width instead.

Calling a host math helper would add a runtime dependency and move target semantics into the bootstrap host. The emitted x87 sequence remains part of the target object.

Rewriting `libm_tanh_impl` to use integer bit manipulation or a weaker formula would move a compiler limitation into active OS source. The compiler now accepts the source's ordinary C expression.

## Consequences and evidence

Frontend contracts cover each operator for both widths, nested expressions, and call-produced operands. Useful diagnostics retain the unsupported boundaries for mixed kinds, integer and floating operands, `long double`, atomic values, comparisons, compound assignment, remainder, and bitwise complement.

Linear IR contracts check the exact operator and type inventory, deterministic repeat lowering, malformed frozen records, constrained storage, rollback, and same-job recovery. An exact source fingerprint keeps the complete unchanged `libm_tanh_impl` body in scope without claiming that this one test transfers ownership of its translation unit.

The object contract decodes the emitted x87 sequence, checks immediate target-width stores, executes asymmetric subtraction and division cases, and covers signed zero, infinity, and NaN behavior through the modeled target path. It also checks nested calls, stable snapshots, frame state, deterministic output, constrained output, and recovery. The model is a decoder-driven proof of the emitted subset, not a native x87 execution claim.

This capability changes no build owner. GCC or Clang still builds the shared compiler, its contracts, and every normal Cupid OS C object. Issue #25 remains open for the rest of floating semantics, the remaining C and ABI surface, production integration, staged self-hosting, and the fixed-point bootstrap.
