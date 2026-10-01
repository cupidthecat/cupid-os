# Native user compilation

Source head adds `cupidbuild compile-user` for the three checked user examples:
`cat.cc`, `hello.cc`, and `ls.cc`. It coordinates checked CupidC through the
same compiler transaction used by the kernel and generated installation tables.
The normal user Make recipes and installed seeds still use the preceding path.

The command accepts the seed manifest, repository root, source, and output.
It resolves a relative repository root against the caller's working directory,
then uses the existing user path resolver before creating any output directory.
The selected source determines the object basename. The object stays beneath
an approved child of `user`, outside `user/examples`. Absolute paths inside the
repository, safe lexical dot and parent aliases, hidden and nested directories,
spaces, UTF-8 names, and long components retain that binding.

The output-parent preparation retains the complete directory chain before the
transaction acquires its lock or captures source bytes. Missing directories are
created and remain after failure. Files, links, reparse points, hardlink aliases,
and conflicting live locks receive the existing transaction diagnostics. Windows
prevents replacement of retained directory handles. Linux checks each original
parent/name binding, including when the original leaf has moved beneath a new
ancestor. Cooperating publications to distinct outputs remain valid.

Each compilation captures exactly two records in a closed `CUPSRC1` bundle:
the selected source and `user/cupid.h`. Logical filenames retain their original
repository spelling. An absent include cannot read the live filesystem. The
fixed profile is `--freestanding -I /user`; kernel definitions, GNU mode, and
caller-supplied flags do not enter this operation. The checked compiler has a
180-second deadline. The transaction freezes and validates the complete six-tool
seed, validates the emitted i386 relocatable object, and rechecks the captured
inputs, bundle, candidate, retained parent, lock, and destination before publishing.
Equal validated objects keep their existing timestamps after those checks.

The compiler launches from the existing short working directory on Windows.
A long repository or private file argument does not become the child process's
working directory. The independent long working-directory limitation remains.
Repository roots must meet the retained observer's filesystem contract; lexical
normalization does not establish support for filesystem aliases or UNC roots.

## Executed validation

The initial eleven-method Linux suite passes in 316.447 seconds. The expanded
fifteen-method suite passes in 312.640 seconds on Linux and 437.961 seconds on
Windows. Both native and CupidC-built coordinators compile all three real user
sources to the same bytes as direct checked CupidC and the Python wrapper.
The suite also checks the exact profile and closures, original logical filenames,
configurable output paths, repositories longer than 260 characters launched from
a short directory, invalid requests before directory creation, failed compilation,
unbundled includes, timestamp preservation, aliases, locks, concurrent distinct
outputs, and source/header/manifest drift before launch and after installation.

The related retained-observer and user-ELF suites pass all 56 methods on each
host: 149.467 seconds with five expected Windows skips, and 142.557 seconds
with two expected Linux skips. The fixtures use the reviewed 77-input seed cohort
in an isolated worktree. These runs do not install that cohort or prove the
normal recipe handoff.

The initial checked harness incorrectly enabled host-only environment-based race
hooks. Freestanding Windows rejected the undeclared `getenv`; the checked caller
now uses its production adapter, while race cases exercise the native caller.
The initial Linux oracle setup also encountered the existing GCC warning in
floating-update IR while building an unused native compiler. The suite now builds
only the coordinator it needs and keeps direct checked CupidC as the compiler
oracle. No production source changed to work around either setup failure.

One initial Windows negative case preserved the previous object but left an empty
private directory. Direct repetitions cleaned up correctly, and the expanded
suite passes without residue. This observation remains in the evidence; it is
not a claimed cleanup repair.

The Linux ancestor-transplant case before mutation retains two equal validated
candidate files, its reservation, and the owner lock. It reports cleanup failure
and preserves the previous object's bytes and timestamp. The test validates that
recovery evidence rather than claiming a clean rollback after namespace changes.

## Staged gate and parent compatibility

A shared staged helper now runs in both bootstrap behavior matrices. Each
compared coordinator receives its own complete six-tool cohort. The helper
checks all three approved sources, freestanding definitions, original logical
filenames, nested hidden output directories with spaces and safe lexical aliases,
unchanged timestamps, eight failures with prior-byte/timestamp preservation,
cleanup and recovery to the original three objects. Rejected output paths must
create neither their destination nor an intermediate directory.

Seventeen helper and related methods pass in 275.203 seconds on Linux and
396.056 seconds on Windows. This includes a real CupidC-built coordinator;
that selector compares the same checked caller through both helper lanes and
does not claim two new compiler generations. Thirteen injected helper defects
are rejected, and audit mutations cover lost preservation, recovery, source
approval, stage binding, lexical normalization, output approval, CLI rejection
and removal of the live Linux matrix call.

The expected future full-proof inventory is 55 failure, seven help and 62
success groups on Linux, and 43 failure, seven help and 49 success groups on
Windows. These are source-driver requirements. The accepted preceding release
retains its own 47/7/55 Linux and 35/7/42 Windows evidence.

The shared manifest reader and independent artifact-policy parser now admit
the exact promoted `8403b0a8` parent tuple for the existing 76- and 77-input
profiles. The preceding `5ba6ea24` tuple remains accepted. Each parent digest
must match its revision, and Windows execution and plan parents must share
one generation. Count, plan and historical-profile checks remain intact.
Both new positive methods fail before the change. CupidC-built readers on
each host then match their native oracles for 1,367 manifest cases and fifteen
pair cases. Checked policy callers pass 55 methods and 320 requests per host.
This source compatibility window does not change installed seed identities.

An initial negative policy fixture expected whole Linux/Windows parent-side
replacement to fail. That parser checks the independent parent pins and the
Windows execution/plan generation; the reviewed release and shared pair reader
supply the cross-manifest authority. The fixture now checks mixed Windows
roles within the parser's actual boundary. Nine older native manifest methods
also inherited the promoted long profile in historical fixtures or expected
the old Windows result tag. Explicit historical parents and the selected
profile's expected result restore those cases without relaxing either reader.

The first helper test reached the stricter Windows audit and exposed its old
ten-operation and behavior-count requirements. The source audit now expects
eleven checked operations and the new counts. Three older helper tests also
retained counts from before long-profile certification; their expectations now
match the complete current source matrix. No old proof is relabeled.

## Remaining acceptance

The preceding long-file pair passes current-control publication verification,
both final manual and user/boot replays, and is committed and pushed as
`c8f46ba8`. The isolated implementation and tests are integrated into source.
The source capability passes its own paired staged proof at `78e71bd6`;
fresh proposed-seed consumers
and ordinary/custom-directory Make handoff remain required.
Both normal OS kernel builds, user programs, images and private four-CPU boot
checks pass. Linux's updated publication and user ABI gate also pass.
The three user links and syscall ABI
gate remain separate operations. Production ownership stays at 441 CupidBuild
actions and eleven Python actions. No existing OS source is simplified or renamed
by this addition; the three user sources already use `.cc`.

Evidence is retained in the isolated user-compile worktree under `build/`:
`user-compile-*-v3.log`, `user-compile-related-*-v1.log`, and the direct
`user-failure-reproduction-v1.json`. The final linked-input selector is recorded
separately in `user-compile-links-*-v4.log`. A final replay after adding the
linked-source case passes in 131.074 seconds on Windows and 137.103 seconds
on Linux. `user-compile-final-links-*-v5.json` binds the six implementation/test
file identities, which match across hosts, to that successful run.

The new helper selector retains all nine implementation, driver, audit and test
identities in `user-compile-staged-*-v1.json`. Full audit replays are separate
records in `user-staged-audit-*-v1.log` and their matching state files.

Both complete 129-method audit replays finish with one stale inventory assertion:
the new C code adds seven `sizeof` expressions, taking the expected count from
6,976 to 6,983 across the same 183 files. The other 128 methods pass on each
host. The corrected inventory selector has a separate replay; these failed full
run records are retained.

The active-checkout manifest, policy and four staged-helper modules pass all
97 methods in 28.618 seconds. Their process record and before/after hashes are
`build/bootstrap/native-profile-validation-258bb5f3/integrated-user-fixtures-windows-v1.json`.

The final four coordinator selectors pass after both parent readers change:
468.623 seconds on Windows and 316.797 seconds on Linux. These recheck all three
real objects, the actual staged helper, long repositories from a short launch
directory, and linked source/header/output/parent rejection. Their
`user-final-coordinator-*-v2.json` records bind thirteen unchanged source/test
identities. Native exports, checked reader oracles and checked policy requests
remain in `user-parent-proofs-v1/`.

Run the complete caller suite from the repository root:

```sh
python -m unittest -v tests.test_cupidbuild_compile_user
python -m unittest -v tests.test_bootstrap_compile_user_behavior
```

The first suite builds its optional native coordinator oracle as well as its
CupidC-built caller. The normal build does not require that oracle compiler.

After integration, the complete sixteen-method caller suite passes on both
hosts: 961.527 seconds on Windows and 655.915 seconds on Linux while the OS
replays run separately. `integrated-user-suite-*-v1.json` and the corresponding
UTF-8 logs under the active evidence directory bind the same thirteen inputs
before and after each run.

## Integrated manual and OS build

Both normal Make kernel targets pass with host compilers, assemblers and linkers
forbidden. The 64,715-byte manual produces identical 9,577,752-byte raw kernels,
9,675,196-byte first-pass ELFs and 9,806,268-byte final ELFs. A native check rejects
the old raw size before that one measured policy row changes. The other fifteen
rows remain exact.

Independent rereads bind 1,544 source/control inputs, sixteen artifacts and all
431 link inputs to matching images. Windows passes the three unchanged user
programs and private four-CPU disassembly/shell/SMP smoke; its image remains
unchanged. Image SHA-256 is
`8631d37aa4cdb4aa417f9cd0b1a65c9e7924446b1ab87464d379b39ba6ddbd42`.
Linux's native artifact check, image publication, current-source contract
publication, user ABI gate and final user/boot replay pass. The independent
paired acceptance rereads all retained inputs and both unchanged images.

The active evidence directory retains `user-source-manual-*-preparation-v7.json`,
`user-source-manual-size-calibration-v7.json`,
`paired-user-source-build-verification-v7.json`,
`user-source-manual-windows-acceptance-v7.json` and
`integrated-user-test-verification-v1.json`. A premature publication-proof
capture rejects the incomplete report and retains no proposed seed proof.

[The staged proof record](USER-COMPILE-PROOF.md) binds the committed producer,
both complete native proofs, the nineteen-path release preview and separate
fresh consumer runs. The proposed pair remains uninstalled.
