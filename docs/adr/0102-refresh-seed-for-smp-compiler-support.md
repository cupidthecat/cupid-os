# Refresh the checked seed with the SMP compiler additions

- Status: Accepted
- Date: 2026-07-24

Current status: ADR 0103 uses this seed for ACPI and MP-table discovery, and
ADR 0104 uses it for e1000, desktop, socket, and TCP. The account below
preserves the seed transition itself.

## Context

The checked i386 Linux seed at revision
`b04c5b5ead1be504669ad8f0f84b3531eda3df9c` contained the compiler used by
the complete kernel crypto cohort. Compiler head had since added three active
capabilities:

- operand-free GNU assembly inside functions;
- the per-CPU `mov %%gs:0, %0` pointer output; and
- integer atomic load, store, exchange, and fetch-add.

Those additions let compiler head emit more unchanged production sources, but
the normal build could not use them until the checked bootstrap root carried
the same compiler. Copying the native Windows compiler would have broken the
stage-three lineage established by ADR 0097.

## Decision

The previous checked seed ran the complete bootstrap against exact source
revision `6639799ee3da19b077c890223e3340fc5e05e7ba`. Both `CC` and `LD`
named commands that do not exist. The run rebuilt all 19 C objects, startup,
and five static tools in stages two and three. Every compared artifact
matched, and both stages agreed on five help paths, ten successful operations,
and six useful failures.

The transition report binds 40 live inputs with source snapshot SHA-256
`175bf51130ca860f82874b5052fb0852f6e0ad9952f283394ed42d8b70cbf88e`.
Its SHA-256 is
`4068ad260e135cfd6dc0d7dc3a20216c0c949aa56959f9e1e592c149f56cfb4c`,
and the run took 484.4 seconds. The old manifest has SHA-256
`90f30ede183176337cbe56463e7f7321291d7b87255c6692784ed6c57634dd6e`.
The four non-compiler seed images matched stage two. CupidC did not, as
expected after the six compiler-source changes.

The complete stage-three set was promoted:

| Tool | Bytes | SHA-256 |
| --- | ---: | --- |
| CupidASM | 433,060 | `00f684ca5ca1e2ba36763e6810c65fea8b3786d40f6008d635751a1f2c2b6db0` |
| CupidDis | 366,968 | `67fcdbcf8a7924e37f00ec571bb5a4dbfbf4897c9743e9f3a3bbcaf0ea20ca60` |
| CupidLD | 262,388 | `373ed96803dcfb0005b8b3b1d49ca1313396ee11e17521aad6402f487cdd97e5` |
| CupidObj | 182,704 | `1f48c3d7b5f80d3e33eb9268c087111e8fa54eb390c24368a09f7ec2981c0030` |
| CupidC | 1,921,292 | `ff8c4aba0c4fc66982343a28356d0f1953503acdb12d76177ed066609e056976` |

Only CupidC changed bytes, but promotion and verification covered all five
files. The manifest retains the stage-three generation, the stage-two CupidC,
CupidASM, and CupidLD producer lineage, the passing fixed-point command, and
build-plan SHA-256
`7fa10ec56ee33b3e3fbc6d2320a6338909cd51c0fcf9c6f9170acb1081f50ec0`.
Its source revision is now the capability commit above. The refreshed
manifest has SHA-256
`f8bd649a1f87ecdd368c22b4149315b4fa48c98fd3f59aaeef706c802d803f33`.

A second poisoned-host bootstrap started from the refreshed seed. All five
seed images matched stage two, and every stage-two object, startup image, and
tool matched stage three. The same 21 behavior cases passed. The reproof
report has SHA-256
`74d70178ee2cadf342bc85a2c1ecf8d5c144f6a2d2a7f5db358008165b97d1b9`;
the run took 518.1 seconds.

## Rejected alternatives

Promoting the native compiler candidate was rejected because its producer
lineage passes through a host compiler and linker.

Promoting stage two was rejected even though it matched stage three. Stage
three is the final generation produced by the checked transition and keeps
the trust boundary unambiguous.

Combining the seed refresh with a normal-build source cutover was rejected.
The seed is a bootstrap input, while production ownership changes the active
build graph and needs its own rollback, image, and runtime evidence.

The first transition command placed its output outside the repository. The
harness rejected that path before executing a seed tool because bootstrap
artifacts must stay under the captured source root. The accepted run used a
fresh private directory inside the repository.

## Consequences

A clean checkout now carries a stage-three CupidC that represents the active
operand-free assembly, per-CPU pointer output, and integer atomic operations.
It can rebuild itself and the other four checked tools without GCC, Clang,
NASM, a host linker, `nm`, or `objcopy`.

This change replaces a bootstrap input; it does not transfer another normal
OS object. The complete crypto cohort continues to use checked-seed CupidC.
`acpi.c`, `mp_tables.c`, and the four sources unlocked by operand-free
assembly remain separate production cutovers. Native contract runners,
hosted development commands, Python orchestration, the Windows WSL bridge,
and most normal C objects remain host dependencies.

The refreshed seed compiles the complete 20-source crypto frontier twice to
the same 204,132 validated object bytes. The normal two-pass image build
produces a 6,443,216-byte `kernel.elf` with SHA-256
`8eb00fa9cfa447f759c4ff16878aeb1676b5e8caab60ad621fb2fc615df70727`
and a 6,264,573-byte `kernel.bin` with SHA-256
`2d73e5deacf45ad486b98e3fd1b77f5d87c6465455783f8bb74b00a8122669ab`.
Before boot, the image has SHA-256
`e36977654f8716018518d709cec503c8f1ebdf1102cc299cb1cecd06b7f6fb53`.

QEMU with four CPUs, the `max` CPU model, and e1000 seeds through RDRAND,
passes exactly 62 crypto, ASN.1, and X.509 checks, brings all four CPUs
online, initializes the network device, reaches the desktop and terminal,
and completes `/bin/ls.cc`. The serial log has SHA-256
`9250a817047041cef012a26155767ab256570752522af0a428e9055e1cb4a374`
and no accepted panic, corruption, self-test failure, or illegal-instruction
marker.
