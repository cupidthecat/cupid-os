# ADR 0330: Support data-backed raw callback arrays

## Status

Accepted on 2026-08-24.

## Context

Private CupidC retained raw callback signatures on scalar file objects,
automatic objects, free-function parameters, and record fields. It also kept
typedef-backed callback signatures on fixed record-field arrays. A raw
function-pointer array still lost the connection between its storage and the
parsed callback signature.

The active Doom wipe implementation in `kernel/doom/src/f_wipe.cc` uses a
six-entry block-static table declared as `static int (*wipes[])(int, int,
int)`. It dispatches each wipe through `(*wipes[wipeno*3])` and the following
two entries. Replacing that table with a typedef or a hand-written dispatch
wrapper would hide the compiler gap in active source.

## Decision

Accept one-dimensional raw function-pointer arrays when they have static
storage. This covers block-static declarations, file objects, and persistent
REPL globals. A fixed array requires a positive bound. An inferred array
requires a nonempty initializer, and its final bound is the number of
initializer elements.

Represent each element as one four-byte i386 callable address. Fixed storage
starts at zero, so an uninitialized array and omitted trailing elements begin
as null. A brace initializer accepts null or a compatible function designator.
The compiler writes a defined target immediately and records
`CC_PATCH_DATA_ABSOLUTE` for a later definition. This is the same data fixup
used by scalar static callbacks.

Retain one signature handle on the array symbol. Subscripting publishes that
signature for checked indexed stores and postfix calls. An explicit
unary `*` on the selected function pointer preserves the same callable value,
so the exact Doom call spelling follows the typed indirect-call path. Each
index expression keeps the existing one-evaluation behavior.

Use the data-backed declaration path for block-static scalar raw callbacks as
well. Program and REPL transactions continue to restore data, symbols,
signature records, and every patch kind after a failed declaration or an
unresolved target.

Keep automatic raw function-pointer arrays, raw callback array parameters, raw
record or class field arrays, and multidimensional raw callback arrays outside
this boundary. Typedef-backed fixed callback field arrays remain the separate
field-layout capability recorded in ADR 0328.

This is private CupidC behavior. The normal build still compiles the production
Doom cohort with checked-seed hosted CupidC, so the change does not transfer
ownership to the in-kernel compiler.

## Evidence

The first focused tests stopped at the raw declarator boundary. The completed
cases preserve the exact six-entry Doom table and its three indexed call
spellings. JIT and fixed-address AOT tests cover fixed and inferred arrays,
block-static persistence, zero-filled elements, defined and later-defined
targets, mixed-width callback arguments, indexed replacement, and explicit
unary dereference.

A persistent REPL test declares an inferred array from a prototype, defines
the target in a later unit, and calls the resolved element from a third unit.
Typed argument and declaration-initializer cases preserve result, parameter,
and record-pointer identity when an indexed element is copied or passed on.
Negative cases reject missing inferred bounds, empty inferred initializers,
excess fixed initializers, incompatible targets, nonpositive bounds,
multidimensional declarations, automatic storage, parameters, record fields,
bad indexed calls, and incompatible indexed stores. Each failure is followed
by a valid same-state compile. The complete private callback ABI suite passes
all 301 tests in 5.505 seconds. The full GUI contract module passes all 126
tests in 0.333 seconds. A four-vCPU private frontier boot records the exact
Doom-like array marker, the overall feature pass, and clean JIT completion.

## Rejected alternatives

Do not rewrite the Doom table around a callback typedef. The raw declarator is
ordinary C and already describes the signature the compiler needs.

Do not treat the array as untyped pointer storage. That would preserve the four
address bytes while dropping argument conversion, arity, result, and assignment
checks from indexed expressions.

Do not lower block-static initialization into an automatic runtime store. The
table has static lifetime, and its initializer belongs in the existing data
buffer and fixup transaction.

Do not claim support for every callback-array placement from this storage
slice. Parameters require an array-adjustment design, and raw record fields
need a declarator and layout rule distinct from the data-backed symbol path.

## Consequences

Private CupidC can represent the active Doom wipe-table shape without changing
the source. Block-static, file-scope, and persistent REPL raw callback arrays
retain their signatures across initialization, indexed stores, and indexed
calls. Block-static scalar raw callbacks use the same storage and patch rules.

Automatic arrays, array parameters, raw record or class field arrays, and
multidimensional raw callback arrays remain explicit compiler limits. The
standalone checked seeds do not contain the private parser. No object format,
build owner, host dependency, guest ABI, or `.c` ownership changes at this
boundary. `TempleOS/` remains read-only reference material.
