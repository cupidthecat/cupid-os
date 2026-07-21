# Lower 32-bit integer shifts and bitwise OR through CupidC IR

## Context

The hosted CupidC path reaches this unchanged helper in `kernel/crypto/aes.c`:

```c
static uint32_t rotw(uint32_t w) { return (w << 8) | (w >> 24); }
```

The frontend already types this expression under Cupid i386 rules. Each shift retains the promoted left operand type, but its count is promoted independently. In `w << 8`, the left operand is unsigned `uint32_t` and the count is signed `int`. The earlier IR check required both binary operands to have the same type, which is correct after the usual arithmetic conversions but wrong for shifts.

## Decision

The existing `CTOOL_C_IR_INSTRUCTION_BINARY` record accepts left shift, right shift, and bitwise OR for represented four-byte integers. A shift requires a four-byte integer value and count, and its result must have the left operand's promoted type. IR keeps that type in `input_type`; it does not require the count to share the type or add target register details to the public record. Bitwise OR keeps the existing same-type check because the frontend applies the usual arithmetic conversions to both operands.

The i386 emitter pops the count into ECX and the left value into EAX. Left shift uses `SHL EAX, CL`. Right shift uses `SAR EAX, CL` for signed input and `SHR EAX, CL` for unsigned input, which fixes Cupid i386's implementation-defined signed right-shift behavior as arithmetic. Bitwise OR uses `OR EAX, ECX`. Every instruction still passes through the shared x86 encoder.

CupidC does not add a runtime check for a count outside 0 through 31. C leaves negative and oversized shift counts undefined, so the processor's masked count is an allowed execution result. This increment keeps one `CL` path because semantic IR does not retain constant-folding facts and the same form handles constant and runtime counts. A later optimizer may select the shorter immediate form without changing IR.

## Consequences and evidence

The IR contract guards the complete unchanged `rotw` helper and pins ten instructions with a maximum abstract-stack depth of three. Its two integer count instructions remain signed `int`, while both shifts and the final OR retain unsigned `uint32_t`.

The object contract emits the exact 53-byte `rotw` function and a 33-byte signed right-shift fixture. The 86-byte text section has one local symbol, one global symbol, and no relocations. Shared decoding checks `SHL`, unsigned `SHR`, signed `SAR`, and `OR`; repeated emission is byte-identical.

Separate 64-bit left-shift, right-shift, and bitwise-OR fixtures receive the unsupported-type diagnostic. Bitwise XOR remains the unsupported represented four-byte binary operation and still produces no partial IR or object.

This is hosted bootstrap evidence. The host C compiler still produces the normal root and user C objects. The private in-kernel compiler remains the embedded runtime JIT and AOT path. No production artifact, build owner, host dependency, boot path, or runtime ABI changed.

Later decisions close several frontiers that were open here. ADRs 0063 and 0064 add assignment and mutation for represented bit fields in four-byte storage units. Issue #25 remains open for non-four-byte field storage, partial volatile mutation, atomic ordering, broader values, production integration, and staged self-hosting.
