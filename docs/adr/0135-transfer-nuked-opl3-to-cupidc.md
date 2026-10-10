# ADR 0135: Transfer Nuked OPL3 to checked CupidC

## Status

Accepted on 2026-07-27.

## Context

Nuked OPL3 was the last normal kernel root blocked only by C11 external inline
semantics. Its header declares `OPL3_Generate4Ch` normally, while the source
provides the inline definition. ADR 0131 taught compiler head to finalize that
declaration set as one external definition. ADR 0134 then promoted the rule
into the checked i386 Linux seed.

The production object still came from GCC or Clang under the relaxed vendored
profile. Moving it required more than a successful compiler-head fixture. The
normal recipe needed a closed input set, poisoned-host coverage, the complete
source frontier, both kernel links, image construction, and runtime evidence
that exercised audio with both supported network devices.

## Decision

Rename `kernel/audio/nuked_opl3.c` to
`kernel/audio/nuked_opl3.cc` without changing its implementation. Build it
through `CUPIDC_KERNEL_COMPILE` under the normal `KERNEL_I386` profile.

The Make recipe declares the source, `nuked_opl3.h`, `kernel/core/string.h`,
`kernel/core/types.h`, and the common checked compiler controls. The wrapper
accepts the `.cc` path as one of its fixed source-driven inputs, snapshots the
complete closure, verifies the checked seed, validates the i386 relocatable
object, checks for input drift, and publishes atomically.

Keep the broader language policy unchanged. A pure external inline definition
still receives its focused unsupported diagnostic. An external-linkage inline
declaration without a definition still fails at translation-unit finalization.

## Evidence

The dedicated build contract first failed because the `.cc` source was not
approved. It now checks the exact Make dependencies and wrapper command, the
approved source list, the active profile, and the absence of a host compiler
fallback.

The first canonical `make test` run exposed stale profile totals in the
preprocessor contract. Its frozen table still expected 154 kernel cases and
four Doom compatibility cases. The generated manifest correctly reported 155
and three after the ownership transfer. Updating those two expected totals
made the focused active-corpus test pass, followed by all 39 preprocessor
contract tests. The complete rerun passed all 764 tests in 3,305.941 seconds
with one expected skip.

Independent review then caught that the wrapper did not yet enforce the input
snapshot promised above. The new positive contract requires CupidC to read
Nuked from a private copy of the source and all three headers. Its negative
pair changes a live header during compilation and requires the existing
object to survive. Both tests failed against the live-repository path, then
passed after the wrapper gained the exact four-file closure and a prepublish
drift check. The generated symbol-source closure keeps the same protection.

The next canonical run reached all 766 tests, then the real WSL frontier hit
a Windows access-denied error while renaming its complete staging directory.
Both smaller two-pass publishers still passed, which isolated the failure to
a short-lived permission lock after the large WSL run. Frontier publication
now retries only permission-style errors with five bounded delays. A positive
test clears a simulated lock on the third attempt. A negative test keeps the
lock active, exhausts every delay, and proves that no frontier is published.
Other filesystem errors still fail immediately.

A concurrent frontier reproof then exposed a separate snapshot bug. The input
scan descended into a hidden `.ksyms_data.o.cupidc-*` staging directory made
by the checked compiler wrapper and treated two copied headers as repository
inputs. A focused test first reproduced that false drift while retaining an
ordinary header in the same include tree. Input discovery now skips paths
with a hidden component below an active include root. The real repository
headers remain frozen, while private compiler staging can no longer change
the frontier inventory.

The current-tree reproof passed in 1,319.325 seconds while the canonical
suite and runtime work were also active. It compiled all 146 roots twice
without false drift and published no partial result.

The final canonical `make test` completed in 3,502.557 seconds. Unittest
discovered 769 tests, passed 768, and recorded the one expected Windows skip
in 3,422.213 seconds. There were no failures or errors.

Two focused production compiles produce the same 40,424-byte ELF32 object with
SHA-256
`a3a04ade4029d9333902bb93376fb5eef21f349ee5a1406bd0751cc4cee9f2a1`.
CupidDis reports a defined global `OPL3_Generate4Ch`. The only undefined import
is `memset`.

The complete frontier compiles 146 checked-in roots twice against 434 frozen
inputs. The snapshot has SHA-256
`4bf3bca0564df68e1049e496ac990691023c4ccb5d8815e419c98bf6a11d2b53`.
Both object sets are byte-identical and total 3,586,148 bytes. The 83,265-byte
frontier manifest has SHA-256
`343df05e20778b8f0d96d35d90f878e2e1fd8fb1bc131a3fde9aaf719d2e055f`.

The generated active-source audit contains 698 sources, 253 feature IDs, 502
transforms, and 42 accounted unreachable files. Ownership is 153 CupidC
transforms, 144 host C transforms, 166 host Python transforms, and five Make
transforms. Host C now produces 92 normal root objects. The active-source
digest is
`0da325a39ff0f8a0ee7e42ccd49ff5137c932ded8e90cc0bbb7885d4d6164c50`.

A clean `make -j2 all WAD_SRCS=` completed in 406.9 seconds. Review then
caught that the embedded CTXT edits made that image stale. The concurrency
fix added one last CTXT clarification, so the final current-tree rebuild ran
after every embedded document had settled. It completed in 455.681 seconds.
Both CupidLD links, CupidDis symbol extraction, CupidC symbol-source
compilation, CupidObj flattening, and image construction passed. The final
artifacts are:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 8,003,988 | `e597c9ca869e3ef3804a12be6c39291161e203c98650d5421202a137b2589315` |
| `kernel/kernel.elf` | 8,110,484 | `1e1720f0603595ccff4b15d6c07433e4fcb9bbe522c1632f383c80ec95250726` |
| `kernel/kernel.bin` | 7,913,528 | `97fd9704ed2ceb1a095853e4746aafa360420f79a1cbefad1374c3396263270a` |
| `cupidos.img` | 209,715,200 | `e480f936f2192cdf4cb35028a3da898817ab4fdee66d1f3c3bfc8e9b6639d819` |
| `kernel/cpu/ksyms_data.cc` | 347,884 | `5aa057ae4472f01df1a07212ed9e313ec75945aaa448fee7eb7adbac92215a08` |
| `kernel/cpu/ksyms_data.o` | 105,348 | `223c19ab43e228e0ef83b2b878617121a687f0eb86e5f9545f1b1754808ef36e` |

CupidDis projects 4,369 text symbols from both link passes. The generated
source packs a 104,933-byte logical blob with three zero padding bytes. The
flat kernel matches the image at LBA 5. `_loaded_end` leaves 472,520 bytes
below `0x008FF600`, and `_kernel_end` leaves 320,912 bytes below
`0x00D00000`.

The first 45-second e1000 boot window expired while both long WSL compiler
proofs were active. The log showed continued startup progress and no panic or
rejection, and the image hash did not change. The 90-second-window retry
passed in 192.908 seconds. It reached the desktop and terminal, passed all 62
crypto checks, exercised USB
storage and reattachment, changed 95,201 framebuffer pixels, reported
6,275,031 AC97 frames with peak 25,600, reported 78,916 PC speaker frames
with peak 32,248, and completed in-OS CupidC execution. Its 71,277-byte log
has SHA-256
`c2e37d7d422a50e62ad016d6afa61b9f4414a5ccb38981477df75f5183e3e55f`.

The first concurrent 45-second RTL8139 boot window also expired without a
failure marker. A 90-second retry passed, and the final captured proof passed
the same contract in 188.651 seconds. It reported 85,633 changed framebuffer
pixels, 6,216,315 AC97 frames with peak 25,600, and 81,905 PC speaker frames
with peak 32,512. Its 65,102-byte log
has SHA-256
`aa8b46c52524cf7d343806b208cc6bb69d361004da5f4be1578e994f043b9d24`.
Neither final log contains a panic, compiler failure, or frontier rejection
marker.

## Rejected alternatives

Keeping the `.c` path after transferring ownership was rejected because the
repository uses `.cc` to mark active CupidC-owned C sources.

Leaving the source on the Doom compatibility profile was rejected because the
checked seed now satisfies the ordinary kernel profile. A production transfer
should not retain a relaxed profile after its only blocker is gone.

Treating the focused object as sufficient was rejected. Nuked OPL3 contributes
many symbols and audio behavior to the linked kernel, so both links, the
generated symbol source, image layout, and runtime audio paths had to pass.

Treating the first pre-review RTL8139 attempt as a compiler failure was
rejected because the log showed a healthy kernel and no relevant failure
marker. A retry passed, but review later invalidated both pre-review runtime
runs when it found that the embedded CTXT edits had not reached their image.
Both final-image gates were required before accepting the transfer.

## Consequences

The normal build now contains 146 checked-in CupidC roots plus the generated
kernel symbol source. All 147 normal sources use `.cc`. Nine strict
checked-in roots remain host-owned. Nuked was the separate relaxed vendored
root, so its transfer reduces the total host root count without changing the
strict count.

The transfer removes one GCC or Clang root object without weakening or
rewriting the vendored emulator. Python and WSL orchestration, native hosted
contracts, the Windows native-tool bootstrap, Doom, the remaining vendored
sources, and the other host-owned C roots remain open.
