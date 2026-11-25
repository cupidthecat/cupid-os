# ADR 0389: Use the pinned proc directory for anonymous tool launches

Date: 2026-09-19

Status: Accepted

Anonymous POSIX publication transactions run checked tools from the retained
`/proc` directory. Their file arguments remain retained `/proc/self/fd/N`
paths. This keeps a checked tool's relative writes away from the repository
without adding a private on-disk directory. It is not a process sandbox.

The flat-transaction work in ADR 0377 created the retained directory and a
public CLI test for this behavior. The general typed-tool launcher still
selected its inherited working directory, however. Guarded assembly, including
raw and ISO operations, used that launcher for both CupidASM and CupidDis.
JPEG wrapping and kernel-symbol authoring shared it. The separate symbol-row
capture path also inherited its working directory.

The host adapter now selects the retained `/proc` descriptor for those
anonymous transactions in both paths. Explicit private-directory launches
keep their existing behavior. The generic checked runner still uses the
working directory requested by its caller, and Windows is unchanged.

The original watcher failure hid a second problem: its one million `nop`
lines exhausted CupidASM's job arena at line 444,976. The test now uses 10,000
lines and checks the command status and streams before checking the observed
directory. That smaller fixture returned success before the adapter repair
while the watcher observed the repository directory 6,717 times. The `/proc`
assertion therefore remains a real behavior check, not a timing concession.
Sentinel files and the transaction cleanup assertions remain unchanged.

A separate timeout-and-seed-drift test waited for an `after-tool-launch` hook
that existed only in the Windows process adapter. On Linux it never changed
the seed, and the runner correctly reported a timeout. The native POSIX test
adapter now pauses after a successful fork, under the existing hosted race
macro. A failed pause kills and reaps the child and closes the launch-status
pipe. The hook is absent from ordinary production builds and the static
Cupid Linux runtime. The test still requires live-seed drift to take
precedence over the timeout diagnostic.

The failed-pause regression supplies a directory as the ready-file path, so
the actual ready-file open fails immediately. A native driver links the
existing race-build objects and calls the public checked-runner API. Its fork
wrapper stops a real child and waits for that stopped state before the parent
continues. After the rejected pause, the driver requires SIGKILL reaping,
`ECHILD`, closed launch-pipe descriptors, and an unchanged descriptor count
inside the still-running caller. The Python test preserves the exact process
diagnostic and requires no filesystem residue. A parent-death signal prevents
an interrupted test from leaving the stopped child behind.

An ignored diagnostic build that suppresses read-only pipe closure fails this
test with descriptor-census result 95 in 9.870 seconds. The unmodified
implementation passes the new regression together with both original watcher
cases: three tests in 19.271 seconds.

Both reported Linux cases pass in 20.606 seconds. Ten representative Linux
cases pass in 21.213 seconds: ISO success and failure, active raw images,
inspector failure, JPEG success and failure, kernel-symbol success and
failure, quoted generic-runner arguments, and timeout cleanup. Strict i386
syntax checking of the custom Linux host adapter also passes. Native Windows
passes the same ten cases plus timeout-and-drift in 18.607 seconds, with no
skips. These runs use
exact copies of the complete `962e476b` seed cohorts beneath an ignored local
validation directory. They do not promote a seed or prove the full OS build.

No OS source is simplified, no C file changes owner, and `TempleOS/` remains
read-only reference material. Paired source reconstruction and checked-seed
publication remain separate gates.
