# Preserve plain assignment values in CupidC IR

## Context

The hosted CupidC path could load a linked file object, but it could not lower the unchanged setter in `drivers/vga.c`:

```c
void vga_set_vsync_wait(bool enabled) { vga_wait_vsync = enabled; }
```

The frontend already records a plain assignment as a raw destination designator and a converted source value. The assignment itself also has a value. That matters even when a statement discards it, because the same value can feed another assignment or a return expression. The existing `STORE` instruction consumes an address and value without leaving a result, so using it for assignment would lose this C behavior.

Atomic assignment needs a separate decision. A plain x86 `MOV` can transfer the represented four-byte value, but this slice has no IR contract for C11 atomic ordering. Accepting `_Atomic` here would claim semantics that the emitter does not yet describe or test.

## Decision

Linear IR adds `STORE_VALUE` and `DISCARD`. `STORE_VALUE` consumes the value on top of the abstract stack and the destination address below it, performs one store, and pushes the stored assignment result. The destination's semantic type remains on the address stack entry until the store. `DISCARD` consumes one nonvoid value and produces nothing.

Plain assignment lowering accepts represented, complete four-byte integer destinations, source values, and results. It lowers the raw destination once, then the converted source once. The emitted `STORE_VALUE` preserves the result for an enclosing assignment, return, or other supported value context. Right-associative assignment therefore works without evaluating either destination twice.

A supported void function body may contain a direct declaration prefix followed by one expression statement. If the expression has a nonvoid result, lowering emits `DISCARD` before the implicit `RETURN_VOID`. Void expressions keep their existing path. The declaration prefix follows the same complete four-byte automatic-object rules as the return-body subset.

The i386 emitter implements `STORE_VALUE` by popping the value into ECX, popping the address into EAX, writing ECX through EAX, and pushing ECX. `DISCARD` pops its value into EAX. Both paths use the shared x86 instruction model. A linked destination still uses `FILE_ADDRESS`, so its address field receives the existing direct-symbol `R_386_32` relocation with addend zero.

Atomic, pointer, wide integer, floating, and aggregate assignment lowering remain unsupported. Compound assignment and update expressions also remain unsupported because they require their own read, computation, conversion, store, and prefix or postfix result rules. ADR 0021 supplies direct ordinary member addresses, but bit-field, subscript, and pointer-based destinations still need their own address or extraction forms. Malformed child references, result metadata, and stack shapes are invalid frozen units rather than deferred language features.

Changing `STORE` to preserve every stored value was rejected. Automatic initializers use `STORE` as a side effect and should not gain a new stack result. Lowering assignment as a side effect only was rejected because it breaks chained and returned assignments. Direct AST-to-x86 emission and hard-coded bytes were rejected because IR, the shared x86 encoder, and ELF32 relocation ownership are established seams. Atomic assignment through an ordinary store was rejected until the IR can state and test the required memory-order behavior.

## Consequences and evidence

The source-driven IR contract lowers the unchanged VGA setter to six instructions with a maximum abstract-stack depth of two: `FILE_ADDRESS`, `PARAMETER_ADDRESS`, `LOAD`, `STORE_VALUE`, `DISCARD`, and `RETURN_VOID`. Its exact object is 27 text bytes with the local four-byte `vga_wait_vsync` object in `.bss` and one `R_386_32` relocation at text offset 4.

A separate `set_both` fixture returns `first_state = second_state = value`. Its seven-instruction stream has a maximum depth of three and contains two `STORE_VALUE` instructions before `RETURN_VALUE`. The exact object is 37 text bytes with two global four-byte objects in `.bss` and direct-object relocations at offsets 4 and 9. Repeated emission is byte-identical. A plain `1;` expression statement independently proves that `DISCARD` is not tied to assignment. Another void fixture declares a volatile automatic integer, chains assignment through that local and the function parameter, then discards the result. Its eight instructions prove the declaration-prefix expression body, `LOCAL_ADDRESS`, `PARAMETER_ADDRESS`, qualifier-aware type matching, two result-preserving stores, and the implicit void return together.

Negative contracts cover atomic, pointer, wide integer, and compound assignments. Mutated-unit cases cover a mismatched assignment computation type, an out-of-range child reference, and a value-producing expression substituted for the destination address. The existing transactional, constrained-output, rollback, and recovery checks continue to apply.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared frontend, IR, emitter, x86, and ELF32 modules and their contracts. The host C compiler still produces the normal root and user C objects. The private in-kernel CupidC path remains the embedded runtime JIT and AOT path. No production artifact, ABI owner, build transform, host dependency, boot path, or runtime behavior changes here.

ADR 0021 closes the direct ordinary member-address frontier named above. ADR 0022 adds represented four-byte bit-field reads without treating them as addresses. ADR 0063 extends the same value-preserving assignment rule to represented four-byte bit fields, and ADR 0064 adds compound assignment and prefix or postfix update. Issue #25 remains open for non-four-byte field storage, partial volatile mutation, atomic ordering, other value widths, production integration, and staged self-hosting.

## Extension: structure assignment

`STORE` and `STORE_VALUE` later gained complete-object semantics for supported structures. A structure lvalue conversion first creates an instruction-owned snapshot. `STORE` copies that snapshot into the destination and leaves no result. `STORE_VALUE` performs the same copy and preserves the source snapshot handle, which supports chained assignments, returned assignments, and whole-record expression initialization without evaluating a destination twice. The emitter uses `CLD` plus `REP MOVSB` while preserving ESI and EDI. Union, Cupid class, array-value, over-aligned, volatile, and atomic assignment remain unsupported. This hosted extension moves no production C object and retires no host dependency.

## Extension: floating assignment

ADR 0076 applies `STORE` and `STORE_VALUE` to same-kind `float` and `double` values. Assignment evaluates the destination once and preserves the stored value when the expression needs a result. The operation copies bytes only. Mixed floating kinds, integer and floating conversion, compound assignment, updates, and atomic access remain unsupported.
