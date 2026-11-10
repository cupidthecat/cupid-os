# ADR 0209: Correct libm x87 range reduction

## Status

Accepted on 2026-08-01.

## Context

The exponent and power paths in `kernel/cpu/libm.cc` split a value into its
rounded and fractional parts before `F2XM1` and `FSCALE`. The intended
fraction is `x - round(x)`. The active GNU spelling, `fsub %st, %st(1)`,
encoded as `DC E1` and produced the opposite value. A native x87 probe and an
independent GNU assembler probe confirmed that this was a source-level
operand-order bug, not a CupidC mnemonic bug. The wrong sign explains the
earlier `exp(1)` result near 1.47.

ADR 0207 added canonical `FSUB ST(1), ST(0)` and kept both GNU spellings
distinct. ADR 0208 promoted that support into the checked CupidASM, CupidC,
and CupidDis seed. The normal build could therefore accept the corrected
source without a host assembler or raw opcode escape.

## Decision

Change the seven aligned range-reduction statements to
`fsubr %st, %st(1)` or the escaped statement form. CupidC emits
`FSUB ST(1), ST(0)` as `DC E9`, which computes the intended fraction. Keep
the surrounding stack order, constants, algorithm, calling convention, and
source size unchanged. The old `fsub` spelling continues to emit `DC E1` and
stays covered as an explicit compatibility case.

Expand `/bin/feature15_libm.cc` from 22 to 29 checks. Seven focused cases
exercise `exp2`, `exp2f`, `exp`, `expf`, `pow`, `powf`, and the internal
exponent helper reached through `sinh`. The program prints a separate
`[feature15-x87]` summary before its complete summary. Its comparisons now
reject any nonzero scaled result, including the integer sentinel produced by
an invalid floating conversion, and its negative input uses the supported
unary-minus path.

The strong GUI frontier requires both zero-failure summaries,
`PASS feature15_libm`, and clean in-OS CupidC completion in that order.
The existing `feature13_double.cc` gate also restores its `exp(1)` check and
raises its tolerance-helper call count from nine to ten. Its comparison
helper rejects a negative integer-conversion sentinel, so NaN or an
out-of-range scaled error cannot pass as a small value.

## Evidence

The corrected source remains 43,736 bytes with 1,500 newline bytes and has
SHA-256
`baffe801c7573b8500c60251298a753f60732608d58443178be8ce9ab809ef93`.
A checked-seed compile and a hosted CupidC compile produced the same
16,164-byte ELF32 object with SHA-256
`c0911732361f2e1ea78aa778f834719ba12208cc2d9f0a312455a5e6a38a75b4`.
The focused compiles passed in 4.495 and 18.759 seconds.

Checked-seed CupidDis finds seven `FSUB ST(1), ST(0)` instructions with
`DC E9` at `.text` offsets `0x2E3`, `0x308`, `0x335`, `0x362`, `0x870`,
`0xA38`, and `0xB1B`. It finds no legacy `DC E1` range subtraction in the
active object. Source contracts require exactly four file-scope and three
statement forms, and reject either old active spelling.

The complete GUI smoke contract module first passed all 95 cases in 1.857
seconds, then passed again in 0.699 seconds after the feature13 check was
restored. It covers the new output order, rejects each missing marker, verifies
the seven active feature15 calls and source spellings, and requires the tenth
feature13 tolerance call and its sentinel guard.

Restoring `exp(1)` exposed a missing `exp` binding in the private-call test
harness. The first focused source compile failed with `Unresolved symbol:
exp`. Adding the same typed kernel binding used for the other libm functions
made that selector pass in 0.484 seconds. A final run of the complete 95-test
GUI module passed in 0.471 seconds, and all five checked seed tools verified.

The first strict frontier reached the final object checks and reported that
its aggregate lock was stale. The promoted seed produces 3,721,392 bytes for
each 155-object pass rather than 3,719,100. An independent sum of the normal
production objects confirmed the new total. It also confirmed the current
135,136-byte `toolchain/x86.o` with SHA-256
`37711fd5fdabfd1e70e8dd469bc6182c5b9167269a27e46c24dca8ced5ffd23c`.
After those exact locks were updated, an isolated strict run compiled all 445
frozen inputs twice, reproduced every object, and passed in 1,342.598 seconds.
The snapshot has SHA-256
`4b4dbd802d8faf0cdf9bc1b2749ab7cddf4c4635dafdea4ac171c37a96449a92`.

The regenerated audit retains 717 active inputs, 449 transforms, 254 feature
requirements, and 25 classified unreachable files. Its active-source digest
is `4cc621b69736f3b9f4c22565a8f4ec026bb775bb311254a6c7f9b1b1dd5f7265`.
The 2,546,938-byte JSON has SHA-256
`fbd3aabb36e73aea1ee332e7c7413614b6b52bd0ffdec090e9cdcfc5691bb22e`,
and the 12,136-byte summary has SHA-256
`956a34695080089d697307c2c672966501f5ccebf8a5d44a5f8c331022d8447c`.
Read-only regeneration passed in 64.4 seconds.

The normal root and partitioned-image build passed in 1,450.715 seconds. It
produced an 8,723,876-byte kernel ELF with SHA-256
`096a260ec1369afa197de2efca5044230d5ae600741e7aa4d3deba8d654f4d89`,
an 8,521,112-byte flat kernel with SHA-256
`358484dea68d170bec6b43ed88599e69e29551a9567e40358ae4e40f66ff5800`,
and a 209,715,200-byte image with SHA-256
`606d5779d8fd96af60e0aadb66cfa85b01af354a705a2630e91b50aeb1fbea40`.
The existing 33,554,432-byte partitioned USB fixture verified with SHA-256
`057e0c86874090c99095f0558e9fa604bd7f1929f4da357da2c1baca949bb2bb`.

Four-CPU private-image QEMU runs passed the complete GUI frontier with both
production NICs. The e1000 run finished in 235.259 seconds; its 46,370-byte
serial log has SHA-256
`bf502bbf9fc5709d5885e221bd8857e994f06d7fed5dc0b168b40fd969148f72`.
The RTL8139 run finished in 232.832 seconds; its 51,063-byte log has SHA-256
`065b30e44347fffdbcdfb2705712d98947778640f8bb591dc064a01a201bb1e5`.
Both logs contain the ten-call feature13 marker, seven successful x87 range
checks, all 29 successful libm checks, and clean JIT completion. Neither log
contains a panic or failure marker. The same runs cover SMP startup, both
audio paths, input, storage, networking, framebuffer changes, crypto, and the
rest of the ten-command frontier.

The complete 68-test build-graph audit passed in 566.423 seconds after the
runtime evidence was recorded. It required no further ownership, lexical, or
generated-audit lock changes.

## Rejected alternatives

Changing CupidC's meaning for `fsub %st, %st(1)` was rejected because GNU
`as` and CupidC already agree on its bytes. Silent remapping would make the
compiler disagree with its source language and would break compatibility for
existing inputs.

Reordering the x87 stack or rewriting the functions in ordinary C was
rejected. The subtraction mnemonic alone explains the observed sign error,
and changing more code would make the correction harder to review without
improving the language or ABI.

Testing only the four public exponent wrappers was rejected. The same
range-reduction sequence appears in the two power statements and the
internal exponent helper, so the guest contract exercises all seven paths.

Keeping the 22-check smoke was rejected because it deliberately skipped the
known-bad `exp(1)` result and did not cover base-two exponents, float
wrappers, fractional powers, or the internal helper.

## Consequences

The active exponent and power paths now compute the intended fractional
remainder through the checked Cupid toolchain. `kernel/cpu/libm.cc` remains
owned by the normal CupidC recipe, and this correction adds no host compiler,
assembler, linker, or binary utility dependency.

The compatibility spelling remains supported, so this is a source repair
rather than a compiler semantic change. General GNU assembly and the wider
libm accuracy surface remain open. `TempleOS/` remains untouched reference
material.
