# Cupid Toolchain bootstrap

Cupid OS builds with checked CupidC, CupidASM, CupidLD, CupidObj, CupidDis,
and CupidBuild tools. Hosted toolchain fixed points have been verified on
Linux and Windows. Building and maintaining the complete system inside
Cupid OS remains unfinished.

## Current boundaries

- The normal graph has 452 transforms: 449 use CupidBuild and three retain
  Python coordination. GNU Make still schedules the build.
- Source-owner modules are in current producer plans, and paired named
  preparations reach a fixed point. The C reader now recognizes their exact
  behavior profiles. Both refreshed full qualifications pass. Replacement-tool
  cold OS/user consumers and four private boots also pass, along with four SDK
  publication profiles. Full current repository replays, installed seed adoption
  and the normal image-command handoff remain open.
- The private in-OS compiler supports halfword storage, integer array lists
  and typed native calls. Its remaining language gaps and guest acceptance
  are separate from hosted compiler fixed points. The shell can inspect
  saved raw files through the shared disassembler.
- Doom keyboard, save-name and exact-image reboot checks pass; full gameplay
  and audio acceptance remain open.
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

- [Verification across seed generations](FINAL-REPLAY-VERIFICATION-FIXTURES.md):
  explicit release authority, exact sealed inputs, boot prefix comparison and
  historical Windows compatibility, with full-replay limits retained.

- [Doom keyboard and handoff](DOOM-KEYBOARD-AND-HANDOFF.md): complete key mapping,
  default save-name input, physical save/reboot checks and clean shell return.

- [SDK owner-seed context](SDK-OWNER-SEED-CONTEXT.md): exact captured-plan support,
  original full publication failures and native/Cupid-built policy checks.
- [Four SDK publications](SOURCE-OWNER-SDK-PUBLICATIONS.md): complete paired
  ordinary/long builds, retained stage pairs and actual author replays.
- [Current private wide stores](PRIVATE-WIDE-STORES-CURRENT.md): retained
  extents, precise timestamps, executable marking and concurrent store checks.
- [Current checked-output transfer](PRIVATE-OUTPUT-TRANSFER-CURRENT.md): retained
  input custody, complete-payload publication and producer rejection checks.
- [Registered-input ranges](PRIVATE-REGISTERED-INPUT-RANGES.md): bounded reads,
  complete retained images, precise metadata checks and real mixed-thread calls.
- [Retained image publisher](PRIVATE-RETAINED-CHECKED-PUBLISHER.md): actual checked
  templates, complete image parity and guarded rejection in one private caller.
- [Retained FAT16 staging](PRIVATE-RETAINED-STAGED-PUBLISHER.md): complete staged
  image parity, failed-stage publication checks and a strict private guest boot.
- [Private Windows store bindings](PRIVATE-WIDE-STORE-WINDOWS-BRIDGE.md): normal
  build configuration, two added imports and native/Cupid-built rejection checks.
- [Source-owner integration](SOURCE-OWNER-INTEGRATION.md): producer plans,
  flexible members, compiler-first stages and remaining acceptance.
- [Public bootstrap entries](SOURCE-OWNER-PUBLIC-ENTRYPOINTS.md): historical
  fixtures, paired full CLI checks and both native Make reconstructions.
- [Current fixture inventories](SOURCE-OWNER-INVENTORY-FIXTURES.md): exact source
  counts, observer-worker selection and paired native/Cupid-built checks.
- [Image-profile fixtures](SOURCE-OWNER-IMAGE-PROFILE-FIXTURES.md): historical
  imports, checked worker links and portable verification dependencies.
- [Native verification fixtures](SOURCE-OWNER-NATIVE-ORACLE-FIXTURES.md): current
  frontend/object goldens, portable native inputs and required Make flags.
- [Worker/kernel fixture closure](SOURCE-OWNER-WORKER-KERNEL-FIXTURES.md): actual
  current Windows tools, 33 executable coordinator checks, native-storage kernel
  replay and checked frontier cases.
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
