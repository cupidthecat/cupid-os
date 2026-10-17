# ADR 0138: Refresh the seed for floating data and comparisons

## Status

Accepted on 2026-07-27.

## Context

ADRs 0136 and 0137 added two source-driven CupidC capabilities after the
previous i386 Linux seed was promoted:

- Static-duration `float` and `double` initializers retain exact target bits.
- All six floating comparisons handle matching and mixed widths, including
  ordered and unordered IEEE inputs.

Compiler head could compile the unchanged JPEG decoder and glyph rasterizer,
but the checked compiler at revision
`a14ce50fa97264eeba2da3f913b643a12517a78b` could not. The normal build
could not transfer either source until the old seed rebuilt the complete
Toolchain, reached a fixed point, and became the new checked seed.

## Decision

Promote the complete stage-three tool set built from revision
`7e7029637ef22a4f18c382ffb225fd6a2ea84b85`.

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,060 | `92ab7705b7a5929185730aa981e40f2b5d6e5100d5913490cb79f930f9de5a5b` |
| CupidDis | 366,968 | `1f264bd895cee544834f10c70ef145492ef833dc759df75c601e90453e429b75` |
| CupidLD | 262,388 | `373ed96803dcfb0005b8b3b1d49ca1313396ee11e17521aad6402f487cdd97e5` |
| CupidObj | 182,704 | `1f48c3d7b5f80d3e33eb9268c087111e8fa54eb390c24368a09f7ec2981c0030` |
| CupidC | 2,109,488 | `39a5783a5ba07a4891b887ea36a5686098dc9ca128b29419aea1e0c2cd8ee86e` |

Only CupidC changes bytes. The manifest still binds all five tools as one
seed, so promotion copies and verifies the complete set.

Keep the 19-source `.cc` plan, five link orders, producer lineage, target ABI,
and build-plan SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.
The refreshed 5,440-byte manifest has SHA-256
`1696d203fa319a98c23eb5f2b15b0764459bf7d495ae52918ae32a0a1606860b`.

## Evidence

The previous seed built the current 19-source Toolchain union with `CC`,
`CXX`, `CPP`, `HOSTCC`, `HOSTCXX`, `ASM`, `LD`, `AR`, `NM`, and `OBJCOPY`
set to commands that do not exist. The 40-input snapshot has SHA-256
`230bffbf41d645e50b9944a179febd1d7920e1cfbc92b98e24a752d93192a7b8`.

All 19 C objects, the startup object, and all five tools match between stages
two and three. Both stages pass five help cases, ten successful operations,
and six useful failures. The 14,879-byte transition report has SHA-256
`e25eebc37719adcbe541b3fc048384ca4735ddf45af59d0e1234604ffdfc0064`.
The run took 610.9 seconds. The old seed matches stage two for CupidASM,
CupidDis, CupidLD, and CupidObj. CupidC differs as expected.

After promotion, a second poisoned-host bootstrap used a fresh output tree.
Every checked image matches stage two, and every stage-two object and image
matches stage three. The same 21 behavior cases pass over the same source
snapshot. The 14,878-byte reproof report has SHA-256
`f4995f4d7a0749cd02d71e3ab99166074665f22f68e06455730eea3dfdc5edf0`.
The reproof took 610.6 seconds.

Before the transition, the native-generation five-tool fixed point passed in
630.1 seconds. `make verify-bootstrap-seed` accepts the promoted manifest and
all five static ELF32 images. The complete seed module passed all 14 tests in
592.649 seconds, including another independent fixed point, exact provenance,
ELF entry checks, source and seed mutation rejection, frozen-input
independence, and private WSL staging.

## Rejected alternatives

Promoting a native compiler image was rejected because its producer lineage
passes through a host compiler and linker.

Promoting only CupidC was rejected because the manifest defines one
five-tool trust root. The complete set must be copied, inventoried, and
verified even when four images remain byte-identical.

Moving JPEG and glyph rasterization into the production cohort in this commit
was rejected. A seed refresh changes a trusted bootstrap input. Production
transfer also needs `.cc` renames, closed recursive input sets, poisoned
normal-build recipes, image linkage, and runtime evidence.

## Consequences

A clean checkout now carries static floating constant data and all six
floating comparisons in the checked compiler. It can reproduce itself and
the other four checked tools without a host code generator.

This decision changes the bootstrap root, not normal-build ownership. The JPEG
decoder and glyph rasterizer remain host-built until their separate production
transfer passes. Native contracts, hosted development commands, 92 normal
host-built root objects, Doom, vendored C, Python orchestration, and the
Windows WSL seed bridge remain dependencies.
