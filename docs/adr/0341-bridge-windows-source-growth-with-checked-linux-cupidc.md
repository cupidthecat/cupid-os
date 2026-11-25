# ADR 0341: Bridge Windows source growth with checked Linux CupidC

## Status

Accepted on 2026-08-24.

## Context

The promoted Windows CupidC uses 16 KiB arena blocks. Windows reserves virtual
address space in 64 KiB allocation-granularity units, so each block consumes
four times its payload in the 32-bit process. After the PE32 inspector,
callback, and CupidBuild work landed together, that compiler reached its
practical address-space boundary while lowering `cupidc_frontend.cc`.

The checked Linux seed compiled the same source successfully. Rewriting or
splitting the active compiler source to fit the older Windows image would hide
a real runtime limit and make the compiler source worse.

## Decision

The Windows fixed-point driver keeps the checked Windows execution seed for
CupidASM, CupidDis, and CupidLD in stage two, but takes stage-two C objects from
the checked Linux seed's CupidC. The Linux compiler runs through the existing
private WSL boundary. No host compiler, assembler, linker, or binary utility is
available to the proof.

The resulting stage-two Windows CupidC uses 64 KiB arena blocks. Its total
512 MiB arena budget is unchanged, but each block now matches Windows virtual
allocation granularity instead of wasting three quarters of each reservation.
Stage two remains transitional. Native stage-two tools build stage three,
native stage-three tools build stage four, and publication still requires
every stage-three and stage-four object and PE image to match.

When `PATH` is deliberately poisoned, the runner may resolve only the known
`System32\wsl.exe` fallback. A missing PATH entry and a missing system copy
remain a hard failure.

Historical seed proofs now intersect the source-head inventory with the named
commit's tree before reconstructing its digest. This keeps the promoted
50-input snapshots reproducible after source head grows to 55 inputs. The
stored count and digest still reject a missing or changed historical input.

## Evidence

Focused snapshot, named-revision, WSL-resolution, rollback, and producer-role
tests pass. The first complete bridged Windows reconstruction built all three
native PE generations and reached a passing fixed-point report. Its only test
failure was a stale expectation that source-head CupidC and CupidDis would
remain byte-identical to the older promoted seed; the report correctly marked
both as changed while CupidASM, CupidLD, and CupidObj remained equal.

The corrected native fixed-point test passed in 1,126.471 seconds. It froze 55
source inputs, built 20 C objects, two assembly objects, and five tools in each
generation, matched every stage-three and stage-four artifact, and passed the
5-help, 8-success, and 9-failure behavior matrix. The report correctly records
CupidC and CupidDis as changed from the older execution seed while CupidASM,
CupidLD, and CupidObj remain equal.

## Rejected alternatives

Increasing the old seed's arena limit would not fix the allocation-granularity
waste and would require replacing a checked binary without a reproducible
promotion path.

Shrinking or awkwardly splitting `cupidc_frontend.cc` would make active source
conform to an obsolete bootstrap constraint.

Calling a native host compiler for stage two would break the Cupid-only
producer boundary. The checked Linux CupidC bridge keeps code generation under
Cupid ownership while native generations converge.

## Consequences

The normal Windows build continues to run output-bearing tools from the native
checked execution seed. Only fixed-point reconstruction uses WSL for the
stage-two C bridge. Python still coordinates freezing, process launch, drift
checks, and publication.

The next seed promotion can remove the bridge once its native CupidC carries
the 64 KiB arena-block policy and builds the current source closure directly.
`TempleOS/` remains read-only reference material.
