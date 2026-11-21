# Expand checked CupidC ownership to 116 normal-build sources

- Status: Accepted
- Date: 2026-07-25

## Context

The checked CupidC seed owned 40 normal-build C objects. A source-driven
probe then compiled every remaining strict kernel candidate without changing
the active source. Seventy-one kernel and driver files already passed the
checked compiler, deterministic object validation, and the fixed i386 kernel
profile. Five shared toolchain implementations also passed:
`toolchain/ctool.c`, `toolchain/cupidasm.c`, `toolchain/cupiddis.c`,
`toolchain/elf32.c`, and `toolchain/x86.c`.

The probe exposed two production gaps outside the C language itself. First,
`kernel/gfx/font_8x8.c` emits a valid data-only relocatable object. The
validator incorrectly required every object to have a `.text` section.
Second, unoptimized CupidC output brought the 111-source kernel to
`_kernel_end = 0x00BDFA70`, leaving only 132,496 bytes below the old
`0x00C00000` stack boundary. Adding the five shared toolchain objects needed
more room.

The fixed execution layout still had an unused one-MiB gap between the
CupidC and CupidASM arenas. Shrinking an active region or rewriting source to
reduce compiler output would have traded away real operating-system
behavior.

## Decision

The production wrapper now owns an explicit 116-source allowlist. It adds the
71 passing kernel and driver sources and the five shared toolchain
implementations to the established 40-source cohort. The wrapper remains the
only Make interface for these objects. Every recipe names its complete
recursive header closure and the shared seed, frontier, profile, and
validation controls.

The ELF32 relocatable validator no longer requires `.text`. It still requires
the symbol and string tables, validates every section and symbol range, and
rejects malformed data-only objects. Objects with code continue through the
same relocation and instruction checks.

The fixed memory regions move upward by one MiB while keeping their useful
sizes:

| Region | Range | Size |
| --- | --- | ---: |
| Kernel image ceiling | below `0x00D00000` | 12 MiB above the 1 MiB load base |
| Kernel stack | `[0x00D00000, 0x00F00000)` | 2 MiB |
| External ELF arena | `[0x00F00000, 0x01100000)` | 2 MiB |
| CupidC JIT/AOT arena | `[0x01100000, 0x01A00000)` | 9 MiB |
| CupidASM JIT/AOT arena | `[0x01A00000, 0x01C00000)` | 2 MiB |

The stack, external arena, CupidC arena, and CupidASM arena are adjacent.
Static assertions and tests pin their alignment, size, and boundaries.
CupidC keeps its one-MiB code region and eight-MiB data region. CupidASM
keeps its existing base and full two-MiB region.

The two vendored audio helpers that pass the strict kernel profile,
`kernel/audio/memio.c` and `kernel/audio/mus2midi.c`, leave the relaxed Doom
compatibility profile. Their source remains unchanged.

## Consequences and evidence

The strict frontier compiles all 116 approved sources twice and produces
2,267,588 byte-identical i386 ELF32 object bytes. It freezes those sources
and 288 headers or includes in a 404-file snapshot with SHA-256
`bba3c57ce5617d7afb70fb1c32b721b213aea86a54d4f905bb270c211c321c03`.
The frontier manifest has SHA-256
`64afe80b241f360bd7eb6d985fed04f42ca3924f7ac21087c819b3a1c2f37294`.
It records the fixed profile and checked-seed provenance separately.
Poisoned-host rebuilds cover all 116 recipes, and the audit and focused tests
pin their wrapper, Make, and ownership controls.

The generated build audit assigns 116 transforms to CupidC, 181 to the host C
compiler, and 125 to host Python. CupidASM owns four transforms, CupidDis one,
CupidLD five, and CupidObj 182. The active graph still has 698 source inputs,
501 transforms, and 252 feature requirements. Its active-source digest is
`b2687273e9b0aff71479bf97c4624e51bdb911611cf5cc894d4a400d2c906eb1`,
and the JSON SHA-256 is
`b9069f86aa59e7bcc32d343b82496fc3e13b108f356f55a2ee6a917e3d6061a8`.
This decision moves 76 normal-build C transforms without removing or
simplifying an active source.

The enlarged image links with `_loaded_end = 0x007F6641` and
`_kernel_end = 0x00C1BA70`. That leaves 1,085,375 bytes in the bootloader's
file-backed reserve and 935,312 bytes below the new stack boundary. A
four-vCPU GUI smoke reaches the desktop, terminal, all 62 crypto checks, and
CupidC execution at `0x01100000`. A separate runtime gate loads the same
external ELF program twice at `0x00F00000`. The first process releases lease
1 before the second claims lease 2, and both processes exit without a panic.
The complete repository suite passes all 638 tests, with one
environment-dependent skip.

Thirty-eight strict kernel roots remain host-built after the probe. Their
current blockers cluster around broader GNU attributes and inline assembly,
floating constants and conversions, static pointer or floating
initialization, and unsupported IR statement, conversion, or expression
forms. These failures define the next compiler work. They are not a reason to
reduce the source.

Python still launches the checked seed, and Windows still uses WSL for the
static i386 executable. Native contract runners, hosted development commands,
the remaining normal C objects, Doom, and user-program C compilation remain
host dependencies. This decision does not rename `.c` files; that migration
belongs after full self-hosting.
