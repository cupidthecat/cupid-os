# ADR 0299: Pass private SIMD values through cdecl

## Status

Accepted on 2026-08-15.

## Context

Private CupidC already treats `float4` and `double2` as complete 16-byte values.
It can create them, store them, update them, place them in fixed arrays, and
evaluate packed arithmetic. Active `simd_intrin.h` also declares 29 functions
that use those types. Calls to the named intrinsics have a special inline path,
but an ordinary helper could not accept or return either vector type.

The existing private cdecl path supports four-byte scalar and pointer slots and
eight-byte `double` slots. It evaluates arguments from left to right, then
permutes complete words into source order before the call. Its permutation
identity and scratch arrays allowed only two words per argument. Function
pointers retain neither parameter types nor a useful result signature.

The hosted i386 ABI has a separate aligned-call contract. Extending the private
runtime must not claim that alignment unless every private caller supplies it.

## Decision

Allow `float4` and `double2` in fixed-prototype direct function and method
parameters. Each value occupies one inline 16-byte cdecl stack slot in
low-address word order. The argument permutation carries four words per
parameter, with identities spaced by four so adjacent vectors and scalars
cannot collide. The caller cleans the sum of all 4-, 8-, and 16-byte slots.

Keep source-order evaluation. A vector expression is stored to its outgoing
slot with `MOVUPS`. Callees advance later parameter offsets by 16 bytes and use
the same unaligned-safe move. The slot is a by-value copy. Const qualification
is retained on the parameter symbol, so a const vector parameter is readable
but cannot be assigned or incremented.

Return `float4` and `double2` through XMM0. A return expression must match the
declared vector type. A bare return in a vector function and a return of the
other vector type receive a focused diagnostic. Prototypes carry the same
parameter and result metadata as definitions.

Pack private call slots at four-byte granularity. A vector may follow method
`self`, an integer, or a `double`, so its address is not necessarily aligned to
16 bytes. `MOVUPS` is required, and this decision does not extend the separate
hosted call-alignment promise from ADR 0050.

A fixed SIMD prefix may appear before scalar variadic arguments. A SIMD value
in the variadic tail is rejected because no fixed parameter type describes its
slot. Apply the same rejection to unprototyped SIMD calls. Function-pointer
signatures remain erased in this increment, so SIMD arguments and results
through a function pointer fail explicitly. Retain the declared function-
pointer result type only long enough to report the result case clearly.

Keep named `_mm_*` intrinsic lowering unchanged. Those names continue to emit
their packed instruction sequences inline rather than calling the ordinary
ABI.

## Evidence

Five focused tests failed first at the old SIMD parameter and call boundary.
The completed private call-ABI module passes all 168 tests in 24.403 seconds.
Its execution cases cover direct and nested `float4` and `double2` calls, mixed
4-, 8-, and 16-byte arguments, all four raw vector words, left-to-right side
effects, exact cleanup, methods, statement-form methods, prototypes, XMM0
returns, pass-by-value mutation, const parameters, a fixed SIMD variadic
prefix, and private AOT output.

Negative cases reuse the same compiler state after rejecting mismatched fixed
arguments, SIMD variadic tails, unprototyped calls, function-pointer arguments
and results, const mutation, bare vector returns, and mismatched vector returns.
The GUI and frontier contract module passes all 125 tests and now requires
`[feature14-call] PASS float4=4 double2=2 nested=2 calls=6` before feature 14
can complete.

Audit regeneration and deterministic replay pass, and the full fail-closed
audit module passes all 100 tests. A poisoned-host normal build completed in
668.5 seconds after the expected size-policy measurement moved only the three
changed kernel rows. The checked seed compiled both the updated private
compiler and feature-14 guest sources. A private four-vCPU e1000 boot then
compiled `/bin/feature14_simd.cc` in the OS, printed the six-call marker,
reported overall PASS, and completed the JIT without a panic.

## Rejected alternatives

Do not pass a vector by pointer merely to fit the old four-byte slot. That
would change value semantics and make callers rewrite ordinary helper APIs.

Do not split a vector into four source-level scalar parameters. The source type
is one value, and the compiler already has complete 128-bit storage and result
semantics.

Do not use aligned packed moves without proving every mixed private call site.
Method `self` and ordinary scalar slots can place a vector at a four-byte
boundary.

Do not guess a SIMD layout for variadic, unprototyped, or function-pointer
calls. Their missing signature metadata needs a separate design.

## Consequences

Ordinary private CupidC helpers can now compose packed values without relying
on intrinsic-name lowering. The active feature-14 guest exercises this through
nested calls of both vector types.

This changes compiler-head JIT and AOT behavior but moves no build owner and
adds no host dependency. The checked seed can still compile the updated private
compiler source, so no seed promotion is required for this slice.

SIMD pointers, record fields, allocation with `new`, array parameters, row
values, lane updates, computed vectors, signature-bearing function pointers,
and a fully aligned private call boundary remain open.
