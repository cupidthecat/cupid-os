# ADR 0163: Represent the sqrtsd register statement

## Status

Accepted on 2026-07-28.

## Context

After CupidC learned both power statements, unchanged `kernel/cpu/libm.c`
reached the volatile `sqrtsd %1, %0` statement in `libm_sqrt_impl()`. Its GNU
constraints use the SSE register class rather than memory operands: one
`double` `=x` result and one `double` `x` input.

Cupid's shared x86 model already knows `MOVSD` and `SQRTSD`, including the
file-scope `sqrt` wrapper near the start of the same source. Statement
assembly still needs its own typed frontend, Linear IR, and object-emission
contract. Passing the statement to GAS would retain a host assembler path.

## Decision

Compiler-head CupidC represents this exact volatile statement. The frontend
requires one modifiable, non-atomic `double` `=x` output, one non-atomic
`double` `x` input, and no clobbers. Other `x` and `=x` templates remain
outside this source-driven slice.

Linear IR evaluates the output lvalue address before the converted input
value. It validates the same types, constraints, counts, flags, and layout in
reachable and unreachable code.

The i386 emitter uses XMM0 as a private implementation register. It loads the
input with `MOVSD`, applies `SQRTSD XMM0, XMM0`, and stores the result through
the saved output address. Every instruction goes through Cupid's shared x86
model.

## Evidence

The focused function contains 65 text bytes and no relocations. Its direct
statement sequence is:

```text
58 F2 0F 10 00 F2 0F 51 C0 58 F2 0F 11 00
```

Positive contracts check the exact active statement, output-before-input
evaluation, shared decoding, deterministic output, unreachable validation,
rollback, and same-job recovery. Negative contracts reject wrong widths,
atomic operands, rvalues, constants, register objects, constraints, counts,
templates, flags, clobbers, matching metadata, and forged layouts.

The unchanged source now reaches the following independent statement:

```text
/kernel/cpu/libm.c:922:5: error CTB00000F: GNU inline assembly m input template is outside this slice
```

That boundary is the x87 memory program in `libm_atan2_impl()`.

## Rejected alternatives

A general XMM register allocator was not needed to represent this exact
active statement. The private XMM0 path keeps the current boundary narrow
without changing the source contract.

Treating the operands as memory constraints was rejected because it would
misrepresent their GNU `x` register-class contract.

Reusing the file-scope wrapper path was rejected because function-body
assembly has typed expressions, evaluation order, and recovery behavior that
the file-scope parser does not own.

Handing the statement to GAS was rejected because CupidC object emission must
remain independent of a host assembler.

## Consequences

This change advances compiler head without moving a production recipe. The
checked seed still predates named operands and the three later libm statement
blocks. `kernel/cpu/libm.c` therefore remains host-owned and keeps its `.c`
suffix.

No normal OS object, ABI, image, runtime path, or host-dependency count
changes. `TempleOS/` remains untouched reference material.
