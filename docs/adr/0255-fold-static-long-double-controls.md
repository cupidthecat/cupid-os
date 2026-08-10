# ADR 0255: Fold static long-double controls

## Status

Accepted on 2026-08-09.

## Context

CupidC could place bounded decimal `long double` values in static objects and
convert those values to or from represented integers. It still rejected
long-double truth, comparisons, short-circuit logic, conditional selection,
and conversion to or from `float` and `double` during static initialization.

These forms share one requirement: the frontend must understand the target
floating representation without asking the host to evaluate it. Mixed
comparisons and conditional arms also need the usual C conversions before a
constant result can be selected.

## Decision

The static evaluator decodes binary32, binary64, and the represented x87
payload into one private target-value form. The decoded value carries its
sign, classification, unbiased exponent, and normalized significand. This is
the only internal seam used for floating truth and ordered magnitude
comparison.

Both signed zeros are false. Every other represented floating value is true.
The evaluator implements all six comparison operators with C signed-zero and
unordered behavior. Sign and exponent decide most ordered cases. Values with
the same exponent are compared after their 24-bit, 53-bit, or 64-bit
significands have been aligned to the same high bit. The comparison evaluator
still requires both typed operands to have the same floating kind, which keeps
the frontend's usual-conversion nodes observable instead of hiding a missing
conversion.

Static `&&` and `||` evaluate the right operand only when its value is needed.
The conditional operator evaluates only its selected arm. An unsupported
long-double arithmetic expression in an unselected path therefore has no
effect, while the same expression in a selected path keeps the existing
diagnostic.

Finite binary32 and binary64 values widen to exact x87 payloads. Their decoded
significand is shifted into the 64-bit explicit x87 significand, and the
unbiased exponent receives the x87 bias. This includes binary32 subnormal
results produced by the existing static arithmetic evaluator. Signed zero is
preserved. Infinity and NaN cannot be widened through this static path and
receive a focused finite-source diagnostic.

A represented finite `long double` narrows to binary32 or binary64 with
integer-only round-to-nearest, ties-to-even packing. Overflow produces the
target infinity, underflow produces the correctly rounded normal, subnormal,
or signed zero, and the final initializer keeps its destination type and
qualifiers. Static mixed integer, enum, and long-double comparisons or
conditional arms reuse the exact integer-to-x87 conversion from ADR 0254.

The folded values become final initializer records. Linear IR publishes no
runtime functions or instructions for the shared fixture. Object emission
uses the existing integer, binary32, binary64, and padded x87 data writers,
including the ordinary `.bss`, `.data`, and `.rodata` policy.

Static long-double addition, subtraction, multiplication, and division remain
unsupported. Hexadecimal or subnormal long-double literals, decimal ratios
beyond the bounded parser, atomic floating conversion, and infinity or NaN
widening to `long double` also remain outside this boundary.

## Evidence

One shared source fixture drives the frontend, Linear IR, and object contracts.
It contains nine static objects, 62 initializer nodes, and 53 list edges. The
fixture covers signed-zero truth, all six comparisons, same-exponent x87
significand ordering, mixed floating widths, represented integers and enums
through `ULLONG_MAX`, short-circuit nonselection, selected conditional arms,
finite width conversion in both directions, and mutable selected positive and
negative zero.

The binary32 expression `(1e-19f * 1e-19f)` produces a subnormal before it is
widened. Its exact x87 payload is `d9c7dc0000000000/3f80`. The frontend and IR
oracles also require an empty retained expression forest and no runtime IR.
The object oracle checks every integer and floating byte, both x87 padding
bytes, section and symbol order, zero relocations, deterministic repeated
emission, an output-limit failure, and byte-identical recovery in the same
job. A separately compiled source with already-folded values supplies an
independent object comparison for the control results.

The first focused truth test failed on the old long-double truth diagnostic.
The broadened control fixture then reached the missing `float` to
`long double` usual conversion in a mixed comparison. A later diagnostic test
found that infinity widening still used the older generic message. A decimal
`1e4000L` probe failed earlier in the bounded literal parser, so it remains a
parser boundary instead of being replaced by a weaker test.

The final focused frontend selector passed one test in 14.781 seconds, the
focused Linear IR selector passed one test in 18.267 seconds, and the focused
object selector passed one test in 27.509 seconds. The complete frontend
module passed all 95 tests in 14.210 seconds. The complete Linear IR module
passed all 83 tests in 13.743 seconds. The calibrated self-host frontier
selector passed one test in 24.317 seconds. The complete object module passed
all 109 tests in 1,031.116 seconds.

The final frontend run first exposed the new header in two exact frontier
locks: 161 active non-Doom headers, of which 159 pass. After those lock-only
corrections, the complete module passed with no failures, errors, or skips.

The active-source audit regenerated in 63.717 seconds and passed its stale
check in 62.384 seconds. Its digest is
`e3cf93926ea6f531c37b4a3dcb09f85edab3cc1094abbc6e729aeaf55154674b`.
It records 723 active inputs, 25 accounted unreachable files, 447 transforms,
255 requirements, and 81,615 occurrences across 12 C control features. The
Toolchain contract cohort contains 19 files and 154,042 checked-source lines;
Toolchain core contains 33 files and 87,018 lines.

The complete Toolchain proof passed in 3,146.419 seconds. Checked-seed
bootstrap, both compiler stages, byte identity, the hosted runtime, frozen
input verification, publication, and verification of all 20 artifacts passed.
The normal OS build then passed in 1,513.347 seconds and produced the boot
sector, assembly and C objects, both kernel links, generated symbols, flattened
kernel, deterministic ISO, and 200 MB image with `/hello.iso` staged.

The final private four-CPU e1000 smoke passed in 67.788 seconds. RDRAND, every
CPU, the FPU smoke, all 62 TLS checks, e1000 and DHCP, the desktop and terminal,
and the ordered feature13 compile, final PASS, and JIT-completion markers were
present. No rejected panic, storage, SMP, or NIC marker appeared. The
35,988-byte log has SHA-256
`e4985b389f800e3b64816d7688e1b3a9e335b2423f945e4b428a2f55f9dbdd0e`.

## Rejected alternatives

Host floating arithmetic was rejected because the static object would depend
on the bootstrap host's floating formats and evaluation rules.

Comparing raw payload integers was rejected because signed ordering, signed
zero, NaN, and different floating precisions do not share one unsigned bit
ordering.

Evaluating both operands of `&&` and `||`, or both conditional arms, was
rejected because it would diagnose expressions that C does not evaluate.

Keeping finite width conversion blocked was rejected because mixed
long-double comparisons and conditional arms require those conversions as
part of their ordinary C typing.

## Consequences

Compiler-head CupidC can fold static long-double control expressions and
finite width conversions without host floating behavior or runtime work. ADR
0258 carries this capability in the checked seed.

No production source changes owner, and no `.c` to `.cc` rename is due.
Issue #25 remains open for static long-double arithmetic, widening infinity or
NaN from `float` or `double` to `long double`, more literal forms, atomics,
other C11 gaps, and staged self-hosting.
`TempleOS/` remains untouched reference material.
