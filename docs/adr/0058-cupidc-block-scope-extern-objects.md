# Keep block-scope extern objects linked in hosted CupidC

## Context

ADR 0057 moved the exact Doom profile to the two block-scope declarations `extern int forwardmove[2];` and `extern int sidemove[2];` at `kernel/doom/src/d_main.c:1336` and `:1337`. These declarations do not create automatic objects. They introduce or repeat names for objects with linkage, and every use must reach the same canonical entity that an eligible file-scope declaration would name.

C linkage depends on the declarations visible at the point of the block declaration. A visible file-scope object with internal linkage keeps that linkage. If no linked declaration is visible, the block declaration introduces an external entity even though its name is only visible inside the block. A nearer automatic object or parameter can hide a file declaration, so lookup cannot simply scan all file bindings by spelling. Repeated compatible declarations must share identity, and an incomplete array declaration may be completed by a later compatible declaration.

## Decision

Each block-scope `extern` object keeps a lexical block binding and the absolute index of its canonical linked object binding. The lexical record controls name visibility. Expressions use the canonical file-binding identity directly, so later IR and ELF emission do not mistake the declaration for automatic storage.

A canonical object introduced only through a block declaration records that it has no ordinary file-scope visibility. File lookup skips such bindings, while another compatible block `extern` can still find the entity through a visible lexical declaration. A later file-scope declaration may merge with the hidden entity and publish the name at file scope. This keeps entity identity separate from ordinary-name visibility.

When a linked file declaration is visible, the block declaration inherits its linkage. A visible file-scope `static` object therefore remains an internal-linkage object. If a nearer automatic object or parameter hides that declaration, a nested block `extern` introduces a distinct external-linkage entity. A block declaration may also hide a file typedef or enumerator until its scope ends.

Compatible repeated block declarations coalesce. This includes an incomplete array followed by a fixed bound. Incompatible types, conflicts with an automatic object or parameter in the same scope, and initializers receive focused diagnostics. The supported form is a declaration statement, not a `for` initializer. ADR 0060 extends the same lexical-to-canonical model to block-scope function declarations.

Linear IR validates the lexical-to-canonical link and emits no instruction for the declaration itself. It reserves no local frame storage. Uses lower through `FILE_ADDRESS`, so the existing linked-object path carries the canonical identity into ELF emission. An unresolved object receives one ordinary undefined ELF symbol and one `R_386_32` relocation at each address site, with no local object symbol or automatic frame slot.

Treating the declaration as an automatic local was rejected because that changes storage duration and linkage. Publishing every block-introduced entity as an ordinary file-scope name was rejected because it leaks lexical names. Moving the declarations to file scope was rejected because the active source is valid C and should drive the toolchain. Accepting the syntax without a canonical linked identity was rejected because it would fail as soon as the object reached IR or ELF emission.

## Consequences and evidence

The frontend contract covers scalar and array objects, repeated compatible declarations, incomplete-to-fixed array completion, visible internal linkage, hidden file declarations, typedef and enumerator shadowing, later file-scope publication, and scope restoration. Exact negative cases cover an initializer, a `for` initializer, incompatible repeated arrays, an incompatible later file declaration, a same-scope automatic object, and a parameter conflict. Malformed public records fail freeze checks instead of reaching a later consumer.

The unchanged `kernel/doom/src/d_main.c` now parses completely under the exact Doom profile. It publishes 2,631 bindings, 21 function definitions, 645 statements, 2,999 expressions, 41 block bindings, and 120 initializers. The declarations on lines 1336 and 1337 each retain a fixed two-element array type, a lexical block binding, and a hidden canonical external object binding.

The IR contract covers a scalar external object and an array declared first with an incomplete type and then with a two-element bound. The declaration statements emit no instructions, both uses lower through `FILE_ADDRESS`, and no `LOCAL_ADDRESS` appears. Transactional negatives reject a missing canonical link, a link to a non-object binding, a mismatched canonical name, and an impossible storage and linkage pair.

The deterministic ELF32 fixture emits 15 text bytes, three symbols, and one `R_386_32` relocation at text offset 4 with addend zero. The symbol table contains the null entry, the defined function, and one undefined object. Repeat emission is byte-identical. The complete frontend, IR, and object Python modules pass all 97 tests.

This is hosted bootstrap evidence. It transfers no production C object and removes no host dependency. GCC or Clang still builds the shared compiler and its contracts, while the normal Cupid OS build still uses the host compiler for C objects.
