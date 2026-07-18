# Lower object-pointer values through CupidC IR

## Context

The shared CupidC frontend already distinguishes pointer values from the addresses of the objects they name. Hosted IR previously accepted direct file and local object addresses, but it stopped when a function parameter or loaded object supplied the base address. That blocked ordinary active code such as `obj_region_less` in `toolchain/cupidobj.c`, which reads members through two `const obj_flat_region_t *` parameters.

The missing path is broader than member syntax. Object pointers must cross parameters, return values, automatic locals, linked file objects, direct calls, initialization, assignment, dereference, address-of, and null-pointer conversion without becoming untyped machine integers. The i386 target represents each pointer and each object address in one 32-bit word, but the IR still needs to preserve their different meanings.

## Decision

The represented scalar slice now contains four-byte integers and four-byte object pointers. Fixed nonvariadic cdecl functions may use either represented scalar kind for parameters and non-void results. Direct arguments and returns use structural pointer compatibility instead of requiring the same frontend graph index. Value matching drops qualifiers from the pointer object after lvalue conversion but keeps qualifiers on its referent. Array qualifiers follow the frontend rule and move to the element comparison, so qualifying an array typedef and qualifying its element type produce the same answer. The shared IR query follows qualified, aligned, pointer, array, vector, enumeration, and scalar nodes. Distinct record and function nodes still rely on the frontend's canonical graph identity.

Two public IR instructions record the address and value transition. `DEREFERENCE` consumes a pointer value and produces the referenced object address. `ADDRESS_OF` consumes an object address and produces the corresponding pointer value. Both validate the exact source and result types. Neither emits a target instruction because the abstract transition does not change the i386 word already on the machine stack.

Pointer loads, stores, plain assignments, automatic initializers, locals, linked objects, direct calls, compatible pointer conversions, and null-pointer conversions use the existing scalar paths. The emitter accepts these pointer records only when both sides describe compatible represented pointer values. A null-pointer conversion still starts with a represented integer value and must carry `CTOOL_C_CONVERSION_NULL_POINTER`.

Atomic pointer loads remain unsupported. Treating them as ordinary `MOV` operations would invent an ordering contract that the current IR and emitter do not express. Pointer arithmetic, subscripts, pointer conditions and comparisons, pointer casts, pointer mutation, function-pointer values, indirect calls, and array or function decay also remain outside this increment.

## Consequences and evidence

The unchanged `obj_region_less` helper now publishes 50 exact IR instructions with a maximum stack depth of two. The contract checks every instruction in order, including its types, operation, conversion, reference, value bits, branch target, and presumed and physical source location. The stream includes six parameter addresses, twelve loads, six dereferences, six member addresses, three comparisons, five conditional branches, four jumps, seven integer constants, and one return.

Twelve focused functions publish 61 exact instructions. They cover direct dereference, address-of a member, indirect store, a return between distinct compatible pointer-to-array types, qualification conversion, both directions between an object pointer and `void *`, automatic pointer initialization and load, linked pointer assignment and load, null assignment, indirect member access, and a direct call with pointer argument and result types. One automatic initializer reads a `const volatile` pointer object and stores it in another `const volatile` pointer object whose compatible array referent uses a different qualification spelling. The contract pins every instruction field, proves that the compatible array referents have distinct graph identities, and checks the public value query with malformed layout input. The same query rejects an incompatible pointer pair and a referent-qualification mismatch.

The deterministic object contains twelve functions in 266 exact text bytes. It has two four-byte BSS objects and fifteen symbols. Its four `R_386_32` relocations target `global_value` once and `global_pointer` three times at text offsets 25, 175, 202, and 223 with addend zero. One `R_386_PC32` relocation targets `pass_pointer` at offset 255 with addend `-4`. The shared x86 decoder consumes the complete text and finds twelve returns and one call. A second emission from the same frozen unit is byte-identical.

Malformed frozen units reject an out-of-range dereference result, an out-of-range address-of result, a null pointer mislabeled as a general pointer conversion, and a qualification conversion mislabeled as integer assignment conversion. An atomic pointer load receives the public unsupported-type diagnostic. A function-pointer parameter stops at the public unsupported-ABI boundary instead of entering the object-pointer path. Every failure preserves the input, publishes an empty result, and rewinds operation allocations.

The first active-source contract stopped at the existing integer-only ABI diagnostic. The first address-of fixture then stopped at the unsupported-expression diagnostic. Review fixtures later exposed two structural-matching mistakes: top-level pointer-object qualifiers survived value conversion, and array qualifiers were compared before reaching their elements. Both valid programs now lower, while the pointed-to qualifiers still remain significant. Object emission later established the exact byte, symbol, relocation, function-size, and decoded-instruction contract recorded above. No active source was rewritten to avoid the missing feature.

This change transfers no production build ownership, removes no host dependency, changes no kernel artifact, and makes no boot claim. The private in-kernel CupidC compiler still produces every normal OS C object.

Issue #25 remains open. The remaining pointer work includes arithmetic, subscripts, conditions, comparisons, casts, mutation, decay, indirect calls, atomics, production integration, staged self-hosting, and the fixed-point bootstrap.
