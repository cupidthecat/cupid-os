# Preserve the lexical activation point of block enum definitions

## Context

ADR 0061 put declaration-position enumerators in CupidC's ordinary block-binding stream. That model did not cover enum definitions in a record member, a function definition's parameter list, or a block type name. Those forms are valid C, and their enumerators enter the ordinary identifier namespace at the point where each name is declared.

A type name can appear in several places inside a function. It may be part of `sizeof`, `_Alignof`, a cast, a compound literal, `__builtin_offsetof`, a case value, a loop header, or `__builtin_va_arg`. It can also appear in an initializer designator whose folded expression is absent from the runtime AST. Giving every new enumerator to the enclosing declaration would lose that source order. Following runtime evaluation order would also be wrong for constructs such as a `for` statement, where the iteration expression appears before the body in the source but runs after it.

The frontend therefore needs to retain both the enumerator and the exact point where it becomes visible. IR must validate that order without inventing storage or runtime work.

## Decision

All block enumerators continue to use `CTOOL_C_BINDING_ENUMERATOR` in one stable, source-ordered block-binding stream. Record-member definitions belong to the declaration that contains the record. A function definition records a prefix slice for enumerators introduced by its parameter list. That prefix enters scope before the outer compound statement and expires with the definition.

An enum definition in an expression type name records an activation expression. The expression owns a contiguous binding slice and a direct-child offset, which identifies the source point before, between, or after its children. Enum definitions folded inside an initializer designator use an initializer-owned slice instead. Each enumerator points back to exactly one expression or initializer owner, unless it belongs to a declaration or function prefix.

Automatic objects reserve their public block-binding index before the initializer is parsed. The provisional record makes the object name visible at C's point of declaration. It also leaves later enum events in their real source positions instead of letting them take the object's future index.

Linear IR performs a lexical ownership pass before lowering a function. It walks statement headers, bodies, expression children, and initializer forests in source order. The pass validates every ownership slice, rejects a reference that precedes activation, and establishes the complete function binding range. Runtime lowering then follows control-flow order without changing enum visibility. A `do` body is scanned before its condition, while a `for` condition and iteration are scanned before its body, matching their written order.

The represented variadic read path accepts a complete, non-atomic four-byte enum. The frontend checks the target compatible-integer width, while IR and object emission check the completed layout. All three enforce the same one-word ABI rule.

We rejected function-wide visibility without activation records because it would accept uses written before a type-name definition. We rejected token replay in IR because it would duplicate parsing and constant evaluation. We also rejected synthetic local objects because enumerators have no address, storage, ELF symbol, or relocation.

## Consequences and evidence

Focused frontend contracts cover record members, function-definition parameters, `sizeof`, `_Alignof`, casts, compound literals, `__builtin_offsetof`, case values, loop iteration expressions, `__builtin_va_arg`, aggregate designators, and designators inside compound literals. They also cover name conflicts, scope expiry, tag expiry, use before declaration, and conflicts with an object whose initializer contains the enum.

IR contracts reject malformed function prefixes, expression and initializer slices, broken owner back-references, invalid child offsets, and references before activation. Object contracts compare the enum forms with equivalent literal forms byte for byte and verify that no enumerator reaches the ELF symbol table. A four-byte enum variadic read follows the same emitted path as an `int` read.

This decision extends ADR 0061 and closes the enum boundaries recorded in ADR 0014, ADR 0057, and ADR 0060. It does not move a production C object to CupidC. Block declaration attributes, block-scope static assertions, nested function definitions, direct 64-bit runtime enum values, floating semantics, production integration, and self-hosting remain separate work.
