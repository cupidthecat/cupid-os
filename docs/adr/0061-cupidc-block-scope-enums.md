# Keep block enumerators in the lexical binding stream

## Context

The normal OS C source has two enum definitions inside function bodies. `desktop_run` in `kernel/gui/desktop.c` defines `CURSOR_W`, `CURSOR_H`, and `CURSOR_PAD`. `shell_cc_repl` in `kernel/lang/shell.c` defines `CC_REPL_LINE_MAX` and `CC_REPL_SRC_MAX`. Moving these constants to file scope would hide a real CupidC gap and change source that already expresses the intended scope.

An enum tag uses the tag namespace, but each enumerator enters the ordinary identifier namespace as soon as its declarator is complete. A later enumerator may use an earlier one in an integer constant expression. A nested declaration may shadow both the tag and its enumerators, and leaving that block must restore the outer names. The frontend therefore needs more than a local tag identity. It must retain every enumerator's type, value, source location, and declaration ownership.

## Decision

Declaration-position block enums publish `CTOOL_C_BINDING_ENUMERATOR` records in the existing source-ordered block-binding stream. Each record has no storage, initializer, or linked identity. It retains the evaluated target bit pattern and unsignedness alongside the final identifier type. The declaration statement owns the contiguous enumerator slice, followed by any declarators that use the enum type.

The parser publishes an enumerator immediately after evaluating its value. This gives the next enumerator the correct point of declaration. Ordinary-name lookup checks the nearest active block binding before parameters and file bindings, so objects and parameters hide outer enum constants and nested enum constants restore correctly at scope exit. A named enum definition searches only the current tag scope before creating its type. A reference searches all visible tag scopes. Retained type nodes stay stable after the tag name expires.

An anonymous enum may appear in a `for` declaration that also declares an automatic or register object. Its enumerators and object share the loop scope. The existing rule still rejects a `for` declaration that introduces a named tag or omits its object.

A block type name or record member may refer to an enum tag that is already visible. These uses retain the same enum type and do not publish another tag or enumerator. [ADR 0062](0062-cupidc-nested-block-enum-definitions.md) extends this model to new enum definitions in record members, function-definition parameter lists, and block type names. It adds function-prefix, expression, and initializer ownership without changing the binding kind chosen here.

Linear IR validates each enumerator record without allocating storage or emitting declaration work. A represented 32-bit use becomes `CTOOL_C_IR_INSTRUCTION_INTEGER`. The ELF emitter therefore sees the same instruction sequence as an equivalent direct integer constant. It creates no frame slot, symbol, or relocation for the enum name. ADR 0062 adds a lexical ownership pass for enumerators that are not introduced directly by a declaration. A GNU-width enumerator may survive an unused declaration, but using a 64-bit value still stops at the existing unsupported runtime-value boundary.

Publishing block enumerators as file bindings was rejected because it leaks names across functions and breaks shadowing. Recomputing values from tokens during lowering was rejected because it would duplicate frontend semantics and lose the immutable typed result. Treating an enumerator as an object was rejected because it has no address or storage. Moving the active declarations was rejected because CupidC must accept the source as written.

## Consequences and evidence

The original frontend contract covers named and anonymous declaration-position definitions, values that depend on earlier enumerators, enum-typed objects, nested tag and enumerator shadowing, restoration, static initialization, file-name shadowing, reference-only uses in block type names and record members, and an anonymous enum in a `for` declaration. It also reproduces the cursor and REPL constants from active source. Exact failures cover duplicate names, parameter conflicts, expired tags and enumerators, loop-scope expiry, address-taking, named tag introduction in a `for` declaration, and an enum-only `for` initializer. ADR 0062 adds the remaining nested definition contexts and their source-point ownership contracts.

The IR contract proves deterministic constant lowering with no `LOCAL_ADDRESS`, accepts an unused wide GNU enumerator, rejects a referenced wide value, and rejects malformed storage, initializer, linkage, unsignedness, value, and kind metadata. The object contract compares the active cursor and REPL shapes plus nested shadowing with direct folded constants. Their ELF32 bytes match exactly, and the parsed object has no enumerator symbol or relocation.

This is hosted bootstrap evidence. GCC or Clang still builds CupidC and the contracts, and the normal Cupid OS build still uses the host compiler for C objects. No production artifact, ABI, or boot path changes in this increment. Block declaration attributes, nested function definitions, the remaining enum contexts, production ownership, and self-hosting remain open.
