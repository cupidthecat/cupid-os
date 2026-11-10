# ADR 0260: Fold static long-double arithmetic with target semantics

## Status

Accepted on 2026-08-10.

ADR 0265 records checked-seed carriage. The decision and evidence below
describe the source capability before that promotion and remain unchanged.

## Context

CupidC could already carry canonical x87 zero, subnormal, normal, infinity,
and NaN payloads through static initializers. It could also fold truth,
comparisons, conditional selection, and conversions involving `long double`.
An initializer that used `+`, `-`, `*`, or `/` on two long-double values still
stopped at `CTB000007`, even when the exact target result was known.

Using the host's floating-point type was not an option. Windows and Linux do
not promise the same `long double` format, and CupidC must produce the same
i386 x87 bytes on either host. The older binary32 and binary64 rounding helper
also has a deliberate workspace bound below 64 bits. Expanding that helper
would have mixed two representation rules and weakened its existing proof.

## Decision

Fold static `long double` addition, subtraction, multiplication, and division
in the frontend with integer arithmetic only. Ordinary C conversions still
choose the expression type before this path runs.

The new x87 packer treats a finite result as an unsigned 128-bit magnitude
times a signed power of two. It rounds once to the 64-bit explicit
significand using round-to-nearest, ties-to-even. Normal results use their
unbiased exponent. Subnormal results use the fixed `2^-16445` quantum, so
underflow is gradual and a rounded carry can become the minimum normal value.
Overflow produces the canonical signed x87 infinity. The existing binary32
and binary64 packer remains unchanged.

Addition aligns exact significands in a 128-bit workspace through an exponent
difference of 64. A subtraction just below a power-of-two boundary also
handles the difference-65 predecessor case, where the spacing changes across
the boundary. Larger differences cannot change the rounded result. Exact
cancellation produces positive zero under the default rounding rule, while
the ordinary signed-zero cases keep their C signs.

Multiplication forms the complete 64-by-64-bit product before packing.
Division builds a normalized quotient with 66 fractional steps. Its remainder
loop compares against the divisor gap before doubling, which avoids unsigned
overflow. Any remaining tail is jammed into the sticky bit before the common
x87 packer rounds.

The arithmetic helpers classify canonical x87 payloads before finite work.
They preserve signed infinity and zero where the operation requires it and
return one canonical quiet NaN for invalid operations. Division by zero,
infinity arithmetic, zero times infinity, and zero divided by zero therefore
stay inside the representation accepted by the frontend, Linear IR, and the
object emitter.

## Test-first findings

The first shared fixture expanded a long chain of power macros and reached the
preprocessor's storage limit before it reached arithmetic. The numerical
oracles stayed unchanged. Balancing the exact power construction reduced the
expansion pressure and exposed the intended capability failure.

The next frontend run stopped at
`/static-long-double-arithmetic.c:20:8` with `CTB000007` and
`static long-double arithmetic is outside this constant-data slice.` That was
the implementation red. The production change is confined to
`toolchain/cupidc_frontend.cc`.

The shared fixture has 80 exact x87 results: 16 addition and subtraction
cases, 16 multiplication and division cases, 16 rounding cases, 16 finite
edge cases, and 16 special-value cases. It covers ties on even and odd
significands, cancellation, signs, one third, the normal/subnormal boundary,
the minimum subnormal, underflow to both signed zeros, the largest finite
value, overflow, infinities, invalid operations, and a direct canonical quiet
NaN on either side of every arithmetic operator.

Frontend negatives reject nonconstant operands for each operator and retain
the established diagnostic location. A deep expression still receives the
nesting-limit diagnostic, and a valid expression succeeds afterward in the
same job. Repeating the complete fixture in a fresh job produces the same
initializer forest.

Linear IR receives 85 final initializer nodes and 80 list edges. It publishes
no function, instruction, argument-type, or file-assembly record for the
fixture. A forged pseudo-special initializer is rejected transactionally, and
the original unit lowers afterward. A separate runtime long-double constant
proves the floating-instruction validation seam: malformed payload metadata
is rejected, then the untouched unit lowers to the same IR fingerprint.

The object contract checks all twelve bytes of every result, including the two
zero padding bytes. The deterministic ELF32 object is 1,540 bytes with FNV-1a
fingerprint `8bf3a10b`. Its 768-byte `.rodata` section has fingerprint
`ca7e53bc`, and its 192-byte `.data` section has fingerprint `b2cac65f`. It has
six sections, six symbols, no relocation, and no `.text` section. A
one-byte-short output rolls back, an exact-fit output matches the canonical
image, and malformed initializer and runtime-instruction payloads both recover
in the same job.

The expanded frontend parses at the exact hosted source frontier of 445
definitions, 17,242 statements, 113,778 expressions, 2,565 block bindings,
and 1,547 initializers. Its deterministic self-host object contains 445
functions, 893,359 text bytes, and 1,058,536 total bytes, with text fingerprint
`851f24d7`. Only the frontend rows changed in either exact frontier.

The complete frontend, Linear IR, and object replay passes all 290 tests in
1,074.555 seconds. This includes the staged fixed-point checks; it does not
promote or replace the checked seed.

The shared fixture is an explicit prerequisite of all three contract objects,
so changing it rebuilds every consumer. Its ordinary `#ifndef` include guard
does not add a row to the active conditional-expression manifest, which
intentionally inventories only `#if` and `#elif` expressions.

## Integrated evidence

The arithmetic branch was applied after the parity-predicate source change
and the five-tool seed promotion. The first three focused integrated tests
passed in 53.492 seconds. A complete frontend, Linear IR, and object replay
then reached every arithmetic and fixed-point test, but five lexical frontier
checks read the previous generated audit. That run was not used to set new
locks.

Regenerating the audit first produced the authoritative control-flow totals:
23,346 `return`, 4,273 `for`, 2,770 `while`, 38,402 `if`, 4,818 `else`, and
3,040 `goto` occurrences. An independent scan of the 720 tracked inputs
reproduced those values and their file counts without a source-hash mismatch.
The focused frontier group then passed all five tests in 17.380 seconds.

The new fixture also completed two inventories that had outgrown older
locks. The active graph contains 724 inputs, including 293 headers. The
standalone non-Doom header sweep passes 160 of 162 inputs and retains the same
two expected failures. The checked contract publisher freezes 50 inputs. Its
three-file increase since the old lock consists of shared support headers
that the publisher already discovered through `toolchain/tests/*.h`.

The include-operand contract covers 2,422 direct includes across 691 C-family
inputs: 2,185 quoted and 237 angle forms, with no macro operand. The matching
line-directive contract covers the same 691 inputs and still finds no line
directive or numeric marker. These changes are consequences of the fixture
header and its three contract consumers, not new preprocessing syntax.

The final generated audit has active-source digest
`8d62b831b5086b8fc99918644b1e04e12101167e74fde1d67cb623da5794b12a`.
Its JSON is 2,600,505 bytes with SHA-256
`4e49b2d0c3965724c577c93ff29159fd8f611a57055a97a31a68cd887756374e`.
The generated Markdown summary is 12,218 bytes with SHA-256
`9b24b798076d3447d5446bc07e50f2c2126fbb4fb4e5dca2f073671dbc11f98f`.
Audit generation and the independent check both passed. The complete
build-graph replay passed all 75 tests in 800.415 seconds.

The canonical `make -C toolchain test` replay passed in 6,624.611 seconds.
Checked-seed bootstrap, both compiler stages, the byte-identical fixed point,
hosted runtime, publication and verification of all 20 artifacts, both
self-host frontiers, and the complete tool-link replay succeeded. Its
54,080-byte log contains 296 explicit passing cases and has SHA-256
`bd1fb693cfe4d9216791d62581024f339981842fd3a9853eb3046650466ca65e`.
The installed five-tool seed still passes its independent manifest and hash
verification; this decision does not promote a new seed.

After the header and audit locks were calibrated, the complete frontend,
Linear IR, and object replay passed all 290 tests in 1,142.370 seconds. The
56,264-byte log has SHA-256
`c651955e8da869590f171e2f503b5093323d7e798269348af4c32df562552c26`.
Independent exact-integer and rational checks also covered 120,000 addition
or subtraction cases, 80,000 multiplication cases, 80,000 division cases,
and one million raw 64-by-64 products without a mismatch.

The four-job root build passed in 585.218 seconds. It produced an 8,900,764-byte
`kernel.bin`, a 9,110,352-byte `kernel.elf`, and the 200 MiB `cupidos.img`.
Their SHA-256 digests are
`6c6d378dcc54a9ac191dacb0624693874e8b4334a611c278ec27a7d119960c0f`,
`6406261ff463dc8dd9039ac4e31e47ab9e88eff5403ad3eb59adbf0a51061765`,
and `115dbdc1d79f9916585a487b51e30bf728e955fbd02cb6199b66849da79ea2b5`.

A fresh 200 MiB image built from those boot and kernel artifacts passed the
complete e1000, four-vCPU frontier smoke in 542.796 seconds. The run brought
all four CPUs online, passed all 62 TLS checks, exercised the floating-point
and SIMD programs, completed the HomeFS batch test, survived the USB replug
sequence, and finished the audio checks without a panic. Its 160,300-byte log
has SHA-256
`58ec2b8fc729c8b171c7e74cd4a8b952ce2aa936f3225f708f6feef7e6c97e52`.

Two attempts against the existing persistent image were not accepted as
runtime evidence. The first stopped at a concurrent kernel panic during the
HomeFS stress sequence, although a focused replay of the same 328-cluster
write passed. A second full attempt reached the graphics test without a panic
but missed its immediate font-pixel check. Both paths passed in the fresh-image
run. These non-reproduced failures remain recorded as runtime flakiness; they
do not replace the successful clean-image proof.

## Rejected alternatives

Evaluating through the host's `long double` was rejected because the output
would depend on the build machine and its floating-point environment.

Routing x87 results through binary64 was rejected because it would lose eleven
bits of significand and erase most of the gradual-underflow range.

Widening the existing binary32 and binary64 packer was rejected. A separate
128-bit x87 packer keeps the old bounded helper and its tests intact.

Rewriting the fixture around easier values was rejected. The edge and rounding
oracles describe the capability CupidC needs, so only the fixture's expansion
shape changed.

## Consequences

Compiler-head CupidC can turn represented static long-double arithmetic into
exact target initializer data without runtime work or host floating point.
Runtime long-double arithmetic is unchanged.

This decision did not refresh the checked five-tool seed. Its 602-form,
247-mnemonic, 64-register catalogue and fingerprint `64429699` were the
baseline at that boundary. ADR 0265 later promoted this arithmetic path. A
final poisoned-host `make -j2 all` passed with exit 0 in 1,022.190 seconds.
The exact-size prerequisite accepted all nine artifacts before publishing the
209,715,200-byte image with SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.
The final four-vCPU E1000 and RTL8139 frontiers passed from that image. Both
used the partitioned USB fixture, `--smp 4`, `--cpu max`, SMP and frontier
runtime verification, a private image, and a 300-second phase timeout. E1000
exited 0 in 725.058 seconds with 103,673 changed framebuffer pixels, 29,608,822
AC97 frames at peak 25,600, and 76,784 PC speaker frames at peak 30,710.
RTL8139 exited 0 in 725.406 seconds with 106,151 changed pixels, 29,601,879
AC97 frames at peak 25,600, and 76,719 PC speaker frames at peak 31,501. Both
used a 640 by 480 framebuffer, and the image hash remained unchanged.
No production source changes owner, no host dependency is removed, and no
`.c` to `.cc` rename is due. `TempleOS/` remains read-only reference material.
