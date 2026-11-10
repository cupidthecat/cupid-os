# ADR 0405: Bound Doom music production per call

Date: 2026-09-24

`cup_music_pump` produces only the complete 512-frame chunks that fit in its
4,096-frame ring when the call begins. Space freed during synthesis becomes
work for the next call. The previous loop recalculated free space after every
chunk. When the interrupt consumer drained at least as fast as synthesis,
that loop could keep a Doom clock query from returning indefinitely.

The producer remains on the Doom main thread, called from `DG_GetTicksMs`,
`DG_SleepMs`, and initial playback prefill. The interrupt callback only copies
published samples or emits silence on underrun. Sample generation, stereo
order, MIDI timing, ring capacity, and the publication barrier are unchanged.
An empty stopped ring still receives all eight chunks. The budget bounds the
number of chunks, not the duration of an individual synthesis call.

The ring retains one producer and one consumer. Only the producer advances
the write counter; the consumer advances the read counter after copying
available samples. Concurrent reads can only free space while the producer
uses its entry budget. The write position stays chunk-aligned, and unsigned
counter subtraction retains occupancy across wraparound. Moving synthesis to
another task would require separate song-state ownership, wakeup, and
shutdown rules; this change adds no asynchronous producer.

## Evidence and limits

The original function passes seven of the nine retained regression cases and
fails both draining-consumer cases. The fixed function passes all nine with
host compilers and checked Cupid-built PE32 and ELF executables. These tests
execute the extracted production function, model a consumer draining during
rendering, and check bounded return, sample order, stereo data, capacity,
partial fills, and counter arithmetic. Integration adds a tenth case in which
the producer's write publication itself crosses `UINT32_MAX`; the earlier
wrap case exercises wrapped occupancy but does not cross that boundary during
the call. The ten-case integration suite passes on both hosts and through
checked Cupid-built PE32 and ELF executables. The original function still
fails only the two draining-consumer cases. The harness does not model hardware interrupt ordering or validate
the cost of OPL synthesis.

Checked Windows and Linux production compiles produce the same 22,204-byte
object, SHA-256 `de0b6d2292cac198bdd6168f7a732c6ea3a63765d2a853437e21c615f66d67c6`.
A private build from tree `9eebb94d097b07875163c7278985c8b85e744f4b`
changes only the sound source and the measured raw-size policy. Both kernel
links, both instruction inspections, sixteen artifact checks, and image
creation pass. The first inspection exceeded its existing deadline during
concurrent builds; the unchanged retry passed. These observations do not
establish why the timing varied. The private raw kernel is 9,553,172 bytes;
that historical size is not the policy for a later embedded manual.

The original runtime reference is image
`fbc3d3e98160e252b44d42e6e061ac65ca127b4eef0ae74374e1f7ae6fd9f686`.
The newer fixed image is
`da38661b43347d4ec2a3c20132c2d6381b3fa881cd072b77e45a96a1b83af6c2`.
They are different whole-image bases. The controlled comparison is the
function regression, followed by exact-image validation of the fixed call
path. The older trace stays at game tic zero in the initial demo clock query
while music counters advance. The newer four-CPU guest reaches graphics
initialization and records game tics 37, 90, 143, and 197 across 180.113 host
seconds. Its demo pointer advances four bytes per tic after the thirteen-byte
header. Live samples are sequential, not atomic snapshots or a throughput
benchmark.

The newer image still fails the explicit 1,200-second timedemo command
deadline. The run exits 1 after 1,292.067 seconds of total wall time without
the required completion message. Its source image stays unchanged, and no
panic appears in that run. Full timedemo completion, interactive gameplay,
audio quality, save/load, and reboot persistence remain open. The older EHCI
DMA-ownership panic remains unresolved; this producer fix does not explain or
repair it. The hardware quiescence rule in ADR 0109 remains intact.

The retained evidence is under
`build/bootstrap/20260920-ehci-doom-diagnosis/`: the red/green and checked-tool
reports, source/object identities, private build records, and exact-image
`fixed-summary.json`. The final integration below repeats the build after
embedded documentation settles, its measured size gate, and a private runtime check.
No checked seed refresh or compiler behavior change belongs to this fix.

## Integration with the promoted toolchain

The active integration follows seed promotion `88bb2d26`. Its sound-source
bytes match the retained candidate. The ten-case repository replay reproduces
the two baseline watchdog failures, then passes with the fixed producer.
Independent PE32 and ELF replays with the promoted seeds also pass all ten
candidate cases and reproduce the two baseline failures. Reports are under
`build/bootstrap/music-pump-promoted-seed-e4f2ed65/`.

Fresh Windows and Linux kernel, image and user-program builds pass with the
current embedded manual. The measured raw kernel is 9,567,636 bytes. Both
private four-CPU E1000 disassembly/shell smokes pass and preserve their source
images. An independent comparison rehashes 1,503 inputs, sixteen artifacts and
431 link inputs; images and all three user programs match across hosts.
Each host also passes the 24 music-producer and artifact-policy tests.

The exact-image IWAD diagnostic fails the 1,200-second command deadline,
exiting 1 after 1,268.995 seconds including startup. It preserves the staged
source image and reports no panic. Four active samples record game tics 48,
114, 182 and 249; the initial clock query returned. Two sampled stacks reach
OPL synthesis through the producer. These sequential reads establish partial
progress, not timedemo completion or a synthesis benchmark. The pinned demo
contains 7,117 single-player commands. Interactive gameplay and audio quality
remain unverified, and the earlier EHCI ownership failure remains open.
Integration evidence is under `build/bootstrap/music-integration-d8e2931e/`.
