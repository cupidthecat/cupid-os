# Refresh the checked seed for the source-driven frontier

- Status: Accepted
- Date: 2026-07-25

## Context

Compiler revision `d2e0f8b876d96b9268666e16c26a9e16ab5249af`
implements the source requirements recorded by ADR 0113. It emits weak
symbols and named sections, retains `unused` declarations, preserves typed
static null pointers, models known-true loop exits, lowers comma expressions,
keeps represented function-pointer bits, and emits bounded output-only
register and EFLAGS snapshots.

The checked seed still contained the 1,950,556-byte CupidC image from
revision `10d2412ece22968e03dbe22b048c3d92f210f2ba`. That seed could compile
the updated Toolchain closure, but it did not carry the new language and
object behavior into normal checked-seed builds.

Replacing a checked executable changes a bootstrap root. The new compiler
therefore had to come from the old seed's stage three, then reproduce itself
and the other four tools from a refreshed manifest.

## Decision

The old seed ran the complete bootstrap against revision
`d2e0f8b876d96b9268666e16c26a9e16ab5249af`. Both `CC` and `LD` named
commands that do not exist. The run rebuilt all 19 C objects, startup, and
five tools in stages two and three without a host compiler or linker.

Every stage pair matched. Both stages also agreed on five help paths, ten
successful operations, and six useful failures. The 40-input source snapshot
has SHA-256
`7bee858042b883aac9d07ab4e2b6ba5b44075cdca308566a7317c0e00766ef1f`.
The transition consumed the prior 5,421-byte manifest with SHA-256
`48023cf4198f09cca96bc0db79baa920f67b32bbdb899bd55de48101a79e4c11`.
Its 14,860-byte report has SHA-256
`4291fb0441a4f6011be382243481a0ed7d8eb317567a98565f7c33bd26927730`.
The run took 477 seconds.

The transition report records the expected trust-boundary change. CupidASM,
CupidDis, CupidLD, and CupidObj matched their old seed files at stage two.
CupidC did not. Its stage-two and stage-three files matched each other.

The complete stage-three set was promoted:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,060 | `00f684ca5ca1e2ba36763e6810c65fea8b3786d40f6008d635751a1f2c2b6db0` |
| CupidDis | 366,968 | `67fcdbcf8a7924e37f00ec571bb5a4dbfbf4897c9743e9f3a3bbcaf0ea20ca60` |
| CupidLD | 262,388 | `373ed96803dcfb0005b8b3b1d49ca1313396ee11e17521aad6402f487cdd97e5` |
| CupidObj | 182,704 | `1f48c3d7b5f80d3e33eb9268c087111e8fa54eb390c24368a09f7ec2981c0030` |
| CupidC | 2,000,636 | `2224337832dda113f27c70fb944188b48c0660324a652725feb83976461bc0ac` |

Only CupidC changed bytes, but the manifest describes one five-tool seed.
Promotion and verification therefore covered all five files. The build-plan
SHA-256 remains
`7fa10ec56ee33b3e3fbc6d2320a6338909cd51c0fcf9c6f9170acb1081f50ec0`.
The refreshed 5,421-byte manifest has SHA-256
`4021d9f46807decc522725ab0d94c36ff2202f8706cf610a02571973d14455f3`.

A second poisoned-host bootstrap started from the promoted files and
refreshed manifest. All five seed images matched stage two. Every stage-two C
object, startup object, and tool image matched stage three, and the same 21
behavior cases passed. The source snapshot remained
`7bee858042b883aac9d07ab4e2b6ba5b44075cdca308566a7317c0e00766ef1f`.
The 14,859-byte reproof report has SHA-256
`397333debb6ea2bb3cd85330d0b511b62fe2f05e79053393f45384cb64b45147`.
The reproof took 480.9 seconds.

The regenerated active graph remains at 698 sources, 253 feature IDs, and
500 transforms. Its active-source digest remains
`630319a4b5dde5a519c0a0e4da7ee89ff489b2b7aa6224ddad64fa958588de23`.
The refreshed audit JSON has SHA-256
`d8cae3d58df169fa81c6889de28b0e0ef33f74a6efd27b835439b8f7b16af01d`.

The refreshed seed then compiled the complete 116-source production frontier
twice. Both passes emitted the same 2,268,616 i386 ELF32 bytes. The 404-input
snapshot has SHA-256
`8cd59650372a13303c33b2621e67f929d4c0b1a7bff1a134b68bee18c50cd269`.
The 72,433-byte checked-seed frontier manifest has SHA-256
`b49ba83f5c711724a47708a73e48763d86f1ba60a21c498d7e52fb6c91fd5a32`.
The earlier byte and snapshot locks predated the expanded syscall source.
Current production objects matched a native compiler-head diagnostic run,
and the checked-seed run reproduced the corrected object records.

The downstream frontiers ran after the kernel proof. The generated
installation frontier reproduced all three sources and objects over 194
inputs with SHA-256
`69fa9875a02707fdce735ac213cdfa9d490fe8925a9bafee95473aef999c4a39`.
The user frontier reproduced the `hello`, `ls`, and `cat` objects and
executables over 16 inputs with SHA-256
`4c7361aa214de8000874c19277e65469cd75ee60c1ef3561b2fd5a1c60a06499`.
Their output hashes remain unchanged. The seed is part of both input
closures, so refreshing it changed each aggregate input hash.

## Rejected alternatives

Promoting the native compiler candidate was rejected because its lineage
passes through a host compiler and linker.

Promoting stage two was rejected even though it matched stage three. Stage
three is the final generation produced by the checked transition.

Copying only CupidC was rejected. The manifest binds one five-tool set, so a
safe promotion replaces and verifies that set as a unit.

Combining the seed refresh with the normal-build source transfer was
rejected. The seed is a trusted input. Production ownership also changes
recipes, source names, dependency closures, audit counts, and runtime
evidence.

Running the kernel, generated, and user frontiers at the same time was also
rejected. The generated build creates private compiler-input directories
below `kernel/util` while it works. Those temporary headers correctly trip
the kernel frontier's source-drift guard. The three gates now run one after
another.

Using the live working tree for the final kernel snapshot was rejected during
review. Two unrelated, unstaged line-ending changes were part of the
frontier's watched source and header set. They did not change object behavior,
but they changed the input digest and would not exist in a clean checkout.
The accepted replay materialized the exact Git index in an isolated
directory, leaving both user files untouched.

## Consequences

A clean checkout now carries the stage-three CupidC that implements the ADR
0113 frontier. It can rebuild itself and the other four checked tools without
GCC, Clang, NASM, a host linker, `nm`, or `objcopy`.

This decision changes the bootstrap input, not normal-build ownership. The
production cohort remains at 116 CupidC-owned sources until the separate
frontier, poisoned-host recipes, image build, and runtime gates pass. Native
contract runners, Python orchestration, the Windows WSL bridge, most
remaining kernel C objects, Doom, and vendored C remain host dependencies.
