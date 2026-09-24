# ADR 0356: Refresh the paired seeds for guarded raw assembly

## Status

Accepted on 2026-08-27.

## Context

ADR 0355 added guarded raw-image publication to CupidBuild. The checked Linux
and Windows seeds predated that work, so their CupidBuild images could not run
`assemble-bootloader` or `assemble-smp-trampoline`. Moving either normal Make
recipe at that point would have selected an older executable than the source
that defined the transaction.

The other five tools did not change. A seed refresh should therefore preserve
their bytes, keep the existing Linux and native Windows build plans, and move
the two CupidBuild images together. Windows must also name the exact refreshed
Linux manifest that supplies its plan.

## Decision

Refresh the paired v2 seed cohorts from source revision
`43c747f0e683d0527984bae05bf944879e64a07b`. The 58-input source snapshot has
SHA-256
`4cd9d583933d8a9f1dbfb63425bc3665fe6c306db8ae76606f40a0ade49afe70`.
The Linux plan remains
`52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd`,
and the native Windows plan remains
`f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14`.

Only CupidBuild changes:

| Platform | Bytes | SHA-256 |
| --- | ---: | --- |
| Linux ELF | 276,788 | `55fd96ed06cd451364008a79899765bd8e2796485b73fa65938b2d0f0512f7bb` |
| Windows PE | 293,888 | `508dcc5442b6fde8a2f297965cbd9303a14e7c0a3c5cbda9921d62b255424815` |

The 6,602-byte Linux manifest has SHA-256
`78d26d7ce3aa0393c8c27a33f2b1f2fad6fe5f6f6300267bf674b36ce51a4dd8`.
The 2,852-byte Windows manifest has SHA-256
`019d6ddd54e183752bd6c579215d4c56bf91dbbef9db9cc0854cdce5f4017288`
and records the Linux digest in `plan_seed_manifest_sha256`.

The v2 provenance fields continue to name the original v1 transition parents.
They do not claim that those manifests directly produced every later refresh.
The promoted source revision and snapshot identify this cohort, while the host
verifier pins its exact manifests and artifact identities.

The artifact-size policy changes the two CupidBuild rows and the flat-kernel
row measured after this documentation entered the image. Its 16 exact
artifacts total 38,143,900 bytes. The 3,382-byte policy has SHA-256
`3518552751c6993bbf4c36735a0a780616253543ba5c6555af55ae5979c45ff6`.

## Evidence

The Linux candidate fixed point compared 22 C objects, one startup object, and
six tool images between stages three and four. It passed 24 failure, six help,
and 31 success cases. Its 51,390-byte report has SHA-256
`912d8c43f8c7129985f819b58ee19d8ae92aa9e16e0aae2e9db57ce8cb261d2c`.

The native Windows candidate compared 23 C objects, three assembly objects,
and six PE images between stages three and four. It passed 13 failure, six
help, and 18 success cases. Its 64,516-byte report has SHA-256
`7ac7087a866af10666ff4c4356677bae886c0f3df648076b17a89ade19dac60c`.

In both candidate reports, the five unchanged tools matched the checked seed
at stage two and CupidBuild did not. That mismatch was the expected reason for
the refresh. After promotion, both real-seed runs reported all six initial
images equal and retained stage-three/stage-four equality.

The first complete 129-test coordinator run reached both real fixed points and
found only two stale assertions that still expected CupidBuild to differ at
stage two. Every other case passed. The assertions now describe the refreshed
cohort, where all six seed images match their stage-two outputs. The two
corrected real-seed tests then passed in 3,259.993 seconds. The focused
manifest, artifact-size, and CupidBuild suites passed 169 tests in 163.667
seconds, with nine platform skips.

The standalone normal build compiled the complete kernel, drivers, libraries,
user programs, Doom tree, and embedded toolchain with the refreshed seed. Its
first exact-size check stopped because the updated CTXT payload grew
`kernel.bin` from 9,506,932 to 9,507,240 bytes. The two ELF files kept their
exact sizes. After the measured row was updated, a fresh replay passed the
strict CupidDis scan, all 16 exact-size checks, and disk-image staging.

Review then found a stale SMP-manual claim about seed carriage. Correcting it
shortened the flat image by 16 bytes. The gate rejected 9,507,224 bytes
against the 9,507,240-byte row. The final measured policy accepts the corrected
payload; both ELF byte counts remain unchanged.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/smp_trampoline.bin` | 4,096 | `b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90` |
| `kernel/kernel.elf.pass1` | 9,605,148 | `7f83f2283f5f1c0f90cfde71942c7c7cfb596b13ba4e0974e8e843de28e0bc63` |
| `kernel/kernel.elf` | 9,736,220 | `d55d1170293bbc2e2285586f85cb54702a1fefeae90cc497fd474834ae001076` |
| `kernel/kernel.bin` | 9,507,224 | `efd8290cabcdfddeaa9e40e6a3ae4b2fbec4cc640e53b5abbdbecda8379e24f1` |
| `cupidos.img` | 209,715,200 | `9ee5ed43c1f5615077f6da47e579e41e27e31fd8fe7839d6b220e7e031d17635` |

An intermediate private four-vCPU `max`/E1000 frontier passed before the
16-byte manual correction. It covered SMP, networking, storage, graphics,
audio, and in-OS CupidC work.
The 148,124-byte serial log has SHA-256
`169cbc6abdedae37e5d574b714624eeeb60741b45e10e45c6395ede1521bc5ad`.

The final private four-vCPU `max`/E1000 frontier passed from the
review-corrected image. It exercised the same complete runtime surface. The
framebuffer changed 101,820 pixels, and both audio captures were non-silent.
The 149,029-byte serial log has SHA-256
`5b4cd234867bda2c69152d443f8104bd4d2b7974e7b2da45d30185a60849c538`.

The final artifact-policy and Hostbuild boot/SMP group passed 64 tests in
5.496 seconds, with four platform skips. Both seed verifiers, the direct
16-artifact verifier, audit regeneration, and the deterministic audit check
also passed.

## Consequences

Both checked seeds now contain the CupidBuild implementation that can publish
guarded raw bootloader and SMP trampoline images. The normal Make recipes do
not move in this decision; that ownership transfer remains a separate step so
its graph, build, and boot evidence can stand on its own.

No active source changes suffix in this refresh. The toolchain sources are
already `.cc`, and the updated executable inputs do not make any additional C
source CupidC-owned. `TempleOS/` remains read-only reference material.
