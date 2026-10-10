# ADR 0305: Promote and adopt local relative target checks

## Status

Accepted on 2026-08-20.

## Context

ADR 0300 added `--require-local-targets` to source-head CupidDis but kept the
production bootloader and SMP transactions on `--require-known`. The checked
Linux and Windows seeds did not yet carry the option. Adoption required clean
fixed-point reproof from promoted seeds, exact manifest linkage, focused
carriage tests, and fail-closed publication behavior.

The artifact-size policy also covered only the Linux seed. Once Windows owns
output-bearing production calls, all five checked PE tools need the same exact
size sentinel.

## Decision

Promote the stage-four Linux and Windows tool cohorts from revision
`ed6a91ba954881475ac5ab73d5168d292a584c90`. The Linux manifest binds the exact
50-input source snapshot
`a15970287b5f6d6ef5f4e0092d1b460e6b2af2624db4640d2ba5c435e43c1817`
and build-plan SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.
The Windows manifest binds the same source revision and snapshot and names the
promoted Linux manifest as its parent. It does not duplicate the Linux build
plan.

Run CupidDis with `--require-known --require-local-targets --raw` inside both
checked raw-image transactions. The bootloader policy covers nine direct
targets. The SMP policy covers four. A nonzero local-target failure blocks the
atomic replacement and preserves the prior output.

Expand the exact artifact-size policy from nine to fourteen paths. The complete
set is four OS outputs, five Linux seed executables, and five Windows seed
executables. Make runs one `$(ARTIFACT_SIZE_CONTRACT)` command with
`--checked-manifest`. The Python wrapper captures and pins the policy, the
complete Linux policy manifest, all fourteen observations, and the complete
Windows manifest with its five PE files. It uses the captured Windows bytes for native
execution and rereads the manifest and all five PE files before success. The C
policy contract parses the policy and Linux manifest. It does not parse the
Windows manifest.

## Promoted seed identities

The 5,573-byte Linux manifest has SHA-256
`51c8244aa51fce8ccaf7f2eb24df848f02d9269109599cdbdfb0f1f699b5ee65`.

| Linux tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 458,256 | `1eb32e11f85bb18d39a122853dfc1ad4a446ae7516e3d810c60d5f90b43fed8e` |
| CupidC | 2,687,436 | `273f2621401878f673cc3d2987e267cf188ed016ac2005dc9573b3242b225094` |
| CupidDis | 421,652 | `e325206f591e79997f24b1cb8943c682b3362eb72d01dc9ca7dbd38c32531096` |
| CupidLD | 312,792 | `a2119556894903b662d2e131a9a2436b99a3afdd1b1600a3df4d4669569a0295` |
| CupidObj | 392,688 | `99111b5db7586ac4b2ed00005f2fe2e89c66ed48f007d796206b116a088cdf7a` |

The 2,118-byte Windows manifest has SHA-256
`e7367e50f64fac29cb03f8ef530b350408bdc492b6d924f63809cf862b8dd1c7`.
Its `parent_seed_manifest_sha256` value is the Linux manifest SHA-256 above.

| Windows tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 438,784 | `c54bb09f1eb317a23d1680da25c78a5a439bde44654ae8b908ddca11fd7e56d6` |
| CupidC | 2,613,760 | `c768223d4dcd36023e9793b65d86f7bcbd641e921d6a6febf0a255eb7a0e1002` |
| CupidDis | 400,896 | `d29e4e82571c3fe7895bff215f0e32c25c9c98dce8893fa9a000968d979919d0` |
| CupidLD | 296,448 | `9fe3bd4fda9b87d678aa2eb6305e65b706ecdff074b16722faab23ce05cd8e02` |
| CupidObj | 375,808 | `079bc115e74772e6224e4da164115cc5696e357cca0cb1a0583985b88381cb79` |

## Evidence

Linux reproof passed in 1,323.117 seconds. It matched 19 C objects, one startup
object, and five tools, with all five initial comparisons true. Both compared
stages passed 5 help, 19 success, and 18 useful failure cases.

Windows reproof passed in 1,090.390 seconds. It matched 20 C objects, two
assembly objects, and five tools, with all five initial comparisons true. Both
stages passed the 5/5/6 behavior matrix.

The hostbuild bootloader and SMP modules pass all 10 tests in 1.541 seconds.
The active-source local-target checks pass all four tests in 3.328 seconds.
The two seed-carriage checks pass in 1.547 seconds. The Linux and Windows
fixtures each prove one local target with the promoted CupidDis binary. They
accept `EB 00 C3` and reject `EB 7F` with one failure out of one target and the
exact four-reason summary. The separate active-source tests prove the nine
bootloader and four SMP targets.

The final artifact group runs 45 tests in 2.582 seconds, with four
expected Windows skips. Its POSIX runner passes all 15 tests in 0.146 seconds.
Three focused graph tests pass in 34.677 seconds. Both seed verifiers pass in
0.369 seconds. Independent checks matched every manifest artifact size and
SHA-256 value listed above.

This reproof establishes CupidDis carriage. The standalone CupidC seed images
do not contain private kernel parser or ELF-writer changes.

An earlier full seed-module wrapper exceeded 604 seconds without output. That
run remains incomplete and is not counted as a pass. The next run completed
all 86 tests in 2,394.660 seconds and reported
`FAILED (failures=1, errors=4)`. The small source-tree fixtures lacked the
newly required Windows `publication_start.asm` and `publication_runtime.cc`
files. The fixed-point
report also expected the preceding four-byte CupidASM output with SHA-256
`e26807846248e3d1ea2d9dc0980c4329e7b4638db148879849c725e57de64559`
instead of the promoted six-byte output with SHA-256
`95d76dfca4cb4f279611a6ea7a86202898305a4906c6c822c1bfce2ec9ecf06b`.
One PE temporary-file read failed once. Both focused PE validator cases passed
on immediate replay, so the validator did not change.

The shared source-tree helper and the manual fixture now include both Windows
publication inputs, and the fixed-point report expectation uses the promoted
CupidASM result. Six focused freeze and PE tests passed in 0.736 seconds. The
isolated complete fixed point passed in 1,187.863 seconds, with a
1,188.356-second wrapper. The final
`python -B -m unittest -v tests.test_toolchain_bootstrap_seed` run passed all
86 tests in 2,444.917 seconds, with a 2,445.438-second wrapper.

The schema v3 Toolchain publication that consumes these seeds passed in
4,273.533 seconds.
Its 27,069-byte manifest has SHA-256
`69c5b8e62c1e61a8f1a2823d18edff794ae03239be71c881ddd8a190f1377c91`
and records Linux seed manifest
`51c8244aa51fce8ccaf7f2eb24df848f02d9269109599cdbdfb0f1f699b5ee65`.
The native Windows verifier printed
`Cupid Toolchain manifest: ok (21 artifacts)`. The first
`make bootstrap-audit` run against the then-current tree failed after 65.183
seconds because the artifact-size transform recipe lock omitted the new
Windows seed verifier. That historical checkpoint then locked a separate
verifier command followed by `$(ARTIFACT_SIZE_CONTRACT)`. The current Make
recipe has one `$(ARTIFACT_SIZE_CONTRACT)` command with `--checked-manifest`.
The final post-CTXT audit generated in 71.299 seconds, and deterministic check
mode passed in 72.051 seconds. Its active-source digest is
`6ebbbbf7e10e349ba703fc335e87ba5ba40f241d477155f879f2b86b879efd22`.

A pre-final-CTXT build reached the exact-size gate after 668.414 seconds. It
measured `kernel/kernel.elf.pass1` at 9,320,424 bytes and `kernel/kernel.bin` at
9,224,756 bytes while the 9,447,400-byte `kernel/kernel.elf` stayed exact. Only
the pass-one and raw-kernel policy rows moved. The 684.260-second build and
64.601-second guest result below are preceding checkpoint history.

At that preceding checkpoint, the poisoned-host `make -j4 all` passed in
684.260 seconds with
`CC`, `CXX`, `CPP`, `HOSTCC`, `HOSTCXX`, `ASM`, `AS`, `LD`, `AR`, `RANLIB`,
`NM`, `NASM`, `OBJCOPY`, and `STRIP` set to invalid commands. It checked all
fourteen artifacts, preserved the existing FAT contents, and staged
`test_iso/hello.iso`.

| Preceding checkpoint output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/smp_trampoline.bin` | 4,096 | `b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90` |
| `kernel/kernel.elf.pass1` | 9,320,424 | `3f9a1c681fbcfb1aa453e42a9d77ed1069b9a487110c9ec22ac318d278bdd1e6` |
| `kernel/kernel.elf` | 9,447,400 | `92d4e2f890b657c9881eb2184c7f8f9f0e96b18b5b060dbabab17e7ea305b1ce` |
| `kernel/kernel.bin` | 9,224,756 | `4d53e0456d8e63e140f6dcab135765662d12df6e4a83b246409572501f3b4cbd` |
| `cupidos.img` | 209,715,200 | `43409d159d2da70feb20deccda0d79a695c6ab56d87a179fe21a66ab40c5eedd` |
| `bootstrap/artifact-size-policy.json` | 2,960 | `b23bdcb3757a7ddc2a49eeef51cad48cdbd6899f0080c75896b67ef0c665da6e` |

The private four-vCPU e1000 smoke for that checkpoint, with CPU `max`, passed
in 64.601 seconds. It printed the direct-call marker with `calls=6`, the named-callback
marker with `calls=2`, `[feature14-callback-typedef] PASS float4=4 calls=1`,
the overall feature-14 PASS, and JIT completion in order. The 33,219-byte log
has SHA-256
`e39a1905002c2baa483c65eb6e763f4f62907c22f8954873dbb20f4ba5a53e93`.
It contains no rejection markers, and the source image stayed unchanged.

The first post-documentation fully poisoned `make -j4 all` completed every
compile, assemble, link, flatten, and CupidDis check before stopping only at
the expected size mismatches after 680.281 seconds. The 9,324,520-byte
pass-one ELF stayed within policy, while the final ELF measured 9,451,496
bytes and the raw kernel measured 9,228,296 bytes. Only the policy rows for the
final ELF and raw kernel changed. The artifact group then ran 45 tests in 2.582
seconds with four expected Windows skips.

The definitive fully poisoned `make -j4 all` passed in 708.912 seconds. It
checked all fourteen artifacts, preserved the FAT contents, and staged
`test_iso/hello.iso`.

| Final output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/smp_trampoline.bin` | 4,096 | `b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90` |
| `kernel/kernel.elf.pass1` | 9,324,520 | `453c34c8c21498427b0b38564956cd46be4689d456ccfbec682092c2c03be1c4` |
| `kernel/kernel.elf` | 9,451,496 | `718470e9e08ee8eb07aeae7512c6c74c9bcb4b102290fdcf237d956cc9afc616` |
| `kernel/kernel.bin` | 9,228,296 | `8e5d7c172814dd5db51a16acd41bf0436cb613a7da5f67511622c4b6517e0dbb` |
| `cupidos.img` | 209,715,200 | `8a7a67e3da4dd8e256bbe1f69d511b59dc9f669cb6026acbeca055c998889195` |
| `bootstrap/artifact-size-policy.json` | 2,960 | `c8f320020a28ef914c38871e01b175bf6f15db7459ca9a7f54554e412ecc5b85` |

The strong full private frontier used e1000, four `max` vCPUs, SMP, a private
image, and the USB fixture. It passed in 801.490 seconds. The 640x480
framebuffer changed 96,925 pixels. AC97 produced 32,722,102 stereo 44,100 Hz
frames with a peak of 25,600, and the PC speaker produced 73,533 stereo 44,100
Hz frames with a peak of 8,415. The direct-call,
named-callback, typedef-callback, overall feature-14, and JIT markers each
appeared once and in order. The 150,376-byte log has SHA-256
`73f77abc06357bf5d7185b40825d9d197e9954014ccf09362e9a1d219cc30f02`
and no rejection markers. The source image stayed unchanged at SHA-256
`8a7a67e3da4dd8e256bbe1f69d511b59dc9f669cb6026acbeca055c998889195`.

## Rejected alternatives

Do not enable the production option before both execution seeds carry it. A
source-only command-line capability cannot guard a production transaction.

Do not infer intended source labels from decoded destinations. This policy
proves only that a constant relative target lands on an instruction start in
same-mode code.

Do not treat a timed-out full seed module as evidence. A wrapper timeout
without a result proves neither success nor failure of the suite. The later
completed red run and final green replay are the retained module evidence.

Do not leave Windows seed sizes outside the exact policy after Windows becomes
the selected production execution cohort.

## Consequences

The bootloader and SMP raw-image publishers now reject an outside-image, data,
wrong-mode, or mid-instruction local target before atomic replacement. The
owner and transform counts do not change.

Far pointers, indirect transfers, ELF targets, and source-label identity remain
outside this rule. Host Python still owns locking, pinned filesystem access,
drift checks, and atomic publication. `TempleOS/` remains untouched reference
material.
