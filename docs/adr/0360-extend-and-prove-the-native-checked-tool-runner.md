# ADR 0360: Extend and prove the native checked-tool runner

## Status

Accepted on 2026-08-28.

## Context

ADR 0358 added `cupidbuild run` for checked CupidObj calls. The command froze
the six-tool seed, ran one retained image, withheld its captured streams until
the live seed passed a second check, and preserved the tool's exit status. Its
admitted-tool list contained only CupidObj.

The fixed-point gates exercised CupidBuild's typed assembly transactions, but
they did not run the generic checked-tool path through the stage-three and
stage-four CupidBuild images. A compiler or runtime regression could therefore
leave those generations byte-identical while breaking the runner that the next
Make handoff needs.

CupidLD also needs the same checked invocation boundary for later ownership
work. Admitting the linker now permits an exact end-to-end test without moving
either kernel link or claiming that the generic runner owns publication.

## Decision

`cupidbuild run` admits exactly `cupidobj` and `cupidld`. Both the CLI parser
and the library entry reject every other tool name. The runner selects the
frozen CupidLD or CupidObj image from the six-tool seed, applies the existing
timeout and host-execution rules, rechecks the live seed, and forwards the
captured status and streams. No other trust or cleanup rule changes.

The hosted CLI contract compares direct and checked help and invalid-option
results for both admitted tools. A positive CupidLD contract assembles a real
i386 object with the production CupidASM, links it at `0x01C00000` through the
direct and checked paths, and requires byte-identical fixed-address ELF
images. It also checks the ELF header and the absence of a leaked temporary
output.

Both fixed-point behavior matrices now run stage-three and stage-four
CupidBuild against a private behavior seed. Each generation invokes checked
CupidObj `wrap-text` with the same source identity but a generation-specific
output name. The gate requires empty streams, byte-identical relocatable
objects, and a valid i386 `ET_REL` result. A second invocation forwards an
invalid CupidObj option and requires status 2 with the expected usage
diagnostic. Linux therefore records 25 failure, six help, and 32 success
cases. Native Windows records 14 failure, six help, and 19 success cases.

The build-graph audit pins the helper body, both live call sites, the output
comparison, the negative diagnostic, the matrix totals, and the new
`cupid.cupidbuild_checked_cupidobj_runner` capability.

## Evidence

The native Windows CupidBuild CLI module passed 67 tests in 67.228 seconds
with three expected platform skips. The same module passed under WSL with five expected platform
skips. The suite covers both admitted tools, the exact CupidLD link, argument
forwarding, timeouts, seed drift, stream handling, and the existing host
cleanup boundaries.

Focused fixed-point contracts reached the new 25/6/32 Linux matrix and the
14/6/19 native Windows matrix. The build-graph mutation sweep passed in
208.676 seconds, including deletions and dead-block moves of either live
runner check.

## Consequences

Source-head CupidBuild now carries a checked invocation path for CupidLD as
well as CupidObj, and both fixed-point drivers treat the CupidObj path as
required behavior. The promoted seeds still predate this source change, so
the normal graph remains at four CupidBuild and 448 Python participations
until a paired seed promotion and a separate Make handoff.

The generic command still does not own destination locking, candidate
inspection, or atomic replacement. CupidObj and CupidLD retain their own
output behavior. Moving either kernel link to this runner will require a
separate publication decision and proof.

No active C source changes ownership in this step, so no `.c` file changes
suffix. `TempleOS/` remains read-only reference material.
