# ADR 0395: Prove Doom compilation before refreshing the seed pair

Date: 2026-09-20

## Decision

Both fixed-point drivers run the same `compile-doom` behavior check through
stages three and four. Each coordinator receives its own stage's complete
six-tool seed. The check copies the approved Doom cohort and header roots,
then replaces two sources with fixtures for the compatibility and tree
profiles. They check the absence of `DEBUG`, the tree definitions, the forced
compatibility header, include-stub lookup, and logical source paths.

The proof compares both objects across generations, validates their ELF32
format, and checks unchanged-output timestamps. Invalid C, a missing bundled
include, a missing forced header, a missing cohort member, and an extra legacy
`.c` source must fail while preserving the destination bytes and timestamp.
Both profiles must reproduce their original objects after recovery. Every
invocation must remove its transaction files and output lock.

These five success and five failure groups bring the Linux inventory to
41 failure, seven help, and 47 success groups. Native Windows has 29 failure,
seven help, and 34 success groups. The audit rejects missing shared checks,
stage seed substitutions, and lost preservation or recovery assertions.
These are expected proof inventories; the bootstrap log records which runs
have actually completed.

## Parent compatibility

CupidBuild and the artifact-size contract accept the adjacent promoted-parent
generations `16a86f5b` and `9d2529a7`. Each digest must match its revision.
Windows execution and plan parents must belong to the same generation.
The `0232cb57` generation leaves the accepted v2 window. The v1 contract is
unchanged.

The current checked pair still names `16a86f5b` as its parent. Accepting
`9d2529a7` permits a successor built from that checked pair without trusting
arbitrary ancestry or embedding the successor's own digest in its tools.
Tests cover both admitted generations, mismatched digest/revision pairs,
mixed Windows generations, and the retired pair.

## Publication boundary

A source checkpoint must pass clean Linux and native Windows fixed-point
proofs before seed promotion. Independent promotion checks must verify both
reports, exact stage inventories and bytes, source snapshots against the
commit, build plans, executable formats, and parent identities. The proposed
pair must pass verification before tracked seed files change.

Passing the host-coordinator behavior fixture is useful preflight evidence.
It does not establish staged convergence or transfer production Doom recipes.
Production adoption still requires all-source parity, the normal OS build,
artifact checks, and runtime smoke. TempleOS remains reference material.
