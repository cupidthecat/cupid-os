# ADR 0408: Verify artifact sizes through retained observations

Date: 2026-09-26

Status: Accepted for source integration; seed promotion and production handoff pending

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
The installed 73-input seeds and Python production operation remain in place
until candidate promotion and production acceptance establish the handoff.

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
