# Freeze user ABI inputs and isolate runtime boots

- Status: Accepted
- Date: 2026-07-27

## Context

The external-program build checks six kernel and public declarations before
CupidC compiles a user source. The checker read each file once and compared
the captured text, but it did not revisit the live files before returning
success. An early kernel header could change while a later input was being
read. The captured declarations would still agree even though the live tree
no longer described that ABI.

The hello and ls runtime checks also booted the staged image directly. Cat
already used a private image because its setup command replaces a HomeFS
file. Booting the first two checks from the shared image left their ordinary
filesystem writes available to later checks and contradicted the final
evidence record.

## Decision

Capture the exact bytes and decoded UTF-8 text of all six ABI inputs. Perform
the existing semantic comparison against those snapshots. Before returning
the report, reopen every approved path in the original order and require its
bytes to match. A missing file, symlink, read failure, decoding failure, or
byte difference fails the operation with the changed path in the diagnostic.

Keep `tools/user_syscall_abi.py` in the Make dependency closure. It defines
the check but is not one of the six ABI declarations.

Pass `--private-image` to the hello, ls, and cat guest checks. Staging still
updates the selected image once before the checks start. Each QEMU process
then receives its own temporary copy. Cat copies the hostile fixture into
that copy before it runs.

## Rejected alternatives

Comparing only parsed declarations was rejected because an edit could leave
the captured pair valid while changing the live build input.

Relying on Make timestamps was rejected because the mutation can occur while
one recipe is running. The checker owns the consistency of its read
operation.

Keeping direct boots for hello and ls was rejected because a successful boot
may still write filesystem state. Runtime checks should not depend on their
execution order.

## Evidence

The live-drift negative changes `kernel/core/types.h` after its snapshot while
the checker reads `user/cupid.h`. The old checker returned success. The new
checker reports the changed kernel path, publishes no report, and succeeds
on the next unchanged call.

Two build-graph tests first required three `--private-image` options and
failed against the old single option. They pass after the Make recipe gives
each guest check a private copy.

All 14 syscall ABI tests and both focused runtime build contracts pass.

The clean normal image builds in 402.1 seconds. The external-program gate
then completes all three private-image boots in 907.1 seconds. Each full
success expression matches once for PID 4, and all 69 unique configured
rejection strings have zero matches in every user log. The selected image
remains unchanged by the guest processes.

The separate four-CPU smoke passes in 55.1 seconds. The final canonical
repository gate passes both production frontiers. All 763 tests pass in
3,295.058 seconds with one expected platform skip, and the complete Make
gate returns in 3,370.9 seconds.

## Consequences

The ABI checker now fails if its live declarations drift during one
verification. The compile and link wrappers retain their own immutable input
snapshots and publication checks. No filesystem lock spans the separate Make
recipes, so edits after the ABI checker returns remain outside this
operation.

Each external-program boot starts from the same staged bytes and cannot
change the selected image or another program's test state.
