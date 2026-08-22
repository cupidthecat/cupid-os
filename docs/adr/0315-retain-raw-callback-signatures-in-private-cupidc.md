# ADR 0315: Retain raw callback signatures in private CupidC

## Status

Accepted on 2026-08-21.

## Context

ADRs 0303, 0306, 0310, and 0313 retain callback signatures when a direct
file-scope function-pointer typedef supplies the type. Named block-local raw
callback declarators already retain their signatures. A raw file object or raw
free-function parameter still lost the same information because its declarator
did not pass through the typedef side table.

The active kernel uses both forms. `kernel/core/panic.cc` declares raw callback
file objects initialized from functions, and `panic_set_output` accepts raw
callback parameters. Requiring a typedef would weaken the source-driven
language boundary rather than improve the compiler.

## Decision

Parse a named raw callback declarator into one reusable signature record.
Apply that record directly to a file object. The object may start as null or a
compatible defined or later-defined function. It may then receive a compatible
plain assignment, make a typed indirect call, and be cleared to null. The
existing initialized-data write and absolute data patch rules resolve its
initial address in JIT and fixed-address AOT output.

Retain the same record on a direct raw callback parameter of a free function.
Its indirect call uses the existing cdecl conversion and 4-, 8-, or 16-byte
slot rules. A direct callback argument must match the result, record-pointer
identities, fixed parameters, prototype state, and variadic boundary. Arity is
checked before the call. A free-function prototype and definition must retain
the same raw callback parameter signature.

Intern up to 32 raw parameter signatures in a private pool. Handles below that
pool still name the existing sixteen typedef entries. A failed program restores
the raw signature count with code, data, symbols, typedefs, and patches so the
next compile sees the original state.

Commit the raw signature count at each successful persistent REPL unit. A raw
callback global remains available to later units when initialized to null or
from a defined function. A function prototype may also initialize the global
before a later unit supplies the definition. The existing absolute data fixup
resolves that address when the definition commits. Later units may assign the
global, make a typed call through it, and clear it to null.

Restore the committed raw signature count when a REPL unit fails during
parsing. Restore it again when parsing succeeds but unresolved patches reject
the unit. Both paths leave capacity available to later units.

This is private CupidC behavior. It does not extend hosted CupidC or place the
parser in either checked standalone seed.

## Evidence

JIT and fixed-address AOT tests use the active raw global spelling with null,
defined, and later-defined targets. They cover checked assignment, indirect
calls, null clearing, pointer and floating parameters, and initialized-data
address patches.

A raw free-function callback parameter test uses a pointer, `double`, and
integer boundary to prove the existing cdecl slot conversions. Other cases
check record-pointer identity, default promotion of a variadic argument, and
matching callback parameters between a function prototype and definition.
Negative cases reject result, parameter, record identity, variadic boundary,
and arity mismatches. They also reject a mismatch between the raw callback
parameter in a function prototype and its definition. Every failure is
followed by a valid compile and execution in the same compiler state.

The persistent REPL tests keep globals across units when initialized to null,
from a defined function, or from a prototyped function defined in a later unit.
They check the later address fixup, assignment, null clearing, and typed
indirect calls. A parse failure and a post-parse unresolved-patch failure each
restore the raw signature pool before 32 distinct signatures and a valid retry
are compiled in later units.

The complete private callback ABI module passes all 268 tests in 51.685
seconds. The bounded-pool case accepts 32 distinct raw signatures, rejects the
next one with `too many raw function-pointer signatures`, and then compiles and
runs a valid retry in the same state. No new self-host object identity is
recorded at this boundary.

At this decision boundary, the four-vCPU raw callback QEMU smoke passed with
`[feature14-callback-raw] PASS initialized=1 parameter=1 cleared=1 reassigned=1 calls=3`.
The then-current 32,803-byte
`tests/feature14-callback-raw-qemu.log` has SHA-256
`eb915fe1894e4e1dcea236883f874f2c72e0c700a709f13168e438538d60b1ad`.
The full GUI test module passed all 126 tests in 1.468 seconds. This is
source-head guest evidence. Checked-seed promotion and production adoption
remain pending.

## Rejected alternatives

Requiring a file-scope typedef was rejected. The active source uses an ordinary
raw C declarator, and the compiler already has the signature facts needed to
represent it.

Erasing raw parameter signatures was rejected. That would preserve a call
address while discarding the conversions and arity that make the call valid.

Lowering a raw global initializer into a hidden runtime assignment was
rejected. ADR 0313 already provides one checked initialized-data model.

## Consequences

Private CupidC retains named raw callback signatures for block-local objects,
file objects, and direct free-function parameters. Typedef-backed callback
objects and parameters keep their existing behavior.

Explicit address-of callback initializers, conditional callback initializers,
callback fields, callback arrays, block-static raw callbacks, alias chains,
computed callbacks, and raw Cupid class method parameters remain outside the
tested boundary.
Direct structure and array callback results remain unsupported. No build
owner, host dependency, guest ABI, or object format changes as a result.
`TempleOS/` remains read-only reference material.
