# Optional input observations and Windows UNC roots

## Current result, 2026-10-10

The held host extension retains the first missing component of an optional
pathname, opens ordinary Windows UNC share roots, and resolves Unicode and long
directory names through the existing Windows adapter. Its
[unapplied patch](prototypes/native-observer-paths.patch) includes the earlier
[quota, retained-stream and identity prerequisites](NATIVE-RETAINED-OBSERVER.md).
It applies directly to the normal host source; the earlier patch is not applied
first. The five patch files produce nine complete build source copies, including
the unchanged path codec and generated original legacy callers.

Four native/Cupid callers pass 388 API calls, 340 original legacy methods with
twelve declared platform skips, and all 48 original alias methods. Six complete
shared contract object pairs agree. All 36 strict object/program checks pass.
The [original paired evidence](evidence/native-observer-paths-20261010.json)
binds every complete source copy, checked tool, command, product and result.
Its [receipt archive](evidence/native-observer-paths-originals-20261010.json.gz)
retains the failed source copies, original timeouts and differential probes.

This is an observer and directory-resolution prerequisite. The complete native
pathname selector, image input owner and publisher handoff remain open. All 99
normal producer inputs still match committed `1d852023`; all fifteen installed
seed files retain the `acbbd834` cohort. Normal ownership remains 449 CupidBuild
actions and three Python coordinators across 452 transforms.

## First absence and retained custody

The later [source name profile](NATIVE-SOURCE-OBSERVER.md) includes this complete
extension and adds literal POSIX colon/backslash input names. Its direct patch
and paired replays preserve all outcomes below. Normal observer and output-name
policies remain unchanged.

`cupidbuild_host_observer_path_kind` returns a retained regular-file, directory
or missing observation. It validates the entire normalized UTF-8 path before
the first missing component can end the walk. Paths retain the 8,191-byte limit
and components the 1,023-byte limit. Malformed or unsafe suffixes still reject
when an earlier component is absent. Invalid arguments clear a supplied kind
result and poison the observer.

A missing record owns its first absent component name and original retained
parent. It consumes an entry quota but holds no nonexistent file handle or
file identity. Repeated requests reuse that record only after a fresh absence
check. A previously retained file or directory disappearing poisons the
observer; it cannot become a successful missing observation. Existing or
appearing links and special files also reject.

Full observer validation rechecks every recorded absence and original parent
binding. Closing an observer frees absence names and records without closing
invalid handles. File identity lookup excludes absence records. Classification
checks the complete root binding once per request and each traversed parent and
child binding. It supplies no whole-observer validity verdict. A future input
owner must validate every observer and original request before returning a view,
then repeat those checks for later use and publication.

## Ordinary UNC roots and directory resolution

Windows observer construction selects an ordinary drive or server/share anchor
after the existing absolute UTF-8 conversion. UNC server and share components
use the strict existing name checks. The share opens with read-only directory
access and the existing sharing/reparse flags; each descendant still opens
through its retained no-follow parent. Share-only roots work. Device namespaces,
incomplete or unavailable shares, files and junction descendants reject.
Tests use existing local administrative shares without creating or changing a
share. They cover localhost/address, case/separator and drive/share aliases,
Unicode, long roots, retained payloads and original directory custody.

The existing directory resolver now returns ordinary UNC physical names from
`GetFinalPathNameByHandleW`, as it already does for drive paths. Native Windows
uses the existing UTF-16 codec and internal extended prefix before `CreateFileW`.
The Cupid-built default runtime uses its existing UTF-8 `CreateFileA` adapter,
with that same prefix constructed inside the resolver. This handles long paths
even though the default runtime profile does not add a long-path prefix itself.
The normal profile, imported functions, public types and ordinary device-path
rejections remain unchanged. Resolution follows existing parent aliases and
retains the resulting directory; it creates no output entry.

## Original acceptance

| Host | Missing / UNC calls | Retained calls | Resolver calls | Legacy passes / skips | Alias passes | Strict checks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Windows | 104 | 92 | 22 | 167 / 9 | 24 | 21 |
| Linux | 64 | 86 | 20 | 173 / 3 | 24 | 15 |

Every new API invocation keeps its original total sixty-second deadline,
including paused mutations. Linux keeps 32 MiB of address space and 10,240
descriptors. Each caller completes all 4,096 distinct flat missing, nested
missing and distinct first-absence paths, including appearance of the last
recorded absence. Windows repeats all four complete cases through UNC roots.
The checked Windows UNC cases take 27.181 to 36.459 seconds; the checked Linux
missing cases take 1.175 to 1.518 seconds. These are original measured runs,
not a performance ratio. Ordinary constructors still reject entry exhaustion.

Other cases cover missing-to-file/directory changes, vanished present entries,
existing/appearing links and FIFOs, malformed suffixes, oversized components and
paths, null outputs and poison. Linux replaces an observed nested parent outside
the case root while restoring root metadata; validation rejects the lost
binding. Windows denies replacement while the parent is retained, accepts the
unchanged observer, and permits rename after close.

The complete retained-stream selection keeps all 4,096 flat and nested regular
files, payload sizes through 1,048,577 bytes, ordered callbacks, whole SHA-256
results and restored-time edits. Four added Windows cases cover UNC streaming,
digest drift, captured identity versus ordinary revalidation and custody.

Both original legacy selections keep their test bodies, predicates, process
resource limits and timeouts. The alias selection also uses its unchanged twelve
methods per caller: relative/absolute/lexical requests, internal and external
parents, missing leaves, nonregular leaves, parent replacement, retargeted aliases
and Unicode paths beyond 600 characters. The checked alias caller receives its
own strict object and program checks. All nine conventional producer variables
name forbidden commands; native Clang serves only as a separate comparison.

## Retained failures and fixes

The first header places the kind API before its enum declaration. Moving the
declaration below the existing type repairs compilation. A later custom Windows
branch uses a compiler macro absent from the actual Cupid build. Selecting the
existing hosted-header guard repairs that branch without adding producer flags.
Both failed builds remain retained.

The first full native nested UNC case exceeds sixty seconds. A 32/64/128-entry
probe finds repeated walks of the unchanged absolute root chain for each
component. The fix checks that complete chain once per API call while retaining
each descendant identity and fresh binding check. The final full 4,096-entry
requests pass under the original deadline. Only diagnostic copies contain
counters; no deadline, capacity or final observer check is reduced.

The first directory-resolution fixture assumes an absolute output getter,
although the original API returns its relative spelling. Correcting that fixture
exposes a real Unicode failure. Direct native API probes show ANSI opening fails
for Unicode; wide opening succeeds, and long paths require the internal extended
prefix. The native resolver adopts those existing codecs. The checked default
profile then exposes its separate missing-prefix behavior on the same long
Unicode directory. A two-run differential probe passes only the internal-prefix
form through the unchanged adapter. The final resolver supplies that prefix
without changing the runtime profile or imports.

One test transport rejects the deliberately oversized path before it reaches
the API. Increasing only the fixture's input buffer lets the unchanged production
limit reject the full request. Platform-neutral Cupid fixture rows also lack the
native `_WIN32` macro; querying the existing runtime host format selects the
actual custody expectation. The first minimal-header diagnostic names an absent
directory constant; its corrected copy uses the existing numeric value. Original
receipts, fixture copies and probes remain in the archive. No failed full
selection is treated as acceptance.

## Repository reproduction and integration

With native Clang available for comparison, run from the bootstrap branch:

```text
python docs/bootstrap/prototypes/replay-native-observer-paths.py --output build/observer-paths-replay
```

Use `python3` on Linux. The output directory must be fresh and below repository
`build`. The optional `--manifest` selects a recognized native-host seed manifest.
The replay checks all 99 committed inputs and fifteen installed seed files,
extracts only the five reviewed patch files, freezes the actual Cupid tools,
builds five callers, then runs the complete new, retained, legacy and alias
selections. Its seven helpers live in the repository and need no private build
directory. Linux cases use fresh `/var/tmp` directories; all products and failed
receipts are retained.

Both installed-seed replays pass every complete API, legacy and alias outcome
again. Windows closes in 919.760 seconds and Linux in 1,468.524 seconds, excluding
preflight. Independent rereading compares all 24 complete checked objects and
twelve complete checked programs against the original qualified-tool builds;
every byte agrees. It also checks every original outcome and API bound, all
source copies, helper identities, installed tools and unchanged normal inputs.
The [replay evidence](evidence/native-observer-paths-replay-20261010.json)
binds both complete [replay receipts](evidence/native-observer-paths-replays-20261010.json.gz).

The separate [source spelling component](NATIVE-SOURCE-PATHS.md) now preserves
lexical paths before following filesystem aliases. Four native/Cupid callers
pass 354 calls covering actual Python spellings and useful C failures. Both
complete repository replays retain all cases and matching complete products.
Combining that component with retained observations still requires the complete
physical pathname selector and its original-request revalidation.

Source integration waits for the hosted heap cohort's remaining actual Make
consumers and seed adoption. The complete input owner still needs
original pathname revalidation, final construction checks and borrowed observer
lifetimes. Normal command integration requires complete CLI/image comparisons,
a new qualified cohort, OS builds and boots. Remote-share disconnection and
namespace interference need their own tests. TempleOS stays read-only and
excluded.
