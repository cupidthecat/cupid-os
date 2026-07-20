# Align hosted CupidC i386 call sites to sixteen bytes

## Context

Cupid OS uses an i386 cdecl ABI with a sixteen-byte stack boundary at each call instruction. The hosted CupidC emitter already handled scalar and structure arguments, direct and indirect calls, nested calls, fixed automatic frames, and hidden structure-result pointers. It did not add call-specific padding, so the stack position depended on the frame size and the number of live Linear IR values.

That gap blocks active Toolchain source. Calls appear while earlier expression values are still live, and fixed structures add outgoing spans that do not always preserve a sixteen-byte boundary. Rewriting those expressions or changing their types would hide the ABI requirement instead of teaching CupidC to meet it.

## Decision

Hosted i386 object emission now requires ESP to be congruent to zero modulo sixteen immediately before every `CALL`. The contract assumes a conforming caller, so ESP is congruent to twelve modulo sixteen on function entry. After `PUSH EBP`, the emitter accounts for the fixed frame, each live four-byte semantic stack entry, and any structure-call outgoing block. It then reserves zero, four, eight, or twelve padding bytes.

The public Linear IR does not gain an ABI padding instruction. A target-private control-flow pass computes the semantic stack depth before each reachable instruction from the instruction's consume and produce counts. Both sides of a branch must reach their target with the same depth, and no reachable instruction may underflow or exceed the function's published maximum depth. This keeps target stack layout out of the target-neutral interface while giving each call the live-depth fact it needs.

Scalar calls already hold completed arguments on the machine stack. When padding is needed, the emitter reserves it and shifts the argument words toward the new stack top without changing their cdecl order. An indirect call leaves its saved callee below the original argument area and loads it through the adjusted offset. Cleanup removes the arguments, padding, and saved callee as appropriate.

Structure-aware calls include the hidden result word, scalar slots, rounded structure spans, and alignment padding in one reservation calculation. The existing zeroing and copy rules still apply only to the outgoing ABI block. The caller removes explicit arguments, semantic handles, and padding; a structure-returning callee still removes the hidden result word with `RET 4`.

The emitter rejects inconsistent stack joins, malformed call types, arithmetic overflow, and output exhaustion transactionally. It publishes no partial object, rewinds operation storage, and leaves the frozen translation unit reusable.

The function's published maximum stack depth cannot stand in for a call's live depth because nested and branched expressions reach calls at different points. A sixteen-byte frame alone is also insufficient: arguments and other live values change ESP after the prologue. Public Linear IR does not need a padding opcode because alignment is an i386 ABI emission detail, not a C semantic operation.

## Consequences and evidence

The focused contract covers direct zero- and one-argument calls, a nested direct call, an indirect scalar call, a joined conditional, a loop back edge, and direct and indirect structure calls. Together they exercise padding amounts of zero, four, eight, and twelve bytes. A decoder builds an instruction-boundary control-flow worklist, rejects unmodeled ESP or EBP changes, checks every reachable call, and requires the expected conditional, join, and back-edge shapes. A separate symbolic oracle proves that three constant arguments keep cdecl order after a twelve-byte padding shift. Repeat emission is byte-identical. A constrained output buffer fails with `CTOOL_ERR_LIMIT`, leaves the output empty, and is followed by a successful same-job retry.

Alignment changes the bytes and relocation offsets of earlier call-bearing object proofs. Their refreshed exact oracles cover the combined function object, pre-test and post-test loops, function pointers, automatic objects, narrow calls, casts to `void`, and structure calls. The structure-value object remains 928 text bytes, with FNV fingerprint `31D58B50` after its call padding changed.

This is still hosted bootstrap evidence. GCC or Clang builds the emitter and its contracts, and no normal Cupid OS C object has moved to this path. Issue #25 remains open for variadic calls, direct wide and floating runtime values, the deferred aggregate forms, whole-unit production integration, staged self-hosting, and the fixed-point bootstrap.

This decision closes the call-alignment boundary recorded by ADR 0017, ADR 0043, and ADR 0049. Their original scope statements remain part of the historical record.

## Extension: scalar variadic calls

ADR 0054 applies the same alignment pass to scalar variadic calls. The public call instruction now retains the actual argument count, so stack-depth analysis and padding no longer assume that the function type's named parameter count describes the full call site. Fixed-call behavior is unchanged.
