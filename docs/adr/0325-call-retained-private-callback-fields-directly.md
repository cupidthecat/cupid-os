# ADR 0325: Call retained private callback fields directly

## Status

Accepted on 2026-08-22.

## Context

Private CupidC kept callback signatures on record fields declared through a
function-pointer typedef, but raw field declarators were still rejected. A
field also lost its callable role at postfix `()`. Active timer and GUI event
sources use direct callback members, and copying each member through a local
variable would hide a compiler limitation rather than solve it.

## Decision

Parse raw function-pointer fields in structures, classes, anonymous typedef
records, and persistent REPL records. Intern the result, parameters, record
identities, prototype state, and variadic boundary in the same retained
signature table used by typedef-backed fields. The field remains one four-byte
i386 address.

Allow postfix calls on a field expression that carries a retained signature.
The selected address is saved once below the argument slots. The existing
typed cdecl path converts fixed arguments, promotes scalar variadic arguments,
lays out the stack, and restores it after the indirect call. Scalar, pointer,
floating, and represented SIMD results keep their normal expression metadata.
A real field takes precedence over same-named class method sugar.

## Evidence

The complete private callback ABI module passes 286 tests. The new cases cover
raw declarations, typedef-backed and raw direct calls, fixed and variadic
arguments, record identity, scalar and `float4` results, nested and indexed
designators evaluated once, field and method name collisions, diagnostics,
rollback, and same-state recovery. Exact source guards compile the active timer
and GUI callback forms.

The GUI smoke contract now requires
`[feature14-callback-field-call] PASS typedef=1 raw=1 float4=4 once=1 calls=2`.
Its 126 marker tests pass, and `kernel/lang/cupidc_parse.o` builds cleanly.

## Consequences

Active private CupidC source can declare and call raw or typedef-backed
callback fields without a source workaround. Callback arrays, block-static
callbacks, alias chains, recursive signatures, raw class method parameters,
conditional field values, and aggregate callback results remain separate
work.

The standalone compiler seeds do not contain this private parser. No build
owner, object format, checked seed, or host dependency changes. No active `.c`
source became eligible for a `.cc` rename. `TempleOS/` remains read-only
reference material.
