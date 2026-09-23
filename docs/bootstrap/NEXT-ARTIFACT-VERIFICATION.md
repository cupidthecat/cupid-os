# Next native artifact-size verification boundary

Boundary audit, 2026-09-20; isolated extraction work, 2026-09-21. Production verification remains Python-coordinated.

## Current operation

`Makefile` runs `tools/artifact_size_contract.py verify` with five paths: repository root, size policy, Linux policy manifest, checked Windows manifest, and host-selected execution manifest. The policy currently contains sixteen artifacts: boot, three kernel images, six Linux seed tools, and six Windows seed tools. The Linux manifest supplies the six variable seed paths and immutable sizes; the Windows paths are fixed. This is a paired-seed check on both hosts, even though only one platform's tools execute.

The wrapper pins the repository root and no-follow paths through `_PinnedRepository`. It captures the complete checked Windows seed directory and validates it through `verify_seed_inputs`. Linux separately captures and validates its execution seed; Windows requires execution to use the same checked Windows manifest. Seed captures require exactly manifest.json plus six tool files. The wrapper captures policy and Linux manifest bytes, observes all sixteen regular files, and rejects all size mismatches together.

It then captures nineteen build inputs: Makefile, ten hosted headers, both runtimes and startup files, the C contract, and three Python support modules. Checked CupidC compiles contract and runtime; CupidASM assembles startup; CupidLD links the host-format executable. Every relocatable and final ELF/PE32 image is validated. The execution seed is rechecked after every command. The private contract checks a CUPSIZE2 request and emits canonical JSON. Python compares that report with its independent policy oracle, rechecks captured seed/build bytes, reopens every observed leaf from the retained root, checks recorded directory membership and metadata, and rechecks the named repository root before printing success.

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
reread its bytes. This is a proposed integration boundary, not an implemented
file or a new signing system. No release check should be removed while that
boundary is unfinished.


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
