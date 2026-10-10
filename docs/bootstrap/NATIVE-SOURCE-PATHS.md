# Native image source spellings

The held `cupidbuild_source_path_absolute` component produces the image command's
lexical host pathname before filesystem aliases are resolved. Four native/Cupid
callers pass 354 calls covering actual Python pathname oracles and C failure
contracts: 314 successful spellings and forty useful rejections. All three
complete checked object pairs agree across Linux and Windows, and all eight
strict disassembly checks pass.

The [unapplied patch](prototypes/native-source-path.patch) contains the C module,
its header and its caller fixture. [Independent evidence](evidence/native-source-paths-20261010.json)
binds the exact source, tools, complete products and
[original receipts](evidence/native-source-paths-originals-20261010.json.gz).
The normal 99-input producer cohort and fifteen installed seed files are
unchanged. Normal ownership remains 449 CupidBuild actions and three Python
coordinators across 452 transforms.

## Spelling contract

The separate [source observer profile](NATIVE-SOURCE-OBSERVER.md) now retains
POSIX colon and backslash names accepted by these lexical rules. Physical
selection and original-request binding remain separate input-owner work.

Relative requests use the supplied absolute working-root spelling. POSIX
normalization removes duplicate separators and dot components, collapses parent
components before following any filesystem alias, and preserves exactly two
leading slashes. Colons, backslashes, spaces and literal newlines remain POSIX
filename characters. An empty request selects the working root.

Windows uses the existing absolute-root adapter and its native per-drive
directory rules. Same-drive relative requests use the supplied root. Rooted
requests keep its drive or share anchor. The finishing step preserves drive and
share roots while removing ordinary trailing separators, including requests
relative to another drive. Incomplete UNC spellings and extended UNC share roots
retain the string representation produced by the original `Path()` rules.
Representing a namespace spelling supplies no filesystem access authority.

Each input and the result must contain at most 8,191 UTF-8 bytes. Input and
output buffers must not overlap. The caller supplies output capacity; failure
clears an output that has nonzero capacity. A null working root is usable for an
absolute request whose interpretation does not need it. This component observes
no file, resolves no filesystem alias and creates no namespace entry.

## Actual oracle and caller checks

The oracle runs the unchanged `_disk_absolute(Path(...))` implementation under
the actual Windows Python 3.14.3 and Linux Python 3.12.3 runtimes. Its retained
case trees contain ordinary, empty, Unicode and long Unicode files, parent
aliases, lexical cancellation of missing components, missing leaves and parents,
and nonregular inputs. Windows also uses real localhost/address UNC spellings.
Linux retains colon, backslash and newline files, symlinks, a broken parent alias
and a FIFO.

The oracle also records the original required/optional file-selection outcomes
for later integration. The C comparisons here use its absolute spelling field;
they do not establish a retained filesystem selector.

| Host | Original oracle cases per caller | Additional lexical cases | Further grammar cases | Total native/Cupid calls | Successful spellings | Rejections |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Windows | 19 | 14 | 63 | 212 | 192 | 20 |
| Linux | 23 | 13 | 25 | 142 | 122 | 20 |

Each caller also checks nine direct failures and an absolute request with an
unused null root. Failures cover null arguments, zero and insufficient output
capacity, malformed UTF-8, a relative working root and oversized input. Further
grammar cases include drive-relative requests, repeated root separators,
ordinary/device/extended UNC shapes and the 8,191-byte success/8,192-byte rejection
boundary. Every record retains complete expected and actual stdout, status and
its original process bound.

The builds use the installed native-host Cupid seeds, with all nine conventional
producer variables naming forbidden commands. Native Clang is the separately
recorded comparison. Checked callers borrow exact objects from the accepted
[observer replay](NATIVE-OBSERVER-PATHS.md); each build retains and rechecks that
complete source/body binding. Windows native transport uses wide argv and the
existing UTF-8 codec, with binary stdout. Linux calls retain sixty seconds,
32 MiB address space and 10,240 descriptors. Windows calls retain sixty seconds.
Compilation, linking and strict checks retain their original bounds.

| Complete shared checked object | Bytes | SHA-256 |
| --- | ---: | --- |
| source-path | 9,132 | `0b2cf52dedc89bb4fa044a4a2fd3e9f1368c75e6f631ebb03bf0a856f45e7635` |
| source-contract | 2,864 | `217471bd9782f91617b18f6a4d2e468cced4fcc39fc777ec0dec225c166fc204` |
| path-encoding | 10,000 | `34ab4aeaa38aed335ac99ac3a3deccfa27e9c6ac70de3dfb8f898ec34cf03192` |

Independent rereading takes 5.297 seconds. It checks every complete source,
object, program and runtime record, the actual installed tools, observer inputs,
all 99 named-commit producer inputs and fifteen unchanged installed seed files.
The original archive restores 1,671,016 bytes from 78,230 compressed bytes.

## Retained failures

The first builder imports its portable control reader directly from the docs
directory, giving that reader the wrong repository root. Copying it into the
owned build directory repairs preflight. Both failures precede compilation.

The first POSIX implementation rejects an empty request, while the actual
`Path("")` oracle selects the working root. The next source accepts it. The first
Windows native fixture receives ANSI argv, changing `ordinary/δ-猫.bin` to
`ordinary/d-?.bin` before the path function runs. A direct argv writer confirms
that transport loss. Wide argv plus the existing codec supplies the original
UTF-8 bytes to the function.

The next Windows run exposes ordinary trailing separators left by the native
absolute-path function. The finishing step adopts the original `Path()` result.
A later newline case exposes text-mode stdout conversion to CRLF. A direct
writer distinguishes text and binary output; the fixture switches to binary
stdout without changing the C path function.

The expanded Windows grammar then retains eighteen failures across both callers:
another drive's relative paths, incomplete UNC spellings and extended UNC share
roots. The final source applies the finishing step to the other-drive branch and
handles those root shapes. All original and expanded selections pass without
reducing their cases or bounds. Source generations one through five, failed
receipts and direct probes remain in the archive.

The first repository replay compares observer role keys with filename keys.
Normalizing those names repairs the checker while preserving all five complete
program predicates. Both failed preflights remain retained. The C module and
its successful 354-call acceptance are unchanged by that checker repair.

## Repository reproduction and remaining ownership

First run the complete observer replay described in
[its record](NATIVE-OBSERVER-PATHS.md). Keep its accepted products. On Windows:

```text
python docs/bootstrap/prototypes/replay-native-source-paths.py --output build/source-paths-replay --observer-products build/observer-paths-replay/products-windows1
```

On Linux:

```text
python3 docs/bootstrap/prototypes/replay-native-source-paths.py --output build/source-paths-replay --observer-products build/observer-paths-replay/products-linux1
```

Use a fresh direct child of repository `build` for the output. The five helpers
are retained in the repository. The replay checks the complete accepted observer
source and products, all 99 committed producer inputs and fifteen installed seed
files. It extracts only the three reviewed new files, freezes the actual Cupid
tools, captures fresh oracles, rebuilds both native and Cupid callers, and repeats
the complete original and expanded cases. Both native comparisons recompile the
retained host and path-codec bodies. Every checked object and the complete checked
program must equal the original products.

Both complete installed-seed replays pass every case again. Windows closes in
38.025 seconds and Linux in 168.275 seconds, excluding preflight. Their complete
checked products retain the original identities.

[Independent replay rereading](evidence/native-source-paths-replay-20261010.json)
takes 3.157 seconds. It accepts every complete oracle outcome and runtime record,
six whole checked objects and two whole checked programs, and all original
bounds. The [replay archive](evidence/native-source-paths-replays-20261010.json.gz)
retains 643,910 complete receipt bytes in 47,983 compressed bytes. The first
checker overlooks escaped case-root spellings in Python errors. Recognizing
those representations of the same known temporary roots repairs comparison
without dropping an error, case or output predicate. That checker is retained.

Full pathname selection still needs physical-parent custody, original-request
revalidation, existing aliases, first absences, source-name profiles and borrowed
observer lifetimes. The existing strict POSIX observer profile rejects colon and
backslash components that the original image CLI accepts. The bounded UTF-8
spelling API also leaves the original CLI's wider pathname surface for later
acceptance. No image-command, input-owner or publisher ownership is transferred
through this component. Integration needs a new qualified producer cohort,
complete CLI/image comparisons and the relevant OS builds and boots. TempleOS
remains read-only and excluded.
