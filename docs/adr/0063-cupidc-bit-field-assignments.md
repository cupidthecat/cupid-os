# Preserve stored values for represented bit-field assignments

## Context

The hosted CupidC path could read a represented four-byte bit field, but plain assignment still stopped during Linear IR lowering. Doom depends on this operation when it converts a palette. The existing source writes each channel through expressions such as `colors[i].r = value`, and another path writes fields in a local color record.

A bit field has no C address. Ordinary `MEMBER_ADDRESS` and `STORE_VALUE` therefore cannot describe the operation. Assignment also has a value, and that value reflects the field after target-width conversion. For an unsigned eight-bit field, assigning `0x123` stores and returns `0x23`. A signed field must return the sign-extended stored lane.

## Decision

Linear IR adds `BIT_FIELD_STORE_VALUE`. Its `reference` is the absolute graph-member index, its `input_type` is the complete record operand type, and its `type` is the unqualified assignment result type. The instruction consumes a record address followed by the converted right operand. It replaces the selected field and pushes the value represented by the stored field.

Lowering evaluates the record designator once and the right operand once. It accepts direct, pointer-derived, and indexed record addresses. The graph and target layout must agree on member ownership, type, width, storage size, and bit range. The current operation accepts complete non-atomic integer fields in a four-byte storage unit. Widths from one through 32 are supported when the unit fits inside the record.

The i386 emitter masks the converted value to the field width. A partial field uses one read-modify-write sequence: it clears the selected bits in the old storage unit, inserts the masked lane, and writes the complete unit once. The preserved assignment result is the masked lane for an unsigned field and the sign-extended lane for a signed field. A full-width field uses a direct store and does not read the old unit.

We did not model the field as an address because C does not provide one. Reusing ordinary `STORE_VALUE` would lose the bit offset, width, and neighboring bits. Returning the unconverted right operand would violate assignment value semantics after truncation. A full-unit read before every store was also rejected because a 32-bit field does not need a read, especially when the record is volatile.

## Consequences and evidence

The source guards pin the unchanged color record and palette writes in `kernel/doom/src/i_video.h` and `kernel/doom/src/i_video.c`. The focused IR fixture has four functions and 31 exact instructions. It covers unsigned eight-bit, signed five-bit, and full-width fields through pointer parameters, plus the indexed `colors[index].r` shape used by Doom. Each function reaches a maximum abstract stack depth of two.

The object contract emits the same ELF32 bytes on consecutive runs. Its indexed function keeps the local 1,024-byte color array in `.bss` and uses one `R_386_32` relocation. A decoder-driven i386 oracle executes the three pointer-based functions. It checks unsigned truncation, signed extension, preservation of neighboring bits, one complete-unit store, unchanged arguments, restored stack state, and the direct-store path for a 32-bit field.

Focused negatives retain clear boundaries for character-sized, `_Bool`, atomic, and compact packed storage. Malformed graph or layout widths fail transactionally, leave the frozen input unchanged, and do not publish partial IR. ADR 0064 later adds compound assignment and prefix or postfix update for represented four-byte fields. Explicit automatic-initializer bit-field leaves, non-four-byte storage units, partial volatile mutation, and atomic ordering remain separate work.

This is hosted bootstrap evidence. GCC or Clang still builds the shared compiler and every normal C object. No production object, build transform, boot path, or host dependency changes ownership. Issue #25 remains open for the remaining C and GNU surface, production integration, staged self-hosting, and the fixed-point bootstrap.
