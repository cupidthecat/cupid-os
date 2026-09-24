# ADR 0158: Promote the current toolchain seed

## Status

Accepted on 2026-07-28.

## Context

The checked i386 Linux seed no longer matched compiler head. CupidC had
gained the audited Doom frontier, the current GNU entity attributes, x87 and
SSE memory forms, descriptor and segment assembly, Task 23 file-scope
wrappers, and the exact naked IPI entries. CupidASM and CupidDis had also
moved from 583 to 587 shared x86 catalogue rows.

Those changes passed their focused contracts and the source-head fixed
point, but a normal build could not rely on them until the checked seed
carried the same bytes. A bootstrap run from an uncommitted tree was useful
engineering evidence, not a suitable seed provenance record. The promotion
had to start from a clean, named revision and had to prove that no host code
generator filled a gap.

## Decision

Revision `c00b3494014ca0a5f41143caa7e713e46b2ad3ec` is the source revision for
the refreshed seed. A fresh staged bootstrap built its 19 C objects, one
startup object, and five tool images with `CC`, `CXX`, `CPP`, `HOSTCC`,
`HOSTCXX`, `ASM`, `LD`, `AR`, `NM`, and `OBJCOPY` set to commands that
cannot run.

The stage-two and stage-three artifacts matched byte for byte. The
stage-three tools became the new contents of `bootstrap/seeds/i386-linux/`,
including the two images whose bytes did not change. The manifest records
the exact source revision, sizes, hashes, target ABI, build plan, and link
orders.

The promoted seed then rebuilt the same clean source snapshot in a second
fresh output directory under the same poisoned environment. This time every
initial seed image also matched its stage-two replacement. That reproof is
the acceptance gate for the promotion.

The checked images are:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,104 | `d57e4f0494aef294045c633b12e4db3f14e879102ac4e528fe70d6a5f089c7e7` |
| CupidC | 2,320,544 | `fe4e99837053332e32624208bfceddc60e2be9cdcea5bdacb5b174e6b432cdbb` |
| CupidDis | 371,108 | `e67157c4883f4164635b6084bc8c6475b77fd9d051196f4a553ae64346948d70` |
| CupidLD | 262,388 | `373ed96803dcfb0005b8b3b1d49ca1313396ee11e17521aad6402f487cdd97e5` |
| CupidObj | 182,704 | `1f48c3d7b5f80d3e33eb9268c087111e8fa54eb390c24368a09f7ec2981c0030` |

## Evidence

Both bootstrap runs froze 40 inputs with source-snapshot SHA-256
`e90771d9cb9429b15f136f008ba9c5d8d982f3fd867a57bb520b5aa7b6b32535`.
Each run matched all 19 C object pairs, the startup pair, and all five tool
images between stages two and three. Each generation passed five help
cases, ten successful operations, and six useful failure cases.

The clean promotion candidate completed in about 557 seconds. Its
14,881-byte report has SHA-256
`3522f6ed4baabf91e160db457c5c66f4478253c78f39cb384673de474ed86879`.

The post-promotion reproof also completed in about 557 seconds. Its
14,878-byte report has SHA-256
`839a2584e88fab6a46c325bad108ffeb54765590b04097711eee289a5844b569`.
All five `initial_seed_matches_stage_two` values are true. The 5,440-byte
manifest has SHA-256
`57f7a9dc390ace86e46de9a13b5b8d19330fd5c91fe34d502a550e5d04ed7ee8`,
and the standalone seed verifier accepts all five files.

## Rejected alternatives

Keeping the older seed was rejected because normal CupidC recipes would
still be unable to use capabilities already proven at source head.

Promoting from the earlier uncommitted bootstrap was rejected because its
source revision could not describe the tested tree.

Promoting only the three changed files was rejected. Copying all five
stage-three images makes one generation the unambiguous seed, even when a
tool happens to remain byte-identical.

Moving production source ownership in the same commit was rejected. Seed
identity is one auditable boundary; build-graph ownership, source suffixes,
image construction, and runtime behavior need their own proof.

## Consequences

The checked seed now carries the source-head capabilities listed above and
the complete 587-row shared x86 catalogue. Bootstrap test locks name the new
compiler image, source revision, manifest, and transition snapshot.

This decision does not change a normal production recipe. The active FPU,
per-CPU, and SMP translation units remain host-owned with `.c` suffixes
until a later commit rebuilds them through the checked wrapper and passes
the image and runtime gates. Named GNU operands and broader general assembly
forms also remain open.

`TempleOS/` remains untouched reference material.
