# Build SMP discovery with checked-seed CupidC

- Status: Accepted
- Date: 2026-07-24

Current status: ADR 0104 expands the production CupidC cohort from 22 to 26
objects and reduces host-C ownership from 275 to 271 transforms. The evidence
below remains the record of the earlier SMP discovery hand-off.

## Context

The checked i386 Linux seed from ADR 0102 can compile the unchanged ACPI and
MP-table discovery sources. Both files use the per-CPU pointer load and GNU
integer atomics added in ADRs 0100 and 0101. The root Makefile still sent them
to GCC or Clang, so the compiler capability had not changed production
ownership.

The existing production wrapper and deterministic frontier were named for
kernel cryptography. Copying that security-sensitive path for SMP would have
created two implementations of seed verification, WSL staging, ELF
validation, rollback, and deterministic publication. Broadly approving the
whole `kernel/smp` directory would also have hidden real compiler gaps in the
five SMP sources that remain host-built.

## Decision

One strict kernel wrapper now owns an exact 22-source allowlist: all 20
`kernel/crypto` translation units plus `kernel/smp/acpi.c` and
`kernel/smp/mp_tables.c`. The generalized
`tools/kernel_cupidc_frontier.py` keeps the crypto directory inventory closed,
watches the kernel-profile source and header inputs, compiles every approved
source twice, validates both i386 `ET_REL` outputs, and publishes only a
complete deterministic result. Its explicit-compiler mode also honors
`--compiler-host-path` when that option is used without a runner.

The two SMP Make rules call the same checked-seed wrapper as the crypto
cohort. Their prerequisites include the complete active header closure and
the wrapper, frontier, verifier, seed manifest, and seed tools. Their existing
link order remains `bkl.o`, `mp_tables.o`, `acpi.o`, then `smp.o`.

The old `test-kernel-crypto-frontier` Make target remains as a compatibility
alias. The primary target and Python module use the broader kernel CupidC
name.

The GUI terminal smoke has an optional `--verify-smp-runtime` contract. It
requires the four-vCPU ACPI and MP discovery record, every application
processor online, RDRAND seeding, exactly 62 crypto successes, e1000, the
desktop, the terminal, and CupidC command completion. It rejects the known
panic, exception, corruption, SMP startup, crypto, storage, filesystem, and
illegal-instruction failure markers.

## Rejected alternatives

An SMP-only wrapper and frontier were rejected because they would duplicate a
bootstrap trust boundary that is already tested and used in production.

Approving every `kernel/smp/*.c` file was rejected. `bkl.c`, `ioapic.c`,
`lapic.c`, `percpu.c`, and `smp.c` still have source-driven CupidC
requirements and must remain outside the allowlist until those features are
implemented.

Poisoning the host compiler for the complete image build was rejected as
invalid evidence. The remaining 275 C transforms still legitimately use GCC
or Clang. The acceptance check poisons only the two transferred Make targets.

Rewriting either SMP source to avoid its current language requirements was
rejected. The toolchain now accepts the unchanged production code.

## Consequences

Checked-seed CupidC owns 22 normal-build kernel objects. The complete frontier
emits 213,996 byte-identical object bytes. The two new objects are:

| Source | Object bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/smp/acpi.c` | 5,708 | `0e32026db8af4d22ad9007c1900df16bee2bca342187a797dc12f154f340b1d5` |
| `kernel/smp/mp_tables.c` | 4,156 | `37791cc5ab28b93e92553735a2c8380d539f9473529e3f8d5731859c37358960` |

A forced rebuild with `CC=__cupid_host_cc_must_not_run__` executes exactly
two CupidC wrapper commands and produces those objects in 1.833 seconds.
The active audit retains 698 inputs, 252 feature requirements, 501
transforms, and 39 accounted unreachable files. Ownership moves from 20 to 22
CupidC transforms and from 277 to 275 host-C transforms. Python appears on 31
transforms because it now orchestrates both SMP compilations. The active-source
digest is
`1e4f5fecd656ca495ce453df98064ee63645bd0997fe316ea2fbaf01fe87fb3a`,
and the complete audit JSON has SHA-256
`4df71f07e2c251cffd9cd60c3e165fd7700d338a1eae561ceabadb96cb913ac2`.

The normal two-pass build keeps all 3,869 text symbols at the same address
between links. `mp_tables_discover` is at `0x00155EBE`, and `acpi_discover`
is at `0x001574AF`. `_loaded_end` is `0x006FAA9B`, leaving 2,116,453 bytes
below the reserved-area ceiling. `_kernel_end` is `0x00B1B910`, leaving
935,664 bytes below the fixed stack.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel.elf.pass1` | 6,357,572 | `50bdf148f4c225d0a17b767b0bd3abed1735eaed130ba9993f0d2861a8fea5a4` |
| `kernel.elf` | 6,447,684 | `e444b2979d538dcfa75f17885c5b4efe1c5adee965432a472024d1f1e25dec55` |
| `kernel.bin` | 6,269,595 | `22d51edf92519f928e51c5eed2048c6ea865caf1440c65febeb8e38bd37d9488` |
| Preboot disk image | 209,715,200 | `5d8d1d1da94db449f0a997772b6f82832647cab90bd760b8a3d28861ea661094` |

The raw kernel matches the disk image at LBA 5. QEMU with four CPUs, the
`max` CPU model, and e1000 passes the strong runtime contract and completes
`/bin/ls.cc`. The 62,808-byte serial log has SHA-256
`b15665f889f14baab26eabbd0d918362ac67153477fed0484ef14e4eebd20fc7`
and contains none of the rejected markers.

This cutover does not make the C build self-hosting. Most normal C objects,
the private in-kernel compiler path, hosted development tools, Python
orchestration, and the Windows WSL execution bridge remain host dependencies.
