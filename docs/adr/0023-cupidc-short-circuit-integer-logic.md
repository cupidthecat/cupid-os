# Lower short-circuit integer logic through CupidC IR

## Context

The hosted CupidC path stopped at `cemit_power_of_two` in `toolchain/cupidc_emit.c`. That unchanged helper checks a 32-bit value with inequality, bitwise AND, equality, and short-circuit logical AND:

```c
static ctool_bool cemit_power_of_two(ctool_u32 value) {
  return value != 0u && (value & (value - 1u)) == 0u ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
}
```

The existing `BINARY` instruction could represent eager integer operations, but logical AND needs control flow. Its right operand must not execute when the left operand is false.

## Decision

`BINARY` now accepts equality, inequality, and bitwise AND for represented four-byte integers. Equality and inequality always produce the frontend's 32-bit integer result. Bitwise AND keeps the converted operand type. The i386 emitter uses `CMP` with `SETE` or `SETNE` for the predicates and `AND EAX, ECX` for the bitwise operation.

Logical AND lowers through the existing branch instructions. The left operand is evaluated first and tested with `BRANCH_ZERO`. Only its nonzero path reaches the right operand and a second zero test. The true path pushes one and jumps over a shared false block that pushes zero. Both paths leave one normalized integer value on the abstract stack. No new public IR instruction is needed.

Treating logical AND as an eager `BINARY` operation was rejected because it would evaluate the right operand unconditionally. Folding this helper during lowering was also rejected because the operand is a runtime parameter and the IR must preserve its sequencing. Adding logical OR in the same change was deferred because unchanged active source only requires logical AND here.

## Consequences and evidence

The source guard pins the unchanged helper in `toolchain/cupidc_emit.c`. Its focused function lowers to 23 instructions with a maximum abstract-stack depth of three. The two operand tests branch to one false block, and the surrounding conditional expression retains its own false block and join.

The exact i386 function is 143 bytes. It uses `SETNE`, `AND`, `SETE`, three `TEST` instructions, three `JE` instructions, and two `JMP` instructions. All five relative branch targets are checked through the shared decoder. Adding the local function grows the combined function object's text from 575 to 718 bytes and its symbol count from 25 to 26. The existing ten relocations do not move because the helper is appended after every relocation-bearing function.

A valid logical OR fixture receives the unsupported-expression diagnostic and publishes no partial IR. Existing 64-bit comparison tests keep wider integer predicates outside the represented value width. Count and fill lowering, constrained output, deterministic object emission, frozen-unit preservation, and same-job recovery remain in force.

This is hosted bootstrap evidence. GCC or Clang still builds the shared frontend, IR, emitter, x86, and ELF32 modules and their contracts. The private in-kernel CupidC path still produces every normal OS C object. No production artifact, ABI owner, build transform, host dependency, boot path, or runtime behavior changes here.

Issue #25 remains open. Logical OR, the remaining bitwise and comparison operators, bit-field writes, non-four-byte values, subscript and pointer-based addresses, compound and update lowering, atomic ordering, general statements, broader calls and ABI work, production integration, and staged self-hosting still remain.
