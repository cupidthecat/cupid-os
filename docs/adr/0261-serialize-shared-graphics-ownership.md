# ADR 0261: Serialize shared graphics ownership across processes

## Status

Accepted on 2026-08-11.

## Context

Cupid OS renders the desktop, retained windows, and fullscreen programs into
one VGA back buffer. The gfx2d target, clip, blend mode, transform state,
surface pool, image pool, and font registries are also global. On one CPU,
cooperative scheduling usually hid that sharing. With four CPUs, the desktop
could begin a frame while a terminal process entered fullscreen mode.

The failure appeared in `gfxgui_test.cc`. The program entered fullscreen,
wrote a white font pixel at `(16,16)`, and immediately read it back. An
in-flight desktop frame could still draw the terminal title bar across that
coordinate. The four-CPU e1000 run then reported
`[gfxgui_test] FAIL font pixel`. Three repetitions against the old image did
not reproduce the failure, which confirmed a timing race but did not prove a
repair.

The old `fullscreen_active` integer only told the desktop what to do on its
next loop. It did not wait for a frame already in progress, identify an
owner, handle nested entry, or recover when a process exited without calling
the matching teardown function. Retained window painting also changed the
global gfx2d target without serializing against another process.

## Decision

Use one owner-token handoff for every process that can mutate the shared
render context. A token is the process ID plus one, leaving zero available for
the unowned state. The handoff records a fullscreen request, its owner and
depth, whether that owner has reached exclusive mode, and the current desktop
writer with its depth.

A new fullscreen owner publishes its request, then waits for the desktop
writer to drain. A fresh desktop writer may start only while no fullscreen
request is pending. The current desktop owner may re-enter so a modal or
retained presentation can finish while a fullscreen requester waits. The
final fullscreen release restores the main target, clip, blend mode, and
presentation state before it opens the gate. A process killed while still
waiting clears only its request; it does not reset state owned by the desktop
writer.

The handoff state lock saves and disables local interrupts during each kernel
transition. That keeps a remote kill or reschedule interrupt from stopping a
process while it holds the lock. Fullscreen acquisition may yield while it
waits for another live owner. Cleanup of a quiescent process may attempt at
most 4,096 handoff operations because the reaper already holds the big kernel
lock and cannot schedule another task safely. Exhausting that budget leaves
the process terminated and unreclaimed for the next scheduler reap.

The scheduler reaps detached terminated processes before choosing the next
task. Reaping releases GUI paint state, graphics ownership, and owned windows
before a PID slot can be reused. Cleanup is also called when a JIT program
returns. A same-PID nested JIT is rejected while that process owns render
state, so an inner invocation cannot release an outer frame's lease.

The remote-kill frontier uses a foreign kernel helper instead of Desktop
input, which is unavailable while fullscreen ownership is active. Each
request captures both the PID and its lifetime generation under the big
kernel lock. PID zero selects the caller and makes the helper wait until that
slot contains a different lifetime generation. An explicit PID uses the
requested delay. In either mode, the helper kills only the captured
generation; a reused slot produces a stale-PID diagnostic and remains alive.

Desktop frames, desktop modal drawing, popup menus, retained presentation,
retained window paint, and the legacy draw-frame/flip pair all use the same
writer boundary. Retained paint acquires ownership before it resolves or
allocates a window surface. It publishes its owner before selecting the
global target, and its normal or forced teardown restores the target, clip,
and blend mode before releasing ownership. Window creation, focus changes,
surface lifetime, and window destruction use the same boundary because they
can move the window table or free a selected surface.

Raw modal input uses the same boundary. Desktop keyboard reads take a writer
lease for each queue pop, while mouse-driven window mutations borrow the
lease around their state changes. A popup publishes its input-ready marker
only after it owns the writer. The guest harness waits for that marker before
it sends GodSong's dialog keys. A confirm dialog stops reading after its
first terminal key so one queued Escape cannot answer several dialogs.

Window-mode file dialogs keep a window ID and expected owner instead of a
`window_t *` across a yield. Each paint attempt resolves the ID again, checks
the owner and surface dimensions, and retries a busy ownership result. This
prevents a close, focus change, or resize on another CPU from turning a saved
pointer or stride into another window's storage.

The gfx2d image, bitmap-font, transform, theme, and Fontsys registries borrow
the writer lease for public operations. One-time initialization cannot erase
handles that another program published first. Resource removal marks a slot
unused and clears its pointer before freeing storage, so process termination
in the middle cannot expose freed memory through a live handle. Sparse glyph
cache removal trims only unused trailing slots; its high-water bound is not a
live-entry count.

The desktop icon registry and background configuration use that boundary as
well. Icon text getters copy label, path, and description data into separate
per-process views before releasing ownership, so a caller does not retain a
pointer into a mutable registry slot. Replacing a tiled background clears its
published pointer and dimensions before freeing the old pixels. Theme and
style getters expose immutable borrowed views, and render-path callers keep
those views const while they hold graphics ownership.

Raw draw, sprite, surface, particle, and borrowed-pointer APIs require an
outer fullscreen, retained-paint, legacy-frame, or shared-writer scope. The
kernel header records that contract. Active programs follow it. Fullscreen
Paint and the parity surface demo acquire ownership before initialization and
free their surfaces before the final release.

## Test-first and review findings

The first lease wrapped the desktop frame but missed one early `continue`.
That branch kept the desktop depth nonzero while fullscreen entry waited for
it, creating a deterministic deadlock. Releasing before the branch fixed the
balance, and the source contract now checks every exit.

A boolean request was also too weak. Two callers could enter together, and
the first exit could admit the desktop while the second still drew. Owner
tokens and same-owner depth replaced that model. The state then gained a
separate entered phase after review found that cleanup of a killed pending
requester could reset a still-active desktop target.

Cleanup at JIT return initially released one fullscreen depth. Nested entry
could therefore leave the desktop gated. A drain operation fixed normal
return, and process-reaper cleanup covers `exit()` and remote kill. Reaping
was added to the scheduler because waiting for a later `ps` or process-create
call could otherwise leave the terminated owner in place indefinitely.

Leasing only `gui_present_windows()` did not cover retained paint. The active
surface, clip, blend mode, transform state, and resource pools are shared, so
two offscreen painters could redirect each other's work even before either
presented. The lease moved to the whole begin-paint/end-paint scope. The
legacy draw-frame path received a matching scope instead of remaining an
untracked writer.

The file dialog first replaced its saved window pointer but still reused the
old surface stride after a yield and made only one cleanup attempt. The final
path validates the current dimensions for each acquired paint scope and
retries busy cleanup. Window-table mutations were then brought under the same
writer boundary so re-resolution remains valid for the whole paint attempt.

Resource review found free-before-invalidate order in image, font, sprite,
surface, and glyph-cache slots. The final order makes a terminated owner leak
at worst; it cannot leave a published handle pointing at freed storage.

The first GodSong gate waited for a program-local line and then slept for two
seconds before sending keys. That narrowed the race but did not establish who
owned the raw keyboard queue. Moving the final marker past popup acquisition,
leasing each desktop pop, and removing the timed wait made the boundary
observable. The first confirm loop also kept draining after it had chosen a
result. Breaking at the first terminal key preserves the remaining keys for
the later dialogs.

This decision explicitly reopens and supersedes ADR 0233's two-second
GodSong settling interval. A timed delay cannot prove raw-key ownership on an
SMP system. ADR 0233's AOT/JIT graphics workloads, asset checks, success
markers, and command budgets remain in force.

Final review found that the first dead-owner path still retried its state lock
without a limit even though this decision required bounded cleanup. The
focused contract failed on that mismatch. The reaper path now carries a
4,096-operation budget through nested fullscreen release, finalization,
owner inspection, and desktop-depth release. A busy result defers PCB
reclamation instead of spinning under the big kernel lock. Current-owner JIT
cleanup remains blocking because that process can complete its own teardown.

A later Standards pass found mutable theme and style pointers escaping their
registry. Their public types and all active render callers are now const. The
same pass through registry ownership found unleased icon and desktop
background state. Twenty-seven icon entry points and twelve background entry
points now serialize through the writer boundary; the icon string getters
return per-process snapshots rather than live registry storage.

The first lifecycle smoke proved only voluntary `exit()`. A shell-issued kill
could not reach a fullscreen owner because Desktop stops dispatching terminal
input while that owner holds the gate. The final sequence compiles both
fixtures first. The exit fixture acquires nested ownership, arms a delayed
generation watcher through a foreign helper, records its PID, and exits. A
second fixture reuses that PID, acquires nested ownership, prints its readiness
line, and arms a seven-second generation-bound helper. The earlier request
must reject the reused slot as stale; the second helper then kills the live
owner. A final AOT graphics process must reuse the same PID and complete its
workload. This separates voluntary exit, stale-generation rejection,
remote-kill cleanup, and PID reuse without relying on a guessed reuse delay.

## Evidence

The focused ownership, transform, GUI-smoke, and private binding suites pass
all 143 tests in 1.892 seconds. Their 51,938-byte log has SHA-256
`42aecd26ff815d952e40fd80b6f8367a0e1a5c2121d8391d78d7e7656e37d413`.
The ownership module covers desktop/fullscreen exclusion,
same-owner nesting, pending-owner death, JIT exit and reaping, retained and
legacy paint cleanup, registry serialization, stable file-dialog identity,
invalidation-before-free ordering, and raw modal input arbitration. The
current process, gfx2d, icon, desktop, GUI, theme, shell, CupidC, and CupidASM
translation units compile through the checked CupidC kernel wrapper.

The bounded-reaper contract first failed exactly the old unbounded path, then
passed all seven ownership tests in 0.485 seconds. Checked CupidC compiles of
`gfx2d.cc`, `desktop.cc`, `process.cc`, and `shell.cc` pass after the change.

The frozen production tree completes the normal root build and creates a new
200 MiB image at an absent path. Four-CPU e1000 and RTL8139 frontiers both
pass from private copies of that image. Each run records 36 passing lines,
zero panics, the exact generation-reuse cleanup sequence, a same-PID AOT
graphics recovery, HomeFS and dglibc, modal input, six USB storage lifetimes,
and both audio devices. Exact build, audit, image, and runtime hashes are in
the bootstrap log. The final doc-frozen image has SHA-256
`109b7683719f61ab37170917a3103fe308ba8d0f47dd7972ad5111ff7214c2cf`;
its e1000 and RTL8139 runs pass in 804.237 and 753.885 seconds.

## Consequences

Fullscreen entry now means that earlier desktop and retained writers have
finished before the caller touches the shared back buffer. Normal release,
JIT return, process exit, and remote kill reopen the gate without reusing a
stale PID owner. Retained and legacy window rendering no longer redirect a
different process's gfx2d target.

Graphics resource pools still do not record the PID that created each fully
published handle. A process killed after allocation can therefore orphan a
sprite, surface, particle system, image, bitmap font, or Fontsys face. The
ordering in this decision prevents cross-owner use-after-free, but repeated
abrupt exits can exhaust a finite pool. Owner-tagged resource reclamation is
a separate lifecycle change.

This decision does not change the i386 ABI, the checked five-tool seed, or a
build owner. `TempleOS/` remains read-only reference material.
