# ADR 0453: Compute wide integer mutations before narrow stores

## Status

Applied to bootstrap source on 2026-10-08. Native and Cupid execution contracts
pass. Paired refreshed-manual kernel/image and normal user builds, all four
strict private boots and complete independent comparisons pass.
Committed-source qualification and seed carriage remain pending.

## Context

The ordinary CupidBuild publisher contains
`total += overhead + inputs[index].snapshot.size`. Its i386 `size_t` destination
is four bytes, while the captured extent is eight bytes. ADR 0074 supported wide
destinations but deliberately rejected a narrow destination whose computation
type was wide. Casting away the extent would change the source requirement.

## Decision

Accept an existing represented wide integer computation type for an ordinary
narrow integer or supported integer bit-field compound assignment. Reuse the
frontend's integer promotions and usual arithmetic conversions, the existing
wide operation, assignment conversion and exact-width store. The computation
finishes before narrowing. For example, dividing an unsigned word by
`0x100000001ULL` yields zero; narrowing the divisor first would yield a different
result.

The lvalue address and right operand are each evaluated once. The stored result
remains available to a surrounding expression, including chained assignments.
Supported nonvolatile bit fields retain neighboring bits. Existing volatile
ordinary integer and full-storage-unit bit-field rules remain in force.

Only the computation-type guard and its header comment change. No IR kind,
emitter operation or calling convention is added. Shift counts still use a
represented four-byte value. Atomic and boolean mutation, wide shift counts
and partial volatile bit-field mutation retain their useful rejections.

## Evidence

The execution fixture checks every compound operator, byte/halfword/word and
enum destinations, mixed signedness, divisors and remainders above four GiB,
single operand evaluation, chained results, volatile exact stores and adjacent
bit fields. Six negative inputs require the original diagnostics and preserve
the complete previous output and timestamp. Existing frame-load and wide-pointer
regressions remain in the selection.

The separate normal-source preparations pass all three generations on both
hosts, with 291 complete products and 97 byte-identical fixed-point pairs.
Both new Cupid compilers pass all ten methods per host. Independent checking
compares complete outputs across forty native and Cupid caller/method
combinations and verifies twelve native IR/object contracts. Exactly two of the
99 producer paths differ from the qualified hosted library source. Private host
and publisher extensions are excluded.

[The implementation record](../bootstrap/MIXED-WIDE-MUTATION.md) retains the
application, source/producer bindings, failed fixture copies, fresh worktree
controls and remaining acceptance. Installed seeds remain the qualified
`a1cc8f3a` pair. This source capability does not transfer normal disk recipe
ownership.
