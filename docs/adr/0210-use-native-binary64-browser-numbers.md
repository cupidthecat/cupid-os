# ADR 0210: Use native binary64 Browser numbers

## Status

Accepted on 2026-08-01.

## Context

The Browser's JavaScript interpreter stores numbers as `double`, but two
integer helpers stood between the stored value and most numeric decisions.
They multiplied a difference or a truth operand by one million and converted
the result to `int`. Values closer than one millionth compared equal, tiny
nonzero values became false, and a large finite result could leave the range
of `int`. The workaround predated private CupidC support for floating
comparisons and truth tests.

Numeric tokens had a separate loss. The lexer recognized the fractional part
of a decimal literal but discarded it, and AST nodes kept only an integer
slot. A script could therefore store binary64 results but could not spell the
same values accurately in source.

Changing the Browser's numeric tables from `int` to `double` exposed a private
CupidC defect. One-dimensional fixed `float` and `double` array symbols fell
through the four-byte integer allocation and access path. A `double`
declaration therefore looked valid but overlapped adjacent values and returned
its low word as an integer.

## Decision

Use the private compiler's native `float` and `double` operations throughout
the interpreter. Equality and order compare the binary64 operands directly.
Numeric truth is true only when a value is nonzero and not NaN, which keeps
both signed zeros and NaN false. Division follows IEEE behavior instead of
returning zero when the divisor is zero. Remainder by zero produces NaN.

Keep decimal integer, fraction, and exponent tokens in a `double` lane. AST
numeric nodes copy that lane rather than narrowing through an integer slot.
The exponent accumulator is capped before applying at most 400 decimal
steps, so a long exponent cannot overflow an integer or create an unbounded
loop.

Format NaN and positive or negative infinity explicitly. Finite formatting
keeps the existing six-decimal-place boundary for now.

Teach private CupidC to retain the declared element type on one-dimensional
fixed array symbols. Global, automatic, block-static, and persistent REPL
`float` and `double` arrays reserve four and eight bytes per element. Indexed
reads use `MOVSS` or `MOVSD`, and indexed writes keep values in the scalar XMM
lane. Plain assignment accepts the same integer and floating-width conversions
as a scalar variable. Arithmetic compound assignment uses `ADDSS`/`ADDSD`,
`SUBSS`/`SUBSD`, `MULSS`/`MULSD`, or `DIVSS`/`DIVSD`. `sizeof(*array)` reports
the declared scalar width.

Require a positive bound for each one-dimensional fixed `float` or `double`
array in this boundary. Check the count and stride before multiplication, then
check static or REPL storage against the remaining data section. This prevents
a wrapped allocation from publishing a smaller object than the declared bound.

Keep parsed fixed parameter types on direct function and method symbols. A
known fixed call converts `int` or `char` to `float` or `double`, converts
between the two floating widths, and truncates a floating argument for an
`int` or `char` parameter. Represented pointer categories and integer null
forms can fill a pointer parameter slot. Integer and `char` arithmetic uses
integer promotion, and explicit floating casts accept `char` through the same
integer conversion instructions. A parsed variadic tail widens `float` to
`double` and promotes `char` to `int`. Function-pointer calls, kernel bindings,
and calls without fixed parameter metadata retain their source-width behavior.

Reset subscript metadata at each primary expression. Array symbols,
address expressions, pointer casts, and `new` results publish only their own
known stride, while an ordinary call result cannot inherit a stride from an
earlier expression. This does not add floating pointer dereference.

Multidimensional floating arrays remain unsupported in the private compiler
and now fail with `floating arrays support one dimension`. Fixed SIMD arrays
also fail instead of being allocated as four-byte integer elements. Bitwise
and shift compound assignments on floating arrays receive their own type
diagnostic. Arrays embedded in structure or class fields remain unsupported
and receive a specific diagnostic.

Add `browser --selftest`. It runs without creating a window and combines direct
binary64 helper checks with scripts sent through the actual lexer, parser, and
interpreter. Its 17 fields cover close finite values, large signed values,
negative zero and its reciprocal, NaN comparison and truth, NaN and signed
infinity formatting, decimal fractions, signed and uppercase exponent
literals, relational order, division and division assignment by zero,
remainder by zero, the 400-step exponent cap, and malformed-exponent rejection.

## Evidence

The focused source contracts check all five Browser numeric tables, the
double token and AST lanes, native comparisons, truth handling, special-value
formatting, bounded exponents, and the early self-test path. Private CupidC
runtime contracts execute global, local, and block-static arrays at both
floating widths. They cover element stride, `sizeof`, integer and
floating-width conversions, and all four arithmetic compound assignments.
The REPL declaration path uses the same element metadata and checked storage
reservation. Negative contracts check nonpositive and overflowing bounds plus
the multidimensional, SIMD, field-array, and bitwise compound diagnostics.
Focused contracts exercise integer-to-double fixed-call conversion, `char`
promotion and floating conversion, variadic `float` widening, and fresh
pointer-result stride metadata. The implementation also contains the wider
fixed-call conversion matrix described above. The GUI frontier contract
requires all 17 self-test fields in order, rejects the failure marker, and
requires normal CupidC JIT completion.

The private compiler contract module passes all 28 array, call, conversion,
initializer, metadata, and diagnostic cases. The broader 142-test private and
production compiler set passes in 123.925 seconds, and the final 143-test
Browser, call-ABI, floating comparison, truth, update, and GUI contract set
passes in 7.136 seconds. A strict checked-seed frontier compiles 155 sources
twice, producing 3,749,796 matching bytes per pass from a 445-input snapshot
with SHA-256
`99d03de14f544f6a76d21ed147e62018873f1e2e8dfa2f4459830b69314432c2`.

The normal image build passes in 1,463.8 seconds. It produces an 8,761,216-byte
kernel ELF with SHA-256
`704d5edeaa0deff60c5e4fda006580ae0a10a07f1a7baccbfb480e5047165568`,
an 8,558,284-byte flat kernel with SHA-256
`ff22e539d81bfda855cb17a28e28d0d1275126847c0abe68e37896f4249eb062`,
and a 209,715,200-byte image with SHA-256
`ffdd01119e2b663da405627caacc8cd2af41f0854460321d938e4504b025e084`.

Four-CPU private-image runs pass the complete GUI frontier on e1000 and
RTL8139. Each serial log contains one exact 17-field Browser PASS marker and
no Browser failure marker. The e1000 run finishes in 270.1 seconds with
128,109 changed pixels, 9,826,602 AC97 frames, and 76,628 PC-speaker frames.
Its 51,644-byte log has SHA-256
`899455c43aa01058da5362a092f3c508755b0e072107b54cca523d1ea0f8e966`.
The RTL8139 run finishes in 270.0 seconds with 94,632 changed pixels,
9,846,181 AC97 frames, and 76,758 PC-speaker frames. Its 51,281-byte log has
SHA-256
`d58d1482ff7b7adae43dfa6fa1f86e6930933eba352ab6fad316ec7d474ae3a2`.

## Rejected alternatives

Changing the scale factor was rejected. Any fixed integer scale still merges
some distinct finite values and eventually exceeds the integer range.

Keeping integer AST nodes and reparsing the source during evaluation was
rejected. Tokens already own the literal value, and a second parser would
create two numeric grammars.

Adding special cases only to the self-test was rejected. Source-level cases
run through the page-script lexer, parser, and interpreter. Direct checks use
the same binary64 comparison, truth, formatting, division, and remainder
semantics.

## Consequences

Browser scripts can retain ordinary decimal fractions and exponents and use
binary64 comparison and truth behavior without an integer approximation.
The Browser now requires the private compiler's typed fixed-array path instead
of working around integer-only lowering. This does not add a host compiler or
change source ownership.

This is not a claim of complete ECMAScript number support. Hexadecimal,
binary, octal, numeric separators, full string-to-number syntax, exact
shortest decimal rendering, and general large or non-finite remainder
semantics remain outside this boundary. Private CupidC still needs
multidimensional floating arrays, fixed SIMD arrays, floating pointer types,
floating pointer dereference, and floating arrays embedded in structure or
class fields before those related forms are complete.

`TempleOS/` remains untouched reference material.
