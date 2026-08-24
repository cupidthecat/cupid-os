# ADR 0328: Retain private callback field array signatures

## Status

Accepted on 2026-08-24.

## Context

Private CupidC retained a callback signature on a scalar record field but
discarded the same metadata when the field was a fixed array. An indexed field
could still load and store a four-byte address, but assignment and call checks
treated it as an untyped integer. Copying every selected element through an
unchecked cast would hide the compiler gap.

## Decision

Keep the function-pointer signature on a typedef-backed fixed array field in a
structure, class, anonymous typedef record, or persistent REPL record. Member
assignment carries that signature through the existing one-evaluation lvalue
path. Postfix subscripting preserves it after the index expression, so a named
copy or direct call uses the existing result, record-identity, fixed-parameter,
variadic, cdecl-slot, and return-value rules.

The array index is evaluated once. The field layout remains a contiguous array
of four-byte i386 addresses. Raw `T (*field[N])(parameters)` declarators remain
unsupported and receive a direct diagnostic that recommends a callback
typedef.

## Evidence

The complete private callback ABI module passes 289 tests. The new JIT and AOT
cases store mixed-width callbacks, copy an element, use both expression and
standalone calls, and exercise variadic floating-point return transport. They
also prove that side-effecting indexes run once during a store and a call.
Negative cases reject arity, argument, store, and copy mismatches, diagnose raw
callback array declarators, restore compiler state, and compile a valid retry.

The kernel object build and the private four-vCPU feature-14 smoke remain the
runtime gates for this private compiler change.

## Consequences

Private CupidC source can use fixed typedef-backed callback arrays on record
fields without losing type or cdecl metadata. Raw callback-array declarators,
block-static callbacks, alias chains, recursive signatures, raw class method
parameters, conditional callback values, and aggregate callback results remain
separate work.

The standalone checked seeds do not contain the private parser. This change
does not move a build owner, change an object format, remove a host dependency,
or make a `.c` file eligible for a `.cc` rename. `TempleOS/` remains read-only
reference material.
