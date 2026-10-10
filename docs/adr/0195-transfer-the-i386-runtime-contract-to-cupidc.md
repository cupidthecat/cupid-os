# ADR 0195: Transfer the i386 runtime contract to CupidC

## Status

Accepted on 2026-07-30.

## Context

The hosted i386 runtime contract was already compiled twice by CupidC,
validated as an ELF32 relocatable object, linked with CupidASM startup and the
CupidC runtime, and executed under Linux or WSL. Its `.c` suffix still made
the active-source audit classify it as ordinary C with no runtime owner.

That name understated the checked build boundary. It also left the only
separate runtime-contract source outside the `.cc` naming rule followed by all
19 sources in the static tool fixed point.

The repository still has native contract and oracle sources that a host C
compiler builds. Those files have not crossed the same ownership boundary.

## Decision

Rename the source to
`toolchain/tests/hosted_i386_runtime_contract.cc`. Keep its strict C11 target
profile, `runtime_contract_run` entry point, link order, and runtime checks
unchanged.

Update the Make prerequisite, checked preprocessing manifest, object-link
contract, and build-graph model together. The audit test requires the `.cc`
path, rejects the former `.c` path, and checks that the source is classified
as Cupid C with CupidC as its runtime owner.

The runtime contract remains outside the 19-source seed and fixed-point plan.
It is a behavior probe for that toolchain, not another tool image input.

## Evidence

The focused manifest test passes and includes a negative check for the retired
path.

The regenerated active-source audit still contains 716 inputs, 500
transforms, and 252 feature requirements. Its language split changes from 15
ordinary C and 386 Cupid C files to 14 ordinary C and 387 Cupid C files. The
renamed source is a direct `toolchain_contract` input, is owned by CupidC, and
has SHA-256
`18c4abdd34f18a5e88f5bf4fd516fbc2ef66dc990f1c50b624de5ea1b053baf5`.

The checked tool path compiled and linked the runtime contract, then ran it
under WSL in 30.803 seconds. The program returned zero, printed
`runtime-ok`, kept standard error empty, exercised its missing-input failures,
and wrote the expected `ok -12 0000002A` file.

`make -C toolchain all` rebuilt the complete hosted manifest in 27.9 seconds.
The 42,720-byte runtime executable has SHA-256
`76700d2a5066fba1942f5c86177bfaa11bf1ed85246db5c42ee97e0facd225dc`.
The other five static tool sizes and hashes did not change.

The audit drift and recovery test passed in 180.385 seconds.
The final `make check-bootstrap-audit` run found no drift and passed in 60.4
seconds.

The complete hosted Toolchain suite passed in 35.9 seconds. It covered the
active preprocessor corpus, frontend, Linear IR, object emission, deterministic
self-host linking, all four sibling tool contracts, and their useful failure
paths.

The four-job normal build passed in 588.7 seconds. The changed in-OS manual
compiled into an 11,032-byte `docs_programs_gen.o`. The final kernel is
8,707,172 bytes with SHA-256
`c84d06e641517b1c9a01b15aeae7341aa67da231688ccc6103808b3f98eef774`.
The 209,715,200-byte image has SHA-256
`9e85f55267f6e16153a069a1da985ee5b12ce3bbe09b185a1547c34f65fdd1d5`.

The private four-vCPU e1000 frontier passed in 239.6 seconds. It changed
52,191 framebuffer pixels, captured 8,334,678 AC97 frames, captured 75,868
PC-speaker frames, and completed the compiler, network, USB storage, HID
reattachment, graphics, and audio checks. Its 48,555-byte serial log has
SHA-256
`5e8151231f55a71fe094902cbc93e9dfd27c22b676273593dfeec81c15e41a9a`.

## Rejected alternatives

Leaving the `.c` suffix and adding an ownership note was rejected because the
machine-readable audit derives language ownership from the source name.

Adding the contract to the fixed-point seed plan was rejected because it does
not contribute to any of the five static tool images. The existing plan
already builds the complete production closure.

Renaming every remaining `.c` file in the same change was rejected because
the native contracts, oracles, and inactive legacy sources have not all been
compiled and verified by CupidC. Their names should move only with real
ownership.

## Consequences

All 20 C-family sources in the checked static i386 tool closure use `.cc`.
The 19-source seed and fixed-point plan stays unchanged.

The transfer changes audit ownership, not the runtime ABI or normal OS build.
Host Python still orchestrates the contract, and the native hosted test
programs still require a host C compiler. No GCC, Clang, NASM, linker, or WSL
dependency is added or removed by this rename.
