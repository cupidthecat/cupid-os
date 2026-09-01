# ADR 0198: Lay out private CupidC mixed-width calls

## Status

Accepted on 2026-08-01.

## Context

Private CupidC evaluated call arguments from left to right and pushed each
result immediately. The call paths then reversed pairs of argument blocks to
produce cdecl source order. That worked for equal four-byte slots, but the
eight-byte case could place the words of two `double` arguments incorrectly.
Calls that mixed a `double` with a four-byte value stopped with an unsupported
diagnostic. Direct calls, function-pointer calls, and both method-call forms
also carried separate copies of the reversal logic.

Callees assigned every named parameter a four-byte offset. A parameter after
a `double` therefore read the wrong stack address even if the caller happened
to arrange the outgoing words correctly. The feature13 guest test avoided
this boundary by expanding its tolerance calculation at every use.

## Decision

Give every represented private CupidC call value one explicit cdecl slot
width. `int`, `char`, object pointers, function pointers, `float`, and the
implicit method `self` pointer use four bytes. `double` uses eight bytes with
its low word at the lower address. Void results, SIMD vectors, and aggregate
values receive a focused unsupported call or parameter diagnostic until they
have a complete ABI.

Calls continue to evaluate arguments from left to right. One shared routine
then permutes complete four-byte words in place until the outgoing area holds
arguments at increasing addresses in source order. The word identities keep
the low and high halves of each `double` together. Direct calls,
function-pointer calls, object methods, and pointer methods all use this
routine. Each caller reclaims the sum of the slot widths.

One parameter-binding routine uses the same slot-width rule. It assigns the
current EBP-relative address, then advances the next parameter address by the
complete width. Method `self` remains the first four-byte parameter.

The feature13 program now defines
`feature13_within(double, double, double, int)` and calls it for nine runtime
checks. A `[feature13-call] PASS checks=9` marker makes that guest ABI path a
required part of the four-CPU frontier.

## Evidence

The first runtime cases exposed the old behavior directly. A two-`double`
function returned 30 instead of 34, and mixed calls and methods stopped with
the earlier unsupported diagnostic. A void-valued argument also passed
through as if it occupied an integer slot. The feature source still lacked a
real helper call.

The focused private ABI module now passes ten tests. It executes matching and
alternating slot widths, direct and stored function-pointer calls, object and
pointer method forms, left-to-right side effects, and exact 24-byte cleanup.
It also locks the unchanged argument order of a four-word popup call, compiles
the complete feature13 source, and checks useful diagnostics for unsupported
arguments and parameters.

The GUI terminal smoke module passes 95 tests in 2.080 seconds with the new
guest marker. Fifteen private unary, comparison, truth, update, and binding
tests pass in 2.770 seconds. Checked CupidC compiles the changed parser into a
deterministic 290,416-byte object with SHA-256
`f330b802b80e2625a97801592f2a267d97734da5f9ae8dbe5132689e15e5695b`.
The strict frontier compiles all 155 production sources twice with zero
boundaries. Both object sets total 3,719,100 bytes and match byte for byte
against the 445-input snapshot with SHA-256
`543c7bb3e4946967835fe81daeb6d895d661c03961021681a34b5236cfa20423`.
An uninstrumented private four-vCPU QEMU run reaches
`[feature13-call] PASS checks=9` and completes the full GUI frontier in 233.5
seconds.

## Rejected alternatives

Evaluating arguments from right to left was rejected because it would change
CupidC expression semantics to hide an ABI layout problem.

Treating every value as four bytes was rejected because it truncates
`double`, disagrees with the private compiler's floating transport, and
leaves later parameter addresses wrong.

Keeping separate reversal loops for each call syntax was rejected because the
same scalar ABI must not depend on how the callee is named.

Leaving the feature13 calculations expanded was rejected because the active
guest should exercise the language capability it depends on.

## Consequences

Private CupidC can now pass arbitrary mixtures of its represented four-byte
scalars and pointers with eight-byte `double` values. Callers, callees, and
methods agree on placement and cleanup without changing source evaluation
order.

This changes the embedded JIT and AOT compiler but moves no production source
owner. Aggregate values, SIMD vectors, variadic type promotion, structure
returns, and a shared ABI with hosted CupidC remain separate work.
