# ADR 0366: Admit the 59-input seed transition

## Status

Accepted on 2026-08-29.

## Context

ADR 0365 added hosted `<stddef.h>` and raised the frozen bootstrap closure from
58 inputs to 59. Linux and native Windows candidate fixed points converged over
that closure. Their CupidBuild images still contained the v2 manifest parser's
older requirement that `source_input_count` equal 58.

The mismatch appeared when the provisional images and 59-input manifests were
used for the normal OS build. CupidBuild rejected the manifest with
`fixed-point provenance differs` before it ran CupidASM or changed the target.
The candidate and manifest bytes were removed from the active tree rather than
weakening the production check.

## Decision

Accept exactly 58 or 59 as a promoted v2 manifest's source input count in
source-head CupidBuild. Keep the legacy v1 count at 50. Reject 57, 60, and
every other count.

The two accepted v2 counts form a bounded transition. Fifty-eight preserves
operation with the active paired seeds. Fifty-nine admits the generation that
adds hosted `<stddef.h>`. Revision, snapshot, parent lineage, schema, build
plan, target, artifact inventory, and every seed image remain independently
checked.

The source-current closure has 59 inputs and SHA-256
`3c3218219472735ba1073e1ca7b1f67ee75bf123fb0be77d2c65e019a6aebdef`.
The Linux and native Windows build plans do not change.

## Evidence

The new regression first passed for 58 and failed for 59 with the production
`fixed-point provenance differs` diagnostic. After the parser change, both
accepted counts reached the checked execution profile. Separate negative cases
prove that 57 and 60 fail before the previous output can be replaced.

The focused transition and provenance tests pass together. Full CupidBuild,
audit, OS-build, and bootstrap checks are recorded in the bootstrap log for
this source checkpoint.

The complete CupidBuild module passed 77 tests, with three expected
platform-specific skips. The manifest and runner contract modules passed 65
tests, with three expected Windows skips. Both active 58-input seed cohorts
also passed their manifest checks unchanged.

The 65 CupidC publication and coordinator contracts passed. The active x86
source-manifest test now binds all 1,557 instruction records and the current
startup-source line labels; its instruction bytes and selected signatures did
not change.

The full OS graph then rebuilt the kernel, generated sources, drivers,
in-kernel tools, browser, and all 83 Doom roots. Both links and the two-image
CupidDis check passed. The exact-size gate accepted all 16 artifacts after the
flat-kernel row was updated to the measured 9,509,116 bytes, and image
publication completed.

The three artifact-policy and checked-runner modules passed 54 tests, with
four expected Windows skips. The complete build-graph module passed all 112
tests.

## Alternatives considered

Removing the source-count check was rejected. A promoted manifest must describe
the closure that produced its tools, and accepting an arbitrary count would
discard useful provenance.

Changing the provisional manifest back to 58 was rejected. Hosted `<stddef.h>`
is an actual compiler input and must be included in the reported closure.

Keeping only 59 was rejected for this transition. Source-head CupidBuild is
used with the active 58-input seeds before the new pair can be promoted.

## Consequences

The next paired candidates can consume their 59-input manifests without losing
compatibility with the checked 58-input pair. The provisional candidates that
exposed this problem are not promotion evidence; both platforms must rebuild
from the named source commit and pass self-consumption again.

The JPEG Make recipe remains Python-coordinated until a valid paired promotion
puts the typed CupidBuild command in the active seeds. No `.c` source is added,
and `TempleOS/` remains read-only reference material.
