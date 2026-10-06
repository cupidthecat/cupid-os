# Native ISO fixture publication

## Retained observer publication binding

`cupidbuild_host_transaction_borrow_observer` binds one successful observer to
the transaction's retained root identity before any publication attempt. The
caller keeps it alive through transaction close. Each publication boundary,
including the check after installation, validates captured payloads and metadata,
ancestor bindings and exact memberships. Root size/time changes from publication
are allowed; root identity and explicit memberships remain checked. Binding
failure forbids publication. Source-only observation drift still allows verified
old-output restoration through the existing recovery boundary.

All ten binding methods pass with native and Cupid-built callers on both hosts.
Two native race methods per host inject drift for old and absent output at
before-mutation and after-install checkpoints. All eight fault cases preserve
prior bytes and timestamp or absence, clean state and permit fresh recovery.
Independent evidence records 302 selected methods, 292 executions and ten
platform skips, with matching 27,788-byte caller objects. The receipt is
`iso-bound-observer-lifecycle-paired-independent.json` under
`cupid-native-iso-proof-20261005`. See ADR 0428.

Status, 2026-10-05: inventory validation, retained kind observation, complete
input capture, independent image checking, complete bundle transport and
retained-observer publication binding
implemented and tested. Native publication and recipe handoff remain pending.

The 161,127-byte observer-binding manual passes fresh installed-seed
kernel/image and strict private four-CPU max/e1000 runtime checks with completed
ls and SMP verification. Independent rereading checks 1,589 source controls,
all 429 objects and sixteen artifacts. Only the manual wrapper changes, the
accepted FAT16 object stays identical and FAT data from sector 20,480 remains
intact. The raw kernel measures 9,595,256 bytes; final/pass-one ELF sizes stay
9,822,652 and 9,691,580 bytes. Only the raw-kernel policy row changes. The 200 MiB
image has SHA-256
`4250559828b4a1553b1d32bcbe042c91ed20662a3a5287f7d571273d45a192f4`.
Evidence is `manual-independent-windows.json` under
`cupid-native-iso-bound-observer-manual-20261005`. This installed-seed manual
acceptance does not qualify or install the new source cohort.

The combined FAT16/native ISO manual is 160,376 bytes and passes installed-seed
kernel/image and strict private four-CPU max/e1000 runtime acceptance with
completed ls and SMP checks. Independent proof rereads 1,589 source controls,
all 429 objects and sixteen artifacts. Only the manual wrapper changes from
the accepted FAT16 control, and the image preserves FAT bytes after sector
20,480. Raw/final/pass-one kernel sizes are 9,594,508, 9,822,652 and 9,691,580
bytes. The 200 MiB image has SHA-256
`2c59acf0467a97dcf99d5fe9a3f7636a8df1d23153e1060294f4402fe858479d`.
Evidence is `manual-independent-windows.json` under
`cupid-native-iso-adoption-manual-20261005`. Earlier manual proofs below retain
their exact checkpoint scope. The canonical audit and check pass with 775
active sources and 53 sources outside supported roots, including the accepted
FAT16 test fixture. Normal ownership remains 447 CupidBuild and five Python
actions across 452 transforms.

The complete request now fits a bounded `cupidobj iso-fixture-bundle` command.
`CUPISO1` preserves the manifest, ordered kinds, logical names and payload bytes
without native file paths. Its pure codec checks framing and arena ownership;
the existing producer retains semantic validation and image layout. Current
native/candidate plans and SDK comparisons include the codec object while the
installed seed plans retain their identity. ADR 0424 defines the framing, bounds
and remaining publication work.

Twenty methods pass with native and checked codec/producer callers on each
host: 80 selected, 78 executed and two expected Windows descriptor skips.
All older CupidObj CLI commands and the new/current plan cases pass in the
44-method regression selection per host. A real 512-file request with 127-byte
names now uses 310 Windows UTF-16 units, replacing the 182,582-unit launch that
failed with error 206. Both checked producers emit identical 1,224,704-byte
images; retained complete input/image checking and the Python oracle agree.
`iso-bundle-paired-independent.json` rereads 43 captured sources per host, actual
commands, logs, image profiles, four identical object pairs and complete fixture
inputs/outputs. All 82 SDK model contracts pass on each host with the extended
current fixed-point count and synthetic codec source/link fixtures.

The 158,252-byte bundle manual passes fresh installed-seed kernel/image and
strict private four-CPU runtime acceptance. Its raw kernel is 9,591,040 bytes;
the final/pass-one ELF sizes are 9,818,556 and 9,687,484. Independent verification
checks all 1,589 source controls, 429 objects and sixteen artifacts, with only
the manual wrapper changed and the existing FAT data preserved. The 200 MiB
image has SHA-256
`e2a684819f1699d5ada9da129b1b1a7f46fe736be63b772d28967d8e8b9f1502`.
Evidence is `manual-independent-windows.json` under
`cupid-native-iso-bundle-manual-20261005`.

The current shared reader recognizes the codec-bearing 30-source Linux plan
and all five Windows profiles only through an explicit release. Both hosts
pass the 69-method native selection and twelve methods with Cupid-built
callers; reader/caller object pairs match, and the strict API still rejects
new profiles. Independent evidence is
`checked-iso-seed-profiles-paired-independent.json`. ADR 0426 records exact
plan/count/import bindings and the preserved parent authority.

Both 87-input preparations pass with long paths and aliases. Independent
rereading checks 174 retained source copies, 249 stage artifacts and 83
stage-three/stage-four pairs. Separate release authoring matches all twelve
tools. A separate Windows qualification rebuilds all three stages and passes
seven help, sixty success and fifty-four failure groups in each final stage.
Independent rereading checks 138 stage artifacts, 87 source copies, 46 final-stage
pairs and 100 matching behavior product pairs. This working-source proof is
`iso-windows-release-qualification-independent.json`; committed-source
qualification, the later 528-input change and seed installation remain separate.
The original preparations keep their unqualified status. Linux qualification
was interrupted by a WSL restart before a
closed result; the complete verified preparation copy supports a fresh native
Linux control. The earlier
ordinary Windows strict rejection and Linux frontend timeout remain failed
evidence. The 158,896-byte profile manual now passes installed-seed kernel,
image and strict four-CPU runtime acceptance. Independent evidence under
`cupid-native-iso-profile-manual-20261005` rereads all 429 objects and sixteen
artifacts, finds only its wrapper changed and preserves existing FAT bytes.
Its raw kernel is 9,591,684 bytes; final/pass-one ELF sizes remain
9,818,556 and 9,687,484. The 200 MiB image has SHA-256
`0130e79495a78fde15849931ab49e48efd4c1b646282b65466046d25dd714ddb`.

Canonical audit generation and check pass with the complete codec/header
closure. Both full 39-method native preprocessing selections and the actual
hosted source-plan/link checks pass. Independent evidence covers 82 methods
without skips and seven identical static product pairs. Both full 129-method
graph suites pass after nine measured inventory/count locks are updated. The
combined FAT16/native ISO stale-source and negative-gate selection also passes;
independent evidence rereads 259 methods with no skips in
`iso-graph-regressions-paired-independent.json`. Linux's first completed
qualification hits a frontend
compile timeout; an exact isolated compile passes within the same bound and
reproduces its prepared object. The persistent control's third qualification
also times out at the stage-two frontend after 360 seconds. Independent
rereading preserves all 87 source copies and eighteen completed objects, each
equal to its prepared counterpart. The source root and boot identity remain
present. This is a closed failure with no Linux behavior acceptance; the cause
is unproven. Retry four completes stages two and three, then times out on the
stage-four frontend under the same 360-second bound. Independent rereading
checks 87 source copies, all 74 completed stage-two/three artifacts and eighteen
stage-four objects against preparation. Its 1,601.058548812-second closed failure
has no behavior acceptance. The persistent root and boot identity remain intact.
The timeout cause remains unproven; evidence is
`iso-linux-qualification-retry4-independent.json`.

The normal `test_iso/hello.iso` recipe uses checked CupidObj `iso-fixture` as
its byte author. Python still captures the exact fixture tree, loads its files,
runs the independent renderer, rechecks live inputs and publishes under an
output lock. ADRs 0191, 0239 and 0241 define the complete current format and
transaction. The native handoff must retain those responsibilities and the
full fixture; replacing the author or reducing its contents is unnecessary.

`toolchain/cupidbuild_iso.cc` validates immutable manifest and typed inventory
views through `cupidbuild_iso_inventory_validate`. It uses the existing
CupidObj request layout while checking membership independently of the producer.
It accepts arbitrary entry and manifest order, LF/CRLF and a missing final
newline. It requires exact case, one occurrence of every path, represented
directory parents, matching file/directory source views, portable ASCII names,
127-byte components and the full 512-entry limit. Eight directory levels include
the implicit root. Empty files and empty directories remain valid.

The result counts directories including the root, files, maximum directory depth
and a 64-bit sum of file sizes. The module reads no file payloads and performs no
allocation, filesystem access or tool launch. The caller must capture and retain
file bytes, types and identities. This validation establishes neither release
authority nor a complete ISO image check. It precedes the independent native
layout/byte check described below.

Both host suites pass fourteen methods without skips. The active fixture test
runs the existing checked CupidObj author and requires byte equality with the
independent Python rendering. Other cases cover reordered views, empty inputs,
full capacity, directory depth, names, parent relationships, malformed manifest
membership, request/source errors, bounded diagnostics, recovery and concurrent
calls. CupidC compiles the validator and its contract on both hosts; CupidASM
and CupidLD produce four identical artifact pairs. The 25,148-byte Linux ELF
contract returns zero both natively and through WSL after native Windows
construction. This ELF check does not establish native Windows PE execution.

Independent evidence is `inventory-paired-independent-v2.json` in
`cupid-native-iso-proof-20261005`. Its paired receipts reread the source, logs,
objects, linked image and unchanged checked seed pair. The first compiler
invocation incorrectly mixed logical input with physical output under `--root`;
both rejected receipts remain recorded. Corrected logical paths compile and run.
The first host-suite attempt also exposed a ctypes pointer alias in a mutation
test; retaining the source view fixes that test's restoration.

The host observer now provides `cupidbuild_host_observer_kind`. It opens the
leaf without following links, obtains its kind from the retained handle, and
keeps that kind and metadata for later identity and parent-binding rechecks.
Ancestors still require directories. Empty logical paths select the repository
root. Failure clears the kind result and poisons the lifetime. POSIX discovery
uses a nonblocking open before rejecting special files. Windows rejects reparse
points and shares neither deletion nor replacement. This operation reads no
payload or directory membership; those remain separate explicit observations.

The complete 74-method observer selection passes with native and Cupid-built
callers on each platform: 296 selected, 280 executed and sixteen expected
platform/runtime skips. The fifteen new methods cover ordinary and empty
kinds, UTF-8 paths, invalid arguments and paths, links/junctions/FIFOs, typed API
rejection, metadata and binding drift, and explicit payload/membership checks
after kind discovery, full flat/maximum-depth trees and rejection of a changed
ancestor before recapture. All three checked callers execute as native ELF32 on Linux
and native PE32 on Windows. Their current host adapter is compiled by checked
CupidC; the links use checked CupidASM/CupidLD and the existing hosted runtime.
Independent evidence is `observer-kind-paired-independent-v2.json` beside the
inventory evidence. It rereads source, command receipts, all six callers, suite
logs and unchanged installed seeds. The first Windows kind test exposed native
CRT argument encoding; the established ASCII hex transport fixes the harness.
Two external checked-controller validation errors are retained separately:
missing expected entry and using a no-import PE profile for an imported image.
Neither required an adapter, compiler, linker or runtime change.

The first complete deep-tree probe exhausted handles at file row 252 on both
hosts, while a flat 512-file capture passed. Repeated ancestor handles consumed
the budget. Healthy walks now reuse an already retained directory ancestor
only after checking its original handle and fresh parent binding. Explicit
leaves remain separate observations, and poisoned batches keep their original
independent captures. Relinking the exact probe object with only the corrected
host object changed makes both full layouts pass on both hosts. The original
red callers, fixtures and receipts remain retained. The 4,096-handle limit stays
unchanged; payload and membership capture remain explicit.

`toolchain/cupidbuild_iso_image.cc` now validates a complete captured image through
`cupidbuild_iso_image_validate`. It derives identifier allocation, breadth-first
directory numbering, file extent order and all deterministic records independently
of CupidObj. Every byte must match: system area, primary descriptor, terminator,
both path tables, directories, Rock Ridge continuation, captured files and zero
padding. It uses bounded arena nodes and index arrays, rewinds them on every exit,
and never allocates a second full image. Whole-image sizes must fit the existing
32-bit source view; overflow fails before reading payloads.

Seventeen methods pass with native and checked CupidC callers on each host:
68 selections, 68 executed, no skips. Checked callers run as native Windows PE32
and Linux ELF32, and four compiled object pairs are identical. The active and
collision fixtures match both checked CupidObj and the Python oracle. Cases
include full 512-file and 512-directory inventories, maximum depth/name length,
reordered requests, empty members, block boundaries, corruption in every format
region, extra/truncated bytes, overflow, bounded diagnostics, arena restoration
and concurrent jobs. Checked callers also test null arguments, preserve an arena
prefix and repeat successful or failed checks.

`iso-image-paired-independent-v2.json` rereads terminal results, sources, artifacts,
formats, suite logs and unchanged seeds. The original missing arena-alignment
arguments, fixture-oracle mistakes, isolated-header omission and test-adapter
entry-limit rejection remain recorded. See ADR 0422 for the contract and limits.

The package-style test invocation initially failed on an unqualified sibling
import. The qualified import passes both package and discovery invocations on
both hosts. Compiled sources and callers remain byte-identical and are rehashed
before reuse; the corrected paired matrix still executes 68 methods without skips.

The 157,059-byte manual passes an isolated installed-seed OS acceptance. Its
SHA-256 is `498b9f814ba24ec0c4cc94c07c75c8b05bd940c57d1efac4cc54b89d876d2069`.
The control retains the exact accepted 1,589-file source baseline and unchanged
compiler/seed inputs, replacing only the manual. Its incremental prediction has
zero ordinary source compilations and two generated-source compilations.
The kernel and image commands pass with all seven ordinary tool variables
forbidden. The old size policy rejects the three changed kernel rows; the measured
values are 9,589,844 raw, 9,818,556 final ELF and 9,687,484 pass-one ELF bytes.
The proposal records only those row changes.

The strict private runtime passes in 60.690 seconds with 512 MiB guest RAM, four
max CPUs, e1000, command completion and SMP checks. It uses the previously
qualified 64 MiB host TCG cache. Independent evidence under
`cupid-native-iso-manual-retry2-20261005` rereads all 429 objects and sixteen
artifacts, finds only the manual wrapper changed, confirms one exact manual copy
in every kernel artifact and unchanged FAT data from sector 20,480. The 200 MiB
image has SHA-256
`209f3248b3fedc9295eca82f4fa6b419be9c17f97d379eb966c41aaba9265d0f`.
The initial external preparation incorrectly reused the SDK proposal's
1,591-file assumption and then its four-generated-source prediction. The actual
accepted repository has 1,589 tracked files and this manual predicts two generated
compilations. Corrected preparation preserves accepted bytes and timestamps.

`toolchain/cupidbuild_iso_capture.cc` now owns an immutable manifest and complete
typed payload inventory while borrowing the caller's retained observer. It
discovers all actual kinds before validating the graph, then captures file bytes
and the exact membership of the root and every directory, including empty ones.
An unchanged check precedes success. Closing the capture never closes its
observer. Failed construction frees partial storage and returns a null result.

The full 512-entry layouts pass with both native and checked callers: flat files,
flat directories and seven directories plus 505 files at maximum depth. All
twenty methods pass per host/caller selection, with 76 executions and four
expected platform skips across the paired matrix. Checked callers run as native
PE32 and ELF32; their new capture and caller objects form two identical pairs.
The active fixture passes checked CupidObj, the Python renderer and the native
independent image checker through these captured views. Same-size/restored-time
file and manifest edits, membership drift and changed bindings cannot authorize
publication, while the retained byte digest remains unchanged.

`iso-capture-paired-independent.json` rereads 40 captured source/header/test inputs
per host, closed commands, images, reused support, object pairs, suite logs and
unchanged seeds. Initial fixture-name and root-metadata expectations were harness
errors; the original failing suites remain retained. The capture performs no
namespace writes and enforces the existing 64 MiB observer payload limit before
copying an oversized file. It accepts UTF-8 host paths while preserving the
portable ISO names. Capture has no output-alias or publication authority.
ADR 0423 records its ownership and borrowed-observer lifetime.

The current 157,552-byte manual has separate fresh acceptance under
`cupid-native-iso-capture-manual-20261005`. Its SHA-256 is
`060103c18cf7d37364e6eec1748acfe2a885daaa54d65cc95abd951bafdaf92f`.
The exact 1,589-file installed-seed control replaces only the manual, predicts
zero ordinary and two generated compilations, and passes kernel Make in 571.287
seconds. The old policy correctly rejects the changed kernel sizes. After
measuring only those three rows, image Make passes in 9.246 seconds and strict
private runtime passes in 67.885 seconds with the qualified 64 MiB host TCG cache.
All seven ordinary tool variables remain forbidden in both Make commands.

Independent verification checks every source control, all 429 relocatable objects
and sixteen artifacts. Only the manual wrapper differs; every kernel artifact
contains one exact manual copy, and FAT data from sector 20,480 stays intact.
The raw kernel is 9,590,340 bytes, the final ELF 9,818,556 and pass-one ELF
9,687,484. The 200 MiB image has SHA-256
`6670ba51aaa9c062f04d463bfbae67612038efe4efedd0bf156670429b01465c`.
The preceding 157,059-byte manual receipt remains specific to its original bytes.

Remaining work:

- Use the qualified complete request bundle in the guarded transaction. Its
  transaction now admits 528 retained files, including the manifest and cohort
  beside all 512 file inputs. Integrate that tested capacity with the complete
  inventory contract; ADR 0427 records its separate caller qualification.
- Use the qualified observer-binding API through every publication boundary and
  reject output/input aliases. It accounts for publication's root namespace
  changes while retaining captured memberships and descendant metadata.
  The complete transaction must retain manifest, tool and output observations
  within their lifetimes and preserve the full request boundary during launch.
- Run frozen checked CupidObj first, then pass its candidate and captured inputs
  to the independent native image checker.
- Reuse CupidBuild's output-parent, owner-lock, candidate, drift and recovery
  rules through final publication. Equal images must retain their timestamp.
- Carry the operation through both producer matrices and release consumption,
  then qualify real normal ISO regeneration and feature 17 before recipe handoff.
- Update the supported build graph only when the direct native recipe owns that
  production action. The current 447 CupidBuild/five Python counts stay unchanged.

The ISO source capabilities are isolated from the SDK publication and
ABI proposals already under qualification. The validator is not yet linked into
normal tool images; installed seeds do not carry the new kind API. The preceding
manual and capture paragraph have independent kernel/image/runtime acceptance.
The bundle paragraph also has fresh installed-seed acceptance. Commit `13f19582`
adopts the combined ISO capabilities and accepted FAT16 source. The observer
binding has separate caller, rename-race, preprocessing and manual acceptance
above. See ADR 0421 for the
observer contract and its unchanged observation limits.
