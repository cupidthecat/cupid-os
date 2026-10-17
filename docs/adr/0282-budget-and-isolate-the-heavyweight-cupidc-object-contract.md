# ADR 0282: Budget and isolate the heavyweight CupidC object contract

## Status

Accepted on 2026-08-14. The complete native Windows user-equivalence gate has
passed with this policy.

## Context

The checked Toolchain publisher compiles fifteen contract programs with both
stage-three and stage-four CupidC. Its earlier stage-two and stage-three path
used one 900-second limit for every
contract and admitted two compiles at a time.

Two `make test-user-native-windows-equivalence` attempts timed out after
1,204.051 and 2,104.041 seconds without publishing the contract cohort. The
last observed work in both attempts was
`toolchain/tests/cupidc_object_contract.cc`. The promoted Linux seed later
compiled that source by itself in 826.192 seconds and produced a valid
2,497,288-byte i386 relocatable object. The old limit therefore left 73.808
seconds of idle-host headroom. Running another compiler beside it could use
that margin without exposing a compiler defect.

The object contract is intentionally large. It checks the emitter, ELF32
layout, relocations, target ABI, recovery, and deterministic output. Reducing
the source or weakening its coverage would make the toolchain fit the test
instead of strengthening the toolchain.

## Decision

Compile budgets belong to `ContractPlan`. An ordinary contract keeps the
900-second default. The `cupidc-object` plan receives 1,800 seconds.

The fourteen ordinary contracts retain the worker pool. That pool must finish
and close before the `cupidc-object` compile starts alone. The hosted runtime
probe remains a separate 360-second compile. All fifteen contract links retain
their worker pool and 360-second limit.

A compile timeout names the stage, source, and applied budget. The scheduling
policy is not added to the publication schema. The publisher module is one of
the 65 hashed contract inputs, so a policy change still invalidates an old
publication and forces a checked rebuild.

## Evidence

Test-first coverage reproduced four gaps in the old scheduler: plans had no
budget, the object contract received 900 seconds, its compile overlapped the
ordinary cohort, and its timeout omitted the applied limit. Review then bound
the extended budget to exclusive admission and required all fourteen ordinary
compiles to finish before the object contract enters. The five focused
scheduler tests pass in 0.098 seconds. The complete
`tests.test_cupidc_toolchain_contracts` module first passed all 41 tests in
6.392 seconds. Python bytecode compilation also passes from a private cache.

The first supported gate with the new scheduler completed the isolated
stage-two object compile, then exposed a separate stale link closure. The
checked `cupidasm-kernel-elf` plan omitted `cupidasm`, `x86`, and `cupidld`
even though its native contract already linked them. CupidLD rejected the
unresolved strong symbol, and the publisher left no partial cohort. A new
plan test reproduced the omission before the checked plan adopted the complete
native closure. The six focused tests now pass in 0.128 seconds, and the full
module passes all 42 tests in 8.504 seconds.

The next supported run completed both isolated object compiles, both complete
kernel-ELF links, all cross-stage object and executable comparisons, and the
hosted runtime in 4,480.3 seconds. It then found that the publication verifier
still expected the pre-convergence fixed-point record. The bootstrap report
already named stage three and stage four as its compared pair. A positive
publication test reproduced the rejection, and a wrong-pair negative now
guards the boundary. The v2 manifest verifier requires that exact convergence
pair. Eight focused scheduler, closure, and publication tests pass in 0.302
seconds, and the full module passes all 43 tests in 8.864 seconds.

The third supported run passed in 4,565 seconds. It published 21 artifacts in
a 22-entry directory, including the manifest. The 22,591-byte manifest has
SHA-256
`074796535f19d9797c6f2d90ac43283fa1e32cd2a8530b9600e6ddd6c97419ad`.
It binds 65 contract inputs, seventeen object comparisons, sixteen executable
comparisons, and the stage-three and stage-four fixed-point pair. A separate
public verifier run accepted all 21 artifacts.

The same Make gate validated syscall ABI version 5 with 103 fields, a 412-byte
table, and 101 providers. It then built the three user programs with the
promoted native Windows CupidC and CupidLD and compared them with the checked
seed:

| Program | Object bytes | Object SHA-256 | Executable bytes | Executable SHA-256 |
| --- | ---: | --- | ---: | --- |
| `hello` | 6,124 | `64e0a6ee0d7a45a0901d3db614e73481cdc6b30903345c5015601b2bf344be04` | 13,992 | `4c5622969f39ffe7c2427d65abae2d293dfbd76db2aa80c96f9e6cf01613600c` |
| `ls` | 7,120 | `e0627996a1d9cd6fd428642ffdfada7e07afa81d9267bc714360014af0dd3971` | 18,112 | `094b017eb6914bce6fbc1e99adeae845d5dc05280c1c1d897e68ab9d687c8d79` |
| `cat` | 6,292 | `ff002fc4710704c3941bf6320249e772a3448d15f99269987ab1b9b608b3acb4` | 13,992 | `b66cba4c98221f5006ad4aeee70349a82db20410e027aa863bc33fa5818b5f4c` |

All six comparisons are true. Their 46-input user closure has SHA-256
`726926348f390003ae24e527f1b657e7c83c78a9e8872b884fd8a0791cdf571b`.
The same supported Make target then passed against the current publication in
15.9 seconds. It verified the cohort, repeated the syscall ABI check, and
repeated all six user comparisons without rebuilding the contract stages.

A final publication review then found that the manifest named stage three and
stage four while the publisher still copied stage-two contract executables and
tools. The promoted seed hid this mismatch because it already reproduced those
bytes. A new orchestration test gives each generation distinct bytes. It first
failed because the builder requested stage two and stage three. The checked
path now builds and compares the stage-three and stage-four contracts, runs the
stage-four runtime, and publishes only stage-four contract executables and
tools. The focused provenance test passes, and the complete contract module
passes all 44 tests in 8.223 seconds. That module result covered the wiring,
while the supported Make path still required a full reproof.

The first reproof attempt on that path was stopped before publication when the
full graph audit found that the checked conditional manifest still counted 32
active `_WIN32` predicates while source head contained 33. Finishing would
have installed a cohort that the source audit rejected. The private proof tree
was removed, the reviewed count was updated, and all 44 contract tests passed
again in 9.299 seconds before the final reproof started.

The final supported `make test-user-native-windows-equivalence` reproof passed
in 4,589.9 seconds. Stage-three and stage-four contract objects and executables
matched across seventeen object and sixteen executable comparisons. The gate
ran the stage-four hosted runtime, published only the stage-four cohort, and
verified all 21 artifacts from 65 inputs. Its 22,591-byte manifest has SHA-256
`ff193cf81293553706373f5a37d0fedf3dfae0bebcbc608d892a4f40ea3d9629`.
The syscall ABI and all six native Windows user comparisons passed in the same
run. A direct publication verification accepted all 21 artifacts. The warmed
supported path then passed in 12.2 seconds and recognized the cohort as
current.

## Rejected alternatives

Raising every contract to 1,800 seconds was rejected. It would hide which
source needs the larger budget and double the failure bound for ordinary
contracts.

Serializing every compile was rejected. The ordinary cohort has enough margin
and benefits from bounded parallel work.

Shrinking the object contract was rejected because that would remove emitter,
ELF32, relocation, ABI, recovery, or determinism coverage. CupidC must compile
the complete contract under the measured resource policy.

Retrying a timed-out compile was rejected. A retry would mask admission
pressure, repeat expensive work, and make the publication gate harder to
reason about.

## Consequences

The checked contract build gives its heaviest source the measured
1,800-second budget without reducing coverage or changing compiler output.
Ordinary compilation and all links remain parallel. A timeout still aborts
before publication, with a diagnostic that identifies the exact budgeted
operation.

This decision changes no code generation, ABI, artifact name, manifest schema,
seed identity, production owner, or source suffix. No `.c` file earned a
`.cc` rename.
