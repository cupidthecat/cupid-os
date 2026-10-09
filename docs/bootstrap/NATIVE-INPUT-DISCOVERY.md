# Native image input discovery

The private source at `C:/Users/admin/cp7/input-discovery-source4` owns the
path strings and read-only observers needed by an interpreted image request.
Its Linux copy is `/var/tmp/input-discovery-source4`. This follows the
[native option owner](NATIVE-IMAGE-OPTIONS.md) and the retained
[UNC observer](OBSERVER-UNC-ROOTS.md). Normal image publication still uses Python.

## Retained selection and lifetime

The host path selector normalizes the working-root/request spelling, resolves
existing parent aliases and retains the physical directory before closing the
resolution handle. Required files must exist and be ordinary regular files.
Optional missing paths retain the first absent component beneath the nearest
existing parent. Directory selection retains the directory itself. Linked and
special file leaves fail. Existing parent aliases remain accepted.

Selection owns its physical root, relative logical path and absolute file path.
Revalidation checks all ordinary metadata, streamed payload and absence
observations, then reopens the original parent spelling and compares its
directory identity. The shared directory resolver now returns ordinary UNC
physical names as well as drive names. No hosted runtime import is added.

Discovery owns the working-root spelling, requested and physical input paths,
stage destinations and image spelling. It observes the required seed-manifest,
bootloader and kernel paths plus optional stages. Files beneath the primary
physical root use that observer; external roots are interned by retained
directory identity. Exact repeated requests reuse their observations. Every
original path binding is checked again during revalidation. A failed check
invalidates later views. Partial construction closes every owned root slot.

The retained file-identity comparison reads only cached file observations after
ordinary revalidation. It cannot discover an unobserved leaf or authorize it.
Invalid arguments, unretained paths and changed observations fail and poison
the supplied observers. A supplied result is cleared on failure. The caller
must close any transaction borrowing discovery's observers before closing
discovery itself.

## Actual builds and runtime contracts

The first complete discovery selection executes 128 methods and 188 calls
through four callers, with twelve platform skips. Its accepted record is
`input-discovery-independent2/closed.json` under `C:/Users/admin/cp7`.
The subsequent fixture adds five direct file-identity methods while preserving
every implementation byte. Fresh native and Cupid builds pass all forty methods
per caller: 160 selections, 148 executions, twelve skips and 236 actual calls.
Windows executes sixty calls per caller; Linux executes fifty-eight. Forty-eight
calls exercise the direct comparison boundary.

Actual cases cover copied argument lifetimes, primary descendants, three
external required roots, shared external parents, duplicate sources, parent
aliases, required hardlinks, long Unicode paths, real drive/UNC/server aliases,
missing ancestors, useful file/directory/link/FIFO rejections and cleanup.
Held processes check original parent and working-root rebinding, metadata drift,
same-size streamed edits with restored timestamps and first-absence appearance.
All 4,096 stage rows survive released option/argv storage. That case repeats one
source file; it does not prove 4,096 distinct retained observations. Sixty-four
external roots pass, and a sixty-fifth external root rejects cleanly. Help does
not observe an unavailable working root. Large sector geometries are retained
without creating or publishing a large image.

Both checked builds use the Cupid compiler from the complete qualified normal
99-input source preparation and the existing installed seed helper tools.
Two compile workers, original process bounds and all nine forbidden conventional
producer settings remain. Actual i386 objects and ELF/PE import profiles are
validated. Linux runtime calls keep sixty seconds and 32 MiB. Native builds use
Clang only as the separately recorded comparison path.

Independent evidence is `input-discovery-independent4/closed.json`; its outer
receipt is `input-discovery-independent4-record.json`. It passes in 30.282
seconds and rereads all 206 source/support controls, every command/result,
complete objects, actual compiler copies, original preparation and parent tools.
Native/Cupid complete results agree on each host after replacing only each
fixture's products-root spelling. Five whole checked object pairs agree:

| Object | Bytes | SHA-256 |
| --- | ---: | --- |
| path_encoding | 10,000 | `34ab4aeaa38aed335ac99ac3a3deccfa27e9c6ac70de3dfb8f898ec34cf03192` |
| cupidbuild_disk_stage_arguments | 5,932 | `825fc3800386166cf0df354a725149f8ff3a011ecdd477f0741dea06fd3371f5` |
| cupidbuild_disk_image_options | 19,736 | `28b73d656d6e6bb48361588e9875cad0884562ba964b3bbfe0acd722da31bdfd` |
| cupidbuild_disk_input_discovery | 15,340 | `930b7ccac1b4d7b63e7cab7105970e54f03c97427ee0c47c1b82c3894b50a372` |
| disk-input-discovery-contract | 21,220 | `6f7507b06dd4aa35de3d2b1cee07e84339932eb31b3d74005a59f0c0ffad640e` |

The 13,002-byte discovery implementation has SHA-256
`c34a324bc1f5209fb0250fd2b33c62127ff43ba5e6be8a398e722460ff1c8b34`.
Its source-controls record measures 179,465 bytes, SHA-256
`98e72d9e239606824f2a5924e715e7d5839299bec75c7952b699fbe52041d921`.

The changed host also passes all 52 ordinary-root and 48 Windows UNC regression
executions, for 106 actual calls and four ordinary platform skips. Those fresh
checked builds retain every non-fixture object byte from the discovery builds.
Their fixtures and sixty-second/32-MiB profiles remain unchanged. The independent
record checks all six regression selections and their complete actual results.

## Retained failures and remaining work

The [distinct-input follow-up](../adr/0460-retain-distinct-image-input-capacity.md)
now retains explicit observer quotas, streams already retained file records and
validates the complete construction before exposing a view. It is held separately
in `input-discovery-source7` and its paired Linux copy. All four builders and both
native 64-method selections pass, including 4,096 distinct present/missing/nested
inputs. All 100 original ordinary/UNC observer regression executions and 106
calls also pass. Deterministic construction hooks prove rejection of kernel
metadata drift and original working-root/selected-parent rebinding before return.

Source seven's checked selections remain preserved failures: Windows has four
capacity allocation failures, and Linux has four original sixty-second failures
with 32 MiB. The [hosted heap follow-up](HOSTED-HEAP.md) isolates Windows address
placement and Linux's quadratic allocated-block search, then changes only the
shared runtime in private source nine. All four complete 64-method selections
and legacy observer selections pass through guarded cohort ten. It retains every
original runtime deadline and Linux memory bound and enforces all nine checked
host-producer sentinels. Paired independent review accepts all 242 executions
and 410 discovery calls, five complete object pairs, and all 100 legacy observer
executions with 106 calls. It rereads 206 controls and confirms that every checked
object other than the runtime retains its source-seven bytes on each host.
Named-commit source qualification and normal command integration remain required.
The preceding complete 236-call acceptance and every later failed selection
stay preserved.

The first fixtures spell the WAD option incorrectly and match a UNC kind against
the word "missing" in the test directory name. A second fixture assumes the
wrong WAD destination. Corrections use `--wads`, explicit kind expectations and
the original `_wad_dest` oracle. All implementation and C contract bytes remain
unchanged across those corrections. The later direct-identity extension changes
only the two contract fixtures. Original failed calls remain retained.

The first independent checker assumes forty-five Linux calls where the actual
selection has forty-six. Its successor checks the measured complete multiplicity
and all observer regressions. The expanded checker initially retains old
artifact-directory labels. Correcting those locations preserves every source,
profile, method, result and whole-object predicate. Both failures remain held.

Discovery currently keeps the existing 8,191-byte path, 1,023-byte component,
observer-entry and transaction-input bounds. The strict POSIX name profile also
needs review against the complete original CLI pathname surface. Input discovery
does not validate the selected seed release, grant source authority, freeze
external required inputs, prepare output parents, reject output/input aliases or
publish an image. The image request is an owned spelling only.

Full command integration must retain the complete paired seed trust unit,
external required boot/kernel inputs, original path observations and prepared
output through guarded publication. It must preserve ordinary parent aliases,
input aliases, optional missing paths and accepted large image geometries.
Unique-input capacity and complete CLI diagnostics remain acceptance work.
Source-plan integration must capture every new source and decimal table, then
pass complete producer qualification and normal OS/SDK/public consumers before
recipe adoption. No coordinator or ownership count changes through this private
record. The 99 normal inputs still equal committed `acbbd834`; fifteen installed
seed files retain their parent identities. TempleOS stays read-only and excluded.
