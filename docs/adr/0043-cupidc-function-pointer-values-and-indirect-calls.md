# Represent function pointers and fixed indirect calls in CupidC

## Context

CupidC already preserved function types, function decay, callback declarations, and static function addresses in the frozen frontend unit. The hosted IR stopped when a function designator became a runtime value or when a call target was not a direct identifier. That gap blocks ordinary active Toolchain code. `ctool_invoke` in `toolchain/ctool.c` checks an invocation callback for null and calls it through `body(&invocation, user_data)`. CupidLD also calls section selectors through function pointers.

Treating callbacks as untyped integers would discard the function signature before ABI validation. Treating a function designator as an object address would blur a distinction already enforced by the frontend. Rewriting the active callbacks into direct calls would change the interfaces that CupidC needs to compile.

## Decision

A represented function pointer is a complete four-byte scalar value on the i386 target. It can cross fixed cdecl parameters and results, automatic and linked storage, initializers, loads, stores, assignments, direct arguments, conditional selection, equality, null conversion, and scalar truth tests. Function pointer arithmetic and relational comparison are not added.

The IR abstract stack now keeps function designators separate from object addresses and scalar values. `FUNCTION_ADDRESS` names a linked function binding. `FUNCTION_TO_POINTER` records the standard decay from a function designator to a pointer value. `ADDRESS_OF` accepts either a complete object address or a function designator, and `DEREFERENCE` turns a function pointer back into a function designator. Those last three transitions emit no machine instruction because all represented forms occupy one i386 word.

Function pointer compatibility follows the frontend's structural function rules instead of requiring both declarations to share one graph node. Fixed prototypes compare result and parameter types, parameter counts, and variadic state. Top-level `const`, `volatile`, and `restrict` on parameters do not affect compatibility, while `_Atomic` and referent qualifiers remain significant. Compatible old-style and prototyped declarations retain the existing default-promotion check.

The IR relation uses an arena-backed worklist and remembers every type pair it has checked. Repeated callback children are therefore visited once instead of opening a new recursive comparison for every path through the graph. The public queries return a checked status, publish the Boolean result separately, and rewind their scratch storage before returning. A malformed parameter slice produces a false result without dereferencing missing storage.

`CALL_INDIRECT` retains the callee pointer type. Lowering evaluates the callee first, then evaluates arguments in source order. The emitter reverses the completed four-byte argument slots into cdecl order, loads the callee below those slots into EAX, emits `CALL EAX` through the shared x86 encoder, and removes both the arguments and the saved callee. A non-void result is pushed from EAX. Direct function identifiers still use `CALL_DIRECT` and `R_386_PC32`.

Taking a linked function address emits the same immediate push used for a linked object address, with an `R_386_32` relocation and addend zero. Static callback initializers keep their existing binding-address record and produce the same relocation in `.data` or `.rodata`.

This slice accepts fixed, nonvariadic function types whose parameters and result are represented four-byte scalars, or `void` for the result. Variadic indirect calls, wider integers, floating and aggregate call forms, 16-byte call-site alignment, and atomic callback access remain unsupported. Atomic callback loads need an ordering contract before they can share the ordinary load path.

Explicit function pointer casts that produce another value remain outside this slice. This includes casts to or from integers, object pointers, compatible function pointers, and incompatible function signatures. ADR 0047 later permits a function pointer operand when the cast target is `void`, since the value is discarded and no target conversion occurs. Existing explicit casts among represented integers and object pointers are unchanged.

## Consequences and evidence

The IR contract publishes 36 exact instructions across six call and function-address functions. It covers implicit and explicit function addresses, `(*callback)(value)`, a void callback, and a three-argument callback. Seven value functions add 50 exact instructions for static and automatic storage, assignment, equality, null conversion, truth testing, conditional selection, and a callback passed to a direct call.

The deterministic object contract contains 13 functions, 477 text bytes, 17 symbols, nine text relocations, and one data relocation. It decodes four register-indirect calls, one direct call, and 13 returns. The first 208 text bytes are exact, including the void-result cleanup and three-argument slot reversal. A second 28-byte object proves that taking the address of a defined static function emits one local-symbol `R_386_32` relocation with addend zero. Repeated emission is byte-identical. Negative cases cover a malformed relational comparison, variadic and wide indirect calls, all represented value-producing function pointer cast categories, atomic callback loads, a missing parameter-type array, constrained relation storage, and object rollback.

The compatibility contract also compares distinct 24-level callback graphs whose two parameters both reuse the preceding callback type. It accepts the compatible pair, rejects a pair with a different leaf parameter, checks both directions where the relation is symmetric, and returns its arena mark unchanged. Old-style prototypes are tested with parameters that do and do not survive the default promotions. Parameter tests cover ignored `const`, `volatile`, and `restrict`, plus significant `_Atomic` and referent qualification.

Active-source guards pin the unchanged invocation callback in `toolchain/ctool.c` and the unchanged section-selector call in `toolchain/cupidld.c`. No production C object uses this hosted path, so the change does not alter an OS artifact or justify a boot claim. GCC or Clang still builds the hosted proof. ADR 0044 later adds storage for referenced, uninitialized fixed automatic arrays and records. Issue #25 remains open for aggregate initialization and values, atomic access and mutation, other widths, floating values, production integration, staged self-hosting, and the fixed-point bootstrap.

## Extension: structure-valued indirect calls

Fixed indirect calls later gained the same supported structure ABI as direct calls. The callee handle remains below the source-ordered argument handles. The emitter builds a rounded, zero-padded outgoing object area, loads the saved callee into EAX, and emits `CALL EAX`. Structure results use an instruction-owned destination passed as the hidden first argument. The callee returns that pointer in EAX and removes its hidden slot with `RET 4`; the caller removes the explicit outgoing bytes, argument handles, and saved callee handle. Scalar-only indirect calls keep their original slot-reversal path. Variadic, floating, atomic, and unsupported aggregate forms remain deferred. This extension transfers no production ownership.
