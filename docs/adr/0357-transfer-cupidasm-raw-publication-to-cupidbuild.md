# ADR 0357: Transfer guarded raw publication to CupidBuild

## Status

Accepted on 2026-08-27.

## Context

ADR 0355 gave CupidBuild typed bootloader and SMP-trampoline operations. ADR
0356 then carried those operations into the paired Linux and Windows seeds and
proved that each refreshed cohort consumes itself at stage two. The normal
Make rules still entered the same checked transaction through Hostbuild, so
Python remained a coordinator on two publications that the promoted tool could
already own safely.

Moving the recipes requires more than replacing a command name. Each rule must
retain the complete production seed as a Make dependency, pass an absolute
repository root, ignore standalone tool overrides, and preserve the existing
raw bytes and rollback behavior on both hosts.

## Decision

Invoke the promoted CupidBuild image directly for `boot/boot.bin` and
`kernel/smp_trampoline.bin`. Each rule depends on its source, Makefile, the
production manifest, and all six production seed images. It passes
`$(CURDIR)` as the absolute repository root and does not use `$(PYTHON)`,
`tools/hostbuild.py`, `$(CUPIDASM)`, or `$(CUPIDDIS)`.

CupidBuild owns the transaction boundary. It freezes the source and seed,
asks CupidASM to produce a private raw image and v2 map, validates the exact
artifact size and map policy, asks CupidDis to enforce known decode, local
targets, and source-resolved edges, rechecks every live boundary, and replaces
the public image atomically. The map remains private.

The audit recognizes either the retained Hostbuild compatibility entry point
or the direct CupidBuild command as guarded flat assembly. The normal graph
attributes both transferred outputs to CupidASM, CupidDis, and CupidBuild.
Across the three supported roots, the graph remains at 452 transforms, with
443 under root `all`. CupidBuild participation rises from two to four and
Python participation falls from 450 to 448. CupidASM and CupidDis remain at
nine each.

CupidBuild remains a non-producer in the v2 seed lineage. That manifest role
prevents circular fixed-point provenance; it is separate from normal-build
publication ownership.

## Evidence

The Make-graph contract first failed for both raw targets because their
prerequisite sets still contained `tools/hostbuild.py` and
`tools/bootstrap_toolchain.py`. After the transfer, the same test passes for
Windows and Linux. It requires the exact seven-file production seed unit plus
Makefile and the source, poisons the standalone CupidASM and CupidDis controls
and Python, and observes only the direct CupidBuild command with an absolute
root.

A forced native Windows rebuild passed with `PYTHON=missing-python`. A forced
Linux rebuild through WSL passed with `PYTHON=/bin/false`. Both hosts produced
the established artifacts:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `boot/boot.bin` | 2,560 | `46cc9778da2b5cc5e8f04d7cc4b07243c3e07d466626ad84fb813dc6fef3a0d3` |
| `kernel/smp_trampoline.bin` | 4,096 | `b738ebb68f28b9b07e330761f4e9a7898f0424ab0a3835cd6079ae7d4a189e90` |

The complete CupidBuild suite passed 50 tests in 57.598 seconds, with two
expected platform skips. The build-graph suite passed all 111 tests in
913.207 seconds. Audit generation and checked comparison passed with four
CupidBuild and 448 Python participations. The generated 2,767,674-byte audit
record has SHA-256
`49ed2f7c349a8592e23dc80443931c5a44d4dbff434d3433bb40ee50c7bba218`,
and its 12,882-byte summary has SHA-256
`c617bdddc4b39d162bea773a44c428db4df3e06abafa155976341902224ba547`.

The first complete normal build reached the exact-size gate after both links
and the strict 431-input CupidDis scan. It failed closed because the humanized
embedded manuals moved `kernel.bin` from 9,507,224 to 9,507,804 bytes. The two
ELF sizes stayed fixed. After the measured policy row moved, the direct
sixteen-artifact verifier passed, and a clean normal replay published the
image with its FAT data preserved and `hello.iso` staged. The current kernel
artifacts are:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 9,605,148 | `e54c2fcefb432bc0cab314411a4dfb0dda377169c613497487f0eb6ec75c4b63` |
| `kernel/kernel.elf` | 9,736,220 | `c4004b2b9b003b8c0174a32d11948a228b50ec57e97d69532c2c514834adc436` |
| `kernel/kernel.bin` | 9,507,804 | `2efdc4df2a71cc6e889acd67f9322bf449692ee046d089762df3575dba90143f` |
| `cupidos.img` | 209,715,200 | `1276de1dc03ed01cbcc90e95e9a4d0b71abd0751bd9c74251ab0ccac2719c9bc` |

The 3,382-byte policy covers 38,144,480 bytes across the same sixteen paths
and has SHA-256
`78d1d4cc4b5411cc73523b88166e75fba876b2cd78f1d9c9118b1367fa86ec21`.

A private four-vCPU `max` and E1000 frontier passed from that image. All CPUs
came online, E1000 acquired `10.0.2.15`, the framebuffer changed 69,823 pixels,
and both audio captures were non-silent. The 147,526-byte log has SHA-256
`252d3ef3796233cd752754c19aaa85a7311010bd75d0d5d57264fd6919584b56`.
The private-image run left the source image byte-identical.

The final artifact-policy and Hostbuild compatibility group passed all 64
tests in 6.488 seconds, with four expected platform skips. Both six-tool seed
verifiers passed. After the policy update, audit regeneration and the
deterministic checked comparison both passed.

## Consequences

The normal build no longer needs Python or Hostbuild to assemble, inspect, or
publish either raw boot artifact. Hostbuild retains its compatibility tests,
oracles, disk and ISO work, kernel flattening, staging, and other active roles.
Fixed-point coordination and 448 supported transforms still involve Python.

No source file changes suffix in this handoff. Every active CupidC translation
unit already uses `.cc`, and transferring two assembly publications does not
change C-source ownership. `TempleOS/` remains read-only reference material.
