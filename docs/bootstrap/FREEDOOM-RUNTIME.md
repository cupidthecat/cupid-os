# Freedoom runtime handoff

## Four-CPU initialization sampling, 2026-10-05

The exact accepted image and matching kernel ELF are copied into a private
fixture and staged with the pinned 28,795,076-byte Phase 1 IWAD, SHA-256
`7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d`.
The debugger enumerates every thread-info page and samples all four CPUs at
30-second intervals for 180 seconds after the Doom command. This differs from
the older probe's deadline phase and is retained as a separate observation.

All six samples show game tics and renderer frame count at zero; the screen
buffer is allocated. One CPU repeatedly reaches `DG_Init` through `vfs_mkdir`,
HomeFS flush, reserved FAT replacement and cluster allocation. Other samples
include timer writeback waiting for the big kernel lock and AP idle paths.
Allocation advances between samples. The absent initialization completion
marker does not mean that `DG_Init` was never entered.

The run exits one after 267.090 seconds including startup. It proves neither
a returned draw call nor a rendered gameplay frame. Source image, staged
fixture and matching ELF remain unchanged. The initial probe enumerated only
the first thread-info page; the separate paged retry retains complete CPU
coverage. Both runs and their debugger transcripts remain in
`cupid-doom-initialization-probe-20261005/`.

A controlled extraction of the active allocator measures 2,284,936 reads for
a 317-cluster replacement. Sector scanning reduces this to 9,708 reads with
identical cluster order and both FAT copies. Thirteen native and thirteen
Cupid-built methods pass per host. This supports the isolated FAT improvement
in ADR 0425; it does not yet establish that the full initialization timeout is
fixed. Fresh image, unchanged frame criteria and the existing 1,200-second
timedemo remain required. Input, audio, save/load and reboot persistence stay
open.

The changed FAT source compiles through normal Make on both hosts to identical
65,672-byte objects. Its freshly linked image passes independent source,
artifact, FAT-preservation and strict four-CPU boot checks. Repeating the same
paged 180-second observation reaches graphics setup. The first five samples
remain at zero game tics and frames; the sixth reads five tics and renderer
frame count one, with an active stack in `OPL3_Generate4Ch`. The run still exits
one after 226.224 seconds because no draw-entry/return pair completes within
the window. The source image, staged fixture and exact ELF remain unchanged.
`cupid-fat-doom-initialization-20261005/initialization-independent.json` records
that progress and failed frame boundary without converting it to gameplay
acceptance. The base image is
`3353849bd88f6015a0750498a21e017dc276e9238f306d20c3e63bda58befbaa`;
the private IWAD fixture is
`c6d45d2d49ab583a826c9cdec3a74edb018d975d0b918289a6638975faee11fc`.

The separate full-audio `doom -iwad /disk/wads/freedo~1.wad -timedemo demo1`
replay fails the existing 1,200-second command-completion deadline. It returns
one after 1,290.333 seconds including startup. The serial log reaches graphics
setup and later reports `ehci: could not quiesce async schedule before submit`.
This is a retained error observation, not a diagnosis of the timeout. Source
image, staged fixture and exact kernel remain unchanged. Evidence is
`cupid-fat-doom-initialization-20261005/timedemo/independent.json`, with the real
command, declared 64 MiB TCG cache and complete serial log. It proves no
completed demo, input/audio quality, save/load or reboot-persistence acceptance.

## Absolute Windows staging correction, 2026-10-04

The original drive-colon failure is reproduced by the minimal `C:/a:/b`
parser test. It returns host source `C` instead of `C:/a`, before any FAT
operation. The parser now skips a leading ASCII drive colon when a second
separator follows. With only one separator, existing one-letter source syntax
retains its meaning. A colon later in a guest destination remains guest text;
the FAT writer still owns its filename validation.

Eight regression methods cover forward and backward slashes, drive-relative
and UNC sources, spaces and Unicode, both image/stage CLI entries, legacy
relative and POSIX paths, missing separators and relative guest destinations.
A real Unicode host source passes the public CLI and stages into a compact FAT
image. An independent directory and cluster-chain reader verifies its bytes.
A relative destination is rejected before writing, preserving both image and
source. The complete existing host-build module plus these methods passes all
98 methods on Windows and Linux in 17.608 and 25.589 seconds.

The original absolute Windows IWAD command succeeds against a fresh private
copy of the accepted image in 17.079 seconds. Its entire 209,715,200-byte result
matches the earlier relative-path staged image, SHA-256
`da0e46d6d6ce446728699de7f1e8e85c47768d5a52ff9f0e4fb0bf19c05384cd`.
Source image and IWAD bytes remain unchanged through the replay. Evidence is
`stage-parser-fix-1-independent.json` and both `hostbuild-stage-suite-retry-1-*-closed.json`
records in the private diagnostic directory below. This proves staging behavior;
it does not turn the frame timeout into runtime acceptance.

## Private frame-return diagnostic, 2026-10-04

The accepted `9e566013` source image, with the older installed seed pair, is
copied into a private fixture and staged with the pinned Phase 1 IWAD. A second
copy is the writable guest disk. The source image and staged fixture remain
unchanged through the probe. This is separate from candidate-seed runtime
acceptance.

The probe verifies the linked `DG_DrawFrame` entry bytes against guest memory
and arms a debugger breakpoint there. Its intended frame evidence requires
`GS_LEVEL`, positive game tics and renderer frame count, a non-null
`DG_ScreenBuffer`, the actual call's stack return address, return from the draw
function, and a colored 640-by-400 viewport. `DG_Init` alone cannot pass it.
The debugger uses the [GDB remote protocol](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Packets.html).

The executed retry reaches neither the draw-entry breakpoint nor `DG_Init`
within its explicit 180-second frame observation. It exits one after 247.374
seconds including startup, with no panic. The log records a 1,295,697-byte
`HOMEFS.SYS` rewrite requiring 317 clusters. Allocation advances through
cluster index 200 before the observation ends; no rendered frame is proven.
This locates the last observed progress boundary, not a performance cause.
The existing 1,200-second timedemo requirement is unchanged.

The first staging helper fails before writing because the optional stage CLI
splits an absolute Windows source path at its drive colon. A fresh retry uses
the repository-relative path used by Make and succeeds. The first guest helper
then fails before typing Doom because its XML parser rejects QEMU's undeclared
`xi:include` prefix. A separate namespace-aware retry passes that handshake
and reaches the frame observation above. All original failures remain retained.

Private evidence is under `cupid-doom-frame-return-probe-20261004/`:
`prepared-frame-probe-retry-1.json`, `frame-return-result-retry-1.json`,
`serial-retry-1.log`, `rsp-transcript-retry-1.json`, and the original helper
failures. The linked symbols, verified entry bytes, debugger packets, kernel,
ELF, IWAD and image identities are retained with the exact command. Gameplay
frame acceptance, input, audio quality, save/load and reboot persistence remain
open.

## Current exact-image runtime replay, 2026-09-30

The accepted `ea2f135a` image repeats the pinned Freedoom diagnostic with four
CPUs, `max`, e1000 and the unchanged 1,200-second command deadline. The command
still fails: exit 1 after 1,281.107 seconds including startup. No completion
message or panic is recorded. The original image and private staged source
image remain unchanged. Four active samples record game tics 34, 88, 142 and
197 after the initial startup sample. Some sampled stacks reach OPL generation
through `cup_music_pump`; another reaches frame conversion. These sequential
reads are neither a statistical profile nor proof of a performance cause.

An ignored sustained-note harness links the actual fresh Linux OPL production
object against the verified Cupid-built hosted startup and runtime. Swapping
only that object for GCC i386 `-O0` and `-O2` oracle objects produces identical
176,400-byte PCM output for 44,100 frames. Three alternating measurements have
median native elapsed times of 0.398, 0.068 and 0.026 seconds respectively.
Zero, malformed and excessive frame requests are rejected by each program.
This supports a compiler code-generation investigation. It does not measure
QEMU throughput, Doom's MIDI workload, audible quality or whole-game runtime.
Other bootstrap work runs concurrently, so these figures are diagnostic values.

Evidence is under `build/bootstrap/native-profile-validation-258bb5f3/`:
`doom-runtime-ea2f135a-v1/fixed-summary-v1.json`, its exact-image symbols and
five sample records, and `opl-cost-v2/result.json` with compiler commands,
source/object identities, PCM digests and individual times. The first harness
attempt used an unsupported compiler CLI switch; the corrected invocation uses
`-c`. No production source or vendored OPL implementation is changed by this
measurement. Timedemo completion, interactive gameplay, audio quality,
save/load, reboot persistence and the earlier EHCI ownership failure remain open.

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
