# ADR 0317: Compare bootstrap stages with retained seed bytes

## Status

Accepted on 2026-08-21.

## Context

The Linux and native Windows bootstrap paths verify each checked seed before
building stage two. `SeedInputs` retains two views of that capture. `tools`
names executable files in the private capture directory, while
`artifact_bytes` holds the five verified tool images in memory.

The final report compares each seed image with its stage-two result. That
comparison reopened the paths in `SeedInputs.tools`. Those paths were
ephemeral, so removing the verified capture directory before report assembly
raised `FileNotFoundError`. The bytes needed for the comparison had already
been captured and verified in `SeedInputs.artifact_bytes`.

## Decision

Use `SeedInputs.artifact_bytes` as the lifetime-safe authority for the final
seed-to-stage-two comparison. Keep `SeedInputs.tools` as the executable path
view while the private capture directory exists.

Share one comparison helper between the Linux and native Windows bootstrap
paths. The helper indexes the retained bytes by tool name and compares them
with the stage-two tool images. Report assembly no longer reopens a seed path.

Keep manifest validation, artifact validation, live-input drift checks, stage
construction, behavior checks, and publication unchanged. The current
50-input source-head snapshot has SHA-256
`73b3fa6964292a7f0b753df3535058dd6399f5e6d8e277a082ac70ce65c79e43`.
This source lock does not replace a checked-seed snapshot.

## Evidence

The retained-seed regression was red in 0.024 seconds with
`FileNotFoundError` after the private capture directory was deleted. The
corrected Linux and Windows retained-byte cases pass two tests in 0.067
seconds.

The quick bootstrap group passes seven tests in 1.339 seconds. The two live
seed-drift cases pass in 2.792 seconds. `py_compile` and `git diff --check` are
clean.

The attempted complete 90-test bootstrap-seed run is incomplete evidence. It
encountered this lifecycle failure and a source-head lock mismatch while the
checked seed still named its promoted snapshot. No complete 90-test pass,
fixed-point proof, or seed reproof is attributed to this repair.

A later complete source-head run passed all 92 bootstrap tests in 2,820.626
seconds. This closes the module-level regression gap. It does not promote
either checked seed or change production adoption.

## Rejected alternatives

Extending the temporary directory lifetime only for report assembly was
rejected. It would keep the report coupled to an incidental filesystem
lifetime even though the verified byte capture already exists.

Reopening the live seed files was rejected. The report must describe the
verified input cohort, while live paths remain part of the separate drift
check.

## Limitations

The decision-time focused results did not establish a complete bootstrap-seed
module pass, fixed-point proof, or seed reproof. The later 92-test result closes
the module-level gap, but the source-head snapshot still differs from the
promoted checked-seed lock. The normal promotion and promoted-seed reproof gates
remain open.

## Ownership impact

Linux and native Windows report assembly now use the same retained-byte rule.
The repair changes no build owner, host dependency, report schema, output
format, or operating-system behavior.

This repair promotes no seed image, seed manifest, provenance record, source
lock, or artifact policy. No `.c` file qualifies for a rename from this change.
`TempleOS/` remains untouched reference material.
