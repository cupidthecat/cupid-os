# Freedoom runtime handoff

Cupid OS pins the official Freedoom v0.13.0 Phase 1 IWAD and its complete
upstream release archive under `third_party/freedoom/0.13.0/`. The fixture is
opt-in. The normal asset-free image and its recorded hashes do not change.

## Build and inspect the first image

From the repository root:

```sh
python -m unittest tests.test_freedoom_fixture
make WAD_SRCS=third_party/freedoom/0.13.0/freedoom1.wad all
make WAD_SRCS=third_party/freedoom/0.13.0/freedoom1.wad run
```

At the Cupid OS terminal, start Phase 1 explicitly:

```text
doom -iwad /disk/wads/freedo~1.wad
```

The host image writer stages Phase 1 under that FAT16 short name. Running
`doom` without an explicit path also discovers it.

Record the image hash, kernel hash, QEMU version, CPU count, CPU model, NIC,
and serial-log hash. Use a private image copy for tests that write saves or
configuration.

## Startup diagnostic checkpoint

Private pre-correction `16a86f5b` images found the staged IWAD and entered
fullscreen mode, but the `doom -warp 1 1` probe did not observe graphics
initialization within its 180-second observation window. The last setup
progress was HomeFS save-directory creation and the start of its file rewrite;
no gameplay frame was proven.

A fresh, prelaunch-hash-verified image reproduced the delay with only
`mkdir /home/doom`, without entering Doom. The existing GUI smoke helper used
four `max` vCPUs, E1000, and `--timeout 180`. The log showed JIT compilation
of `/bin/mkdir.cc`. HomeFS then started a 1,295,697-byte, 317-cluster rewrite of
`HOMEFS.SYS`; neither durable publication nor JIT completion was observed.
The harness returned `command did not complete (1/1)`, with no
panic or early-QEMU-exit diagnostic. It cleaned up QEMU normally and retained
the private image and logs.

The same `mkdir /home/doom` command passed on September 20 with a private copy
of the asset-free final-manual image, four `max` vCPUs, E1000, and the unchanged
180-second timeout. HomeFS flushed all 1,295,697 bytes and the JIT returned.
The smoke also passed its SMP and post-command survival checks. This retry
followed recovery of host allocation headroom; it does not prove why the
earlier run timed out. Directory creation no longer reproduces the immediate
startup blocker under these conditions. An IWAD-backed gameplay gate remains
open. The [bootstrap log](LOG.md) preserves both failed observations and the
successful retry. Normal images and OS source are unchanged.

A separate final-manual image with the pinned IWAD still fails before proven
gameplay. The command
`doom -iwad /disk/wads/freedo~1.wad -timedemo demo1` reached the 1,295,697-byte
HomeFS rewrite, then panicked with
`ehci: DMA ownership could not be revoked after transfer`. No completed
timedemo or `DG_Init` marker was observed. The root cause remains open; the
asset-free directory-creation pass does not clear this IWAD-backed failure.

The panic is in `ehci_submit_sync` after `ehci_quiesce_async` fails. That
helper can try to stop the asynchronous schedule, then attempts to halt
the controller before releasing DMA-owned storage. The current serial log
does not record the command/status registers at either failure boundary, so
it cannot distinguish a halt-acknowledgement timeout from a remaining
asynchronous-schedule state. Capture those observations in the next focused
diagnostic. Keep the ownership check: returning from this failure could let
a caller release a buffer still owned by the controller.

[ADR 0257](../adr/0257-descend-private-multidimensional-simd-arrays.md)
records an earlier four-CPU failure in the unchanged EHCI cleanup path.
That history predates this publication handoff; it does not establish a
shared root cause with the current IWAD probe.

The first attempt also exposed a host-test input gap: the GUI harness could
not type `~`. It now sends QEMU's shifted grave-accent key and validates a
complete command before sending any key. The full 138-case helper suite
passes, including FAT short-name input and rejection without partial typing.
This repairs the harness, not the guest's EHCI failure.

## Later reproduction and music-producer checkpoint

Three subsequent private probes against the same pinned IWAD image completed
the HomeFS write without reproducing the EHCI panic, but missed their unchanged
300-second timedemo deadline. A separate 1,200-second diagnostic also timed out.
Its log reached `ST_Init`; that marker records entry, not completion. Advancing
lock counters rule out one continuously held lock across the sampled interval,
not every possible stall. The EHCI failure remains unexplained.

A later stack-and-state probe found a separate progress failure. Four samples
over 180 seconds placed the Doom task in `cup_music_pump`, called by the initial
`G_DoPlayDemo` clock query. Music counters advanced while `gametic` stayed zero
and the demo pointer remained thirteen bytes into the lump. The producer
recomputed ring space after each rendered chunk, allowing an active consumer
to keep the call running.

An isolated candidate limits each pump call to the complete chunks that fit
when it starts. Nine actual-producer tests distinguish the change: the original
exceeds the watchdog in two draining-consumer cases; the candidate passes all
nine with host compilers and checked Cupid-built Windows and Linux executables.
The patch preserves prefill, sample order, ring capacity, and the consumer path.
That candidate is now applied to active source. The final integration results
are recorded below.

The candidate's private guest image shows sustained demo progress: four samples
over 180.113 seconds record game tics 37, 90, 143, and 197, with demo offsets
matching thirteen header bytes plus four bytes per tic. The initial clock query
returned. Full timedemo completion still fails at the explicit 1,200-second
deadline; no panic is reported and the source image remains unchanged. This
image is newer than the unfixed reference, so the guest observations are not a
one-change image comparison. The producer regressions provide the controlled
red/green evidence.

The retained patch, nine-case regression, exact-image symbols, samples, logs,
and hashes are under `build/bootstrap/20260920-ehci-doom-diagnosis/`.
`music-pump-fix.patch` and `fixed-summary.json` identify the candidate and guest
evidence; `remaining-performance.md` records the next measurement boundary.
Before changing synthesis or scheduling, measure guest elapsed time, synthesis
work, and underruns separately. These findings do not establish rendered-frame,
input, audio-quality, save/load, or reboot-persistence acceptance.

## Runtime work still open

The first implementation slice should add a repeatable guest gate that starts
the pinned IWAD and proves a rendered gameplay frame without a panic. Extend
that gate in small steps to cover:

1. keyboard and mouse input that changes player or menu state;
2. AC97 and PC-speaker output during an IWAD-backed session;
3. menu-driven save and load through `/home/doom`;
4. shutdown, reboot, and successful reload of the saved game;
5. both e1000 and RTL8139 four-vCPU configurations used by the existing Doom
   recovery frontier.

Keep the existing missing-IWAD and return-to-shell checks. A successful manual
launch is useful diagnosis, but it does not close any runtime acceptance item.
Update issue #29 and the bootstrap capability, migration, dependency, and log
records with each executed boundary.


## Current producer integration

The active producer now uses the entry-capacity budget. Its ten-case regression
adds a write-counter publication that crosses UINT32_MAX. The original source
fails the two draining-consumer cases; the fixed source passes all ten.
Promoted-seed PE32 and ELF replays also pass. ADR 0405 records the synchronous
producer decision and preserves the earlier guest evidence and limitations.
Paired kernel, image and user-program builds pass with the current embedded
manual and measured artifact policy. Both four-CPU E1000 disassembly/shell
smokes pass. Independent verification confirms matching images and user
programs and unchanged source images through those private smokes.
The fresh exact-image IWAD timedemo fails the same 1,200-second command deadline.
It exits 1 after 1,268.995 seconds including startup, preserves its staged source
image and reports no panic. Active samples record game tics 48, 114, 182 and
249. The pinned demo has 7,117 commands; these samples prove partial progress
only. `fixed-summary.json` retains the terminal result and evidence identities
under `build/bootstrap/music-integration-d8e2931e/`. Timedemo completion,
interactive gameplay, audio quality and the earlier EHCI failure remain open.
