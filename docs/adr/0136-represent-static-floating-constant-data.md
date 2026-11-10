# ADR 0136: Represent static floating constant data

## Status

Accepted on 2026-07-27.

## Context

The shared CupidC path already parses decimal `float` and `double` constants
into exact IEEE bits for runtime expressions. Static-duration initialization
still sent every scalar leaf through the integer constant-expression path.
That rejected valid C such as the 64-entry cosine table in
`kernel/gfx/jpeg.c`.

Changing that table into integer bit patterns would make the source harder to
read and would hide a real compiler gap. CupidC needs a constant-data form
that preserves C's floating type and target representation.

## Decision

Add `CTOOL_C_INITIALIZER_FLOATING` to the public initializer model. The record
owns one target-width IEEE binary32 or binary64 value in `integer_bits` and no
runtime expression, string, address, or child metadata.

For a non-atomic static-duration `float` or `double`, parse the initializer
with the normal typed expression parser. Reduce a decimal floating constant
through parentheses and unary plus or minus. Convert directly between
`float` and `double` when the destination type requires it. The conversion
uses bounded integer arithmetic, rounds to nearest with ties to even, and
preserves signed zero. Temporary expression records are rewound before the
translation unit is published.

Keep static floating arithmetic outside this slice. `long double`, atomic
floating objects, hexadecimal constants, and decimal constants beyond the
existing parser range retain focused diagnostics.

Validate the new record at the frontend, Linear IR, and object boundaries.
The emitter writes its exact little-endian bytes through the existing static
object placement path. Positive zero is zero-filled when a writable object
can use `.bss`; negative zero remains initialized data because its sign bit is
part of the C value.

## Evidence

The frontend, Linear IR, and object contracts use the production JPEG values
`0.35355339f` and `-0.09754516f` inside a nested constant array. They also
cover positive and negative zero, a negative `double`, direct conversion in
both directions, and the ties-to-even narrowing case `16777217.0`. Separate
block-scope cases cover a scalar and a nested array.

The object contract checks every emitted byte, symbol offset, symbol size, and
section choice. It places positive zero in `.bss`, negative zero in `.data`,
and the constant table in `.rodata`. The block-scope object has deterministic
local symbols and one text relocation to each object, which proves that
emitted code can read both forms.

Linear IR rejects forged integer and atomic targets, excess binary32 bits,
and stray expression, string, address, or list metadata. Every failed
lowering leaves its input and job storage intact, and the original unit
lowers successfully afterward. The same validation walks every leaf in
file-scope and block-static initializer lists. The object boundary separately
rejects a wrong target and excess binary32 bits before it publishes output.
Negative frontend cases cover atomic storage, static arithmetic, and
`long double`.

The three focused `floating-scalars` contract modes pass after rebuilding
their native runners with strict warnings enabled. The complete native
Toolchain suite also passes.

## Rejected alternatives

Using the bootstrap host's floating conversion library was rejected because
the compiler must reproduce target bits during self-hosting.

Publishing a floating initializer as an integer record was rejected because
it would erase the semantic type and weaken boundary validation.

Treating negative zero as empty storage was rejected because `.bss` cannot
represent its sign bit.

Rewriting the JPEG table as hexadecimal integers was rejected because the
active C source should drive the compiler instead of working around it.

## Consequences

Compiler head can emit decimal `float` and `double` constants in file-scope
and block-static scalar or aggregate initializers without a host floating
library. The checked seed still needs a fixed-point refresh before the normal
build can transfer `kernel/gfx/jpeg.c` to CupidC ownership and rename it to
`.cc`.

Static floating arithmetic, `long double`, atomic floating access,
hexadecimal and wider-range decimal constants, comparisons, truth, SIMD, and
over-aligned objects remain separate capabilities.
