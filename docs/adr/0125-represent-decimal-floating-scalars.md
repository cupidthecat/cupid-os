# Represent decimal floating scalars

- Status: Accepted
- Date: 2026-07-26

## Context

The shared CupidC frontend already transported `float` and `double` values,
but it could not publish a floating constant or convert between represented
integers and floating values. The first remaining strict production boundary
caused by that gap was `kernel/lang/cupidc_lex.c`. Its number scanner uses
`0.0`, `10.0`, integer-to-`double` casts, and mixed integer and `double`
arithmetic.

The compiler must build itself, so its decimal conversion cannot depend on a
host floating-point library.

## Decision

Add a public floating-constant expression and a matching linear IR
instruction. Store the target IEEE binary32 or binary64 bits in the existing
64-bit immediate field.

Parse decimal constants with integer arithmetic. The parser builds a bounded
integer ratio, calculates the target exponent and significand, and rounds once
to nearest with ties to even. It accepts zero and finite normal binary32 and
binary64 results. An `f` or `F` suffix selects `float`; an unsuffixed constant
selects `double`.

Keep hexadecimal constants, `long double`, subnormal results, and decimal
spellings outside the bounded precision or scale as explicit unsupported
boundaries. The parser reports which boundary was reached and publishes no
partial unit.

Represent these non-atomic conversions:

- signed and unsigned one-byte, two-byte, and four-byte integers to `float` or
  `double`
- `float` or `double` to signed one-byte, two-byte, or four-byte integers
- `float` or `double` to unsigned one-byte or two-byte integers
- `float` to `double`, `double` to `float`, and the usual arithmetic
  conversions used by mixed represented integer and floating arithmetic

Keep conversion from `float` or `double` to unsigned four-byte integers and
`bool` unsupported. Keep eight-byte integer conversions, floating comparisons,
truth testing, mixed integer and floating conditional arms, and floating
increment and decrement outside this change. Matching or mixed-width
floating conditional arms and the four arithmetic compound assignments keep
their existing x87 path.

Emit the new conversion paths through SSE. `MOVSS` and `MOVSD` move values
between the existing semantic slots and SSE registers around those
conversions. Existing floating arithmetic, calls, and returns keep their x87
path. Signed conversions use the matching SSE conversion instructions.
Unsigned four-byte conversion to floating point splits the input into its
upper 31 bits and low bit, converts both exact pieces, doubles the upper part,
and adds the low bit. This covers every `unsigned int` value without treating
the high bit as a sign bit.

## Rejected alternatives

Calling the host C library to parse constants was rejected because it would
add a host runtime dependency to a self-hosted compiler stage and could vary
with the host floating environment.

Using the compiler process's native floating type was rejected for the same
reason. The checked result must depend only on source bytes and target rules.

Using signed `CVTSI2SS` or `CVTSI2SD` directly for every unsigned four-byte
value was rejected because values at or above `0x80000000` would be
misinterpreted as negative.

Accepting `long double` metadata in IR or object emission was rejected. The
current target representation has no published `long double` ABI, so those
forms fail before output.

## Evidence

The `floating-scalars` frontend contract checks exact binary32 and binary64
bits for zero, one tenth, one half, one, and ten. It also checks assignment,
explicit casts, usual arithmetic conversion, rollback, constrained output,
same-job recovery, and diagnostics for hexadecimal, `long double`, excessive
precision, excessive scale, and values outside the supported normal range.

The IR contract checks literal instructions and every supported conversion
kind, repeats the lowering byte for byte, and rejects malformed integer,
unsigned four-byte, and `long double` metadata.

The object contract decodes and executes the emitted SSE path. Its boundary
cases include `0x7fffffff`, `0x80000000`, and `0xffffffff` for unsigned
four-byte conversion, signed byte and word inputs, unsigned byte and word
results, positive and negative truncation, and the production lexer
expression shape. Repeated emission is byte-identical.

The complete compiler source frontier now records:

| Source | Functions | Statements | Expressions | Block bindings | Initializers |
| --- | ---: | ---: | ---: | ---: | ---: |
| `toolchain/cupidc_ir.cc` | 210 | 6,356 | 57,554 | 810 | 295 |
| `toolchain/cupidc_emit.cc` | 224 | 5,920 | 50,859 | 715 | 359 |
| `toolchain/cupidc_frontend.cc` | 332 | 13,534 | 89,124 | 2,000 | 1,324 |

The same compiler head completes the full production preprocessing and object
command for unchanged `kernel/lang/cupidc_lex.c`. Ownership changes only after
the refreshed checked seed carries this compiler.

## Consequences

CupidC can now publish and emit the decimal floating scalar operations needed
by its production lexer. The public AST, IR, validator, and emitter agree on
the supported type boundary.

This does not claim general floating-point C support. Comparisons, truth,
mixed integer and floating conditional arms, increment and decrement,
subnormal literals, hexadecimal literals, `long double`, and the remaining
integer conversion directions are still open.
