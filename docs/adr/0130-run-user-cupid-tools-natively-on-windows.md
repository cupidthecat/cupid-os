# Run user Cupid tools natively on Windows

- Status: Accepted
- Date: 2026-07-26

## Context

The external-program build already belonged to CupidC and CupidLD, but the
checked tool images were static i386 Linux executables. Linux could run those
images directly. Windows had to copy them into WSL for every compile and link.
That made WSL part of the supported Windows user build even though the
repository could already build native Windows CupidC and CupidLD drivers.

The output format does not require a Linux host process. User programs are
freestanding i386 ELF32 images for Cupid OS. A native Windows driver can emit
the same target bytes.

## Decision

On Windows, `user/Makefile` now prepares `toolchain/build/cupidc.exe` and
`toolchain/build/cupidld.exe` through one explicit recursive Make
prerequisite. The production wrappers run those native drivers directly.
Linux continues to run the checked i386 Linux seed.

This handoff applies only to the three external programs. Generated
installation tables and the normal kernel cohort continue to use the checked
seed on both hosts.

The Windows wrapper accepts only the two approved repository paths. It
rejects aliases and symlinks, captures the complete executable bytes, checks
for an AMD64 PE32+ console image, and runs a private copy. It also checks the
live file again before atomically publishing the ELF output. A changed driver
therefore cannot mix one run with another.

The explicit checked-seed mode remains available as an oracle. The Windows
frontier builds `hello`, `ls`, and `cat` with the native drivers, repeats the
native build, then builds them once more with the checked seed. All three
objects and all three executables must match byte for byte.

## Rejected alternatives

Keeping WSL as the only Windows user path was rejected because it was not
needed to produce the target ELF files.

Checking host-built PE files into the bootstrap seed was rejected. The
current native drivers still depend on Clang and its Windows linker, so those
files are hosted bootstrap products rather than Cupid-built seed images.

Calling this a native Windows fixed point was rejected for the same reason.
A real fixed point still needs a Cupid-built Windows runtime and PE/COFF
executable output.

Switching the generated installation tables or the kernel cohort in the same
change was rejected. Those paths need their own ownership proof.

## Evidence

The Windows frontier covers 46 frozen inputs with SHA-256
`3d73ce1475d3398dd7ea069e232663b6ab84e3a201d722e95453cee6028b37b0`.
Native and checked-seed builds produce the same six files:

| Program | Object bytes | Object SHA-256 | Executable bytes | Executable SHA-256 |
| --- | ---: | --- | ---: | --- |
| hello | 6,124 | `64e0a6ee0d7a45a0901d3db614e73481cdc6b30903345c5015601b2bf344be04` | 13,992 | `dbef548d246e12a0933b95ec8349a97f542bd8cbecc253efc514b1483fcc9e0f` |
| ls | 7,120 | `e0627996a1d9cd6fd428642ffdfada7e07afa81d9267bc714360014af0dd3971` | 18,112 | `6eb9d140dd126f74e2815a6836c8858e0d9ca8a1da837bd94784c3a1b7c5ec9d` |
| cat | 6,292 | `ff002fc4710704c3941bf6320249e772a3448d15f99269987ab1b9b608b3acb4` | 13,992 | `ffa5957fb58f0de81e564b3fbadadf60b7b8bc2beb0c50984cd1d4e9481f9367` |

A Windows build also passes after the native drivers are prepared and
`wsl.exe`, `gcc.exe`, `clang.exe`, `ld.exe`, and `cc.exe` are replaced by
failing executables on `PATH`. The conventional Make code-generator
variables are poisoned in the same run.

The direct Linux user build still runs the checked seed without the native
tool prerequisite. Its six files match the Windows files above.

## Consequences

The supported Windows user build no longer needs WSL once the native drivers
exist. A clean Windows user build still needs Clang and its native linker to
prepare those drivers. Root checked-seed work on Windows still needs WSL.

The build graph grows by one explicit Make transform. The supported roots now
contain 502 transforms, including nine under `user:all` and five Make
transforms overall. CupidC remains responsible for 152 translations, and
CupidLD remains responsible for five executable links.
