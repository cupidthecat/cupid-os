# Lower automatic 32-bit integer locals into fixed EBP slots

## Context

ADR 0016 introduced typed linear IR, and ADR 0017 added fixed direct calls. The next source-driven gap is an automatic object whose initializer calls another function. Unchanged `drivers/vga.c` contains this declaration inside `vga_flip_ready`:

```c
uint32_t now = timer_get_uptime_ms();
```

The complete function is not part of this slice. Its return expression still needs a file-object load for `last_flip_ms` and a greater-than-or-equal operation. A focused fixture keeps the declaration unchanged, then uses already supported parameter loads, addition, and return so local storage can be tested without weakening the OS source.

## Decision

Linear IR adds `LOCAL_ADDRESS` and `STORE`. `LOCAL_ADDRESS.reference` is the absolute index of the frontend block binding. It pushes the object's address. `STORE.type` is the destination object type, `STORE.input_type` is the stored value type, and the instruction consumes the value above its destination address without producing another value. Frame offsets stay private to machine-code emission.

The supported body form is an outer compound statement with a source-ordered declaration prefix and one trailing return. Existing empty-void, single-return, and single void-expression forms keep their behavior. Each supported declaration must name a complete four-byte integer object with alignment no greater than four and storage spelled as none, `auto`, or `register`. An uninitialized object emits no initializer instructions. An expression initializer emits the local address, the existing expression IR, and a store. Block-static, narrow, wide, aggregate, incomplete, and over-aligned objects remain outside this boundary.

Lowering consumes the translation unit's block-binding table with one source-order cursor in both the count and fill passes. A binding becomes visible before its own initializer, which follows C's point-of-declaration rule. The current function also records its binding range and visible end. These checks reject forward references, references to another function's local, duplicate ownership, and unowned records without adding a public local table.

Before either lowering pass, a temporary owner map rechecks every initializer root and every child reached through a validated, concatenated `LIST` slice. Each initializer must have exactly one list edge, file definition, or block binding as its owner, and each child must precede its parent. The scratch map is rewound before result allocation. This keeps a caller-mutated frozen unit from aliasing one runtime expression across two locals or disguising an unowned record behind a dangling raw edge.

The emitter scans each function's `LOCAL_ADDRESS` instructions before writing the prologue. Referenced bindings receive four-byte slots in ascending absolute binding order. The first slot is `[EBP-4]`, the next is `[EBP-8]`, and so on. One `SUB ESP, frame_size` reserves the fixed frame. Unused uninitialized objects need no slot. `register` objects may use the same private stack representation because source code cannot take their address. The abstract expression-stack depth remains separate from fixed frame storage.

Runtime expression initializers now remain valid while the object emitter indexes the shared initializer table. A static definition that points at an expression still fails at static-data encoding with the existing diagnostic. This keeps automatic and static storage rules separate.

Exposing EBP offsets in public IR was rejected because those offsets belong to the target emitter. Reusing the parameter-address helper was rejected because it deliberately forces a 32-bit positive displacement, while local slots should use the encoder's shortest valid negative displacement. Reserving space from `maximum_stack_depth` was also rejected because temporary IR values already use real pushes and pops. Rewriting `vga_flip_ready` to avoid its local would have hidden the source requirement.

## Consequences and evidence

The source-driven IR fixture has three locals. The first keeps the unchanged VGA declaration, the second is a `register` object initialized from a parameter, and the third is an uninitialized `auto` object that is never referenced. The exact 13-instruction stream includes two initializer stores, later loads from the first two locals, addition, and return, with a maximum abstract-stack depth of two. The unused object contributes no instructions, so the stream remains 13 instructions.

The point-of-declaration contract separately lowers a self-initializer and mutates that reference to a later binding, which must fail as an invalid frozen unit. Another mutation makes the second function refer to the first function's local. That also fails without publishing partial IR.

The object contract pins the complete 63-byte source-driven function. Its frame reserves eight bytes, assigns the two referenced locals to `[EBP-4]` and `[EBP-8]`, and preserves the first destination address beneath the zero-argument call result. A second 64-byte oracle keeps a local destination beneath two source-ordered arguments while the emitter reverses and removes only the cdecl argument slots. A 17-byte oracle reads a referenced but uninitialized `auto` local from `[EBP-4]` without a store. The combined `.text` is 460 bytes with seven call relocations. The exact 20-symbol inventory contains no symbol named for any local. Repeat emission is byte-identical.

Negative contracts cover missing local, object-definition, initializer, and initializer-element tables; malformed layouts; initializer payloads and roots; duplicate and dangling initializer owners; a void call substituted as an initializer; forward and cross-function references; duplicate binding ownership across functions; narrow and wide integers; aggregates; block-static storage; constrained output; rollback; and same-job recovery. A valid GNU aligned typedef supplies the over-aligned four-byte integer boundary. A source-derived frontier case passes the new local declaration and then stops at the unsupported file-object load. The mixed static-data and local-function fixture also proves that runtime initializer records no longer interfere with static encoding.

This is hosted bootstrap evidence. GCC or Clang still builds the shared modules and contracts and produces the normal root and user C objects. The private in-kernel CupidC compiler remains the embedded runtime JIT and AOT path. No kernel object, disk image, boot path, runtime behavior, production owner, or host dependency changed. Issue #25 remains open for file-object loads, greater-than-or-equal and other expressions, source assignments, nested blocks and general statements, other local types and storage durations, call-site alignment, broader ABI work, production integration, and staged self-hosting.

## Extension

ADR 0019 closes the file-object load and greater-than-or-equal frontier named above. The complete unchanged `vga_flip_ready` body now reaches hosted IR and deterministic ELF32 object emission. ADR 0020 adds value-preserving plain assignment for represented four-byte integers and discarded nonvoid expression values. Broader statements, atomic and compound assignment, member and subscript destinations, other object types, production integration, and the other listed work remain open.

ADR 0036 removes the outer declaration-prefix restriction. The same represented automatic objects can now be declared in supported nested compound statements and in `for` initializers. The fixed-slot emitter and public IR records do not change.

ADR 0044 extends the same private frame policy to referenced, uninitialized fixed arrays and records. Their slots use the target layout's size and alignment, up to four-byte alignment, while `LOCAL_ADDRESS` continues to expose only the frontend binding identity. Automatic aggregate initialization, variable-length arrays, and over-aligned locals remain open.

ADR 0051 gives `LOCAL_ADDRESS` a storage-duration-aware target meaning. Automatic objects still map to private EBP-relative slots, while block statics map to deterministic local ELF symbols and `R_386_32` address relocations. A block static never receives a frame slot.

ADR 0066 admits eight-byte integer automatic objects under the same checked frame allocator. Declaration initialization and later lvalue conversion use private value snapshots, so the local slot and the copied value remain distinct.
