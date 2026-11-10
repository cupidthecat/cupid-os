# Lower represented bit-field mutation through one record address

## Context

The shared CupidC frontend already typed compound assignment and prefix or postfix update on bit fields. Linear IR still stopped at those nodes. An ordinary mutation keeps an object address for the final store, but C does not give a bit field its own address. The lowerer must keep the record address and the selected graph member instead.

No unchanged active expression currently mutates a bit field. Doom supplies the active four-byte layout, read, and plain-assignment shapes, while issue #25 requires the remaining bit-field semantics needed for a complete freestanding C11 path. This increment closes that existing negative-contract seam without changing Doom or claiming production ownership.

On July 21, 2026, the user approved this mutation slice as a narrow exception to the repository's active-source sequencing rule. The exception applies only to the represented bit-field mutation work described here. Later toolchain work still needs an unchanged active-source requirement unless the user approves another exception.

Bit-field mutation also has two value rules that cannot be inferred from the replacement alone. A narrow `unsigned int` bit field promotes to signed `int` when every field value fits in `int`. A postfix update returns the extracted value from before the store, even when the replacement wraps at the field width.

## Decision

The represented slice covers all ten integer compound assignments and all four prefix or postfix updates for non-atomic integer bit fields in four-byte storage units. The field width may be from one through 32 bits, and the complete unit must fit inside its record.

Lowering evaluates the record designator once. `DUPLICATE_ADDRESS` may duplicate a complete record address so one copy feeds `BIT_FIELD_LOAD` and the other survives for the store. The loaded field follows target-aware integer promotion, the requested operation, and assignment conversion before it reaches the existing `BIT_FIELD_STORE_VALUE` instruction.

A width-aware promotion `CONVERT` retains the absolute graph-member index. This lets the i386 emitter validate the otherwise unusual conversion from a narrow `unsigned int` field to signed `int`. A 32-bit unsigned field keeps its declared type and needs no such conversion.

Postfix updates use a new `BIT_FIELD_STORE_OLD_VALUE` instruction. It consumes the record address, the extracted old field value, and the converted replacement. It stores the replacement once and leaves the old value on the abstract stack. Keeping the old value directly avoids trying to reverse an update after width truncation.

A partial field currently performs two complete-unit reads and one complete-unit store. The first read extracts the operand; the second belongs to the final read-modify-write merge that preserves neighboring fields. This is valid for nonvolatile storage. Partial volatile mutation remains unsupported because it would expose the extra read. A volatile 32-bit field uses one read and one direct store, so that case is represented.

## Consequences and evidence

The focused Linear IR contracts cover exact prefix, postfix, and compound sequences, all ten compound operators, target-width unsigned promotion, signed fields, one record-address evaluation, and abstract stack depths of three or four. Full-width volatile prefix and postfix fixtures prove the one-read, one-store path.

The deterministic ELF32 contract contains 20 functions, 1,415 text bytes, 21 symbols including the null symbol, and no relocations. A decoder checks every function span, every relevant indirect load and memory store, and full text coverage. Its i386 execution oracle covers all ten compound operators, signed and unsigned wraparound, prefix and postfix results, preservation of neighboring bits, full-width unsigned wraparound, stack restoration, argument preservation, and poisoned memory guards. An indexed postfix case uses `states[(*index)++].value++`; it returns the old field value, updates the selected element, and advances the index exactly once.

Character-sized, Boolean, atomic, compact packed, and partial volatile mutations keep the unsupported-type diagnostic. Mutated graph and layout widths receive the invalid-unit diagnostic without publishing partial IR. Recovery on the original frozen unit produces the same instruction fingerprint.

Reconstructing the postfix result from the replacement was rejected because narrow-field wrap loses the old value. Treating every `unsigned int` field as an ordinary unsigned operand was rejected because C promotion depends on the field width. Reusing an ordinary member address remains invalid because a bit field has no address in C.

This is hosted bootstrap evidence. GCC or Clang still builds the shared compiler and its contracts. No normal Cupid OS object, build transform, ABI path, or host dependency changes owner. Issue #25 remains open for the rest of the C surface, production integration, staged self-hosting, and the fixed-point bootstrap.
