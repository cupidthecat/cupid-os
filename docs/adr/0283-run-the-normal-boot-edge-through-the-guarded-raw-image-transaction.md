# ADR 0283: Run the normal boot edge through the guarded raw-image transaction

## Status

Accepted on 2026-08-14.

## Context

ADR 0277 gave the bootloader a checked raw-image transaction. It freezes the
source and tool seed, asks CupidASM for a private image and source-derived map,
runs strict CupidDis inspection, rechecks drift and the output boundary, and
publishes atomically. The normal Make rule could not use that path until the
checked Windows tools carried CupidASM `--map` and CupidDis `--range-map`.

ADR 0281 promoted the stage-four Windows cohort with both options. The normal
boot rule still called checked CupidASM directly, so it did not receive the
map inspection, lock, drift checks, or transactional publication already
available in hostbuild.

## Decision

The `boot/boot.bin` rule calls
`tools/hostbuild.py assemble-bootloader`. It passes the production seed
manifest, repository root, source, and output. Its prerequisites include
`tools/hostbuild.py` and `CHECKED_SEED_INPUTS`.

The rule does not depend on `CUPIDASM_INPUTS`. A caller may override the
standalone CupidASM command or its input list without weakening the boot
closure. Hostbuild derives CupidASM and CupidDis from the verified production
manifest.

The transaction requires a 2,560-byte image. It gives CupidDis the private
`cupid.raw-map.v1` candidate and requires every code range to decode without
unknown, invalid, or truncated instructions. Hostbuild checks the frozen
source, seed, candidate, map, live output, and output parent before replacing
the destination. The map remains private.

## Evidence

The Make-database contract first failed against the direct rule because a
`CUPIDASM_INPUTS` override replaced the boot seed closure. After the cutover,
eight focused graph and raw-image tests pass in 1.535 seconds. They cover the
boot and SMP rules, checked-seed closure under tool overrides, source and seed
drift, output replacement, and caller-specific map policy.

The complete `tests.test_build_graph_audit` module passes all 84 tests in
933.312 seconds. The audit contract records the bootloader as a three-line
hostbuild recipe with ten inputs and CupidASM, CupidDis, and Python
participation. The same run synchronizes the active `_WIN32` conditional count
at 33 and checks 2,452 active include occurrences.

The guarded path retains the reviewed 2,560-byte image with SHA-256
`46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3`.

A forced normal boot-edge run passed in 0.866 seconds with `CC`, `CXX`, `CPP`,
`HOSTCC`, `HOSTCXX`, `ASM`, `AS`, `LD`, `AR`, `NM`, `NASM`, and `OBJCOPY`
set to invalid commands. The output retained the reviewed hash. No public map,
candidate, lock, or private bootloader directory remained.

The final poisoned-host `make -j4 all` passed in 674.693 seconds. CupidDis
accepted all 431 production inputs, the nine-artifact size gate passed, and
hostbuild published the image while preserving FAT data. The source-head
artifacts are:

| Output | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/kernel.elf.pass1` | 9,211,340 | `2a6f5deafb580b30254483179d6dade9ed4ed7b17b39f9368137b1ff14932263` |
| `kernel/kernel.elf` | 9,334,220 | `bc855462c1f8f42e34d94a974443f7c6e565d60b1913e3b6f33b3e6e375f3ed6` |
| `kernel/kernel.bin` | 9,114,084 | `8b5d73e74538ce11c1fb074f88b3852d690038aa5cb3a8de3ce222e9df88cade` |
| `cupidos.img` | 209,715,200 | `813c9b0c78f795c1ac9fcff59b9c4111a958a07eb1e3943dc7af60c536521110` |

A private four-vCPU boot compiled `/bin/ls.cc` with in-OS CupidC and reached
JIT completion in 49.257 seconds. A second private boot assembled
`/demos/hello.asm` to a 15,680-byte relocatable object, linked an 8,536-byte
ELF with two load segments, ran it as PID 4, and observed its normal exit in
76.174 seconds.

Final audit regeneration and its deterministic check pass in 69.6 and 69.3
seconds. The graph has 736 active inputs, 452 transforms, 25 unreachable
source-like files, and four CupidDis participations. The 2,673,547-byte JSON
has SHA-256
`a433c3c202f9ccba82fe587b4d5a48b0ec10a0d4440f44cc7b730002473b2604`.
The 12,269-byte summary has SHA-256
`c8afb2c59a3e13c098178b01168ae65fa10293e67b1c7cef57ef596eac72148c`.

## Rejected alternatives

Keeping the direct Make rule was rejected because it left production boot
publication outside the available strict map and transaction checks.

Depending on `CUPIDASM_INPUTS` was rejected because that variable belongs to
the standalone tool edge. An override could otherwise discard the production
manifest closure.

Publishing the layout map beside the boot image was rejected. The map is
transaction evidence for CupidDis, not an OS artifact.

## Consequences

The normal boot transform has CupidASM and CupidDis ownership. CupidDis now
participates in four root transforms. Python still owns coordination, locking,
drift checks, and atomic replacement.

The boot source and binary layout do not change. No host assembler enters the
normal graph, and no `.c` source earns a `.cc` rename from this wiring change.
