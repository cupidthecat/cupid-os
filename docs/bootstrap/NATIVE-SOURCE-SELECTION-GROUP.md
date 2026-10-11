# Temporary source selection observation groups

## Source selection group manual acceptance, 2026-10-11

The [OS manual acceptance](SOURCE-SELECTION-GROUP-MANUAL.md) passes both complete
host builds and normal-root verification, including all six strict SMP boots.
The manual has 82,330 bytes and the raw kernel 9,311,576 bytes. Complete
native input and publication qualification remain open. Held component replay
keeps its recorded source, manual and policy checkpoint.

The held host API captures several source path kinds through one temporary
selection observer. It requires the selection-only profile and validates that
observer before and after capture. Source-root checks use identity, so an
uncaptured sibling or root metadata change follows the existing optional
selection contract. Captured descendants, parents and leaves retain their
existing checks. The ordinary path observation group keeps full root metadata.

Temporary selection grants no publication borrowing authority. Complete input
owners retain their separate ancestor, original-request and payload custody.
The complete owner implementation is excluded from this component's patch.

## Contract

`cupidbuild_host_observer_source_selection_paths_kind` accepts the logical
names, count, optional callback and result array used by the ordinary path
observation group. Storage and names must remain stable throughout the call.
Callbacks receive original array indices and path kinds. Results remain
provisional until the group succeeds.

The API rejects complete retained observers. A zero count accepts null name
and result arrays on a live selection observer; a null callback still captures
and validates paths. Count overflow, invalid storage and malformed names reject
before callbacks. Failure clears result kinds and poisons the observer.

Active capture rejects observer closure, complete revalidation, recursive groups,
ordinary path or stream reentry and transaction borrowing. The selection
observer keeps its existing permanent borrowing rejection after capture.
Repeated calls retain older observations, so an earlier changed file still
rejects later validation. Previously captured payloads keep digest validation.

## Evidence

Both complete repository replays pass all 3,438 preceding API calls and
192 new cases (66 positive and 126 negative), with 99 strict checks and
23 whole object pairs. Windows takes 1576.766 seconds; Linux takes 2004.560 seconds.
The common cohort has 32 core sources and six standalone fixtures. Its patch
maps 25 files; 32 helpers rebuild and execute native and Cupid-built callers.
Four unchanged-copy controls pass; 152 altered-evidence controls reject.
Complete patch extraction passes, and eleven malformed-patch or invalid-output
preflights reject before compiler or runtime calls.

The 32 case plans cover one, 512 and 4,096 paths, repeated calls, zero counts,
null callbacks, mixed file kinds and borrowing rejection. Negative cases cover
callback failure, closure and reentry, invalid arrays, overflow, malformed
UTF-8, traversal, trailing slashes, linked leaves and parents, quotas, changed
earlier files, retargeted original roots and same-size payload edits with
restored modification times. Separate controls permit source-root metadata
and uncaptured sibling changes while rejecting root metadata changes through
the ordinary group API. Complete retained observers cannot use the new API.

Every process keeps its 60-second limit. Linux keeps 32 MiB of address space
and 10,240 descriptors. All publication sentinels retain their original bytes.
Independent review rereads complete objects, linked callers, command streams,
runtime streams, case plans, resource limits and mutation evidence. Native
reference callers match their own completed builds; Cupid-built products match
the whole accepted baseline.

## Private complete-owner experiments

The investigation archive preserves a separate owner that groups present stage
selection by original parent spelling, retains the missing-stage predicate,
copies options and keeps both complete views. All six flat 4,096-stage probes
pass, with 49,152 payload bytes per fixture and full per-file digest streaming.
Its 236 lifetime processes pass: 158 preceding drive and POSIX cases plus
78 UNC cases. One hundred processes succeed and 136 reject as expected.
These measurements do not establish complete owner or publication acceptance.

A fixture correction changes two UNC expected root counts from zero to one.
The selected physical drive name belongs to a distinct root from the original
UNC spelling. Preceding and current implementations produce the same result;
the complete failed run, comparisons and unchanged-source retry remain
preserved. The original C fixture is unchanged. A review wrapper's initial
Windows interpretation of Linux `/mnt/c/` product paths also remains preserved;
mapping those paths to the same local products fixes the review.

The 4,096-distinct-parent Linux probe initially rejects both callers before
construction completes. Descriptor-only diagnostic runs pass with 32,768
descriptors; memory-only runs still reject at 10,240. A separate private owner
closes each temporary selection observer after its last physical capture.
That owner passes both many-parent and both flat Linux callers under the
original 32 MiB and 10,240-descriptor bounds, plus all 236 lifetime processes.
One native Windows flat UNC process times out after the viewed marker; the
other three Windows flat calls pass. The archived progress snapshot leaves
Windows many-parent runtime checks pending and the timeout's cause unproven.
This owner change is excluded from the held component patch.

## Preserved component failures and corrections

The first Windows compatibility run times out on the unchanged native UNC
case for 4,096 nested missing paths at 60.288 seconds. Both preceding and
current binaries later pass that exact captured case twice in 35-38 seconds
under its original limit. The cause of the timeout remains unproven. The full
failed cohort and all four comparisons are archived; an unchanged fresh retry
passes the complete component suite.

A candidate verifier initially accepts an altered embedded stderr field while
the separate stream and its hash remain unchanged. The correction compares the
embedded bytes with the complete captured stream and preceding result. Its
reproducer, initial verifier and passing rejection control remain archived.
Private checks accept two complete case sets and reject 42 altered receipts.
The public verifier retains all fifty preceding rejection controls per host
and adds 26 source-selection controls.

Fresh replay folders keep the existing ordinary Windows writer profile within
its pathname bound. The prepared drive writer has 231 UTF-16 units and the UNC
writer 241, both within the previously measured 259-unit limit. This changes
the output location while preserving sources, cases and runtime bounds.

## Ownership and replay

The 99 producer inputs and fifteen installed seed files retain their bytes.
Normal ownership remains 449 CupidBuild actions and three Python coordinators
across 452 transforms. This component keeps checkpoint `f64b6382`, its
80,111-byte manual, 9,309,356-byte raw kernel and exact artifact policy.
Complete input discovery and publication ownership remain open.

The [acceptance proof](evidence/native-source-selection-group-20261011.json),
[complete originals](evidence/native-source-selection-group-originals-20261011.json.gz),
[fresh replay archive](evidence/native-source-selection-group-replays-20261011.json.gz)
and [investigation archive](evidence/native-source-selection-group-investigation-20261011.json.gz)
retain the accepted products, runtime results and preserved corrections.

Run the [replay](prototypes/replay-native-source-selection-group.py) with a
fresh direct child of `build` as `--output`, then pass that folder as `--replay`
to the [independent verifier](prototypes/verify-native-source-selection-group-replay.py).
Use a short output folder to retain the ordinary Windows fixture's name
profile. The replay checks its recorded component checkpoint and source inputs.
