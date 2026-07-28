# ADR 0160: Transfer FPU and SMP roots to checked CupidC

## Status

Accepted on 2026-07-28.

## Context

The checked i386 Linux seed can compile the active FPU, per-CPU, and SMP
translation units without changing their behavior. The compiler work covers
the MOVSS and x87 memory forms in the FPU root, the descriptor-table and
segment-register forms in the per-CPU root, and the three naked IPI entries in
the SMP root.

Those capabilities were necessary, but they did not make the normal image use
them. The Makefile still sent all three files to the host compiler, and their
`.c` names correctly described that ownership. Moving the names before the
production recipes, frozen-input checks, full image, and runtime path were
proved would have overstated CupidC's role.

## Decision

The normal build now compiles these roots through
`tools/cupidc_kernel_compile.py` and the checked seed:

- `kernel/cpu/fpu.cc`
- `kernel/smp/percpu.cc`
- `kernel/smp/smp.cc`

The wrapper's approved-source table owns each complete recursive input closure.
It freezes the seed, manifest, source, and headers before execution, validates
the resulting i386 ELF32 relocatable object, rechecks the frozen inputs, and
publishes the object atomically. Failure or input drift leaves an existing
output untouched.

The files move to `.cc` in the same change as their production recipes and
proofs. Their source bodies are not simplified or rewritten around CupidC.
The SMP object changes hash because its existing `__FILE__` diagnostic now
contains `smp.cc`; that is the expected result of the ownership rename, not a
compatibility shim.

The strong SMP runtime contract also requires both existing FPU boot markers:
`[fpu] boot smoke ok` and `FPU boot smoke passed`. A successful in-OS
`feature16_asm_fpu.cc` compile and run remains the end-to-end floating
execution check.

## Evidence

Two complete checked compiles of each promoted source produce deterministic,
validated objects:

| Source | Object bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/cpu/fpu.cc` | 6,620 | `14c3ea232b7d4455ceabd561c69293cc5849abae24d9f210aa69d64ed8c8a5cb` |
| `kernel/smp/percpu.cc` | 6,760 | `3c2c6f0e00e5edec1ca16cba91e9fc593d1c42e24f4ebd3591e5f574fb0dd772` |
| `kernel/smp/smp.cc` | 8,444 | `bd3189b2a1a6d15728c559172f5d6acca0889103428085cec8cc1024742a22d1` |

The complete source-driven frontier compiles all 151 checked-in normal roots
twice. Both passes are byte-identical and total 3,643,676 bytes. The frozen
439-input snapshot has SHA-256
`dd61ee8ece6a26282f7ae2d5f252f53c109827bf3e7a3365a00cc5a6e8d59a8a`.
There are no compiler boundaries in that cohort.

A forced rebuild names invalid `CC`, `CXX`, `CPP`, `HOSTCC`, `HOSTCXX`,
`ASM`, `LD`, `AR`, `NM`, and `OBJCOPY` commands. All three objects still
rebuild through the checked wrapper with the hashes above. The wrapper
contracts also cover frozen recursive closures, source drift, invalid objects,
atomic publication, and preservation of an earlier output after failure.

A clean `make -j2 all WAD_SRCS=` build completes through the production
recipes. CupidLD links both kernel passes, CupidObj flattens the final kernel,
and CupidASM assembles the boot and SMP inputs. The resulting artifacts are:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 8,049,800 | `a617da173e8fdaff777221adf1e042d03628108b00a380ba3a6575169c49848a` |
| `kernel/kernel.elf` | 8,156,296 | `cc8b8ea2dfc767d585f490245ce9d416e085e1bf94d5381efe752667f9802efa` |
| `kernel/kernel.bin` | 7,959,532 | `e83dd816ef250f57aab384faafe5fa71e65fcbb91b6cda1e6ddd54d482577b68` |
| `cupidos.img` | 209,715,200 | `f4545e3ff1d6209dd097c548461b235fb8ae95ae3241bceea24034c359e37e2d` |

All 7,959,532 flat-kernel bytes match the image at LBA 5. CupidDis reports
5,722 accepted symbol rows, including 4,392 text symbols, from both the
pass-one and final kernels. Symbol-table generation therefore reaches a fixed
point.

Sequential QEMU runs boot private copies of that image with four `max` CPUs.
The e1000 and RTL8139 runs each bring all four discovered CPUs online,
initialize the selected NIC, print both FPU boot markers, and finish
`feature16_asm_fpu.cc` with its PASS marker and CupidC JIT completion. Each
log contains all 62 TLS self-test successes. The 54,869-byte e1000 log has
SHA-256
`65fc984950e3813e387d41a2f480d3f3e59dd38b6a2478abb2df73a6854c11e2`;
the 53,419-byte RTL8139 log has SHA-256
`7bd6fdb1829c8b71194b6a6024e1ba61528122a345baae4078a127bd8394e989`.

Focused integration checks pass all 29 wrapper tests, all 73 GUI and runtime
contract tests, deterministic object and source-order checks, the poisoned-host
rebuild, the bootstrap audit drift check, and verification of all five checked
seed tools.

## Rejected alternatives

Keeping `.c` names after the checked production handoff was rejected because
the repository uses the suffix as an ownership signal.

Renaming the files while leaving host recipes in place was rejected because it
would claim a transfer that the normal image did not make.

Dropping `__FILE__` or preserving the old `smp.c` text inside the object was
rejected because the source already uses the macro legitimately. The new path
is the truthful production value.

Using compiler-head binaries directly was rejected. Production remains tied to
the verified checked seed and its frozen manifest.

## Consequences

The normal cohort now has 151 checked-in CupidC roots and one generated
CupidC translation, all named `.cc`. Across root and supplemental builds,
CupidC owns 158 transforms, the host C compiler owns 139, and host Python owns
173. The host compiler still produces 87 normal root objects, so the normal OS
build is not host-independent yet.

This transfer changes build ownership and filenames, not the FPU, per-CPU, SMP,
or ABI design. The remaining host-owned C and Doom roots continue to drive the
compiler roadmap. `TempleOS/` remains untouched reference material.
