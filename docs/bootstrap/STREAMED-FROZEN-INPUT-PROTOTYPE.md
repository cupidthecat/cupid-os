# Streamed frozen-input prototype

## Scope

The separate 1,695-input prototype can freeze a complete 200 MiB input without
allocating its whole payload. It adds `cupidbuild_host_freeze_input_bounded` to
CupidBuild's host API. The private integration worktree has not adopted this
API yet. Normal recipes, installed seeds and the public snapshot layout retain
their current contracts.

The explicit unsigned 64-bit capacity must be between one and 2,147,483,647
bytes. Invalid capacities and runner transactions fail before creating a new
copy. Optional result storage is cleared on failure. Each registered input
keeps its own limit for live and frozen revalidation. Ordinary freezing and
requested payloads keep their 64 MiB limit; checked executable inputs keep
their existing admission rules.

## Copy and ownership

The copy uses the retained observer's ordered blocks of at most 65,536 bytes.
It writes only to an owned private file and retains the source observer until
the complete digest has been reread. Windows opens the writer through the
retained private directory and checks it against the created file's identity.
POSIX uses the owned descriptor. Writes retry interrupted calls; the completed
copy is flushed before sealing.

Before registering the input, the API checks the complete observer digest,
revalidates the live source, rereads the frozen copy and compares its identity,
size and digest. Existing no-follow ancestors, input/output alias rejection,
private ownership and cleanup checks remain active. Linux anonymous copies
receive their write, grow, shrink and seal protections after copying. Partial
copies are discarded through their owned handles.

## Evidence, 2026-10-07

Native diagnostic callers pass 31 cases across Windows and Linux. Fresh native
and complete prepared CupidC producers build the actual i386 API and caller on
both hosts. All twenty compile, disassembly and link phases pass. Corresponding
API objects and complete programs match within each host, and all four caller
objects match. Independent evidence is
`stack-frozen-input-build4-four-producer-independent.json`.

All 62 actual i386 runtime cases pass. They cover empty and small inputs, a
64 MiB plus one-byte input, complete 200 MiB copies, payload admission, live
and frozen drift, capacity excess, the ordinary limit, zero and high-word
capacities, prior-output preservation and namespace cleanup. Both Linux i386
producers copy the complete 200 MiB input under a 32 MiB address-space limit.
The native Linux caller passes the same memory control. Every case retains its
original 600-second limit.

Independent rereading checks all 1,695 source inputs, producer and library
bindings, all 93 native/i386 cases, 57 complete exported copies and the three
fixed-memory cases in
`stack-frozen-input-runtime2-six-producer-independent.json`. The original
ordinary API rejects an input above 64 MiB; its failure remains retained.

## Failed approaches

The first Linux stream copy reused the ordinary anonymous-file writer, which
sealed an empty file before copying. The corrected prototype creates the
owned empty descriptor, copies and flushes its payload, then seals it. The
first i386 link omitted required Windows publication support; the next link
included the full-path import stub twice. The final link uses the complete
matching publication libraries with one copy of that stub.

Both first Linux i386 callers returned 98 in the frozen-drift fixture. The
write was blocked and every byte stayed unchanged, but a rejected write to a
sealed memfd can still update its timestamps. The metadata guard correctly
rejected that change. A native kernel probe reproduced the timestamp change
after crossing the kernel's timestamp update tick.

The fixture now requires complete unchanged bytes whenever a write is blocked.
A successful edit must still fail frozen validation. Metadata changes caused
by a rejected write remain rejected. Repeated controls pass through all four
producers with API and runtime objects identical to the failed attempt. Only
the fixture changes; the original failures and sealed source captures remain
retained. No debug instrumentation enters the corrected source.

## Remaining work

The later 1,696-input copy adds the normal regression module and an explicit
`not_reached` ownership row for the optional C fixture. All six module runs
pass: 78 method selections, 75 executions and three expected Windows skips.
Independent rereading checks 141 invocations, 105 complete exported copies,
three fixed-memory cases, all twenty strict audit contracts and 26 existing
ownership controls. The CPP inventory stays at 422 tracked roots, four generated
roots and 53 strict hosted roots. Evidence is
`stack-frozen-input-regression3-six-producer-independent.json`. API, header and
C caller bytes match the preceding accepted prototype.

Apply the complete change to the integration worktree, refresh its audit and
rerun the affected ordinary publication controls. Then carry it through
committed preparations, paired release authoring and complete qualification.

The preserved public disk image still needs a separate previous-output role.
Ordinary input freezing must keep rejecting aliases of the transaction's
output. The previous image must be read through its retained initial-output
authority and checked at publication boundaries without treating the replaced
public pathname as an ordinary live input. Full-range snapshots, independent
disk validation, durable publication and the normal disk recipe handoff remain
open.
