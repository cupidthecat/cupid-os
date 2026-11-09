# Verification across seed generations

The current fixtures preserve their strict checks with both the installed
cohort and the prospective source-owner cohort. Compiler, assembler, linker,
OS source and generated OS bytes are unchanged.

## Corrections

- The pause-failure caller supplies the copied release through the existing
  explicit-release API. Its live-child kill/reap, closed-pipe and descriptor
  checks still run; the historical entry point keeps its original rules.
- The anonymous-input test requires eight exact names: six tools, the manifest
  and the release. All seal, rejected-write/truncate, metadata-drift and cleanup
  assertions remain.
- The preprocessor requires 194 active and 129 inline calls. A fresh
  CupidC/CupidLD verifier accepts all 194 and rejects a copied 193-row input
  with the exact count diagnostic. Macro and error modes also pass.
- NASM 3.01 reverses adjacent `66`/`67` size prefixes in the active 16-bit boot
  copy instructions. Both images must pass strict mapped decoding. The test
  then permits that swap only in leading `code16` prefixes and compares the
  complete images. Data, immediates, operands, opcodes and 32-bit code stay
  exact. CupidASM's frozen 2,560-byte boot hash remains required. ISO remains
  the only raw fixture excluded from the NASM oracle.
- Historical Windows release-operation fixtures keep the actual pre-worker
  CupidBuild image separately from current tools. Its uncompressed SHA-256 is
  `b5349a0f302e4ab98d081f5aaf1670d8fb20022d51795e22baf15b3a4218d4ad`.
  Swapping the six prospective tools individually isolates the profile mismatch
  to CupidBuild. The pinned compatibility fixture is not a fixed-point proof.

## Evidence and remaining work

Complete 200-method selections pass on Windows and native Linux, with twelve
and eight existing platform skips. The complete sixteen release-operation
methods pass against the prospective cohort on both hosts and the installed
Windows cohort. All original tool bounds remain. Six helper tests and a full
boot positive/negative check cover the prefix comparison. The audit refresh and
check pass; only the C test-source hash and aggregate source digest change.
The 114 producer inputs and 449 CupidBuild / three Python transforms stay exact.

The preceding full Linux replay failed four of 3,659 tests with 97 skips.
The Windows replay failed eighteen of 3,643, with one setup error and 75 skips.
These fixture and worker-link corrections address those reported groups;
fresh full replays remain required before adoption.

A concurrent DrvFS module selection also failed a parent-drift check and
rejected a changing directory closure. The isolated native selection passes.
A proposed publication checkpoint still reproduces the DrvFS parent failure;
its cause remains open and the original assertion is unchanged. An auxiliary
clone first stopped on a mistyped revision guard before running tests.
The initial four-method reproduction used an older 189-row private input,
so its preprocessor pass is kept separate from the final 194-row proof.

The [archive](evidence/final-replay-verification-fixtures-20261109.json.gz)
retains 4,603 file records and 1,058 independently rechecked payloads, including
original failures, native and checked callers, probes and source guards.
SHA-256: `67f8f7b48dbbf7df16653c384edf6703fb891764ee8dd6372e4c926fa2337b36`.
Installed seeds, image-command ownership and guest self-hosting remain open.
