# Refresh the checked seed for atomic fetch-or

- Status: Accepted
- Date: 2026-07-25

## Context

Compiler revision `10d2412ece22968e03dbe22b048c3d92f210f2ba`
implements GNU `__atomic_fetch_or` for represented one-, two-, and four-byte
integer objects. Its i386 emitter uses a locked compare-exchange retry loop
that returns the old value and retains competing updates.

The checked seed still contained the earlier 1,946,320-byte CupidC from
revision `d76f543948621ea04520d019fad9aae670f17f11`. That compiler could build
the new Toolchain closure, but it could not compile the active EHCI source
because it treated `__atomic_fetch_or` as an undeclared identifier.

Replacing a checked executable changes the bootstrap root. A hosted compiler
candidate was not enough: the promoted file had to come from the old seed's
stage three and then reproduce itself from the new manifest.

## Decision

The old seed ran the complete staged bootstrap against revision
`10d2412ece22968e03dbe22b048c3d92f210f2ba`. Both `CC` and `LD` named
commands that do not exist. The run rebuilt 19 C objects, startup, and all
five tools in stages two and three without a host compiler or linker.

Every object and tool matched between the two stages. CupidASM, CupidDis,
CupidLD, and CupidObj also matched their old seed files. CupidC changed, as
expected, and its stage-two and stage-three files matched each other. Both
stages agreed on five help cases, ten successful operations, and six useful
failures.

The 40-input source snapshot has SHA-256
`e0e5bb3d520c8c1e9354ae359c3e41f891702cd07e57235cd98ae661f8b8bd85`.
The transition consumed a manifest with SHA-256
`cf944909bf0795d07b06505526a2750d4c0a773aaaff1972444861db49a69ab0`.
Its 14,860-byte report has SHA-256
`fab944312b551421f036018c484665f1c48b0c2bd4771fabec8d9fece6bd77ed`.
The run took 482.612 seconds.

The complete stage-three set was promoted:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,060 | `00f684ca5ca1e2ba36763e6810c65fea8b3786d40f6008d635751a1f2c2b6db0` |
| CupidDis | 366,968 | `67fcdbcf8a7924e37f00ec571bb5a4dbfbf4897c9743e9f3a3bbcaf0ea20ca60` |
| CupidLD | 262,388 | `373ed96803dcfb0005b8b3b1d49ca1313396ee11e17521aad6402f487cdd97e5` |
| CupidObj | 182,704 | `1f48c3d7b5f80d3e33eb9268c087111e8fa54eb390c24368a09f7ec2981c0030` |
| CupidC | 1,950,556 | `f4d49d8b870868ccd57aed94eaf7565404ceb10732c79c868e65f9beca5371c8` |

Only CupidC changed bytes, but the manifest describes one five-tool seed, so
promotion and verification covered all five files. The build-plan SHA-256
remains
`7fa10ec56ee33b3e3fbc6d2320a6338909cd51c0fcf9c6f9170acb1081f50ec0`.
The refreshed manifest has SHA-256
`48023cf4198f09cca96bc0db79baa920f67b32bbdb899bd55de48101a79e4c11`.

A second poisoned-host bootstrap started from the promoted files and
refreshed manifest. Every seed image matched stage two, every stage-two
artifact matched stage three, and the same 21 behavior cases passed. The
source snapshot stayed unchanged. The 14,859-byte reproof report has SHA-256
`6921050a8f91662d4c047c916b2c64b2ae37cd55953bf9038cb1a424d91cc9be`.
The reproof took 459.883 seconds.

## Rejected alternatives

Promoting the hosted compiler was rejected because its lineage includes a
host C compiler and host linker.

Promoting stage two was rejected even though it matched stage three. Stage
three is the final generation produced by the checked transition.

Copying only CupidC was rejected. The manifest binds one five-tool set, so a
safe promotion replaces and verifies that set as a unit.

Combining the seed refresh with the normal-build source cutover was rejected.
The seed is a trusted input; build ownership also changes recipes, dependency
closures, the active graph, and runtime evidence.

## Consequences

A clean checkout now carries the stage-three CupidC that implements atomic
fetch-or. The refreshed seed compiles the current `kernel/usb/ehci.c` twice
into byte-identical 19,408-byte ELF32 objects with SHA-256
`8e3aad270d63003a48c526a54a99d31022aac1d334a187308add52c875edb971`.

This decision changes the bootstrap input, not normal-build ownership. The
production cohort remains at 26 CupidC-owned sources until the separate
port-I/O and USB cutover passes its build, image, SMP, storage, input, audio,
and hot-plug runtime gates.

The checked seed can rebuild itself and the other four tools without GCC,
Clang, NASM, a host linker, `nm`, or `objcopy`. Native contract runners,
Python orchestration, the WSL bridge on Windows, most kernel C objects, and
all user C objects remain host dependencies.
