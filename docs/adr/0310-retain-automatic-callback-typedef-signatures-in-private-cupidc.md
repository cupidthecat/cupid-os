# ADR 0310: Retain automatic callback typedef signatures in private CupidC

## Status

Accepted on 2026-08-21.

## Context

ADR 0303 carries a file-scope function-pointer typedef signature into a direct
free-function parameter. Private CupidC still reduced an automatic object or a
Cupid class method parameter declared with that typedef to a four-byte pointer.
An indirect call through either spelling could lose fixed conversions, arity,
record identity, variadic state, and its result channel.

The active UHCI and EHCI poll paths both declare `usb_complete_cb_t cb = NULL`
before copying a queued callback into that automatic object. These declarations
need a real language rule even though later callback assignment remains a
separate compiler boundary.

## Decision

Capture the typedef index immediately after parsing the declaration type. Copy
the referenced callback signature into every typedef-typed automatic
declarator. A comma-separated declaration gives each object its own copy.

Use the existing callback initializer probe, compatibility checks, provisional
target handling, signature journal, and program transaction. A missing
initializer and an integer constant zero produce a null callback. An explicit
cast through `void *` remains the intentional erasure path. A later function
definition still resolves the existing forward patch and must match any
provisional signature.

Carry the same typedef index through each Cupid class method parameter record.
The ordinary cdecl parameter binder copies the signature to the method's local
parameter symbol. The method body then uses the same indirect-call validator,
slot conversion, stack cleanup, and XMM0 result path as a free function.

A failed initializer, method, function, program, or REPL evaluation restores
emitted code and data, patches, candidate signatures, typedef metadata, and all
other parser state together.

## Evidence

The first local and method JIT and AOT tests compiled but returned 0 instead of
34. Both forms lost the declared `double, int` boundary. After retaining the
typedef index, those four executions return 34.

`python -B -m unittest -q tests.test_private_cupidc_call_abi` passes all 248
tests in 43.968 seconds. The added cases cover mixed-width scalar slots, exact
12-byte cleanup, SIMD arguments and XMM0 results, separate comma declarators,
null initialization, explicit `void *` erasure, and later definitions in both
JIT and AOT. Negative cases cover result, parameter, record identity, variadic,
and arity mismatches. Every failure is followed by a valid compile in the same
state, including failed later definitions and a failed method that reuses its
full name.

The first shared-helper draft sent a signature-erased callback alias chain
through the typed validator. Its JIT and AOT regression stopped with
`function-pointer initializer result does not match declaration`. The final
gate enters the typed path only after the typedef signature copy succeeds. The
same alias-chain fixture now compiles and runs through the legacy initializer
path in both modes without claiming retained metadata.

The same module pins the active USB callback typedef and its UHCI and EHCI
automatic declarations. `tests.test_gui_terminal_smoke` passes all 125 tests in
0.753 second. It requires
`[feature14-callback-automatic] PASS local=4 method=4 calls=2` after the
existing typedef callback marker and rejects the matching failure marker.

The later integrated four-vCPU guest frontier printed the automatic marker
once after the global callback marker and completed the feature run cleanly.
Its 148,491-byte log has SHA-256
`b31fcc79c861cbdead01967c1417409f7a8cdf46cc375300a17e64df4beca041`.

At the focused automatic-object and method-parameter checkpoint,
`make kernel/lang/cupidc_parse.o kernel/lang/cupidc_elf.o` passes in 68.8
seconds with the promoted Windows checked seed. `cupidc_parse.o` is 461,624
bytes with SHA-256
`117a446f37ee9382b0fba48aa395359544711e3298e702cd4391bea090b08ce4`.
`cupidc_elf.o` remains 3,604 bytes with SHA-256
`c2ad171aacd493a33a477e7a3196a5d28b04b0f74521cd8cbaec2598f391880c`.

## Rejected alternatives

Do not infer the signature from call arguments or the initializer target. The
typedef declaration is the ABI authority, including when the target is null or
defined later.

Do not special-case USB names. The active declarations are source evidence for
a general automatic-object rule.

Do not widen this change to globals, fields, block-static objects, or later
assignments. Those paths need their own storage and mutation ownership work.

## Consequences

File-scope callback typedefs now keep their signature across direct
free-function parameters, Cupid class method parameters, and automatic objects
initialized in their declarations. Mixed scalar and SIMD indirect calls use
the declared ABI in JIT and AOT output.

Record and class fields, block-static objects, later assignment to automatic
objects, recursive signatures, direct structure or array results, and
arbitrary computed callback expressions remain open. ADR 0306 separately
records global callback objects and their checked plain assignment. The slice
changes no build owner, checked seed, or host dependency. `TempleOS/` remains
read-only reference material.
