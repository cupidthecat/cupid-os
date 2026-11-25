# Cupid Toolchain bootstrap

Cupid OS builds with checked CupidC, CupidASM, CupidLD, CupidObj, CupidDis,
and CupidBuild tools. Hosted toolchain fixed points have been verified on
Linux and Windows. Building and maintaining the complete system inside
Cupid OS remains unfinished.

## Current boundaries

- The normal OS and user builds run through Cupid tools. GNU Make schedules
  the work, and CupidBuild publishes the disk image. The supported graph
  has 455 transforms, of which 453 use CupidBuild. The other two publish and
  verify the hosted SDK contracts through Python.
- Both installed seeds come from the same 124-file source snapshot. Cold
  builds on Windows and WSL produced matching disk images and user programs.
- Cupid OS rebuilt and ran CupidC, CupidASM with CupidDis, CupidLD, CupidObj,
  and CupidBuild inside the guest. A 19-request plan rebuilt all 431 linked
  kernel objects, including Doom and the generated symbol table. It ran
  both links and wrote a disk prefix. The read-only ISO replay and the
  editable HomeFS replay each produced eleven matching outputs. Their
  rebuilt disks booted with four CPUs and ran `ls`.
  The guest disk worker can also save a complete 32 MiB image in HomeFS.
  QEMU booted that file without host padding. A 64 MiB guest image with
  the checked ISO and Freedoom WAD also matched the hosted reference and
  booted. HomeFS still keeps the image in memory during publication.
- A separate guest plan built `cat`, `hello` and `ls` in one command. Each
  program ran from HomeFS. Linux or WSL still prepares the plans and
  media for these checks. The [guest build record](FREESTANDING-COMPILER-ENGINE.md)
  has the replay commands.
- The shell compiler supports halfword storage, integer array lists, and
  typed native calls. The shell can inspect saved raw files with CupidDis.
  The [capability matrix](CAPABILITY-MATRIX.md) lists remaining language gaps.
- Doom passed the keyboard, save-name, and reboot checks. Freedoom completed
  a four-CPU timedemo on November 18. The November 21 replay timed out after
  graphics startup. Gameplay and audio acceptance remain open.
- TempleOS is read-only reference material and is excluded from build counts.

See the [ownership audit](ACTIVE-SOURCE-AUDIT.md),
[capability matrix](CAPABILITY-MATRIX.md) and
[host dependencies](HOST-DEPENDENCIES.md) for measured scope and limitations.
The [source layout inventory](SOURCE-LAYOUT.md) tracks files over 2,000 lines.
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

- [Remaining formatting fixtures](REMAINING-FORMAT-FIXTURES.md): bounded user
  profile readback, exact producer snapshot and paired complete user checks.

- [Freestanding compiler engine](FREESTANDING-COMPILER-ENGINE.md) covers the
  ELF loader, compiler-source ISO, standalone workers and kernel replay.

- [Keyboard FIFO repair](KEYBOARD-FIFO-REPAIR.md): shared reader cursor, checked
  tool fixtures, complete kernel rechecks and the 31-command guest frontier.

- [Source formatting](CUPIDC-SOURCE-FORMATTING.md): the configured `.cc` style,
  include dependencies, source-driven checks and remaining verification.

- [Cold qualified-tool builds](FORMATTED-COLD-CONSUMERS.md): matching Linux and
  Windows objects and images, user builds and four strict private boots.

- [External observer authority](PRIVATE-WIDE-EXTERNAL-OBSERVERS.md): distinct
  retained roots, exact selection, actual post-install rollback and original
  path-rebinding checks in the current private wide adapter.

- [Coalesced wide-input checks](PRIVATE-COALESCED-WIDE-INPUTS.md): one complete
  frozen-set walk per live-input call, with counted rejections and full clean
  image checks.

- [Source-free publication](PRIVATE-SOURCE-FREE-PUBLICATION.md): explicit input
  registration, unchanged legacy capture, exact constructor extraction and
  complete staged images through the normal Windows bridge.

- [Filesystem-aware fixtures](FILESYSTEM-VERIFICATION-FIXTURES.md): synchronized
  parent-drift checks, actual DrvFS exclusion and current include counts.

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
- [Source layout](SOURCE-LAYOUT.md) tracks files over 2,000 lines.
- [Active build audit](audits/active-build.json): machine-readable graph;
  regenerate it and its summary with `make bootstrap-audit`.
- [Baseline procedure](BASELINE.md): reproducible optional host-oracle captures.
- [Architectural decisions](../adr/): stable design decisions.
- [User and developer wiki](../../wiki/Home.md): supported commands and features.

Each toolchain capability needs positive and useful negative tests. Ownership
moves only after the replacement build and relevant guest checks pass. Keep
source-head experiments, installed seeds and production ownership distinct
when recording evidence.
