# Retained original source roots

The held source-root constructor resolves an absolute directory request and
retains both its original spelling and physical directory. It keeps the first
resolution handle open while constructing the source observer, compares the
two directory identities, copies both names, and revalidates before returning.
Later complete validation and borrowed publication checks resolve the original
request again. Retargeting an alias rejects even when the retained physical
directory and its original payload still exist.

Four native/Cupid callers pass 176 new calls: 76 successes and 100 rejections.
The changed host also passes all 564 source-profile calls, 388 preceding API
calls, 340 original legacy methods with twelve declared skips, and all 48 alias
methods. Eight whole shared object pairs agree. All 44 strict checks pass.

The [unapplied patch](prototypes/native-source-root.patch) includes the earlier
[observer-path extension](NATIVE-OBSERVER-PATHS.md) and
[source name profile](NATIVE-SOURCE-OBSERVER.md). Apply this patch directly to
the normal host source; do not apply the earlier patches first. Nine
[replay helpers](prototypes/native-source-root/) and the
[repository runner](prototypes/replay-native-source-root.py) retain the complete
selection. [Original evidence](evidence/native-source-root-20261010.json) binds
all eleven source copies, actual tools, complete products and
[full receipts](evidence/native-source-root-originals-20261010.json.gz).

All 99 normal producer inputs still match committed `1d852023`; the fifteen
installed seed files retain the bytes checked in at `acbbd834`.
The host extension remains outside the normal producer closure. Normal
ownership remains 449 CupidBuild actions and three Python coordinators across
452 transforms. TempleOS is excluded.

## Directory request ownership

`cupidbuild_host_observer_resolve_source_root_quota` accepts an absolute UTF-8
directory request, a retained-entry quota, an owner output and a borrowed
physical-root output. The physical string belongs to the observer and remains
valid through its close. The caller may release or overwrite its original
request after construction; the fixture overwrites that storage before using
the owner.

Null or empty requests, relative paths, malformed UTF-8, requests of 8,192 bytes
or more, zero and `SIZE_MAX` quotas, missing directories, regular-file roots
and broken aliases reject. Failure clears both supplied outputs. Windows uses
the existing drive/UNC directory resolver and UTF-8 codec. POSIX uses its
existing directory-handle resolver. The resulting source observer retains the
same quotas, name profile, no-follow observations and payload checks as its
preceding constructor. No new host imports are added.

The owner stores two allocated strings. Its close frees both. Complete
validation compares the newly resolved physical spelling and directory identity
with the captured root. A failed binding check poisons the owner; a later
validation or borrowed publication check cannot restore it. Historical captured
identity comparisons still grant no current source authority.

This constructor handles the working source directory. The later
[required source-file owner](NATIVE-SOURCE-FILE.md) uses it to retain original
parent requests and regular leaves, with linked and special leaves rejected.
Nearest-existing-parent selection for optional missing inputs, complete discovery
and image publication still need their own owner and integrated acceptance. Individual capture calls do not
replace complete validation. The complete image command must retain its owners
and validate them before and after consuming borrowed views.

## Original acceptance

| Host | Root calls | Successes | Rejections | Actual Python directory oracles | Old constructor alias rejections | Complete regression seconds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Windows | 82 | 32 | 50 | 14 | 4 | 798.488 |
| Linux | 94 | 44 | 50 | 20 | 4 | 1,241.703 |

The actual host Python `Path.resolve(strict=True)` selects ordinary, parent
alias, final alias, Unicode, space, dot and repeated-root directory spellings.
Linux also selects roots containing literal colon, backslash and newline
bytes. Each shape checks validation and current same-root identity. The ordinary
source constructor rejects parent and final aliases on both callers, confirming
why the new resolution boundary is needed. These directory oracles describe
the tested root shapes; they are separate from the complete image file selector.

Both validation and actual borrowed publication boundaries run held mutation
cases. Unchanged roots pass. Alias retargeting, removal, broken targets,
replacement by an ordinary directory and same-size payload edits with restored
timestamps reject and remain poisoned. Retargeted roots contain the same
original payload, while the first physical directory remains unchanged. This
isolates original-request binding from payload and retained-directory checks.
Windows uses real junctions; Linux uses real directory symlinks. This packet
does not inject a race inside constructor execution.

Every new call retains sixty seconds. Linux retains 32 MiB address space and
10,240 descriptors. All six preceding runners keep their exact test bodies and
original process bounds. Both builders use actual installed native-host seeds
with all nine conventional producer variables blocked. Native Clang supplies
the separate comparison. Windows builds in 120.090 seconds and Linux in
244.113 seconds. Forty build checks and four alias checks use strict CupidDis
validation. Twenty-two unchanged complete objects equal the accepted source
observer products.

Independent original review takes 5.660 seconds and rereads every complete
product and oracle payload. The public root runner changes only its baseline
program pathname: it uses the freshly built ordinary-constructor program
instead of a preceding local build folder. The original helper, exact single
replacement and original results remain archived. Both complete public replays
accept that derived helper, including all eight ordinary-constructor rejections.

## Complete repository replays

Windows passes the full replay in 1,288.890 seconds; Linux passes in 2,636.952.
Each rebuilds all seven callers and repeats the new root contracts, source
profiles and all five preceding selections. Independent
[replay review](evidence/native-source-root-replay-20261010.json) takes 3.188
seconds and compares every recorded result and bound, all 28 whole checked
objects and sixteen complete checked programs. Eight complete cross-host object
pairs agree. Only elapsed time and the exact temporary case-root prefixes vary.
Logical names, output bytes, rejections, mutation facts and original method
results remain equal. The
[replay archive](evidence/native-source-root-replays-20261010.json.gz) retains
both complete runs and their comparison.

From this packet's accepted checkout, use a fresh direct child of `build`:

```text
python docs/bootstrap/prototypes/replay-native-source-root.py --output build/source-root-replay
```

Use `python3` on Linux. The runner chooses the actual installed native-host
cohort, checks all 99 normal inputs against their raw Git blobs, retains source
and support bytes, and rejects drift at every boundary. The evidence binds the
reader and installed seeds at this checkpoint. A later seed or reader update
needs separate acceptance or a checkout of this checkpoint for reproduction.
