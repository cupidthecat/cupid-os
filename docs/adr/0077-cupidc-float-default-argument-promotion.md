# Promote float arguments to double through hosted CupidC

- Status: Accepted
- Date: 2026-07-22

## Context

C requires a `float` in an ellipsis or a call through a function type without a prototype to arrive as `double`. ADR 0076 could transport a value that was already `double`, but it rejected a source `float` at both call boundaries. The unchanged Doom configuration writer in `kernel/doom/src/m_config.c` passes a loaded `float` to `fprintf`, so the missing conversion blocks active source without presenting a reason to rewrite that source.

The supported conversion must change the value, its Linear IR type, and its i386 argument width together. Treating the four source bytes as an eight-byte argument would read unrelated stack data. Relabeling the existing four-byte value as `double` would give the emitter a snapshot handle that does not name a converted value.

## Decision

The shared frontend applies ordinary lvalue conversion first. When default argument promotions then see `float`, the frontend publishes an implicit conversion with kind `CTOOL_C_CONVERSION_FLOAT_PROMOTION` and the plain target `double` type. This conversion is limited to ellipsis arguments and arguments whose function type has no prototype. A named parameter still uses assignment conversion and retains the existing unsupported diagnostic for mixed floating kinds.

Linear IR accepts this conversion only when its source is a represented, non-atomic `float` value and its result is a represented, non-atomic `double` value. It emits one typed `CONVERT` instruction. The call's packed actual-type slice therefore records `double`, and the existing call validator, alignment calculation, argument placement, and cleanup all use an eight-byte cdecl slot.

The i386 emitter loads the four-byte source with x87 `FLD`, removes that semantic stack slot, and stores the converted result with `FSTP` into a fresh private eight-byte snapshot. It then pushes the snapshot handle used by the existing `double` call path. No live x87 value crosses another Linear IR instruction.

This is the only supported value-producing floating conversion. Fixed-parameter `float` to `double` conversion, explicit floating casts, arithmetic conversions, floating literals, arithmetic, comparisons, conditions, and conditional expressions remain unsupported. `va_arg(float)` remains invalid C because an unnamed source `float` arrives as `double`.

Malformed conversion metadata and storage limits remain transactional. A failed lowering or emission publishes no partial result, preserves its borrowed input, rewinds temporary storage, and permits the same job to recover.

## Rejected alternatives

Copying the source word into the low half of an eight-byte slot would not produce an IEEE-754 `double`. It would create unrelated bits whose value depends on the adjacent word.

Performing the promotion only while laying out a call would hide a C conversion inside the target backend. The explicit frontend and Linear IR record keeps type checking, call metadata, and target emission consistent.

Keeping the promoted result live in x87 `ST0` until the call would couple argument evaluation and placement to x87 stack depth. The existing snapshot model gives the converted value the same lifetime rules as every other represented `double`.

## Consequences and evidence

Frontend contracts cover direct and indirect variadic calls plus direct and indirect unprototyped calls. They require lvalue conversion before the new promotion and keep conversion from a `float` argument to a named `double` parameter as a useful unsupported boundary. Linear IR contracts require one `float` to `double` conversion for each call form, check packed actual types, reject malformed conversion records, repeat lowering deterministically, and prove same-job recovery.

The deterministic ELF32 contract executes all four call forms with positive zero, negative zero, a value just above one, the smallest positive subnormal, and positive infinity. Its decoder-driven oracle models the exact widening and checks cdecl stack restoration and call alignment. The oracle is not a native x87 execution proof.

This capability changes no build owner. GCC or Clang still builds the shared compiler, its contracts, and every normal Cupid OS C object. No production transform or host dependency moves.

Issue #25 remains open for general floating conversion and computation, the remaining C and ABI surface, production integration, staged self-hosting, and the fixed-point bootstrap.
