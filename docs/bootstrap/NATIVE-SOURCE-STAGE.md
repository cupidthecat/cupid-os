# Retained image-stage requests

The held image-stage owner copies a complete original request and preserves
the normal image command's missing-path rules. Present regular files retain
the optional source-file owner's observations. A missing stage retains its
requested name without an observer or physical-root claim. The normal image
command still uses Python; complete discovery and publication remain open.

The [evidence](evidence/native-source-stage-20261010.json) binds the combined
[unapplied patch](prototypes/native-source-stage.patch), all original calls
and both repository replays. The [original archive](evidence/native-source-stage-originals-20261010.json.gz)
keeps complete sources, scripts, receipts, Python decisions and failed
approaches. The [replay archive](evidence/native-source-stage-replays-20261010.json.gz)
keeps every fresh receipt, command log and the independent verifier.

## Selection and borrowed lifetime

`cupidbuild_source_stage_open_quota` normalizes and copies the request before
caller strings can be released. A present regular file uses the existing
optional owner, including its parent binding and retained observations.
Directories, linked or special leaves, lookup errors, invalid UTF-8, paths
beyond 8,191 bytes, zero and `SIZE_MAX` quotas reject. A race from present
selection to a missing optional view also rejects construction.

`cupidbuild_host_source_missing` performs a read-only full-path absence
query. Windows accepts file/path-not-found results; Linux accepts `ENOENT`
from its no-follow leaf lookup. Other errors and every present leaf reject.
Parent aliases follow the host's existing lookup rules. This query captures
no observation and grants no source authority.

A missing view has `NONE` and only `file.requested_absolute`. Its other file
fields are null. A present view has `FILE` and the complete borrowed file
view. Each view revalidates before returning storage. Failure clears the
supplied result and poisons the stage owner permanently, including a null
result argument. Returning the path to its earlier state cannot repair it.

The caller must revalidate before consuming a stage and after use. Keep the
owner alive through borrowed views and transactions using a present-file
observer. Closing the owner releases storage; close is not a final validation
boundary. Metadata alone accepts restored-time edits. Retained streaming
adds the existing payload digest checks.

## Missing-path compatibility

For `new/subtree/payload.bin`, absent parents may appear or change while the
complete requested leaf stays absent. Appearance of that leaf rejects.
Children of broken parent aliases remain missing on both hosts. Children of
regular-file parents remain missing on Windows and reject on Linux.
Linked leaves reject even when their targets are missing.

These rules match the actual selection loop and final missing-stage predicate
extracted from the normal Python command. The existing optional file owner
continues to retain its first absence. Its original contracts and the earlier
[twenty-call diagnostic](evidence/native-source-missing-revalidation-20261010.json)
remain unchanged. The separate stage API supplies the image-specific behavior.

## Complete acceptance

Four native/Cupid callers pass 312 new API calls: 150 successes and 162 useful
rejections. The basic fixtures execute 276 calls and 280 actual Python oracles.
Capacity adds 36 calls, each retaining all 4,096 distinct missing requests.
Every complete request and final decision remains in the archive: 147,456
Python selection calls and 147,456 publication-predicate calls.

| Host | Basic calls | Distinct-capacity calls | Complete current API calls | Replay seconds |
| --- | ---: | ---: | ---: | ---: |
| Windows | 136 | 24 | 980 | 1147.320 |
| Linux | 140 | 12 | 1,074 | 2897.097 |

Cases cover copied caller strings, cleared failure results, sticky rejection,
appearing ancestors and leaves, broken/file parents, ordinary UNC paths,
Unicode and long parents, and literal POSIX colon/backslash names. Windows
metadata handles permit deletion; retained stream handles block deletion with
WinError 32. Both behaviors have explicit unchanged-payload controls.

Every new C runtime process keeps sixty seconds. Linux keeps 32 MiB of address
space and 10,240 descriptors. Capacity uses distinct flat and nested paths,
including all-parent appearance and a last-leaf rejection. It establishes
missing-request capacity only; present-stage sharing remains unimplemented.

The unchanged required and optional callers pass all 232 and 382 calls. The
preceding source/root/observer fixtures pass all 1,128 calls. Both fresh replays
preserve all 2,054 current API outcomes, 340 legacy passes with twelve declared
skips, and all 48 alias methods. Independent review rereads 24 whole checked
objects on Windows and nineteen on Linux, twelve complete checked programs
per host, every source control and command stream. 15 whole object
pairs agree across hosts. All 69 strict checks retain `--require-known`,
`--require-local-targets` and `--require-code-anchors`.

## Preserved failures and review corrections

The first Linux builder cannot use a Windows worktree's `.git` pointer through
ordinary Linux Git. It fails before compiling. The correction translates the
worktree Git directory using the existing consumer-audit convention.

The first two Windows fixture runs apply the wrong file-sharing expectations
to deletion. The accepted fixture distinguishes metadata deletion from stream
deletion and retains both WinError 32 controls. Implementation and runtime
bounds stay unchanged.

The first Windows aggregate reads the wrong missing-UNC receipt generation
after all seven child commands pass. Its continuation independently checks the
actual second-generation receipt and every original command stream, without
repeating those completed tests. The first read-only product checker also
constructs a capacity filename with a doubled dot. The corrected filename
retains all review predicates.

The first independent summary counts 65 strict checks and omits `alias.o`
from its object map. Supplemental review rereads the alias object, both
programs and every alias command stream on both original hosts and both
replays. The corrected total is 69. The first archive and receipts retain
their original bytes. The separate repository verifier repeats that review.

## Repository replay and ownership

Use this packet from commit `7765296f`, before the subsequent
[image-stage manual acceptance](SOURCE-STAGE-MANUAL.md). From that committed
repository root, use a fresh direct child of `build` on each host:

```powershell
python docs/bootstrap/prototypes/replay-native-source-stage.py --output build/source-stage-review
python docs/bootstrap/prototypes/verify-native-source-stage-replay.py --replay build/source-stage-review
```

```sh
python3 docs/bootstrap/prototypes/replay-native-source-stage.py --output build/source-stage-review
python3 docs/bootstrap/prototypes/verify-native-source-stage-replay.py --replay build/source-stage-review
```

Use different fresh destinations when both hosts share the same worktree.
Both occupied-output and out-of-tree checks reject before writing into the
destination. The replay guards twenty root files, including the 73,031-byte
manual and its measured artifact policy, and the committed `72a02d85` basis.
The later 73,655-byte manual changes two guarded files. Replay at
`7765296f` to retain the packet's original controls.

The patch supplies seventeen patched sources and four fixture/runtime
supplements, twenty-one logical files in total. Nine argument-owner sources
are copied as unchanged replay controls and are unused by this capability.
Apply this combined patch without first applying its older prerequisite
patches. None of the 99 normal producer inputs or fifteen installed seed files
changes. The build graph retains 449 CupidBuild actions and three Python
coordinators. Full required-input discovery, physical-root interning,
distinct present-stage capacity, command integration and publication remain
open. TempleOS stays read-only and excluded from builds and metrics.
