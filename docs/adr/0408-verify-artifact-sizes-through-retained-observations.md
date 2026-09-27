# ADR 0408: Verify artifact sizes through retained observations

Date: 2026-09-26

Status: Accepted for source integration and seed promotion; production handoff pending

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
