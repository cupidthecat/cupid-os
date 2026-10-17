# ADR 0228: Promote Cupid types and SHRD into the checked seed

## Status

Accepted on 2026-08-04.

## Context

Revision `bd64a39d1b419df3fb3182c33869084f4bc09c2c` adds Cupid's native
type spellings to the shared declaration frontend and adds canonical SHRD to
the shared x86 model. The same revision transfers the ISO spanning fixture to
checked CupidASM. It was committed and pushed before the promotion candidate
was built.

The preceding seed could compile the new source, but its generated CupidC,
CupidASM, and CupidDis images did not contain those capabilities. The
19-source build plan remains unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

## Decision

Promote all five stage-three images as one checked cohort:

| Tool | Bytes | SHA-256 | Producer |
| --- | ---: | --- | --- |
| CupidASM | 445,616 | `1dc9061912f127d231d320940ba781781af663bde83852a613910394709ecc76` | yes |
| CupidC | 2,578,244 | `b652adc07442df04fa577fb7987598619cb573c5d932d639288ddddc939f622f` | yes |
| CupidDis | 379,648 | `a45fc4c57afd3bb02980e514d58c11588ba3a8bfa2f05ca348fe465cfdaf9749` | no |
| CupidLD | 266,672 | `2bdb6ce6b04678bb89c6bb4f7afac7e152ce6c4a07c4e14e1b3aee0c899008ec` | yes |
| CupidObj | 270,700 | `a8de7de19d1ffbec90f0603f0f796f4a03fa74b8181c62f0f395b22a52423d1d` | no |

CupidC, CupidASM, and CupidDis change from the preceding cohort. CupidLD and
CupidObj remain byte-identical, but the manifest continues to verify all five
files as one trust unit. Producer flags and link orders do not change.

The 5,440-byte manifest has SHA-256
`7e7da98d2adddbf59fbd7c4da7af7375e08c10147b8c802a2d4a816161f647ea`.
It names the pushed capability revision, the static i386 Linux ABI, the
existing producer lineage, and the unchanged build plan.

## Evidence

The transition completed in 618.9 seconds. It froze 41 inputs with SHA-256
`206a8124bbbc084153827308581131945aa62272e025edfcd33db910026363b5`.
All nineteen C objects, startup, and five tool images matched between stages
two and three. Both stages passed five help cases, eleven successful
operations, and seven useful failures. CupidC, CupidASM, and CupidDis differed
from the preceding seed; CupidLD and CupidObj matched. The 15,050-byte report
has SHA-256
`f633f186baea1cea07055d99b676a046c504bbadbf6169d080ba8a7b54c50188`.

The post-promotion reproof completed in 615.8 seconds. All five promoted seed
images matched stage two before the same objects, tools, and behavior matrix
matched between stages two and three. Its 15,047-byte report has SHA-256
`fd94d1699f968d4ff730ad93e6950c0fcf256b018ac132de8d17d2d00eb91051`.

The checked-seed module first found one stale expected snapshot digest after
the complete internal fixed point had passed. The expectation was updated to
the promoted 41-input digest. The clean rerun passed all 41 tests in 717.625
seconds. Its new carriage case assembles and disassembles exact `66 67` SHRD
forms through explicit `a32` and `a16` prefixes, rejects a non-`CL` register
count, and preserves an existing output on failure.

The promoted seed rebuilt and published the complete 20-artifact Toolchain
contract cohort in 2,710.4 seconds. All sixteen objects and fifteen contract
executables matched between stages two and three, the hosted runtime passed,
and the 45 live inputs still matched their frozen copies. The 18,231-byte
cohort manifest has SHA-256
`aec70359a82e63912c8f986c44a42331dec63b357cc68313ee4ecd57e6f55cf4`.
The published frontend and x86 contracts then reported `cupid-types: ok` and
`double-shift: ok` when run directly.

The canonical active-build audit regenerated in 93.3 seconds, and its
independent drift check passed in 94.5 seconds. It records 719 active source
inputs, 449 reachable transforms, 255 distinct feature requirements, and 25
unreachable source-like files. Its active-source digest is
`62af2e41e3f4f7a95c0248c958a8b0404ed28f499a6dd4c7a2baf9d834e269ba`.
The 12,196-byte summary has SHA-256
`dff18237312b8edbd247e46f7ae379d96ec44629d368cff47a93de5a546c9527`.
The 2,557,086-byte JSON audit has SHA-256
`c16fc7d5b45e9960c2054bf06ddf6a6d8e3fb704d4dc311cb54202815c24159a`.

The normal `make -j4 all` build passed in 922.8 seconds with all production
CupidC, CupidASM, CupidDis, CupidLD, and CupidObj commands selected from the
promoted manifest. Its principal outputs were:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `test_iso/hello.iso` | 61,440 | `40359c1cec72219f21e87ce71b31e621209036042440e1b38c5e59de157e0fb6` |
| `kernel/kernel.elf.pass1` | 8,929,588 | `6026c29b025aeff88ec3536ece4973d0469800e3a327fac475846827e4404afc` |
| `kernel/cpu/ksyms_data.cc` | 379,312 | `6c4472c3f772581f6b5319faa204eaa3484ebe0a2d88dcfb15f14600961bd986` |
| `kernel/cpu/ksyms_data.o` | 114,836 | `2e33a15e64be9a9d48010e762381c67d3f7b01173cbb8e9294b09b324ac0e1a4` |
| `kernel/kernel.elf` | 9,044,276 | `3d9c08d9d0fc0f385311428ee56eb54415c1f469074d7e2a9181779615523fe7` |
| `kernel/kernel.bin` | 8,835,976 | `08b55c67c01b0590c4ed5c47b074b92c6636006376db241422f3be17c9505d57` |
| `cupidos.img` | 209,715,200 | `11f95aae99c7dfc7d66381b69f8de6ea70fd1b389040a1fb939d1650640226f5` |

A private four-vCPU e1000 frontier passed in 345.1 seconds. It covered the
fixed in-OS CupidC and CupidASM commands, cross-sector ISO reads, Browser's
numeric self-test, SMP, RDRAND, crypto, USB HID reattachment, six EHCI storage
lifetimes, and clean JIT stack completion. The 640 by 480 framebuffer changed
96,557 pixels. AC97 produced 11,994,266 stereo frames at 44.1 kHz with peak
25,600, and the PC speaker produced 77,137 stereo frames with peak 24,831. The
113,876-byte serial log has SHA-256
`75346112842fe30283d4a899a8c9100b370b3e52fe7704279fff7d3eabe08fa9`.

## Rejected alternatives

Keeping the preceding seed was rejected because checked runs would continue
to use a compiler and shared instruction catalogue that predated the new
source behavior.

Replacing only the three changed executables was rejected because the five
images and their manifest form one trust unit. The two unchanged images were
copied from stage three and verified with the cohort.

Treating implicit non-default address registers as CupidASM syntax was
rejected during the carriage test. CupidASM already spells explicit address
overrides as `a32` and `a16`, and active boot source uses that rule. The test
now exercises those prefixes instead of adding a second implicit rule.

Promoting the pre-commit candidate was rejected. A fresh transition was run
only after the capability revision matched the pushed branch.

## Consequences

The checked seed now recognizes Cupid's sized scalar, Boolean, and vector type
spellings through its shared frontend. Its shared x86 catalogue has 596 forms,
245 canonical mnemonics, 64 registers, and fingerprint `DA15E97F`. CupidASM
can encode the represented SHRD family, and CupidDis decodes the active
checked-CupidC SHRD sites without fallback rows.

The ISO fixture transfer does not change seed behavior beyond using the
promoted CupidASM on later builds. Python still coordinates the bootstrap,
Windows still runs the static i386 tools through WSL, and the private kernel
compiler remains the embedded JIT and AOT path.
