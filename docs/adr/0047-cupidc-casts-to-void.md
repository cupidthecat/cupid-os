# Lower explicit casts to void in hosted CupidC

## Context

The shared frontend retains an explicit cast to `void` as a typed `CAST` node. Active hosted Toolchain source uses this form for intentionally unused parameters. `ctool_host_allocate` casts `context` to `void`. `ctool_host_release` does the same for `context` and `bytes`.

Hosted IR already lowers the represented operand types used by these helpers and already has `DISCARD` for unused values. It still rejected a cast whose result type was `void`. The same check rejected `(void)sink()` even though a void call leaves no value to discard. Removing the casts from active source would hide an ordinary C requirement.

## Decision

Hosted CupidC accepts an explicit cast to `void` when it can lower the operand. Lowering records the incoming abstract-stack depth and evaluates the operand once. A represented integer, object pointer, or function pointer must leave exactly one matching value. `DISCARD` consumes that value and retains its operand type and source location. A `void` operand must leave the stack at its incoming depth and adds no discard instruction. The cast finishes at the recorded depth and produces no value.

The i386 emitter uses its existing discard sequence for represented values. A void operand adds no target instruction beyond the code produced by the operand itself.

Casting a function pointer to `void` does not define a conversion between function pointers and another value category. Explicit function pointer casts that produce values remain unsupported. Wide integer, floating, aggregate, and atomic operands also remain unsupported where their evaluation cannot yet lower. A cast to `void` cannot skip that evaluation.

## Consequences and evidence

The complete unchanged `ctool_host_allocate` and `ctool_host_release` functions guard the active requirement. Their two functions publish 18 IR instructions, including three typed discards and two direct calls. A focused function covers represented integer, object pointer, function pointer, volatile byte, scalar-call, and void-call operands in 16 exact IR instructions with a maximum abstract-stack depth of one.

The deterministic object contract emits `discard_values` in 46 exact text bytes. The object has three symbols and one `R_386_PC32` relocation at text offset 40. The relocation targets `sink` with addend `-4`. Shared decoding checks all 23 i386 instructions, and repeated emission is byte-identical.

The first focused IR test stopped at the former unsupported-type diagnostic for `(void)sink()`. That was the red result for this increment. Wide, floating, aggregate, and atomic operand fixtures keep their existing unsupported-type diagnostic and transactional rollback. A copied cast node with an operator payload receives the invalid-unit diagnostic without changing the frozen frontend unit.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared frontend, IR, emitter, x86, ELF32, and contract modules. The host C compiler still produces the normal root and user C objects. The private in-kernel CupidC compiler remains the runtime JIT and AOT path. No production artifact, build transform, host dependency, boot path, runtime behavior, or ABI owner changes here.

Issue #25 remains open for Boolean mutation, aggregate initialization and values, narrow bit fields, atomic access, 64-bit integers, floating values, variadic calls, call-site alignment, production integration, staged self-hosting, and the fixed-point bootstrap.

## Extension: discarded structure values

A later structure-value path allows a cast to `void` to consume a complete supported structure. Lowering still evaluates the operand once. Lvalue conversion captures the structure in an instruction-owned snapshot, then typed `DISCARD` removes its one abstract handle. This does not skip a copy or invent a scalar conversion. Union, Cupid class, array-value, over-aligned, volatile, and atomic operands remain unsupported at their normal evaluation boundaries. The extension remains host-built proof and changes no ownership or dependency count.

## Extension: discarded eight-byte integers

ADR 0065 allows a cast to `void` to consume an eight-byte constant or supported fixed-call result. ADR 0066 adds eight-byte lvalues. Lowering evaluates the lvalue once, captures its eight target bytes in a private snapshot, and discards that one handle.
