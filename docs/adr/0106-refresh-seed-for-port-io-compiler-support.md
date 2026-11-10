# Refresh the checked seed with port-I/O compiler support

- Status: Accepted
- Date: 2026-07-24

## Context

The checked i386 Linux seed at revision
`6639799ee3da19b077c890223e3340fc5e05e7ba` carried operand-free GNU
assembly, the per-CPU pointer output, and the active integer atomics. It did
not carry the width-aware fixed-register and string-I/O forms implemented in
ADR 0105.

Compiler revision `d76f543948621ea04520d019fad9aae670f17f11` parses,
lowers, and emits all eight unchanged port helpers. Its self-host object
proof showed that the old checked seed could compile the updated CupidC
closure, but that proof did not replace the repository's bootstrap root.

A seed replacement changes a trusted binary input. The new compiler therefore
had to come from the old checked seed's stage three, retain the established
producer lineage, and reproduce itself from the refreshed manifest before
normal-build ownership could use it.

## Decision

The old seed ran the complete staged bootstrap against exact source revision
`d76f543948621ea04520d019fad9aae670f17f11`. The output lived in a fresh
private directory under the captured repository root. Both `CC` and `LD`
named commands that do not exist, so an accidental host compiler or linker
fallback would have stopped the run.

The transition rebuilt all 19 C objects, startup, and five tools in stages
two and three. Every stage pair matched. Both stages also agreed on five
help paths, ten successful operations, and six useful failures. The
40-input snapshot has SHA-256
`cf17ed01addfb3ce6743785b14d523b86b2eedf34579df10aa4682cb1642ab35`.
The old manifest has SHA-256
`f8bd649a1f87ecdd368c22b4149315b4fa48c98fd3f59aaeef706c802d803f33`.
The 14,860-byte transition report has SHA-256
`b5c02487a41888142c221150b58ade3e2a55671afc8665e579cecb90122ef99e`.
The run took 470.6 seconds.

The transition report records the expected trust-boundary change. CupidASM,
CupidDis, CupidLD, and CupidObj still matched the old seed at stage two.
CupidC did not. Its stage-two and stage-three images matched each other.

The complete stage-three set was promoted:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,060 | `00f684ca5ca1e2ba36763e6810c65fea8b3786d40f6008d635751a1f2c2b6db0` |
| CupidDis | 366,968 | `67fcdbcf8a7924e37f00ec571bb5a4dbfbf4897c9743e9f3a3bbcaf0ea20ca60` |
| CupidLD | 262,388 | `373ed96803dcfb0005b8b3b1d49ca1313396ee11e17521aad6402f487cdd97e5` |
| CupidObj | 182,704 | `1f48c3d7b5f80d3e33eb9268c087111e8fa54eb390c24368a09f7ec2981c0030` |
| CupidC | 1,946,320 | `57bea3f86ad601254539d96081473a8309400eedfef46c03e2ad34d0f195351c` |

Only CupidC changed bytes, but promotion and verification covered all five
files. The manifest keeps the v1 schema, static i386 Linux target, stage-three
generation, stage-two checked-seed producer lineage, 19-source build plan,
five link orders, two workers, and fixed-point command. The build-plan
SHA-256 remains
`7fa10ec56ee33b3e3fbc6d2320a6338909cd51c0fcf9c6f9170acb1081f50ec0`.
The refreshed manifest has SHA-256
`86001db0540aeaf2568c38359a8adb310e259bb8cac7070937e8a7f2f4714d46`.

A second poisoned-host bootstrap started from the promoted files and refreshed
manifest. All five seed images matched stage two. Every stage-two C object,
startup object, and tool image matched stage three, and the same 21 behavior
cases passed. The source snapshot remained
`cf17ed01addfb3ce6743785b14d523b86b2eedf34579df10aa4682cb1642ab35`.
The 14,859-byte reproof report has SHA-256
`f40ccd4195cf6fbcdc515b3c043e4d1ef00efbe5fa9acf9e7aeb75579be992a6`.
The reproof took 460.2 seconds.

## Rejected alternatives

Promoting the native compiler candidate was rejected because its producer
lineage passes through a host compiler and linker.

Promoting stage two was rejected even though it matched stage three. Stage
three is the final generation produced by the checked transition and keeps
the trust boundary explicit.

Copying only CupidC was rejected. The manifest describes one five-tool seed,
so the promotion copied and verified the complete stage-three set even though
four files were unchanged.

Combining this refresh with a normal-build source cutover was rejected. The
seed is a bootstrap input. Production ownership changes the active build graph
and needs its own dependency closure, poisoned-host recipe proof, image build,
and subsystem runtime evidence.

## Consequences

A clean checkout now carries the stage-three CupidC that implements the
width-aware port-I/O assembly boundary from ADR 0105. It can rebuild itself
and the other four checked tools without GCC, Clang, NASM, a host linker,
`nm`, or `objcopy`.

This change replaces a bootstrap input and does not transfer a normal OS
object. The production cohort remains 26 CupidC-owned sources and 366,592
deterministic object bytes. The 14 sources whose last measured compiler
blocker was `kernel/core/ports.h` remain candidates until the checked-seed
frontier, Make ownership, image, and subsystem runtime gates pass together.

Native contract runners, hosted development commands, Python orchestration,
the Windows WSL bridge, most kernel C objects, and all user C objects remain
host dependencies.
