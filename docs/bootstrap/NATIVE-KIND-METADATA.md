# Initial leaf kind and metadata reuse

The source observer takes an unknown leaf's kind and first metadata snapshot
from one query of its newly opened handle. It uses that snapshot when adding
the leaf to retained observations. This removes a second query of the same
handle from ordinary walking and grouped path-kind capture.

Known-type checks keep their existing rules. An unknown kind accepts only a
regular file or directory; unsupported kinds reject before retention. Failed
queries clear the metadata and kind outputs. Later fresh path-binding checks,
root and parent validation, payload hashing and provisional callback rules
remain in force. The public header and API signatures retain their accepted
bytes.

## Complete component acceptance

Both complete repository replays pass all 3,630 existing API outcomes: 2,028
on Windows and 1,602 on Linux. This refactor adds no public API or new case
plan. The existing 192 selection-group cases remain part of the cohort.
Independent review rereads complete case plans, generated objects and
programs, runtime streams and resource bounds. All 99 strict checks and
23 whole portable object pairs pass. Windows takes 1037.269 seconds; Linux
takes 1590.953 seconds. Four unchanged-copy controls pass;
152 altered evidence controls reject. Every process keeps its sixty-second limit; Linux
keeps 32 MiB of address space and 10,240 descriptors.

The patch reconstructs all 25 mapped sources. One complete-patch
control passes and eleven malformed-patch or invalid-output controls reject
before any compiler or runtime command. The private verifier passes two
unchanged-receipt controls and rejects 42 altered-evidence controls against
previously accepted Linux receipts. These checks test verifier logic; they do
not substitute for new generated-code acceptance.

## Query diagnosis

Six unchanged native-reference calls preserve the complete 512-stage fixture,
both views and per-file payload checks. Each namespace comparison records
1,030 fewer metadata helper reads during construction. That difference stays
constant through both views; later validation increments keep their preceding
values. The Linux diagnostic uses its tree under `/mnt/c`.

A separate four-call Windows diagnosis instruments seventeen direct API and
UTF-8 adapter counters. Drive and UNC comparisons record 1,030 fewer
`GetFileInformationByHandle` calls at every phase. The other sixteen counters
retain their values, including opens, closes, reads and seeks. Each completed
512-stage caller makes 10,908 `NtCreateFile` calls, 2,048 `ReadFile` calls and
2,048 `SetFilePointer` calls. The counters cover the host module and UTF-8
adapter; CRT internals, fixture APIs, other modules and OS-internal work remain
outside that measurement. Elapsed times do not establish a capacity speedup.

## Complete owner boundary

The private owner using this host refactor passes 236 lifetime processes, all
192 existing selection-group cases and four Linux capacity calls under the
original 32 MiB and 10,240-descriptor bounds. Four Windows drive capacity calls
pass. All four Windows UNC capacity calls reach their unchanged sixty-second
deadline, across flat and many-parent fixtures and native and Cupid-built
callers. Their raw streams, partial progress and complete products are archived.
The cause remains unproven.

That owner includes a separate temporary-observer release change. The held
component source cohort excludes the complete owner and retains all other
accepted core modules. Normal image publication remains Python-owned.

## Accepted root and ownership

The preceding manual acceptance is `53041192`: the manual has 82,330 bytes and
the raw kernel has 9,311,576 bytes. The new cohort captures that complete
committed root projection. The 99 producer inputs and fifteen installed seed
files retain their accepted bytes. Normal ownership remains 449 CupidBuild
actions and three Python coordinators across 452 transforms. TempleOS remains
read-only reference material and is excluded from builds and progress counts.

## Preserved preparation failures

The first source preparer expected a `pass` status from an explicitly
unqualified private-owner archive. Its next version tried to compare a complete
owner module absent from the accepted source cohort. Both stopped before
creating the target or running compiler or runtime commands. The corrected
preparer checks the archived status and excludes all complete-owner modules.

The first public preparer required the private 24-source patch helper to equal
the published 25-source helper. The corrected preparation checks their one-line
inventory difference and preserves the partial first candidate. The first patch
preflight also assumed the selection fixture was the final patch entry. The
corrected preflight selects its exact header and removes only that file's
hunks. Both initial recipes and their partial files remain preserved.

The first Linux compatibility coordinator completed its build, then lost test
fixture directories while its aggregate cohort was running. Child receipts
preserve a failed parent-alias identity check and a missing-directory exception.
The kernel log records WSL shutdown and restart. No cohort process remained,
and neither the coordinator nor aggregate wrote a closed receipt. The first
rejection's cause remains unproven. The interrupted products, streams and
partial receipts are archived. A fresh Linux folder passes the full cohort
with identical source, case plans and limits after the environment interruption.

## Reproduce the component

The [acceptance proof](evidence/native-kind-metadata-20261011.json),
[complete originals](evidence/native-kind-metadata-originals-20261011.json.gz),
[fresh replay archive](evidence/native-kind-metadata-replays-20261011.json.gz)
and [investigation archive](evidence/native-kind-metadata-investigation-20261011.json.gz)
preserve generated products, runtime results, diagnostics and interrupted runs.

Run the [replay](prototypes/replay-native-kind-metadata.py) with a fresh direct
child of `build` as `--output`, then pass that folder as `--replay` to the
[independent verifier](prototypes/verify-native-kind-metadata-replay.py).
Use a short folder name to retain the ordinary Windows writer fixture's
existing pathname bound. The replay checks its recorded component checkpoint
and complete source inputs.
