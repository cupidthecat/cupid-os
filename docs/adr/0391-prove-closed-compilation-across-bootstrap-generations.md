# ADR 0391: Prove closed compilation across bootstrap generations

Date: 2026-09-20

## Decision

Both fixed-point drivers exercise `cupidbuild compile-kernel` through each
compared tool generation. Each coordinator receives its own generation's
complete six-tool seed. The proof covers code and data-only objects, unchanged
output reuse, failed compilation, missing closure inputs, recovery, and
transaction cleanup. A passing object comparison alone does not establish
production ownership.

Closed compilation uses the existing checked `publish_if_changed` operation.
Equal validated object bytes retain the prior timestamp. Captured inputs,
candidate identity, output binding, and the publication boundary are still
checked before this no-change result. A missing header must fail even when
the destination already contains the last successful object.

Promoted v2 readers accept the adjacent `0232cb57` and `16a86f5b` parent
generations. The former remains the parent of the currently checked seeds;
the latter is the parent for their successors. Linux digests must match their
revision, and Windows execution and plan parents must belong to the same
generation. Historical v1 validation keeps its existing contract. The
`9d10c223` parent pair leaves the v2 window.

This prepares the source capability from ADR 0390 for a checked-seed refresh.
The refresh must reconstruct Linux and native Windows tools from a committed
source tree, compare every final-stage object and executable, run the expanded
behavior checks, and independently verify the source inventory and artifacts.
Make adoption follows that proof and requires compiler parity, failure tests,
the full image build, exact artifact checks, and runtime smoke.

## Scope

The eleven admitted sources already use `.cc`. Their source code, kernel
profile, header closures, and timeout policy remain unchanged. The work adds
no host C or assembly dependency to the normal build. Python still coordinates
the staged proof and every production kernel/Doom compilation until the
recipe handoff is separately verified. TempleOS remains reference material.

Executed checks and failed approaches are recorded in the bootstrap log.
