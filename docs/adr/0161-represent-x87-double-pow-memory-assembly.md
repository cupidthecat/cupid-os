# ADR 0161: Represent x87 double-power memory assembly

## Status

Accepted on 2026-07-28.

## Context

After compiler head learned GNU named operands, unchanged
`kernel/cpu/libm.c` reached the assembly statement inside
`libm_pow_impl()`. This is not a small variation of the earlier sine or
round-down forms. It has one memory output, four memory inputs, a memory
clobber, and a seventeen-instruction x87 program.

The source computes `exp(y * log(x))` without calling another kernel symbol.
Its stack program uses `FYL2X`, `FRNDINT`, `F2XM1`, and `FSCALE`, with a
reverse subtraction between two explicit x87 registers. Passing the statement
to a host assembler would preserve the dependency this bootstrap is meant to
remove.

## Decision

Compiler-head CupidC represents this exact volatile statement. The frontend
requires one modifiable, non-atomic `double` `=m` output, four addressable,
non-atomic `double` `m` inputs, one `memory` clobber, and no other clobber.
The active named spelling and its canonical numeric spelling identify the
same exact template. Public frontend metadata still contains only numeric
operand indexes.

Linear IR evaluates the output followed by the four inputs, once each and in
source order. It retains five address values for the assembly operation.
Forged templates, flags, counts, constraints, matching metadata, types, and
layouts fail before object emission.

The i386 emitter consumes those addresses through EAX and emits the complete
x87 sequence through Cupid's shared instruction model. `FSUBR ST(1), ST(0)`
uses the canonical `DC E1` encoding. The sequence returns the x87 stack to
its incoming depth after reaching a maximum depth of three.

## Evidence

The focused object contains one 116-byte function, no relocations, and the
exact expected instruction stream. Shared decoding checks all seventeen x87
instructions, five address loads, one twenty-byte stack discard, the
`DC E1` bytes, and the depth change at every instruction.

Frontend negatives cover a constant, register, or atomic output; a
wrong-width, rvalue, register, or atomic input; missing volatility or memory
clobber; and an extra AX clobber. IR and object negatives cover forged frozen
metadata, output limits, rollback, deterministic repeat output, and
same-job recovery.

The first integration run exposed an ordering detail in the earlier named
operand work. Operand semantics are checked before names are normalized, so
the new negative fixture received a generic floating-memory diagnostic. A
pre-scan of the entire operand list would have duplicated expression parsing.
Instead, the exact frontend classifier recognizes the active named spelling
for early diagnostics while the published statement remains numeric. The
one-test regression and the original four-test loop pass.

The unchanged source now passes the double-precision statement and reaches
the separate mixed-width statement in `libm_powf_impl()`:

```text
/kernel/cpu/libm.c:807:5: error CTB00000F: GNU inline assembly m input template is outside this slice
```

The new boundary has complete frontend, Linear IR, deterministic object,
unchanged-source, audit, self-host, and fixed-point gates. Exact results are
recorded in `docs/bootstrap/LOG.md`.

## Rejected alternatives

Calling `exp()` and `log()` from C was rejected because the current source
deliberately avoids the kernel's XMM0 return bridge inside this helper.

Handing the statement to GAS was rejected because it would add a host
assembler path to CupidC object emission.

Pre-scanning and partially parsing every GNU operand before the normal parser
was rejected. It would maintain two views of operand syntax and make
transactional error handling harder to trust.

Treating the float `powf` statement as though it used five doubles was
rejected. Its output and two inputs have different widths and need their own
represented contract.

## Consequences

This change advances compiler head without moving a production recipe.
The checked seed still predates named operands and this x87 form, while the
next unchanged-source blocker is the float `powf` statement at line 807.
`kernel/cpu/libm.c` therefore stays host-owned and keeps its `.c` name.

No normal OS object, ABI, image, runtime path, or host-dependency count
changes. `TempleOS/` remains untouched reference material.
