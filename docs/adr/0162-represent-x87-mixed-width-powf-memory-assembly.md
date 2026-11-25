# ADR 0162: Represent x87 mixed-width powf memory assembly

## Status

Accepted on 2026-07-28.

## Context

After CupidC learned the double-precision power statement, unchanged
`kernel/cpu/libm.c` reached the similar-looking statement in
`libm_powf_impl()`. The two forms do not have the same operand contract.
`powf` stores a `float`, loads `float` values for `x` and `y`, and still
loads the `double` constants for `ln(2)` and `log2(e)`.

Treating all five operands as one width would accept incorrect source or emit
the wrong x87 memory instructions. Passing the mixed-width statement to GAS
would preserve a host-assembler dependency.

## Decision

Compiler-head CupidC represents this exact volatile statement. The frontend
requires one modifiable, non-atomic `float` `=m` output, two addressable,
non-atomic `float` `m` inputs, two addressable, non-atomic `double` `m`
inputs, one `memory` clobber, and no other clobber. Its active named spelling
and normalized numeric spelling identify the same statement. Public metadata
continues to retain numeric operand indexes only.

Linear IR evaluates the output, `x`, `y`, `ln(2)`, and `log2(e)` addresses
once each in source order. It checks the width and modifiability of every
operand in reachable and unreachable code.

The i386 emitter shares the proven power sequence while selecting 32-bit
loads for `x` and `y`, 64-bit loads for the constants, and a 32-bit final
store. The sequence uses Cupid's shared x86 model throughout.
`FSUBR ST(1), ST(0)` retains the canonical `DC E1` encoding.

## Evidence

The focused function contains 116 text bytes and no relocations. Its direct
assembly sequence occupies 56 bytes. Shared decoding checks all seventeen
x87 instructions, the five consumed address slots, the exact `83 C4 14`
cleanup, maximum x87 depth three, and balanced depth on return.

Frontend negatives distinguish the float result and arguments from the
double constants. IR and object negatives cover forged types, constraints,
counts, matching metadata, templates, layouts, and flags. Constrained output,
deterministic repeat output, rollback, unreachable validation, and same-job
recovery are also covered.

Integration found that early operand diagnostics need the exact named source
spelling as well as the normalized numeric spelling. The classifier now
recognizes both forms, while the frozen frontend graph remains numeric.

The unchanged source now passes both power statements and reaches the next
independent assembly boundary:

```text
/kernel/cpu/libm.c:914:44: error CTB00000F: GNU inline assembly output constraint is outside this slice
```

The new boundary is the `=x` output of `sqrtsd %1, %0` in
`libm_sqrt_impl()`.

## Rejected alternatives

Reusing the double-power metadata unchanged was rejected because it would
misrepresent three `float` operands as `double`.

Duplicating the complete emitter sequence was rejected. One width-parameter
path keeps the already-proven instruction order and cleanup shared.

Handing the statement to GAS was rejected because CupidC object emission must
not acquire a host assembler path.

General x87 template interpretation remains outside this source-driven
increment.

## Consequences

This change advances compiler head without moving a production recipe. The
checked seed still predates named operands and both power statements.
`kernel/cpu/libm.c` therefore stays host-owned and keeps its `.c` suffix.

No normal OS object, ABI, image, runtime path, or host-dependency count
changes. `TempleOS/` remains untouched reference material.
