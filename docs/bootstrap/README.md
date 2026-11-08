# Cupid Toolchain bootstrap

Cupid OS builds with checked CupidC, CupidASM, CupidLD, CupidObj, CupidDis,
and CupidBuild tools. Hosted toolchain fixed points have been verified on
Linux and Windows. Building and maintaining the complete system inside
Cupid OS remains unfinished.

## Current boundaries

- The normal graph has 452 transforms: 449 use CupidBuild and three retain
  Python coordination. GNU Make still schedules the build.
- Native source discovery and disk/SDK publication still need integration
  and complete qualification. Passing API experiments do not transfer those
  normal build edges.
- The private in-OS compiler supports halfword storage, integer array lists
  and typed native calls. Its remaining language gaps and guest acceptance
  are separate from hosted compiler fixed points. The shell can inspect
  saved raw files through the shared disassembler.
- Full Doom gameplay and audio acceptance remain open.
- TempleOS is read-only reference material and is excluded from build counts.

See the [ownership audit](ACTIVE-SOURCE-AUDIT.md),
[capability matrix](CAPABILITY-MATRIX.md) and
[host dependencies](HOST-DEPENDENCIES.md) for measured scope and limitations.
The [checkpoint history](HISTORY.md) preserves earlier results and failures;
its dated counts describe those checkpoints.

## Build and verify

The [getting started guide](../../wiki/Getting-Started.md) covers dependencies
and platform setup. The normal image build is:

```sh
make -j2 all
```

Useful verification entry points:

```sh
make test
make check-bootstrap-audit
make verify-bootstrap-seed
make verify-windows-bootstrap-seed
make bootstrap-from-seed
make bootstrap-windows-from-seed
```

The last two reconstruct the hosted tools from checked seeds. Their reports
record stage comparisons and behavior checks; they do not prove that the
complete OS can rebuild itself from inside the guest.

## Records

- [Private integer updates](PRIVATE-INTEGER-UPDATES.md): typed lvalue mutation
  with paired-host source checks and installed JIT/AOT evidence.
- [Private pointer difference](PRIVATE-POINTER-DIFFERENCE.md): signed element
  counts, diagnostics, native compiler tests and Windows guest evidence.
- [Implementation log](LOG.md): changes, test evidence and failed approaches.
- [Migration matrix](MIGRATION-MATRIX.md): tool ownership by source cohort.
- [Active build audit](audits/active-build.json): machine-readable graph;
  regenerate it and its summary with `make bootstrap-audit`.
- [Baseline procedure](BASELINE.md): reproducible optional host-oracle captures.
- [Architectural decisions](../adr/): stable design decisions.
- [User and developer wiki](../../wiki/Home.md): supported commands and features.

Each toolchain capability needs positive and useful negative tests. Ownership
moves only after the replacement build and relevant guest checks pass. Keep
source-head experiments, installed seeds and production ownership distinct
when recording evidence.
