# ADR 0440: Fold pure unsigned rotate leaves

## Status

Implemented privately on 2026-10-06. Both native object controls pass. Both current Cupid seeds compile the
emitter and produce the same object. Full compiler qualification, source
frontier comparison and OS acceptance remain open.

## Context

The guarded publisher repeatedly computes complete SHA-256 digests. Its C
rotate helper uses the ordinary shift-and-OR expression. CupidC emits that
helper through stack operations, including repeated parameter loads and two
variable shifts. Large publication controls exposed the cost while retaining
all identity and digest checks.

## Decision

Recognize a complete, pure, fourteen-instruction unsigned rotate leaf in the
object emitter. Require two distinct parameters of the same unqualified
unsigned 32-bit type, a matching return type, no local frame and no function
code-generation attributes. Check the instruction kinds, types, conversions,
references, operation fields and the exact complementary constant 32.

Emit the existing cdecl parameter loads into EAX and ECX, then use the shared
x86 encoder for `ROR EAX, CL` or `ROL EAX, CL`. Preserve the ordinary function
prologue, return, section alignment and symbol placement. Resolve parameter
offsets through the existing ABI helper, including reversed source order.
The selected leaf has no branches, calls or side effects.

Keep ordinary emission for signed or wide operands, qualified parameters,
mixed types, different source values and a different complementary constant.
The optimization is independent of function names. It adds no public IR
records or language syntax. Counts outside the C expression's defined range
have no new result guarantee.

## Evidence and limits

The private runtime fixture covers both directions, reversed arguments and
unsigned long, which occupies four bytes on this target. It exercises six
values across counts 1 through 31. Eight nearby expressions retain ordinary
emission. Object controls also check deterministic output, rejection of an
invalid function type, allocation rollback and recovery with the valid unit.

A retained Linux benchmark calls the unchanged SHA byte interface 3,200 times
with a fixed 65,536-byte block. Both outputs match an independent digest of
that block. The old object takes 28.726 seconds; output from the native emitter
prototype takes 21.159 seconds, about 26 percent less elapsed time. The total
compression input is 200 MiB. This benchmark does not establish a complete
file digest, a publication result or a qualified compiler generation.

The initial object control used byte offsets for short frame displacements,
but the shared encoder emits four-byte displacements. Its corrected checks
read the actual ABI offsets. The invalid-type control also expected an emitter
diagnostic; IR lowering rejects that record first. Both failed reports remain
retained. Neither correction changes emitted code.

The first checked Windows capacity suite passes nine cases, skips the POSIX
memory case and times out while replacing a 200 MiB previous output. The Linux
suite reaches its original whole-suite deadline after large-case timeouts.
The prototype publication controls retain the same deadlines and complete
guards. Their outcome must be checked separately from the compression result.
