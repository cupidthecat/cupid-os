# ADR 0183: Promote the Doom-capable Toolchain seed

## Status

Accepted on 2026-07-29.

## Context

The checked i386 Linux CupidC seed emitted all 80 Doom-tree objects but
predated the final three compatibility requirements. It could not preserve the
explicit static pointer cast in `kernel/doom/doom_libc_stubs.c`, and it
rejected the combined `dg_setjmp` and `dg_longjmp` file-scope assembly block
in `kernel/doom/dglibc.c`.

Compiler head emitted all three compatibility roots without changing their
source. The production recipes could not move until that compiler crossed the
checked seed boundary and reached another five-tool fixed point.

## Decision

Promote the complete stage-three Toolchain set built from pushed revision
`7609793ea594a8e024474509e5faacaf1d6c76ea`.

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,104 | `d57e4f0494aef294045c633b12e4db3f14e879102ac4e528fe70d6a5f089c7e7` |
| CupidC | 2,524,088 | `d05b48f14c5c57930c151f4d7099d686066c6cface01305c7d2c0261b660970d` |
| CupidDis | 371,108 | `e67157c4883f4164635b6084bc8c6475b77fd9d051196f4a553ae64346948d70` |
| CupidLD | 262,388 | `373ed96803dcfb0005b8b3b1d49ca1313396ee11e17521aad6402f487cdd97e5` |
| CupidObj | 182,704 | `1f48c3d7b5f80d3e33eb9268c087111e8fa54eb390c24368a09f7ec2981c0030` |

Only CupidC differs from the preceding seed. All five files remain one trust
unit and were copied from the same stage-three directory. The manifest binds
their exact bytes, the pushed source revision, and the unchanged 19-source
build plan.

The checked-seed gate now compiles each compatibility root twice under the
exact `DOOM_COMPAT_I386` profile. It fixes the source bytes as well as the
result:

| Source | Source bytes | Source lines | Source SHA-256 | Object bytes | Object SHA-256 |
| --- | ---: | ---: | --- | ---: | --- |
| `kernel/doom/dglibc.c` | 22,607 | 814 | `1f0d6f52e6b59b3f6364fd1dbdeb09491804409e11fc6932dc86134a39ad2cca` | 27,992 | `88e3a66488e09ee15769e666971dd34ed0fe0707a54f9962f5f7dadbe4fd4224` |
| `kernel/doom/doom_libc_stubs.c` | 8,168 | 289 | `41ba584dd83a21602d46307837e0966bdf1d7087ec832ca57c38882b20ad16b6` | 14,352 | `8f667113c54fa0b0d27ce83d134242065ba5b9258324a809e11e72229752ff3b` |
| `kernel/doom/doomgeneric_cupidos.c` | 13,544 | 400 | `049359f33dbdb64af446522043c528b67fd8bc98bb344fa3aa8e16e3b690dd2e` | 10,232 | `5274b91dfa7bac56cd83ff0f8096eb5a06fef5e61f91ebb3b80efacc8ad2a9cb` |

This decision moves compiler capability across the seed boundary. It does not
move a normal Doom recipe or rename a source.

## Evidence

The transition bootstrap started from the preceding checked seed with the
normal host compiler, assembler, linker, archive, symbol, and object-copy
commands poisoned. It captured 40 source inputs with snapshot SHA-256
`1199072a4415195a83e45c6469c79e066d445d96a884d6b0b9235cc09f035986`.

All 19 C objects, the startup object, and all five linked images matched
between stage two and stage three. Both stages passed five help cases, ten
successful operations, and six useful failures. CupidASM, CupidDis, CupidLD,
and CupidObj already matched stage two. The preceding CupidC did not, which
was the expected transition to the new compiler.

The transition completed in 770.978 seconds. Its 14,880-byte report has
SHA-256
`cd4150909067db1894f563c17b973e18f2a1564a4570199e29213502428c7eaa`.

The checked-seed regression was written before promotion. It failed on
`kernel/doom/dglibc.c:61:1` with `CTC000003`, reporting that the GNU
file-scope template was outside the old seed's i386 emission slice. After
promotion, seed verification and the repeated three-object proof passed. The
object proof ran in 21.046 seconds.

The promoted manifest is 5,440 bytes with SHA-256
`df78825b4bb5051f9e13b65e561365d10d13cb3ff5b7af1d6a88bf70af4d4dce`.

A second poisoned-host bootstrap started from the promoted seed. Every input
seed image matched its stage-two replacement. Stage two and stage three again
matched all 19 C objects, startup, and five linked tools, and both stages
passed all 21 behavior cases. The run completed in 754.238 seconds. Its
14,879-byte report has SHA-256
`8df090f19f26672bba3e981f8eb3a3ca6405375e67fcc2e2020c5e4b0c9a41e0`.

The complete checked-seed module passed all 26 tests in 765.549 seconds. It
repeated the fixed point and checked the manifest schema, exact provenance,
source and plan drift, frozen inputs, ELF entry points, seed-byte tampering,
and the three repeated Doom compatibility objects.

## Rejected alternatives

Promoting only CupidC was rejected because the manifest treats all five tools
as one trust root.

Assigning the pushed revision to an earlier build from an unpushed temporary
commit was rejected. The accepted candidate was rebuilt from the exact
remote-visible revision named by the manifest.

Moving the Doom recipes before seed promotion was rejected because that would
make the normal build depend on compiler bytes outside the checked trust root.

Changing the Doom sources to fit the preceding seed was rejected because
their existing forms express the required pointer and non-local-jump behavior.

## Consequences

The checked seed now emits all 83 Doom and port objects. The normal build can
transfer the cohort after exact object and link comparison, closed-input
wrapper checks, byte-preserving `.cc` renames, image construction, and runtime
proof.

The host compiler remains responsible for those 83 normal recipes in this
commit. Native Windows tools and the broader hosted contract build also remain
host-produced. `TempleOS/` remains untouched reference material.
