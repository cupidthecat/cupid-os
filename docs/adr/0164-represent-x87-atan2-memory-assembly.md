# ADR 0164: Represent the x87 atan2 memory statement

## Status

Accepted on 2026-07-28.

## Context

After CupidC learned the square-root register statement, unchanged
`kernel/cpu/libm.c` reached the volatile x87 program in
`libm_atan2_impl()`. It uses one named `double` `=m` result, two named
`double` `m` inputs, and a `memory` clobber.

The statement loads `y`, loads `x`, applies `FPATAN`, and stores the result.
Cupid's shared x86 model already knows those instructions. Statement
assembly still needs typed operand checks, source-order address evaluation,
frozen Linear IR metadata, and deterministic object emission. Passing the
program to GAS would retain a host assembler path.

## Decision

Compiler-head CupidC represents this exact volatile statement. The frontend
requires one modifiable, non-atomic `double` `=m` output, two addressable,
non-atomic `double` `m` inputs in `y`, `x` order, and exactly one `memory`
clobber. Both the named source spelling and its normalized numeric spelling
use the same validation path.

Linear IR evaluates the output, `y`, and `x` addresses once in source order.
It validates the same types, constraints, counts, flags, clobber, matching
metadata, and layouts in reachable and unreachable code.

The i386 emitter consumes the saved addresses, emits both x87 loads followed
by `FPATAN`, and stores through the saved output address. Every instruction
comes from Cupid's shared x86 model.

## Evidence

The focused function contains 53 text bytes and no relocations. Its direct
statement sequence is:

```text
8B 44 24 04 DD 00 58 DD 00 D9 F3 58 58 DD 18
```

Positive contracts check the named and normalized forms, output-before-input
evaluation, shared decoding, x87 stack balance, deterministic output,
unreachable validation, rollback, and same-job recovery. Negative contracts
reject wrong types, rvalues, constants, register objects, atomic operands,
constraints, counts, templates, flags, clobbers, matching metadata, and
forged layouts.

The unchanged source now reaches the following independent statement:

```text
/kernel/cpu/libm.c:940:5: error CTB00000F: GNU inline assembly m input template is outside this slice
```

That boundary is the x87 exponent program in `libm_exp_impl()`.

## Rejected alternatives

A general x87 assembly parser was not needed to represent this exact active
statement. The source-shaped path keeps type and stack checks explicit.

Changing `libm_atan2_impl()` to a lower-level C approximation was rejected
because the existing x87 behavior is part of the source contract.

Handing the statement to GAS was rejected because CupidC object emission must
remain independent of a host assembler.

## Consequences

This change advances compiler head without moving a production recipe. The
checked seed still predates named operands and the four later libm statement
blocks. `kernel/cpu/libm.c` therefore remains host-owned and keeps its `.c`
suffix.

No normal OS object, ABI, image, runtime path, or host-dependency count
changes. `TempleOS/` remains untouched reference material.
