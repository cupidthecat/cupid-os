# Lower fixed direct calls through CupidC linear IR

## Context

ADR 0016 established typed linear IR and deterministic `.text` emission for the first leaf functions. The next unchanged active-source boundary is `syscall_getpid` at `kernel/core/syscall.c:61`. Its return expression calls `process_get_current_pid` directly. Supporting that source requires a stable call identity, cdecl argument placement, an ELF function symbol, and a PC-relative relocation. Rewriting the wrapper or bypassing the shared x86 and ELF32 modules would hide those requirements instead of implementing them.

The private in-kernel CupidC compiler evaluates call arguments from left to right, then reorders the completed four-byte stack slots before it emits `CALL`. The shared path must preserve that behavior so Cupid mode does not acquire a new evaluation order during migration.

## Decision

Linear IR adds one public `CALL_DIRECT` instruction. Its `reference` is the canonical file-binding index of the named function. Its `input_type` is the function type, and its `type` is the call result type. The function type already owns the fixed parameter list, so the instruction does not duplicate argument counts or types in a second public table.

Lowering accepts the frozen direct-call AST shape: a call whose callee is a function-to-pointer conversion over an identifier bound to a function. It rejects indirect callees through the existing unsupported-expression diagnostic. The current ABI boundary accepts a fixed, prototyped, nonvariadic function with 32-bit integer arguments and either a 32-bit integer or `void` result. Wider, floating, and aggregate call forms receive the ABI diagnostic.

Arguments lower in source order. Argument zero is deepest in the abstract stack, and the final argument is on top. `CALL_DIRECT` consumes the fixed arguments and pushes one result unless the result type is `void`. This keeps evaluation policy in the language-facing IR while leaving cdecl stack placement private to machine-code emission.

The body subset accepts one void expression statement in a void function, then emits `RETURN_VOID` for the implicit return at the closing brace. This makes a void direct call reachable from valid source without adding a dummy value to the IR. A discarded nonvoid expression and a longer statement list remain outside this slice.

The emitter reverses only the completed argument slots for the current call. It swaps pairs through ECX and EDX, leaving an odd middle slot and any older abstract-stack values untouched. It then emits a symbol reference through `ctool_x86_encode`, restores ESP after the call, and pushes EAX for a non-void result. Sixteen-byte call-site alignment remains outside this slice and must be added before the complete active ABI can transfer.

The x86 encoder supplies the four-byte PC-relative field and its encoded addend of `-4`. The emitter maps the call binding to its local or unresolved external `STT_FUNC` symbol, patches the REL field, and records an `R_386_PC32` relocation in `.rel.text`. It counts direct-call relocations before allocating the relocation table. The shared ELF32 writer still owns serialization, and CupidLD still owns final symbol resolution.

Lowering continues to use count and fill passes. A failed direct call clears the result, restores the arena mark, retains its structured diagnostic, and leaves the borrowed translation unit unchanged. Object emission keeps its existing empty-output and rollback guarantees.

Lowering arguments in reverse order was rejected because it would change the current Cupid evaluation order. Adding a public call side table was also rejected because the function type and canonical binding already carry the required identity and signature. Directly writing `E8` or constructing ELF relocation bytes in the emitter was rejected because ADRs 0007 and 0002 assign those facts to the shared x86 and ELF32 modules.

## Consequences and evidence

The IR contract loads the unchanged syscall wrapper and pins its call to `process_get_current_pid`. Separate fixtures cover an unresolved two-argument call, a local call, and a one-argument void call followed by an implicit return. They check source-order argument instructions, exact call fields, stack depth, immutable input, and stable source locations. Indirect, variadic, and 64-bit calls fail transactionally with useful diagnostics. A discarded nonvoid expression statement pins the narrow body boundary.

The object contract emits local and unresolved external calls, reads the object back, and checks every call byte, function symbol, relocation offset, `R_386_PC32` type, and `-4` addend. Its nested three-argument case calls another represented function for the middle argument. That proves a nested call leaves older stack values alone and that odd-arity reversal preserves the middle slot. The void-call oracle proves caller cleanup without an EAX result push. Repeat emission remains byte-identical, and constrained output still rolls back cleanly.

This decision transfers no production build ownership. GCC or Clang still builds the hosted modules and contracts and produces the normal root and user C objects. The private in-kernel CupidC path remains the embedded runtime JIT and AOT path. No kernel object, image, boot path, or runtime ABI changed. ADR 0018 later adds the first automatic 32-bit integer locals without changing this call contract. Issue #25 remains open for broader statements and expressions, other local forms, indirect and variadic calls, 16-byte call-site alignment, floating and aggregate ABI work, production integration, and staged self-hosting.

## Extension: structure calls

Fixed direct calls later gained complete supported structure parameters and results. Source-order evaluation still publishes one completed handle per argument. When any structure participates, the emitter reserves one outgoing block, zeros it for deterministic padding, and copies arguments into cdecl order. A structure argument occupies its inline object size rounded to four bytes. A structure result adds a hidden destination pointer before the explicit arguments; the callee reads it at `EBP + 8`, starts explicit parameters at `EBP + 12`, returns the pointer in EAX, and removes the hidden slot with encoded `RET 4`. The caller removes only the explicit outgoing argument bytes plus its saved IR handles. Scalar-only calls keep the original byte path. Variadic calls, floating calls, 16-byte call-site alignment, and aggregate categories outside the supported structure slice remain open. This extension changes no production owner.

## Extension: eight-byte integer results

ADR 0065 lets a fixed direct call return an eight-byte integer without changing the parameter boundary established by this decision. The caller snapshots EAX and EDX into instruction-owned frame storage and leaves one logical value handle. A later return restores that pair. ADR 0066 adds object loads and plain stores for the same handle. ADR 0067 adds declared wide parameters and named call arguments. ADRs 0068 through 0074 add mixed-width conversion, arithmetic, control flow, and mutation. Wide values without a declared parameter type remain open.

ADR 0068 later adds wide shifts, AND, OR, XOR, and conversion to or from represented integer widths. These operations keep the existing call-result snapshot and do not change direct-call relocation or cleanup rules.

## Extension: floating direct calls

ADR 0076 gives fixed direct calls four-byte `float` slots and eight-byte `double` slots. Floating results use x87 `ST0`; the caller immediately stores them as a raw `float` value or private `double` snapshot. Direct-call relocation, source-order evaluation, alignment, and cleanup rules do not change.
