# ADR 0237: Promote disk-template into the checked Toolchain seed

## Status

Accepted on 2026-08-05.

## Context

Revision `ba385f763742a77be6952457b0d5c0fb323cfc4f` adds the checked,
transactional CupidObj `disk-template` transform. The preceding seed can
compile the new source, but its own CupidObj image predates the command and
fails the direct carriage test at option parsing. The 19-source build plan
remains unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

## Decision

Promote all five stage-three images as one checked cohort:

| Tool | Bytes | SHA-256 | Producer |
| --- | ---: | --- | --- |
| CupidASM | 445,616 | `1dc9061912f127d231d320940ba781781af663bde83852a613910394709ecc76` | yes |
| CupidC | 2,582,400 | `03084115bcacb1987db5513c8a8be9b7d884029b03ab4b212bf40d997871ae79` | yes |
| CupidDis | 379,648 | `a45fc4c57afd3bb02980e514d58c11588ba3a8bfa2f05ca348fe465cfdaf9749` | no |
| CupidLD | 266,672 | `2bdb6ce6b04678bb89c6bb4f7afac7e152ce6c4a07c4e14e1b3aee0c899008ec` | yes |
| CupidObj | 295,712 | `be5385d8666a625844cb1be5611bd307fa865ca6cf1d50b4e836dfdb3ba45efc` | no |

CupidObj changes from the preceding cohort. The other four images remain
byte-identical, but the manifest continues to bind all five files because
they form one fixed-point generation. The 5,440-byte manifest has SHA-256
`019c77d53ddaf64a382962e1d9588a60046b75a7661f70beb0da7510945f35d0`.
It names the pushed source revision and retains the existing producer lineage,
static i386 Linux target, link orders, and build plan.

## Evidence

The promotion completed in 726.5 seconds. It froze 41 source
inputs with SHA-256
`21a45c2358abf649f0e5e25cebceed320fc1055906cf7c59e40f4ac03baff6c4`.
All 19 C object pairs, startup, and five tool images match between stage two
and stage three. Both stages pass five help cases, thirteen successful
operations, and nine useful failures. CupidObj differs from the preceding
seed; CupidASM, CupidC, CupidDis, and CupidLD match. The 15,057-byte report has
SHA-256
`9b13bc6b98075ed872e48470334fea412914ed71be92fb2aa61070b73858413d`.

The direct checked-seed disk test failed with the preceding image because
its usage text did not list `disk-template`. After promotion, the test builds
the exact 38,400-byte compact template with SHA-256
`a1784fde1833c6cd24f49dff105ff8a70de5b9e619dd8883b4d92d597f241501`.
It also rejects an overlapping kernel and preserves the existing output
sentinel. `make verify-bootstrap-seed` accepts the complete promoted cohort.

An independent post-promotion rebuild completed in 831.8 seconds with host C
and linker commands poisoned. All five seed images match stage two before the
stage-two and stage-three comparison. The rebuilt tools repeat the 5/13/9
behavior matrix. Its 15,056-byte report has SHA-256
`60f24c8c77c81d3771263f102808607e7dcf92b4043cbc9a26c5307f08e0a276`.
The complete checked-seed module passed all 43 tests in 922.204 seconds,
including another full fixed point and the direct disk-template carriage
contract.

The promoted seed also completed the normal four-job OS and partitioned-USB
build. The final boot image is 2,560 bytes with SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`.
The 8,863,576-byte kernel binary has SHA-256
`812650e14616da6a4d41848f9cc909a4e65d5de80c00e102fc9685bd88d2eea1`,
and the 209,715,200-byte disk image has SHA-256
`142b2ebf17bbcabb22aec63911768b21ef292d776423fbb0c5898ec05bfad118`.
A private four-vCPU boot passed in 68.4 seconds. The desktop, SMP runtime, and
network driver initialized, and `/bin/ls.cc` completed through the in-OS
CupidC JIT. The 37,762-byte serial log has SHA-256
`cb70eb25ee70803c0b729cd58e3bce168f16fb1124a21d191165dc816e258192`
and contains no panic, fatal, assertion, exception, or triple-fault marker.

## Rejected alternatives

Replacing only CupidObj was rejected. The unchanged files were rebuilt and
compared as part of the same generation, and the manifest is a five-tool
trust unit rather than a set of independent caches.

Moving the normal disk recipe in this commit was rejected. Seed carriage
proves that the checked command exists. Preserving existing FAT data,
freezing inputs, checking Python parity and drift, and publishing the complete
image atomically need their own production handoff.

## Consequences

Checked-seed CupidObj now builds the pristine disk prefix used by Cupid OS.
The fixed-point behavior matrix is five help cases, thirteen successes, and
nine failures. Python still authors the normal image until the guarded
publisher consumes this command. Python also continues to coordinate the
bootstrap, and Windows continues to run the static i386 seed through WSL.
