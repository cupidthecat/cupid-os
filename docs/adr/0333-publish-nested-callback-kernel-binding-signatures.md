# ADR 0333: Publish nested callback kernel binding signatures

## Status

Accepted on 2026-08-24.

## Context

ADR 0332 lets a reviewed native binding publish one
`cc_function_pointer_signature_t`, but the active `set_icon_drawer` entry still
used the legacy result-only path. Its native declaration is
`void(int, void (*)(int, int))`. Without a retained child handle, private
CupidC accepted any four-byte value in the drawer slot and could not compare a
callback's result, parameters, record identities, prototype state, or variadic
boundary before entering the kernel.

The parser already represents nested callback parameters as a recursive graph.
Adding a second callback model for bindings would create separate compatibility,
depth, capacity, and rollback rules for the same C type.

## Decision

Expose a public kernel-binding callback retention function that accepts the
existing signature record. It runs the same descriptor and recursive graph
validation used by typed binding registration, then interns the record in the
existing raw-signature graph. The outer binding stores that handle in
`param_struct_indices` for its `TYPE_FUNC_PTR` parameter.

Register `set_icon_drawer` with a retained `void(int, int)` child and a fixed
outer `void(int, callback)` descriptor. Calls use the existing typed
`SYM_KERNEL` path, including fixed arity, callback compatibility, cdecl layout,
cleanup, and result handling.

Keep the 16-level graph depth limit and the source-facing capacity of 32 raw
signatures. The shared backing pool grows to 33 records because the active
kernel descriptor occupies one record before a source unit is parsed. A corrupt
handle, excessive graph, or full pool consumes no new record. A rejected outer
registration consumes no symbol. An existing record may still be recovered by
structural deduplication in the same state.

## Evidence

The red public driver failed to compile because
`cc_retain_kernel_binding_callback_signature` was not declared. The green JIT
and fixed-address AOT fixture passes a real `void(int, int)` callback through a
fake `set_icon_drawer` native entry and observes its two arguments. Negative
sources reject nested result, parameter type, record identity, and variadic
boundary mismatches, then compile a valid binding call in the same state.

Public descriptor cases reject a corrupt child handle and a seventeenth nested
edge, preserve the raw count, and recover by retaining a valid or deduplicated
record. The capacity case fills the 33-record backing graph, rejects one more
record, and recovers without changing the count. The first full ABI replay
exposed four persistent-REPL failures when the built-in descriptor reduced the
old source pool to 31 records. Expanding only the backing graph restored all
four 32-record source contracts.

Feature 14 calls the active binding with an invalid handle. The kernel returns
without invoking the callback, and the guest requires
`[feature14-callback-binding] PASS call=1 ignored=1 callback=0` after its
existing nested callback marker. The frontier contract also rejects the new
failure marker.

The private call ABI, binding contract, and GUI modules pass 456 tests in
78.310 seconds. Checked-seed CupidC builds both changed kernel objects.
`cupidc_parse.o` is 505,228 bytes with SHA-256
`6b540b53a606d5d5471ea372100a3380049915c50a3c8bf12926081705a06603`.
`cupidc.o` is 299,208 bytes with SHA-256
`bc66b63d7711ab49c8baac2719ffc87be9a6c330081888374db0dbbfdce0f5c9`.

The supported audit regeneration and reproducibility check both pass. The
2,702,374-byte JSON has SHA-256
`43d13d4c40afedb97247fbc30c9f8daebdf26bc60e35ee8c4d971534ab3f16ba`.
The combined seed retry was still active, so this boundary does not start a
competing full build or four-vCPU smoke. Those checks are deferred to one
consolidated integration run. No raw kernel or image was published, and no
artifact-size policy row changed.

## Consequences

Private CupidC now checks the active icon drawer's complete higher-order type
before a JIT or AOT call. Source callback capacity, the four-byte runtime slot,
the native graphics ABI, and unreviewed binding behavior remain unchanged.

The standalone checked seeds do not contain this private parser. Checked-seed
hosted CupidC remains the production owner of `kernel/lang/cupidc.cc` and the
feature guest. This change transfers no object owner and removes no host
dependency. Callback-valued results, pointer-to-function-pointer `**`
declarators, callback alias chains, aggregate callback contexts, and the other
unreviewed native bindings remain open. `TempleOS/` remains read-only reference
material.
