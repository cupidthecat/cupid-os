# ADR 0297: Run artifact-size policy through a CupidC contract

## Status

Accepted on 2026-08-15.

## Context

`verify-artifact-sizes` is the last root `all` transform without a Cupid
participant. Host Python pins the policy, the Linux bootstrap manifest, and
nine deterministic artifacts. It validates their paths, owners, and exact
sizes before `cupidos.img` can be published.

The check is semantic work that CupidC can own. The filesystem boundary is a
different problem. It needs native no-follow inspection, stable open handles,
and a final drift check while the contract is built and run. Moving those
operations into a new freestanding runtime would add risk without improving
the policy proof.

The Linux manifest supplies the five immutable seed paths and sizes named by
the policy. That provenance role does not require a Linux executable. Earlier
documentation grouped this gate with Windows paths that run the static Linux
seed through WSL, even though the Python-only verifier never launched a seed
tool.

## Decision

Keep filesystem capture and process coordination in Python. Move the policy
decision into `artifact_size_policy_contract.cc`, a strict C11 program built by
the checked Cupid tools for each verification.

Python opens the repository through the existing pinned-path reader. It
captures the raw policy, the raw Linux bootstrap manifest, and metadata for
the nine policy members. It also runs the existing Python policy decoder as an
independent oracle. The contract request starts with `CUPSIZE1` and contains
length-prefixed policy bytes, the logical manifest path, manifest bytes, and
one typed path and size observation per artifact.

The contract parses both JSON documents itself. The policy must use its exact
schema and object keys. The seed manifest must use its expected schema and
provide the required name, file, and size fields for all five tools; extra
bootstrap provenance fields remain valid. The contract also requires safe
sorted unique paths, the fixed boot and kernel owners, the selected manifest
parent, and nine regular files with exact sizes. It rejects malformed JSON,
missing or extra cohort members, duplicate keys relevant to the policy, unsafe
paths, wrong owners, invalid file kinds, truncation, and trailing request
bytes. Before success it rereads the request and emits one canonical JSON
report with the artifact count and total exact bytes.

The runner freezes the contract, hosted declarations, runtime, startup, and
support modules into a private source root. It freezes the host-selected
execution seed separately. Checked CupidC compiles the contract and runtime,
checked CupidASM assembles startup, and checked CupidLD links the executable.
Linux builds a static i386 ELF. Windows builds a native PE32 image with the
reviewed import table and runs it directly, without WSL. The runner validates
every relocatable and final image, rechecks the live five-tool seed after each
command, compares the contract report with the Python oracle, and rechecks all
pinned live inputs before returning. The final check walks every logical path
again from the pinned repository root and compares file identity as well as
size and time. Replacing either a leaf or one of its parent directories cannot
leave the original open descriptor looking valid.

The Make edge names the Linux bootstrap manifest as semantic policy
provenance and `PRODUCTION_SEED_MANIFEST` as the execution seed. The build
audit classifies the transform as `verify_artifact_size_policy` with CupidC,
CupidASM, CupidLD, the Cupid-built contract, and Host Python. No checked seed
is promoted because this change adds a consumer without changing any tool
image.

## Evidence

The public contract test failed first because the strict C11 source did not
exist. The runner tests then failed because its module did not exist. The
three suites now contain 38 tests. A Windows run skips the two POSIX-only
replacement cases. Together, the tests cover deterministic reports, policy and
manifest shape, path and owner rules, missing and extra artifacts, wrong sizes
and file kinds, malformed requests, truncation, trailing bytes, oracle
disagreement, live drift, strict report field types, and CLI behavior. The leaf
and parent replacement cases pass under Linux; Windows denies replacement
while its pinned handle is open.

The checked Windows seed compiled the 1,350-line contract into a 55,612-byte
i386 relocatable. A full private checked build assembled startup, compiled the
Windows runtime, linked a PE32 executable, and ran it in 12.4 seconds.

The required documentation update changed the embedded manual. The first
poisoned-host normal build reached the new gate in 695.8 seconds and rejected a
436-byte increase in `kernel.bin`; image publication did not start. After that
one reviewed policy row moved, the complete poisoned-host build passed in
693.5 seconds. The contract and Python oracle both reported nine artifacts
totaling 31,980,840 bytes.

The first audit run rejected the new Make edge as an unclassified Cupid
delivery transform. Adding the exact operation, tool set, and source closure
made the focused audit tests pass. Review caught a closure check limited to
three C sources. Two red drift cases now bind the complete 35-input
transform closure, including all 18 build inputs, startup assembly, hosted
headers, Python support, policy data, artifacts, and both checked seeds.
Regeneration now records 738 active language inputs and 452 transforms. All
443 root transforms have a Cupid participant.
Across all supported roots, CupidC participates in 248 transforms, CupidASM in
seven, CupidLD in seven, CupidObj in 192, and CupidDis in six. Host Python
still participates in all 452 transforms.

## Rejected alternatives

Leaving the decision entirely in Python was rejected because the checked
CupidC seed already supports the required parsing and integer work. It would
also leave the sole root transform without a Cupid participant.

Adding this operation to CupidObj was deferred. That would change a production
tool, require Linux and Windows seed promotion, and force the full fixed-point
cohort for a policy that fits a small standalone contract.

Reading the live files from the contract was rejected. The hosted runtimes do
not provide the pinned, no-follow repository walk needed to preserve the
existing publication boundary on both hosts.

Using the Linux seed through WSL on Windows was rejected because the checked
native execution seed can build and run the same contract directly. The Linux
manifest remains policy provenance, not an execution requirement.

## Consequences

Every root `all` transform now has a Cupid participant. The artifact-size
decision is checked twice from one frozen request by independent CupidC and
Python implementations. A disagreement, producer failure, malformed output,
or input drift still blocks image publication.

This does not make the build Python-free. Python still owns native path safety,
snapshot creation, private staging, process launch, the independent oracle,
and final drift checks. WSL remains required on Windows for Linux fixed-point
reconstruction and the complete Linux Toolchain contract cohort, but not for
artifact-size verification.

The active source is already named `.cc`. The residual `.c` census is unchanged
and its safe rename set remains empty. `TempleOS/` remains untouched reference
material and stays outside all progress counts.
