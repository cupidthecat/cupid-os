# Native artifact verification: selected execution seeds

The installed Linux and Windows seeds now carry selected execution-seed
verification from source commit `5ba6ea24fdef3b23c505551ab537688e681c9593`.
Native `verify-artifact-sizes` accepts paired `--checked-manifest` and
`--execution-manifest` options while preserving its original API and command.
Windows requires its canonical execution cohort; Linux can retain a separate
copy of the reviewed six-tool cohort through the final drift check.

Fresh staged proofs match 41 Windows and 34 Linux outputs. Both promoted-seed
self-bootstrap proofs reproduce all twelve installed tools. Promotion regressions
pass 352 unique methods per host, with platform skips recorded separately.
Checked callers pass 4,919 reader cases and 52 policy methods per host; installed
artifact-verifier fixtures pass 40 Windows and 56 Linux cases across ASCII, accented, Japanese and emoji
paths. All 28 converged Windows Unicode-path commands pass.

Linux publication verifies 22 artifacts, 88 publication inputs and 67 stage
pairs. All three hosted contracts pass independent verification, including seven
i386 executables. Both hosts produce the same 9,571,000-byte kernel with the
updated embedded manual. All 74 policy regression tests pass per host.
Independent paired OS acceptance checks 1,534 source inputs, sixteen artifacts
and 431 linker inputs per host. Images and all three user programs match.
Both private four-CPU max/e1000 smokes pass disassembly, shell completion and
SMP checks while preserving their source images. The image SHA-256 is
`8ea7d8eabd6937ad71f5d2ec0ec1cdabf1ac649466a4fc8450f49b987d60ff81`.

Production still uses the Python artifact verifier: 440 CupidBuild actions and
twelve Python actions. The native Make handoff must pass both selection options
and receive fresh graph and OS acceptance. Full Doom gameplay, audio, save/load,
reboot and performance acceptance remains outstanding.

Promotion evidence is retained under
`build/bootstrap/artifact-promotion-5ba6ea24/`. The paired OS report is
`paired-os-acceptance-v1.json`; hosted verification is
`linux-promoted-hosted-verification-v1.json`. Promotion and OS captures retain
separate identities: the embedded manual changes first, then the exact-size
policy changes from the measured kernel result. Final prose updates do not alter
those tested inputs.

## Source integration history

The following records describe the source acceptance that preceded promotion.
References to unchanged installed seeds or pending promotion apply to that stage.

The production handoff audit found an input missing from the promoted native
interface. The Python runner retains the selected Linux execution manifest and
all six payloads, even when that cohort is separate from the policy cohort.
Windows requires the execution manifest to name the canonical checked Windows
seed. Silently dropping those checks would change supported selection behavior.

The source candidate adds `cupidbuild_verify_artifact_sizes_selected` without
changing the original request layout or entry point. Its two additional paths
name the checked Windows manifest and execution manifest. The CLI accepts them
as a pair through `--checked-manifest` and `--execution-manifest`; existing
three-option callers keep their original behavior.

The checked Windows path must remain
`bootstrap/seeds/i386-windows/manifest.json`. Windows execution must use that
path. Linux may select a separate directory containing `manifest.json` and the
six reviewed ELF32 tools. The verifier validates the manifest against the
reviewed release and supported plan, checks exact directory membership, hashes
all payloads, and checks their executable profiles. Those observations stay in
the same observer through its final drift check and close. A selected cohort
already captured for policy verification reuses those observations.

This remains read-only verification. It does not launch tools, authenticate its
own running executable, or provide an atomic filesystem snapshot. The selected
manifest describes retained inputs; the build environment still supplies the
initial CupidBuild executable, as it does for other direct native operations.

Windows and Linux source runs each pass all fifteen native artifact methods,
including current-cohort success, rejection of a noncanonical checked manifest,
paired-option validation and alternate Windows execution rejection. The Linux
alternate-directory case checks success, rejection
of a same-size payload mutation with its timestamp restored, and recovery.
The API tests check invalid selections, cleared results, bounded diagnostics and
recovery. Linux also rejects selected-manifest, selected-payload and directory
membership mutations after capture. The prior twelve retained-observation race
cases and allocation failure tests remain in the suite.

Evidence is in `build/bootstrap/native-artifact-selection-tests-windows-v3.log`
and `build/bootstrap/native-artifact-selection-tests-linux-v3.log`. These are
host-compiled source tests. The checked-Cupid tests and staged proofs are
recorded below; seed promotion and paired production acceptance remain pending.
The installed seeds do not yet carry the new options. Production therefore
keeps its accepted Python verifier during source acceptance. The unfinished
native Make handoff is preserved in
`build/bootstrap/native-artifact-handoff-deferred-v1.patch`; after promotion,
it must pass both selection options and receive fresh graph and OS acceptance.

The first checked-object build exposed a platform-selection error: the shared
artifact module is deliberately compiled without `_WIN32`, including in the
Windows plan. It must ask the host adapter for its execution image format.
`cupidbuild_host_execution_format` now provides that answer from the existing
platform-specific translation unit. The corrected coordinator, CLI and host
adapter compile with the installed CupidC on both hosts. The two shared objects
match across hosts; the host adapters retain their distinct platform profiles.
The `native-artifact-selection-checked-{windows,linux}-v2/report.json` reports
bind the copied sources, headers, checked seed and resulting objects. These
object checks do not establish staged convergence.

Both checked linked candidates pass ten CLI methods, including selected-cohort
success, invalid selection, aggregate policy diagnostics and recovery. Each
harness first relinks recorded stage-four objects and reproduces the installed
CupidBuild byte for byte. It then replaces the three changed objects with the
new checked outputs. Reports and logs are under
`build/bootstrap/native-artifact-selection-linked-{windows,linux}-v1/`.
This is a diagnostic relink, not a fresh staged proof.

The first checked race-caller link failed because the harness linked the verifier
both through the caller's source include and as a separate object. The corrected
harness omits the separate object after its baseline reproduction. The failed
attempts remain under `native-artifact-selection-race-{windows,linux}-v1/`;
the corrected v2 runs pass. The checked Windows caller rejects twelve mutations;
Linux rejects fifteen, including the three separately selected-cohort mutations.
Independent paired verification rehashes the copied inputs, objects, executables,
logs and recorded reports in
`build/bootstrap/native-artifact-selection-paired-verification-v1.json`.

Promotion preflight found another requirement in both native readers. Their
parent lists ended at the `72170b06` generation. They now also admit the exact
`ec896462586597893dd197697ee3b68ce2c8e69b` parent, paired with Linux manifest
`dabdc048ce54c7434fd9edd602f0531ead60f425db39bc2550332d7c69d30608` and Windows
manifest `25290a99f9de273890cd98130e8ad7df5e9ffcd89010be8e285a06a380e1eaf2`.
Existing generations remain admitted. Tests reject each unknown or mixed parent
field and complete Windows execution/plan parents from different generations.
All 74 manifest-reader and artifact-policy methods pass on each host; their logs
are `native-artifact-selection-parent-tests-{windows,linux}-v1.log` under the
bootstrap evidence directory.

Installed CupidC also compiles the two changed readers on both hosts with
matching objects. Checked executables pass 4,919 reader cases, including 1,242
manifest cases, and all 52 artifact-policy methods per host. Independent
verification rehashes the copied sources, resulting objects and executables,
exported cases, outputs and test reports. Evidence is under
`build/bootstrap/native-artifact-selection-parent-checked-v1/`, including
`paired-checked-cohorts-v1.json`. Its current-source record is a post-test byte
check; the individual build reports retain the inputs copied before compilation.

The isolated v1 staged proofs passed on both hosts. They captured 1,534 files
and 76 producer inputs before the parent-list extension, so their results apply
to that earlier capture. The v2 proofs also passed and include both reader
changes. Independent verification rehashed the captured inputs, stage objects,
tools and behavior reports, including 41 compared Windows artifacts and 34 Linux
artifacts. Stage three and stage four match on each host. Evidence is under
`build/bootstrap/native-artifact-selection-proof-v2/`, including
`native-paired-proof-verification-v1.json`.

The build-graph suite passes all 124 tests, and the generated audit check passes.
Production ownership remains 440 CupidBuild transformations and 12 Python
transformations. Neither the installed seeds nor the production handoff have
been promoted.

Both hosts rebuilt the kernel with the updated manual. Independent comparison
verified matching bytes for all 16 artifacts and 431 linker inputs, the embedded
manual, and preservation of the previous disk images. The manual adds 424 bytes
to `kernel.bin`, bringing it to 9,570,996 bytes; both ELF sizes remain unchanged.
The exact-size policy now records that measured size. All 74 policy regression
tests pass on each host, with independently checked reports and retained logs.
Evidence is under `build/bootstrap/native-artifact-selection-os-v2/`, including
`paired-kernels.json` and `paired-policy-regressions-v1.json`.

The policy update changes no producer input. The OS acceptance capture retains
1,534 files and the same 76 producer inputs as the v2 staged proofs.

Linux publication passed with 22 artifacts, 88 publication inputs and 76 producer
inputs. The checked manifest verifier passed. Independent verification rehashed
all published executables and their 88 inputs in both the publication root and
the Linux OS root after transfer. The previous publication remains preserved.
Evidence is under `build/bootstrap/native-artifact-selection-publication-v1/`,
including `independent-publication-files-v1.json`.

Windows image/runtime acceptance passed: image creation, the three user programs,
native artifact-size verification, and a private-image boot smoke with four CPUs,
`max` CPU, e1000, SMP checks, `dis /bin/ls.cc`, and `ls`. Independent verification
checked the 1,534 captured inputs, 16 artifacts, 431 linker inputs, embedded manual,
user programs and retained serial log. The smoke test preserved the source image.
Its report is `native-artifact-selection-os-v2/windows-independent-os-acceptance-v1.json`
under the bootstrap evidence directory. All three Linux hosted contracts pass,
including tool linking. Independent verification binds their logs, seven i386
executables, publication manifest and 1,534 captured source inputs in
`native-artifact-selection-publication-v1/linux-selection-hosted-verification-v1.json`.
Linux image/runtime acceptance also passed. Independent per-host and paired
verification checked the same 1,534 inputs, sixteen artifacts and 431 linker
inputs, plus the embedded manual and retained serial logs. Both private
four-CPU max/e1000 smokes passed disassembly, shell completion and SMP checks
without changing their source images. Images and all three user programs match.
The image SHA-256 is
`097ac6818d9fc85347c7e99d867fd4ef3d470ead67aea36de6790793fcb0fff5`.
Evidence is `native-artifact-selection-os-v2/paired-os-acceptance-v1.json`.
Source acceptance is complete; seed promotion and the production handoff remain.
