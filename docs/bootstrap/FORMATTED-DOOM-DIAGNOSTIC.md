# Formatted-image Doom diagnostic

The private four-CPU Windows QEMU guest renders the pinned Freedoom Phase 1
level and quit prompt. Game tics advance from 58 to 335 and the renderer frame
counter from 11 to 67. Movement/fire reports are recorded, and the screenshots
show changed views and ammunition. Doom returns to the shell, but the first
`ls` fails with `undefined variable` after the long-held quit confirmation.
The cause of that failure is not established.

This is a failed diagnostic, not gameplay, clean-handoff or audio acceptance.
Debugger attempts include an observation timeout, misassociated asynchronous
stop packets and a paused guest. Explicit continue and monitor readback later
confirm that the same guest resumes. These intervals cannot support a frame
rate claim. Earlier attempts retain the unavailable private-symbol failure and
an incomplete `-warp` argument; the final command uses `-warp 1 1 -skill 3`.

The closed AC97 capture has 46,240,622 stereo 16-bit frames at 44,100 Hz,
6,468,467 active frames and peak magnitude 8,510. An independent RIFF/PCM reader
checks the complete payload without changing QEMU's zero length fields.
Nonzero audio does not establish smooth music. Source image, ELF, WAD and
guarded production inputs remain unchanged.

## Isolated synthesis comparison

A separate harness links the exact production MIDI/OPL objects with CupidLD
and the qualified Linux startup/runtime. It renders 32,768 stereo frames from
the pinned GENMIDI data with nine active MIDI channels. Native 32-bit and
64-bit source oracles render the same workload. All nine 131,072-byte outputs
match with SHA-256
`69db577234bc2d493f8c50e8fe73cad05a5959f7afa681ea6b00b1a77134b1f5`.

| Build | Median wall time, seconds, three runs |
| --- | ---: |
| Production Cupid-built objects | 0.723313 |
| Native 32-bit, `-O2`, same CupidLD/start/runtime | 0.057390 |
| Native 64-bit, `-O2` | 0.051526 |

The production-object run takes about 12.6 times the native 32-bit wall time
for this workload. This supports investigating code-generation cost; it does
not isolate guest scheduling, interrupt delivery or every audio stall. The
optional native tools produce no OS artifacts. The first system-libc 32-bit
oracle lacks development headers; a later include-order attempt also fails.
The passing 32-bit oracle uses existing repository ABI declarations with kernel
headers first. No dependencies or source semantics change.

The [diagnostic archive](evidence/formatted-doom-diagnostic-20261110.json.gz)
retains 190 file records and 140 payloads, including raw audio, screenshots,
failed probes, source, objects, all benchmark outputs and controllers. Every
exported payload passes independent size and SHA-256 checks. Private whole
disk images and production image/ELF inputs are bound by hash rather than
embedded. The archive has 30,696,735 bytes and SHA-256
`e4c1845f57c04fa0ac0f6a6a371120721627f4f8b594ce7ea6c4748c7c4d309e`.

Smooth audio, timedemo completion, the new handoff failure and the equivalent
Linux guest comparison remain open. Issue #41 remains open. Installed seeds
and normal-build ownership do not change.
