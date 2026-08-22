# Retain typedef callback signatures on private record fields

## Context

Private CupidC retained a file-scope callback typedef on named parameters,
automatic objects, and file objects. A structure or class field declared with
the same typedef kept only its four-byte storage type. Reading the field lost
the result, fixed parameters, record identities, and variadic boundary.
Writing the field also used the generic indirect store path, which accepted an
incompatible callback or a compound assignment without a signature check.

The active UHCI and EHCI interrupt slots both store `usb_complete_cb_t` in a
record field. Submission writes a callback parameter to the slot. The polling
path later copies that field into a typed automatic object, clears the slot,
and calls the local object after releasing the controller lock. This source
shape needs the field to carry the same signature as the typedef without
rewriting the USB code.

## Decision

Keep the signature handle of a direct file-scope function-pointer typedef on
each structure or class field declared with that typedef. Program, nested
record, class, and persistent REPL declarations use the same rule. Ordinary
fields keep an invalid handle and retain their existing layout and behavior.

A callback field read publishes its retained signature with the expression.
Assignment to a named callback object then checks a field copy against the
destination before storing it. A callback field lvalue carries its signature
through nested record and indexed record-array traversal. Its store accepts
only plain assignment from null, a compatible function, or another compatible
signature-bearing callback value. Result type, fixed parameters, record
identity, prototype state, and the variadic boundary must match.

The field remains one four-byte i386 address word. Calls still use the existing
named callback path after the field is copied into a typed local object. This
slice does not add a postfix call expression such as `slot->cb(...)`.

## Evidence

The public whole-source tests compile and execute a USB-shaped controller with
an array of interrupt-slot records. JIT and fixed-address AOT both store a
callback parameter in `controller->interrupt_slots[index].cb`, copy it into a
typed automatic object, clear the field, and make the typed indirect call.

Negative cases reject a mismatched result, mismatched record-pointer
parameter, and compound field assignment. Every case retries in the same
compiler state and executes a valid record-field copy. The complete private
callback ABI module passes all 272 tests in 52.354 seconds. The GUI terminal
smoke module passes all 126 tests in 1.368 seconds with the new guest marker
contract present. These are host and JIT/AOT results. A real QEMU observation
of the field marker is still pending.

## Rejected alternatives

Rewriting UHCI and EHCI to store untyped addresses was rejected because it
would remove the callback ABI that the typedef already declares.

Creating a temporary fake symbol for every field expression was rejected. A
field is not a named compiler symbol, and tying its signature to a synthetic
symbol would make nested member and array traversal harder to reason about.

Allowing the generic four-byte store without compatibility checks was rejected
because it would preserve storage while discarding the language rule.

## Consequences

The active USB callback field shape now fits private CupidC without changing
the USB source. Typedef-backed structure and class fields support checked
stores, null clearing, and checked copies into named callback objects.

Raw callback field declarators, callback arrays, block-static callbacks,
postfix calls on field expressions, alias chains, conditional field values,
raw Cupid class method parameters, recursive signatures, and aggregate callback
results remain outside this boundary.

The standalone compiler seeds do not contain this private parser. This
decision does not change a build owner, checked seed, object format, or host
dependency. `TempleOS/` remains read-only reference material.
