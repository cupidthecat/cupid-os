# Retained original file-request binding groups

## Original file-request group manual acceptance, 2026-10-11

The [OS manual acceptance](FILE-REQUEST-GROUP-MANUAL.md) passes both complete
host builds and normal-root verification, including all six strict SMP boots.
The manual has 80,111 bytes and the raw kernel 9,309,356 bytes. Complete
native input and publication qualification remain open. Held component replay
keeps its recorded source, manual and policy checkpoint.

The held host API checks retained regular-file bindings against their original
absolute requests. Requests with the same original parent spelling share two
parent resolutions. Every member still checks the resolved physical parent,
captured parent identity and fresh no-follow file metadata before and after its
callback. This component also reuses directory metadata already captured from
the same resolution handle.

This is a metadata check. The caller must validate complete root and payload
custody before and after the group, as with the ordinary single-request
validator. Callback results remain provisional until the call succeeds. The
API creates no observations and grants no publication authority.

## Contract

`cupidbuild_host_file_request_t` carries the retained logical name, original
absolute request, expected physical absolute name and parent-validation mode.
`cupidbuild_host_observer_require_file_request_bindings` accepts an array and an
optional callback. Request storage, names and callback context must remain
stable throughout the call. The observer must remain alive, including through
any borrowing transaction's lifetime.

The implementation groups by the original parent bytes. Different alias
spellings remain separate even when they currently resolve to the same physical
directory. Each parent resolves before and after its group. A required member
also requires unchanged parent metadata across that group; identity-only
members retain their existing parent-identity contract. Every file opens without
following the final leaf and retains its before/after metadata checks.

Callbacks run in parent order and receive original array indices. Members of
one parent keep their original order. The implementation uses a
heapsort and a compact row array, with at most three additional handles during
validation. Count overflow rejects before array access. A live, valid observer
accepts a zero count with null requests. A null callback still performs checks.

Failure poisons the observer. Active group validation rejects complete observer
revalidation, observer closure, recursive groups, path or stream group reentry
and new transaction borrowing. Established borrowers keep their existing
lifetime and sticky-error rules. Ordinary single-request validation remains
available under its existing contract.

Directory metadata reuse uses the information already read from the exact
resolution handle. It removes immediate duplicate stat calls without replacing
fresh handle, parent, leaf, original-root or payload checks with cached identity.

## Evidence

Both complete repository replays pass all 3,228 preceding API calls and
210 new cases (72 positive and 138 negative), with 95 strict checks and
22 whole object pairs. Windows takes 1281.974 seconds; Linux takes 1544.067 seconds.
The common cohort has 32 core sources and five standalone fixtures. Its patch
maps 24 files; 30 replay helpers rebuild and execute both native and Cupid-built
callers. Four unchanged-copy controls pass; one hundred altered-evidence
controls reject. Complete patch extraction passes, and seven malformed-patch
or invalid-output preflights reject before compiler or runtime calls.

The new cases cover single, 512-member and 4,096-member groups, three original
parents, duplicates, repeated calls, zero counts, null callbacks, ordinary
single-file reads, new borrowing and existing borrower boundaries. Rejections
cover invalid storage, count overflow, missing or malformed names, unknown
retained files, bad parent modes, callback failure and reentry, current file
changes, parent metadata changes and parent or original-root alias retargeting.

Restored-time payload edits demonstrate the caller's responsibility. The bare
metadata group succeeds after a same-size edit with restored modification time;
the complete caller frame rejects the changed digest. Both single-file and
earlier-member cases retain this distinction. Every process keeps its original
60-second limit. Linux keeps 32 MiB of address space and 10,240 descriptors.
All publication sentinels retain their original bytes.

Independent review rereads complete objects, linked callers, command streams,
runtime streams, case plans, limits and mutation evidence. Portable archives
retain complete products and receipts. Native reference callers are checked
against their own completed builds; every Cupid-built object and program must
match the complete accepted baseline. Verifier controls preserve the thirty
preceding rejection controls and add twenty group-specific controls per host.

## Complete input owner remains unqualified

A separate private owner gathers the three required inputs and present stages
into per-observer request groups. It keeps copied options, both views, complete
root checks at both boundaries, per-file payload capture and the separate
missing-stage predicate. This adoption is excluded from the held packet.

Its uninstrumented 4,096-stage probe passes five of six callers. The native UNC
caller finishes in 52.867 seconds. The Cupid-built UNC caller reaches its final
ready marker at 56.502 seconds and times out at 60.243 seconds. Windows drive and
Linux callers pass. The complete owner therefore receives no normal recipe or
publication ownership.

Four 512-stage diagnostic calls produce identical query vectors. Relative to
the preceding metadata-reuse experiment, constructor directory stats fall from
4,735 to 3,709 and directory resolutions from 2,066 to 1,040. Per-file opens,
metadata checks, complete entry checks and full-validation calls stay unchanged.
All 515 original-request group members remain checked. The old
`original_file_request_checks` counter measures only entries to the ordinary
single-request function and excludes the group; its zero value does not mean
that original-request validation disappeared.

Temporary selection still performs 1,030 directory resolutions and 2,060
directory stats. Grouped physical capture remains unchanged. Instrumentation
removal restores the exact host and owner sources. These are private fixture
measurements, with no claim of complete-owner or release qualification.

## Preserved failures and corrections

The first Windows compatibility cohort times out on the ordinary native UNC
case for 4,096 nested missing paths at 60.331 seconds. Both preceding and current
callers later pass on reused and fresh equivalent trees in 28-30 seconds. The
measurements do not establish the cause of the timeout. The complete failed
cohort and all four comparisons remain archived.

The next fresh Windows cohort passes the complete root regressions but rejects
the existing Cupid-built ordinary-publication control. Its frozen writer path
has 260 UTF-16 units. Separate unchanged-source comparisons show both preceding
and current Cupid-built callers rejecting at 260 and accepting at 259; both
native callers accept at 260. The checked adapter's explicit long-path profile
remains separate under ADR 0410. A fresh shorter output directory avoids
crossing that inherited profile boundary while retaining identical source,
recipes, cases and resource limits. The failed cohort and all six comparisons
remain archived.

The first preservation recipe follows a Windows directory junction despite
`os.walk(..., followlinks=False)` and fails with `PermissionError`. The corrected
recipe explicitly prunes junctions and records fixture metadata separately from
whole products. The initial bookkeeping receipt called this attempt terminated
before its process result was retrieved; the correction records its actual exit
code and error. Neither attempt changes a compatibility source or runtime limit.

The first group fixture calls an unavailable private join helper and fails both
native builds before runtime. The corrected fixture uses its own bounded join.
A later Windows fixture tries to rename a held directory; the corrected case
retargets an original parent alias. Its first UNC form compares that alias's
drive-resolved physical parent with the main UNC observer's physical spelling
and correctly rejects before the callback. The final fixture resolves the alias
with a separate temporary selector and verifies the ordinary single-request
binding before the group. Earlier fixtures, binaries, receipts and streams remain
archived. The API implementation does not change during these corrections.

Candidate verifier review finds that omitted per-case Linux resource fields
could evade a generic optional-field check. The new group verifier requires the
exact Linux limits and null Windows process limits. The complete Linux group
cohort passes again, and nineteen private altered-evidence controls reject.

The first paired reviewer treats every intersecting non-host object as a
platform-independent pair. The runtime and startup objects retain their separate
platform bytes, so that assertion fails before a paired report or packet is
written. The corrected reviewer uses the preceding accepted 21-pair inventory
and adds the new group fixture, while checking that the three platform-specific
objects keep their existing membership. The packet assembler is also invoked
after that failed review and stops on the missing report before writing.

## Normal ownership

The 99 producer inputs, fifteen installed seed files and three Python
coordinators retain their accepted ownership. Normal ownership stays at
449 CupidBuild actions across 452 transforms. The component checkpoint keeps
the accepted 77,802-byte OS manual and 9,307,048-byte raw kernel. No normal
source, build recipe or publication owner is transferred by the held packet.

The [acceptance proof](evidence/native-file-request-group-20261011.json),
[complete originals](evidence/native-file-request-group-originals-20261011.json.gz),
[fresh replay archive](evidence/native-file-request-group-replays-20261011.json.gz)
and [investigation archive](evidence/native-file-request-group-investigation-20261011.json.gz)
retain every accepted product, runtime result and preserved correction.

Run the [replay](prototypes/replay-native-file-request-group.py) with a fresh
direct child of `build` as `--output`, then pass that folder as `--replay` to the
[independent verifier](prototypes/verify-native-file-request-group-replay.py).
The replay retains its original component checkpoint and source controls.
