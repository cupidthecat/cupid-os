# ADR 0278: Add a native Windows fixed-point driver

## Status

Accepted on 2026-08-13.

## Context

The checked Windows PE32 cohort can run all five Cupid tools directly, but its
execution manifest intentionally contains no build plan. The Linux bootstrap
manifest carries the reviewed source graph, compiler profiles, object order,
and link plan. As a result, Windows production commands are native while the
fixed-point driver still enters through the Linux executable seed and WSL.

Copying the Linux plan into the Windows execution manifest would blur two trust
roles and duplicate a large reviewed contract. A native proof can instead
freeze both verified manifests and derive the small target-specific changes in
one public driver.

## Decision

Add `bootstrap-windows` to the public bootstrap command. It requires a checked
Windows execution manifest and a checked Linux plan manifest. It freezes and
verifies both before probing the host. The driver derives the native compile
definitions, Windows startup objects, import selectors, and PE link order from
the Linux source plan without changing either manifest's role.

The checked Windows CupidC, CupidASM, and CupidLD images build stage two. The
stage-two producer trio builds stage three, and stage three builds stage four.
ADR 0279 defines stages two and three as transition generations and stages
three and four as the comparison pair. The driver compares every C and assembly
object plus all five PE tools byte for byte. It runs help, useful success, and
useful failure behavior on the comparison pair, rehashes the frozen and live
source closures at every boundary, and publishes one complete directory with a
`cupid.windows-bootstrap-report.v1` report. An occupied output, source drift,
seed drift, or failed behavior gate publishes nothing.

Make exposes the reproducible operator boundary. The
`make verify-windows-bootstrap-seed` target verifies the separate PE execution
seed and Linux plan manifest. The `make bootstrap-windows-from-seed` target
runs the proof and publishes only to `build/bootstrap/checked-windows-seed`.

The existing Linux `bootstrap` command continues to reject the Windows
execution seed. The two public commands keep their target and manifest roles
explicit.

## Evidence

Five focused tests pass in 1.521 seconds. They cover both manifest roles,
freezing of both verified seed cohorts, rejection before any WSL probe,
occupied-output preservation before a producer runs, and the existing Linux
path rejection. A seven-test publication and native-execution subset passes in
3.569 seconds, and Python bytecode compilation succeeds.

The first complete native run built stage two and reached its final source
rehash in 373.721 seconds. Concurrent CupidASM edits changed the live closure,
so the driver rejected publication and removed its private directory. That is
positive evidence for the drift boundary, not a completed fixed point. A
source-stable rerun is required before seed promotion.

A later source-stable run reached the stage comparison and stopped safely at
`cupidobj_main` after 821.9 seconds. The stack-probe change had altered
compiler-produced objects, so stage two and stage three represented different
code-generator generations. The two-stage comparison could not demonstrate
convergence. ADR 0279 adds stage four and moves the comparison to stage three
against stage four.

A later uncapped native run completed in 20 minutes 43 seconds. All 20 C
objects, both assembly objects, and all five PE tools matched between stages
three and four. The two stages also passed the complete 5/5/5 native behavior
matrix. The report binds the same 50-input snapshot as the matching Linux run,
and an independent pass verified every recorded artifact size and hash. This
proof began from an uncommitted source tree, so a named clean-commit replay is
still required before promotion.

The Make dry run and two Make contract tests pass. They pin both manifest
arguments, the repository root, and the checked Windows output directory.

## Consequences

Cupid OS now has a public native Windows reconstruction path without placing a
second build plan in the PE execution manifest. Host Python still coordinates
the proof, and the Linux manifest remains a required plan input. WSL is no
longer inherent to Windows fixed-point reconstruction, but it remains in use
for Linux contract programs. ADR 0279 supersedes the two-generation comparison
with a convergence generation. A later promotion record must supply the
named clean-commit comparison, new seed hashes, and production build results.
