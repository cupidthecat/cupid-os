# ADR 0377: Add typed Doom profile publication to CupidBuild

## Status

Accepted on 2026-08-30. Amended on 2026-08-31 after the host transaction
review.

## Context

The normal Doom compile profiles share one generated input manifest. It fixes
the three compatibility sources, the eighty Doom-tree sources, and every
visible `.h` and `.inc` file beneath the twenty include roots. That closed
inventory prevents a new source or input from entering a checked compile
unnoticed.

CupidObj already authors the canonical JSON from a bounded `CUPROF1` snapshot.
Python still discovers and freezes the closure, builds the snapshot, renders
an independent JSON copy, checks drift, owns the publication lock, and replaces
the manifest. This is the remaining Python-owned part of the profile-manifest
transaction.

The first source implementation also exposed several host-boundary gaps.
POSIX could not safely claim ownership of a directory created by `mkdirat`
before a later `openat`. Directory identity and mtime alone did not catch a
create-and-remove cycle whose visible mtime was restored. Windows child launch
inherited every inheritable handle, and flat POSIX transactions named more
temporary files than publication required.

## Decision

Add `cupidbuild generate-profile-manifest` with explicit seed-manifest,
repository-root, and output arguments. CupidBuild recursively discovers
visible `.h` and `.inc` files beneath `drivers`, `kernel`, and `toolchain`, then
selects the exact union used by the two Doom profiles. It separately discovers
every `.c` and `.cc` file beneath `kernel/doom` and requires the exact approved
83-source cohort. Missing entries, extra entries, links, junctions, unsafe
paths, duplicate identities, devices, and a legacy `.c` source stop the
transaction. A walk may retain at most 512 directories; the next directory is
a useful failure.

Freeze the source, input, and complete six-tool seed closure before rendering
anything. Build `CUPROF1` directly from the frozen input bytes, preserving the
profile names and accepted input and source order. Run the frozen promoted
CupidObj `profile-manifest` command first. CupidBuild then hashes the same
frozen inputs, renders the canonical JSON independently, and requires
byte-for-byte parity with CupidObj's candidate.

Keep every discovered directory handle or descriptor until publication ends.
POSIX snapshots contain device and inode identity plus nanosecond mtime and
ctime. Windows queries the exact named entry with
`FileIdFullDirectoryInformation`, samples the retained handle, repeats the
named query, and requires the two named records to match. The retained handle
must agree with the entry's nonzero file ID and LastWriteTime. ChangeTime is
copied into the retained snapshot and participates in every later equality
check. Reparse points and devices are rejected.

Validate the complete retained and public directory set twice at each closure
checkpoint. A change between those passes fails before publication or triggers
rollback after installation. The second successful pass is the checkpoint.
These tokens are observations rather than a filesystem journal, so a writer
can still change the tree after the final pass.

For the production `build/bootstrap/doom-cupidc-inputs.json` output, pin the
repository and fixed parent chain in a profile-only preparation. Windows may
create missing `build` and `build/bootstrap` components with parent-relative
`NtCreateFile`. Each successful create returns the directory handle in the
same operation. Rollback removes a created component only while the returned
identity still matches and the directory is empty. Existing directories are
never claimed.

POSIX requires both `build` and `build/bootstrap` to exist before the command
starts. A missing component fails before the publication transaction opens.
`mkdirat` does not return the new directory descriptor, and a later `openat`
cannot prove that a same-user process did not replace the public name between
the two calls. Refusing creation is the only supported policy that avoids
claiming a foreign directory for rollback. Other output paths retain their
existing-parent contract.

Use an exact Windows child handle allowlist. `STARTUPINFOEXA` carries one
`PROC_THREAD_ATTRIBUTE_HANDLE_LIST` containing standard input, standard
output, and standard error. Borrowed stream handles are marked inheritable for
the `CreateProcessA` window and cleared immediately afterward. An unrelated
inheritable handle stays in the parent. This temporary flag window assumes
CupidBuild launches children from one thread; the handle list is not a child
sandbox.

The source-current Windows profile has 33 exact `KERNEL32.dll` imports and
three exact `NTDLL.dll` imports: `NtCreateFile`, `NtQueryDirectoryFile`, and
`NtSetInformationFile`. The promoted parent keeps its earlier 29-plus-one
profile until the next paired seed refresh.

Reduce the POSIX private namespace. Flat transactions keep frozen inputs,
maps, captured streams, and other non-publication artifacts in sealed
anonymous memfds. Checked tools receive `/proc/self/fd/N` paths. The exclusive
reservation, adjacent owner lock, candidate, publication alias, and a parked
old output remain named because the publication protocol needs filesystem
entries on the target volume.

Use `renameat2(RENAME_NOREPLACE)` when the filesystem supports it. DrvFS rejects
rename flags, so the fallback creates the destination hard link first and then
unlinks the source. This preserves no-replace behavior for the regular files
handled by the transaction. When exchange rename is unavailable for an
existing output, create and verify a unique hard link to the old output before
installing the candidate with plain atomic rename. A failed post-install check
can restore that verified old binding directly over the target.

The POSIX fallback still has a formal same-user namespace limit. Its cleanup
and link-move helpers compare a name with an expected inode before a later
unlink, link, or rename. POSIX has no general unlink-by-open-file operation, so
an actor with the same permissions can replace the name in that gap. The
transaction minimizes named state and fails closed when it observes an
ambiguous binding, but it does not claim a hostile same-user namespace as a
fully isolated security boundary.

Classify post-install failures before cleanup. Source or input drift does not
by itself make the output namespace ambiguous. If CupidBuild can restore the
old output and candidate, then recheck the repository root, output parent,
output, lock, and candidate without repeating discovery, normal cleanup may
remove the known transaction state. If any of those public bindings is
ambiguous, set the namespace-interference result and close retained handles
without deleting uncertain names. The old output remains readable, while any
reservation, lock, candidate, parked-output, or cleanup names remain as
recovery evidence.

Equal candidate bytes leave the existing file and timestamp unchanged. A
malformed closure, tool failure, parity mismatch, alias, lock conflict, or
observed drift preserves the previous manifest. A Windows publication is
committed once the new candidate passes the final checks. Failure to dispose
the parked old handle after that point is reported as cleanup trouble and does
not roll back committed bytes.

Register the exact production output in both source-head fixed-point drivers.
Together with the checked CupidC runner, the Linux source matrix contains 31
failure, seven help, and 37 success groups. The native Windows matrix contains
19 failure, seven help, and 24 success groups. This record does not promote a
seed or change Make.

## Evidence

The existing public CupidBuild cases cover help text, exact parity with the
Python oracle, unchanged timestamps, live and replaced locks, unlisted Doom
sources, linked inputs, output-to-input hard-link aliases, mutated CupidObj
output, input drift, seed drift, output drift, and tool-before-oracle order.
Parent cases cover Windows clean-root creation and rollback, POSIX clean-root
and missing-bootstrap refusal, collisions at both levels, directory-link
rejection, replacement of either pre-existing component, and preservation of
foreign contents.

At the first source-capability checkpoint, twenty public CLI tests covered that
surface. The complete native Windows CupidBuild module passed 116 tests in
144.329 seconds. The three expected skips were the anonymous Linux runner, the
Linux build-plan contract, and the Linux hard-link recovery protocol. Four
focused WSL parent tests passed in 127.235 seconds, including clean-root
refusal, malformed-source behavior through pre-existing parents, foreign-file
preservation, and the platform split. The later instrumented pre-open
replacement pair passed in 253.826 seconds.

Hosted Clang built the changed CupidBuild image at that checkpoint. The
promoted CupidC seeds compiled `cupidbuild.cc`, `cupidbuild_host.cc`, and
`cupidbuild_main.cc` for native Windows and static i386 Linux. The focused
fixed-point structure checks passed, and the fail-closed mutation audit passed
its single case in 264.903 seconds.

The directory contracts accept exactly 512 retained directories and reject
513. Deterministic race seams create and remove a directory while restoring its
visible mtime, both before a final validation and between the first and second
passes. Focused WSL runs passed both races. Each preserved the previous output
bytes and mtime and left no transaction residue. Ordinary create and unchanged
profile publication also passed through WSL.

Native Windows tests cover a directory change between the first named query
and retained-handle sample. They also launch a child with an unrelated
inheritable sentinel handle. The child writes the allowlisted standard output
and standard error streams but cannot write through the sentinel. The
dedicated native Windows process module covered inherited-handle isolation,
the exact handle allowlist, frozen-input sharing, retained cleanup authority,
and a matching-file read through the retained discovery handle.

Strict native Clang and WSL syntax checks passed for the changed host adapter.
Python bytecode compilation and `git diff --check` passed at the same
checkpoint. The WSL post-install source-drift race also passed. It restored the
previous manifest and removed the candidate, publication alias, reservation,
owner lock, and parked-output state.

The first native profile candidate exposed the expected seed transition. The
promoted Windows tools predate the source-current output sharing and
caller-owned CupidASM protocol. A later same-generation v4 pair carried both
changes and converged on Linux and native Windows. Because that pair came from
a dirty working tree, clean commit-pinned reconstruction and seed publication
remain separate work. ADR 0381 records the handle and publication decision.

The success fixture still produces the same 72,950-byte canonical JSON as
`tools/cupidc_kernel_compile.py`. It contains the current 304 `.h` and `.inc`
inputs and all 83 Doom sources. The command runs the frozen CupidObj image
before the independent renderer and leaves sentinels intact for each exercised
failure.

## Rejected alternatives

Reusing Python's discovered inventory inside CupidBuild was rejected because
it would move the command name without moving discovery or drift ownership.

Rendering JSON before CupidObj was rejected. The checked Cupid tool remains
the author, and the independent native renderer is a veto after that author
runs.

Switching Make in the same change was rejected because the promoted Linux and
Windows seeds do not contain this command. Both seeds must reproduce the
expanded fixed-point behavior before a separate production handoff can remove
Python from this edge.

Creating POSIX parents with `mkdirat`, opening them afterward, and either
claiming or abandoning rollback ownership was rejected. Claiming ownership can
delete a replacement. Abandoning ownership leaves a public directory that the
transaction cannot prove it created. The supported POSIX contract therefore
requires the parent chain to exist.

Naming every POSIX frozen input, map, and captured stream was rejected. Those
files need stable bytes and descriptors, not public namespace entries. Sealed
memfds remove the cleanup race for that state without changing the tool's
source-level interface.

Treating every closure failure as namespace interference was rejected. A
source-only change can be rolled back and cleaned once the publication
bindings are verified. Residue is reserved for ambiguous namespace state.

## Consequences

Source-head CupidBuild can express the complete Doom profile-manifest
transaction on both checked targets. Windows can prepare a clean repository
root and roll back only its own empty parent directories. POSIX callers must
create `build/bootstrap` before invoking the production command.

The transaction now observes directory create-and-remove cycles through ctime
or ChangeTime and reduces POSIX named state with anonymous memfds. The last
successful pass is an observation checkpoint, not a durable snapshot or a
transaction-wide filesystem linearization point. The DrvFS and cleanup
fallbacks retain the documented same-user namespace limitation, and ambiguous
state is left visible for recovery instead of being deleted speculatively.

The normal graph remains at 195 CupidBuild and 257 Python participations
because Make still invokes
`tools/cupidc_kernel_compile.py --write-profile-input-manifest`. The next step
is a paired Linux and native Windows seed promotion that carries
`generate-profile-manifest`, the current import profile, and the retained
output share mode. Only after that proof may Make invoke the promoted command
and transfer production publication ownership.

No `.c` file is renamed. The command rejects a `.c` file in the active Doom
closure. No GCC, NASM, host linker, or host object utility is added.
`TempleOS/` remains read-only reference material.
