# Lower 32-bit less-than comparisons through CupidC IR

## Context

The hosted CupidC path could emit equality and the two greater-than relations, but it stopped on the unchanged branch-range helper in `toolchain/cupidasm.c`:

```c
static ctool_bool asm_branch_fits_i8(ctool_u32 bits) {
  return bits <= 0x7fu || bits >= 0xffffff80u ? CTOOL_TRUE : CTOOL_FALSE;
}
```

The frontend already assigns the correct unsigned operand type to both comparisons. Logical OR and conditional selection also lower through the existing branch instructions. The missing operation was unsigned less-than-or-equal. Less-than uses the same target predicate family, so both remaining relational operators belong in one decision.

## Decision

The existing `CTOOL_C_IR_INSTRUCTION_BINARY` record accepts `CTOOL_C_EXPRESSION_OPERATOR_LESS` and `CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL` for represented four-byte integers. IR keeps the source operator, converted operand type, and signedness-bearing type identity. No public record changes.

The i386 emitter pops the right operand into ECX and the left operand into EAX, then emits `CMP EAX, ECX`. Signed less-than uses `SETL`, and signed less-than-or-equal uses `SETLE`. Unsigned less-than uses `SETB`, and unsigned less-than-or-equal uses `SETBE`. `MOVZX` normalizes the predicate result to a four-byte integer before it returns to the abstract stack.

Pointer relations and 64-bit integer relations remain unsupported in IR. The frontend represents those expressions, but the current object path does not represent pointer values or wide integer values. It must not accept either form as a four-byte integer comparison.

Rewriting less-than as a reversed greater-than operation was rejected. Lowering already preserves source-order evaluation before `BINARY`, but retaining the original operator keeps IR semantic and makes target predicate selection direct. A new comparison instruction was also rejected because `BINARY` already carries the operator and input type needed by the emitter.

## Consequences and evidence

The source guard pins the complete unchanged CupidASM helper. Its focused fixture lowers to 20 IR instructions with a maximum abstract-stack depth of two. The first predicate is unsigned less-than-or-equal, the second is the existing unsigned greater-than-or-equal, and logical OR skips the second comparison when the first is true.

The exact object contains the 127-byte active helper plus three 39-byte functions for signed less-than, signed less-than-or-equal, and unsigned less-than. It has 244 text bytes, five symbols, no relocations, and byte-identical repeat emission. Shared x86 decoding checks each predicate and all six branch destinations in the active helper.

A 64-bit less-than-or-equal fixture receives the unsupported-type diagnostic and publishes no partial IR. Existing frozen-unit validation, output rollback, earlier-result preservation, and same-job recovery still apply.

This is hosted bootstrap evidence. GCC or Clang still builds the shared frontend, IR, emitter, x86, ELF32, and contract modules. The private in-kernel CupidC path still produces every normal OS C object. No production artifact, build owner, host dependency, boot path, or runtime ABI changes here.

Issue #25 remains open. Bitwise OR and XOR, shifts, pointer and subscript addresses, bit-field writes, other value widths, general statements, broader calls, production integration, and staged self-hosting still remain.
