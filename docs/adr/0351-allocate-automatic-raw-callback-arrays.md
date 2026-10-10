# ADR 0351: Allocate automatic raw callback arrays

## Status

Accepted on 2026-08-25.

## Context

The private in-OS CupidC compiler retained signatures for raw callback arrays
with static storage, but rejected the same fixed declarator inside a function.
That left ordinary local callback tables outside both JIT and fixed-address AOT
even though scalar automatic callbacks already used typed cdecl calls.

## Decision

Allocate a fixed-size automatic raw callback array as contiguous four-byte
frame slots. Retain one interned callback signature on the array symbol so
indexed loads, stores, copies, and calls use the existing compatibility and
cdecl conversion rules.

Zero every element when the declaration executes. A braced initializer fills
elements in source order, accepts compatible defined or later-defined function
targets, permits a trailing comma, and leaves omitted elements null. Reentering
the block initializes a fresh array. Evaluate a side-effecting index once.

Require an explicit positive bound. Keep unsized automatic arrays, raw callback
array parameters, raw callback arrays in records or classes, and
multidimensional raw callback arrays outside this slice. Preserve the existing
frame-capacity, signature-pool, translation rollback, and recovery limits.

## Evidence

The first JIT and AOT executions failed with the previous unsupported
diagnostic. The completed contract covers initialized and uninitialized arrays,
zero-filled trailing elements, later targets, frame reinitialization, typed
copies and calls, and one-evaluation indexed assignment. Negative cases cover
an omitted bound, an incompatible function signature, frame overflow,
rollback, and a valid retry.

The two focused tests pass in 5.215 seconds. The complete private callback ABI
module passes all 318 tests in 60.519 seconds. Manifest-bound CupidC builds of
`cupidc_parse.o` and `cupidc_elf.o` pass; the parser object is 508,488 bytes
with SHA-256
`27af09c37216cde40f20e3dfc4bef8f2b41e8bc8f48f772ce7cf661c9c4c9371`.
`git diff --check` also passes.

## Consequences

Private CupidC programs can keep short typed dispatch tables in automatic
storage without moving them to global data. Parameter, field, unsized, and
multidimensional raw callback arrays remain explicit later work.

This private compiler change does not move a normal build object, alter the
hosted CupidC seed, or justify renaming an inactive `.c` file. It supersedes
ADR 0330 only where that decision listed automatic raw callback arrays as
unsupported.
