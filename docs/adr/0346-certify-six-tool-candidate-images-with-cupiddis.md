# ADR 0346: Certify six-tool candidate images with CupidDis

## Status

Accepted on 2026-08-25.

## Context

The source-head fixed points compare six Linux tools and six native Windows
tools between stages three and four. Their behavior gates ran each command and
exercised CupidBuild's guarded object transaction, but they did not explicitly
ask the compared CupidDis images to certify every compared executable.

That left a gap before seed promotion. Byte equality proves that consecutive
generations agree, but it does not prove that the common image still satisfies
the known-decode, local-target, and code-anchor policies expected of a static
Cupid tool.

## Decision

Make candidate-image certification part of both fixed-point behavior gates.
Stage-three CupidDis inspects each stage-three candidate, and stage-four
CupidDis inspects the corresponding stage-four candidate. Every call uses
`--require-known --require-local-targets --require-code-anchors`, must return
success, and must write nothing to either stream. Give each whole-image check
up to 360 seconds because the largest current CupidC images take more than two
minutes to inspect on the supported host paths.

Add one useful negative to each host gate. The coordinator copies the
stage-three CupidBuild image, locates its file-backed entry instruction through
the ELF32 program headers or PE32 section table, and replaces the first two
bytes with `0F FF`. Both compared CupidDis images must reject that private copy
with status 1, no stdout, and a `code check failed` diagnostic. The original
candidate remains unchanged.

Count these cases in the public fixed-point record. Linux now has 23 failures,
six help cases, and 29 successes. Native Windows has 12 failures, six help
cases, and 16 successes.

Keep the audit fail-closed. It requires the format-aware corruption helper,
the three strict flags, all six positive inspections, the negative result and
diagnostic checks, one live helper call in each behavior runner, and the exact
summary counts.

## Evidence

The public behavior test first failed because neither runner made a strict
real-image call. After adding the six positive checks, it failed again because
the corrupted CupidBuild case was absent. The completed focused five-test set
passes in 0.326 seconds. Python compilation and `git diff --check` also pass.

The build-graph contract test first rejected the old behavior matrices. The
first expanded sweep covered a missing helper, a weakened strict flag, a
removed corruption step, accepted negative stdout, dead Linux and Windows
calls, and stale counts. It passed in 201.931 seconds. The final sweep adds the
PE32 branch, invalid replacement bytes, and the 360-second budget. It passes in
214.414 seconds.

The first complete Linux and Windows attempts built all final images, then
timed out while strictly inspecting the large stage-three CupidC image. Both
CupidDis processes were still responsive and CPU-bound at the original
120-second limit. A focused regression captured all seven checks using that
short limit. Raising only the certification timeout to 360 seconds made the
regression pass; the audit locks that budget. Neither failed attempt published
its private cohort.

The restarted complete fixed points pass. Linux matches 22 C objects, one
startup object, and six images, then passes the 23/6/29 behavior matrix. Its
50,082-byte report has SHA-256
`ec25194a2c458541be2e1179283b2efaed0de827835fa665c7d42fe5f9938f78`.
Native Windows matches 23 C objects, three assembly objects, and six images,
then passes 12/6/16 behavior. Its 63,124-byte report has SHA-256
`e073a1ecb7fc34d2cdd60868a6a9daaba863f4c93f9797b18e894e9143340e8b`.
Both reports retain the 58-file source digest
`497cd80f8491d6952ae6c86c12f4838db05b4a4f9a542d3bfd5755be21304878`
and mark every stage-three to stage-four comparison equal. The final
CupidBuild images keep their previous bytes and digests.

`make bootstrap-audit` and `make check-bootstrap-audit` pass against the final
contract. The 2,738,394-byte JSON has SHA-256
`0c5828107e2714334cd7aef4ba0590b35beed6df900b85570bb1995d11c9141f`,
and the 12,856-byte Markdown summary has SHA-256
`8ffe80bce9188c32cf4badbae9d7e67142e0981196c648cbca9e5cef9a863b25`.

Before the review hardening, the then-current complete
`tests.test_toolchain_bootstrap_seed` module passed all 113 tests in 3,752.741
seconds. That run repeated independent Linux and native Windows fixed points,
exercised the six-image checks and corruption rejection through the public
runners, and covered the existing publication and failure paths.

The standards review found one low-risk source of drift: the new PE32
corruption path used the raw section size while the existing validator bounded
file-backed entry bytes by both virtual and raw size. A focused negative first
failed because a one-byte virtual extent still accepted a two-byte
replacement. ELF validation, PE32 validation, and corruption now share one
file-backed entry-offset rule. The audit requires all three users and rejects
a raw-size-only mutation. Its complete mutation sweep passes in 208.541
seconds. Thirteen affected candidate and ELF32/PE32 entry tests pass after the
change. Direct reruns against the retained final Linux and Windows cohorts also
pass all twelve positive image checks and both corrupted-CupidBuild negatives.
The separate specification review found no missing requirement, scope creep,
or incorrect behavior in this slice.

## Consequences

The six-tool candidate now has semantic executable evidence for every image
that the fixed point compares. A shared malformed image can no longer pass
only because stages three and four produced the same bytes.

This decision does not promote CupidBuild, change either five-tool seed, alter
an object or executable format, transfer a normal Make recipe, or remove
Python coordination. Those steps still require the non-circular six-tool trust
contract identified by ADR 0345.

No active `.c` source is eligible for renaming in this change. `TempleOS/`
remains read-only reference material.
