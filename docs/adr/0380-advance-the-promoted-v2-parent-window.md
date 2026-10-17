# ADR 0380: Advance the promoted v2 parent window

Date: 2026-08-30

Status: Accepted

## Context

The next paired seed refresh will start from the active Linux and native
Windows cohorts built from revision
`0232cb57aad5d6bdfd7bd77499762514b2f0ebfd`. Its candidate manifests must name
those cohorts as their parents.

Source-head CupidBuild and the Cupid-built artifact-size verifier still
accepted the original v1 parent pair and the later kernel-symbol pair when
they parsed a promoted v2 manifest. A candidate that correctly named the
active kernel-flattening pair would therefore fail with
`fixed-point provenance differs` before it could exercise a new command.

The v1 schema still needs its historical parent. The promoted v2 transition
does not.

## Decision

Promoted v2 readers accept exactly two adjacent parent generations:

| Generation | Source revision | Linux manifest SHA-256 | Windows manifest SHA-256 |
| --- | --- | --- | --- |
| Preceding | `9d10c223fc7aa22901e6f4ae81ce800ff1b62ad6` | `770f979407f930deba0c9ba887bcd14f2350a785b1c0df6b31ddc2659c46eaae` | `bf6147cf2e8249372869a24e5b8477ffb785d9a48eef80209366cfbaff19c7db` |
| Active | `0232cb57aad5d6bdfd7bd77499762514b2f0ebfd` | `470fcd1b8b1a1506f26d3dd33d51f55d6896571aacb7329b792d4612f9434781` | `e7e65908eb03eec43e44e2946b395723b164f5701d980aae8ffaaf1006c3d7e4` |

CupidBuild applies the window to the Linux parent and to both Windows
execution and plan parents. The artifact-size contract uses the same two
pairs. A digest is valid only with the revision from its own row. The matched
v1 pair is no longer accepted inside a v2 manifest, while the historical v1
schema keeps its original validation. A Windows execution parent and plan
parent must also come from the same row.

This commit changes source capability only. It does not build candidates,
publish seed binaries, or move a Make recipe.

## Evidence

The two new positive tests first failed at the intended boundaries. CupidBuild
reported `fixed-point provenance differs`, and the artifact-size contract
reported `seed manifest parent provenance differs`. Both tests pass after the
window moved.

The complete CupidBuild CLI module passes 97 tests in 100.735 seconds, with
three platform skips. The artifact-size and Toolchain manifest contract group
passes 72 tests in 74.214 seconds. The 65-test CupidC Toolchain coordinator
module passes in 12.201 seconds. Negative coverage rejects the retired v1
pair in a promoted v2 manifest, a digest paired with the wrong revision, and
mixed generations in the Linux parent and both Windows parent fields. It also
rejects individually valid Windows execution and plan parents from different
rows.

A clean `make -j2 all` reached both CupidLD links and the guarded 431-input
flatten before the exact-size verifier stopped publication. The new CTXT
payload measured `kernel/kernel.bin` at 9,514,816 bytes instead of 9,514,384,
and `kernel/kernel.elf.pass1` at 9,613,368 bytes instead of 9,609,272. The
final ELF stayed at 9,740,344 bytes. After only those two policy rows changed,
a second full replay accepted all 16 artifacts and rebuilt the 200 MiB FAT16
image with `/hello.iso` staged.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/kernel.elf.pass1` | 9,613,368 | `b053778960e812de604807d4f3e7cdfb43f10d5fa4ca172699ff532d3d58c37a` |
| `kernel/kernel.elf` | 9,740,344 | `28d1e9c242dea3ae08146c005787f2f50aa537b9ca08162b10fceb07e7cd899c` |
| `kernel/kernel.bin` | 9,514,816 | `8078c11fbb1e4ef78410f634c69e0f176649e7d1d32e0dab23118c88696bdead` |
| `test_iso/hello.iso` | 61,440 | `40359c1cec72219f21e87ce71b31e621209036042440e1b38c5e59de157e0fb6` |
| `cupidos.img` | 209,715,200 | `e07e7424d1f9818b87ab9700406a5036e4ed56dcaad724c7a3aec48004ffb7b6` |

The 3,382-byte policy covers 38,335,516 bytes and has SHA-256
`22e5e9c011876f3991eefff13bef44b199c10839f5a69a521f052bd750472027`.
The regenerated 2,779,011-byte audit JSON has SHA-256
`6d21e24f18eedbc17f4785cac648ef3fcf26a323be47b9905c060d8ac5568741`;
the 13,192-byte summary has SHA-256
`9a6b8589e1a413cfb84377e9791b6deb46b3563665540644acd5316d29f07d31`.
Regeneration and the independent check both pass with 748 active inputs, 452
transforms, 255 feature requirements, and 28 accounted unreachable inputs.

A private four-vCPU QEMU boot passed the `max`/E1000 strong SMP gate, brought
all four CPUs online, and ran `/bin/ls.cc` to normal JIT completion with 911
code bytes and 71 data bytes. Its 33,159-byte serial log has SHA-256
`c40c6ca664ca7e627c19b44bd85745865ab9969cdcd6702e822b810d408d7fc8`.

## Alternatives considered

Accepting any well-formed parent digest was rejected because the parent is a
reviewed trust boundary, not descriptive metadata.

Keeping three v2 generations was rejected. The source-built tools need the
parent used by the checked seed and the parent that the next candidate will
name. Older v2 lineage can leave that moving window.

Changing the candidate generator without changing CupidBuild was rejected.
The candidate's own CupidBuild image must consume the manifest during the
fixed point and after promotion.

## Consequences

Source-head tools can validate the active checked pair and a candidate whose
parent is that pair. A future promotion still needs paired Linux and native
Windows fixed points, complete behavior evidence, seed publication, and
self-consumption before any production handoff.

No active C source changes suffix. `TempleOS/` remains read-only reference
material.
