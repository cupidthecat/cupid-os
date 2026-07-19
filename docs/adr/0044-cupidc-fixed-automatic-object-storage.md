# Allocate fixed automatic objects in hosted CupidC

## Context

The shared CupidC frontend already publishes complete target layouts for automatic arrays and records. It also keeps their block-binding identities, member selections, array decay, and initializer forests. Hosted IR lowering stopped at an automatic aggregate declaration because the object emitter assigned every referenced local a four-byte slot.

Active Toolchain source needs real aggregate storage. `cemit_build_sections` in `toolchain/cupidc_emit.c` uses `ctool_u32 section_map[CEMIT_SECTION_COUNT]`, and `cir_lower_conditional` in `toolchain/cupidc_ir.c` uses `ctool_u32 children[3]`. Rewriting either array as separate scalar variables would hide a normal C requirement from the compiler roadmap.

The existing IR already has the required address operations. `LOCAL_ADDRESS` names an absolute frontend block binding, `ARRAY_TO_POINTER` records array decay, and `MEMBER_ADDRESS` applies a record member identity. A new aggregate-specific instruction would repeat type and layout facts that are already present in the frozen translation unit.

## Decision

Hosted IR lowering accepts an uninitialized automatic array or record when it is a complete fixed object with nonzero target size, power-of-two alignment, and alignment no greater than four bytes. Represented scalar locals keep their existing behavior. Aggregate initializer forests remain unsupported by this lowering path.

`LOCAL_ADDRESS` continues to publish the absolute block-binding index. Frame offsets stay private to the i386 emitter. The emitter first marks every binding referenced by `LOCAL_ADDRESS`, then allocates those objects in ascending block-binding order. Each offset is rounded so that `EBP - offset` meets the object's target alignment. The final frame size is rounded to four bytes.

This order is deterministic and independent of instruction order. It also keeps public IR free of target frame policy. Unreferenced fixed objects do not receive a slot because they have no runtime initializer or observable address in this slice.

The emitter rejects any object that cannot fit in a positive signed i386 frame displacement. Size and addition checks run before arithmetic that could wrap. An oversized frame returns the object-emission limit diagnostic, leaves caller output empty, preserves the frozen unit, and rewinds operation storage.

This decision does not claim 16-byte call-site alignment. Automatic objects that require more than four-byte alignment remain unsupported. Narrow arrays may decay to pointers and may be passed to represented calls, but narrow element loads and stores are still outside the current value path. Variable-length arrays, automatic aggregate initialization, aggregate assignment values, and aggregate call or return conventions also remain open.

## Consequences and evidence

The focused IR contract publishes 47 exact instructions across five functions. It covers a four-element integer array with indexed store and load, an eight-byte record with access to its second member, a three-byte character array passed by pointer, and a mixed frame containing both a three-byte array and an eight-byte integer array. A fifth function reproduces the active `&children[index]` shape through local storage, array decay, pointer arithmetic, dereference, address-of, and a direct call. The mixed function proves that its two block bindings retain distinct identities.

The deterministic ELF32 object contains five functions in 236 exact text bytes, nine symbols, and three `R_386_PC32` relocations. Exact prologues reserve 16 bytes for the integer array, 8 bytes for the record, a 4-byte frame for the 3-byte object, and 12 bytes for both the mixed frame and `children`. The mixed local addresses are EBP minus 3 and EBP minus 12. The active-shape call relocation starts at text offset 227. Repeated emission is byte-identical.

Negative contracts reject aggregate initialization, aggregate assignment, narrow indexed loads and stores, and an eight-byte-aligned automatic record at their existing unsupported boundaries. A zero-sized mutated layout fails as an invalid frozen unit. A referenced `unsigned char[4294967295u]` after another referenced local returns overflow with the public limit diagnostic and full transaction rollback.

Active-source guards pin the unchanged `section_map` and `children` arrays and their indexed uses. GCC or Clang still builds this hosted proof and still produces the normal OS C objects. The private in-kernel CupidC compiler continues to own embedded runtime JIT and AOT compilation. No production object, disk image, boot path, or host dependency changes ownership, so this decision makes no emulator claim. Issue #25 remains open for the remaining C and ABI work, production integration, staged self-hosting, and the fixed-point bootstrap.

## Extension: structure-value temporaries

The emitter later added private frame storage for structure snapshots and structure-returning call results. It allocates these instruction-owned slots after ordinary block-binding storage and in instruction order, using the same checked target size, alignment, displacement, and final four-byte frame rounding rules. `LOAD` copies an lvalue into its slot before publishing a value handle. A structure call passes its result slot through the hidden return pointer. Ordinary automatic object offsets remain deterministic and unchanged. The supported structure alignment is still at most four bytes; over-aligned structures and variable-length objects remain open. This hosted storage does not move a production artifact or dependency.

ADR 0051 keeps block statics out of this frame allocator. Their absolute block-binding identities map to local ELF symbols, while automatic objects alone receive EBP-relative storage.

## Extension: compound-literal objects

ADR 0052 gives each referenced block-scope compound literal one target-sized persistent automatic slot. Its absolute expression index, rather than a synthetic block binding, is the slot identity. An aggregate list root also receives a target-private staging slot of the same size and alignment, so initializer reads finish before the persistent object is replaced. The emitter places persistent slots after named automatic objects, staging slots after them, and instruction-owned structure snapshots last. Reaching the expression again reuses its slots and reruns the initializer. Recursion receives fresh slots with the rest of the function frame. The same size, alignment, displacement, overflow, and final frame-rounding checks apply.
