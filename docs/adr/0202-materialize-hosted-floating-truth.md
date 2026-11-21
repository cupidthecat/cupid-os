# ADR 0202: Materialize hosted floating truth

## Status

Accepted on 2026-08-01.

## Context

Hosted CupidC could transport, calculate with, and compare non-atomic
`float`, `double`, and automatic `long double` values. It still rejected a
floating operand in `!`, short-circuit logic, conditional selection, and
statement control. Runtime conversion to `_Bool` stopped at the same gap.

The stored representation could not stand in for C truth. Testing the raw
word of a `float` would make negative zero true. Testing a `double` or
`long double` snapshot handle would inspect its address rather than its
value. NaN also needs explicit unordered handling because an x86 unordered
comparison sets the zero flag.

## Decision

Treat a non-atomic floating value as a represented truth scalar in the
shared frontend and Linear IR. This covers unary `!`, `&&`, `||`, the
condition of `?:`, and the conditions of `if`, `while`, `do`, and `for`.
Explicit casts and assignment conversion from a represented floating value
to `_Bool` use the same rule. Atomic floating values keep a precise
unsupported diagnostic because the shared atomic load path does not yet
carry floating objects.

For `float` and `double`, the i386 emitter loads the semantic value into
XMM0, clears XMM1, and compares with `UCOMISS` or `UCOMISD`. For
`long double`, it loads the 80-bit value, pushes an exact zero with `FLDZ`,
compares with `FUCOMIP ST0, ST1`, and discards the surviving value with
`FSTP ST0`. The x87 sequence leaves the register stack at its starting
depth.

An ordered equality result means zero. Unary `!` and a zero branch use that
result directly. Conversion to `_Bool` uses the opposite ordered predicate.
A parity branch handles unordered input separately: NaN produces zero for
`!` and one for `_Bool`, so it remains true as C requires. Positive and
negative zero are false. Every finite nonzero value, subnormal, infinity,
and NaN is true.

Add `FLDZ` to the shared x86 catalogue instead of emitting private opcode
bytes. CupidC, CupidASM, and CupidDis therefore agree on the canonical
instruction and its `D9 EE` encoding.

## Evidence

The frontend, Linear IR, and object tests each failed at their prior
floating-truth boundary before the corresponding implementation changed.
The shared x86 test failed to compile before the public `FLDZ` mnemonic was
added.

The final source catalogue has 591 forms, 244 canonical mnemonics, 64
registers, and fingerprint `DBE77533`. Exact model and CLI checks encode and
decode `FLDZ` as `D9 EE`, round-trip it beside the existing long-double
comparison sequence, and reject an operand without publishing output.

The Linear IR fixture contains 13 functions and 96 instructions with
fingerprint `67E16BF9D4C6FBA3`. It pins one floating logical-not instruction
at each width, nine floating zero branches across all control forms, and
one floating-to-Boolean conversion at each width. Repeated lowering is
identical. Malformed result metadata, constrained storage, rollback, and
same-job recovery are also checked. A mutation retags a floating constant
with an existing `_Atomic float` type. Linear IR and the object boundary both
reject it with the specific unsupported-type diagnostic, preserve their
inputs and output buffers, and recover in the same job.

The deterministic object fixture contains nine functions in 855 text bytes
with fingerprint `6B4D7E90`, ten symbols including the null symbol, and no
relocations. Decoding finds eight `PXOR` instructions, four `UCOMISS`, four
`UCOMISD`, three `FLDZ`, three `FUCOMIP`, three register-stack `FSTP`, eleven
parity branches, eight `SETE`, and three `SETNE`. A constrained output fails
without leaving bytes, and the next emission matches the first object.

The hosted runtime source covers both signed zeros, finite values,
subnormals, infinities, quiet and signaling NaNs, logical normalization,
Boolean casts and assignment, short-circuit side effects, conditional
selection, every loop form, and repeated long-double tests that expose x87
stack growth. Its long-double case constructs the smallest positive 80-bit
subnormal directly in the twelve-byte target layout, rather than promoting a
binary64 value that would become normal at x87 range. Native CupidC emitted
the runtime and contract objects,
CupidASM built the startup object, and CupidLD linked a 76,052-byte static
i386 executable with SHA-256
`fefbb56f06d752681146cc6da2534c7d4ab445b4caa27911ef022a06d3f1d1e6`.
Its Linux run printed `runtime-ok`, wrote the exact expected file contents,
and left the deliberately missing path absent.

## Rejected alternatives

Testing raw stored bits was rejected because signed zero and snapshot
handles do not have C truth semantics.

Converting `long double` to `double` first was rejected because it would
discard target range and precision before the truth test.

Using only `SETNE` was rejected because x86 marks an unordered comparison as
equal as well as unordered. The parity flag must override that result so NaN
stays true.

Writing `D9 EE` directly in the emitter was rejected because it would leave
the assembler and disassembler with a different instruction authority.

## Consequences

The hosted implementation now lowers ordinary C floating truth and `_Bool`
conversion without invoking a host compiler, assembler, or floating library.
The change moves no production source owner and adds no host dependency.

Hexadecimal and subnormal floating literals, `long double` literals, nonzero
or floating static long-double initializers, integer conversions involving
`long double` other than `_Bool`, floating conversion to unsigned four-byte
integers, floating increment and decrement, general SIMD values, floating
atomics, and over-aligned floating objects remain open.
