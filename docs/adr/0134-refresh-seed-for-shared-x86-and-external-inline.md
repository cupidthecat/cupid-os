# ADR 0134: Refresh the seed for shared x86 and external inline support

## Status

Accepted on 2026-07-27.

## Context

The checked i386 Linux seed came from revision
`fe3bdfe451d7e019a052c7c8ba53f1f9f3f1fb3d`. Later compiler and x86 work
added two source-driven capabilities:

- ADR 0131 finalized C11 external inline definitions across a complete
  translation unit. Compiler head could compile unchanged
  `kernel/audio/nuked_opl3.c`, but the checked compiler could not.
- ADR 0132 added the three-operand immediate `IMUL` family to the shared x86
  encoder and decoder. That changed the CupidASM and CupidDis closures.

The normal build could not use the external inline rule until a compiler
produced by the old seed rebuilt the complete Toolchain, reached a fixed
point, and became the new checked seed.

## Decision

Promote the complete stage-three tool set built from revision
`a14ce50fa97264eeba2da3f913b643a12517a78b`.

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,060 | `92ab7705b7a5929185730aa981e40f2b5d6e5100d5913490cb79f930f9de5a5b` |
| CupidDis | 366,968 | `1f264bd895cee544834f10c70ef145492ef833dc759df75c601e90453e429b75` |
| CupidLD | 262,388 | `373ed96803dcfb0005b8b3b1d49ca1313396ee11e17521aad6402f487cdd97e5` |
| CupidObj | 182,704 | `1f48c3d7b5f80d3e33eb9268c087111e8fa54eb390c24368a09f7ec2981c0030` |
| CupidC | 2,084,592 | `f65bf21e999d09abf5028971000e5f2f4e58a82aed21c286fcd5c24ec9f68ab1` |

CupidASM, CupidDis, and CupidC change bytes. CupidLD and CupidObj remain
byte-identical, but the manifest treats the five tools as one seed. Promotion
therefore replaces and verifies the complete set.

Keep the 19-source `.cc` plan, five link orders, producer lineage, target ABI,
and build-plan SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.
The refreshed 5,440-byte manifest has SHA-256
`de82efad0d42d5e45f6b2c4771f04ebe3838e1c71359ea32dc0d7652aa674b74`.

The verifier pins the new source revision. The seed contract also pins the
40-input source snapshot used for this transition so the promoted-seed
reproof must match all five checked images against stage two.

## Evidence

The previous checked seed built the current 19-source Toolchain union with
`CC`, `CXX`, `CPP`, `HOSTCC`, `HOSTCXX`, `ASM`, `LD`, `AR`, `NM`, and
`OBJCOPY` set to commands that do not exist. The 40-input snapshot has
SHA-256
`7db570c8f0975d10a00930fdd5e17ec321a0e45e83cb6e365c824dc89d4df9d8`.

All 19 C objects, the startup object, and all five tools match between stages
two and three. Both stages pass five help cases, ten successful operations,
and six useful failures. The 14,881-byte transition report has SHA-256
`14ef822581df1b3ccb66533c58f9f521b60c73d7df7272d015c6b08fb4f24073`.
The old seed matches stage two for CupidLD and CupidObj. The other three
images differ as expected.

After promotion, the focused checked-seed fixed-point test passed in 611.116
seconds. Its snapshot lock requires every promoted seed image to match stage
two before it accepts the stage-two and stage-three equality and the 21
behavior cases.

## Rejected alternatives

Promoting only CupidC was rejected because the shared x86 change also affects
CupidASM and CupidDis, and the manifest binds all five tools.

Keeping the old CupidASM and CupidDis files was rejected even though their
interfaces did not change. Their checked source closure changed, so the seed
must contain the images produced by the accepted stage-three build.

Using a native compiler candidate was rejected because its producer lineage
passes through a host compiler and linker.

Moving Nuked OPL3 into the production cohort in this commit was rejected. A
seed refresh changes a trusted bootstrap input. Production ownership also
needs a source rename, a closed recursive header set, deterministic frontier
evidence, a clean image build, and runtime proof.

## Consequences

The checked seed now carries the shared immediate multiply family and C11
external inline finalization. A clean checkout can reproduce these tools
without a host code generator.

This decision changes the bootstrap input, not normal-build ownership. Nuked
OPL3 remains host-built until its separate production transfer passes. Native
contracts, hosted development commands, 93 normal host-built root objects,
Doom, the remaining vendored code, Python orchestration, and the Windows WSL
seed bridge remain dependencies.
