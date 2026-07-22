# Lower wide integer shifts, bitwise operations, and conversions through CupidC

## Context

ADRs 0065 through 0067 let hosted CupidC carry an eight-byte integer through values, objects, declared parameters, named arguments, and the EDX:EAX return boundary. The next active source boundary is the unchanged pair of little-endian buffer helpers in `toolchain/ctool.c`:

```c
bytes[index] = (ctool_u8)((value >> (index * 8u)) & 0xffu);
```

This expression needs more than parameter transport. It shifts an eight-byte value by a runtime four-byte count, converts `0xffu` to the wide unsigned type for bitwise AND, then explicitly narrows the result to one byte. Splitting the value into source-level words or rewriting the loop around hosted compiler limits would hide an ordinary C requirement.

## Decision

Hosted CupidC extends the existing `BINARY` and `CONVERT` records. No public IR kind, register pair, or target opcode is added.

A wide `BINARY` accepts left shift and signed or unsigned right shift when its left operand and result have the same eight-byte integer type and its independently promoted count is a represented four-byte integer. It also accepts AND, OR, and XOR when both operands and the result share the same wide type. The i386 emitter reads each value from its private snapshot and writes every result to a distinct eight-byte snapshot.

Defined shift counts from zero through 63 preserve the complete two-word value. The emitter masks the count to six bits, handles zero without entering the loop, and shifts one bit at a time with `SHL` and `RCL` for left shift or `SHR` or `SAR` and `RCR` for right shift. Cupid keeps arithmetic right shift for signed values, matching ADR 0026. C leaves a negative count or a count of at least 64 undefined, so this decision does not add a runtime diagnostic for those values.

`CONVERT` now carries represented one-byte, two-byte, or four-byte integers into an eight-byte snapshot for explicit and assignment conversion. Signed sources fill the high word from the sign bit; unsigned sources clear it. The usual arithmetic conversion may widen a represented four-byte integer when the other operand selects an eight-byte common type. This is the path used by `0xffu` in the active mask expression. It may also reinterpret `signed long long` as `unsigned long long` when C's rank and range rules select the unsigned type. The reverse label is rejected because usual arithmetic conversion does not turn an unsigned operand into the signed type at the same rank.

GNU mode can give an enum a `signed long long` or `unsigned long long` compatible type. Integer promotion accepts that enum only when the conversion target is its exact compatible wide type. This keeps the extension usable without letting arbitrary enum-to-wide metadata pass validation. A same-width conversion keeps the snapshot bits and emits no target instruction.

The reverse conversion truncates a wide snapshot to the destination lane. Signed byte and word results are sign-extended into their canonical 32-bit IR value, while unsigned results are zero-extended. A four-byte destination keeps the low word. Conversion to `_Bool` ORs both source words before normalization, so `0x0000000100000000` is true.

Each widening conversion and wide binary result owns a private frame slot. Public Linear IR continues to treat one C value as one stack handle. Lowering and object emission keep their existing transaction rules: a failure publishes no partial result, restores an empty output buffer, rewinds operation storage, and leaves the frontend unit unchanged.

Publishing two public 32-bit lanes was rejected because it would make value identity and control-flow joins depend on the i386 ABI. Adding `SHLD` or `SHRD` to the shared x86 surface was unnecessary for this boundary. Testing only the low word for Boolean conversion was incorrect. Rewriting the active helpers to extract source-level words was also rejected because the compiler should accept their normal C expression.

## Consequences and evidence

The complete unchanged bodies of `ctool_buffer_put_le64` and `ctool_buffer_patch_le64` now parse, lower, and emit together. Their object retains three external `R_386_PC32` calls: `ctool_bytes`, `ctool_buffer_append`, and `ctool_buffer_patch`. Full-body source guards prevent a smaller fixture from replacing either active requirement.

Focused IR contracts cover unsigned and signed widening from every represented lane used by the fixture, explicit casts from signed byte and unsigned word sources, explicit narrowing to byte, word, and doubleword lanes, Boolean conversion, a same-width assignment retype, runtime left and right shifts, signed right shift, mixed signed and unsigned wide AND, GNU wide-enum promotion, OR, XOR, and the active extraction expression. Repeated lowering is deterministic. A wide shift count remains a useful unsupported case and fails transactionally.

A relocated i386 oracle executes every defined shift count from 0 through 63 on values whose bits cross the word boundary. It checks signed and unsigned right shifts, left shifts, all three bitwise operations, mixed signedness, a wide enum, every extracted byte, implicit and explicit signed or unsigned widening, every represented narrowing width, same-width assignment conversion, and low-word, high-word, and zero Boolean cases. Mutated units prove that reverse same-rank usual arithmetic conversion and promotion to the wrong enum-compatible type both fail transactionally. The oracle also checks ESP, EBP, callee-saved EBX, argument slots, output-limit rollback, same-job recovery, and byte-identical repeat objects.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler, the contracts, and every normal C object. No production transform, OS object, boot path, or host dependency changes owner.

Issue #25 remains open. ADR 0069 later adds wide comparisons and scalar conditions, ADR 0070 adds wide addition, subtraction, and nonlogical unary operations, ADR 0071 adds full-width switch dispatch, ADR 0072 adds multiplication, and ADR 0073 adds division and remainder. Wide compound mutation, increment and decrement, values without declared parameter types, floating values, production integration, staged self-hosting, and the fixed-point bootstrap remain unfinished.
