# Refresh the checked seed for the GNU assembly frontier

- Status: Accepted
- Date: 2026-07-26

## Context

Compiler revision `32b0f65d8cb31dc6e5a3fd5b6a2837b7e30bf9fb` carries
the source-driven capabilities recorded by ADRs 0116 through 0121. CupidC
retains GNU `used`, emits the active privileged-register forms, accepts an
FXSAVE pointer input, captures a call-next address, selects DX for the GNU
`Nd` port alternative, and writes the three panic-path machine-state
snapshots through exact memory outputs.

The prior checked seed came from revision
`d2e0f8b876d96b9268666e16c26a9e16ab5249af`. It could rebuild the newer
compiler source, but normal checked-seed builds could not use the new
capabilities until the resulting compiler passed a trust transition and
reproduced itself from a refreshed manifest.

## Decision

The prior seed rebuilt the complete 19-source Toolchain union, startup, and
all five static tools. `CC`, `CXX`, `CPP`, `HOSTCC`, `HOSTCXX`, `ASM`, `LD`,
`AR`, `NM`, and `OBJCOPY` all named commands that do not exist. Stage two
matched stage three for every C object, startup object, and linked image.
Both stages also passed five help cases, ten successful operations, and six
useful failures.

The transition watched 40 inputs with SHA-256
`8afdfbe4917adbe43d0c97a5b158b70220ab7f973b6b6293f284d3dc80a727ba`.
Its 14,860-byte report has SHA-256
`f2520ee3e2eb7dee2cb023b63d7de4bd97ccc4600f6e0ac1769972ea486f81f5`.
The run took 515.3 seconds. CupidASM, CupidDis, CupidLD, and CupidObj matched
their prior seed images. CupidC changed, and its stage-two and stage-three
images matched each other.

All five stage-three images were promoted as one seed:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,060 | `00f684ca5ca1e2ba36763e6810c65fea8b3786d40f6008d635751a1f2c2b6db0` |
| CupidDis | 366,968 | `67fcdbcf8a7924e37f00ec571bb5a4dbfbf4897c9743e9f3a3bbcaf0ea20ca60` |
| CupidLD | 262,388 | `373ed96803dcfb0005b8b3b1d49ca1313396ee11e17521aad6402f487cdd97e5` |
| CupidObj | 182,704 | `1f48c3d7b5f80d3e33eb9268c087111e8fa54eb390c24368a09f7ec2981c0030` |
| CupidC | 2,042,976 | `e30e51550326f4e74de9095c1256a3d4b40b734e060b896be89433d3518ffd41` |

The build-plan SHA-256 remains
`7fa10ec56ee33b3e3fbc6d2320a6338909cd51c0fcf9c6f9170acb1081f50ec0`.
The refreshed 5,421-byte manifest has SHA-256
`3c83d8b8fb2467a011b1cc99551e83ffda80ddaed78582123b0266b6a188951d`.

A second poisoned-host bootstrap started from the promoted seed. Every seed
image matched stage two, and every stage-two object and image matched stage
three. The same 21 behavior cases passed over the same 40-input snapshot.
The 14,859-byte report has SHA-256
`1a98a07c0b5fc9e43ff6ca76c489b68552725eaae5996e5358bd2f7094208d10`.
The reproof took 505.4 seconds.

The complete seed test module then passed all 14 tests in 506.181 seconds.
Those tests cover exact provenance, manifest schema and digests, ELF entry
validation, source and seed mutation rejection, frozen input independence,
private WSL staging, and another complete fixed point.

## Rejected alternatives

Promoting a native compiler image was rejected because its producer lineage
passes through a host compiler and linker.

Promoting only CupidC was rejected. The manifest binds one five-tool seed,
so promotion and verification cover the complete set even when four files
remain byte-identical.

Moving production source ownership in the same commit was rejected. A seed
refresh changes the trusted bootstrap input. Production transfer also needs
closed source and header sets, `.cc` renames, poisoned normal-build recipes,
image linkage, and runtime evidence.

## Consequences

A clean checkout now carries the stage-three compiler for the GNU assembly
frontier. It can rebuild itself and the other four checked tools without a
host code generator. The seed can compile the pending strict roots, including
the generated kernel-symbol source, but those roots remain host-owned until
their separate production transfer passes.

Native contract runners, Python orchestration, the Windows WSL bridge, Doom,
vendored C, and the remaining host-owned normal-build objects are unchanged
dependencies.
