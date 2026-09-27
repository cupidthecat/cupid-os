# Next native artifact-size verification boundary

Native `verify-artifact-sizes` passes source integration acceptance. It checks
the paired release and sixteen artifacts through retained read-only observations.
The plans contain 76 producer inputs and 88 publication inputs. Staged proofs
match 41 Windows and 34 Linux outputs; full publication matches 67 stage pairs
and verifies 22 artifacts. Hosted acceptance and both private four-CPU
max/e1000 disassembly, shell and SMP smokes pass. Independent paired verification
checks 1,533 source inputs, sixteen artifacts and 431 link inputs per host;
images and all three user programs match. ADR 0408 records the boundary and limits.

The installed Linux and Windows seed pair comes from accepted source
`ec896462586597893dd197697ee3b68ce2c8e69b` and its 76-input snapshot. Both
promoted-seed self-bootstrap proofs reproduce all twelve installed tools,
with 41 Windows and 34 Linux stage-three/four artifacts matching. All 28
converged Windows Unicode-path commands pass. Promotion regressions cover
345 unique methods per host, with platform skips recorded separately; checked
callers pass 4,901 reader cases and 51 policy methods per host.

Full publication verifies 22 artifacts, 88 publication inputs and 67 stage
pairs. Hosted acceptance passes three commands and checks seven resulting
i386 executables. Independent paired OS acceptance rehashes 1,533 sources,
sixteen artifacts and 431 link inputs per host. Images and all three user
programs match; both private four-CPU max/e1000 disassembly, shell and SMP
smokes pass without changing their source images. The image SHA-256 is
`60743ecd68d29b13b7e63090f272f73cfffd14fe01772d273b10288a58b633d0`.

The promoted CupidBuild carries native `verify-artifact-sizes`. Production
still invokes the Python verifier, so ownership remains 440 CupidBuild and
twelve Python actions. Switching that Make edge and its dependency audit is
the next implementation step. ADR 0408 describes the native operation.

Boundary audit, 2026-09-20; isolated extraction work, 2026-09-21; private native
integration rebased on the UTF-8 tools, 2026-09-26. Production verification
remains Python-coordinated.

## Progress through the retained observer

ADR 0400 completes the shared manifest and paired-release byte readers. The
UTF-8 generation has 73 bootstrap inputs and 87 publication inputs. The
59/61-input discussion below records the earlier extraction; it does not
describe the installed tools. See [the current seed identities](README.md)
and [the UTF-8 boundary](NEXT-WINDOWS-UTF8.md).

ADR 0401 adds the read-only retained observer described below. Its 36-method
suite passes with native and Cupid-built callers on both hosts, and both
candidate staged proofs pass independent verification. Contract publication and
paired final OS/runtime acceptance also pass. See
[observer evidence and limits](NEXT-NATIVE-OBSERVER.md).

ADR 0402 adds the tracked release record and its Python migration check.
The production runner captures `bootstrap/seeds/release.json`, requires exact
semantic agreement with the existing pins before contract execution, and
rechecks its bytes afterward. The 26-input closure includes the record, helper
and Python package marker. Both hosts pass 104 related tests and real checked
verification of all sixteen artifacts. Final image/user builds and four-CPU
boot/disassembly/shell smokes pass on both hosts with matching, preserved images. The native `verify-artifact-sizes` command must still
combine the byte validators and observer into one transaction. The twelve
Python-coordinated operations remain.

To author a new review candidate from the independently pinned installed pair:

```sh
python -m tools.seed_release_identity --root . --output release-candidate.json
```

The destination must not exist. The command validates both promoted cohorts,
the exact Windows-to-Linux manifest binding and live seed bytes. It neither
promotes tools nor overwrites the reviewed release. A future seed promotion
must update and review the record alongside the independently verified pins.

## Current operation

`Makefile` runs `tools/artifact_size_contract.py verify` with five paths: repository root, size policy, Linux policy manifest, checked Windows manifest, and host-selected execution manifest. The policy currently contains sixteen artifacts: boot, three kernel images, six Linux seed tools, and six Windows seed tools. The Linux manifest supplies the six variable seed paths and immutable sizes; the Windows paths are fixed. This is a paired-seed check on both hosts, even though only one platform's tools execute.

The wrapper pins the repository root and no-follow paths through `_PinnedRepository`. It captures the complete checked Windows seed directory and validates it through `verify_seed_inputs`. Linux separately captures and validates its execution seed; Windows requires execution to use the same checked Windows manifest. Seed captures require exactly manifest.json plus six tool files. The wrapper captures policy and Linux manifest bytes, observes all sixteen regular files, and rejects all size mismatches together.

It then captures 26 build inputs: Makefile, the reviewed release record, shared policy and parser sources and headers, ten hosted headers, both runtimes and startup files, the C contract, four Python support modules and the Python package marker. Checked CupidC compiles contract and runtime; CupidASM assembles startup; CupidLD links the host-format executable. Every relocatable and final ELF/PE32 image is validated. The execution seed is rechecked after every command. The private contract checks a CUPSIZE2 request and emits canonical JSON. Python compares that report with its independent policy oracle, rechecks captured seed/build bytes, reopens every observed leaf from the retained root, checks recorded directory membership and metadata, and rechecks the named repository root before printing success.

The public success output is exactly `Cupid artifact sizes: ok (16 exact artifacts)` followed by a newline. Errors use `artifact size verification failed:` and retain the multiline list of size mismatches. The inner JSON contains only artifact_count, schema, and total_exact_bytes, with sorted compact keys and one newline. There is no persistent report file or publication candidate. The gate must succeed before disk-image publication.

## Reusable C policy core

`toolchain/tests/artifact_size_policy_contract.cc` already contains the semantic verifier. Its request parser and `validate_request` cover raw policy JSON, Linux manifest JSON and its observed digest, Windows manifest JSON, six Windows size/hash observations, and sixteen regular-file size observations. The parser checks schemas, owners, sorted unique safe paths, exact cohorts, positive integer limits, UTF-8/JSON validity, truncation, trailing bytes, paired source identity/counts, and the admitted parent windows. Windows tool sizes and hashes must agree with the manifest. The observation producer, not this parser, computes filesystem digests and proves path identity.

The smallest useful source step is to extract this parser/validator into a reusable module with an explicit result and error buffer. Keep its existing executable adapter and CUPSIZE2 tests. Avoid including a test `.cc` directly into CupidBuild or exposing static global `contract_error` as a shared API. The contract file rereads its request before success; an in-memory API should receive owned immutable bytes and document who proves their lifetime. The current native seed reader is reusable for execution-seed trust, but its platform selection and parent-window policy cannot by themselves prove the opposite platform's manifest or the pair relationship.

## Native transaction required

A native `verify-artifact-sizes` operation needs a read-only retained-root transaction. It can reuse SHA256, no-follow file walking, retained directory identity, seed capture, and live/frozen rechecks from CupidBuild. The existing public host API exposes freeze/read/discovery and output transactions, but no dedicated metadata-only observation plus fresh-root rewalk contract matching `_PinnedRepository`. Copying all policy artifacts into a publication transaction is not the same boundary. Ordinary output files currently contribute stable metadata, not payload digests; changing that choice should be explicit and tested.

The read-only host boundary must retain root and parent identities, reject symlinks/reparse points and nonregular leaves, compare open-descriptor metadata with freshly resolved names, preserve exact seed-directory membership, and recheck policy/manifest/seed payload bytes even when size and timestamps are restored. It must collect every artifact-size mismatch before returning. Unrelated normal-build writers are currently ordered before the gate because directory membership is captured. The order-only ISO edge must remain; a more permissive directory policy would be a separate behavior change.

Two implementation routes are honest, but differ materially:

1. Preserve the private checked contract build and native-coordinate all capture, compilation, assembly, linking, execution, report validation, and final rechecks. This retains current production participation and nineteen-input closure, but is a substantial command pipeline, not a renamed Python invocation.
2. Link the extracted policy core into checked CupidBuild. This removes per-call private compilation only after the core belongs to source snapshots, both staged proofs and seed promotion. Python's independent oracle then remains a test oracle rather than a production dependency. This changes production participants and the verification closure, so Make, audit ownership, docs, and acceptance tests must change together.

The second route appears smaller long-term; extraction plus compatibility tests is the first coherent step. Neither route should accept arbitrary policy rows, execution tools, imports, or an unchecked external request file as a shortcut. Preserve selected-manifest directory behavior or document and test any deliberate restriction before handoff.

## Release identities and opposite-host formats

The current seed readers enforce different contracts. Python's
`_verify_seed_manifest_data` pins the promoted source revision, snapshot,
input count, parent identities, plan hashes, and each tool's exact size and
SHA-256 to release constants. It then validates captured executable bytes.
CupidBuild's execution reader admits a staged v2 manifest with a valid source
revision/snapshot shape, an admitted input count and parent generation, the
fixed build plan, and self-consistent artifact rows. That is needed for its
own-generation staged checks, but it is not the same release-pinning rule.
Reusing that reader alone would drop checks from artifact verification.

The execution reader still selects its manifest schema and build-plan parsing
for its own host. The isolated `cupidbuild_validate_seed_image_bytes` API now
accepts an explicit image format and role, making both ELF32 and PE32 validation
available on either host. Its existing transaction caller remains bound to its
own host format. Twelve API tests pass with host and checked Cupid-built callers,
including all twelve current images, wrong entry points, dynamic or writable
executable ELF segments, malformed PE32, and role-specific import failures.
[Seed executable byte profiles](NATIVE-SEED-IMAGE-PROFILES.md) defines that API.
An artifact verifier must still parse both manifests and their policy bindings,
prove release identities, and retain the filesystem observations.

Release pins need an external data boundary when the verifier checks its own
CupidBuild executable. Embedding that executable's finished digest into its
source would make promotion depend on a self-referential hash. A tracked
release-identity file produced by the existing independently checked promotion
step can preserve the repository's current trust model. It would be a required,
captured input, separate from the manifest being validated, with exact schema
and duplicate/missing/unknown-field rejection. Its release fields must agree
with the existing Python constants during migration, and final checks must
reread its bytes. ADR 0402 implements this file and migration check. It does not add signing;
the existing Python release pins remain mandatory. The following probe records
the evidence that preceded adoption.


Keep those pins semantic. A private probe of both current seed cohorts accepts
compact JSON and reordered artifact rows, then rejects a changed CupidBuild
image even when its manifest row carries the changed image's matching digest.
All four probes pass through Python's current `verify_seed_inputs`; evidence
is retained under `build/bootstrap/20260921-release-pin-audit/`. Pinning whole
manifest bytes instead would introduce a new formatting/order restriction.
The existing Windows-to-selected-Linux manifest digest binding remains required.
These probes do not test or implement a native release verifier.

## Tests and adoption evidence

The three existing files contain seventy test methods: fourteen in `test_artifact_size_policy.py`, thirty-six in `test_artifact_size_policy_contract.py`, and twenty in `test_artifact_size_contract_runner.py`. This audit inspected them; it did not rerun them. They cover semantic malformed input, paired provenance, exact inventories, nonregular/linked paths, same-metadata payload drift, atomic leaf/parent/root replacement, report disagreement/types, CLI formatting, and parallel Make ordering including failed ISO and removed-edge regressions.

For a native transaction, reuse the semantic fixtures through both old contract and new API, then compare actual CLI success and failure behavior on Windows and Linux. Add race hooks before validation and before success output for policy, both manifests, each seed cohort, artifact replacement, parent/root replacement, and restored-metadata content changes. Add missing/extra seed files, mixed generations, unrelated checked directories, invalid requests, cleanup, and silence-on-failure checks. If a private build remains, cover tool failure, timeout, malformed object/executable, and invalid/noncanonical contract reports. If it is removed, prove the promoted Cupid-built coordinator executes the extracted core and keep independent oracle differential tests.

Both stage matrices need live native verification cases using each generation's own paired seed inputs, plus audit mutations that cannot pass under dead code. A later ownership change also needs a normal parallel OS build, all sixteen checks, preserved image on verification failure, and kernel/boot smoke as appropriate. The existing audit records this as `verify_artifact_size_policy` with CupidASM, CupidC, the contract, CupidLD, and Host Python; it is not safe to subtract one Python row without accounting for the changed participants and closure.

References: ADR0297, `tools/artifact_size_contract.py`, `tools/artifact_size_policy.py`, `toolchain/tests/artifact_size_policy_contract.cc`, `toolchain/cupidbuild_host.h`, Makefile lines206-246 and1595 onward, and the three test modules above. Current raw-size observations are evidence only; this audit proposes no automatic policy update.


## Isolated extraction and parser integration

The extracted policy API accepts immutable `CUPSIZE2` bytes and returns the
artifact count and total exact bytes. Failure clears the result. Diagnostics
use caller-owned storage with an explicit capacity; zero capacity is supported.
The API retains no input pointer and shares no mutable error state between calls.
It validates supplied observations. Filesystem capture, release pins, live-path
rechecks, and publication ordering remain outside this API.

The manifest contract previously included the artifact contract's implementation
file and used its parser helpers directly. A thin artifact adapter alone broke
that dependency. Both contracts now use an internal parser module with explicit
per-call diagnostics. Their command-line adapters retain their own file I/O.
The artifact runner compiles the adapter, policy, and parser separately; the
manifest verifier and staged manifest author compile and link the parser.
The artifact runner's captured build closure grows from nineteen to twenty-three
files: the policy and parser each add a source and header. The earlier operation
description records the pre-extraction closure.

The source inventories include both new headers: 61 bootstrap inputs and 78
contract inputs. The audit records 405 tracked preprocessing roots and four
generated roots. Exactly two new roots appear: the policy core and shared parser.
The shared strict-profile parser is registered once after both participating
build transformations pass their exact checks. Other duplicate roots remain
errors.

Following ADR 0380, the draft v2 readers accept only the preceding `83d00ce7`
parent pair and active `142a9737` pair. Each digest remains bound to its own
revision, and Windows execution and plan parents must share a generation.
The retired `9d2529` pair is rejected. Current-source counts are exactly 59 or
61; historical v1 parsing is unchanged. Release pins still describe the installed
59-input seed. Source admission does not constitute seed promotion.

Both hosts pass 48 policy/API tests and the targeted CupidBuild provenance tests.
Checked CupidC, CupidASM, and CupidLD build the extracted contracts on both hosts;
the resulting executables pass 42 artifact-policy and 39 manifest semantic tests.
Policy, parser, and adapter objects match across hosts. The full manifest runner
and publisher suites pass on both hosts. Same-size, same-timestamp changes to
each extracted source and header are rejected. API tests also cover bounded
diagnostics, failed-result clearing, recovery, immutable inputs, and concurrent
calls with separate output storage.

The first checked-tool harness retry accidentally selected the older draft; its
output is retained but does not prove this integration. The corrected harness
selects the isolated root explicitly, asserts 59/61 admission, and records the
captured input hashes. A complete staged proof, production build/runtime gates,
and checked-seed promotion remain separate work. No native artifact-verification
command or ownership handoff is claimed by this extraction.


Linked diagnostic CupidBuild executables also pass the six targeted provenance
cases on both hosts (one Windows-only case is skipped on Linux). Each baseline
relink first reproduces the installed executable byte for byte from hash-checked
stage-four objects. Replacing only the CupidBuild object then tests the new
reader through checked Cupid-generated code. These private diagnostics do not
replace installed seed tools and do not substitute for a complete fixed point.

## Private native integration on the UTF-8 generation

The retained native verifier has been merged onto the source captured for the
`72170b06` seed promotion in private Windows and Linux roots. Twelve files carry
the verifier, its CLI, bounded batch observations, aggregate policy diagnostics,
Make integration, and tests. The merge preserves the current wide Windows entry
points, path codec, imports, and conditional-assembly parent checks. Obsolete
runtime and startup changes from the earlier candidate were not restored.

The private command captures the reviewed release record, policy, both
manifests, and all twelve seed payloads. It validates both executable profiles,
collects sixteen artifact observations, applies the policy, and closes the
retained observer before reporting success. It does not launch subprocesses or
write repository files. Production still uses the Python coordinator and its
independent pins; ownership remains 440 CupidBuild operations and twelve Python
operations.

Both hosts pass the private 102-method native suite. Normal Make builds and
separate builds using only the installed CupidC, CupidASM, and CupidLD pass
eight CLI commands per host: success and size rejection under paths with
spaces, accented text, Japanese text, and a supplementary Unicode character.
The checked Windows binary uses the wide entry point and UTF-8 imports.
Independent verification rehashes all 1,530 captured inputs per host, both
executables, every checked object, and the result records. Eight shared objects,
including the verifier and policy, match across hosts. Evidence is retained in
`build/bootstrap/utf8-promotion-72170b06/native-rebase-v1/`.

The checked binaries also pass six existing CLI methods per host. These compare
aggregate size, missing-file, and non-directory diagnostics with Python's
oracle, and check release rejection, invalid arguments, and success. The two
ctypes API methods remain part of the native suite.

A checked caller pauses the verifier after capture and before its final drift
check. Both hosts reject twelve mutations: release, policy, and manifest byte
changes at both pauses, then seed-payload, artifact-size, and seed-membership
changes before final acceptance. Payload mutations retain size and restore the
original modification time. Every rejection leaves stdout empty. Independent
verification binds the copied sources, reused objects, caller, and results;
the caller object matches across hosts.

These results cover the recorded private bytes. Three retained new source files
still have CRLF endings; integration must normalize them and repeat acceptance
on the final bytes. The new modules are not yet in the producer plans, source
and header closures, staged proofs, or installed seeds. The remaining work is to
integrate those closures, retain the race and allocation-failure coverage, prove
the new coordinator through both staged builds, and then change the production
operation with matching audit and OS/runtime evidence.

A read-only closure calculation for those two added source modules gives 76
producer inputs, 88 publication inputs, and 115 distinct inputs across both
sets. The producer adds `artifact_size_policy.cc`, `cupidbuild_artifacts.cc`,
and `cupidbuild_artifacts.h`; the publication set adds the new header. These
are measured proposed counts, not an admitted build profile. Additional
integration fixtures may change them.

A second private candidate integrates those plans and the corresponding reader
and publication inventories. It retains the installed 73-input seed pins,
admits the exact new plan pair and UTF-8 parent tuple, and keeps the existing
wide Windows import profile. Paired verification covers 287 native methods and
4,869 checked reader cases per host, with matching shared objects. Publication
fixtures were corrected to include both modules and the new object counts;
the original failures and targeted replays are retained under
`build/bootstrap/utf8-promotion-72170b06/native-plan-v1/`. This candidate still
needs complete staged, publication, and OS acceptance before active integration
and production handoff.

The 76-input candidate's artifact-policy contract also passes all 51 methods
with checked Cupid tools on each host. Independent verification binds the
captured inputs, test logs, programs, and three matching shared objects.
Both staged builds of this private candidate pass independent paired
verification. Stage three and stage four match across 41 Windows artifacts
and 34 Linux artifacts, with 76 producer inputs on each host. These candidate
proofs are separate from the 73-input seed promotion.

The supplemental harness also passes independent verification: 56 commands
per host exercise both compared generations across four path encodings.
The cases cover success, metadata and payload rejection, size and membership
failures, and recovery. Every call preserved its observed fixture bytes and
membership; verification also rehashed all 1,530 captured files on each host.
The paired-release fixtures contain the actual twelve tool images from each
generation, a synthetic release revision, and bounded ordinary artifact files.
These results do not establish OS acceptance or promote the candidate seeds.

A separate native caller now injects failure at each of the verifier translation
unit's four allocation calls. Both hosts pass twenty failure/recovery pairs,
covering diagnostic capacities of zero, one, two, nine, and 64 bytes. Each failed
call clears its result, bounds its diagnostic, releases tracked allocations, and
allows the following valid call to succeed. The fixture stays unchanged.
Independent verification binds both binaries, the caller, logs, and all 1,530
captured inputs. This covers the verifier's own allocations with host compilers;
it does not claim fault injection throughout the observer or checked-Cupid
execution of this caller.

The canonical candidate's full Toolchain publication build was interrupted.
A recovery build is running in a new Linux checkout containing the same 1,530
captured files; the interrupted checkout and logs are retained. Its controller
will run the checked manifest reader and independently verify the 88 publication
inputs and 76 producer inputs after the build. This is separate from acceptance
of the installed 73-input seed pair; neither publication has been promoted on
the strength of the other's results.

The canonical candidate's retained-input race replay passes on both hosts.
The pause-hook caller was compiled with checked Cupid tools and linked with
verified stage-four objects against the actual paired stage-four fixture.
Each host passed the baseline and twelve mutations covering captured metadata,
seed payloads, ordinary artifact sizes, and directory membership. Independent
verification checked every retained fixture against its intended mutation,
rehashed all 1,530 captured inputs on each host, and confirmed matching caller
objects. These results apply to the canonical 76-input candidate; the earlier
rebase's evidence remains separate.

### Native artifact integration: paired acceptance complete

Windows also passed image construction, user builds, the native artifact check
and the private four-CPU smoke. Independent paired verification checks 1,533
source inputs, sixteen artifacts and 431 link inputs per host. Images and all
three user programs match; both smokes preserve their source image, and the
preceding accepted images remain unchanged. The new 200 MiB image SHA-256 is
`60743ecd68d29b13b7e63090f272f73cfffd14fe01772d273b10288a58b633d0`.

The evidence directory is
`build/bootstrap/utf8-promotion-72170b06/native-plan-v1/`.
`utf8-paired-acceptance-v4.json` binds Windows v3 and the Linux v4 continuation,
including the retained successful Linux image command and old publication.
`final-regression-audit-v2.json` binds the current regression sources and reports.
The 291 distinct methods per host combine three runs; the 26 runner methods,
4,869 checked reader cases, 51 checked policy methods, twelve checked race
mutations, 56 supplemental stage commands and twenty allocation failure/recovery
pairs per host remain separately recorded. Allocation injection uses host
compilers for the verifier translation unit. The graph suite covers 124 unique
methods across the full run and its focused corrected replay; reproducibility
also passes.

The candidate is ready for source integration. Its installed-seed promotion and
production recipe handoff still require their own acceptance. This change does
not claim Doom gameplay acceptance or completion of the remaining Python work.

## Promoted verifier identities and evidence

The 76-input producer snapshot is
`0bf11fcbef634c80716cbe178434a6060a04bf5bb7d07f9ccb03d5248a0c173c`.
The Linux manifest SHA-256 is
`dabdc048ce54c7434fd9edd602f0531ead60f425db39bc2550332d7c69d30608`;
the Windows manifest SHA-256 is
`25290a99f9de273890cd98130e8ad7df5e9ffcd89010be8e285a06a380e1eaf2`.
Both name the accepted `72170b06` pair as their parent. The Linux and Windows
plan hashes are `9e16b501a87c06ba6ae45d50a349dc96a03294e2ddd6769c57ec42a79eac08e5`
and `6aba99be40f915aa2adcb92ecb8341bef6f4a8a290e275fe47823ad380bd3748`.

Evidence under `build/bootstrap/artifact-promotion-ec896462/` includes
`native-paired-self-proof-verification-v1.json`,
`paired-promoted-regressions-v1.json`, `paired-checked-cohorts-v1.json`,
`converged-unicode-binding-v1.json`, `linux-promoted-publication-v1.json`,
`linux-promoted-hosted-verification-v1.json` and `paired-os-acceptance-v1.json`.
The installed verifier also passes 28 supplemental mutation/recovery cases per
host across four pathname categories using the actual paired seed payloads.
The bootstrap log retains the failed profile, stale fixture, stale publication
pin, shared-root scheduling and copied Linux execute-permission attempts.

The next production change must replace the private contract build closure
with the native command's runtime inputs, update ownership classification and
strict graph tests, and retain the ISO-before-verification ordering. A command
spelling change alone does not establish native production ownership.
