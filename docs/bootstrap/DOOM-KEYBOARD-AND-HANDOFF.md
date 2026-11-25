# Doom keyboard and input handoff

Doom now receives the complete letter and digit map, base punctuation,
Backspace and F1–F12. Space, Shift and Alt use the engine's Use, Run
and Strafe codes. Unmapped scancodes are rejected before they can fill the
input ring. The existing ring capacity and overflow behavior are preserved.

Text events convert the Use token to a literal space. The vanilla save-name
path does the same conversion while preserving its default policy of ignoring
Shift. Letters, spaces, digits and Backspace now reach the save menu together.

## Returning input to the shell

The desktop leaves its consumer queue unread while Doom owns fullscreen input.
Previously, those copied game keys reached the shell after Doom quit. The port
now acquires and releases its subscription under the BKL, tracks whether it
owns that subscription, and discards buffered consumer events before releasing
fullscreen ownership. A busy subscription fails without removing the existing
owner. `keyboard_discard_events` preserves physical key/modifier state, event
storage and COM1 input; its caller holds the BKL.

## Verification

- All 27 keyboard, handoff, music-pump, asset and subscriber checks pass on
  Windows and Linux. The original map fails five of six cases. A later
  seven-case regression isolates the missing vanilla-menu space.
- All 83 Doom objects match fresh native and Cupid-built reconstruction and
  the production objects, totalling 1,213,972 bytes. Both native hosts agree.
- The complete original 156-source kernel method passes in 1,805.316 seconds
  under its unchanged 2,340-second deadline. Both object sets match production:
  4,253,532 bytes per set, with the exact 486-input snapshot retained.
- The fresh integration checkout passes 205 tests with two existing skips.

The OS reconstruction reuses 424 verified baseline objects and regenerates five
objects, both kernel passes and the normal image. This is a guarded incremental
build, separate from a cold replacement-tool build. Three measured policy rows
change:

| Product | Exact bytes |
| --- | ---: |
| Raw kernel | 9,417,868 |
| Final ELF | 9,647,828 |
| Pass-one ELF | 9,516,756 |

A four-CPU guest saves `CUPID A1`, quits and runs `ls` through the JIT compiler
without replaying game keys at the shell. Independent FAT/HomeFS readback finds
the new 61,342-byte slot and preserves the preceding two slots. A second guest
boots a byte-identical copy of that closed image and loads the new slot. Its
health, ammunition, position, angle and saved level-time offset agree; the load
menu retains the name. It also quits and runs `ls`. Both logs pass the unchanged
strict SMP/network/desktop/command validator with all 62 TLS checks.

The 2026-11-17 four-vCPU replay loaded the bundled Freedoom 1 WAD, quit
through the F10 confirmation and ran `ls` through the CupidC JIT. The
shell resumed input and completed the command. This replay covered
startup and input return.

The four-vCPU timedemo reproduced the EHCI ownership panic after graphics
initialized. A register trace caught a separate failure before submission,
followed by the controller eventually asserting halt. With the fallback
halt wait extended to 100 ms, the timedemo completed 7,117 game ticks
at 7.14 fps, exited fullscreen and returned to the shell without an
EHCI ownership failure. See the
[EHCI halt decision](../adr/0491-allow-time-for-ehci-halt-proof.md).

The November 21 replays selected the same Freedoom WAD and started
`demo1`. Two four-vCPU runs and one single-vCPU run reached graphics
initialization and hit the 1,500-second deadline without a result or
kernel panic. Samples from the second four-vCPU run caught rendering and
OPL3 synthesis. The idle thread often appeared in synchronous EHCI
polling while other CPUs waited for the big kernel lock. The samples
showed activity in those paths, and the cause of the slowdown remains
open.

On November 24, the 64 MiB guest-matched image completed 7,117 game
ticks in 19,179 real ticks under WSL at 12.99 frames per second.
The four-CPU 200 MiB image with the staged ISO and WAD reached
graphics and started `demo1` on WSL and Windows. WSL reached its
1,800-second deadline and Windows reached its 5,400-second deadline
without a timedemo result or return to the shell. The first Windows
run missed keystrokes in the IWAD path. The retry used 1.2 seconds per
key report and opened the correct WAD.

A Windows QEMU monitor sampled `gametic` on the later 200 MiB image
with the streaming HomeFS writer. It advanced from 1 to 38 in about
65 seconds after graphics initialized. One sampled CPU was executing
`OPL3_EnvelopeCalc`. The timedemo was still running when the sample ended.

Windows and WSL later finished the timedemo on the same 64 MiB hosted
reference disk with the streaming kernel and both staged assets.
Both completed 7,117 game ticks and returned to the shell.

| QEMU host | Real ticks | Frames per second |
| --- | ---: | ---: |
| Windows | 48,970 | 5.09 |
| WSL | 41,596 | 5.99 |

The WSL retry used `doom -timedemo demo1` after a missed command. The
game selected `/disk/wads/freedo~1.wad`. The image SHA-256 was
`59e89ef8d10565f5e2dccfe6a76d5c892f68c753a3dcbe32ea9b833d6c5e8193`.

## Evidence and limits

The first rebuilt guest exposes both the missing space and the input leak; it
remains a failed diagnostic. One build rejects changing manifest membership
while native harness directories are created concurrently. The quiet retry
keeps the original checks and bounds. Report-writer and initial debugger
failures remain in the evidence, and debugger-paused time is excluded from
performance claims. Raw QEMU WAV headers have zero length fields; derived
excerpts preserve the recorded PCM, but audio quality remains unaccepted.

The [evidence archive](evidence/doom-keyboard-save-handoff-20261109.json.gz)
retains 2,791 file records, 990 local payloads, 671 references to preceding
archives and sixteen locally retained large-file references. SHA-256:
`5477a16687ccf1bd0379c63e9d9871e7460fb26d0c40f5d40b1f8674d1b561d6`.
Only the newly created save files are exported from HomeFS.

Full gameplay and smooth audio remain open.
TempleOS remains reference material and is excluded from these checks.
