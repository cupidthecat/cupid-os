# Lower 32-bit bitwise XOR through CupidC IR

## Context

The hosted CupidC path reaches this unchanged return statement in `kernel/cpu/simd.c`:

```c
return ((before ^ after) & (1u << 21)) != 0u;
```

The expression checks whether toggling the CPUID flag changed EFLAGS. Its two XOR operands are unsigned 32-bit values after the usual arithmetic conversions. The shared frontend already records those types and the surrounding shift, bitwise AND, comparison, and assignment conversion to `bool`.

The complete `simd_cpu_has_cpuid` helper also contains GNU inline assembly and a statement sequence outside the current leaf-body subset. This decision covers the unchanged return expression without claiming that the whole helper can enter the hosted object path.

## Decision

The existing `CTOOL_C_IR_INSTRUCTION_BINARY` record accepts bitwise XOR when both operands and the result use the same represented four-byte integer type. No new IR instruction is needed because the frontend has already applied the usual arithmetic conversions and the operation has the same stack behavior as bitwise AND and OR.

The i386 emitter pops the right operand into ECX and the left operand into EAX, then emits `XOR EAX, ECX` through the shared x86 encoder. The result keeps the frontend-selected signed or unsigned type. XOR itself needs no signedness-specific encoding.

## Consequences and evidence

A source guard pins the unchanged return statement. Its focused fixture lowers to exactly 13 IR instructions with a maximum abstract-stack depth of three. The sequence retains both unsigned parameters, the XOR, the signed `int` shift count, the unsigned mask, the signed `int` comparison result, the assignment conversion to `bool`, and the return.

The object contract emits one exact 69-byte local function. Its ELF32 object has two symbols including the null symbol and no relocations. Shared decoding checks all 36 instructions, including `XOR`, `SHL`, `AND`, `CMP`, `SETNE`, and `MOVZX`. Repeated emission is byte-identical.

A 64-bit XOR fixture receives the unsupported-type diagnostic. A copied frozen unit whose XOR result type is changed from unsigned `uint32_t` to signed `int` receives the invalid-unit diagnostic before its parent expression can reinterpret the mismatch. Unary complement now supplies the ordinary unsupported-expression boundary, so accepting XOR does not hide fail-closed behavior for another valid frontend expression.

This is hosted bootstrap evidence. The host C compiler still produces the normal root and user C objects. The private in-kernel compiler remains the embedded runtime JIT and AOT path. No production artifact, build owner, host dependency, boot path, or runtime ABI changed.

Later decisions close several frontiers that were open here. ADRs 0063 and 0064 add assignment and mutation for represented bit fields in four-byte storage units. Issue #25 remains open for non-four-byte field storage, partial volatile mutation, atomic ordering, broader values, production integration, and staged self-hosting.
