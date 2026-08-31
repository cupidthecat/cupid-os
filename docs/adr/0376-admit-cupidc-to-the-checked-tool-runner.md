# ADR 0376: Admit CupidC to the checked-tool runner

## Status

Accepted on 2026-08-30.

## Context

The generic `cupidbuild run` boundary already launched CupidObj and CupidLD
from a frozen six-tool seed. It checked the manifest, complete directory
membership, declared artifact bytes, target execution profile, working
directory, captured streams, timeout, cleanup, and the live seed after the
tool returned.

Production CupidC wrappers still entered an equivalent checked boundary
through Python. Moving those recipes requires a CupidBuild image that can
select CupidC without weakening the runner or giving every seed role a generic
execution path. The fixed-point drivers also needed to exercise a real
compiler operation through consecutive CupidBuild generations before any seed
or recipe transfer could be considered.

## Decision

Admit exactly CupidC, CupidObj, and CupidLD in both the `cupidbuild run` parser
and library entry. CupidASM, CupidDis, CupidBuild, missing roles, and malformed
requests remain rejected. CupidC selects artifact index 1 from the same frozen
six-tool seed. No manifest, execution-profile, host-runner, stream, timeout,
cleanup, or live-drift rule changes.

Extend the public CLI contract with direct and checked CupidC help and
invalid-option comparisons. Compile one freestanding `.cc` source through both
paths and require identical i386 `ET_REL` bytes. Compile a malformed source
through both paths, require the same nonzero status and diagnostic, and prove
that both existing output files remain unchanged.

Add three checked CupidC cases to both fixed-point behavior drivers. The help
case verifies the public command. The success case compares the stage-three
and stage-four relocatable objects and validates their ELF32 type and machine.
The failure case compares the diagnostic and requires both sentinel outputs to
survive. The source-head behavior inventories become 29 failure, seven help,
and 36 success cases on Linux, plus 18 failure, seven help, and 23 success
cases on native Windows.

The build-graph audit treats the helper, both live call sites, result checks,
matrix totals, and `cupid.cupidbuild_checked_cupidc_runner` capability as one
fail-closed contract. Its mutation tests reject a missing helper, a removed
output comparison, and either call moved under a dead block.

## Evidence

The complete CupidBuild CLI and host-runner suite passed 106 tests in 117.338
seconds with seven platform skips. It includes the new positive and negative
compiler cases and the existing manifest, profile, seed-drift, stream, status,
timeout, and cleanup coverage.

Focused fixed-point carrier tests passed for both host paths. They inspect the
checked compiler help, success, and failure commands and the registered matrix
counts. The deterministic active-source audit regenerated successfully with
the new capability and totals, and check-only mode accepted those outputs
without drift. The complete fixed-point contract mutation sweep passed in
408.127 seconds. Full Linux and native Windows fixed-point reconstructions were
not run for this source-only admission.

## Rejected alternatives

Admitting all six seed roles was rejected because generic execution must be
granted for a demonstrated consumer, not inferred from seed membership.
CupidASM, CupidDis, and CupidBuild have no ownership transfer in this change.

Changing production compiler wrappers in the same step was rejected because
the active checked seeds predate this source capability. A recipe handoff
before paired fixed-point reconstruction and seed promotion would select a
runner that still rejects CupidC.

Adding compiler-specific publication logic to the generic runner was rejected.
The runner forwards tool behavior and checks the trust boundary; destination
locking, candidate validation, and atomic publication remain caller contracts.

## Consequences

Source-head CupidBuild can run the promoted CupidC artifact under the existing
native checked-tool boundary. The compiler operation is now part of both
fixed-point behavior contracts, so a future paired reconstruction must prove
it on Linux and native Windows before promotion.

The active seeds, production compiler wrappers, Make graph ownership, and
normal OS outputs do not change. A later seed promotion and any compiler
wrapper transfer remain separate green commits. No source suffix changes in
this step because no active `.c` file changes ownership. `TempleOS/` remains
read-only reference material.
