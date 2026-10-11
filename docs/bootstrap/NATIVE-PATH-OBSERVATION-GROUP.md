# Retained path observation groups

## Source selection group follow-up, 2026-10-11

The held [source selection observation group](NATIVE-SOURCE-SELECTION-GROUP.md)
preserves this component and all 3,438 preceding API outcomes. Both complete
repository replays add 192 cases, pass 99 strict checks and preserve 23 whole
object pairs. Independent review and 152 rejection controls pass. Temporary
selection observers reject publication borrowing, and complete input custody
stays with the retained owner. Complete owner and publication acceptance remain
open; the private owner implementation is excluded from this held patch.

## Original file-request group manual acceptance, 2026-10-11

The [OS manual acceptance](FILE-REQUEST-GROUP-MANUAL.md) passes both complete
host builds and normal-root verification, including all six strict SMP boots.
The manual has 80,111 bytes and the raw kernel 9,309,356 bytes. Complete
native input and publication qualification remain open. Held component replay
keeps its recorded source, manual and policy checkpoint.

## Original-request group follow-up, 2026-10-11

The held [original file-request binding group](NATIVE-FILE-REQUEST-GROUP.md)
retains this component and all 3,228 preceding API outcomes. Both complete
repository replays add 210 cases, pass 95 strict checks and preserve 22 whole
object pairs. Independent review and one hundred rejection controls pass.
Complete root and payload validation remains outside the metadata group.
The separate complete owner still times out on the Cupid-built UNC caller;
normal image publication remains Python-owned.

## Path observation group manual acceptance, 2026-10-11

The current [OS manual acceptance](PATH-OBSERVATION-GROUP-MANUAL.md) passes both
complete host builds and normal-root verification, including all six strict
SMP boots. The manual has 77,802 bytes and the raw kernel 9,307,048 bytes.
Complete native input ownership remains unqualified on Windows UNC paths;
normal image publication still uses Python. Held component replay keeps its
recorded source, manual and policy checkpoint.

The held host API captures ordinary file, directory and first-absence observations
as one serialized group. Complete observer validation brackets capture, avoiding
a repeated full ancestor walk for every path. Each path retains root-metadata,
immediate-parent and fresh no-follow leaf checks. The complete native input owner
remains unqualified on Windows UNC paths, and normal image publication uses Python.

The [evidence](evidence/native-path-observation-group-20261011.json) binds the
combined [unapplied patch](prototypes/native-path-observation-group.patch),
36 logical source files and 28 replay helpers. The
[original archive](evidence/native-path-observation-group-originals-20261011.json.gz)
retains accepted source, whole products, complete receipts and command streams.
The [investigation archive](evidence/native-path-observation-group-investigation-20261011.json.gz)
preserves constructor query attribution, the unqualified complete-owner probe,
failed fixture assumptions and their corrections. The complete input-discovery
module is absent from the accepted patch. [ADR 0466](../adr/0466-bracket-retained-path-observation-with-complete-validation.md)
records the custody decision.

## Capture and lifetime

`cupidbuild_host_observer_paths_kind` takes one observer, an array of logical
names, a count, an optional callback and context, and an array of entry kinds.
All request names and callback storage must remain valid and stable throughout
the call. Callbacks receive the index, logical name and provisional kind.

The API validates every normalized UTF-8 name before the first callback. Complete
validation checks the observer's roots, retained ancestors, observations,
memberships and earlier payload digests before and after capture. Each path then
checks current root metadata, the immediate parent, freshly opened names without
following links, and retained entry metadata. Existing observations and first
absences remain subject to their original rules. This API uses ordinary path
observation semantics; image-stage missing selection keeps its separate contract.

Kinds and callback results remain provisional until the whole group succeeds.
Failure poisons the observer, clears every supplied kind and rejects a corrected
retry. Count overflow rejects before array access or output writes. Zero count
accepts null arrays while still validating complete custody. A null callback
performs capture without notification. The API has no additional fixed count cap;
the contract covers 4,096 distinct names.

Observer revalidation, closure, recursive groups, ordinary path capture and
retained streaming reject during a callback. Closure leaves the active observer
allocated for the outer call to finish. New transaction borrowing rejects
permanently during capture. An established borrower also rejects validation then
and keeps its transaction error; that error alone does not revoke its established
binding after capture. Calling borrow again invalidates that binding permanently.
The observer must outlive each borrowing transaction.

A callback may compare historical parent and leaf bindings through a separate
temporary selector and then close that selector. Temporary selection observers
retain their permanent publication-borrow restriction. Capture grants no
publication authority. The ordinary single-path entry point retains its full
ancestor checks.

## Paired component acceptance

The 28-case plan runs through native and Cupid-built callers on Windows drive
and UNC paths, and native Linux storage. It yields 168 cases: 54 positive and
114 negative. Positive cases cover 1, 512 and 4,096 distinct names, repeated and
zero-count capture, a null callback, mixed file/directory/absence observations,
and controls for rejected new borrowing and established borrower validation
during capture.

Negative cases cover failed callbacks, observer revalidation and closure,
recursive capture, path and streaming reentry, null arrays and names, overflow,
invalid UTF-8, traversal, trailing slashes, linked leaves and parents, quota
exhaustion, earlier-file metadata changes, original-root retargeting and an
earlier streamed digest changed while restoring its size and modification time.
Each negative case checks cleared results and permanent rejection on retry.
Overflow verifies untouched output storage before the corrected retry.

Complete compatibility preserves all 3,060 preceding API outcomes, 340 legacy
passes with twelve declared skips and all 48 alias methods. Independent review
rereads every whole product and complete runtime stream, including the original
case plans and resource bounds.

| Paired acceptance | Windows | Linux |
| --- | ---: | ---: |
| New path-group cases | 112 | 56 |
| Complete API outcomes | 1,760 | 1,468 |
| Whole checked objects | 30 | 25 |
| Whole checked programs | 18 | 18 |
| Strict CupidDis checks | 48 | 43 |

Twenty-one whole object pairs agree across hosts. Strict checks retain
`--require-known`, `--require-local-targets` and `--require-code-anchors`.
Runtime processes keep sixty seconds. Linux keeps 32 MiB of address space and
10,240 descriptors. Checked builds forbid conventional producers; Clang remains
the separately recorded native comparison. Both complete repository replays
pass, taking 1171.223 seconds on Windows and 1793.882 seconds on Linux.

The [replay archive](evidence/native-path-observation-group-replays-20261011.json.gz)
retains complete fresh receipts, independent whole-product review and verifier
controls. Four unchanged-copy controls pass; sixty altered-product, stream,
case, deadline, limit, publication and mutation controls reject. Complete
extraction of all 23 mapped patch files passes. Seven malformed-patch and
invalid-output preflights reject before compiler or runtime calls.

## Constructor diagnosis and remaining owner work

Debug-only counters measured the preceding constructor with 512 selected inputs
in a 4,096-file tree. Temporary selection performs 3,090 directory stats and no
directory child opens. Primary physical capture performs 9,779 directory stats
and 4,632 directory child opens. That is about 70 percent of constructor directory
stats and 99 percent of its directory child opens. All four Windows caller and
namespace combinations agree. These counters are absent from the accepted source.

A separate private owner defers ordinary physical capture to this group while
preserving temporary selections, historical parent/leaf comparisons, copied
requests and options, original-request validation, both borrowed views and
retained payload capture. Each callback closes its separate temporary selector
after comparison, avoiding a second complete live handle set. All remaining
selectors close on failure. Missing image stages retain their original predicate.

The six-call 4,096-stage owner probe passes both Linux and Windows drive callers.
The native UNC caller reaches construction at 27.694 seconds, its first view at
34.190 seconds and payload capture at 52.932 seconds, then times out. The checked
UNC caller reaches those phases at 32.791, 40.802 and 59.753 seconds, then times
out. Each process retains sixty seconds. Its required files are fixtures rather
than a qualified release manifest. The four successes and two timeouts establish
diagnostic progress; complete discovery and normal publication still need acceptance.

The 99 normal producer inputs, fifteen installed seed files and three Python
coordinators retain their accepted ownership. No production source or seed is
promoted by this held component.

A [fresh query diagnosis](evidence/native-grouped-input-query-20261011.json)
measures the grouped constructor with 512 selected inputs in the same 4,096-file
tree. Constructor directory stats fall from 13,971 to 5,771, and directory child
opens from 4,666 to 563. Temporary selection accounts for 3,090 directory stats;
grouped capture uses 1,579 and final original-request validation uses 1,072.
The four Windows callers retain copied options, both views and 6,144 streamed
payload bytes, and produce identical query vectors. Their complete diagnostic
source, inverse instrumentation checks, products and streams remain in the
[query archive](evidence/native-grouped-input-query-originals-20261011.json.gz).
The 512-input measurement establishes no 4,096-input capacity claim.

## Preserved corrections

The first fresh Windows replay times out on the existing native UNC case for
4,096 nested missing paths at 60.311 seconds. Its unchanged caller and tree then
produce both a 44.320-second pass and another sixty-second timeout; the checked
caller passes at 39.217 seconds. The preceding native caller passes on that tree
at 35.812 seconds. Both preceding and current native callers pass on a fresh
equivalent tree at about 34 seconds. These measurements do not establish a
specific host-latency cause or a native-code regression. The final Windows replay
uses a fresh output with unchanged cases, source and resource bounds.
The [failed replay archive](evidence/native-path-observation-group-first-replay-20261011.json.gz)
retains the failure, whole callers, complete output and all six diagnostic calls.

The first Linux verifier-control wrapper stops after 27 commands at its
180-second allowance. The preserved partial attempt receives no acceptance
claim. A fresh copy completes all thirty rejection controls and two passing
controls with a 360-second allowance for review bookkeeping. Every native
runtime contract keeps its original sixty-second deadline. The replay archive
includes the original wrapper, correction record and partial control output.

The first standalone fixture opened its publication transaction after capturing
strict root metadata. Creating transaction storage changed that metadata and
invalidated the fixture setup. The corrected fixture establishes transaction
storage before capture. A later fixture incorrectly treated a sticky transaction
error as permanent loss of an existing binding. The corrected case preserves
the established borrow contract and separately checks permanent rejection after
an invalid new borrow. Both failed attempts and whole corrected receipts remain
in the investigation archive.

Independent compatibility review initially expected supplemental source entries
to contain identity objects. The preceding archive stores those entries as plain
source text. The corrected reviewer checks the full UTF-8 bytes against that text
and the mapped patch identity. The first reviewer and failure are preserved.

## Reproduce the held component

Use the source and manual checkpoint `cf52cd12` recorded in the evidence. Choose
a fresh direct child of `build/` for each host:

```powershell
C:\Python314\python.exe docs/bootstrap/prototypes/replay-native-path-observation-group.py --output build/path-group-replay-windows
C:\Python314\python.exe docs/bootstrap/prototypes/verify-native-path-observation-group-replay.py --replay build/path-group-replay-windows
```

```sh
python3 docs/bootstrap/prototypes/replay-native-path-observation-group.py --output build/path-group-replay-linux
python3 docs/bootstrap/prototypes/verify-native-path-observation-group-replay.py --replay build/path-group-replay-linux
```

Replay extracts the complete mapped patch, adds the retained option prerequisite
and unchanged legacy fixtures, freezes installed seeds and rebuilds every family.
The verifier rereads whole products, complete outputs, deadlines, resource bounds,
mutation evidence and the original case plan. The recorded checkpoint is needed
after later OS manual or artifact-policy changes.
