# ADR 0284: Enforce CupidC source suffix ownership in the build audit

## Status

Accepted on 2026-08-14.

## Context

Cupid OS uses `.cc` to mark source that has earned checked CupidC ownership.
The suffix is an ownership claim, not a spelling preference. A source moves
only after CupidC builds it through a supported graph edge and the relevant
behavior proof passes.

The repository still tracks seventeen `.c` files outside `TempleOS/`. The
evaluated root, user, and hosted Toolchain graphs reach none of them. Seven are
historical `bin/` copies, three are superseded implementations, one is an
unlinked runtime draft, five are native host behavior fixtures, and one is an
optional host compiler oracle.

No file in that census has earned a rename. A `bin/*.c` rename would make an
old copy visible to wildcard discovery. A host fixture rename would select C++
semantics and report the wrong owner. Renaming a superseded or dormant source
would add no checked CupidC build or behavior evidence.

The human-readable inventory already explained that boundary, but the build
audit did not enforce it. It also reported the dormant and host-owned files as
generic `not_reached` entries, which hid why they remain ordinary C.

The native evidence run also found that `tests/kernel_process_contract.c` no
longer linked with the current `process.cc`. The production source had gained
four time, paint, and graphics cleanup dependencies since the fixture last
updated.

## Decision

The active-source audit publishes a `c_source_ownership` contract. It counts
tracked `.c` sources, separates active and unreachable paths, and records the
evaluated build and runtime owner for each active path. Any active tracked
`.c` source with CupidC as its runtime owner fails the audit and must use
`.cc`.

The rule is ownership-aware. An active `.c` input compiled by a native host
test remains valid. The contract does not turn `.cc` into a general ban on C
or silently change a fixture's language mode.

The unreachable inventory records project-specific reasons for the remaining
seven paths that are neither historical copies nor superseded sources:

- `kernel/lang/cupidc_runtime.c` is `dormant`.
- The five native behavior inputs under `tests/` are `host_fixture`.
- `toolchain/tests/elf32_oracle.c` is `host_oracle`.

The seven `bin/` copies remain `historical_copy`, and the three replaced
implementations remain `superseded`.

The process fixture retains native C mode and supplies only the four missing
host adapters. The timer adapter returns the fixture's fixed zero time, the
paint adapter has no external state, and the two graphics cleanup adapters
report success. Their real declarations come from the production headers.
`kernel/core/process.cc` is unchanged.

## Evidence

Test-first coverage reproduced two missing controls. A checked CupidC Make
edge accepted `main.c`, and the residual policy paths appeared as generic
`not_reached` entries. The new negative receives the focused diagnostic
`CupidC-owned tracked .c source must use .cc: main.c` without writing an audit
report. The policy test now distinguishes `dormant`, `host_fixture`, and
`host_oracle`.

A positive checked CupidC edge using `main.cc` passes with no tracked `.c`
source. A separate host-owned `active.c` fixture also passes and keeps
`host_c_compiler` as its build owner. The real Make graph reports seventeen
tracked `.c` files, zero active, zero CupidC-owned, and all seventeen in the
audited unreachable inventory.

Four focused ownership and inventory tests pass in 1.615 seconds. The complete
`tests.test_build_graph_audit` module passes all 87 tests in 823.153 seconds.
The first `make bootstrap-audit` and deterministic check pass in 73.3 and 74.5
seconds. After the host fixture repair, final regeneration and checking both
pass in 74.2 seconds.

The first native fixture group ran 36 tests in 4.551 seconds and stopped when
the process contract linker reported the four missing symbols. The focused
process fixture then passed all 20 cases in 0.556 seconds. The repeated native
C group passed all 56 kernel, USB, and ELF32 oracle cases in 4.530 seconds.
Five final ownership tests, including the real three-root census, pass against
the completed tree in 37.106 seconds.

## Rejected alternatives

Renaming the historical copies was rejected because wildcard discovery would
activate stale implementations.

Renaming native fixtures or the optional oracle was rejected because `.cc`
would change their host language mode and falsely claim CupidC ownership.

Renaming the dormant or superseded files was rejected because no supported
build or behavior proof reaches them.

Adding a compiler feature for one of those files was rejected because no
active source requirement selects it. Compiler work remains driven by source
that participates in the supported graph.

Deleting the residual files was not part of this decision. Their separate
historical, test, and cleanup purposes need their own reviewed changes.

## Consequences

The checked audit now prevents a new CupidC-owned `.c` edge from entering the
supported graph. Reviewers can distinguish historical, superseded, dormant,
host fixture, and host oracle paths in the generated inventory.

No source suffix, compiler output, ABI, build owner, artifact, or host
dependency changes in this step. The native process fixture again follows its
current production source. The safe rename set remains empty until a real
source earns CupidC ownership.
