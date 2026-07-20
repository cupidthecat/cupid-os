# Keep block-scope record tags lexical in hosted CupidC

## Context

ADR 0056 moved the exact Doom profile to the anonymous `struct` in the block-static `packs` declaration at `kernel/doom/src/d_main.c:689`. The declaration combines a block-scope record definition, an inferred array bound, static storage, nested aggregate initialization, and string-backed relocations. CupidC already handled every part except the record specifier.

C gives `struct` and `union` tags their own namespace and lexical scope. A declaration such as `struct Item;` may introduce an incomplete tag that hides an outer tag. A later definition in the same scope completes that type. A nested definition may shadow it temporarily, and leaving the block must restore the nearest outer tag. Publishing every local tag in the translation unit would lose those rules and expose names after their lifetime ended.

## Decision

The frontend keeps a tag mark with each block-binding scope mark. Record lookup for a definition or a tag-only declaration begins at the current scope mark, while an ordinary record reference searches from the innermost live tag outward. Leaving a block rewinds its tag slice together with its ordinary-name slice. Type nodes remain stable, so retained bindings can continue to refer to a local record after its tag name leaves parser scope.

Tags declared in a function definition's parameter list share the scope of the outer function body. The parser saves that parameter tag slice while it builds the function type, restores it before parsing the body, and removes it when the definition ends. A declaration inside the outer body therefore sees the same record identity, while a later function does not.

Named `struct` and `union` declarations now support forward declaration, same-scope completion, ordinary reference, nested shadowing, and restoration after scope exit. Anonymous record definitions work when a declarator owns the resulting type. A tag-only declaration becomes a declaration statement with a zero-length block-binding slice. It may use the represented `typedef`, `extern`, `static`, `auto`, or `register` spelling, or a represented type qualifier, when the declaration introduces a new tag. An empty declaration with storage or type qualification that only names a visible tag is rejected because it declares neither an identifier nor a tag. The statement has no runtime effect, and Linear IR accepts it as a checked no-op.

A `for` initializer may declare an object using a visible record tag or an anonymous record definition. It may not introduce a named tag, and a tag-only declaration still fails because the initializer must declare an automatic or register object.

Block enum specifiers stay at an exact unsupported boundary, including enums nested inside a block record, enums in block type names, and enums introduced in a function definition's parameter list. The public record-tag table only needs type identity, but block enumerators also need scoped integer values in the ordinary identifier namespace. Accepting the syntax without representing those values would produce incorrect lookup and constant-expression behavior. Block `extern` objects, block typedef names, and block function declarations were left as separate work in this increment.

An anonymous-record-only shortcut was rejected because it would clear one Doom line without implementing C tag semantics. Retaining local tags in the public translation-unit tag table was rejected because nested scopes could leak or overwrite file tags. Treating a tag-only declaration as an expression or null statement was rejected because it is a declaration in the source grammar and later consumers need its source-order position.

## Consequences and evidence

The frontend contract covers file-tag references, block forwards, same-scope completion, anonymous block-static arrays, struct and union objects, nested shadowing, scope expiry, restoration of a hidden file tag, all five represented storage-class spellings and a type-qualified spelling on newly introduced tags, and a parameter-list tag used by the function body. A `for` fixture proves that a visible record reference and an anonymous record object remain valid. Exact negative cases cover direct, nested, type-name, and parameter-list block enums; duplicate definitions; tag-kind conflicts; expired block and parameter-list tags; anonymous records without declarators; both tag-only and object-declaring `for` initializers that introduce a tag; and storage- or type-qualified empty declarations that only name an outer tag. Failed parses preserve the earlier successful unit, do not leak an enumerator into the next parse, and recover in the same job.

The IR contract retains two storage-qualified, zero-binding declarations and lowers the one runtime object through ten deterministic instructions with a maximum stack depth of two. A malformed binding cursor fails transactionally. The object contract emits the named automatic record and the active anonymous block-static shape twice with byte-identical results. It pins 91 text bytes with fingerprint `8C3E7E1C`, seven symbols, four relocations, and all 43 bytes of the read-only data section. The text relocation at offset 57 is a direct `R_386_32` reference with addend zero to `packs`. The local `packs` object occupies the first 24 read-only bytes. Its relocations at offsets 0, 8, and 16 are direct `R_386_32` references with addend zero to the exact `doom2`, `tnt`, and `plutonia` literal symbols at offsets 24, 30, and 34.

The exact Doom profile now passes the unchanged `packs` declaration and reaches the block-scope `extern` objects at `kernel/doom/src/d_main.c:1336`. This moves a measured hosted frontend boundary. It does not move a production C object, alter the target ABI, or justify a boot claim. GCC or Clang still builds the shared compiler and its contracts, and the normal Cupid OS build still uses the host C compiler for C objects.

## Later extensions

ADR 0058 adds block-scope external objects. ADR 0059 adds block typedefs while preserving the same lexical ordinary-name scope used by local objects and parameters. Block enum specifiers and block function declarations remain open.
