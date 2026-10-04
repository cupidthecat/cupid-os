# Carry structure values through CupidC IR and i386 cdecl

## Context

The shared frontend already represents compatible structure values in assignment, automatic expression initialization, fixed arguments, return statements, conditional expressions, and casts to `void`. ADR 0044 gives fixed automatic structures real frame storage, while ADR 0048 initializes array and structure lists. Hosted IR and object emission still stopped when a structure crossed the address-to-value boundary.

This was the first body-lowering boundary in each hosted Cupid Toolchain source root. The source needs ordinary structure copies and calls, including structures whose members are not otherwise represented as scalar runtime values. Replacing those objects with pointers or splitting them into hand-written scalar operations would change the source to fit the compiler and would lose the ABI work needed for self-hosting.

Local Clang and GCC i386 oracles agree on the memory ABI used here. A structure argument occupies consecutive stack bytes rounded up to four. A function that returns a structure receives a hidden destination pointer at `[EBP+8]`, so its first explicit parameter starts at `[EBP+12]`. The callee copies the result through that pointer, returns the pointer in EAX, and uses `RET 4` to remove the hidden word.

## Decision

Hosted CupidC carries a complete fixed-size `struct` value when its target alignment is a power of two no greater than four bytes and its inline object graph contains no volatile or atomic subobject. Unions and Cupid classes are not included. The structure may contain fixed arrays, bit fields, wide integers, or floating members because this path copies the complete target representation instead of evaluating those members separately.

The public linear IR keeps its existing instruction kinds. A structure lvalue conversion uses `LOAD`. The instruction copies the complete source object into emitter-owned frame storage and pushes one abstract structure value. `STORE` copies that value into a destination without a result. `STORE_VALUE` performs the same copy and preserves the source value for chained assignment or return. `DISCARD`, direct and indirect calls, conditional joins, automatic expression initializers, structure-valued leaves in aggregate initializer lists, and `RETURN_VALUE` use the same one-entry value model.

Each structure `LOAD` and each structure-result call owns a distinct private frame snapshot. Ordinary locals are allocated first in block-binding order. Snapshots follow in instruction order, using the structure's target size and alignment. The public IR contains neither a snapshot address nor an EBP offset. Treating a value as a borrowed source address was rejected because a later store could then change a value that C had already copied. Expanding every value into member instructions was also rejected because that would mishandle padding, bit fields, fixed arrays, and members whose scalar operations remain deferred.

Fixed direct and indirect calls still evaluate the callee and arguments in source order. If a call uses a structure parameter or result, the i386 emitter reserves one outgoing block in ABI order. Each scalar parameter occupies four bytes. Each structure parameter is copied inline and its slot is rounded up to four bytes. The emitter zeroes the outgoing block before filling it, which gives tail padding deterministic bytes without changing the copied object bytes.

A structure-result call places the address of its private result snapshot in the first outgoing word. The callee removes that hidden word with `RET 4`; the caller removes the explicit argument block and its internal evaluation handles. On entry to a structure-returning function, `PARAMETER_ADDRESS` accounts for the hidden word. `RETURN_VALUE` copies the selected value to the hidden destination and leaves that address in EAX. Direct calls keep their existing `R_386_PC32` relocation policy, and indirect calls use the same memory ABI through EAX.

Structure copies issue `CLD` and `REP MOVSB` while preserving ESI and EDI. Outgoing-area clearing uses `CLD` and `REP STOSB` while preserving EDI. The shared x86 catalogue owns the immediate-cleanup return form `RET imm16`, encoded as `C2 iw`; the emitter does not write those bytes directly. Calls that use only represented scalars retain the established scalar emission path.

Lowering and object emission remain transactional. Invalid graph references, incompatible structure identities, impossible layouts, malformed IR payloads, and frame-size overflow publish no partial result. A valid but unsupported union, volatile or atomic structure value, over-aligned structure, variadic call, or unrepresented ABI value receives the existing focused diagnostic.

Representing structure values as pointer-typed public IR was rejected because an object address and a copied value have different C semantics. A new structure-only instruction family was also rejected. The existing typed `LOAD`, `STORE`, `STORE_VALUE`, call, discard, and return records already state the required operations once their value semantics include an emitter-private snapshot. Hard-coding `C2 04 00` in CupidC was rejected because CupidASM, CupidDis, and every other x86 consumer must share the same instruction model.

## Consequences and evidence

The structure-value IR contract covers parameter loads, direct return, conditional selection, plain and chained assignment, automatic expression initialization, a direct call, an indirect call, and a discarded structure value. It checks the aggregate payloads on the reused instruction kinds and keeps transactional negative cases for union values, volatile structure loads, and eight-byte alignment.

The deterministic object proof covers three-byte, eight-byte, and twelve-byte structures mixed with scalar parameters. Its `.text` section is 928 bytes with 13 symbols, four `R_386_PC32` relocations, and FNV fingerprint `39A8B114`. Decoder checks pin rounded and zero-filled outgoing slots, `[EBP+8]` and `[EBP+12]` parameter placement, direct and indirect calls, preserved copy registers, caller cleanup, EAX result pointers, and the exact `RET 4` epilogue. Repeated emission is byte-identical and leaves the frozen translation unit unchanged.

The shared x86 contract encodes and decodes `RET 4` as `C2 04 00`, rejects an immediate outside 16 bits and a mismatched operand width, and detects truncated input. CupidASM assembles `ret 4` to the same bytes and rejects `ret 65536`. CupidDis renders `C2 04 00` as `ret 0x4`.

This remains hosted bootstrap evidence. The host C compiler still produces normal root and user C objects, and the private in-kernel CupidC compiler remains the runtime JIT and AOT path. No production artifact, build owner, boot path, or host dependency changes here.

Issue #25 remains open. Union and Cupid class values, volatile and atomic structure access, alignment above four bytes, wide arithmetic and values without declared parameter types, floating runtime values, production integration, staged self-hosting, and the fixed-point bootstrap remain. ADR 0050 later added sixteen-byte alignment for every represented direct and indirect call.

## Extension: structure compound literals

ADR 0052 lets a supported structure compound literal build an aggregate value in target-private staging and commit it to its persistent unnamed frame object. The existing lvalue-to-value path then applies. `LOAD` still creates a separate private snapshot before a call, assignment, conditional, or return uses the value. Staging, the persistent object, and a later value snapshot have distinct storage, so initialization can read the prior object and reevaluation cannot change a structure value that was already copied.

## Parallel eight-byte integer snapshots

ADR 0065 reuses the one-handle, emitter-private snapshot principle for an eight-byte integer result. It does not use the structure hidden-pointer ABI. The i386 scalar result remains in EDX:EAX until the caller copies both words into its private slot.

ADR 0066 applies the structure copy rule to wide integer lvalues and stores. Each `LOAD` owns an eight-byte snapshot. `STORE_VALUE` copies those bytes and keeps the source snapshot as the assignment result, which preserves value semantics across chains without borrowing an object address.

ADR 0067 passes a declared eight-byte integer in an eight-byte cdecl slot. It reuses the outgoing-area copy path but does not use the structure hidden-result rule. The callee copies the parameter into a wide snapshot before using it as a value.
