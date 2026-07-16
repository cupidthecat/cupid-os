# Lower 32-bit bitwise complement through CupidC IR

## Context

The hosted CupidC path reaches this unchanged helper in `kernel/mm/memory.c`:

```c
static inline uint32_t align_up(uint32_t val, uint32_t align) {
  return (val + align - 1) & ~(align - 1);
}
```

The existing IR already handles the parameter loads, addition, subtraction, usual arithmetic conversions, bitwise AND, and return. Lowering stopped at the unary complement. Rewriting the helper as XOR with an all-ones constant would hide a real C operation that CupidC needs across the active source tree.

## Decision

The public IR gains `CTOOL_C_IR_INSTRUCTION_UNARY`. It is appended to the instruction-kind enum so existing numeric values remain stable. The record keeps the frontend operator, result type, and operand type without adding target register details.

This increment accepts `CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT` when the operand and result use the same represented four-byte integer type. A caller-supplied frozen unit with a different result type is invalid. A valid wider integer complement remains unsupported.

The i386 emitter pops the operand into EAX, emits `NOT EAX` through the shared x86 encoder, and pushes the result. The instruction has no signedness-specific encoding, so the frontend-selected type remains unchanged.

## Consequences and evidence

The IR contract guards the complete helper and builds its fixture from the guarded text. It pins 16 instructions with a maximum abstract-stack depth of three. The stream includes both parameter loads, two signed `int` constants, their conversions to `uint32_t`, the unary complement, the final bitwise AND, and return.

The object contract emits one exact 73-byte local `align_up` function. Its ELF32 object has two symbols including the null symbol and no relocations. Shared decoding checks all 41 instructions, including `ADD`, `SUB`, `NOT`, and `AND`. A second emission is byte-identical and leaves the frontend unit unchanged.

A 64-bit complement fixture receives the unsupported-type diagnostic. A copied frozen unit whose complement result is changed from unsigned `uint32_t` to signed `int` receives the invalid-unit diagnostic at the unary node. Unary negation now supplies the ordinary unsupported-expression boundary.

This is hosted bootstrap evidence. The private in-kernel compiler still produces every normal OS C object. No production artifact, build owner, host dependency, boot path, or runtime ABI changed.

Issue #25 remains open. Unary plus, negation, logical not, address and dereference lowering, wide integer and pointer operations, bit-field writes, broader statements and calls, production integration, and staged self-hosting still remain.
