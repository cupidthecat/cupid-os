# ADR 0240: Promote iso-fixture into the checked Toolchain seed

## Status

Accepted on 2026-08-05.

## Context

Revision `5452538ff42efe21e20d2e243cc76cacdbd05b92` adds the bounded,
transactional CupidObj `iso-fixture` transform. The preceding seed can compile
the new source, but its CupidObj image predates the command. A direct carriage
test therefore stopped at option parsing before promotion.

The 19-source build plan remains unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.
The source closure still contains 41 files.

## Decision

Promote all five stage-three images as one checked cohort:

| Tool | Bytes | SHA-256 | Producer |
| --- | ---: | --- | --- |
| CupidASM | 445,616 | `1dc9061912f127d231d320940ba781781af663bde83852a613910394709ecc76` | yes |
| CupidC | 2,582,400 | `03084115bcacb1987db5513c8a8be9b7d884029b03ab4b212bf40d997871ae79` | yes |
| CupidDis | 379,648 | `a45fc4c57afd3bb02980e514d58c11588ba3a8bfa2f05ca348fe465cfdaf9749` | no |
| CupidLD | 266,672 | `2bdb6ce6b04678bb89c6bb4f7afac7e152ce6c4a07c4e14e1b3aee0c899008ec` | yes |
| CupidObj | 350,348 | `394c7bcfe04baf3f032a9b85ce8d908268dde9ec6527840665bc77e4b2d02b14` | no |

CupidObj changes from the preceding cohort. The other four files are
byte-identical, but the manifest binds all five because they belong to one
fixed-point generation. The 5,440-byte manifest has SHA-256
`5a27d7a4a65637da413756a6c154bf44ac0879c7d941881fbd3b995733a805a8`.
It names the pushed source revision and retains the static i386 Linux target,
producer lineage, link orders, and build plan.

Extend the fixed-point behavior gate at the same boundary. Both generated
stages must expose `iso-fixture`, build one exact seven-entry nested image,
and preserve an existing output when the manifest names a file below an
unrepresented parent. The behavior matrix is now five help cases, fourteen
successful operations, and ten useful failures.

## Evidence

The transition completed in 674.3 seconds with `CC`, `CXX`, `CPP`, `HOSTCC`,
`HOSTCXX`, `ASM`, `LD`, `AR`, `NM`, and `OBJCOPY` set to commands that cannot
run. It froze 41 source inputs with SHA-256
`bac03a6d2b36dff48983221aae209a6688b408232b5d5373b6c2128082228a66`.
All 19 C object pairs, startup, and five tool images match between stage two
and stage three. The preceding CupidObj differs from stage two; the other four
seed images match. The 15,058-byte transition report has SHA-256
`c40f5a8d8fb7bc63d237e2fd07636e0c10c9e69196d1ac684927eb9e8551ee39`.

The direct checked-seed ISO test first failed because the preceding usage text
did not list `iso-fixture`. After promotion, it reproduces the tracked
61,440-byte image with SHA-256
`40359c1cec72219f21e87ce71b31e621209036042440e1b38c5e59de157e0fb6`.
It also rejects `lost/payload.bin` without a represented `lost` directory and
leaves the old output sentinel intact. `make verify-bootstrap-seed` accepts
the promoted manifest and all five static ELF32 files.

An independent post-promotion rebuild completed in 675.6 seconds under the
same poisoned environment. Every promoted seed image matches stage two, and
stage two again matches stage three across the complete 5/14/10 behavior
matrix. Its 15,057-byte report has SHA-256
`29ad7ce56f2311855feb96a387c3d77859a39b07dcc90d2ea0e93cfe532444f0`.
The complete checked-seed module then passed all 44 tests in 750.771 seconds,
including another full fixed point and the direct ISO carriage contract.

The active graph regenerated in 58.4 seconds, and its independent stale check
passed in 59.0 seconds. The census remains 719 active inputs, 449 transforms,
255 feature records, and 25 accounted unreachable files. The active-source
digest remains
`b6a340db80dfb5d95eaf429b386aa8f5f6a359091e1f7b879ca38f72f7b6de02`.
The 2,558,331-byte JSON has SHA-256
`3463c86f1bbb8158ab2ee84d50612a37580b187ebaae8f1a3c2a9cbc80d9e246`,
and the 12,196-byte summary has SHA-256
`caa636e630cb9b55c9be633c31b45ad1385d2bde3d8cdba2d228eaae694e567f`.
The complete 68-test graph-audit module passes in 560.234 seconds.

## Rejected alternatives

Replacing only CupidObj was rejected. The unchanged files were rebuilt and
compared in the same generation, and the checked manifest is a five-tool trust
unit rather than a set of independent caches.

Leaving the new operation outside the fixed-point behavior gate was rejected.
Source compilation alone would not prove that both runnable stages expose the
command, retain its exact bytes, or preserve an old output on failure.

Moving the production ISO recipe in this commit was rejected. Seed carriage
proves that the command is available. Filesystem snapshots, independent
Python parity, live-input drift checks, and atomic publication remain a
separate handoff.

## Consequences

Checked-seed CupidObj now carries deterministic ISO fixture authoring. Python
continues to author the normal image until the guarded publisher consumes this
command. No normal-build transform changes ownership in this promotion, and
no ordinary C or assembly source changes ownership, so no `.c` to `.cc`
rename is due. `TempleOS/` remains untouched reference material.
