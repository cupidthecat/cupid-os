# Freeze the fixed-point source closure

- Status: Accepted
- Date: 2026-07-27

## Context

The checked i386 Linux bootstrap recorded hashes for its 40 active source
inputs before it started. It checked the live tree again after each stage and
after the behavior suite. CupidC still compiled from that live tree.

Those checks caught a lasting edit, but they did not isolate a build from an
edit that was made and restored while a compiler process was running. The
stage directories and behavior evidence were also written under the requested
output directory as work progressed. A late failure could leave results that
looked publishable even though the complete fixed-point gate had not passed.

## Decision

Capture the 40 source inputs once and copy those exact bytes into a private
compiler root. Preserve every logical path, use a mode-0700 root and mode-0600
source files, and reject symlinks, duplicate paths, paths outside the source
root, missing files, and nonregular files.

Run both stages from that private root. CupidC receives it through `--root`,
the tool runner uses it as the working directory, and the stage and behavior
outputs stay below it. A change to the live tree during a compile therefore
cannot change the bytes consumed by that stage.

Rehash both the private closure and the live closure before stage two, after
stage two, after stage three, and after the behavior suite. The report keeps
the existing schema. Its source inventory and digest describe the captured
bytes, while the seed manifest hash and historical seed revision remain
separate fields.

Keep the requested output absent or empty while the build runs. After every
comparison, behavior case, closure check, artifact inventory, and report write
has succeeded, move stage two, stage three, behavior evidence, and the report
into one complete bundle. Publish that bundle with one directory replacement.
Reject a nonempty output directory without changing it.

## Rejected alternatives

Continuing to compile from the live tree with more before-and-after hashes was
rejected. A file can change and return to its original bytes between those
checks.

Holding a repository-wide filesystem lock was rejected. It would make ordinary
edits and unrelated builds contend with a bootstrap that takes several
minutes, and it would still depend on every writer honoring the lock.

Writing public stage directories early and deleting them after a failure was
rejected. Cleanup cannot make incomplete evidence indistinguishable from
evidence that was never published.

Publishing the four result paths one at a time was rejected. A complete
directory replacement gives the output one visible publication boundary.

## Evidence

The focused source and publication tests cover a real checked CupidC compile
while the live source is changed and restored, mutation of the private
closure, symlinked source rejection, incomplete bundle rejection, preservation
of an occupied output, successful replacement of an empty output, and
restoration of that empty output when directory replacement fails. A forced
stage-three failure also leaves a completed first stage private. All 21 seed
tests that do not rebuild the full fixed point pass in 5.100 seconds.

The build audit pins the private compiler root, both private stage paths, the
four dual-closure checks, behavior execution below the private root, the
private report and bundle roots, and the complete-bundle publication call.
Mutation tests remove or redirect each edge and require the audit to fail.
All 62 build-audit tests pass in 639.964 seconds.

The complete checked-seed test rebuilds all 19 C objects, startup, and five
tools in both stages. It passes in 613.942 seconds with all 21 behavior cases.
The captured 40-input source digest remains
`230bffbf41d645e50b9944a179febd1d7920e1cfbc92b98e24a752d93192a7b8`.
The full repository suite, normal image build, and a GUI boot that lists the
updated `/docs/04CUPIDC.ctxt` payload also pass. Exact evidence is recorded in
the bootstrap log for this change.

## Consequences

A fixed-point stage now consumes one immutable captured source closure. The
live tree remains a consistency check, not a compiler input.

Failed compilation, comparison, behavior, closure, inventory, or report work
publishes no stage or evidence path. An existing nonempty output is preserved
and rejected before expensive work begins.

This decision does not change the 19-source build plan, checked seed, report
schema, target ABI, tool ownership, or OS executable behavior. The accompanying
CTXT updates change the documentation payload embedded in the normal image.
Windows still uses WSL to execute the static i386 Linux tools.
