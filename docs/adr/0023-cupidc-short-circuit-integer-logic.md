# Lower short-circuit integer logic through CupidC IR

## Context

The hosted CupidC path first stopped at `cemit_power_of_two` in `toolchain/cupidc_emit.c`. That unchanged helper needs inequality, bitwise AND, equality, and short-circuit logical AND:

```c
static ctool_bool cemit_power_of_two(ctool_u32 value) {
  return value != 0u && (value & (value - 1u)) == 0u ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
}
```

The next short-circuit requirement comes from `cfront_bool_valid` in `toolchain/cupidc_frontend.c`. It uses logical OR while checking the two represented Boolean values:

```c
static ctool_bool cfront_bool_valid(ctool_bool value) {
  return value == CTOOL_FALSE || value == CTOOL_TRUE ? CTOOL_TRUE
                                                      : CTOOL_FALSE;
}
```

The existing `BINARY` instruction can represent eager integer operations. It cannot represent either logical operator because C must skip the right operand when the left operand determines the result.

## Decision

`BINARY` accepts equality, inequality, and bitwise AND for represented four-byte integers. Equality and inequality produce the frontend's 32-bit integer result. Bitwise AND keeps the converted operand type. The i386 emitter uses `CMP` with `SETE` or `SETNE` for the predicates and `AND EAX, ECX` for the bitwise operation.

Logical AND lowers through `BRANCH_ZERO` and `JUMP`. The left operand is evaluated first. A zero left operand branches to the false block, while a nonzero value reaches the right operand and its own zero test. The true path pushes one, the false path pushes zero, and both paths join with one normalized integer value.

Logical OR uses the same public instructions. A zero left operand branches to the right operand. A nonzero left operand pushes one and jumps to the join without evaluating the right operand. The right path branches to a shared false block when it produces zero; otherwise it pushes one and jumps over that block. No `BRANCH_NONZERO` instruction is needed.

Treating either logical operator as eager `BINARY` work was rejected because that would violate C sequencing. Folding either active helper during lowering was also rejected because the parameter is a runtime value and the IR must preserve the source behavior. A new nonzero branch was considered for logical OR, but the existing two-instruction branch model already expresses the required flow without widening the public IR.

## Consequences and evidence

Source guards pin both unchanged helpers. `cemit_power_of_two` lowers to 23 instructions with maximum abstract-stack depth three. Its two logical operand tests share one false block, and the surrounding conditional expression keeps its own false block and join.

`cfront_bool_valid` lowers to 20 instructions with maximum stack depth two. Its left zero branch reaches the second equality test. The nonzero left path and nonzero right path each jump to the logical join, while the zero right path supplies the normalized false value. The surrounding conditional then uses its own branch and join.

The exact `cemit_power_of_two` function remains 143 bytes with five checked relative branch targets. The exact `cfront_bool_valid` function is 127 bytes with 46 decoded instructions and six checked branch targets. Appending both local helpers brings the combined function object to 845 text bytes and 27 symbols. Its ten earlier relocations keep their offsets because both helpers follow every relocation-bearing function. Repeated object emission remains byte-identical.

A 64-bit logical OR fixture receives the unsupported-type diagnostic and publishes no partial IR. Count and fill lowering, constrained output, frozen-unit preservation, arena rollback, and same-job recovery remain in force.

This is hosted bootstrap evidence. GCC or Clang still builds the shared frontend, IR, emitter, x86, and ELF32 modules and their contracts. The host C compiler still produces the normal root and user C objects. The private in-kernel CupidC path remains the embedded runtime JIT and AOT path. No production artifact, ABI owner, build transform, host dependency, boot path, or runtime behavior changes here.

Issue #25 remains open. The remaining bitwise and comparison operators, bit-field writes, non-four-byte values, subscript and pointer-based addresses, compound and update lowering, atomic ordering, general statements, broader calls and ABI work, production integration, and staged self-hosting still remain.
