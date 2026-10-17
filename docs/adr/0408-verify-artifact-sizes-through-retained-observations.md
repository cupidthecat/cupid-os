# ADR 0408: Verify artifact sizes through retained observations

Date: 2026-09-26

Status: Accepted for source integration, seed promotion and production handoff

CupidBuild now combines the shared release and manifest readers, artifact policy,
and retained filesystem observer in one read-only `verify-artifact-sizes`
operation. It launches no child tools and creates no files or locks. Keeping
original handles through the final drift check preserves the Python verifier's
observation boundary without using a publication transaction to copy artifacts.

The reviewed release record supplies the expected paired identities. The command
captures that record, the policy, both manifests and all twelve tool payloads;
it checks exact seed membership, byte identities, executable profiles and pair
binding. It observes the sixteen policy artifacts, reports ordinary-file errors
in policy order, rechecks retained observations, and closes them before success.
Unavailable observations are diagnostic data only: a poisoned observer cannot
succeed. Failures clear the result and bound the caller's diagnostic.

These are sequential drift checks, not an atomic filesystem snapshot. Ordinary
artifacts retain metadata checks; seed and metadata payloads receive hash checks.
The release record remains reviewed repository data rather than a signature.
The installed 76-input seeds carry this operation. The Python production
operation remains in place until the Make edge and dependency audit transfer
ownership together.

The new producer plans include the verifier, policy module and header: 76 source
inputs, 88 publication inputs and a 115-file union. Exact plan/count profiles and
the complete UTF-8 parent tuple are admitted together; mixed generations fail.
The canonical candidate matches 41 Windows and 34 Linux stage-three/four outputs.
Its checked supplemental and retained-input race tests pass on both hosts.
Repository tests now retain allocation failure/recovery and twelve deterministic
race mutations. Allocation injection covers the verifier translation unit with
host compilers; checked-Cupid race evidence is recorded separately. Full
publication and hosted acceptance pass. Both hosts pass artifact, user-program
and private four-CPU runtime checks. Independent paired acceptance verifies
1,533 source inputs, sixteen artifacts and 431 link inputs per host, with
matching images and user programs.

Promotion acceptance, 2026-09-27: the pair from `ec896462` reproduces all twelve
installed payloads in fresh self-bootstrap proofs. Historical test fixtures now
name their own plans and parents explicitly, so promotion cannot turn a negative
mutation into a no-op. The checked publication reader pins both the new Linux
manifest and its build plan. Both hosts pass regression, checked-reader, policy,
real-artifact and private boot acceptance; their images and user programs match.
The retained evidence is under `build/bootstrap/artifact-promotion-ec896462/`.

Source extension, 2026-09-27: the Python runner also retains an independently
selected Linux execution cohort. The native handoff must preserve that check.
A separate selected-seed entry point keeps the original request layout and API
intact. Paired CLI options identify the checked Windows and execution manifests.
Windows keeps its canonical execution constraint; Linux permits a separate copy
of the reviewed cohort. All observations share the verifier's existing lifetime.
The platform adapter supplies the execution format because the shared module is
compiled without platform defines. The two native readers also admit the exact
current release as a parent, with mixed identities still rejected.

Checked linked commands, retained-input races and parent-reader tests pass on
both hosts. Fresh staged proofs converge on both hosts, and Linux publication
passes independent verification. Paired image/runtime acceptance passes with
matching kernels, images and user programs. Both private four-CPU max/e1000
smokes pass disassembly, shell completion and SMP checks. All three hosted
contracts pass independent verification. Seed promotion remains before the
production handoff; production keeps the Python verifier meanwhile.
See [selected-seed evidence and limits](../bootstrap/NATIVE-ARTIFACT-SELECTION.md).

Selected-seed promotion acceptance, 2026-09-27: the pair from `5ba6ea24` now
carries both selection options. Fresh named-commit and promoted self-bootstrap
proofs converge; the latter reproduce all twelve installed tools. Installed
commands pass canonical and alternate-selection checks, useful negative cases
and recovery across ASCII, accented, Japanese and emoji paths. Checked readers, policy tests,
publication and hosted contracts pass. Independent paired OS acceptance verifies
1,534 sources, sixteen artifacts and 431 linker inputs per host, with matching
images and user programs. Both private four-CPU smokes pass disassembly, shell
completion and SMP checks. Evidence is under
`build/bootstrap/artifact-promotion-5ba6ea24/`. Production still uses the Python
verifier until the Make recipe, selection arguments and dependency audit change
together and pass fresh acceptance.

Production handoff acceptance, 2026-09-29:

Production artifact verification now runs directly through the promoted
CupidBuild command. Make passes both manifest selection options and waits for
ISO publication before the verifier observes the repository. The audited graph
has 441 CupidBuild actions and eleven Python actions across 452 transforms.
Python still coordinates three user compilations, three user links, two image
operations, two verification operations and hosted contract publication.

The corrected hosted inventory contains 412 active preprocessor cases. Fresh
publication verifies 22 artifacts and 67 stage pairs; all three hosted contracts
pass, including seven linked i386 executables. Independent paired OS verification
checks 1,534 captured sources, sixteen artifacts and 431 linker inputs per host.
Windows and Linux images and all three user programs match. Both private
four-CPU max/e1000 smokes pass disassembly, shell completion and SMP checks
without changing their source images. The kernel is 9,571,268 bytes; the image
SHA-256 is `ef8b033647fd7d68ca4dbb54c01f500ce3c9b4efaf12c488b24cbf8b78254a23`.

The retained reports are under
`build/bootstrap/native-artifact-handoff-os-v2/`: `paired-os-acceptance-v3.json`,
`linux-promoted-hosted-verification-v3.json`,
`independent-publication-files-v3.json`, `graph-verification-v3.json` and
`paired-policy-regressions-v1.json`. The graph suite covers 125 tests and policy
regressions cover 77 tests per host. Full Doom gameplay, audio, save/load, reboot
and performance acceptance remains outstanding.
