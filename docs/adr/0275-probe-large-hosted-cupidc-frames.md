# ADR 0275: Probe large hosted CupidC frames

## Status

Accepted on 2026-08-13.

## Context

CupidC reserved every fixed function frame with one stack-pointer adjustment.
That is safe for small frames, but a large adjustment can jump over a guarded
stack page before the operating system has a chance to commit the next page.
The native Windows compiler exposed this with a frame larger than 18 KiB in
switch lowering. Committing the complete one MiB PE stack fixed that process,
but it did not make generated functions safe under ordinary demand-grown
stacks.

Changing active source to avoid a large frame would hide a compiler and ABI
gap. The emitter needs to honor the page-growth contract while preserving the
existing bytes for normal frames.

## Decision

Keep the existing one-step reservation for fixed frames of 4,096 bytes or
less. For a larger fixed frame, subtract at most 4,096 bytes at a time and
touch the new stack page after every subtraction. The final partial step is
also touched. The probe is a read-only `test dword [esp], esp`, so it does not
change the frame contents or add a runtime dependency.

Naked functions still omit the compiler prologue. The kernel entry path keeps
its existing zero-frame behavior. A failed oversized compile must preserve
the caller's output, and the same command must be reusable after the source is
corrected.

## Evidence

The public object test covers frames just below one page, exactly one page,
one page plus one word, and more than two pages. It checks every reservation
step and probe, including the last partial page. Focused object tests also
cover naked IPI wrappers, the kernel entry, output preservation, and recovery.
Five focused tests pass in 23.344 seconds. A separate red run failed at the
missing first probe before the emitter changed, then passed in 23.807 seconds.

The broader self-host run reached generation three but exceeded its 904-second
harness limit without reporting a test failure. That timeout is not fixed-point
evidence and remains recorded as such.

## Consequences

Hosted CupidC output changes only for fixed frames larger than one page. Those
functions can now grow a guarded stack without relying on a fully committed PE
stack. The checked seed still carries the one MiB commit policy from ADR 0274
until a promoted compiler proves the new prologue through the complete native
Windows fixed point.
