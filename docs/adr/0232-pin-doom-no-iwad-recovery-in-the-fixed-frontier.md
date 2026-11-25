# ADR 0232: Pin Doom no-IWAD recovery in the fixed frontier

## Status

Accepted on 2026-08-04.

## Context

CupidC already owns all 83 Doom and platform translation units in the normal
image. Separate asset-free boots prove missing-IWAD recovery, but the fixed
frontier command list did not invoke Doom. A normal frontier run could
therefore pass without reproducing the first user-visible Doom boundary.

The repository contains no IWAD outside the read-only TempleOS reference
tree. Gameplay, input, game audio, menu save and load, and reboot persistence
cannot be proved honestly from the available asset set.

## Decision

Extend the fixed asset-free command sequence after `dglibc_test` with:

1. `doom`, requiring the no-WAD search diagnostic and explicit `-iwad`
   guidance.
2. `doom -iwad /disk/missing.wad`, requiring the named missing-file error and
   `[doom] returned to shell`.
3. A second `ls`, requiring a fresh CupidC JIT compile and completion marker.

Keep the original first `ls` because the USB reattachment gate uses command
zero. The second occurrence has its own output window, so an earlier JIT
completion cannot satisfy the post-Doom recovery check.

## Evidence

The source behavior is in `kernel/doom/doomgeneric_cupidos.cc` and
`kernel/doom/src/d_iwad.cc`. No kernel or vendored Doom source changes are
needed.

Tests lock the complete command order, remove each required guidance, error,
and shell-return marker in turn, and withhold the second `ls` output to prove
that the first occurrence is insufficient. The complete GUI terminal smoke
module passes 103 tests in 2.571 seconds.

Fresh four-vCPU runs pass the complete fixed frontier on both supported NICs.
The e1000 run finishes in 370.5 seconds with 108,706 changed framebuffer
pixels, 13,994,333 AC97 frames, and 75,720 PC-speaker frames. The RTL8139 run
finishes in 349.8 seconds with 87,996 changed pixels, 13,073,288 AC97 frames,
and 74,410 PC-speaker frames. Both runs finish the post-Doom `ls` without a
panic.

## Rejected alternatives

Treating the existing standalone missing-IWAD boots as sufficient was
rejected because their behavior was not part of the reproducible fixed
frontier sequence.

Adding a test WAD or using TempleOS assets was rejected because no approved
Cupid OS IWAD is present, and TempleOS is reference material only. Claiming
gameplay from error recovery was also rejected.

## Consequences

Every fixed asset-free frontier now proves both Doom discovery failures and a
healthy shell afterward. Issue 29 remains open for IWAD-backed gameplay,
input, audio, menu save and load, and reboot persistence.
