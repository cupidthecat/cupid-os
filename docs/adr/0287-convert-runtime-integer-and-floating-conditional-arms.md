# ADR 0287: Convert runtime integer and floating conditional arms

## Status

Accepted on 2026-08-14.

## Context

Hosted CupidC already applied the usual arithmetic conversions to runtime
integer and floating arithmetic. Its static initializer evaluator also accepted
integer and floating conditional arms. The runtime conditional path rejected
the same represented types before Linear IR could see them.

That split was not a C language rule. It was an unfinished parser boundary.
Changing source to avoid a conditional would have hidden the compiler gap and
made later self-hosting work harder.

## Decision

A runtime conditional may mix a non-atomic `float` or `double` arm with a
represented signed or unsigned integer arm no wider than four bytes. CupidC
applies the existing usual arithmetic conversion to the integer arm. A
`float` arm gives the expression type `float`, and a `double` arm gives it
type `double`.

Conditional control flow remains lazy. Linear IR lowers the condition, takes
one branch, converts the chosen integer value when needed, and joins with one
typed floating result. The emitter reuses its existing signed and unsigned
integer-to-floating conversion sequences and x87 value storage. The frontend
change needs no new IR operation, object format, relocation, or ABI rule.

An eight-byte integer opposite `float` or `double` remains outside the
represented runtime conversion slice. An integer opposite `long double` also
remains unsupported. Both cases receive conditional-specific diagnostics.
Atomic floating arms keep their existing focused rejection.

ADR 0288 later removed the integer and `long double` limit by admitting the
existing x87 conversion under the usual arithmetic rules. ADR 0289 later
removed the eight-byte integer limit for `float` and `double` by admitting the
wide x87 conversion already present in the emitter.

## Evidence

The frontend contract first reproduced the old rejection for an `int` and
`float` conditional. Its positive fixture now checks signed `int`, signed
`short`, unsigned `int`, and unsigned `char` opposite both floating widths.
Each expression publishes exactly one `USUAL_ARITHMETIC` conversion on the
integer arm. At the time of this decision, a negative fixture checked the
exact eight-byte diagnostic. ADR 0289 replaces that historical boundary with
positive wide coverage.

The Linear IR contract publishes nine conditional branches across the
floating-conversion fixture. Each of the four new functions keeps one typed
conversion inside the selected branch and joins at the expected floating
type.

The object contract emits 45 functions in 7,144 text bytes with fingerprint
`7186D278`, 46 symbols, and 97 relocations. Its decoder-driven i386 model runs
both branch directions for all four integer kinds. The cases include negative
signed values, `UINT_MAX`, and unsigned-char value 255. Repeated focused runs
of the frontend, IR, and object `floating-conversions` modes pass. Twenty-five
neighboring floating, conditional, boundary, and static-long-double contract
modes also pass across the three public seams.

The promoted i386 Linux seed compiles the complete changed
`toolchain/cupidc_frontend.cc` under the production Windows profile in 212.4
seconds. The source-head native compiler performs the same compile in 1.6
seconds. Both produce the same 1,059,736-byte object with SHA-256
`becfcde1cf3e20b457b6f3c73b5356fb3aed7a64ca13f8869476616635a69c1d`.
This proves that the checked seed can carry the source change without reducing
the frontend.

## Rejected alternatives

Converting both arms before the branch was rejected because C evaluates only
the selected arm. Eager conversion would be the wrong execution model once an
arm contains side effects or traps.

Rewriting active source to use an `if` statement or explicit cast was rejected.
The conditional operator is ordinary C, and CupidC already had the required
conversion and emission machinery.

Silently accepting eight-byte integer mixes was rejected at this stage. A
wide implementation had to preserve all eight bytes and provide its own
executable proof. ADR 0289 later met that requirement through the existing
x87 64-bit conversion path.

## Consequences

Source-head hosted CupidC accepts one more standard runtime expression shape
without weakening active source. The current checked seed predates this
extension, and the active build graph has no source that requires it. No OS
object, build owner, artifact, ABI, dependency, or source suffix changes.

The next seed convergence must carry this frontend and contract update before
the checked toolchain can claim the capability. Atomic floating conditionals
remain explicit follow-up work. ADR 0288 completes the source-head integer and
long-double conditional case, and ADR 0289 completes the represented wide
integer case for `float` and `double`.
