# ADR 0358: Add a native checked CupidObj runner

## Status

Accepted on 2026-08-27.

## Context

The normal build invokes CupidObj 186 times through the Python checked-seed
runner. Most of those calls wrap one text or binary input, while three build
generated installation sources. CupidObj already owns the output bytes, but
Python still freezes and validates the six-tool seed, launches the private
executable, and checks the live seed afterward.

CupidBuild already performs the same seed checks for guarded assembly
transactions. Reusing that parser and host boundary is the shortest safe path
to native orchestration, provided the generic runner does not inherit the
publication claims of the typed assembly commands. The source command must
also exist before the paired seeds can carry it, so source capability, seed
promotion, and Make ownership are separate commits.

## Decision

Add `cupidbuild run` with one admitted tool, `cupidobj`. The command accepts a
seed manifest, working directory, optional positive timeout, and the tool's
arguments after a required `--` separator. The default timeout is 300 seconds,
and the accepted range is one second through one day.

The runner freezes the manifest and all six tools, checks exact seed-directory
membership, parses the promoted v2 contract, verifies every declared size and
digest, and validates the host execution profile. Linux does not create a
`.cupidbuild-run-*` directory or any other runner namespace. It copies the
manifest and six tools into fully sealed anonymous memfds, pins the requested
working directory with an open descriptor, and launches CupidObj from its
retained descriptor. Native builds use `fexecve`; the static i386 host uses
`execveat` with `AT_EMPTY_PATH`. Arguments remain separate in both cases.

The POSIX child calls `fchdir` before it remaps standard output and standard
error. If the retained tool descriptor occupies 0, 1, or 2, the child first
duplicates it above the standard descriptors. The `dup2`, launch-pipe read and
write, and wait loops retry `EINTR`; `dup2` also retries Linux `EBUSY`. Output
is captured in anonymous memfds and fully sealed after the child exits. A
close-on-exec launch-status pipe separates a failed `fchdir` or executable
launch from the child's own status. Successful exec closes the pipe, so an
exact CupidObj exit of 125 remains 125. The custom i386 startup exports
`cupid_linux_syscall5` for the `pread64` and `execveat` calls used by this
path.

Windows creates a private root below the requested working directory and pins
the working directory, private root, and files with handles and captured
identities. It rechecks the working-directory handle and live path before
launch. The private CupidObj image is opened without write or delete sharing,
and that handle stays open through `CreateProcessA`. Windows applies its
command-line quoting rules, rejects a command longer than 32,767 characters,
and forwards captured streams in binary mode so their bytes do not change.

Standard output and standard error stay private until CupidObj exits and the
live manifest, tool files, and seed membership all match their frozen
snapshots. The runner then forwards the two streams in the same order used by
the Python caller and returns CupidObj's status. Drift suppresses both streams
and fails the invocation. Timeout and launch failures also pass through the
live-seed check before they are reported.

Cleanup is part of the command result. Linux closes anonymous memfds and its
working-directory descriptor, so there is no runner directory to remove.
Windows verifies each known file and the private root against their captured
identities before deleting through handles. A file that changed in place still
belongs to the runner and is removed. A replacement has a different identity,
so cleanup preserves it and fails. Windows retries sharing violations from a
recently exited process. An unexpected file, private-directory replacement,
or persistent sharing error also makes an otherwise successful invocation
fail without deleting an unowned path.

This command guards a checked-tool invocation. It does not claim the
destination lock, inspection, or atomic replacement owned by CupidBuild's
typed assembly transactions. CupidObj remains responsible for its output
behavior.

The manifest is the native runner's trust root. CupidBuild checks its complete
shape, declared identities, provenance form, membership, execution profiles,
and live snapshots. It cannot compare the manifest to the Python module's
separate promoted-seed constants because those constants are deliberately not
compiled into the self-referential CupidBuild image. The later Make handoff
therefore depends on a reviewed, checked-in production manifest and its full
six-image prerequisite closure.

## Evidence

The command tests were written before the implementation and first failed
because source-head CupidBuild did not recognize `run`. The native Windows
CupidBuild CLI suite completed 66 tests in 65.934 seconds with three expected
platform skips. The host-runner Python module completed eight tests in 0.962
seconds with four POSIX cases skipped. Its dedicated Make contract passed.
The standard Toolchain test target now builds and runs the same native
contract, while `test-cupidbuild-host-runner` keeps focused reruns short.
The CupidASM source suite passed all six tests in 3.771 seconds. Strict
compilation passed for both the Windows adapter and the freestanding i386
adapter. The timeout-and-seed-drift case also passed and confirmed that drift
takes precedence over the timeout result.

The full build-graph module ran 111 tests in 732.137 seconds. Its only initial
failure was a stale exact inventory: the runner added 26 real `sizeof` uses,
moving the checked total from 6,630 to 6,656 across the same 179 files. The
corrected selector and deterministic audit then passed.

The final normal `make -j2 all` passed after the exact-size contract failed
closed on the changed outputs and its policy was updated. The verifier accepted
all 16 exact artifacts. `kernel/kernel.bin` is 9,515,260 bytes,
`kernel/kernel.elf` is 9,744,412 bytes, and
`kernel/kernel.elf.pass1` is 9,613,340 bytes. Whole-image CupidDis inspection
and disk-image staging passed in the same build.

A private four-vCPU E1000 QEMU smoke ran with
`--cpu max --verify-smp-runtime`, executed `/bin/ls.cc`, and passed in about
47.5 seconds. CupidC compiled 911 code bytes and 71 data bytes and completed
JIT execution. The 33,113-byte log has SHA-256
`7b0711ce849107f838aed61f4238ce6edb79d787911edbd39194ec8868cdcf24`
and no rejected runtime marker.

A final full Windows Toolchain rerun was attempted after those focused checks.
It could not start because WSL failed while translating the Linux seed after a
WSL VM and service outage. Earlier full Windows and Linux green baselines
remain useful pre-edge-fix evidence, but they are not final evidence for this
runner revision. Final WSL-backed Toolchain verification remains open.

**TODO:** Repeat the full Windows Toolchain run after WSL can translate the
Linux seed, then record the current Linux-backed result.

## Consequences

Source-head CupidBuild can now replace the Python runner for ordinary
CupidObj calls after the command reaches both checked seeds. This commit does
not change the Make graph, a production owner, or the current counts of 448
Python and four CupidBuild participations.

The admitted tool list stays intentionally narrow. CupidLD's two kernel links
are boot-critical composite transactions and need their own output and
inspection handoff. The remaining Python paths also include typed Hostbuild
work, fixed-point coordination, contracts, packaging, and image construction;
they are not generic runner substitutions.

Captured standard output and standard error are each limited to the existing
64 MiB hosted file boundary. No active source changes suffix: all active
CupidC translation units already use `.cc`. `TempleOS/` remains untouched
reference material.
