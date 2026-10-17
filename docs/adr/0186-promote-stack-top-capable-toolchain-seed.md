# ADR 0186: Promote the stack-top-capable Toolchain seed

## Status

Accepted on 2026-07-29.

## Context

Compiler head can read a nonzero, page-aligned stack top from the otherwise
fixed kernel-entry BSS-clear statement. The checked i386 Linux seed still
contained the earlier compiler, so the active memory map could not use that
capability through the normal production wrapper.

The compiler change was committed and pushed as
`af4644177c033eebda164d7893074315439df119` before the candidate seed was
built.

## Decision

Promote all five stage-three Toolchain files produced from that revision:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,104 | `d57e4f0494aef294045c633b12e4db3f14e879102ac4e528fe70d6a5f089c7e7` |
| CupidC | 2,528,332 | `f53989572cd1564a8bf91059552868ee43a1d80905986b58cd97d44949aab3a1` |
| CupidDis | 371,108 | `e67157c4883f4164635b6084bc8c6475b77fd9d051196f4a553ae64346948d70` |
| CupidLD | 262,388 | `373ed96803dcfb0005b8b3b1d49ca1313396ee11e17521aad6402f487cdd97e5` |
| CupidObj | 182,704 | `1f48c3d7b5f80d3e33eb9268c087111e8fa54eb390c24368a09f7ec2981c0030` |

Only CupidC differs from the preceding seed, but the manifest continues to
treat the five files as one trust unit. It binds the pushed source revision
and the unchanged 19-source build plan with SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

A checked-seed regression compiles a small kernel-entry source twice and
requires the exact `BC 00 00 10 01` encoding for `MOV ESP, 0x01100000`.
This promotion changes compiler capability only. The active kernel source,
linker script, bootloader, and disk layout remain unchanged.

## Evidence

The new regression was added before promotion. The preceding seed rejected
the statement at line 6, column 3 with `CTB00000F`, identifying the unsupported
kernel BSS-clear template.

The transition started from that seed with the host compiler, C++ compiler,
assembler, linker, archive tool, symbol tool, object-copy tool, object
inspector, and NASM commands poisoned. The harness froze 40 source inputs with
snapshot SHA-256
`0f203fa31a8212e804c82ccc70aef267d83b70aadc3d2c4e969640947c5468ff`.

All 19 C objects, startup, and five tool images matched between stage two and
stage three. Both stages passed five help cases, ten successful operations,
and six useful failures. CupidASM, CupidDis, CupidLD, and CupidObj already
matched stage two; CupidC differed as expected.

The transition completed in 671.599 seconds. Its 14,880-byte report has
SHA-256
`14238a5b62f06bbb8d51874bccb7635ab1e1fe5a1a0382a9c25b285181645cd7`.

The promoted 5,440-byte manifest has SHA-256
`98dd40674aa42f0fc52689dfe22d459d78c9b2374f7110f83727e5da12321939`.
Seed verification and the new repeated object test pass after promotion.

A second poisoned-host bootstrap started from the promoted seed. All five
input images matched stage two. Stage two and stage three then matched the
same 19 C objects, startup, and five tools, and both stages passed all 21
behavior cases. This run completed in 670.133 seconds. Its 14,879-byte report
has SHA-256
`5aea23e068e68f09eba2bbc97e2e03ae0773223e42b153780bf2006b0e99ce41`.

The complete checked-seed module passed all 27 tests in 717.669 seconds. It
repeated the fixed point and checked the manifest schema, exact provenance,
source and plan drift, frozen inputs, ELF properties, seed-byte tampering,
kernel and Doom objects, and the new stack-top object.

## Rejected alternatives

Changing the active memory map before seed promotion was rejected because the
normal kernel recipe must remain reproducible from the checked trust root.

Replacing only `cupidc.elf` was rejected because the manifest promotes the
five Toolchain executables together.

Recording an uncommitted worktree state as provenance was rejected. The
candidate was built from the exact pushed revision named by the manifest.

## Consequences

The normal wrapper can now compile a kernel entry that installs a different
page-aligned stack top. A later commit may coordinate that source change with
the linker, bootloader, disk image, external-program arena, and runtime
contracts.

Native contract runners and hosted development commands remain host-built.
The 83 normal Doom objects also remain host-owned until their separate
production handoff.
