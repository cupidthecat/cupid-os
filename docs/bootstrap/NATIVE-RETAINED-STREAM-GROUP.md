# Retained file stream groups

The held host API captures already retained regular files as one serialized
group. Complete observer validation brackets the group; each member keeps its
immediate parent, no-follow leaf, metadata and payload checks. This avoids
repeating the full retained ancestor walk between every file. The normal image
command continues to use Python, and complete native input ownership remains
unqualified on Windows.

The [evidence](evidence/native-retained-stream-group-20261011.json) binds the
combined [unapplied patch](prototypes/native-retained-stream-group.patch),
35 logical source files and 26 replay helpers. The
[original archive](evidence/native-retained-stream-group-originals-20261011.json.gz)
retains the accepted source, complete receipts and command streams. The
[replay archive](evidence/native-retained-stream-group-replays-20261011.json.gz)
retains both fresh repository replays, independent whole-product review and
verifier controls. The
[investigation archive](evidence/native-retained-stream-group-investigation-20261011.json.gz)
preserves the query-cost diagnosis, complete-owner probe and harness corrections.
The complete input-discovery module is absent from the accepted patch.

## Capture and lifetime

`cupidbuild_host_observer_files_stream_retained` takes an observer, an array of
`cupidbuild_host_retained_stream_request_t`, a count and a result array. Each
request supplies a retained logical file name, a per-file byte limit, a sink and
its context. Requests, results and callback storage must stay valid and stable
throughout the call. A callback must not reenter or close the observer.

Every request must name a previously retained regular file. The group creates
no observations. Complete validation runs before the first callback and after
the final capture, checking the observer's retained roots, ancestors, entries,
memberships and earlier payload digests. Temporary selection observers retain
their existing custody limits and permanent publication-borrow restriction.

Each member checks its retained parent and leaf metadata, opens a fresh leaf
without following links, and checks metadata around streaming. An existing
digest must still agree. It then checks the held leaf, the freshly named leaf
and the immediate parent before replacing the original handle and recording
the digest. The original single-file streaming body and its ancestor checks
remain unchanged.

Copied callback bytes are provisional until the whole group succeeds. Failure
poisons the observer and clears every supplied result; callers must discard
those bytes. A later retry remains rejected. An overflowing count rejects
before array access or output writes. Zero count accepts null arrays and still
validates the observer. The API adds no fixed count cap; qualification includes
4,096 distinct retained inputs.

Observer revalidation, closure and recursive group capture reject during a
callback. Closure leaves the active observer allocated for the outer call to
finish. New transaction borrowing rejects permanently while capture is active.
An existing borrower also rejects validation during capture and keeps its
transaction error. The observer must outlive every borrowing transaction.
Successful payload capture grants no publication authority.

## Original-request metadata reuse

The private directory resolver can return the metadata it has already checked
for its successful final handle. Original-request binding uses that result for
each of its two parent resolutions, removing two immediately repeated directory
stat calls. The existing resolver wrapper remains available to other callers.
Original parent-name, identity, metadata, regular-leaf and fresh-binding checks
remain in force. All preceding binding and observer contracts are replayed
against this source cohort.

## Accepted contracts

The 24-case plan runs through native and Cupid-built callers on Windows drive
and UNC names, and on native Linux storage. It yields 144 new cases: 54 positive
and 90 negative. Positive cases include 1, 512 and 4,096 distinct inputs, repeated
capture, zero-count validation, empty files and multi-chunk streaming. The large
case streams 49,152 bytes in total, with a separate 64 MiB upper bound per file.
The chunked fixture streams 131,083 bytes per file in three callbacks.

Negative cases cover unretained files, directories, null arrays and names,
count overflow, byte limits, failed sinks, revalidation, closure, recursive
capture and previous-digest drift. A final callback mutates an earlier file
while restoring its size and modification time; final group validation still
rejects. Original-root retargeting rejects. Every negative case also verifies
cleared results and permanent rejection on retry.

Transaction controls establish a successful borrow and validation before the
capture callback tries that boundary again. UNC input observers use an explicit
drive spelling of the same physical root for the publication transaction. The
existing ordinary UNC publication preflight rejects the server-only prefix;
this component preserves that rule and retains all UNC input cases.

Both complete repository replays preserve all 2,916 preceding API outcomes,
340 legacy passes with twelve declared skips and all 48 alias methods.

| Acceptance | Windows | Linux |
| --- | ---: | ---: |
| New group cases | 96 | 48 |
| Complete API outcomes | 1,648 | 1,412 |
| Whole checked objects | 29 | 24 |
| Whole checked programs | 17 | 17 |
| Strict CupidDis checks | 46 | 41 |

Twenty whole object pairs agree across hosts. All strict checks retain
`--require-known`, `--require-local-targets` and `--require-code-anchors`.
Independent review rereads every whole product, complete runtime stream,
original case and resource bound. Four unchanged-copy controls pass; forty
altered-product, stream, case, deadline, limit, publication and mutation controls
reject. Complete extraction of all 22 mapped patch files passes. Seven
malformed-patch and invalid-output preflights reject before compiler or runtime
calls.

The fresh Windows replay takes 1203.051 seconds; Linux takes
1435.608 seconds. Every runtime process keeps sixty seconds. Linux keeps
32 MiB of address space and 10,240 descriptors. Checked builds forbid conventional
producers. Clang remains the separately recorded native comparison.

## Query-cost and complete-owner evidence

Debug-only counters measured the preceding single-file retained-stream path
with 512 selected inputs in a 4,096-file tree. Each file performed 38 directory
stats, eighteen directory child opens, 22 retained ancestor steps, two binding
checks, six file stats and three file opens. All four Windows caller/namespace
combinations produced the same vector. The counters are absent from the
accepted source. Those timings diagnose duplicated work; they do not establish
complete input-owner capacity.

A fresh six-call complete-owner probe uses group capture for 4,096 distinct flat
present stages. Both Linux callers and both Windows drive callers pass. The
native UNC caller times out after construction at 40.266 seconds and its first
view at 48.729 seconds. The checked UNC caller reaches those phases at 43.408
and 54.721 seconds, then times out. Both retain the original sixty-second bound.
This probe preserves copied options, source and destination ownership, but uses
fixture required files rather than a qualified release manifest. Its four
successes and two timeouts remain diagnostic evidence. Complete discovery,
physical-root ownership and normal publication still need acceptance.

## Preserved corrections

The first transaction fixture sealed discovery without captured directories
and failed during setup. A corrected fixture removed that invalid step. Ordinary
UNC transaction setup then rejected before the callback, leading to the explicit
physical publication-root argument described above. Neither correction changes
the group API or weakens UNC input coverage.

Review found that the first thin fixture closed its observer before its borrowing
transaction. The corrected fixture closes the transaction first and still
attempts both cleanups. All 144 final cases use that corrected fixture; earlier
receipts remain archived. The first Linux compatibility coordinator tried to
execute Windows PowerShell from WSL and failed before several case families.
The complete fresh Linux run uses the Bash Git human helper with translated
worktree paths. Case bodies and runtime bounds stay unchanged.

The first public replay referenced the preceding packet's original archive and
stopped in preflight on both hosts, before compilation or runtime tests. The
corrected runner uses this packet's archive. Original failed receipts and source
reconstruction records remain in the investigation; missing earlier private
receipts are not presented as recovered evidence.

## Replay and normal ownership

The later [OS manual acceptance](RETAINED-STREAM-GROUP-MANUAL.md) changes the
embedded manual and exact policy after complete paired and root verification.
Replay this held packet at `c2eabb95a088f938a66ec779fc4e94b1ce04844d`, using its original
manual and policy. Run both commands from that checkpoint with a fresh direct
child of `build`:

```powershell
python docs/bootstrap/prototypes/replay-native-retained-stream-group.py --output build/retained-stream-group-review
python docs/bootstrap/prototypes/verify-native-retained-stream-group-replay.py --replay build/retained-stream-group-review
```

```sh
python3 docs/bootstrap/prototypes/replay-native-retained-stream-group.py --output build/retained-stream-group-review
python3 docs/bootstrap/prototypes/verify-native-retained-stream-group-replay.py --replay build/retained-stream-group-review
```

Use different destinations when both hosts share a worktree. Apply this combined
22-file patch directly; older prerequisite patches must not be applied first.
Four existing supplements and nine unchanged option-owner sources complete its
35 logical sources. The replay guards the `9c4105c6` control projection,
including twenty root files, the 74,594-byte manual, 99 normal producer inputs
and fifteen installed seed files.

Normal ownership remains 449 CupidBuild actions and three Python coordinators
across 452 transforms. No active `.c` file is renamed by this held component.
TempleOS stays read-only and excluded from builds and metrics.
