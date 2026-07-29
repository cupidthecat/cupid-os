# ADR 0174: Promote the libm-capable Toolchain seed

## Status

Accepted on 2026-07-29.

## Context

The checked i386 Linux seed came from revision
`c00b3494014ca0a5f41143caa7e713e46b2ad3ec`. Compiler head later gained:

- named GNU assembly operands and compatible fixed-register overlap
- exact EFLAGS restore assembly
- the five active libm statement-assembly blocks
- the `fabs`, rounding, remainder, exponent/logarithm, and cdecl bridge
  file-scope families
- explicit non-atomic `double` to `unsigned long long` conversion

Revision `be5945915af8f76792eba573950f263bdae133a3` completed deterministic
object emission for unchanged `kernel/cpu/libm.c`. The old checked seed
could rebuild that compiler at a fixed point, but it could not use those
new capabilities in a normal kernel compile. Production ownership could not
move until the refreshed compiler became a checked bootstrap input.

The promotion had to begin from a clean, pushed revision. A native compiler
image or an uncommitted source tree would not provide the required producer
lineage.

## Decision

Promote the complete stage-three tool set built from revision
`be5945915af8f76792eba573950f263bdae133a3`.

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,104 | `d57e4f0494aef294045c633b12e4db3f14e879102ac4e528fe70d6a5f089c7e7` |
| CupidC | 2,447,776 | `afc8003e5e047c721fa085c793f2c4fe7e0b5c8e29d4f0bebac5282eb10cace9` |
| CupidDis | 371,108 | `e67157c4883f4164635b6084bc8c6475b77fd9d051196f4a553ae64346948d70` |
| CupidLD | 262,388 | `373ed96803dcfb0005b8b3b1d49ca1313396ee11e17521aad6402f487cdd97e5` |
| CupidObj | 182,704 | `1f48c3d7b5f80d3e33eb9268c087111e8fa54eb390c24368a09f7ec2981c0030` |

Only CupidC changes bytes. The manifest still defines one five-tool trust
root, so promotion copies and verifies the complete stage-three set.

The 19-source `.cc` build plan, startup input, five link orders, target ABI,
and producer lineage do not change. The build-plan SHA-256 remains
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

The refreshed 5,440-byte manifest records the new source revision, CupidC
size, and CupidC hash. Its SHA-256 is
`11cba1f48348ea857a8a8f4a3d1d276fdd90df03f889ff07154f25d85e51db52`.

## Evidence

The prior seed rebuilt the clean revision with `CC`, `CXX`, `CPP`, `HOSTCC`,
`HOSTCXX`, `ASM`, `LD`, `AR`, `NM`, and `OBJCOPY` set to commands that do
not exist. The frozen 40-input source snapshot has SHA-256
`0a10b3d9e477cdb9ca341814e481bdfcb4532fa052e5cfb1d0ca27045f6457e7`.

All 19 C objects, the startup object, and all five images match between
stages two and three. Both stages pass five help cases, ten successful
operations, and six useful failures. The transition took 715.1 seconds.
Its 14,879-byte report has SHA-256
`48fb5af629d5770990305fd51663a74966569993f067ee069cf85c6f77fc4ade`.
The prior seed matches stage two for CupidASM, CupidDis, CupidLD, and
CupidObj. CupidC differs as expected.

After promotion, a second poisoned-host bootstrap used a fresh output tree.
Every checked seed image matches stage two. Every stage-two object, startup
object, and image then matches stage three, and all 21 behavior cases pass.
The reproof took 705.2 seconds. Its 14,878-byte report has SHA-256
`a3a5d48867b781af56f8a48ab0e2db86cc1df0c22d7ec42a3e803d4be9a7df25`.

The standalone verifier accepts the manifest and all five ELF32 images. A
direct checked-seed contract compiles unchanged `kernel/cpu/libm.c` twice
under the exact `KERNEL_I386` profile. Both runs produce the locked
16,164-byte relocatable object with SHA-256
`ccfb59839b058020a3cdc30c8e6db7ebac8845215a38ff974b3cbca876574eac`.
That test passed in 5.085 seconds. The complete checked-seed module then
passed all 23 tests in 709.937 seconds.

Regenerating the active build records leaves the graph at 698 sources, 253
feature IDs, 504 transforms, and 42 accounted unreachable files. Its
active-source digest remains
`95c2eb5c3af777d6b6901d491b502e0658ddac0bbcaea7d834138d810979e909`.
The 1,526,996-byte JSON now has SHA-256
`be6ecfdcbddfb9e0b789e317f10cbc2154e44556d5d95e4dbcc25517c5b6e145`.
The unchanged 15,060-byte Markdown record has SHA-256
`53f72262e6dbae27da017e08cc83a662368f83438beb5c6909cc66b922951298`,
and the unchanged active preprocessor cases have SHA-256
`67c2e854455c5fdf38be1551c00b739a2501a31b79f04a9461fdbc0de3f22672`.
The deterministic drift check passed in 60.129 seconds. The complete
build-graph audit module passed all 62 tests in 570.748 seconds.

## Rejected alternatives

Keeping the old seed was rejected because normal checked-seed compilation
would still stop before the later libm capabilities.

Promoting a native compiler image was rejected because its producer lineage
passes through a host compiler and linker.

Promoting only CupidC was rejected because the manifest binds all five
static tools as one seed, even when four images remain byte-identical.

Moving `libm.c` into the production cohort in this commit was rejected. A
seed refresh changes the trusted bootstrap input. Production transfer also
needs a checked wrapper closure, source rename, normal-build poisoning,
image linkage, and runtime proof.

## Consequences

The checked seed carries the complete `libm.c` assembly frontier and the
other compiler capabilities listed above. A clean checkout
can reproduce all five tools without a host code generator and can emit the
complete unchanged libm object.

This decision changes the bootstrap input, not normal-build ownership.
`kernel/cpu/libm.c` remains host-owned and keeps its `.c` suffix until the
separate production transfer passes. No normal object, image, ABI, runtime
path, ownership count, or host-dependency count changes here. Issue #26
remains open for that transfer.

`TempleOS/` remains untouched reference material.
