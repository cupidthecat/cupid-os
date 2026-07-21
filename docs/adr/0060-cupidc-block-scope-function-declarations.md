# Keep block-scope function names lexical in hosted CupidC

## Context

Active Cupid OS C source contains 27 function declarations inside compound statements across nine files. Most are local `extern` prototypes near optional subsystems, while `kernel/doom/src/wi_stuff.c` uses the same form without an explicit storage class. Moving these declarations to headers or file scope would change the source to avoid a compiler gap.

A function name declared in a block shares C's ordinary identifier namespace. The name has lexical scope, but the function has linkage and may already have a declaration at file scope. A visible file-scope `static` function must keep internal linkage. A block declaration hidden behind a nearer object or parameter may instead introduce an external function entity. Compatible declarations of the same entity must share one identity and form a composite function type.

## Decision

The frontend publishes each block function declaration as a source-ordered `ctool_c_block_binding_t` with kind `CTOOL_C_BINDING_FUNCTION`. Its storage spelling is either `CTOOL_C_STORAGE_NONE` or `CTOOL_C_STORAGE_EXTERN`, and `linkage_binding` points to the canonical function binding. The block record controls lexical visibility and retains the type in effect at that declaration. Expressions use that lexical type with the canonical binding reference, so a function never looks like automatic storage.

Lookup first considers the nearest active ordinary name. A visible linked function is reused when the new declaration is compatible. A visible file-scope `static` function keeps internal linkage. If a nearer object, parameter, typedef, or enumerator hides that name, the block declaration introduces or reuses a separate external function entity. An entity first introduced inside a block stays out of ordinary file-scope lookup until a later compatible file declaration publishes it.

Compatible declarations share the canonical identity across nested blocks, sibling blocks, and later file declarations. Type composition happens only when a prior linked declaration is visible. An old-style declaration in a later sibling block therefore stays old-style even if an earlier sibling used a prototype. The hidden declarations still name the same external function and must have compatible types. The canonical declaration also retains merged function flags such as `inline`.

Only no storage spelling and `extern` are accepted for a block function. `static`, `auto`, `register`, an initializer, and use as a `for` initializer receive focused diagnostics. GNU nested function definitions and block declaration attributes remain unsupported at this boundary.

Linear IR validates that both the block record and its canonical binding have compatible function types, advances the lexical declaration cursor, and emits no instruction for the declaration. Calls and address uses retain the lexical type while referring to the canonical symbol. A direct call produces the ordinary `R_386_PC32` relocation, and a function address produces `R_386_32`.

Treating the declaration as a local object was rejected because functions have no automatic storage. Publishing every block name at file scope was rejected because it breaks lexical lookup. Giving each declaration a fresh function entity was rejected because compatible declarations must share linkage identity. Moving the active declarations was rejected because CupidC must grow to accept the existing source.

## Consequences and evidence

The frontend contract covers plain, `extern`, and `inline` spellings; compatible repetition; visible old-style to prototype composition; hidden sibling declarations that keep different compatible types; multiple declarators; visible internal linkage; a nearer automatic object; typedef and enumerator shadowing; scope restoration; later file publication; and a pointer to an incomplete record return type. Its positive fixture has 15 block function declarations. Fourteen exact negative cases cover invalid storage, initializers, conflicts, scope expiry, nested definitions, attributes, and incompatible declarations. An active-source guard pins all 27 declarations in their nine files.

The IR contract proves external and internal direct calls, an old-style sibling call, one function address, and no declaration-owned `LOCAL_ADDRESS`. It also rejects a block function whose record and canonical binding were both changed to a compatible non-function type. The object contract is byte-identical to an equivalent file-scope declaration sequence. It has three defined functions, one undefined function symbol, two `R_386_PC32` call relocations with addend `-4`, and one `R_386_32` function-address relocation with addend zero.

This is hosted bootstrap evidence. GCC or Clang still builds CupidC and the contracts, and the normal Cupid OS build still uses the host compiler for C objects. No production artifact, ABI, or boot path changes in this increment. Nested functions, block attributes, production ownership, and self-hosting remain open.

## Later work

ADR 0061 adds declaration-position block enums to the same lexical binding stream. ADR 0062 adds enum definitions in block record members, type names, and function-definition parameter lists. Nested functions and block attributes remain open.
