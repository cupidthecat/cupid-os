# Retained observer root identity

The separate source in `C:/Users/admin/cp7/observer-root-identity-source1`
provides physical-root comparison for native image discovery. Linux retains
the same source under `/var/tmp/observer-root-identity-source1`. The normal
99-input producer source and installed seeds remain unchanged.

## Interface and custody

`cupidbuild_host_observers_share_root(first, second, same_out)` compares the
device and identity captured by two open observers. Before returning a result,
it applies each observer's complete ordinary read-only revalidation. Identical
pointers need one validation. Success returns one and writes a normalized zero
or one to `same_out`. Failure returns zero, clears a supplied output and poisons
both supplied observers while preserving existing diagnostic text. Output
storage remains live and disjoint from the observer state; operations are
serialized with every other observer and transaction operation.

The operation creates no files and grants or borrows no source authority.
Discovery can use the result to select an already retained observer before
binding a transaction. Root metadata, streamed file digests, namespace bindings
and missing-file observations keep their ordinary checks. The primary
publisher's permission for its own root mutations does not apply here.

Exactly two existing paths change in this separate copy:
`toolchain/cupidbuild_host.cc` and its header. The two new paths are
`toolchain/tests/cupidbuild_observer_root_identity_contract.cc` and
`tests/test_cupidbuild_observer_root_identity.py`. The copy retains the original
190 controls from `external-publisher-large-source1` before those changes;
build and execution records cover all 192 current source/support controls.
Its private producer inventory contains 106 inputs and remains separate from
the normal 99-input cohort.

## Actual execution evidence

Native Clang and the new normal-source stage-four Cupid compilers build and run
the actual contract on both hosts. All fourteen methods pass on Windows through
both callers. Linux runs twelve through each caller and skips the two Windows
case/separator alias methods. Across all four callers, 56 methods are selected,
52 execute and four skip. Every executed method makes one recorded program
invocation.

Positive cases cover separately opened observers for one root, one observer
compared with itself, distinct roots, Unicode and Windows path aliases. Useful
negative cases cover missing arguments, poisoned observers, root-directory
metadata changes, a streamed same-size payload edit with its timestamp restored,
and appearance of an observed missing nested leaf. Each rejection checks cleared
output, observer poisoning and successful handle closure. Distinct valid roots
compare false without poisoning either observer.

Build and runtime labels are
`observer-root-identity-{native,checked}-{build,contracts}-{windows,linux}1`.
Native compilation uses C11, optimization and the existing strict warning/error
profile under 180 seconds. Cupid compilation retains two workers and a
360-second per-source bound, with 120 seconds for assembly and 180 for linking.
Both changed host objects and new fixture objects pass strict disassembly under
sixty seconds. Checked programs retain the existing exact static execution
profiles, including the already required private Windows query import. The
normal-source compiler comes from the retained mixed-mutation preparation;
the remaining build tools match their qualified installed parents.

Every runtime invocation keeps sixty seconds; Linux also keeps a 32 MiB address
space. The paused mutation cases share one total invocation deadline.
`observer-root-identity-independent1/closed.json` passes in 18.604 seconds.
It rereads all source copies, actual programs, original compiler/preparation
files, build commands and limits, runtime receipts and complete result bytes.
The four callers agree on every shared output, and the complete checked fixture
objects agree between hosts. All 99 normal compiler inputs and fifteen installed
seed files remain unchanged. The original 190-control source copy is preserved.

## Remaining discovery work

The new comparison is still private. The complete image command must integrate
absolute path discovery, physical-root reuse, optional absence, source selection
and observer lifetimes with the accepted publisher. This contract does not
establish execution of the complete publisher body with the changed host copy,
committed producer qualification or normal recipe adoption.

Windows observer opening still requires a drive-rooted absolute path and rejects
UNC roots. UNC discovery needs a separate retained-root implementation and
useful positive and negative filesystem tests. The stage argument splitter's
UTF-8/path syntax support does not supply that host capability.

Normal ownership remains 449 CupidBuild and three Python actions across 452
transforms. Full Doom runtime acceptance and the existing standalone Windows
cleanup failure retain their separate requirements. TempleOS remains read-only
and excluded.
