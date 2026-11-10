# Repair verification for the installed 99-input release

The installed seed pair carries 99 producer inputs and an explicit release.
Several older verification fixtures still built commands or manifests for an
earlier cohort. A broad replay exposed those stale assumptions alongside
outdated preprocessor and audit counts. The repairs preserve the current
release checks and the historical reader's rejection of a modern manifest
without explicit release context.

## Conditional inventory and compatibility policy

The active inventory contains 452 `#if` and 24 `#elif` occurrences, 476
conditional expressions, 61 distinct expressions and 64 directive probes.
The Windows runtime source contributes 21 predicates. The checked table
still recorded 18 and used a total guard of 449. The corrected table fails
against the preceding executable guard before the guard is updated.

Windows passes all 39 preprocessor methods. Linux passes those methods and
the active-source inventory comparison. Both checked CupidC seeds compile
the same 411,540-byte contract object, with SHA-256
`1559b028132a43376d7b2d15143ce2fde5940b44678c6c0a6998c097a11ccd29`.
This comparison proves compilation; it does not claim a new checked runtime
execution or promote a seed.

The older `CUPSIZE2` compatibility policy now accepts the installed 99-input
cohort only with its complete reviewed parent tuple and exact Linux and
Windows plan digests. Tests mutate every parent field, reject nearby counts
and mixed plans, and recover after restoring the valid record. The normal
observation-only `CUPSIZE3` verifier keeps its existing release boundary.

All 99 selected policy, runner and native-verifier methods pass on each host,
with four expected Windows skips. Checked Cupid-built ELF32 and PE32 policy
executables each pass all 59 semantic methods. Their contract, policy and
parse objects agree byte for byte across hosts.

## Explicit release context in test fixtures

Current tool commands copy and select a test-owned release alongside the
manifest. When a negative test substitutes a tool image, its helper updates
the image's size and digest in both records. The negative then reaches the
intended command, transaction or output check instead of failing at an
unrelated provenance check. Installed manifests and release files are
unchanged.

Historical schema, provenance and producer-role probes use the existing
frozen historical fixture and omit explicit release context. They retain
their original diagnostics. The generic suite also retains its rejection
of a current cohort used without its release. The repair does not make the
historical reader accept arbitrary modern plans.

The ISO Make fixture copies the required release file. Native CupidBuild
CLI fixtures link the five existing ISO publication modules required by the
current entry point. The Make-writer test includes `tools/hostbuild.py`,
which the source reader already requires. Active audit expectations now
match 75 unreachable files, 30 static-assert sites with six expressions,
and 7,111 `sizeof` sites with 191 expressions.

Staged behavior fixtures attach explicit test-owned authority for the six
tools actually built in that stage and pass its runner to the public
behavior checks. These fixtures exercise release plumbing. They do not
qualify a new paired producer release or replace installed seeds. The
long-path user fixture also copies its selected release into the long root.

## Replays and retained failures

| Replay | Host | Result |
| --- | --- | --- |
| Policy, runner and native-verifier modules | Linux | 99 tests; OK |
| Policy, runner and native-verifier modules | Windows | 99 tests; OK, four expected skips |
| Checked policy semantic modules | Linux and Windows | 59 tests per host; OK |
| Conditional contract and active-audit repair checks | Linux | 40 tests; OK |
| Complete Doom coordinator module | Linux | 21 tests; OK, one expected skip |
| Complete Doom coordinator module | Windows | 21 tests; OK |
| Native user-link and release-operation fixtures | Windows | 68 tests; OK |
| Checked Cupid-built user-link callers | Windows | 52 tests; OK, nine native-hook skips |
| Generic CupidBuild and large flattening modules | Windows | 150 tests; OK, twelve expected skips |
| Repaired staged behavior and long-path cases | Windows | Four tests; OK |

The Doom replay retains all 83 source inputs for each profile, real
coordinator compilation, object comparisons and transaction negatives.
The large flattening replay retains all 500 inputs, repeatability and
preservation when its final input is invalid. No count or timeout is
reduced to make these cases pass.

The original broad Linux suite was interrupted after its failures were
independently reproduced. Its exit status is `-2`; it is not a successful
suite. An initial 42-test Windows compile replay passed the ordinary source
and object comparisons but ended with three staged behavior errors and one
long-path failure. Its complete failure log is retained. All four repaired
cases pass in a 2,007.885-second replay. This targeted replay does not turn
the preceding 42-test attempt into a complete passing module run. A fresh
complete suite remains pending.

The original red installed-seed policy run, an audit import mistake, the
first generic fixture replay, all successful component replays and their
controlled-file identities remain in the evidence. Completed Linux
receipts were preserved before Ubuntu began reporting block-device I/O
errors. No later Linux execution is claimed.

## Scope and evidence

The original verification projection changes seventeen files. Only
`toolchain/artifact_size_policy.cc` is one of the 99 hosted producer inputs.
The other changes are tests and checked contract fixtures. Installed seeds
retain their accepted bytes. The measured OS artifact policy from the
separate storage-binding acceptance is retained.

Normal ownership remains 449 CupidBuild actions and three Python
coordinators across 452 transforms. These repairs do not transfer image
discovery or publication ownership. TempleOS remains reference material
and is excluded from builds and progress counts.

The [evidence index](evidence/verification-release-fixtures-20261011.json)
binds six component archives, the four-case replay and the applied source
projection. Each archive has an independent index and a copy outside
`build`. Final root checks pass; successful commands total 1494.566 seconds. The audit reproduces
exactly; the normal build, user build, ABI check, sixteen exact artifact
gates and 155 portable methods pass, with four expected skips. All 439
accepted OS products and every image byte retain the storage acceptance
bytes. The [root archive](evidence/storage-verification-final-root-20261011.json.gz)
and [root index](evidence/storage-verification-final-root-20261011.json)
preserve those checks, the initial native-test environment failure and its
correction. The OS build retains all nine producer sentinels; development
oracles use their normal compiler environment. A fresh complete suite remains pending.

## Native oracle and remaining fixture follow-up, 2026-10-12

The test recipe now supplies the package root with `-t .`. Five existing
modules then import their relative fixtures correctly. The fresh ABI-command
fixture also captures `.inc` inputs; its ten-method Windows replay passes,
with one expected POSIX-only skip. The Windows private compiler harness now
uses a native PE32 launcher. Its integrated 503-method replay passes without
skips; Linux passes the same selections with eight Windows-only skips.

The [native oracle record](NATIVE-WINDOWS-PRIVATE-ORACLE.md) describes those
repairs, independent launcher rejection controls and the preserved prototype
failure. The two launcher source files lie outside the supported Make roots,
so the current audit's unreachable count is 77. The preceding seventeen-file
projection's count of 75 retains its historical scope. Normal production
ownership and installed seeds are unchanged.
