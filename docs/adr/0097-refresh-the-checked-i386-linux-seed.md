# Refresh the checked i386 Linux seed from stage three

- Status: Accepted
- Date: 2026-07-24

Current status: ADR 0098 consumes this refreshed seed for all 20 production
kernel crypto sources. The staged refresh evidence below remains unchanged.

## Context

The first checked seed in ADR 0092 could rebuild the complete five-tool
Toolchain, but its CupidC image predated typed null conversion, address decay
for external arrays with unspecified bounds, and the active CSPRNG GNU
assembly subset. Compiler head could emit all 20 crypto sources, while the
normal build still had to use the older seed and leave four of them on the
host compiler.

A replacement seed is a change to the bootstrap trust root. Copying the
native compiler candidate would have lost the lineage established by the
checked seed. Copying a stage-two file would have worked after a fixed point,
but it would not have represented the final generation built by the staged
bootstrap. The replacement needed to come from the checked seed's own stage
three and then reproduce itself from the new manifest.

## Decision

The previous checked seed ran `make bootstrap-from-seed` with a fresh
dedicated output directory against exact source revision
`b04c5b5ead1be504669ad8f0f84b3531eda3df9c`. Both `CC` and `LD` named
commands that do not exist, so an accidental host compiler or linker fallback
would have stopped the run.

That transition rebuilt all 19 C objects, the startup object, and five static
tools in stages two and three. Every pair matched, and both stages agreed on
five help paths, ten successful operations, and six useful failures. The
40-input source snapshot has SHA-256
`b616fb0a65a9e31824a817356ec665a0acaf5448f8e8aa4b4fa23aff06f24fe3`.
The previous manifest has SHA-256
`ca9c1cca497717317ffdc7af3f22eb330074b1fd5656ec4625c379363bb80412`,
and the transition report has SHA-256
`40f7418c7067f079bd7f1a4a13cb12d1d523e70ee82d99d98c14e65a72026925`.
The run took 451.5 seconds.

The five files promoted into `bootstrap/seeds/i386-linux/` are the actual
stage-three images from that run:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,060 | `00f684ca5ca1e2ba36763e6810c65fea8b3786d40f6008d635751a1f2c2b6db0` |
| CupidDis | 366,968 | `67fcdbcf8a7924e37f00ec571bb5a4dbfbf4897c9743e9f3a3bbcaf0ea20ca60` |
| CupidLD | 262,388 | `373ed96803dcfb0005b8b3b1d49ca1313396ee11e17521aad6402f487cdd97e5` |
| CupidObj | 182,704 | `1f48c3d7b5f80d3e33eb9268c087111e8fa54eb390c24368a09f7ec2981c0030` |
| CupidC | 1,883,836 | `f412a39f204380de8986d6dc3c3a8d6feecf4c40990c40b31634e58d254624df` |

The four non-compiler tools were already at the same fixed point and did not
change. CupidC is the only new binary.

The manifest keeps the existing v1 target and build-plan schema. Its build
plan remains SHA-256
`7fa10ec56ee33b3e3fbc6d2320a6338909cd51c0fcf9c6f9170acb1081f50ec0`.
Its provenance now records:

- source revision `b04c5b5ead1be504669ad8f0f84b3531eda3df9c`;
- seed generation `stage-three`;
- stage-two CupidC, CupidASM, and CupidLD from the checked-seed bootstrap as
  the C, assembly, and link producers;
- `make bootstrap-from-seed` as the fixed-point command; and
- a passing fixed-point result.

The verifier pins those values instead of accepting the former host-built
lineage. Focused negative tests replace the source revision, generation,
result, producer lineage, and command one at a time and require verification
to fail before execution.

The refreshed seed then repeated the complete poisoned-host bootstrap in a
fresh directory. All five seed images matched stage two, and stage two matched
stage three at every C object, startup object, and tool image. All 21 behavior
checks passed. The new manifest has SHA-256
`90f30ede183176337cbe56463e7f7321291d7b87255c6692784ed6c57634dd6e`;
the reproof report has SHA-256
`9757b0e4318c50acde1f42ddedce60a17ab611d6578cce175806150312412ab8`.
The reproof took 461.1 seconds.

The seed-to-stage-two equality assertion is scoped to the recorded source
snapshot. A later Toolchain source change may legitimately make an older seed
differ from stage two while still reaching a stage-two to stage-three fixed
point. This keeps ADR 0092's distinction between seed provenance and live
source inputs.

## Rejected alternatives

Promoting the native static CupidC candidate was rejected. Its bytes matched
the staged result, but its producer lineage still passed through a host
compiler and linker.

Promoting a stage-two image was rejected. Stage two and stage three matched,
but stage three is the final generation produced by the checked staged build
and makes the transition unambiguous.

Keeping the old host-built provenance was rejected. The new file was produced
by stage-two Cupid tools from the checked seed, and recording anything else
would make the manifest misleading.

Making seed-to-stage-two equality a permanent rule was rejected. That would
turn the historical bootstrap root into a mandatory rolling binary after
every Toolchain source edit and would contradict the report's separate seed
and live-source identities.

## Consequences

The checked seed now contains the CupidC capability that compiler head proved
for all 20 crypto sources. A clean checkout can rebuild that exact compiler
and the other four static tools without GCC, Clang, NASM, a host linker,
`nm`, or `objcopy`.

This refresh changes the bootstrap input, not the normal image's source-owner
list. The root build still assigns 16 crypto sources to CupidC until the
production frontier and four Make rules move in a separate boot-tested
change. Native contract runners, hosted development commands, most kernel C,
all user C, Python orchestration, and the Windows WSL bridge remain host
dependencies.

ADR 0092 remains the record of the first checked seed. This decision
supersedes its current artifact inventory and provenance values without
rewriting that historical evidence.
