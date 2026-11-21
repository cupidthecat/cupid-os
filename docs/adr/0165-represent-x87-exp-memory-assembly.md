# ADR 0165: Represent the x87 exp memory statement

## Status

Accepted on 2026-07-28.

## Context

After CupidC learned the `atan2` statement, unchanged
`kernel/cpu/libm.c` reached the volatile x87 program in
`libm_exp_impl()`. It uses one named `double` `=m` result, one named
`double` `m` argument, one named `double` `m` constant, and a `memory`
clobber.

The statement computes `exp(x)` as `exp2(x * log2(e))`. Cupid's shared x86
model already knows the required x87 instructions, including the canonical
reverse-subtract form. Statement assembly still needs typed operand checks,
source-order address evaluation, frozen Linear IR metadata, x87 stack
validation, and deterministic object emission.

## Decision

Compiler-head CupidC represents this exact volatile statement. The frontend
requires one modifiable, non-atomic `double` `=m` output, two addressable,
non-atomic `double` `m` inputs in `x`, `log2e` order, and exactly one
`memory` clobber. Both the named source spelling and its normalized numeric
spelling use the same validation path.

Linear IR evaluates the output, `x`, and `log2e` addresses once in source
order. It validates the same types, constraints, counts, flags, clobber,
matching metadata, and layouts in reachable and unreachable code.

The i386 emitter consumes the saved addresses and emits the complete x87
pipeline through Cupid's shared model. It reaches stack depth three and
returns to the incoming depth before storing the result.

## Evidence

The focused function contains 71 text bytes and no relocations. Its direct
statement sequence is:

```text
8B 44 24 04 DD 00 58 DD 00 DE C9 D9 C0 D9 FC DC E1 D9 C9 D9 F0 D9 E8 DE C1 D9 FD DD D9 58 58 DD 18
```

Positive contracts check the named and normalized forms, source-order
address evaluation, shared decoding, x87 depth, deterministic output,
unreachable validation, rollback, and same-job recovery. Negative contracts
reject wrong types, rvalues, constants, register objects, atomic operands,
constraints, counts, templates, flags, clobbers, matching metadata, and
forged layouts.

The unchanged source now reaches this independent file-scope effect:

```text
/kernel/cpu/libm.c:242:1: error CTC000003: GNU file-scope assembly template is outside this i386 emission slice
```

That boundary defines the aligned `fabs_mask_d` and `fabs_mask_s` constants
used by the following `fabs` wrappers.

## Rejected alternatives

A general x87 assembly parser was not needed to represent this exact active
statement. The source-shaped path keeps operand and stack checks explicit.

Replacing the pipeline with a C approximation was rejected because the
existing x87 behavior is part of the source contract.

Handing the statement to GAS was rejected because CupidC object emission must
remain independent of a host assembler.

## Consequences

This change advances compiler head without moving a production recipe. The
checked seed still predates named operands and the five later libm statement
blocks. `kernel/cpu/libm.c` therefore remains host-owned and keeps its `.c`
suffix.

No normal OS object, ABI, image, runtime path, or host-dependency count
changes. `TempleOS/` remains untouched reference material.
