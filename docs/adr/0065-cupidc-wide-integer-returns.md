# Carry eight-byte integer returns through CupidC

## Context

The shared frontend already represents signed and unsigned eight-byte integer constants, matching conditional expressions, fixed direct and indirect calls, casts to `void`, and return conversion. Hosted IR still rejected those values at its four-byte scalar boundary. The unchanged `cfront_integer_mask` helper in `toolchain/cupidc_frontend.c` returns either `0xffffffffull` or `0xffffffffffffffffull`, so its ordinary source shape could not cross the hosted compiler path.

The Cupid i386 target returns an eight-byte integer in EDX:EAX. EAX holds the low word and EDX holds the high word. Truncating the result, splitting the source expression by hand, or changing the helper's return type would hide an active compiler requirement and would not advance the target ABI.

## Decision

Hosted CupidC carries a limited eight-byte integer value through constants, matching conditional arms, fixed direct and indirect call results, discarded expressions, casts to `void`, and returns. Signed and unsigned values use the same transport. This step did not add eight-byte parameters, lvalue loads or stores, conditions, arithmetic, mutation, variadic transport, or mixed-width conversions.

One wide value occupies one abstract Linear IR stack entry. The entry is a handle to an emitter-owned eight-byte frame snapshot. Constants write their low and high words into an instruction-owned snapshot. A direct or indirect call copies EAX and EDX into its own snapshot before later evaluation can overwrite either register. A return restores the low word to EAX and the high word to EDX. Discard removes the handle, and a same-width conversion adds no target instruction.

Private snapshot slots are assigned after ordinary locals in instruction order. Each value-producing instruction owns a distinct slot, including values created on different conditional arms. This keeps public IR target-neutral and lets its existing control-flow stack analysis continue to count one logical value. It also preserves the established sixteen-byte call-alignment calculation because a wide value contributes one live handle, not two public machine words.

Publishing two abstract stack entries for one C value was rejected. It would make branch joins, calls, discard, and value identity depend on an i386 register pair. Publishing an address or a public word-pair record was also rejected because frame layout and target transport belong to emission. Enabling eight-byte parameters, lvalue access, arithmetic, or mixed-width conversion in the same step was rejected because each needs its own complete semantic and ABI contract.

Lowering and object emission remain transactional. At this decision boundary, a wide parameter received the focused ABI diagnostic. Wide arithmetic and mixed-width widening retain the unsupported-value diagnostic. Failed operations leave the frozen frontend unit and previously published output unchanged and allow another operation in the same job.

## Consequences and evidence

The IR contract guards the complete unchanged `cfront_integer_mask` helper and a focused six-function unit. It checks three full-width constants, one conditional, two direct calls, one indirect call, four wide returns, one discard, deterministic repeat lowering, and unchanged input. The original failures covered an eight-byte parameter, eight-byte addition, and a four-byte to eight-byte cast. ADR 0067 replaces the parameter failure with positive declared-parameter and call contracts.

The deterministic ELF32 proof emits 439 bytes of `.text` with fingerprint `181CA1EC`, seven symbols, three `R_386_PC32` relocations with addend `-4`, and one `R_386_32` relocation with addend zero. Shared decoding checks constant and call-result snapshots, direct and indirect relays, discard, and call alignment. A small i386 execution oracle applies the object's relocations in memory, runs both conditional-mask outcomes, and carries both outcomes through the direct and indirect relays. It also checks the literal result `0x1122334455667788`, nested call and return stack effects, EBP restoration, and the final EDX:EAX pair.

This remains hosted bootstrap evidence. GCC or Clang still builds the shared compiler, its contracts, and every normal C object. The private in-kernel CupidC compiler remains the runtime JIT and AOT path. No production object, build transform, boot path, host dependency, or ownership count changes.

Issue #25 remains open. Eight-byte values without declared parameter types, conditions, arithmetic, conversions, mutation, production integration, staged self-hosting, and the fixed-point bootstrap remain unfinished.

## Extension: eight-byte integer objects

ADR 0066 carries the same one-handle value through file, block-static, automatic, pointer-derived, member, and indexed lvalues. A wide `LOAD` owns an eight-byte snapshot. Plain and chained stores copy from that snapshot, while return still crosses the i386 boundary in EDX:EAX. Atomic access remains rejected.

## Extension: eight-byte integer parameters

ADR 0067 uses the same snapshot handle for declared wide parameters and named call arguments. The caller copies eight bytes into the outgoing cdecl area, and the callee copies the parameter into a new value snapshot. Values passed through an ellipsis or an unprototyped call remain unsupported.

## Extension: wide operations and mixed-width conversion

ADR 0068 gives wide shift, AND, OR, XOR, widening, and narrowing results their own snapshots. Public IR still carries one logical value, and the return path still restores EDX:EAX. Wide arithmetic outside that named set remains open.
