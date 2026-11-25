# ADR 0393: Adopt closed kernel compilation in Make

Date: 2026-09-20

## Decision

Promote the paired Linux and Windows tools built from source checkpoint
`9d2529a7` only after their clean fixed-point proofs and independent artifact
checks pass. Both cohorts must bind the same 59 source inputs. The compared
stage-three and stage-four tools exercise closed compilation with their own
complete six-tool seed, including output parity, input failures, unchanged
output reuse, recovery, and cleanup.

Move all 157 kernel-profile compiler recipes to the checked
`cupidbuild compile-kernel` operation. Every rule retains its exact source and
output binding, complete source/header closure, Makefile dependency, six-tool
seed dependency, and existing profile scheduling edges. The kernel compiler
profile and generated-symbol timeout remain unchanged. No source is weakened
or removed. The three generated installation tables, user-program compilation,
and 83 Doom compilations keep their separate paths.

The source audit independently describes and checks each native recipe. It
rejects changed flags, source/output bindings, missing or repeated closure
inputs, incomplete seeds, and wrapper reintroduction. Production ownership
becomes 354 CupidBuild and 98 Python participations across the same 452
transforms. Make, Python image publication, and Python bootstrap verification
remain host dependencies. This step does not establish an in-OS build.

## Validation boundary

`tools/validate_kernel_compile_handoff.py` provides a reproducible production
check. Its preparation runs the ordinary image build, retains every kernel
object and declared artifact, and snapshots compilation controls, including
all 304 Doom headers. It then poisons Python compiler coordinators, the checked
Python runner, and host C/ASM/link commands.

The first validation phase forces and executes the original profile
transaction. The second forces all 157 real compiler sources through the
original Make graph without the profile overlay. Both phases ignore only the
empty phony `FORCE` trigger; every real prerequisite and ordering edge remains
reachable. The profile can remain older than its prerequisites when its
validated bytes do not change. Make's dry run then predicts downstream Doom
wrappers that actual execution leaves current. The validator records these
predictions as advisory evidence. A real Make regression covers the older
profile and confirms that a changed Doom input fails actual poisoned replay.

Dry-run and execution logs must each contain one exact bound profile command
in the first phase, then all 157 exact compiler calls in the second. The second
phase allows zero or one fully bound profile command and requires execution
to match that planned count. Each phase requires unchanged control and
artifact bytes and timestamps. An executed poisoned command, timeout, input
change, output change, or missing command fails the report. Timeout cleanup
covers the utility's own process children. A
private-image runtime smoke remains a separate required check.

The bootstrap log records actual host results, paired manifests, source and
artifact hashes, and any failed approaches. Source capability, seed carriage,
recipe ownership, and runtime acceptance are separate claims.

## Remaining work

Doom compiles through CupidC but still needs a native coordinator policy that
allows object publication inside the recursively discovered source tree.
`NEXT-DOOM-COORDINATOR.md` records the required identity, membership, content,
and concurrent-publication tests. Existing strict profile-manifest checks must
remain intact. Full IWAD-backed gameplay acceptance remains open.

All 157 kernel roots already use `.cc`. TempleOS remains reference material
and is excluded from the build and ownership counts.
