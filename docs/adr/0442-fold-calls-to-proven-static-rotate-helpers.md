# ADR 0442: Fold calls to proven static rotate helpers

## Status

Implemented privately on 2026-10-06. Strict native emitter compilation and all
eight frame and call controls pass on Windows and Linux. Current Cupid
compilation, a compression benchmark and complete large-publication acceptance
are separate checks. Full compiler qualification and OS acceptance remain open.

## Context

The guarded publisher repeatedly hashes candidate and previous-output bytes.
Its unchanged SHA helper uses a pure unsigned rotate. The leaf optimization
shortens that helper, but each call still performs the normal cdecl argument
reshuffle, stack alignment, call and cleanup. The Linux 200 MiB replacement
controls exceed their original deadlines. A retained 64 MiB probe takes
246.115 seconds; publication and cleanup account for most of that time.

## Decision

After the ordinary direct-call validation and argument-transport checks,
recognize an internal-linkage, attribute-free helper with the exact pure
fourteen-instruction rotate body described in ADR 0440. Require two actual
arguments of its unqualified unsigned four-byte return type, a matching local
definition, no block bindings or labels and valid instruction bounds.
Reject the fold for any function attribute or unmatched body.

Argument expressions keep their ordinary evaluation. The call instruction
consumes their two stack values into EAX and ECX, selects ROR or ROL through
the shared x86 encoder and pushes one result. Reversed parameter order selects
the opposite POP order. Values below the arguments remain on the abstract
stack. There is no actual call and therefore no outgoing cdecl alignment or
argument reshuffle at this instruction.

External and indirect calls retain their existing paths. The fold preserves
`noinline`, volatile helper parameters, mismatched types and bodies with other
effects. It adds no syntax, IR fields or source special cases. Counts outside
the source expression's defined range have no new result guarantee. The
publisher retains every complete digest and identity check and the existing
600-second case and 1,800-second suite limits.

## Evidence and limits

The runtime fixture covers both directions, reversed declarations, unsigned
long, surrounding live values and side-effecting argument expressions. Six
values across counts 1 through 31 execute on each host. Both argument counters
must reach 186. Relocation checks require no helper call at selected sites and
retain direct calls for external, `noinline` and qualified helpers. An indirect
call retains its CALL-register instruction. Existing frame controls also pass.
The retained records are `rotate-call-emitter1-native2-*`.

The first native test attempt omitted GNU mode for the `noinline` fixture and
GCC rejected two implicit register-index narrowings. The fixture now uses the
existing `--gnu` option and the emitter explicitly converts the proven zero
or one register index to its byte-sized type. A later harness attempt used an
unsupported option spelling. These failures remain retained. None establishes
an implementation or qualification pass.

The derivative links a freshly compiled native emitter with retained earlier
native compiler components. Full paired Cupid producer qualification, contract
capture, code-shape expectations, source-frontier measurement and a normal OS
build with strict runtime checks are required before adoption.
