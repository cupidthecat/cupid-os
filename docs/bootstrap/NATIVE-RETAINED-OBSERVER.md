# Retained observer prerequisites for native image discovery

## Optional paths and UNC follow-up, 2026-10-10

The [current observer-path extension](NATIVE-OBSERVER-PATHS.md) includes this
patch's quota, retained-stream and captured-identity APIs, then adds retained
first absences, ordinary UNC roots and Unicode/long directory resolution.
Its four callers pass 388 API calls, the same complete legacy selections and
all 48 original alias methods. Six shared object pairs and 36 strict checks
pass. Its patch applies directly to normal source; the earlier patch below
remains independently reproducible. Both installed-seed replays pass every
outcome again and reproduce all 24 checked objects and twelve complete programs.
Native pathname selection and the complete
image input owner still precede normal command adoption.

## Current result, 2026-10-10

The held host extension gives image discovery an explicit entry budget, streams
files through their existing observation records, and compares captured root
and file identities during serialized construction. The implementation follows
[ADR 0460](../adr/0460-retain-distinct-image-input-capacity.md). Its source is
retained in an [unapplied patch](prototypes/native-retained-observer.patch).
The two original host implementations and all normal producer inputs remain
unchanged.

Four native/Cupid callers pass 170 API calls. Every caller observes and streams
4,096 distinct flat files and 4,096 distinct nested files. The unchanged legacy
observer and streaming selections also pass on both hosts: 352 selections,
340 passes and twelve declared skips. Three complete shared contract object
pairs agree. The original builds pass all 24 strict object/program checks.

This restores a reproducible prerequisite from the historical private discovery
work. The old `cp7` products are absent from the current worktree and do not
establish this result. The current follow-up above supplies missing-stage
observation and Windows UNC roots. Native pathname selection and the full image
input owner still require implementation and integrated acceptance.
The normal graph retains 449 CupidBuild actions and
three Python coordinators across 452 transforms.

## API and lifetime rules

| Operation | Contract |
| --- | --- |
| `cupidbuild_host_observer_open_quota` | Set an explicit retained-entry budget. Allocate linked records as observations arrive. Zero and SIZE_MAX reject before resolution and clear the owner output. |
| `cupidbuild_host_observer_file_stream_retained` | Stream a regular file already retained by that observer. Validate its original handle and every ancestor binding before and after capture, preserve a previous digest, then upgrade the same record's handle. |
| `cupidbuild_host_observer_roots_same` / `files_same` | Revalidate both complete observers, then compare captured device/file identities. File paths must already name retained regular files. |
| `cupidbuild_host_observer_roots_captured_same` / `files_captured_same` | Compare historical captured identities during serialized construction. These calls do not revalidate metadata, digests or pathname bindings. |

Ordinary open keeps its 4,096-entry budget, including absolute ancestors and
file records. Directory membership and metadata batch bounds remain separate.
The explicit constructor does not allocate storage for its whole quota.

Retained streaming discovers no new leaf and grants no publication authority.
It reopens through the retained no-follow parent, uses the existing bounded
stream reader and checks full file metadata. Intermediate directories retain
their identity checks; the root retains its full metadata check. The temporary
payload handle replaces the old handle only after successful validation. A
failed operation poisons the observer and clears a supplied stream result.
Sink ordering, callback ownership and serialization rules remain unchanged.

Identity queries clear their supplied result on failure and poison supplied
observers. Invalid or unretained file paths reject, and captured comparisons
cannot bypass existing poison. A future discovery owner must validate every
observer and original request before exposing its result. Publication and
later checks must repeat that validation; captured identity comparisons supply
no cached validity verdict.

## Original acceptance

| Host | Cases per caller | API calls | Legacy passes / skips | Checked flat / nested runtime |
| --- | ---: | ---: | ---: | ---: |
| Windows | 42 | 84 | 167 / 9 | 5.573 s / 7.582 s |
| Linux | 43 | 86 | 173 / 3 | 1.048 s / 2.057 s |

Every API invocation keeps a total sixty-second deadline, including paused
mutations. Linux retains the 32 MiB address-space and 10,240-descriptor bounds.
The complete flat and nested inputs stay at 4,096 distinct files. Preparation
and receipt guards are outside each program's recorded runtime.

The ordinary Linux streaming control first retains all nested metadata, then
adds separate stream records. Both native and Cupid callers reach the original
descriptor limit after 2,039 streams. Retained streaming completes all 4,096
under the same limit. Ordinary construction still exhausts its default entry
budget at 4,090 flat files on the Linux fixture and 4,086 on Windows; these
counts include each fixture's absolute ancestors.

Other cases cover invalid quotas, unobserved and directory streams, invalid
UTF-8 and unsafe components, null result pointers, sink failure, zero limits,
root/file equality, distinct identities and hard links. Eleven payload sizes
from zero through 1,048,577 bytes check ordered callback bytes, complete SHA-256
results and repeated streaming. Paused tests reject size changes, edits with
restored timestamps and poison bypass. Historical comparisons can report their
captured identities before an ordinary comparison rejects drift.

Linux replaces a retained parent directory and verifies stream rejection.
Windows denies that rename while the directory is retained; the test checks
the unchanged stream, full validation and a successful rename after close.
This records each platform's actual custody behavior.

The legacy selections run all 75 observer and thirteen streaming methods for
each caller. Their original bodies, predicates and timeouts are unchanged.
Native execution retains its declared invalid-stdio skip; the checked caller
executes that case. Streaming retains the original large-file and sparse-file
cases. All nine checked host-producer variables name forbidden commands.

## Retained failures

The first Linux build wrapper reads a missing `definitions` key from a normal
build-plan row after native compilation succeeds. Using the plan's existing
empty-definition and non-GNU defaults repairs the wrapper. The failed receipt
and original wrapper remain in the receipt archive.

The first legacy runner adds a 32 MiB memory limit that those original tests
did not have. Six checked Linux methods then fail while growing the frozen
transaction table from 512 to 528 inputs. A strict Cupid-built control using
the unmodified host source reproduces the same allocation failure twice under
that limit. Both versions pass twice with the original resource limits.
Correcting the legacy runner restores inherited process limits and makes its
complete original selection pass. The new API selection keeps its original
sixty-second, 32 MiB and descriptor bounds. No runtime or request was changed
to repair this runner error.

The first evidence checker names an absent support module. Capturing the actual
`tools/cupidc_toolchain_contracts.py` module repairs the checker without changing
any program or runtime receipt.

## Reproduction and integration

With native Clang available for comparison, run from the bootstrap branch:

```text
python docs/bootstrap/prototypes/replay-native-retained-observer.py --output build/retained-observer-replay
```

Use `python3` on Linux. The output directory must be fresh and below the
repository's `build` directory. The optional `--manifest` selects a recognized
native-host seed manifest. The replay verifies the exact 99 committed inputs,
extracts only the three reviewed patch files, freezes installed Cupid tools,
builds three callers and runs both complete selections. Its helpers live in
the repository and require no earlier private build directory. Linux case
trees use fresh directories under `/var/tmp`; all products and failed receipts
are retained.

Both installed-seed repository replays pass the same complete API and legacy
outcomes again. Windows closes in 394.698 seconds and Linux in 746.098 seconds,
excluding preflight. Independent rereading checks every outcome and all source
copies, plus all eighteen complete checked objects and six complete programs
against the original qualified-tool builds. Every object and program agrees
byte for byte. The [replay evidence](evidence/native-retained-observer-replay-20261010.json)
binds both complete [replay receipts](evidence/native-retained-observer-replays-20261010.json.gz).

The [paired evidence](evidence/native-retained-observer-20261010.json) binds the
seven complete copied sources, tools, programs, object pairs and every original
result. Its [receipt archive](evidence/native-retained-observer-originals-20261010.json.gz)
also retains the failed wrappers and differential capacity controls.

The extension must join complete native pathname selection and image input
discovery after the hosted heap cohort finishes its required consumers and
adoption. Normal source integration requires a new qualified cohort and the
complete image command comparisons, OS builds and boots. The current work
changes none of the fifteen installed seed files or 99 producer inputs.
TempleOS remains excluded.
