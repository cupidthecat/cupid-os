# Represent narrow integer values in hosted CupidC

## Context

The shared CupidC frontend and target layout already distinguish signed and unsigned one-byte and two-byte integers. Hosted IR lowering still stopped when those types became runtime values. The previous fixed automatic-object work could reserve space for a narrow array, but it could not load or store an element.

Active Toolchain source needs these values without changing its declarations. `asm_lower` in `toolchain/cupidasm.c` accepts and returns `char`. `x86_class_width` and `x86_set_memory_width` in `toolchain/x86.c` use one-byte and two-byte integer values in parameters, returns, record members, conditions, and conversions. Rewriting them around four-byte temporaries would hide normal C requirements from the bootstrap plan.

## Decision

Hosted CupidC represents complete one-byte, two-byte, and four-byte integer values as canonical 32-bit words on its abstract evaluation stack. A signed narrow value is sign-extended. An unsigned narrow value is zero-extended. `_Bool` is always zero or one.

The value path now accepts narrow parameters, automatic locals, linked file objects, ordinary record members, indexed elements, loads, stores, plain assignments, initializers, integer promotions, explicit and implicit integer conversions, scalar conditions, logical not, short-circuit logic, conditional expressions, fixed direct calls, fixed indirect calls, and function results. Arithmetic and comparisons still use the frontend's promoted computation types, so this change does not introduce narrow arithmetic instructions.

Loads use `MOVSX` or `MOVZX` from an 8-bit or 16-bit memory operand. Stores use the low byte or word of ECX and write exactly one or two bytes. A conversion to `_Bool` tests the full source word before producing zero or one. Other narrow conversions canonicalize the low AL or AX lane according to the destination signedness.

Fixed cdecl arguments still occupy four-byte stack slots. A caller canonicalizes a narrow result from AL or AX after either a direct or register-indirect call. A callee canonicalizes its return value before `LEAVE` and `RET`. Keeping the same four-byte slot shape preserves the existing call cleanup and indirect-callee layout while retaining the declared parameter and result types in IR.

Narrow mutation remained outside this decision when it was accepted. ADR 0046 later extends compound assignment and prefix or postfix updates to represented, non-Boolean byte and word objects. Narrow bit-field storage, atomic access, 64-bit integers and floating-point values, aggregate values and initialization, variadic calls, casts involving function pointers, object-pointer casts to or from narrow integers, pointer-to-`_Bool` casts, and 16-byte call-site alignment remain unsupported.

## Consequences and evidence

The focused IR contract publishes exact indexed `unsigned char` load and store streams. It also guards the unchanged bodies of `asm_lower`, `x86_class_width`, and `x86_set_memory_width`, then adds fixed fixtures for a two-byte local and file object, a signed-byte callback, direct and indirect two-byte results, and narrow short-circuit conditions. The checks require signed-byte loads and returns, two-byte loads and stores, promotions, both call forms, and narrow condition branches.

The 30-function object contract covers signed and unsigned byte and word loads, exact-width stores, narrowing conversions, `_Bool` loads and stores, narrow locals and file objects, record members, automatic arrays, scalar conditions, and direct and indirect calls. It checks the exact `MOVSX`, `MOVZX`, byte-store, word-store, and Boolean conversion encodings inside the functions that require them. Four direct calls and three register-indirect calls cover sign and zero extension from AL and AX for signed and unsigned byte and word results plus `_Bool`. Shared decoding finds signed and unsigned byte and word loads, exact-width stores, and 31 returns. The two-byte file object occupies two aligned BSS bytes. Repeated emission is byte-identical and leaves the frozen translation unit and job arena unchanged.

The original useful negative contracts kept narrow compound assignment and narrow update expressions at the unsupported boundary. ADR 0046 replaces those cases with Boolean mutation failures and positive byte and word mutation coverage. Malformed frozen units reject a narrowing assignment mislabeled as integer promotion, a promotion with the wrong target, a usual arithmetic conversion that skips promotion, a prototype parameter width that disagrees with its definition object, and an incompatible function binding type. Tests that formerly rejected narrow loads, stores, casts, locals, and file objects now verify those paths succeed.

The first indexed-load contract stopped at the unsupported integer-conversion boundary. The first assignment contract then stopped at the narrow value-type boundary. After IR lowering accepted the typed values, object emission stopped at its former four-byte-only load and store checks. Those failures separated the semantic and target changes before the combined contracts passed.

This is hosted bootstrap evidence. GCC or Clang still builds the shared frontend, IR, emitter, x86, ELF32, and contract modules. The host C compiler still produces the normal root and user C objects. The private in-kernel CupidC compiler remains the embedded runtime JIT and AOT path. No production artifact, build transform, host dependency, boot path, or runtime behavior changes ownership here.

Issue #25 remains open for the rest of the C and ABI surface, production integration, staged self-hosting, and the fixed-point bootstrap.
