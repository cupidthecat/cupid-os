# ADR 0199: Compare long-double values with x87

## Status

Accepted on 2026-08-01.

## Context

Hosted CupidC already transported non-atomic `long double` values in the
twelve-byte i386 object layout. It could convert among floating widths,
evaluate arithmetic, pass arguments, return results, and read variadic
values. The frontend still rejected every long-double comparison.

The existing comparison emitter used `UCOMISS` and `UCOMISD`, which cannot
compare the 80-bit value stored in a long-double object. Cupid's shared x86
catalogue also lacked the x87 instruction that compares two register-stack
values and publishes ordinary integer flags. Adding raw opcode bytes only to
the compiler would have left CupidASM and CupidDis behind.

## Decision

Add the i686 `FUCOMIP ST0, ST(i)` form to the shared x86 model. Its encoding is
`DF E8+i`; the first operand is fixed at `ST0`, and the second operand selects
the low opcode bits. CupidASM and CupidDis use the same form for assembly and
canonical disassembly.

Hosted CupidC accepts all six comparisons when both inputs are non-atomic
floating values and the usual arithmetic conversion produces `long double`.
This includes matching long-double operands and mixed pairs with `float` or
`double` in either source position. Atomic operands keep their existing
diagnostic.

The emitter loads the right value and then the left value, leaving the left
operand in `ST0`. It emits `FUCOMIP ST0, ST1`, which compares left with right
and pops the left value. It then emits `FSTP ST0` to discard the surviving
right value. The existing predicate and parity path returns a normalized
signed `int`. Signed zeros compare equal. If either input is unordered, only
`!=` returns true.

The Intel 64 and IA-32 Architectures Software Developer's Manual defines the
opcode, flag results, and single pop used by this sequence:
<https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>.

## Evidence

The frontend, Linear IR, object, and hosted-runtime tests were red on the
first long-double comparison with diagnostic `CTB000010`. The x86 contract
also failed to compile before the shared mnemonic existed. Later red runs
exposed each changed inventory before its checked value was updated.

The final x86 catalogue has 590 forms, 243 canonical mnemonics, 64 registers,
and fingerprint `74EC8312`. Exact encode and decode coverage checks
`FUCOMIP ST0, ST1` as `DF E9`. A public CLI round trip assembles that
instruction followed by `FSTP ST0` to `DF E9 DD D8`, disassembles both
instructions, and rejects `FUCOMIP ST1, ST0` without publishing output.
Checked-seed CupidC emits `toolchain/x86.cc` as a 134,984-byte production
object with SHA-256
`9f7a9e58fdd9a28d089e72ababf3248ef64a25e67ab2107c18bbe8cf3bc41c17`.

The Linear IR fixture has 30 functions and 192 instructions with fingerprint
`00EF66C81C2A4BD4`. It covers six matching and six mixed comparisons. The
2,861-byte object has fingerprint `76D70CA0`, 31 symbols, no relocations, 24
80-bit loads, 12 `FUCOMIP` instructions, and 12 register-stack `FSTP`
instructions.

The static i386 runtime covers lower, greater, and equal values, positive and
negative zero, mixed `float` and `double` inputs in both positions, and a
quiet NaN on either side. It repeats ordered and unordered comparisons 32
times to expose a leaked x87 value. The linked executable ended with
`runtime-ok`.

Frontend negatives keep atomic operands and integer-to-long-double
conversions outside this slice. They cover the integer on either side of the
comparison, preserve the previously parsed public graph, and recover in the
same job. A driver-level contract repeats the rejection through hosted and
Cupid-built CupidC, preserves existing output files, and then produces
matching valid recovery objects.

The final combined run passed 18 focused x86, CLI, frontend, Linear IR,
object, and Cupid-built runtime tests in 58.716 seconds. The static runtime
linked with CupidC, CupidASM, and CupidLD and ended with `runtime-ok`.

## Rejected alternatives

Comparing only the first eight object bytes was rejected because an i386
`long double` carries an 80-bit value, not a binary64 value with padding.

Converting both operands to `double` was rejected because it would discard
range and precision before the comparison.

Adding private compiler opcode bytes was rejected because the compiler,
assembler, and disassembler share one x86 authority.

Leaving the surviving x87 value in place was rejected because repeated
comparisons would overflow the register stack and corrupt later floating
work.

## Consequences

Hosted CupidC can compare represented non-atomic long-double values without a
host assembler or a second instruction table. CupidASM and CupidDis gain the
same `FUCOMIP` form. This changes no production source owner or host
dependency.

Long-double literals, nonzero or floating static initializers, integer
conversions involving `long double`, runtime floating truth, floating update,
floating atomics, and over-aligned floating objects remain open.
