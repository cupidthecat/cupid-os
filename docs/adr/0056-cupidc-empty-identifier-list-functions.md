# Lower empty identifier-list functions through hosted CupidC

## Context

ADR 0055 moved the exact Doom profile past its variadic compatibility header. The next failure was the unchanged `void doomgeneric_Tick()` definition at `kernel/doom/src/d_main.c:405`. In C, an empty identifier-list definition has no parameters, but its function type still has no prototype. Calls through that type may supply arguments, and every argument receives the default argument promotions.

The shared frontend already used a distinct `has_prototype` fact and already had the represented default-promotion path for ellipsis arguments. Linear IR and the i386 emitter still required a prototype for every definition and call. Treating `()` as `(void)` would have cleared the immediate parser failure, but it would have changed the declaration's compatibility and call semantics.

## Decision

The frontend accepts a function definition whose identifier list is empty. The definition has zero parameters and retains `has_prototype == false`. A nonempty identifier list still receives a focused unsupported-declarator diagnostic because parameter declaration lists for that form are not represented yet.

A call through a function type without a prototype may carry any number of arguments. The frontend applies default argument promotions to every argument. Lvalues undergo lvalue conversion, arrays and functions decay to pointers, and narrow integers promote to target `int`. Named parameters of a prototype continue to use assignment conversion, while only its ellipsis arguments use default promotions.

Linear IR retains the call site's actual argument count for both direct and indirect calls. A function type without a prototype must have zero declared parameter records and must not be marked variadic. Every actual argument therefore follows the existing path for a value without a declared parameter type. That path currently accepts represented four-byte integers and pointers. It rejects structures and other non-scalar values with an ABI diagnostic. Floating arguments stop earlier because the required default promotion to `double` is not represented.

The i386 emitter uses the actual count for stack effects, cdecl argument order, indirect-callee placement, sixteen-byte call alignment, and caller cleanup. An empty identifier-list definition emits the same zero-parameter frame and return path as any other represented zero-parameter definition. No target-specific fact is added to the frontend type graph.

Normalizing an empty identifier list to a `void` prototype was rejected because later declarations and calls could observe the wrong function type. Rejecting every nonzero actual count was rejected because a type without a prototype does not declare an arity. Passing raw words was rejected because it would skip C's default argument promotions and could expose stale high bits from narrow values.

## Consequences and evidence

The frontend contract defines and calls a zero-parameter function written with `()`. It also calls a separate unprototyped function directly with a promoted signed character, a pointer lvalue, and a function designator, then calls an unprototyped function pointer with a promoted unsigned short. Exact negative cases cover a floating argument that needs `double`, a nonempty identifier list, rollback, prior-unit preservation, and same-job recovery.

The IR contract publishes four functions and 20 instructions. It contains one zero-argument direct call, one three-argument direct call, and one one-argument indirect call. The checks pin two integer promotions, one function address and decay, the actual counts, and maximum stack depths of one, one, three, and two. A structure argument without a declared parameter type fails transactionally at the represented-scalar ABI boundary.

The object contract emits byte-identical ELF32 output twice. It pins 181 `.text` bytes, five named symbols, two direct-call relocations, and FNV fingerprint `898EBD57`. Shared decoding proves sixteen-byte alignment and the zero-argument direct call. A symbolic execution oracle follows both the direct and indirect unprototyped calls. It requires three distinct argument values in cdecl order, preserves the indirect callee below them, and checks the exact caller cleanup.

The exact Doom profile now passes `doomgeneric_Tick()` and reaches `kernel/doom/src/d_main.c:689`. Its next failure is the anonymous block-scope `struct` in the static `packs` declaration. This advances the measured frontend boundary without claiming a complete Doom translation unit or moving any production C object.

GCC or Clang still builds the shared compiler and its contracts. The normal Cupid OS build still uses the host C compiler for C objects, and the private in-kernel CupidC remains the embedded JIT and AOT path. Nonempty identifier-list definitions, implicit function declarations, floating and wider runtime values, aggregate arguments without declared parameter types, the remaining C and GNU surface, production integration, staged self-hosting, and the fixed-point bootstrap remain open under issue #25.
