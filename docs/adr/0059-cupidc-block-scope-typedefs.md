# Keep block-scope typedef names lexical in hosted CupidC

## Context

The hosted Toolchain contract sources contain eight block-scope typedef declarations across the frontend, preprocessor, IR, and object contracts. These declarations name local test-case records and function types. Rewriting them at file scope would hide a real requirement from the compiler roadmap.

A typedef name shares C's ordinary identifier namespace with objects, functions, parameters, and enumerators. Its meaning depends on the nearest active declaration. A nested typedef may hide an outer typedef or parameter, and the earlier meaning must return when the nested block closes. A repeated typedef in one scope is valid only when it names the same type.

## Decision

The frontend publishes each block typedef as a source-ordered `ctool_c_block_binding_t`. The record has kind `CTOOL_C_BINDING_TYPEDEF`, storage `CTOOL_C_STORAGE_TYPEDEF`, and a stable graph type. It has no initializer or linked object identity because a typedef declares a name, not storage.

Typedef lookup checks the nearest active block binding before parameters and file bindings. A typedef binding supplies its retained type. Any other ordinary binding hides an outer typedef. Leaving a block rewinds the active binding slice, so nested aliases and parameter shadows restore the earlier meaning without changing public type identities.

The normal declarator path builds the aliased type. This covers scalar, pointer, record, function, incomplete, and `void` aliases without applying object completeness rules. Multiple declarators retain source order. Repeating the same typedef in one scope is accepted only when strict type identity matches. A different type, an object or parameter conflict, an initializer, or use in a `for` initializer receives a focused diagnostic. Using a typedef name as an expression also fails at the expression seam.

Linear IR validates the typedef record, advances the declaration and visibility cursors, and emits no instruction. It assigns no local slot and creates no symbol or relocation. The object emitter needs no typedef-specific path because it consumes the resulting runtime IR and static object bindings.

Dropping typedef declarations after parsing was rejected because declaration slices and lexical visibility would no longer describe the source. Treating an alias as an object was rejected because it would invent storage and target output. Accepting merely compatible repeated types was rejected because C requires the same type in this scope. Moving the active declarations to file scope was rejected because the compiler must grow to fit the source.

## Consequences and evidence

The frontend contract covers multiple declarators, exact same-type repetition, typedef-on-typedef shadowing, an object that hides an outer typedef, restoration after both forms, a parameter hidden by a nested alias, record aliases, function aliases, and ordinary objects declared through those names. Exact negative cases cover initializers, incompatible types, compatible but non-identical incomplete and fixed arrays, object and parameter conflicts, `for` initializers, expression use, and scope expiry. An active-source guard pins all eight declarations in their original files.

The IR contract proves that only the object declared through an alias receives `LOCAL_ADDRESS`. Malformed public units with missing typedef storage, an initializer, a linked identity, or an invalid type fail transactionally. The ELF32 contract compares a typedef-based function with the same function using the underlying type directly. Both objects are byte-identical, and the typedef creates no symbol, relocation, or frame storage.

This is hosted bootstrap evidence. GCC or Clang still builds CupidC and the contracts, and the normal Cupid OS build still uses the host compiler for C objects. No production artifact, ABI, or boot path changes in this increment.
