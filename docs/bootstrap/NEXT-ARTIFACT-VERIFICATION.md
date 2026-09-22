# Next native artifact-size verification boundary

Read-only audit, 2026-09-20. No implementation, policy change, or ownership handoff is included.

## Current operation

`Makefile` runs `tools/artifact_size_contract.py verify` with five paths: repository root, size policy, Linux policy manifest, checked Windows manifest, and host-selected execution manifest. The policy currently contains sixteen artifacts: boot, three kernel images, six Linux seed tools, and six Windows seed tools. The Linux manifest supplies the six variable seed paths and immutable sizes; the Windows paths are fixed. This is a paired-seed check on both hosts, even though only one platform's tools execute.

The wrapper pins the repository root and no-follow paths through `_PinnedRepository`. It captures the complete checked Windows seed directory and validates it through `verify_seed_inputs`. Linux separately captures and validates its execution seed; Windows requires execution to use the same checked Windows manifest. Seed captures require exactly manifest.json plus six tool files. The wrapper captures policy and Linux manifest bytes, observes all sixteen regular files, and rejects all size mismatches together.

It then captures nineteen build inputs: Makefile, ten hosted headers, both runtimes and startup files, the C contract, and three Python support modules. Checked CupidC compiles contract and runtime; CupidASM assembles startup; CupidLD links the host-format executable. Every relocatable and final ELF/PE32 image is validated. The execution seed is rechecked after every command. The private contract checks a CUPSIZE2 request and emits canonical JSON. Python compares that report with its independent policy oracle, rechecks captured seed/build bytes, reopens every observed leaf from the retained root, checks recorded directory membership and metadata, and rechecks the named repository root before printing success.

The public success output is exactly `Cupid artifact sizes: ok (16 exact artifacts)` followed by a newline. Errors use `artifact size verification failed:` and retain the multiline list of size mismatches. The inner JSON contains only artifact_count, schema, and total_exact_bytes, with sorted compact keys and one newline. There is no persistent report file or publication candidate. The gate must succeed before disk-image publication.

## Reusable C policy core

`toolchain/tests/artifact_size_policy_contract.cc` already contains the semantic verifier. Its request parser and `validate_request` cover raw policy JSON, Linux manifest JSON and its observed digest, Windows manifest JSON, six Windows size/hash observations, and sixteen regular-file size observations. The parser checks schemas, owners, sorted unique safe paths, exact cohorts, positive integer limits, UTF-8/JSON validity, truncation, trailing bytes, paired source identity/counts, and the admitted parent windows. Windows tool sizes and hashes must agree with the manifest. The observation producer, not this parser, computes filesystem digests and proves path identity.

The smallest useful source step is to extract this parser/validator into a reusable module with an explicit result and error buffer. Keep its existing executable adapter and CUPSIZE2 tests. Avoid including a test `.cc` directly into CupidBuild or exposing static global `contract_error` as a shared API. The contract file rereads its request before success; an in-memory API should receive owned immutable bytes and document who proves their lifetime. The current native seed reader is reusable for execution-seed trust, but its platform selection and parent-window policy cannot by themselves prove the opposite platform's manifest or the pair relationship.

## Shared parser consumer

The Toolchain manifest contract includes `artifact_size_policy_contract.cc`
with `main` renamed. It uses the included JSON and binary readers, text and
path helpers, seed-name lookup, request-file handling, and global error state.
It also depends on the reader types and seed constants defined by that file.
Replacing the included file with a thin artifact-policy adapter would therefore
break the manifest verifier and author even if every artifact-policy test passed.

Preserve both consumers when extracting the core. Give shared parsing helpers
an internal module and explicit per-call error state; keep request-file I/O in
the executable adapters. The public artifact-policy API should remain a single
immutable-byte validation call with an explicit result and bounded diagnostic
buffer. Do not retain duplicated policy implementations or include a test `.cc`
from CupidBuild to bypass this dependency.

The integration gate must cover the Toolchain manifest verifier and author as
well as artifact verification, including exact reports, malformed requests,
failure recovery, and checked Cupid-built executables. Their runner closures,
Make prerequisites, manifest input inventories, and audit contracts must all
include the extracted modules. Host-compiled API tests alone cannot establish
that this extraction is ready for production.

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

The native reader also selects the schema, build-plan parsing, and ELF/PE32
execution profile at compile time. An artifact verifier running on Linux must
still validate all six checked Windows images. A Windows verifier must parse
the selected Linux manifest and its policy bindings. Extract validators over
captured bytes with an explicit format/role argument; keep the current
execution entry point bound to its own host. Tests must reject the wrong
entry point, dynamic or writable-executable ELF segments, malformed PE32,
wrong import libraries or procedures, and role-specific import mismatches.

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
