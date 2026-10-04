# ADR 0313: Initialize private CupidC global callbacks from functions

## Status

Accepted on 2026-08-21.

## Context

ADR 0306 retained the full signature of a file-scope callback typedef in a
global object, but that object could begin only as null. Assigning a function
later worked because the JIT could emit a runtime store. A static initializer
could not use the same shortcut. Its address belongs in the initialized data
buffer before either the JIT or private AOT image starts.

This blocked an ordinary C form such as `callback_t callback = target;`. It
also made a function defined later in the translation unit impossible to
represent without weakening the source or inventing an initialization call.

## Decision

Accept a compatible plain function designator, including redundant
parentheses, in a typedef-backed file-scope callback initializer. Keep the
typedef's result, fixed parameters, record identities, and variadic boundary
as the compatibility authority.

Write the function address directly into the four-byte data slot when the
target is already defined or is a kernel binding. When the target is defined
later, record an absolute data patch beside the existing relative-call and
absolute-code patch kinds. The shared symbol-resolution pass applies all three
kinds after parsing. A checked data writer rejects any patch outside the
initialized storage buffer.

Retain the existing program transaction. A bad initializer, an unresolved
target, or a later definition that contradicts a provisional signature must
restore the data buffer, patch list, symbols, and provisional signatures
together. A following valid program in the same compiler state must behave as
if the failed program had never run.

This is a fixed-address private JIT and AOT rule. It does not add an ELF data
relocation or extend the hosted compiler.

## Evidence

The first defined-target JIT and AOT test stopped at the old null-only
diagnostic. The first later-target test reached the unresolved address because
initialized data had no patch kind. After the change, the defined target
returns 11 in both modes and the later target returns 13 in both modes.

Five focused tests pass in 2.829 seconds. They cover defined, later-defined,
and kernel-bound functions, grouped designators, parameter mismatch, an
unresolved function, a computed expression, and a later definition that
contradicts its provisional signature. Every negative case compiles and runs a
valid callback program in the same state afterward.

The complete private callback ABI module passes all 256 tests. The callback ABI
and GUI terminal modules pass all 381 tests together in 40.333 seconds. Their
guest contract requires the feature program to invoke its initialized callback,
reassign it, invoke it again, and clear it before printing the focused PASS
marker. The promoted Windows seed also builds a 467,688-byte `cupidc_parse.o`
with SHA-256
`009ddb57bd7cb5bd1312d429eae0000de2941284e061cb98ac9b7929fdf2240d`.
The unchanged 3,604-byte `cupidc_elf.o` keeps SHA-256
`c2ad171aacd493a33a477e7a3196a5d28b04b0f74521cd8cbaec2598f391880c`.
Broader build and guest evidence is recorded in the bootstrap log.

## Rejected alternatives

Do not lower a static initializer into a hidden runtime assignment. That would
change startup semantics and create a second initialization model.

Do not encode a forward function's current zero address and leave it for the
caller to replace. The compiler already owns symbol resolution, and a silent
zero would turn a compile-time failure into a null indirect call.

Do not treat this fixed-address patch as relocatable ELF support. The private
AOT writer knows its final code and data bases. An ELF object needs an explicit
symbol relocation owned by the object format.

## Consequences

Private CupidC can initialize a typedef-backed global callback directly from a
compatible defined or later-defined function. JIT and fixed-address AOT images
receive the same resolved address, and failed programs cannot leak data patches
or provisional signatures into the next compile.

Raw pointer declarators, `&function`, conditional callback expressions,
callback fields and arrays, block-static callbacks, alias chains, recursive
signatures, and the separate REPL global path remain open. The promoted
standalone seeds do not contain this private parser. No build owner, host
dependency, guest ABI, or object format changes. `TempleOS/` remains read-only
reference material.
